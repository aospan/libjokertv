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
	dvbpsi_pmt_es_t* p_es = p_pmt->p_first_es;

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
		if(!list_empty(&program->es_list)) {
			list_for_each_entry(es, &program->es_list, list) {
				if (es->pid == p_es->i_pid) 
					continue; // ignore, already in the list
			}
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
	dvbpsi_pmt_delete(p_pmt);
}

static void DumpPAT(void* data, dvbpsi_pat_t* p_pat)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	dvbpsi_pat_program_t* p_program = p_pat->p_first_program;
	jdebug(  "\n");
	jdebug(  "New PAT. pool=%p\n", pool);
	jdebug(  "  transport_stream_id : %d\n", p_pat->i_ts_id);
	jdebug(  "  version_number      : %d\n", p_pat->i_version);
	jdebug(  "    | program_number @ [NIT|PMT]_PID\n");
	while(p_program)
	{
		// avoid duplicates
		if(!list_empty(&pool->programs_list)) {
			list_for_each_entry(program, &pool->programs_list, list) {
				if (program->number == p_program->i_number) 
					continue; // ignore, already in the list
			}
		}

		program = (struct program_t*)malloc(sizeof(*program));
		if (!program)
			break;

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

// convert name to utf-8
static void to_utf(char * buf, int maxlen, uint8_t codepage)
{
	iconv_t cd;
	char outbuf[maxlen];
	char * outptr = &outbuf[0];
	char * charset = "ISO6937";
	char *inbuf = buf;
	size_t nconv = 0, insize = 0, avail = maxlen;

	insize = strlen(buf);
	memset(outbuf, 0x0, maxlen);

	// Values used from DVB Document A038 (July 2014)
	// Table A.3: Character coding tables
	switch (codepage) {
		case 0x01:
			// Latin/Cyrillic
			charset = "ISO8859-5"; break;
		case 0x02:
			// Latin/Arabic
			charset = "ISO8859-6"; break;
		case 0x03:
			// Latin/Greek
			charset = "ISO8859-7"; break;
		case 0x04:
			// Latin/Hebrew
			charset = "ISO8859-8"; break;
		case 0x05:
			// Latin alphabet No. 5
			charset = "ISO8859-9"; break;
		case 0x06:
			// Latin alphabet No. 6
			charset = "ISO8859-10"; break;
		case 0x07:
			// Latin/Thai
			charset = "ISO8859-11"; break;
		case 0x09:
			// Latin alphabet No. 7
			charset = "ISO8859-13"; break;
		case 0x0a:
			// Latin alphabet No. 8 (Celtic)
			charset = "ISO8859-14"; break;
		case 0x0b:
			// Latin alphabet No. 9
			charset = "ISO8859-15"; break;
		case 0x11:
		case 0x14: // Big5 subset of ISO/IEC 10646 [16] Traditional Chinese
		case 0x15: // UTF-8 encoding of ISO/IEC 10646 [16] Basic Multilingual Plane (BMP)
			// Basic Multilingual Plane (BMP)
			charset = "ISO-10646"; break;
		case 0x13:
			// Simplified Chinese Character
			charset = "GB2312"; break;
		case 0x00:
		default:
			// default codepage  ISO/IEC 6937
			charset = "ISO6937";
			break;
	}

	cd = iconv_open ("UTF-8", charset);
	if (cd == (iconv_t) -1)
	{
		printf("can't open iconv for charset conversion\n");
		return;
	}


	nconv = iconv (cd, &inbuf, &insize, &outptr, &avail);
	if (nconv == -1)
		printf("iconv conversion may be failed. But we use result anyway ... \n");

	jdebug("iconv: insize=%zd avail=%zd nconv=%zd \n", insize, avail, nconv );
	// copy result 
	memset(buf, 0, maxlen);
	memcpy(buf, outbuf, maxlen - avail);
	iconv_close (cd);
}

static void get_service_name(struct program_t *program, dvbpsi_descriptor_t* p_descriptor)
{ 
	int i = 0, off = 0, service_provider_name_length = 0, service_name_length = 0;
	unsigned char *service_name_ptr = NULL;
	uint8_t codepage = 0;

	memset(&program->name, 0, SERVICE_NAME_LEN);
	
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
			codepage = p_descriptor->p_data[service_provider_name_length + 3];
			jdebug("provider_len=%d service_name_length=%d service_name_ptr=%d codepage=0x%x\n",
					service_provider_name_length, service_name_length, service_provider_name_length + 3, codepage);
			for (i = 1; i < service_name_length; i++) {
				// special chars. ignore it
				if (service_name_ptr[i] >= 0x80 && service_name_ptr[i] <= 0x8B)
					continue;

				program->name[off] = service_name_ptr[i];
				off++;
			}
			jdebug("program=%d new name=%s \n", program->number, program->name);

			// convert name to utf-8
			to_utf(program->name, SERVICE_NAME_LEN, codepage);
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

/*****************************************************************************
 * NewSubtable
 *****************************************************************************/
static void NewSubtable(dvbpsi_t *p_dvbpsi, uint8_t i_table_id, uint16_t i_extension,
		void * data)
{
	if(i_table_id == 0x42)
	{  
		if (!dvbpsi_sdt_attach(p_dvbpsi, i_table_id, i_extension, DumpSDT, data))
			fprintf(stderr, "Failed to attach SDT subdecoder\n");
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

struct list_head * get_programs(struct big_pool_t *pool)
{
	// unsigned char *res = NULL;
	int res_len = 0, i = 0;
	struct list_head *result = NULL;
	unsigned char *pkt = NULL;
	int pid = 0;
	struct program_t *program = NULL;
	struct program_es_t *es = NULL;
	int notready = 0, cnt = 1000;

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

	// install hooks
	pool->hooks[0x00] = &pat_hook;

	// check program list (PAT parse)
	while (list_empty(&pool->programs_list))
		usleep(1000);

	// check ES streams (PMT parse)
	jdebug("is PMT done ? \n");
	while (cnt-- > 0 ) {	
		// we are ready when all programs PMT parsed
		notready = 0;
		list_for_each_entry(program, &pool->programs_list, list) {
			if(list_empty(&program->es_list))
				notready = 1;
		}

		if (!notready)
			break;
			
		usleep(1000);
	}
	printf("All PAT/PMT parse done. Program list is ready now.\n");

	// parse SDT only after PAT and PMT !
	pool->hooks[0x11] = &sdt_hook;

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

	return NULL;
}
