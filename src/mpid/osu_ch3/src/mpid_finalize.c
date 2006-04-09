/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

/* FIXME: This routine needs to be factored into finalize actions per module,
   In addition, we should consider registering callbacks for those actions
   rather than direct routine calls.
 */

#include "pmi.h"

#undef FUNCNAME
#define FUNCNAME MPID_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Finalize(void)
{
    MPID_Progress_state progress_state;
    int mpi_errno = MPI_SUCCESS, inuse, rc;
    MPIDI_STATE_DECL(MPID_STATE_MPID_FINALIZE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_FINALIZE);
    MPIDI_DBG_PRINTF((10, FCNAME, "entering"));

    /*
     * Wait for all posted receives to complete.  For now we are not doing 
     * this since it will cause invalid programs to hang.
     * The side effect of not waiting is that posted any source receives 
     * may erroneous blow up.
     *
     * For now, we are placing a warning at the end of MPID_Finalize() to 
     * inform the user if any outstanding posted receives exist.
     */
     /* FIXME: The correct action here is to begin a shutdown protocol
      * that lets other processes know that this process is now
      * in finalize.  
      *
      * Note that only requests that have been freed with MPI_Request_free
      * are valid at this point; other pending receives can be ignored 
      * since a valid program should wait or test for them before entering
      * finalize.  
      * 
      * The easist fix is to allow an MPI_Barrier over comm_world (and 
      * any connected processes in the MPI-2 case).  Once the barrier
      * completes, all processes are in finalize and any remaining 
      * unmatched receives will never be matched (by a correct program; 
      * a program with a send in a separate thread that continues after
      * some thread calls MPI_Finalize is erroneous).
      * 
      * Avoiding the barrier is hard.  Consider this sequence of steps:
      * Send in-finalize message to all connected processes.  Include
      * information on whether there are pending receives.
      *   (Note that a posted receive with any source is a problem)
      *   (If there are many connections, then this may take longer than
      *   the barrier)
      * Allow connection requests from anyone who has not previously
      * connected only if there is an possible outstanding receive; 
      * reject others with a failure (causing the source process to 
      * fail).
      * Respond to an in-finalize message with the number of posted receives
      * remaining.  If both processes have no remaining receives, they 
      * can both close the connection.
      * 
      * Processes with no pending receives and no connections can exit, 
      * calling PMI_Finalize to let the process manager know that they
      * are in a controlled exit.  
      *
      * Processes that still have open connections must then try to contact
      * the remaining processes.
      * 
      */
    
    mpi_errno = MPID_VCRT_Release(MPIR_Process.comm_self->vcrt);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = MPID_VCRT_Release(MPIR_Process.comm_world->vcrt);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
		
    /*
     * Initiate close protocol for all active VCs
     */
    MPIDI_PG_Iterate_reset();
    for (;;)
    {
	int i;
	MPIDI_PG_t * pg;
	MPIDI_VC_t * vc;

	MPIDI_PG_Get_next(&pg);
	if (pg == NULL)
	{
	    break;
	}

	for (i = 0; i < MPIDI_PG_Get_size(pg); i++)
	{
	    MPIDI_PG_Get_vcr(pg, i, &vc);

	    /* If the VC is myself then skip the close message */
	    if (pg == MPIDI_Process.my_pg && i == MPIDI_Process.my_pg_rank)
	    {
                if (vc->ref_count != 0) {
                    MPIDI_PG_Release_ref(pg, &inuse);
                    if (inuse == 0)
                    {
                        MPIDI_PG_Destroy(pg);
                    }
                }

		continue;
	    }

	    if (vc->state == MPIDI_VC_STATE_ACTIVE || vc->state == MPIDI_VC_STATE_REMOTE_CLOSE
#ifdef MPIDI_CH3_USES_SSHM
		/* sshm queues are uni-directional.  A VC that is connected 
		 * in the read direction is marked MPIDI_VC_STATE_INACTIVE
		 * so that a connection will be formed on the first write.  
		 * Since the other side is marked MPIDI_VC_STATE_ACTIVE for 
		 * writing 
		 * we need to initiate the close protocol on the read side 
		 * even if the write state is MPIDI_VC_STATE_INACTIVE. */
		|| ((vc->state == MPIDI_VC_STATE_INACTIVE) && vc->ch.shm_read_connected)
#endif
		)
	    {
		MPIDI_CH3_Pkt_t upkt;
		MPIDI_CH3_Pkt_close_t * close_pkt = &upkt.close;
		MPID_Request * sreq;
		    
		MPIDI_Pkt_init(close_pkt, MPIDI_CH3_PKT_CLOSE);
		close_pkt->ack = (vc->state == MPIDI_VC_STATE_ACTIVE) ? FALSE : TRUE;
		
		/* MT: this is not thread safe */
		/* FIXME: This global variable should be encapsulated
		   in the appropriate module (connections?) */
		MPIDI_Outstanding_close_ops += 1;
		MPIDI_DBG_PRINTF((30, FCNAME, "sending close(%s) to %d, ops = %d", close_pkt->ack ? "TRUE" : "FALSE",
				       i, MPIDI_Outstanding_close_ops));
		    

		/*
		 * A close packet acknowledging this close request could be received during iStartMsg, therefore the state must
		 * be changed before the close packet is sent.
		 */
		if (vc->state == MPIDI_VC_STATE_ACTIVE)
		{ 
		    MPIU_DBG_PrintVCState2(vc, MPIDI_VC_STATE_LOCAL_CLOSE);
		    MPIU_DBG_MSG(CH3_CONNECT,TYPICAL,"Setting state to VC_STATE_LOCAL_CLOSE");
		    vc->state = MPIDI_VC_STATE_LOCAL_CLOSE;
		}
		else /* if (vc->state == MPIDI_VC_STATE_REMOTE_CLOSE) */
		{
		    MPIU_DBG_PrintVCState2(vc, MPIDI_VC_STATE_CLOSE_ACKED);
		    MPIU_DBG_MSG(CH3_CONNECT,TYPICAL,"Setting state to VC_STATE_CLOSE_ACKED");
		    vc->state = MPIDI_VC_STATE_CLOSE_ACKED;
		}
		
		mpi_errno = MPIDI_CH3_iStartMsg(vc, close_pkt, sizeof(*close_pkt), &sreq);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_SET(mpi_errno,MPI_ERR_OTHER,
				 "**ch3|send_close_ack");
		    continue;
		}
		/* --END ERROR HANDLING-- */
		    
		if (sreq != NULL)
		{
		    MPID_Request_release(sreq);
		}
	    }
	    else
	    {
                if (vc->state == MPIDI_VC_STATE_INACTIVE && vc->ref_count != 0) {
                    MPIDI_PG_Release_ref(pg, &inuse);
                    if (inuse == 0)
                    {
                        MPIDI_PG_Destroy(pg);
                    }
                }

		MPIDI_DBG_PRINTF((30, FCNAME, "not sending a close to %d, vc in state %s", i,
				  MPIDI_VC_Get_state_description(vc->state)));
	    }
	}
    }

    /*
     * Wait for all VCs to finish the close protocol
     */
    MPID_Progress_start(&progress_state);
    while(MPIDI_Outstanding_close_ops > 0)
    {
	mpi_errno = MPID_Progress_wait(&progress_state);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS) {
	    MPID_Progress_end(&progress_state);
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
				"**ch3|close_progress");
	}
	/* --END ERROR HANDLING-- */
    }
    MPID_Progress_end(&progress_state);

    /* FIXME: Progress finalize should be in CH3_Finalize */
    mpi_errno = MPIDI_CH3I_Progress_finalize();
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = MPIDI_CH3_Finalize();
#ifdef MPIDI_DEV_IMPLEMENTS_KVS
    /* Finalize the CH3 device KVS cache interface */
    rc = MPIDI_KVS_Finalize();
#endif
    /* Let PMI know the process is about to exit */
    rc = PMI_Finalize();
    if (rc != 0) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
			     "**ch3|pmi_finalize", 
			     "**ch3|pmi_finalize %d", rc);
    }

    MPIDI_PG_Release_ref(MPIDI_Process.my_pg, &inuse);
    if (inuse == 0) {
        MPIDI_PG_Destroy(MPIDI_Process.my_pg);
    }
    MPIDI_Process.my_pg = NULL;

 fn_exit:
    MPIDI_DBG_PRINTF((10, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_FINALIZE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
