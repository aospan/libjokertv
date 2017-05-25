/* stub */
#ifndef _LINUX_I2C_H
#define _LINUX_I2C_H

struct i2c_adapter {
        int nr;
        void * dev;
        void *algo_data;
};

struct i2c_msg {
        __u16 addr;     /* slave address                        */
        __u16 flags;
#define I2C_M_RD                0x0001  /* read data, from slave to master */
                                        /* I2C_M_RD is guaranteed to be 0x0001! */
#define I2C_M_TEN               0x0010  /* this is a ten bit chip address */
#define I2C_M_RECV_LEN          0x0400  /* length will be first received byte */
#define I2C_M_NO_RD_ACK         0x0800  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_IGNORE_NAK        0x1000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_REV_DIR_ADDR      0x2000  /* if I2C_FUNC_PROTOCOL_MANGLING */
#define I2C_M_NOSTART           0x4000  /* if I2C_FUNC_NOSTART */
#define I2C_M_STOP              0x8000  /* if I2C_FUNC_PROTOCOL_MANGLING */
        __u16 len;              /* msg length                           */
        __u8 *buf;              /* pointer to msg data                  */
};

/* Return the adapter number for a specific adapter */
static inline int i2c_adapter_id(struct i2c_adapter *adap)
{
          return adap->nr;
}

#define KBUILD_MODNAME __FILE__
#define MODULE_DESCRIPTION(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)

static inline void print_hex_dump_bytes(const char *prefix_str, int prefix_type,
                                            const void *buf, size_t len)
{
}

enum {
  DUMP_PREFIX_NONE,
  DUMP_PREFIX_ADDRESS,
  DUMP_PREFIX_OFFSET
};

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

#define EREMOTEIO       121     /* Remote I/O error */

#endif /* _LINUX_I2C_H */
