/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: initthread.c,v 1.105 2007/08/03 21:02:32 buntinas Exp $
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
#include "datatype.h"
#include "mpi_init.h"
#ifdef HAVE_CRTDBG_H
#include <crtdbg.h>
#endif

/* -- Begin Profiling Symbol Block for routine MPI_Init_thread */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Init_thread = PMPI_Init_thread
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Init_thread  MPI_Init_thread
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Init_thread as PMPI_Init_thread
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Init_thread
#define MPI_Init_thread PMPI_Init_thread

/* Any internal routines can go here.  Make them static if possible */

/* Global variables can be initialized here */
MPICH_PerProcess_t MPIR_Process = { MPICH_PRE_INIT }; 
     /* all other fields in MPIR_Process are irrelevant */
MPICH_ThreadInfo_t MPIR_ThreadInfo = { 0 };

#if defined(_OSU_MVAPICH_)
#define DEFAULT_SHMEM_BCAST_LEADERS    4096
#endif /* _OSU_MVAPICH_ */


/* These are initialized as null (avoids making these into common symbols).
   If the Fortran binding is supported, these can be initialized to 
   their Fortran values (MPI only requires that they be valid between
   MPI_Init and MPI_Finalize) */
MPIU_DLL_SPEC MPI_Fint *MPI_F_STATUS_IGNORE = 0;
MPIU_DLL_SPEC MPI_Fint *MPI_F_STATUSES_IGNORE = 0;

/* This will help force the load of initinfo.o, which contains data about
   how MPICH2 was configured. */
extern const char MPIR_Version_device[];

#ifdef HAVE_WINDOWS_H
/* User-defined abort hook function.  Exiting here will prevent the system from
 * bringing up an error dialog box.
 */
/* style: allow:fprintf:1 sig:0 */
static int assert_hook( int reportType, char *message, int *returnValue )
{
    MPIU_UNREFERENCED_ARG(reportType);
    fprintf(stderr, "%s", message);
    if (returnValue != NULL)
	ExitProcess((UINT)(*returnValue));
    ExitProcess((UINT)(-1));
    return TRUE;
}

/* MPICH2 dll entry point */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    BOOL result = TRUE;
    hinstDLL;
    lpReserved;

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            break;

        case DLL_THREAD_ATTACH:
	    /* allocate thread specific data */
            break;

        case DLL_THREAD_DETACH:
	    /* free thread specific data */
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return result;
}
#endif


#if !defined(MPICH_IS_THREADED)
/* If single threaded, we preallocate this.  Otherwise, we create it */
MPICH_PerThread_t  MPIR_Thread = { 0 };
#elif defined(HAVE_RUNTIME_THREADCHECK)
/* If we may be single threaded, we need a preallocated version to use
   if we are single threaded case */
MPICH_PerThread_t  MPIR_ThreadSingle = { 0 };
#endif

#if defined(MPICH_IS_THREADED)
/* This routine is called when a thread exits; it is passed the value 
   associated with the key.  In our case, this is simply storage allocated
   with MPIU_Calloc */
void MPIR_CleanupThreadStorage( void *a )
{
    if (a != 0) {
	MPIU_Free( a );
    }
}
#endif /* MPICH_IS_THREADED */


int MPIR_Init_thread(int * argc, char ***argv, int required,
		     int * provided)
{
    int mpi_errno = MPI_SUCCESS;
    int has_args;
    int has_env;
    int thread_provided;
    MPIU_THREADPRIV_DECL;

    /* FIXME: Move to os-dependent interface? */
#ifdef HAVE_WINDOWS_H
    /* prevent the process from bringing up an error message window if mpich 
       asserts */
    _CrtSetReportMode( _CRT_ASSERT, _CRTDBG_MODE_FILE );
    _CrtSetReportFile( _CRT_ASSERT, _CRTDBG_FILE_STDERR );
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, assert_hook);
#ifdef _WIN64
    {
    /* FIXME: This severly degrades performance but fixes alignment issues 
       with the datatype code. */
    /* Prevent misaligned faults on Win64 machines */
    UINT mode, old_mode;
    
    old_mode = SetErrorMode(SEM_NOALIGNMENTFAULTEXCEPT);
    mode = old_mode | SEM_NOALIGNMENTFAULTEXCEPT;
    SetErrorMode(mode);
    }
#endif
#endif

    /* We need this inorder to implement IS_THREAD_MAIN */
#   if (MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED)
    {
	MPID_Thread_self(&MPIR_ThreadInfo.master_thread);
    }
