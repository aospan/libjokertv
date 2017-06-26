/* 
 * Joker TV helper utils
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
#include <errno.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_fpga.h>
#include <joker_utils.h>

void jhexdump(unsigned char * buf, int size)
{
	int i = 0, printed = 0;
	unsigned char txt[512] = { 0 };
	unsigned char * ptr = txt;

	jdebug("hexdump:");
	for(i = 0; i < size; i ++){
		if (i && !(i%8))
			jdebug("   ");
		if (i && !(i%16)) {
			jdebug("  %s\n", txt, ptr, txt);
			ptr = txt;
			jdebug("%.8x  ", i);
		}
		jdebug("%.2x ", buf[i]);
		printed = sprintf(ptr, "%c", isprint(buf[i]) ? buf[i] : '.' );
		if (printed > 0)
			ptr += printed;
	}

	jdebug("\n");
}
