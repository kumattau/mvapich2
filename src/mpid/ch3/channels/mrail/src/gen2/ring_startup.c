/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3i_rdma_conf.h"

#include <mpimem.h>
#include <netdb.h>
#include <string.h>

#include "rdma_impl.h"
#include "pmi.h"
#include "ibv_param.h"

#define MPD_WINDOW 10
#define ADDR_PKT_SIZE (sizeof(struct addr_packet) + ((pg_size - 1) * sizeof(struct host_addr_inf)))
#define ADDR_INDEX(_p, _i) ((struct addr_packet *)(_p + (_i * ADDR_PKT_SIZE)))

#define IBA_PMI_ATTRLEN (16)
#define IBA_PMI_VALLEN  (4096)

struct init_addr_inf {
    uint16_t    lid;
    uint32_t    qp_num[2];
};

struct host_addr_inf {
    uint32_t    sr_qp_num;
    uint32_t    osc_qp_num;
};

struct addr_packet {
    int         rank;
    int         host_id;
    int         lid;
    int         rail;
    uint32_t    hca_type;
    struct host_addr_inf val[1];
};

struct ring_packet {
    int     type;
    int     value;
};

/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
/*
 * TODO add error handling
 */
static inline int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    host_id = (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;

    return host_id;
}

static uint16_t get_local_lid(struct ibv_context * ctx, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(ctx, port, &attr)) {
        return -1;
    }

    MPIDI_CH3I_RDMA_Process.lmc = attr.lmc;

    return attr.lid;
}

static inline int round_left(int current, int size)
{
    return current == 0 ? size - 1 : current - 1;
}

static inline int is_A_on_left_of_B(int a, int b, int rank, int size)
{
    int dist_a = (rank - a + size) % size;
    int dist_b = (rank - b + size) % size;
    return dist_a > dist_b;
}

/* Exchange address info with other processes in the job.
 * MPD provides the ability for processes within the job to
 * publish information which can then be querried by other
 * processes.  It also provides a simple barrier sync.
 */
static int _rdma_pmi_exchange_addresses(int pg_rank, int pg_size,
                                       void *localaddr, int addrlen, 
                                       void *alladdrs)
{
    int     ret, i, j, lhs, rhs, len_local, len_remote, key_max_sz, val_max_sz;
    char    attr_buff[IBA_PMI_ATTRLEN];
    char    val_buff[IBA_PMI_VALLEN];
    char    *temp_localaddr = (char *) localaddr;
    char    *temp_alladdrs = (char *) alladdrs;
    char    *key, *val;
    char    *kvsname = NULL;

    /* Allocate space for pmi keys and values */
    ret = PMI_KVS_Get_key_length_max(&key_max_sz);
    CHECK_UNEXP((ret != PMI_SUCCESS), "Could not get KVS key length");

    key_max_sz++;
    key = MPIU_Malloc(key_max_sz);
    CHECK_UNEXP((key == NULL), "Could not get key \n");

    ret = PMI_KVS_Get_value_length_max(&val_max_sz);
    CHECK_UNEXP((ret != PMI_SUCCESS), "Could not get KVS value length");
    val_max_sz++;

    val = MPIU_Malloc(val_max_sz);
    CHECK_UNEXP((val == NULL), "Could not get val \n");
    len_local = strlen(temp_localaddr);

    /* TODO: Double check the value of value */
    CHECK_UNEXP((len_local > val_max_sz), "local address length is larger then string length");

    /* Be sure to use different keys for different processes */
    memset(attr_buff, 0, IBA_PMI_ATTRLEN * sizeof(char));
    snprintf(attr_buff, IBA_PMI_ATTRLEN, "MVAPICH2_%04d", pg_rank);

    /* put the kvs into PMI */
    MPIU_Strncpy(key, attr_buff, key_max_sz);
    MPIU_Strncpy(val, temp_localaddr, val_max_sz);
    MPIDI_PG_GetConnKVSname( &kvsname );
    ret = PMI_KVS_Put(kvsname, key, val);

    CHECK_UNEXP((ret != 0), "PMI_KVS_Put error \n");

    ret = PMI_KVS_Commit(kvsname);
    CHECK_UNEXP((ret != 0), "PMI_KVS_Commit error \n");

    /* Wait until all processes done the same */
    ret = PMI_Barrier();
    CHECK_UNEXP((ret != 0), "PMI_Barrier error \n");
    lhs = (pg_rank + pg_size - 1) % pg_size;
    rhs = (pg_rank + 1) % pg_size;

    for (i = 0; i < 2; i++) {
        /* get lhs and rhs processes' data */
        j = (i == 0) ? lhs : rhs;
        /* Use the key to extract the value */
        memset(attr_buff, 0, IBA_PMI_ATTRLEN * sizeof(char));
        memset(val_buff, 0, IBA_PMI_VALLEN * sizeof(char));
        snprintf(attr_buff, IBA_PMI_ATTRLEN, "MVAPICH2_%04d", j);
        MPIU_Strncpy(key, attr_buff, key_max_sz);

        ret = PMI_KVS_Get(kvsname, key, val, val_max_sz);
        CHECK_UNEXP((ret != 0), "PMI_KVS_Get error \n");
        MPIU_Strncpy(val_buff, val, val_max_sz);

        /* Simple sanity check before stashing it to the alladdrs */
        len_remote = strlen(val_buff);
        CHECK_UNEXP((len_remote < len_local), "remote length is smaller than local length");
        strncpy(temp_alladdrs, val_buff, len_local);
        temp_alladdrs += len_local;
    }

    /* Free the key-val pair */
    MPIU_Free(key);
    MPIU_Free(val);

    /* this barrier is to prevent some process from overwriting values that
       has not been get yet */
    ret = PMI_Barrier();
    CHECK_UNEXP((ret != 0), "PMI_Barrier error \n");
    return 0;
}


