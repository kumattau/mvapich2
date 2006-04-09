/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPIDU_SOCKI_H
#define MPIDU_SOCKI_H

#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>

#define MPIDU_SOCK_INVALID_SOCK   NULL
#define MPIDU_SOCK_INVALID_SET    NULL
#define MPIDU_SOCK_INFINITE_TIME   INFINITE
#define inline __inline

typedef HANDLE MPIDU_SOCK_NATIVE_FD;
typedef HANDLE MPIDU_Sock_set_t;
typedef struct sock_state_t * MPIDU_Sock_t;
typedef DWORD MPIDU_Sock_size_t;

#include "mpiiov.h"
#include "mpi.h"

#endif
