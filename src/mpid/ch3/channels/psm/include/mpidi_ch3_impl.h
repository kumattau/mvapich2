/* Copyright (c) 2001-2013, The Ohio State University. All rights
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

#ifndef _MPIDI_CH3_IMPL_H
#define _MPIDI_CH3_IMPL_H

#include "mpidimpl.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

/* Shared memory window atomic/accumulate mutex implementation */

#define MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr)
#define MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr)
#define MPIDI_CH3I_SHM_MUTEX_INIT(win_ptr)
#define MPIDI_CH3I_SHM_MUTEX_DESTROY(win_ptr)

#endif
