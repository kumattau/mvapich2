/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#ifndef _VAPI_PARAM_H
#define _VAPI_PARAM_H

#include<vapi.h>
#include<evapi.h>
#include<mtl_common.h>
#include<ib_defs.h>
#include <vapi_common.h>
#include "vapi_arch.h"
#include "mpi.h"

extern unsigned long rdma_default_max_cq_size;
extern int rdma_default_port;
extern unsigned long rdma_default_max_wqe;
extern u_int32_t rdma_default_max_sg_list;
extern VAPI_pkey_ix_t rdma_default_pkey_ix;
extern u_int8_t rdma_default_qp_ous_rd_atom;
extern u_int8_t rdma_default_max_rdma_dst_ops;
extern IB_mtu_t rdma_default_mtu;
extern VAPI_psn_t rdma_default_psn;
extern IB_rnr_nak_timer_code_t rdma_default_min_rnr_timer;
extern IB_sl_t rdma_default_service_level;
extern IB_static_rate_t rdma_default_static_rate;
extern u_int8_t rdma_default_src_path_bits;
extern VAPI_timeout_t rdma_default_time_out;
extern VAPI_retry_count_t rdma_default_retry_count;
extern VAPI_retry_count_t rdma_default_rnr_retry;
extern int rdma_default_put_get_list_size;
extern int rdma_read_reserve;
extern float rdma_credit_update_threshold;
extern int num_rdma_buffer;
extern int rdma_iba_eager_threshold;
extern char rdma_iba_default_hca[32];
extern unsigned int vapi_ndreg_entries;
extern int vapi_vbuf_max;
extern int vapi_vbuf_pool_size;
extern int vapi_vbuf_secondary_pool_size;
extern int vapi_initial_prepost_depth;
extern int vapi_prepost_depth;
extern int vapi_prepost_threshold;
extern int vapi_prepost_noop_extra;
extern int vapi_initial_credits;
extern int vapi_prepost_rendezvous_extra;
extern int vapi_dynamic_credit_threshold;
extern int vapi_credit_notify_threshold;
extern int vapi_credit_preserve;
extern int vapi_rq_size;

extern int rdma_num_rails;

#ifdef ONE_SIDED
extern int rdma_pin_pool_size;
extern int rdma_put_fallback_threshold;
extern int rdma_get_fallback_threshold;
extern int rdma_integer_pool_size;
extern int rdma_iba_eager_threshold;
extern long rdma_eagersize_1sc;
#endif

#define RDMA_PIN_POOL_SIZE         (2*1024*1024)        /* for small size message */
#define RDMA_DEFAULT_MAX_CQ_SIZE        (6000)
#define RDMA_DEFAULT_MAX_WQE            (300)
#define RDMA_READ_RESERVE  				(10)
#define RDMA_DEFAULT_MAX_SG_LIST        (20)
#define RDMA_DEFAULT_PKEY_IX            (0)
#define RDMA_DEFAULT_QP_OUS_RD_ATOM     (4)
#define RDMA_DEFAULT_MAX_RDMA_DST_OPS   (4)
#define RDMA_DEFAULT_PSN                (0)
#define RDMA_DEFAULT_MIN_RNR_TIMER      (5)
#define RDMA_DEFAULT_SERVICE_LEVEL      (0)
#define RDMA_DEFAULT_STATIC_RATE        (0)
#define RDMA_DEFAULT_SRC_PATH_BITS      (0)
#define RDMA_DEFAULT_TIME_OUT           (30)
#define RDMA_DEFAULT_HCA_ID				(0)
#define RDMA_DEFAULT_RETRY_COUNT        (7)
#define RDMA_DEFAULT_RNR_RETRY          (7)
#define RDMA_DEFAULT_PUT_GET_LIST_SIZE  (300)
#define RDMA_INTEGER_POOL_SIZE			(1024)
#define MAX_NUM_HCAS                    (1)
#define MAX_SUBCHANNELS                 (1)
#define VAPI_NDREG_ENTRIES              (1100)
#define VAPI_VBUF_POOL_SIZE             (5000)
#define VAPI_VBUF_SECONDARY_POOL_SIZE   (500)
#define VAPI_PREPOST_DEPTH              (80)
#define VAPI_INITIAL_PREPOST_DEPTH      (10)
#define VAPI_LOW_WQE_THRESHOLD          (10)
#define VAPI_MAX_RDMA_SIZE            (1048576)

#define HOSTNAME_LEN    (255)

#if defined(_DDR_)
#define RDMA_DEFAULT_MTU        (MTU2048)
#else
#define RDMA_DEFAULT_MTU	(MTU1024)
#endif


#ifdef _IA32_

    #ifdef _LARGE_CLUSTER
	#define NUM_RDMA_BUFFER                 (16)
	#define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
    #elif defined(_MEDIUM_CLUSTER)
	#define NUM_RDMA_BUFFER                 (16)
	#define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
    #else
	#define NUM_RDMA_BUFFER                 (32) 
	#define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (1 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (394 * 1024)

#elif defined(_EM64T_)

    #ifdef _LARGE_CLUSTER
	#define NUM_RDMA_BUFFER                 (16)
	#if defined _PCI_EX_
	    #define RDMA_IBA_EAGER_THRESHOLD    (4*1024)
	#else
	    #define RDMA_IBA_EAGER_THRESHOLD    (12*1024)
	#endif
    #elif defined(_MEDIUM_CLUSTER)
	#define NUM_RDMA_BUFFER                 (16)
	#if defined _PCI_EX_
	    #define RDMA_IBA_EAGER_THRESHOLD    (4*1024)
	#else
	    #define RDMA_IBA_EAGER_THRESHOLD    (12*1024)
	#endif
    #else
	#define NUM_RDMA_BUFFER                 (32) 
	#define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

#elif defined(_X86_64_)

    #ifdef _LARGE_CLUSTER
	#define NUM_RDMA_BUFFER                 (16)
	#if defined _PCI_EX_
	    #define RDMA_IBA_EAGER_THRESHOLD    (8*1024)
	#else
	     #define RDMA_IBA_EAGER_THRESHOLD    (12*1024)
	#endif
    #elif defined(_MEDIUM_CLUSTER)
	#define NUM_RDMA_BUFFER                 (16)
	#if defined _PCI_EX_
	    #define RDMA_IBA_EAGER_THRESHOLD    (12*1024)
	#else
	    #define RDMA_IBA_EAGER_THRESHOLD    (12*1024)
	#endif
    #else
	#define NUM_RDMA_BUFFER                 (32)
	#define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

#elif defined(MAC_OSX)

    #ifdef _LARGE_CLUSTER
	#define NUM_RDMA_BUFFER                 (32)
	#define RDMA_IBA_EAGER_THRESHOLD        (8*1024)
    #elif defined(_MEDIUM_CLUSTER)
	#define NUM_RDMA_BUFFER                 (32)
	#define RDMA_IBA_EAGER_THRESHOLD        (8*1024)
    #else
	#define NUM_RDMA_BUFFER                 (64)
	#define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (8 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (394 * 1024)

#else

    #ifdef _LARGE_CLUSTER
        #define NUM_RDMA_BUFFER                 (16)
        #define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
    #elif defined(_MEDIUM_CLUSTER)
        #define NUM_RDMA_BUFFER                 (16)
        #define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
    #else
        #define NUM_RDMA_BUFFER                 (32)
        #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (8 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (394 * 1024)

#endif

void rdma_init_parameters(int num_proc, int me);

#define MIN(a,b) ((a)<(b)?(a):(b))

#endif                          /* _VAPI_PARAM_H */
