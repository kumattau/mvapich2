/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#include <netdb.h>
#include <string.h>
#include <sysfs/libsysfs.h>
#include "vbuf.h"
#include "rdma_impl.h"
#include "pmi.h"
#include "ibv_param.h"
#include "rdma_cm.h"

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

static uint32_t dst_qp;

#define MPD_WINDOW 10
#define ADDR_PKT_SIZE (sizeof(struct addr_packet) + ((pg_size - 1) * sizeof(struct host_addr_inf)))
#define ADDR_INDEX(_p, _i) ((struct addr_packet *)(_p + (_i * ADDR_PKT_SIZE)))

typedef struct init_addr_inf {
    uint16_t    lid;
    uint32_t    qp_num[2];
} init_addr_inf;

typedef struct host_addr_inf {
    uint32_t    sr_qp_num; 
    uint32_t    osc_qp_num;
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

int power_two(int x)
{
    int pow = 1;

    while (x) {
        pow = pow * 2;
        x--;
    }

    return pow;
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

#undef FUNCNAME
#define FUNCNAME rdma_iba_bootstrap_cleanup
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_iba_bootstrap_cleanup(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int mpi_errno = MPI_SUCCESS;
    int ret;

    ret = ibv_dereg_mr(proc->boot_mem_hndl);
    free(proc->boot_mem);

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


struct ibv_srq *create_srq(struct MPIDI_CH3I_RDMA_Process_t *proc,
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

static int check_attrs( struct ibv_port_attr *port_attr, struct ibv_device_attr *dev_attr)
{
    int ret = 0;

    if(port_attr->active_mtu < rdma_default_mtu) {
        fprintf(stderr,
                "Active MTU is %d, MV2_DEFAULT_MTU set to %d. See User Guide\n",
                port_attr->active_mtu, rdma_default_mtu);
        ret = 1;
    }

    if(dev_attr->max_qp_rd_atom < rdma_default_qp_ous_rd_atom) {
        fprintf(stderr,
                "Max MV2_DEFAULT_QP_OUS_RD_ATOM is %d, set to %d\n",
                dev_attr->max_qp_rd_atom, rdma_default_qp_ous_rd_atom);
        ret = 1;
    }

    if(MPIDI_CH3I_RDMA_Process.has_srq) {
        if(dev_attr->max_srq_sge < rdma_default_max_sg_list) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_srq_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_srq_wr < viadev_srq_size) {
            fprintf(stderr,
                    "Max MV2_SRQ_SIZE is %d, set to %d\n",
                    dev_attr->max_srq_wr, (int) viadev_srq_size);
            ret = 1;
        }
    } else {
        if(dev_attr->max_sge < rdma_default_max_sg_list) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_qp_wr < rdma_default_max_wqe) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_WQE is %d, set to %d\n",
                    dev_attr->max_qp_wr, (int) rdma_default_max_wqe);
            ret = 1;
        }
    }
    if(dev_attr->max_cqe < rdma_default_max_cq_size) {
        fprintf(stderr,
                "Max MV2_DEFAULT_MAX_CQ_SIZE is %d, set to %d\n",
                dev_attr->max_cqe, (int) rdma_default_max_cq_size);
        ret = 1;
    }

    return ret;
}

#undef FUNCNAME
#define FUNCNAME rdma_open_hca
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    struct ibv_device       *ib_dev = NULL;
    struct ibv_device **dev_list;
    int i, j;
    int mpi_errno = MPI_SUCCESS;
#ifdef CRC_CHECK
    gen_crc_table();
#endif
    dev_list = ibv_get_device_list(NULL);

    for (i = 0; i < rdma_num_hcas; i ++) {

        if(!strncmp(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32) || 
                rdma_num_hcas > 1) {

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
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "No IB device found");
        }

        proc->nic_context[i] = ibv_open_device(ib_dev);

        proc->ib_dev[i] = ib_dev;

        if (!proc->nic_context[i]) {
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s %d", "Failed to open HCA number", i);
        }

    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/*
 * TODO add error handling
 */
