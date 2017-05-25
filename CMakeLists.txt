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
  linux/drivers/media/dvb-frontends/lgdt3306a.c
  linux/drivers/media/dvb-frontends/cxd2841er.c
  linux/drivers/media/dvb-frontends/tps65233.c
  linux/drivers/media/dvb-core/dvb_math.c
  src/u_drv_tune.c)

add_library (udrv_data
  src/u_drv_data.c)

set(KOPTIONS
	"-I ${CMAKE_SOURCE_DIR}/linux/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi/linux/dvb/ \
	-I ${CMAKE_SOURCE_DIR}/linux/drivers/media/dvb-core/ \
	-include ./include/linux/types.h")

set(INCLUDE_USER "-I ${LIBUSB_INCLUDE_DIR} -I${CMAKE_SOURCE_DIR}/include")

set_target_properties(udrv PROPERTIES COMPILE_FLAGS "${KOPTIONS} ${INCLUDE_USER}")
set_target_properties(udrv_data PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")

add_library (joker_i2c src/joker_i2c.c)
set_target_properties(joker_i2c PROPERTIES COMPILE_FLAGS ${INCLUDE_USER})

add_library (joker_fpga src/joker_fpga.c)
set_target_properties(joker_fpga PROPERTIES COMPILE_FLAGS ${INCLUDE_USER})

find_package( Threads )
add_executable(joker-tv src/joker-tv.cc)
set_target_properties(joker-tv PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
target_link_libraries(joker-tv udrv udrv_data joker_i2c joker_fpga ${CMAKE_THREAD_LIBS_INIT} ${LIBUSB_LIBRARIES})

add_executable(i2c-scan src/i2c-scan.c)
set_target_properties(i2c-scan PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
target_link_libraries(i2c-scan joker_i2c joker_fpga ${LIBUSB_LIBRARIES})

# TS stream generate/validate utilities
add_executable(tsgen src/tsgen.c)
set_target_properties(tsgen PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")

add_executable(tscheck src/tscheck.c)
set_target_properties(tscheck PROPERTIES COMPILE_FLAGS "${INCLUDE_USER}")
