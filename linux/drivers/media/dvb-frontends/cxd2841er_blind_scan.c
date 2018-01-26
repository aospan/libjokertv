/*
 * cxd2841er_blind_scan.c
 *
 * Sony digital demodulator blind scan for
 *	CXD2841ER - DVB-S/S2/T/T2/C/C2
 *	CXD2854ER - DVB-S/S2/T/T2/C/C2, ISDB-T/S
 *
 * Copyright 2012 Sony Corporation
 * Copyright (C) 2017 NetUP Inc.
 * Copyright (C) 2017 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
  */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/log2.h>
#include <linux/dynamic_debug.h>

#include "dvb_math.h"
#include "dvb_frontend.h"
#include "cxd2841er.h"
#include "cxd2841er_priv.h"
#include "cxd2841er_blind_scan.h"

/*** utility ***/
#define MASKUPPER(n) (((n) == 0) ? 0 : (0xFFFFFFFFU << (32 - (n))))
#define MASKLOWER(n) (((n) == 0) ? 0 : (0xFFFFFFFFU >> (32 - (n))))
/* Convert N (<32) bit 2's complement value to 32 bit signed value */
int32_t sony_Convert2SComplement(uint32_t value, uint32_t bitlen)
{   
	if((bitlen == 0) || (bitlen >= 32)){
		return (int32_t)value;
	}

	if(value & (uint32_t)(1 << (bitlen - 1))){
		/* minus value */
		return (int32_t)(MASKUPPER(32 - bitlen) | value);
	}
	else{
		/* plus value */
		return (int32_t)(MASKLOWER(bitlen) & value);
	}
}

// TODO 
sony_result_t sony_demod_dvbs_s2_CheckIQInvert (struct cxd2841er_priv *priv,
		uint8_t * pIsInvert)
{   
	if ((!priv) || (!pIsInvert)) {
		return (SONY_RESULT_ERROR_ARG);
	}

	*pIsInvert = 0;
	// TODO: inverted and singlecable
	
	return (SONY_RESULT_OK);
}

static sony_result_t incrementMemCount (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage)
{
	if (!pStorage){
		return SONY_RESULT_ERROR_ARG;
	}
	pStorage->currentUsedCount++;
	if(pStorage->currentUsedCount <= 0){
		/* Error(Overflow) */
		return SONY_RESULT_ERROR_OVERFLOW;
	}
	if(pStorage->maxUsedCount < pStorage->currentUsedCount){
		pStorage->maxUsedCount = pStorage->currentUsedCount;
	}
	return SONY_RESULT_OK;
}

static sony_result_t decrementMemCount (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage)
{   
	if (!pStorage){
		return SONY_RESULT_ERROR_ARG;
	}
	if(pStorage->currentUsedCount == 0){
		/* Error(Overflow) */
		return SONY_RESULT_ERROR_OVERFLOW;
	} else {
		pStorage->currentUsedCount--;
	}
	return SONY_RESULT_OK;
}

static sony_result_t incrementPowCount (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage)
{   
	if (!pStorage){
		return SONY_RESULT_ERROR_ARG;
	}
	pStorage->currentUsedPowerCount++;
	if(pStorage->currentUsedPowerCount <= 0){
		/* Error(Overflow) */
		return SONY_RESULT_ERROR_OVERFLOW;
	}
	if(pStorage->maxUsedPowerCount < pStorage->currentUsedPowerCount){
		pStorage->maxUsedPowerCount = pStorage->currentUsedPowerCount;
	}
	return SONY_RESULT_OK;
}

static sony_result_t decrementPowCount (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage)
{
	if (!pStorage){
		return SONY_RESULT_ERROR_ARG;
	}
	if(pStorage->currentUsedPowerCount == 0){
		/* Error(Overflow) */
		return SONY_RESULT_ERROR_OVERFLOW;
	} else {
		pStorage->currentUsedPowerCount--;
	}
	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_AllocPower (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t ** ppPower)
{
	if((!pStorage) || (!ppPower)) {
		return SONY_RESULT_ERROR_ARG;
	}

	if(pStorage->availablePowerList.pNext != NULL){
		*ppPower = pStorage->availablePowerList.pNext;
		if((*ppPower)->pNext != NULL){
			pStorage->availablePowerList.pNext = (*ppPower)->pNext;
		} else {
			/* Alloc the last data. */
			pStorage->availablePowerList.pNext = NULL;
		}
	} else {
		/* Overflow. */
		return SONY_RESULT_ERROR_OVERFLOW;
	}
	(*ppPower)->pNext = NULL;
	return incrementPowCount (pStorage);
}

sony_result_t sony_demod_dvbs_s2_blindscan_AllocData (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t ** ppData)
{
	if ((!pStorage) || (!ppData)){
		return SONY_RESULT_ERROR_ARG;
	}   

	if(pStorage->availableDataList.pNext){
		*ppData = pStorage->availableDataList.pNext;
		if((*ppData)->pNext){
			pStorage->availableDataList.pNext = (*ppData)->pNext;
		} else {
			/* Alloc the last data. */
			pStorage->availableDataList.pNext = NULL;
		}
	} else {
		/* Overflow. */
		return SONY_RESULT_ERROR_OVERFLOW;
	}
	(*ppData)->pNext = NULL;
	return incrementMemCount(pStorage);
}

sony_result_t sony_demod_dvbs_s2_blindscan_FreePower (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPower)
{
	sony_demod_dvbs_s2_blindscan_power_t * pTemp = NULL;

	if((!pStorage) || (!pPower)) {
		return SONY_RESULT_ERROR_ARG;
	}

	if(pStorage->availablePowerList.pNext != NULL){
		pTemp = pStorage->availablePowerList.pNext;
		pStorage->availablePowerList.pNext = pPower;
		pPower->pNext = pTemp;
	} else {
		/* Data is empty. */
		pStorage->availablePowerList.pNext = pPower;
		pPower->pNext = NULL;
	}
	return decrementPowCount (pStorage);
}

sony_result_t sony_demod_dvbs_s2_blindscan_ClearPowerList (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pListTop)
{   
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_power_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_power_t * pTemp = NULL;

	if ((!pStorage) || (!pListTop)){
		return SONY_RESULT_ERROR_ARG;
	}

	pCurrent = pListTop->pNext;

	while (pCurrent){
		pTemp = pCurrent->pNext;
		result = sony_demod_dvbs_s2_blindscan_FreePower (pStorage, pCurrent);
		if (result != SONY_RESULT_OK){
			return result;
		}
		pCurrent = pTemp;
	}

	pListTop->pNext = NULL;

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_FreeData (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pData)
{
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;

	if ((!pStorage) || (!pData)){
		return SONY_RESULT_ERROR_OTHER;
	}

	if(pStorage->availableDataList.pNext){
		pTemp = pStorage->availableDataList.pNext;
		pStorage->availableDataList.pNext = pData;
		pData->pNext = pTemp;
	} else {
		/* Data is empty. */
		pStorage->availableDataList.pNext = pData;
		pData->pNext = NULL;
	}
	return decrementMemCount(pStorage);
}

sony_result_t sony_demod_dvbs_s2_blindscan_ClearDataList (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pListTop)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;

	if ((!pStorage) || (!pListTop)){
		return SONY_RESULT_ERROR_ARG;
	}

	pCurrent = pListTop->pNext;

	while (pCurrent){
		pTemp = pCurrent->pNext;
		result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pCurrent);
		if (result != SONY_RESULT_OK){
			return  (result);
		}
		pCurrent = pTemp;
	}

	pListTop->pNext = NULL;

	return SONY_RESULT_OK;
}
/*** utility end ***/

/*** math ***/
#define MAX_BIT_PRECISION        5
#define FRAC_BITMASK            0x1F                    /**< Depends upon MAX_BIT_PRECISION. */
#define LOG2_10_100X            332                     /**< log2 (10) */
#define LOG2_E_100X             144                     /**< log2 (e) */

/*------------------------------------------------------------------------------
 *  Statics
 *  ------------------------------------------------------------------------------*/
/**
 *  @brief Look up table for decimal portion of log2 calculation.
 *  */
static const uint8_t log2LookUp[] = {
	0, /* 0 */ 4,               /* 0.04439 */
	9, /* 0.08746 */ 13,        /* 0.12928 */
	17, /* 0.16993 */ 21,       /* 0.20945 */
	25, /* 0.24793 */ 29,       /* 0.28540 */
	32, /* 0.32193 */ 36,       /* 0.35755 */
	39, /* 0.39232 */ 43,       /* 0.42627 */
	46, /* 0.45943 */ 49,       /* 0.49185 */
	52, /* 0.52356 */ 55,       /* 0.55249 */
	58, /* 0.58496 */ 61,       /* 0.61471 */
	64, /* 0.64386 */ 67,       /* 0.67246 */
	70, /* 0.70044 */ 73,       /* 0.72792 */
	75, /* 0.75489 */ 78,       /* 0.78136 */
	81, /* 0.80736 */ 83,       /* 0.83289 */
	86, /* 0.85798 */ 88,       /* 0.88264 */
	91, /* 0.90689 */ 93,       /* 0.93074 */
	95, /* 0.95420 */ 98        /* 0.97728 */
};

uint32_t sony_math_log2 (uint32_t x)
{
    uint8_t count = 0;
    uint8_t index = 0;
    uint32_t xval = x;

    /* Get the MSB position. */
    for (x >>= 1; x > 0; x >>= 1) {
        count++;
    }

    x = count * 100;

    if (count > 0) {
        if (count <= MAX_BIT_PRECISION) {
            /* Mask the bottom bits. */
            index = (uint8_t) (xval << (MAX_BIT_PRECISION - count)) & FRAC_BITMASK;
            x += log2LookUp[index];
        }
        else {
            /* Mask the bits just below the radix. */
            index = (uint8_t) (xval >> (count - MAX_BIT_PRECISION)) & FRAC_BITMASK;
            x += log2LookUp[index];
        }
    }

    return (x);
}

uint32_t sony_math_log10 (uint32_t x)
{
	/* log10(x) = log2 (x) / log2 (10) */
	/* Note uses: logN (x) = logM (x) / logM (N) */
	return ((100 * sony_math_log2 (x) + LOG2_10_100X / 2) / LOG2_10_100X);
}

uint32_t sony_math_log (uint32_t x)
{
	/* ln (x) = log2 (x) / log2(e) */
	return ((100 * sony_math_log2 (x) + LOG2_E_100X / 2) / LOG2_E_100X);
}

/*** monitor ***/
static const struct {
	uint32_t value;
	int32_t cnr_x1000;
} s_cn_data[] = {
	{0x033e, 0},
	{0x0339, 100},
	{0x0333, 200},
	{0x032e, 300},
	{0x0329, 400},
	{0x0324, 500},
	{0x031e, 600},
	{0x0319, 700},
	{0x0314, 800},
	{0x030f, 900},
	{0x030a, 1000},
	{0x02ff, 1100},
	{0x02f4, 1200},
	{0x02e9, 1300},
	{0x02de, 1400},
	{0x02d4, 1500},
	{0x02c9, 1600},
	{0x02bf, 1700},
	{0x02b5, 1800},
	{0x02ab, 1900},
	{0x02a1, 2000},
	{0x029b, 2100},
	{0x0295, 2200},
	{0x0290, 2300},
	{0x028a, 2400},
	{0x0284, 2500},
	{0x027f, 2600},
	{0x0279, 2700},
	{0x0274, 2800},
	{0x026e, 2900},
	{0x0269, 3000},
	{0x0262, 3100},
	{0x025c, 3200},
	{0x0255, 3300},
	{0x024f, 3400},
	{0x0249, 3500},
	{0x0242, 3600},
	{0x023c, 3700},
	{0x0236, 3800},
	{0x0230, 3900},
	{0x022a, 4000},
	{0x0223, 4100},
	{0x021c, 4200},
	{0x0215, 4300},
	{0x020e, 4400},
	{0x0207, 4500},
	{0x0201, 4600},
	{0x01fa, 4700},
	{0x01f4, 4800},
	{0x01ed, 4900},
	{0x01e7, 5000},
	{0x01e0, 5100},
	{0x01d9, 5200},
	{0x01d2, 5300},
	{0x01cb, 5400},
	{0x01c4, 5500},
	{0x01be, 5600},
	{0x01b7, 5700},
	{0x01b1, 5800},
	{0x01aa, 5900},
	{0x01a4, 6000},
	{0x019d, 6100},
	{0x0196, 6200},
	{0x018f, 6300},
	{0x0189, 6400},
	{0x0182, 6500},
	{0x017c, 6600},
	{0x0175, 6700},
	{0x016f, 6800},
	{0x0169, 6900},
	{0x0163, 7000},
	{0x015c, 7100},
	{0x0156, 7200},
	{0x0150, 7300},
	{0x014a, 7400},
	{0x0144, 7500},
	{0x013e, 7600},
	{0x0138, 7700},
	{0x0132, 7800},
	{0x012d, 7900},
	{0x0127, 8000},
	{0x0121, 8100},
	{0x011c, 8200},
	{0x0116, 8300},
	{0x0111, 8400},
	{0x010b, 8500},
	{0x0106, 8600},
	{0x0101, 8700},
	{0x00fc, 8800},
	{0x00f7, 8900},
	{0x00f2, 9000},
	{0x00ee, 9100},
	{0x00ea, 9200},
	{0x00e6, 9300},
	{0x00e2, 9400},
	{0x00de, 9500},
	{0x00da, 9600},
	{0x00d7, 9700},
	{0x00d3, 9800},
	{0x00d0, 9900},
	{0x00cc, 10000},
	{0x00c7, 10100},
	{0x00c3, 10200},
	{0x00bf, 10300},
	{0x00ba, 10400},
	{0x00b6, 10500},
	{0x00b2, 10600},
	{0x00ae, 10700},
	{0x00aa, 10800},
	{0x00a7, 10900},
	{0x00a3, 11000},
	{0x009f, 11100},
	{0x009c, 11200},
	{0x0098, 11300},
	{0x0094, 11400},
	{0x0091, 11500},
	{0x008e, 11600},
	{0x008a, 11700},
	{0x0087, 11800},
	{0x0084, 11900},
	{0x0081, 12000},
	{0x007e, 12100},
	{0x007b, 12200},
	{0x0079, 12300},
	{0x0076, 12400},
	{0x0073, 12500},
	{0x0071, 12600},
	{0x006e, 12700},
	{0x006c, 12800},
	{0x0069, 12900},
	{0x0067, 13000},
	{0x0065, 13100},
	{0x0062, 13200},
	{0x0060, 13300},
	{0x005e, 13400},
	{0x005c, 13500},
	{0x005a, 13600},
	{0x0058, 13700},
	{0x0056, 13800},
	{0x0054, 13900},
	{0x0052, 14000},
	{0x0050, 14100},
	{0x004e, 14200},
	{0x004c, 14300},
	{0x004b, 14400},
	{0x0049, 14500},
	{0x0047, 14600},
	{0x0046, 14700},
	{0x0044, 14800},
	{0x0043, 14900},
	{0x0041, 15000},
	{0x003f, 15100},
	{0x003e, 15200},
	{0x003c, 15300},
	{0x003b, 15400},
	{0x003a, 15500},
	{0x0037, 15700},
	{0x0036, 15800},
	{0x0034, 15900},
	{0x0033, 16000},
	{0x0032, 16100},
	{0x0031, 16200},
	{0x0030, 16300},
	{0x002f, 16400},
	{0x002e, 16500},
	{0x002d, 16600},
	{0x002c, 16700},
	{0x002b, 16800},
	{0x002a, 16900},
	{0x0029, 17000},
	{0x0028, 17100},
	{0x0027, 17200},
	{0x0026, 17300},
	{0x0025, 17400},
	{0x0024, 17500},
	{0x0023, 17600},
	{0x0022, 17800},
	{0x0021, 17900},
	{0x0020, 18000},
	{0x001f, 18200},
	{0x001e, 18300},
	{0x001d, 18500},
	{0x001c, 18700},
	{0x001b, 18900},
	{0x001a, 19000},
	{0x0019, 19200},
	{0x0018, 19300},
	{0x0017, 19500},
	{0x0016, 19700},
	{0x0015, 19900},
	{0x0014, 20000},
};

static const struct {
	uint32_t value;
	int32_t cnr_x1000;
} s2_cn_data[] = {
	{0x05af, 0},
	{0x0597, 100},
	{0x057e, 200},
	{0x0567, 300},
	{0x0550, 400},
	{0x0539, 500},
	{0x0522, 600},
	{0x050c, 700},
	{0x04f6, 800},
	{0x04e1, 900},
	{0x04cc, 1000},
	{0x04b6, 1100},
	{0x04a1, 1200},
	{0x048c, 1300},
	{0x0477, 1400},
	{0x0463, 1500},
	{0x044f, 1600},
	{0x043c, 1700},
	{0x0428, 1800},
	{0x0416, 1900},
	{0x0403, 2000},
	{0x03ef, 2100},
	{0x03dc, 2200},
	{0x03c9, 2300},
	{0x03b6, 2400},
	{0x03a4, 2500},
	{0x0392, 2600},
	{0x0381, 2700},
	{0x036f, 2800},
	{0x035f, 2900},
	{0x034e, 3000},
	{0x033d, 3100},
	{0x032d, 3200},
	{0x031d, 3300},
	{0x030d, 3400},
	{0x02fd, 3500},
	{0x02ee, 3600},
	{0x02df, 3700},
	{0x02d0, 3800},
	{0x02c2, 3900},
	{0x02b4, 4000},
	{0x02a6, 4100},
	{0x0299, 4200},
	{0x028c, 4300},
	{0x027f, 4400},
	{0x0272, 4500},
	{0x0265, 4600},
	{0x0259, 4700},
	{0x024d, 4800},
	{0x0241, 4900},
	{0x0236, 5000},
	{0x022b, 5100},
	{0x0220, 5200},
	{0x0215, 5300},
	{0x020a, 5400},
	{0x0200, 5500},
	{0x01f6, 5600},
	{0x01ec, 5700},
	{0x01e2, 5800},
	{0x01d8, 5900},
	{0x01cf, 6000},
	{0x01c6, 6100},
	{0x01bc, 6200},
	{0x01b3, 6300},
	{0x01aa, 6400},
	{0x01a2, 6500},
	{0x0199, 6600},
	{0x0191, 6700},
	{0x0189, 6800},
	{0x0181, 6900},
	{0x0179, 7000},
	{0x0171, 7100},
	{0x0169, 7200},
	{0x0161, 7300},
	{0x015a, 7400},
	{0x0153, 7500},
	{0x014b, 7600},
	{0x0144, 7700},
	{0x013d, 7800},
	{0x0137, 7900},
	{0x0130, 8000},
	{0x012a, 8100},
	{0x0124, 8200},
	{0x011e, 8300},
	{0x0118, 8400},
	{0x0112, 8500},
	{0x010c, 8600},
	{0x0107, 8700},
	{0x0101, 8800},
	{0x00fc, 8900},
	{0x00f7, 9000},
	{0x00f2, 9100},
	{0x00ec, 9200},
	{0x00e7, 9300},
	{0x00e2, 9400},
	{0x00dd, 9500},
	{0x00d8, 9600},
	{0x00d4, 9700},
	{0x00cf, 9800},
	{0x00ca, 9900},
	{0x00c6, 10000},
	{0x00c2, 10100},
	{0x00be, 10200},
	{0x00b9, 10300},
	{0x00b5, 10400},
	{0x00b1, 10500},
	{0x00ae, 10600},
	{0x00aa, 10700},
	{0x00a6, 10800},
	{0x00a3, 10900},
	{0x009f, 11000},
	{0x009b, 11100},
	{0x0098, 11200},
	{0x0095, 11300},
	{0x0091, 11400},
	{0x008e, 11500},
	{0x008b, 11600},
	{0x0088, 11700},
	{0x0085, 11800},
	{0x0082, 11900},
	{0x007f, 12000},
	{0x007c, 12100},
	{0x007a, 12200},
	{0x0077, 12300},
	{0x0074, 12400},
	{0x0072, 12500},
	{0x006f, 12600},
	{0x006d, 12700},
	{0x006b, 12800},
	{0x0068, 12900},
	{0x0066, 13000},
	{0x0064, 13100},
	{0x0061, 13200},
	{0x005f, 13300},
	{0x005d, 13400},
	{0x005b, 13500},
	{0x0059, 13600},
	{0x0057, 13700},
	{0x0055, 13800},
	{0x0053, 13900},
	{0x0051, 14000},
	{0x004f, 14100},
	{0x004e, 14200},
	{0x004c, 14300},
	{0x004a, 14400},
	{0x0049, 14500},
	{0x0047, 14600},
	{0x0045, 14700},
	{0x0044, 14800},
	{0x0042, 14900},
	{0x0041, 15000},
	{0x003f, 15100},
	{0x003e, 15200},
	{0x003c, 15300},
	{0x003b, 15400},
	{0x003a, 15500},
	{0x0038, 15600},
	{0x0037, 15700},
	{0x0036, 15800},
	{0x0034, 15900},
	{0x0033, 16000},
	{0x0032, 16100},
	{0x0031, 16200},
	{0x0030, 16300},
	{0x002f, 16400},
	{0x002e, 16500},
	{0x002d, 16600},
	{0x002c, 16700},
	{0x002b, 16800},
	{0x002a, 16900},
	{0x0029, 17000},
	{0x0028, 17100},
	{0x0027, 17200},
	{0x0026, 17300},
	{0x0025, 17400},
	{0x0024, 17500},
	{0x0023, 17600},
	{0x0022, 17800},
	{0x0021, 17900},
	{0x0020, 18000},
	{0x001f, 18200},
	{0x001e, 18300},
	{0x001d, 18500},
	{0x001c, 18700},
	{0x001b, 18900},
	{0x001a, 19000},
	{0x0019, 19200},
	{0x0018, 19300},
	{0x0017, 19500},
	{0x0016, 19700},
	{0x0015, 19900},
	{0x0014, 20000},
};

/*----------------------------------------------------------------------------
  Static Functions
  ----------------------------------------------------------------------------*/
static sony_result_t monitor_SamplingRateMode (struct cxd2841er_priv *priv,
		uint8_t * pIsHSMode);

static sony_result_t monitor_System (struct cxd2841er_priv *priv,
		sony_dtv_system_t * pSystem);

static sony_result_t monitor_CarrierOffset (struct cxd2841er_priv *priv,
		int32_t * pOffset);

static sony_result_t s_monitor_CodeRate (struct cxd2841er_priv *priv,
		sony_dvbs_coderate_t * pCodeRate);

static sony_result_t s_monitor_IQSense (struct cxd2841er_priv *priv,
		sony_demod_sat_iq_sense_t * pSense);

static sony_result_t s_monitor_CNR (struct cxd2841er_priv *priv,
		int32_t * pCNR);

static sony_result_t s_monitor_PER (struct cxd2841er_priv *priv,
		uint32_t * pPER);

static sony_result_t s2_monitor_IQSense (struct cxd2841er_priv *priv,
		sony_demod_sat_iq_sense_t * pSense);

static sony_result_t s2_monitor_CNR (struct cxd2841er_priv *priv,
		int32_t * pCNR);

static sony_result_t s2_monitor_PER (struct cxd2841er_priv *priv,
		uint32_t * pPER);

/*----------------------------------------------------------------------------
  Functions
  ----------------------------------------------------------------------------*/
sony_result_t sony_demod_dvbs_s2_monitor_SyncStat (struct cxd2841er_priv *priv,
		uint8_t * pTSLockStat)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t data = 0;

	if ((!priv) || (!pTSLockStat)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       11h       [2]      ITSLOCK
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x11, &data, 1);

	if (data & 0x04){
		*pTSLockStat = 1;
	} else {
		*pTSLockStat = 0;
	}

	return  (result);
}

/*
 *  Freeze all registers in the SLV-T device. This API is used by the monitor functions to ensure multiple separate 
 *          register reads are from the same snapshot 
 */
#define SLVT_FreezeReg(priv) (cxd2841er_write_reg(priv, I2C_SLVT, 0x01, 0x01))

/*
 *  Unfreeze all registers in the SLV-T device 
 */
#define SLVT_UnFreezeReg(priv) ((void)(cxd2841er_write_reg(priv, I2C_SLVT, 0x01, 0x00)))

