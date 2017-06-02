/* 
 * Access to Joker TV CI (Common Interface)
 * 
 * Conditional Access Module for scrambled streams (pay tv)
 * Based on EN 50221-1997 standard
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
#include <joker_ci.h>
#include <joker_fpga.h>

void hexdump(unsigned char * buf, int size)
{
	int i = 0, printed = 0;
	unsigned char txt[512] = { 0 };
	unsigned char * ptr = txt;

	for(i = 0; i < size; i ++){
		if (i && !(i%8))
			printf("   ");
		if (i && !(i%16)) {
			printf("  %s\n", txt, ptr, txt);
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

int joker_ci(struct joker_t * joker)
{
	int ret = 0, i = 0, j = 0;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	unsigned char mem[JCMD_BUF_LEN];

	buf[0] = J_CMD_CI_STATUS;
	if ((ret = joker_cmd(joker, buf, 1, in_buf, 2 /* in_len */)))
		return ret;
	printf("CAM status 0x%x %x\n", in_buf[0], in_buf[1]);

	if (in_buf[1] & 0x01) {
		printf("CAM attribute memory dump:\n");
		printf("%.8x  ", i);
		for(i = 0, j = 0; i < 512; i += 2, j++){
			/* CI */
			buf[0] = J_CMD_CI_READ_MEM;
			buf[1] = i;
			if ((ret = joker_cmd(joker, buf, 2, in_buf, 2 /* in_len */)))
				return ret;

			mem[j] = in_buf[1];
		}
		hexdump(mem, j);
	}

	fflush(stdout);

	return 0;
}
