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
#include <signal.h>
#include <poll.h>
#include <libucsi/section.h>
#include <libucsi/mpeg/section.h>

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

/* TODO: !*/
struct joker_t *s_joker = NULL;

/****** stuff from ./test/libdvben50221/test-app.c */
void *stackthread_func(void* arg);
void *pmtthread_func(void* arg);
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

int adapterid;

int shutdown_stackthread = 0;
int shutdown_pmtthread = 0;
int in_menu = 0;
int in_enq = 0;
int ca_connected = 0;
int pmt_pid = -1;
int ca_session_number = 0;

// instances of resources we actually implement here
struct en50221_app_rm *rm_resource;
struct en50221_app_datetime *datetime_resource;
struct en50221_app_ai *ai_resource;
struct en50221_app_ca *ca_resource;
struct en50221_app_mmi *mmi_resource;

// lookup table used in resource manager implementation
struct resource {
    struct en50221_app_public_resource_id resid;
    uint32_t binary_resource_id;
    en50221_sl_resource_callback callback;
    void *arg;
};
struct resource resources[20];
int resources_count = 0;

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
int resource_ids_count = sizeof(resource_ids)/4;


uint16_t ai_session_numbers[5];

uint16_t mmi_session_number;

int joker_ci_poll(struct pollfd *fds, nfds_t nfds, int timeout, void *arg)
{
	struct joker_t * joker = (struct joker_t *)arg;

	if (!joker)
		return -EINVAL;

	// printf("EN50221:%s \n", __func__);
	if (!joker_ci_wait_status(joker, STATUSREG_DA, timeout /* msec */)) {
		// printf("EN50221:%s has data\n", __func__);
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
int joker_ci_en50221(struct joker_t * joker)
{
	int ret = -EINVAL, i = 0, j = 0;
	int slot_id = 0;
	struct joker_ci_t * ci = NULL;
	struct en50221_transport_layer *tl = NULL;
	struct en50221_session_layer *sl = NULL;
	struct en50221_app_send_functions sendfuncs;
	char tmp[256];
	pthread_t stackthread;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	s_joker = joker;
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
	if ((slot_id = en50221_tl_register_slot(tl, 1 /* fake fd */, 0, 4000, 100)) < 0) {
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
	sendfuncs.arg        = sl;
	sendfuncs.send_data  = (en50221_send_data) en50221_sl_send_data;
	sendfuncs.send_datav = (en50221_send_datav) en50221_sl_send_datav;

	// create the resource manager resource
	rm_resource = en50221_app_rm_create(&sendfuncs);
	en50221_app_decode_public_resource_id(&resources[resources_count].resid, EN50221_APP_RM_RESOURCEID);
	resources[resources_count].binary_resource_id = EN50221_APP_RM_RESOURCEID;
	resources[resources_count].callback = (en50221_sl_resource_callback) en50221_app_rm_message;
	resources[resources_count].arg = rm_resource;
	en50221_app_rm_register_enq_callback(rm_resource, test_rm_enq_callback, NULL);
	en50221_app_rm_register_reply_callback(rm_resource, test_rm_reply_callback, NULL);
	en50221_app_rm_register_changed_callback(rm_resource, test_rm_changed_callback, NULL);
	resources_count++;

	// create the datetime resource
	datetime_resource = en50221_app_datetime_create(&sendfuncs);
	en50221_app_decode_public_resource_id(&resources[resources_count].resid, EN50221_APP_DATETIME_RESOURCEID);
	resources[resources_count].binary_resource_id = EN50221_APP_DATETIME_RESOURCEID;
	resources[resources_count].callback = (en50221_sl_resource_callback) en50221_app_datetime_message;
	resources[resources_count].arg = datetime_resource;
	en50221_app_datetime_register_enquiry_callback(datetime_resource, test_datetime_enquiry_callback, NULL);
	resources_count++;

	// create the application information resource
	ai_resource = en50221_app_ai_create(&sendfuncs);
	en50221_app_decode_public_resource_id(&resources[resources_count].resid, EN50221_APP_AI_RESOURCEID);
	resources[resources_count].binary_resource_id = EN50221_APP_AI_RESOURCEID;
	resources[resources_count].callback = (en50221_sl_resource_callback) en50221_app_ai_message;
	resources[resources_count].arg = ai_resource;
	en50221_app_ai_register_callback(ai_resource, test_ai_callback, NULL);
	resources_count++;

	// create the CA resource
	ca_resource = en50221_app_ca_create(&sendfuncs);
	en50221_app_decode_public_resource_id(&resources[resources_count].resid, EN50221_APP_CA_RESOURCEID);
	resources[resources_count].binary_resource_id = EN50221_APP_CA_RESOURCEID;
	resources[resources_count].callback = (en50221_sl_resource_callback) en50221_app_ca_message;
	resources[resources_count].arg = ca_resource;
	en50221_app_ca_register_info_callback(ca_resource, test_ca_info_callback, NULL);
	en50221_app_ca_register_pmt_reply_callback(ca_resource, test_ca_pmt_reply_callback, NULL);
	resources_count++;

	// create the MMI resource
	mmi_resource = en50221_app_mmi_create(&sendfuncs);
	en50221_app_decode_public_resource_id(&resources[resources_count].resid, EN50221_APP_MMI_RESOURCEID);
	resources[resources_count].binary_resource_id = EN50221_APP_MMI_RESOURCEID;
	resources[resources_count].callback = (en50221_sl_resource_callback) en50221_app_mmi_message;
	resources[resources_count].arg = mmi_resource;
	en50221_app_mmi_register_close_callback(mmi_resource, test_mmi_close_callback, NULL);
	en50221_app_mmi_register_display_control_callback(mmi_resource, test_mmi_display_control_callback, NULL);
	en50221_app_mmi_register_keypad_control_callback(mmi_resource, test_mmi_keypad_control_callback, NULL);
	en50221_app_mmi_register_subtitle_segment_callback(mmi_resource, test_mmi_subtitle_segment_callback, NULL);
	en50221_app_mmi_register_scene_end_mark_callback(mmi_resource, test_mmi_scene_end_mark_callback, NULL);
	en50221_app_mmi_register_scene_control_callback(mmi_resource, test_mmi_scene_control_callback, NULL);
	en50221_app_mmi_register_subtitle_download_callback(mmi_resource, test_mmi_subtitle_download_callback, NULL);
	en50221_app_mmi_register_flush_download_callback(mmi_resource, test_mmi_flush_download_callback, NULL);
	en50221_app_mmi_register_enq_callback(mmi_resource, test_mmi_enq_callback, NULL);
	en50221_app_mmi_register_menu_callback(mmi_resource, test_mmi_menu_callback, NULL);
	en50221_app_mmi_register_list_callback(mmi_resource, test_app_mmi_list_callback, NULL);
	resources_count++;

	// start another thread running the stack
	pthread_create(&stackthread, NULL, stackthread_func, tl);

	// register callbacks
	en50221_sl_register_lookup_callback(sl, test_lookup_callback, sl);
	en50221_sl_register_session_callback(sl, test_session_callback, sl);

	// create a new connection on each slot
	int tc = en50221_tl_new_tc(tl, slot_id);

	printf("Press a key to enter menu\n");
	getchar();
	sleep(1); // prophylactic
	en50221_app_ai_entermenu(ai_resource, ai_session_numbers[slot_id]);
	printf("Entering MMI menu ... \n");

	while(1) {
		fgets(tmp, sizeof(tmp), stdin);
		int choice = atoi(tmp);

		if (in_menu) {
			en50221_app_mmi_menu_answ(mmi_resource, mmi_session_number, choice);
			in_menu = 0;
		}
		if (in_enq) {
			uint32_t i;
			uint32_t len = strlen(tmp);
			for(i=0; i< len; i++) {
				if (!isdigit(tmp[i])) {
					len = i;
					break;
				}
			}
			en50221_app_mmi_answ(mmi_resource, mmi_session_number, MMI_ANSW_ID_ANSWER, (uint8_t*) tmp, len);
			in_enq = 0;
		}
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
		uint8_t *data, uint16_t data_length)
{
	int ret = 0;
	uint8_t *buf = malloc(data_length + 2);
	jdebug("EN50221:%s slot=%d connection_id=%d len=%d called \n",
			__func__, slot, connection_id, data_length);

	if (!buf)
		return -ENOMEM;

	/* inject link layer (en50221) header */
	buf[0] = connection_id;
	buf[1] = 0x0; // TODO:More/Last indicator if fragmented
	memcpy(buf+2, data, data_length);

	if ((ret = joker_ci_write_data(s_joker, buf, data_length+2)) < 0)
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
		uint8_t *data, uint16_t data_length)
{
	int size = 0;
	uint8_t *buf = malloc(data_length + 2);

	jdebug("EN50221:%s slot=%p connection_id=%p len=%d called \n",
			__func__, slot, connection_id, data_length);

	if (!buf)
		return -ENOMEM;

	if ((size = joker_ci_read_data(s_joker, buf, data_length+2)) < 0)
			return -EIO;

	/* extract link layer (en50221) header */
	*connection_id = buf[0];
	*slot = 0x0; // TODO
	memcpy(data, buf+2, size-2);
	free(buf);

	return size-2;
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
    (void)arg;

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
    for(i=0; i<resources_count; i++) {
        if ((resid.resource_class == resources[i].resid.resource_class) &&
            (resid.resource_type == resources[i].resid.resource_type)) {
            *callback_out = resources[i].callback;
            *arg_out = resources[i].arg;
            *connected_resource_id = resources[i].binary_resource_id;
            return 0;
        }
    }

    return -1;
}

int test_session_callback(void *arg, int reason, uint8_t slot_id, uint16_t session_number, uint32_t resource_id)
{
    (void)arg;
    switch(reason) {
        case S_SCALLBACK_REASON_CAMCONNECTING:
            jdebug("%02x:CAM connecting to resource %08x, session_number %i\n",
                   slot_id, resource_id, session_number);
            break;
        case S_SCALLBACK_REASON_CAMCONNECTED:
            jdebug("%02x:CAM successfully connected to resource %08x, session_number %i\n",
                   slot_id, resource_id, session_number);

            if (resource_id == EN50221_APP_RM_RESOURCEID) {
                en50221_app_rm_enq(rm_resource, session_number);
            } else if (resource_id == EN50221_APP_AI_RESOURCEID) {
                en50221_app_ai_enquiry(ai_resource, session_number);
            } else if (resource_id == EN50221_APP_CA_RESOURCEID) {
                en50221_app_ca_info_enq(ca_resource, session_number);
                ca_session_number = session_number;
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
    (void)arg;

    jdebug("%02x:%s\n", slot_id, __func__);

    if (en50221_app_rm_reply(rm_resource, session_number, resource_ids_count, resource_ids)) {
        jdebug("%02x:Failed to send reply to ENQ\n", slot_id);
    }

    return 0;
}

int test_rm_reply_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t resource_id_count, uint32_t *_resource_ids)
{
    (void)arg;
    jdebug("%02x:%s\n", slot_id, __func__);

    uint32_t i;
    for(i=0; i< resource_id_count; i++) {
        jdebug("  CAM provided resource id: %08x\n", _resource_ids[i]);
    }

    if (en50221_app_rm_changed(rm_resource, session_number)) {
        jdebug("%02x:Failed to send REPLY\n", slot_id);
    }

    return 0;
}

int test_rm_changed_callback(void *arg, uint8_t slot_id, uint16_t session_number)
{
    (void)arg;
    jdebug("%02x:%s\n", slot_id, __func__);

    if (en50221_app_rm_enq(rm_resource, session_number)) {
        jdebug("%02x:Failed to send ENQ\n", slot_id);
    }

    return 0;
}



int test_datetime_enquiry_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t response_interval)
{
    (void)arg;
    jdebug("%02x:%s\n", slot_id, __func__);
    jdebug("  response_interval:%i\n", response_interval);

    if (en50221_app_datetime_send(datetime_resource, session_number, time(NULL), -1)) {
        jdebug("%02x:Failed to send datetime\n", slot_id);
    }

    return 0;
}



int test_ai_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                     uint8_t application_type, uint16_t application_manufacturer,
                     uint16_t manufacturer_code, uint8_t menu_string_length,
                     uint8_t *menu_string)
{
    (void)arg;

    jdebug("%02x:%s\n", slot_id, __func__);
    printf("  Application type: %02x\n", application_type);
    printf("  Application manufacturer: %04x\n", application_manufacturer);
    printf("  Manufacturer code: %04x\n", manufacturer_code);
    printf("  Menu string: %.*s\n", menu_string_length, menu_string);

    ai_session_numbers[slot_id] = session_number;

    return 0;
}



int test_ca_info_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint32_t ca_id_count, uint16_t *ca_ids)
{
    (void)arg;
    (void)session_number;

    jdebug("%02x:%s\n", slot_id, __func__);
    uint32_t i;
    for(i=0; i< ca_id_count; i++) {
        printf("  Supported CA ID: %04x\n", ca_ids[i]);
    }

    ca_connected = 1;
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

    return 0;
}


int test_mmi_close_callback(void *arg, uint8_t slot_id, uint16_t session_number, uint8_t cmd_id, uint8_t delay)
{
    (void)arg;
    (void)session_number;

    jdebug("%02x:%s\n", slot_id, __func__);
    jdebug("  cmd_id: %02x\n", cmd_id);
    jdebug("  delay: %02x\n", delay);

    return 0;
}

int test_mmi_display_control_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                                        uint8_t cmd_id, uint8_t mmi_mode)
{
    (void)arg;
    (void)session_number;

    jdebug("%02x:%s\n", slot_id, __func__);
    jdebug("  cmd_id: %02x\n", cmd_id);
    jdebug("  mode: %02x\n", mmi_mode);

    if (cmd_id == MMI_DISPLAY_CONTROL_CMD_ID_SET_MMI_MODE) {
        struct en50221_app_mmi_display_reply_details details;

        details.u.mode_ack.mmi_mode = mmi_mode;
        if (en50221_app_mmi_display_reply(mmi_resource, session_number, MMI_DISPLAY_REPLY_ID_MMI_MODE_ACK, &details)) {
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

    jdebug("%02x:%s\n", slot_id, __func__);

    return 0;
}

int test_mmi_subtitle_segment_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                                        uint8_t *segment, uint32_t segment_size)
{
    (void)arg;
    (void)session_number;
    (void)segment;
    (void)segment_size;

    jdebug("%02x:%s\n", slot_id, __func__);

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

    jdebug("%02x:%s\n", slot_id, __func__);

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

    jdebug("%02x:%s\n", slot_id, __func__);

    return 0;
}

int test_mmi_subtitle_download_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                                        uint8_t *segment, uint32_t segment_size)
{
    (void)arg;
    (void)session_number;
    (void)segment;
    (void)segment_size;

    jdebug("%02x:%s\n", slot_id, __func__);

    return 0;
}

int test_mmi_flush_download_callback(void *arg, uint8_t slot_id, uint16_t session_number)
{
    (void)arg;
    (void)session_number;

    jdebug("%02x:%s\n", slot_id, __func__);

    return 0;
}

int test_mmi_enq_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                            uint8_t blind_answer, uint8_t expected_answer_length,
                            uint8_t *text, uint32_t text_size)
{
    (void)arg;
    (void)text;
    (void)text_size;

    jdebug("%02x:%s\n", slot_id, __func__);
    jdebug("  blind: %i\n", blind_answer);
    jdebug("  expected_answer_length: %i\n", expected_answer_length);

    mmi_session_number = session_number;
    in_enq = 1;

    return 0;
}

