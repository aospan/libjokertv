cmake_minimum_required (VERSION 2.8.12)
project (joker_tv)
set (joker_tv_VERSION_MAJOR 1)
set (joker_tv_VERSION_MINOR 3)
set (joker_tv_VERSION_PATCH 0)
set (joker_tv_VERSION
	${joker_tv_VERSION_MAJOR}.${joker_tv_VERSION_MINOR}.${joker_tv_VERSION_PATCH})

set(CMAKE_MACOSX_RPATH 1)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
		"Choose the type of build, options are: Debug Release
		RelWithDebInfo MinSizeRel."
		FORCE)
endif(NOT CMAKE_BUILD_TYPE)

find_package( Threads REQUIRED)
find_package( PkgConfig )

add_subdirectory("3party/libdvben50221/")

# under OSx we need some system libs for libusb functionality
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	find_library(LIBIOKit IOKit)
	find_library(LIBCoreFoundation CoreFoundation)
	find_library(LIBICONV iconv)
	find_library(LIBobjc objc)
	set(PTHREADS_LIB "pthread")
	set(LINUXLIB -Wl,-force_load,${CMAKE_CURRENT_BINARY_DIR}/libjokertv_linux.a)
	set(EN50221LIB -Wl,-force_load,${CMAKE_CURRENT_BINARY_DIR}/3party/libdvben50221/libdvben50221.a)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	find_library(LIBICONV libiconv.a REQUIRED)
	set(LIBWS2 "-lws2_32")
	set(PTHREADS_LIB "-Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
	set(LINUXLIB -Wl,--whole-archive ${CMAKE_CURRENT_BINARY_DIR}/libjokertv_linux.a -Wl,--no-whole-archive)
	set(EN50221LIB -Wl,--whole-archive ${CMAKE_CURRENT_BINARY_DIR}/3party/libdvben50221/libdvben50221.a -Wl,--no-whole-archive)
	set(INCLUDE_PLATFORM "include/win")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(LINUXLIB -Wl,--whole-archive ${CMAKE_CURRENT_BINARY_DIR}/libjokertv_linux.a -Wl,--no-whole-archive)
	set(EN50221LIB -Wl,--whole-archive ${CMAKE_CURRENT_BINARY_DIR}/3party/libdvben50221/libdvben50221.a -Wl,--no-whole-archive)
	find_library(LIBUDEV udev)
endif()

# build libdvbpsi for TS processing
include(ExternalProject)
ExternalProject_Add(libdvbpsi
	GIT_REPOSITORY https://code.videolan.org/videolan/libdvbpsi.git
	GIT_TAG "1.3.0"
	CONFIGURE_COMMAND ./bootstrap COMMAND ./configure --enable-static
	--with-pic
	UPDATE_COMMAND ""
	INSTALL_COMMAND ""
	BUILD_COMMAND MACOSX_DEPLOYMENT_TARGET=10.6 make
	BUILD_IN_SOURCE 1
	)
set(PC_LIBDVBPSI_INCLUDE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/"
	"${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/descriptors/"
	"${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/tables/")
set(PC_LIBDVBPSI_LIBRARIES
	"${CMAKE_CURRENT_BINARY_DIR}/libdvbpsi-prefix/src/libdvbpsi/src/.libs/libdvbpsi${CMAKE_STATIC_LIBRARY_SUFFIX}")

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
	src/joker_en50221.c
	src/joker_en50221_server.c
	src/joker_ts.c)

set(KOPTIONS "-include ${CMAKE_SOURCE_DIR}/linux/include/linux/types.h")
set(KINCLUDE_DIRS
	${CMAKE_SOURCE_DIR}/include/
	${CMAKE_SOURCE_DIR}/linux/
	${CMAKE_SOURCE_DIR}/linux/include/
	${CMAKE_SOURCE_DIR}/linux/include/uapi
	${CMAKE_SOURCE_DIR}/linux/include/uapi/linux/dvb/
	${CMAKE_SOURCE_DIR}/linux/drivers/media/dvb-core/)

set(CFLAGS_USER "${PC_LIBUSB_CFLAGS}")
set(INCLUDE_USER ${INCLUDE_PLATFORM} $<BUILD_INTERFACE:include ${PC_LIBDVBPSI_INCLUDE_DIR}> 3party)
set(EXT_LIBS ${CMAKE_THREAD_LIBS_INIT}
	${PC_LIBDVBPSI_LIBRARIES} ${LIBIOKit} ${LIBCoreFoundation} ${LIBobjc} ${LIBUDEV} ${LIBICONV} ${LIBWS2})

