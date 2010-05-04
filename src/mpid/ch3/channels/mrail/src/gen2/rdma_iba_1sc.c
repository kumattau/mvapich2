/* Copyright (c) 2003-2010, The Ohio State University. All rights
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
#include <math.h>

#include "rdma_impl.h"
#include "dreg.h"
#include "ibv_param.h"
#include "infiniband/verbs.h"
#include "mpidrma.h"
#include "pmi.h"
#include "mpiutil.h"

#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

/*#define DEBUG */
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

extern int number_of_op;
static int Decrease_CC(MPID_Win *, int);
static int Post_Get_Put_Get_List(MPID_Win *, 
        int , dreg_entry * ,
        MPIDI_VC_t * , void *local_buf[], 
        void *remote_buf[], int length,
        uint32_t lkeys[], uint32_t rkeys[], 
        int use_multi);

static int Post_Put_Put_Get_List(MPID_Win *, int,  dreg_entry *, 
        MPIDI_VC_t *, void *local_buf[], void *remote_buf[], int length,
        uint32_t lkeys[], uint32_t rkeys[],int use_multi );

static int iba_put(MPIDI_RMA_ops *, MPID_Win *, int);
static int iba_get(MPIDI_RMA_ops *, MPID_Win *, int);
static int Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size);
int     iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
int     iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);
int MRAILI_Handle_one_sided_completions(vbuf * v);                            

