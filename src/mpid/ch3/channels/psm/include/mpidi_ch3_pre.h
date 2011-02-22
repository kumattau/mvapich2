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
#ifndef MPIDI_CH3_PRE_H
#define MPIDI_CH3_PRE_H

#include <sys/types.h>
#include <stdint.h>
#include <psm.h>
#include <psm_mq.h>

/* FIXME: These should be removed */
#define MPIDI_DEV_IMPLEMENTS_KVS


typedef struct MPIDI_CH3I_VC
{
    /* currently, psm is fully connected. so no state is needed. */
    struct MPID_Request *recv_active;
    void *pkt_active;
} MPIDI_CH3I_VC;

#define MPIDI_CH3_VC_DECL   \
    MPIDI_CH3I_VC   ch;

/*  psm specific items in MPID_Request 
    mqreq is the request pushed to psm
    psmcompnext is used by progress engine to keep track of reqs
    pkbuf, pksz are used for non-contig operations and contain
        packing buffers and sizes
    psm_flags are bitflags passed around
*/

#define MPID_DEV_PSM
#define MPID_DEV_PSM_REQUEST_DECL       \
    psm_mq_req_t mqreq;                 \
    struct MPID_Request *psmcompnext;   \
    struct MPID_Request *psmcompprev;   \
    struct MPID_Request *savedreq;      \
    struct MPID_Request *pending_req;   \
    int pktlen;                         \
    void *pkbuf;                        \
    uint32_t pksz;                      \
    uint32_t psm_flags;                 \
    void *vbufptr;                      

#if __GNUC__ >= 3
    #define likely(x)     __builtin_expect(!!(x), 1)
    #define unlikely(x)   __builtin_expect(!!(x), 0)
#else
    #define likely(x)     (x)
    #define unlikely(x)   (x)
#endif

#define PSM_BLOCKING    1
#define PSM_NONBLOCKING 0

/* bit-flags set in psm_flags */

#define PSM_NON_BLOCKING_SEND   0x00000001  /* send req nonblocking */
#define PSM_NON_CONTIG_REQ      0x00000002  /* is req non-config    */
#define PSM_SYNC_SEND           0x00000004  /* this is a sync send  */
#define PSM_SEND_CANCEL         0x00000008  /* send cancel req      */
#define PSM_RECV_CANCEL         0x00000010  /* recv cancel req      */
#define PSM_COMPQ_PENDING       0x00000020  /* req is in compQ      */
#define PSM_PACK_BUF_FREE       0x00000040  /* pack-buf not freed   */
#define PSM_NON_BLOCKING_RECV   0x00000080  /* recv req nonblocking */
#define PSM_1SIDED_PREPOST      0x00000100  /* preposted recv req   */
#define PSM_1SIDED_PUTREQ       0x00000200  /* 1-sided PUT req      */
#define PSM_RNDVRECV_PUT_REQ    0x00000400  /* rndv_recv req        */
#define PSM_CONTROL_PKTREQ      0x00000800  /* req is for ctrl-pkt  */
#define PSM_RNDVPUT_COMPLETED   0x00001000  /* completed rdnv req   */
#define PSM_RNDVSEND_REQ        0x00002000  /* rendezvous send req  */
#define PSM_RNDV_ACCUM_REQ      0x00004000  /* req is rndv accum    */
#define PSM_RNDVRECV_ACCUM_REQ  0x00008000  /* rndv_recv req        */
#define PSM_GETRESP_REQ         0x00010000  
#define PSM_GETPKT_REQ          0x00020000
#define PSM_RNDVRECV_GET_REQ    0x00040000
#define PSM_RNDVRECV_NC_REQ     0x00080000
#define PSM_NEED_DTYPE_RELEASE  0x00100000
#define PSM_RNDVRECV_GET_PACKED 0x00200000

#define MPIDI_CH3_REQUEST_INIT(__p)  \
        __p->psm_flags = 0;          \
        __p->pkbuf = 0;              \
        __p->pksz = 0                \

#define HAVE_DEV_COMM_HOOK
#define MPID_Dev_comm_create_hook( a ) 
#define MPID_Dev_comm_destroy_hook( a ) 

#endif
