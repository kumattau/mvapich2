
/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2006, The Ohio State University. All rights
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

#include "vbuf.h"
#include "mpidi_ch3_impl.h"
#include "pmi.h"

#ifdef _SMP_
static int MPIDI_CH3_SMP_Rendezvous_push(MPIDI_VC_t *,
                                                MPID_Request *);
#endif

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
#define FUNCNAME MPIDI_CH3_Prepare_rndv_cts
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Prepare_rndv_cts(MPIDI_VC_t * vc,
                               MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt,
                               MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    int reg_success;
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
            break;
        }
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
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Start_rndv_transfer
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Start_rndv_transfer(MPIDI_VC_t * vc,
                                  MPID_Request * sreq,
                                  MPIDI_CH3_Pkt_rndv_clr_to_send_t *
                                  cts_pkt)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_CH3I_MRAILI_Rndv_info_t *rndv;        /* contains remote info */

    DEBUG_PRINT("Get rndv reply, add to list\n");
    rndv = (cts_pkt == NULL) ? NULL : &cts_pkt->rndv;

    sreq->mrail.partner_id = cts_pkt->receiver_req_id;

    switch (sreq->mrail.protocol) {
    case VAPI_PROTOCOL_RPUT:
        MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(sreq, rndv);
        break;
    case VAPI_PROTOCOL_R3:
        assert(rndv->protocol == VAPI_PROTOCOL_R3);
        break;
    default:
        mpi_errno = MPIR_Err_create_code(0, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER,
                                         "**fail | unknown protocol", 0);
        return mpi_errno;
    }
    RENDEZVOUS_IN_PROGRESS(vc, sreq);
    /*
     * this is where all rendezvous transfers are started,
     * so it is the only place we need to set this kludgy
     * field
     */

    sreq->mrail.nearly_complete = 0;

    PUSH_FLOWLIST(vc);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Rendezvous_push
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_push(MPIDI_VC_t * vc, MPID_Request * sreq)
{
    MRAILI_Channel_info channel;

    channel.hca_index = 0;
    channel.rail_index = 0;
#ifdef _SMP_
    if (vc->smp.local_nodes >= 0 &&
        vc->smp.local_nodes != smpi.my_local_id) {
        assert(VAPI_PROTOCOL_R3 == sreq->mrail.protocol);
        MPIDI_CH3_SMP_Rendezvous_push(vc, sreq);
        return MPI_SUCCESS;
    }
#endif
    if (VAPI_PROTOCOL_RPUT == sreq->mrail.protocol) {
        MPIDI_CH3I_MRAILI_Rendezvous_rput_push(vc, sreq);
    } else {
        MPIDI_CH3_Rendezvous_r3_push(vc, sreq);
    }
    return MPI_SUCCESS;
}

#ifdef _SMP_
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
                        sreq->dev.iov_count, sreq->ch.iov_offset,
                        sreq->dev.iov[0].MPID_IOV_LEN);

            mpi_errno = MPIDI_CH3I_SMP_writev(vc, 
                                &sreq->dev.iov[sreq->ch.iov_offset], 
                                sreq->dev.iov_count - sreq->ch.iov_offset,
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
                    }
                } else {
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                    sreq->mrail.nearly_complete = 1;
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
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        sreq->mrail.nearly_complete = 1;
        DEBUG_PRINT("Enqueue sreq %p", sreq);
    }
    return MPI_SUCCESS;
}

