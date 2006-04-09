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

#ifndef _VBUF_H_
#define _VBUF_H_

#include "infiniband/verbs.h"

#include "ibv_param.h"

#if !defined(_PCI_X_) && !defined(_PCI_EX_)
#error IO_BUS type _PCI_X_, _PCI_EX_ must be defined
#endif

#if defined(_LARGE_CLUSTER)
        #define VBUF_TOTAL_SIZE (2*1024)
#elif defined(_MEDIUM_CLUSTER)
        #define VBUF_TOTAL_SIZE (4*1024)
#else

#if defined(MAC_OSX)
        #define VBUF_TOTAL_SIZE (16*1024)
#elif defined(_X86_64_)
        #if defined(_PCI_X_)
                #define VBUF_TOTAL_SIZE (12*1024)
        #elif defined(_PCI_EX_)
                #define VBUF_TOTAL_SIZE (12*1024)
        #else
                #define VBUF_TOTAL_SIZE (12*1024)
        #endif
                                                                                                                                                             
#elif defined(_EM64T_)
        #if defined(_PCI_X_)
                #define VBUF_TOTAL_SIZE (12*1024)
        #elif defined(_PCI_EX_)
                #define VBUF_TOTAL_SIZE (6*1024)
        #else
                #define VBUF_TOTAL_SIZE (6*1024)
        #endif

#elif defined(_PPC64_)
        #define VBUF_TOTAL_SIZE (6*1024)

#elif defined(_IA32_)
        #if defined(_PCI_X_)
                #define VBUF_TOTAL_SIZE (12*1024)
        #elif defined(_PCI_EX_)
                #define VBUF_TOTAL_SIZE (6*1024)
        #else
                #define VBUF_TOTAL_SIZE (12*1024)
        #endif
                                                                                                                                                             
#else
        #if defined(_PCI_X_)
                #define VBUF_TOTAL_SIZE (12*1024)
        #elif defined(_PCI_EX_)
                #define VBUF_TOTAL_SIZE (6*1024)
        #else
                #define VBUF_TOTAL_SIZE (12*1024)
        #endif
                                                                                                                                                             
#endif
                                                                                                                                                             
#endif

#define VBUF_ALIGNMENT VBUF_TOTAL_SIZE

#define CREDIT_VBUF_FLAG (111)
#define NORMAL_VBUF_FLAG (222)
#define RPUT_VBUF_FLAG (333)
#define VBUF_FLAG_TYPE u_int32_t

#define FREE_FLAG (0)
#define BUSY_FLAG (1)

#define ALIGN_UNIT (4)
#define MRAILI_ALIGN_LEN(len, align_len) \
{                                                   \
    align_len = ((int)(((len)+ALIGN_UNIT-1) /         \
                ALIGN_UNIT)) * ALIGN_UNIT;          \
}

#ifdef RDMA_FAST_PATH
#define MRAILI_FAST_RDMA_VBUF_START(_v, _len, _start) \
{                                                                       \
    int __align_len;                                                      \
    __align_len = ((int)(((_len)+ALIGN_UNIT-1) / ALIGN_UNIT)) * ALIGN_UNIT; \
    _start = (void *)((unsigned long)&(_v->head_flag) - __align_len);    \
}
#endif
/* 
   This function will return -1 if buf is NULL
   else return -2 if buf contains a pkt that doesn't contain sequence number 
*/

#define PKT_NO_SEQ_NUM -2
#define PKT_IS_NULL         -1


/*
 * brief justification for vbuf format:
 * descriptor must be aligned (64 bytes).
 * vbuf size must be multiple of this alignment to allow contiguous allocation
 * descriptor and buffer should be contiguous to allow via implementations that
 * optimize contiguous descriptor/data (? how likely ?)
 * need to be able to store send handle in vbuf so that we can mark sends
 * complete when communication completes. don't want to store
 * it in packet header because we don't always need to send over the network.
 * don't want to store at beginning or between desc and buffer (see above) so
 * store at end.
 */

struct ibv_wr_descriptor {
    union {
        struct ibv_recv_wr rr;
        struct ibv_send_wr sr;
    };
    union {
        struct ibv_send_wr * bad_sr;
        struct ibv_recv_wr * bad_rr;
    };
    struct ibv_sge sg_entry;
    void *next;
};

typedef struct MPIDI_CH3I_MRAILI_Channel_info_t {
    int hca_index;
    int port_index;
    int rail_index;
} MRAILI_Channel_info;

