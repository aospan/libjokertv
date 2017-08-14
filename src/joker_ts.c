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
#include <pat.h>
#include <descriptor.h>
#include <demux.h>
#include <sdt.h>

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
		program = (struct program_t*)malloc(sizeof(*program));
		if (!program)
			break;

		memset(&program->name, 0, SERVICE_NAME_LEN);
		program->number = p_program->i_number;
		list_add_tail(&program->list, &pool->programs_list);

		jdebug("    | %14d @ 0x%x (%d)\n",
				p_program->i_number, p_program->i_pid, p_program->i_pid);
		p_program = p_program->p_next;
	}
	jdebug(  "  active              : %d\n", p_pat->b_current_next);
	dvbpsi_pat_delete(p_pat);
}

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
	uint8_t service_type = 0;

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
			service_type = p_descriptor->p_data[0];
			service_provider_name_length = p_descriptor->p_data[1];
			service_name_length = p_descriptor->p_data[service_provider_name_length + 2];
			service_name_ptr = p_descriptor->p_data + service_provider_name_length + 3;

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
void pat_hook(struct big_pool_t * pool, unsigned char *pkt)
{
	jdebug("%s:pool=%p pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->pat_dvbpsi, pkt);
}

void sdt_hook(struct big_pool_t * pool, unsigned char *pkt)
{
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
	pool->hooks[0x11] = &sdt_hook;

	while (list_empty(&pool->programs_list))
		usleep(1000);

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
