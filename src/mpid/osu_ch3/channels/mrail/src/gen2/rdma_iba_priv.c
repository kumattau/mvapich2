/* Copyright (c) 2003-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MPICH2 directory.
 *
 */
#ifndef MAC_OSX
#include <malloc.h>
#else
#include <netinet/in.h>
#endif

#include <netdb.h>
#include <string.h>
#include <sysfs/libsysfs.h>

#include "vbuf.h"
#include "rdma_impl.h"
#include "ibv_priv.h"
#include "pmi.h"
#include "ibv_param.h"

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

#ifdef ONE_SIDED
static uint32_t dst_qp;
#endif

static uint16_t get_local_lid(struct ibv_context * ctx, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(ctx, port, &attr)) {
        return -1;
    }

    return attr.lid;
}



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
    ret = ibv_dereg_mr(proc->boot_mem_hndl);
    free(proc->boot_mem);

    ret = ibv_destroy_qp(proc->boot_qp_hndl[0]);
    CHECK_RETURN(ret, "could not destroy lhs QP");
    
    ret = ibv_destroy_qp(proc->boot_qp_hndl[1]);
    CHECK_RETURN(ret, "could not destroy rhs QP");

    ret = ibv_destroy_cq(proc->boot_cq_hndl);
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
    struct ibv_send_wr sr;
    struct ibv_sge sg_entry_s;
    struct ibv_send_wr *bad_wr_s;

    /* completion related variables */
    struct ibv_cq * cq_hndl;
    struct ibv_wc rc;

    /* Memory related variables */
    struct ibv_mr *mem_handle;

    char *send_addr;
    char *send_base_addr;

    int barrier_recvd = 0;
    int ne, i, offset;
    int send_comp = 0;

    if (pg_size == 1) 
	    return 0;

    send_base_addr = (char *) proc->boot_mem + 2;
    mem_handle = proc->boot_mem_hndl;

    /* make sure there are no conflicts in ids */
    offset = pg_rank + 2 + 
             (NUM_BOOTSTRAP_BARRIERS * 2) + (pg_rank % 2);

    sr.opcode       = IBV_WR_SEND;
    sr.send_flags   = IBV_SEND_SIGNALED;
    sr.num_sge      = 1;
    sr.sg_list      = &sg_entry_s;
    sr.next         = NULL;
    sg_entry_s.lkey = mem_handle->lkey;
    cq_hndl         = proc->boot_cq_hndl;

    if (pg_rank == 0) {
        /* send_lhs(); send_rhs(); recv_lhs(); recv_rhs(); */

        /* post sends */

        for (i = 0; i < 2; i++) {
            DEBUG_PRINT("Post send to %d\n", i);
            send_addr = send_base_addr + i;
            sr.wr_id = offset + i;
            sg_entry_s.length = 1;
            sg_entry_s.addr = (uintptr_t) send_addr;

            if (ibv_post_send(proc->boot_qp_hndl[i], &sr, &bad_wr_s)) {
                DEBUG_PRINT("Posting send had an error\n");
                ibv_error_abort(IBV_STATUS_ERR,
                        "Error posting send!\n");
            }
        }

        while (barrier_recvd < 2 || send_comp < 2) {

            ne = ibv_poll_cq(cq_hndl, 1, &rc);
            if (ne < 0) {
                 ibv_error_abort(IBV_STATUS_ERR, "Poll CQ failed!\n");
            } else if (ne > 1) {
                 ibv_error_abort(IBV_STATUS_ERR, "Got more than one\n");
            } else if (ne == 1) {
                if (rc.status != IBV_WC_SUCCESS) {
                        DEBUG_PRINT("status was not success in poll\n");
                       ibv_error_abort(IBV_STATUS_ERR,
                               " Error code in polled desc!\n")
                }
                /* Make sure it is a recv completion */
                else if (rc.opcode == IBV_WC_RECV) {
                    DEBUG_PRINT("(S) Received msg, id: %d\n", (int) rc.wr_id);
                    barrier_recvd++;
                }
                else if (rc.opcode == IBV_WC_SEND) {
                    DEBUG_PRINT("(S) Send completed for id: %d\n", (int) rc.wr_id);
                    send_comp++;
                } else {
                    DEBUG_PRINT("(S) Got something unxepected\n");
                }

            }
        }  /* end of while loop */
    } else {
        /* recv_lhs(); send_rhs(); recv_rhs(); send_lhs(); */

        while (barrier_recvd < 2 || send_comp < 2) {
            ne = ibv_poll_cq(cq_hndl, 1, &rc);
            if (ne < 0) {
                ibv_error_abort(IBV_STATUS_ERR, "Poll CQ failed!\n");
            } else if (ne > 1) {
                ibv_error_abort(IBV_STATUS_ERR, "Got more than one\n");
            } else if (ne == 1) {

                if (rc.status != IBV_WC_SUCCESS) {
                    ibv_error_abort(IBV_STATUS_ERR,
                            "Error code in polled desc!\n");
                }
                /* Make sure it is a recv completion */
                else if (rc.opcode == IBV_WC_RECV) {
                    DEBUG_PRINT("(R) Received msg, id: %d\n", (int) rc.wr_id);
                    barrier_recvd++;

                    send_addr = send_base_addr + (rc.wr_id % 2);
                    sr.wr_id          = offset + (rc.wr_id % 2);
                    sr.next           = NULL;
                    sg_entry_s.length = 1;
                    sg_entry_s.addr = (uintptr_t) send_addr;

                    if (ibv_post_send(proc->boot_qp_hndl[((int) rc.wr_id + 1) % 2],
                                &sr, &bad_wr_s)) {
                       ibv_error_abort(IBV_STATUS_ERR, 
                                "Error posting send!\n");
                    }

                    DEBUG_PRINT("(R) Sent to qp id: %d\n", (int) rc.wr_id);
                } /* end else if */
                else if (rc.opcode == IBV_WC_SEND) {
                    DEBUG_PRINT("(R) Send completed for id: %d\n", (int) rc.wr_id);
                    send_comp++;
                } else {
                    DEBUG_PRINT("(R) Got something unexpected \n");
                }
            }     /* end else if (ne == 1)                  */
        }         /* end while                              */
    }             /* end else                               */

    DEBUG_PRINT("Done with bootstrap barrier\n");
    return (0);
}

