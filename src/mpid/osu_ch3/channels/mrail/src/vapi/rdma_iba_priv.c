/* Copyright (c) 2003-2007, The Ohio State University. All rights
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

#include "rdma_impl.h"
#include "pmi.h"
#include "vapi_util.h"
#include "vapi_param.h"

#ifndef MAC_OSX
#include <malloc.h>
#else
#include <netinet/in.h>
#endif

#include <netdb.h>
#include <string.h>

#include "vbuf.h"

#undef DEBUG_PRINT

#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif


#ifdef ONE_SIDED
#define QPLEN_XDR       (2*8+2*16+4)    /* 52-bytes */
#else
#define QPLEN_XDR       (8+2*16+3)      /* 43-bytes */
#endif

#define UNIT_QPLEN      (8)
#define IBA_PMI_ATTRLEN (16)
#define IBA_PMI_VALLEN  (4096)

static VAPI_qp_hndl_t dst_qp_handle;
static int dst_qp;

/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
static inline int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    host_id = (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;
    return host_id;
}

#ifdef USE_MPD_RING
int rdma_iba_bootstrap_cleanup(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int ret;
    /* Free all the temorary memory used for MPD_RING */
    ret = VAPI_deregister_mr(proc->nic[0], proc->boot_mem_hndl.hndl);
    free(proc->boot_mem);

    ret = VAPI_destroy_qp(proc->nic[0], proc->boot_qp_hndl[0]);
    CHECK_RETURN(ret, "could not destroy lhs QP");

    ret = VAPI_destroy_qp(proc->nic[0], proc->boot_qp_hndl[1]);
    CHECK_RETURN(ret, "could not destroy rhs QP");

    ret = VAPI_destroy_cq(proc->nic[0], proc->boot_cq_hndl);
    CHECK_RETURN(ret, "could not destroy CQ");
    return ret;
}


/* A ring-based barrier: process 0 initiates two tokens 
 * going clockwise and counter-clockwise, respectively.
 * The return of these two tokens completes the barrier.
 */
