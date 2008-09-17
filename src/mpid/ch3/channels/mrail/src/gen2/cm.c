/* Copyright (c) 2002-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
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

#if defined(CKPT)
#define CM_MSG_TYPE_REACTIVATE_REQ   2
#define CM_MSG_TYPE_REACTIVATE_REP   3
#endif /* defined(CKPT) */

#define CM_MSG_TYPE_FIN_SELF  99

typedef struct cm_msg {
    uint32_t req_id;
    uint32_t server_rank;
    uint32_t client_rank;
    uint8_t msg_type;
    uint8_t nrails;
    uint16_t lids[MAX_NUM_SUBRAILS];
    uint32_t qpns[MAX_NUM_SUBRAILS];
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

MPICM_ib_context cm_ib_context;

struct ibv_comp_channel *cm_ud_comp_ch;
struct ibv_qp *cm_ud_qp;
struct ibv_cq *cm_ud_recv_cq;
struct ibv_cq *cm_ud_send_cq;
struct ibv_mr *cm_ud_mr;
struct ibv_ah **cm_ah;          /*Array of address handles of peers */
uint32_t *cm_ud_qpn;            /*Array of ud pqn of peers */
uint16_t *cm_lid;               /*Array of lid of all procs */
void *cm_ud_buf;
void *cm_ud_send_buf;           /*length is set to 1 */
void *cm_ud_recv_buf;
int cm_ud_recv_buf_index;
int page_size;

extern int *rdma_cm_host_list;

#define CM_ERR_ABORT(args...) do {                                                        \
    fprintf(stderr, "[Rank %d][%s: line %d]", cm_ib_context.rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                                \
    fprintf(stderr, "\n");                                                                \
    exit(-1);                                                                             \
}while (0)

#if defined(CM_DEBUG)
#define CM_DBG(args...)  do {                                                             \
    fprintf(stderr, "[Rank %d][%s: line %d]", cm_ib_context.rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                                \
    fprintf(stderr, "\n");                                                                \
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
    int peer;                   /*if peer = self rank, it's head node */
    cm_packet *packet;
    struct cm_pending *next;
    struct cm_pending *prev;
} cm_pending;

int cm_pending_num;

#define CM_PENDING_SERVER   0
#define CM_PENDING_CLIENT   1

cm_pending *cm_pending_head = NULL;

/*Interface to lock/unlock connection manager*/
void MPICM_lock()
{
  /*    printf("[Rank %d],CM lock\n",cm_ib_context.rank); */
  pthread_mutex_lock(&cm_conn_state_lock);
}

void MPICM_unlock()
{
  /*    printf("[Rank %d],CM unlock\n", cm_ib_context.rank); */
  pthread_mutex_unlock(&cm_conn_state_lock);
}

/*
 * TODO add error checking
 */
cm_pending *cm_pending_create()
{
    cm_pending *temp = (cm_pending *) MPIU_Malloc(sizeof(cm_pending));
    memset(temp, 0, sizeof(cm_pending));
    return temp;
}

int cm_pending_init(cm_pending * pending, cm_msg * msg)
{
    if (msg->msg_type == CM_MSG_TYPE_REQ)
    {
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->peer = msg->server_rank;
    } 
    else if (msg->msg_type == CM_MSG_TYPE_REP)
    {
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->peer = msg->client_rank;
    }
#if defined(CKPT)
    else if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REQ)
    {
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->peer = msg->server_rank;
    }
    else if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REP)
    {
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->peer = msg->client_rank;
    }
#endif /* defined(CKPT) */    
    else
    {
        CM_ERR_ABORT("error message type");
    }
    pending->packet = (cm_packet *) MPIU_Malloc(sizeof(cm_packet));
    memcpy(&(pending->packet->payload), msg, sizeof(cm_msg));
    
    return MPI_SUCCESS;
}

cm_pending *cm_pending_search_peer(int peer, int cli_or_srv)
{
    cm_pending *pending = cm_pending_head;
    while (pending->next != cm_pending_head)
    {
        pending = pending->next;
        if (pending->cli_or_srv == cli_or_srv && pending->peer == peer)
        {
            return pending;
        }
    }
    return NULL;
}