/* Set up a ring of qp's, here a separate bootstrap channel is used.*/
static void ibv_enable_ring(int lhs, int rhs, char *ring_addr)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_attr      qp_attr;

    uint32_t    qp_attr_mask = 0;
    int         i;
    int         ret;
    char        temp_str[UNIT_QPLEN + 1];
    char        *temp_ptr;

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


    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE 
	                            | IBV_ACCESS_REMOTE_WRITE 
	                            | IBV_ACCESS_REMOTE_READ;
    qp_attr.port_num        = rdma_default_port;
 
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
    qp_attr.ah_attr.port_num    =   rdma_default_port;

    qp_attr_mask        |=  IBV_QP_STATE;
    qp_attr_mask        |=  IBV_QP_PATH_MTU;
    qp_attr_mask        |=  IBV_QP_RQ_PSN;
    qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
    qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
    qp_attr_mask        |=  IBV_QP_AV;

    /* lhs */
    for (i = 0; i < 2; i++) {
        qp_attr.dest_qp_num     = proc->boot_tb[i][1];
        qp_attr.ah_attr.dlid    = proc->boot_tb[i][0];
        qp_attr_mask            |=  IBV_QP_DEST_QPN;

        DEBUG_PRINT("Remote LID[%d] QP=%x, original LID %d qp=%x\n",
                    qp_attr.ah_attr.dlid, qp_attr.dest_qp_num,
        		    proc->boot_tb[i][0], proc->boot_tb[i][1]);

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
}

/* Using a ring of queue pairs to exchange all the queue_pairs,
 * If mpd is used,  only info about lhs and rsh are provided. */
static void ibv_bootstrap_ring(int lhs, int rhs,
                                   char *ring_addr,
                                   int pg_rank, int pg_size,
                                   char *local_addr,
                                   char *alladdrs,
                                   struct ibv_mr *mem_handle)
{
    int i, j, ret;
    int recv_index;
    int send_comp, recv_comp;

    /* Now enable the queue pairs and post descriptors */
    ibv_enable_ring(lhs, rhs, ring_addr);

