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

#include "mpidi_ch3i_rdma_conf.h"
#include <mpimem.h>
#include "rdma_impl.h"
#include "udapl_header.h"
#include "dreg.h"
#include <math.h>
#include "udapl_param.h"
#include "mpidrma.h"
#include "pmi.h"
#include "udapl_util.h"
#include "mpiutil.h"

#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */ 

#ifndef GEN_EXIT_ERR
#define GEN_EXIT_ERR    -1
#endif
#ifndef ibv_error_abort
#define ibv_error_abort(code, message) do                       \
{                                                               \
	int my_rank;                                                \
	PMI_Get_rank(&my_rank);                                     \
	fprintf(stderr, "[%d] Abort: ", my_rank);                   \
	fprintf(stderr, message);                                   \
	fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
	    __FILE__);                                              \
    fflush (stderr);                                            \
	exit(code);                                                 \
} while (0)
#endif

extern int number_of_op;

int Decrease_CC (MPID_Win *, int);
int POST_PUT_PUT_GET_LIST (MPID_Win *, int, dreg_entry *,
                           MPIDI_VC_t *, UDAPL_DESCRIPTOR *);
int POST_GET_PUT_GET_LIST (MPID_Win *, int, void *, void *,
                           dreg_entry *, MPIDI_VC_t *, UDAPL_DESCRIPTOR *);
int Consume_signals (MPID_Win *, aint_t);
int Find_Avail_Index (void);
int Find_Win_Index (MPID_Win *);
int IBA_PUT (MPIDI_RMA_ops *, MPID_Win *, int);
int IBA_GET (MPIDI_RMA_ops *, MPID_Win *, int);
int IBA_ACCUMULATE (MPIDI_RMA_ops *, MPID_Win *, int);
int iba_lock (MPID_Win *, MPIDI_RMA_ops *, int);
int iba_unlock (MPID_Win *, MPIDI_RMA_ops *, int);

void MPIDI_CH3I_RDMA_lock (MPID_Win * win_ptr,
                           int target_rank, int lock_type, int blocking);
void Get_Pinned_Buf (MPID_Win * win_ptr, char **origin, int size);
int     iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
int     iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);


#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */



/* For active synchronization, it is a blocking call*/





void
MPIDI_CH3I_RDMA_start (MPID_Win * win_ptr,
                       int start_grp_size, int *ranks_in_win_grp)
{
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    int flag = 0, src, i;

    if (SMP_INIT)
    {
        MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    }

    DEBUG_PRINT ("before while loop %d\n", win_ptr->post_flag[1]);
    while (flag == 0 && start_grp_size != 0)
      {
          for (i = 0; i < start_grp_size; i++)
            {
                flag = 1;
                src = ranks_in_win_grp[i];      /*src is the rank in comm */

                if (SMP_INIT) {
                    MPIDI_Comm_get_vc (comm_ptr, src, &vc);
                    if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
                      {
                        /*correspoding post has not been issued */
                        flag = 0;
                        break;
                      }
                } else if (win_ptr->post_flag[src] == 0)
                  {
                      /*correspoding post has not been issued */
                      flag = 0;
                      break;
                  }
            }                   /* end of for loop  */
      }                         /* end of while loop */
    DEBUG_PRINT ("finish while loop\n");
}

/* For active synchronization, if all rma operation has completed, we issue a RDMA
write operation with fence to update the remote flag in target processes*/
void
MPIDI_CH3I_RDMA_complete_rma (MPID_Win * win_ptr,
                              int start_grp_size, int *ranks_in_win_grp,
                              int send_complete)
{
    int i, target, dst;
    int comm_size;
    int* nops_to_proc = NULL;
    int mpi_errno;
    MPID_Comm* comm_ptr = NULL;
    MPIDI_RMA_ops* curr_ptr = NULL;
    MPIDI_VC_t* vc = NULL;

    MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    comm_size = comm_ptr->local_size;

    nops_to_proc = (int *) MPIU_Calloc (comm_size, sizeof (int));
    /* --BEGIN ERROR HANDLING-- */
    if (!nops_to_proc)
      {
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**nomem", 0);
          goto fn_exit;
      }
    /* --END ERROR HANDLING-- */

    /* set rma_target_proc[i] to 1 if rank i is a target of RMA
       ops from this process */
    for (i = 0; i < comm_size; i++)
        nops_to_proc[i] = 0;
    curr_ptr = win_ptr->rma_ops_list;
    while (curr_ptr != NULL)
      {
          nops_to_proc[curr_ptr->target_rank]++;
          curr_ptr = curr_ptr->next;
      }
    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; i++)
      {
          dst = ranks_in_win_grp[i];
          win_ptr->post_flag[dst] = 0;
      }
    win_ptr->using_start = 0;

    for (i = 0; i < start_grp_size; i++)
      {
          target = ranks_in_win_grp[i]; /* target is the rank is comm */

          if (SMP_INIT) {
              MPIDI_Comm_get_vc (comm_ptr, target, &vc);
              if (nops_to_proc[target] == 0 && send_complete == 1
                  && vc->smp.local_nodes == -1) {
                  Decrease_CC (win_ptr, target);
                  if (win_ptr->wait_for_complete == 1)
                    {
                      MPIDI_CH3I_RDMA_finish_rma (win_ptr);
                    }

              }
          } else if (nops_to_proc[target] == 0 && send_complete == 1)
            {
                Decrease_CC (win_ptr, target);
                if (win_ptr->wait_for_complete == 1)
                  {
                      MPIDI_CH3I_RDMA_finish_rma (win_ptr);
                  }
            }
      }
  fn_exit:
    return;
}

