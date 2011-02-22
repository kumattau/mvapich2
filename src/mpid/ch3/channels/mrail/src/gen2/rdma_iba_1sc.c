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
#include <math.h>

#include "rdma_impl.h"
#include "dreg.h"
#include "ibv_param.h"
#include "infiniband/verbs.h"
#include "mpidrma.h"
#include "pmi.h"
#include "mpiutil.h"

#if defined(_SMP_LIMIC_)
#include <fcntl.h>
#include <sys/mman.h>
#include "mpimem.h"
#endif /*_SMP_LIMIC_*/

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

typedef struct {
  uintptr_t win_ptr;
  uint32_t win_rkeys[MAX_NUM_HCAS];
  uint32_t completion_counter_rkeys[MAX_NUM_HCAS];
  uint32_t post_flag_rkeys[MAX_NUM_HCAS];
  uint32_t fall_back;
} win_info;

#if defined(_SMP_LIMIC_)
#define PID_CHAR_LEN 22

char* shmem_file;
unsigned short rma_shmid = 100;
extern struct smpi_var g_smpi;
extern int g_smp_use_limic2;
extern int limic_fd;
#endif /* _SMP_LIMIC_ */

extern int number_of_op;
static int Decrease_CC(MPID_Win *, int);
static int Post_Get_Put_Get_List(MPID_Win *, 
        int , dreg_entry * ,
        MPIDI_VC_t * , void *local_buf[], 
        void *remote_buf[], void *user_buf[], int length,
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

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME mv2_rma_allocate_shm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int mv2_rma_allocate_shm(int size, int g_rank, int *shmem_fd, 
                   void **rnt_buf, MPID_Comm * comm_ptr)
{
    int ret, mpi_errno = MPI_SUCCESS;
    void* rma_shared_memory = NULL;
    const char *rma_shmem_dir="/";
    struct stat file_status;
    int length;
    char *rma_shmem_file;

    length = strlen(rma_shmem_dir)
             + 7 /*this is to hold MV2 and the unsigned short rma_shmid*/
             + PID_CHAR_LEN 
             + 6 /*this is for the hyphens and extension */;

    rma_shmem_file = (char *) MPIU_Malloc(length);

    if(g_rank == 0)
    {
       sprintf(rma_shmem_file, "%smv2-%d-%d.tmp",
                       rma_shmem_dir, getpid(), rma_shmid);
       rma_shmid++;
    }

    MPIR_Bcast(rma_shmem_file, length, MPI_CHAR, 0, comm_ptr); 

    *shmem_fd = shm_open(rma_shmem_file, O_CREAT | O_RDWR, S_IRWXU);
    if(*shmem_fd == -1){
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
        goto fn_exit;
    }

    if (ftruncate(*shmem_fd, size) == -1)
    {
           mpi_errno =
              MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
           goto fn_exit;
    } 

    /*verify file creation*/
    do 
    {
        if (fstat(*shmem_fd, &file_status) != 0)
        {
            shm_unlink(rma_shmem_file);
            MPIU_Free(rma_shmem_file);

            mpi_errno =
               MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                   __LINE__, MPI_ERR_OTHER, "**nomem", 0);
            goto fn_exit;
        }
    } while (file_status.st_size != size);

    rma_shared_memory = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         *shmem_fd, 0);
    if (rma_shared_memory == MAP_FAILED)
    {
         mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
         goto fn_exit;
    }

    *rnt_buf =  rma_shared_memory;

    MPIR_Barrier(comm_ptr);
 
    shm_unlink(rma_shmem_file);
    MPIU_Free(rma_shmem_file);

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME mv2_rma_deallocate_shm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void mv2_rma_deallocate_shm(void *addr, int size)
{
    if(munmap(addr, size))
    {
        DEBUG_PRINT("munmap failed in mv2_rma_deallocate_shm with error: %d \n", errno);
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc: mv2_rma_deallocate_shm");
    }
}
#endif /* _SMP_LIMIC_ */

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

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_start
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_LIMIC_start (MPID_Win* win_ptr, int start_grp_size, 
                  int* ranks_in_win_grp) 
{
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    int src, i;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);

    for (i = 0; i < start_grp_size; ++i) {
       src = ranks_in_win_grp[i];  /*src is the rank in comm*/
       MPIDI_Comm_get_vc(comm_ptr, src, &vc);
       if (vc->smp.local_nodes != -1) {
          /* Casting to volatile. This will prevent the compiler 
             from optimizing the loop out or from storing the veriable in 
             registers */
          while (*((volatile int *) &win_ptr->limic_post_flag_me[src]) == 0) {
          }
       }
     }
    
    /*Clearing the post flag counters once we ahve received all the 
      flags for the current epoch*/
    MPIU_Memset(win_ptr->limic_post_flag_me, 0, win_ptr->comm_size*sizeof(int));
    win_ptr->limic_post_flag_me[comm_ptr->rank] = 1;
}
#endif /* _SMP_LIMIC_ */

