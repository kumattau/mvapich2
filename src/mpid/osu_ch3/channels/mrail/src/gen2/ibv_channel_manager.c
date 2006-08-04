/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

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

MRAILI_Channel_manager *arriving_head = NULL;
MRAILI_Channel_manager *arriving_tail = NULL;

#define INDEX_GLOBAL(_cmanager,_global_index) \
        (_global_index)

#define INDEX_LOCAL(_cmanager,_local_index) \
        (((_cmanager)->num_channels-(_cmanager)->num_local_pollings)+(_local_index))

static inline void CHANNEL_ENQUEUE(MRAILI_Channel_manager * cmanager)
{
    if (arriving_tail == NULL) {
	arriving_head = arriving_tail = cmanager;
	cmanager->next_arriving = NULL;
    } else {
	arriving_tail->next_arriving = cmanager;
	cmanager->next_arriving      = NULL;
	arriving_tail		     = cmanager;
    }
    cmanager->inqueue = 1;
}

static inline void VQUEUE_ENQUEUE(
        MRAILI_Channel_manager * cmanager,
        int index,
        vbuf * v
        ) 
{                      
    v->desc.next = NULL;                            
    if (cmanager->msg_channels[index].v_queue_tail == NULL) { 
        cmanager->msg_channels[index].v_queue_head = v;      
    } else {
        cmanager->msg_channels[index].v_queue_tail->desc.next = v;
    }
    cmanager->msg_channels[index].v_queue_tail = v;
    cmanager->msg_channels[index].len++;

    if (!cmanager->inqueue)
	CHANNEL_ENQUEUE(cmanager);
}

/*add later */
static inline vbuf * VQUEUE_DEQUEUE(MRAILI_Channel_manager *cmanager,
        int index)
{
    vbuf * v;
    v = cmanager->msg_channels[index].v_queue_head;
    cmanager->msg_channels[index].v_queue_head = v->desc.next;
    if (v == cmanager->msg_channels[index].v_queue_tail) {
        cmanager->msg_channels[index].v_queue_tail = NULL;
    }
    cmanager->msg_channels[index].len--;
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
    if (NULL == buf) {
        return PKT_IS_NULL;
    }
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
        default:
            return PKT_NO_SEQ_NUM;
    }
}

#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
static inline vbuf * MPIDI_CH3I_RDMA_poll(MPIDI_VC_t * vc)
{
    vbuf *v = NULL;
    volatile VBUF_FLAG_TYPE *tail;

    if (num_rdma_buffer == 0)
        return NULL;

    v = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
    tail = v->head_flag;

    if (*tail && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail) {
        /* advance receive pointer */
        if (++(vc->mrail.rfp.p_RDMA_recv) >= num_rdma_buffer)
            vc->mrail.rfp.p_RDMA_recv = 0;
        MRAILI_FAST_RDMA_VBUF_START(v, *tail, v->pheader)
            DEBUG_PRINT("[recv: poll rdma] recv %d, tail %d, size %d\n",
                    vc->pg_rank,
                    vc->mrail.rfp.p_RDMA_recv, vc->mrail.rfp.p_RDMA_recv_tail, *tail);
	v->content_size = *v->head_flag;
    } else {
        v = NULL;
    }
    return v;
}
#endif

static int MPIDI_CH3I_MRAILI_Test_pkt(vbuf **vbuf_handle)
{
    int type = T_CHANNEL_NO_ARRIVE;
    while (arriving_head) {
        type = MPIDI_CH3I_MRAILI_Waiting_msg(arriving_head->vc, vbuf_handle, 0);
        if (type == T_CHANNEL_NO_ARRIVE) {
            arriving_head->inqueue = 0;
            arriving_head = arriving_head->next_arriving;
            if (!arriving_head)
                arriving_tail = NULL;
        } else {
            break;
        }
    }

    return type;
}

/* Yet this functionality has not been implemented */
int MPIDI_CH3I_MRAILI_Register_channels(MPIDI_VC_t *vc, int num, vbuf *(*func[])(void *))
{
    return MPI_SUCCESS;    
}

