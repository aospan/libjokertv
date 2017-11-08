/* 
 * Access to Joker TV CI (Common Interface)
 * EN50221 support
 * 
 * Conditional Access Module for scrambled streams (pay tv)
 * Based on EN 50221-1997 standard
 * 
 * https://jokersys.com
 * (c) Abylay Ospan, 2017
 * aospan@jokersys.com
 * GPLv2
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>
#include <joker_utils.h>
#include <joker_en50221.h>
#include <signal.h>
#include <poll.h>
#include <libucsi/section.h>
#include <libucsi/mpeg/section.h>
#include <libucsi/mpeg/pmt_section.h> 
#include <win/wtime.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef __WIN32__
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <unistd.h>

#include <libdvbmisc/dvbmisc.h>
#include <pthread.h>
#include <libucsi/mpeg/descriptor.h>
#include "en50221_app_ca.h"
#include "asn_1.h"

// stuff from libdvbpsi
#include <stdbool.h>
#include <dvbpsi.h>
#include <psi.h>
#include <descriptor.h>
#include <pat.h>
#include <pmt.h>

// libdvben50221 stuff
#include <libdvben50221/en50221_session.h>
#include <libdvben50221/en50221_app_utils.h>
#include <libdvben50221/en50221_app_ai.h>
#include <libdvben50221/en50221_app_auth.h>
#include <libdvben50221/en50221_app_ca.h>
#include <libdvben50221/en50221_app_datetime.h>
#include <libdvben50221/en50221_app_dvb.h>
#include <libdvben50221/en50221_app_epg.h>
#include <libdvben50221/en50221_app_lowspeed.h>
#include <libdvben50221/en50221_app_mmi.h>
#include <libdvben50221/en50221_app_rm.h>
#include <libdvben50221/en50221_app_smartcard.h>
#include <libdvben50221/en50221_app_teletext.h>

/****** stuff from ./test/libdvben50221/test-app.c */
void *ci_poll_func(void* arg);
int test_lookup_callback(void *arg, uint8_t slot_id, uint32_t requested_resource_id,
		en50221_sl_resource_callback *callback_out, void **arg_out, uint32_t *connected_resource_id);
int test_session_callback(void *arg, int reason, uint8_t slot_id, uint16_t session_number, uint32_t resource_id);

int test_datetime_enquiry_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t response_interval);

int test_rm_enq_callback(void *arg, uint8_t slot_id, uint16_t session_number);
int test_rm_reply_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t resource_id_count, uint32_t *resource_ids);
int test_rm_changed_callback(void *arg, uint8_t slot_id, uint16_t session_number);

int test_ai_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t application_type, uint16_t application_manufacturer,
		uint16_t manufacturer_code, uint8_t menu_string_length,
		uint8_t *menu_string);

int test_ca_info_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t ca_id_count, uint16_t *ca_ids);
int test_ca_pmt_reply_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_pmt_reply *reply, uint32_t reply_size);

int test_mmi_close_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t cmd_id, uint8_t delay);

int test_mmi_display_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t cmd_id, uint8_t mmi_mode);

int test_mmi_keypad_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t cmd_id, uint8_t *key_codes, uint32_t key_codes_count);

int test_mmi_subtitle_segment_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t *segment, uint32_t segment_size);

int test_mmi_scene_end_mark_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t decoder_continue_flag, uint8_t scene_reveal_flag,
		uint8_t send_scene_done, uint8_t scene_tag);

int test_mmi_scene_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t decoder_continue_flag, uint8_t scene_reveal_flag,
		uint8_t scene_tag);

int test_mmi_subtitle_download_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t *segment, uint32_t segment_size);

int test_mmi_flush_download_callback(void *arg, uint8_t slot_id, uint16_t session_number);

int test_mmi_enq_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t blind_answer, uint8_t expected_answer_length,
		uint8_t *text, uint32_t text_size);

int test_mmi_menu_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_mmi_text *title,
		struct en50221_app_mmi_text *sub_title,
		struct en50221_app_mmi_text *bottom,
		uint32_t item_count, struct en50221_app_mmi_text *items,
		uint32_t item_raw_length, uint8_t *items_raw);

int test_app_mmi_list_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_mmi_text *title,
		struct en50221_app_mmi_text *sub_title,
		struct en50221_app_mmi_text *bottom,
		uint32_t item_count, struct en50221_app_mmi_text *items,
		uint32_t item_raw_length, uint8_t *items_raw);

struct section_ext *read_section_ext(char *buf, int buflen, int adapter, int demux, int pid, int table_id);

// lookup table used in resource manager implementation
struct resource {
	struct en50221_app_public_resource_id resid;
	uint32_t binary_resource_id;
	en50221_sl_resource_callback callback;
	void *arg;
};

// this contains all known resource ids so we can see if the cam asks for something exotic
uint32_t resource_ids[] = { EN50221_APP_TELETEXT_RESOURCEID,
	EN50221_APP_SMARTCARD_RESOURCEID(1),
	EN50221_APP_RM_RESOURCEID,
	EN50221_APP_MMI_RESOURCEID,
	EN50221_APP_LOWSPEED_RESOURCEID(1,1),
	EN50221_APP_EPG_RESOURCEID(1),
	EN50221_APP_DVB_RESOURCEID,
	EN50221_APP_CA_RESOURCEID,
	EN50221_APP_DATETIME_RESOURCEID,
	EN50221_APP_AUTH_RESOURCEID,
	EN50221_APP_AI_RESOURCEID, };

struct joker_en50221_t {
	struct en50221_transport_layer *tl;

	int shutdown_stackthread;
	int shutdown_pmtthread;
	int in_menu;
	int in_enq;
	int ca_connected;
	int pmt_pid;
	int ca_session_number;

	// instances of resources we actually implement here
	struct en50221_app_rm *rm_resource;
	struct en50221_app_datetime *datetime_resource;
	struct en50221_app_ai *ai_resource;
	struct en50221_app_ca *ca_resource;
	struct en50221_app_mmi *mmi_resource;

	uint16_t ai_session_numbers[5];
	uint16_t mmi_session_number;

