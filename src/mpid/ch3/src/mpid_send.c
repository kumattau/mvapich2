/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

/* FIXME - HOMOGENEOUS SYSTEMS ONLY -- no data conversion is performed */

/* FIXME: This should share the rendezvous and eager code with 
   the related operations in MPID_Rsend (always eager) and MPID_Ssend
   (always rendezvous, though an eager sync mode is possible, it isn't
   worth the effort). */

/*
 * MPID_Send()
 */
#undef FUNCNAME
#define FUNCNAME MPID_Send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Send(const void * buf, int count, MPI_Datatype datatype, int rank, int tag, MPID_Comm * comm, int context_offset,
	      MPID_Request ** request)
{
    MPIDI_msg_sz_t data_sz;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPID_Datatype * dt_ptr;
    MPID_Request * sreq = NULL;
    MPIDI_VC_t * vc;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif    
    int mpi_errno = MPI_SUCCESS;    
    MPIDI_STATE_DECL(MPID_STATE_MPID_SEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_SEND);

    MPIDI_DBG_PRINTF((10, FCNAME, "entering"));
    MPIDI_DBG_PRINTF((15, FCNAME, "rank=%d, tag=%d, context=%d", rank, tag, comm->context_id + context_offset));

    if (rank == comm->rank && comm->comm_kind != MPID_INTERCOMM)
    {
	mpi_errno = MPIDI_Isend_self(buf, count, datatype, rank, tag, comm, context_offset, MPIDI_REQUEST_TYPE_SEND, &sreq);
#       if (MPICH_THREAD_LEVEL < MPI_THREAD_MULTIPLE)
	{
	    /* --BEGIN ERROR HANDLING-- */
	    if (sreq != NULL && sreq->cc != 0)
	    {
		mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
						 "**dev|selfsenddeadlock", 0);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	}
#	endif
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	}
	/* --END ERROR HANDLING-- */
	goto fn_exit;
    }

    if (rank == MPI_PROC_NULL)
    {
	goto fn_exit;
    }

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    MPIDI_Comm_get_vc(comm, rank, &vc);
    
    if (data_sz == 0)
    {
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_eager_send_t * const eager_pkt = &upkt.eager_send;

	MPIDI_DBG_PRINTF((15, FCNAME, "sending zero length message"));
	MPIDI_Pkt_init(eager_pkt, MPIDI_CH3_PKT_EAGER_SEND);
	eager_pkt->match.rank = comm->rank;
	eager_pkt->match.tag = tag;
	eager_pkt->match.context_id = comm->context_id + context_offset;
	eager_pkt->sender_req_id = MPI_REQUEST_NULL;
	eager_pkt->data_sz = 0;
	
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);
	
	mpi_errno = MPIDI_CH3_iStartMsg(vc, eager_pkt, sizeof(*eager_pkt), &sreq);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    /* FIXME: this is a fatal error because a sequence number has already been allocated.  If sequence numbers are not
	       being used then this could be a recoverable error.  A check needs to be added that sets the error to fatal or
	       recoverable depending on the use of sequence numbers. */
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|eagermsg", 0);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	if (sreq != NULL)
	{
	    MPIDI_Request_set_seqnum(sreq, seqnum);
	    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
	    /* sreq->comm = comm;
	      MPIR_Comm_add_ref(comm); -- not necessary for blocking functions */
	}
	
	goto fn_exit;
    }
    
    /* FIXME: flow control: limit number of outstanding eager messsages containing data and need to be buffered by the receiver */

    if (data_sz + sizeof(MPIDI_CH3_Pkt_eager_send_t) <=	MPIDI_CH3_EAGER_MAX_MSG_SIZE)
    {
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_eager_send_t * const eager_pkt = &upkt.eager_send;
	MPID_IOV iov[MPID_IOV_LIMIT];
	    
	MPIDI_Pkt_init(eager_pkt, MPIDI_CH3_PKT_EAGER_SEND);
	eager_pkt->match.rank = comm->rank;
	eager_pkt->match.tag = tag;
	eager_pkt->match.context_id = comm->context_id + context_offset;
	eager_pkt->sender_req_id = MPI_REQUEST_NULL;
	eager_pkt->data_sz = data_sz;

	iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)eager_pkt;
	iov[0].MPID_IOV_LEN = sizeof(*eager_pkt);

	if (dt_contig)
	{
	    MPIDI_DBG_PRINTF((15, FCNAME, "sending contiguous eager message, data_sz=" MPIDI_MSG_SZ_FMT, data_sz));
	    
	    iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) ((char *)buf + dt_true_lb);
	    iov[1].MPID_IOV_LEN = data_sz;
	    
	    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	    MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);
	    
	    mpi_errno = MPIDI_CH3_iStartMsgv(vc, iov, 2, &sreq);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|eagermsg", 0);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    if (sreq != NULL)
	    {
		MPIDI_Request_set_seqnum(sreq, seqnum);
		MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
		/* sreq->comm = comm;
		   MPIR_Comm_add_ref(comm); -- not necessary for blocking functions */

	    }
	}
	else
	{
	    int iov_n;
	    
	    MPIDI_DBG_PRINTF((15, FCNAME, "sending non-contiguous eager message, data_sz=" MPIDI_MSG_SZ_FMT, data_sz));
	    
	    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
	    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
	    
	    MPID_Segment_init(buf, count, datatype, &sreq->dev.segment, 0);
	    sreq->dev.segment_first = 0;
	    sreq->dev.segment_size = data_sz;
	    
	    iov_n = MPID_IOV_LIMIT - 1;
	    mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq, &iov[1], &iov_n);
	    if (mpi_errno == MPI_SUCCESS)
	    {
		iov_n += 1;
		
		MPIDI_VC_FAI_send_seqnum(vc, seqnum);
		MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);
		MPIDI_Request_set_seqnum(sreq, seqnum);
		
		if (sreq->dev.ca != MPIDI_CH3_CA_COMPLETE)
		{
		    /* sreq->dev.datatype_ptr = dt_ptr;
		       MPID_Datatype_add_ref(dt_ptr); -- not necessary for blocking functions */
		}
		
		mpi_errno = MPIDI_CH3_iSendv(vc, sreq, iov, iov_n);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    MPIU_Object_set_ref(sreq, 0);
		    MPIDI_CH3_Request_destroy(sreq);
		    sreq = NULL;
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|eagermsg", 0);
		    goto fn_exit;
		}
		/* --END ERROR HANDLING-- */
	    }
	    else
	    {
		/* --BEGIN ERROR HANDLING-- */
		MPIU_Object_set_ref(sreq, 0);
		MPIDI_CH3_Request_destroy(sreq);
		sreq = NULL;
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|loadsendiov", 0);
		goto fn_exit;
		/* --END ERROR HANDLING-- */
	    }
	}
    }
    else
    {
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_rndv_req_to_send_t * const rts_pkt = &upkt.rndv_req_to_send;
#ifndef MPIDI_CH3_CHANNEL_RNDV
	MPID_Request * rts_sreq;
#endif

	MPIDI_DBG_PRINTF((15, FCNAME, "sending rndv RTS, data_sz=" MPIDI_MSG_SZ_FMT, data_sz));
	    
	MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
	MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
	sreq->partner_request = NULL;
	
	/* FIXME - Since the request is never returned to the user and they can't do things like cancel it or wait on it, we may
           not need to fill in all of the fields.  For example, it may be completely unnecessary to supply the matching
           information.  Also, some of the fields can be set after the message has been sent.  These issues should be looked at
           more closely when we are trying to squeeze those last few nanoseconds out of the code.  */
	
	MPIDI_Pkt_init(rts_pkt, MPIDI_CH3_PKT_RNDV_REQ_TO_SEND);
	rts_pkt->match.rank = comm->rank;
	rts_pkt->match.tag = tag;
	rts_pkt->match.context_id = comm->context_id + context_offset;
	rts_pkt->sender_req_id = sreq->handle;
	rts_pkt->data_sz = data_sz;
	
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(rts_pkt, seqnum);
	MPIDI_Request_set_seqnum(sreq, seqnum);

/* FIXME: What is MPIDI_CH3_CHANNEL_RNDV, who defines it, and why? */
#ifdef MPIDI_CH3_CHANNEL_RNDV

	MPIDI_DBG_PRINTF((30, FCNAME, "Rendezvous send using iStartRndvMsg"));
    
	if (dt_contig) 
	{
	    MPIDI_DBG_PRINTF((30, FCNAME, "  contiguous rndv data, data_sz="
			      MPIDI_MSG_SZ_FMT, data_sz));
		
	    sreq->dev.ca = MPIDI_CH3_CA_COMPLETE;
	    
	    sreq->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) ((char*)sreq->dev.user_buf + dt_true_lb);
	    sreq->dev.iov[0].MPID_IOV_LEN = data_sz;
	    sreq->dev.iov_count = 1;
	}
	else
	{
	    MPID_Segment_init(sreq->dev.user_buf, sreq->dev.user_count,
			      sreq->dev.datatype, &sreq->dev.segment, 0);
	    sreq->dev.iov_count = MPID_IOV_LIMIT;
	    sreq->dev.segment_first = 0;
	    sreq->dev.segment_size = data_sz;
	    mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq, &sreq->dev.iov[0],
							 &sreq->dev.iov_count);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
						 FCNAME, __LINE__, MPI_ERR_OTHER,
						 "**ch3|loadsendiov", 0);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	}

	mpi_errno = MPIDI_CH3_iStartRndvMsg (vc, sreq, &upkt);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    MPIU_Object_set_ref(sreq, 0);
	    MPIDI_CH3_Request_destroy(sreq);
	    sreq = NULL;
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
					     FCNAME, __LINE__, MPI_ERR_OTHER,
					     "**ch3|rtspkt", 0);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	
