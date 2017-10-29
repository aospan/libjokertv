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

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>
#include <joker_utils.h>

int joker_ci_wait_status(struct joker_t * joker, uint8_t waitfor, int timeout);
int joker_ci_rw(struct joker_t * joker, int command, uint16_t offset, unsigned char *buf, int size);

struct ci_thread_opaq_t
{
	/* ci thread */
	pthread_t ci_thread;
	pthread_cond_t cond;
	pthread_mutex_t mux;
	int cancel;
};

int joker_ci_read(struct joker_t * joker, int offset, int area)
{
	int ret = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	ret = joker_ci_rw(joker,
			JOKER_CI_CTRL_READ | ((area == JOKER_CI_IO) ? JOKER_CI_CTRL_IO : JOKER_CI_CTRL_MEM),
			offset, buf, 1);
	if (ret < 0) {
		printf("CAM:%s can't read from offset 0x%x\n", __func__, offset);
		return ret;
	}

	return buf[0];
}

int joker_ci_write(struct joker_t * joker, int offset, int area, char data)
{
	int ret = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	buf[0] = data;
	ret = joker_ci_rw(joker,
			JOKER_CI_CTRL_WRITE | ((area == JOKER_CI_IO) ? JOKER_CI_CTRL_IO : JOKER_CI_CTRL_MEM),
			offset, buf, 1);
	if (ret < 0)
		printf("CAM:%s can't write to offset 0x%x\n", __func__, offset);

	return ret;
}

/* CI rw IO or MEM
 * return read/write bytes if success
 * or negative value if failed
 */
int joker_ci_rw(struct joker_t * joker, int command, uint16_t offset, unsigned char *buf, int size)
{
	int bytes_read = 0;
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;
	unsigned char out_buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	if (size > (JCMD_BUF_LEN - 6))
		return -EINVAL;

	memset(out_buf, 0, JCMD_BUF_LEN);

	out_buf[0] = J_CMD_CI_RW;
	out_buf[1] = command;
	out_buf[2] = (size >> 8) & 0xFF;
	out_buf[3] = (size) & 0xFF;
	out_buf[4] = (offset >> 8) & 0xFF;
	out_buf[5] = (offset) & 0xFF;
	memcpy(&out_buf[6], buf, size);

	if (ci->ci_verbose) {
		printf("CAM:%s offset=0x%x size=%d req hexdump (%d bytes):\n",
				__func__, offset, size, 6 + ((command&JOKER_CI_CTRL_WRITE) ? size : 0));
		hexdump(out_buf, 6 + ((command&JOKER_CI_CTRL_WRITE) ? size : 0) );
	}

	if ((ret = joker_cmd(joker, out_buf, size + 6, in_buf, size + 4 /* in_len */)))
		return -EIO;

	if (ci->ci_verbose) {
		printf("CAM:%s result=0x%x\n", __func__, in_buf[1]);
		hexdump(in_buf, 8);
	}

	if(in_buf[1] == JOKER_CI_CTRL_OK) {
		if (command & JOKER_CI_CTRL_READ) {
			// this was a read command
			memcpy(buf, &in_buf[4], size);
			if (ci->ci_verbose) {
				printf("CAM:%s read %d bytes:\n",
						__func__, size);
				hexdump(buf, size);
			}


		}
		// return read or written bytes count
		return (in_buf[2] << 8 | in_buf[3]);
	} else {
		printf("CAM:%s request failed. result=0x%x \n", __func__, in_buf[1]);
		hexdump(in_buf, 6);
	}

	return -EIO;
}

