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

/* This is the default implementation of alltoallw. The algorithm is:
   
   Algorithm: MPI_Alltoallw

   Since each process sends/receives different amounts of data to
   every other process, we don't know the total message size for all
   processes without additional communication. Therefore we simply use
   the "middle of the road" isend/irecv algorithm that works
   reasonably well in all cases.

   We post all irecvs and isends and then do a waitall. We scatter the
   order of sources and destinations among the processes, so that all
   processes don't try to send/recv to/from the same process at the
   same time. 

   *** Modification: We post only a small number of isends and irecvs 
   at a time and wait on them as suggested by Tony Ladd. ***

   Possible improvements: 

   End Algorithm: MPI_Alltoallw
*/
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoallw_OSU ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	MPI_Datatype *sendtypes, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	MPI_Datatype *recvtypes, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Alltoallw_OSU";
    int        comm_size, i, j;
    int        mpi_errno = MPI_SUCCESS;
    MPI_Status status;
    MPI_Status *starray;
    MPI_Request *reqarray;
    int dst, rank;
    MPI_Comm comm;
    int outstanding_requests;
    int ii, ss, bblock;
    int type_size;

    MPIU_CHKLMEM_DECL(2);
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
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
                    mpi_errno = MPIC_Sendrecv_replace(((char *)recvbuf + rdispls[j]),
                                                      recvcnts[j], recvtypes[j],
                                                      j, MPIR_ALLTOALL_TAG,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      comm, &status);
                    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                }
                else if (rank == j) {
                    /* same as above with i/j args reversed */
                    mpi_errno = MPIC_Sendrecv_replace(((char *)recvbuf + rdispls[i]),
                                                      recvcnts[i], recvtypes[i],
                                                      i, MPIR_ALLTOALL_TAG,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      comm, &status);
                    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                }
            }
        }
    }
    else {
        bblock = MPIR_ALLTOALL_THROTTLE;
        if (bblock == 0) bblock = comm_size;

        MPIU_CHKLMEM_MALLOC(starray,  MPI_Status*,  2*bblock*sizeof(MPI_Status),  mpi_errno, "starray");
        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request*, 2*bblock*sizeof(MPI_Request), mpi_errno, "reqarray");

        /* post only bblock isends/irecvs at a time as suggested by Tony Ladd */
        for (ii=0; ii<comm_size; ii+=bblock) {
            outstanding_requests = 0;
            ss = comm_size-ii < bblock ? comm_size-ii : bblock;

            /* do the communication -- post ss sends and receives: */
            for ( i=0; i<ss; i++ ) { 
                dst = (rank+i+ii) % comm_size;
                if (recvcnts[dst]) {
                    MPID_Datatype_get_size_macro(recvtypes[dst], type_size);
                    if (type_size) {
                        mpi_errno = MPIC_Irecv((char *)recvbuf+rdispls[dst],
                                               recvcnts[dst], recvtypes[dst], dst,
                                               MPIR_ALLTOALLW_TAG, comm,
                                               &reqarray[outstanding_requests]);
                        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

                        outstanding_requests++;
                    }
                }
            }

            for ( i=0; i<ss; i++ ) { 
                dst = (rank-i-ii+comm_size) % comm_size;
                if (sendcnts[dst]) {
                    MPID_Datatype_get_size_macro(sendtypes[dst], type_size);
                    if (type_size) {
                        mpi_errno = MPIC_Isend((char *)sendbuf+sdispls[dst],
                                               sendcnts[dst], sendtypes[dst], dst,
                                               MPIR_ALLTOALLW_TAG, comm,
                                               &reqarray[outstanding_requests]);
                        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

                        outstanding_requests++;
                    }
                }
            }

            mpi_errno = NMPI_Waitall(outstanding_requests, reqarray, starray);

            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno == MPI_ERR_IN_STATUS) {
                for (i=0; i<outstanding_requests; i++) {
                    if (starray[i].MPI_ERROR != MPI_SUCCESS) 
                        mpi_errno = starray[i].MPI_ERROR;
                }
            }
            /* --END ERROR HANDLING-- */   
        }

