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



#include "mpidi_ch3_impl.h"

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

/*static MPID_Request * create_request(MPID_IOV * iov, int iov_count, int iov_offset, int nb)*/
#undef create_request
#define create_request(sreq, iov, count, offset, nb) \
{ \
    /*int i;*/ \
    MPIDI_STATE_DECL(MPID_STATE_CREATE_REQUEST); \
    MPIDI_STATE_DECL(MPID_STATE_MEMCPY); \
    MPIDI_FUNC_ENTER(MPID_STATE_CREATE_REQUEST); \
    sreq = MPIDI_CH3_Request_create(); \
    MPIU_Assert(sreq != NULL); \
    MPIU_Object_set_ref(sreq, 2); \
    sreq->kind = MPID_REQUEST_SEND; \
    MPIDI_FUNC_ENTER(MPID_STATE_MEMCPY); \
    memcpy(sreq->dev.iov, iov, count * sizeof(MPID_IOV)); \
    MPIDI_FUNC_EXIT(MPID_STATE_MEMCPY); \
    /*for (i = 0; i < count; i++) { sreq->dev.iov[i] = iov[i]; }*/ \
    if (offset == 0) \
    { \
	/* memcpy(&sreq->ch.pkt, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN); */ \
	sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) iov[0].MPID_IOV_BUF; \
	sreq->dev.iov[0].MPID_IOV_BUF = (void*)&sreq->ch.pkt; \
    } \
    sreq->dev.iov[offset].MPID_IOV_BUF = ((char*)sreq->dev.iov[offset].MPID_IOV_BUF) + nb; \
    sreq->dev.iov[offset].MPID_IOV_LEN -= nb; \
    sreq->ch.iov_offset = offset; \
    sreq->dev.iov_count = count; \
    sreq->dev.ca = MPIDI_CH3_CA_COMPLETE; \
    MPIDI_FUNC_EXIT(MPID_STATE_CREATE_REQUEST); \
}

#ifdef _SMP_
static int MPIDI_CH3_SMP_iStartMsgv(MPIDI_VC_t * vc, MPID_IOV * iov,
                                           int n_iov,
                                           MPID_Request ** sreq_ptr);
#endif
/*
 * MPIDI_CH3_iStartMsgv() attempts to send the message immediately.  If the
 * entire message is successfully sent, then NULL is returned.  Otherwise a
 * request is allocated, the iovec and the first buffer pointed to by the
 * iovec (which is assumed to be a MPIDI_CH3_Pkt_t) are copied into the
 * request, and a pointer to the request is returned.  An error condition also
 * results in a request be allocated and the errror being returned in the
 * status field of the request.
 */

/* XXX - What do we do if MPIDI_CH3_Request_create() returns NULL???  If
   MPIDI_CH3_iStartMsgv() returns NULL, the calling code assumes the request
   completely successfully, but the reality is that we couldn't allocate the
   memory for a request.  This seems like a flaw in the CH3 API. */

