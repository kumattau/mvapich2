/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/mman.h>
#include <errno.h>
#include "pmi.h"

#ifdef _AFFINITY_
#include <sched.h>
#endif /*_AFFINITY_*/

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#include "mpidi_ch3_impl.h"
#include "smp_smpi.h"

#include <stdio.h>

#ifdef _X86_64_
    #define _AFFINITY_
#endif

#ifdef _AFFINITY_
unsigned int viadev_enable_affinity = 1;
#endif

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

/* Macros for flow control and rqueues management */
#define SMPI_TOTALIN(sender,receiver)                               \
smpi_shmem->rqueues_params[sender].params[receiver].msgs_total_in

#define SMPI_TOTALOUT(sender,receiver)                              \
smpi_shmem->rqueues_flow_out[receiver][sender].msgs_total_out

#define SMPI_CURRENT(sender,receiver)                               \
smpi_shmem->rqueues_params[receiver].params[sender].current

#define SMPI_NEXT(sender,receiver)                                  \
smpi_shmem->rqueues_params[sender].params[receiver].next

#define SMPI_FIRST(sender,receiver)                                 \
smpi_shmem->rqueues_limits[receiver][sender].first

#define SMPI_LAST(sender,receiver)                                  \
smpi_shmem->rqueues_limits[receiver][sender].last

struct smpi_var smpi;
struct shared_mem *smpi_shmem;
static char hostname[HOSTNAME_LEN];
int SMP_INIT = 0;
int SMP_ONLY = 0;
static volatile void *current_ptr[SMPI_MAX_NUMLOCALNODES];
static int current_bytes[SMPI_MAX_NUMLOCALNODES];
static int total_bytes[SMPI_MAX_NUMLOCALNODES];

int      smp_eagersize      = SMP_EAGERSIZE;
int      smpi_length_queue  = SMPI_LENGTH_QUEUE;


#if defined(MAC_OSX) || defined(_PPC64_) 
#ifdef __GNUC__
/* can't use -ansi for vxworks ccppc or this will fail with a syntax error
 * */
#define STBAR()  asm volatile ("sync": : :"memory")     /* ": : :" for C++ */
#define READBAR() asm volatile ("sync": : :"memory")
#define WRITEBAR() asm volatile ("sync": : :"memory")
#else
#if  defined (__IBMC__) || defined (__IBMCPP__)
extern void __iospace_eieio(void);
extern void __iospace_sync(void);
#define STBAR()   __iospace_sync ()
#define READBAR() __iospace_sync ()
#define WRITEBAR() __iospace_eieio ()
#else                           /*  defined (__IBMC__) || defined (__IBMCPP__) */
#error Do not know how to make a store barrier for this system
#endif                          /*  defined (__IBMC__) || defined (__IBMCPP__) */
#endif

#ifndef WRITEBAR
#define WRITEBAR() STBAR()
#endif
#ifndef READBAR
#define READBAR() STBAR()
#endif

#else
#define WRITEBAR()
#define READBAR()
#endif

static int smpi_exchange_info(MPIDI_PG_t *pg);

/*********** Declaration for locally used buffer ***********/
static inline void smpi_malloc_assert(void *ptr, char *fct, char *msg)
{
    int rank;

    PMI_Get_rank(&rank);
    if (NULL == ptr) {
        fprintf(stderr, "Cannot Allocate Memory: [%d] in function %s, context: %s\n",
                         rank, fct, msg);
        exit(-1);
    }
}

static inline int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    host_id = (unsigned long)(((struct in_addr *) hostent->h_addr_list[0])->s_addr);
    return host_id;
}

static inline int smpi_get_avail_length(int dest)
{
    int avail;
    if (SMPI_TOTALIN(smpi.my_local_id, dest) >=
        SMPI_TOTALOUT(smpi.my_local_id, dest)) {
        avail = (smpi.available_queue_length -
                 (SMPI_TOTALIN(smpi.my_local_id, dest) -
                  SMPI_TOTALOUT(smpi.my_local_id, dest)));
    } else {
        avail = smpi.available_queue_length -
            (SMPI_MAX_INT - SMPI_TOTALOUT(smpi.my_local_id, dest) +
             SMPI_TOTALIN(smpi.my_local_id, dest));
    }

	avail = ((avail / (int) SMPI_CACHE_LINE_SIZE) * SMPI_CACHE_LINE_SIZE) -
		SMPI_CACHE_LINE_SIZE;

	if (avail < 0) avail = 0;
    return avail;
}

static inline void smpi_complete_send(unsigned int my_id,
                                      unsigned int destination,
                                      unsigned int length)
{
    SMPI_NEXT(my_id, destination) += SMPI_ALIGN(length);

    if (SMPI_NEXT(my_id, destination) > SMPI_LAST(my_id, destination)) {
        SMPI_NEXT(my_id, destination) = SMPI_FIRST(my_id, destination);
    }
    WRITEBAR();
    SMPI_TOTALIN(my_id, destination) += SMPI_ALIGN(length);

}

