/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: info_create.c,v 1.20 2006/05/08 15:55:47 toonen Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpiinfo.h"

/* -- Begin Profiling Symbol Block for routine MPI_Info_create */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Info_create = PMPI_Info_create
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Info_create  MPI_Info_create
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Info_create as PMPI_Info_create
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Info_create
#define MPI_Info_create PMPI_Info_create
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Info_create

/*@
    MPI_Info_create - Creates a new info object

   Output Parameter:
. info - info object created (handle)

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_OTHER
@*/
int MPI_Info_create( MPI_Info *info )
{
    MPID_Info *info_ptr;
    static const char FCNAME[] = "MPI_Info_create";
    int mpi_errno = MPI_SUCCESS;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_INFO_CREATE);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("info");
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_INFO_CREATE);

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            MPIR_ERRTEST_ARGNULL(info, "info", mpi_errno);
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    info_ptr = (MPID_Info *)MPIU_Handle_obj_alloc( &MPID_Info_mem );
    MPIU_ERR_CHKANDJUMP1((!info_ptr), mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPI_Info");

    *info	     = info_ptr->handle;
    /* (info_ptr)->cookie = MPIR_INFO_COOKIE; */
    info_ptr->key    = 0;
    info_ptr->value  = 0;
    info_ptr->next   = 0;
    /* this is the first structure in this linked list. it is 
       always kept empty. new (key,value) pairs are added after it. */
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_INFO_CREATE);
    MPIU_THREAD_SINGLE_CS_EXIT("info");
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_info_create",
	    "**mpi_info_create %p", info);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( NULL, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
