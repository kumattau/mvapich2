/* Copyright (c) 2003-2006, The Ohio State University. All rights
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
#include<vapi.h>
#include<evapi.h>
#include<mtl_common.h>
#include<ib_defs.h>
#include <vapi_common.h>
#include "vapi_arch.h"
#include "mpi.h"
#include "vapi_param.h"
#include "vapi_header.h"
#include "vbuf.h"
#include "vapi_util.h"

/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */
int      				rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
unsigned long        	rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int      				rdma_default_port = -1;
unsigned long        	rdma_default_max_wqe = RDMA_DEFAULT_MAX_WQE;
u_int32_t        		rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
VAPI_pkey_ix_t       	rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
u_int8_t         		rdma_default_qp_ous_rd_atom = RDMA_DEFAULT_QP_OUS_RD_ATOM;
u_int8_t                rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
IB_mtu_t         		rdma_default_mtu = RDMA_DEFAULT_MTU;
VAPI_psn_t       		rdma_default_psn = RDMA_DEFAULT_PSN;
IB_rnr_nak_timer_code_t      rdma_default_min_rnr_timer = RDMA_DEFAULT_MIN_RNR_TIMER;
IB_sl_t      			rdma_default_service_level = RDMA_DEFAULT_SERVICE_LEVEL;
IB_static_rate_t        rdma_default_static_rate = RDMA_DEFAULT_STATIC_RATE;
u_int8_t         		rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;
VAPI_timeout_t       	rdma_default_time_out = RDMA_DEFAULT_TIME_OUT;
VAPI_retry_count_t      rdma_default_retry_count = RDMA_DEFAULT_RETRY_COUNT;
VAPI_retry_count_t      rdma_default_rnr_retry = RDMA_DEFAULT_RNR_RETRY;
int      				rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int						rdma_read_reserve = RDMA_READ_RESERVE;
long    		        rdma_eagersize_1sc = RDMA_EAGERSIZE_1SC;
int                     rdma_put_fallback_threshold = RDMA_PUT_FALLBACK_THRESHOLD;
int                     rdma_get_fallback_threshold = RDMA_GET_FALLBACK_THRESHOLD;
int      				rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
#ifdef RDMA_FAST_PATH
int						num_rdma_buffer	= NUM_RDMA_BUFFER;
#endif
int                     rdma_iba_eager_threshold = RDMA_IBA_EAGER_THRESHOLD;
char                    rdma_iba_default_hca[32];
unsigned int                     vapi_ndreg_entries = VAPI_NDREG_ENTRIES;

/* max (total) number of vbufs to allocate, after which process
 * terminates with a fatal error.
 * -1 means no limit.
 */
int vapi_vbuf_max = -1;
/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int vapi_vbuf_secondary_pool_size = VAPI_VBUF_SECONDARY_POOL_SIZE;

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int vapi_vbuf_pool_size = VAPI_VBUF_POOL_SIZE;
int vapi_prepost_depth = VAPI_PREPOST_DEPTH;
int vapi_initial_prepost_depth = VAPI_INITIAL_PREPOST_DEPTH;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
int vapi_prepost_noop_extra = 5;

int vapi_credit_preserve = 3;

int vapi_initial_credits; 

/* Max number of entries on the Send Q of QPs per connection.
 * Should be about (prepost_depth + extra).
 * Must be within NIC MaxQpEntries limit.
 * Size will be adjusted below.
 */
int vapi_sq_size = 200;
                                                                                                                                                             
/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + vapi_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
int vapi_rq_size;

/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is vapi_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int vapi_prepost_rendezvous_extra = 10;

int vapi_dynamic_credit_threshold = 10;

int vapi_credit_notify_threshold = 10;

int vapi_prepost_threshold = 5;

