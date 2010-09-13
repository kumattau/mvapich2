# -*- Mode: Makefile; -*-
#
# (C) 2008 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

libhydra_la_SOURCES += $(top_srcdir)/tools/bind/plpa/bind_plpa.c

AM_CPPFLAGS += -I$(top_srcdir)/tools/bind/plpa/plpa/src/libplpa \
	-I$(top_builddir)/tools/bind/plpa/plpa/src/libplpa

# Append plpa to the external subdirs, so it gets built first
external_subdirs += tools/bind/plpa/plpa
external_ldflags += -L$(top_builddir)/tools/bind/plpa/plpa/src/libplpa
external_libs += -lplpa
