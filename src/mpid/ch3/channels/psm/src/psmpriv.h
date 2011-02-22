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

#ifndef _PSMPRIV_H
#define _PSMPRIV_H

#define _GNU_SOURCE
#include <stdint.h>
#include <psm.h>
#include <psm_mq.h>
#include <string.h>
#include <pthread.h>
#include "mpidimpl.h"
#include "pmi.h"

#define MPID_PSM_UUID           "uuid"      /* pmi key for uuid */
#define WRBUFSZ                 1024        /* scratch buffer */
#define ROOT                    0           
#define TIMEOUT                 50          /* connect timeout */
#define MQ_FLAGS_NONE           0 

/* tag selection macros, taken from mvapich-psm code */
#define MQ_TAGSEL_ALL           0xffffffffffffffff
#define TAG_BITS                32
#define TAG_MASK                ~(MQ_TAGSEL_ALL << TAG_BITS)
#define SRC_RANK_BITS           16
#define SRC_RANK_MASK           ~(MQ_TAGSEL_ALL << SRC_RANK_BITS)
#define MQ_TAGSEL_ANY_SOURCE    (MQ_TAGSEL_ALL << SRC_RANK_BITS)
#define MQ_TAGSEL_ANY_TAG       ~(TAG_MASK << SRC_RANK_BITS)
#define SEC_IN_NS               1000000000ULL

//#define DEBUG_PSM
#ifdef DEBUG_PSM
#define DBG(args...)                                                 \
    do {                                                             \
        int __rank;                                                  \
        PMI_Get_rank(&__rank);                                       \
        fprintf(stderr, "[%d][%s:%d]\t\t", __rank, __FILE__, __LINE__); \
        fprintf(stderr, args);                                       \
        fflush(stderr);                                              \
    } while (0)
#else /* defined(DEBUG_) */
#define DBG(args...)
#endif /* defined(DEBUG) */

#define PSM_ERR_ABORT(args...) do {                                          \
    int __rank; PMI_Get_rank(&__rank);                                       \
    fprintf(stderr, "[Rank %d][%s: line %d]", __rank ,__FILE__, __LINE__);   \
    fprintf(stderr, args);                                                   \
    fprintf(stderr, "\n");                                                   \
    fflush(stderr);                                                          \
}while (0)

#define MAKE_PSM_SELECTOR(out, cid, tag, rank) do { \
        out = cid;                                  \
        out = out << TAG_BITS;                      \
        out = out | (tag & TAG_MASK);               \
        out = out << SRC_RANK_BITS;                 \
        out = out | (rank & SRC_RANK_MASK);         \
} while(0)

#define CAN_BLK_PSM(_len) ((MPIR_ThreadInfo.thread_provided != MPI_THREAD_MULTIPLE) &&  \
                             (_len < ipath_rndv_thresh))

int psm_no_lock(pthread_spinlock_t *);
int (*psm_lock_fn)(pthread_spinlock_t *);
int (*psm_unlock_fn)(pthread_spinlock_t *);

#define _psm_enter_  psm_lock_fn(&psmlock)
#define _psm_exit_   psm_unlock_fn(&psmlock)

#define PSM_COUNTERS    8 

struct psmdev_info_t {
    psm_ep_t        ep;
    psm_mq_t        mq;
    psm_epaddr_t    *epaddrs;
    int             pg_size;
    uint16_t        cnt[PSM_COUNTERS];
};

#define psm_tot_sends           psmdev_cw.cnt[0]
#define psm_tot_recvs           psmdev_cw.cnt[1]
#define psm_tot_pposted_recvs   psmdev_cw.cnt[2]
#define psm_tot_eager_puts      psmdev_cw.cnt[3]
#define psm_tot_rndv_puts       psmdev_cw.cnt[4]
#define psm_tot_eager_gets      psmdev_cw.cnt[5]
#define psm_tot_rndv_gets       psmdev_cw.cnt[6]
#define psm_tot_accs            psmdev_cw.cnt[7]

/* externs */
extern struct psmdev_info_t psmdev_cw;
extern uint32_t             ipath_rndv_thresh;
extern uint8_t              ipath_debug_enable;
extern pthread_spinlock_t   psmlock;

int psm_doinit(int has_parent, MPIDI_PG_t *pg, int pg_rank);   
int psm_istartmsgv(MPIDI_VC_t *vc, MPID_IOV *iov, int iov_n, MPID_Request **rptr);
int psm_recv(int rank, int tag, int context_id, void *buf, int buflen,
             MPI_Status *stat, MPID_Request **req);
int psm_send_noncontig(MPIDI_VC_t *vc, MPID_Request *sreq, 
                       MPIDI_Message_match match);
int MPIDI_CH3_iRecv(int rank, int tag, int cid, void *buf, int buflen, MPID_Request *req);
int MPIDI_CH3_Recv(int rank, int tag, int cid, void *buf, int buflen, MPI_Status *stat, MPID_Request **req);

void psm_pe_yield();
MPID_Request *psm_create_req();
void psm_update_mpistatus(MPI_Status *, psm_mq_status_t);
int psm_1sided_input(MPID_Request *req, int inlen);

#endif 