int bootstrap_barrier(struct MPIDI_CH3I_RDMA_Process_t *proc,
                      int pg_rank, int pg_size)
{
    /* work entries related variables */
    VAPI_sr_desc_t sr;
    VAPI_sg_lst_entry_t sg_entry_s;

    /* completion related variables */
    VAPI_cq_hndl_t cq_hndl;
    VAPI_wc_desc_t rc;

    /* Memory related variables */
    VIP_MEM_HANDLE *mem_handle;
    int unit_length;
    char *barrier_buff;
    char *send_addr;
    char *temp_buff;
    int barrier_recvd = 0;
    int ret;

    /* Find out the barrier address */
    barrier_buff = proc->boot_mem;
    unit_length = UNIT_QPLEN * (pg_size);
    temp_buff = (char *) barrier_buff + pg_rank * unit_length;
    mem_handle = &proc->boot_mem_hndl;

    sr.opcode = VAPI_SEND;
    sr.comp_type = VAPI_SIGNALED;
    sr.sg_lst_len = 1;
    sr.sg_lst_p = &sg_entry_s;
    sr.remote_qkey = 0;
    sg_entry_s.lkey = mem_handle->lkey;
    cq_hndl = proc->boot_cq_hndl;

    if (pg_rank == 0) {
        /* send_lhs(); send_rhs(); recv_lhs(); recv_rhs(); */
        sr.id = 0;
        sg_entry_s.len = UNIT_QPLEN;
        send_addr = temp_buff + unit_length;
        snprintf(send_addr, UNIT_QPLEN, "000");
        sg_entry_s.addr = (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
        ret = VAPI_post_sr(proc->nic[0], proc->boot_qp_hndl[1], &sr);
        CHECK_RETURN(ret, "Error posting send!\n");

        sr.id = 1;
        sg_entry_s.len = 2 * UNIT_QPLEN;
        send_addr = temp_buff + unit_length + UNIT_QPLEN;
        snprintf(send_addr, 2 * UNIT_QPLEN, "111");
        sg_entry_s.addr = (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
        ret = VAPI_post_sr(proc->nic[0], proc->boot_qp_hndl[0], &sr);
        CHECK_RETURN(ret, "Error posting second send!\n");

        while (barrier_recvd < 2) {
            ret = VAPI_CQ_EMPTY;
            rc.opcode = VAPI_CQE_INVAL_OPCODE;

            /* Polling CQ */
            ret = VAPI_poll_cq(proc->nic[0], cq_hndl, &rc);
            if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA) {
                DEBUG_PRINT("Receive forward %s reverse %s id %d \n",
                            (char *) temp_buff,
                            (char *) (temp_buff + UNIT_QPLEN),
                            (int) rc.id);

                barrier_recvd++;
            } else if (ret == VAPI_EINVAL_HCA_HNDL
                       || ret == VAPI_EINVAL_CQ_HNDL) {
                fprintf(stderr, "[%d](%s:%d)invalid send CQ or DEV hndl\n",
                        pg_rank, __FILE__, __LINE__);
                fflush(stderr);
            }
        }                       /* end of while loop */
    } else {
        /* recv_lhs(); send_rhs(); recv_rhs(); send_lhs(); */
        while (barrier_recvd < 2) {
            ret = VAPI_CQ_EMPTY;
            rc.opcode = VAPI_CQE_INVAL_OPCODE;

            /* Polling CQ */
            ret = VAPI_poll_cq(proc->nic[0], cq_hndl, &rc);
            if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA) {
                DEBUG_PRINT("Received forward %s reverse %s id %d\n",
                            (char *) temp_buff,
                            (char *) (temp_buff + UNIT_QPLEN),
                            (int) rc.id);

                barrier_recvd++;

                /* find out which one is received, and forward accordingly */
                sr.id = rc.id + 1 - pg_size;
                sg_entry_s.len = (sr.id + 1) * UNIT_QPLEN;
                send_addr = temp_buff + sr.id * UNIT_QPLEN;
                sg_entry_s.addr =
                    (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
                ret = VAPI_post_sr(proc->nic[0],
                                   proc->boot_qp_hndl[1 - sr.id], &sr);
                CHECK_RETURN(ret, "Error posting second send!\n");
            } else if (ret == VAPI_EINVAL_HCA_HNDL
                       || ret == VAPI_EINVAL_CQ_HNDL) {
                fprintf(stderr, "[%d](%s:%d)invalid CQ or DEV hndl\n",
                        pg_rank, __FILE__, __LINE__);
                fflush(stderr);
            }
        }
    }
    return 0;
}


/* Set up a ring of qp's, here a separate bootstrap channel is used.*/
static void ib_vapi_enable_ring(int lhs, int rhs, char *ring_addr)
{

    VAPI_qp_attr_t qp_attr;
    VAPI_qp_cap_t qp_cap;
    VAPI_qp_attr_mask_t qp_attr_mask;

    int i;
    VAPI_ret_t ret;

    /* Get lhs and rhs hca_lid and qp # */
    char temp_str[UNIT_QPLEN + 1];
    char *temp_ptr;

    MPIDI_CH3I_RDMA_Process_t *proc;

    proc = &MPIDI_CH3I_RDMA_Process;
    temp_str[UNIT_QPLEN] = '\0';

    for (i = 0; i < 2; i++) {

        /* hca_lid + lhs_qp + rhs_qp */
        temp_ptr = ring_addr + i * (UNIT_QPLEN * 3);
        strncpy(temp_str, temp_ptr, UNIT_QPLEN);
        proc->boot_tb[i][0] = strtol(temp_str, NULL, 16);
        DEBUG_PRINT("Got hca_lid %s num %08X\n",
                    temp_str, proc->boot_tb[i][0]);

        /* qp # to me usually on rhs unless I am at the beginning */
        temp_ptr += UNIT_QPLEN * (2 - i);
        strncpy(temp_str, temp_ptr, UNIT_QPLEN);
        proc->boot_tb[i][1] = strtol(temp_str, NULL, 16);
        DEBUG_PRINT("Got queue_pair %s num %08X\n",
                    temp_str, proc->boot_tb[i][1]);
    }

    /* Modifying  QP to INIT */
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_INIT;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.port = rdma_default_port;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PORT);
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);

    {
        ret = VAPI_modify_qp(proc->nic[0], proc->boot_qp_hndl[0], &qp_attr,
                             &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify lhs qp to INIT!\n");
        ret = VAPI_modify_qp(proc->nic[0], proc->boot_qp_hndl[1], &qp_attr,
                             &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify rhs qp to INIT!\n");
    }

    /**********************  INIT --> RTR  ************************/
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_RTR;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.qp_ous_rd_atom = rdma_default_qp_ous_rd_atom;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM);
    qp_attr.path_mtu = rdma_default_mtu;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PATH_MTU);
    qp_attr.rq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RQ_PSN);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.min_rnr_timer = rdma_default_min_rnr_timer;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_MIN_RNR_TIMER);

    qp_attr.av.sl = rdma_default_service_level;
    qp_attr.av.grh_flag = FALSE;
    qp_attr.av.static_rate = rdma_default_static_rate;
    qp_attr.av.src_path_bits = rdma_default_src_path_bits;
    /* QP_ATTR_MASK_SET(qp_attr_mask,QP_ATTR_AV); */

    /* lhs */
    for (i = 0; i < 2; i++) {
        qp_attr.dest_qp_num = proc->boot_tb[i][1];
        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM);
        qp_attr.av.dlid = proc->boot_tb[i][0];
        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_AV);

        DEBUG_PRINT("Remote LID[%d] QP=%d\n",
                    qp_attr.av.dlid, qp_attr.dest_qp_num);
        ret =
            VAPI_modify_qp(proc->nic[0], proc->boot_qp_hndl[i], &qp_attr,
                           &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify qp to RTR!\n");
        DEBUG_PRINT("Modified to RTR..Qp[%d]\n", i);
    }

    /************** RTS *******************/
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_RTS;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.sq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_SQ_PSN);
    qp_attr.timeout = rdma_default_time_out;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_TIMEOUT);
    qp_attr.retry_count = rdma_default_retry_count;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RETRY_COUNT);
    qp_attr.rnr_retry = rdma_default_rnr_retry;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RNR_RETRY);
    qp_attr.ous_dst_rd_atom = rdma_default_max_rdma_dst_ops;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM);

    {
        ret = VAPI_modify_qp(proc->nic[0], proc->boot_qp_hndl[0],
                             &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify lhs qp to RTS!\n");
        ret = VAPI_modify_qp(proc->nic[0], proc->boot_qp_hndl[1],
                             &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify rhs qp to RTS!\n");
    }
}

/* Using a ring of queue pairs to exchange all the queue_pairs,
 * If mpd is used,  only info about lhs and rsh are provided. */
static void ib_vapi_bootstrap_ring(int lhs, int rhs,
                                   char *ring_addr,
                                   int pg_rank, int pg_size,
                                   char *local_addr,
                                   char *alladdrs,
                                   VIP_MEM_HANDLE * mem_handle)
{
    int i, j, ret;
    int recv_index;

    /* Now enable the queue pairs and post descriptors */
    ib_vapi_enable_ring(lhs, rhs, ring_addr);

    /* Register alladdrs and post receive descriptors */
    {
        /* work entries related variables */
        VAPI_rr_desc_t rr;
        VAPI_sg_lst_entry_t sg_entry_r;
        VAPI_sr_desc_t sr;
        VAPI_sg_lst_entry_t sg_entry_s;

        /* completion related variables */
        VAPI_cq_hndl_t cq_hndl;
        VAPI_wc_desc_t rc, sc;

        /* Memory related variables */
        VAPI_mrw_t mr_in, mr_out;

        int unit_length;
        char *dest_loc;
        char *recv_addr;
        char *send_addr;

        struct MPIDI_CH3I_RDMA_Process_t *proc;
        proc = &MPIDI_CH3I_RDMA_Process;

        /* Same as local_addr_len; memory is already regitered */
        unit_length = pg_size * QPLEN_XDR;

        /* Copy local_addr to the correct slot in alladdrs
         * and  post receive descriptors for all_addr */
        dest_loc = alladdrs + pg_rank * unit_length;
        strncpy(dest_loc, local_addr, unit_length);

        recv_index = 0;

        /* Post receive for all_addr */
        for (j = 0; j < pg_size + 1; j++) {
            /* The last two entries are used for a barrier,
             * they overlap the local address */
            if ((j + 1) < pg_size) {
                recv_addr = alladdrs + unit_length *
                    ((pg_rank + pg_size - j - 1) % pg_size);
            } else if (j < pg_size) {
                recv_addr = alladdrs + unit_length * pg_rank;
            } else {
                recv_addr = alladdrs + unit_length * pg_rank + QPLEN_XDR;
            }

            /* Fillup a recv descriptor */
            rr.comp_type = VAPI_SIGNALED;
            rr.opcode = VAPI_RECEIVE;
            rr.id = j;
            rr.sg_lst_len = 1;
            rr.sg_lst_p = &(sg_entry_r);
            sg_entry_r.lkey = mem_handle->lkey;
            sg_entry_r.addr = (VAPI_virt_addr_t) (virt_addr_t) recv_addr;

            if ((j + 1) >= pg_size)
                sg_entry_r.len = (j + 2 - pg_size) * QPLEN_XDR;
            else
                sg_entry_r.len = unit_length;

            /* Post the recv descriptor */
            if (j < pg_size) {
                ret =
                    VAPI_post_rr(proc->nic[0], proc->boot_qp_hndl[0], &rr);
            } else {
                ret =
                    VAPI_post_rr(proc->nic[0], proc->boot_qp_hndl[1], &rr);
            }
            CHECK_RETURN(ret, "Error posting recv!");
        }

        /* synchronize all the processes */
        ret = PMI_Barrier();
        CHECK_UNEXP((ret != 0), "PMI_KVS_Barrier error \n");

        recv_index = 0;

        /* transfer all the addresses */
        for (j = 0; j < pg_size - 1; j++) {

            send_addr = alladdrs +
                unit_length * ((pg_rank + pg_size - j) % pg_size);

            /* send to rhs */
            sr.opcode = VAPI_SEND;
            sr.comp_type = VAPI_SIGNALED;
            sr.id = j;
            sr.sg_lst_len = 1;
            sr.sg_lst_p = &sg_entry_s;

            sr.remote_qkey = 0;
            sg_entry_s.addr =
                (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
            sg_entry_s.len = unit_length;
            sg_entry_s.lkey = mem_handle->lkey;

            ret = VAPI_post_sr(proc->nic[0], proc->boot_qp_hndl[1], &sr);
            CHECK_RETURN(ret, "Error posting send!");

            /* recv from lhs */
            cq_hndl = proc->boot_cq_hndl;

            ret = VAPI_CQ_EMPTY;
            rc.opcode = VAPI_CQE_INVAL_OPCODE;
            rc.id = -1;

            do {
                ret = VAPI_poll_cq(proc->nic[0], cq_hndl, &rc);
                if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA) {
                    /* Detect any earlier message */
                    if (rc.id != recv_index) {
                        fprintf(stderr,
                                "[%d](%s:%d)unexpected message %p vs %d\n",
                                pg_rank, __FILE__, __LINE__,
                                (void *)(unsigned long)rc.id, recv_index);
                        exit(1);
                    } else {
                        DEBUG_PRINT("expected message %d\n", rc.id);
                        recv_index = rc.id + 1;
                    }
                } else if (ret == VAPI_EINVAL_HCA_HNDL
                           || ret == VAPI_EINVAL_CQ_HNDL) {
                    fprintf(stderr,
                            "[%d](%s:%d)invalid CQ or DEV hndl\n",
                            pg_rank, __FILE__, __LINE__);
                    fflush(stderr);
                }
            } while (rc.opcode != VAPI_CQE_RQ_SEND_DATA);       /* do-while loop */
        }                       /* end of for loop */
    }                           /* end of alltoall exchange */
}
#endif

/* Exchange address info with other processes in the job.
 * MPD provides the ability for processes within the job to
 * publish information which can then be querried by other
 * processes.  It also provides a simple barrier sync.
 */
static int
rdma_pmi_exchange_addresses(int pg_rank, int pg_size,
                            void *localaddr, int addrlen, void *alladdrs)
{
    int ret, i, j, lhs, rhs, len_local, len_remote, key_max_sz, val_max_sz;
    char attr_buff[IBA_PMI_ATTRLEN];
    char val_buff[IBA_PMI_VALLEN];
    char *temp_localaddr = (char *) localaddr;
    char *temp_alladdrs = (char *) alladdrs;
    char *key, *val;
    char *kvsname = NULL;

    /* Allocate space for pmi keys and values */
    ret = PMI_KVS_Get_key_length_max(&key_max_sz);
    assert(ret == PMI_SUCCESS);
    key_max_sz++;
    key = MPIU_Malloc(key_max_sz);
    CHECK_UNEXP((key == NULL), "Could not get key \n");

    ret = PMI_KVS_Get_value_length_max(&val_max_sz);
    assert(ret == PMI_SUCCESS);
    val_max_sz++;
    val = MPIU_Malloc(val_max_sz);
    CHECK_UNEXP((val == NULL), "Could not get val \n");
    len_local = strlen(temp_localaddr);

    /* TODO: Double check the value of value */
    assert(len_local <= val_max_sz);

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

#ifdef USE_MPD_RING
    lhs = (pg_rank + pg_size - 1) % pg_size;
    rhs = (pg_rank + 1) % pg_size;

    for (i = 0; i < 2; i++) {
        /* get lhs and rhs processes' data */
        j = (i == 0) ? lhs : rhs;
#else
    for (j = 0; j < pg_size; j++) {
        /* get lhs and rhs processes' data */
#endif

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

        assert(len_remote >= len_local);
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
    return (1);
}

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_attr_t qp_attr;
    VAPI_qp_attr_mask_t qp_attr_mask;

#ifdef USE_INLINE
    VAPI_qp_init_attr_t qp_query_init_attr;
#endif

    unsigned int act_num_cqe;
    int i;
    int ret = VAPI_OK;

    /* XXX: The device name now defaults to be "InfiniHost0" */
    /* XXX: Now the default number of hca is 1 */

    for (i = 0; i < proc->num_hcas; i++) {
        int j;
        ret = VAPI_open_hca(rdma_iba_default_hca, &proc->nic[i]);

        ret = EVAPI_get_hca_hndl(rdma_iba_default_hca, &proc->nic[i]);
        CHECK_RETURN(ret, "cannot query HCA");

        if (rdma_default_port == -1) {
	        for (j = 1; j <= 2; j++) {
        	    ret = VAPI_query_hca_port_prop(proc->nic[i],
                                           (IB_port_t) j,
                                           (VAPI_hca_port_t *) & proc->
                                           hca_port[i]);
                if (PORT_ACTIVE == proc->hca_port[i].state) {
                    DEBUG_PRINT("Found port %d on nic %d active, lid %08x\n",
                            j, i, (uint32_t) proc->hca_port[i].lid);
                   	rdma_default_port = j;
                    break;
                }
    	    }

	        if (rdma_default_port < 0) {
        	    vapi_error_abort(VAPI_STATUS_ERR, "No port active on nic %d,"
    	                         "exiting ...\n", i);
	        }
		} else {
			ret = VAPI_query_hca_port_prop(proc->nic[i],
                                       (IB_port_t) rdma_default_port,
                                       (VAPI_hca_port_t *) & proc->
                                       hca_port[i]);
            if (PORT_ACTIVE != proc->hca_port[i].state) {
				vapi_error_abort(VAPI_STATUS_ERR, "Port %d is not active! "
                                 "exiting ...\n", rdma_default_port);
            }
		}

        ret = VAPI_alloc_pd(proc->nic[i], &proc->ptag[i]);
        CHECK_RETURN(ret, "Cannot allocate PD");

        ret = VAPI_create_cq(proc->nic[i], rdma_default_max_cq_size,
                             &proc->cq_hndl[i], &act_num_cqe);
        CHECK_RETURN(ret, "cannot allocate CQ");
#ifdef ONE_SIDED
        /* This part is for built up QPs and CQ for one-sided communication */
        ret = VAPI_create_cq(proc->nic[i], rdma_default_max_cq_size,
                             &proc->cq_hndl_1sc, &act_num_cqe);
        CHECK_RETURN(ret,
                     "cannot allocate CQ for one-sided communication\n");
#endif
    }

    /* Queue Pair creation, Basic */
    qp_init_attr.cap.max_oust_wr_rq = rdma_default_max_wqe;
    qp_init_attr.cap.max_oust_wr_sq = rdma_default_max_wqe;
    qp_init_attr.cap.max_sg_size_rq = rdma_default_max_sg_list;
    qp_init_attr.cap.max_sg_size_sq = rdma_default_max_sg_list;
    qp_init_attr.rdd_hndl = 0;
    qp_init_attr.rq_sig_type = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_sig_type = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type = VAPI_TS_RC;

    for (i = 0; i < pg_size; i++) {
        int hca_index = 0;
        int j = 0, qp_index = 0;
        if (i == pg_rank)
            continue;

        qp_init_attr.pd_hndl = proc->ptag[0];
        qp_init_attr.rq_cq_hndl = proc->cq_hndl[0];
        qp_init_attr.sq_cq_hndl = proc->cq_hndl[0];

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
        /* Currently we only support one rail */
        assert(vc->mrail.subrail_per_hca * proc->num_hcas ==
               vc->mrail.num_total_subrails);
        vc->mrail.qp_hndl =
            malloc(sizeof(VAPI_qp_hndl_t) * vc->mrail.num_total_subrails);
        vc->mrail.qp_prop =
            malloc(sizeof(VAPI_qp_prop_t) * vc->mrail.num_total_subrails);
        /* per connection per qp information is saved to 
           rdma_iba_addr_table.qp_num_rdma[][] */
        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {
            ret =
                VAPI_create_qp(proc->nic[hca_index], &qp_init_attr,
                               &vc->mrail.qp_hndl[qp_index],
                               &vc->mrail.qp_prop[qp_index]);
            CHECK_RETURN(ret, "Could not create QP");
            rdma_iba_addr_table.qp_num_rdma[i][hca_index] =
                vc->mrail.qp_prop[qp_index].qp_num;

            DEBUG_PRINT("Created qp %d, num %X\n",
                        i, rdma_iba_addr_table.qp_num_rdma[i][qp_index]);
#ifdef USE_INLINE
            QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
            ret =
                VAPI_query_qp(proc->nic[hca_index],
                              vc->mrail.qp_hndl[qp_index], &qp_attr,
                              &qp_attr_mask, &qp_query_init_attr);
            CHECK_RETURN(ret, "Fail to query QP attr");
            proc->inline_size[hca_index] = qp_attr.cap.max_inline_data_sq;
#endif
            j++;
            if (j == vc->mrail.subrail_per_hca) {
                j = 0;
                hca_index++;
                qp_init_attr.pd_hndl = proc->ptag[hca_index];
                qp_init_attr.rq_cq_hndl = proc->cq_hndl[hca_index];
                qp_init_attr.sq_cq_hndl = proc->cq_hndl[hca_index];
            }
        }
    }
#ifdef USE_MPD_RING
    ret = VAPI_create_cq(proc->nic[0], rdma_default_max_cq_size,
                         &proc->boot_cq_hndl, &act_num_cqe);
    CHECK_RETURN(ret, "cannot allocate CQ");

    /* Create complete Queue and Queue pairs */
    qp_init_attr.pd_hndl = proc->ptag[0];
    qp_init_attr.rq_cq_hndl = proc->boot_cq_hndl;
    qp_init_attr.sq_cq_hndl = proc->boot_cq_hndl;

    ret = VAPI_create_qp(proc->nic[0], &qp_init_attr,
                         &proc->boot_qp_hndl[0], &proc->boot_qp_prop[0]);
    CHECK_RETURN(ret, "Could not create lhs QP");

    ret = VAPI_create_qp(proc->nic[0], &qp_init_attr,
                         &proc->boot_qp_hndl[1], &proc->boot_qp_prop[1]);
    CHECK_RETURN(ret, "Could not create rhs QP");

    DEBUG_PRINT("Created bootstrap qp num lhs %X and rhs %X\n",
                proc->boot_qp_prop[0].qp_num,
                proc->boot_qp_prop[1].qp_num);
#endif


#ifdef ONE_SIDED
    /* This part is for built up QPs and CQ for one-sided communication */
    qp_init_attr.pd_hndl = proc->ptag[0];
    qp_init_attr.rq_cq_hndl = proc->cq_hndl_1sc;
    qp_init_attr.sq_cq_hndl = proc->cq_hndl_1sc;

    for (i = 0; i < pg_size; i++) {
        /* XXX: G. Santhana, Please put your comment here  */
        MPIDI_PG_Get_vc(cached_pg, i, &vc);
        ret = VAPI_create_qp(proc->nic[0], &qp_init_attr,
                             &vc->mrail.qp_hndl_1sc,
                             &vc->mrail.qp_prop_1sc);
        CHECK_RETURN(ret, "Could not create QP");

        /* FIXME: G. Santhana, Please document why here  */
        if (i == pg_rank) {
            dst_qp_handle = vc->mrail.qp_hndl_1sc;
            dst_qp = vc->mrail.qp_prop_1sc.qp_num;
        }
        rdma_iba_addr_table.qp_num_onesided[i][0] =
            vc->mrail.qp_prop_1sc.qp_num;

        DEBUG_PRINT("Created onesdied qp %d, num %X\n",
                    i, rdma_iba_addr_table.qp_num_onesided[i]);
#ifdef USE_INLINE
        QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
        ret = VAPI_query_qp(proc->nic[0], vc->mrail.qp_hndl_1sc,
                            &qp_attr, &qp_attr_mask, &qp_query_init_attr);
        CHECK_RETURN(ret, "Fail to query QP attr");
        proc->inline_size_1sc = qp_attr.cap.max_inline_data_sq;
#endif
    }
#endif

    return VAPI_SUCCESS;

}

/* Allocate memory and handlers */
int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    VAPI_mrw_t mr_in, mr_out;
    int ret, i = 0;
    int iter_hca;

#ifdef RDMA_FAST_PATH
    /*The memory for sending the long int variable */
    mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
    mr_in.l_key = 0;
    mr_in.r_key = 0;
    mr_in.type = VAPI_MR;

    /* FIrst allocate space for RDMA_FAST_PATH for every connection */
    for (i = 0; i < pg_size; i++) {
        int tmp_index, j;
        if (i == pg_rank)
            continue;

        /* Allocate RDMA buffers, have an extra one for some ACK message */
        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#define RDMA_ALIGNMENT 4096
#define RDMA_ALIGNMENT_OFFSET (4096)
        /* allocate RDMA buffers */
        vc->mrail.rfp.RDMA_send_buf_orig =
            malloc(sizeof(struct vbuf) * (num_rdma_buffer) +
                   2 * RDMA_ALIGNMENT);
        vc->mrail.rfp.RDMA_recv_buf_orig =
            malloc(sizeof(struct vbuf) * (num_rdma_buffer) +
                   2 * RDMA_ALIGNMENT);
        /* align vbuf->buffer to 64 byte boundary */
        vc->mrail.rfp.RDMA_send_buf =
            (struct vbuf
             *) ((unsigned long) (vc->mrail.rfp.RDMA_send_buf_orig)
                 / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                 RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);
        vc->mrail.rfp.RDMA_recv_buf =
            (struct vbuf
             *) ((unsigned long) (vc->mrail.rfp.RDMA_recv_buf_orig)
                 / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                 RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);

        vc->mrail.rfp.remote_RDMA_buf_hndl =
            malloc(sizeof(VIP_MEM_HANDLE) *
                   MPIDI_CH3I_RDMA_Process.num_hcas);

        if (!vc->mrail.rfp.RDMA_send_buf || !vc->mrail.rfp.RDMA_recv_buf) {
            fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                    "Fail to register required buffers");
            exit(1);
        }

        /* zero buffers */
        memset(vc->mrail.rfp.RDMA_send_buf, 0,
              sizeof(struct vbuf) * (num_rdma_buffer));
        memset(vc->mrail.rfp.RDMA_recv_buf, 0, 
              sizeof(struct vbuf) * (num_rdma_buffer));

        DEBUG_PRINT("sizeof vbuf %d, numrdma %d\n", sizeof(struct vbuf),
                    num_rdma_buffer);
        /* set pointers */
        vc->mrail.rfp.phead_RDMA_send = 0;
        vc->mrail.rfp.ptail_RDMA_send = num_rdma_buffer - 1;
        vc->mrail.rfp.p_RDMA_recv = 0;
        vc->mrail.rfp.p_RDMA_recv_tail = num_rdma_buffer - 1;

        vc->mrail.rfp.RDMA_send_buf_hndl = malloc(sizeof(VIP_MEM_HANDLE) *
                                                  MPIDI_CH3I_RDMA_Process.
                                                  num_hcas);
        vc->mrail.rfp.RDMA_recv_buf_hndl =
            malloc(sizeof(VIP_MEM_HANDLE) *
                   MPIDI_CH3I_RDMA_Process.num_hcas);

        for (iter_hca = 0; iter_hca < MPIDI_CH3I_RDMA_Process.num_hcas;
             iter_hca++) {
            /* initialize unsignal record */
            mr_in.pd_hndl = proc->ptag[iter_hca];

            mr_in.size = sizeof(struct vbuf) * (num_rdma_buffer);
            mr_in.start =
                (VAPI_virt_addr_t) (virt_addr_t) (vc->mrail.rfp.
                                                  RDMA_send_buf);
            DEBUG_PRINT("i %d, pg_rank %d, vc %p, recv_buf %p\n", i,
                        pg_rank, vc, vc->mrail.rfp.RDMA_recv_buf);

            ret = VAPI_register_mr(proc->nic[iter_hca], &mr_in,
                                   &vc->mrail.rfp.
                                   RDMA_send_buf_hndl[iter_hca].hndl,
                                   &mr_out);
            CHECK_RETURN(ret, "VAPI_register_mr");

            vc->mrail.rfp.RDMA_send_buf_hndl[iter_hca].lkey = mr_out.l_key;
            vc->mrail.rfp.RDMA_send_buf_hndl[iter_hca].rkey = mr_out.r_key;

            mr_in.size = sizeof(struct vbuf) * (num_rdma_buffer);
            mr_in.start =
                (VAPI_virt_addr_t) (virt_addr_t) (vc->mrail.rfp.
                                                  RDMA_recv_buf);
            ret =
                VAPI_register_mr(proc->nic[iter_hca], &mr_in,
                                 &vc->mrail.rfp.
                                 RDMA_recv_buf_hndl[iter_hca].hndl,
                                 &mr_out);
            CHECK_RETURN(ret, "VAPI_register_mr");

            vc->mrail.rfp.RDMA_recv_buf_hndl[iter_hca].lkey = mr_out.l_key;
            vc->mrail.rfp.RDMA_recv_buf_hndl[iter_hca].rkey = mr_out.r_key;

            DEBUG_PRINT
                ("Created buff for rank %d, recvbuff %p, key %p\n", i,
                 (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                 (VAPI_rkey_t) vc->mrail.rfp.RDMA_recv_buf_hndl[iter_hca].
                 rkey);
        }
    }
#endif

#ifdef ONE_SIDED
    vc->mrail.postsend_times_1sc = 0;
#endif

    {
#if defined(USE_MPD_RING)       /* Get a memory handler */
        int unit_length;

        /* XXX: Because the value length is rather long,
         * here there is a little different from MVAPICH */
        unit_length = (pg_size) * QPLEN_XDR;
        proc->boot_mem = (char *) malloc(unit_length * pg_size + 4);
        CHECK_RETURN(!proc->boot_mem, "Error getting memory\n");

        DEBUG_PRINT("Created boot mem %p, size %d \n",
                    proc->boot_mem, pg_size * unit_length);

        mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
        mr_in.pd_hndl = proc->ptag[0];
        mr_in.l_key = 0;
        mr_in.r_key = 0;
        mr_in.size = pg_size * unit_length;
        mr_in.start = (VAPI_virt_addr_t) (virt_addr_t) proc->boot_mem;
        mr_in.type = VAPI_MR;
        ret = VAPI_register_mr(proc->nic[0], &mr_in,
                               &proc->boot_mem_hndl.hndl, &mr_out);
        CHECK_RETURN(ret, "Error registering memory!");
        proc->boot_mem_hndl.lkey = mr_out.l_key;
        proc->boot_mem_hndl.rkey = mr_out.r_key;
        DEBUG_PRINT("boot mem key %X\n", proc->boot_mem_hndl.rkey);
#endif
    }


    /* We need now allocate vbufs for send/recv path */
    allocate_vbufs(MPIDI_CH3I_RDMA_Process.nic,
                   MPIDI_CH3I_RDMA_Process.ptag, vapi_vbuf_pool_size);
    return MPI_SUCCESS;
}

#ifdef USE_MPD_RING
int
rdma_iba_exchange_info(struct MPIDI_CH3I_RDMA_Process_t *proc,
                       MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    int i, temp_hostid, local_addr_len;
    char *alladdrs_self;
    char *alladdrs_group;
    char *temp_ptr;
    char hostname[HOSTNAME_LEN + 1];

    /*#ifdef USE_MPD_RING */
    int lhs, rhs;
    char ring_qp_out[64];
    char ring_qp_in[128];

    temp_ptr = (char *) ring_qp_out;
    lhs = (pg_rank + pg_size - 1) % pg_size;
    rhs = (pg_rank + 1) % pg_size;

    /* Prepare my_hca_lid, lhs qp, rsh qp for exchange */
    sprintf(temp_ptr, "%08X", proc->hca_port[0].lid);
    temp_ptr += 8;
    sprintf(temp_ptr, "%08X", proc->boot_qp_prop[0].qp_num);
    temp_ptr += 8;
    sprintf(temp_ptr, "%08X", proc->boot_qp_prop[1].qp_num);
    temp_ptr += 8;
    *temp_ptr = '\0';
    DEBUG_PRINT("Bootstrap info out %s\n", ring_qp_out);
    /*#endif */

    local_addr_len = pg_size * QPLEN_XDR;
    alladdrs_self = (char *) malloc(local_addr_len + 4);

    if (gethostname(hostname, HOSTNAME_LEN)) {
        fprintf(stderr, "Could not get hostname\n");
        exit(1);
    }

    temp_hostid = get_host_id(hostname, HOSTNAME_LEN);
    temp_ptr = (char *) alladdrs_self;

    for (i = 0; i < pg_size; i++) {

        /* Queue pairs for all processes */
        if (i == pg_rank) {
            /* Stash in my hca_lid, node_id, host_id and thread info */
            sprintf(temp_ptr, "%08X:%08X:%24s:",
                    proc->hca_port[0].lid, temp_hostid,
                    "------ABCD----------");
#ifdef ONE_SIDED
            sprintf((char *) temp_ptr + 43, "%8s:", "WITH-1SC");
#endif

            DEBUG_PRINT("Before exchange, hostid %08X, hca_lid %08X\n",
                        temp_hostid, proc->hca_port[0].lid);
        } else {

            /* Stash in my qp_num, and memory */
            int qp_num_1sc;
            long local_array = 0;
#ifdef RDMA_FAST_PATH
            unsigned long osu_recv_buff = 0;
#endif
            MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef RDMA_FAST_PATH
            osu_recv_buff = (unsigned long) vc->mrail.rfp.RDMA_recv_buf;
            sprintf(temp_ptr, "%08X:%016lX:%016lX:",
                    vc->mrail.qp_prop[0].qp_num,
                    vc->mrail.rfp.RDMA_recv_buf_hndl[0].rkey,
                    osu_recv_buff);
#else
            sprintf(temp_ptr, "%08X:%16s:%16s:",
                    vc->mrail.qp_prop[0].qp_num,
                    "NO-RDMA-FASTPATH", "NO-RDMA-FASTPATH");
#endif

#ifdef ONE_SIDED
            qp_num_1sc = vc->mrail.qp_prop_1sc.qp_num,
                sprintf((char *) temp_ptr + 43, "%08X:", qp_num_1sc);
#endif
#ifdef RDMA_FAST_PATH
            DEBUG_PRINT("Before exchange, qp %08X, rkey %016lX,"
                        " recv_buff %016lX\n",
                        vc->mrail.qp_prop[0].qp_num,
                        vc->mrail.rfp.RDMA_recv_buf_hndl[0].rkey,
                        osu_recv_buff);
#else
            DEBUG_PRINT("Before exchange, qp %08X\n",
                        vc->mrail.qp_prop[0].qp_num);

#endif

        }
        DEBUG_PRINT("Before exchange, info out %s\n", temp_ptr);
        temp_ptr += QPLEN_XDR;
    }

    /* Chop off any remanant bytes */
    *temp_ptr = '\0';

#ifdef USE_MPD_RING
    /* Save the static buffer address */
    rdma_pmi_exchange_addresses(pg_rank, pg_size, ring_qp_out,
                                strlen(ring_qp_out), ring_qp_in);
    DEBUG_PRINT("Bootstrap info in %s\n", ring_qp_in);

    /* XXX: Enable the ring to exhange all the others queue_pair numbers.
     *   Create memory handle 
     *   Exchange lhs, rhs and memory handle over PMI interface 
     *   Exchange rdma_iba_table_out and recv into rdma_iba_addr_table */
    DEBUG_PRINT("ring exchange out %s\n", alladdrs_self);
    ib_vapi_bootstrap_ring(lhs, rhs, ring_qp_in,
                           pg_rank, pg_size, alladdrs_self,
                           proc->boot_mem, &proc->boot_mem_hndl);
#else
    /* XXX: Exchange all the information over PMI interface,
     *      Need to double check the val_max_sz, so that to avoid
     *      overflowing. */
    DEBUG_PRINT("ring exchange out %s\n", alladdrs_self);
    rdma_pmi_exchange_addresses(pg_rank, pg_size, alladdrs_self,
                                local_addr_len, proc->boot_mem);
#endif
    DEBUG_PRINT("ring exchange in %s\n", proc->boot_mem);

    /* release temporary memory */
    free(alladdrs_self);

    for (i = 0; i < pg_size; i++) {
        char *alladdr_inv;
        char *temp_ptr;
        char temp_str[24];
        long temp_array = 0;
        unsigned long temp_recv_buff = 0;
        int num_tokens = 0;

        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
        alladdr_inv = proc->boot_mem + i * local_addr_len;
        temp_ptr = alladdr_inv + i * QPLEN_XDR;

        /* Get the hostid and hca_lid */
        num_tokens = sscanf(temp_ptr, "%08X:%08X:",
                            &rdma_iba_addr_table.lid[i][0],
                            &rdma_iba_addr_table.hostid[i][0]);
        assert(num_tokens == 2);
        DEBUG_PRINT("After exchange, hostid %08X, hca_lid %08X "
                    "local_len %d qp len %d\n",
                    rdma_iba_addr_table.hostid[i][0],
                    rdma_iba_addr_table.lid[i][0],
                    local_addr_len, QPLEN_XDR);
#ifdef _SMP_
      if (SMP_INIT)
        vc->smp.hostid = rdma_iba_addr_table.hostid[i][0];
#endif

        /* Get the qp, key and buffer for this process */
        temp_ptr = alladdr_inv + pg_rank * QPLEN_XDR;

#ifdef RDMA_FAST_PATH
        num_tokens = sscanf(temp_ptr, "%08X:%016lX:%016lX:",
                            &rdma_iba_addr_table.qp_num_rdma[i][0],
                            &vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey,
                            &temp_recv_buff);
        assert(num_tokens == 3);
#else
        num_tokens = sscanf(temp_ptr, "%08X:",
                            &rdma_iba_addr_table.qp_num_rdma[i][0]);
        assert(num_tokens == 1);
#endif

#ifdef ONE_SIDED
        num_tokens = sscanf((char *) temp_ptr + 43, "%08X:",
                            &rdma_iba_addr_table.qp_num_onesided[i][0]);
        assert(num_tokens == 1);
#endif

#ifdef RDMA_FAST_PATH
        vc->mrail.rfp.remote_RDMA_buf = (void *) temp_recv_buff;

        DEBUG_PRINT("After exchange, qp %08X, rkey %08X,"
                    " recv_buff %016lX\n",
                    rdma_iba_addr_table.qp_num_rdma[i][0],
                    vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey,
                    vc->mrail.rfp.remote_RDMA_buf);
#else
        DEBUG_PRINT("After exchange, qp %08X\n",
                    rdma_iba_addr_table.qp_num_rdma[i][0]);
#endif
    }
    DEBUG_PRINT("Done exchanging info\n");
    return 0;
}
#endif

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    VAPI_qp_cap_t qp_cap;
    VAPI_qp_attr_t qp_attr;
    VAPI_qp_attr_mask_t qp_attr_mask;

    int i;
    int ret = VAPI_OK;

    /**********************  QP --> INIT ************************/
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_INIT;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.port = rdma_default_port;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PORT);
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE
        | VAPI_EN_REM_READ | VAPI_EN_REM_ATOMIC_OP;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);

    for (i = 0; i < pg_size; i++) {
        int hca_index = 0, qp_index;
        int j = 0;
        MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef  ONE_SIDED
        ret = VAPI_modify_qp(proc->nic[0], vc->mrail.qp_hndl_1sc,
                             &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify qp to INIT");
#endif                          /* ONE_SIDED */

        if (i == pg_rank)
            continue;
        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {

            ret = VAPI_modify_qp(proc->nic[hca_index],
                                 vc->mrail.qp_hndl[qp_index], &qp_attr,
                                 &qp_attr_mask, &qp_cap);
            CHECK_RETURN(ret, "Could not modify qp to INIT");
            j++;
            if (j == vc->mrail.subrail_per_hca) {
                j = 0;
                hca_index++;
            }
        }
    }

    /**********************  INIT --> RTR  ************************/
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_RTR;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.qp_ous_rd_atom = rdma_default_qp_ous_rd_atom;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM);
    qp_attr.path_mtu = rdma_default_mtu;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PATH_MTU);
    qp_attr.rq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RQ_PSN);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.min_rnr_timer = rdma_default_min_rnr_timer;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_MIN_RNR_TIMER);
    qp_attr.av.sl = rdma_default_service_level;
    qp_attr.av.grh_flag = FALSE;
    qp_attr.av.static_rate = rdma_default_static_rate;
    qp_attr.av.src_path_bits = rdma_default_src_path_bits;

    for (i = 0; i < pg_size; i++) {
        int hca_index = 0, qp_index;
        int j = 0;

        if (i == pg_rank)
            continue;
        MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef ONE_SIDED
        if (i == pg_rank)
            qp_attr.dest_qp_num = dst_qp;
        else
            qp_attr.dest_qp_num =
                rdma_iba_addr_table.qp_num_onesided[i][0];

        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM);

        if (i == pg_rank)
            qp_attr.av.dlid = proc->hca_port[0].lid;
        else
            qp_attr.av.dlid = rdma_iba_addr_table.lid[i][0];

        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_AV);
        ret = VAPI_modify_qp(proc->nic[0], vc->mrail.qp_hndl_1sc,
                             &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify qp to RTR");
#endif                          /* End of ONE_SIDED */

        qp_attr.dest_qp_num =
            rdma_iba_addr_table.qp_num_rdma[i][hca_index];
        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_DEST_QP_NUM);
        qp_attr.av.dlid = rdma_iba_addr_table.lid[i][hca_index];
        QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_AV);
        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {
            ret =
                VAPI_modify_qp(proc->nic[hca_index],
                               vc->mrail.qp_hndl[qp_index], &qp_attr,
                               &qp_attr_mask, &qp_cap);
            CHECK_RETURN(ret, "Could not modify qp to RTR");
            j++;
            if (j == vc->mrail.subrail_per_hca) {
                j = 0;
                hca_index++;
                qp_attr.dest_qp_num =
                    rdma_iba_addr_table.qp_num_rdma[i][hca_index];
                qp_attr.av.dlid = rdma_iba_addr_table.lid[i][hca_index];
            }
        }
    }

    /************** RTR --> RTS *******************/
    QP_ATTR_MASK_CLR_ALL(qp_attr_mask);
    qp_attr.qp_state = VAPI_RTS;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.sq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_SQ_PSN);
    qp_attr.timeout = rdma_default_time_out;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_TIMEOUT);
    qp_attr.retry_count = rdma_default_retry_count;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RETRY_COUNT);
    qp_attr.rnr_retry = rdma_default_rnr_retry;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_RNR_RETRY);
    qp_attr.ous_dst_rd_atom = rdma_default_max_rdma_dst_ops;
    QP_ATTR_MASK_SET(qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM);

    for (i = 0; i < pg_size; i++) {
        int qp_index;
        int hca_index = 0, j = 0;

        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#ifdef ONE_SIDED
        ret = VAPI_modify_qp(proc->nic[0], vc->mrail.qp_hndl_1sc,
                             &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN(ret, "Could not modify qp to RTS");
#endif                          /* ONE_SIDED */

        if (i == pg_rank)
            continue;
        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {
            ret =
                VAPI_modify_qp(proc->nic[hca_index],
                               vc->mrail.qp_hndl[qp_index], &qp_attr,
                               &qp_attr_mask, &qp_cap);
            CHECK_RETURN(ret, "Could not modify qp to RTR");
            j++;
            if (j == vc->mrail.subrail_per_hca) {
                j = 0;
                hca_index++;
            }
        }
    }

    DEBUG_PRINT("Done enabling connections\n");
    return 0;
}