int joker_ci_read_data(struct joker_t * joker, unsigned char *buf, int size)
{
	int bytes_read = 0;
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;
	unsigned char out_buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	jdebug("CAM:%s: start. size=%d \n", __func__, size);
	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	if (size > (JCMD_BUF_LEN - 6))
		size = JCMD_BUF_LEN - 6;

	ret = joker_ci_wait_status(joker, STATUSREG_DA, 1000);
	if (ret != 0) {
		printf("CAM:%s: no data \n", __func__);
		return -EIO;
	}

	ret = joker_ci_read(joker, CTRLIF_SIZE_HIGH, JOKER_CI_IO);
	if (ret < 0) {
		printf("CAM:%s: can't read CTRLIF_SIZE_HIGH\n", __func__);
		return -EIO;
	}
	bytes_read = ret << 8;

	ret = joker_ci_read(joker, CTRLIF_SIZE_LOW, JOKER_CI_IO);
	if (ret < 0) {
		printf("CAM:%s: can't read CTRLIF_SIZE_HIGH\n", __func__);
		return -EIO;
	}
	bytes_read |= ret;

	if (bytes_read > size) {
		printf("CAM:%s: can't fit data (%d) to buffer (%d)\n",
				__func__, bytes_read, size);
		return -ENOMEM;
	}

	jdebug("CAM:%s: try to read %d bytes from CAM\n", __func__, bytes_read);

	for (i = 0; i < bytes_read; i++) {
		ret = joker_ci_read(joker, CTRLIF_DATA, JOKER_CI_IO);
		if (ret < 0) {
			printf("CAM:%s: can't read CTRLIF_DATA\n", __func__);
			return -EIO;
		}

		buf[i] = ret;
	}

	ret = joker_ci_read(joker, CTRLIF_STATUS, JOKER_CI_IO);
	if (ret < 0)
		return -EIO;

	if (ret & STATUSREG_RE) {
		printf("CAM:%s: read error indicated by cam. status=0x%x\n", __func__, ret);
		return -EIO;
	}

	if (ret & STATUSREG_DA) {
		printf("CAM:%s More data available! status=0x%x\n", __func__, ret);
	}

	if (ci->ci_verbose) {
		printf("CAM:%s: read %d bytes from CAM. hexdump:\n", __func__, bytes_read);
		hexdump(buf, bytes_read);
	}

	return bytes_read;
}

/* CI write IO
 * return written bytes if success
 * or negative value if failed
 *
 * all EN50221 write logic is inside FPGA for stability:
 * some CAM's fail if we write byte-by-byte from host
 * i'm suspect because of big delay between bytes (but need to proof)
 */
int joker_ci_write_data(struct joker_t * joker, unsigned char *buf, int size)
{
	int bytes_read = 0;
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;
	unsigned char out_buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	jdebug("CAM:%s write size=%d\n", __func__, size);
	if (size > (JCMD_BUF_LEN - 6))
		return -EINVAL;

	ret = joker_ci_rw(joker,
			JOKER_CI_CTRL_WRITE | JOKER_CI_CTRL_IO | JOKER_CI_CTRL_BULK,
			0x0, buf, size);
	if (ret < 0) {
		printf("CAM:%s can't write\n", __func__);
		return -EIO;
	}

	return size;
}

int joker_ci_read_tuple(struct joker_t * joker, struct ci_tuple_t *tuple)
{
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;
	unsigned char buf[JCMD_BUF_LEN];

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* clean tuple before reading */
	memset(tuple, 0, sizeof(struct ci_tuple_t));

	ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, ci->tuple_cur_offset, buf, 1);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}
	ci->tuple_cur_offset += 2;

	tuple->type = buf[0];
	// end of chain tuple
	if (tuple->type == 0xFF) {
		return 0;
	}

	ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, ci->tuple_cur_offset, buf, 1);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}
	ci->tuple_cur_offset += 2;
	tuple->size = buf[0];

	if (tuple->size > TUPLE_MAX_SIZE) {
		printf("Too big tuple found (%d bytes) \n", tuple->size);
		return -EIO;
	}

	// read tuple data
	ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, ci->tuple_cur_offset, buf, tuple->size*2);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}

	for (i = 0; i < tuple->size; i++) {
		if (ret < 0) {
			printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
			return -EIO;
		}
		ci->tuple_cur_offset += 2;
		tuple->data[i] = buf[i*2];
	}

	return 0;
}

/* read and validate CAM module tuple
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_get_next_tuple(struct joker_t * joker, struct ci_tuple_t *tuple, int type, int size)
{
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	if (joker_ci_read_tuple(joker, tuple))
		return -EIO;

	// validate tuple as asked
	if (type && tuple->type != type) {
		printf("CAM: tuple type mismatch 0x%x expected 0x%x \n",
			tuple->type, type);
		return -EIO;
	}

	if (size && size != tuple->size) {
		printf("CAM: tuple size mismatch %d expected %d \n",
			tuple->size, size);
		return -EIO;
	}

	if (ci->ci_verbose)
		printf("CAM: tuple 0x%x size %d found\n",
				tuple->type, tuple->size);
	return 0;
}

/* parse CAM module attributes (tuples)
 * inspired by drivers/media/dvb-core/dvb_ca_en50221.c from Linux kernel
 * return 0 if success
 * other return values indicates error
 */
