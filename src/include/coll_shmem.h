/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2012, The Ohio State University. All rights
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


#ifndef _COLL_SHMEM_
#define _COLL_SHMEM_
#include <pthread.h>
#include "mpidimpl.h"

/*********** Macro defines of local variables ************/
#define PID_CHAR_LEN 22

#define SHMEM_COLL_HOSTNAME_LEN  (255)

#define SHMEM_SMALLEST_SIZE (64)

#define SHMEM_MAX_INT ((unsigned int)(-1))

#define MV2_DEFAULT_SHMEM_BCAST_LEADERS    4096
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_SMALL      384
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_MEDIUM     1024

#define MV2_INTER_NODE_KNOMIAL_FACTOR_MAX 8
#define MV2_INTER_NODE_KNOMIAL_FACTOR_MIN 2
#define MV2_INTRA_NODE_KNOMIAL_FACTOR_MAX 8
#define MV2_INTRA_NODE_KNOMIAL_FACTOR_MIN 2 

#if defined(_IA32_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SHMEM_AVAIL(a)	                                  \
 ((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_IA64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)                                    \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_X86_64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)                                    \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_EM64T_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a + SHMEM_CACHE_LINE_SIZE)
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(MAC_OSX)

#define SHMEM_CACHE_LINE_SIZE 16
#define SHMEM_ALIGN(a)                                   \
(((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#else

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a + SHMEM_CACHE_LINE_SIZE)
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#endif


int MPIDI_CH3I_SHMEM_COLL_init(MPIDI_PG_t *pg, int local_id);

int MPIDI_CH3I_SHMEM_COLL_Mmap(MPIDI_PG_t *pg, int local_id); 

int MPIDI_CH3I_SHMEM_COLL_finalize(int local_id, int num_local_nodes);

void MPIDI_CH3I_SHMEM_COLL_Unlink(void);

void MV2_Read_env_vars(void);

#define SHMEM_COLL_BLOCK_SIZE (MPIDI_Process.my_pg->ch.num_local_processes * mv2_g_shmem_coll_max_msg_size)


#define COLL_COUNT              7
#define COLL_SIZE               3
#define ALLGATHER_ID            0
#define ALLREDUCE_SHMEM_ID      1
#define ALLREDUCE_2LEVEL_ID     2
#define BCAST_KNOMIAL_ID        3
#define BCAST_SHMEM_ID          4
#define REDUCE_SHMEM_ID         5
#define REDUCE_2LEVEL_ID        6

#define SMALL                   0
#define MEDIUM                  1
#define LARGE                   2

extern int mv2_tuning_table[COLL_COUNT][COLL_SIZE]; 

struct scatter_tuning{
    int numproc;
    int small;
    int medium;
};

struct gather_tuning{
    int numproc;
    int switchp;
};

struct allgatherv_tuning{
    int numproc;
    int switchp;
};

#define BCAST_LEN 20
#define SHMEM_BCAST_FLAGS	1024
/*
 * We're converting this into a environment variable
 * #define SHMEM_BCAST_LEADERS     1024
 */
#define SHMEM_BCAST_METADATA	(sizeof(addrint_t) + 2*sizeof(int))       
  /* METADATA: buffer address, offset, num_bytes */ 

extern int mv2_g_shmem_coll_max_msg_size;
extern int mv2_g_shmem_coll_blocks;
extern int mv2_shmem_coll_num_procs;
extern int mv2_shmem_coll_num_comm;
extern int mv2_shmem_coll_spin_count;
extern int mv2_enable_shmem_collectives;
int is_shmem_collectives_enabled();

extern struct coll_runtime mv2_coll_param;
void MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int, int, int, void**);
void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int, int, int);

extern int mv2_tune_parameter;
/* Use for gather_osu.c*/
#define MPIR_GATHER_BINOMIAL_MEDIUM_MSG 16384
extern int mv2_user_gather_switch_point;
extern int mv2_size_mv2_gather_mv2_tuning_table;
extern struct gather_tuning mv2_gather_mv2_tuning_table[8];
extern int mv2_use_two_level_gather; 
extern int mv2_gather_direct_system_size_small;
extern int mv2_gather_direct_system_size_medium; 
extern int mv2_use_direct_gather; 

