/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "ch3i_progress.h"

int MPIDI_CH3I_shm_read_active = 0;
int MPIDI_CH3I_shm_write_active = 0;
int MPIDI_CH3I_sock_read_active = 0;
int MPIDI_CH3I_sock_write_active = 0;
int MPIDI_CH3I_active_flag = 0;

MPIDU_Sock_set_t MPIDI_CH3I_sock_set = NULL; 

#if defined(USE_FIXED_SPIN_WAITS) || !defined(MPID_CPU_TICK)
/****************************************/
/*                                      */
/*   Fixed spin waits progress engine   */
/*                                      */
/****************************************/

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress(int is_blocking, MPID_Progress_state *state)
{
    int mpi_errno = MPI_SUCCESS;
    int rc;
#ifdef MPICH_DBG_OUTPUT
    int count;
#endif
    int bShmProgressMade;
    MPIDU_Sock_event_t event;
    unsigned completions = MPIDI_CH3I_progress_completion_count;
    MPIDI_CH3I_Shmem_queue_info info;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;
    static int spin_count = 1;
    static int msg_queue_count = 0;
#if defined(HAVE_SHARED_PROCESS_READ) && !defined(HAVE_WINDOWS_H)
    char filename[256];
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_PROGRESS);
#ifdef USE_SLEEP_YIELD
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_SLEEP_YIELD);
#else
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);
#endif

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_PROGRESS);

#ifdef MPICH_DBG_OUTPUT
    if (is_blocking)
    {
	MPIDI_DBG_PRINTF((50, FCNAME, "entering, blocking=%s", is_blocking ? "true" : "false"));
    }
#endif
    do
    {
	/* make progress on the shared memory queues */

	bShmProgressMade = FALSE;
	if (MPIDI_CH3I_Process.shm_reading_list)
	{
	    rc = MPIDI_CH3I_SHM_read_progress(MPIDI_CH3I_Process.shm_reading_list, 0, &vc_ptr, &num_bytes);
	    if (rc == MPI_SUCCESS)
	    {
		MPIDI_DBG_PRINTF((50, FCNAME, "MPIDI_CH3I_SHM_read_progress reported %d bytes read", num_bytes));
		mpi_errno = MPIDI_CH3I_Handle_shm_read(vc_ptr, num_bytes);
		if (mpi_errno != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
		    goto fn_exit;
		}
		bShmProgressMade = TRUE;
	    }
	    else
	    {
		if (rc != SHM_WAIT_TIMEOUT)
		{
		    /*MPIDI_err_printf("MPIDI_CH3_Progress", "MPIDI_CH3I_SHM_read_progress returned error %d\n", rc);*/
		    mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**shm_read_progress", 0);
		    goto fn_exit;
		}
	    }
	}

	if (MPIDI_CH3I_Process.shm_writing_list)
	{
	    vc_ptr = MPIDI_CH3I_Process.shm_writing_list;
	    while (vc_ptr)
	    {
		if (vc_ptr->ch.send_active != NULL)
		{
		    rc = MPIDI_CH3I_SHM_write_progress(vc_ptr);
		    if (rc == MPI_SUCCESS)
		    {
			bShmProgressMade = TRUE;
		    }
		    else if (rc != SHM_WAIT_TIMEOUT)
		    {
			mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			goto fn_exit;
		    }
		}
		vc_ptr = vc_ptr->ch.shm_next_writer;
	    }
	}

#if 0
	if (bShmProgressMade) /* This variable is not determined correctly */
	{
	    spin_count = 1;
#ifdef USE_SLEEP_YIELD
	    MPIDI_Sleep_yield_count = 0;
#endif
	    continue;
	}
#endif

	if (spin_count >= MPIDI_Process.my_pg->ch.nShmWaitSpinCount)
	{
#ifdef USE_SLEEP_YIELD
	    if (spin_count >= MPIDI_Process.my_pg->ch.nShmWaitYieldCount)
	    {
		MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_SLEEP_YIELD);
		MPIDU_Sleep_yield();
		MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_SLEEP_YIELD);
	    }
	    else
	    {
		MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
		MPIDU_Yield();
		MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
	    }
#else
	    MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
	    MPIDU_Yield();
	    MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
	    spin_count = 1;