int cm_pending_append(cm_pending * node)
{
    cm_pending *last = cm_pending_head->prev;
    last->next = node;
    node->next = cm_pending_head;
    cm_pending_head->prev = node;
    node->prev = last;
    ++cm_pending_num;
    return MPI_SUCCESS;
}

int cm_pending_remove_and_destroy(cm_pending * node)
{
    MPIU_Free(node->packet);
    node->next->prev = node->prev;
    node->prev->next = node->next;
    MPIU_Free(node);
    --cm_pending_num;
    return MPI_SUCCESS;
}

/*
 * TODO add error checking
 */
int cm_pending_list_init()
{
    cm_pending_num = 0;
    cm_pending_head = cm_pending_create();
    cm_pending_head->peer = cm_ib_context.rank;
    cm_pending_head->prev = cm_pending_head;
    cm_pending_head->next = cm_pending_head;
    return MPI_SUCCESS;
}

int cm_pending_list_finalize()
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


int cm_post_ud_recv(void *buf, int size)
{
    struct ibv_sge list;
    struct ibv_recv_wr wr;
    struct ibv_recv_wr *bad_wr;

    memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) buf;
    list.length = size + 40;
    list.lkey = cm_ud_mr->lkey;
    memset(&wr, 0, sizeof(struct ibv_recv_wr));
    wr.next = NULL;
    wr.wr_id = CM_UD_RECV_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;

    return ibv_post_recv(cm_ud_qp, &wr, &bad_wr);
}

int cm_post_ud_packet(cm_msg * msg)
{
    int peer;
    struct ibv_sge list;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    struct ibv_wc wc;
    int ne;

    if (msg->msg_type == CM_MSG_TYPE_REQ)
    {
        peer = msg->server_rank;
    } 
    else if (msg->msg_type == CM_MSG_TYPE_REP)
    {
        peer = msg->client_rank;
    }
#if defined(CKPT)
    else if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REQ)
    {
        peer = msg->server_rank;
    }
    else if (msg->msg_type == CM_MSG_TYPE_REACTIVATE_REP)
    {
        peer = msg->client_rank;
    }
#endif /* defined(CKPT) */
    else
    {
        CM_ERR_ABORT("error message type\n");
    }
    CM_DBG("cm_post_ud_packet, post message type %d to rank %d", msg->msg_type, peer);

    memcpy((char*)cm_ud_send_buf + 40, msg, sizeof(cm_msg));
    memset(&list, 0, sizeof(struct ibv_sge));
    list.addr = (uintptr_t) cm_ud_send_buf + 40;
    list.length = sizeof(cm_msg);
    list.lkey = cm_ud_mr->lkey;

    memset(&wr, 0, sizeof(struct ibv_send_wr));
    wr.wr_id = CM_UD_SEND_WR_ID;
    wr.sg_list = &list;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
    wr.wr.ud.ah = cm_ah[peer];
    wr.wr.ud.remote_qpn = cm_ud_qpn[peer];
    wr.wr.ud.remote_qkey = 0;

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

/*functions for cm protocol*/
int cm_send_ud_msg(cm_msg * msg)
{
    struct timeval now;

    CM_DBG("cm_send_ud_msg Enter");
    cm_pending* pending = cm_pending_create();
    if (cm_pending_init(pending, msg))
    {
        CM_ERR_ABORT("cm_pending_init failed");
    }
    cm_pending_append(pending);

    gettimeofday(&now, NULL);
    pending->packet->timestamp = now;
    int ret = cm_post_ud_packet(&(pending->packet->payload));

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



int cm_accept(cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t* vc;
    int i = 0;
    CM_DBG("cm_accpet Enter");

    /*Prepare QP */
    MPIDI_PG_Get_vc(cm_ib_context.pg, msg->client_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails)
    {
        CM_ERR_ABORT("mismatch in number of rails");
    }

    cm_qp_create(vc);
    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    for (; i < msg_send.nrails; ++i)
    {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

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
            MSG_LOG_ENQUEUE(vc, entry);
        }
        vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_SRV;
    }
    else 
#endif /* defined(CKPT) */
    {
        /*Init vc and post buffers*/
        msg_send.msg_type = CM_MSG_TYPE_REP;
        MRAILI_Init_vc(vc, cm_ib_context.rank);
        vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
    }

    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accpet Exit");
    return MPI_SUCCESS;
}