int test_mmi_menu_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                            struct en50221_app_mmi_text *title,
                            struct en50221_app_mmi_text *sub_title,
                            struct en50221_app_mmi_text *bottom,
                            uint32_t item_count, struct en50221_app_mmi_text *items,
                            uint32_t item_raw_length, uint8_t *items_raw)
{
    (void)arg;
    (void)items_raw;

    jdebug("%02x:%s\n", slot_id, __func__);

    printf("\n");
    printf("  %.*s\n", title->text_length, title->text);
    printf("  %.*s\n", sub_title->text_length, sub_title->text);
    printf("  %.*s\n", bottom->text_length, bottom->text);

    uint32_t i;
    for(i=0; i< item_count; i++) {
        printf("    %i: %.*s\n", i+1, items[i].text_length, items[i].text);
    }
    jdebug("  raw_length: %i\n", item_raw_length);

    mmi_session_number = session_number;
    in_menu = 1;

    return 0;
}

int test_app_mmi_list_callback(void *arg, uint8_t slot_id, uint16_t session_number,
                                struct en50221_app_mmi_text *title,
                                struct en50221_app_mmi_text *sub_title,
                                struct en50221_app_mmi_text *bottom,
                                uint32_t item_count, struct en50221_app_mmi_text *items,
                                uint32_t item_raw_length, uint8_t *items_raw)
{
    (void)arg;
    (void)items_raw;
    (void)arg;

    printf("%02x:%s\n", slot_id, __func__);

    printf("  title: %.*s\n", title->text_length, title->text);
    printf("  sub_title: %.*s\n", sub_title->text_length, sub_title->text);
    printf("  bottom: %.*s\n", bottom->text_length, bottom->text);

    uint32_t i;
    for(i=0; i< item_count; i++) {
        printf("  item %i: %.*s\n", i+1, items[i].text_length, items[i].text);
    }
    printf("  raw_length: %i\n", item_raw_length);

    mmi_session_number = session_number;
    in_menu = 1;

    return 0;
}

