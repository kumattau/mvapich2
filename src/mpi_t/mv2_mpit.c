/* Copyright (c) 2001-2014, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
#include <mpichconf.h>
#include <mpiimpl.h>

/*
 * Profile IB channel-manager
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_ibv_channel_ctrl_packet_count);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_ibv_channel_out_of_order_packet_count);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_ibv_channel_exact_recv_count);

/*
 * Profile RDMA_FP connections and packets
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_rdmafp_ctrl_packet_count);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_rdmafp_out_of_order_packet_count);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_rdmafp_exact_recv_count);

/*
 * Track vbuf usage
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_vbuf_allocated);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_vbuf_freed);
MPIR_T_PVAR_ULONG_LEVEL_DECL(MV2, mv2_vbuf_available);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_ud_vbuf_allocated);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_ud_vbuf_freed);
MPIR_T_PVAR_ULONG_LEVEL_DECL(MV2, mv2_ud_vbuf_available);

/*
 * Track the registration cache hits and misses
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_reg_cache_hits);
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mv2_reg_cache_misses);

/*
 * Count progress engine polling
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, mpit_progress_poll);

/*
 * Count number of shared-memory collective calls
 */
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mv2_num_shmem_coll_calls);

/*
 * Count 2-level communicator creation requests
 */
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mv2_num_2level_comm_requests);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mv2_num_2level_comm_success);

/*
 * Count MVAPICH Broadcast algorithms used
 */
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_binomial);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_scatter_doubling_allgather);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_scatter_ring_allgather);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_scatter_ring_allgather_shm);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_shmem);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_knomial_internode);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_knomial_intranode);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_mcast_internode);
MPIR_T_PVAR_ULONG2_COUNTER_DECL(MV2, mpit_bcast_mv2_pipelined);

/*
 * Count number of UD retransmissions
 */
MPIR_T_PVAR_ULONG_COUNTER_DECL(MV2, rdma_ud_retransmissions);

void
MPIT_REGISTER_MV2_VARIABLES (void)
{
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mv2_num_2level_comm_requests,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of 2-level comm creation requests");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mv2_num_2level_comm_success,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of successful 2-level comm creations");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mv2_num_shmem_coll_calls,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 shared-memory collective calls were invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mpit_progress_poll,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "CH3 RDMA progress engine polling count");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            rdma_ud_retransmissions,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "CH3 RDMA UD retransmission count");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_binomial,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 binomial bcast algorithm  was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_scatter_doubling_allgather,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 scatter+double allgather bcast algorithm "
            "was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_scatter_ring_allgather,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 scatter+ring allgather bcast algorithm "
            "was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_scatter_ring_allgather_shm,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 scatter+ring allgather shm bcast "
            "algorithm was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_shmem,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 shmem bcast algorithm was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_knomial_internode,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 knomial internode bcast algorithm "
            "was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_knomial_intranode,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 knomial intranode bcast algorithm "
            "was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_mcast_internode,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 mcast internode bcast algorithm "
            "was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mpit_bcast_mv2_pipelined,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of times MV2 pipelined bcast algorithm was invoked");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_reg_cache_hits,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of registration cache hits");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_reg_cache_misses,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of registration cache misses");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_vbuf_allocated,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of VBUFs allocated");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_vbuf_freed,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of VBUFs freed");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ud_vbuf_allocated,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of UD VBUFs allocated");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ud_vbuf_freed,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of UD VBUFs freed");
    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_vbuf_available,
            0, /* initial value */
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of VBUFs available");
    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ud_vbuf_available,
            0, /* initial value */
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of UD VBUFs available");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_rdmafp_ctrl_packet_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of RDMA FP CTRL Packet count");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_rdmafp_out_of_order_packet_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of RDMA FP Out of Order Packet count");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_rdmafp_exact_recv_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of RDMA FP Exact Recv count");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ibv_channel_ctrl_packet_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of IB control packets");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ibv_channel_out_of_order_packet_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of IB out-of-order packets");
    MPIR_T_PVAR_COUNTER_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG,
            mv2_ibv_channel_exact_recv_count,
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Number of IB exact receives");
}