    /* Register alladdrs and post receive descriptors */
    {
        /* work entries related variables */
        struct ibv_send_wr sr;
        struct ibv_send_wr *bad_sr;
        struct ibv_recv_wr rr;
        struct ibv_recv_wr *bad_rr;
        struct ibv_sge sg_entry_s; 
        struct ibv_sge sg_entry_r; 
        void * base_addr;        

        /* completion related variables */
        struct ibv_cq * cq_hndl;
        struct ibv_wc rc, sc;

        /* Memory related variables */
        int unit_length;
        char *dest_loc;
        char *recv_addr;
        char *send_addr;
        unsigned long register_nbytes;
        char *recv_base_addr;
        char *recv_addr_tmp;

        struct MPIDI_CH3I_RDMA_Process_t *proc;
        proc = &MPIDI_CH3I_RDMA_Process;

        /* Same as local_addr_len; memory is already regitered */
        unit_length = pg_size * QPLEN_XDR;

        /* Copy local_addr to the correct slot in alladdrs
         * and  post receive descriptors for all_addr */
        dest_loc = alladdrs + pg_rank * unit_length;
        strncpy(dest_loc, local_addr, unit_length);

        recv_index = 0;

        /* for the bootstrap barrier */
        recv_base_addr = (char *) proc->boot_mem;

        DEBUG_PRINT("about to call post receive\n");

        /* Post receive for all_addr */
        for (j = 0; j < pg_size - 1; j++) {
            if ((j + 1) < pg_size) {
                recv_addr = alladdrs + unit_length *
                    ((pg_rank + pg_size - j - 1) % pg_size);
            } else if (j < pg_size) {
                recv_addr = alladdrs + unit_length * pg_rank;
            } else {
                recv_addr = alladdrs + unit_length * pg_rank + QPLEN_XDR;
            }

            /* Fillup a recv descriptor */
            rr.wr_id    = j;
            rr.num_sge  = 1;
            rr.sg_list  = &(sg_entry_r);
            rr.next     = NULL;
            sg_entry_r.lkey = mem_handle->lkey;
            sg_entry_r.addr = (uintptr_t)recv_addr;

            if ((j + 1) >= pg_size)
                sg_entry_r.length = (j + 2 - pg_size) * QPLEN_XDR;
            else
                sg_entry_r.length = unit_length;

            if (j < pg_size) {
                ret = ibv_post_recv(proc->boot_qp_hndl[0], &rr, &bad_rr);
                CHECK_RETURN(ret, "post recv error");
            } else {
                ret = ibv_post_recv(proc->boot_qp_hndl[1], &rr, &bad_rr);
                CHECK_RETURN(ret, "post recv error");
            }
          
            if(ret){
                ibv_error_abort(-1, "");
            } 
        }

        /* Post the recvs for the bootstrap barriers */

        for (i = 0; i < 2 * NUM_BOOTSTRAP_BARRIERS; i++) {
            DEBUG_PRINT("Post recv from %d\n", i);
            recv_addr_tmp   = recv_base_addr + (i % 2);
            rr.wr_id        = pg_size + i + (pg_size % 2);
            rr.num_sge      = 1;    
            rr.sg_list      = &(sg_entry_r);
            rr.next         = NULL; 
            sg_entry_r.lkey = mem_handle->lkey;
            sg_entry_r.addr = (uintptr_t) recv_addr_tmp;
            sg_entry_r.length = 1;

            if(ibv_post_recv(proc->boot_qp_hndl[i % 2], &rr, &bad_rr)) {
                ibv_error_abort(IBV_STATUS_ERR,
                        "Error posting barrier recv!\n");
            }       
        }

        /* synchronize all the processes */
        ret = PMI_Barrier();

        recv_index = 0;
        recv_comp  = 0;
        /* transfer all the addresses */
        for (j = 0; j < pg_size - 1; j++) {
            send_addr = alladdrs +
                unit_length * ((pg_rank + pg_size - j) % pg_size);

             /* send to rhs */
            sr.opcode      = IBV_WR_SEND;
            sr.send_flags  = IBV_SEND_SIGNALED;
            sr.wr_id       = j+2*pg_size;
            sr.num_sge     = 1;
            sr.sg_list     = &sg_entry_s;
            sr.next        = NULL;
            sg_entry_s.addr    = (uintptr_t) send_addr;
            sg_entry_s.length  = unit_length;
            sg_entry_s.lkey    = mem_handle->lkey;

            ret = ibv_post_send(proc->boot_qp_hndl[1], &sr, &bad_sr);
            if(ret){
                ibv_error_abort(-1, "");
            } 
            
             /* recv from lhs */
            cq_hndl = proc->boot_cq_hndl;
            rc.wr_id    = -1;
            send_comp   =-1;

            DEBUG_PRINT("ABOUT TO CALL CQ POLL\n");
            do {
                ret = ibv_poll_cq(cq_hndl,1,&rc);
                if (ret > 0){
                    if(rc.status != IBV_WC_SUCCESS){
                        ibv_error_abort(IBV_STATUS_ERR, "in bootstrap barr\n");
                    }

                    DEBUG_PRINT("SUCCESFUL POLL rc.opcode %d, rc.wr_id is %d\n",
                           rc.opcode, rc.wr_id);

                    if (rc.wr_id == (j + 2 * pg_size))
                        send_comp = rc.wr_id;
                    else
                        recv_comp = rc.wr_id + 1;

                    if(rc.opcode == IBV_WR_SEND && rc.status == IBV_WC_SUCCESS){ 
                        if (rc.wr_id != recv_index) {
                            fprintf(stderr, "unexpected message"__FILE__, __LINE__);  
                            exit(1);
                        }
                        else{
                            recv_index = rc.wr_id + 1;
                        }
                    }
                }
                else if (ret < 0)
                {
                    DEBUG_PRINT("ret is < 0\n");
                }
            } while(!(send_comp == (j+2*pg_size) && recv_comp > j));
            DEBUG_PRINT("RETURNED FROM CQ POLL \n");
        }
    }
    PMI_Barrier();
}
#endif


#ifdef USE_MPD_RING
/* Exchange address info with other processes in the job.
 * MPD provides the ability for processes within the job to
 * publish information which can then be querried by other
 * processes.  It also provides a simple barrier sync.
 */
