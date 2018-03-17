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
#include <joker_ts_filter.h>
#include <joker_utils.h>
#include <joker_en50221.h>
#include <u_drv_data.h>

#include <stdbool.h>
#include <dvbpsi.h>
#include <psi.h>
#include <descriptor.h>
#include <pat.h>
#include <cat.h>
#include <pmt.h>
#include <tot.h>
#include <nit.h>
#include <dr.h>
#include <demux.h>
#include <sdt.h>
/*  ATSC PSI Tables */
#include <atsc_eit.h>
#include <atsc_ett.h>
#include <atsc_mgt.h>
#include <atsc_stt.h>
#include <atsc_vct.h>

static void DumpDescriptors(const char* str, dvbpsi_descriptor_t* p_descriptor);

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
	struct program_ca_t*ca = NULL;
	int audio = 0, video = 0;
	struct dvbpsi_psi_section_s *current_section = NULL;
	dvbpsi_pmt_es_t* p_es = p_pmt->p_first_es;
	int ignore = 0;
	dvbpsi_descriptor_t *p_descriptor_l = NULL;
	int pid = 0, caid = 0;

	if (!program)
		return;

	jdebug(  "\n");
	jdebug(  "New active PMT\n");
	jdebug(  "  program_number : %d\n",
			p_pmt->i_program_number);
	jdebug(  "  version_number : %d\n",
			p_pmt->i_version);
	jdebug(  "  PCR_PID        : 0x%x (%d)\n",
			p_pmt->i_pcr_pid, p_pmt->i_pcr_pid);
	jdebug(  "    | type @ elementary_PID\n");

	program->pcr_pid = p_pmt->i_pcr_pid;

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

		es = (struct program_es_t*)calloc(1, sizeof(*es));
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

		// allow PID in TS PID filtering
		if (is_program_selected(program->joker->pool, p_pmt->i_program_number))
			ts_filter_one(program->joker, TS_FILTER_UNBLOCK, p_es->i_pid);

		// loop descriptors
		p_descriptor_l = p_es->p_first_descriptor;
		while(p_descriptor_l)
		{ 
			jdebug("%s: program=0x%x es=0x%x descr=0x%x \n", __func__,
					p_pmt->i_program_number, p_es->i_pid, p_descriptor_l->i_tag);
			// DumpDescriptors("	", p_descriptor_l);
			if (p_descriptor_l->i_tag == 0x0a ||
					p_descriptor_l->i_tag == 0x56) {
				// language descriptors
				memcpy(es->lang, p_descriptor_l->p_data, 3);
			}
			p_descriptor_l = p_descriptor_l->p_next;
		}

		jdebug("    | 0x%02x @ 0x%x (%d)\n",
				p_es->i_type,
				p_es->i_pid, p_es->i_pid);
		p_es = p_es->p_next;
	}

	// loop descriptors
	p_descriptor_l = p_pmt->p_first_descriptor;
	while(p_descriptor_l)
	{ 
		jdebug("%s: program=0x%x descr=0x%x \n", __func__,
				p_pmt->i_program_number, p_descriptor_l->i_tag);
		if (p_descriptor_l->i_tag == 0x09 /* CA */) {
			// CA descriptor found
			pid = ((p_descriptor_l->p_data[2]&0x1F) <<8) | p_descriptor_l->p_data[3];
			caid = p_descriptor_l->p_data[0] << 8 | p_descriptor_l->p_data[1];

			// avoid duplicates
			// WARNING: one PID can be used twice in CA descriptors
			// we need only PID/CAID, so we drop duplicates
			ignore = 0;
			if(!list_empty(&program->ca_list)) {
				list_for_each_entry(ca, &program->ca_list, list) {
					if (ca->pid == pid && ca->caid == caid)
						ignore = 1; // ignore, already in the list
				}
			}

			if (ignore) {
				p_descriptor_l = p_descriptor_l->p_next;
				continue;
			}

			ca = (struct program_ca_t*)calloc(1, sizeof(*ca));
			if (!ca)
				break;

			ca->pid = pid;
			ca->caid = caid;

			// allow PID in TS PID filtering
			if (is_program_selected(program->joker->pool, p_pmt->i_program_number))
				ts_filter_one(program->joker, TS_FILTER_UNBLOCK, pid);

			list_add_tail(&ca->list, &program->ca_list);
			jdebug ("add to CA list for program=%d caid=0x%x pid=0x%x \n",
					program->number, caid, pid);
		}
		p_descriptor_l = p_descriptor_l->p_next;
	}


	// get pointer to "raw" PMT (unparsed)
	current_section = ((dvbpsi_t*)program->pmt_dvbpsi)->p_decoder->p_current_section;
	jdebug("%s: p_current_section=%p \n", __func__, current_section);

	// send "raw" PMT to en50221 layer for processing
	joker_en50221_pmt_update(program,
			current_section->p_data, current_section->i_length);

	dvbpsi_pmt_delete(p_pmt);
}

