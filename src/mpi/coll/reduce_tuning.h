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

#ifndef _REDUCE_TUNING_
#define _REDUCE_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif                          /* #if defined(_OSU_MVAPICH_) */

#define NMATCH (3+1)

/* Reduce tuning flags
 * flat binomial: MV2_INTER_REDUCE_TUNING=1
 * flat knomial:  MV2_INTER_REDUCE_TUNING=2 
 *                MV2_USE_INTER_KNOMIAL_REDUCE_FACTOR=?
 * flat reduce-scatter-gather(rsa): MV2_INTER_REDUCE_TUNING=5
 * 2-level: MV2_INTER_REDUCE_TUNING=? MV2_INTRA_REDUCE_TUNING=? 
 *          MV2_USE_INTRA_KNOMIAL_REDUCE_FACTOR=? 
 *          MV2_USE_INTER_KNOMIAL_REDUCE_FACTOR=?
 *          MV2_INTER_REDUCE_TUNING_TWO_LEVEL=1
 *          where intra-reduce flag takes 1(binomial) 2(knomial) 4(shm) 5(rsa) 
 */

typedef struct {
    int min;
    int max;
    int (*MV2_pt_Reduce_function)(const void *sendbuf,
                                 void *recvbuf,
                                 int count,
                                 MPI_Datatype datatype,
                                 MPI_Op op,
                                 int root,
                                 MPID_Comm * comm_ptr, int *errflag);
} mv2_reduce_tuning_element;

typedef struct {
    int numproc; 
    int inter_k_degree;
    int intra_k_degree;
    int is_two_level_reduce[MV2_MAX_NB_THRESHOLDS];
    int size_inter_table;
    mv2_reduce_tuning_element inter_leader[MV2_MAX_NB_THRESHOLDS];
    int size_intra_table;
    mv2_reduce_tuning_element intra_node[MV2_MAX_NB_THRESHOLDS];
} mv2_reduce_tuning_table;

extern int mv2_size_reduce_tuning_table;
extern mv2_reduce_tuning_table *mv2_reduce_thresholds_table;
extern int mv2_use_old_reduce;

extern int MPIR_Reduce_binomial_MV2(const void *sendbuf,
                                          void *recvbuf,
                                          int count,
                                          MPI_Datatype datatype,
                                          MPI_Op op,
                                          int root,
                                          MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Reduce_intra_knomial_wrapper_MV2(const void *sendbuf,
                                   void *recvbuf,
                                   int count,
                                   MPI_Datatype datatype,
                                   MPI_Op op,
                                   int root,
                                   MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Reduce_inter_knomial_wrapper_MV2(const void *sendbuf,
                                   void *recvbuf,
                                   int count,
                                   MPI_Datatype datatype,
                                   MPI_Op op,
                                   int root,
                                   MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Reduce_shmem_MV2(const void *sendbuf,
                                 void *recvbuf,
                                 int count,
                                 MPI_Datatype datatype,
                                 MPI_Op op,
                                 int root,
                                 MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Reduce_redscat_gather_MV2(const void *sendbuf,
                                          void *recvbuf,
                                          int count,
                                          MPI_Datatype datatype,
                                          MPI_Op op,
                                          int root,
                                          MPID_Comm * comm_ptr, int *errflag);
  
extern int MPIR_Reduce_two_level_helper_MV2(const void *sendbuf,
                                            void *recvbuf,
                                            int count,
                                            MPI_Datatype datatype,
                                            MPI_Op op,
                                            int root,
                                            MPID_Comm * comm_ptr, int *errflag);

/* Architecture detection tuning */
int MV2_set_reduce_tuning_table();

/* Function to clean free memory allocated by reduce tuning table*/
void MV2_cleanup_reduce_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune reduce thresholds */
int MV2_internode_Reduce_is_define(char *mv2_user_reduce_inter, char
                                  *mv2_user_reduce_intra);
int MV2_intranode_Reduce_is_define(char *mv2_user_reduce_intra);
                                           

#endif

