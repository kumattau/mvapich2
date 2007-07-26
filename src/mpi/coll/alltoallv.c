/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Alltoallv */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Alltoallv = PMPI_Alltoallv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Alltoallv  MPI_Alltoallv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Alltoallv as PMPI_Alltoallv
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Alltoallv
#define MPI_Alltoallv PMPI_Alltoallv
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
int MPIR_Alltoallv ( 
	void *sendbuf, 
	int *sendcnts, 
	int *sdispls, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int *recvcnts, 
	int *rdispls, 
	MPI_Datatype recvtype, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Alltoallv";
    int        comm_size, i;
    MPI_Aint   send_extent, recv_extent;
    int        mpi_errno = MPI_SUCCESS;
    MPI_Status *starray;
    MPI_Request *reqarray;
    int dst, rank, req_cnt;
    MPI_Comm comm;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(sendtype, send_extent);
    MPID_Datatype_get_extent_macro(recvtype, recv_extent);
    
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

    req_cnt = 0;
    for ( i=0; i<comm_size; i++ ) { 
        dst = (rank+i) % comm_size;
        if (recvcnts[dst]) {
            mpi_errno = MPIC_Irecv((char *)recvbuf+rdispls[dst]*recv_extent, 
                                   recvcnts[dst], recvtype, dst,
                                   MPIR_ALLTOALLV_TAG, comm,
                                   &reqarray[req_cnt]);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
            /* --END ERROR HANDLING-- */
            req_cnt++;
        }
    }

    for ( i=0; i<comm_size; i++ ) { 
        dst = (rank+i) % comm_size;
        if (sendcnts[dst]) {
            mpi_errno = MPIC_Isend((char *)sendbuf+sdispls[dst]*send_extent, 
                                   sendcnts[dst], sendtype, dst,
                                   MPIR_ALLTOALLV_TAG, comm,
                                   &reqarray[req_cnt]);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
            /* --END ERROR HANDLING-- */
            req_cnt++;
        }
    }

    mpi_errno = NMPI_Waitall(req_cnt, reqarray, starray);

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
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}
/* end:nested */

/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoallv_inter ( 
    void *sendbuf, 
    int *sendcnts, 
    int *sdispls, 
    MPI_Datatype sendtype, 
    void *recvbuf, 
    int *recvcnts, 
    int *rdispls, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr )
{
/* Intercommunicator alltoallv. We use a pairwise exchange algorithm
   similar to the one used in intracommunicator alltoallv. Since the
   local and remote groups can be of different 
   sizes, we first compute the max of local_group_size,
   remote_group_size. At step i, 0 <= i < max_size, each process
   receives from src = (rank - i + max_size) % max_size if src <
   remote_size, and sends to dst = (rank + i) % max_size if dst <
   remote_size. 

   FIXME: change algorithm to match intracommunicator alltoallv

*/
    static const char FCNAME[] = "MPIR_Alltoallv_inter";
    int local_size, remote_size, max_size, i;
    MPI_Aint   send_extent, recv_extent;
    int        mpi_errno = MPI_SUCCESS;
    MPI_Status status;
    int src, dst, rank, sendcount, recvcount;
    char *sendaddr, *recvaddr;
    MPI_Comm comm;
    
    local_size = comm_ptr->local_size; 
    remote_size = comm_ptr->remote_size;
    comm = comm_ptr->handle;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(sendtype, send_extent);
    MPID_Datatype_get_extent_macro(recvtype, recv_extent);
    
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
        }
        else {
            recvaddr = (char *)recvbuf + rdispls[src]*recv_extent;
            recvcount = recvcnts[src];
        }
        if (dst >= remote_size) {
            dst = MPI_PROC_NULL;
            sendaddr = NULL;
            sendcount = 0;
        }
        else {
            sendaddr = (char *)sendbuf + sdispls[dst]*send_extent;
            sendcount = sendcnts[dst];
        }

        mpi_errno = MPIC_Sendrecv(sendaddr, sendcount, sendtype, dst, 
                                  MPIR_ALLTOALLV_TAG, recvaddr, recvcount, 
                                  recvtype, src, MPIR_ALLTOALLV_TAG,
                                  comm, &status); 
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
/* end:nested */
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Alltoallv

