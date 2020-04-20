// # My tar.
// A solution to the C programming language course assignment.
// - https://github.com/devnull-cz/c-prog-lang/blob/master/getting-credits/2020/assignment.txt
//
// ## Resources
// - https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html#tag_20_92_13_06
// - https://www.gnu.org/software/tar/manual/html_node/Standard.html
// - https://www.gnu.org/software/tar/manual/html_section/tar_67.html
//
// ## Notes to myself
// - C stdio file API must be used 
// - use of getopt(3) not allowed

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGICAL_RECORD_SIZE 512

// the TAR Header structure
struct Header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
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


/* Convert AN OCTAL string to an integer. */
long int oct_to_int(char* string, int length) {
	long int number = 0;
	for (int i = 0; i < length - 1; i++) number = number * 8 + (string[i] - 48);
	return number;
}

/* Return 1 if a logical header is full of zeroes, else 1. */
int is_header_empty(char* header) {
	for (int i = 0; i < LOGICAL_RECORD_SIZE; i++)
		if (header[i] != 0)
			return 0;

	return 1;
}

int main(int argc, char *argv[]) {
	if (argc == 1) {
		printf("mytar: invalid invocation.");
		exit(2);
	}

	char* file;

	// allocate enough space for plain arguments
	char** plain_arguments = malloc(sizeof(char*) * argc);
	int argument_counter = 0;

	// mytar action (currently only -t)
	char action = ' ';

	// parse the arguments
	for (int i = 1; i < argc; i++) {
		// get the f option
		if (strcmp(argv[i], "-f") == 0) {
			if (i + 1 == argc) {
				printf("mytar: option requires an argument -- 'f'");
				exit(2);
			} else file = argv[++i];
			continue;
		}

		// check flags
		if (argv[i][0] == '-') {
			// currently the only supported flag
			if (argv[i][1] != 't') {
				printf("mytar: invalid invocation.");
				exit(2);
			} else action = argv[i][1];
			continue;
		}

		// save plain arguments
		plain_arguments[argument_counter++] = argv[i];
	}

	if (action != 't') {
		printf("mytar: invalid invocation.");
		exit(2);
	}

	FILE *fp;

	// attempt to open the file, crash if it didn't work
	if ((fp = fopen(file, "r")) == NULL) {
		printf("mytar: %s: Cannot open: No such file or directory\n", file);
		exit(2);
	}

	int last_header_empty = 0;
	int header_count = 0;

	int* used_plain_arguments = malloc(sizeof(int) * argument_counter);
	for (int i = 0; i < argument_counter; i++) used_plain_arguments[i] = 0;
	
	// read headers, one by one
	struct Header* header = (struct Header*)malloc(sizeof(char) * 512);
	while (1) {
		size_t read = fread(header, sizeof(char), 512, fp);

		if (read == 0) {
			// crash on singular empty header
			if (last_header_empty) {
				printf("mytar: Unexpected EOF in archive\n");
				printf("mytar: Error is not recoverable: exiting now\n");
			}

			// break on both missing TODO
			break;
		}

		// read empty headers
		if (is_header_empty((char*)header)) {
			// exit on two empty headers in a row
			if (last_header_empty) break;
			else last_header_empty = 1;
			header_count++;
			continue;
		} else {
			// lone empty headers
			if (last_header_empty) printf("mytar: A lone zero block at %d", header_count);
			last_header_empty = 0;
		}

		// crash if the archive contains anything but regular files
		if (header->typeflag[0] != '0') {
			printf("mytar: Unsupported header type: %d", header->typeflag[0]);
			exit(2);
		}

		// if we parsed some file names, check if they match first
		if (argument_counter != 0) {
			for (int i = 0; i < argument_counter; i++) {
				if (strcmp(header->name, plain_arguments[i]) == 0 && !used_plain_arguments[i]) {
					used_plain_arguments[i] = 1;
					printf("%s\n", header->name);
					break;
				}
			}
		} else
			printf("%s\n", header->name);


		// the number of logical records in the file
		long int header_offset = (oct_to_int(header->size, 12) + 511) / 512;
		header_count += header_offset + 1;

		// skip contents
		fseek(fp, header_offset * 512, SEEK_CUR);
	}
	
	int found = 0;
	for (int i = 0; i < argument_counter; i++) {
		if (used_plain_arguments[i] == 0) {
			printf("mytar: %s: Not found in archive\n", plain_arguments[i]);
			found = 1;
		}
	}

	if (found) {
		printf("mytar: Exiting with failure status due to previous errors\n");
		exit(2);
	}

	exit(0);
}