int MPIDI_CH3I_MRAILI_Get_next_vbuf_local(MPIDI_VC_t *vc, vbuf ** vbuf_handle)
{
#ifdef RDMA_FAST_PATH
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int seq;
#endif
    int i;
    int seq_expected;
    int type;

    seq_expected = vc->seqnum_recv;
    *vbuf_handle = NULL;

#ifdef RDMA_FAST_PATH 
    /*First loop over all queues to see if there is any pkt already there*/
    for (i = 0; i < cmanager->num_channels; i ++) {
        seq = GetSeqNumVbuf(cmanager->msg_channels[i].v_queue_head);
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
        seq = GetSeqNumVbuf(cmanager->msg_channels[INDEX_LOCAL(cmanager,i)].v_queue_head);
        if (seq == seq_expected) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
            vc->seqnum_recv ++;
            type = T_CHANNEL_EXACT_ARRIVE;
            goto fn_exit;
        } else if (seq == PKT_NO_SEQ_NUM) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
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
                VQUEUE_ENQUEUE(cmanager, INDEX_LOCAL(cmanager,i), *vbuf_handle);
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
    for (i = 0; i < vc->mrail.num_rails; i ++) {
        if (vc->mrail.rails[i].send_wqes_avail < RDMA_LOW_WQE_THRESHOLD) {
            break;
        }
    }
    if (i != vc->mrail.num_rails) {
        vbuf * vbuffer;
        MPIDI_CH3I_MRAILI_Cq_poll(&vbuffer, vc, 1);
    }

    return type;
}

#ifdef ADAPTIVE_RDMA_FAST_PATH
int MPIDI_CH3I_MRAILI_Get_next_vbuf(MPIDI_VC_t ** vc_ptr, vbuf ** vbuf_ptr)
{
    MPIDI_VC_t *vc;
    int type = T_CHANNEL_NO_ARRIVE;
    int i;
    int seq;
    vbuf *v;
    volatile VBUF_FLAG_TYPE *tail;

    v		= NULL;
    *vc_ptr 	= NULL;
    *vbuf_ptr 	= NULL;

    type = MPIDI_CH3I_MRAILI_Test_pkt(vbuf_ptr);
    if (type == T_CHANNEL_EXACT_ARRIVE || type == T_CHANNEL_CONTROL_MSG_ARRIVE) {
	*vc_ptr = (*vbuf_ptr)->vc;
	goto fn_exit;
    } else if (type == T_CHANNEL_OUT_OF_ORDER_ARRIVE) {
	type = T_CHANNEL_NO_ARRIVE;
	*vbuf_ptr = NULL;
    }

    if (num_rdma_buffer == 0)
	goto fn_exit;

    /* no msg is queued, poll rdma polling set */
    for (i = 0; i < MPIDI_CH3I_RDMA_Process.polling_group_size; i++) {
	vc   = MPIDI_CH3I_RDMA_Process.polling_set[i];
	seq  = GetSeqNumVbuf(vc->mrail.cmanager.msg_channels[INDEX_LOCAL(&vc->mrail.cmanager,0)].v_queue_head);
	if (seq == PKT_IS_NULL) {
	    v    = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
	    tail = v->head_flag;

	    if (*tail && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail) {
		DEBUG_PRINT("Get one!!!!!!!!!!!!!!\n");
		if (++(vc->mrail.rfp.p_RDMA_recv) >= num_rdma_buffer)
		    vc->mrail.rfp.p_RDMA_recv = 0;
		MRAILI_FAST_RDMA_VBUF_START(v, *tail, v->pheader)
		v->content_size = *v->head_flag;

		seq = GetSeqNumVbuf(v);
		if (seq == vc->seqnum_recv) {
		   DEBUG_PRINT("Get one exact seq: %d\n", seq);
		    type = T_CHANNEL_EXACT_ARRIVE;
		    vc->seqnum_recv ++;
		    *vbuf_ptr = v;
		    *vc_ptr   = v->vc;
		    goto fn_exit;
		} else if( seq == PKT_NO_SEQ_NUM) {
		    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
		    DEBUG_PRINT("[vbuf_local]: get control msg\n");
		    *vbuf_ptr = v;
		    *vc_ptr   = v->vc;
                    goto fn_exit;
		} else {
		   DEBUG_PRINT("Get one out of order seq: %d, expecting %d\n", 
				seq, vc->seqnum_recv);
                    VQUEUE_ENQUEUE(&vc->mrail.cmanager,
			INDEX_LOCAL(&vc->mrail.cmanager,0), v);
		    continue;
                }
	    } else
		continue;
	} 

	if (seq == vc->seqnum_recv) {
            *vbuf_ptr = VQUEUE_DEQUEUE(&vc->mrail.cmanager, INDEX_LOCAL(&vc->mrail.cmanager,0));
	    *vc_ptr   = (*vbuf_ptr)->vc;
            vc->seqnum_recv ++;
            type = T_CHANNEL_EXACT_ARRIVE;
            goto fn_exit;
        } else if (seq == PKT_NO_SEQ_NUM) {
            *vbuf_ptr = VQUEUE_DEQUEUE(&vc->mrail.cmanager, INDEX_LOCAL(&vc->mrail.cmanager,0));
	    *vc_ptr   = (*vbuf_ptr)->vc;
            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
            goto fn_exit;
        }
    }
  fn_exit:
    return type;
}

