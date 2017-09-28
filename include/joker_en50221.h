/* 
 * Access to Joker TV CI (Common Interface)
 * EN50221 part
 * 
 * Conditional Access Module for scrambled streams (pay tv)
 * Based on EN 50221-1997 standard
 * 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include "joker_tv.h"

#ifndef _JOKER_EN50221
#define _JOKER_EN50221	1

#ifdef __cplusplus
extern "C" {
#endif

/* initialize EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221(struct joker_t * joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
