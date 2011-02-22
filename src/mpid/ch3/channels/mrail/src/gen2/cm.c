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

#include "mpidi_ch3i_rdma_conf.h"
#include <mpimem.h>
#include <errno.h>
#include <string.h>
#include "cm.h"
#include "rdma_cm.h"
#include "mpiutil.h"

#define CM_MSG_TYPE_REQ     0
#define CM_MSG_TYPE_REP     1
#define CM_MSG_TYPE_RAW_REQ 2
#define CM_MSG_TYPE_RAW_REP 3
#ifdef _ENABLE_XRC_
#define CM_MSG_TYPE_XRC_REQ 4
#define CM_MSG_TYPE_XRC_REP 5
#endif

#ifdef _ENABLE_XRC_
#define CM_ATTS             120
#define VC_SET_ACTIVE(vc) do {                              \
    if (USE_XRC) {                                          \
        if (vc->state != MPIDI_VC_STATE_REMOTE_CLOSE &&     \
                vc->state != MPIDI_VC_STATE_LOCAL_CLOSE &&  \
                vc->state != MPIDI_VC_STATE_CLOSE_ACKED)    \
            vc->state = MPIDI_VC_STATE_ACTIVE;              \
    }                                                       \
    else                                                    \
            vc->state = MPIDI_VC_STATE_ACTIVE;              \
} while (0);
#else
#define VC_SET_ACTIVE(vc) vc->state = MPIDI_VC_STATE_ACTIVE;
#endif

#if defined(CKPT)
#define CM_MSG_TYPE_REACTIVATE_REQ   10
#define CM_MSG_TYPE_REACTIVATE_REP   11
#endif /* defined(CKPT) */

#define CM_MSG_TYPE_FIN_SELF  99

typedef struct cm_msg {
    uint32_t req_id;
    uint32_t server_rank;
    uint32_t client_rank;
    uint8_t  msg_type;
    uint8_t  nrails;
    uint16_t lids[MAX_NUM_SUBRAILS];
    union ibv_gid gids[MAX_NUM_SUBRAILS];
    uint32_t qpns[MAX_NUM_SUBRAILS];
    uint64_t vc_addr;
    uint64_t vc_addr_bounce; /* for dpm, bounce vc_addr back */
#ifdef _ENABLE_XRC_
    uint32_t    xrc_srqn[MAX_NUM_HCAS];
    uint32_t    xrc_rqpn[MAX_NUM_SUBRAILS];
#endif
    char     pg_id[MAX_PG_ID_SIZE];
    char     ifname[128];
} cm_msg;


#define DEFAULT_CM_MSG_RECV_BUFFER_SIZE   1024
#define DEFAULT_CM_SEND_DEPTH             10
#define DEFAULT_CM_MAX_SPIN_COUNT         5000   
#define DEFAULT_CM_THREAD_STACKSIZE   (1024*1024)

/*In microseconds*/
#define CM_DEFAULT_TIMEOUT      500000
#define CM_MIN_TIMEOUT           20000

#define CM_UD_DEFAULT_PSN   0

#define CM_UD_SEND_WR_ID  11
#define CM_UD_RECV_WR_ID  13

static int cm_send_depth;
static int cm_recv_buffer_size;
static int cm_ud_psn;
static int cm_req_id_global;
static int cm_max_spin_count;
static int cm_is_finalizing;
static pthread_t cm_comp_thread, cm_timer_thread;
static pthread_cond_t cm_cond_new_pending;
pthread_mutex_t cm_conn_state_lock;
struct timespec cm_timeout;
long cm_timeout_usec;
size_t cm_thread_stacksize;

struct ibv_comp_channel *cm_ud_comp_ch;
struct ibv_qp *cm_ud_qp;
struct ibv_cq *cm_ud_recv_cq;
struct ibv_cq *cm_ud_send_cq;
struct ibv_mr *cm_ud_mr;
void *cm_ud_buf;
void *cm_ud_send_buf;           /*length is set to 1 */
void *cm_ud_recv_buf;
int cm_ud_recv_buf_index;
int page_size;

extern int *rdma_cm_host_list;

#define CM_ERR_ABORT(args...) do {                                           \
    int _rank; PMI_Get_rank(&_rank);  \
    fprintf(stderr, "[Rank %d][%s: line %d]", _rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                   \
    fprintf(stderr, "\n");                                                   \
    fflush(stderr);                                                          \
    exit(-1);                                                                \
}while (0)

#if defined(CM_DEBUG)
#define CM_DBG(args...)  do {                                                \
    int _rank; PMI_Get_rank(&_rank);                                         \
    fprintf(stderr, "[Rank %d][%s: line %d]", _rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                   \
    fprintf(stderr, "\n");                                                   \
    fflush(stderr);                                                          \
}while (0)
#else /* defined(CM_DEBUG) */
#define CM_DBG(args...)
#endif /* defined(CM_DEBUG) */

typedef struct cm_packet {
    struct timeval timestamp;        /*the time when timer begins */
    cm_msg payload;
} cm_packet;

typedef struct cm_pending {
    int cli_or_srv;             /*pending as a client or server */
    int has_pg;
    cm_packet *packet;
    union {
        struct {
            int peer;
            MPIDI_PG_t *pg;
        } pg;
        struct {
            void *tag; /* raw vc will not be backed up by a pg */
            struct ibv_ah *ah;
            uint32_t qpn;
        } nopg;
    } data; 
#ifdef _ENABLE_XRC_
    int attempts;
#endif
    struct cm_pending *next;
    struct cm_pending *prev;
} cm_pending;

int cm_pending_num;

#define CM_PENDING_SERVER   0
#define CM_PENDING_CLIENT   1

cm_pending *cm_pending_head = NULL;

/*Interface to lock/unlock connection manager*/
void MPICM_lock(void)
{
  pthread_mutex_lock(&cm_conn_state_lock);
}

void MPICM_unlock(void)
{
  pthread_mutex_unlock(&cm_conn_state_lock);
}

#ifdef _ENABLE_XRC_
xrc_hash_t *xrc_hash[XRC_HASH_SIZE];

int compute_xrc_hash (uint32_t v)
{
    uint8_t *p = (uint8_t *)  &v;
    return ((p[0] ^ p[1] ^ p[2] ^ p[3])  & XRC_HASH_MASK);
}

void clear_xrc_hash (void)
{
    int i;
    xrc_hash_t *iter, *next;
    for (i = 0; i < XRC_HASH_SIZE; i ++) {
        iter = xrc_hash[i];
        while (iter) {
            next = iter->next;
            MPIU_Free (iter);
            iter = next;
        }
    }
}
void remove_vc_xrc_hash (MPIDI_VC_t *vc)
{
    int hash;
    xrc_hash_t *iter, *tmp;

    MPIU_Assert (VC_XST_ISUNSET (vc, XF_SMP_VC) && 
            VC_XST_ISUNSET (vc, XF_INDIRECT_CONN));
    hash = compute_xrc_hash (vc->smp.hostid);
    iter = xrc_hash[hash];

    if (!iter) return;

    if (iter->vc == vc) {
        xrc_hash[hash] = iter->next;
        MPIU_Free (iter);
    }
    else {
        while (iter->next) {
            if (iter->next->vc == vc) {
                tmp = iter->next;
                iter->next = iter->next->next;
                MPIU_Free (tmp);
                XRC_MSG ("Removed vc from hash");
                return;
            }
            iter = iter->next;
        }
        fprintf( stderr, "[%s %d] Error vc not found.\n", __FILE__, __LINE__ );
        exit(EXIT_FAILURE);
        /* MPIU_Assert (0); */
    }
    MPIU_Assert (iter != NULL);
    return;
}

void add_vc_xrc_hash (MPIDI_VC_t *vc)
{
    int hash = compute_xrc_hash (vc->smp.hostid);

    xrc_hash_t *iter, *node = (xrc_hash_t *) MPIU_Malloc (xrc_hash_s);
    memset (node, 0, xrc_hash_s);
    node->vc = vc;
    node->xrc_qp_dst = vc->pg_rank;

    if (NULL == xrc_hash[hash]) {
        xrc_hash[hash] = node;
        return;
    }

    iter = xrc_hash[hash];
    
    while (iter->next != NULL) {
        iter = iter->next;
    }
    iter->next = node;
}
#endif /* _ENABLE_XRC_ */


#ifdef CKPT
// At resume phase, each vc sends out REM_UPDATE(if any), followed by REACT_DONE. 
// This func is to guarantee that, 
//REACT_DONE is behind REM_UPDATE in the msg_log_q for each vc. 
// NOTE: "entry" is REACT_DONE 
static inline int cm_enq_react_done(MPIDI_VC_t* vc,
	MPIDI_CH3I_CR_msg_log_queue_entry_t* entry )
{
       int ret;
       pthread_spin_lock( &vc->mrail.cr_lock);
       if( vc->mrail.react_send_ready )
       {       //can safely enq the msg
	       MSG_LOG_ENQUEUE(vc, entry);
           vc->mrail.react_entry = NULL;
           ret = 0;
           CM_DBG("%s: [%d => %d]: enq REACT_DONE\n", __func__,
                 MPIDI_Process.my_pg_rank, vc->pg_rank );
       }
       else
       {       // may need to enq some local REM_UPDATE msg before this 
               // REACT_DONE, so store it temporarily
           vc->mrail.react_entry = entry;
           ret = 1;
           CM_DBG("%s: [%d => %d]: save REACT_DONE to be enq later...\n",__func__,
                MPIDI_Process.my_pg_rank, vc->pg_rank );
       }
       pthread_spin_unlock( &vc->mrail.cr_lock);
       return ret;
}
#endif

/*
 * TODO add error checking
 */
static inline struct ibv_ah *cm_create_ah(struct ibv_pd *pd, uint32_t lid, 
                                          int port)
{
    struct ibv_ah_attr ah_attr;

    MPIU_Memset(&ah_attr, 0, sizeof(ah_attr));
    ah_attr.is_global = 0;
    ah_attr.dlid = lid;
    ah_attr.sl = 0;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num = port;

    return ibv_create_ah(pd, &ah_attr);
}

/* 
 * Supporting function to handle cm pending requests
 */
static cm_pending *cm_pending_create(void)
{
    cm_pending *temp = (cm_pending *) MPIU_Malloc(sizeof(cm_pending));
    MPIU_Memset(temp, 0, sizeof(cm_pending));
    return temp;
}

static int cm_pending_init(cm_pending * pending, MPIDI_PG_t *pg, cm_msg * msg,
                           void *tag)
{
    switch(msg->msg_type) {
    case CM_MSG_TYPE_REQ:
#ifdef _ENABLE_XRC_
    case CM_MSG_TYPE_XRC_REQ:
#endif /* _ENABLE_XRC_ */
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->data.pg.peer = msg->server_rank;
        break;
    case CM_MSG_TYPE_REP:
#ifdef _ENABLE_XRC_
    case CM_MSG_TYPE_XRC_REP:
#endif /* _ENABLE_XRC_ */
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->data.pg.peer = msg->client_rank;
        break;
    case CM_MSG_TYPE_RAW_REQ:
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->data.nopg.tag = tag;
        break;
    case CM_MSG_TYPE_RAW_REP:
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->data.nopg.tag = tag;
        break;
#if defined(CKPT)
    case CM_MSG_TYPE_REACTIVATE_REQ:
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->data.pg.peer = msg->server_rank;
        break;
    case CM_MSG_TYPE_REACTIVATE_REP:
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->data.pg.peer = msg->client_rank;
        break;
#endif /* defined(CKPT) */    
    default:
        CM_ERR_ABORT("error message type");
    }

    if (pg) {
        pending->has_pg = 1;
        pending->data.pg.pg     = pg;
    } else {
        pending->has_pg = 0;
    }

    pending->packet = (cm_packet *) MPIU_Malloc(sizeof(cm_packet));
    MPIU_Memcpy(&(pending->packet->payload), msg, sizeof(cm_msg));
#ifdef _ENABLE_XRC_
    pending->attempts = 0;
#endif
    
    return MPI_SUCCESS;
}

