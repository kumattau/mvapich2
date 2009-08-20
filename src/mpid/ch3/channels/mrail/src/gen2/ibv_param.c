/* Copyright (c) 2003-2009, The Ohio State University. All rights
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
#include <stdlib.h>
#include <stdio.h>
#include <infiniband/verbs.h>
#include <infiniband/umad.h>
#include "ibv_param.h"
#include "vbuf.h"
#include "rdma_impl.h"

/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */


int           rdma_num_hcas   = 1;
int           rdma_num_ports  = 1;
int           rdma_num_qp_per_port = 1;
int           rdma_num_rails;
int           rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
unsigned long rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int           rdma_default_port = RDMA_DEFAULT_PORT;
unsigned long rdma_default_max_send_wqe = RDMA_DEFAULT_MAX_SEND_WQE;
unsigned long rdma_default_max_recv_wqe = RDMA_DEFAULT_MAX_RECV_WQE;
uint32_t      rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
uint16_t      rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
uint16_t      rdma_default_pkey = RDMA_DEFAULT_PKEY;
uint8_t       rdma_default_qp_ous_rd_atom; 
uint8_t       rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
enum ibv_mtu  rdma_default_mtu;
uint32_t      rdma_default_psn = RDMA_DEFAULT_PSN;
uint8_t       rdma_default_min_rnr_timer = RDMA_DEFAULT_MIN_RNR_TIMER;
uint8_t       rdma_default_service_level = RDMA_DEFAULT_SERVICE_LEVEL;
uint8_t       rdma_default_static_rate = RDMA_DEFAULT_STATIC_RATE;
uint8_t       rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;
uint8_t       rdma_default_time_out = RDMA_DEFAULT_TIME_OUT;
uint8_t       rdma_default_retry_count = RDMA_DEFAULT_RETRY_COUNT;
uint8_t       rdma_default_rnr_retry = RDMA_DEFAULT_RNR_RETRY;
int           rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int           rdma_read_reserve = RDMA_READ_RESERVE;
long          rdma_eagersize_1sc;
int           rdma_put_fallback_threshold;
int           rdma_get_fallback_threshold;
int           rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
int           rdma_polling_set_limit = -1;
int           rdma_polling_set_threshold = 10;
int	      rdma_eager_limit = 32;
int           rdma_iba_eager_threshold;
char          rdma_iba_hca[32];
int           rdma_max_inline_size;
unsigned int  rdma_ndreg_entries = RDMA_NDREG_ENTRIES;
int           rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
int           rdma_r3_threshold = 4096;
int           rdma_r3_threshold_nocache = 8192 * 4;
int           num_rdma_buffer;
int           USE_SMP = 1;
int           enable_knomial_2level_bcast=1;
int           inter_node_knomial_factor=4;
int           intra_node_knomial_factor=4;
int           knomial_2level_bcast_threshold=0;
/* Force to use rendezvous if extended sendq size exceeds this value */
int           rdma_rndv_ext_sendq_size = 5;
/* Whether coalescing of messages should be attempted */
int           rdma_use_coalesce = 1;

/* If this number of eager sends are already outstanding
 * the message can be coalesced with other messages (and
 * will not be sent until a previous message completes)
 */
int           rdma_coalesce_threshold = 6;

/* max (total) number of vbufs to allocate, after which process
 * terminates with a fatal error.
 * -1 means no limit.
 */
int rdma_vbuf_max = -1;
/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int rdma_vbuf_secondary_pool_size = RDMA_VBUF_SECONDARY_POOL_SIZE;

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int rdma_vbuf_pool_size         = RDMA_VBUF_POOL_SIZE;
int rdma_prepost_depth          = RDMA_PREPOST_DEPTH;
int rdma_initial_prepost_depth  = RDMA_INITIAL_PREPOST_DEPTH;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
int rdma_prepost_noop_extra     = 6;
int rdma_credit_preserve;
int rdma_initial_credits        = 0; 

/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + rdma_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
int rdma_rq_size;

uint32_t viadev_srq_size = 512;
uint32_t viadev_srq_limit = 30;
uint32_t viadev_max_r3_oust_send = 32;


/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is rdma_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int rdma_prepost_rendezvous_extra   = 10;
int rdma_dynamic_credit_threshold   = 10;
int rdma_credit_notify_threshold    = 10;
int rdma_prepost_threshold          = 5;

unsigned long rdma_max_registered_pages = RDMA_MAX_REGISTERED_PAGES;
unsigned long rdma_dreg_cache_limit = 0;

/* Blocking mode progress */
int rdma_use_blocking = 0;
unsigned long rdma_spin_count = 5000;

/* The total size of each vbuf. Used to be the eager threshold, but
 * it can be smaller, so that each eager message will span over few
 * vbufs
 */
int rdma_vbuf_total_size; 

/* Small message scheduling policy
 * Was earlier set to USE_FIRST, optimized for minimal QP cache misses
 * Now setting it to ROUND_ROBIN as we get better performance.
 */
int sm_scheduling = USE_FIRST;

/* This value should increase with the increase in number
 * of rails */
int striping_threshold = STRIPING_THRESHOLD;

/* Used IBoEth mode */
int use_iboeth = 0;

