
/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#ifndef MVAPICH2_GEN2_RDMA_CM_H
#define MVAPICH2_GEN2_RDMA_CM_H

#include "rdma_impl.h"

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <pthread.h>

extern int num_smp_peers;

#ifdef RDMA_CM

#include <rdma/rdma_cma.h>


extern int *rdma_cm_host_list;

/* Initiate all active connect requests */
int rdma_cm_connect_all(int *hosts, int pg_rank, int pg_size);

/* Exchange the ip information with all the processes */
int *rdma_cm_get_hostnames(int pg_rank, int pg_size);

/* Initialize rdma_cm resources + cm_ids + bind port + connection thrd */
void ib_init_rdma_cm(struct MPIDI_CH3I_RDMA_Process_t *proc,
		     int pg_rank, int pg_size);

/* Finalize rdma_cm specific resources */
void ib_finalize_rdma_cm(int pg_rank, int pg_size);

#endif

#endif  /* MVAPICH2_GEN2_CM_H */