static inline void smpi_complete_recv(unsigned int from_grank,
                                      unsigned int my_id,
                                      unsigned int length)
{
    SMPI_CURRENT(from_grank, my_id) += SMPI_ALIGN(length);

    if (SMPI_CURRENT(from_grank, my_id) > SMPI_LAST(from_grank, my_id)) {
        SMPI_CURRENT(from_grank, my_id) = SMPI_FIRST(from_grank, my_id);
    }
    WRITEBAR();
    SMPI_TOTALOUT(from_grank, my_id) += SMPI_ALIGN(length);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_process_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int MPIDI_CH3I_SMP_Process_header(MPIDI_VC_t * vc,
                                                MPIDI_CH3_Pkt_t * pkt)
{
    int mpi_errno = MPI_SUCCESS;

    if (MPIDI_CH3_PKT_RNDV_R3_DATA == pkt->type) {
        MPID_Request *rreq;

        MPID_Request_get_ptr(((MPIDI_CH3_Pkt_rndv_r3_data_t *) (pkt))->
                             receiver_req_id, rreq);
        DEBUG_PRINT("R3 data received, don't need to proceed\n");
        vc->smp.recv_active = rreq;
        goto fn_exit;
    }
    mpi_errno = MPIDI_CH3U_Handle_recv_pkt(vc, pkt, &vc->smp.recv_active);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**fail",
                                         0);
        MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_READ);
        return mpi_errno;
    }

  fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_write_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_write_progress(MPIDI_PG_t *pg)
{
    int mpi_errno;
    int nb, i;
    MPIDI_VC_t *vc;
    int complete;
                                                                                                                                               
    for (i = 0; i < smpi.num_local_nodes; i++) {
        MPIDI_PG_Get_vc(pg, smpi.l2g_rank[i], &vc);
                                                                                                                                               
        while (vc->smp.send_active != NULL) {
            MPID_Request *req = vc->smp.send_active;
            
	        MPIU_Assert(req->ch.iov_offset < req->dev.iov_count);
            /*MPIDI_DBG_PRINTF((60, FCNAME, "calling rdma_put_datav")); */
            mpi_errno =
                MPIDI_CH3I_SMP_writev(vc,
                                      req->dev.iov +
                                      req->ch.iov_offset,
                                      req->dev.iov_count -
                                      req->ch.iov_offset, &nb);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER,
                                         "**write_progress", 0);
                return mpi_errno;
            }
            DEBUG_PRINT("shm_writev returned %d", nb);
                                                                                                                                               
            if (nb > 0) {
                if (MPIDI_CH3I_Request_adjust_iov(req, nb)) {
                    /* Write operation complete */
                    mpi_errno =
                        MPIDI_CH3U_Handle_send_req(vc, req, &complete);
                    if (mpi_errno != MPI_SUCCESS) {
                        mpi_errno =
                            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                                 FCNAME, __LINE__,
                                                 MPI_ERR_OTHER, "**fail",
                                                 0);
                        MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_WRITTEN);
                        return mpi_errno;
                    }
                    if (complete) {
                        MPIDI_CH3I_SMP_SendQ_dequeue(vc);
                        DEBUG_PRINT("Dequeue request from sendq %p, now head %p\n", 
                                     req, vc->smp.sendq_head);
                    }
                    vc->smp.send_active = MPIDI_CH3I_SMP_SendQ_head(vc);
                } else {
                    MPIDI_DBG_PRINTF((65, FCNAME,
                                      "iovec updated by %d bytes but not complete",
                                      nb));
                    MPIU_Assert(req->ch.iov_offset < req->dev.iov_count);
                    break;
                }
            } else {
                MPIDI_DBG_PRINTF((65, FCNAME,
                                  "shm_post_writev returned %d bytes",
                                  nb));
                break;
            }
        }
    }
    /*MPIDI_DBG_PRINTF((60, FCNAME, "exiting")); */
                                                                                                                                               
    MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_WRITTEN);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_read_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_read_progress(MPIDI_PG_t *pg)
{
    MPIDI_VC_t *vc;
    MPIDI_CH3_Pkt_t *pkt_head;
    int nb = 0;
    int complete;
    int i;
    int mpi_errno = MPI_SUCCESS;

    for (i = 0; i < smpi.num_local_nodes; i ++) {
        if (i == smpi.my_local_id) continue;
        MPIDI_PG_Get_vc(pg, smpi.l2g_rank[i], &vc);
        if (NULL == vc->smp.recv_active) {
            MPIDI_CH3I_SMP_pull_header(vc, &pkt_head);

            if (pkt_head) {
                mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head);
                if (mpi_errno != MPI_SUCCESS) {
                    return mpi_errno;
                }
            }
        }
        if (NULL != vc->smp.recv_active) {
            MPIDI_CH3I_SMP_readv(vc,
                                 &vc->smp.recv_active->dev.iov[vc->smp.
                                                               recv_active->
                                                               ch.
                                                               iov_offset],
                                 vc->smp.recv_active->dev.iov_count - 
                                        vc->smp.recv_active->ch.iov_offset, &nb);
            DEBUG_PRINT("request to fill: iovlen %d, iov[0].len %d, [1] %d, nb %d\n",
                    vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->ch.iov_offset, 
                    vc->smp.recv_active->dev.iov[vc->smp.recv_active->
                          ch.iov_offset].MPID_IOV_LEN, 
                    vc->smp.recv_active->dev.iov[vc->smp.recv_active->
                          ch.iov_offset + 1].MPID_IOV_LEN, nb);
            if (nb > 0) {
                if (MPIDI_CH3I_Request_adjust_iov(vc->smp.recv_active, nb)) {
                    DEBUG_PRINT("adjust iov finished, handle req\n");
                    mpi_errno =
                        MPIDI_CH3U_Handle_recv_req(vc, vc->smp.recv_active, &complete);
                    DEBUG_PRINT("finished handle req, complete %d\n", complete);
                    if (mpi_errno != MPI_SUCCESS) {
                        mpi_errno =
                            MPIR_Err_create_code(mpi_errno,
                                                 MPIR_ERR_RECOVERABLE,
                                                 FCNAME, __LINE__,
                                                 MPI_ERR_OTHER, "**fail",
                                                 0);
                        goto fn_exit;
                    }
                    if (complete) {
                        vc->smp.recv_active = NULL;
						/* assert: current_bytes[vc->smp.local_nodes] == 0 */
                    }
                }
            }
        } 
    }
  fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_init(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int my_rank;
    unsigned int i, j, size, pool, pid, wait;
    struct stat file_status;
    char *shmem_file = NULL;
    int pagesize = getpagesize();
    char *value;

    if ((value = getenv("SMP_EAGERSIZE")) != NULL) {
        smp_eagersize = atoi(value);
    }
    if ((value = getenv("SMPI_LENGTH_QUEUE")) != NULL) {
        smpi_length_queue = atoi(value);
    }
#ifdef _AFFINITY_
    if ((value = getenv("VIADEV_ENABLE_AFFINITY")) != NULL) {
            viadev_enable_affinity = atoi(value);
    }
#endif

    if (gethostname(hostname, sizeof(char) * HOSTNAME_LEN) < 0) {
        fprintf(stderr, "[%s:%d] Unable to get hostname\n", __FILE__, __LINE__);
        return -1;
    }

    DEBUG_PRINT("gethostname: %s\n", hostname);

    mpi_errno = smpi_exchange_info(pg);
    if (MPI_SUCCESS != mpi_errno) {
        return mpi_errno;
    }

    DEBUG_PRINT("finished exchange info\n");

    PMI_Get_rank(&my_rank);
    /* Convert to bytes */
    smp_eagersize = smp_eagersize * 1024 + 1;
    smpi_length_queue = smpi_length_queue * 1024 * 1024;

#ifdef DEBUG
    if (my_rank == 0)
        DEBUG_PRINT("smp eager size %d, smp queue length %d\n",
                    smp_eagersize, smpi_length_queue);
#endif
    if (smp_eagersize > smpi_length_queue / 2) {
        fprintf(stderr, "SMP_EAGERSIZE should not exceed half of "
                "SMPI_LENGTH_QUEUE. Note that SMP_EAGERSIZE is set in KBytes, "
                "and SMPI_LENGTH_QUEUE is set in MBytes.\n");
        return -1;
    }

    if (smpi.num_local_nodes > SMPI_MAX_NUMLOCALNODES) {
        fprintf(stderr,
                         "ERROR: mpi node %d, too many local processes "
                         "(%d processes, %d maximum). Change the "
                         "SMPI_MAX_NUMLOCALNODES value in "
                         "src/mpid/osu_ch3/channels/mrail/include/mpidi_ch3_impl.h\n",
                         my_rank, smpi.num_local_nodes,
                         SMPI_MAX_NUMLOCALNODES);
        return -1;
    }
    smpi.available_queue_length =
        (smpi_length_queue - smp_eagersize - sizeof(int));

    /* add pid for unique file name */
    shmem_file =
        (char *) malloc(sizeof(char) * (HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    smpi_malloc_assert(shmem_file, "smpi_init", "shared memory file name");

    /* unique shared file name */
    sprintf(shmem_file, "/tmp/ib_shmem-%s-%s-%d.tmp",
            pg->ch.kvs_name, hostname, getuid());

    DEBUG_PRINT("shemfile %s\n", shmem_file);

    /* open the shared memory file */
    smpi.fd =
        open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (smpi.fd < 0) {
        perror("open");
        fprintf(stderr, 
                         "[%d] smpi_init:error in opening "
                         "shared memory file <%s>: %d\n",
                         my_rank, shmem_file, errno);
        return -1;
    }

    /* compute the size of this file */
    size = (SMPI_CACHE_LINE_SIZE + sizeof(struct shared_mem) + pagesize +
            (smpi.num_local_nodes * (smpi.num_local_nodes - 1) *
             (SMPI_ALIGN(smpi_length_queue + pagesize))));

    DEBUG_PRINT("sizeof shm file %d\n", size);
    /* initialization of the shared memory file */
    /* just set size, don't really allocate memory, to allow intelligent memory
     * allocation on NUMA arch */
    if (smpi.my_local_id == 0) {
        if (ftruncate(smpi.fd, 0)) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] smpi_init:error in ftruncate to zero "
                             "shared memory file: %d\n", my_rank, errno);
            return -1;
        }

        /* set file size, without touching pages */
        if (ftruncate(smpi.fd, size)) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] smpi_init:error in ftruncate to size "
                             "shared memory file: %d\n", my_rank, errno);
            return -1;
        }
