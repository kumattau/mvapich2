/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *
 * Copyright (c) 2001-2016, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include "mpid_nem_impl.h"

#include "upmi.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Abort
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Abort(int exit_code, const char *error_msg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ABORT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ABORT);

    UPMI_ABORT(exit_code, error_msg);

    /* if abort returns for some reason, exit here */

    MPIU_Error_printf("%s", error_msg);
    fflush(stderr);

    MPIU_Exit(exit_code);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ABORT);
    return MPI_ERR_INTERN;
}
