/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Alltoallw */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Alltoallw = PMPI_Alltoallw
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Alltoallw  MPI_Alltoallw
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Alltoallw as PMPI_Alltoallw
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Alltoallw
#define MPI_Alltoallw PMPI_Alltoallw
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

   Possible improvements: 

   End Algorithm: MPI_Alltoallw
*/
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoallw ( 
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
    static const char FCNAME[] = "MPIR_Alltoallw";
    int        comm_size, i;
    int        mpi_errno = MPI_SUCCESS;
    MPI_Status *starray;
    MPI_Request *reqarray;
    int dst, rank;
    MPI_Comm comm;
    int outstanding_requests;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    starray = (MPI_Status *) MPIU_Malloc(2*comm_size*sizeof(MPI_Status));
    /* --BEGIN ERROR HANDLING-- */
    if (!starray) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    reqarray = (MPI_Request *) MPIU_Malloc(2*comm_size*sizeof(MPI_Request));
    /* --BEGIN ERROR HANDLING-- */
    if (!reqarray) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    outstanding_requests = 0;
    for ( i=0; i<comm_size; i++ ) { 
        dst = (rank+i) % comm_size;
	if (recvcnts[dst]) {
	    mpi_errno = MPIC_Irecv((char *)recvbuf+rdispls[dst], 
				   recvcnts[dst], recvtypes[dst], dst,
				   MPIR_ALLTOALLW_TAG, comm,
				   &reqarray[outstanding_requests]);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */

	    outstanding_requests++;
	}
    }

    for ( i=0; i<comm_size; i++ ) { 
        dst = (rank+i) % comm_size;
	if (sendcnts[dst]) {
	    mpi_errno = MPIC_Isend((char *)sendbuf+sdispls[dst], 
				   sendcnts[dst], sendtypes[dst], dst,
				   MPIR_ALLTOALLW_TAG, comm,
				   &reqarray[outstanding_requests]);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */

	    outstanding_requests++;
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
    
    MPIU_Free(reqarray);
    MPIU_Free(starray);


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
	return mpi_errno;
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
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }
#endif
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}
/* end:nested */

/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoallw_inter ( 
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
    static const char FCNAME[] = "MPIR_Alltoallw_inter";
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

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Alltoallw

/*@
   MPI_Alltoallw - Generalized all-to-all communication allowing different
   datatypes, counts, and displacements for each partner

   Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcounts - integer array equal to the group size specifying the number of 
  elements to send to each processor (integer) 
. sdispls - integer array (of length group size). Entry j specifies the 
  displacement in bytes (relative to sendbuf) from which to take the outgoing 
  data destined for process j 
. sendtypes - array of datatypes (of length group size). Entry j specifies the 
  type of data to send to process j (handle) 
. recvcounts - integer array equal to the group size specifying the number of
   elements that can be received from each processor (integer) 
. rdispls - integer array (of length group size). Entry i specifies the 
  displacement in bytes (relative to recvbuf) at which to place the incoming 
  data from process i 
. recvtypes - array of datatypes (of length group size). Entry i specifies 
  the type of data received from process i (handle) 
- comm - communicator (handle) 

 Output Parameter:
. recvbuf - address of receive buffer (choice) 

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_ARG
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
@*/
int MPI_Alltoallw(void *sendbuf, int *sendcnts, int *sdispls, 
                  MPI_Datatype *sendtypes, void *recvbuf, int *recvcnts, 
                  int *rdispls, MPI_Datatype *recvtypes, MPI_Comm comm)
{
    static const char FCNAME[] = "MPI_Alltoallw";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_ALLTOALLW);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("coll");
    MPID_MPI_COLL_FUNC_ENTER(MPID_STATE_MPI_ALLTOALLW);

    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm, mpi_errno);
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr( comm, comm_ptr );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPID_Datatype *sendtype_ptr=NULL, *recvtype_ptr=NULL;
            int i, comm_size;
	    
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;

            if (comm_ptr->comm_kind == MPID_INTRACOMM) 
                comm_size = comm_ptr->local_size;
            else
                comm_size = comm_ptr->remote_size;

            for (i=0; i<comm_size; i++) {
                MPIR_ERRTEST_COUNT(sendcnts[i], mpi_errno);
                MPIR_ERRTEST_COUNT(recvcnts[i], mpi_errno);
                MPIR_ERRTEST_DATATYPE(sendtypes[i], "sendtype[i]", mpi_errno);
                MPIR_ERRTEST_DATATYPE(recvtypes[i], "recvtype[i]", mpi_errno);

                if (HANDLE_GET_KIND(sendtypes[i]) != HANDLE_KIND_BUILTIN) {
                    MPID_Datatype_get_ptr(sendtypes[i], sendtype_ptr);
                    MPID_Datatype_valid_ptr( sendtype_ptr, mpi_errno );
                    MPID_Datatype_committed_ptr( sendtype_ptr, mpi_errno );
                }
                if (HANDLE_GET_KIND(recvtypes[i]) != HANDLE_KIND_BUILTIN) {
                    MPID_Datatype_get_ptr(recvtypes[i], recvtype_ptr);
                    MPID_Datatype_valid_ptr( recvtype_ptr, mpi_errno );
                    MPID_Datatype_committed_ptr( recvtype_ptr, mpi_errno );
                }
            }

            for (i=0; i<comm_size; i++) {
                if (sendcnts[i] > 0) {
                    MPIR_ERRTEST_SENDBUF_INPLACE(sendbuf, sendcnts[i], mpi_errno);
                    MPIR_ERRTEST_USERBUFFER(sendbuf,sendcnts[i],sendtypes[i],mpi_errno); 
                    break;
                }
            }
            for (i=0; i<comm_size; i++) {
                if (recvcnts[i] > 0) {
                    MPIR_ERRTEST_RECVBUF_INPLACE(recvbuf, recvcnts[i], mpi_errno);
                    MPIR_ERRTEST_USERBUFFER(recvbuf,recvcnts[i],recvtypes[i],mpi_errno); 
                    break;
                }
            }

            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    if (comm_ptr->coll_fns != NULL && comm_ptr->coll_fns->Alltoallw != NULL)
    {
	mpi_errno = comm_ptr->coll_fns->Alltoallw(sendbuf, sendcnts, sdispls,
                                                 sendtypes, recvbuf, recvcnts,
                                                 rdispls, recvtypes, comm_ptr);
    }
    else
    {
	MPIU_THREADPRIV_DECL;
	MPIU_THREADPRIV_GET;

	MPIR_Nest_incr();
        if (comm_ptr->comm_kind == MPID_INTRACOMM) 
            /* intracommunicator */
            mpi_errno = MPIR_Alltoallw(sendbuf, sendcnts, sdispls,
                                       sendtypes, recvbuf, recvcnts,
                                       rdispls, recvtypes, comm_ptr);
        else {
            /* intercommunicator */
            mpi_errno = MPIR_Alltoallw_inter(sendbuf, sendcnts, sdispls,
                                       sendtypes, recvbuf, recvcnts,
                                       rdispls, recvtypes, comm_ptr);
        }
	MPIR_Nest_decr();
    }

    /* ... end of body of routine ... */
    
    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

  fn_exit:
    MPID_MPI_COLL_FUNC_EXIT(MPID_STATE_MPI_ALLTOALLW);
    MPIU_THREAD_SINGLE_CS_EXIT("coll");
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_alltoallw",
	    "**mpi_alltoallw %p %p %p %p %p %p %p %p %C", sendbuf, sendcnts, sdispls, sendtypes,
	    recvbuf, recvcnts, rdispls, recvtypes, comm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
