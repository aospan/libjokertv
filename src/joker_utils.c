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
	for(i = 0; i < size; i++){
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
	printf("  %s\n", txt);

	printf("\n");
}

/* do actual reset control on device
 * return 0 if success
 */
int joker_reset_write(struct joker_t *joker)
{
	unsigned char buf[BUF_LEN];
	int ret = 0;

	if (!joker)
		return -EINVAL;

	buf[0] = J_CMD_RESET_CTRL_WRITE;
	buf[1] = joker->reset;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	return 0;
}

/* put chips into reset state
 * chips selected by mask
 * return 0 if success
 */
int joker_reset(struct joker_t *joker, int mask)
{
	if (!joker)
		return -EINVAL;

	joker->reset |= mask;
	if (joker_reset_write(joker))
		return -EIO;
}

/* wakeup chips from reset
 * chips selected by mask
 * return 0 if success
 */
int joker_unreset(struct joker_t *joker, int mask)
{
	if (!joker)
		return -EINVAL;

	joker->reset &= ~mask;
	if (joker_reset_write(joker))
		return -EIO;
}
