#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <err.h>

// the size of a single logical record
#define LR_SIZE 512
#define SIZE_OCTETS 12

// the header logical record structure
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

	// allocate enough space for plain arguments (might be more than necessary)
	int pargs_counter = 0;
	char** pargs = malloc_with_error(sizeof(char*) * argc);

	// information regarding tar options
	enum actions action = none;
	char* file;
	bool verbose = false;

	// parse the arguments
	for (int i = 1; i < argc; i++) {
		// if it starts with a '-', it's a flag
		// currently only a few flags are supported
		if (argv[i][0] == '-') {
			switch(argv[i][1]) {
				// file option
				case 'f':
					if (i + 1 == argc)
						errx(2, "option requires an argument -- 'f'");

					file = argv[++i];
					break;

				// action options
				case 't':
					action = list;
					break;

				case 'x':
					action = extract;
					break;

				// verbosity flag
				case 'v':
					verbose = true;
					break;

				default:
					errx(2, "flag %s not recognized.", argv[i]);
			}
			continue;
		}

		// save plain arguments
		pargs[pargs_counter++] = argv[i];
	}

	// if no action was parsed, don't do anything
	if (action == none)
		errx(2, "invalid invocation.");

	FILE *fp;

	// attempt to open the file
	if ((fp = fopen(file, "r")) == NULL)
		errx(2, "%s: Cannot open: No such file or directory", file);

	// to determine, if we read past the end or not
	int fs = get_file_size(fp);

	bool prev_header_empty  = false;  // whether the last l record was empty
	int header_count = 0;        // how many logical records have we read?

	// note, which file names were in the archive and which were not
	// we're using calloc, since we want the array to contain zeros
	int* used_pargs = calloc_with_error(pargs_counter, sizeof(int));
	
	// read logical records, one by one
	struct Header* header = malloc_with_error(sizeof(char) * LR_SIZE);

	// for checking the first logical record (whether we're reading a tar file or not)
	bool first_record = true;

	while (1) {
		// read the next logical record
		int read = fread(header, sizeof(char), LR_SIZE, fp);
		header_count++;

		// we haven't read anything
		if (read == 0) {
			// crash on a singular empty logical record being read
			if (prev_header_empty)
				warnx("A lone zero block at %d", header_count - 1);

			// break on both missing (since that's fine)
			break;
		}

		// check magic bits of the first logical record
		if (first_record) {
			if (strcmp("ustar  ", header->magic) != 0) {
				warnx("This does not look like a tar archive");
				errx(2, "Exiting with failure status due to previous errors");
			}

			first_record = false;
		}

		// check for empty/non-empty header
		if (is_header_empty(header)) {
			// break on two empty logical records in a row
			if (prev_header_empty)
				break;

			prev_header_empty = true;
			continue;
		}
		else {
			// warn if the previous logical record was zero
			if (prev_header_empty)
				warnx("A lone zero block at %d", header_count);

			prev_header_empty  = false;
		}

		// exit if the archive contains anything but regular files
		if (header->typeflag[0] != '0')
			errx(2, "Unsupported header type: %d", header->typeflag[0]);

		// if there are either no pargs or the file is one of them, do something
		// with the file (print, extract...)
		int name_found = (
			pargs_counter == 0
			|| file_in_pargs(pargs_counter, pargs, used_pargs, header->name)
		);

		// only print the name if either -t or verbose -x is specified
		if (name_found && (action == list || (action == extract && verbose)))
			printf("%s\n", header->name);

		// get the number of logical records in the file (to skip or write)
		int header_size = oct_to_int(header->size, SIZE_OCTETS);
		long int header_offset = (header_size + (LR_SIZE - 1)) / LR_SIZE;
		header_count += header_offset;

		if (action == list) {
			fseek_with_error(fp, header_offset * LR_SIZE, SEEK_CUR);

			// check if we didn't accidentaly seek past the file size
			if (ftell(fp) > fs) {
				warnx("Unexpected EOF in archive");
				errx(2, "Error is not recoverable: exiting now");
			}
		}
		else if (action == extract) {
			FILE *fout = fopen(header->name, "w");

			// read from *fp LR_SIZE by LR_SIZE
			char* buffer = malloc_with_error(sizeof(char*) * LR_SIZE);

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
	bool found = false;
	for (int i = 0; i < pargs_counter; i++) {
		if (used_pargs[i] == 0) {
			warnx("%s: Not found in archive", pargs[i]);
			found = true;
		}
	}

	// if not, report errors and exit
	if (found)
		errx(2, "Exiting with failure status due to previous errors");
}