#endif

int MPIDI_CH3I_MRAILI_Waiting_msg(MPIDI_VC_t * vc, vbuf ** vbuf_handle, int blocking) 
{
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int i, seq;
    int seq_expected = vc->seqnum_recv;
    int type = T_CHANNEL_NO_ARRIVE;

    *vbuf_handle = NULL;

    if (blocking) {
	DEBUG_PRINT("{entering} solve_out_of_order next expected %d, channel %d, head %p (%d)\n", 
            vc->seqnum_recv, cmanager->num_channels, 
	    cmanager->msg_channels[0].v_queue_head,
	    GetSeqNumVbuf(cmanager->msg_channels[0].v_queue_head));
    }

    for (i = 0; i < cmanager->num_channels; i ++) {
        seq = GetSeqNumVbuf(cmanager->msg_channels[i].v_queue_head);
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
            /* Do nothing */
        } else {
            *vbuf_handle = cmanager->msg_channels[i].v_queue_head;
            type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
        }
    }

    /* Obviously the packet with correct sequence hasn't arrived */
    while (blocking) {
        /* poll local subrails*/
        for (i = 0; i < cmanager->num_local_pollings; i ++) {
            seq = GetSeqNumVbuf(cmanager->msg_channels[INDEX_LOCAL(cmanager,i)].v_queue_head);
            if (seq == seq_expected) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
                vc->seqnum_recv ++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            } else if (seq == PKT_NO_SEQ_NUM) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
#if defined (RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
#if defined (RDMA_FAST_PATH)
            else {
#elif defined(ADAPTIVE_RDMA_FAST_PATH)
	    else if (vc->mrail.rfp.in_polling_set) {
#endif
                *vbuf_handle = MPIDI_CH3I_RDMA_poll(vc);
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
                    VQUEUE_ENQUEUE(cmanager, INDEX_LOCAL(cmanager,i), *vbuf_handle);
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
    } 
fn_exit:
    if (blocking) {
	DEBUG_PRINT("{return} solve_out_of_order, type %d, next expected %d\n", 
            type, vc->seqnum_recv);
    }
    return type;
}

#ifdef DEBUG
unsigned long debug = 0;
MPIDI_VC_t *debug_vc;
#define LONG_WAIT (4000000)
#endif

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

    int i,j, needed, is_completion;
    static int last_poll = 0;
    int type = T_CHANNEL_NO_ARRIVE;

    *vbuf_handle = NULL;

    if (!receiving) {
	type = MPIDI_CH3I_MRAILI_Test_pkt(vbuf_handle);
	if (type == T_CHANNEL_EXACT_ARRIVE || type == T_CHANNEL_CONTROL_MSG_ARRIVE)
	    goto fn_exit;
    }

    for (
            i = last_poll, j = 0; 
            j < rdma_num_hcas; 
            i =  ((i + 1) % rdma_num_hcas) , 
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

            is_completion = wc.opcode == IBV_WC_SEND
                || wc.opcode == IBV_WC_RDMA_WRITE
                || wc.opcode == IBV_WC_RDMA_READ;
            
#ifdef SRQ
            if(!is_completion) {
                int src_rank = ((MPIDI_CH3I_MRAILI_Pkt_comm_header *)((vbuf *)v)->pheader)->mrail.src_rank;
                DEBUG_PRINT("src rank is %d\n", src_rank);
                vc = (MPIDI_VC_t *) MPIDI_CH3I_RDMA_Process.vc_mapping[src_rank];
                v->vc = vc;
		v->rail = ((MPIDI_CH3I_MRAILI_Pkt_comm_header *)
			  ((vbuf*)v)->pheader)->mrail.rail;
            } 
#endif
            /* get the VC and increase its wqe */
            if (is_completion) {
                DEBUG_PRINT("[device_Check] process send, v %p\n", v);
                MRAILI_Process_send(v);
                type = T_CHANNEL_NO_ARRIVE;
                *vbuf_handle = NULL;
            } else if ((NULL == vc_req || vc_req == vc) && 0 == receiving ){
                /* In this case, we should return the vbuf any way if it is next expected*/
                int seqnum = GetSeqNumVbuf(v);

                *vbuf_handle = v; 
                v->content_size = wc.byte_len;
#ifdef SRQ
                pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

#ifdef ADAPTIVE_RDMA_FAST_PATH
                if(v->padding == NORMAL_VBUF_FLAG) {
                    /* Can only be from SRQ path */
                    MPIDI_CH3I_RDMA_Process.posted_bufs[i]--;
                }
#else
                /* Cannot be any other type of vbuf
                 *      * than SRQ */
                MPIDI_CH3I_RDMA_Process.posted_bufs[i]--;
#endif

                if(MPIDI_CH3I_RDMA_Process.posted_bufs[i] <= rdma_credit_preserve) {

                    /* Need to post more to the SRQ */
                    MPIDI_CH3I_RDMA_Process.posted_bufs[i] +=
                        viadev_post_srq_buffers(viadev_srq_size - 
                                MPIDI_CH3I_RDMA_Process.posted_bufs[i], i);

                }

                pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

#else
               
                vc->mrail.srp.credits[v->rail].preposts--;

                needed = rdma_prepost_depth + rdma_prepost_noop_extra
                    + MIN(rdma_prepost_rendezvous_extra,
                            vc->mrail.srp.credits[v->rail].rendezvous_packets_expected);
#endif

                if (seqnum == PKT_NO_SEQ_NUM){
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                } else if (seqnum == vc->seqnum_recv) {
                    vc->seqnum_recv ++;
                    type = T_CHANNEL_EXACT_ARRIVE;
                    DEBUG_PRINT("[channel manager] get one with exact seqnum\n");
                } else {
                    type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                    VQUEUE_ENQUEUE(&vc->mrail.cmanager, 
                            INDEX_GLOBAL(&vc->mrail.cmanager, v->rail),
                            v);                    
                    DEBUG_PRINT("get recv %d (%d)\n", seqnum, vc->seqnum_recv);
                }

#ifndef SRQ
                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->rail);
                    /* noops don't count for credits */
                    vc->mrail.srp.credits[v->rail].local_credit--;
                } 
                else if (vc->mrail.srp.credits[v->rail].preposts < rdma_rq_size &&
                        vc->mrail.srp.credits[v->rail].preposts + rdma_prepost_threshold < needed)
                {
                    do {
                        PREPOST_VBUF_RECV(vc, v->rail);
                    } while (vc->mrail.srp.credits[v->rail].preposts < rdma_rq_size &&
                            vc->mrail.srp.credits[v->rail].preposts < needed);
                }

                MRAILI_Send_noop_if_needed(vc, v->rail);
#endif
                if (type == T_CHANNEL_CONTROL_MSG_ARRIVE || 
                        type == T_CHANNEL_EXACT_ARRIVE || 
                        type == T_CHANNEL_OUT_OF_ORDER_ARRIVE) {
                    goto fn_exit;
                }
            } else {
                /* Now since this is not the packet we want, we have to enqueue it */
                type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                *vbuf_handle = NULL;
                v->content_size = wc.byte_len;
                VQUEUE_ENQUEUE(&vc->mrail.cmanager,
                        INDEX_GLOBAL(&vc->mrail.cmanager, v->rail),
                        v);
#ifdef SRQ
                pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

#ifdef ADAPTIVE_RDMA_FAST_PATH
                if(v->padding == NORMAL_VBUF_FLAG) {
                    /* Can only be from SRQ path */
                    MPIDI_CH3I_RDMA_Process.posted_bufs[i]--;
                }
#else
                /* Cannot be any other type of vbuf
 *                  *      * than SRQ */
                MPIDI_CH3I_RDMA_Process.posted_bufs[i]--;
#endif

                if(MPIDI_CH3I_RDMA_Process.posted_bufs[i] <= rdma_credit_preserve) {

                    /* Need to post more to the SRQ */
                    MPIDI_CH3I_RDMA_Process.posted_bufs[i] +=
                        viadev_post_srq_buffers(viadev_srq_size - 
                                MPIDI_CH3I_RDMA_Process.posted_bufs[i], i);

                }

                pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

#else

                vc->mrail.srp.credits[v->rail].preposts--;

                needed = rdma_prepost_depth + rdma_prepost_noop_extra
                    + MIN(rdma_prepost_rendezvous_extra,
                            vc->mrail.srp.credits[v->rail].rendezvous_packets_expected);

                if (PKT_IS_NOOP(v)) {
                    PREPOST_VBUF_RECV(vc, v->rail);
                    vc->mrail.srp.credits[v->rail].local_credit--;
                }
                else if (vc->mrail.srp.credits[v->rail].preposts < rdma_rq_size &&
                        vc->mrail.srp.credits[v->rail].preposts + rdma_prepost_threshold < needed) {
                    do {
                        PREPOST_VBUF_RECV(vc, v->rail);
                    } while (vc->mrail.srp.credits[v->rail].preposts 
                            < rdma_rq_size && vc->mrail.srp.credits[v->rail].preposts < needed);
                }
                MRAILI_Send_noop_if_needed(vc, v->rail);
#endif
            }
        } else {
            *vbuf_handle = NULL;
            type = T_CHANNEL_NO_ARRIVE;
        }
    }
