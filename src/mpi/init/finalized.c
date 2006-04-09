/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: finalized.c,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Finalized */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Finalized = PMPI_Finalized
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Finalized  MPI_Finalized
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Finalized as PMPI_Finalized
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#define MPI_Finalized PMPI_Finalized
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Finalized

/*@
   MPI_Finalized - Indicates whether 'MPI_Finalize' has been called.

Output Parameter:
. flag - Flag is true if 'MPI_Finalize' has been called and false otherwise. 
     (logical)

.N SignalSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Finalized( int *flag )
{
    static const char FCNAME[] = "MPI_Finalized";
    int mpi_errno = MPI_SUCCESS;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_FINALIZED);

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_FINALIZED);

#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    /* Should check that flag is not null */
	    if (flag == NULL)
	    {
		mpi_errno = MPI_ERR_ARG;
		goto fn_fail;
	    }
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    *flag = (MPIR_Process.initialized >= MPICH_POST_FINALIZED);
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_FINALIZED);
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
    if (MPIR_Process.initialized == MPICH_WITHIN_MPI)
    { 
	MPID_CS_ENTER();
#       ifdef HAVE_ERROR_CHECKING
	{
	    mpi_errno = MPIR_Err_create_code(
		mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_finalized",
		"**mpi_finalized %p", flag);
	}
#       endif
	
	mpi_errno = MPIR_Err_return_comm( 0, FCNAME, mpi_errno );
	MPID_CS_EXIT();
    }
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
