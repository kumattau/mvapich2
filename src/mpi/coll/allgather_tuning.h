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

#ifndef _ALLGATHER_TUNING_
#define _ALLGATHER_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif                          /* #if defined(_OSU_MVAPICH_) */

#define NMATCH (3+1)

/* Allgather tuning flags 
 * recursive doubling with allgather_comm: MV2_INTER_ALLGATHER_TUNING=1 
 * recursive doubling: MV2_INTER_ALLGATHER_TUNING=2
 * bruck: MV2_INTER_ALLGATHER_TUNING=3
 * ring: MV2_INTER_ALLGATHER_TUNING=4
 * 2-level recursive doubling:  MV2_INTER_ALLGATHER_TUNING=2 
 *                              MV2_INTER_ALLGATHER_TUNING_TWO_LEVEL=1
 * 2-level bruck: MV2_INTER_ALLGATHER_TUNING=3
 *                MV2_INTER_ALLGATHER_TUNING_TWO_LEVEL=1
 * 2-level ring:  MV2_INTER_ALLGATHER_TUNING=4
 *                MV2_INTER_ALLGATHER_TUNING_TWO_LEVEL=1
 */

typedef struct {
    int min;
    int max;
    int (*MV2_pt_Allgather_function)(const void *sendbuf,
                                 int sendcount,
                                 MPI_Datatype sendtype,
                                 void *recvbuf,
                                 int recvcount,
                                 MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                                 int *errflag);
} mv2_allgather_tuning_element;

typedef struct {
    int numproc; 
    int two_level[MV2_MAX_NB_THRESHOLDS];
    int size_inter_table;
    mv2_allgather_tuning_element inter_leader[MV2_MAX_NB_THRESHOLDS];
} mv2_allgather_tuning_table;

extern int mv2_size_allgather_tuning_table;
extern mv2_allgather_tuning_table *mv2_allgather_thresholds_table;
extern int mv2_use_old_allgather;

extern int MPIR_Allgather_RD_Allgather_Comm_MV2(const void *sendbuf,
                                 int sendcount,
                                 MPI_Datatype sendtype,
                                 void *recvbuf,
                                 int recvcount,
                                 MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                                 int *errflag);

extern int MPIR_Allgather_RD_MV2(const void *sendbuf,
                                 int sendcount,
                                 MPI_Datatype sendtype,
                                 void *recvbuf,
                                 int recvcount,
                                 MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                                 int *errflag);

extern int MPIR_Allgather_Bruck_MV2(const void *sendbuf,
                                    int sendcount,
                                    MPI_Datatype sendtype,
                                    void *recvbuf,
                                    int recvcount,
                                    MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                                    int *errflag);

extern int MPIR_Allgather_Ring_MV2(const void *sendbuf,
                                   int sendcount,
                                   MPI_Datatype sendtype,
                                   void *recvbuf,
                                   int recvcount,
                                   MPI_Datatype recvtype, MPID_Comm * comm_ptr,
                                   int *errflag);

extern int MPIR_2lvl_Allgather_MV2(const void *sendbuf,int sendcnt, MPI_Datatype sendtype,
                                   void *recvbuf, int recvcnt,MPI_Datatype recvtype,
                                   MPID_Comm * comm_ptr, int *errflag);


/* Architecture detection tuning */
int MV2_set_allgather_tuning_table();

/* Function to clean free memory allocated by allgather tuning table*/
void MV2_cleanup_allgather_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune allgather thresholds */
int MV2_internode_Allgather_is_define(char *mv2_user_allgather_inter);

#endif


