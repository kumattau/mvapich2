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

#include "udapl_arch.h"
#include "mpi.h"
#include "udapl_param.h"
#include "udapl_header.h"
#include "vbuf.h"
#include "rdma_impl.h"
#include <string.h>
#include DAT_HEADER

/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */
int rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
u_int32_t rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
u_int8_t rdma_default_qp_ous_rd_atom = RDMA_DEFAULT_QP_OUS_RD_ATOM;
u_int8_t rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
u_int8_t rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;

unsigned long rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int rdma_default_port = RDMA_DEFAULT_PORT;
unsigned long rdma_default_max_send_wqe = RDMA_DEFAULT_MAX_SEND_WQE;
unsigned long rdma_default_max_recv_wqe = RDMA_DEFAULT_MAX_RECV_WQE;
DAT_VLEN rdma_default_mtu_size = RDMA_DEFAULT_MTU_SIZE;
int rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int rdma_read_reserve = RDMA_READ_RESERVE;
long rdma_eagersize_1sc = RDMA_EAGERSIZE_1SC;
int rdma_put_fallback_threshold = RDMA_PUT_FALLBACK_THRESHOLD;
int rdma_get_fallback_threshold = RDMA_GET_FALLBACK_THRESHOLD;
float rdma_credit_update_threshold = RDMA_CREDIT_UPDATE_THRESHOLD;
int rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
int num_rdma_buffer = NUM_RDMA_BUFFER;
int rdma_iba_eager_threshold = RDMA_IBA_EAGER_THRESHOLD;
char dapl_provider[32] = DAPL_DEFAULT_PROVIDER;

/* max (total) number of vbufs to allocate, after which process
 * terminates with a fatal error.
 * -1 means no limit.
 */
int udapl_vbuf_max = -1;
/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int udapl_vbuf_secondary_pool_size = UDAPL_VBUF_SECONDARY_POOL_SIZE;

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int udapl_vbuf_pool_size = UDAPL_VBUF_POOL_SIZE;
int udapl_prepost_depth = UDAPL_PREPOST_DEPTH;
int udapl_initial_prepost_depth = UDAPL_INITIAL_PREPOST_DEPTH;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
int udapl_prepost_noop_extra = 8;

int udapl_credit_preserve = 5;

int udapl_initial_credits;

/* Max number of entries on the Send Q of QPs per connection.
 * Should be about (prepost_depth + extra).
 * Must be within NIC MaxQpEntries limit.
 * Size will be adjusted below.
 */
int udapl_sq_size = 200;

/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + udapl_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
int udapl_rq_size;

/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is udapl_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int udapl_prepost_rendezvous_extra = 10;

int udapl_dynamic_credit_threshold = 10;

int udapl_credit_notify_threshold = 10;

int udapl_prepost_threshold = 5;

int rdma_num_rails = 1;

int rdma_num_hcas = 1;

unsigned int  rdma_ndreg_entries = RDMA_NDREG_ENTRIES;
unsigned long rdma_dreg_cache_limit = 0;