static inline int Find_Avail_Index()
{
    int i, index = -1;
    for (i = 0; i < MAX_WIN_NUM; ++i) {
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
    for (i = 0; i < MAX_WIN_NUM; ++i) {
        if (MPIDI_CH3I_RDMA_Process.win_index2address[i] == (long) win_ptr) {
            index = i;
            break;
        }
    }
    return index;
}

static inline int Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size)
{
    int mpi_errno = MPI_SUCCESS;
    if (win_ptr->pinnedpool_1sc_index + size >=
        rdma_pin_pool_size) {
        win_ptr->poll_flag = 1;
        while (win_ptr->poll_flag == 1) {
            mpi_errno = MPIDI_CH3I_Progress_test();
            if(mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        *origin = win_ptr->pinnedpool_1sc_buf;
        win_ptr->pinnedpool_1sc_index = size;
    } else {
        *origin =
            win_ptr->pinnedpool_1sc_buf +
            win_ptr->pinnedpool_1sc_index;
        win_ptr->pinnedpool_1sc_index += size;
    }

fn_fail:
    return mpi_errno;
}


/* For active synchronization, it is a blocking call*/
void
MPIDI_CH3I_RDMA_start (MPID_Win* win_ptr, int start_grp_size, int* ranks_in_win_grp) 
{
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    int flag = 0;
    int src;
    int i;
    int counter = 0;

    if (SMP_INIT)
    {
        MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    }

    while (flag == 0 && start_grp_size != 0)
    {
        /* Need to make sure we make some progress on
         * anything in the extended sendq or coalesced
         * or we can have a deadlock.
         */
        if (counter % 200 == 0)
        {
            MPIDI_CH3I_Progress_test();
        }

        ++counter;

        for (i = 0; i < start_grp_size; ++i)
        {
            flag = 1;
            src = ranks_in_win_grp[i];  /*src is the rank in comm*/

            if (SMP_INIT)
            {
                MPIDI_Comm_get_vc(comm_ptr, src, &vc);

                if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
                {
                    /* Correspoding post has not been issued. */
                    flag = 0;
                    break;
                }
            }
            else if (win_ptr->post_flag[src] == 0)
            {
                /* Correspoding post has not been issued. */
                flag = 0;
                break;
            }
        }
    }
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
    MPIDI_VC_t* vc = NULL;

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

    win_ptr->wait_for_complete = 1;

    /* set rma_target_proc[i] to 1 if rank i is a target of RMA
       ops from this process */
    for (i = 0; i < comm_size; ++i)
    {
        nops_to_proc[i] = 0;
    }
    curr_ptr = win_ptr->rma_ops_list;
    while (curr_ptr != NULL) {
        ++nops_to_proc[curr_ptr->target_rank];
        curr_ptr = curr_ptr->next;
    }
    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; ++i) {
        dst = ranks_in_win_grp[i];
        win_ptr->post_flag[dst] = 0;
    }
    win_ptr->using_start = 0;

    for (i = 0; i < start_grp_size; ++i) {
        target = ranks_in_win_grp[i];   /* target is the rank is comm */

      if(SMP_INIT) {
        MPIDI_Comm_get_vc(comm_ptr, target, &vc);
        if (nops_to_proc[target] == 0 && send_complete == 1 
            && vc->smp.local_nodes == -1)
        {
            Decrease_CC(win_ptr, target);
            if (win_ptr->wait_for_complete == 1) {
                MPIDI_CH3I_RDMA_finish_rma(win_ptr);
            }
        }
      } else if (nops_to_proc[target] == 0 && send_complete == 1)  
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
    int mpi_errno = MPI_SUCCESS;
    if (win_ptr->put_get_list_size != 0) {
            win_ptr->poll_flag = 1;
            while(win_ptr->poll_flag == 1){
                mpi_errno = MPIDI_CH3I_Progress_test();
                if(mpi_errno) MPIU_ERR_POP(mpi_errno);
	    }	
    }
    else return 0;

fn_fail:
    return mpi_errno;
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
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;

    if (SMP_INIT)
    {
        MPID_Comm_get_ptr( win_ptr->comm, comm_ptr );
    }

    prev_ptr = curr_ptr = head_ptr = *MPIDI_RMA_ops_list;
    if (*MPIDI_RMA_ops_list != NULL) tag = 1;

#ifdef _SCHEDULE
    while (curr_ptr != NULL || skipped_op != NULL)
    {
        if (curr_ptr == NULL)
        {
            curr_ptr = skipped_op;
            skipped_op = NULL;
            ++fall_back;
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

     if (SMP_INIT) {
        MPIDI_Comm_get_vc(comm_ptr, curr_ptr->target_rank, &vc);

        if (vc->smp.local_nodes != -1)  {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }
     }

     if (passive == 0
         && win_ptr->post_flag[curr_ptr->target_rank] == 1
         && win_ptr->using_lock == 0)
     {
         switch (curr_ptr->type)
         {
            case MPIDI_RMA_PUT:
            {
                int origin_dt_derived;
                int target_dt_derived;
                origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                MPID_Datatype_get_size_macro(curr_ptr->origin_datatype, origin_type_size);
                size = curr_ptr->origin_count * origin_type_size;
 
                if (!origin_dt_derived
                    && !target_dt_derived
                    && size > rdma_put_fallback_threshold)
                {
#ifdef _SCHEDULE
                    if (curr_put != 1 || force_to_progress == 1)
                    { 
                      /* Nearest issued rma is not a put. */
#endif
                        ++win_ptr->rma_issued;
                        iba_put(curr_ptr, win_ptr, size);

                        if (head_ptr == curr_ptr)
                        {
                            prev_ptr = head_ptr = curr_ptr->next;
                        }
                        else
                        {
                            prev_ptr->next = curr_ptr->next;
                        }

                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                        curr_put = 1;
                        ++issued;
                    }
                    else
                    {
                        /* Nearest issued rma is a put. */
                        if (skipped_op == NULL)
                        {
                            skipped_op = curr_ptr;
                            prev_ptr = curr_ptr;
                            curr_ptr = curr_ptr->next;
                        }
                    }
#endif
                 }                 
                 else
                 {
                     prev_ptr = curr_ptr;
                     curr_ptr = curr_ptr->next;
                 }

                 break;
              }
            case MPIDI_RMA_ACCUMULATE:
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                break;
            case MPIDI_RMA_GET:
            {
                int origin_dt_derived;
                int target_dt_derived;

                origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                MPID_Datatype_get_size_macro(curr_ptr->target_datatype, target_type_size);
                size = curr_ptr->target_count * target_type_size;

                if (!origin_dt_derived
                    && !target_dt_derived 
                    && size > rdma_get_fallback_threshold)
                {
#ifdef _SCHEDULE
                    if (curr_put != 0 || force_to_progress == 1)
                    {
#endif
                        ++win_ptr->rma_issued;
                        iba_get(curr_ptr, win_ptr, size);

                        if (head_ptr == curr_ptr)
                        {
                            prev_ptr = head_ptr = curr_ptr->next;
                        }
                        else
                        {
                            prev_ptr->next = curr_ptr->next;
                        }

                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                        curr_put = 0;
                        ++issued;
                    }
                    else
                    {
                        if (skipped_op == NULL)
                        {
                            skipped_op = curr_ptr;
                        }

                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
                    }
#endif
                }
                else
                {
                    prev_ptr = curr_ptr;
                    curr_ptr = curr_ptr->next;
                }

                break;
            }
            default:
                DEBUG_PRINT("Unknown ONE SIDED OP\n");
                ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
                break;
            }
        }
        else
        {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
        }
    }

    *MPIDI_RMA_ops_list = head_ptr;
}

/* For active synchronization */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_post
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int MPIDI_CH3I_RDMA_post(MPID_Win * win_ptr, int target_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    int mpi_errno = MPI_SUCCESS;

    char                *origin_addr;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;
    /*part 1 prepare origin side buffer */
    char* remote_address = (char *) win_ptr->remote_post_flags[target_rank];
    int size = sizeof(int);
    Get_Pinned_Buf(win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    int hca_index = 0;
    uint32_t l_key = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
    uint32_t r_key = win_ptr->r_key4[target_rank * rdma_num_hcas + hca_index];

    Post_Put_Put_Get_List(win_ptr, -1, NULL, 
            tmp_vc, (void *)&origin_addr, 
            (void*)&remote_address, size, 
            &l_key, &r_key, 0 );

    win_ptr->poll_flag = 1;
    while (win_ptr->poll_flag == 1)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    return mpi_errno;
}

void
MPIDI_CH3I_RDMA_win_create(void *base,
                           MPI_Aint size,
                           int comm_size,
                           int rank,
                           MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
 
    int ret, i,j, index, arr_index;
    unsigned long   *tmp, *tmp_new;
    uint32_t        r_key[MAX_NUM_HCAS], r_key2[MAX_NUM_HCAS];
    uint32_t        r_key3[MAX_NUM_HCAS], postflag_rkey[MAX_NUM_HCAS];
    unsigned long   *tmp1, *tmp2, *tmp3, *tmp4;
    int             fallback_trigger = 0;

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided)
    {
        (*win_ptr)->fall_back = 1;
        return;
    }
    
    /*There may be more than one windows existing at the same time */
   
    ++MPIDI_CH3I_RDMA_Process.current_win_num;
    MPIU_Assert(MPIDI_CH3I_RDMA_Process.current_win_num <= MAX_WIN_NUM);
    index = Find_Avail_Index();
    MPIU_Assert(index != -1);

    tmp = MPIU_Malloc(comm_size * sizeof(unsigned long) * 7 * rdma_num_hcas); 

    if (!tmp)
    {
        DEBUG_PRINT("Error malloc tmp when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    
    tmp_new = MPIU_Malloc(comm_size * sizeof(unsigned long) * rdma_num_rails);
   
    if (!tmp_new)
    {
        DEBUG_PRINT("Error malloc tmp_new when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->fall_back = 0;

    /*Register the exposed buffer in this window */
    if (base != NULL && size > 0) {
        MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] =
            /* Register the entire window with all HCAs */
            dreg_register(base, size);
        if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]) {
            (*win_ptr)->fall_back = 1;
            goto err_base_register;
        }
        for (i=0; i < rdma_num_hcas; ++i) {
             r_key[i] = MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry
                 [index]->memhandle[i]->rkey;
        }
    } else {
        /* Self */
        MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] = NULL;
        for (i = 0; i < rdma_num_hcas; ++i) {
             r_key[i] = 0;
        }
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter = MPIU_Malloc(sizeof(long long) * comm_size * rdma_num_rails);

    /* Fallback case */
    if (!(*win_ptr)->completion_counter) {
        (*win_ptr)->fall_back = 1;
        goto err_cc_buf;
    }

    MPIU_Memset(
        (void *)(*win_ptr)->completion_counter,
        0, 
        sizeof(long long) * comm_size * rdma_num_rails
    ); 

    MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] = dreg_register(
        (void*)(*win_ptr)->completion_counter,
        sizeof(long long) * comm_size * rdma_num_rails
    );

    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_register;
    }

    for (i = 0; i < rdma_num_hcas; ++i)
    {
        r_key2[i] = MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]->memhandle[i]->rkey;
    }

    /*Register buffer for accumulation exclusive access lock */

    (*win_ptr)->actlock = (long long *) MPIU_Malloc(sizeof(long long));
    if (!(*win_ptr)->actlock) {
        (*win_ptr)->fall_back = 1;
        goto err_actlock_buf;
    }

    MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] = dreg_register(
        (void*)(*win_ptr)->actlock,
        sizeof(long long)
    );

    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]) {
        (*win_ptr)->fall_back = 1;
        goto err_actlock_register;
    }
    
    for (i=0; i<rdma_num_hcas; ++i) {
        r_key3[i] = MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]->memhandle[i]->rkey;
        DEBUG_PRINT("the rkey3 is %x\n", r_key3[i]);
    }

    *((long long*)((*win_ptr)->actlock)) = 0;

    /*Register buffer for post flags : from target to origin */
    
    (*win_ptr)->post_flag = (int *) MPIU_Malloc(comm_size * sizeof(int)); 
    
    if (!(*win_ptr)->post_flag) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_buf;
    }

    DEBUG_PRINT(
        "rank[%d] : post flag start before exchange is %p\n",
        rank,
        (*win_ptr)->post_flag
    ); 
  
    /* Register the post flag */

    MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] = dreg_register(
        (void*)(*win_ptr)->post_flag,
        sizeof(int) * comm_size
    );
    
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]) {
        /* Fallback case */
        (*win_ptr)->fall_back = 1;
        goto err_postflag_register;
    }

    for (i = 0; i < rdma_num_hcas; ++i)
    {
        postflag_rkey[i] = MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]->memhandle[i]->rkey;
        DEBUG_PRINT(
            "the rank [%d] postflag_rkey before exchange is %x\n", 
            rank,
            postflag_rkey[i]
        );
    }

    /* Malloc Pinned buffer for one sided communication */
    (*win_ptr)->pinnedpool_1sc_buf = MPIU_Malloc(rdma_pin_pool_size);
    if (!(*win_ptr)->pinnedpool_1sc_buf) {
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_buf;
    }
    
    (*win_ptr)->pinnedpool_1sc_dentry = dreg_register(
        (*win_ptr)->pinnedpool_1sc_buf,
        rdma_pin_pool_size
    );
          
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_register;
    }
     
    (*win_ptr)->pinnedpool_1sc_index = 0;

    /*Exchange the information about rkeys and addresses */

    int calcIndex; 

    for (i = 0; i < rdma_num_hcas; ++i)
    {
        calcIndex = (rank * rdma_num_hcas + i) * 7;
        tmp[calcIndex] = r_key[i];
        tmp[calcIndex + 1] = r_key2[i];
        tmp[calcIndex + 2] = r_key3[i];
        tmp[calcIndex + 3] = (uintptr_t)((*win_ptr)->actlock); 
        tmp[calcIndex + 4] = (uintptr_t)((*win_ptr)->completion_counter + comm_size * i);
        tmp[calcIndex + 5] = (*win_ptr)->fall_back;
    }

    /* All processes will exchange the setup keys *
     * since each process has the same data for all other
     * processes, use allgather */

    ret = NMPI_Allgather(
        MPI_IN_PLACE,
        0,
        MPI_DATATYPE_NULL,
        tmp,
        rdma_num_hcas * 7,
        MPI_LONG,
        comm_ptr->handle
    );

    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* check if any peers fail */
    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_hcas; ++j)
        {
            calcIndex = (rdma_num_hcas * i + j) * 7 + 5;

            if (tmp[calcIndex] != 0)
            {
                fallback_trigger = 1;
            }
        }
    } 
    
    if (fallback_trigger) {
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

    for (i = 0; i < rdma_num_rails; ++i){
         tmp_new[rank * rdma_num_rails + i] = 
             (uintptr_t) ((*win_ptr)->completion_counter + i);
    }    

    ret = 
        NMPI_Allgather(MPI_IN_PLACE, 0 , MPI_DATATYPE_NULL, tmp_new,
                rdma_num_rails,
                MPI_LONG, comm_ptr->handle);

    /* Now allocate the rkey array for all other processes */
    (*win_ptr)->r_key = (uint32_t *) MPIU_Malloc(comm_size * 
                                              sizeof(uint32_t) 
                                                 * rdma_num_hcas);
    if (!(*win_ptr)->r_key) {
        DEBUG_PRINT("Error malloc win->r_key when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey2 array for all other processes */
    (*win_ptr)->r_key2 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)
                                                  * rdma_num_hcas);
    if (!(*win_ptr)->r_key2) {
        DEBUG_PRINT("Error malloc win->r_key2 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey3 array for all other processes */
    (*win_ptr)->r_key3 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)
                                                  * rdma_num_hcas);
    if (!(*win_ptr)->r_key3) {
        DEBUG_PRINT("error malloc win->r_key3 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->all_actlock_addr = (long long **) MPIU_Malloc(comm_size 
                                                              * sizeof(long long *) 
                                                              * rdma_num_hcas);
    /* Now allocate the passive communication array for all other processes */
    if (!(*win_ptr)->all_actlock_addr) {
        DEBUG_PRINT
            ("error malloc win->all_actlock_addr when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    
    /* Now allocate the completion counter array for all other processes */
    (*win_ptr)->all_completion_counter =
        (long long **) MPIU_Malloc(comm_size * sizeof(long long *) * rdma_num_rails);

    if (!(*win_ptr)->all_completion_counter) {
        DEBUG_PRINT
            ("error malloc win->all_completion_counter when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    int compIndex;

    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_hcas; ++j) 
        {
            compIndex = rdma_num_hcas * i + j;
            calcIndex = (rdma_num_hcas * i + j) * 7;
            (*win_ptr)->r_key[compIndex] = tmp[calcIndex];
            (*win_ptr)->r_key2[compIndex] = tmp[calcIndex + 1];
            (*win_ptr)->r_key3[compIndex] = tmp[calcIndex + 2];
            (*win_ptr)->all_actlock_addr[compIndex] = (void *)tmp[calcIndex + 3]; 
        }
    }

    for (i = 0; i < comm_size; ++i){
        for (j = 0; j < rdma_num_rails; ++j){
            arr_index = rdma_num_rails * i + j;     
            (*win_ptr)->all_completion_counter[arr_index] =
                (long long *) (tmp_new[arr_index] + sizeof(long long)*rank * rdma_num_rails); 
        }    
        
    }
       
    tmp1 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long)*rdma_num_hcas );
    if (!tmp1) {
        DEBUG_PRINT("Error malloc tmp1 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    tmp2 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long) );
    if (!tmp2) {
        DEBUG_PRINT("Error malloc tmp2 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    tmp3 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long)*rdma_num_hcas );
    if (!tmp3) {
        DEBUG_PRINT("Error malloc tmp3 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    tmp4 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long) );
    if (!tmp4) {
        DEBUG_PRINT("Error malloc tmp4 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; ++i)
    {
        (*win_ptr)->post_flag[i] = i != rank ? 0 : 1;

        for (j = 0; j < rdma_num_hcas; ++j)
        {
            tmp1[i * rdma_num_hcas + j] = postflag_rkey[j];
        }

        tmp2[i] = (uintptr_t) &((*win_ptr)->post_flag[i]);
    }

    /* use all to all to exchange rkey of post flag */ 
    ret = NMPI_Alltoall(tmp1, rdma_num_hcas, MPI_LONG, tmp3, rdma_num_hcas, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* use all to all to exchange the address of post flag */
    ret = NMPI_Alltoall(tmp2, 1, MPI_LONG, tmp4, 1, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->r_key4 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)*rdma_num_hcas);
    if (!(*win_ptr)->r_key4) {
        DEBUG_PRINT("error malloc win->r_key3 when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    
    (*win_ptr)->remote_post_flags =
        (long **) MPIU_Malloc(comm_size * sizeof(long *));
    if (!(*win_ptr)->remote_post_flags) {
        DEBUG_PRINT
            ("error malloc win->remote_post_flags when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    for (i = 0; i < comm_size; ++i) {
        for (j = 0; j < rdma_num_hcas; ++j){
            calcIndex = rdma_num_hcas * i + j; 
            (*win_ptr)->r_key4[calcIndex] = tmp3[calcIndex];
            DEBUG_PRINT("AFTER ALLTOALL the rank[%d] post_flag_key[%x]\n",
                    rank, (*win_ptr)->r_key4[calcIndex]);
        }
        (*win_ptr)->remote_post_flags[i] = (long *) tmp4[i];
        DEBUG_PRINT(" rank is %d remote rank %d,  post flag addr is %p\n",
                rank, i, (*win_ptr)->remote_post_flags[i]);
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
        DEBUG_PRINT("Fail to malloc space for window put get list\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }


    MPIDI_CH3I_RDMA_Process.win_index2address[index] = (long) *win_ptr;
    DEBUG_PRINT("done win_create\n");

fn_exit:
    if (1 == (*win_ptr)->fall_back) {
        MPIDI_CH3I_RDMA_Process.win_index2address[index] = 0;
        --MPIDI_CH3I_RDMA_Process.current_win_num;
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
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
            RDMA_local_win_dreg_entry[index]);
  err_base_register:
    tmp[7 * rank * rdma_num_hcas + 5] = (*win_ptr)->fall_back;

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 
                      rdma_num_hcas*7, MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    goto fn_exit;
     
}

void MPIDI_CH3I_RDMA_win_free(MPID_Win** win_ptr)
{
    int index = Find_Win_Index(*win_ptr);

    if (index == -1) {
        DEBUG_PRINT("dont know win_ptr %p %d \n", *win_ptr,
               MPIDI_CH3I_RDMA_Process.current_win_num);
    }

    MPIDI_CH3I_RDMA_Process.win_index2address[index] = 0;
    --MPIDI_CH3I_RDMA_Process.current_win_num;
    MPIU_Assert(MPIDI_CH3I_RDMA_Process.current_win_num >= 0);

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

    if (MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] != NULL)
    {
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
    int                 i;
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key2[MAX_NUM_SUBRAILS], l_key2[MAX_NUM_SUBRAILS];
    long long           *cc;
    int hca_index;
    void * remote_addr[MAX_NUM_SUBRAILS], *local_addr[MAX_NUM_SUBRAILS];

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    
    Get_Pinned_Buf(win_ptr, (void*) &cc, sizeof(long long));

    for (i=0; i<rdma_num_rails; ++i) { 
            hca_index = tmp_vc->mrail.rails[i].hca_index;
            remote_addr[i]    = (void *)(uintptr_t)
                (win_ptr->all_completion_counter[target_rank*rdma_num_rails+i]);
	    *((long long *) cc) = 1;
            local_addr[i]     = (void *)cc;
            l_key2[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
            r_key2[i] = win_ptr->r_key2[target_rank*rdma_num_hcas + hca_index];
    }
 
    Post_Put_Put_Get_List(win_ptr, -1, NULL, tmp_vc, 
                   local_addr, remote_addr, sizeof (long long), l_key2, r_key2,2);
    return mpi_errno;
    
}

static int Post_Put_Put_Get_List(  MPID_Win * winptr, 
                            int size, 
                            dreg_entry * dreg_tmp,
                            MPIDI_VC_t * vc_ptr,
                            void *local_buf[], void *remote_buf[],
                            int length,
                            uint32_t lkeys[], uint32_t rkeys[],
                            int use_multi)
{
    int i,mpi_errno = MPI_SUCCESS;
    /* int hca_index; */
    int rail;
    int index = winptr->put_get_list_tail;
    vbuf *v;
    MPIDI_VC_t *save_vc = vc_ptr;

    winptr->put_get_list[index].op_type     = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry   = dreg_tmp;
    winptr->put_get_list[index].data_size   = size;
    winptr->put_get_list[index].win_ptr     = winptr;
    winptr->put_get_list[index].vc_ptr      = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                    rdma_default_put_get_list_size;
    winptr->put_get_list[index].completion = 0;

    if (use_multi == 1) { /* stripe the message across rails */
        int posting_length;
        void *local_address, *remote_address;

        for (i = 0; i < rdma_num_rails; ++i) {
            v = get_vbuf(); 
            if (NULL == v) {
                MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
            }

            v->list = (void *)(&(winptr->put_get_list[index]));
            v->vc = (void *)vc_ptr;
#if 0
Trac #426
            hca_index = vc_ptr->mrail.rails[i].hca_index;
            ++(vc_ptr->mrail.rails[hca_index].postsend_times_1sc);
#endif
            ++(winptr->put_get_list[index].completion);
            ++(winptr->put_get_list_size);

            local_address = (void *)((char*)local_buf[0] +
                                     i * (size/rdma_num_rails));
            remote_address = (void *)((char *)remote_buf[0] +
                                      i * (size/rdma_num_rails));
            if (i < rdma_num_rails - 1) {
                posting_length = length / rdma_num_rails;
            } else {
                posting_length = size - (rdma_num_rails - 1) *
                                        (length / rdma_num_rails);
            }
            vbuf_init_rma_put(v, local_address, lkeys[i], remote_address,
                              rkeys[i], posting_length, i);

            if (vc_ptr->ch.state != MPIDI_CH3I_VC_STATE_IDLE
#ifdef _ENABLE_XRC_
                || (USE_XRC && VC_XST_ISUNSET (vc_ptr, XF_SEND_IDLE))
#endif
                || !MPIDI_CH3I_CM_One_Sided_SendQ_empty(vc_ptr)) {
                /* VC is not ready to be used. Wait till it is ready and send */
                MPIDI_CH3I_CM_One_Sided_SendQ_enqueue(vc_ptr, v);

                if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                    /* VC is not connected, initiate connection */
                    MPIDI_CH3I_CM_Connect(vc_ptr);
                }
            } else {
                XRC_FILL_SRQN_FIX_CONN (v, vc_ptr, i);
                if (MRAILI_Flush_wqe(vc_ptr,v,i) != -1) { /* message not enqueued */
                    -- vc_ptr->mrail.rails[i].send_wqes_avail;
                    IBV_POST_SR(v, vc_ptr, i, "Failed to post rma put");
                }
                vc_ptr = save_vc;
            }
        }
    } else if (use_multi == 0) { /* send on a single rail */
        rail = 0;
        v = get_vbuf(); 
        if (NULL == v) {
            MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
        }

        winptr->put_get_list[index].completion = 1;
        ++(winptr->put_get_list_size);
#if 0
Trac #426
        ++(vc_ptr->mrail.rails[rail].postsend_times_1sc);
#endif

        vbuf_init_rma_put(v, local_buf[rail], lkeys[rail], remote_buf[rail],
                          rkeys[rail], length, rail);
        v->list = (void *)(&(winptr->put_get_list[index]));
        v->vc = (void *)vc_ptr;

        if (vc_ptr->ch.state != MPIDI_CH3I_VC_STATE_IDLE 
#ifdef _ENABLE_XRC_
                || (USE_XRC && VC_XST_ISUNSET (vc_ptr, XF_SEND_IDLE))
#endif
	    || !MPIDI_CH3I_CM_One_Sided_SendQ_empty(vc_ptr)) {
            /* VC is not ready to be used. Wait till it is ready and send */
            MPIDI_CH3I_CM_One_Sided_SendQ_enqueue(vc_ptr, v);

            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                /* VC is not connected, initiate connection */
                MPIDI_CH3I_CM_Connect(vc_ptr);
            }
        } else {
            XRC_FILL_SRQN_FIX_CONN (v, vc_ptr, rail);
            if (MRAILI_Flush_wqe(vc_ptr, v, rail) != -1) {
                --(vc_ptr->mrail.rails[rail].send_wqes_avail);
                IBV_POST_SR(v, vc_ptr, rail, "Failed to post rma put");
            }
        }
    } else if (use_multi == 2) { /* send on all rails */
        for (i = 0; i < rdma_num_rails; ++i) {
            
            v = get_vbuf(); 
            if (NULL == v) {
                MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
            }

#if 0
Trac #426
            hca_index = vc_ptr->mrail.rails[i].hca_index;
            ++(vc_ptr->mrail.rails[hca_index].postsend_times_1sc);
#endif
            ++(winptr->put_get_list[index].completion);
            ++(winptr->put_get_list_size);

            vbuf_init_rma_put(v, local_buf[i], lkeys[i], remote_buf[i],
                              rkeys[i], length, i);
            v->list = (void *)(&(winptr->put_get_list[index]));
            v->vc = (void *)vc_ptr;
 
            if (vc_ptr->ch.state != MPIDI_CH3I_VC_STATE_IDLE
#ifdef _ENABLE_XRC_
                || (USE_XRC && VC_XST_ISUNSET (vc_ptr, XF_SEND_IDLE))
#endif
                || !MPIDI_CH3I_CM_One_Sided_SendQ_empty(vc_ptr)) {
                /* VC is not ready to be used. Wait till it is ready and send */
                MPIDI_CH3I_CM_One_Sided_SendQ_enqueue(vc_ptr, v);

                if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                    /* VC is not connected, initiate connection */
                    MPIDI_CH3I_CM_Connect(vc_ptr);
                }
            } else {
                XRC_FILL_SRQN_FIX_CONN (v, vc_ptr, i);
                if (MRAILI_Flush_wqe(vc_ptr, v ,i) != -1) {
                    --(vc_ptr->mrail.rails[i].send_wqes_avail);
                    IBV_POST_SR(v, vc_ptr, i, "Failed to post rma put");                     
       	        } 
                vc_ptr = save_vc;
       	    }
        }
    }
 
    while (winptr->put_get_list_size >= rdma_default_put_get_list_size)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    
fn_fail:
    return mpi_errno;
}


static int Post_Get_Put_Get_List(  MPID_Win * winptr, 
                            int size, 
                            dreg_entry * dreg_tmp,
                            MPIDI_VC_t * vc_ptr,
                            void *local_buf[], void *remote_buf[],
                            int length,
                            uint32_t lkeys[], uint32_t rkeys[],
                            int use_multi)
{
     int i, mpi_errno = MPI_SUCCESS;
     int hca_index;
     int index = winptr->put_get_list_tail;
     vbuf *v;
     MPIDI_VC_t *save_vc = vc_ptr;

     if(size <= rdma_eagersize_1sc){    
         winptr->put_get_list[index].origin_addr = remote_buf[0];
         winptr->put_get_list[index].target_addr = local_buf[0];
     } else {
         winptr->put_get_list[index].origin_addr = NULL;
         winptr->put_get_list[index].target_addr = NULL;
     }
     winptr->put_get_list[index].op_type     = SIGNAL_FOR_GET;
     winptr->put_get_list[index].mem_entry   = dreg_tmp;
     winptr->put_get_list[index].data_size   = size;
     winptr->put_get_list[index].win_ptr     = winptr;
     winptr->put_get_list[index].vc_ptr      = vc_ptr;
     winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                                   rdma_default_put_get_list_size;
     winptr->put_get_list[index].completion = 0;

     if (use_multi == 1) {
        int posting_length;
        char *local_address, *remote_address;

        for (i = 0; i < rdma_num_rails; ++i) {
            v = get_vbuf(); 
            if (NULL == v) {
                MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
            }

            local_address = (char*)local_buf[0] + i*(length/rdma_num_rails);
            remote_address = (char*)remote_buf[0] + i*(length/rdma_num_rails);
            if (i < rdma_num_rails - 1) {
                posting_length = length / rdma_num_rails;
            } else {
                posting_length = length - (rdma_num_rails - 1) *
                                          (length / rdma_num_rails);
            }

            vbuf_init_rma_get(v, local_address, lkeys[i], remote_address, 
                              rkeys[i], posting_length, i);
            v->list = (void *)(&(winptr->put_get_list[index]));
            v->vc = (void *)vc_ptr;

            hca_index = vc_ptr->mrail.rails[i].hca_index;
            ++(winptr->put_get_list[index].completion);
            ++(vc_ptr->mrail.rails[hca_index].postsend_times_1sc);
            ++(winptr->put_get_list_size);

            if ((vc_ptr->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
#ifdef _ENABLE_XRC_
                || (USE_XRC && VC_XST_ISUNSET (vc_ptr, XF_SEND_IDLE))
#endif
                || !MPIDI_CH3I_CM_One_Sided_SendQ_empty(vc_ptr)) {
                /* VC is not ready to be used. Wait till it is ready and send */
                MPIDI_CH3I_CM_One_Sided_SendQ_enqueue(vc_ptr, v);

                if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                    /* VC is not connected, initiate connection */
                    MPIDI_CH3I_CM_Connect(vc_ptr);
                }
            } else {
                XRC_FILL_SRQN_FIX_CONN (v, vc_ptr, i);
                if (MRAILI_Flush_wqe(vc_ptr,v,i) != -1) {
                    --(vc_ptr->mrail.rails[i].send_wqes_avail);
                    IBV_POST_SR(v, vc_ptr, i, "Failed to post rma get");
                }
                vc_ptr = save_vc;
            }
     	} 
    }

    while (winptr->put_get_list_size >= rdma_default_put_get_list_size)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

fn_fail:
    return mpi_errno;
}

int MRAILI_Handle_one_sided_completions(vbuf * v)                            
{
    dreg_entry      	          *dreg_tmp;
    int                           size;
    int                           mpi_errno = MPI_SUCCESS;
    void                          *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list  *list_entry=NULL;
    MPID_Win                      *list_win_ptr;
    MPIDI_VC_t                    *list_vc_ptr;
    int                           rail = v->rail;       
    list_entry = (MPIDI_CH3I_RDMA_put_get_list *)v->list;
    list_win_ptr = list_entry->win_ptr;
    list_vc_ptr = list_entry->vc_ptr;

    switch (list_entry->op_type) {
    case (SIGNAL_FOR_PUT):
        {
            dreg_tmp = list_entry->mem_entry;
            size = list_entry->data_size;

            if (size > (int)rdma_eagersize_1sc) {
                --(list_entry->completion);
                if (list_entry->completion == 0) {
                    dreg_unregister(dreg_tmp);
                }
            }
            --(list_win_ptr->put_get_list_size);
            --(list_vc_ptr->mrail.rails[rail].postsend_times_1sc);
            break;
        }
    case (SIGNAL_FOR_GET):
        {
            size = list_entry->data_size;
            target_addr = list_entry->target_addr;
            origin_addr = list_entry->origin_addr;
            dreg_tmp = list_entry->mem_entry;

            if (origin_addr == NULL) {
                MPIU_Assert(size > rdma_eagersize_1sc);
                --(list_entry->completion);
                if (list_entry->completion == 0)
                    dreg_unregister(dreg_tmp);
            } else {
                MPIU_Assert(size <= rdma_eagersize_1sc);
                MPIU_Assert(target_addr != NULL);
                MPIU_Memcpy(target_addr, origin_addr, size);
            }
            --(list_win_ptr->put_get_list_size);
            --(list_vc_ptr->mrail.rails[rail].postsend_times_1sc);
            break;
        }
    default:
            MPIU_ERR_SETSIMPLE(mpi_errno, MPI_ERR_OTHER, "**onesidedcomps");
            break;
    }
    
    if (list_win_ptr->put_get_list_size == 0) 
        list_win_ptr->put_get_list_tail = 0;

    if (list_win_ptr->put_get_list_size == 0){
        list_win_ptr->rma_issued = 0;
        list_win_ptr->pinnedpool_1sc_index = 0;
        list_win_ptr->poll_flag = 0;
     }
#ifndef _OSU_MVAPICH_
fn_fail:
#endif
    return mpi_errno;
}

static int iba_put(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_SUBRAILS],
                        l_key1[MAX_NUM_SUBRAILS], 
                        l_key[MAX_NUM_SUBRAILS];
    int                 i;
    dreg_entry          *tmp_dreg = NULL;
    char                *origin_addr;

    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    /*part 1 prepare origin side buffer target buffer and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank]
        + win_ptr->disp_units[rma_op->target_rank]
        * rma_op->target_disp;

 
    if (size <= rdma_eagersize_1sc) {
        char *tmp = rma_op->origin_addr;

        Get_Pinned_Buf(win_ptr, &origin_addr, size);
        MPIU_Memcpy(origin_addr, tmp, size);

        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);

        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }        

        origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }
    
    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

    for (i=0; i<rdma_num_rails; ++i) {
        hca_index = tmp_vc->mrail.rails[i].hca_index;
        r_key[i] = win_ptr->r_key[rma_op->target_rank*rdma_num_hcas + hca_index];
        l_key1[i] = l_key[hca_index];
    }

    Post_Put_Put_Get_List(win_ptr, size, tmp_dreg, tmp_vc, 
            (void *)&origin_addr, (void *)&remote_address, 
            size, l_key1, r_key, 1 );

    return mpi_errno;
}


int iba_get(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_SUBRAILS], 
                        l_key1[MAX_NUM_SUBRAILS], 
                        l_key[MAX_NUM_SUBRAILS];
    dreg_entry          *tmp_dreg = NULL;
    char                *origin_addr;
    int                 i;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    MPIU_Assert(rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys  */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        Get_Pinned_Buf(win_ptr, &origin_addr, size);
    for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
    for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }
        origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

   for (i=0; i<rdma_num_rails; ++i)
   {
       hca_index = tmp_vc->mrail.rails[i].hca_index;
       r_key[i] = win_ptr->r_key[rma_op->target_rank*rdma_num_hcas + hca_index];
       l_key1[i] = l_key[hca_index];
   }

   Post_Get_Put_Get_List(win_ptr, size, tmp_dreg, 
                         tmp_vc, (void *)&origin_addr, 
                         (void *)&remote_address, size, 
                         l_key1, r_key, 1 );

   return mpi_errno;
}
