/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpi_init.h"
#include "mpiu_thread.h"

static MPI_Comm progress_comm;
static MPIU_Thread_id_t progress_thread_id;
static MPIU_Thread_mutex_t progress_mutex;
static MPIU_Thread_cond_t progress_cond;
static volatile int progress_thread_done = 0;

#undef FUNCNAME
#define FUNCNAME MPIR_Init_async_thread
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static void progress_fn(void * data)
{
#if MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED
    int mpi_errno = MPI_SUCCESS;
    MPIU_THREADPRIV_DECL;

    /* Explicitly add CS_ENTER/EXIT since this thread is created from
     * within an internal function and will call NMPI functions
     * directly. */
    /* FIXME: The CS_ENTER/EXIT code should be abstracted out
     * correctly, instead of relying on the #if protection here. */
#if MPIU_THREAD_GRANULARITY == MPIU_THREAD_GRANULARITY_GLOBAL
    MPID_CS_ENTER();
#endif

    /* FIXME: We assume that waiting on some request forces progress
     * on all requests. With fine-grained threads, will this still
     * work as expected? We can imagine an approach where a request on
     * a non-conflicting communicator would not touch the remaining
     * requests to avoid locking issues. Once the fine-grained threads
     * code is fully functional, we need to revisit this and, if
     * appropriate, either change what we do in this thread, or delete
     * this comment. */

    MPIU_THREADPRIV_GET;

    MPIR_Nest_incr();
    mpi_errno = NMPI_Recv(NULL, 0, MPI_CHAR, 0, 0, progress_comm, MPI_STATUS_IGNORE);
    MPIU_Assert(!mpi_errno);
    MPIR_Nest_decr();

    /* Send a signal to the main thread saying we are done */
    MPIU_Thread_mutex_lock(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    progress_thread_done = 1;

    MPIU_Thread_mutex_unlock(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPIU_Thread_cond_signal(&progress_cond, &mpi_errno);
    MPIU_Assert(!mpi_errno);

#if MPIU_THREAD_GRANULARITY == MPIU_THREAD_GRANULARITY_GLOBAL
    MPID_CS_EXIT();
#endif

#endif /* MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED */
    return;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Init_async_thread
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Init_async_thread(void)
{
    int mpi_errno = MPI_SUCCESS;
#if MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED
    MPIU_THREADPRIV_DECL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIR_INIT_ASYNC_THREAD);

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIR_INIT_ASYNC_THREAD);

    MPIU_THREADPRIV_GET;

    /* Dup comm world for the progress thread */
    MPIR_Nest_incr();
    mpi_errno = NMPI_Comm_dup(MPI_COMM_SELF, &progress_comm);
    MPIU_Assert(!mpi_errno);
    MPIR_Nest_decr();

    MPIU_Thread_cond_create(&progress_cond, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPIU_Thread_mutex_create(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPIU_Thread_create((MPIU_Thread_func_t) progress_fn, NULL, &progress_thread_id, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPID_MPI_FUNC_EXIT(MPID_STATE_MPIR_INIT_ASYNC_THREAD);

#endif /* MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED */
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Finalize_async_thread
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Finalize_async_thread(void)
{
    int mpi_errno = MPI_SUCCESS;
#if MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED
    MPIU_THREADPRIV_DECL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIR_FINALIZE_ASYNC_THREAD);

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIR_FINALIZE_ASYNC_THREAD);

    MPIU_THREADPRIV_GET;

    MPIR_Nest_incr();
    mpi_errno = NMPI_Send(NULL, 0, MPI_CHAR, 0, 0, progress_comm);
    MPIU_Assert(!mpi_errno);
    MPIR_Nest_decr();

#if MPIU_THREAD_GRANULARITY == MPIU_THREAD_GRANULARITY_GLOBAL
    MPID_CS_EXIT();
#endif

    MPIU_Thread_mutex_lock(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    while (!progress_thread_done) {
        MPIU_Thread_cond_wait(&progress_cond, &progress_mutex, &mpi_errno);
        MPIU_Assert(!mpi_errno);
    }

    MPIU_Thread_mutex_unlock(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

#if MPIU_THREAD_GRANULARITY == MPIU_THREAD_GRANULARITY_GLOBAL
    MPID_CS_ENTER();
#endif

    MPIU_Thread_cond_destroy(&progress_cond, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPIU_Thread_mutex_destroy(&progress_mutex, &mpi_errno);
    MPIU_Assert(!mpi_errno);

    MPID_MPI_FUNC_EXIT(MPID_STATE_MPIR_FINALIZE_ASYNC_THREAD);

#endif /* MPICH_THREAD_LEVEL >= MPI_THREAD_SERIALIZED */
    return mpi_errno;
}