static int
rdma_pmi_exchange_addresses(int pg_rank, int pg_size,
                            void *localaddr, int addrlen, void *alladdrs)
{
    int     ret, i, j, lhs, rhs, len_local, len_remote, key_max_sz, val_max_sz;
    char    attr_buff[IBA_PMI_ATTRLEN];
    char    val_buff[IBA_PMI_VALLEN];
    char    *temp_localaddr = (char *) localaddr;
    char    *temp_alladdrs = (char *) alladdrs;
    char    *key, *val;

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
    ret = PMI_KVS_Put(cached_pg->ch.kvs_name, key, val);
    CHECK_UNEXP((ret != 0), "PMI_KVS_Put error \n");

    ret = PMI_KVS_Commit(cached_pg->ch.kvs_name);
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

        ret = PMI_KVS_Get(cached_pg->ch.kvs_name, key, val, val_max_sz);
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
    return 0;
}
#endif

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    struct ibv_device       *ib_dev;
    struct ibv_qp_init_attr attr;
    struct ibv_qp_init_attr boot_attr;
    struct ibv_qp_attr      qp_attr;
    struct ibv_port_attr    port_attr;
#ifdef GEN2_OLD_DEVICE_LIST_VERB
    struct dlist *dev_list;
#else
    struct ibv_device **dev_list;
#endif

    int i;
    int hca_index = 0;
    int qp_index = 0;

    proc->num_hcas = 1;
#ifdef GEN2_OLD_DEVICE_LIST_VERB
    dev_list = ibv_get_devices();
#else
    dev_list = ibv_get_device_list(NULL);
#endif
    for (i = 0; i < proc->num_hcas; i++) {
#ifdef GEN2_OLD_DEVICE_LIST_VERB
        dlist_start(dev_list);
        if (!rdma_iba_default_hca) {
            ib_dev = dlist_next(dev_list);
            if (!ib_dev) {
                fprintf(stderr, "No IB device found\n");
                return -1;
            }
        } else {
            dlist_for_each_data(dev_list, ib_dev, struct ibv_device)
            if (!strcmp(ibv_get_device_name(ib_dev), rdma_iba_default_hca))
                break;
            if (!ib_dev) {
                fprintf(stderr, "IB device %s not found\n", rdma_iba_default_hca);
                return -1;
            }
        }
#else 
	if(!strncmp(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32)) {
        /* User hasn't specified any HCA name
         * We will use the first available HCA */
	    if(dev_list[0]) {
		ib_dev = dev_list[0];
	    }
	} else {
	    int j = 0;

	    /* User specified a HCA, try to look for it */
	    while(dev_list[j]) {

		if(!strncmp(ibv_get_device_name(dev_list[j]),
                        rdma_iba_hca, 32)) {
		    ib_dev = dev_list[j];
		    break;
		}
		j++;
	    }
	}

        if (!ib_dev) {
            fprintf(stderr, "No IB device found\n");
	}
#endif
        proc->nic_context[i] = ibv_open_device(ib_dev);
        if (!proc->nic_context[i]) {
            fprintf(stderr, "Fail to open HCA\n");
            return -1;
        }
   
        proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
        if (!proc->ptag[i]) {
            fprintf(stderr, "Fail to alloc pd\n");
            goto err;
        }
        proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

        if (!proc->cq_hndl[i]) {
            fprintf(stderr, "cannot create cq\n");
            goto err_pd;
        }

#ifdef ONE_SIDED
        proc->cq_hndl_1sc = ibv_create_cq(proc->nic_context[i],
                            rdma_default_max_cq_size, NULL, NULL, 0);
        if (!proc->cq_hndl_1sc) {
            fprintf(stderr, 
                     "cannot allocate CQ for one-sided communication\n");
            goto err_cq;
        }
#endif
    }

    if (rdma_default_port < 0){
        for (i = 1; i <= RDMA_DEFAULT_MAX_PORTS; i++) {
            if ((! ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[0],
                                  i, &port_attr)) &&
                    port_attr.state == IBV_PORT_ACTIVE &&
                    port_attr.lid ) {
                rdma_default_port = i;
            }
        }
        if (rdma_default_port < 0) {
            ibv_error_abort(IBV_STATUS_ERR, "No port is in active state,"
                          "please check the IB setup\n");
        }
    } else {
        if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[0],
                          rdma_default_port, &port_attr) 
            || (!port_attr.lid ) 
            || (port_attr.state != IBV_PORT_ACTIVE)
            ) {
             ibv_error_abort(IBV_STATUS_ERR, "user specified port %d: fail to"
                                             "query or not ACTIVE\n", 
                                             rdma_default_port);
        }
    }


    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);

        /* Currently we only support one rail */
        for (   qp_index = 0; 
                qp_index < vc->mrail.num_total_subrails; 
                qp_index++) 
        {
            memset(&attr, 0, sizeof attr);
            attr.cap.max_send_wr = rdma_default_max_wqe;
            attr.cap.max_recv_wr = rdma_default_max_wqe;
            attr.cap.max_send_sge = rdma_default_max_sg_list;
            attr.cap.max_recv_sge = rdma_default_max_sg_list;
            attr.cap.max_inline_data = RDMA_MAX_INLINE_SIZE;
            attr.send_cq = proc->cq_hndl[0];
            attr.recv_cq = proc->cq_hndl[0];
            attr.qp_type = IBV_QPT_RC;
            attr.sq_sig_all = 0;

            vc->mrail.qp_hndl[qp_index] = 
                ibv_create_qp(proc->ptag[0], &attr);
            if (!vc->mrail.qp_hndl[qp_index]) {
                fprintf(stderr, "[Init] Fail to create qp for rank %d\n", i);
                goto err_cq; 
            }

            rdma_iba_addr_table.qp_num_rdma[i][hca_index] =
                vc->mrail.qp_hndl[qp_index]->qp_num;

            qp_attr.qp_state        = IBV_QPS_INIT;
            qp_attr.pkey_index      = 0;
            qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

            qp_attr.port_num = rdma_default_port;

            if (ibv_modify_qp(vc->mrail.qp_hndl[qp_index], &qp_attr,
                  IBV_QP_STATE              |
                  IBV_QP_PKEY_INDEX         |
                  IBV_QP_PORT               |
                  IBV_QP_ACCESS_FLAGS)) {
                fprintf(stderr, "Failed to modify QP to INIT\n");
                goto err_cq;
            }
        }
    }
