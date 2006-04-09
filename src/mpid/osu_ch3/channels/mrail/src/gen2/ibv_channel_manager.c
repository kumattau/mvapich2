/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "pmi.h"
#include "rdma_impl.h"
#include "ibv_priv.h"

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
    fflush(stderr); \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

static inline int CMANAGER_SOLVE_GLOBAL(MRAILI_Channel_manager *cmanager, 
                                        int global_index)
{
    return (global_index + cmanager->num_local_pollings);
}

static inline void VQUEUE_ENQUEUE(
                                    MRAILI_Channel_manager * cmanager,
                                    int index,
                                    vbuf * v
                                  ) 
{                      
    v->desc.next = NULL;                            
    if (cmanager->v_queue_tail[index] == NULL) { 
         cmanager->v_queue_head[index] = v;      
    } else {
         cmanager->v_queue_tail[index]->desc.next = v;
    }
    cmanager->v_queue_tail[index] = v;
    cmanager->len[index]++;
}

/*add later */
static inline vbuf * VQUEUE_DEQUEUE(MRAILI_Channel_manager *cmanager,
                                    int index)
{
    vbuf * v;
    v = cmanager->v_queue_head[index];
    cmanager->v_queue_head[index] = v->desc.next;
    if (v == cmanager->v_queue_tail[index]) {
        cmanager->v_queue_tail[index] = NULL;
    }
    cmanager->len[index]--;
    v->desc.next = NULL;
    return v;
}

inline int PKT_IS_NOOP(void *v)
{        
    MPIDI_CH3I_MRAILI_Pkt_comm_header * p = ((vbuf *)v)->pheader; 
    return ((p->type == MPIDI_CH3_PKT_NOOP)? 1 : 0);    
}

