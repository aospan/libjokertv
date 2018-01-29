/*
 * cxd2841er_blind_scan.h
 *
 * Sony CXD2441ER digital demodulator blind scan
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

#ifndef CXD2841ER_BLIND_SCAN_H
#define CXD2841ER_BLIND_SCAN_H

#include <linux/dvb/frontend.h>
#include "cxd2841er.h"

/*  Default value is 1220(MHz).
*   It is 940MHz - 2160MHz. ( with some margin )
*/
#define SONY_DEMOD_DVBS_S2_BLINDSCAN_SEARCH_RANGE_MHZ    1220
#define SONY_DEMOD_DVBS_S2_BLINDSCAN_DATA_MAX           10000
#define SONY_DEMOD_DVBS_S2_BLINDSCAN_POWER_MAX ((SONY_DEMOD_DVBS_S2_BLINDSCAN_SEARCH_RANGE_MHZ + 45) * 20)

/* Return codes */
typedef enum {
	SONY_RESULT_OK,              /**< Function was successfully actioned */
	SONY_RESULT_ERROR_ARG,       /**< Invalid argument (maybe software bug) */
	SONY_RESULT_ERROR_I2C,       /**< I2C communication error */
	SONY_RESULT_ERROR_SW_STATE,  /**< Invalid software state */
	SONY_RESULT_ERROR_HW_STATE,  /**< Invalid hardware state */
	SONY_RESULT_ERROR_TIMEOUT,   /**< Timeout occurred */
	SONY_RESULT_ERROR_UNLOCK,    /**< Failed to lock */
	SONY_RESULT_ERROR_RANGE,     /**< Out of range */
	SONY_RESULT_ERROR_NOSUPPORT, /**< Not supported for current device */
	SONY_RESULT_ERROR_CANCEL,    /**< The operation is cancelled */
	SONY_RESULT_ERROR_OTHER,     /**< Unspecified error */
	SONY_RESULT_ERROR_OVERFLOW,  /**< Memory overflow */
	SONY_RESULT_OK_CONFIRM       /**< Tune was successful, but confirm parameters */
} sony_result_t; 

typedef enum {
        BLINDSCAN_SEQ_STATE_SPECTRUM,       /**< Obtain spectrum */
        BLINDSCAN_SEQ_STATE_SPECTRUM_SAVE,  /**< Save spectrum */
        BLINDSCAN_SEQ_STATE_START,          /**< Start */
        BLINDSCAN_SEQ_STATE_SS1_FIN,        /**< SS1 fin */
        BLINDSCAN_SEQ_STATE_STAGE1_FIN,     /**< Stage1 fin */
        BLINDSCAN_SEQ_STATE_SS2_START,      /**< SS2 start */
        BLINDSCAN_SEQ_STATE_SS2_FIN,        /**< SS2 fin */
        BLINDSCAN_SEQ_STATE_FS2_START,      /**< FS2 start */
        BLINDSCAN_SEQ_STATE_FS2_FIN,        /**< FS2 fin */
        BLINDSCAN_SEQ_STATE_CS_PREPARING,   /**< CS preparing */
        BLINDSCAN_SEQ_STATE_CS_TUNED,       /**< CS tuned */
        BLINDSCAN_SEQ_STATE_CS_FIN,         /**< CS fin */
        BLINDSCAN_SEQ_STATE_FS3_START,      /**< FS3 start */
        BLINDSCAN_SEQ_STATE_FINISH,         /**< Finish */
        BLINDSCAN_SEQ_STATE_UNKNOWN         /**< Unknown */
} sony_demod_dvbs_s2_blindscan_seq_state_t;

typedef enum {
	SONY_DTV_SYSTEM_UNKNOWN,        /**< Unknown. */
	SONY_DTV_SYSTEM_DVBT,           /**< DVB-T. */
	SONY_DTV_SYSTEM_DVBT2,          /**< DVB-T2. */
	SONY_DTV_SYSTEM_DVBC,           /**< DVB-C. */
	SONY_DTV_SYSTEM_DVBC2,          /**< DVB-C2. */
	SONY_DTV_SYSTEM_DVBS,           /**< DVB-S. */
	SONY_DTV_SYSTEM_DVBS2,          /**< DVB-S2. */
	SONY_DTV_SYSTEM_ISDBT,          /**< ISDB-T. */
	SONY_DTV_SYSTEM_ISDBS,          /**< ISDB-S. */
	SONY_DTV_SYSTEM_ANY             /**< Used for multiple system scanning / blind tuning */
} sony_dtv_system_t;