int joker_ci_parse_attributes(struct joker_t * joker)
{
	int tuple_type = 0;
	int i = 0;
	struct ci_tuple_t tuple;
	struct joker_ci_t * ci = NULL;
	int rasz;
	int got_cftableentry = 0, end_chain = 0;
	int off0, off1, off2;
	int ret;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	memset(&tuple, 0, sizeof(struct ci_tuple_t));

	// CISTPL_DEVICE_0A
	if (joker_ci_get_next_tuple(joker, &tuple, 0x1D, 0))
		return -EIO;

	// CISTPL_DEVICE_0C
	if (joker_ci_get_next_tuple(joker, &tuple, 0x1C, 0))
		return -EIO;

	// CISTPL_VERS_1
	if (joker_ci_get_next_tuple(joker, &tuple, 0x15, 0))
		return -EIO;
	off0 = 2;
	off1 = strlen(tuple.data+off0) + off0 + 1;
	off2 = strlen(tuple.data+off1) + off1 + 1;

	sprintf(ci->cam_infostring, "%s %s %s",
			(char*)tuple.data+off0,
			(char*)tuple.data+off1,
			(char*)tuple.data+off2);

	// CISTPL_MANFID
	if (joker_ci_get_next_tuple(joker, &tuple, 0x20, 4))
		return -EIO;
	ci->manfid = (tuple.data[1] << 8) | tuple.data[0];
	ci->devid = (tuple.data[3] << 8) | tuple.data[2];

	// CISTPL_CONFIG
	if (joker_ci_get_next_tuple(joker, &tuple, 0x1A, 0))
		return -EIO;
	if (tuple.size < 3) {
		printf ("CAM: tuple 0x1A (CISTPL_CONFIG) size (%d) not less than 3\n",
				tuple.size);
		return -EIO;
	}

	/* extract the configbase */
	rasz = tuple.data[0] & 3;
	if (tuple.size < (3 + rasz + 14))
		return -EIO;
	ci->config_base = 0;
	for (i = 0; i < rasz + 1; i++) {
		ci->config_base |= (tuple.data[2 + i] << (8 * i));
	}

	/* validate CAM version */
	if (!xmemmem(tuple.data, tuple.size, "DVB_CI_V", strlen("DVB_CI_V"))) {
		printf("CAM: can't find DVB_CI_V string \n");
		return -EIO;
	}

	if (!xmemmem(tuple.data + 8, tuple.size - 8, "1.0", strlen("1.0"))) {
		printf("CAM: not 1.0 version\n");
		return -EIO;
	}

	/* process the CFTABLE_ENTRY tuples, and any after those */
	while (!end_chain && ci->tuple_cur_offset < 0x1000) {
		if (joker_ci_get_next_tuple(joker, &tuple, 0, 0))
			return -EIO;

		switch (tuple.type) {
			case 0x1B:      // CISTPL_CFTABLE_ENTRY
				if (tuple.size < (2 + 11 + 17))
					break;

				/* if we've already parsed one, just use it */
				if (got_cftableentry)
					break;

				/* get the config option */
				ci->config_option = tuple.data[0] & 0x3f;

				/* OK, check it contains the correct strings */
				if (!xmemmem(tuple.data, tuple.size, "DVB_HOST", strlen("DVB_HOST")) ||
						!xmemmem(tuple.data, tuple.size, "DVB_CI_MODULE", strlen("DVB_CI_MODULE"))) {
					printf ("CAM: can't validate CISTPL_CFTABLE_ENTRY. \n");
					hexdump(tuple.data, tuple.size);
					break;
				}

				got_cftableentry = 1;
				break;
			case 0x14:      // CISTPL_NO_LINK
				break;

			case 0xFF:      // CISTPL_END
				end_chain = 1;
				break;

			default:
				printf("CAM: unknown tuple 0x%x size %d \n",
						tuple.type, tuple.size);
				break;
		}
	}

	if (!got_cftableentry) {
		printf("CAM: can't find valid CISTPL_CFTABLE_ENTRY\n");
		return -EIO;
	}

	ci->cam_validated = 1;
	printf ("CAM: Validated. Infostring:%s MANID: 0x%x DEVID:%x CONFIGBASE:0x%x CONFIGOPTION:0x%x \n",
		ci->cam_infostring, ci->manfid, ci->devid, ci->config_base, ci->config_option);

	return 0;
}

