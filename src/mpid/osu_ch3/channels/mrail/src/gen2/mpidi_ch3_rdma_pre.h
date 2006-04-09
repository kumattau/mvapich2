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

#ifndef MPIDI_CH3_RDMA_PRE_H
#define MPIDI_CH3_RDMA_PRE_H

#include "vbuf.h"
#include "mpiimpl.h"
#include "ibv_param.h"

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;
#define MPIDI_CH3I_MRAILI_IBA_PKT_DEFS 1

/* #define _SCHEDULE 1 */

typedef struct MPIDI_CH3I_MRAILI_IBA_Pkt {
    u_int8_t vbuf_credit;      /* piggybacked vbuf credit   */
    u_int8_t remote_credit;    /* our current credit count */
#if defined(RDMA_FAST_PATH)
    u_int8_t rdma_credit;
#endif
} MPIDI_CH3I_MRAILI_Iba_pkt_t;

#define MPIDI_CH3I_MRAILI_IBA_PKT_DECL \
    MPIDI_CH3I_MRAILI_Iba_pkt_t mrail;

typedef enum {
    VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED = 0,
    VAPI_PROTOCOL_EAGER,
    VAPI_PROTOCOL_R3,
    VAPI_PROTOCOL_RPUT,
    VAPI_PROTOCOL_RGET,
} MRAILI_Protocol_t;

typedef struct MPIDI_CH3I_MRAILI_Rndv_info {
    MRAILI_Protocol_t   protocol;
    void                *buf_addr;
    uint32_t            rkey;
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
    uint32_t rkey;                  \
    uint8_t  nearly_complete;       \
    struct MPID_Request *next_inflow;  \
} mrail;

#ifdef USE_HEADER_CACHING
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
    uint8_t type;  /* XXX - uint8_t to conserve space ??? */
#if defined(MPIDI_CH3I_MRAILI_IBA_PKT_DECL)
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#endif
} MPIDI_CH3I_MRAILI_Pkt_comm_header;

#define MPIDI_CH3I_MRAILI_Pkt_noop MPIDI_CH3I_MRAILI_Pkt_comm_header

typedef struct MRAILI_Channel_manager_t {
    int     total_subrails;
    
    vbuf    *v_queue_head[MAX_SUBCHANNELS];  /* put vbufs from each channel */
    vbuf    *v_queue_tail[MAX_SUBCHANNELS];
    int     len[MAX_SUBCHANNELS];

    int     num_local_pollings;
    vbuf    *(**poll_channel)(void *vc);
} MRAILI_Channel_manager;


#ifdef RDMA_FAST_PATH
typedef struct MPIDI_CH3I_MRAILI_RDMAPATH_VC
{
    /**********************************************************
     * Following part of the structure is shared by all rails *
     **********************************************************/
    /* RDMA buffers */
    void    *RDMA_send_buf_orig;
    void    *RDMA_recv_buf_orig;
    struct vbuf *RDMA_send_buf;
    struct vbuf *RDMA_recv_buf;
    /* RDMA buffer address on the remote side */
    struct vbuf *remote_RDMA_buf;

    struct ibv_mr *RDMA_send_buf_mr[MAX_NUM_HCAS];
    struct ibv_mr *RDMA_recv_buf_mr[MAX_NUM_HCAS];
    uint32_t       RDMA_remote_buf_rkey;

    /* current flow control credit accumulated for remote side */
    u_int8_t rdma_credit;

    int phead_RDMA_send;
    int ptail_RDMA_send;

    /* pointer to the head of free receive buffers
     * this is also where we should poll for incoming
     * rdma write messages */
    /* this pointer advances when we receive packets */
    int p_RDMA_recv;
    int p_RDMA_recv_tail;

#ifdef USE_HEADER_CACHING
    void    *cached_outgoing;
    void    *cached_incoming; 
    int     cached_hit;
    int     cached_miss;
#endif
} MPIDI_CH3I_MRAILI_RDMAPATH_VC;
#endif

typedef struct _ibv_backlog_queue_t {
    int     len;                   /* length of backlog queue */
    vbuf    *vbuf_head;           /* head of backlog queue */
    vbuf    *vbuf_tail;           /* tail of backlog queue */
} ibv_backlog_queue_t;


typedef struct MPIDI_CH3I_MRAILI_SR_VC
{
    uint8_t remote_credit[MAX_SUBCHANNELS];   /* how many vbufs I can consume on remote end. */
    uint8_t local_credit[MAX_SUBCHANNELS];    /* accumulate vbuf credit locally here */
    uint8_t preposts[MAX_SUBCHANNELS];        /* number of vbufs currently preposted */

    uint8_t remote_cc[MAX_SUBCHANNELS];
    uint8_t initialized[MAX_SUBCHANNELS];

    /* the backlog queue for this connection. */
    ibv_backlog_queue_t backlog;
    /* This field is used for managing preposted receives. It is the
     * number of rendezvous packets (r3/rput) expected to arrive for
     * receives we've ACK'd but have not been completed. In general we
     * can't prepost all of these vbufs, but we do prepost extra ones
     * to allow sender to keep the pipe full. As packets come in this
     * field is decremented.  We know when to stop preposting extra
     * buffers when this number goes to zero.
     */
    int rendezvous_packets_expected[MAX_SUBCHANNELS];
} MPIDI_CH3I_MRAILI_SR_VC;

/* sample implemenation structure */
typedef struct MPIDI_CH3I_MRAIL_VC_t
{
    int     num_total_subrails;
    int     subrail_per_hca;
    /* qp handle for each of the sub-rails */
    struct  ibv_qp * qp_hndl[MAX_SUBCHANNELS];

#ifdef ONE_SIDED
    struct  ibv_qp * qp_hndl_1sc;
    int     postsend_times_1sc;
#endif

    /* number of send wqes available */
    /* Following three pointers are allocated in MPIDI_Init_vc function*/
    int     send_wqes_avail[MAX_SUBCHANNELS];
     /* queue of sends which didn't fit on send Q */ 
    struct  vbuf *ext_sendq_head[MAX_SUBCHANNELS]; 
    struct  vbuf *ext_sendq_tail[MAX_SUBCHANNELS];

    u_int16_t next_packet_expected;
    u_int16_t next_packet_tosend;

#ifdef RDMA_FAST_PATH
    MPIDI_CH3I_MRAILI_RDMAPATH_VC rfp;
#endif
    MPIDI_CH3I_MRAILI_SR_VC srp;

    /* Buffered receiving request for packetized transfer */
    void                    *packetized_recv;
    MRAILI_Channel_manager  cmanager;

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
} MPIDI_CH3I_MRAIL_VC;

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;


#ifdef ONE_SIDED
/* structure MPIDI_CH3I_RDMA_Ops_list is the queue pool to record every
 * issued signaled RDMA write and RDMA read operation. The address of
 * the entries are assigned to the id field of the descriptors when they
 * are posted. So it will be easy to find the corresponding operators of
 * the RDMA operations when a completion queue entry is polled.
 */
struct MPIDI_CH3I_RDMA_put_get_list_t;
typedef struct MPIDI_CH3I_RDMA_put_get_list_t
        MPIDI_CH3I_RDMA_put_get_list;

#endif

#endif /* MPIDI_CH3_RDMA_PRE_H */
