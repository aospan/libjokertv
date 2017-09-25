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

/* read CI attributes memory at offset
 * return negative value if error
 * or read byte if success */
int joker_ci_read(struct joker_t * joker, int offset)
{
	int ret = 0;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	struct joker_ci_t * ci = NULL;

	if (!joker)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	buf[0] = J_CMD_CI_READ_MEM;
	buf[1] = offset;
	if ((ret = joker_cmd(joker, buf, 2, in_buf, 2 /* in_len */)))
		return -EIO;

	jdebug("CAM: read 0x%x from offset %d \n", in_buf[1], offset);

	return in_buf[1];
}

int joker_ci_read_tuple(struct joker_t * joker, struct ci_tuple_t *tuple)
{
	int ret = 0, i = 0;
	struct joker_ci_t * ci = NULL;

	if (!joker)
		return -EINVAL;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* clean tuple before reading */
	memset(tuple, 0, sizeof(struct ci_tuple_t));

	ret = joker_ci_read(joker, ci->tuple_cur_offset);
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

	ret = joker_ci_read(joker, ci->tuple_cur_offset);
	if (ret < 0) {
		printf("CAM: can't read from offset 0x%x \n", ci->tuple_cur_offset);
		return -EIO;
	}
	ci->tuple_cur_offset += 2;
	tuple->size = ret;

	// read tuple data
	for (i = 0; i < tuple->size; i++) {
		ret = joker_ci_read(joker, ci->tuple_cur_offset);
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

	if (!joker)
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

	if (!joker)
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

	buf[0] = J_CMD_CI_STATUS;
	if ((ret = joker_cmd(joker, buf, 1, in_buf, 2 /* in_len */)))
		goto fail_out;
	jdebug("CAM status 0x%x %x\n", in_buf[0], in_buf[1]);

	if (!(in_buf[1] & 0x01)) {
		printf("CAM not detected\n");
		return -ENOENT;
	}

	printf("CAM detected\n");
	ci->cam_detected = 1;
	if (ci->ci_verbose) {
		printf("CAM attribute memory dump:\n");
		printf("%.8x  ", i);
		for(i = 0, j = 0; i < 256; i += 2, j++){
			/* CI */
			ret = joker_ci_read(joker, i);
			if (ret >= 0)
				mem[j] = (unsigned char)ret;
		}
		hexdump(mem, j);
	}

	if(joker_ci_parse_attributes(joker)) {
		printf("Can't parse CAM attributes \n");
		goto fail_out;
	}

	fflush(stdout);

	return 0;

fail_out:
	if (ci)
		free(ci);

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
