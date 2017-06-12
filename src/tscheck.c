#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

#define TS_SIZE 188
#define TS_SIZE_ONCE 188000
// about 54MB of data
#define TS_LIMIT 300000

void help() {
	printf("tscheck utility for TS stream validation\n");
	printf("Usage:\n");
	printf("	-f filename	input file with TS stream\n");
	printf("	-p		check pattern (see tsgen.c for more info)\n");
	printf("	-c		check TS header counters\n");
}

int last_pattern = 0;

int checkts(unsigned char * pkt, int64_t off, int64_t *success_pattern, int64_t *fail_pattern) {
	int i = 0;
	int64_t foff = 0;
	unsigned char pattern = pkt[4]; /* first byte after TS header */
	int fail = 0;

	foff = off + i;
	if (pattern != (last_pattern + 1)) {
		printf("Prev pattern 0x%x mismatch with current 0x%x. file offset=%lld (0x%" PRIx64 " or %d MB) \n",
				last_pattern, pattern, (long long int)foff, foff, (int)(foff/1024/1024));
		fail = 1;
	}

	for(i = 4; i < TS_SIZE; i++) {
		if (pkt[i] != pattern) {
			foff = off + i;
			printf("Pattern 0x%x mismatch with byte 0x%x. file offset=%lld (0x%" PRIx64 " or %d MB) \n",
					pattern, pkt[i], (long long int)foff, foff, (int)(foff/1024/1024));
			fail = 1;
		}
	}

	last_pattern = pattern;
	if (pattern == 0xff)
		last_pattern = -1;

	if (fail)
		(*fail_pattern)++;
	else
		(*success_pattern)++;
}

int main(int argc, char **argv) {
	unsigned char pkt[TS_SIZE_ONCE];
	int counter = 0;
	int pattern = 0;
	int i = 0, j = 0;
	FILE * ofd;
	int c;
	char *filename = "tsgen.ts";
	int nbytes = 0;
	int pid = 0;
	int64_t off = 0;
	int tail = 0;
	int check_pattern = 0, check_counter = 0;
	int64_t success_pattern = 0, success_counter = 0;
	int64_t fail_pattern = 0, fail_counter = 0;
	char counter_map[8192]; // counter map

	while ((c = getopt (argc, argv, "f:pc")) != -1) {
		switch (c)
		{
			case 'f':
				filename = optarg;
				break;
			case 'p':
				check_pattern = 1;
				break;
			case 'c':
				check_counter = 1;
				break;
			default:
				help();
				return 0;
		}
	}

	ofd = fopen(filename, "r");
	if (ofd <= 0) {
		printf("can't open file %s \n", filename);
		perror("");
		return -1;
	} else {
		printf("file %s opened. \n", filename);
	}

	while ( (nbytes = fread(pkt + tail, 1, TS_SIZE_ONCE - tail, ofd)) ) {
		// printf("read nbytes=%d tail=%d \n", nbytes, tail);
		nbytes += tail;
		tail = 0;

		// printf("%d read\n", nbytes);
		// find sync byte
		for (i = 0; i < nbytes;) {
			if (pkt[i] == 0x47 && (i+TS_SIZE) <= nbytes ) {
				// printf("synced\n");
				pid = (pkt[i+1] & 0x1f) << 8 | pkt[i+2];
				counter = (pkt[i+3] & 0xf);

				if (pid == 0x177 && check_pattern) 
					checkts(&pkt[i], off, &success_pattern, &fail_pattern);
				if (check_counter && pid != 0x1FFF) {
					if (counter_map[pid] == 0x0f)
						counter_map[pid] = -1;

					if (counter_map[pid] + 1 != counter) {
						printf ("counter 0x%x error (should be 0x%x) for pid=0x%x \n",
								counter, counter_map[pid], pid );
						fail_counter++;
					} else {
						success_counter++;
					}

					counter_map[pid] = counter;

					if (success_counter && !(success_counter%10000))
						printf("%" PRId64 " counters OK, %" PRId64 " FAIL\n", success_counter, fail_counter);
				}

				i += TS_SIZE;
				off += TS_SIZE;
			} else if ((i+TS_SIZE) > nbytes) {
				tail = nbytes - i;
				// save tail for later use
				memmove(pkt, pkt + i, tail);
				// printf("tail=%d i=%d \n", tail, i);
				i+= tail;
			} else {
				off++;
				i++;
			}
		}
	}

	fclose(ofd);

	printf("TS stream validation done. %lld bytes (%d MB) processed \n", (long long int)off, (int)(off/1024/1024));
	if (check_counter)
		printf("%" PRId64 " counters OK, %" PRId64 " FAIL\n", success_counter, fail_counter);
	if (check_pattern)
		printf("%" PRId64 " pattern OK, %" PRId64 " FAIL\n", success_pattern, fail_pattern);
}