static struct ibv_qp *create_qp(struct ibv_pd *pd, 
                                struct ibv_cq *scq, struct ibv_cq *rcq)
{
    struct ibv_qp_init_attr boot_attr;

    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = rdma_max_inline_size;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = scq;
    boot_attr.recv_cq = rcq;

    return ibv_create_qp(pd, &boot_attr);
}

static int _find_active_port(struct ibv_context *context) 
{
    struct ibv_port_attr port_attr;
    int j;

    for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; ++ j) {
        if ((! ibv_query_port(context, j, &port_attr)) &&
             port_attr.state == IBV_PORT_ACTIVE &&
             port_attr.lid) {
            return j;
        }
    }

    return -1;
}

static int _setup_ib_boot_ring(struct init_addr_inf * neighbor_addr,
                              struct MPIDI_CH3I_RDMA_Process_t *proc,
                              int port)
{
    struct ibv_qp_attr      qp_attr;
    uint32_t    qp_attr_mask = 0;
    int         i;
    int         ret;
    qp_attr.qp_state        = IBV_QPS_INIT;
    set_pkey_index(&qp_attr.pkey_index, 0, port);
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    qp_attr.port_num        = port;

    DEBUG_PRINT("default port %d, qpn %x\n", port,
            proc->boot_qp_hndl[0]->qp_num);

    ret = ibv_modify_qp(proc->boot_qp_hndl[0],&qp_attr,(IBV_QP_STATE
                        | IBV_QP_PKEY_INDEX
                        | IBV_QP_PORT
                        | IBV_QP_ACCESS_FLAGS));
    CHECK_RETURN(ret, "Could not modify boot qp to INIT");

    ret = ibv_modify_qp(proc->boot_qp_hndl[1],&qp_attr,(IBV_QP_STATE
                        | IBV_QP_PKEY_INDEX
                        | IBV_QP_PORT
                        | IBV_QP_ACCESS_FLAGS));
    CHECK_RETURN(ret, "Could not modify boot qp to INIT");

    /**********************  INIT --> RTR  ************************/
    memset(&qp_attr, 0, sizeof qp_attr);
    qp_attr.qp_state    =   IBV_QPS_RTR;
    qp_attr.path_mtu    =   rdma_default_mtu;
    qp_attr.rq_psn      =   rdma_default_psn;
    qp_attr.max_dest_rd_atomic  =   rdma_default_max_rdma_dst_ops;
    qp_attr.min_rnr_timer       =   rdma_default_min_rnr_timer;
    qp_attr.ah_attr.is_global   =   0;
    qp_attr.ah_attr.sl          =   rdma_default_service_level;
    qp_attr.ah_attr.static_rate =   rdma_default_static_rate;
    qp_attr.ah_attr.src_path_bits   =   rdma_default_src_path_bits;
    qp_attr.ah_attr.port_num    =   port;

    qp_attr_mask        |=  IBV_QP_STATE;
    qp_attr_mask        |=  IBV_QP_PATH_MTU;
    qp_attr_mask        |=  IBV_QP_RQ_PSN;
    qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
    qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
    qp_attr_mask        |=  IBV_QP_AV;

    /* lhs */
    for (i = 0; i < 2; i++) {
        qp_attr.dest_qp_num     = neighbor_addr[i].qp_num[1 - i];
        qp_attr.ah_attr.dlid    = neighbor_addr[i].lid;
        qp_attr_mask            |=  IBV_QP_DEST_QPN;

        ret = ibv_modify_qp(proc->boot_qp_hndl[i],&qp_attr, qp_attr_mask);
        CHECK_RETURN(ret, "Could not modify boot qp to RTR");

        DEBUG_PRINT("local QP=%x\n", proc->boot_qp_hndl[i]->qp_num);
    }

    /************** RTS *******************/
    memset(&qp_attr, 0, sizeof qp_attr);
    qp_attr.qp_state        = IBV_QPS_RTS;
    qp_attr.sq_psn          = rdma_default_psn;
    qp_attr.timeout         = rdma_default_time_out;
    qp_attr.retry_cnt       = rdma_default_retry_count;
    qp_attr.rnr_retry       = rdma_default_rnr_retry;
    qp_attr.max_rd_atomic   = rdma_default_qp_ous_rd_atom;

    qp_attr_mask = 0;
    qp_attr_mask =    IBV_QP_STATE              |
                      IBV_QP_TIMEOUT            |
                      IBV_QP_RETRY_CNT          |
                      IBV_QP_RNR_RETRY          |
                      IBV_QP_SQ_PSN             |
                      IBV_QP_MAX_QP_RD_ATOMIC;

    ret = ibv_modify_qp(proc->boot_qp_hndl[0],&qp_attr,qp_attr_mask);
        CHECK_RETURN(ret, "Could not modify boot qp to RTS");
    ret = ibv_modify_qp(proc->boot_qp_hndl[1],&qp_attr,qp_attr_mask);
        CHECK_RETURN(ret, "Could not modify boot qp to RTS");

    DEBUG_PRINT("Modified to RTS..Qp\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME rdma_setup_startup_ring
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_setup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank,
                        int pg_size)
{
    struct init_addr_inf neighbor_addr[2];
    char ring_qp_out[64];
    char ring_qp_in[128];
    int bootstrap_len;
    int mpi_errno = MPI_SUCCESS;
    int port;

    port = _find_active_port(proc->nic_context[0]);
    if (port < 0) {
        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto out, "**fail",
                "**fail %s", "could not find active port");
    }

    proc->boot_cq_hndl = ibv_create_cq(proc->nic_context[0],
                                       rdma_default_max_cq_size,
                                       NULL, NULL, 0);
    if (!proc->boot_cq_hndl) {
        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto out,
                "**fail", "**fail %s", "cannot create cq");
    }

    proc->boot_qp_hndl[0] = create_qp(proc->ptag[0], proc->boot_cq_hndl,
                                      proc->boot_cq_hndl);
    if (!proc->boot_qp_hndl[0]) {
        MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto out,
                "**fail", "%s%d", "Fail to create qp on rank ", pg_rank);
    }

    proc->boot_qp_hndl[1] = create_qp(proc->ptag[0], proc->boot_cq_hndl,
                                      proc->boot_cq_hndl);
    if (!proc->boot_qp_hndl[1]) {
        MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto out,
                "**fail", "%s%d", "Fail to create qp on rank ", pg_rank);
    }

    sprintf(ring_qp_out, "%08x:%08x:%08x:",
             get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0],
                           port),
             proc->boot_qp_hndl[0]->qp_num,
             proc->boot_qp_hndl[1]->qp_num
           );

    DEBUG_PRINT("After setting LID: %d, qp0: %x, qp1: %x\n",
            get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0],
                          port),
            proc->boot_qp_hndl[0]->qp_num,
            proc->boot_qp_hndl[1]->qp_num
            );

    bootstrap_len = strlen(ring_qp_out);
    _rdma_pmi_exchange_addresses(pg_rank, pg_size, ring_qp_out,
            bootstrap_len, ring_qp_in);

    sscanf(&ring_qp_in[0], "%08x:%08x:%08x:", 
           &neighbor_addr[0].lid, 
           &neighbor_addr[0].qp_num[0],
           &neighbor_addr[0].qp_num[1]);
    sscanf(&ring_qp_in[27], "%08x:%08x:%08x:",
           &neighbor_addr[1].lid, 
           &neighbor_addr[1].qp_num[0],
           &neighbor_addr[1].qp_num[1]);

    mpi_errno = _setup_ib_boot_ring(neighbor_addr, proc, port);

