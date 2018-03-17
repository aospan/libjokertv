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
#include <getopt.h>

#include <queue>
#include "joker_tv.h"
#include "joker_fpga.h"
#include "joker_ci.h"
#include "joker_en50221.h"
#include "joker_spi.h"
#include "joker_ts.h"
#include "joker_utils.h"
#include "joker_ts_filter.h"
#include "joker_xml.h"
#include <libxml/xmlreader.h>
#include "u_drv_tune.h"
#include "u_drv_data.h"
#include "joker_blind_scan.h"

// status & statistics callback
// will be called periodically after 'tune' call
void status_callback_f(void *data)
{
	struct joker_t *joker= (struct joker_t *)data;
	struct stat_t *stat = NULL;

	if (!joker)
		return;
		
	stat = &joker->stat;
	printf("INFO: status=%d (%s) ucblocks=%d, rflevel=%.3f dBm, SNR %.3f dB, BER %.2e, quality %d \n", 
			stat->status, stat->status == JOKER_LOCK ? "LOCK" : "NOLOCK",
			stat->ucblocks, (double)stat->rf_level/1000, (double)stat->snr/1000,
			(double)stat->bit_error/stat->bit_count,
			stat->signal_quality);

	// less heavy refresh if status locked
	if (stat->status == JOKER_LOCK)
		stat->refresh_ms = 2000;
	else
		stat->refresh_ms = 500;
}

// this callback will be called when new service name arrived
void service_name_update(struct program_t *program)
{
	struct program_es_t *es = NULL;
	struct program_ca_t *ca = NULL;
	printf("callback:%s program number=%d name=%s provider=%s type=0x%x. video:%s audio:%s\n",
			__func__, program->number, program->name, 
			program->provider_name, program->service_type,
			program->has_video ? "yes" : "",
			program->has_audio ? "yes" : "");

	if(!list_empty(&program->es_list)) {
		list_for_each_entry(es, &program->es_list, list) {
			printf("	ES pid=0x%x type=0x%x lang=%s\n",
					es->pid, es->type, es->lang);
		}
	}

	if(!list_empty(&program->ca_list)) {
		list_for_each_entry(ca, &program->ca_list, list) {
			printf("	CA pid=0x%x caid=0x%x\n",
					ca->pid, ca->caid);
		}
	}

}

static const int convert2xml_pol[] = { 18, 13 };

