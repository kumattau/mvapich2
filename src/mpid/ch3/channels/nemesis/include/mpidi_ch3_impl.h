/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#if !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED)
#define MPICH_MPIDI_CH3_IMPL_H_INCLUDED

#include "mpidi_ch3_conf.h"
#include "mpidimpl.h"

#if defined(HAVE_ASSERT_H)
#include <assert.h>
#endif

extern void *MPIDI_CH3_packet_buffer;
extern int MPIDI_CH3I_my_rank;

#define CH3_NORMAL_QUEUE 0
#define CH3_RNDV_QUEUE   1
#define CH3_NUM_QUEUES   2

extern struct MPID_Request *MPIDI_CH3I_sendq_head[CH3_NUM_QUEUES];
extern struct MPID_Request *MPIDI_CH3I_sendq_tail[CH3_NUM_QUEUES];
extern struct MPID_Request *MPIDI_CH3I_active_send[CH3_NUM_QUEUES];

#define MPIDI_CH3I_SendQ_enqueue(req, queue)					\
{										\
    /* MT - not thread safe! */							\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue req=0x%08x", req->handle));	\
    req->dev.next = NULL;							\
    if (MPIDI_CH3I_sendq_tail[queue] != NULL)					\
    {										\
	MPIDI_CH3I_sendq_tail[queue]->dev.next = req;				\
    }										\
    else									\
    {										\
	MPIDI_CH3I_sendq_head[queue] = req;					\
    }										\
    MPIDI_CH3I_sendq_tail[queue] = req;						\
}

#define MPIDI_CH3I_SendQ_dequeue(queue)						\
{										\
    /* MT - not thread safe! */							\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_dequeue req=0x%08x",			\
                      MPIDI_CH3I_sendq_head[queue]->handle));			\
    MPIDI_CH3I_sendq_head[queue] = MPIDI_CH3I_sendq_head[queue]->dev.next;	\
    if (MPIDI_CH3I_sendq_head[queue] == NULL)					\
    {										\
	MPIDI_CH3I_sendq_tail[queue] = NULL;					\
    }										\
}

#define MPIDI_CH3I_SendQ_head(queue) (MPIDI_CH3I_sendq_head[queue])

#define MPIDI_CH3I_SendQ_empty(queue) (MPIDI_CH3I_sendq_head[queue] == NULL)


int MPIDI_CH3I_Progress_init(void);
int MPIDI_CH3I_Progress_finalize(void);

int MPIDI_CH3I_SendEagerNoncontig( MPIDI_VC_t *vc, MPID_Request *sreq, void *header, MPIDI_msg_sz_t hdr_sz );

int MPID_nem_lmt_shm_initiate_lmt(MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *rts_pkt, MPID_Request *req);
int MPID_nem_lmt_shm_start_recv(MPIDI_VC_t *vc, MPID_Request *req, MPID_IOV s_cookie);
int MPID_nem_lmt_shm_start_send(MPIDI_VC_t *vc, MPID_Request *req, MPID_IOV r_cookie);
int MPID_nem_lmt_shm_handle_cookie(MPIDI_VC_t *vc, MPID_Request *req, MPID_IOV cookie);
int MPID_nem_lmt_shm_done_send(MPIDI_VC_t *vc, MPID_Request *req);
int MPID_nem_lmt_shm_done_recv(MPIDI_VC_t *vc, MPID_Request *req);

int MPID_nem_handle_pkt(MPIDI_VC_t *vc, char *buf, MPIDI_msg_sz_t buflen);

/* #define BYPASS_PROGRESS */

/* short MPIDI_CH3I_Listener_get_port(void); */
/* int MPIDI_CH3I_VC_post_connect(MPIDI_VC_t *); */
/* int MPIDI_CH3I_VC_post_read(MPIDI_VC_t *, MPID_Request *); */
/* int MPIDI_CH3I_VC_post_write(MPIDI_VC_t *, MPID_Request *); */
/* int MPIDI_CH3I_sock_errno_to_mpi_errno(char * fcname, int sock_errno); */
/* int MPIDI_CH3I_Get_business_card(char *value, int length); */

struct MPIDI_VC;
struct MPID_Request;
struct MPID_nem_copy_buf;
union MPIDI_CH3_Pkt;
struct MPID_nem_lmt_shm_wait_element;
struct MPIDI_CH3_PktGeneric;

typedef struct MPIDI_CH3I_VC
{
    int pg_rank;
    struct MPID_Request *recv_active;

    int is_local;
    unsigned short send_seqno;
    MPID_nem_fbox_mpich2_t *fbox_out;
    MPID_nem_fbox_mpich2_t *fbox_in;
    MPID_nem_queue_ptr_t recv_queue;
    MPID_nem_queue_ptr_t free_queue;

    int node_id;

    /* temp buffer to store partially received header */
    MPIDI_msg_sz_t pending_pkt_len;
    struct MPIDI_CH3_PktGeneric *pending_pkt;

    /* can be used by netmods to put this vc on a send queue or list */
    struct MPIDI_VC *next;
    struct MPIDI_VC *prev;

    enum {MPID_NEM_VC_STATE_CONNECTED, MPID_NEM_VC_STATE_DISCONNECTED} state;

    /* contig function pointers.  Netmods should set these. */
    /* iStartContigMsg -- sends a message consisting of a header (hdr) and contiguous data (data), possibly of 0 size.  If the
       message cannot be sent immediately, the function should create a request and return a pointer in sreq_ptr.  The network
       module should complete the request once the message has been completely sent. */
    int (* iStartContigMsg)(struct MPIDI_VC *vc, void *hdr, MPIDI_msg_sz_t hdr_sz, void *data, MPIDI_msg_sz_t data_sz,
                            struct MPID_Request **sreq_ptr);
    /* iSentContig -- sends a message consisting of a header (hdr) and contiguous data (data), possibly of 0 size.  The
       network module should complete the request once the message has been completely sent. */
    int (* iSendContig)(struct MPIDI_VC *vc, struct MPID_Request *sreq, void *hdr, MPIDI_msg_sz_t hdr_sz,
                        void *data, MPIDI_msg_sz_t data_sz);

    /* LMT function pointers */
    int (* lmt_initiate_lmt)(struct MPIDI_VC *vc, union MPIDI_CH3_Pkt *rts_pkt, struct MPID_Request *req);
    int (* lmt_start_recv)(struct MPIDI_VC *vc, struct MPID_Request *req, MPID_IOV s_cookie);
    int (* lmt_start_send)(struct MPIDI_VC *vc, struct MPID_Request *sreq, MPID_IOV r_cookie);
    int (* lmt_handle_cookie)(struct MPIDI_VC *vc, struct MPID_Request *req, MPID_IOV cookie);
    int (* lmt_done_send)(struct MPIDI_VC *vc, struct MPID_Request *req);
    int (* lmt_done_recv)(struct MPIDI_VC *vc, struct MPID_Request *req);

    /* LMT shared memory copy-buffer ptr */
    volatile struct MPID_nem_copy_buf *lmt_copy_buf;
    char *lmt_copy_buf_handle;
    int lmt_buf_num;
    struct {struct MPID_nem_lmt_shm_wait_element *head, *tail;} lmt_queue;
    struct MPID_nem_lmt_shm_wait_element *lmt_active_lmt;
    int lmt_enqueued; /* FIXME: used for debugging */

    struct 
    {
        char padding[MPID_NEM_VC_NETMOD_AREA_LEN];
    } netmod_area;
    

    /* FIXME: ch3 assumes there is a field called sendq_head in the ch
       portion of the vc.  This is unused in nemesis and should be set
       to NULL */
    void *sendq_head;
} MPIDI_CH3I_VC;


#endif /* !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED) */
