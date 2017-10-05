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
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>
#include <joker_utils.h>

int joker_ci_wait_status(struct joker_t * joker, uint8_t waitfor, int timeout);

/* read from CAM at offset
 * if read_from=JOKER_CI_IO then read performed from IO space
 * if read_from=JOKER_CI_MEM then read performed from attributes memory
 * return negative value if error
 * or read byte if success */
int joker_ci_read(struct joker_t * joker, int offset, int read_from)
{
	int ret = 0;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	buf[0] = J_CMD_CI_READ_MEM;
	buf[1] = (read_from == JOKER_CI_IO) ? 0x00 : 0x80; /* MSB */
	buf[1] |= (offset>>8)&0x3f; /* MSB */
	buf[2] = offset&0xff; /* LSB */
	buf[3] = 0x0;
	if ((ret = joker_cmd(joker, buf, 4, in_buf, 2 /* in_len */)))
		return -EIO;

	jdebug("CAM: read 0x%x from offset %d \n", in_buf[1], offset);

	return in_buf[1];
}

int joker_ci_write(struct joker_t * joker, int offset, int area, char data)
{
	int ret = 0;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	buf[0] = J_CMD_CI_READ_MEM;
	buf[1] = (area == JOKER_CI_IO) ? 0x00 : 0x80; /* MSB */
	buf[1] |= 0x40; /* write */
	buf[1] |= (offset>>8)&0x3f; /* MSB */
	buf[2] = offset&0xff; /* LSB */
	buf[3] = data;
	if ((ret = joker_cmd(joker, buf, 4, in_buf, 2 /* in_len */)))
		return -EIO;

	jdebug("CAM: write 0x%x to offset 0x%x at %s\n", buf[3], offset, (area == JOKER_CI_IO) ? "IO" : "MEM");

	return 0;
}

/* EN50221 read data
 * return read bytes if success
 * or negative value if failed
 */
int joker_ci_read_data(struct joker_t * joker, unsigned char *buf, int size)
{
	int bytes_read = 0;
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	ret = joker_ci_wait_status(joker, STATUSREG_DA, 1000);
	if (ret != 0) {
		jdebug("CAM:%s: no data \n", __func__);
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
		printf("CAM:%s: can't fit data to buffer\n", __func__);
		return -ENOMEM;
	}

	if (ci->ci_verbose)
		jdebug("CAM:%s: try to read %d bytes from CAM\n", __func__, bytes_read);

	for (i = 0; i < bytes_read; i++) {
		ret = joker_ci_read(joker, CTRLIF_DATA, JOKER_CI_IO);
		if (ret < 0) {
			printf("CAM:%s: can't read CTRLIF_DATA\n", __func__);
			return -EIO;
		}

		buf[i] = ret;
	}

#if 0
	if (ci->ci_verbose) {
		printf("CAM:%s: %d bytes read from CAM. hexdump:\n", __func__, bytes_read);
		hexdump(buf, bytes_read);
	}
#endif

	ret = joker_ci_read(joker, CTRLIF_STATUS, JOKER_CI_IO);
	if (ret < 0)
		return -EIO;

	if (ret & STATUSREG_RE) {
		printf("CAM:%s: read error indicated by cam. status=0x%x\n", __func__, ret);
		return -EIO;
	}

	return bytes_read;
}

/* EN50221 write data
 * return written bytes if success
 * or negative value if failed
 */
