cmake_minimum_required (VERSION 2.8.12)
project (joker-tv)

set(CMAKE_MACOSX_RPATH 1)

#set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

find_package( Threads REQUIRED)
find_package( PkgConfig )

# unser OSx we need some system libs for libusb functionality
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	find_library(LIBIOKit IOKit)
	find_library(LIBCoreFoundation CoreFoundation)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	find_library(LIBUDEV udev)
endif()


# Add libusb
# we should build our own libusb with patches for Isoc transfers
# until it not accepted to mainline
include(ExternalProject)
ExternalProject_Add(libusb
	GIT_REPOSITORY https://github.com/philemonf/libusb.git
	CONFIGURE_COMMAND ./autogen.sh COMMAND ./configure --enable-static --with-pic
	UPDATE_COMMAND ""
	INSTALL_COMMAND ""
	BUILD_COMMAND MACOSX_DEPLOYMENT_TARGET=10.6 make
	BUILD_IN_SOURCE 1
	)
set(PC_LIBUSB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libusb-prefix/src/libusb//libusb")
set(PC_LIBUSB_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/libusb-prefix/src/libusb/libusb/.libs/libusb-1.0${CMAKE_STATIC_LIBRARY_SUFFIX}")

# build libdvbpsi for TS processing
include(ExternalProject)
ExternalProject_Add(libdvbpsi
	GIT_REPOSITORY https://code.videolan.org/videolan/libdvbpsi.git
	CONFIGURE_COMMAND ./bootstrap COMMAND ./configure --enable-static --with-pic
	UPDATE_COMMAND ""
	INSTALL_COMMAND ""
	BUILD_COMMAND MACOSX_DEPLOYMENT_TARGET=10.6 make
	BUILD_IN_SOURCE 1
	)
set(PC_LIBDVBPSI_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/" "${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/tables/")
set(PC_LIBDVBPSI_LIBRARIES "${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/.libs/libdvbpsi${CMAKE_STATIC_LIBRARY_SUFFIX}")

#set sources list
SET (LINUX_SRC linux/drivers/media/dvb-frontends/helene.c
	linux/drivers/media/dvb-frontends/lgdt3306a.c
	linux/drivers/media/dvb-frontends/cxd2841er.c
	linux/drivers/media/dvb-frontends/tps65233.c
	linux/drivers/media/dvb-frontends/atbm888x.c
	linux/drivers/media/dvb-core/dvb_math.c
	src/u_drv_tune.c)

SET (JOKERTV_SRC src/u_drv_data.c
	src/joker_i2c.c
	src/joker_fpga.c
	src/joker_spi.c
	src/joker_ci.c
	src/joker_utils.c
	src/joker_ts.c)

set(KOPTIONS
	"-I ${CMAKE_SOURCE_DIR}/linux/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi/linux/dvb/ \
	-I ${CMAKE_SOURCE_DIR}/linux/drivers/media/dvb-core/ \
	-include ./include/linux/types.h")

set(CFLAGS_USER "${PC_LIBUSB_CFLAGS}")
set(INCLUDE_USER ${PC_LIBUSB_INCLUDE_DIR} ${PC_LIBDVBPSI_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/include)
set(EXT_LIBS ${CMAKE_THREAD_LIBS_INIT} ${PC_LIBUSB_LIBRARIES}
	${PC_LIBDVBPSI_LIBRARIES} ${LIBIOKit} ${LIBCoreFoundation} ${LIBUDEV})

#install header files
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include
          FILES_MATCHING PATTERN "*.h")

#combined shared lib
add_library (jokertv SHARED ${LINUX_SRC} ${JOKERTV_SRC})
set_target_properties(jokertv PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
set_source_files_properties(${LINUX_SRC} PROPERTIES COMPILE_FLAGS ${KOPTIONS})
target_link_libraries(jokertv ${EXT_LIBS})
target_include_directories(jokertv PUBLIC ${INCLUDE_USER})
add_dependencies(jokertv libusb libdvbpsi)
install(TARGETS jokertv DESTINATION lib)

#simple program: tune and save TS to file
add_executable(joker-tv src/joker-tv.cc)
set_target_properties(joker-tv PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
target_link_libraries(joker-tv jokertv ${EXT_LIBS})
add_dependencies(joker-tv libusb libdvbpsi)
target_include_directories(joker-tv PUBLIC ${INCLUDE_USER})
install(TARGETS joker-tv DESTINATION bin)

#simple program to scan i2c bus
add_executable(i2c-scan src/i2c-scan.c)
set_target_properties(i2c-scan PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
target_include_directories(i2c-scan PUBLIC ${INCLUDE_USER})
target_link_libraries(i2c-scan jokertv ${EXT_LIBS})
install(TARGETS i2c-scan DESTINATION bin)

# TS stream generate/validate utilities
add_executable(tsgen src/tsgen.c)
target_include_directories(tsgen PUBLIC ${INCLUDE_USER})

add_executable(tscheck src/tscheck.c)
target_include_directories(tscheck PUBLIC ${INCLUDE_USER})

#Installers generator
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Joker TV")
SET(CPACK_PACKAGE_VENDOR "Joker Systems Inc.")
SET(CPACK_PACKAGE_VERSION_MAJOR "0")
SET(CPACK_PACKAGE_VERSION_MINOR "8")
SET(CPACK_PACKAGE_VERSION_PATCH "0")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(CPACK_GENERATOR "DragNDrop;TGZ")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CPACK_GENERATOR "TGZ")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(CPACK_GENERATOR "NSIS;ZIP")
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
	include(InstallRequiredSystemLibraries)
	FIND_FILE(WINPTHREAD_LIB libwinpthread-1.dll)
	INSTALL(TARGETS jokertv DESTINATION "bin")
	install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION programs COMPONENT applications)
endif()

INCLUDE(CPack)
