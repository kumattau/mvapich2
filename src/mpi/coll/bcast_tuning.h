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

#ifndef _BCAST_TUNING_
#define _BCAST_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif                          /* #if defined(_OSU_MVAPICH_) */

#define NMATCH (3+1)

typedef struct {
    int min;
    int max;
    int (*MV2_pt_Bcast_function) (void *buf, int count, MPI_Datatype datatype,
                                  int root, MPID_Comm * comm_ptr, int *errflag);
} mv2_bcast_tuning_element;

typedef struct {
    int numproc;
    int bcast_segment_size;
    int intra_node_knomial_factor;
    int inter_node_knomial_factor;
    int is_two_level_bcast[MV2_MAX_NB_THRESHOLDS];
    int size_inter_table;
    mv2_bcast_tuning_element inter_leader[MV2_MAX_NB_THRESHOLDS];
    int size_intra_table;
    mv2_bcast_tuning_element intra_node[MV2_MAX_NB_THRESHOLDS];
} mv2_bcast_tuning_table;

extern int mv2_use_pipelined_bcast;
extern int mv2_pipelined_knomial_factor; 
extern int bcast_segment_size;

extern int mv2_size_bcast_tuning_table;
extern mv2_bcast_tuning_table *mv2_bcast_thresholds_table;
extern int mv2_use_old_bcast;

extern int MPIR_Bcast_binomial_MV2(void *buffer,
                                   int count,
                                   MPI_Datatype datatype,
                                   int root, MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Bcast_scatter_doubling_allgather_MV2(void *buffer,
                                                     int count,
                                                     MPI_Datatype datatype,
                                                     int root,
                                                     MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Bcast_scatter_ring_allgather_MV2(void *buffer,
                                                 int count,
                                                 MPI_Datatype datatype,
                                                 int root,
                                                 MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Bcast_scatter_ring_allgather_shm_MV2(void *buffer,
                                                        int count,
                                                        MPI_Datatype datatype,
                                                        int root,
                                                        MPID_Comm * comm_ptr,
                                                        int *errflag);

extern int MPIR_Knomial_Bcast_inter_node_MV2(void *buffer,
                                             int count,
                                             MPI_Datatype datatype,
                                             int root, int knomial_factor,
                                             MPID_Comm * comm_ptr, int *errflag);

extern int MPIR_Knomial_Bcast_inter_node_wrapper_MV2(void *buffer,
                                                     int count,
                                                     MPI_Datatype datatype,
                                                     int root, MPID_Comm * comm_ptr, 
                                                     int *errflag);

extern int MPIR_Pipelined_Bcast_MV2(void *buffer,
                                    int count,
                                    MPI_Datatype datatype,
                                    int root, MPID_Comm * comm_ptr, int *errflag);

/* Use for intra-node in case of two lvl algo */
extern int MPIR_Shmem_Bcast_MV2(void *buffer,
                                int count,
                                MPI_Datatype datatype,
                                int root, MPID_Comm * shmem_comm_ptr, int *errflag);

extern int MPIR_Knomial_Bcast_intra_node_MV2(void *buffer,
                                             int count,
                                             MPI_Datatype datatype,
                                             int root,
                                             MPID_Comm * comm_ptr, int *errflag);

#if defined(_MCST_SUPPORT_)
extern int MPIR_Mcast_inter_node_MV2(void *buffer,
                                     int count,
                                     MPI_Datatype datatype,
                                     int root, MPID_Comm * comm_ptr, int *errflag);
#endif                          /* #if defined(_MCST_SUPPORT_) */

/* Architecture detection tuning */
int MV2_set_bcast_tuning_table();

/* Function to clean free memory allocated by bcast tuning table*/
void MV2_cleanup_bcast_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune bcast thresholds */
int MV2_internode_Bcast_is_define(char *mv2_user_bcast_inter, char
                                  *mv2_user_bcast_intra);
int MV2_intranode_Bcast_is_define(char *mv2_user_bcast_intra);

#endif