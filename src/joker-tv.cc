/* 
 * Joker TV app
 * Supported standards:
 *
 * DVB-S/S2 – satellite, is found everywhere in the world
 * DVB-T/T2 – mostly Europe
 * DVB-C/C2 – cable, is found everywhere in the world
 * ISDB-T – Brazil, Latin America, Japan, etc
 * ATSC – USA, Canada, Mexico, South Korea, etc
 * DTMB – China, Cuba, Hong-Kong, Pakistan, etc
 *
 * (c) Abylay Ospan <aospan@jokersys.com>, 2017
 * LICENSE: GPLv2
 * https://tv.jokersys.com
 * GPLv2
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <libusb.h>
#include <pthread.h>

#include <queue>
#include "joker_tv.h"
#include "joker_fpga.h"
#include "joker_ci.h"
#include "u_drv_tune.h"
#include "u_drv_data.h"

void * print_stat(void *data)
{
	int status = 0;
	int ucblocks = 0;
	int signal = 0;
	struct tune_info_t * info = (struct tune_info_t *)data;

	while(1) {
		status = read_status(info);
		ucblocks = read_ucblocks(info);
		signal = read_signal(info);
		printf("INFO: status=%d (%s) signal=%d (%d %%) uncorrected blocks=%d\n", 
				status, status ? "NOLOCK" : "LOCK", signal, 100*(int)signal/0xFFFF, ucblocks );
		sleep(1);
	}
}

void show_help() {
	printf("joker-tv usage: \n");
	printf("	-d delsys	Delivery system. Options: \n \
			%d-ATSC  %d-DVB-S  %d-DVB-C %d-DVB-T %d-DVB-T2 %d-ISDB-T %d-DTMB\n", 
			JOKER_SYS_ATSC, JOKER_SYS_DVBS, JOKER_SYS_DVBC_ANNEX_A,
			JOKER_SYS_DVBT, JOKER_SYS_DVBT2, JOKER_SYS_ISDBT, JOKER_SYS_DTMB);
	printf("	-m modulation	Modulation. Options: \n \
			%d-VSB8 (for ATSC) 0-AUTO\n", JOKER_VSB_8);
	printf("	-f freq		Frequency in Hz. Example: 1402000000\n");
	printf("	-s symbol_rate	Symbol rate. Options: 0-AUTO. Example: 20000000\n");
	printf("	-b bandwidth	Bandwidth in Hz. Example: 8000000\n");
	printf("	-o filename	Output TS filename. Default: out.ts\n");
	printf("	-t		enable TS generator. Default: disabled\n");

	exit(0);
}

int main (int argc, char **argv)
{
	struct tune_info_t info;
	struct big_pool_t pool;
	int status = 0, ret = 0, rbytes = 0, i = 0;
	struct joker_t * joker;
	unsigned char buf[512];
	unsigned char in_buf[JCMD_BUF_LEN];
	int isoc_len = USB_PACKET_SIZE;
	pthread_t stat_thread;
	int c, tsgen = 0;
	int delsys = 0, mod = 0, freq = 0, sr = 0, bw = 0;
	FILE * out = NULL;
	unsigned char filename[FNAME_LEN] = "out.ts";

	while ((c = getopt (argc, argv, "d:m:f:s:o:b:t")) != -1)
		switch (c)
		{
			case 'd':
				delsys = atoi(optarg);
				break;
			case 'm':
				mod = atoi(optarg);
				break;
			case 'f':
				freq = atoi(optarg);
				break;
			case 's':
				sr = atoi(optarg);
				break;
			case 'b':
				bw = atoi(optarg);
				break;
			case 't':
				tsgen = 1;
				break;
			case 'o':
				strncpy((char*)filename, optarg, FNAME_LEN);
				break;
			default:
				show_help();
		}

	if (delsys == JOKER_SYS_UNDEFINED && tsgen !=1 )
		show_help();

	out = fopen((char*)filename, "w+");
	if (!out){
		printf("Can't open out file '%s' \n", filename);
		perror("");
		exit(-1);
	} else {
		printf("TS outfile:%s \n", filename);
	}

	joker = (struct joker_t *) malloc(sizeof(struct joker_t));
	if (!joker)
		return ENOMEM;

	/* open Joker TV on USB bus */
	if ((ret = joker_open(joker)))
		return ret;
	printf("allocated joker=%p \n", joker);

	/* init CI */
	joker_ci(joker);

	/* tune usb isoc transaction len */
	buf[0] = J_CMD_ISOC_LEN_WRITE_HI;
	buf[1] = (isoc_len >> 8) & 0x7;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	buf[0] = J_CMD_ISOC_LEN_WRITE_LO;
	buf[1] = isoc_len & 0xFF;
	if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
		return ret;

	if ((ret = joker_i2c_init(joker)))
		return ret;

	if(tsgen) {
		/* TS generator selected */
		buf[0] = J_CMD_TS_INSEL_WRITE;
		buf[1] = J_INSEL_TSGEN;
		if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */)))
			return ret;
	} else {
		/* real demod selected
		 * tuning ...
		 */
		info.delivery_system = (joker_fe_delivery_system)delsys;
		info.bandwidth_hz = bw;
		info.frequency = freq;
		info.symbol_rate = sr;
		info.modulation = (joker_fe_modulation)mod;

		printf("TUNE start \n");
		if (tune(joker, &info))
			return -1;
		printf("TUNE done \n");

		while (1) {
			status = read_status(&info);
			printf("WAITING LOCK. status=%d error=%s \n", status, strerror(status) );
			fflush(stdout);
			if (!status)
				break;
			sleep(1);
		}

		/* start status printing thread */
		if(pthread_create(&stat_thread, NULL, print_stat, &info)) {
			fprintf(stderr, "Error creating status thread\n");
			return -1;
		}
	}

	/* start TS collection and save to file */
	start_ts(joker, &pool);
	while(1) {
		rbytes = read_data(joker, &pool, &buf[0], 512);
		fwrite(buf, 512, 1, out);
	}

	free(joker);
}