int add_program_to_pat(struct big_pool_t *pool, int program_number, int pmt_pid)
{
	if (!pool)
		return -EINVAL;

	if (!pool->generated_pat) {
		pool->generated_pat = calloc(1, sizeof(dvbpsi_pat_t));
		if (!pool->generated_pat)
			return -ENOMEM;
		dvbpsi_pat_init((dvbpsi_pat_t*)pool->generated_pat, 1, 0, 1 /* b_current_next high - PAT active now */ );
	}

	dvbpsi_pat_program_add((dvbpsi_pat_t*)pool->generated_pat, program_number, pmt_pid);
	jdebug("Program %d with pmt pid %d added to PAT \n", program_number, pmt_pid);
}

uint8_t *alloc_ts(uint8_t* p_packet_origin, int pkts_allocated)
{
	uint8_t* p_packet = NULL;

	p_packet = realloc(p_packet_origin, TS_SIZE*pkts_allocated);
	if (!p_packet)
		return 0;
	memset(p_packet + TS_SIZE*(pkts_allocated-1), 0, TS_SIZE);

	return p_packet;
}

// return allocated pkts (each TS_SIZE long)
// caller should free allocated memory
int psi_pkts_generate(uint8_t** p_packet_ret, dvbpsi_psi_section_t* p_section, int pid)
{
	uint8_t* buf = NULL;
	uint8_t* p_packet = NULL;
	int pkts_allocated = 0;

	pkts_allocated++;
	if (!(buf = alloc_ts(buf, pkts_allocated))) {
		return 0;
	}
	p_packet = buf + TS_SIZE*(pkts_allocated-1);

	while(p_section)
	{
		size_t i_bytes_written = 0;
		uint8_t* p_pos_in_ts;
		uint8_t* p_byte = p_section->p_data;
		uint8_t* p_end =   p_section->p_payload_end
			+ (p_section->b_syntax_indicator ? 4 : 0);

		jdebug("%s: packet1 start. size=%d\n", __func__, p_end - p_byte);

		p_packet[0] = 0x47;
		p_packet[1] = 0x40 | (pid>>8)&0x1f;
		p_packet[2] = (pid&0xff);
		p_packet[3] = 0x10;

		p_packet[4] = 0x00; /* pointer_field */
		p_pos_in_ts = p_packet + 5;

		while((p_pos_in_ts < p_packet + 188) && (p_byte < p_end))
			*(p_pos_in_ts++) = *(p_byte++);
		while(p_pos_in_ts < p_packet + 188)
			*(p_pos_in_ts++) = 0xff;
		jdebug("%s: packet1 done \n", __func__);
		
		// write next packets
		if (p_byte < p_end) {
			// allocate next packet
			pkts_allocated++;
			buf = alloc_ts(buf, pkts_allocated);
			p_packet = buf + TS_SIZE*(pkts_allocated-1);
		}

		jdebug("%s: packet2 start. size=%d\n", __func__, p_end - p_byte);
		while(p_byte < p_end)
		{ 
			p_packet[0] = 0x47;
			p_packet[1] = (pid>>8)&0x1f;
			p_packet[2] = (pid&0xff);
			p_packet[3] = 0x10;

			p_pos_in_ts = p_packet + 4;

			while((p_pos_in_ts < p_packet + 188) && (p_byte < p_end))
				*(p_pos_in_ts++) = *(p_byte++);
			while(p_pos_in_ts < p_packet + 188)
				*(p_pos_in_ts++) = 0xff;

			jdebug("%s: packet2 done. size=%d\n", __func__, p_end - p_byte);
		}

		p_section = p_section->p_next;
	}
	*p_packet_ret = buf;

	return pkts_allocated;
}

