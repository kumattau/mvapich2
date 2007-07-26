
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

#ifdef _SMP_
#include "mpidimpl.h"

#undef FUNCNAME
#define FUNCNAME MPID_Is_local
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Is_local(MPID_Comm * comm, int rank)
{
    MPIDI_VC_t * vc;
    MPIDI_Comm_get_vc(comm, rank, &vc);

    if (vc->smp.local_nodes >=0){
        return 1;
    }
    else{
        return 0;
    }

}

#endif
