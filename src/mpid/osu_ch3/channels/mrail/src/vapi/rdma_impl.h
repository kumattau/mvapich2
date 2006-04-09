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

#ifndef RDMA_IMPL_H
#define RDMA_IMPL_H

#include "vapi_param.h"
#include "vapi_header.h"
#include "mpidi_ch3_impl.h"
#include "vapi_priv.h"
#include "mpidi_ch3_rdma_pre.h"

typedef struct MPIDI_CH3I_RDMA_Process_t {
    /* keep all rdma implementation specific global variable in a
       structure like this to avoid name collisions */
    int num_hcas;

    int maxtransfersize;

    VAPI_hca_hndl_t nic[MAX_NUM_HCAS];
    /* NICs */
    VAPI_hca_port_t hca_port[MAX_SUBCHANNELS];
    /* Port numbers */
    VAPI_pd_hndl_t ptag[MAX_NUM_HCAS];
    /* single protection tag for all memory registration ??? */
    /* VAPI_qp_hndl_t qp_hndl[MAX_SUBCHANNELS]; */
    /* Array of QP handles for all connections */
    /* VAPI_qp_prop_t qp_prop[MAX_SUBCHANNELS]; */
    /* TO DO:Wil have qpnum. We cud just store qp_num instead of entire pros structure
     * */
    VAPI_cq_hndl_t cq_hndl[MAX_NUM_HCAS];
    /* one cq for both send and recv */

#ifdef USE_INLINE
    int inline_size[MAX_SUBCHANNELS];
#endif

#ifdef ONE_SIDED
    /* information for the one-sided communication connection */
    VAPI_cq_hndl_t cq_hndl_1sc;
    int inline_size_1sc;

    /*information for management of windows */
    dreg_entry *RDMA_local_win_dreg_entry[MAX_WIN_NUM];
    dreg_entry *RDMA_local_wincc_dreg_entry[MAX_WIN_NUM];
    dreg_entry *RDMA_local_actlock_dreg_entry[MAX_WIN_NUM];
    dreg_entry *RDMA_post_flag_dreg_entry[MAX_WIN_NUM];
    dreg_entry *RDMA_assist_thr_ack_entry[MAX_WIN_NUM];

    /* there two variables are used to help keep track of different windows
     * */
    long win_index2address[MAX_WIN_NUM];
    int current_win_num;
#endif

#ifdef USE_MPD_RING
    VAPI_qp_hndl_t boot_qp_hndl[2];   /* qp's for bootstrapping */
    VAPI_qp_prop_t boot_qp_prop[2];   /* qp_prop's for bootstrapping */
    VAPI_cq_hndl_t boot_cq_hndl;      /* one cq for bootstrapping */
    int boot_tb[2][2];                /* HCA LID and QP for the ring */
    VIP_MEM_HANDLE boot_mem_hndl;     /* memory handler for the bootstrap*/
    char * boot_mem;
#endif
} MPIDI_CH3I_RDMA_Process_t;

extern MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
extern rdma_iba_addr_tb_t rdma_iba_addr_table;
extern MPIDI_PG_t *cached_pg;

int MRAILI_Backlog_send(MPIDI_VC_t * vc,
                        const MRAILI_Channel_info * channel);

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  MPIDI_VC_t * vc, int pg_rank, int pg_size);

int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         MPIDI_VC_t * vc, int pg_rank, int pg_size);

#ifdef USE_MPD_RING
int
rdma_iba_exchange_info(struct MPIDI_CH3I_RDMA_Process_t *proc,
                       MPIDI_VC_t * vc, int pg_rank, int pg_size);
#endif 
int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            MPIDI_VC_t * vc, int pg_rank, int pg_size);

int MRAILI_Process_send(void *vbuf_addr);

void MRAILI_RDMA_Put(   MPIDI_VC_t * vc, vbuf *v,
                        char * local_addr, VIP_MEM_HANDLE local_hndl,
                        char * remote_addr, VIP_MEM_HANDLE remote_hndl,
                        int nbytes, MRAILI_Channel_info * subchannel
                    );

#endif                          /* RDMA_IMPL_H */


