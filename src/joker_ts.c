/* 
 * Joker TV 
 * Transport Stream related stuff
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
#include <iconv.h>
#include <sys/types.h>
#include <joker_tv.h>
#include <joker_ts.h>
#include <joker_utils.h>
#include <u_drv_data.h>

#include <stdbool.h>
#include <dvbpsi.h>
#include <psi.h>
#include <descriptor.h>
#include <pat.h>
#include <pmt.h>
#include <dr.h>
#include <demux.h>
#include <sdt.h>
/*  ATSC PSI Tables */
#include <atsc_eit.h>
#include <atsc_ett.h>
#include <atsc_mgt.h>
#include <atsc_stt.h>
#include <atsc_vct.h>

static void message(dvbpsi_t *handle, const dvbpsi_msg_level_t level, const char* msg)
{
	switch(level)
	{   
		case DVBPSI_MSG_ERROR:
			jdebug("Error: "); break;
		case DVBPSI_MSG_WARN:
			jdebug("Warning: "); break;
		case DVBPSI_MSG_DEBUG:
			jdebug("Debug: "); break;
		default:
			return;
	}
	jdebug("%s\n", msg);
}

// this hook will be called when TS packet with desired pid received
void pmt_hook(void *opaque, unsigned char *pkt)
{
	struct program_t *program = (struct program_t *)opaque;
	jdebug("%s:program=%p pkt=%p\n", __func__, program, pkt);
	if (!program)
		return;

	dvbpsi_packet_push(program->pmt_dvbpsi, pkt);
}

char * parse_type(uint8_t type, int *_audio, int *_video)
{   
	switch (type)
	{
		case 0x00:
			return "Reserved";
		case 0x01:
			*_video = 1;
			return "ISO/IEC 11172 Video";
		case 0x02:
			*_video = 1;
			return "ISO/IEC 13818-2 Video";
		case 0x03:
			*_audio = 1;
			return "ISO/IEC 11172 Audio";
		case 0x04:
			*_audio = 1;
			return "ISO/IEC 13818-3 Audio";
		case 0x05:
			return "ISO/IEC 13818-1 Private Section";
		case 0x06:
			return "ISO/IEC 13818-1 Private PES data packets";
		case 0x07:
			return "ISO/IEC 13522 MHEG";
		case 0x08:
			return "ISO/IEC 13818-1 Annex A DSM CC";
		case 0x09:
			return "H222.1";
		case 0x0A:
			return "ISO/IEC 13818-6 type A";
		case 0x0B:
			return "ISO/IEC 13818-6 type B";
		case 0x0C:
			return "ISO/IEC 13818-6 type C";
		case 0x0D:
			return "ISO/IEC 13818-6 type D";
		case 0x0E:
			return "ISO/IEC 13818-1 auxillary";
		case 0x1B:
			*_video = 1;
			return "AVC video stream as defined in ITU-T Rec. H.264 | ISO/IEC 14496-10 Video";
		case 0x81:
			// from ATSC A/53 Part 3:2013 Service Multiplex and
			// Transport 
			*_audio = 1;
			return "AC-3 Audio";
		case 0x87:
			*_audio = 1;
			return "E-AC-3 Audio";
		default:
			if (type < 0x80)
				return "ISO/IEC 13818-1 reserved";
			else
				return "User Private";
	}
}

/*****************************************************************************
 * DumpPMT
 *****************************************************************************/