fn_exit:
#ifdef DEBUG
    if (type == T_CHANNEL_NO_ARRIVE) {
	int rank;
	debug ++;
	PMI_Get_rank(&rank);
	if (debug % LONG_WAIT == 0) {
		fprintf(stderr, "[%d] waiting %lu rounds but get nothing, arriving head %p\n", rank, debug, arriving_head);
		fprintf(stderr, "   vc is %p, expecting %d, channels %d, queue heads <%p><%p>\n",
			debug_vc, debug_vc->seqnum_recv, 
			debug_vc->mrail.cmanager.num_channels,
			debug_vc->mrail.cmanager.msg_channels[0].v_queue_head,
			debug_vc->mrail.cmanager.msg_channels[1].v_queue_head);
		fprintf(stderr, "   backlog length %d, wqe <%d><%d>, ext queue heads <%p><%p>\n",
			debug_vc->mrail.srp.credits[0].backlog.len,
			debug_vc->mrail.rails[0].send_wqes_avail,
			debug_vc->mrail.rails[1].send_wqes_avail,
			debug_vc->mrail.rails[0].ext_sendq_head,
			debug_vc->mrail.rails[1].ext_sendq_head
			);
	}
    } else {
	debug = 0;
	debug_vc = vc;
    }
#endif
    return type;
}