int rdma_iba_hca_init_noqp(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  int pg_rank, int pg_size)
{
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;

    int i, j, k;

    /* step 1: open hca, create ptags  and create cqs */
    for (i = 0; i < rdma_num_hcas; i++) {

        if(ibv_query_device(proc->nic_context[i], &dev_attr)) {
            ibv_error_abort(GEN_EXIT_ERR,
                    "Error getting HCA attributes\n");
        }

        /* detecting active ports */
        if (rdma_default_port < 0 || rdma_num_ports > 1) {
            k = 0;
            for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; j ++) {
                if ((! ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                        j, &port_attr)) &&
                        port_attr.state == IBV_PORT_ACTIVE &&
                        port_attr.lid) {
                    MPIDI_CH3I_RDMA_Process.lids[i][k]    = port_attr.lid;
                    MPIDI_CH3I_RDMA_Process.ports[i][k++] = j;

                    if (check_attrs(&port_attr, &dev_attr)) {
                        ibv_error_abort(GEN_EXIT_ERR, "Attributes failed sanity check");
                    }
                }
            }
            if (k < rdma_num_ports) {
                ibv_va_error_abort(IBV_STATUS_ERR, "Not enough ports are in active state"
                                "needed active ports %d\n", rdma_num_ports);
            }
        } else {
            if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, &port_attr)
                || (!port_attr.lid )
                || (port_attr.state != IBV_PORT_ACTIVE))
            {
                ibv_va_error_abort(IBV_STATUS_ERR, "user specified port %d: fail to"
                                                "query or not ACTIVE\n",
                                                rdma_default_port);
            }
            MPIDI_CH3I_RDMA_Process.ports[i][0] = rdma_default_port;
            MPIDI_CH3I_RDMA_Process.lids[i][0]  = port_attr.lid;

            if (check_attrs(&port_attr, &dev_attr)) {
                ibv_error_abort(GEN_EXIT_ERR, "Attributes failed sanity check");
            }
        }

        /* Allocate the protection domain for the HCA */
        proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
        if (!proc->ptag[i]) {
            fprintf(stderr, "Fail to alloc pd number %d\n", i);
            goto err;
        }

        if(rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);
            
            if(!proc->comp_channel[i]) {
                fprintf(stderr, "cannot create completion channel\n");
                goto err_pd;
            }

            fprintf(stderr,"Created comp channel %p\n", proc->comp_channel[i]);

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err_pd;
            }
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err_pd;
            }
        }

	if (proc->has_srq)
	        proc->srq_hndl[i] = create_srq(proc, i);

	if (proc->has_one_sided) {
	    proc->cq_hndl_1sc[i] = ibv_create_cq(proc->nic_context[i],
                            rdma_default_max_cq_size, NULL, NULL, 0);
	    if (!proc->cq_hndl_1sc[i]) {
 		fprintf(stderr, 
                         "cannot allocate CQ for one-sided communication\n");
                goto err_cq;
            }
	}
    }

    /*Port for all mgmt*/
    rdma_default_port = MPIDI_CH3I_RDMA_Process.ports[0][0]; 
    return 0;
    err_cq:
        for (i = 0; i < rdma_num_hcas; i ++) {
            if (proc->cq_hndl[i])
                ibv_destroy_cq(proc->cq_hndl[i]);
        }
        for (i = 0; proc->has_one_sided && i < rdma_num_hcas; i ++) {
             if (proc->cq_hndl_1sc[0])
                 ibv_destroy_cq(proc->cq_hndl_1sc[i]);
        }
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

#undef FUNCNAME
#define FUNCNAME rdma_iba_hca_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank,
    int pg_size)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_init_attr boot_attr;
    struct ibv_qp_attr      qp_attr;
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;

    MPIDI_VC_t	*vc;

    int i, j, k;
    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;
    int ports[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int lids[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int mpi_errno = MPI_SUCCESS;

#ifdef RDMA_CM
  if (!proc->use_rdma_cm)
  {
#endif
    /* step 1: open hca, create ptags  and create cqs */
    for (i = 0; i < rdma_num_hcas; i++) {
        if(ibv_query_device(proc->nic_context[i], &dev_attr)) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Error getting HCA attributes");
        }

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

                    if (check_attrs(&port_attr, &dev_attr)) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**fail", "**fail %s",
				"Attributes failed sanity check");
                    }

                }
            }
            if (k < rdma_num_ports) {
		MPIU_ERR_SETFATALANDJUMP3(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s%d%s", "Not enough ports in active state, needed ",
			rdma_num_ports, " active ports");
            }
        } else {
            if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, &port_attr)
                || (!port_attr.lid )
                || (port_attr.state != IBV_PORT_ACTIVE))
            {
		MPIU_ERR_SETFATALANDJUMP3(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s%d%s", "user specified port ", rdma_default_port,
			": failed to query or not ACTIVE");
            }

            ports[i][0] = rdma_default_port;
            lids[i][0]  = port_attr.lid;

            if (check_attrs(&port_attr, &dev_attr)) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Attributes failed sanity check");
            }

        }

        /* Allocate the protection domain for the HCA */
        proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
        if (!proc->ptag[i]) {
	    MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto err,
		    "**fail", "%s%d", "Failed to alloc pd number ", i);
        }
        
        if(rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);

            if(!proc->comp_channel[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_pd,
			"**fail", "**fail %s",
			"cannot create completion channel");
            }

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);

            if (!proc->cq_hndl[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_pd,
			"**fail", "**fail %s", "cannot create cq");
            }

            if (ibv_req_notify_cq(proc->cq_hndl[i], 0)) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_pd,
			"**fail", "**fail %s",
			"cannot request cq notification");
            }
            
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

            if (!proc->cq_hndl[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_pd,
			"**fail", "**fail %s", "cannot create cq");
            }
        }

	if (proc->has_srq)
	        proc->srq_hndl[i] = create_srq(proc, i);

	if (proc->has_one_sided) {
	    proc->cq_hndl_1sc[i] = ibv_create_cq(proc->nic_context[i],
                            rdma_default_max_cq_size, NULL, NULL, 0);
	    if (!proc->cq_hndl_1sc[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
			"**fail", "**fail %s", "cannot allocate CQ for "
			"one-sided communication");
            }
	}
    }
