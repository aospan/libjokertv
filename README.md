# u-drv project

Compile Linux kernel drivers inside user-level with different stubs (i2c, printk, kmalloc, etc).
Now we can run and debug drivers as user-level process.

(c) Abylay Ospan <aospan@jokersys.com>, 2017
LICENSE: GPLv2

I have compiled driver for Joker TV card (https://tv.jokersys.com)  but this project can be used for any other drivers (check CMakeLists.txt and src/u-drv.c for details)

# Usage

make symlink for your Linux kernel sources (it can be any version different from your running kernel):
```
ln -s /usr/src/linux-stable linux
```

then compile:
```
mkdir build
cd build
cmake ../
make
```

# Run
```
./u-drv
```

if everything is fine then you can see progress. Something like this:
```
helene_set_params(): tune done
atbm888x_write_reg: reg=0x0103, data=0x00
Try 0
atbm888x_read_reg: reg=0x030D, data=0x01
ATBM888x locked!
```

## I2C adapter
i2c adapter is FT232H based adapter:
https://www.adafruit.com/product/2264

following packages should be installed:
```
apt install libftdi-dev
```

and
https://github.com/devttys0/libmpsse.git