int cm_accept_and_cancel(cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t* vc;
    int i = 0;
    CM_DBG("cm_accept_and_cancel Enter");

    /* Prepare QP */
    MPIDI_PG_Get_vc(cm_ib_context.pg, msg->client_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails)
    {
        CM_ERR_ABORT("mismatch in number of rails");
    }
    
    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    for (; i < msg_send.nrails; ++i)
    {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }
    
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
            MSG_LOG_ENQUEUE(vc, entry);
        }
        vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_SRV;
    }
    else 
#endif /* defined(CKPT) */
    {
        /*Init vc and post buffers*/
        msg_send.msg_type = CM_MSG_TYPE_REP;
        MRAILI_Init_vc(vc, cm_ib_context.rank);
        vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
    }
    
    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accept_and_cancel Cancel");
    /*Cancel client role */
    {
        cm_pending *pending = cm_pending_search_peer(msg->client_rank, CM_PENDING_CLIENT);
        
        if (NULL == pending)
        {
            CM_ERR_ABORT("Can't find pending entry");
        }
        cm_pending_remove_and_destroy(pending);
    }
    CM_DBG("cm_accept_and_cancel Exit");
    
    return MPI_SUCCESS;
}


int cm_enable(cm_msg * msg)
{
    MPIDI_VC_t* vc;
    CM_DBG("cm_enable Enter");

    MPIDI_PG_Get_vc(cm_ib_context.pg, msg->server_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails)
    {
        CM_ERR_ABORT("mismatch in number of rails");
    }

    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);

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
            MSG_LOG_ENQUEUE(vc, entry);
        }
    }
    else 
#endif /* defined(CKPT) */
    {
        MRAILI_Init_vc(vc, cm_ib_context.rank);
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
        vc->state = MPIDI_VC_STATE_ACTIVE;
        MPIDI_CH3I_Process.new_conn_complete = 1;
    } 

    CM_DBG("cm_enable Exit");
    return MPI_SUCCESS;
}

int cm_handle_msg(cm_msg * msg)
{
    MPIDI_VC_t *vc;
    CM_DBG("Handle cm_msg: msg_type: %d, client_rank %d, server_rank %d",
           msg->msg_type, msg->client_rank, msg->server_rank);
    switch (msg->msg_type)
    {
    case CM_MSG_TYPE_REQ:
        {
            MPICM_lock();
            MPIDI_PG_Get_vc(cm_ib_context.pg, msg->client_rank, &vc);
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE)
            {
                CM_DBG("Connection already exits");
                /*already existing */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV)
            {
                CM_DBG("Already serving that client");
                /*already a pending request from that peer */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            else if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI)
            {
                /*already initiated a request to that peer */
                /*smaller rank will be server*/
                CM_DBG("Concurrent request");
                if (msg->client_rank < cm_ib_context.rank)
                {
                    /*that peer should be server, ignore the request*/
                    MPICM_unlock();
                    CM_DBG("Should act as client, ignore request");
                    return MPI_SUCCESS;
                }
                else
                {
                    /*myself should be server */
                    CM_DBG("Should act as server, accept and cancel");
                    cm_accept_and_cancel(msg);
                }
            }
            else
            {
                cm_accept(msg);
            }
            MPICM_unlock();
        }
        break;
    case CM_MSG_TYPE_REP:
        {
            MPICM_lock();
            MPIDI_PG_Get_vc(cm_ib_context.pg, msg->server_rank, &vc); 
            if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI)
            {
                /*not waiting for any reply */
                MPICM_unlock();
                return MPI_SUCCESS;
            }
            cm_pending* pending = cm_pending_search_peer(msg->server_rank, CM_PENDING_CLIENT);
            if (NULL == pending)
            {
                CM_ERR_ABORT("Can't find pending entry");
            }
            cm_pending_remove_and_destroy(pending);
            cm_enable(msg);
            MPICM_unlock();
        }
        break;
