/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
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


#ifndef _COLL_SHMEM_
#define _COLL_SHMEM_
#include <pthread.h>
#include "mpidimpl.h"

#if defined(_SMP_LIMIC_)
#define LIMIC_COLL_NUM_COMM  128
#endif /* #if defined(_SMP_LIMIC_) */ 

#define PID_CHAR_LEN 22

#define SHMEM_COLL_HOSTNAME_LEN  (255)

#define SHMEM_SMALLEST_SIZE (64)

#define SHMEM_MAX_INT ((unsigned int)(-1))

#define MV2_DEFAULT_SHMEM_BCAST_LEADERS    4096
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_SMALL      384
#define MV2_GATHER_DIRECT_SYSTEM_SIZE_MEDIUM     1024

#define MV2_INTER_NODE_KNOMIAL_FACTOR_MAX 8
#define MV2_INTER_NODE_KNOMIAL_FACTOR_MIN 2
#define MV2_INTRA_NODE_KNOMIAL_FACTOR_MAX 8
#define MV2_INTRA_NODE_KNOMIAL_FACTOR_MIN 2 

#if defined(_IA32_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8)
#define SHMEM_AVAIL(a)	                                  \
 ((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_IA64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)                                    \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_X86_64_)

#define SHMEM_CACHE_LINE_SIZE 128
#define SHMEM_ALIGN(a)                                    \
((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFFFFFFFFFF8)
#define SHMEM_AVAIL(a)                                    \
 ((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(_EM64T_)

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a + SHMEM_CACHE_LINE_SIZE)
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#elif defined(MAC_OSX)

#define SHMEM_CACHE_LINE_SIZE 16
#define SHMEM_ALIGN(a)                                   \
(((a + SHMEM_CACHE_LINE_SIZE + 7) & 0xFFFFFFF8))
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#else

#define SHMEM_CACHE_LINE_SIZE 64
#define SHMEM_ALIGN(a) (a + SHMEM_CACHE_LINE_SIZE)
#define SHMEM_AVAIL(a)                                   \
((a & 0xFFFFFFFFFFFFFFF8) - SHMEM_CACHE_LINE_SIZE)

#endif


int MPIDI_CH3I_SHMEM_COLL_init(MPIDI_PG_t *pg, int local_id);

int MPIDI_CH3I_SHMEM_COLL_Mmap(MPIDI_PG_t *pg, int local_id); 

int MPIDI_CH3I_SHMEM_COLL_finalize(int local_id, int num_local_nodes);

void MPIDI_CH3I_SHMEM_COLL_Unlink(void);

void MV2_Read_env_vars(void);

#define SHMEM_COLL_BLOCK_SIZE (MPIDI_Process.my_pg->ch.num_local_processes * mv2_g_shmem_coll_max_msg_size)


#define COLL_COUNT              7
#define COLL_SIZE               3
#define ALLGATHER_ID            0
#define ALLREDUCE_SHMEM_ID      1
#define ALLREDUCE_2LEVEL_ID     2
#define BCAST_KNOMIAL_ID        3
#define BCAST_SHMEM_ID          4
#define REDUCE_SHMEM_ID         5
#define REDUCE_2LEVEL_ID        6

#define SMALL                   0
#define MEDIUM                  1
#define LARGE                   2

#define MV2_MAX_NB_THRESHOLDS  10

#define MV2_PARA_PACKET_SIZE    5

extern int mv2_tuning_table[COLL_COUNT][COLL_SIZE]; 

struct scatter_tuning{
    int numproc;
    int small;
    int medium;
};

struct gather_tuning{
    int numproc;
    int switchp;
};

struct allgatherv_tuning{
    int numproc;
    int switchp;
};

#define BCAST_LEN 20
#define SHMEM_BCAST_FLAGS	1024
/*
 * We're converting this into a environment variable
 * #define SHMEM_BCAST_LEADERS     1024
 */
#define SHMEM_BCAST_METADATA	(sizeof(addrint_t) + 2*sizeof(int))       
  /* METADATA: buffer address, offset, num_bytes */ 

extern int mv2_g_shmem_coll_max_msg_size;
extern int mv2_g_shmem_coll_blocks;
extern int mv2_shmem_coll_num_procs;
extern int mv2_shmem_coll_num_comm;
extern int mv2_shmem_coll_spin_count;
extern int mv2_enable_shmem_collectives;
int is_shmem_collectives_enabled();

extern struct coll_runtime mv2_coll_param;
void MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int, int, int, void**);
void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int, int, int);
int create_allgather_comm(MPID_Comm * comm_ptr, int *errflag);

