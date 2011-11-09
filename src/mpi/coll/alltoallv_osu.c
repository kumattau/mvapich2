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

/* This is the default implementation of alltoallv. The algorithm is:
   
   Algorithm: MPI_Alltoallv

   Since each process sends/receives different amounts of data to
   every other process, we don't know the total message size for all
   processes without additional communication. Therefore we simply use
   the "middle of the road" isend/irecv algorithm that works
   reasonably well in all cases.

   We post all irecvs and isends and then do a waitall. We scatter the
   order of sources and destinations among the processes, so that all
   processes don't try to send/recv to/from the same process at the
   same time. 

   Possible improvements: 

   End Algorithm: MPI_Alltoallv
*/
 
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Alltoallv_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)

int MPIR_Alltoallv_intra_MV2 ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	MPI_Datatype recvtype, 
	MPID_Comm *comm_ptr, 
    int *errflag )
{
    int        comm_size, i, j;
    MPI_Aint   send_extent, recv_extent;
    int        mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int dst, rank;
    MPI_Comm comm;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int pof2, src;
    MPI_Status status;
#else
    MPI_Status *starray;
    MPI_Request *reqarray;
    int req_cnt;
#endif
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(sendtype, send_extent);
    MPID_Datatype_get_extent_macro(recvtype, recv_extent);
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) 
     if (sendbuf == MPI_IN_PLACE) {
        /* We use pair-wise sendrecv_replace in order to conserve memory usage,
         * which is keeping with the spirit of the MPI-2.2 Standard.  But
         * because of this approach all processes must agree on the global
         * schedule of sendrecv_replace operations to avoid deadlock.
         *
         * Note that this is not an especially efficient algorithm in terms of
         * time and there will be multiple repeated malloc/free's rather than
         * maintaining a single buffer across the whole loop.  Something like
         * MADRE is probably the best solution for the MPI_IN_PLACE scenario. */
        for (i = 0; i < comm_size; ++i) {
            /* start inner loop at i to avoid re-exchanging data */
            for (j = i; j < comm_size; ++j) {
                if (rank == i) {
                    /* also covers the (rank == i && rank == j) case */
                    mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + rdispls[j]*recv_extent),
                                                      recvcnts[j], recvtype,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      comm, &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but
                         * continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
                else if (rank == j) {
                    /* same as above with i/j args reversed */
                    mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + rdispls[i]*recv_extent),
                                                      recvcnts[i], recvtype,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      comm, &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but
                         * continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
            }
        }
    } else { 
            mpi_errno = MPIR_Localcopy(((char *)sendbuf +
                                        sdispls[rank]*send_extent),
                                       sendcnts[rank], sendtype,
                                       ((char *)recvbuf +
                                        rdispls[rank]*recv_extent),
                                       recvcnts[rank], recvtype);

            if (mpi_errno)
            {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, 
                                                 FCNAME, __LINE__, MPI_ERR_OTHER, 
                                                 "**fail", 0);
                return mpi_errno;
            }

            /* Is comm_size a power-of-two? */
            i = 1;
            while (i < comm_size)
                i *= 2;
            if (i == comm_size)
                pof2 = 1;
            else
                pof2 = 0;

            /* Do the pairwise exchanges */
            for (i=1; i<comm_size; i++) {
                if (pof2 == 1) {
                    /* use exclusive-or algorithm */
                    src = dst = rank ^ i;
                }
                else {
                    src = (rank - i + comm_size) % comm_size;
                    dst = (rank + i) % comm_size;
                }

                mpi_errno = MPIC_Sendrecv_ft(((char *)sendbuf +
                                           sdispls[dst]*send_extent),
                                          sendcnts[dst], sendtype, dst,
                                          MPIR_ALLTOALL_TAG,
                                          ((char *)recvbuf +
                                           rdispls[src]*recv_extent),
                                          recvcnts[src], recvtype, src,
                                          MPIR_ALLTOALL_TAG, comm, &status, 
                                          errflag);
                 if (mpi_errno) {
                        /* for communication errors, just record the error but
                         * continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                 }


            }
    } 
#else /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */ 

    starray = (MPI_Status *) MPIU_Malloc(2*comm_size*sizeof(MPI_Status));
    /* --BEGIN ERROR HANDLING-- */
    if (!starray) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                          FCNAME, __LINE__, 
                                          MPI_ERR_OTHER, "**nomem", 0 );
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    reqarray = (MPI_Request *) MPIU_Malloc(2*comm_size*sizeof(MPI_Request));
    /* --BEGIN ERROR HANDLING-- */
    if (!reqarray) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                          FCNAME, __LINE__, 
                                          MPI_ERR_OTHER, "**nomem", 0 );
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    req_cnt = 0;
    for ( i=0; i<comm_size; i++ ) {
        dst = (rank+i) % comm_size;
        if (recvcnts[dst]) {
            mpi_errno = MPIC_Irecv_ft((char *)recvbuf+rdispls[dst]*recv_extent,
                                   recvcnts[dst], recvtype, dst,
                                   MPIR_ALLTOALLV_TAG, comm,
                                   &reqarray[req_cnt]);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            req_cnt++;
        }
    }

    for ( i=0; i<comm_size; i++ ) {
        dst = (rank+i) % comm_size;
        if (sendcnts[dst]) {
            mpi_errno = MPIC_Isend_ft((char *)sendbuf+sdispls[dst]*send_extent,
                                   sendcnts[dst], sendtype, dst,
                                   MPIR_ALLTOALLV_TAG, comm,
                                   &reqarray[req_cnt],errflag);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            req_cnt++;
        }
    }

    mpi_errno = MPIC_Waitall_ft(req_cnt, reqarray, starray, errflag);

    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno == MPI_ERR_IN_STATUS) {
        for (i=0; i<req_cnt; i++) {
            if (starray[i].MPI_ERROR != MPI_SUCCESS)
                mpi_errno = starray[i].MPI_ERROR;
        }
    }
    /* --END ERROR HANDLING-- */

    MPIU_Free(reqarray);
    MPIU_Free(starray);
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */ 
    
fn_exit:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    return (mpi_errno);
    
fn_fail:
    goto fn_exit;
}
/* end:nested */

#undef FUNCNAME
#define FUNCNAME MPIR_Alltoallv_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoallv_MV2(void *sendbuf, int *sendcnts, int *sdispls, MPI_Datatype sendtype,
                   void *recvbuf, int *recvcnts, int *rdispls, MPI_Datatype recvtype,
                   MPID_Comm *comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
        /* intracommunicator */
        mpi_errno = MPIR_Alltoallv_intra_MV2(sendbuf, sendcnts, sdispls,
                                         sendtype, recvbuf, recvcnts,
                                         rdispls, recvtype, comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    } else {
        /* intercommunicator */
        mpi_errno = MPIR_Alltoallv_inter(sendbuf, sendcnts, sdispls,
                                         sendtype, recvbuf, recvcnts,
                                         rdispls, recvtype, comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


