/* 
 * Access to Joker TV CI (Common Interface)
 * EN50221 MMI access
 * 
 * Conditional Access Module for scrambled streams (pay tv)
 * Based on EN 50221-1997 standard
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>

#include <joker_tv.h>
#include <joker_ci.h>
#include <joker_fpga.h>
#include <joker_utils.h>
#include <joker_en50221.h>

struct ci_server_thread_opaq_t
{
	/* ci thread for network MMI access */
	pthread_t ci_server_thread;
	pthread_cond_t cond;
	pthread_mutex_t mux;
	int cancel;
};

void mmi_callback(void *data, unsigned char *buf, int len)
{
	struct joker_t * joker = (struct joker_t *)data;
	int err = 0 ;

	if (!joker || !joker->joker_en50221_opaque)
		return;

	err = send(joker->ci_client_fd, buf, len, 0);
	if (err < 0)
		printf("%s: Client write failed\n", __func__);

	return;
}

void* joker_en50221_server_worker(void * data)
{
	struct joker_t * joker = (struct joker_t *)data;
	int ret = 0;
	int server_fd, err;
	struct sockaddr_in server, client;
	char buf[MAX_EN50221_BUF];
	char out_buf[MAX_EN50221_BUF];

	if (!joker || !joker->joker_en50221_opaque)
		return;

	if (joker->ci_server_port <= 0) {
		printf("TCP port not defined. Do not start EN50221 MMI server ... \n");
		return 0;
	}

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		printf("Could not create socket\n");
		return;
	}

	server.sin_family = AF_INET;
	server.sin_port = htons(joker->ci_server_port);
	// for security reasons listen only on 127.0.0.1
	server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	int opt_val = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);

	err = bind(server_fd, (struct sockaddr *) &server, sizeof(server));
	if (err < 0) {
		printf("%s: Could not bind socket\n", __func__);
		return;
	}

	err = listen(server_fd, 128);
	if (err < 0) {
		printf("%s: Could not listen on socket\n", __func__);
		return;
	}

	printf("%s: Server is listening on %d\n", __func__, joker->ci_server_port);

	while (1) {
		socklen_t client_len = sizeof(client);
		joker->ci_client_fd = accept(server_fd, (struct sockaddr *) &client, &client_len);

		if (joker->ci_client_fd < 0) {
			printf("%s: Could not establish new connection\n", __func__);
			continue;
		}

		// start MMI
		if (joker_en50221_mmi_enter(joker, &mmi_callback)) {
			printf("%s: can't enter to MMI menu\n", __func__);
			return;
		};

		while (1) {
			int read = recv(joker->ci_client_fd, buf, MAX_EN50221_BUF, 0);

			if (!read) break; // done reading
			if (read < 0) {
				printf("%s: Client read failed\n", __func__);
				continue;
			}

			if ((ret = joker_en50221_mmi_call(joker, buf, read)) < 0) {
				printf("%s: can't call MMI \n", __func__);
				continue;
			};

			/* 
			err = send(ci_client_fd, buf, read, 0);
			if (err < 0) {
				printf("%s: Client write failed\n", __func__);
				continue;
			} */
		}
	}
	printf("%s: Shutdown en50221 server\n", __func__);

	return 0;
}

int start_en50221_server(struct joker_t * joker)
{
	int rc = 0;

	if (!joker)
		return -EINVAL;

	if (!joker->ci_server_threading) {
		joker->ci_server_threading = malloc(sizeof(struct ci_server_thread_opaq_t));
		memset(joker->ci_server_threading, 0, sizeof(struct ci_server_thread_opaq_t));
		pthread_mutex_init(&joker->ci_server_threading->mux, NULL);
		pthread_cond_init(&joker->ci_server_threading->cond, NULL);

		pthread_attr_t attrs;
		pthread_attr_init(&attrs);
		pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
		rc = pthread_create(&joker->ci_server_threading->ci_server_thread, &attrs, joker_en50221_server_worker, (void *)joker);
		if (rc){
			printf("ERROR: can't start EN50221 TCP server thread. code=%d\n", rc);
			return rc;
		}
	}

	return 0;
}

int stop_en50221_server(struct joker_t * joker)
{
	// TODO
	return 0;
}
