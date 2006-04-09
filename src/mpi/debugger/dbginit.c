/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: dbginit.c,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

void *MPIR_Breakpoint(void);

/*
 * This file contains information and routines used to simplify the interface 
 * to a debugger.  This follows the description in "A Standard Interface 
 * for Debugger Access to Message Queue Information in MPI", by Jim Cownie
 * and William Gropp.
 *
 * This file should be compiled with debug information (-g)
 */

/* The following is used to tell a debugger the location of the shared
   library that the debugger can load in order to access information about
   the parallel program, such as message queues */
#ifdef HAVE_DEBUGGER_SUPPORT
#ifdef MPICH_INFODLL_LOC
char MPIR_dll_name[] = MPICH_INFODLL_LOC;
#endif

/* 
 * The following variables are used to interact with the debugger.
 *
 * MPIR_debug_state 
 *    Values are 0 (before MPI_Init), 1 (after MPI_init), and 2 (Aborting).
 * MPIR_debug_gate
 *    The debugger will set this to 1 when the debugger attaches 
 *    to the process to tell the process to proceed.
 * MPIR_being_debugged
 *    Set to 1 if the process is started or attached under the debugger 
 * MPIR_debug_abort_string
 *    String that the debugger can display on an abort (?)
 */
volatile int MPIR_debug_state    = 0;
volatile int MPIR_debug_gate     = 0;
volatile int MPIR_being_debugged = 0;
char * MPIR_debug_abort_string   = 0;

/* Values for the debug_state, this seems to be all we need at the moment
 * but that may change... 
 */
#define MPIR_DEBUG_SPAWNED   1
#define MPIR_DEBUG_ABORTING  2
/*
 * MPIR_PROCDESC is used to pass information to the debugger about 
 * all of the processes.
 */
typedef struct {
    char *host_name;         /* Valid name for inet_addr */
    char *executable_name;   /* The name of the image */
    int  pid;                /* The process id */
} MPIR_PROCDESC;
MPIR_PROCDESC *MPIR_proctable    = 0;
int MPIR_proctable_size          = 1;

/* Other symbols:
 * MPIR_i_am_starter - Indicates that this process is not an MPI process
 *   (for example, the forker mpiexec?)
 * MPIR_acquired_pre_main - 
 * MPIR_partial_attach_ok -
*/
#endif

/*
 * If MPICH2 is built with the --enable-debugger option, MPI_Init and 
 * MPI_Init_thread will call MPIR_WaitForDebugger.  This ensures both that
 * the debugger can gather information on the MPI job before the MPI_Init
 * returns to the user and that the necessary symbols for providing 
 * information such as message queues is available.
 *
 * In addition, the environment variable MPIEXEC_DEBUG, if set, will cause
 * all MPI processes to wait in this routine until the variable 
 * MPIR_debug_gate is set to 1.
 */
void MPIR_WaitForDebugger( void )
{
    int rank = MPIR_Process.comm_world->rank;
    int size = MPIR_Process.comm_world->local_size;

    if (rank == 0) {
	MPIR_proctable    = (MPIR_PROCDESC *)MPIU_Malloc( size * sizeof(MPIR_PROCDESC) );
	/* Temporary to see if we can get totalview's attention */
	MPIR_proctable[0].host_name       = 0;
	MPIR_proctable[0].executable_name = 0;
	MPIR_proctable[0].pid             = getpid();

	MPIR_proctable_size          = 1;
    }

    /* Put the breakpoint after setting up the proctable */
    MPIR_debug_state    = MPIR_DEBUG_SPAWNED;
    (void)MPIR_Breakpoint();
    /* After we exit the MPIR_Breakpoint routine, the debugger may have
       set variables such as MPIR_being_debugged */

#ifdef MPID_HAS_PROCTABLE_INFO
#endif
    /* Check to see if we're not the master,
     * and wait for the debugger to attach if we're 
     * a slave. The debugger will reset the debug_gate.
     * There is no code in the library which will do it !
     * 
     * THIS IS OLD CODE FROM MPICH1  FIXME
     */
    if (MPIR_being_debugged && rank != 0) {
	while (MPIR_debug_gate == 0) {
	    /* Wait to be attached to, select avoids 
	     * signaling and allows a smaller timeout than 
	     * sleep(1)
	     */
	    struct timeval timeout;
	    timeout.tv_sec  = 0;
	    timeout.tv_usec = 250000;
	    select( 0, (void *)0, (void *)0, (void *)0,
		    &timeout );
	}
    }

    if (getenv("MPIEXEC_DEBUG")) {
	while (!MPIR_debug_gate) ; 
    }
}

/* 
 * This routine is a special dummy routine that is used to provide a
 * location for a debugger to set a breakpoint on, allowing a user (and the
 * debugger) to attach to MPI processes after MPI_Init succeeds but before
 * MPI_Init returns control to the user.
 *
 * This routine can also initialize any datastructures that are required
 * 
 */
void * MPIR_Breakpoint( void )
{
    return 0;
}
