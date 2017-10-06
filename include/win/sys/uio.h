/* fake uio.h
 * for Joker TV under Windows OS
 */

#ifndef _SYS_UIO_H
#define _SYS_UIO_H      1

typedef char *caddr_t;

struct iovec {
	caddr_t iov_base;
	int iov_len;
};
#endif