#ifdef RDMA_CM
  }
#endif /* RDMA_CM */

    rdma_default_port 	    = ports[0][0];
    /* step 2: create qps for all vc */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(cached_pg, i, &vc);

        vc->mrail.num_rails = rdma_num_rails;
        vc->mrail.rails = malloc
            (sizeof *vc->mrail.rails * vc->mrail.num_rails);

        if (!vc->mrail.rails) {
	    MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		    "**fail", "**fail %s", "Failed to allocate resources for "
		    "multirails");
        }

	vc->mrail.srp.credits = malloc
        (sizeof *vc->mrail.srp.credits * vc->mrail.num_rails);
	if (!vc->mrail.srp.credits) {
	    MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		    "**fail", "**fail %s", "Failed to allocate resources for "
		    "credits array");
	}
	memset(vc->mrail.srp.credits, 0,
	       (sizeof *vc->mrail.srp.credits * vc->mrail.num_rails));

        if (i == pg_rank)
            continue;
#ifdef RDMA_CM
	if (proc->use_rdma_cm)
		continue;
#endif
	for (   rail_index = 0; 
        	rail_index < vc->mrail.num_rails;
        	rail_index++) 
	{
            hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
	    port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
	    memset(&attr, 0, sizeof attr);
	    attr.cap.max_send_wr = rdma_default_max_wqe;

            if (proc->has_srq) {
                attr.cap.max_recv_wr = 0;
                attr.srq = proc->srq_hndl[hca_index];
            } else {
                attr.cap.max_recv_wr = rdma_default_max_wqe;
            }

	    attr.cap.max_send_sge = rdma_default_max_sg_list;
	    attr.cap.max_recv_sge = rdma_default_max_sg_list;
	    attr.cap.max_inline_data = rdma_max_inline_size;
	    attr.send_cq = proc->cq_hndl[hca_index];
	    attr.recv_cq = proc->cq_hndl[hca_index];
	    attr.qp_type = IBV_QPT_RC;
	    attr.sq_sig_all = 0;

	    vc->mrail.rails[rail_index].qp_hndl = 
	        ibv_create_qp(proc->ptag[hca_index], &attr);
	    if (!vc->mrail.rails[rail_index].qp_hndl) {
		MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto err_cq,
			"**fail", "%s%d", "Failed to create qp for rank ", i);
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
	    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

	    qp_attr.port_num = ports[hca_index][port_index];
        set_pkey_index(&qp_attr.pkey_index, hca_index,
                qp_attr.port_num);

	    if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
			"**fail", "**fail %s", "Failed to modify QP to INIT");
            }
	}
    }

#ifdef RDMA_CM
    if (proc->use_rdma_cm){
	    ib_init_rdma_cm(proc, pg_rank, pg_size);
	    goto fn_exit;
    }
