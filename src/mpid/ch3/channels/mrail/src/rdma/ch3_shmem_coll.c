/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

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

#include "mpidi_ch3i_rdma_conf.h"
#include "mpidi_ch3_impl.h"
#include <mpimem.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "pmi.h"

#include <sched.h>

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#include "coll_shmem.h"
#include "coll_shmem_internal.h"

#include <stdio.h>

typedef unsigned long addrint_t;

struct shmem_coll_mgmt shmem_coll_obj;

int shmem_coll_size = 0;
char *shmem_file = NULL;

char hostname[SHMEM_COLL_HOSTNAME_LEN];
int my_rank;

int g_shmem_coll_blocks = 8;
int g_shmem_coll_max_msg_size = (1 << 17); 

int tuning_table[COLL_COUNT][COLL_SIZE] = {{2048, 1024, 512},
                                         {-1, -1, -1},
                                         {-1, -1, -1}
                                         };
/* array used to tune scatter*/
int size_scatter_tuning_table=4;
struct scatter_tuning scatter_tuning_table[] = {{64, 4096, 8192},{128, 8192, 16384},{256, 4096, 8192},{512, 4096, 8192}};

/*array used to tune gather */
int size_gather_tuning_table=8;
struct gather_tuning gather_tuning_table[] = {{32, 256},{64, 512},{128, 2048},{256, 2048},{384, 8196},{512, 8196},{768,8196},{1024,8196}};


#if defined(CKPT)
extern void Wait_for_CR_Completion();
void *smc_store;
int smc_store_set;
#endif

/* Change the values set inside the array by the one define by the user */
int tuning_init(){

    int i;

    /* If MV2_SCATTER_SMALL_MSG is define*/
    if(user_scatter_small_msg>0){
        for(i=0; i <= size_scatter_tuning_table; i++){
            scatter_tuning_table[i].small = user_scatter_small_msg;
        }
    }

    /* If MV2_SCATTER_MEDIUM_MSG is define */
    if(user_scatter_medium_msg>0){
        for(i=0; i <= size_scatter_tuning_table; i++){
            if(scatter_tuning_table[i].small < user_scatter_medium_msg){ 
                scatter_tuning_table[i].medium = user_scatter_medium_msg;
            }
        }
    }

    /* If MV2_GATHER_SWITCH_POINT is define  */
    if(user_gather_switch_point>0){
        for(i=0; i <= size_gather_tuning_table; i++){
            gather_tuning_table[i].switchp = user_gather_switch_point;
        }
    }

    return 0;
} 


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_init(MPIDI_PG_t *pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    int mpi_errno = MPI_SUCCESS;
#if defined(SOLARIS)
    char *setdir="/tmp";
#else
    char *setdir="/dev/shm";
#endif
    char *shmem_dir, *shmdir;
    size_t pathlen;

    if ((shmdir = getenv("MV2_SHMEM_DIR")) != NULL) {
        shmem_dir = shmdir;
    } else {
        shmem_dir = setdir;
    }
    pathlen = strlen(shmem_dir);

    if (gethostname(hostname, sizeof(char) * SHMEM_COLL_HOSTNAME_LEN) < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"gethostname", strerror(errno));
    }

    PMI_Get_rank(&my_rank);

    /* add pid for unique file name */
    if ((shmem_file = (char *) MPIU_Malloc(pathlen + 
             sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN))) == NULL) {
        MPIU_CHKMEM_SETERR(mpi_errno, sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN), "shared memory filename");
    }

    if (!shmem_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "shmem_file");
    }

    /* unique shared file name */
    sprintf(shmem_file, "%s/ib_shmem_coll-%s-%s-%d.tmp",
            shmem_dir, pg->ch.kvs_name, hostname, getuid());

    /* open the shared memory file */
    shmem_coll_obj.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    
    if (shmem_coll_obj.fd < 0) {
        /* Fallback */
        sprintf(shmem_file, "/tmp/ib_shmem_coll-%s-%s-%d.tmp",
                pg->ch.kvs_name, hostname, getuid());

        shmem_coll_obj.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (shmem_coll_obj.fd < 0) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
                "open", strerror(errno));
        }
    }

    shmem_coll_size = SMPI_ALIGN (SHMEM_COLL_BUF_SIZE + getpagesize()) + SMPI_CACHE_LINE_SIZE;

    if (g_smpi.my_local_id == 0) {
        if (ftruncate(shmem_coll_obj.fd, 0)) {
	    int ftruncate_errno = errno;

            /* to clean up tmp shared file */
            unlink(shmem_file);
   	        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
        }

        /* set file size, without touching pages */
        if (ftruncate(shmem_coll_obj.fd, shmem_coll_size)) {
	    int ftruncate_errno = errno;

            /* to clean up tmp shared file */
            unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
        }

/* Ignoring optimal memory allocation for now */
#if !defined(_X86_64_)
        {
            char *buf = (char *) MPIU_Calloc(shmem_coll_size + 1, sizeof(char));
            
            if (write(shmem_coll_obj.fd, buf, shmem_coll_size) != shmem_coll_size) {
  		        int write_errno = errno;
                MPIU_Free(buf);
 		        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
 			    "%s: %s", "write", strerror(write_errno));
            }
            MPIU_Free(buf);
        }
