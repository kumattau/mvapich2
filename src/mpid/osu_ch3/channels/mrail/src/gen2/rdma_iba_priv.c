/* Copyright (c) 2003-2006, The Ohio State University. All rights
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

#define IBA_PMI_ATTRLEN (16)
#define IBA_PMI_VALLEN  (4096)

#ifdef ONE_SIDED
static uint32_t dst_qp;
#endif

#ifdef USE_MPD_RING


#define MPD_WINDOW 10
#define ADDR_PKT_SIZE (sizeof(struct addr_packet) + ((pg_size - 1) * sizeof(struct host_addr_inf)))
#define ADDR_INDEX(_p, _i) ((struct addr_packet *)(_p + (_i * ADDR_PKT_SIZE)))

typedef struct init_addr_inf {
    uint16_t    lid;
    uint32_t    qp_num[2];
} init_addr_inf;

typedef struct host_addr_inf {
    uint32_t    sr_qp_num; 
#ifdef ONE_SIDED
    uint32_t    osc_qp_num;
#endif
} host_addr_inf;

typedef struct addr_packet {
    int         rank;
    int         host_id;
    int         lid;
    int         rail;
    struct host_addr_inf val[1];
} addr_packet;

typedef struct ring_packet {
    int     type;
    int     value;
} ring_packet;


static void MPI_Ring_Setup(struct init_addr_inf * neighbor_addr,
              struct MPIDI_CH3I_RDMA_Process_t *proc);
void 
MPI_Ring_Exchange(struct ibv_mr * addr_hndl, void * addr_pool,
        struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank, int pg_size);

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


#ifdef SRQ
static struct ibv_srq *create_srq(struct MPIDI_CH3I_RDMA_Process_t *proc,
                                int hca_num)
{
    struct ibv_srq_init_attr srq_init_attr;
    struct ibv_srq *srq_ptr = NULL;

    memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context = proc->nic_context[hca_num];
    srq_init_attr.attr.max_wr = viadev_srq_size;
    srq_init_attr.attr.max_sge = 1;
    /* The limit value should be ignored during SRQ create */
    srq_init_attr.attr.srq_limit = viadev_srq_limit;

    srq_ptr = ibv_create_srq(proc->ptag[hca_num], &srq_init_attr);

    if (!srq_ptr) {
        ibv_error_abort(IBV_RETURN_ERR, "Error creating SRQ\n");
    }

    return srq_ptr;
}
#endif