static cm_pending *cm_pending_search_peer(MPIDI_PG_t *pg, int peer, 
                                          int cli_or_srv, void *tag)
{
    cm_pending *pending = cm_pending_head;
    while (pending->next != cm_pending_head)
    {
        pending = pending->next;
        if (pending->has_pg && pending->data.pg.pg == pg && 
            pending->cli_or_srv == cli_or_srv && pending->data.pg.peer == peer) {
            return pending;
        } else if (!pending->has_pg && (uintptr_t)pending->data.nopg.tag == (uintptr_t)tag) {
            CM_DBG("Found pending, return pending\n");
            return pending;
        }
    }
    return NULL;
}

static inline int cm_pending_append(cm_pending * node)
{
    cm_pending *last;
    if (!cm_pending_head) return MPI_SUCCESS;
    last = cm_pending_head->prev;
    last->next = node;
    node->next = cm_pending_head;
    cm_pending_head->prev = node;
    node->prev = last;
    ++cm_pending_num;
    return MPI_SUCCESS;
}

static inline int cm_pending_remove_and_destroy(cm_pending * node)
{
    MPIU_Free(node->packet);
    node->next->prev = node->prev;
    node->prev->next = node->next;
    if (node->data.nopg.ah && node->has_pg == 0)
        ibv_destroy_ah(node->data.nopg.ah);
    MPIU_Free(node);
    --cm_pending_num;
    return MPI_SUCCESS;
}

/*
 * TODO add error checking
 */
static int cm_pending_list_init(void)
{
    cm_pending_num = 0;
    cm_pending_head = cm_pending_create();
    cm_pending_head->data.pg.peer = -1;
    cm_pending_head->prev = cm_pending_head;
    cm_pending_head->next = cm_pending_head;
    return MPI_SUCCESS;
}

static int cm_pending_list_finalize(void)
{
    while (cm_pending_head->next != cm_pending_head)
    {
        cm_pending_remove_and_destroy(cm_pending_head->next);
    }
    MPIU_Assert(cm_pending_num == 0);
    MPIU_Free(cm_pending_head);
    cm_pending_head = NULL;
    return MPI_SUCCESS;
}

/* Return 1 if my order is larger
         -1 if my order is smaller 
          0 if r_pg,r_rank and my_pg,my_rank are the same process */
static int cm_compare_peer(MPIDI_PG_t *r_pg, MPIDI_PG_t *my_pg,
                           int r_rank, int my_rank)
{
    int order = strcmp(r_pg->id, my_pg->id);

    if (order == 0) {
        if (r_rank < my_rank)
            order = -1;
        else if (r_rank == my_rank)
            order = 0;
        else
            order = 1;
    }

    return order;
}

/* 
 * Supporting functions to send ud packets 
 */
#undef FUNCNAME
#define FUNCNAME cm_post_ud_recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int cm_post_ud_recv(void *buf, int size)
{
    struct ibv_sge list;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr *bad_wr;

    MPIU_Memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) buf;
    list.length = size + 40;
    list.lkey = cm_ud_mr->lkey;
    MPIU_Memset(&wr, 0, sizeof(struct ibv_recv_wr));
    wr.next = NULL;
    wr.wr_id = CM_UD_RECV_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;

    return ibv_post_recv(cm_ud_qp, &wr, &bad_wr);
}

static int __cm_post_ud_packet(cm_msg * msg, struct ibv_ah *ah, uint32_t qpn)
{
    struct ibv_sge list;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    struct ibv_wc wc;
    int ne;

    CM_DBG("cm_post_ud_packet, post message type %d", msg->msg_type);

    MPIU_Memcpy((char*)cm_ud_send_buf + 40, msg, sizeof(cm_msg));
    MPIU_Memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) cm_ud_send_buf + 40;
    list.length = sizeof(cm_msg);
    list.lkey = cm_ud_mr->lkey;

    MPIU_Memset(&wr, 0, sizeof(struct ibv_send_wr));
    wr.wr_id = CM_UD_SEND_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    wr.wr.ud.ah = ah;
    wr.wr.ud.remote_qpn = qpn;
    wr.wr.ud.remote_qkey = 0;

    CM_DBG("Post with nrails %d\n", msg->nrails);
    if (ibv_post_send(cm_ud_qp, &wr, &bad_wr))
    {
        CM_ERR_ABORT("ibv_post_send to ud qp failed");
    }

    /* poll for completion */
    while (1)
    {
        ne = ibv_poll_cq(cm_ud_send_cq, 1, &wc);
        if (ne < 0)
        {
            CM_ERR_ABORT("poll CQ failed %d", ne);
        }
        else if (ne == 0)
        {
            continue;
        }

        if (wc.status != IBV_WC_SUCCESS)
        {
            CM_ERR_ABORT("Failed status %d for wr_id %d",
                    wc.status, (int) wc.wr_id);
        }

        if (wc.wr_id == CM_UD_SEND_WR_ID)
        {
            break;
        }
        else
        {
            CM_ERR_ABORT("unexpected completion, wr_id: %d",
                    (int) wc.wr_id);
        }
    }

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME cm_resolve_conn_info
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_resolve_conn_info(MPIDI_PG_t *pg, int peer)
{
    int mpi_errno = MPI_SUCCESS;
    uint32_t rank, lid, qpn, port; 
#ifdef _ENABLE_XRC_
    uint32_t hostid;
#endif
    struct ibv_ah *ah;
    char string[128];

    if (!pg->connData) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s", "No connection info available");
    }

    mpi_errno = MPIDI_PG_GetConnString(pg, peer, string, 128);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

    CM_DBG(stderr, "Peer %d, connString %s\n", peer, string);
#ifdef _ENABLE_XRC_
    sscanf(string, "#RANK:%08d(%08x:%08x:%08x:%08x)#",
            &rank, &lid, &qpn, &port, &hostid);
#else
    sscanf(string, "#RANK:%08d(%08x:%08x:%08x)#",
            &rank, &lid, &qpn, &port);
#endif
    ah = cm_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], lid, port);
    if (!ah) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s", "Cannot create address handle");
    }

    pg->ch.mrail.cm_ah[peer]     = ah;
    pg->ch.mrail.cm_ud_qpn[peer] = qpn;
    pg->ch.mrail.cm_lid[peer]    = lid;
#ifdef _ENABLE_XRC_
    pg->ch.mrail.xrc_hostid[peer] = hostid;
#endif
fn_fail:
    return mpi_errno;
}

static int cm_post_ud_packet(MPIDI_PG_t *pg, cm_msg * msg)
{
    int peer;
    int err;

    switch(msg->msg_type) {
#ifdef _ENABLE_XRC_
    case CM_MSG_TYPE_XRC_REQ:
        peer = msg->server_rank;
        XRC_MSG ("Posting REQ msg to %d\n", peer);
        break;
    case CM_MSG_TYPE_XRC_REP:
        peer = msg->client_rank;
        XRC_MSG ("Posting REP msg to %d\n", peer);
        break;
#endif /* _ENABLE_XRC_ */
    case CM_MSG_TYPE_REQ:
        peer = msg->server_rank;
        break;
    case CM_MSG_TYPE_REP:
        peer = msg->client_rank;
        break;
#if defined(CKPT)
    case CM_MSG_TYPE_REACTIVATE_REQ:
        peer = msg->server_rank;
        break;
    case CM_MSG_TYPE_REACTIVATE_REP:
        peer = msg->client_rank;
        break;
#endif /* defined(CKPT) */
    default:
        CM_ERR_ABORT("error message type\n");
    }

    if (!pg->ch.mrail.cm_ah[peer]) {
        /* We need to resolve the address */
        CM_DBG("cm_ah not created, resolve conn info\n");
        err = cm_resolve_conn_info(pg, peer);
        if (err) {
            CM_ERR_ABORT("Cannot resolve connection info");
        }
    }

    CM_DBG("[%d] Post ud packet, srank %d, crank %d, peer %d, rid %d, "
            "rlid %08x\n",
            MPIDI_Process.my_pg_rank, msg->server_rank, msg->client_rank, peer,
            msg->req_id, pg->ch.mrail.cm_lid[peer]);

    __cm_post_ud_packet(msg, pg->ch.mrail.cm_ah[peer], 
                        pg->ch.mrail.cm_ud_qpn[peer]);

    return MPI_SUCCESS;
}


/*functions for cm protocol*/
static int cm_send_ud_msg(MPIDI_PG_t *pg, cm_msg * msg)
{
    struct timeval now;
    cm_pending *pending;
    int ret;

    CM_DBG("cm_send_ud_msg Enter");

    pending = cm_pending_create();
    if (cm_pending_init(pending, pg, msg, NULL))
    {
        CM_ERR_ABORT("cm_pending_init failed");
    }
    cm_pending_append(pending);

    gettimeofday(&now, NULL);
    pending->packet->timestamp = now;

    ret = cm_post_ud_packet(pg, &(pending->packet->payload));
    if (ret)
    {
        CM_ERR_ABORT("cm_post_ud_packet failed %d", ret);
    }

    if (cm_pending_num == 1)
    {
        pthread_cond_signal(&cm_cond_new_pending);
    }
    CM_DBG("cm_send_ud_msg Exit");

    return MPI_SUCCESS;
}

/*functions for cm protocol*/
static int cm_send_ud_msg_nopg(cm_msg * msg, struct ibv_ah *ah, 
                               uint32_t qpn, void *tag)
{
    struct timeval now;
    cm_pending *pending;
    int ret;

    CM_DBG("cm_send_ud_msg_nopg Enter");

    pending = cm_pending_create();
    if (cm_pending_init(pending, NULL, msg, tag))
    {
        CM_ERR_ABORT("cm_pending_init failed");
    }
    pending->data.nopg.ah  = ah;
    pending->data.nopg.qpn = qpn;
    cm_pending_append(pending);
    CM_DBG("pending head %p, add pending %p\n", cm_pending_head, pending);

    gettimeofday(&now, NULL);
    pending->packet->timestamp = now;

    ret = __cm_post_ud_packet(&(pending->packet->payload), ah, qpn);
    if (ret)
    {
        CM_ERR_ABORT("__cm_post_ud_packet failed %d", ret);
    }

    if (cm_pending_num == 1)
    {
        pthread_cond_signal(&cm_cond_new_pending);
    }
    CM_DBG("cm_send_ud_msg_nopg Exit");

    return MPI_SUCCESS;
}


#ifdef _ENABLE_XRC_
int cm_rcv_qp_create (MPIDI_VC_t *vc, uint32_t *qpn) 
{
    struct ibv_qp_init_attr init_attr;
    struct ibv_qp_attr attr;
    int rail_index, hca_index, port_index;

    memset (&init_attr, 0, sizeof (struct ibv_qp_init_attr));
    memset (&attr, 0, sizeof (struct ibv_qp_attr));
    
    vc->mrail.num_rails = rdma_num_rails;
    if (!vc->mrail.rails) {
        vc->mrail.rails = MPIU_Malloc
                (sizeof *vc->mrail.rails * vc->mrail.num_rails);
    
        if (!vc->mrail.rails) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for multirails\n");
        }
    }

    if (!vc->mrail.srp.credits) {
        vc->mrail.srp.credits = MPIU_Malloc(sizeof *vc->mrail.srp.credits * 
                vc->mrail.num_rails);
        if (!vc->mrail.srp.credits) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for credits array\n");
        }
    }
    
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    attr.qp_state = IBV_QPS_INIT;
     
    for (rail_index = 0; rail_index < vc->mrail.num_rails;
    	    rail_index++) {
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
        port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
        
        init_attr.xrc_domain = MPIDI_CH3I_RDMA_Process.xrc_domain [hca_index];
        if (ibv_create_xrc_rcv_qp (&init_attr, &qpn[rail_index])) {
            goto fn_err;
        }
        XRC_MSG ("Created RQPN: %d(%d) on %d\n", qpn[rail_index], rail_index,
                MPIDI_Process.my_pg->ch.mrail.xrc_hostid[MPIDI_Process.my_pg_rank]);
        vc->ch.xrc_my_rqpn[rail_index] = qpn[rail_index];
        
        vc->mrail.rails[rail_index].lid     = 
            MPIDI_CH3I_RDMA_Process.lids[hca_index][port_index];

        attr.port_num = MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        set_pkey_index (&attr.pkey_index, hca_index, attr.port_num);
        if (ibv_modify_xrc_rcv_qp (
                    MPIDI_CH3I_RDMA_Process.xrc_domain [hca_index], 
                    qpn[rail_index], &attr, 
                    IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | 
                    IBV_QP_ACCESS_FLAGS)) {
            goto fn_err;
        }
    }
    return 0;

