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

int joker_ci_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	printf("EN50221:%s called \n",
			__func__);

	// if (!joker_ci_wait_status(joker, STATUSREG_DA, 1))
		// fds[0].revents = POLLPRI | POLLIN;

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

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	// create transport layer
	tl = en50221_tl_create(5 /* max_slots */,
			32 /* max_connections_per_slot */);
	if (tl == NULL) {
		printf("EN50221:%s Failed to create transport layer\n", __func__);
		return -EIO;
	}

	// register it with the CA stack
	if ((slot_id = en50221_tl_register_slot(tl, 1 /* fake fd */, 0, 1000, 100)) < 0) {
		printf("EN50221:%s Slot registration failed\n", __func__);
		return -EIO;
	}

	// create session layer
	sl = en50221_sl_create(tl, 256);
	if (sl == NULL) {
		printf("EN50221:%s Failed to create session layer\n", __func__);
		return -EIO;
	}

	if((ret = en50221_tl_poll(tl))) {
		printf("EN50221:%s Failed to poll. ret=%d\n", __func__, ret);
		return -EIO;
	}

	printf("EN50221:%s success\n", __func__);
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
	printf("EN50221:%s slot=%d connection_id=%d len=%d called \n",
			__func__, slot, connection_id, data_length);
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
	printf("EN50221:%s slot=%p connection_id=%p len=%d called \n",
			__func__, slot, connection_id, data_length);
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
}