int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  int pg_rank, int pg_size)
{
    struct ibv_device       *ib_dev = NULL;
    struct ibv_qp_init_attr attr;
#ifdef USE_MPD_RING
    struct ibv_qp_init_attr boot_attr;
#endif
    struct ibv_qp_attr      qp_attr;
    struct ibv_port_attr    port_attr;
#ifdef GEN2_OLD_DEVICE_LIST_VERB
    struct dlist *dev_list;
#else
    struct ibv_device **dev_list;
#endif
    MPIDI_VC_t	*vc;

    int i, j, k;
    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;
    int ports[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int lids[MAX_NUM_HCAS][MAX_NUM_PORTS];

#ifdef GEN2_OLD_DEVICE_LIST_VERB
    dev_list = ibv_get_devices();
#else
    dev_list = ibv_get_device_list(NULL);
#endif
    /* step 1: open hca, create ptags  and create cqs */
    for (i = 0; i < rdma_num_hcas; i++) {
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
        if(!strncmp(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32) || rdma_num_hcas > 1) {
            /* User hasn't specified any HCA name
             * We will use the first available HCA */
            if(dev_list[i]) {
                ib_dev = dev_list[i];
            }
        } else {
            /* User specified a HCA, try to look for it */
	    j = 0;
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
        /* Create the context for the HCA */
        proc->nic_context[i] = ibv_open_device(ib_dev);
        if (!proc->nic_context[i]) {
            fprintf(stderr, "Fail to open HCA number %d\n", i);
            return -1;
        }
   
        /* Allocate the protection domain for the HCA */
        proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
        if (!proc->ptag[i]) {
            fprintf(stderr, "Fail to alloc pd number %d\n", i);
            goto err;
        }
        
        /* Allocate the completion queue handle for the HCA */
        proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

        if (!proc->cq_hndl[i]) {
            fprintf(stderr, "cannot create cq\n");
            goto err_pd;
        }
#ifdef SRQ
        proc->srq_hndl[i] = create_srq(proc, i);
#endif

#ifdef ONE_SIDED
        proc->cq_hndl_1sc[i] = ibv_create_cq(proc->nic_context[i],
                            rdma_default_max_cq_size, NULL, NULL, 0);
        if (!proc->cq_hndl_1sc[i]) {
            fprintf(stderr, 
                     "cannot allocate CQ for one-sided communication\n");
            goto err_cq;
        }
#endif
	/* detecting active ports */
	if (rdma_default_port < 0 || rdma_num_ports > 1) {
	    k = 0;
	    for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; j ++) {
		if ((! ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
					j, &port_attr)) &&
			port_attr.state == IBV_PORT_ACTIVE &&
			port_attr.lid) {
		    lids[i][k]    = port_attr.lid;
		    ports[i][k++] = j;
		} 
	    }
	    if (k < rdma_num_ports) {
		ibv_error_abort(IBV_STATUS_ERR, "Not enough port is in active state"
				"needed active ports %d\n", rdma_num_ports);
	    }
	} else {
	    if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
				rdma_default_port, &port_attr)
		|| (!port_attr.lid )
		|| (port_attr.state != IBV_PORT_ACTIVE))
	    {
		ibv_error_abort(IBV_STATUS_ERR, "user specified port %d: fail to"
						"query or not ACTIVE\n",
						rdma_default_port);
	    }
	    ports[i][0] = rdma_default_port;
	    lids[i][0]  = port_attr.lid;
	}
    }

    rdma_default_port 	    = ports[0][0];
    /* step 2: create qps for all vc */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE 
                                    | IBV_ACCESS_LOCAL_WRITE;

    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(cached_pg, i, &vc);

        vc->mrail.num_rails = rdma_num_rails;
        vc->mrail.rails = malloc(sizeof *vc->mrail.rails * vc->mrail.num_rails);
        if (!vc->mrail.rails) {
            fprintf(stderr, "[INIT] Fail to allocate resources for multirails\n");
            goto err_cq;
        }

	vc->mrail.srp.credits = malloc(sizeof *vc->mrail.srp.credits * vc->mrail.num_rails);
	if (!vc->mrail.srp.credits) {
	    fprintf(stderr, "[INIT] Fail to allocate resources for credits array\n");
	    goto err_cq;
	}

        if (i == pg_rank)
            continue;

	for (   rail_index = 0; 
        	rail_index < vc->mrail.num_rails;
        	rail_index++) 
	{
            hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
	    port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
	    memset(&attr, 0, sizeof attr);
	    attr.cap.max_send_wr = rdma_default_max_wqe;
#ifdef SRQ
        attr.cap.max_recv_wr = 0;
        attr.srq = proc->srq_hndl[hca_index];
#else
        attr.cap.max_recv_wr = rdma_default_max_wqe;
#endif
	    attr.cap.max_send_sge = rdma_default_max_sg_list;
	    attr.cap.max_recv_sge = rdma_default_max_sg_list;
	    attr.cap.max_inline_data = RDMA_MAX_INLINE_SIZE;
	    attr.send_cq = proc->cq_hndl[hca_index];
	    attr.recv_cq = proc->cq_hndl[hca_index];
	    attr.qp_type = IBV_QPT_RC;
	    attr.sq_sig_all = 0;

	    vc->mrail.rails[rail_index].qp_hndl = 
	        ibv_create_qp(proc->ptag[hca_index], &attr);
	    if (!vc->mrail.rails[rail_index].qp_hndl) {
	        fprintf(stderr, "[Init] Fail to create qp for rank %d\n", i);
	        goto err_cq; 
	    }
	    vc->mrail.rails[rail_index].nic_context = proc->nic_context[hca_index];
	    vc->mrail.rails[rail_index].hca_index   = hca_index;
	    vc->mrail.rails[rail_index].port	= ports[hca_index][port_index];
	    vc->mrail.rails[rail_index].lid         = lids[hca_index][port_index];
	    vc->mrail.rails[rail_index].cq_hndl	= proc->cq_hndl[hca_index];

	    rdma_iba_addr_table.lid[i][rail_index] = lids[hca_index][port_index];
	    rdma_iba_addr_table.qp_num_rdma[i][rail_index] =
	        vc->mrail.rails[rail_index].qp_hndl->qp_num;

	    qp_attr.qp_state        = IBV_QPS_INIT;
	    qp_attr.pkey_index      = 0;
	    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;

	    qp_attr.port_num = ports[hca_index][port_index];

	    if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
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
        attr.send_cq = proc->cq_hndl_1sc[hca_index];
        attr.recv_cq = proc->cq_hndl_1sc[hca_index];
        attr.qp_type = IBV_QPT_RC;
        attr.sq_sig_all = 0;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);

        for (   rail_index = 0;
                 rail_index < vc->mrail.num_rails;
                 rail_index++)
        {
            hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
            port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;

            vc->mrail.rails[rail_index].qp_hndl_1sc = ibv_create_qp(proc->ptag[hca_index], &attr);
            if (!vc->mrail.rails[rail_index].qp_hndl_1sc) {
                fprintf(stderr, "[Init] Fail to create one sided qp for rank %d\n", i);
                goto err_cq;
            }

            if (i == pg_rank) {
                dst_qp = vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
            }
            rdma_iba_addr_table.qp_num_onesided[i][rail_index] =
                vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
            DEBUG_PRINT("Created onesdied qp %d, num %X, qp_handle is %x\n",
                i, rdma_iba_addr_table.qp_num_onesided[i][rail_index],vc->mrail.rails[rail_index].qp_hndl_1sc);

            if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl_1sc, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
                fprintf(stderr, "Failed to modify One sided QP to INIT\n");
                goto err_cq;
            }
       }
    }
