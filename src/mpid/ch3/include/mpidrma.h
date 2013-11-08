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

#include "mpl_utlist.h"
#include "mpidi_ch3_impl.h"

#if defined(_OSU_MVAPICH_) && !defined(_DAPL_DEFAULT_PROVIDER_)
#define MPIDI_CH3I_SHM_win_mutex_lock(win, rank) pthread_mutex_lock(&win->shm_win_mutex[rank]);
#define MPIDI_CH3I_SHM_win_mutex_unlock(win, rank) pthread_mutex_unlock(&win->shm_win_mutex[rank]);
#endif

#ifdef USE_MPIU_INSTR
MPIU_INSTR_DURATION_EXTERN_DECL(wincreate_allgather);
MPIU_INSTR_DURATION_EXTERN_DECL(winfree_rs);
MPIU_INSTR_DURATION_EXTERN_DECL(winfree_complete);
#endif

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
int MPIDI_CH3I_RDMA_finish_rma_target(MPID_Win *win_ptr, int target_rank);
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


/* ------------------------------------------------------------------------ */
/*
 * Followings are new routines for origin completion for RMA operations.
 */
/* ------------------------------------------------------------------------ */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_put_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_put_op(const void *origin_addr, int origin_count, MPI_Datatype
                                        origin_datatype, int target_rank, MPI_Aint target_disp,
                                        int target_count, MPI_Datatype target_datatype,
                                        MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    void *base = NULL;
    int disp_unit;
    MPIDI_VC_t *orig_vc, *target_vc;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_PUT_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_PUT_OP);

    /* FIXME: Here we decide whether to perform SHM operations by checking if origin and target are on
       the same node. However, in ch3:sock, even if origin and target are on the same node, they do
       not within the same SHM region. Here we filter out ch3:sock by checking shm_allocated flag first,
       which is only set to TRUE when SHM region is allocated in nemesis.
       In future we need to figure out a way to check if origin and target are in the same "SHM comm".
    */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }

    mpi_errno = MPIR_Localcopy(origin_addr, origin_count, origin_datatype,
                               (char *) base + disp_unit * target_disp,
                               target_count, target_datatype);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_PUT_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_acc_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_acc_op(const void *origin_addr, int origin_count, MPI_Datatype
                                        origin_datatype, int target_rank, MPI_Aint target_disp,
                                        int target_count, MPI_Datatype target_datatype, MPI_Op op,
                                        MPID_Win *win_ptr)
{
    void *base = NULL;
    int disp_unit, shm_op = 0;
    int origin_predefined, target_predefined;
    MPI_User_function *uop = NULL;
    MPID_Datatype *dtp;
    MPIDI_VC_t *orig_vc, *target_vc;
    int mpi_errno = MPI_SUCCESS;
    MPIU_CHKLMEM_DECL(2);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_ACC_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_ACC_OP);

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(origin_datatype, origin_predefined);
    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(target_datatype, target_predefined);

    /* FIXME: refer to FIXME in MPIDI_CH3I_Shm_put_op */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        shm_op = 1;
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    int l_rank = -1;
    if (!win_ptr->shm_fallback) {
        l_rank = win_ptr->shm_g2l_rank[win_ptr->comm_ptr->rank]; 
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_rank);
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
               "**fail %s", "mutex lock error");
        }
    }
#endif

    if (op == MPI_REPLACE)
    {
        if (shm_op) MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        mpi_errno = MPIR_Localcopy(origin_addr, origin_count,
                                   origin_datatype,
                                   (char *) base + disp_unit * target_disp,
                                   target_count, target_datatype);
        if (shm_op) MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
            if (!win_ptr->shm_fallback) {
                mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_rank);
                if (mpi_errno != MPI_SUCCESS) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                            "**fail", "**fail %s", "mutex unlock error");
                }
            }
