/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2008, The Ohio State University. All rights
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

#ifndef RDMA_IMPL_H
#define RDMA_IMPL_H

#include "mpidi_ch3_impl.h"
#include "mpidi_ch3_rdma_pre.h"
#include "pmi.h"

#include <infiniband/verbs.h>
#include "ibv_param.h"

#ifdef RDMA_CM
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <pthread.h>
#endif /* RDMA_CM */

#include <errno.h>

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    MPIU_Error_printf("[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    MPIU_Error_printf(args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

/* HCA type */
enum {
    UNKNOWN_HCA, 
    MLX_PCI_EX_SDR, 
    MLX_PCI_EX_DDR, 
    MLX_CX_SDR,
    MLX_CX_DDR,
    MLX_CX_QDR,
    PATH_HT, 
    MLX_PCI_X, 
    IBM_EHCA,
    CHELSIO_T3
};

/* cluster size */
enum {SMALL_CLUSTER, MEDIUM_CLUSTER, LARGE_CLUSTER};

typedef struct MPIDI_CH3I_RDMA_Process_t {
    /* keep all rdma implementation specific global variable in a
       structure like this to avoid name collisions */
    int                         hca_type;
    int                         cluster_size;
    uint8_t                     has_srq;
    uint8_t                     has_hsam;
    uint8_t                     has_apm;
    uint8_t                     has_adaptive_fast_path;
    uint8_t                     has_ring_startup;
    uint8_t                     has_lazy_mem_unregister;
    uint8_t                     has_one_sided;
    int                         maxtransfersize;
    uint8_t                     lmc;

    struct ibv_context          *nic_context[MAX_NUM_HCAS];
    struct ibv_device           *ib_dev[MAX_NUM_HCAS];
    struct ibv_pd               *ptag[MAX_NUM_HCAS];
    struct ibv_cq               *cq_hndl[MAX_NUM_HCAS];
    struct ibv_comp_channel     *comp_channel[MAX_NUM_HCAS];

    /*record lid and port information for connection establish later*/
    int ports[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int lids[MAX_NUM_HCAS][MAX_NUM_PORTS];

    int    (*post_send)(MPIDI_VC_t * vc, vbuf * v, int rail);

    /*information for management of windows */
    struct dreg_entry           *RDMA_local_win_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry           *RDMA_local_wincc_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry           *RDMA_local_actlock_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry           *RDMA_post_flag_dreg_entry[MAX_WIN_NUM];
    struct dreg_entry           *RDMA_assist_thr_ack_entry[MAX_WIN_NUM];

    /* there two variables are used to help keep track of different windows
     * */
    long                        win_index2address[MAX_WIN_NUM];
    int                         current_win_num;

    uint32_t                    pending_r3_sends[MAX_NUM_SUBRAILS];
    struct ibv_srq              *srq_hndl[MAX_NUM_HCAS];
    pthread_spinlock_t          srq_post_spin_lock;
    pthread_mutex_t             srq_post_mutex_lock[MAX_NUM_HCAS];
    pthread_mutex_t             async_mutex_lock[MAX_NUM_HCAS];
    pthread_cond_t              srq_post_cond[MAX_NUM_HCAS];
    uint32_t                    srq_zero_post_counter[MAX_NUM_HCAS];
    pthread_t                   async_thread[MAX_NUM_HCAS];
    uint32_t                    posted_bufs[MAX_NUM_HCAS];
    MPIDI_VC_t                  **vc_mapping;

    /* data structure for ring based startup */
    struct ibv_cq               *boot_cq_hndl;
    struct ibv_qp               *boot_qp_hndl[2];
    int                         boot_tb[2][2];

    int                         polling_group_size;
    MPIDI_VC_t                  **polling_set;

#if defined(RDMA_CM)
    pthread_t                   cmthread;
    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *cm_listen_id;
    sem_t                       rdma_cm;
    uint8_t                     use_rdma_cm;
    uint8_t                     use_iwarp_mode;
    uint8_t                     use_rdma_cm_on_demand;
#endif /* defined(RDMA_CM) */
} MPIDI_CH3I_RDMA_Process_t;

struct process_init_info {
    int         **hostid;
    uint16_t    **lid;
    uint32_t    **qp_num_rdma;
    /* TODO: haven't consider one sided queue pair yet */
    uint32_t    **qp_num_onesided;
    uint32_t    *hca_type;
};

typedef struct ud_addr_info {
    int hostid;
    uint16_t lid;
    uint32_t qpn;
}ud_addr_info_t;

struct MPIDI_PG;

MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
extern struct MPIDI_PG* g_cached_pg;

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define IBV_RETURN_ERR   -3     /* gen2 function return error */
#define IBV_STATUS_ERR   -4     /*  gen2 function status error */

#define ibv_va_error_abort(code, message, args...)  {           \
    int my_rank;                                                \
    PMI_Get_rank(&my_rank);                                     \
    fprintf(stderr, "[%d] Abort: ", my_rank);                   \
    fprintf(stderr, message, ##args);                           \
    fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
            __FILE__);                                          \
    exit(code);                                                 \
}

#define ibv_error_abort(code, message)                          \
{                                                               \
	int my_rank;                                                \
	PMI_Get_rank(&my_rank);                                     \
	fprintf(stderr, "[%d] Abort: ", my_rank);                   \
	fprintf(stderr, message);                                   \
	fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
	    __FILE__);                                              \
	exit(code);                                                 \
}

