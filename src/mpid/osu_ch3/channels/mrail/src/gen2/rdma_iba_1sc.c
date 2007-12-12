/* Copyright (c) 2003-2007, The Ohio State University. All rights
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
#include <math.h>

#include "rdma_impl.h"
#include "dreg.h"
#include "ibv_param.h"
#include "infiniband/verbs.h"
#include "mpidrma.h"
#include "pmi.h"


#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

#define ASSERT assert
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

static int Consume_signals(MPID_Win *, uint64_t);
static int iba_put(MPIDI_RMA_ops *, MPID_Win *, int);
static int iba_get(MPIDI_RMA_ops *, MPID_Win *, int);
static void Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size);
int     iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
int     iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);


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
    int flag = 0, src, i, counter = 0;

#ifdef _SMP_
    if (SMP_INIT)
        MPID_Comm_get_ptr( win_ptr->comm, comm_ptr );
#endif
    while (flag == 0 && start_grp_size != 0) {

        /* need to make sure we make some progress on
         * anything in the extended sendq or coalesced
         * or we can have a deadlock 
         */
        if(counter++ % 200 == 0) {
            MPIDI_CH3I_Progress_test();
        }

        for (i = 0; i < start_grp_size; i++) {
            flag = 1;
            src = ranks_in_win_grp[i];  /*src is the rank in comm*/
#ifdef _SMP_
         if (SMP_INIT) {
            MPIDI_Comm_get_vc(comm_ptr, src, &vc);
            if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
            {
                /*correspoding post has not been issued */
                flag = 0;
                break;
            }

         } else if (win_ptr->post_flag[src] == 0)
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
  if (SMP_INIT)
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
     if(SMP_INIT) {
        MPIDI_Comm_get_vc(comm_ptr, curr_ptr->target_rank, &vc);

        if (vc->smp.local_nodes != -1)  {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }
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
 
                    if (!origin_dt_derived && !target_dt_derived && size > 
                            rdma_put_fallback_threshold) {
#ifdef _SCHEDULE
                        if (curr_put != 1 || force_to_progress == 1)  { 
                        /* nearest issued rma is not a put*/
#endif
                            win_ptr->rma_issued ++;
                            iba_put(curr_ptr, win_ptr, size);
                            if (head_ptr == curr_ptr) {
                                prev_ptr = head_ptr = curr_ptr->next;
                            } else
                                prev_ptr->next = curr_ptr->next;

                            tmp_ptr = curr_ptr->next;
                            MPIU_Free(curr_ptr);
                            curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                            curr_put = 1;
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
                    prev_ptr = curr_ptr;
                    curr_ptr = curr_ptr->next;
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
                        iba_get(curr_ptr, win_ptr, size);
                        if (head_ptr == curr_ptr)
                            prev_ptr = head_ptr = curr_ptr->next;
                        else
                            prev_ptr->next = curr_ptr->next;
                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                            curr_put = 0;
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
                DEBUG_PRINT("Unknown ONE SIDED OP\n");
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
    int         hca_index; 
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;
    /*part 1 prepare origin side buffer */
    remote_address = (char *) win_ptr->remote_post_flags[target_rank];
    size = sizeof(int);
    Get_Pinned_Buf(win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    hca_index = 0;
    l_key = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
    r_key = win_ptr->r_key4[target_rank * rdma_num_hcas + hca_index];

    Post_Put_Put_Get_List(win_ptr, -1, NULL, 
            tmp_vc, (void *)&origin_addr, 
            (void*)&remote_address, size, 
            &l_key, &r_key, 0 );

     Consume_signals(win_ptr, 0);
}

void
MPIDI_CH3I_RDMA_win_create(void *base,
                           MPI_Aint size,
                           int comm_size,
                           int rank,
                           MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
    int ret, i,j, index;
    unsigned long   *tmp, *tmp_new;
    uint32_t        r_key[MAX_NUM_HCAS], r_key2[MAX_NUM_HCAS];
    uint32_t        r_key3[MAX_NUM_HCAS], postflag_rkey[MAX_NUM_HCAS];
    int             my_rank;
    unsigned long   *tmp1, *tmp2, *tmp3, *tmp4;
    int             fallback_trigger = 0;

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided) {
    (*win_ptr)->fall_back = 1;
    return;
    }
    
    PMI_Get_rank(&my_rank);
    
    /*There may be more than one windows existing at the same time */
   
    MPIDI_CH3I_RDMA_Process.current_win_num++;
    ASSERT(MPIDI_CH3I_RDMA_Process.current_win_num <= MAX_WIN_NUM);
    index = Find_Avail_Index();
    ASSERT(index != -1);

    tmp = MPIU_Malloc(comm_size * sizeof(unsigned long) * 
            7 * rdma_num_hcas);

    if (!tmp) {
        DEBUG_PRINT("Error malloc tmp when creating windows\n");
        exit(0);
    }
    
    tmp_new = MPIU_Malloc(comm_size * sizeof(unsigned long) *
              rdma_num_rails);
   
    if (!tmp_new) {
        DEBUG_PRINT("Error malloc tmp_new when creating windows\n");
        exit(0);
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
        for (i=0; i < rdma_num_hcas; i++) {
             r_key[i] = MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry
                 [index]->memhandle[i]->rkey;
        }
    } else {
        /* Self */
        MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] = NULL;
        for (i = 0; i < rdma_num_hcas; i++) {
             r_key[i] = 0;
        }
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter = 
        MPIU_Malloc(sizeof(long long) * comm_size * rdma_num_rails);

    /* Fallback case */
    if (!(*win_ptr)->completion_counter) {
        (*win_ptr)->fall_back = 1;
        goto err_cc_buf;
    }

    memset((*win_ptr)->completion_counter, 0, 
            sizeof(long long) * comm_size * rdma_num_rails); 

    MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] =
        dreg_register((void *) (*win_ptr)->completion_counter,
                          sizeof(long long) * comm_size * rdma_num_rails);

    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_register;
    }

    for (i = 0; i < rdma_num_hcas; i++) {
        r_key2[i] =
            MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]->
            memhandle[i]->rkey;
    }

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
    
    for (i=0; i<rdma_num_hcas; i++) {
        r_key3[i] =
            MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]->
            memhandle[i]->rkey;
         DEBUG_PRINT("the rkey3 is %x\n", r_key3[i]);
    }
    *((long long *) ((*win_ptr)->actlock)) = 0;

    /*Register buffer for post flags : from target to origin */
    
    (*win_ptr)->post_flag = (int *) MPIU_Malloc(comm_size 
                                                * sizeof(int));
    if (!(*win_ptr)->post_flag) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_buf;
    }

    DEBUG_PRINT("rank[%d] : post flag start before exchange is %p\n",
            rank,(*win_ptr)->post_flag);   
    /* Register the post flag */

    MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] =
        dreg_register((void *) (*win_ptr)->post_flag, sizeof(int) * comm_size);
    
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]) {
        /* Fallback case */
        (*win_ptr)->fall_back = 1;
        goto err_postflag_register;
    }

    for (i = 0; i < rdma_num_hcas; i++) {
        postflag_rkey[i] =
            MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]->
            memhandle[i]->rkey;
        DEBUG_PRINT("the rank [%d] postflag_rkey before exchange is %x\n", 
                rank,postflag_rkey[i]);
    }

    /* Malloc Pinned buffer for one sided communication */
    (*win_ptr)->pinnedpool_1sc_buf = MPIU_Malloc(rdma_pin_pool_size);
    if (!(*win_ptr)->pinnedpool_1sc_buf) {
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_buf;
    }
    
    (*win_ptr)->pinnedpool_1sc_dentry = 
        dreg_register((*win_ptr)->pinnedpool_1sc_buf,
                    rdma_pin_pool_size);
          
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_register;
    }
     
    (*win_ptr)->pinnedpool_1sc_index = 0;

    /*Exchange the information about rkeys and addresses */
    
    for (i = 0; i < rdma_num_hcas; i++){
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 0] = r_key[i];
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 1] = r_key2[i];
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 2] = r_key3[i];
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 3] = 
            (uintptr_t) ((*win_ptr)->actlock);
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 4] = 
            (uintptr_t) ((*win_ptr)->completion_counter 
                         + comm_size * i);
        tmp[(7 * rank * rdma_num_hcas) + i * 7 + 5] = (*win_ptr)->fall_back;
    }
