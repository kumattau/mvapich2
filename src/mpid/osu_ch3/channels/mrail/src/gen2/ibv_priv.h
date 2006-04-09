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

#ifndef _IBV_PRIV_H__
#define _IBV_PRIV_H__

#include "pmi.h"
#include "infiniband/verbs.h"

#include "rdma_impl.h"

struct MPIDI_CH3I_RDMA_Process_t;

typedef struct rdma_iba_addr_tb {
    int    **hostid;
    uint16_t    **lid;
    uint32_t    **qp_num_rdma;
    /* TODO: haven't consider one sided queue pair yet */
    uint32_t    **qp_num_onesided;
} rdma_iba_addr_tb_t ;

extern struct rdma_iba_addr_tb rdma_iba_addr_table;

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define IBV_RETURN_ERR	  -3    /* gen2 function return error */
#define IBV_STATUS_ERR   -4    /*  gen2 function status error */

#define ibv_error_abort(code, message, args...)  {              \
    int my_rank;                                                    \
    PMI_Get_rank(&my_rank);                                         \
    fprintf(stderr, "[%d] Abort: ", my_rank);    \
    fprintf(stderr, message, ##args);                               \
    fprintf(stderr, " at line %d in file %s\n", __LINE__, __FILE__);\
    exit(code);                                                     \
}

#ifdef RDMA_FAST_PATH
#define PACKET_SET_RDMA_CREDIT(_p, _c) \
{                                                                   \
    (_p)->mrail.rdma_credit = (_c)->mrail.rfp.rdma_credit;                     \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->mrail.vbuf_credit = 0;      \
    (_p)->mrail.remote_credit = 0;    \
}

#define PACKET_SET_CREDIT(_p, _c, _rail_index) \
{                                                                   \
    (_p)->mrail.rdma_credit = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->mrail.vbuf_credit = (_c)->mrail.srp.local_credit[(_rail_index)];      \
    (_p)->mrail.remote_credit = (_c)->mrail.srp.remote_credit[(_rail_index)];   \
    (_c)->mrail.srp.local_credit[(_rail_index)] = 0;         \
}

#else

#define PACKET_SET_CREDIT(_p, _c, _rail_index) \
{                                                                   \
    (_p)->mrail.vbuf_credit = (_c)->mrail.srp.local_credit[(_rail_index)];      \
    (_p)->mrail.remote_credit = (_c)->mrail.srp.remote_credit[(_rail_index)];   \
    (_c)->mrail.srp.local_credit[(_rail_index)] = 0;         \
}

#endif

#define PREPOST_VBUF_RECV(c, subchannel)  {                         \
    vbuf *v = get_vbuf();                                       \
    vbuf_init_recv(v, VBUF_BUFFER_SIZE, &subchannel);        \
    IBV_POST_RR(c, v, subchannel);                     \
    vc->mrail.srp.local_credit[subchannel.rail_index]++;                          \
    vc->mrail.srp.preposts[subchannel.rail_index]++;                              \
}

#define  IBV_POST_SR(_v, _c, _channel, err_string) {                 \
    {                                                               \
        int __ret;            /* We could even use send with immediate */    \
        if(_v->desc.sg_entry.length <= RDMA_MAX_INLINE_SIZE ) {            \
            _v->desc.sr.send_flags = (enum ibv_send_flags)	\
					(IBV_SEND_SIGNALED | IBV_SEND_INLINE);  \
        } else {    \
            _v->desc.sr.send_flags = IBV_SEND_SIGNALED ;    \
        }   \
        __ret = ibv_post_send(_c->mrail.qp_hndl[(_channel).rail_index], \
                            &(_v->desc.sr), &(_v->desc.bad_sr)); \
        if(__ret) {                                        \
            ibv_error_abort(-1, err_string);       \
        }                                                           \
    }                                                               \
}

#define IBV_POST_RR(_c,_vbuf,_channel) {   \
    int __ret;   \
    _vbuf->vc = (void *)_c;                  \
    __ret = ibv_post_recv(_c->mrail.qp_hndl[_channel.rail_index],          \
                          &(_vbuf->desc.rr), &(_vbuf->desc.bad_rr));           \
    if (__ret) {                           \
        ibv_error_abort(IBV_RETURN_ERR, "VAPI_post_rr (viadev_post_recv)");    \
    }   \
}        

struct ibv_mr * register_memory(void *, int len);

int deregister_memory(struct ibv_mr * mr);

#define BACKLOG_ENQUEUE(q,v) {                      \
    v->desc.next = NULL;                            \
    if (q->vbuf_tail == NULL) {                     \
         q->vbuf_head = v;                          \
    } else {                                        \
         q->vbuf_tail->desc.next = v;               \
    }                                               \
    q->vbuf_tail = v;                               \
    q->len++;                                       \
}
                                                                                                                                               
#define BACKLOG_DEQUEUE(q,v)  {                     \
    v = q->vbuf_head;                               \
    q->vbuf_head = v->desc.next;                    \
    if (v == q->vbuf_tail) {                        \
        q->vbuf_tail = NULL;                        \
    }                                               \
    q->len--;                                       \
    v->desc.next = NULL;                            \
}

int MRAILI_Backlog_send(MPIDI_VC_t * vc,
                        const MRAILI_Channel_info * channel);

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  MPIDI_VC_t * vc, int pg_rank, int pg_size);

int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         MPIDI_VC_t * vc, int pg_rank, int pg_size);

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            MPIDI_VC_t * vc, int pg_rank, int pg_size);

int MRAILI_Process_send(void *vbuf_addr);

void MRAILI_RDMA_Put(   MPIDI_VC_t * vc, vbuf *v,
                        char * local_addr, uint32_t lkey,
                        char * remote_addr, uint32_t rkey,
                        int nbytes, MRAILI_Channel_info * subchannel
                    );

#define DEBUG_PRINT(args...)

#define CHECK_UNEXP(ret, s)                           \
do {                                                  \
    if (ret) {                                        \
        fprintf(stderr, "[%s:%d]: %s\n",              \
                __FILE__,__LINE__, s);                \
    exit(1);                                          \
    }                                                 \
} while (0)
                                                                                                                                               
#define CHECK_RETURN(ret, s)                            \
do {                                                    \
    if (ret) {                               \
    fprintf(stderr, "[%s:%d] error(%d): %s\n",          \
        __FILE__,__LINE__, ret, s);                     \
    exit(1);                                            \
    }                                                   \
}                                                       \
while (0)

#undef IN
#undef OUT

double get_us(void);

#define INVAL_HNDL (0xffffffff)


#define IN
#define OUT

#undef MALLOC
#undef FREE

#define MALLOC(a)    malloc((unsigned)(a))
#define CALLOC(a,b)  calloc((unsigned)(a),(unsigned)(b))
#define FREE(a)      free((char *)(a))
#define NEW(a)    (a *)MALLOC(sizeof(a))
#define STRDUP(a)   strdup(a)

#ifdef ONE_SIDED

#define SIGNAL_FOR_PUT        (1)
#define SIGNAL_FOR_GET        (2)
#define SIGNAL_FOR_LOCK_ACT   (3)
#define SIGNAL_FOR_DECR_CC    (4)

#endif
                                                                                                                                               
#endif