fn_err:
    ibv_error_abort(GEN_EXIT_ERR, "Failed to create XRC rcv QP\n");
    return -1;
}
#endif


/* 
 * Higher level cm supporting functions *
 */
#undef FUNCNAME
#define FUNCNAME cm_accept
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_accept(MPIDI_PG_t *pg, cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t* vc;
    int i = 0;
    MPIDI_STATE_DECL(MPID_GET2_CM_ACCEPT);
    MPIDI_FUNC_ENTER(MPID_GET2_CM_ACCEPT);

    CM_DBG("cm_accpet Enter");

    /*Prepare QP */
    MPIDI_PG_Get_vc(pg, msg->client_rank, &vc);
    XRC_MSG ("BAR %d %d %d %d\n", vc->mrail.num_rails, msg->nrails, msg->client_rank, vc->pg_rank);
    vc->mrail.num_rails = msg->nrails;

    /*Prepare rep msg */
    MPIU_Memcpy(&msg_send, msg, sizeof(cm_msg));

#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        cm_rcv_qp_create (vc, msg_send.xrc_rqpn);
        cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 1, msg_send.xrc_rqpn, 0);
        for (i = 0; i < msg_send.nrails; ++i)
        {
            msg_send.lids[i] = vc->mrail.rails[i].lid;
            XRC_MSG ("cm_accept for %d lid %d qpn %d\n", vc->pg_rank, msg->lids[i], msg->qpns[i]);
            XRC_MSG ("RQP for %d, LID: %d\n", vc->pg_rank, msg_send.lids[i]);
            msg_send.qpns[i] = msg_send.xrc_rqpn[i];
        }
        for (i = 0; i < rdma_num_hcas; i++) {
            msg_send.xrc_srqn[i] = MPIDI_CH3I_RDMA_Process.xrc_srqn[i];
        }
    }
    else
#endif
    {
        cm_qp_create(vc, 1, MV2_QPT_XRC);
        cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, NULL, 0);

        for (i = 0; i < msg_send.nrails; ++i)
        {
            msg_send.lids[i] = vc->mrail.rails[i].lid;
            msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
        }
    }
    msg_send.vc_addr = (uintptr_t)vc;
    MPIU_Strncpy(msg_send.pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE);

#if defined(CKPT)
    if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REQ)
    {
        msg_send.msg_type = CM_MSG_TYPE_REACTIVATE_REP;
        /*Init vc and post buffers*/
        MRAILI_Init_vc_network(vc);
        {
            /*Adding the reactivation done message to the msg_log_queue*/
            MPIDI_CH3I_CR_msg_log_queue_entry_t *entry = 
                (MPIDI_CH3I_CR_msg_log_queue_entry_t *) MPIU_Malloc(sizeof(MPIDI_CH3I_CR_msg_log_queue_entry_t));
            vbuf *v=get_vbuf();
            MPIDI_CH3I_MRAILI_Pkt_comm_header *p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
            p->type = MPIDI_CH3_PKT_CM_REACTIVATION_DONE;
            /*Now all logged messages are sent using rail 0, 
            otherwise every rail needs to have one message*/
            entry->buf = v;
            entry->len = sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header);
            // MSG_LOG_ENQUEUE(vc, entry);
	        cm_enq_react_done(vc, entry);
        }
        vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_SRV;
    }
    else 
#endif /* defined(CKPT) */
    {
        /*Init vc and post buffers*/
        msg_send.msg_type = CM_MSG_TYPE_REP;
        MRAILI_Init_vc(vc);
#ifdef _ENABLE_XRC_
        /* Recv only */
        if (USE_XRC) 
        {
            XRC_MSG ("RECV_IDLE\n");
            VC_XST_SET (vc, XF_RECV_IDLE | XF_NEW_RECV);
            VC_SET_ACTIVE (vc);
        }
        else 
#endif
        {
            vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
        }
    }

    /*Send rep msg */
    if (cm_send_ud_msg(pg, &msg_send))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accept exit");
    MPIDI_FUNC_EXIT(MPID_GET2_CM_ACCEPT);
    return MPI_SUCCESS;
}



#undef FUNCNAME
#define FUNCNAME cm_accept_and_cancel
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_accept_and_cancel(MPIDI_PG_t *pg, cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t* vc;
    int i = 0;
    MPIDI_STATE_DECL(MPID_GEN2_CM_ACCEPT_AND_CANCEL);
    MPIDI_FUNC_ENTER(MPID_GEN2_CM_ACCEPT_AND_CANCEL);
    CM_DBG("cm_accept_and_cancel Enter");
    XRC_MSG ("accept_and_cancel\n");
    /* Prepare QP */
    MPIDI_PG_Get_vc(pg, msg->client_rank, &vc);
    vc->mrail.num_rails = msg->nrails;
   
    cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, NULL, 0);

    /*Prepare rep msg */
    MPIU_Memcpy(&msg_send, msg, sizeof(cm_msg));
    for (; i < msg_send.nrails; ++i)
    {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }
    msg_send.vc_addr = (uintptr_t)vc;
    MPIU_Strncpy(msg_send.pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE);
    
#if defined(CKPT)
    if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REQ)
    {
        msg_send.msg_type = CM_MSG_TYPE_REACTIVATE_REP;
        /*Init vc and post buffers*/
        MRAILI_Init_vc_network(vc);
        {
            /*Adding the reactivation done message to the msg_log_queue*/
            MPIDI_CH3I_CR_msg_log_queue_entry_t *entry = 
                (MPIDI_CH3I_CR_msg_log_queue_entry_t *) MPIU_Malloc(sizeof(MPIDI_CH3I_CR_msg_log_queue_entry_t));
            vbuf *v=get_vbuf();
            MPIDI_CH3I_MRAILI_Pkt_comm_header *p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
            p->type = MPIDI_CH3_PKT_CM_REACTIVATION_DONE;
            /*Now all logged messages are sent using rail 0, 
            otherwise every rail needs to have one message*/
            entry->buf = v;
            entry->len = sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header);
            // MSG_LOG_ENQUEUE(vc, entry);
            cm_enq_react_done(vc, entry);
        }
        vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_SRV;
    }
    else 
#endif /* defined(CKPT) */
    {
        /*Init vc and post buffers*/
        msg_send.msg_type = CM_MSG_TYPE_REP;
        MRAILI_Init_vc(vc);
        vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
    }
    
    /*Send rep msg */
    if (cm_send_ud_msg(pg, &msg_send))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accept_and_cancel Cancel");
    /*Cancel client role */
    {
        cm_pending *pending = cm_pending_search_peer(pg, msg->client_rank,
                                                     CM_PENDING_CLIENT, NULL);
        
        if (NULL == pending)
        {
            CM_ERR_ABORT("Can't find pending entry");
        }
        CM_DBG("[line 676] remove pending %p\n", pending);
        cm_pending_remove_and_destroy(pending);
    }
    CM_DBG("cm_accept_and_cancel Exit");
    
    MPIDI_FUNC_EXIT(MPID_GEN2_CM_ACCEPT_AND_CANCEL);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME cm_accept_nopg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_accept_nopg(MPIDI_VC_t *vc, cm_msg * msg)
{
    cm_msg msg_send;
    struct ibv_ah *ah;
    int rank;
    uint32_t lid, qpn, port;
#ifdef _ENABLE_XRC_
    uint32_t hostid;
#endif
    int i;
    MPIDI_STATE_DECL(MPID_GEN2_CM_ACCEPT_NOPG);
    MPIDI_FUNC_ENTER(MPID_GEN2_CM_ACCEPT_NOPG);
    CM_DBG("cm_accpet_nopg Enter");
    
    XRC_MSG ("cm_accept_nopg");
#ifdef _ENABLE_XRC_
    VC_XST_SET (vc, XF_DPM_INI);
#endif

    cm_qp_create(vc, 1, MV2_QPT_RC);
    cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, NULL, 1);

    /*Prepare rep msg */
    MPIU_Memcpy(&msg_send, msg, sizeof(cm_msg));
    for (i=0; i < msg_send.nrails; ++i)
    {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }
    msg_send.vc_addr = (uintptr_t)vc;
    msg_send.vc_addr_bounce = msg->vc_addr;

    PMI_Get_rank(&rank);
    CM_DBG("[%d cm_accept_nopg] get remote ifname %s, local qpn %08x, "
            "remote qpn %08x\n", rank, msg->ifname,
            vc->mrail.rails[0].qp_hndl->qp_num, msg->qpns[0]);

#ifdef _ENABLE_XRC_
    sscanf(msg->ifname, "#RANK:%08d(%08x:%08x:%08x:%08x)#",
            &rank, &lid, &qpn, &port, &hostid);
#else
    sscanf(msg->ifname, "#RANK:%08d(%08x:%08x:%08x)#", &rank,
            &lid, &qpn, &port);
#endif
    ah = cm_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], lid, port);
    if (!ah) {
        CM_ERR_ABORT("Cannot create ah");
    }

    MRAILI_Init_vc(vc);
    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
    msg_send.msg_type = CM_MSG_TYPE_RAW_REP;

    CM_DBG("### send RAW REP\n");

    /*Send rep msg */
    if (cm_send_ud_msg_nopg(&msg_send, ah, qpn, vc))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accpet_nopg Exit");
    MPIDI_FUNC_EXIT(MPID_GEN2_CM_ACCEPT_NOPG);
    return MPI_SUCCESS;
}

#ifdef _ENABLE_XRC_
void cm_xrc_send_enable (MPIDI_VC_t *vc)
{
    xrc_pending_conn_t *iter, *tmp;

    VC_XST_SET (vc, XF_SEND_IDLE);
    VC_XST_CLR (vc, XF_SEND_CONNECTING);
    VC_XST_CLR (vc, XF_REUSE_WAIT);

    iter = vc->ch.xrc_conn_queue;
    while (iter) {
        tmp = iter->next;
        XRC_MSG ("Activating conn to %d\n", iter->vc->pg_rank);
        cm_qp_reuse (iter->vc, vc);
        VC_XST_CLR (iter->vc, XF_REUSE_WAIT);
        MPIU_Free (iter);
        iter = tmp;
    }
    vc->ch.xrc_conn_queue = NULL;
}
#endif 


#undef FUNCNAME
#define FUNCNAME cm_enable
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_enable(MPIDI_PG_t *pg, cm_msg * msg)
{
    MPIDI_VC_t* vc;
    MPIDI_STATE_DECL(MPID_GEN2_CM_ENABLE);
    MPIDI_FUNC_ENTER(MPID_GEN2_CM_ENABLE);
    CM_DBG("cm_enable Enter");

    MPIDI_PG_Get_vc(pg, msg->server_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails)
    { /* Sanity check */
        XRC_MSG ("FOO %d %d \n", vc->mrail.num_rails, msg->nrails);
        CM_ERR_ABORT("mismatch in number of rails");
    }

#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        int i;
        /* Copy rcv qp numbers */
        for (i = 0; i < msg->nrails; ++i) {
            XRC_MSG ("cm_enable for %d lid %d qpn %d\n", vc->pg_rank, msg->lids[i], msg->qpns[i]);
            XRC_MSG ("Got RQPN: %d lid: %d\n", msg->xrc_rqpn[i], msg->lids[i]);
            vc->ch.xrc_rqpn[i] = msg->xrc_rqpn[i];
        }
        for (i = 0; i < rdma_num_hcas; i++) {
            vc->ch.xrc_srqn[i] = msg->xrc_srqn[i];
        }
        cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, msg->xrc_rqpn, 0);
    }
    else
