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

#include <stdio.h>
#include <libusb.h>
#include "joker_tv.h"

#ifndef _JOKER_CI
#define _JOKER_CI	1

#ifdef __cplusplus
extern "C" {
#endif

int joker_ci(struct joker_t * joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
