# -*- Mode: Makefile; -*-
#
# (C) 2008 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

AM_CPPFLAGS += -I$(top_srcdir)/pm/include

include pm/utils/Makefile.mk

if hydra_pm_pmiserv
include pm/pmiserv/Makefile.mk
endif
