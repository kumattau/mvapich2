/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

/* This is the default implementation of scatterv. The algorithm is:
   
   Algorithm: MPI_Scatterv_OSU

   Since the array of sendcounts is valid only on the root, we cannot
   do a tree algorithm without first communicating the sendcounts to
   other processes. Therefore, we simply use a linear algorithm for the
   scatter, which takes (p-1) steps versus lgp steps for the tree
   algorithm. The bandwidth requirement is the same for both algorithms.

   Cost = (p-1).alpha + n.((p-1)/p).beta

   Possible improvements: 

   End Algorithm: MPI_Scatterv_OSU
*/

/* not declared static because it is called in intercomm. reduce_scatter */
int MPIR_Scatterv_OSU ( 
	void *sendbuf, 
	int *sendcnts, 
	int *displs, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt,  
	MPI_Datatype recvtype, 
	int root, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Scatterv_OSU";
    int rank, comm_size, mpi_errno = MPI_SUCCESS;
    MPI_Comm comm;
    MPI_Aint extent;
    int      i, reqs;
    MPI_Request *reqarray;
    MPI_Status *starray;
    MPIU_CHKLMEM_DECL(2);

    comm = comm_ptr->handle;
    rank = comm_ptr->rank;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
    /* If I'm the root, then scatter */
    if (((comm_ptr->comm_kind == MPID_INTRACOMM) && (root == rank)) ||
        ((comm_ptr->comm_kind == MPID_INTERCOMM) && (root == MPI_ROOT))) {
        if (comm_ptr->comm_kind == MPID_INTRACOMM)
            comm_size = comm_ptr->local_size;
        else
            comm_size = comm_ptr->remote_size;

        MPID_Datatype_get_extent_macro(sendtype, extent);
        /* We need a check to ensure extent will fit in a
         * pointer. That needs extent * (max count) but we can't get
         * that without looping over the input data. This is at least
         * a minimal sanity check. Maybe add a global var since we do
         * loop over sendcount[] in MPI_Scatterv before calling
         * this? */
        MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT sendbuf + extent);

        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, comm_size * sizeof(MPI_Request), mpi_errno, "reqarray");
        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, comm_size * sizeof(MPI_Status), mpi_errno, "starray");

        reqs = 0;
        for (i = 0; i < comm_size; i++) {
            if (sendcnts[i]) {
                if ((comm_ptr->comm_kind == MPID_INTRACOMM) && (i == rank)) {
                    if (recvbuf != MPI_IN_PLACE) {
                        mpi_errno = MPIR_Localcopy(((char *)sendbuf+displs[rank]*extent), 
                                                   sendcnts[rank], sendtype, 
                                                   recvbuf, recvcnt, recvtype);
                    }
                }
                else {
                    mpi_errno = MPIC_Isend(((char *)sendbuf+displs[i]*extent), 
                                           sendcnts[i], sendtype, i,
                                           MPIR_SCATTERV_TAG, comm, &reqarray[reqs++]);
                }
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno) {
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
            }
        }
        /* ... then wait for *all* of them to finish: */
        mpi_errno = NMPI_Waitall(reqs, reqarray, starray);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < reqs; i++) {
                if (starray[i].MPI_ERROR != MPI_SUCCESS)
                    mpi_errno = starray[i].MPI_ERROR;
            }
        }
        /* --END ERROR HANDLING-- */
    }

    else if (root != MPI_PROC_NULL) { /* non-root nodes, and in the intercomm. case, non-root nodes on remote side */
        if (recvcnt) {
            mpi_errno = MPIC_Recv(recvbuf,recvcnt,recvtype,root,
                                  MPIR_SCATTERV_TAG,comm,MPI_STATUS_IGNORE);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno) {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        }
    }
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
fn_fail:
    goto fn_exit;
}
