/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
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
	MPIU_Error_printf("[%d][%s:%d] ", rank, __FILE__, __LINE__);\
	MPIU_Error_printf(args);                                    \
    } while (0)
#else
#define DEBUG_PRINT(args...)
#endif

/* Macros for flow control and rqueues management */
#define SMPI_TOTALIN(sender,receiver)                               \
    ((volatile smpi_params *)smpi_shmem->rqueues_params[sender])[receiver].msgs_total_in

#define SMPI_TOTALOUT(sender,receiver)                              \
    ((volatile smpi_rqueues *)smpi_shmem->rqueues_flow_out[receiver])[sender].msgs_total_out

#define SMPI_CURRENT(sender,receiver)                               \
    ((volatile smpi_params *)smpi_shmem->rqueues_params[receiver])[sender].current

#define SMPI_NEXT(sender,receiver)                                  \
    ((volatile smpi_params *)smpi_shmem->rqueues_params[sender])[receiver].next

#define SMPI_FIRST(sender,receiver)                                 \
    ((volatile smpi_rq_limit *)smpi_shmem->rqueues_limits[receiver])[sender].first

#define SMPI_LAST(sender,receiver)                                  \
    ((volatile smpi_rq_limit *)smpi_shmem->rqueues_limits[receiver])[sender].last

struct smpi_var smpi;
struct shared_mem *smpi_shmem;
struct shared_buffer_pool sh_buf_pool;
SEND_BUF_T **buffer_head;
SEND_BUF_T *my_buffer_head;
static char hostname[HOSTNAME_LEN];
int SMP_INIT = 0;
int SMP_ONLY = 0;
static void **current_ptr;
static int *current_bytes;
static int *total_bytes;

int size_shmem;
int size_pool;

int      smp_eagersize      = SMP_EAGERSIZE;
int      smpi_length_queue  = SMPI_LENGTH_QUEUE;
int      smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
int      smp_batch_size = SMP_BATCH_SIZE;

extern int enable_shmem_collectives;

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
static inline SEND_BUF_T *get_buf_from_pool (void);
static inline void send_buf_reclaim (void);
static inline void put_buf_to_pool (int, int);
static inline void link_buf_to_send_queue (int, int);


/*
 * This function currently exits(-1) on error.  The semantics need to be
 * changed to gracefully set mpi_errno and exit the same as other errors.
 *
 * TODO trigger an mpi error in the calling function
 */
/*********** Declaration for locally used buffer ***********/
static inline void smpi_malloc_assert(void *ptr, char *fct, char *msg)
{
    int rank;

    PMI_Get_rank(&rank);
    if (NULL == ptr) {
	MPIU_Error_printf("Cannot Allocate Memory: [%d] in function %s, context: %s\n",
		rank, fct, msg);
	exit(-1);
    }
}

/*
 * TODO add error handling
 */
static inline int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    host_id = (unsigned long)(((struct in_addr *)
		    hostent->h_addr_list[0])->s_addr);

    return host_id;
}