extern int mv2_tune_parameter;

/* Use for collective tuning based on arch detection*/
void MV2_collectives_arch_init(int heterogeneity);
void MV2_collectives_arch_finalize();

/* Use for allgather_osu.c */
#define MV2_ALLGATHER_SMALL_SYSTEM_SIZE       128
#define MV2_ALLGATHER_MEDIUM_SYSTEM_SIZE      256
#define MV2_ALLGATHER_LARGE_SYSTEM_SIZE       512 
extern int mv2_allgather_ranking;

/* Use for allgatherv_osu.c */
extern int mv2_size_mv2_allgatherv_mv2_tuning_table;
extern struct allgatherv_tuning mv2_allgatherv_mv2_tuning_table[4];
extern int mv2_user_allgatherv_switch_point;

/* Use for scatter_osu.c*/
extern int mv2_user_scatter_small_msg;
extern int mv2_user_scatter_medium_msg;
extern int mv2_size_mv2_scatter_mv2_tuning_table;
extern struct scatter_tuning mv2_scatter_mv2_tuning_table[4];
extern int mv2_use_two_level_scatter; 
extern int mv2_use_direct_scatter;
#if defined(_MCST_SUPPORT_)
extern int mv2_use_mcast_scatter;
extern int mv2_mcast_scatter_msg_size; 
extern int mv2_mcast_scatter_small_sys_size;
extern int mv2_mcast_scatter_large_sys_size;
#endif  /* #if defined(_MCST_SUPPORT_) */ 

/* Use inside reduce_osu.c*/
extern int mv2_user_reduce_two_level;
extern int mv2_user_allgather_two_level;

/* Use inside allreduce_osu.c*/
extern int mv2_disable_shmem_allreduce;
extern int mv2_user_allreduce_two_level;
#if defined(_MCST_SUPPORT_)
extern int mv2_use_mcast_allreduce; 
extern int mv2_mcast_allreduce_small_msg_size; 
extern int mv2_mcast_allreduce_large_msg_size; 
#endif  /* #if defined(_MCST_SUPPORT_) */ 

/* Use inside alltoall_osu.h */
extern int mv2_use_xor_alltoall; 


/* Use inside barrier_osu.c*/
extern int mv2_disable_shmem_barrier;
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int, int, int);
extern void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int, int, int);


/* Use inside bcast_osu.c */
extern int  mv2_bcast_short_msg; 
extern int  mv2_bcast_large_msg; 
extern int  mv2_knomial_2level_bcast_system_size_threshold;
extern int  mv2_knomial_2level_bcast_message_size_threshold;
extern int  mv2_enable_knomial_2level_bcast;
extern int  mv2_inter_node_knomial_factor;
extern int  mv2_intra_node_knomial_factor;
extern int  mv2_scatter_rd_inter_leader_bcast; 
extern int  mv2_scatter_ring_inter_leader_bcast;
extern int  mv2_knomial_intra_node_threshold; 
extern int  mv2_knomial_inter_leader_threshold; 
extern int  mv2_knomial_inter_leader_bcast;
extern int  mv2_enable_shmem_bcast;
extern int  mv2_bcast_two_level_system_size; 
extern int  mv2_alltoall_inplace_old;

extern int mv2_bcast_scatter_ring_overlap;
extern int mv2_bcast_scatter_ring_overlap_msg_upperbound;
extern int mv2_bcast_scatter_ring_overlap_cores_lowerbound;

/* Used inside reduce_osu.c */
extern int mv2_disable_shmem_reduce;
extern int mv2_use_knomial_reduce;
extern int mv2_reduce_inter_knomial_factor;
extern int mv2_reduce_intra_knomial_factor;
extern int MPIR_Reduce_two_level_helper_MV2(const void *sendbuf,
                                     void *recvbuf,
                                     int count,
                                     MPI_Datatype datatype,
                                     MPI_Op op,
                                     int root,
                                     MPID_Comm * comm_ptr, int *errflag); 
extern int MPIR_Reduce_redscat_gather_MV2(const void *sendbuf,
                                          void *recvbuf,
                                          int count,
                                          MPI_Datatype datatype,
                                          MPI_Op op,
                                          int root,
                                          MPID_Comm * comm_ptr, int *errflag); 
