/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

#undef FUNCNAME
#define FUNCNAME MPID_Recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Recv(void * buf, int count, MPI_Datatype datatype, int rank, int tag, MPID_Comm * comm, int context_offset,
	      MPI_Status * status, MPID_Request ** request)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request * rreq;
    int found;
    MPIDI_STATE_DECL(MPID_STATE_MPID_RECV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_RECV);

    MPIDI_DBG_PRINTF((10, FCNAME, "entering"));
    MPIDI_DBG_PRINTF((15, FCNAME, "rank=%d, tag=%d, context=%d", rank, tag,
		      comm->context_id + context_offset));
    
    if (rank == MPI_PROC_NULL)
    {
	MPIR_Status_set_procnull(status);
	rreq = NULL;
	goto fn_exit;
    }

    rreq = MPIDI_CH3U_Recvq_FDU_or_AEP(rank, tag, comm->context_id + context_offset, &found);
    if (rreq == NULL) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_NO_MEM, "**nomem");
    }

    /* FIXME: in the common case, we want to simply complete the message
       and make as few updates as possible.
       Note in addition that this routine is used only by MPI_Recv (a
       blocking routine; the intent of the interface (which returns 
       a request) was to simplify the handling of the case where the
       message was not found in the unexpected queue. */
    /* FIXME: why do we add the ref count to comm?  The routine that
       calls this is required to complete before returning, so
       no valid user program can free the communicator while we
       are within this routine, and no change to the ref count should 
       be needed.  Ditto for remembering the datatype and user buffer
       statistics (no request should need to be returned by
       this routine if the message is already available) */
    rreq->comm = comm;
    MPIR_Comm_add_ref(comm);
    rreq->dev.user_buf = buf;
    rreq->dev.user_count = count;
    rreq->dev.datatype = datatype;

    if (found)
    {
	MPIDI_VC_t * vc;

	/* FIXME: We do not need the vc unless we are performing
	   a rendezvous (or synchronous eager(!!!)) send.  This code should 
	   instead be optimized for the low-latency case of an eager 
	   message, already received first, and then handle the more
	   expensive case of rendezvous messages separately. */
	MPIDI_Comm_get_vc(comm, rreq->dev.match.rank, &vc);

	/* Message was found in the unexepected queue */
	MPIDI_DBG_PRINTF((15, FCNAME, "request found in unexpected queue"));

	if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_EAGER_MSG)
	{
	    int recv_pending;
	    
	    /* This is an eager message. */
	    MPIDI_DBG_PRINTF((15, FCNAME, "eager message in the request"));

	    /* FIXME: There should be no synchronous eager messages.  It
	       is not worth the extra complexity to support these separate
	       from the rendezvous case (we did this early in MPICH-1 and
	       took it out, we should *not* commit the same mistake in MPICH-2)
	    */
	    /* FIXME: This appears to be identical code to that in MPID_Isend;
	       that is a maintenance problem (duplicate code should rarely be
	       used; if it is necessary to duplicate code, then it is vital 
	       that all locations of the duplicate code contain a complete
	       list of locations where the same code appears, so that any
	       changes may be uniformly applied */
	    /* If this is a eager synchronous message, then we need to send an acknowledgement back to the sender. */
	    if (MPIDI_Request_get_sync_send_flag(rreq))
	    {
		MPIDI_CH3_Pkt_t upkt;
		MPIDI_CH3_Pkt_eager_sync_ack_t * const esa_pkt = &upkt.eager_sync_ack;
		MPID_Request * esa_req;
		    
		MPIDI_DBG_PRINTF((30, FCNAME, "sending eager sync ack"));
		MPIDI_Pkt_init(esa_pkt, MPIDI_CH3_PKT_EAGER_SYNC_ACK);
		esa_pkt->sender_req_id = rreq->dev.sender_req_id;
		mpi_errno = MPIDI_CH3_iStartMsg(vc, esa_pkt, sizeof(*esa_pkt), &esa_req);
		if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_POP(mpi_errno);
		}
		if (esa_req != NULL)
		{
		    MPID_Request_release(esa_req);
		}
	    }
	    
            MPIDI_Request_recv_pending(rreq, &recv_pending);
	    if (!recv_pending)
	    {
		/* All of the data has arrived, we need to unpack the data and then free the buffer and the request. */
		if (rreq->dev.recv_data_sz > 0)
		{
		    MPIDI_CH3U_Request_unpack_uebuf(rreq);
		    MPIU_Free(rreq->dev.tmpbuf);
		}
		
		mpi_errno = rreq->status.MPI_ERROR;
		if (status != MPI_STATUS_IGNORE)
		{
		    *status = rreq->status;
		}
		
		MPID_Request_release(rreq);
		rreq = NULL;
		
		goto fn_exit;
	    }
	    else
	    {
		/* The data is still being transfered across the net.  We'll leave it to the progress engine to handle once the
		   entire message has arrived. */
		if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
		{
		    MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
		    MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
		}
	    }
	}
	else if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_RNDV_MSG)
	{
	    /* A rendezvous request-to-send (RTS) message has arrived.  We need to send a CTS message to the remote process. */
	    /* FIXME: We should decide who is responsible for the rendezvous
	       and require them to perform the operation.  It makes 
	       the most sense for the channel to do this, since it may want to
	       optimize the particular handling of the operation.  Common
	       code can be made available to the channels separately */
#ifdef MPIDI_CH3_CHANNEL_RNDV
		/* The channel will be performing the rendezvous */

		mpi_errno = MPIDI_CH3U_Post_data_receive(found, &rreq);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,
					 "**ch3|postrecv",
					 "**ch3|postrecv %s",
					 "MPIDI_CH3_PKT_RNDV_REQ_TO_SEND");
		}
		/* --END ERROR HANDLING-- */
		mpi_errno = MPIDI_CH3_iStartRndvTransfer (vc, rreq);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
					"**ch3|ctspkt");
		}
		/* --END ERROR HANDLING-- */