static inline int smpi_get_avail_length(int dest)
{
    int avail;
    WRITEBAR();
    if (SMPI_TOTALIN(smpi.my_local_id, dest) >=
	    SMPI_TOTALOUT(smpi.my_local_id, dest)) {
	WRITEBAR();
	avail = (smpi.available_queue_length -
		(SMPI_TOTALIN(smpi.my_local_id, dest) -
		 SMPI_TOTALOUT(smpi.my_local_id, dest)));
    } else {
	WRITEBAR();
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
	MPIDI_CH3_Pkt_t * pkt, int *index)
{
    int mpi_errno = MPI_SUCCESS;

    if (MPIDI_CH3_PKT_RNDV_R3_DATA == pkt->type) {
	MPID_Request *rreq;
	MPIDI_CH3_Pkt_send_t *pkt_header;

	pkt_header = (MPIDI_CH3_Pkt_send_t *)pkt;
	(*index) = pkt_header->mrail.smp_index;

	if(*index == -1) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "*index == -1");
	}

	vc->smp.recv_current_pkt_type = SMP_RNDV_MSG;
	MPID_Request_get_ptr(((MPIDI_CH3_Pkt_rndv_r3_data_t *) (pkt))->
		receiver_req_id, rreq);
	DEBUG_PRINT("R3 data received, don't need to proceed\n");
	vc->smp.recv_active = rreq;
	goto fn_exit;
    }
    mpi_errno = MPIDI_CH3U_Handle_recv_pkt(vc, pkt, &vc->smp.recv_active);
    vc->smp.recv_current_pkt_type = SMP_EAGER_MSG;
    if (mpi_errno) {
	MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_READ);
	MPIU_ERR_POP(mpi_errno);
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_write_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_write_progress(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int nb, i;
    MPIDI_VC_t *vc;
    int complete;

    for (i = 0; i < smpi.num_local_nodes; i++) {
	MPIDI_PG_Get_vc(pg, smpi.l2g_rank[i], &vc);

	vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
	while (vc->smp.send_active != NULL) {
	    MPID_Request *req = vc->smp.send_active;

	    if(req->ch.iov_offset >= req->dev.iov_count) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s",
			"req->ch.iov_offset >= req->dev.iov_count");
	    }

	    /*MPIDI_DBG_PRINTF((60, FCNAME, "calling rdma_put_datav")); */
	    if(req->ch.reqtype == REQUEST_RNDV_R3_HEADER){
		vc->smp.send_current_pkt_type = SMP_RNDV_MSG;
		mpi_errno =
		    MPIDI_CH3I_SMP_writev_rndv_header(vc,
			    req->dev.iov +
			    req->ch.iov_offset,
			    req->dev.iov_count -
			    req->ch.iov_offset, &nb);
	    } else if (req->ch.reqtype == REQUEST_RNDV_R3_DATA){
		if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG) {
		    mpi_errno = 
			MPIDI_CH3I_SMP_writev_rndv_data(vc,
				req->dev.iov +
				req->ch.iov_offset,
				req->dev.iov_count -
				req->ch.iov_offset, &nb);   
		} else {
		    mpi_errno =
			MPIDI_CH3I_SMP_writev_rndv_data_cont(vc,
				req->dev.iov +
				req->ch.iov_offset,
				req->dev.iov_count -
				req->ch.iov_offset, &nb);
		}
	    } else {
		mpi_errno =
		    MPIDI_CH3I_SMP_writev(vc,
			    req->dev.iov +
			    req->ch.iov_offset,
			    req->dev.iov_count -
			    req->ch.iov_offset, &nb);
	    }

	    if (mpi_errno) {
		MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER,
			"**progress");
	    }
	    DEBUG_PRINT("shm_writev returned %d", nb);

	    if (nb > 0) {
		if (MPIDI_CH3I_Request_adjust_iov(req, nb)) {
		    /* Write operation complete */
		    mpi_errno =
			MPIDI_CH3U_Handle_send_req(vc, req, &complete);
		    if (mpi_errno) {
			MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_WRITTEN);
			MPIU_ERR_POP(mpi_errno);
		    }
		    if (complete) {
                        req->ch.reqtype = REQUEST_NORMAL;
			MPIDI_CH3I_SMP_SendQ_dequeue(vc);
			DEBUG_PRINT("Dequeue request from sendq %p, now head %p\n", 
				req, vc->smp.sendq_head);
		    } else {
			if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
			    vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
		    }
		    vc->smp.send_active = MPIDI_CH3I_SMP_SendQ_head(vc);
		} else {
		    if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
			vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;

		    MPIDI_DBG_PRINTF((65, FCNAME,
				"iovec updated by %d bytes but not complete",
				nb));

		    if(req->ch.iov_offset >= req->dev.iov_count) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**fail", "**fail %s",
				"req->ch.iov_offset >= req->dev.iov_count");
		    }

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

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
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
    int index = -1;

    for (i = 0; i < smpi.num_local_nodes; i ++) {
	if (i == smpi.my_local_id) continue;
	MPIDI_PG_Get_vc(pg, smpi.l2g_rank[i], &vc);
	if (NULL == vc->smp.recv_active) {
	    MPIDI_CH3I_SMP_pull_header(vc, &pkt_head);

	    if (pkt_head) {
		mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head, &index);
		if (mpi_errno) {
		    MPIU_ERR_POP(mpi_errno);
		}
	    }
	}
	if (NULL != vc->smp.recv_active) {
	    if (vc->smp.recv_current_pkt_type == SMP_RNDV_MSG) {
		mpi_errno = MPIDI_CH3I_SMP_readv_rndv(vc,
			&vc->smp.recv_active->dev.iov[vc->smp.
			recv_active->
			ch.
			iov_offset],
			vc->smp.recv_active->dev.iov_count -
			vc->smp.recv_active->ch.iov_offset, index, &nb);
	    } else if (vc->smp.recv_current_pkt_type == SMP_RNDV_MSG_CONT) {
		mpi_errno = MPIDI_CH3I_SMP_readv_rndv_cont(vc,
			&vc->smp.recv_active->dev.iov[vc->smp.
			recv_active->
			ch.
			iov_offset],
			vc->smp.recv_active->dev.iov_count -
			vc->smp.recv_active->ch.iov_offset, index, &nb);
	    } else {
		mpi_errno = MPIDI_CH3I_SMP_readv(vc,
			&vc->smp.recv_active->dev.iov[vc->smp.
			recv_active->
			ch.
			iov_offset],
			vc->smp.recv_active->dev.iov_count - 
			vc->smp.recv_active->ch.iov_offset, &nb);
	    }

	    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

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

		    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

		    if (complete) {
			vc->smp.recv_active = NULL;
			/* assert: current_bytes[vc->smp.local_nodes] == 0 */
		    } else {
			if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG) 
			    vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;
		    }
		} else {
		    if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG)
			vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;
		}
	    }
	} 
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_init(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int use_smp, my_rank;
    unsigned int i, j, pool, pid, wait;
    int local_num, sh_size, pid_len, rq_len, param_len, limit_len;
    struct stat file_status;
    struct stat file_status_pool;
    char *shmem_file = NULL;
    char *pool_file = NULL;
    int pagesize = getpagesize();
    char *value;
    struct shared_mem *shmem;
    int blocking_val;
    SEND_BUF_T *send_buf;
#ifdef _X86_64_
    volatile char tmpchar;
#endif

    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        blocking_val = !!atoi(value);
        if(blocking_val) {
            /* blocking is enabled, so
             * automatically disable
             * shared memory */
            smp_eagersize = rdma_iba_eager_threshold;  
            return MPI_SUCCESS;
        }
    }

    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {

        use_smp= atoi(value);

        if (!use_smp) { 
            smp_eagersize = rdma_iba_eager_threshold;  
            return MPI_SUCCESS;
        }
    }

    if ((value = getenv("SMP_EAGERSIZE")) != NULL) {
        smp_eagersize = atoi(value);
    }
    if ((value = getenv("SMPI_LENGTH_QUEUE")) != NULL) {
        smpi_length_queue = atoi(value);
    }
    if ((value = getenv("SMP_NUM_SEND_BUFFER")) != NULL ) {
        smp_num_send_buffer = atoi(value);
    }
    if ((value = getenv("SMP_BATCH_SIZE")) != NULL ) {
        smp_batch_size = atoi(value);
    }
#ifdef _AFFINITY_

#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    /* by default turn off affinity if THREAD_MULTIPLE
       is requested
    */
    MPIU_THREAD_CHECK_BEGIN
        viadev_enable_affinity = 0;
    MPIU_THREAD_CHECK_END
#endif

    if ((value = getenv("MV2_ENABLE_AFFINITY")) != NULL) {
        viadev_enable_affinity = atoi(value);
    }
#endif

    if (gethostname(hostname, sizeof(char) * HOSTNAME_LEN) < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "gethostname", strerror(errno));
    }

    DEBUG_PRINT("gethostname: %s\n", hostname);

    mpi_errno = smpi_exchange_info(pg);
    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

    DEBUG_PRINT("finished exchange info\n");

    PMI_Get_rank(&my_rank);
    /* Convert to bytes */
    smp_eagersize = smp_eagersize * 1024 + 1;
    smpi_length_queue = smpi_length_queue * 1024;

#ifdef DEBUG
    if (my_rank == 0)
	DEBUG_PRINT("smp eager size %d, smp queue length %d\n",
		smp_eagersize, smpi_length_queue);