	struct resource resources[20];
	int resources_count;
	int resource_ids_count;
	int slot_id;
	int mmi_entered;
	mmi_callback_t cb;

	// protect access to CAM from another threads
	pthread_mutex_t mux;
	struct list_head programs_list;
};

int joker_ci_poll(struct pollfd *fds, nfds_t nfds, int timeout, void *arg)
{
	struct joker_t * joker = (struct joker_t *)arg;

	if (!joker)
		return -EINVAL;

	jdebug("EN50221:%s timeout=%d\n", __func__, timeout);
	if (!joker_ci_wait_status(joker, STATUSREG_DA, timeout /* msec */)) {
		jdebug("EN50221:%s has data\n", __func__);
		fds[0].revents = POLLPRI | POLLIN;
		return 1;
	} else {
		fds[0].revents = 0x0;
	}

	return 0;
}

/* initialize EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221_init(struct joker_t * joker)
{
	int ret = -EINVAL, i = 0, j = 0;
	struct joker_en50221_t *jen = NULL;

	if (!joker)
		return -EINVAL;

	// already initialized, okay
	if (joker->joker_en50221_opaque)
		return 0;

	jen = (struct joker_en50221_t *)malloc(sizeof(struct joker_en50221_t));
	if (!jen)
		return -ENOMEM;
	memset(jen, 0, sizeof(struct joker_en50221_t));
	joker->joker_en50221_opaque = jen;
	pthread_mutex_init(&jen->mux, NULL);

	INIT_LIST_HEAD(&jen->programs_list);

	return 0;
}

/* start EN50221
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_en50221_start(struct joker_t * joker)
{
	struct joker_en50221_t * jen = NULL;
	struct en50221_transport_layer *tl = NULL;
	struct en50221_session_layer *sl = NULL;
	struct en50221_app_send_functions *sendfuncs = NULL;
	char tmp[256];
	pthread_t stackthread;

	jdebug("EN50221:%s called \n", __func__);
	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;
	jen->resource_ids_count = sizeof(resource_ids)/4;

	sendfuncs = (struct en50221_app_send_functions *)malloc(sizeof(struct en50221_app_send_functions));
	if (!sendfuncs)
		return -ENOMEM;

	// create transport layer
	tl = en50221_tl_create(5 /* max_slots */,
			32 /* max_connections_per_slot */);
	if (tl == NULL) {
		printf("EN50221:%s Failed to create transport layer\n", __func__);
		return -EIO;
	}

	// resgister out custom joker poll
	en50221_tl_register_poll(tl, joker_ci_poll, joker);

	// register it with the CA stack
	if ((jen->slot_id = en50221_tl_register_slot(tl, 1 /* fake fd */, 0, 4000, 100)) < 0) {
		printf("EN50221:%s Slot registration failed\n", __func__);
		return -EIO;
	}

	// create session layer
	sl = en50221_sl_create(tl, 256);
	if (sl == NULL) {
		printf("EN50221:%s Failed to create session layer\n", __func__);
		return -EIO;
	}

	// create the sendfuncs
	sendfuncs->arg        = sl;
	sendfuncs->send_data  = (en50221_send_data) en50221_sl_send_data;
	sendfuncs->send_datav = (en50221_send_datav) en50221_sl_send_datav;

	// create the resource manager resource
	jen->rm_resource = en50221_app_rm_create(sendfuncs);
	en50221_app_decode_public_resource_id(&jen->resources[jen->resources_count].resid, EN50221_APP_RM_RESOURCEID);
	jen->resources[jen->resources_count].binary_resource_id = EN50221_APP_RM_RESOURCEID;
	jen->resources[jen->resources_count].callback = (en50221_sl_resource_callback) en50221_app_rm_message;
	jen->resources[jen->resources_count].arg = jen->rm_resource;
	en50221_app_rm_register_enq_callback(jen->rm_resource, test_rm_enq_callback, joker);
	en50221_app_rm_register_reply_callback(jen->rm_resource, test_rm_reply_callback, joker);
	en50221_app_rm_register_changed_callback(jen->rm_resource, test_rm_changed_callback, joker);
	jen->resources_count++;

	// create the datetime resource
	jen->datetime_resource = en50221_app_datetime_create(sendfuncs);
	en50221_app_decode_public_resource_id(&jen->resources[jen->resources_count].resid, EN50221_APP_DATETIME_RESOURCEID);
	jen->resources[jen->resources_count].binary_resource_id = EN50221_APP_DATETIME_RESOURCEID;
	jen->resources[jen->resources_count].callback = (en50221_sl_resource_callback) en50221_app_datetime_message;
	jen->resources[jen->resources_count].arg = jen->datetime_resource;
	en50221_app_datetime_register_enquiry_callback(jen->datetime_resource, test_datetime_enquiry_callback, joker);
	jen->resources_count++;

	// create the application information resource
	jen->ai_resource = en50221_app_ai_create(sendfuncs);
	en50221_app_decode_public_resource_id(&jen->resources[jen->resources_count].resid, EN50221_APP_AI_RESOURCEID);
	jen->resources[jen->resources_count].binary_resource_id = EN50221_APP_AI_RESOURCEID;
	jen->resources[jen->resources_count].callback = (en50221_sl_resource_callback) en50221_app_ai_message;
	jen->resources[jen->resources_count].arg = jen->ai_resource;
	en50221_app_ai_register_callback(jen->ai_resource, test_ai_callback, joker);
	jen->resources_count++;

	// create the CA resource
	jen->ca_resource = en50221_app_ca_create(sendfuncs);
	en50221_app_decode_public_resource_id(&jen->resources[jen->resources_count].resid, EN50221_APP_CA_RESOURCEID);
	jen->resources[jen->resources_count].binary_resource_id = EN50221_APP_CA_RESOURCEID;
	jen->resources[jen->resources_count].callback = (en50221_sl_resource_callback) en50221_app_ca_message;
	jen->resources[jen->resources_count].arg = jen->ca_resource;
	en50221_app_ca_register_info_callback(jen->ca_resource, test_ca_info_callback, joker);
	en50221_app_ca_register_pmt_reply_callback(jen->ca_resource, test_ca_pmt_reply_callback, joker);
	jen->resources_count++;

	// create the MMI resource
	jen->mmi_resource = en50221_app_mmi_create(sendfuncs);
	en50221_app_decode_public_resource_id(&jen->resources[jen->resources_count].resid, EN50221_APP_MMI_RESOURCEID);
	jen->resources[jen->resources_count].binary_resource_id = EN50221_APP_MMI_RESOURCEID;
	jen->resources[jen->resources_count].callback = (en50221_sl_resource_callback) en50221_app_mmi_message;
	jen->resources[jen->resources_count].arg = jen->mmi_resource;
	en50221_app_mmi_register_close_callback(jen->mmi_resource, test_mmi_close_callback, joker);
	en50221_app_mmi_register_display_control_callback(jen->mmi_resource, test_mmi_display_control_callback, joker);
	en50221_app_mmi_register_keypad_control_callback(jen->mmi_resource, test_mmi_keypad_control_callback, joker);
	en50221_app_mmi_register_subtitle_segment_callback(jen->mmi_resource, test_mmi_subtitle_segment_callback, joker);
	en50221_app_mmi_register_scene_end_mark_callback(jen->mmi_resource, test_mmi_scene_end_mark_callback, joker);
	en50221_app_mmi_register_scene_control_callback(jen->mmi_resource, test_mmi_scene_control_callback, joker);
	en50221_app_mmi_register_subtitle_download_callback(jen->mmi_resource, test_mmi_subtitle_download_callback, joker);
	en50221_app_mmi_register_flush_download_callback(jen->mmi_resource, test_mmi_flush_download_callback, joker);
	en50221_app_mmi_register_enq_callback(jen->mmi_resource, test_mmi_enq_callback, joker);
	en50221_app_mmi_register_menu_callback(jen->mmi_resource, test_mmi_menu_callback, joker);
	en50221_app_mmi_register_list_callback(jen->mmi_resource, test_app_mmi_list_callback, joker);
	jen->resources_count++;

	// start another thread running the stack
	jen->tl = tl;
	pthread_create(&stackthread, NULL, ci_poll_func, joker);

	// register callbacks
	en50221_sl_register_lookup_callback(sl, test_lookup_callback, joker);
	en50221_sl_register_session_callback(sl, test_session_callback, joker);

	// create a new connection on each slot
	int tc = en50221_tl_new_tc(tl, jen->slot_id);

	// start simple TCP server for MMI access
	start_en50221_server(joker);
	printf("EN50221 initialized\n");

	return 0;
}

