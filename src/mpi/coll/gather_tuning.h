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

#ifndef _GATHER_TUNING_
#define _GATHER_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif /* #if defined(_OSU_MVAPICH_) */
#define MV2_DEFAULT_SHMEM_BCAST_LEADERS    4096
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_SMALL      384
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_MEDIUM     1024
#define MPIR_GATHER_BINOMIAL_MEDIUM_MSG 16384

#define NMATCH (3+1)

typedef struct {
    int min;
    int max;
    int (*MV2_pt_Gather_function)(void *sendbuf, int sendcnt,
                                  MPI_Datatype sendtype, void *recvbuf, int recvcnt,
                                  MPI_Datatype recvtype, int root, MPID_Comm * comm_ptr,
                                  int *errflag);
} mv2_gather_tuning_element;

typedef struct {
    int numproc;
    int size_inter_table;
    mv2_gather_tuning_element inter_leader[MV2_MAX_NB_THRESHOLDS];
    int size_intra_table;
    mv2_gather_tuning_element intra_node[MV2_MAX_NB_THRESHOLDS];
} mv2_gather_tuning_table;

extern int mv2_size_gather_tuning_table;
extern mv2_gather_tuning_table mv2_gather_thresholds_table[];

extern int mv2_user_gather_switch_point;
extern int mv2_use_two_level_gather;
extern int mv2_gather_direct_system_size_small;
extern int mv2_gather_direct_system_size_medium;
extern int mv2_use_direct_gather;

extern int MPIR_Gather_MV2_Direct(void *sendbuf, int sendcnt,
                                      MPI_Datatype sendtype, void *recvbuf, int recvcnt,
                                      MPI_Datatype recvtype, int root, MPID_Comm * comm_ptr,
                                      int *errflag);
extern int MPIR_Gather_MV2_two_level_Direct(void *sendbuf, int sendcnt,
            MPI_Datatype sendtype, void *recvbuf, int recvcnt,
            MPI_Datatype recvtype, int root, MPID_Comm * comm_ptr,
            int *errflag);
/* Architecture detection tuning */
int MV2_set_gather_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune gather thresholds */
int MV2_internode_Gather_is_define(char *mv2_user_gather_inter, char *mv2_user_gather_intra);
int MV2_intranode_Gather_is_define(char *mv2_user_gather_intra);
void MV2_user_gather_switch_point_is_define(int mv2_user_gather_switch_point);

#endif
