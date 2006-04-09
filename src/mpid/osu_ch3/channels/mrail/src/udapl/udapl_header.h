/* Copyright (c) 2003-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */


#ifndef _UDAPL_HEADER_H
#define _UDAPL_HEADER_H


#undef IN
#undef OUT
#include "udapl_param.h"
#include "udapl_arch.h"
#include <dat/udat.h>
#include <inttypes.h>
#include <stdlib.h>

#ifndef RDMA_FAST_PATH
#undef USE_HEADER_CACHING
#endif

double get_us (void);

#define INVAL_HNDL (0xffffffff)


#define IN
#define OUT

#undef MALLOC
#undef FREE
/* src/env/initutil.c NEW not defined */
#define MALLOC(a)    malloc((unsigned)(a))
#define CALLOC(a,b)  calloc((unsigned)(a),(unsigned)(b))
#define FREE(a)      free((char *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#define STRDUP(a) 	strdup(a)

#ifdef ONE_SIDED

#define MAX_WIN_NUM           (16)
#define SIGNAL_FOR_PUT        (1)
#define SIGNAL_FOR_GET        (2)
#define SIGNAL_FOR_LOCK_ACT   (3)
#define SIGNAL_FOR_DECR_CC    (4)

#endif

/* memory handle */
typedef struct
{
    DAT_LMR_HANDLE hndl;
    DAT_LMR_CONTEXT lkey;
    DAT_RMR_CONTEXT rkey;
} VIP_MEM_HANDLE;

#if 0
#define D_PRINT(fmt, args...)	{fprintf(stderr, "[%d][%s:%d]", viadev.me, __FILE__, __LINE__);\
				 fprintf(stderr, fmt, ## args); fflush(stderr);}
#else
#define D_PRINT(fmt, args...)
#endif

#endif