#ifdef USE_MPD_RING
    DEBUG_PRINT("ENTERING MPDRING CASE\n");  
    proc->boot_cq_hndl =
            ibv_create_cq(proc->nic_context[0],rdma_default_max_cq_size, 
                        NULL, NULL, 0); 
    if (!proc->boot_cq_hndl) {
            fprintf(stderr, "cannot create cq\n");
            goto err_pd;
        }

    /* Create complete Queue and Queue pairs */
    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = RDMA_MAX_INLINE_SIZE;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;

    proc->boot_qp_hndl[0] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[0]) {
        fprintf(stderr, "[Init] Fail to create qp for rank %d\n", i);
        goto err_cq;
    }
 
    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = RDMA_MAX_INLINE_SIZE;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;
    
    proc->boot_qp_hndl[1] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[1]) {
        fprintf(stderr, "[Init] Fail to create qp for rank %d\n", i);
        goto err_cq;
    }
    DEBUG_PRINT("Created boot qp %x, %x\n",
	    proc->boot_qp_hndl[0]->qp_num, proc->boot_qp_hndl[1]->qp_num);
#endif

#ifdef ONE_SIDED
    /* This part is for built up QPs and CQ for one-sided communication */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags =   IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_REMOTE_READ;
    qp_attr.port_num        = rdma_default_port;
    
    for (i = 0; i < pg_size; i++) {
        memset(&attr, 0, sizeof attr);
        attr.cap.max_send_wr = rdma_default_max_wqe;
        attr.cap.max_recv_wr = rdma_default_max_wqe;
        attr.cap.max_send_sge = rdma_default_max_sg_list;
        attr.cap.max_recv_sge = rdma_default_max_sg_list;
        attr.cap.max_inline_data = sizeof(long long);
        attr.send_cq = proc->cq_hndl_1sc;
        attr.recv_cq = proc->cq_hndl_1sc;
        attr.qp_type = IBV_QPT_RC;
        attr.sq_sig_all = 0;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
        vc->mrail.qp_hndl_1sc = ibv_create_qp(proc->ptag[0], &attr);
        if (!vc->mrail.qp_hndl_1sc) {
            fprintf(stderr, "[Init] Fail to create one sided qp for rank %d\n", i);
            goto err_cq;
        }

        if (i == pg_rank) {
            dst_qp = vc->mrail.qp_hndl_1sc->qp_num;
        }
        rdma_iba_addr_table.qp_num_onesided[i][0] =
            vc->mrail.qp_hndl_1sc->qp_num;
        DEBUG_PRINT("Created onesdied qp %d, num %X\n",
                    i, rdma_iba_addr_table.qp_num_onesided[i][0]);

        if (ibv_modify_qp(vc->mrail.qp_hndl_1sc, &qp_attr,
              IBV_QP_STATE              |
              IBV_QP_PKEY_INDEX         |
              IBV_QP_PORT               |
              IBV_QP_ACCESS_FLAGS)) {
            fprintf(stderr, "Failed to modify One sided QP to INIT\n");
            goto err_cq;
        }
    }
#endif

    DEBUG_PRINT("Return from init hca\n");
    return 0;
err_cq:
    if (proc->cq_hndl[0])
        ibv_destroy_cq(proc->cq_hndl[0]);
  #ifdef ONE_SIDED
    if (proc->cq_hndl_1sc)
        ibv_destroy_cq(proc->cq_hndl_1sc);
  #endif
err_pd:
    if (proc->ptag[0])
        ibv_dealloc_pd(proc->ptag[0]);
err:
    ibv_close_device(proc->nic_context[0]);
    return -1;

}

/* Allocate memory and handlers */
int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    int ret, i = 0;
    int iter_hca;