#if defined(CKPT)
    case CM_MSG_TYPE_REACTIVATE_REQ:
        {
            MPICM_lock();
            MPIDI_PG_Get_vc(cm_ib_context.pg, msg->client_rank, &vc);
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
                if (msg->client_rank < cm_ib_context.rank)
                {
                    /*that peer should be server, ignore the request*/
                    MPICM_unlock();
                    return MPI_SUCCESS;
                }
                else
                {
                    /*myself should be server */
                    cm_accept_and_cancel(msg);
                }
            }
            else
            {
                cm_accept(msg);
            }
            MPICM_unlock();
        }
        break;
    case CM_MSG_TYPE_REACTIVATE_REP:
        {
            MPICM_lock();
            MPIDI_PG_Get_vc(cm_ib_context.pg, msg->server_rank, &vc);
            if (vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1)
            {
                /*not waiting for any reply */
                CM_DBG("Ignore CM_MSG_TYPE_REACTIVATE_REP, local state: %d",
                        vc->ch.state);
                MPICM_unlock();
                return MPI_SUCCESS;
            }

            cm_pending* pending = cm_pending_search_peer(msg->server_rank, CM_PENDING_CLIENT);

            if (NULL == pending)
            {
                CM_ERR_ABORT("Can't find pending entry");
            }
            cm_pending_remove_and_destroy(pending);
            cm_enable(msg);
            MPICM_unlock();
        }
        break;
#endif /* defined(CKPT) */
    default:
        CM_ERR_ABORT("Unknown msg type: %d", msg->msg_type);
    }
    CM_DBG("cm_handle_msg Exit");
    return MPI_SUCCESS;
}

void *cm_timeout_handler(void *arg)
{
    struct timeval now;
    int delay;
    int ret;
    cm_pending *p;
    struct timespec remain;
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
            p = cm_pending_head;
            if (NULL == p)
            {
                CM_ERR_ABORT("cm_pending_head corrupted");
            }
            gettimeofday(&now, NULL);
            while (p->next != cm_pending_head)
            {
                p = p->next;
                delay = (now.tv_sec - p->packet->timestamp.tv_sec) * 1000000
                    + (now.tv_usec - p->packet->timestamp.tv_usec);
                if (delay > cm_timeout_usec)
                {       /*Timer expired */
                    CM_DBG("Resend");
                    p->packet->timestamp = now;
                    ret = cm_post_ud_packet(&(p->packet->payload));
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

    /*Initialization */
    cm_ah = MPIU_Malloc(cm_ib_context.size * sizeof(struct ibv_ah *));
    if (cm_ah == NULL)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "cm_ah");
    }

    cm_ud_qpn = MPIU_Malloc(cm_ib_context.size * sizeof(uint32_t));
    if (cm_ud_qpn == NULL)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "cm_ud_qpn");
    }

    cm_lid = MPIU_Malloc(cm_ib_context.size * sizeof(uint16_t));
    if (cm_lid == NULL)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "cm_lid");
    }

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

    cm_ud_buf = memalign(page_size,
                 (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    if (!cm_ud_buf)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "cm_ud_buf");
    }
    
    memset(cm_ud_buf, 0,
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
        memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
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
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

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
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to modify QP to RTR");
        }
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = cm_ud_psn;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN))
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to modify QP to RTS");
        }
    }

    for (; i < cm_recv_buffer_size; ++i)
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
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

int MPICM_Connect_UD(uint32_t * qpns, uint16_t * lids)
{
    int i = 0;
    int ret;
    int mpi_errno = MPI_SUCCESS;
    
    /*Copy qpns and lids */
    memcpy(cm_ud_qpn, qpns, cm_ib_context.size * sizeof(uint32_t));
    memcpy(cm_lid, lids, cm_ib_context.size * sizeof(uint16_t));

    /*Create address handles */
    for (; i < cm_ib_context.size; ++i)
    {
        struct ibv_ah_attr ah_attr;
        memset(&ah_attr, 0, sizeof(ah_attr));
        ah_attr.is_global = 0;
        ah_attr.dlid = cm_lid[i];
        ah_attr.sl = 0;
        ah_attr.src_path_bits = 0;
        ah_attr.port_num = rdma_default_port;
        cm_ah[i] = ibv_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], &ah_attr);
        if (!cm_ah[i])
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Failed to create AH");
        }
    }

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

