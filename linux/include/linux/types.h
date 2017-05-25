#ifndef _LINUX_TYPES_H_
#define _LINUX_TYPES_H_

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

#define module_param(name, type, perm)
#define MODULE_PARM_DESC(_parm, desc)

#define KERN_SOH        "\001"          /* ASCII Start Of Header */
#define KERN_SOH_ASCII  '\001'

#define KERN_EMERG      KERN_SOH "0"    /* system is unusable */
#define KERN_ALERT      KERN_SOH "1"    /* action must be taken immediately */
#define KERN_CRIT       KERN_SOH "2"    /* critical conditions */
#define KERN_ERR        KERN_SOH "3"    /* error conditions */
#define KERN_WARNING    KERN_SOH "4"    /* warning conditions */
#define KERN_NOTICE     KERN_SOH "5"    /* normal but significant condition */
#define KERN_INFO       KERN_SOH "6"    /* informational */
#define KERN_DEBUG      KERN_SOH "7"    /* debug-level messages */

#define KERN_DEFAULT    KERN_SOH "d"    /* the default kernel loglevel */

#define MODULE_VERSION(_version)

#define pr_emerg(fmt, ...) \
          printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...) \
          printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...) \
          printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) \
          printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning(fmt, ...) \
          printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn pr_warning
#define pr_notice(fmt, ...) \
          printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
          printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

#define ARRAY_SIZE(a)   (sizeof(a)/sizeof(a[0]))

#define HZ 100

#define EPERM            1      /* Operation not permitted */
#define ENOENT           2      /* No such file or directory */
#define ESRCH            3      /* No such process */
#define EINTR            4      /* Interrupted system call */
#define EIO              5      /* I/O error */
#define ENXIO            6      /* No such device or address */
#define E2BIG            7      /* Argument list too long */
#define ENOEXEC          8      /* Exec format error */
#define EBADF            9      /* Bad file number */
#define ECHILD          10      /* No child processes */
#define EAGAIN          11      /* Try again */
#define ENOMEM          12      /* Out of memory */
#define EACCES          13      /* Permission denied */
#define EFAULT          14      /* Bad address */
#define ENOTBLK         15      /* Block device required */
#define EBUSY           16      /* Device or resource busy */
#define EEXIST          17      /* File exists */
#define EXDEV           18      /* Cross-device link */
#define ENODEV          19      /* No such device */
#define ENOTDIR         20      /* Not a directory */
#define EISDIR          21      /* Is a directory */
#define EINVAL          22      /* Invalid argument */
#define ENFILE          23      /* File table overflow */
#define EMFILE          24      /* Too many open files */
#define ENOTTY          25      /* Not a typewriter */
#define ETXTBSY         26      /* Text file busy */
#define EFBIG           27      /* File too large */
#define ENOSPC          28      /* No space left on device */
#define ESPIPE          29      /* Illegal seek */
#define EROFS           30      /* Read-only file system */
#define EMLINK          31      /* Too many links */
#define EPIPE           32      /* Broken pipe */
#define EDOM            33      /* Math argument out of domain of func */
#define ERANGE          34      /* Math result not representable */

#define ETIMEDOUT       110     /* Connection timed out */
#define EREMOTEIO       121     /* Remote I/O error */



#define IS_REACHABLE(x) 1
typedef unsigned char           u8;
typedef unsigned short          u16;
typedef unsigned int            u32;
typedef unsigned long long      u64;
typedef signed char             s8;
typedef short                   s16;
typedef int                     s32;
typedef long long               s64;

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#ifdef __GNUC__
__extension__ typedef __signed__ long long __s64;
__extension__ typedef unsigned long long __u64;
#else
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif

typedef int spinlock_t;
typedef int dma_addr_t;
#define __iomem

#define PAGE_SIZE               4096

#define BITS_PER_LONG 64

typedef long            __kernel_long_t;
typedef unsigned long   __kernel_ulong_t;

# define likely(x)      __builtin_expect(!!(x), 1)
# define unlikely(x)    __builtin_expect(!!(x), 0)

#define WARN_ON(condition)

#define __always_inline
#define EXPORT_SYMBOL(x)

/**
 * fls - find last (most-significant) bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */

static __always_inline int fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}



#define dev_dbg(dev, format, ...)                    \
  do {                                                 \
            __dynamic_dev_dbg(dev, format, ##__VA_ARGS__); \
  } while (0)

#define roundup(x, y) (                                 \
    {                                                       \
            const typeof(y) __y = y;                        \
            (((x) + (__y - 1)) / __y) * __y;                \
    })

#define DIV_ROUND_CLOSEST(x, divisor)(                  \
    {                                                       \
            typeof(x) __x = x;                              \
            typeof(divisor) __d = divisor;                  \
            (((typeof(x))-1) > 0 ||                         \
                      ((typeof(divisor))-1) > 0 || (__x) > 0) ?      \
                    (((__x) + ((__d) / 2)) / (__d)) :       \
                    (((__x) - ((__d) / 2)) / (__d));        \
    }                                                       \
    )

union ktime {
          s64     tv64;
};

typedef union ktime ktime_t;            /* Kill this */


typedef enum {
  GFP_KERNEL,
  GFP_ATOMIC,
  __GFP_HIGHMEM,
  __GFP_HIGH
} gfp_t;

void *kzalloc(size_t size, gfp_t flags);

#endif