#ifdef RDMA_FAST_PATH
    /* FIrst allocate space for RDMA_FAST_PATH for every connection */
    for (i = 0; i < pg_size; i++) {
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
        if (!vc->mrail.rfp.RDMA_send_buf_orig) {
            fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                    "Fail to register required buffers");
            return -1;
        }

        vc->mrail.rfp.RDMA_recv_buf_orig =
            malloc(sizeof(struct vbuf) * (num_rdma_buffer) +
                   2 * RDMA_ALIGNMENT);
        if (!vc->mrail.rfp.RDMA_recv_buf_orig) {
            fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                    "Fail to register required buffers");
            ret = -1;
            goto err_sbuf;
        }

        /* align vbuf->buffer to 64 byte boundary */
        vc->mrail.rfp.RDMA_send_buf =
            (struct vbuf *) 
                 ((unsigned long) (vc->mrail.rfp.RDMA_send_buf_orig)
                 / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                 RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);
        vc->mrail.rfp.RDMA_recv_buf =
            (struct vbuf
             *) ((unsigned long) (vc->mrail.rfp.RDMA_recv_buf_orig)
                 / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                 RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);

        memset(vc->mrail.rfp.RDMA_send_buf, 0,
              sizeof(struct vbuf) * (num_rdma_buffer));
        memset(vc->mrail.rfp.RDMA_recv_buf, 0,
              sizeof(struct vbuf) * (num_rdma_buffer));

        /* set pointers */
        vc->mrail.rfp.phead_RDMA_send = 0;
        vc->mrail.rfp.ptail_RDMA_send = num_rdma_buffer - 1;
        vc->mrail.rfp.p_RDMA_recv = 0;
        vc->mrail.rfp.p_RDMA_recv_tail = num_rdma_buffer - 1;

        for (iter_hca = 0; iter_hca < MPIDI_CH3I_RDMA_Process.num_hcas;
             iter_hca++) {
            /* initialize unsignal record */
            vc->mrail.rfp.RDMA_send_buf_mr[iter_hca] = 
                    ibv_reg_mr( MPIDI_CH3I_RDMA_Process.ptag[iter_hca],
                                vc->mrail.rfp.RDMA_send_buf,
                                sizeof(struct vbuf) * (num_rdma_buffer),
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE );
            if (!vc->mrail.rfp.RDMA_send_buf_mr[iter_hca]) {
                ret = -1;
                fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                                "Unable to register RDMA buffer\n");
                goto err_buf;
            }

            vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca] = 
                    ibv_reg_mr( MPIDI_CH3I_RDMA_Process.ptag[iter_hca],
                                vc->mrail.rfp.RDMA_recv_buf,
                                sizeof(struct vbuf) * (num_rdma_buffer),
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE  );

            if (!vc->mrail.rfp.RDMA_send_buf_mr[iter_hca] ||
                !vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca] ) {
                fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__, 
                                "Unable to register RDMA buffer\n");
                goto err_sreg;
            }
            DEBUG_PRINT
                ("Created buff for rank %d, recvbuff %p, key %p\n", i,
                 (uintptr_t) vc->mrail.rfp.RDMA_recv_buf,
                 vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca]->rkey);
        }
    }
#endif

#ifdef ONE_SIDED
    vc->mrail.postsend_times_1sc = 0;
#endif

    {
#if defined(USE_MPD_RING) /* Get a memory handler */
    int unit_length ;

    /* XXX: Because the value length is rather long,
     * here there is a little different from MVAPICH */
    unit_length = (pg_size) * QPLEN_XDR;
    proc->boot_mem = (char*)malloc( unit_length * pg_size + 4 );
    CHECK_RETURN (!proc->boot_mem, "Error getting memory\n");

    DEBUG_PRINT("Created boot mem %p, size %d \n",
        proc->boot_mem, pg_size * unit_length);

    proc->boot_mem_hndl = ibv_reg_mr(proc->ptag[0],
                                proc->boot_mem,
                                 (pg_size * unit_length),
                                (IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE ));
#endif
    }

    /* We need now allocate vbufs for send/recv path */
    ret = allocate_vbufs(MPIDI_CH3I_RDMA_Process.ptag, rdma_vbuf_pool_size);
    if (ret) {
        goto err_reg;
    }
    
    return MPI_SUCCESS;
#ifdef RDMA_FAST_PATH
  err_reg:
    ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca]);
  err_sreg:
    ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[iter_hca]);
  err_buf:
    free(vc->mrail.rfp.RDMA_recv_buf_orig);
  err_sbuf:
    free(vc->mrail.rfp.RDMA_send_buf_orig);
#else
   err_reg:
#endif
    return ret;
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
    sprintf(temp_ptr, "%08X", get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], rdma_default_port));
    temp_ptr += 8;
    sprintf(temp_ptr, "%08X", proc->boot_qp_hndl[0]->qp_num);
    temp_ptr += 8;
    sprintf(temp_ptr, "%08X", proc->boot_qp_hndl[1]->qp_num);
    temp_ptr += 8;
    *temp_ptr = '\0';
    DEBUG_PRINT("Bootstrap info out %s\n", ring_qp_out);

    local_addr_len = pg_size * QPLEN_XDR;
    alladdrs_self = (char *) malloc(local_addr_len + 4);

    gethostname(hostname, HOSTNAME_LEN);
    if (!hostname) {
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
      get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], rdma_default_port), temp_hostid,
                    "----------ABCD----------");
