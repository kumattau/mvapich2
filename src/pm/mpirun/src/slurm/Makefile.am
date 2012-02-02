AM_CPPFLAGS = -I$(top_srcdir)/../../mpid/ch3/channels/common/include
AM_CPPFLAGS += -I$(top_srcdir) -I$(srcdir)/../
AM_YFLAGS = -d

noinst_LIBRARIES = libslurm.a

libslurm_a_SOURCES = nodelist_parser.y nodelist_scanner.l suffixlist.c tasklist_parser.y tasklist_scanner.l slurm_startup.c
BUILT_SOURCES = nodelist_parser.h tasklist_parser.h