static void DumpPMT(void* data, dvbpsi_pmt_t* p_pmt)
{
	struct program_t *program = (struct program_t *)data;
	struct program_es_t*es = NULL;
	int audio = 0, video = 0;
	struct dvbpsi_psi_section_s *current_section = NULL;
	dvbpsi_pmt_es_t* p_es = p_pmt->p_first_es;
	int ignore = 0;

	jdebug(  "\n");
	jdebug(  "New active PMT\n");
	jdebug(  "  program_number : %d\n",
			p_pmt->i_program_number);
	jdebug(  "  version_number : %d\n",
			p_pmt->i_version);
	jdebug(  "  PCR_PID        : 0x%x (%d)\n",
			p_pmt->i_pcr_pid, p_pmt->i_pcr_pid);
	jdebug(  "    | type @ elementary_PID\n");

	while(p_es)
	{
		// avoid duplicates
		ignore = 0;
		if(!list_empty(&program->es_list)) {
			list_for_each_entry(es, &program->es_list, list) {
				if (es->pid == p_es->i_pid)
					ignore = 1; // ignore, already in the list
			}
		}

		if (ignore) {
			p_es = p_es->p_next;
			continue;
		}

		es = (struct program_es_t*)malloc(sizeof(*es));
		if (!es)
			break;

		es->pid = p_es->i_pid;
		es->type = p_es->i_type;

		audio = 0;
		video = 0;
		parse_type(p_es->i_type, &audio, &video);

		if (video)
			program->has_video = 1;

		if (audio)
			program->has_audio = 1;

		list_add_tail(&es->list, &program->es_list);

		jdebug("    | 0x%02x (%s) @ 0x%x (%d)\n",
				p_es->i_type, GetTypeName(p_es->i_type),
				p_es->i_pid, p_es->i_pid);
		p_es = p_es->p_next;
	}

	// get pointer to "raw" PMT (unparsed)
	current_section = ((dvbpsi_t*)program->pmt_dvbpsi)->p_decoder->p_current_section;
	jdebug("%s: p_current_section=%p \n", __func__, current_section);

	// send "raw" PMT to en50221 layer for processing
	joker_en50221_pmt_update(program,
			current_section->p_data, current_section->i_length);

	dvbpsi_pmt_delete(p_pmt);
}

static void DumpPAT(void* data, dvbpsi_pat_t* p_pat)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	int ignore = 0;

	dvbpsi_pat_program_t* p_program = p_pat->p_first_program;
	jdebug(  "\n");
	jdebug(  "New PAT. pool=%p\n", pool);
	jdebug(  "  transport_stream_id : %d\n", p_pat->i_ts_id);
	jdebug(  "  version_number      : %d\n", p_pat->i_version);
	jdebug(  "    | program_number @ [NIT|PMT]_PID\n");
	while(p_program)
	{
		// avoid duplicates
		ignore = 0;
		if(!list_empty(&pool->programs_list)) {
			list_for_each_entry(program, &pool->programs_list, list) {
				if (program->number == p_program->i_number) 
					ignore = 1; // ignore, already in the list
			}
		}

		if (ignore || (p_program->i_number == 0x0 && p_program->i_pid == 0x10 /* NIT */)) {
			p_program = p_program->p_next;
			continue;
		}

		program = (struct program_t*)malloc(sizeof(*program));
		if (!program)
			break;

		program->joker = pool->joker;
		memset(&program->name, 0, SERVICE_NAME_LEN);
		program->number = p_program->i_number;
		INIT_LIST_HEAD(&program->es_list);
		list_add_tail(&program->list, &pool->programs_list);

		jdebug("    | %14d @ 0x%x (%d)\n",
				p_program->i_number, p_program->i_pid, p_program->i_pid);
		program->pmt_pid = p_program->i_pid;

		// attach PMT parser
		program->pmt_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
		if (program->pmt_dvbpsi == NULL) {
			printf("Can't attach PMT pid 0x%x to program 0x%x \n",
					p_program->i_pid, p_program->i_number);
			p_program = p_program->p_next;
			continue;
		}

		if (!dvbpsi_pmt_attach(program->pmt_dvbpsi, p_program->i_number, DumpPMT, program)) {
			dvbpsi_delete(program->pmt_dvbpsi);
			p_program = p_program->p_next;
			continue;
		}

		pool->hooks[p_program->i_pid] = &pmt_hook;
		pool->hooks_opaque[p_program->i_pid] = program;
		p_program = p_program->p_next;
	}
	jdebug(  "  active              : %d\n", p_pat->b_current_next);
	dvbpsi_pat_delete(p_pat);
}

/*****************************************************************************
 * DumpDescriptors
 *****************************************************************************/
static void DumpDescriptors(const char* str, dvbpsi_descriptor_t* p_descriptor)
{ 
	dvbpsi_descriptor_t *p_descriptor_l = p_descriptor;
	// Parse according DVB Document A038 (July 2014)
	while(p_descriptor_l)
	{ 
		printf("	tag=0x%02x : len=%d \n", p_descriptor_l->i_tag, p_descriptor_l->i_length );
		hexdump(p_descriptor_l->p_data, p_descriptor_l->i_length);
		p_descriptor_l = p_descriptor_l->p_next;
	}
};