#ifdef DEBUG 
    for(i = 0 ; i < rdma_num_hcas; i++) {
        DEBUG_PRINT("Before Allgather, rank[%d], r_key[%x], "
                "r_key2[%x], r_key3[%x], comp_counter[%x], fall_back[%d]\n",
                my_rank,
                tmp[(7 * rank * rdma_num_hcas) + i * 7 + 0],
                tmp[(7 * rank * rdma_num_hcas) + i * 7 + 1],
                tmp[(7 * rank * rdma_num_hcas) + i * 7 + 2],
                tmp[(7 * rank * rdma_num_hcas) + i * 7 + 4],
                tmp[(7 * rank * rdma_num_hcas) + i * 7 + 5]);
    }
#endif
    /* All processes will exchange the setup keys *
     * since each process has the same data for all other
     * processes, use allgather */

    ret =
        NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 
                7 * rdma_num_hcas,
                MPI_LONG, comm_ptr->handle);

    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        exit(0);
    }

    /* check if any peers fail */
     for (i = 0; i < comm_size; i++) {
          for (j = 0; j < rdma_num_hcas; j++){
               if (tmp[(7 * i *rdma_num_hcas) + j * 7 + 5] != 0)
		       fallback_trigger = 1;
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

    for (i = 0; i < rdma_num_rails; i++){
         tmp_new[rank * rdma_num_rails + i] = 
             (uintptr_t) ((*win_ptr)->completion_counter + comm_size * i);
        
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
        exit(0);
    }

    /* Now allocate the rkey2 array for all other processes */
    (*win_ptr)->r_key2 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)
                                                  * rdma_num_hcas);
    if (!(*win_ptr)->r_key2) {
        DEBUG_PRINT("Error malloc win->r_key2 when creating windows\n");
        exit(0);
    }

    /* Now allocate the rkey3 array for all other processes */
    (*win_ptr)->r_key3 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)
                                                  * rdma_num_hcas);
    if (!(*win_ptr)->r_key3) {
        DEBUG_PRINT("error malloc win->r_key3 when creating windows\n");
        exit(0);
    }

    (*win_ptr)->all_actlock_addr = (long long **) MPIU_Malloc(comm_size 
                                                              * sizeof(long long *) 
                                                              * rdma_num_hcas);
    /* Now allocate the passive communication array for all other processes */
    if (!(*win_ptr)->all_actlock_addr) {
        DEBUG_PRINT
            ("error malloc win->all_actlock_addr when creating windows\n");
        exit(0);
    }
    
    /* Now allocate the completion counter array for all other processes */
    (*win_ptr)->all_completion_counter =
        (long long **) MPIU_Malloc(comm_size * sizeof(long long *) * rdma_num_rails);

    if (!(*win_ptr)->all_completion_counter) {
        DEBUG_PRINT
            ("error malloc win->all_completion_counter when creating windows\n");
        exit(0);
    }



    for (i = 0; i < comm_size; i++) {
        for (j = 0; j < rdma_num_hcas; j++) 
        {
            (*win_ptr)->r_key[i * rdma_num_hcas + j] = 
                tmp[(7 * i * rdma_num_hcas) + 7 * j];
            (*win_ptr)->r_key2[i * rdma_num_hcas + j] = 
                tmp[(7 * i * rdma_num_hcas) + 7 * j + 1];
            (*win_ptr)->r_key3[i * rdma_num_hcas + j] = 
                tmp[(7 * i * rdma_num_hcas) + 7 * j + 2];
            (*win_ptr)->all_actlock_addr[i * rdma_num_hcas + j] = 
                                (long long *) tmp[7 * i *rdma_num_hcas + 7 * j + 3];

        }
    }

    for (i = 0; i < comm_size; i++){
        for (j = 0; j < rdma_num_rails; j++){
            (*win_ptr)->all_completion_counter[i * rdma_num_rails + j] = 
                (long long *) (tmp_new[i * rdma_num_rails + j] + 
                               sizeof(long long) * rank);
        }    
        
    }
    
