/* 
 * Joker TV 
 * Transport Stream related stuff
 *
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#ifndef _JOKER_TS_FILTER
#define _JOKER_TS_FILTER 1

#include "u_drv_data.h"

#ifdef __cplusplus
extern "C" {
#endif
// hack for vim indent :)
#if 0
}
#endif
#define TS_FILTER_BLOCK 1
#define TS_FILTER_UNBLOCK 0

// block/unblock PID's
int ts_filter_one(struct joker_t *joker, int block, int pid);
int ts_filter_all(struct joker_t *joker, int block);

// Allow only service PID's (information from https://en.wikipedia.org/wiki/MPEG_transport_stream):
// 0x00 ... 0x1F - PAT, CAT, TSDT, IPMP, NIT, SDT, EIT, etc
// 0x1FFB - ATSC MGT
// All other PID's are blocked
int ts_filter_only_service_pids(struct joker_t *joker);

#ifdef __cplusplus
}
#endif


#endif /* end */