// get charset name from codepage
// Values used from DVB Document A038 (July 2014)
// Table A.3: Character coding tables
int get_charset_name(uint8_t codepage, char * charset)
{
	if (!charset)
		return -EINVAL;

	// Values used from DVB Document A038 (July 2014)
	// Table A.3: Character coding tables
	switch (codepage) {
		case 0x01:
			// Latin/Cyrillic
			strncpy(charset, "ISO8859-5", SERVICE_NAME_LEN); break;
		case 0x02:
			// Latin/Arabic
			strncpy(charset, "ISO8859-6", SERVICE_NAME_LEN); break;
		case 0x03:
			// Latin/Greek
			strncpy(charset, "ISO8859-7", SERVICE_NAME_LEN); break;
		case 0x04:
			// Latin/Hebrew
			strncpy(charset, "ISO8859-8", SERVICE_NAME_LEN); break;
		case 0x05:
			// Latin alphabet No. 5
			strncpy(charset, "ISO8859-9", SERVICE_NAME_LEN); break;
		case 0x06:
			// Latin alphabet No. 6
			strncpy(charset, "ISO8859-10", SERVICE_NAME_LEN); break;
		case 0x07:
			// Latin/Thai
			strncpy(charset, "ISO8859-11", SERVICE_NAME_LEN); break;
		case 0x09:
			// Latin alphabet No. 7
			strncpy(charset, "ISO8859-13", SERVICE_NAME_LEN); break;
		case 0x0a:
			// Latin alphabet No. 8 (Celtic)
			strncpy(charset, "ISO8859-14", SERVICE_NAME_LEN); break;
		case 0x0b:
			// Latin alphabet No. 9
			strncpy(charset, "ISO8859-15", SERVICE_NAME_LEN); break;
		case 0x11:
		case 0x14: // Big5 subset of ISO/IEC 10646 [16] Traditional Chinese
		case 0x15: // UTF-8 encoding of ISO/IEC 10646 [16] Basic Multilingual Plane (BMP)
			// Basic Multilingual Plane (BMP)
			strncpy(charset, "ISO-10646", SERVICE_NAME_LEN); break;
		case 0x13:
			// Simplified Chinese Character
			strncpy(charset, "GB2312", SERVICE_NAME_LEN); break;
		case 0x00:
		default:
			// default codepage  ISO8859-1
			strncpy(charset, "ISO8859-1", SERVICE_NAME_LEN);
			break;
	}

	return 0;
}

// convert name to utf-8
int to_utf(char * buf, size_t insize, char * _outbuf, int maxlen, char *charset)
{
	iconv_t cd;
	char outbuf[maxlen];
	char * outptr = &outbuf[0];
	char *inbuf = buf;
	size_t nconv = 0, avail = maxlen;

	if (!charset || !buf || maxlen <= 0)
		return -EINVAL;

	memset(outbuf, 0x0, maxlen);

	cd = iconv_open ("UTF-8", charset);
	if (cd == (iconv_t) -1)
	{
		printf("can't open iconv for charset conversion\n");
		return -EIO;
	}


	nconv = iconv (cd, &inbuf, &insize, &outptr, &avail);
	if (nconv == -1)
		printf("iconv conversion may be failed. But we use result anyway ... \n");

	jdebug("iconv: charset=%s insize=%zd avail=%zd nconv=%zd \n",
			charset, insize, avail, nconv );
	// copy result 
	memset(_outbuf, 0, maxlen);
	memcpy(_outbuf, outbuf, maxlen - avail);
	iconv_close (cd);
}

/* convert name to utf-8
 * first byte can be used as codepage */
int dvb_to_utf(char * buf, size_t insize, char * _outbuf, int maxlen)
{
	unsigned char charset[SERVICE_NAME_LEN];
	unsigned char *final_inbuf = buf;

	memset(&charset[0], 0, SERVICE_NAME_LEN);

	if(buf[0] > 0 && buf[0] < 0x20) {
		if (get_charset_name(buf[0], charset))
			return -ENOENT;

		final_inbuf = buf + 1; // skip first byte
		insize = insize - 1;
	} else {
		// default latin
		strncpy(charset, "ISO6937", SERVICE_NAME_LEN);
	}

	return to_utf(final_inbuf, insize, _outbuf, maxlen, charset);
}

