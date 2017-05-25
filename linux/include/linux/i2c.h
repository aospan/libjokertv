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

#endif /* _LINUX_I2C_H */