#endif
	}
	/* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
	   macros around them but this one does not? */
	else MPIDU_Yield(); /* always yield for now */
	spin_count++;

	if (spin_count > (MPIDI_Process.my_pg->ch.nShmWaitSpinCount >> 1) )
	{
	    /* make progress on the sockets */

	    mpi_errno = MPIDU_Sock_wait(MPIDI_CH3I_sock_set, 0, &event);
	    if (mpi_errno == MPI_SUCCESS)
	    {
		mpi_errno = MPIDI_CH3I_Progress_handle_sock_event(&event);
		if (mpi_errno != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_handle_sock_op", 0);
		    goto fn_exit;
		}
	    }
	    else
	    {
		if (MPIR_ERR_GET_CLASS(mpi_errno) != MPIDU_SOCK_ERR_TIMEOUT)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_sock_wait", 0);
		    goto fn_exit;
		}
		mpi_errno = MPI_SUCCESS;
		/* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
		   macros around them but this one does not? */
		MPIDU_Yield();
	    }
	}

	if (((msg_queue_count++ % MPIDI_CH3I_MSGQ_ITERATIONS) == 0) || !is_blocking)
	{
	    /* check for new shmem queue connection requests */
	    rc = MPIDI_CH3I_BootstrapQ_recv_msg(MPIDI_Process.my_pg->ch.bootstrapQ, &info, sizeof(info), &num_bytes, FALSE);
	    if (rc != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**boot_recv", 0);
		goto fn_exit;
	    }
#ifdef MPICH_DBG_OUTPUT
	    /*MPIU_Assert(num_bytes == 0 || num_bytes == sizeof(info));*/
	    if (num_bytes != 0 && num_bytes != sizeof(info))
	    {
		mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**bootqmsg", "**bootqmsg %d", num_bytes);
		goto fn_exit;
	    }
#endif
	    if (num_bytes)
	    {
		MPIDI_PG_t *pg;

		MPIDI_PG_Find(info.pg_id, &pg);
		MPIDI_PG_Get_vc(pg, info.pg_rank, &vc_ptr);
		/*
		printf("attaching to shared memory queue:\nVC.rank %d\nVC.pg_id <%s>\nPG.id <%s>\n",
		    vc_ptr->pg_rank, vc_ptr->pg->id, pg->id);
		fflush(stdout);
		*/
		/*vc_ptr = &MPIDI_Process.my_pg->ch.vc_table[info.pg_rank];*/
		rc = MPIDI_CH3I_SHM_Attach_to_mem(&info.info, &vc_ptr->ch.shm_read_queue_info);
		if (rc != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**attach_to_mem", "**attach_to_mem %d", vc_ptr->ch.shm_read_queue_info.error);
		    goto fn_exit;
		}
		MPIU_DBG_PRINTF(("attached to queue from process %d\n", info.pg_rank));
#ifdef HAVE_SHARED_PROCESS_READ
#ifdef HAVE_WINDOWS_H
		/*MPIU_DBG_PRINTF(("Opening process[%d]: %d\n", i, pSharedProcess[i].nPid));*/
		vc_ptr->ch.hSharedProcessHandle =
		    OpenProcess(STANDARD_RIGHTS_REQUIRED | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 
		    FALSE, info.pid);
		if (vc_ptr->ch.hSharedProcessHandle == NULL)
		{
		    int err = GetLastError();
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**OpenProcess", "**OpenProcess %d %d", info.pg_rank, err);
		    return mpi_errno;
		}
#else
		MPIU_Snprintf(filename, 256, "/proc/%d/mem", info.pid);
		vc_ptr->ch.nSharedProcessID = info.pid;
		vc_ptr->ch.nSharedProcessFileDescriptor = open(filename, O_RDWR/*O_RDONLY*/);
		if (vc_ptr->ch.nSharedProcessFileDescriptor == -1)
		{
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**open", "**open %s %d %d", filename, info.pid, errno);
		    return mpi_errno;
		}
#endif
#endif
		/*vc_ptr->ch.state = MPIDI_CH3I_VC_STATE_CONNECTED;*/ /* we are read connected but not write connected */
		vc_ptr->ch.shm_read_connected = 1;
		vc_ptr->ch.bShm = TRUE;
		vc_ptr->ch.read_shmq = vc_ptr->ch.shm_read_queue_info.addr;/*info.info.addr;*/
		MPIU_DBG_PRINTF(("read_shmq = %p\n", vc_ptr->ch.read_shmq));
		vc_ptr->ch.shm_reading_pkt = TRUE;
		/* add this VC to the global list to be shm_waited on */
		MPIDI_CH3I_SHM_Add_to_reader_list(vc_ptr);
	    }
	}
    }
    while (completions == MPIDI_CH3I_progress_completion_count && is_blocking);

fn_exit:
#ifdef MPICH_DBG_OUTPUT
    count = MPIDI_CH3I_progress_completion_count - completions;
    if (is_blocking)
    {
	MPIDI_DBG_PRINTF((50, FCNAME, "exiting, count=%d", count));
    }
    else
    {
	if (count > 0)
	{
	    MPIDI_DBG_PRINTF((50, FCNAME, "exiting (non-blocking), count=%d", count));
	}
    }
#endif
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_PROGRESS);
    return mpi_errno;
}

#endif /* USE_FIXED_SPIN_WAITS */

#if defined(USE_ADAPTIVE_PROGRESS) && defined(MPID_CPU_TICK)
/********************************/
/*                              */
/*   Adaptive progress engine   */
/*                              */
/********************************/

#define MPIDI_CH3I_UPDATE_ITERATIONS    10
#define MPID_SINGLE_ACTIVE_FACTOR      100

/* FIXME: What is this routine for?  Who uses it? Why is it not a static function? */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Message_queue_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Message_queue_progress()
{
    MPIDI_CH3I_Shmem_queue_info info;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;
    int mpi_errno;
#if defined(HAVE_SHARED_PROCESS_READ) && !defined(HAVE_WINDOWS_H)
    char filename[256];
#endif

    /* check for new shmem queue connection requests */
    /*printf("<%dR>", MPIR_Process.comm_world->rank);fflush(stdout);*/
    mpi_errno = MPIDI_CH3I_BootstrapQ_recv_msg(
	MPIDI_Process.my_pg->ch.bootstrapQ, &info, sizeof(info), 
	&num_bytes, FALSE);
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**boot_recv", 0);
	return mpi_errno;
    }
#ifdef MPICH_DBG_OUTPUT
    /*MPIU_Assert(num_bytes == 0 || num_bytes == sizeof(info));*/
    if (num_bytes != 0 && num_bytes != sizeof(info))
    {
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**bootqmsg", "**bootqmsg %d", num_bytes);
	return mpi_errno;
    }
