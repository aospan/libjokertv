#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#define TS_SIZE 188
#define TS_SIZE_ONCE 188000
// about 54MB of data
#define TS_LIMIT 300000

void help() {
	printf("tscheck utility for TS stream validation\n");
	printf("Usage:\n");
	printf("	-f filename	input file with TS stream\n");
}

int last_pattern = 0;

int checkts(unsigned char * pkt, int64_t off) {
	int i = 0;
	unsigned char pattern = pkt[4]; /* first byte after TS header */

	if (pattern != (last_pattern + 1)) 
		printf("Prev pattern 0x%x mismatch with current 0x%x. file offset=%lld (0x%" PRIx64 ") \n",
				last_pattern, pattern, (long long int)off + i, off + i );

	for(i = 4; i < TS_SIZE; i++) {
		if (pkt[i] != pattern) 
			printf("Pattern 0x%x mismatch with byte 0x%x. file offset=%lld (0x%" PRIx64 ") \n",
					pattern, pkt[i], (long long int)off + i, off + i );
	}

	last_pattern = pattern;
	if (pattern == 0xff)
		last_pattern = -1;
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

  while ((c = getopt (argc, argv, "f:")) != -1) {
	  switch (c)
	  {
		  case 'f':
			  filename = optarg;
			  break;
		  default:
			  help();
			  return 0;
	  }
  }

  ofd = fopen(filename, "r");
  if (ofd < 0)
	  return -1;

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
			if (pid == 0x177) 
				checkts(&pkt[i], off);
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

  printf("TS stream validation done. %lld bytes processed \n", (long long int)off);
}
