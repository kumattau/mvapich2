/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2001-2012, The Ohio State University. All rights
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

#ifndef _SMPI_SMP_
#define _SMPI_SMP_

#ifdef _SMP_LIMIC_
#   include <limic.h>
#endif

/* SMP user parameters*/

extern int                  g_smp_eagersize;
extern int                  s_smpi_length_queue;
extern int                  s_smp_num_send_buffer;
extern int                  s_smp_batch_size;
extern int                  s_smp_block_size;

#if defined _ENABLE_CUDA_
extern int                  s_smp_cuda_pipeline;
extern int                  s_smp_h2h_block_size;
#endif


#if defined(_ENABLE_CUDA_) && defined(HAVE_CUDA_IPC)
extern void **smp_cuda_region_send;
extern void **smp_cuda_region_recv;
extern int smp_cuda_region_size;

extern CUevent *sr_event;
extern CUevent *sr_event_local;
extern CUevent *loop_event;
extern CUevent *loop_event_local;
#endif 

/*********** Macro defines of local variables ************/
#define PID_CHAR_LEN 22

#define SMPI_SMALLEST_SIZE (64)

#define SMPI_MAX_INT ((unsigned int)(-1))

#if defined(_IA32_)

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SMPI_AVAIL(a)	\
 ((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_IA64_)

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(_X86_64_) && defined(_AMD_QUAD_CORE_)

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(_X86_64_)

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(_EM64T_)

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#elif defined(MAC_OSX)

#define SMPI_CACHE_LINE_SIZE 16
#define SMPI_ALIGN(a)                                               \
(((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#else
                                                                                                                                               
#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#endif

typedef struct {
    volatile unsigned int current;
} smpi_params_c;

typedef struct {
    volatile unsigned int next;
} smpi_params_n;

typedef struct {
    volatile unsigned int msgs_total_in;
    volatile unsigned int msgs_total_out;
    char pad[SMPI_CACHE_LINE_SIZE/2 - 8];
} smpi_rqueues;

#if defined(_ENABLE_CUDA_) && defined(HAVE_CUDA_IPC)
typedef struct {
    volatile unsigned int cuda_head; 
    volatile unsigned int cuda_tail;    
    char pad[SMPI_CACHE_LINE_SIZE/2 - 8];
} smpi_cu_ipc_attr;
#endif  

typedef struct {
    volatile unsigned int ptr;
    char pad[SMPI_CACHE_LINE_SIZE - sizeof(unsigned int)];
} smpi_shared_tails;

typedef struct {
    volatile unsigned int first;
    volatile unsigned int last;
} smpi_rq_limit;

/* the shared area itself */
struct shared_mem {
    volatile int *pid;   /* use for initial synchro */

    smpi_shared_tails **shared_tails;

#if defined(_ENABLE_CUDA_) && defined(HAVE_CUDA_IPC)
    smpi_cu_ipc_attr **cu_attrbs;
#endif

    smpi_rq_limit *rqueues_limits_s;
    smpi_rq_limit *rqueues_limits_r;

    /* the receives queues */
    char *pool;
};

/* structure for a buffer in the sending buffer pool */
typedef struct send_buf_t {
    int myindex;
    volatile int next;
    volatile int busy;
    int len;
    volatile int has_next;
    int msg_complete;
    char buf[];
} SEND_BUF_T;

/* send queue, to be initialized */
struct shared_buffer_pool {
    int free_head;
    int *send_queue;
    int *tail;
};

#if defined(_SMP_LIMIC_)
struct limic_header {
    limic_user lu; 
    int total_bytes;
    struct MPID_Request *send_req_id;
};
extern int g_smp_use_limic2;
extern int g_use_limic2_coll;
#endif

extern struct smpi_var g_smpi;
extern struct shared_mem *g_smpi_shmem;


#endif