#endif
    if (smp_eagersize > smpi_length_queue / 2) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "SMP_EAGERSIZE should not exceed half of "
		"SMPI_LENGTH_QUEUE. Note that SMP_EAGERSIZE "
		"and SMPI_LENGTH_QUEUE are set in KBytes.");
    }

    smpi.available_queue_length =
	(smpi_length_queue - smp_eagersize - sizeof(int));

    /* add pid for unique file name */
    shmem_file =
	(char *) malloc(sizeof(char) * (HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    if(!shmem_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "shmem_file");
    }

    pool_file =
	(char *) malloc (sizeof (char) * (HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    if(!pool_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "pool_file");
    }

    /* unique shared file name */
    sprintf(shmem_file, "/tmp/ib_shmem-%s-%s-%d.tmp",
	    pg->ch.kvs_name, hostname, getuid());

    DEBUG_PRINT("shemfile %s\n", shmem_file);

    sprintf (pool_file, "/tmp/ib_pool-%s-%s-%d.tmp", pg->ch.kvs_name,
	    hostname, getuid ());
    DEBUG_PRINT("shemfile %s\n", pool_file);

    /* open the shared memory file */
    smpi.fd =
	open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (smpi.fd < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: ", "open", strerror(errno));
    }

    smpi.fd_pool =
	open (pool_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (smpi.fd_pool < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: ", "open", strerror(errno));
    }

    /* compute the size of this file */
    local_num = smpi.num_local_nodes * smpi.num_local_nodes;
    pid_len = smpi.num_local_nodes * sizeof(int);
    param_len = sizeof(smpi_params) * local_num;
    rq_len = sizeof(smpi_rqueues) * local_num;
    limit_len = sizeof(smpi_rq_limit) * local_num;
    sh_size = sizeof(struct shared_mem) + pid_len + param_len + rq_len +
	limit_len + SMPI_CACHE_LINE_SIZE * 4;

    size_shmem = (SMPI_CACHE_LINE_SIZE + sh_size + pagesize +
	    (smpi.num_local_nodes * (smpi.num_local_nodes - 1) *
	     (SMPI_ALIGN(smpi_length_queue + pagesize))));

    DEBUG_PRINT("sizeof shm file %d\n", size_shmem);

    size_pool =
	SMPI_ALIGN (sizeof (SEND_BUF_T) * smp_num_send_buffer +
		pagesize) * smpi.num_local_nodes + SMPI_CACHE_LINE_SIZE;

    DEBUG_PRINT("sizeof pool file %d\n", size_pool);

    /* initialization of the shared memory file */
    /* just set size, don't really allocate memory, to allow intelligent memory
     * allocation on NUMA arch */
    if (smpi.my_local_id == 0) {
	if (ftruncate(smpi.fd, 0)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	/* set file size, without touching pages */
	if (ftruncate(smpi.fd, size_shmem)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	if (ftruncate (smpi.fd_pool, 0)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	if (ftruncate (smpi.fd_pool, size_pool)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

#ifndef _X86_64_
	{
	    char *buf;
	    buf = (char *) calloc(size_shmem + 1, sizeof(char));
	    if (write(smpi.fd, buf, size_shmem) != size_shmem) {
		int write_errno = errno;

		free(buf);
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "write", strerror(write_errno));
	    }
	    free(buf);
	}

	{
	    char *buf;
	    buf = (char *) calloc (size_pool + 1, sizeof (char));
	    if (write (smpi.fd_pool, buf, size_pool) != size_pool) {
		int write_errno = errno;

		free(buf);
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "write", strerror(write_errno));
	    }
	    free (buf);
	}

#endif
	if (lseek(smpi.fd, 0, SEEK_SET) != 0) {
	    int lseek_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
	}

	if (lseek (smpi.fd_pool, 0, SEEK_SET) != 0) {
	    int lseek_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
	}

    }

    if (enable_shmem_collectives){
	/* Shared memory for collectives */
	mpi_errno = MPIDI_CH3I_SHMEM_COLL_init(pg);
	if(mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

    DEBUG_PRINT("process arrives before sync stage\n");
    /* synchronization between local processes */
    do {
	if (fstat(smpi.fd, &file_status) != 0 ||
		fstat (smpi.fd_pool, &file_status_pool) != 0) {
	    int fstat_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "fstat", strerror(fstat_errno));
	}
	usleep(1);
    }
    while (file_status.st_size != size_shmem ||
	    file_status_pool.st_size != size_pool);

    smpi_shmem = (struct shared_mem *)malloc(sizeof(struct shared_mem));

    if(!smpi_shmem) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "smpi_shmem");
    }

    DEBUG_PRINT("before mmap\n");
    /* mmap of the shared memory file */
    smpi.mmap_ptr = mmap(0, size_shmem,
	    (PROT_READ | PROT_WRITE), (MAP_SHARED), smpi.fd,
	    0);
    if (smpi.mmap_ptr == (void *) -1) {
	int mmap_errno = errno;

	/* to clean up tmp shared file */
	unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "mmap", strerror(errno));
    }

    smpi.send_buf_pool_ptr = mmap (0, size_pool, (PROT_READ | PROT_WRITE),
	    (MAP_SHARED), smpi.fd_pool, 0);

    if (smpi.send_buf_pool_ptr == (void *) -1) {
	int mmap_errno = errno;

	unlink (pool_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "mmap", strerror(errno));
    }

    shmem = (struct shared_mem *) smpi.mmap_ptr;
    if (((long) shmem & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
	/* to clean up tmp shared file */
	unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "error in shifting mmapped shared memory");
    }

    buffer_head = (SEND_BUF_T **) malloc(sizeof(SEND_BUF_T *) * smpi.num_local_nodes);
    for(i=0; i<smpi.num_local_nodes; i++){
	buffer_head[i] = (SEND_BUF_T *)((unsigned long)smpi.send_buf_pool_ptr +
		SMPI_ALIGN(sizeof(SEND_BUF_T) * smp_num_send_buffer + 
		    pagesize) * i);

	if (((long) buffer_head[i] & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s",
		    "error in shifting mmapped shared pool memory");
	}
    }
    my_buffer_head = buffer_head[smpi.my_local_id];

    sh_buf_pool.free_head = 0;

    sh_buf_pool.send_queue = (int *)malloc(sizeof(int) * smpi.num_local_nodes);

    if(!sh_buf_pool.send_queue) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "sh_buf_pool.send_queue");
    }

    sh_buf_pool.tail = (int *)malloc(sizeof(int) * smpi.num_local_nodes);

    if(!sh_buf_pool.tail) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "sh_buf_pool.tail");
    }

    for (i = 0; i < smpi.num_local_nodes; i++) {
	sh_buf_pool.send_queue[i] = sh_buf_pool.tail[i] = -1;
    }