#ifdef DEBUG
    for(i = 0 ; i < comm_size; i++) {
        if(i == my_rank)
            continue;
        for(j = 0 ; j < rdma_num_hcas; j++) {
            DEBUG_PRINT("After Allgather, rank[%d], r_key[%x], "
                    "r_key2[%x], r_key3[%x], comp_counter[%x]\n",
                    i,
                    (*win_ptr)->r_key[i * rdma_num_hcas + j],
                    (*win_ptr)->r_key2[i * rdma_num_hcas + j], 
                    (*win_ptr)->r_key3[i * rdma_num_hcas + j],
                   (*win_ptr)->all_completion_counter[i * rdma_num_hcas + j]); 
        }
    }
#endif
    
    tmp1 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long)*rdma_num_hcas );
    if (!tmp1) {
        DEBUG_PRINT("Error malloc tmp1 when creating windows\n");
        exit(0);
    }
    tmp2 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long) );
    if (!tmp2) {
        DEBUG_PRINT("Error malloc tmp2 when creating windows\n");
        exit(0);
    }
    tmp3 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long)*rdma_num_hcas );
    if (!tmp3) {
        DEBUG_PRINT("Error malloc tmp3 when creating windows\n");
        exit(0);
    }
    tmp4 = (unsigned long *) MPIU_Malloc(comm_size * 
            sizeof(unsigned long) );
    if (!tmp4) {
        DEBUG_PRINT("Error malloc tmp4 when creating windows\n");
        exit(0);
    }
    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; i++) {
            if (i != rank)
                (*win_ptr)->post_flag[i] = 0;
            else
                (*win_ptr)->post_flag[i] = 1;
            
            for(j = 0; j < rdma_num_hcas; j++) { 
                    tmp1[i * rdma_num_hcas + j] = postflag_rkey[j];
                } 
            tmp2[i] = (uintptr_t) &((*win_ptr)->post_flag[i]);
         
    }

    /* use all to all to exchange rkey of post flag */ 
    ret =
        NMPI_Alltoall(tmp1, rdma_num_hcas, MPI_LONG, tmp3, rdma_num_hcas, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        exit(0);
    }

    /* use all to all to exchange the address of post flag */
    ret =
        NMPI_Alltoall(tmp2, 1, MPI_LONG, tmp4, 1, MPI_LONG,
                      comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        exit(0);
    }

    (*win_ptr)->r_key4 = (uint32_t *) MPIU_Malloc(comm_size * 
                                                  sizeof(uint32_t)*rdma_num_hcas);
    if (!(*win_ptr)->r_key4) {
        DEBUG_PRINT("error malloc win->r_key3 when creating windows\n");
        exit(0);
    }
    
    (*win_ptr)->remote_post_flags =
        (long **) MPIU_Malloc(comm_size * sizeof(long *));
    if (!(*win_ptr)->remote_post_flags) {
        DEBUG_PRINT
            ("error malloc win->remote_post_flags when creating windows\n");
        exit(0);
    }
    for (i = 0; i < comm_size; i++) {
        for (j = 0; j < rdma_num_hcas; j++){
            (*win_ptr)->r_key4[i*rdma_num_hcas + j] = tmp3[i*rdma_num_hcas + j];
            DEBUG_PRINT("AFTER ALLTOALL the rank[%d] post_flag_key[%x]\n",
                    rank, (*win_ptr)->r_key4[i*rdma_num_hcas + j]);
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
        exit(0);
    }


    MPIDI_CH3I_RDMA_Process.win_index2address[index] = (long) *win_ptr;
    DEBUG_PRINT("done win_create\n");

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
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
            RDMA_local_win_dreg_entry[index]);
  err_base_register:
    tmp[7 * rank + 6] = (*win_ptr)->fall_back;

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 7,
                       MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather rkey  when creating windows\n");
        exit(0);
    }
    
    return;
}

