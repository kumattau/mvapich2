/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
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

#ifndef RDMA_IMPL_H
#define RDMA_IMPL_H

#include "mpidi_ch3_impl.h"
#include "mpidi_ch3_rdma_pre.h"

#include "infiniband/verbs.h"
#include "ibv_param.h"


typedef struct MPIDI_CH3I_RDMA_Process_t {
    /* keep all rdma implementation specific global variable in a
       structure like this to avoid name collisions */
    int maxtransfersize;

    struct ibv_context *nic_context[MAX_NUM_HCAS];
    /* Port numbers */
    struct ibv_pd * ptag[MAX_NUM_HCAS];
    struct ibv_cq * cq_hndl[MAX_NUM_HCAS];

    /* port and device attributes for sanity check */
    struct ibv_device_attr dev_attr;
    struct ibv_port_attr port_attr;
    /* one cq for both send and recv */
#ifdef ONE_SIDED
    /* information for the one-sided communication connection */
    struct ibv_cq * cq_hndl_1sc[MAX_NUM_HCAS];
    int inline_size_1sc;

    /*information for management of windows */
    struct dreg_entry *RDMA_local_win_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry *RDMA_local_wincc_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry *RDMA_local_actlock_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry *RDMA_post_flag_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry *RDMA_assist_thr_ack_entry[MAX_WIN_NUM];

    /* there two variables are used to help keep track of different windows
     * */
    long win_index2address[MAX_WIN_NUM];
    int current_win_num;
#endif


#ifdef SRQ
    uint32_t pending_r3_sends[MAX_NUM_SUBRAILS];
    struct ibv_srq      *srq_hndl[MAX_NUM_HCAS];
    pthread_spinlock_t  srq_post_lock;
    pthread_t           async_thread[MAX_NUM_HCAS];
    uint32_t            posted_bufs[MAX_NUM_HCAS];
    MPIDI_VC_t **vc_mapping;
#endif

#ifdef USE_MPD_RING
    struct ibv_cq * boot_cq_hndl;
    struct ibv_qp * boot_qp_hndl[2];
    int boot_tb[2][2];                /* HCA LID and QP for the ring */
    struct ibv_mr * boot_mem_hndl;
    char * boot_mem;
#endif

#ifdef ADAPTIVE_RDMA_FAST_PATH
    int  polling_group_size;
    MPIDI_VC_t **polling_set;
#endif

} MPIDI_CH3I_RDMA_Process_t;

struct rdma_iba_addr_tb;
struct MPIDI_PG;

extern MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
extern struct MPIDI_PG *cached_pg;


#endif                          /* RDMA_IMPL_H */
