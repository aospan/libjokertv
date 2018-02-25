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

#define TUPLE_MAX_SIZE	128
#define JOKER_CI_IO	1
#define JOKER_CI_MEM	0
#define CAID_MAX_COUNT	128

// commands to read/write CAM module IO or MEM
#define JOKER_CI_CTRL_READ	1 << 0 // 0x01
#define JOKER_CI_CTRL_WRITE	1 << 1 // 0x02
#define JOKER_CI_CTRL_IO	1 << 2 // 0x04
#define JOKER_CI_CTRL_MEM	1 << 3 // 0x08
#define JOKER_CI_CTRL_BULK	1 << 4 // 0x10

// commands status
#define JOKER_CI_CTRL_ERR	1
#define JOKER_CI_CTRL_OK	2

/* from Linux kernel:
 * ./drivers/media/dvb-core/dvb_ca_en50221.c */
#define CTRLIF_DATA      0
/* command bits: R R R R RS SR SW HC */
#define CTRLIF_COMMAND   1
/* status bits: DA FR R R R R WE RE */
#define CTRLIF_STATUS    1
#define CTRLIF_SIZE_LOW  2
#define CTRLIF_SIZE_HIGH 3

#define CMDREG_HC        1      /* Host control */
#define CMDREG_SW        2      /* Size write */
#define CMDREG_SR        4      /* Size read */
#define CMDREG_RS        8      /* Reset interface */
#define CMDREG_FRIE   0x40      /* Enable FR interrupt */
#define CMDREG_DAIE   0x80      /* Enable DA interrupt */
#define IRQEN (CMDREG_DAIE)

#define STATUSREG_RE     1      /* read error */
#define STATUSREG_WE     2      /* write error */
#define STATUSREG_FR  0x40      /* module free */
#define STATUSREG_DA  0x80      /* data available */
#define STATUSREG_TXERR (STATUSREG_RE|STATUSREG_WE)     /* general transfer error */

// PCAP file definitions
// pcap format description here:
// https://wiki.wireshark.org/Development/LibpcapFileFormat
// version 2.4
// DVBCI for PCAP description here
// http://www.kaiser.cx/pcap-dvbci.html
#define JOKER_LINKTYPE_DVB_CI 235
#define JOKER_PCAP_HW_EVT 0xFB
#define JOKER_PCAP_DATA_CAM_TO_HOST 0xFF
#define JOKER_PCAP_DATA_HOST_TO_CAM 0xFE
#define JOKER_PCAP_CIS_READ 0xFD
#define JOKER_PCAP_COR_WRITE 0xFC

#define JOKER_PCAP_CAM_IN 0x01 // CI Module is inserted into the slot
#define JOKER_PCAP_CAM_OUT 0x02 // CI Module is removed from the slot
#define JOKER_PCAP_POWER_ON 0x03
#define JOKER_PCAP_POWER_OFF 0x04
#define JOKER_PCAP_TS_ROUTE 0x05 // DVB Transport Stream is routed through the CI Module
#define JOKER_PCAP_TS_BYPASS 0x06 // DVB Transport Stream bypasses the CI Module

typedef struct joker_pcap_hdr_s {
	uint32_t magic_number;   /* magic number */
	uint16_t version_major;  /* major version number */
	uint16_t version_minor;  /* minor version number */
	int32_t thiszone;       /* GMT to local correction */
	uint32_t sigfigs;        /* accuracy of timestamps */
	uint32_t snaplen;        /* max length of captured packets, in octets */
	uint32_t network;        /* data link type */
} joker_pcap_hdr_t;

typedef struct joker_pcaprec_hdr_s {
	uint32_t ts_sec;         /* timestamp seconds */
	uint32_t ts_usec;        /* timestamp microseconds */
	uint32_t incl_len;       /* number of octets of packet saved in file */
	uint32_t orig_len;       /* actual length of packet */
} joker_pcaprec_hdr_t;

struct joker_dvbci_header {
	uint8_t    version;
	uint8_t    event;
	uint16_t   len;
} __attribute__((__packed__));

struct joker_dvbci_cor {
	uint16_t   addr;
	uint8_t   val;
} __attribute__((__packed__));

// CI common definitions
struct ci_tuple_t {
	uint8_t type;
	uint8_t size;
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

	/* EN50221 stuff */
	uint8_t application_type;
	uint16_t application_manufacturer;
	uint16_t manufacturer_code;
	unsigned char menu_string[TUPLE_MAX_SIZE];
	uint16_t ca_ids[CAID_MAX_COUNT];
};

/* initialize CAM module
 * return 0 if success
 * other return values indicates error
 */
int joker_ci(struct joker_t * joker);

/* CAM PCAP write event */
int cam_pcap_write_event(struct joker_t * joker, uint8_t event, char * data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* end */
