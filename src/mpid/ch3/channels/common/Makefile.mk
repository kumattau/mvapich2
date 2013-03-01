## -*- Mode: Makefile; -*-
## vim: set ft=automake :
##
## Copyright (c) 2001-2013, The Ohio State University. All rights
## reserved.
##
## This file is part of the MVAPICH2 software package developed by the
## team members of The Ohio State University's Network-Based Computing
## Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
##
## For detailed copyright and licensing information, please refer to the
## copyright file COPYRIGHT in the top level MVAPICH2 directory.
##


if BUILD_LIB_CH3AFFINITY
noinst_LTLIBRARIES += libch3affinity.la
libch3affinity_la_SOURCES = src/mpid/ch3/channels/common/src/affinity/hwloc_bind.c
endif

if BUILD_LIB_SCR
EXTRA_DIST += src/mpid/ch3/channels/common/src/scr/LICENSE.TXT
AM_CPPFLAGS += -I$(top_srcdir)/src/mpid/ch3/channels/common/src/scr
noinst_LTLIBRARIES += libscr.la
libscr_la_SOURCES = src/mpid/ch3/channels/common/src/scr/scr_conf.h \
		    src/mpid/ch3/channels/common/src/scr/scr.h \
		    src/mpid/ch3/channels/common/src/scr/scr_err.h \
		    src/mpid/ch3/channels/common/src/scr/scr_io.h \
		    src/mpid/ch3/channels/common/src/scr/scr_util.h \
		    src/mpid/ch3/channels/common/src/scr/scr_hash.h \
		    src/mpid/ch3/channels/common/src/scr/tv_data_display.h \
		    src/mpid/ch3/channels/common/src/scr/scr_filemap.h \
		    src/mpid/ch3/channels/common/src/scr/scr_meta.h \
		    src/mpid/ch3/channels/common/src/scr/scr_config.h \
		    src/mpid/ch3/channels/common/src/scr/scr_param.h \
		    src/mpid/ch3/channels/common/src/scr/scr_log.h \
		    src/mpid/ch3/channels/common/src/scr/scr_copy_xor.h \
		    src/mpid/ch3/channels/common/src/scr/scr_halt.h \
		    src/mpid/ch3/channels/common/src/scr/scr.c \
		    src/mpid/ch3/channels/common/src/scr/scr_io.c \
		    src/mpid/ch3/channels/common/src/scr/scr_util.c \
		    src/mpid/ch3/channels/common/src/scr/scr_hash.c \
		    src/mpid/ch3/channels/common/src/scr/tv_data_display.c \
		    src/mpid/ch3/channels/common/src/scr/scr_filemap.c \
		    src/mpid/ch3/channels/common/src/scr/scr_meta.c \
		    src/mpid/ch3/channels/common/src/scr/scr_config.c \
		    src/mpid/ch3/channels/common/src/scr/scr_param.c \
		    src/mpid/ch3/channels/common/src/scr/scr_log.c \
		    src/mpid/ch3/channels/common/src/scr/scr_copy_xor.c \
		    src/mpid/ch3/channels/common/src/scr/scr_halt.c

lib_lib@MPILIBNAME@_la_LIBADD += libscr.la
endif

if BUILD_MRAIL

include $(top_srcdir)/src/mpid/ch3/channels/common/src/Makefile.mk

endif

if BUILD_NEMESIS_NETMOD_IB

include $(top_srcdir)/src/mpid/ch3/channels/common/src/Makefile.mk

endif
