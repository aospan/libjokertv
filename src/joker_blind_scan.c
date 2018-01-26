/* Blind scan
 *
 * (c) Abylay Ospan <aospan@jokersys.com>, 2017
 * LICENSE: GPLv2
 */

/*
 * this file contains drivers for Joker TV card
 * Supported standards:
 *
 * DVB-S/S2 – satellite, is found everywhere in the world
 * DVB-T/T2 – mostly Europe
 * DVB-C/C2 – cable, is found everywhere in the world
 * ISDB-T – Brazil, Latin America, Japan, etc
 * ATSC – USA, Canada, Mexico, South Korea, etc
 * DTMB – China, Cuba, Hong-Kong, Pakistan, etc
 *
 * https://tv.jokersys.com
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
typedef unsigned char           uint8_t;
typedef unsigned short int      uint16_t;
typedef unsigned int            uint32_t;

// Linux kernel header files
#include <drivers/media/dvb-frontends/helene.h>
#include <drivers/media/dvb-frontends/cxd2841er.h>
#include <drivers/media/dvb-frontends/cxd2841er_blind_scan.h>
#include <drivers/media/dvb-frontends/lgdt3306a.h>
#include <drivers/media/dvb-frontends/atbm888x.h>
#include <drivers/media/dvb-frontends/tps65233.h>
#include <drivers/media/dvb-core/dvb_frontend.h>
#include <linux/dvb/frontend.h>
#include <time.h>
#include <linux/i2c.h>
#include <sys/time.h>
#include "pthread.h"
#include "joker_i2c.h"
#include "joker_fpga.h"
#include "u_drv_tune.h"
#include "joker_blind_scan.h"

void blind_scan_callback (void *data)
{
	sony_demod_dvbs_s2_blindscan_data_t *pCurrent = NULL;
	sony_integ_dvbs_s2_blindscan_result_t * res =
		(sony_integ_dvbs_s2_blindscan_result_t*) data;
	int cnt = 0;
	FILE *pfd = NULL;
	char * filename = NULL;

	struct joker_t *joker = (struct joker_t *)res->callback_arg;
	if (!joker || !joker->info)
		return;
	struct tune_info_t *info = joker->info;

	jdebug("%s: eventId=%d \n", __func__, res->eventId);
	if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT) {
		printf("\nDetected! progress=%u%% %s %dMHz (lnb freq=%d) %s symbol rate=%d ksym/s\n",
				res->progress, (res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? "DVB-S" : "DVB-S2",
				res->tuneParam.centerFreqKHz/1000 + info->lnb.selected_freq,
				info->lnb.selected_freq,
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v V(R)" : "18v H(L)",
				res->tuneParam.symbolRateKSps);

		// save results to file 
		if (joker->blind_out_filename_fd) {
			fprintf(joker->blind_out_filename_fd,
					"\"%d\",\"%s\",\"%s\",\"%d\"\n",
					res->tuneParam.centerFreqKHz/1000 + info->lnb.selected_freq,
					(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v V(R)" : "18v H(L)",
					(res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? "DVB-S" : "DVB-S2",
					res->tuneParam.symbolRateKSps);
			fflush(joker->blind_out_filename_fd);
		}
	} else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_POWER) {
		if (!joker->blind_power_file_prefix)
			return;

		// save power
		filename = calloc(1, 1024);
		if(!filename)
			return;
		snprintf(filename, 1024, "%s-%s-lnb_%d.csv", joker->blind_power_file_prefix,
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v" : "18v",
				info->lnb.selected_freq);

		pCurrent = res->pPowerList->pNext;
		pfd = fopen(filename, "w+b");
		if (pfd <= 0) {
			printf("Can't open power list file %s error %d (%s)\n",
					filename, errno, strerror(errno));
			return;
		}
		while(pCurrent){
			fprintf(pfd, "%d %.2f\n", pCurrent->data.power.freqKHz/1000 + info->lnb.selected_freq,
					(double)pCurrent->data.power.power/100);
			pCurrent = pCurrent->pNext;
			cnt++;
		}
		fclose(pfd);
		printf("power list (size %d) dumped to %s\n", cnt, filename);
		free(filename);
	} else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS) {
		printf("progress=%u%%\r", res->progress);
	}
}

int blind_scan_do_quadrant(struct joker_t *joker, struct tune_info_t *info,
		struct dvb_frontend *fe,
		enum joker_fe_sec_tone_mode tone,
		enum joker_fe_sec_voltage voltage)
{
	int freq_min = 950000;
	int freq_max = 2150000;

	/* avoid duplicate range scan
	 * example: duplicate range for universal LNB:
	 * 11550 - 11900 MHz
	 *
	 * calc:
	 * 10600 + 950 = 11550 MHz
	 * 9750 + 2150 = 11900 Mhz
	 * 
	 */
	if (tone == JOKER_SEC_TONE_ON) {
		// add 31Mhz because blind scan will add this for powerscan
		// change this constant if symbolrate changed
		// freq_min = 1000 * (info->lnb.lowfreq + 2150 - info->lnb.highfreq + 31 * 2);
		freq_min = 1000 * (info->lnb.switchfreq - info->lnb.highfreq);
	} else {
		freq_max = 1000 * (info->lnb.switchfreq - info->lnb.lowfreq);
	}

	printf("\n\t *** Blind scan quadrant %dv, %s LNB band (22khz %s) min/max freq=%d/%d\n",
			(voltage == JOKER_SEC_VOLTAGE_13) ? 13:18,
			(tone == JOKER_SEC_TONE_ON) ? "high" : "low",
			(tone == JOKER_SEC_TONE_ON) ? "on" : "off",
			freq_min, freq_max);

	fe->ops.set_tone(fe, tone);
	fe->ops.set_voltage(fe, voltage);
	info->lnb.selected_freq = (tone == JOKER_SEC_TONE_ON) ? info->lnb.highfreq : info->lnb.lowfreq;
	info->voltage = voltage;
	joker->info = info;
	fe->ops.blind_scan(fe, freq_min, freq_max, 1000, 45000,
			joker->blind_power_file_prefix ? true : false,
			blind_scan_callback, joker);
}

