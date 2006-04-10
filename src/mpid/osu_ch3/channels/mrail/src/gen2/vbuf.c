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
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "infiniband/verbs.h"
#include "pmi.h"
#include "rdma_impl.h"
#include "vbuf.h"
#include "ibv_priv.h"
#include "dreg.h"

/* head of list of allocated vbuf regions */
static vbuf_region *vbuf_region_head = NULL;
                                                                                                                                               
/*
 * free_vbuf_head is the head of the free list
 */
                                                                                                                                               
static vbuf *free_vbuf_head = NULL;

/*
 * cache the nic handle, and ptag the first time a region is
 * allocated (at init time) for later additional vbur allocations
 */
static struct ibv_pd *ptag_save[MAX_NUM_HCAS];

static int vbuf_n_allocated = 0;
static long num_free_vbuf = 0;
static long num_vbuf_get = 0;
static long num_vbuf_freed = 0;

void dump_vbuf_region(vbuf_region * r)
{
}
                                                                                                                                               
void dump_vbuf_regions()
{
    vbuf_region *r = vbuf_region_head;
                                                                                                                                               
    while (r) {
        dump_vbuf_region(r);
        r = r->next;
    }
}

void deallocate_vbufs()
{
    vbuf_region *r = vbuf_region_head;
    int ret;
    int i;
                                                                                                                                               
    while (r) {
        for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; i ++) {
            if (r->mem_handle[i]) {
                ret = ibv_dereg_mr(r->mem_handle[i]);
                if (ret) {
                    ibv_error_abort(IBV_RETURN_ERR,
                                    "could not deregister MR");
                }
                /* free vbufs add it later */
            }
        }
        DEBUG_PRINT("deregister vbufs\n");
        r = r->next;
    }
}

static int allocate_vbuf_region(int nvbufs)
{
    struct vbuf_region *reg;
    void *mem;
    int i;
    vbuf *cur;
    int alignment = VBUF_TOTAL_SIZE;
                                                                                                                                               
    if (free_vbuf_head != NULL)
        ibv_error_abort(GEN_ASSERT_ERR, "free_vbuf_head = NULL");
    /* are we limiting vbuf allocation?  If so, make sure
     * we dont alloc more than allowed
     */
    if (rdma_vbuf_max > 0) {
        nvbufs = MIN(nvbufs, rdma_vbuf_max - vbuf_n_allocated);
        if (nvbufs <= 0) {
            ibv_error_abort(GEN_EXIT_ERR,
                                "VBUF alloc failure, limit exceeded");
        }
    }
                                                                                                                                               
    SET_ORIGINAL_MALLOC_HOOKS;
    reg = (struct vbuf_region *) malloc(sizeof(struct vbuf_region));
    if (NULL == reg) {
        ibv_error_abort(GEN_EXIT_ERR,
                            "Unable to malloc a new struct vbuf_region");
    }
    mem = (void *) malloc(nvbufs * sizeof(vbuf) + (alignment - 1));
    if (NULL == mem) {
        fprintf(stderr, "[%s %d] Cannot allocate vbuf region\n", __FILE__, __LINE__);
        return -1;
    }
                                                                                                                                               
    SAVE_MALLOC_HOOKS;
    SET_ORIGINAL_MALLOC_HOOKS;

    memset(mem, 0, nvbufs * sizeof(vbuf) + (alignment - 1));
                                                                                                                                               
    vbuf_n_allocated += nvbufs;
    num_free_vbuf += nvbufs;
    reg->malloc_start = mem;
    reg->malloc_end = (void *) ((char *) mem + nvbufs * sizeof(vbuf) +
                                alignment - 1);
    reg->count = nvbufs;
    free_vbuf_head = (vbuf *) (((uintptr_t) mem + (uintptr_t) (alignment - 1)) &
                               ~((uintptr_t) alignment - 1));
    reg->vbuf_head = free_vbuf_head;
                                                                                                                                               
    DEBUG_PRINT("VBUF REGION ALLOCATION SZ %d TOT %d FREE %ld NF %ld NG %ld",
            nvbufs, vbuf_n_allocated, num_free_vbuf,
            num_vbuf_freed, num_vbuf_get);

    /* region should be registered for both of the hca */
    for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; i ++) {
        reg->mem_handle[i] = ibv_reg_mr(ptag_save[i], free_vbuf_head, 
                             nvbufs * sizeof(vbuf), 
                             IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        if (!reg->mem_handle[i]) {
            fprintf(stderr, "[%s %d] Cannot register vbuf region\n", 
                    __FILE__, __LINE__);
            return -1;  
        }
    }

    /* init the free list */
    for (i = 0; i < nvbufs - 1; i++) {
        cur = free_vbuf_head + i;
                                                                                                                                               
        cur->desc.next = free_vbuf_head + i + 1;
        cur->region = reg;
    }
    /* last one needs to be set to NULL */
    cur = free_vbuf_head + nvbufs - 1;
                                                                                                                                               
    cur->desc.next = NULL;
                                                                                                                                               
    cur->region = reg;

    /* thread region list */
    reg->next = vbuf_region_head;
    vbuf_region_head = reg;

    return 0;
}

