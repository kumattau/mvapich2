/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#if !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED)
#define MPICH_MPIDI_CH3_IMPL_H_INCLUDED

#include "mpidi_ch3i_sock_conf.h"
#include "mpidi_ch3_conf.h"
#include "mpidimpl.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

/* This is all socket connection definitions */

enum MPIDI_CH3I_Conn_state
{
    CONN_STATE_UNCONNECTED,
    CONN_STATE_LISTENING,
    CONN_STATE_CONNECTING,
    CONN_STATE_CONNECT_ACCEPT, 
    CONN_STATE_OPEN_CSEND,
    CONN_STATE_OPEN_CRECV,
    CONN_STATE_OPEN_LRECV_PKT,
    CONN_STATE_OPEN_LRECV_DATA,
    CONN_STATE_OPEN_LSEND,
    CONN_STATE_CONNECTED,
    CONN_STATE_CLOSING,
    CONN_STATE_CLOSED,
    CONN_STATE_FAILED
};

typedef struct MPIDI_CH3I_Connection
{
    MPIDI_VC_t * vc;
    MPIDU_Sock_t sock;
    enum MPIDI_CH3I_Conn_state state;
    MPID_Request * send_active;
    MPID_Request * recv_active;
    MPIDI_CH3_Pkt_t pkt;
    char * pg_id;
    MPID_IOV iov[2];
} MPIDI_CH3I_Connection_t;

    /* MT - not thread safe! */
#define MPIDI_CH3I_SendQ_enqueue(vc, req)				\
{									\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue vc=%p req=0x%08x", vc, req->handle));  \
    req->dev.next = NULL;						\
    if (vc->ch.sendq_tail != NULL)					\
    {									\
	vc->ch.sendq_tail->dev.next = req;				\
    }									\
    else								\
    {									\
	vc->ch.sendq_head = req;					\
    }									\
    vc->ch.sendq_tail = req;						\
}

    /* MT - not thread safe! */
#define MPIDI_CH3I_SendQ_enqueue_head(vc, req)				\
{									\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue_head vc=%p req=0x%08x", vc, req->handle));\
    req->dev.next = vc->ch.sendq_head;					\
    if (vc->ch.sendq_tail == NULL)					\
    {									\
	vc->ch.sendq_tail = req;					\
    }									\
    vc->ch.sendq_head = req;						\
}

    /* MT - not thread safe! */
#define MPIDI_CH3I_SendQ_dequeue(vc)					\
{									\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_dequeue vc=%p req=0x%08x", vc, vc->ch.sendq_head->handle));\
    vc->ch.sendq_head = vc->ch.sendq_head->dev.next;			\
    if (vc->ch.sendq_head == NULL)					\
    {									\
	vc->ch.sendq_tail = NULL;					\
    }									\
}


#define MPIDI_CH3I_SendQ_head(vc) (vc->ch.sendq_head)

#define MPIDI_CH3I_SendQ_empty(vc) (vc->ch.sendq_head == NULL)

/* End of connection-related macros */

/* FIXME: Any of these used in the ch3->channel interface should be
   defined in a header file in ch3/include that defines the 
   channel interface */
int MPIDI_CH3I_Progress_init(void);
int MPIDI_CH3I_Progress_finalize(void);
int MPIDI_CH3I_VC_post_connect(MPIDI_VC_t *);
int MPIDI_CH3I_Initialize_tmp_comm(MPID_Comm **comm_pptr, MPIDI_VC_t *vc_ptr, 
				   int is_low_group);

#endif /* !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED) */