#endif
    if (num_bytes)
    {
	MPIDI_PG_t *pg;

	MPIDI_PG_Find(info.pg_id, &pg);
	MPIDI_PG_Get_vc(pg, info.pg_rank, &vc_ptr);
	/*vc_ptr = &MPIDI_Process.my_pg->ch.vc_table[info.pg_rank];*/
	/*printf("rank %d attaching to shm(%s) from rank %d\n", MPIR_Process.comm_world->rank, info.info.name, info.pg_rank);fflush(stdout);*/
	mpi_errno = MPIDI_CH3I_SHM_Attach_to_mem(
	    &info.info, &vc_ptr->ch.shm_read_queue_info);
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**MPIDI_CH3I_SHM_Attach_to_mem", "**MPIDI_CH3I_SHM_Attach_to_mem %d", vc_ptr->ch.shm_read_queue_info.error);
	    return mpi_errno;
	}
	MPIU_DBG_PRINTF(("attached to queue from process %d\n", info.pg_rank));
#ifdef HAVE_SHARED_PROCESS_READ
#ifdef HAVE_WINDOWS_H
	/*MPIU_DBG_PRINTF(("Opening process[%d]: %d\n", i, pSharedProcess[i].nPid));*/
	vc_ptr->ch.hSharedProcessHandle =
	    OpenProcess(STANDARD_RIGHTS_REQUIRED | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 
	    FALSE, info.pid);
	if (vc_ptr->ch.hSharedProcessHandle == NULL)
	{
	    int err = GetLastError();
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**OpenProcess", "**OpenProcess %d %d", info.pg_rank, err);
	    return mpi_errno;
	}
#else
	MPIU_Snprintf(filename, 256, "/proc/%d/mem", info.pid);
	vc_ptr->ch.nSharedProcessID = info.pid;
	vc_ptr->ch.nSharedProcessFileDescriptor = open(filename, O_RDWR/*O_RDONLY*/);
	if (vc_ptr->ch.nSharedProcessFileDescriptor == -1)
	{
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**open", "**open %s %d %d", filename, info.pid, errno);
	    return mpi_errno;
	}
#endif
#endif
	/*vc_ptr->ch.state = MPIDI_CH3I_VC_STATE_CONNECTED;*/ /* we are read connected but not write connected */
	vc_ptr->vc_ptr->ch.shm_read_connected = 1;
	vc_ptr->ch.bShm = TRUE;
	vc_ptr->ch.read_shmq = vc_ptr->ch.shm_read_queue_info.addr;
	MPIU_DBG_PRINTF(("read_shmq = %p\n", vc_ptr->ch.read_shmq));
	vc_ptr->ch.shm_reading_pkt = TRUE;
	/* add this VC to the global list to be shm_waited on */
	/*printf("vc added to reading list.\n");fflush(stdout);*/
	MPIDI_CH3I_SHM_Add_to_reader_list(vc_ptr);
    }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Progress_test()
{
    /* This function has a problem that is #if 0'd out.
     * The commented out code causes test to only probe the message queue for connection attempts
     * every MPIDI_CH3I_MSGQ_ITERATIONS iterations.  This can delay shm connection formation for 
     * codes that call test infrequently.
     * But the uncommented code also has the problem that the overhead of checking the message queue
     * is incurred with every call to test.
     */
    int mpi_errno = MPI_SUCCESS;
    int rc;
    MPIDU_Sock_event_t event;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;
#if 0
    static int msgqIter = 0;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_TEST);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_TEST);


    if (MPIDI_CH3I_Process.shm_reading_list)
    {
	rc = MPIDI_CH3I_SHM_read_progress(
	    MPIDI_CH3I_Process.shm_reading_list,
	    0, &vc_ptr, &num_bytes);
	if (rc == MPI_SUCCESS)
	{
	    MPIDI_DBG_PRINTF((50, FCNAME, "MPIDI_CH3I_SHM_read_progress reported %d bytes read", num_bytes));
	    mpi_errno = MPIDI_CH3I_Handle_shm_read(vc_ptr, num_bytes);
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_test", 0);
		goto fn_exit;
	    }
	}
    }

    if (MPIDI_CH3I_Process.shm_writing_list)
    {
	vc_ptr = MPIDI_CH3I_Process.shm_writing_list;
	while (vc_ptr)
	{
	    if (vc_ptr->ch.send_active != NULL)
	    {
		rc = MPIDI_CH3I_SHM_write_progress(vc_ptr);
		if (rc != MPI_SUCCESS && rc != SHM_WAIT_TIMEOUT)
		{
		    mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
		    goto fn_exit;
		}
	    }
	    vc_ptr = vc_ptr->ch.shm_next_writer;
	}
    }

    mpi_errno = MPIDU_Sock_wait(MPIDI_CH3I_sock_set, 0, &event);
    if (mpi_errno == MPI_SUCCESS)
    {
	mpi_errno = MPIDI_CH3I_Progress_handle_sock_event(&event);
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**handle_sock_op", 0);
	    return mpi_errno;
	}
	/*active = active | MPID_CH3I_SOCK_BIT;*/
	MPIDI_CH3I_active_flag |= MPID_CH3I_SOCK_BIT;
    }
    else
    {
	if (MPIR_ERR_GET_CLASS(mpi_errno) != MPIDU_SOCK_ERR_TIMEOUT)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**test_sock_wait", 0);
	    goto fn_exit;
	}
	mpi_errno = MPI_SUCCESS;
    }

#if 0
    if (msgqIter++ == MPIDI_CH3I_MSGQ_ITERATIONS)
    {
	msgqIter = 0;
	/*printf("[%d] calling message queue progress from test.\n", MPIR_Process.comm_world->rank);fflush(stdout);*/
	mpi_errno = MPID_CH3I_Message_queue_progress();
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**mqp_failure", 0);
	}
    }