int joker_ci_write_data(struct joker_t * joker, unsigned char *buf, int size)
{
	int bytes_read = 0;
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* TODO: read data from CAM first if DA bit set ! */

	ret = joker_ci_wait_status(joker, STATUSREG_FR, 1000);
	if (ret != 0) {
		printf("CAM:%s: FR not set\n", __func__);
		return -EIO;
	}

	/* set HC bit */
	ret = joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN | CMDREG_HC);
	if (ret < 0) {
		printf("CAM:%s: can't set CMDREG_HC\n", __func__);
		ret = -EIO;
		goto exit;
	}

	/* is CAM still free ? */
	ret = joker_ci_wait_status(joker, STATUSREG_FR, 1000);
	if (ret != 0) {
		printf("CAM:%s: FR not set\n", __func__);
		ret = -EIO;
		goto exit;
	}

	/* TODO: here the same ! read data from CAM first if DA bit set ! */

	ret = joker_ci_write(joker, CTRLIF_SIZE_HIGH, JOKER_CI_IO, size >> 8);
	if (ret < 0) {
		ret = -EIO;
		goto exit;
	}

	ret = joker_ci_write(joker, CTRLIF_SIZE_LOW, JOKER_CI_IO, size & 0xff);
	if (ret < 0) {
		ret = -EIO;
		goto exit;
	}

	if (ci->ci_verbose)
		jdebug("CAM:%s: try to write %d bytes to CAM\n", __func__, size);

	for (i = 0; i < size; i++) {
		ret = joker_ci_write(joker, CTRLIF_DATA, JOKER_CI_IO, buf[i]);
		if (ret < 0) {
			printf("CAM:%s: can't write to CAM\n", __func__);
			ret = -EIO;
			goto exit;
		}
	}

#if 0
	if (ci->ci_verbose) {
		printf("CAM:%s: %d bytes written to CAM. hexdump:\n", __func__, size);
		hexdump(buf, size);
	}
#endif

	ret = joker_ci_read(joker, CTRLIF_STATUS, JOKER_CI_IO);
	if (ret < 0) {
		ret = -EIO;
		goto exit;
	}

	if (ret & STATUSREG_WE) {
		printf("CAM:%s: write error indicated by cam. status=0x%x\n", __func__, ret);
		ret = -EIO;
		goto exit;
	}

exit:
	/* clear HC bit */
	ret = joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN);
	if (ret < 0) {
		printf("CAM:%s: can't clear CMDREG_HC\n", __func__);
		return -EIO;
	}

	return ret ? ret : size;
}