#endif
    {
        cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, NULL, 0);
    }

#if defined(CKPT)
    if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REP)
    {
        /*Init vc and post buffers*/
        MRAILI_Init_vc_network(vc);
        {
            /*Adding the reactivation done message to the msg_log_queue*/
            MPIDI_CH3I_CR_msg_log_queue_entry_t *entry = 
                (MPIDI_CH3I_CR_msg_log_queue_entry_t *) MPIU_Malloc(sizeof(MPIDI_CH3I_CR_msg_log_queue_entry_t));
            vbuf *v=get_vbuf();
            MPIDI_CH3I_MRAILI_Pkt_comm_header *p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
            p->type = MPIDI_CH3_PKT_CM_REACTIVATION_DONE;
            /*Now all logged messages are sent using rail 0, 
            otherwise every rail needs to have one message*/
            entry->buf = v;
            entry->len = sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header);
            // MSG_LOG_ENQUEUE(vc, entry);
	        cm_enq_react_done(vc, entry);
        }
    }
    else 
#endif /* defined(CKPT) */
    {
        MRAILI_Init_vc(vc);
    }

    cm_qp_move_to_rts(vc);

    /* No need to send confirm and let the first message serve as confirm. */
#if defined(CKPT)
    if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REP)
    {
        /*Mark connected */
        vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2;
        MPIDI_CH3I_Process.reactivation_complete = 1;
    }
    else
#endif /* defined(CKPT) */
    {
        /*Mark connected */
        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
        VC_SET_ACTIVE (vc);
#ifdef _ENABLE_XRC_
        if (USE_XRC) {
            XRC_MSG ("SEND_IDLE\n");
            cm_xrc_send_enable (vc);
        }
#endif
        XRC_MSG ("new_conn_complete to %d state %d xst: 0x%08x", vc->pg_rank, 
                vc->state, vc->ch.xrc_flags);
        MPIDI_CH3I_Process.new_conn_complete = 1;
    } 

    CM_DBG("cm_enable Exit");
    MPIDI_FUNC_EXIT(MPID_GEN2_CM_ENABLE);
    return MPI_SUCCESS;
}

