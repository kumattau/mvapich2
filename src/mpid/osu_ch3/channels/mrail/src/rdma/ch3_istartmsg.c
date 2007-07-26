/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
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


#include "mpidi_ch3_impl.h"
#ifdef MPICH_DBG_OUTPUT
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#endif

#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)                                  \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)

#ifndef DEBUG
#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)
#endif

#undef FUNCNAME
#define FUNCNAME create_request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPID_Request * create_request(void * hdr, MPIDI_msg_sz_t hdr_sz,
					    MPIU_Size_t nb)
{
    MPID_Request * sreq;
    MPIDI_STATE_DECL(MPID_STATE_CREATE_REQUEST);

    MPIDI_FUNC_ENTER(MPID_STATE_CREATE_REQUEST);

    sreq = MPID_Request_create();
    /* --BEGIN ERROR HANDLING-- */
    if (sreq == NULL)
        return NULL;
    /* --END ERROR HANDLING-- */
    MPIU_Object_set_ref(sreq, 2);
    sreq->kind = MPID_REQUEST_SEND;
    sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) hdr;
    sreq->ch.reqtype = REQUEST_NORMAL;
    sreq->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)((char *) &sreq->ch.pkt + nb);
    sreq->dev.iov[0].MPID_IOV_LEN = hdr_sz - nb;
    sreq->dev.iov_count = 1;
    sreq->dev.OnDataAvail = 0;

    MPIDI_FUNC_EXIT(MPID_STATE_CREATE_REQUEST);
    return sreq;
}

#ifdef _SMP_
static int MPIDI_CH3_SMP_iStartMsg(MPIDI_VC_t * vc, void *pkt,
                                          MPIDI_msg_sz_t pkt_sz,
                                          MPID_Request ** sreq_ptr);
#endif
/*
 * MPIDI_CH3_iStartMsg() attempts to send the message immediately.  If the
 * entire message is successfully sent, then NULL is returned.  Otherwise a
 * request is allocated, the header is copied into the request, and a pointer
 * to the request is returned.  An error condition also results in a request be
 * allocated and the errror being returned in the status field of the
 * request.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iStartMsg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iStartMsg(MPIDI_VC_t * vc, void *pkt, MPIDI_msg_sz_t pkt_sz,
                        MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_IOV iov[1];
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ISTARTMSG);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ISTARTMSG);

    MPIU_DBG_PRINTF(("ch3_istartmsg\n"));
    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
#ifdef _SMP_
    if (SMP_INIT && vc->smp.local_nodes >= 0 &&
        vc->smp.local_nodes != smpi.my_local_id) {
        mpi_errno = MPIDI_CH3_SMP_iStartMsg(vc, pkt, pkt_sz,sreq_ptr);
        MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
        MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISTARTMSG);
        return mpi_errno;
    }
#endif

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
    /*Detect whether the packet is CTS*/
    MPIDI_CH3_Pkt_t *upkt = (MPIDI_CH3_Pkt_t *)pkt;
    if (upkt->type == MPIDI_CH3_PKT_RNDV_CLR_TO_SEND) {
        MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt = &(upkt->rndv_clr_to_send);
        if (cts_pkt->rndv.protocol == VAPI_PROTOCOL_RPUT) {
            /*If using rput protocol, keep track of the request*/
            MPID_Request *rreq;
            MPID_Request_get_ptr(cts_pkt->receiver_req_id, rreq);
            MPIDI_CH3I_CR_req_enqueue(rreq, vc);
        }
    }