#endif

    DEBUG_PRINT("Return from init hca\n");
    return 0;
err_cq:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->cq_hndl[i])
            ibv_destroy_cq(proc->cq_hndl[i]);
    }
#ifdef ONE_SIDED
    for (i = 0; i < rdma_num_hcas; i ++) {
         if (proc->cq_hndl_1sc[0])
             ibv_destroy_cq(proc->cq_hndl_1sc[i]);
    }
#endif
err_pd:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->ptag[i])
            ibv_dealloc_pd(proc->ptag[i]);
    }
err:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->nic_context[i])
            ibv_close_device(proc->nic_context[i]);
    }
    return -1;

}

/* Allocate memory and handlers */
int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         int pg_rank, int pg_size)
{
    int ret = 0, i = 0;
    MPIDI_VC_t * vc;
#if defined(RDMA_FAST_PATH)
    int iter_hca;
    int vbuf_alignment = 64;
    int pagesize = getpagesize();
#endif
    /* FIrst allocate space for RDMA_FAST_PATH for every connection */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#ifdef ONE_SIDED
	vc->mrail.rails[0].postsend_times_1sc = 0;
#endif

#ifdef RDMA_FAST_PATH
        if (i == pg_rank)
            continue;

        /* Allocate RDMA buffers, have an extra one for some ACK message */
#define RDMA_ALIGNMENT 4096
#define RDMA_ALIGNMENT_OFFSET (4096)
        /* allocate RDMA buffers */
        if (posix_memalign
            ((void **) &vc->mrail.rfp.RDMA_send_buf, vbuf_alignment,
             sizeof(struct vbuf) * num_rdma_buffer)) {
                ibv_error_abort(GEN_EXIT_ERR, "Unable to alloc rdma buffers");
        }

        if (posix_memalign
            ((void **) &vc->mrail.rfp.RDMA_recv_buf, vbuf_alignment,
             sizeof(struct vbuf) * num_rdma_buffer)) {
                ibv_error_abort(GEN_EXIT_ERR, "Unable to alloc rdma recv buffers");
        }

        memset(vc->mrail.rfp.RDMA_send_buf, 0,
               sizeof(struct vbuf) * (num_rdma_buffer));
        memset(vc->mrail.rfp.RDMA_recv_buf, 0,
               sizeof(struct vbuf) * (num_rdma_buffer));

        if (posix_memalign((void **) &vc->mrail.rfp.RDMA_send_buf_DMA, pagesize,
                           rdma_vbuf_total_size * num_rdma_buffer)) {
            ibv_error_abort(GEN_EXIT_ERR, "Unable to malloc rdma buffers");
        }

        if (posix_memalign((void **) &vc->mrail.rfp.RDMA_recv_buf_DMA, pagesize,
                           rdma_vbuf_total_size * num_rdma_buffer)) {
            ibv_error_abort(GEN_EXIT_ERR, "Unable to malloc rdma buffers");
        }

        memset(vc->mrail.rfp.RDMA_send_buf_DMA, 0,
               rdma_vbuf_total_size * num_rdma_buffer);
        memset(vc->mrail.rfp.RDMA_recv_buf_DMA, 0,
               rdma_vbuf_total_size * num_rdma_buffer);

        /* set pointers */
        vc->mrail.rfp.phead_RDMA_send = 0;
        vc->mrail.rfp.ptail_RDMA_send = num_rdma_buffer - 1;
        vc->mrail.rfp.p_RDMA_recv = 0;
        vc->mrail.rfp.p_RDMA_recv_tail = num_rdma_buffer - 1;

        for (iter_hca = 0; iter_hca < rdma_num_hcas; iter_hca++) {
            /* initialize unsignal record */
            vc->mrail.rfp.RDMA_send_buf_mr[iter_hca] = 
                    ibv_reg_mr( MPIDI_CH3I_RDMA_Process.ptag[iter_hca],
                                vc->mrail.rfp.RDMA_send_buf_DMA,
                                rdma_vbuf_total_size * (num_rdma_buffer),
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE );
            if (!vc->mrail.rfp.RDMA_send_buf_mr[iter_hca]) {
                ret = -1;
                fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                                "Unable to register RDMA buffer\n");
                goto err_buf;
            }

            vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca] = 
                    ibv_reg_mr( MPIDI_CH3I_RDMA_Process.ptag[iter_hca],
                                vc->mrail.rfp.RDMA_recv_buf_DMA,
                                rdma_vbuf_total_size * (num_rdma_buffer),
                                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE  );

            if (!vc->mrail.rfp.RDMA_send_buf_mr[iter_hca] ||
                !vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca] ) {
                fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__, 
                                "Unable to register RDMA buffer\n");
                goto err_sreg;
            }
            DEBUG_PRINT
                ("Created buff for rank %d, recvbuff %p, key %p\n", i,
                 (uintptr_t) vc->mrail.rfp.RDMA_recv_buf_DMA,
                 vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca]->rkey);
        }