/* Waiting for all the completion signals and unregister buffers*/
int
MPIDI_CH3I_RDMA_finish_rma (MPID_Win * win_ptr)
{
    if (win_ptr->put_get_list_size != 0)
      {
          return Consume_signals (win_ptr, 0);
      }
    else
        return 0;
}


/* Go through RMA op list once, and start as many RMA ops as possible */
void
MPIDI_CH3I_RDMA_try_rma (MPID_Win * win_ptr,
                         MPIDI_RMA_ops ** MPIDI_RMA_ops_list, int passive)
{
    MPIDI_RMA_ops *curr_ptr, *head_ptr = NULL, *prev_ptr = NULL, *tmp_ptr;
    int size, origin_type_size, target_type_size;
    int tag = 0;
#ifdef _SCHEDULE
    int curr_put = 1;
    int fall_back = 0;
    int force_to_progress = 0, issued = 0;
    MPIDI_RMA_ops *skipped_op = NULL;
#endif

    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    
    if (SMP_INIT)
    {
        MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    }

    prev_ptr = curr_ptr = head_ptr = *MPIDI_RMA_ops_list;
    if (*MPIDI_RMA_ops_list != NULL)
        tag = 1;

#ifdef _SCHEDULE
    while (curr_ptr != NULL || skipped_op != NULL)
      {
          if (curr_ptr == NULL && skipped_op != NULL)
            {
                curr_ptr = skipped_op;
                skipped_op = NULL;
                fall_back++;
                if (issued == 0)
                  {
                      force_to_progress = 1;
                  }
                else
                  {
                      force_to_progress = 0;
                      issued = 0;
                  }
            }
#else
    while (curr_ptr != NULL)
      {
#endif

      if (SMP_INIT) {
          MPIDI_Comm_get_vc (comm_ptr, curr_ptr->target_rank, &vc);

          if (vc->smp.local_nodes != -1)
            {
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                continue;
            }
      }

          if (passive == 0 && win_ptr->post_flag[curr_ptr->target_rank] == 1
              && win_ptr->using_lock == 0)
            {
                switch (curr_ptr->type)
                  {
                  case (MPIDI_RMA_PUT):
                      {
                          int origin_dt_derived, target_dt_derived;

                          if (HANDLE_GET_KIND (curr_ptr->origin_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              origin_dt_derived = 1;
                          else
                              origin_dt_derived = 0;
                          if (HANDLE_GET_KIND (curr_ptr->target_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              target_dt_derived = 1;
                          else
                              target_dt_derived = 0;

                          MPID_Datatype_get_size_macro (curr_ptr->
                                                        origin_datatype,
                                                        origin_type_size);
                          size = curr_ptr->origin_count * origin_type_size;

                          if (!origin_dt_derived && !target_dt_derived
                              && size > rdma_put_fallback_threshold)
                            {
                                if (IBA_PUT (curr_ptr, win_ptr, size)
                                    != MPI_SUCCESS)
                                  {
                                      prev_ptr = curr_ptr;
                                      curr_ptr = curr_ptr->next;
                                  }
                                else
                                  {
                                      win_ptr->rma_issued++;
                                      if (head_ptr == curr_ptr)
                                        {
                                            prev_ptr = head_ptr =
                                                curr_ptr->next;
                                        }
                                      else
                                          prev_ptr->next = curr_ptr->next;

                                      tmp_ptr = curr_ptr->next;
                                      MPIU_Free (curr_ptr);
                                      curr_ptr = tmp_ptr;
                                  }
                            }
                          else
                            {
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
                          if (HANDLE_GET_KIND (curr_ptr->origin_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              origin_dt_derived = 1;
                          else
                              origin_dt_derived = 0;
                          if (HANDLE_GET_KIND (curr_ptr->target_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              target_dt_derived = 1;
                          else
                              target_dt_derived = 0;
                          MPID_Datatype_get_size_macro (curr_ptr->
                                                        target_datatype,
                                                        target_type_size);
                          size = curr_ptr->target_count * target_type_size;

                          if (!origin_dt_derived && !target_dt_derived
                              && size > rdma_get_fallback_threshold)
                            {
                                if (IBA_GET (curr_ptr, win_ptr, size)
                                    != MPI_SUCCESS)
                                  {
                                      prev_ptr = curr_ptr;
                                      curr_ptr = curr_ptr->next;
                                  }
                                else
                                  {
                                      win_ptr->rma_issued++;
                                      if (head_ptr == curr_ptr)
                                          prev_ptr = head_ptr =
                                              curr_ptr->next;
                                      else
                                          prev_ptr->next = curr_ptr->next;
                                      tmp_ptr = curr_ptr->next;
                                      MPIU_Free (curr_ptr);
                                      curr_ptr = tmp_ptr;
                                  }
                            }
                          else
                            {
                                prev_ptr = curr_ptr;
                                curr_ptr = curr_ptr->next;
                            }
                          break;
                      }
                  default:
                      printf ("Unknown ONE SIDED OP\n");
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
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
int MPIDI_CH3I_RDMA_post (MPID_Win * win_ptr, int target_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST);

    char *origin_addr;
    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    /*part 1 prepare origin side buffer */
    char* remote_address = (char *) win_ptr->remote_post_flags[target_rank];
    uint32_t l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
    uint32_t r_key = win_ptr->r_key4[target_rank];
    int size = sizeof (int);
    Get_Pinned_Buf (win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc (comm_ptr, target_rank, &tmp_vc);

    /*part 2 Do RDMA WRITE */
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) remote_address;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    POST_PUT_PUT_GET_LIST (win_ptr, -1, NULL, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);
    Consume_signals (win_ptr, 0);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    return MPI_SUCCESS;
}

void
MPIDI_CH3I_RDMA_win_create (void *base,
                            MPI_Aint size,
                            int comm_size,
                            int rank,
                            MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
    int ret, i, index;
    uintptr_t *tmp;
    uint32_t r_key, r_key2, r_key3, r_key4, r_key5;
    int my_rank;
    uintptr_t *tmp1, *tmp2, *tmp3, *tmp4;
    int recvbuf;

    if (strcmp(dapl_provider, "nes0") == 0) {
          (*win_ptr)->fall_back = 1;
          return;
    }

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided) {
        (*win_ptr)->fall_back = 1;
        return;
    }

    PMI_Get_rank (&my_rank);
    /*There may be more than one windows existing at the same time */
    MPIDI_CH3I_RDMA_Process.current_win_num++;
    MPIU_Assert(MPIDI_CH3I_RDMA_Process.current_win_num <= MAX_WIN_NUM);
    index = Find_Avail_Index ();
    MPIU_Assert(index != -1);
    /*Register the exposed buffer in this window */
    if (base != NULL && size > 0)
      {
          MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] =
              dreg_register (base, size);
          if (NULL ==
              MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index])
              (*win_ptr)->fall_back = 1;
          else
              (*win_ptr)->fall_back = 0;

          ret =
              NMPI_Allreduce (&((*win_ptr)->fall_back), &recvbuf, 1, MPI_INT,
                              MPI_SUM, comm_ptr->handle);
          if (ret != MPI_SUCCESS)
            {
                printf ("Error allreduce while creating windows\n");
                ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
            }

          if (recvbuf != 0)
            {
                (*win_ptr)->fall_back = 1;
                goto win_unregister;
            }
          else
              MPIU_Assert((*win_ptr)->fall_back == 0);

          r_key =
              MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]->
              memhandle.rkey;
      }
    else
      {
          MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] = NULL;
          r_key = -1;

          (*win_ptr)->fall_back = 0;
          ret =
              NMPI_Allreduce (&((*win_ptr)->fall_back), &recvbuf, 1, MPI_INT,
                              MPI_SUM, comm_ptr->handle);
          if (ret != MPI_SUCCESS)
            {
                printf ("Error allreduce while creating windows\n");
                ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
            }

          if (recvbuf != 0)
            {
                (*win_ptr)->fall_back = 1;
                goto fn_exit;
            }
          else
              MPIU_Assert((*win_ptr)->fall_back == 0);
      }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter =
        MPIU_Malloc (sizeof (long long) * comm_size);
    MPIU_Memset ((*win_ptr)->completion_counter, 0,
            sizeof (long long) * comm_size);
    MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] =
        dreg_register ((void *) (*win_ptr)->completion_counter,
                       sizeof (long long) * comm_size);
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index])
      {
          (*win_ptr)->fall_back = 1;
          goto win_unregister;
      }
    r_key2 =
        MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index]->
        memhandle.rkey;

    /*Register buffer for accumulation exclusive access lock */
    (*win_ptr)->actlock = (long long *) MPIU_Malloc (sizeof (long long));
    MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] =
        dreg_register ((void *) (*win_ptr)->actlock, sizeof (long long));
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index])
      {
          (*win_ptr)->fall_back = 1;
          goto cc_unregister;
      }
    r_key3 =
        MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index]->
        memhandle.rkey;
    *((long long *) ((*win_ptr)->actlock)) = 0;

    /*Register buffer for post flags : from target to origin */
    (*win_ptr)->post_flag = (int *) MPIU_Malloc (comm_size * sizeof (int));
    MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] =
        dreg_register ((void *) (*win_ptr)->post_flag,
                       sizeof (int) * comm_size);
    if (NULL == MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index])
      {
          (*win_ptr)->fall_back = 1;
          goto actlock_unregister;
      }
    r_key4 =
        MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index]->
        memhandle.rkey;

    /* Preregister buffer*/
    (*win_ptr)->pinnedpool_1sc_buf = MPIU_Malloc (rdma_pin_pool_size);
    (*win_ptr)->pinnedpool_1sc_dentry =
        dreg_register ((*win_ptr)->pinnedpool_1sc_buf, rdma_pin_pool_size);
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry)
      {
          (*win_ptr)->fall_back = 1;
          goto post_unregister;
    }

    (*win_ptr)->pinnedpool_1sc_index = 0;

    /*Exchagne the information about rkeys and addresses */
    tmp = MPIU_Malloc (comm_size * sizeof (uintptr_t) * 8);
    if (!tmp)
      {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    tmp[8 * rank] = (uint32_t) r_key;
    tmp[8 * rank + 1] = (uint32_t) r_key2;
    tmp[8 * rank + 2] = (uint32_t) r_key3;
    tmp[8 * rank + 3] = (uintptr_t) ((*win_ptr)->actlock);
    tmp[8 * rank + 4] = (uintptr_t) ((*win_ptr)->completion_counter);
    tmp[8 * rank + 5] = (uintptr_t) ((*win_ptr)->assist_thr_ack);
    tmp[8 * rank + 6] = (uint32_t) r_key5;
    tmp[8 * rank + 7] = (uint32_t) (*win_ptr)->fall_back;

    ret =
        NMPI_Allgather (MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 8,
                        MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS)
      {
          printf ("Error gather rkey  when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    /* check if any peers fail */
    for (i = 0; i < comm_size; ++i)
    {
            if (tmp[i*8 + 7] != 0)
            {
               (*win_ptr)->fall_back = 1;
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
               MPIU_Free(tmp);
               goto fn_exit;
            }
    }    

    (*win_ptr)->r_key =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->r_key)
      {
          printf ("Error malloc win->r_key when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->r_key2 =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->r_key2)
      {
          printf ("Error malloc win->r_key2 when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->r_key3 =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->r_key3)
      {
          printf ("error malloc win->r_key3 when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->all_actlock_addr =
        (long long **) MPIU_Malloc (comm_size * sizeof (long long *));
    if (!(*win_ptr)->all_actlock_addr)
      {
          printf
              ("error malloc win->all_actlock_addr when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->all_completion_counter =
        (long long **) MPIU_Malloc (comm_size * sizeof (long long *));
    if (!(*win_ptr)->all_actlock_addr)
      {
          printf
              ("error malloc win->all_completion_counter when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->r_key5 =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->r_key5)
      {
          printf ("error malloc win->all_wins when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    (*win_ptr)->all_assist_thr_acks =
        (int **) MPIU_Malloc (comm_size * sizeof (int *));
    if (!(*win_ptr)->all_assist_thr_acks)
      {
          printf ("error malloc win->all_wins when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    for (i = 0; i < comm_size; i++)
      {
          (*win_ptr)->r_key[i] = tmp[7 * i];
          (*win_ptr)->r_key2[i] = tmp[7 * i + 1];
          (*win_ptr)->r_key3[i] = tmp[7 * i + 2];
          (*win_ptr)->all_actlock_addr[i] = (long long *) tmp[7 * i + 3];
          (*win_ptr)->all_completion_counter[i] =
              (long long *) (tmp[7 * i + 4] + sizeof (long long) * rank);
          (*win_ptr)->all_assist_thr_acks[i] = (int *) tmp[7 * i + 5];
          (*win_ptr)->r_key5[i] = tmp[7 * i + 6];

      }
    tmp1 = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!tmp1)
      {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    tmp2 = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!tmp2)
      {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    tmp3 = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!tmp3)
      {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    tmp4 = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!tmp4)
      {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; i++)
      {
          if (i != rank)
              (*win_ptr)->post_flag[i] = 0;
          else
              (*win_ptr)->post_flag[i] = 1;
          tmp1[i] = (uint32_t) r_key4;
          tmp2[i] = (uintptr_t) & ((*win_ptr)->post_flag[i]);
      }
    ret =
        NMPI_Alltoall (tmp1, 1, MPI_LONG, tmp3, 1, MPI_LONG,
                       comm_ptr->handle);
    if (ret != MPI_SUCCESS)
      {
          printf ("Error gather rkey  when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    ret =
        NMPI_Alltoall (tmp2, 1, MPI_LONG, tmp4, 1, MPI_LONG,
                       comm_ptr->handle);
    if (ret != MPI_SUCCESS)
      {
          printf ("Error gather rkey  when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->r_key4 =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->r_key4)
      {
          printf ("error malloc win->r_key3 when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    (*win_ptr)->remote_post_flags =
        (long **) MPIU_Malloc (comm_size * sizeof (long *));
    if (!(*win_ptr)->remote_post_flags)
      {
          printf
              ("error malloc win->remote_post_flags when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    for (i = 0; i < comm_size; i++)
      {
          (*win_ptr)->r_key4[i] = tmp3[i];
          (*win_ptr)->remote_post_flags[i] = (long *) tmp4[i];
      }
    MPIU_Free (tmp);
    MPIU_Free (tmp1);
    MPIU_Free (tmp2);
    MPIU_Free (tmp3);
    MPIU_Free (tmp4);
    (*win_ptr)->using_lock = 0;
    (*win_ptr)->using_start = 0;
    (*win_ptr)->my_id = rank;
    (*win_ptr)->comm_size = comm_size;

    /* Initialize put/get queue */
    (*win_ptr)->put_get_list_size = 0;
    (*win_ptr)->put_get_list_tail = 0;
    (*win_ptr)->put_get_list =
        MPIU_Malloc (rdma_default_put_get_list_size *
                     sizeof (MPIDI_CH3I_RDMA_put_get_list));
    (*win_ptr)->wait_for_complete = 0;
    (*win_ptr)->rma_issued = 0;

    if (!(*win_ptr)->put_get_list)
      {
          printf ("Fail to malloc space for window put get list\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    MPIDI_CH3I_RDMA_Process.win_index2address[index] = (uintptr_t) * win_ptr;

  fn_exit:
    if (1 == (*win_ptr)->fall_back)
      {
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
  post_unregister:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
                      RDMA_post_flag_dreg_entry[index]);
    MPIU_Free((*win_ptr)->post_flag);
  actlock_unregister:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
                      RDMA_local_actlock_dreg_entry[index]); 
    MPIU_Free((*win_ptr)->actlock); 
  cc_unregister:
    dreg_unregister(MPIDI_CH3I_RDMA_Process.
                      RDMA_local_wincc_dreg_entry[index]);
    MPIU_Free((*win_ptr)->completion_counter);
  win_unregister:
    if(MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index]) 
      {
          dreg_unregister(MPIDI_CH3I_RDMA_Process.
                      RDMA_local_win_dreg_entry[index]);
      }
    tmp = MPIU_Malloc (comm_size * sizeof (uintptr_t) * 8);
    if (!tmp)
     {
          printf ("Error malloc tmp when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
     }
    tmp[8 * rank + 7] = (uint32_t) (*win_ptr)->fall_back;
    ret =
        NMPI_Allgather (MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, tmp, 8,
                        MPI_LONG, comm_ptr->handle);
    if (ret != MPI_SUCCESS)
      {
          printf ("Error gather rkey  when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }
    goto fn_exit;
}

void
MPIDI_CH3I_RDMA_win_free (MPID_Win ** win_ptr)
{
    int index;
    index = Find_Win_Index (*win_ptr);
    if (index == -1)
      {
          printf ("dont know win_ptr %p %d \n", *win_ptr,
                  MPIDI_CH3I_RDMA_Process.current_win_num);
      }
    MPIU_Assert (index != -1);
    MPIDI_CH3I_RDMA_Process.win_index2address[index] = 0;
    MPIDI_CH3I_RDMA_Process.current_win_num--;
    MPIU_Assert (MPIDI_CH3I_RDMA_Process.current_win_num >= 0);
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_win_dreg_entry[index] != NULL)
      {
          dreg_unregister (MPIDI_CH3I_RDMA_Process.
                           RDMA_local_win_dreg_entry[index]);
      }
    MPIU_Free ((*win_ptr)->r_key);
    MPIU_Free ((*win_ptr)->r_key2);
    MPIU_Free ((*win_ptr)->r_key3);
    MPIU_Free ((*win_ptr)->all_actlock_addr);
    MPIU_Free ((*win_ptr)->r_key4);
    MPIU_Free ((*win_ptr)->remote_post_flags);
    MPIU_Free ((*win_ptr)->put_get_list);
    dreg_unregister ((*win_ptr)->pinnedpool_1sc_dentry);
    MPIU_Free ((*win_ptr)->pinnedpool_1sc_buf);
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_wincc_dreg_entry[index] != NULL)
      {
          dreg_unregister (MPIDI_CH3I_RDMA_Process.
                           RDMA_local_wincc_dreg_entry[index]);
      }
    if (MPIDI_CH3I_RDMA_Process.RDMA_local_actlock_dreg_entry[index] != NULL)
      {
          dreg_unregister (MPIDI_CH3I_RDMA_Process.
                           RDMA_local_actlock_dreg_entry[index]);
      }
    if (MPIDI_CH3I_RDMA_Process.RDMA_post_flag_dreg_entry[index] != NULL)
      {
          dreg_unregister (MPIDI_CH3I_RDMA_Process.
                           RDMA_post_flag_dreg_entry[index]);
      }
    MPIU_Free ((*win_ptr)->actlock);
    MPIU_Free ((*win_ptr)->completion_counter);
    MPIU_Free ((*win_ptr)->all_completion_counter);
}

int
Decrease_CC (MPID_Win * win_ptr, int target_rank)
{
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key2, l_key2;
    int ret;
    long long *cc;
    DAT_EP_HANDLE qp_hndl;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc (comm_ptr, target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;
    Get_Pinned_Buf (win_ptr, (char **) &cc, sizeof (long long));
    *((long long *) cc) = 1;
    l_key2 = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
    r_key2 = win_ptr->r_key2[target_rank];

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 1;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key2;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) cc;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = sizeof (long long);
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key2;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) (win_ptr->
                                     all_completion_counter[target_rank]);
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = sizeof (long long);
    POST_PUT_PUT_GET_LIST (win_ptr, -1, NULL, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);

    return mpi_errno;
}

int
POST_PUT_PUT_GET_LIST (MPID_Win * winptr,
                       int size,
                       dreg_entry * dreg_tmp,
                       MPIDI_VC_t * vc_ptr, UDAPL_DESCRIPTOR * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;
    winptr->put_get_list[index].op_type = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry = dreg_tmp;
    winptr->put_get_list[index].data_size = size;
    winptr->put_get_list[index].win_ptr = winptr;
    winptr->put_get_list[index].vc_ptr = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
        rdma_default_put_get_list_size;
    winptr->put_get_list_size++;
    vc_ptr->mrail.postsend_times_1sc++;

    desc_p->cookie.as_ptr = (&(winptr->put_get_list[index]));

    {
        unsigned int completionflags = 0;

        if (desc_p->fence)
          {
              completionflags = DAT_COMPLETION_BARRIER_FENCE_FLAG;
          }

        ret = dat_ep_post_rdma_write (vc_ptr->mrail.qp_hndl_1sc, 1,
                                      &(desc_p->local_iov),
                                      desc_p->cookie,
                                      &(desc_p->remote_iov), completionflags);
    }

    CHECK_RETURN (ret, "Fail in posting RDMA_Write");

    if (winptr->put_get_list_size == rdma_default_put_get_list_size
        || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_send_wqe - 1)
      {
          Consume_signals (winptr, 0);
      }
    return index;
}

/*functions storing the get operations */
int
POST_GET_PUT_GET_LIST (MPID_Win * winptr,
                       int size,
                       void *target_addr,
                       void *origin_addr,
                       dreg_entry * dreg_tmp,
                       MPIDI_VC_t * vc_ptr, UDAPL_DESCRIPTOR * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;

    winptr->put_get_list[index].op_type = SIGNAL_FOR_GET;
    winptr->put_get_list[index].origin_addr = origin_addr;
    winptr->put_get_list[index].target_addr = target_addr;
    winptr->put_get_list[index].data_size = size;
    winptr->put_get_list[index].mem_entry = dreg_tmp;
    winptr->put_get_list[index].win_ptr = winptr;
    winptr->put_get_list[index].vc_ptr = vc_ptr;
    winptr->put_get_list_size++;
    vc_ptr->mrail.postsend_times_1sc++;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
        rdma_default_put_get_list_size;

    desc_p->cookie.as_ptr = &winptr->put_get_list[index];
    winptr->wait_for_complete = 1;

    {
        unsigned int completionflags = 0;

        if (desc_p->fence)
            completionflags = DAT_COMPLETION_BARRIER_FENCE_FLAG;

        ret = dat_ep_post_rdma_read (vc_ptr->mrail.qp_hndl_1sc, 1,
                                     &(desc_p->local_iov),
                                     desc_p->cookie,
                                     &(desc_p->remote_iov), completionflags);
    }

    CHECK_RETURN (ret, "Fail in posting RDMA_READ");

    if (strcmp (dapl_provider, "ib0") == 0)
      {
          if (winptr->put_get_list_size == rdma_default_put_get_list_size
              || vc_ptr->mrail.postsend_times_1sc ==
              DAPL_DEFAULT_MAX_RDMA_IN - 1)
              /* the get queue is full */
              Consume_signals (winptr, 0);
      }
    else
      {
          if (winptr->put_get_list_size == rdma_default_put_get_list_size
              || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_send_wqe - 1)
              /* the get queue is full */
              Consume_signals (winptr, 0);
      }
    return index;
}

/*if signal == -1, cousume all the signals, 
  otherwise return when signal is found*/
int
Consume_signals (MPID_Win * winptr, aint_t expected)
{
    DAT_EVENT event;
    DAT_RETURN ret;
    dreg_entry *dreg_tmp;
    int i = 0, size;
    void *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list *list_entry = NULL;
    MPID_Win *list_win_ptr;
    MPIDI_VC_t *list_vc_ptr;

    while (1)
      {
          ret = dat_evd_dequeue (MPIDI_CH3I_RDMA_Process.cq_hndl_1sc, &event);
          if (ret == DAT_SUCCESS)
            {
                i++;
                if (event.event_number != DAT_DTO_COMPLETION_EVENT
                    || event.event_data.dto_completion_event_data.status !=
                    DAT_DTO_SUCCESS)
                  {
                      printf ("in Consume_signals %d  \n",
                              winptr->pinnedpool_1sc_index);
                      printf ("\n DAT_EVD_ERROR inside Consume_signals\n");
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");

                  }
                MPIU_Assert (event.event_data.dto_completion_event_data.status ==
                        DAT_DTO_SUCCESS);
                MPIU_Assert (event.event_data.dto_completion_event_data.
                        user_cookie.as_64 != -1);

                if ((aint_t) event.event_data.dto_completion_event_data.
                    user_cookie.as_ptr == expected)
                  {
                      goto fn_exit;
                  }

                /* if id is not equal to expected id, the only possible cq
                 * is for the posted signaled PUT/GET.
                 * Otherwise we have error.
                 */
                list_entry =
                    (MPIDI_CH3I_RDMA_put_get_list *) (aint_t) event.
                    event_data.dto_completion_event_data.user_cookie.as_ptr;

                list_win_ptr = list_entry->win_ptr;
                list_vc_ptr = list_entry->vc_ptr;
                if (list_entry->op_type == SIGNAL_FOR_PUT)
                  {
                      dreg_tmp = list_entry->mem_entry;
                      size = list_entry->data_size;
                      if (size > (int) rdma_eagersize_1sc)
                        {
                            dreg_unregister (dreg_tmp);
                        }
                      list_win_ptr->put_get_list_size--;
                      list_vc_ptr->mrail.postsend_times_1sc--;
                  }
                else if (list_entry->op_type == SIGNAL_FOR_GET)
                  {
                      size = list_entry->data_size;
                      target_addr = list_entry->target_addr;
                      origin_addr = list_entry->origin_addr;
                      dreg_tmp = list_entry->mem_entry;
                      if (origin_addr == NULL)
                        {
                            MPIU_Assert(size > rdma_eagersize_1sc);
                            dreg_unregister (dreg_tmp);
                        }
                      else
                        {
                            MPIU_Assert(size <= rdma_eagersize_1sc);
                            MPIU_Assert(target_addr != NULL);
                            MPIU_Memcpy (target_addr, origin_addr, size);
                        }
                      list_win_ptr->put_get_list_size--;
                      list_vc_ptr->mrail.postsend_times_1sc--;
                  }
                else
                  {
                      fprintf (stderr, "Error! rank %d, Undefined op_type, op type %d, \
                list id %u, expecting id %u\n",
                               winptr->my_id, list_entry->op_type, list_entry, expected);
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
                  }
            }


          if (winptr->put_get_list_size == 0)
              winptr->put_get_list_tail = 0;
          if (winptr->put_get_list_size == 0 && (expected == 0))
            {
                winptr->rma_issued = 0;
                winptr->pinnedpool_1sc_index = 0;
                goto fn_exit;
            }
      }                         /* end of while */
  fn_exit:
    MPIU_Assert(event.event_data.dto_completion_event_data.user_cookie.as_64 ==
            expected || expected == 0);

    return 0;
}

int
Find_Avail_Index ()
{
    int i, index = -1;
    for (i = 0; i < MAX_WIN_NUM; i++)
      {
          if (MPIDI_CH3I_RDMA_Process.win_index2address[i] == 0)
            {
                index = i;
                break;
            }
      }
    return index;
}

int
Find_Win_Index (MPID_Win * win_ptr)
{
    int i, index = -1;
    for (i = 0; i < MAX_WIN_NUM; i++)
      {
          if (MPIDI_CH3I_RDMA_Process.win_index2address[i] == (long) win_ptr)
            {
                index = i;
                break;
            }
      }
    return index;
}

int
IBA_PUT (MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char *remote_address;
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key, l_key;
    int ret;
    DAT_EP_HANDLE qp_hndl;
    int origin_type_size;
    dreg_entry *tmp_dreg = NULL;
    char *origin_addr;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    int index;

    /*part 1 prepare origin side buffer target buffer and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank]
        + win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
    if (size <= rdma_eagersize_1sc)
      {
          char *tmp = rma_op->origin_addr;

          Get_Pinned_Buf (win_ptr, &origin_addr, size);
          MPIU_Memcpy (origin_addr, tmp, size);
          l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
      }
    else
      {
          tmp_dreg = dreg_register (rma_op->origin_addr, size);
          if (!tmp_dreg)
            {
                return -1;
            }
          l_key = tmp_dreg->memhandle.lkey;
          origin_addr = rma_op->origin_addr;
          win_ptr->wait_for_complete = 1;
      }
    r_key = win_ptr->r_key[rma_op->target_rank];
    MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc (comm_ptr, rma_op->target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;

    /*part 2 Do RDMA WRITE */
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) remote_address;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    POST_PUT_PUT_GET_LIST (win_ptr, size, tmp_dreg, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);

    return mpi_errno;
}

int
IBA_GET (MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char *remote_address;
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key, l_key;
    /* int ret = 0; */
    DAT_EP_HANDLE qp_hndl;
    int index;
    dreg_entry *tmp_dreg;
    char *origin_addr;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;


    MPIU_Assert (rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc)
      {
          Get_Pinned_Buf (win_ptr, &origin_addr, size);
          l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
      }
    else
      {
          tmp_dreg = dreg_register (rma_op->origin_addr, size);
          if (tmp_dreg == NULL)
            {
                return -1;
            }
          l_key = tmp_dreg->memhandle.lkey;
          origin_addr = rma_op->origin_addr;
      }

    r_key = win_ptr->r_key[rma_op->target_rank];
    MPID_Comm_get_ptr (win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc (comm_ptr, rma_op->target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address = (DAT_VADDR) (unsigned long) remote_address;
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address = (DAT_VADDR) (unsigned long) remote_address;
#endif /* DAT_VERSION_MAJOR < 2 */
    /*        = (DAT_VADDR) (unsigned long) remote_address; */

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    if (size <= rdma_eagersize_1sc)
        index = POST_GET_PUT_GET_LIST (win_ptr, size, rma_op->origin_addr,
                                       origin_addr, NULL, tmp_vc,
                                       &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                               put_get_list_tail]);
    else
        index = POST_GET_PUT_GET_LIST (win_ptr, size, NULL, NULL,
                                       tmp_dreg, tmp_vc,
                                       &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                               put_get_list_tail]);

    return mpi_errno;
}

void
Get_Pinned_Buf (MPID_Win * win_ptr, char **origin, int size)
{
    if (win_ptr->pinnedpool_1sc_index + size >= rdma_pin_pool_size)
      {
          Consume_signals (win_ptr, 0);
          *origin = win_ptr->pinnedpool_1sc_buf;
          win_ptr->pinnedpool_1sc_index = size;
      }
    else
      {
          *origin =
              win_ptr->pinnedpool_1sc_buf + win_ptr->pinnedpool_1sc_index;
          win_ptr->pinnedpool_1sc_index += size;
      }
}

