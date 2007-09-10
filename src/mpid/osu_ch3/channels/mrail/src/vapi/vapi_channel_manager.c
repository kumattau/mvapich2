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

#include "rdma_impl.h"
#include "vapi_util.h"
#include "vapi_priv.h"

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



static inline void VQUEUE_ENQUEUE( MRAILI_Channel_manager * cmanager,
                                    int index, vbuf * v) 
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


static inline int PKT_IS_NOOP(void *v)
{        
    MPIDI_CH3I_MRAILI_Pkt_comm_header * p = ((vbuf *)v)->pheader; 
    return ((p->type == MPIDI_CH3_PKT_NOOP)? 1 : 0);    
}


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
    case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
        {
            return ((MPIDI_CH3_Pkt_cancel_send_req_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
        {
            return ((MPIDI_CH3_Pkt_cancel_send_resp_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_PUT:
    case MPIDI_CH3_PKT_GET:
    case MPIDI_CH3_PKT_GET_RESP:
    case MPIDI_CH3_PKT_ACCUMULATE:
    case MPIDI_CH3_PKT_LOCK:
    case MPIDI_CH3_PKT_LOCK_GRANTED:
    case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:
    case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:
    case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:
    case MPIDI_CH3_PKT_PT_RMA_DONE:
    case MPIDI_CH3_PKT_PUT_RNDV:
    case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
    case MPIDI_CH3_PKT_GET_RNDV:
        {
            return ((MPIDI_CH3_Pkt_put_t *)(buf->pheader))->seqnum;
        }
    case MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND:
	{
	    return ((MPIDI_CH3_Pkt_rndv_clr_to_send_t *)
		    (buf->pheader))->seqnum;
	}

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
        DEBUG_PRINT("[send: rdma_send] lkey %08x, rkey %08x\n",
        (uint32_t)vc->mrail.rfp.RDMA_send_buf_hndl[0].lkey,
        (uint32_t)vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey);
                                                                                                                                               
        MRAILI_FAST_RDMA_VBUF_START(v, *tail, v->pheader)
        DEBUG_PRINT("[recv: poll rdma] recv index %d, tail %d, received size %d\n",
            vc->mrail.rfp.p_RDMA_recv, vc->mrail.rfp.p_RDMA_recv_tail, *tail);
    } else {
        v = NULL;
    }
    return v;
}
#endif

int MPIDI_CH3I_MRAILI_Register_channels(MPIDI_VC_t *vc, int num, vbuf *(*func[])(void *))
{
    return MPI_SUCCESS;    
}

int MPIDI_CH3I_MRAILI_Get_next_vbuf_local(MPIDI_VC_t *vc, vbuf ** vbuf_handle)
{
    int i, seq;
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int seq_expected = vc->seqnum_recv;
    int type;

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
fn_exit:

    for (i = 0; i < vc->mrail.num_total_subrails; i ++) {
        if (vc->mrail.send_wqes_avail[i] < VAPI_LOW_WQE_THRESHOLD) {
            break;
        }
    }

    if (i != vc->mrail.num_total_subrails) {
        vbuf * vbuffer;
        MPIDI_CH3I_MRAILI_Cq_poll(&vbuffer, vc, 1, 0);
    }

    return type;
}

#ifndef RDMA_FAST_PATH
int MPIDI_CH3I_MRAILI_Get_next_vbuf(MPIDI_VC_t **vc_pptr, vbuf **v_ptr)
{
    *vc_pptr = NULL;
    *v_ptr   = NULL;
     return T_CHANNEL_NO_ARRIVE;
}
#endif

int MPIDI_CH3I_MRAILI_Waiting_msg(MPIDI_VC_t * vc, vbuf ** vbuf_handle, int blocking) 
{
    int i, seq;
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
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
                /* *vbuf_handle = cmanager->poll_channel[i]((void *)vc); */
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
        
        type = MPIDI_CH3I_MRAILI_Cq_poll(vbuf_handle, vc, 0, 0);
        if (type != T_CHANNEL_NO_ARRIVE) {
            switch(type) {
            case (T_CHANNEL_EXACT_ARRIVE):
               goto fn_exit;
            case (T_CHANNEL_OUT_OF_ORDER_ARRIVE):
                continue;
            case (T_CHANNEL_CONTROL_MSG_ARRIVE):
               goto fn_exit;
            default:
                    /* Error here */
                assert(0);
                break;
            }
        } else {
    
        }

        if (is_blocking != 1)
            goto fn_exit;
    } 
fn_exit:
    DEBUG_PRINT("now return from solve out of order, type %d, next expected %d\n", 
            type, vc->seqnum_recv);
    return type;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_cq_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* Behavior of RDMA cq poll:
 *  1, of vc_req is not null, then function return vbuf only on this vc_req
 *  2, else function return vbuf on any incomming channel
 *  3, if vbuf is exactly the next expected vbuf on the channel, increase
 *  vc->seqnum_recv, and vbuf is not enqueued
 *  4, possible return values:
 *      #define T_CHANNEL_NO_ARRIVE 0
 *      #define T_CHANNEL_EXACT_ARRIVE 1
 *      #define T_CHANNEL_OUT_OF_ORDER_ARRIVE 2
 *      #define T_CHANNEL_CONTROL_MSG_ARRIVE 3
 *      #define T_CHANNEL_ERROR -1
 */
int MPIDI_CH3I_MRAILI_Cq_poll(vbuf **vbuf_handle, MPIDI_VC_t * vc_req, 
			      int receiving, int is_blocking)
{
    VAPI_ret_t ret1;
    MPIDI_VC_t *vc;
    VAPI_wc_desc_t sc;
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
        ret1 =
            VAPI_poll_cq(MPIDI_CH3I_RDMA_Process.nic[i],
                     MPIDI_CH3I_RDMA_Process.cq_hndl[i], &sc);
        if (ret1 == VAPI_OK) {
            DEBUG_PRINT("[poll cq]: get complete queue entry\n");
            v = (vbuf *) ((aint_t) sc.id);
            vc = (MPIDI_VC_t *) (v->vc);
            if (sc.status != VAPI_SUCCESS) {
                if (sc.opcode == VAPI_CQE_SQ_SEND_DATA ||
                    sc.opcode == VAPI_CQE_SQ_RDMA_WRITE ||
                    sc.opcode == VAPI_CQE_SQ_RDMA_READ) {
                    fprintf(stderr, "send desc error\n");
                } 
                else fprintf(stderr, "recv desc error, %d\n", sc.opcode);

                vapi_error_abort(VAPI_STATUS_ERR,
                    "[] Got completion with error, "
                    "code=%s, vendor code=%x, dest rank=%d\n",
                    VAPI_wc_status_sym(sc.status),
                    sc.vendor_err_syndrome,
                    ((MPIDI_VC_t *)v->vc)->pg_rank
                    );

            }
    
            /* get the VC and increase its wqe */
            if (sc.opcode == VAPI_CQE_SQ_SEND_DATA ||
                sc.opcode == VAPI_CQE_SQ_RDMA_WRITE ||
                sc.opcode == VAPI_CQE_SQ_RDMA_READ) {
                DEBUG_PRINT("[device_Check] process send\n");
                MRAILI_Process_send(v);

                type = T_CHANNEL_NO_ARRIVE;
                *vbuf_handle = NULL;
            } else if ((NULL == vc_req || vc_req == vc) && 0 == receiving ){
            /* In this case, we should return the vbuf any way if it is next expected*/
                int seqnum = GetSeqNumVbuf(v);

                *vbuf_handle = v; 
                v->head_flag = sc.byte_len;
                vc->mrail.srp.preposts[v->subchannel.rail_index]--;

                needed = vapi_prepost_depth + vapi_prepost_noop_extra
                    + MIN(vapi_prepost_rendezvous_extra,
                          vc->mrail.srp.rendezvous_packets_expected[v->subchannel.rail_index]);

                if (seqnum == PKT_NO_SEQ_NUM){
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                } else if (seqnum == vc->seqnum_recv) {
                    vc->seqnum_recv ++;
                    type = T_CHANNEL_EXACT_ARRIVE;
                    DEBUG_PRINT("Get one with exact seqnum\n");
                } else {
                    type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                    VQUEUE_ENQUEUE(&vc->mrail.cmanager, 
                                   CMANAGER_SOLVE_GLOBAL(&vc->mrail.cmanager, i),
                                   v);                    
                    DEBUG_PRINT("Get out of order recv %d (%d)\n", seqnum, vc->seqnum_recv);
                }

                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->subchannel);
                    /* noops don't count for credits */
                    vc->mrail.srp.local_credit[v->subchannel.rail_index]--;
                } 
                else if (vc->mrail.srp.preposts[v->subchannel.rail_index] < vapi_rq_size &&
                   vc->mrail.srp.preposts[v->subchannel.rail_index] + vapi_prepost_threshold < needed)
                {
                    do {
                        PREPOST_VBUF_RECV(vc, v->subchannel);
                    } while (vc->mrail.srp.preposts[v->subchannel.rail_index] < vapi_rq_size &&
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
                                                                                                                                               
                needed = vapi_prepost_depth + vapi_prepost_noop_extra
                    + MIN(vapi_prepost_rendezvous_extra,
                      vc->mrail.srp.rendezvous_packets_expected[v->subchannel.rail_index]);

                v->head_flag = sc.byte_len;
                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->subchannel);
                    /* noops don't count for credits */
                    vc->mrail.srp.local_credit[v->subchannel.rail_index]--;
                }
                else if (vc->mrail.srp.preposts[v->subchannel.rail_index] < vapi_rq_size &&
                   vc->mrail.srp.preposts[v->subchannel.rail_index] + vapi_prepost_threshold < needed) {
                    do {
                        PREPOST_VBUF_RECV(vc, v->subchannel);
                    } while (vc->mrail.srp.preposts[v->subchannel.rail_index] 
                                < vapi_rq_size && vc->mrail.srp.preposts[v->subchannel.rail_index] < needed);
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
