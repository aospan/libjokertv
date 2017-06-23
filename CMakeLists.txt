cmake_minimum_required (VERSION 2.8.12)
project (joker-tv)

set(CMAKE_MACOSX_RPATH 1)

set(CMAKE_BUILD_TYPE Debug)
#set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

find_package( Threads REQUIRED)

# Search libusb
find_package(PkgConfig)
#pkg_check_modules(PC_LIBUSB libusb-1.0)

#Add libusb
#we should build our own libusb with our patches for Isoc transfers
set(PC_LIBUSB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/libusb-prefix/src/libusb//libusb")
set(PC_LIBUSB_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}/libusb-prefix/src/libusb/libusb/.libs/")
set(PC_LIBUSB_LIBRARIES "libusb-1.0.0${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(PC_LIBUSB_LIBRARIES_FULL "${PC_LIBUSB_LIBDIR}/${PC_LIBUSB_LIBRARIES}")
add_library(libusb_imported SHARED IMPORTED)
set_target_properties(libusb_imported PROPERTIES IMPORTED_LOCATION ${PC_LIBUSB_LIBRARIES_FULL})
install(FILES ${PC_LIBUSB_LIBRARIES_FULL} DESTINATION lib)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(PC_LIBUSB_INSTALL install_name_tool -id @rpath/libusb-1.0.0.dylib ${PC_LIBUSB_LIBRARIES_FULL})
	#dmg image binary should has relative path under OSx
	SET(CMAKE_INSTALL_RPATH "@loader_path/../lib")
else()
	set(PC_LIBUSB_INSTALL )
endif()

#we should build our own libusb with our patches for Isoc transfers
include(ExternalProject)
ExternalProject_Add(libusb
	GIT_REPOSITORY https://github.com/aospan/libusb.git
	CONFIGURE_COMMAND ./autogen.sh COMMAND ./configure
	UPDATE_COMMAND ""
	INSTALL_COMMAND ${PC_LIBUSB_INSTALL}
	BUILD_IN_SOURCE 1
	)

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
	src/joker_ci.c)

set(KOPTIONS
	"-I ${CMAKE_SOURCE_DIR}/linux/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/ \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi \
	-I ${CMAKE_SOURCE_DIR}/linux/include/uapi/linux/dvb/ \
	-I ${CMAKE_SOURCE_DIR}/linux/drivers/media/dvb-core/ \
	-include ./include/linux/types.h")

set(CFLAGS_USER "${PC_LIBUSB_CFLAGS}")
set(INCLUDE_USER ${PC_LIBUSB_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/include)

#combined shared lib
add_library (jokertv SHARED ${LINUX_SRC} ${JOKERTV_SRC})
set_target_properties(jokertv PROPERTIES COMPILE_FLAGS "${KOPTIONS} ${CFLAGS_USER}")
set_target_properties(jokertv PROPERTIES LINK_FLAGS "-L${PC_LIBUSB_LIBDIR}")
target_link_libraries(jokertv ${CMAKE_THREAD_LIBS_INIT} ${PC_LIBUSB_LIBRARIES})
target_include_directories(jokertv PUBLIC ${INCLUDE_USER})
add_dependencies(jokertv libusb)
install(TARGETS jokertv DESTINATION lib)

#simple program: tune and save TS to file
add_executable(joker-tv src/joker-tv.cc)
set_target_properties(joker-tv PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
set_target_properties(joker-tv PROPERTIES LINK_FLAGS "-L${PC_LIBUSB_LIBDIR}")
target_link_libraries(joker-tv jokertv ${CMAKE_THREAD_LIBS_INIT} ${PC_LIBUSB_LIBRARIES})
add_dependencies(joker-tv libusb)
target_include_directories(joker-tv PUBLIC ${INCLUDE_USER})
install(TARGETS joker-tv DESTINATION bin)

#simple program to scan i2c bus
add_executable(i2c-scan src/i2c-scan.c)
set_target_properties(i2c-scan PROPERTIES COMPILE_FLAGS "${CFLAGS_USER}")
set_target_properties(i2c-scan PROPERTIES LINK_FLAGS "-L${PC_LIBUSB_LIBDIR}")
target_include_directories(i2c-scan PUBLIC ${INCLUDE_USER})
target_link_libraries(i2c-scan jokertv ${PC_LIBUSB_LIBRARIES})
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
SET(CPACK_PACKAGE_VERSION_MINOR "1")
SET(CPACK_PACKAGE_VERSION_PATCH "1")

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(CPACK_GENERATOR "DragNDrop")
else()
	set(CPACK_GENERATOR "TGZ")
endif()

INCLUDE(CPack)
