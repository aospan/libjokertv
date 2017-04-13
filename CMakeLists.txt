cmake_minimum_required (VERSION 2.8.11)
project (u-drv)

include_directories(include)
include_directories(linux)
include_directories(linux/include)
include_directories(linux/arch/x86/include)
include_directories(linux/arch/x86/include/generated)
include_directories(linux/arch/x86/include/uapi)
include_directories(linux/include/uapi)
include_directories(linux/drivers/media/dvb-core)

add_definitions(-DCONFIG_PRINTK)
add_definitions(-D__KERNEL__)
add_definitions(-DCONFIG_64BIT)
add_definitions(-DCONFIG_X86_64)
add_definitions(-DCONFIG_HAVE_ARCH_WITHIN_STACK_FRAMES)
add_definitions(-DCONFIG_PAGE_OFFSET=0x80000000)
add_definitions(-DCONFIG_X86_L1_CACHE_SHIFT=6)
add_definitions(-DCONFIG_THREAD_INFO_IN_TASK)
add_definitions(-DKBUILD_MODNAME="u-drv")
add_definitions(-DCONFIG_DVB_HELENE)
add_definitions(-DCONFIG_DVB_CXD2841ER)
add_definitions(-DCONFIG_DVB_LGDT3306A)
add_definitions(-DCONFIG_DVB_ATBM888x)
add_definitions(-DCONFIG_DVB_TPS65233)
add_definitions(-DCONFIG_DYNAMIC_DEBUG)
add_definitions(-D_GCC_WRAP_STDINT_H)
add_definitions(-DDEBUG)
#add_definitions(-D_LINUX_TYPES_H)
#add_definitions(-D_SYS_TYPES_H)
#add_definitions(-D_SIGNAL_H)
add_definitions(-D_STDLIB_H)
add_definitions(-D_SYS_PARAM_H)
add_definitions(-D_SYS_SELECT_H)
add_definitions(-D__dev_t_defined)
add_definitions(-D__nlink_t_defined)
add_definitions(-D__timer_t_defined)

SET(CMAKE_C_FLAGS "-include ./include/linux/kconfig.h")

set(CMAKE_BUILD_TYPE Debug)

add_library (libu-drv linux/drivers/media/dvb-frontends/helene.c
  linux/drivers/media/dvb-frontends/cxd2841er.c
  linux/drivers/media/dvb-frontends/lgdt3306a.c
  linux/drivers/media/dvb-frontends/atbm888x.c
  linux/drivers/media/dvb-frontends/tps65233.c
  linux/drivers/media/v4l2-core/videobuf2-dvb.c
  linux/drivers/media/v4l2-core/videobuf2-core.c
  linux/drivers/media/dvb-core/dvb_demux.c
  linux/drivers/media/dvb-core/dvbdev.c
  linux/drivers/media/dvb-core/dvb_frontend.c
  linux/drivers/media/dvb-core/dmxdev.c
  linux/drivers/media/dvb-core/dvb_net.c
  linux/drivers/media/dvb-core/dvb_math.c)

add_executable(u-drv src/u-drv.c)
target_link_libraries(u-drv libu-drv mpsse)