#else

	mpi_errno = MPIDI_CH3_iStartMsg(vc, rts_pkt, sizeof(*rts_pkt), &rts_sreq);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    MPIU_Object_set_ref(sreq, 0);
	    MPIDI_CH3_Request_destroy(sreq);
	    sreq = NULL;
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|rtspkt", 0);
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	if (rts_sreq != NULL)
	{
	    if (rts_sreq->status.MPI_ERROR != MPI_SUCCESS)
	    {
		MPIU_Object_set_ref(sreq, 0);
		MPIDI_CH3_Request_destroy(sreq);
		sreq = NULL;
		mpi_errno = MPIR_Err_create_code(rts_sreq->status.MPI_ERROR, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|rtspkt", 0);
		MPID_Request_release(rts_sreq);
		goto fn_exit;
	    }
	    MPID_Request_release(rts_sreq);
	}
#endif
	
	/* FIXME: fill temporary IOV or pack temporary buffer after send to hide some latency.  This requires synchronization
           because the CTS packet could arrive and be processed before the above iStartmsg completes (depending on the progress
           engine, threads, etc.). */
	
	if (dt_ptr != NULL)
	{
	    /* sreq->dev.datatype_ptr = dt_ptr;
	       MPID_Datatype_add_ref(dt_ptr);  -- no necessary for blocking send */
	}

    }

  fn_exit:
    *request = sreq;
    
#   if defined(MPICH_DBG_OUTPUT)
    {
	if (mpi_errno == MPI_SUCCESS)
	{
	    if (sreq)
	    {
		MPIDI_DBG_PRINTF((15, FCNAME, "request allocated, handle=0x%08x", sreq->handle));
	    }
	    else
	    {
		MPIDI_DBG_PRINTF((15, FCNAME, "operation complete, no requests allocated"));
	    }
	}
    }
#   endif
    
    MPIDI_DBG_PRINTF((10, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_SEND);
    return mpi_errno;
}
