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
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3_impl.h"
#include "vbuf.h"
#include "pmi.h"
#include "mpiutil.h"

static int MPIDI_CH3_SMP_Rendezvous_push(MPIDI_VC_t *, MPID_Request *);

MPIDI_VC_t *flowlist;

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Prepare_rndv_get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Prepare_rndv_get(MPIDI_VC_t * vc,
                               MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    MPIU_Assert(VAPI_PROTOCOL_RGET == rreq->mrail.protocol);

    MPIDI_CH3I_MRAIL_Prepare_rndv(vc, rreq);

#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Prepare_rndv_cts
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Prepare_rndv_cts(MPIDI_VC_t * vc,
                               MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt,
                               MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    int reg_success;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    switch (rreq->mrail.protocol) {
    case VAPI_PROTOCOL_R3:
        {
            cts_pkt->rndv.protocol = VAPI_PROTOCOL_R3;
            /*MRAILI_Prepost_R3(); */
            break;
        }
    case VAPI_PROTOCOL_RPUT:
        {
            reg_success = MPIDI_CH3I_MRAIL_Prepare_rndv(vc, rreq);
            MPIDI_CH3I_MRAIL_SET_PKT_RNDV(cts_pkt, rreq);
	    MPIDI_CH3I_MRAIL_REVERT_RPUT(rreq);
            break;
        }
    case VAPI_PROTOCOL_RGET:
        {
            int rank;
            PMI_Get_rank(&rank);
            fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);
            fprintf(stderr, "RGET preparing CTS?\n");
            mpi_errno = -1;
            break;
        }
        break;
    default:
        {
            int rank;
            PMI_Get_rank(&rank);
            fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);
            fprintf(stderr,
                    "Unknown protocol %d type from rndv req to send\n",
		    rreq->mrail.protocol);
            mpi_errno = -1;
            break;
        }
    }

#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iStartRndvTransfer
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iStartRndvTransfer(MPIDI_VC_t * vc, MPID_Request * rreq)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_rndv_clr_to_send_t *cts_pkt = &upkt.rndv_clr_to_send;
    MPID_Request *cts_req;
    MPID_Seqnum_t seqnum;
    int mpi_errno = MPI_SUCCESS;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif
        
    if (rreq->dev.iov_count == 1 && rreq->dev.OnDataAvail == NULL)
	cts_pkt->recv_sz = rreq->dev.iov[0].MPID_IOV_LEN;
    else
	cts_pkt->recv_sz = rreq->dev.segment_size;
    
    MPIDI_Pkt_init(cts_pkt, MPIDI_CH3_PKT_RNDV_CLR_TO_SEND);
    cts_pkt->sender_req_id = rreq->dev.sender_req_id;
    cts_pkt->receiver_req_id = rreq->handle;
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(cts_pkt, seqnum);    

    mpi_errno = MPIDI_CH3_Prepare_rndv_cts(vc, cts_pkt, rreq);
    if (mpi_errno != MPI_SUCCESS) {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
					 FCNAME, __LINE__,
					 MPI_ERR_OTHER, "**ch3|ctspkt", 0);
	goto fn_exit;
    }

    mpi_errno = MPIDI_CH3_iStartMsg(vc, cts_pkt, sizeof(*cts_pkt), &cts_req);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**ch3|ctspkt", 0);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */
    if (cts_req != NULL) {
        MPID_Request_release(cts_req);
    }

    if (HANDLE_GET_KIND(rreq->dev.datatype) != HANDLE_KIND_BUILTIN)
    {
        MPID_Datatype_get_ptr(rreq->dev.datatype, rreq->dev.datatype_ptr);
        MPID_Datatype_add_ref(rreq->dev.datatype_ptr);
    }

  fn_exit:
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rndv_transfer
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rndv_transfer(MPIDI_VC_t * vc,
        MPID_Request * sreq,
        MPID_Request * rreq,
        MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt,
        MPIDI_CH3_Pkt_rndv_req_to_send_t * rts_pkt)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_CH3I_MRAILI_Rndv_info_t *rndv;        /* contains remote info */
    MPID_Request * req;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif
        
    DEBUG_PRINT("Get rndv reply, add to list\n");

    /* This function can adapt to either read
     * or write based on the value of sreq or
     * rreq. */
    if(sreq) {
        req = sreq;
    } else {
        req = rreq;
    }

    switch (req->mrail.protocol)
    {
    case VAPI_PROTOCOL_RPUT:
            rndv = (cts_pkt == NULL) ? NULL : &cts_pkt->rndv;
            sreq->mrail.partner_id = cts_pkt->receiver_req_id;
            MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(sreq, rndv);
        break;
    case VAPI_PROTOCOL_R3:
            rndv = (cts_pkt == NULL) ? NULL : &cts_pkt->rndv;
            sreq->mrail.partner_id = cts_pkt->receiver_req_id;
            MPIU_Assert(rndv->protocol == VAPI_PROTOCOL_R3);
        break;
    case VAPI_PROTOCOL_RGET:
            rndv = (rts_pkt == NULL) ? NULL : &rts_pkt->rndv;
            MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(rreq, rndv);
        break;
    default:
            mpi_errno = MPIR_Err_create_code(
                0,
                MPIR_ERR_FATAL,
                FCNAME,
                __LINE__,
                MPI_ERR_OTHER,
                "**fail",
                "**fail %s",
                "unknown protocol");
#if defined(CKPT)
            MPIDI_CH3I_CR_unlock();
#endif /* defined(CKPT) */
        return mpi_errno;
    }

    RENDEZVOUS_IN_PROGRESS(vc, req);
    /*
     * this is where all rendezvous transfers are started,
     * so it is the only place we need to set this kludgy
     * field
     */

    req->mrail.nearly_complete = 0;

    PUSH_FLOWLIST(vc);

