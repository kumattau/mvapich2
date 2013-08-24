/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* Copyright (c) 2001-2013, The Ohio State University. All rights
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

#if defined(_OSU_MVAPICH_) && !defined(_DAPL_DEFAULT_PROVIDER_)
#define MPIDI_CH3I_SHM_win_mutex_lock(win, rank) pthread_mutex_lock(&win->shm_mutex[rank]);
#define MPIDI_CH3I_SHM_win_mutex_unlock(win, rank) pthread_mutex_unlock(&win->shm_mutex[rank]);
#endif

#include "mpl_utlist.h"

typedef enum MPIDI_RMA_Op_type {
    MPIDI_RMA_PUT               = 23,
    MPIDI_RMA_GET               = 24,
    MPIDI_RMA_ACCUMULATE        = 25,
 /* REMOVED: MPIDI_RMA_LOCK     = 26, */
    MPIDI_RMA_ACC_CONTIG        = 27,
    MPIDI_RMA_GET_ACCUMULATE    = 28,
    MPIDI_RMA_COMPARE_AND_SWAP  = 29,
    MPIDI_RMA_FETCH_AND_OP      = 30
} MPIDI_RMA_Op_type_t;

/* Special case RMA operations */

enum MPIDI_RMA_Datatype {
    MPIDI_RMA_DATATYPE_BASIC    = 50,
    MPIDI_RMA_DATATYPE_DERIVED  = 51
};

enum MPID_Lock_state {
    MPID_LOCK_NONE              = 0,
    MPID_LOCK_SHARED_ALL        = 1
};

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
    int           dataloop_size; /* not needed because this info is sent in 
				    packet header. remove it after lock/unlock 
				    is implemented in the device */
    void          *dataloop;  /* pointer needed to update pointers
                                 within dataloop on remote side */
    int           dataloop_depth; 
    int           eltype;
    MPI_Aint ub, lb, true_ub, true_lb;
    int has_sticky_ub, has_sticky_lb;
} MPIDI_RMA_dtype_info;

/* for keeping track of RMA ops, which will be executed at the next sync call */
typedef struct MPIDI_RMA_Op {
    struct MPIDI_RMA_Op *prev;  /* pointer to next element in list */
    struct MPIDI_RMA_Op *next;  /* pointer to next element in list */
    /* FIXME: It would be better to setup the packet that will be sent, at 
       least in most cases (if, as a result of the sync/ops/sync sequence,
       a different packet type is needed, it can be extracted from the 
       information otherwise stored). */
    MPIDI_RMA_Op_type_t type;
    void *origin_addr;
    int origin_count;
    MPI_Datatype origin_datatype;
    int target_rank;
    MPI_Aint target_disp;
    int target_count;
    MPI_Datatype target_datatype;
    MPI_Op op;  /* for accumulate */
    /* Used to complete operations */
    struct MPID_Request *request;
    MPIDI_RMA_dtype_info dtype_info;
    void *dataloop;
    void *result_addr;
    int result_count;
    MPI_Datatype result_datatype;
    void *compare_addr;
    int compare_count;
    MPI_Datatype compare_datatype;
} MPIDI_RMA_Op_t;

typedef struct MPIDI_PT_single_op {
    int type;  /* put, get, or accum. */
    void *addr;
    int count;
    MPI_Datatype datatype;
    MPI_Op op;
    void *data;  /* for queued puts and accumulates, data is copied here */
    MPI_Request request_handle;  /* for gets */
    int data_recd;  /* to indicate if the data has been received */
    MPIDI_CH3_Pkt_flags_t flags;
} MPIDI_PT_single_op;

typedef struct MPIDI_Win_lock_queue {
    struct MPIDI_Win_lock_queue *next;
    int lock_type;
    MPI_Win source_win_handle;
    MPIDI_VC_t * vc;
    struct MPIDI_PT_single_op *pt_single_op;  /* to store info for 
						 lock-put-unlock optimization */
} MPIDI_Win_lock_queue;