int generate_pat_pkt(struct big_pool_t *pool)
{
	dvbpsi_t *p_dvbpsi = NULL;
	dvbpsi_pat_t pat;
	dvbpsi_psi_section_t* p_section1;
	char * packet = NULL;
	int allocated = 0;

	if (!pool || !pool->generated_pat)
		return -EINVAL;

	if (pool->generated_pat_pkt)
		return 0;
	p_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
	if (p_dvbpsi == NULL)
		return -ENOMEM;

	p_section1 = dvbpsi_pat_sections_generate(p_dvbpsi, (dvbpsi_pat_t*)pool->generated_pat, 250);

	allocated = psi_pkts_generate(&pool->generated_pat_pkt, p_section1, 0x00 /* PAT */);

	dvbpsi_delete(p_dvbpsi);

	jdebug("PAT generated. allocated=%d\n", allocated);

	return 0;
}

int is_program_selected (struct big_pool_t *pool, int program_number)
{
	struct program_t *program = NULL;
	if (!pool)
		return 0;

	jdebug("%s: searching program %d \n", __func__, program_number);

	if(!list_empty(&pool->selected_programs_list))
		list_for_each_entry(program, &pool->selected_programs_list, list)
			if (program->number == program_number)
				return 1;

	jdebug("%s: program %d not selected \n", __func__, program_number);
	return 0;
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
		INIT_LIST_HEAD(&program->ca_list);
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

		// allow PID in TS PID filtering
		if (is_program_selected(pool, p_program->i_number)) {
			ts_filter_one(pool->joker, TS_FILTER_UNBLOCK, p_program->i_pid);
			add_program_to_pat(pool, p_program->i_number, p_program->i_pid);
		}

		pool->hooks[p_program->i_pid] = &pmt_hook;
		pool->hooks_opaque[p_program->i_pid] = program;
		p_program = p_program->p_next;
	}
	jdebug(  "  active              : %d\n", p_pat->b_current_next);
	dvbpsi_pat_delete(p_pat);
}

static void DumpCAT(void* data, dvbpsi_cat_t* p_cat)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	int ignore = 0;
	dvbpsi_descriptor_t *p_descriptor_l = NULL;
	int pid = 0, caid = 0;
	struct program_ca_t*ca = NULL;

	// loop descriptors
	p_descriptor_l = p_cat->p_first_descriptor;
	while(p_descriptor_l)
	{ 
		if (p_descriptor_l->i_tag == 0x09 /* CA */) {
			// CA descriptor found
			pid = ((p_descriptor_l->p_data[2]&0x1F) <<8) | p_descriptor_l->p_data[3];
			caid = p_descriptor_l->p_data[0] << 8 | p_descriptor_l->p_data[1];

			// avoid duplicates
			// WARNING: one PID can be used twice in CA descriptors
			// we need only PID/CAID, so we drop duplicates
			ignore = 0;
			if(!list_empty(&pool->ca_list)) {
				list_for_each_entry(ca, &pool->ca_list, list) {
					if (ca->pid == pid && ca->caid == caid)
						ignore = 1; // ignore, already in the list
				}
			}

			if (ignore) {
				p_descriptor_l = p_descriptor_l->p_next;
				continue;
			}

			ca = (struct program_ca_t*)calloc(1, sizeof(*ca));
			if (!ca)
				break;

			ca->pid = pid;
			ca->caid = caid;

			// allow PID in TS PID filtering
			if(!list_empty(&pool->selected_programs_list))
				ts_filter_one(pool->joker, TS_FILTER_UNBLOCK, pid);

			list_add_tail(&ca->list, &pool->ca_list);
			jdebug ("add to CA list for pool caid=0x%x pid=0x%x \n",
					caid, pid);
		}
		p_descriptor_l = p_descriptor_l->p_next;
	}



	dvbpsi_cat_delete(p_cat);
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
		jdebug("	tag=0x%02x : len=%d \n", p_descriptor_l->i_tag, p_descriptor_l->i_length );
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

	return 0;
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

