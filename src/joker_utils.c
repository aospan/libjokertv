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

/* check TS packet pattern
 * return 0 if TS pattern ok
 * return 0 if TS pattern check failed
 * */
int check_pattern(struct joker_t *joker, unsigned char * pkt)
{
	int i = 0;
	int ret = 0;
	unsigned char pattern = pkt[4]; /* first byte after TS header */

	// printf("pattern=0x%x prev=0x%x\n",
			// pattern, joker->last_pattern);

	if (pattern != (joker->last_pattern + 1)) {
		printf("ERR:pattern=0x%x prev=0x%x\n",
				pattern, joker->last_pattern);
		ret = -ERANGE;
		goto done;
	}

	for(i = 4; i < TS_SIZE; i++)
		if (pkt[i] != pattern) {
			printf("ERR:pkt[%d]=0x%x pattern=0x%x \n",
					i, pkt[i], pattern);
			ret = -EINVAL;
			goto done;
		}

done:
	joker->last_pattern = pattern;
	if (pattern == 0xff)
		joker->last_pattern = -1;
	return ret;
}

/* receive TS and validate for patterns
 * return 0 if success
 *
 * min_count - amount of TS packets should be received. return error if less
 * max_err - maximum amount of errnous packets
 * timeout - max time to analyze (in seconds)
 *
 * example:
 * timout = 10
 * min_count = 10000
 * max_err = 10
 * accceptable error rate 10/10000 ~ 0.1%
 * expected minimum bitrate 188*10000/10 ~ 188KB/sec
 */
int validate_ts(struct joker_t * joker, int timeout, int min_count, int max_err)
{
	int read_once = 0, buf_len = 0;
	int ret = 0;
	unsigned char * buf = NULL;
	int err_count = 0, total_count = 0;
	time_t start = time(0);
	int pid = 0;
	unsigned char * pkt = NULL;
	int i = 0;

	/* get raw TS and save it to output file */
	/* reading about 18K at once */
	read_once = TS_SIZE * 100;
	buf = (unsigned char*)malloc(read_once);
	if (!buf)
		return -1;

	err_count = 0;
	total_count = 0;
	while((start + timeout) > time(0)) {
		buf_len = read_ts_data(joker->pool, buf, read_once);
		if (buf_len < 0) {
			usleep(1000);
			continue;
		}

		for (i = 0; i < buf_len; ) {
			pkt = buf + i;
			pid = (pkt[1] & 0x1f) << 8 | pkt[2];

			if (pid == 0x177) {
				total_count++;
				if (check_pattern(joker, pkt))
					err_count++;

			}
			i += TS_SIZE;
		}
	}

	printf("err_count=%d total_count=%d\n", err_count, total_count);
	if (err_count < max_err && total_count > min_count)
		ret = 0;
	else
		ret = -EIO;

	return ret;
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