typedef enum {
	SONY_DVBS_CODERATE_1_2,             /**< 1/2 */
	SONY_DVBS_CODERATE_2_3,             /**< 2/3 */
	SONY_DVBS_CODERATE_3_4,             /**< 3/4 */
	SONY_DVBS_CODERATE_5_6,             /**< 5/6 */
	SONY_DVBS_CODERATE_7_8,             /**< 7/8 */
	SONY_DVBS_CODERATE_INVALID          /**< Invalid */
} sony_dvbs_coderate_t;

typedef enum {
	SONY_DVBS2_CODERATE_1_4,                /**< 1/4 */
	SONY_DVBS2_CODERATE_1_3,                /**< 1/3 */
	SONY_DVBS2_CODERATE_2_5,                /**< 2/5 */
	SONY_DVBS2_CODERATE_1_2,                /**< 1/2 */
	SONY_DVBS2_CODERATE_3_5,                /**< 3/5 */
	SONY_DVBS2_CODERATE_2_3,                /**< 2/3 */
	SONY_DVBS2_CODERATE_3_4,                /**< 3/4 */
	SONY_DVBS2_CODERATE_4_5,                /**< 4/5 */
	SONY_DVBS2_CODERATE_5_6,                /**< 5/6 */
	SONY_DVBS2_CODERATE_8_9,                /**< 8/9 */
	SONY_DVBS2_CODERATE_9_10,               /**< 9/10 */
	SONY_DVBS2_CODERATE_RESERVED_29,        /**< Reserved */
	SONY_DVBS2_CODERATE_RESERVED_30,        /**< Reserved */
	SONY_DVBS2_CODERATE_RESERVED_31,        /**< Reserved */
	SONY_DVBS2_CODERATE_INVALID             /**< Invalid */
} sony_dvbs2_coderate_t;

typedef enum {
	SONY_DVBS2_MODULATION_QPSK,             /**< QPSK */
	SONY_DVBS2_MODULATION_8PSK,             /**< 8PSK */
	SONY_DVBS2_MODULATION_16APSK,           /**< 16APSK */
	SONY_DVBS2_MODULATION_32APSK,           /**< 32APSK */
	SONY_DVBS2_MODULATION_RESERVED_29,      /**< Reserved */
	SONY_DVBS2_MODULATION_RESERVED_30,      /**< Reserved */
	SONY_DVBS2_MODULATION_RESERVED_31,      /**< Reserved */
	SONY_DVBS2_MODULATION_DUMMY_PLFRAME,    /**< Dummy PL Frame */
	SONY_DVBS2_MODULATION_INVALID           /**< Invalid */
} sony_dvbs2_modulation_t;

typedef struct {
	sony_dvbs2_modulation_t modulation; /**< Modulation. */
	sony_dvbs2_coderate_t codeRate;     /**< Code rate. */
	uint8_t isShortFrame;               /**< FEC Frame size (0:normal, 1:short). */
	uint8_t isPilotOn;                  /**< Pilot mode (0:Off, 1:On). */
} sony_dvbs2_plscode_t;

typedef enum {
	SONY_DEMOD_SAT_IQ_SENSE_NORMAL = 0,   /**< I/Q normal sense. */
	SONY_DEMOD_SAT_IQ_SENSE_INV           /**< I/Q inverted. */
} sony_demod_sat_iq_sense_t;

typedef enum {
	SONY_DVBS2_STREAM_GENERIC_PACKETIZED = 0x00,   /**< Generic Packetized Stream Input. */
	SONY_DVBS2_STREAM_GENERIC_CONTINUOUS = 0x01,   /**< Generic Continuous Stream Input. */
	SONY_DVBS2_STREAM_RESERVED = 0x02,             /**< Reserved. */
	SONY_DVBS2_STREAM_TRANSPORT = 0x03             /**< Transport Stream Input. */
} sony_dvbs2_stream_t;

