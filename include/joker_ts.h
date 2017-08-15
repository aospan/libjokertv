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
// hack for vim indent :)
#if 0
}
#endif

#define SERVICE_NAME_LEN	128

/* service types
 * defined in DVB Document A038 (July 2014) 
 * Table 87: Service type coding
 */
#define SERVICE_TYPE_TV		0x01
#define SERVICE_TYPE_RADIO	0x02
#define SERVICE_TYPE_TLX	0x03

// Programs elementary streams (audio, video, etc)
// "type" described in ISO/IEC 13818-1 : 2000 (E)
// Table 2-29 – Stream type assignments 
struct program_es_t
{
	uint8_t                       type;
	uint16_t                      pid;
	struct list_head list;
};

struct program_t {
	int number;
	unsigned char name[SERVICE_NAME_LEN];
	uint8_t service_type;
	int pmt_pid;
	void * pmt_dvbpsi;
	struct list_head es_list; // elementary streams belongs to this program
	struct list_head list;
	int has_video;
	int has_audio;
};

struct list_head * get_programs(struct big_pool_t *pool);

#ifdef __cplusplus
}
#endif


#endif /* end */
