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
#ifdef ONE_SIDED
#include <math.h>

#include "rdma_impl.h"
#include "ibv_priv.h"
#include "dreg.h"
#include "ibv_param.h"
#include "infiniband/verbs.h"

#include "pmi.h"

#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

#define ASSERT assert

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#ifdef ONE_SIDED

extern int number_of_op;

static int Decrease_CC(MPID_Win *, int);
static int POST_PUT_PUT_GET_LIST(MPID_Win *, int, dreg_entry *, 
            MPIDI_VC_t *, struct ibv_send_wr *);
static int POST_GET_PUT_GET_LIST(MPID_Win *, int, void *, void *, 
            dreg_entry *, MPIDI_VC_t *, struct ibv_send_wr *);
static int Consume_signals(MPID_Win *, uint64_t);
static int IBA_PUT(MPIDI_RMA_ops *, MPID_Win *, int);
static int IBA_GET(MPIDI_RMA_ops *, MPID_Win *, int);

static void Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size);

#ifdef _FOO_
static int IBA_ACCUMULATE(MPIDI_RMA_ops *, MPID_Win *, int);
static int iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
static int iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);
static void MPIDI_CH3I_RDMA_lock(MPID_Win * win_ptr,
                          int target_rank, int lock_type, int blocking);
#endif

static inline int Find_Avail_Index()
{
    int i, index = -1;
    for (i = 0; i < MAX_WIN_NUM; i++) {
        if (MPIDI_CH3I_RDMA_Process.win_index2address[i] == 0) {
            index = i;
            break;
        }
    }
    return index;
}

static inline int Find_Win_Index(MPID_Win * win_ptr)
{
    int i, index = -1;
    for (i = 0; i < MAX_WIN_NUM; i++) {
        if (MPIDI_CH3I_RDMA_Process.win_index2address[i] == (long) win_ptr) {
            index = i;
            break;
        }
    }
    return index;
}

static inline void Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size)
{
    if (win_ptr->pinnedpool_1sc_index + size >=
        rdma_pin_pool_size) {
        Consume_signals(win_ptr, 0);
        *origin = win_ptr->pinnedpool_1sc_buf;
        win_ptr->pinnedpool_1sc_index = size;
    } else {
        *origin =
            win_ptr->pinnedpool_1sc_buf +
            win_ptr->pinnedpool_1sc_index;
        win_ptr->pinnedpool_1sc_index += size;
    }
}


/* For active synchronization, it is a blocking call*/
void
MPIDI_CH3I_RDMA_start(MPID_Win * win_ptr,
                      int start_grp_size, int *ranks_in_win_grp)
{
#ifdef _SMP_
    MPIDI_VC_t *vc;
    MPID_Comm * comm_ptr;
#endif
    int flag = 0, src, i;

#ifdef _SMP_
    MPID_Comm_get_ptr( win_ptr->comm, comm_ptr );
#endif
    while (flag == 0 && start_grp_size != 0) {
        for (i = 0; i < start_grp_size; i++) {
            flag = 1;
            src = ranks_in_win_grp[i];  /*src is the rank in comm*/
#ifdef _SMP_
            MPIDI_Comm_get_vc(comm_ptr, src, &vc);
            if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
#else 
            if (win_ptr->post_flag[src] == 0) 
#endif
            {
                /*correspoding post has not been issued */
                flag = 0;
                break;
            }
        }                       /* end of for loop  */
    }                           /* end of while loop */
}

/* For active synchronization, if all rma operation has completed, we issue a RDMA
write operation with fence to update the remote flag in target processes*/
void
MPIDI_CH3I_RDMA_complete_rma(MPID_Win * win_ptr,
                         int start_grp_size, int *ranks_in_win_grp,
                         int send_complete)
{
    int i, target, dst;
    int comm_size;
    int *nops_to_proc;
    int mpi_errno;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops *curr_ptr;
#ifdef _SMP_
    MPIDI_VC_t *vc;
#endif

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    comm_size = comm_ptr->local_size;

    nops_to_proc = (int *) MPIU_Calloc(comm_size, sizeof(int));
    /* --BEGIN ERROR HANDLING-- */
    if (!nops_to_proc) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* set rma_target_proc[i] to 1 if rank i is a target of RMA
       ops from this process */
    for (i = 0; i < comm_size; i++)
        nops_to_proc[i] = 0;
    curr_ptr = win_ptr->rma_ops_list;
    while (curr_ptr != NULL) {
        nops_to_proc[curr_ptr->target_rank]++;
        curr_ptr = curr_ptr->next;
    }
    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; i++) {
        dst = ranks_in_win_grp[i];
        win_ptr->post_flag[dst] = 0;
    }
    win_ptr->using_start = 0;

    for (i = 0; i < start_grp_size; i++) {
        target = ranks_in_win_grp[i];   /* target is the rank is comm */
#ifdef _SMP_
        MPIDI_Comm_get_vc(comm_ptr, target, &vc);
        if (nops_to_proc[target] == 0 && send_complete == 1 
            && vc->smp.local_nodes == -1)
#else
        if (nops_to_proc[target] == 0 && send_complete == 1) 
#endif
        {
            Decrease_CC(win_ptr, target);
            if (win_ptr->wait_for_complete == 1) {
                MPIDI_CH3I_RDMA_finish_rma(win_ptr);
            }
        }
    }
  fn_exit:
    return;
}