#elif defined(ADAPTIVE_RDMA_FAST_PATH)
	vc->mrail.rfp.phead_RDMA_send = 0;
	vc->mrail.rfp.ptail_RDMA_send = 0;
	vc->mrail.rfp.p_RDMA_recv = 0;
	vc->mrail.rfp.p_RDMA_recv_tail = 0;
#endif
    }

#if defined(ADAPTIVE_RDMA_FAST_PATH)
    MPIDI_CH3I_RDMA_Process.polling_group_size = 0;
    if (rdma_polling_set_limit > 0) 
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t **)
                        malloc(rdma_polling_set_limit * sizeof(MPIDI_VC_t *));
    else 
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t **)
                        malloc(pg_size * sizeof(MPIDI_VC_t *));
    
    if (! MPIDI_CH3I_RDMA_Process.polling_set) {
	fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
		"unable to allocate space for polling set\n");
	
	goto err_reg;
    }
#endif

    /* We need now allocate vbufs for send/recv path */
    ret = allocate_vbufs(MPIDI_CH3I_RDMA_Process.ptag, rdma_vbuf_pool_size);
    if (ret) {
        goto err_reg;
    }
   
    /* Post the buffers for the SRQ */

#ifdef SRQ
    pthread_spin_init(&MPIDI_CH3I_RDMA_Process.srq_post_lock, 0);
    pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

    int hca_num = 0;
    
    for(hca_num = 0; hca_num < rdma_num_hcas; hca_num++) { 
        MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] = 
            viadev_post_srq_buffers(viadev_srq_size, hca_num);

        {
            struct ibv_srq_attr srq_attr;
            srq_attr.max_wr = viadev_srq_size;
            srq_attr.max_sge = 1;
            srq_attr.srq_limit = viadev_srq_limit;

            if (ibv_modify_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                        &srq_attr, IBV_SRQ_LIMIT)) {
                ibv_error_abort(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
            }

            /* Start the async thread which watches for SRQ limit events */
            pthread_create(&MPIDI_CH3I_RDMA_Process.async_thread[hca_num], NULL,
                    (void *) async_thread, (void *) MPIDI_CH3I_RDMA_Process.nic_context[hca_num]);
        }
    }

    pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);

