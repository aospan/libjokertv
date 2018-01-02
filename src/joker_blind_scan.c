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
	sony_integ_dvbs_s2_blindscan_result_t * res =
		(sony_integ_dvbs_s2_blindscan_result_t*) data;

	struct tune_info_t *info = (struct tune_info_t *)res->callback_arg;
	if (!info)
		return;

	if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT)
		printf("\nDetected! progress=%u%% %s %dMHz (lnb freq=%d) %s symbol rate=%d ksym/s\n",
				res->progress, (res->tuneParam.system == SONY_DTV_SYSTEM_DVBS) ? "DVB-S" : "DVB-S2",
				res->tuneParam.centerFreqKHz/1000 + info->lnb.selected_freq,
				info->lnb.selected_freq,
				(info->voltage == JOKER_SEC_VOLTAGE_13) ? "13v V(R)" : "18v H(L)",
				res->tuneParam.symbolRateKSps);
	else if (res->eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS)
		printf("progress=%u%%\r", res->progress);
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
		freq_min = info->lnb.lowfreq + 2150000 - info->lnb.highfreq;
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
	fe->ops.blind_scan(fe, freq_min, freq_max, 1000, 45000, blind_scan_callback, info);
}

/*** Blind scan ***/
int blind_scan(struct joker_t *joker, struct tune_info_t *info,
		struct dvb_frontend *fe)
{
	sony_integ_dvbs_s2_blindscan_param_t blindscanParam;

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

	return 0;
}
