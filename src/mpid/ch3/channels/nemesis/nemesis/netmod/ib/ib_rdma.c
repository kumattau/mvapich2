/* Copyright (c) 2003-2010, The Ohio State University. All rights
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

#include "pmi.h"

#include <ib_rdma.h>
#include <ib_errors.h>
#include <ib_process.h>

struct ibv_mr * register_memory(void * buf, int len, int hca_num)
{
    struct ibv_mr * mr = ibv_reg_mr(hca_list[hca_num].ptag, buf, len,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ );
    DEBUG_PRINT("register return mr %p, buf %p, len %d\n", mr, buf, len);
    return mr;
}

int deregister_memory(struct ibv_mr * mr)
{
    int ret;

    ret = ibv_dereg_mr(mr);
    DEBUG_PRINT("deregister mr %p, ret %d\n", mr, ret);
    return ret;
}



/*vbuf_fast_rdma_alloc in iba_priv.c*/
void vbuf_fast_rdma_alloc (MPIDI_VC_t * c, int dir)
{
    vbuf * v;
    int vbuf_alignment = 64;
    int pagesize = getpagesize();
    int i;
    struct ibv_mr *mem_handle[MAX_NUM_HCAS];

    void *vbuf_ctrl_buf = NULL;
    void *vbuf_rdma_buf = NULL;

    /* initialize revelant fields */
    VC_FIELD(c, connection)->rfp.rdma_credit = 0;

    if (num_rdma_buffer) {

#ifdef USE_MEMORY_TRACING
        vbuf_ctrl_buf = MPIU_Malloc(sizeof(struct vbuf) * num_rdma_buffer);
#else
    /* allocate vbuf struct buffers */
        if(posix_memalign((void **) &vbuf_ctrl_buf, vbuf_alignment,
            sizeof(struct vbuf) * num_rdma_buffer)) {
            ibv_error_abort(GEN_EXIT_ERR,
                    "malloc: vbuf in vbuf_fast_rdma_alloc");
        }
#endif /* USE_MEMORY_TRACING */

        memset(vbuf_ctrl_buf, 0,
                sizeof(struct vbuf) * num_rdma_buffer);

#ifdef USE_MEMORY_TRACING
         vbuf_rdma_buf = MPIU_Malloc(rdma_vbuf_total_size * num_rdma_buffer);
#else
        /* allocate vbuf RDMA buffers */
        if(posix_memalign((void **)&vbuf_rdma_buf, pagesize,
            rdma_vbuf_total_size * num_rdma_buffer)) {
            ibv_error_abort(GEN_EXIT_ERR,
                "malloc: vbuf DMA in vbuf_fast_rdma_alloc");
        }
#endif /* USE_MEMORY_TRACING */

        memset(vbuf_rdma_buf, 0, rdma_vbuf_total_size * num_rdma_buffer);

        /* REGISTER RDMA SEND BUFFERS */
        for ( i = 0 ; i < ib_hca_num_hcas; i ++ ) {
            mem_handle[i] =  register_memory(vbuf_rdma_buf,
                                rdma_vbuf_total_size * num_rdma_buffer, i);
            if (!mem_handle[i]) {
                ibv_va_error_abort(GEN_EXIT_ERR,
                        "fail to register rdma memory, size %d\n",
                        rdma_vbuf_total_size * num_rdma_buffer);
            }
        }

        /* Connect the DMA buffer to the vbufs */
        for (i = 0; i < num_rdma_buffer; i++) {
            v = ((vbuf *)vbuf_ctrl_buf) + i;
            v->head_flag = (VBUF_FLAG_TYPE *) ( (char *)(vbuf_rdma_buf) + (i + 1) *
                          rdma_vbuf_total_size - sizeof *v->head_flag);
            v->buffer = (unsigned char *) ( (char *)(vbuf_rdma_buf) + (i *
                           rdma_vbuf_total_size) );
            v->vc     = c;
        }

        /* Some vbuf initialization */
        for (i = 0; i < num_rdma_buffer; i++) {
            if (dir==0) {
                ((vbuf *)vbuf_ctrl_buf + i)->desc.next = NULL;
                ((vbuf *)vbuf_ctrl_buf + i)->padding = FREE_FLAG;
            } else {
                ((vbuf *)vbuf_ctrl_buf + i)->padding = BUSY_FLAG;
            }
        }

        DEBUG_PRINT("[remote-rank %d][dir=%d]"
                "rdma buffer %p, lkey %08x, rkey %08x\n",
                c->pg_rank, dir, vbuf_rdma_buf, mem_handle[0]->lkey,
        mem_handle[0]->rkey);
        if (dir==0) {
            VC_FIELD(c, connection)->rfp.RDMA_send_buf       = vbuf_ctrl_buf;
            VC_FIELD(c, connection)->rfp.RDMA_send_buf_DMA   = vbuf_rdma_buf;
            for (i = 0; i < ib_hca_num_hcas; i++)
                VC_FIELD(c, connection)->rfp.RDMA_send_buf_mr[i] = mem_handle[i];
            /* set pointers */
            VC_FIELD(c, connection)->rfp.phead_RDMA_send = 0;
            VC_FIELD(c, connection)->rfp.ptail_RDMA_send = num_rdma_buffer - 1;
        } else {
            VC_FIELD(c, connection)->rfp.RDMA_recv_buf       = vbuf_ctrl_buf;
            VC_FIELD(c, connection)->rfp.RDMA_recv_buf_DMA   = vbuf_rdma_buf;
            for (i = 0; i < ib_hca_num_hcas; i++)
                VC_FIELD(c, connection)->rfp.RDMA_recv_buf_mr[i] = mem_handle[i];
            /* set pointers */
            VC_FIELD(c, connection)->rfp.p_RDMA_recv = 0;
            VC_FIELD(c, connection)->rfp.p_RDMA_recv_tail = num_rdma_buffer - 1;

            /* Add the connection to the RDMA polling list */
            process_info.polling_set
              [process_info.polling_group_size] = c;
            process_info.polling_group_size++;

            VC_FIELD(c, cmanager)->num_channels      += 1;
            VC_FIELD(c, cmanager)->num_local_pollings = 1;
            VC_FIELD(c, connection)->rfp.in_polling_set          = 1;
        }

    }
}