int joker_en50221_mmi_enter(struct joker_t * joker, mmi_callback_t cb)
{
	struct joker_en50221_t * jen = NULL;

	jdebug("EN50221:%s called \n", __func__);
	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jen->cb = cb;

	en50221_app_ai_entermenu(jen->ai_resource, jen->ai_session_numbers[jen->slot_id]);
	jen->mmi_entered = 1;
	return 0;
}

int joker_en50221_mmi_call(struct joker_t * joker, const unsigned char *buf, int len)
{
	int choice = atoi(buf);
	struct joker_en50221_t * jen = NULL;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("EN50221:%s called. choice=%d in_menu=%d in_enq=%d\n",
			__func__, choice, jen->in_menu, jen->in_enq);

	if (jen->in_menu) {
		en50221_app_mmi_menu_answ(jen->mmi_resource, jen->mmi_session_number, choice);
		jen->in_menu = 0;
	}
	if (jen->in_enq) {
		uint32_t i;
		uint32_t len = strlen(buf);
		for(i=0; i< len; i++) {
			if (!isdigit(buf[i])) {
				len = i;
				break;
			}
		}
		en50221_app_mmi_answ(jen->mmi_resource, jen->mmi_session_number, MMI_ANSW_ID_ANSWER, (uint8_t*) buf, len);
		jen->in_enq = 0;
	}

	return 0;
}

int joker_ci_en50221_close(struct joker_t * joker)
{
	return 0;
}

/********************* helper functions from dvb-apps ************************/
uint32_t integer_to_bcd(uint32_t intval)
{
	uint32_t val = 0;

	int i;
	for(i=0; i<=28;i+=4) {
		val |= ((intval % 10) << i);
		intval /= 10;
	}

	return val;
}

void unixtime_to_dvbdate(time_t unixtime, dvbdate_t dvbdate)
{
	struct tm tm;
	double l = 0;
	int mjd;

	/* the undefined value */
	if (unixtime == -1) {
		memset(dvbdate, 0xff, 5);
		return;
	}

	gmtime_r(&unixtime, &tm);
	tm.tm_mon++;
	if ((tm.tm_mon == 1) || (tm.tm_mon == 2)) l = 1;
	mjd = 14956 + tm.tm_mday + (int) ((tm.tm_year - l) * 365.25) + (int) ((tm.tm_mon + 1 + l * 12) * 30.6001);

	dvbdate[0] = (mjd & 0xff00) >> 8;
	dvbdate[1] = mjd & 0xff;
	dvbdate[2] = integer_to_bcd(tm.tm_hour);
	dvbdate[3] = integer_to_bcd(tm.tm_min);
	dvbdate[4] = integer_to_bcd(tm.tm_sec);
}

/**
 * Open a CA device. Multiple CAMs can be accessed through a CA device.
 *
 * @param adapter Index of the DVB adapter.
 * @param cadevice Index of the CA device on that adapter (usually 0).
 * @return A unix file descriptor on success, or -1 on failure.
 */
extern int dvbca_open(int adapter, int cadevice)
{
	printf("EN50221:%s called \n", __func__);
}

/**
 * Reset a CAM.
 *
 * @param fd File handle opened with dvbca_open.
 * @param slot Slot where the requested CAM is in.
 * @return 0 on success, -1 on failure.
 */
extern int dvbca_reset(int fd, uint8_t slot)
{
	printf("EN50221:%s called \n", __func__);
}

/**
 * Get the interface type of a CAM.
 *
 * @param fd File handle opened with dvbca_open.
 * @param slot Slot where the requested CAM is in.
 * @return One of the DVBCA_INTERFACE_* values, or -1 on failure.
 */
extern int dvbca_get_interface_type(int fd, uint8_t slot)
{
	printf("EN50221:%s called \n", __func__);
}