/* Use for allgather_osu.c */
#define MV2_ALLGATHER_SMALL_SYSTEM_SIZE       128
#define MV2_ALLGATHER_MEDIUM_SYSTEM_SIZE      256
#define MV2_ALLGATHER_LARGE_SYSTEM_SIZE       512 
extern int mv2_allgather_ranking;

/* Use for allgatherv_osu.c */
extern int mv2_size_mv2_allgatherv_mv2_tuning_table;
extern struct allgatherv_tuning mv2_allgatherv_mv2_tuning_table[4];
extern int mv2_user_allgatherv_switch_point;

/* Use for scatter_osu.c*/
extern int mv2_user_scatter_small_msg;
extern int mv2_user_scatter_medium_msg;
extern int mv2_size_mv2_scatter_mv2_tuning_table;
extern struct scatter_tuning mv2_scatter_mv2_tuning_table[4];
extern int mv2_use_two_level_scatter; 
extern int mv2_use_direct_scatter; 


/* Use inside allreduce_osu.c*/
extern int mv2_disable_shmem_allreduce;
int check_comm_registry(MPI_Comm);


/* Use inside alltoall_osu.h */
extern int mv2_use_xor_alltoall; 


/* Use inside barrier_osu.c*/
extern int mv2_disable_shmem_barrier;
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int, int, int);
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int, int, int);

/* Use inside bcast_osu.c */
extern int  mv2_bcast_short_msg; 
extern int  mv2_bcast_large_msg; 
extern int  mv2_knomial_2level_bcast_system_size_threshold;
extern int  mv2_knomial_2level_bcast_message_size_threshold;
extern int  mv2_enable_knomial_2level_bcast;
extern int  mv2_inter_node_knomial_factor;
extern int  mv2_intra_node_knomial_factor;
extern int  mv2_scatter_rd_inter_leader_bcast; 
extern int  mv2_scatter_ring_inter_leader_bcast;
extern int  mv2_knomial_intra_node_threshold; 
extern int  mv2_knomial_inter_leader_threshold; 
extern int  mv2_knomial_inter_leader_bcast;
extern int  mv2_enable_shmem_bcast;
extern int  mv2_bcast_two_level_system_size; 
extern int MPIR_Shmem_Bcast_MV2( void *,  int ,  MPI_Datatype , int ,  MPID_Comm *);
extern int MPIR_Knomial_Bcast_intra_node_MV2(void *, int ,  MPI_Datatype, int , MPID_Comm *, int *);
extern int MPIR_Knomial_Bcast_inter_node_MV2(void *, int ,  MPI_Datatype, int , int * , MPID_Comm *, int *);



/* Use inside reduce_osu.c */
extern int mv2_disable_shmem_reduce;
int check_comm_registry(MPI_Comm);


/* Use inside red_scat_osu.c */
#define MPIR_RED_SCAT_SHORT_MSG 64
#define MPIR_RED_SCAT_LONG_MSG  512*1024
extern int mv2_red_scat_short_msg;
extern int mv2_red_scat_long_msg;

/* Lock/unlock shmem region */
void lock_shmem_region(void);
void unlock_shmem_region(void);

/* utils */
void increment_mv2_shmem_comm_count(void);
int get_mv2_shmem_comm_count(void);

void MPIDI_CH3I_SHMEM_Bcast_GetBuf(int, int, int, void**);
void MPIDI_CH3I_SHMEM_Bcast_Complete(int ,int , int);
int init_thread_reg(void);

extern int mv2_use_osu_collectives;
extern int mv2_use_anl_collectives;


/* Comm functions*/
extern int split_comm;
int check_split_comm(pthread_t);
int disable_split_comm(pthread_t);
int create_2level_comm (MPI_Comm, int, int);
int free_2level_comm (MPID_Comm *);
int enable_split_comm(pthread_t);
void MPIR_pof2_comm(MPID_Comm *, int, int);


#endif  /* _COLL_SHMEM_ */
