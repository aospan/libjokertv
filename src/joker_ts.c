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
		printf("	tag=0x%02x : \n", p_descriptor_l->i_tag);
		hexdump(p_descriptor_l->p_data, p_descriptor_l->i_length);
		p_descriptor_l = p_descriptor_l->p_next;
	}
};

static void get_service_name(struct program_t *program, dvbpsi_descriptor_t* p_descriptor)
{ 
	int i = 0, off = 0, provider_len = 0, name_len = 0;
	unsigned char *ptr = NULL;

	memset(&program->name, 0, SERVICE_NAME_LEN);
	
	// Parse according DVB Document A038 (July 2014)
	while(p_descriptor)
	{ 
		if (p_descriptor->i_tag == 0x48 /* Table 12. service_descriptor */) {
			// 6.2.33 Service descriptor
			// p_data:
			// service_type
			//	service_provider_name_length
			//	for (i=0;i<N;i++){ }
			//	service_name_length
			//	for (i=0;i<N;i++){ }
			provider_len = p_descriptor->p_data[1];
			name_len = p_descriptor->p_data[provider_len + 2];
			ptr = p_descriptor->p_data + provider_len + 3;
			jdebug("provider_len=%d name_len=%d ptr=%d \n", provider_len, name_len, provider_len + 3);
			for (i = 0; i < name_len; i++) {
				// special chars. ignore it
				if (ptr[i] >= 0x80 && ptr[i] <= 0x8B)
					continue;

				program->name[off] = ptr[i];
				off++;
			}
			
			jdebug("program=%d new name=%s \n", program->number, program->name);
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

		// DumpDescriptors("	", p_service->p_first_descriptor);
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
