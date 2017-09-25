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
#include "joker_tv.h"

#ifndef _JOKER_CI
#define _JOKER_CI	1

#ifdef __cplusplus
extern "C" {
#endif

#define TUPLE_MAX_SIZE 128

struct ci_tuple_t {
	int type;
	int size;
	unsigned char data[TUPLE_MAX_SIZE];
};

struct joker_ci_t {
	/* enable CAM debug if not zero */
	int ci_verbose;

	/* CAM manufacturer info */
	uint16_t manfid;
	uint16_t devid;

	/* base address of CAM config */
	uint32_t config_base;

	/* value to write into Config Control register */
	uint8_t config_option;

	/* current offset when reading next tuple */
	int tuple_cur_offset;

	/* CAM module detected */
	int cam_detected;

	/* CAM module validated */
	int cam_validated;

	/* CAM module info string */
	unsigned char cam_infostring[TUPLE_MAX_SIZE];
};

/* initialize CAM module
 * return 0 if success
 * other return values indicates error
 */
int joker_ci(struct joker_t * joker);

#ifdef __cplusplus
}
#endif

#endif /* end */
