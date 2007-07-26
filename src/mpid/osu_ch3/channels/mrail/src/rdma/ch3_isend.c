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

/* static void update_request(MPID_Request * sreq, void * pkt, int pkt_sz, int nb) */
#undef update_request
#ifdef MPICH_DBG_OUTPUT
#define update_request(sreq, pkt, pkt_sz, nb) \
{ \
    MPIDI_STATE_DECL(MPID_STATE_UPDATE_REQUEST); \
    MPIDI_FUNC_ENTER(MPID_STATE_UPDATE_REQUEST); \
    sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) pkt; \
    sreq->dev.iov[0].MPID_IOV_BUF = (char *) &sreq->ch.pkt + nb; \
    sreq->dev.iov[0].MPID_IOV_LEN = pkt_sz - nb; \
    sreq->dev.iov_count = 1; \
    sreq->ch.iov_offset = 0; \
    MPIDI_FUNC_EXIT(MPID_STATE_UPDATE_REQUEST); \
}
#else
#define update_request(sreq, pkt, pkt_sz, nb) \
{ \
    MPIDI_STATE_DECL(MPID_STATE_UPDATE_REQUEST); \
    MPIDI_FUNC_ENTER(MPID_STATE_UPDATE_REQUEST); \
    sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) pkt; \
    sreq->dev.iov[0].MPID_IOV_BUF = (char *) &sreq->ch.pkt + nb; \
    sreq->dev.iov[0].MPID_IOV_LEN = pkt_sz - nb; \
    sreq->dev.iov_count = 1; \
    sreq->ch.iov_offset = 0; \
    MPIDI_FUNC_EXIT(MPID_STATE_UPDATE_REQUEST); \
}
#endif

#define DEBUG_PRINT(args...)

#ifdef _SMP_
static int MPIDI_CH3_SMP_iSend(MPIDI_VC_t * vc, MPID_Request * sreq, void *pkt,
                        MPIDI_msg_sz_t pkt_sz);
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iSend(MPIDI_VC_t * vc, MPID_Request * sreq, void *pkt,
                    MPIDI_msg_sz_t pkt_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_IOV iov[1];
    int complete;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ISEND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ISEND);

    MPIU_DBG_PRINTF(("ch3_isend\n"));
    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));
#ifdef _SMP_
    if (SMP_INIT && vc->smp.local_nodes >= 0 &&
        vc->smp.local_nodes != smpi.my_local_id) {
        mpi_errno =
            MPIDI_CH3_SMP_iSend(vc, sreq, pkt, pkt_sz);
        MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
        MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISEND);
        return mpi_errno;
    }
#endif

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    /*CM code*/
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE 
    || !MPIDI_CH3I_CM_SendQ_empty(vc)) {
        /*Request need to be queued*/
        MPIDI_DBG_PRINTF((55, FCNAME, "not connected, enqueuing"));
        update_request(sreq, pkt, pkt_sz, 0);
        MPIDI_CH3I_CM_SendQ_enqueue(vc, sreq);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED)  {
            MPIDI_CH3I_CM_Connect(vc);
        }
        goto fn_exit;
    }

    /* The RDMA implementation uses a fixed length header, the size of which is the maximum of all possible packet headers */

    if (MPIDI_CH3I_SendQ_empty(vc)) {   /* MT */
        int nb;
        vbuf *buf;
        MPIDI_DBG_PRINTF((55, FCNAME,
                          "send queue empty, attempting to write"));

        /* MT: need some signalling to lock down our right to use the channel, thus insuring that the progress engine does
           also try to write */

        iov[0].MPID_IOV_BUF = pkt;
        iov[0].MPID_IOV_LEN = pkt_sz;

        mpi_errno =
            MPIDI_CH3I_MRAILI_Eager_send(vc, iov, 1, pkt_sz, &nb, &buf);
        DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);
       
        if (mpi_errno == MPI_SUCCESS) {
            DEBUG_PRINT("[send path] eager send return %d bytes\n", nb);

            if (nb == 0) {
                /* under layer cannot send out the msg because there is no credit or
                 * no send wqe available
                 DEBUG_PRINT("Send 0 bytes\n");
                 create_request(sreq, iov, n_iov, 0, 0);
                 MPIDI_CH3I_SendQ_enqueue(vc, sreq);
                 */
            } else {
                MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                if (!complete) {
                    /* NOTE: dev.iov_count is used to detect completion instead of
                     * cc
                     * because the transfer may be complete, but
                     request may still be active (see MPI_Ssend()) */
                    MPIDI_CH3I_SendQ_enqueue_head(vc, sreq);
                    vc->ch.send_active = sreq;
                } else {
                    vc->ch.send_active = MPIDI_CH3I_SendQ_head(vc);
                }

            }
        } else if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
            buf->sreq = (void *) sreq;
            mpi_errno = MPI_SUCCESS;
        } else {
            /* Connection just failed.  Mark the request complete and return an
             * error. */
            vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
            /* TODO: Create an appropriate error message based on the value of errno
             * */
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
            /* MT - CH3U_Request_complete performs write barrier */
            MPIDI_CH3U_Request_complete(sreq);

        }
        goto fn_exit;

    } else {
        MPIDI_DBG_PRINTF((55, FCNAME, "send queue not empty, enqueuing"));
        update_request(sreq, pkt, pkt_sz, 0);
        MPIDI_CH3I_SendQ_enqueue(vc, sreq);
    }
  fn_exit:
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISEND);
    return mpi_errno;
}

#ifdef _SMP_
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3_SMP_iSend(MPIDI_VC_t * vc, MPID_Request * sreq, void *pkt,
                        MPIDI_msg_sz_t pkt_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_IOV iov[1];
    int complete;

    DEBUG_PRINT("entering ch3_isend\n");

    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {       /* MT */
        int nb;

        iov[0].MPID_IOV_BUF = pkt;
        iov[0].MPID_IOV_LEN = pkt_sz;

        mpi_errno = MPIDI_CH3I_SMP_writev(vc, iov, 1, &nb);
        if (mpi_errno == MPI_SUCCESS) {
            DEBUG_PRINT("wrote %d bytes\n", nb);

            if (nb == pkt_sz) {
                DEBUG_PRINT("write complete, calling MPIDI_CH3U_Handle_send_req()\n");
                MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                if (!complete) {
                    /* NOTE: dev.iov_count is used to detect completion instead of cc because
                     * the transfer may be complete, but the
                     request may still be active (see MPI_Ssend()) */
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                } else {
                    vc->smp.send_active = MPIDI_CH3I_SendQ_head(vc);
                }
            } else {
                MPIDI_DBG_PRINTF((55, FCNAME,
                                  "partial write, enqueuing at head"));
                update_request(sreq, pkt, pkt_sz, nb);
                MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                vc->smp.send_active = sreq;
            }
        } else {
            /* Connection just failed. Mark the request complete and return an error. */
            vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
            /* TODO: Create an appropriate error message based on the value of errno */
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
            /* MT -CH3U_Request_complete() performs write barrier */
            MPIDI_CH3U_Request_complete(sreq);
        }
    } else {
        DEBUG_PRINT("send queue not empty, enqueuing\n");
        update_request(sreq, pkt, pkt_sz, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
    }

    return mpi_errno;
}
#endif