#endif
    
    return MPI_SUCCESS;
#ifdef RDMA_FAST_PATH
err_reg:
    for (iter_hca = 0; iter_hca < rdma_num_hcas; iter_hca ++)
        ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[iter_hca]);
err_sreg:
    for (iter_hca = 0; iter_hca < rdma_num_hcas; iter_hca ++)
        ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[iter_hca]);
err_buf:
    free(vc->mrail.rfp.RDMA_recv_buf_DMA);
    free(vc->mrail.rfp.RDMA_send_buf_DMA);
    free(vc->mrail.rfp.RDMA_recv_buf);
    free(vc->mrail.rfp.RDMA_send_buf);
#else
err_reg:
#endif
    return ret;
}

#ifdef USE_MPD_RING

void
MPD_Ring_Startup(struct MPIDI_CH3I_RDMA_Process_t *proc,
        int pg_rank, int pg_size) {

    struct ibv_mr * addr_hndl;
    void * addr_pool;
    struct init_addr_inf neighbor_addr[2];
    int i, bootstrap_len, qp_len;  

    char ring_qp_out[64];
    char ring_qp_in[128];
    char tmp_str[9];
    char *tmp_char_ptr;

    tmp_str[8] = '\0';


    DEBUG_PRINT("Before formatting ring_qp_out\n");
    sprintf(ring_qp_out, "%08d%08d%08d",
             get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], 
                           rdma_default_port),
             proc->boot_qp_hndl[0]->qp_num,
             proc->boot_qp_hndl[1]->qp_num
           );

    DEBUG_PRINT("After setting LID: %d, qp0: %d, qp1: %d\n", get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], rdma_default_port),
            proc->boot_qp_hndl[0]->qp_num,
            proc->boot_qp_hndl[1]->qp_num
            );

    DEBUG_PRINT("after formatting ring_qp_out\n");
    
    bootstrap_len = strlen(ring_qp_out);
    rdma_pmi_exchange_addresses(pg_rank, pg_size, ring_qp_out,
            bootstrap_len, ring_qp_in);

    DEBUG_PRINT("after pmi exchange\n");



    qp_len = 8;
    for (i = 0; i < 2; i++) {
        /* For hca_lid */
        tmp_char_ptr = ring_qp_in + i * (qp_len * 3);
        strncpy(tmp_str, tmp_char_ptr, qp_len);
        DEBUG_PRINT("Got hca_lid %s \n", tmp_str);
        neighbor_addr[i].lid = atoi(tmp_str);

        tmp_char_ptr += qp_len;
        strncpy(tmp_str, tmp_char_ptr, qp_len);
        DEBUG_PRINT("Got queue_pair 0: %s \n", tmp_str);
        neighbor_addr[i].qp_num[0] = atoi(tmp_str);

        tmp_char_ptr += qp_len;
        strncpy(tmp_str, tmp_char_ptr, qp_len); 
        DEBUG_PRINT("Got queue_pair 1: %s \n", tmp_str);
        neighbor_addr[i].qp_num[1] = atoi(tmp_str);
    }

    /* Allocate the memory we need */

    addr_pool = malloc(MPD_WINDOW * ADDR_PKT_SIZE);
    addr_hndl = ibv_reg_mr(proc->ptag[0],
            addr_pool, MPD_WINDOW * ADDR_PKT_SIZE,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        
    if(addr_hndl == NULL) {
        ibv_error_abort(GEN_EXIT_ERR,"ibv_reg_mr failed for addr_hndl\n");
    }

    DEBUG_PRINT("val of addr_pool is: %d, handle: %d\n", 
            addr_pool, addr_hndl->handle);

    /* Setup the necessary qps for the bootstrap ring */
    MPI_Ring_Setup(neighbor_addr, proc);
    MPI_Ring_Exchange(addr_hndl, addr_pool, proc, pg_rank, pg_size);

    proc->boot_mem_hndl = addr_hndl;
    proc->boot_mem      = addr_pool;
}