/* Linear update factor for HSAM */
int alpha = 0.9;
int stripe_factor = 1;
int apm_tester = 0;
int apm_count;

static int check_hsam_parameters();

static inline int log_2(int np)
{
    int lgN, t;
    for (lgN = 0, t = 1; t < np; lgN++, t += t);
    return lgN;
}

static int get_rate(umad_ca_t *umad_ca)
{
    int i;

    for (i = 1; i <= umad_ca->numports; i++) {
        if (IBV_PORT_ACTIVE == umad_ca->ports[i]->state) {
            return umad_ca->ports[i]->rate;
        }
    }
    return 0;
}

#undef FUNCNAME
#define FUNCNAME hcaNameToType
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int hcaNameToType(char *dev_name, int* hca_type)
{
    MPIDI_STATE_DECL(MPID_STATE_HCANAMETOTYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_HCANAMETOTYPE);
    int mpi_errno = MPI_SUCCESS;
    int rate;

    *hca_type = UNKNOWN_HCA;

    if (!strncmp(dev_name, "mlx4", 4) || !strncmp(dev_name, "mthca", 5)) {
        umad_ca_t umad_ca;

        *hca_type = MLX_PCI_X;

        if (umad_init() < 0) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadinit");
        }

        memset(&umad_ca, 0, sizeof(umad_ca_t));
        if (umad_get_ca(dev_name, &umad_ca) < 0) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadgetca");
        }

        rate = get_rate(&umad_ca);
        if (!rate) {
            umad_release_ca(&umad_ca);
            umad_done();
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadgetrate");
        }

        if (!strncmp(dev_name, "mthca", 5)) {
            *hca_type = MLX_PCI_X;

            if (!strncmp(umad_ca.ca_type, "MT25", 4)) {
                switch (rate) {
                case 20:
                    *hca_type = MLX_PCI_EX_DDR;
                    break;
                case 10:
                    *hca_type = MLX_PCI_EX_SDR;
                    break;
                default:
                    *hca_type = MLX_PCI_EX_SDR;
                    break;
                }
            } else if (!strncmp(umad_ca.ca_type, "MT23", 4)) {
                *hca_type = MLX_PCI_X;
            } else {
                *hca_type = MLX_PCI_EX_SDR; 
            }
        } else { /* mlx4 */ 
            switch(rate) {
            case 40:
                *hca_type = MLX_CX_QDR;
                break;
            case 20:
                *hca_type = MLX_CX_DDR;
                break;
            case 10:
                *hca_type = MLX_CX_SDR;
                break;
            default:
                *hca_type = MLX_CX_SDR;
                break;
            }
        }

        umad_release_ca(&umad_ca);
        umad_done();
    } else if(!strncmp(dev_name, "ipath", 5)) {
        *hca_type = PATH_HT;
    } else if(!strncmp(dev_name, "ehca", 4)) {
        *hca_type = IBM_EHCA;
    } else if (!strncmp(dev_name, "cxgb3", 5)) {
        *hca_type = CHELSIO_T3;
    } else {
        *hca_type = UNKNOWN_HCA;
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_HCANAMETOTYPE);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME get_hca_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int get_hca_type (struct ibv_device* dev, struct ibv_context* ctx, int* hca_type)
{
    MPIDI_STATE_DECL(MPID_STATE_GET_HCA_TYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_GET_HCA_TYPE);
    int mpi_errno = MPI_SUCCESS;
    struct ibv_device_attr dev_attr;

    memset(&dev_attr, 0, sizeof(struct ibv_device_attr));

    char* dev_name = (char*) ibv_get_device_name(dev);

    if (!dev_name)
    {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ibv_get_device_name");
    }

    int ret = ibv_query_device(ctx, &dev_attr);

    if (ret)
    {
        MPIU_ERR_SETANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**ibv_query_device",
            "**ibv_query_device %s",
            dev_name
        );
    }

    if ((mpi_errno = hcaNameToType(dev_name, hca_type)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_GET_HCA_TYPE);
    return mpi_errno;
}

/*
 * Function: rdma_cm_get_hca_type
 *
 * Description:
 *      Finds out the type of the HCA on the system.
 *
 * Input:
 *      use_iwarp_mode  - Command line input specifying whether we need to use
 *                        iWARP mode.
 * Output:
 *      hca_type        - The type of HCA we are going to use.
 *
 * Return:
 *      Success:    MPI_SUCCESS.
 *      Failure:    ERROR (-1).
 */
