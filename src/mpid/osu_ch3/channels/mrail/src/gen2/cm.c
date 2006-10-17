
/* Copyright (c) 2002-2006, The Ohio State University. All rights
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

#include "cm.h"

#define CM_MSG_TYPE_REQ     0
#define CM_MSG_TYPE_REP     1

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
static pthread_t cm_comp_thread, cm_timer_thread;
static pthread_mutex_t cm_conn_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cm_cond_new_pending = PTHREAD_COND_INITIALIZER;
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


#define CM_ERR_ABORT(args...)  do {\
    fprintf(stderr, "[Rank %d][%s: line %d]", cm_ib_context.rank ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    fprintf(stderr, "\n"); \
    exit(-1);\
}while(0)

#ifdef CM_DEBUG
#define CM_DBG(args...)  do {\
    fprintf(stderr, "[Rank %d][%s: line %d]", cm_ib_context.rank ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    fprintf(stderr, "\n"); \
}while(0)
#else
#define CM_DBG(args...)
#endif

#define CM_LOCK   pthread_mutex_lock(&cm_conn_state_lock)

#define CM_UNLOCK   pthread_mutex_unlock(&cm_conn_state_lock)

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

cm_pending *cm_pending_create()
{
    cm_pending *temp = (cm_pending *) malloc(sizeof(cm_pending));
    memset(temp, 0, sizeof(cm_pending));
    return temp;
}

int cm_pending_init(cm_pending * pending, cm_msg * msg)
{
    if (msg->msg_type == CM_MSG_TYPE_REQ) {
        pending->cli_or_srv = CM_PENDING_CLIENT;
        pending->peer = msg->server_rank;
    } else if (msg->msg_type == CM_MSG_TYPE_REP) {
        pending->cli_or_srv = CM_PENDING_SERVER;
        pending->peer = msg->client_rank;
    } else {
        CM_ERR_ABORT("error message type");
    }
    pending->packet = (cm_packet *) malloc(sizeof(cm_packet));
    memcpy(&(pending->packet->payload), msg, sizeof(cm_msg));
    
    return MPI_SUCCESS;
}

cm_pending *cm_pending_search_peer(int peer, int cli_or_srv)
{
    cm_pending *pending = cm_pending_head;
    
    while (pending->next != cm_pending_head) {
        pending = pending->next;
        if (pending->cli_or_srv == cli_or_srv && pending->peer == peer) {
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
    cm_pending_num++;
    
    return MPI_SUCCESS;
}

int cm_pending_remove_and_destroy(cm_pending * node)
{
    free(node->packet);
    node->next->prev = node->prev;
    node->prev->next = node->next;
    free(node);
    cm_pending_num--;
    
    return MPI_SUCCESS;
}

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
    while (cm_pending_head->next != cm_pending_head) {
        cm_pending_remove_and_destroy(cm_pending_head->next);
    }
    assert(cm_pending_num==0);
    free(cm_pending_head);
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
    int ret;
    struct ibv_sge list;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    struct ibv_wc wc;
    int ne;

    if (msg->msg_type == CM_MSG_TYPE_REP) {
        peer = msg->client_rank;
    } else {
        peer = msg->server_rank;
    }

    memcpy(cm_ud_send_buf + 40, msg, sizeof(cm_msg));
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

    ret = ibv_post_send(cm_ud_qp, &wr, &bad_wr);
    if (ret) {
        CM_ERR_ABORT("ibv_post_send to ud qp failed");
    }

    /*poll for completion */
    while (1) {
        ne = ibv_poll_cq(cm_ud_send_cq, 1, &wc);
        if (ne < 0) {
            CM_ERR_ABORT("poll CQ failed %d", ne);
        } else if (ne == 0)
            continue;

        if (wc.status != IBV_WC_SUCCESS) {
            CM_ERR_ABORT("Failed status %d for wr_id %d",
                    wc.status, (int) wc.wr_id);
        }

        if (wc.wr_id == CM_UD_SEND_WR_ID) {
            break;
        } else {
            CM_ERR_ABORT("unexpected completion, wr_id: %d",
                    (int) wc.wr_id);
        }
    }

    return MPI_SUCCESS;
}