#endif
        goto fn_exit;
    }

    MPIU_ERR_CHKANDJUMP1((HANDLE_GET_KIND(op) != HANDLE_KIND_BUILTIN),
                         mpi_errno, MPI_ERR_OP, "**opnotpredefined",
                         "**opnotpredefined %d", op );

    /* get the function by indexing into the op table */
    uop = MPIR_OP_HDL_TO_FN(op);

    if (origin_predefined && target_predefined)
    {
        /* Cast away const'ness for origin_address in order to
         * avoid changing the prototype for MPI_User_function */
        if (shm_op) MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        (*uop)((void *) origin_addr, (char *) base + disp_unit*target_disp,
               &target_count, &target_datatype);
        if (shm_op) MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }
    else
    {
        /* derived datatype */

        MPID_Segment *segp;
        DLOOP_VECTOR *dloop_vec;
        MPI_Aint first, last;
        int vec_len, i, type_size, count;
        MPI_Datatype type;
        MPI_Aint true_lb, true_extent, extent;
        void *tmp_buf=NULL, *target_buf;
        const void *source_buf;

        if (origin_datatype != target_datatype)
        {
            /* first copy the data into a temporary buffer with
               the same datatype as the target. Then do the
               accumulate operation. */

            MPIR_Type_get_true_extent_impl(target_datatype, &true_lb, &true_extent);
            MPID_Datatype_get_extent_macro(target_datatype, extent);

            MPIU_CHKLMEM_MALLOC(tmp_buf, void *,
                                target_count * (MPIR_MAX(extent,true_extent)),
                                mpi_errno, "temporary buffer");
            /* adjust for potential negative lower bound in datatype */
            tmp_buf = (void *)((char*)tmp_buf - true_lb);

            mpi_errno = MPIR_Localcopy(origin_addr, origin_count,
                                       origin_datatype, tmp_buf,
                                       target_count, target_datatype);
            if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        }

        if (target_predefined) {
            /* target predefined type, origin derived datatype */

            if (shm_op) MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
            (*uop)(tmp_buf, (char *) base + disp_unit * target_disp,
                   &target_count, &target_datatype);
            if (shm_op) MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        }
        else {

            segp = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((!segp), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem","**nomem %s","MPID_Segment_alloc");
            MPID_Segment_init(NULL, target_count, target_datatype, segp, 0);
            first = 0;
            last  = SEGMENT_IGNORE_LAST;

            MPID_Datatype_get_ptr(target_datatype, dtp);
            vec_len = dtp->max_contig_blocks * target_count + 1;
            /* +1 needed because Rob says so */
            MPIU_CHKLMEM_MALLOC(dloop_vec, DLOOP_VECTOR *,
                                vec_len * sizeof(DLOOP_VECTOR),
                                mpi_errno, "dloop vector");

            MPID_Segment_pack_vector(segp, first, &last, dloop_vec, &vec_len);

            source_buf = (tmp_buf != NULL) ? tmp_buf : origin_addr;
            target_buf = (char *) base + disp_unit * target_disp;
            type = dtp->eltype;
            type_size = MPID_Datatype_get_basic_size(type);
            if (shm_op) MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
            for (i=0; i<vec_len; i++)
            {
                count = (dloop_vec[i].DLOOP_VECTOR_LEN)/type_size;
                (*uop)((char *)source_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                       (char *)target_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                       &count, &type);
            }
            if (shm_op) MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);

            MPID_Segment_free(segp);
        }
    }

#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    if (!win_ptr->shm_fallback) {
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_rank);
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "mutex unlock error");
        }
    }