#ifdef ONE_SIDED
            sprintf((char *) temp_ptr + 43, "%8s:", "WITH-1SC");
#endif
        } else {
            /* Stash in my qp_num, and memory */
            int     qp_num_1sc;
            long    local_array = 0;
#ifdef RDMA_FAST_PATH
            unsigned long osu_recv_buff = 0;
#endif
            MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef RDMA_FAST_PATH
            osu_recv_buff = (unsigned long) vc->mrail.rfp.RDMA_recv_buf;
            sprintf(temp_ptr, "%08X:%016lX:%016lX:",
                    vc->mrail.qp_hndl[0]->qp_num,
                    vc->mrail.rfp.RDMA_recv_buf_mr[0]->rkey,
                    osu_recv_buff);
#else
            sprintf(temp_ptr, "%08X:%16s:%16s:",
		    vc->mrail.qp_hndl[0]->qp_num,
                    "NO-RDMA-FASTPATH", "NO-RDMA-FASTPATH");
#endif

#ifdef ONE_SIDED
             qp_num_1sc = vc->mrail.qp_hndl_1sc->qp_num;
             sprintf((char *) temp_ptr + 43, "%08X:", qp_num_1sc);
#endif
            DEBUG_PRINT("Before exchange, qp %08X, rkey %016lX,"
                        " recv_buff %016lX\n",
                         vc->mrail.qp_hndl[0]->qp_num,
                        vc->mrail.rfp.RDMA_remote_buf_rkey,
                        osu_recv_buff);
        }
        DEBUG_PRINT("Before exchange, info out %s\n", temp_ptr);
        temp_ptr += QPLEN_XDR;
    }

    /* Chop off any remanant bytes */
    *temp_ptr = '\0';
#ifdef USE_MPD_RING
    /* Save the static buffer address */
    DEBUG_PRINT("Bootstrap info out %s\n", ring_qp_out);
    rdma_pmi_exchange_addresses(pg_rank, pg_size, ring_qp_out,
                                strlen(ring_qp_out), ring_qp_in);
    DEBUG_PRINT("Bootstrap info in %s\n", ring_qp_in);

    /* XXX: Enable the ring to exhange all the others queue_pair numbers.
     *   Create memory handle 
     *   Exchange lhs, rhs and memory handle over PMI interface 
     *   Exchange rdma_iba_table_out and recv into rdma_iba_addr_table */
    DEBUG_PRINT("ring exchange out %s\n", alladdrs_self);
    ibv_bootstrap_ring(lhs, rhs, ring_qp_in,
                           pg_rank, pg_size, alladdrs_self,
                           proc->boot_mem, proc->boot_mem_hndl);
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

        /* Get the qp, key and buffer for this process */
        temp_ptr = alladdr_inv + pg_rank * QPLEN_XDR;

#ifdef RDMA_FAST_PATH
        num_tokens = sscanf(temp_ptr, "%08X:%016lX:%016lX:",
                            &rdma_iba_addr_table.qp_num_rdma[i][0],
                             &vc->mrail.rfp.RDMA_remote_buf_rkey,
                            &temp_recv_buff);
        DEBUG_PRINT("AFTER MPD exchange, remote qpnum from rank %d: %08x\n",
                i, rdma_iba_addr_table.qp_num_rdma[i][0]);
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
                    vc->mrail.rfp.RDMA_remote_buf_rkey,
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
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 i, j;
    int                 hca_index, qp_index;
    int                 ret = 0;

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
    qp_attr.ah_attr.port_num    =   rdma_default_port;

    qp_attr_mask        |=  IBV_QP_STATE; 
    qp_attr_mask        |=  IBV_QP_PATH_MTU;
    qp_attr_mask        |=  IBV_QP_RQ_PSN;
    qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
    qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
    qp_attr_mask        |=  IBV_QP_AV;

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;

        j = 0;
        hca_index   = 0;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef ONE_SIDED
        if (i == pg_rank)
            qp_attr.dest_qp_num = dst_qp;
        else
            qp_attr.dest_qp_num =
                rdma_iba_addr_table.qp_num_onesided[i][0];

        qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][0];
        qp_attr_mask    |=  IBV_QP_DEST_QPN;

        if (ibv_modify_qp( vc->mrail.qp_hndl_1sc,
                             &qp_attr, qp_attr_mask)) {
            fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTR\n", 
                    __FILE__, __LINE__);
            return 1;
        }
