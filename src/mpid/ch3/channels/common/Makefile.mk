## -*- Mode: Makefile; -*-
## vim: set ft=automake :
##
## Copyright (c) 2001-2012, The Ohio State University. All rights
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

if BUILD_MRAIL

include $(top_srcdir)/src/mpid/ch3/channels/common/src/Makefile.mk

endif

if BUILD_NEMESIS_NETMOD_IB

include $(top_srcdir)/src/mpid/ch3/channels/common/src/Makefile.mk

endif