#endif 

    if (MPIDI_CH3I_RDMA_Process.has_ring_startup && pg_size > 1) {
        DEBUG_PRINT("ENTERING MPDRING CASE\n");  
        proc->boot_cq_hndl =
            ibv_create_cq(proc->nic_context[0],rdma_default_max_cq_size, 
                          NULL, NULL, 0); 
    if (!proc->boot_cq_hndl) {
	MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_pd,
		"**fail", "**fail %s", "cannot create cq");
    }

    /* Create complete Queue and Queue pairs */
    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = rdma_max_inline_size;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;

    proc->boot_qp_hndl[0] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[0]) {
	MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		"**fail", "%s%d", "Fail to create qp for rank ", i);
    }

    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = rdma_max_inline_size;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;

    proc->boot_qp_hndl[1] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[1]) {
	MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		"**fail", "%s%d", "Fail to create qp for rank ", i);
    }
    DEBUG_PRINT("Created boot qp %x, %x\n",
            proc->boot_qp_hndl[0]->qp_num, proc->boot_qp_hndl[1]->qp_num);
    }

    if (proc->has_one_sided) {
	/* This part is for built up QPs and CQ for one-sided communication */
	qp_attr.qp_state        = IBV_QPS_INIT;
	qp_attr.qp_access_flags =   IBV_ACCESS_LOCAL_WRITE |
	    			    IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

	qp_attr.port_num        = rdma_default_port;

	for (i = 0; i < pg_size; i++) {
	    memset(&attr, 0, sizeof attr);
	    attr.cap.max_send_wr  = rdma_default_max_wqe;
	    attr.cap.max_recv_wr  = rdma_default_max_wqe;
	    attr.cap.max_send_sge = rdma_default_max_sg_list;
	    attr.cap.max_recv_sge = rdma_default_max_sg_list;
	    attr.cap.max_inline_data = sizeof(long long);
	    attr.qp_type 	  = IBV_QPT_RC;
	    attr.sq_sig_all 	  = 0;
	
	    MPIDI_PG_Get_vc(cached_pg, i, &vc);

	    for (rail_index = 0;
	         rail_index < vc->mrail.num_rails;
	         rail_index++)
	    {
	        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
	        port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
	                     rdma_num_ports))) % rdma_num_ports;


	        attr.send_cq = proc->cq_hndl_1sc[hca_index];
	        attr.recv_cq = proc->cq_hndl_1sc[hca_index];

	        vc->mrail.rails[rail_index].qp_hndl_1sc = 
                ibv_create_qp(proc->ptag[hca_index], &attr);
	        if (!vc->mrail.rails[rail_index].qp_hndl_1sc) {
		    MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER,
			    goto err_cq, "**fail", "%s%d",
			    "Failed to create one sided qp for rank ", i);
	        }

	        if (i == pg_rank) {
	            dst_qp = vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
	        }
	        rdma_iba_addr_table.qp_num_onesided[i][rail_index] =
	            vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
	        DEBUG_PRINT("Created onesdied qp %d, num %X, qp_handle is %x\n",
	            i, rdma_iba_addr_table.qp_num_onesided[i][rail_index],
                vc->mrail.rails[rail_index].qp_hndl_1sc);

            set_pkey_index(&qp_attr.pkey_index, hca_index, rdma_default_port);
	
	        if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl_1sc, &qp_attr,
	                	  IBV_QP_STATE              |
	                	  IBV_QP_PKEY_INDEX         |
	                	  IBV_QP_PORT               |
	                	  IBV_QP_ACCESS_FLAGS)) {
		    MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER,
			    goto err_cq, "**fail", "**fail %s",
			    "Failed to modify One sided QP to INIT");
	        }
	    }
	}
    }

    DEBUG_PRINT("Return from init hca\n");

fn_exit:
    return mpi_errno;

err_cq:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->cq_hndl[i])
            ibv_destroy_cq(proc->cq_hndl[i]);
    }

    for (i = 0; proc->has_one_sided && i < rdma_num_hcas; i ++) {
         if (proc->cq_hndl_1sc[0])
             ibv_destroy_cq(proc->cq_hndl_1sc[i]);
    }

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

fn_fail:
    goto fn_exit;
}

/* Allocate memory and handlers */
/*
 * TODO add error handling
 */
int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         int pg_rank, int pg_size)
{
    int ret = 0, i = 0, j;
    MPIDI_VC_t * vc;
    /* FIrst allocate space for RDMA_FAST_PATH for every connection */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(cached_pg, i, &vc);
	if (proc->has_one_sided) 
	    for (j = 0; j < rdma_num_rails; j ++)
		vc->mrail.rails[j].postsend_times_1sc = 0;
	vc->mrail.rfp.phead_RDMA_send = 0;
	vc->mrail.rfp.ptail_RDMA_send = 0;
	vc->mrail.rfp.p_RDMA_recv = 0;
	vc->mrail.rfp.p_RDMA_recv_tail = 0;
    }

    MPIDI_CH3I_RDMA_Process.polling_group_size = 0;
    if (rdma_polling_set_limit > 0) 
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t **)
                        malloc (rdma_polling_set_limit * sizeof(MPIDI_VC_t *));
    else 
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t **)
                        malloc(pg_size * sizeof(MPIDI_VC_t *));
    
    if (! MPIDI_CH3I_RDMA_Process.polling_set) {
	fprintf(stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
		"unable to allocate space for polling set\n");
	
	goto err_reg;
    }

    /* We need now allocate vbufs for send/recv path */
    ret = allocate_vbufs(MPIDI_CH3I_RDMA_Process.ptag, rdma_vbuf_pool_size);
    if (ret) {
        goto err_reg;
    }
   
    /* Post the buffers for the SRQ */

    if (MPIDI_CH3I_RDMA_Process.has_srq) {
        int hca_num = 0;

        pthread_spin_init(
                &MPIDI_CH3I_RDMA_Process.srq_post_spin_lock, 0);

        pthread_spin_lock(
                &MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

        for(hca_num = 0; hca_num < rdma_num_hcas; hca_num++) { 

            pthread_mutex_init(
                    &MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[hca_num], 0);
            pthread_cond_init(&MPIDI_CH3I_RDMA_Process.srq_post_cond[hca_num], 0);
            MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[hca_num] = 0;

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
                pthread_create(
                        &MPIDI_CH3I_RDMA_Process.async_thread[hca_num], 
                        NULL, async_thread, 
                        (void *) 
                        MPIDI_CH3I_RDMA_Process.nic_context[hca_num]);
            }
        }

        pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);
    }
    
    return MPI_SUCCESS;
