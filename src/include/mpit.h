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
extern int mv2_num_shmem_coll_calls; 

/* to count 2-level communicator creation requests */
extern int mv2_num_2level_comm_requests; 
extern int mv2_num_2level_comm_success;

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
extern long rdma_ud_retransmissions;