/* NOTE - The completion action associated with a request created by
   CH3_iStartMsgv() is alway MPIDI_CH3_CA_COMPLETE.  This implies that
   CH3_iStartMsgv() can only be used when the entire message can be described
   by a single iovec of size MPID_IOV_LIMIT. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iStartMsgv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iStartMsgv(MPIDI_VC_t * vc, MPID_IOV * iov, int n_iov,
                         MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    vbuf *buf;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ISTARTMSGV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ISTARTMSGV);
    DEBUG_PRINT("ch3_istartmsgv, header %d\n", ((MPIDI_CH3_Pkt_t *)iov[0].MPID_IOV_BUF)->type);
    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));
#ifdef MPICH_DBG_OUTPUT
    if (n_iov > MPID_IOV_LIMIT) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**arg", 0);
        goto fn_exit;
    }
    if (iov[0].MPID_IOV_LEN > sizeof(MPIDI_CH3_Pkt_t)) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**arg", 0);
        goto fn_exit;
    }
#endif

    MPIDI_DBG_Print_packet((MPIDI_CH3_Pkt_t *) iov[0].MPID_IOV_BUF);

#ifdef _SMP_
    DEBUG_PRINT("remote local nodes %d, myid %d\n", 
                vc->smp.local_nodes, smpi.my_local_id);
    if (vc->smp.local_nodes >= 0 &&
        vc->smp.local_nodes != smpi.my_local_id) {
        mpi_errno =
            MPIDI_CH3_SMP_iStartMsgv(vc, iov, n_iov, sreq_ptr);
        MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
        MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISTARTMSGV);
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
        create_request(sreq, iov, n_iov, 0, 0);
        MPIDI_CH3I_CM_SendQ_enqueue(vc, sreq);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED)  {
            MPIDI_CH3I_CM_Connect(vc);
        }
        goto fn_exit;
    }

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
    if (MPIDI_CH3I_SendQ_empty(vc)) {   /* MT */
        int nb;
        int pkt_len;
        int rdma_ok;

        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */
        Calculate_IOV_len(iov, n_iov, pkt_len);
        DEBUG_PRINT("[send], n_iov: %d, pkt_len %d\n", n_iov,
                    pkt_len);
        if (pkt_len > MRAIL_MAX_EAGER_SIZE) {
            create_request(sreq, iov, n_iov, 0, 0);
            mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq);
            if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                mpi_errno = MPI_SUCCESS;
            }
            goto fn_exit;
        }

        rdma_ok = MPIDI_CH3I_MRAILI_Fast_rdma_ok(vc, pkt_len);
        DEBUG_PRINT("[send], rdma ok: %d\n", rdma_ok);
        if (rdma_ok != 0) {
            /* send pkt through rdma fast path */
            /* take care of the header caching */
            /* the packet header and the data now is in rdma fast buffer */
            mpi_errno =
                MPIDI_CH3I_MRAILI_Fast_rdma_send_complete(vc, iov, n_iov,
                                                          &nb, &buf);
            DEBUG_PRINT("[send: send progress] mpi_errno %d, nb %d\n",
                        mpi_errno == MPI_SUCCESS, nb);
        } else {
            /* TODO: Codes to send pkt through send/recv path */
            mpi_errno =
                MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, &nb, &buf);
            DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);

        }
        if (mpi_errno == MPI_SUCCESS) {
            DEBUG_PRINT("[send path] eager send return %d bytes\n", nb);
        } else if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
            /* fast rdma ok but cannot send: there is no send wqe available */
            create_request(sreq, iov, n_iov, 0, 0);
            buf->sreq = (void *) sreq;
            mpi_errno = MPI_SUCCESS;
        } else {
            sreq = MPIDI_CH3_Request_create();
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
        goto fn_exit;
    }
    create_request(sreq, iov, n_iov, 0, 0);
    MPIDI_CH3I_SendQ_enqueue(vc, sreq);

  fn_exit:
    *sreq_ptr = sreq;
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISTARTMSGV);
    return mpi_errno;
}

#ifdef _SMP_
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SMP_iStartMsgv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3_SMP_iStartMsgv(MPIDI_VC_t * vc,
                                    MPID_IOV * iov, int n_iov,
                                    MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;

    DEBUG_PRINT("entering ch3_smp_istartmsgv\n");

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {
        int nb;
        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */
        mpi_errno = MPIDI_CH3I_SMP_writev(vc, iov, n_iov, &nb);
        if (mpi_errno == MPI_SUCCESS) {
            int offset = 0;
            DEBUG_PRINT("ch3_smp_istartmsgv: writev returned %d bytes\n",
                        nb);

            while (offset < n_iov) {
                if (nb >= (int) iov[offset].MPID_IOV_LEN) {
                    nb -= iov[offset].MPID_IOV_LEN;
                    offset++;
                } else {
                    DEBUG_PRINT
                        ("ch3_istartmsgv: shm_writev did not complete the send,\
                    allocating request\n");
                    create_request(sreq, iov, n_iov, offset, nb);
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                    break;
                }
            }
        } else {
            sreq = MPIDI_CH3_Request_create();
            if (sreq == NULL) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**nomem", 0);
                goto fn_exit;
            }
            sreq->kind = MPID_REQUEST_SEND;
            sreq->cc = 0;
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
        }
    } else {
        create_request(sreq, iov, n_iov, 0, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
    }

  fn_exit:
    *sreq_ptr = sreq;
    return mpi_errno;
}
#endif