typedef enum {
	SONY_DVBS2_ROLLOFF_35 = 0x00,           /**< Roll-off = 0.35. */
	SONY_DVBS2_ROLLOFF_25 = 0x01,           /**< Roll-off = 0.25. */
	SONY_DVBS2_ROLLOFF_20 = 0x02,           /**< Roll-off = 0.20. */
	SONY_DVBS2_ROLLOFF_RESERVED = 0x03      /**< Reserved. */
} sony_dvbs2_rolloff_t;

typedef struct {
	sony_dvbs2_stream_t streamInput;            /**< Stream Input (Transport or Generic). */
	uint8_t isSingleInputStream;                /**< Multiple Input Stream or Single Input Stream. */
	uint8_t isConstantCodingModulation;         /**< Constant or Adaptive Coding and Modulation. */
	uint8_t issyIndicator;                      /**< Input Stream Synchronisation Indicator. */
	uint8_t nullPacketDeletion;                 /**< Null Packet Deletion active/not active. */
	sony_dvbs2_rolloff_t rollOff;               /**< Transmission Roll-Off factor. */
	uint8_t inputStreamIdentifier;              /**< Input Stream Identifier (only valid for MIS). */
	uint16_t userPacketLength;                  /**< User Packet Length in bits. */
	uint16_t dataFieldLength;                   /**< Data Field Length in bits. */
	uint8_t syncByte;                           /**< User Packet Sync Byte. */
} sony_dvbs2_bbheader_t;

/*
 *  The demodulator Chip ID mapping.
 */
typedef enum {
	SONY_DEMOD_CHIP_ID_CXD2837 = 0xB1,  /**< CXD2837  DVB-T/T2/C */
	SONY_DEMOD_CHIP_ID_CXD2839 = 0xA5,  /**< CXD2839  DVB-S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2841 = 0xA7,  /**< CXD2841  DVB-T/T2/C/C2/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2842 = 0xA5,  /**< CXD2842  DVB-T/T2/C/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2843 = 0xA4,  /**< CXD2843  DVB-T/T2/C/C2 */
	SONY_DEMOD_CHIP_ID_CXD2844 = 0xC5,  /**< CXD2844  DVB-T/T2/C/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2846 = 0xC7,  /**< CXD2846  DVB-T/T2/C/C2/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2853 = 0xC9,  /**< CXD2853  DVB-T/C/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2854 = 0xC1,  /**< CXD2854  DVB-T/T2/C/C2/S/S2. ISDB-T/S */
	SONY_DEMOD_CHIP_ID_CXD2855 = 0xC3,  /**< CXD2855  DVB-T/T2/C. ISDB-T */
	SONY_DEMOD_CHIP_ID_CXD2856 = 0xCA,  /**< CXD2856  DVB-T/C/C2/S/S2 */
	SONY_DEMOD_CHIP_ID_CXD2857 = 0xCC,  /**< CXD2857  DVB-C/C2 */
} sony_demod_chip_id_t;

/*
 *  This macro is used to determine that 2k12 Generation Demod chip is used.
 */
#define SONY_DEMOD_CHIP_ID_2k12_GENERATION(chipId) ((chipId == SONY_DEMOD_CHIP_ID_CXD2837) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2839) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2841) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2842) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2843))

/*
 *  This macro is used to determine that 2k14 Generation Demod chip is used.
 */
#define SONY_DEMOD_CHIP_ID_2k14_GENERATION(chipId) ((chipId == SONY_DEMOD_CHIP_ID_CXD2844) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2846) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2853) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2854) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2855) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2856) || \
		(chipId == SONY_DEMOD_CHIP_ID_CXD2857))

/*** Data storage ***/
typedef struct sony_demod_dvbs_s2_blindscan_power_t {
	struct sony_demod_dvbs_s2_blindscan_power_t * pNext;/**< Pointer of next item. */
	uint32_t freqKHz;                                   /**< Frequency in KHz. */
	int32_t power;                                      /**< Power data in dB x 100 */
} sony_demod_dvbs_s2_blindscan_power_t;

