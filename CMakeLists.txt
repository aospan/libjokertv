cmake_minimum_required (VERSION 2.8.11)
project (u-drv)

set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Search libusb
find_package(PkgConfig)
pkg_check_modules(PC_LIBUSB libusb-1.0)

FIND_PATH(LIBUSB_INCLUDE_DIR libusb.h
	    PATHS ${PC_LIBUSB_INCLUDEDIR} ${PC_LIBUSB_INCLUDE_DIRS})

FIND_LIBRARY(LIBUSB_LIBRARIES NAMES usb-1.0
	    PATHS ${PC_LIBUSB_LIBDIR} ${PC_LIBUSB_LIBRARY_DIRS})

add_library (udrv linux/drivers/media/dvb-frontends/helene.c
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
  linux/drivers/media/dvb-core/dvb_math.c
  src/u-drv-main.c)

# WARNING: Special properties to compile kernel sources in user level 
set(KOPTIONS
	"-I ${CMAKE_SOURCE_DIR}/linux \
	-I ${CMAKE_SOURCE_DIR}/linux/include \
	-I ${CMAKE_SOURCE_DIR}/linux/arch/x86/include \
	-I ${CMAKE_SOURCE_DIR}/linux/arch/x86/include/generated \
	-I ${CMAKE_SOURCE_DIR}/linux/arch/x86/include/uapi \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi \
	-I ${CMAKE_SOURCE_DIR}/linux/drivers/media/dvb-core \
	-DCONFIG_PRINTK \
	-D__KERNEL__ \
	-DCONFIG_64BIT \
	-DCONFIG_X86_64 \
	-DCONFIG_HAVE_ARCH_WITHIN_STACK_FRAMES \
	-DCONFIG_PAGE_OFFSET=0x80000000 \
	-DCONFIG_X86_L1_CACHE_SHIFT=6 \
	-DCONFIG_THREAD_INFO_IN_TASK \
	-DKBUILD_MODNAME='\"udrv\"' \
	-DCONFIG_DVB_HELENE \
	-DCONFIG_DVB_CXD2841ER \
	-DCONFIG_DVB_LGDT3306A \
	-DCONFIG_DVB_ATBM888x \
	-DCONFIG_DVB_TPS65233 \
	-DCONFIG_DYNAMIC_DEBUG \
	-D_GCC_WRAP_STDINT_H \
	-DDEBUG \
	-DCONFIG_SLOB \
	-D_STDLIB_H \
	-D_SYS_TYPES_H \
	-D_STRUCT_TIMEVAL\
	-D_SYS_TIME_H\
	-D_TIME_H\
	-D_SYS_PARAM_H \
	-D_SYS_SELECT_H \
	-D__dev_t_defined \
	-D__nlink_t_defined \
	-D__timer_t_defined \
	-include ./include/linux/kconfig.h")

set(INCLUDE_USER "-I ${LIBUSB_INCLUDE_DIR} -I${CMAKE_SOURCE_DIR}/include")

set_target_properties(udrv PROPERTIES COMPILE_FLAGS "${KOPTIONS} ${INCLUDE_USER}")

add_library (oc_i2c src/oc_i2c.c)
set_target_properties(oc_i2c PROPERTIES COMPILE_FLAGS ${INCLUDE_USER})

find_package( Threads )
add_executable(joker-tv src/joker-tv.cc)
set_target_properties(joker-tv PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
target_link_libraries(joker-tv udrv oc_i2c "${CMAKE_THREAD_LIBS_INIT} ${LIBUSB_LIBRARIES}")

add_executable(i2c-scan src/i2c-scan.c)
set_target_properties(i2c-scan PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
target_link_libraries(i2c-scan oc_i2c ${LIBUSB_LIBRARIES})

# TS stream generate/validate utilities
add_executable(tsgen src/tsgen.c)
set_target_properties(tsgen PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")

add_executable(tscheck src/tscheck.c)
set_target_properties(tscheck PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
