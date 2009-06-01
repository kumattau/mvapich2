/* Copyright (c) 2003-2009, The Ohio State University. All rights
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

#include "mpiimpl.h"

#if defined(_OSU_MVAPICH_)
#include <mpimem.h>
#include "mpidimpl.h"
#include "mpicomm.h"
#include "../../mpid/ch3/channels/mrail/src/rdma/coll_shmem.h"
#include <pthread.h>
#ifndef GEN_EXIT_ERR
#define GEN_EXIT_ERR    -1
#endif
#ifndef ibv_error_abort
#define ibv_error_abort(code, message) do {                     \
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

#define MAX_SHMEM_COMM  4
#define MAX_NUM_COMM    10000
#define MAX_ALLOWED_COMM   250
unsigned int comm_registry [MAX_NUM_COMM];
unsigned int comm_registered = 0;
unsigned int comm_count = 0;

int shmem_comm_count = 0;
extern shmem_coll_region *shmem_coll;
static pthread_mutex_t comm_lock  = PTHREAD_MUTEX_INITIALIZER;
extern int g_shmem_coll_blocks;

#define MAX_NUM_THREADS 1024
pthread_t thread_reg[MAX_NUM_THREADS];

void clear_2level_comm (MPID_Comm* comm_ptr)
{
    comm_ptr->shmem_coll_ok = 0;
    comm_ptr->leader_map  = NULL;
    comm_ptr->leader_rank = NULL;
}

void free_2level_comm (MPID_Comm* comm_ptr)
{
    if (comm_ptr->leader_map)  { 
        MPIU_Free(comm_ptr->leader_map);  
     }
    if (comm_ptr->leader_rank) { 
        MPIU_Free(comm_ptr->leader_rank); 
     }
    if (comm_ptr->leader_comm) { 
        PMPI_Comm_free(&(comm_ptr->leader_comm));
     }
    if (comm_ptr->shmem_comm)  { 
        PMPI_Comm_free(&(comm_ptr->shmem_comm));
     }
    clear_2level_comm(comm_ptr);
}

int create_2level_comm (MPI_Comm comm, int size, int my_rank)
{
    static const char FCNAME[] = "create_2level_comm";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm* comm_ptr;
    MPID_Comm* comm_world_ptr;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    MPID_Comm_get_ptr( comm, comm_ptr );
    MPID_Comm_get_ptr( MPI_COMM_WORLD, comm_world_ptr );

    MPIR_Nest_incr();

    int* shmem_group = MPIU_Malloc(sizeof(int) * size);
    if (NULL == shmem_group){
        printf("Couldn't malloc shmem_group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    /* Creating local shmem group */
    int i = 0;
    int local_rank = 0;
    int grp_index = 0;
    MPIDI_VC_t* vc = NULL;
    for (; i < size ; ++i){
       MPIDI_Comm_get_vc(comm_ptr, i, &vc);
       if (my_rank == i || vc->smp.local_nodes >= 0){
           shmem_group[grp_index] = i;
           if (my_rank == i){
               local_rank = grp_index;
           }
           ++grp_index;
       }  
    } 

    /* Creating leader group */
    int leader = 0;
    leader = shmem_group[0];
    MPIU_Free(shmem_group);


    /* Gives the mapping to any process's leader in comm */
    comm_ptr->leader_map = MPIU_Malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_map){
        printf("Couldn't malloc group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    mpi_errno = MPIR_Allgather (&leader, 1, MPI_INT , comm_ptr->leader_map, 1, MPI_INT, comm_ptr);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    int leader_group_size=0;
    int* leader_group = MPIU_Malloc(sizeof(int) * size);
    if (NULL == leader_group){
        printf("Couldn't malloc leader_group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    /* Gives the mapping from leader's rank in comm to 
     * leader's rank in leader_comm */
    comm_ptr->leader_rank = MPIU_Malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_rank){
        printf("Couldn't malloc marker\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    for (i=0; i < size ; ++i){
         comm_ptr->leader_rank[i] = -1;
    }
    int* group = comm_ptr->leader_map;
    grp_index = 0;
    for (i=0; i < size ; ++i){
        if (comm_ptr->leader_rank[(group[i])] == -1){
            comm_ptr->leader_rank[(group[i])] = grp_index;
            leader_group[grp_index++] = group[i];
           
        }
    }

    leader_group_size = grp_index;
    comm_ptr->leader_group_size = leader_group_size;

    MPI_Group subgroup1, comm_group;
    
    mpi_errno = PMPI_Comm_group(comm, &comm_group);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    mpi_errno = PMPI_Group_incl(comm_group, leader_group_size, leader_group, &subgroup1);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    mpi_errno = PMPI_Comm_create(comm, subgroup1, &(comm_ptr->leader_comm));
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }
    MPIU_Free(leader_group);
    MPID_Comm *leader_ptr;
    MPID_Comm_get_ptr( comm_ptr->leader_comm, leader_ptr );
       
    mpi_errno = PMPI_Comm_split(comm, leader, local_rank, &(comm_ptr->shmem_comm));
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    MPID_Comm *shmem_ptr;
    MPID_Comm_get_ptr(comm_ptr->shmem_comm, shmem_ptr);


    int my_local_id, input_flag =0, output_flag=0;
    mpi_errno = PMPI_Comm_rank(comm_ptr->shmem_comm, &my_local_id);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    if (my_local_id == 0){
        pthread_spin_lock(&shmem_coll->shmem_coll_lock);
        ++shmem_coll->shmem_comm_count;
        shmem_comm_count = shmem_coll->shmem_comm_count;
        pthread_spin_unlock(&shmem_coll->shmem_coll_lock);
    }

    shmem_ptr->shmem_coll_ok = 0; 
    /* To prevent Bcast taking the knomial_2level_bcast route */
    mpi_errno = MPIR_Bcast (&shmem_comm_count, 1, MPI_INT, 0, shmem_ptr);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    if (shmem_comm_count <= g_shmem_coll_blocks){
        shmem_ptr->shmem_comm_rank = shmem_comm_count-1;
        input_flag = 1;
    } else{
        input_flag = 0;
    }
    comm_ptr->shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
    mpi_errno = MPIR_Allreduce(&input_flag, &output_flag, 1, MPI_INT, MPI_LAND, comm_ptr);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    if (output_flag == 1){
        comm_ptr->shmem_coll_ok = 1;
        comm_registry[comm_registered++] = comm_ptr->context_id;
    } else{
        comm_ptr->shmem_coll_ok = 0;
        free_2level_comm(comm_ptr);
        mpi_errno = PMPI_Group_free(&subgroup1);
         if(mpi_errno) {
           MPIU_ERR_POP(mpi_errno);
        }
        mpi_errno =PMPI_Group_free(&comm_group);
         if(mpi_errno) {
           MPIU_ERR_POP(mpi_errno);
        }

    
    }
    ++comm_count;

    comm_ptr->bcast_mmap_ptr = NULL;
    comm_ptr->bcast_shmem_file = NULL;
    comm_ptr->bcast_fd = -1;
    comm_ptr->bcast_index = 0;

    MPIR_Nest_decr();
   
    fn_fail: 
       MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);


}

int init_thread_reg(void)
{
    int j = 0;

    for (; j < MAX_NUM_THREADS; ++j)
    {
        thread_reg[j] = -1;
    }

    return 1;
}

int check_split_comm(pthread_t my_id)
{
    int j = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (pthread_equal(thread_reg[j], my_id))
        {
            pthread_mutex_unlock(&comm_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&comm_lock);
    return 1;
}

int disable_split_comm(pthread_t my_id)
{
    int j = 0;
    int found = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (thread_reg[j] == -1)
        {
            thread_reg[j] = my_id;
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&comm_lock);

    if (found == 0)
    {
        printf("Error:max number of threads reached\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    return 1;
}


int enable_split_comm(pthread_t my_id)
{
    int j = 0;
    int found = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (pthread_equal(thread_reg[j], my_id))
        {
            thread_reg[j] = -1;
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&comm_lock);

    if (found == 0)
    {
        printf("Error: Could not locate thread id\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    return 1;
}

int check_comm_registry(MPI_Comm comm)
{
    MPID_Comm* comm_ptr;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    MPID_Comm_get_ptr( comm, comm_ptr );
    int context_id = 0, i =0, my_rank, size;
    context_id = comm_ptr->context_id;

    MPIR_Nest_incr();
    PMPI_Comm_rank(comm, &my_rank);
    PMPI_Comm_size(comm, &size);
    MPIR_Nest_decr();

    for (i = 0; i < comm_registered; i++){
        if (comm_registry[i] == context_id){
            return 1;
        }
    }

    return 0;
}

#endif /* defined(_OSU_MVAPICH_) */