sony_result_t sony_demod_dvbs_s2_monitor_CarrierOffset (struct cxd2841er_priv *priv,
		int32_t * pOffset)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t tsLock = 0;

	if ((!priv) || (!pOffset)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	result = sony_demod_dvbs_s2_monitor_SyncStat(priv, &tsLock);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (SONY_RESULT_ERROR_I2C);
	}

	if (tsLock == 0){
		SLVT_UnFreezeReg (priv);
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	result = monitor_CarrierOffset (priv, pOffset);

	SLVT_UnFreezeReg (priv);
	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_IFAGCOut (struct cxd2841er_priv *priv,
		uint32_t * pIFAGC)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t data[2] = {0, 0};

	if ((!priv) || (!pIFAGC)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit       Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       1Fh       [4:0]     IRFAGC_GAIN[12:8]
	 * <SLV-T>    A0h       20h       [7:0]     IRFAGC_GAIN[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x1F, data, 2);
	dev_dbg(&priv->i2c->dev, "%s: read=0x%x %x \n",
					__func__, data[0], data[1]);

	*pIFAGC = (((uint32_t)data[0] & 0x1F) << 8) | (uint32_t)(data[1] & 0xFF);

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_System (struct cxd2841er_priv *priv,
		sony_dtv_system_t * pSystem)
{
	sony_result_t result = SONY_RESULT_OK;

	if ((!priv) || (!pSystem)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	result = monitor_System (priv, pSystem);

	SLVT_UnFreezeReg (priv);

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_SymbolRate (struct cxd2841er_priv *priv,
		uint32_t * pSymbolRateSps)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isHSMode = 0;
	uint32_t itrl_ckferr_sr = 0;
	int32_t ckferr_sr = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;
	uint32_t tempDiv = 0;
	uint8_t data[4];


	if ((!priv) || (!pSymbolRateSps)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       10h       [0]      ITRL_LOCK
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, data, 1);

	if ((data[0] & 0x01) == 0x00){
		SLVT_UnFreezeReg (priv);
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	result = monitor_SamplingRateMode (priv, &isHSMode);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	/*  (In case of 2k12 Generation Demod Chips)
	 *  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A3h       D3h       [6:0]    ITRL_CKFERR_SR[22:16]
	 * <SLV-T>    A3h       D4h       [7:0]    ITRL_CKFERR_SR[15:8]
	 * <SLV-T>    A3h       D5h       [7:0]    ITRL_CKFERR_SR[7:0]
	 */
	if (SONY_DEMOD_CHIP_ID_2k12_GENERATION (priv->chip_id)) {
		/* Set SLV-T Bank : 0xA3 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA3);
		cxd2841er_read_regs(priv, I2C_SLVT, 0xD3, data, 3);

		itrl_ckferr_sr = ((uint32_t)(data[0] & 0x7F) << 16) |
			((uint32_t)(data[1] & 0xFF) <<  8) |
			(uint32_t)(data[2] & 0xFF);
		ckferr_sr = sony_Convert2SComplement (itrl_ckferr_sr, 23);
	}

	/*  (In case of 2k14 Generation Demod Chips)
	 *  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    AFh       38h       [2:0]    ITRL_CKFERR_SR[26:24]
	 * <SLV-T>    AFh       39h       [7:0]    ITRL_CKFERR_SR[23:16]
	 * <SLV-T>    AFh       3Ah       [7:0]    ITRL_CKFERR_SR[15:8]
	 * <SLV-T>    AFh       3Bh       [7:0]    ITRL_CKFERR_SR[7:0]
	 */
	if (SONY_DEMOD_CHIP_ID_2k14_GENERATION (priv->chip_id)) {
		/* Set SLV-T Bank : 0xAF */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAF);
		cxd2841er_read_regs(priv, I2C_SLVT, 0x38, data, 4);

		itrl_ckferr_sr = ((uint32_t)(data[0] & 0x07) << 24) |
			((uint32_t)(data[1] & 0xFF) << 16) |
			((uint32_t)(data[2] & 0xFF) <<  8) |
			(uint32_t)(data[3] & 0xFF);
		ckferr_sr = sony_Convert2SComplement (itrl_ckferr_sr, 27);
	}

	SLVT_UnFreezeReg (priv);

	tempDiv = (uint32_t)((int32_t)65536 - ckferr_sr);

	/* Checks to prevent overflow in the calculation. */
	if (ckferr_sr >= 64051 || tempDiv >= 0x01FFFFFF){
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	if (isHSMode){
		switch (priv->xtal) {
			case SONY_XTAL_24000:
				/*----------------------------------------------------------------
				  samplingRate = 96000 (KHz)

				  Symbol rate[Sps] = (0.5 * 96000) / ((1 - ckferr_sr/2^16) * 10^-3)
				  = 48 * 10^3 * 2^16 / ((2^16 - ckferr_sr) * 10^-3)
				  = 48 * 2^16 * 10^3 * 10^3 / 2^16 - ckferr_sr
				  = 48 * 65536 * 10^3 * 10^3 / (2^16 - ckferr_sr)
				  = 3145728000 * 100 * 10 / (65536 - ckferr_sr)
				  (Divide in 3 steps to prevent overflow.)
				  ----------------------------------------------------------------*/
				tempQ = 3145728000u / tempDiv;
				tempR = 3145728000u % tempDiv;

				tempR *= 100;
				tempQ = (tempQ * 100) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				tempR *= 10;
				tempQ = (tempQ * 10) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				/* Round up based on the remainder */
				if (tempR >= tempDiv/2) {
					*pSymbolRateSps = tempQ + 1;
				} 
				else {
					*pSymbolRateSps = tempQ;
				}
				break;

			case SONY_XTAL_20500:
			case SONY_XTAL_41000:
				/*----------------------------------------------------------------
				  samplingRate = 779000/8 (KHz)

				  Symbol rate[Sps] = (0.5 * (779000/8)) / ((1 - ckferr_sr/2^16) * 10^-3)
				  = (779 / 2^4) * 10^3 * 2^16 / ((2^16 - ckferr_sr) * 10^-3)
				  = (779 / 2^4) * 2^16 * 10^3 * 10^3 / 2^16 - ckferr_sr
				  = 779 * 2^12 * 10^3 * 10^3 / (2^16 - ckferr_sr)
				  = 3190784000 * 100 * 10 / (65536 - ckferr_sr)
				  (Divide in 3 steps to prevent overflow.)
				  ----------------------------------------------------------------*/
				tempQ = 3190784000u / tempDiv;
				tempR = 3190784000u % tempDiv;

				tempR *= 100;
				tempQ = (tempQ * 100) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				tempR *= 10;
				tempQ = (tempQ * 10) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				/* Round up based on the remainder */
				if (tempR >= tempDiv/2) {
					*pSymbolRateSps = tempQ + 1;
				} 
				else {
					*pSymbolRateSps = tempQ;
				}
				break;

			default:
				return  (SONY_RESULT_ERROR_SW_STATE);
		}
	} else {
		switch (priv->xtal) {
			case SONY_XTAL_24000:
				/*----------------------------------------------------------------
				  samplingRate = 64000 (KHz)

				  Symbol rate[Sps] = (0.5 * 64000) / ((1 - ckferr_sr/2^16) * 10^-3)
				  = 32 * 10^3 * 2^16 / ((2^16 - ckferr_sr) * 10^-3)
				  = 32 * 2^16 * 10^3 * 10^3 / 2^16 - ckferr_sr
				  = 32 * 65536 * 10^3 * 10^3 / (2^16 - ckferr_sr)
				  = 2097152000 * 100 * 10 / (65536 - ckferr_sr)
				  (Divide in 3 steps to prevent overflow.)
				  ----------------------------------------------------------------*/
				tempQ = 2097152000u / tempDiv;
				tempR = 2097152000u % tempDiv;

				tempR *= 100;
				tempQ = (tempQ * 100) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				tempR *= 10;
				tempQ = (tempQ * 10) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				/* Round up based on the remainder */
				if (tempR >= tempDiv/2) {
					*pSymbolRateSps = tempQ + 1;
				} 
				else {
					*pSymbolRateSps = tempQ;
				}

				break;

			case SONY_XTAL_20500:
			case SONY_XTAL_41000:
				/*----------------------------------------------------------------
				  samplingRate = 779000/12 (KHz)

				  Symbol rate[Sps] = 0.5 * 779000/12 / ((1 - ckferr_sr/2^16) * 10^-3)
				  = (779 / 24) * 10^3 / ((1 - ckferr_sr/2^16) * 10^-3)
				  = (779 / 24) * 10^3 * 2^16 / (2^16 - ckferr_sr) * 10^-3
				  = (779 /  3) * 10^3 * 2^13 * 10^3 / (2^16 - ckferr_sr)
				  = 779 * 2^13 * 10^3 * 10^3 / 3 * (2^16 - ckferr_sr)
				  = (6381568000 * 10^3) / 3 * (65536 - ckferr_sr)
				  = (638156800 * 100 *100) / 3 * (65536 - ckferr_sr)
				  (Divide in 4 steps to prevent overflow.)
				  ----------------------------------------------------------------*/
				tempQ = 638156800u / tempDiv;
				tempR = 638156800u % tempDiv;

				tempR *= 100;
				tempQ = (tempQ * 100) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				tempR *= 100;
				tempQ = (tempQ * 100) + tempR / tempDiv;
				tempR = tempR % tempDiv;

				/* Round up based on the remainder */
				if (tempR >= tempDiv/2) {
					*pSymbolRateSps = tempQ + 1;
				} 
				else {
					*pSymbolRateSps = tempQ;
				}

				/* Finally, divide the result by 3. */
				*pSymbolRateSps = (*pSymbolRateSps + 1) / 3;
				break;

			default:
				return  (SONY_RESULT_ERROR_SW_STATE);
		}
	}

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_IQSense (struct cxd2841er_priv *priv,
		sony_demod_sat_iq_sense_t * pSense)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;


	if ((!priv) || (!pSense)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return  (result);
			}
			if ((dtvSystem != SONY_DTV_SYSTEM_DVBS) && (dtvSystem != SONY_DTV_SYSTEM_DVBS2)){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
		case SONY_DTV_SYSTEM_DVBS2:
			dtvSystem = priv->system_sony;
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	if (dtvSystem == SONY_DTV_SYSTEM_DVBS) {
		result = s_monitor_IQSense (priv, pSense);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	} else {
		result = s2_monitor_IQSense (priv, pSense);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	}

	SLVT_UnFreezeReg (priv);
	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_monitor_CNR (struct cxd2841er_priv *priv,
		int32_t * pCNR)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;


	if ((!priv) || (!pCNR)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return  (result);
			}
			if ((dtvSystem != SONY_DTV_SYSTEM_DVBS) && (dtvSystem != SONY_DTV_SYSTEM_DVBS2)){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
		case SONY_DTV_SYSTEM_DVBS2:
			dtvSystem = priv->system_sony;
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	if (dtvSystem == SONY_DTV_SYSTEM_DVBS) {
		result = s_monitor_CNR (priv, pCNR);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	} else {
		result = s2_monitor_CNR (priv, pCNR);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	}

	SLVT_UnFreezeReg (priv);
	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_monitor_PER (struct cxd2841er_priv *priv,
		uint32_t * pPER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;

	if ((!priv) || (!pPER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return  (result);
			}
			if ((dtvSystem != SONY_DTV_SYSTEM_DVBS) && (dtvSystem != SONY_DTV_SYSTEM_DVBS2)){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
		case SONY_DTV_SYSTEM_DVBS2:
			dtvSystem = priv->system_sony;
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	if (dtvSystem == SONY_DTV_SYSTEM_DVBS) {
		result = s_monitor_PER (priv, pPER);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	} else {
		result = s2_monitor_PER (priv, pPER);
		if (result != SONY_RESULT_OK){
			SLVT_UnFreezeReg (priv);
			return (result);
		}
	}

	SLVT_UnFreezeReg (priv);
	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_monitor_CodeRate (struct cxd2841er_priv *priv,
		sony_dvbs_coderate_t * pCodeRate)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;


	if ((!priv) || (!pCodeRate)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	result = s_monitor_CodeRate (priv, pCodeRate);

	SLVT_UnFreezeReg (priv);
	return  (result);
}

sony_result_t sony_demod_dvbs_monitor_PreViterbiBER (struct cxd2841er_priv *priv,
		uint32_t * pBER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint8_t data[11];
	uint32_t bitError = 0;
	uint32_t bitCount = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if ((!priv) || (!pBER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS){
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       35h       [0]      IFVBER_VALID
	 * <SLV-T>    A0h       36h       [5:0]    IFVBER_BITERR[21:16]
	 * <SLV-T>    A0h       37h       [7:0]    IFVBER_BITERR[15:8]
	 * <SLV-T>    A0h       38h       [7:0]    IFVBER_BITERR[7:0]
	 * <SLV-T>    A0h       3Dh       [5:0]    IFVBER_BITNUM[21:16]
	 * <SLV-T>    A0h       3Eh       [7:0]    IFVBER_BITNUM[15:8]
	 * <SLV-T>    A0h       3Fh       [7:0]    IFVBER_BITNUM[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x35, data, 11);

	if (data[0] & 0x01){
		bitError = ((uint32_t)(data[1]  & 0x3F) << 16) |
			((uint32_t)(data[2]  & 0xFF) <<  8) |
			(uint32_t)(data[3]  & 0xFF);
		bitCount = ((uint32_t)(data[8]  & 0x3F) << 16) |
			((uint32_t)(data[9]  & 0xFF) <<  8) |
			(uint32_t)(data[10] & 0xFF);
		/*--------------------------------------------------------------------
		  BER = bitError / bitCount
		  = (bitError * 10^7) / bitCount
		  = ((bitError * 625 * 125 * 128) / bitCount
		  --------------------------------------------------------------------*/
		tempDiv = bitCount;
		if ((tempDiv == 0) || (bitError > bitCount)){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (bitError * 625) / tempDiv;
		tempR = (bitError * 625) % tempDiv;

		tempR *= 125;
		tempQ = (tempQ * 125) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		tempR *= 128;
		tempQ = (tempQ * 128) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if ((tempDiv != 1) && (tempR >= (tempDiv/2))){
			*pBER = tempQ + 1;
		} else {
			*pBER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_monitor_PreRSBER (struct cxd2841er_priv *priv,
		uint32_t * pBER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint8_t data[4];
	uint32_t bitError = 0;
	uint32_t period = 0;
	uint32_t bitCount = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if ((!priv) || (!pBER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS){
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS:
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);

	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       25h       [0]      IFBER_VALID
	 * <SLV-T>    A0h       26h       [7:0]    IFBER_BITERR[23:16]
	 * <SLV-T>    A0h       27h       [7:0]    IFBER_BITERR[15:8]
	 * <SLV-T>    A0h       28h       [7:0]    IFBER_BITERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x25, data, 4);

	if (data[0] & 0x01){
		bitError = ((uint32_t)(data[1]  & 0xFF) << 16) |
			((uint32_t)(data[2]  & 0xFF) <<  8) |
			(uint32_t)(data[3]  & 0xFF);

		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h     BAh     [3:0]    8'h08      period       OFBER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0xBA, data, 1);

		/*--------------------------------------------------------------------
		  BER = bitError / bitCount
		  = (bitError * 10^7) / (period * 204 * 8 * 8)
		  = (bitError * 5^7 * 2^7) / (period * 102 * 2 * 2^3 * 2^3)
		  = (bitError * 5^7) / (period * 102)
		  = (bitError * 5^3 * 5^4) / (period * 102)
		  --------------------------------------------------------------------*/
		period = (uint32_t)(1 << (data[0] & 0x0F));
		bitCount = period * 102;
		tempDiv = bitCount;
		if (tempDiv == 0){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (bitError * 125) / tempDiv;
		tempR = (bitError * 125) % tempDiv;

		tempR *= 625;
		tempQ = (tempQ * 625) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if (tempR >= (tempDiv/2)){
			*pBER = tempQ + 1;
		} else {
			*pBER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs2_monitor_PLSCode (struct cxd2841er_priv *priv,
		sony_dvbs2_plscode_t * pPLSCode)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint8_t data = 0;
	uint8_t isTSLock = 0;
	uint8_t mdcd_type = 0;

	if ((!priv) || (!pPLSCode)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS2){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS2:
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, &isTSLock);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	if (!isTSLock){
		SLVT_UnFreezeReg (priv);
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       21h       [6:2]    IMDCD_TYP[6:2]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x21, &data, 1);

	SLVT_UnFreezeReg (priv);

	mdcd_type = (uint8_t)(data & 0x7F);

	switch((mdcd_type >> 2) & 0x1F)
	{
		case 0x01:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_1_4;
			break;
		case 0x02:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_1_3;
			break;
		case 0x03:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_2_5;
			break;
		case 0x04:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_1_2;
			break;
		case 0x05:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_3_5;
			break;
		case 0x06:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_2_3;
			break;
		case 0x07:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_3_4;
			break;
		case 0x08:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_4_5;
			break;
		case 0x09:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_5_6;
			break;
		case 0x0A:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_8_9;
			break;
		case 0x0B:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_QPSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_9_10;
			break;
		case 0x0C:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_3_5;
			break;
		case 0x0D:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_2_3;
			break;
		case 0x0E:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_3_4;
			break;
		case 0x0F:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_5_6;
			break;
		case 0x10:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_8_9;
			break;
		case 0x11:
			pPLSCode->modulation = SONY_DVBS2_MODULATION_8PSK;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_9_10;
			break;
		case 0x1D:
			/* Reserved */
			pPLSCode->modulation = SONY_DVBS2_MODULATION_RESERVED_29;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_RESERVED_29;
			break;
		case 0x1E:
			/* Reserved */
			pPLSCode->modulation = SONY_DVBS2_MODULATION_RESERVED_30;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_RESERVED_30;
			break;
		case 0x1F:
			/* Reserved */
			pPLSCode->modulation = SONY_DVBS2_MODULATION_RESERVED_31;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_RESERVED_31;
			break;
		default:
			/* 16APSK and 32APSK are not supported. */
			pPLSCode->modulation = SONY_DVBS2_MODULATION_INVALID;
			pPLSCode->codeRate = SONY_DVBS2_CODERATE_INVALID;
			break;
	}

	/* Frame type is always "Normal" */
	pPLSCode->isShortFrame = 0;

	/* Pilot */
	if (mdcd_type & 0x01){
		pPLSCode->isPilotOn = 1;
	} else {
		pPLSCode->isPilotOn = 0;
	}

	return  (result);
}

sony_result_t sony_demod_dvbs2_monitor_PreLDPCBER (struct cxd2841er_priv *priv,
		uint32_t * pBER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint32_t bitError = 0;
	uint32_t period = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;
	uint8_t data[5];

	if ((!priv) || (!pBER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS2){
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS2:
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xB2 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xB2);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    B2h       30h       [0]      IFLBER_VALID
	 * <SLV-T>    B2h       31h       [3:0]    IFLBER_BITERR[27:24]
	 * <SLV-T>    B2h       32h       [7:0]    IFLBER_BITERR[23:16]
	 * <SLV-T>    B2h       33h       [7:0]    IFLBER_BITERR[15:8]
	 * <SLV-T>    B2h       34h       [7:0]    IFLBER_BITERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x30, data, 5);

	if (data[0] & 0x01){
		/* Bit error count */
		bitError = ((uint32_t)(data[1] & 0x0F) << 24) |
			((uint32_t)(data[2] & 0xFF) << 16) |
			((uint32_t)(data[3] & 0xFF) <<  8) |
			(uint32_t)(data[4] & 0xFF);

		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * -----------------------------------------------------------------------------
		 * <SLV-T>    A0h     7Ah     [3:0]    8'h08      period       OFLBER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0x7A, data, 1);

		/* Measurement period */
		period = (uint32_t)(1 << (data[0] & 0x0F));

		if (bitError > (period * 64800)){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		/*--------------------------------------------------------------------
		  BER = bitError / (period * 64800)
		  = (bitError * 10^7) / (period * 64800)
		  = (bitError * 10^5) / (period * 648)
		  = (bitError * 12500) / (period * 81)
		  = (bitError * 10) * 1250 / (period * 81)
		  --------------------------------------------------------------------*/
		tempDiv = period * 81;
		if (tempDiv == 0){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (bitError * 10) / tempDiv;
		tempR = (bitError * 10) % tempDiv;

		tempR *= 1250;
		tempQ = (tempQ * 1250) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if (tempR >= (tempDiv/2)){
			*pBER = tempQ + 1;
		} else {
			*pBER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs2_monitor_PreBCHBER (struct cxd2841er_priv *priv,
		uint32_t * pBER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint32_t bitError = 0;
	uint32_t period = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;
	uint8_t data[4];
	sony_dvbs2_plscode_t plscode;
	uint32_t tempA = 0;
	uint32_t tempB = 0;

	if ((!priv) || (!pBER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS2){
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS2:
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       2Fh       [0]      IF2BER_VALID
	 * <SLV-T>    A0h       30h       [6:0]    IF2BER_BITERR[22:16]
	 * <SLV-T>    A0h       31h       [7:0]    IF2BER_BITERR[15:8]
	 * <SLV-T>    A0h       32h       [7:0]    IF2BER_BITERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x2F, data, 4);

	if (data[0] & 0x01){
		/* Bit error count */
		bitError = ((uint32_t)(data[1] & 0x7F) << 16) |
			((uint32_t)(data[2] & 0xFF) <<  8) |
			(uint32_t)(data[3] & 0xFF);

		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h     BCh     [3:0]    8'h08      period       OF2BER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0xBC, data, 1);

		/* Measurement period */
		period = (uint32_t)(1 << (data[0] & 0x0F));

		/* Code rate */
		result = sony_demod_dvbs2_monitor_PLSCode (priv, &plscode);
		if (result != SONY_RESULT_OK){
			return  (result);
		}

		/*--------------------------------------------------------------------
		  BER = bitError / (period * 64800 * codeRate)       (codeRate = A/B)
		  = (bitError * 10^9) / (period * 64800 * A/B)
		  = (bitError * 10^7 * B) / (period * 648 * A)
		  = (bitError * 1250000 * B) / (period * 81 * A)
		  = ((bitError * B * 10) * 125 * 100 * 10) / (period * 81 * A)
		  --------------------------------------------------------------------*/
		switch(plscode.codeRate)
		{
			case SONY_DVBS2_CODERATE_1_4:  tempA = 1; tempB =  4; break;
			case SONY_DVBS2_CODERATE_1_3:  tempA = 1; tempB =  3; break;
			case SONY_DVBS2_CODERATE_2_5:  tempA = 2; tempB =  5; break;
			case SONY_DVBS2_CODERATE_1_2:  tempA = 1; tempB =  2; break;
			case SONY_DVBS2_CODERATE_3_5:  tempA = 3; tempB =  5; break;
			case SONY_DVBS2_CODERATE_2_3:  tempA = 2; tempB =  3; break;
			case SONY_DVBS2_CODERATE_3_4:  tempA = 3; tempB =  4; break;
			case SONY_DVBS2_CODERATE_4_5:  tempA = 4; tempB =  5; break;
			case SONY_DVBS2_CODERATE_5_6:  tempA = 5; tempB =  6; break;
			case SONY_DVBS2_CODERATE_8_9:  tempA = 8; tempB =  9; break;
			case SONY_DVBS2_CODERATE_9_10: tempA = 9; tempB = 10; break;
			default: return  (SONY_RESULT_ERROR_HW_STATE);
		}

		if((bitError * tempB) > (period * 64800 * tempA)){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempDiv = period * 81 * tempA;
		if (tempDiv == 0){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (bitError * tempB * 10) / tempDiv;
		tempR = (bitError * tempB * 10) % tempDiv;

		tempR *= 125;
		tempQ = (tempQ * 125) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		tempR *= 100;
		tempQ = (tempQ * 100) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		tempR *= 10;
		tempQ = (tempQ * 10) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if (tempR >= (tempDiv/2)){
			*pBER = tempQ + 1;
		} else {
			*pBER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs2_monitor_PostBCHFER (struct cxd2841er_priv *priv,
		uint32_t * pFER)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint8_t data[6];
	uint32_t frameError = 0;
	uint32_t frameCount = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if ((!priv) || (!pFER)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS2){
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS2:
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       2Fh       [0]      IF2BER_VALID
	 * <SLV-T>    A0h       33h       [7:0]    IF2BER_FRMERR[15:8]
	 * <SLV-T>    A0h       34h       [7:0]    IF2BER_FRMERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x2F, data, 6);

	if (data[0] & 0x01){
		frameError = ((uint32_t)(data[4] & 0xFF) << 8) |
			(uint32_t)(data[5] & 0xFF);

		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h     BCh     [3:0]    8'h08      period       OF2BER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0xBC, data, 1);

		/* Measurement period */
		frameCount = (uint32_t)(1 << (data[0] & 0x0F));

		/*--------------------------------------------------------------------
		  FER = frameError / frameCount
		  = (frameError * 10^6) / frameCount
		  = ((frameError * 10000) * 100) / frameCount
		  --------------------------------------------------------------------*/
		tempDiv = frameCount;
		if ((tempDiv == 0) || (frameError > frameCount)){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (frameError * 10000) / tempDiv;
		tempR = (frameError * 10000) % tempDiv;

		tempR *= 100;
		tempQ = (tempQ * 100) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if ((tempDiv != 1) && (tempR >= (tempDiv/2))){
			*pFER = tempQ + 1;
		} else {
			*pFER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs2_monitor_BBHeader (struct cxd2841er_priv *priv, 
		sony_dvbs2_bbheader_t * pBBHeader)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem = SONY_DTV_SYSTEM_UNKNOWN;
	uint8_t isTSLock = 0;
	uint8_t data[7];

	if ((!priv) || (!pBBHeader)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Freeze the register. */
	if (SLVT_FreezeReg (priv) != SONY_RESULT_OK) {
		return  (SONY_RESULT_ERROR_I2C);
	}

	switch(priv->system_sony)
	{
		case SONY_DTV_SYSTEM_ANY:
			result = monitor_System (priv, &dtvSystem);
			if (result != SONY_RESULT_OK){
				SLVT_UnFreezeReg (priv);
				return  (result);
			}
			if (dtvSystem != SONY_DTV_SYSTEM_DVBS2){
				SLVT_UnFreezeReg (priv);
				return  (SONY_RESULT_ERROR_HW_STATE);
			}
			break;

		case SONY_DTV_SYSTEM_DVBS2:
			break;

		default:
			SLVT_UnFreezeReg (priv);
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, &isTSLock);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	if (!isTSLock){
		SLVT_UnFreezeReg (priv);
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * ------------------------------------------------
	 * <SLV-T>    A0h       49h       [7:0]    IBBH[79:72]
	 * <SLV-T>    A0h       4Ah       [7:0]    IBBH[71:64]
	 * <SLV-T>    A0h       4Bh       [7:0]    IBBH[63:56]
	 * <SLV-T>    A0h       4Ch       [7:0]    IBBH[55:48]
	 * <SLV-T>    A0h       4Dh       [7:0]    IBBH[47:40]
	 * <SLV-T>    A0h       4Eh       [7:0]    IBBH[39:32]
	 * <SLV-T>    A0h       4Fh       [7:0]    IBBH[31:24]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x49, data, sizeof (data));
	SLVT_UnFreezeReg (priv);

	/* Convert data to appropriate format. */
	pBBHeader->streamInput = (sony_dvbs2_stream_t) ((data[0] & 0xC0) >> 6);
	pBBHeader->isSingleInputStream = (data[0] & 0x20) >> 5;
	pBBHeader->isConstantCodingModulation = (data[0] & 0x10) >> 4;
	pBBHeader->issyIndicator = (data[0] & 0x08) >> 3;
	pBBHeader->nullPacketDeletion = (data[0] & 0x04) >> 2;
	pBBHeader->rollOff = (sony_dvbs2_rolloff_t) (data[0] & 0x03);
	pBBHeader->inputStreamIdentifier = data[1];
	pBBHeader->userPacketLength = (data[2] << 8);
	pBBHeader->userPacketLength |= data[3];
	pBBHeader->dataFieldLength = (data[4] << 8);
	pBBHeader->dataFieldLength |= data[5];
	pBBHeader->syncByte = data[6];

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_Pilot (struct cxd2841er_priv *priv,
		uint8_t * pPlscLock,
		uint8_t * pPilotOn)
{
	uint8_t data = 0;


	if ((!priv) || (!pPlscLock) || (!pPilotOn)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x11, &data, 1);
	*pPlscLock = (uint8_t)(data & 0x01);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x21, &data, 1);
	*pPilotOn = (uint8_t)(data & 0x01);

	SLVT_UnFreezeReg (priv);
	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_monitor_ScanInfo (struct cxd2841er_priv *priv,
		uint8_t * pTSLock,
		int32_t * pOffset,
		sony_dtv_system_t * pSystem)
{
	sony_result_t result = SONY_RESULT_OK;


	if ((!priv) || (!pTSLock) || (!pOffset) || (!pSystem)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	SLVT_FreezeReg (priv);

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, pTSLock);
	if ((result != SONY_RESULT_OK) || (*pTSLock == 0)){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	result = monitor_CarrierOffset (priv, pOffset);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	result = monitor_System (priv, pSystem);
	if (result != SONY_RESULT_OK){
		SLVT_UnFreezeReg (priv);
		return  (result);
	}

	SLVT_UnFreezeReg (priv);
	return  (result);
}

sony_result_t sony_demod_dvbs_s2_monitor_TsRate (struct cxd2841er_priv *priv,
		uint32_t * pTsRateKbps)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_dtv_system_t dtvSystem;
	uint32_t symbolRateSps;
	uint32_t symbolRateKSps;


	if ((!priv) || (!pTsRateKbps)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_monitor_System (priv, &dtvSystem);
	if (result != SONY_RESULT_OK) {
		return  (result);
	}
	result = sony_demod_dvbs_s2_monitor_SymbolRate (priv, &symbolRateSps);
	if (result != SONY_RESULT_OK) {
		return  (result);
	}
	symbolRateKSps = (symbolRateSps + 500) / 1000;
	if (dtvSystem == SONY_DTV_SYSTEM_DVBS) {
		sony_dvbs_coderate_t codeRate;
		uint32_t numerator;
		uint32_t denominator;
		result = sony_demod_dvbs_monitor_CodeRate (priv, &codeRate);
		if (result != SONY_RESULT_OK) {
			return  (result);
		}
		switch (codeRate) {
			case SONY_DVBS_CODERATE_1_2:
				numerator = 1;
				denominator = 2;
				break;
			case SONY_DVBS_CODERATE_2_3:
				numerator = 2;
				denominator = 3;
				break;
			case SONY_DVBS_CODERATE_3_4:
				numerator = 3;
				denominator = 4;
				break;
			case SONY_DVBS_CODERATE_5_6:
				numerator = 5;
				denominator = 6;
				break;
			case SONY_DVBS_CODERATE_7_8:
				numerator = 7;
				denominator = 8;
				break;
			default:
				return  (SONY_RESULT_ERROR_OTHER);
		}
		if (symbolRateKSps >= 60000) {
			return  (SONY_RESULT_ERROR_HW_STATE);
		} else {
			/*
			 * Bit rate = SR[MSps] * 2 * CodeRate * 188 / 204
			 *          = SR[MSps] *     CodeRate * 188 / 102
			 *          = SR[MSps] *     CodeRate *  94 /  51
			 */
			*pTsRateKbps = ((symbolRateKSps * numerator * 94) + (51 * denominator / 2)) / (51 * denominator);
		}
	} else if (dtvSystem == SONY_DTV_SYSTEM_DVBS2) {
		sony_dvbs2_plscode_t plsCode;
		uint32_t kbch;
		result = sony_demod_dvbs2_monitor_PLSCode (priv, &plsCode);
		if (result != SONY_RESULT_OK) {
			return  (result);
		}
		switch (plsCode.codeRate) {
			case SONY_DVBS2_CODERATE_1_2:
				kbch = 32208;
				break;
			case SONY_DVBS2_CODERATE_3_5:
				kbch = 38688;
				break;
			case SONY_DVBS2_CODERATE_2_3:
				kbch = 43040;
				break;
			case SONY_DVBS2_CODERATE_3_4:
				kbch = 48408;
				break;
			case SONY_DVBS2_CODERATE_4_5:
				kbch = 51648;
				break;
			case SONY_DVBS2_CODERATE_5_6:
				kbch = 53840;
				break;
			case SONY_DVBS2_CODERATE_8_9:
				kbch = 57472;
				break;
			case SONY_DVBS2_CODERATE_9_10:
				kbch = 58192;
				break;
			default:
				return  (SONY_RESULT_ERROR_OTHER);
		}
		if (plsCode.modulation == SONY_DVBS2_MODULATION_8PSK) {
			if (plsCode.isPilotOn) {
				*pTsRateKbps = ((symbolRateKSps * (kbch - 80)) + 11097) / 22194;
			} else {
				*pTsRateKbps = ((symbolRateKSps * (kbch - 80)) + 10845) / 21690;
			}
		} else if (plsCode.modulation == SONY_DVBS2_MODULATION_QPSK) {
			if (plsCode.isPilotOn) {
				*pTsRateKbps = ((symbolRateKSps * (kbch - 80)) + 16641) / 33282;
			} else {
				*pTsRateKbps = ((symbolRateKSps * (kbch - 80)) + 16245) / 32490;
			}
		} else {
			return  (SONY_RESULT_ERROR_OTHER);
		}
	} else {
		return  (SONY_RESULT_ERROR_OTHER);
	}

	return  (result);
}

/*----------------------------------------------------------------------------
  Static Functions
  ----------------------------------------------------------------------------*/
static sony_result_t monitor_System (struct cxd2841er_priv *priv,
		sony_dtv_system_t * pSystem)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isTSLock = 0;
	uint8_t data = 0;


	if ((!priv) || (!pSystem)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, &isTSLock);
	if (result != SONY_RESULT_OK){return  (result);}

	if (!isTSLock){
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       50h       [1:0]    IMODE[1:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x50, &data, 1);

	switch(data & 0x03)
	{
		case 0x00:
			*pSystem = SONY_DTV_SYSTEM_DVBS2;
			break;

		case 0x01:
			*pSystem = SONY_DTV_SYSTEM_DVBS;
			break;

		default:
			return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (result);
}

static sony_result_t monitor_CarrierOffset (struct cxd2841er_priv *priv,
		int32_t * pOffset)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t iqInv = 0;
	uint8_t isHSMode = 0;
	uint8_t data[3];
	uint32_t regValue = 0;
	int32_t cfrl_ctrlval = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;
	uint32_t tempDiv = 0;
	uint8_t isNegative = 0;


	result = monitor_SamplingRateMode (priv, &isHSMode);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       45h       [4:0]    ICFRL_CTRLVAL[20:16]
	 * <SLV-T>    A0h       46h       [7:0]    ICFRL_CTRLVAL[15:8]
	 * <SLV-T>    A0h       47h       [7:0]    ICFRL_CTRLVAL[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x45, data, 3);

	regValue = (((uint32_t)data[0] & 0x1F) << 16) | (((uint32_t)data[1] & 0xFF) <<  8) | ((uint32_t)data[2] & 0xFF);
	cfrl_ctrlval = sony_Convert2SComplement(regValue, 21);

	if (isHSMode){
		switch(priv->xtal)
		{
			case SONY_XTAL_24000:
				/*----------------------------------------------------------------
				  samplingRate = 96000 (KHz)

				  Offset(KHz) = cfrl_ctrlval * (-1) * 96000 / 2^20
				  = cfrl_ctrlval * (-1) * (375 * 2^8) / 2^20
				  = cfrl_ctrlval * (-1) * 375 / 2^12
				  ----------------------------------------------------------------*/
				if(cfrl_ctrlval > 0){
					*pOffset = ((cfrl_ctrlval * (-375)) - 2048) / 4096;
				} else {
					*pOffset = ((cfrl_ctrlval * (-375)) + 2048) / 4096;
				}
				break;

			case SONY_XTAL_20500:
			case SONY_XTAL_41000:
				/*----------------------------------------------------------------
				  samplingRate = 779000/8 (KHz)

				  Offset(KHz) = (cfrl_ctrlval * (-1) *  779000 / 8) / 2^20
				  = (cfrl_ctrlval * (-1) *  97375) / 2^20
				  = (cfrl_ctrlval * (-779) * 125)  / 1048576
				  ----------------------------------------------------------------*/
				tempDiv = 1048576; /* 2^20 */
				if(cfrl_ctrlval < 0){
					isNegative = 0;
					tempQ = (uint32_t)(cfrl_ctrlval * (-779)) / tempDiv;
					tempR = (uint32_t)(cfrl_ctrlval * (-779)) % tempDiv;
				} else {
					isNegative = 1;
					tempQ = (uint32_t)(cfrl_ctrlval * 779) / tempDiv;
					tempR = (uint32_t)(cfrl_ctrlval * 779) % tempDiv;
				}

				tempR *= 125;
				tempQ = (tempQ * 125) + (tempR / tempDiv);
				tempR = tempR % tempDiv;

				if (tempR >= (tempDiv/2)){
					if (isNegative){
						*pOffset = ((int32_t)tempQ * (-1)) - 1;
					} else {
						*pOffset = (int32_t)tempQ + 1;
					}
				} else {
					if (isNegative){
						*pOffset = (int32_t)tempQ * (-1);
					} else {
						*pOffset = (int32_t)tempQ;
					}
				}
				break;

			default:
				return  (SONY_RESULT_ERROR_SW_STATE);
		}
	} else {
		switch(priv->xtal)
		{
			case SONY_XTAL_24000:
				/*----------------------------------------------------------------
				  samplingRate = 64000(KHz)

				  Offset(KHz) = cfrl_ctrlval * (-1) * 64000 / 2^20
				  = cfrl_ctrlval * (-1) * 125 * 2^9 / 2^20
				  = cfrl_ctrlval * (-1) * 125 / 2^11
				  ----------------------------------------------------------------*/
				if(cfrl_ctrlval > 0){
					*pOffset = ((cfrl_ctrlval * (-125)) - 1024) / 2048;
				} else {
					*pOffset = ((cfrl_ctrlval * (-125)) + 1024) / 2048;
				}
				break;

			case SONY_XTAL_20500:
			case SONY_XTAL_41000:
				/*----------------------------------------------------------------
				  samplingRate = 779000/12 (KHz)

				  Offset(KHz) = cfrl_ctrlval * (-1) * (779000 / 12) / 2^20
				  = cfrl_ctrlval * (-1) * (97375 * 8 / 12) / 2^20
				  = cfrl_ctrlval * (-1) * (97375 * 2 / 3) / 2^20
				  = cfrl_ctrlval * (-1) * (97375 / 3) / 2^19
				  = cfrl_ctrlval * (-779) * 125 / (2^19 * 3)
				  ----------------------------------------------------------------*/
				tempDiv = 1572864; /* (2^19 * 3) */
				if(cfrl_ctrlval < 0){
					isNegative = 0;
					tempQ = (uint32_t)(cfrl_ctrlval * (-779)) / tempDiv;
					tempR = (uint32_t)(cfrl_ctrlval * (-779)) % tempDiv;
				} else {
					isNegative = 1;
					tempQ = (uint32_t)(cfrl_ctrlval * 779) / tempDiv;
					tempR = (uint32_t)(cfrl_ctrlval * 779) % tempDiv;
				}

				tempR *= 125;
				tempQ = (tempQ * 125) + (tempR / tempDiv);
				tempR = tempR % tempDiv;

				if (tempR >= (tempDiv/2)){
					if(isNegative){
						*pOffset = ((int32_t)tempQ * (-1)) - 1;
					} else {
						*pOffset = (int32_t)tempQ + 1;
					}
				} else {
					if(isNegative){
						*pOffset = (int32_t)tempQ * (-1);
					} else {
						*pOffset = (int32_t)tempQ;
					}
				}
				break;

			default:
				return  (SONY_RESULT_ERROR_SW_STATE);
		}
	}

	result =sony_demod_dvbs_s2_CheckIQInvert (priv, &iqInv);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	if (iqInv){
		*pOffset *= (-1);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t s_monitor_CodeRate (struct cxd2841er_priv *priv,
		sony_dvbs_coderate_t * pCodeRate)
{
	uint8_t data = 0;


	if ((!priv) || (!pCodeRate)) {
		return  (SONY_RESULT_ERROR_ARG);
	}
	/* Set SLV-T Bank : 0xB1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xB1);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    B1h       10h       [7]      ISYND_VAL
	 * <SLV-T>    B1h       10h       [2:0]    ISYND_CDRATE[2:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, &data, 1);

	if (data & 0x80){
		switch(data & 0x07)
		{
			case 0x00:
				*pCodeRate = SONY_DVBS_CODERATE_1_2;
				break;

			case 0x01:
				*pCodeRate = SONY_DVBS_CODERATE_2_3;
				break;

			case 0x02:
				*pCodeRate = SONY_DVBS_CODERATE_3_4;
				break;

			case 0x03:
				*pCodeRate = SONY_DVBS_CODERATE_5_6;
				break;

			case 0x04:
				*pCodeRate = SONY_DVBS_CODERATE_7_8;
				break;

			default:
				return  (SONY_RESULT_ERROR_HW_STATE);
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t monitor_SamplingRateMode (struct cxd2841er_priv *priv,
		uint8_t * pIsHSMode)
{
	uint8_t data = 0;


	if ((!priv) || (!pIsHSMode)) {
		return  (SONY_RESULT_ERROR_ARG);
	}
	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       10h       [0]      ITRL_LOCK
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, &data, 1);
	if (data & 0x01){
		/*  slave     Bank      Addr      Bit      Signal name
		 * --------------------------------------------------------------
		 * <SLV-T>    A0h       50h       [4]      IHSMODE
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0x50, &data, 1);

		if (data & 0x10){
			/* High sample mode */
			*pIsHSMode = 1;
		} else {
			/* Low sample mode */
			*pIsHSMode = 0;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t s_monitor_IQSense (struct cxd2841er_priv *priv,
		sony_demod_sat_iq_sense_t * pSense)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isTSLock = 0;
	uint8_t isInverted = 0;
	uint8_t data = 0;
	sony_dvbs_coderate_t codeRate;


	if ((!priv) || (!pSense)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, &isTSLock);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	if (!isTSLock){
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	result = s_monitor_CodeRate (priv, &codeRate);
	if (result != SONY_RESULT_OK){return  (result);}

	/* Set SLV-T Bank : 0xB1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xB1);

	switch(codeRate)
	{
		case SONY_DVBS_CODERATE_1_2:
		case SONY_DVBS_CODERATE_2_3:
		case SONY_DVBS_CODERATE_3_4:
		case SONY_DVBS_CODERATE_7_8:
			/*  slave     Bank      Addr      Bit      Signal name
			 * --------------------------------------------------------------
			 * <SLV-T>    B1h       3Eh       [0]      ISYND_IQTURN
			 */
			cxd2841er_read_regs(priv, I2C_SLVT, 0x3E, &data, 1);
			break;

		case SONY_DVBS_CODERATE_5_6:
			/*  slave     Bank      Addr      Bit      Signal name
			 * --------------------------------------------------------------
			 * <SLV-T>    B1h       5Dh       [0]      IBYTE_R56IQINV
			 */
			cxd2841er_read_regs(priv, I2C_SLVT, 0x5D, &data, 1);
			break;

		default:
			return  (SONY_RESULT_ERROR_HW_STATE);
	}

	result = sony_demod_dvbs_s2_CheckIQInvert (priv, &isInverted);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	if (isInverted){
		*pSense = (data & 0x01) ? SONY_DEMOD_SAT_IQ_SENSE_NORMAL : SONY_DEMOD_SAT_IQ_SENSE_INV;
	} else {
		*pSense = (data & 0x01) ? SONY_DEMOD_SAT_IQ_SENSE_INV : SONY_DEMOD_SAT_IQ_SENSE_NORMAL;
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t s_monitor_CNR (struct cxd2841er_priv *priv,
		int32_t * pCNR)
{
	int32_t index = 0;
	int32_t minIndex = 0;
	int32_t maxIndex = 0;
	uint8_t data[3];
	uint32_t value = 0;


	if ((!priv) || (!pCNR)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA1);
	/*  slave     Bank      Addr      Bit     Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A1h       10h       [0]     ICPM_QUICKRDY
	 * <SLV-T>    A1h       11h       [4:0]   ICPM_QUICKCNDT[12:8]
	 * <SLV-T>    A1h       12h       [7:0]   ICPM_QUICKCNDT[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, data, 3);

	if(data[0] & 0x01){
		value = ((uint32_t)(data[1] & 0x1F) << 8) | (uint32_t)(data[2] & 0xFF);

		minIndex = 0;
		maxIndex = sizeof(s_cn_data)/sizeof(s_cn_data[0]) - 1;

		if(value >= s_cn_data[minIndex].value){
			*pCNR = s_cn_data[minIndex].cnr_x1000;
			return  (SONY_RESULT_OK);
		}
		if(value <= s_cn_data[maxIndex].value){
			*pCNR = s_cn_data[maxIndex].cnr_x1000;
			return  (SONY_RESULT_OK);
		}

		while ((maxIndex - minIndex) > 1){

			index = (maxIndex + minIndex) / 2;

			if (value == s_cn_data[index].value){
				*pCNR = s_cn_data[index].cnr_x1000;
				return  (SONY_RESULT_OK);
			} else if (value > s_cn_data[index].value){
				maxIndex = index;
			} else {
				minIndex = index;
			}

			if ((maxIndex - minIndex) <= 1){
				if (value == s_cn_data[maxIndex].value){
					*pCNR = s_cn_data[maxIndex].cnr_x1000;
					return  (SONY_RESULT_OK);
				} else {
					*pCNR = s_cn_data[minIndex].cnr_x1000;
					return  (SONY_RESULT_OK);
				}
			}
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	/* Error */
	return  (SONY_RESULT_ERROR_OTHER);
}

static sony_result_t s_monitor_PER (struct cxd2841er_priv *priv,
		uint32_t * pPER)
{
	uint8_t data[3];
	uint32_t packetError = 0;
	uint32_t period = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if ((!priv) || (!pPER)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A0h       25h       [0]      IFBER_VALID
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x25, data, 1);

	if (data[0] & 0x01){
		/*  slave     Bank      Addr      Bit      Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h       2Ch       [1:0]    IFBER_PKTERR[17:16]
		 * <SLV-T>    A0h       2Dh       [7:0]    IFBER_PKTERR[15:8]
		 * <SLV-T>    A0h       2Eh       [7:0]    IFBER_PKTERR[7:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0x2C, data, 3);
		packetError = ((uint32_t)(data[0]  & 0x03) << 16) |
			((uint32_t)(data[1]  & 0xFF) <<  8) |
			(uint32_t)(data[2]  & 0xFF);

		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h     BAh     [3:0]    8'h08      period       OFBER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0xBA, data, 1);

		period = (uint32_t)(1 << (data[0] & 0x0F));
		/*--------------------------------------------------------------------
		  PER = packetError / (period * 8)
		  = (packetError * 10^6) / (period * 8)
		  = (packetError * 125000) / period
		  = (packetError * 1000) * 125 / period
		  --------------------------------------------------------------------*/
		tempDiv = period;
		if ((tempDiv == 0) || (packetError > (period * 8))){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (packetError * 1000) / tempDiv;
		tempR = (packetError * 1000) % tempDiv;

		tempR *= 125;
		tempQ = (tempQ * 125) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if ((tempDiv != 1) && (tempR >= (tempDiv/2))){
			*pPER = tempQ + 1;
		} else {
			*pPER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t s2_monitor_IQSense (struct cxd2841er_priv *priv,
		sony_demod_sat_iq_sense_t * pSense)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isTSLock = 0;
	uint8_t isInverted = 0;
	uint8_t data = 0;


	if ((!priv) || (!pSense)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_monitor_SyncStat (priv, &isTSLock);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	if (!isTSLock){
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    ABh       23h       [0]      ICFRL_FIQINV
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x23, &data, 1);

	SLVT_UnFreezeReg (priv);

	result = sony_demod_dvbs_s2_CheckIQInvert (priv, &isInverted);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	if (isInverted){
		*pSense = (data & 0x01) ? SONY_DEMOD_SAT_IQ_SENSE_NORMAL : SONY_DEMOD_SAT_IQ_SENSE_INV;
	} else {
		*pSense = (data & 0x01) ? SONY_DEMOD_SAT_IQ_SENSE_INV : SONY_DEMOD_SAT_IQ_SENSE_NORMAL;
	}

	return  (result);
}

static sony_result_t s2_monitor_CNR (struct cxd2841er_priv *priv,
		int32_t * pCNR)
{
	int32_t index = 0;
	int32_t minIndex = 0;
	int32_t maxIndex = 0;
	uint8_t data[3];
	uint32_t value = 0;


	if ((!priv) || (!pCNR)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	if ((priv->system_sony != SONY_DTV_SYSTEM_DVBS2) &&
			(priv->system_sony != SONY_DTV_SYSTEM_ANY)) {
		/* Not DVB-S2 or ANY */
		return  (SONY_RESULT_ERROR_SW_STATE);
	}

	/* Set SLV-T Bank : 0xA1 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA1);
	/*  slave     Bank      Addr      Bit     Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    A1h       10h       [0]     ICPM_QUICKRDY
	 * <SLV-T>    A1h       11h       [4:0]   ICPM_QUICKCNDT[12:8]
	 * <SLV-T>    A1h       12h       [7:0]   ICPM_QUICKCNDT[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, data, 3);

	if(data[0] & 0x01){
		value = ((uint32_t)(data[1] & 0x1F) << 8) | (uint32_t)(data[2] & 0xFF);

		minIndex = 0;
		maxIndex = sizeof(s2_cn_data)/sizeof(s2_cn_data[0]) - 1;

		if(value >= s2_cn_data[minIndex].value){
			*pCNR = s2_cn_data[minIndex].cnr_x1000;
			return  (SONY_RESULT_OK);
		}
		if(value <= s2_cn_data[maxIndex].value){
			*pCNR = s2_cn_data[maxIndex].cnr_x1000;
			return  (SONY_RESULT_OK);
		}

		while ((maxIndex - minIndex) > 1){

			index = (maxIndex + minIndex) / 2;

			if (value == s2_cn_data[index].value){
				*pCNR = s2_cn_data[index].cnr_x1000;
				return  (SONY_RESULT_OK);
			} else if (value > s2_cn_data[index].value){
				maxIndex = index;
			} else {
				minIndex = index;
			}

			if ((maxIndex - minIndex) <= 1){
				if (value == s2_cn_data[maxIndex].value){
					*pCNR = s2_cn_data[maxIndex].cnr_x1000;
					return  (SONY_RESULT_OK);
				} else {
					*pCNR = s2_cn_data[minIndex].cnr_x1000;
					return  (SONY_RESULT_OK);
				}
			}
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}
	/* Error */
	return  (SONY_RESULT_ERROR_OTHER);
}

static sony_result_t s2_monitor_PER (struct cxd2841er_priv *priv,
		uint32_t * pPER)
{
	uint8_t data[3];
	uint32_t packetError = 0;
	uint32_t packetCount = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if ((!priv) || (!pPER)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xB8 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xB8);
	/*  slave     Bank      Addr      Bit      Signal name
	 * --------------------------------------------------------------
	 * <SLV-T>    B8h       26h       [0]      IPERVALID
	 * <SLV-T>    B8h       27h       [7:0]    IPERPKTERR[15:8]
	 * <SLV-T>    B8h       28h       [7:0]    IPERPKTERR[7:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x26, data, 3);

	if (data[0] & 0x01){
		packetError = ((uint32_t)(data[1] & 0xFF) << 8) |
			(uint32_t)(data[2] & 0xFF);

		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/*  slave     Bank    Addr    Bit     Default    Setting       Signal name
		 * ----------------------------------------------------------------------------------
		 * <SLV-T>    A0h     BAh     [3:0]    8'h08      period       OFBER_MES[3:0]
		 */
		cxd2841er_read_regs(priv, I2C_SLVT, 0xBA, data, 1);

		packetCount = (uint32_t)(1 << (data[0] & 0x0F));

		/*--------------------------------------------------------------------
		  PER = packetError / packetCount
		  = (packetError * 10^6) / packetCount
		  = ((packetError * 10000) * 100) / packetCount
		  --------------------------------------------------------------------*/
		tempDiv = packetCount;
		if ((tempDiv == 0) || (packetError > packetCount)){
			return (SONY_RESULT_ERROR_HW_STATE);
		}

		tempQ = (packetError * 10000) / tempDiv;
		tempR = (packetError * 10000) % tempDiv;

		tempR *= 100;
		tempQ = (tempQ * 100) + (tempR / tempDiv);
		tempR = tempR % tempDiv;

		if ((tempDiv != 1) && (tempR >= (tempDiv/2))){
			*pPER = tempQ + 1;
		} else {
			*pPER = tempQ;
		}
	} else {
		return  (SONY_RESULT_ERROR_HW_STATE);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs2_monitor_Rolloff (struct cxd2841er_priv *priv,
		uint8_t * pRolloff)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t data=0x00;

	if ((!priv) || (!pRolloff)){
		return  (SONY_RESULT_ERROR_ARG);
	}
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	cxd2841er_read_regs(priv, I2C_SLVT, 0x49, &data, 1);

	*pRolloff=(data & 0x03);  
	return  (result);
}
/*** monitor end ***/

/*** FS sub seq ***/
static sony_result_t finish_ok (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq);
static sony_result_t updateProgress (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq);
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_bt_Start (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq,
		uint32_t centerFreqKHz,
		uint32_t candSymbolRateKSps,
		uint32_t minCandSymbolRateKSps,
		uint32_t maxCandSymbolRateKSps,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps);

/*----------------------------------------------------------------------------
  Functions
----------------------------------------------------------------------------*/
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_fs_Initialize (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq,
                                                                 sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeqBT,
                                                                 sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams)
{

    if ((!pSeq) || (!pSeqBT) || (!pCommonParams)){
        return  (SONY_RESULT_ERROR_ARG);
    }

    pSeq->isEnable = 0;
    pSeq->state = FS_STATE_START;
    pSeq->pSeqBT = pSeqBT;
    pSeq->pCommonParams = pCommonParams;

    return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_fs_Start (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq,
                                                            sony_demod_dvbs_s2_blindscan_data_t * pCandList,
                                                            uint32_t minSymbolRateKSps,
                                                            uint32_t maxSymbolRateKSps,
                                                            sony_demod_dvbs_s2_blindscan_data_t * pChannelList)
{
    sony_result_t result = SONY_RESULT_OK;
    sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;

    if ((!pSeq) || (!pCandList) || (!pChannelList)){
        return  (SONY_RESULT_ERROR_ARG);
    }

    pSeq->isEnable = 1;
    pSeq->state = FS_STATE_START;
    pSeq->pCandList = pCandList;
    pSeq->pCandCurrent = pSeq->pCandList->pNext;
    pSeq->pChannelList = pChannelList;
    pSeq->minSymbolRateKSps = minSymbolRateKSps;
    pSeq->maxSymbolRateKSps = maxSymbolRateKSps;

    pSeq->candIndex = 1;
    pSeq->candCount = 1;
    pCurrent = pCandList->pNext;
    while(pCurrent){
        pSeq->candCount++;
        pCurrent = pCurrent->pNext;
    }
    result = updateProgress (pSeq);
    if (result != SONY_RESULT_OK){
        return  (result);
    }

    pSeq->pChannelLast = pChannelList;
    while(pSeq->pChannelLast->pNext){
        pSeq->pChannelLast = pSeq->pChannelLast->pNext;
    }

    return  (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_fs_Sequence (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq)
{
    sony_result_t result = SONY_RESULT_OK;
    sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
    sony_demod_dvbs_s2_blindscan_data_t * pChannelCurrent = NULL;
    uint32_t candFreq = 0;
    uint32_t candSR = 0;
    uint32_t candMinSR = 0;
    uint32_t candMaxSR = 0;
    uint32_t channelFreq = 0;
    uint32_t channelSR = 0;
    uint8_t isTry = 0;

    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }

    switch(pSeq->state)
    {
    case FS_STATE_START:
        result = updateProgress (pSeq);
        if (result != SONY_RESULT_OK){
            return  (result);
        }
        if (pSeq->pCandCurrent){
            /* Cand is exist */
            candFreq = pSeq->pCandCurrent->data.candidate.centerFreqKHz;
            candSR = pSeq->pCandCurrent->data.candidate.symbolRateKSps;
            candMinSR = pSeq->pCandCurrent->data.candidate.minSymbolRateKSps;
            candMaxSR = pSeq->pCandCurrent->data.candidate.maxSymbolRateKSps;

            isTry = 1;
            if (candMaxSR > pSeq->minSymbolRateKSps){
                pChannelCurrent = pSeq->pChannelList->pNext;
                while(pChannelCurrent){
                    channelFreq = pChannelCurrent->data.candidate.centerFreqKHz;
                    channelSR = pChannelCurrent->data.candidate.symbolRateKSps;

                    if (((channelFreq - (channelSR/2)) <= candFreq) && (candFreq <= (channelFreq + (channelSR/2)))){
                        /* duplicated */
                        isTry = 0;
                        break;
                    }
                    /* Next channel */
                    pChannelCurrent = pChannelCurrent->pNext;
                }
            } else {
                isTry = 0;
            }

            pSeq->candIndex++;
            pSeq->pCandCurrent = pSeq->pCandCurrent->pNext;

            if ((isTry) && (candMaxSR > 1000)) {
                result = sony_demod_dvbs_s2_blindscan_subseq_bt_Start (pSeq->pSeqBT,
                                                                       candFreq,
                                                                       candSR,
                                                                       candMinSR,
                                                                       candMaxSR,
                                                                       pSeq->minSymbolRateKSps,
                                                                       pSeq->maxSymbolRateKSps);
                if (result != SONY_RESULT_OK){
                    return  (result);
                }
                pSeq->state = FS_STATE_WAIT_BTFIN;
                pSeq->pCommonParams->waitTime =0;
            }
        } else {
            /* Finish */
            result = finish_ok(pSeq);
        }
        break;

    case FS_STATE_WAIT_BTFIN:
        /* Check result */
        if (pSeq->pSeqBT->isLocked){
            uint8_t isExist = 0;
            sony_demod_dvbs_s2_blindscan_data_t * pCurrent = pSeq->pChannelList->pNext;
            uint32_t detFrequencyKHz = (uint32_t)((int32_t)(pSeq->pCommonParams->tuneReq.frequencyKHz) + pSeq->pSeqBT->detCarrierOffsetKHz);
            uint32_t detSymbolRateKSps = (pSeq->pSeqBT->detSymbolRateSps + 500) / 1000;
            uint32_t chFrequencyKHz = 0;
            uint32_t chSymbolRateKSps = 0;

            isExist = 0;
            while(pCurrent){

                chFrequencyKHz = pCurrent->data.channelInfo.centerFreqKHz;
                chSymbolRateKSps = pCurrent->data.channelInfo.symbolRateKSps/2;

                if ((detFrequencyKHz >= (chFrequencyKHz - chSymbolRateKSps)) &&
                    (detFrequencyKHz <= (chFrequencyKHz + chSymbolRateKSps))){
                    /* It is already detected channel */
                    isExist = 1;
                    break;
                }
                pCurrent = pCurrent->pNext;
            }

            if (isExist == 0){

                /* Detect signal */
                pSeq->pCommonParams->detInfo.isDetect = 1;
                pSeq->pCommonParams->detInfo.centerFreqKHz = detFrequencyKHz;
                pSeq->pCommonParams->detInfo.symbolRateKSps = detSymbolRateKSps;
                pSeq->pCommonParams->detInfo.system = pSeq->pSeqBT->detSystem;

                result = sony_demod_dvbs_s2_blindscan_AllocData (&(pSeq->pCommonParams->storage), &pTemp); 
                if (result != SONY_RESULT_OK){
                    return  (result);
                }

                pTemp->data.channelInfo.centerFreqKHz = pSeq->pCommonParams->detInfo.centerFreqKHz;
                pTemp->data.channelInfo.symbolRateKSps = pSeq->pCommonParams->detInfo.symbolRateKSps;
                pTemp->data.channelInfo.system = pSeq->pCommonParams->detInfo.system;

                pSeq->pChannelLast->pNext = pTemp;
                pSeq->pChannelLast = pSeq->pChannelLast->pNext;
            }
        } else {
            /* Unlock */
            pSeq->pCommonParams->detInfo.isDetect = 0;
        }
        pSeq->state = FS_STATE_START;
        pSeq->pCommonParams->waitTime = 0;
        break;

    default:
        return  (SONY_RESULT_ERROR_SW_STATE);
    }

    return  (result);
}

static sony_result_t finish_ok (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq)
{
    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }
    pSeq->isEnable = 0;
    return  (SONY_RESULT_OK);
}

static sony_result_t updateProgress (sony_demod_dvbs_s2_blindscan_subseq_fs_t * pSeq)
{
    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }
    if (pSeq->candIndex > pSeq->candCount){
        pSeq->candIndex = pSeq->candCount;
    }
    pSeq->pCommonParams->progressInfo.minorProgress = (uint8_t)((pSeq->candIndex * 100) / pSeq->candCount);
    return  (SONY_RESULT_OK);
}
/*** FS sub seq end ***/

/*** BT sub seq ***/
sony_result_t sony_demod_dvbs_s2_blindscan_SetSymbolRateRatio (struct cxd2841er_priv *priv,
		uint32_t ratioMin,
		uint32_t ratioMax)
{   
	uint8_t data[4];

	if (!priv){
		return (SONY_RESULT_ERROR_ARG);
	}

	if ((ratioMax > 0x03FF) || (ratioMin > 0x3FF)){
		return (SONY_RESULT_ERROR_ARG);
	}

	data[0] = (uint8_t)((ratioMax >> 8) & 0x03);
	data[1] = (uint8_t)( ratioMax       & 0xFF);
	data[2] = (uint8_t)((ratioMin >> 8) & 0x03);
	data[3] = (uint8_t)( ratioMin       & 0xFF);

	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAE);
	/* OFSM_RATIOMAX_ARS[9:0]
	 * OFSM_RATIOMIN_ARS[9:0]
	 */
	cxd2841er_write_regs(priv, I2C_SLVT, 0x28, data, 4);

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_SoftReset (struct cxd2841er_priv *priv)
{   
    sony_result_t result = SONY_RESULT_OK;

    if (!priv) {
        return (SONY_RESULT_ERROR_ARG);
    }

    /* Set SLV-T Bank : 0x00 */
    cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
    /* SW Reset */
    cxd2841er_write_reg(priv, I2C_SLVT, 0xFE, 0x01);

    return (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_GetSRSFIN (struct cxd2841er_priv *priv,
		uint8_t * pSRSFIN)
{   
	uint8_t data = 0;

	if ((!priv) || (!pSRSFIN)){
		return (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAE);
	/* IFSM_SRSFIN_ARS[0] */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, &data, 1);

	if (data & 0x01){
		*pSRSFIN = 1;
	} else {
		*pSRSFIN = 0;
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_CheckTRLLock (struct cxd2841er_priv *priv,
		uint8_t * pIsTRLLock)
{   
	uint8_t data = 0;

	if ((!priv) || (!pIsTRLLock)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* ITRL_LOCK[0] */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x10, &data, 1);

	if (data & 0x01){
		*pIsTRLLock = 1;
	} else {
		*pIsTRLLock = 0;
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t finish_ok_bt (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq);
static sony_result_t finish_ng (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq);

/*----------------------------------------------------------------------------
  Functions
  ----------------------------------------------------------------------------*/
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_bt_Initialize (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq,
		sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams)
{
	sony_result_t result = SONY_RESULT_OK;

	if ((!pSeq) || (!pCommonParams)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	pSeq->isEnable = 0;
	pSeq->state = BT_STATE_INIT;
	pSeq->pCommonParams = pCommonParams;

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_bt_Start (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq,
		uint32_t centerFreqKHz,
		uint32_t candSymbolRateKSps,
		uint32_t minCandSymbolRateKSps,
		uint32_t maxCandSymbolRateKSps,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps)
{
	sony_result_t result = SONY_RESULT_OK;

	if (!pSeq){
		return  (SONY_RESULT_ERROR_ARG);
	}

	pSeq->isEnable = 1;
	pSeq->state = BT_STATE_START;
	pSeq->centerFreqKHz = centerFreqKHz;
	pSeq->candSymbolRateKSps = candSymbolRateKSps;
	pSeq->minCandSymbolRateKSps = minCandSymbolRateKSps;
	pSeq->maxCandSymbolRateKSps = maxCandSymbolRateKSps;
	pSeq->minSymbolRateKSps = minSymbolRateKSps;
	pSeq->maxSymbolRateKSps = maxSymbolRateKSps;
	pSeq->isLocked = 0;

	pSeq->pCommonParams->waitTime = 0;


	return  (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_bt_Sequence (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t srsfin = 0;
	uint8_t trllock = 0;
	uint8_t tslock = 0;
	uint32_t elapsedTime = 0;
	uint32_t detSymbolRateSps = 0;
	uint32_t detSymbolRateKSps = 0;
	uint32_t ratioMax = 0;
	uint32_t ratioMin = 0;

	if(!pSeq) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(pSeq->state)
	{
		case BT_STATE_START:

			if (pSeq->maxCandSymbolRateKSps < pSeq->minSymbolRateKSps){
				result = finish_ng (pSeq);
				return  (result);
			}

			if (pSeq->maxCandSymbolRateKSps >= 20000){
				/* HS mode */
				result = sony_demod_dvbs_s2_blindscan_SetSampleMode (pSeq->pCommonParams->priv, 1);
				if (result != SONY_RESULT_OK){
					return  (result);
				}
			} else {
				/* LS mode */
				result = sony_demod_dvbs_s2_blindscan_SetSampleMode (pSeq->pCommonParams->priv, 0);
				if (result != SONY_RESULT_OK){
					return  (result);
				}
			}

			/* Clip 1 */
			if (pSeq->candSymbolRateKSps < pSeq->minSymbolRateKSps) {
				pSeq->candSymbolRateKSps = pSeq->minSymbolRateKSps;
			}
			if (pSeq->candSymbolRateKSps > pSeq->maxSymbolRateKSps){
				pSeq->candSymbolRateKSps = pSeq->maxSymbolRateKSps;
			}
			if (pSeq->minCandSymbolRateKSps < pSeq->minSymbolRateKSps) {
				pSeq->minCandSymbolRateKSps = pSeq->minSymbolRateKSps;
			}
			if (pSeq->minCandSymbolRateKSps > pSeq->maxSymbolRateKSps){
				pSeq->minCandSymbolRateKSps = pSeq->maxSymbolRateKSps;
			}
			if (pSeq->maxCandSymbolRateKSps < pSeq->minSymbolRateKSps) {
				pSeq->maxCandSymbolRateKSps = pSeq->minSymbolRateKSps;
			}
			if (pSeq->maxCandSymbolRateKSps > pSeq->maxSymbolRateKSps){
				pSeq->maxCandSymbolRateKSps = pSeq->maxSymbolRateKSps;
			}

			/* Clip 2 */
			if (pSeq->maxCandSymbolRateKSps < pSeq->candSymbolRateKSps){
				pSeq->maxCandSymbolRateKSps = pSeq->candSymbolRateKSps;
			}
			if (pSeq->maxCandSymbolRateKSps > (pSeq->candSymbolRateKSps * 2)){
				pSeq->maxCandSymbolRateKSps = (pSeq->candSymbolRateKSps * 2);
			}
			if (pSeq->minCandSymbolRateKSps > pSeq->candSymbolRateKSps){
				pSeq->minCandSymbolRateKSps = pSeq->candSymbolRateKSps;
			}

			/* Set target symbol rate */
			result = cxd2841er_dvbs2_set_symbol_rate(pSeq->pCommonParams->priv, pSeq->candSymbolRateKSps);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			if(pSeq->candSymbolRateKSps == 0){
				return  (SONY_RESULT_ERROR_ARG);
			}

			ratioMax = ((pSeq->maxCandSymbolRateKSps * 1024) / (pSeq->candSymbolRateKSps)) - 1024;
			ratioMin = ((pSeq->minCandSymbolRateKSps * 1024) / (pSeq->candSymbolRateKSps));

			if (ratioMin >= 1024){
				ratioMin = 1023;
			}

			result = sony_demod_dvbs_s2_blindscan_SetSymbolRateRatio (pSeq->pCommonParams->priv, ratioMin, ratioMax);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			/* Tune */
			pSeq->pCommonParams->tuneReq.isRequest = 1;
			pSeq->pCommonParams->tuneReq.frequencyKHz = pSeq->centerFreqKHz;
			pSeq->pCommonParams->tuneReq.system = SONY_DTV_SYSTEM_DVBS;
			pSeq->pCommonParams->tuneReq.symbolRateKSps = pSeq->maxCandSymbolRateKSps;
			pSeq->pCommonParams->waitTime = 0;
			pSeq->state = BT_STATE_RF_TUNED;
			break;

		case BT_STATE_RF_TUNED:
			result = sony_demod_SoftReset (pSeq->pCommonParams->priv);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			result = sony_stopwatch_start (&(pSeq->stopwatch));
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			pSeq->pCommonParams->waitTime = 10;
			pSeq->state = BT_STATE_WAIT_SRSFIN;
			break;

		case BT_STATE_WAIT_SRSFIN:
			result = sony_stopwatch_elapsed (&(pSeq->stopwatch), &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			result = sony_demod_dvbs_s2_blindscan_GetSRSFIN (pSeq->pCommonParams->priv, &srsfin);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (srsfin){
				/* Check TRL lock */
				result = sony_demod_dvbs_s2_blindscan_CheckTRLLock (pSeq->pCommonParams->priv, &trllock);
				if (result != SONY_RESULT_OK){
					return  (result);
				}
				if (trllock){
					/* Symbol rate information */
					result = sony_demod_dvbs_s2_monitor_SymbolRate (pSeq->pCommonParams->priv, &detSymbolRateSps);
					switch(result)
					{
						case SONY_RESULT_OK:
							/* Set symbol rate */
							pSeq->detSymbolRateSps = detSymbolRateSps;
							break;

						case SONY_RESULT_ERROR_HW_STATE:
							/* Not detect */
							result = finish_ng (pSeq);
							return  (result);

						default:
							return  (result);
					}
					detSymbolRateKSps = (detSymbolRateSps + 500) / 1000;
					pSeq->timeout = ((3600000 + (detSymbolRateKSps - 1)) / detSymbolRateKSps) + 150;
					result = sony_stopwatch_start (&(pSeq->stopwatch));
					if (result != SONY_RESULT_OK){
						return  (result);
					}
					pSeq->state = BT_STATE_WAIT_TSLOCK;
					pSeq->pCommonParams->waitTime = 10;
				} else {
					/* TRL unlock */
					result = finish_ng (pSeq);
				}
			} else {
				if (elapsedTime > 10000){
					/* Timeout */
					result = finish_ng (pSeq);
				} else {
					/* Continue */
					pSeq->pCommonParams->waitTime = 10;
					pSeq->state = BT_STATE_WAIT_SRSFIN;
				}
			}
			break;

		case BT_STATE_WAIT_TSLOCK:
			result = sony_stopwatch_elapsed (&(pSeq->stopwatch), &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			/* Monitor TSLock, CarrierOffset, System */
			result = sony_demod_dvbs_s2_monitor_ScanInfo (pSeq->pCommonParams->priv, 
					&tslock,
					&(pSeq->detCarrierOffsetKHz),
					&(pSeq->detSystem));
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			if (tslock){
				/* Store system information */
				pSeq->pCommonParams->priv->system_sony = pSeq->detSystem;

				/* Success */
				result = finish_ok_bt (pSeq);
				return  (result);
			} else {
				if (elapsedTime > pSeq->timeout){
					uint8_t plscLock = 0;
					uint8_t pilotOn = 0;
					/* Not detected channel */
					result = sony_demod_dvbs_s2_monitor_Pilot (pSeq->pCommonParams->priv, &plscLock, &pilotOn);
					if (result != SONY_RESULT_OK){
						return  (result);
					}
					if ((plscLock != 0) && (pilotOn == 0)) {
						/* Calculate timeout time for Pilot off signal */
						detSymbolRateKSps = (pSeq->detSymbolRateSps + 500) / 1000;
						pSeq->timeout = ((3600000 + (detSymbolRateKSps - 1)) / detSymbolRateKSps) + 150;
						/* Restart timer */
						result = sony_stopwatch_start (&(pSeq->stopwatch));
						if (result != SONY_RESULT_OK){
							return  (result);
						}
						pSeq->state = BT_STATE_WAIT_TSLOCK2;
						pSeq->pCommonParams->waitTime = 10;
					} else {
						result = finish_ng (pSeq);
						return  (result);
					}
				} else {
					/* Continue to wait */
				}
			}
			break;

		case BT_STATE_WAIT_TSLOCK2:
			result = sony_stopwatch_elapsed (&(pSeq->stopwatch), &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			/* Monitor TSLock, CarrierOffset, System */
			result = sony_demod_dvbs_s2_monitor_ScanInfo (pSeq->pCommonParams->priv, 
					&tslock,
					&(pSeq->detCarrierOffsetKHz),
					&(pSeq->detSystem));
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (tslock){
				/* Store system information */
				pSeq->pCommonParams->priv->system_sony = pSeq->detSystem;

				/* Success */
				result = finish_ok_bt (pSeq);
				return  (result);
			} else {
				if (elapsedTime > pSeq->timeout){
					result = finish_ng (pSeq);
					return  (result);
				} else {
					/* Continue to wait */
				}
			}
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	return  (result);
}

static sony_result_t finish_ok_bt (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq)
{
	if(!pSeq) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	pSeq->isLocked = 1;
	pSeq->isEnable = 0;
	return SONY_RESULT_OK;
}

static sony_result_t finish_ng (sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeq)
{
	if(!pSeq) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	pSeq->isLocked = 0;
	pSeq->isEnable = 0;
	return SONY_RESULT_OK;
}
/*** BT sub seq end ***/

/*** CS sub seq ***/
sony_result_t sony_demod_dvbs_s2_blindscan_CS_INIT (struct cxd2841er_priv *priv)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t iqInv = 0;

	if (!priv){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANON[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x56, 0x01);
	/* OCFRL_CSK[2:0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x57, 0x01);

	iqInv = 0; // TODO
	if (iqInv){
		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/* OTUIF_POLAR[0] */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xD7, 0x01, 0x01);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_CS_FIN (struct cxd2841er_priv *priv)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t iqInv = 0;

	if (!priv){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANON[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x56, 0x00);
	/* OCFRL_CSK[2:0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x57, 0x02);

	iqInv = 0; // TODO
	if (iqInv){
		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/* OTUIF_POLAR[0] */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xD7, 0x00, 0x01);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t get_step_cs (int32_t stepCount, int32_t * pValue);
static sony_result_t finish_ok_cs (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq);
static sony_result_t finish_ng_cs (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq);

/*----------------------------------------------------------------------------
  Functions
----------------------------------------------------------------------------*/
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_cs_Initialize (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq,
                                                                 sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeqPM,
                                                                 sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams)
{
    sony_result_t result = SONY_RESULT_OK;

    if ((!pSeq) || (!pSeqPM) || (!pCommonParams)){
        return  (SONY_RESULT_ERROR_ARG);
    }

    pSeq->isEnable = 0;
    pSeq->state = CS_STATE_START;
    pSeq->pSeqPM = pSeqPM;
    pSeq->pCommonParams = pCommonParams;
    pSeq->freqOffsetKHz = 0;

    return  (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_cs_Start (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq,
                                                            int32_t freqOffsetKHz)
{
    sony_result_t result = SONY_RESULT_OK;

    if (!pSeq){
        return  (SONY_RESULT_ERROR_ARG);
    }


    pSeq->isEnable = 1;
    pSeq->state = CS_STATE_START;
    pSeq->freqOffsetKHz = freqOffsetKHz;
    pSeq->isExist = 0;
    pSeq->coarseSymbolRateKSps = 0;

    return  (result);
}

#define LOVAL 137
#define HIVAL 150

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_cs_Sequence(sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq)
{
    sony_result_t result = SONY_RESULT_OK;
    int32_t power = 0;
    int32_t stepFreqKHz = 0;

    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }

    switch(pSeq->state)
    {
    case CS_STATE_START:
        result = sony_demod_dvbs_s2_blindscan_CS_INIT (pSeq->pCommonParams->priv);
        if (result != SONY_RESULT_OK){
            return  (result);
        }
        pSeq->pCommonParams->waitTime = 0;
        pSeq->state = CS_STATE_PEAK_SEARCH_START;
        /* Set init value for index */
        pSeq->index = -5;
        break;

    case CS_STATE_PEAK_SEARCH_START:
        result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->freqOffsetKHz + ((int32_t)(pSeq->index) * 100));
        if (result != SONY_RESULT_OK){
            return  (result);
        }
        pSeq->state = CS_STATE_PEAK_SEARCHING;
        break;

    case CS_STATE_PEAK_SEARCHING:
        /* Get result */
        power = (int32_t)(pSeq->pSeqPM->power);
        if ((pSeq->index == -5) || (pSeq->peakPower < power)){
            pSeq->peakPower = power;
            pSeq->peakFreqOfsKHz = (pSeq->index) * 100;
        }
        pSeq->index++;
        if(pSeq->index <= 5){
            /* Continue peak search */
            result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->freqOffsetKHz + (pSeq->index * 100));
            if (result != SONY_RESULT_OK){
                return  (result);
            }
        } else {
            /* Next stage */
            pSeq->state = CS_STATE_LOWER_SEARCHING;
            pSeq->index = 1;
            pSeq->isFin = 0;
            result = get_step_cs (pSeq->index, &stepFreqKHz);
            if (result != SONY_RESULT_OK){
                return  (result);
            }
            pSeq->lowerFreqKHz = pSeq->freqOffsetKHz + (pSeq->peakFreqOfsKHz - stepFreqKHz);
            result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->lowerFreqKHz);
            if (result != SONY_RESULT_OK){
                return  (result);
            }
        }
        break;

    case CS_STATE_LOWER_SEARCHING:
        power = (int32_t)(pSeq->pSeqPM->power);
        if ((pSeq->peakPower * 100) > (power * LOVAL)){
            /* Next stage */
            pSeq->state = CS_STATE_UPPER_SEARCHING;
            pSeq->index = 1;
            result = get_step_cs (pSeq->index, &stepFreqKHz);
            if (result != SONY_RESULT_OK){
                return  (result);
            }
            pSeq->upperFreqKHz = pSeq->freqOffsetKHz + (pSeq->peakFreqOfsKHz + stepFreqKHz);
            result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->upperFreqKHz);
            if (result != SONY_RESULT_OK){
                return  (result);
            }
        } else if ((power * 100) > (pSeq->peakPower * HIVAL)){
            /* NG */
            pSeq->lowerFreqKHz = -1000; /* Invalid */
            result = finish_ng_cs (pSeq);
        } else {
            pSeq->index++;
            if (pSeq->index < 27){
                /* Continue */
                result = get_step_cs (pSeq->index, &stepFreqKHz);
                if (result != SONY_RESULT_OK){
                    return  (result);
                }
                pSeq->lowerFreqKHz = pSeq->freqOffsetKHz + (pSeq->peakFreqOfsKHz - stepFreqKHz);
                result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->lowerFreqKHz);
                if (result != SONY_RESULT_OK){
                    return  (result);
                }
            } else {
                /* NG */
                pSeq->lowerFreqKHz = -1000; /* Invalid */
                result = finish_ng_cs (pSeq);
            }
        }
        break;

    case CS_STATE_UPPER_SEARCHING:
        power = (int32_t)(pSeq->pSeqPM->power);
        if ((pSeq->peakPower * 100) > (power * LOVAL)){
            /* OK */
            result = finish_ok_cs (pSeq);
        } else if ((power * 100) > (pSeq->peakPower * HIVAL)){
            /* NG */
            pSeq->upperFreqKHz = -1000; /* Invalid */
            result = finish_ng_cs (pSeq);
        } else {
            pSeq->index++;
            if (pSeq->index < 27){
                /* Continue */
                result = get_step_cs (pSeq->index, &stepFreqKHz);
                if (result != SONY_RESULT_OK){
                    return  (result);
                }
                pSeq->upperFreqKHz = pSeq->freqOffsetKHz + (pSeq->peakFreqOfsKHz + stepFreqKHz);
                result = sony_demod_dvbs_s2_blindscan_subseq_pm_Start (pSeq->pSeqPM, pSeq->upperFreqKHz);
                if (result != SONY_RESULT_OK){
                    return  (result);
                }
            } else {
                /* NG */
                pSeq->upperFreqKHz = -1000; /* Invalid */
                result = finish_ng_cs (pSeq);
            }
        }
        break;

    default:
        return  (SONY_RESULT_ERROR_SW_STATE);
    }

    return  (result);
}

/*----------------------------------------------------------------------------
  Static Functions
----------------------------------------------------------------------------*/
static sony_result_t get_step_cs (int32_t stepCount, int32_t * pValue)
{
    static const int32_t step_table[] = {
        0,     /* 100(KHz) * 0         */
        100,   /* 100(KHz) * 1         */
        200,   /* 100(KHz) * 2         */
        300,   /* 100(KHz) * 3         */
        400,   /* 400(KHz) * (1.1^0)   */
        440,   /* 400(KHz) * (1.1^1)   */
        484,   /* 400(KHz) * (1.1^2)   */
        532,   /* 400(KHz) * (1.1^3)   */
        586,   /* 400(KHz) * (1.1^4)   */
        644,   /* 400(KHz) * (1.1^5)   */
        709,   /* 400(KHz) * (1.1^6)   */
        779,   /* 400(KHz) * (1.1^7)   */
        857,   /* 400(KHz) * (1.1^8)   */
        943,   /* 400(KHz) * (1.1^9)   */
        1037,  /* 400(KHz) * (1.1^10)  */
        1141,  /* 400(KHz) * (1.1^11)  */
        1255,  /* 400(KHz) * (1.1^12)  */
        1381,  /* 400(KHz) * (1.1^13)  */
        1519,  /* 400(KHz) * (1.1^14)  */
        1671,  /* 400(KHz) * (1.1^15)  */
        1838,  /* 400(KHz) * (1.1^16)  */
        2022,  /* 400(KHz) * (1.1^17)  */
        2224,  /* 400(KHz) * (1.1^18)  */
        2446,  /* 400(KHz) * (1.1^19)  */
        2691,  /* 400(KHz) * (1.1^20)  */
        2960,  /* 400(KHz) * (1.1^21)  */
        3256,  /* 400(KHz) * (1.1^22)  */
        3582   /* 400(KHz) * (1.1^23)  */
    };


    if ((!pValue) || (stepCount < 0) || (stepCount > 27)){
        return  (SONY_RESULT_ERROR_ARG);
    }

    *pValue = step_table[stepCount];

    return  (SONY_RESULT_OK);
}

static sony_result_t finish_ok_cs (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq)
{
    sony_result_t result = SONY_RESULT_OK;
    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }
    if (pSeq->upperFreqKHz > pSeq->lowerFreqKHz){
        pSeq->coarseSymbolRateKSps = (uint32_t)(pSeq->upperFreqKHz - pSeq->lowerFreqKHz);
        pSeq->isExist = 1;
    } else {
        pSeq->coarseSymbolRateKSps = 0;
        pSeq->isExist = 0;
    }
    pSeq->isEnable = 0;
    result = sony_demod_dvbs_s2_blindscan_CS_FIN (pSeq->pCommonParams->priv);
    return  (result);
}

static sony_result_t finish_ng_cs (sony_demod_dvbs_s2_blindscan_subseq_cs_t * pSeq)
{
    sony_result_t result = SONY_RESULT_OK;
    if(!pSeq) {
        return  (SONY_RESULT_ERROR_ARG);
    }
    pSeq->coarseSymbolRateKSps = 0;
    pSeq->isExist = 0;
    pSeq->isEnable = 0;
    result = sony_demod_dvbs_s2_blindscan_CS_FIN (pSeq->pCommonParams->priv);
    return  (result);
}
/*** CS sub seq end ***/

/*** PM sub seq ***/
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_pm_Initialize (sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeq,
		sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams)
{

	if ((!pSeq) || (!pCommonParams)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	pSeq->isEnable = 0;
	pSeq->state = PM_STATE_START;
	pSeq->pCommonParams = pCommonParams;

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_pm_Start (sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeq,
		int32_t freqOffsetKHz)
{
	sony_result_t result = SONY_RESULT_OK;
	if(!pSeq) {
		return  (SONY_RESULT_ERROR_ARG);
	}
	pSeq->isEnable = 1;
	pSeq->state = PM_STATE_START;
	pSeq->freqOffsetKHz = freqOffsetKHz;
	pSeq->power = 0;

	return  (result);
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_pm_Sequence (sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeq)
{
	sony_result_t result = SONY_RESULT_OK;
	uint32_t elapsedTime = 0;
	uint8_t csfin = 0;

	if(!pSeq) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	switch(pSeq->state)
	{
		case PM_STATE_START:
			result = sony_demod_dvbs_s2_blindscan_SetCFFine (pSeq->pCommonParams->priv, pSeq->freqOffsetKHz);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			/* Scan start */
			result = sony_demod_dvbs_s2_blindscan_PS_START (pSeq->pCommonParams->priv);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			pSeq->state = PM_STATE_WAITING_CSFIN;
			pSeq->pCommonParams->waitTime = 0;
			result = sony_stopwatch_start (&pSeq->stopwatch);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			break;

		case PM_STATE_WAITING_CSFIN:
			result = sony_stopwatch_elapsed (&pSeq->stopwatch, &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			result = sony_demod_dvbs_s2_blindscan_GetCSFIN (pSeq->pCommonParams->priv, &csfin);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			if (csfin){
				result = sony_demod_dvbs_s2_blindscan_GetPSPow (pSeq->pCommonParams->priv, &pSeq->power);
				if (result != SONY_RESULT_OK){
					return  (result);
				}

				/* Scan ack */
				result = sony_demod_dvbs_s2_blindscan_PS_RACK (pSeq->pCommonParams->priv);
				if (result != SONY_RESULT_OK){
					return  (result);
				}

				/* End */
				pSeq->isEnable = 0;
			} else {
				if (elapsedTime >= 1000){
					return  (SONY_RESULT_ERROR_TIMEOUT);
				} else {
					/* Continue to wait */
				}
			}
			break;

		default:
			return  (SONY_RESULT_ERROR_SW_STATE);
	}

	return  (result);
}
/*** PM sub seq end ***/

/*** stopwatch ***/
sony_result_t sony_stopwatch_start (sony_stopwatch_t * pStopwatch)
{   
	if (!pStopwatch) {
		return SONY_RESULT_ERROR_ARG;
	}
	pStopwatch->startTime = getus();

	return SONY_RESULT_OK;
}

sony_result_t sony_stopwatch_elapsed (sony_stopwatch_t * pStopwatch, uint32_t* pElapsed)
{   
	if (!pStopwatch || !pElapsed) {
		return SONY_RESULT_ERROR_ARG;
	}
	*pElapsed = (uint32_t)(getus() - pStopwatch->startTime)/1000;

	return SONY_RESULT_OK;
}

sony_result_t sony_stopwatch_sleep (sony_stopwatch_t * pStopwatch, uint32_t ms)
{
	msleep (ms);

	return (SONY_RESULT_OK);
}
/*** stopwatch end ***/

/*** PS ***/
sony_result_t sony_demod_dvbs_s2_blindscan_SetSampleMode (struct cxd2841er_priv *priv,
		uint8_t isHSMode)
{   
	uint8_t data = 0;

	if (!priv){
		return SONY_RESULT_ERROR_ARG;
	}

	if (isHSMode){
		data = 0x01;
	} else {
		data = 0x00;
	}

	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* OREG_ARC_HSMODE[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x24, data);

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_PS_INIT (struct cxd2841er_priv *priv, uint8_t ocfr_csk)
{
	uint8_t iqInv = 0;
	sony_result_t result = SONY_RESULT_OK;

	if (!priv)
		return SONY_RESULT_ERROR_SW_STATE;

	result = sony_demod_dvbs_s2_blindscan_SetSampleMode (priv, 1);
	if (result != SONY_RESULT_OK){
		return result;
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANON[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x56, 0x01);
	/* OCFRL_CSK[2:0] (0x01 in normal case) */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x57, (ocfr_csk & 0x07));
	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/* OCFRL_CSBUFOFF[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xF9, 0x00);

	// TODO: get iqInv
	iqInv = 0;

	if (iqInv){
		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/* OTUIF_POLAR[0] */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xD7, 0x01, 0x01);
	}

	return result;
}

sony_result_t sony_demod_dvbs_s2_blindscan_PS_SET (struct cxd2841er_priv *priv,
		int32_t lowerOffsetKHz,
		int32_t upperOffsetKHz,
		uint32_t stepKHz,
		uint32_t ckaFreqKHz)
{
	uint8_t data[6];
	int32_t startFreqKHz = 0;
	int32_t endFreqKHz = 0;
	uint32_t stepFreqKHz = 0;

	if (!priv)
		return SONY_RESULT_ERROR_ARG;

	if (ckaFreqKHz == 0)
		return SONY_RESULT_ERROR_ARG;

	startFreqKHz =  (lowerOffsetKHz * 2048)                              / (int32_t)ckaFreqKHz; /* Floor */
	endFreqKHz   = ((upperOffsetKHz * 2048) + ((int32_t)ckaFreqKHz - 1)) / (int32_t)ckaFreqKHz; /* Ceil  */
	stepFreqKHz  = ((stepKHz        * 2048) + (         ckaFreqKHz - 1)) /          ckaFreqKHz; /* Ceil  */

	data[0] = (uint8_t)(((uint32_t)startFreqKHz >> 8) & 0x07);
	data[1] = (uint8_t)(((uint32_t)startFreqKHz     ) & 0xFF);
	data[2] = (uint8_t)(((uint32_t)endFreqKHz   >> 8) & 0x07);
	data[3] = (uint8_t)(((uint32_t)endFreqKHz       ) & 0xFF);
	data[4] = (uint8_t)((          stepFreqKHz  >> 8) & 0x03);
	data[5] = (uint8_t)((          stepFreqKHz      ) & 0xFF);

	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/* OCFRL_FSTART_CS[10:0]
	 * OCFRL_FEND_CS[10:0]
	 * OCFRL_FSTEP_CS[9:0]
	 */
	cxd2841er_write_regs(priv, I2C_SLVT, 0xF1, data, 6);

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_PS_START (struct cxd2841er_priv *priv)
{
	if (!priv)
		return SONY_RESULT_ERROR_ARG;

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANST[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x54, 0x01);

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_PS_FIN (struct cxd2841er_priv *priv)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t iqInv = 0;

	if (!priv){
		return SONY_RESULT_ERROR_ARG;
	}

	result = sony_demod_dvbs_s2_blindscan_SetSampleMode (priv, 0);
	if (result != SONY_RESULT_OK){
		return result;
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANON[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x56, 0x00);
	/* OCFRL_CSK[2:0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x57, 0x02);
	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/* OCFRL_CSBUFOFF[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xF9, 0x01);

	iqInv = 0;  //TODO: get actual iqInv
	if (iqInv){
		/* Set SLV-T Bank : 0xA0 */
		cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
		/* OTUIF_POLAR[0] */
		cxd2841er_set_reg_bits(priv, I2C_SLVT, 0xD7, 0x00, 0x01);
	}

	return SONY_RESULT_OK;
}

/*** blindscan ***/
sony_result_t sony_demod_dvbs_s2_blindscan_GetCSFIN (struct cxd2841er_priv *priv,
		uint8_t * pCSFIN)
{
	uint8_t data = 0;

	if ((!priv) || (!pCSFIN)){
		return SONY_RESULT_ERROR_ARG;
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* ICFRL_CSFIN[0] */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x40, &data, 1);

	if (data & 0x01){
		*pCSFIN = 1;
	} else {
		*pCSFIN = 0;
	}

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_GetCSRDEND (struct cxd2841er_priv *priv,
		uint8_t * pCSRDEND)
{   
	uint8_t data = 0;

	if ((!priv) || (!pCSRDEND))
		return SONY_RESULT_ERROR_ARG;

	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/* ICFRL_CSRDEND[0] */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x44, &data, 1);

	if (data & 0x01){
		*pCSRDEND = 1;
	} else {
		*pCSRDEND = 0;
	}

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_GetCSINFO (struct cxd2841er_priv *priv,
		int32_t * pCSFREQ,
		uint32_t * pCSPOW)
{   
	uint8_t data[4];

	if ((!priv) || (!pCSFREQ) || (!pCSPOW)){
		return SONY_RESULT_ERROR_ARG;
	}

	/* Set SLV-T Bank : 0xAB */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAB);
	/* ICFRL_CSFREQ[10:0]
	 * ICFRL_CSPOW[11:0]
	 */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x40, data, 4);

	*pCSFREQ = sony_Convert2SComplement(((uint32_t)(data[0] & 0x07) << 8) | (uint32_t)(data[1] & 0xFF), 11);
	*pCSPOW = ((uint32_t)(data[2] & 0x0F) << 8) | (uint32_t)(data[3] & 0xFF);

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_PS_RACK (struct cxd2841er_priv *priv)
{
	if (!priv)
		return SONY_RESULT_ERROR_ARG;

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OCFRL_CHSCANRACK[0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x55, 0x01);

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_GetPSPow (struct cxd2841er_priv *priv,
		uint16_t * pPower)
{
	uint8_t data[2] = {0, 0};

	if ((!priv) || (!pPower)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* ICFRL_PSPOW[11:0] */
	cxd2841er_read_regs(priv, I2C_SLVT, 0x52, data, 2);

	*pPower = (uint16_t)(((uint32_t)(data[0] & 0x0F) << 8) | (uint32_t)(data[1] & 0xFF));

	return  (SONY_RESULT_OK);
}


sony_result_t sony_demod_dvbs_s2_blindscan_SetCFFine (struct cxd2841er_priv *priv,
                                                      int32_t freqOffsetKHz)
{
    uint8_t data[3];
    uint32_t regvalue = 0;
    uint8_t isNegative = 0;
    uint32_t tempQ = 0;
    uint32_t tempR = 0;
    uint32_t tempDiv = 0;

    if (!priv) {
        return  (SONY_RESULT_ERROR_ARG);
    }

    freqOffsetKHz *= (-1);

    if (freqOffsetKHz < 0){
        isNegative = 1;
        regvalue = (uint32_t)(freqOffsetKHz * (-1));
    } else {
        isNegative = 0;
        regvalue = (uint32_t)freqOffsetKHz;
    }

    /*
     * regvalue = regvalue(21bit) * 2^20 / SONY_DEMOD_DVBS_S2_BLINDSCAN_CKAL
     *          = ((regvalue(21bit) * 2^10) * 2^10) / SONY_DEMOD_DVBS_S2_BLINDSCAN_CKAL
     */
    switch(priv->xtal)
    {
    case SONY_XTAL_41000:
    case SONY_XTAL_20500:
        /* CKAL */
        tempDiv = 64917;
        break;

    case SONY_XTAL_24000:
        /* CKAL */
        tempDiv = 64000;
        break;

    default:
        return (SONY_RESULT_ERROR_SW_STATE);
    }

    tempQ = (regvalue * 1024) / tempDiv;
    tempR = (regvalue * 1024) % tempDiv;

    tempR *= 1024;
    tempQ = (tempQ * 1024) + (tempR / tempDiv);
    tempR = tempR % tempDiv;

    if (tempR >= (tempDiv/2)){
        tempQ = tempQ + 1;
    } else {
        tempQ = tempQ;
    }

    if (isNegative){
        if (tempQ > 0x100000) {
            return  (SONY_RESULT_ERROR_RANGE);
        }
        regvalue = (uint32_t)((int32_t)tempQ * (-1));
    } else {
        if (tempQ > 0xFFFFF) {
            return  (SONY_RESULT_ERROR_RANGE);
        }
        regvalue = tempQ;
    }

    data[0] = (uint8_t)((regvalue >> 16) & 0x1F);
    data[1] = (uint8_t)((regvalue >>  8) & 0xFF);
    data[2] = (uint8_t)( regvalue        & 0xFF);

    /* Set SLV-T Bank : 0xA0 */
    cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
    /* OCFRL_CTRLVALINIT[20:0] */
    cxd2841er_write_regs(priv, I2C_SLVT, 0xCA, data, 3);

    return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_SetTSOut (struct cxd2841er_priv *priv,
		uint8_t enable)
{
	sony_result_t result = SONY_RESULT_OK;

	if (!priv){
		return  (SONY_RESULT_ERROR_ARG);
	}

	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	if (enable){
		/* Enable TS output */
		cxd2841er_write_reg(priv, I2C_SLVT, 0xC3, 0x00);
	} else {
		/* Disable TS output */
		cxd2841er_write_reg(priv, I2C_SLVT, 0xC3, 0x01);
	}

	return  (result);
}
/*** blindscan end ***/


/*** SS sub seq ***/
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_ss_Initialize (sony_demod_dvbs_s2_blindscan_subseq_ss_t * pSeq,
		sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams)
{   
	sony_result_t result = SONY_RESULT_OK;

	if ((!pSeq) || (!pCommonParams)){
		return SONY_RESULT_ERROR_ARG;
	}

	pSeq->isEnable = 0;
	pSeq->state = SS_STATE_INIT;

	pSeq->maxFreqKHz = 0;
	pSeq->minFreqKHz = 0;
	pSeq->stepFreqKHz = 0;
	pSeq->tunerStepFreqKHz = 0;

	pSeq->pCommonParams = pCommonParams;

	result = sony_demod_dvbs_s2_blindscan_AllocPower (&(pSeq->pCommonParams->storage), &(pSeq->pPowerList));
	if (result != SONY_RESULT_OK){
		return result;
	}

	return result;
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_ss_Start (sony_demod_dvbs_s2_blindscan_subseq_ss_t * pSeq,
		uint32_t minFreqKHz,
		uint32_t maxFreqKHz,
		uint32_t stepFreqKHz,
		uint32_t tunerStepFreqKHz,
		uint8_t powerSmooth)
{   
	sony_result_t result = SONY_RESULT_OK;

	if (!pSeq)
		return  (SONY_RESULT_ERROR_ARG);

	pSeq->isEnable = 1;
	pSeq->state = SS_STATE_START;

	pSeq->minFreqKHz = minFreqKHz;
	pSeq->maxFreqKHz = maxFreqKHz;
	pSeq->stepFreqKHz = stepFreqKHz;
	pSeq->tunerStepFreqKHz = tunerStepFreqKHz;

	pSeq->pCommonParams->waitTime = 0;

	pSeq->ocfr_csk = powerSmooth;

	result = sony_demod_dvbs_s2_blindscan_ClearPowerList (&(pSeq->pCommonParams->storage), pSeq->pPowerList);
	if (result != SONY_RESULT_OK){
		return result;
	}
	pSeq->pPowerListLast = pSeq->pPowerList;

	return result;
}

sony_result_t sony_demod_dvbs_s2_blindscan_subseq_ss_Sequence(sony_demod_dvbs_s2_blindscan_subseq_ss_t * pSeq)
{                   
	sony_result_t result = SONY_RESULT_OK;
	uint8_t csfin = 0;
	uint8_t csrdend = 0;
	int32_t csfreq = 0;
	uint32_t cspow = 0;
	uint32_t elapsedTime = 0;
	int32_t offsetFreq = 0;
	uint32_t ckaFreqKHz = 0;
	sony_demod_dvbs_s2_blindscan_power_t * pTempPower = NULL;

	if(!pSeq)
		return SONY_RESULT_ERROR_ARG;

	switch(pSeq->state)
	{
		case SS_STATE_START:
			result = sony_demod_dvbs_s2_blindscan_PS_INIT (pSeq->pCommonParams->priv, pSeq->ocfr_csk);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			offsetFreq = (int32_t)pSeq->tunerStepFreqKHz / 2;
			ckaFreqKHz = pSeq->pCommonParams->ckahFreqKHz;

			result = sony_demod_dvbs_s2_blindscan_PS_SET (pSeq->pCommonParams->priv,
					-offsetFreq,
					offsetFreq,
					pSeq->stepFreqKHz,
					ckaFreqKHz);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			pSeq->currentFreqKHz = (uint32_t)((int32_t)pSeq->minFreqKHz + offsetFreq);
			pSeq->state = SS_STATE_RF_TUNING;
			pSeq->pCommonParams->waitTime = 0;
			break;

		case SS_STATE_RF_TUNING:
			pSeq->pCommonParams->tuneReq.isRequest = 1;
			pSeq->pCommonParams->tuneReq.frequencyKHz = pSeq->currentFreqKHz;
			pSeq->pCommonParams->tuneReq.system = SONY_DTV_SYSTEM_DVBS;
			pSeq->pCommonParams->tuneReq.symbolRateKSps = 0;
			pSeq->pCommonParams->waitTime = 0;
			pSeq->state = SS_STATE_RF_TUNED;
			break;

		case SS_STATE_RF_TUNED:
			result = sony_demod_dvbs_s2_monitor_IFAGCOut (pSeq->pCommonParams->priv, &(pSeq->agcLevel));
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pSeq->pCommonParams->agcInfo.isRequest = 1;
			pSeq->pCommonParams->agcInfo.agcLevel = pSeq->agcLevel;
			pSeq->pCommonParams->waitTime = 0;
			pSeq->state = SS_STATE_AGC_CALCULATED;
			break;

		case SS_STATE_AGC_CALCULATED:
			pSeq->agc_x100dB = pSeq->pCommonParams->agcInfo.agc_x100dB;
			result = sony_demod_dvbs_s2_blindscan_PS_START (pSeq->pCommonParams->priv);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			result = sony_stopwatch_start (&pSeq->stopwatch);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			pSeq->state = SS_STATE_WAIT_CSFIN;
			pSeq->pCommonParams->waitTime = 10;
			break;


		case SS_STATE_WAIT_CSFIN:
			result = sony_stopwatch_elapsed (&pSeq->stopwatch, &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			result = sony_demod_dvbs_s2_blindscan_GetCSFIN (pSeq->pCommonParams->priv, &csfin);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (csfin){
				pSeq->state = SS_STATE_READ_CS;
				pSeq->pCommonParams->waitTime = 0;
			} else {
				if (elapsedTime > 10000){
					/* Timeout error */
					return SONY_RESULT_ERROR_TIMEOUT;
				}
			}
			break;

		case SS_STATE_READ_CS:
			result = sony_demod_dvbs_s2_blindscan_GetCSRDEND(pSeq->pCommonParams->priv, &csrdend);
			if (result != SONY_RESULT_OK){
				return  (result);
			}
			if (csrdend){
				if ((pSeq->currentFreqKHz + (pSeq->tunerStepFreqKHz / 2)) < pSeq->maxFreqKHz){
					pSeq->currentFreqKHz += pSeq->tunerStepFreqKHz;
					pSeq->state = SS_STATE_RF_TUNING;
					pSeq->pCommonParams->waitTime = 0;
					/* Go to next loop */
				} else {
					pSeq->state = SS_STATE_END;
					pSeq->pCommonParams->waitTime = 0;
				}
			} else {
				ckaFreqKHz = pSeq->pCommonParams->ckahFreqKHz;

				result = sony_demod_dvbs_s2_blindscan_GetCSINFO (pSeq->pCommonParams->priv, &csfreq, &cspow);
				if (result != SONY_RESULT_OK){
					return  (result);
				}

				/* Add to power list */
				result = sony_demod_dvbs_s2_blindscan_AllocPower (&(pSeq->pCommonParams->storage), &pTempPower);
				if (result != SONY_RESULT_OK){
					return  (result);
				}

				/* Power data (frequency) */
				if (csfreq >= 0){
					pTempPower->freqKHz = (uint32_t)((int32_t)(pSeq->pCommonParams->tuneReq.frequencyKHz) + (((csfreq * (int32_t)ckaFreqKHz) + (int32_t)1024) / (int32_t)2048));
				} else {
					pTempPower->freqKHz = (uint32_t)((int32_t)(pSeq->pCommonParams->tuneReq.frequencyKHz) + (((csfreq * (int32_t)ckaFreqKHz) - (int32_t)1024) / (int32_t)2048));
				}

				if (pTempPower->freqKHz > pSeq->maxFreqKHz){
					result = sony_demod_dvbs_s2_blindscan_FreePower (&(pSeq->pCommonParams->storage), pTempPower);
					if (result != SONY_RESULT_OK){
						return  (result);
					}
				} else {
					/*
					 * Power[dB]     = 10 * log10 (CSPOW/2^14) - gain_db
					 * Power[x100dB] = (1000 * log10 (CSPOW/2^14)) - gain_db_x100
					 *               = (1000 * (log10(CSPOW) - log10(2^14))) - gain_db_x100
					 *               = (10 * (log10(CSPOW) - 421)) - gain_db_x100
					 *
					 * log10() in this driver returns "result x 100".
					 */
					pTempPower->power = (int32_t)((sony_math_log10 (cspow) - 421) * 10) - pSeq->pCommonParams->agcInfo.agc_x100dB;

					dev_dbg(&pSeq->pCommonParams->priv->i2c->dev,
							"%s(): add PowerList pTempPower=%d freq=%d kHz\n",
							__func__, pTempPower, pTempPower->freqKHz); 
					/* Add data to list */
					pSeq->pPowerListLast->pNext = pTempPower;
					pSeq->pPowerListLast = pSeq->pPowerListLast->pNext;
				}

				result = sony_demod_dvbs_s2_blindscan_PS_RACK (pSeq->pCommonParams->priv);
				if (result != SONY_RESULT_OK){
					return  (result);
				}
			}
			break;
		case SS_STATE_END:
			pSeq->isEnable = 0;
			result = sony_demod_dvbs_s2_blindscan_PS_FIN (pSeq->pCommonParams->priv);
			if (result != SONY_RESULT_OK){
				return result;
			}
			pSeq->pCommonParams->waitTime = 0;
			break;

		default:
			return SONY_RESULT_ERROR_SW_STATE;
	}

	return result;
}

/*** SS sub seq end ***/

/*** Algo ***/
static sony_result_t moving_average (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		int32_t maLength);

static sony_result_t wagiri (struct cxd2841er_priv *priv,
		sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		uint32_t clipStep,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint32_t cferr,
		uint8_t isMpMode,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList);

static sony_result_t delete_duplicated_area (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t cferr);

static sony_result_t get_power_range (sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		int32_t * pPowerMin,
		int32_t * pPowerMax);

static sony_result_t select_candidate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint8_t isMpMode);

static sony_result_t pick_duplicated_area (struct cxd2841er_priv *priv, sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t cferr);

static sony_result_t limit_range (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps);

/*----------------------------------------------------------------------------
  Functions
  ----------------------------------------------------------------------------*/
sony_result_t sony_demod_dvbs_s2_blindscan_algo_GetCandidateMp (struct cxd2841er_priv *priv,
		sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		uint32_t clipStep,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint32_t cferr,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;

	if ((!pStorage) || (!pPowerList) || (!pCandList)){
		return (SONY_RESULT_ERROR_ARG);
	}

	/* wagiri mp */
	result = wagiri (priv, pStorage,
			pPowerList,
			clipStep,
			(minSymbolRateKSps * 65) / 100,
			((maxSymbolRateKSps * 135) + 99) / 100,
			cferr,
			1,
			pCandList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): wagiri failed \n", __func__); 
		return (result);
	}

	result = moving_average (pStorage, pPowerList, 250);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): moving_average failed \n", __func__); 
		return (result);
	}

	/* wagiri mp */
	result = wagiri (priv, pStorage,
			pPowerList,
			clipStep,
			(minSymbolRateKSps * 65) / 100,
			((maxSymbolRateKSps * 135) + 99) / 100,
			cferr,
			1,
			pCandList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): wagiri mp failed \n", __func__); 
		return (result);
	}

	result = delete_duplicated_area (pStorage, pCandList, cferr);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): delete_duplicated_area failed \n", __func__); 
		return (result);
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_GetCandidateNml (struct cxd2841er_priv *priv,
		sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		uint32_t clipStep,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint32_t cferr,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;

	/* wagiri nml */
	result = wagiri (priv, pStorage,
			pPowerList,
			clipStep,
			(minSymbolRateKSps * 65) / 100,
			((maxSymbolRateKSps * 135) + 99) / 100,
			cferr,
			0,
			pCandList);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	result = delete_duplicated_area (pStorage, pCandList, cferr);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	result = limit_range (pStorage,
			pCandList,
			(minSymbolRateKSps * 65) / 100,
			((maxSymbolRateKSps * 135) + 99) / 100);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_SeparateCandidate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCandCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLast = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	int32_t candSymbolRate = 0;
	int32_t candFrequency = 0;
	int32_t symbolRate = 0;
	int32_t frequency = 0;

	if ((!pStorage) || (!pCandList)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	pLast = pList;

	pCandCurrent = pCandList->pNext;
	while(pCandCurrent){
		candFrequency = (int32_t)(pCandCurrent->data.candidate.centerFreqKHz);
		candSymbolRate = (int32_t)(pCandCurrent->data.candidate.maxSymbolRateKSps / 2);

		for (symbolRate = (candSymbolRate * (-1)); symbolRate <= candSymbolRate; symbolRate += 1000){
			frequency = candFrequency + symbolRate;

			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}

			pTemp->data.candidate.centerFreqKHz = (uint32_t)frequency;
			pTemp->data.candidate.symbolRateKSps = 0;
			pTemp->data.candidate.minSymbolRateKSps = 0;
			pTemp->data.candidate.maxSymbolRateKSps = 0;

			pLast->pNext = pTemp;
			pLast = pLast->pNext;
		}

		pCandCurrent = pCandCurrent->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return  (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_ReduceCandidate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		sony_demod_dvbs_s2_blindscan_data_t * pChannelList)
{
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pCandCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pChannelCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLast = NULL;
	sony_result_t result = SONY_RESULT_OK;
	uint8_t dflg = 0;
	uint32_t candFreq = 0;
	uint32_t candSR = 0;
	uint32_t candMinSR = 0;
	uint32_t candMaxSR = 0;
	uint32_t channelFreq = 0;
	uint32_t channelSR = 0;


	if ((!pCandList) || (!pChannelList)) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	pLast = pList;

	pCandCurrent = pCandList->pNext;
	while (pCandCurrent){
		dflg = 0;
		candFreq = pCandCurrent->data.candidate.centerFreqKHz;
		candSR = pCandCurrent->data.candidate.symbolRateKSps;
		candMinSR = pCandCurrent->data.candidate.minSymbolRateKSps;
		candMaxSR = pCandCurrent->data.candidate.maxSymbolRateKSps;
		pChannelCurrent = pChannelList->pNext;
		while (pChannelCurrent){
			channelFreq = pChannelCurrent->data.channelInfo.centerFreqKHz;
			channelSR = pChannelCurrent->data.channelInfo.symbolRateKSps;
			if (((channelFreq - (channelSR / 2)) < candFreq) && (candFreq < (channelFreq + (channelSR / 2)))){
				dflg = 1;
				break;
			}
			/* Next */
			pChannelCurrent = pChannelCurrent->pNext;
		}
		if (dflg == 0){
			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pTemp->data.candidate.centerFreqKHz = candFreq;
			pTemp->data.candidate.symbolRateKSps = candSR;
			pTemp->data.candidate.minSymbolRateKSps = candMinSR;
			pTemp->data.candidate.maxSymbolRateKSps = candMaxSR;
			pLast->pNext = pTemp;
			pLast = pLast->pNext;
		}
		/* Next */
		pCandCurrent = pCandCurrent->pNext;
	}

	/* Clear CandList */
	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent2 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLast = NULL;
	uint8_t dflg = 0;
	uint32_t freqA = 0;
	uint32_t srA = 0;
	uint32_t freqB = 0;
	uint32_t srB = 0;


	if ((!pStorage) || (!pCandList)){
		return (SONY_RESULT_ERROR_ARG);
	}

	pCurrent = pCandList->pNext;

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	pLast = pList;

	while(pCurrent){
		pCurrent2 = pCurrent->pNext;
		dflg = 0;
		freqA = pCurrent->data.candidate.centerFreqKHz;
		srA = pCurrent->data.candidate.symbolRateKSps;
		while(pCurrent2){
			freqB = pCurrent2->data.candidate.centerFreqKHz;
			srB = pCurrent2->data.candidate.symbolRateKSps;
			if((freqA == freqB) && (srA == srB)){
				dflg = 1;
				break;
			}
			/* Go to next */
			pCurrent2 = pCurrent2->pNext;
		}
		if(dflg == 0){
			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pTemp->data.candidate.centerFreqKHz = pCurrent->data.candidate.centerFreqKHz;
			pTemp->data.candidate.symbolRateKSps = pCurrent->data.candidate.symbolRateKSps;
			pTemp->data.candidate.minSymbolRateKSps = pCurrent->data.candidate.minSymbolRateKSps;
			pTemp->data.candidate.maxSymbolRateKSps = pCurrent->data.candidate.maxSymbolRateKSps;
			pLast->pNext = pTemp;
			pLast = pLast->pNext;
		}
		/* Go to next */
		pCurrent = pCurrent->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate2 (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pList;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp;
	sony_demod_dvbs_s2_blindscan_data_t * pLast;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent2;
	uint8_t dflg = 0;

	if ((!pStorage) || (!pCandList)){
		return (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	pLast = pList;

	pCurrent = pCandList->pNext;
	while(pCurrent){
		dflg = 0;
		pCurrent2 = pList->pNext;
		while(pCurrent2){
			if (pCurrent->data.candidate.centerFreqKHz == pCurrent2->data.candidate.centerFreqKHz){
				dflg = 1;
				break;
			}
			pCurrent2 = pCurrent2->pNext;
		}
		if (dflg == 0){
			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pTemp->data.candidate.centerFreqKHz = pCurrent->data.candidate.centerFreqKHz;
			pTemp->data.candidate.symbolRateKSps = pCurrent->data.candidate.symbolRateKSps;
			pTemp->data.candidate.minSymbolRateKSps = pCurrent->data.candidate.minSymbolRateKSps;
			pTemp->data.candidate.maxSymbolRateKSps = pCurrent->data.candidate.maxSymbolRateKSps;
			pLast->pNext = pTemp;
			pLast = pLast->pNext;
		}

		pCurrent = pCurrent->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_GetNonDetectedBand (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		uint32_t minPowerFreqKHz,
		uint32_t maxPowerFreqKHz,
		uint32_t resolutionKHz,
		sony_demod_dvbs_s2_blindscan_data_t * pChannelList,
		sony_demod_dvbs_s2_blindscan_data_t * pBandList)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pBandLast = NULL;
	uint32_t freqKHz = 0;
	uint32_t cf = 0;
	uint32_t sr = 0;
	uint8_t dflg = 0;
	uint32_t minFreqKHz = 0;
	uint32_t maxFreqKHz = 0;

	if ((!pStorage) || (!pChannelList) || (!pBandList)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	pBandLast = pBandList;

	minFreqKHz = minPowerFreqKHz;
	maxFreqKHz = 0;
	for (freqKHz = minPowerFreqKHz; freqKHz <= maxPowerFreqKHz; freqKHz += resolutionKHz){
		pCurrent = pChannelList->pNext;
		dflg = 0;
		while(pCurrent){
			cf = pCurrent->data.channelInfo.centerFreqKHz;
			sr = pCurrent->data.channelInfo.symbolRateKSps;
			if (((cf - (sr/2)) <= freqKHz) && (freqKHz <= (cf + (sr/2)))){
				dflg = 1;
				break;
			}
			/* Go to next */
			pCurrent = pCurrent->pNext;
		}
		if (dflg == 1){
			if ((minFreqKHz > 0) && (maxFreqKHz > 0)){
				result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
				if (result != SONY_RESULT_OK){
					return (result);
				}
				pTemp->data.band.minFreqKHz = minFreqKHz;
				pTemp->data.band.maxFreqKHz = maxFreqKHz;
				pBandLast->pNext = pTemp;
				pBandLast = pBandLast->pNext;
			}
			minFreqKHz = 0;
			maxFreqKHz = 0;
		} else {
			if (minFreqKHz == 0){
				minFreqKHz = freqKHz;
			} else {
				maxFreqKHz = freqKHz;
			}
		}
	}
	if ((minFreqKHz > 0) && (maxFreqKHz > 0)){
		result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
		if (result != SONY_RESULT_OK){
			return (result);
		}
		pTemp->data.band.minFreqKHz = minFreqKHz;
		pTemp->data.band.maxFreqKHz = maxFreqKHz;
		pBandLast->pNext = pTemp;
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_SortBySymbolrate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t baseSymbolRateKSps)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isContinue = 0;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListBase = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListNext = NULL;
	uint32_t current;
	uint32_t next;
	uint32_t target;


	if (!pCandList) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	while (pCandList->pNext){
		/* Get 1st data to "Temp" from CandList. */
		pTemp = pCandList->pNext;
		pCandList->pNext = pTemp->pNext;
		pTemp->pNext = NULL;
		if (pTemp->data.candidate.symbolRateKSps > baseSymbolRateKSps){
			target = pTemp->data.candidate.symbolRateKSps - baseSymbolRateKSps;
		} else {
			target = baseSymbolRateKSps - pTemp->data.candidate.symbolRateKSps;
		}

		isContinue = 1;
		pListBase = pList;
		while (isContinue){
			if (pListBase->pNext){
				/* Get current */
				pListCurrent = pListBase->pNext;
				if (pListCurrent->data.candidate.symbolRateKSps > baseSymbolRateKSps){
					current = pListCurrent->data.candidate.symbolRateKSps - baseSymbolRateKSps;
				} else {
					current = baseSymbolRateKSps - pListCurrent->data.candidate.symbolRateKSps;
				}

				if (pListCurrent->pNext){
					/* Get next */
					pListNext = pListCurrent->pNext;
					if (pListNext->data.candidate.symbolRateKSps > baseSymbolRateKSps){
						next = pListNext->data.candidate.symbolRateKSps - baseSymbolRateKSps;
					} else {
						next = baseSymbolRateKSps - pListNext->data.candidate.symbolRateKSps;
					}

					if (target < current){
						/*
						 *      Target
						 *        |
						 * [Base]---[Cur]---[Next]---
						 */
						pTemp->pNext = pListCurrent;
						pListBase->pNext = pTemp;
						isContinue = 0;
					} else if (target < next){
						/*
						 *              Target
						 *                |
						 * [Base]---[Cur]---[Next]---
						 */
						pTemp->pNext = pListNext;
						pListCurrent->pNext = pTemp;
						isContinue = 0;
					} else {
						/* Continue */
					}
				} else {
					if(target < current){
						/*
						 *      Target
						 *        |
						 * [Base]---[Cur]---(null)
						 */
						pTemp->pNext = pListCurrent;
						pListBase->pNext = pTemp;
						isContinue = 0;
					} else {
						/*
						 *              Target
						 *                |
						 * [Base]---[Cur]---(null)
						 */
						pListCurrent->pNext = pTemp;
						isContinue = 0;
					}
				}
				/* Update [Base] */
				pListBase = pListBase->pNext;
			} else {
				/* List is empty */
				pListBase->pNext = pTemp;
				isContinue = 0;
			}
		}
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

sony_result_t sony_demod_dvbs_s2_blindscan_algo_SortByFrequency (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t baseFrequencyKHz)
{
	sony_result_t result = SONY_RESULT_OK;
	uint8_t isContinue = 0;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListBase = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListNext = NULL;
	uint32_t current;
	uint32_t next;
	uint32_t target;


	if(!pCandList) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	while (pCandList->pNext){
		/* Get 1st data to "Temp" from CandList. */
		pTemp = pCandList->pNext;
		pCandList->pNext = pTemp->pNext;
		pTemp->pNext = NULL;
		if (pTemp->data.candidate.centerFreqKHz > baseFrequencyKHz){
			target = pTemp->data.candidate.centerFreqKHz - baseFrequencyKHz;
		} else {
			target = baseFrequencyKHz - pTemp->data.candidate.centerFreqKHz;
		}

		isContinue = 1;
		pListBase = pList;
		while (isContinue){
			if (pListBase->pNext){
				/* Get current */
				pListCurrent = pListBase->pNext;
				if (pListCurrent->data.candidate.centerFreqKHz > baseFrequencyKHz){
					current = pListCurrent->data.candidate.centerFreqKHz - baseFrequencyKHz;
				} else {
					current = baseFrequencyKHz - pListCurrent->data.candidate.centerFreqKHz;
				}

				if (pListCurrent->pNext){
					/* Get next */
					pListNext = pListCurrent->pNext;
					if (pListNext->data.candidate.centerFreqKHz > baseFrequencyKHz){
						next = pListNext->data.candidate.centerFreqKHz - baseFrequencyKHz;
					} else {
						next = baseFrequencyKHz - pListNext->data.candidate.centerFreqKHz;
					}

					if (target < current){
						/*
						 *      Target
						 *        |
						 * [Base]---[Cur]---[Next]---
						 */
						pTemp->pNext = pListCurrent;
						pListBase->pNext = pTemp;
						isContinue = 0;
					} else if (target < next){
						/*
						 *              Target
						 *                |
						 * [Base]---[Cur]---[Next]---
						 */
						pTemp->pNext = pListNext;
						pListCurrent->pNext = pTemp;
						isContinue = 0;
					} else {
						/* Continue */
					}
				} else {
					if(target < current){
						/*
						 *      Target
						 *        |
						 * [Base]---[Cur]---(null)
						 */
						pTemp->pNext = pListCurrent;
						pListBase->pNext = pTemp;
						isContinue = 0;
					} else {
						/*
						 *              Target
						 *                |
						 * [Base]---[Cur]---(null)
						 */
						pListCurrent->pNext = pTemp;
						isContinue = 0;
					}
				}
				/* Update [Base] */
				pListBase = pListBase->pNext;
			} else {
				/* List is empty */
				pListBase->pNext = pTemp;
				isContinue = 0;
			}
		}
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

/*----------------------------------------------------------------------------
  Static Functions
  ----------------------------------------------------------------------------*/
static sony_result_t moving_average (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		int32_t maLength)
{
	sony_demod_dvbs_s2_blindscan_power_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	sony_demod_dvbs_s2_blindscan_power_t * pMaLast = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pBufferList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLast = NULL;

	int32_t powerTemp = 0;
	int32_t power = 0;
	int32_t sum = 0;
	int32_t index = 0;
	sony_result_t result = SONY_RESULT_OK;


	if((!pStorage) || (!pPowerList) || (maLength == 0)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pBufferList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pLast = pBufferList;

	/* Get lower edge data */
	if(pPowerList->pNext){
		power = pPowerList->pNext->power;
	} else {
		return  (SONY_RESULT_ERROR_ARG);
	}
	sum = 0;
	/* Buffer for lower half */
	for(index = 0; index < (maLength/2); index++){
		result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
		if (result != SONY_RESULT_OK){
			return (result);
		}
		pTemp->data.power.power = power;
		pLast->pNext = pTemp;
		pLast = pLast->pNext;
		sum += power;
	}

	/*
	 *            <---maLength/2---->
	 * Buffer (*)--[ ]--[ ]--[ ]--[ ]...
	 *
	 * Power  (*)--[x]--[ ]--[ ]--[ ]...
	 */
	pCurrent = pPowerList->pNext;
	pMaLast = pPowerList->pNext;
	for (index = 0; index < (maLength/2); index++){
		if (pMaLast){
			power = pMaLast->power;
			if (pMaLast->pNext){
				pMaLast = pMaLast->pNext;
			}
		}
		sum += power;
	}

	while (pCurrent){
		/* Store power */
		powerTemp = pCurrent->power;
		/* Update data */
		pCurrent->power = sum / maLength;

		/* === Prepare for next data. === */
		sum -= pBufferList->pNext->data.power.power;

		/* Remove 1st data from BufferList.
		 *            <---maLength/2---->
		 * Buffer (*)--[ ]--[ ]--[ ]--[ ]...
		 *              |
		 *              Remove
		 */
		pTemp = pBufferList->pNext;
		pBufferList->pNext = pTemp->pNext;
		result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pTemp);
		if (result != SONY_RESULT_OK){
			return  (result);
		}

		if(pMaLast)
		{
			power = pMaLast->power;
			if(pMaLast->pNext){
				pMaLast = pMaLast->pNext;
			}
		}

		/* Add data to last of BufferList.
		 *            <---maLength/2---->
		 * Buffer (*)--[ ]--[ ]--[ ]--[ ]...
		 *                                 |
		 *                                 Add
		 */
		result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
		if (result != SONY_RESULT_OK){
			return  (result);
		}
		pTemp->data.power.power = powerTemp;
		pLast->pNext = pTemp;
		pLast = pLast->pNext;
		sum += power;

		pCurrent = pCurrent->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pBufferList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pBufferList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

static sony_result_t wagiri (struct cxd2841er_priv *priv,
		sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		uint32_t clipStep,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint32_t cferr,
		uint8_t isMpMode,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList)
{
	sony_result_t result = SONY_RESULT_OK;
	int32_t level = 0;
	int32_t powerMin = 0;
	int32_t powerMax = 0;
	uint8_t state = 0;
	uint32_t startFreqKHz = 0;
	uint32_t stopFreqKHz = 0;
	uint32_t ctFrequencyKHz = 0;
	uint32_t ctSymbolRateKSps = 0;
	sony_demod_dvbs_s2_blindscan_power_t * pCurrentPower = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLastCand = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTempList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTempCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTempData = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTempList2 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTempCurrent2 = NULL;


	if((!pStorage) || (!pPowerList)|| (!pCandList)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	if(!pPowerList->pNext){
		/* Power data is empty */
		return  (SONY_RESULT_OK);
	}

	/* Get last data */
	pLastCand = pCandList;
	while(pLastCand->pNext){
		pLastCand = pLastCand->pNext;
	}

	result = get_power_range (pPowerList, &powerMin, &powerMax);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): get_power_range failed \n", __func__); 
		return (result);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTempList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): AllocData failed \n", __func__); 
		return (result);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTempList2);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): AllocData.2 failed \n", __func__); 
		return (result);
	}

	pTempCurrent2 = pTempList2;

	for (level = powerMax; level >= powerMin; level -= (int32_t)clipStep){
		/* Initialize state */
		state = 2;
		pTempCurrent = pTempList;

		/* Set 1st data. */
		pCurrentPower = pPowerList->pNext;
		while(pCurrentPower){

			if(pCurrentPower->freqKHz == 0){
				break;
			}

			if(pCurrentPower->power > level){
				switch(state)
				{
					case 0:
						/* start edge. */
						startFreqKHz = pCurrentPower->freqKHz;
						state = 1;
						break;

					case 1:
						/* do nothing. */
						break;

					case 2:
						/*
						 * do nothing.
						 * wait for "power <= clipLevel"
						 */
						break;

					default:
						dev_err(&priv->i2c->dev, "%s(): pCurrentPower failed \n", __func__); 
						return  (SONY_RESULT_ERROR_SW_STATE);
				}
			} else {
				switch(state)
				{
					case 0:
						/* do nothing. */
						break;

					case 1:
						/* Stop edge. */
						stopFreqKHz = pCurrentPower->freqKHz;
						state = 0;
						ctFrequencyKHz = ((stopFreqKHz + startFreqKHz + 1000) / 2000) * 1000;
						ctSymbolRateKSps = stopFreqKHz - startFreqKHz;

						/* Alloc data */
						result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTempData);
						if (result != SONY_RESULT_OK){
							return (result);
						}

						pTempData->data.candidate.centerFreqKHz = ctFrequencyKHz;
						pTempData->data.candidate.symbolRateKSps = ctSymbolRateKSps;
						pTempData->data.candidate.minSymbolRateKSps = 0;
						pTempData->data.candidate.maxSymbolRateKSps = 0;

						/* Add data */
						pTempCurrent->pNext = pTempData;
						pTempCurrent = pTempCurrent->pNext;
						break;

					case 2:
						/* Entry point. */
						state = 0;
						break;

					default:
						dev_err(&priv->i2c->dev, "%s(): pCurrentPower.2 failed \n", __func__); 
						return  (SONY_RESULT_ERROR_SW_STATE);
				}
			}

			/* Go to next data. */
			pCurrentPower = pCurrentPower->pNext;
		}

		/* Get candidate from once wagiri result. */
		result = select_candidate (pStorage, pTempList, minSymbolRateKSps, maxSymbolRateKSps, isMpMode);
		if (result != SONY_RESULT_OK){
			dev_err(&priv->i2c->dev, "%s(): select_candidate failed \n", __func__); 
			return (result);
		}

		/* Add TempList data to TempList2 */
		while (pTempCurrent2->pNext){
			pTempCurrent2 = pTempCurrent2->pNext;
		}
		pTempCurrent2->pNext = pTempList->pNext;
		pTempList->pNext = NULL;
	}

	result = pick_duplicated_area (priv, pStorage, pTempList2, cferr);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): pick_duplicated_area failed \n", __func__); 
		return (result);
	}

	pLastCand->pNext = pTempList2->pNext;
	pTempList2->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pTempList);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pTempList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pTempList2);
	if (result != SONY_RESULT_OK){
		return (result);
	}
	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pTempList2);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return  (SONY_RESULT_OK);
}

static sony_result_t delete_duplicated_area (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t cferr)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrentA = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrentB = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pListCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	uint32_t a_cf = 0;
	uint32_t a_min = 0;
	uint32_t a_max = 0;
	uint32_t b_cf = 0;
	uint32_t b_min = 0;
	uint32_t b_max = 0;
	int32_t ms = 0;

	if(!pCandList) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pListCurrent = pList;

	pCurrentA = pCandList->pNext;

	while (pCurrentA){
		a_cf = pCurrentA->data.candidate.centerFreqKHz;
		a_min = pCurrentA->data.candidate.minSymbolRateKSps;
		a_max = pCurrentA->data.candidate.maxSymbolRateKSps;
		if (a_cf != 0){
			pCurrentB = pCurrentA->pNext;
			while (pCurrentB){
				b_cf = pCurrentB->data.candidate.centerFreqKHz;
				b_min = pCurrentB->data.candidate.minSymbolRateKSps;
				b_max = pCurrentB->data.candidate.maxSymbolRateKSps;
				if (a_min < b_min){
					ms = (int32_t)((a_min + 3) / 4) - (int32_t)cferr;
				} else {
					ms = (int32_t)((b_min + 3) / 4) - (int32_t)cferr;
				}
				if (ms < 0){
					ms = 0;
				}
				if (((a_cf >= b_cf) && ((a_cf - b_cf) <= (uint32_t)ms)) || ((b_cf >= a_cf) && ((b_cf - a_cf) <= (uint32_t)ms))){
					if ((b_min <= a_max) && (a_max <= b_max)){
						a_max = b_max;
						pCurrentB->data.candidate.centerFreqKHz = 0;
					} else if ((b_min < a_min) && (a_min < b_max)){
						a_min = b_min;
						pCurrentB->data.candidate.centerFreqKHz = 0;
					} else {
						/* Do nothing */
					}
				}
				pCurrentB = pCurrentB->pNext;
			}
			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pTemp->data.candidate.centerFreqKHz = a_cf;
			pTemp->data.candidate.symbolRateKSps = (a_min + a_max) / 2;
			pTemp->data.candidate.minSymbolRateKSps = a_min;
			pTemp->data.candidate.maxSymbolRateKSps = a_max;
			pListCurrent->pNext = pTemp;
			pListCurrent = pListCurrent->pNext;
		}
		pCurrentA = pCurrentA->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

static sony_result_t get_power_range (sony_demod_dvbs_s2_blindscan_power_t * pPowerList,
		int32_t * pPowerMin,
		int32_t * pPowerMax)
{
	sony_demod_dvbs_s2_blindscan_power_t * pCurrent = NULL;
	int32_t minValue = 0;
	int32_t maxValue = 0;


	if((!pPowerList) || (!pPowerList->pNext) || (!pPowerMin) || (!pPowerMax)){
		return  (SONY_RESULT_ERROR_ARG);
	}

	pCurrent = pPowerList->pNext;
	minValue = pCurrent->power;
	maxValue = pCurrent->power;

	while(pCurrent->pNext){
		pCurrent = pCurrent->pNext;
		if (pCurrent->freqKHz > 0){
			if (minValue > pCurrent->power){
				minValue = pCurrent->power;
			}
			if (maxValue < pCurrent->power){
				maxValue = pCurrent->power;
			}
		}
	}

	*pPowerMin = minValue;
	*pPowerMax = maxValue;

	return (SONY_RESULT_OK);
}

static sony_result_t select_candidate (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps,
		uint8_t isMpMode)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent1 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent2 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pOutList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pOutLast = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	uint32_t freq1 = 0;
	uint32_t sr1 = 0;
	uint32_t freq2 = 0;
	uint32_t sr2 = 0;
	uint32_t mpFrequencyKHz = 0;
	uint32_t mpSymbolRateKSps = 0;
	uint32_t space = 0;


	if ((!pStorage) || (!pCandList)){
		return (SONY_RESULT_ERROR_ARG);
	}

	/* Alloc */
	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pOutList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pOutLast = pOutList;

	if(isMpMode){
		pCurrent1 = pCandList->pNext;
		while(pCurrent1){
			/* Get freq1 and sr1 */
			freq1 = pCurrent1->data.candidate.centerFreqKHz;
			sr1 = pCurrent1->data.candidate.symbolRateKSps;

			pCurrent2 = pCurrent1->pNext;
			while(pCurrent2){
				/* Get freq2 and sr2 */
				freq2 = pCurrent2->data.candidate.centerFreqKHz;
				sr2 = pCurrent2->data.candidate.symbolRateKSps;

				mpFrequencyKHz   = ((freq2 + ( sr2      / 2)) + (freq1 - (sr1 / 2))) / 2;
				mpSymbolRateKSps =  (freq2 + ( sr2      / 2)) - (freq1 - (sr1 / 2));
				space            =  (freq2 - ((sr2 + 1) / 2)) - (freq1 + (sr1 / 2));

				if((space < ((mpSymbolRateKSps + 1)/2)) && (minSymbolRateKSps <= mpSymbolRateKSps) && (mpSymbolRateKSps <= maxSymbolRateKSps)){
					/* Add candidate */
					result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
					if (result != SONY_RESULT_OK){
						return (result);
					}
					pTemp->data.candidate.centerFreqKHz = mpFrequencyKHz;
					pTemp->data.candidate.symbolRateKSps = mpSymbolRateKSps;
					pTemp->data.candidate.minSymbolRateKSps = 0;
					pTemp->data.candidate.maxSymbolRateKSps = 0;
					pOutLast->pNext = pTemp;
					pOutLast = pOutLast->pNext;
				} else {
					break;
				}

				/* Go to next */
				pCurrent2 = pCurrent2->pNext;
			}

			/* Go to next */
			pCurrent1 = pCurrent1->pNext;
		}
	}

	pCurrent1 = pCandList->pNext;
	while(pCurrent1){
		freq1 = pCurrent1->data.candidate.centerFreqKHz;
		sr1 = pCurrent1->data.candidate.symbolRateKSps;

		if ((minSymbolRateKSps <= sr1) && (sr1 <= maxSymbolRateKSps)){
			/* Add candidate */
			result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
			if (result != SONY_RESULT_OK){
				return (result);
			}
			pTemp->data.candidate.centerFreqKHz = freq1;
			pTemp->data.candidate.symbolRateKSps = sr1;
			pTemp->data.candidate.minSymbolRateKSps = 0;
			pTemp->data.candidate.maxSymbolRateKSps = 0;
			pOutLast->pNext = pTemp;
			pOutLast = pOutLast->pNext;
		}

		/* Go to next */
		pCurrent1 = pCurrent1->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pOutList->pNext;

	/* Free */
	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pOutList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}

static sony_result_t pick_duplicated_area (struct cxd2841er_priv *priv,
		sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t cferr)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent1 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent2 = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pInList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pOutList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pOutCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	uint32_t freq1 = 0;
	uint32_t sr1 = 0;
	uint32_t freq2 = 0;
	uint32_t sr2 = 0;
	uint32_t freqUpper = 0;
	uint32_t freqLower = 0;
	uint32_t srUpper = 0;
	uint32_t srLower = 0;
	uint32_t minSymbolRateKSps = 0;
	uint32_t maxSymbolRateKSps = 0;


	if ((!pStorage) || (!pCandList)){
		dev_err(&priv->i2c->dev, "%s(): pStorage=%p pCandList=%p\n", __func__, pStorage, pCandList); 
		return (SONY_RESULT_ERROR_ARG);
	}

	/* Alloc */
	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pInList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): AllocData failed \n", __func__); 
		return (result);
	}
	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pOutList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): AllocData.2 failed \n", __func__); 
		return (result);
	}

	pInList->pNext = pCandList->pNext;
	pOutCurrent = pOutList;

	pCurrent1 = pCandList->pNext;
	while(pCurrent1){
		freq1 = pCurrent1->data.candidate.centerFreqKHz;
		sr1 = pCurrent1->data.candidate.symbolRateKSps;

		pCurrent2 = pCurrent1->pNext;
		while(pCurrent2){
			freq2 = pCurrent2->data.candidate.centerFreqKHz;
			sr2 = pCurrent2->data.candidate.symbolRateKSps;

			freqUpper = (freq1 > freq2) ? freq1 : freq2;
			freqLower = (freq1 > freq2) ? freq2 : freq1;
			srUpper = (sr1 > sr2) ? sr1 : sr2;
			srLower = (sr1 > sr2) ? sr2 : sr1;
			if ((freqUpper - freqLower) <= cferr){
				minSymbolRateKSps =  (srUpper *  7)      / 10;
				maxSymbolRateKSps = ((srLower * 16) + 9) / 10;
				if (minSymbolRateKSps < maxSymbolRateKSps){
					dev_dbg(&priv->i2c->dev, "%s(): adding candidate freq=%d\n",
							__func__, ((((freq1 + freq2) / 2) + 500) / 1000) * 1000); 
					/* Add candidate */
					result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
					if (result != SONY_RESULT_OK){
						dev_err(&priv->i2c->dev, "%s(): AllocData.3 failed \n", __func__); 
						return (result);
					}
					pTemp->data.candidate.centerFreqKHz = ((((freq1 + freq2) / 2) + 500) / 1000) * 1000;
					pTemp->data.candidate.symbolRateKSps = 0;
					pTemp->data.candidate.minSymbolRateKSps = minSymbolRateKSps;
					pTemp->data.candidate.maxSymbolRateKSps = maxSymbolRateKSps;
					pOutCurrent->pNext = pTemp;
					pOutCurrent = pOutCurrent->pNext;
					// fprintf(stderr,"aospan:%s candidate.centerFreqKHz=%d \n", __func__, pTemp->data.candidate.centerFreqKHz);
				}
			}

			/* Go to next */
			pCurrent2 = pCurrent2->pNext;
		}

		/* Go to next */
		pCurrent1 = pCurrent1->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pInList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): ClearDataList failed \n", __func__); 
		return (result);
	}

	pCandList->pNext = pOutList->pNext;

	/* Free */
	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pInList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): FreeData failed \n", __func__); 
		return (result);
	}
	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pOutList);
	if (result != SONY_RESULT_OK){
		dev_err(&priv->i2c->dev, "%s(): FreeData.2 failed \n", __func__); 
		return (result);
	}

	return (SONY_RESULT_OK);
}

static sony_result_t limit_range (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_data_t * pCandList,
		uint32_t minSymbolRateKSps,
		uint32_t maxSymbolRateKSps)
{
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pList = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pLast = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
	uint32_t lim_min = 0;
	uint32_t lim_max = 0;
	uint32_t center = 0;

	if(!pCandList) {
		return  (SONY_RESULT_ERROR_ARG);
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pLast = pList;

	pCurrent = pCandList->pNext;
	while (pCurrent){
		/* lim_min */
		if (pCurrent->data.candidate.minSymbolRateKSps < minSymbolRateKSps){
			lim_min = minSymbolRateKSps;
		} else {
			lim_min = pCurrent->data.candidate.minSymbolRateKSps;
		}
		/* lim_max */
		if (pCurrent->data.candidate.maxSymbolRateKSps > maxSymbolRateKSps){
			lim_max = maxSymbolRateKSps;
		} else {
			lim_max = pCurrent->data.candidate.maxSymbolRateKSps;
		}
		if ((lim_min < 30000) && (20000 < lim_max)){
			if ((20000 < lim_min) && (lim_max < 30000)){
				center = (lim_min + lim_max) / 2;
			} else if ((lim_min <= 20000) && (lim_max < 30000)){
				center = (20000 + lim_max) / 2;
			} else if ((20000 < lim_min) && (30000 <= lim_max)){
				center = (30000 + lim_min) / 2;
			} else {
				center = 25000;
			}
		} else {
			center = (lim_min + lim_max) / 2;
		}

		result = sony_demod_dvbs_s2_blindscan_AllocData (pStorage, &pTemp);
		if (result != SONY_RESULT_OK){
			return (result);
		}

		pTemp->data.candidate.centerFreqKHz = pCurrent->data.candidate.centerFreqKHz;
		pTemp->data.candidate.symbolRateKSps = center;
		pTemp->data.candidate.minSymbolRateKSps = lim_min;
		pTemp->data.candidate.maxSymbolRateKSps = lim_max;

		pLast->pNext = pTemp;
		pLast = pLast->pNext;

		/* Next data */
		pCurrent = pCurrent->pNext;
	}

	result = sony_demod_dvbs_s2_blindscan_ClearDataList (pStorage, pCandList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	pCandList->pNext = pList->pNext;
	pList->pNext = NULL;

	result = sony_demod_dvbs_s2_blindscan_FreeData (pStorage, pList);
	if (result != SONY_RESULT_OK){
		return (result);
	}

	return (SONY_RESULT_OK);
}
/*** Algo end ***/

/*** helene tuner ***/
static sony_result_t sony_tuner_helene_sat_AGCLevel2AGCdB (uint32_t AGCLevel,
		int32_t * pAGCdB)
{
	int32_t tempA = 0;
	uint8_t isNegative = 0;
	uint32_t tempDiv = 0;
	uint32_t tempQ = 0;
	uint32_t tempR = 0;

	if (!pAGCdB){
		return (SONY_RESULT_ERROR_ARG);
	}

	/*------------------------------------------------
	  Gain_db      = AGCLevel * (-14   / 403) + 97
	  Gain_db_x100 = AGCLevel * (-1400 / 403) + 9700
	  -------------------------------------------------*/
	tempA = (int32_t)AGCLevel * (-1400);

	tempDiv = 403;
	if (tempA > 0){
		isNegative = 0;
		tempQ = (uint32_t)tempA / tempDiv;
		tempR = (uint32_t)tempA % tempDiv;
	} else {
		isNegative = 1;
		tempQ = (uint32_t)(tempA * (-1)) / tempDiv;
		tempR = (uint32_t)(tempA * (-1)) % tempDiv;
	}

	if (isNegative){
		if (tempR >= (tempDiv/2)){
			*pAGCdB = (int32_t)(tempQ + 1) * (int32_t)(-1);
		} else {
			*pAGCdB = (int32_t)tempQ * (int32_t)(-1);
		}
	} else {   
		if (tempR >= (tempDiv/2)){
			*pAGCdB = (int32_t)(tempQ + 1);
		} else {
			*pAGCdB = (int32_t)tempQ;
		}
	}
	*pAGCdB += 9700;

	return (SONY_RESULT_OK);
}

/*** helene tuner end ***/

static sony_result_t setProgress (sony_demod_dvbs_s2_blindscan_seq_t * pSeq,
		uint8_t minProgressRange,
		uint8_t maxProgressRange,
		uint8_t minorProgress)
{
	if(!pSeq) {
		return SONY_RESULT_ERROR_ARG;
	}

	pSeq->commonParams.progressInfo.majorMinProgress = minProgressRange;
	pSeq->commonParams.progressInfo.majorMaxProgress = maxProgressRange;
	pSeq->commonParams.progressInfo.minorProgress = minorProgress;
	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_StorageInit (sony_demod_dvbs_s2_blindscan_data_storage_t * pStorage,
		sony_demod_dvbs_s2_blindscan_power_t * pPowerArrayTop,
		int32_t powerArrayLength,
		sony_demod_dvbs_s2_blindscan_data_t * pDataArrayTop,
		int32_t dataArrayLength) 
{   
	int32_t i = 0;
	sony_demod_dvbs_s2_blindscan_power_t * pPowerCurrent = NULL;
	sony_demod_dvbs_s2_blindscan_data_t * pDataCurrent = NULL;

	if ((!pStorage) || (!pPowerArrayTop) || (powerArrayLength <= 0) || (!pDataArrayTop) || (dataArrayLength <= 0)){
		return SONY_RESULT_ERROR_ARG;
	}

	pStorage->pPowerArrayTop = pPowerArrayTop;
	pStorage->powerArrayLength = powerArrayLength;
	pStorage->pDataArrayTop = pDataArrayTop;
	pStorage->dataArrayLength = dataArrayLength;

	pPowerCurrent = &(pStorage->availablePowerList);
	for(i = 0; i < powerArrayLength; i++){
		pPowerCurrent->pNext = (pPowerArrayTop + i);
		pPowerCurrent = pPowerCurrent->pNext;
	}
	pPowerCurrent->pNext = NULL;
	pStorage->currentUsedPowerCount = 0;
	pStorage->maxUsedPowerCount = 0;

	pDataCurrent = &(pStorage->availableDataList);
	for(i = 0; i < dataArrayLength; i++){
		pDataCurrent->pNext = (pDataArrayTop + i);
		pDataCurrent = pDataCurrent->pNext;
	}
	pDataCurrent->pNext = NULL;
	pStorage->currentUsedCount = 0;
	pStorage->maxUsedCount = 0;

	return SONY_RESULT_OK;
}

sony_result_t sony_demod_dvbs_s2_blindscan_Initialize(struct dvb_frontend* fe)
{   
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	uint32_t agcLevel = 0;
	sony_result_t result = SONY_RESULT_OK;

	fe->dtv_property_cache.frequency = 1000000; /* 1GHz */
	fe->dtv_property_cache.symbol_rate = 20000000; /* 20 kSym */
	fe->dtv_property_cache.delivery_system = SYS_DVBS_S2_AUTO;
	priv->system_sony = SONY_DTV_SYSTEM_ANY;

	result = cxd2841er_set_frontend_s (fe);
	if (result){
		return BLINDSCAN_SEQ_STATE_UNKNOWN;
	}

	// TODO: pDemod->dvbss2ScanMode = 0x01;

	/* Set SLV-T Bank : 0x00 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0x00);
	/* OREG_ARC_CKACHGAUTO[0]
	 * LS/HS auto selection : OFF
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2B, 0x00);
	/* Set SLV-T Bank : 0xAE */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xAE);
	/* OFSM_SRSON[0]
	 * SR search : ON
	 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x24, 0x01);
	/* OFSM_UNLOCKSEL_ARS[2:0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x2E, 0x05);
	/* Set SLV-T Bank : 0xA0 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA0);
	/* OMODE[1:0] */
	cxd2841er_write_reg(priv, I2C_SLVT, 0xA5, 0x00);
	/* Set SLV-T Bank : 0xA3 */
	cxd2841er_write_reg(priv, I2C_SLVT, 0x00, 0xA3);
	cxd2841er_write_reg(priv, I2C_SLVT, 0xBD, 0x00);

	return result;
}


int cxd2841er_blind_scan_init(struct dvb_frontend* fe,
		bool do_power_scan,
		sony_demod_dvbs_s2_blindscan_seq_t * pSeq,
		u32 minFreqKHz, u32 maxFreqKHz, u32 minSymbolRateKSps, u32 maxSymbolRateKSps)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	sony_result_t result = SONY_RESULT_OK;

	dev_dbg(&priv->i2c->dev, "%s():\n", __func__); 
	if (!pSeq || !priv)
		return -EINVAL;

	pSeq->isContinue = 1;
	if (do_power_scan)
		pSeq->seqState = BLINDSCAN_SEQ_STATE_SPECTRUM;
	else
		pSeq->seqState = BLINDSCAN_SEQ_STATE_START;
	pSeq->minFreqKHz = minFreqKHz;
	pSeq->maxFreqKHz = maxFreqKHz;
	pSeq->minSymbolRateKSps = minSymbolRateKSps;
	pSeq->maxSymbolRateKSps = maxSymbolRateKSps;

	pSeq->minPowerFreqKHz = minFreqKHz - (((maxSymbolRateKSps * 675) + 999) / 1000);
	pSeq->minPowerFreqKHz = (pSeq->minPowerFreqKHz / 1000) * 1000;

	pSeq->maxPowerFreqKHz = maxFreqKHz + (((maxSymbolRateKSps * 675) + 999) / 1000);
	pSeq->maxPowerFreqKHz = ((pSeq->maxPowerFreqKHz + 999) / 1000) * 1000;

	// TODO: pSeq->commonParams.pDemod = pDemod;
	pSeq->commonParams.detInfo.isDetect = 0;
	pSeq->commonParams.tuneReq.isRequest = 0;
	pSeq->commonParams.agcInfo.isRequest = 0;
	pSeq->commonParams.waitTime = 0;
	pSeq->commonParams.fe = fe;
	pSeq->commonParams.priv = priv;

	pSeq->commonParams.progressInfo.progress = 0;
	result = setProgress (pSeq, 0, 10, 0);
	if (result != SONY_RESULT_OK){
		return result;
	}

	switch (priv->xtal) {
		case SONY_XTAL_24000:
			pSeq->commonParams.ckalFreqKHz = 64000; /* CKAL */
			pSeq->commonParams.ckahFreqKHz = 96000; /* CKAH */
			break;

		case SONY_XTAL_20500:
		case SONY_XTAL_41000:
			pSeq->commonParams.ckalFreqKHz = 64917; /* CKAL */
			pSeq->commonParams.ckahFreqKHz = 97375; /* CKAH */
			break;

		default:
			return -EINVAL;
	}

	dev_dbg(&priv->i2c->dev, "%s(): storage init\n", __func__); 
	result = sony_demod_dvbs_s2_blindscan_StorageInit (&(pSeq->commonParams.storage),
			pSeq->powerArray,
			sizeof(pSeq->powerArray)/sizeof(pSeq->powerArray[0]),
			pSeq->dataArray,
			sizeof(pSeq->dataArray)/sizeof(pSeq->dataArray[0]));

	if (result != SONY_RESULT_OK)
		return result;

	/* Initialize demodulator to start BlindScan. */
	dev_dbg(&priv->i2c->dev, "%s(): init demod to blind scan \n", __func__); 
	result = sony_demod_dvbs_s2_blindscan_Initialize(fe);
	if (result != SONY_RESULT_OK){
		return result;
	}

	dev_dbg(&priv->i2c->dev, "%s(): init subseq\n", __func__); 
	result = sony_demod_dvbs_s2_blindscan_subseq_bt_Initialize (&pSeq->subseqBT, &pSeq->commonParams);
	if (result != SONY_RESULT_OK){
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_subseq_pm_Initialize (&pSeq->subseqPM, &pSeq->commonParams);
	if (result != SONY_RESULT_OK) {
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_subseq_ss_Initialize (&pSeq->subseqSS, &pSeq->commonParams);
	if (result != SONY_RESULT_OK){
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_subseq_fs_Initialize (&pSeq->subseqFS, &pSeq->subseqBT, &pSeq->commonParams);
	if (result != SONY_RESULT_OK){
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_subseq_cs_Initialize (&pSeq->subseqCS, &pSeq->subseqPM, &pSeq->commonParams);
	if (result != SONY_RESULT_OK){
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData(&(pSeq->commonParams.storage), &(pSeq->pCandList1));
	if (result != SONY_RESULT_OK){
		return result;
	}

	result = sony_demod_dvbs_s2_blindscan_AllocData(&(pSeq->commonParams.storage), &(pSeq->pCandList2));
	if (result != SONY_RESULT_OK){
		return result;
	}   

	result = sony_demod_dvbs_s2_blindscan_AllocData(&(pSeq->commonParams.storage), &(pSeq->pDetectedList));
	if (result != SONY_RESULT_OK){
		return result;
	}   

	result = sony_demod_dvbs_s2_blindscan_AllocData(&(pSeq->commonParams.storage), &(pSeq->pBandList));
	if (result != SONY_RESULT_OK){
		return result;
	}   

	return SONY_RESULT_OK;
}

int dump_power(sony_demod_dvbs_s2_blindscan_seq_t * pSeq)
{
	// state machine will dump power soon
	pSeq->commonParams.powerInfo.pPowerList = pSeq->subseqSS.pPowerList;
	pSeq->commonParams.powerInfo.isPower = 1;
}

sony_result_t sony_demod_dvbs_s2_blindscan_seq_Sequence (sony_demod_dvbs_s2_blindscan_seq_t * pSeq)
{   
	struct cxd2841er_priv *priv = pSeq->commonParams.priv;
	sony_result_t result = SONY_RESULT_OK;

	if(!pSeq) {
		return SONY_RESULT_ERROR_ARG;
	}

	if (pSeq->subseqSS.isEnable){
		/* SS seq */
		result = sony_demod_dvbs_s2_blindscan_subseq_ss_Sequence (&pSeq->subseqSS);
		if (result != SONY_RESULT_OK){
			dev_err(&priv->i2c->dev, "%s(): ss_Sequence failed \n", __func__); 
			return result;
		}
		{   
			uint32_t current = pSeq->commonParams.tuneReq.frequencyKHz;
			uint32_t min = pSeq->minPowerFreqKHz;
			uint32_t max = pSeq->maxPowerFreqKHz;
			if (pSeq->commonParams.tuneReq.isRequest){
				if (min >= max){
					pSeq->commonParams.progressInfo.minorProgress = 100;
				} else {
					pSeq->commonParams.progressInfo.minorProgress = (uint8_t)(((current - min) * 100) / (max - min));
				}
			}
		}
	} else if (pSeq->subseqFS.isEnable){
		if (pSeq->subseqBT.isEnable){
			/* BT seq */
			result = sony_demod_dvbs_s2_blindscan_subseq_bt_Sequence (&pSeq->subseqBT);
			if (result != SONY_RESULT_OK){
				dev_err(&priv->i2c->dev, "%s(): bt_Sequence failed \n", __func__); 
				return result;
			}
		} else {
			/* FS seq */
			result = sony_demod_dvbs_s2_blindscan_subseq_fs_Sequence (&pSeq->subseqFS);
			if (result != SONY_RESULT_OK){
				dev_err(&priv->i2c->dev, "%s(): fs_Sequence failed \n", __func__); 
				return result;
			}
		}
	} else if (pSeq->subseqCS.isEnable){
		if (pSeq->subseqPM.isEnable){
			/* PM seq */
			result = sony_demod_dvbs_s2_blindscan_subseq_pm_Sequence(&pSeq->subseqPM);
			if (result != SONY_RESULT_OK){
				dev_err(&priv->i2c->dev, "%s(): pm_Sequence failed \n", __func__); 
				return result;
			}
		} else {
			/* CS seq */
			result = sony_demod_dvbs_s2_blindscan_subseq_cs_Sequence(&pSeq->subseqCS);
			if (result != SONY_RESULT_OK){
				dev_err(&priv->i2c->dev, "%s(): cs_Sequence failed \n", __func__); 
				return result;
			}
		}
	} else {
		/* Main sequence */
		switch(pSeq->seqState)
		{
			case BLINDSCAN_SEQ_STATE_SPECTRUM:
				/* Get power spectrum (1st) */
				result = sony_demod_dvbs_s2_blindscan_subseq_ss_Start(&pSeq->subseqSS,
						pSeq->minPowerFreqKHz,
						pSeq->maxPowerFreqKHz,
						500,
						20000,
						priv->dvbss2PowerSmooth);
				if (result != SONY_RESULT_OK){
					dev_err(&priv->i2c->dev, "%s(): ss_Start spectrum failed \n", __func__); 
					return result;
				}
				pSeq->commonParams.waitTime = 0;
				pSeq->seqState = BLINDSCAN_SEQ_STATE_SPECTRUM_SAVE;
				break;

			case BLINDSCAN_SEQ_STATE_SPECTRUM_SAVE:
				dump_power(pSeq);
				pSeq->seqState = BLINDSCAN_SEQ_STATE_START;
				break;

			case BLINDSCAN_SEQ_STATE_START:
				if (pSeq->maxSymbolRateKSps >= 20000){
					/* === Stage 1 === */
					/* Get power spectrum (1st) */
					result = sony_demod_dvbs_s2_blindscan_subseq_ss_Start(&pSeq->subseqSS,
							pSeq->minPowerFreqKHz,
							pSeq->maxPowerFreqKHz,
							2000,
							20000,
							priv->dvbss2PowerSmooth);
					if (result != SONY_RESULT_OK){
						dev_err(&priv->i2c->dev, "%s(): ss_Start failed \n", __func__); 
						return result;
					}
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_SS1_FIN;
				} else {
					/* Skip stage 1 */
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_STAGE1_FIN;
				}
				break;

			case BLINDSCAN_SEQ_STATE_SS1_FIN:
				/* Get candidate (MP) */
				result = sony_demod_dvbs_s2_blindscan_algo_GetCandidateMp (priv, &(pSeq->commonParams.storage),
						pSeq->subseqSS.pPowerList,
						100,     /* slice :  1.00dB  */
						20000,   /* min   : 20  MSps */
						45000,   /* max   : 45  MSps */
						4000,    /* cferr :  4  MHz  */
						pSeq->pCandList1);
				if (result != SONY_RESULT_OK){
					dev_err(&priv->i2c->dev, "%s(): algo_GetCandidateMp failed \n", __func__); 
					return result;
				}

				result = sony_demod_dvbs_s2_blindscan_algo_SortBySymbolrate (&(pSeq->commonParams.storage), pSeq->pCandList1, 20000);
				if (result != SONY_RESULT_OK){
					dev_err(&priv->i2c->dev, "%s(): SortBySymbolrate failed \n", __func__); 
					return result;
				}

				/* Start fine search */
				result = setProgress (pSeq, 10, 25, 0);
				if (result != SONY_RESULT_OK){
					return result;
				}

				result = sony_demod_dvbs_s2_blindscan_subseq_fs_Start(&pSeq->subseqFS, pSeq->pCandList1, 19900, 45000, pSeq->pDetectedList);
				if (result != SONY_RESULT_OK){
					dev_err(&priv->i2c->dev, "%s(): fs_Start (19900-45000) failed \n", __func__); 
					return result;
				}

				pSeq->commonParams.waitTime = 0;
				pSeq->seqState = BLINDSCAN_SEQ_STATE_STAGE1_FIN;
				break;

			case BLINDSCAN_SEQ_STATE_STAGE1_FIN:
				if (pSeq->minSymbolRateKSps < 20000){
					/* Stage 2 or 3 */
					result = sony_demod_dvbs_s2_blindscan_ClearDataList (&(pSeq->commonParams.storage), pSeq->pCandList1);
					if (result != SONY_RESULT_OK){
						return result;
					}
					result = sony_demod_dvbs_s2_blindscan_ClearDataList (&(pSeq->commonParams.storage), pSeq->pCandList2);
					if (result != SONY_RESULT_OK){
						return result;
					}
					result = sony_demod_dvbs_s2_blindscan_algo_GetNonDetectedBand (&(pSeq->commonParams.storage),
							pSeq->minPowerFreqKHz,
							pSeq->maxPowerFreqKHz,
							500,
							pSeq->pDetectedList,
							pSeq->pBandList);
					if (result != SONY_RESULT_OK){
						return result;
					}
					pSeq->pBandCurrent = pSeq->pBandList->pNext;

					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_SS2_START;

					result = setProgress (pSeq, 25, 35, 0);
					if (result != SONY_RESULT_OK){
						return result;
					}
				} else {
					/* Finish scan sequence */
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_FINISH;
				}
				break;

			case BLINDSCAN_SEQ_STATE_SS2_START:
				if (pSeq->pBandCurrent){
					result = sony_demod_dvbs_s2_blindscan_ClearPowerList (&(pSeq->commonParams.storage), pSeq->subseqSS.pPowerList);
					if (result != SONY_RESULT_OK){
						return result;
					}
					result = sony_demod_dvbs_s2_blindscan_subseq_ss_Start(&pSeq->subseqSS,
							pSeq->pBandCurrent->data.band.minFreqKHz,
							pSeq->pBandCurrent->data.band.maxFreqKHz,
							500,
							20000,
							priv->dvbss2PowerSmooth);
					if (result != SONY_RESULT_OK){
						return result;
					}
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_SS2_FIN;
				} else {
					/* Go to next */
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_FS2_START;
				}
				break;

			case BLINDSCAN_SEQ_STATE_SS2_FIN:
				{   
					sony_demod_dvbs_s2_blindscan_data_t * pList;
					sony_demod_dvbs_s2_blindscan_data_t * pLast1;
					sony_demod_dvbs_s2_blindscan_data_t * pLast2;

					result = sony_demod_dvbs_s2_blindscan_AllocData (&pSeq->commonParams.storage, &pList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					/* === For stage 2 === */
					result = sony_demod_dvbs_s2_blindscan_algo_GetCandidateNml (priv,
							&(pSeq->commonParams.storage),
							pSeq->subseqSS.pPowerList,
							100,    /* slice :  1.00dB  */
							5000,   /* min   :  5  MSps */
							20000,  /* max   : 20  MSps */
							1000,   /* cferr :  1  MHz  */
							pList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					result = sony_demod_dvbs_s2_blindscan_algo_ReduceCandidate (&(pSeq->commonParams.storage), pList, pSeq->pDetectedList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					result = sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate (&(pSeq->commonParams.storage), pList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					pLast1 = pSeq->pCandList1;
					while(pLast1->pNext){
						pLast1 = pLast1->pNext;
					}
					pLast1->pNext = pList->pNext;
					pList->pNext = NULL;

					if (pSeq->minSymbolRateKSps <= 5000){
						/* === For stage 3 === */
						result = sony_demod_dvbs_s2_blindscan_algo_GetCandidateNml (priv,
								&(pSeq->commonParams.storage),
								pSeq->subseqSS.pPowerList,
								200,    /* slice :  2.00dB  */
								1000,   /* min   :  1  MSps */
								5000,   /* max   :  5  MSps */
								1000,   /* cferr :  1  MHz  */
								pList);
						if (result != SONY_RESULT_OK){
							return result;
						}

						result = sony_demod_dvbs_s2_blindscan_algo_ReduceCandidate (&(pSeq->commonParams.storage), pList, pSeq->pDetectedList);
						if (result != SONY_RESULT_OK){
							return result;
						}

						result = sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate (&(pSeq->commonParams.storage), pList);
						if (result != SONY_RESULT_OK){
							return result;
						}

						/* to candlist2 */
						pLast2 = pSeq->pCandList2;
						while(pLast2->pNext){
							pLast2 = pLast2->pNext;
						}
						pLast2->pNext = pList->pNext;
						pList->pNext = NULL;
					}
					result = sony_demod_dvbs_s2_blindscan_FreeData (&pSeq->commonParams.storage, pList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					pSeq->pBandCurrent = pSeq->pBandCurrent->pNext;
					pSeq->commonParams.waitTime = 0;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_SS2_START;
				}
				break;

			case BLINDSCAN_SEQ_STATE_FS2_START:
				result = sony_demod_dvbs_s2_blindscan_algo_SortBySymbolrate (&(pSeq->commonParams.storage), pSeq->pCandList1, 20000);
				if (result != SONY_RESULT_OK){
					return result;
				}

				result = setProgress (pSeq, 35, 55, 0);
				if (result != SONY_RESULT_OK){
					return result;
				}

				result = sony_demod_dvbs_s2_blindscan_subseq_fs_Start (&pSeq->subseqFS, pSeq->pCandList1, 4975, 20000, pSeq->pDetectedList);
				if (result != SONY_RESULT_OK){
					return result;
				}

				pSeq->commonParams.waitTime = 0;
				pSeq->seqState = BLINDSCAN_SEQ_STATE_FS2_FIN;
				break;

			case BLINDSCAN_SEQ_STATE_FS2_FIN:
				{   
					sony_demod_dvbs_s2_blindscan_data_t * pCurrent = NULL;
					result = sony_demod_dvbs_s2_blindscan_ClearDataList (&(pSeq->commonParams.storage), pSeq->pCandList1);
					if (result != SONY_RESULT_OK){
						return result;
					}
					pSeq->pCandLast = pSeq->pCandList1;

					result = sony_demod_dvbs_s2_blindscan_algo_SeparateCandidate (&(pSeq->commonParams.storage), pSeq->pCandList2);
					if (result != SONY_RESULT_OK){
						return result;
					}

					result = sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate2 (&(pSeq->commonParams.storage), pSeq->pCandList2);
					if (result != SONY_RESULT_OK){
						return result;
					}

					result = sony_demod_dvbs_s2_blindscan_algo_ReduceCandidate (&(pSeq->commonParams.storage), pSeq->pCandList2, pSeq->pDetectedList);
					if (result != SONY_RESULT_OK){
						return result;
					}

					pSeq->pCandCurrent = pSeq->pCandList2->pNext;
					pSeq->seqState = BLINDSCAN_SEQ_STATE_CS_PREPARING;
					pSeq->commonParams.waitTime = 0;
					result = setProgress (pSeq, 55, 75, 0);
					if (result != SONY_RESULT_OK){
						return result;
					}

					/* For calculate progress */
					pCurrent = pSeq->pCandList2->pNext;
					pSeq->candCount = 1;
					pSeq->candIndex = 1;
					while(pCurrent){
						pSeq->candCount++;
						pCurrent = pCurrent->pNext;
					}
				}
				break;

			case BLINDSCAN_SEQ_STATE_CS_PREPARING:
				if (pSeq->candIndex > pSeq->candCount){
					pSeq->candIndex = pSeq->candCount;
				}
				pSeq->commonParams.progressInfo.minorProgress = (uint8_t)((pSeq->candIndex * 100) / pSeq->candCount);
				if (pSeq->pCandCurrent){
					uint8_t dflg = 0;
					uint32_t detFreqKHz = 0;
					uint32_t detSRKSps = 0;
					uint32_t candFreqKHz = pSeq->pCandCurrent->data.candidate.centerFreqKHz;
					sony_demod_dvbs_s2_blindscan_data_t * pCurrent = pSeq->pDetectedList->pNext;
					while(pCurrent){
						detFreqKHz = pCurrent->data.channelInfo.centerFreqKHz;
						detSRKSps = pCurrent->data.channelInfo.symbolRateKSps;
						if (((detFreqKHz - (detSRKSps/2)) <= candFreqKHz) && (candFreqKHz <= (detFreqKHz + (detSRKSps/2)))){
							dflg = 1;
							break;
						}
						pCurrent = pCurrent->pNext;
					}
					if (dflg == 0){
						pSeq->commonParams.tuneReq.isRequest = 1;
						pSeq->commonParams.tuneReq.frequencyKHz = ((candFreqKHz + 500) / 1000) * 1000;
						pSeq->commonParams.tuneReq.symbolRateKSps = 1000;
						pSeq->commonParams.tuneReq.system = SONY_DTV_SYSTEM_DVBS;
						pSeq->seqState = BLINDSCAN_SEQ_STATE_CS_TUNED;
						pSeq->commonParams.waitTime = 0;
					} else {
						/* Go to next candidate */
						pSeq->candIndex++;
						pSeq->pCandCurrent = pSeq->pCandCurrent->pNext;
						pSeq->seqState = BLINDSCAN_SEQ_STATE_CS_PREPARING;
						pSeq->commonParams.waitTime = 0;
					}
				} else {
					/* Go to next step */
					pSeq->seqState = BLINDSCAN_SEQ_STATE_FS3_START;
					pSeq->commonParams.waitTime = 0;
				}
				break;

			case BLINDSCAN_SEQ_STATE_CS_TUNED:
				{   
					result = sony_demod_dvbs_s2_blindscan_subseq_cs_Start (&(pSeq->subseqCS), 0);
					if (result != SONY_RESULT_OK){
						return result;
					}
					pSeq->seqState = BLINDSCAN_SEQ_STATE_CS_FIN;
				}
				break;

			case BLINDSCAN_SEQ_STATE_CS_FIN:
				if (pSeq->subseqCS.isExist){
					sony_demod_dvbs_s2_blindscan_data_t * pTemp = NULL;
					result = sony_demod_dvbs_s2_blindscan_AllocData (&(pSeq->commonParams.storage), &pTemp);
					if (result != SONY_RESULT_OK){
						return result;
					}
					pTemp->data.candidate.centerFreqKHz = pSeq->commonParams.tuneReq.frequencyKHz;
					pTemp->data.candidate.symbolRateKSps = pSeq->subseqCS.coarseSymbolRateKSps;
					pTemp->data.candidate.minSymbolRateKSps = (pSeq->subseqCS.coarseSymbolRateKSps * 700) / 1000;
					pTemp->data.candidate.maxSymbolRateKSps = ((pSeq->subseqCS.coarseSymbolRateKSps * 1600) + 999) / 1000;
					pSeq->pCandLast->pNext = pTemp;
					pSeq->pCandLast = pSeq->pCandLast->pNext;
				}
				/* Go to next candidate */
				pSeq->candIndex++;
				pSeq->pCandCurrent = pSeq->pCandCurrent->pNext;
				pSeq->seqState = BLINDSCAN_SEQ_STATE_CS_PREPARING;
				pSeq->commonParams.waitTime = 0;
				break;

			case BLINDSCAN_SEQ_STATE_FS3_START:
				result = sony_demod_dvbs_s2_blindscan_algo_DeleteDuplicate (&(pSeq->commonParams.storage), pSeq->pCandList1);
				if (result != SONY_RESULT_OK){
					return result;
				}

				result = setProgress (pSeq, 75, 100, 0);
				if (result != SONY_RESULT_OK){
					return result;
				}

				result = sony_demod_dvbs_s2_blindscan_subseq_fs_Start(&pSeq->subseqFS, pSeq->pCandList1, 1000, 5000, pSeq->pDetectedList);
				if (result != SONY_RESULT_OK){
					return result;
				}

				pSeq->seqState = BLINDSCAN_SEQ_STATE_FINISH;
				pSeq->commonParams.waitTime = 0;
				break;

			case BLINDSCAN_SEQ_STATE_FINISH:
				/* Finish */
				pSeq->isContinue = 0;
				pSeq->commonParams.waitTime = 0;
				result = setProgress (pSeq, 100, 100, 100);
				if (result != SONY_RESULT_OK){
					return result;
				}
				break;

			default:
				return SONY_RESULT_ERROR_SW_STATE;
		}
	}
	return result;
}

int cxd2841er_blind_scan(struct dvb_frontend* fe,
		u32 min_khz, u32 max_khz, u32 min_sr, u32 max_sr,
		bool do_power_scan,
		void (*callback)(void *data), void *arg)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	uint32_t elapsedTime = 0;
	sony_result_t result = SONY_RESULT_OK;
	sony_demod_dvbs_s2_blindscan_seq_t *pSeq = NULL;
	sony_stopwatch_t stopwatch;
	sony_integ_dvbs_s2_blindscan_result_t blindscanResult;

	dev_dbg(&priv->i2c->dev, "%s(): khz min/max=%d/%d sr min/max=%d/%d\n",
			__func__, min_khz, max_khz, min_sr, max_sr);

	blindscanResult.callback_arg = arg;

	/* brief blind scan description:
	 * get AGC values and prepare candidate list
	 * iterate candidates and search symbol rate (SRS)
	 * if locked then callback will be called with found parameters
	 * periodic callback percentage report
	 * */

	/* TODO: args sanity check */

	pSeq = kzalloc(sizeof(sony_demod_dvbs_s2_blindscan_seq_t), GFP_KERNEL);
	if (!pSeq)
		return -ENOMEM;
	memset(pSeq, 0, sizeof(pSeq));

	/* init values required for blind scan */
	priv->blind_scan_cancel = 0;
	priv->dvbss2PowerSmooth = 1; /* 1: normal, 7: 64 times smoother than normal */

	dev_dbg(&priv->i2c->dev, "%s(): init\n", __func__); 
	result = cxd2841er_blind_scan_init(fe, do_power_scan, pSeq, min_khz, max_khz, min_sr, max_sr);
	if (result != SONY_RESULT_OK)
		return -EINVAL;

	dev_dbg(&priv->i2c->dev, "%s(): start main cycle \n", __func__); 
	while(pSeq->isContinue){
		/* Check cancellation. */
		if (priv->blind_scan_cancel)
			return  (result);

		if (pSeq->commonParams.waitTime == 0){
			/* Execute one sequence */
			result = sony_demod_dvbs_s2_blindscan_seq_Sequence (pSeq);
			if (result != SONY_RESULT_OK){
				dev_info(&priv->i2c->dev, "%s(): seq_Sequence failed \n", __func__); 
				return  (result);
			}

			/* Start stopwatch */
			result = sony_stopwatch_start (&stopwatch);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			if (pSeq->commonParams.agcInfo.isRequest){

				/* Clear request flag. */
				pSeq->commonParams.agcInfo.isRequest = 0;

				result = sony_tuner_helene_sat_AGCLevel2AGCdB(pSeq->commonParams.agcInfo.agcLevel,
						&(pSeq->commonParams.agcInfo.agc_x100dB));
				if (result != SONY_RESULT_OK){
					dev_info(&priv->i2c->dev, "%s(): AGCLevel2AGCdB failed \n", __func__); 
					return  (result);
				}
				dev_dbg(&priv->i2c->dev, "%s(): AGCLevel2AGCdB=%d agcLevel=%d\n",
						__func__, pSeq->commonParams.agcInfo.agc_x100dB, pSeq->commonParams.agcInfo.agcLevel); 
			}

			if (pSeq->commonParams.tuneReq.isRequest){
				uint32_t symbolRateKSps = pSeq->commonParams.tuneReq.symbolRateKSps;

				/* Clear request flag. */
				pSeq->commonParams.tuneReq.isRequest = 0;

				/* Symbol rate */
				if (symbolRateKSps == 0) {
					/* Symbol rate setting for power spectrum */
					symbolRateKSps = 45000;
				}

				dev_dbg(&priv->i2c->dev, "aospan:RFtune %d kHz system=%d symbolRateKSps=%d \n",
						pSeq->commonParams.tuneReq.frequencyKHz,
						pSeq->commonParams.tuneReq.system,
						symbolRateKSps);

				/* RF Tune */
				if (pSeq->commonParams.tuneReq.system == SONY_DTV_SYSTEM_DVBS2) 
						fe->dtv_property_cache.delivery_system = SYS_DVBS2;
				else
						fe->dtv_property_cache.delivery_system = SYS_DVBS;
				fe->dtv_property_cache.symbol_rate = symbolRateKSps*1000;
				fe->dtv_property_cache.frequency = pSeq->commonParams.tuneReq.frequencyKHz * 1000;

				if ((priv->flags & CXD2841ER_USE_GATECTRL) && fe->ops.i2c_gate_ctrl)
					fe->ops.i2c_gate_ctrl(fe, 1);
				if (fe->ops.tuner_ops.set_params)
					fe->ops.tuner_ops.set_params(fe);
				if ((priv->flags & CXD2841ER_USE_GATECTRL) && fe->ops.i2c_gate_ctrl)
					fe->ops.i2c_gate_ctrl(fe, 0);
				msleep(40);

				pSeq->commonParams.tuneReq.frequencyKHz = ((pSeq->commonParams.tuneReq.frequencyKHz + 2) / 4) * 4;
			}

			if (pSeq->commonParams.powerInfo.isPower){
				pSeq->commonParams.powerInfo.isPower = 0;
				/* Prepare callback information.(Detected channel) */
				blindscanResult.eventId = SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_POWER;
				blindscanResult.pPowerList = pSeq->commonParams.powerInfo.pPowerList;

				/* Callback */
				callback((void*)&blindscanResult);
			}

			if (pSeq->commonParams.detInfo.isDetect){

				/* Clear detect flag */
				pSeq->commonParams.detInfo.isDetect = 0;

				/* Prepare callback information.(Detected channel) */
				blindscanResult.eventId = SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT;
				blindscanResult.tuneParam.system = pSeq->commonParams.detInfo.system;
				blindscanResult.tuneParam.centerFreqKHz = pSeq->commonParams.detInfo.centerFreqKHz;
				blindscanResult.tuneParam.symbolRateKSps = pSeq->commonParams.detInfo.symbolRateKSps;

				/* TS output enable */
				result = sony_demod_dvbs_s2_blindscan_SetTSOut (priv, 1);
				if (result != SONY_RESULT_OK){
					dev_info(&priv->i2c->dev, "%s(): SetTSOut failed \n", __func__); 
					return  (result);
				}

				/* Callback */
				callback((void*)&blindscanResult);

				/* TS output disable */
				result = sony_demod_dvbs_s2_blindscan_SetTSOut (priv, 0);
				if (result != SONY_RESULT_OK){
					dev_info(&priv->i2c->dev, "%s(): SetTSOut(0) failed \n", __func__); 
					return  (result);
				}
			}

			{   
				/* Progress calculation */
				uint8_t progress = 0;
				uint8_t rangeMin = pSeq->commonParams.progressInfo.majorMinProgress;
				uint8_t rangeMax = pSeq->commonParams.progressInfo.majorMaxProgress;
				uint8_t minorProgress = pSeq->commonParams.progressInfo.minorProgress;
				progress = rangeMin + (((rangeMax - rangeMin) * minorProgress) / 100);

				if (pSeq->commonParams.progressInfo.progress < progress){
					pSeq->commonParams.progressInfo.progress = progress;
					/* Prepare callback information.(Progress) */
					blindscanResult.eventId = SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS;
					blindscanResult.progress = progress;
					/* Callback */
					callback((void*)&blindscanResult);
				}
			}
		} else {
			/* waiting */
			result = sony_stopwatch_sleep (&stopwatch, 10 /* poll every 10 msec */);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			result = sony_stopwatch_elapsed (&stopwatch, &elapsedTime);
			if (result != SONY_RESULT_OK){
				return  (result);
			}

			if(elapsedTime > pSeq->commonParams.waitTime){
				pSeq->commonParams.waitTime = 0;
			}
		}
	}

	// TODO: sleep
#if 0
	result = pInteg->pTunerSat->Sleep(pInteg->pTunerSat);
	if (result != SONY_RESULT_OK){
		return  (result);
	}

	result = sony_demod_dvbs_s2_Sleep (pSeq->commonParams.pDemod);
	if (result != SONY_RESULT_OK){
		return  (result);
	}
#endif

	return (result);
}

int cxd2841er_blind_scan_cancel(struct dvb_frontend* fe)
{
	struct cxd2841er_priv *priv = fe->demodulator_priv;

	priv->blind_scan_cancel = 1;
	return 0;
}

