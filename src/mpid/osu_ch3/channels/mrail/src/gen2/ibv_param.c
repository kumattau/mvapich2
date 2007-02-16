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
unsigned long rdma_default_max_wqe = RDMA_DEFAULT_MAX_WQE;
uint32_t      rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
uint16_t      rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
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
int           rdma_iba_eager_threshold;
char          rdma_iba_hca[32];
int           rdma_max_inline_size;
unsigned int  rdma_ndreg_entries = RDMA_NDREG_ENTRIES;
int           num_rdma_buffer;

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
int rdma_prepost_noop_extra     = 5;
int rdma_credit_preserve;
int rdma_initial_credits        = 0; 

/* Max number of entries on the Send Q of QPs per connection.
 * Should be about (prepost_depth + extra).
 * Must be within NIC MaxQpEntries limit.
 * Size will be adjusted below.
 */
int rdma_sq_size                = 200;

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

/* The total size of each vbuf. Used to be the eager threshold, but
 * it can be smaller, so that each eager message will span over few
 * vbufs
 */
int rdma_vbuf_total_size; 

static inline int log_2(int np)
{
    int lgN, t;

    for (lgN = 0, t = 1; t < np; lgN++, t += t);

    return lgN;
}

static inline int get_hca_type(struct ibv_device *dev,
        struct ibv_context *ctx)
{
    struct ibv_device_attr dev_attr;
    int hca_type = HCA_ERROR;
    int ret, i, rate = 0;
    char *dev_name;
    umad_ca_t umad_ca;

    memset(&dev_attr, 0, sizeof(struct ibv_device_attr));

    ret = ibv_query_device(ctx, &dev_attr);

    if(ret) {
        return HCA_ERROR;
    }

    dev_name = (char *) ibv_get_device_name(dev);

    if(NULL == dev_name) {
        return HCA_ERROR;
    }

    if(!strncmp(dev_name, "mthca", 5)) {

        hca_type = MLX_PCI_X;

        memset(&umad_ca, 0, sizeof(umad_ca_t));

        if(umad_init() < 0) {
            fprintf(stderr,"Error initializing UMAD library,"
                    " best guess as Mellanox PCI-Ex SDR\n");
            return MLX_PCI_EX_SDR;
        }

        ret = umad_get_ca(dev_name, &umad_ca);

        if(ret) {
            fprintf(stderr,"Error getting CA information"
                    " from UMAD library ... taking the"
                    " best guess as Mellanox PCI-Ex SDR\n");
            return MLX_PCI_EX_SDR;
        }

        if(!strncmp(umad_ca.ca_type, "MT25", 4)) {

            for(i = 1; i <= umad_ca.numports; i++) {

                if(IBV_PORT_ACTIVE == umad_ca.ports[i]->state) {
                    rate = umad_ca.ports[i]->rate;
                    break;
                }
            }

            if(20 == rate) {

                hca_type = MLX_PCI_EX_DDR;

            } else if (10 == rate) {

                hca_type = MLX_PCI_EX_SDR;

            } else {

                fprintf(stderr,"Unknown Mellanox PCI-Express HCA"
                        " best guess as Mellanox PCI-Express SDR\n");

                hca_type = MLX_PCI_EX_SDR;
            }

        } else if (!strncmp(umad_ca.ca_type, "MT23", 4)) {

            hca_type = MLX_PCI_X;

        } else {

            fprintf(stderr,"Unknown Mellanox HCA type (%s), best"
                    " guess as Mellanox PCI-Express SDR\n",
                    umad_ca.ca_type);
            hca_type = MLX_PCI_EX_SDR;
        }

        umad_release_ca(&umad_ca);

        umad_done();

    } else if(!strncmp(dev_name, "ipath", 5)) {

        hca_type = PATH_HT;

    } else if(!strncmp(dev_name, "ehca", 4)) {

        hca_type = IBM_EHCA;

    } else {

        hca_type = UNKNOWN_HCA;
    }
    return hca_type;
}