/*functions for cm protocol*/
int cm_send_ud_msg(cm_msg * msg)
{
    int ret;
    cm_pending *pending;
    struct timeval now;

    CM_DBG("cm_send_ud_msg Enter");
    pending = cm_pending_create();
    if (cm_pending_init(pending, msg)) {
        CM_ERR_ABORT("cm_pending_init failed");
    }
    cm_pending_append(pending);

    gettimeofday(&now, NULL);
    pending->packet->timestamp = now;
    ret = cm_post_ud_packet(&(pending->packet->payload));
    if (ret) {
        CM_ERR_ABORT("cm_post_ud_packet failed %d", ret);
    }
    if (cm_pending_num == 1) {
        pthread_cond_signal(&cm_cond_new_pending);
    }
    CM_DBG("cm_send_ud_msg Exit");
    
    return MPI_SUCCESS;
}



int cm_accept(cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t * vc;
    int i;
    CM_DBG("cm_accpet Enter");

    /*Prepare QP */
    MPIDI_PG_Get_vcr(cm_ib_context.pg, msg->client_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails) {
        CM_ERR_ABORT("mismatch in number of rails");
    }

    cm_qp_create(vc);
    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    msg_send.msg_type = CM_MSG_TYPE_REP;
    for (i=0; i<msg_send.nrails; i++) {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;

    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send)) {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accpet Exit");
    return MPI_SUCCESS;
}

int cm_accept_and_cancel(cm_msg * msg)
{
    cm_msg msg_send;
    MPIDI_VC_t * vc;
    int i;
    CM_DBG("cm_accpet Enter");

    /*Prepare QP */
    MPIDI_PG_Get_vcr(cm_ib_context.pg, msg->client_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails) {
        CM_ERR_ABORT("mismatch in number of rails");
    }
    
    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);

    /*Prepare rep msg */
    memcpy(&msg_send, msg, sizeof(cm_msg));
    msg_send.msg_type = CM_MSG_TYPE_REP;
    for (i=0; i<msg_send.nrails; i++) {
        msg_send.lids[i] = vc->mrail.rails[i].lid;
        msg_send.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;
   
    /*Send rep msg */
    if (cm_send_ud_msg(&msg_send)) {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }

    CM_DBG("cm_accept_and_cancel Cancel");
    /*Cancel client role */
    {
        cm_pending *pending;
        pending =
            cm_pending_search_peer(msg->client_rank, CM_PENDING_CLIENT);
        if (NULL == pending) {
            CM_ERR_ABORT("Can't find pending entry");
        }
        cm_pending_remove_and_destroy(pending);
    }
    CM_DBG("cm_accept_and_cancel Exit");
    
    return MPI_SUCCESS;
}


int cm_enable(cm_msg * msg)
{
    MPIDI_VC_t * vc;
    CM_DBG("cm_enable Enter");

    MPIDI_PG_Get_vcr(cm_ib_context.pg, msg->server_rank, &vc);
    if (vc->mrail.num_rails != msg->nrails) {
        CM_ERR_ABORT("mismatch in number of rails");
    }

    cm_qp_move_to_rtr(vc, msg->lids, msg->qpns);
    cm_qp_move_to_rts(vc);

    /*Mark connected */
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    /*No need to send confirm and let the first message serve as confirm*/
    /*ring the door bell*/
    MPIDI_CH3I_Process.new_conn_door_bell = 1;
    CM_DBG("cm_enable Exit");
    return MPI_SUCCESS;
}

