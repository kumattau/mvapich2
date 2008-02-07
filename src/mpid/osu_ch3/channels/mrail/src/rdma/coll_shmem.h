/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */


#ifndef _COLL_SHMEM_
#define _COLL_SHMEM_
#include <pthread.h>
/*********** Macro defines of local variables ************/
#define PID_CHAR_LEN 22

#define SHMEM_COLL_HOSTNAME_LEN  (255)

#define SHMEM_SMALLEST_SIZE (64)

#define SHMEM_MAX_INT ((unsigned int)(-1))

#if defined(_IA32_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a)                                               \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SHMEM_AVAIL(a)	\
 ((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_IA64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                               \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_X86_64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                               \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_EM64T_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a +SHMEM_CACHE_LINE_SIZE)

#define SHMEM_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(MAC_OSX)

#define SHMEM_CACHE_LINE_SIZE 16
#define SHMEM_ALIGN(a)                                               \
(((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SHMEM_AVAIL(a)   \
((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#else
                                                                                                                                               
#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a +SHMEM_CACHE_LINE_SIZE)

#define SHMEM_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#endif

#define SHMEM_COLL_NUM_PROCS 32 
#define SHMEM_COLL_NUM_COMM  20

#define SHMEM_COLL_MAX_MSG_SIZE (1<<17)
#define SHMEM_COLL_BLOCK_SIZE    ((smpi.num_local_nodes) * shmem_coll_max_msg_size)
    
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

   /* the collective buffer */
    char shmem_coll_buf;
}shmem_coll_region;

#define SHMEM_COLL_BUF_SIZE (shmem_coll_blocks * SHMEM_COLL_BLOCK_SIZE + sizeof(shmem_coll_region))
shmem_coll_region *shmem_coll;

#endif