typedef struct sony_demod_dvbs_s2_blindscan_data_t {
	struct sony_demod_dvbs_s2_blindscan_data_t * pNext; /**< The pointer of next item. */
	union data_t {
		/** Power data. */
		struct power_t {
			uint32_t freqKHz;           /**< Frequency in KHz. */
			int32_t power;              /**< Power in dB x 100. */
		} power;

		/** Band information data. */
		struct band_t {
			uint32_t minFreqKHz;        /**< Min frequency in KHz. */
			uint32_t maxFreqKHz;        /**< Max frequency in KHz. */
		} band;

		/** Candidate data. */
		struct candidate_t {
			uint32_t centerFreqKHz;     /**< Center frequency in KHz. */
			uint32_t symbolRateKSps;    /**< Target symbol rate in KSps. */
			uint32_t minSymbolRateKSps; /**< Minimum symbol rate in range of candidate in KSps. */
			uint32_t maxSymbolRateKSps; /**< Maximum symbol rate in range of candidate in KSps. */
		} candidate;

		/** Channel data. */
		struct channel_t {
			uint32_t centerFreqKHz;     /**< Center frequency in KHz. */
			uint32_t symbolRateKSps;    /**< Symbol rate in KSps. */
			sony_dtv_system_t system;   /**< System. */
		} channelInfo;
	} data;
} sony_demod_dvbs_s2_blindscan_data_t;

typedef struct {
	sony_demod_dvbs_s2_blindscan_power_t availablePowerList; /**< List of available power data. */
	sony_demod_dvbs_s2_blindscan_power_t * pPowerArrayTop;   /**< Top of power data array. */
	int32_t powerArrayLength;                                /**< Length of power data array. */

	sony_demod_dvbs_s2_blindscan_data_t availableDataList;   /**< List of available data. */
	sony_demod_dvbs_s2_blindscan_data_t * pDataArrayTop;     /**< Top of data array. */
	int32_t dataArrayLength;                                 /**< Length of data array. */

	int32_t currentUsedCount;                                /**< Current used data count. */
	int32_t maxUsedCount;                                    /**< Max used data count. */
	int32_t currentUsedPowerCount;                           /**< Current used power data count. */
	int32_t maxUsedPowerCount;                               /**< Max used power data count. */
} sony_demod_dvbs_s2_blindscan_data_storage_t;

/*** Common ***/
typedef struct {
	char * prefix;
        uint8_t isPower; 
	sony_demod_dvbs_s2_blindscan_data_t * pPowerList;
} sony_demod_dvbs_s2_blindscan_power_info_t;

typedef struct {
	char * prefix;
        uint8_t isCand; 
	sony_demod_dvbs_s2_blindscan_data_t * pCandList;
} sony_demod_dvbs_s2_blindscan_cand_info_t;

typedef struct {
        uint8_t isDetect;  /* 0: Not detect, 1: Detect */
        sony_dtv_system_t system; /* The system of the detected channel */
        uint32_t centerFreqKHz; /* The center frequency of the detected channel in KHz */
        uint32_t symbolRateKSps; /* The symbol rate of the detected channel in KSps */
} sony_demod_dvbs_s2_blindscan_det_info_t;

typedef struct {
        uint8_t isRequest; /* If this is "1(Request)", driver request to tuning */
        uint32_t frequencyKHz; /* The frequency to tune in KHz */
        sony_dtv_system_t system; /* The system to tune */
        uint32_t symbolRateKSps; /* The symbol rate to tune in KSps */
} sony_demod_dvbs_s2_blindscan_tune_req_t;

typedef struct {
        uint8_t isRequest;  /**< Calculate request flag. */
        uint32_t agcLevel;  /**< AGC level get from demodulator. */
        int32_t agc_x100dB; /**< The result of calculation. */
} sony_demod_dvbs_s2_blindscan_agc_info_t;

