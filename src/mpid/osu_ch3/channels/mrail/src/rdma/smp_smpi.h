/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2006, The Ohio State University. All rights
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


#ifndef _SMPI_SMP_
#define _SMPI_SMP_

/*********** Macro defines of local variables ************/
#define PID_CHAR_LEN 22

#define SMPI_SMALLEST_SIZE (64)

#define SMPI_MAX_INT ((unsigned int)(-1))

#if defined(_IA32_)

#define SMP_EAGERSIZE	    (8)
#define SMPI_LENGTH_QUEUE   (32) /* 32 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192
#define SMP_NUM_SEND_BUFFER 128

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SMPI_AVAIL(a)	\
 ((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_IA64_)

#define SMP_EAGERSIZE       (8)
#define SMPI_LENGTH_QUEUE   (32) /* 32 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192
#define SMP_NUM_SEND_BUFFER 128

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_X86_64_)

#define SMP_EAGERSIZE	    (8)
#define SMPI_LENGTH_QUEUE   (32) /* 32 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192
#define SMP_NUM_SEND_BUFFER 32

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(_EM64T_)

#define SMP_EAGERSIZE       (64)
#define SMPI_LENGTH_QUEUE   (256) /* 256 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192 
#define SMP_NUM_SEND_BUFFER 256 

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(MAC_OSX)

#define SMP_EAGERSIZE	    (8)
#define SMPI_LENGTH_QUEUE   (32) /* 32 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192
#define SMP_NUM_SEND_BUFFER 128

#define SMPI_CACHE_LINE_SIZE 16
#define SMPI_ALIGN(a)                                               \
(((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#else
                                                                                                                                               
#define SMP_EAGERSIZE	    (16)
#define SMPI_LENGTH_QUEUE   (64) /* 32 Kbytes */
#define SMP_BATCH_SIZE 8
#define SMP_SEND_BUF_SIZE 8192 
#define SMP_NUM_SEND_BUFFER 128

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#endif

typedef struct {
    volatile unsigned int current;
    volatile unsigned int next;
    volatile unsigned int msgs_total_in;
} smpi_params;

typedef struct {
    volatile unsigned int msgs_total_out;
    char pad[SMPI_CACHE_LINE_SIZE - 4];
} smpi_rqueues;

typedef struct {
    volatile unsigned int first;
    volatile unsigned int last;
} smpi_rq_limit;

/* the shared area itself */
struct shared_mem {
    volatile int *pid;   /* use for initial synchro */
    /* receive queues descriptors */
    smpi_params **rqueues_params;

     /* rqueues flow control */
    smpi_rqueues **rqueues_flow_out;

    smpi_rq_limit **rqueues_limits;

    /* the receives queues */
    char *pool;
};

/* structure for a buffer in the sending buffer pool */
typedef struct send_buf_t {
    int myindex;
    int next;
    volatile int busy;
    int len;
    int has_next;
    int msg_complete;
    char buf[SMP_SEND_BUF_SIZE];
} SEND_BUF_T;

/* send queue, to be initialized */
struct shared_buffer_pool {
    int free_head;
    int *send_queue;
    int *tail;
};

extern struct smpi_var smpi;
extern struct shared_mem *smpi_shmem;

#endif