#if defined(RDMA_CM)
#undef FUNCNAME
#define FUNCNAME rdma_cm_get_hca_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_cm_get_hca_type (int use_iwarp_mode, int* hca_type)
{
    MPIDI_STATE_DECL(MPID_STATE_RDMA_CM_GET_HCA_TYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_RDMA_CM_GET_HCA_TYPE);
    int mpi_errno = MPI_SUCCESS;
    int numdevices = 0;
    int i = 0;
    int ret;
    struct ibv_device_attr dev_attr;
    char* dev_name;
    *hca_type = UNKNOWN_HCA;
    struct ibv_context** ctx = rdma_get_devices(&numdevices);

    for (; i < numdevices; ++i) {
        dev_name = (char*) ibv_get_device_name(ctx[i]->device);

        if (!dev_name) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ibv_get_device_name");
        }
 
        if ((ret = ibv_query_device(ctx[i], &dev_attr))) {
            MPIU_ERR_SETANDJUMP1(
                mpi_errno,
                MPI_ERR_OTHER,
                "**ibv_query_device",
                "**ibv_query_device %s",
                dev_name
            );
        }

        if (ERROR == rdma_find_active_port(ctx[i], ctx[i]->device)) {
            /* Trac #376 The device has no active ports, continue to next device */
            continue;
        }

        if ((mpi_errno = hcaNameToType(dev_name, hca_type)) != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno); 
        }

        if (!use_iwarp_mode && *hca_type != UNKNOWN_HCA) {
            break;
        } else if (use_iwarp_mode && *hca_type == CHELSIO_T3) {
            /* Trac #376 recognize chelsio nic even if it's not the first */
            strncpy(rdma_iba_hca, CHELSIO_RNIC, 32);
            break;
        }
    }

    if (use_iwarp_mode && *hca_type != CHELSIO_T3) {
        /* iWARP RNIC not found. Assuming a generic Adapter. */
        *hca_type = UNKNOWN_HCA;
    }

fn_fail:
    rdma_free_devices(ctx);
    MPIDI_FUNC_EXIT(MPID_STATE_RDMA_CM_GET_HCA_TYPE);
    return mpi_errno;
}
#endif /* defined(RDMA_CM) */

#undef FUNCNAME
#define FUNCNAME rdma_get_control_parameters
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_get_control_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    MPIDI_STATE_DECL(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);
    MPIDI_FUNC_ENTER(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);
    char* value = NULL;
    int mpi_errno = MPI_SUCCESS;
    int my_rank = -1;

    PMI_Get_rank(&my_rank);

    if ((value = getenv("MV2_NUM_HCAS")) != NULL) {
        rdma_num_hcas = (int)atoi(value);

        if (rdma_num_hcas > MAX_NUM_HCAS) {
            rdma_num_hcas = MAX_NUM_HCAS;

	    MPIU_Msg_printf("Warning, max hca is %d, change %s in ibv_param.h "
		    "to overide the option\n", MAX_NUM_HCAS, "MAX_NUM_HCAS");
        }
    }

    /* Start HSAM Parameters */
    if ((value = getenv("MV2_USE_HSAM")) != NULL) {
        proc->has_hsam = (int)atoi(value);
        if(proc->has_hsam) {
            check_hsam_parameters();
        }
    } else {
        /* By default disable the HSAM, due to problem with
         * multi-pathing with current version of opensm and
         * up/down */
        proc->has_hsam = 0;
    }

    proc->has_apm = (value = getenv("MV2_USE_APM")) != NULL ? (int) atoi(value) : 0;
    apm_tester = (value = getenv("MV2_USE_APM_TEST")) != NULL ? (int) atoi(value) : 0;
    apm_count = (value = getenv("MV2_APM_COUNT")) != NULL ? (int) atoi(value) : APM_COUNT;

    /* Scheduling Parameters */
    if ( (value = getenv("MV2_SM_SCHEDULING")) != NULL) {
        if (!strcmp(value, "USE_FIRST")) {
            sm_scheduling = USE_FIRST;
        } else if (!strcmp(value, "ROUND_ROBIN")) {
            sm_scheduling = ROUND_ROBIN;
        } else if (!strcmp(value, "PROCESS_BINDING")) {
            sm_scheduling = PROCESS_BINDING;
        } else {
            MPIU_Usage_printf("Invalid small message scheduling\n");
        }
    }

    /* End : HSAM Parameters */

    strncpy(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32);

    if ((value = getenv("MV2_IBA_HCA")) != NULL) {
        strncpy(rdma_iba_hca, value, 32);
    }

#if defined(RDMA_CM)
    if ((value = getenv("MV2_USE_IWARP_MODE")) != NULL) {
            proc->use_rdma_cm = !!atoi(value);
            proc->use_iwarp_mode = !!atoi(value);
    }
    
    if (!proc->use_rdma_cm){
	    if ((value = getenv("MV2_USE_RDMA_CM")) != NULL) {
		    proc->use_rdma_cm = !!atoi(value);
	    }
	    else {
		    proc->use_rdma_cm = 0;
		    proc->use_iwarp_mode = 0;
	    }
    }

    if ((value = getenv("MV2_SUPPORT_DPM")) && !!atoi(value)) {
        proc->use_rdma_cm = 0;
        proc->use_iwarp_mode = 0;
    }

    if (proc->use_rdma_cm) {
        int rank = ERROR;
	    int pg_size = ERROR;
	    int threshold = ERROR;

        if (proc->use_iwarp_mode) {
            /* Trac #423 */
	        threshold = MPIDI_CH3I_CM_DEFAULT_IWARP_ON_DEMAND_THRESHOLD;
        } else {
	        threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;
        }

	    PMI_Get_size(&pg_size);
	    PMI_Get_rank(&rank);                                      

	    if ((value = getenv("MV2_ON_DEMAND_THRESHOLD")) != NULL){
		    threshold = atoi(value);
	    }
	    if (pg_size > threshold) {
		    proc->use_rdma_cm_on_demand = 1;
	    }	    
    }
