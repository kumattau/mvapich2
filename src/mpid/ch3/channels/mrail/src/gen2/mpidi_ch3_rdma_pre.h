/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

#ifndef MPIDI_CH3_RDMA_PRE_H
#define MPIDI_CH3_RDMA_PRE_H

#include "mpidi_ch3i_rdma_conf.h"
#include "vbuf.h"
#include "mpiimpl.h"
#include "ibv_param.h"

/* this device supports the coalescing interface */
#define MPIDI_MRAILI_COALESCE_ENABLED 1

#define MPIDI_CH3I_MRAILI_FLUSH 1

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;
#define MPIDI_CH3I_MRAILI_IBA_PKT_DEFS 1

#ifdef CRC_CHECK
#define MPIDI_CH3I_MRAILI_IBA_PKT_DECL \
    uint8_t  vbuf_credit;       \
    uint8_t  remote_credit;     \
    uint8_t  rdma_credit;       \
    uint8_t  rail;              \
    union {                     \
        uint32_t smp_index;     \
        uint64_t vc_addr;       \
    } src;                      \
    unsigned long crc;
#else
#define MPIDI_CH3I_MRAILI_IBA_PKT_DECL \
    uint8_t  vbuf_credit;       \
    uint8_t  remote_credit;     \
    uint8_t  rdma_credit;       \
    uint8_t  rail;              \
    union {                     \
        uint32_t smp_index;     \
        uint64_t vc_addr;       \
    } src;                      
#endif

typedef enum {
    VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED = 0,
    VAPI_PROTOCOL_EAGER,
    VAPI_PROTOCOL_R3,
    VAPI_PROTOCOL_RPUT,
    VAPI_PROTOCOL_RGET,
} MRAILI_Protocol_t;

typedef struct MPIDI_CH3I_MRAILI_Rndv_info {
    /* Buffer Address */
    void                *buf_addr;
    /* rkey for RDMA for all HCAs */
    uint32_t            rkey[MAX_NUM_HCAS];
    /* Protocol to be used, Choices: R3/RPUT/RGET */
    MRAILI_Protocol_t   protocol;
    /* This is required for telling the receiver
     * when to mark the recv as complete */
    uint8_t            weight_rail[MAX_NUM_HCAS];
} MPIDI_CH3I_MRAILI_Rndv_info_t;

#define MPIDI_CH3I_MRAILI_RNDV_INFO_DECL \
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;

struct dreg_entry;

#define MPIDI_CH3I_MRAILI_REQUEST_DECL \
    struct MPIDI_CH3I_MRAILI_Request {  \
        MPI_Request partner_id;         \
        uint8_t rndv_buf_alloc;         \
        void * rndv_buf;    \
        int rndv_buf_sz;    \
        int rndv_buf_off;   \
        MRAILI_Protocol_t protocol;     \
        struct dreg_entry *d_entry;     \
        void     *remote_addr;          \
        uint32_t rkey[MAX_NUM_HCAS];    \
        uint8_t  nearly_complete;       \
        uint8_t  num_rdma_read_completions;       \
        uint32_t completion_counter;   \
        double  initial_weight[MAX_NUM_SUBRAILS];   \
        double  stripe_start_time;   \
        double  stripe_finish_time[MAX_NUM_SUBRAILS];   \
        struct MPID_Request *next_inflow;  \
    } mrail;

#ifndef MV2_DISABLE_HEADER_CACHING 
#define MAX_SIZE_WITH_HEADER_CACHING 255

typedef struct MPIDI_CH3I_MRAILI_Pkt_fast_eager_t {
    uint8_t     type;
    uint8_t     bytes_in_pkt;
    uint16_t    seqnum;
} MPIDI_CH3I_MRAILI_Pkt_fast_eager;

typedef struct MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req_t {
    uint8_t     type;
    uint8_t     bytes_in_pkt;
    uint16_t    seqnum;
    int         sender_req_id;    
} MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req;
#endif

typedef struct MPIDI_CH3I_MRAILI_Pkt_comm_header_t {
    uint8_t type;  
#if defined(MPIDI_CH3I_MRAILI_IBA_PKT_DECL)
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#endif
} MPIDI_CH3I_MRAILI_Pkt_comm_header;

#define MPIDI_CH3I_MRAILI_Pkt_noop MPIDI_CH3I_MRAILI_Pkt_comm_header