static int cm_enable_nopg(MPIDI_VC_t *vc, cm_msg * msg)
{
    int rank; PMI_Get_rank(&rank);
    CM_DBG("cm_enable_nopg Enter");

    XRC_MSG ("cm_enable_nopg");
#ifdef _ENABLE_XRC_
    VC_XST_SET (vc, XF_DPM_INI);
#endif

    cm_qp_move_to_rtr(vc, msg->lids, msg->gids, msg->qpns, 0, NULL, 1);

    MRAILI_Init_vc(vc);

    cm_qp_move_to_rts(vc);
    XRC_MSG ("RTS1");
    CM_DBG("[%d enable-nopg] remote qpn %08x, local qpn %08x\n",
            rank, msg->qpns[0], vc->mrail.rails[0].qp_hndl->qp_num);

    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
#ifdef _ENABLE_XRC_
    VC_XST_SET (vc, XF_SEND_IDLE);
#endif
    VC_SET_ACTIVE (vc);
    MPIDI_CH3I_Process.new_conn_complete = 1;

    CM_DBG("cm_enable_nopg Exit");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME cm_handle_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int cm_handle_msg(cm_msg * msg)
{
    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int my_rank;
    MPIDI_STATE_DECL(MPID_GEN2_CM_HANDLE_MSG);
    MPIDI_FUNC_ENTER(MPID_GEN2_CM_HANDLE_MSG);
    CM_DBG("##### Handle cm_msg: msg_type: %d, client_rank %d, server_rank"
           "%d\n",
           msg->msg_type, msg->client_rank, msg->server_rank, msg->nrails);

    /* FIXME: Ideally MPIDI_Process.my_pg_rank should be used here. However, 
     * MPIDI_Process.my_pg_rank is initialized after the ud thread is created,
     * which may cause MPIDI_Process.my_pg_rank to be 0 */
    PMI_Get_rank(&my_rank);

    switch (msg->msg_type)
    {
#ifdef _ENABLE_XRC_
        case CM_MSG_TYPE_XRC_REQ: 
            {
                int rail_index, hca_index;
                MPIU_Assert (USE_XRC != 0);
                MPICM_lock ();
                cm_msg rep;
                XRC_MSG ("CM_MSG_TYPE_XRC_REQ from %d\n", msg->client_rank);
                MPIDI_PG_Find (msg->pg_id, &pg);
                if (!pg) {
                    CM_ERR_ABORT ("No PG matches id %s", msg->pg_id);
                }
                MPIDI_PG_Get_vc (pg, msg->client_rank, &vc);

                if (VC_XST_ISSET (vc, XF_RECV_IDLE)) {
                    MPICM_unlock ();
                    return MPI_SUCCESS;
                }
                VC_XST_SET (vc, XF_RECV_IDLE);
                VC_SET_ACTIVE (vc);
                
                MPIU_Memcpy (&rep, msg, sizeof(cm_msg));
                rep.vc_addr = (uintptr_t) vc;
                MPIU_Strncpy (rep.pg_id, MPIDI_Process.my_pg->id, 
                        MAX_PG_ID_SIZE);
                rep.msg_type = CM_MSG_TYPE_XRC_REP;

                vc->mrail.remote_vc_addr = msg->vc_addr;
                vc->mrail.num_rails = rdma_num_rails;
                if (!vc->mrail.rails) {
                     vc->mrail.rails = MPIU_Malloc
                             (sizeof *vc->mrail.rails * vc->mrail.num_rails);
                    if (!vc->mrail.rails) {
                        ibv_error_abort(GEN_EXIT_ERR, 
                                "Fail to allocate resources for multirails\n");
                    }
                }

                if (!vc->mrail.srp.credits) {
                    vc->mrail.srp.credits = MPIU_Malloc(sizeof 
                            *vc->mrail.srp.credits * 
                            vc->mrail.num_rails);
                    if (!vc->mrail.srp.credits) {
                        ibv_error_abort(GEN_EXIT_ERR, 
                                "Fail to allocate resources for credits "
                                "array\n");
                    }
                }
                MRAILI_Init_vc (vc);
                for (rail_index = 0; rail_index < msg->nrails;
                        rail_index++) {
                    hca_index  = rail_index / (vc->mrail.num_rails / 
                            rdma_num_hcas);
                    XRC_MSG ("Registered with RQPN %d hca_index: %d on %d\n", 
                            msg->xrc_rqpn[rail_index], hca_index,
                            MPIDI_Process.my_pg->ch.mrail.xrc_hostid[MPIDI_Process.my_pg_rank]);
                    if (ibv_reg_xrc_rcv_qp (
                            MPIDI_CH3I_RDMA_Process.xrc_domain [hca_index], 
                            msg->xrc_rqpn[rail_index])) {
                        perror ("ibv_reg_xrc_rcv_qp");
                        ibv_error_abort(GEN_EXIT_ERR, 
                                "Can't register with RCV QP");
                    }
                    XRC_MSG ("DONE");
                    vc->ch.xrc_my_rqpn[rail_index] = msg->xrc_rqpn[rail_index];
                }
                for (rail_index = 0; rail_index < rdma_num_hcas; 
                        rail_index++) {
                    rep.xrc_srqn[rail_index] = 
                        MPIDI_CH3I_RDMA_Process.xrc_srqn[rail_index];
                }

                if (cm_send_ud_msg (pg, &rep)) {
                    CM_ERR_ABORT ("cm_send_ud_msg failed");
                }
                MPICM_unlock ();
                if (USE_XRC && VC_XSTS_ISUNSET (vc, XF_DPM_INI)) {
                    XRC_MSG ("2 CONNECT");
                    MPIDI_CH3I_CM_Connect (vc);
                }
            }
            break;

        case CM_MSG_TYPE_XRC_REP:
            {
                int i;
                MPICM_lock();
                MPIU_Assert (USE_XRC != 0);
                XRC_MSG ("CM_MSG_TYPE_XRC_REP from %d\n", msg->server_rank);
           
                MPIDI_PG_Find (msg->pg_id, &pg);
                if (!pg) {
                    CM_ERR_ABORT ("No PG matches id %s", msg->pg_id);
                }
                MPIDI_PG_Get_vc (pg, msg->server_rank, &vc); 
                if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE 
                        && VC_XST_ISSET (vc, XF_SEND_IDLE)) {
                    MPICM_unlock ();
                    return MPI_SUCCESS;
                }

                /* Ready to reuse now */
                cm_pending *pending = cm_pending_search_peer (pg, 
                        msg->server_rank, CM_PENDING_CLIENT, NULL);
                if (NULL == pending) {
                    CM_ERR_ABORT("Can't find pending entry");
                }
                CM_DBG("type rep, remove pending %p\n", pending);
                cm_pending_remove_and_destroy(pending);

                MRAILI_Init_vc (vc);
                for (i = 0; i < rdma_num_hcas; i++) {
                    vc->ch.xrc_srqn[i] = msg->xrc_srqn[i];
                }
                vc->mrail.remote_vc_addr = msg->vc_addr;
                vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                VC_SET_ACTIVE (vc);
                cm_xrc_send_enable (vc);
                MPIDI_CH3I_Process.new_conn_complete = 1;
                MPICM_unlock();
            }
            break;
#endif
    case CM_MSG_TYPE_REQ:
        {
            MPICM_lock();
            XRC_MSG ("CM_MSG_TYPE_REQ from %d\n", msg->client_rank);
            CM_DBG("Search for pg_id %s\n", msg->pg_id);
            MPIDI_PG_Find(msg->pg_id, &pg);
            if (!pg) {
                CM_ERR_ABORT("No PG matches id %s", msg->pg_id);
#if 0
                MPICM_unlock ();
                return MPI_SUCCESS;
#endif 
            }
            MPIDI_PG_Get_vc(pg, msg->client_rank, &vc);

            vc->mrail.remote_vc_addr = msg->vc_addr;
            CM_DBG("pg_id %s, pg %p, mypg %p, Server rank %d, "
                    "client rank %d,  my pg rank %d, "
                    "my lid %08x, vc state %d\n", msg->pg_id,
                    pg, MPIDI_Process.my_pg, 
                    msg->server_rank, msg->client_rank, my_rank,
                    MPIDI_CH3I_RDMA_Process.lids[0][0], 
                    vc->ch.state);

            assert(msg->server_rank == my_rank);

            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
#ifdef _ENABLE_XRC_
                    && (!USE_XRC || VC_XST_ISSET (vc, XF_RECV_IDLE))
#endif
                    ) {
                CM_DBG("Connection already exits");
                /*already existing */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (
#ifdef _ENABLE_XRC_
                    !USE_XRC && 
#endif
                    vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV)
            {
                CM_DBG("Already serving that client");
                /*already a pending request from that peer */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (
#ifdef _ENABLE_XRC_
                    !USE_XRC && 
#endif
                    vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI
                    )
            {
                /*already initiated a request to that peer */
                /*smaller rank will be server*/
                int compare = cm_compare_peer(pg, MPIDI_Process.my_pg,
                                              msg->client_rank, my_rank);
                CM_DBG("Concurrent request");
                if (compare < 0) {
                    /*that peer should be server, ignore the request*/
                    MPICM_unlock();
                    CM_DBG("Should act as client, ignore request");
                    return MPI_SUCCESS;
                }
                else if (compare > 0) {
                    /*myself should be server */
                    CM_DBG("Should act as server, accept and cancel");
                    cm_accept_and_cancel(pg, msg);
                } else {
                    CM_ERR_ABORT("Remote process has the same order");
                }
            }
            else
            {
                cm_accept(pg, msg);
            }
            MPICM_unlock();
#ifdef _ENABLE_XRC_
            if (USE_XRC && VC_XSTS_ISUNSET (vc, XF_DPM_INI)) {
                XRC_MSG ("3 CONNECT");
                MPIDI_CH3I_CM_Connect (vc);
            }
#endif
        }
        break;
    case CM_MSG_TYPE_REP:
        {
            cm_pending* pending;
            MPICM_lock();
            XRC_MSG ("CM_MSG_TYPE_REP from %d\n", msg->server_rank);
            CM_DBG("Got TYPE_REP, pg_id %s, my pg_id %s\n",
                    msg->pg_id, MPIDI_Process.my_pg->id);
            MPIDI_PG_Find(msg->pg_id, &pg);
            if (!pg) {
                CM_ERR_ABORT("No PG matches id %s", msg->pg_id);
            }
            MPIDI_PG_Get_vc(pg, msg->server_rank, &vc); 

            if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI
#ifdef _ENABLE_XRC_
                    || (USE_XRC && VC_XST_ISSET (vc, XF_SEND_IDLE))
#endif
                    ) {
                /*not waiting for any reply */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            vc->mrail.remote_vc_addr = msg->vc_addr;

            pending = cm_pending_search_peer(pg, msg->server_rank, 
                                                         CM_PENDING_CLIENT, NULL);
            if (NULL == pending)
            {
                CM_ERR_ABORT("Can't find pending entry");
            }
            CM_DBG("type rep, remove pending %p\n", pending);
            cm_pending_remove_and_destroy(pending);
            cm_enable(pg, msg);
            MPICM_unlock();
        }
        break;
    case CM_MSG_TYPE_RAW_REQ:
        {
            MPIDI_VC_t *vc;

            vc = MPIU_Malloc(sizeof(MPIDI_VC_t));
            if (!vc) {
                CM_ERR_ABORT("No memory for creating new vc");
            }
            MPIDI_VC_Init(vc, NULL, 0);
            vc->mrail.remote_vc_addr = msg->vc_addr;
            CM_DBG("Incoming REQ. remote VC is %x\n", msg->vc_addr);
            MPICM_lock();
            cm_accept_nopg(vc, msg);
            MPICM_unlock();
            break;
        }
    case CM_MSG_TYPE_RAW_REP:
        {
            cm_pending *pending;
            MPIDI_VC_t *vc = (void *)(uintptr_t)(msg->vc_addr_bounce);
            CM_DBG("Bounced VC is %x\n", vc);

            if(vc && vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) { /* this might a re-transmitted raw rep message */
                CM_DBG("RAW_REP re-transmission ignored\n");
                return MPI_SUCCESS;
            }
            
            if(!(vc && vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI)) {
                if(!vc) {
                    CM_DBG("No VC. Bounced VC is NULL\n");
                    CM_ERR_ABORT("VC Missing\n");
                }
                CM_DBG("Invalid VC: state is %d\n", vc->ch.state);         
                /* CM_ERR_ABORT("Invalid VC or VC state\n"); */
                if(vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
                    CM_DBG("VC already in connected state\n");
                }
                if(vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                    CM_DBG("VC in dis-connected state. but tried RAW-REQ\n");
                    CM_ERR_ABORT("VC should not be in UNCONN state\n");
                }
                return MPI_SUCCESS;
            }

            vc->mrail.remote_vc_addr = msg->vc_addr;
            MPICM_lock();
            pending = cm_pending_search_peer(NULL, -1, -1, vc);
            if (pending) {
                CM_DBG("Raw reply, remove pending %p\n", pending);
                cm_pending_remove_and_destroy(pending);
                cm_enable_nopg(vc, msg);
            }
            MPICM_unlock();
            break;
        }
#if defined(CKPT)
    case CM_MSG_TYPE_REACTIVATE_REQ:
        {
            MPICM_lock();
            MPIDI_PG_Find(msg->pg_id, &pg);
            if(!pg) {
                MPICM_unlock();
                CM_ERR_ABORT("No PG matches id %s", msg->pg_id);
            }
            MPIDI_PG_Get_vc(pg, msg->client_rank, &vc);
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
                || vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2)
            {
                /*already existing */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV)
            {
                /*already a pending request from that peer */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1)
            {
                /*already initiated a request to that peer */
                /*smaller rank will be server*/
                if (msg->client_rank < msg->server_rank)
                {
                    /*that peer should be server, ignore the request*/
                    MPICM_unlock();
                    return MPI_SUCCESS;
                }
                else
                {
                    /*myself should be server */
                    cm_accept_and_cancel(pg, msg); 
                }
            }
            else // still be "SUSPENDED", I will become srv
            {
                cm_accept(pg, msg);
            }
            MPICM_unlock();
        }
        break;
    case CM_MSG_TYPE_REACTIVATE_REP:
        {
            MPICM_lock();
            MPIDI_PG_Find(msg->pg_id, &pg);
            if(!pg) {
                MPICM_unlock();
                CM_ERR_ABORT("No PG matches id %s", msg->pg_id);
            }
            MPIDI_PG_Get_vc(pg, msg->server_rank, &vc);

            if (vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1)
            {
                /*not waiting for any reply */
                CM_DBG("Ignore CM_MSG_TYPE_REACTIVATE_REP, local state: %d",
                        vc->ch.state);
                MPICM_unlock();
                return MPI_SUCCESS;
            }

            cm_pending* pending = cm_pending_search_peer(pg, msg->server_rank,
                CM_PENDING_CLIENT, vc ); 

            if (NULL == pending)
            {
                CM_ERR_ABORT("Can't find pending entry");
            }
            cm_pending_remove_and_destroy(pending);
            cm_enable(pg, msg);
            MPICM_unlock();
        }
        break;
#endif /* defined(CKPT) */
    default:
        CM_ERR_ABORT("Unknown msg type: %d", msg->msg_type);
    }
    CM_DBG("cm_handle_msg Exit");
    MPIDI_FUNC_EXIT(MPID_GEN2_CM_HANDLE_MSG);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME cm_timeout_handler
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void *cm_timeout_handler(void *arg)
{
    struct timeval now;
    int delay;
    int ret;
    cm_pending *next_p, *curr_p;
    struct timespec remain;
    MPIDI_STATE_DECL(MPID_GEN2_CM_TIMEOUT_HANDLER);
    MPIDI_FUNC_ENTER(MPID_GEN2_CM_TIMEOUT_HANDLER);
    while (1)
    {
        MPICM_lock();
        while (cm_pending_num == 0)
        {
            pthread_cond_wait(&cm_cond_new_pending, &cm_conn_state_lock);
            CM_DBG("cond wait finish");
            if (cm_is_finalizing)
            {
                CM_DBG("Timer thread finalizing");
                MPICM_unlock();
                pthread_exit(NULL);
            }
        }
        while (1)
        {
            MPICM_unlock();
            nanosleep(&cm_timeout,&remain);/*Not handle the EINTR*/
            MPICM_lock();
            if (cm_is_finalizing)
            {
                CM_DBG("Timer thread finalizing");
                MPICM_unlock();
                pthread_exit(NULL);
            }
            if (cm_pending_num == 0)
            {
                break;
            }
            CM_DBG("Time out");
            curr_p = cm_pending_head;
            if (NULL == curr_p)
            {
                CM_ERR_ABORT("cm_pending_head corrupted");
            }
            next_p = cm_pending_head->next;
            gettimeofday(&now, NULL);
            while (next_p != cm_pending_head)
            {
                curr_p = next_p;
                next_p = next_p->next;
#ifdef _ENABLE_XRC_
                curr_p->attempts ++;
                if (curr_p->packet->payload.msg_type == CM_MSG_TYPE_XRC_REP 
                        || (USE_XRC && curr_p->attempts > CM_ATTS))
                {
                    /* Free it, never retransmit */
                    XRC_MSG ("Deleted CM entry");
                    cm_pending_remove_and_destroy (curr_p);
                    continue;
                }
#endif
                delay = (now.tv_sec - curr_p->packet->timestamp.tv_sec) * 1000000
                    + (now.tv_usec - curr_p->packet->timestamp.tv_usec);
                if (delay > cm_timeout_usec)
                {       /*Timer expired */
                    curr_p->packet->timestamp = now;
                    if (curr_p->has_pg){
                        ret = cm_post_ud_packet(curr_p->data.pg.pg, &(curr_p->packet->payload));
                    }
                    else 
                        ret = __cm_post_ud_packet(&(curr_p->packet->payload),
                                                  curr_p->data.nopg.ah, curr_p->data.nopg.qpn);
                    if (ret)
                    {
                        CM_ERR_ABORT("cm_post_ud_packet failed %d", ret);
                    }
                    gettimeofday(&now,NULL);
                }
            }
            CM_DBG("Time out exit");
        }
        MPICM_unlock();
    }
    MPIDI_FUNC_EXIT(MPID_GEN2_CM_TIMEOUT_HANDLER);
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    return NULL;
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(default, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
}

void *cm_completion_handler(void *arg)
{
    while (1)
    {
        struct ibv_cq *ev_cq;
        void *ev_ctx;
        struct ibv_wc wc;
        int ne;
        int spin_count;
        int ret;
        char* buf;
        cm_msg* msg;

        CM_DBG("Waiting for cm message");

        do
        {
            ret = ibv_get_cq_event(cm_ud_comp_ch, &ev_cq, &ev_ctx);
            if (ret && errno != EINTR)
            {
                CM_ERR_ABORT("Failed to get cq_event: %d", ret);
                return NULL;
            }
        }
        while (ret && errno == EINTR);

        ibv_ack_cq_events(ev_cq, 1);

        if (ev_cq != cm_ud_recv_cq)
        {
            CM_ERR_ABORT("CQ event for unknown CQ %p", ev_cq);
            return NULL;
        }

        CM_DBG("Processing cm message");
          
        spin_count = 0;
        do
        {
            ne = ibv_poll_cq(cm_ud_recv_cq, 1, &wc);
            if (ne < 0)
            {
                CM_ERR_ABORT("poll CQ failed %d", ne);
                return NULL;
            }
            else if (ne == 0)
            {
                ++spin_count;
                continue;
            }

            spin_count = 0;

            if (wc.status != IBV_WC_SUCCESS)
            {
                CM_ERR_ABORT("Failed status %d for wr_id %d",
                        wc.status, (int) wc.wr_id);
                return NULL;
            }

            if (wc.wr_id == CM_UD_RECV_WR_ID)
            {
                buf = (char*)cm_ud_recv_buf + cm_ud_recv_buf_index * (sizeof(cm_msg) + 40) + 40;
                msg = (cm_msg*) buf;
                if (msg->msg_type == CM_MSG_TYPE_FIN_SELF)
                {
                    CM_DBG("received finalization message");
                    return NULL;
                }
                cm_handle_msg(msg);
                CM_DBG("Post recv");
                cm_post_ud_recv(buf - 40, sizeof(cm_msg));
                cm_ud_recv_buf_index = (cm_ud_recv_buf_index + 1) % cm_recv_buffer_size;
            }
        }
        while (spin_count < cm_max_spin_count);

        CM_DBG("notify_cq");
        if (ibv_req_notify_cq(cm_ud_recv_cq, 1))
        {
            CM_ERR_ABORT("Couldn't request CQ notification");
            return NULL;
        }
    }
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    return NULL;
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(default, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
}

#undef FUNCNAME
#define FUNCNAME MPICM_Init_UD
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPICM_Init_UD(uint32_t * ud_qpn)
{
    int i = 0;
    char *value;
    int mpi_errno = MPI_SUCCESS;
    int result;
    MPIDI_STATE_DECL(MPID_GEN2_MPICM_INIT_UD);
    MPIDI_FUNC_ENTER(MPID_GEN2_MPICM_INIT_UD);

    cm_is_finalizing = 0;
    cm_req_id_global = 0;
    errno = 0;
    page_size = sysconf(_SC_PAGESIZE);
    if (errno != 0)
    {
        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
            "sysconf", strerror(errno));
    }

    if ((value = getenv("MV2_CM_SEND_DEPTH")) != NULL)
    {
        cm_send_depth = atoi(value);
    }
    else
    {
        cm_send_depth = DEFAULT_CM_SEND_DEPTH;
    }

    if ((value = getenv("MV2_CM_RECV_BUFFERS")) != NULL)
    {
        cm_recv_buffer_size = atoi(value);
    }
    else
    {
        cm_recv_buffer_size = DEFAULT_CM_MSG_RECV_BUFFER_SIZE;
    }

    if ((value = getenv("MV2_CM_UD_PSN")) != NULL)
    {
        cm_ud_psn = atoi(value);
    }
    else
    {
        cm_ud_psn = CM_UD_DEFAULT_PSN;
    }

    if ((value = getenv("MV2_CM_MAX_SPIN_COUNT")) != NULL)
    {
        cm_max_spin_count = atoi(value);
    }
    else
    {
        cm_max_spin_count = DEFAULT_CM_MAX_SPIN_COUNT;
    }
    
    if ((value = getenv("MV2_CM_THREAD_STACKSIZE")) != NULL)
    {
        cm_thread_stacksize = atoi(value);
    }
    else
    {
        cm_thread_stacksize = DEFAULT_CM_THREAD_STACKSIZE;
    }
   
    if ((value = getenv("MV2_CM_TIMEOUT")) != NULL)
    {
        cm_timeout_usec = atoi(value)*1000;
    }
    else
    { 
        cm_timeout_usec = CM_DEFAULT_TIMEOUT;
    }

    if (cm_timeout_usec < CM_MIN_TIMEOUT)
    {
        cm_timeout_usec = CM_MIN_TIMEOUT;
    }

    cm_timeout.tv_sec = cm_timeout_usec/1000000;
    cm_timeout.tv_nsec = (cm_timeout_usec-cm_timeout.tv_sec*1000000)*1000;
#ifdef USE_MEMORY_TRACING
    cm_ud_buf = MPIU_Malloc((sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1)); 
#else
    result = posix_memalign(&cm_ud_buf, page_size,
                 (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
#endif
    if ((cm_ud_buf==NULL))
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "cm_ud_buf");
    }
    
    MPIU_Memset(cm_ud_buf, 0,
           (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    cm_ud_send_buf = cm_ud_buf;
    cm_ud_recv_buf = (char*)cm_ud_buf + sizeof(cm_msg) + 40;

    /*use default nic*/
    cm_ud_comp_ch = ibv_create_comp_channel(MPIDI_CH3I_RDMA_Process.nic_context[0]);
    if (!cm_ud_comp_ch)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Couldn't create completion channel");
    }

    cm_ud_mr = ibv_reg_mr(MPIDI_CH3I_RDMA_Process.ptag[0], cm_ud_buf,
                          (sizeof(cm_msg) +
                           40) * (cm_recv_buffer_size + 1),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!cm_ud_mr)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Couldn't allocate MR");
    }

    cm_ud_recv_cq =
        ibv_create_cq(MPIDI_CH3I_RDMA_Process.nic_context[0], cm_recv_buffer_size, NULL,
                      cm_ud_comp_ch, 0);
    if (!cm_ud_recv_cq)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Couldn't create CQ");
    }

    cm_ud_send_cq =
        ibv_create_cq(MPIDI_CH3I_RDMA_Process.nic_context[0], cm_send_depth, NULL, NULL, 0);
    if (!cm_ud_send_cq)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Couldn't create CQ");
    }

    {
        struct ibv_qp_init_attr attr;
        MPIU_Memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
        attr.send_cq = cm_ud_send_cq;
        attr.recv_cq = cm_ud_recv_cq;
        attr.cap.max_send_wr = cm_send_depth;
        attr.cap.max_recv_wr = cm_recv_buffer_size;
        attr.cap.max_send_sge = 1;
        attr.cap.max_recv_sge = 1;
        attr.qp_type = IBV_QPT_UD;

        cm_ud_qp = ibv_create_qp(MPIDI_CH3I_RDMA_Process.ptag[0], &attr);
        if (!cm_ud_qp)
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Couldn't create UD QP");
        }
    }

    *ud_qpn = cm_ud_qp->qp_num;
    {
        struct ibv_qp_attr attr;
        MPIU_Memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_INIT;
        attr.pkey_index = 0;
        attr.port_num =  rdma_default_port; /*use default port*/
        attr.qkey = 0;

        if (ibv_modify_qp(cm_ud_qp, &attr,
                                 IBV_QP_STATE |
                                 IBV_QP_PKEY_INDEX |
                                 IBV_QP_PORT | IBV_QP_QKEY))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to modify QP to INIT");
        }
    }
    {
        struct ibv_qp_attr attr;
        MPIU_Memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to modify QP to RTR");
        }
    }

    {
        struct ibv_qp_attr attr;
        MPIU_Memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = cm_ud_psn;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to modify QP to RTS");
        }
    }

    for (i=0; i < cm_recv_buffer_size; ++i)
    {
        if (cm_post_ud_recv(
            (char*)cm_ud_recv_buf + (sizeof(cm_msg) + 40) * i,
            sizeof(cm_msg)))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "cm_post_ud_recv failed");
        }
    }
    cm_ud_recv_buf_index = 0;

    if (ibv_req_notify_cq(cm_ud_recv_cq, 1))
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Couldn't request CQ notification");
    }

    cm_pending_list_init();