/* Waiting for all the completion signals and unregister buffers*/
int MPIDI_CH3I_RDMA_finish_rma(MPID_Win * win_ptr)
{
    if (win_ptr->put_get_list_size != 0) {
        return Consume_signals(win_ptr, 0);
    }
    else return 0;
}


/* Go through RMA op list once, and start as many RMA ops as possible */
void
MPIDI_CH3I_RDMA_try_rma(MPID_Win * win_ptr,
                        MPIDI_RMA_ops ** MPIDI_RMA_ops_list, int passive)
{
    MPIDI_RMA_ops *curr_ptr, *head_ptr = NULL, *prev_ptr =
        NULL, *tmp_ptr;
    int size, origin_type_size, target_type_size;
    int tag = 0;
#ifdef _SCHEDULE
    int curr_put = 1;
    int fall_back = 0;
    int force_to_progress = 0, issued = 0;
    MPIDI_RMA_ops * skipped_op = NULL;
#endif
#ifdef _SMP_
    MPIDI_VC_t * vc;
    MPID_Comm * comm_ptr;

    MPID_Comm_get_ptr( win_ptr->comm, comm_ptr );
#endif

    prev_ptr = curr_ptr = head_ptr = *MPIDI_RMA_ops_list;
    if (*MPIDI_RMA_ops_list != NULL) tag = 1;

#ifdef _SCHEDULE
    while (curr_ptr != NULL || skipped_op != NULL) {
        if (curr_ptr == NULL && skipped_op != NULL) {
            curr_ptr = skipped_op;
            skipped_op = NULL;
            fall_back ++;
            if (issued == 0) {
                force_to_progress = 1;
            } else {
                force_to_progress = 0;
                issued = 0;
            }
        }
#else
    while (curr_ptr != NULL) {
#endif
#ifdef _SMP_
        MPIDI_Comm_get_vc(comm_ptr, curr_ptr->target_rank, &vc);

        if (vc->smp.local_nodes != -1)  {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }
#endif
        if (passive == 0 && win_ptr->post_flag[curr_ptr->target_rank] == 1
            && win_ptr->using_lock == 0
            ) {
            switch (curr_ptr->type) {
            case (MPIDI_RMA_PUT):
                {
                    int origin_dt_derived, target_dt_derived;

                    if (HANDLE_GET_KIND(curr_ptr->origin_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        origin_dt_derived = 1;
                    else
                        origin_dt_derived = 0;
                    if (HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        target_dt_derived = 1;
                    else
                        target_dt_derived = 0;
    
                    MPID_Datatype_get_size_macro(curr_ptr->origin_datatype,
                                 origin_type_size);
                    size = curr_ptr->origin_count * origin_type_size;
 
                    if (!origin_dt_derived && !target_dt_derived && size > rdma_put_fallback_threshold) {
#ifdef _SCHEDULE
                        if (curr_put != 1 || force_to_progress == 1)  { 
                        /* nearest issued rma is not a put*/
#endif
                            win_ptr->rma_issued ++;
                            IBA_PUT(curr_ptr, win_ptr, size);
                            if (head_ptr == curr_ptr) {
                                prev_ptr = head_ptr = curr_ptr->next;
                            } else
                                prev_ptr->next = curr_ptr->next;

                            tmp_ptr = curr_ptr->next;
                            MPIU_Free(curr_ptr);
                            curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                            curr_put = 1;
                            DEBUG_PRINT("put isssued\n");
                            issued ++;
                        } else {    /* nearest issued rma is a put */
                            if (skipped_op == NULL)
                                skipped_op = curr_ptr;
                            prev_ptr = curr_ptr;
                            curr_ptr = curr_ptr->next;
                        }
#endif
                    } else {
                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
                    }
                    break;
                }
            case (MPIDI_RMA_ACCUMULATE):
                {
#if 0
                    /*int origin_dt_derived, target_dt_derived;
                    if (HANDLE_GET_KIND(curr_ptr->origin_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        origin_dt_derived = 1;
                    else
                        origin_dt_derived = 0;
                    if (HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        target_dt_derived = 1;
                    else
                        target_dt_derived = 0;
                    if (!origin_dt_derived && !target_dt_derived) {
                        IBA_ACCUMULATE(curr_ptr, win_ptr, passive);
                        if (head_ptr == curr_ptr)
                            prev_ptr = head_ptr = curr_ptr->next;
                        else
                            prev_ptr->next = curr_ptr->next;
                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
                    } else {*/
#endif
                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
#if 0
                    /*}*/
#endif
                    break;
                }
            case (MPIDI_RMA_GET):
                {
                    int origin_dt_derived, target_dt_derived;
                    if (HANDLE_GET_KIND(curr_ptr->origin_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        origin_dt_derived = 1;
                    else
                        origin_dt_derived = 0;
                    if (HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                        HANDLE_KIND_BUILTIN)
                        target_dt_derived = 1;
                    else
                        target_dt_derived = 0;
                    MPID_Datatype_get_size_macro(curr_ptr->target_datatype,
                                 target_type_size);
                    size = curr_ptr->target_count * target_type_size;

                    if (!origin_dt_derived && !target_dt_derived 
                        && size > rdma_get_fallback_threshold) {
#ifdef _SCHEDULE
                        if (curr_put != 0 || force_to_progress == 1) {
#endif
                        win_ptr->rma_issued ++;
                        IBA_GET(curr_ptr, win_ptr, size);
                        if (head_ptr == curr_ptr)
                            prev_ptr = head_ptr = curr_ptr->next;
                        else
                            prev_ptr->next = curr_ptr->next;
                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                            curr_put = 0;
                            DEBUG_PRINT("Get issued\n");
                            issued ++;
                        } else {
                            if (skipped_op == NULL)
                                skipped_op = curr_ptr;
                            prev_ptr = curr_ptr;
                            curr_ptr = curr_ptr->next;
                        }
#endif
                    } else {
                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
                    }
                    break;
                }
            default:
                /*if (remain_ptr ==NULL)
                   remain_ptr = curr_ptr;
                   else
                   remain_ptr->next=curr_ptr; */
                printf("Unknown ONE SIDED OP\n");
                exit(0);
                break;
            }
        } else {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
        }
    }
    *MPIDI_RMA_ops_list = head_ptr;
}

/* For active synchronization */
void MPIDI_CH3I_RDMA_post(MPID_Win * win_ptr, int target_rank)
{
    char                *remote_address;
    char                *origin_addr;
    uint32_t            l_key, r_key;
    int                 size;
    struct ibv_qp       *qp_hndl;
    int                 ret;
    struct ibv_send_wr  desc;
    struct ibv_sge      sg_entry;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    /*part 1 prepare origin side buffer */
    remote_address = (char *) win_ptr->remote_post_flags[target_rank];
    l_key = win_ptr->pinnedpool_1sc_dentry->memhandle->lkey;
    r_key = win_ptr->r_key4[target_rank];
    size = sizeof(int);
    Get_Pinned_Buf(win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;
    /*part 2 Do RDMA WRITE */
    desc.send_flags     = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    desc.opcode         = IBV_WR_RDMA_WRITE;
    desc.num_sge        = 1;
    desc.wr.rdma.remote_addr    = (uintptr_t) (remote_address);
    desc.wr.rdma.rkey          = r_key;
    desc.sg_list        = &(sg_entry);

    sg_entry.length     = size;
    sg_entry.lkey       = l_key;
    sg_entry.addr       = (uintptr_t) (origin_addr);

    POST_PUT_PUT_GET_LIST(win_ptr, -1, NULL, tmp_vc, &desc);
    Consume_signals(win_ptr, 0);
}

void
MPIDI_CH3I_RDMA_win_create(void *base,
                           MPI_Aint size,
                           int comm_size,
                           int rank,
                           MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
    int ret, i, index;
    unsigned long   *tmp;
    uint32_t        r_key, r_key2, r_key3, postflag_rkey;
    int             my_rank;
    unsigned long   *tmp1, *tmp2, *tmp3, *tmp4;

    PMI_Get_rank(&my_rank);
    /*There may be more than one windows existing at the same time */
    MPIDI_CH3I_RDMA_Process.current_win_num++;
    ASSERT(MPIDI_CH3I_RDMA_Process.current_win_num <= MAX_WIN_NUM);
    index   = Find_Avail_Index();
    ASSERT(index != -1);

    tmp = MPIU_Malloc(comm_size * sizeof(unsigned long) * 7);
    if (!tmp) {
        printf("Error malloc tmp when creating windows\n");
        exit(0);
    }

    (*win_ptr)->fall_back = 0;
    /*Register the exposed buffer in this window */
    if (base != NULL && size > 0) {
        MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] =
            dreg_register(base, size);
        if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]) {
            (*win_ptr)->fall_back = 1;
            goto err_base_register;
        }
        r_key =
            MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]->
            memhandle->rkey;
    } else {
        MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] = NULL;
        r_key = 0;
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter = MPIU_Malloc(sizeof(long long) * comm_size);
    if (!(*win_ptr)->completion_counter) {
        (*win_ptr)->fall_back = 1;
        goto err_cc_buf;
    }

    memset((*win_ptr)->completion_counter, 0, sizeof(long long) * comm_size); 
    MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] =
        dreg_register((void *) (*win_ptr)->completion_counter,
                          sizeof(long long) * comm_size);
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]) {
        (*win_ptr)->fall_back = 1;
        goto err_cc_register;
    }
    r_key2 =
        MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]->
        memhandle->rkey;

    /*Register buffer for accumulation exclusive access lock */
    (*win_ptr)->actlock = (long long *) MPIU_Malloc(sizeof(long long));
    if (!(*win_ptr)->actlock) {
        (*win_ptr)->fall_back = 1;
        goto err_actlock_buf;
    }

    MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] =
        dreg_register((void *) (*win_ptr)->actlock, sizeof(long long));
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]) {
        (*win_ptr)->fall_back = 1;
        goto err_actlock_register;
    }
    r_key3 =
        MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]->
        memhandle->rkey;
    *((long long *) ((*win_ptr)->actlock)) = 0;

    /*Register buffer for post flags : from target to origin */
    (*win_ptr)->post_flag = (int *) MPIU_Malloc(comm_size * sizeof(int));
    if (!(*win_ptr)->post_flag) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_buf;
    }

    MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] =
        dreg_register((void *) (*win_ptr)->post_flag, sizeof(int) * comm_size);
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_register;
    }
    postflag_rkey =
        MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]->
        memhandle->rkey;

    (*win_ptr)->pinnedpool_1sc_buf =
            MPIU_Malloc(rdma_pin_pool_size);
    if (!(*win_ptr)->pinnedpool_1sc_buf) {
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_buf;
    }
    (*win_ptr)->pinnedpool_1sc_dentry = 
        dreg_register((*win_ptr)->pinnedpool_1sc_buf,
                    rdma_pin_pool_size);
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry) {
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_register;
    }
    (*win_ptr)->pinnedpool_1sc_index = 0;

    /*Exchagne the information about rkeys and addresses */
    tmp[7 * rank]     = r_key;
    tmp[7 * rank + 1] = r_key2;
    tmp[7 * rank + 2] = r_key3;
    tmp[7 * rank + 3] = (uintptr_t) ((*win_ptr)->actlock);
    tmp[7 * rank + 4] = (uintptr_t) ((*win_ptr)->completion_counter);
    tmp[7 * rank + 5] = (uintptr_t) ((*win_ptr)->assist_thr_ack);
    tmp[7 * rank + 6] = (*win_ptr)->fall_back;

    ret =
        NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 7,
                       MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        printf("Error gather rkey  when creating windows\n");
        exit(0);
    }

    /* check if any peers fail */
    for (i = 0; i < comm_size; i++) {
        if (tmp[7 * i + 6] != 0) 
            break;
    }

    if (i != comm_size) {
        MPIU_Free(tmp);
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
        MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_post_flag_dreg_entry[index]);
        MPIU_Free((*win_ptr)->post_flag);
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_local_actlock_dreg_entry[index]);
        MPIU_Free((*win_ptr)->actlock);
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_local_wincc_dreg_entry[index]);
        MPIU_Free((*win_ptr)->completion_counter);
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_local_win_dreg_entry[index]);
        (*win_ptr)->fall_back = 1;
        goto fn_exit;
    }

    (*win_ptr)->r_key = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t));
    if (!(*win_ptr)->r_key) {
        printf("Error malloc win->r_key when creating windows\n");
        exit(0);
    }
    (*win_ptr)->r_key2 = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t));
    if (!(*win_ptr)->r_key2) {
        printf("Error malloc win->r_key2 when creating windows\n");
        exit(0);
    }
    (*win_ptr)->r_key3 = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t));
    if (!(*win_ptr)->r_key3) {
        printf("error malloc win->r_key3 when creating windows\n");
        exit(0);
    }
    (*win_ptr)->all_actlock_addr =
        (long long **) MPIU_Malloc(comm_size * sizeof(long long *));
    if (!(*win_ptr)->all_actlock_addr) {
        printf
            ("error malloc win->all_actlock_addr when creating windows\n");
        exit(0);
    }
    (*win_ptr)->all_completion_counter =
        (long long **) MPIU_Malloc(comm_size * sizeof(long long *));
    if (!(*win_ptr)->all_completion_counter) {
        printf
            ("error malloc win->all_completion_counter when creating windows\n");
        exit(0);
    }

    (*win_ptr)->all_assist_thr_acks =
        (int **) MPIU_Malloc(comm_size * sizeof(int *));
    if (!(*win_ptr)->all_assist_thr_acks) {
        printf("error malloc win->all_wins when creating windows\n");
        exit(0);
    }


    for (i = 0; i < comm_size; i++) {
        (*win_ptr)->r_key[i]    = tmp[7 * i];
        (*win_ptr)->r_key2[i]   = tmp[7 * i + 1];
        (*win_ptr)->r_key3[i]   = tmp[7 * i + 2];
        (*win_ptr)->all_actlock_addr[i] = (long long *) tmp[7 * i + 3];
        (*win_ptr)->all_completion_counter[i] =
            (long long *) (tmp[7 * i + 4] + sizeof(long long)*rank);
        (*win_ptr)->all_assist_thr_acks[i] 
                                = (int *) tmp[7 * i + 5];
    }

    tmp1 = (unsigned long *) MPIU_Malloc(comm_size * sizeof(unsigned long));
    if (!tmp1) {
        printf("Error malloc tmp1 when creating windows\n");
        exit(0);
    }
    tmp2 = (unsigned long *) MPIU_Malloc(comm_size * sizeof(unsigned long));
    if (!tmp2) {
        printf("Error malloc tmp2 when creating windows\n");
        exit(0);
    }
    tmp3 = (unsigned long *) MPIU_Malloc(comm_size * sizeof(unsigned long));
    if (!tmp3) {
        printf("Error malloc tmp3 when creating windows\n");
        exit(0);
    }
    tmp4 = (unsigned long *) MPIU_Malloc(comm_size * sizeof(unsigned long));
    if (!tmp4) {
        printf("Error malloc tmp4 when creating windows\n");
        exit(0);
    }
    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; i++) {
        if (i != rank)
            (*win_ptr)->post_flag[i] = 0;
        else
            (*win_ptr)->post_flag[i] = 1;
        tmp1[i] = postflag_rkey;
        tmp2[i] = (uintptr_t) &((*win_ptr)->post_flag[i]);
    }
    ret =
        NMPI_Alltoall(tmp1, 1, MPI_LONG, tmp3, 1, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        printf("Error gather rkey  when creating windows\n");
        exit(0);
    }
    ret =
        NMPI_Alltoall(tmp2, 1, MPI_LONG, tmp4, 1, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        printf("Error gather rkey  when creating windows\n");
        exit(0);
    }
    (*win_ptr)->r_key4 = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t));
    if (!(*win_ptr)->r_key4) {
        printf("error malloc win->r_key3 when creating windows\n");
        exit(0);
    }
    (*win_ptr)->remote_post_flags =
        (long **) MPIU_Malloc(comm_size * sizeof(long *));
    if (!(*win_ptr)->remote_post_flags) {
        printf
            ("error malloc win->remote_post_flags when creating windows\n");
        exit(0);
    }
    for (i = 0; i < comm_size; i++) {
        (*win_ptr)->r_key4[i] = tmp3[i];
        (*win_ptr)->remote_post_flags[i] = (long *) tmp4[i];
    }
    MPIU_Free(tmp);
    MPIU_Free(tmp1);
    MPIU_Free(tmp2);
    MPIU_Free(tmp3);
    MPIU_Free(tmp4);
    (*win_ptr)->using_lock = 0;
    (*win_ptr)->using_start = 0;
    (*win_ptr)->my_id = rank;
    (*win_ptr)->comm_size = comm_size;
    /* Initialize put/get queue */
    (*win_ptr)->put_get_list_size = 0;
    (*win_ptr)->put_get_list_tail = 0;
    (*win_ptr)->put_get_list = 
        MPIU_Malloc( rdma_default_put_get_list_size *
            sizeof(MPIDI_CH3I_RDMA_put_get_list)); 
    (*win_ptr)->wait_for_complete = 0;
    (*win_ptr)->rma_issued = 0;

    if (!(*win_ptr)->put_get_list) {
        printf("Fail to malloc space for window put get list\n");
        exit(0);
    }


    MPIDI_CH3I_RDMA_Process.win_index2address[index] = (long) *win_ptr;