// blind scan callback
void blind_scan_callback(void *data)
{
	blind_scan_res_t * res = (blind_scan_res_t *)data;
	struct program_t *program = NULL, *tmp = NULL;
	struct program_es_t*es = NULL;
	struct program_ca_t*ca = NULL;
	struct joker_t *joker = NULL;
	struct big_pool_t *pool;
	unsigned char * name_esc = NULL;
	unsigned char * provider_name_esc = NULL;
	joker_nit_t * nit = NULL;

	if (!res)
		return;

	joker = res->joker;
	pool = joker->pool;

	if (res->event_id == EVENT_DETECT) {
		// save found transponder and programs to file
		if (res->joker->blind_programs_filename_fd) {
			// transponder
			// convert values to satellites xml format
			fprintf(res->joker->blind_programs_filename_fd,
					"\t\t<transponder frequency=\"%lld\" symbol_rate=\"%d\" symbol_rate_raw=\"%d\" "
					"polarization=\"%d\" fec_inner=\"%d\" system=\"%d\" modulation=\"%d\">\n",
					(long long)res->info->frequency, res->info->symbol_rate_rounded,
					res->info->symbol_rate,
					res->info->voltage == JOKER_SEC_VOLTAGE_13 ? 1 : 0,
					res->info->coderate,
					res->info->delivery_system == JOKER_SYS_DVBS ? 0 : 1,
					res->info->modulation);

			fprintf(res->joker->blind_programs_filename_fd,
					"\t\t\t<sdt network_id=\"%d\" ts_id=\"%d\"/>\n",
						pool->network_id, pool->ts_id);

			// programs belongs to this transponder
			list_for_each_entry_safe(program, tmp, res->programs, list) {
				name_esc = xmlEncodeEntitiesReentrant (NULL, program->name);
				provider_name_esc = xmlEncodeEntitiesReentrant (NULL, program->name);
				fprintf(res->joker->blind_programs_filename_fd,
						"\t\t\t<program number=\"%d\" name=\"%s\" "
						"provider=\"%s\" pmt_pid=\"%d\" pcr_pid=\"%d\">\n",
						program->number,
						name_esc,
						provider_name_esc,
						program->pmt_pid, program->pcr_pid);
				xmlFree(name_esc);
				xmlFree(provider_name_esc);
				if(!list_empty(&program->es_list)) {
					list_for_each_entry(es, &program->es_list, list) {
						fprintf(res->joker->blind_programs_filename_fd,
						"\t\t\t\t<pid id=\"%d\" type=\"0x%x\" lang=\"%s\"/>\n",
								es->pid, es->type, es->lang);
					}
				}
				if(!list_empty(&program->ca_list)) {
					list_for_each_entry(ca, &program->ca_list, list) {
						fprintf(res->joker->blind_programs_filename_fd,
						"\t\t\t\t<ca pid=\"%d\" caid=\"0x%x\"/>\n",
								ca->pid, ca->caid);
					}
				}
				fprintf(res->joker->blind_programs_filename_fd,
						"\t\t\t</program>\n");
			}

			// dump CAID's from CAT
			if (pool) {
				if(!list_empty(&pool->ca_list)) {
					fprintf(joker->blind_programs_filename_fd,
						"\t\t\t<cat>\n");
					list_for_each_entry(ca, &pool->ca_list, list) {
						fprintf(res->joker->blind_programs_filename_fd,
								"\t\t\t\t<ca caid=\"0x%x\" pid=\"0x%x\"/>\n",
								ca->caid, ca->pid);
					}
					fprintf(joker->blind_programs_filename_fd,
						"\t\t\t</cat>\n");
				}
			}

			// dump NIT info
			fprintf(res->joker->blind_programs_filename_fd, "\t\t\t<nit network_name=\"%s\" network_id=\"%d\">\n",
					pool->network_name, pool->nit_network_id);
			if(!list_empty(&pool->nit_list)) {
				list_for_each_entry(nit, &pool->nit_list, list) {
					fprintf(res->joker->blind_programs_filename_fd,
							"\t\t\t\t<ts tsid=\"%d\" onid=\"%d\"/>\n",
							nit->ts_id, nit->orig_network_id);
				}
			}
			fprintf(res->joker->blind_programs_filename_fd, "\t\t\t</nit>\n");

			// transponder footer
			fprintf(res->joker->blind_programs_filename_fd, "\t\t</transponder>\n");
			fflush(res->joker->blind_programs_filename_fd);
		}

		printf("%s: found transponder freq=%lld\n", __func__, (long long)res->info->frequency);

		list_for_each_entry_safe(program, tmp, res->programs, list)
			printf("Program number=%d name=%s\n", program->number, program->name);
	} else if (res->event_id == EVENT_PROGRESS) {
		printf("progress=%.2d%%\n", res->progress);
	}
}

// CAM module info callback
// will be called when CAM module info available
void ci_info_callback_f(void *data)
{
	struct joker_t *joker= (struct joker_t *)data;
	struct joker_ci_t * ci = NULL;
	int i = 0;

	if (!joker || !joker->joker_ci_opaque)
		return;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* print CAM info */
	printf("  CAM info: \n");
	printf("  Application type: %02x\n", ci->application_type);
	printf("  Application manufacturer: %04x\n", ci->application_manufacturer);
	printf("  Manufacturer code: %04x\n", ci->manufacturer_code);
	printf("  Menu string: %s\n", ci->menu_string);
	printf("  Info string: %s\n", ci->cam_infostring);
}