/* For active synchronization, if all rma operation has completed, we issue a RDMA
write operation with fence to update the remote flag in target processes*/
void
MPIDI_CH3I_RDMA_complete_rma(MPID_Win * win_ptr,
                         int start_grp_size, int *ranks_in_win_grp)
{
    int i, target, dst;
    int my_rank, comm_size;
    int *nops_to_proc;
    int mpi_errno;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops *curr_ptr;
    MPIDI_VC_t* vc = NULL;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    my_rank = comm_ptr->rank;
    comm_size = comm_ptr->local_size;

    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; ++i) {
        dst = ranks_in_win_grp[i];
        win_ptr->post_flag[dst] = 0;
    }
    win_ptr->using_start = 0;

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
    for (i = 0; i < comm_size; ++i)
    {
        nops_to_proc[i] = 0;
    }
    curr_ptr = win_ptr->rma_ops_list;
    while (curr_ptr != NULL) {
        ++nops_to_proc[curr_ptr->target_rank];
        curr_ptr = curr_ptr->next;
    }

    for (i = 0; i < start_grp_size; ++i) {
        target = ranks_in_win_grp[i]; /* target is the rank is comm */

        if (target == my_rank) {
            continue;
        }

        if(SMP_INIT) 
        {
            MPIDI_Comm_get_vc(comm_ptr, target, &vc);
            if (nops_to_proc[target] == 0 && vc->smp.local_nodes == -1)
            {
            /* We ensure local completion of all the pending operations on 
             * the current window and then set the complete flag on the remote
             * side. We do not wait for the completion of this operation as this
             * would unnecessarily wait on completion of all operations on this
             * RC connection.
             */ 
               Decrease_CC(win_ptr, target);
            }
        } 
        else if (nops_to_proc[target] == 0)  
        {
            Decrease_CC(win_ptr, target);
        }
    }

    MPIU_Free(nops_to_proc);

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

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_try_rma
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_LIMIC_try_rma(MPIDI_RMA_ops * curr_ptr, MPID_Win * win_ptr,
                           MPI_Win source_win_handle, MPID_Comm *comm_ptr,
                           int isPut)
{

    int complete_size, local_src=-1;
    int i;
    int mpi_errno = MPI_SUCCESS;
    int origin_type_size,target_type_size,size,offset;
    char *target_addr;
    MPIDI_VC_t* tmp_vc = NULL;
    int origin_dt_derived;
    int target_dt_derived;
    int cmpl_index;
    long long curr_complete[rdma_num_rails];
    int initial_offset,initial_nrPages,initial_len;
    int rank;
    int dst_l_rank;

    MPIDI_Comm_get_vc(comm_ptr, curr_ptr->target_rank, &tmp_vc);

    if(tmp_vc->smp.local_nodes == -1) {
        goto fn_fail;
    } 

    origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) != 
                  HANDLE_KIND_BUILTIN ? 1 : 0;
    target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                  HANDLE_KIND_BUILTIN ? 1 : 0;
    MPID_Datatype_get_size_macro(curr_ptr->target_datatype,
                  target_type_size);
    size = curr_ptr->target_count * target_type_size;

    if((isPut && (size < limic_put_threshold)) 
       || (!isPut && (size < limic_get_threshold)) 
       || origin_dt_derived 
       || target_dt_derived) {
         goto fn_fail;
    }     

    initial_offset = win_ptr->peer_lu[curr_ptr->target_rank].offset;
    initial_len = win_ptr->peer_lu[curr_ptr->target_rank].length;
    offset = initial_offset + win_ptr->disp_units[curr_ptr->target_rank] 
                        * curr_ptr->target_disp;
    win_ptr->peer_lu[curr_ptr->target_rank].offset = offset;

    if(isPut)
        complete_size = limic_tx_comp(limic_fd, curr_ptr->origin_addr,
                        size, &(win_ptr->peer_lu[curr_ptr->target_rank]));
    else
        complete_size = limic_rx_comp(limic_fd, curr_ptr->origin_addr,
                        size, &(win_ptr->peer_lu[curr_ptr->target_rank]));

    win_ptr->peer_lu[curr_ptr->target_rank].offset = initial_offset;
    win_ptr->peer_lu[curr_ptr->target_rank].length = initial_len;

    if( complete_size != size ){
         MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                       "**fail", "**fail %s",
                       "LiMIC: (MPIDI_Win_complete)limic_tx_comp fail");
    }
    else if(source_win_handle != MPI_WIN_NULL)
    {
        NMPI_Comm_rank(win_ptr->comm, &rank);
        for(i=0; i<win_ptr->l_ranks; i++)
        {
           if(win_ptr->l2g_rank[i] == curr_ptr->target_rank){
               dst_l_rank = i;
               break;
           }
        }

        *(win_ptr->limic_cmpl_counter_all[dst_l_rank] + rank) = 1LL;
        return 1;
    }
    else
    {
       return complete_size;
    }

