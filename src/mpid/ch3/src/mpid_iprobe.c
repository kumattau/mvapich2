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
#define FUNCNAME MPID_Iprobe
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Iprobe(int source, int tag, MPID_Comm *comm, int context_offset, 
		int *flag, MPI_Status *status)
{
    const int context = comm->recvcontext_id + context_offset;
    int found = 0;
    int mpi_errno = MPI_SUCCESS;
    int complete = FALSE; 

    MPIDI_STATE_DECL(MPID_STATE_MPID_IPROBE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_IPROBE);

    if (source == MPI_PROC_NULL)
    {
	MPIR_Status_set_procnull(status);
	/* We set the flag to true because an MPI_Recv with this rank will
	   return immediately */
	*flag = TRUE;
	goto fn_exit;
    }

#if defined (_OSU_PSM_) 
        MPID_Progress_poke();
        mpi_errno = MPIDI_CH3_Probe(source, tag, context, status,
                                    &complete, PSM_NONBLOCKING);
        if(mpi_errno)   MPIU_ERR_POP(mpi_errno);
        *flag = complete;
        goto fn_exit;
#endif

    /* FIXME: The routine CH3U_Recvq_FU is used only by the probe functions;
       it should atomically return the flag and status rather than create 
       a request.  Note that in some cases it will be possible to 
       atomically query the unexpected receive list (which is what the
       probe routines are for). */
    MPIU_THREAD_CS_ENTER(MSGQUEUE,);
    found = MPIDI_CH3U_Recvq_FU( source, tag, context, status );
    MPIU_THREAD_CS_EXIT(MSGQUEUE,);
    if (!found) {
	/* Always try to advance progress before returning failure
	   from the iprobe test.  */
	/* FIXME: It would be helpful to know if the Progress_poke
	   operation causes any change in state; we could then avoid
	   a second test of the receive queue if we knew that nothing
	   had changed */
	mpi_errno = MPID_Progress_poke();
	MPIU_THREAD_CS_ENTER(MSGQUEUE,);
	found = MPIDI_CH3U_Recvq_FU( source, tag, context, status );
	MPIU_THREAD_CS_EXIT(MSGQUEUE,);
    }
	
    *flag = found;

 fn_fail:   
 fn_exit:    
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_IPROBE);
    return mpi_errno;
}
