/* Copyright (c) 2001-2022, The Ohio State University. All rights
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
#include "rdma_impl.h"
#include "ibv_param.h"
#include "coll_shmem_internal.h"
#include "gather_tuning.h"
#include "hwloc_bind.h"

/* List of all runtime environment variables.
** Format of the parameter info
**  {
**      id,
**      datatype,
**      name,
**      addr of variables which stores the param value
**      externally visible 1 or 0
**      descrption of the parameter.
**  }
*/
mv2_env_param_list_t  param_list[] = {
/* mpirun_rsh */
{
    MV2_COMM_WORLD_LOCAL_RANK,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MV2_COMM_WORLD_LOCAL_RANK",
    NULL,
    0,
    NULL    },
{
    PMI_ID,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "PMI_ID",
    NULL,
    0,
    NULL    },
{
    MPIRUN_COMM_MULTIPLE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPIRUN_COMM_MULTIPLE",
    NULL,
    0,
    NULL    },
{
    MPIRUN_RSH_LAUNCH,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_launcher,
    "MPIRUN_RSH_LAUNCH",
    &using_mpirun_rsh,
    0,
    NULL    },
{
    MPISPAWN_BINARY_PATH,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_BINARY_PATH",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_CR_CKPT_CNT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_CR_CKPT_CNT",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_CR_CONTEXT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_CR_CONTEXT",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_CR_SESSIONID,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_CR_SESSIONID",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_GLOBAL_NPROCS,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_GLOBAL_NPROCS",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_MPIRUN_CR_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_MPIRUN_CR_PORT",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_MPIRUN_HOST,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_MPIRUN_HOST",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_MPIRUN_ID,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_MPIRUN_ID",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_NNODES,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_NNODES",
    NULL,
    0,
    NULL    },
{
    MPISPAWN_WORKING_DIR,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_WORKING_DIR",
    NULL,
    0,
    NULL    },
{
    MPIEXEC_TIMEOUT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPIEXEC_TIMEOUT",
    NULL,
    1,
    NULL    },
{
    MPISPAWN_USE_TOTALVIEW,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MPISPAWN_USE_TOTALVIEW",
    NULL,
    0,
    NULL    },
{
    MV2_FASTSSH_THRESHOLD,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MV2_FASTSSH_THRESHOLD",
    NULL,
    1,
    NULL    },
{
    MV2_MPIRUN_TIMEOUT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MV2_MPIRUN_TIMEOUT",
    NULL,
    1,
    NULL    },
{
    MV2_MT_DEGREE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MV2_MT_DEGREE",
    NULL,
    1,
    NULL    },
{
    MV2_NPROCS_THRESHOLD,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "MV2_NPROCS_THRESHOLD",
    NULL,
    1,
    NULL    },
{
    USE_LINEAR_SSH,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "USE_LINEAR_SSH",
    NULL,
    0,
    NULL    },
{
    PMI_SUBVERSION,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "PMI_SUBVERSION",
    NULL,
    0,
    NULL    },
{
    PMI_VERSION,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "PMI_VERSION",
    NULL,
    0,
    NULL    },
{
    PMI_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "PMI_PORT",
    NULL,
    0,
    NULL    },
{
    PARENT_ROOT_PORT_NAME,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_launcher,
    "PARENT_ROOT_PORT_NAME",
    NULL,
    0,
    NULL    },
{
    MV2_SHMEM_BACKED_UD_CM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_launcher,
    "MV2_SHMEM_BACKED_UD_CM",
    &mv2_shmem_backed_ud_cm,
    0,
    NULL    },
/* QoS */
{
    MV2_3DTORUS_SUPPORT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_3DTORUS_SUPPORT",
    &rdma_3dtorus_support,
    0,
    NULL    },
{
    MV2_NUM_SA_QUERY_RETRIES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_NUM_SA_QUERY_RETRIES",
    &rdma_num_sa_query_retries,
    1,
    NULL    },
{
    MV2_NUM_SLS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_NUM_SLS",
    &rdma_qos_num_sls,
    0,
    NULL    },
{
    MV2_DEFAULT_SERVICE_LEVEL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_DEFAULT_SERVICE_LEVEL",
    &rdma_default_service_level,
    0,
    NULL    },
{
    MV2_PATH_SL_QUERY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_PATH_SL_QUERY",
    &rdma_path_sl_query,
    0,
    NULL    },
{
    MV2_USE_QOS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_QoS,
    "MV2_USE_QOS",
    &rdma_use_qos,
    0,
    NULL    },
/* collectives */
#if defined(_MCST_SUPPORT_)
{
    MV2_USE_MCAST,
    MV2_PARAM_TYPE_INT8, 
    MV2_PARAM_GROUP_collective,
    "MV2_USE_MCAST",
    &rdma_enable_mcast,
    1,
    NULL    },
#if defined(RDMA_CM)
{
    MV2_USE_RDMA_CM_MCAST,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_RDMA_CM_MCAST",
    &rdma_use_rdma_cm_mcast,
    1,
    NULL    },
#endif /*defined(RDMA_CM)*/
{
    MV2_MCAST_BCAST_MIN_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_MCAST_BCAST_MIN_MSG",
    &mcast_bcast_min_msg,
    1,
    NULL    },
{
    MV2_MCAST_BCAST_MAX_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_MCAST_BCAST_MAX_MSG",
    &mcast_bcast_max_msg,
    1,
    NULL    },
#endif /*_MCST_SUPPORT_*/
{
    MV2_ALLGATHER_BRUCK_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLGATHER_BRUCK_THRESHOLD",
    &mv2_coll_param.allgather_bruck_threshold,
    0,
    NULL    },
{
    MV2_ALLGATHER_RD_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLGATHER_RD_THRESHOLD",
    &mv2_coll_param.allgather_rd_threshold,
    0,
    NULL    },
{
    MV2_ALLGATHER_REVERSE_RANKING,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLGATHER_REVERSE_RANKING",
    &mv2_allgather_ranking,
    0,
    NULL    },
{
    MV2_ALLGATHERV_RD_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLGATHERV_RD_THRESHOLD",
    &mv2_user_allgatherv_switch_point,
    0,
    NULL    },
{
    MV2_ALLREDUCE_2LEVEL_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLREDUCE_2LEVEL_MSG",
    &mv2_coll_param.allreduce_2level_threshold,
    1,
    NULL    },
{
    MV2_ALLREDUCE_SHORT_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLREDUCE_SHORT_MSG",
    &mv2_coll_param.allreduce_short_msg,
    0,
    NULL    },
{
    MV2_ALLTOALL_MEDIUM_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLTOALL_MEDIUM_MSG",
    &mv2_coll_param.alltoall_medium_msg,
    0,
    NULL    },
{
    MV2_ALLTOALL_SMALL_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLTOALL_SMALL_MSG",
    &mv2_coll_param.alltoall_small_msg,
    0,
    NULL    },
{
    MV2_ALLTOALL_THROTTLE_FACTOR,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ALLTOALL_THROTTLE_FACTOR",
    &mv2_coll_param.alltoall_throttle_factor,
    0,
    NULL    },
{
    MV2_BCAST_TWO_LEVEL_SYSTEM_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_BCAST_TWO_LEVEL_SYSTEM_SIZE",
    &mv2_bcast_two_level_system_size,
    0,
    NULL    },
{
    MV2_GATHER_SWITCH_PT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_GATHER_SWITCH_PT",
    &mv2_user_gather_switch_point,
    1,
    NULL    },
{
    MV2_INTRA_SHMEM_REDUCE_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_INTRA_SHMEM_REDUCE_MSG",
    &mv2_coll_param.shmem_intra_reduce_msg,
    0,
    NULL    },
{
    MV2_KNOMIAL_2LEVEL_BCAST_MESSAGE_SIZE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_2LEVEL_BCAST_MESSAGE_SIZE_THRESHOLD",
    &mv2_knomial_2level_bcast_message_size_threshold,
    1,
    NULL    },
{
    MV2_KNOMIAL_2LEVEL_BCAST_SYSTEM_SIZE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_2LEVEL_BCAST_SYSTEM_SIZE_THRESHOLD",
    &mv2_knomial_2level_bcast_system_size_threshold,
    0,
    NULL    },
{
    MV2_KNOMIAL_INTER_LEADER_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_INTER_LEADER_THRESHOLD",
    &mv2_knomial_inter_leader_threshold,
    0,
    NULL    },
{
    MV2_KNOMIAL_INTER_NODE_FACTOR,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_INTER_NODE_FACTOR",
    &mv2_inter_node_knomial_factor,
    1,
    NULL    },
{
    MV2_KNOMIAL_INTRA_NODE_FACTOR,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_INTRA_NODE_FACTOR",
    &mv2_intra_node_knomial_factor,
    1,
    NULL    },
{
    MV2_KNOMIAL_INTRA_NODE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_KNOMIAL_INTRA_NODE_THRESHOLD",
    &mv2_knomial_intra_node_threshold,
    0,
    NULL    },
{
    MV2_RED_SCAT_LARGE_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_RED_SCAT_LARGE_MSG",
    &mv2_red_scat_long_msg,
    1,
    NULL    },
{
    MV2_RED_SCAT_SHORT_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_RED_SCAT_SHORT_MSG",
    &mv2_red_scat_short_msg,
    1,
    NULL    },
{
    MV2_REDUCE_2LEVEL_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_REDUCE_2LEVEL_MSG",
    &mv2_coll_param.reduce_2level_threshold,
    1,
    NULL    },
{
    MV2_REDUCE_SHORT_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_REDUCE_SHORT_MSG",
    &mv2_coll_param.reduce_short_msg,
    0,
    NULL    },
{
    MV2_SCATTER_MEDIUM_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SCATTER_MEDIUM_MSG",
    &mv2_user_scatter_medium_msg,
    1,
    NULL    },
{
    MV2_SCATTER_SMALL_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SCATTER_SMALL_MSG",
    &mv2_user_scatter_small_msg,
    0,
    NULL    },
{
    MV2_SHMEM_ALLREDUCE_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_ALLREDUCE_MSG",
    &mv2_coll_param.shmem_allreduce_msg,
    1,
    NULL    },
{
    MV2_SHMEM_COLL_MAX_MSG_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_COLL_MAX_MSG_SIZE",
    &mv2_g_shmem_coll_max_msg_size,
    1,
    NULL    },
{
    MV2_SHMEM_COLL_NUM_COMM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_COLL_NUM_COMM",
    &mv2_g_shmem_coll_blocks,
    1,
    NULL    },
{
    MV2_SHMEM_COLL_NUM_PROCS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_COLL_NUM_PROCS",
    &mv2_shmem_coll_num_procs,
    0,
    NULL    },
{
    MV2_SHMEM_COLL_SPIN_COUNT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_COLL_SPIN_COUNT",
    &mv2_shmem_coll_spin_count,
    0,
    NULL    },
{
    MV2_SHMEM_DIR,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_DIR",
    NULL,
    1,
    NULL    },
{
    MV2_SHMEM_REDUCE_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SHMEM_REDUCE_MSG",
    &mv2_coll_param.shmem_reduce_msg,
    1,
    NULL    },
{
    MV2_USE_BCAST_SHORT_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_BCAST_SHORT_MSG",
    &mv2_bcast_short_msg,
    0,
    NULL    },
{
    MV2_USE_DIRECT_GATHER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_DIRECT_GATHER",
    &mv2_use_direct_gather,
    1,
    NULL    },
{
    MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_MEDIUM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_MEDIUM",
    &mv2_gather_direct_system_size_medium,
    0,
    NULL    },
{
    MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_SMALL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_SMALL",
    &mv2_gather_direct_system_size_small,
    0,
    NULL    },
{
    MV2_USE_DIRECT_SCATTER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_DIRECT_SCATTER",
    &mv2_use_direct_scatter,
    1,
    NULL    },
{
    MV2_USE_OSU_COLLECTIVES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_OSU_COLLECTIVES",
    &mv2_use_osu_collectives,
    1,
    NULL    },
{
    MV2_USE_OSU_NB_COLLECTIVES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_OSU_NB_COLLECTIVES",
    &mv2_use_osu_nb_collectives,
    1,
    NULL    },
{
    MV2_USE_KNOMIAL_2LEVEL_BCAST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_KNOMIAL_2LEVEL_BCAST",
    &mv2_enable_knomial_2level_bcast,
    1,
    NULL    },
{
    MV2_USE_KNOMIAL_INTER_LEADER_BCAST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_KNOMIAL_INTER_LEADER_BCAST",
    &mv2_knomial_inter_leader_bcast,
    0,
    NULL    },
{
    MV2_USE_SCATTER_RD_INTER_LEADER_BCAST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SCATTER_RD_INTER_LEADER_BCAST",
    &mv2_scatter_rd_inter_leader_bcast,
    0,
    NULL    },
{
    MV2_USE_SCATTER_RING_INTER_LEADER_BCAST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SCATTER_RING_INTER_LEADER_BCAST",
    &mv2_scatter_ring_inter_leader_bcast,
    0,
    NULL    },
{
    MV2_USE_SHMEM_ALLREDUCE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SHMEM_ALLREDUCE",
    &mv2_enable_shmem_allreduce,
    1,
    NULL    },
{
    MV2_USE_SHMEM_BARRIER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SHMEM_BARRIER",
    &mv2_enable_shmem_barrier,
    1,
    NULL    },
{
    MV2_USE_SHMEM_BCAST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SHMEM_BCAST",
    &mv2_enable_shmem_bcast,
    1,
    NULL    },
{
    MV2_USE_SHMEM_COLL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SHMEM_COLL",
    &mv2_enable_shmem_collectives,
    1,
    NULL    },
{
    MV2_USE_SHMEM_REDUCE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SHMEM_REDUCE",
    &mv2_enable_shmem_reduce,
    1,
    NULL    },
{
    MV2_USE_TWO_LEVEL_GATHER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_TWO_LEVEL_GATHER",
    &mv2_use_two_level_gather,
    1,
    NULL    },
{
    MV2_USE_TWO_LEVEL_SCATTER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_TWO_LEVEL_SCATTER",
    &mv2_use_two_level_scatter,
    1,
    NULL    },
{
    MV2_USE_XOR_ALLTOALL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_XOR_ALLTOALL",
    &mv2_use_xor_alltoall,
    0,
    NULL    },
{
    MV2_ENABLE_SOCKET_AWARE_COLLECTIVES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_ENABLE_SOCKET_AWARE_COLLECTIVES",
    &mv2_enable_socket_aware_collectives,
    0,
    NULL    },
{
    MV2_USE_SOCKET_AWARE_ALLREDUCE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SOCKET_AWARE_ALLREDUCE",
    &mv2_use_socket_aware_allreduce,
    0,
    NULL    },
{
    MV2_USE_SOCKET_AWARE_BARRIER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SOCKET_AWARE_BARRIER",
    &mv2_use_socket_aware_barrier,
    0,
    NULL    },
{
    MV2_USE_SOCKET_AWARE_SHARP_ALLREDUCE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_USE_SOCKET_AWARE_SHARP_ALLREDUCE",
    &mv2_use_socket_aware_sharp_allreduce,
    0,
    NULL    },
{
    MV2_SOCKET_AWARE_ALLREDUCE_MAX_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SOCKET_AWARE_ALLREDUCE_MAX_MSG",
    &mv2_socket_aware_allreduce_max_msg,
    0,
    NULL    },
{
    MV2_SOCKET_AWARE_ALLREDUCE_MIN_MSG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_collective,
    "MV2_SOCKET_AWARE_ALLREDUCE_MIN_MSG",
    &mv2_socket_aware_allreduce_min_msg,
    0,
    NULL    },
/* ckpt */
{
    MV2_CKPT_AGGRE_MIG_ROLE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_AGGRE_MIG_ROLE",
    NULL,
    0,
    NULL    },
{
    MV2_CKPT_AGGREGATION_BUFPOOL_SIZE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_AGGREGATION_BUFPOOL_SIZE",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_AGGREGATION_CHUNK_SIZE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_AGGREGATION_CHUNK_SIZE",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_AGGRE_MIG_FILE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_AGGRE_MIG_FILE",
    NULL,
    0,
    NULL    },
{
    MV2_CKPT_FILE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_FILE",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_INTERVAL,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_INTERVAL",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_MAX_CKPTS,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_MAX_CKPTS",
    NULL,
    0,
    NULL    },
{
    MV2_CKPT_MAX_SAVE_CKPTS,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_MAX_SAVE_CKPTS",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_MPD_BASE_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_MPD_BASE_PORT",
    NULL,
    0,
    NULL    },
{
    MV2_CKPT_NO_SYNC,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_NO_SYNC",
    NULL,
    1,
    NULL    },
{
    MV2_CKPT_SESSIONID,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_SESSIONID",
    NULL,
    0,
    NULL    },
{
    MV2_CKPT_USE_AGGREGATION,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_ckpt,
    "MV2_CKPT_USE_AGGREGATION",
    NULL,
    1,
    NULL    },
/*start up */
{
    MV2_CM_MAX_SPIN_COUNT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_CM_MAX_SPIN_COUNT",
    NULL,
    1,
    NULL    },
{
    MV2_CM_RECV_BUFFERS,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_CM_RECV_BUFFERS",
    NULL,
    1,
    NULL    },
{
    MV2_CM_SEND_DEPTH,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_CM_SEND_DEPTH",
    NULL,
    0,
    NULL    },
{
    MV2_CM_TIMEOUT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_CM_TIMEOUT",
    NULL,
    1,
    NULL    },
{
    MV2_CM_UD_PSN,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_CM_UD_PSN",
    NULL,
    0,
    NULL    },
{
    MV2_DEFAULT_SRC_PATH_BITS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_SRC_PATH_BITS",
    &rdma_default_src_path_bits,
    0,
    NULL    },
{
    MV2_DEFAULT_STATIC_RATE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_STATIC_RATE",
    &rdma_default_static_rate,
    0,
    NULL    },
{
    MV2_DEFAULT_TIME_OUT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_TIME_OUT",
    &rdma_default_time_out,
    0,
    NULL    },
{
    MV2_DEFAULT_MTU,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MTU",
    &rdma_default_mtu,
    1,
    NULL    },
{
    MV2_DEFAULT_PKEY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_PKEY",
    &rdma_default_pkey,
    1,
    NULL    },
{
    MV2_DEFAULT_QKEY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_QKEY",
    &rdma_default_qkey,
    1,
    NULL    },
{
    MV2_DEFAULT_PORT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_PORT",
    &rdma_default_port,
    0,
    NULL    },
{
    MV2_DEFAULT_GID_INDEX,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_GID_INDEX",
    &rdma_default_gid_index,
    0,
    NULL    },
{
    MV2_DEFAULT_PSN,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_PSN",
    &rdma_default_psn,
    0,
    NULL    },
{
    MV2_DEFAULT_MAX_RECV_WQE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MAX_RECV_WQE",
    &rdma_default_max_recv_wqe,
    1,
    NULL    },
{
    MV2_DEFAULT_MAX_SEND_WQE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MAX_SEND_WQE",
    &rdma_default_max_send_wqe,
    1,
    NULL    },
{
    MV2_DEFAULT_MAX_SG_LIST,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MAX_SG_LIST",
    &rdma_default_max_sg_list,
    0,
    NULL    },
{
    MV2_DEFAULT_MIN_RNR_TIMER,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MIN_RNR_TIMER",
    &rdma_default_min_rnr_timer,
    0,
    NULL    },
{
    MV2_DEFAULT_QP_OUS_RD_ATOM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_QP_OUS_RD_ATOM",
    &rdma_default_qp_ous_rd_atom,
    0,
    NULL    },
{
    MV2_DEFAULT_RETRY_COUNT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_RETRY_COUNT",
    &rdma_default_retry_count,
    0,
    NULL    },
{
    MV2_DEFAULT_RNR_RETRY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_RNR_RETRY",
    &rdma_default_rnr_retry,
    0,
    NULL    },
{
    MV2_DEFAULT_MAX_CQ_SIZE,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MAX_CQ_SIZE",
    &rdma_default_max_cq_size,
    0,
    NULL    },
{
    MV2_DEFAULT_MAX_RDMA_DST_OPS,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_startup,
    "MV2_DEFAULT_MAX_RDMA_DST_OPS",
    &rdma_default_max_rdma_dst_ops,
    0,
    NULL    },
{
    MV2_IGNORE_SYSTEM_CONFIG,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_IGNORE_SYSTEM_CONFIG",
    NULL,
    1,
    NULL    },
{
    MV2_IGNORE_USER_CONFIG,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_IGNORE_USER_CONFIG",
    NULL,
    1,
    NULL    },
{
    MV2_INITIAL_PREPOST_DEPTH,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_INITIAL_PREPOST_DEPTH",
    &rdma_initial_prepost_depth,
    1,
    NULL    },
{
    MV2_IBA_HCA,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_IBA_HCA",
    NULL,
    1,
    NULL    },
{
    MV2_IWARP_MULTIPLE_CQ_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_IWARP_MULTIPLE_CQ_THRESHOLD",
    &rdma_iwarp_multiple_cq_threshold,
    1,
    NULL    },
{
    MV2_NUM_HCAS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_NUM_HCAS",
    &rdma_num_hcas,
    1,
    NULL    },
{
    MV2_NUM_NODES_IN_JOB,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_NUM_NODES_IN_JOB",
    NULL,
    0,
    NULL    },
{
    MV2_NUM_PORTS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_NUM_PORTS",
    &rdma_num_ports,
    1,
    NULL    },
{
    MV2_NUM_QP_PER_PORT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_NUM_QP_PER_PORT",
    &rdma_num_qp_per_port,
    1,
    NULL    },
{
    MV2_MAX_RDMA_CONNECT_ATTEMPTS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_MAX_RDMA_CONNECT_ATTEMPTS",
    &max_rdma_connect_attempts,
    0,
    NULL    },
{
    MV2_ON_DEMAND_THRESHOLD,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_ON_DEMAND_THRESHOLD",
    NULL,
    1,
    NULL    },
{
    MV2_ON_DEMAND_UD_INFO_EXCHANGE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_ON_DEMAND_UD_INFO_EXCHANGE",
    &mv2_on_demand_ud_info_exchange,
    0,
    NULL    },
{
    MV2_PREPOST_DEPTH,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_PREPOST_DEPTH",
    &rdma_prepost_depth,
    1,
    NULL    },
{
    MV2_USER_CONFIG,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_USER_CONFIG",
    NULL,
    1,
    NULL    },
{
    MV2_USE_RING_STARTUP,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_startup,
    "MV2_USE_RING_STARTUP",
    NULL,
    1,
    NULL    },
{
    MV2_HOMOGENEOUS_CLUSTER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_HOMOGENEOUS_CLUSTER",
    &mv2_homogeneous_cluster,
    0,
    NULL    },
{
    MV2_UNIVERSE_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_startup,
    "MV2_UNIVERSE_SIZE",
    NULL,
    0,
    NULL    },
/* pt-pt */
/* pt-pt */
{
    MV2_NUM_CQES_PER_POLL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_NUM_CQES_PER_POLL",
    &rdma_num_cqes_per_poll,
    0,
    NULL    },
{
    MV2_COALESCE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_COALESCE_THRESHOLD",
    &rdma_coalesce_threshold,
    0,
    NULL    },
{
    MV2_DREG_CACHE_LIMIT,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_DREG_CACHE_LIMIT",
    &rdma_dreg_cache_limit,
    0,
    NULL    },
{
    MV2_IBA_EAGER_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_IBA_EAGER_THRESHOLD",
    &rdma_iba_eager_threshold,
    1,
    NULL    },
{
    MV2_MAX_INLINE_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_MAX_INLINE_SIZE",
    &rdma_max_inline_size,
    1,
    NULL    },
{
    MV2_MAX_R3_PENDING_DATA,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_MAX_R3_PENDING_DATA",
    &rdma_max_r3_pending_data,
    0,
    NULL    },
{
    MV2_MED_MSG_RAIL_SHARING_POLICY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_MED_MSG_RAIL_SHARING_POLICY",
    &rdma_med_msg_rail_sharing_policy,
    0,
    NULL    },
{
    MV2_NDREG_ENTRIES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_NDREG_ENTRIES",
    &rdma_ndreg_entries,
    1,
    NULL    },
{
    MV2_NUM_RDMA_BUFFER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_NUM_RDMA_BUFFER",
    &num_rdma_buffer,
    1,
    NULL    },
{
    MV2_NUM_SPINS_BEFORE_LOCK,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_NUM_SPINS_BEFORE_LOCK",
    &mv2_spins_before_lock,
    0,
    NULL    },
{
    MV2_POLLING_LEVEL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_POLLING_LEVEL",
    &rdma_polling_level,
    0,
    NULL    },
{
    MV2_POLLING_SET_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_POLLING_SET_LIMIT",
    &rdma_polling_set_limit,
    0,
    NULL    },
{
    MV2_POLLING_SET_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_POLLING_SET_THRESHOLD",
    &rdma_polling_set_threshold,
    0,
    NULL    },
{
    MV2_PROCESS_TO_RAIL_MAPPING,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_PROCESS_TO_RAIL_MAPPING",
    NULL,
    1,
    NULL    },
{
    MV2_R3_NOCACHE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_R3_NOCACHE_THRESHOLD",
    &rdma_r3_threshold_nocache,
    1,
    NULL    },
{
    MV2_R3_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_R3_THRESHOLD",
    &rdma_r3_threshold,
    1,
    NULL    },
{
    MV2_RAIL_SHARING_LARGE_MSG_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RAIL_SHARING_LARGE_MSG_THRESHOLD",
    &rdma_large_msg_rail_sharing_threshold,
    0,
    NULL    },
{
    MV2_RAIL_SHARING_MED_MSG_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RAIL_SHARING_MED_MSG_THRESHOLD",
    &rdma_med_msg_rail_sharing_threshold,
    0,
    NULL    },
{
    MV2_RAIL_SHARING_POLICY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RAIL_SHARING_POLICY",
    &rdma_rail_sharing_policy,
    1,
    NULL    },
{
    MV2_RDMA_EAGER_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RDMA_EAGER_LIMIT",
    &rdma_eager_limit,
    0,
    NULL    },
{
    MV2_RDMA_FAST_PATH_BUF_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RDMA_FAST_PATH_BUF_SIZE",
    &rdma_fp_buffer_size,
    1,
    NULL    },
{
    MV2_RDMA_NUM_EXTRA_POLLS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RDMA_NUM_EXTRA_POLLS",
    &rdma_num_extra_polls,
    0,
    NULL    },
{
    MV2_RNDV_EXT_SENDQ_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RNDV_EXT_SENDQ_SIZE",
    &rdma_rndv_ext_sendq_size,
    0,
    NULL    },
{
    MV2_RNDV_PROTOCOL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_RNDV_PROTOCOL",
    &rdma_rndv_protocol,
    1,
    NULL    },
{
    MV2_SMP_RNDV_PROTOCOL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SMP_RNDV_PROTOCOL",
    &smp_rndv_protocol,
    1,
    NULL    },
{
    MV2_SMALL_MSG_RAIL_SHARING_POLICY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SMALL_MSG_RAIL_SHARING_POLICY",
    &rdma_small_msg_rail_sharing_policy,
    0,
    NULL    },
{
    MV2_SM_SCHEDULING,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SM_SCHEDULING",
    &rdma_rail_sharing_policy,
    1,
    NULL    },
{
    MV2_SPIN_COUNT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SPIN_COUNT",
    &rdma_blocking_spin_count_threshold,
    0,
    NULL    },
{
    MV2_SRQ_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SRQ_LIMIT",
    &mv2_srq_limit,
    1,
    NULL    },
{
    MV2_SRQ_MAX_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SRQ_MAX_SIZE",
    &mv2_srq_alloc_size,
    1,
    NULL    },
{
    MV2_SRQ_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_SRQ_SIZE",
    &mv2_srq_fill_size,
    1,
    NULL    },
{
    MV2_STRIPING_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_STRIPING_THRESHOLD",
    &striping_threshold,
    1,
    NULL    },
{
    MV2_USE_BLOCKING,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_BLOCKING",
    NULL,
    1,
    NULL    },
{
    MV2_USE_COALESCE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_COALESCE",
    &rdma_use_coalesce,
    1,
    NULL    },
{
    MV2_USE_LAZY_MEM_UNREGISTER,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_LAZY_MEM_UNREGISTER",
    NULL,
    1,
    NULL    },
{
    MV2_USE_RDMA_FAST_PATH,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_RDMA_FAST_PATH",
    NULL,
    1,
    NULL    },
{
    MV2_USE_SRQ,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_SRQ",
    NULL,
    1,
    NULL    },
{
    MV2_USE_XRC,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_XRC",
    &rdma_use_xrc,
    1,
    NULL    },
{
    MV2_VBUF_MAX,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_VBUF_MAX",
    &rdma_vbuf_max,
    1,
    NULL    },
{
    MV2_VBUF_POOL_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_VBUF_POOL_SIZE",
    &rdma_vbuf_pool_size,
    1,
    NULL    },
{
    MV2_VBUF_SECONDARY_POOL_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_VBUF_SECONDARY_POOL_SIZE",
    &rdma_vbuf_secondary_pool_size,
    1,
    NULL    },
{
    MV2_VBUF_TOTAL_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_VBUF_TOTAL_SIZE",
    &rdma_vbuf_total_size,
    1,
    NULL    },
#if defined(RDMA_CM)
{
    MV2_USE_IWARP_MODE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_IWARP_MODE",
    &mv2_MPIDI_CH3I_RDMA_Process.use_iwarp_mode,
    1,
    NULL    },
#endif /*RDMA_CM*/
{
    MV2_USE_RoCE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_pt2pt,
    "MV2_USE_RoCE",
    NULL,
    1,
    NULL    },
/* smp */
{
    MV2_CPU_BINDING_POLICY,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_intranode,
    "MV2_CPU_BINDING_POLICY",
    NULL,
    1,
    NULL    },
{
    MV2_CPU_BINDING_LEVEL,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_intranode,
    "MV2_CPU_BINDING_LEVEL",
    NULL,
    1,
    NULL    },
{
    MV2_USE_HWLOC_CPU_BINDING,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_USE_HWLOC_CPU_BINDING",
    &use_hwloc_cpu_binding,
    0,
    NULL    },
{
    MV2_CPU_MAPPING,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_intranode,
    "MV2_CPU_MAPPING",
    NULL,
    1,
    NULL    },
{
    MV2_ENABLE_AFFINITY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_ENABLE_AFFINITY",
    &mv2_enable_affinity,
    1,
    NULL    },
{
    MV2_ENABLE_LEASTLOAD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_ENABLE_LEASTLOAD",
    &mv2_enable_leastload,
    0,
    NULL    },
#if defined(_SMP_LIMIC_)
{
    MV2_LIMIC_GET_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_LIMIC_GET_THRESHOLD",
    &limic_get_threshold,
    1,
    NULL    },
{
    MV2_LIMIC_PUT_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_LIMIC_PUT_THRESHOLD",
    &limic_put_threshold,
    1,
    NULL    },
{
    MV2_SMP_USE_LIMIC2,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_USE_LIMIC2",
    &g_smp_use_limic2,
    1,
    NULL    },
{
    MV2_USE_LIMIC2_COLL,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_USE_LIMIC2_COLL",
    &g_use_limic2_coll,
    1,
    NULL    },
#endif
{
    MV2_SMP_BATCH_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_BATCH_SIZE",
    &s_smp_batch_size,
    0,
    NULL    },
{
    MV2_SMP_EAGERSIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_EAGERSIZE",
    &g_smp_eagersize,
    1,
    NULL    },
{
    MV2_SMP_QUEUE_LENGTH,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_QUEUE_LENGTH",
    &s_smp_queue_length,
    1,
    NULL    },
{
    MV2_SMP_NUM_SEND_BUFFER,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_NUM_SEND_BUFFER",
    &s_smp_num_send_buffer,
    1,
    NULL    },
{
    MV2_SMP_SEND_BUF_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_SEND_BUF_SIZE",
    &s_smp_block_size,
    1,
    NULL    },
{
    MV2_USE_SHARED_MEM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_USE_SHARED_MEM",
    &mv2_use_smp,
    1,
    NULL    },
{
    MV2_USE_PT2PT_SHMEM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_USE_PT2PT_SHMEM",
    &rdma_use_smp,
    1,
    NULL    },
{
    MV2_SMP_CMA_MAX_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_CMA_MAX_SIZE",
    &s_smp_cma_max_size,
    1,
    NULL    },
{
    MV2_SMP_LIMIC2_MAX_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_intranode,
    "MV2_SMP_LIMIC2_MAX_SIZE",
    &s_smp_limic2_max_size,
    1,
    NULL    },
/* debug */
{
    MV2_DEBUG_CORESIZE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_CORESIZE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_SHOW_BACKTRACE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_SHOW_BACKTRACE",
    NULL,
    0,
    NULL    },
{
    MV2_ABORT_SLEEP_SECONDS,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_ABORT_SLEEP_SECONDS",
    NULL,
    0,
    NULL    },
{
    MV2_SHOW_ENV_INFO,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_debugger,
    "MV2_SHOW_ENV_INFO",
    &mv2_show_env_info,
    0,
    NULL    },
{
    MV2_SYSREPORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_SYSREPORT",
    NULL,
    0,
    NULL    },
{
    TOTALVIEW,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "TOTALVIEW",
    NULL,
    0,
    NULL    },
{
    MV2_DEBUG_CM_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_CM_VERBOSE",
    NULL,
    0,
    NULL    },
{
    MV2_DEBUG_CUDA_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_CUDA_VERBOSE",
    NULL,
    0,
    NULL    },
{
    MV2_DEBUG_FORK_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_FORK_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_FT_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_FT_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_MEM_USAGE_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_MEM_USAGE_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_MIG_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_MIG_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_UDSTAT_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_UDSTAT_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_UD_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_UD_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_XRC_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_XRC_VERBOSE",
    NULL,
    1,
    NULL    },
{
    MV2_DEBUG_ZCOPY_VERBOSE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_debugger,
    "MV2_DEBUG_ZCOPY_VERBOSE",
    NULL,
    1,
    NULL    },
/* one-sided */
{
    MV2_DEFAULT_PUT_GET_LIST_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_rma,
    "MV2_DEFAULT_PUT_GET_LIST_SIZE",
    &rdma_default_put_get_list_size,
    0,
    NULL    },
{
    MV2_EAGERSIZE_1SC,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_rma,
    "MV2_EAGERSIZE_1SC",
    &rdma_eagersize_1sc,
    0,
    NULL    },
{
    MV2_GET_FALLBACK_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_rma,
    "MV2_GET_FALLBACK_THRESHOLD",
    &rdma_get_fallback_threshold,
    1,
    NULL    },
{
    MV2_PIN_POOL_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_rma,
    "MV2_PIN_POOL_SIZE",
    &rdma_pin_pool_size,
    0,
    NULL    },
{
    MV2_PUT_FALLBACK_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_rma,
    "MV2_PUT_FALLBACK_THRESHOLD",
    &rdma_put_fallback_threshold,
    1,
    NULL    },
{
    MV2_USE_RDMA_ONE_SIDED,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rma,
    "MV2_USE_RDMA_ONE_SIDED",
    NULL,
    1,
    NULL    },
/* rdma cm */
#if defined(RDMA_CM)
{
    MV2_RDMA_CM_ARP_TIMEOUT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_RDMA_CM_ARP_TIMEOUT",
    NULL,
    1,
    NULL    },
{
    MV2_RDMA_CM_CONNECT_RETRY_INTERVAL,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_RDMA_CM_CONNECT_RETRY_INTERVAL",
    NULL,
    0,
    NULL    },
{
    MV2_RDMA_CM_MAX_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_RDMA_CM_MAX_PORT",
    NULL,
    1,
    NULL    },
{
    MV2_RDMA_CM_MIN_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_RDMA_CM_MIN_PORT",
    NULL,
    1,
    NULL    },
{
    MV2_RDMA_CM_PORT,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_RDMA_CM_PORT",
    NULL,
    0,
    NULL    },
{
    MV2_USE_RDMA_CM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_rdma_cm,
    "MV2_USE_RDMA_CM",
    &mv2_MPIDI_CH3I_RDMA_Process.use_rdma_cm,
    1,
    NULL    },
#endif /*RDMA_CM*/
/* hybrid */
#if defined (_ENABLE_UD_)
#if defined (_MV2_UD_DROP_PACKET_RATE_)
{
    MV2_UD_DROP_PACKET_RATE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_DROP_PACKET_RATE",
    &ud_drop_packet_rate,
    0,
    NULL    },
#endif
{
    MV2_UD_MAX_ACK_PENDING,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_MAX_ACK_PENDING",
    &rdma_ud_max_ack_pending,
    0,
    NULL    },
{
    MV2_UD_MAX_RECV_WQE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_MAX_RECV_WQE",
    &rdma_default_max_ud_recv_wqe,
    0,
    NULL    },
{
    MV2_UD_MAX_RETRY_TIMEOUT,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_MAX_RETRY_TIMEOUT",
    &rdma_ud_max_retry_timeout,
    0,
    NULL    },
{
    MV2_UD_MAX_SEND_WQE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_MAX_SEND_WQE",
    &rdma_default_max_ud_send_wqe,
    0,
    NULL    },
{
    MV2_UD_MTU,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_MTU",
    &rdma_default_ud_mtu,
    0,
    NULL    },
{
    MV2_UD_NUM_MSG_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_NUM_MSG_LIMIT",
    &rdma_ud_num_msg_limit,
    0,
    NULL    },
{
    MV2_UD_NUM_ZCOPY_RNDV_QPS,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_NUM_ZCOPY_RNDV_QPS",
    &rdma_ud_num_rndv_qps,
    0,
    NULL    },
{
    MV2_UD_PROGRESS_SPIN,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_PROGRESS_SPIN",
    &rdma_ud_progress_spin,
    0,
    NULL    },
{
    MV2_UD_PROGRESS_TIMEOUT,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_PROGRESS_TIMEOUT",
    &rdma_ud_progress_timeout,
    1,
    NULL    },
{
    MV2_UD_RECVWINDOW_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_RECVWINDOW_SIZE",
    &rdma_default_ud_recvwin_size,
    0,
    NULL    },
{
    MV2_UD_RETRY_COUNT,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_RETRY_COUNT",
    &rdma_ud_max_retry_count,
    1,
    NULL    },
{
    MV2_UD_RETRY_TIMEOUT,
    MV2_PARAM_TYPE_LONG,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_RETRY_TIMEOUT",
    &rdma_ud_retry_timeout,
    0,
    NULL    },
{
    MV2_UD_SENDWINDOW_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_SENDWINDOW_SIZE",
    &rdma_default_ud_sendwin_size,
    0,
    NULL    },
{
    MV2_UD_VBUF_POOL_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_VBUF_POOL_SIZE",
    &rdma_ud_vbuf_pool_size,
    0,
    NULL    },
{
    MV2_UD_ZCOPY_RQ_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_ZCOPY_RQ_SIZE",
    &rdma_ud_zcopy_rq_size,
    0,
    NULL    },
{
    MV2_UD_ZCOPY_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_ZCOPY_THRESHOLD",
    &rdma_ud_zcopy_threshold,
    0,
    NULL    },
{
    MV2_UD_ZCOPY_NUM_RETRY,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_ZCOPY_NUM_RETRY",
    &rdma_ud_zcopy_num_retry,
    0,
    NULL    },
{
    MV2_USE_UD_ZCOPY,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_hybrid,
    "MV2_USE_UD_ZCOPY",
    &rdma_use_ud_zcopy,
    1,
    NULL    },
{
    MV2_USE_UD_HYBRID,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_hybrid,
    "MV2_USE_UD_HYBRID",
    &rdma_enable_hybrid,
    1,
    NULL    },
{
    MV2_USE_ONLY_UD,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_hybrid,
    "MV2_USE_ONLY_UD",
    &rdma_enable_only_ud,
    0,
    NULL    },
{
    MV2_USE_UD_SRQ,
    MV2_PARAM_TYPE_INT8,
    MV2_PARAM_GROUP_hybrid,
    "MV2_USE_UD_SRQ",
    &rdma_use_ud_srq,
    1,
    NULL    },
{
    MV2_UD_SRQ_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_SRQ_SIZE",
    &mv2_ud_srq_fill_size,
    1,
    NULL    },
{
    MV2_UD_SRQ_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_SRQ_LIMIT",
    &mv2_ud_srq_limit,
    1,
    NULL    },
{
    MV2_UD_SRQ_MAX_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_UD_SRQ_MAX_SIZE",
    &mv2_ud_srq_alloc_size,
    1,
    NULL    },
{
    MV2_HYBRID_ENABLE_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_hybrid,
    "MV2_HYBRID_ENABLE_THRESHOLD",
    &rdma_hybrid_enable_threshold,
    1,
    NULL    },
{
    MV2_HYBRID_MAX_RC_CONN,
    MV2_PARAM_TYPE_INT16,
    MV2_PARAM_GROUP_hybrid,
    "MV2_HYBRID_MAX_RC_CONN",
    &rdma_hybrid_max_rc_conn,
    0,
    NULL    },
#endif /* _ENABLE_HYBRID_ */
/* threads */
{
    MV2_ASYNC_THREAD_STACK_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_threads,
    "MV2_ASYNC_THREAD_STACK_SIZE",
    &rdma_default_async_thread_stack_size,
    0,
    NULL    },
{
    MV2_CM_THREAD_STACKSIZE,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_threads,
    "MV2_CM_THREAD_STACKSIZE",
    NULL,
    0,
    NULL    },
{
    MV2_THREAD_YIELD_SPIN_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_threads,
    "MV2_THREAD_YIELD_SPIN_THRESHOLD",
    &rdma_polling_spin_count_threshold,
    0,
    NULL    },
{
    MV2_USE_THREAD_WARNING,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_threads,
    "MV2_USE_THREAD_WARNING",
    NULL,
    0,
    NULL    },
{
    MV2_USE_THREAD_YIELD,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_threads,
    "MV2_USE_THREAD_YIELD",
    NULL,
    0,
    NULL    },
/* other */
{
    MV2_SUPPORT_DPM,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_other,
    "MV2_SUPPORT_DPM",
    &MPIDI_CH3I_Process.has_dpm, // Hari 
    1,
    NULL    },
{
    MV2_USE_APM,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_other,
    "MV2_USE_APM",
    NULL,
    1,
    NULL    },
{
    MV2_USE_APM_TEST,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_other,
    "MV2_USE_APM_TEST",
    NULL,
    1,
    NULL    },
{
    MV2_USE_HSAM,
    MV2_PARAM_TYPE_INVALID,
    MV2_PARAM_GROUP_other,
    "MV2_USE_HSAM",
    NULL,
    0,
    NULL    },
{
    MV2_USE_HUGEPAGES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_other,
    "MV2_USE_HUGEPAGES",
    &rdma_enable_hugepage,
    1,
    NULL    },

};