#else
    mpi_errno = MPIDI_CH3I_Message_queue_progress();
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**mqp_failure", 0);
    }
#endif

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_TEST);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_wait
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Progress_wait(MPID_Progress_state *state)
{
    int mpi_errno = MPI_SUCCESS;
    int rc;
    MPIDU_Sock_event_t event;
    unsigned completions = MPIDI_CH3I_progress_completion_count;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;

    /*static int active = 0;*/
    static int updateIter = 0;
    static int msgqIter = MPIDI_CH3I_MSGQ_ITERATIONS;
    MPID_CPU_Tick_t start, end;

    static MPID_CPU_Tick_t shmTicks = 0;
    static int shmIter = 0;
    static int shmReps = 1;
    static int shmTotalReps = 0;
    static int spin_count = 1;

    static MPID_CPU_Tick_t sockTicks = 0;
    static int sockIter = 0;
    static int sockReps = 1;
    static int sockTotalReps = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_WAIT);
    /*MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);*/

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_WAIT);

    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));
    do
    {
	/* make progress on the shared memory queues */

	if (sockIter != 0)
	    goto skip_shm_loop;

	MPID_CPU_TICK(&start);
	for (; shmIter<shmReps; shmIter++, shmTotalReps++)
	{
	    if (MPIDI_CH3I_Process.shm_reading_list)
	    {
		rc = MPIDI_CH3I_SHM_read_progress(
		    MPIDI_CH3I_Process.shm_reading_list,
		    0, &vc_ptr, &num_bytes);
		if (rc == MPI_SUCCESS)
		{
		    MPIDI_DBG_PRINTF((50, FCNAME, "MPIDI_CH3I_SHM_read_progress reported %d bytes read", num_bytes));
		    mpi_errno = MPIDI_CH3I_Handle_shm_read(vc_ptr, num_bytes);
		    if (mpi_errno != MPI_SUCCESS)
		    {
			mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			goto fn_exit;
		    }
		}
		else
		{
		    if (rc != SHM_WAIT_TIMEOUT)
		    {
			/*MPIDI_err_printf("MPIDI_CH3_Progress", "MPIDI_CH3I_SHM_read_progress returned error %d\n", rc);*/
			mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			goto fn_exit;
		    }
		    /*
		    if (rc == SHM_WAIT_TIMEOUT_WHILE_ACTIVE)
		    {
			active = active | MPID_CH3I_SHM_BIT;
		    }
		    */
		}
		if (MPIDI_CH3I_shm_read_active)
		    spin_count = 1;
		if (completions != MPIDI_CH3I_progress_completion_count)
		{
		    MPIDI_CH3I_active_flag |= MPID_CH3I_SHM_BIT;
		    /*
		    active = active | MPID_CH3I_SHM_BIT;
		    spin_count = 1;
		    */
		    shmIter++;
		    shmTotalReps++;
		    goto after_shm_loop;
		}
	    }

	    if (MPIDI_CH3I_Process.shm_writing_list)
	    {
		vc_ptr = MPIDI_CH3I_Process.shm_writing_list;
		while (vc_ptr)
		{
		    if (vc_ptr->ch.send_active != NULL)
		    {
			rc = MPIDI_CH3I_SHM_write_progress(vc_ptr);
			if (rc == MPI_SUCCESS)
			{
			    /*active = active | MPID_CH3I_SHM_BIT;*/
			    if (completions != MPIDI_CH3I_progress_completion_count)
			    {
				MPIDI_CH3I_active_flag |= MPID_CH3I_SHM_BIT;
				spin_count = 1;
				shmIter++;
				shmTotalReps++;
				goto after_shm_loop;
			    }
			}
			else if (rc != SHM_WAIT_TIMEOUT)
			{
			    mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			    goto fn_exit;
			}
		    }
		    vc_ptr = vc_ptr->ch.shm_next_writer;
		}
	    }
/*
	    if (spin_count++ >= MPIDI_Process.my_pg->ch.nShmWaitSpinCount)
	    {
		MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
		MPIDU_Yield();
		MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
		spin_count = 1;
	    }
*/
	    /* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
	       macros around them but this one does not? */
	    MPIDU_Yield();
	}
after_shm_loop:
	if (shmIter == shmReps)
	{
	    shmIter = 0;
	}
	MPID_CPU_TICK(&end);
	shmTicks += end - start;

	if (shmIter != 0)
	    goto skip_sock_loop;

skip_shm_loop:
	MPID_CPU_TICK(&start);
	for (; sockIter<sockReps; sockIter++, sockTotalReps++)
	{
	    /* make progress on the sockets */

	    mpi_errno = MPIDU_Sock_wait(MPIDI_CH3I_sock_set, 0, &event);
	    if (mpi_errno == MPI_SUCCESS)
	    {
		mpi_errno = MPIDI_CH3I_Progress_handle_sock_event(&event);
		if (mpi_errno != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_sock_wait", 0);
		    goto fn_exit;
		}
		/*active = active | MPID_CH3I_SOCK_BIT;*/
		MPIDI_CH3I_active_flag |= MPID_CH3I_SOCK_BIT;
	    }
	    else
	    {
		if (MPIR_ERR_GET_CLASS(mpi_errno) != MPIDU_SOCK_ERR_TIMEOUT)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_sock_wait", 0);
		    goto fn_exit;
		}
		/* comment out this line to test the error functions */
		mpi_errno = MPI_SUCCESS;
		/* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
		   macros around them but this one does not? */
		MPIDU_Yield();
	    }
	    if (completions != MPIDI_CH3I_progress_completion_count)
	    {
		sockIter++;
		sockTotalReps++;
		goto after_sock_loop;
	    }
	}
