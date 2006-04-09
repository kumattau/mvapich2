/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPIU_EVENTS_H
#define MPIU_EVENTS_H

#include "mpiu_events_conf.h"

#define MPIU_EVENT_TYPE_WINDOWS 1
#define MPIU_EVENT_TYPE_MUTEX   2

#if (USE_MPIU_EVENT_TYPE == MPIU_EVENT_TYPE_WINDOWS)
#include <windows.h>

#define MPIU_EVENT_NAME_LEN_MAX 40
typedef HANDLE MPIU_Event;

#elif (USE_MPIU_EVENT_TYPE == MPIU_EVENT_TYPE_MUTEX)

#define MPIU_EVENT_NAME_LEN_MAX 40
typedef mutex_t MPIU_Event;

#else

#define MPIU_EVENT_NAME_LEN_MAX 40
typedef int * MPIU_Event;

#endif

int MPIU_Event_create(MPIU_Event *event, char *name, int length);
int MPIU_Event_open(MPIU_Event *event, char *name);
int MPIU_Event_close(MPIU_Event event);

int MPIU_Event_set(MPIU_Event event);
int MPIU_Event_reset(MPIU_Event event);

int MPIU_Event_wait(MPIU_Event event);
int MPIU_Event_wait_multiple(MPIU_Event *events, int num_events, int wait_all);
int MPIU_Event_test(MPIU_Event event, int *flag);
int MPIU_Event_test_multiple(MPIU_Event *events, int num_events, int *any_set_flag);

#endif