void MPIDI_CH3I_RDMA_win_free(MPID_Win ** win_ptr)
{
    int index;
    index = Find_Win_Index(*win_ptr);
    if (index == -1) {
        DEBUG_PRINT("dont know win_ptr %p %d \n", *win_ptr,
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
    int                 i;
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key2[MAX_NUM_HCAS], l_key2[MAX_NUM_HCAS];
    long long           *cc;
    int hca_index;
    void * remote_addr[MAX_NUM_SUBRAILS], *local_addr[MAX_NUM_SUBRAILS];

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    
    Get_Pinned_Buf(win_ptr, (void*) &cc, sizeof(long long));

    *((long long *) cc) = 1;
    for (i=0; i<rdma_num_rails; i++) { 
            hca_index = tmp_vc->mrail.rails[i].hca_index;
            remote_addr[i]    = (void *)(uintptr_t)
                (win_ptr->all_completion_counter[target_rank*rdma_num_rails+i]);
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
    int ret, i;
    struct ibv_send_wr   send_wr;
    struct ibv_sge      sg_entry;
    struct ibv_send_wr * bad_wr;
    int hca_index;
    int total_sent =0;
    int rail;
    int index = winptr->put_get_list_tail;
   
    winptr->put_get_list[index].op_type     = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry   = dreg_tmp;
    winptr->put_get_list[index].data_size   = size;
    winptr->put_get_list[index].win_ptr     = winptr;
    winptr->put_get_list[index].vc_ptr      = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                    rdma_default_put_get_list_size;
    winptr->put_get_list[index].completion = 0;
    
    if (use_multi == 1) {
        for (i = 0; i < rdma_num_rails; i ++) {
            hca_index = vc_ptr->mrail.rails[i].hca_index;
            winptr->put_get_list[index].completion++;
            vc_ptr->mrail.rails[hca_index].postsend_times_1sc ++;
            winptr->put_get_list_size++;

            send_wr.wr_id = (uintptr_t)(&(winptr->put_get_list[index])); 
            send_wr.next    = NULL;
#ifdef _IBM_EHCA_
            send_wr.send_flags             = IBV_SEND_SIGNALED;
#else
            if (length < sizeof(long long))
                send_wr.send_flags             = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
            else 
                send_wr.send_flags             = IBV_SEND_SIGNALED;
#endif
            send_wr.opcode                 = IBV_WR_RDMA_WRITE;
            send_wr.num_sge                = 1;
            if (i < (rdma_num_rails -1) )
            {
                sg_entry.length             = length/rdma_num_rails;
                total_sent                  = total_sent + sg_entry.length;
                sg_entry.addr               = (uintptr_t) 
                    ( local_buf[0] + i*(size/rdma_num_rails) );
                send_wr.wr.rdma.remote_addr = (uintptr_t) 
                    ( remote_buf[0] + i*(size/rdma_num_rails) );
            }
            else
            {
                sg_entry.length             = size - total_sent;
                sg_entry.addr               = (uintptr_t) 
                    ( local_buf[0] + i*(size/rdma_num_rails) );
                send_wr.wr.rdma.remote_addr = (uintptr_t) 
                    ( remote_buf[0] + i*(size/rdma_num_rails) );
            }
            send_wr.wr.rdma.rkey           = rkeys[i];
            send_wr.sg_list                = &(sg_entry);
            sg_entry.lkey                  = lkeys[i];
            
            DEBUG_PRINT("post one,id is %08X, sge %d, send flag %d, size %d, "
                    "1sc_qp_hndl is %x,1sc_qp_num is %08X\n",(uint32_t)send_wr.wr_id, 
                    send_wr.num_sge, send_wr.send_flags, 
                    send_wr.sg_list->length, vc_ptr->mrail.rails[i].qp_hndl_1sc,
                    vc_ptr->mrail.rails[i].qp_hndl_1sc->qp_num); 
            
            ret = ibv_post_send(vc_ptr->mrail.rails[i].qp_hndl_1sc, &send_wr, &bad_wr);
            CHECK_RETURN(ret, "Fail in posting RDMA_Write");

        }
    } else if (use_multi == 0) {
        winptr->put_get_list[index].completion = 1;
        rail = 0;
        winptr->put_get_list_size++;
        vc_ptr->mrail.rails[rail].postsend_times_1sc ++;
        send_wr.wr_id = (uintptr_t)(&(winptr->put_get_list[index]));
        send_wr.next    = NULL;

#ifdef _IBM_EHCA_
        send_wr.send_flags             = IBV_SEND_SIGNALED;
#else
        if (length < sizeof(long long))
             send_wr.send_flags             = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
        else 
            send_wr.send_flags             = IBV_SEND_SIGNALED;
#endif
        send_wr.opcode                 = IBV_WR_RDMA_WRITE;
        send_wr.num_sge                = 1;
        send_wr.wr.rdma.remote_addr    = (uintptr_t)remote_buf[rail];
        send_wr.wr.rdma.rkey           = rkeys[rail];
        send_wr.sg_list                = &(sg_entry);
        sg_entry.length             = length;
        sg_entry.lkey               = lkeys[rail];
        sg_entry.addr               = (uintptr_t)local_buf[rail];
            
        DEBUG_PRINT("post one,id is %08X, sge %d, send flag %d, "
                "size %d, 1sc_qp_hndl is %x,1sc_qp_num is %08X\n",
                (uint32_t)send_wr.wr_id, send_wr.num_sge, 
                send_wr.send_flags, send_wr.sg_list->length, 
                vc_ptr->mrail.rails[rail].qp_hndl_1sc,
                vc_ptr->mrail.rails[rail].qp_hndl_1sc->qp_num); 
            
        ret = ibv_post_send(vc_ptr->mrail.rails[rail].qp_hndl_1sc, 
                &send_wr, &bad_wr);

        CHECK_RETURN(ret, "Fail in posting RDMA_Write");
    }

    else if (use_multi == 2) {
        for (i = 0; i < rdma_num_rails; i ++) {
             hca_index = vc_ptr->mrail.rails[i].hca_index;
             winptr->put_get_list[index].completion++;
             vc_ptr->mrail.rails[hca_index].postsend_times_1sc ++;
             winptr->put_get_list_size++;
                  
             send_wr.wr_id = (uintptr_t)(&(winptr->put_get_list[index])); 
             send_wr.next    = NULL;
#ifdef _IBM_EHCA_
             send_wr.send_flags             = IBV_SEND_SIGNALED;
#else                    
             if (length < sizeof(long long) )
                 send_wr.send_flags          = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
             else
                 send_wr.send_flags          = IBV_SEND_SIGNALED;
#endif
             send_wr.opcode                 = IBV_WR_RDMA_WRITE;
             send_wr.num_sge                = 1;
             send_wr.wr.rdma.remote_addr    = (uintptr_t)remote_buf[i];
             send_wr.wr.rdma.rkey           = rkeys[i];
             send_wr.sg_list                = &(sg_entry);
             sg_entry.length             = length;
             sg_entry.lkey               = lkeys[i];
             sg_entry.addr               = (uintptr_t)local_buf[i];

             DEBUG_PRINT("post one,id is %08X, sge %d, send flag %d, "
                     "size %d, 1sc_qp_hndl is %x,1sc_qp_num is %08X\n",
                     (uint32_t)send_wr.wr_id, send_wr.num_sge, 
                     send_wr.send_flags, send_wr.sg_list->length, 
                     vc_ptr->mrail.rails[i].qp_hndl_1sc,
                     vc_ptr->mrail.rails[i].qp_hndl_1sc->qp_num); 
  
             ret = ibv_post_send(vc_ptr->mrail.rails[i].qp_hndl_1sc, 
                     &send_wr, &bad_wr);
             CHECK_RETURN(ret, "Fail in posting RDMA_Write");
                  
         }


    }     
    for (i = 0; i < rdma_num_rails; i ++) {
        if (winptr->put_get_list_size == rdma_default_put_get_list_size
            || vc_ptr->mrail.rails[i].postsend_times_1sc == rdma_default_max_wqe - 1) {
            Consume_signals(winptr, 0);
        }
    }
    return index;
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
     int ret, i;
     struct ibv_send_wr   send_wr;
     struct ibv_sge      sg_entry;
     struct ibv_send_wr * bad_wr;
     int hca_index;
     int total_recv = 0;
     int index = winptr->put_get_list_tail;

     if(size <= rdma_eagersize_1sc){    
         winptr->put_get_list[index].origin_addr = remote_buf[0];
         winptr->put_get_list[index].target_addr = local_buf[0];
     }
     else {
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
        for (i = 0; i < rdma_num_rails; i ++) {
            hca_index = vc_ptr->mrail.rails[i].hca_index;
            winptr->put_get_list[index].completion++;
            vc_ptr->mrail.rails[hca_index].postsend_times_1sc ++;
            winptr->put_get_list_size++;

            send_wr.wr_id = (uintptr_t)(&(winptr->put_get_list[index])); 
            send_wr.next    = NULL;
#ifdef _IBM_EHCA_
            send_wr.send_flags             = IBV_SEND_SIGNALED;
#else
            if (length < sizeof(long long))
                send_wr.send_flags             = IBV_SEND_SIGNALED | IBV_SEND_INLINE;
            else 
                send_wr.send_flags             = IBV_SEND_SIGNALED;
#endif
            send_wr.opcode                 = IBV_WR_RDMA_READ;
            send_wr.num_sge                = 1;

            if (i < (rdma_num_rails -1) )
            {
                sg_entry.length             = length/rdma_num_rails;
                total_recv                  = total_recv + sg_entry.length;
                sg_entry.addr               = (uintptr_t) 
                    ( local_buf[0] + i*(size/rdma_num_rails) );
                send_wr.wr.rdma.remote_addr = (uintptr_t) 
                    ( remote_buf[0] + i*(size/rdma_num_rails) );
            }
            else
            {
                sg_entry.length             = size - total_recv;
                sg_entry.addr               = (uintptr_t) 
                    ( local_buf[0] + i*(size/rdma_num_rails) );
                send_wr.wr.rdma.remote_addr = (uintptr_t) 
                    ( remote_buf[0] + i*(size/rdma_num_rails) );
            }
            send_wr.wr.rdma.rkey           = rkeys[i];
            send_wr.sg_list                = &(sg_entry);
            sg_entry.lkey               = lkeys[i];
            
            DEBUG_PRINT("post one,id is %08X, sge %d, send flag %d, "
                    "size %d, 1sc_qp_hndl is %x,1sc_qp_num is %08X\n",
                    (uint32_t)send_wr.wr_id, send_wr.num_sge, 
                    send_wr.send_flags, send_wr.sg_list->length, 
                    vc_ptr->mrail.rails[i].qp_hndl_1sc,
                    vc_ptr->mrail.rails[i].qp_hndl_1sc->qp_num); 
            
            ret = ibv_post_send(vc_ptr->mrail.rails[i].qp_hndl_1sc, 
                    &send_wr, &bad_wr);
            CHECK_RETURN(ret, "Fail in posting RDMA_Write");

        }
    }

    for (i = 0; i < rdma_num_rails; i ++) {
        if (winptr->put_get_list_size == rdma_default_put_get_list_size
            || vc_ptr->mrail.rails[i].postsend_times_1sc == 
            rdma_default_max_wqe - 1) {
            Consume_signals(winptr, 0);
        }
    }
    return index;

}

                            

/*if signal == -1, cousume all the signals, 
  otherwise return when signal is found*/
static int Consume_signals(MPID_Win * winptr, uint64_t expected)
{
    struct ibv_wc   wc;
    dreg_entry      *dreg_tmp;
    int             i = 0,j, size;
    int             ne;
    void            *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list  *list_entry=NULL;
    MPID_Win                      *list_win_ptr;
    MPIDI_VC_t                    *list_vc_ptr;

    while (1) {
      
       for (j=0; j < rdma_num_hcas; j++)
       {
         ne = ibv_poll_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[j], 1, &wc);
         if (ne > 0) {
            i++;
            if (wc.status != IBV_WC_SUCCESS) {
                ibv_va_error_abort(IBV_STATUS_ERR, "in Consume_signals "
                        "%08u get wrong status %d \n",
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
                    list_entry->completion--;
                    if (list_entry->completion == 0) 
                        dreg_unregister(dreg_tmp);
                }
                list_win_ptr->put_get_list_size --;
                list_vc_ptr->mrail.rails[j].postsend_times_1sc --;
            } else if (list_entry->op_type == SIGNAL_FOR_GET) {
                size = list_entry->data_size;
                target_addr = list_entry->target_addr;
                origin_addr = list_entry->origin_addr;
                dreg_tmp = list_entry->mem_entry;
                if (origin_addr == NULL) {
                    ASSERT(size > rdma_eagersize_1sc);
                    list_entry->completion--;
                    if (list_entry->completion == 0)
                        dreg_unregister(dreg_tmp);
                } else {
                    ASSERT(size <= rdma_eagersize_1sc);
                    ASSERT(target_addr != NULL);
                    memcpy(target_addr, origin_addr, size);
                }
                list_win_ptr->put_get_list_size --;
                list_vc_ptr->mrail.rails[j].postsend_times_1sc --;
            } else {
                fprintf(stderr, "Error! rank %d, Undefined op_type, op type %d, \
                list id %p, expecting id %lu\n",
                winptr->my_id, list_entry->op_type, list_entry, 
                expected);
                exit(0);
            }
        } else if (ne < 0) {
            ibv_error_abort(IBV_STATUS_ERR, 
                    "Fail to poll one sided completion queue\n");
        }


        if (winptr->put_get_list_size == 0) 
            winptr->put_get_list_tail = 0;
        if (winptr->put_get_list_size == 0
            && (expected == 0)) {
            winptr->rma_issued = 0;
            winptr->pinnedpool_1sc_index = 0;

            DEBUG_PRINT("exiting from the 1sc\n");

            goto fn_exit;
        }
     } /*end of for */
    } /* end of while */
  fn_exit:
    return 0;
}

static int iba_put(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_HCAS],
                        l_key1[MAX_NUM_HCAS], 
                        l_key[MAX_NUM_HCAS];
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
        memcpy(origin_addr, tmp, size);
    for(i = 0; i < rdma_num_hcas; i++) {
        l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
    }
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
    for(i = 0; i < rdma_num_hcas; i++) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
    }        
    origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }
    
    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

    for (i=0; i<rdma_num_rails; i++) {
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
    uint32_t            r_key[MAX_NUM_HCAS], 
                        l_key1[MAX_NUM_HCAS], 
                        l_key[MAX_NUM_HCAS];
    int                 index;
    dreg_entry          *tmp_dreg = NULL;
    char                *origin_addr;
    int                 i;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    assert(rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys  */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        Get_Pinned_Buf(win_ptr, &origin_addr, size);
    for(i = 0; i < rdma_num_hcas; i++) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
    for(i = 0; i < rdma_num_hcas; i++) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }
        origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

   for (i=0; i<rdma_num_rails; i++)
   {
       hca_index = tmp_vc->mrail.rails[i].hca_index;
       r_key[i] = win_ptr->r_key[rma_op->target_rank*rdma_num_hcas + hca_index];
       l_key1[i] = l_key[hca_index];
   }

   index = Post_Get_Put_Get_List(win_ptr, size, tmp_dreg, 
           tmp_vc, (void *)&origin_addr, 
           (void *)&remote_address, size, 
           l_key1, r_key, 1 );
   return mpi_errno;
}
