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

#define J_TRANSPORT_PAT_PID 0x00
#define J_TRANSPORT_CAT_PID 0x01
#define J_TRANSPORT_NIT_PID 0x10
#define J_TRANSPORT_SDT_PID 0x11
#define J_TRANSPORT_BAT_PID 0x11
#define J_TRANSPORT_EIT_PID 0x12
#define J_TRANSPORT_CIT_PID 0x12
#define J_TRANSPORT_RST_PID 0x13
#define J_TRANSPORT_TDT_PID 0x14
#define J_TRANSPORT_TOT_PID 0x14
#define J_TRANSPORT_RNT_PID 0x16
#define J_TRANSPORT_DIT_PID 0x1e
#define J_TRANSPORT_SIT_PID 0x1f

/* service types
 * defined in DVB Document A038 (July 2014) 
 * Table 87: Service type coding
 */
#define SERVICE_TYPE_TV		0x01
#define SERVICE_TYPE_RADIO	0x02
#define SERVICE_TYPE_TLX	0x03

enum ci_program_status {
	CI_NONE, // do not descramble this program
	CI_INIT, // program not sent to CAM yet
	CI_CAM_SENT // program sent to CAM 
};

// Programs elementary streams (audio, video, etc)
// "type" described in ISO/IEC 13818-1 : 2000 (E)
// Table 2-29 – Stream type assignments 
struct program_es_t
{
	uint8_t                       type;
	uint16_t                      pid;
	char	lang[3];
	struct list_head list;
};

// Conditional Access descriptors info
struct program_ca_t
{
	uint16_t                       pid;
	uint16_t                      caid;
	struct list_head list;
};

struct program_t {
	int number;
	unsigned char name[SERVICE_NAME_LEN];
	uint8_t service_type;
	int pmt_pid;
	int pcr_pid;
	void * pmt_dvbpsi;
	struct list_head es_list; // elementary streams belongs to this program
	struct list_head ca_list; // Conditional Access descriptors info
	struct list_head list;
	int has_video;
	int has_audio;

	struct joker_t * joker;

	// CI stuff
	int capmt_sent;
	void *pmt; // "raw" PMT. actually this is struct mpeg_pmt_section *
	int pmt_len;
	enum ci_program_status ci_status;

	// last used PMT stuff
	uint8_t i_version;
	int b_current_next;

	// SDT
	void *generated_sdt_pkt;
};

struct list_head * get_programs(struct big_pool_t *pool);

/* convert name to utf-8
 * first byte can be used as codepage (see ETSI EN 300 468 V1.11.1 (2010-04) */
int dvb_to_utf(char * buf, size_t insize, char * _outbuf, int maxlen);

void * get_next_sdt(struct big_pool_t *pool);

#ifdef __cplusplus
}
#endif


#endif /* end */