/**
 * Get the state of a CAM.
 *
 * @param fd File handle opened with dvbca_open.
 * @param slot Slot where the requested CAM is in.
 * @return One of the DVBCA_CAMSTATE_* values, or -1 on failure.
 */
extern int dvbca_get_cam_state(int fd, uint8_t slot)
{
	printf("EN50221:%s called \n", __func__);
}

/**
 * Write a message to a CAM using a link-layer interface.
 *
 * @param fd File handle opened with dvbca_open.
 * @param slot Slot where the requested CAM is in.
 * @param connection_id Connection ID of the message.
 * @param data Data to write.
 * @param data_length Number of bytes to write.
 * @return 0 on success, or -1 on failure.
 */
extern int dvbca_link_write(int fd, uint8_t slot, uint8_t connection_id,
		uint8_t *data, uint16_t data_length, void *arg)
{
	int ret = 0;
	uint8_t *buf = malloc(data_length + 2);
	struct joker_t *joker = (struct joker_t *)arg;

	jdebug("EN50221:%s slot=%d connection_id=%d len=%d called \n",
			__func__, slot, connection_id, data_length);

	if (!joker)
		return -EINVAL;

	if (!buf)
		return -ENOMEM;

	/* inject link layer (en50221) header */
	buf[0] = connection_id;
	buf[1] = 0x0; // TODO:More/Last indicator if fragmented
	memcpy(buf+2, data, data_length);

	if ((ret = joker_ci_write_data(joker, buf, data_length+2)) < 0)
		return -EIO;

	free(buf);
	return ret;
}

/**
 * Read a message from a CAM using a link-layer interface.
 *
 * @param fd File handle opened with dvbca_open.
 * @param slot Slot where the responding CAM is in.
 * @param connection_id Destination for the connection ID the message came from.
 * @param data Data that was read.
 * @param data_length Max number of bytes to read.
 * @return Number of bytes read on success, or -1 on failure.
 */
extern int dvbca_link_read(int fd, uint8_t *slot, uint8_t *connection_id,
		uint8_t *data, uint16_t data_length, void *arg)
{
	uint8_t *buf = malloc(data_length);
	struct joker_t *joker = (struct joker_t *)arg;
	int size = 0;
	int dst_off = 0;
	int more_data = 1;

	jdebug("EN50221:%s slot=%p connection_id=%p len=%d joker=%p buf=%p called \n",
			__func__, slot, connection_id, data_length, joker, buf);

	if (!joker)
		return -EINVAL;

	if (!buf)
		return -ENOMEM;

	/* from EN 50221:1997 standard:
	 * The first byte of the header is the Transport
	 * Connection Identifier for that TPDU fragment. The second byte contains a More/Last indicator in its most
	 * significant bit. If the bit is set to '1' then at least one more TPDU fragment follows, and if the bit is set to '0'
	 * then it indicates this is the last (or only) fragment of the TPDU for that Transport Connection.
	 */
	while (more_data) {
		if ((size = joker_ci_read_data(joker, buf, data_length)) < 0) {
			printf("EN50221:%s joker_ci_read_data failed, size=%d \n",
					__func__, size);
			return -EIO;
		}

		/* extract link layer (en50221) header */
		*connection_id = buf[0];
		*slot = 0x0; // TODO
		memcpy(data + dst_off, buf+2, size-2);
		dst_off += size-2;
		more_data = buf[1]&0x80;
	}

	free(buf);

	jdebug("EN50221:%s joker_ci_read_data done, size=%d \n",
			__func__, dst_off);
	return dst_off;
}

// FIXME how do we determine which CAM slot of a CA is meant?
/**
 * Write a message to a CAM using an HLCI interface.
 *
 * @param fd File handle opened with dvbca_open.
 * @param data Data to write.
 * @param data_length Number of bytes to write.
 * @return 0 on success, or -1 on failure.
 */
extern int dvbca_hlci_write(int fd, uint8_t *data, uint16_t data_length)
{
	printf("EN50221:%s called \n", __func__);
}

// FIXME how do we determine which CAM slot of a CA is meant?
/**
 * Read a message from a CAM using an HLCI interface.
 *
 * @param fd File handle opened with dvbca_open.
 * @param app_tag Application layer tag giving the message type to read.
 * @param data Data that was read.
 * @param data_length Max number of bytes to read.
 * @return Number of bytes read on success, or -1 on failure.
 */
extern int dvbca_hlci_read(int fd, uint32_t app_tag, uint8_t *data,
		uint16_t data_length)
{
	printf("EN50221:%s called \n", __func__);
}

int test_lookup_callback(void *arg, uint8_t slot_id, uint32_t requested_resource_id,
		en50221_sl_resource_callback *callback_out, void **arg_out, uint32_t *connected_resource_id)
{
	struct en50221_app_public_resource_id resid;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%s: requested_resource_id=%d \n", __func__, requested_resource_id);

	// decode the resource id
	if (en50221_app_decode_public_resource_id(&resid, requested_resource_id)) {
		jdebug("%02x:Public resource lookup callback %i %i %i\n", slot_id,
				resid.resource_class, resid.resource_type, resid.resource_version);
	} else {
		jdebug("%02x:Private resource lookup callback %08x\n", slot_id, requested_resource_id);
		return -1;
	}

	// FIXME: need better comparison
	// FIXME: return resourceid we actually connected to

	// try and find an instance of the resource
	int i;
	for(i=0; i<jen->resources_count; i++) {
		if ((resid.resource_class == jen->resources[i].resid.resource_class) &&
				(resid.resource_type == jen->resources[i].resid.resource_type)) {
			*callback_out = jen->resources[i].callback;
			*arg_out = jen->resources[i].arg;
			*connected_resource_id = jen->resources[i].binary_resource_id;
			return 0;
		}
	}

	return -1;
}