#else
	    MPID_Request * cts_req;
	    MPIDI_CH3_Pkt_t upkt;
	    MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt = &upkt.rndv_clr_to_send;
		
	    MPIDI_DBG_PRINTF((15, FCNAME, "rndv RTS in the request, sending rndv CTS"));
	    
	    MPIDI_Pkt_init(cts_pkt, MPIDI_CH3_PKT_RNDV_CLR_TO_SEND);
	    cts_pkt->sender_req_id = rreq->dev.sender_req_id;
	    cts_pkt->receiver_req_id = rreq->handle;
	    mpi_errno = MPIDI_CH3_iStartMsg(vc, cts_pkt, sizeof(*cts_pkt), &cts_req);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**ch3|ctspkt");
	    }
	    if (cts_req != NULL)
	    {
		/* FIXME: Ideally we could specify that a req not be returned.  This would avoid our having to decrement the
		   reference count on a req we don't want/need. */
		MPID_Request_release(cts_req);
	    }
#endif
	    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
	    {
		MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
		MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
	    }
	}
	else if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_SELF_MSG)
	{
	    MPID_Request * const sreq = rreq->partner_request;

	    if (sreq != NULL)
	    {
		MPIDI_msg_sz_t data_sz;
		
		MPIDI_CH3U_Buffer_copy(sreq->dev.user_buf, sreq->dev.user_count, sreq->dev.datatype, &sreq->status.MPI_ERROR,
				       buf, count, datatype, &data_sz, &rreq->status.MPI_ERROR);
		rreq->status.count = (int)data_sz;
		MPID_Request_set_completed(sreq);
		MPID_Request_release(sreq);
	    }
	    else
	    {
		/* The sreq is missing which means an error occurred.  rreq->status.MPI_ERROR should have been set when the
		   error was detected. */
	    }

	    if (status != MPI_STATUS_IGNORE)
	    {
		*status = rreq->status;
	    }

	    /* no other thread can possibly be waiting on rreq, so it is safe to reset ref_count and cc */
	    rreq->cc = 0;
	    MPIU_Object_set_ref(rreq, 1);
	}
	else
	{
	    /* --BEGIN ERROR HANDLING-- */
	    MPID_Request_release(rreq);
	    rreq = NULL;
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_INTERN, "**ch3|badmsgtype",
		      "**ch3|badmsgtype %d", MPIDI_Request_get_msg_type(rreq));
	    /* --END ERROR HANDLING-- */
	}
    }
    else
    {
	/* Message has yet to arrived.  The request has been placed on the list of posted receive requests and populated with
           information supplied in the arguments. */
	MPIDI_DBG_PRINTF((15, FCNAME, "request allocated in posted queue"));
	
	if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN)
	{
	    MPID_Datatype_get_ptr(datatype, rreq->dev.datatype_ptr);
	    MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
	}

	rreq->dev.recv_pending_count = 1;
        MPID_Request_initialized_set(rreq);
    }

  fn_exit:
    *request = rreq;
    if (rreq)
    {
	MPIDI_DBG_PRINTF((15, FCNAME, "request allocated, handle=0x%08x", rreq->handle));
    }
    else
    {
	MPIDI_DBG_PRINTF((15, FCNAME, "operation complete, no requests allocated"));
    }
 fn_fail:
    MPIDI_DBG_PRINTF((10, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_RECV);
    return mpi_errno;
}