typedef struct {
        uint8_t progress;           /**< Total progress in "%". */
        uint8_t majorMinProgress;   /**< Current min range in "%". */
        uint8_t majorMaxProgress;   /**< Current max range in "%". */
        uint8_t minorProgress;      /**< Current sub progress in "%". */
} sony_demod_dvbs_s2_blindscan_progress_t;

typedef struct {
	struct cxd2841er_priv *priv;
	struct dvb_frontend* fe;
        uint32_t waitTime;                                      /**< Wait time in ms */
        sony_demod_dvbs_s2_blindscan_cand_info_t candInfo;    /**< Candidates information */
        sony_demod_dvbs_s2_blindscan_power_info_t powerInfo;    /**< Power information for spectrum draw */
        sony_demod_dvbs_s2_blindscan_det_info_t detInfo;        /**< Detected channel information */
        sony_demod_dvbs_s2_blindscan_tune_req_t tuneReq;        /**< Request to tune information */
        sony_demod_dvbs_s2_blindscan_agc_info_t agcInfo;        /**< Request to calculate information */
        sony_demod_dvbs_s2_blindscan_data_storage_t storage;    /**< Data storage */
        sony_demod_dvbs_s2_blindscan_progress_t progressInfo;   /**< Progress information */
        uint32_t ckalFreqKHz;                                   /**< CKAL in KHz */
        uint32_t ckahFreqKHz;                                   /**< CKAH in KHz */
} sony_demod_dvbs_s2_blindscan_seq_common_t;

typedef struct sony_stopwatch_t {
	uint64_t startTime; // usecs
} sony_stopwatch_t;

typedef enum {
	SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT,    /**< Detect channel. */
	SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS,  /**< Update progress. */
	SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_POWER,	/**< Power info for spectrum draw */
	SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_CAND		/**< Candidates info */
} sony_integ_dvbs_s2_blindscan_event_id_t;

typedef struct {
	sony_dtv_system_t system;       /**< System of the channel. */
	uint32_t centerFreqKHz;         /**< Center frequency in kHz of the DVB-S/S2 channel. */
	uint32_t symbolRateKSps;        /**< Symbol rate in kHz of the DVB-S/S2 channel. */
} sony_dvbs_s2_tune_param_t;

typedef struct {
	    sony_integ_dvbs_s2_blindscan_event_id_t eventId;
	    /* If "eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_DETECT", this value is valid. */
	    sony_dvbs_s2_tune_param_t tuneParam;
	    sony_demod_dvbs_s2_blindscan_data_t* pPowerList;      /**< Power list. */
	    sony_demod_dvbs_s2_blindscan_data_t* pCandList;      /**< Cand list. */
	    /*
	     *                  If "eventId == SONY_INTEG_DVBS_S2_BLINDSCAN_EVENT_PROGRESS", this value is valid.
	     *                              The range of this value is 0 - 100 in percentage.
	     */
	    uint8_t progress;
	    char *prefix;
	    void *callback_arg;
}sony_integ_dvbs_s2_blindscan_result_t;

/*** SS sub sequence ***/
typedef enum {
	SS_STATE_INIT,              /**< Initialized */
	SS_STATE_START,             /**< Start */
	SS_STATE_RF_TUNING,         /**< RF tuning */
	SS_STATE_RF_TUNED,          /**< RF tuned */
	SS_STATE_AGC_CALCULATED,    /**< AGC calculating */
	SS_STATE_WAIT_CSFIN,        /**< Wait for CSFIN */
	SS_STATE_READ_CS,           /**< Read CS data */
	SS_STATE_END,               /**< End */
	SS_STATE_UNKNOWN            /**< Unknown */
} sony_demod_dvbs_s2_blindscan_ss_state_t;

