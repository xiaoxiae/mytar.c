#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <err.h>


#define LR_SIZE 512     // size of a single logical record
#define SIZE_OCTETS 12  // size of the 'size' value in the logical record

struct Header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[SIZE_OCTETS];
	char mtime[12];
	char chksum[8];
	char typeflag[1];
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
};


/* Convert an octal string to an integer. */
long int oct_to_int(char* string, int length) {
	long int number = 0;

	for (int i = 0; i < length - 1; i++)
		number = number * 8 + (string[i] - '0');

	return number;
}

/* Return 1 if the header is full of zeroes, else 1. */
int is_header_empty(struct Header* header) {
	for (int i = 0; i < LR_SIZE; i++)
		if (((char*)header)[i] != 0)
			return 0;
	return 1;
}

void fseek_with_error(FILE *fp, int offset, int origin) {
	if (fseek(fp, offset, origin))
		errx(2, "error when performing seek on the file.");
}
		
int ftell_with_error(FILE *fp) {
	int result = ftell(fp);
	if (result == -1)
		errx(2, "error when determining file.");
	return result;
}

void *malloc_with_error(size_t bytes) {
	void *result = malloc(bytes);
	if (!result)
		errx(2, "failed to allocate memory, exiting.");
	return result;
}

void *calloc_with_error(size_t num, size_t size) {
	void *result = calloc(num, size);
	if (!result)
		errx(2, "failed to allocate memory, exiting.");
	return result;
}

/* Get the size of the file using fseeks and an ftell. */
int get_file_size(FILE *fp) {
	fseek_with_error(fp, 0, SEEK_END);
	int fs = ftell_with_error(fp);
	fseek_with_error(fp, 0, SEEK_SET);

	return fs;
}

/* Return 1 and mark the file in the pargs */
int file_in_pargs(int pargs_counter, char** pargs, int* used_pargs, char* name) {
	for (int i = 0; i < pargs_counter; i++)
		if (strcmp(name, pargs[i]) == 0 && !used_pargs[i])
			return (used_pargs[i] = 1);
	return 0;
}

/* An enum of currently supported actions. */
enum actions {
	none,
	list,
	extract
};


int main(int argc, char *argv[]) {
	// prevent buffering to not mess with printf and errx/warnx order
    setbuf(stdout, NULL);

	if (argc == 1)
		errx(2, "invalid invocation.");

	// plain arguments variables
	int pargs_counter = 0;
	char** pargs = malloc_with_error(sizeof(char*) * argc);

	// information regarding tar options
	enum actions action = none;
	char* file;
	bool verbose = false;

	// parse the arguments
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {
				case 'f':
					if (i + 1 == argc)
						errx(2, "option requires an argument -- 'f'");

					file = argv[++i];
					break;

				case 't':
					action = list;
					break;

				case 'x':
					action = extract;
					break;

				case 'v':
					verbose = true;
					break;

				default:
					errx(2, "flag %s not recognized.", argv[i]);
			}
		}
		else
			pargs[pargs_counter++] = argv[i];
	}

	if (action == none)
		errx(2, "invalid invocation.");

	FILE *fp;

	if ((fp = fopen(file, "r")) == NULL)
		errx(2, "%s: Cannot open: No such file or directory", file);

	// to determine, if we read past the end or not
	int fs = get_file_size(fp);

	bool first_record = true;         // for checks on the first LR
	bool prev_header_empty  = false;  // whether the LR was empty
	int lr_count = 0;                 // number of read records

	// remember which file names were in the archive and which were not (for warnings)
	int* used_pargs = calloc_with_error(pargs_counter, sizeof(int));
	
	struct Header* header = malloc_with_error(sizeof(char) * LR_SIZE);
	while (true) {
		// read the next logical record
		int read = fread(header, sizeof(char), LR_SIZE, fp);

		if (read == 0) {
			// warn when ending on a singular LR
			if (prev_header_empty)
				warnx("A lone zero block at %d", lr_count);
			break;
		}
		else if (read != LR_SIZE)
			errx(2, "Block %d incomplete, exiting", lr_count);

		lr_count++;

		// check magic bits of the first logical record
		if (first_record) {
			if (strcmp("ustar  ", header->magic) != 0) {
				warnx("This does not look like a tar archive");
				errx(2, "Exiting with failure status due to previous errors");
			}

			first_record = false;
		}

		if (is_header_empty(header)) {
			// break on two empty LRs in a row
			if (prev_header_empty)
				break;

			prev_header_empty = true;
			continue;
		}
		else {
			// warn if the previous logical record was zero
			if (prev_header_empty)
				warnx("A lone zero block at %d", lr_count);

			prev_header_empty  = false;
		}

		// exit if the archive contains anything but regular files
		if (header->typeflag[0] != '0')
			errx(2, "Unsupported header type: %d", header->typeflag[0]);

		// if there are either no pargs or the file is one of them, do something
		// with the file (print, extract...)
		bool name_found = (
			pargs_counter == 0
			|| file_in_pargs(pargs_counter, pargs, used_pargs, header->name)
		);

		if (name_found && (action == list || (action == extract && verbose)))
			printf("%s\n", header->name);

		// get the number of logical records in the file
		int header_size = oct_to_int(header->size, SIZE_OCTETS);
		long int header_offset = (header_size + (LR_SIZE - 1)) / LR_SIZE;
		lr_count += header_offset;

		if (action == list) {
			fseek_with_error(fp, header_offset * LR_SIZE, SEEK_CUR);

			// check if we didn't accidentaly seek past the end of the file
			if (ftell(fp) > fs) {
				warnx("Unexpected EOF in archive");
				errx(2, "Error is not recoverable: exiting now");
			}
		}
		else if (action == extract) {
			FILE *fout = fopen(header->name, "w");
			char* buffer = malloc_with_error(sizeof(char*) * LR_SIZE);

			// read from *fp, one LR_SIZE at a time
			// should be IO-friendly-enough
			for (int i = 0; i < header_offset; i++) {
				int read = fread(buffer, sizeof(char), LR_SIZE, fp);

				if (read != LR_SIZE) {
					warnx("Unexpected EOF in archive");
					errx(2, "Error is not recoverable: exiting now");
				}

				fwrite(buffer, sizeof(char), LR_SIZE, fout);
			}

			fclose(fout);
		}
	}
	
	// check, if we found all filenames in the archive
	bool found = true;
	for (int i = 0; i < pargs_counter; i++) {
		if (used_pargs[i] == 0) {
			warnx("%s: Not found in archive", pargs[i]);
			found = false;
		}
	}

	if (!found)
		errx(2, "Exiting with failure status due to previous errors");
}
