/* Copyright (c) 2003-2010, The Ohio State University. All rights
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

#include "ib_recv.h"
#include "ib_send.h"
#include "ib_vc.h"
#include "ib_vbuf.h"
#include "ib_errors.h"
#include "ib_process.h"
#include "ib_rdma.h"
/* add ib_poll.h for MPIDI_nem_ib_request_adjust_iov */
#include "ib_poll.h"


/** We maintain an index table to get the header size ******/
int MPIDI_CH3_Pkt_size_index[] = {
    sizeof(MPIDI_CH3_Pkt_eager_send_t),        /* 0 */
#if defined(USE_HEADER_CACHING)
    sizeof(MPIDI_nem_ib_pkt_fast_eager),
    sizeof(MPIDI_nem_ib_pkt_fast_eager_with_req),
#endif /* defined(USE_HEADER_CACHING) */
    sizeof(MPIDI_nem_ib_pkt_noop),
    sizeof(MPIDI_CH3_Pkt_rndv_clr_to_send_t),
    sizeof(MPIDI_CH3_Pkt_rndv_req_to_send_t),
    sizeof(MPIDI_CH3_Pkt_packetized_send_start_t),
    sizeof(MPIDI_CH3_Pkt_packetized_send_data_t),
    sizeof(MPIDI_nem_ib_pkt_address),
#if defined(USE_EAGER_SHORT)
    sizeof(MPIDI_CH3_Pkt_eagershort_send_t),
#endif /* defined(USE_EAGER_SHORT) */
    sizeof(MPIDI_CH3_Pkt_eager_sync_send_t),
    sizeof(MPIDI_CH3_Pkt_eager_sync_ack_t),
    sizeof(MPIDI_CH3_Pkt_ready_send_t),
    sizeof(MPIDI_CH3_Pkt_rndv_req_to_send_t),
    sizeof(MPIDI_CH3_Pkt_rndv_clr_to_send_t),
    sizeof(MPIDI_CH3_Pkt_rndv_send_t),
    sizeof(MPIDI_CH3_Pkt_cancel_send_req_t),
    sizeof(MPIDI_CH3_Pkt_cancel_send_resp_t),
    sizeof(MPIDI_CH3_Pkt_put_t),
    sizeof(MPIDI_CH3_Pkt_get_t),
    sizeof(MPIDI_CH3_Pkt_get_resp_t),
    sizeof(MPIDI_CH3_Pkt_accum_t),
    sizeof(MPIDI_CH3_Pkt_lock_t),
    sizeof(MPIDI_CH3_Pkt_lock_granted_t),
    sizeof(MPIDI_CH3_Pkt_pt_rma_done_t),
    sizeof(MPIDI_CH3_Pkt_lock_put_unlock_t),
    sizeof(MPIDI_CH3_Pkt_lock_get_unlock_t),
    sizeof(MPIDI_CH3_Pkt_lock_accum_unlock_t),
    -1,                                /* FLOW CONTROL UPDATE unused */
    sizeof(MPIDI_CH3_Pkt_close_t),
    -1
};

#define SET_CREDIT(header, c, rail) \
{                                                               \
    c->rfp.ptail_RDMA_send += header->rdma_credit;         \
    if (c->rfp.ptail_RDMA_send >= num_rdma_buffer)               \
        c->rfp.ptail_RDMA_send -= num_rdma_buffer;               \
    c->srp.credits[rail].remote_cc = header->remote_credit;\
    c->srp.credits[rail].remote_credit += header->vbuf_credit; \
}

#undef FUNCNAME
#define FUNCNAME MPIDI_nem_ib_pass_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 *  FIXME: Ideally the header size should be determined by high level macros,
 * instead of hacking the message header at the device layer
 */
