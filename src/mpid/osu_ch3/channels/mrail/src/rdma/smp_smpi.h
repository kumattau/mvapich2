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

#define SMP_EAGERSIZE	    (256)
#define SMPI_LENGTH_QUEUE   (1) /* 1 Mbytes */

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SMPI_AVAIL(a)	\
 ((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_IA64_)

#define SMP_EAGERSIZE       (256)
#define SMPI_LENGTH_QUEUE   (4) /* 4 Mbytes */

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

                                                                                                                                               
#elif defined(_X86_64_)

#define SMP_EAGERSIZE	    (256)
#define SMPI_LENGTH_QUEUE   (2) /* 4 Mbytes */

#define SMPI_CACHE_LINE_SIZE 128
#define SMPI_ALIGN(a)                                               \
((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SMPI_AVAIL(a)   \
 ((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)
                                                                                                                                               
#elif defined(MAC_OSX)

#define SMP_EAGERSIZE	    (256)
#define SMPI_LENGTH_QUEUE   (4) /* 4 Mbytes */

#define SMPI_CACHE_LINE_SIZE 16
#define SMPI_ALIGN(a)                                               \
(((a + SMPI_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#else
                                                                                                                                               
#define SMP_EAGERSIZE	    (256)
#define SMPI_LENGTH_QUEUE   (1) /* 1 Mbytes */

#define SMPI_CACHE_LINE_SIZE 64
#define SMPI_ALIGN(a) (a +SMPI_CACHE_LINE_SIZE)

#define SMPI_AVAIL(a)   \
((a & 0xFFFFFFFFFFFFFFF8) - SMPI_CACHE_LINE_SIZE)

#endif

/* the shared area itself */
struct shared_mem {
    volatile int pid[SMPI_MAX_NUMLOCALNODES];   /* use for initial synchro */
    /* receive queues descriptors */
    volatile struct {
        volatile struct {
            volatile unsigned int current;
            volatile unsigned int next;
            volatile unsigned int msgs_total_in;
        } params[SMPI_MAX_NUMLOCALNODES];
        char pad[SMPI_CACHE_LINE_SIZE];
    } rqueues_params[SMPI_MAX_NUMLOCALNODES];
                                                                                                                                               
    /* rqueues flow control */
    volatile struct {
        volatile unsigned int msgs_total_out;
        char pad[SMPI_CACHE_LINE_SIZE - 4];
    } rqueues_flow_out[SMPI_MAX_NUMLOCALNODES][SMPI_MAX_NUMLOCALNODES];
                                                                                                                                               
    volatile struct {
        volatile unsigned int first;
        volatile unsigned int last;
    } rqueues_limits[SMPI_MAX_NUMLOCALNODES][SMPI_MAX_NUMLOCALNODES];
    int pad2[SMPI_CACHE_LINE_SIZE];
                                                                                                                                               
    /* the receives queues */
    volatile char pool;
};

extern struct smpi_var smpi;
extern struct shared_mem *smpi_shmem;

#endif
