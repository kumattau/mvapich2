/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"

#undef FUNCNAME
#define FUNCNAME update_request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static void update_request(MPID_Request * sreq, void * hdr, MPIDI_msg_sz_t hdr_sz, MPIU_Size_t nb)
{
    MPIDI_STATE_DECL(MPID_STATE_UPDATE_REQUEST);

    MPIDI_FUNC_ENTER(MPID_STATE_UPDATE_REQUEST);
    MPIU_Assert(hdr_sz == sizeof(MPIDI_CH3_Pkt_t));
    sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) hdr;
    sreq->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)((char *) &sreq->ch.pkt + nb);
    sreq->dev.iov[0].MPID_IOV_LEN = hdr_sz - nb;
    sreq->dev.iov_count = 1;
    MPIDI_FUNC_EXIT(MPID_STATE_UPDATE_REQUEST);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iSend(MPIDI_VC_t * vc, MPID_Request * sreq, void * hdr, MPIDI_msg_sz_t hdr_sz)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ISEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ISEND);

    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));
#ifdef MPICH_DBG_OUTPUT
    /* --BEGIN ERROR HANDLING-- */
    if (hdr_sz > sizeof(MPIDI_CH3_Pkt_t))
    {
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**arg", 0);
	MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISEND);
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
#endif

    /* The sock channel uses a fixed length header, the size of which is the maximum of all possible packet headers */
    hdr_sz = sizeof(MPIDI_CH3_Pkt_t);
    MPIDI_DBG_Print_packet((MPIDI_CH3_Pkt_t*)hdr);

    if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTED) /* MT */
    {
	/* Connection already formed.  If send queue is empty attempt to send data, queuing any unsent data. */
	if (MPIDI_CH3I_SendQ_empty(vc)) /* MT */
	{
	    MPIU_Size_t nb;
	    int rc;

	    MPIDI_DBG_PRINTF((55, FCNAME, "send queue empty, attempting to write"));
	    
	    /* MT: need some signalling to lock down our right to use the channel, thus insuring that the progress engine does
               also try to write */
	    rc = MPIDU_Sock_write(vc->ch.sock, hdr, hdr_sz, &nb);
	    if (rc == MPI_SUCCESS)
	    {
		MPIDI_DBG_PRINTF((55, FCNAME, "wrote %ld bytes", (unsigned long) nb));
		
		if (nb == hdr_sz)
		{
		    MPIDI_DBG_PRINTF((55, FCNAME, "write complete %d bytes, calling MPIDI_CH3U_Handle_send_req()", nb));
		    MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
		    if (!complete)
		    {
			MPIDI_CH3I_SendQ_enqueue_head(vc, sreq);
			MPIDI_DBG_PRINTF((55, FCNAME, "posting writev, vc=0x%p, sreq=0x%08x", vc, sreq->handle));
			vc->ch.conn->send_active = sreq;
			mpi_errno = MPIDU_Sock_post_writev(vc->ch.conn->sock, sreq->dev.iov, sreq->dev.iov_count, NULL);
			/* --BEGIN ERROR HANDLING-- */
			if (mpi_errno != MPI_SUCCESS)
			{
			    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
							     "**ch3|sock|postwrite", "ch3|sock|postwrite %p %p %p",
							     sreq, vc->ch.conn, vc);
			}
			/* --END ERROR HANDLING-- */
		    }
		}
		else
		{
		    MPIDI_DBG_PRINTF((55, FCNAME, "partial write of %d bytes, request enqueued at head", nb));
		    update_request(sreq, hdr, hdr_sz, nb);
		    MPIDI_CH3I_SendQ_enqueue_head(vc, sreq);
		    MPIDI_DBG_PRINTF((55, FCNAME, "posting write, vc=0x%p, sreq=0x%08x", vc, sreq->handle));
		    vc->ch.conn->send_active = sreq;
		    mpi_errno = MPIDU_Sock_post_write(vc->ch.conn->sock, sreq->dev.iov[0].MPID_IOV_BUF,
						      sreq->dev.iov[0].MPID_IOV_LEN, sreq->dev.iov[0].MPID_IOV_LEN, NULL);
		    /* --BEGIN ERROR HANDLING-- */
		    if (mpi_errno != MPI_SUCCESS)
		    {
			mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
							 "**ch3|sock|postwrite", "ch3|sock|postwrite %p %p %p",
							 sreq, vc->ch.conn, vc);
		    }
		    /* --END ERROR HANDLING-- */
		}
	    }
	    /* --BEGIN ERROR HANDLING-- */
	    else if (MPIR_ERR_GET_CLASS(rc) == MPIDU_SOCK_ERR_NOMEM)
	    {
		MPIDI_DBG_PRINTF((55, FCNAME, "MPIDU_Sock_write failed, out of memory"));
		sreq->status.MPI_ERROR = MPIR_ERR_MEMALLOCFAILED;
	    }
	    else
	    {
		MPIDI_DBG_PRINTF((55, FCNAME, "MPIDU_Sock_write failed, rc=%d", rc));
		/* Connection just failed. Mark the request complete and return an error. */
		MPIU_DBG_MSG(CH3_CONNECT,TYPICAL,"Setting state to VC_STATE_FAILED");
		vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
		/* TODO: Create an appropriate error message based on the return value (rc) */
		sreq->status.MPI_ERROR = MPI_ERR_INTERN;
		 /* MT -CH3U_Request_complete() performs write barrier */
		MPIDI_CH3U_Request_complete(sreq);
	    }
	    /* --END ERROR HANDLING-- */
	}
	else
	{
	    MPIDI_DBG_PRINTF((55, FCNAME, "send queue not empty, enqueuing"));
	    update_request(sreq, hdr, hdr_sz, 0);
	    MPIDI_CH3I_SendQ_enqueue(vc, sreq);
	}
    }
    else if (vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) /* MT */
    {
	/* Form a new connection, queuing the data so it can be sent later. */
	MPIDI_DBG_PRINTF((55, FCNAME, "unconnected.  enqueuing request"));
	update_request(sreq, hdr, hdr_sz, 0);
	MPIDI_CH3I_SendQ_enqueue(vc, sreq);
	mpi_errno = MPIDI_CH3I_VC_post_connect(vc);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	}
	/* --END ERROR HANDLING-- */
    }
    else if (vc->ch.state != MPIDI_CH3I_VC_STATE_FAILED)
    {
	/* Unable to send data at the moment, so queue it for later */
	MPIDI_DBG_PRINTF((55, FCNAME, "still connecting.  enqueuing request"));
	update_request(sreq, hdr, hdr_sz, 0);
	MPIDI_CH3I_SendQ_enqueue(vc, sreq);
    }
    /* --BEGIN ERROR HANDLING-- */
    else
    {
	/* Connection failed.  Mark the request complete and return an error. */
	/* TODO: Create an appropriate error message */
	sreq->status.MPI_ERROR = MPI_ERR_INTERN;
	/* MT - CH3U_Request_complete() performs write barrier */
	MPIDI_CH3U_Request_complete(sreq);
    }
    /* --END ERROR HANDLING-- */

    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISEND);
    return mpi_errno;
}

