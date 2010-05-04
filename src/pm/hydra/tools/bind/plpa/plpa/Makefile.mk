# -*- Mode: Makefile; -*-
#
# (C) 2008 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

AM_CPPFLAGS += -I$(top_srcdir)/tools/bind/plpa/plpa

libhydra_a_SOURCES += $(top_srcdir)/tools/bind/plpa/plpa/plpa_api_probe.c \
	$(top_srcdir)/tools/bind/plpa/plpa/plpa_dispatch.c \
	$(top_srcdir)/tools/bind/plpa/plpa/plpa_map.c \
	$(top_srcdir)/tools/bind/plpa/plpa/plpa_runtime.c