#define PACKET_SET_RDMA_CREDIT(_p, _c)                          \
{                                                               \
    (_p)->mrail.rdma_credit     = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                            \
    (_p)->mrail.vbuf_credit     = 0;                            \
    (_p)->mrail.remote_credit   = 0;                            \
}

#define PACKET_SET_CREDIT(_p, _c, _rail_index)                  \
{                                                               \
    (_p)->mrail.rdma_credit     = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                            \
    (_p)->mrail.vbuf_credit     =                               \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit;        \
    (_p)->mrail.remote_credit   =                               \
    (_c)->mrail.srp.credits[(_rail_index)].remote_credit;       \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit = 0;    \
}

#define PREPOST_VBUF_RECV(_c, _subrail)  {                      \
    vbuf *__v = get_vbuf();                                     \
    vbuf_init_recv(__v, VBUF_BUFFER_SIZE, _subrail);            \
    IBV_POST_RR(_c, __v, (_subrail));                           \
    (_c)->mrail.srp.credits[(_subrail)].local_credit++;         \
    (_c)->mrail.srp.credits[(_subrail)].preposts++;             \
}

#define  IBV_POST_SR(_v, _c, _rail, err_string) {                     \
    {                                                                 \
        int __ret;                                                    \
        if(((_v)->desc.sg_entry.length <= rdma_max_inline_size)       \
                && ((_v)->desc.u.sr.opcode != IBV_WR_RDMA_READ))      \
        {                                                             \
           (_v)->desc.u.sr.send_flags = (enum ibv_send_flags)         \
                                        (IBV_SEND_SIGNALED |          \
                                         IBV_SEND_INLINE);            \
        } else {                                                      \
            (_v)->desc.u.sr.send_flags = IBV_SEND_SIGNALED ;          \
        }                                                             \
        if ((_rail) != (_v)->rail)                                    \
        {                                                             \
                DEBUG_PRINT(stderr, "[%s:%d] rail %d, vrail %d\n",    \
                        __FILE__, __LINE__,(_rail), (_v)->rail);      \
                MPIU_Assert((_rail) == (_v)->rail);                   \
        }                                                             \
        __ret = ibv_post_send((_c)->mrail.rails[(_rail)].qp_hndl,     \
                  &((_v)->desc.u.sr),&((_v)->desc.y.bad_sr));         \
        if(__ret) {                                                   \
            fprintf(stderr, "failed while avail wqe is %d, "          \
                    "rail %d\n",                                      \
                    (_c)->mrail.rails[(_rail)].send_wqes_avail,       \
                    (_rail));                                         \
            ibv_error_abort(-1, err_string);                          \
        }                                                             \
    }                                                                 \
}