typedef struct {
	sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams;

	uint8_t isEnable;                                       /**< Enable flag (0: disable, 1: enable). */
	sony_demod_dvbs_s2_blindscan_ss_state_t state;          /**< State. */
	sony_stopwatch_t stopwatch;                             /**< Stopwatch for internal use. */

	uint32_t minFreqKHz;                                    /**< Min frequency in KHz. */
	uint32_t maxFreqKHz;                                    /**< Max frequency in KHz. */
	uint32_t stepFreqKHz;                                   /**< Resolution in KHz. */
	uint32_t tunerStepFreqKHz;                              /**< RF step frequency in KHz. */

	uint32_t currentFreqKHz;                                /**< Current frequency in KHz. */
	uint32_t agcLevel;                                      /**< AGC level from register. */
	int32_t agc_x100dB;                                     /**< AGC (x 100dB) calculated by tuner driver. */

	sony_demod_dvbs_s2_blindscan_power_t * pPowerList;      /**< Power list. */
	sony_demod_dvbs_s2_blindscan_power_t * pPowerListLast;  /**< The last item of power list. */

	uint8_t ocfr_csk;
	sony_demod_dvbs_s2_blindscan_power_t * pAve1;
	sony_demod_dvbs_s2_blindscan_power_t * pAve2;

} sony_demod_dvbs_s2_blindscan_subseq_ss_t;

/*** PM sub sequence ***/
typedef enum {
	PM_STATE_START,         /**< Start */
	PM_STATE_WAITING_CSFIN, /**< Wait for CS fin */
	PM_STATE_UNKNOWN        /**< Unknown */
} sony_demod_dvbs_s2_blindscan_pm_state_t;

typedef struct {
	sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams;

	uint8_t isEnable;                               /**< Enable flag (0: disable, 1: enable). */
	sony_demod_dvbs_s2_blindscan_pm_state_t state;  /**< State. */
	sony_stopwatch_t stopwatch;                     /**< Stopwatch for internal use. */

	int32_t freqOffsetKHz;                          /**< Frequency offset in KHz. */
	uint16_t power;                                 /**< Power value */
} sony_demod_dvbs_s2_blindscan_subseq_pm_t;

/*** CS sub sequence ***/
typedef enum {
	CS_STATE_START,             /**< Start */
	CS_STATE_PEAK_SEARCH_START, /**< Peak search start */
	CS_STATE_PEAK_SEARCHING,    /**< Peak searching */
	CS_STATE_LOWER_SEARCHING,   /**< Lower edge searching */
	CS_STATE_UPPER_SEARCHING,   /**< Upper edge searching */
	CS_STATE_UNKNOWN            /**< Unknown */
} sony_demod_dvbs_s2_blindscan_cs_state_t;

typedef struct {
	sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams;
	sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeqPM;

	uint8_t isEnable;                               /**< Enable flag (0: disable, 1: enable). */
	sony_demod_dvbs_s2_blindscan_cs_state_t state;  /**< State. */
	uint32_t agcLevel;                              /**< AGC level from register. */
	int32_t agc_x100dB;                             /**< Calculated AGC (x 100dB) by tuner driver. */
	int8_t index;                                   /**< Index for internal use. */
	int32_t peakPower;                              /**< Peak power. */
	int32_t peakFreqOfsKHz;                         /**< Peak frequency offset in KHz. */
	int32_t lowerFreqKHz;                           /**< Lower frequency in KHz. */
	int32_t upperFreqKHz;                           /**< Upper frequency in KHz. */
	uint8_t isFin;                                  /**< Flags whether this sequence finished or not. */

	int32_t freqOffsetKHz;                          /**< Frequency offset in KHz. */

	uint8_t isExist;                                /**< Result of this CS sub sequence (0 : Signal is not exist, 1 : Signal is exist) */
	uint32_t coarseSymbolRateKSps;                  /**< Result of this CS sub sequence (Symbol rate in KSps) */

} sony_demod_dvbs_s2_blindscan_subseq_cs_t;

/*** BT sub sequence ***/
typedef enum {
        BT_STATE_INIT,          /**< Initialized */
        BT_STATE_START,         /**< Started */
        BT_STATE_RF_TUNED,      /**< RF tuned */
        BT_STATE_WAIT_SRSFIN,   /**< Wait for SRS fin */
        BT_STATE_WAIT_TSLOCK,   /**< Wait for TS lock */
        BT_STATE_WAIT_TSLOCK2,  /**< Wait for TS lock (2nd) */
        BT_STATE_UNKNOWN        /**< Unknown state */
} sony_demod_dvbs_s2_blindscan_bt_state_t;