#   endif

#if 0
    /* This should never happen */
    if (MPIR_Version_device == 0) {
	
    }
#endif     
#ifdef HAVE_ERROR_CHECKING
    /* Eventually this will support commandline and environment options
     for controlling error checks.  It will use the routine 
     MPIR_Err_init, which does as little as possible (e.g., it only 
     determines the value of do_error_checks) */
    MPIR_Process.do_error_checks = 1;
#else
    MPIR_Process.do_error_checks = 0;
#endif

    /* Initialize necessary subsystems and setup the predefined attribute
       values.  Subsystems may change these values. */
    MPIR_Process.attrs.appnum          = -1;
    MPIR_Process.attrs.host            = 0;
    MPIR_Process.attrs.io              = 0;
    MPIR_Process.attrs.lastusedcode    = MPI_ERR_LASTCODE;
    MPIR_Process.attrs.tag_ub          = 0;
    MPIR_Process.attrs.universe        = MPIR_UNIVERSE_SIZE_NOT_SET;
    MPIR_Process.attrs.wtime_is_global = 0;

    /* Set the functions used to duplicate attributes.  These are 
       when the first corresponding keyval is created */
    MPIR_Process.attr_dup  = 0;
    MPIR_Process.attr_free = 0;

#ifdef HAVE_CXX_BINDING
    /* Set the functions used to call functions in the C++ binding 
       for reductions and attribute operations.  These are null
       until a C++ operation is defined.  This allows the C code
       that implements these operations to not invoke a C++ code
       directly, which may force the inclusion of symbols known only
       to the C++ compiler (e.g., under more non-GNU compilers, including
       Solaris and IRIX). */
    MPIR_Process.cxx_call_op_fn = 0;
    MPIR_Process.cxx_call_delfn = 0;

#endif
    /* This allows the device to select an alternative function for 
       dimsCreate */
    MPIR_Process.dimsCreate     = 0;

    /* "Allocate" from the reserved space for builtin communicators and
       (partially) initialize predefined communicators.  comm_parent is
       intially NULL and will be allocated by the device if the process group
       was started using one of the MPI_Comm_spawn functions. */
    MPIR_Process.comm_world		    = MPID_Comm_builtin + 0;
    MPIR_Process.comm_world->handle	    = MPI_COMM_WORLD;
    MPIU_Object_set_ref( MPIR_Process.comm_world, 1 );
    MPIR_Process.comm_world->context_id	    = 0; /* XXX */
    MPIR_Process.comm_world->recvcontext_id = 0;
    MPIR_Process.comm_world->attributes	    = NULL;
    MPIR_Process.comm_world->local_group    = NULL;
    MPIR_Process.comm_world->remote_group   = NULL;
    MPIR_Process.comm_world->comm_kind	    = MPID_INTRACOMM;
    /* This initialization of the comm name could be done only when 
       comm_get_name is called */
    MPIU_Strncpy(MPIR_Process.comm_world->name, "MPI_COMM_WORLD",
		 MPI_MAX_OBJECT_NAME);
    MPIR_Process.comm_world->errhandler	    = NULL; /* XXX */
    MPIR_Process.comm_world->coll_fns	    = NULL; /* XXX */
    MPIR_Process.comm_world->topo_fns	    = NULL; /* XXX */
    
    MPIR_Process.comm_self		    = MPID_Comm_builtin + 1;
    MPIR_Process.comm_self->handle	    = MPI_COMM_SELF;
    MPIU_Object_set_ref( MPIR_Process.comm_self, 1 );
    MPIR_Process.comm_self->context_id	    = 4; /* XXX */
    MPIR_Process.comm_self->recvcontext_id  = 4; /* XXX */
    MPIR_Process.comm_self->attributes	    = NULL;
    MPIR_Process.comm_self->local_group	    = NULL;
    MPIR_Process.comm_self->remote_group    = NULL;
    MPIR_Process.comm_self->comm_kind	    = MPID_INTRACOMM;
    MPIU_Strncpy(MPIR_Process.comm_self->name, "MPI_COMM_SELF",
		 MPI_MAX_OBJECT_NAME);
    MPIR_Process.comm_self->errhandler	    = NULL; /* XXX */
    MPIR_Process.comm_self->coll_fns	    = NULL; /* XXX */
    MPIR_Process.comm_self->topo_fns	    = NULL; /* XXX */