fn_exit:
    MPIDI_FUNC_EXIT(MPID_GEN2_MPICM_INIT_UD);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPICM_Init_UD_struct
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPICM_Init_UD_struct(MPIDI_PG_t *pg, uint32_t * qpns, uint16_t * lids)
{
    int i;
    int mpi_errno = MPI_SUCCESS;
    int rank; PMI_Get_rank(&rank);
    MPIDI_STATE_DECL(MPID_GEN2_MPICM_INIT_UD_STRUCT);
    MPIDI_FUNC_ENTER(MPID_GEN2_MPICM_INIT_UD_STRUCT);
    
    /*Copy qpns and lids */
    MPIU_Memcpy(pg->ch.mrail.cm_ud_qpn, qpns, pg->size * sizeof(uint32_t));
    MPIU_Memcpy(pg->ch.mrail.cm_lid, lids, pg->size * sizeof(uint16_t));

    /*Create address handles */
    for (i=0; i < pg->size; ++i)
    {
        pg->ch.mrail.cm_ah[i] = cm_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], 
                                             pg->ch.mrail.cm_lid[i],
                                             rdma_default_port);
        if (!pg->ch.mrail.cm_ah[i])
        {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "Failed to create AH");
        }
    }
fn_fail:
    MPIDI_FUNC_EXIT(MPID_GEN2_MPICM_INIT_UD_STRUCT);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPICM_Create_UD_threads
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPICM_Create_UD_threads()
{
    int ret;
    int mpi_errno = MPI_SUCCESS;

    pthread_mutex_init(&cm_conn_state_lock, NULL);
    /*Current protocol requires cm_conn_state_lock not to be Recursive*/
    pthread_cond_init(&cm_cond_new_pending, NULL);
    /*Spawn cm thread */
    {
        pthread_attr_t attr;
        if (pthread_attr_init(&attr))
        {
           MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "pthread_attr_init failed");
        }
        ret = pthread_attr_setstacksize(&attr,cm_thread_stacksize);
        if (ret && ret != EINVAL) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "pthread_attr_setstacksize failed");
        }
        pthread_create(&cm_comp_thread, &attr, cm_completion_handler, NULL);
        pthread_create(&cm_timer_thread, &attr, cm_timeout_handler, NULL);
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPICM_Finalize_UD
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPICM_Finalize_UD()
{
    cm_msg msg;
    struct ibv_sge list;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    MPIDI_PG_t *pg = MPIDI_Process.my_pg;
    MPIDI_STATE_DECL(MPID_GEN2_MPICM_FINALIZE_UD);
    MPIDI_FUNC_ENTER(MPID_GEN2_MPICM_FINALIZE_UD);

    int i = 0;

    CM_DBG("In MPICM_Finalize_UD");
    cm_is_finalizing = 1;
    cm_pending_list_finalize();

    /*Cancel cm thread */
    msg.msg_type = CM_MSG_TYPE_FIN_SELF;
    MPIU_Memcpy((char*)cm_ud_send_buf + 40, &msg, sizeof(cm_msg));
    MPIU_Memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) cm_ud_send_buf + 40;
    list.length = sizeof(cm_msg);
    list.lkey = cm_ud_mr->lkey;

    MPIU_Memset(&wr, 0, sizeof(struct ibv_send_wr));
    wr.wr_id = CM_UD_SEND_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    wr.wr.ud.ah = pg->ch.mrail.cm_ah[MPIDI_Process.my_pg_rank];
    wr.wr.ud.remote_qpn = pg->ch.mrail.cm_ud_qpn[MPIDI_Process.my_pg_rank];
    wr.wr.ud.remote_qkey = 0;

    if (ibv_post_send(cm_ud_qp, &wr, &bad_wr))
    {
        CM_ERR_ABORT("ibv_post_send to ud qp failed");
    }
    CM_DBG("Self send issued");

    pthread_join(cm_comp_thread,NULL);
    CM_DBG("Completion thread destroyed");
#if defined(CKPT)
    if (MPIDI_CH3I_CR_Get_state() == MPICR_STATE_PRE_COORDINATION)
    {
        pthread_cancel(cm_timer_thread);
/*
        pthread_mutex_trylock(&cm_cond_new_pending);
        pthread_cond_signal(&cm_cond_new_pending);
        CM_DBG("Timer thread signaled");
*/        
        MPICM_unlock();
        pthread_join(cm_timer_thread, NULL);
    }
    else 
#endif /* defined(CKPT) */
    {
        pthread_cancel(cm_timer_thread);
    }
    CM_DBG("Timer thread destroyed");

    pthread_mutex_destroy(&cm_conn_state_lock);
    pthread_cond_destroy(&cm_cond_new_pending);
    /*Clean up */
    for (; i < pg->size; ++i)
    {
        if (ibv_destroy_ah(pg->ch.mrail.cm_ah[i]))
        {
            CM_ERR_ABORT("ibv_destroy_ah failed\n");
        }
    }

    if (ibv_destroy_qp(cm_ud_qp))
    {
        CM_ERR_ABORT("ibv_destroy_qp failed\n");
    }

    if (ibv_destroy_cq(cm_ud_recv_cq))
    {
        CM_ERR_ABORT("ibv_destroy_cq failed\n");
    }

    if (ibv_destroy_cq(cm_ud_send_cq))
    {
        CM_ERR_ABORT("ibv_destroy_cq failed\n");
    }

    if (ibv_destroy_comp_channel(cm_ud_comp_ch))
    {
        CM_ERR_ABORT("ibv_destroy_comp_channel failed\n");
    }

    if (ibv_dereg_mr(cm_ud_mr))
    {
        CM_ERR_ABORT("ibv_dereg_mr failed\n");
    }

    if (cm_ud_buf)
    {
        MPIU_Free(cm_ud_buf);
    }

    CM_DBG("MPICM_Finalize_UD done");
    MPIDI_FUNC_EXIT(MPID_GEN2_MPICM_FINALIZE_UD);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Connect
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Connect(MPIDI_VC_t * vc)
{
    cm_msg msg;
    int i = 0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_GEN2_CH3I_CM_CONNECT);
    MPIDI_FUNC_ENTER(MPID_GEN2_CH3I_CM_CONNECT);

    MPICM_lock();

#ifdef _ENABLE_XRC_
    if (USE_XRC && VC_XST_ISSET (vc, (XF_SEND_CONNECTING | XF_SEND_IDLE))) {
        goto fn_exit;
    }
#endif

    if (vc->ch.state != MPIDI_CH3I_VC_STATE_UNCONNECTED)
    {
        goto fn_exit;
    }

    if (vc->pg_rank == MPIDI_Process.my_pg_rank &&
        vc->pg == MPIDI_Process.my_pg)
    {
        goto fn_exit;
    }

