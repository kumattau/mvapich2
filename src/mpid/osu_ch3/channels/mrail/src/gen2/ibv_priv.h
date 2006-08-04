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

#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
#define PACKET_SET_RDMA_CREDIT(_p, _c) \
{                                                                   \
    (_p)->mrail.rdma_credit 	= (_c)->mrail.rfp.rdma_credit;                     \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->mrail.vbuf_credit 	= 0;      \
    (_p)->mrail.remote_credit 	= 0;    \
}

#define PACKET_SET_CREDIT(_p, _c, _rail_index) \
{                                                                   \
    (_p)->mrail.rdma_credit	= (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->mrail.vbuf_credit 	= (_c)->mrail.srp.credits[(_rail_index)].local_credit;      \
    (_p)->mrail.remote_credit 	= (_c)->mrail.srp.credits[(_rail_index)].remote_credit;   \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit = 0;      \
}

#else

#define PACKET_SET_CREDIT(_p, _c, _rail_index) \
{                                                                   \
    (_p)->mrail.vbuf_credit 	= (_c)->mrail.srp.credits[(_rail_index)].local_credit; \
    (_p)->mrail.remote_credit 	= (_c)->mrail.srp.credits[(_rail_index)].remote_credit; \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit = 0;         \
}

#endif

#define PREPOST_VBUF_RECV(_c, _subrail)  {                         \
    vbuf *__v = get_vbuf();                                       \
    vbuf_init_recv(__v, VBUF_BUFFER_SIZE, _subrail);        \
    IBV_POST_RR(_c, __v, (_subrail));                     \
    (_c)->mrail.srp.credits[(_subrail)].local_credit++;                          \
    (_c)->mrail.srp.credits[(_subrail)].preposts++;                              \
}

/* Disabling the inline for IBM EHCA */
#ifdef _IBM_EHCA_

#define  IBV_POST_SR(_v, _c, _rail, err_string) {                 \
    {                                                               \
        int __ret;            /* We could even use send with immediate */    \
        (_v)->desc.sr.send_flags = IBV_SEND_SIGNALED ;    \
    if ((_rail) != (_v)->rail) { \
        fprintf(stderr, "[%s:%d] rail %d, vrail %d\n", \
            __FILE__, __LINE__, (_rail), (_v)->rail); \
        assert((_rail) == (_v)->rail); \
    } \
        __ret = ibv_post_send((_c)->mrail.rails[(_rail)].qp_hndl, &((_v)->desc.sr),&((_v)->desc.bad_sr)); \
        if(__ret) {                                        \
        fprintf(stderr, "failed while avail wqe is %d, rail %d\n",  \
        (_c)->mrail.rails[(_rail)].send_wqes_avail, (_rail)); \
            ibv_error_abort(-1, err_string);       \
        }                                                           \
    }                                                               \
}

#else

#define  IBV_POST_SR(_v, _c, _rail, err_string) {                 \
    {                                                               \
        int __ret;            /* We could even use send with immediate */    \
        if((_v)->desc.sg_entry.length <= RDMA_MAX_INLINE_SIZE ) {            \
           (_v)->desc.sr.send_flags = (enum ibv_send_flags)	\
					(IBV_SEND_SIGNALED | IBV_SEND_INLINE);  \
        } else {    \
            (_v)->desc.sr.send_flags = IBV_SEND_SIGNALED ;    \
        }   \
	if ((_rail) != (_v)->rail) { \
		fprintf(stderr, "[%s:%d] rail %d, vrail %d\n", \
			__FILE__, __LINE__, (_rail), (_v)->rail); \
		assert((_rail) == (_v)->rail); \
	} \
        __ret = ibv_post_send((_c)->mrail.rails[(_rail)].qp_hndl, &((_v)->desc.sr),&((_v)->desc.bad_sr)); \
        if(__ret) {                                        \
	    fprintf(stderr, "failed while avail wqe is %d, rail %d\n",  \
		(_c)->mrail.rails[(_rail)].send_wqes_avail, (_rail)); \
            ibv_error_abort(-1, err_string);       \
        }                                                           \
    }                                                               \
}

#endif


#define IBV_POST_RR(_c,_vbuf,_rail) {   \
    int __ret;   \
    _vbuf->vc = (void *)_c;          \
    __ret = ibv_post_recv(_c->mrail.rails[(_rail)].qp_hndl,          \
                          &((_vbuf)->desc.rr), &((_vbuf)->desc.bad_rr));          \
    if (__ret) {                           \
        ibv_error_abort(IBV_RETURN_ERR, "VAPI_post_rr (viadev_post_recv) with %d", __ret);    \
    }   \
}        

struct ibv_mr * register_memory(void *, int len, int hca_num);

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

int MRAILI_Backlog_send(MPIDI_VC_t * vc, int subrail);

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  int pg_rank, int pg_size);

int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         int pg_rank, int pg_size);

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            int pg_rank, int pg_size);

int MRAILI_Process_send(void *vbuf_addr);

void MRAILI_RDMA_Put(   MPIDI_VC_t * vc, vbuf *v,
                        char * local_addr, uint32_t lkey,
                        char * remote_addr, uint32_t rkey,
                        int nbytes, int subrail
                    );

#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
int MRAILI_Fast_rdma_select_rail(MPIDI_VC_t * vc);
#endif
int MRAILI_Send_select_rail(MPIDI_VC_t * vc);

#if defined(ADAPTIVE_RDMA_FAST_PATH)
void vbuf_address_send(MPIDI_VC_t *vc);
void vbuf_fast_rdma_alloc (struct MPIDI_VC *, int dir);
#endif

int MPIDI_CH3I_MRAILI_rput_complete(MPIDI_VC_t *, MPID_IOV *,
                                 int, int *num_bytes_ptr, vbuf **, int rail);

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