#ifdef MPID_NEEDS_ICOMM_WORLD
    MPIR_Process.icomm_world		    = MPID_Comm_builtin + 2;
    MPIR_Process.icomm_world->handle	    = MPIR_ICOMM_WORLD;
    MPIU_Object_set_ref( MPIR_Process.icomm_world, 1 );
    MPIR_Process.icomm_world->context_id    = 8; /* XXX */
    MPIR_Process.icomm_world->recvcontext_id= 8;
    MPIR_Process.icomm_world->attributes    = NULL;
    MPIR_Process.icomm_world->local_group   = NULL;
    MPIR_Process.icomm_world->remote_group  = NULL;
    MPIR_Process.icomm_world->comm_kind	    = MPID_INTRACOMM;
    /* This initialization of the comm name could be done only when 
       comm_get_name is called */
    MPIU_Strncpy(MPIR_Process.icomm_world->name, "MPI_ICOMM_WORLD",
		 MPI_MAX_OBJECT_NAME);
    MPIR_Process.icomm_world->errhandler    = NULL; /* XXX */
    MPIR_Process.icomm_world->coll_fns	    = NULL; /* XXX */
    MPIR_Process.icomm_world->topo_fns	    = NULL; /* XXX */

    /* Note that these communicators are not ready for use - MPID_Init 
       will setup self and world, and icomm_world if it desires it. */
#endif

    MPIR_Process.comm_parent = NULL;

    /* Setup the initial communicator list in case we have 
       enabled the debugger message-queue interface */
    MPIR_COMML_REMEMBER( MPIR_Process.comm_world );
    MPIR_COMML_REMEMBER( MPIR_Process.comm_self );

    /* Call any and all MPID_Init type functions */
    /* FIXME: The call to err init should be within an ifdef
       HAVE_ ERROR_CHECKING block (as must all uses of Err_create_code) */
    MPID_Wtime_init();
#ifdef USE_DBG_LOGGING
    MPIU_DBG_PreInit( argc, argv );
#endif
    MPIR_Err_init();
    MPIR_Datatype_init();

    MPIU_THREADPRIV_GET;

    MPIR_Nest_init();
    /* MPIU_Timer_pre_init(); */

    /* define MPI as initialized so that we can use MPI functions within 
       MPID_Init if necessary */
    MPIR_Process.initialized = MPICH_WITHIN_MPI;

    /* For any code in the device that wants to check for runtime 
       decisions on the value of isThreaded, set a provisional
       value here. We could let the MPID_Init routine override this */
#ifdef HAVE_RUNTIME_THREADCHECK
    MPIR_ThreadInfo.isThreaded = required == MPI_THREAD_MULTIPLE;
#endif
    mpi_errno = MPID_Init(argc, argv, required, &thread_provided, 
			  &has_args, &has_env);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, 
			   "MPIR_Init_thread", __LINE__, MPI_ERR_OTHER, 
			   "**init", 0);
	/* FIXME: the default behavior for all MPI routines is to abort.  
	   This isn't always convenient, because there's no other way to 
	   get this routine to simply return.  But we should provide some
	   sort of control for that and follow the default defined 
	   by the standard */
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    /* Capture the level of thread support provided */
    MPIR_ThreadInfo.thread_provided = thread_provided;
    if (provided) *provided = thread_provided;
    /* FIXME: Rationalize this with the above */
#ifdef HAVE_RUNTIME_THREADCHECK
    MPIR_ThreadInfo.isThreaded = required == MPI_THREAD_MULTIPLE;
#if !defined(_OSU_MVAPICH_)
    if (provided) *provided = required;
#endif /* !defined(_OSU_MVAPICH_) */
#endif

    /* FIXME: Define these in the interface.  Does Timer init belong here? */
    MPIU_dbg_init(MPIR_Process.comm_world->rank);
    MPIU_Timer_init(MPIR_Process.comm_world->rank,
		    MPIR_Process.comm_world->local_size);
#ifdef USE_MEMORY_TRACING
    MPIU_trinit( MPIR_Process.comm_world->rank );
    /* Indicate that we are near the end of the init step; memory 
       allocated already will have an id of zero; this helps 
       separate memory leaks in the initialization code from 
       leaks in the "active" code */
    /* Uncomment this code to leave out any of the MPID_Init/etc 
       memory allocations from the memory leak testing */
    /* MPIU_trid( 1 ); */
#endif
#ifdef USE_DBG_LOGGING
    MPIU_DBG_Init( argc, argv, has_args, has_env, 
		   MPIR_Process.comm_world->rank );