/* Wait link status bits
 * timeout in ms
 * return 0 if success
 * other values are errors
 */
int joker_ci_wait_status(struct joker_t * joker, uint8_t waitfor, int timeout)
{
	struct joker_ci_t * ci = NULL;
	int ret;
	unsigned char buf[JCMD_BUF_LEN];
	int sleep_delay = 10; // msec
	int timeout_cnt = timeout/sleep_delay;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	while(timeout_cnt--) {
		/* status bits:
		 * DA FR R R R R WE RE
		 */
		ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_IO, CTRLIF_STATUS, buf, 1);
		if (ret < 0) {
			printf("CAM:%s can't read status reg \n", __func__);
			return -EIO;
		}

		if (buf[0] & waitfor)
			return 0;

		msleep(sleep_delay);
	}
	return -ETIMEDOUT;
}

/* do init routines 
 * return 0 if success
 * other values are errors
 */
int joker_ci_init_interface(struct joker_t * joker)
{
	struct joker_ci_t * ci = NULL;
	int ret;
	unsigned char buf[JCMD_BUF_LEN];

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* set configoption */
	ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, ci->config_base, buf, 1);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}

	jdebug("CAM: current config option 0x%x \n", buf[0]);

	buf[0] = ci->config_option;
	ret = joker_ci_rw(joker, JOKER_CI_CTRL_WRITE | JOKER_CI_CTRL_MEM, ci->config_base, buf, 1);
	if (ret < 0) {
		printf("CAM: can't write to offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}

	ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, ci->config_base, buf, 1);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	} else {
		if (ci->ci_verbose)
			printf("CAM: new config option 0x%x \n", ret);
		if (buf[0] != ci->config_option) {
			printf("CAM:ERROR: config option read=0x%x expected 0x%x\n",
					buf[0], ci->config_option);
			return -EIO;
		}
	}

	/* reset link */
	buf[0] = CMDREG_RS;
	ret = joker_ci_rw(joker, JOKER_CI_CTRL_WRITE | JOKER_CI_CTRL_IO, CTRLIF_COMMAND, buf, 1);
	if (ret < 0) {
		printf("CAM:%s can't set RS bit to CI \n", __func__);
		return -EIO;
	}

	/* wait FR (free) bit in status reg
	 * CAM should respond in 10 sec */
	ret = joker_ci_wait_status(joker, STATUSREG_FR, 10000);
	if (ret != 0) {
		printf("CAM:ERROR: link FREE bit wait timeout \n");
		return -EIO;
	}

	return 0;
}

/* do init link
 * return 0 if success
 * other values are errors
 */
int joker_ci_link_init(struct joker_t * joker)
{
	struct joker_ci_t * ci = NULL;
	int ret;
	unsigned char buf[128];

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* read the buffer size from the CAM */
	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN | CMDREG_SR);
	ret = joker_ci_wait_status(joker, STATUSREG_DA, 1000);
	if (ret != 0) {
		printf("CAM:ERROR: link buffer size init. ret=%d\n", ret);
		return -EIO;
	}
	ret = joker_ci_read_data(joker, buf, 2);
	if (ret != 2)
		return -EIO;
	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN);

	if (ci->ci_verbose)
		printf("CAM:%s suggested buffer size %d bytes \n", __func__, ((int)buf[0] << 8 | buf[1]));

	/* write the buffer size to the CAM */
	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN | CMDREG_SW);
	ret = joker_ci_wait_status(joker, STATUSREG_FR, 100);
	if (ret != 0) {
		printf("CAM:ERROR: link buffer size write failed\n");
		return -EIO;
	}
	/* we are 'huge host' and can handle any buffer size
	 * so, just write suggested buffer size back to CAM
	 */
	ret = joker_ci_write_data(joker, buf, 2);
	if (ret != 2) {
		printf("CAM:ERROR: link buffer size write failed (2 bytes)\n");
		return -EIO;
	}

	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN);

	return 0;
}

