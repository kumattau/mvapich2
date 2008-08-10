/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#include "mpid_mrail_rndv.h"
#include "mpidimpl.h"

/*
 * This file contains the implementation of the rendezvous protocol
 * for MPI point-to-point messaging.
 */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_RndvSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* MPID_MRAIL_RndvSend - Send a request to perform a rendezvous send */
int MPID_MRAIL_RndvSend (
    MPID_Request** sreq_p,
    const void* buf,
    int count,
    MPI_Datatype datatype,
    int dt_contig,
    MPIDI_msg_sz_t data_sz,
    MPI_Aint dt_true_lb,
    int rank,
    int tag,
    MPID_Comm* comm,
    int context_offset)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_rndv_req_to_send_t * const rts_pkt = &upkt.rndv_req_to_send;
    MPIDI_VC_t * vc;
    MPID_Request *sreq =*sreq_p;
    int          mpi_errno = MPI_SUCCESS;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
	
    MPIU_DBG_MSG_D(CH3_OTHER,VERBOSE,
		   "sending rndv RTS, data_sz=" MPIDI_MSG_SZ_FMT, data_sz);
	    
    sreq->partner_request = NULL;
	
    MPIDI_Pkt_init(rts_pkt, MPIDI_CH3_PKT_RNDV_REQ_TO_SEND);
    rts_pkt->match.rank	      = comm->rank;
    rts_pkt->match.tag	      = tag;
    rts_pkt->match.context_id = comm->context_id + context_offset;
    rts_pkt->sender_req_id    = sreq->handle;
    rts_pkt->data_sz	      = data_sz;

    MPIDI_Comm_get_vc(comm, rank, &vc);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(rts_pkt, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);

    MPIU_DBG_MSGPKT(vc,tag,rts_pkt->match.context_id,rank,data_sz,"Rndv");

    if (dt_contig) 
    {
	MPIU_DBG_MSG_D(CH3_OTHER,VERBOSE,"  contiguous rndv data, data_sz="
		       MPIDI_MSG_SZ_FMT, data_sz);
		
	sreq->dev.OnDataAvail = 0;
	
	sreq->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) ((char*)sreq->dev.user_buf + dt_true_lb);
	sreq->dev.iov[0].MPID_IOV_LEN = data_sz;
	sreq->dev.iov_count = 1;
    }
    else
    {
	sreq->dev.segment_ptr = MPID_Segment_alloc( );
	/* if (!sreq->dev.segment_ptr) { MPIU_ERR_POP(); } */
	MPID_Segment_init(sreq->dev.user_buf, sreq->dev.user_count,
			  sreq->dev.datatype, sreq->dev.segment_ptr, 0);
	sreq->dev.iov_count = MPID_IOV_LIMIT;
	sreq->dev.segment_first = 0;
	sreq->dev.segment_size = data_sz;
	/* One the initial load of a send iov req, set the OnFinal action (null
	   for point-to-point) */
	sreq->dev.OnFinal = 0;
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
	*sreq_p = NULL;
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
					 FCNAME, __LINE__, MPI_ERR_OTHER,
					 "**ch3|rtspkt", 0);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */
    
 fn_exit:

    return mpi_errno;
}

/*
 * This routine processes a rendezvous message once the message is matched.
 * It is used in mpid_recv and mpid_irecv.
 */
int MPID_MRAIL_RndvRecv (MPIDI_VC_t* vc, MPID_Request* rreq)
{
    int mpi_errno = MPI_SUCCESS;
    /* A rendezvous request-to-send (RTS) message has arrived.  We need
       to send a CTS message to the remote process. */
    
    if (rreq->dev.recv_data_sz == 0) {
	MPIDI_CH3U_Request_complete(rreq);
    }
    else {
	mpi_errno = MPIDI_CH3U_Post_data_receive_found(rreq);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,
				 "**ch3|postrecv",
				 "**ch3|postrecv %s",
				 "MPIDI_CH3_PKT_RNDV_REQ_TO_SEND");
	}
    }

    mpi_errno = MPIDI_CH3_iStartRndvTransfer (vc, rreq);

    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
				"**ch3|ctspkt");
    }

 fn_fail:    
    return mpi_errno;
}
