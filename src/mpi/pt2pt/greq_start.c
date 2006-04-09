/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: greq_start.c,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Grequest_start */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Grequest_start = PMPI_Grequest_start
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Grequest_start  MPI_Grequest_start
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Grequest_start as PMPI_Grequest_start
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines.  You can use USE_WEAK_SYMBOLS to see if MPICH is
   using weak symbols to implement the MPI routines. */
#ifndef MPICH_MPI_FROM_PMPI
#define MPI_Grequest_start PMPI_Grequest_start

/* Any internal routines can go here.  Make them static if possible.  If they
   are used by both the MPI and PMPI versions, use PMPI_LOCAL instead of 
   static; this macro expands into "static" if weak symbols are supported and
   into nothing otherwise. */
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Grequest_start

/*@
   MPI_Grequest_start - Create and return a user-defined request

Input Parameters:
+ query_fn - callback function invoked when request status is queried (function)  
. free_fn - callback function invoked when request is freed (function) 
. cancel_fn - callback function invoked when request is cancelled (function) 
- extra_state - Extra state passed to the above functions.

Output Parameter:
.  request - Generalized request (handle)

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_ARG
@*/
int MPI_Grequest_start( MPI_Grequest_query_function *query_fn, 
			MPI_Grequest_free_function *free_fn, 
			MPI_Grequest_cancel_function *cancel_fn, 
			void *extra_state, MPI_Request *request )
{
    static const char FCNAME[] = "MPI_Grequest_start";
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *lrequest_ptr;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_GREQUEST_START);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPID_CS_ENTER();
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_GREQUEST_START);

    /* Validate parameters if error checking is enabled */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_ARGNULL(request,"request",mpi_errno);
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    lrequest_ptr = MPID_Request_create();
    /* --BEGIN ERROR HANDLING-- */
    if (lrequest_ptr == NULL)
    {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
					  FCNAME, __LINE__, MPI_ERR_OTHER, 
					  "**nomem", "**nomem %s", 
					  "generalized request" );
	goto fn_fail;
    }
    /* --END ERROR HANDLING-- */
    
    lrequest_ptr->kind                 = MPID_UREQUEST;
    MPIU_Object_set_ref( lrequest_ptr, 1 );
    lrequest_ptr->cc_ptr               = &lrequest_ptr->cc;
    lrequest_ptr->cc                   = 1;
    lrequest_ptr->comm                 = NULL;
    lrequest_ptr->cancel_fn            = cancel_fn;
    lrequest_ptr->free_fn              = free_fn;
    lrequest_ptr->query_fn             = query_fn;
    lrequest_ptr->grequest_extra_state = extra_state;
    lrequest_ptr->greq_lang            = MPID_LANG_C;
    *request = lrequest_ptr->handle;
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_GREQUEST_START);
    MPID_CS_EXIT();
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, 
	    "**mpi_grequest_start",
	    "**mpi_grequest_start %p %p %p %p %p", 
	    query_fn, free_fn, cancel_fn, extra_state, request);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( NULL, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