int cm_handle_msg(cm_msg * msg)
{
    CM_DBG("Handle cm_msg: msg_type: %d, client_rank %d, server_rank %d",
           msg->msg_type, msg->client_rank, msg->server_rank);
    switch (msg->msg_type) {
    case CM_MSG_TYPE_REQ:
        {
            pthread_mutex_lock(&cm_conn_state_lock);
            if (*(cm_ib_context.conn_state[msg->client_rank])
                == MPIDI_CH3I_VC_STATE_IDLE) {
                /*already existing */
                pthread_mutex_unlock(&cm_conn_state_lock);
                return MPI_SUCCESS;
            } else if (*(cm_ib_context.conn_state[msg->client_rank]) 
                == MPIDI_CH3I_VC_STATE_CONNECTING_SRV) {
                /*already a pending request from that peer */
                pthread_mutex_unlock(&cm_conn_state_lock);
                return MPI_SUCCESS;
            } else if (*(cm_ib_context.conn_state[msg->client_rank]) 
                == MPIDI_CH3I_VC_STATE_CONNECTING_CLI) {
                /*already initiated a request to that peer */
                if (msg->client_rank > cm_ib_context.rank) {
                    /*that peer should be server, ignore the request*/
                    pthread_mutex_unlock(&cm_conn_state_lock);
                    return MPI_SUCCESS;
                } else {
                    /*myself should be server */
                    cm_accept_and_cancel(msg);
                }
            }
            else {
                cm_accept(msg);
            }
            pthread_mutex_unlock(&cm_conn_state_lock);
        }
        break;
    case CM_MSG_TYPE_REP:
        {
            cm_pending *pending;
            pthread_mutex_lock(&cm_conn_state_lock);
            if (*(cm_ib_context.conn_state[msg->server_rank])
                != MPIDI_CH3I_VC_STATE_CONNECTING_CLI) {
                /*not waiting for any reply */
                pthread_mutex_unlock(&cm_conn_state_lock);
                return MPI_SUCCESS;
            }
            pending =
                cm_pending_search_peer(msg->server_rank,
                                       CM_PENDING_CLIENT);
            if (NULL == pending) {
                CM_ERR_ABORT("Can't find pending entry");
            }
            cm_pending_remove_and_destroy(pending);
            cm_enable(msg);
            pthread_mutex_unlock(&cm_conn_state_lock);
        }
        break;
    default:
        CM_ERR_ABORT("Unknown msg type: %d", msg->msg_type);
    }
    CM_DBG("cm_handle_msg Exit");
    return MPI_SUCCESS;
}

void *cm_timeout_handler(void *arg)
{
    struct timeval now;
    int delay, ret;
    cm_pending *p;
    struct timespec remain;
    while (1) {
        pthread_mutex_lock(&cm_conn_state_lock);
        while (cm_pending_num == 0) {
            pthread_cond_wait(&cm_cond_new_pending, &cm_conn_state_lock);
        }
        while (1) {
            pthread_mutex_unlock(&cm_conn_state_lock);
            nanosleep(&cm_timeout,&remain);/*Not handle the EINTR*/
            pthread_mutex_lock(&cm_conn_state_lock);
            if (cm_pending_num == 0) {
                break;
            }
            CM_DBG("Time out");
            p = cm_pending_head;
            if (NULL == p) {
                CM_ERR_ABORT("cm_pending_head corrupted");
            }
            gettimeofday(&now, NULL);
            while (p->next != cm_pending_head) {
                p = p->next;
                delay = (now.tv_sec - p->packet->timestamp.tv_sec) * 1000000
                    + (now.tv_usec - p->packet->timestamp.tv_usec);
                if (delay > cm_timeout_usec) {       /*Timer expired */
                    CM_DBG("Resend");
                    p->packet->timestamp = now;
                    ret = cm_post_ud_packet(&(p->packet->payload));
                    if (ret) {
                        CM_ERR_ABORT("cm_post_ud_packet failed %d", ret);
                    }
                    gettimeofday(&now,NULL);
                }
            }
            CM_DBG("Time out exit");
        }
        pthread_mutex_unlock(&cm_conn_state_lock);
    }
    return NULL;
}