int MPIDI_CH3I_nem_ib_parse_header(MPIDI_VC_t * vc,
                                  vbuf * v, void **pkt, int *header_size)
{
    void *vstart;
    MPIDI_nem_ib_pkt_comm_header *header;
#ifdef CRC_CHECK
    unsigned long crc;
#endif
    int mpi_errno = MPI_SUCCESS;
    DEBUG_PRINT("[parse header] vbuf address %p\n", v);
    vstart = v->pheader;
    header = (MPIDI_nem_ib_pkt_comm_header *)v->iheader;
    DEBUG_PRINT("[parse header] header type %d\n", header->type);

    /* set it to the header size by default */
    *header_size = sizeof(MPIDI_CH3_Pkt_t);
#ifdef CRC_CHECK
    crc = update_crc(1, (void *)((uintptr_t)header+sizeof *header),
                     v->content_size - sizeof *header);
    if (crc != header->mrail.crc) {
        int rank; PMI_Get_rank(&rank);
        MPIU_Error_printf(stderr, "CRC mismatch, get %lx, should be %lx "
                "type %d, ocntent size %d\n",
                crc, header->mrail.crc, header->type, v->content_size);
        exit( EXIT_FAILURE );
    }
#endif
    switch (header->type) {
/*header caching codes */
#ifdef USE_HEADER_CACHING
    case (MPIDI_CH3_PKT_FAST_EAGER_SEND):
    case (MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ):
        {
            /* since header caching do not have regular iheader,
             * revert back pre-adjust
             */
            v->content_size += IB_PKT_HEADER_LENGTH;
            vstart -= IB_PKT_HEADER_LENGTH;
            v->pheader -= IB_PKT_HEADER_LENGTH;
            MPIDI_nem_ib_pkt_fast_eager *fast_header = vstart;
            MPIDI_CH3_Pkt_eager_send_t *eager_header =
                (MPIDI_CH3_Pkt_eager_send_t *) VC_FIELD(vc, connection)->rfp.
                cached_incoming;
            MPIDI_nem_ib_pkt_comm_header *eager_iheader =
                (MPIDI_nem_ib_pkt_comm_header *) VC_FIELD(vc, connection)->rfp.
                cached_incoming_iheader;

            if (MPIDI_CH3_PKT_FAST_EAGER_SEND == header->type) {
                *header_size = sizeof(MPIDI_nem_ib_pkt_fast_eager);
            } else {
                *header_size =
                    sizeof(MPIDI_nem_ib_pkt_fast_eager_with_req);
                eager_header->sender_req_id =
                    ((MPIDI_nem_ib_pkt_fast_eager_with_req *)
                     vstart)->sender_req_id;
            }
            header = eager_iheader;

            DEBUG_PRINT("[receiver side] cached credit %d\n",
                        eager_iheader->rdma_credit);

            eager_header->data_sz = fast_header->bytes_in_pkt;

            *pkt = (void *) eager_header;
            DEBUG_PRINT
                ("[recv: parse header] faster headersize returned %d\n",
                 *header_size);
        }
        break;
#endif
    case (MPIDI_CH3_PKT_EAGER_SEND):
        {
            DEBUG_PRINT("[recv: parse header] pkt eager send\n");
/* header caching codes */
#ifdef USE_HEADER_CACHING
            if (v->padding != NORMAL_VBUF_FLAG) {
                /* Only cache header if the packet is from RdMA path
                 * XXXX: what is R3_FLAG?
                 */
                MPIU_Memcpy((VC_FIELD(vc, connection)->rfp.cached_incoming), vstart,
                       sizeof(MPIDI_CH3_Pkt_eager_send_t));
                MPIU_Memcpy((VC_FIELD(vc, connection)->rfp.cached_incoming_iheader), header,
                    sizeof(MPIDI_nem_ib_pkt_comm_header));
            }
#endif
            *pkt = (MPIDI_CH3_Pkt_t *) vstart;
            *header_size = sizeof(MPIDI_CH3_Pkt_t);
            DEBUG_PRINT("[recv: parse header] headersize returned %d\n",
                        *header_size);
        }
        break;
    case (MPIDI_CH3_PKT_RNDV_REQ_TO_SEND):
    case (MPIDI_CH3_PKT_RNDV_CLR_TO_SEND):
    case MPIDI_CH3_PKT_EAGER_SYNC_ACK:
    case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
    case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
    case MPIDI_NEM_PKT_LMT_RTS:
    case MPIDI_NEM_PKT_LMT_CTS:
    case MPIDI_NEM_PKT_LMT_DONE:
    case MPIDI_NEM_PKT_LMT_COOKIE:
/* CKPT codes */
#ifdef CKPT
    case MPIDI_CH3_PKT_CM_SUSPEND:
    case MPIDI_CH3_PKT_CM_REACTIVATION_DONE:
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
#endif
        {
            *pkt = vstart;
        }
        break;
    case (MPIDI_CH3_PKT_NOOP):
        {
            *pkt = v->iheader;
        }
        break;
    /* rfp codes */
    case MPIDI_CH3_PKT_ADDRESS:
        {
            *pkt = v->iheader;
            MPIDI_nem_ib_recv_addr(vc, vstart);
            break;
        }
    case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
        {
            *pkt = vstart;
            *header_size = sizeof(MPIDI_CH3_Pkt_packetized_send_start_t);
            break;
        }
    case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_packetized_send_data_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_RNDV_R3_DATA:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_rndv_r3_data_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
    case MPIDI_CH3_PKT_READY_SEND:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_PUT:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_GET:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_GET_RESP:       /*15 */
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_ACCUMULATE:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_GRANTED:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_PT_RMA_DONE:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_FLOW_CNTL_UPDATE:
        {
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_CLOSE:
        {
            *pkt = vstart;
        }
        break;
    default:
        {
            /* Header is corrupted if control has reached here in prototype */
            /* */
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno,
                    MPI_ERR_OTHER,
                    "**fail",
                    "**fail %s %d",
                    "Control shouldn't reach here "
                    "in prototype, header %d\n",
                    header->type);
        }
    }

    DEBUG_PRINT("Before set credit, vc: %p, v->rail: %d, "
            "pkt: %p, pheader: %p\n", vc, v->rail, pkt, v->pheader);

    SET_CREDIT(header, VC_FIELD(vc, connection), (v->rail));


    if (VC_FIELD(vc, connection)->srp.credits[v->rail].remote_credit > 0 &&
        VC_FIELD(vc, connection)->srp.credits[v->rail].backlog.len > 0) {
        /* backlog send codes */
        MRAILI_Backlog_send(vc, v->rail);
    }

    /* if any credits remain, schedule rendezvous progress */
    if ((VC_FIELD(vc, connection)->srp.credits[v->rail].remote_credit > 0
/* rfp codes */
            || (VC_FIELD(vc, connection)->rfp.ptail_RDMA_send !=
                VC_FIELD(vc, connection)->rfp.phead_RDMA_send)
        )
        && (VC_FIELD(vc, connection)->sreq_head != NULL)) {
        /* rndv codes */
#if 0
        PUSH_FLOWLIST(vc);
#endif
    }

    /* rfp codes */
    if ((VC_FIELD(vc, connection)->rfp.RDMA_recv_buf == NULL) &&       /*(c->initialized) && */
            num_rdma_buffer) {
        if (process_info.polling_group_size <
                rdma_polling_set_limit) {
            VC_FIELD(vc, connection)->rfp.eager_start_cnt++;
            if (rdma_polling_set_threshold <
                    VC_FIELD(vc, connection)->rfp.eager_start_cnt) {
                {
                    vbuf_fast_rdma_alloc(vc, 1);
                    vbuf_address_send(vc);
                    goto fn_exit;
                }
            }
        }
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_nem_ib_fill_request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_nem_ib_fill_request(MPID_Request * req, vbuf * v,
                                  int header_size, int *nb)
{

    MPID_IOV    *iov;
    int         n_iov;
    int         len_avail;
    void        *data_buf;
    int         i;

    len_avail   = v->content_size - header_size;
    iov         = (req == NULL) ? NULL : req->dev.iov;
    n_iov       = (req == NULL) ? 0 : req->dev.iov_count;
    data_buf    = (void *) ((uintptr_t) v->pheader + header_size);

    DEBUG_PRINT 
        ("[recv:fill request] total len %d, head len %d, n iov %d\n",
         v->content_size, header_size, n_iov);


    *nb = 0;
    for (i = req->dev.iov_offset; i < n_iov; i++) {
        if (len_avail >= (int) iov[i].MPID_IOV_LEN
            && iov[i].MPID_IOV_LEN != 0) {
            MPIU_Memcpy(iov[i].MPID_IOV_BUF, data_buf, iov[i].MPID_IOV_LEN);
            data_buf = (void *) ((uintptr_t) data_buf + iov[i].MPID_IOV_LEN);
            len_avail -= iov[i].MPID_IOV_LEN;
            *nb += iov[i].MPID_IOV_LEN;
        } else if (len_avail > 0) {
          MPIU_Memcpy(iov[i].MPID_IOV_BUF, data_buf, len_avail);
            *nb += len_avail;
            break;
        }
    }

    v->content_consumed = header_size + *nb;

    DEBUG_PRINT
        ("[recv:fill request] about to return form request, nb %d\n", *nb);
    return MPI_SUCCESS;
}

void MPIDI_nem_ib_release_vbuf(vbuf * v)
{
    v->eager = 0;
    v->content_size = 0;

    if (v->padding == NORMAL_VBUF_FLAG || v->padding == RPUT_VBUF_FLAG){
        MRAILI_Release_vbuf(v);
    }
    else {
        MRAILI_Release_recv_rdma(v);
        MRAILI_Send_noop_if_needed((MPIDI_VC_t *) v->vc, v->rail);
    }
}

int MPIDI_nem_ib_recv_addr(MPIDI_VC_t * vc, void *vstart)
{
    MPIDI_nem_ib_pkt_address *pkt = vstart;
    int i;
#ifdef _ENABLE_XRC_
    if (USE_XRC && (0 == MPIDI_CH3I_RDMA_Process.xrc_rdmafp ||
            VC_XST_ISSET (vc, XF_CONN_CLOSING)))
        return 1;
#endif
    DEBUG_PRINT("set rdma address, dma address %p\n",
            (void *)pkt->rdma_address);

    if (pkt->rdma_address != 0) {
    /* Allocating the send vbufs for the eager RDMA flow */
    vbuf_fast_rdma_alloc(vc, 0);

    for (i = 0; i < ib_hca_num_hcas; i ++) {
        VC_FIELD(vc, connection)->rfp.RDMA_remote_buf_rkey[i] = pkt->rdma_hndl[i];
    }
    VC_FIELD(vc, connection)->rfp.remote_RDMA_buf = (void *)pkt->rdma_address;
    }

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_NEM_IB_PACKETIZED_RECV_REQ
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_nem_ib_packetized_recv_req(MPIDI_VC_t * vc, MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;
    if (NULL == VC_FIELD(vc, connection)->packetized_recv) {
        VC_FIELD(vc, connection)->packetized_recv = (void *) rreq;
    } else {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER,
                                 "**fail", 0);
    }
    DEBUG_PRINT("Add rreq %p to packetized recv\n", rreq);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_NEM_IB_PACKETIZED_RECV_DATA
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_nem_ib_packetized_recv_data(MPIDI_VC_t * vc, vbuf *v)
{
    int mpi_errno = MPI_SUCCESS;
    int skipsize = sizeof(MPIDI_CH3_Pkt_packetized_send_data_t);
    int nb, complete;
    MPID_Request *rreq = VC_FIELD(vc, connection)->packetized_recv;

    if (NULL == VC_FIELD(vc, connection)->packetized_recv) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER,
                                 "**fail", 0);
        goto fn_exit;
    }

    DEBUG_PRINT("[pkt recv], rreq %p, offset %d, count %d\n", rreq, rreq->dev.iov_offset,
            rreq->dev.iov_count);

    mpi_errno = MPIDI_nem_ib_fill_request(rreq, v, skipsize, &nb);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**fail",
                                 0);
        goto fn_exit;
    }
    skipsize += nb;

    if (MPIDI_nem_ib_request_adjust_iov(rreq, nb)) {
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
            mpi_errno = MPIDI_nem_ib_fill_request(rreq, v, skipsize, &nb);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                 FCNAME, __LINE__,
                                 MPI_ERR_OTHER, "**fail",
                                 0);
                goto fn_exit;
            }
            if (!MPIDI_nem_ib_request_adjust_iov(rreq, nb)) {
                goto fn_exit;
            }
        skipsize += nb;

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
        }
        if (TRUE == complete) {
            VC_FIELD(vc, connection)->packetized_recv = NULL;
        }
    }
  fn_exit:
    return mpi_errno;

}