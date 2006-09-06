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

#include "ibv_param.h"
#include "vbuf.h"
/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */
int rdma_num_hcas 	= 1;
int rdma_num_ports 	= 1;
int rdma_num_qp_per_port = 4;
int rdma_num_rails;

int      		rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
unsigned long           rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int      		rdma_default_port = RDMA_DEFAULT_PORT;
unsigned long        	rdma_default_max_wqe = RDMA_DEFAULT_MAX_WQE;
uint32_t        	rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
uint16_t               	rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
uint8_t         	rdma_default_qp_ous_rd_atom = RDMA_DEFAULT_QP_OUS_RD_ATOM;
uint8_t                 rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
enum ibv_mtu       	rdma_default_mtu = RDMA_DEFAULT_MTU;
uint32_t           	rdma_default_psn = RDMA_DEFAULT_PSN;
uint8_t                 rdma_default_min_rnr_timer = RDMA_DEFAULT_MIN_RNR_TIMER;
uint8_t      		rdma_default_service_level = RDMA_DEFAULT_SERVICE_LEVEL;
uint8_t                 rdma_default_static_rate = RDMA_DEFAULT_STATIC_RATE;
uint8_t         	rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;
uint8_t                	rdma_default_time_out = RDMA_DEFAULT_TIME_OUT;
uint8_t                 rdma_default_retry_count = RDMA_DEFAULT_RETRY_COUNT;
uint8_t                 rdma_default_rnr_retry = RDMA_DEFAULT_RNR_RETRY;
int      		rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int			rdma_read_reserve = RDMA_READ_RESERVE;
long    	        rdma_eagersize_1sc = RDMA_EAGERSIZE_1SC;
int                     rdma_put_fallback_threshold = RDMA_PUT_FALLBACK_THRESHOLD;
int                     rdma_get_fallback_threshold = RDMA_GET_FALLBACK_THRESHOLD;
int      		rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
int			num_rdma_buffer	= NUM_RDMA_BUFFER;
#endif
#ifdef ADAPTIVE_RDMA_FAST_PATH
int                     rdma_polling_set_limit = -1;
int                     rdma_polling_set_threshold = 10;
#endif

int                     rdma_iba_eager_threshold = RDMA_IBA_EAGER_THRESHOLD;
char                    rdma_iba_hca[32];
unsigned int            rdma_ndreg_entries = RDMA_NDREG_ENTRIES;

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
#ifdef SRQ
int rdma_credit_preserve        = 100;
#else
int rdma_credit_preserve        = 3;
#endif
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

#ifdef SRQ

uint32_t viadev_srq_size = 512;
uint32_t viadev_srq_limit = 30;
uint32_t viadev_max_r3_oust_send = 32;

#endif


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
int rdma_vbuf_total_size = VBUF_TOTAL_SIZE;

#if defined(ADAPTIVE_RDMA_FAST_PATH)
static inline int log_2(int np)
{
    int lgN, t;

    for (lgN = 0, t = 1; t < np; lgN++, t += t);

    return lgN;
}
#endif

