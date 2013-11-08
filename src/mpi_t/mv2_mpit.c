/* Copyright (c) 2001-2013, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpiimpl.h"
#include "mpidi_common_statistics.h"

#if defined (_OSU_MVAPICH_) && OSU_MPIT

#include "mpit.h"

MPIR_T_SIMPLE_HANDLE_CREATOR(simple_ul_creator, unsigned long, 1)
MPIR_T_SIMPLE_HANDLE_CREATOR(simple_ull_creator, unsigned long long, 1)

/*  init all MVAPICH MPIT parameters */
#undef FUNCNAME
#define FUNCNAME MV2_init_mpit_params
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MV2_init_mpit_params(void)
{

    int mpi_errno = MPI_SUCCESS;
    static int initialized = FALSE;

    /* FIXME any MT issues here? */
    if (initialized)
        return MPI_SUCCESS;
    initialized = TRUE;

    int idx = -1;

    /* expose "mpit_mem_allocated_current" as a perf variable */
    mpi_errno = MPIR_T_pvar_add("mem_allocated",
                                MPI_T_VERBOSITY_USER_BASIC,
                                MPI_T_PVAR_CLASS_LEVEL,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Current allocated memory within the MPI library",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_mem_allocated_current,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* expose "mpit_mem_allocated_max" as a perf variable */
    mpi_errno = MPIR_T_pvar_add("mem_allocated",
                                MPI_T_VERBOSITY_USER_BASIC,
                                MPI_T_PVAR_CLASS_HIGHWATERMARK,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Maximum allocated memory within the MPI library",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_mem_allocated_max,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* mpit pvar to track the number of registration cache hits*/
    mpi_errno = MPIR_T_pvar_add("mv2_reg_cache_hits",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of registration cache hits",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &dreg_stat_cache_hit,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* mpit pvar to track the number of registration cache misses*/
    mpi_errno = MPIR_T_pvar_add("mv2_reg_cache_misses",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of registration cache misses",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &dreg_stat_cache_miss,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* mpit pvars to track vbuf usage */
    mpi_errno = MPIR_T_pvar_add("mv2_vbuf_allocated",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of VBUFs allocated",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &vbuf_n_allocated,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno); 

    mpi_errno = MPIR_T_pvar_add("mv2_vbuf_freed",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of VBUFs freed",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &num_vbuf_freed,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_vbuf_available",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of VBUFs available",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &num_free_vbuf,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    #if defined(_ENABLE_UD_) || defined(_MCST_SUPPORT_)
    mpi_errno = MPIR_T_pvar_add("mv2_ud_vbuf_n_allocated",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of UD-VBUFs available",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &ud_vbuf_n_allocated,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_ud_vuf_freed",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of UD-VBUFs available",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &ud_num_vbuf_freed,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_ud_vbuf_available",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of UD-VBUFs available",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &ud_num_free_vbuf,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    #endif

    /* mpit pvar to count progress engine polling */
    mpi_errno = MPIR_T_pvar_add("mv2_progress_poll_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "CH3 RDMA progress engine polling count",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/FALSE,
                                /*continuous=*/FALSE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_progress_poll,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    #ifdef _ENABLE_UD_
    /* mpit pvar to count the number of UD retransmissions */
    /* TODO: update the level and other metadata */
    mpi_errno = MPIR_T_pvar_add("mv2_rdma_ud_retransmit_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "CH3 RDMA UD retransmission count",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/FALSE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &rdma_ud_retransmissions,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    #endif /* _ENABLE_UD_ */

    /*mpit pvars to count different collective algorithms used by MPICH collectives*/
    mpi_errno = MPIR_T_pvar_add("coll_bcast_binomial",
                                MPI_T_VERBOSITY_USER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times binomial bcast algorithm was  invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_bcast_binomial,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("coll_bcast_scatter_doubling_allgather",
                                MPI_T_VERBOSITY_USER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times scatter+doubling allgather bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_scatter_doubling_allgather,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    mpi_errno = MPIR_T_pvar_add("coll_bcast_scatter_ring_allgather",
                                MPI_T_VERBOSITY_USER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times scatter+ring allgather bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_scatter_ring_allgather,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* mpit pvars to count 2-level communicator creation in MVAPICH */
    mpi_errno = MPIR_T_pvar_add("mv2_num_2level_comm_requests",
                                MPI_T_VERBOSITY_USER_DETAIL,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of 2-level comm creation requests",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_num_2level_comm_requests,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_num_2level_comm_success",
                                MPI_T_VERBOSITY_USER_DETAIL,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of successful 2-level comm creations",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_num_2level_comm_success,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);


    /*mpit pvars to count different collective algorithms used by MVAPICH collectives*/
    mpi_errno = MPIR_T_pvar_add("mv2_num_shmem_coll_calls",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 shared-memory collective calls were invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_num_shmem_coll_calls,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_binomial",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 binomial bcast algorithm  was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_bcast_mv2_binomial,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_scatter_doubling_allgather",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 scatter+double allgather  bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_mv2_scatter_doubling_allgather,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_scatter_ring_allgather",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 scatter+ring allgather    bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_mv2_scatter_ring_allgather,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_scatter_ring_allgather_shm",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 scatter+ring allgather    shm bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_mv2_scatter_ring_allgather_shm,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_shmem",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 shmem bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_bcast_mv2_shmem,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_knomial_internode",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 knomial internode bcast   algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_mv2_knomial_internode,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_knomial_intranode",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 knomial intranode bcast   algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/&mpit_bcast_mv2_knomial_intranode,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_mcast_internode",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 mcast internode bcast     algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_bcast_mv2_mcast_internode,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_coll_bcast_pipelined",
                                MPI_T_VERBOSITY_TUNER_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of times MV2 pipelined bcast algorithm was invoked",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mpit_bcast_mv2_pipelined,
                                &simple_ull_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
 
    /* mpit pvars to profile IB channel-manager */
    mpi_errno = MPIR_T_pvar_add("mv2_ibv_channel_ctrl_packet_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of IB control packets",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_ibv_channel_ctrl_packet_count,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    mpi_errno = MPIR_T_pvar_add("mv2_ibv_channel_out_of_order_packet_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of IB out-of-order packets",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_ibv_channel_out_of_order_packet_count,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_ibv_channel_out_of_order_packet_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of IB exact receives",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_ibv_channel_out_of_order_packet_count,
                                &simple_ul_creator,
                                &idx);
 
    /* mpit pvars to count different types of RDMA_FP packets */
    mpi_errno = MPIR_T_pvar_add("mv2_rdmafp_ctrl_packet_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of RDMA FP control packets",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_rdmafp_ctrl_packet_count,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    mpi_errno = MPIR_T_pvar_add("mv2_rdmafp_out_of_order_packet_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of RDMA FP out-of-order packets",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_rdmafp_out_of_order_packet_count,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_rdmafp_exact_recv_count",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of RDMA FP exact receives",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &mv2_rdmafp_exact_recv_count,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    mpi_errno = MPIR_T_pvar_add("mv2_rdmafp_sendconn_accepted",
                                MPI_T_VERBOSITY_MPIDEV_BASIC,
                                MPI_T_PVAR_CLASS_COUNTER,
                                MPI_UNSIGNED_LONG,
                                MPI_T_ENUM_NULL,
                                "Number of RDMA FP connections",
                                MPI_T_BIND_NO_OBJECT,
                                /*readonly=*/TRUE,
                                /*continuous=*/TRUE,
                                /*atomic=*/FALSE,
                                MPIR_T_PVAR_IMPL_SIMPLE,
                                /*var_state=*/ &rdma_fp_sendconn_accepted,
                                &simple_ul_creator,
                                &idx);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#endif /* (_OSU_MVAPICH_) && OSU_MPIT */