int allocate_vbufs(struct ibv_pd * ptag[], int nvbufs)
{
    /* this function is only called by the init routines.
     * cache the nic handle and ptag for later vbuf_region allocations
     */
    int i;

    for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; i ++) {
        ptag_save[i] = ptag[i];
    }
                                                                                                                                               
    /* now allocate the first vbuf region */
    return allocate_vbuf_region(nvbufs);
}

vbuf *get_vbuf()
{
    vbuf *v;
    /*
     * It will often be possible for higher layers to recover
     * when no vbuf is available, but waiting for more descriptors
     * to complete. For now, just abort.
     */
    if (NULL == free_vbuf_head ) {
        DEBUG_PRINT("Allocating new vbuf region\n");
                                                                                                                                               
        allocate_vbuf_region(rdma_vbuf_secondary_pool_size);
        if (NULL ==free_vbuf_head) {
            ibv_error_abort(GEN_EXIT_ERR,
                    "No free vbufs. Pool size %d",
                       vbuf_n_allocated);
        }
    }
                                                                                                                                               
    v = free_vbuf_head;
    num_free_vbuf--;
    num_vbuf_get++;

    /* this correctly handles removing from single entry free list */
    free_vbuf_head = free_vbuf_head->desc.next;
#if defined(RDMA_FAST_PATH)
                                                                                                                                               
    /* need to change this to RPUT_VBUF_FLAG later
     * if we are doing rput */
    v->padding = NORMAL_VBUF_FLAG;
#endif
    v->pheader = (void *)v->buffer;
    /* this is probably not the right place to initialize shandle to NULL.
     * Do it here for now because it will make sure it is always initialized.
     * Otherwise we would need to very carefully add the initialization in
     * a dozen other places, and probably miss one.
     */
    v->sreq = NULL;
                                                                                                                                          
    return(v);
}

void MRAILI_Release_vbuf(vbuf *v)
{
    /* note this correctly handles appending to empty free list */
                                                                                                                                               
    DEBUG_PRINT("release_vbuf: releasing %p previous head = %p, padding %d\n",
        v, free_vbuf_head, v->padding);
                                                                                                                                               
    assert(v != free_vbuf_head);
                                                                                                                                               
    v->desc.next = free_vbuf_head;
#if defined(RDMA_FAST_PATH)
    if ((v->padding != NORMAL_VBUF_FLAG)
        && (v->padding != RPUT_VBUF_FLAG)) {
        ibv_error_abort(GEN_EXIT_ERR, "vbuf not correct!!!\n");
    }
#endif
    free_vbuf_head = v;
    v->pheader = NULL;
    v->head_flag = 0;
    v->sreq = NULL;
    v->vc = NULL;
    num_free_vbuf++;
    num_vbuf_freed++;
}

#if defined(RDMA_FAST_PATH)
void MRAILI_Release_recv_rdma(vbuf *v)
{
    vbuf *next_free;
    MPIDI_VC_t * c = (MPIDI_VC_t *)v->vc;
    int next;
    int i;

    next = c->mrail.rfp.p_RDMA_recv_tail + 1;
    if (next >= num_rdma_buffer)
        next = 0;
    next_free = &(c->mrail.rfp.RDMA_recv_buf[next]);
                                                                                                                                               
    v->padding = FREE_FLAG;
    v->head_flag = 0;
    v->sreq = NULL;

    if (v != next_free) {
        return;
    }
                                                                                                                                               
    /* search all free buffers */
    for (i = next; i != c->mrail.rfp.p_RDMA_recv;) {

        if (c->mrail.rfp.RDMA_recv_buf[i].padding == FREE_FLAG) {
            c->mrail.rfp.rdma_credit++;
            if (++(c->mrail.rfp.p_RDMA_recv_tail) >= num_rdma_buffer)
                c->mrail.rfp.p_RDMA_recv_tail = 0;
            c->mrail.rfp.RDMA_recv_buf[i].padding = BUSY_FLAG;
            c->mrail.rfp.RDMA_recv_buf[i].head_flag = 0;
        } else break;
        if (++i >= num_rdma_buffer)
            i = 0;
    }
}
#endif
void vbuf_init_rdma_write(vbuf * v)
{
    v->desc.sr.next         = NULL;
    v->desc.sr.opcode       = IBV_WR_RDMA_WRITE;
    v->desc.sr.send_flags   = IBV_SEND_SIGNALED;
    v->desc.sr.wr_id        = (uintptr_t) v;

    v->desc.sr.num_sge      = 1;
    v->desc.sr.sg_list      = &(v->desc.sg_entry);
#ifdef RDMA_FAST_PATH
    v->padding              = FREE_FLAG;
#endif
}

