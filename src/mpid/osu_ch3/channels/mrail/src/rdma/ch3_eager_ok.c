/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
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


#include "mpidi_ch3_impl.h"
#ifdef MPICH_DBG_OUTPUT
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Eager_ok
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Eager_ok(MPIDI_VC_t * vc, int len)
{
#ifdef _SMP_
    if (vc->smp.local_nodes >=0 && SMP_INIT) {
        if (len <= smp_eagersize) return 1;
        else return 0;
    }
#endif
    if (len < MPIDI_CH3_EAGER_MAX_MSG_SIZE(vc)) 
        return 1;
    else return 0;
}
