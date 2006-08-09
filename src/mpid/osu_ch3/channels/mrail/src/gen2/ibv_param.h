
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

#ifndef _IBV_PARAM_H
#define _IBV_PARAM_H

#include "ibv_arch.h"
#include "infiniband/verbs.h"

/* Support multiple QPs/port, multiple ports, multiple HCAs and combinations */
extern int rdma_num_hcas;
extern int rdma_num_ports;
extern int rdma_num_qp_per_port;
extern int rdma_num_rails;

extern unsigned long 	rdma_default_max_cq_size;
extern int 		rdma_default_port;
extern unsigned long 	rdma_default_max_wqe;
extern uint32_t 	rdma_default_max_sg_list;
extern uint16_t 	rdma_default_pkey_ix;
extern uint8_t 	    	rdma_default_qp_ous_rd_atom;
extern uint8_t          rdma_default_max_rdma_dst_ops;
extern enum ibv_mtu 	rdma_default_mtu;
extern uint32_t 	rdma_default_psn;
extern uint8_t   	rdma_default_min_rnr_timer;
extern uint8_t   	rdma_default_service_level;
extern uint8_t   	rdma_default_static_rate;
extern uint8_t 	    	rdma_default_src_path_bits;
extern uint8_t 		rdma_default_time_out;
extern uint8_t 		rdma_default_retry_count;
extern uint8_t 		rdma_default_rnr_retry;
extern int 		rdma_default_put_get_list_size;
extern int		rdma_read_reserve;
extern float 	        rdma_credit_update_threshold;	
extern int		num_rdma_buffer;
extern int              rdma_iba_eager_threshold;
extern char             rdma_iba_hca[32];
extern unsigned int     rdma_ndreg_entries;
extern int              rdma_vbuf_max;
extern int              rdma_vbuf_pool_size;
extern int              rdma_vbuf_secondary_pool_size;
extern int              rdma_initial_prepost_depth;
extern int              rdma_prepost_depth;
extern int              rdma_prepost_threshold;
extern int              rdma_prepost_noop_extra;
extern int              rdma_initial_credits;
extern int              rdma_prepost_rendezvous_extra;
extern int              rdma_dynamic_credit_threshold;
extern int              rdma_credit_notify_threshold;
extern int              rdma_credit_preserve;
extern int              rdma_rq_size;
extern unsigned long	rdma_dreg_cache_limit;
extern int              rdma_vbuf_total_size;

#ifdef SRQ
extern uint32_t             viadev_srq_size;
extern uint32_t             viadev_srq_limit;
extern uint32_t             viadev_max_r3_oust_send;
#endif

#ifdef ADAPTIVE_RDMA_FAST_PATH
extern int rdma_polling_set_threshold;
extern int rdma_polling_set_limit;
#endif

#ifdef ONE_SIDED
extern int                     rdma_pin_pool_size;
extern int                     rdma_put_fallback_threshold;
extern int                     rdma_get_fallback_threshold; 
extern int                     rdma_integer_pool_size;
extern int                     rdma_iba_eager_threshold;
extern long                    rdma_eagersize_1sc;
#endif

#define RDMA_PIN_POOL_SIZE         (2*1024*1024)     /* for small size message */
#define RDMA_DEFAULT_MAX_CQ_SIZE        (40000)
#define RDMA_DEFAULT_PORT               (-1)
#define RDMA_DEFAULT_MAX_PORTS		(2)
#define RDMA_DEFAULT_MAX_WQE            (200)
#define RDMA_READ_RESERVE  		(10)
#define RDMA_DEFAULT_MAX_SG_LIST        (1)
#define RDMA_DEFAULT_PKEY_IX            (0)
#ifdef _PATH_HT_
#define RDMA_DEFAULT_QP_OUS_RD_ATOM     (1)
#else
#define RDMA_DEFAULT_QP_OUS_RD_ATOM     (4)
#endif
#define RDMA_DEFAULT_MAX_RDMA_DST_OPS   (4)
#define RDMA_DEFAULT_PSN                (0)
#define RDMA_DEFAULT_MIN_RNR_TIMER      (12)     /*12 in ib example*/
#define RDMA_DEFAULT_SERVICE_LEVEL      (0)
#define RDMA_DEFAULT_STATIC_RATE        (0)
#define RDMA_DEFAULT_SRC_PATH_BITS      (0)
#define RDMA_DEFAULT_TIME_OUT           (14)
#define RDMA_DEFAULT_RETRY_COUNT        (7)  
#define RDMA_DEFAULT_RNR_RETRY          (7)
#define RDMA_DEFAULT_PUT_GET_LIST_SIZE  (200)
#define RDMA_INTEGER_POOL_SIZE		(1024)
#define RDMA_IBA_NULL_HCA            	"nohca"
#define MAX_NUM_HCAS                    (4)
#define MAX_NUM_PORTS                   (2)
#define MAX_NUM_QP_PER_PORT             (4)
/* This is a overprovision of resource, do not use in critical structures */
#define MAX_NUM_SUBRAILS  (MAX_NUM_HCAS*MAX_NUM_PORTS*MAX_NUM_QP_PER_PORT)
#define RDMA_NDREG_ENTRIES              (1000)
#define RDMA_VBUF_POOL_SIZE             (5000)
#define RDMA_VBUF_SECONDARY_POOL_SIZE   (500)
#define RDMA_PREPOST_DEPTH              (80)
#define RDMA_INITIAL_PREPOST_DEPTH      (10)
#define RDMA_LOW_WQE_THRESHOLD          (10)
#define RDMA_MAX_RDMA_SIZE              (1048576)

#define USE_FIRST                       (0)
#define ROUND_ROBIN                     (1)
#define EVEN_STRIPING                   (2)
#define ADAPTIVE_STRIPING               (3)
/* Inline not supported for PPC */
#ifdef _PPC64_
#define RDMA_MAX_INLINE_SIZE            (-1)
#else
#define RDMA_MAX_INLINE_SIZE            (128)
#endif

#define HOSTNAME_LEN                    (255)
#ifdef ONE_SIDED
#define MAX_WIN_NUM                     (16)
#endif
#define RDMA_MAX_REGISTERED_PAGES       (0)

#if defined(_MLX_PCI_EX_DDR_)
#define RDMA_DEFAULT_MTU                (IBV_MTU_2048)
#else
#define RDMA_DEFAULT_MTU                (IBV_MTU_1024)
#endif

#if defined(_MLX_PCI_EX_SDR_) || defined(_MLX_PCI_EX_DDR_) || defined(_PATH_HT_)
#define _PCI_EX_
#elif defined(_MLX_PCI_X_) || defined (_IBM_EHCA_)
#define _PCI_X_
#else
#define _PCI_X_
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
    #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
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
    #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
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
    #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

#elif defined(_PPC64_)

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
    #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
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
    #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
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
        #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE_DEFAULT)
    #endif

    #define RDMA_EAGERSIZE_1SC              (4 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (8 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (394 * 1024)

#endif

void rdma_init_parameters(int num_proc, int me);

#define MIN(a,b) ((a)<(b)?(a):(b))

#define NUM_BOOTSTRAP_BARRIERS  2

#endif /* _RDMA_PARAM_H */
