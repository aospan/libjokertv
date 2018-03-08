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
#include "joker_ts.h"
#include "joker_blind_scan.h"
#include "u_drv_tune.h"
#include "u_drv_data.h"

// this callback will be called when new service name arrived
void service_name_cb(struct program_t *program)
{
	struct program_es_t*es = NULL;
	jdebug("callback:%s program number=%d name=%s type=0x%x. video:%s audio:%s\n",
			__func__, program->number, program->name, program->service_type,
			program->has_video ? "yes" : "",
			program->has_audio ? "yes" : "");

	if(!list_empty(&program->es_list)) {
		list_for_each_entry(es, &program->es_list, list) {
			jdebug("    ES pid=0x%x type=0x%x\n",
					es->pid, es->type);
		}   
	}
}

void blind_scan_callback (void *data)
{
	sony_demod_dvbs_s2_blindscan_data_t *pCurrent = NULL;
	sony_integ_dvbs_s2_blindscan_result_t * res =
		(sony_integ_dvbs_s2_blindscan_result_t*) data;
	int cnt = 0;
	FILE *pfd = NULL;
	char * filename = NULL;
	char buf[1024];
	struct program_t *program = NULL, *tmp = NULL;
	struct list_head *programs = NULL;
	struct big_pool_t pool;
	int ret = 0;
	int ready = 0, timeout = 0;
	blind_scan_res_t blind_scan_res;
	struct joker_t *joker = (struct joker_t *)res->callback_arg;
	struct tune_info_t *info = NULL;

	if (!joker || !joker->info)
		return;
	info = joker->info;

	// prepare result for main callback
	memset(&blind_scan_res, 0, sizeof(blind_scan_res));
	blind_scan_res.progress = res->progress;
	blind_scan_res.joker = joker;
	blind_scan_res.info = info;

	jdebug("%s: eventId=%d \n", __func__, res->eventId);
	if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT) {
		snprintf(buf, 1024,
				"\"%d\",\"%s\",\"%s\",\"%d\",\"%d\",\"%s\",\"%s\",\"%s\"\n",
				abs(res->tuneParam.centerFreqKHz/1000 + info->lnb.selected_freq),
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v V(R)" : "18v H(L)",
				(res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? "DVB-S" : "DVB-S2",
				10 * ((res->tuneParam.symbolRateKSps + 10)/10),
				res->tuneParam.symbolRateKSps,
				(res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? \
				"QPSK" : DVBS2_Modulation[res->tuneParam.plscode.modulation],
				(res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? \
				DVBS_CodeRate[res->tuneParam.coderate] : \
				DVBS2_CodeRate[res->tuneParam.plscode.codeRate],
				(res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? \
				"" : res->tuneParam.plscode.isPilotOn ? "PilotOn" : "PilotOff");

		// fill info about found transponder
		info->delivery_system = (res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? JOKER_SYS_DVBS : JOKER_SYS_DVBS2;

		// HZ
		info->frequency = 1000*1000*(int64_t)abs(res->tuneParam.centerFreqKHz/1000 + info->lnb.selected_freq);
		info->symbol_rate = 1000*res->tuneParam.symbolRateKSps;
		info->symbol_rate_rounded = 1000 * 10 * ((res->tuneParam.symbolRateKSps + 10)/10);
		info->bandwidth_hz = 0;

		// "glue" values with upper level (outside of libjokertv)
		if (res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) {
			info->delivery_system = JOKER_SYS_DVBS;
			info->modulation = JOKER_QPSK;
			if (res->tuneParam.coderate > sizeof(DVBS_CodeRate2joker))
				info->coderate = 0;
			else
				info->coderate = DVBS_CodeRate2joker[res->tuneParam.coderate];
		} else {
			info->delivery_system = JOKER_SYS_DVBS2;
			if (res->tuneParam.plscode.codeRate > sizeof(DVBS2_CodeRate2joker))
				info->coderate = 0;
			else
				info->coderate = DVBS2_CodeRate2joker[res->tuneParam.plscode.codeRate];
			switch (res->tuneParam.plscode.modulation) {
				case SONY_DVBS2_MODULATION_8PSK:
					info->modulation = JOKER_PSK_8;
					break;
				case SONY_DVBS2_MODULATION_16APSK:
					info->modulation = JOKER_APSK_16;
					break;
				case SONY_DVBS2_MODULATION_32APSK:
					info->modulation = JOKER_APSK_32;
					break;
				case SONY_DVBS2_MODULATION_QPSK:
				default:
					info->modulation = JOKER_QPSK;
					break;
			}
		}

		printf("\nDetected! progress=%u%% %s", res->progress, buf);

		// save results to file 
		if (joker->blind_out_filename_fd) {
			fprintf(joker->blind_out_filename_fd, "%s", buf);
			fflush(joker->blind_out_filename_fd);
		}

		// Start TS 
		memset(&pool, 0, sizeof(struct big_pool_t));
		pool.service_name_callback = &service_name_cb;
		if((ret = start_ts(joker, &pool))) {
			printf("start_ts failed. err=%d \n", ret);
			return;
		}

		/* get TV programs list */
		jdebug("Trying to get programs list ... \n");
		programs = get_programs(&pool);

		// wait until all tv channel names arrived
		timeout = 20;
		while (timeout--) {
			ready = 1;
			list_for_each_entry_safe(program, tmp, programs, list) {
				if (!strlen(program->name)) {
					ready = 0;
					break;
				}
			}
			if (ready) {
				jdebug("All programs has a name. Breaking ... \n");
				break;
			}
			jdebug("Not all programs has a name. not breaking ... \n");
			msleep(500);
		}
		blind_scan_res.programs = programs;
		blind_scan_res.event_id = EVENT_DETECT;

		if (joker->blind_scan_cb)
			joker->blind_scan_cb (&blind_scan_res);

		// Stop TS
		if((ret = stop_ts(joker, &pool))) {
			printf("stop_ts failed. err=%d \n", ret);
			return;
		}

	} else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_POWER) {
		if (!joker->blind_power_file_prefix)
			return;

		// save power
		filename = calloc(1, 1024);
		if(!filename)
			return;
		snprintf(filename, 1024, "%s-%s-%s-lnb_%d.csv", joker->blind_power_file_prefix,
				res->prefix,
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v" : "18v",
				abs(info->lnb.selected_freq));

		pCurrent = res->pPowerList->pNext;
		pfd = fopen(filename, "w+b");
		if (pfd <= 0) {
			printf("Can't open power list file %s error %d (%s)\n",
					filename, errno, strerror(errno));
			return;
		}
		while(pCurrent){
			fprintf(pfd, "%d %.2f\n", abs(pCurrent->data.power.freqKHz/1000 + info->lnb.selected_freq),
					(double)pCurrent->data.power.power/100);
			pCurrent = pCurrent->pNext;
			cnt++;
		}
		fclose(pfd);
		printf("power list (size %d) dumped to %s\n", cnt, filename);
		free(filename);
	} else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_CAND) {
		if (!joker->blind_power_file_prefix)
			return;

		// save candidates
		filename = calloc(1, 1024);
		if(!filename)
			return;
		snprintf(filename, 1024, "cand-%s-%s-%s-lnb_%d.csv", joker->blind_power_file_prefix,
				res->prefix,
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v" : "18v",
				abs(info->lnb.selected_freq));

		pCurrent = res->pCandList->pNext;
		pfd = fopen(filename, "w+b");
		if (pfd <= 0) {
			printf("Can't open cand list file %s error %d (%s)\n",
					filename, errno, strerror(errno));
			return;
		}
		while(pCurrent){
			fprintf(pfd, "candidate freq=%d sr=%d minSR=%d maxSR=%d\n",
					pCurrent->data.candidate.centerFreqKHz,
					pCurrent->data.candidate.symbolRateKSps,
					pCurrent->data.candidate.minSymbolRateKSps,
					pCurrent->data.candidate.maxSymbolRateKSps);
			pCurrent = pCurrent->pNext;
			cnt++;
		}
		fclose(pfd);
		printf("cand list (size %d) dumped to %s\n", cnt, filename);
		free(filename);
	} else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS) {
		blind_scan_res.programs = programs;
		blind_scan_res.event_id = EVENT_PROGRESS;
		if (joker->blind_scan_cb)
			joker->blind_scan_cb (&blind_scan_res);
	}

	return;
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
	if (info->lnb.switchfreq) {
		if (tone == JOKER_SEC_TONE_ON) {
			// add 31Mhz because blind scan will add this for powerscan
			// change this constant if symbolrate changed
			// freq_min = 1000 * (info->lnb.lowfreq + 2150 - info->lnb.highfreq + 31 * 2);
			freq_min = 1000 * (info->lnb.switchfreq - info->lnb.highfreq);
		} else {
			freq_max = 1000 * (info->lnb.switchfreq - info->lnb.lowfreq);
		}
	}

	printf("\n\t *** Blind scan quadrant %dv, %s LNB band (22khz %s) min/max freq=%d/%d\n",
			(voltage == JOKER_SEC_VOLTAGE_13) ? 13:18,
			(tone == JOKER_SEC_TONE_ON) ? "high" : "low",
			(tone == JOKER_SEC_TONE_ON) ? "on" : "off",
			freq_min, freq_max);

	fe->ops.set_tone(fe, tone);
	fe->ops.set_voltage(fe, voltage);
	info->lnb.selected_freq = (tone == JOKER_SEC_TONE_ON) ? info->lnb.highfreq : info->lnb.lowfreq;
	// hack: detect C band LNB
	// TODO: rework for more reliable way to detect C band LNB
	if (info->lnb.lowfreq < 6000)
		info->lnb.selected_freq *= -1;
	info->voltage = voltage;
	info->tone = tone;
	joker->info = info;
	fe->ops.blind_scan(fe, freq_min, freq_max, 1000, 45000,
			joker->blind_power_file_prefix ? true : false,
			blind_scan_callback, joker);
}

/*** Blind scan ***/
int blind_scan(struct joker_t *joker, struct tune_info_t *info)
{
	sony_integ_dvbs_s2_blindscan_param_t blindscanParam;
	struct dvb_frontend *fe = (struct dvb_frontend *)joker->fe_opaque;

	/* open file for blind scan results */
	if (joker->blind_out_filename) {
		joker->blind_out_filename_fd = fopen(joker->blind_out_filename, "w+b");
		if (joker->blind_out_filename_fd < 0) {
			printf("Can't open blind scan resulting file %s. Error=%d (%s)\n",
					joker->blind_out_filename, errno, strerror(errno));
			joker->blind_out_filename_fd = NULL;
			return -EIO;
		}

		// write file header
		fprintf(joker->blind_out_filename_fd,
				"freq_mhz,pol,standard,symbol_rate_ksps,symbol_rate_ksps_raw,modulation,fec,pilot\n");
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

	// scan only two quadrants if LNB has only one LO
	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_OFF, JOKER_SEC_VOLTAGE_13);
	blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_OFF, JOKER_SEC_VOLTAGE_18);

	if (info->lnb.switchfreq) {
		// scan another two quadrants if LNB has two LO
		blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_ON, JOKER_SEC_VOLTAGE_13);
		blind_scan_do_quadrant(joker, info, fe, JOKER_SEC_TONE_ON, JOKER_SEC_VOLTAGE_18);
		printf("\n\tBlind scan will scan four quadrants (13v/18v 22khz on/off)\n");
	} else {
		printf("\n\tBlind scan will scan two quadrants (13v/18v 22khz off)\n");
	}

	if (joker->blind_out_filename_fd) {
		fclose(joker->blind_out_filename_fd);
		joker->blind_out_filename_fd = NULL;
	}

	return 0;
}