after_sock_loop:
	if (sockIter == sockReps)
	{ 
	    sockIter = 0;
	    updateIter++;
	}
	MPID_CPU_TICK(&end);
	sockTicks += end - start;

skip_sock_loop:
	if (updateIter == MPIDI_CH3I_UPDATE_ITERATIONS)
	{
	    updateIter = 0;
	    switch (MPIDI_CH3I_active_flag)
	    {
	    case MPID_CH3I_SHM_BIT:
		/* only shared memory has been active */
		/* give shm MPID_SINGLE_ACTIVE_FACTOR cycles for every 1 sock cycle */
		shmReps = (int) (
		    ( sockTicks * (MPID_CPU_Tick_t)MPID_SINGLE_ACTIVE_FACTOR * (MPID_CPU_Tick_t)shmTotalReps ) 
		    / (MPID_CPU_Tick_t)sockTotalReps 
		    / shmTicks);
		if (shmReps < 1)
		    shmReps = 1;
		if (shmReps > 256)
		{
		    shmReps = 256;
		}
		sockReps = 1;
		/*MPIU_DBG_PRINTF(("(SHM_BIT: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    case MPID_CH3I_SOCK_BIT:
		/* only sockets have been active */
		/* give sock MPID_SINGLE_ACTIVE_FACTOR cycles for every 1 shm cycle */
		sockReps = (int) ( 
		    ( shmTicks * (MPID_CPU_Tick_t)MPID_SINGLE_ACTIVE_FACTOR * (MPID_CPU_Tick_t)sockTotalReps )
		    / (MPID_CPU_Tick_t)shmTotalReps
		    / sockTicks );
		if (sockReps < 1)
		    sockReps = 1;
		if (sockReps > 100)
		{
		    sockReps = 100;
		}
		shmReps = 1;
		/*MPIU_DBG_PRINTF(("(SOCK_BIT: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    case MPID_CH3I_SHM_BIT + MPID_CH3I_SOCK_BIT:
		/* both channels have been active */
		/* give each channel 50% of the spin cycles */
		shmReps = (int)
		    (
		     (
		      (MPID_CPU_Tick_t)((shmTicks + sockTicks) >> 1)
		      * (MPID_CPU_Tick_t)shmTotalReps
		     ) / shmTicks
		    ) / MPIDI_CH3I_UPDATE_ITERATIONS;
		if (shmReps < 1)
		    shmReps = 1;
		if (shmReps > 256)
		{
		    shmReps = 256;
		}
		sockReps = (int)
		    (
		     (
		      (MPID_CPU_Tick_t)((shmTicks + sockTicks) >> 1)
		      * (MPID_CPU_Tick_t)sockTotalReps
		     ) / sockTicks
		    ) / MPIDI_CH3I_UPDATE_ITERATIONS;
		if (sockReps < 1)
		    sockReps = 1;
		if (sockReps > 100)
		{
		    sockReps = 100;
		}
		/*MPIU_DBG_PRINTF(("(BOTH_BITS: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    default:
		/* neither channel has been active */
		/* leave the state in its previous state */
		/*printf(".");*/
		break;
	    }
	    /*
	    if (shmReps > 500 || sockReps > 100)
	    {
		printf("[%d] SHMREPS=%d, SOCKREPS=%d\n", MPIR_Process.comm_world->rank, shmReps, sockReps);
	    }
	    */
	    MPIDI_CH3I_active_flag = 0;
	    shmTotalReps = 0;
	    shmTicks = 0;
	    shmIter = 0;
	    sockTotalReps = 0;
	    sockTicks = 0;
	    sockIter = 0;
	}

	if (msgqIter++ == MPIDI_CH3I_MSGQ_ITERATIONS)
	{
	    msgqIter = 0;
	    /*printf("[%d] calling message queue progress\n", MPIR_Process.comm_world->rank);fflush(stdout);*/
	    /* printf("%d",2*MPIR_Process.comm_world->rank); */
	    mpi_errno = MPIDI_CH3I_Message_queue_progress();
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
						 "**mqp_failure", 0);
		goto fn_exit;
	    }
	    /* printf("%d",2*MPIR_Process.comm_world->rank+1); */
	}
    }
    while (completions == MPIDI_CH3I_progress_completion_count);

fn_exit:
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting, count=%d", MPIDI_CH3I_progress_completion_count - completions));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_WAIT);
    return mpi_errno;
}

#endif /* USE_ADAPTIVE_PROGRESS */

#ifdef USE_FIXED_ACTIVE_PROGRESS
/**********************************************************/
/*                                                        */
/*   Fixed active progress engine                         */
/*                                                        */
/*   Like the adaptive engine but active reps are fixed   */
/*   instead of calculated from CPU time.                 */
/*                                                        */
/**********************************************************/

#define MPIDI_CH3I_UPDATE_ITERATIONS    10
#define MPID_SINGLE_ACTIVE_FACTOR      100