int  rdma_get_control_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    char *value;
    int size;
    int err;

    if ((value = getenv("MV2_NUM_HCAS")) != NULL) {
        rdma_num_hcas = (int)atoi(value);

        if (rdma_num_hcas > MAX_NUM_HCAS) {

            rdma_num_hcas = MAX_NUM_HCAS;

            fprintf(stderr, "Warning, max hca is %d,"
                   " change %s in ibv_param.h to"
                    "overide the option\n", 
                    MAX_NUM_HCAS, "MAX_NUM_HCAS");
        }
    }

    strncpy(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32);

    if ((value = getenv("MV2_IBA_HCA")) != NULL) {
        strncpy(rdma_iba_hca, value, 32);
    }

#if defined(RDMA_CM)
    if ((value = getenv("MV2_USE_RDMA_CM")) != NULL) {
	    proc->use_rdma_cm = 1;
    }
    else {
	    proc->use_rdma_cm = 0;
	    proc->use_iwarp_mode = 0;
    }

    if ((value = getenv("MV2_ENABLE_IWARP_MODE")) != NULL) {
	    proc->use_rdma_cm = 1;
	    proc->use_iwarp_mode = 1;
	    rdma_default_max_cq_size = 2000;
    }
#else
    proc->use_rdma_cm = 0;
    proc->use_iwarp_mode = 0;
#endif

    err = rdma_open_hca(proc);
    
    if (err) {
	    return err;
    }
    
    /* Set default parameter acc. to the first hca */
    if (proc->use_iwarp_mode) {
	    proc->hca_type = UNKNOWN_HCA; /* Using default parameter values */
    }
    else {
	    proc->hca_type = get_hca_type(proc->ib_dev[0],
					  proc->nic_context[0]);
    }
    
    if (proc->hca_type == HCA_ERROR) {
	    return -1;
    }

    PMI_Get_size(&size); 

    if (size <= 64) {
        proc->cluster_size = SMALL_CLUSTER;
    } else if (size < 256) {
        proc->cluster_size = MEDIUM_CLUSTER;
    } else {
        proc->cluster_size = LARGE_CLUSTER;
    }

    if (!(value = getenv("MV2_DISABLE_SRQ")) && proc->hca_type != PATH_HT
            && proc->hca_type != MLX_PCI_X  && proc->hca_type != IBM_EHCA && !proc->use_rdma_cm 
	    && !proc->use_iwarp_mode) {
        proc->has_srq = 1;
        proc->post_send = post_srq_send;
    } else {
        proc->has_srq = 0;
        proc->post_send = post_send;
    }

    if (((value = getenv("MV2_ENABLE_RDMA_FAST_PATH")) == NULL) 
	&& (((value = getenv("MV2_DISABLE_RDMA_FAST_PATH")) != NULL) || proc->use_iwarp_mode)){
        proc->has_adaptive_fast_path = 0;
        rdma_polling_set_limit       = 0;
    } else {
        proc->has_adaptive_fast_path = 1;
    }

    if ((value = getenv("MV2_DISABLE_RING_STARTUP")) != NULL) {
        proc->has_ring_startup = 0;
    } else {
        proc->has_ring_startup = 1;
    }

    if ((value = getenv("MV2_DISABLE_LAZY_MEM_UNREGISTER")) != NULL) {
        proc->has_lazy_mem_unregister = 0;
    } else {
        proc->has_lazy_mem_unregister = 1;
    }

    if ((value = getenv("MV2_DISABLE_RDMA_ONE_SIDED")) != NULL
     || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND
     || proc->use_rdma_cm) {
        proc->has_one_sided = 0;
    } else {
        proc->has_one_sided = 1;
    }

#ifdef CKPT
    /*RDMA FAST PATH is disabled*/
    proc->has_adaptive_fast_path = 0;
    rdma_polling_set_limit       = 0;
    /*RDMA ONE SIDED is disabled*/
    proc->has_one_sided = 0;