err_reg:
    return ret;
}

static inline int round_left(int current, int size)
{
    if (current == 0) {
        return size-1;
    }
    else 
        return current-1;
}

static inline int is_A_on_left_of_B(int a, int b, int rank, int size)
{
    int dist_a = (rank - a + size)%size;
    int dist_b = (rank - b + size)%size;
    return dist_a > dist_b;
}
    
/*
 * TODO add error handling
 */
void IB_ring_based_alltoall(void *sbuf, int data_size, 
        int pg_rank, void *rbuf, int pg_size, 
        struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    struct ibv_mr * addr_hndl;
    struct ibv_qp_init_attr boot_attr;
    struct init_addr_inf neighbor_addr[2];
    int i, bootstrap_len, qp_len;  

    char ring_qp_out[64];
    char ring_qp_in[128];
    char tmp_str[9];
    char *tmp_char_ptr;

    tmp_str[8] = '\0';

    /*create cq and qp*/
    proc->boot_cq_hndl = ibv_create_cq(proc->nic_context[0],rdma_default_max_cq_size,
                          NULL, NULL, 0);
    
    if (!proc->boot_cq_hndl) {
        ibv_error_abort(GEN_EXIT_ERR,"cannot create cq\n");
    }

    /*create boot_qp*/
    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = rdma_max_inline_size;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;

    proc->boot_qp_hndl[0] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[0]) {
        ibv_error_abort(GEN_EXIT_ERR, "[Init] Fail to create qp\n");
    }

    memset(&boot_attr, 0, sizeof boot_attr);
    boot_attr.cap.max_send_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_recv_wr   = rdma_default_max_wqe;
    boot_attr.cap.max_send_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_recv_sge  = rdma_default_max_sg_list;
    boot_attr.cap.max_inline_data = rdma_max_inline_size;
    boot_attr.qp_type = IBV_QPT_RC;
    boot_attr.sq_sig_all = 0;

    boot_attr.send_cq = proc->boot_cq_hndl;
    boot_attr.recv_cq = proc->boot_cq_hndl;

    proc->boot_qp_hndl[1] = ibv_create_qp(proc->ptag[0], &boot_attr);
    if (!proc->boot_qp_hndl[1]) {
        ibv_error_abort(GEN_EXIT_ERR, "[Init] Fail to create qp\n");
    }
    
    DEBUG_PRINT("Created boot qp %x, %x\n",
            proc->boot_qp_hndl[0]->qp_num, proc->boot_qp_hndl[1]->qp_num);

    DEBUG_PRINT("Before formatting ring_qp_out\n");
    sprintf(ring_qp_out, "%08d%08d%08d",
            get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], 
                rdma_default_port),
            proc->boot_qp_hndl[0]->qp_num,
            proc->boot_qp_hndl[1]->qp_num
           );

    DEBUG_PRINT("After setting LID: %d, qp0: %d, qp1: %d\n", 
            get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], 
                rdma_default_port),
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

    addr_hndl = ibv_reg_mr(proc->ptag[0],
            rbuf, data_size*pg_size,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

    if(addr_hndl == NULL) {
        ibv_error_abort(GEN_EXIT_ERR,"ibv_reg_mr failed for addr_hndl\n");
    }

    DEBUG_PRINT("val of addr_pool is: %d, handle: %d\n", 
            rbuf, addr_hndl->handle);

    proc->boot_mem_hndl = addr_hndl;
    proc->boot_mem      = rbuf;
 
    /* Setup the necessary qps for the bootstrap ring */
    MPI_Ring_Setup(neighbor_addr, proc);
    
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
                    assert(recv_comp_index == rc.wr_id);
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
}

/*
 * TODO add error handling 
 */
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

    DEBUG_PRINT("After setting LID: %d, qp0: %d, qp1: %d\n", 
            get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], 
                rdma_default_port),
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
    struct ibv_qp_attr      qp_attr;

    uint32_t    qp_attr_mask = 0;
    int         i;
    int         ret;

    qp_attr.qp_state        = IBV_QPS_INIT;
    set_pkey_index(&qp_attr.pkey_index, 0, rdma_default_port);
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | 
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
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
    int i, ne, index_to_send, rail_index;
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
		if (proc->has_one_sided)
	            send_packet->val[i].osc_qp_num = -1;
            } else {
                MPIDI_PG_Get_vc(cached_pg, i, &vc);

                send_packet->lid     = vc->mrail.rails[rail_index].lid;
                send_packet->val[i].sr_qp_num = 
                    vc->mrail.rails[rail_index].qp_hndl->qp_num;
		if (proc->has_one_sided)
		    send_packet->val[i].osc_qp_num =
                        vc->mrail.rails[rail_index].qp_hndl_1sc->qp_num;
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
                      if (SMP_INIT) {
                        MPIDI_PG_Get_vc(cached_pg, recv_packet->rank, &vc);
                        vc->smp.hostid = recv_packet->host_id;
                      }
#endif
                        
                        rdma_iba_addr_table.qp_num_rdma[recv_packet->rank][rail_index] =
                            recv_packet->val[pg_rank].sr_qp_num;

			if (proc->has_one_sided)
			    rdma_iba_addr_table.qp_num_onesided[recv_packet->rank][rail_index] =
                                recv_packet->val[pg_rank].osc_qp_num;

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
                ibv_va_error_abort(GEN_EXIT_ERR,"Error code %d in polled desc!\n",
			        rc.status);
            }
            last_send_comp = rc.wr_id;
        }
    }

}