/* List of all cuda runtime environment variables.
 * ** Format of the parameter info
 * * **  {
 * * **      id,
 * * **      datatype,
 * * **      name,
 * * **      addr of variables which stores the param value
 * * **      externally visible 1 or 0
 * * **      descrption of the parameter.
 * * **  }
 * * */
#if defined(_ENABLE_CUDA_)
mv2_env_param_list_t  cuda_param_list[] = {
{
    MV2_CUDA_BLOCK_SIZE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_BLOCK_SIZE",
    &mv2_device_stage_block_size,
    1,
    NULL    },
{
    MV2_CUDA_NUM_RNDV_BLOCKS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_NUM_RNDV_BLOCKS",
    &mv2_device_num_rndv_blocks,
    0,
    NULL    },
{
    MV2_CUDA_VECTOR_OPT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_VECTOR_OPT",
    &rdma_cuda_vector_dt_opt,
    0,
    NULL    },
{
    MV2_CUDA_KERNEL_OPT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_KERNEL_OPT",
    &rdma_cuda_kernel_dt_opt,
    0,
    NULL    },
{
    MV2_EAGER_CUDAHOST_REG,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_EAGER_CUDAHOST_REG",
    &rdma_eager_devicehost_reg,
    0,
    NULL    },
{
    MV2_USE_CUDA,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_USE_CUDA",
    &rdma_enable_cuda,
    0,
    NULL    },
{
    MV2_CUDA_NUM_EVENTS,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_NUM_EVENTS",
    &mv2_device_event_count,
    0,
    NULL    },
#if defined(HAVE_CUDA_IPC)
{
    MV2_CUDA_IPC,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_IPC",
    &mv2_device_use_ipc,
    0,
    NULL    },
{
    MV2_CUDA_IPC_THRESHOLD,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_IPC_THRESHOLD",
    &mv2_device_ipc_threshold,
    1,
    NULL    },
{
    MV2_CUDA_ENABLE_IPC_CACHE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ENABLE_IPC_CACHE",
    &mv2_device_enable_ipc_cache,
    0,
    NULL    },
{
    MV2_CUDA_IPC_MAX_CACHE_ENTRIES,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_IPC_MAX_CACHE_ENTRIES",
    &mv2_device_ipc_cache_max_entries,
    0,
    NULL    },
#endif /*#if defined(HAVE_CUDA_IPC)*/
{
    MV2_CUDA_USE_NAIVE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_USE_NAIVE",
    &mv2_device_coll_use_stage,
    0,
    NULL    },
{
    MV2_CUDA_REGISTER_NAIVE_BUF,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_REGISTER_NAIVE_BUF",
    &mv2_device_coll_register_stage_buf_threshold,
    0,
    NULL    },
{
    MV2_CUDA_GATHER_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_GATHER_NAIVE_LIMIT",
    &mv2_device_gather_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_SCATTER_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_SCATTER_NAIVE_LIMIT",
    &mv2_device_scatter_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLGATHER_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLGATHER_NAIVE_LIMIT",
    &mv2_device_allgather_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLGATHERV_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLGATHERV_NAIVE_LIMIT",
    &mv2_device_allgatherv_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLTOALL_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLTOALL_NAIVE_LIMIT",
    &mv2_device_alltoall_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLTOALLV_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLTOALLV_NAIVE_LIMIT",
    &mv2_device_alltoallv_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_BCAST_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_BCAST_NAIVE_LIMIT",
    &mv2_device_bcast_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_GATHERV_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_GATHERV_NAIVE_LIMIT",
    &mv2_device_gatherv_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_SCATTERV_NAIVE_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_SCATTERV_NAIVE_LIMIT",
    &mv2_device_scatterv_stage_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLTOALL_DYNAMIC,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLTOALL_DYNAMIC",
    &mv2_device_alltoall_dynamic,
    0,
    NULL    },
{
    MV2_CUDA_ALLGATHER_RD_LIMIT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLGATHER_RD_LIMIT",
    &mv2_device_allgather_rd_limit,
    0,
    NULL    },
{
    MV2_CUDA_ALLGATHER_FGP,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_ALLGATHER_FGP",
    &mv2_device_use_allgather_fgp,
    0,
    NULL    },
{
    MV2_SMP_CUDA_PIPELINE,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_SMP_CUDA_PIPELINE",
    &mv2_device_smp_pipeline,
    0,
    NULL    },
{
    MV2_CUDA_INIT_CONTEXT,
    MV2_PARAM_TYPE_INT,
    MV2_PARAM_GROUP_cuda,
    "MV2_CUDA_INIT_CONTEXT",
    &mv2_device_init_context,
    0,
    NULL    },
};
#endif /*#if defined(_ENABLE_CUDA_)*/