int test_session_callback(void *arg, int reason, uint8_t slot_id, uint16_t session_number, uint32_t resource_id)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	switch(reason) {
		case S_SCALLBACK_REASON_CAMCONNECTING:
			jdebug("%02x:CAM connecting to resource %08x, session_number %i\n",
					slot_id, resource_id, session_number);
			break;
		case S_SCALLBACK_REASON_CAMCONNECTED:
			jdebug("%02x:CAM successfully connected to resource %08x, session_number %i\n",
					slot_id, resource_id, session_number);

			if (resource_id == EN50221_APP_RM_RESOURCEID) {
				en50221_app_rm_enq(jen->rm_resource, session_number);
			} else if (resource_id == EN50221_APP_AI_RESOURCEID) {
				en50221_app_ai_enquiry(jen->ai_resource, session_number);
			} else if (resource_id == EN50221_APP_CA_RESOURCEID) {
				en50221_app_ca_info_enq(jen->ca_resource, session_number);
				jen->ca_session_number = session_number;
			}

			break;
		case S_SCALLBACK_REASON_CAMCONNECTFAIL:
			jdebug("%02x:CAM on failed to connect to resource %08x\n", slot_id, resource_id);
			break;
		case S_SCALLBACK_REASON_CONNECTED:
			jdebug("%02x:Host connection to resource %08x connected successfully, session_number %i\n",
					slot_id, resource_id, session_number);
			break;
		case S_SCALLBACK_REASON_CONNECTFAIL:
			jdebug("%02x:Host connection to resource %08x failed, session_number %i\n",
					slot_id, resource_id, session_number);
			break;
		case S_SCALLBACK_REASON_CLOSE:
			jdebug("%02x:Connection to resource %08x, session_number %i closed\n",
					slot_id, resource_id, session_number);
			break;
		case S_SCALLBACK_REASON_TC_CONNECT:
			jdebug("%02x:Host originated transport connection %i connected\n", slot_id, session_number);
			break;
		case S_SCALLBACK_REASON_TC_CAMCONNECT:
			jdebug("%02x:CAM originated transport connection %i connected\n", slot_id, session_number);
			break;
	}
	return 0;
}



int test_rm_enq_callback(void *arg, uint8_t slot_id, uint16_t session_number)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);

	if (en50221_app_rm_reply(jen->rm_resource, session_number, jen->resource_ids_count, resource_ids)) {
		printf("%02x:Failed to send reply to ENQ\n", slot_id);
	}

	return 0;
}

int test_rm_reply_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t resource_id_count, uint32_t *_resource_ids)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);

	uint32_t i;
	for(i=0; i< resource_id_count; i++) {
		jdebug("  CAM provided resource id: %08x\n", _resource_ids[i]);
	}

	if (en50221_app_rm_changed(jen->rm_resource, session_number)) {
		printf("%02x:Failed to send REPLY\n", slot_id);
	}

	return 0;
}

int test_rm_changed_callback(void *arg, uint8_t slot_id, uint16_t session_number)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);

	if (en50221_app_rm_enq(jen->rm_resource, session_number)) {
		printf("%02x:Failed to send ENQ\n", slot_id);
	}

	return 0;
}



int test_datetime_enquiry_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t response_interval)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);

	if (en50221_app_datetime_send(jen->datetime_resource, session_number, time(NULL), -1)) {
		printf("%02x:Failed to send datetime\n", slot_id);
	}

	return 0;
}



int test_ai_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t application_type, uint16_t application_manufacturer,
		uint16_t manufacturer_code, uint8_t menu_string_length,
		uint8_t *menu_string)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_ci_t * ci = NULL;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_ci_opaque || !joker->joker_en50221_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	/* example from real CAM module:
	 *   Application type: 01
	 *   Application manufacturer: 0b00
	 *   Manufacturer code: 8001
	 *   Menu string: Conax PRO CAM
	 *   Supported CA ID: 0b00
	 */
	jdebug("%02x:%s\n", slot_id, __func__);
	jdebug("  Application type: %02x\n", application_type);
	jdebug("  Application manufacturer: %04x\n", application_manufacturer);
	jdebug("  Manufacturer code: %04x\n", manufacturer_code);
	jdebug("  Menu string: %.*s\n", menu_string_length, menu_string);

	ci->application_type = application_type;
	ci->application_manufacturer = application_manufacturer;
	ci->manufacturer_code = manufacturer_code;
	memset(ci->menu_string, 0, TUPLE_MAX_SIZE);
	len = (menu_string_length > TUPLE_MAX_SIZE) ? TUPLE_MAX_SIZE - 1 : menu_string_length;
	memcpy(ci->menu_string, menu_string, len);

	if(joker->ci_info_callback)
		joker->ci_info_callback(joker);

	jen->ai_session_numbers[slot_id] = session_number;

	return 0;
}

int test_ca_info_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t ca_id_count, uint16_t *ca_ids)
{
	(void)session_number;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_ci_t * ci = NULL;
	struct joker_en50221_t * jen = NULL;
	struct big_pool_t *pool = NULL;
	struct program_t *program = NULL;
	int len = 0;
	uint32_t i;

	if (!joker || !joker->joker_ci_opaque || !joker->joker_en50221_opaque /* || !joker->pool */)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;
	pool = joker->pool;

	jdebug("%02x:%s\n", slot_id, __func__);
	for(i=0; i< ca_id_count; i++) {
		jdebug("  Supported CA ID[%d]: %04x\n", i, ca_ids[i]);
		ci->ca_ids[i] = ca_ids[i];
	}
	ci->ca_ids[i] = 0; /* end of list */

	if(joker->ci_caid_callback)
		joker->ci_caid_callback(joker);

	pthread_mutex_lock(&jen->mux);
	jen->ca_connected = 1;

	// send CA PMT to CAM if we have some in queue
	joker_en50221_sync_cam(joker);
	
	pthread_mutex_unlock(&jen->mux);

	return 0;
}

