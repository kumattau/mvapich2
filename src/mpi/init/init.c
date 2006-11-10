/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: init.c,v 1.2 2006/10/27 06:56:49 mamidala Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
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
#ifdef _SMP_
extern int split_comm;
extern int enable_shmem_collectives;
int disable_shmem_allreduce=0;
int disable_shmem_reduce=0;
int disable_shmem_barrier=0;
#endif
int MPI_Init( int *argc, char ***argv )
{
    static const char FCNAME[] = "MPI_Init";
    int mpi_errno = MPI_SUCCESS;
    MPID_MPI_INIT_STATE_DECL(MPID_STATE_MPI_INIT);

#ifdef _SMP_
#ifdef _SHMEM_COLL_
    if (setenv("MV2_ENABLE_SHMEM_COLL","1",1) == -1){
        printf("Error in setting environment\n");
        exit(0);
    }
#endif
#endif
    MPID_CS_INITIALIZE();
    MPID_CS_ENTER();
    MPID_MPI_INIT_FUNC_ENTER(MPID_STATE_MPI_INIT);
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

#ifdef _SMP_
    char *value;
    int flag;
    if ((value = getenv("MV2_ENABLE_SHMEM_COLL")) != NULL){
        enable_shmem_collectives = 1;
    }
    if ((value = getenv("MV2_DISABLE_SHMEM_ALLREDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_allreduce = 1;
        else disable_shmem_allreduce = 0;
    }
    if ((value = getenv("MV2_DISABLE_SHMEM_REDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_reduce = 1;
        else disable_shmem_reduce = 0;
    }
    if ((value = getenv("MV2_DISABLE_SHMEM_BARRIER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_barrier = 1;
        else disable_shmem_barrier = 0;
    }

    if (enable_shmem_collectives){
    if (split_comm == 1){
        int my_id, size;
        MPI_Comm_rank(MPI_COMM_WORLD, &my_id);
        MPI_Comm_size(MPI_COMM_WORLD, &size);
        split_comm = 0;
        create_2level_comm(MPI_COMM_WORLD, size, my_id);
        split_comm = 1;
    }
    }
#endif

    /* ... end of body of routine ... */
    
    MPID_MPI_INIT_FUNC_EXIT(MPID_STATE_MPI_INIT);
    MPID_CS_EXIT();
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_HANDLING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_init", "**mpi_init %p %p", argc, argv);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( 0, FCNAME, mpi_errno );
    MPID_CS_EXIT();
    MPID_CS_FINALIZE();
    return mpi_errno;
    /* --END ERROR HANDLING-- */
}