fn_exit:
    if (1 == (*win_ptr)->fall_back) {
        MPIDI_CH3I_RDMA_Process.win_index2address[index] = 0;
        MPIDI_CH3I_RDMA_Process.current_win_num--;
        (*win_ptr)->using_lock = 0;
        (*win_ptr)->using_start = 0;
        (*win_ptr)->my_id = rank;

        (*win_ptr)->comm_size = comm_size;
        /* Initialize put/get queue */
        (*win_ptr)->put_get_list_size = 0;
        (*win_ptr)->put_get_list_tail = 0;
        (*win_ptr)->wait_for_complete = 0;
        (*win_ptr)->rma_issued = 0;
    }
    return;

  err_pinnedpool_register:
    MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
  err_pinnedpool_buf:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]);
  err_postflag_register:
    MPIU_Free((*win_ptr)->post_flag);
  err_postflag_buf:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_local_actlock_dreg_entry[index]);
  err_actlock_register:
    MPIU_Free((*win_ptr)->actlock);
  err_actlock_buf:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
                        RDMA_local_wincc_dreg_entry[index]);
  err_cc_register:
    MPIU_Free((*win_ptr)->completion_counter);
  err_cc_buf:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]);
  err_base_register:
    tmp[7 * rank + 6] = (*win_ptr)->fall_back;

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 7,
                       MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        printf("Error gather rkey  when creating windows\n");
        exit(0);
    }
    return;
}