void rdma_init_parameters(int num_proc, int me){

    char *value;
	unsigned int num_hcas;
	VAPI_hca_id_t* vapi_device_list;
	VAPI_ret_t ret;
	
    if ((value = getenv("RDMA_IBA_DEFAULT_HCA")) != NULL) {
        strcpy(rdma_iba_default_hca, value);
    } else {
        /* First, to get the number of hcas currently available */
        ret = EVAPI_list_hcas(0, &num_hcas, NULL);

        if ( VAPI_OK == ret ){
            vapi_error_abort(GEN_EXIT_ERR,
                "Error: No HCA Available and found by EVAPI_list_hcas.\n");
        }
        if ( VAPI_EAGAIN != ret ){
            vapi_error_abort(VAPI_RETURN_ERR,
                                "Error: EVAPI_list_hcas: %s\n",
                                VAPI_strerror(ret));
        }

        /* Second,  to get the list of all devices currently available */

        vapi_device_list = malloc(num_hcas * sizeof(VAPI_hca_id_t));
        if ( NULL == vapi_device_list ){
            vapi_error_abort(GEN_EXIT_ERR,
                               "Error:malloc for viadev_device_list.\n");
        }

        ret = EVAPI_list_hcas(num_hcas, &num_hcas, vapi_device_list);
        if ( VAPI_OK != ret ){
            vapi_error_abort(VAPI_RETURN_ERR,
                                "Error: EVAPI_list_hcas : %s\n",
                                VAPI_strerror(ret));
        }
        /* Third,  choose the first one currently available.
         * This is decided by "DEFAULT_HCA_ID" by default.
         */
        strncpy(rdma_iba_default_hca, (char*)&vapi_device_list[RDMA_DEFAULT_HCA_ID],
                sizeof(VAPI_hca_id_t));
	}

    if ((value = getenv("RDMA_DEFAULT_MTU")) != NULL) {
        if (strcmp(value,"MTU256")==0)
            rdma_default_mtu = MTU256;
        else if (strcmp(value,"MTU512")==0)
            rdma_default_mtu = MTU512;
        else if (strcmp(value,"MTU1024")==0)
            rdma_default_mtu = MTU1024;
        else if (strcmp(value,"MTU2048")==0)
            rdma_default_mtu = MTU2048;
        else if (strcmp(value,"MTU4096")==0)
            rdma_default_mtu = MTU4096;
        else
            rdma_default_mtu = MTU1024;
    }
    if ((value = getenv("RDMA_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_CQ_SIZE")) != NULL) {
        rdma_default_max_cq_size = (int)atoi(value);
    }
    if ((value = getenv("RDMA_READ_RESERVE")) != NULL) {
        rdma_read_reserve = (int)atoi(value);
    }
#ifdef RDMA_FAST_PATH
    if ((value = getenv("NUM_RDMA_BUFFER")) != NULL) { 
        num_rdma_buffer = (int)atoi(value);
    }
#endif
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
        rdma_default_qp_ous_rd_atom = (u_int8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (u_int8_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PSN")) != NULL) {
        rdma_default_psn = (VAPI_psn_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_PKEY_IX")) != NULL) {
        rdma_default_pkey_ix = (VAPI_pkey_ix_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        rdma_default_min_rnr_timer = (IB_rnr_nak_timer_code_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_SERVICE_LEVEL")) != NULL) {
        rdma_default_service_level = (IB_sl_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_TIME_OUT")) != NULL) {
        rdma_default_time_out = (VAPI_timeout_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_STATIC_RATE")) != NULL) {
        rdma_default_static_rate = (IB_static_rate_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_SRC_PATH_BITS")) != NULL) {
        rdma_default_src_path_bits = (u_int8_t)atoi(value);
    }
    if ((value = getenv("RDMA_DEFAULT_RETRY_COUNT")) != NULL) {
        rdma_default_retry_count = (VAPI_retry_count_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_RNR_RETRY")) != NULL) {
        rdma_default_rnr_retry = (VAPI_retry_count_t)atol(value);
    }
    if ((value = getenv("RDMA_DEFAULT_MAX_SG_LIST")) != NULL) {
        rdma_default_max_sg_list = (u_int32_t)atol(value);
    }
#if 0
    if ((value = getenv("RDMA_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (u_int8_t)atol(value);
    }
#endif
    if ((value = getenv("RDMA_DEFAULT_MAX_WQE")) != NULL) {
        rdma_default_max_wqe = atol(value);
    }
    if ((value = getenv("VAPI_NDREG_ENTRIES")) != NULL) {
        vapi_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = getenv("VAPI_VBUF_MAX")) != NULL) {
        vapi_vbuf_max = atoi(value);
    }
    if ((value = getenv("VAPI_INITIAL_PREPOST_DEPTH")) != NULL) {
        vapi_initial_prepost_depth = atoi(value);
    }    
    if ((value = getenv("VAPI_PREPOST_DEPTH")) != NULL) {
        vapi_prepost_depth = atoi(value);
    }   
    if (vapi_initial_prepost_depth <= vapi_prepost_noop_extra) {
        vapi_initial_credits = vapi_initial_prepost_depth;
    } else {
        vapi_initial_credits =
            vapi_initial_prepost_depth - vapi_prepost_noop_extra;
    }

    vapi_rq_size = 
        vapi_prepost_depth + vapi_prepost_rendezvous_extra +
        vapi_prepost_noop_extra;


}