typedef struct {
        /**
          @brief Pointer of common parameters used by each sub sequence
          */
        sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams;

        uint8_t isEnable;                               /**< Enable flag (0: disable, 1: enable) */
        sony_demod_dvbs_s2_blindscan_bt_state_t state;  /**< State */
        sony_stopwatch_t stopwatch;                     /**< Stopwatch used in this sequence */
        uint32_t timeout;                               /**< Timeout time in ms. */

        uint32_t centerFreqKHz;                         /**< Center frequency in KHz set by main sequence. */
        uint32_t candSymbolRateKSps;                    /**< Candidate symbol rate in KSps set by main sequence. */
        uint32_t minCandSymbolRateKSps;                 /**< Min symbol rate of candidate in KSps set by main sequence. */
        uint32_t maxCandSymbolRateKSps;                 /**< Max symbol rate of candidate in KSps set by main sequence. */
        uint32_t minSymbolRateKSps;                     /**< Min symbol rate of search range set by main sequence. */
        uint32_t maxSymbolRateKSps;                     /**< Max symbol rate of search range set by main sequence. */

        /* Result of BT sequence. */
        uint8_t isLocked;                               /**< Lock flag (0 : Not TS locked, 1: TS locked) */
        uint32_t detSymbolRateSps;                      /**< Detected symbol rate in Sps. */
        int32_t detCarrierOffsetKHz;                    /**< Detected carrier offset frequency in KHz. */
        sony_dtv_system_t detSystem;                    /**< Detected system (DVB-S/S2) */
} sony_demod_dvbs_s2_blindscan_subseq_bt_t;

/*** FS sub sequence ***/
typedef enum {
	FS_STATE_START,         /**< Start */
	FS_STATE_WAIT_BTFIN,    /**< Wait for BT fin */
	FS_STATE_FINISH,        /**< Finish FS */
	FS_STATE_UNKNOWN        /**< Unknown */
} sony_demod_dvbs_s2_blindscan_fs_state_t;

typedef struct {
	sony_demod_dvbs_s2_blindscan_seq_common_t * pCommonParams;
	sony_demod_dvbs_s2_blindscan_subseq_bt_t * pSeqBT;

	uint8_t isEnable;                                   /**< Enable flag (0: disable, 1: enable). */
	sony_demod_dvbs_s2_blindscan_fs_state_t state;      /**< State. */

	sony_demod_dvbs_s2_blindscan_data_t * pCandList;    /**< Candidate list */
	sony_demod_dvbs_s2_blindscan_data_t * pChannelList; /**< Detected channel list */
	uint32_t minSymbolRateKSps;                         /**< Min symbol rate of search range in KSps */
	uint32_t maxSymbolRateKSps;                         /**< Max symbol rate of search range in KSps */

	sony_demod_dvbs_s2_blindscan_data_t * pCandCurrent; /**< Pointer of current candidate */
	sony_demod_dvbs_s2_blindscan_data_t * pChannelLast; /**< The last pionter of detected channel list */
	uint32_t candFreq;                                  /**< Frequency of candidate in KHz. */

	uint32_t candCount;                                 /**< Candidate number for calculate progress. */
	uint32_t candIndex;                                 /**< Candidate index for calculate progress. */

} sony_demod_dvbs_s2_blindscan_subseq_fs_t;


