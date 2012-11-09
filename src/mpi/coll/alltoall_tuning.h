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

#ifndef _ALLTOALL_TUNING_
#define _ALLTOALL_TUNING_

#include "coll_shmem.h"
#if defined(_OSU_MVAPICH_)
#ifndef DAPL_DEFAULT_PROVIDER
#include "ibv_param.h"
#else
#include "udapl_param.h"
#endif
#endif                          /* #if defined(_OSU_MVAPICH_) */

#define NMATCH (3+1)

enum {
    ALLTOALL_BRUCK_MV2=0,
    ALLTOALL_RD_MV2,
    ALLTOALL_SCATTER_DEST_MV2, 
    ALLTOALL_PAIRWISE_MV2, 
    ALLTOALL_INPLACE_MV2, 
};


typedef struct {
    int min;
    int max;
    int (*MV2_pt_Alltoall_function) (const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                                     void *recvbuf, int recvcount, MPI_Datatype recvtype,
                                     MPID_Comm *comm_ptr, int *errflag );
} mv2_alltoall_tuning_element;

typedef struct {
    int numproc;
    int size_table;
    mv2_alltoall_tuning_element algo_table[MV2_MAX_NB_THRESHOLDS];
    mv2_alltoall_tuning_element in_place_algo_table[MV2_MAX_NB_THRESHOLDS];
} mv2_alltoall_tuning_table;

extern int mv2_size_alltoall_tuning_table;
extern mv2_alltoall_tuning_table *mv2_alltoall_thresholds_table;
extern int mv2_use_old_alltoall;

extern int MPIR_Alltoall_bruck_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag );

extern int MPIR_Alltoall_RD_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag );


extern int MPIR_Alltoall_Scatter_dest_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag );



extern int MPIR_Alltoall_pairwise_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag );

extern int MPIR_Alltoall_inplace_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag ); 


/* Architecture detection tuning */
int MV2_set_alltoall_tuning_table();

/* Function to clean free memory allocated by bcast tuning table*/
void MV2_cleanup_alltoall_tuning_table();

/* Function used inside ch3_shmem_coll.c to tune bcast thresholds */
int MV2_Alltoall_is_define(char *mv2_user_alltoall);

                                           

#endif