void mv2_show_all_params()
{
    int i, n_params;
    char *value;
    n_params = sizeof(param_list)/sizeof(param_list[0]);
    fprintf(stderr, "\n MVAPICH2 All Parameters\n");
    for (i = 0; i <n_params; i++) {
        if(param_list[i].value != NULL) {
            switch(param_list[i].type) {
                case MV2_PARAM_TYPE_INT8:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        param_list[i].name, *(uint8_t *)(param_list[i].value));
                        break;
                case MV2_PARAM_TYPE_INT16:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        param_list[i].name, *(uint16_t *)(param_list[i].value));
                        break;
                case MV2_PARAM_TYPE_INT:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        param_list[i].name, *(int *)(param_list[i].value));
                    break;
                case MV2_PARAM_TYPE_LONG:
                    fprintf(stderr, "\t%-35s : %ld\n", 
                        param_list[i].name, *(long *)param_list[i].value);
                    break;
                case MV2_PARAM_TYPE_STRING:
                    fprintf(stderr, "\t%-35s : %s\n", 
                        param_list[i].name, (char *)param_list[i].value);
                    break;
                case MV2_PARAM_TYPE_INVALID:
                    if(param_list[i].value != NULL) {
                        //fprintf(stderr, "\n Param :%s is missing datate\n",
                        //param_list[i].name);
                    }
                    break;
            }
        } else if((value = getenv(param_list[i].name)) != NULL) {
                    fprintf(stderr, "\t%-35s : %s\n", 
                                param_list[i].name, value);
        }
    }
}