int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            int pg_rank, int pg_size)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 i, j;
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

        if (proc->has_one_sided) {
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
                if (i == pg_rank)
                    qp_attr.dest_qp_num = dst_qp;
                else
                    qp_attr.dest_qp_num =
                        rdma_iba_addr_table.qp_num_onesided[i][rail_index];

                qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index];

                qp_attr_mask    |=  IBV_QP_DEST_QPN;
        
                if(MPIDI_CH3I_RDMA_Process.has_hsam) {
                    qp_attr.ah_attr.src_path_bits = rail_index %
                        power_two(MPIDI_CH3I_RDMA_Process.lmc);
                    qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index]
                        + rail_index % power_two(MPIDI_CH3I_RDMA_Process.lmc);
                }

                if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl_1sc,
                            &qp_attr, qp_attr_mask)) {
                    fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTR\n", 
                            __FILE__, __LINE__);
                    return 1;
                }
            }
        }

	for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
        qp_attr.dest_qp_num =
            rdma_iba_addr_table.qp_num_rdma[i][rail_index];
        qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index];
        qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;

        /* If HSAM is enabled, include the source path bits and change
         * the destination LID accordingly */

        /* Both source and destination should have the same value of the
         * bits */

        if(MPIDI_CH3I_RDMA_Process.has_hsam) {
            qp_attr.ah_attr.src_path_bits = rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
            qp_attr.ah_attr.dlid = rdma_iba_addr_table.lid[i][rail_index]
                + rail_index % power_two(MPIDI_CH3I_RDMA_Process.lmc);
        }
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

        for(j = 0; j < rdma_num_rails - 1; j++) {
            vc->mrail.rails[j].s_weight =
                DYNAMIC_TOTAL_WEIGHT / rdma_num_rails;
        }
        
        vc->mrail.rails[rdma_num_rails - 1].s_weight =
            DYNAMIC_TOTAL_WEIGHT -
            (DYNAMIC_TOTAL_WEIGHT / rdma_num_rails) * 
            (rdma_num_rails - 1);

        if (proc->has_one_sided) {
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
                if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl_1sc,
                            &qp_attr, qp_attr_mask)) {
                    fprintf(stderr, "[%s:%d] Could not modify one sided qp to RTS\n",
                            __FILE__, __LINE__);
                    return 1;
                }
                if(MPIDI_CH3I_RDMA_Process.has_apm) {
                    reload_alternate_path(vc->mrail.rails[rail_index].qp_hndl_1sc);
                }
            } 
        }

        if (i == pg_rank)
            continue;
        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            if (ibv_modify_qp(  vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                        qp_attr_mask)) {
                fprintf(stderr, "[%s:%d] Could not modify rdma qp to RTS\n",
                        __FILE__, __LINE__);
                return 1;
            }
            
            if(MPIDI_CH3I_RDMA_Process.has_apm) {
                reload_alternate_path(vc->mrail.rails[rail_index].qp_hndl);
            }
        }
    }

    DEBUG_PRINT("Done enabling connections\n");
    return 0;
}

void MRAILI_Init_vc(MPIDI_VC_t * vc, int pg_rank)
{
    int pg_size;
    int i;

    PMI_Get_size(&pg_size);

    /* Now we will need to */
    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_wqe - 20;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
    }

    vc->mrail.next_packet_expected  = 0;
    vc->mrail.next_packet_tosend    = 0;

    vc->mrail.outstanding_eager_vbufs = 0;
    vc->mrail.coalesce_vbuf = NULL;

    vc->mrail.rfp.rdma_credit = 0;