/*@
MPI_Alltoallv - Sends data from all to all processes; each process may 
   send a different amount of data and provide displacements for the input
   and output data.

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcounts - integer array equal to the group size 
specifying the number of elements to send to each processor 
. sdispls - integer array (of length group size). Entry 
 'j'  specifies the displacement (relative to sendbuf  from
which to take the outgoing data destined for process  'j'  
. sendtype - data type of send buffer elements (handle) 
. recvcounts - integer array equal to the group size 
specifying the maximum number of elements that can be received from
each processor 
. rdispls - integer array (of length group size). Entry 
 'i'  specifies the displacement (relative to recvbuf  at
which to place the incoming data from process  'i'  
. recvtype - data type of receive buffer elements (handle) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice) 

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Alltoallv(void *sendbuf, int *sendcnts, int *sdispls, 
                  MPI_Datatype sendtype, void *recvbuf, int *recvcnts, 
                  int *rdispls, MPI_Datatype recvtype, MPI_Comm comm)
{
    static const char FCNAME[] = "MPI_Alltoallv";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_ALLTOALLV);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("coll");
    MPID_MPI_COLL_FUNC_ENTER(MPID_STATE_MPI_ALLTOALLV);

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
                MPIR_ERRTEST_DATATYPE(sendtype, "sendtype", mpi_errno);
                MPIR_ERRTEST_DATATYPE(recvtype, "recvtype", mpi_errno);
	    }
            if (HANDLE_GET_KIND(sendtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype_get_ptr(sendtype, sendtype_ptr);
                MPID_Datatype_valid_ptr( sendtype_ptr, mpi_errno );
                MPID_Datatype_committed_ptr( sendtype_ptr, mpi_errno );
            }
            if (HANDLE_GET_KIND(recvtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype_get_ptr(recvtype, recvtype_ptr);
                MPID_Datatype_valid_ptr( recvtype_ptr, mpi_errno );
                MPID_Datatype_committed_ptr( recvtype_ptr, mpi_errno );
            }

            
            for (i=0; i<comm_size; i++) {
                if (sendcnts[i] > 0) {
                    MPIR_ERRTEST_SENDBUF_INPLACE(sendbuf, sendcnts[i], mpi_errno);
                    MPIR_ERRTEST_USERBUFFER(sendbuf,sendcnts[i],sendtype,mpi_errno); 
                    break;
                }
            }
            for (i=0; i<comm_size; i++) {
                if (recvcnts[i] > 0) {
                    MPIR_ERRTEST_RECVBUF_INPLACE(recvbuf, recvcnts[i], mpi_errno);
                    MPIR_ERRTEST_USERBUFFER(recvbuf,recvcnts[i],recvtype,mpi_errno); 
                    break;
                }
            }

            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    if (comm_ptr->coll_fns != NULL && comm_ptr->coll_fns->Alltoallv != NULL)
    {
	mpi_errno = comm_ptr->coll_fns->Alltoallv(sendbuf, sendcnts, sdispls,
                                                 sendtype, recvbuf, recvcnts,
                                                 rdispls, recvtype, comm_ptr);
    }
    else
    {
	MPIU_THREADPRIV_DECL;
	MPIU_THREADPRIV_GET;

	MPIR_Nest_incr();
        if (comm_ptr->comm_kind == MPID_INTRACOMM) 
            /* intracommunicator */
            mpi_errno = MPIR_Alltoallv(sendbuf, sendcnts, sdispls,
                                       sendtype, recvbuf, recvcnts,
                                       rdispls, recvtype, comm_ptr);
        else {
            /* intercommunicator */
            mpi_errno = MPIR_Alltoallv_inter(sendbuf, sendcnts, sdispls,
                                             sendtype, recvbuf, recvcnts,
                                             rdispls, recvtype, comm_ptr);
        }
	MPIR_Nest_decr();
    }

    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_COLL_FUNC_EXIT(MPID_STATE_MPI_ALLTOALLV);
    MPIU_THREAD_SINGLE_CS_EXIT("coll");
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_alltoallv",
	    "**mpi_alltoallv %p %p %p %D %p %p %p %D %C", sendbuf, sendcnts, sdispls, sendtype,
	    recvbuf, recvcnts, rdispls, recvtype, comm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