#endif

    if ((mpi_errno = rdma_open_hca(proc)) != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }
    
    /* Set default parameter acc. to the first hca */
#if defined(RDMA_CM)
    if (proc->use_iwarp_mode) {
	    if ((mpi_errno = rdma_cm_get_hca_type(proc->use_iwarp_mode, &proc->hca_type)) != MPI_SUCCESS)
	    {
		    MPIU_ERR_POP(mpi_errno);
	    }

	    if (proc->hca_type == CHELSIO_T3)
	    {
		    proc->use_iwarp_mode = 1;
	    }
    }
    else 
#endif /* defined(RDMA_CM) */
    if ((mpi_errno = get_hca_type(proc->ib_dev[0], proc->nic_context[0], &proc->hca_type)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    int size;
    PMI_Get_size(&size); 

    if (size <= 32)
    {
        proc->cluster_size = VERY_SMALL_CLUSTER;
    }
    else if (size <= 64)
    {
        proc->cluster_size = SMALL_CLUSTER;
    }
    else if (size < 256)
    {
        proc->cluster_size = MEDIUM_CLUSTER;
    }
    else
    {
        proc->cluster_size = LARGE_CLUSTER;
    }

    proc->has_srq = (value = getenv("MV2_USE_SRQ")) != NULL ? !!atoi(value) : 1;
#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        proc->has_srq = 1;
        MPIU_Assert (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND);
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
        rdma_use_coalesce = 0;
        rdma_use_blocking = 0;
    }
#endif /* _ENABLE_XRC_ */
    
    if (proc->has_srq
        && proc->hca_type != PATH_HT
        && proc->hca_type != MLX_PCI_X
        && proc->hca_type != IBM_EHCA
#if defined(RDMA_CM)
        && !proc->use_iwarp_mode
#endif /* defined(RDMA_CM) */
    )
    {
        proc->post_send = post_srq_send;
    }
    else
    {
        proc->has_srq = 0;
        proc->post_send = post_send;
    }

#if defined(CKPT)
    proc->has_adaptive_fast_path = 0;
    rdma_polling_set_limit = 0; 
#else /* defined(CKPT) */
    if ((value = getenv("MV2_USE_RDMA_FAST_PATH")) != NULL)
    {
        proc->has_adaptive_fast_path = !!atoi(value);

	if (!proc->has_adaptive_fast_path)
        {
	    rdma_polling_set_limit = 0;
        }
    }
    else
    {
        proc->has_adaptive_fast_path = 1;
    }
#endif /* defined(CKPT) */

    proc->has_ring_startup = (value = getenv("MV2_USE_RING_STARTUP")) != NULL ? !!atoi(value) : 1;

#if !defined(DISABLE_PTMALLOC)
    proc->has_lazy_mem_unregister = (value = getenv("MV2_USE_LAZY_MEM_UNREGISTER")) != NULL ? !!atoi(value) : 1;
#endif /* !defined(DISABLE_PTMALLOC) */

#if defined(CKPT)
    proc->has_one_sided = 0;
#else /* defined(CKPT) */
    proc->has_one_sided = (value = getenv("MV2_USE_RDMA_ONE_SIDED")) != NULL ? !!atoi(value) : 1; 

#endif /* defined(CKPT) */

    if ((value = getenv("MV2_RNDV_EXT_SENDQ_SIZE")) != NULL) {
        rdma_rndv_ext_sendq_size = atoi(value);
        if (rdma_rndv_ext_sendq_size <= 1) {
            MPIU_Usage_printf("Setting MV2_RNDV_EXT_SENDQ_SIZE smaller than 1 "
                              "will severely limit the MPI bandwidth.\n");
        }
    }

    if ((value = getenv("MV2_COALESCE_THRESHOLD")) != NULL) {
        rdma_coalesce_threshold = atoi(value);
        if(rdma_coalesce_threshold < 1) {
            MPIU_Usage_printf("MV2_COALESCE_THRESHOLD must be >= 1\n");
            rdma_coalesce_threshold = 1;
        }
    }

    if ((value = getenv("MV2_USE_COALESCE")) != NULL) {
        rdma_use_coalesce = !!atoi(value);
    }

    if (proc->hca_type == MLX_CX_DDR ||
        proc->hca_type == MLX_CX_SDR ||
        proc->hca_type == MLX_CX_QDR) {
	rdma_use_coalesce = 0;
    }

    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {
        USE_SMP = !!atoi(value);
    }

    if ((value = getenv("MV2_USE_IBOETH")) != NULL) {
        use_iboeth = !!atoi(value);
        if (1 == proc->has_ring_startup) {
            if (0 == my_rank) {
                MPIU_Usage_printf("Ring start up cannot be used in IBoEth mode."
                                "Falling back to PMI exchange.\r\n"
                                "You can also set MV2_USE_RING_STARTUP=0.\r\n");
            }
            proc->has_ring_startup = 0;
        }
        if (!USE_SMP) {
            if (0 == my_rank) {
                MPIU_Usage_printf("IBoEth mode cannot function without SHMEM."
                                "Falling back to use SHMEM.\r\n"
                                "Please do NOT set MV2_USE_SHARED_MEM=0.\r\n");
            }
            USE_SMP = 1;
        }
    }

#ifdef _ENABLE_XRC_
    if (!USE_XRC) {
#endif
    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        rdma_use_blocking = !!atoi(value);

        /* Automatically turn off RDMA fast path */
        if(rdma_use_blocking) {
            USE_SMP = 0;
            proc->has_adaptive_fast_path = 0;
        }
    }
#ifdef _ENABLE_XRC_
    }