int test_ca_pmt_reply_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_pmt_reply *reply, uint32_t reply_size)
{
	(void)arg;
	(void)session_number;
	(void)reply;
	(void)reply_size;

	jdebug("%02x:%s\n", slot_id, __func__);
	jdebug("%s: CA_enable_flag=%d CA_enable=0x%x \n", __func__,
			reply->CA_enable_flag, reply->CA_enable);

	// from EN 50221:1997 standard:
	// CA_enable value (hex)
	// Descrambling possible 01
	// Descrambling possible under conditions (purchase dialogue) 02
	// Descrambling possible under conditions (technical dialogue) 03
	// Descrambling not possible (because no entitlement) 71
	// Descrambling not possible (for technical reasons) 73
	// RFU other values 00-7F
	switch (reply->CA_enable) {
		case 0x01:
			printf("%s: program=%d, Descrambling possible \n",
					__func__, reply->program_number );
			break;
		case 0x02:
			printf("%s: program=%d, Descrambling possible under conditions (purchase dialogue) \n",
					__func__, reply->program_number );
			break;
		case 0x03:
			printf("%s: program=%d, Descrambling possible under conditions (technical dialogue) \n",
					__func__, reply->program_number );
			break;
		case 0x71:
			printf("%s: program=%d, Descrambling not possible (because no entitlement) \n",
					__func__, reply->program_number );
			break;
		case 0x73:
			printf("%s: program=%d, Descrambling not possible (for technical reasons) \n",
					__func__, reply->program_number );
			break;
		default:
			printf("%s: program=%d, CA_enable=0x%x \n",
					__func__, reply->program_number, reply->CA_enable);
			break;
	}

	return 0;
}


int test_mmi_close_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t cmd_id, uint8_t delay)
{
	(void)arg;
	(void)session_number;

	printf("%02x:%s\n", slot_id, __func__);
	printf("  cmd_id: %02x\n", cmd_id);
	printf("  delay: %02x\n", delay);

	return 0;
}

int test_mmi_display_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t cmd_id, uint8_t mmi_mode)
{
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);
	jdebug("  cmd_id: %02x\n", cmd_id);
	jdebug("  mode: %02x\n", mmi_mode);

	if (cmd_id == MMI_DISPLAY_CONTROL_CMD_ID_SET_MMI_MODE) {
		struct en50221_app_mmi_display_reply_details details;

		details.u.mode_ack.mmi_mode = mmi_mode;
		if (en50221_app_mmi_display_reply(jen->mmi_resource, session_number, MMI_DISPLAY_REPLY_ID_MMI_MODE_ACK, &details)) {
			jdebug("%02x:Failed to send mode ack\n", slot_id);
		}
	}

	return 0;
}

int test_mmi_keypad_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t cmd_id, uint8_t *key_codes, uint32_t key_codes_count)
{
	(void)arg;
	(void)session_number;
	(void)cmd_id;
	(void)key_codes;
	(void)key_codes_count;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_subtitle_segment_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t *segment, uint32_t segment_size)
{
	(void)arg;
	(void)session_number;
	(void)segment;
	(void)segment_size;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_scene_end_mark_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t decoder_continue_flag, uint8_t scene_reveal_flag,
		uint8_t send_scene_done, uint8_t scene_tag)
{
	(void)arg;
	(void)session_number;
	(void)decoder_continue_flag;
	(void)scene_reveal_flag;
	(void)send_scene_done;
	(void)scene_tag;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_scene_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t decoder_continue_flag, uint8_t scene_reveal_flag,
		uint8_t scene_tag)
{
	(void)arg;
	(void)session_number;
	(void)decoder_continue_flag;
	(void)scene_reveal_flag;
	(void)scene_tag;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_subtitle_download_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t *segment, uint32_t segment_size)
{
	(void)arg;
	(void)session_number;
	(void)segment;
	(void)segment_size;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_flush_download_callback(void *arg, uint8_t slot_id, uint16_t session_number)
{
	(void)arg;
	(void)session_number;

	printf("%02x:%s\n", slot_id, __func__);

	return 0;
}

int test_mmi_enq_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		uint8_t blind_answer, uint8_t expected_answer_length,
		uint8_t *text, uint32_t text_size)
{
	(void)text;
	(void)text_size;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;


	printf("%02x:%s\n", slot_id, __func__);
	printf("  blind: %i\n", blind_answer);
	printf("  expected_answer_length: %i\n", expected_answer_length);

	jen->mmi_session_number = session_number;
	jen->in_enq = 1;

	return 0;
}

int test_mmi_menu_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_mmi_text *title,
		struct en50221_app_mmi_text *sub_title,
		struct en50221_app_mmi_text *bottom,
		uint32_t item_count, struct en50221_app_mmi_text *items,
		uint32_t item_raw_length, uint8_t *items_raw)
{
	(void)items_raw;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0, total = 0;
	unsigned char * buf = NULL;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	jdebug("%02x:%s\n", slot_id, __func__);
	buf = malloc(MAX_EN50221_BUF);
	if (!buf)
		return -ENOMEM;
	memset(buf, 0, MAX_EN50221_BUF);

	total = sprintf(buf, "\n");
	total += sprintf(buf + total, "  %.*s\n", title->text_length, title->text);
	total += sprintf(buf + total, "  %.*s\n", sub_title->text_length, sub_title->text);
	total += sprintf(buf + total, "  %.*s\n", bottom->text_length, bottom->text);

	uint32_t i;
	for(i=0; i< item_count; i++) {
		total += sprintf(buf + total, "    %i: %.*s\n", i+1, items[i].text_length, items[i].text);
	}
	jdebug("  raw_length: %i\n", item_raw_length);

	if (jen->cb)
		jen->cb(joker, buf, total);
	else
		printf("  %.*s\n", total, buf);

	jen->mmi_session_number = session_number;
	jen->in_menu = 1;

	return 0;
}

int test_app_mmi_list_callback(void *arg, uint8_t slot_id, uint16_t session_number,
		struct en50221_app_mmi_text *title,
		struct en50221_app_mmi_text *sub_title,
		struct en50221_app_mmi_text *bottom,
		uint32_t item_count, struct en50221_app_mmi_text *items,
		uint32_t item_raw_length, uint8_t *items_raw)
{
	(void)items_raw;
	(void)arg;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return -EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	printf("%02x:%s\n", slot_id, __func__);

	printf("  title: %.*s\n", title->text_length, title->text);
	printf("  sub_title: %.*s\n", sub_title->text_length, sub_title->text);
	printf("  bottom: %.*s\n", bottom->text_length, bottom->text);

	uint32_t i;
	for(i=0; i< item_count; i++) {
		printf("  item %i: %.*s\n", i+1, items[i].text_length, items[i].text);
	}
	printf("  raw_length: %i\n", item_raw_length);

	jen->mmi_session_number = session_number;
	jen->in_menu = 1;

	return 0;
}