#endif

    /* FIXME: There is no code for this comment */
    /* We now initialize the Fortran symbols from within the Fortran 
       interface in the routine that first needs the symbols.
       This fixes a problem with symbols added by a Fortran compiler that 
       are not part of the C runtime environment (the Portland group
       compilers would do this) */

    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
        MPIR_Process.initialized = MPICH_PRE_INIT;
    /* --END ERROR HANDLING-- */

#ifdef HAVE_DEBUGGER_SUPPORT
    MPIR_WaitForDebugger();
#endif
    
    /* Let the device know that the rest of the init process is completed */
    if (mpi_errno == MPI_SUCCESS) 
	mpi_errno = MPID_InitCompleted();

    return mpi_errno;
}
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Init_thread

/*@
   MPI_Init_thread - Initialize the MPI execution environment

   Input Parameters:
+  argc - Pointer to the number of arguments 
.  argv - Pointer to the argument vector
-  required - Level of desired thread support

   Output Parameter:
.  provided - Level of provided thread support

   Command line arguments:
   MPI specifies no command-line arguments but does allow an MPI 
   implementation to make use of them.  See 'MPI_INIT' for a description of 
   the command line arguments supported by 'MPI_INIT' and 'MPI_INIT_THREAD'.

   Notes:
   The valid values for the level of thread support are\:
+ MPI_THREAD_SINGLE - Only one thread will execute. 
. MPI_THREAD_FUNNELED - The process may be multi-threaded, but only the main 
  thread will make MPI calls (all MPI calls are funneled to the 
   main thread). 
. MPI_THREAD_SERIALIZED - The process may be multi-threaded, and multiple 
  threads may make MPI calls, but only one at a time: MPI calls are not 
  made concurrently from two distinct threads (all MPI calls are serialized). 
- MPI_THREAD_MULTIPLE - Multiple threads may call MPI, with no restrictions. 

Notes for Fortran:
   Note that the Fortran binding for this routine does not have the 'argc' and
   'argv' arguments. ('MPI_INIT_THREAD(required, provided, ierror)')


.N Errors
.N MPI_SUCCESS
.N MPI_ERR_OTHER

.seealso: MPI_Init, MPI_Finalize
@*/
#if defined(_OSU_MVAPICH_)
extern int split_comm;
int enable_shmem_collectives = 1;
int disable_shmem_allreduce=0;
int disable_shmem_reduce=0;
int disable_shmem_barrier=0;
int alltoall_dreg_disable_threshold=1024;
int alltoall_dreg_disable=0;
int g_shmem_bcast_leaders = DEFAULT_SHMEM_BCAST_LEADERS;
int g_shmem_bcast_flags = DEFAULT_SHMEM_BCAST_LEADERS;
extern int g_shmem_coll_blocks;
extern int g_shmem_coll_max_msg_size;
extern int shmem_bcast_threshold;
extern int enable_shmem_bcast;
void MV2_Read_env_vars(void);
void init_thread_reg();

extern int check_split_comm(pthread_t);
extern int disable_split_comm(pthread_t);
extern void create_2level_comm (MPI_Comm, int, int);
extern int enable_split_comm(pthread_t);

struct coll_runtime coll_param = { MPIR_ALLREDUCE_SHORT_MSG,
                                   MPIR_REDUCE_SHORT_MSG,
                                   SHMEM_ALLREDUCE_THRESHOLD,
                                   SHMEM_REDUCE_THRESHOLD
};
#endif /* defined(_OSU_MVAPICH_) */

int MPI_Init_thread( int *argc, char ***argv, int required, int *provided )
{
    static const char FCNAME[] = "MPI_Init_thread";
    int mpi_errno = MPI_SUCCESS;
    MPID_MPI_INIT_STATE_DECL(MPID_STATE_MPI_INIT_THREAD);

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

#if 0
    /* Create the thread-private region if necessary and go ahead 
       and initialize it */
    MPIU_THREADPRIV_INITKEY;
    MPIU_THREADPRIV_INIT;
#endif

    MPID_MPI_INIT_FUNC_ENTER(MPID_STATE_MPI_INIT_THREAD);
    
#if defined(_OSU_MVAPICH_)
    MV2_Read_env_vars();
#endif /* defined(_OSU_MVAPICH_) */

#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            if (MPIR_Process.initialized != MPICH_PRE_INIT) {
                mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, "MPI_Init_thread", __LINE__, MPI_ERR_OTHER,
						  "**inittwice", 0 );
	    }
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ... */
    
    mpi_errno = MPIR_Init_thread( argc, argv, required, provided );
    if (mpi_errno != MPI_SUCCESS) goto fn_fail; 