void MPIDI_CH3I_RDMA_win_free(MPID_Win ** win_ptr)
{
    int index;
    index = Find_Win_Index(*win_ptr);
    if (index == -1) {
        printf("dont know win_ptr %p %d \n", *win_ptr,
               MPIDI_CH3I_RDMA_Process.current_win_num);
    }
    assert(index != -1);
    MPIDI_CH3I_RDMA_Process.win_index2address[index] = 0;
    MPIDI_CH3I_RDMA_Process.current_win_num--;
    assert(MPIDI_CH3I_RDMA_Process.current_win_num >= 0);
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] != NULL) {
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                            RDMA_local_win_dreg_entry[index]);
    }
    MPIU_Free((*win_ptr)->r_key);
    MPIU_Free((*win_ptr)->r_key2);
    MPIU_Free((*win_ptr)->r_key3);
    MPIU_Free((*win_ptr)->all_actlock_addr);
    MPIU_Free((*win_ptr)->r_key4);
    MPIU_Free((*win_ptr)->remote_post_flags);
    MPIU_Free((*win_ptr)->put_get_list);
    if ((*win_ptr)->pinnedpool_1sc_dentry) {
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
    }
    MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] != NULL) {
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                            RDMA_local_wincc_dreg_entry[index]);
    }
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] !=
        NULL) {
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                            RDMA_local_actlock_dreg_entry[index]);
    }
    if (MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] != NULL) {
        dreg_unregister(MPIDI_CH3I_RDMA_Process.
                            RDMA_post_flag_dreg_entry[index]);
    }
    MPIU_Free((*win_ptr)->actlock);
    MPIU_Free((*win_ptr)->completion_counter);
    MPIU_Free((*win_ptr)->all_completion_counter);
}