typedef struct MRAILI_Channel_manager_t {
    int     num_channels;
    int     num_local_pollings;

    struct {
    	vbuf    *v_queue_head;
    	vbuf    *v_queue_tail;
    	int     len;
    } *msg_channels;

    struct MRAILI_Channel_manager_t *next_arriving;
    int    inqueue;

    struct MRAILI_Channel_manager_t *prev;
    struct MRAILI_Channel_manager_t *next;   /* for msg queue */
    int    pending_vbuf;

    void *vc;
} MRAILI_Channel_manager;

typedef struct MPIDI_CH3I_MRAILI_RDMAPATH_VC
{
    /**********************************************************
     * Following part of the structure is shared by all rails *
     **********************************************************/
    /* RDMA buffers */
    void    	*RDMA_send_buf_DMA;
    void    	*RDMA_recv_buf_DMA;
    struct vbuf *RDMA_send_buf;
    struct vbuf *RDMA_recv_buf;

    struct ibv_mr *RDMA_send_buf_mr[MAX_NUM_HCAS];
    struct ibv_mr *RDMA_recv_buf_mr[MAX_NUM_HCAS];
    uint32_t       RDMA_remote_buf_rkey[MAX_NUM_HCAS];

    /* current flow control credit accumulated for remote side */
    uint8_t 	rdma_credit;

    /* RDMA buffer address on the remote side */
    char    	*remote_RDMA_buf;

    int 	phead_RDMA_send;
    int 	ptail_RDMA_send;

    /* pointer to the head of free receive buffers
     * this is also where we should poll for incoming
     * rdma write messages */
    /* this pointer advances when we receive packets */
    int 	p_RDMA_recv;
    int 	p_RDMA_recv_tail;

    int 	eager_start_cnt;
    int 	in_polling_set;
    int     rdma_failed;

#ifndef MV2_DISABLE_HEADER_CACHING 
    void    	*cached_outgoing;
    void    	*cached_incoming; 
    int     	cached_hit;
    int     	cached_miss;
#endif
} MPIDI_CH3I_MRAILI_RDMAPATH_VC;

typedef struct _ibv_backlog_queue_t {
    int     	len;                  /* length of backlog queue */
    vbuf    	*vbuf_head;           /* head of backlog queue */
    vbuf    	*vbuf_tail;           /* tail of backlog queue */
} ibv_backlog_queue_t;


typedef struct MPIDI_CH3I_MRAILI_SR_VC
{
    struct {
	/* how many vbufs can be consumed on remote end. */
	uint8_t 	remote_credit;
	/* accumulate vbuf credit locally here */
	uint8_t 	local_credit;
	/* number of vbufs currently preposted */
	uint8_t 	preposts;
	uint32_t	 pending_r3_sends;
	uint8_t 	remote_cc;
	uint8_t 	initialized;
	int 		rendezvous_packets_expected;
        ibv_backlog_queue_t backlog;
    } *credits;
} MPIDI_CH3I_MRAILI_SR_VC;

struct mrail_rail {
        struct ibv_context *nic_context;
        int    hca_index;
        int    port;
        int    lid;
        int    s_weight;
        int    used_send_cq; 
        int    used_recv_cq; 
        union  ibv_gid  gid;
        struct ibv_cq	*cq_hndl;
        struct ibv_cq	*send_cq_hndl;
        struct ibv_cq	*recv_cq_hndl;
        struct ibv_qp 	*qp_hndl;
#ifdef RDMA_CM
        struct rdma_cm_id 	*cm_ids;
#endif
        int		send_wqes_avail;
        struct vbuf 	*ext_sendq_head;
        struct vbuf	*ext_sendq_tail;
        int             ext_sendq_size;
        struct  ibv_qp * qp_hndl_1sc;
        int     postsend_times_1sc;
#ifdef _ENABLE_XRC_
        uint32_t        rqp_hndl;      /* Receive only QP */
#endif
};

#ifdef CKPT
typedef struct MPIDI_CH3I_CR_msg_log_queue_entry {
    vbuf *buf;
    unsigned long len;
    struct MPIDI_CH3I_CR_msg_log_queue_entry *next;
}MPIDI_CH3I_CR_msg_log_queue_entry_t;
#endif

