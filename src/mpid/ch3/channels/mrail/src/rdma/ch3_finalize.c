/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2011, The Ohio State University. All rights
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


#include "mpidi_ch3_impl.h"
#include "pmi.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Flush
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Flush() 
{
  MPIDI_STATE_DECL(MPIDI_CH3_FLUSH);
  MPIDI_FUNC_ENTER(MPIDI_CH3_FLUSH);
#ifdef MPIDI_CH3I_MRAILI_FLUSH
    MPIDI_CH3I_MRAILI_Flush();
#endif
    MPIDI_FUNC_EXIT(MPIDI_CH3_FLUSH);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Finalize()
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPIDI_CH3_FINALIZE);
    MPIDI_FUNC_ENTER(MPIDI_CH3_FINALIZE);

    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));

#ifdef CKPT
    mpi_errno = MPIDI_CH3I_CR_Finalize();
    if(mpi_errno) MPIU_ERR_POP(mpi_errno);
#endif

    /* Shutdown the progress engine */
    mpi_errno = MPIDI_CH3I_Progress_finalize();
    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* allocate rmda memory and set up the queues */
    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND) {
	/*FillMe:call MPIDI_CH3I_CM_Finalize here*/
	mpi_errno = MPIDI_CH3I_CM_Finalize();
    }
#ifdef RDMA_CM
    else if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_RDMA_CM) {
	/*FillMe:call RDMA_CM's finalization here*/
	mpi_errno = MPIDI_CH3I_CM_Finalize();
    }
#endif
    else {
	/*call old init to setup all connections*/
	mpi_errno = MPIDI_CH3I_RDMA_finalize();
    }

    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

    if (SMP_INIT) {
	mpi_errno = MPIDI_CH3I_SMP_finalize();
	if(mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

fn_exit:
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPIDI_CH3_FINALIZE);
    return mpi_errno;

fn_fail:
    /*
     * We need to add "**ch3_finalize" to the list of error messages
     */
    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**mpi_finalize");

    goto fn_exit;
}

/* vi:set sw=4 tw=80: */
