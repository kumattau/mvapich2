/* Copyright (c) 2003-2008, The Ohio State University. All rights
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
#include <netdb.h>
#include <string.h>
#include "vbuf.h"
#include "rdma_impl.h"
#include "pmi.h"
#include "ibv_param.h"
#include "rdma_cm.h"
#include "mpiutil.h"

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

#define MPD_WINDOW 10
#define ADDR_PKT_SIZE (sizeof(struct addr_packet) + ((pg_size - 1) * sizeof(struct host_addr_inf)))
#define ADDR_INDEX(_p, _i) ((struct addr_packet *)(_p + (_i * ADDR_PKT_SIZE)))

int power_two(int x)
{
    int pow = 1;

    while (x) {
        pow = pow * 2;
        x--;
    }

    return pow;
}

struct process_init_info *alloc_process_init_info(int pg_size, int rails)
{
    struct process_init_info *info;
    int i;

    info = MPIU_Malloc(sizeof *info);
    if (!info) {
        return NULL;
    }

    info->lid = (uint16_t **) MPIU_Malloc(pg_size * sizeof(uint16_t *));
    info->hostid = (int **) MPIU_Malloc(pg_size * sizeof(int *));
    info->qp_num_rdma = (uint32_t **) MPIU_Malloc(pg_size * sizeof(uint32_t *));
    info->qp_num_onesided = (uint32_t **) MPIU_Malloc(pg_size * sizeof(uint32_t *));
    info->hca_type = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));
    if (!info->lid
        || !info->hostid 
        || !info->qp_num_rdma
        || !info->qp_num_onesided 
        || !info->hca_type) {
        return NULL;
    }

    for (i = 0; i < pg_size; ++i)
    {
        info->qp_num_rdma[i] = (uint32_t *) MPIU_Malloc(rails * sizeof(uint32_t));
        info->lid[i] = (uint16_t *) MPIU_Malloc(rails * sizeof(uint16_t));
        info->hostid[i] = (int *) MPIU_Malloc(rails * sizeof(int));
        info->qp_num_onesided[i] = (uint32_t *) MPIU_Malloc(rails * sizeof(uint32_t));

        if (!info->lid[i]
                || !info->hostid[i]
                || !info->qp_num_rdma[i]
                || !info->qp_num_onesided[i]) {
             return NULL;
        }
    }

    return info;
}

void free_process_init_info(struct process_init_info *info, int pg_size)
{
    int i;

    if (!info)
        return;

    for (i = 0; i < pg_size; ++ i) {
        MPIU_Free(info->qp_num_rdma[i]);
        MPIU_Free(info->lid[i]);
        MPIU_Free(info->hostid[i]);
        MPIU_Free(info->qp_num_onesided[i]);
    }

    MPIU_Free(info->lid);
    MPIU_Free(info->hostid);
    MPIU_Free(info->qp_num_rdma);
    MPIU_Free(info->qp_num_onesided);
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

        if(dev_attr->max_qp_wr < rdma_default_max_send_wqe) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SEND_WQE is %d, set to %d\n",
                    dev_attr->max_qp_wr, (int) rdma_default_max_send_wqe);
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

            ib_dev = dev_list[i];

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

        proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
        if (!proc->ptag[i]) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, 
                    "**fail", "%s%d", "Failed to alloc pd number ", i);
        }
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME rdma_iba_hca_init_noqp
#undef FCNAME
#define FCNAME MPIDI(FUNCNAME)
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

        if(rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);
            
            if(!proc->comp_channel[i]) {
                fprintf(stderr, "cannot create completion channel\n");
                goto err;
            }

            fprintf(stderr,"Created comp channel %p\n", proc->comp_channel[i]);

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err;
            }
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err;
            }
        }

	if (proc->has_srq) {
	        proc->srq_hndl[i] = create_srq(proc, i);
            if((proc->srq_hndl[i]) == NULL)
                goto err_cq;
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

    err:
        for (i = 0; i < rdma_num_hcas; i ++) {
            if (proc->ptag[i])
                ibv_dealloc_pd(proc->ptag[i]);
        }

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
                      int pg_size, struct process_init_info *info)
{
    struct ibv_qp_init_attr attr;
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
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**activeports", "**activeports %d", rdma_num_ports);
            }
        } else {
            if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, &port_attr)
                || (!port_attr.lid )
                || (port_attr.state != IBV_PORT_ACTIVE))
            {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**portquery", "**portquery %d", rdma_default_port);
            }

            ports[i][0] = rdma_default_port;
            lids[i][0]  = port_attr.lid;

            if (check_attrs(&port_attr, &dev_attr)) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Attributes failed sanity check");
            }

        }
        
        if(rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);

            if(!proc->comp_channel[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			"**fail", "**fail %s",
			"cannot create completion channel");
            }

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);

            if (!proc->cq_hndl[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			"**fail", "**fail %s", "cannot create cq");
            }

            if (ibv_req_notify_cq(proc->cq_hndl[i], 0)) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			"**fail", "**fail %s",
			"cannot request cq notification");
            }
            
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

            if (!proc->cq_hndl[i]) {
		MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			"**fail", "**fail %s", "cannot create cq");
            }
        }

	if (proc->has_srq)
	        proc->srq_hndl[i] = create_srq(proc, i);

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
        MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

        vc->mrail.num_rails = rdma_num_rails;
        vc->mrail.rails = MPIU_Malloc
            (sizeof *vc->mrail.rails * vc->mrail.num_rails);

        if (!vc->mrail.rails) {
	    MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		    "**fail", "**fail %s", "Failed to allocate resources for "
		    "multirails");
        }

	vc->mrail.srp.credits = MPIU_Malloc
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
	    attr.cap.max_send_wr = rdma_default_max_send_wqe;

            if (proc->has_srq) {
                attr.cap.max_recv_wr = 0;
                attr.srq = proc->srq_hndl[hca_index];
            } else {
                attr.cap.max_recv_wr = rdma_default_max_recv_wqe;
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

            if (info) {
                info->lid[i][rail_index] = lids[hca_index][port_index];
                info->hca_type[i] = proc->hca_type;
	        info->qp_num_rdma[i][rail_index] =
        	        vc->mrail.rails[rail_index].qp_hndl->qp_num;
                DEBUG_PRINT("Setting hca type %d\n", info->hca_type[i]);
            }

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
    if (proc->use_rdma_cm)
    {
        if ((mpi_errno = ib_init_rdma_cm(proc, pg_rank, pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }
#endif 

    DEBUG_PRINT("Return from init hca\n");

fn_exit:
    return mpi_errno;

err_cq:
    for (i = 0; i < rdma_num_hcas; ++i) {
        if (proc->cq_hndl[i])
            ibv_destroy_cq(proc->cq_hndl[i]);
    }

err:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->ptag[i])
            ibv_dealloc_pd(proc->ptag[i]);
    }

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
    int ret = 0;
    int i = 0;
    MPIDI_VC_t * vc;

    /* First allocate space for RDMA_FAST_PATH for every connection */
    for (; i < pg_size; ++i)
    {
        MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

	vc->mrail.rfp.phead_RDMA_send = 0;
	vc->mrail.rfp.ptail_RDMA_send = 0;
	vc->mrail.rfp.p_RDMA_recv = 0;
	vc->mrail.rfp.p_RDMA_recv_tail = 0;
    }

    MPIDI_CH3I_RDMA_Process.polling_group_size = 0;

    if (rdma_polling_set_limit > 0)
    {
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t**) MPIU_Malloc(rdma_polling_set_limit * sizeof(MPIDI_VC_t*));
    }
    else
    {
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t**) MPIU_Malloc(pg_size * sizeof(MPIDI_VC_t*));
    }
    
    if (!MPIDI_CH3I_RDMA_Process.polling_set)
    {
	fprintf(
            stderr,
            "[%s:%d]: %s\n",
            __FILE__,
            __LINE__,
            "unable to allocate space for polling set\n");
        return 0;
    }

    /* We need to allocate vbufs for send/recv path */
    if ((ret = allocate_vbufs(MPIDI_CH3I_RDMA_Process.ptag, rdma_vbuf_pool_size)))
    {
        return ret;
    }
   
    /* Post the buffers for the SRQ */
    if (MPIDI_CH3I_RDMA_Process.has_srq)
    {
        int hca_num = 0;

        pthread_spin_init(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock, 0);
        pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

        for (; hca_num < rdma_num_hcas; ++hca_num)
        { 
            pthread_mutex_init(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[hca_num], 0);
            pthread_cond_init(&MPIDI_CH3I_RDMA_Process.srq_post_cond[hca_num], 0);
            MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[hca_num] = 0;
            MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] = viadev_post_srq_buffers(viadev_srq_size, hca_num);

            {
                struct ibv_srq_attr srq_attr;
                srq_attr.max_wr = viadev_srq_size;
                srq_attr.max_sge = 1;
                srq_attr.srq_limit = viadev_srq_limit;

                if (ibv_modify_srq(
                    MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                    &srq_attr,
                    IBV_SRQ_LIMIT))
                {
                    ibv_error_abort(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
                }

                /* Start the async thread which watches for SRQ limit events */
                pthread_create(
                    &MPIDI_CH3I_RDMA_Process.async_thread[hca_num], 
                    NULL,
                    (void *) async_thread, 
                    (void *) MPIDI_CH3I_RDMA_Process.nic_context[hca_num]);
            }
        }

        pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);
    }
    
    return MPI_SUCCESS;
}
    
