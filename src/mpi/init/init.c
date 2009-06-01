/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: init.c,v 1.34 2007/08/03 21:02:32 buntinas Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2009, The Ohio State University. All rights
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
#include "mpi_init.h"


/* -- Begin Profiling Symbol Block for routine MPI_Init */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Init = PMPI_Init
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Init  MPI_Init
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Init as PMPI_Init
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Init
#define MPI_Init PMPI_Init

/* Fortran logical values. extern'd in mpiimpl.h */
/* MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE; */

/* Any internal routines can go here.  Make them static if possible */
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Init

/*@
   MPI_Init - Initialize the MPI execution environment

   Input Parameters:
+  argc - Pointer to the number of arguments 
-  argv - Pointer to the argument vector

Thread and Signal Safety:
This routine must be called by one thread only.  That thread is called
the `main thread` and must be the thread that calls 'MPI_Finalize'.

Notes:
   The MPI standard does not say what a program can do before an 'MPI_INIT' or
   after an 'MPI_FINALIZE'.  In the MPICH implementation, you should do
   as little as possible.  In particular, avoid anything that changes the
   external state of the program, such as opening files, reading standard
   input or writing to standard output.

Notes for Fortran:
The Fortran binding for 'MPI_Init' has only the error return
.vb
    subroutine MPI_INIT( ierr )
    integer ierr
.ve

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_INIT

.seealso: MPI_Init_thread, MPI_Finalize
@*/
#if defined(_OSU_MVAPICH_)
extern int split_comm;
extern int enable_shmem_collectives;
extern void MV2_Read_env_vars();
extern int check_split_comm(pthread_t);
extern int disable_split_comm(pthread_t);
extern void create_2level_comm (MPI_Comm, int, int);
extern int enable_split_comm(pthread_t);
#endif /* defined(_OSU_MVAPICH_) */
int MPI_Init( int *argc, char ***argv )
{
    static const char FCNAME[] = "MPI_Init";
    int mpi_errno = MPI_SUCCESS;
    MPID_MPI_INIT_STATE_DECL(MPID_STATE_MPI_INIT);

#if defined(_OSU_MVAPICH_)
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
#endif /* defined(_OSU_MVAPICH_) */
    MPID_CS_INITIALIZE();
    /* FIXME: Can we get away without locking every time.  Now, we
       need a MPID_CS_ENTER/EXIT around MPI_Init and MPI_Init_thread.
       Progress may be called within MPI_Init, e.g., by a spawned
       child process.  Within progress, the lock is released and
       reacquired when blocking.  If the lock isn't acquired before
       then, the release in progress is incorrect.  Furthermore, if we
       don't release the lock after progress, we'll deadlock the next
       time this process tries to acquire the lock.
       MPID_CS_ENTER/EXIT functions are used here instead of
       MPIU_THREAD_SINGLE_CS_ENTER/EXIT because
       MPIR_ThreadInfo.isThreaded hasn't been initialized yet.
    */
    MPID_CS_ENTER();
    
    MPID_MPI_INIT_FUNC_ENTER(MPID_STATE_MPI_INIT);
#if defined(_OSU_MVAPICH_)
    MV2_Read_env_vars();
#endif /* defined(_OSU_MVAPICH_) */

#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            if (MPIR_Process.initialized != MPICH_PRE_INIT) {
                mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
						  "**inittwice", NULL );
	    }
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ... */
    
    mpi_errno = MPIR_Init_thread( argc, argv, MPI_THREAD_SINGLE, (int *)0 );
    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

#if defined(_OSU_MVAPICH_) 
    if (enable_shmem_collectives){
        if (check_split_comm(pthread_self())){
            MPIR_Nest_incr();
            int my_id, size;
            MPI_Comm_rank(MPI_COMM_WORLD, &my_id);
            MPI_Comm_size(MPI_COMM_WORLD, &size);
            disable_split_comm(pthread_self());
            create_2level_comm(MPI_COMM_WORLD, size, my_id);
            enable_split_comm(pthread_self());
            MPIR_Nest_decr();
        }
    }
#endif /* defined(_OSU_MVAPICH_) */
    /* ... end of body of routine ... */
    
    MPID_MPI_INIT_FUNC_EXIT(MPID_STATE_MPI_INIT);
    MPID_CS_EXIT();
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_REPORTING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, 
	    "**mpi_init", "**mpi_init %p %p", argc, argv);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( 0, FCNAME, mpi_errno );
    MPID_CS_EXIT();
    MPID_CS_FINALIZE();
    return mpi_errno;
    /* --END ERROR HANDLING-- */
}