#ifndef _X86_64_
        {
            char *buf;
            buf = (char *) calloc(size + 1, sizeof(char));
            if (write(smpi.fd, buf, size) != size) {
                fprintf(stderr,  "[%d] smpi_init:error in writing "
                                 "shared memory file: %d\n",
                                 my_rank, errno);
                free(buf);
                return -1;
            }
            free(buf);
        }
#endif
        if (lseek(smpi.fd, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] smpi_init:error in lseek "
                             "on shared memory file: %d\n",
                             my_rank, errno);
            return -1;
        }
    }

    DEBUG_PRINT("process arrives before sync stage\n");
    /* synchronization between local processes */
    do {
        if (fstat(smpi.fd, &file_status) != 0) {
            /* to clean up tmp shared file */
            unlink(shmem_file);
            fprintf(stderr,  "[%d] smpi_init:error in fstat "
                             "on shared memory file: %d\n",
                             my_rank, errno);
            return -1;
        }
        usleep(1000);
    }
    while (file_status.st_size != size);

    DEBUG_PRINT("before mmap\n");
    /* mmap of the shared memory file */
    smpi.mmap_ptr = mmap(0, size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), smpi.fd,
                         0);
    if (smpi.mmap_ptr == (void *) -1) {
        /* to clean up tmp shared file */
        unlink(shmem_file);
        fprintf(stderr,  "[%d] smpi_init:error in mmapping "
                         "shared memory: %d\n", my_rank, errno);
        return -1;
    }
    smpi_shmem = (struct shared_mem *) smpi.mmap_ptr;
    if (((long) smpi_shmem & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
        /* to clean up tmp shared file */
        unlink(shmem_file);
        fprintf(stderr,  "[%d] smpi_init:error in shifting mmapped "
                         "shared memory\n", my_rank);
        return -1;
    }

    /* init rqueues in shared memory */
    if (0 == smpi.my_local_id) {
        pool = pagesize;
        for (i = 0; i < smpi.num_local_nodes; i++) {
            for (j = 0; j < smpi.num_local_nodes; j++) {
                if (i != j) {
                    READBAR();
                    smpi_shmem->rqueues_limits[i][j].first =
                        SMPI_ALIGN(pool);
                    smpi_shmem->rqueues_limits[i][j].last =
                        SMPI_ALIGN(pool + smpi.available_queue_length);
                    smpi_shmem->rqueues_params[i].params[j].current =
                        SMPI_ALIGN(pool);
                    smpi_shmem->rqueues_params[j].params[i].next =
                        SMPI_ALIGN(pool);
                    smpi_shmem->rqueues_params[j].params[i].msgs_total_in =
                        0;
                    smpi_shmem->rqueues_flow_out[i][j].msgs_total_out = 0;
                    READBAR();
                    pool += SMPI_ALIGN(smpi_length_queue + pagesize);
                }
            }
        }
    }

    /* another synchronization barrier */
    if (0 == smpi.my_local_id) {
        wait = 1;
        while (wait) {
            wait = 0;
            for (i = 1; i < smpi.num_local_nodes; i++) {
                if (smpi_shmem->pid[i] == 0) {
                    wait = 1;
                }
            }
        }
        /* id = 0, unlink the shared memory file, so that it is cleaned
         *       up when everyone exits */
        unlink(shmem_file);

        pid = getpid();
        if (0 == pid) {
            fprintf(stderr, "[%d] smpi_init:error in geting pid\n",
                             my_rank);
            return -1;
        }
        smpi_shmem->pid[smpi.my_local_id] = pid;
        WRITEBAR();
    } else {
        while (smpi_shmem->pid[0] != 0);
        while (smpi_shmem->pid[0] == 0) {
            smpi_shmem->pid[smpi.my_local_id] = getpid();
            WRITEBAR();
        }
        for (i = 0; i < smpi.num_local_nodes; i++) {
            if (smpi_shmem->pid[i] <= 0) {
                fprintf(stderr,  "[%d] smpi_init:error in getting pid\n",
                                 my_rank);
                return -1;
            }
        }
    }

    free(shmem_file);

#ifdef _X86_64_
    /*
     * Okay, here we touch every page in the shared memory region.
     * We do this to get the pages allocated so that they are local
     * to the receiver on a numa machine (instead of all being located
     * near the first process).
     */
    {
        int receiver, sender;

        for (receiver = 0; receiver < smpi.num_local_nodes; receiver++) {
            volatile char *ptr = &smpi_shmem->pool;
            volatile char tmp;

            sender = smpi.my_local_id;
            if (sender != receiver) {
                int k;

                for (k = SMPI_FIRST(sender, receiver);
                     k < SMPI_LAST(sender, receiver); k += pagesize) {
                    tmp = ptr[k];
                }
            }
        }
    }
#endif

    for (i = 0; i < SMPI_MAX_NUMLOCALNODES; i++) {
        current_ptr[i] = NULL;
        current_bytes[i] = 0;
        total_bytes[i] = 0;
    }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_finalize()
{
    /* unmap the shared memory file */
    munmap(smpi.mmap_ptr, (SMPI_CACHE_LINE_SIZE +
                           sizeof(struct shared_mem) +
                           (smpi.num_local_nodes *
                            (smpi.num_local_nodes -
                             1) * (smpi_length_queue +
                                   SMPI_CACHE_LINE_SIZE))));

    close(smpi.fd);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_writev(MPIDI_VC_t * vc, const MPID_IOV * iov,
                          const int n, int *num_bytes_ptr)
{
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    int i, offset = 0;

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (!pkt_avail) {
        *num_bytes_ptr = 0;
        goto fn_exit;
    }

    ptr_volatile = (void *) ((&smpi_shmem->pool)
                             + SMPI_NEXT(smpi.my_local_id,
                                         vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > smp_eagersize) ? smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

    if (pkt_avail < SMPI_SMALLEST_SIZE) {
        *num_bytes_ptr = 0;
        goto fn_exit;
    } 
	
    ptr = (void *) ((unsigned long) ptr + sizeof(int));

    i = 0;
    *num_bytes_ptr = 0;
    do {
        pkt_len = 0;
        for (; i < n;) {
            DEBUG_PRINT
                ("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
                 iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
                 pkt_avail);
            if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
                if (offset != 0) {
                    memcpy(ptr,
                           (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                                     offset),
                           iov[i].MPID_IOV_LEN - offset);
                    pkt_avail -= (iov[i].MPID_IOV_LEN - offset);
                    ptr =
                        (void *) ((unsigned long) ptr +
                                  iov[i].MPID_IOV_LEN - offset);
                    pkt_len += (iov[i].MPID_IOV_LEN - offset);
                    offset = 0;
                } else {
                    memcpy(ptr, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
                    pkt_avail -= iov[i].MPID_IOV_LEN;
                    ptr =
                        (void *) ((unsigned long) ptr +
                                  iov[i].MPID_IOV_LEN);
                    pkt_len += iov[i].MPID_IOV_LEN;
                }
                i++;
            } else if (pkt_avail > 0) {
                memcpy(ptr,
                       (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                                 offset), pkt_avail);
                ptr = (void *) ((unsigned long) ptr + pkt_avail);
                pkt_len += pkt_avail;
                offset += pkt_avail;
                pkt_avail = 0;
                break;
            }
        }

        DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
        *num_bytes_ptr += pkt_len;
        *((int *) ptr_head) = pkt_len;

        smpi_complete_send(smpi.my_local_id, vc->smp.local_nodes,
                           (pkt_len + sizeof(int)));

        if (i == n) {
            DEBUG_PRINT("counter value, in %d, out %d\n",
                        SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
            break;
        }

        pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);
        if (pkt_avail != 0) {
            pkt_avail =
                (pkt_avail > smp_eagersize) ? smp_eagersize : pkt_avail;
            pkt_avail -= sizeof(int);
            ptr_head = (void *) ((&smpi_shmem->pool)
                                 + SMPI_NEXT(smpi.my_local_id,
                                             vc->smp.local_nodes));
            ptr = (void *) ((unsigned long) ptr_head + sizeof(int));
        }
        DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
  fn_exit:
    DEBUG_PRINT("writev returns bytes %d\n", *num_bytes_ptr);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_readv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
                         const int iovlen, int
                         *num_bytes_ptr)
{
    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    /* all variable must be declared before the state declarations */

    *num_bytes_ptr = 0;

    DEBUG_PRINT
        ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
         current_bytes[recv_vc_ptr->smp.local_nodes],
         total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
         iov[0].MPID_IOV_LEN);
    if (current_ptr[recv_vc_ptr->smp.local_nodes] != NULL) {
        /** last smp packet has not been drained up yet **/
        DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
                    iov_off, current_bytes[recv_vc_ptr->smp.local_nodes],
                    iov[iov_off].MPID_IOV_LEN);
        for (;
             iov_off < iovlen
             && current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
            if (current_bytes[recv_vc_ptr->smp.local_nodes] >=
                iov[iov_off].MPID_IOV_LEN) {
                memcpy((void *) iov[iov_off].MPID_IOV_BUF,
                       (void *) current_ptr[recv_vc_ptr->smp.local_nodes],
                       iov[iov_off].MPID_IOV_LEN);
                current_ptr[recv_vc_ptr->smp.local_nodes] =
                    (void *) ((unsigned long)
                              current_ptr[recv_vc_ptr->smp.local_nodes] +
                              iov[iov_off].MPID_IOV_LEN);
                current_bytes[recv_vc_ptr->smp.local_nodes] -=
                    iov[iov_off].MPID_IOV_LEN;
                received_bytes += iov[iov_off].MPID_IOV_LEN;
                iov_off++;
            } else if (current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
                memcpy((void *) iov[iov_off].MPID_IOV_BUF,
                       (void *) current_ptr[recv_vc_ptr->smp.local_nodes],
                       current_bytes[recv_vc_ptr->smp.local_nodes]);
                current_ptr[recv_vc_ptr->smp.local_nodes] =
                    (void *) ((unsigned long)
                              current_ptr[recv_vc_ptr->smp.local_nodes] +
                              current_bytes[recv_vc_ptr->smp.local_nodes]);
                received_bytes +=
                    current_bytes[recv_vc_ptr->smp.local_nodes];
                buf_off = current_bytes[recv_vc_ptr->smp.local_nodes];
                current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
            }
        }
        *num_bytes_ptr += received_bytes;
        DEBUG_PRINT
            ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
             current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
             iov_off, iovlen);

        if (0 == current_bytes[recv_vc_ptr->smp.local_nodes]) {
            current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
            DEBUG_PRINT("total in %d, total out %d\n",
                        SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
                                     smpi.my_local_id),
                        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
                                      smpi.my_local_id));

            smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
                               smpi.my_local_id,
                               total_bytes[recv_vc_ptr->smp.local_nodes] +
                               sizeof(int));

            DEBUG_PRINT("total in %d, total out %d\n",
                        SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
                                     smpi.my_local_id),
                        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
                                      smpi.my_local_id));

            total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
        }
        received_bytes = 0;
        if (iov_off == iovlen) {
			/* assert: current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
            goto fn_exit;
        }
    }

    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, smpi.my_local_id) !=
           SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, smpi.my_local_id)) {
        /****** received the next smp packet **************/
        current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((&smpi_shmem->pool) +
                      SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
                                   smpi.my_local_id));
        total_bytes[recv_vc_ptr->smp.local_nodes] =
            *((int *) current_ptr[recv_vc_ptr->smp.local_nodes]);
        current_bytes[recv_vc_ptr->smp.local_nodes] =
            total_bytes[recv_vc_ptr->smp.local_nodes];
        current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((unsigned long)
                      current_ptr[recv_vc_ptr->smp.local_nodes] +
                      sizeof(int));

        /****** starting to fill the iov buffers *********/
        for (;
             iov_off < iovlen
             && current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
            if (current_bytes[recv_vc_ptr->smp.local_nodes] >=
                iov[iov_off].MPID_IOV_LEN - buf_off) {
                memcpy((void *) ((unsigned long) iov[iov_off].
                                 MPID_IOV_BUF + buf_off),
                       (void *) current_ptr[recv_vc_ptr->smp.local_nodes],
                       iov[iov_off].MPID_IOV_LEN - buf_off);
                current_bytes[recv_vc_ptr->smp.local_nodes] -=
                    (iov[iov_off].MPID_IOV_LEN - buf_off);
                current_ptr[recv_vc_ptr->smp.local_nodes] =
                    (void *) ((unsigned long)
                              current_ptr[recv_vc_ptr->smp.local_nodes] +
                              (iov[iov_off].MPID_IOV_LEN - buf_off));
                received_bytes += (iov[iov_off].MPID_IOV_LEN - buf_off);
                iov_off++;
                buf_off = 0;
            } else if (current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
                memcpy((void *) ((unsigned long) iov[iov_off].
                                 MPID_IOV_BUF + buf_off),
                       (void *) current_ptr[recv_vc_ptr->smp.local_nodes],
                       current_bytes[recv_vc_ptr->smp.local_nodes]);
                current_ptr[recv_vc_ptr->smp.local_nodes] =
                    (void *) ((unsigned long)
                              current_ptr[recv_vc_ptr->smp.local_nodes] +
                              current_bytes[recv_vc_ptr->smp.local_nodes]);
                received_bytes +=
                    current_bytes[recv_vc_ptr->smp.local_nodes];
                buf_off += current_bytes[recv_vc_ptr->smp.local_nodes];
                current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
            }
        }
        *num_bytes_ptr += received_bytes;
        if (0 == current_bytes[recv_vc_ptr->smp.local_nodes]) {
            current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
            smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
                               smpi.my_local_id,
                               total_bytes[recv_vc_ptr->smp.local_nodes] +
                               sizeof(int));
            total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
        }
        received_bytes = 0;
        if (iov_off == iovlen) {
            goto fn_exit;
        }
    }
  fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_pull_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_pull_header(MPIDI_VC_t * vc,
                               MPIDI_CH3_Pkt_t ** pkt_head)
{
    READBAR();

    /*assert(vc->smp.local_nodes != smpi.my_local_id);*/
    if (current_bytes[vc->smp.local_nodes] != 0) {
        fprintf(stderr, "current bytes %d, total bytes %d, remote id %d\n",
                current_bytes[vc->smp.local_nodes],
                total_bytes[vc->smp.local_nodes], vc->smp.local_nodes);
        assert(current_bytes[vc->smp.local_nodes] == 0);
    }

    READBAR();
    if (total_bytes[vc->smp.local_nodes] != 0) {
        current_ptr[vc->smp.local_nodes] = NULL;
        smpi_complete_recv(vc->smp.local_nodes,
                           smpi.my_local_id,
                           total_bytes[vc->smp.local_nodes] + sizeof(int));
        total_bytes[vc->smp.local_nodes] = 0;
        current_bytes[vc->smp.local_nodes] = 0;
    }

    READBAR();  
    if (SMPI_TOTALIN(vc->smp.local_nodes, smpi.my_local_id) !=
        SMPI_TOTALOUT(vc->smp.local_nodes, smpi.my_local_id)) {
        DEBUG_PRINT("remote %d, local %d, total in %d, total out %d\n",
                    vc->smp.local_nodes, smpi.my_local_id,
                    SMPI_TOTALIN(vc->smp.local_nodes, smpi.my_local_id),
                    SMPI_TOTALOUT(vc->smp.local_nodes, smpi.my_local_id));

        current_ptr[vc->smp.local_nodes] = (void *) ((&smpi_shmem->pool) +
                                                     SMPI_CURRENT(vc->smp.
                                                                  local_nodes,
                                                                  smpi.
                                                                  my_local_id));
        READBAR();
        total_bytes[vc->smp.local_nodes] =
            *((int *) current_ptr[vc->smp.local_nodes]);
        *pkt_head =
            (void *) ((unsigned long) current_ptr[vc->smp.local_nodes] +
                      sizeof(int));
        DEBUG_PRINT("bytes arrived %d, head type %d, headersize %d\n",
                    total_bytes[vc->smp.local_nodes],
                    ((MPIDI_CH3_Pkt_t *) (*pkt_head))->type,
                    MPIDI_CH3U_PKT_SIZE(*pkt_head));
        current_bytes[vc->smp.local_nodes] =
            total_bytes[vc->smp.local_nodes] -
            MPIDI_CH3U_PKT_SIZE(*pkt_head);
        current_ptr[vc->smp.local_nodes] =
            (void *) ((unsigned long) current_ptr[vc->smp.local_nodes] +
                      sizeof(int) + MPIDI_CH3U_PKT_SIZE(*pkt_head));
        DEBUG_PRINT("current bytes %d\n",
                    current_bytes[vc->smp.local_nodes]);
    } else {
        *pkt_head = NULL;
    }
    return MPI_SUCCESS;
}


