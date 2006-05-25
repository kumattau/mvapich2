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


#ifndef _UDAPL_PARAM_H
#define _UDAPL_PARAM_H

#include "udapl_arch.h"
#include "mpi.h"
#include <dat/udat.h>

extern unsigned long rdma_default_max_cq_size;
extern int rdma_default_port;
extern unsigned long rdma_default_max_wqe;
extern u_int32_t rdma_default_max_sg_list;
extern u_int8_t rdma_default_qp_ous_rd_atom;
extern u_int8_t rdma_default_max_rdma_dst_ops;
extern u_int8_t rdma_default_src_path_bits;
extern int rdma_default_put_get_list_size;
extern int rdma_read_reserve;
extern float rdma_credit_update_threshold;
extern int num_rdma_buffer;
extern int rdma_iba_eager_threshold;
extern unsigned int udapl_ndreg_entries;
extern int udapl_vbuf_max;
extern int udapl_vbuf_pool_size;
extern int udapl_vbuf_secondary_pool_size;
extern int udapl_initial_prepost_depth;
extern int udapl_prepost_depth;
extern int udapl_prepost_threshold;
extern int udapl_prepost_noop_extra;
extern int udapl_initial_credits;
extern int udapl_prepost_rendezvous_extra;
extern int udapl_dynamic_credit_threshold;
extern int udapl_credit_notify_threshold;
extern int udapl_credit_preserve;
extern int udapl_rq_size;
extern DAT_VLEN rdma_default_mtu_size;
extern char dapl_provider[32];

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
#define RDMA_DEFAULT_PORT               (1)
#define RDMA_DEFAULT_MAX_WQE            (300)
#define RDMA_READ_RESERVE  				(10)
#define RDMA_DEFAULT_MAX_SG_LIST        (20)
#define RDMA_DEFAULT_QP_OUS_RD_ATOM     (8)
#define RDMA_DEFAULT_MAX_RDMA_DST_OPS   (8)
#define RDMA_DEFAULT_MTU                (2048)
#define RDMA_DEFAULT_STATIC_RATE        (0)
#define RDMA_DEFAULT_SRC_PATH_BITS      (0)
#define RDMA_DEFAULT_PUT_GET_LIST_SIZE  (300)
#define	RDMA_CREDIT_UPDATE_THRESHOLD	(1.1)
#define RDMA_INTEGER_POOL_SIZE			(1024)

#define MAX_NUM_HCAS                    (1)
#define MAX_SUBCHANNELS                 (1)
#define UDAPL_NDREG_ENTRIES              (1000)
#define UDAPL_VBUF_POOL_SIZE             (5000)
#define UDAPL_VBUF_SECONDARY_POOL_SIZE   (500)
#define UDAPL_PREPOST_DEPTH              (80)
#define UDAPL_INITIAL_PREPOST_DEPTH      (10)
#define UDAPL_LOW_WQE_THRESHOLD          (10)
#define UDAPL_MAX_RDMA_SIZE            (1048576)

#ifndef DAPL_DEFAULT_PROVIDER
#define DAPL_DEFAULT_PROVIDER           "ib0"
#endif

#define DAPL_DEFAULT_MIN_EVD_SIZE            (256)
#define DAPL_DEFAULT_MAX_RDMA_IN        (4)
#define DAPL_DEFAULT_MAX_RDMA_OUT       (4)
#define DAPL_DEFAULT_ASYNC_EVD_SIZE     (8)
#define RDMA_DEFAULT_MAX_WQE_IBA        (300)
#define RDMA_DEFAULT_MAX_WQE_CCIL       (100)
#define RDMA_DEFAULT_MAX_WQE_GM         (8)
#define RDMA_DEFAULT_MAX_WQE_SOLARIS    (300)
#define RDMA_READ_RESERVE_GM            (6)
#define RDMA_DEFAULT_MTU_SIZE           (1024)
#define RDMA_DEFAULT_MTU_SIZE_GM             (4096)
#define RDMA_DEFAULT_MTU_SIZE_CCIL           (65535)
#define RDMA_DEFAULT_MTU_SIZE_SOLARIS        (2048)
#define DAPL_GEN2_MAX_MSG_SIZE               (2147483648)
#define HOSTNAME_LEN    (255)

