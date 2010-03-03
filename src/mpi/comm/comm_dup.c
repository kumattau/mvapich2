/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpiimpl.h"
#include "mpicomm.h"

/* -- Begin Profiling Symbol Block for routine MPI_Comm_dup */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_dup = PMPI_Comm_dup
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_dup  MPI_Comm_dup
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_dup as PMPI_Comm_dup
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Comm_dup
#define MPI_Comm_dup PMPI_Comm_dup

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Comm_dup

/*@

MPI_Comm_dup - Duplicates an existing communicator with all its cached
               information

Input Parameter:
. comm - Communicator to be duplicated (handle) 

Output Parameter:
. newcomm - A new communicator over the same group as 'comm' but with a new
  context. See notes.  (handle) 

Notes:
  This routine is used to create a new communicator that has a new
  communication context but contains the same group of processes as
  the input communicator.  Since all MPI communication is performed
  within a communicator (specifies as the group of processes `plus`
  the context), this routine provides an effective way to create a
  private communicator for use by a software module or library.  In
  particular, no library routine should use 'MPI_COMM_WORLD' as the
  communicator; instead, a duplicate of a user-specified communicator
  should always be used.  For more information, see Using MPI, 2nd
  edition. 

  Because this routine essentially produces a copy of a communicator,
  it also copies any attributes that have been defined on the input
  communicator, using the attribute copy function specified by the
  'copy_function' argument to 'MPI_Keyval_create'.  This is
  particularly useful for (a) attributes that describe some property
  of the group associated with the communicator, such as its
  interconnection topology and (b) communicators that are given back
  to the user; the attibutes in this case can track subsequent
  'MPI_Comm_dup' operations on this communicator.

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM

.seealso: MPI_Comm_free, MPI_Keyval_create, MPI_Attr_put, MPI_Attr_delete,
 MPI_Comm_create_keyval, MPI_Comm_set_attr, MPI_Comm_delete_attr
@*/
#if defined(_OSU_MVAPICH_)
extern int split_comm;
extern int enable_shmem_collectives;
extern int check_split_comm(pthread_t);
extern int disable_split_comm(pthread_t);
extern int create_2level_comm (MPI_Comm, int, int);
extern int enable_split_comm(pthread_t);
#endif /* defined(_OSU_MVAPICH_) */

int MPI_Comm_dup(MPI_Comm comm, MPI_Comm *newcomm)
{
    static const char FCNAME[] = "MPI_Comm_dup";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL, *newcomm_ptr;
    MPID_Attribute *new_attributes = 0;
#if defined(_OSU_MVAPICH_)
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
#endif /* defined(_OSU_MVAPICH_) */

    MPID_MPI_STATE_DECL(MPID_STATE_MPI_COMM_DUP);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("comm");
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_COMM_DUP);
    
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
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
	    /* If comm_ptr is not valid, it will be reset to null */
            MPIR_ERRTEST_ARGNULL(newcomm, "newcomm", mpi_errno);
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    /* Copy attributes, executing the attribute copy functions */
    /* This accesses the attribute dup function through the perprocess
       structure to prevent comm_dup from forcing the linking of the
       attribute functions.  The actual function is (by default)
       MPIR_Attr_dup_list 
    */
    if (MPIR_Process.attr_dup) {
	mpi_errno = MPIR_Process.attr_dup( comm_ptr->handle, 
					   comm_ptr->attributes, 
					   &new_attributes );
	if (mpi_errno) {
	    /* Note: The error code returned here should reflect the error code
	       determined by the user routine called during the
	       attribute duplication step.  Adding additional text to the 
	       message associated with the code is allowable; changing the
	       code is not */
	    *newcomm = MPI_COMM_NULL;
	    goto fn_fail;
	}
    }

    
    /* Generate a new context value and a new communicator structure */ 
    /* We must use the local size, because this is compared to the 
       rank of the process in the communicator.  For intercomms, 
       this must be the local size */
    mpi_errno = MPIR_Comm_copy( comm_ptr, comm_ptr->local_size, 
				&newcomm_ptr );
    if (mpi_errno) goto fn_fail;

    newcomm_ptr->attributes = new_attributes;
    *newcomm = newcomm_ptr->handle;

#if defined(_OSU_MVAPICH_)
    /* We also need to replicate the leader_comm and the shmem_comm
     * communicators in the newly created communicator. We have all the 
     * information available locally. We can just use this information instead of 
     * going through the create_2level_comm function. We can also save 
     * on memory this way, instead of allocating new memory buffers. 
     */ 
    if (enable_shmem_collectives){
       if(newcomm_ptr != NULL && *newcomm != MPI_COMM_NULL) { 
          int flag;
          PMPI_Comm_test_inter(*newcomm, &flag);

          if(flag == 0) { 
               newcomm_ptr->leader_comm       = comm_ptr->leader_comm;
               newcomm_ptr->shmem_comm        = comm_ptr->shmem_comm; 
               newcomm_ptr->leader_map        = comm_ptr->leader_map;
               newcomm_ptr->leader_rank       = comm_ptr->leader_rank;
               newcomm_ptr->shmem_comm_rank   = comm_ptr->shmem_comm_rank;
               newcomm_ptr->shmem_coll_ok     = comm_ptr->shmem_coll_ok;
               newcomm_ptr->leader_group_size = comm_ptr->leader_group_size;
               newcomm_ptr->bcast_fd          = comm_ptr->bcast_fd;
               newcomm_ptr->bcast_index       = comm_ptr->bcast_index;
               newcomm_ptr->bcast_mmap_ptr    = comm_ptr->bcast_mmap_ptr;
               newcomm_ptr->bcast_shmem_file  = comm_ptr->bcast_shmem_file;
               newcomm_ptr->bcast_seg_size    = comm_ptr->bcast_seg_size;
           }
        } 
    }      
#endif /* defined(_OSU_MVAPICH_) */
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_COMM_DUP);
    MPIU_THREAD_SINGLE_CS_EXIT("comm");
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_comm_dup",
	    "**mpi_comm_dup %C %p", comm, newcomm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

