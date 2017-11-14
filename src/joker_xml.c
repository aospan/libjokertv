/* 
 * process input XML file with lock instructions (frequencies, etc)
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
#include <joker_xml.h>
#include <joker_fpga.h>
#include "u_drv_tune.h"
#include <libxml/xmlreader.h>

static int delivery_system = 0;


void write_out(struct joker_t *joker, char *buf)
{
	if (joker->csv_out_filename_fd)
		fwrite(buf, strlen(buf), 1, joker->csv_out_filename_fd);
	else
		printf("%s", buf);
}

void write_stat(struct joker_t * joker, int delivery_system, int frequency_mhz)
{
	struct stat_t *stat = NULL;
	char buf[512];

	if (!joker)
		return;

	stat = &joker->stat;

	snprintf(buf, 512, "\"%d\",\"%d\",\"%lld\",\"%.3f\",\"%.3f\"\n",
			delivery_system, frequency_mhz,
			(long long)stat->avg_ucblocks/stat->avg_count,
			(double)stat->avg_rf_level/(stat->avg_count*1000),
			(double)stat->avg_snr/(stat->avg_count*1000));
	write_out(joker, buf);
}

static void do_elements(struct joker_t * joker, xmlNode * a_node)
{
	xmlNode *cur_node = NULL;
	int bandwidth = 0;
	int frequency_mhz = 0;
	int modulation = 0;
	struct tune_info_t info;
	int timeout = 0;
	struct stat_t *stat = NULL;

	if (!joker)
		return;

	stat = &joker->stat;

	memset(&info, 0, sizeof(struct tune_info_t));

	for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			xmlAttr* attribute = cur_node->properties;
			while(attribute)
			{
				xmlChar* value = xmlNodeListGetString(cur_node->doc, attribute->children, 1);
				if (!strcasecmp(cur_node->name, "delivery_system") &&
						!strcasecmp(attribute->name,"standard")) {
					delivery_system = atoi(value);
				}else if (!strcasecmp(cur_node->name, "freq")) {
					if (!strcasecmp(attribute->name, "bandwidth")) {
						bandwidth = atoi(value);
					} else if (!strcasecmp(attribute->name, "frequency_mhz")) {
						frequency_mhz = atoi(value);
					} else if (!strcasecmp(attribute->name, "modulation")) {
						modulation = atoi(value);
					}
				}
				xmlFree(value); 
				attribute = attribute->next;
			}

			if (!strcasecmp(cur_node->name, "freq")) {
				jdebug("Locking to freq=%d bw=%d mod=%d delsys=%d \n",
					frequency_mhz, bandwidth, modulation, delivery_system);

				info.delivery_system = (enum joker_fe_delivery_system)delivery_system;
				info.bandwidth_hz = bandwidth;
				info.frequency = frequency_mhz * 1000000;
				info.modulation = (enum joker_fe_modulation)modulation;

				// clear avg values before
				stat->avg_rf_level = 0;
				stat->avg_snr = 0;
				stat->avg_ucblocks = 0;
				stat->avg_count = 0;

				if (tune(joker, &info)) {
					printf("Tuning error. Exit.\n");
					return;
				}

				timeout = 10; /* 1.0 sec */
				while (timeout--) {
					if (joker->stat.status == JOKER_LOCK) {
						printf("LOCKED \n");
						sleep(2);
						// save collected avg values
						if (stat->avg_count > 0 )
							write_stat(joker, delivery_system, frequency_mhz);
						break;
					}
					usleep(1000*100);
				}

			}
		}

		do_elements(joker, cur_node->children);
	}
}

void status_callback_xml(void *data)
{
	struct joker_t *joker= (struct joker_t *)data;
	struct stat_t *stat = NULL;

	if (!joker)
		return;

	stat = &joker->stat;
	jdebug("XML INFO: status=%d (%s) ucblocks=%d, rflevel=%.3f dBm, SNR %.3f dB, BER %.2e, quality %d \n",
			stat->status, stat->status == JOKER_LOCK ? "LOCK" : "NOLOCK",
			stat->ucblocks, (double)stat->rf_level/1000, (double)stat->snr/1000,
			(double)stat->bit_error/stat->bit_count,
			stat->signal_quality);

	// update collected stat if LOCKed
	if (stat->status == JOKER_LOCK) {
		// skip first reported values because
		// demod not stable yet and can report incorrect values
		if (stat->avg_count > 0) {
			stat->avg_rf_level += stat->rf_level;
			stat->avg_snr += stat->snr;
			stat->avg_ucblocks += stat->ucblocks;
		}
		stat->avg_count++;
	}

	stat->refresh_ms = 100;
}

/* process input XML
 * return 0 if success
 */
int joker_process_xml(struct joker_t * joker)
{
	int ret = 0;
	xmlDoc *doc = NULL;
	xmlNode *root_element = NULL;

	if (!joker || !joker->xml_in_filename)
		return -EINVAL;

	/*
	 * this initialize the library and check potential ABI mismatches
	 * between the version it was compiled for and the actual shared
	 * library used.
	 */
	LIBXML_TEST_VERSION

	doc = xmlReadFile(joker->xml_in_filename, NULL, 0);
	if (!doc)
		return -EINVAL;

	root_element = xmlDocGetRootElement(doc);

	// open output file
	joker->csv_out_filename_fd = fopen(joker->csv_out_filename, "w+");
	write_out(joker, "delivery_system,frequency_mhz,uncorrected_blocks,rf_level_dbm,snr_db\n");

	joker->status_callback = &status_callback_xml;
	do_elements(joker, root_element);

	fclose(joker->csv_out_filename_fd);
	joker->csv_out_filename_fd = 0;

	xmlFreeDoc(doc);
	xmlCleanupParser();

	return ret;
}