void *cm_completion_handler(void *arg)
{
    while (1) {
        struct ibv_cq *ev_cq;
        void *ev_ctx;
        struct ibv_wc wc;
        int ne;
        int spin_count;
        int ret;

        CM_DBG("Waiting for cm message");

        do {
            ret = ibv_get_cq_event(cm_ud_comp_ch, &ev_cq, &ev_ctx);
            if (ret && errno != EINTR ) {
                CM_ERR_ABORT("Failed to get cq_event: %d", ret);
                return NULL;
            }
        } while(ret && errno == EINTR);

        ibv_ack_cq_events(ev_cq, 1);

        if (ev_cq != cm_ud_recv_cq) {
            CM_ERR_ABORT("CQ event for unknown CQ %p", ev_cq);
            return NULL;
        }

        CM_DBG("Processing cm message");
          
        spin_count = 0;
        do {
            ne = ibv_poll_cq(cm_ud_recv_cq, 1, &wc);
            if (ne < 0) {
                CM_ERR_ABORT("poll CQ failed %d", ne);
                return NULL;
            } else if (ne == 0) {
                spin_count++;
                continue;
            }

            spin_count = 0;

            if (wc.status != IBV_WC_SUCCESS) {
                CM_ERR_ABORT("Failed status %d for wr_id %d",
                        wc.status, (int) wc.wr_id);
                return NULL;
            }

            if (wc.wr_id == CM_UD_RECV_WR_ID) {
                void *buf =
                    cm_ud_recv_buf +
                    cm_ud_recv_buf_index * (sizeof(cm_msg) + 40) + 40;
                cm_msg *msg = (cm_msg *) buf;
                cm_handle_msg(msg);
                CM_DBG("Post recv");
                cm_post_ud_recv(buf - 40, sizeof(cm_msg));
                cm_ud_recv_buf_index =
                    (cm_ud_recv_buf_index + 1) % cm_recv_buffer_size;
            }
        }while (spin_count < cm_max_spin_count);

        CM_DBG("notify_cq");
        if (ibv_req_notify_cq(cm_ud_recv_cq, 1)) {
            CM_ERR_ABORT("Couldn't request CQ notification");
            return NULL;
        }
    }
    return NULL;
}

