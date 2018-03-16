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
#include <string.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>

/* get current time in usec */
uint64_t getus() {
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
}

void hexdump(unsigned char * buf, int size)
{
	int i = 0, printed = 0, padding = 0;
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
	padding = (i%16) ? (3 * (16 - i%16) + 6) : 3;
	printf("%*s %s\n", padding, " ", txt);

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

/* get raw TS and save it to output file
   reading about 18K at once
   limit amount of bytes to save. 0 for unlimited (call is blocked !).

   return saved bytes if success
   return negative error code if failed
   */
int64_t save_ts(struct joker_t *joker, char *filename, int64_t limit)
{
	FILE * out = NULL;
	struct big_pool_t *pool;
	unsigned char *res = NULL;
	int res_len = 0, read_once = 0;
	int64_t total_len = 0;

	if (!joker || !joker->pool || !filename )
		return -EINVAL;

	pool = joker->pool;

	out = fopen((char*)filename, "w+b");
	if (!out){
		printf("Can't open out file '%s' error=%s (%d)\n",
				filename, errno, strerror(errno));
		return -EIO;
	} else {
		printf("TS outfile:%s \n", filename);
	}

	/* get raw TS and save it to output file */
	/* reading about 18K at once */
	read_once = TS_SIZE * 100;
	res = (unsigned char*)malloc(read_once);
	if (!res) {
		printf("Can't alloc mem for TS \n");
		return -1;
	}

	while( limit == 0 || (limit > 0 && total_len < limit) ) {
		res_len = read_ts_data(pool, res, read_once);
		jdebug("%s: %d bytes read \n", __func__, res_len);

		/* save to output file */
		if (res_len > 0)
			fwrite(res, res_len, 1, out);
		else
			usleep(1000); // TODO: rework this (condwait ?)

		total_len += res_len;
	}
	fclose(out);

	return total_len;
}

/* clean TS FIFO inside FPGA
 * return 0 if success
 */
int joker_clean_ts(struct joker_t *joker)
{
	unsigned char buf[BUF_LEN];
	int ret = 0;

	if (!joker)
		return -EINVAL;

	buf[0] = J_CMD_CLEAR_TS_FIFO;
	if ((ret = joker_cmd(joker, buf, 1, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	return 0;
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
	jdebug("%s: mask=0x%x final=0x%x\n",
			__func__, mask, joker->reset);
	if (joker_reset_write(joker)) {
		printf("%s: Reset register write failed! \n", __func__);
		return -EIO;
	}
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
	jdebug("%s: mask=0x%x final=0x%x\n",
			__func__, mask, joker->reset);
	if (joker_reset_write(joker)) {
		printf("%s: Reset register write failed! \n", __func__);
		return -EIO;
	}
}


static void*
deconst(const void *c)
{
	return (char *)0x1 + (((const char *)c) - (const char *)0x1);
}

char*
xmemmem(const char *hay, const size_t haysize,
	const char *needle, const size_t needlesize)
{
	const char *const eoh = hay + haysize;
	const char *const eon = needle + needlesize;
	const char *hp;
	const char *np;
	const char *cand;
	unsigned int hsum;
	unsigned int nsum;
	unsigned int eqp;

	/* trivial checks first
         * a 0-sized needle is defined to be found anywhere in haystack
         * then run strchr() to find a candidate in HAYSTACK (i.e. a portion
         * that happens to begin with *NEEDLE) */
	if (needlesize == 0UL) {
		return deconst(hay);
	} else if ((hay = memchr(hay, *needle, haysize)) == NULL) {
		/* trivial */
		return NULL;
	}

	/* First characters of haystack and needle are the same now. Both are
	 * guaranteed to be at least one character long.  Now computes the sum
	 * of characters values of needle together with the sum of the first
	 * needle_len characters of haystack. */
	for (hp = hay + 1U, np = needle + 1U, hsum = *hay, nsum = *hay, eqp = 1U;
	     hp < eoh && np < eon;
	     hsum ^= *hp, nsum ^= *np, eqp &= *hp == *np, hp++, np++);

	/* HP now references the (NEEDLESIZE + 1)-th character. */
	if (np < eon) {
		/* haystack is smaller than needle, :O */
		return NULL;
	} else if (eqp) {
		/* found a match */
		return deconst(hay);
	}

	/* now loop through the rest of haystack,
	 * updating the sum iteratively */
	for (cand = hay; hp < eoh; hp++) {
		hsum ^= *cand++;
		hsum ^= *hp;

		/* Since the sum of the characters is already known to be
		 * equal at that point, it is enough to check just NEEDLESIZE - 1
		 * characters for equality,
		 * also CAND is by design < HP, so no need for range checks */
		if (hsum == nsum && memcmp(cand, needle, needlesize - 1U) == 0) {
			return deconst(cand);
		}
	}
	return NULL;
}
