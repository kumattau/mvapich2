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


if BUILD_CH3_PSM

AM_CPPFLAGS += -I$(top_srcdir)/src/mpid/ch3/channels/psm/include

lib_lib@MPILIBNAME@_la_SOURCES +=   \
    src/mpid/ch3/channels/psm/src/mpidi_calls.c  \
    src/mpid/ch3/channels/psm/src/psm_entry.c    \
    src/mpid/ch3/channels/psm/src/psm_exit.c     \
    src/mpid/ch3/channels/psm/src/psm_istart.c   \
    src/mpid/ch3/channels/psm/src/psm_send.c     \
    src/mpid/ch3/channels/psm/src/psm_recv.c     \
    src/mpid/ch3/channels/psm/src/psm_queue.c    \
    src/mpid/ch3/channels/psm/src/psm_1sided.c   \
    src/mpid/ch3/channels/psm/src/psm_comm.c     \
    src/mpid/ch3/channels/psm/src/psm_vbuf.c     \
    src/mpid/ch3/channels/psm/src/ch3_win_fns.c

endif BUILD_CH3_PSM