#ifdef FOO
        /* Use pairwise exchange algorithm. */
        
        /* Make local copy first */
        mpi_errno = MPIR_Localcopy(((char *)sendbuf+sdispls[rank]), 
                                   sendcnts[rank], sendtypes[rank], 
                                   ((char *)recvbuf+rdispls[rank]), 
                                   recvcnts[rank], recvtypes[rank]);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
        {
            mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
            goto fn_fail;
        }
        /* --END ERROR HANDLING-- */
        /* Do the pairwise exchange. */
        for (i=1; i<comm_size; i++) {
            src = (rank - i + comm_size) % comm_size;
            dst = (rank + i) % comm_size;
            mpi_errno = MPIC_Sendrecv(((char *)sendbuf+sdispls[dst]), 
                                      sendcnts[dst], sendtypes[dst], dst,
                                      MPIR_ALLTOALLW_TAG, 
                                      ((char *)recvbuf+rdispls[src]), 
                                      recvcnts[src], recvtypes[dst], src,
                                      MPIR_ALLTOALLW_TAG, comm, &status);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno)
            {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
                goto fn_fail;
            }
            /* --END ERROR HANDLING-- */
        }
#endif
    }

    /* check if multiple threads are calling this collective function */
  fn_exit:
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );  
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}
/* end:nested */

/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoallw_inter_OSU ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	MPI_Datatype *sendtypes, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	MPI_Datatype *recvtypes, 
	MPID_Comm *comm_ptr )
{
/* Intercommunicator alltoallw. We use a pairwise exchange algorithm
   similar to the one used in intracommunicator alltoallw. Since the
   local and remote groups can be of different 
   sizes, we first compute the max of local_group_size,
   remote_group_size. At step i, 0 <= i < max_size, each process
   receives from src = (rank - i + max_size) % max_size if src <
   remote_size, and sends to dst = (rank + i) % max_size if dst <
   remote_size. 

   FIXME: change algorithm to match intracommunicator alltoallv
*/
    static const char FCNAME[] = "MPIR_Alltoallw_inter_OSU";
    int local_size, remote_size, max_size, i;
    int mpi_errno = MPI_SUCCESS;
    MPI_Status status;
    int src, dst, rank, sendcount, recvcount;
    char *sendaddr, *recvaddr;
    MPI_Datatype sendtype, recvtype;
    MPI_Comm comm;
    
    local_size = comm_ptr->local_size; 
    remote_size = comm_ptr->remote_size;
    comm = comm_ptr->handle;
    rank = comm_ptr->rank;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    /* Use pairwise exchange algorithm. */
    max_size = MPIR_MAX(local_size, remote_size);
    for (i=0; i<max_size; i++) {
        src = (rank - i + max_size) % max_size;
        dst = (rank + i) % max_size;
        if (src >= remote_size) {
            src = MPI_PROC_NULL;
            recvaddr = NULL;
            recvcount = 0;
            recvtype = MPI_DATATYPE_NULL;
        }
        else {
            recvaddr = (char *)recvbuf + rdispls[src];
            recvcount = recvcnts[src];
            recvtype = recvtypes[src];
        }
        if (dst >= remote_size) {
            dst = MPI_PROC_NULL;
            sendaddr = NULL;
            sendcount = 0;
            sendtype = MPI_DATATYPE_NULL;
        }
        else {
            sendaddr = (char *)sendbuf+sdispls[dst];
            sendcount = sendcnts[dst];
            sendtype = sendtypes[dst];
        }

        mpi_errno = MPIC_Sendrecv(sendaddr, sendcount, sendtype, 
                                  dst, MPIR_ALLTOALLW_TAG, recvaddr, 
                                  recvcount, recvtype, src,
                                  MPIR_ALLTOALLW_TAG, comm, &status);
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}

