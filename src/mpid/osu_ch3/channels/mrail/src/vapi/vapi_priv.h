/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#ifndef _VAPI_PRIV_H__
#define _VAPI_PRIV_H__

typedef struct rdma_iba_addr_tb {
    int    **hostid;
    int    **lid;
    int    **qp_num_rdma;
    /* For easy parsing, these are included by default */
    /* TODO: haven't consider one sided queue pair yet */
    int    **qp_num_onesided;
} rdma_iba_addr_tb_t ;


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
    VAPI_POST_RR(c, v, subchannel);                     \
    vc->mrail.srp.local_credit[subchannel.rail_index]++;                          \
    vc->mrail.srp.preposts[subchannel.rail_index]++;                              \
}

#define  VAPI_POST_SR(_v, _c, _channel, err_string) {                 \
    {                                                               \
        VAPI_ret_t ret;                                             \
        if(_v->desc.sg_entry.len <= \
            MPIDI_CH3I_RDMA_Process.inline_size[(_channel).hca_index]) {   \
            ret = EVAPI_post_inline_sr(MPIDI_CH3I_RDMA_Process.nic[(_channel).hca_index],\
                            _c->mrail.qp_hndl[(_channel).rail_index], \
                            &(_v->desc.sr)); \
        } else {                                                    \
            ret =           \
                VAPI_post_sr(MPIDI_CH3I_RDMA_Process.nic[(_channel).hca_index], \
                        _c->mrail.qp_hndl[(_channel).rail_index], &(_v->desc.sr)); \
        }                                                           \
        if(ret != VAPI_OK) {                                        \
            vapi_error_abort(-1, err_string);       \
        }                                                           \
    }                                                               \
}

#define VAPI_POST_RR(c,vbuf,channel) {   \
    int result;   \
    vbuf->vc = (void *)c;                  \
    result = VAPI_post_rr(MPIDI_CH3I_RDMA_Process.nic[channel.hca_index], \
                          c->mrail.qp_hndl[channel.rail_index],          \
                          &(vbuf->desc.rr));           \
    if (result != VAPI_OK) {                           \
        vapi_error_abort(VAPI_RETURN_ERR, "VAPI_post_rr (viadev_post_recv)");    \
    }   \
}        

/*
 * post a descriptor to the send queue
 * all outgoing packets go through this routine.
 * takes a connection rather than a vi because we need to
 * update flow control information on the connection. Also
 * it turns out it is always called in the context of a connection,
 * i.e. it would be post_send(c->vi) otherwise.
 * xxx should be inlined
 */

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
                                                                                                                                               
                                                                                                                                               
/*add later */
#define BACKLOG_DEQUEUE(q,v)  {                     \
    v = q->vbuf_head;                               \
    q->vbuf_head = v->desc.next;                    \
    if (v == q->vbuf_tail) {                        \
        q->vbuf_tail = NULL;                        \
    }                                               \
    q->len--;                                       \
    v->desc.next = NULL;                            \
}

#endif
