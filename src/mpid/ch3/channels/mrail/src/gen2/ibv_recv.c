/* Copyright (c) 2002-2010, The Ohio State University. All rights
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

#include "rdma_impl.h"
#include "pmi.h"
#include "mpiutil.h"
#include "cm.h"

#define SET_CREDIT(header, vc, rail) \
{                                                               \
    vc->mrail.rfp.ptail_RDMA_send += header->mrail.rdma_credit; \
    if (vc->mrail.rfp.ptail_RDMA_send >= num_rdma_buffer)       \
        vc->mrail.rfp.ptail_RDMA_send -= num_rdma_buffer;       \
    vc->mrail.srp.credits[rail].remote_cc = header->mrail.remote_credit;\
    vc->mrail.srp.credits[rail].remote_credit += header->mrail.vbuf_credit; \
}

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
#ifdef CRC_CHECK
    unsigned long crc;
#endif
    int mpi_errno = MPI_SUCCESS;
    DEBUG_PRINT("[parse header] vbuf address %p\n", v);
    vstart = v->pheader;
    header = vstart;
    DEBUG_PRINT("[parse header] header type %d\n", header->type);

    /* set it to the header size by default */
    *header_size = MPIDI_CH3_Pkt_size_index[header->type];
#ifdef CRC_CHECK
    crc = update_crc(1, (void *)((uintptr_t)header+sizeof *header),
                     v->content_size - sizeof *header);
    if (crc != header->mrail.crc) {
	int rank; PMI_Get_rank(&rank);
	MPIU_Error_printf(stderr, "CRC mismatch, get %lx, should be %lx "
		"type %d, ocntent size %d\n", 
		crc, header->mrail.crc, header->type, v->content_size);
	MPIU_Assert(0);
    }
#endif
    XRC_MSG ("Recd %d from %d\n", header->type,
            vc->pg_rank);
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
                MPIU_Memcpy((vc->mrail.rfp.cached_incoming), vstart,
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
#ifdef CKPT
    case MPIDI_CH3_PKT_CM_SUSPEND:
    case MPIDI_CH3_PKT_CM_REACTIVATION_DONE:
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
#endif
        {
            *pkt = vstart;
        }
        break;
    case MPIDI_CH3_PKT_ADDRESS:
	{
	    *pkt = vstart;
	    MPIDI_CH3I_MRAILI_Recv_addr(vc, vstart);
	    break;
	}
    case MPIDI_CH3_PKT_CM_ESTABLISH:
        {
            *pkt = vstart;
            *header_size = sizeof(MPIDI_CH3_Pkt_cm_establish_t);
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
    case MPIDI_CH3_PKT_RGET_FINISH:
        {
            *header_size = sizeof(MPIDI_CH3_Pkt_rget_finish_t);
            *pkt = vstart;
            break;
        }
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

    SET_CREDIT((&(((MPIDI_CH3_Pkt_t *) 
                        (*pkt))->eager_send)), vc, (v->rail));


    if (vc->mrail.srp.credits[v->rail].remote_credit > 0 &&
        vc->mrail.srp.credits[v->rail].backlog.len > 0) {
        MRAILI_Backlog_send(vc, v->rail);
    }

    /* if any credits remain, schedule rendezvous progress */
    if ((vc->mrail.srp.credits[v->rail].remote_credit > 0 
            || (vc->mrail.rfp.ptail_RDMA_send != 
                vc->mrail.rfp.phead_RDMA_send)
        )
        && (vc->mrail.sreq_head != NULL)) {
        PUSH_FLOWLIST(vc);
    }

    if ((vc->mrail.rfp.RDMA_recv_buf == NULL) &&       /*(c->initialized) && */
            num_rdma_buffer) {
        if (MPIDI_CH3I_RDMA_Process.polling_group_size <
                rdma_polling_set_limit) {
            vc->mrail.rfp.eager_start_cnt++;
            if (rdma_polling_set_threshold < 
                    vc->mrail.rfp.eager_start_cnt) {
                MPICM_lock();
#ifdef _ENABLE_XRC_
                if (MPIDI_CH3I_RDMA_Process.xrc_rdmafp &&
                        USE_XRC && VC_XST_ISUNSET (vc, XF_SEND_IDLE)) {
                    if (VC_XSTS_ISUNSET (vc, XF_START_RDMAFP | 
                                XF_CONN_CLOSING | XF_DPM_INI)) {
                        XRC_MSG ("Trying to FP to %d st: %d xr: 0x%08x", 
                                vc->pg_rank, vc->ch.state, vc->ch.xrc_flags);
                        VC_XST_SET (vc, XF_START_RDMAFP);
                        MPICM_unlock();
                        MPIDI_CH3I_CM_Connect (vc);
                        goto fn_exit;
                    }
                }
                else if (!USE_XRC || 
                        (MPIDI_CH3I_RDMA_Process.xrc_rdmafp && 
                        VC_XSTS_ISUNSET(vc, 
                            XF_DPM_INI | XF_CONN_CLOSING | XF_START_RDMAFP)
                        && header->type != MPIDI_CH3_PKT_ADDRESS))
#endif
                {
                    XRC_MSG ("FP to %d (IDLE)\n", vc->pg_rank);
                    MPICM_unlock();
                    vbuf_fast_rdma_alloc(vc, 1);
                    vbuf_address_send(vc);
                    goto fn_exit;
                }
                MPICM_unlock();
            }
        }
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;

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

void MPIDI_CH3I_MRAIL_Release_vbuf(vbuf * v)
{
    v->eager = 0;
    v->coalesce = 0;
    v->content_size = 0;

    if (v->padding == NORMAL_VBUF_FLAG || v->padding == RPUT_VBUF_FLAG)
        MRAILI_Release_vbuf(v);
    else {
        MRAILI_Release_recv_rdma(v);
        MRAILI_Send_noop_if_needed((MPIDI_VC_t *) v->vc, v->rail);
    }
}

int MPIDI_CH3I_MRAILI_Recv_addr(MPIDI_VC_t * vc, void *vstart)
{
    MPIDI_CH3_Pkt_address_t *pkt = vstart;
    int i;
#ifdef _ENABLE_XRC_
    if (USE_XRC && (0 == MPIDI_CH3I_RDMA_Process.xrc_rdmafp || 
            VC_XST_ISSET (vc, XF_CONN_CLOSING)))
        return 1;
#endif

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