// CAM module supported CAIDs callback
// will be called when CAM module supported CAIDs available
void ci_caid_callback_f(void *data)
{
	struct joker_t *joker= (struct joker_t *)data;
	struct joker_ci_t * ci = NULL;
	int i = 0;

	if (!joker || !joker->joker_ci_opaque)
		return;
	ci = (struct joker_ci_t *)joker->joker_ci_opaque;

	/* print supported CAIDs */
	printf("  Supported CAIDs: \n");
	for (i = 0; i < CAID_MAX_COUNT; i++) {
		if (!ci->ca_ids[i])
			break; /* end of list detected */
		printf("    CAID: %.4x\n", ci->ca_ids[i]);
	}
}

void show_help() {
	printf("joker-tv usage: \n");
	printf("	-d delsys	Delivery system. Options: \n \
			%d-ATSC  %d-DVB-S  %d-DVB-S2 %d-DVB-C %d-DVB-T %d-DVB-T2 %d-ISDB-T %d-DTMB %d-J.83B\n", 
			JOKER_SYS_ATSC, JOKER_SYS_DVBS, JOKER_SYS_DVBS2, JOKER_SYS_DVBC_ANNEX_A,
			JOKER_SYS_DVBT, JOKER_SYS_DVBT2, JOKER_SYS_ISDBT, JOKER_SYS_DTMB, JOKER_SYS_DVBC_ANNEX_B);
	printf("	-m modulation	Modulation. Options: \n \
			%d-VSB8 (for ATSC) %d-QPSK %d-8PSK %d-QAM64 %d-QAM256\n",
			JOKER_VSB_8, JOKER_QPSK, JOKER_PSK_8, JOKER_QAM_64, JOKER_QAM_256);
	printf("	-f freq		Frequency in Hz. Example: 1402000000\n");
	printf("	-s symbol_rate	Symbol rate. Options: 0-AUTO. Example: 20000000\n");
	printf("	-y voltage	LNB voltage. Options: 13-Vert/Right, 18-Horiz/Left, 0-OFF. Example: -y 18\n");
	printf("	-b bandwidth	Bandwidth in Hz. Example: 8000000\n");
	printf("	-o filename	Output TS filename. Default: out.ts\n");
	printf("	-t		Enable TS generator. Default: disabled\n");
	printf("	-n		Disable TS data processing. Default: enabled\n");
	printf("	-l limit	Write only limit MB(megabytes) of TS. Default: unlimited\n");
	printf("	-u level	Libusb verbose level (0 - less, 4 - more verbose). Default: 0\n");
	printf("	-w filename	Update firmware on flash. Default: none\n");
	printf("	-p		Decode programs info (DVB PSI tables). Default: no\n");
	printf("	-z l,h,s	LNB settings: low/high/switch frequency. Example: -z 9750,10600,11700\n");
	printf("	-e		Enable 22 kHz tone (continuous). Default: disabled\n");
	printf("	-c		Enable CAM module. Default: disabled\n");
	printf("	-g		Enable TS traffic through CAM module. Default: disabled\n");
	printf("	-q program	Descramble program number using CAM. Multiple programs supported. Example: -q 2 -q 3 -q 4\n");
	printf("	-j		Enable CAM module verbose messages. Default: disabled\n");
	printf("	-i port		TCP port for MMI (CAM) server. Default: 7777\n");
	printf("	-k filename.ts	Send TS traffic to Joker TV. TS will return back (loop) Default: none\n");
	printf("	-r		Send QUERY CA PMT to CAM (check is descrambling possible). Default: disabled\n");
	printf("	--program num	Save only selected programs (not full TS). Example: --program 1 --program 2\n");
	printf("	--in in.xml	XML file with lock instructions. Example: --in ./docs/atsc_north_america_freq.xml \n");
	printf("	--out out.csv	output CSV file with lock results (BER, etc). Example: --out ant1-result.csv \n");
	printf("	--blind		Do blind scan (DVB-S/S2 only). Default: disabled\n");
	printf("	--blind-out file.csv	Write blind scan results to file. Example: blind.csv\n");
	printf("	--blind-power file	Write power (dB) to file. Example: file\n");
	printf("	--blind-save-ts	prefix	Write TS to file. 2MB limit. Default: disabled\n");
	printf("	--blind-save-ts-size MB Write TS to file limit. Default: 2 MBytes\n");
	printf("	--blind-programs file.xml	Write blind scan programs to file. Example: blind.xml\n");
	printf("	--blind-sr-coeff coeff	Symbol rate correction coefficient. Default: %.11f\n", SR_DEFAULT_COEFF);
	printf("	--diseqc diseqc.txt	File with Diseqc commands. One command per line. Scripting supported.\n");
	printf("	--raw-data raw.bin	output raw data received from USB\n");
	printf("	--cam-pcap cam.pcap	dump all CAM interaction to file. Use Wireshark to parse this file.\n");
	exit(0);
}

