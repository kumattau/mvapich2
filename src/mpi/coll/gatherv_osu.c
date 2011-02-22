/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
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


#include "mpiimpl.h"

/* This is the default implementation of gatherv. The algorithm is:
   
   Algorithm: MPI_Gatherv_OSU

   Since the array of recvcounts is valid only on the root, we cannot
   do a tree algorithm without first communicating the recvcounts to
   other processes. Therefore, we simply use a linear algorithm for the
   gather, which takes (p-1) steps versus lgp steps for the tree
   algorithm. The bandwidth requirement is the same for both algorithms.

   Cost = (p-1).alpha + n.((p-1)/p).beta

   Possible improvements: 

   End Algorithm: MPI_Gatherv_OSU
*/

/* not declared static because it is called in intercommunicator allgatherv */
int MPIR_Gatherv_OSU ( 
	void *sendbuf, 
	int sendcnt,  
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *displs, 
	MPI_Datatype recvtype, 
	int root, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Gatherv_OSU";
    int        comm_size, rank;
    int        mpi_errno = MPI_SUCCESS;
    MPI_Comm comm;
    MPI_Aint       extent;
    int            i, reqs;                      
    int min_procs;
    char *min_procs_str;
    MPI_Request *reqarray;
    MPI_Status *starray;
    MPIU_CHKLMEM_DECL(2);
    
    comm = comm_ptr->handle;
    rank = comm_ptr->rank;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
	/* If rank == root, then I recv lots, otherwise I send */
    if (((comm_ptr->comm_kind == MPID_INTRACOMM) && (root == rank)) ||
        ((comm_ptr->comm_kind == MPID_INTERCOMM) && (root == MPI_ROOT))) {
        if (comm_ptr->comm_kind == MPID_INTRACOMM)
            comm_size = comm_ptr->local_size;
        else
            comm_size = comm_ptr->remote_size;

        MPID_Datatype_get_extent_macro(recvtype, extent);
	/* each node can make sure it is not going to overflow aint */
        MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT recvbuf +
					 displs[rank] * extent);

        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, comm_size * sizeof(MPI_Request), mpi_errno, "reqarray");
        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, comm_size * sizeof(MPI_Status), mpi_errno, "starray");

        reqs = 0;
        for (i = 0; i < comm_size; i++) {
            if (recvcnts[i]) {
                if ((comm_ptr->comm_kind == MPID_INTRACOMM) && (i == rank)) {
                    if (sendbuf != MPI_IN_PLACE) {
                        mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
                                                   ((char *)recvbuf+displs[rank]*extent), 
                                                   recvcnts[rank], recvtype);
                    }
                }
                else {
                    mpi_errno = MPIC_Irecv(((char *)recvbuf+displs[i]*extent), 
                                           recvcnts[i], recvtype, i,
                                           MPIR_GATHERV_TAG, comm,
                                           &reqarray[reqs++]);
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
        if (sendcnt) {
            /* we want local size in both the intracomm and intercomm cases
               because the size of the root's group (group A in the standard) is
               irrelevant here. */
            comm_size = comm_ptr->local_size;

            min_procs_str = getenv("MPICH2_GATHERV_MIN_PROCS");
            if (min_procs_str != NULL)
                min_procs = atoi(min_procs_str);
            else
                min_procs = comm_size + 1; /* Disable ssend if env not set */

            if (min_procs == -1)
                min_procs = comm_size + 1; /* Disable ssend */
            else if (min_procs == 0)
                min_procs = MPIR_GATHERV_MIN_PROCS; /* Use the default value */

            if (comm_size >= min_procs) {
                mpi_errno = MPIC_Ssend(sendbuf, sendcnt, sendtype, root, 
                                       MPIR_GATHERV_TAG, comm);
            }
            else {
                mpi_errno = MPIC_Send(sendbuf, sendcnt, sendtype, root, 
                                      MPIR_GATHERV_TAG, comm);
            }
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