#define IBV_POST_RR(_c,_vbuf,_rail) {                           \
    int __ret;                                                  \
    _vbuf->vc = (void *)_c;                                     \
    __ret = ibv_post_recv(_c->mrail.rails[(_rail)].qp_hndl,     \
                          &((_vbuf)->desc.u.rr),                  \
            &((_vbuf)->desc.y.bad_rr));                           \
    if (__ret) {                                                \
        ibv_va_error_abort(IBV_RETURN_ERR,                      \
            "ibv_post_recv err with %d",          \
                __ret);                                         \
    }                                                           \
}

#define BACKLOG_ENQUEUE(q,v) {                      \
    v->desc.next = NULL;                            \
    if (q->vbuf_tail == NULL) {                     \
         q->vbuf_head = v;                          \
    } else {                                        \
         q->vbuf_tail->desc.next = v;               \
    }                                               \
    q->vbuf_tail = v;                               \
    q->len++;                                       \
}

#define BACKLOG_DEQUEUE(q,v)  {                     \
    v = q->vbuf_head;                               \
    q->vbuf_head = v->desc.next;                    \
    if (v == q->vbuf_tail) {                        \
        q->vbuf_tail = NULL;                        \
    }                                               \
    q->len--;                                       \
    v->desc.next = NULL;                            \
}

#define CHECK_UNEXP(ret, s)                           \
do {                                                  \
    if (ret) {                                        \
        fprintf(stderr, "[%s:%d]: %s\n",              \
                __FILE__,__LINE__, s);                \
    exit(1);                                          \
    }                                                 \
} while (0)

#define CHECK_RETURN(ret, s)                            \
do {                                                    \
    if (ret) {                                          \
    fprintf(stderr, "[%s:%d] error(%d): %s\n",          \
        __FILE__,__LINE__, ret, s);                     \
    exit(1);                                            \
    }                                                   \
}                                                       \
while (0)

#ifdef CKPT
#define MSG_LOG_ENQUEUE(vc, entry) { \
    entry->next = NULL; \
    if (vc->mrail.msg_log_queue_tail!=NULL) { \
        vc->mrail.msg_log_queue_tail->next = entry; \
    } \
    vc->mrail.msg_log_queue_tail = entry; \
    if (vc->mrail.msg_log_queue_head==NULL) { \
        vc->mrail.msg_log_queue_head = entry; \
    }\
}

#define MSG_LOG_DEQUEUE(vc, entry) { \
    entry = vc->mrail.msg_log_queue_head; \
    if (vc->mrail.msg_log_queue_head!=NULL) {\
        vc->mrail.msg_log_queue_head = vc->mrail.msg_log_queue_head->next; \
    }\
    if (entry == vc->mrail.msg_log_queue_tail) { \
        vc->mrail.msg_log_queue_tail = NULL; \
    }\
}

#define MSG_LOG_QUEUE_TAIL(vc) (vc->mrail.msg_log_queue_tail)

#define MSG_LOG_EMPTY(vc) (vc->mrail.msg_log_queue_head == NULL)

void MRAILI_Init_vc_network(MPIDI_VC_t * vc);

#endif

#define INVAL_HNDL (0xffffffff)

#define SIGNAL_FOR_PUT        (1)
#define SIGNAL_FOR_GET        (2)
#define SIGNAL_FOR_LOCK_ACT   (3)
#define SIGNAL_FOR_DECR_CC    (4)

/* Prototype for ring based startup */
void rdma_ring_boot_exchange(struct MPIDI_CH3I_RDMA_Process_t *proc,
                        int pg_rank, int pg_size, struct process_init_info *);