#endif /* !defined(_X86_64_) */

        if (lseek(shmem_coll_obj.fd, 0, SEEK_SET) != 0) {
	         int lseek_errno = errno;

            /* to clean up tmp shared file */
            unlink(shmem_file);
	        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
        }

    }

    if(tune_parameter==1){
        tuning_init();
    }
    
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Mmap
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_Mmap()
{
    int i = 0;
    int j = 0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);

    shmem_coll_obj.mmap_ptr = mmap(0, shmem_coll_size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), shmem_coll_obj.fd,
                         0);
    if (shmem_coll_obj.mmap_ptr == (void *) -1) {
	int mmap_errno = errno;

        /* to clean up tmp shared file */
        unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"mmap", strerror(mmap_errno));
    }

#if defined(CKPT)
    if (smc_store_set) {
        MPIU_Memcpy(shmem_coll_obj.mmap_ptr, smc_store, shmem_coll_size);
	MPIU_Free(smc_store);
	smc_store_set = 0;
    }
#endif

    shmem_coll = (shmem_coll_region *) shmem_coll_obj.mmap_ptr;

    if (g_smpi.my_local_id == 0) {
      MPIU_Memset(shmem_coll_obj.mmap_ptr, 0, shmem_coll_size);

        for (j=0; j < SHMEM_COLL_NUM_COMM; ++j) {
            for (i = 0; i < SHMEM_COLL_NUM_PROCS; ++i) {
                shmem_coll->child_complete_bcast[j][i] = 0;
            }

            for (i = 0; i < SHMEM_COLL_NUM_PROCS; ++i) { 
                shmem_coll->root_complete_gather[j][i] = 1;
            }
        }
        pthread_spin_init(&shmem_coll->shmem_coll_lock, 0);

#if defined(CKPT)
	/*
	 * FIXME: The second argument to pthread_spin_init() indicates whether the
	 * Lock can be accessed by a process other than the one that initialized
	 * it. So, it should actually be PTHREAD_PROCESS_SHARED. However, the
	 * "shmem_coll_lock" above sets this to 0. Hence, I am doing the same.
	 */
	pthread_spin_init(&shmem_coll->cr_smc_spinlock, PTHREAD_PROCESS_SHARED);
	shmem_coll->cr_smc_cnt = 0;
#endif
    }
    
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_finalize()
{
  MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);
  MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);

#if defined(CKPT)

    extern int g_cr_in_progress;

    if (g_cr_in_progress) {
            /* Wait for other local processes to check-in */
            pthread_spin_lock(&shmem_coll->cr_smc_spinlock);
            ++(shmem_coll->cr_smc_cnt);
            pthread_spin_unlock(&shmem_coll->cr_smc_spinlock);
            while(shmem_coll->cr_smc_cnt < g_smpi.num_local_nodes);

            if (g_smpi.my_local_id == 0) {
                smc_store = MPIU_Malloc(shmem_coll_size);
                MPIU_Memcpy(smc_store, shmem_coll_obj.mmap_ptr, shmem_coll_size);
                smc_store_set = 1;
            }
    }

