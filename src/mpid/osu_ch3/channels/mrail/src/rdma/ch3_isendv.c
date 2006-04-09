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

/*static void update_request(MPID_Request * sreq, MPID_IOV * iov, int count, int offset, int nb)*/
#undef update_request
#ifdef MPICH_DBG_OUTPUT
#define update_request(sreq, iov, count, offset, nb) \
{ \
    int i; \
    MPIDI_STATE_DECL(MPID_STATE_UPDATE_REQUEST); \
    MPIDI_FUNC_ENTER(MPID_STATE_UPDATE_REQUEST); \
    for (i = 0; i < count; i++) \
    { \
	sreq->dev.iov[i] = iov[i]; \
    } \
    if (offset == 0) \
    { \
	sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) iov[0].MPID_IOV_BUF; \
	sreq->dev.iov[0].MPID_IOV_BUF = (void*)&sreq->ch.pkt; \
    } \
    sreq->dev.iov[offset].MPID_IOV_BUF = (char *) sreq->dev.iov[offset].MPID_IOV_BUF + nb; \
    sreq->dev.iov[offset].MPID_IOV_LEN -= nb; \
    sreq->dev.iov_count = count; \
    sreq->ch.iov_offset = offset; \
    MPIDI_FUNC_EXIT(MPID_STATE_UPDATE_REQUEST); \
}
#else
#define update_request(sreq, iov, count, offset, nb) \
{ \
    int i; \
    MPIDI_STATE_DECL(MPID_STATE_UPDATE_REQUEST); \
    MPIDI_FUNC_ENTER(MPID_STATE_UPDATE_REQUEST); \
    for (i = 0; i < count; i++) \
    { \
	sreq->dev.iov[i] = iov[i]; \
    } \
    if (offset == 0) \
    { \
	sreq->ch.pkt = *(MPIDI_CH3_Pkt_t *) iov[0].MPID_IOV_BUF; \
	sreq->dev.iov[0].MPID_IOV_BUF = (void*)&sreq->ch.pkt; \
    } \
    sreq->dev.iov[offset].MPID_IOV_BUF = (char *) sreq->dev.iov[offset].MPID_IOV_BUF + nb; \
    sreq->dev.iov[offset].MPID_IOV_LEN -= nb; \
    sreq->dev.iov_count = count; \
    sreq->ch.iov_offset = offset; \
    MPIDI_FUNC_EXIT(MPID_STATE_UPDATE_REQUEST); \
}
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

#ifdef _SMP_
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SMP_iSendv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3_SMP_iSendv(MPIDI_VC_t * vc,
                                       MPID_Request * sreq, MPID_IOV * iov,
                                       int n_iov)
{
    int mpi_errno = MPI_SUCCESS;
    int complete;

    DEBUG_PRINT("Entering %s\n", FUNCNAME);

    /* Connection already formed.  If send queue is empty attempt to send data,
     * queuing any unsent data. */
    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {   /* MT */
        int nb;

        DEBUG_PRINT("Send queue empty, attempting to write\n");

        /* MT - need some signalling to lock down our right to use the channel, thus
         * insuring that the progress engine does
         also try to write */
        mpi_errno = MPIDI_CH3I_SMP_writev(vc, iov, n_iov, &nb);
        if (mpi_errno == MPI_SUCCESS) {
            int offset = 0;

            DEBUG_PRINT("wrote %d bytes\n", nb);

            while (offset < n_iov) {
                if ((int) iov[offset].MPID_IOV_LEN <= nb) {
                    nb -= iov[offset].MPID_IOV_LEN;
                    offset++;
                } else {
                    DEBUG_PRINT("partial write, enqueuing at head\n");
                    update_request(sreq, iov, n_iov, offset, nb);
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                    break;
                }
            }
            if (offset == n_iov) {
                DEBUG_PRINT("write complete, calling MPIDI_CH3U_Handle_send_req()\n");
                MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                if (!complete) {
                    /* NOTE: dev.iov_count is used to detect completion instead of cc because the
                     * transfer may be complete, but
                     request may still be active (see MPI_Ssend()) */
                    MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, sreq);
                    vc->smp.send_active = sreq;
                } else {
                    vc->smp.send_active = MPIDI_CH3I_SMP_SendQ_head(vc);
                }
            }
        } else {
            /* Connection just failed.  Mark the request complete and return an error. */
            vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
            /* TODO: Create an appropriate error message based on the value of errno */
            sreq->status.MPI_ERROR = MPI_ERR_INTERN;
            /* MT - CH3U_Request_complete performs write barrier */
            MPIDI_CH3U_Request_complete(sreq);
        }
    } else {
        MPIDI_DBG_PRINTF((55, FCNAME, "send queue not empty, enqueuing"));
        update_request(sreq, iov, n_iov, 0, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
    }

    DEBUG_PRINT("exiting %s\n", FUNCNAME);
    return mpi_errno;
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSendv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_iSendv(MPIDI_VC_t * vc, MPID_Request * sreq, MPID_IOV * iov,
                     int n_iov)
{
    int mpi_errno = MPI_SUCCESS;
    void *databuf = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ISENDV);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ISENDV);
    MPIU_DBG_PRINTF(("ch3_isendv\n"));
    MPIDI_DBG_PRINTF((50, FCNAME, "entering"));

#ifdef _SMP_
    if (vc->smp.local_nodes >= 0 &&
        vc->smp.local_nodes != smpi.my_local_id) {
        mpi_errno = MPIDI_CH3_SMP_iSendv(vc, sreq, iov, n_iov);
        MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
        MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISENDV);
        return mpi_errno;
    }
