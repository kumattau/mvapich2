/* Copyright (c) 2002-2006, The Ohio State University. All rights
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

#include "rdma_impl.h"

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);  fflush(stderr);                   \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

struct ibv_mr * register_memory(void * buf, int len)
{
    struct ibv_mr * mr = ibv_reg_mr(MPIDI_CH3I_RDMA_Process.ptag[0], buf, len,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ );
    DEBUG_PRINT("register return mr %p, buf %p, len %d\n", mr, buf, len);
    return mr;
}

int deregister_memory(struct ibv_mr * mr)
{
    int ret;

    ret = ibv_dereg_mr(mr);
    DEBUG_PRINT("deregister mr %p, ret %d\n", mr, ret);
    return ret;
}