#define MRAILI_CHANNEL_INFO_INIT(subchannel,i,vc) {         \
    subchannel.hca_index = i/vc->mrail.subrail_per_hca;     \
    subchannel.port_index = i%vc->mrail.subrail_per_hca;    \
    subchannel.rail_index = i;                              \
}

#ifdef RDMA_FAST_PATH
#define VBUF_BUFFER_SIZE                                            \
(VBUF_TOTAL_SIZE - sizeof(struct ibv_wr_descriptor) - 3*sizeof(void *)         \
 - sizeof(struct vbuf_region*) -sizeof(int) - sizeof(MRAILI_Channel_info) - \
sizeof(VBUF_FLAG_TYPE))

#else
#define VBUF_BUFFER_SIZE   \
(VBUF_TOTAL_SIZE - sizeof(struct ibv_wr_descriptor) - 3*sizeof(void *)         \
 - sizeof(struct vbuf_region*) - sizeof(MRAILI_Channel_info) - \
sizeof(VBUF_FLAG_TYPE))
#endif

#define MRAIL_MAX_EAGER_SIZE VBUF_BUFFER_SIZE

typedef struct vbuf {
    
    struct ibv_wr_descriptor desc;
    void *pheader;
    void *sreq;
    struct vbuf_region *region;
    void *vc;
    MRAILI_Channel_info subchannel;

#if defined(RDMA_FAST_PATH)
    int padding;
#endif
    unsigned char buffer[VBUF_BUFFER_SIZE];

    VBUF_FLAG_TYPE head_flag;
    /* NULL shandle means not send or not complete. Non-null
     * means pointer to send handle that is now complete. Used
     * by viadev_process_send
     */
} vbuf;

/* one for head and one for tail */
#define VBUF_FAST_RDMA_EXTRA_BYTES (sizeof(VBUF_FLAG_TYPE))

#define FAST_RDMA_ALT_TAG 0x8000
#define FAST_RDMA_SIZE_MASK 0x7fff

/*
 * Vbufs are allocated in blocks and threaded on a single free list.
 *
 * These data structures record information on all the vbuf
 * regions that have been allocated.  They can be used for
 * error checking and to un-register and deallocate the regions
 * at program termination.
 *
 */
typedef struct vbuf_region {
    struct ibv_mr *mem_handle[MAX_NUM_HCAS]; /* mem hndl for entire region */
    void *malloc_start;         /* used to free region later  */
    void *malloc_end;           /* to bracket mem region      */
    int count;                  /* number of vbufs in region  */
    struct vbuf *vbuf_head;     /* first vbuf in region       */
    struct vbuf_region *next;   /* thread vbuf regions        */
} vbuf_region;

static void inline VBUF_SET_RDMA_ADDR_KEY(vbuf * v, int len,
                                          void *local_addr,
                                          uint32_t lkey,
                                          void *remote_addr,
                                          uint32_t rkey)
{
    v->desc.sr.next         = NULL;
    v->desc.sr.opcode       = IBV_WR_RDMA_WRITE;
    v->desc.sr.send_flags   = IBV_SEND_SIGNALED;
    v->desc.sr.wr_id        = (uintptr_t) v;

    v->desc.sr.num_sge      = 1;
    v->desc.sr.sg_list      = &(v->desc.sg_entry);

    (v)->desc.sr.wr.rdma.remote_addr = (uintptr_t) (remote_addr);
    (v)->desc.sr.wr.rdma.rkey = (rkey);
    (v)->desc.sg_entry.length = (len);
    (v)->desc.sg_entry.lkey = (lkey);
    (v)->desc.sg_entry.addr = (uintptr_t)(local_addr);
}

int allocate_vbufs(struct ibv_pd * ptag[], int nvbufs);

void deallocate_vbufs(void);

vbuf *get_vbuf();

void MRAILI_Release_vbuf(vbuf * v);

void vbuf_init_rdma_write(vbuf * v);

void vbuf_init_send(vbuf * v, unsigned long len,
                    const MRAILI_Channel_info *);

void vbuf_init_recv(vbuf * v, unsigned long len,
                    const MRAILI_Channel_info *);

void vbuf_init_rput(vbuf * v, void *local_address,
                    uint32_t lkey, void *remote_address,
                    uint32_t rkey, int nbytes,
                    const MRAILI_Channel_info *);

void dump_vbuf(char *msg, vbuf * v);

#endif