#endif

    if ((value = getenv("MV2_SPIN_COUNT")) != NULL) {
        rdma_spin_count = atol(value);
    }

    if ((value = getenv("MV2_RNDV_PROTOCOL")) != NULL) {
        if (strncmp(value,"RPUT", 4) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        } else if (strncmp(value,"RGET", 4) == 0
#ifdef _ENABLE_XRC_
                && !USE_XRC
#endif
                ) {
#if defined(CKPT)
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\" or \"R3\" when checkpoint is enabled\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
#else /* defined(CKPT) */
            rdma_rndv_protocol = VAPI_PROTOCOL_RGET;
#endif /* defined(CKPT) */
        } else if (strncmp(value,"R3", 2) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_R3;
        } else {
#ifdef _ENABLE_XRC_
            if(!USE_XRC)
#endif
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\", \"RGET\", or \"R3\"\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        }
    }

    if ((value = getenv("MV2_R3_THRESHOLD")) != NULL) {
        rdma_r3_threshold = atoi(value);
        if(rdma_r3_threshold < 0) {
            rdma_r3_threshold = 0;
        }
    }

    if ((value = getenv("MV2_R3_NOCACHE_THRESHOLD")) != NULL) {
        rdma_r3_threshold_nocache = atoi(value);
        if(rdma_r3_threshold_nocache < 0) {
            rdma_r3_threshold_nocache = 0;
        }
    }

#if defined(RDMA_CM)
    if (proc->use_rdma_cm_on_demand){
	    proc->use_iwarp_mode = 1;
    }
#endif /* defined(RDMA_CM) */

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

void  rdma_set_default_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->cluster_size) {

        case LARGE_CLUSTER:
            rdma_vbuf_total_size = 2*1024;
            break;
        case MEDIUM_CLUSTER:
            rdma_vbuf_total_size = 4*1024;
            break;
        case SMALL_CLUSTER:
        case VERY_SMALL_CLUSTER:
        default:
            switch(proc->hca_type) {
                case MLX_PCI_X:
                case IBM_EHCA:
                    rdma_vbuf_total_size = 12*1024;
                    break;
                case MLX_CX_DDR:
                case MLX_CX_SDR:
                case MLX_CX_QDR:
                    rdma_vbuf_total_size = 9 * 1024;
                    break;
	        case CHELSIO_T3:
		    rdma_vbuf_total_size = 9 * 1024;
		    break;
                case MLX_PCI_EX_SDR:
                case MLX_PCI_EX_DDR:
                case PATH_HT:
                default:
#ifdef _X86_64_
                    rdma_vbuf_total_size = 9 * 1024;
#else
                    rdma_vbuf_total_size = 6 * 1024;
#endif
                    
                    break;
            }
            break;
    }

    switch(proc->hca_type) {
        case MLX_PCI_X:
        case IBM_EHCA:
            switch(proc->cluster_size) {
                case LARGE_CLUSTER:
                    num_rdma_buffer         = 4;
                    rdma_iba_eager_threshold    = 6 * 1024 -sizeof(VBUF_FLAG_TYPE);
                    break;
                case MEDIUM_CLUSTER:
                    num_rdma_buffer         = 16;
                    rdma_iba_eager_threshold    = 6 * 1024 -sizeof(VBUF_FLAG_TYPE);
                    break;
                case SMALL_CLUSTER:
                case VERY_SMALL_CLUSTER:
                default:
                    num_rdma_buffer         = 32;
                    rdma_iba_eager_threshold    = rdma_vbuf_total_size - 
                        sizeof(VBUF_FLAG_TYPE);
                    break;
            }

            rdma_eagersize_1sc      = 4 * 1024;
            rdma_put_fallback_threshold = 8 * 1024;
            rdma_get_fallback_threshold = 394 * 1024;
            break;
        case CHELSIO_T3:
	    switch(proc->cluster_size) {
		case LARGE_CLUSTER:
			num_rdma_buffer         = 4;
			rdma_iba_eager_threshold    = 2 * 1024 -sizeof(VBUF_FLAG_TYPE);
			break;
                case MEDIUM_CLUSTER:
			num_rdma_buffer         = 8;
			rdma_iba_eager_threshold = 4 * 1024 -sizeof(VBUF_FLAG_TYPE);

			break;
                case SMALL_CLUSTER:
                case VERY_SMALL_CLUSTER:
                default:
			num_rdma_buffer         = 16;
			rdma_iba_eager_threshold = rdma_vbuf_total_size -
				sizeof(VBUF_FLAG_TYPE);
			break;
		}
	    rdma_eagersize_1sc      = 4 * 1024;
	    rdma_put_fallback_threshold = 8 * 1024;
	    rdma_get_fallback_threshold = 394 * 1024;
	    break;
        case MLX_PCI_EX_SDR:
        case MLX_PCI_EX_DDR:
        case MLX_CX_DDR:
        case MLX_CX_SDR:
        case MLX_CX_QDR:
        case PATH_HT:
        default:
            switch(proc->cluster_size) {
                case LARGE_CLUSTER:
                    num_rdma_buffer         = 4;
                    rdma_iba_eager_threshold    = 2 * 1024- sizeof(VBUF_FLAG_TYPE);
                    break;
                case MEDIUM_CLUSTER:
                    num_rdma_buffer         = 8;
                    rdma_iba_eager_threshold    = 4 * 1024 -sizeof(VBUF_FLAG_TYPE);
                    break;
                case SMALL_CLUSTER:
                case VERY_SMALL_CLUSTER:
                default:
                    num_rdma_buffer         = 16;
                    rdma_iba_eager_threshold = rdma_vbuf_total_size -
                        sizeof(VBUF_FLAG_TYPE);
                    break;
            }

            rdma_eagersize_1sc      = 4 * 1024;
            rdma_put_fallback_threshold = 2 * 1024;
            rdma_get_fallback_threshold = 192 * 1024;

            break;
    }

    if (proc->hca_type == PATH_HT) {
        rdma_default_qp_ous_rd_atom = 1;
    } else {
        rdma_default_qp_ous_rd_atom = 4;
    }

    if (proc->hca_type == IBM_EHCA) {
        rdma_max_inline_size = -1;
    } else if (proc->hca_type == CHELSIO_T3) {
	rdma_max_inline_size = 64;
    } else {
        rdma_max_inline_size = 128;
    }

    if (proc->hca_type == MLX_PCI_EX_DDR) {
        rdma_default_mtu = IBV_MTU_2048;
    } else if(proc->hca_type == MLX_CX_QDR) {
        rdma_default_mtu = IBV_MTU_2048;
    } else {
        rdma_default_mtu = IBV_MTU_1024;
    }

    if (proc->hca_type == CHELSIO_T3) {
        /* Trac #423 */
        rdma_default_max_cq_size = RDMA_DEFAULT_IWARP_CQ_SIZE;
        rdma_prepost_noop_extra = 8;
    }

    if (proc->has_srq) {
        rdma_credit_preserve = 100;
    } else {
        rdma_credit_preserve = 3;
    }
}

