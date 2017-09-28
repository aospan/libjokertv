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

#ifdef __cplusplus
}
#endif

#endif /* end */