void vbuf_init_send(vbuf *v, unsigned long len, const MRAILI_Channel_info * subchannel)
{
    int hca_num = subchannel->hca_index;

    v->desc.sr.next         = NULL;
    v->desc.sr.send_flags   = IBV_SEND_SIGNALED;
    v->desc.sr.opcode       = IBV_WR_SEND;
    v->desc.sr.wr_id        = (uintptr_t) v;
    v->desc.sr.num_sge      = 1;
    v->desc.sr.sg_list      = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey   = v->region->mem_handle[hca_num]->lkey;
    v->desc.sg_entry.addr   = (uintptr_t)(v->buffer);
#ifdef RDMA_FAST_PATH
    v->padding = NORMAL_VBUF_FLAG;
#endif
    v->subchannel = *subchannel;
}
                                                                                                                                               
void vbuf_init_recv(vbuf *v, unsigned long len,
        const MRAILI_Channel_info * subchannel)
{
    int hca_num = subchannel->hca_index;

    v->desc.rr.next         = NULL;
    v->desc.rr.wr_id        = (uintptr_t) v;
    v->desc.rr.num_sge      = 1;
    v->desc.rr.sg_list      = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey   = v->region->mem_handle[hca_num]->lkey;
    v->desc.sg_entry.addr   = (uintptr_t)(v->buffer);
#ifdef RDMA_FAST_PATH
    v->padding = NORMAL_VBUF_FLAG;
#endif
    v->subchannel = *subchannel;
}

void vbuf_init_rput(vbuf * v, 
                    void *local_address, uint32_t lkey, 
                    void *remote_address, uint32_t rkey, int len, 
                    const MRAILI_Channel_info * subchannel)
{
    v->desc.sr.next         = NULL;
    v->desc.sr.send_flags   = IBV_SEND_SIGNALED;
    v->desc.sr.opcode       = IBV_WR_RDMA_WRITE;
    v->desc.sr.wr_id        = (uintptr_t) v;

    v->desc.sr.num_sge      = 1;
    v->desc.sr.wr.rdma.remote_addr 
                            = (uintptr_t)(remote_address);
    v->desc.sr.wr.rdma.rkey = rkey;

    v->desc.sr.sg_list      = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey   = lkey;
    v->desc.sg_entry.addr   = (uintptr_t)(local_address);
#ifdef RDMA_FAST_PATH
    v->padding = RPUT_VBUF_FLAG;
#endif
    v->subchannel = *subchannel;                                                                                           
    DEBUG_PRINT("RDMA write\n");
}

#ifdef RDMA_FAST_PATH
int MPI_Debug_vbuf_recv(int rank, int index, int len)
{
    MPID_Comm * comm_ptr;
    MPIDI_VC_t * vc;
    vbuf * v;
    char * start;
    int i;
    int align_len;
     
    MPID_Comm_get_ptr (MPI_COMM_WORLD, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rank, &vc);
    
    v = &(vc->mrail.rfp.RDMA_recv_buf[index]);
    MRAILI_ALIGN_LEN(len, align_len);    
 
    start = (char *)((uintptr_t)(&v->head_flag) - align_len);
    fprintf(stderr, "Printing recv buffer (len %d): ", v->head_flag);
    for (i = 0; i < align_len + 4; i ++) {
        fprintf(stderr, "%c", start[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    return 0;
}

int MPI_Debug_vbuf_send(int rank, int index, int len)
{
    MPID_Comm * comm_ptr;
    MPIDI_VC_t * vc;
    vbuf * v;
    char * start;
    int i;
    int align_len;

    MPID_Comm_get_ptr (MPI_COMM_WORLD, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rank, &vc);

    v = &(vc->mrail.rfp.RDMA_send_buf[index]);
    MRAILI_ALIGN_LEN(len, align_len);
    
    start = (char *)((uintptr_t)(&v->head_flag) - align_len);
    fprintf(stderr, "Printing send buffer (len %d): ", v->head_flag);
    for (i = 0; i < align_len + 4; i ++) {
        fprintf(stderr, "%c", start[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    return 0;
}

#endif

/*
 * print out vbuf contents for debugging 
 */

void dump_vbuf(char *msg, vbuf * v)
{
    int i, len;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *header;
    header = v->pheader;
    DEBUG_PRINT("%s: dump of vbuf %p, type = %d\n",
            msg, v, header->type);
    len = 100;
#if defined(RDMA_FAST_PATH)
    DEBUG_PRINT("total_size = %u\n", v->head_flag);
#endif
    for (i = 0; i < len; i++) {
        if (0 == i % 16)
            DEBUG_PRINT("\n  ");
        DEBUG_PRINT("%2x  ", (unsigned int) v->buffer[i]);
    }
    DEBUG_PRINT("\n");
    DEBUG_PRINT("  END OF VBUF DUMP\n");
}