/* rdma_param_handle_heterogenity resets control parameters given the hca_type 
 * from all ranks. Parameters may change:
 *      rdma_default_mtu
 *      rdma_iba_eager_threshold
 *      proc->has_srq
 *      rdma_credit_preserve
 *      rdma_max_inline_size
 *      rdma_default_qp_ous_rd_atom
 *      rdma_put_fallback_threshold
 *      rdma_get_fallback_threshold
 *      num_rdma_buffer
 *      rdma_vbuf_total_size
 */
void rdma_param_handle_heterogenity(uint32_t hca_type[], int pg_size)
{
    uint32_t type;
    int heterogenous = 0;
    int i;

    type = hca_type[0];
    for (i = 0; i < pg_size; ++ i) {
        if (hca_type[i] == PATH_HT ||
            hca_type[i] == MLX_PCI_X ||
            hca_type[i] == IBM_EHCA) {
            MPIDI_CH3I_RDMA_Process.has_srq = 0;
            MPIDI_CH3I_RDMA_Process.post_send = post_send;
            rdma_credit_preserve = 3;
        }

        if (hca_type[i] == IBM_EHCA)
            rdma_max_inline_size = -1;                

        if (hca_type[i] == PATH_HT)
            rdma_default_qp_ous_rd_atom = 1;

        if (hca_type[i] != type)
            heterogenous = 1;

        DEBUG_PRINT("rank %d, type %d\n", i, hca_type[i]);
    }

    if (heterogenous) {
        rdma_default_mtu = IBV_MTU_1024;
        rdma_vbuf_total_size = 8 * 1024;
        rdma_iba_eager_threshold = rdma_vbuf_total_size;
        rdma_max_inline_size = (rdma_max_inline_size == -1) ? -1 : 64;
        rdma_put_fallback_threshold = 4 * 1024;
        rdma_get_fallback_threshold = 192 * 1024;
        num_rdma_buffer = 16;
    }
}