#endif                          /* End of ONE_SIDED */

        qp_attr.dest_qp_num =
            rdma_iba_addr_table.qp_num_rdma[i][hca_index];
        qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][hca_index];

        qp_attr_mask    |=  IBV_QP_DEST_QPN;

        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {
            DEBUG_PRINT("!!!Modify qp %d with qpnum %08x, dlid %x\n", qp_index,
                qp_attr.dest_qp_num, qp_attr.ah_attr.dlid);
            if (ibv_modify_qp( vc->mrail.qp_hndl[qp_index], 
                               &qp_attr, qp_attr_mask)) {
                fprintf(stderr, "[%s:%d] Could not modify qp" 
                        "to RTR\n",__FILE__, __LINE__); 
                return 1;
            }

            j++;
            if (j == vc->mrail.subrail_per_hca) {
                j = 0;
                hca_index++;
                qp_attr.dest_qp_num =
                    rdma_iba_addr_table.qp_num_rdma[i][hca_index];
                qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][hca_index];
            }
        }
    }

    /************** RTR --> RTS *******************/
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

    for (i = 0; i < pg_size; i++) {
        hca_index = 0, j = 0;

        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#ifdef ONE_SIDED
        if (ibv_modify_qp( vc->mrail.qp_hndl_1sc,
                           &qp_attr, qp_attr_mask))
        {
            fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTS\n",
                    __FILE__, __LINE__);
            return 1;
        }
#endif                          /* ONE_SIDED */

        if (i == pg_rank)
            continue;
        for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
             qp_index++) {
            if (ibv_modify_qp(  vc->mrail.qp_hndl[qp_index], &qp_attr,
                                qp_attr_mask))
            {
                fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTS\n",
                    __FILE__, __LINE__);
                return 1;
            }

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

    /* Now we will need to */
    for (i = 0; i < channels; i++) {
        vc->mrail.send_wqes_avail[i]    = rdma_default_max_wqe - 20;
        vc->mrail.ext_sendq_head[i]     = NULL;
        vc->mrail.ext_sendq_tail[i]     = NULL;
    }
    vc->mrail.next_packet_expected  = 0;
    vc->mrail.next_packet_tosend    = 0;

#ifdef RDMA_FAST_PATH
    for (i = 0; i < num_rdma_buffer; i++) {
        vbuf_init_rdma_write(&vc->mrail.rfp.RDMA_send_buf[i]);
        vc->mrail.rfp.RDMA_send_buf[i].vc       = (void *) vc;
        vc->mrail.rfp.RDMA_send_buf[i].padding  = FREE_FLAG;
        vc->mrail.rfp.RDMA_recv_buf[i].vc       = (void *) vc;
        vc->mrail.rfp.RDMA_recv_buf[i].padding  = BUSY_FLAG;
    }

    vc->mrail.rfp.rdma_credit = 0;
#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif
    vc->mrail.cmanager.total_subrails       = 2;
    vc->mrail.cmanager.num_local_pollings   = 1;

    vc->mrail.cmanager.poll_channel = malloc(sizeof(vbuf *(**)(void *)));
    
#else
    vc->mrail.cmanager.total_subrails       = 1;
    vc->mrail.cmanager.num_local_pollings   = 0;
#endif
    for (i = 0; i < vc->mrail.cmanager.total_subrails; i++) {
        vc->mrail.cmanager.v_queue_head[i] =
            vc->mrail.cmanager.v_queue_tail[i] = NULL;
        vc->mrail.cmanager.len[i] = 0;
    }

    DEBUG_PRINT("Cmanager total rail %d, local polling %d\n",
                vc->mrail.cmanager.total_subrails, vc->mrail.cmanager.num_local_pollings);

    vc->mrail.srp.backlog.len       = 0;
    vc->mrail.srp.backlog.vbuf_head = NULL;
    vc->mrail.srp.backlog.vbuf_tail = NULL;

    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow  = NULL;
    vc->mrail.inflow    = 0;

    for (i = 0; i < vc->mrail.num_total_subrails; i++) {
        int k;

        MRAILI_CHANNEL_INFO_INIT(subchannel, i, vc);

        for (k = 0; k < rdma_initial_prepost_depth; k++) {
            PREPOST_VBUF_RECV(vc, subchannel);
        }

        vc->mrail.srp.remote_credit[i]  = rdma_initial_credits;
        vc->mrail.srp.remote_cc[i]      = rdma_initial_credits;
        vc->mrail.srp.local_credit[i]   = 0;
        vc->mrail.srp.preposts[i]       = rdma_initial_prepost_depth;
        vc->mrail.srp.initialized[i]    =
            (rdma_prepost_depth == rdma_initial_prepost_depth);

        vc->mrail.srp.rendezvous_packets_expected[i] = 0;
    }
    DEBUG_PRINT
        ("[Init:priv] remote_credit %d, remote_cc %d, local_credit %d, prepost%d, local_polling %d\n ",
         vc->mrail.srp.remote_credit[0], vc->mrail.srp.remote_cc[0],
         vc->mrail.srp.local_credit[0], vc->mrail.srp.preposts[0], vc->mrail.cmanager.num_local_pollings);
}