void
rdma_init_parameters (MPIDI_CH3I_RDMA_Process_t *proc)
{
    char* value = NULL;

    if ((value = (char *) getenv ("MV2_DAPL_PROVIDER")) != NULL)
      {
          strcpy (dapl_provider, value);
      }

    if (strcmp (dapl_provider, "ib0") == 0)
      {
          rdma_default_mtu_size = RDMA_DEFAULT_MTU_SIZE_IBA;
      }
    else if (strcmp (dapl_provider, "ccil") == 0)
      {
          rdma_default_mtu_size = RDMA_DEFAULT_MTU_SIZE_CCIL;
          rdma_get_fallback_threshold = 131072;
          rdma_put_fallback_threshold = 32768;
          rdma_iba_eager_threshold = 65536;
          udapl_prepost_depth = 50;
      }
    else if (strcmp (dapl_provider, "gmg2") == 0)
      {
          rdma_default_mtu_size = RDMA_DEFAULT_MTU_SIZE_GM;
          rdma_read_reserve = RDMA_READ_RESERVE_GM;
          rdma_put_fallback_threshold = 16384;
      }
    else if (strcmp (dapl_provider, "ibd0") == 0)
      {
          rdma_default_mtu_size = RDMA_DEFAULT_MTU_SIZE_SOLARIS;
      }

    if ((value = getenv("MV2_DEFAULT_MAX_SEND_WQE")) != NULL) {
        rdma_default_max_send_wqe = atol(value);
    }

    if ((value = getenv("MV2_DEFAULT_MAX_RECV_WQE")) != NULL) {
        rdma_default_max_recv_wqe = atol(value);
    }

    if ((value = (char *) getenv ("MV2_DEFAULT_MTU")) != NULL)
      {
          rdma_default_mtu_size = (int) atoi (value);
      }

    if ((value = (char *) getenv ("MV2_PIN_POOL_SIZE")) != NULL)
      {
          rdma_pin_pool_size = (int) atoi (value);
      }
    if ((value = getenv("MV2_DREG_CACHE_LIMIT")) != NULL) {
        rdma_dreg_cache_limit = atol(value);
    }
    if ((value = (char *) getenv ("MV2_DEFAULT_MAX_CQ_SIZE")) != NULL)
      {
          rdma_default_max_cq_size = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_READ_RESERVE")) != NULL)
      {
          rdma_read_reserve = (int) atoi (value);
      }

#if !defined(DISABLE_PTMALLOC) || defined(SOLARIS)
    proc->has_lazy_mem_unregister = (value = getenv("MV2_USE_LAZY_MEM_UNREGISTER")) != NULL ? !!atoi(value) : 1;
#endif /* !defined(DISABLE_PTMALLOC) || defined(SOLARIS) */

    if ((value = (char *) getenv ("MV2_USE_RDMA_FAST_PATH")) != NULL)
    {
        proc->has_rdma_fast_path = (int) atoi (value);
    }

    proc->has_one_sided = (value = getenv("MV2_USE_RDMA_ONE_SIDED")) != NULL ? !!atoi(value) : 1;

    if ((value = (char *) getenv ("MV2_NUM_RDMA_BUFFER")) != NULL)
      {
          num_rdma_buffer = (int) atoi (value);
      }

    if ((value = (char *) getenv ("MV2_IBA_EAGER_THRESHOLD")) != NULL)
      {
          rdma_iba_eager_threshold = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_CREDIT_UPDATE_THRESHOLD")) != NULL)
      {
          rdma_credit_update_threshold = (float) atof (value);
      }

    if ((value = (char *) getenv ("MV2_INTEGER_POOL_SIZE")) != NULL)
      {
          rdma_integer_pool_size = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_PUT_GET_LIST_SIZE")) != NULL)
      {
          rdma_default_put_get_list_size = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_EAGERSIZE_1SC")) != NULL)
      {
          rdma_eagersize_1sc = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_PUT_FALLBACK_THRESHOLD")) != NULL)
      {
          rdma_put_fallback_threshold = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_GET_FALLBACK_THRESHOLD")) != NULL)
      {
          rdma_get_fallback_threshold = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_PORT")) != NULL)
      {
          rdma_default_port = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_QP_OUS_RD_ATOM")) != NULL)
      {
          rdma_default_qp_ous_rd_atom = (u_int8_t) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_MAX_RDMA_DST_OPS")) != NULL)
      {
          rdma_default_max_rdma_dst_ops = (u_int8_t) atol (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_SRC_PATH_BITS")) != NULL)
      {
          rdma_default_src_path_bits = (u_int8_t) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_DEFAULT_MAX_SG_LIST")) != NULL)
      {
          rdma_default_max_sg_list = (u_int32_t) atol (value);
      }
    if ((value = getenv("MV2_NDREG_ENTRIES")) != NULL) {
        rdma_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = (char *) getenv ("MV2_VBUF_MAX")) != NULL)
      {
          udapl_vbuf_max = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_INITIAL_PREPOST_DEPTH")) != NULL)
      {
          udapl_initial_prepost_depth = (int) atoi (value);
      }
    if ((value = (char *) getenv ("MV2_PREPOST_DEPTH")) != NULL)
      {
          udapl_prepost_depth = (int) atoi (value);
      }
    
    udapl_initial_credits = udapl_initial_prepost_depth <= udapl_prepost_noop_extra ?
        udapl_initial_prepost_depth
        : udapl_initial_prepost_depth - udapl_prepost_noop_extra;

    udapl_rq_size =
        udapl_prepost_depth + udapl_prepost_rendezvous_extra +
        udapl_prepost_noop_extra;
}