/*** Blind scan ***/
int blind_scan(struct joker_t *joker, struct tune_info_t *info,
		struct dvb_frontend *fe)
{
	sony_integ_dvbs_s2_blindscan_param_t blindscanParam;

	/* open file for blind scan results */
	if (joker->blind_out_filename) {
		joker->blind_out_filename_fd = fopen(joker->blind_out_filename, "w+b");
		if (joker->blind_out_filename_fd < 0) {
			printf("Can't open blind scan resulting file %s. Error=%d (%s)\n",
					joker->blind_out_filename, errno, strerror(errno));
			joker->blind_out_filename_fd = NULL;
		}

		// write file header
		fprintf(joker->blind_out_filename_fd,
				"freq_mhz,pol,standard,symbol_rate_ksps\n");
		fflush(joker->blind_out_filename_fd);
	}

	/* Split frequncy range to four quadrants:
	 *
	 * 18v 22khz tone=off | 18v 22khz tone=on
	 * --------------------------------------
	 * 13v 22khz tone=off | 13v 22khz tone=on
	 *
	 *
	 * Universal LNB (9750, 10600, 11700) as example:
	 *
	 * 10700H to 11900H (force to 11700) | 11550H (force to 11700) to 12750H
	 * -----------------------------------
	 * 10700V to 11900V  (force to 11700)| 11550V (force to 11700) to 12750V
	 */
	printf("\n\tBlind scan will scan four quadrants (13v/18v 22khz on/off)\n");

	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_OFF, JOKER_SEC_VOLTAGE_13);
	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_ON, JOKER_SEC_VOLTAGE_13);
	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_OFF, JOKER_SEC_VOLTAGE_18);
	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_ON, JOKER_SEC_VOLTAGE_18);

	if (joker->blind_out_filename_fd) {
		fclose(joker->blind_out_filename_fd);
		joker->blind_out_filename_fd = NULL;
	}

	return 0;
}
