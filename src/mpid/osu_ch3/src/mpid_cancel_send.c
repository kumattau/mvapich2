/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#include "mpidimpl.h"

/* FIXME: This should call a channel-provided routine to deliver the 
   cancel message, once the code decides that the request can still
   be cancelled */

#undef FUNCNAME
#define FUNCNAME MPID_Cancel_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Cancel_send(MPID_Request * sreq)
{
    MPIDI_VC_t * vc;
    int proto;
    int flag;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPID_CANCEL_SEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_CANCEL_SEND);
    
    MPIU_Assert(sreq->kind == MPID_REQUEST_SEND);

    MPIDI_Request_cancel_pending(sreq, &flag);
    if (flag)
    {
	goto fn_exit;
    }

    /*
     * FIXME: user requests returned by MPI_Ibsend() have a NULL comm pointer
     * and no pointer to the underlying communication
     * request.  For now, we simply fail to cancel the request.  In the future,
     * we should add a new request kind to indicate that
     * the request is a BSEND.  Then we can properly cancel the request, much 
     * in the way we do persistent requests.
     */
    if (sreq->comm == NULL)
    {
	goto fn_exit;
    }

    MPIDI_Comm_get_vc(sreq->comm, sreq->dev.match.rank, &vc);

    proto = MPIDI_Request_get_msg_type(sreq);

    if (proto == MPIDI_REQUEST_SELF_MSG)
    {
	MPID_Request * rreq;
	
	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,
		     "attempting to cancel message sent to self");
	
	rreq = MPIDI_CH3U_Recvq_FDU(sreq->handle, &sreq->dev.match);
	if (rreq)
	{
	    MPIU_Assert(rreq->partner_request == sreq);
	    
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
             "send-to-self cancellation successful, sreq=0x%08x, rreq=0x%08x",
						sreq->handle, rreq->handle));
	    
	    MPIU_Object_set_ref(rreq, 0);
	    MPIDI_CH3_Request_destroy(rreq);
	    
	    sreq->status.cancelled = TRUE;
	    /* no other thread should be waiting on sreq, so it is safe to 
	       reset ref_count and cc */
	    sreq->cc = 0;
	    MPIU_Object_set_ref(sreq, 1);
	}
	else
	{
	    sreq->status.cancelled = FALSE; 
	    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
               "send-to-self cancellation failed, sreq=0x%08x, rreq=0x%08x",
						sreq->handle, rreq->handle));
	}
	
	goto fn_exit;
    }

    /* Check to see if the send is still in the send queue.  If so, remove it, 
       mark the request and cancelled and complete, and
       release the device's reference to the request object.  
    */
    {
	int cancelled;
	
	if (proto == MPIDI_REQUEST_RNDV_MSG)
	{
	    MPID_Request * rts_sreq;
	    /* The cancellation of the RTS request needs to be atomic through 
	       the destruction of the RTS request to avoid
               conflict with release of the RTS request if the CTS is received
	       (see handling of a rendezvous CTS packet in
               MPIDI_CH3U_Handle_recv_pkt()).  
	       MPID_Request_fetch_and_clear_rts_sreq() is used to gurantee 
	       that atomicity. */
	    /* OSU-MPI2 finishes rndv request with extra step */
	    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(sreq);
	    MPIDI_Request_fetch_and_clear_rts_sreq(sreq, &rts_sreq);
	    if (rts_sreq != NULL) 
	    {
		cancelled = FALSE;
		
		/* since we attempted to cancel a RTS request, then we are 
		   responsible for releasing that request */
		MPID_Request_release(rts_sreq);

		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
						     "**ch3|cancelrndv", 0);
		    goto fn_exit;
		}
		/* --END ERROR HANDLING-- */
		
		if (cancelled)
		{
		    sreq->status.cancelled = TRUE;
		    /* no other thread should be waiting on sreq, so it is 
		       safe to reset ref_count and cc */
		    sreq->cc = 0;
		    MPIU_Object_set_ref(sreq, 1);
		    goto fn_exit;
		}
	    }
	}
	else
	{
	    cancelled = FALSE;
	    if (cancelled)
	    {
		sreq->status.cancelled = TRUE;
		/* no other thread should be waiting on sreq, so it is safe to 
		   reset ref_count and cc */
		sreq->cc = 0;
		MPIU_Object_set_ref(sreq, 1);
		goto fn_exit;
	    }
	}
    }

    /* Part or all of the message has already been sent, so we need to send a 
       cancellation request to the receiver in an attempt
       to catch the message before it is matched. */
    {
	int was_incomplete;
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_cancel_send_req_t * const csr_pkt = &upkt.cancel_send_req;
	MPID_Request * csr_sreq;
	int seqnum; /* OSU-MPI2 requires seqnum */
	
	MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
              "sending cancel request to %d for 0x%08x", 
	      sreq->dev.match.rank, sreq->handle));
	
	/* The completion counter and reference count are incremented to keep 
	   the request around long enough to receive a
	   response regardless of what the user does (free the request before 
	   waiting, etc.). */
	MPIDI_CH3U_Request_increment_cc(sreq, &was_incomplete);
	if (!was_incomplete)
	{
	    /* The reference count is incremented only if the request was 
	       complete before the increment. */
	    MPIR_Request_add_ref( sreq );
	}

	MPIDI_Pkt_init(csr_pkt, MPIDI_CH3_PKT_CANCEL_SEND_REQ);
	csr_pkt->match.rank = sreq->comm->rank;
	csr_pkt->match.tag = sreq->dev.match.tag;
	csr_pkt->match.context_id = sreq->dev.match.context_id;
	csr_pkt->sender_req_id = sreq->handle;

	/* OSU-MPI2 requires seqnum */	
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(csr_pkt, seqnum);
	/* End of OSU-MPI2 */

	mpi_errno = MPIDI_CH3_iStartMsg(vc, csr_pkt, sizeof(*csr_pkt), &csr_sreq);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
					     "**ch3|cancelreq", 0);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	if (csr_sreq != NULL)
	{
	    MPID_Request_release(csr_sreq);
	}
    }
    
    /* FIXME: if send cancellation packets are allowed to arrive out-of-order 
       with respect to send packets, then we need to
       timestamp send and cancel packets to insure that a cancellation request 
       does not bypass the send packet to be cancelled
       and erroneously cancel a previously sent message with the same request 
       handle. */
    /* FIXME: A timestamp is more than is necessary; a message sequence number
       should be adequate. */

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_CANCEL_SEND);
    return mpi_errno;
}