void rdma_get_user_parameters(int num_proc, int me)
{
    char *value;

    if ((value = getenv("MV2_DEFAULT_MTU")) != NULL) {

        if (strncmp(value,"IBV_MTU_256",11)==0) {
            rdma_default_mtu = IBV_MTU_256;
        } else if (strncmp(value,"IBV_MTU_512",11)==0) {
            rdma_default_mtu = IBV_MTU_512;
        } else if (strncmp(value,"IBV_MTU_1024",12)==0) {
            rdma_default_mtu = IBV_MTU_1024;
        } else if (strncmp(value,"IBV_MTU_2048",12)==0) {
            rdma_default_mtu = IBV_MTU_2048;
        } else if (strncmp(value,"IBV_MTU_4096",12)==0) {
            rdma_default_mtu = IBV_MTU_4096;
        } else {
            rdma_default_mtu = IBV_MTU_1024;
        }
    }


     if( (value = getenv("MV2_USE_KNOMIAL_2LEVEL_BCAST")) != NULL) { 
            enable_knomial_2level_bcast=!!(int)atoi(value);
        if(enable_knomial_2level_bcast <= 0)  { 
                enable_knomial_2level_bcast = 0;
         } 
     }     

    if( (value = getenv("MV2_KNOMIAL_INTRA_NODE_FACTOR")) != NULL) {
            intra_node_knomial_factor=(int)atoi(value);
        if(intra_node_knomial_factor < INTRA_NODE_KNOMIAL_FACTOR_MIN) { 
                intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if(intra_node_knomial_factor > INTRA_NODE_KNOMIAL_FACTOR_MAX) { 
                intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MAX;
        } 
     }     
    if( (value = getenv("MV2_KNOMIAL_INTER_NODE_FACTOR")) != NULL) {
            inter_node_knomial_factor=(int)atoi(value);
        if(inter_node_knomial_factor < INTER_NODE_KNOMIAL_FACTOR_MIN) { 
                inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if(inter_node_knomial_factor > INTER_NODE_KNOMIAL_FACTOR_MAX) { 
                inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MAX;
        } 
     }     
    if( (value = getenv("MV2_KNOMIAL_2LEVEL_BCAST_THRESHOLD")) != NULL) {
            knomial_2level_bcast_threshold=(int)atoi(value);
     }



    /* Get number of ports/HCA used by a process */
    if ((value = getenv("MV2_NUM_PORTS")) != NULL) {
        rdma_num_ports = (int)atoi(value);
        if (rdma_num_ports > MAX_NUM_PORTS) {
            rdma_num_ports = MAX_NUM_PORTS;
	    MPIU_Usage_printf("Warning, max ports per hca is %d, change %s in "
		    "ibv_param.h to overide the option\n", MAX_NUM_PORTS,
		    "MAX_NUM_PORTS");
        }
    }

    /* Get number of qps/port used by a process */
    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {

        rdma_num_qp_per_port = (int)atoi(value);

        if (rdma_num_qp_per_port > MAX_NUM_QP_PER_PORT) {
            rdma_num_qp_per_port = MAX_NUM_QP_PER_PORT;
            MPIU_Usage_printf("Warning, max qps per port is %d, change %s in "
		    "ibv_param.h to overide the option\n", MAX_NUM_QP_PER_PORT,
		    "MAX_NUM_QP_PER_PORT");
        }
    }

    if ((value = getenv("MV2_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_MAX_INLINE_SIZE")) != NULL) {
        rdma_max_inline_size = (int)atoi(value);
    } else if(num_proc > 256) {
        rdma_max_inline_size = 0;
    }

    if ((value = getenv("MV2_DEFAULT_MAX_CQ_SIZE")) != NULL) {
        rdma_default_max_cq_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_READ_RESERVE")) != NULL) {
        rdma_read_reserve = (int)atoi(value);
    }
    if ((value = getenv("MV2_NUM_RDMA_BUFFER")) != NULL) { 
        num_rdma_buffer = (int)atoi(value);
    }
    if ((value = getenv("MV2_POLLING_SET_THRESHOLD")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_threshold = atoi(value);
    }
    if ((value = getenv("MV2_RDMA_EAGER_LIMIT")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_eager_limit = atoi(value);
	if (rdma_eager_limit < 0)
	    rdma_eager_limit = 0;
    }
    if ((value = getenv("MV2_POLLING_SET_LIMIT")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_limit = atoi(value);
        if (rdma_polling_set_limit == -1) {
            rdma_polling_set_limit = log_2(num_proc);
        }
    } else if (MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_limit = num_proc;
    }
    if ((value = getenv("MV2_VBUF_TOTAL_SIZE")) != NULL) {
        rdma_vbuf_total_size= atoi(value);
        if (rdma_vbuf_total_size <= 2 * sizeof(int))
            rdma_vbuf_total_size = 2 * sizeof(int);
    }

    /* We have read the value of the rendezvous threshold, and the number of
     * rails used for communication, increase the striping threshold
     * accordingly */

    /* Messages in between will use the rendezvous protocol, however will
     * not be striped */

    striping_threshold = rdma_vbuf_total_size * rdma_num_ports *
        rdma_num_qp_per_port * rdma_num_hcas;
       
    if ((value = getenv("MV2_SRQ_SIZE")) != NULL) {
        viadev_srq_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("MV2_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);

        if(viadev_srq_limit > viadev_srq_size) {
	    MPIU_Usage_printf("SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if ((value = getenv("MV2_IBA_EAGER_THRESHOLD")) != NULL) {
        rdma_iba_eager_threshold = (int)atoi(value);
    }

    if ((value = getenv("MV2_STRIPING_THRESHOLD")) != NULL) {
        striping_threshold = atoi(value);
        if (striping_threshold <= 0) {
            /* Invalid value - set to computed value */
            striping_threshold = rdma_vbuf_total_size * rdma_num_ports *
                                    rdma_num_qp_per_port * rdma_num_hcas;
        }
        if (striping_threshold < rdma_iba_eager_threshold) {
            /* checking to make sure that the striping threshold is not less
             * than the RNDV threshold since it won't work as expected.
             */
            striping_threshold = rdma_iba_eager_threshold;
        }
    }

    if ((value = getenv("MV2_INTEGER_POOL_SIZE")) != NULL) {
        rdma_integer_pool_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PUT_GET_LIST_SIZE")) != NULL) {
        rdma_default_put_get_list_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_EAGERSIZE_1SC")) != NULL) {
        rdma_eagersize_1sc = (int)atoi(value);
    }
    if ((value = getenv("MV2_PUT_FALLBACK_THRESHOLD")) != NULL) {
        rdma_put_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("MV2_GET_FALLBACK_THRESHOLD")) != NULL) {
        rdma_get_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PORT")) != NULL) {
        rdma_default_port = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_QP_OUS_RD_ATOM")) != NULL) {
        rdma_default_qp_ous_rd_atom = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PSN")) != NULL) {
        rdma_default_psn = (uint32_t)atoi(value);
    }

    if ((value = getenv("MV2_DEFAULT_PKEY")) != NULL) {
        rdma_default_pkey = (uint16_t)strtol(value, (char **) NULL,0) & PKEY_MASK; 
    }

    if ((value = getenv("MV2_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        rdma_default_min_rnr_timer = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_SERVICE_LEVEL")) != NULL) {
        rdma_default_service_level = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_TIME_OUT")) != NULL) {
        rdma_default_time_out = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_STATIC_RATE")) != NULL) {
        rdma_default_static_rate = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_SRC_PATH_BITS")) != NULL) {
        rdma_default_src_path_bits = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_RETRY_COUNT")) != NULL) {
        rdma_default_retry_count = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_RNR_RETRY")) != NULL) {
        rdma_default_rnr_retry = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SG_LIST")) != NULL) {
        rdma_default_max_sg_list = (uint32_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SEND_WQE")) != NULL) {
        rdma_default_max_send_wqe = atol(value);
    } else if(num_proc > 256) {
        rdma_default_max_send_wqe = 16;
    }
    if ((value = getenv("MV2_DEFAULT_MAX_RECV_WQE")) != NULL) {
        rdma_default_max_recv_wqe = atol(value);
    } 
    if ((value = getenv("MV2_NDREG_ENTRIES")) != NULL) {
        rdma_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = getenv("MV2_VBUF_MAX")) != NULL) {
        rdma_vbuf_max = atoi(value);
    }
    if ((value = getenv("MV2_INITIAL_PREPOST_DEPTH")) != NULL) {
        rdma_initial_prepost_depth = atoi(value);
    }    
    if ((value = getenv("MV2_PREPOST_DEPTH")) != NULL) {
        rdma_prepost_depth = atoi(value);
    }  
    if ((value = getenv("MV2_MAX_REGISTERED_PAGES")) != NULL) {
        rdma_max_registered_pages = atol(value);
    }
    if ((value = getenv("MV2_VBUF_POOL_SIZE")) != NULL) {
        rdma_vbuf_pool_size = atoi(value);
    }
    if ((value = getenv("MV2_DREG_CACHE_LIMIT")) != NULL) {
        rdma_dreg_cache_limit = atol(value);
    }
    if (rdma_vbuf_pool_size <= 10) {
        rdma_vbuf_pool_size = 10;
        MPIU_Usage_printf("Warning! Too small vbuf pool size (%d).  "
		"Reset to %d\n", rdma_vbuf_pool_size, 10);
    }
    if ((value = getenv("MV2_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
        rdma_vbuf_secondary_pool_size = atoi(value);
    }
    if (rdma_vbuf_secondary_pool_size <= 0) {
        rdma_vbuf_secondary_pool_size = 1;
        MPIU_Usage_printf("Warning! Too small secondary vbuf pool size (%d).  "
                "Reset to %d\n", rdma_vbuf_secondary_pool_size, 1);
    }
    if (rdma_initial_prepost_depth <= rdma_prepost_noop_extra) {
        rdma_initial_credits = rdma_initial_prepost_depth;
    } else {
        rdma_initial_credits =
            rdma_initial_prepost_depth - rdma_prepost_noop_extra;
    }

    rdma_rq_size = rdma_prepost_depth + 
        rdma_prepost_rendezvous_extra + rdma_prepost_noop_extra;
}

/* This function is specifically written to make sure that HSAM
 * parameters are configured correctly */

static int check_hsam_parameters()
{
    char *value;
    int size;

    /* Get the number of processes */
    PMI_Get_size(&size);
 
    /* If the number of processes is less than 64, we can afford * to
     * have more RC QPs and hence a value of 4 is chosen, for * other
     * cases, a value of 2 is chosen */

    /* (rdma_num_qp_per_port/ stripe factor) represents the number
     * of QPs which will be chosen for data transfer at a given point */

    /* If the user has not specified any value, then perform
     * this tuning */

    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {
        rdma_num_qp_per_port = atoi(value);
        if(rdma_num_qp_per_port <= 2) {
            stripe_factor = 1;
        } else {
            stripe_factor = (rdma_num_qp_per_port / 2);
        }
    } else {
        /* Speculated value */

        /* The congestion is actually never seen for less
         * than 8 nodes */
        if((size > 8) && (size < 64)) {
            rdma_num_qp_per_port = 4;
            stripe_factor = (rdma_num_qp_per_port / 2);
        } else {
            rdma_num_qp_per_port = 2;
            stripe_factor = 1;
        }
    }

    return MPI_SUCCESS;
}

/* vi:set sw=4 */
