# -*- Mode: Makefile; -*-
# vim: set ft=automake :
#
# (C) 2011 by Argonne National Laboratory.
#     See COPYRIGHT in top-level directory.
#

# FIXME should these be prefixed with AM_ under automake?  Should we cut some or
# all of these?
MPICHDIR=..
#CC       = $(MPICHDIR)/bin/mpicc
CFLAGS   = $(WRAPPER_CFLAGS)
CXXFLAGS = $(WRAPPER_CXXFLAGS)
FFLAGS   = $(WRAPPER_FFLAGS)
FCFLAGS  = $(WRAPPER_FCFLAGS)
LDFLAGS  =
CPPFLAGS = $(WRAPPER_CPPFLAGS)
#DEFS     = 

AM_CPPFLAGS = -I$(top_builddir)/src/include -I${top_srcdir}/src/include
if BUILD_ROMIO
AM_CPPFLAGS += -I$(top_builddir)/src/mpi/romio/include -I${top_srcdir}/mpi/romio/include/include
endif BUILD_ROMIO

cpi_DEPENDENCIES =

# This directory of examples conceptually lives "outside" of the real MPICH2
# build and so we add "-lblah" for MPICH2 libs instead of specifying
# "../lib/libblah.la" (this invocation of make has no way to cause it to be
# rebuilt).
#
# FIXME we add the core MPICH2 libs explicitly to cpi_DEPENDENCIES to force cpi
# to get rebuilt whenever MPICH2's core libraries are rebuilt, but perhaps there
# is a better system?

# the make-time instances of libpmpich.la and libmpich.la live here
LDFLAGS += -L../lib
# the make-time instances of libmpl.la and libopa.la live here
LDFLAGS += -L../src/mpl -L../src/openpa/src

# Wrapper LDFLAGS need to be added at the end to make sure we link
# with the libraries we just built, and not any previously installed
# libraries.
LDFLAGS += $(WRAPPER_LDFLAGS)

external_libs = -lmpl -lopa $(WRAPPER_LIBS)
if BUILD_PROFILING_LIB
LIBS += -l@PMPILIBNAME@
cpi_DEPENDENCIES += ../lib/lib@PMPILIBNAME@.la
endif BUILD_PROFILING_LIB
LIBS += -l@MPILIBNAME@ $(external_libs)
cpi_DEPENDENCIES += ../lib/lib@MPILIBNAME@.la


noinst_PROGRAMS = cpi

# pmandel requires a separate set of socket calls (its a long story)
# and may not build on most platforms
EXTRA_PROGRAMS = pmandel pmandel_spawn pmandel_service pmandel_spaserv    \
                 pmandel_fence hellow icpi parent child srtest \
                 spawn_merge_parent spawn_merge_child1 spawn_merge_child2

# LIBS includes -lmpich and other libraries (e.g., -lpmpich if
# weak symbols are not supported)
# FIXME is the above true under automake?
hellow_SOURCES = hellow.c
srtest_SOURCES = srtest.c
child_SOURCES = child.c
parent_SOURCES = parent.c

# "cpi" is special, we always build it at the end of the regular "make" process
# and its main purpose is to catch missing symbol link-time errors before any
# tests are run.  So we add "-static" to its LDFLAGS in order to force this
# symbol resolution at build time instead of deferring it to run time as would
# occur in dynamic linking.
#
# TODO need to check that this works correctly if "--enable-shared
# --disable-static" is passed to configure.
cpi_SOURCES = cpi.c
cpi_LDADD = -lm
cpi_LDFLAGS = $(LDFLAGS) $(mpich_libtool_static_flag)

icpi_SOURCES = icpi.c
icpi_LDADD = -lm
pmandel_SOURCES = pmandel.c
pmandel_LDADD = -lm
pmandel_spawn_SOURCES = pmandel_spawn.c
pmandel_spawn_LDADD = -lm
pmandel_service_SOURCES = pmandel_service.c
pmandel_service_LDADD = -lm
pmandel_spaserv_SOURCES = pmandel_spaserv.c
pmandel_spaserv_LDADD = -lm
pmandel_fence_SOURCES = pmandel_fence.c
pmandel_fence_LDADD = -lm
spawn_merge_parent_SOURCES = spawn_merge_parent.c
spawn_merge_child1_SOURCES = spawn_merge_child1.c
spawn_merge_child2_SOURCES = spawn_merge_child2.c