int convert_dvb_line (unsigned char *ptr, int len, unsigned char *dst, int maxlen)
{
	int i = 0, off = 0, ret = 0;
	uint8_t codepage = 0;
	unsigned char charset[SERVICE_NAME_LEN];

	memset(dst, 0, SERVICE_NAME_LEN);
	memset(&charset[0], 0, SERVICE_NAME_LEN);

	// Text fields can optionally start with non-spacing,
	// non-displayed data which specifies the
	// alternative character table to be used for the
	// remainder of the text item.
	// If the first byte of the text field has a value in the range "0x20" to "0xFF"
	// then this and all subsequent bytes in the text
	// item are coded using the default character coding table (table 00 - Latin alphabet) of figure A.1. 
	if (ptr[0] >= 0x20 && ptr[0] <= 0xFF)
		codepage = 0x0; // ISO8859-1
	else
		codepage = ptr[0];

	for (i = isprint(ptr[0])?0:1; i < len; i++) {
		// special chars. ignore it
		if (ptr[i] >= 0x80 && ptr[i] <= 0x8B)
			continue;

		dst[off] = ptr[i];
		off++;
	}

	jdebug("%s: ptr=%s length=%d (%d) codepage=0x%x\n", __func__, ptr, len, off, codepage);
	// convert name to utf-8
	if (!get_charset_name(codepage, &charset[0]))
		ret = to_utf(dst, off, dst, SERVICE_NAME_LEN, charset);

	return ret;
}

static void get_service_name(struct program_t *program, dvbpsi_descriptor_t* p_descriptor)
{ 
	int service_provider_name_length = 0, service_name_length = 0;
	unsigned char *service_name_ptr = NULL;
	unsigned char *service_provider_name_ptr = NULL;

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
			service_provider_name_ptr = p_descriptor->p_data + 2;
			service_name_length = p_descriptor->p_data[service_provider_name_length + 2];
			service_name_ptr = p_descriptor->p_data + service_provider_name_length + 3;

			jdebug("service_type=%d \n", service_type );

			memset(program->provider_name, 0, SERVICE_NAME_LEN);
			if (service_provider_name_length)
				convert_dvb_line(service_provider_name_ptr, service_provider_name_length,
						program->provider_name, SERVICE_NAME_LEN);

			memset(program->name, 0, SERVICE_NAME_LEN);
			if (service_name_length)
				convert_dvb_line(service_name_ptr, service_name_length,
						program->name, SERVICE_NAME_LEN);
		}
		p_descriptor = p_descriptor->p_next;
	}
};

int generate_sdt_pkt(struct big_pool_t *pool, struct program_t *program, dvbpsi_sdt_t* p_sdt)
{
	dvbpsi_t *p_dvbpsi = NULL;
	dvbpsi_pat_t pat;
	dvbpsi_psi_section_t* p_section1;
	char * packet = NULL;
	int allocated;

	if (!pool || !program || !p_sdt)
		return -EINVAL;

	if (program->generated_sdt_pkt)
		return 0;

	p_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
	if (p_dvbpsi == NULL)
		return -ENOMEM;

	p_section1 = dvbpsi_sdt_sections_generate(p_dvbpsi, p_sdt);

	allocated = psi_pkts_generate(&program->generated_sdt_pkt, p_section1, 0x11 /* SDT */);
	if (allocated <= 0)
		return -ENOMEM;

	dvbpsi_delete(p_dvbpsi);

	printf("SDT generated for program %d size=%d\n", program->number, allocated);

	// add SDT to array
	pool->sdt_pkt_array = realloc(pool->sdt_pkt_array, TS_SIZE * (pool->sdt_count + allocated));
	if (!pool->sdt_pkt_array)
		return -ENOMEM;

	memcpy(pool->sdt_pkt_array + TS_SIZE * pool->sdt_count, program->generated_sdt_pkt, allocated * TS_SIZE);
	pool->sdt_count += allocated;

	printf("SDT generated for program %d sdt_count=%d\n", program->number, pool->sdt_count);
}

void * get_next_sdt(struct big_pool_t *pool)
{
	char * ptr = NULL;

	ptr = pool->sdt_pkt_array + TS_SIZE * pool->cur_sdt;

	pool->cur_sdt++;
	if (pool->cur_sdt >= pool->sdt_count)
		pool->cur_sdt = 0;

	return (void*)ptr;
}