/*
 * TODO add error handling 
 */

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            int pg_rank, int pg_size, 
                            struct process_init_info *info)
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

        MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

	for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
        qp_attr.dest_qp_num = info->qp_num_rdma[i][rail_index];
        qp_attr.ah_attr.dlid = info->lid[i][rail_index];
        qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;

        /* If HSAM is enabled, include the source path bits and change
         * the destination LID accordingly */

        /* Both source and destination should have the same value of the
         * bits */

        if(MPIDI_CH3I_RDMA_Process.has_hsam) {
            qp_attr.ah_attr.src_path_bits = rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
            qp_attr.ah_attr.dlid = info->lid[i][rail_index]
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

        MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

        for(j = 0; j < rdma_num_rails - 1; j++) {
            vc->mrail.rails[j].s_weight =
                DYNAMIC_TOTAL_WEIGHT / rdma_num_rails;
        }
        
        vc->mrail.rails[rdma_num_rails - 1].s_weight =
            DYNAMIC_TOTAL_WEIGHT -
            (DYNAMIC_TOTAL_WEIGHT / rdma_num_rails) * 
            (rdma_num_rails - 1);

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
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_send_wqe;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
        vc->mrail.rails[i].ext_sendq_size     = 0;
    }

    vc->mrail.next_packet_expected  = 0;
    vc->mrail.next_packet_tosend    = 0;

    vc->mrail.outstanding_eager_vbufs = 0;
    vc->mrail.coalesce_vbuf = NULL;

    vc->mrail.rfp.rdma_credit = 0;