static void get_service_name(struct program_t *program, dvbpsi_descriptor_t* p_descriptor)
{ 
	int i = 0, off = 0, service_provider_name_length = 0, service_name_length = 0;
	unsigned char *service_name_ptr = NULL;
	uint8_t codepage = 0;
	unsigned char charset[SERVICE_NAME_LEN];

	memset(&program->name, 0, SERVICE_NAME_LEN);
	memset(&charset[0], 0, SERVICE_NAME_LEN);
	
	// Parse according DVB Document A038 (July 2014)
	// Specification for Service Information (SI)
	// in DVB systems)
	while(p_descriptor)
	{ 
		if (p_descriptor->i_tag == 0x48 /* Table 12. service_descriptor */) {
			// 6.2.33 Service descriptor
			// Table 86: Service descriptor
			//	byte0 - service_type
			//	byte1 - service_provider_name_length
			//		0 ... N - provider_name
			//	byteN + 2 - service_name_length
			//		0 ... N - service_name
			program->service_type = p_descriptor->p_data[0];
			service_provider_name_length = p_descriptor->p_data[1];
			service_name_length = p_descriptor->p_data[service_provider_name_length + 2];
			service_name_ptr = p_descriptor->p_data + service_provider_name_length + 3;

			jdebug("service_type=%d \n", service_type );

			// sanity check
			if (!service_name_length)
				return;

			// Text fields can optionally start with non-spacing,
			// non-displayed data which specifies the
			// alternative character table to be used for the
			// remainder of the text item.
			// If the first byte of the text field has a value in the range "0x20" to "0xFF"
			// then this and all subsequent bytes in the text
			// item are coded using the default character coding table (table 00 - Latin alphabet) of figure A.1. 
			if (service_name_ptr[0] >= 0x20 && service_name_ptr[0] <= 0xFF)
				codepage = 0x0; // ISO8859-1
			else
				codepage = service_name_ptr[0];

			jdebug("provider_len=%d service_name_length=%d service_name_ptr=%d codepage=0x%x\n",
					service_provider_name_length, service_name_length, service_provider_name_length + 3, codepage);
			for (i = isprint(service_name_ptr[0])?0:1; i < service_name_length; i++) {
				// special chars. ignore it
				if (service_name_ptr[i] >= 0x80 && service_name_ptr[i] <= 0x8B)
					continue;

				program->name[off] = service_name_ptr[i];
				off++;
			}
			jdebug("program=%d new name=%s \n", program->number, program->name);

			// convert name to utf-8
			if (!get_charset_name(codepage, &charset[0]))
				to_utf(program->name, off, program->name, SERVICE_NAME_LEN, charset);
		}
		p_descriptor = p_descriptor->p_next;
	}
};



/*****************************************************************************
 * DumpSDT
 *****************************************************************************/
static void DumpSDT(void* data, dvbpsi_sdt_t* p_sdt)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	dvbpsi_sdt_service_t* p_service = p_sdt->p_first_service;

	jdebug(  "\n");
	jdebug(  "New active SDT\n");
	jdebug(  "  ts_id : %d\n",
			p_sdt->i_extension);
	jdebug(  "  version_number : %d\n",
			p_sdt->i_version);
	jdebug(  "  network_id        : %d\n",
			p_sdt->i_network_id);

	while(p_service)
	{
		jdebug("service_id=0x%02x\n", p_service->i_service_id);
		// DumpDescriptors("	", p_service->p_first_descriptor);
		if(!list_empty(&pool->programs_list)) {
			list_for_each_entry(program, &pool->programs_list, list) {
				if (program->number == p_service->i_service_id) {
					get_service_name(program, p_service->p_first_descriptor);

					// call service name callback with new name
					if (pool->service_name_callback)
						pool->service_name_callback(program);
				}
			}
		}

		p_service = p_service->p_next;
	}
	dvbpsi_sdt_delete(p_sdt);
}

