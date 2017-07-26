/* 
 * Joker TV 
 * Transport Stream related stuff
 *
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#ifndef _JOKER_TS
#define _JOKER_TS 1

#include "u_drv_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct program_t {
	int number;
	unsigned char * name;
	struct list_head list;
};

struct list_head * get_programs(struct big_pool_t *pool);

#ifdef __cplusplus
}
#endif


#endif /* end */