#endif

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
    int rdma_ok;
    int seqnum;
    int finished = 0;
    int mpi_errno;

    MPIDI_CH3_Pkt_rndv_r3_data_t pkt_head;

    MPIDI_Pkt_init(&pkt_head, MPIDI_CH3_PKT_RNDV_R3_DATA);
    iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t);
    iov[0].MPID_IOV_BUF = &pkt_head;
    pkt_head.receiver_req_id = sreq->mrail.partner_id;

    do {
        do {
            MPIDI_VC_FAI_send_seqnum(vc, seqnum);
            MPIDI_Pkt_set_seqnum(&pkt_head, seqnum);
            MPIDI_Request_set_seqnum(sreq, seqnum);

            memcpy((void *) &iov[1],
                   &sreq->dev.iov[sreq->ch.iov_offset],
                   (sreq->dev.iov_count -
                    sreq->ch.iov_offset) * sizeof(MPID_IOV));
            n_iov = sreq->dev.iov_count - sreq->ch.iov_offset + 1;

            DEBUG_PRINT("iov count (sreq): %d, offset %d, len[1] %d\n",
                        sreq->dev.iov_count, sreq->ch.iov_offset,
                        sreq->dev.iov[0].MPID_IOV_LEN);

#ifdef RDMA_FAST_PATH
            rdma_ok = MPIDI_CH3I_MRAILI_Fast_rdma_ok(vc, 0);
            DEBUG_PRINT("[send], rdma ok: %d\n", rdma_ok);
            if (rdma_ok != 0) {
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Fast_rdma_send_complete(vc, iov,
                                                              n_iov, &nb,
                                                              &buf);
                DEBUG_PRINT("[send: send progress] mpi_errno %d, nb %d\n",
                            mpi_errno == MPI_SUCCESS, nb);
                assert(NULL == buf->sreq);
            } else
#endif
            {
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, &nb,
                                                 &buf);
                DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno,
                            nb);
                assert(NULL == buf->sreq);
            }
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
            DEBUG_PRINT("ajust iov finish: %d, ca %d\n", finished,
                        sreq->dev.ca);
        } while (!finished && !msg_buffered);

        if (finished && sreq->dev.ca != MPIDI_CH3_CA_COMPLETE) {
            MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
            nb = 0;
            complete = 0;
        } else if (finished) {
            complete = 1;
        }
    } while (1 != msg_buffered && 0 == complete);

    DEBUG_PRINT("exit loop with complete %d, msg_buffered %d\n", complete,
                msg_buffered);
    if (0 == complete && 1 == msg_buffered) {
        /* there are more data, buf no credit available, return for now */
        sreq->mrail.nearly_complete = 0;
    } else if (1 == msg_buffered) {
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

        sreq = flowlist->mrail.sreq_head;
        while (sreq != NULL) {
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
        assert(VAPI_PROTOCOL_R3 == rreq->mrail.protocol ||
               VAPI_PROTOCOL_RPUT == rreq->mrail.protocol);
    }

    mpi_errno = MPIDI_CH3I_MRAIL_Fill_Request(rreq, buffer, skipsize, &nb);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**fail | fill request",
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
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER,
                                         "**fail | fill request", 0);
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
#define FUNCNAME MPIDI_CH3_Rendezvouz_r3_recv_data
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Rendezvous_rput_finish(MPIDI_VC_t * vc,
                                     MPIDI_CH3_Pkt_rput_finish_t * rf_pkt)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;
    MPID_Request *rreq;
    MPID_Request_get_ptr(rf_pkt->receiver_req_id, rreq);

    if (1 == rreq->mrail.rndv_buf_alloc) {
        /* If we are using datatype, then need to unpack data from tmpbuf */
        int iter;
        uintptr_t buf;
        int copied = 0;

        buf = (uintptr_t) rreq->mrail.rndv_buf;

        for (iter = 0; iter < rreq->dev.iov_count; iter++) {
            memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                   (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
            buf += rreq->dev.iov[iter].MPID_IOV_LEN;
            copied += rreq->dev.iov[iter].MPID_IOV_LEN;
        }

        MPIDI_CH3I_Request_adjust_iov(rreq, copied);

        while (rreq->dev.ca != MPIDI_CH3_CA_COMPLETE &&
               rreq->dev.ca != MPIDI_CH3_CA_UNPACK_SRBUF_AND_COMPLETE &&
               rreq->dev.ca != MPIDI_CH3_CA_UNPACK_UEBUF_AND_COMPLETE) {
            /* XXX: dev.ca should only be CA_COMPLETE? */
            if (rreq->dev.ca != MPIDI_CH3_CA_RELOAD_IOV &&
                rreq->dev.ca != MPIDI_CH3_CA_UNPACK_SRBUF_AND_RELOAD_IOV) {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                                 FCNAME, __LINE__,
                                                 MPI_ERR_OTHER, "**ch3|loadrecviov", "**ch3|we have changed datatype processing that \
                                         this status is no longer valid %s", "MPIDI_CH3_CA_RELOAD_IOV");
                goto fn_exit;
            }
            /* end of XXX */
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS || complete == TRUE) {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                                 FCNAME, __LINE__,
                                                 MPI_ERR_OTHER,
                                                 "**ch3|loadrecviov",
                                                 "**ch3|loadrecviov %s",
                                                 "MPIDI_CH3_CA_RELOAD_IOV");
                goto fn_exit;
            }

            copied = 0;
            for (iter = 0; iter < rreq->dev.iov_count; iter++) {
                memcpy(rreq->dev.iov[iter].MPID_IOV_BUF,
                       (void *) buf, rreq->dev.iov[iter].MPID_IOV_LEN);
                buf += rreq->dev.iov[iter].MPID_IOV_LEN;
                copied += rreq->dev.iov[iter].MPID_IOV_LEN;
            }

            MPIDI_CH3I_Request_adjust_iov(rreq, copied);
        }
    } else {
        rreq->mrail.rndv_buf = NULL;
    }


    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(rreq);

    mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, rreq, &complete);

    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno,
                                 MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**fail", 0);
    }
    if (complete) {
        vc->ch.recv_active = NULL;
    } else {
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

    if (VAPI_PROTOCOL_R3 == req->mrail.protocol) {
        req->mrail.partner_id = get_resp_pkt->request_handle;
        RENDEZVOUS_IN_PROGRESS(vc, req);
        req->mrail.nearly_complete = 0;
        PUSH_FLOWLIST(vc);
    } else {
        MPID_IOV iov;
        int n_iov = 1;
        int rdma_ok, nb;
        MPIDI_CH3I_MRAILI_Rndv_info_t rndv;

        MPIDI_CH3I_MRAIL_SET_REMOTE_RNDV_INFO(&rndv, req);
        MPIDI_CH3I_MRAILI_Get_rndv_rput(vc, req, &rndv);

        if (VAPI_PROTOCOL_R3 == req->mrail.protocol) {
            req->mrail.partner_id = get_resp_pkt->request_handle;
            RENDEZVOUS_IN_PROGRESS(vc, req);
            req->mrail.nearly_complete = 0;
            PUSH_FLOWLIST(vc);
        } else {
            iov.MPID_IOV_BUF = get_resp_pkt;
            iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_get_resp_t);

            get_resp_pkt->protocol = req->mrail.protocol;
#if defined(RDMA_FAST_PATH)
            rdma_ok =
                MPIDI_CH3I_MRAILI_Fast_rdma_ok(vc,
                                               sizeof
                                               (MPIDI_CH3_Pkt_get_resp_t));
            if (rdma_ok) {
                /* the packet header and the data now is in rdma fast buffer */
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Fast_rdma_send_complete(vc, &iov,
                                                              n_iov, &nb,
                                                              &v);
                if (mpi_errno == MPI_SUCCESS
                    || mpi_errno == MPI_MRAIL_MSG_QUEUED) {
                    mpi_errno = MPI_SUCCESS;
                } else {
                    vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                    /* TODO: Create an appropriate error message based on the value of errno
                     * */
                    req->status.MPI_ERROR = MPI_ERR_INTERN;
                    /* MT - CH3U_Request_complete performs write barrier */
                    MPIDI_CH3U_Request_complete(req);
                }
            } else
#endif
            {
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Eager_send(vc, &iov, n_iov, &nb, &v);
                if (mpi_errno == MPI_SUCCESS
                    || mpi_errno == MPI_MRAIL_MSG_QUEUED) {
                    mpi_errno = MPI_SUCCESS;
                } else {
                    vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                    /* TODO: Create an appropriate error message based on the value of errno
                     * */
                    req->status.MPI_ERROR = MPI_ERR_INTERN;
                    /* MT - CH3U_Request_complete performs write barrier */
                    MPIDI_CH3U_Request_complete(req);
                }
            }
            /* mark MPI send complete when VIA send completes */
            v->sreq = (void *) req;
        }
    }
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

    assert(VAPI_PROTOCOL_RPUT == req->mrail.protocol);

    if (1 == req->mrail.rndv_buf_alloc) {
        /* If we are using datatype, then need to unpack data from tmpbuf */
        int iter;
        uintptr_t buf;

        buf = (uintptr_t) req->mrail.rndv_buf;

        for (iter = 0; iter < req->dev.iov_count; iter++) {
            memcpy(req->dev.iov[iter].MPID_IOV_BUF,
                   (void *) buf, req->dev.iov[iter].MPID_IOV_LEN);
            buf += req->dev.iov[iter].MPID_IOV_LEN;
        }
        while (req->dev.ca != MPIDI_CH3_CA_COMPLETE) {
            /* XXX: dev.ca should only be CA_COMPLETE? */
            if (req->dev.ca != MPIDI_CH3_CA_RELOAD_IOV) {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                       FCNAME, __LINE__,
                                       MPI_ERR_OTHER, "**ch3|loadrecviov", 
                                        "**ch3|we have changed datatype processing that"
                                        "this status is no longer valid %s", 
                                        "MPIDI_CH3_CA_RELOAD_IOV");
                goto fn_exit;
            }
            /* end of XXX */
            mpi_errno = MPIDI_CH3U_Request_load_recv_iov(req);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS) {
                goto fn_exit;
            }
            for (iter = 0; iter < req->dev.iov_count; iter++) {
                memcpy(req->dev.iov[iter].MPID_IOV_BUF,
                       (void *) buf, req->dev.iov[iter].MPID_IOV_LEN);
                buf += req->dev.iov[iter].MPID_IOV_LEN;
            }
        }
    } else {
        req->mrail.rndv_buf = NULL;
    }

    MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(req);

    mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);

    if (mpi_errno != MPI_SUCCESS) {
        goto fn_exit;
    }
    MPIU_Assert(TRUE == complete);

  fn_exit:
    return mpi_errno;
}

