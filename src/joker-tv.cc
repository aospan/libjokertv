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
#include <unistd.h>
#include <libusb.h>
#include <pthread.h>

#include <queue>
#include "joker_tv.h"
#include "joker_fpga.h"
#include "joker_ci.h"
#include "joker_spi.h"
#include "u_drv_tune.h"
#include "u_drv_data.h"

void * print_stat(void *data)
{
	int status = 0;
	int ucblocks = 0;
	int signal = 0;
	struct tune_info_t * info = (struct tune_info_t *)data;

	if (info->refresh <= 0)
		info->refresh = 1000; /* 1 sec refresh by default */

	while(1) {
		status = read_status(info);
		ucblocks = read_ucblocks(info);
		signal = read_signal(info);
		printf("INFO: status=%d (%s) signal=%d (%d %%) uncorrected blocks=%d\n", 
				status, status ? "NOLOCK" : "LOCK", signal, 100*(int)(65535 - signal)/0xFFFF, ucblocks );
		usleep(1000 * info->refresh);
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
	printf("	-u level	Libusb verbose level (0 - less, 4 - more verbose). Default: 0\n");
	printf("	-w filename	Update firmware on flash. Default: none\n");

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
	char filename[FNAME_LEN] = "out.ts";
	char fwfilename[FNAME_LEN] = "";
	int signal = 0;

	joker = (struct joker_t *) malloc(sizeof(struct joker_t));
	if (!joker)
		return ENOMEM;
	memset(joker, 0, sizeof(struct joker_t));

	while ((c = getopt (argc, argv, "d:m:f:s:o:b:tu:w:")) != -1)
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
			case 'u':
				joker->libusb_verbose = atoi(optarg);
				break;
			case 'o':
				strncpy((char*)filename, optarg, FNAME_LEN);
				break;
			case 'w':
				strncpy((char*)fwfilename, optarg, FNAME_LEN);
				break;
			default:
				show_help();
		}

	out = fopen((char*)filename, "w+b");
	if (!out){
		printf("Can't open out file '%s' \n", filename);
		perror("");
		exit(-1);
	} else {
		printf("TS outfile:%s \n", filename);
	}

	/* open Joker TV on USB bus */
	if ((ret = joker_open(joker)))
		return ret;
	printf("allocated joker=%p \n", joker);

	/* init CI */
	joker_ci(joker);

	/* upgrade fw if selected */
	if(strlen((const char*)fwfilename)) {
		if(joker_flash_checkid(joker)) {
			printf("SPI flash id check failed. Cancelling fw update.\n");
			return -1;
		}
		printf("SPI flash id check success. Starting fw update.\n");

		if(joker_flash_write(joker, fwfilename)) {
			printf("Can't write fw to flash !\n");
			return -1;
		} else {
			printf("FW successfully upgraded. Reconnect device please.\n");
			return 0;
		}
	}

	if (delsys == JOKER_SYS_UNDEFINED && tsgen !=1 )
		show_help();

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
		info.refresh = 500;

		printf("TUNE start \n");
		if (tune(joker, &info))
			return -1;
		printf("TUNE done \n");

		while (1) {
			printf("WAITING LOCK.\n");
			print_stat(&info);
			fflush(stdout);
			if (!status)
				break;
			sleep(1);
		}
		info.refresh = 3000; /* less heavy refresh */

		/* start status printing thread */
		if(pthread_create(&stat_thread, NULL, print_stat, &info)) {
			fprintf(stderr, "Error creating status thread\n");
			return -1;
		}
	}

	/* start TS collection and save to file */
	if((ret = start_ts(joker, &pool))) {
		printf("start_ts failed. err=%d \n", ret);
		exit(-1);
	}
	fflush(stdout);

	while(1) {
		rbytes = read_data(joker, &pool, &buf[0], 512);
		fwrite(buf, 512, 1, out);
	}

	free(joker);
}