static struct option long_options[] = {
	{"in",  required_argument, 0, 0},
	{"out",  required_argument, 0, 0},
	{"blind",  no_argument, 0, 0},
	{"program",  required_argument, 0, 0},
	{"diseqc",  required_argument, 0, 0},
	{"blind-out",  required_argument, 0, 0},
	{"blind-sr-coeff",  required_argument, 0, 0},
	{"blind-power",  required_argument, 0, 0},
	{"blind-save-ts",  required_argument, 0, 0},
	{"blind-save-ts-size",  required_argument, 0, 0},
	{"blind-programs",  required_argument, 0, 0},
	{"raw-data",  required_argument, 0, 0},
	{"cam-pcap",  required_argument, 0, 0},
	{ 0, 0, 0, 0}
};

int main (int argc, char **argv)
{
	struct tune_info_t info;
	struct big_pool_t pool;
	int status = 0, ret = 0, rbytes = 0, i = 0;
	struct joker_t * joker = NULL;
	unsigned char buf[JCMD_BUF_LEN];
	unsigned char in_buf[JCMD_BUF_LEN];
	int c, tsgen = 0;
	int delsys = 0, mod = 0, sr = 0, bw = 0;
	uint64_t freq = 0;
	FILE * out = NULL;
	FILE * fd = NULL;
	long size = 0;
	char filename[FNAME_LEN] = "out.ts";
	char fwfilename[FNAME_LEN] = "";
	char infilename[FNAME_LEN] = "";
	char confirm[FNAME_LEN];
	int signal = 0;
	int disable_data = 0;
	struct ts_node * node = NULL;
	unsigned char *res = NULL;
	int64_t total_len = 0, limit = 0;
	struct list_head *programs = NULL;
	struct program_t *program = NULL, *tmp = NULL;
	bool decode_program = false;
	int voltage = 0, tone = 1;
	int ci_server_port = 7777;
	int len = 0;
	int descramble_programs = 0;
	int option_index = 0;
	char datetime[512];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char * diseqc = NULL;
	int diseqc_len = 0;

	strftime(datetime, sizeof(datetime)-1, "%d %b %Y %H:%M", t);

	/* disable output buffering
	 * helps under Windows with stdout delays
	 */
	setbuf(stdout, NULL);

	joker = (struct joker_t *) malloc(sizeof(struct joker_t));
	if (!joker)
		return ENOMEM;
	memset(joker, 0, sizeof(struct joker_t));
	memset(in_buf, 0, JCMD_BUF_LEN);
	memset(buf, 0, JCMD_BUF_LEN);
	memset(&pool, 0, sizeof(struct big_pool_t));

	// set callbacks
	pool.service_name_callback = &service_name_update;
	joker->status_callback = &status_callback_f;
	joker->ci_info_callback = &ci_info_callback_f;
	joker->ci_caid_callback = &ci_caid_callback_f;
	joker->blind_scan_cb = &blind_scan_callback;

	// clear descramble program list
	joker_en50221_descramble_clear(joker);

	INIT_LIST_HEAD(&pool.selected_programs_list);

	while (1) {
		c = getopt_long (argc, argv, "q:k:d:y:z:m:f:s:o:b:l:tpu:w:i:nhecjgr", long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				if (!strcasecmp(long_options[option_index].name, "in")) {
					len = strlen(optarg);
					joker->xml_in_filename = (char*)calloc(1, len + 1);
					strncpy(joker->xml_in_filename, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "out")) {
					len = strlen(optarg);
					joker->csv_out_filename = (char*)calloc(1, len + 1);
					strncpy(joker->csv_out_filename, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "program")) {
					printf("selected program %d \n", atoi(optarg));

					program = (struct program_t*)malloc(sizeof(*program));
					if (!program)
						return -ENOMEM;

					program->joker = joker;
					memset(&program->name, 0, SERVICE_NAME_LEN);
					program->number = atoi(optarg);
					list_add_tail(&program->list, &pool.selected_programs_list);
				}
				if (!strcasecmp(long_options[option_index].name, "blind")) {
					joker->blind_scan = 1;
					delsys = JOKER_SYS_DVBS;
				}
				if (!strcasecmp(long_options[option_index].name, "diseqc")) {
					fd = fopen(optarg, "r+b");
					if (fd <= 0) {
						printf("Can't open diseqc script %s error=%s (%d)\n",
								optarg, strerror(errno), errno);
						return -1;
					}

					/* get file size */
					fseek(fd, 0L, SEEK_END);
					size = ftell(fd);
					fseek(fd, 0L, SEEK_SET);
					if (size > 512*1024) {
						size = 512*1024; // read only 512KB
					}

					joker->diseqc_script = (char*)calloc(1, size);
					if (!joker->diseqc_script)
						return -1;

					joker->diseqc_script_len = fread(joker->diseqc_script, 1, size, fd);
					printf("%d bytes diseqc script read \n", joker->diseqc_script_len);
					fclose(fd);
					fd = NULL;
				}
				if (!strcasecmp(long_options[option_index].name, "blind-out")) {
					len = strlen(optarg);
					joker->blind_out_filename = (char*)calloc(1, len + 1);
					strncpy(joker->blind_out_filename, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "blind-programs")) {
					len = strlen(optarg);
					joker->blind_programs_filename = (char*)calloc(1, len + 1);
					strncpy(joker->blind_programs_filename, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "blind-power")) {
					len = strlen(optarg);
					joker->blind_power_file_prefix = (char*)calloc(1, len + 1);
					strncpy(joker->blind_power_file_prefix, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "blind-save-ts")) {
					len = strlen(optarg);
					joker->blind_ts_file_prefix = (char*)calloc(1, len + 1);
					strncpy(joker->blind_ts_file_prefix, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "blind-save-ts-size")) {
					len = strlen(optarg);
					joker->blind_ts_file_size = atoi(optarg) * 1024 * 1024;
				}
				if (!strcasecmp(long_options[option_index].name, "blind-sr-coeff")) {
					len = strlen(optarg);
					joker->blind_sr_coeff = atof(optarg);
				}
				if (!strcasecmp(long_options[option_index].name, "raw-data")) {
					len = strlen(optarg);
					joker->raw_data_filename = (char*)calloc(1, len + 1);
					strncpy(joker->raw_data_filename, optarg, len);
				}
				if (!strcasecmp(long_options[option_index].name, "cam-pcap")) {
					len = strlen(optarg);
					joker->cam_pcap_filename = (char*)calloc(1, len + 1);
					strncpy(joker->cam_pcap_filename, optarg, len);
				}
				break;
			case 'd':
				delsys = atoi(optarg);
				break;
			case 'y':
				voltage = atoi(optarg);
				break;
			case 'e':
				tone = 0; /* 0 - mean tone on */
				break;
			case 'z':
				sscanf(optarg, "%d,%d,%d", &info.lnb.lowfreq, &info.lnb.highfreq, &info.lnb.switchfreq);
				break;
			case 'm':
				mod = atoi(optarg);
				break;
			case 'f':
				freq = strtoull(optarg, NULL, 10);
				break;
			case 's':
				sr = atoi(optarg);
				break;
			case 'b':
				bw = atoi(optarg);
				break;
			case 'n':
				disable_data = 1;
				break;
			case 't':
				tsgen = 1;
				break;
			case 'p':
				decode_program = 1;
				break;
			case 'u':
				joker->libusb_verbose = atoi(optarg);
				break;
			case 'c':
				joker->ci_enable = 1;
				break;
			case 'g':
				joker->ci_ts_enable = 1;
				break;
			case 'j':
				joker->ci_verbose = 1;
				break;
			case 'r':
				joker->cam_query_send = 1;
				break;
			case 'i':
				ci_server_port = atoi(optarg);
				break;
			case 'q':
				joker_en50221_descramble_add(joker, atoi(optarg));
				descramble_programs = 1;
				break;
			case 'l':
				limit = 1024*1024*atoi(optarg);
				break;
			case 'o':
				strncpy((char*)filename, optarg, FNAME_LEN);
				break;
			case 'k':
				len = strlen(optarg);
				joker->loop_ts_filename = (unsigned char*)calloc(1, len + 1);
				strncpy((char*)joker->loop_ts_filename, optarg, len);
				break;
			case 'w':
				strncpy((char*)fwfilename, optarg, FNAME_LEN);
				break;
			case 'h':
			default:
				show_help();
		}
	}

	// just show help message if nothing selected by user
	if (delsys == JOKER_SYS_UNDEFINED && !tsgen &&
			!joker->loop_ts_filename && !joker->ci_enable &&
			!strlen((const char*)fwfilename) &&
			!joker->xml_in_filename 
			&& !joker->blind_scan)
		show_help();

	/* open Joker TV on USB bus */
	if ((ret = joker_open(joker))) {
		printf("Can't open device \n");
		return ret;
	}
	jdebug("allocated joker=%p \n", joker);

	/* init CI */
	if (joker->ci_enable) {
		joker->ci_server_port = ci_server_port;
		joker_ci(joker);

		// no TS options specified. In this case we sleep forever and only CI will work
		if (delsys == JOKER_SYS_UNDEFINED && !tsgen && !joker->loop_ts_filename)
			while (1)
				sleep(3600);
	}

	/* upgrade fw if selected */
	if(strlen((const char*)fwfilename)) {
		if(joker_flash_checkid(joker)) {
			printf("SPI flash id check failed. Cancelling fw update.\n");
			return -1;
		}
		printf("SPI flash id check success. Please enter 'yes' to continue: ");

		if (!fgets(confirm, FNAME_LEN, stdin))
			return -1;

		if (strncmp(confirm, "yes", 3))
			return -1;

		printf("\nStarting fw update.\n");
		if(joker_flash_write(joker, fwfilename)) {
			printf("Can't write fw to flash !\n");
			return -1;
		} else {
			printf("FW successfully upgraded. Rebooting device ...\n");
			buf[0] = J_CMD_REBOOT;
			if ((ret = joker_cmd(joker, buf, 1, NULL /* in_buf */, 0 /* in_len */))) {
				printf("Can't reboot FPGA\n");
				return ret;
			}
			return 0;
		}
	}

	if(tsgen) {
		/* TS generator selected */
		buf[0] = J_CMD_TS_INSEL_WRITE;
		buf[1] = J_INSEL_TSGEN;
		if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */))) {
			printf("Can't set TS source (TS generator) \n");
			return ret;
		}

	} else if (joker->loop_ts_filename) {
		/* USB bulk selected */
		buf[0] = J_CMD_TS_INSEL_WRITE;
		buf[1] = J_INSEL_USB_BULK;
		if ((ret = joker_cmd(joker, buf, 2, NULL /* in_buf */, 0 /* in_len */))) {
			printf("Can't set TS source (USB bulk) \n");
			return ret;
		}
	} else if (joker->xml_in_filename) {
		// XML in file with lock instructions (list of frequencies, etc)
		return joker_process_xml(joker);
	} else {
		/* real demod selected
		 * tuning ...
		 */
		info.delivery_system = (joker_fe_delivery_system)delsys;
		info.bandwidth_hz = bw;
		info.frequency = freq;
		info.symbol_rate = sr;
		info.modulation = (joker_fe_modulation)mod;
		info.tone = (joker_fe_sec_tone_mode)tone;

		/* set LNB voltage for satellite */
		if (voltage == 13)
			info.voltage = JOKER_SEC_VOLTAGE_13;
		else if (voltage == 18)
			info.voltage = JOKER_SEC_VOLTAGE_18;
		else
			info.voltage = JOKER_SEC_VOLTAGE_OFF;

		if (tune(joker, &info)) {
			printf("Tuning error. Exit.\n");
			return -1;
		}

#if 0
		// Start Motor Driving East E1 31 68 40
		diseqc_len = 4;
		diseqc = (char*)calloc(1, diseqc_len);
		diseqc[0] = 0xE0;
		diseqc[1] = 0x31;
		diseqc[2] = 0x68;
		diseqc[3] = 0x40;
		while (1) {
			send_diseqc_message(joker, diseqc, diseqc_len);
			sleep(2);
		}
#endif

		if (joker->blind_scan) {
			// open out file 
			if (joker->blind_programs_filename) {
				joker->blind_programs_filename_fd = fopen(joker->blind_programs_filename, "w+b");
				if (!joker->blind_programs_filename_fd) {
					printf("Can't open blind scan resulting file %s. Error=%d (%s)\n",
							joker->blind_programs_filename, errno, strerror(errno));
					joker->blind_programs_filename_fd = NULL;
					return -1;
				}

				// write file header
				fprintf(joker->blind_programs_filename_fd,
						"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<satellites>\n");
				fprintf(joker->blind_programs_filename_fd,
						"\t<sat name=\"blindscan\" scan_date=\"%s\">\n", datetime);
				fflush(joker->blind_programs_filename_fd);
			}

			blind_scan(joker, &info);

			// write footer
			if (joker->blind_programs_filename_fd) {
				fprintf(joker->blind_programs_filename_fd, "\t</sat>\n</satellites>\n");
				fclose(joker->blind_programs_filename_fd);
				joker->blind_programs_filename_fd = NULL;
			}
			printf("Blind scan done \n");
			return 0;
		}

		printf("TUNE done \n");
		while (joker->stat.status != JOKER_LOCK)
			usleep(1000*100);
	}

	while(disable_data)
		sleep(3600);

	if (joker->raw_data_filename)
		joker->raw_data_filename_fd = fopen(joker->raw_data_filename, "w+");

	/* start TS collection */
	if((ret = start_ts(joker, &pool))) {
		printf("start_ts failed. err=%d \n", ret);
		exit(-1);
	}

	if (joker->loop_ts_filename) {
		/* start TS file reading thread */
		start_ts_loop(joker);
	}

	if (decode_program || descramble_programs || 
			!list_empty(&pool.selected_programs_list)) {
		/* get TV programs list */
		printf("Trying to get programs list ... \n");
		programs = get_programs(&pool);
		list_for_each_entry_safe(program, tmp, programs, list)
			printf("Program number=%d \n", program->number);
	}

	total_len = save_ts(joker, filename, limit);
	printf("saved %lld bytes. Stopping TS ... \n", (long long)total_len);
	stop_ts(joker, &pool);

	printf("Closing device ... \n");
	joker_close(joker);
	free(joker);
}
