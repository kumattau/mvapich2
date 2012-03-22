/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2003-2012, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "gather_tuning.h"

int (*MV2_Gather_intra_node_function) (void *sendbuf,
                                       int sendcnt,
                                       MPI_Datatype sendtype,
                                       void *recvbuf,
                                       int recvcnt,
                                       MPI_Datatype recvtype,
                                       int root, MPID_Comm * comm_ptr, int *errflag) =
    NULL;

int (*MV2_Gather_inter_leader_function) (void *sendbuf,
                                         int sendcnt,
                                         MPI_Datatype sendtype,
                                         void *recvbuf,
                                         int recvcnt,
                                         MPI_Datatype recvtype,
                                         int root, MPID_Comm * comm_ptr, int *errflag) =
    NULL;

#undef FUNCNAME
#define FUNCNAME MPIR_Gather_MV2_Direct
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Gather_MV2_Direct(void *sendbuf,
                                  int sendcnt,
                                  MPI_Datatype sendtype,
                                  void *recvbuf,
                                  int recvcnt,
                                  MPI_Datatype recvtype,
                                  int root, MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Aint extent = 0;        /* Datatype extent */
    MPI_Comm comm;
    int reqs = 0, i = 0;
    MPI_Request *reqarray;
    MPI_Status *starray;
    MPIU_CHKLMEM_DECL(2);

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (((rank == root) && (recvcnt == 0)) ||
        ((rank != root) && (sendcnt == 0))) {
        return MPI_SUCCESS;
    }

    if (root == rank) {
        comm_size = comm_ptr->local_size;

        MPID_Datatype_get_extent_macro(recvtype, extent);
        /* each node can make sure it is not going to overflow aint */

        MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT
                                         recvbuf +
                                         (extent * recvcnt * comm_size));

        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *,
                            comm_size * sizeof (MPI_Request),
                            mpi_errno, "reqarray");
        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *,
                            comm_size * sizeof (MPI_Status),
                            mpi_errno, "starray");

        reqs = 0;
        for (i = 0; i < comm_size; i++) {
            if (i == rank) {
                if (sendbuf != MPI_IN_PLACE) {
                    mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
                                               ((char *) recvbuf +
                                                rank * recvcnt * extent),
                                               recvcnt, recvtype);
                }
            } else {
                mpi_errno = MPIC_Irecv_ft(((char *) recvbuf +
                                           i * recvcnt * extent),
                                          recvcnt, recvtype, i,
                                          MPIR_GATHER_TAG, comm,
                                          &reqarray[reqs++]);

            }
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno) {
                mpi_errno = MPIR_Err_create_code(mpi_errno,
                                                 MPIR_ERR_RECOVERABLE,
                                                 FCNAME,
                                                 __LINE__, MPI_ERR_OTHER,
                                                 "**fail", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        }
        /* ... then wait for *all* of them to finish: */
        mpi_errno = MPIC_Waitall_ft(reqs, reqarray, starray, errflag);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < reqs; i++) {
                if (starray[i].MPI_ERROR != MPI_SUCCESS) {
                    mpi_errno = starray[i].MPI_ERROR;
                    if (mpi_errno) {
                        /* for communication errors, just record 
                           the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
            }
        }
        /* --END ERROR HANDLING-- */
    }

    else if (root != rank) {    /* non-root nodes proceses */
        if (sendcnt) {
            comm_size = comm_ptr->local_size;
            if (sendbuf != MPI_IN_PLACE) {
                mpi_errno = MPIC_Send_ft(sendbuf, sendcnt, sendtype, root,
                                         MPIR_GATHER_TAG, comm, errflag);
            } else {
                mpi_errno = MPIC_Send_ft(recvbuf, sendcnt, sendtype, root,
                                         MPIR_GATHER_TAG, comm, errflag);
            }
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    }
  fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIU_CHKLMEM_FREEALL();

    return (mpi_errno);
}

#undef FUNCNAME
#define FUNCNAME MPIR_Gather_MV2_two_level_Direct
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Gather_MV2_two_level_Direct(void *sendbuf,
                                            int sendcnt,
                                            MPI_Datatype sendtype,
                                            void *recvbuf,
                                            int recvcnt,
                                            MPI_Datatype recvtype,
                                            int root,
                                            MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int local_rank, local_size;
    int leader_comm_rank = -1, leader_comm_size = 0;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int recvtype_size = 0, sendtype_size = 0, nbytes;
    void *tmp_buf = NULL;
    void *leader_gather_buf = NULL;
    MPI_Status status;
    MPI_Aint sendtype_extent = 0, recvtype_extent = 0;  /* Datatype extent */
    MPI_Aint true_lb, sendtype_true_extent, recvtype_true_extent;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    int leader_root, leader_of_root;
    MPI_Comm shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr, *leader_commptr = NULL;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (((rank == root) && (recvcnt == 0)) ||
        ((rank != root) && (sendcnt == 0))) {
        return MPI_SUCCESS;
    }

    if (sendtype != MPI_DATATYPE_NULL) {
        MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);
        MPIR_Type_get_true_extent_impl(sendtype, &true_lb,
                                       &sendtype_true_extent);
    }
    if (recvtype != MPI_DATATYPE_NULL) {
        MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        MPIR_Type_get_true_extent_impl(recvtype, &true_lb,
                                       &recvtype_true_extent);
    }

    /* extract the rank,size information for the intra-node
     * communicator */
    shmem_comm = comm_ptr->ch.shmem_comm;
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;

    if (local_rank == 0) {
        /* Node leader. Extract the rank, size information for the leader
         * communicator */
        leader_comm = comm_ptr->ch.leader_comm;
        MPID_Comm_get_ptr(leader_comm, leader_commptr);
        leader_comm_rank = leader_commptr->rank;
        leader_comm_size = leader_commptr->local_size;
    }

    if (rank == root) {
        nbytes = recvcnt * recvtype_size;
    } else {
        nbytes = sendcnt * sendtype_size;
    }

    /* First do the intra-node gather */
    if (local_rank == 0) {
        /* Node leader, allocate tmp_buffer */
        if (rank == root) {
            tmp_buf = MPIU_Malloc(recvcnt * MPIR_MAX(recvtype_extent,
                                                     recvtype_true_extent) *
                                  local_size);
        } else {
            tmp_buf = MPIU_Malloc(sendcnt * MPIR_MAX(sendtype_extent,
                                                     sendtype_true_extent) *
                                  local_size);
        }
        if (tmp_buf == NULL) {
            mpi_errno = MPIR_Err_create_code(MPI_SUCCESS,
                                             MPIR_ERR_RECOVERABLE,
                                             FCNAME, __LINE__, MPI_ERR_OTHER,
                                             "**nomem", 0);
            return mpi_errno;
        }
    }

 /* Ok, lets first do the intra-node gather */
    if (rank == root && sendbuf == MPI_IN_PLACE) {
        mpi_errno = MV2_Gather_intra_node_function(recvbuf +
                                                   rank * recvcnt * recvtype_extent,
                                                   recvcnt, recvtype, tmp_buf, nbytes,
                                                   MPI_BYTE, 0, shmem_commptr, errflag);
    } else {
        mpi_errno = MV2_Gather_intra_node_function(sendbuf, sendcnt, sendtype,
                                                   tmp_buf, nbytes, MPI_BYTE,
                                                   0, shmem_commptr, errflag);
    }
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

    leader_of_root = comm_ptr->ch.leader_map[root];
    /* leader_of_root is the global rank of the leader of the root */
    leader_root = comm_ptr->ch.leader_rank[leader_of_root];
    /* leader_root is the rank of the leader of the root in leader_comm. 
     * leader_root is to be used as the root of the inter-leader gather ops 
     */
    if (comm_ptr->ch.is_uniform != 1) {
        if (local_rank == 0) {
            int *displs = NULL;
            int *recvcnts = NULL;
            int *node_sizes;
            int i = 0;
            /* Node leaders have all the data. But, different nodes can have
             * different number of processes. Do a Gather first to get the 
             * buffer lengths at each leader, followed by a Gatherv to move
             * the actual data */

            if (leader_comm_rank == leader_root && root != leader_of_root) {
                /* The root of the Gather operation is not a node-level 
                 * leader and this process's rank in the leader_comm 
                 * is the same as leader_root */
                leader_gather_buf = MPIU_Malloc(recvcnt *
                                                MPIR_MAX(recvtype_extent,
                                                         recvtype_true_extent) *
                                                comm_size);
                if (leader_gather_buf == NULL) {
                    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS,
                                                     MPIR_ERR_RECOVERABLE,
                                                     FCNAME, __LINE__,
                                                     MPI_ERR_OTHER,
                                                     "**nomem", 0);
                    return mpi_errno;
                }
            }

            node_sizes = comm_ptr->ch.node_sizes;

            if (leader_comm_rank == leader_root) {
                displs = MPIU_Malloc(sizeof (int) * leader_comm_size);
                recvcnts = MPIU_Malloc(sizeof (int) * leader_comm_size);
                if (!displs || !recvcnts) {
                    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS,
                                                     MPIR_ERR_RECOVERABLE,
                                                     FCNAME, __LINE__,
                                                     MPI_ERR_OTHER,
                                                     "**nomem", 0);
                    return mpi_errno;
                }
            }

            if (root == leader_of_root) {
                /* The root of the gather operation is also the node 
                 * leader. Receive into recvbuf and we are done */
                if (leader_comm_rank == leader_root) {
                    recvcnts[0] = node_sizes[0] * recvcnt;
                    displs[0] = 0;

                    for (i = 1; i < leader_comm_size; i++) {
                        displs[i] = displs[i - 1] + node_sizes[i - 1] * recvcnt;
                        recvcnts[i] = node_sizes[i] * recvcnt;
                    }
                } 
                mpi_errno = MPIR_Gatherv(tmp_buf,
                                         local_size * nbytes,
                                         MPI_BYTE, recvbuf, recvcnts,
                                         displs, recvtype,
                                         leader_root, leader_commptr, errflag);
            } else {
                /* The root of the gather operation is not the node leader. 
                 * Receive into leader_gather_buf and then send 
                 * to the root */
                if (leader_comm_rank == leader_root) {
                    recvcnts[0] = node_sizes[0] * nbytes;
                    displs[0] = 0;

                    for (i = 1; i < leader_comm_size; i++) {
                        displs[i] = displs[i - 1] + node_sizes[i - 1] * nbytes;
                        recvcnts[i] = node_sizes[i] * nbytes;
                    }
                } 
                mpi_errno = MPIR_Gatherv(tmp_buf, local_size * nbytes,
                                         MPI_BYTE, leader_gather_buf,
                                         recvcnts, displs, MPI_BYTE,
                                         leader_root, leader_commptr, errflag);
            }
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
            if (leader_comm_rank == leader_root) {
                MPIU_Free(displs);
                MPIU_Free(recvcnts);
            }
        }
    } else {
        /* All nodes have the same number of processes. 
         * Just do one Gather to get all 
         * the data at the leader of the root process */
        if (local_rank == 0) {
            if (leader_comm_rank == leader_root && root != leader_of_root) {
                /* The root of the Gather operation is not a node-level leader
                 */
                leader_gather_buf = MPIU_Malloc(nbytes * comm_size);
                if (leader_gather_buf == NULL) {
                    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS,
                                                     MPIR_ERR_RECOVERABLE,
                                                     FCNAME, __LINE__,
                                                     MPI_ERR_OTHER,
                                                     "**nomem", 0);
                    return mpi_errno;
                }
            }
            if (root == leader_of_root) {
                mpi_errno = MPIR_Gather_MV2_Direct(tmp_buf,
                                                   nbytes * local_size,
                                                   MPI_BYTE, recvbuf,
                                                   recvcnt * local_size,
                                                   recvtype, leader_root,
                                                   leader_commptr, errflag);
            } else {
                mpi_errno = MPIR_Gather_MV2_Direct(tmp_buf, nbytes * local_size,
                                                   MPI_BYTE, leader_gather_buf,
                                                   nbytes * local_size,
                                                   MPI_BYTE, leader_root,
                                                   leader_commptr, errflag);
            }
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
        }
    }
    if ((local_rank == 0) && (root != rank)
        && (leader_of_root == rank)) {
        mpi_errno = MPIC_Send_ft(leader_gather_buf,
                                 nbytes * comm_size, MPI_BYTE,
                                 root, MPIR_GATHER_TAG, comm, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error 
               but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

    if (rank == root && local_rank != 0) {
        /* The root of the gather operation is not the node leader. Receive
         * data from the node leader */
        mpi_errno = MPIC_Recv_ft(recvbuf, recvcnt * comm_size, recvtype,
                                 leader_of_root, MPIR_GATHER_TAG, comm,
                                 &status, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but 
               continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

  fn_fail:
    /* check if multiple threads are calling this collective function */
    if (local_rank == 0) {
        if (tmp_buf != NULL) {
            MPIU_Free(tmp_buf);
        }
        if (leader_gather_buf != NULL) {
            MPIU_Free(leader_gather_buf);
        }
    }

    return (mpi_errno);
}
#endif                          /* #if defined(_OSU_MVAPICH_)  || defined(_OSU_PSM_) */

#undef FUNCNAME
#define FUNCNAME MPIR_Gather_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Gather_MV2(void *sendbuf,
                    int sendcnt,
                    MPI_Datatype sendtype,
                    void *recvbuf,
                    int recvcnt,
                    MPI_Datatype recvtype,
                    int root, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int range = 0;
    int range_threshold = 0;
    int nbytes = 0;
    int comm_size = 0;
    int recvtype_size, sendtype_size;
    int rank = -1;
#endif                          /* #if defined(_OSU_MVAPICH_) */
    MPIU_THREADPRIV_DECL;

    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
#endif                          /* #if defined(_OSU_MVAPICH_) */

    MPIU_THREADPRIV_GET;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if (rank == root) {
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        nbytes = recvcnt * recvtype_size;
    } else {
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);
        nbytes = sendcnt * sendtype_size;
    }
    /* Search for the corresponding system size inside the tuning table */
    while ((range < (mv2_size_gather_tuning_table - 1)) &&
           (comm_size > mv2_gather_thresholds_table[range].numproc)) {
        range++;
    }
    /* Search for corresponding inter-leader function */
    while ((range_threshold < (mv2_gather_thresholds_table[range].size_inter_table - 1))
           && (nbytes >
               mv2_gather_thresholds_table[range].inter_leader[range_threshold].max)
           && (mv2_gather_thresholds_table[range].inter_leader[range_threshold].max !=
               -1)) {
        range_threshold++;
    }
#ifdef _ENABLE_CUDA_
   MPI_Aint sendtype_extent;
   MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
   int recvtype_extent = 0;
   MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
   int send_mem_type = 0;
   int recv_mem_type = 0;
   if (rdma_enable_cuda) {
       send_mem_type = is_device_buffer(sendbuf);
       recv_mem_type = is_device_buffer(recvbuf);
   }
   if (rdma_enable_cuda && (send_mem_type || recv_mem_type) &&
       rdma_cuda_use_naive && (nbytes <= rdma_cuda_gather_naive_limit/comm_size)) {
       if (sendbuf != MPI_IN_PLACE) {
            if (rank == root) {
                mpi_errno = cuda_stage_alloc (NULL, 0,
                          &recvbuf, recvcnt*recvtype_extent*comm_size, 
                          0, recv_mem_type, 
                          0);
            } else {
                mpi_errno = cuda_stage_alloc (&sendbuf, sendcnt*sendtype_extent,
                          NULL, 0, 
                          send_mem_type, 0, 
                          0);
            }
       } else {
            mpi_errno = cuda_stage_alloc (&sendbuf, recvcnt*recvtype_extent,
                      &recvbuf, recvcnt*recvtype_extent*comm_size, 
                      0, recv_mem_type, 
                      rank*recvcnt*recvtype_extent);
       }
       if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
       }
   }


    /* Use Direct algorithm in cuda configuration */
    if (rdma_enable_cuda && (((nbytes > rdma_cuda_gather_naive_limit/comm_size) &&
        rdma_cuda_use_naive) || !rdma_cuda_use_naive)) {
        mpi_errno = MPIR_Gather_MV2_Direct(sendbuf, sendcnt,
                                           sendtype, recvbuf, recvcnt, recvtype,
                                           root, comm_ptr, errflag);
    } else
#endif /*_ENABLE_CUDA_*/

    if (comm_ptr->ch.is_global_block == 1 && mv2_use_direct_gather == 1 &&
            mv2_use_two_level_gather == 1 && comm_ptr->ch.shmem_coll_ok == 1) {
        /* Set intra-node function pt for gather_two_level */
        MV2_Gather_intra_node_function =
            mv2_gather_thresholds_table[range].intra_node[0].
            MV2_pt_Gather_function;
        /* Set inter-leader pt */
        MV2_Gather_inter_leader_function =
            mv2_gather_thresholds_table[range].inter_leader[range_threshold].
            MV2_pt_Gather_function;
        /* We call Gather function */
        mpi_errno =
            MV2_Gather_inter_leader_function(sendbuf, sendcnt, sendtype, recvbuf, recvcnt,
                                             recvtype, root, comm_ptr, errflag);

    } else {
#endif                          /* #if defined(_OSU_MVAPICH_) */
        mpi_errno = MPIR_Gather_intra(sendbuf, sendcnt, sendtype,
                                      recvbuf, recvcnt, recvtype,
                                      root, comm_ptr, errflag);
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    }

#ifdef _ENABLE_CUDA_ 
    if (rdma_enable_cuda && (send_mem_type || recv_mem_type) &&
        rdma_cuda_use_naive && (nbytes <= rdma_cuda_gather_naive_limit/comm_size)){
        if (rank == root) {
            cuda_stage_free (NULL, 
                        &recvbuf, recvcnt*recvtype_extent*comm_size,
                        0, recv_mem_type);
        } else {
            cuda_stage_free (&sendbuf, 
                        NULL, 0,
                        send_mem_type, 0);
        }
    }
#endif                          /*#ifdef _ENABLE_CUDA_*/     

#endif                          /* #if defined(_OSU_MVAPICH_) */
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

/* end:nested */