void rdma_init_parameters(int num_proc, int me){
    char *value;

    if ((value = getenv("RDMA_DEFAULT_MTU")) != NULL) {
        if (strcmp(value,"IBV_MTU_256")==0)
            rdma_default_mtu = IBV_MTU_256;
        else if (strcmp(value,"IBV_MTU_512")==0)
            rdma_default_mtu = IBV_MTU_512;
        else if (strcmp(value,"IBV_MTU_1024")==0)
            rdma_default_mtu = IBV_MTU_1024;
        else if (strcmp(value,"IBV_MTU_2048")==0)
            rdma_default_mtu = IBV_MTU_2048;
        else if (strcmp(value,"IBV_MTU_4096")==0)
            rdma_default_mtu = IBV_MTU_4096;
        else
            rdma_default_mtu = IBV_MTU_1024;
    }

    fprintf(stdout,"Number of QPs per port = %d\n", rdma_num_qp_per_port);
    fflush(stdout);
    /* Get number of HCAs/node used by a process */
    if ((value = getenv("NUM_HCAS")) != NULL) {
        rdma_num_hcas = (int)atoi(value);
        if (rdma_num_hcas > MAX_NUM_HCAS) {
            rdma_num_hcas = MAX_NUM_HCAS;
            fprintf(stderr, "Warning, max hca is %d, change %s in ibv_param.h to"
                    "overide the option\n", MAX_NUM_HCAS, "MAX_NUM_HCAS");
        }
    }
    /* Get number of ports/HCA used by a process */
    if ((value = getenv("NUM_PORTS")) != NULL) {
        rdma_num_ports = (int)atoi(value);
        if (rdma_num_ports > MAX_NUM_PORTS) {
            rdma_num_ports = MAX_NUM_PORTS;
            fprintf(stderr, "Warning, max ports per hca is %d, change %s in ibv_param.h to"
                    "overide the option\n", MAX_NUM_PORTS, "MAX_NUM_PORTS");
        }
    }
    /* Get number of qps/port used by a process */
    if ((value = getenv("NUM_QP_PER_PORT")) != NULL) {
        rdma_num_qp_per_port = (int)atoi(value);
        if (rdma_num_qp_per_port > MAX_NUM_QP_PER_PORT) {
            rdma_num_qp_per_port = MAX_NUM_QP_PER_PORT;
            fprintf(stderr, "Warning, max qps per port is %d, change %s in ibv_param.h to"
                    "overide the option\n", MAX_NUM_QP_PER_PORT, "MAX_NUM_QP_PER_PORT");
        }
    }

    rdma_num_qp_per_port = 4;
    if ((value = getenv("RDMA_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_CQ_SIZE")) != NULL) {
        rdma_default_max_cq_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_READ_RESERVE")) != NULL) {
        rdma_read_reserve = (int)atoi(value);
    }
#if defined(RDMA_FAST_PATH) || defined(ADAPTIVE_RDMA_FAST_PATH)
    if ((value = getenv("NUM_RDMA_BUFFER")) != NULL) { 
        num_rdma_buffer = (int)atoi(value);
    }
#endif
#ifdef ADAPTIVE_RDMA_FAST_PATH
    if ((value = getenv("RDMA_POLLING_SET_THRESHOLD")) != NULL) {
        rdma_polling_set_threshold = atoi(value);
    }
    if ((value = getenv("RDMA_POLLING_SET_LIMIT")) != NULL) {
        rdma_polling_set_limit = atoi(value);
        if (rdma_polling_set_limit == -1)
            rdma_polling_set_limit = log_2(num_proc);
    } else {
        rdma_polling_set_limit = num_proc;
    }
#endif
    if ((value = getenv("RDMA_VBUF_TOTAL_SIZE")) != NULL) {
        rdma_vbuf_total_size= atoi(value);
       if (rdma_vbuf_total_size <= 2 * sizeof(int))
           rdma_vbuf_total_size = 2 * sizeof(int);
    }

    if ((value = getenv("VIADEV_SRQ_SIZE")) != NULL) {
        viadev_srq_size = (uint32_t) atoi(value);
    }       

    if ((value = getenv("VIADEV_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);
            
        if(viadev_srq_limit > viadev_srq_size) {
            fprintf(stderr,
                    "SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if ((value = getenv("RDMA_IBA_EAGER_THRESHOLD")) != NULL) {
        rdma_iba_eager_threshold = (int)atoi(value);
    }

    if ((value = getenv("RDMA_INTEGER_POOL_SIZE")) != NULL) {
        rdma_integer_pool_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PUT_GET_LIST_SIZE")) != NULL) {
        rdma_default_put_get_list_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_EAGERSIZE_1SC")) != NULL) {
        rdma_eagersize_1sc = (int)atoi(value);
    }
    if ((value = getenv("RDMA_PUT_FALLBACK_THRESHOLD")) != NULL) {
        rdma_put_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("RDMA_GET_FALLBACK_THRESHOLD")) != NULL) {
        rdma_get_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PORT")) != NULL) {
        rdma_default_port = (int)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_QP_OUS_RD_ATOM")) != NULL) {
        rdma_default_qp_ous_rd_atom = (uint8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (uint8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PSN")) != NULL) {
        rdma_default_psn = (uint32_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PKEY_IX")) != NULL) {
        rdma_default_pkey_ix = (uint16_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        rdma_default_min_rnr_timer = (uint8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_SERVICE_LEVEL")) != NULL) {
        rdma_default_service_level = (uint8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_TIME_OUT")) != NULL) {
        rdma_default_time_out = (uint8_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_STATIC_RATE")) != NULL) {
        rdma_default_static_rate = (uint8_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_SRC_PATH_BITS")) != NULL) {
        rdma_default_src_path_bits = (uint8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_RETRY_COUNT")) != NULL) {
        rdma_default_retry_count = (uint8_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_RNR_RETRY")) != NULL) {
        rdma_default_rnr_retry = (uint8_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_SG_LIST")) != NULL) {
        rdma_default_max_sg_list = (uint32_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_WQE")) != NULL) {
        rdma_default_max_wqe = atol(value);
    }

    strncpy(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32);
    if ((value = getenv("RDMA_IBA_HCA")) != NULL) {
        strncpy(rdma_iba_hca, value, 32);
    }
    if ((value = getenv("RDMA_NDREG_ENTRIES")) != NULL) {
        rdma_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = getenv("RDMA_VBUF_MAX")) != NULL) {
        rdma_vbuf_max = atoi(value);
    }
    if ((value = getenv("RDMA_INITIAL_PREPOST_DEPTH")) != NULL) {
        rdma_initial_prepost_depth = atoi(value);
    }    
    if ((value = getenv("RDMA_PREPOST_DEPTH")) != NULL) {
        rdma_prepost_depth = atoi(value);
    }  
    if ((value = getenv("RDMA_MAX_REGISTERED_PAGES")) != NULL) {
        rdma_max_registered_pages = atol(value);
    }
    if ((value = getenv("RDMA_VBUF_POOL_SIZE")) != NULL) {
	rdma_vbuf_pool_size = atoi(value);
    }
    if ((value = getenv("RDMA_DREG_CACHE_LIMIT")) != NULL) {
	rdma_dreg_cache_limit = atol(value);
    }
    if (rdma_vbuf_pool_size <= 10) {
	rdma_vbuf_pool_size = 10;
	fprintf(stderr, "[%s:%d] Warning! Too small vbuf pool size (%d). "
			"Reset to %d\n", 
			__FILE__, __LINE__, rdma_vbuf_pool_size, 10);
    }
    if ((value = getenv("RDMA_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
	rdma_vbuf_secondary_pool_size = atoi(value);
    }
    if (rdma_vbuf_secondary_pool_size <= 0) {
	rdma_vbuf_secondary_pool_size = 1;
	fprintf(stderr, "[%s:%d] Warning! Too small secondary vbuf pool size (%d). "
                        "Reset to %d\n",
                        __FILE__, __LINE__, rdma_vbuf_secondary_pool_size, 1);
    }
    if (rdma_initial_prepost_depth <= rdma_prepost_noop_extra) {
        rdma_initial_credits = rdma_initial_prepost_depth;
    } else {
        rdma_initial_credits =
            rdma_initial_prepost_depth - rdma_prepost_noop_extra;
    }

    rdma_rq_size = 
        rdma_prepost_depth + rdma_prepost_rendezvous_extra +
        rdma_prepost_noop_extra;
}
