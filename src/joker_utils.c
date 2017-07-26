/* 
 * Joker TV helpers
 * 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>

void hexdump(unsigned char * buf, int size)
{
	int i = 0, printed = 0;
	unsigned char txt[512] = { 0 };
	unsigned char * ptr = txt;

	printf("%.8x  ", i);
	for(i = 0; i < size; i ++){
		if (i && !(i%8))
			printf("   ");
		if (i && !(i%16)) {
			printf("  %s\n", txt);
			ptr = txt;
			printf("%.8x  ", i);
		}
		printf("%.2x ", buf[i]);
		printed = sprintf(ptr, "%c", isprint(buf[i]) ? buf[i] : '.' );
		if (printed > 0)
			ptr += printed;
	}

	printf("\n");
}
