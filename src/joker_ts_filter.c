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

	printf("%s: 0x%x %s \n", __func__, pid, block ? " blocked" : " unblocked");
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

	printf("%s: %s \n", __func__, block ? "all PIDs blocked" : "all PIDs unblocked");

	return 0;
}