void *ci_poll_func(void* arg) {
	struct en50221_transport_layer *tl = NULL;
	int lasterror = 0;
	struct joker_t * joker = (struct joker_t *)arg;
	struct joker_en50221_t * jen = NULL;
	int len = 0;

	if (!joker || !joker->joker_en50221_opaque)
		return (void *)-EINVAL;
	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;
	tl = jen->tl;
	if (!tl)
		return (void *)-EINVAL;

	while(!jen->shutdown_stackthread) {
		int error;
		if ((error = en50221_tl_poll(tl)) != 0) {
			if (error != lasterror) {
				jdebug(stderr, "POLL return stack slot:%i error:%i\n",
						en50221_tl_get_error_slot(tl),
						en50221_tl_get_error(tl));
			}
			lasterror = error;
		}
	}

	jen->shutdown_stackthread = 0;
	return 0;
}

/* parse PMT
 * return pointer to pmt
 * 0 if failed */
struct mpeg_pmt_section * joker_en50221_parse_pmt(void* _pmt, int len)
{
	struct mpeg_pmt_section *pmt = NULL;
	struct section_ext *ext = NULL;
	struct section *section = NULL;

	// parse it as a section
	// note: actually _pmt len is 1024 byte (hardcoded in libdvbpsi/src/tables/pmt.c)
	section = section_codec((uint8_t*) _pmt, len);
	if (!section) {
		printf("CAM: can't parse PMT as section\n");
		return 0;
	}
	
	// parse it as a section_ext
	ext = section_ext_decode(section, 0);
	if (!ext) {
		printf("CAM: can't parse PMT as ext section\n");
		return 0;
	}

	pmt = mpeg_pmt_section_codec(ext);
	if (!pmt) {
		printf("CAM: can't parse PMT \n");
		return 0;
	}

	return pmt;
}

/* add/find program in list
 * must me called with jen->mux locked for thread safe list operations
 * return pointer to created/found program_t
 * or 0 if failed
 */
struct program_t * joker_en50221_program_add_or_find(struct joker_en50221_t * jen, int program_num)
{
	struct program_t *program = NULL, *list_program = NULL;

	if (!jen)
		return 0;

	// find program in list
	if(!list_empty(&jen->programs_list)) {
		list_for_each_entry(list_program, &jen->programs_list, list) {
			if (list_program->number == program_num) {
				program = list_program;
				break; // program found in list 
			}
		}
	}

	// program not found. create new one and insert into list
	if (!program) {
		program = (struct program_t*)calloc(1, sizeof(*program));
		if (!program) {
			pthread_mutex_unlock(&jen->mux);
			return 0;
		}

		program->number = program_num;
		list_add_tail(&program->list, &jen->programs_list);
	}

	return program;
}

/* send all CA PMT to CAM
 * must be called with "jen->mux" locked
 * return 0 if success */
int joker_en50221_sync_cam(struct joker_t * joker)
{
	int ret = 0;
	struct program_t *program = NULL, *list_program = NULL;
	int count = 0, i = 0;
	int listmgmt = CA_LIST_MANAGEMENT_ONLY;
	struct mpeg_pmt_section * pmt = NULL;
	uint8_t capmt[4096];
	uint8_t buf[4096];
	int size = 0;
	struct joker_en50221_t * jen = NULL;

	if (!joker->joker_en50221_opaque)
		return -EINVAL;

	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	// CAM not ready yet. Do not sync now
	if (!jen->ca_connected)
		return 0;

	// find number of programs to descramble
	if(!list_empty(&jen->programs_list)) {
		list_for_each_entry(list_program, &jen->programs_list, list) {
			if (list_program->ci_status && list_program->pmt) {
				count++;
			}
		}
	}
	printf("%s: %d program(s) will send to CAM \n", __func__, count);

	// actual send CA PMT to CAM
	i = 0;
	if(!list_empty(&jen->programs_list) && count > 0) {
		list_for_each_entry(list_program, &jen->programs_list, list) {
			jdebug("%s: program %d found in list \n", __func__, list_program->number);
			if (list_program->ci_status && list_program->pmt) {
				// parse PMT as a section
				pmt = (struct mpeg_pmt_section *)list_program->pmt;

				if (count == 1) {
					listmgmt = CA_LIST_MANAGEMENT_ONLY;
				} else if (count > 1) {
					if (i == 0)
						listmgmt = CA_LIST_MANAGEMENT_FIRST;
					else if ((i + 1) == count)
						listmgmt = CA_LIST_MANAGEMENT_LAST;
					else
						listmgmt = CA_LIST_MANAGEMENT_MORE;
				}

				// prepare CA PMT
				if ((size = en50221_ca_format_pmt(pmt,
								capmt,
								sizeof(capmt),
								0,
								listmgmt,
								CA_PMT_CMD_ID_OK_DESCRAMBLING)) < 0) {
					printf("Failed to format CA PMT object for program=%d\n", list_program->number);
					continue;
				}

				// send it into CAM
				if (en50221_app_ca_pmt(jen->ca_resource,
							jen->ca_session_number,
							capmt, size)) {
					printf("Failed to send CA PMT object into CAM for program=%d\n", list_program->number);
					continue;
				}
				printf("CA PMT object sent into CAM for program=%d listmgmt=%d\n",
						list_program->number, listmgmt);

				// now program is inside CAM
				// we should update CAM if PMT changes for this program
				list_program->ci_status = CI_CAM_SENT;
				i++;
			}
		}
	}

	// Query descrambling status for all programs
	// this is just for information
	// CAM should send CA_enable (see 'test_ca_pmt_reply_callback')
	// we will send CA PMT for all programs anyway
	if(!list_empty(&jen->programs_list) && count > 0 && joker->cam_query_send) {
		list_for_each_entry(list_program, &jen->programs_list, list) {
			jdebug("%s: program %d found in list \n", __func__, list_program->number);
			if (list_program->ci_status && list_program->pmt) {
				// parse PMT as a section
				pmt = (struct mpeg_pmt_section *)list_program->pmt;
				listmgmt = CA_LIST_MANAGEMENT_ONLY;
				// prepare CA PMT
				if ((size = en50221_ca_format_pmt(pmt,
								capmt,
								sizeof(capmt),
								0,
								listmgmt,
								/* CA_PMT_CMD_ID_OK_MMI */ CA_PMT_CMD_ID_QUERY)) < 0) {
					printf("Failed to format CA PMT object for program=%d\n", list_program->number);
					continue;
				}

				// send it into CAM
				if (en50221_app_ca_pmt(jen->ca_resource,
							jen->ca_session_number,
							capmt, size)) {
					printf("Failed to send CA PMT object into CAM for program=%d\n", list_program->number);
					continue;
				}
				printf("CA PMT QUERY object sent into CAM for program=%d listmgmt=%d size=%d table_id_ext=%d curnext=%d\n",
						list_program->number, listmgmt, size, pmt->head.table_id_ext, pmt->head.current_next_indicator);
			}
		}
	}

	if (count > 0) {
		printf ("Enabling TS traffic through CAM \n");
		// enable/disable TS traffic through CAM
		buf[0] = J_CMD_CI_TS;
		buf[1] = joker->ci_ts_enable; // enable or disable
		if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
			return -EIO;
		printf ("Enabled TS traffic through CAM\n");
	}

	return ret;
}