#ifdef USE_HEADER_CACHING
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
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
    vc->mrail.cmanager.msg_channels = MPIU_Malloc
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

    if (MPIDI_CH3I_RDMA_Process.has_one_sided)
      {
	for (j = 0; j < rdma_num_rails; ++j)
	  {
	    vc->mrail.rails[j].postsend_times_1sc = 0;
	  }
      }
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
    vc->mrail.rails = MPIU_Malloc
        (sizeof *vc->mrail.rails * vc->mrail.num_rails);

    if (!vc->mrail.rails) {
        ibv_error_abort(GEN_EXIT_ERR, 
                "Fail to allocate resources for multirails\n");
    }

    vc->mrail.srp.credits = MPIU_Malloc(sizeof *vc->mrail.srp.credits * 
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
        attr.cap.max_send_wr = rdma_default_max_send_wqe;

        if (MPIDI_CH3I_RDMA_Process.has_srq) {
            attr.cap.max_recv_wr = 0;
            attr.srq = MPIDI_CH3I_RDMA_Process.srq_hndl[hca_index];
        } else {
            attr.cap.max_recv_wr = rdma_default_max_recv_wqe;
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

int get_pkey_index(uint16_t pkey, int hca_num, int port_num, uint16_t* index)
{
    uint16_t i = 0;
    struct ibv_device_attr dev_attr;

    if(ibv_query_device(
                MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                &dev_attr)) {

        ibv_error_abort(GEN_EXIT_ERR,
                "Error getting HCA attributes\n");
    }

    for (; i < dev_attr.max_pkeys ; ++i) {
        uint16_t curr_pkey;
        ibv_query_pkey(MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                (uint8_t)port_num, (int)i ,&curr_pkey);
        if (pkey == ntohs(curr_pkey)) {
            *index = i;
            return 1;
        }
    }

    return 0;
}

void set_pkey_index(uint16_t * pkey_index, int hca_num, int port_num)
{
    if (rdma_default_pkey == RDMA_DEFAULT_PKEY)
    {
        *pkey_index = rdma_default_pkey_ix;
    }
    else if (!get_pkey_index(
        rdma_default_pkey,
        hca_num,
        port_num,
        pkey_index)
    )
    {
        ibv_error_abort(
            GEN_EXIT_ERR,
            "Can't find PKEY INDEX according to given PKEY\n"
        );
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
    vc->mrail.rfp.cached_incoming = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_recv_wqe;
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

/* vi:set sw=4 */
