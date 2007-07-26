/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: finalize.c,v 1.52 2006/11/07 19:18:14 gropp Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* style: allow:fprintf:1 sig:0 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Finalize */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Finalize = PMPI_Finalize
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Finalize  MPI_Finalize
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Finalize as PMPI_Finalize
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Finalize
#define MPI_Finalize PMPI_Finalize

/* Any internal routines can go here.  Make them static if possible */

/* The following routines provide a callback facility for modules that need 
   some code called on exit.  This method allows us to avoid forcing 
   MPI_Finalize to know the routine names a priori.  Any module that wants to 
   have a callback calls MPIR_Add_finalize( routine, extra, priority ).
   
 */
PMPI_LOCAL void MPIR_Call_finalize_callbacks( int, int );
typedef struct Finalize_func_t {
    int (*f)( void * );      /* The function to call */
    void *extra_data;        /* Data for the function */
    int  priority;           /* priority is used to control the order
				in which the callbacks are invoked */
} Finalize_func_t;
#define MAX_FINALIZE_FUNC 16
static Finalize_func_t fstack[MAX_FINALIZE_FUNC];
static int fstack_sp = 0;
static int fstack_max_priority = 0;

void MPIR_Add_finalize( int (*f)( void * ), void *extra_data, int priority )
{
    /* --BEGIN ERROR HANDLING-- */
    if (fstack_sp >= MAX_FINALIZE_FUNC) {
	/* This is a little tricky.  We may want to check the state of
	   MPIR_Process.initialized to decide how to signal the error */
	(void)MPIU_Internal_error_printf( "overflow in finalize stack!\n" );
	if (MPIR_Process.initialized == MPICH_WITHIN_MPI) {
	    MPID_Abort( NULL, MPI_SUCCESS, 13, NULL );
	}
	else {
	    exit(1);
	}
    }
    /* --END ERROR HANDLING-- */
    fstack[fstack_sp].f            = f;
    fstack[fstack_sp].priority     = priority;
    fstack[fstack_sp++].extra_data = extra_data;

    if (priority > fstack_max_priority) 
	fstack_max_priority = priority;
}

/* Invoke the registered callbacks */
PMPI_LOCAL void MPIR_Call_finalize_callbacks( int min_prio, int max_prio )
{
    int i, j;
    for (j=fstack_max_priority; j>=min_prio; j--) {
	for (i=fstack_sp-1; i>=0; i--) {
	    if (fstack[i].f && fstack[i].priority == j) {
		fstack[i].f( fstack[i].extra_data );
		fstack[i].f = 0;
	    }
	}
    }
}
#else
#ifndef USE_WEAK_SYMBOLS
PMPI_LOCAL void MPIR_Call_finalize_callbacks( int, int );
#endif
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Finalize

