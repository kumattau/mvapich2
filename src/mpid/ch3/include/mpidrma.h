/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

#if !defined(MPICH_MPIDRMA_H_INCLUDED)
#define MPICH_MPIDRMA_H_INCLUDED

/*
 * RMA Declarations.  We should move these into something separate from
 * a Request.
 */
/* to send derived datatype across in RMA ops */
typedef struct MPIDI_RMA_dtype_info { /* for derived datatypes */
    int           is_contig; 
    int           max_contig_blocks;
    int           size;     
    MPI_Aint      extent;   
    int           dataloop_size; /* not needed because this info is sent in packet header. remove it after lock/unlock is implemented in the device */
    void          *dataloop;  /* pointer needed to update pointers
                                 within dataloop on remote side */
    int           dataloop_depth; 
    int           eltype;
    MPI_Aint ub, lb, true_ub, true_lb;
    int has_sticky_ub, has_sticky_lb;
} MPIDI_RMA_dtype_info;

/* for keeping track of RMA ops, which will be executed at the next sync call */
typedef struct MPIDI_RMA_ops {
    struct MPIDI_RMA_ops *next;  /* pointer to next element in list */
    int type;  /* MPIDI_RMA_PUT, MPID_REQUEST_GET,
		  MPIDI_RMA_ACCUMULATE, MPIDI_RMA_LOCK */
    void *origin_addr;
    int origin_count;
    MPI_Datatype origin_datatype;
    int target_rank;
    MPI_Aint target_disp;
    int target_count;
    MPI_Datatype target_datatype;
    MPI_Op op;  /* for accumulate */
    int lock_type;  /* for win_lock */
} MPIDI_RMA_ops;

typedef struct MPIDI_PT_single_op {
    int type;  /* put, get, or accum. */
    void *addr;
    int count;
    MPI_Datatype datatype;
    MPI_Op op;
    void *data;  /* for queued puts and accumulates, data is copied here */
    MPI_Request request_handle;  /* for gets */
    int data_recd;  /* to indicate if the data has been received */
} MPIDI_PT_single_op;

typedef struct MPIDI_Win_lock_queue {
    struct MPIDI_Win_lock_queue *next;
    int lock_type;
    MPI_Win source_win_handle;
    MPIDI_VC_t * vc;
    struct MPIDI_PT_single_op *pt_single_op;  /* to store info for lock-put-unlock optimization */
} MPIDI_Win_lock_queue;

#if defined(_OSU_MVAPICH_)
void MPIDI_CH3I_RDMA_win_create(void *base, MPI_Aint size, int comm_size,
                           int rank, MPID_Win ** win_ptr, MPID_Comm * comm_ptr);
void MPIDI_CH3I_RDMA_win_free(MPID_Win ** win_ptr);
void MPIDI_CH3I_RDMA_start(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
void MPIDI_CH3I_RDMA_complete(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
void MPIDI_CH3I_RDMA_try_rma(MPID_Win * win_ptr, MPIDI_RMA_ops ** MPIDI_RMA_ops_list, int passive);
void MPIDI_CH3I_RDMA_complete_rma(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
int MPIDI_CH3I_RDMA_post(MPID_Win * win_ptr, int target_rank);
int MPIDI_CH3I_RDMA_finish_rma(MPID_Win * win_ptr);
#if defined(_SMP_LIMIC_) && !defined(_DAPL_DEFAULT_PROVIDER_)
void MPIDI_CH3I_LIMIC_win_create(void *base, MPI_Aint size, int comm_size,
                           int rank, MPID_Win ** win_ptr, MPID_Comm * comm_ptr);
void MPIDI_CH3I_LIMIC_start(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
int MPIDI_CH3I_LIMIC_try_rma(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr,
                                   MPI_Win source_win_handle, MPID_Comm *comm_ptr,
                                   int isPut);
#endif /* _SMP_LIMIC_  && !_DAPL_DEFAULT_PROVIDER_ */
#endif /* defined(_OSU_MVAPICH_) */

#endif