/* sample implemenation structure */
typedef struct MPIDI_CH3I_MRAIL_VC_t
{
    int     	num_rails;
    /* qp handle for each of the sub-rails */
    struct  	mrail_rail *rails;

    uint16_t        lid[MAX_NUM_HCAS][MAX_NUM_PORTS];
    union ibv_gid   gid[MAX_NUM_HCAS][MAX_NUM_PORTS];

    /* number of send wqes available */
    uint16_t 	next_packet_expected;
    uint16_t 	next_packet_tosend;

    /* how many eager sends do we have outstanding */
    int outstanding_eager_vbufs;

    /* what buffer are we currently packing */
    struct vbuf *coalesce_vbuf;

    MPIDI_CH3I_MRAILI_RDMAPATH_VC 	rfp;
    MPIDI_CH3I_MRAILI_SR_VC 		srp;

    MRAILI_Channel_manager  cmanager;
    /* Buffered receiving request for packetized transfer */
    void                    *packetized_recv;

    /* these fields are used to remember data transfer operations
     * that are currently in progress on this connection. The
     * send handle list is a queue of send handles representing
     * in-progress rendezvous transfers. It is processed in FIFO
     * order (because of MPI ordering rules) so there is both a head
     * and a tail.
     *
     * The receive handle is a pointer to a single
     * in-progress eager receive. We require that an eager sender
     * send *all* packets associated with an eager receive before
     * sending any others, so when we receive the first packet of
     * an eager series, we remember it by caching the rhandle
     * on the connection.
     *
     */
    void    *sreq_head; /* "queue" of send handles to process */
    void    *sreq_tail;
    /* these two fields are used *only* by MPID_DeviceCheck to
     * build up a list of connections that have received new
     * flow control credit so that pending operations should be
     * pushed. nextflow is a pointer to the next connection on the
     * list, and inflow is 1 (true) or 0 (false) to indicate whether
     * the connection is currently on the flowlist. This is needed
     * to prevent a circular list.
     */
    void    *nextflow;
    int     inflow;
    /* used to distinguish which VIA barrier synchronozations have
     * completed on this connection.  Currently, only used during
     * process teardown.
     *
     int barrier_id; */

    uint64_t remote_vc_addr; /* Used to find vc at remote side */
#ifdef CKPT
    /*Record the number of suspended rails*/
    int suspended_rails_send;
    int suspended_rails_recv;

    /*For buffering the message*/
    MPIDI_CH3I_CR_msg_log_queue_entry_t *msg_log_queue_head;
    MPIDI_CH3I_CR_msg_log_queue_entry_t *msg_log_queue_tail;

    /*For reactivation*/
    int reactivation_done_send;
    int reactivation_done_recv;
    /* num of active-rndvs in sreq_list. For sender side only.   */
    /* Inc before a Ckpt, and dec when updating rkeys by REMOTE_UPDATE msg */
    int sreq_to_update;
    pthread_spinlock_t      cr_lock;
    int react_send_ready; // At resume: can we enqueu the REACT_DONE msg for send?
    MPIDI_CH3I_CR_msg_log_queue_entry_t*    react_entry; 
    // if react_send_ready=0, store the REACT_DONE msg here 
#endif
} MPIDI_CH3I_MRAIL_VC;

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;

typedef struct MPIDI_CH3I_MRAIL_PG {
    struct ibv_ah   **cm_ah;        /*Array of address handles of peers */
    uint32_t        *cm_ud_qpn;     /*Array of ud pqn of peers */
    uint16_t        *cm_lid;        /*Array of lid of all procs */
    union ibv_gid    *cm_gid;        /*Array of gid of all procs */
#ifdef _ENABLE_XRC_
    uint32_t        *xrc_hostid;
#endif
} MPIDI_CH3I_MRAIL_PG_t;

#define MPIDI_CH3I_RDMA_PG_DECL MPIDI_CH3I_MRAIL_PG_t mrail;

/* structure MPIDI_CH3I_RDMA_Ops_list is the queue pool to record every
 * issued signaled RDMA write and RDMA read operation. The address of
 * the entries are assigned to the id field of the descriptors when they
 * are posted. So it will be easy to find the corresponding operators of
 * the RDMA operations when a completion queue entry is polled.
 */
struct MPIDI_CH3I_RDMA_put_get_list_t;
typedef struct MPIDI_CH3I_RDMA_put_get_list_t
MPIDI_CH3I_RDMA_put_get_list;

#endif /* MPIDI_CH3_RDMA_PRE_H */