/* add program to descramble list
 * return 0 if success
 */
int joker_en50221_descramble_add(struct joker_t * joker, int program_num)
{
	struct program_t *program = NULL;
	struct joker_en50221_t * jen = NULL;

	if (!joker)
		return -EINVAL;

	if (!joker->joker_en50221_opaque)
		if (joker_ci_en50221_init(joker))
			return -EINVAL;

	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	// add or update program in list
	pthread_mutex_lock(&jen->mux);
	if (!(program = joker_en50221_program_add_or_find(jen, program_num))) {
		pthread_mutex_unlock(&jen->mux);
		printf ("%s: Can't add/find program in list \n", __func__);
		return -ENOENT;
	}

	program->ci_status = CI_INIT; // intended to descramble

	pthread_mutex_unlock(&jen->mux);

	return 0;
}


/* update PMT for program
 * copy of PMT will be saved in internal list for later CAM updated
 * return 0 if success
 */
int joker_en50221_pmt_update(struct program_t *_program, void* _pmt, int len)
{
	struct joker_t * joker = _program->joker;
	struct joker_en50221_t * jen = NULL;
	struct program_t *program = NULL;
	int ret = 0;
	struct mpeg_pmt_section *pmt = NULL;
	uint8_t capmt[4096];
	int size = 0;

	if (!_pmt || !_program || !joker)
		return -EINVAL;

	if (!joker->joker_en50221_opaque)
		if (joker_ci_en50221_init(joker))
			return -EINVAL;

	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	// parse it as a section
	if (!(pmt = joker_en50221_parse_pmt(_pmt, len + 3))) {
		printf("CAM: can't parse PMT \n");
		return -EINVAL;
	}
	jdebug("CAM:joker_en50221_pmt_update: raw PMT parsed. version=%d curnext=%d\n",
			pmt->head.version_number, pmt->head.current_next_indicator);

	pthread_mutex_lock(&jen->mux);

	// find program in list or create new
	if (!(program = joker_en50221_program_add_or_find(jen, _program->number))) {
		pthread_mutex_unlock(&jen->mux);
		printf ("%s: Can't add/find program in list \n", __func__);
		return -ENOENT;
	}

	// update PMT for program if changed version or current_next_indicator
	if (program->i_version != pmt->head.version_number ||
			program->b_current_next != pmt->head.current_next_indicator) {
		if (program->pmt)
			free(program->pmt);

		// save this PMT for later actual CAM processing
		program->pmt = calloc(1, len + 3);
		if (!program->pmt) {
			pthread_mutex_unlock(&jen->mux);
			return -ENOMEM;
		}
		memcpy(program->pmt, _pmt, len + 3);
		program->pmt_len = len + 3;
		jdebug("CAM: new CA PMT for program=%d \n", program->number);

		program->i_version = pmt->head.version_number;
		program->b_current_next = pmt->head.current_next_indicator;

		// update CA PMT inside CAM module
		if (program->ci_status == CI_CAM_SENT) {
			// prepare CA PMT
			if ((size = en50221_ca_format_pmt(pmt,
							capmt,
							sizeof(capmt),
							0,
							CA_LIST_MANAGEMENT_UPDATE,
							CA_PMT_CMD_ID_OK_DESCRAMBLING)) < 0) {
				printf("Failed to format CA PMT object for program=%d\n", program->number);
				pthread_mutex_unlock(&jen->mux);
				return -EIO;
			}

			// send it into CAM
			if (en50221_app_ca_pmt(jen->ca_resource,
						jen->ca_session_number,
						capmt, size)) {
				printf("Failed to send CA PMT object into CAM for program=%d\n", program->number);
				pthread_mutex_unlock(&jen->mux);
				return -EIO;
			}
			printf("CA PMT object updated inside CAM for program=%d\n",
					program->number);

		}

		// program not sent to the cam yet. sync it
		if (program->ci_status == CI_INIT) {
			joker_en50221_sync_cam(joker);
		}
	}

	pthread_mutex_unlock(&jen->mux);

	return 0;
}

/* clear descramble program list
 * return 0 if success
 */
int joker_en50221_descramble_clear(struct joker_t * joker)
{
	struct program_t *program = NULL;
	struct joker_en50221_t * jen = NULL;

	if (!joker)
		return -EINVAL;

	if (!joker->joker_en50221_opaque)
		if (joker_ci_en50221_init(joker))
			return -EINVAL;

	jen = (struct joker_en50221_t *)joker->joker_en50221_opaque;

	// safe update programs list
	pthread_mutex_lock(&jen->mux);
	if(!list_empty(&jen->programs_list)) {
		list_for_each_entry(program, &jen->programs_list, list) {
			program->ci_status = CI_NONE; // not intended to descramble
		}
	}
	pthread_mutex_unlock(&jen->mux);

	return 0;
}
