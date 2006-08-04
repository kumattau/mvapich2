/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "rdma_impl.h"
#include "pmi.h"
#include "ibv_priv.h"

#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
#define SET_CREDIT(header, vc, rail) \
{                                                               \
    vc->mrail.rfp.ptail_RDMA_send += header->mrail.rdma_credit; \
    if (vc->mrail.rfp.ptail_RDMA_send >= num_rdma_buffer)       \
        vc->mrail.rfp.ptail_RDMA_send -= num_rdma_buffer;       \
    vc->mrail.srp.credits[rail].remote_cc = header->mrail.remote_credit;\
    vc->mrail.srp.credits[rail].remote_credit += header->mrail.vbuf_credit; \
}
#else
#define SET_CREDIT(header, vc, rail) \
{                             \
    vc->mrail.srp.credits[rail].remote_cc = header->mrail.remote_credit;\
    vc->mrail.srp.credits[rail].remote_credit += header->mrail.vbuf_credit; \
}

#endif

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);  fflush(stderr);                   \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

/* FIXME: Ideally the header size should be determined by high level macros,
 * instead of hacking the message header at the device layer */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_Pass_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAIL_Parse_header(MPIDI_VC_t * vc,
                                  vbuf * v, void **pkt, int *header_size)
{
    void *vstart;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *header;

    DEBUG_PRINT("[parse header] vbuf address %p\n", v);
    vstart = v->pheader;
    header = vstart;
    DEBUG_PRINT("[parse header] header type %d\n", header->type);

    switch (header->type) {
#ifdef USE_HEADER_CACHING
    case (MPIDI_CH3_PKT_FAST_EAGER_SEND):
    case (MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ):
        {
            MPIDI_CH3I_MRAILI_Pkt_fast_eager *fast_header = vstart;
            MPIDI_CH3_Pkt_eager_send_t *eager_header =
                (MPIDI_CH3_Pkt_eager_send_t *) vc->mrail.rfp.
                cached_incoming;

            if (MPIDI_CH3_PKT_FAST_EAGER_SEND == header->type) {
                *header_size = sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager);
            } else {
                *header_size =
                    sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req);
                eager_header->sender_req_id =
                    ((MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req *)
                     vstart)->sender_req_id;
            }

            DEBUG_PRINT("[receiver side] cached credit %d\n",
                        eager_header->mrail.rdma_credit);

            eager_header->data_sz = fast_header->bytes_in_pkt;
            eager_header->seqnum = fast_header->seqnum;

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
#ifdef USE_HEADER_CACHING
            if (v->padding != NORMAL_VBUF_FLAG) {
                /* Only cache header if the packet is from RdMA path 
                 * XXXX: what is R3_FLAG? 
                 */
                memcpy((vc->mrail.rfp.cached_incoming), vstart,
                       sizeof(MPIDI_CH3_Pkt_eager_send_t));
            }
#endif
            *pkt = (MPIDI_CH3_Pkt_t *) vstart;
#if 0
            if (v->padding == NORMAL_VBUF_FLAG)
#endif
            *header_size = sizeof(MPIDI_CH3_Pkt_eager_send_t);
            DEBUG_PRINT("[recv: parse header] headersize returned %d\n",
                        *header_size);
        }
        break;
    case (MPIDI_CH3_PKT_RNDV_REQ_TO_SEND):
    case (MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND):
    case (MPIDI_CH3_PKT_RNDV_CLR_TO_SEND):
    case (MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND):
    case (MPIDI_CH3_PKT_RPUT_FINISH):
    case (MPIDI_CH3_PKT_NOOP):
    case MPIDI_CH3_PKT_EAGER_SYNC_ACK:
    case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
    case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
        {
            *pkt = vstart;
        }
        break;
#ifdef ADAPTIVE_RDMA_FAST_PATH
    case MPIDI_CH3_PKT_ADDRESS:
	{
	    *pkt = vstart;
	    MPIDI_CH3I_MRAILI_Recv_addr(vc, vstart);
	    break;
	}
#endif
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
            *header_size = sizeof(MPIDI_CH3_Pkt_send_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_PUT:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_put_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_PUT_RNDV:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_put_rndv_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_GET_RNDV:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_get_rndv_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_GET:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_get_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_GET_RESP:       /*15 */
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_get_resp_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_ACCUMULATE:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_accum_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_accum_rndv_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_lock_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_GRANTED:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_lock_granted_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_PT_RMA_DONE:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_pt_rma_done_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_lock_put_unlock_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_lock_get_unlock_t);
            *pkt = vstart;
            break;
        }
    case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_lock_accum_unlock_t);
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
            *header_size = sizeof(MPIDI_CH3_Pkt_close_t);
            *pkt = vstart;
        }
        break;
    default:
        {
            /* Header is corrupted if control has reached here in prototype */
            /* */
            ibv_error_abort(-1,
                             "Control shouldn't reach here in prototype, header %d\n",
                             header->type);
        }
    }
    SET_CREDIT((&(((MPIDI_CH3_Pkt_t *) (*pkt))->eager_send)), vc,
               (v->rail));

    /* MRAILI_Send_noop_if_needed(vc, &v->subchannel); */