int MPICM_Init_UD(uint32_t * ud_qpn)
{
    int i, ret;
    char *value;

    /*Initialization */
    cm_ah = malloc(cm_ib_context.size * sizeof(struct ibv_ah *));
    cm_ud_qpn = malloc(cm_ib_context.size * sizeof(uint32_t));
    cm_lid = malloc(cm_ib_context.size * sizeof(uint16_t));

    cm_req_id_global = 0;

    page_size = sysconf(_SC_PAGESIZE);

    if ((value = getenv("MV2_CM_SEND_DEPTH")) != NULL) {
        cm_send_depth = atoi(value);
    } else {
        cm_send_depth = DEFAULT_CM_SEND_DEPTH;
    }

    if ((value = getenv("MV2_CM_RECV_BUFFERS")) != NULL) {
        cm_recv_buffer_size = atoi(value);
    } else {
        cm_recv_buffer_size = DEFAULT_CM_MSG_RECV_BUFFER_SIZE;
    }

    if ((value = getenv("MV2_CM_UD_PSN")) != NULL) {
        cm_ud_psn = atoi(value);
    } else {
        cm_ud_psn = CM_UD_DEFAULT_PSN;
    }

    if ((value = getenv("MV2_CM_MAX_SPIN_COUNT")) != NULL) {
        cm_max_spin_count = atoi(value);
    } else {
        cm_max_spin_count = DEFAULT_CM_MAX_SPIN_COUNT;
    }
    
    if ((value = getenv("MV2_CM_THREAD_STACKSIZE")) != NULL) {
        cm_thread_stacksize = atoi(value);
    } else {
        cm_thread_stacksize = DEFAULT_CM_THREAD_STACKSIZE;
    }
   
    if ((value = getenv("MV2_CM_TIMEOUT")) != NULL) {
        cm_timeout_usec = atoi(value)*1000;
    } else { 
        cm_timeout_usec = CM_DEFAULT_TIMEOUT;
    }
    if (cm_timeout_usec < CM_MIN_TIMEOUT) {
        cm_timeout_usec = CM_MIN_TIMEOUT;
    }

    cm_timeout.tv_sec = cm_timeout_usec/1000000;
    cm_timeout.tv_nsec = (cm_timeout_usec-cm_timeout.tv_sec*1000000)*1000;

    cm_ud_buf =
        memalign(page_size,
                 (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    if (!cm_ud_buf) {
        CM_ERR_ABORT("Couldn't allocate work buf");
    }
    
    memset(cm_ud_buf, 0,
           (sizeof(cm_msg) + 40) * (cm_recv_buffer_size + 1));
    cm_ud_send_buf = cm_ud_buf;
    cm_ud_recv_buf = cm_ud_buf + sizeof(cm_msg) + 40;

    /*use default nic*/
    cm_ud_comp_ch = ibv_create_comp_channel(MPIDI_CH3I_RDMA_Process.nic_context[0]);
    if (!cm_ud_comp_ch) {
        CM_ERR_ABORT("Couldn't create completion channel");
    }

    cm_ud_mr = ibv_reg_mr(MPIDI_CH3I_RDMA_Process.ptag[0], cm_ud_buf,
                          (sizeof(cm_msg) +
                           40) * (cm_recv_buffer_size + 1),
                          IBV_ACCESS_LOCAL_WRITE);
    if (!cm_ud_mr) {
        CM_ERR_ABORT("Couldn't allocate MR");
    }

    cm_ud_recv_cq =
        ibv_create_cq(MPIDI_CH3I_RDMA_Process.nic_context[0], cm_recv_buffer_size, NULL,
                      cm_ud_comp_ch, 0);
    if (!cm_ud_recv_cq) {
        CM_ERR_ABORT("Couldn't create CQ");
    }

    cm_ud_send_cq =
        ibv_create_cq(MPIDI_CH3I_RDMA_Process.nic_context[0], cm_send_depth, NULL, NULL, 0);
    if (!cm_ud_send_cq) {
        CM_ERR_ABORT("Couldn't create CQ");
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
        if (!cm_ud_qp) {
            CM_ERR_ABORT("Couldn't create UD QP");
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

        if ((ret = ibv_modify_qp(cm_ud_qp, &attr,
                                 IBV_QP_STATE |
                                 IBV_QP_PKEY_INDEX |
                                 IBV_QP_PORT | IBV_QP_QKEY))) {
            CM_ERR_ABORT("Failed to modify QP to INIT, ret = %d, errno=%d",
                    ret, errno);
        }
    }
    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTR;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE)) {
            CM_ERR_ABORT("Failed to modify QP to RTR");
        }
    }

    {
        struct ibv_qp_attr attr;
        memset(&attr, 0, sizeof(struct ibv_qp_attr));

        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = cm_ud_psn;
        if (ibv_modify_qp(cm_ud_qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            CM_ERR_ABORT("Failed to modify QP to RTS");
        }
    }

    for (i = 0; i < cm_recv_buffer_size; i++) {
        if (cm_post_ud_recv(cm_ud_recv_buf + 
            (sizeof(cm_msg) + 40) * i, sizeof(cm_msg))) {
            CM_ERR_ABORT("cm_post_ud_recv failed");
        }
    }
    cm_ud_recv_buf_index = 0;

    if (ibv_req_notify_cq(cm_ud_recv_cq, 1)) {
        CM_ERR_ABORT("Couldn't request CQ notification");
    }

    cm_pending_list_init();
    return MPI_SUCCESS;
}

int MPICM_Connect_UD(uint32_t * qpns, uint16_t * lids)
{
    int i, ret;
    
    /*Copy qpns and lids */
    memcpy(cm_ud_qpn, qpns, cm_ib_context.size * sizeof(uint32_t));
    memcpy(cm_lid, lids, cm_ib_context.size * sizeof(uint16_t));

    /*Create address handles */
    for (i = 0; i < cm_ib_context.size; i++) {
        struct ibv_ah_attr ah_attr;
        memset(&ah_attr, 0, sizeof(ah_attr));
        ah_attr.is_global = 0;
        ah_attr.dlid = cm_lid[i];
        ah_attr.sl = 0;
        ah_attr.src_path_bits = 0;
        ah_attr.port_num = rdma_default_port;
        cm_ah[i] = ibv_create_ah(MPIDI_CH3I_RDMA_Process.ptag[0], &ah_attr);
        if (!cm_ah[i]) {
            CM_ERR_ABORT("Failed to create AH");
        }
    }

    /*Spawn cm thread */
    {
        pthread_attr_t attr;
        if (pthread_attr_init(&attr))
        {
            CM_ERR_ABORT("pthread_attr_init failed\n");
        }
        ret = pthread_attr_setstacksize(&attr,cm_thread_stacksize);
        if (ret && ret != EINVAL) {
            CM_ERR_ABORT("pthread_attr_setstacksize failed\n");
        }
        pthread_create(&cm_comp_thread, &attr, cm_completion_handler, NULL);
        pthread_create(&cm_timer_thread, &attr, cm_timeout_handler, NULL);
    }
    return MPI_SUCCESS;
}

int MPICM_Finalize_UD()
{
    int i;

    CM_DBG("In MPICM_Finalize_UD");

    cm_pending_list_finalize();
    /*Cancel cm thread */

    pthread_cancel(cm_comp_thread);
    pthread_cancel(cm_timer_thread);

    /*Clean up */
    for (i = 0; i < cm_ib_context.size; i++)
        ibv_destroy_ah(cm_ah[i]);
    ibv_destroy_qp(cm_ud_qp);

    ibv_destroy_comp_channel(cm_ud_comp_ch);
    ibv_destroy_cq(cm_ud_recv_cq);
    ibv_destroy_cq(cm_ud_send_cq);
    ibv_dereg_mr(cm_ud_mr);

    free(cm_ud_buf);
    free(cm_ah);
    free(cm_ud_qpn);
    free(cm_lid);

    CM_DBG("MPICM_Finalize_UD done");
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Connect(MPIDI_VC_t * vc)
{
    cm_msg msg;
    int i;
    pthread_mutex_lock(&cm_conn_state_lock);
    if (vc->ch.state != MPIDI_CH3I_VC_STATE_UNCONNECTED) {
        pthread_mutex_unlock(&cm_conn_state_lock);
        return MPI_SUCCESS;
    }
    if (vc->pg_rank == cm_ib_context.rank) {
        pthread_mutex_unlock(&cm_conn_state_lock);
        return MPI_SUCCESS;
    }

    CM_DBG("Sending Req to rank %d", vc->pg_rank);
    /*Create qps*/
    cm_qp_create(vc);
    msg.server_rank = vc->pg_rank;
    msg.client_rank = cm_ib_context.rank;
    msg.msg_type = CM_MSG_TYPE_REQ;
    msg.req_id = ++cm_req_id_global;
    msg.nrails = vc->mrail.num_rails;
    for (i=0;i<msg.nrails;i++) {
        msg.lids[i] = vc->mrail.rails[i].lid;
        msg.qpns[i] = vc->mrail.rails[i].qp_hndl->qp_num;
    }

    if (cm_send_ud_msg(&msg)) {
        CM_ERR_ABORT("cm_send_ud_msg failed");
    }
    
    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;
    pthread_mutex_unlock(&cm_conn_state_lock);
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Establish(MPIDI_VC_t * vc)
{
    /*This function should be called when VC received the 
    first message in on-demand case */
    cm_pending *pending;
    CM_DBG("In MPIDI_CH3I_CM_Establish");
    pthread_mutex_lock(&cm_conn_state_lock);
    if (vc->ch.state !=
        MPIDI_CH3I_VC_STATE_CONNECTING_SRV) {
        /*not waiting for comfirm */
        pthread_mutex_unlock(&cm_conn_state_lock);
        return MPI_SUCCESS;
    }
    pending = cm_pending_search_peer(vc->pg_rank, CM_PENDING_SERVER);
    if (NULL == pending) {
        CM_ERR_ABORT("Can't find pending entry");
    }
    cm_pending_remove_and_destroy(pending);
    cm_qp_move_to_rts(vc);
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    pthread_mutex_unlock(&cm_conn_state_lock);
    return MPI_SUCCESS;
}