static int Decrease_CC(MPID_Win * win_ptr, int target_rank)
{
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key2, l_key2;
    int                 ret;
    struct ibv_send_wr  desc;
    struct ibv_sge      sg_entry;
    long long           *cc;
    struct ibv_qp       *qp_hndl;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;

    Get_Pinned_Buf(win_ptr, (char **) &cc, sizeof(long long));
    *((long long *) cc) = 1;
    l_key2 = win_ptr->pinnedpool_1sc_dentry->memhandle->lkey;
    r_key2 = win_ptr->r_key2[target_rank];

    desc.send_flags             = IBV_SEND_SIGNALED | IBV_SEND_INLINE; 
    desc.opcode                 = IBV_WR_RDMA_WRITE;
    desc.num_sge                = 1;
    desc.wr.rdma.remote_addr    = (uintptr_t)(win_ptr->all_completion_counter[target_rank]);
    desc.wr.rdma.rkey           = r_key2;
    desc.sg_list                = &(sg_entry);

    sg_entry.length             = sizeof(long long);
    sg_entry.lkey               = l_key2;
    sg_entry.addr               = (uintptr_t) (cc);

    POST_PUT_PUT_GET_LIST(win_ptr, -1, NULL, tmp_vc, &desc);

    return mpi_errno;
}