#if defined(_OSU_MVAPICH_) && !defined(_DAPL_DEFAULT_PROVIDER_)
typedef struct MPIDI_Win_pending_lock {
   MPID_Win *win_ptr;
   struct MPIDI_Win_pending_lock *next;
} MPIDI_Win_pending_lock_t;

extern MPIDI_Win_pending_lock_t *pending_lock_winlist;
#endif

/* Routine use to tune RMA optimizations */
void MPIDI_CH3_RMA_SetAccImmed( int flag );
#if defined(_OSU_MVAPICH_)
typedef enum {
   /*local process gets the shared memory lock*/
   ACQUIRE_SHARED_LOCK,
   /*Before granting lock to others, local process 
    *  acquire this shared memory lock */
   BLOCK_OTHERS,
   RELEASE_LOCK,
   REMOVE_BLOCK
} shared_memory_lock_flag_t;

void *MPIDI_CH3I_Alloc_mem (size_t size, MPID_Info *info);
void MPIDI_CH3I_Free_mem (void *ptr);
void MPIDI_CH3I_RDMA_win_create(void *base, MPI_Aint size, int comm_size,
                           int rank, MPID_Win ** win_ptr, MPID_Comm * comm_ptr);
void MPIDI_CH3I_RDMA_win_free(MPID_Win ** win_ptr);
void MPIDI_CH3I_RDMA_start(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
void MPIDI_CH3I_RDMA_try_rma(MPID_Win * win_ptr, int passive, int target_rank);
int MPIDI_CH3I_RDMA_post(MPID_Win * win_ptr, int target_rank);
int MPIDI_CH3I_RDMA_complete(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
int MPIDI_CH3I_RDMA_finish_rma(MPID_Win * win_ptr);
#if !defined(_DAPL_DEFAULT_PROVIDER_)
void MPIDI_CH3I_SHM_win_create(void *base, MPI_Aint size, MPID_Win ** win_ptr);
void MPIDI_CH3I_SHM_win_free(MPID_Win ** win_ptr);
int MPIDI_CH3I_SHM_try_rma(MPID_Win * win_ptr, int dest);
int MPIDI_CH3I_SHM_win_lock (int dest_rank, int lock_type_requested, 
                MPID_Win *win_ptr, int blocking, 
                shared_memory_lock_flag_t type);
void MPIDI_CH3I_SHM_win_unlock (int dest_rank, MPID_Win *win_ptr,
                shared_memory_lock_flag_t type);
void MPIDI_CH3I_SHM_win_lock_enqueue (MPID_Win *win_ptr);
int MPIDI_CH3I_Process_locks();
void MPIDI_CH3I_INTRANODE_start(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
void MPIDI_CH3I_INTRANODE_complete(MPID_Win * win_ptr, int start_grp_size, int *ranks_in_win_grp);
#if defined(_SMP_LIMIC_)
void MPIDI_CH3I_LIMIC_win_create(void *base, MPI_Aint size, MPID_Win ** win_ptr);
void MPIDI_CH3I_LIMIC_win_free(MPID_Win** win_ptr);
int MPIDI_CH3I_LIMIC_try_rma(MPID_Win * win_ptr, int dest);
#endif /* _SMP_LIMIC_ */
#endif /* !_DAPL_DEFAULT_PROVIDER_ */
#endif /* defined(_OSU_MVAPICH_) */

/*** RMA OPS LIST HELPER ROUTINES ***/

typedef MPIDI_RMA_Op_t * MPIDI_RMA_Ops_list_t;

/* Return nonzero if the RMA operations list is empty.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_isempty
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_RMA_Ops_isempty(MPIDI_RMA_Ops_list_t *list)
{
    return *list == NULL;
}


/* Return a pointer to the first element in the list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_head
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Op_t *MPIDI_CH3I_RMA_Ops_head(MPIDI_RMA_Ops_list_t *list)
{
    return *list;
}


/* Return a pointer to the last element in the list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_tail
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Op_t *MPIDI_CH3I_RMA_Ops_tail(MPIDI_RMA_Ops_list_t *list)
{
    return (*list) ? (*list)->prev : NULL;
}


/* Append an element to the tail of the RMA ops list
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    elem      Pointer to the element to be appended
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_append
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_append(MPIDI_RMA_Ops_list_t *list,
                                             MPIDI_RMA_Op_t *elem)
{
    MPL_DL_APPEND(*list, elem);
}


/* Allocate a new element on the tail of the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param OUT   new_ptr   Pointer to the element that was allocated
 * @return                MPI error class
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_alloc_tail
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_RMA_Ops_alloc_tail(MPIDI_RMA_Ops_list_t *list,
                                                MPIDI_RMA_Op_t **new_elem)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_Op_t *tmp_ptr;
    MPIU_CHKPMEM_DECL(1);

    /* FIXME: We should use a pool allocator here */
    MPIU_CHKPMEM_MALLOC(tmp_ptr, MPIDI_RMA_Op_t *, sizeof(MPIDI_RMA_Op_t),
                        mpi_errno, "RMA operation entry");

    tmp_ptr->next = NULL;
    tmp_ptr->dataloop = NULL;

    MPL_DL_APPEND(*list, tmp_ptr);

    *new_elem = tmp_ptr;

 fn_exit:
    MPIU_CHKPMEM_COMMIT();
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    *new_elem = NULL;
    goto fn_exit;
}


/* Unlink an element from the RMA ops list
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    elem      Pointer to the element to be unlinked
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_unlink
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_unlink(MPIDI_RMA_Ops_list_t *list,
                                             MPIDI_RMA_Op_t *elem)
{
    MPL_DL_DELETE(*list, elem);
}


/* Free an element in the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param IN    curr_ptr  Pointer to the element to be freed.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free_elem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free_elem(MPIDI_RMA_Ops_list_t *list,
                                                MPIDI_RMA_Op_t *curr_ptr)
{
    MPIDI_RMA_Op_t *tmp_ptr = curr_ptr;

    MPIU_Assert(curr_ptr != NULL);

    MPL_DL_DELETE(*list, curr_ptr);

    /* Check if we allocated a dataloop for this op (see send/recv_rma_msg) */
    if (tmp_ptr->dataloop != NULL)
        MPIU_Free(tmp_ptr->dataloop);
    MPIU_Free( tmp_ptr );
}


/* Free an element in the RMA operations list.
 *
 * @param IN    list      Pointer to the RMA ops list
 * @param INOUT curr_ptr  Pointer to the element to be freed.  Will be updated
 *                        to point to the element following the element that
 *                        was freed.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free_and_next
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free_and_next(MPIDI_RMA_Ops_list_t *list,
                                                    MPIDI_RMA_Op_t **curr_ptr)
{
    MPIDI_RMA_Op_t *next_ptr = (*curr_ptr)->next;

    MPIDI_CH3I_RMA_Ops_free_elem(list, *curr_ptr);
    *curr_ptr = next_ptr;
}


/* Free the entire RMA operations list.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Ops_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void MPIDI_CH3I_RMA_Ops_free(MPIDI_RMA_Ops_list_t *list)
{
    MPIDI_RMA_Op_t *curr_ptr, *tmp_ptr;

    MPL_DL_FOREACH_SAFE(*list, curr_ptr, tmp_ptr) {
        MPIDI_CH3I_RMA_Ops_free_elem(list, curr_ptr);
    }
}


/* Retrieve the RMA ops list pointer from the window.  This routine detects
 * whether we are in an active or passive target epoch and returns the correct
 * ops list; we use a shared list for active target and separate per-target
 * lists for passive target.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMA_Get_ops_list
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPIDI_RMA_Ops_list_t *MPIDI_CH3I_RMA_Get_ops_list(MPID_Win *win_ptr,
                                                                int target)
{
    if (win_ptr->epoch_state == MPIDI_EPOCH_FENCE ||
        win_ptr->epoch_state == MPIDI_EPOCH_START ||
        win_ptr->epoch_state == MPIDI_EPOCH_PSCW  ||
        target == -1)
    {
        return &win_ptr->at_rma_ops_list;
    }
    else {
        return &win_ptr->targets[target].rma_ops_list;
    }
}

#undef FUNCNAME
#undef FCNAME

#endif