int joker_ci_read_tuple(struct joker_t * joker, struct ci_tuple_t *tuple)
{
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* clean tuple before reading */
	memset(tuple, 0, sizeof(struct ci_tuple_t));

	ret = joker_ci_read(joker, ci->tuple_cur_offset, JOKER_CI_MEM);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}
	ci->tuple_cur_offset += 2;

	tuple->type = ret;
	// end of chain tuple
	if (tuple->type == 0xFF) {
		return 0;
	}

	ret = joker_ci_read(joker, ci->tuple_cur_offset, JOKER_CI_MEM);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}
	ci->tuple_cur_offset += 2;
	tuple->size = ret;

	if (tuple->size > TUPLE_MAX_SIZE) {
		printf("Too big tuple found (%d bytes) \n", tuple->size);
		return -EIO;
	}

	// read tuple data
	for (i = 0; i < tuple->size; i++) {
		ret = joker_ci_read(joker, ci->tuple_cur_offset, JOKER_CI_MEM);
		if (ret < 0) {
			printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
			return -EIO;
		}
		ci->tuple_cur_offset += 2;
		tuple->data[i] = ret;
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
	if (!memmem(tuple.data, tuple.size, "DVB_CI_V", strlen("DVB_CI_V"))) {
		printf("CAM: can't find DVB_CI_V string \n");
		return -EIO;
	}

	if (!memmem(tuple.data + 8, tuple.size - 8, "1.0", strlen("1.0"))) {
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
				if (!memmem(tuple.data, tuple.size, "DVB_HOST", strlen("DVB_HOST")) ||
						!memmem(tuple.data, tuple.size, "DVB_CI_MODULE", strlen("DVB_CI_MODULE"))) {
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

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	while(timeout--) {
		/* status bits:
		 * DA FR R R R R WE RE
		 */
		ret = joker_ci_read(joker, CTRLIF_STATUS, JOKER_CI_IO);
		if (ret < 0) {
			printf("CAM:ERROR: can't read status reg\n");
			return -EIO;
		}

		if (ret & waitfor)
			return 0;
		msleep(1);
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

	if (!joker || !joker->joker_ci_opaque)
		return -EINVAL;

	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* set configoption */
	if (ci->ci_verbose) {
		ret = joker_ci_read(joker, ci->config_base, JOKER_CI_MEM);
		if (ret < 0)
			return -EIO;
		printf("CAM: current config option 0x%x \n", ret);
	}

	ret = joker_ci_write(joker, ci->config_base, JOKER_CI_MEM, ci->config_option);
	if (ret < 0) {
		printf("CAM:ERROR: can't write config option\n");
		return -EIO;
	}

	ret = joker_ci_read(joker, ci->config_base, JOKER_CI_MEM);
	if (ret < 0) {
		printf("CAM:ERROR: can't read config option\n");
		return -EIO;
	} else {
		if (ci->ci_verbose)
			printf("CAM: new config option 0x%x \n", ret);
		if (ret != ci->config_option) {
			printf("CAM:ERROR: config option read=0x%x expected 0x%x\n",
					ret, ci->config_option);
			return -EIO;
		}
	}

	/* reset link */
	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, CMDREG_RS);
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

	/* write the buffer size to the CAM */
	joker_ci_write(joker, CTRLIF_COMMAND, JOKER_CI_IO, IRQEN | CMDREG_SW);
	ret = joker_ci_wait_status(joker, STATUSREG_FR, 100);
	if (ret != 0) {
		printf("CAM:ERROR: link buffer size write failed\n");
		return -EIO;
	}
	jdebug("CAM:%s suggested buffer size %d bytes \n", __func__, ((int)buf[0] << 8 | buf[1]));
	/* we are 'huge host' and can handle any buffer size
	 * so, just write suggested buffer size back to CAM
	 */
	ret = joker_ci_write_data(joker, buf, 2);
	if (ret != 2)
		return -EIO;

	return 0;
}

/* initialize CAM module
 * return 0 if success
 * other return values indicates error
 */
int joker_ci(struct joker_t * joker)
{
	int ret = -EINVAL, i = 0, j = 0;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	unsigned char mem[512];
	struct joker_ci_t * ci = NULL;
	int ci_timeout = 70; /* 7 sec timeout for CAM reset */

	if (joker->joker_ci_opaque) {
		printf("CAM already initialized ? Call joker_ci_close to deinit\n");
		return -EINVAL;
	}

	ci = malloc(sizeof(struct joker_ci_t));
	if (!ci)
		return -ENOMEM;
	memset(ci, 0, sizeof(struct joker_ci_t));
	ci->ci_verbose = joker->ci_verbose;
	joker->joker_ci_opaque = ci;

	/* power cycle for CAM */
	joker_reset(joker, OC_I2C_RESET_TPS_CI);
	msleep(10);
	joker_unreset(joker, OC_I2C_RESET_TPS_CI);

	// wait until cam status changed 
	while(ci_timeout--) {
		buf[0] = J_CMD_CI_STATUS;
		if ((ret = joker_cmd(joker, buf, 1, in_buf, 2 /* in_len */)))
			goto fail_out;
		jdebug("CAM status 0x%x %x\n", in_buf[0], in_buf[1]);

		if (in_buf[1] & 0x01)
			break; /* CAM detected */
		
		msleep(100);
	}

	if (!(in_buf[1] & 0x01)) {
		printf("CAM not detected. status=0x%x\n", in_buf[1]);
		return -ENOENT;
	}

	printf("CAM detected\n");
	ci->cam_detected = 1;

	if (ci->ci_verbose) {
		printf("CAM attribute memory dump:\n");
		for(i = 0, j = 0; i < 256; i += 2, j++){
			/* CI */
			ret = joker_ci_read(joker, i, JOKER_CI_MEM);
			if (ret >= 0)
				mem[j] = (unsigned char)ret;
		}
		hexdump(mem, j);
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

	fflush(stdout);

	return 0;

fail_out:
	if (ci)
		free(ci);
	joker->joker_ci_opaque = NULL;

	return ret;
}

int joker_ci_close(struct joker_t * joker)
{
	if (joker->joker_ci_opaque) {
		free(joker->joker_ci_opaque);
		joker->joker_ci_opaque = NULL;
	}

	return 0;
}