static int smpi_exchange_info(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int pg_rank, pg_size;
    int hostid;

    int *hostnames_j = NULL;
    int i, j;

    /** variables needed for PMI exchange **/
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];


    MPIDI_VC_t *vc;

#ifdef _AFFINITY_
    long N_CPUs_online;
    unsigned long affinity_mask=1;
    unsigned long affinity_mask_len=sizeof(affinity_mask);

    /*Get number of CPU on machine*/
    if ( (N_CPUs_online=sysconf (_SC_NPROCESSORS_ONLN)) < 1 ) {
            perror("sysconf");
    }
#endif /*_AFFINITY_*/

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);

    hostid = get_host_id(hostname, HOSTNAME_LEN);

    hostnames_j = (int *) malloc(pg_size * sizeof(int));

    if (hostnames_j == NULL) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem",
                                 "**nomem %s", "host names");
        return mpi_errno;
    }

    /** exchange address hostid using PMI interface **/
    if (pg_size > 1) {
        char *key;
        char *val;

        mpi_errno = PMI_KVS_Get_key_length_max(&key_max_sz);
        assert(mpi_errno == PMI_SUCCESS);
        mpi_errno = PMI_KVS_Get_value_length_max(&val_max_sz);
        assert(mpi_errno == PMI_SUCCESS);

        key_max_sz++;
        val_max_sz++;

        key = MPIU_Malloc(key_max_sz);
        val = MPIU_Malloc(val_max_sz);

        if (key == NULL || val == NULL) {
            mpi_errno =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**nomem",
                                     "**nomem %s", "pmi key");
            return mpi_errno;
        }

        for (i = 0; i < pg_size; i++) {
            if (i == pg_rank)
                continue;
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            sprintf(rdmavalue, "%08d", hostid);

            DEBUG_PRINT("put hostid %p\n", hostid);

            MPIU_Strncpy(key, rdmakey, key_max_sz);
            MPIU_Strncpy(val, rdmavalue, val_max_sz);

            mpi_errno = PMI_KVS_Put(pg->ch.kvs_name, key, val);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_put",
                                         "**pmi_kvs_put %d", mpi_errno);
                return mpi_errno;
            }

            mpi_errno = PMI_KVS_Commit(pg->ch.kvs_name);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_commit",
                                         "**pmi_kvs_commit %d", mpi_errno);
                return mpi_errno;
            }
        }

        mpi_errno = PMI_Barrier();
        if (mpi_errno != 0) {
            mpi_errno =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     mpi_errno);
            return mpi_errno;
        }

        /* Here, all the key and value pairs are put, now we can get them */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i) {
                hostnames_j[i] = hostid;
                continue;
            }
            /* generate the key */
            sprintf(rdmakey, "%08d-%08d", i, pg_rank);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            mpi_errno =
                PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (mpi_errno != 0) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", mpi_errno);
                return mpi_errno;
            }
            MPIU_Strncpy(rdmavalue, val, val_max_sz);
            hostnames_j[i] = atoi(rdmavalue);
            DEBUG_PRINT("get dest rank %d, hostname %p \n", i,
                        hostnames_j[i]);
        }

        mpi_errno = PMI_Barrier();
        if (mpi_errno != 0) {
            mpi_errno =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     mpi_errno);
            return mpi_errno;
        }


    }
    /** end of exchange address **/

    SET_ORIGINAL_MALLOC_HOOKS;

    if (1 == pg_size)
        hostnames_j[0] = hostid;
    /* smpi.local_nodes = (unsigned int *) malloc(pg_size * sizeof(int)); */

    if (hostnames_j == NULL) {
        fprintf(stderr, "malloc: in ib_rank_lid_table for SMP");
        return -1;
    }

    SAVE_MALLOC_HOOKS;
    SET_MVAPICH_MALLOC_HOOKS;

    smpi.only_one_device = 1;
    SMP_ONLY = 1;
    smpi.num_local_nodes = 0;
    for (j = 0; j < pg_size; j++) {
        MPIDI_PG_Get_vc(pg, j, &vc);
        if (hostnames_j[pg_rank] == hostnames_j[j]) {
            if (j == pg_rank) {
                smpi.my_local_id = smpi.num_local_nodes;
#ifdef _AFFINITY_
                if ( viadev_enable_affinity > 0 ){
                        affinity_mask=1<<(smpi.my_local_id%N_CPUs_online);
                        if ( sched_setaffinity(0,affinity_mask_len,&affinity_mask)<0 ) {
                                perror("sched_setaffinity");
                        }
                }
#endif /* _AFFINITY_ */
            }
            vc->smp.local_nodes = smpi.num_local_nodes;
            smpi.l2g_rank[smpi.num_local_nodes] = j;
            smpi.num_local_nodes++;
            SMP_INIT = 1;
        } else {
            smpi.only_one_device = 0;
            SMP_ONLY = 0;
            vc->smp.local_nodes = -1;
        }
    }

    DEBUG_PRINT("num local nodes %d, my local id %d\n",
                smpi.num_local_nodes, smpi.my_local_id);
    free(hostnames_j);
    return MPI_SUCCESS;
}

#endif