#endif

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_ACC_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (shm_op) MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_get_acc_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_get_acc_op(const void *origin_addr, int origin_count, MPI_Datatype
                                            origin_datatype, void *result_addr, int result_count,
                                            MPI_Datatype result_datatype, int target_rank, MPI_Aint
                                            target_disp, int target_count, MPI_Datatype target_datatype,
                                            MPI_Op op, MPID_Win *win_ptr)
{
    int disp_unit, shm_locked = 0;
    void *base = NULL;
    MPI_User_function *uop = NULL;
    MPID_Datatype *dtp;
    int origin_predefined, target_predefined;
    MPIDI_VC_t *orig_vc, *target_vc;
    int mpi_errno = MPI_SUCCESS;
    MPIU_CHKLMEM_DECL(2);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_GET_ACC_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_GET_ACC_OP);

    origin_predefined = TRUE; /* quiet uninitialized warnings (b/c goto) */
    if (op != MPI_NO_OP) {
        MPIDI_CH3I_DATATYPE_IS_PREDEFINED(origin_datatype, origin_predefined);
    }
    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(target_datatype, target_predefined);

    /* FIXME: refer to FIXME in MPIDI_CH3I_Shm_put_op */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];
        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        shm_locked = 1;
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }

    /* Perform the local get first, then the accumulate */
    mpi_errno = MPIR_Localcopy((char *) base + disp_unit * target_disp,
                               target_count, target_datatype,
                               result_addr, result_count, result_datatype);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

    /* NO_OP: Don't perform the accumulate */
    if (op == MPI_NO_OP) {
        if (shm_locked) {
            MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
            shm_locked = 0;
        }

        goto fn_exit;
    }

    if (op == MPI_REPLACE) {
        mpi_errno = MPIR_Localcopy(origin_addr, origin_count, origin_datatype,
                                   (char *) base + disp_unit * target_disp,
                                   target_count, target_datatype);

        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

        if (shm_locked) {
            MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
            shm_locked = 0;
        }

        goto fn_exit;
    }

    MPIU_ERR_CHKANDJUMP1((HANDLE_GET_KIND(op) != HANDLE_KIND_BUILTIN),
                          mpi_errno, MPI_ERR_OP, "**opnotpredefined",
                          "**opnotpredefined %d", op );

    /* get the function by indexing into the op table */
    uop = MPIR_OP_HDL_TO_FN(op);

    if (origin_predefined && target_predefined) {
        /* Cast away const'ness for origin_address in order to
         * avoid changing the prototype for MPI_User_function */
        (*uop)((void *) origin_addr, (char *) base + disp_unit*target_disp,
               &target_count, &target_datatype);
    }
    else {
        /* derived datatype */

        MPID_Segment *segp;
        DLOOP_VECTOR *dloop_vec;
        MPI_Aint first, last;
        int vec_len, i, type_size, count;
        MPI_Datatype type;
        MPI_Aint true_lb, true_extent, extent;
        void *tmp_buf=NULL, *target_buf;
        const void *source_buf;

        if (origin_datatype != target_datatype) {
            /* first copy the data into a temporary buffer with
               the same datatype as the target. Then do the
               accumulate operation. */

            MPIR_Type_get_true_extent_impl(target_datatype, &true_lb, &true_extent);
            MPID_Datatype_get_extent_macro(target_datatype, extent);

            MPIU_CHKLMEM_MALLOC(tmp_buf, void *,
                                target_count * (MPIR_MAX(extent,true_extent)),
                                mpi_errno, "temporary buffer");
            /* adjust for potential negative lower bound in datatype */
            tmp_buf = (void *)((char*)tmp_buf - true_lb);

            mpi_errno = MPIR_Localcopy(origin_addr, origin_count,
                                       origin_datatype, tmp_buf,
                                       target_count, target_datatype);
            if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        }

        if (target_predefined) {
            /* target predefined type, origin derived datatype */

            (*uop)(tmp_buf, (char *) base + disp_unit * target_disp,
                   &target_count, &target_datatype);
        }
        else {

            segp = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((!segp), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem","**nomem %s","MPID_Segment_alloc");
            MPID_Segment_init(NULL, target_count, target_datatype, segp, 0);
            first = 0;
            last  = SEGMENT_IGNORE_LAST;

            MPID_Datatype_get_ptr(target_datatype, dtp);
            vec_len = dtp->max_contig_blocks * target_count + 1;
            /* +1 needed because Rob says so */
            MPIU_CHKLMEM_MALLOC(dloop_vec, DLOOP_VECTOR *,
                                vec_len * sizeof(DLOOP_VECTOR),
                                mpi_errno, "dloop vector");

            MPID_Segment_pack_vector(segp, first, &last, dloop_vec, &vec_len);

            source_buf = (tmp_buf != NULL) ? tmp_buf : origin_addr;
            target_buf = (char *) base + disp_unit * target_disp;
            type = dtp->eltype;
            type_size = MPID_Datatype_get_basic_size(type);

            for (i=0; i<vec_len; i++) {
                count = (dloop_vec[i].DLOOP_VECTOR_LEN)/type_size;
                (*uop)((char *)source_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                       (char *)target_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                       &count, &type);
            }

            MPID_Segment_free(segp);
        }
    }

    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        shm_locked = 0;
    }

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_GET_ACC_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_get_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_get_op(void *origin_addr, int origin_count, MPI_Datatype origin_datatype,
                                        int target_rank, MPI_Aint target_disp, int target_count,
                                        MPI_Datatype target_datatype, MPID_Win *win_ptr)
{
    void *base = NULL;
    int disp_unit;
    MPIDI_VC_t *orig_vc, *target_vc;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_GET_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_GET_OP);

    /* FIXME: refer to FIXME in MPIDI_CH3I_Shm_put_op */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }

    mpi_errno = MPIR_Localcopy((char *) base + disp_unit * target_disp,
                               target_count, target_datatype, origin_addr,
                               origin_count, origin_datatype);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_GET_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_cas_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_cas_op(const void *origin_addr, const void *compare_addr,
                                        void *result_addr, MPI_Datatype datatype, int target_rank,
                                        MPI_Aint target_disp, MPID_Win *win_ptr)
{
    void *base = NULL, *dest_addr = NULL;
    int disp_unit;
    int len, shm_locked = 0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t *orig_vc, *target_vc;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_CAS_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_CAS_OP);

    /* FIXME: refer to FIXME in MPIDI_CH3I_Shm_put_op */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];

        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        shm_locked = 1;
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }

    dest_addr = (char *) base + disp_unit * target_disp;

    MPID_Datatype_get_size_macro(datatype, len);
    MPIU_Memcpy(result_addr, dest_addr, len);

    if (MPIR_Compare_equal(compare_addr, dest_addr, datatype)) {
        MPIU_Memcpy(dest_addr, origin_addr, len);
    }

    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        shm_locked = 0;
    }

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_CAS_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Shm_fop_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Shm_fop_op(const void *origin_addr, void *result_addr,
                                        MPI_Datatype datatype, int target_rank,
                                        MPI_Aint target_disp, MPI_Op op, MPID_Win *win_ptr)
{
    void *base = NULL, *dest_addr = NULL;
    MPI_User_function *uop = NULL;
    int disp_unit;
    int len, one, shm_locked = 0;
    MPIDI_VC_t *orig_vc, *target_vc;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHM_FOP_OP);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHM_FOP_OP);

    /* FIXME: refer to FIXME in MPIDI_CH3I_Shm_put_op */
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, win_ptr->comm_ptr->rank, &orig_vc);
    MPIDI_Comm_get_vc(win_ptr->comm_ptr, target_rank, &target_vc);
    if (win_ptr->shm_allocated == TRUE && orig_vc->node_id == target_vc->node_id) {
        base = win_ptr->shm_base_addrs[target_rank];
        disp_unit = win_ptr->disp_units[target_rank];

        MPIDI_CH3I_SHM_MUTEX_LOCK(win_ptr);
        shm_locked = 1;
    }
    else {
        base = win_ptr->base;
        disp_unit = win_ptr->disp_unit;
    }

    dest_addr = (char *) base + disp_unit * target_disp;

    MPID_Datatype_get_size_macro(datatype, len);
    MPIU_Memcpy(result_addr, dest_addr, len);

    uop = MPIR_OP_HDL_TO_FN(op);
    one = 1;

    (*uop)((void *) origin_addr, dest_addr, &one, &datatype);

    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
        shm_locked = 0;
    }

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHM_FOP_OP);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (shm_locked) {
        MPIDI_CH3I_SHM_MUTEX_UNLOCK(win_ptr);
    }
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Wait_for_pt_ops_finish
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_Wait_for_pt_ops_finish(MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS, total_pt_rma_puts_accs;
    MPID_Comm *comm_ptr;
    int errflag = FALSE;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_WAIT_FOR_PT_OPS_FINISH);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_WAIT_FOR_PT_OPS_FINISH);

    comm_ptr = win_ptr->comm_ptr;
    MPIU_INSTR_DURATION_START(winfree_rs);
    mpi_errno = MPIR_Reduce_scatter_block_impl(win_ptr->pt_rma_puts_accs,
                                               &total_pt_rma_puts_accs, 1,
                                               MPI_INT, MPI_SUM, comm_ptr, &errflag);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    MPIU_ERR_CHKANDJUMP(errflag, mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    MPIU_INSTR_DURATION_END(winfree_rs);

    if (total_pt_rma_puts_accs != win_ptr->my_pt_rma_puts_accs)
    {
	MPID_Progress_state progress_state;

	/* poke the progress engine until the two are equal */
	MPIU_INSTR_DURATION_START(winfree_complete);
	MPID_Progress_start(&progress_state);
	while (total_pt_rma_puts_accs != win_ptr->my_pt_rma_puts_accs)
	{
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
	    }
	    /* --END ERROR HANDLING-- */
	}
	MPID_Progress_end(&progress_state);
	MPIU_INSTR_DURATION_END(winfree_complete);
    }

 fn_exit:
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_WAIT_FOR_PT_OPS_FINISH);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#undef FCNAME

#endif