#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif /* defined(CKPT) */

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_push
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_push(MPIDI_VC_t * vc, MPID_Request * sreq)
{
    if (SMP_INIT
        && vc->smp.local_nodes >= 0
        && vc->smp.local_nodes != g_smpi.my_local_id)
    {
        MPIU_Assert(sreq->mrail.protocol == VAPI_PROTOCOL_R3);
        MPIDI_CH3_SMP_Rendezvous_push(vc, sreq);
        return MPI_SUCCESS;
    }

    switch (sreq->mrail.protocol)
    {
    case VAPI_PROTOCOL_RPUT:
            MPIDI_CH3I_MRAILI_Rendezvous_rput_push(vc, sreq);
        break;
    case VAPI_PROTOCOL_RGET:
            MPIDI_CH3I_MRAILI_Rendezvous_rget_push(vc, sreq);
        break;
    default:
            MPIDI_CH3_Rendezvous_r3_push(vc, sreq);
        break;
    }
    
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SMP_Rendezvous_push
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3_SMP_Rendezvous_push(MPIDI_VC_t * vc,
                                                MPID_Request * sreq)
{
    int nb;
    int complete = 0;
    int seqnum;
    int mpi_errno;
    MPIDI_CH3_Pkt_rndv_r3_data_t pkt_head;
    MPID_Request * send_req;

    MPIDI_Pkt_init(&pkt_head, MPIDI_CH3_PKT_RNDV_R3_DATA);
    pkt_head.receiver_req_id = sreq->mrail.partner_id;
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(&pkt_head, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);

    mpi_errno = MPIDI_CH3_iStartMsg(vc, &pkt_head,
                                    sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t),
                                    &send_req);
    if (mpi_errno != MPI_SUCCESS) {
         MPIU_Object_set_ref(sreq, 0);
         MPIDI_CH3_Request_destroy(sreq);
         sreq = NULL;
         mpi_errno =
             MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                              __LINE__, MPI_ERR_OTHER, "**ch3|rtspkt",
                              0);
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    if (send_req != NULL) {
        DEBUG_PRINT("r3 packet not sent \n");
        MPID_Request_release(send_req);
    }
 

    DEBUG_PRINT("r3 sent req is %p\n", sreq);
    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {
        for (;;) {
            DEBUG_PRINT("iov count (sreq): %d, offset %d, len[1] %d\n",
                        sreq->dev.iov_count, sreq->dev.iov_offset,
                        sreq->dev.iov[0].MPID_IOV_LEN);

            mpi_errno = MPIDI_CH3I_SMP_writev_rndv_data(vc, 
                                &sreq->dev.iov[sreq->dev.iov_offset], 
                                sreq->dev.iov_count - sreq->dev.iov_offset,
                                &nb);

            if (MPI_SUCCESS != mpi_errno) {
                vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                sreq->status.MPI_ERROR = MPI_ERR_INTERN;
                MPIDI_CH3U_Request_complete(sreq);
                return mpi_errno;
            }

            if (nb >= 0) {
                if (MPIDI_CH3I_Request_adjust_iov(sreq, nb)) {
                    MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                    if (complete) {
                        sreq->mrail.nearly_complete = 1;
                        break;
                    } else {
                        vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
                    }
                } else {
                    sreq->ch.reqtype = REQUEST_RNDV_R3_DATA;
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                    sreq->mrail.nearly_complete = 1;
                    vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
                    break;
                }
            } else {
                MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                vc->smp.send_active = sreq;
                sreq->mrail.nearly_complete = 1;
                break;
            }
        }
    } else {
        sreq->ch.reqtype = REQUEST_RNDV_R3_DATA;
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        sreq->mrail.nearly_complete = 1;
        vc->smp.send_current_pkt_type = SMP_RNDV_MSG;
        DEBUG_PRINT("Enqueue sreq %p", sreq);
    }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_r3_push
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3_Rendezvous_r3_push(MPIDI_VC_t * vc, MPID_Request * sreq)
{
    vbuf *buf;
    MPID_IOV iov[MPID_IOV_LIMIT + 1];
    int n_iov;
    int msg_buffered = 0;
    int nb;
    int complete = 0;
    int seqnum;
    int finished = 0;
    int mpi_errno;

    MPIDI_CH3_Pkt_rndv_r3_data_t pkt_head;

    MPIDI_Pkt_init(&pkt_head, MPIDI_CH3_PKT_RNDV_R3_DATA);
    iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t);
    iov[0].MPID_IOV_BUF = (void*) &pkt_head;
    pkt_head.receiver_req_id = sreq->mrail.partner_id;

    do {
        do {
            MPIDI_VC_FAI_send_seqnum(vc, seqnum);
            MPIDI_Pkt_set_seqnum(&pkt_head, seqnum);
            MPIDI_Request_set_seqnum(sreq, seqnum);

            memcpy((void *) &iov[1],
                   &sreq->dev.iov[sreq->dev.iov_offset],
                   (sreq->dev.iov_count -
                    sreq->dev.iov_offset) * sizeof(MPID_IOV));
            n_iov = sreq->dev.iov_count - sreq->dev.iov_offset + 1;

            DEBUG_PRINT("iov count (sreq): %d, offset %d, len[1] %d\n",
                        sreq->dev.iov_count, sreq->dev.iov_offset,
                        sreq->dev.iov[0].MPID_IOV_LEN);

            {
                int i = 0, total_len = 0;
                for (i = 0; i < n_iov; i++) {
                    total_len += (iov[i].MPID_IOV_LEN);
                }

                mpi_errno =
                    MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, 
                        total_len, &nb, &buf);
            }

            DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno,
                    nb);

            if (MPI_SUCCESS != mpi_errno
                && MPI_MRAIL_MSG_QUEUED != mpi_errno) {
                vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                sreq->status.MPI_ERROR = MPI_ERR_INTERN;
                MPIDI_CH3U_Request_complete(sreq);
                return;
            } else if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                msg_buffered = 1;
            }

            nb -= sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t);
            finished = MPIDI_CH3I_Request_adjust_iov(sreq, nb);
            DEBUG_PRINT("ajust iov finish: %d\n", finished);
        } while (!finished/* && !msg_buffered*/);

        if (finished && sreq->dev.OnDataAvail ==
			MPIDI_CH3_ReqHandler_SendReloadIOV) {
            MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
            nb = 0;
            complete = 0;
        } else if (finished) {
            complete = 1;
        }
    } while (/* 1 != msg_buffered && */0 == complete);

    DEBUG_PRINT("exit loop with complete %d, msg_buffered %d\n", complete,
                msg_buffered);

    /*if (0 == complete && 1 == msg_buffered) {
        sreq->mrail.nearly_complete = 0;
    } else */if (1 == msg_buffered) {
        buf->sreq = (void *) sreq;
        sreq->mrail.nearly_complete = 1;
    } else {
        buf->sreq = NULL;
        MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
        sreq->mrail.nearly_complete = 1;
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Process_rndv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_MRAILI_Process_rndv()
{
    MPID_Request *sreq;
    while (flowlist) {

        /* Push on the the first ongoing receive with
         * viadev_rendezvous_push. If the receive
         * finishes, it will advance the shandle_head
         * pointer on the connection.
         *
         * xxx the side effect of viadev_rendezvous_push is
         * bad practice. Find a way to do this so the logic
         * is obvious.
         */
#ifdef CKPT
        /*If vc is suspended, ignore this flow and move on*/
        if (flowlist->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
            POP_FLOWLIST();/*VC will be push back when state becomes MPIDI_CH3I_VC_STATE_IDLE*/
            continue;
        }
#endif
        sreq = flowlist->mrail.sreq_head;
        while (sreq != NULL) {
#ifdef CKPT
            if (flowlist->ch.rput_stop
             && VAPI_PROTOCOL_RPUT == sreq->mrail.protocol) {
                break; /*VC will be push back when the rput_stop becomes 0*/
            }
#endif
            MPIDI_CH3_Rendezvous_push(flowlist, sreq);
            DEBUG_PRINT("[process rndv] after rndv push\n");
            if (1 != sreq->mrail.nearly_complete) {
                break;
            }
            DEBUG_PRINT
                ("[process rndv] nearly complete, remove from list\n");
            RENDEZVOUS_DONE(flowlist);
            sreq = flowlist->mrail.sreq_head;
        }
        /* now move on to the next connection */
        POP_FLOWLIST();
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvouz_r3_recv_data
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvouz_r3_recv_data(MPIDI_VC_t * vc, vbuf * buffer)
{
    int mpi_errno = MPI_SUCCESS;
    int skipsize = sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t);
    int nb, complete;
    MPID_Request *rreq;
    MPID_Request_get_ptr(((MPIDI_CH3_Pkt_rndv_r3_data_t *) (buffer->
                                                            pheader))->
                         receiver_req_id, rreq);

    if (!(VAPI_PROTOCOL_R3 == rreq->mrail.protocol ||
          VAPI_PROTOCOL_RPUT == rreq->mrail.protocol)) {
        int rank;
        PMI_Get_rank(&rank);

        DEBUG_PRINT( "[rank %d]get wrong req protocol, req %p, protocol %d\n", rank,
            rreq, rreq->mrail.protocol);
        MPIU_Assert(VAPI_PROTOCOL_R3 == rreq->mrail.protocol ||
               VAPI_PROTOCOL_RPUT == rreq->mrail.protocol);
    }

    rreq->mrail.protocol = VAPI_PROTOCOL_R3;

    mpi_errno = MPIDI_CH3I_MRAIL_Fill_Request(rreq, buffer, skipsize, &nb);
    if (mpi_errno != MPI_SUCCESS)
    {
        mpi_errno = MPIR_Err_create_code(
            mpi_errno,
            MPIR_ERR_FATAL,
            FCNAME,
            __LINE__,
            MPI_ERR_OTHER,
            "**fail",
            0);
        goto fn_exit;
    }

    skipsize += nb;
    DEBUG_PRINT("[recv r3: handle read] filled request nb is %d\n", nb);

    if (MPIDI_CH3I_Request_adjust_iov(rreq, nb)) {
        mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);
        DEBUG_PRINT("[recv: handle read] adjust req fine, complete %d\n",
                    complete);
        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno,
                                     MPIR_ERR_RECOVERABLE, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**fail", 0);
            goto fn_exit;
        }
        while (complete != TRUE) {
            mpi_errno =
                MPIDI_CH3I_MRAIL_Fill_Request(rreq, buffer, skipsize, &nb);
            
            if (mpi_errno != MPI_SUCCESS)
            {
                mpi_errno = MPIR_Err_create_code(
                    mpi_errno,
                    MPIR_ERR_FATAL,
                    FCNAME,
                    __LINE__,
                    MPI_ERR_OTHER,
                    "**fail",
                    0);
                goto fn_exit;
            }
            if (!MPIDI_CH3I_Request_adjust_iov(rreq, nb)) {
                goto fn_exit;
            }
            skipsize += nb;

            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);
            DEBUG_PRINT
                ("[recv: handle read] adjust req fine, complete %d\n",
                 complete);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno,
                                         MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**fail",
                                         0);
                goto fn_exit;
            }
        }
        if (TRUE == complete) {
            rreq->mrail.protocol = VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED;
        }
    }
  fn_exit:
    DEBUG_PRINT("Successfully return from r3 recv\n");
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_rget_send_finish
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_rget_send_finish(MPIDI_VC_t * vc,
                                     MPIDI_CH3_Pkt_rget_finish_t *rget_pkt)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;
    MPID_Request *sreq;

    MPID_Request_get_ptr(rget_pkt->sender_req_id, sreq);

    if (!MPIDI_CH3I_MRAIL_Finish_request(sreq)) {
        return MPI_SUCCESS;
    }

    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(sreq);