int rdma_setup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *, int pg_rank, int pg_size);
int rdma_cleanup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *proc);
void rdma_ring_based_allgather(void *sbuf, int data_size,
        int proc_rank, void *rbuf, int job_size,
        struct MPIDI_CH3I_RDMA_Process_t *proc);

/* Other prototype */
struct process_init_info *alloc_process_init_info(int pg_size, int num_rails);
void free_process_init_info(struct process_init_info *, int pg_size);
struct ibv_mr * register_memory(void *, int len, int hca_num);
int deregister_memory(struct ibv_mr * mr);
int MRAILI_Backlog_send(MPIDI_VC_t * vc, int subrail);
int rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc);
int  rdma_get_control_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc);
void  rdma_set_default_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc);
void rdma_get_user_parameters(int num_proc, int me);
int rdma_iba_hca_init_noqp(struct MPIDI_CH3I_RDMA_Process_t *proc,
              int pg_rank, int pg_size);
int rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
              int pg_rank, int pg_size, struct process_init_info *);
int rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                 int pg_rank, int pg_size);
int rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                int pg_rank, int pg_size, struct process_init_info *);
void rdma_param_handle_heterogenity(uint32_t hca_type[], int pg_size);
int MRAILI_Process_send(void *vbuf_addr);
int post_send(MPIDI_VC_t *vc, vbuf *v, int rail);
int post_srq_send(MPIDI_VC_t *vc, vbuf *v, int rail);
int MRAILI_Fill_start_buffer(vbuf *v, MPID_IOV *iov, int n_iov);
int MPIDI_CH3I_MRAILI_Recv_addr(MPIDI_VC_t * vc, void *vstart);
void MRAILI_RDMA_Put(MPIDI_VC_t * vc, vbuf *v,
                     char * local_addr, uint32_t lkey,
                     char * remote_addr, uint32_t rkey,
                     int nbytes, int subrail);
void MRAILI_RDMA_Get(MPIDI_VC_t * vc, vbuf *v,
                     char * local_addr, uint32_t lkey,
                     char * remote_addr, uint32_t rkey,
                     int nbytes, int subrail);
int MRAILI_Send_select_rail(MPIDI_VC_t * vc);
void vbuf_address_send(MPIDI_VC_t *vc);
void vbuf_fast_rdma_alloc (struct MPIDI_VC *, int dir);
int MPIDI_CH3I_MRAILI_rput_complete(MPIDI_VC_t *, MPID_IOV *,
                                    int, int *num_bytes_ptr, 
                                    vbuf **, int rail);
int MPIDI_CH3I_MRAILI_rget_finish(MPIDI_VC_t *, MPID_IOV *,
                                    int, int *num_bytes_ptr, 
                                    vbuf **, int rail);
int MRAILI_Handle_one_sided_completions(vbuf * v);                            
int MRAILI_Flush_wqe(MPIDI_VC_t *vc, vbuf *v , int rail);
struct ibv_srq *create_srq(struct MPIDI_CH3I_RDMA_Process_t *proc,
				  int hca_num);

/*function to create qps for the connection and move them to INIT state*/
int cm_qp_create(MPIDI_VC_t *vc);

/*function to move qps to rtr and prepost buffers*/
int cm_qp_move_to_rtr(MPIDI_VC_t *vc, uint16_t *lids, uint32_t *qpns);

/*function to move qps to rts and mark the connection available*/
int cm_qp_move_to_rts(MPIDI_VC_t *vc);

int get_pkey_index(uint16_t pkey, int hca_num, int port_num, uint16_t* index);
void set_pkey_index(uint16_t * pkey_index, int hca_num, int port_num);

void init_apm_lock();

void MRAILI_RDMA_Get_finish(MPIDI_VC_t * vc, 
        MPID_Request * rreq, int rail);
        
int reload_alternate_path(struct ibv_qp *qp);

int power_two(int x);

#endif                          /* RDMA_IMPL_H */