#endif
    
    return 0;
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
        default:
            switch(proc->hca_type) {
                case MLX_PCI_X:
                case IBM_EHCA:
                    rdma_vbuf_total_size = 12*1024;
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
                    rdma_iba_eager_threshold    = 6 * 1024;
                    break;
                case MEDIUM_CLUSTER:
                    num_rdma_buffer         = 16;
                    rdma_iba_eager_threshold    = 6 * 1024;
                    break;
                case SMALL_CLUSTER:
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
        case MLX_PCI_EX_SDR:
        case MLX_PCI_EX_DDR:
        case PATH_HT:
        default:
            switch(proc->cluster_size) {
                case LARGE_CLUSTER:
                    num_rdma_buffer         = 4;
                    rdma_iba_eager_threshold    = 2 * 1024;
                    break;
                case MEDIUM_CLUSTER:
                    num_rdma_buffer         = 8;
                    rdma_iba_eager_threshold    = 4 * 1024;
                    break;
                case SMALL_CLUSTER:
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
    } else {
        rdma_max_inline_size = 128;
    }

    if (proc->hca_type == MLX_PCI_EX_DDR) {
        rdma_default_mtu = IBV_MTU_2048;
    } else {
        rdma_default_mtu = IBV_MTU_1024;
    }

    if (proc->has_srq) {
        rdma_credit_preserve = 100;
    } else {
        rdma_credit_preserve = 3;
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

    /* Get number of ports/HCA used by a process */
    if ((value = getenv("MV2_NUM_PORTS")) != NULL) {
        rdma_num_ports = (int)atoi(value);
        if (rdma_num_ports > MAX_NUM_PORTS) {
            rdma_num_ports = MAX_NUM_PORTS;
            fprintf(stderr, "Warning, max ports per hca is %d,"
                   " change %s in ibv_param.h to"
                   "overide the option\n", 
                   MAX_NUM_PORTS, "MAX_NUM_PORTS");
        }
    }

    /* Get number of qps/port used by a process */
    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {

        rdma_num_qp_per_port = (int)atoi(value);

        if (rdma_num_qp_per_port > MAX_NUM_QP_PER_PORT) {
            rdma_num_qp_per_port = MAX_NUM_QP_PER_PORT;
            fprintf(stderr, "Warning, max qps per port is %d,"
                   " change %s in ibv_param.h to"
                   "overide the option\n", 
                   MAX_NUM_QP_PER_PORT, "MAX_NUM_QP_PER_PORT");
        }
    }

    if ((value = getenv("MV2_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = (int)atoi(value);
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

    if ((value = getenv("MV2_SRQ_SIZE")) != NULL) {
        viadev_srq_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("MV2_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);

        if(viadev_srq_limit > viadev_srq_size) {
            fprintf(stderr,
                    "SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if ((value = getenv("MV2_IBA_EAGER_THRESHOLD")) != NULL) {
        rdma_iba_eager_threshold = (int)atoi(value);
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
    if ((value = getenv("MV2_DEFAULT_PKEY_IX")) != NULL) {
        rdma_default_pkey_ix = (uint16_t)atoi(value);
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
    if ((value = getenv("MV2_DEFAULT_MAX_WQE")) != NULL) {
        rdma_default_max_wqe = atol(value);
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
        fprintf(stderr, "[%s:%d] Warning! Too small vbuf pool size (%d) "
                "Reset to %d\n", __FILE__, __LINE__, 
                rdma_vbuf_pool_size, 10);
    }
    if ((value = getenv("MV2_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
        rdma_vbuf_secondary_pool_size = atoi(value);
    }
    if (rdma_vbuf_secondary_pool_size <= 0) {
        rdma_vbuf_secondary_pool_size = 1;
        fprintf(stderr, "[%s:%d] Warning! Too small secondary "
                "vbuf pool size (%d). "
                "Reset to %d\n", __FILE__, __LINE__, 
                rdma_vbuf_secondary_pool_size, 1);
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
