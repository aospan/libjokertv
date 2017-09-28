cmake_minimum_required (VERSION 2.8.12)
project (libdvben50221)

set(CMAKE_MACOSX_RPATH 1)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
		"Choose the type of build, options are: Debug Release
		RelWithDebInfo MinSizeRel."
		FORCE)
endif(NOT CMAKE_BUILD_TYPE)

find_package( PkgConfig )

#set sources list
SET (SRC asn_1.c             en50221_app_ca.c        en50221_app_epg.c
	en50221_app_rm.c         en50221_app_utils.c  en50221_stdcam_hlci.c
	en50221_app_ai.c    en50221_app_datetime.c  en50221_app_lowspeed.c
	en50221_app_smartcard.c  en50221_session.c    en50221_stdcam_llci.c
	en50221_app_auth.c  en50221_app_dvb.c       en50221_app_mmi.c
	en50221_app_teletext.c   en50221_stdcam.c     en50221_transport.c
	)

#static lib 
add_library (dvben50221 STATIC ${SRC})
target_include_directories(dvben50221 PRIVATE ../)
#set_source_files_properties(${SRC} PROPERTIES COMPILE_FLAGS ${KOPTIONS})
set_target_properties(dvben50221 PROPERTIES COMPILE_FLAGS "-fPIC -DLOG_LEVEL=1")