#ifdef _X86_64_
    usleep(smpi.my_local_id);

    for (i = 0; i < smp_num_send_buffer; i++) {
	send_buf =&(my_buffer_head[i]);
	send_buf->myindex = i;
	send_buf->next = i+1;
	send_buf->busy = 0;
	send_buf->len = 0;
	send_buf->has_next = 0;
	send_buf->msg_complete = 0;

	for (j = 0; j < SMP_SEND_BUF_SIZE; j += pagesize) {
	    tmpchar = (send_buf->buf)[j];
	}
    }
    send_buf->next = -1;

#else

    if (0 == smpi.my_local_id) {
	for(j = 0; j < smpi.num_local_nodes; j++){
	    for (i = 0; i < smp_num_send_buffer; i++) {
		send_buf = &buffer_head[j][i];
		send_buf->myindex = i;
		send_buf->next = i+1;
		send_buf->busy = 0;
		send_buf->len = 0;
		send_buf->has_next = 0;
		send_buf->msg_complete = 0;
	    }
	    send_buf->next = -1;
	}
    }
#endif

    /* Initialize shared_mem pointers */
    smpi_shmem->pid = (int *) shmem;

    smpi_shmem->rqueues_params =
	(smpi_params **) malloc(sizeof(smpi_params *)*smpi.num_local_nodes);
    smpi_shmem->rqueues_flow_out =
	(smpi_rqueues **) malloc(sizeof(smpi_rqueues *)*smpi.num_local_nodes);
    smpi_shmem->rqueues_limits =
	(smpi_rq_limit **)malloc(sizeof(smpi_rq_limit *)*smpi.num_local_nodes);

    if (smpi_shmem->rqueues_params == NULL ||
	    smpi_shmem->rqueues_flow_out == NULL ||
	    smpi_shmem->rqueues_limits == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "smpi_shmem rqueues");
    }

    smpi_shmem->rqueues_params[0] =
	(smpi_params *)((char *)shmem + SMPI_ALIGN(pid_len) + SMPI_CACHE_LINE_SIZE);
    smpi_shmem->rqueues_flow_out[0] =
	(smpi_rqueues *)((char *)smpi_shmem->rqueues_params[0] + 
		SMPI_ALIGN(param_len) + SMPI_CACHE_LINE_SIZE);
    smpi_shmem->rqueues_limits[0] =
	(smpi_rq_limit *)((char *)smpi_shmem->rqueues_flow_out[0] + 
		SMPI_ALIGN(rq_len) + SMPI_CACHE_LINE_SIZE);
    smpi_shmem->pool =
	(char *)((char *)smpi_shmem->rqueues_limits[0] + SMPI_ALIGN(limit_len) +
		SMPI_CACHE_LINE_SIZE);

    for (i = 1; i < smpi.num_local_nodes; i++) {
	smpi_shmem->rqueues_params[i] = (smpi_params *)
	    (smpi_shmem->rqueues_params[i-1] + smpi.num_local_nodes);
	smpi_shmem->rqueues_flow_out[i] = (smpi_rqueues *)
	    (smpi_shmem->rqueues_flow_out[i-1] + smpi.num_local_nodes);
	smpi_shmem->rqueues_limits[i] = (smpi_rq_limit *)
	    (smpi_shmem->rqueues_limits[i-1] + smpi.num_local_nodes);
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
		    smpi_shmem->rqueues_params[i][j].current =
			SMPI_ALIGN(pool);
		    smpi_shmem->rqueues_params[j][i].next =
			SMPI_ALIGN(pool);
		    smpi_shmem->rqueues_params[j][i].msgs_total_in = 0;
		    smpi_shmem->rqueues_flow_out[i][j].msgs_total_out = 0;
		    READBAR();
		    pool += SMPI_ALIGN(smpi_length_queue + pagesize);
		}
	    }
	}
    }

    if (enable_shmem_collectives){
	/* Memory Mapping shared files for collectives*/
	mpi_errno = MPIDI_CH3I_SHMEM_COLL_Mmap();
	if(mpi_errno) MPIU_ERR_POP(mpi_errno);
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
	unlink(pool_file);

	if (enable_shmem_collectives){
	    /* Unlinking shared files for collectives*/  
	    MPIDI_CH3I_SHMEM_COLL_Unlink();
	}

	pid = getpid();
	if (0 == pid) {
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "getpid", strerror(errno));
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
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "getpid", strerror(errno));
	    }
	}
    }

    free(shmem_file);
    free(pool_file);

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
	    volatile char *ptr = smpi_shmem->pool;
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

    current_ptr = (void **)malloc(sizeof(void *) * smpi.num_local_nodes);

    if(!current_ptr) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "current_ptr");
    }

    current_bytes = (int *)malloc(sizeof(int) * smpi.num_local_nodes);

    if(!current_bytes) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "current_bytes");
    }

    total_bytes = (int *)malloc(sizeof(int) * smpi.num_local_nodes);

    if(!total_bytes) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "total_bytes");
    }

    for (i = 0; i < smpi.num_local_nodes; i++) {
	current_ptr[i] = NULL;
	current_bytes[i] = 0;
	total_bytes[i] = 0;
    }

    SMP_INIT = 1;

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_finalize()
{
    /* unmap the shared memory file */
    munmap(smpi.mmap_ptr, size_shmem); 
    close(smpi.fd);

    munmap(smpi.send_buf_pool_ptr, size_pool); 
    close(smpi.fd_pool);

    if(buffer_head) {
	free(buffer_head);
    }

    if (smpi.l2g_rank) { 
	free(smpi.l2g_rank);
    } 

    if(smpi_shmem) {
	free(smpi_shmem);
    }

    if(current_ptr) {
	free(current_ptr);
    }

    if(current_bytes) {
	free(current_bytes);
    }

    if(total_bytes) {
	free(total_bytes);
    } 

    if(sh_buf_pool.send_queue) {
	free(sh_buf_pool.send_queue);
    }

    if(sh_buf_pool.tail) {
	free(sh_buf_pool.tail);
    }

    if (enable_shmem_collectives){
	/* Freeing up shared memory collective resources*/
	MPIDI_CH3I_SHMEM_COLL_finalize();
    }

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_writev_rndv_header(MPIDI_VC_t * vc, const MPID_IOV * iov,
	const int n, int *num_bytes_ptr)
{
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    int i, offset = 0;
    MPIDI_CH3_Pkt_send_t *pkt_header;

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (!pkt_avail) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    ptr_volatile = (void *) ((smpi_shmem->pool)
	    + SMPI_NEXT(smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > smp_eagersize) ? smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

    if (pkt_avail < SMPI_SMALLEST_SIZE) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    } 

    send_buf_reclaim();

    if (sh_buf_pool.free_head == -1) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    pkt_header = (MPIDI_CH3_Pkt_send_t *)(iov[0].MPID_IOV_BUF);
    pkt_header->mrail.smp_index = sh_buf_pool.free_head;

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
		pkt_len + sizeof(int));
	if (i == n) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
fn_exit:
    DEBUG_PRINT("writev_rndv_header returns bytes %d\n", *num_bytes_ptr);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_data_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_writev_rndv_data_cont(MPIDI_VC_t * vc, const MPID_IOV * iov,
	const int n, int *num_bytes_ptr)
{
    volatile void *ptr_volatile;
    void *ptr_head;
    int pkt_avail;
    int pkt_len = 0;
    int i, offset = 0;
    int destination = vc->smp.local_nodes;
    int first_index;
    SEND_BUF_T *send_buf = NULL;
    SEND_BUF_T *tmp_buf = NULL;
    int has_sent = 0;

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (pkt_avail < 2*sizeof(int)) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    pkt_avail = SMP_SEND_BUF_SIZE;

    send_buf_reclaim();

    if (sh_buf_pool.free_head == -1) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    first_index = sh_buf_pool.free_head;

    i = 0;
    *num_bytes_ptr = 0;
    do {
	pkt_len = 0;
	for (; i < n;) {
	    DEBUG_PRINT
		("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
		 iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
		 pkt_avail);

	    if(has_sent >= smp_batch_size)
		break;
	    has_sent++;

	    send_buf = get_buf_from_pool();
	    if(send_buf == NULL)
		break;

	    if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
		if (offset != 0) {
		    memcpy(send_buf->buf,
			    (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
				offset),
			    iov[i].MPID_IOV_LEN - offset);
		    send_buf->busy = 1;
		    send_buf->len = iov[i].MPID_IOV_LEN - offset;
		    send_buf->has_next = 1;

		    link_buf_to_send_queue (destination, send_buf->myindex);
		    tmp_buf = send_buf;

		    pkt_len += (iov[i].MPID_IOV_LEN - offset);
		    offset = 0;
		} else {
		    memcpy(send_buf->buf, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
		    send_buf->busy = 1;
		    send_buf->len = iov[i].MPID_IOV_LEN;
		    send_buf->has_next = 1;

		    link_buf_to_send_queue (destination, send_buf->myindex);
		    tmp_buf = send_buf;

		    pkt_len += iov[i].MPID_IOV_LEN;
		}
		i++;
	    } else if (pkt_avail > 0) {
		memcpy(send_buf->buf,
			(void *) ((unsigned long) iov[i].MPID_IOV_BUF +
			    offset), 
			pkt_avail); 
		send_buf->busy = 1;
		send_buf->len = pkt_avail;
		send_buf->has_next = 1;

		link_buf_to_send_queue (destination, send_buf->myindex);
		tmp_buf = send_buf;

		pkt_len += pkt_avail;
		offset += pkt_avail;
	    }
	}

	DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
	*num_bytes_ptr += pkt_len;

	if (i == n || has_sent >= smp_batch_size) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	send_buf_reclaim();

    } while (sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
	tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((smpi_shmem->pool)
	    + SMPI_NEXT(smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    ptr_head =(void *) ((unsigned long)ptr_head + sizeof(int));
    (*((int *) ptr_head)) = first_index;

    smpi_complete_send(smpi.my_local_id, vc->smp.local_nodes,
	    2*sizeof(int));

fn_exit:
    DEBUG_PRINT("writev_rndv_data_cont returns bytes %d\n", *num_bytes_ptr);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_data
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_writev_rndv_data(MPIDI_VC_t * vc, const MPID_IOV * iov,
	const int n, int *num_bytes_ptr)
{
    volatile void *ptr_volatile;
    void *ptr_head;
    int pkt_avail;
    int pkt_len = 0;
    int i, offset = 0;
    int destination = vc->smp.local_nodes;
    SEND_BUF_T *send_buf = NULL;
    SEND_BUF_T *tmp_buf = NULL;
    int has_sent=0;
    int mpi_errno = MPI_SUCCESS;

    pkt_avail = SMP_SEND_BUF_SIZE;

    if(sh_buf_pool.free_head == -1) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "sh_buf_pool.free_head == -1");
    }

    i = 0;
    *num_bytes_ptr = 0;
    do {
	pkt_len = 0;
	for (; i < n;) {
	    DEBUG_PRINT
		("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
		 iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
		 pkt_avail);
	    if(has_sent >= smp_batch_size)
		break;
	    has_sent++;

	    send_buf = get_buf_from_pool();
	    if(send_buf == NULL)
		break;

	    if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
		if (offset != 0) {
		    memcpy(send_buf->buf,
			    (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
				offset),
			    iov[i].MPID_IOV_LEN - offset);
		    send_buf->busy = 1;
		    send_buf->len = iov[i].MPID_IOV_LEN - offset;
		    send_buf->has_next = 1;

		    link_buf_to_send_queue (destination, send_buf->myindex);
		    tmp_buf = send_buf;

		    pkt_len += (iov[i].MPID_IOV_LEN - offset);
		    offset = 0;
		} else {
		    memcpy(send_buf->buf, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
		    send_buf->busy = 1;
		    send_buf->len = iov[i].MPID_IOV_LEN;
		    send_buf->has_next = 1;

		    link_buf_to_send_queue (destination, send_buf->myindex);
		    tmp_buf = send_buf;

		    pkt_len += iov[i].MPID_IOV_LEN;
		}
		i++;
	    } else if (pkt_avail > 0) {
		memcpy(send_buf->buf,
			(void *) ((unsigned long) iov[i].MPID_IOV_BUF +
			    offset), 
			pkt_avail); 
		send_buf->busy = 1;
		send_buf->len = pkt_avail;
		send_buf->has_next = 1;

		link_buf_to_send_queue (destination, send_buf->myindex);
		tmp_buf = send_buf;

		pkt_len += pkt_avail;
		offset += pkt_avail;
	    }
	}

	DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
	*num_bytes_ptr += pkt_len;

	if (i == n || has_sent >= smp_batch_size) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	send_buf_reclaim();

    } while (sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
	tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((smpi_shmem->pool)
	    + SMPI_NEXT(smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    smpi_complete_send(smpi.my_local_id, vc->smp.local_nodes,
	    sizeof(int));

fn_exit:
    DEBUG_PRINT("writev_rndv_data returns bytes %d\n", *num_bytes_ptr);
    return mpi_errno;

fn_fail:
    goto fn_exit;
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

    ptr_volatile = (void *) ((smpi_shmem->pool)
	    + SMPI_NEXT(smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > smp_eagersize) ? smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

    if (pkt_avail < iov[0].MPID_IOV_LEN) {
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
	    ptr_head = (void *) ((smpi_shmem->pool)
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
#define FUNCNAME MPIDI_CH3I_SMP_readv_rndv_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_readv_rndv_cont(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
	const int iovlen, int index, int *num_bytes_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    int destination = recv_vc_ptr->smp.local_nodes;
    int current_index = index;
    int recv_offset = 0;
    int msglen, iov_len;
    void *current_buf;
    SEND_BUF_T *recv_buf;
    /* all variable must be declared before the state declarations */

    *num_bytes_ptr = 0;

    if (current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
	if(total_bytes[recv_vc_ptr->smp.local_nodes] != 0) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "total_bytes[recv_vc_ptr->smp.local_nodes] "
		    "!= 0");
	}

	if (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, smpi.my_local_id) ==
		SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, smpi.my_local_id)) {
	    goto fn_exit;
	} 

	READBAR();
	current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *) ((smpi_shmem->pool) +
		    SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id));
	WRITEBAR();
	total_bytes[recv_vc_ptr->smp.local_nodes] =
	    *((int *) current_ptr[recv_vc_ptr->smp.local_nodes]);
	current_bytes[recv_vc_ptr->smp.local_nodes] =
	    total_bytes[recv_vc_ptr->smp.local_nodes];

	DEBUG_PRINT
	    ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
	     current_bytes[recv_vc_ptr->smp.local_nodes],
	     total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
	     iov[0].MPID_IOV_LEN);
	WRITEBAR();

	current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *)((unsigned long) current_ptr[recv_vc_ptr->smp.local_nodes] +
		    sizeof(int)); 
	current_index = *((int *) current_ptr[recv_vc_ptr->smp.local_nodes]);
	smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
		smpi.my_local_id, 2*sizeof(int));
    } else {
	total_bytes[recv_vc_ptr->smp.local_nodes] =
	    current_bytes[recv_vc_ptr->smp.local_nodes]; 
	current_index = recv_vc_ptr->smp.read_index;
	recv_offset = recv_vc_ptr->smp.read_off;
    }

    if (current_index != -1) {
	/** last smp packet has not been drained up yet **/
	DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
		iov_off, current_bytes[recv_vc_ptr->smp.local_nodes],
		iov[iov_off].MPID_IOV_LEN);

	recv_buf = &buffer_head[destination][current_index];

	if(recv_buf->busy != 1) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "recv_buf->busy != 1");
	}

	msglen = recv_buf->len - recv_offset;
	current_buf = (void *)((unsigned long) recv_buf->buf + recv_offset);
	iov_len = iov[0].MPID_IOV_LEN;

	for (;
		iov_off < iovlen
		&& current_bytes[recv_vc_ptr->smp.local_nodes] > 0
		&& current_index != -1;) {

	    if (msglen > iov_len) {
		READBAR(); 
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len); 
		READBAR();
		current_buf = (void *) ((unsigned long) current_buf +
			iov_len);
		msglen -= iov_len;
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		iov_off++;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = current_index;
		    recv_vc_ptr->smp.read_off = (unsigned long) current_buf -
			(unsigned long) recv_buf->buf;
		    break;
		}

		if (current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}

		else if (current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s",
			    "current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;
	    } else if (msglen == iov_len) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len);
		READBAR();
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		iov_off++;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = recv_buf->next;
		    recv_vc_ptr->smp.read_off = 0;
		    recv_buf->busy = 0;
		    break;
		}
		if (current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
		    assert(current_bytes[recv_vc_ptr->smp.local_nodes] == 0);
		    recv_buf->busy = 0;
		    break;
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;

		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &buffer_head[destination][current_index];
		assert(recv_buf->busy == 1);
		msglen = recv_buf->len;
		current_buf = (void *)recv_buf->buf;

	    } else if (msglen > 0) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, msglen);
		READBAR();
		iov_len -= msglen;
		received_bytes += msglen;
		buf_off += msglen;
		current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

		if (current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}
		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &buffer_head[destination][current_index];
		assert(recv_buf->busy == 1);
		msglen = recv_buf->len;
		current_buf = (void *)recv_buf->buf;
	    }
	}
	*num_bytes_ptr += received_bytes;
	DEBUG_PRINT
	    ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
	     current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
	     iov_off, iovlen);

	if (0 == current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id));

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

fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv_rndv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_readv_rndv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
	const int iovlen, int index, int *num_bytes_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    int destination = recv_vc_ptr->smp.local_nodes;
    int current_index = index;
    int msglen, iov_len;
    void *current_buf;
    SEND_BUF_T *recv_buf;
    /* all variable must be declared before the state declarations */

    *num_bytes_ptr = 0;

    if (0 == current_bytes[recv_vc_ptr->smp.local_nodes]) {
	READBAR();
	current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	DEBUG_PRINT("total in %d, total out %d\n",
		SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
		    smpi.my_local_id),
		SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes));

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

    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, smpi.my_local_id) ==
	    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, smpi.my_local_id)); 

    READBAR();
    current_ptr[recv_vc_ptr->smp.local_nodes] =
	(void *) ((smpi_shmem->pool) +
		SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
		    smpi.my_local_id));
    WRITEBAR();
    total_bytes[recv_vc_ptr->smp.local_nodes] =
	*((int *) current_ptr[recv_vc_ptr->smp.local_nodes]);
    current_bytes[recv_vc_ptr->smp.local_nodes] =
	total_bytes[recv_vc_ptr->smp.local_nodes];
    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
	    smpi.my_local_id,sizeof(int));
    DEBUG_PRINT
	("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
	 current_bytes[recv_vc_ptr->smp.local_nodes],
	 total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
	 iov[0].MPID_IOV_LEN);

    WRITEBAR();

    if (current_index != -1) {
	/** last smp packet has not been drained up yet **/
	DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
		iov_off, current_bytes[recv_vc_ptr->smp.local_nodes],
		iov[iov_off].MPID_IOV_LEN);

	recv_buf = &buffer_head[destination][current_index];

	if(recv_buf->busy != 1) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "recv_buf->busy == 1");
	}

	msglen = recv_buf->len;
	current_buf = (void *)recv_buf->buf;
	iov_len = iov[0].MPID_IOV_LEN;

	for (;
		iov_off < iovlen
		&& current_bytes[recv_vc_ptr->smp.local_nodes] > 0
		&& current_index != -1;) {

	    if (msglen > iov_len) {
		READBAR(); 
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len); 
		READBAR();
		current_buf = (void *) ((unsigned long) current_buf +
			iov_len);
		msglen -= iov_len;
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		iov_off++;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = current_index; 
		    recv_vc_ptr->smp.read_off = (unsigned long) current_buf - 
			(unsigned long) recv_buf->buf;
		    break;
		}
		if (current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
		    assert(current_bytes[recv_vc_ptr->smp.local_nodes] == 0); 
		    recv_buf->busy = 0;
		    break;
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;
	    } else if (msglen == iov_len) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len);
		READBAR();
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		iov_off++;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = recv_buf->next;
		    recv_vc_ptr->smp.read_off = 0;
		    recv_buf->busy = 0;
		    break;
		}

		if (current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}

		else if(current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s",
			    "current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;

		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &buffer_head[destination][current_index];

		if(recv_buf->busy != 1) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s", "recv_buf->busy != 1");
		}

		msglen = recv_buf->len;
		current_buf = (void *)recv_buf->buf;

	    } else if (msglen > 0) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, msglen);
		READBAR();
		iov_len -= msglen;
		received_bytes += msglen;
		buf_off += msglen;
		current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

		if (current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}
		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &buffer_head[destination][current_index];

		if(recv_buf->busy != 1) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s", "recv_buf->busy != 1");
		}

		msglen = recv_buf->len;
		current_buf = (void *)recv_buf->buf;
	    }
	}
	*num_bytes_ptr += received_bytes;
	DEBUG_PRINT
	    ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
	     current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
	     iov_off, iovlen);

	if (0 == current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id));

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

fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    return mpi_errno;

fn_fail:
    goto fn_exit;
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
    WRITEBAR();
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
		READBAR(); 
		memcpy((void *) iov[iov_off].MPID_IOV_BUF,
			(void *) current_ptr[recv_vc_ptr->smp.local_nodes],
			iov[iov_off].MPID_IOV_LEN);
		READBAR();
		current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    current_ptr[recv_vc_ptr->smp.local_nodes] +
			    iov[iov_off].MPID_IOV_LEN);
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov[iov_off].MPID_IOV_LEN;
		received_bytes += iov[iov_off].MPID_IOV_LEN;
		iov_off++;
	    } else if (current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
		READBAR();
		memcpy((void *) iov[iov_off].MPID_IOV_BUF,
			(void *) current_ptr[recv_vc_ptr->smp.local_nodes],
			current_bytes[recv_vc_ptr->smp.local_nodes]);
		READBAR();
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
	    READBAR();
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

    WRITEBAR();
    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, smpi.my_local_id) !=
	    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, smpi.my_local_id)) {
	/****** received the next smp packet **************/
	READBAR();
	current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *) ((smpi_shmem->pool) +
		    SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
			smpi.my_local_id));
	WRITEBAR();
	total_bytes[recv_vc_ptr->smp.local_nodes] =
	    *((int *) current_ptr[recv_vc_ptr->smp.local_nodes]);
	current_bytes[recv_vc_ptr->smp.local_nodes] =
	    total_bytes[recv_vc_ptr->smp.local_nodes];
	READBAR();
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
		WRITEBAR();
		memcpy((void *) ((unsigned long) iov[iov_off].
			    MPID_IOV_BUF + buf_off),
			(void *) current_ptr[recv_vc_ptr->smp.local_nodes],
			iov[iov_off].MPID_IOV_LEN - buf_off);
		current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    (iov[iov_off].MPID_IOV_LEN - buf_off);
		READBAR();
		current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    current_ptr[recv_vc_ptr->smp.local_nodes] +
			    (iov[iov_off].MPID_IOV_LEN - buf_off));
		received_bytes += (iov[iov_off].MPID_IOV_LEN - buf_off);
		iov_off++;
		buf_off = 0;
	    } else if (current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
		WRITEBAR();
		memcpy((void *) ((unsigned long) iov[iov_off].
			    MPID_IOV_BUF + buf_off),
			(void *) current_ptr[recv_vc_ptr->smp.local_nodes],
			current_bytes[recv_vc_ptr->smp.local_nodes]);
		READBAR();
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
	    READBAR();
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
	WRITEBAR();
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
    /*assert(vc->smp.local_nodes != smpi.my_local_id);*/
    if (current_bytes[vc->smp.local_nodes] != 0) {
	MPIU_Error_printf("current bytes %d, total bytes %d, remote id %d\n",
		current_bytes[vc->smp.local_nodes],
		total_bytes[vc->smp.local_nodes], vc->smp.local_nodes);
	assert(current_bytes[vc->smp.local_nodes] == 0);
    }

    if (total_bytes[vc->smp.local_nodes] != 0) {
	READBAR();
	current_ptr[vc->smp.local_nodes] = NULL;
	smpi_complete_recv(vc->smp.local_nodes,
		smpi.my_local_id,
		total_bytes[vc->smp.local_nodes] + sizeof(int));
	total_bytes[vc->smp.local_nodes] = 0;
	current_bytes[vc->smp.local_nodes] = 0;
    }

    WRITEBAR();
    if (SMPI_TOTALIN(vc->smp.local_nodes, smpi.my_local_id) !=
	    SMPI_TOTALOUT(vc->smp.local_nodes, smpi.my_local_id)) {
	DEBUG_PRINT("remote %d, local %d, total in %d, total out %d\n",
		vc->smp.local_nodes, smpi.my_local_id,
		SMPI_TOTALIN(vc->smp.local_nodes, smpi.my_local_id),
		SMPI_TOTALOUT(vc->smp.local_nodes, smpi.my_local_id));

	READBAR();
	current_ptr[vc->smp.local_nodes] = (void *) ((smpi_shmem->pool) +
		SMPI_CURRENT(vc->smp.
		    local_nodes,
		    smpi.
		    my_local_id));
	WRITEBAR();
	total_bytes[vc->smp.local_nodes] =
	    *((int *) current_ptr[vc->smp.local_nodes]);
	WRITEBAR();
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
	READBAR();
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
    int *smpi_ptr = NULL;
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
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"sysconf", strerror(errno));
    }
