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

#ifndef _ALLREDUCE_TUNING_
#define _ALLREDUCE_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif                          /* #if defined(_OSU_MVAPICH_) */

#define NMATCH (3+1)

/* Allreduce tuning flags
 * flat recursive doubling(rd): MV2_INTER_ALLREDUCE_TUNING=1
 * flat reduce scatter allgather(rsa): MV2_INTER_ALLREDUCE_TUNING=2
 * mcast: MV2_INTER_ALLREDUCE_TUNING_TWO_LEVEL=1 MV2_USE_MCAST_ALLREDUCE=1  
 *        MV2_USE_MCAST=1 MV2_INTER_ALLREDUCE_TUNING=3
 * 2-level: MV2_INTER_ALLREDUCE_TUNING_TWO_LEVEL=1
 *          MV2_INTER_ALLREDUCE_TUNING=?  MV2_INTRA_ALLREDUCE_TUNING=?
 *          intra-reduce flag can take 1(rd), 2(rsa), 5(shm), 6(p2p), while
 *          inter-reduce flag can take 1(rd), 2(rsa)
 */
 
typedef struct {
    int min;
    int max;
    int (*MV2_pt_Allreduce_function)(const void *sendbuf,
                                   void *recvbuf,
                                   int count,
                                   MPI_Datatype datatype,
                                   MPI_Op op, MPID_Comm * comm_ptr, int *errflag);
} mv2_allreduce_tuning_element;

typedef struct {
    int numproc; 
    int mcast_enabled;  
    int is_two_level_allreduce[MV2_MAX_NB_THRESHOLDS];   
    int size_inter_table;
    mv2_allreduce_tuning_element inter_leader[MV2_MAX_NB_THRESHOLDS];
    int size_intra_table;
    mv2_allreduce_tuning_element intra_node[MV2_MAX_NB_THRESHOLDS];
} mv2_allreduce_tuning_table;

extern int mv2_size_allreduce_tuning_table;
extern mv2_allreduce_tuning_table *mv2_allreduce_thresholds_table;
extern int mv2_use_old_allreduce;

/* flat p2p recursive-doubling allreduce */
extern int MPIR_Allreduce_pt2pt_rd_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

/* flat p2p reduce-scatter-allgather allreduce */
extern int MPIR_Allreduce_pt2pt_rs_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Allreduce_mcst_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Allreduce_two_level_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

/* shmem reduce used as the first reduce in allreduce */
extern int MPIR_Allreduce_reduce_shmem_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);
/* p2p reduce used as the first reduce in allreduce */
extern int MPIR_Allreduce_reduce_p2p_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Allreduce_mcst_reduce_two_level_helper_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Allreduce_mcst_reduce_redscat_gather_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag);

/* Architecture detection tuning */
int MV2_set_allreduce_tuning_table();

/* Function to clean free memory allocated by allreduce tuning table*/
void MV2_cleanup_allreduce_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune allreduce thresholds */
int MV2_internode_Allreduce_is_define(char *mv2_user_allreduce_inter, char
                                  *mv2_user_allreduce_intra);
int MV2_intranode_Allreduce_is_define(char *mv2_user_allreduce_intra);
                                           

#endif


