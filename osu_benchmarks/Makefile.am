 #
 # Copyright (C) 2002-2014 the Network-Based Computing Laboratory
 # (NBCL), The Ohio State University. 
 #
 # Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 #
 # For detailed copyright and licensing information, please refer to the
 # copyright file COPYRIGHT in the top level OMB directory.
 #

pkglibexec_PROGRAMS =		\
    osu_alltoallv		\
    osu_allgatherv		\
    osu_scatterv		\
    osu_gatherv			\
    osu_reduce_scatter		\
    osu_allgather		\
    osu_barrier			\
    osu_reduce			\
    osu_allreduce		\
    osu_gather			\
    osu_scatter			\
    osu_alltoall		\
    osu_bcast			\
    osu_bibw			\
    osu_bw			\
    osu_latency			\
    osu_mbw_mr			\
    osu_multi_lat		\
    osu_acc_latency		\
	osu_get_acc_latency \
	osu_cas_latency     \
	osu_fop_latency     \
    osu_get_bw			\
    osu_get_latency		\
    osu_latency_mt		\
    osu_put_bibw		\
    osu_put_bw			\
    osu_put_latency		\
    osu_init			\
    osu_hello			\
    osu_iallgather		\
    osu_ibcast			\
    osu_ialltoall		\
    osu_ibarrier		\
    osu_igather			\
    osu_iscatter

osu_bw_SOURCES = osu_bw.c osu_pt2pt.c osu_pt2pt.h
osu_bibw_SOURCES = osu_bibw.c osu_pt2pt.c osu_pt2pt.h
osu_latency_SOURCES = osu_latency.c osu_pt2pt.c osu_pt2pt.h
osu_multi_lat_SOURCES = osu_multi_lat.c osu_pt2pt.c osu_pt2pt.h
osu_latency_mt_SOURCES = osu_latency_mt.c osu_pt2pt.c osu_pt2pt.h

osu_put_latency_SOURCES = osu_put_latency.c osu_1sc.c osu_1sc.h
osu_put_bw_SOURCES = osu_put_bw.c osu_1sc.c osu_1sc.h
osu_put_bibw_SOURCES = osu_put_bibw.c osu_1sc.c osu_1sc.h
osu_get_latency_SOURCES = osu_get_latency.c osu_1sc.c osu_1sc.h
osu_get_bw_SOURCES = osu_get_bw.c osu_1sc.c osu_1sc.h
osu_acc_latency_SOURCES = osu_acc_latency.c osu_1sc.c osu_1sc.h
osu_fop_latency_SOURCES = osu_fop_latency.c osu_1sc.c osu_1sc.h
osu_cas_latency_SOURCES = osu_cas_latency.c osu_1sc.c osu_1sc.h

osu_alltoallv_SOURCES = osu_alltoallv.c osu_coll.c osu_coll.h
osu_allgatherv_SOURCES = osu_allgatherv.c osu_coll.c osu_coll.h
osu_scatterv_SOURCES = osu_scatterv.c osu_coll.c osu_coll.h
osu_gatherv_SOURCES = osu_gatherv.c osu_coll.c osu_coll.h
osu_reduce_scatter_SOURCES = osu_reduce_scatter.c osu_coll.c osu_coll.h
osu_barrier_SOURCES = osu_barrier.c osu_coll.c osu_coll.h
osu_ibarrier_SOURCES = osu_ibarrier.c osu_coll.c osu_coll.h
osu_reduce_SOURCES = osu_reduce.c osu_coll.c osu_coll.h
osu_allreduce_SOURCES = osu_allreduce.c osu_coll.c osu_coll.h
osu_alltoall_SOURCES = osu_alltoall.c osu_coll.c osu_coll.h
osu_ialltoall_SOURCES = osu_ialltoall.c osu_coll.c osu_coll.h
osu_bcast_SOURCES = osu_bcast.c osu_coll.c osu_coll.h
osu_ibcast_SOURCES = osu_ibcast.c osu_coll.c osu_coll.h
osu_gather_SOURCES = osu_gather.c osu_coll.c osu_coll.h
osu_igather_SOURCES = osu_igather.c osu_coll.c osu_coll.h
osu_allgather_SOURCES = osu_allgather.c osu_coll.c osu_coll.h
osu_iallgather_SOURCES = osu_iallgather.c osu_coll.c osu_coll.h
osu_scatter_SOURCES = osu_scatter.c osu_coll.c osu_coll.h
osu_iscatter_SOURCES = osu_iscatter.c osu_coll.c osu_coll.h

AM_CPPFLAGS = -DFIELD_WIDTH=18 -DFLOAT_PRECISION=2

if BUILD_MRAIL_CUDA
dist_pkglibexec_SCRIPTS = get_local_rank
AM_CPPFLAGS += -D_ENABLE_CUDA_
endif

if BUILD_MRAIL_OPENACC
AM_CPPFLAGS += -D_ENABLE_OPENACC_
AM_CFLAGS = -acc
endif

EXTRA_DIST = README CHANGES

if BUILD_PROFILING_LIB
AM_LDFLAGS = ../lib/lib@PMPILIBNAME@.la ../lib/lib@MPILIBNAME@.la
else 
AM_LDFLAGS = ../lib/lib@MPILIBNAME@.la
endif