#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

    vc->mrail.cmanager.num_channels         = vc->mrail.num_rails;
    vc->mrail.cmanager.num_local_pollings   = 0;

    if (pg_size < rdma_eager_limit) 
	vc->mrail.rfp.eager_start_cnt = rdma_polling_set_threshold + 1;
    else
	vc->mrail.rfp.eager_start_cnt = 0;

    vc->mrail.rfp.in_polling_set = 0;

    /* extra one channel for later increase the adaptive rdma */
    vc->mrail.cmanager.msg_channels = malloc
        (sizeof *vc->mrail.cmanager.msg_channels 
         * (vc->mrail.cmanager.num_channels + 1));
    if (!vc->mrail.cmanager.msg_channels) {
        ibv_error_abort(GEN_EXIT_ERR, "No resource for msg channels\n");
    }
    memset(vc->mrail.cmanager.msg_channels, 0, 
            sizeof *vc->mrail.cmanager.msg_channels
            * (vc->mrail.cmanager.num_channels + 1));

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
        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            for (k = 0; k < rdma_initial_prepost_depth; k++) {
                PREPOST_VBUF_RECV(vc, i);
            }
        }
        vc->mrail.srp.credits[i].remote_credit     = rdma_initial_credits;
        vc->mrail.srp.credits[i].remote_cc         = rdma_initial_credits;
        vc->mrail.srp.credits[i].local_credit      = 0;
        vc->mrail.srp.credits[i].preposts          = rdma_initial_prepost_depth;

        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            vc->mrail.srp.credits[i].initialized	   =
                (rdma_prepost_depth == rdma_initial_prepost_depth);
        } else {
            vc->mrail.srp.credits[i].initialized = 1; 
            vc->mrail.srp.credits[i].pending_r3_sends = 0;
        }

        vc->mrail.srp.credits[i].backlog.len       = 0;
        vc->mrail.srp.credits[i].backlog.vbuf_head = NULL;
        vc->mrail.srp.credits[i].backlog.vbuf_tail = NULL;

        vc->mrail.srp.credits[i].rendezvous_packets_expected = 0;
    }

    int j;
    
    for(j = 0; j < rdma_num_rails - 1; j++) {
        vc->mrail.rails[j].s_weight =
            DYNAMIC_TOTAL_WEIGHT / rdma_num_rails;
    }
    
    vc->mrail.rails[rdma_num_rails - 1].s_weight =
        DYNAMIC_TOTAL_WEIGHT -
        (DYNAMIC_TOTAL_WEIGHT / rdma_num_rails) * (rdma_num_rails - 1);

}

/*function to create qps for the connection and move them to INIT state*/
int cm_qp_create(MPIDI_VC_t *vc)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_attr      qp_attr;

    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;
    int pg_rank, pg_size;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    qp_attr.qp_state  = IBV_QPS_INIT;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;


    vc->mrail.num_rails = rdma_num_rails;
    vc->mrail.rails = malloc
        (sizeof *vc->mrail.rails * vc->mrail.num_rails);

    if (!vc->mrail.rails) {
        ibv_error_abort(GEN_EXIT_ERR, 
                "Fail to allocate resources for multirails\n");
    }

    vc->mrail.srp.credits = malloc(sizeof *vc->mrail.srp.credits * 
            vc->mrail.num_rails);
    if (!vc->mrail.srp.credits) {
        ibv_error_abort(GEN_EXIT_ERR, 
                "Fail to allocate resources for credits array\n");
    }

    for (rail_index = 0; 
    	    rail_index < vc->mrail.num_rails;
    	    rail_index++) 
    {
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
        port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
        memset(&attr, 0, sizeof attr);
        attr.cap.max_send_wr = rdma_default_max_wqe;

        if (MPIDI_CH3I_RDMA_Process.has_srq) {
            attr.cap.max_recv_wr = 0;
            attr.srq = MPIDI_CH3I_RDMA_Process.srq_hndl[hca_index];
        } else {
            attr.cap.max_recv_wr = rdma_default_max_wqe;
        }

        attr.cap.max_send_sge = rdma_default_max_sg_list;
        attr.cap.max_recv_sge = rdma_default_max_sg_list;
        attr.cap.max_inline_data = rdma_max_inline_size;
        attr.send_cq = MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
        attr.recv_cq = MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
        attr.qp_type = IBV_QPT_RC;
        attr.sq_sig_all = 0;

        vc->mrail.rails[rail_index].qp_hndl = 
            ibv_create_qp(MPIDI_CH3I_RDMA_Process.ptag[hca_index], &attr);
        if (!vc->mrail.rails[rail_index].qp_hndl) {
            ibv_error_abort(GEN_EXIT_ERR, "Failed to create QP\n");
        }
        vc->mrail.rails[rail_index].nic_context = 
            MPIDI_CH3I_RDMA_Process.nic_context[hca_index];
        vc->mrail.rails[rail_index].hca_index   = hca_index;
        vc->mrail.rails[rail_index].port	= 
            MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        vc->mrail.rails[rail_index].lid     = 
            MPIDI_CH3I_RDMA_Process.lids[hca_index][port_index];
        vc->mrail.rails[rail_index].cq_hndl	= 
            MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];

        rdma_iba_addr_table.lid[pg_rank][rail_index] = 
            MPIDI_CH3I_RDMA_Process.lids[hca_index][port_index];
        rdma_iba_addr_table.qp_num_rdma[pg_rank][rail_index] =
            vc->mrail.rails[rail_index].qp_hndl->qp_num;

        qp_attr.qp_state        = IBV_QPS_INIT;
        qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

        qp_attr.port_num = MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        set_pkey_index(&qp_attr.pkey_index, hca_index, qp_attr.port_num);

        if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
                ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to INIT\n");
            }
    }

    return 0;
}

