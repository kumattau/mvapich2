/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Gatherv */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Gatherv = PMPI_Gatherv
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Gatherv  MPI_Gatherv
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Gatherv as PMPI_Gatherv
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Gatherv
#define MPI_Gatherv PMPI_Gatherv

/* This is the default implementation of gatherv. The algorithm is:
   
   Algorithm: MPI_Gatherv

   Since the array of recvcounts is valid only on the root, we cannot
   do a tree algorithm without first communicating the recvcounts to
   other processes. Therefore, we simply use a linear algorithm for the
   gather, which takes (p-1) steps versus lgp steps for the tree
   algorithm. The bandwidth requirement is the same for both algorithms.

   Cost = (p-1).alpha + n.((p-1)/p).beta

   Possible improvements: 

   End Algorithm: MPI_Gatherv
*/

/* not declared static because it is called in intercommunicator allgatherv */
int MPIR_Gatherv ( 
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
    static const char FCNAME[] = "MPIR_Gatherv";
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

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Gatherv

/*@

MPI_Gatherv - Gathers into specified locations from all processes in a group

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements in send buffer (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcounts - integer array (of length group size) 
containing the number of elements that are received from each process
(significant only at 'root') 
. displs - integer array (of length group size). Entry 
 'i'  specifies the displacement relative to recvbuf  at
which to place the incoming data from process  'i'  (significant only
at root) 
. recvtype - data type of recv buffer elements 
(significant only at 'root') (handle) 
. root - rank of receiving process (integer) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice, significant only at 'root') 

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Gatherv(void *sendbuf, int sendcnt, MPI_Datatype sendtype, 
                void *recvbuf, int *recvcnts, int *displs, 
                MPI_Datatype recvtype, int root, MPI_Comm comm)
{
    static const char FCNAME[] = "MPI_Gatherv";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPIU_THREADPRIV_DECL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_GATHERV);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_CS_ENTER(ALLFUNC,);
    MPID_MPI_COLL_FUNC_ENTER(MPID_STATE_MPI_GATHERV);

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
            int i, rank, comm_size;
	    
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;

	    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
		MPIR_ERRTEST_INTRA_ROOT(comm_ptr, root, mpi_errno);

                if (sendbuf != MPI_IN_PLACE) {
                    MPIR_ERRTEST_COUNT(sendcnt, mpi_errno);
                    MPIR_ERRTEST_DATATYPE(sendtype, "sendtype", mpi_errno);
                    if (HANDLE_GET_KIND(sendtype) != HANDLE_KIND_BUILTIN) {
                        MPID_Datatype_get_ptr(sendtype, sendtype_ptr);
                        MPID_Datatype_valid_ptr( sendtype_ptr, mpi_errno );
                        MPID_Datatype_committed_ptr( sendtype_ptr, mpi_errno );
                    }
                    MPIR_ERRTEST_USERBUFFER(sendbuf,sendcnt,sendtype,mpi_errno);
                }

                rank = comm_ptr->rank;
                if (rank == root) {
                    comm_size = comm_ptr->local_size;
                    for (i=0; i<comm_size; i++) {
                        MPIR_ERRTEST_COUNT(recvcnts[i], mpi_errno);
                        MPIR_ERRTEST_DATATYPE(recvtype, "recvtype", mpi_errno);
                    }
                    if (HANDLE_GET_KIND(recvtype) != HANDLE_KIND_BUILTIN) {
                        MPID_Datatype_get_ptr(recvtype, recvtype_ptr);
                        MPID_Datatype_valid_ptr( recvtype_ptr, mpi_errno );
                        MPID_Datatype_committed_ptr( recvtype_ptr, mpi_errno );
                    }

                    for (i=0; i<comm_size; i++) {
                        if (recvcnts[i] > 0) {
                            MPIR_ERRTEST_RECVBUF_INPLACE(recvbuf, recvcnts[i], mpi_errno);
                            MPIR_ERRTEST_USERBUFFER(recvbuf,recvcnts[i],recvtype,mpi_errno); 
                            break;
                        }
                    }
                }
                else
                    MPIR_ERRTEST_SENDBUF_INPLACE(sendbuf, sendcnt, mpi_errno);
            }

	    if (comm_ptr->comm_kind == MPID_INTERCOMM) {
		MPIR_ERRTEST_INTER_ROOT(comm_ptr, root, mpi_errno);

                if (root == MPI_ROOT) {
                    comm_size = comm_ptr->remote_size;
                    for (i=0; i<comm_size; i++) {
                        MPIR_ERRTEST_COUNT(recvcnts[i], mpi_errno);
                        MPIR_ERRTEST_DATATYPE(recvtype, "recvtype", mpi_errno);
                    }
                    if (HANDLE_GET_KIND(recvtype) != HANDLE_KIND_BUILTIN) {
                        MPID_Datatype_get_ptr(recvtype, recvtype_ptr);
                        MPID_Datatype_valid_ptr( recvtype_ptr, mpi_errno );
                        MPID_Datatype_committed_ptr( recvtype_ptr, mpi_errno );
                    }
                    for (i=0; i<comm_size; i++) {
                        if (recvcnts[i] > 0) {
                            MPIR_ERRTEST_RECVBUF_INPLACE(recvbuf, recvcnts[i], mpi_errno);
                            MPIR_ERRTEST_USERBUFFER(recvbuf,recvcnts[i],recvtype,mpi_errno); 
                            break;
                        }
                    }
                }
                else if (root != MPI_PROC_NULL) {
                    MPIR_ERRTEST_COUNT(sendcnt, mpi_errno);
                    MPIR_ERRTEST_DATATYPE(sendtype, "sendtype", mpi_errno);
                    if (HANDLE_GET_KIND(sendtype) != HANDLE_KIND_BUILTIN) {
                        MPID_Datatype_get_ptr(sendtype, sendtype_ptr);
                        MPID_Datatype_valid_ptr( sendtype_ptr, mpi_errno );
                        MPID_Datatype_committed_ptr( sendtype_ptr, mpi_errno );
                    }
                    MPIR_ERRTEST_SENDBUF_INPLACE(sendbuf, sendcnt, mpi_errno);
                    MPIR_ERRTEST_USERBUFFER(sendbuf,sendcnt,sendtype,mpi_errno);
                }
	    }

            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    if (comm_ptr->coll_fns != NULL && comm_ptr->coll_fns->Gatherv != NULL)
    {
#if defined(_OSU_MVAPICH_)
     	MPIU_THREADPRIV_GET;
        MPIR_Nest_incr();
#endif
	    mpi_errno = comm_ptr->coll_fns->Gatherv(sendbuf, sendcnt,
                                                sendtype, recvbuf, recvcnts,
                                                displs, recvtype, root,
                                                comm_ptr);  
#if defined(_OSU_MVAPICH_)
        MPIR_Nest_decr();
#endif
    }
    else
    {
	MPIU_THREADPRIV_GET;
        
        MPIR_Nest_incr();
        mpi_errno = MPIR_Gatherv(sendbuf, sendcnt, sendtype, 
                                 recvbuf, recvcnts,
                                 displs, recvtype, root, comm_ptr); 
        MPIR_Nest_decr();
    }

    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

    /* ... end of body of routine ... */
    
  fn_exit:
    MPID_MPI_COLL_FUNC_EXIT(MPID_STATE_MPI_GATHERV);
    MPIU_THREAD_CS_EXIT(ALLFUNC,);
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_gatherv",
	    "**mpi_gatherv %p %d %D %p %p %p %D %d %C", sendbuf, sendcnt, sendtype,
	    recvbuf, recvcnts, displs, recvtype, root, comm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