static int POST_PUT_PUT_GET_LIST(  MPID_Win * winptr, 
                            int size, 
                            dreg_entry * dreg_tmp,
                            MPIDI_VC_t * vc_ptr,                         
                            struct ibv_send_wr * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;
    struct ibv_send_wr * bad_wr;
    winptr->put_get_list[index].op_type     = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry   = dreg_tmp;
    winptr->put_get_list[index].data_size   = size;
    winptr->put_get_list[index].win_ptr     = winptr;
    winptr->put_get_list[index].vc_ptr      = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                    rdma_default_put_get_list_size;
    winptr->put_get_list_size++;
    vc_ptr->mrail.postsend_times_1sc ++;

    desc_p->wr_id   = (uintptr_t)(&(winptr->put_get_list[index])); 
    desc_p->next    = NULL;

    DEBUG_PRINT("post one, sge %d, send flag %d, size %d\n", 
            desc_p->num_sge, desc_p->send_flags, desc_p->sg_list->length);
    ret = ibv_post_send(vc_ptr->mrail.qp_hndl_1sc, desc_p, &bad_wr);
    CHECK_RETURN(ret, "Fail in posting RDMA_Write");

    if (winptr->put_get_list_size == rdma_default_put_get_list_size
        || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_wqe - 1) {
        Consume_signals(winptr, 0);
    }
    return index;
}