/* FIXME: What is this routine for?  Who uses it? Why is it not a static function? */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Message_queue_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Message_queue_progress()
{
    MPIDI_CH3I_Shmem_queue_info info;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;
    int rc, mpi_errno;
#if defined(HAVE_SHARED_PROCESS_READ) && !defined(HAVE_WINDOWS_H)
    char filename[256];
#endif

    /* check for new shmem queue connection requests */
    rc = MPIDI_CH3I_BootstrapQ_recv_msg(
	MPIDI_Process.my_pg->ch.bootstrapQ, &info, sizeof(info), 
	&num_bytes, FALSE);
    if (rc != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**boot_recv", 0);
	return mpi_errno;
    }
#ifdef MPICH_DBG_OUTPUT
    /*MPIU_Assert(num_bytes == 0 || num_bytes == sizeof(info));*/
    if (num_bytes != 0 && num_bytes != sizeof(info))
    {
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**bootqmsg", "**bootqmsg %d", num_bytes);
	return mpi_errno;
    }
#endif
    if (num_bytes)
    {
	MPIDI_PG_t *pg;

	MPIDI_PG_Find(info.pg_id, &pg);
	MPIDI_PG_Get_vc(pg, info.pg_rank, &vc_ptr);
	/*vc_ptr = &MPIDI_Process.my_pg->ch.vc_table[info.pg_rank];*/
	rc = MPIDI_CH3I_SHM_Attach_to_mem(
	    &info.info, &vc_ptr->ch.shm_read_queue_info);
	if (rc != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**MPIDI_CH3I_SHM_Attach_to_mem", "**MPIDI_CH3I_SHM_Attach_to_mem %d", vc_ptr->ch.shm_read_queue_info.error); /*"MPIDI_CH3I_SHM_Attach_to_mem failed, error %d\n", vc_ptr->ch.shm_read_queue_info.error);*/
	    return mpi_errno;
	}
	MPIU_DBG_PRINTF(("attached to queue from process %d\n", info.pg_rank));
#ifdef HAVE_SHARED_PROCESS_READ
#ifdef HAVE_WINDOWS_H
	/*MPIU_DBG_PRINTF(("Opening process[%d]: %d\n", i, pSharedProcess[i].nPid));*/
	vc_ptr->ch.hSharedProcessHandle =
	    OpenProcess(STANDARD_RIGHTS_REQUIRED | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 
	    FALSE, info.pid);
	if (vc_ptr->ch.hSharedProcessHandle == NULL)
	{
	    int err = GetLastError();
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**OpenProcess", "**OpenProcess %d %d", info.pg_rank, err);
	    return mpi_errno;
	}
#else
	MPIU_Snprintf(filename, 256, "/proc/%d/mem", info.pid);
	vc_ptr->ch.nSharedProcessID = info.pid;
	vc_ptr->ch.nSharedProcessFileDescriptor = open(filename, O_RDWR/*O_RDONLY*/);
	if (vc_ptr->ch.nSharedProcessFileDescriptor == -1)
	{
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**open", "**open %s %d %d", filename, info.pid, errno);
	    return mpi_errno;
	}
#endif
#endif
	/*vc_ptr->ch.state = MPIDI_CH3I_VC_STATE_CONNECTED;*/ /* we are read connected but not write connected */
	vc_ptr->vc_ptr->ch.shm_read_connected = 1;
	vc_ptr->ch.bShm = TRUE;
	vc_ptr->ch.read_shmq = vc_ptr->ch.shm_read_queue_info.addr;
	MPIU_DBG_PRINTF(("read_shmq = %p\n", vc_ptr->ch.read_shmq));
	vc_ptr->ch.shm_reading_pkt = TRUE;
	/* add this VC to the global list to be shm_waited on */
	MPIDI_CH3I_SHM_Add_to_reader_list(vc_ptr);
    }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress(int is_blocking, MPID_Progress_state *state)
{
    int mpi_errno = MPI_SUCCESS;
    int rc;
    MPIDU_Sock_event_t event;
    unsigned completions = MPIDI_CH3I_progress_completion_count;
    int num_bytes;
    MPIDI_VC_t *vc_ptr;

    /*static int active = 0;*/
    static int updateIter = 0;
    static int msgqIter = 0;
    MPID_CPU_Tick_t start, end;

    static MPID_CPU_Tick_t shmTicks = 0;
    static int shmIter = 0;
    static int shmReps = 1;
    static int shmTotalReps = 0;
    static int spin_count = 1;

    static MPID_CPU_Tick_t sockTicks = 0;
    static int sockIter = 0;
    static int sockReps = 1;
    static int sockTotalReps = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS);
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS);

#ifdef MPICH_DBG_OUTPUT
    if (is_blocking)
    {
	MPIDI_DBG_PRINTF((50, FCNAME, "entering, blocking=%s", is_blocking ? "true" : "false"));
    }