#endif

    /* unmap the shared memory file */
    munmap(shmem_coll_obj.mmap_ptr, shmem_coll_size);
    close(shmem_coll_obj.fd);
    MPIU_Free(shmem_file);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);
    return MPI_SUCCESS;
}


void MPIDI_CH3I_SHMEM_COLL_Unlink()
{
    unlink(shmem_file);
}


/* Shared memory gather: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_GetShmemBuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i = 1, cnt=0;
    char* shmem_coll_buf = (char*)(&(shmem_coll->shmem_coll_buf));
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->child_complete_gather[shmem_comm_rank][i] == 0) {
#if defined(CKPT)
  		Wait_for_CR_Completion();
#endif
                MPIDI_CH3I_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

            }
        }
        /* Set the completion flags back to zero */
        for (i = 1; i < size; ++i) { 
            shmem_coll->child_complete_gather[shmem_comm_rank][i] = 0;
        }

        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    } else {
        while (shmem_coll->root_complete_gather[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
   	    Wait_for_CR_Completion();
#endif
            MPIDI_CH3I_Progress_test(); 
                /* Yield once in a while */
            MPIU_THREAD_CHECK_BEGIN
            ++cnt;
            if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END

        }

        shmem_coll->root_complete_gather[shmem_comm_rank][rank] = 0;
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
}


/* Shared memory bcast: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_Bcast_GetBuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_Bcast_GetBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i = 1, cnt=0;
    char* shmem_coll_buf = (char*)(&(shmem_coll->shmem_coll_buf) +
                               g_shmem_coll_blocks*SHMEM_COLL_BLOCK_SIZE);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->child_complete_bcast[shmem_comm_rank][i] == 1) {
#if defined(CKPT)
  		Wait_for_CR_Completion();
#endif
                MPIDI_CH3I_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

            }
        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    } else {
        while (shmem_coll->child_complete_bcast[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
   	    Wait_for_CR_Completion();
#endif
            MPIDI_CH3I_Progress_test(); 
                /* Yield once in a while */
            MPIU_THREAD_CHECK_BEGIN
            ++cnt;
            if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END

        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);
}

/* Shared memory bcast: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_Bcast_Complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_Bcast_Complete(int size, int rank, int shmem_comm_rank)
{
    int i = 1, cnt=0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETBCASTCOMPLETE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETBCASTCOMPLETE);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->child_complete_bcast[shmem_comm_rank][i] = 1;
        } 
    } else {
            shmem_coll->child_complete_bcast[shmem_comm_rank][rank] = 0;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
}



#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_SetGatherComplete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int size, int rank, int shmem_comm_rank)
{
    int i = 1;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->root_complete_gather[shmem_comm_rank][i] = 1;
        }
    } else {
        shmem_coll->child_complete_gather[shmem_comm_rank][rank] = 1;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Barrier_gather
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int size, int rank, int shmem_comm_rank)
{
    int i = 1, cnt=0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->barrier_gather[shmem_comm_rank][i] == 0) {
#if defined(CKPT)
                Wait_for_CR_Completion();
#endif
                MPIDI_CH3I_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END
            }
        }
        for (i = 1; i < size; ++i) { 
            shmem_coll->barrier_gather[shmem_comm_rank][i] = 0; 
        }
    } else {
        shmem_coll->barrier_gather[shmem_comm_rank][rank] = 1;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Barrier_bcast
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int size, int rank, int shmem_comm_rank)
{
    int i = 1, cnt=0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->barrier_bcast[shmem_comm_rank][i] = 1;
        }
    } else {
        while (shmem_coll->barrier_bcast[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
	        Wait_for_CR_Completion();
#endif
                MPIDI_CH3I_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

        }
        shmem_coll->barrier_bcast[shmem_comm_rank][rank] = 0;
    }

    MPIDI_CH3I_Progress_test();
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);
}


void lock_shmem_region()
{
    pthread_spin_lock(&shmem_coll->shmem_coll_lock);
}

void unlock_shmem_region()
{
    pthread_spin_unlock(&shmem_coll->shmem_coll_lock);
}

void increment_shmem_comm_count()
{
    ++ shmem_coll->shmem_comm_count;
}
int get_shmem_comm_count()
{
    return shmem_coll->shmem_comm_count;
}
