## -*- Mode: Makefile; -*-
## vim: set ft=automake :
##
## (C) 2011 by Argonne National Laboratory.
##     See COPYRIGHT in top-level directory.
##

if BUILD_NEMESIS_NETMOD_ELAN

lib_lib@MPILIBNAME@_la_SOURCES +=                                     \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_finalize.c \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_init.c     \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_poll.c     \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_send.c     \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_register.c \
    src/mpid/ch3/channels/nemesis/netmod/elan/elan_test.c

noinst_HEADERS += src/mpid/ch3/channels/nemesis/netmod/elan/elan_impl.h

endif BUILD_NEMESIS_NETMOD_ELAN