/*functions storing the get operations */
static int POST_GET_PUT_GET_LIST(
                MPID_Win * winptr,
                int size,
                void *target_addr, 
                void *origin_addr, 
                dreg_entry * dreg_tmp,
                MPIDI_VC_t * vc_ptr,
                struct ibv_send_wr * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;
    struct ibv_send_wr  * bad_wr;
   
    winptr->put_get_list[index].op_type = SIGNAL_FOR_GET;
    winptr->put_get_list[index].origin_addr = origin_addr;
    winptr->put_get_list[index].target_addr = target_addr;
    winptr->put_get_list[index].data_size = size;
    winptr->put_get_list[index].mem_entry = dreg_tmp;
    winptr->put_get_list[index].win_ptr = winptr;
    winptr->put_get_list[index].vc_ptr = vc_ptr;
    winptr->put_get_list_size ++;
    vc_ptr->mrail.postsend_times_1sc ++;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) % 
                    rdma_default_put_get_list_size;

    desc_p->wr_id   = (uintptr_t)&winptr->put_get_list[index];
    desc_p->next    = NULL;
    winptr->wait_for_complete = 1;
    
    ret = ibv_post_send(vc_ptr->mrail.qp_hndl_1sc, desc_p, &bad_wr);
    CHECK_RETURN(ret, "Fail in posting RDMA_READ");

    if (winptr->put_get_list_size == rdma_default_put_get_list_size
        || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_wqe - 1) 
        /* the get queue is full */
        Consume_signals(winptr, 0);
    return index;
}

/*if signal == -1, cousume all the signals, 
  otherwise return when signal is found*/
static int Consume_signals(MPID_Win * winptr, uint64_t expected)
{
    struct ibv_wc   wc;
    int             ret;
    dreg_entry      *dreg_tmp;
    int             i = 0, size;
    int             ne;
    void            *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list  *list_entry=NULL;
    MPID_Win                      *list_win_ptr;
    MPIDI_VC_t                    *list_vc_ptr;

    while (1) {
        ne = ibv_poll_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc, 1, &wc);
        if (ne > 0) {
            i++;
            if (wc.status != IBV_WC_SUCCESS) {
                ibv_error_abort(IBV_STATUS_ERR, "in Consume_signals %08lx get wrong status %d \n",
                       (uint32_t)expected, wc.status);
                
            }
            assert(wc.wr_id != -1);

            DEBUG_PRINT("[rank %d ]Get success wc, id %08x, expecting %08x\n", 
                    winptr->my_id, (uint32_t)wc.wr_id, (uint32_t)expected); 
            if (wc.wr_id == expected) {
                goto fn_exit;
            }

            /* if id is not equal to expected id, the only possible cq
             * is for the posted signaled PUT/GET.
             * Otherwise we have error.
             */
            list_entry = (MPIDI_CH3I_RDMA_put_get_list *)(uintptr_t)wc.wr_id;

            list_win_ptr = list_entry->win_ptr;
            list_vc_ptr = list_entry->vc_ptr;
            if (list_entry->op_type == SIGNAL_FOR_PUT) {
                dreg_tmp = list_entry->mem_entry;
                size = list_entry->data_size;
                if (size > (int)rdma_eagersize_1sc) {
                    dreg_unregister(dreg_tmp);
                }
                list_win_ptr->put_get_list_size --;
                list_vc_ptr->mrail.postsend_times_1sc --;
            } else if (list_entry->op_type == SIGNAL_FOR_GET) {
                size = list_entry->data_size;
                target_addr = list_entry->target_addr;
                origin_addr = list_entry->origin_addr;
                dreg_tmp = list_entry->mem_entry;
                if (origin_addr == NULL) {
                    ASSERT(size > rdma_eagersize_1sc);
                    dreg_unregister(dreg_tmp);
                } else {
                    ASSERT(size <= rdma_eagersize_1sc);
                    ASSERT(target_addr != NULL);
                    memcpy(target_addr, origin_addr, size);
                }
                list_win_ptr->put_get_list_size --;
                list_vc_ptr->mrail.postsend_times_1sc --;
            } else {
                fprintf(stderr, "Error! rank %d, Undefined op_type, op type %d, \
                list id %u, expecting id %u\n",
                winptr->my_id, list_entry->op_type, list_entry, (uint32_t)expected);
                exit(0);
            }
        } else if (ne < 0) {
            ibv_error_abort(IBV_STATUS_ERR, "Fail to poll one sided completion queue\n");
        }


        if (winptr->put_get_list_size == 0) 
            winptr->put_get_list_tail = 0;
        if (winptr->put_get_list_size == 0
            && (expected == 0)) {
            winptr->rma_issued = 0;
            winptr->pinnedpool_1sc_index = 0;
            goto fn_exit;
        }
    } /* end of while */
  fn_exit:
    return 0;
}