int MPICM_Finalize_UD()
{
    int i = 0;

    CM_DBG("In MPICM_Finalize_UD");
    cm_is_finalizing = 1;
    cm_pending_list_finalize();

    /*Cancel cm thread */
    {
        cm_msg msg;
        struct ibv_sge list;
        struct ibv_send_wr wr;
        struct ibv_send_wr *bad_wr;
        struct ibv_wc wc;
        msg.msg_type = CM_MSG_TYPE_FIN_SELF;
        memcpy((char*)cm_ud_send_buf + 40, &msg, sizeof(cm_msg));
        memset(&list, 0, sizeof(struct ibv_sge));
        list.addr = (uintptr_t) cm_ud_send_buf + 40;
        list.length = sizeof(cm_msg);
        list.lkey = cm_ud_mr->lkey;

        memset(&wr, 0, sizeof(struct ibv_send_wr));
        wr.wr_id = CM_UD_SEND_WR_ID;
        wr.sg_list = &list;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_SEND;
        wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_SOLICITED;
        wr.wr.ud.ah = cm_ah[cm_ib_context.rank];
        wr.wr.ud.remote_qpn = cm_ud_qpn[cm_ib_context.rank];
        wr.wr.ud.remote_qkey = 0;

        if (ibv_post_send(cm_ud_qp, &wr, &bad_wr))
        {
            CM_ERR_ABORT("ibv_post_send to ud qp failed");
        }
        CM_DBG("Self send issued");
    }
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
    for (; i < cm_ib_context.size; ++i)
    {
        if (ibv_destroy_ah(cm_ah[i]))
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

    if (cm_ah)
    {
        MPIU_Free(cm_ah);
    }

    if (cm_ud_qpn)
    {
        MPIU_Free(cm_ud_qpn);
    }

    if (cm_lid)
    {
        MPIU_Free(cm_lid);
    }

    CM_DBG("MPICM_Finalize_UD done");
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Connect(MPIDI_VC_t * vc)
{
    cm_msg msg;
    int i = 0;
    MPICM_lock();
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_UNCONNECTED)
    {
        MPICM_unlock();
        return MPI_SUCCESS;
    }

    if (vc->pg_rank == cm_ib_context.rank)
    {
        MPICM_unlock();
        return MPI_SUCCESS;
    }

#if defined(RDMA_CM)
    /* Trap into the RDMA_CM connection initiation */
    if (MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand)
    {
        int j;
        int rail_index;
	vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;
	for (; i < rdma_num_hcas; ++i)
        {
	    for (j = 0; j < (rdma_num_ports * rdma_num_qp_per_port); ++j)
            {
	        rail_index = i * rdma_num_ports * rdma_num_qp_per_port + j;
	        rdma_cm_connect_to_server(vc->pg_rank, rdma_cm_host_list[vc->pg_rank * rdma_num_hcas + i], rail_index);
	    }
	}
	MPICM_unlock();
	return MPI_SUCCESS;
    }
#endif /* defined(RDMA_CM) */ 

    CM_DBG("Sending Req to rank %d", vc->pg_rank);
    /*Create qps*/
    cm_qp_create(vc);
    msg.server_rank = vc->pg_rank;
    msg.client_rank = cm_ib_context.rank;
    msg.msg_type = CM_MSG_TYPE_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;
#if defined(RDMA_CM)
    for (i = 0; i < msg.nrails; ++i)
#else /* defined(RDMA_CM) */
    for (; i < msg.nrails; ++i)
#endif /* defined(RDMA_CM) */
    {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    if (cm_send_ud_msg(&msg))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }
    
    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;
    MPICM_unlock();
    return MPI_SUCCESS;
}

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

    CM_DBG("MPIDI_CH3I_CM_Establish peer rank %d",vc->pg_rank);
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_SRV
#if defined(CKPT)
        && vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_SRV
#endif /* defined(CKPT) */
    ) {
        /*not waiting for comfirm */
        MPICM_unlock();
        return MPI_SUCCESS;
    }
    pending = cm_pending_search_peer(vc->pg_rank, CM_PENDING_SERVER);
    if (NULL == pending)
    {
        CM_ERR_ABORT("Can't find pending entry");
    }
    cm_pending_remove_and_destroy(pending);
    cm_qp_move_to_rts(vc);
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    vc->state = MPIDI_VC_STATE_ACTIVE;
    MPICM_unlock();
    return MPI_SUCCESS;
}

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
                    entry->len, vc->mrail.rails[rail].hca_index, 0);

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
	MPID_Request *sreq;
	extern int MPIDI_CH3_SMP_iStartMsg(MPIDI_VC_t *, void *, MPIDI_msg_sz_t, MPID_Request **);
	v = get_vbuf();
	p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
	p->type = MPIDI_CH3_PKT_CM_SUSPEND;
	MPIDI_CH3_SMP_iStartMsg(vc, p, sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header), &sreq);
	vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDING;
	vc->ch.rput_stop = 1;
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
    cm_qp_create(vc);
    msg.server_rank = vc->pg_rank;
    msg.client_rank = cm_ib_context.rank;
    msg.msg_type = CM_MSG_TYPE_REACTIVATE_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;
    for (; i < msg.nrails; ++i)
    {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    if (cm_send_ud_msg(&msg))
    {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }
    
    vc->ch.state = MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1;
    MPICM_unlock();
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Disconnect(MPIDI_VC_t* vc)
{
    /*To be implemented*/
    MPIU_Assert(0);
    return 0;
}

/*Suspend connections in use*/
int MPIDI_CH3I_CM_Suspend(MPIDI_VC_t ** vc_vector)
{
    MPIDI_VC_t * vc;
    int i;
    int flag;
    CM_DBG("MPIDI_CH3I_CM_Suspend Enter");
    /*Send out all flag messages*/
    for (i = 0; i < cm_ib_context.size; ++i)
    {
        if (i == cm_ib_context.rank)
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
        for (i = 0; i < cm_ib_context.size; ++i)
        {
            if (i == cm_ib_context.rank)
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
    for (i = 0; i < cm_ib_context.size; ++i)
    {
        if (i == cm_ib_context.rank)
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
    for (; i < cm_ib_context.size; ++i)
    {
        if (i == cm_ib_context.rank)
        {
            continue;
        }

        if (NULL != vc_vector[i])
        {
            vc = vc_vector[i];
            pthread_mutex_lock(&cm_automic_op_lock);
            
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED)
            {
                cm_send_reactivate_msg(vc);
            }
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

        for (i = 0; i < cm_ib_context.size; ++i)
        {
            if (i == cm_ib_context.rank)
            {
                continue;
            }

            if (NULL != vc_vector[i])
            {
                vc = vc_vector[i];

		/* Handle the reactivation of the SMP channel */
		if (SMP_INIT && (vc->smp.local_nodes >= 0)) {
                    pthread_mutex_lock(&cm_automic_op_lock);
		    if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
                        pthread_mutex_unlock(&cm_automic_op_lock);
			flag = 1;
			break;
		    }
                    pthread_mutex_unlock(&cm_automic_op_lock);
		    continue;
		}

                if (!vc->mrail.reactivation_done_send
                    || !vc->mrail.reactivation_done_recv)
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
    for (i = 0; i < cm_ib_context.size; ++i)
    {
        if (i == cm_ib_context.rank)
        {
            continue;
        }

        if (NULL != vc_vector[i])
        {
            vc=vc_vector[i];
            vc->mrail.reactivation_done_send = 0;
            vc->mrail.reactivation_done_recv = 0;
        }
    }

    return 0;
}

/*CM message handler for RC message in progress engine*/
void MPIDI_CH3I_CM_Handle_recv(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf * v)
{
    /*Only count the total number, specific rail matching is not needed*/
    if (msg_type == MPIDI_CH3_PKT_CM_SUSPEND)
    {
        CM_DBG("handle recv CM_SUSPEND, peer rank %d, rails_send %d, rails_recv %d, ch.state %d",
                vc->pg_rank, vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv, vc->ch.state);
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
        vc->ch.rput_stop = 0;

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
