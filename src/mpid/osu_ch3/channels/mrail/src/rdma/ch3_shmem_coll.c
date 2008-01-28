/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
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

#ifdef _AFFINITY_
#include <sched.h>
#endif /*_AFFINITY_*/

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#include "mpidi_ch3_impl.h"
#include "smp_smpi.h"
#include "coll_shmem.h"
#include <stdio.h>

struct shmem_coll_mgmt shmem_coll_obj;
extern struct smpi_var smpi;

int shmem_coll_size = 0;
char *shmem_file = NULL;

char hostname[SHMEM_COLL_HOSTNAME_LEN];
int my_rank;

int 	shmem_coll_blocks = 8;
int 	shmem_coll_max_msg_size = (1<<17);

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_init(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    unsigned int i, j, size, size_pool, pool, pid, wait;
    int local_num, sh_size, pid_len, rq_len, param_len, limit_len;
    struct stat file_status;
    int pagesize = getpagesize();
    char *value;
    struct shared_mem *shmem;
#ifdef _X86_64_
    volatile char tmpchar;
#endif

    if (gethostname(hostname, sizeof(char) * SHMEM_COLL_HOSTNAME_LEN) < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"gethostname", strerror(errno));
    }

    PMI_Get_rank(&my_rank);

    /* add pid for unique file name */
    shmem_file = (char *) malloc(sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    if(!shmem_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "shmem_file");
    }

    /* unique shared file name */
    sprintf(shmem_file, "/tmp/ib_shmem_coll-%s-%s-%d.tmp",
            pg->ch.kvs_name, hostname, getuid());

    /* open the shared memory file */
    shmem_coll_obj.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (shmem_coll_obj.fd < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"open", strerror(errno));
    }


    shmem_coll_size = SMPI_ALIGN (SHMEM_COLL_BUF_SIZE + pagesize) + SMPI_CACHE_LINE_SIZE;

    if (smpi.my_local_id == 0) {
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
#ifndef _X86_64_
        {
            char *buf;
            buf = (char *) calloc(shmem_coll_size + 1, sizeof(char));
            if (write(shmem_coll_obj.fd, buf, shmem_coll_size) != shmem_coll_size) {
		int write_errno = errno;

                free(buf);
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "write", strerror(write_errno));
            }
            free(buf);
        }

#endif
        if (lseek(shmem_coll_obj.fd, 0, SEEK_SET) != 0) {
	    int lseek_errno = errno;

            /* to clean up tmp shared file */
            unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
        }

    }

fn_exit:
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
    int i = 0, j = 0;
    int mpi_errno = MPI_SUCCESS;

    shmem_coll_obj.mmap_ptr = mmap(0, shmem_coll_size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), shmem_coll_obj.fd,
                         0);
    if (shmem_coll_obj.mmap_ptr == (void *) -1) {
	int mmap_errno = errno;

        /* to clean up tmp shared file */
        unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"mmap", strerror(errno));
    }

    shmem_coll = (shmem_coll_region *) shmem_coll_obj.mmap_ptr;

    if (smpi.my_local_id == 0){
        memset(shmem_coll_obj.mmap_ptr, 0, shmem_coll_size);
        for(j=0; j < SHMEM_COLL_NUM_COMM; j++){
            for (i = 0; i < SHMEM_COLL_NUM_PROCS; i++){
                shmem_coll->child_complete_bcast[j][i] = 1;
            }
            for (i = 0; i < SHMEM_COLL_NUM_PROCS; i++){
                shmem_coll->root_complete_gather[j][i] = 1;
            }
        }
        pthread_spin_init(&shmem_coll->shmem_coll_lock,0);
    }
    
fn_exit:
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
    /* unmap the shared memory file */
    munmap(shmem_coll_obj.mmap_ptr, shmem_coll_size);

    close(shmem_coll_obj.fd);
    
    free(shmem_file);
    return MPI_SUCCESS;
}

void MPIDI_CH3I_SHMEM_COLL_Unlink(){
        unlink(shmem_file);
}

/* Shared memory gather: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Gather
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i,myid;
    char value = 0;
    char* dest_buf;
    char* shmem_coll_buf = (char*)(&(shmem_coll->shmem_coll_buf));

    myid = rank;

    if (myid == 0){
        
        for (i=1; i < size; i++){ 
            while (shmem_coll->child_complete_gather[shmem_comm_rank][i] == 0)
            {
                MPIDI_CH3I_Progress_test();
            };
        }
        /* Set the completion flags back to zero */
        for (i=1; i < size; i++){ 
            shmem_coll->child_complete_gather[shmem_comm_rank][i] = 0;
        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank*SHMEM_COLL_BLOCK_SIZE;
    }
    else{
        while (shmem_coll->root_complete_gather[shmem_comm_rank][myid] == 0)
        {
            MPIDI_CH3I_Progress_test(); 
        };
        shmem_coll->root_complete_gather[shmem_comm_rank][myid] = 0;
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank*SHMEM_COLL_BLOCK_SIZE;
    }
}



void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int size, int rank, int shmem_comm_rank)
{

    int i, myid;
    myid = rank;

    if (myid == 0){
        for (i=1; i < size; i++){ 
            shmem_coll->root_complete_gather[shmem_comm_rank][i] = 1;
        }
    }
    else{
        shmem_coll->child_complete_gather[shmem_comm_rank][myid] = 1;
    }
}

void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int size, int rank, int shmem_comm_rank)
{
    int i, myid;
    myid = rank;

    if (rank == 0){
        for (i=1; i < size; i++){ 
            while (shmem_coll->barrier_gather[shmem_comm_rank][i] == 0)
            {
                MPIDI_CH3I_Progress_test();
            }
        }
        for (i=1; i < size; i++){ 
            shmem_coll->barrier_gather[shmem_comm_rank][i] = 0; 
        }
    }
    else{
        shmem_coll->barrier_gather[shmem_comm_rank][myid] = 1;
    }
}

void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int size, int rank, int shmem_comm_rank)
{
    int i, myid;
    myid = rank;

    if (rank == 0){
        for (i=1; i < size; i++){ 
            shmem_coll->barrier_bcast[shmem_comm_rank][i] = 1;
        }
    }
    else{
        while (shmem_coll->barrier_bcast[shmem_comm_rank][myid] == 0)
        {
            MPIDI_CH3I_Progress_test();
        }
        shmem_coll->barrier_bcast[shmem_comm_rank][myid] = 0;
    }
                MPIDI_CH3I_Progress_test();
}
#endif

/* vi:set sw=4 tw=80: */
