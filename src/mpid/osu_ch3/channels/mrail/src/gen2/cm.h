
/* Copyright (c) 2002-2007, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#ifndef MVAPICH2_GEN2_CM_H
#define MVAPICH2_GEN2_CM_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <infiniband/verbs.h>
#include <errno.h>
#include <assert.h>

#include "rdma_impl.h"

typedef struct MPICM_ib_context{
    int rank;
    int size;
    MPIDI_PG_t * pg;
}MPICM_ib_context;

extern pthread_mutex_t cm_conn_state_lock;
extern MPICM_ib_context cm_ib_context;
/*
MPICM_Init_UD
Initialize MPICM based on UD connection, need to be called before calling 
any of following interfaces, it will return a qpn for the established ud qp, 
mpi lib needs to exchange the ud qpn with other procs
*/
int MPICM_Init_UD(uint32_t *ud_qpn);

/*
MPICM_Connect_UD
Provide connect information to UD
*/
int MPICM_Connect_UD(uint32_t *qpns, uint16_t *lids);

/*
MPICM_Finalize_UD
Cleanup ud related data structures
*/
int MPICM_Finalize_UD();

/*Interface to lock/unlock connection manager*/
void MPICM_lock();

void MPICM_unlock();

#endif  /* MVAPICH2_GEN2_CM_H */