#endif
    do
    {
	/* make progress on the shared memory queues */

	if (sockIter != 0)
	    goto skip_shm_loop;

	MPID_CPU_TICK(&start);
	for (; shmIter<shmReps; shmIter++, shmTotalReps++)
	{
	    if (MPIDI_CH3I_Process.shm_reading_list)
	    {
		rc = MPIDI_CH3I_SHM_read_progress(
		    MPIDI_CH3I_Process.shm_reading_list,
		    0, &vc_ptr, &num_bytes);
		if (rc == MPI_SUCCESS)
		{
		    MPIDI_DBG_PRINTF((50, FCNAME, "MPIDI_CH3I_SHM_read_progress reported %d bytes read", num_bytes));
		    mpi_errno = MPIDI_CH3I_Handle_shm_read(vc_ptr, num_bytes);
		    if (mpi_errno != MPI_SUCCESS)
		    {
			mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			goto fn_exit;
		    }
		}
		else
		{
		    if (rc != SHM_WAIT_TIMEOUT)
		    {
			MPIDI_err_printf("MPIDI_CH3_Progress", "MPIDI_CH3I_SHM_read_progress returned error %d\n", rc);
			mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			goto fn_exit;
		    }
		    /*
		    if (rc == SHM_WAIT_TIMEOUT_WHILE_ACTIVE)
		    {
			active = active | MPID_CH3I_SHM_BIT;
		    }
		    */
		}
		if (MPIDI_CH3I_shm_read_active)
		    spin_count = 1;
		if (completions != MPIDI_CH3I_progress_completion_count)
		{
		    MPIDI_CH3I_active_flag |= MPID_CH3I_SHM_BIT;
		    /*
		    active = active | MPID_CH3I_SHM_BIT;
		    spin_count = 1;
		    */
		    shmIter++;
		    shmTotalReps++;
		    goto after_shm_loop;
		}
	    }

	    if (MPIDI_CH3I_Process.shm_writing_list)
	    {
		vc_ptr = MPIDI_CH3I_Process.shm_writing_list;
		while (vc_ptr)
		{
		    if (vc_ptr->ch.send_active != NULL)
		    {
			rc = MPIDI_CH3I_SHM_write_progress(vc_ptr);
			if (rc == MPI_SUCCESS)
			{
			    /*active = active | MPID_CH3I_SHM_BIT;*/
			    if (completions != MPIDI_CH3I_progress_completion_count)
			    {
				MPIDI_CH3I_active_flag |= MPID_CH3I_SHM_BIT;
				spin_count = 1;
				shmIter++;
				shmTotalReps++;
				goto after_shm_loop;
			    }
			}
			else if (rc != SHM_WAIT_TIMEOUT)
			{
			    mpi_errno = MPIR_Err_create_code(rc, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress", 0);
			    goto fn_exit;
			}
		    }
		    vc_ptr = vc_ptr->ch.shm_next_writer;
		}
	    }
	    /*
	    if (spin_count++ >= MPIDI_Process.my_pg->ch.nShmWaitSpinCount)
	    {
		MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
		MPIDU_Yield();
		MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
		spin_count = 1;
	    }
	    */
	    /* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
	       macros around them but this one does not? */
	    MPIDU_Yield();
	}
after_shm_loop:
	if (shmIter == shmReps)
	{
	    shmIter = 0;
	}
	MPID_CPU_TICK(&end);
	shmTicks += end - start;

	if (shmIter != 0)
	    goto skip_sock_loop;

skip_shm_loop:
	MPID_CPU_TICK(&start);
	for (; sockIter<sockReps; sockIter++, sockTotalReps++)
	{
	    /* make progress on the sockets */

	    mpi_errno = MPIDU_Sock_wait(MPIDI_CH3I_sock_set, 0, &event);
	    if (mpi_errno == MPI_SUCCESS)
	    {
		mpi_errno = MPIDI_CH3I_Progress_handle_sock_event(&event);
		if (mpi_errno != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**handle_sock_op", 0);
		    goto fn_exit;
		}
		/*active = active | MPID_CH3I_SOCK_BIT;*/
		MPIDI_CH3I_active_flag |= MPID_CH3I_SOCK_BIT;
	    }
	    else
	    {
		if (MPIR_ERR_GET_CLASS(mpi_errno) != MPIDU_SOCK_ERR_TIMEOUT)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**handle_sock_op", 0);
		    goto fn_exit;
		}
		mpi_errno = MPI_SUCCESS;
		/* FIXME: Why do the other yields have MPIDI_FUNC_ENTER/EXIT 
		   macros around them but this one does not? */
		MPIDU_Yield();
	    }
	    if (completions != MPIDI_CH3I_progress_completion_count)
	    {
		sockIter++;
		sockTotalReps++;
		goto after_sock_loop;
	    }
	}
after_sock_loop:
	if (sockIter == sockReps)
	{ 
	    sockIter = 0;
	    updateIter++;
	}
	MPID_CPU_TICK(&end);
	sockTicks += end - start;

skip_sock_loop:
	if (updateIter == MPIDI_CH3I_UPDATE_ITERATIONS)
	{
	    updateIter = 0;
	    switch (MPIDI_CH3I_active_flag)
	    {
	    case MPID_CH3I_SHM_BIT:
		/* only shared memory has been active */
		/* give shm MPID_SINGLE_ACTIVE_FACTOR cycles for every 1 sock cycle */
		shmReps = (int) (
		    ( sockTicks * (MPID_CPU_Tick_t)MPID_SINGLE_ACTIVE_FACTOR * (MPID_CPU_Tick_t)shmTotalReps ) 
		    / (MPID_CPU_Tick_t)sockTotalReps 
		    / shmTicks);
		if (shmReps < 1)
		    shmReps = 1;
		sockReps = 1;
		/*MPIU_DBG_PRINTF(("(SHM_BIT: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    case MPID_CH3I_SOCK_BIT:
		/* only sockets have been active */
		/* give sock MPID_SINGLE_ACTIVE_FACTOR cycles for every 1 shm cycle */
		sockReps = (int) ( 
		    ( shmTicks * (MPID_CPU_Tick_t)MPID_SINGLE_ACTIVE_FACTOR * (MPID_CPU_Tick_t)sockTotalReps )
		    / (MPID_CPU_Tick_t)shmTotalReps
		    / sockTicks );
		if (sockReps < 1)
		    sockReps = 1;
		shmReps = 1;
		/*MPIU_DBG_PRINTF(("(SOCK_BIT: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    case MPID_CH3I_SHM_BIT + MPID_CH3I_SOCK_BIT:
		/* both channels have been active */
		/* give each channel 50% of the spin cycles */
		shmReps = (int)
		    (
		     (
		      (MPID_CPU_Tick_t)((shmTicks + sockTicks) >> 1)
		      * (MPID_CPU_Tick_t)shmTotalReps
		     ) / shmTicks
		    ) / MPIDI_CH3I_UPDATE_ITERATIONS;
		if (shmReps < 1)
		    shmReps = 1;
		sockReps = (int)
		    (
		     (
		      (MPID_CPU_Tick_t)((shmTicks + sockTicks) >> 1)
		      * (MPID_CPU_Tick_t)sockTotalReps
		     ) / sockTicks
		    ) / MPIDI_CH3I_UPDATE_ITERATIONS;
		if (sockReps < 1)
		    sockReps = 1;
		/*MPIU_DBG_PRINTF(("(BOTH_BITS: shmReps = %d, sockReps = %d)", shmReps, sockReps));*/
		break;
	    default:
		/* neither channel has been active */
		/* leave the state in its previous state */
		/*printf(".");*/
		break;
	    }
	    MPIDI_CH3I_active_flag = 0;
	    shmTotalReps = 0;
	    shmTicks = 0;
	    shmIter = 0;
	    sockTotalReps = 0;
	    sockTicks = 0;
	    sockIter = 0;
	}

	if (msgqIter++ == MPIDI_CH3I_MSGQ_ITERATIONS)
	{
	    msgqIter = 0;
	    mpi_errno = MPIDI_CH3I_Message_queue_progress();
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
						 "**mqp_failure", 0);
		goto fn_exit;
	    }
	}
    }
    while (completions == MPIDI_CH3I_progress_completion_count && is_blocking);