/* Set up a ring of qp's, here a separate bootstrap channel is used.*/
static void MPI_Ring_Setup(struct init_addr_inf * neighbor_addr,
        struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_attr      qp_attr;

    uint32_t    qp_attr_mask = 0;
    int         i;
    int         ret;

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
}

void 
MPI_Ring_Exchange(struct ibv_mr * addr_hndl, void * addr_pool,
        struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank, int pg_size)
{
    int i, j, ne, index_to_send, rail_index;
    int hostid;
    char hostname[HOSTNAME_LEN + 1];

    uint64_t last_send = -2;
    uint64_t last_send_comp = -1;

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
    struct ibv_cq * cq_hndl;
    struct ibv_wc rc;

    MPIDI_VC_t * vc;

    /* Post the window of recvs: The first entry
     * is not posted since it is used for the 
     * initial send
     */

    DEBUG_PRINT("Posting recvs\n");

    for(i = 1; i < MPD_WINDOW; i++) {
        rr.wr_id   = i;
        rr.num_sge = 1;
        rr.sg_list = &(sg_entry_r);
        rr.next    = NULL;
        sg_entry_r.lkey = addr_hndl->lkey;
        sg_entry_r.addr = (uintptr_t) ADDR_INDEX(addr_pool, i);
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

        send_packet          = ADDR_INDEX(addr_pool, index_to_send);
        DEBUG_PRINT("formatting 1\n");
        send_packet->rank    = pg_rank;
        send_packet->rail    = rail_index;
        send_packet->host_id = hostid;
        DEBUG_PRINT("formatting 2\n");

        DEBUG_PRINT("formatting \n");

        for(i = 0; i < pg_size; i++) {
            if(i == pg_rank) {
                send_packet->val[i].sr_qp_num = -1;
#ifdef ONE_SIDED
                send_packet->val[i].osc_qp_num = -1;
#endif
            } else {
                MPIDI_PG_Get_vc(cached_pg, i, &vc);

                send_packet->lid     = vc->mrail.rails[rail_index].lid;
                send_packet->val[i].sr_qp_num = 
                    vc->mrail.rails[rail_index].qp_hndl->qp_num;
#ifdef ONE_SIDED
                send_packet->val[i].osc_qp_num =
                    vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
#endif
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
                ADDR_INDEX(addr_pool, index_to_send);
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

                        recv_packet = ADDR_INDEX(addr_pool, rc.wr_id);


                        rdma_iba_addr_table.lid[recv_packet->rank][rail_index] =
                            recv_packet->lid;

                        rdma_iba_addr_table.hostid[recv_packet->rank][rail_index] =
                            recv_packet->host_id;

#ifdef _SMP_
                        MPIDI_PG_Get_vc(cached_pg, recv_packet->rank, &vc);
                        vc->smp.hostid = recv_packet->host_id;
#endif
                        
                        rdma_iba_addr_table.qp_num_rdma[recv_packet->rank][rail_index] =
                            recv_packet->val[pg_rank].sr_qp_num;

#ifdef ONE_SIDED
                        rdma_iba_addr_table.qp_num_onesided[recv_packet->rank][rail_index] =
                            recv_packet->val[pg_rank].osc_qp_num;
#endif

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
                            ADDR_INDEX(addr_pool, rr.wr_id);
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
                ibv_error_abort(GEN_EXIT_ERR,"Error code %d in polled desc!\n");
            }
            last_send_comp = rc.wr_id;
        }
    }

}