#if defined(_ENABLE_CUDA_)
void mv2_show_cuda_params()
{
    int i, n_params;
    char *value;
    n_params = sizeof(cuda_param_list)/sizeof(cuda_param_list[0]);
    fprintf(stderr, "\n MVAPICH2 GDR Parameters\n");
    for (i = 0; i <n_params; i++) {
        if(cuda_param_list[i].value != NULL) {
            switch(cuda_param_list[i].type) {
                case MV2_PARAM_TYPE_INT8:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        cuda_param_list[i].name, *(uint8_t *)(cuda_param_list[i].value));
                        break;
                case MV2_PARAM_TYPE_INT16:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        cuda_param_list[i].name, *(uint16_t *)(cuda_param_list[i].value));
                        break;
                case MV2_PARAM_TYPE_INT:
                    fprintf(stderr, "\t%-35s : %d\n", 
                        cuda_param_list[i].name, *(int *)(cuda_param_list[i].value));
                    break;
                case MV2_PARAM_TYPE_LONG:
                    fprintf(stderr, "\t%-35s : %ld\n", 
                        cuda_param_list[i].name, *(long *)cuda_param_list[i].value);
                    break;
                case MV2_PARAM_TYPE_STRING:
                    fprintf(stderr, "\t%-35s : %s\n", 
                        cuda_param_list[i].name, (char *)cuda_param_list[i].value);
                    break;
                case MV2_PARAM_TYPE_INVALID:
                    if(cuda_param_list[i].value != NULL) {
                    }
                    break;
            }
        } else if((value = getenv(cuda_param_list[i].name)) != NULL) {
                    fprintf(stderr, "\t%-35s : %s\n",
                                cuda_param_list[i].name, value);
        }
    }
}
#endif /*#if defined(_ENABLE_CUDA_)*/