fn_exit:
#ifdef MPICH_DBG_OUTPUT
    if (is_blocking)
    {
	MPIDI_DBG_PRINTF((50, FCNAME, "exiting, count=%d", MPIDI_CH3I_progress_completion_count - completions));
    }
    else
    {
	if (MPIDI_CH3I_progress_completion_count - completions > 0)
	{
	    MPIDI_DBG_PRINTF((50, FCNAME, "exiting (non-blocking), count=%d", MPIDI_CH3I_progress_completion_count - completions));
	}
    }
#endif
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    /*return MPIDI_CH3I_progress_completion_count - completions;*/
    return mpi_errno;
}

#endif /* USE_FIXED_ACTIVE_PROGRESS */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_poke
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Progress_poke()
{
    int mpi_errno;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    mpi_errno = MPIDI_CH3_Progress_test();
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    return mpi_errno;
}

#if !defined(MPIDI_CH3_Progress_start)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_start
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3_Progress_start(MPID_Progress_state *state)
{
    /* MT - This function is empty for the single-threaded implementation */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_START);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_START);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_START);
}
#endif

#if !defined(MPIDI_CH3_Progress_end)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_end
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3_Progress_end(MPID_Progress_state *state)
{
    /* MT: This function is empty for the single-threaded implementation */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_END);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_END);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_END);
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_init()
{
    MPIDU_Sock_t sock;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_PROGRESS_INIT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_PROGRESS_INIT);

    MPIDI_DBG_PRINTF((60, FCNAME, "entering"));

    /* FIXME: copied from sock
#   if (USE_THREAD_IMPL == MPICH_THREAD_IMPL_GLOBAL_MUTEX && !defined(USE_CH3I_PROGRESS_DELAY_QUEUE))
    {
	MPID_Thread_cond_create(&MPIDI_CH3I_progress_completion_cond, NULL);
    }
#   endif
    */

    mpi_errno = MPIDU_Sock_init();
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_init", 0);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* create sock set */
    mpi_errno = MPIDU_Sock_create_set(&MPIDI_CH3I_sock_set);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**progress_init", 0);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* establish non-blocking listener */
    mpi_errno = MPIDI_CH3I_Connection_alloc(&MPIDI_CH3I_listener_conn);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", NULL);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */
    MPIDI_CH3I_listener_conn->sock = NULL;
    MPIDI_CH3I_listener_conn->vc = NULL;
    MPIDI_CH3I_listener_conn->state = CONN_STATE_LISTENING;
    MPIDI_CH3I_listener_conn->send_active = NULL;
    MPIDI_CH3I_listener_conn->recv_active = NULL;

    mpi_errno = MPIDU_Sock_listen(MPIDI_CH3I_sock_set, MPIDI_CH3I_listener_conn, &MPIDI_CH3I_listener_port, &sock);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", NULL);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */
 
    MPIDI_CH3I_listener_conn->sock = sock;

fn_exit:
    MPIDI_DBG_PRINTF((60, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_PROGRESS_INIT);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_finalize()
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Progress_state progress_state;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_PROGRESS_FINALIZE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_PROGRESS_FINALIZE);
    MPIDI_DBG_PRINTF((60, FCNAME, "entering"));

    /* Shut down the listener */
    mpi_errno = MPIDU_Sock_post_close(MPIDI_CH3I_listener_conn->sock);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", NULL);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    MPID_Progress_start(&progress_state);
    while(MPIDI_CH3I_listener_conn != NULL)
    {
	mpi_errno = MPID_Progress_wait(&progress_state);
	
    }
    MPID_Progress_end(&progress_state);

    /* FIXME: Cleanly shutdown other socks and MPIU_Free connection structures. (close protocol?) */

    MPIDU_Sock_destroy_set(MPIDI_CH3I_sock_set);
    MPIDU_Sock_finalize();

    /* FIXME: copied from sock
#   if (USE_THREAD_IMPL == MPICH_THREAD_IMPL_GLOBAL_MUTEX && !defined(USE_CH3I_PROGRESS_DELAY_QUEUE))
    {
	MPID_Thread_cond_destroy(&MPIDI_CH3I_progress_completion_cond, NULL);
    }
#   endif
    */

fn_exit:
    MPIDI_DBG_PRINTF((60, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_PROGRESS_FINALIZE);
    return mpi_errno;
}
