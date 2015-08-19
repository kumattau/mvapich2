/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* Copyright (c) 2001-2015, The Ohio State University. All rights
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

/* FIXME: HOMOGENEOUS SYSTEMS ONLY -- no data conversion is performed */

/*
 * MPID_Irsend()
 */
#undef FUNCNAME
#define FUNCNAME MPID_Irsend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_Irsend(const void * buf, int count, MPI_Datatype datatype, int rank, int tag, MPID_Comm * comm, int context_offset,
		MPID_Request ** request)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_ready_send_t * const ready_pkt = &upkt.ready_send;
    MPIDI_msg_sz_t data_sz;
    int dt_contig;
    MPI_Aint dt_true_lb;
    MPID_Datatype * dt_ptr;
    MPID_Request * sreq;
    MPIDI_VC_t * vc = NULL;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif    
    int mpi_errno = MPI_SUCCESS;    

#if defined (CHANNEL_PSM)
    /* implement rsend as a regular send. this switch should happen 
       before any of the macro's are invoked below */
    return (MPID_Isend(buf, count, datatype, rank, tag, comm,
            context_offset, request));
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPID_IRSEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPID_IRSEND);

    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                "rank=%d, tag=%d, context=%d", 
                rank, tag, comm->context_id + context_offset));

    /* Check to make sure the communicator hasn't already been revoked */
    if (comm->revoked &&
            MPIR_AGREE_TAG != MPIR_TAG_MASK_ERROR_BIT(tag & ~MPIR_Process.tagged_coll_mask) &&
            MPIR_SHRINK_TAG != MPIR_TAG_MASK_ERROR_BIT(tag & ~MPIR_Process.tagged_coll_mask)) {
        MPIU_ERR_SETANDJUMP(mpi_errno,MPIX_ERR_REVOKED,"**revoked");
    }
    
    if (rank == comm->rank && comm->comm_kind != MPID_INTERCOMM)
    {
	mpi_errno = MPIDI_Isend_self(buf, count, datatype, rank, tag, comm, context_offset, MPIDI_REQUEST_TYPE_RSEND, &sreq);
	goto fn_exit;
    }

    if (rank != MPI_PROC_NULL) {
        MPIDI_Comm_get_vc_set_active(comm, rank, &vc);
#ifdef ENABLE_COMM_OVERRIDES
        /* this needs to come before the sreq is created, since the override
         * function is responsible for creating its own request */
        if (vc->comm_ops && vc->comm_ops->irsend)
        {
            mpi_errno = vc->comm_ops->irsend( vc, buf, count, datatype, rank, tag, comm, context_offset, &sreq);
            goto fn_exit;
        }
#endif
    }
    
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_RSEND);
    MPIDI_Request_set_msg_type(sreq, MPIDI_REQUEST_EAGER_MSG);
    
    if (rank == MPI_PROC_NULL)
    {
	MPIU_Object_set_ref(sreq, 1);
        MPID_cc_set(&sreq->cc, 0);
	goto fn_exit;
    }
    
    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    MPIDI_Pkt_init(ready_pkt, MPIDI_CH3_PKT_READY_SEND);
    ready_pkt->match.parts.rank = comm->rank;
    ready_pkt->match.parts.tag = tag;
    ready_pkt->match.parts.context_id = comm->context_id + context_offset;
    ready_pkt->sender_req_id = MPI_REQUEST_NULL;
    ready_pkt->data_sz = data_sz;

    if (data_sz == 0)
    {
	MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending zero length message");

	sreq->dev.OnDataAvail = 0;
	
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(ready_pkt, seqnum);
	MPIDI_Request_set_seqnum(sreq, seqnum);
	
	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
	mpi_errno = MPIDI_CH3_iSend(vc, sreq, ready_pkt, sizeof(*ready_pkt));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
            MPID_Request_release(sreq);
	    sreq = NULL;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**ch3|eagermsg");
	    goto fn_exit;
	}
	/* --END ERROR HANDLING-- */
	goto fn_exit;
    }
    
#if defined(CHANNEL_MRAIL)
    /* OSU-MPI2 use rndv protocol for ready send */
    if (data_sz + sizeof(MPIDI_CH3_Pkt_eager_send_t) <= vc->eager_max_msg_sz 
        && ! vc->force_rndv) {
#else /* defined(CHANNEL_MRAIL) */
	if (vc->ready_eager_max_msg_sz < 0 || data_sz + sizeof(MPIDI_CH3_Pkt_ready_send_t) <= vc->ready_eager_max_msg_sz) {
#endif
       if (dt_contig) {
            mpi_errno = MPIDI_CH3_EagerContigIsend( &sreq,
                                                    MPIDI_CH3_PKT_READY_SEND,
                                                    (char*)buf + dt_true_lb,
                                                    data_sz, rank, tag,
                                                    comm, context_offset );
            
        }
        else {
            mpi_errno = MPIDI_CH3_EagerNoncontigSend( &sreq,
                                                      MPIDI_CH3_PKT_READY_SEND,
                                                      buf, count, datatype,
                                                      data_sz, rank, tag,
                                                      comm, context_offset );
            /* If we're not complete, then add a reference to the datatype */
            if (sreq && sreq->dev.OnDataAvail) {
                sreq->dev.datatype_ptr = dt_ptr;
                MPID_Datatype_add_ref(dt_ptr);
            }
        }
    } else {
 	/* Do rendezvous.  This will be sent as a regular send not as
           a ready send, so the receiver won't know to send an error
           if the receive has not been posted */
#if defined(CHANNEL_MRAIL)
	MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
	MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_RSEND);
	mpi_errno = MPIDI_CH3_RndvSend( &sreq, buf, count, datatype, dt_contig,
	                                data_sz, dt_true_lb, rank, tag, comm,
	                                context_offset );
	/* Note that we don't increase the ref count on the datatype
	   because this is a blocking call, and the calling routine
	   must wait until sreq completes */
#else
	MPIDI_Request_set_msg_type( sreq, MPIDI_REQUEST_RNDV_MSG );
	mpi_errno = vc->rndvSend_fn( &sreq, buf, count, datatype, dt_contig,
                                     data_sz, dt_true_lb, rank, tag, comm,
                                     context_offset );
	if (sreq && dt_ptr != NULL) {
	    sreq->dev.datatype_ptr = dt_ptr;
	    MPID_Datatype_add_ref(dt_ptr);
	}
#endif
    }

 
  fn_exit:
    *request = sreq;

    MPIU_DBG_STMT(CH3_OTHER,VERBOSE,{
	if (sreq != NULL)
	{
	    MPIU_DBG_MSG_P(CH3_OTHER,VERBOSE,"request allocated, handle=0x%08x", sreq->handle);
	}
    }
		  );
    
  fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_IRSEND);
    return mpi_errno;
}