void* joker_ci_worker(void * data)
{
	struct joker_t * joker = (struct joker_t *)data;
	int ret = -EINVAL, i = 0, j = 0;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	unsigned char mem[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;
	int ci_timeout = 0;
	int try = 10;

	if (joker->joker_ci_opaque) {
		printf("CAM already initialized ? Call joker_ci_close to deinit\n");
		return (void *)-EINVAL;
	}

	ci = malloc(sizeof(struct joker_ci_t));
	if (!ci)
		return (void *)-ENOMEM;
	memset(ci, 0, sizeof(struct joker_ci_t));
	ci->ci_verbose = joker->ci_verbose;
	joker->joker_ci_opaque = ci;

	while(try--) {
		/* power cycle for CAM */
		joker_reset(joker, OC_I2C_RESET_TPS_CI);
		msleep(10);
		joker_unreset(joker, OC_I2C_RESET_TPS_CI);

		// wait until cam status changed 
		ci_timeout = 70; /* 7 sec timeout for CAM reset */
		while(ci_timeout--) {
			buf[0] = J_CMD_CI_STATUS;
			if ((ret = joker_cmd(joker, buf, 1, in_buf, 2 /* in_len */)))
				goto fail_out;
			jdebug("CAM status 0x%x %x\n", in_buf[0], in_buf[1]);

			if (in_buf[1] & 0x01)
				break; /* CAM detected */

			msleep(100);
		}
		if (in_buf[1] & 0x01)
			break; // detected
	}

	if (!(in_buf[1] & 0x01)) {
		printf("CAM not detected. status=0x%x\n", in_buf[1]);
		return (void *)-ENODEV;
	} else {
		printf("CAM detected\n");
	}

	ci->cam_detected = 1;

	if (ci->ci_verbose) {
		printf("CAM attribute memory dump:\n");
		ret = joker_ci_rw(joker, JOKER_CI_CTRL_READ | JOKER_CI_CTRL_MEM, 0, mem, 500);
		hexdump(mem, 500);
	}

	// this will validate CAM
	if (joker_ci_parse_attributes(joker)) {
		printf("CAM: Can't parse CAM attributes \n");
		goto fail_out;
	}
	
	// init interface
	if (joker_ci_init_interface(joker)) {
		printf("CAM: Can't init CAM interface\n");
		goto fail_out;
	} else {
		printf("CAM: CAM interface initialized\n");
	}

	// init en50221 link
	if (joker_ci_link_init(joker)) {
		printf("CAM: Can't init EN50221 link\n");
		goto fail_out;
	} else {
		printf("CAM: EN50221 link initialized \n");
	}

	// init rest EN50221 stuff
	joker_ci_en50221(joker);

	return 0;

fail_out:
	if (ci)
		free(ci);
	joker->joker_ci_opaque = NULL;

	return (void *)-EIO;
}

/* initialize CAM module
 * this call return immediately
 *
 * return 0 if success
 * other return values indicates error
 */
int joker_ci(struct joker_t * joker)
{
	int rc = 0;

	if (!joker)
		return -EINVAL;

	if (!joker->ci_threading) {
		joker->ci_threading = malloc(sizeof(struct ci_thread_opaq_t));
		memset(joker->ci_threading, 0, sizeof(struct ci_thread_opaq_t));
		pthread_mutex_init(&joker->ci_threading->mux, NULL);
		pthread_cond_init(&joker->ci_threading->cond, NULL);

		pthread_attr_t attrs;
		pthread_attr_init(&attrs);
		pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
		rc = pthread_create(&joker->ci_threading->ci_thread, &attrs, joker_ci_worker, (void *)joker);
		if (rc){
			printf("ERROR: can't start CI thread. code=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

int joker_ci_close(struct joker_t * joker)
{
	if (joker->joker_ci_opaque) {
		free(joker->joker_ci_opaque);
		joker->joker_ci_opaque = NULL;
	}

	return 0;
}