extern int MPIR_Reduce_binomial_MV2(const void *sendbuf,
                                    void *recvbuf,
                                    int count,
                                    MPI_Datatype datatype,
                                    MPI_Op op,
                                    int root,
                                    MPID_Comm * comm_ptr, int *errflag); 





/* Use inside red_scat_osu.c */
#define MPIR_RED_SCAT_SHORT_MSG 64
#define MPIR_RED_SCAT_LONG_MSG  512*1024
extern int mv2_red_scat_short_msg;
extern int mv2_red_scat_long_msg;

/* Lock/unlock shmem region */
void lock_shmem_region(void);
void unlock_shmem_region(void);

/* utils */
int mv2_increment_shmem_coll_counter(MPID_Comm *comm_ptr); 
int mv2_increment_allgather_coll_counter(MPID_Comm *comm_ptr); 
void increment_mv2_shmem_comm_count(void);
int get_mv2_shmem_comm_count(void);
int MPIDI_CH3I_SHMEM_Coll_get_free_block(); 
void MPIDI_CH3I_SHMEM_Coll_Block_Clear_Status(int block_id); 
#if defined(_SMP_LIMIC_)
void UpdateNumCoresPerSock(int numcores);
void UpdateNumSocketsPerNode(int numSocketsNode);
void increment_mv2_limic_comm_count();
int get_mv2_limic_comm_count();
extern int mv2_max_limic_comms;
extern int limic_fd;
#endif
void MPIDI_CH3I_SHMEM_Bcast_GetBuf(int, int, int, void**);
void MPIDI_CH3I_SHMEM_Bcast_Complete(int ,int , int);
int init_thread_reg(void);

extern int mv2_use_osu_collectives;
extern int mv2_use_anl_collectives;

/* Comm functions*/
extern int split_comm;
int check_split_comm(pthread_t);
int disable_split_comm(pthread_t);
int create_2level_comm (MPI_Comm, int, int);
int free_2level_comm (MPID_Comm *);
int enable_split_comm(pthread_t);
void MPIR_pof2_comm(MPID_Comm *, int, int);

/*Fn pointers for collectives */
int (*reduce_fn)(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, int root, MPID_Comm * comm_ptr, int *errflag);

#ifdef _ENABLE_CUDA_
int cuda_stage_alloc(void **, int, void **, int,
                      int, int, int);
void cuda_stage_free (void **, void **, int, int, 
                        int);
void CUDA_COLL_Finalize ();                        
void cuda_coll_pack (void **, int *, MPI_Datatype *,
                     void **, int *, MPI_Datatype *,
                     int, int, int);
void cuda_coll_unpack (int *, int);
#endif /*_ENABLE_CUDA_*/

extern int mv2_shm_window_size;
extern int mv2_shm_slot_len;
extern int mv2_use_slot_shmem_coll;
extern int mv2_use_slot_shmem_bcast;
extern int mv2_use_mcast_pipeline_shm;

#define MV2_SHM_ALIGN (128)

#define MV2_SHM_ALIGN_LEN(len, align_unit)          \
{                                                   \
    len = ((int)(((len)+align_unit-1) /             \
                align_unit)) * align_unit;          \
}
#define IS_SHMEM_WINDOW_FULL(start, end) \
    ((((int)(start) - (end)) >= mv2_shm_window_size -1) ? 1 : 0)

typedef struct shm_slot_t {
    volatile uint32_t psn __attribute__((aligned(MV2_SHM_ALIGN)));
    char buf[] __attribute__((aligned(MV2_SHM_ALIGN)));
} shm_slot_t;

typedef struct shm_queue_t {
    shm_slot_t **shm_slots;
}shm_queue_t;

typedef struct shm_info_t {
    char *buffer;
    char *file_name;
    int local_rank;
    int local_size;
    int file_fd;
    int size;
    int count;
    int write;
    int read;
    int tail;
    shm_queue_t *queue;
} shmem_info_t;

shmem_info_t * mv2_shm_coll_init(int id, int local_rank, int local_size);
void mv2_shm_coll_cleanup(shmem_info_t * shmem);
void mv2_shm_barrier(shmem_info_t * shmem);
void mv2_shm_bcast(shmem_info_t * shmem, char *buf, int len, int root);
void mv2_shm_reduce(shmem_info_t *shmem, char *buf, int len, 
                        int count, int root, MPI_User_function *uop, MPI_Datatype datatype);

#endif  /* _COLL_SHMEM_ */