void *stackthread_func(void* arg) {
    struct en50221_transport_layer *tl = arg;
    int lasterror = 0;

    while(!shutdown_stackthread) {
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

    shutdown_stackthread = 0;
    return 0;
}

void *pmtthread_func(void* arg) {
    (void)arg;
    char buf[4096];
    uint8_t capmt[4096];
    int pmtversion = -1;
    struct mpeg_pmt_section *pmt = NULL;

    while(!shutdown_pmtthread) {

        if (!ca_connected) {
            sleep(1);
            continue;
        }

#if 0
        // read the PMT
        struct section_ext *section_ext = read_section_ext(buf, sizeof(buf), adapterid, 0, pmt_pid, stag_mpeg_program_map);
        if (section_ext == NULL) {
            jdebug(stderr, "Failed to read PMT\n");
            exit(1);
        }
        struct mpeg_pmt_section *pmt = mpeg_pmt_section_codec(section_ext);
        if (pmt == NULL) {
            jdebug(stderr, "Bad PMT received\n");
            exit(1);
        }
        if (pmt->head.version_number == pmtversion) {
            continue;
        }
#endif

        // translate it into a CA PMT
        int listmgmt = CA_LIST_MANAGEMENT_ONLY;
        if (pmtversion != -1) {
            listmgmt = CA_LIST_MANAGEMENT_UPDATE;
        }
        int size;
        if ((size = en50221_ca_format_pmt(pmt,
                                         capmt,
                                         sizeof(capmt),
                                         listmgmt,
                                         0,
                                         CA_PMT_CMD_ID_OK_DESCRAMBLING)) < 0) {
            jdebug(stderr, "Failed to format CA PMT object\n");
            exit(1);
        }

        // set it
        if (en50221_app_ca_pmt(ca_resource, ca_session_number, capmt, size)) {
            jdebug(stderr, "Failed to send CA PMT object\n");
            exit(1);
        }
        pmtversion = pmt->head.version_number;
    }
    shutdown_pmtthread = 0;
    return 0;
}


struct section_ext *read_section_ext(char *buf, int buflen, int adapter, int demux, int pid, int table_id)
{
    int demux_fd = -1;
    uint8_t filter[18];
    uint8_t mask[18];
    int size;
    struct section *section;
    struct section_ext *result = NULL;

#if 0
    // open the demuxer
    if ((demux_fd = dvbdemux_open_demux(adapter, demux, 0)) < 0) {
        goto exit;
    }

    // create a section filter
    memset(filter, 0, sizeof(filter));
    memset(mask, 0, sizeof(mask));
    filter[0] = table_id;
    mask[0] = 0xFF;
    if (dvbdemux_set_section_filter(demux_fd, pid, filter, mask, 1, 1)) {
        goto exit;
    }

    // read the section
    if ((size = read(demux_fd, buf, buflen)) < 0) {
        goto exit;
    }

    // parse it as a section
    section = section_codec((uint8_t*) buf, size);
    if (section == NULL) {
        goto exit;
    }

    // parse it as a section_ext
    result = section_ext_decode(section, 0);

exit:
    if (demux_fd != -1)
        close(demux_fd);
    return result;
#endif
}
