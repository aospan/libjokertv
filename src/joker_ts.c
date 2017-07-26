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
#include <u_drv_data.h>

#include <stdbool.h>
#include <dvbpsi/dvbpsi.h>
#include <dvbpsi/psi.h>
#include <dvbpsi/pat.h>

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

		program->name = (unsigned char*)malloc(FNAME_LEN);
		if (!program->name)
			break;

		memset(program->name, 0, FNAME_LEN);
		snprintf(program->name, FNAME_LEN, "program-%d", p_program->i_number);

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

struct list_head * get_programs(struct big_pool_t *pool)
{
	unsigned char *res = NULL;
	int res_len = 0, i = 0;
	dvbpsi_t *p_dvbpsi;
	struct list_head *result = NULL;

	p_dvbpsi = dvbpsi_new(&message, DVBPSI_MSG_DEBUG);
	if (p_dvbpsi == NULL)
		goto out;

	if (!dvbpsi_pat_attach(p_dvbpsi, DumpPAT, pool))
		goto out;

	res = (unsigned char*)malloc(TS_BUF_MAX_SIZE);
	if (!res)
		goto out;

	while(1) {
		res_len = read_ts_data_pid(pool, 0x0, res);

		// printf("res_len=%d \n", res_len );
		if (res_len > 0) {
			// supply packets to libdvbpsi
			for(i = 0; i < res_len; i += TS_SIZE) {
				dvbpsi_packet_push(p_dvbpsi, res+i);
				if (!list_empty(&pool->programs_list))
					goto out;
			}
		} else {
			usleep(1000); // TODO: rework this (condwait ?)
		}
	}

out:
	if (p_dvbpsi)
	{
		dvbpsi_pat_detach(p_dvbpsi);
		dvbpsi_delete(p_dvbpsi);
	}

	return &pool->programs_list;
}