/* example from real ATSC stream (575MHz Miami, FL)
   ATSC VCT: Virtual Channel Table
   Version number : 1
   Current next   : yes
   Protocol version: 0
Type : Terrestrial Virtual Channel Table
| Short name  : 
| Major number: 6
| Minor number: 1
| Modulation  : ATSC (8 VSB) — The virtual channel uses the 8-VSB modulation method conforming to A/53 Part 2 [2].
| Carrier     : 0
| Transport id: 631
| Program number: 3
| ETM location: No ETM
| Scrambled   : no
| Path Select : yes
| Out of band : yes
| Hidden      : no
| Hide guide  : no
| Service type: 2
| Source id   : 3
|  ] 0xa1 : "<E0>1^C^B<E0>1^@^@^@<81><E0>4eng<81><E0>5spa" (User Private)
*/
static void DumpAtscVCTChannels(dvbpsi_atsc_vct_channel_t *p_vct_channels, struct big_pool_t *pool)
{
	struct program_t *program = NULL;
	dvbpsi_atsc_vct_channel_t *p_channel = p_vct_channels;

	while (p_channel)
	{   
		jdebug("\n");
		jdebug("\t  | Short name  : %s\n", p_channel->i_short_name);
		jdebug("\t  | Major number: %d\n", p_channel->i_major_number);
		jdebug("\t  | Minor number: %d\n", p_channel->i_minor_number);
		jdebug("\t  | Modulation  : %d\n", p_channel->i_modulation);
		jdebug("\t  | Carrier     : %d\n", p_channel->i_carrier_freq);
		jdebug("\t  | Transport id: %d\n", p_channel->i_channel_tsid);
		jdebug("\t  | Program number: %d\n", p_channel->i_program_number);
		jdebug("\t  | ETM location: %d\n", p_channel->i_etm_location);
		jdebug("\t  | Scrambled   : %s\n", p_channel->b_access_controlled ? "yes" : "no");
		jdebug("\t  | Path Select : %s\n", p_channel->b_path_select ? "yes" : "no");
		jdebug("\t  | Out of band : %s\n", p_channel->b_out_of_band ? "yes" : "no");
		jdebug("\t  | Hidden      : %s\n", p_channel->b_hidden ? "yes" : "no");
		jdebug("\t  | Hide guide  : %s\n", p_channel->b_hide_guide ? "yes" : "no");
		jdebug("\t  | Service type: %d\n", p_channel->i_service_type);
		jdebug("\t  | Source id   : %d\n", p_channel->i_source_id);

		jdebug("i_program_number=0x%02x\n", p_channel->i_program_number);
		if(!list_empty(&pool->programs_list)) {
			list_for_each_entry(program, &pool->programs_list, list) {
				if (program->number == p_channel->i_program_number) {
					// ATSC A/65:2013 Program and System
					// Information Protocol
					// short_name – The name of the virtual
					// channel, represented as a sequence of
					// one to seven 16-bit
					// code values interpreted in accordance
					// with the UTF-16 representation of
					// Unicode character
					// data. 
					memcpy(program->name, p_channel->i_short_name, 14);
					to_utf(program->name, 14, program->name, SERVICE_NAME_LEN, "UTF-16BE");

					// call service name callback with new name
					if (pool->service_name_callback)
						pool->service_name_callback(program);
				}
			}
		}
		p_channel = p_channel->p_next;
	}
}

static void handle_atsc_VCT(void* data, dvbpsi_atsc_vct_t *p_vct)
{
	struct big_pool_t *pool = (struct big_pool_t *)data;
	jdebug("\n");
	jdebug("  ATSC VCT: Virtual Channel Table\n");

	jdebug("\tVersion number : %d\n", p_vct->i_version);
	jdebug("\tCurrent next   : %s\n", p_vct->b_current_next ? "yes" : "no");
	jdebug("\tProtocol version: %d\n", p_vct->i_protocol); /* PSIP protocol version */
	jdebug("\tType : %s Virtual Channel Table\n", (p_vct->b_cable_vct) ? "Cable" : "Terrestrial" );

	DumpAtscVCTChannels(p_vct->p_first_channel, pool);
	dvbpsi_atsc_DeleteVCT(p_vct);
}

/*****************************************************************************
 * NewSubtable
 *****************************************************************************/
