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
/* initialize all MVAPICH MPIT parameters */
int MV2_init_mpit_params(void);

/* end impl functions for MPI_T (MPI_T_ right now) */

/* to calculate memory usage */
extern unsigned long long mpit_mem_allocated_current;
extern unsigned long long mpit_mem_allocated_max;
extern unsigned long long mpit_mem_allocated_min;

/* to count progress engine polling */
extern unsigned long mpit_progress_poll;

/* to count number of shared-memory collective calls */
extern unsigned long mv2_num_shmem_coll_calls; 

/* to count 2-level communicator creation requests */
extern unsigned long mv2_num_2level_comm_requests; 
extern unsigned long mv2_num_2level_comm_success;

/* to track the registration cache hits and misses */
extern unsigned long dreg_stat_cache_hit;
extern unsigned long dreg_stat_cache_miss;

/* to track the vbuf usage */
extern int vbuf_n_allocated;
extern long num_free_vbuf;
extern long num_vbuf_freed;

#if defined(_ENABLE_UD_) || defined(_MCST_SUPPORT_)
extern int ud_vbuf_n_allocated;
extern long ud_num_free_vbuf;
extern long ud_num_vbuf_freed;
#endif


/* to count MPICH Broadcast algorithms used */
extern unsigned long long mpit_bcast_smp;
extern unsigned long long mpit_bcast_binomial;
extern unsigned long long mpit_bcast_scatter_doubling_allgather;
extern unsigned long long mpit_bcast_scatter_ring_allgather;

/* to count MVAPICH Broadcast algorithms used */
extern unsigned long long mpit_bcast_mv2_binomial;
extern unsigned long long mpit_bcast_mv2_scatter_doubling_allgather;
extern unsigned long long mpit_bcast_mv2_scatter_ring_allgather;
extern unsigned long long mpit_bcast_mv2_scatter_ring_allgather_shm;
extern unsigned long long mpit_bcast_mv2_shmem;
extern unsigned long long mpit_bcast_mv2_knomial_internode;
extern unsigned long long mpit_bcast_mv2_knomial_intranode;
extern unsigned long long mpit_bcast_mv2_mcast_internode;
extern unsigned long long mpit_bcast_mv2_pipelined;

/* to count number of UD retransmissions */
#if defined(_ENABLE_UD_)
extern long rdma_ud_retransmissions;
#endif

/* IB channel-manager profiling */
extern unsigned long mv2_ibv_channel_ctrl_packet_count;
extern unsigned long mv2_ibv_channel_out_of_order_packet_count;
extern unsigned long mv2_ibv_channel_exact_recv_count;

/* to profile RDMA_FP connections and packets */
extern unsigned long mv2_rdmafp_ctrl_packet_count;
extern unsigned long mv2_rdmafp_out_of_order_packet_count;
extern unsigned long mv2_rdmafp_exact_recv_count;
