/* Copyright (c) 2003-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */
#ifdef _SMP_

#include "mpiimpl.h"
#include "mpicomm.h"
#include "../../mpid/osu_ch3/channels/mrail/src/rdma/coll_shmem.h"
#include <pthread.h>
#define MAX_SHMEM_COMM  4
#define MAX_NUM_COMM    10000
#define MAX_ALLOWED_COMM   250
unsigned int comm_registry [MAX_NUM_COMM];
unsigned int comm_registered = 0;
unsigned int comm_count = 0;

int shmem_comm_count = 0;
extern shmem_coll_region *shmem_coll;
static pthread_mutex_t shmem_coll_lock  = PTHREAD_MUTEX_INITIALIZER;

void create_2level_comm (MPI_Comm comm, int size, int my_rank){

    MPID_Comm* comm_ptr;
    MPID_Comm* comm_world_ptr;
    MPID_Comm_get_ptr( comm, comm_ptr );
    MPID_Comm_get_ptr( MPI_COMM_WORLD, comm_world_ptr );

    if (comm_count > MAX_ALLOWED_COMM) return;

    int* shmem_group = malloc(sizeof(int) * size);
    if (NULL == shmem_group){
        printf("Couldn't malloc shmem_group\n");
        exit(0);
    }

    /* Creating local shmem group */
    int i, shmem_grp_size, local_rank;
    int grp_index = 0;
    for (i=0; i < size ; i++){
       if ((my_rank == i) || (MPID_Is_local(comm_ptr, i) == 1)){
           shmem_group[grp_index] = i;
           if (my_rank == i){
               local_rank = grp_index;
           }
           grp_index++;
       }  
    } 

    shmem_grp_size = grp_index;
    
    /* Creating leader group */
    int leader = 0;
    leader = shmem_group[0];

    /* Gives the mapping to any process's leader in comm */
    comm_ptr->leader_map = malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_map){
        printf("Couldn't malloc group\n");
        exit(0);
    }

    MPI_Allgather (&leader, 1, MPI_INT , comm_ptr->leader_map, 1, MPI_INT, comm);

    int leader_group_size=0;
    int* leader_group = malloc(sizeof(int) * size);
    if (NULL == leader_group){
        printf("Couldn't malloc leader_group\n");
        exit(0);
    }

    /* Gives the mapping from leader's rank in comm to 
     * leader's rank in leader_comm */
    comm_ptr->leader_rank = malloc(sizeof(int) * size);
    if (NULL == comm_ptr->leader_rank){
        printf("Couldn't malloc marker\n");
        exit(0);
    }

    for (i=0; i < size ; i++){
         comm_ptr->leader_rank[i] = -1;
    }
    int* group = comm_ptr->leader_map;
    grp_index = 0;
    for (i=0; i < size ; i++){
        if (comm_ptr->leader_rank[(group[i])] == -1){
            comm_ptr->leader_rank[(group[i])] = grp_index;
            leader_group[grp_index++] = group[i];
        }
    }
    leader_group_size = grp_index;

    MPI_Group MPI_GROUP_WORLD, subgroup1, comm_group;
    
    MPI_Comm_group(comm, &comm_group);


    MPI_Group_incl(comm_group, leader_group_size, leader_group, &subgroup1);
    MPI_Comm_create(comm, subgroup1, &(comm_ptr->leader_comm));
    MPID_Comm *leader_ptr;
    MPID_Comm_get_ptr( comm_ptr->leader_comm, leader_ptr );
    
    MPI_Comm_split(comm, leader, local_rank, &(comm_ptr->shmem_comm));
    MPID_Comm *shmem_ptr;
    MPID_Comm_get_ptr(comm_ptr->shmem_comm, shmem_ptr);


    int shmem_collective = 0, my_local_id, input_flag =0, output_flag=0;
    MPI_Comm_rank(comm_ptr->shmem_comm, &my_local_id);

    if (my_local_id == 0){
        pthread_mutex_lock(&shmem_coll_lock);
        shmem_coll->shmem_comm_count++;
        pthread_mutex_unlock(&shmem_coll_lock);
    }

    shmem_comm_count = shmem_coll->shmem_comm_count;
    MPI_Bcast (&shmem_comm_count, 1, MPI_INT, 0, comm_ptr->shmem_comm);

    if (shmem_comm_count <= SHMEM_COLL_BLOCKS){
        shmem_ptr->shmem_comm_rank = shmem_comm_count-1;
        input_flag = 1;
    }
    else{
        input_flag = 0;
    }

    comm_ptr->shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
    MPI_Allreduce(&input_flag, &output_flag, 1, MPI_INT, MPI_LAND, comm);

    if (output_flag == 1){
        comm_ptr->shmem_coll_ok = 1;
        comm_registry[comm_registered++] = comm_ptr->context_id;
    }
    else{
        comm_ptr->shmem_coll_ok = 0;
    }

    ++comm_count;
}

int check_comm_registry(MPI_Comm comm)
{
    MPID_Comm* comm_ptr;
    MPID_Comm_get_ptr( comm, comm_ptr );
    int context_id = 0, i =0, my_rank, size;
    context_id = comm_ptr->context_id;

    MPI_Comm_rank(comm, &my_rank);
    MPI_Comm_size(comm, &size);

    for (i = 0; i < comm_registered; i++){
        if (comm_registry[i] == context_id){
            return 1;
        }
    }

    return 0;
}

#endif