static void NewSubtable(dvbpsi_t *p_dvbpsi, uint8_t i_table_id, uint16_t i_extension,
		void * data)
{
	jdebug("%s: new i_table_id=0x%x\n", __func__, i_table_id);
	switch (i_table_id) {
		case 0x42: // SDT
			if (!dvbpsi_sdt_attach(p_dvbpsi, i_table_id, i_extension, DumpSDT, data))
				fprintf(stderr, "Failed to attach SDT subdecoder\n");
			break;
		case 0xC8: // ATSC VCT
		case 0xC9: // ATSC VCT
			if (!dvbpsi_atsc_AttachVCT(p_dvbpsi, i_table_id, i_extension, handle_atsc_VCT, data))
				fprintf(stderr, "Failed to attach SDT subdecoder\n");
			break;
	}
}

// this hooks will be called when TS packet with desired pid received
// 0x00 - PAT pid
// 0x11 - SDT pid
void pat_hook(void *data, unsigned char *pkt)
{
	struct big_pool_t * pool = (struct big_pool_t *)data;
	jdebug("%s:pool=%p pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->pat_dvbpsi, pkt);
}

void sdt_hook(void *data, unsigned char *pkt)
{
	struct big_pool_t * pool = (struct big_pool_t *)data;
	jdebug("%s:pool=%p pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->sdt_dvbpsi, pkt);
}

void atsc_hook(void *data, unsigned char *pkt)
{
	struct big_pool_t * pool = (struct big_pool_t *)data;
	jdebug("%s:pool=%p pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->atsc_dvbpsi, pkt);
}

struct list_head * get_programs(struct big_pool_t *pool)
{
	// unsigned char *res = NULL;
	int res_len = 0, i = 0;
	struct list_head *result = NULL;
	unsigned char *pkt = NULL;
	int pid = 0;
	struct program_t *program = NULL;
	struct program_es_t *es = NULL;
	int notready = 0, cnt = 20;

	// Attach PAT
	pool->pat_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (pool->pat_dvbpsi == NULL)
		goto out;
	if (!dvbpsi_pat_attach(pool->pat_dvbpsi, DumpPAT, pool))
		goto out;

	// Attach SDT
	pool->sdt_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (pool->sdt_dvbpsi == NULL)
		goto out;
	if (!dvbpsi_AttachDemux(pool->sdt_dvbpsi, NewSubtable, pool))
		goto out;

	// Attach ATSC
	pool->atsc_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (pool->atsc_dvbpsi == NULL)
		goto out;
	if (!dvbpsi_AttachDemux(pool->atsc_dvbpsi, NewSubtable, pool))
		goto out;

	// install hooks
	pool->hooks[0x00] = &pat_hook;

	// check program list (PAT parse)
	while (cnt-- > 0 && list_empty(&pool->programs_list))
		usleep(50000);

	// check ES streams (PMT parse)
	jdebug("is PMT done ? \n");
	cnt = 20;
	while (cnt-- > 0 ) {	
		// we are ready when all programs PMT parsed
		notready = 0;
		list_for_each_entry(program, &pool->programs_list, list) {
			if(list_empty(&program->es_list))
				notready = 1;
		}

		if (!notready)
			break;
			
		usleep(50000);
	}
	printf("All PAT/PMT parse done. Program list is ready now.\n");

	// parse SDT only after PAT and PMT !
	pool->hooks[0x11] = &sdt_hook;

	// parse ATSC channels
	pool->hooks[0x1FFB] = &atsc_hook;

	// OK exit
	return &pool->programs_list;

	// FAIL exit
out:
	if (pool->pat_dvbpsi)
	{
		dvbpsi_pat_detach(pool->pat_dvbpsi);
		dvbpsi_delete(pool->pat_dvbpsi);
	}

	if (pool->sdt_dvbpsi)
	{
		dvbpsi_DetachDemux(pool->sdt_dvbpsi);
		dvbpsi_delete(pool->sdt_dvbpsi);
	}

	if (pool->atsc_dvbpsi)
	{
		dvbpsi_DetachDemux(pool->atsc_dvbpsi);
		dvbpsi_delete(pool->atsc_dvbpsi);
	}

	return NULL;
}