#endif

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            int pg_rank, int pg_size)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 i;
    int                 rail_index;
    MPIDI_VC_t * vc;

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

    qp_attr_mask        |=  IBV_QP_STATE; 
    qp_attr_mask        |=  IBV_QP_PATH_MTU;
    qp_attr_mask        |=  IBV_QP_RQ_PSN;
    qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
    qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
    qp_attr_mask        |=  IBV_QP_AV;

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);

#ifdef ONE_SIDED
 
     for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
        if (i == pg_rank)
            qp_attr.dest_qp_num = dst_qp;
        else
            qp_attr.dest_qp_num =
                rdma_iba_addr_table.qp_num_onesided[i][rail_index];

        qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index];
        qp_attr_mask    |=  IBV_QP_DEST_QPN;

        if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl_1sc,
                             &qp_attr, qp_attr_mask)) {
            fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTR\n", 
                    __FILE__, __LINE__);
            return 1;
        }
    }
#endif                          /* End of ONE_SIDED */
	for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
            qp_attr.dest_qp_num =
                rdma_iba_addr_table.qp_num_rdma[i][rail_index];
            qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index];
     	    qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;

            qp_attr_mask    |=  IBV_QP_DEST_QPN;

            DEBUG_PRINT("!!!Modify qp %d with qpnum %08x, dlid %x\n", rail_index,
                qp_attr.dest_qp_num, qp_attr.ah_attr.dlid);
            if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl,
                               &qp_attr, qp_attr_mask)) {
                fprintf(stderr, "[%s:%d] Could not modify qp" 
                        "to RTR\n",__FILE__, __LINE__); 
                return 1;
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
        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#ifdef ONE_SIDED
        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
             if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl_1sc,
                           &qp_attr, qp_attr_mask))
             {
                fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTS\n",
                        __FILE__, __LINE__);
                return 1;
             }
         }     
#endif                          /* ONE_SIDED */

        if (i == pg_rank)
            continue;
        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            if (ibv_modify_qp(  vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                                qp_attr_mask))
            {
                fprintf(stderr, "[%s:%d] Could not modify rdma qp to RTS\n",
                    __FILE__, __LINE__);
                return 1;
            }
        }
    }

    DEBUG_PRINT("Done enabling connections\n");
    return 0;
}

void MRAILI_Init_vc(MPIDI_VC_t * vc, int pg_rank)
{
    int i;

    /* Now we will need to */
    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_wqe - 20;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
    }

    vc->mrail.next_packet_expected  = 0;
    vc->mrail.next_packet_tosend    = 0;

#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
#if defined(RDMA_FAST_PATH)
    for (i = 0; i < num_rdma_buffer; i++) {
        vbuf_init_rdma_write(&vc->mrail.rfp.RDMA_send_buf[i]);
        vc->mrail.rfp.RDMA_send_buf[i].vc       = (void *) vc;
        vc->mrail.rfp.RDMA_send_buf[i].padding  = FREE_FLAG;
        vc->mrail.rfp.RDMA_recv_buf[i].vc       = (void *) vc;
        vc->mrail.rfp.RDMA_recv_buf[i].padding  = BUSY_FLAG;
	vc->mrail.rfp.RDMA_send_buf[i].head_flag =
		(VBUF_FLAG_TYPE *) ( (char *)(vc->mrail.rfp.RDMA_send_buf_DMA)
		+ (i + 1) * rdma_vbuf_total_size - sizeof (VBUF_FLAG_TYPE)) ;
	vc->mrail.rfp.RDMA_send_buf[i].buffer =
		(char *) ((char *)(vc->mrail.rfp.RDMA_send_buf_DMA) +
		i * rdma_vbuf_total_size );
	vc->mrail.rfp.RDMA_recv_buf[i].head_flag =
		(VBUF_FLAG_TYPE *) ( (char *)(vc->mrail.rfp.RDMA_recv_buf_DMA)
		+ (i + 1) * rdma_vbuf_total_size - sizeof (VBUF_FLAG_TYPE)) ;
	vc->mrail.rfp.RDMA_recv_buf[i].buffer =
		(char *) ((char *)(vc->mrail.rfp.RDMA_recv_buf_DMA) +
		i * rdma_vbuf_total_size );
    }