static void DumpTOT(void* data, dvbpsi_tot_t* p_tot)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	uint8_t dvbdate[5];
	time_t t;

	
	dvbdate[0] = (p_tot->i_utc_time >> 32)&0xFF;
	dvbdate[1] = (p_tot->i_utc_time >> 24)&0xFF;
	dvbdate[2] = (p_tot->i_utc_time >> 16)&0xFF;
	dvbdate[3] = (p_tot->i_utc_time >> 8)&0xFF;
	dvbdate[4] = (p_tot->i_utc_time)&0xFF;

	t = dvbdate_to_unixtime(dvbdate);

	// update en50221 stack with new dvb time arrived
	joker_en50221_set_dvbtime(pool, t);

	jdebug("%s: i_utc_time=%llu time_t=%llu - %s\n",
			__func__, p_tot->i_utc_time, t, ctime(&t));

}

/*****************************************************************************
 * DumpSDT
 *****************************************************************************/
static void DumpSDT(void* data, dvbpsi_sdt_t* p_sdt)
{
	struct program_t *program = NULL;
	struct big_pool_t *pool = (struct big_pool_t *)data;
	joker_nit_t * joker_nit = NULL;
	dvbpsi_sdt_service_t* p_service = p_sdt->p_first_service;
	dvbpsi_sdt_t* p_stored_sdt = (dvbpsi_sdt_t*)pool->stored_sdt;

	jdebug(  "\n");
	jdebug(  "New active SDT b_current_next=%d\n", p_sdt->b_current_next);
	jdebug(  "  ts_id : %d\n",
			p_sdt->i_extension);
	jdebug(  "  version_number : %d\n",
			p_sdt->i_version);
	jdebug(  "  network_id        : %d\n",
			p_sdt->i_network_id);

	// check if we have already stored this SDT
	// from "DVB BlueBook A038":
	// current_next_indicator: This 1-bit indicator, when set to "1" indicates that the sub_table is the
	// currently applicable sub_table. When the bit is set to "0", it indicates that the sub_table sent is
	// not yet applicable and shall be the next sub_table to be valid.
	if (p_stored_sdt) {
		if (p_stored_sdt->i_version == p_sdt->i_version
			|| p_stored_sdt->b_current_next > p_sdt->b_current_next) {
			dvbpsi_sdt_delete(p_sdt);
			return;
		}
	}

	// save TS ID and Network ID 
	pool->network_id = p_sdt->i_network_id;
	pool->ts_id = p_sdt->i_extension;

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

					// TODO: rework SDT generator ! 
					// generate SDT packet if program selected
					// if (is_program_selected(pool, program->number)) {
						// generate_sdt_pkt(pool, program, p_sdt);
					// }
				}
			}
		}

		p_service = p_service->p_next;
	}

	if (p_stored_sdt)
		dvbpsi_sdt_delete(p_stored_sdt);

	pool->stored_sdt = (void*)p_sdt;
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

static void DumpNIT(void* p_data, dvbpsi_nit_t* p_nit)
{
	dvbpsi_descriptor_t *p_descriptor_l = p_nit->p_first_descriptor;
	struct big_pool_t *pool = (struct big_pool_t *)p_data;
	dvbpsi_nit_ts_t *p_ts = p_nit->p_first_ts;
	joker_nit_t * joker_nit = NULL;

	if (!pool)
		return;

	jdebug("\n");
	jdebug("  NIT: Network Information Table\n");
	jdebug("\tVersion number : %d\n", p_nit->i_version);
	jdebug("\tNetwork id     : %d\n", p_nit->i_network_id);
	jdebug("\tCurrent next   : %s\n", p_nit->b_current_next ? "yes" : "no");

	pool->nit_network_id = p_nit->i_network_id;

	// Parse according DVB Document A038 (July 2014)
	while(p_descriptor_l)
	{ 
		jdebug("%s: tag=0x%02x : len=%d \n", __func__, p_descriptor_l->i_tag, p_descriptor_l->i_length );
		// 0x40	Network Name descr.
		if (p_descriptor_l->i_tag == 0x40) {
			if (pool->network_name)
				free(pool->network_name);

			pool->network_name = (char*)calloc(1, p_descriptor_l->i_length + 1);
			if (!pool->network_name)
				return;
			memcpy(pool->network_name, p_descriptor_l->p_data, p_descriptor_l->i_length);
		}
		p_descriptor_l = p_descriptor_l->p_next;
	}

	while (p_ts)
	{   
		jdebug("\t  | transport id: %d\n", p_ts->i_ts_id);
		jdebug("\t  | original network id: %d\n", p_ts->i_orig_network_id);
		joker_nit = calloc(1, sizeof(joker_nit_t));
		if (!joker_nit)
			return;

		joker_nit->ts_id = p_ts->i_ts_id;
		joker_nit->orig_network_id = p_ts->i_orig_network_id;
		list_add_tail(&joker_nit->list, &pool->nit_list);

		p_ts = p_ts->p_next;
	}

	dvbpsi_nit_delete(p_nit);
}