#install header files
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include
	FILES_MATCHING PATTERN "*.h")

#static lib with kernel stuff
add_library (jokertv_linux STATIC ${LINUX_SRC})
target_include_directories(jokertv_linux PRIVATE ${KINCLUDE_DIRS})
set_source_files_properties(${LINUX_SRC} PROPERTIES COMPILE_FLAGS ${KOPTIONS})
set_target_properties(jokertv_linux PROPERTIES COMPILE_FLAGS "-fPIC")

#combined shared lib
add_library (jokertv SHARED ${JOKERTV_SRC})
set_property(TARGET jokertv PROPERTY VERSION ${joker_tv_VERSION})
set_target_properties(jokertv PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
add_dependencies(jokertv libusb libdvbpsi jokertv_linux dvben50221)
target_link_libraries(jokertv
	PRIVATE ${LINUXLIB}
	PRIVATE ${EN50221LIB}
	PRIVATE ${PC_LIBUSB_LIBRARIES}
	PRIVATE ${PC_LIBDVBPSI_LIBRARIES}
	PRIVATE ${LIBICONV}
	PRIVATE ${LIBWS2}
	PRIVATE ${PTHREADS_LIB}
	PRIVATE ${EXT_LIBS}
	)
target_include_directories(jokertv PRIVATE
	$<BUILD_INTERFACE:include ${PC_LIBUSB_INCLUDE_DIR}>
	#$<BUILD_INTERFACE:include ${PC_LIBDVBPSI_INCLUDE_DIR}/dvbpsi>
	${INCLUDE_USER}
	)

#simple program: tune and save TS to file
add_executable(joker-tv src/joker-tv.cc)
set_target_properties(joker-tv PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
target_link_libraries(joker-tv jokertv ${EXT_LIBS})
target_include_directories(joker-tv PUBLIC ${INCLUDE_USER} ${PC_LIBUSB_INCLUDE_DIR})
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
target_link_libraries(tsgen ${PC_LIBDVBPSI_LIBRARIES})
add_dependencies(tsgen libdvbpsi)
install(TARGETS tsgen DESTINATION bin)

add_executable(tscheck src/tscheck.c)
target_include_directories(tscheck PUBLIC ${INCLUDE_USER})
install(TARGETS tscheck DESTINATION bin)

##############################################
# prepare cmake files for downstream projects
##############################################
install(TARGETS jokertv EXPORT jokertvTargets
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
	RUNTIME DESTINATION bin
	INCLUDES DESTINATION include
	)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
	"${CMAKE_CURRENT_BINARY_DIR}/jokertv/jokertvConfigVersion.cmake"
	VERSION ${joker_tv_VERSION}
	COMPATIBILITY AnyNewerVersion
	)

export(EXPORT jokertvTargets
	FILE
	"${CMAKE_CURRENT_BINARY_DIR}/jokertv/jokertvTargets.cmake"
	)

configure_file(cmake/jokertvConfig.cmake
	"${CMAKE_CURRENT_BINARY_DIR}/jokertv/jokertvConfig.cmake"
	COPYONLY
	)

set(ConfigPackageLocation lib/cmake/jokertv)
install(EXPORT jokertvTargets
	FILE
	jokertvTargets.cmake
	DESTINATION
	${ConfigPackageLocation}
	)

install(
	FILES
	cmake/jokertvConfig.cmake
	"${CMAKE_CURRENT_BINARY_DIR}/jokertv/jokertvConfigVersion.cmake"
	DESTINATION
	${ConfigPackageLocation}
	COMPONENT
	Devel
	)

######################
# Installers generator
######################
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Joker TV")
SET(CPACK_PACKAGE_VENDOR "Joker Systems Inc.")
SET(CPACK_PACKAGE_VERSION_MAJOR ${joker_tv_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${joker_tv_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${joker_tv_VERSION_PATCH})

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(CPACK_GENERATOR "DragNDrop;TGZ")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CPACK_GENERATOR "TGZ")
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(CPACK_GENERATOR "NSIS;ZIP")
	set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
	include(InstallRequiredSystemLibraries)
	INSTALL(TARGETS jokertv DESTINATION "bin")
	install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} DESTINATION programs COMPONENT applications)
endif()

INCLUDE(CPack)