/*
 * Handler routines called when cancel send packets arrive
 */

int MPIDI_CH3_PktHandler_CancelSendReq( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
					MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_cancel_send_req_t * req_pkt = &pkt->cancel_send_req;
    MPID_Request * rreq;
    int ack;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_cancel_send_resp_t * resp_pkt = &upkt.cancel_send_resp;
    MPID_Request * resp_sreq;
    int mpi_errno = MPI_SUCCESS;
    /* OSU-MPI2 */
    int seqnum;
    /* End of OSU-MPI2 */
    
    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
      "received cancel send req pkt, sreq=0x%08x, rank=%d, tag=%d, context=%d",
		      req_pkt->sender_req_id, req_pkt->match.rank, 
		      req_pkt->match.tag, req_pkt->match.context_id));
	    
    rreq = MPIDI_CH3U_Recvq_FDU(req_pkt->sender_req_id, &req_pkt->match);
    if (rreq != NULL)
    {
	MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"message cancelled");
	if (MPIDI_Request_get_msg_type(rreq) == MPIDI_REQUEST_EAGER_MSG && rreq->dev.recv_data_sz > 0)
	{
	    MPIU_Free(rreq->dev.tmpbuf);
	}
	/* OSU-MPI2 */
	MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(rreq);
	/* End of OSU-MPI2 */
	MPID_Request_release(rreq);
	ack = TRUE;
    }
    else
    {
	MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"unable to cancel message");
	ack = FALSE;
    }
    
    MPIDI_Pkt_init(resp_pkt, MPIDI_CH3_PKT_CANCEL_SEND_RESP);
    resp_pkt->sender_req_id = req_pkt->sender_req_id;
    resp_pkt->ack = ack;
    /* OSU-MPI2 */
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(resp_pkt, seqnum);
    /* End of OSU-MPI2 */
    mpi_errno = MPIDI_CH3_iStartMsg(vc, resp_pkt, sizeof(*resp_pkt), &resp_sreq);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
			    "**ch3|cancelresp");
    }
    if (resp_sreq != NULL)
    {
	MPID_Request_release(resp_sreq);
    }
    
    *rreqp = NULL;

 fn_fail:
    return mpi_errno;
}

int MPIDI_CH3_PktHandler_CancelSendResp( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
					 MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_cancel_send_resp_t * resp_pkt = &pkt->cancel_send_resp;
    MPID_Request * sreq;
    
    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
			"received cancel send resp pkt, sreq=0x%08x, ack=%d",
			resp_pkt->sender_req_id, resp_pkt->ack));
	    
    MPID_Request_get_ptr(resp_pkt->sender_req_id, sreq);
    
    if (resp_pkt->ack)
    {
	sreq->status.cancelled = TRUE;
	
	if (MPIDI_Request_get_msg_type(sreq) == MPIDI_REQUEST_RNDV_MSG ||
	    MPIDI_Request_get_type(sreq) == MPIDI_REQUEST_TYPE_SSEND)
	{
	    int cc;
	    
	    /* decrement the CC one additional time for the CTS/sync ack that 
	       is never going to arrive */
	    MPIDI_CH3U_Request_decrement_cc(sreq, &cc);
	}
		
	MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"message cancelled");
    }
    else
    {
	sreq->status.cancelled = FALSE; 
	MPIU_DBG_MSG(CH3_OTHER,TYPICAL,"unable to cancel message");
    }
    
    MPIDI_CH3U_Request_complete(sreq);
    
    *rreqp = NULL;

    return MPI_SUCCESS;
}

/*
 * Define the routines that can print out the cancel packets if 
 * debugging is enabled.
 */
#ifdef MPICH_DBG_OUTPUT
int MPIDI_CH3_PktPrint_CancelSendReq( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... CANCEL_SEND\n"));
    MPIU_DBG_PRINTF((" sender_reqid . 0x%08X\n", pkt->cancel_send_req.sender_req_id));
    MPIU_DBG_PRINTF((" context_id ... %d\n", pkt->cancel_send_req.match.context_id));
    MPIU_DBG_PRINTF((" tag .......... %d\n", pkt->cancel_send_req.match.tag));
    MPIU_DBG_PRINTF((" rank ......... %d\n", pkt->cancel_send_req.match.rank));

    return MPI_SUCCESS;
}

int MPIDI_CH3_PktPrint_CancelSendResp( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... CANCEL_SEND_RESP\n"));
    MPIU_DBG_PRINTF((" sender_reqid . 0x%08X\n", pkt->cancel_send_resp.sender_req_id));
    MPIU_DBG_PRINTF((" ack .......... %d\n", pkt->cancel_send_resp.ack));
    
    return MPI_SUCCESS;
}
#endif