#endif
    vc->mrail.rfp.rdma_credit = 0;
#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = malloc(sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

#ifdef RDMA_FAST_PATH
    vc->mrail.cmanager.num_channels         = vc->mrail.num_rails + 1;
    vc->mrail.cmanager.num_local_pollings   = 1;
#else
    vc->mrail.cmanager.num_channels         = vc->mrail.num_rails;
    vc->mrail.cmanager.num_local_pollings   = 0;
#endif

#else
    vc->mrail.cmanager.num_channels         = vc->mrail.num_rails;
    vc->mrail.cmanager.num_local_pollings   = 0;
#endif

#ifdef ADAPTIVE_RDMA_FAST_PATH
    vc->mrail.rfp.eager_start_cnt           = 0;
    vc->mrail.rfp.in_polling_set            = 0;

    /* extra one channel for later increase the adaptive rdma */
    vc->mrail.cmanager.msg_channels = malloc(sizeof *vc->mrail.cmanager.msg_channels 
					* (vc->mrail.cmanager.num_channels + 1));
    if (!vc->mrail.cmanager.msg_channels) {
	ibv_error_abort(GEN_EXIT_ERR, "No resource for msg channels\n");
    }
    memset(vc->mrail.cmanager.msg_channels, 0, 
		sizeof *vc->mrail.cmanager.msg_channels
                * (vc->mrail.cmanager.num_channels + 1));
    
#else
    vc->mrail.cmanager.msg_channels = malloc(sizeof *vc->mrail.cmanager.msg_channels
					* vc->mrail.cmanager.num_channels);
    if (!vc->mrail.cmanager.msg_channels) {
	ibv_error_abort(GEN_EXIT_ERR, "No resource for msg channels\n");
    }
    memset(vc->mrail.cmanager.msg_channels, 0, 
		sizeof *vc->mrail.cmanager.msg_channels
                * vc->mrail.cmanager.num_channels);
#endif

    vc->mrail.cmanager.next_arriving = NULL;
    vc->mrail.cmanager.inqueue	     = 0;
    vc->mrail.cmanager.vc	     = (void *)vc;

    DEBUG_PRINT("Cmanager total channel %d, local polling %d\n",
                vc->mrail.cmanager.num_channels, vc->mrail.cmanager.num_local_pollings);

    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow  = NULL;
    vc->mrail.inflow    = 0;

    for (i = 0; i < vc->mrail.num_rails; i++) {
        int k;
#ifndef SRQ
        for (k = 0; k < rdma_initial_prepost_depth; k++) {
            PREPOST_VBUF_RECV(vc, i);
        }
#endif
        vc->mrail.srp.credits[i].remote_credit     = rdma_initial_credits;
        vc->mrail.srp.credits[i].remote_cc         = rdma_initial_credits;
        vc->mrail.srp.credits[i].local_credit      = 0;
        vc->mrail.srp.credits[i].preposts          = rdma_initial_prepost_depth;

#ifndef SRQ
        vc->mrail.srp.credits[i].initialized	   =
            (rdma_prepost_depth == rdma_initial_prepost_depth);
#else
        vc->mrail.srp.credits[i].initialized = 1; 
        vc->mrail.srp.credits[i].pending_r3_sends = 0;
#endif

	vc->mrail.srp.credits[i].backlog.len       = 0;
	vc->mrail.srp.credits[i].backlog.vbuf_head = NULL;
	vc->mrail.srp.credits[i].backlog.vbuf_tail = NULL;

        vc->mrail.srp.credits[i].rendezvous_packets_expected = 0;
    }
}