out:
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME rdma_cleanup_startup_ring
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_cleanup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int mpi_errno = MPI_SUCCESS;

    PMI_Barrier();
    
    if(ibv_destroy_qp(proc->boot_qp_hndl[0])) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "could not destroy lhs QP");
    }

    if(ibv_destroy_qp(proc->boot_qp_hndl[1])) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "could not destroy rhs QP");
    }

    if(ibv_destroy_cq(proc->boot_cq_hndl)) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "could not destroy CQ");
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME rdma_ring_based_allgather
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void rdma_ring_based_allgather(void *sbuf, int data_size,
        int pg_rank, void *rbuf, int pg_size,
        struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    struct ibv_mr * addr_hndl;
    int i;

    addr_hndl = ibv_reg_mr(proc->ptag[0],
            rbuf, data_size*pg_size,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

    if(addr_hndl == NULL) {
        ibv_error_abort(GEN_EXIT_ERR,"ibv_reg_mr failed for addr_hndl\n");
    }

    DEBUG_PRINT("val of addr_pool is: %p, handle: %08x\n",
            rbuf, addr_hndl->handle);

    /* Now start exchanging data*/
    {
        int recv_post_index = round_left(pg_rank,pg_size);
        int send_post_index = pg_rank;
        int recv_comp_index = pg_rank;
        int send_comp_index = -1;
        int credit = MPD_WINDOW/2;

        /* work entries related variables */
        struct ibv_recv_wr rr;
        struct ibv_sge sg_entry_r;
        struct ibv_recv_wr *bad_wr_r;
        struct ibv_send_wr sr;
        struct ibv_sge sg_entry_s;
        struct ibv_send_wr *bad_wr_s;

        /* completion related variables */
        struct ibv_wc rc;

        char* rbufProxy = (char*) rbuf;

        /* copy self data*/
        memcpy(rbufProxy+data_size*pg_rank, sbuf, data_size);

        /* post receive*/
        for(i = 0; i < MPD_WINDOW; i++) {
            if (recv_post_index == pg_rank)
                continue;
            rr.wr_id   = recv_post_index;
            rr.num_sge = 1;
            rr.sg_list = &(sg_entry_r);
            rr.next    = NULL;
            sg_entry_r.lkey = addr_hndl->lkey;
            sg_entry_r.addr = (uintptr_t)(rbufProxy+data_size*recv_post_index);
            sg_entry_r.length = data_size;

            if(ibv_post_recv(proc->boot_qp_hndl[0], &rr, &bad_wr_r)) {
                ibv_error_abort(GEN_EXIT_ERR,"Error posting recv!\n");
            }
            recv_post_index = round_left(recv_post_index,pg_size);
        }

        PMI_Barrier();

        /* sending and receiving*/
        while (recv_comp_index !=
                (pg_rank+1)%pg_size ||
                send_comp_index !=
                (pg_rank+2)%pg_size+pg_size) {
            int ne;
            /* Three conditions
             * 1: not complete sending
             * 2: has received the data
             * 3: has enough credit
             */
            if (send_post_index != (pg_rank+1)%pg_size &&
                    (recv_comp_index == send_post_index ||
                 is_A_on_left_of_B(recv_comp_index,
                     send_post_index,pg_rank,pg_size)) && credit > 0) {

                sr.opcode         = IBV_WR_SEND;
                sr.send_flags     = IBV_SEND_SIGNALED;
                sr.wr_id          = send_post_index+pg_size;
                sr.num_sge        = 1;
                sr.sg_list        = &sg_entry_s;
                sr.next           = NULL;
                sg_entry_s.addr   = (uintptr_t)(rbufProxy+data_size*send_post_index);
                sg_entry_s.length = data_size;
                sg_entry_s.lkey   = addr_hndl->lkey;

                if (ibv_post_send(proc->boot_qp_hndl[1], &sr, &bad_wr_s)) {
                    ibv_error_abort(GEN_EXIT_ERR, "Error posting send!\n");
                }

                send_post_index=round_left(send_post_index,pg_size);
                credit--;
            }

            ne = ibv_poll_cq(proc->boot_cq_hndl, 1, &rc);
            if (ne < 0) {
                ibv_error_abort(GEN_EXIT_ERR, "Poll CQ failed!\n");
            } else if (ne > 1) {
                ibv_error_abort(GEN_EXIT_ERR, "Got more than one\n");
            } else if (ne == 1) {
                if (rc.status != IBV_WC_SUCCESS) {
                    if(rc.status == IBV_WC_RETRY_EXC_ERR) {
                        DEBUG_PRINT("Got IBV_WC_RETRY_EXC_ERR\n");
                    }
                    ibv_error_abort(GEN_EXIT_ERR,"Error code in polled desc!\n");
                }
                if (rc.wr_id < pg_size) {
                    /*recv completion*/
                    recv_comp_index = round_left(recv_comp_index,pg_size);
                    MPIU_Assert(recv_comp_index == rc.wr_id);
                    if (recv_post_index != pg_rank) {
                        rr.wr_id   = recv_post_index;
                        rr.num_sge = 1;
                        rr.sg_list = &(sg_entry_r);
                        rr.next    = NULL;
                        sg_entry_r.lkey = addr_hndl->lkey;
                        sg_entry_r.addr = (uintptr_t)(rbufProxy+data_size*recv_post_index);
                        sg_entry_r.length = data_size;

                        if(ibv_post_recv(proc->boot_qp_hndl[0], &rr, &bad_wr_r)) {
                            ibv_error_abort(GEN_EXIT_ERR,"Error posting recv!\n");
                        }

                        recv_post_index = round_left(recv_post_index,pg_size);
                    }
                }
                else {
                    /*send completion*/
                    credit++;
                    send_comp_index = rc.wr_id;
                }
            }
        }

        /*Now all send and recv finished*/
    }

    ibv_dereg_mr(addr_hndl);
}

static void _ring_boot_exchange(struct ibv_mr * addr_hndl, void * addr_pool,
        struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank, int pg_size,
        struct process_init_info *info)
{
    int i, ne, index_to_send, rail_index;
    int hostid;
    char hostname[HOSTNAME_LEN + 1];

    uint64_t last_send = 0;
    uint64_t last_send_comp = 0;

    struct addr_packet * send_packet;
    struct addr_packet * recv_packet;

    /* work entries related variables */
    struct ibv_recv_wr rr;
    struct ibv_sge sg_entry_r;
    struct ibv_recv_wr *bad_wr_r;
    struct ibv_send_wr sr;
    struct ibv_sge sg_entry_s;
    struct ibv_send_wr *bad_wr_s;

    /* completion related variables */
    struct ibv_wc rc;
    MPIDI_VC_t * vc;

    /* Post the window of recvs: The first entry
     * is not posted since it is used for the
     * initial send
     */

    DEBUG_PRINT("Posting recvs\n");
    char* addr_poolProxy = (char*) addr_pool;

    for(i = 1; i < MPD_WINDOW; i++) {
        rr.wr_id   = i;
        rr.num_sge = 1;
        rr.sg_list = &(sg_entry_r);
        rr.next    = NULL;
        sg_entry_r.lkey = addr_hndl->lkey;
        sg_entry_r.addr = (uintptr_t) ADDR_INDEX(addr_poolProxy, i);
        sg_entry_r.length = ADDR_PKT_SIZE;

        if(ibv_post_recv(proc->boot_qp_hndl[0], &rr, &bad_wr_r)) {
            ibv_error_abort(GEN_EXIT_ERR,"Error posting recv!\n");
        }
    }

    DEBUG_PRINT("done posting recvs\n");

    index_to_send = 0;

    /* get hostname stuff */

    gethostname(hostname, HOSTNAME_LEN);
    if (!hostname) {
        fprintf(stderr, "Could not get hostname\n");
        exit(1);
    }
    hostid = get_host_id(hostname, HOSTNAME_LEN);

    /* send information for each rail */

    DEBUG_PRINT("rails: %d\n", rdma_num_rails);

    PMI_Barrier();

    for(rail_index = 0; rail_index < rdma_num_rails; rail_index++) {

        DEBUG_PRINT("doing rail %d\n", rail_index);

        send_packet          = ADDR_INDEX(addr_poolProxy, index_to_send);
        send_packet->rank    = pg_rank;
        send_packet->rail    = rail_index;
        send_packet->host_id = hostid;

        for(i = 0; i < pg_size; i++) {
            if(i == pg_rank) {
                send_packet->val[i].sr_qp_num = -1;

                if (proc->has_one_sided)
                {
                    send_packet->val[i].osc_qp_num = -1;
                }
                info->hca_type[i] = MPIDI_CH3I_RDMA_Process.hca_type;
            } else {
                MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

                send_packet->lid     = vc->mrail.rails[rail_index].lid;
                send_packet->val[i].sr_qp_num =
                    vc->mrail.rails[rail_index].qp_hndl->qp_num;
                send_packet->hca_type = MPIDI_CH3I_RDMA_Process.hca_type;
            }
        }

        DEBUG_PRINT("starting to do sends\n");
        for(i = 0; i < pg_size - 1; i++) {

            sr.opcode         = IBV_WR_SEND;
            sr.send_flags     = IBV_SEND_SIGNALED;
            sr.wr_id          = MPD_WINDOW + index_to_send;
            sr.num_sge        = 1;
            sr.sg_list        = &sg_entry_s;
            sr.next           = NULL;
            sg_entry_s.addr   = (uintptr_t)
                ADDR_INDEX(addr_poolProxy, index_to_send);
            sg_entry_s.length = ADDR_PKT_SIZE;
            sg_entry_s.lkey   = addr_hndl->lkey;

            /* keep track of the last send... */
            last_send         = sr.wr_id;

            if (ibv_post_send(proc->boot_qp_hndl[1], &sr, &bad_wr_s)) {
                ibv_error_abort(GEN_EXIT_ERR, "Error posting send!\n");
            }

            /* flag that keeps track if we are waiting
             * for a recv or more credits
             */

            while(1) {
                ne = ibv_poll_cq(proc->boot_cq_hndl, 1, &rc);
                if (ne < 0) {
                    ibv_error_abort(GEN_EXIT_ERR, "Poll CQ failed!\n");
                } else if (ne > 1) {
                    ibv_error_abort(GEN_EXIT_ERR, "Got more than one\n");
                } else if (ne == 1) {
                    if (rc.status != IBV_WC_SUCCESS) {
                        if(rc.status == IBV_WC_RETRY_EXC_ERR) {
                            DEBUG_PRINT("Got IBV_WC_RETRY_EXC_ERR\n");
                        }
                        ibv_error_abort(GEN_EXIT_ERR,"Error code in polled desc!\n");
                    }

                    if (rc.wr_id < MPD_WINDOW) {
                        /* completion of recv */

                        recv_packet = ADDR_INDEX(addr_poolProxy, rc.wr_id);


                        info->lid[recv_packet->rank][rail_index] =
                            recv_packet->lid;
                        info->hostid[recv_packet->rank][rail_index] =
                            recv_packet->host_id;
                        info->hca_type[recv_packet->rank] =
                            recv_packet->hca_type;

                        MPIDI_PG_Get_vc(g_cached_pg, recv_packet->rank, &vc);
                        vc->smp.hostid = recv_packet->host_id;

                        info->qp_num_rdma[recv_packet->rank][rail_index] =
                            recv_packet->val[pg_rank].sr_qp_num;

                        if (proc->has_one_sided)
                        {
                            info->qp_num_onesided[recv_packet->rank][rail_index] = recv_packet->val[pg_rank].osc_qp_num;
                        }

                        /* queue this for sending to the next
                         * hop in the ring
                         */
                        index_to_send = rc.wr_id;

                        break;
                    } else {
                        /* completion of send */
                        last_send_comp = rc.wr_id;

                        /* now post as recv */
                        rr.wr_id   = rc.wr_id - MPD_WINDOW;
                        rr.num_sge = 1;
                        rr.sg_list = &(sg_entry_r);
                        rr.next    = NULL;
                        sg_entry_r.lkey = addr_hndl->lkey;
                        sg_entry_r.addr = (uintptr_t)
                            ADDR_INDEX(addr_poolProxy, rr.wr_id);
                        sg_entry_r.length = ADDR_PKT_SIZE;
                        if(ibv_post_recv(proc->boot_qp_hndl[0], &rr, &bad_wr_r)) {
                            ibv_error_abort(GEN_EXIT_ERR,
                                    "Error posting recv!\n");
                        }
                    }
                }
            }

        }
    } /* end for(rail_index... */

    /* Make sure all sends have completed */

    while(last_send_comp != last_send) {
        ne = ibv_poll_cq(proc->boot_cq_hndl, 1, &rc);
        if(ne == 1) {
            if (rc.status != IBV_WC_SUCCESS) {
                ibv_va_error_abort(GEN_EXIT_ERR,"Error code %d in polled desc!\n",
                                rc.status);
            }
            last_send_comp = rc.wr_id;
        }
    }
}

#undef FUNCNAME
#define FUNCNAME rdma_ring_boot_exchange
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void rdma_ring_boot_exchange(struct MPIDI_CH3I_RDMA_Process_t *proc,
                      int pg_rank, int pg_size, struct process_init_info *info)
{
    struct ibv_mr * addr_hndl;
    void * addr_pool;

    addr_pool = MPIU_Malloc(MPD_WINDOW * ADDR_PKT_SIZE);
    addr_hndl = ibv_reg_mr(proc->ptag[0],
            addr_pool, MPD_WINDOW * ADDR_PKT_SIZE,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

    if(addr_hndl == NULL) {
        ibv_error_abort(GEN_EXIT_ERR,"ibv_reg_mr failed for addr_hndl\n");
    }

    _ring_boot_exchange(addr_hndl, addr_pool, proc, pg_rank, pg_size, info);

    ibv_dereg_mr(addr_hndl);
    MPIU_Free(addr_pool);
}