#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
    DEBUG_PRINT(
            "[parse header] after set the credit, remote_cc %d, remote_credit %d, rdma head %d, tail %d\n",
            vc->mrail.srp.credits[v->rail].remote_cc,
            vc->mrail.srp.credits[v->rail].remote_credit,
            vc->mrail.rfp.phead_RDMA_send, vc->mrail.rfp.ptail_RDMA_send);
#endif

    if (vc->mrail.srp.credits[v->rail].remote_credit > 0 &&
        vc->mrail.srp.credits[v->rail].backlog.len > 0) {
        MRAILI_Backlog_send(vc, v->rail);
    }
    /* if any credits remain, schedule rendezvous progress */
    if ((vc->mrail.srp.credits[v->rail].remote_credit > 0 
#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
            || (vc->mrail.rfp.ptail_RDMA_send != vc->mrail.rfp.phead_RDMA_send)
#endif
        )
        && (vc->mrail.sreq_head != NULL)) {
        PUSH_FLOWLIST(vc);
    }

#if defined(ADAPTIVE_RDMA_FAST_PATH)
    if ((vc->mrail.rfp.RDMA_recv_buf == NULL) &&       /*(c->initialized) && */
	num_rdma_buffer) {
	if (MPIDI_CH3I_RDMA_Process.polling_group_size <
	    rdma_polling_set_limit) {
	    vc->mrail.rfp.eager_start_cnt++;
	    if (rdma_polling_set_threshold < vc->mrail.rfp.eager_start_cnt) {
		vbuf_fast_rdma_alloc(vc, 1);
		vbuf_address_send(vc);
	    }
	}
    }
#endif

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_Fill_Request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAIL_Fill_Request(MPID_Request * req, vbuf * v,
                                  int header_size, int *nb)
{
    MPID_IOV    *iov;
    int         n_iov;
    int         len_avail;
    void        *data_buf;
    int         i;

    len_avail 	= v->content_size - header_size;
    iov 	= (req == NULL) ? NULL : req->dev.iov;
    n_iov 	= (req == NULL) ? 0 : req->dev.iov_count;
    data_buf    = (void *) ((uintptr_t) v->pheader + header_size);

    DEBUG_PRINT
        ("[recv:fill request] total len %d, head len %d, n iov %d\n",
         v->content_size, header_size, n_iov);

    *nb = 0;
    for (i = req->ch.iov_offset; i < n_iov; i++) {
        if (len_avail >= (int) iov[i].MPID_IOV_LEN
            && iov[i].MPID_IOV_LEN != 0) {
            memcpy(iov[i].MPID_IOV_BUF, data_buf, iov[i].MPID_IOV_LEN);
            data_buf = (void *) ((uintptr_t) data_buf + iov[i].MPID_IOV_LEN);
            len_avail -= iov[i].MPID_IOV_LEN;
            *nb += iov[i].MPID_IOV_LEN;
        } else if (len_avail > 0) {
            memcpy(iov[i].MPID_IOV_BUF, data_buf, len_avail);
            *nb += len_avail;
            break;
        }
    }

    DEBUG_PRINT
        ("[recv:fill request] about to return form request, nb %d\n", *nb);
    return MPI_SUCCESS;
}

void MPIDI_CH3I_MRAIL_Release_vbuf(vbuf * v)
{
#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
    if (v->padding == NORMAL_VBUF_FLAG || v->padding == RPUT_VBUF_FLAG)
        MRAILI_Release_vbuf(v);
    else {
        MRAILI_Release_recv_rdma(v);
        MRAILI_Send_noop_if_needed((MPIDI_VC_t *) v->vc, v->rail);
    }
#else
    MRAILI_Release_vbuf(v);
#endif
}

#ifdef ADAPTIVE_RDMA_FAST_PATH

int MPIDI_CH3I_MRAILI_Recv_addr(MPIDI_VC_t * vc, void *vstart)
{
    MPIDI_CH3_Pkt_address_t *pkt = vstart;
    int i;

    DEBUG_PRINT("set rdma address, dma address %p\n",
            (void *)pkt->addr.rdma_address);

    if (pkt->addr.rdma_address != 0) {
	/* Allocating the send vbufs for the eager RDMA flow */
	vbuf_fast_rdma_alloc(vc, 0);

	for (i = 0; i < rdma_num_hcas; i ++) {
	    vc->mrail.rfp.RDMA_remote_buf_rkey[i] = pkt->addr.rdma_hndl[i];
	}
	vc->mrail.rfp.remote_RDMA_buf = (void *)pkt->addr.rdma_address;
    }

    return MPI_SUCCESS;
}

#endif
