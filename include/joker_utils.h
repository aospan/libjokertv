/* 
 * Joker TV helpers
 * 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include "joker_tv.h"

#ifndef _JOKER_UTILS
#define _JOKER_UTILS	1

#ifdef __cplusplus
extern "C" {
#endif

/* get current time in usec */
uint64_t getus();

void msleep_msecs(unsigned int msecs);
#define msleep(x) msleep_msecs(x);

void hexdump(unsigned char * buf, int size);

/* put chips into reset state
 * chips selected by mask
 * return 0 if success
 */
int joker_reset(struct joker_t *joker, int mask);

/* wakeup chips from reset
 * chips selected by mask
 * return 0 if success
 */
int joker_unreset(struct joker_t *joker, int mask);

/* check TS packet pattern
 * return 0 if TS pattern ok
 * return 0 if TS pattern check failed
 * */
int check_pattern(struct joker_t *joker, unsigned char * pkt);

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
int validate_ts(struct joker_t * joker, int timeout, int min_count, int max_err);

/* memmem impl for cross-platform use
 * based on https://github.com/libarchive/libarchive
 */
char*
xmemmem(const char *hay, const size_t haysize,
	const char *needle, const size_t needlesize);

/* clean TS FIFO inside FPGA
 * return 0 if success
 */
int joker_clean_ts(struct joker_t *joker);

/* get raw TS and save it to output file
   reading about 18K at once
   limit amount of bytes to save. 0 for unlimited (call is blocked !).

   return saved bytes if success
   return negative error code if failed
   */
int64_t save_ts(struct joker_t *joker, char *filename, int64_t limit);

#ifdef __cplusplus
}
#endif

#endif /* end */