#if defined(RDMA_CM)
    /* Trap into the RDMA_CM connection initiation */
    if (MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand)
    {
        int j;
        int rail_index;
        vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;
        for (i = 0; i < rdma_num_hcas; ++i) {
            for (j = 0; j < (rdma_num_ports * rdma_num_qp_per_port); ++j) {
                rail_index = i * rdma_num_ports * rdma_num_qp_per_port + j;
                rdma_cm_connect_to_server(vc, 
                    rdma_cm_host_list[vc->pg_rank * rdma_num_hcas + i], 
                    rail_index);
            }
        }
        goto fn_exit;
    }
#endif /* defined(RDMA_CM) */ 

    CM_DBG("Sending Req to rank %d", vc->pg_rank);
    CM_DBG("Sending CM_Request, pgid %s, vc %p, num_rails %d\n", 
            MPIDI_Process.my_pg->id, vc, vc->mrail.num_rails);
    XRC_MSG ("CM_Connect");
    /*Create qps*/
#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        VC_XST_SET (vc, XF_SEND_CONNECTING);
        if (!vc->mrail.rails) {
            XRC_MSG ("alloc 20");
            vc->mrail.num_rails = rdma_num_rails;
            vc->mrail.rails = MPIU_Malloc
                    (sizeof *vc->mrail.rails * vc->mrail.num_rails);
            if (!vc->mrail.rails) {
                ibv_error_abort(GEN_EXIT_ERR, 
                        "Fail to allocate resources for multirails\n");
            }
        }
        if (!vc->pg->ch.mrail.cm_ah[vc->pg_rank]) {
            XRC_MSG ("BBB");
            /* We need to resolve the address */
            CM_DBG("cm_ah not created, resolve conn info\n");
            if (cm_resolve_conn_info(vc->pg, vc->pg_rank)) {
                CM_ERR_ABORT("Cannot resolve connection info");
            }
        }
        if (vc->smp.hostid == -1) {
            XRC_MSG ("INIT HOSTID %d", 
                    vc->pg->ch.mrail.xrc_hostid [vc->pg_rank]);
            vc->smp.hostid = vc->pg->ch.mrail.xrc_hostid [vc->pg_rank];
        }
    }

#endif
    if (cm_qp_create(vc, 0, MV2_QPT_XRC) == MV2_QP_REUSE) {
        goto fn_exit;
    }
    msg.server_rank = vc->pg_rank;
    msg.client_rank = MPIDI_Process.my_pg_rank;
    msg.msg_type = CM_MSG_TYPE_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;
    for (i = 0; i < msg.nrails; ++i)
    {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
        XRC_MSG ("Created SQP: %d LID: %d\n", msg.qpns[i], msg.lids[i]);
    }
    msg.vc_addr = (uintptr_t)vc;
    if (strlen(MPIDI_Process.my_pg->id) > MAX_PG_ID_SIZE) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s", "pg id too long");
    }
    MPIU_Strncpy(msg.pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE);

    mpi_errno = cm_send_ud_msg(vc->pg, &msg);
    if (mpi_errno)
    {
        MPIU_ERR_POP(mpi_errno);
    }
    
    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;

fn_exit:
    MPICM_unlock();
    MPIDI_FUNC_EXIT(MPID_GEN2_CH3I_CM_CONNECT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Connect_raw_vc
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Connect_raw_vc(MPIDI_VC_t * vc, char *ifname)
{
    struct ibv_ah *ah;
    cm_msg msg;
    int mpi_errno = MPI_SUCCESS;
    int rank;
    uint32_t lid, qpn, port;
#ifdef _ENABLE_XRC_
    uint32_t hostid;
#endif
    int i = 0;

    XRC_MSG ("MPIDI_CH3I_CM_Connect_raw_vc");
    MPICM_lock();

#ifdef _ENABLE_XRC_
    VC_XST_SET (vc, XF_DPM_INI);
#endif

    if (vc->ch.state != MPIDI_CH3I_VC_STATE_UNCONNECTED)
    {
        MPICM_unlock();
        return MPI_SUCCESS;
    }

#ifdef _ENABLE_XRC_
    sscanf(ifname, "#RANK:%08d(%08x:%08x:%08x:%08x)#",
            &rank, &lid, &qpn, &port, &hostid);
#else
    sscanf(ifname, "#RANK:%08d(%08x:%08x:%08x)#", &rank,
            &lid, &qpn, &port);
#endif

    ah = cm_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], lid, port);
    if (!ah) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "Fail to create address handle");
    }
    CM_DBG("Sending Req to rank %d", vc->pg_rank);
    /*Create qps*/
    cm_qp_create(vc, 1, MV2_QPT_RC);
    msg.msg_type = CM_MSG_TYPE_RAW_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;

    for (i = 0; i < msg.nrails; ++i) {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    msg.vc_addr = (uintptr_t)vc;
    CM_DBG("CM_MSG_SEND_RAW_REQ: sending vc is %x\n", vc);
    mpi_errno = MPIDI_CH3I_CM_Get_port_info(msg.ifname, 128);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;
    if (cm_send_ud_msg_nopg(&msg, ah, qpn, vc)) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "Fail to post ud msg");
        CM_ERR_ABORT("Fail to post UD RAW Request message\n");
    }

fn_fail:
    MPICM_unlock();
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Establish
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* This function should be called when VC received the first message in on-demand case. */
int MPIDI_CH3I_CM_Establish(MPIDI_VC_t * vc)
{
    cm_pending *pending;

    MPICM_lock();

#if defined(RDMA_CM)
    if (MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand)
    {
        MPICM_unlock();
        return MPI_SUCCESS;
    }
#endif /* defined(RDMA_CM) */
        XRC_MSG ("EST vc %d: st: %d, xr: 0x%08x\n", vc->pg_rank, vc->state, vc->ch.xrc_flags);

    CM_DBG("MPIDI_CH3I_CM_Establish peer rank %d",vc->pg_rank);
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_SRV
#if defined(CKPT)
            && vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_SRV
#endif /* defined(CKPT) */
       ) {
#ifdef _ENABLE_XRC_
        if (USE_XRC && VC_XST_ISSET (vc, XF_NEW_RECV)) {
            goto remove_pending;

        }
#endif
        MPICM_unlock();
        return MPI_SUCCESS;
    }
#ifdef _ENABLE_XRC_
remove_pending:
#endif
    pending = cm_pending_search_peer(vc->pg, vc->pg_rank, CM_PENDING_SERVER, 
            vc);
    if (NULL == pending)
    {
#ifdef _ENABLE_XRC_
        if (!USE_XRC)
#endif
            CM_ERR_ABORT("Can't find pending entry");

    }
    else {
    
        CM_DBG("pending head %p, remove %p\n", cm_pending_head, pending);
        cm_pending_remove_and_destroy(pending);
    }
#ifdef _ENABLE_XRC_
    if (USE_XRC && VC_XST_ISUNSET (vc, XF_DPM_INI)) {
        VC_XST_CLR (vc, XF_NEW_RECV);
    }
    else 
#endif
    {
        cm_qp_move_to_rts(vc);

        XRC_MSG ("RTS2");
        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
#ifdef _ENABLE_XRC_
        VC_XST_SET (vc, XF_SEND_IDLE);
#endif
        VC_SET_ACTIVE (vc);
    }
    MPICM_unlock();
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Get_port_info
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Get_port_info(char *ifname, int max_len)
{
    int mpi_errno = MPI_SUCCESS;
    int rank;

    PMI_Get_rank(&rank);

    if (max_len < 128) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "Array too short to hold port info");
    }

#ifdef _ENABLE_XRC_
    MPIU_Snprintf(ifname, 128, "#RANK:%08d(%08x:%08x:%08x:%08x)#",
            rank, MPIDI_CH3I_RDMA_Process.lids[0][0], cm_ud_qp->qp_num, 
            rdma_default_port, 
            MPIDI_Process.my_pg->ch.mrail.xrc_hostid[rank]);
#else
    MPIU_Snprintf(ifname, 128, "#RANK:%08d(%08x:%08x:%08x)#",
            rank, MPIDI_CH3I_RDMA_Process.lids[0][0], cm_ud_qp->qp_num, 
            rdma_default_port);
#endif
fn_fail:
    XRC_MSG ("ret: %d\n", mpi_errno);
    return mpi_errno;   
}

#ifdef _ENABLE_XRC_
#undef FUNCNAME
#define FUNCNAME cm_send_xrc_cm_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int cm_send_xrc_cm_msg (MPIDI_VC_t *vc, MPIDI_VC_t *orig_vc)
{
    cm_msg msg;
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPIDI_PG_t *pg;

    XRC_MSG ("cm_send_xrc_cm_msg %d\n", vc->pg_rank);

    msg.nrails = vc->mrail.num_rails;
    pg = vc->pg;
    for (i = 0; i < msg.nrails; ++i) {
        XRC_MSG ("Sending RQPN %d to %d\n", 
                orig_vc->ch.xrc_rqpn[i], vc->pg_rank);
        msg.xrc_rqpn[i] = orig_vc->ch.xrc_rqpn[i];
    }

    msg.server_rank = vc->pg_rank;
    msg.client_rank = MPIDI_Process.my_pg_rank;
    msg.msg_type = CM_MSG_TYPE_XRC_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.vc_addr = (uintptr_t) vc;
    MPIU_Strncpy (msg.pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE);

    mpi_errno = cm_send_ud_msg (vc->pg, &msg);
    if (mpi_errno)
    {
        MPIU_ERR_POP (mpi_errno);
    }
fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}
#endif /* _ENABLE_XRC_ */

#if defined(CKPT)

static pthread_mutex_t cm_automic_op_lock = PTHREAD_MUTEX_INITIALIZER;

/* Send messages buffered in msg log queue. */
int MPIDI_CH3I_CM_Send_logged_msg(MPIDI_VC_t *vc) 
{
    vbuf *v;
    MPIDI_CH3I_CR_msg_log_queue_entry_t *entry; 

    while (!MSG_LOG_EMPTY(vc))
    {
        MSG_LOG_DEQUEUE(vc, entry);
        v = entry->buf;
        
        /* Only use rail 0 to send logged message. */
        DEBUG_PRINT("[eager send] len %d, selected rail hca %d, rail %d\n",
                    entry->len, vc->mrail.rails[0].hca_index, 0);

        vbuf_init_send(v, entry->len, 0);
        MPIDI_CH3I_RDMA_Process.post_send(vc, v, 0);

        MPIU_Free(entry);
    }
    return 0;
}
 
int cm_send_suspend_msg(MPIDI_VC_t* vc)
{
    vbuf *v;
    int rail = 0;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;

    if (SMP_INIT && (vc->smp.local_nodes >= 0)) {
        /* Use the shared memory channel to send Suspend message for SMP VCs */
        MPID_Request *sreq = NULL;
        extern int MPIDI_CH3_SMP_iStartMsg(MPIDI_VC_t *, void *, MPIDI_msg_sz_t, MPID_Request **);
        v = get_vbuf();
        p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
        p->type = MPIDI_CH3_PKT_CM_SUSPEND;
        MPIDI_CH3_SMP_iStartMsg(vc, p, sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header), &sreq);
        vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDING;
        vc->ch.rput_stop = 1;

		if(!sreq )// if sreq == NULL, the msg has been sent out 
			vc->mrail.suspended_rails_send++;
		if( vc->mrail.suspended_rails_send > 0 &&
			vc->mrail.suspended_rails_recv > 0 ){
			vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
			vc->mrail.suspended_rails_send = 0;
			vc->mrail.suspended_rails_recv = 0;
			CM_DBG("%s [%d <= %d]: turn to SUSPENDED\n", __func__, 
				MPIDI_Process.my_pg_rank, vc->pg_rank );
		}
        return(0);
    }

    CM_DBG("In cm_send_suspend_msg peer %d",vc->pg_rank);
    for (; rail < vc->mrail.num_rails; ++rail)
    {
        /*Send suspend msg to each rail*/
        v = get_vbuf(); 
        p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
        p->type = MPIDI_CH3_PKT_CM_SUSPEND;
        vbuf_init_send(v, sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header), rail);
        MPIDI_CH3I_RDMA_Process.post_send(vc, v, rail);
    }
    vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDING;
    vc->ch.rput_stop = 1;

    CM_DBG("Out cm_send_suspend_msg");
    return 0;
}