/*function to move qps to rtr and prepost buffers*/
int cm_qp_move_to_rtr(MPIDI_VC_t *vc, uint16_t *lids, uint32_t *qpns)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 rail_index;

    int pg_rank, pg_size;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
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
        qp_attr.dest_qp_num = qpns[rail_index];
        qp_attr.ah_attr.dlid = lids[rail_index];
        qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;
    
        if (MPIDI_CH3I_RDMA_Process.has_hsam) {
            qp_attr.ah_attr.src_path_bits = rdma_default_src_path_bits
                + rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
            qp_attr.ah_attr.dlid = lids[rail_index] + rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
        } 
    
        qp_attr_mask        |=  IBV_QP_STATE; 
        qp_attr_mask        |=  IBV_QP_PATH_MTU;
        qp_attr_mask        |=  IBV_QP_RQ_PSN;
        qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
        qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
        qp_attr_mask        |=  IBV_QP_AV;
        qp_attr_mask        |=  IBV_QP_DEST_QPN;

        DEBUG_PRINT("!!!Modify qp %d with qpnum %08x, dlid %x\n", rail_index,
            qp_attr.dest_qp_num, qp_attr.ah_attr.dlid);
        if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl,
                           &qp_attr, qp_attr_mask)) {
            ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to RTR\n");
        }
    }

    return 0;
}

/*function to move qps to rts and mark the connection available*/
int cm_qp_move_to_rts(MPIDI_VC_t *vc)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 rail_index;

    int pg_rank, pg_size;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);
        
    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
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
        if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    qp_attr_mask)) {
            ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to RTS\n");
        }
        
        if(MPIDI_CH3I_RDMA_Process.has_apm) {
            reload_alternate_path(vc->mrail.rails[rail_index].qp_hndl);
        }

    }
    return 0;
}

uint16_t get_pkey_index(uint16_t pkey, int hca_num, int port_num)
{
    uint16_t i;
    static const uint16_t bad_pkey_idx = -1;
    struct ibv_device_attr dev_attr;

    if(ibv_query_device(
                MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                &dev_attr)) {

        ibv_error_abort(GEN_EXIT_ERR,
                "Error getting HCA attributes\n");
    }   

    for (i = 0; i < dev_attr.max_pkeys ; ++i) {
        uint16_t curr_pkey;
        ibv_query_pkey(MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                (uint8_t)port_num, (int)i ,&curr_pkey);
        if (pkey == ntohs(curr_pkey)) {
            return i;
        }
    }
    return bad_pkey_idx;
}

void set_pkey_index(uint16_t * pkey_index, int hca_num, int port_num)
{
    *pkey_index = (rdma_default_pkey == 
            RDMA_DEFAULT_PKEY ? rdma_default_pkey_ix :  
            get_pkey_index(rdma_default_pkey, hca_num, port_num));
    if (pkey_index < 0 ) {
        ibv_error_abort(GEN_EXIT_ERR,
                "Can't find PKEY INDEX according to given PKEY\n");
    }
}

#ifdef CKPT
void MRAILI_Init_vc_network(MPIDI_VC_t * vc)
{
    /*Reinitialize VC after channel reactivation
     *      * No change in cmanager, no change in rndv, not support rdma
     *      fast path*/
    int i;

#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_wqe - 20;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
    }
    for (i = 0; i < vc->mrail.num_rails; i++) {
        int k;
        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            for (k = 0; k < rdma_initial_prepost_depth; k++) {
                PREPOST_VBUF_RECV(vc, i);
            }
        }
        vc->mrail.srp.credits[i].remote_credit     = rdma_initial_credits;
        vc->mrail.srp.credits[i].remote_cc         = rdma_initial_credits;
        vc->mrail.srp.credits[i].local_credit      = 0;
        vc->mrail.srp.credits[i].preposts          = rdma_initial_prepost_depth;

        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            vc->mrail.srp.credits[i].initialized       =
                (rdma_prepost_depth == rdma_initial_prepost_depth);
        } else {
            vc->mrail.srp.credits[i].initialized = 1;
            vc->mrail.srp.credits[i].pending_r3_sends = 0;
        }
        vc->mrail.srp.credits[i].backlog.len       = 0;
        vc->mrail.srp.credits[i].backlog.vbuf_head = NULL;
        vc->mrail.srp.credits[i].backlog.vbuf_tail = NULL;
        vc->mrail.srp.credits[i].rendezvous_packets_expected = 0;
    }
}

#endif

/* vi:set sw=4 tw=80: */