#ifdef SRQ

void async_thread(void *context)
{
    struct ibv_async_event event;
    struct ibv_srq_attr srq_attr;
    int post_new, i, hca_num;
    
    while (1) {
        if (ibv_get_async_event((struct ibv_context *) context, &event)) {
            fprintf(stderr, "Error getting event!\n"); 
        }

        switch (event.event_type) {
            /* Fatal */
        case IBV_EVENT_CQ_ERR:
        case IBV_EVENT_QP_FATAL:
        case IBV_EVENT_QP_REQ_ERR:
        case IBV_EVENT_QP_ACCESS_ERR:
        case IBV_EVENT_PATH_MIG:
        case IBV_EVENT_PATH_MIG_ERR:
        case IBV_EVENT_DEVICE_FATAL:
        case IBV_EVENT_SRQ_ERR:
            ibv_error_abort(GEN_EXIT_ERR, "Got FATAL event %d\n",
                            event.event_type);
            break;

        case IBV_EVENT_COMM_EST:
        case IBV_EVENT_PORT_ACTIVE:
        case IBV_EVENT_SQ_DRAINED:
        case IBV_EVENT_PORT_ERR:
        case IBV_EVENT_LID_CHANGE:
        case IBV_EVENT_PKEY_CHANGE:
        case IBV_EVENT_SM_CHANGE:
        case IBV_EVENT_QP_LAST_WQE_REACHED:
            break;

        case IBV_EVENT_SRQ_LIMIT_REACHED:

            pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

            for(i = 0; i < rdma_num_hcas; i++) {
                if(MPIDI_CH3I_RDMA_Process.nic_context[i] == context) {
                    hca_num = i;
                }
            }
            /* Need to post more to the SRQ */
            post_new = MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num];

            MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] +=
                viadev_post_srq_buffers(viadev_srq_size -
                                        viadev_srq_limit, hca_num);

            post_new = MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] - post_new;

            srq_attr.max_wr = viadev_srq_size;
            srq_attr.max_sge = 1;
            srq_attr.srq_limit = viadev_srq_limit;

            if (ibv_modify_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                        &srq_attr, IBV_SRQ_LIMIT)) {
                ibv_error_abort(GEN_EXIT_ERR,
                                "Couldn't modify SRQ limit (%u) after posting %d\n",
                                viadev_srq_limit, post_new);
            }

            pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

            break;
        default:
            fprintf(stderr,
                    "Got unknown event %d ... continuing ...\n",
                    event.event_type);
        }

        ibv_ack_async_event(&event);
    }
}
#endif
