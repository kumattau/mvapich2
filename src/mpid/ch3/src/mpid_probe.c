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

#include "mpidimpl.h"

#undef FUNCNAME
#define FUNCNAME MPID_Probe
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Probe(int source, int tag, MPID_Comm * comm, int context_offset, 
	       MPI_Status * status)
{
    MPID_Progress_state progress_state;
    const int context = comm->recvcontext_id + context_offset;
    int mpi_errno = MPI_SUCCESS;
    int complete = FALSE;
    MPIDI_STATE_DECL(MPID_STATE_MPID_PROBE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_PROBE);

    if (source == MPI_PROC_NULL)
    {
	MPIR_Status_set_procnull(status);
	goto fn_exit;
    }

#if defined (_OSU_PSM_)
    MPID_Progress_poke();
    mpi_errno = MPIDI_CH3_Probe(source, tag, context, status,
                                &complete, PSM_BLOCKING);
    if(mpi_errno)   MPIU_ERR_POP(mpi_errno);
    assert(complete == TRUE);
    goto fn_exit;
#endif

    MPIDI_CH3_Progress_start(&progress_state);
    do
    {
	if (MPIDI_CH3U_Recvq_FU( source, tag, context, status )) {
	    break;
	}

	mpi_errno = MPIDI_CH3_Progress_wait(&progress_state);
    }
    while(mpi_errno == MPI_SUCCESS);
    MPIDI_CH3_Progress_end(&progress_state);

 fn_fail:
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_PROBE);
    return mpi_errno;
}
