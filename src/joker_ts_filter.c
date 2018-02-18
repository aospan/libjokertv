/* 
 * Joker TV 
 * Transport Stream PID filtering
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
#include <unistd.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_fpga.h>
#include <joker_ts_filter.h>
#include <joker_utils.h>
#include <u_drv_data.h>

// block/unblock PID's
int ts_filter_one(struct joker_t *joker, int block, int pid)
{
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	int ret = 0;

	buf[0] = J_CMD_TS_FILTER;
	if (block)
		buf[1] = 0x3; // block one pid
	else
		buf[1] = 0x2; // allow one pid
	buf[2] = (pid>>8)&0x1f;
	buf[3] = (pid)&0xff;
	if ((ret = joker_cmd(joker, buf, 4, NULL /* in_buf */, 0 /* in_len */))) {
		printf("%s: io failed\n", __func__);
		return ret;
	}

	jdebug("%s: 0x%x %s \n", __func__, pid, block ? " blocked" : " unblocked");
	return 0;
}

int ts_filter_all(struct joker_t *joker, int block)
{
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	int ret = 0;

	buf[0] = J_CMD_TS_FILTER;
	if (block)
		buf[1] = 0x1; // block all pids
	else
		buf[1] = 0x0; // allow all pid
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */))) {
		printf("%s: io failed\n", __func__);
		return ret;
	}

	jdebug("%s: %s \n", __func__, block ? "all PIDs blocked" : "all PIDs unblocked");

	return 0;
}

// Allow only service PID's (information from https://en.wikipedia.org/wiki/MPEG_transport_stream):
// 0x00 ... 0x1F - PAT, CAT, TSDT, IPMP, NIT, SDT, EIT, etc
// 0x1FFB - ATSC MGT
// All other PID's are blocked
int ts_filter_only_service_pids(struct joker_t *joker)
{
	int i = 0;

	// block all PID's first
	if (ts_filter_all(joker, TS_FILTER_BLOCK))
		return -EIO;

	// allow only service PID's
	for (i = 0; i <= 0x1F; i++) {
		if (ts_filter_one(joker, TS_FILTER_UNBLOCK, i))
			return -EIO;
	}

	if (ts_filter_one(joker, TS_FILTER_UNBLOCK, 0x1FFB))
		return -EIO;

	return 0;
}