static int IBA_PUT(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key, l_key;
    int                 ret;
    int                 origin_type_size;
    dreg_entry          *tmp_dreg = NULL;
    struct ibv_send_wr   desc;
    struct ibv_sge       sg_entry;
    char                *origin_addr;

    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    int                 index;

    /*part 1 prepare origin side buffer target buffer and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank]
        + win_ptr->disp_units[rma_op->target_rank]
        * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        char *tmp = rma_op->origin_addr;

        Get_Pinned_Buf(win_ptr, &origin_addr, size);
        memcpy(origin_addr, tmp, size);
        l_key = win_ptr->pinnedpool_1sc_dentry->memhandle->lkey;
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
        l_key = tmp_dreg->memhandle->lkey;
        origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }
    r_key = win_ptr->r_key[rma_op->target_rank];
    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

    /*part 2 Do RDMA WRITE */
    desc.send_flags     = IBV_SEND_SIGNALED;
    desc.opcode         = IBV_WR_RDMA_WRITE;

    desc.num_sge        = 1;
    desc.wr.rdma.remote_addr    = (uintptr_t) (remote_address);
    desc.wr.rdma.rkey           = r_key;
    desc.sg_list                = &(sg_entry);

    sg_entry.length             = size;
    sg_entry.lkey               = l_key;
    sg_entry.addr               = (uintptr_t) (origin_addr);
    POST_PUT_PUT_GET_LIST(win_ptr, size, tmp_dreg, tmp_vc,&desc);
    return mpi_errno;
}

int IBA_GET(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key, l_key;
    int                 ret;
    int                 index;
    dreg_entry          *tmp_dreg;
    struct ibv_send_wr  desc;
    struct ibv_sge      sg_entry;
    char                *origin_addr;

    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;


    assert(rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        Get_Pinned_Buf(win_ptr, &origin_addr, size);
        l_key = win_ptr->pinnedpool_1sc_dentry->memhandle->lkey;
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
        l_key = tmp_dreg->memhandle->lkey;
        origin_addr = rma_op->origin_addr;
    }

    r_key = win_ptr->r_key[rma_op->target_rank];
    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

    desc.send_flags         = IBV_SEND_SIGNALED;
    desc.opcode             = IBV_WR_RDMA_READ;
    desc.num_sge            = 1;
    desc.wr.rdma.remote_addr  = (uintptr_t)(remote_address);
    desc.wr.rdma.rkey         = r_key;
    desc.sg_list            = &(sg_entry);

    sg_entry.length         = size;
    sg_entry.lkey           = l_key;
    sg_entry.addr           = (uintptr_t) (origin_addr);
    if (size <= rdma_eagersize_1sc)
        index = POST_GET_PUT_GET_LIST(win_ptr, size,
                          rma_op->origin_addr, origin_addr,
                          NULL, tmp_vc, &desc);
    else
        index = POST_GET_PUT_GET_LIST(win_ptr, size, NULL, NULL,
            tmp_dreg, tmp_vc, &desc);

    return mpi_errno;
}

#endif

#endif
