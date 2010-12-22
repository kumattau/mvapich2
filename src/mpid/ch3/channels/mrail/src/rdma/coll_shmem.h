/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2010, The Ohio State University. All rights
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
#include "smp_smpi.h"

/*********** Macro defines of local variables ************/
#define PID_CHAR_LEN 22

#define SHMEM_COLL_HOSTNAME_LEN  (255)

#define SHMEM_SMALLEST_SIZE (64)

#define SHMEM_MAX_INT ((unsigned int)(-1))

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

#define SHMEM_COLL_NUM_PROCS 32
#define SHMEM_COLL_NUM_COMM  20

#define SHMEM_COLL_BLOCK_SIZE (g_smpi.num_local_nodes * g_shmem_coll_max_msg_size)

/* the shared area itself */
typedef struct {
    volatile int child_complete_bcast[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];   /* use for initial synchro */
    volatile int root_complete_bcast[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];  
    volatile int child_complete_gather[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];   /* use for initial synchro */
    volatile int root_complete_gather[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];  
    volatile int barrier_gather[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];
    volatile int barrier_bcast[SHMEM_COLL_NUM_COMM][SHMEM_COLL_NUM_PROCS];
    volatile int shmem_comm_count;
    pthread_spinlock_t shmem_coll_lock;

#if defined(CKPT)
    volatile int cr_smc_cnt;
    volatile pthread_spinlock_t cr_smc_spinlock;
#endif

    /* the collective buffer */
    char shmem_coll_buf;
} shmem_coll_region;

#define SHMEM_COLL_BUF_SIZE (g_shmem_coll_blocks * SHMEM_COLL_BLOCK_SIZE + sizeof(shmem_coll_region))
shmem_coll_region *shmem_coll;


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

extern int tuning_table[COLL_COUNT][COLL_SIZE]; 


#define BCAST_LEN 20
#define SHMEM_BCAST_FLAGS	1024
/*
 * We're converting this into a environment variable
 * #define SHMEM_BCAST_LEADERS     1024
 */
#define SHMEM_BCAST_METADATA	(sizeof(addrint_t) + 2*sizeof(int))       /* METADATA: buffer address, offset, num_bytes */ 

extern int enable_shmem_collectives;
extern struct coll_runtime coll_param;
extern void MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int, int, int, void**);
extern void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int, int, int);

/* Use inside allreduce_osu.c*/
extern int disable_shmem_allreduce;
extern int check_comm_registry(MPI_Comm);


/* Use inside alltoall_osu.h */
extern int alltoall_dreg_disable_threshold;
extern int alltoall_dreg_disable;
extern int g_is_dreg_initialized;


/* Use inside barrier_osu.c*/
extern int disable_shmem_barrier;
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int, int, int);
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int, int, int);

/* Use inside bcast_osu.c */
extern int  knomial_2level_bcast_system_size_threshold;
extern int  knomial_2level_bcast_message_size_threshold;
extern int  enable_knomial_2level_bcast;
extern int  inter_node_knomial_factor;
extern int  intra_node_knomial_factor;
extern int  bcast_short_msg_threshold;
extern void MPID_SHMEM_COLL_GetShmemBcastBuf(void**, void*);
extern void signal_local_processes(int, int, char*, int, int, void*);
extern void wait_for_signal(int, int, char**, int*, int*, void*);

/* Use inside reduce_osu.c */
extern int disable_shmem_reduce;
extern int check_comm_registry(MPI_Comm);

#endif  /* _COLL_SHMEM_ */