#endif

    /*CM code*/
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE 
    || !MPIDI_CH3I_CM_SendQ_empty(vc)) {
        /*Request need to be queued*/
        MPIDI_DBG_PRINTF((55, FCNAME, "not connected, enqueuing"));
        sreq = create_request(pkt, pkt_sz, 0);
        MPIDI_CH3I_CM_SendQ_enqueue(vc, sreq);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED)  {
            MPIDI_CH3I_CM_Connect(vc);
        }
        goto fn_exit;
    }

    if (MPIDI_CH3I_SendQ_empty(vc)) {   /* MT */
        int nb;
        int pkt_len;
        vbuf *buf;

        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */

        iov[0].MPID_IOV_BUF = pkt;
        iov[0].MPID_IOV_LEN = pkt_sz;
        pkt_len = pkt_sz;

        /* TODO: Codes to send pkt through send/recv path */
        mpi_errno =
            MPIDI_CH3I_MRAILI_Eager_send(vc, iov, 1, pkt_len, &nb, &buf);
        DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);

        if (mpi_errno == MPI_SUCCESS) {
            DEBUG_PRINT("[send path] eager send return %d bytes\n", nb);
            goto fn_exit;
        } else if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
            /* fast rdma ok but cannot send: there is no send wqe available */
            sreq = create_request(pkt, pkt_sz, 0);
            buf->sreq = (void *) sreq;
            mpi_errno = MPI_SUCCESS;
            goto fn_exit;
        } else {
            sreq = MPID_Request_create();
            if (sreq == NULL) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER, "**nomem", 0);
                goto fn_exit;
            }
            sreq->kind = MPID_REQUEST_SEND;
            sreq->cc = 0;
            /* TODO: Create an appropriate error message based on the value of errno
             * */
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
        }
    } else {
        MPIDI_DBG_PRINTF((55, FCNAME,
                          "send in progress, request enqueued"));
        sreq = create_request(pkt, pkt_sz, 0);
        MPIDI_CH3I_SendQ_enqueue(vc, sreq);
    }

  fn_exit:
    *sreq_ptr = sreq;
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    DEBUG_PRINT("Exiting istartmsg\n");
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISTARTMSG);
    return mpi_errno;
}

#ifdef _SMP_

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SMP_iStartMsg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3_SMP_iStartMsg(MPIDI_VC_t * vc, void *pkt,
                                          MPIDI_msg_sz_t pkt_sz,
                                          MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_IOV iov[1];
    MPIDI_CH3_Pkt_send_t *pkt_header;

    DEBUG_PRINT("entering ch3_istartmsg\n");

    pkt_header = (MPIDI_CH3_Pkt_send_t *)pkt;

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {       /* MT */
        int nb;

        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */

        iov[0].MPID_IOV_BUF = pkt;
        iov[0].MPID_IOV_LEN = pkt_sz;

        if(pkt_header->type == MPIDI_CH3_PKT_RNDV_R3_DATA)
            mpi_errno = MPIDI_CH3I_SMP_writev_rndv_header(vc, iov, 1, &nb);
        else
            mpi_errno = MPIDI_CH3I_SMP_writev(vc, iov, 1, &nb);

        if (mpi_errno == MPI_SUCCESS) {
            if (nb == pkt_sz) {
                DEBUG_PRINT("data sent immediately\n");
                /* done.  get us out of here as quickly as possible. */
            } else {
                DEBUG_PRINT("send delayed, request enqueued\n");
                sreq = create_request(pkt, pkt_sz, nb);
                if(pkt_header->type == MPIDI_CH3_PKT_RNDV_R3_DATA) 
                    sreq->ch.reqtype = REQUEST_RNDV_R3_HEADER;
                MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                vc->smp.send_active = sreq;
            }
        } else {
            MPIDI_DBG_PRINTF((55, FCNAME,
                              "ERROR - MPIDI_CH3I_RDMA_put_datav failed, "
                              "errno=%d:%s", errno, strerror(errno)));
            sreq = MPID_Request_create();
            if (sreq == NULL) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**nomem", 0);
                goto fn_exit;
            }
            sreq->kind = MPID_REQUEST_SEND;
            sreq->cc = 0;
            /* TODO: Create an appropriate error message based on the value
               of errno */
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
        }
    } else {
        MPIDI_DBG_PRINTF((55, FCNAME,
                          "send in progress, request enqueued"));
        sreq = create_request(pkt, pkt_sz, 0);
        if(pkt_header->type == MPIDI_CH3_PKT_RNDV_R3_DATA)
            sreq->ch.reqtype = REQUEST_RNDV_R3_HEADER; 
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
    }

  fn_exit:
    *sreq_ptr = sreq;

    return mpi_errno;

}
#endif