#if 0
    if(MPIDI_CH3I_RDMA_Process.has_hsam && 
            ((req->mrail.rndv_buf_sz > striping_threshold))) {

        /* Adjust the weights of different paths according to the
         * timings obtained for the stripes */

        adjust_weights(v->vc, req->mrail.stripe_start_time,
                req->mrail.stripe_finish_time, req->mrail.initial_weight);
    }
#endif

    MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);

    if (complete != TRUE)
    {
        mpi_errno = MPIR_Err_create_code(
            mpi_errno,
            MPIR_ERR_FATAL,
            FCNAME,
            __LINE__,
            MPI_ERR_OTHER,
            "**fail",
            0);
        goto fn_exit;
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_req_dequeue(sreq);
#endif /* defined(CKPT) */

fn_exit:
    return mpi_errno;

}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_rget_recv_finish
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_rget_recv_finish(MPIDI_VC_t * vc,
                                     MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;

    if (!MPIDI_CH3I_MRAIL_Finish_request(rreq))
    {
        return MPI_SUCCESS;
    }

    if (rreq->mrail.rndv_buf_alloc == 1)
    {
        /* If we are using datatype, then need to unpack data from tmpbuf */
        int iter = 0;
        int copied = 0;
        uintptr_t buf = (uintptr_t) rreq->mrail.rndv_buf;

        for (; iter < rreq->dev.iov_count; ++iter)
        {
            memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                   (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
            buf += rreq->dev.iov[iter].MPID_IOV_LEN;
            copied += rreq->dev.iov[iter].MPID_IOV_LEN;
        }

        MPIDI_CH3I_Request_adjust_iov(rreq, copied);

        while (rreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_UnpackSRBufReloadIOV
            || rreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_ReloadIOV)
        {
            /* XXX: dev.ca should only be CA_COMPLETE? */
            /* end of XXX */
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);

            if (mpi_errno != MPI_SUCCESS || complete == TRUE)
            {
                mpi_errno = MPIR_Err_create_code(
                    mpi_errno,
                    MPIR_ERR_FATAL,
                    FCNAME,
                    __LINE__,
                    MPI_ERR_OTHER,
                    "**fail",
                    0);
                goto fn_exit;
            }

            copied = 0;

            for (iter = 0; iter < rreq->dev.iov_count; ++iter)
            {
                memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                       (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
                buf += rreq->dev.iov[iter].MPID_IOV_LEN;
                copied += rreq->dev.iov[iter].MPID_IOV_LEN;
            }

            MPIDI_CH3I_Request_adjust_iov(rreq, copied);
        }
    }
    else
    {
        rreq->mrail.rndv_buf = NULL;
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_req_dequeue(rreq);
#endif /* defined(CKPT) */

    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(rreq);

    mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);

    if (mpi_errno != MPI_SUCCESS)
    {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno,
                                 MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**fail", 0);
    }

    if (complete)
    {
        vc->ch.recv_active = NULL;
    }
    else
    {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**fail", 0);
        goto fn_exit;
    }

  fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_rput_finish
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_rput_finish(MPIDI_VC_t * vc,
                                     MPIDI_CH3_Pkt_rput_finish_t * rf_pkt)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;
    MPID_Request *rreq;
    MPID_Request_get_ptr(rf_pkt->receiver_req_id, rreq);

    if (!MPIDI_CH3I_MRAIL_Finish_request(rreq))
    {
        return MPI_SUCCESS;
    }

    if (rreq->mrail.rndv_buf_alloc == 1)
    {
        /* If we are using datatype, then need to unpack data from tmpbuf */
        int iter = 0;
        int copied = 0;
        uintptr_t buf = (uintptr_t) rreq->mrail.rndv_buf;

        for (; iter < rreq->dev.iov_count; ++iter)
        {
            memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                   (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
            buf += rreq->dev.iov[iter].MPID_IOV_LEN;
            copied += rreq->dev.iov[iter].MPID_IOV_LEN;
        }

        MPIDI_CH3I_Request_adjust_iov(rreq, copied);

        while (rreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_UnpackSRBufReloadIOV
            || rreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_ReloadIOV)
        {
            /* XXX: dev.ca should only be CA_COMPLETE? */
            /* end of XXX */
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS || complete == TRUE)
            {
                mpi_errno = MPIR_Err_create_code(
                    mpi_errno,
                    MPIR_ERR_FATAL,
                    FCNAME,
                    __LINE__,
                    MPI_ERR_OTHER,
                    "**fail",
                    0);
                goto fn_exit;
            }

            copied = 0;

            for (iter = 0; iter < rreq->dev.iov_count; ++iter)
            {
                memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                       (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
                buf += rreq->dev.iov[iter].MPID_IOV_LEN;
                copied += rreq->dev.iov[iter].MPID_IOV_LEN;
            }

            MPIDI_CH3I_Request_adjust_iov(rreq, copied);
        }
    }
    else
    {
        rreq->mrail.rndv_buf = NULL;
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_req_dequeue(rreq);
#endif /* defined(CKPT) */

    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(rreq);

    mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);
    if (mpi_errno != MPI_SUCCESS)
    {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno,
                                 MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**fail", 0);
    }

    if (complete)
    {
        vc->ch.recv_active = NULL;
    }
    else
    {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**fail", 0);
        goto fn_exit;
    }

  fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Get_rndv_push
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Get_rndv_push(MPIDI_VC_t * vc,
                            MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt,
                            MPID_Request * req)
{
    int mpi_errno = MPI_SUCCESS;
    vbuf *v;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    if (VAPI_PROTOCOL_R3 == req->mrail.protocol) {
        req->mrail.partner_id = get_resp_pkt->request_handle;
	MPIDI_VC_revoke_seqnum_send(vc, get_resp_pkt->seqnum);
        RENDEZVOUS_IN_PROGRESS(vc, req);
        req->mrail.nearly_complete = 0;
        PUSH_FLOWLIST(vc);
    } else {
        MPID_IOV iov;
        int n_iov = 1;
        int nb;
        MPIDI_CH3I_MRAILI_Rndv_info_t rndv;

        iov.MPID_IOV_BUF = (void*) get_resp_pkt;
        iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_get_resp_t);
        get_resp_pkt->protocol = VAPI_PROTOCOL_RPUT;

        MPIDI_CH3I_MRAIL_SET_REMOTE_RNDV_INFO(&rndv, req);
        MPIDI_CH3I_MRAILI_Get_rndv_rput(vc, req, &rndv, &iov);

        if (VAPI_PROTOCOL_R3 == req->mrail.protocol) {
            req->mrail.partner_id = get_resp_pkt->request_handle;
	    MPIDI_VC_revoke_seqnum_send(vc, get_resp_pkt->seqnum);
            RENDEZVOUS_IN_PROGRESS(vc, req);
            req->mrail.nearly_complete = 0;
            PUSH_FLOWLIST(vc);
        }
    }
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Get_rndv_recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Get_rndv_recv(MPIDI_VC_t * vc, MPID_Request * req)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;

    MPIU_Assert(req->mrail.protocol == VAPI_PROTOCOL_RPUT);

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    if (req->mrail.rndv_buf_alloc == 1)
    {
        /* If we are using datatype, then need to unpack data from tmpbuf */
        int iter = 0;
        uintptr_t buf = (uintptr_t) req->mrail.rndv_buf;

        for (; iter < req->dev.iov_count; ++iter)
        {
            memcpy(req->dev.iov[iter].MPID_IOV_BUF,
                   (void *) buf, req->dev.iov[iter].MPID_IOV_LEN);
            buf += req->dev.iov[iter].MPID_IOV_LEN;
        }

        while (req->dev.OnDataAvail == MPIDI_CH3_ReqHandler_UnpackSRBufReloadIOV
            || req->dev.OnDataAvail == MPIDI_CH3_ReqHandler_ReloadIOV)
        {
            /* mpi_errno = MPIDI_CH3U_Request_load_recv_iov(req); */
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);

            if (mpi_errno != MPI_SUCCESS)
            {
                goto fn_exit;
            }

            for (iter = 0; iter < req->dev.iov_count; ++iter)
            {
                memcpy(req->dev.iov[iter].MPID_IOV_BUF,
                       (void *) buf, req->dev.iov[iter].MPID_IOV_LEN);
                buf += req->dev.iov[iter].MPID_IOV_LEN;
            }
        }
    }
    else
    {
        req->mrail.rndv_buf = NULL;
    }

    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(req);

    mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);

    if (mpi_errno != MPI_SUCCESS)
    {
        goto fn_exit;
    }

    MPIU_Assert(complete == TRUE);

  fn_exit:
#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif /* defined(CKPT) */
    return mpi_errno;
}
