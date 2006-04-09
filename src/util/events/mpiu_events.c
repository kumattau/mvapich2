/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpiu_events.h"
#include "mpidu_process_locks.h" /* MPIDU_Yield */
/*#include "mpimem.h"*/
#include "mpi.h"
#include <stdio.h>

/* FIXME: What is this for?  Who uses it?  */

/*
#if defined(MPICH_DBG_OUTPUT)
#ifndef MPIU_DBG_PRINTF
#define MPIU_DBG_PRINTF(e)			\
{						\
    if (MPIUI_dbg_state != MPIU_DBG_STATE_NONE)	\
    {						\
	MPIU_dbg_printf e;			\
    }						\
}
#endif
#else
#ifndef MPIU_DBG_PRINTF
#define MPIU_DBG_PRINTF(e)
#endif
#endif
*/

#if (USE_MPIU_EVENT_TYPE == MPIU_EVENT_TYPE_WINDOWS)

#undef FUNCNAME
#define FUNCNAME MPIU_Event_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_create(MPIU_Event *event, char *name, int length)
{
    HANDLE hEvent;
    UUID guid;
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_CREATE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_CREATE);

    if (event == NULL || name == NULL || length < MPIU_EVENT_NAME_LEN_MAX)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CREATE);
	return MPI_ERR_ARG;
    }

    UuidCreate(&guid);
    MPIU_Snprintf(name, length, "%08lX-%04X-%04x-%02X%02X-%02X%02X%02X%02X%02X%02X",
	guid.Data1, guid.Data2, guid.Data3,
	guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
	guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);

    hEvent = CreateEvent(NULL, TRUE, FALSE, name);
    if (hEvent == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CREATE);
	return MPI_ERR_OTHER;
    }
    *event = hEvent;
    MPIU_DBG_PRINTF(("created event: %p=<%s>\n", hEvent, name));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CREATE);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_open
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_open(MPIU_Event *event, char *name)
{
    HANDLE hEvent;
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_OPEN);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_OPEN);

    if (event == NULL || name == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_OPEN);
	return MPI_ERR_ARG;
    }

    hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, name);
    if (hEvent == NULL)
    {
	MPIU_DBG_PRINTF(("failed to open event: <%s>\n", name));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_OPEN);
	return MPI_ERR_OTHER;
    }
    *event = hEvent;
    MPIU_DBG_PRINTF(("opened event: %p=<%s>\n", hEvent, name));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_OPEN);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_close
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_close(MPIU_Event event)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_CLOSE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_CLOSE);

    if (event == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CLOSE);
	return MPI_ERR_ARG;
    }
    if (CloseHandle(event))
    {
	MPIU_DBG_PRINTF(("closed event %p\n", event));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CLOSE);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to close event %p\n", event));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_CLOSE);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIDU_Event_set
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_set(MPIU_Event event)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_SET);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_SET);

    if (event == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_SET);
	return MPI_ERR_ARG;
    }
    if (SetEvent(event))
    {
	MPIU_DBG_PRINTF(("set event %p\n", event));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_SET);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to set event %p\n", event));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_SET);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_reset
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_reset(MPIU_Event event)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_RESET);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_RESET);

    if (event == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_RESET);
	return MPI_ERR_ARG;
    }
    if (ResetEvent(event))
    {
	MPIU_DBG_PRINTF(("reset event %p\n", event));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_RESET);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to reset event %p\n", event));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_RESET);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_wait
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_wait(MPIU_Event event)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_WAIT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_WAIT);

    MPIU_DBG_PRINTF(("waiting for event %p\n", event));
    if (WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0)
    {
	MPIU_DBG_PRINTF(("waited on event %p\n", event));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_WAIT);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to wait on event %p\n", event));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_WAIT);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_wait_multiple
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_wait_multiple(MPIU_Event *events, int num_events, int wait_all)
{
    DWORD result;
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_WAIT_MULTIPLE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_WAIT_MULTIPLE);

    if (events == NULL || num_events < 1)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_WAIT_MULTIPLE);
	return MPI_ERR_ARG;
    }
    MPIU_DBG_PRINTF(("waiting on %d events\n", num_events));
    result = WaitForMultipleObjects(num_events, events, wait_all, INFINITE);
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + num_events)
    {
	MPIU_DBG_PRINTF(("waited on %d events, %p first signalled\n", num_events, events[result - WAIT_OBJECT_0]));
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_WAIT_MULTIPLE);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to wait on %d events\n", num_events));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_WAIT_MULTIPLE);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_test(MPIU_Event event, int *flag)
{
    DWORD result;
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_TEST);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_TEST);

    if (event == NULL || flag == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST);
	return MPI_ERR_ARG;
    }

    MPIU_DBG_PRINTF(("testing event %p\n", event));
    result = WaitForSingleObject(event, 0);
    if (result == WAIT_OBJECT_0)
    {
	MPIU_DBG_PRINTF(("tested event %p, TRUE\n", event));
	*flag = 1;
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST);
	return MPI_SUCCESS;
    }
    if (result == WAIT_TIMEOUT)
    {
	MPIU_DBG_PRINTF(("tested event %p, FALSE\n", event));
	*flag = 0;
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to test event %p\n", event));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST);
    return MPI_ERR_OTHER;
}