/* List of all runtime info.
** Format of the parameter info
**  {
**      description,
**      addr of variables which stores the param value
**      datatype,
**   }
***/

typedef struct mv2_runlog_info_list {
    char *description;
    void *param;
    MPI_Datatype datatype;
} mv2_runlog_info_list_t;

int mv2_show_runlog_level = 0;

mv2_runlog_info_list_t runlog_info[] = {
{"Max dreg entries used",            &dreg_max_use_count,         MPI_INT},
};

#undef FUNCNAME
#define FUNCNAME mv2_print_param_info
#undef FCNAME
#define FCNAME MPL_QUOTE(FUNCNAME)
static inline int mv2_print_param_info(MPID_Comm *comm_ptr, mv2_runlog_info_list_t *item, int level)
{
    char param_avg[16], param_min[16], param_max[16];
    int root=0;
    int mpi_errno = MPI_SUCCESS;
    MPIR_Errflag_t errflag = MPIR_ERR_NONE;

    if (level == 2 ) {
        mpi_errno =  MPIR_Reduce_binomial_MV2(item->param, param_max, 1, item->datatype,
                MPI_MAX, root, comm_ptr, &errflag);
        if (mpi_errno) {
            MPIR_ERR_POP(mpi_errno);
        }
        mpi_errno = MPIR_Reduce_binomial_MV2(item->param, param_min, 1, item->datatype,
                MPI_MIN, root, comm_ptr, &errflag);
        if (mpi_errno) {
            MPIR_ERR_POP(mpi_errno);
        }
        mpi_errno = MPIR_Reduce_binomial_MV2(item->param, param_avg, 1, item->datatype,
                MPI_SUM, root, comm_ptr, &errflag);
        if (mpi_errno) {
            MPIR_ERR_POP(mpi_errno);
        }
    }

    if(comm_ptr->rank == 0) {
        if (level == 2) {
            if(item->datatype == MPI_LONG) {
                long *pavg, *pmin, *pmax;
                pavg = (long *) param_avg;
                pmin = (long *) param_min;
                pmax = (long *) param_max;
                *pavg =  *pavg / comm_ptr->local_size;
                fprintf(stderr, "\t%-30s  : Min: %-8lu  Max: %-8lu Avg: %-8lu \n",
                        item->description, *pmin, *pmax, *pavg);
            } else {
                int *pavg, *pmin, *pmax;
                pavg = (int *) param_avg;
                pmin = (int *) param_min;
                pmax = (int *) param_max;
                *pavg /= comm_ptr->local_size;
                fprintf(stderr, "\t%-30s  : Min: %-8d  Max: %-8d Avg: %-8d \n",
                        item->description, *pmin, *pmax, *pavg);
            }
        } else {
            if(item->datatype == MPI_LONG) {
                fprintf(stderr, "\t%-30s  : %-8lu\n",
                        item->description, *(long *)item->param);
            } else {
                fprintf(stderr, "\t%-30s  :%-8d \n",
                        item->description, *(int *)item->param);
            }
        }
    }

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;

}

void mv2_show_runlog_info(int level)
{
    int pg_rank;
    int n_params, i;

    MPID_Comm *comm_ptr = NULL;

    MPID_Comm_get_ptr(MPI_COMM_WORLD, comm_ptr);
    pg_rank = comm_ptr->rank;

    n_params = sizeof(runlog_info)/sizeof(runlog_info[0]);

    if (pg_rank == 0) {
        fprintf(stderr, "\n-------------------------------");
        fprintf(stderr, "\n\n MVAPICH2 DEBUG RUN LOG\n\n");
    }

    for (i = 0; i <n_params; i++) {
        mv2_print_param_info(comm_ptr, &runlog_info[i], level);
    }

    if (pg_rank == 0) {
        fprintf(stderr, "-------------------------------\n");
    }

}