/*@
   MPI_Finalize - Terminates MPI execution environment

   Notes:
   All processes must call this routine before exiting.  The number of
   processes running `after` this routine is called is undefined; 
   it is best not to perform much more than a 'return rc' after calling
   'MPI_Finalize'.

Thread and Signal Safety:
The MPI standard requires that 'MPI_Finalize' be called `only` by the same 
thread that initialized MPI with either 'MPI_Init' or 'MPI_Init_thread'.

.N Fortran

.N Errors
.N MPI_SUCCESS
@*/
int MPI_Finalize( void )
{
    static const char FCNAME[] = "MPI_Finalize";
    int mpi_errno = MPI_SUCCESS;
#if defined(HAVE_USLEEP) && defined(USE_COVERAGE)
    int rank=0;
#endif
    MPID_MPI_FINALIZE_STATE_DECL(MPID_STATE_MPI_FINALIZE);

    MPIR_ERRTEST_INITIALIZED_ORDIE();

    MPIU_THREAD_SINGLE_CS_ENTER("init");
    MPID_MPI_FINALIZE_FUNC_ENTER(MPID_STATE_MPI_FINALIZE);
    
    /* ... body of routine ... */
    
#if defined(HAVE_USLEEP) && defined(USE_COVERAGE)
    /* We need to get the rank before freeing MPI_COMM_WORLD */
    rank = MPIR_Process.comm_world->rank;
#endif    

    /* Remove the attributes, executing the attribute delete routine.
       Do this only if the attribute functions are defined. */ 
    /* The standard (MPI-2, section 4.8) says that the attributes on 
       MPI_COMM_SELF are deleted before almost anything else happens */
    if (MPIR_Process.attr_free && MPIR_Process.comm_self->attributes) {
        mpi_errno = MPIR_Process.attr_free( MPI_COMM_SELF,
					   MPIR_Process.comm_self->attributes);
    }
    if (MPIR_Process.attr_free && MPIR_Process.comm_world->attributes) {
        mpi_errno = MPIR_Process.attr_free( MPI_COMM_WORLD, 
                                         MPIR_Process.comm_world->attributes);
    }

    /* FIXME: Why is this not one of the finalize callbacks?.  Do we need
       pre and post MPID_Finalize callbacks? */
    MPIU_Timer_finalize();

    /* Call the high-priority callbacks */
    MPIR_Call_finalize_callbacks( MPIR_FINALIZE_CALLBACK_PRIO+1, 
				  MPIR_FINALIZE_CALLBACK_MAX_PRIO );

    mpi_errno = MPID_Finalize();
    if (mpi_errno) {
	MPIU_ERR_POP(mpi_errno);
    }
    
    /* delete local and remote groups on comm_world and comm_self if
       they had been created (should we use a function pointer here
       as well to avoid loading the group code?) */
    if (MPIR_Process.comm_world->local_group)
        MPIR_Group_release(MPIR_Process.comm_world->local_group);
    if (MPIR_Process.comm_world->remote_group)
        MPIR_Group_release(MPIR_Process.comm_world->remote_group);
    if (MPIR_Process.comm_self->local_group)
        MPIR_Group_release(MPIR_Process.comm_self->local_group);
    if (MPIR_Process.comm_self->remote_group)
        MPIR_Group_release(MPIR_Process.comm_self->remote_group);

    /* Call the low-priority (post Finalize) callbacks */
    MPIR_Call_finalize_callbacks( 0, MPIR_FINALIZE_CALLBACK_PRIO-1 );

    /* FIXME: Both the memory tracing and debug nesting code blocks should
       be finalize callbacks */
    /* If memory debugging is enabled, check the memory here, after all
       finalize callbacks */
#ifdef USE_MEMORY_TRACING
    /* FIXME: the (1) in the if test should be replaced by a 
       parameter call */
    /* FIXME: We'd like to arrange for the mem dump output to
       go to separate files or to be sorted by rank (note that
       the rank is at the head of the line) */
    if (1) {
	/* FIXME: The second argument is the min id to print; memory allocated 
	   after MPI_Init is given an id of one.  This allows us to
	   ignore, for the moment, memory leaks in the MPID_Init call */
	MPIU_trdump( (void *)0, 1 );
    }
#endif
#ifdef MPICH_DEBUG_NESTING
    /* FIXME: the (1) in the if test should be replaced by a 
       parameter call */
    if (1) {
	MPIU_THREADPRIV_DECL;

	MPIU_THREADPRIV_GET;
	/* Check for an error in the nesting level */
	if (MPIR_Nest_value()) {
	    int i,n;
	    n = MPIR_Nest_value();
	    fprintf( stderr, "Unexpected value for nesting level = %d\n", n );
	    fprintf( stderr, "Nest stack is:\n" );
	    for (i=n-1; i>=0; i--) {
		fprintf( stderr, "\t[%d] %s:%d\n", i, 
			 MPIU_THREADPRIV_FIELD(nestinfo[i].file), 
			 MPIU_THREADPRIV_FIELD(nestinfo[i].line) );
	    }
	}
    }
#endif

    MPIR_Process.initialized = MPICH_POST_FINALIZED;

#if defined(HAVE_USLEEP) && defined(USE_COVERAGE)
    /* If performing coverage analysis, make each process sleep for
       rank * 100 ms, to give time for the coverage tool to write out
       any files */
    usleep( rank * 100000 );
#endif
    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FINALIZE_FUNC_EXIT(MPID_STATE_MPI_FINALIZE);
    MPIU_THREAD_SINGLE_CS_EXIT("init");
    MPIU_THREAD_SINGLE_CS_FINALIZE;
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, 
			FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_finalize", 0);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( 0, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