/*****************************************************************************
 * NewSubtable
 *****************************************************************************/
static void NewSubtable(dvbpsi_t *p_dvbpsi, uint8_t i_table_id, uint16_t i_extension,
		void * data)
{
	jdebug("%s: new i_table_id=0x%x\n", __func__, i_table_id);
	switch (i_table_id) {
		case 0x40: // NIT
		case 0x41: // NIT
			if (!dvbpsi_nit_attach(p_dvbpsi, i_table_id, i_extension, DumpNIT, data))
				fprintf(stderr, "Failed to attach NIT subdecoder\n");
			break;
		case 0x42: // SDT
			if (!dvbpsi_sdt_attach(p_dvbpsi, i_table_id, i_extension, DumpSDT, data))
				fprintf(stderr, "Failed to attach SDT subdecoder\n");
			break;
		case 0x70: // TDT
		case 0x73: // TOT
			if (!dvbpsi_tot_attach(p_dvbpsi, i_table_id, i_extension, DumpTOT, data))
				fprintf(stderr, "Failed to attach TDT/TOT subdecoder\n");
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

void cat_hook(void *data, unsigned char *pkt)
{
	struct big_pool_t * pool = (struct big_pool_t *)data;
	jdebug("%s:pool=%p pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->cat_dvbpsi, pkt);
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
	dvbpsi_packet_push(pool->si_dvbpsi, pkt);
}

void si_hook(void *data, unsigned char *pkt)
{
	struct big_pool_t * pool = (struct big_pool_t *)data;
	jdebug("%s:pool=%p TDT pkt=%p\n", __func__, pool, pkt);
	dvbpsi_packet_push(pool->si_dvbpsi, pkt);
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

	// Attach SI demux (for TOT/TDT, ATSC )
	pool->si_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (pool->si_dvbpsi == NULL)
		goto out;
	if (!dvbpsi_AttachDemux(pool->si_dvbpsi, NewSubtable, pool))
		goto out;

	// Attach SDT demux 
	pool->sdt_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (pool->sdt_dvbpsi == NULL)
		goto out;
	if (!dvbpsi_AttachDemux(pool->sdt_dvbpsi, NewSubtable, pool))
		goto out;

	// Attach CAT
	pool->cat_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_NONE);
	if (!dvbpsi_cat_attach(pool->cat_dvbpsi, DumpCAT, pool))
		goto out;

	// install hooks
	pool->hooks[J_TRANSPORT_PAT_PID] = &pat_hook;
	pool->hooks[J_TRANSPORT_CAT_PID] = &cat_hook;
	pool->hooks[J_TRANSPORT_TDT_PID] = &si_hook;
	pool->hooks[J_TRANSPORT_NIT_PID] = &si_hook;

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
	pool->hooks[J_TRANSPORT_SDT_PID] = &sdt_hook;

	// parse ATSC channels
	pool->hooks[0x1FFB] = &atsc_hook;

	if (!list_empty(&pool->selected_programs_list))
		generate_pat_pkt(pool);

	// OK exit
	return &pool->programs_list;

	// FAIL exit
out:
	if (pool->pat_dvbpsi)
	{
		dvbpsi_pat_detach(pool->pat_dvbpsi);
		dvbpsi_delete(pool->pat_dvbpsi);
	}

	if (pool->si_dvbpsi)
	{
		dvbpsi_DetachDemux(pool->si_dvbpsi);
		dvbpsi_delete(pool->si_dvbpsi);
	}

	if (pool->sdt_dvbpsi)
	{
		dvbpsi_DetachDemux(pool->sdt_dvbpsi);
		dvbpsi_delete(pool->sdt_dvbpsi);
	}

	return NULL;
}