#if defined(_OSU_MVAPICH_)
    if (enable_shmem_collectives){
        if (check_split_comm(pthread_self())){
            MPIR_Nest_incr();
            int my_id, size;
            PMPI_Comm_rank(MPI_COMM_WORLD, &my_id);
            PMPI_Comm_size(MPI_COMM_WORLD, &size);
            disable_split_comm(pthread_self());
            create_2level_comm(MPI_COMM_WORLD, size, my_id);
            enable_split_comm(pthread_self());
            MPIR_Nest_decr();
        }
    }
#endif /* defined(_OSU_MVAPICH_) */
    /* ... end of body of routine ... */
    
    MPID_MPI_INIT_FUNC_EXIT(MPID_STATE_MPI_INIT_THREAD);
    MPID_CS_EXIT();
    return mpi_errno;
    
  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_REPORTING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, 
	    "**mpi_init_thread",
	    "**mpi_init_thread %p %p %d %p", argc, argv, required, provided);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( 0, FCNAME, mpi_errno );
    MPID_MPI_INIT_FUNC_EXIT(MPID_STATE_MPI_INIT_THREAD);
    MPID_CS_EXIT();
    MPID_CS_FINALIZE();
    return mpi_errno;
    /* --END ERROR HANDLING-- */
}

#if defined(_OSU_MVAPICH_)
void MV2_Read_env_vars(void){
    char *value;
    int flag;
    if ((value = getenv("MV2_USE_SHMEM_COLL")) != NULL){
        flag = (int)atoi(value); 
        if (flag > 0) enable_shmem_collectives = 1;
        else enable_shmem_collectives = 0;
    }
    if ((value = getenv("MV2_USE_SHMEM_ALLREDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_allreduce = 0;
        else disable_shmem_allreduce = 1;
    }
    if ((value = getenv("MV2_USE_SHMEM_REDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_reduce = 0;
        else disable_shmem_reduce = 1;
    }
    if ((value = getenv("MV2_USE_SHMEM_BARRIER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_barrier = 0;
        else disable_shmem_barrier = 1;
    }
    if ((value = getenv("MV2_SHMEM_COLL_NUM_COMM")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) g_shmem_coll_blocks = flag;
    }
    if ((value = getenv("MV2_SHMEM_COLL_MAX_MSG_SIZE")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) g_shmem_coll_max_msg_size = flag;
    }
    if ((value = getenv("MV2_SHMEM_BCAST_LEADERS")) != NULL){
        if ((atoi(value) > DEFAULT_SHMEM_BCAST_LEADERS )) {
            /* We only accept positive values */
	        g_shmem_bcast_leaders = (int)atoi(value);
	        g_shmem_bcast_flags = (int)atoi(value);
        }
    }
    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL){
	    flag = (int)atoi(value);
	    if (flag <= 0) enable_shmem_collectives = 0;
    }
    if ((value = getenv("MV2_USE_BLOCKING")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) enable_shmem_collectives = 0;
    }

    if ((value = getenv("MV2_ALLREDUCE_SHORT_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.allreduce_short_msg = flag;
    }
    if ((value = getenv("MV2_REDUCE_SHORT_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.reduce_short_msg = flag;
    }
    if ((value = getenv("MV2_SHMEM_ALLREDUCE_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.shmem_allreduce_msg = flag;
    }
    if ((value = getenv("MV2_SHMEM_REDUCE_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.shmem_reduce_msg = flag;
    }
    if ((value = getenv("MV2_USE_SHMEM_BCAST")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) enable_shmem_bcast = 1;
        else enable_shmem_bcast = 0;
    }
    if ((value = getenv("MV2_SHMEM_BCAST_MSG")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) shmem_bcast_threshold = flag;
    }
    if ((value = getenv("MV2_ALLTOALL_DREG_DISABLE_THRESHOLD")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) alltoall_dreg_disable_threshold = flag;
    }
    if ((value = getenv("MV2_ALLTOALL_DREG_DISABLE")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 1) { 
          alltoall_dreg_disable = 1;
        } 
	    if (flag <= 0) { 
          alltoall_dreg_disable = 0;
        } 
    }
    


    init_thread_reg();
}
#endif /* defined(_OSU_MVAPICH_) */