#endif /*_AFFINITY_*/

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);

    hostid = get_host_id(hostname, HOSTNAME_LEN);

    hostnames_j = (int *) malloc(pg_size * sizeof(int));

    if (hostnames_j == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
		"**nomem %s", "host names");
    }

    /** exchange address hostid using PMI interface **/
    if (pg_size > 1) {
	int need_exchange = 0;

	for(i = 0; i < pg_size; i++) {
	    MPIDI_PG_Get_vc(pg, i, &vc);
	    if(i == pg_rank) {
		hostnames_j[i] = hostid;
	    } else {
		if (vc->smp.hostid == -1) {
		    need_exchange = 1;
		    break;
		}
		hostnames_j[i] = vc->smp.hostid;
	    }
	}

	if (need_exchange) {
	    char *key;
	    char *val;

	    if(PMI_KVS_Get_key_length_max(&key_max_sz) != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max key length");
	    }

	    if(PMI_KVS_Get_value_length_max(&val_max_sz) != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max value length");
	    }

	    key_max_sz++;
	    val_max_sz++;

	    key = MPIU_Malloc(key_max_sz);
	    val = MPIU_Malloc(val_max_sz);

	    if (key == NULL || val == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "pmi key");
	    }

	    for (i = 0; i < pg_size; i++) {
		if (i == pg_rank)
		    continue;
		sprintf(rdmakey, "%08d-%08d", pg_rank, i);
		sprintf(rdmavalue, "%08d", hostid);

		DEBUG_PRINT("put hostid %p\n", hostid);

		MPIU_Strncpy(key, rdmakey, key_max_sz);
		MPIU_Strncpy(val, rdmavalue, val_max_sz);

		if(PMI_KVS_Put(pg->ch.kvs_name, key, val) != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_put", "**pmi_kvs_put %d", mpi_errno);
		}

		if(PMI_KVS_Commit(pg->ch.kvs_name) != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_commit", "**pmi_kvs_commit %d",
			    mpi_errno);
		}
	    }

	    if(PMI_Barrier() != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_kvs_barrier", "**pmi_kvs_barrier %d", mpi_errno);
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

		if(PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz) !=
			PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_get", "**pmi_kvs_get %d", mpi_errno);
		}

		MPIU_Strncpy(rdmavalue, val, val_max_sz);
		hostnames_j[i] = atoi(rdmavalue);
		DEBUG_PRINT("get dest rank %d, hostname %p \n", i,
			hostnames_j[i]);
	    }

	    if(PMI_Barrier() != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", mpi_errno);
	    }
	}
    }
    /** end of exchange address **/

    if (1 == pg_size) hostnames_j[0] = hostid;
    /* smpi.local_nodes = (unsigned int *) malloc(pg_size * sizeof(int)); */

    smpi_ptr = (int *) malloc(pg_size * sizeof(int));

    if(smpi_ptr == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
		"**nomem %s", "smpi_ptr");
    }

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
			MPIU_Error_printf("sched_setaffinity: %s\n",
				strerror(errno));
		    }
		}
