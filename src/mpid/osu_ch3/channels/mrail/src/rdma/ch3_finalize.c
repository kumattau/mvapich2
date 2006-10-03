/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

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


#include "mpidi_ch3_impl.h"
#include "pmi.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Finalize()
{
    int mpi_errno = MPI_SUCCESS;
    int rc;

    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));

    /* Shutdown the progress engine */
    mpi_errno = MPIDI_CH3I_Progress_finalize();
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**ch3_finalize",
                                 0);
    }

    /* allocate rmda memory and set up the queues */
    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND) {
        /*FillMe:call MPIDI_CH3I_CM_Finalize here*/
        mpi_errno = MPIDI_CH3I_CM_Finalize();
    }
    else {
        /*call old init to setup all connections*/
        mpi_errno = MPIDI_CH3I_RMDA_finalize();
    }

    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**ch3_finalize",
                                 0);
    }

#ifdef _SMP_
    MPIDI_CH3I_SMP_finalize();
#endif

    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    return mpi_errno;
}