typedef struct {
        sony_demod_dvbs_s2_blindscan_seq_common_t commonParams;
        uint8_t isContinue; /* 0 - finish, 1 - continue */
        sony_demod_dvbs_s2_blindscan_seq_state_t seqState;    /**< State of BlindScan sequence. */
        sony_demod_dvbs_s2_blindscan_subseq_bt_t subseqBT;    /**< Data for sub sequence BT. */
        sony_demod_dvbs_s2_blindscan_subseq_pm_t subseqPM;    /**< Data for sub sequence PM. */
        sony_demod_dvbs_s2_blindscan_subseq_ss_t subseqSS;    /**< Data for sub sequence SS. */
        sony_demod_dvbs_s2_blindscan_subseq_fs_t subseqFS;    /**< Data for sub sequence FS. */
        sony_demod_dvbs_s2_blindscan_subseq_cs_t subseqCS;    /**< Data for sub sequence CS. */

        uint32_t minFreqKHz;        /**< Min frequency in KHz which set by BlindScan API */
        uint32_t maxFreqKHz;        /**< Max frequency in KHz which set by BlindScan API */
        uint32_t minSymbolRateKSps; /**< Min symbol rate in KSps which set by BlindScan API */
        uint32_t maxSymbolRateKSps; /**< Max symbol rate in KSps which set by BlindScan API */

        uint32_t minPowerFreqKHz;   /**< Min frequency in KHz which need for power spectrum */
        uint32_t maxPowerFreqKHz;   /**< Max frequency in KHz which need for power spectrum */

        sony_demod_dvbs_s2_blindscan_data_t * pDetectedList;    /**< Detected channel list */

        sony_demod_dvbs_s2_blindscan_data_t * pBandList;        /**< Band list */
        sony_demod_dvbs_s2_blindscan_data_t * pBandCurrent;     /**< Current processing band data */

        sony_demod_dvbs_s2_blindscan_data_t * pCandList1;       /**< Candidate list 1 */
        sony_demod_dvbs_s2_blindscan_data_t * pCandList2;       /**< Candidate list 2 */
        sony_demod_dvbs_s2_blindscan_data_t * pCandCurrent;     /**< The pointer of candidate */
        sony_demod_dvbs_s2_blindscan_data_t * pCandLast;        /**< The last pointer of candidate list */

        uint32_t candCount; /**< Candidate total count for calculate progress */
        uint32_t candIndex; /**< Candidate current number for calculate progress */

        sony_demod_dvbs_s2_blindscan_power_t powerArray[SONY_DEMOD_DVBS_S2_BLINDSCAN_POWER_MAX];
        sony_demod_dvbs_s2_blindscan_data_t dataArray[SONY_DEMOD_DVBS_S2_BLINDSCAN_DATA_MAX];
	bool do_power_scan; /* callback call with power data */
} sony_demod_dvbs_s2_blindscan_seq_t;

typedef struct {
        /* in kHz, example: 950000 */
        uint32_t minFreqKHz;
        /* in kHz, example: 2150000 */
        uint32_t maxFreqKHz;
        /* in kS, example: 1000 */
        uint32_t minSymbolRateKSps;
        /* in kS, example: 45000 */
        uint32_t maxSymbolRateKSps;
} sony_integ_dvbs_s2_blindscan_param_t;

sony_result_t sony_stopwatch_start (sony_stopwatch_t * pStopwatch);
sony_result_t sony_stopwatch_elapsed (sony_stopwatch_t * pStopwatch, uint32_t* pElapsed);
sony_result_t sony_demod_dvbs_s2_blindscan_SetSampleMode (struct cxd2841er_priv *priv,
		uint8_t isHSMode);
sony_result_t sony_demod_dvbs_s2_blindscan_subseq_pm_Start (sony_demod_dvbs_s2_blindscan_subseq_pm_t * pSeq,
		int32_t freqOffsetKHz);
sony_result_t sony_demod_dvbs_s2_blindscan_PS_START (struct cxd2841er_priv *priv);
sony_result_t sony_demod_dvbs_s2_blindscan_GetCSFIN (struct cxd2841er_priv *priv,
		uint8_t * pCSFIN);
sony_result_t sony_demod_dvbs_s2_blindscan_PS_RACK (struct cxd2841er_priv *priv);
sony_result_t sony_demod_dvbs_s2_blindscan_GetPSPow (struct cxd2841er_priv *priv,
		uint16_t * pPower);
sony_result_t sony_demod_dvbs_s2_blindscan_SetCFFine (struct cxd2841er_priv *priv,
                                                      int32_t freqOffsetKHz);
int cxd2841er_blind_scan(struct dvb_frontend* fe,
		u32 min_khz, u32 max_khz, u32 min_sr, u32 max_sr,
		bool do_power_scan,
		void (*callback)(void *data), void *arg);

#endif