#endif /* _AFFINITY_ */
	    }
	    vc->smp.local_nodes = smpi.num_local_nodes;
	    smpi_ptr[smpi.num_local_nodes] = j;
	    smpi.num_local_nodes++;
	    /* SMP_INIT = 1; */
	} else {
	    smpi.only_one_device = 0;
	    SMP_ONLY = 0;
	    vc->smp.local_nodes = -1;
	}
    }

    DEBUG_PRINT("num local nodes %d, my local id %d\n",
	    smpi.num_local_nodes, smpi.my_local_id);

    smpi.l2g_rank = (unsigned int *)malloc(smpi.num_local_nodes * sizeof(int));
    if(smpi.l2g_rank == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "smpi.12g_rank");
	MPIU_Error_printf(
		"malloc: in ib_rank_lid_table for SMP");
    }

    for (j = 0; j < smpi.num_local_nodes; j++) {
	smpi.l2g_rank[j] = smpi_ptr[j];
    }
    free(smpi_ptr);

    free(hostnames_j);

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/*----------------------------------------------------------*/
    static inline SEND_BUF_T *
get_buf_from_pool ()
{
    SEND_BUF_T *ptr;

    if (sh_buf_pool.free_head == -1) 
	return NULL;

    ptr = &my_buffer_head[sh_buf_pool.free_head];
    sh_buf_pool.free_head = ptr->next;
    ptr->next = -1;

    assert (ptr->busy == 0);

    return ptr;
}

    static inline void
send_buf_reclaim ()
{
    int i, index, last_index;
    SEND_BUF_T *ptr;

    for (i = 0; i < smpi.num_local_nodes; i++) {
	if (i != smpi.my_local_id) {
	    index = sh_buf_pool.send_queue[i];
	    last_index = -1;
	    ptr = NULL;
	    while (index != -1) {
		ptr = &my_buffer_head[index];
		if(ptr->busy == 1)
		    break;
		last_index = index;
		index = ptr->next;
	    }
	    if (last_index != -1)
		put_buf_to_pool (sh_buf_pool.send_queue[i], last_index);
	    sh_buf_pool.send_queue[i] = index;
	    if (sh_buf_pool.send_queue[i] == -1)
		sh_buf_pool.tail[i] = -1;
	}
    }
}

    static inline void
put_buf_to_pool (int head, int tail)
{
    SEND_BUF_T *ptr;

    assert (head != -1);
    assert (tail != -1);

    ptr = &my_buffer_head[tail];

    ptr->next = sh_buf_pool.free_head;
    sh_buf_pool.free_head = head;
}

    static inline void
link_buf_to_send_queue (int dest, int index)
{
    if (sh_buf_pool.send_queue[dest] == -1) {
	sh_buf_pool.send_queue[dest] = sh_buf_pool.tail[dest] = index;
	return;
    }

    my_buffer_head[(sh_buf_pool.tail[dest])].next = index;
    sh_buf_pool.tail[dest] = index;
}

#endif

/* vi:set sw=4 tw=80: */