#endif

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
    if (MPIDI_CH3I_SendQ_empty(vc)) {   /* MT */
        int nb;
        int pkt_len;
        int complete;
#ifdef RDMA_FAST_PATH
        int rdma_ok;
#endif
        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */
        Calculate_IOV_len(iov, n_iov, pkt_len);

        if (pkt_len > MRAIL_MAX_EAGER_SIZE) {
            memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
            sreq->dev.iov_count = n_iov;
            mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq);
            if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                mpi_errno = MPI_SUCCESS;
            }
            goto fn_exit;
        }

        if (sreq->dev.ca != MPIDI_CH3_CA_COMPLETE) {
            /*reload iov */
            void *tmpbuf;
            int iter_iov;

            tmpbuf = MPIU_Malloc(sreq->dev.segment_size + pkt_len);
            databuf = tmpbuf;
            pkt_len = 0;
            /* First copy whatever has already been in iov set */
            for (iter_iov = 0; iter_iov < n_iov; iter_iov++) {
                memcpy(tmpbuf, iov[iter_iov].MPID_IOV_BUF,
                       iov[iter_iov].MPID_IOV_LEN);
                tmpbuf = (void *) ((unsigned long) tmpbuf +
                                   iov[iter_iov].MPID_IOV_LEN);
                pkt_len += iov[iter_iov].MPID_IOV_LEN;
            }
            DEBUG_PRINT("Pkt len after first stage %d\n", pkt_len);
            /* Second reload iov and copy */
            do {
                sreq->dev.iov_count = MPID_IOV_LIMIT;
                mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq,
                                                             sreq->dev.iov,
                                                             &sreq->dev.
                                                             iov_count);
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno != MPI_SUCCESS) {
                    mpi_errno =
                        MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**ch3|loadsendiov", 0);
                    goto fn_exit;
                }
                for (iter_iov = 0; iter_iov < sreq->dev.iov_count;
                     iter_iov++) {
                    memcpy(tmpbuf, sreq->dev.iov[iter_iov].MPID_IOV_BUF,
                           sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                    tmpbuf =
                        (void *) ((unsigned long) tmpbuf +
                                  sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                    pkt_len += sreq->dev.iov[iter_iov].MPID_IOV_LEN;
                }
            } while (sreq->dev.ca != MPIDI_CH3_CA_COMPLETE);
            iov[0].MPID_IOV_BUF = databuf;
            iov[0].MPID_IOV_LEN = pkt_len;
            n_iov = 1;
        }

        if (pkt_len > MRAIL_MAX_EAGER_SIZE) {
            memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
            sreq->dev.iov_count = n_iov;
            mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq);
            if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                mpi_errno = MPI_SUCCESS;
            }
            goto fn_exit;
        }
#ifdef RDMA_FAST_PATH
        DEBUG_PRINT("[send], n_iov: %d, pkt_len %d\n", n_iov, pkt_len);
        rdma_ok = MPIDI_CH3I_MRAILI_Fast_rdma_ok(vc, pkt_len);
        DEBUG_PRINT("[send], rdma ok: %d\n", rdma_ok);
        if (rdma_ok != 0) {
            /* send pkt through rdma fast path */
            /* take care of the header caching */
            vbuf *buf;

            /* the packet header and the data now is in rdma fast buffer */
            mpi_errno =
                MPIDI_CH3I_MRAILI_Fast_rdma_send_complete(vc, iov, n_iov,
                                                          &nb, &buf);
            DEBUG_PRINT("[send: send progress] mpi_errno %d, nb %d\n",
                        mpi_errno == MPI_SUCCESS, nb);
            if (mpi_errno == MPI_SUCCESS) {
                MPIU_DBG_PRINTF(("ch3_istartmsgv: put_datav returned %d bytes\n", nb));

                if (nb == 0) {
                    /* fast rdma ok but cannot send: there is no send wqe available */
                } else {
                    DEBUG_PRINT("Start handle req\n");
                    MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                    DEBUG_PRINT("Finish handle req with complete %d\n",
                                complete);
                    if (!complete) {
                        /* NOTE: dev.iov_count is used to detect completion instead of cc
                         * because the transfer may be complete, but
                         request may still be active (see MPI_Ssend()) */
                        MPIDI_CH3I_SendQ_enqueue_head(vc, sreq);
                        vc->ch.send_active = sreq;
                        assert(0);
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
        } else
#endif
        {
            /* TODO: Codes to send pkt through send/recv path */
            vbuf *buf;

            mpi_errno =
                MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, &nb, &buf);
            DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);
            if (mpi_errno == MPI_SUCCESS) {
                DEBUG_PRINT("[send path] eager send return %d bytes\n",
                            nb);

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
                /* TODO: Create an appropriate error message based on the value of errno */
                sreq->status.MPI_ERROR = MPI_ERR_INTERN;
                /* MT - CH3U_Request_complete performs write barrier */
                MPIDI_CH3U_Request_complete(sreq);

            }
            goto fn_exit;
        }
    } else {
        MPIDI_DBG_PRINTF((55, FCNAME, "send queue not empty, enqueuing"));
        update_request(sreq, iov, n_iov, 0, 0);
        MPIDI_CH3I_SendQ_enqueue(vc, sreq);
    }

  fn_exit:
    if (databuf)
        MPIU_Free(databuf);
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ISENDV);
    return mpi_errno;

}