#if defined(_DDR_)
    #define RDMA_DEFAULT_MTU_SIZE_IBA            (2048)
#else
    #define RDMA_DEFAULT_MTU_SIZE_IBA            (1024)
#endif

#ifdef _IB_GEN2_

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


#elif defined(_IB_VAPI_)

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

#elif defined(SOLARIS)

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

    #define RDMA_EAGERSIZE_1SC              (8 * 1024)
    #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
    #define RDMA_GET_FALLBACK_THRESHOLD     (256 * 1024)

#else
  
  #ifdef _LARGE_CLUSTER
        #define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
  #elif defined(_MEDIUM_CLUSTER)
        #define RDMA_IBA_EAGER_THRESHOLD        (12*1024)
  #else
        #define RDMA_IBA_EAGER_THRESHOLD        (VBUF_BUFFER_SIZE)
  #endif

  #ifdef _IA32_

      #ifdef _SMALL_CLUSTER
          #define NUM_RDMA_BUFFER                 (32)
      #endif
      #ifdef _MEDIUM_CLUSTER
          #define NUM_RDMA_BUFFER                 (16)
      #endif
      #ifdef _LARGE_CLUSTER
          #define NUM_RDMA_BUFFER                 (8)
      #endif

      #define RDMA_EAGERSIZE_1SC              (8 * 1024)
      #define RDMA_PUT_FALLBACK_THRESHOLD     (1 * 1024)
      #define RDMA_GET_FALLBACK_THRESHOLD     (394 * 1024)

  #elif defined(_EM64T_)

      #ifdef _SMALL_CLUSTER
          #define NUM_RDMA_BUFFER                 (40)
      #endif
      #ifdef _MEDIUM_CLUSTER
          #define NUM_RDMA_BUFFER                 (16)
      #endif
      #ifdef _LARGE_CLUSTER
          #define NUM_RDMA_BUFFER                 (8)
      #endif
      
      #define RDMA_EAGERSIZE_1SC              (8 * 1024)
      #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
      #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

  #elif defined(_IA64_)

      #ifdef _SMALL_CLUSTER
          #define NUM_RDMA_BUFFER                 (32)
      #endif
      #ifdef _MEDIUM_CLUSTER
          #define NUM_RDMA_BUFFER                 (16)
      #endif
      #ifdef _LARGE_CLUSTER
          #define NUM_RDMA_BUFFER                 (8)
      #endif

      #define RDMA_EAGERSIZE_1SC              (8 * 1024)
      #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
      #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

  #elif defined(_X86_64_)

      #ifdef _SMALL_CLUSTER
          #define NUM_RDMA_BUFFER                 (32)
      #endif
      #ifdef _MEDIUM_CLUSTER
          #define NUM_RDMA_BUFFER                 (24)
      #endif
      #ifdef _LARGE_CLUSTER
          #define NUM_RDMA_BUFFER                 (16)
      #endif

      #define RDMA_EAGERSIZE_1SC              (8 * 1024)
      #define RDMA_PUT_FALLBACK_THRESHOLD     (2 * 1024)
      #define RDMA_GET_FALLBACK_THRESHOLD     (192 * 1024)

  #elif defined(MAC_OSX)

      #ifdef _SMALL_CLUSTER
          #define NUM_RDMA_BUFFER                 (32)
      #endif
      #ifdef _MEDIUM_CLUSTER
          #define NUM_RDMA_BUFFER                 (16)
      #endif
      #ifdef _LARGE_CLUSTER
          #define NUM_RDMA_BUFFER                 (8)
      #endif

      #define RDMA_EAGERSIZE_1SC              (12 * 1024)
      #define RDMA_PUT_FALLBACK_THRESHOLD     (8 * 1024)
      #define RDMA_GET_FALLBACK_THRESHOLD     (16 * 1024)

  #endif
#endif

void rdma_init_parameters (int num_proc, int me);

#define MIN(a,b) ((a)<(b)?(a):(b))

#endif /* _UDAPL_PARAM_H */
