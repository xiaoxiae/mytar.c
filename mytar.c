#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Do an fseek but handle the return value. */ 
void fseek_with_error(FILE *fp, int offset, int origin) {
	if (fseek(fp, offset, origin))
		errx(2, "error when performing seek on the file.");
}
		
/* Do an fseek but handle the return value. */ 
int ftell_with_error(FILE *fp) {
	int result = ftell(fp);

	if (result == -1)
		errx(2, "error when determining file.");
	else
		return result;
}


int main(int argc, char *argv[]) {
	// prevent buffering to not mess with printf and errx/warnx order
    setbuf(stdout, NULL);

	if (argc == 1)
		errx(2, "invalid invocation.");

	char* file;

	// allocate enough space for plain arguments (might be more than necessary)
	char** pargs = malloc(sizeof(char*) * argc);
	if (!pargs)
		errx(2, "failed to allocate memory, exiting.");

	int pargs_counter = 0;

	// mytar action flag
	char action = ' ';

	// parse the arguments
	for (int i = 1; i < argc; i++) {
		// get f and its argument
		if (strcmp(argv[i], "-f") == 0) {
			if (i + 1 == argc)
				errx(2, "option requires an argument -- 'f'");

			file = argv[++i];
			continue;
		}

		// check for flags
		if (argv[i][0] == '-') {
			if (argv[i][1] != 't')
				errx(2, "invalid invocation.");
			else
				action = argv[i][1];

			continue;
		}

		// save plain arguments
		pargs[pargs_counter++] = argv[i];
	}

	// currently only 't' is supported
	if (action == 't') {
		FILE *fp;

		// attempt to open the file, issue an error if something went awry
		if ((fp = fopen(file, "r")) == NULL)
			errx(2, "%s: Cannot open: No such file or directory", file);

		// get file size to determine, if we read past
		fseek_with_error(fp, 0, SEEK_END);
		int fs = ftell_with_error(fp);
		fseek_with_error(fp, 0, SEEK_SET);

		int prev_header_empty  = 0;  // whether the last l record was empty
		int header_count = 0;        // how many logical records have we read?

		// note, which file names were in the archive and which were not
		// we're using calloc, since we want the array to contain zeros
		int* used_pargs = calloc(pargs_counter, sizeof(int));
		if (!used_pargs)
			errx(2, "failed to allocate memory, exiting.");
		
		// read logical records, one by one
		struct Header* header = malloc(sizeof(char) * LR_SIZE);

		while (1) {
			// read the next logical record
			int read = fread(header, sizeof(char), LR_SIZE, fp);
			header_count++;

			// we haven't read anything
			if (read == 0) {
				// crash on a singular empty logical record being read
				if (prev_header_empty)
					warnx("A lone zero block at %d", header_count - 1);

				// break on both missing (since that's apparently fine)
				break;
			}

			// check for empty/non-empty header
			if (is_header_empty(header)) {
				// break on two empty logical records in a row
				if (prev_header_empty)
					break;

				prev_header_empty  = 1;
				continue;
			}
			else {
				// warn if the previous logical record was zero
				if (prev_header_empty)
					warnx("A lone zero block at %d", header_count);

				prev_header_empty  = 0;
			}

			// exit if the archive contains anything but regular files
			if (header->typeflag[0] != '0')
				errx(2, "Unsupported header type: %d", header->typeflag[0]);

			// if we parsed some file names, check if they match any of the
			// plain args first
			if (pargs_counter != 0) {
				for (int i = 0; i < pargs_counter; i++) {
					if (strcmp(header->name, pargs[i]) == 0 && !used_pargs[i]) {
						used_pargs[i] = 1;
						printf("%s\n", header->name);
						break;
					}
				}
			}
			else
				printf("%s\n", header->name);

			// the number of logical records in the file (to skip)
			int header_size = oct_to_int(header->size, SIZE_OCTETS);
			long int header_offset = (header_size + (LR_SIZE - 1)) / LR_SIZE;
			header_count += header_offset;

			// skip contents
			fseek(fp, header_offset * LR_SIZE, SEEK_CUR);

			if (ftell(fp) > fs) {
				warnx("Unexpected EOF in archive");
				errx(2, "Error is not recoverable: exiting now");
			}
		}
		
		// check, if we found all filenames in the archive
		int found = 0;
		for (int i = 0; i < pargs_counter; i++) {
			if (used_pargs[i] == 0) {
				warnx("%s: Not found in archive", pargs[i]);
				found = 1;
			}
		}

		// if not, crash
		if (found)
			errx(2, "Exiting with failure status due to previous errors");
	}
	else
		errx(2, "invalid invocation.");
}
