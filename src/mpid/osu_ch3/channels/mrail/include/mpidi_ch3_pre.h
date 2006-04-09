/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2006, The Ohio State University. All rights
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

#if !defined(MPICH_MPIDI_CH3_PRE_H_INCLUDED)
#define MPICH_MPIDI_CH3_PRE_H_INCLUDED

#include "mpidi_ch3i_rdma_conf.h"
#include "mpidi_ch3_rdma_pre.h"

/*#define MPICH_DBG_OUTPUT*/

typedef struct MPIDI_CH3I_Process_group_s
{
    char * kvs_name;
    struct MPIDI_VC * unex_finished_list;
    int nEagerLimit;
    int nRDMAWaitSpinCount;
    int nRDMAWaitYieldCount;
# if defined(MPIDI_CH3I_RDMA_PG_DECL)
    MPIDI_CH3I_RDMA_PG_DECL
# endif
}
MPIDI_CH3I_Process_group_t;

#define MPIDI_CH3_PG_DECL MPIDI_CH3I_Process_group_t ch;

#define MPIDI_CH3_PKT_DECL 
#define MPIDI_CH3_PKT_ENUM
#define MPIDI_CH3_PKT_DEFS

#define MPIDI_DEV_IMPLEMENTS_KVS

typedef enum MPIDI_CH3I_VC_state
{
    MPIDI_CH3I_VC_STATE_INVALID,
    MPIDI_CH3I_VC_STATE_IDLE,
    MPIDI_CH3I_VC_STATE_FAILED
}
MPIDI_CH3I_VC_state_t;

/* This structure requires the iovec structure macros to be defined */
typedef struct MPIDI_CH3I_Buffer_t
{
    int use_iov;
    unsigned int num_bytes;
    void *buffer;
    unsigned int bufflen;
    MPID_IOV *iov;
    int iovlen;
    int index;
    int total;
} MPIDI_CH3I_Buffer_t;

typedef struct MPIDI_CH3I_RDMA_Unex_read_s
{
    struct MPIDI_CH3I_RDMA_Packet_t *pkt_ptr;
    unsigned char *buf;
    unsigned int length;
    int src;
    struct MPIDI_CH3I_RDMA_Unex_read_s *next;
} MPIDI_CH3I_RDMA_Unex_read_t;

typedef struct MPIDI_CH3I_VC
{
    struct MPID_Request * sendq_head;
    struct MPID_Request * sendq_tail;
    struct MPID_Request * send_active;
    struct MPID_Request * recv_active;
    struct MPID_Request * req;
    MPIDI_CH3I_VC_state_t state;
    MPIDI_CH3I_Buffer_t read;
    int read_state;
    int port_name_tag;
} MPIDI_CH3I_VC;

/* SMP Channel is added by OSU-MPI2 */
#ifdef _SMP_
typedef struct MPIDI_CH3I_SMP_VC
{
    struct MPID_Request * sendq_head;
    struct MPID_Request * sendq_tail;
    struct MPID_Request * send_active;
    struct MPID_Request * recv_active;
    int local_nodes;
} MPIDI_CH3I_SMP_VC;
#endif

#ifndef MPIDI_CH3I_VC_RDMA_DECL
#define MPIDI_CH3I_VC_RDMA_DECL
#endif

#ifdef _SMP_
#define MPIDI_CH3_VC_DECL \
MPIDI_CH3I_VC ch; \
MPIDI_CH3I_SMP_VC smp; \
MPIDI_CH3I_VC_RDMA_DECL
#else
#define MPIDI_CH3_VC_DECL \
MPIDI_CH3I_VC ch; \
MPIDI_CH3I_VC_RDMA_DECL
#endif
/* end of OSU-MPI2 */

/*
 * MPIDI_CH3_CA_ENUM (additions to MPIDI_CA_t)
 *
 * MPIDI_CH3I_CA_HANDLE_PKT - The completion of a packet request (send or
 * receive) needs to be handled.
 */
#define MPIDI_CH3_CA_ENUM			\
MPIDI_CH3I_CA_HANDLE_PKT,			\
MPIDI_CH3I_CA_END_RDMA


/*
 * MPIDI_CH3_REQUEST_DECL (additions to MPID_Request)
 */
#define MPIDI_CH3_REQUEST_DECL						\
struct MPIDI_CH3I_Request						\
{									\
    /* iov_offset points to the current head element in the IOV */	\
    int iov_offset;							\
    									\
    /*  pkt is used to temporarily store a packet header associated	\
       with this request */						\
    MPIDI_CH3_Pkt_t pkt;						\
} ch;

typedef struct MPIDI_CH3I_Progress_state
{
    int completion_count;
}
MPIDI_CH3I_Progress_state;

#define MPIDI_CH3_PROGRESS_STATE_DECL MPIDI_CH3I_Progress_state ch;

#endif /* !defined(MPICH_MPIDI_CH3_PRE_H_INCLUDED) */