fn_fail:
    return 0;
}
#endif /* _SMP_LIMIC_ */

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

    char                *origin_addr, *remote_addr;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;
    uint32_t            i, size, hca_index, 
                        r_key[MAX_NUM_SUBRAILS],
                        l_key[MAX_NUM_SUBRAILS];

    /*part 1 prepare origin side buffer */
    remote_addr = (char *) win_ptr->remote_post_flags[target_rank];

    size = sizeof(int);
    Get_Pinned_Buf(win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    for (i=0; i<rdma_num_rails; ++i) {
        hca_index = i/rdma_num_rails_per_hca;
        l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
        r_key[i] = win_ptr->post_flag_rkeys[target_rank * rdma_num_hcas + hca_index];
    }

    Post_Put_Put_Get_List(win_ptr, -1, NULL, 
            tmp_vc, (void *)&origin_addr, 
            (void*)&remote_addr, size, 
            l_key, r_key, 0 );

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
                           int my_rank,
                           MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
 
    int             ret, i,j,arrIndex;
    win_info        *win_info_exchange;
    uintptr_t       *cc_ptrs_exchange;
    uintptr_t       *post_flag_ptr_send, *post_flag_ptr_recv;
    int             fallback_trigger = 0;

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided)
    {
        (*win_ptr)->fall_back = 1;
        return;
    }

    /*Allocate structure for window information exchange*/
    win_info_exchange = MPIU_Malloc(comm_size * sizeof(win_info));
    if (!win_info_exchange)
    {
        DEBUG_PRINT("Error malloc win_info_exchange when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /*Allocate memory for completion counter pointers exchange*/
    cc_ptrs_exchange =  MPIU_Malloc(comm_size * sizeof(uintptr_t) * rdma_num_rails);
    if (!cc_ptrs_exchange)
    {
        DEBUG_PRINT("Error malloc cc_ptrs_exchangee when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->fall_back = 0;
    /*Register the exposed buffer for this window */
    if (base != NULL && size > 0) {

        (*win_ptr)->win_dreg_entry = dreg_register(base, size);
        if (NULL == (*win_ptr)->win_dreg_entry) {
            (*win_ptr)->fall_back = 1;
            goto err_base_register;
        }
        for (i=0; i < rdma_num_hcas; ++i) {
            win_info_exchange[my_rank].win_rkeys[i] = 
                 (uint32_t) (*win_ptr)->win_dreg_entry->memhandle[i]->rkey;
        }
    } else {
        (*win_ptr)->win_dreg_entry = NULL;
        for (i = 0; i < rdma_num_hcas; ++i) {
             win_info_exchange[my_rank].win_rkeys[i] = 0;
        }
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter = MPIU_Malloc(sizeof(long long) * comm_size 
                * rdma_num_rails);
    if (NULL == (*win_ptr)->completion_counter) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_buf;
    }

    MPIU_Memset((void *) (*win_ptr)->completion_counter, 0, sizeof(long long)   
            * comm_size * rdma_num_rails);

    (*win_ptr)->completion_counter_dreg_entry = dreg_register(
        (void*)(*win_ptr)->completion_counter, sizeof(long long) * comm_size 
               * rdma_num_rails);
    if (NULL == (*win_ptr)->completion_counter_dreg_entry) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_register;
    }

    for (i = 0; i < rdma_num_rails; ++i){
        cc_ptrs_exchange[my_rank * rdma_num_rails + i] =
               (uintptr_t) ((*win_ptr)->completion_counter + i);
    }

    for (i = 0; i < rdma_num_hcas; ++i){
        win_info_exchange[my_rank].completion_counter_rkeys[i] =
               (uint32_t) (*win_ptr)->completion_counter_dreg_entry->
                            memhandle[i]->rkey;
    }

    /*Register buffer for post flags : from target to origin */
    (*win_ptr)->post_flag = (int *) MPIU_Malloc(comm_size * sizeof(int)); 
    if (!(*win_ptr)->post_flag) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_buf;
    }
    DEBUG_PRINT(
        "rank[%d] : post flag start before exchange is %p\n",
        my_rank,
        (*win_ptr)->post_flag
    ); 
  
    /* Register the post flag */
    (*win_ptr)->post_flag_dreg_entry = 
                dreg_register((void*)(*win_ptr)->post_flag, 
                               sizeof(int)*comm_size);
    if (NULL == (*win_ptr)->post_flag_dreg_entry) {
        /* Fallback case */
        (*win_ptr)->fall_back = 1;
        goto err_postflag_register;
    }

    for (i = 0; i < rdma_num_hcas; ++i)
    {
        win_info_exchange[my_rank].post_flag_rkeys[i] = 
              (uint32_t)((*win_ptr)->post_flag_dreg_entry->memhandle[i]->rkey);
        DEBUG_PRINT(
            "the rank [%d] post_flag rkey before exchange is %x\n", 
            my_rank,
            win_info_exchange[my_rank].post_flag_rkeys[i]
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

    win_info_exchange[my_rank].fall_back = (*win_ptr)->fall_back;    

    /*Exchange the information about rkeys and addresses */
    /* All processes will exchange the setup keys *
     * since each process has the same data for all other
     * processes, use allgather */

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, win_info_exchange,
               sizeof(win_info), MPI_BYTE, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather win_info  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* check if any peers fail */
    for (i = 0; i < comm_size; ++i)
    {
            if (win_info_exchange[i].fall_back != 0)
            {
                fallback_trigger = 1;
            }
    } 
    
    if (fallback_trigger) {
        MPIU_Free((void *) win_info_exchange);
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
        MPIU_Free((void *) (*win_ptr)->pinnedpool_1sc_buf);
        dreg_unregister((*win_ptr)->post_flag_dreg_entry);
        MPIU_Free((void *) (*win_ptr)->post_flag);
        dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
        MPIU_Free((void *) (*win_ptr)->completion_counter);
        dreg_unregister((*win_ptr)->win_dreg_entry);
        (*win_ptr)->fall_back = 1;
        goto fn_exit;
    }

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, cc_ptrs_exchange,
             rdma_num_rails*sizeof(uintptr_t), MPI_BYTE, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error cc pointer  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey array for all other processes */
    (*win_ptr)->win_rkeys = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t) 
                             * rdma_num_hcas);
    if (!(*win_ptr)->win_rkeys) {
        DEBUG_PRINT("Error malloc win->win_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey2 array for all other processes */
    (*win_ptr)->completion_counter_rkeys = (uint32_t *) MPIU_Malloc(comm_size * 
                             sizeof(uint32_t) * rdma_num_hcas); 
    if (!(*win_ptr)->completion_counter_rkeys) {
        DEBUG_PRINT("Error malloc win->completion_counter_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the completion counter array for all other processes */
    (*win_ptr)->all_completion_counter = (long long **) MPIU_Malloc(comm_size * 
                             sizeof(long long *) * rdma_num_rails);
    if (!(*win_ptr)->all_completion_counter) {
        DEBUG_PRINT
            ("error malloc win->all_completion_counter when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the post flag rkey array for all other processes */
    (*win_ptr)->post_flag_rkeys = (uint32_t *) MPIU_Malloc(comm_size *
                                                  sizeof(uint32_t) * rdma_num_hcas);
    if (!(*win_ptr)->post_flag_rkeys) {
        DEBUG_PRINT("error malloc win->post_flag_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the post flag ptr array for all other processes */
    (*win_ptr)->remote_post_flags =
        (int **) MPIU_Malloc(comm_size * sizeof(int *));
    if (!(*win_ptr)->remote_post_flags) {
        DEBUG_PRINT
            ("error malloc win->remote_post_flags when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_hcas; ++j) 
        {
            arrIndex = rdma_num_hcas * i + j;
            (*win_ptr)->win_rkeys[arrIndex] = 
                    win_info_exchange[i].win_rkeys[j];
            (*win_ptr)->completion_counter_rkeys[arrIndex] = 
                    win_info_exchange[i].completion_counter_rkeys[j];
            (*win_ptr)->post_flag_rkeys[arrIndex] = 
                    win_info_exchange[i].post_flag_rkeys[j];
        }
    }

    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_rails; ++j)
        {
            arrIndex = rdma_num_rails * i + j;
            (*win_ptr)->all_completion_counter[arrIndex] = (long long *)
                    ((size_t)(cc_ptrs_exchange[arrIndex])
                    + sizeof(long long) * my_rank * rdma_num_rails);
        }
    }

    post_flag_ptr_send = (uintptr_t *) MPIU_Malloc(comm_size * 
            sizeof(uintptr_t));
    if (!post_flag_ptr_send) {
        DEBUG_PRINT("Error malloc post_flag_ptr_send when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    post_flag_ptr_recv = (uintptr_t *) MPIU_Malloc(comm_size * 
            sizeof(uintptr_t));
    if (!post_flag_ptr_recv) {
        DEBUG_PRINT("Error malloc post_flag_ptr_recv when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; ++i)
    {
        (*win_ptr)->post_flag[i] = i != my_rank ? 0 : 1;
        post_flag_ptr_send[i] = (uintptr_t) &((*win_ptr)->post_flag[i]);
    }

    /* use all to all to exchange the address of post flag */
    ret = NMPI_Alltoall(post_flag_ptr_send, sizeof(uintptr_t), MPI_BYTE, 
            post_flag_ptr_recv, sizeof(uintptr_t), MPI_BYTE, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather post flag ptr  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    for (i = 0; i < comm_size; ++i) {
        (*win_ptr)->remote_post_flags[i] = (int *) post_flag_ptr_recv[i];
        DEBUG_PRINT(" rank is %d remote rank %d,  post flag addr is %p\n",
                my_rank, i, (*win_ptr)->remote_post_flags[i]);
    }

    MPIU_Free(win_info_exchange);
    MPIU_Free(cc_ptrs_exchange);
    MPIU_Free(post_flag_ptr_send);
    MPIU_Free(post_flag_ptr_recv);
    (*win_ptr)->using_lock = 0;
    (*win_ptr)->using_start = 0;
    /* Initialize put/get queue */
    (*win_ptr)->put_get_list_size = 0;
    (*win_ptr)->put_get_list_tail = 0;
    (*win_ptr)->wait_for_complete = 0;
    (*win_ptr)->rma_issued = 0;

    (*win_ptr)->put_get_list =
        (MPIDI_CH3I_RDMA_put_get_list *) MPIU_Malloc( 
            rdma_default_put_get_list_size *
            sizeof(MPIDI_CH3I_RDMA_put_get_list));
    if (!(*win_ptr)->put_get_list) {
        DEBUG_PRINT("Fail to malloc space for window put get list\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

fn_exit:
    
    if (1 == (*win_ptr)->fall_back) {
        (*win_ptr)->using_lock = 0;
        (*win_ptr)->using_start = 0;
        /* Initialize put/get queue */
        (*win_ptr)->put_get_list_size = 0;
        (*win_ptr)->put_get_list_tail = 0;
        (*win_ptr)->wait_for_complete = 0;
        (*win_ptr)->rma_issued = 0;
    }

    return;

  err_pinnedpool_register:
    MPIU_Free((void *) (*win_ptr)->pinnedpool_1sc_buf);
  err_pinnedpool_buf:
    dreg_unregister((*win_ptr)->post_flag_dreg_entry);
  err_postflag_register:
    MPIU_Free((void *) (*win_ptr)->post_flag);
  err_postflag_buf:
    dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
  err_cc_register:
    MPIU_Free((void *) (*win_ptr)->completion_counter);
  err_cc_buf:
    dreg_unregister((*win_ptr)->win_dreg_entry);
  err_base_register:
    win_info_exchange[my_rank].fall_back = (*win_ptr)->fall_back;

    ret = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, win_info_exchange, 
                      sizeof(win_info), MPI_BYTE, comm_ptr->handle);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather window information when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
 
    MPIU_Free((void *) win_info_exchange);
    MPIU_Free((void *) cc_ptrs_exchange);
    goto fn_exit;
     
}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_win_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_LIMIC_win_create(void *base, MPI_Aint size, int comm_size,
                      int g_rank, MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
    int             i, j, mpi_errno=MPI_SUCCESS;
    int             num_local_procs = 0, l_rank = -1, tx_init_size, counter_size;
    MPIDI_VC_t*     vc;

    if(!SMP_INIT || !g_smp_use_limic2 || 
            !MPIDI_CH3I_RDMA_Process.has_limic_one_sided) {
       (*win_ptr)->limic_fallback = 1;
       return;
    }

    if ((*win_ptr)->limic_fallback) {
        return;
    }

    for(i=0; i<comm_size; i++) {
        MPIDI_Comm_get_vc(comm_ptr, i, &vc);
        if(g_rank == i || vc->smp.local_nodes != -1)   
        {  
           num_local_procs++;
        }
    }

    (*win_ptr)->peer_lu = (limic_user *) 
                  MPIU_Malloc(comm_size * sizeof(limic_user));
    if((*win_ptr)->peer_lu == NULL) {
        DEBUG_PRINT("Error malloc peer_lu when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
    memset((*win_ptr)->peer_lu,0,comm_size * sizeof(limic_user));

    (*win_ptr)->l_ranks = num_local_procs;
    (*win_ptr)->l2g_rank = (int *) 
                  MPIU_Malloc(num_local_procs * sizeof(int));
    if((*win_ptr)->l2g_rank == NULL) {
        DEBUG_PRINT("Error malloc l2g_rank when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    j=0;
    for(i=0; i<comm_size; i++) {
        MPIDI_Comm_get_vc(comm_ptr, i, &vc);
        if(g_rank == i || vc->smp.local_nodes != -1) {
            if(g_rank == i) {
                l_rank = j;
            }
            (*win_ptr)->l2g_rank[j] = i;
            j++;
        }
    }

    /*map the window memory*/
    if (base != NULL && size > 0) {
      tx_init_size = limic_tx_init(limic_fd, base, size,
                   &((*win_ptr)->peer_lu[g_rank]));
      if (tx_init_size != size) {
         DEBUG_PRINT("Error limic_tx_init when creating windows\n");
         ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
      }
    }

    NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
         (*win_ptr)->peer_lu, sizeof(limic_user), MPI_BYTE,
         comm_ptr->handle);

    /*allocate completion counter buffers in shared memory*/
    counter_size = sizeof(long long) * comm_size * num_local_procs;
    mpi_errno = mv2_rma_allocate_shm(
                               counter_size,
                               g_rank,
                               &((*win_ptr)->cmpl_shmid),
                               (void **) &((*win_ptr)->limic_cmpl_counter_buf),
                               comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
       DEBUG_PRINT("Error mv2_rma_allocate_shm when creating windows\n");
       ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->limic_cmpl_counter_all = (long long **) MPIU_Malloc(sizeof(long long *) *
                                     num_local_procs);
    if(!(*win_ptr)->limic_cmpl_counter_all) {
       DEBUG_PRINT("Error malloc limic_cmpl_counter_all when creating windows\n");
       ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
  
    for(i=0; i<num_local_procs; i++) {
        (*win_ptr)->limic_cmpl_counter_all[i] = 
                       (*win_ptr)->limic_cmpl_counter_buf + comm_size * i;
        if(i == l_rank) {
            (*win_ptr)->limic_cmpl_counter_me = 
                       (*win_ptr)->limic_cmpl_counter_buf + comm_size * i;
        }
    }
    MPIU_Memset((*win_ptr)->limic_cmpl_counter_me, 0, 
                       comm_size * sizeof(long long));

    /*allocate post flag buffers in shared memory*/ 
    counter_size = sizeof(int) * comm_size * num_local_procs;
    mpi_errno = mv2_rma_allocate_shm(
                              counter_size,
                              g_rank,
                              &((*win_ptr)->post_shmid), 
                              (void **) &((*win_ptr)->limic_post_flag_buf),
                              comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
       DEBUG_PRINT("Error mv2_rma_allocate_shm when creating windows\n");
       ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->limic_post_flag_all = (int **) MPIU_Malloc(sizeof(int *) *
                                                  num_local_procs);
    if(!(*win_ptr)->limic_post_flag_all) {
       DEBUG_PRINT("Error malloc limic_post_flag_all when creating windows\n");
       ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
 
    for(i = 0; i<num_local_procs; i++) {
         (*win_ptr)->limic_post_flag_all[i] = 
                         (*win_ptr)->limic_post_flag_buf + comm_size * i;

         if(i == l_rank) {
              (*win_ptr)->limic_post_flag_me = 
                         (*win_ptr)->limic_post_flag_buf + comm_size * i;
         }
    }
    MPIU_Memset((*win_ptr)->limic_post_flag_me, 0, comm_size * sizeof(int));
    (*win_ptr)->limic_post_flag_me[g_rank] = 1;

    MPIR_Barrier(comm_ptr);

fn_exit:  
    return;

fn_fail:
    goto fn_exit;
}  
#endif /* _SMP_LIMIC_ */


void MPIDI_CH3I_RDMA_win_free(MPID_Win** win_ptr)
{
    if ((*win_ptr)->win_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->win_dreg_entry);
    }

    MPIU_Free((*win_ptr)->win_rkeys);
    MPIU_Free((*win_ptr)->completion_counter_rkeys);
    MPIU_Free((void *) (*win_ptr)->post_flag);
    MPIU_Free((*win_ptr)->post_flag_rkeys);
    MPIU_Free((*win_ptr)->remote_post_flags);
    MPIU_Free((*win_ptr)->put_get_list);

    if ((*win_ptr)->pinnedpool_1sc_dentry) {
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
    }

    MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
    if ((*win_ptr)->completion_counter_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
    }

    if ((*win_ptr)->post_flag_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->post_flag_dreg_entry);
    }

    MPIU_Free((void *) (*win_ptr)->completion_counter);
    MPIU_Free((void *) (*win_ptr)->all_completion_counter);
}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_win_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_LIMIC_win_free(MPID_Win** win_ptr)
{
    MPID_Comm *comm_ptr;
    int i, counter_size, rank;

    MPID_Comm_get_ptr((*win_ptr)->comm, comm_ptr);
    rank = comm_ptr->rank;

    close((*win_ptr)->cmpl_shmid);
    counter_size = sizeof(long long) * 
           comm_ptr->local_size * (*win_ptr)->l_ranks;
    mv2_rma_deallocate_shm((*win_ptr)->limic_cmpl_counter_buf, counter_size);

    close((*win_ptr)->post_shmid);
    counter_size = sizeof(int) * 
           comm_ptr->local_size * (*win_ptr)->l_ranks;
    mv2_rma_deallocate_shm((*win_ptr)->limic_post_flag_buf, counter_size);

    MPIU_Free((*win_ptr)->limic_post_flag_all);
    MPIU_Free((*win_ptr)->limic_cmpl_counter_all);

    MPIU_Free((*win_ptr)->peer_lu);
    MPIU_Free((*win_ptr)->l2g_rank);
}
#endif /*defined(_SMP_LIMIC_)*/

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
            r_key2[i] = win_ptr->completion_counter_rkeys[target_rank*rdma_num_hcas + hca_index];
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
        rail = MRAILI_Send_select_rail(vc_ptr);
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

        vbuf_init_rma_put(v, local_buf[0], lkeys[rail], remote_buf[0],
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
                            void *user_buf[], int length,
                            uint32_t lkeys[], uint32_t rkeys[],
                            int use_multi)
{
     int i, mpi_errno = MPI_SUCCESS;
     int hca_index, rail;
     int index = winptr->put_get_list_tail;
     vbuf *v;
     MPIDI_VC_t *save_vc = vc_ptr;

     if(size <= rdma_eagersize_1sc){    
         winptr->put_get_list[index].origin_addr = local_buf[0];
         winptr->put_get_list[index].target_addr = user_buf[0];
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
    } else if (use_multi == 0) { /* send on a single rail */
        rail = MRAILI_Send_select_rail(vc_ptr);
        v = get_vbuf();
        if (NULL == v) {
            MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
        }

        winptr->put_get_list[index].completion = 1;
        ++(winptr->put_get_list_size);

        vbuf_init_rma_get(v, local_buf[0], lkeys[rail], remote_buf[0],
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
        r_key[i] = win_ptr->win_rkeys[rma_op->target_rank*rdma_num_hcas + hca_index];
        l_key1[i] = l_key[hca_index];
    }

    if(size < rdma_large_msg_rail_sharing_threshold) { 
        Post_Put_Put_Get_List(win_ptr, size, tmp_dreg, tmp_vc, 
                (void *)&origin_addr, (void *)&remote_address, 
                size, l_key1, r_key, 0 );
    } else {
        Post_Put_Put_Get_List(win_ptr, size, tmp_dreg, tmp_vc,
                (void *)&origin_addr, (void *)&remote_address,
                size, l_key1, r_key, 1 );
    }

    return mpi_errno;
}


int iba_get(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_SUBRAILS], 
                        l_key1[MAX_NUM_SUBRAILS], 
                        l_key[MAX_NUM_SUBRAILS];
    dreg_entry          *tmp_dreg = NULL;
    char                *target_addr, *user_addr;
    char                *remote_addr;
    int                 i;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    MPIU_Assert(rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys  */
    remote_addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        Get_Pinned_Buf(win_ptr, &target_addr, size);
        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
        user_addr = rma_op->origin_addr;
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }
        target_addr = user_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }

    MPID_Comm_get_ptr(win_ptr->comm, comm_ptr);
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

   for (i=0; i<rdma_num_rails; ++i)
   {
       hca_index = tmp_vc->mrail.rails[i].hca_index;
       r_key[i] = win_ptr->win_rkeys[rma_op->target_rank*rdma_num_hcas + hca_index];
       l_key1[i] = l_key[hca_index];
   }

   if(size < rdma_large_msg_rail_sharing_threshold) { 
       Post_Get_Put_Get_List(win_ptr, size, tmp_dreg, 
                             tmp_vc, (void *)&target_addr, 
                             (void *)&remote_addr, (void*)&user_addr, size, 
                             l_key1, r_key, 0 );
   } else {
       Post_Get_Put_Get_List(win_ptr, size, tmp_dreg,
                             tmp_vc, (void *)&target_addr,
                             (void *)&remote_addr, (void*)&user_addr, size,
                             l_key1, r_key, 1 );
   }

   return mpi_errno;
}