#undef FUNCNAME
#define FUNCNAME MPIU_Event_test_multiple
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIU_Event_test_multiple(MPIU_Event *events, int num_events, int *any_set_flag)
{
    DWORD result;
    MPIDI_STATE_DECL(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);

    if (events == NULL || num_events < 1 || any_set_flag == NULL)
    {
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);
	return MPI_ERR_ARG;
    }

    MPIU_DBG_PRINTF(("testing %d events\n", num_events));
    result = WaitForMultipleObjects(num_events, events, FALSE, 0);
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + num_events)
    {
	MPIU_DBG_PRINTF(("tested %d events, %p set\n", num_events, events[result - WAIT_OBJECT_0]));
	*any_set_flag = 1;
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);
	return MPI_SUCCESS;
    }
    if (result == WAIT_TIMEOUT)
    {
	MPIU_DBG_PRINTF(("tested %d events, none set\n", num_events));
	*any_set_flag = 0;
	MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);
	return MPI_SUCCESS;
    }
    MPIU_DBG_PRINTF(("failed to test %d events\n", num_events));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIU_EVENT_TEST_MULTIPLE);
    return MPI_ERR_OTHER;
}

#elif (USE_MPIU_EVENT_TYPE == MPIU_EVENT_TYPE_MUTEX)

int MPIU_Event_create(MPIU_Event *event, char *name, int length)
{
}

int MPIU_Event_open(MPIU_Event *event, char *name)
{
}

int MPIU_Event_close(MPIU_Event event)
{
}

int MPIU_Event_set(MPIU_Event event)
{
}

int MPIU_Event_reset(MPIU_Event event)
{
}

int MPIU_Event_wait(MPIU_Event event)
{
}

int MPIU_Event_wait_multiple(MPIU_Event *events, int num_events, int wait_all)
{
}

int MPIU_Event_test(MPIU_Event event, int *flag)
{
}

int MPIU_Event_test_multiple(MPIU_Event *events, int num_events, int *any_set_flag)
{
}

#else

int MPIU_Event_create(MPIU_Event *event, char *name, int length)
{
    int mpi_errno;
    int *p;

    p = (int*)MPIU_Malloc(sizeof(int));
    if (p == NULL)
    {
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, "MPIU_Event_create", __LINE__, MPI_ERR_OTHER, "**fail", 0);
	return mpi_errno;
    }
    *event = p;
    MPIU_Snprintf(name, length, "%p", p);
    return MPI_SUCCESS;
}

int MPIU_Event_open(MPIU_Event *event, char *name)
{
    int n;
    int *p;
    n = sscanf(name, "%p", &p);
    if (n == 1)
	return MPI_SUCCESS;
    return -1;
}

int MPIU_Event_close(MPIU_Event event)
{
    MPIU_Free(event);
    return MPI_SUCCESS;
}

int MPIU_Event_set(MPIU_Event event)
{
    *event = 1;
}

int MPIU_Event_reset(MPIU_Event event)
{
    *event = 0;
}

int MPIU_Event_wait(MPIU_Event event)
{
    while (*event == 0)
    {
	MPIDU_Yield();
    }
}

int MPIU_Event_wait_multiple(MPIU_Event *events, int num_events, int wait_all)
{
    int i;
    int num_set;
    for(;;)
    {
	num_set = 0;
	for (i=0; i<num_events; i++)
	{
	    if (*events[i] != 0)
	    {
		num_set++;
	    }
	}
	if (num_set)
	{
	    if (wait_all)
	    {
		if (num_set == num_events)
		{
		    return MPI_SUCCESS;
		}
	    }
	    else
	    {
		return MPI_SUCCESS;
	    }
	}
    }
}

int MPIU_Event_test(MPIU_Event event, int *flag)
{
    *flag = *event;
    return MPI_SUCCESS;
}

int MPIU_Event_test_multiple(MPIU_Event *events, int num_events, int *any_set_flag)
{
    int i;
    for (i=0; i<num_events; i++)
    {
	if (*events[i] != 0)
	{
	    *any_set_flag = 1;
	    return MPI_SUCCESS;
	}
    }
    *any_set_flag = 0;
    return MPI_SUCCESS;
}

#endif