int cm_send_reactivate_msg(MPIDI_VC_t* vc)
{
    cm_msg msg;
    int i = 0;

    /* Use the SMP channel to send Reactivate message for SMP VCs */
    MPID_Request *sreq;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
    vbuf *v;
    extern int MPIDI_CH3_SMP_iStartMsg(MPIDI_VC_t *, void *, MPIDI_msg_sz_t, MPID_Request **);
    if (SMP_INIT && (vc->smp.local_nodes >= 0)) {
    v = get_vbuf();
    p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
    p->type = MPIDI_CH3_PKT_CM_REACTIVATION_DONE;
    MPIDI_CH3_SMP_iStartMsg(vc, p, sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header), &sreq);
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    return(MPI_SUCCESS);
    }

    MPICM_lock();
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDED)
    {
        CM_DBG("Already being reactivated by remote side peer rank %d\n", vc->pg_rank);
        MPICM_unlock();
        return MPI_SUCCESS;
    }
    CM_DBG("Sending CM_MSG_TYPE_REACTIVATE_REQ to rank %d", vc->pg_rank);
    /*Create qps*/
    if (cm_qp_create(vc, 0, MV2_QPT_XRC) == MV2_QP_REUSE) {
        MPICM_unlock();
        return MPI_SUCCESS;
    }
    MPIU_Strncpy(msg.pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE); 
    msg.server_rank = vc->pg_rank;
    msg.client_rank = MPIDI_Process.my_pg_rank; 
    msg.msg_type = CM_MSG_TYPE_REACTIVATE_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;
    for (; i < msg.nrails; ++i)
    {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }
    
    if (cm_send_ud_msg(MPIDI_Process.my_pg, &msg))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }
    
    vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1;
    MPICM_unlock();
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Disconnect
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Disconnect(MPIDI_VC_t* vc)
{
    /*To be implemented*/
    int mpi_errno;
    /* all variable must be declared before the state declarations */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_CM_DISCONNECT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_CM_DISCONNECT);

    /* Insert implementation here */
    fprintf( stderr, "Function not implemented\n" );
    exit(EXIT_FAILURE);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_CM_DISCONNECT);
    return mpi_errno;
}

/*Suspend connections in use*/
int MPIDI_CH3I_CM_Suspend(MPIDI_VC_t ** vc_vector)
{
    MPIDI_VC_t * vc;
    int i;
    int flag;
    CM_DBG("MPIDI_CH3I_CM_Suspend Enter");
    /*Send out all flag messages*/
    for (i = 0; i < MPIDI_Process.my_pg->size; ++i)
    {
        if (i == MPIDI_Process.my_pg_rank)
        {
            continue;
        }

        if (NULL != vc_vector[i])
        {
            vc = vc_vector[i];
            pthread_mutex_lock(&cm_automic_op_lock);
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) 
            {
                cm_send_suspend_msg(vc);
            }
            else if (vc->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDING
                && vc->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDED)
            { 
                CM_ERR_ABORT("Wrong state when suspending %d\n",vc->ch.state);
            }

            pthread_mutex_unlock(&cm_automic_op_lock);
        }
    }
    CM_DBG("Progressing"); 
    /*Make sure all channels suspended*/
    do
    {
        flag = 0;
        for (i = 0; i < MPIDI_Process.my_pg->size; ++i)
        {
            if (i == MPIDI_Process.my_pg_rank)
            {
                continue;
            }

            pthread_mutex_lock(&cm_automic_op_lock);
            if (NULL!=vc_vector[i] 
                && vc_vector[i]->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDED)
            {
                pthread_mutex_unlock(&cm_automic_op_lock);
                flag = 1;
                break;
            }
            pthread_mutex_unlock(&cm_automic_op_lock);
        }
        if (flag == 0)
        {
            break;
        }

		MPIDI_CH3I_Progress(FALSE, NULL);
    }
    while (flag);

    CM_DBG("Channels suspended");

#if defined(CM_DEBUG)
    int rail;

    /*Sanity check*/
    for (i = 0; i < MPIDI_Process.my_pg->size; ++i)
    {
        if (i == MPIDI_Process.my_pg_rank)
        {
            continue;
        }

        if (NULL != vc_vector[i])
        {
            vc = vc_vector[i];

        /* Skip if it is an SMP VC */
        if (SMP_INIT && (vc->smp.local_nodes >= 0))
            continue;

            /*assert ext send queue and backlog queue empty*/
            for (rail = 0; rail < vc->mrail.num_rails; ++rail)
            {
                ibv_backlog_queue_t q = vc->mrail.srp.credits[rail].backlog;
                MPIU_Assert(q.len == 0);
                MPIU_Assert(vc->mrail.rails[rail].ext_sendq_head == NULL);
            }
        }
    }
#endif /* defined(CM_DEBUG) */
    CM_DBG("MPIDI_CH3I_CM_Suspend Exit");
    return 0;
}


/*Reactivate previously suspended connections*/
int MPIDI_CH3I_CM_Reactivate(MPIDI_VC_t ** vc_vector)
{
    MPIDI_VC_t* vc;
    int i = 0;
    int flag;
    CM_DBG("MPIDI_CH3I_CM_Reactivate Enter");

    /*Send out all reactivate messages*/
    for (; i < MPIDI_Process.my_pg->size; ++i)
    {
        if (i == MPIDI_Process.my_pg_rank)
            continue;

       if (NULL != vc_vector[i])
       {
            vc = vc_vector[i];
            pthread_mutex_lock(&cm_automic_op_lock);
           
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED)
                cm_send_reactivate_msg(vc);
            else if (vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1
                && vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2
                && vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_SRV
                && vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
            {
                CM_ERR_ABORT("Wrong state when reactivation %d\n",vc->ch.state);
            }
            pthread_mutex_unlock(&cm_automic_op_lock);
        }
    }


    /*Make sure all channels reactivated*/
    do
    {
        flag = 0;

        for (i = 0; i < MPIDI_Process.my_pg->size; ++i)
        {
            if (i == MPIDI_Process.my_pg_rank)
                continue;

            if (NULL != vc_vector[i])
            {
                vc = vc_vector[i];

				/* Handle the reactivation of the SMP channel */
				if (SMP_INIT && (vc->smp.local_nodes >= 0)) 
                {
                      pthread_mutex_lock(&cm_automic_op_lock);
                      if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
                          pthread_mutex_unlock(&cm_automic_op_lock);
                          flag = 1;
                          break;
                     }
                     pthread_mutex_unlock(&cm_automic_op_lock);
                     continue;
                }
				///
                MPIU_Assert( vc->mrail.sreq_to_update >= 0 );
                if (!vc->mrail.reactivation_done_send 
					|| !vc->mrail.reactivation_done_recv )
                //|| vc->mrail.sreq_to_update>0 )//some rndv(sender) haven't been updated yet
                {
                       flag = 1;
                       break;
                }
             }
		}

        if (flag == 0)
        {
            break;
        }

		MPIDI_CH3I_Progress(FALSE, NULL);
    }
    while (flag);

    /*put down flags*/
    MPIDI_CH3I_Process.reactivation_complete = 0;
    for (i = 0; i < MPIDI_Process.my_pg->size; ++i)
    {
        if (i == MPIDI_Process.my_pg_rank)
        {
            continue;
        }

        if (NULL != vc_vector[i])
        {
            vc=vc_vector[i];
            vc->mrail.reactivation_done_send = 0;
            vc->mrail.reactivation_done_recv = 0;
	    	///clear CR related fields 
            vc->ch.rput_stop = 0;
            vc->mrail.react_entry = NULL;
            vc->mrail.react_send_ready = 0; 
            pthread_spin_destroy( &vc->mrail.cr_lock);
            if (vc->mrail.sreq_head){
               PUSH_FLOWLIST(vc);      
            }
        }
    }

    return 0;
}

/*CM message handler for RC message in progress engine*/
void MPIDI_CH3I_CM_Handle_recv(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf * v)
{
	CM_DBG("%s: [%d <= %d]: got msg: %s(%d)\n", __func__, MPIDI_Process.my_pg_rank, 
		vc->pg_rank, MPIDI_CH3_Pkt_type_to_string[msg_type], msg_type  );

    /*Only count the total number, specific rail matching is not needed*/
    if (msg_type == MPIDI_CH3_PKT_CM_SUSPEND)
    {
        CM_DBG("[%d]: handle recv CM_SUSPEND, peer rank %d, rails_send %d, rails_recv %d, ch.state %d",
           MPIDI_Process.my_pg_rank,  vc->pg_rank, vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv, vc->ch.state);
        pthread_mutex_lock(&cm_automic_op_lock);

        /*Note no need to lock in ibv_send, because this function is called in 
        * progress engine, so that it can't be called in parallel with ibv_send*/
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE)
        { 
            /*passive suspending*/
            CM_DBG("Not in Suspending state yet, start suspending");
            cm_send_suspend_msg(vc);
        }

        ++vc->mrail.suspended_rails_recv;

        if (vc->mrail.suspended_rails_send == vc->mrail.num_rails
            && vc->mrail.suspended_rails_recv == vc->mrail.num_rails
            && vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING)
        {
            vc->mrail.suspended_rails_send = 0;
            vc->mrail.suspended_rails_recv = 0;
            vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
        }

        pthread_mutex_unlock(&cm_automic_op_lock);
        CM_DBG("handle recv CM_SUSPEND done, peer rank %d, rails_send %d, rails_recv %d, ch.state %d",
                vc->pg_rank, vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv, vc->ch.state);
    } 
    else if (msg_type == MPIDI_CH3_PKT_CM_REACTIVATION_DONE)
    {
        CM_DBG("handle recv MPIDI_CH3_PKT_CM_REACTIVATION_DONE peer rank %d, done_recv %d",
                vc->pg_rank,vc->mrail.reactivation_done_recv);
        vc->mrail.reactivation_done_recv = 1;
        //vc->ch.rput_stop = 0;
        // if(vc->mrail.sreq_to_update==0 )
        //    vc->ch.rput_stop = 0;
        if (vc->mrail.sreq_head)
        {
            PUSH_FLOWLIST(vc);
        }
    }
}

void MPIDI_CH3I_CM_Handle_send_completion(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf * v)
{
    /*Only count the total number, specific rail matching is not needed*/
    if (msg_type == MPIDI_CH3_PKT_CM_SUSPEND)
    {
        CM_DBG("handle send CM_SUSPEND, peer rank %d, rails_send %d, rails_recv %d, ch.state %d",
                vc->pg_rank, vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv, vc->ch.state);
        pthread_mutex_lock(&cm_automic_op_lock);
        ++vc->mrail.suspended_rails_send;

        if (vc->mrail.suspended_rails_send == vc->mrail.num_rails
            && vc->mrail.suspended_rails_recv == vc->mrail.num_rails
            && vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING)
        {
            vc->mrail.suspended_rails_send = 0;
            vc->mrail.suspended_rails_recv = 0;
            vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
        }

        pthread_mutex_unlock(&cm_automic_op_lock);
        CM_DBG("handle send CM_SUSPEND done, peer rank %d, rails_send %d, rails_recv %d, ch.state %d",
                vc->pg_rank, vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv, vc->ch.state);
    } 
    else if (msg_type == MPIDI_CH3_PKT_CM_REACTIVATION_DONE)
    {
        CM_DBG("handle send MPIDI_CH3_PKT_CM_REACTIVATION_DONE peer rank %d, done_send %d",
                vc->pg_rank, vc->mrail.reactivation_done_send);
        vc->mrail.reactivation_done_send = 1;
    }
}

#endif /* defined(CKPT) */

/* vi:set sw=4 tw=80: */