void MRAILI_Init_vc(MPIDI_VC_t * vc, int pg_rank)
{
    int channels = vc->mrail.num_total_subrails;
    int i;
    MRAILI_Channel_info subchannel;

    vc->mrail.send_wqes_avail = malloc(sizeof(int) * channels);
    vc->mrail.ext_sendq_head = malloc(sizeof(aint_t) * channels);
    vc->mrail.ext_sendq_tail = malloc(sizeof(aint_t) * channels);

    /* Now we will need to */
    for (i = 0; i < channels; i++) {
        vc->mrail.send_wqes_avail[i] = rdma_default_max_wqe - 20;
        DEBUG_PRINT("set send_wqe_avail as %d\n",
                    rdma_default_max_wqe - 20);
        vc->mrail.ext_sendq_head[i] = NULL;
        vc->mrail.ext_sendq_tail[i] = NULL;
    }
    vc->mrail.next_packet_expected = 0;
    vc->mrail.next_packet_tosend = 0;
	vc->mrail.packetized_recv = NULL;

#ifdef RDMA_FAST_PATH
    /* prefill desc of credit_vbuf */
    for (i = 0; i < num_rdma_buffer; i++) {
        /* prefill vbuf desc */
        vbuf_init_rdma_write(&vc->mrail.rfp.RDMA_send_buf[i]);
        /* associate vc to vbuf */
        vc->mrail.rfp.RDMA_send_buf[i].vc = (void *) vc;
        vc->mrail.rfp.RDMA_send_buf[i].padding = FREE_FLAG;
        vc->mrail.rfp.RDMA_recv_buf[i].vc = (void *) vc;
        vc->mrail.rfp.RDMA_recv_buf[i].padding = BUSY_FLAG;
    }

    vc->mrail.rfp.rdma_credit = 0;
#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss = 0;
    vc->mrail.rfp.cached_hit = 0;
    vc->mrail.rfp.cached_incoming = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif
    vc->mrail.cmanager.total_subrails = 2;
    vc->mrail.cmanager.num_local_pollings = 1;

    vc->mrail.cmanager.poll_channel = malloc(sizeof(vbuf * (**)(void *)));
#else
    vc->mrail.cmanager.total_subrails = 1;
    vc->mrail.cmanager.num_local_pollings = 0;
#endif
    vc->mrail.cmanager.v_queue_head = malloc(sizeof(vbuf *) *
                                             vc->mrail.cmanager.
                                             total_subrails);
    vc->mrail.cmanager.v_queue_tail =
        malloc(sizeof(vbuf *) * vc->mrail.cmanager.total_subrails);
    vc->mrail.cmanager.len =
        malloc(sizeof(int) * vc->mrail.cmanager.total_subrails);

    for (i = 0; i < vc->mrail.cmanager.total_subrails; i++) {
        vc->mrail.cmanager.v_queue_head[i] =
            vc->mrail.cmanager.v_queue_tail[i] = NULL;
        vc->mrail.cmanager.len[i] = 0;
    }

    DEBUG_PRINT("Cmanager total rail %d\n",
                vc->mrail.cmanager.total_subrails);

    /* Set the initial value for credits */
    /* And for each queue pair, we need to prepost a certain number of recv descriptors */
    vc->mrail.srp.backlog.len = 0;
    vc->mrail.srp.backlog.vbuf_head = NULL;
    vc->mrail.srp.backlog.vbuf_tail = NULL;

    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow = NULL;
    vc->mrail.inflow = 0;

    for (i = 0; i < vc->mrail.num_total_subrails; i++) {
        int k;

        MRAILI_CHANNEL_INFO_INIT(subchannel, i, vc);

        for (k = 0; k < vapi_initial_prepost_depth; k++) {
            PREPOST_VBUF_RECV(vc, subchannel);
        }

        vc->mrail.srp.remote_credit[i] = vapi_initial_credits;
        vc->mrail.srp.remote_cc[i] = vapi_initial_credits;
        vc->mrail.srp.local_credit[i] = 0;
        vc->mrail.srp.preposts[i] = vapi_initial_prepost_depth;
        vc->mrail.srp.initialized[i] =
            (vapi_prepost_depth == vapi_initial_prepost_depth);

        vc->mrail.srp.rendezvous_packets_expected[i] = 0;
    }
    DEBUG_PRINT
        ("[Init:priv] remote_credit %d, remote_cc %d, local_credit %d, prepost%d\n ",
         vc->mrail.srp.remote_credit[0], vc->mrail.srp.remote_cc[0],
         vc->mrail.srp.local_credit[0], vc->mrail.srp.preposts[0]);
}