/*FIXME: Ideally this functionality should be provided by higher levels*/
static inline int GetSeqNumVbuf(vbuf * buf)
{
    if (NULL == buf) return PKT_IS_NULL;
    
    switch(((MPIDI_CH3I_MRAILI_Pkt_comm_header *)buf->pheader)->type) {
    case MPIDI_CH3_PKT_EAGER_SEND:
    case MPIDI_CH3_PKT_READY_SEND:
    case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
    case MPIDI_CH3_PKT_RNDV_REQ_TO_SEND:
    case MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND:
        {
            return ((MPIDI_CH3_Pkt_send_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_RNDV_CLR_TO_SEND:
        {
            return ((MPIDI_CH3_Pkt_rndv_clr_to_send_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
        {
           return ((MPIDI_CH3_Pkt_packetized_send_start_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
        {
            return ((MPIDI_CH3_Pkt_packetized_send_data_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_RNDV_R3_DATA:
        {
            return ((MPIDI_CH3_Pkt_rndv_r3_data_t *)(buf->pheader))->seqnum;
        }
#ifdef USE_HEADER_CACHING
    case MPIDI_CH3_PKT_FAST_EAGER_SEND:
    case MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ:
        {
            return ((MPIDI_CH3I_MRAILI_Pkt_fast_eager *)(buf->pheader))->seqnum;
        }
#endif
    default:
        return PKT_NO_SEQ_NUM;
    }
}

#ifdef RDMA_FAST_PATH
static inline vbuf * MPIDI_CH3I_RDMA_poll(MPIDI_VC_t * vc)
{
    vbuf *v = NULL;
    volatile VBUF_FLAG_TYPE *tail;

    if (num_rdma_buffer == 0)
        return NULL;

    v = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
    tail = &v->head_flag;

    if (*tail && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail) {
        /* advance receive pointer */
        if (++(vc->mrail.rfp.p_RDMA_recv) >= num_rdma_buffer)
            vc->mrail.rfp.p_RDMA_recv = 0;
        MRAILI_FAST_RDMA_VBUF_START(v, *tail, v->pheader)
        DEBUG_PRINT("[recv: poll rdma] recv %d, tail %d, size %d\n",
            vc->pg_rank,
            vc->mrail.rfp.p_RDMA_recv, vc->mrail.rfp.p_RDMA_recv_tail, *tail);
    } else {
        v = NULL;
    }
    return v;
}
#endif

/* Yet this functionality has not been implemented */
int MPIDI_CH3I_MRAILI_Register_channels(MPIDI_VC_t *vc, int num, vbuf *(*func[])(void *))
{
    return MPI_SUCCESS;    
}

int MPIDI_CH3I_MRAILI_Get_next_vbuf_local(MPIDI_VC_t *vc, vbuf ** vbuf_handle)
{
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int i, seq;
    int seq_expected;
    int type;

    seq_expected = vc->seqnum_recv;
    *vbuf_handle = NULL;

#ifdef RDMA_FAST_PATH 
    	/*First loop over all queues to see if there is any pkt already there*/
        for (i = 0; i < cmanager->total_subrails; i ++) {
            seq = GetSeqNumVbuf(cmanager->v_queue_head[i]);
            if (seq == seq_expected) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                vc->seqnum_recv ++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            } else if (seq == PKT_NO_SEQ_NUM) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
            else if (seq != seq_expected && seq != -1) {
            }
        }

        /* no pkt has arrived yet, so we will poll the local channels for the pkt*/
        for (i = 0; i < cmanager->num_local_pollings; i++ ) {
            seq = GetSeqNumVbuf(cmanager->v_queue_head[i]);
            if (seq == seq_expected) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                vc->seqnum_recv ++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            } else if (seq == PKT_NO_SEQ_NUM) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit; 
            }
            else {
                DEBUG_PRINT("channel %d, seq_expected %d\n",i, seq_expected);
                *vbuf_handle =  MPIDI_CH3I_RDMA_poll(vc);
                seq = GetSeqNumVbuf(*vbuf_handle);
                if (seq == seq_expected) {
                    type = T_CHANNEL_EXACT_ARRIVE;
                    vc->seqnum_recv ++;
                    DEBUG_PRINT("[vbuf_local]:find one, now seqnum %d\n", vc->seqnum_recv);
                    goto fn_exit;
                }
                else if( seq == PKT_NO_SEQ_NUM) {
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                    DEBUG_PRINT("[vbuf_local]: get control msg\n");
                    goto fn_exit;
                } else if (*vbuf_handle != NULL){
                    VQUEUE_ENQUEUE(cmanager, i, *vbuf_handle);
                    DEBUG_PRINT("[vbuf_local]: get out of order msg, seq %d, expecting %d\n",
                            seq, seq_expected);
                    *vbuf_handle = NULL;
                }
            } 
        }
#endif
        type = T_CHANNEL_NO_ARRIVE;
        *vbuf_handle = NULL;
#ifdef RDMA_FAST_PATH
  fn_exit:
#endif
    for (i = 0; i < vc->mrail.num_total_subrails; i ++) {
        if (vc->mrail.send_wqes_avail[i] < RDMA_LOW_WQE_THRESHOLD) {
            break;
        }
    }
    if (i != vc->mrail.num_total_subrails) {
        vbuf * vbuffer;
        MPIDI_CH3I_MRAILI_Cq_poll(&vbuffer, vc, 1);
    }

    return type;
}

int MPIDI_CH3I_MRAILI_Waiting_msg(MPIDI_VC_t * vc, vbuf ** vbuf_handle) 
{
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int i, seq;
    int seq_expected = vc->seqnum_recv;
    int type = T_CHANNEL_NO_ARRIVE;
    int is_blocking = 1;
   
    *vbuf_handle = NULL;

    for (i = 0; i < cmanager->total_subrails; i ++) {
        seq = GetSeqNumVbuf(cmanager->v_queue_head[i]);
        if (seq == seq_expected) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
            type = T_CHANNEL_EXACT_ARRIVE;
            vc->seqnum_recv ++;
            goto fn_exit;
        } else if (PKT_NO_SEQ_NUM == seq) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
            goto fn_exit;
        } else if (PKT_IS_NULL == seq) {
        }
    }
    
    /* Obviously the packet with correct sequence hasn't arrived */
    while (1) {
        /* poll local subrails*/
        for (i = 0; i < cmanager->num_local_pollings; i ++) {
            seq = GetSeqNumVbuf(cmanager->v_queue_head[i]);
            if (seq == seq_expected) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                vc->seqnum_recv ++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            } else if (seq == PKT_NO_SEQ_NUM) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
#ifdef RDMA_FAST_PATH
            else {
                *vbuf_handle =  MPIDI_CH3I_RDMA_poll(vc);
                seq = GetSeqNumVbuf(*vbuf_handle);
                if (seq == seq_expected) {
                    type = T_CHANNEL_EXACT_ARRIVE;
                    vc->seqnum_recv ++;
                    goto fn_exit;
                }
                else if( seq == PKT_NO_SEQ_NUM) {
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                    goto fn_exit;
                } else if (*vbuf_handle != NULL){
                    VQUEUE_ENQUEUE(cmanager, i, *vbuf_handle);
                    *vbuf_handle = NULL;
                }
            }
#endif
        }
        
        type = MPIDI_CH3I_MRAILI_Cq_poll(vbuf_handle, vc, 0);
        if (type != T_CHANNEL_NO_ARRIVE) {
            switch(type) {
            case (T_CHANNEL_EXACT_ARRIVE):
                goto fn_exit;
            case (T_CHANNEL_OUT_OF_ORDER_ARRIVE):
                continue;
            case (T_CHANNEL_CONTROL_MSG_ARRIVE):
                goto fn_exit;
            default:
                ibv_error_abort(GEN_ASSERT_ERR, "Unexpected return type\n");
                break;
            }
        } else {
    
        }

        if (is_blocking != 1)
            goto fn_exit;
    } 
fn_exit:
    DEBUG_PRINT("return solve_out_of_order, type %d, next expected %d\n", 
            type, vc->seqnum_recv);
    return type;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_cq_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Cq_poll(vbuf **vbuf_handle, MPIDI_VC_t * vc_req, int receiving)
{
    int ne;
    MPIDI_VC_t *vc;
    struct ibv_wc wc;
    vbuf *v;

    int i,j, needed;
    static int last_poll = 0;
    int type = T_CHANNEL_NO_ARRIVE;

    *vbuf_handle = NULL;
    for (
            i = last_poll, j = 0; 
            j < MPIDI_CH3I_RDMA_Process.num_hcas; 
            i =  ((i + 1) % MPIDI_CH3I_RDMA_Process.num_hcas) , 
            j ++ 
        )
    {
        last_poll = i;
        ne = ibv_poll_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i], 1, &wc);
        if (ne < 0 ) {
            ibv_error_abort(IBV_RETURN_ERR, "Fail to poll cq\n");
        }
        else if (ne) {
            DEBUG_PRINT("[poll cq]: get complete queue entry, ne %d\n", ne);
            v = (vbuf *) ((uintptr_t) wc.wr_id);
            vc = (MPIDI_VC_t *) (v->vc);
            if (wc.status != IBV_WC_SUCCESS) {
                if (wc.opcode == IBV_WC_SEND ||
                    wc.opcode == IBV_WC_RDMA_WRITE ) {
                    fprintf(stderr, "send desc error\n");
                } 
                else fprintf(stderr, "recv desc error, %d\n", wc.opcode);

                ibv_error_abort(IBV_STATUS_ERR,
                    "[] Got completion with error %d, "
                    "vendor code=%x, dest rank=%d\n",
                    wc.status,    
                    wc.vendor_err, 
                    ((MPIDI_VC_t *)v->vc)->pg_rank
                    );
            }
    
            /* get the VC and increase its wqe */
            if (wc.opcode == IBV_WC_SEND ||
                wc.opcode == IBV_WC_RDMA_WRITE ||
                wc.opcode == IBV_WC_RDMA_READ) {
                DEBUG_PRINT("[device_Check] process send\n");
                MRAILI_Process_send(v);
                type = T_CHANNEL_NO_ARRIVE;
                *vbuf_handle = NULL;
            } else if ((NULL == vc_req || vc_req == vc) && 0 == receiving ){
            /* In this case, we should return the vbuf any way if it is next expected*/
                int seqnum = GetSeqNumVbuf(v);

                *vbuf_handle = v; 
                v->head_flag = wc.byte_len;
                vc->mrail.srp.preposts[v->subchannel.rail_index]--;

                needed = rdma_prepost_depth + rdma_prepost_noop_extra
                    + MIN(rdma_prepost_rendezvous_extra,
                          vc->mrail.srp.rendezvous_packets_expected[v->subchannel.rail_index]);

                if (seqnum == PKT_NO_SEQ_NUM){
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                } else if (seqnum == vc->seqnum_recv) {
                    vc->seqnum_recv ++;
                    type = T_CHANNEL_EXACT_ARRIVE;
                    DEBUG_PRINT("[channel manager] get one with exact seqnum\n");
                } else {
                    type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                    VQUEUE_ENQUEUE(&vc->mrail.cmanager, 
                                   CMANAGER_SOLVE_GLOBAL(&vc->mrail.cmanager, i),
                                   v);                    
                    DEBUG_PRINT("get recv %d (%d)\n", seqnum, vc->seqnum_recv);
                }

                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->subchannel);
                    /* noops don't count for credits */
                    vc->mrail.srp.local_credit[v->subchannel.rail_index]--;
                } 
                else if (vc->mrail.srp.preposts[v->subchannel.rail_index] < rdma_rq_size &&
                   vc->mrail.srp.preposts[v->subchannel.rail_index] + rdma_prepost_threshold < needed)
                {
                    do {
                        PREPOST_VBUF_RECV(vc, v->subchannel);
                    } while (vc->mrail.srp.preposts[v->subchannel.rail_index] < rdma_rq_size &&
                             vc->mrail.srp.preposts[v->subchannel.rail_index] < needed);
                }
                
                MRAILI_Send_noop_if_needed(vc, &v->subchannel);
                if (type == T_CHANNEL_CONTROL_MSG_ARRIVE || 
                    type == T_CHANNEL_EXACT_ARRIVE || 
                    type == T_CHANNEL_OUT_OF_ORDER_ARRIVE) {
                    goto fn_exit;
                }
            } else {
            /* Now since this is not the packet we want, we have to enqueue it */
                type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                *vbuf_handle = NULL;
                VQUEUE_ENQUEUE(&vc->mrail.cmanager,
                               CMANAGER_SOLVE_GLOBAL(&vc->mrail.cmanager, i),
                               v);
                vc->mrail.srp.preposts[v->subchannel.rail_index]--;
                                                                                                                                               
                needed = rdma_prepost_depth + rdma_prepost_noop_extra
                    + MIN(rdma_prepost_rendezvous_extra,
                      vc->mrail.srp.rendezvous_packets_expected[v->subchannel.rail_index]);

                v->head_flag = wc.byte_len;
                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->subchannel);
                    vc->mrail.srp.local_credit[v->subchannel.rail_index]--;
                }
                else if (vc->mrail.srp.preposts[v->subchannel.rail_index] < rdma_rq_size &&
                    vc->mrail.srp.preposts[v->subchannel.rail_index] + rdma_prepost_threshold < needed) {
                    do {
                        PREPOST_VBUF_RECV(vc, v->subchannel);
                    } while (vc->mrail.srp.preposts[v->subchannel.rail_index] 
                                < rdma_rq_size && vc->mrail.srp.preposts[v->subchannel.rail_index] < needed);
                }
                MRAILI_Send_noop_if_needed(vc, &v->subchannel);
            }
        } else {
            *vbuf_handle = NULL;
            type = T_CHANNEL_NO_ARRIVE;
        }
    }
fn_exit:
    return type;
}
