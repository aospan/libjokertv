/* fake poll.h
 * for Joker TV under Windows OS
 */

#ifndef _SYS_POLL_H
#define _SYS_POLL_H     1

#define POLLIN          0x0001
#define POLLPRI         0x0002
#define POLLOUT         0x0004
#define POLLERR         0x0008
#define POLLHUP         0x0010
#define POLLNVAL        0x0020
typedef char *caddr_t;

struct iovec {
	caddr_t iov_base;
	int iov_len;
};

struct pollfd {
	int fd;
	short events;
	short revents;
};

#define _NO_POLL 1

#endif
