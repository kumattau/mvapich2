/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
 */

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
#include "mpidi_ch3_impl.h"
#include <mpimem.h>
#include <limits.h>
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
#include <assert.h>
#include "pmi.h"
#include "smp_smpi.h"
#include "mpiutil.h"

#if defined(MAC_OSX)
#include <netinet/in.h>
#endif /* defined(MAC_OSX) */

/* CPU Mapping related definitions */
#if defined(USE_PROCESSOR_AFFINITY)
#include "plpa.h"
#define CONFIG_FILE "/proc/cpuinfo"
#define MAX_LINE_LENGTH 512
#define MAX_NAME_LENGTH 64

extern multi_core_arch_type_t arch_type;

typedef enum{
    CPU_FAMILY_NONE=0,
    CPU_FAMILY_INTEL,
    CPU_FAMILY_AMD,
} cpu_type_t;

int CLOVERTOWN_MODEL=15;
int HARPERTOWN_MODEL=23;
int NEHALEM_MODEL=26;

#if defined(_SMP_HWLOC_)
#include <hwloc.h>
int ip            = 0;
int *core_mapping = NULL;
int *obj_tree     = NULL;
#endif
int INTEL_XEON_DUAL_MAPPING[]      = {0,1,0,1};
int INTEL_CLOVERTOWN_MAPPING[]     = {0,0,1,1,0,0,1,1};                  /*        ((0,1),(4,5))((2,3),(6,7))             */
int INTEL_HARPERTOWN_LEG_MAPPING[] = {0,1,0,1,0,1,0,1};                  /* legacy ((0,2),(4,6))((1,3),(5,7))             */
int INTEL_HARPERTOWN_COM_MAPPING[] = {0,0,0,0,1,1,1,1};                  /* common ((0,1),(2,3))((4,5),(6,7))             */
int INTEL_NEHALEM_LEG_MAPPING[]    = {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1};  /* legacy (0,2,4,6)(1,3,5,7) with hyperthreading */
int INTEL_NEHALEM_COM_MAPPING[]    = {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1};  /* common (0,1,2,3)(4,5,6,7) with hyperthreading */
int AMD_OPTERON_DUAL_MAPPING[]     = {0,0,1,1};
int AMD_BARCELONA_MAPPING[]        = {0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3};

extern int num_cpus;
extern int use_optimal_cpu_binding;
extern int use_hwloc_cpu_binding;
unsigned int viadev_enable_affinity = 1;
#endif /* defined(USE_PROCESSOR_AFFINITY) */

#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
    do {                                                          \
	int rank;                                                 \
	PMI_Get_rank(&rank);                                      \
	MPIU_Error_printf("[%d][%s:%d] ", rank, __FILE__, __LINE__);\
	MPIU_Error_printf(args);                                    \
    } while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */

/* Macros for flow control and rqueues management */
#define SMPI_TOTALIN(sender,receiver)                               \
    ((volatile smpi_params *)g_smpi_shmem->rqueues_params[sender])[receiver].msgs_total_in

#define SMPI_TOTALOUT(sender,receiver)                              \
    ((volatile smpi_rqueues *)g_smpi_shmem->rqueues_flow_out[receiver])[sender].msgs_total_out

#define SMPI_CURRENT(sender,receiver)                               \
    ((volatile smpi_params *)g_smpi_shmem->rqueues_params[receiver])[sender].current

#define SMPI_NEXT(sender,receiver)                                  \
    ((volatile smpi_params *)g_smpi_shmem->rqueues_params[sender])[receiver].next

#define SMPI_FIRST(sender,receiver)                                 \
    ((volatile smpi_rq_limit *)g_smpi_shmem->rqueues_limits[receiver])[sender].first

#define SMPI_LAST(sender,receiver)                                  \
    ((volatile smpi_rq_limit *)g_smpi_shmem->rqueues_limits[receiver])[sender].last

struct smpi_var g_smpi;
struct shared_mem *g_smpi_shmem;
static struct shared_buffer_pool s_sh_buf_pool;
static SEND_BUF_T** s_buffer_head = NULL;
static SEND_BUF_T* s_my_buffer_head = NULL;
static char s_hostname[HOSTNAME_LEN];
int SMP_INIT = 0;
int SMP_ONLY = 0;
static void** s_current_ptr = NULL;
static int* s_current_bytes = NULL;
static int* s_total_bytes = NULL;

int g_size_shmem = 0;
int g_size_pool = 0; 

int g_smp_eagersize;
static int s_smpi_length_queue;
static int s_smp_num_send_buffer;
static int s_smp_batch_size;
static char* s_cpu_mapping = NULL;
static char* custom_cpu_mapping = NULL;
static int s_cpu_mapping_line_max = _POSIX2_LINE_MAX;
static int custom_cpu_mapping_line_max = _POSIX2_LINE_MAX;
char *cpu_mapping = NULL;

#if defined(_SMP_LIMIC_)
int limic_fd;
int g_smp_use_limic2 = 1;
static inline void adjust_lu_info(struct limic_user *lu, int old_len);
void MPIDI_CH3I_SMP_send_limic_comp(struct limic_header *l_header,
                                    MPIDI_VC_t* vc, int nb);
extern MPID_Request * create_request(void * hdr, MPIDI_msg_sz_t hdr_sz,
                                            MPIU_Size_t nb);
#endif

extern int enable_shmem_collectives;

#if defined(MAC_OSX) || defined(_PPC64_) 
#if defined(__GNUC__)
/* can't use -ansi for vxworks ccppc or this will fail with a syntax error
 * */
#define STBAR()  asm volatile ("sync": : :"memory")     /* ": : :" for C++ */
#define READBAR() asm volatile ("sync": : :"memory")
#define WRITEBAR() asm volatile ("sync": : :"memory")
#else /* defined(__GNUC__) */
#if  defined(__IBMC__) || defined(__IBMCPP__)
extern void __iospace_eieio(void);
extern void __iospace_sync(void);
#define STBAR()   __iospace_sync ()
#define READBAR() __iospace_sync ()
#define WRITEBAR() __iospace_eieio ()
#else /* defined(__IBMC__) || defined(__IBMCPP__) */
#error Do not know how to make a store barrier for this system
#endif /* defined(__IBMC__) || defined(__IBMCPP__) */
#endif /* defined(__GNUC__) */

#if !defined(WRITEBAR)
#define WRITEBAR() STBAR()
#endif /* !defined(WRITEBAR) */
#if !defined(READBAR)
#define READBAR() STBAR()
#endif /* !defined(READBAR) */

#else /* defined(MAC_OSX) || defined(_PPC64_) */
#define WRITEBAR()
#define READBAR()
#endif /* defined(MAC_OSX) || defined(_PPC64_) */

static void smpi_setaffinity();
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
    if (SMPI_TOTALIN(g_smpi.my_local_id, dest) >=
	    SMPI_TOTALOUT(g_smpi.my_local_id, dest)) {
	WRITEBAR();
	avail = (g_smpi.available_queue_length -
		(SMPI_TOTALIN(g_smpi.my_local_id, dest) -
		 SMPI_TOTALOUT(g_smpi.my_local_id, dest)));
    } else {
	WRITEBAR();
	avail = g_smpi.available_queue_length -
	    (SMPI_MAX_INT - SMPI_TOTALOUT(g_smpi.my_local_id, dest) +
	     SMPI_TOTALIN(g_smpi.my_local_id, dest));
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
#if defined(_SMP_LIMIC_)
static inline int MPIDI_CH3I_SMP_Process_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t* pkt, int* index, 
                  struct limic_header *l_header, int *use_limic)
#else
static inline int MPIDI_CH3I_SMP_Process_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t* pkt, int* index)
#endif
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
    int mpi_errno = MPI_SUCCESS;

#if defined(_SMP_LIMIC_)
    if (MPIDI_CH3_PKT_LIMIC_COMP == pkt->type)
    {
        /* convert to MPIDI_CH3_Pkt_limic_comp_t */
        MPIDI_CH3_Pkt_limic_comp_t *lc_pkt = (MPIDI_CH3_Pkt_limic_comp_t *)pkt;
        MPID_Request *sreq = (MPID_Request *)(lc_pkt->mrail.send_req_id);
        
        int nb = lc_pkt->nb;
        int complete = 0;

        if (MPIDI_CH3I_Request_adjust_iov(sreq, nb)) {
            MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
        }
        MPIU_Assert(complete);
   
        return mpi_errno;
    }
#endif

    if (MPIDI_CH3_PKT_RNDV_R3_DATA == pkt->type)
    {
        MPIDI_CH3_Pkt_send_t* pkt_header = (MPIDI_CH3_Pkt_send_t*) pkt;

#if defined(_SMP_LIMIC_)
        /* This is transferred through limic2, retrieve related info */
        if (pkt_header->mrail.send_req_id) {
            *use_limic = 1;
            memcpy(&(l_header->lu), s_current_ptr[vc->smp.local_nodes], sizeof(limic_user));
         
            s_current_ptr[vc->smp.local_nodes] = (void*)(
                (unsigned long) s_current_ptr[vc->smp.local_nodes]
                + sizeof(limic_user));

            l_header->total_bytes = *((int *)s_current_ptr[vc->smp.local_nodes]);
            l_header->send_req_id = (MPID_Request *)(pkt_header->mrail.send_req_id);

            s_current_ptr[vc->smp.local_nodes] = (void*)(
                (unsigned long) s_current_ptr[vc->smp.local_nodes]
                + sizeof(int));

            s_current_bytes[vc->smp.local_nodes] = s_current_bytes[vc->smp.local_nodes] -
                sizeof(struct limic_user) - sizeof(int);
        } else {
#endif
            if ((*index = pkt_header->mrail.src.smp_index) == -1)
            {
                MPIU_ERR_SETFATALANDJUMP1(
                    mpi_errno,
                    MPI_ERR_OTHER,
                    "**fail",
                    "**fail %s",
                    "*index == -1"
                );
            }
#if defined(_SMP_LIMIC_)
        }
#endif
	vc->smp.recv_current_pkt_type = SMP_RNDV_MSG;

	MPID_Request* rreq = NULL;
        MPID_Request_get_ptr(((MPIDI_CH3_Pkt_rndv_r3_data_t*) pkt)->receiver_req_id, rreq);
	DEBUG_PRINT("R3 data received, don't need to proceed\n");
	vc->smp.recv_active = rreq;
	goto fn_exit;
    }

#if defined(CKPT)
    /*
     * Handle the MPIDI_CH3_PKT_CM_SUSPEND packet
     * for the shared memory channel
     */
    else if (pkt->type == MPIDI_CH3_PKT_CM_SUSPEND) {
	if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING) {
	    cm_send_suspend_msg(vc);
	    vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
	}
	goto fn_exit;
    }

    /*
     * Handle the MPIDI_CH3_PKT_CM_REACTIVATION_DONE packet
     * for the shared memory channel
     */
    else if (pkt->type == MPIDI_CH3_PKT_CM_REACTIVATION_DONE) {
	if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED) {
	    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
	}
	goto fn_exit;
    }
#endif /* defined(CKPT) */

    MPIDI_msg_sz_t buflen = sizeof(MPIDI_CH3_Pkt_t);

    if ((mpi_errno = MPIDI_CH3U_Handle_recv_pkt(
        vc,
        pkt,
        &buflen,
        &vc->smp.recv_active)) != MPI_SUCCESS)
    {
	MPIU_ERR_POP(mpi_errno);
    }

    vc->smp.recv_current_pkt_type = SMP_EAGER_MSG;

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
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
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    int mpi_errno = MPI_SUCCESS;
    int nb;
    int i = 0;
    MPIDI_VC_t *vc;
    int complete;

    for (; i < g_smpi.num_local_nodes; ++i)
    {
	MPIDI_PG_Get_vc(pg, g_smpi.l2g_rank[i], &vc);

#if defined(CKPT)
	/* Don't touch a suspended channel */
	if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED)
	    continue;
#endif /* defined(CKPT) */

	vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
/*
        if (vc->smp.send_active)
*/
	while (vc->smp.send_active != NULL) {
	    MPID_Request *req = vc->smp.send_active;

	    if(req->dev.iov_offset >= req->dev.iov_count) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s",
			"req->dev.iov_offset >= req->dev.iov_count");
	    }

            switch (req->ch.reqtype)
            { 
	        case REQUEST_RNDV_R3_HEADER:
		        vc->smp.send_current_pkt_type = SMP_RNDV_MSG;
                        MPIDI_CH3I_SMP_writev_rndv_header(
                            vc,
			    req->dev.iov + req->dev.iov_offset,
			    req->dev.iov_count - req->dev.iov_offset,
			    &nb
                        );
                    break;
                case REQUEST_RNDV_R3_DATA:
                        if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
                        {
                            mpi_errno = MPIDI_CH3I_SMP_writev_rndv_data(
                                vc,
                                req->dev.iov + req->dev.iov_offset,
                                req->dev.iov_count - req->dev.iov_offset,
                                &nb
                           );
                        }
                        else
                        {
                            MPIDI_CH3I_SMP_writev_rndv_data_cont(
                                vc,
                                req->dev.iov + req->dev.iov_offset,
                                req->dev.iov_count - req->dev.iov_offset,
                                &nb
                            );
                        }
                    break;
                default:
                        MPIDI_CH3I_SMP_writev(
                            vc,
                            req->dev.iov + req->dev.iov_offset,
                            req->dev.iov_count - req->dev.iov_offset,
                            &nb
                        );
                    break;
	    }

            if (mpi_errno != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }

	    DEBUG_PRINT("shm_writev returned %d", nb);

	    if (nb > 0)
            {
                if (MPIDI_CH3I_Request_adjust_iov(req, nb))
                {
		    /* Write operation complete */
                    if ((mpi_errno = MPIDI_CH3U_Handle_send_req(vc, req, &complete)) != MPI_SUCCESS)
                    {
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

		    if(req->dev.iov_offset >= req->dev.iov_count) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**fail", "**fail %s",
				"req->dev.iov_offset >= req->dev.iov_count");
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

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_read_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_read_progress (MPIDI_PG_t* pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t* vc = NULL;
    MPIDI_CH3_Pkt_t* pkt_head = NULL;
    int nb = 0;
    int complete = 0;
    int i = 0;
    int index = -1;
#if defined(_SMP_LIMIC_)
    struct limic_header l_header;
    int use_limic = 0;
#endif

    for (; i < g_smpi.num_local_nodes; ++i)
    {
        if (g_smpi.my_local_id == i)
        {
            continue;
        }

        MPIDI_PG_Get_vc(pg, g_smpi.l2g_rank[i], &vc);

        if (!vc->smp.recv_active)
        {
            MPIDI_CH3I_SMP_pull_header(vc, &pkt_head);

            if (pkt_head)
            {
#if defined(_SMP_LIMIC_)
                if ((mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head, &index, &l_header, &use_limic)))
#else
                if ((mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head, &index)))
#endif
                {
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        }

	if (vc->smp.recv_active)
        {
            switch(vc->smp.recv_current_pkt_type)
            {
            case SMP_RNDV_MSG:
#if defined(_SMP_LIMIC_)
                    mpi_errno = MPIDI_CH3I_SMP_readv_rndv(
                        vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count - vc->smp.recv_active->dev.iov_offset,
                        index,
                        &l_header,
                        &nb, use_limic
                    );
#else
                    mpi_errno = MPIDI_CH3I_SMP_readv_rndv(
                        vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count - vc->smp.recv_active->dev.iov_offset,
                        index,
                        &nb
                    );
#endif
                break;
            case SMP_RNDV_MSG_CONT:
#if defined(_SMP_LIMIC_)
                mpi_errno = MPIDI_CH3I_SMP_readv_rndv_cont(vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->dev.iov_offset, index, &vc->smp.current_l_header,
                        &nb, vc->smp.use_limic);
#else
                mpi_errno = MPIDI_CH3I_SMP_readv_rndv_cont(vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->dev.iov_offset, index, &nb);
#endif
                break;
            default:
		mpi_errno = MPIDI_CH3I_SMP_readv(vc,
			&vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
			vc->smp.recv_active->dev.iov_count - 
			vc->smp.recv_active->dev.iov_offset, &nb);
                break;
            }

	    if (mpi_errno)
            {
                MPIU_ERR_POP(mpi_errno);
            }

	    DEBUG_PRINT("request to fill: iovlen %d, iov[0].len %d, [1] %d, nb %d\n",
		    vc->smp.recv_active->dev.iov_count -
		    vc->smp.recv_active->dev.iov_offset, 
		    vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset].MPID_IOV_LEN, 
		    vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset + 1].MPID_IOV_LEN, nb);
	    if (nb > 0) {
		if (MPIDI_CH3I_Request_adjust_iov(vc->smp.recv_active, nb)) {
		    DEBUG_PRINT("adjust iov finished, handle req\n");
		    mpi_errno =
			MPIDI_CH3U_Handle_recv_req(vc, vc->smp.recv_active, &complete);
		    DEBUG_PRINT("finished handle req, complete %d\n", complete);

		    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

		    if (complete) {
#if defined(_SMP_LIMIC_)
                        /* send completion message with sender's send request
                         * and number of bytes received.
                         * header type is MPIDI_CH3_PKT_LIMIC_COMP
                         */
                        if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG && use_limic) {
                            MPIDI_CH3I_SMP_send_limic_comp(&l_header, vc, nb);
                        } else if (vc->smp.recv_current_pkt_type == SMP_RNDV_MSG_CONT &&
                                   vc->smp.use_limic) {
                            vc->smp.current_nb += nb;
                            MPIDI_CH3I_SMP_send_limic_comp(&vc->smp.current_l_header, vc, vc->smp.current_nb);
                        }
#endif
                        vc->smp.recv_active = NULL;
		    } else {
                        if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG) {
                            vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;
#if defined(_SMP_LIMIC_)
                            if (use_limic) {
                                vc->smp.current_l_header = l_header;
                                vc->smp.current_nb = nb;
                                vc->smp.use_limic = 1;
                            } else {
                                vc->smp.use_limic = 0;
                            }
#endif
                        }
#if defined(_SMP_LIMIC_)
                        else {
                            if (vc->smp.use_limic) {
                                vc->smp.current_nb += nb;
                            }
                        }
#endif
                    }
		} else {
#if defined(_SMP_LIMIC_)
                    MPIU_Assert(vc->smp.recv_current_pkt_type != SMP_RNDV_MSG ||
                        !use_limic);
#endif
		    if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG)
		        vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;
		}
	    }
	} 
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
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
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_INIT);
    int mpi_errno = MPI_SUCCESS;
    int use_smp;
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
    SEND_BUF_T *send_buf = NULL;
#if defined(SOLARIS)
    char *setdir="/tmp";
#else
    char *setdir="/dev/shm";
#endif
    char *shmem_dir, *shmdir;
    size_t pathlen;
#if defined(_X86_64_)
    volatile char tmpchar;
#endif /* defined(_X86_64_) */
    int default_eager_size = 1;

    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        blocking_val = !!atoi(value);
        if(blocking_val) {
            /* blocking is enabled, so
             * automatically disable
             * shared memory */
            return MPI_SUCCESS;
        }
    }

    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {

        use_smp= atoi(value);

        if (!use_smp) { 
            return MPI_SUCCESS;
        }
    }

    if(MPIDI_CH3I_Process.has_dpm) {
        return MPI_SUCCESS;
    }

    /*
     * Do the initializations here. These will be needed on restart
     * after a checkpoint has been taken.
     */
    g_smp_eagersize = SMP_EAGERSIZE;
    s_smpi_length_queue = SMPI_LENGTH_QUEUE;
    s_smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
    s_smp_batch_size = SMP_BATCH_SIZE;

    if ((value = getenv("SMP_EAGERSIZE")) != NULL) {
        g_smp_eagersize = atoi(value);
	default_eager_size = 0;
    }
    if ((value = getenv("SMPI_LENGTH_QUEUE")) != NULL) {
        s_smpi_length_queue = atoi(value);
    }
    if ((value = getenv("SMP_NUM_SEND_BUFFER")) != NULL ) {
        s_smp_num_send_buffer = atoi(value);
    }
    if ((value = getenv("SMP_BATCH_SIZE")) != NULL ) {
        s_smp_batch_size = atoi(value);
    }
    if ((shmdir = getenv("MV2_SHMEM_DIR")) != NULL) {
        shmem_dir = shmdir;
    } else {
        shmem_dir = setdir;
    }
    pathlen = strlen(shmem_dir);
#if defined(_SMP_LIMIC_)
    if ((value = getenv("MV2_SMP_USE_LIMIC2")) != NULL) {
        g_smp_use_limic2 = atoi(value);
    }
#endif

#if defined(USE_PROCESSOR_AFFINITY)
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    /* by default turn off affinity if THREAD_MULTIPLE
       is requested
    */
    MPIU_THREAD_CHECK_BEGIN
        viadev_enable_affinity = 0;
    MPIU_THREAD_CHECK_END
#endif /* (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE) */

    if ((value = getenv("MV2_ENABLE_AFFINITY")) != NULL) {
        viadev_enable_affinity = atoi(value);
    }

    if (viadev_enable_affinity && (value = getenv("MV2_CPU_MAPPING")) != NULL)
    {
        int linelen = strlen(value);

        if (linelen < s_cpu_mapping_line_max)
        { 
            s_cpu_mapping_line_max = linelen;
        }

        s_cpu_mapping = (char*) MPIU_Malloc(sizeof(char) * (s_cpu_mapping_line_max + 1));
        strncpy(s_cpu_mapping, value, s_cpu_mapping_line_max);
        s_cpu_mapping[s_cpu_mapping_line_max] = '\0';
    }
#endif /* defined(USE_PROCESSOR_AFFINITY) */

    if (gethostname(s_hostname, sizeof(char) * HOSTNAME_LEN) < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "gethostname", strerror(errno));
    }

    DEBUG_PRINT("gethostname: %s\n", s_hostname);

    if ((mpi_errno = smpi_exchange_info(pg)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    DEBUG_PRINT("finished exchange info\n");

#ifdef _SMP_LIMIC_
    if(default_eager_size && arch_type == MULTI_CORE_ARCH_NEHALEM) {
	g_smp_eagersize = 64;
     }
#endif /* _SMP_LIMIC_ */

    /* Convert to bytes */
    g_smp_eagersize = g_smp_eagersize * 1024 + 1;
    s_smpi_length_queue = s_smpi_length_queue * 1024;

#if defined(DEBUG)
    int my_rank;
    PMI_Get_rank(&my_rank);

    if (my_rank == 0)
    {
	DEBUG_PRINT("smp eager size %d, smp queue length %d\n",
		g_smp_eagersize, s_smpi_length_queue);
    }
#endif /* defined(DEBUG) */

    if (g_smp_eagersize > s_smpi_length_queue / 2) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "SMP_EAGERSIZE should not exceed half of "
		"SMPI_LENGTH_QUEUE. Note that SMP_EAGERSIZE "
		"and SMPI_LENGTH_QUEUE are set in KBytes.");
    }

    g_smpi.available_queue_length =
        (s_smpi_length_queue - g_smp_eagersize - sizeof(int));

    /* add pid for unique file name */
    shmem_file =
        (char *) MPIU_Malloc(sizeof(char) * (pathlen + HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    if(!shmem_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "shmem_file");
    }

    pool_file =
        (char *) MPIU_Malloc (sizeof (char) * (pathlen + HOSTNAME_LEN + 26 + PID_CHAR_LEN));

    if(!pool_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "pool_file");
    }

    /* unique shared file name */
    sprintf(shmem_file, "%s/ib_shmem-%s-%s-%d.tmp",
	    shmem_dir, pg->ch.kvs_name, s_hostname, getuid());

    DEBUG_PRINT("shemfile %s\n", shmem_file);

    sprintf (pool_file, "%s/ib_pool-%s-%s-%d.tmp", shmem_dir, pg->ch.kvs_name,
	    s_hostname, getuid ());
    DEBUG_PRINT("shemfile %s\n", pool_file);

    /* open the shared memory file */
    g_smpi.fd =
	open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);

    if (g_smpi.fd < 0) {
        /* fallback */
        sprintf(shmem_file, "/tmp/ib_shmem-%s-%s-%d.tmp",
                pg->ch.kvs_name, s_hostname, getuid());

        DEBUG_PRINT("shemfile %s\n", shmem_file);

        sprintf (pool_file, "/tmp/ib_pool-%s-%s-%d.tmp", pg->ch.kvs_name,
                s_hostname, getuid ());
        DEBUG_PRINT("shemfile %s\n", pool_file);

        g_smpi.fd =
            open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (g_smpi.fd < 0) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "%s: %s", "open", strerror(errno));
        }
    }

    g_smpi.fd_pool =
	open (pool_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (g_smpi.fd_pool < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "open", strerror(errno));
    }

    /* compute the size of this file */
    local_num = g_smpi.num_local_nodes * g_smpi.num_local_nodes;
    pid_len = g_smpi.num_local_nodes * sizeof(int);
    param_len = sizeof(smpi_params) * local_num;
    rq_len = sizeof(smpi_rqueues) * local_num;
    limit_len = sizeof(smpi_rq_limit) * local_num;
    sh_size = sizeof(struct shared_mem) + pid_len + param_len + rq_len +
	limit_len + SMPI_CACHE_LINE_SIZE * 4;

    g_size_shmem = (SMPI_CACHE_LINE_SIZE + sh_size + pagesize +
	    (g_smpi.num_local_nodes * (g_smpi.num_local_nodes - 1) *
	     (SMPI_ALIGN(s_smpi_length_queue + pagesize))));

    DEBUG_PRINT("sizeof shm file %d\n", g_size_shmem);

    g_size_pool =
	SMPI_ALIGN (sizeof (SEND_BUF_T) * s_smp_num_send_buffer +
		pagesize) * g_smpi.num_local_nodes + SMPI_CACHE_LINE_SIZE;

    DEBUG_PRINT("sizeof pool file %d\n", g_size_pool);

    /* initialization of the shared memory file */
    /* just set size, don't really allocate memory, to allow intelligent memory
     * allocation on NUMA arch */
    if (g_smpi.my_local_id == 0) {
	if (ftruncate(g_smpi.fd, 0)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	/* set file size, without touching pages */
	if (ftruncate(g_smpi.fd, g_size_shmem)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	if (ftruncate (g_smpi.fd_pool, 0)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

	if (ftruncate (g_smpi.fd_pool, g_size_pool)) {
	    int ftruncate_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "ftruncate", strerror(ftruncate_errno));
	}

#if !defined(_X86_64_)
	{
	    char *buf;
	    buf = (char *) MPIU_Calloc(g_size_shmem + 1, sizeof(char));
	    if (write(g_smpi.fd, buf, g_size_shmem) != g_size_shmem) {
		int write_errno = errno;

		MPIU_Free(buf);
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "write", strerror(write_errno));
	    }
            MPIU_Free(buf);
	}

	{
	    char *buf;
	    buf = (char *) MPIU_Calloc (g_size_pool + 1, sizeof (char));
	    if (write (g_smpi.fd_pool, buf, g_size_pool) != g_size_pool) {
		int write_errno = errno;

		MPIU_Free(buf);
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "write", strerror(write_errno));
	    }
	    MPIU_Free(buf);
	}
#endif /* !defined(_X86_64_) */

	if (lseek(g_smpi.fd, 0, SEEK_SET) != 0) {
	    int lseek_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
	}

	if (lseek (g_smpi.fd_pool, 0, SEEK_SET) != 0) {
	    int lseek_errno = errno;

	    /* to clean up tmp shared file */
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "lseek", strerror(lseek_errno));
	}

    }

    if (enable_shmem_collectives){
	/* Shared memory for collectives */

        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_init(pg)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    DEBUG_PRINT("process arrives before sync stage\n");
    /* synchronization between local processes */
    do {
	if (fstat(g_smpi.fd, &file_status) != 0 ||
		fstat (g_smpi.fd_pool, &file_status_pool) != 0) {
	    int fstat_errno = errno;

	    /* to clean up tmp shared file */
	    unlink(shmem_file);
	    MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "%s: %s", "fstat", strerror(fstat_errno));
	}
	usleep(1);
    }
    while (file_status.st_size != g_size_shmem ||
	    file_status_pool.st_size != g_size_pool);

    g_smpi_shmem = (struct shared_mem *) MPIU_Malloc(sizeof(struct shared_mem));

    if(!g_smpi_shmem) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "g_smpi_shmem");
    }

    DEBUG_PRINT("before mmap\n");
    /* mmap of the shared memory file */
    g_smpi.mmap_ptr = mmap(0, g_size_shmem,
	    (PROT_READ | PROT_WRITE), (MAP_SHARED), g_smpi.fd,
	    0);
    if (g_smpi.mmap_ptr == (void *) -1) {
	/* to clean up tmp shared file */
	unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "mmap", strerror(errno));
    }

    g_smpi.send_buf_pool_ptr = mmap (0, g_size_pool, (PROT_READ | PROT_WRITE),
	    (MAP_SHARED), g_smpi.fd_pool, 0);

    if (g_smpi.send_buf_pool_ptr == (void *) -1) {
	unlink (pool_file);
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		"%s: %s", "mmap", strerror(errno));
    }

    shmem = (struct shared_mem *) g_smpi.mmap_ptr;
    if (((long) shmem & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
	/* to clean up tmp shared file */
	unlink(shmem_file);
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "error in shifting mmapped shared memory");
    }

    s_buffer_head = (SEND_BUF_T **) MPIU_Malloc(sizeof(SEND_BUF_T *) * g_smpi.num_local_nodes);
    for(i=0; i < g_smpi.num_local_nodes; ++i){
	s_buffer_head[i] = (SEND_BUF_T *)((unsigned long)g_smpi.send_buf_pool_ptr +
		SMPI_ALIGN(sizeof(SEND_BUF_T) * s_smp_num_send_buffer + 
		    pagesize) * i);

	if (((long) s_buffer_head[i] & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
	    unlink (pool_file);
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s",
		    "error in shifting mmapped shared pool memory");
	}
    }
    s_my_buffer_head = s_buffer_head[g_smpi.my_local_id];

    s_sh_buf_pool.free_head = 0;

    s_sh_buf_pool.send_queue = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);

    if(!s_sh_buf_pool.send_queue) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "s_sh_buf_pool.send_queue");
    }

    s_sh_buf_pool.tail = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);

    if(!s_sh_buf_pool.tail) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "s_sh_buf_pool.tail");
    }

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
	s_sh_buf_pool.send_queue[i] = s_sh_buf_pool.tail[i] = -1;
    }

#if defined(_X86_64_)
    for (i = 0; i < s_smp_num_send_buffer; ++i) {
	send_buf =&(s_my_buffer_head[i]);
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
#else /* defined(_X86_64_) */
    if (0 == g_smpi.my_local_id) {
	for(j = 0; j < g_smpi.num_local_nodes; ++j){
	    for (i = 0; i < s_smp_num_send_buffer; ++i) {
		send_buf = &s_buffer_head[j][i];
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
#endif /* defined(_X86_64_) */

    /* Initialize shared_mem pointers */
    g_smpi_shmem->pid = (int *) shmem;

    g_smpi_shmem->rqueues_params =
        (smpi_params **) MPIU_Malloc(sizeof(smpi_params *)*g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_flow_out =
        (smpi_rqueues **) MPIU_Malloc(sizeof(smpi_rqueues *)*g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_limits =
        (smpi_rq_limit **) MPIU_Malloc(sizeof(smpi_rq_limit *)*g_smpi.num_local_nodes);

    if (g_smpi_shmem->rqueues_params == NULL ||
	    g_smpi_shmem->rqueues_flow_out == NULL ||
	    g_smpi_shmem->rqueues_limits == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "smpi_shmem rqueues");
    }

    g_smpi_shmem->rqueues_params[0] =
	(smpi_params *)((char *)shmem + SMPI_ALIGN(pid_len) + SMPI_CACHE_LINE_SIZE);
    g_smpi_shmem->rqueues_flow_out[0] =
	(smpi_rqueues *)((char *)g_smpi_shmem->rqueues_params[0] + 
		SMPI_ALIGN(param_len) + SMPI_CACHE_LINE_SIZE);
    g_smpi_shmem->rqueues_limits[0] =
	(smpi_rq_limit *)((char *)g_smpi_shmem->rqueues_flow_out[0] + 
		SMPI_ALIGN(rq_len) + SMPI_CACHE_LINE_SIZE);
    g_smpi_shmem->pool =
	(char *)((char *)g_smpi_shmem->rqueues_limits[0] + SMPI_ALIGN(limit_len) +
		SMPI_CACHE_LINE_SIZE);

    for (i = 1; i < g_smpi.num_local_nodes; ++i) {
	g_smpi_shmem->rqueues_params[i] = (smpi_params *)
	    (g_smpi_shmem->rqueues_params[i-1] + g_smpi.num_local_nodes);
	g_smpi_shmem->rqueues_flow_out[i] = (smpi_rqueues *)
	    (g_smpi_shmem->rqueues_flow_out[i-1] + g_smpi.num_local_nodes);
	g_smpi_shmem->rqueues_limits[i] = (smpi_rq_limit *)
	    (g_smpi_shmem->rqueues_limits[i-1] + g_smpi.num_local_nodes);
    }

    /* init rqueues in shared memory */
    if (0 == g_smpi.my_local_id) {
	pool = pagesize;
	for (i = 0; i < g_smpi.num_local_nodes; ++i) {
	    for (j = 0; j < g_smpi.num_local_nodes; ++j) {
		if (i != j) {
		    READBAR();
		    g_smpi_shmem->rqueues_limits[i][j].first =
			SMPI_ALIGN(pool);
		    g_smpi_shmem->rqueues_limits[i][j].last =
			SMPI_ALIGN(pool + g_smpi.available_queue_length);
		    g_smpi_shmem->rqueues_params[i][j].current =
			SMPI_ALIGN(pool);
		    g_smpi_shmem->rqueues_params[j][i].next =
			SMPI_ALIGN(pool);
		    g_smpi_shmem->rqueues_params[j][i].msgs_total_in = 0;
		    g_smpi_shmem->rqueues_flow_out[i][j].msgs_total_out = 0;
		    READBAR();
		    pool += SMPI_ALIGN(s_smpi_length_queue + pagesize);
		}
	    }
	}
    }

    if (enable_shmem_collectives)
    {
	/* Memory Mapping shared files for collectives*/
        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_Mmap()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    /* another synchronization barrier */
    if (0 == g_smpi.my_local_id) {
	wait = 1;
	while (wait) {
	    wait = 0;
	    for (i = 1; i < g_smpi.num_local_nodes; ++i) {
		if (g_smpi_shmem->pid[i] == 0) {
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

	g_smpi_shmem->pid[g_smpi.my_local_id] = pid;
	WRITEBAR();
    } else {
	while (g_smpi_shmem->pid[0] != 0);
	while (g_smpi_shmem->pid[0] == 0) {
	    g_smpi_shmem->pid[g_smpi.my_local_id] = getpid();
	    WRITEBAR();
	}
	for (i = 0; i < g_smpi.num_local_nodes; ++i) {
	    if (g_smpi_shmem->pid[i] <= 0) {
		MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
			"%s: %s", "getpid", strerror(errno));
	    }
	}
    }

    MPIU_Free(shmem_file);
    MPIU_Free(pool_file);

#if defined(_X86_64_)
    /*
     * Okay, here we touch every page in the shared memory region.
     * We do this to get the pages allocated so that they are local
     * to the receiver on a numa machine (instead of all being located
     * near the first process).
     */
    {
	int receiver, sender;

	for (receiver = 0; receiver < g_smpi.num_local_nodes; ++receiver) {
	    volatile char *ptr = g_smpi_shmem->pool;
	    volatile char tmp;

	    sender = g_smpi.my_local_id;
	    if (sender != receiver) {
		int k;

		for (k = SMPI_FIRST(sender, receiver);
			k < SMPI_LAST(sender, receiver); k += pagesize) {
		    tmp = ptr[k];
		}
	    }
	}
    }
#endif /* defined(_X86_64_) */

    s_current_ptr = (void **) MPIU_Malloc(sizeof(void *) * g_smpi.num_local_nodes);

    if(!s_current_ptr) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "s_current_ptr");
    }

    s_current_bytes = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);

    if(!s_current_bytes) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "s_current_bytes");
    }

    s_total_bytes = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);

    if(!s_total_bytes) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "s_total_bytes");
    }

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
	s_current_ptr[i] = NULL;
	s_current_bytes[i] = 0;
	s_total_bytes[i] = 0;
    }

#ifdef _SMP_LIMIC_
    if (g_smp_use_limic2) {
        limic_fd = limic_open();
    
        if (limic_fd == -1) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
                        "%s: %s", "LiMIC2 device does not exist", 
                        strerror(errno));
        }
    }
#endif

    SMP_INIT = 1;

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_INIT);
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
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);

    /* unmap the shared memory file */
    munmap(g_smpi.mmap_ptr, g_size_shmem); 
    close(g_smpi.fd);

    munmap(g_smpi.send_buf_pool_ptr, g_size_pool); 
    close(g_smpi.fd_pool);

    if(s_buffer_head) {
        MPIU_Free(s_buffer_head);
    }

    if (g_smpi.l2g_rank) { 
	MPIU_Free(g_smpi.l2g_rank);
    } 

    if(g_smpi_shmem) {
       if(g_smpi_shmem->rqueues_params != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_params);
       } 
       if(g_smpi_shmem->rqueues_flow_out != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_flow_out);
       } 
       if(g_smpi_shmem->rqueues_limits != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_limits);
       }
       if(g_smpi_shmem != NULL) { 
          MPIU_Free(g_smpi_shmem);
       }
    }

    if(s_current_ptr) {
	MPIU_Free(s_current_ptr);
    }

    if(s_current_bytes) {
	MPIU_Free(s_current_bytes);
    }

    if(s_total_bytes) {
	MPIU_Free(s_total_bytes);
    } 

    if(s_sh_buf_pool.send_queue) {
	MPIU_Free(s_sh_buf_pool.send_queue);
    }

    if(s_sh_buf_pool.tail) {
	MPIU_Free(s_sh_buf_pool.tail);
    }

    if (enable_shmem_collectives){
	/* Freeing up shared memory collective resources*/
	MPIDI_CH3I_SHMEM_COLL_finalize();
    }

#if defined(USE_PROCESSOR_AFFINITY)
    PLPA_NAME(finalize)();
#endif

#ifdef _SMP_LIMIC_
    limic_close(limic_fd);
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_writev_rndv_header(MPIDI_VC_t * vc, const MPID_IOV * iov,
	const int n, int *num_bytes_ptr)
{
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    int i, offset = 0;
    MPIDI_CH3_Pkt_send_t *pkt_header;
#if defined(_SMP_LIMIC_)
    int err;
    int total_bytes = 0;
    MPID_Request *sreq = NULL;
    limic_user lu;
#endif

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (!pkt_avail) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
	    + SMPI_NEXT(g_smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

#if defined(_SMP_LIMIC_) 
    if (g_smp_use_limic2) {
        /* iov[0] is the header pkt */
        pkt_header = (MPIDI_CH3_Pkt_send_t *)(iov[0].MPID_IOV_BUF);
    
        /* sreq is the send request handle for the data */
        sreq = pkt_header->mrail.send_req_id;
        assert(sreq->dev.iov_count == 1);
        /* The last sizeof(int) is the total num of data bytes */
        pkt_len = iov[0].MPID_IOV_LEN + sizeof(limic_user) * sreq->dev.iov_count + sizeof(int); 
             
        if (pkt_avail < pkt_len) {
            *num_bytes_ptr = 0;
            goto fn_exit;
        }

        /* number of bytes */
        ptr = (void *) ((unsigned long) ptr + sizeof(int));

        /* header format: 
         * normal header | lu | total_num_bytes 
         */
        memcpy(ptr, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN);
        ptr = (void *) ((unsigned long) ptr +
                  iov[0].MPID_IOV_LEN);
 
        for(i = 0; i < sreq->dev.iov_count; ++i) {
            err = limic_tx_init( limic_fd, sreq->dev.iov[i].MPID_IOV_BUF, 
                      sreq->dev.iov[i].MPID_IOV_LEN, &lu);
            if (err != sreq->dev.iov[i].MPID_IOV_LEN)
                MPIU_ERR_SETFATALANDJUMP1(err, MPI_ERR_OTHER,
                    "**fail", "**fail %s",
                    "LiMIC: (MPIDI_CH3I_SMP_writev_rndv_header) limic_tx_init fail");

            total_bytes += sreq->dev.iov[i].MPID_IOV_LEN;

            /* copy the limic_user information to the shared memory
               and move the shared memory pointer properly 
             */
            memcpy(ptr, &lu, sizeof(limic_user));
            ptr = (void *) ((unsigned long) ptr + sizeof(limic_user));
        }

        *((int *) ptr) = total_bytes;
        ptr = (void *) ((unsigned long) ptr + sizeof(int));

        /* the first element will be pkt_len (header len) */    
        *((int *) ptr_head) = pkt_len;
        *num_bytes_ptr = iov[0].MPID_IOV_LEN;

        smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
            pkt_len + sizeof(int));
    } else {
#endif /* _SMP_LIMIC */

    if (pkt_avail < SMPI_SMALLEST_SIZE) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    } 

    send_buf_reclaim();

    if (s_sh_buf_pool.free_head == -1) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    pkt_header = (MPIDI_CH3_Pkt_send_t *)(iov[0].MPID_IOV_BUF);
    pkt_header->mrail.src.smp_index = s_sh_buf_pool.free_head;

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
		++i;
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

	smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
		pkt_len + sizeof(int));
	if (i == n) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
#if defined(_SMP_LIMIC_)
    }
#endif /* _SMP_LIMIC_ */

fn_exit:
    DEBUG_PRINT("writev_rndv_header returns bytes %d\n", *num_bytes_ptr);
    return;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_data_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_writev_rndv_data_cont(MPIDI_VC_t * vc, const MPID_IOV * iov,
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

    if (s_sh_buf_pool.free_head == -1) {
	*num_bytes_ptr = 0;
	goto fn_exit;
    }

    first_index = s_sh_buf_pool.free_head;

    i = 0;
    *num_bytes_ptr = 0;
    do {
	pkt_len = 0;
	for (; i < n;) {
	    DEBUG_PRINT
		("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
		 iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
		 pkt_avail);

	    if(has_sent >= s_smp_batch_size)
		break;
	    ++has_sent;

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
		++i;
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

	if (i == n || has_sent >= s_smp_batch_size) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	send_buf_reclaim();

    } while (s_sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
	tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
	    + SMPI_NEXT(g_smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    ptr_head =(void *) ((unsigned long)ptr_head + sizeof(int));
    (*((int *) ptr_head)) = first_index;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
	    2*sizeof(int));

fn_exit:
    DEBUG_PRINT("writev_rndv_data_cont returns bytes %d\n", *num_bytes_ptr);
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

    if(s_sh_buf_pool.free_head == -1) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "s_sh_buf_pool.free_head == -1");
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
	    if(has_sent >= s_smp_batch_size)
		break;
	    ++has_sent;

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
		++i;
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

	if (i == n || has_sent >= s_smp_batch_size) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	send_buf_reclaim();

    } while (s_sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
	tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
	    + SMPI_NEXT(g_smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
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
void MPIDI_CH3I_SMP_writev(MPIDI_VC_t * vc, const MPID_IOV * iov,
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

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
	    + SMPI_NEXT(g_smpi.my_local_id,
		vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
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
		++i;
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

	smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
		(pkt_len + sizeof(int)));

	if (i == n) {
	    DEBUG_PRINT("counter value, in %d, out %d\n",
		    SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
	    break;
	}

	pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);
	if (pkt_avail != 0) {
	    pkt_avail =
		(pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
	    pkt_avail -= sizeof(int);
	    ptr_head = (void *) ((g_smpi_shmem->pool)
		    + SMPI_NEXT(g_smpi.my_local_id,
			vc->smp.local_nodes));
	    ptr = (void *) ((unsigned long) ptr_head + sizeof(int));
	}
	DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
fn_exit:
    DEBUG_PRINT("writev returns bytes %d\n", *num_bytes_ptr);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv_rndv_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#if defined(_SMP_LIMIC_)
int MPIDI_CH3I_SMP_readv_rndv_cont(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, struct limic_header *l_header,
        int *num_bytes_ptr, int use_limic)
#else
int MPIDI_CH3I_SMP_readv_rndv_cont(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, int *num_bytes_ptr)
#endif
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
#if defined(_SMP_LIMIC_)
    int i, err, old_len;
    int total_bytes = l_header->total_bytes;
#endif
    /* all variable must be declared before the state declarations */

    *num_bytes_ptr = 0;

#if defined(_SMP_LIMIC_)
    if (use_limic) {
        /* copy the message from the send buffer to the receive buffer */
        msglen = total_bytes;
        iov_len = iov[0].MPID_IOV_LEN;

        for (i = 0; total_bytes > 0 && iov_off < iovlen; ) {
            if (msglen == iov_len) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
         		}

                received_bytes += msglen;
                total_bytes -= msglen;

                assert(total_bytes == 0 && ++iov_off >= iovlen);

            } else if (msglen > iov_len) {
                old_len = l_header->lu.length;
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      iov_len, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
         		}

                received_bytes += iov_len;
                total_bytes -= iov_len;
                msglen -= iov_len;

                adjust_lu_info(&(l_header->lu), old_len);

                if (++iov_off >= iovlen)
                    break;
                buf_off = 0;
                iov_len = iov[iov_off].MPID_IOV_LEN;

            }  else if (msglen > 0) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
		}

                received_bytes += msglen;
                total_bytes -= msglen;
            }
        }

        *num_bytes_ptr = received_bytes;
        l_header->total_bytes -= received_bytes;
    } else {
#endif /* _SMP_LIMIC_ */
    if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
	if(s_total_bytes[recv_vc_ptr->smp.local_nodes] != 0) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "s_total_bytes[recv_vc_ptr->smp.local_nodes] "
		    "!= 0");
	}

	if (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) ==
		SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id)) {
	    goto fn_exit;
	} 

	READBAR();
	s_current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *) ((g_smpi_shmem->pool) +
		    SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));
	WRITEBAR();
	s_total_bytes[recv_vc_ptr->smp.local_nodes] =
	    *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
	s_current_bytes[recv_vc_ptr->smp.local_nodes] =
	    s_total_bytes[recv_vc_ptr->smp.local_nodes];

	DEBUG_PRINT
	    ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
	     s_current_bytes[recv_vc_ptr->smp.local_nodes],
	     s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
	     iov[0].MPID_IOV_LEN);
	WRITEBAR();

	s_current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *)((unsigned long) s_current_ptr[recv_vc_ptr->smp.local_nodes] +
		    sizeof(int)); 
	current_index = *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
	smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
		g_smpi.my_local_id, 2*sizeof(int));
    } else {
	s_total_bytes[recv_vc_ptr->smp.local_nodes] =
	    s_current_bytes[recv_vc_ptr->smp.local_nodes]; 
	current_index = recv_vc_ptr->smp.read_index;
	recv_offset = recv_vc_ptr->smp.read_off;
    }

    if (current_index != -1) {
	/** last smp packet has not been drained up yet **/
	DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
		iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
		iov[iov_off].MPID_IOV_LEN);

	recv_buf = &s_buffer_head[destination][current_index];

	if(recv_buf->busy != 1) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "recv_buf->busy != 1");
	}

	msglen = recv_buf->len - recv_offset;
	current_buf = (void *)((unsigned long) recv_buf->buf + recv_offset);
	iov_len = iov[0].MPID_IOV_LEN;

	for (;
		iov_off < iovlen
		&& s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0
		&& current_index != -1;) {

	    if (msglen > iov_len) {
		READBAR(); 
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len); 
		READBAR();
		current_buf = (void *) ((unsigned long) current_buf +
			iov_len);
		msglen -= iov_len;
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		++iov_off;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = current_index;
		    recv_vc_ptr->smp.read_off = (unsigned long) current_buf -
			(unsigned long) recv_buf->buf;
		    break;
		}

		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}

		else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s",
			    "s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;
	    } else if (msglen == iov_len) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len);
		READBAR();
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		++iov_off;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = recv_buf->next;
		    recv_vc_ptr->smp.read_off = 0;
		    recv_buf->busy = 0;
		    break;
		}
		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
		    MPIU_Assert(s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0);
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
		recv_buf = &s_buffer_head[destination][current_index];
		MPIU_Assert(recv_buf->busy == 1);
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
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}
		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &s_buffer_head[destination][current_index];
		MPIU_Assert(recv_buf->busy == 1);
		msglen = recv_buf->len;
		current_buf = (void *)recv_buf->buf;
	    }
	}
	*num_bytes_ptr += received_bytes;
	DEBUG_PRINT
	    ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
	     s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
	     iov_off, iovlen);

	if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
	}
	received_bytes = 0;
	if (iov_off == iovlen) {
	    /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
	    goto fn_exit;
	}
    }
#if defined(_SMP_LIMIC_)
    }
#endif

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
#if defined(_SMP_LIMIC_)
int MPIDI_CH3I_SMP_readv_rndv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, struct limic_header *l_header, int *num_bytes_ptr, int use_limic)
#else
int MPIDI_CH3I_SMP_readv_rndv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
	const int iovlen, int index, int *num_bytes_ptr)
#endif
{
    int mpi_errno = MPI_SUCCESS;
    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    int msglen, iov_len;
    /* all variable must be declared before the state declarations */
#if defined(_SMP_LIMIC_)
    int i, err, old_len;
    int total_bytes = l_header->total_bytes;
#endif
    int destination = recv_vc_ptr->smp.local_nodes;
    int current_index = index;
    void *current_buf;
    SEND_BUF_T *recv_buf;

    *num_bytes_ptr = 0;

    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
	READBAR();
	s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	DEBUG_PRINT("total in %d, total out %d\n",
		SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id),
		SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
                    g_smpi.my_local_id));

	smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
		g_smpi.my_local_id,
		s_total_bytes[recv_vc_ptr->smp.local_nodes] +
		sizeof(int));

	DEBUG_PRINT("total in %d, total out %d\n",
		SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id),
		SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id));
	s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }

#if defined(_SMP_LIMIC_)
    if (use_limic) {
        /* copy the message from the send buffer to the receive buffer */
        msglen = total_bytes;
        iov_len = iov[0].MPID_IOV_LEN;

        for (i = 0; total_bytes > 0 && iov_off < iovlen; ) {
            if (msglen == iov_len) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err ) 
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += msglen;
                total_bytes -= msglen;
  
                assert(total_bytes == 0 && ++iov_off >= iovlen);

            } else if (msglen > iov_len) {
                old_len = l_header->lu.length;
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      iov_len, &(l_header->lu));
                if( !err )
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += iov_len;
                total_bytes -= iov_len;
                msglen -= iov_len;

                adjust_lu_info(&(l_header->lu), old_len);

                if (++iov_off >= iovlen)
                    break;
                buf_off = 0;
                iov_len = iov[iov_off].MPID_IOV_LEN;
            
            } else if (msglen > 0) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err )
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += msglen;
                total_bytes -= msglen;
            }   
        }

        *num_bytes_ptr = received_bytes;
        l_header->total_bytes -= received_bytes;
    } else {
#endif /* _SMP_LIMIC_ */
    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) ==
	    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id)); 

    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
	(void *) ((g_smpi_shmem->pool) +
		SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id));
    WRITEBAR();
    s_total_bytes[recv_vc_ptr->smp.local_nodes] =
	*((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
    s_current_bytes[recv_vc_ptr->smp.local_nodes] =
	s_total_bytes[recv_vc_ptr->smp.local_nodes];
    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
	    g_smpi.my_local_id,sizeof(int));
    DEBUG_PRINT
	("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
	 s_current_bytes[recv_vc_ptr->smp.local_nodes],
	 s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
	 iov[0].MPID_IOV_LEN);

    WRITEBAR();

    if (current_index != -1) {
	/** last smp packet has not been drained up yet **/
	DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
		iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
		iov[iov_off].MPID_IOV_LEN);

	recv_buf = &s_buffer_head[destination][current_index];

	if(recv_buf->busy != 1) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "recv_buf->busy == 1");
	}

	msglen = recv_buf->len;
	current_buf = (void *)recv_buf->buf;
	iov_len = iov[0].MPID_IOV_LEN;

	for (;
		iov_off < iovlen
		&& s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0
		&& current_index != -1;) {

	    if (msglen > iov_len) {
		READBAR(); 
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len); 
		READBAR();
		current_buf = (void *) ((unsigned long) current_buf +
			iov_len);
		msglen -= iov_len;
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		++iov_off;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = current_index; 
		    recv_vc_ptr->smp.read_off = (unsigned long) current_buf - 
			(unsigned long) recv_buf->buf;
		    break;
		}
		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
		    MPIU_Assert(s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0); 
		    recv_buf->busy = 0;
		    break;
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;
	    } else if (msglen == iov_len) {
		READBAR();
		memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
			(void *) current_buf, iov_len);
		READBAR();
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov_len;
		received_bytes += iov_len;
		buf_off = 0;
		++iov_off;

		if (iov_off >= iovlen) {
		    recv_vc_ptr->smp.read_index = recv_buf->next;
		    recv_vc_ptr->smp.read_off = 0;
		    recv_buf->busy = 0;
		    break;
		}

		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}

		else if(s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s",
			    "s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
		}

		iov_len = iov[iov_off].MPID_IOV_LEN;

		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &s_buffer_head[destination][current_index];

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
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

		if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
		    recv_buf->busy = 0;
		    break;
		}
		if(recv_buf->has_next == 0){
		    recv_buf->busy = 0;
		    break;
		}

		current_index = recv_buf->next;
		recv_buf->busy = 0;
		recv_buf = &s_buffer_head[destination][current_index];

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
	     s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
	     iov_off, iovlen);

	if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
	}
	received_bytes = 0;
	if (iov_off == iovlen) {
	    /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
	    goto fn_exit;
	}
    }
#if defined(_SMP_LIMIC_)
    }
#endif

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
	 s_current_bytes[recv_vc_ptr->smp.local_nodes],
	 s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
	 iov[0].MPID_IOV_LEN);
    WRITEBAR();
    if (s_current_ptr[recv_vc_ptr->smp.local_nodes] != NULL) {
	/** last smp packet has not been drained up yet **/
	DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
		iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
		iov[iov_off].MPID_IOV_LEN);
	for (;
		iov_off < iovlen
		&& s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
	    if (s_current_bytes[recv_vc_ptr->smp.local_nodes] >=
		    iov[iov_off].MPID_IOV_LEN) {
		READBAR(); 
		memcpy((void *) iov[iov_off].MPID_IOV_BUF,
			(void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
			iov[iov_off].MPID_IOV_LEN);
		READBAR();
		s_current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    s_current_ptr[recv_vc_ptr->smp.local_nodes] +
			    iov[iov_off].MPID_IOV_LEN);
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    iov[iov_off].MPID_IOV_LEN;
		received_bytes += iov[iov_off].MPID_IOV_LEN;
		++iov_off;
	    } else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
		READBAR();
		memcpy((void *) iov[iov_off].MPID_IOV_BUF,
			(void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
			s_current_bytes[recv_vc_ptr->smp.local_nodes]);
		READBAR();
		s_current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    s_current_ptr[recv_vc_ptr->smp.local_nodes] +
			    s_current_bytes[recv_vc_ptr->smp.local_nodes]);
		received_bytes +=
		    s_current_bytes[recv_vc_ptr->smp.local_nodes];
		buf_off = s_current_bytes[recv_vc_ptr->smp.local_nodes];
		s_current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
	    }
	}
	*num_bytes_ptr += received_bytes;
	DEBUG_PRINT
	    ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
	     s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
	     iov_off, iovlen);

	if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id,
		    s_total_bytes[recv_vc_ptr->smp.local_nodes] +
		    sizeof(int));

	    DEBUG_PRINT("total in %d, total out %d\n",
		    SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id),
		    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));

	    s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
	}
	received_bytes = 0;
	if (iov_off == iovlen) {
	    /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
	    goto fn_exit;
	}
    }

    WRITEBAR();
    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) !=
	    SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id)) {
	/****** received the next smp packet **************/
	READBAR();
	s_current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *) ((g_smpi_shmem->pool) +
		    SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
			g_smpi.my_local_id));
	WRITEBAR();
	s_total_bytes[recv_vc_ptr->smp.local_nodes] =
	    *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
	s_current_bytes[recv_vc_ptr->smp.local_nodes] =
	    s_total_bytes[recv_vc_ptr->smp.local_nodes];
	READBAR();
	s_current_ptr[recv_vc_ptr->smp.local_nodes] =
	    (void *) ((unsigned long)
		    s_current_ptr[recv_vc_ptr->smp.local_nodes] +
		    sizeof(int));

	/****** starting to fill the iov buffers *********/
	for (;
		iov_off < iovlen
		&& s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
	    if (s_current_bytes[recv_vc_ptr->smp.local_nodes] >=
		    iov[iov_off].MPID_IOV_LEN - buf_off) {
		WRITEBAR();
		memcpy((void *) ((unsigned long) iov[iov_off].
			    MPID_IOV_BUF + buf_off),
			(void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
			iov[iov_off].MPID_IOV_LEN - buf_off);
		s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
		    (iov[iov_off].MPID_IOV_LEN - buf_off);
		READBAR();
		s_current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    s_current_ptr[recv_vc_ptr->smp.local_nodes] +
			    (iov[iov_off].MPID_IOV_LEN - buf_off));
		received_bytes += (iov[iov_off].MPID_IOV_LEN - buf_off);
		++iov_off;
		buf_off = 0;
	    } else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
		WRITEBAR();
		memcpy((void *) ((unsigned long) iov[iov_off].
			    MPID_IOV_BUF + buf_off),
			(void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
			s_current_bytes[recv_vc_ptr->smp.local_nodes]);
		READBAR();
		s_current_ptr[recv_vc_ptr->smp.local_nodes] =
		    (void *) ((unsigned long)
			    s_current_ptr[recv_vc_ptr->smp.local_nodes] +
			    s_current_bytes[recv_vc_ptr->smp.local_nodes]);
		received_bytes +=
		    s_current_bytes[recv_vc_ptr->smp.local_nodes];
		buf_off += s_current_bytes[recv_vc_ptr->smp.local_nodes];
		s_current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
	    }
	}
	*num_bytes_ptr += received_bytes;
	if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
	    READBAR();
	    s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
	    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
		    g_smpi.my_local_id,
		    s_total_bytes[recv_vc_ptr->smp.local_nodes] +
		    sizeof(int));
	    s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
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
int MPIDI_CH3I_SMP_pull_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t** pkt_head)
{
    if (s_current_bytes[vc->smp.local_nodes] != 0)
    {
        MPIU_Error_printf(
            "current bytes %d, total bytes %d, remote id %d\n",
            s_current_bytes[vc->smp.local_nodes],
            s_total_bytes[vc->smp.local_nodes],
            vc->smp.local_nodes
        );
        MPIU_Assert(s_current_bytes[vc->smp.local_nodes] == 0);
    }

    if (s_total_bytes[vc->smp.local_nodes] != 0)
    {
        READBAR();
        s_current_ptr[vc->smp.local_nodes] = NULL;
        smpi_complete_recv(
            vc->smp.local_nodes,
            g_smpi.my_local_id,
            s_total_bytes[vc->smp.local_nodes] + sizeof(int)
        );
        s_total_bytes[vc->smp.local_nodes] = 0;
        s_current_bytes[vc->smp.local_nodes] = 0;
    }

    WRITEBAR();

    if (SMPI_TOTALIN(vc->smp.local_nodes, g_smpi.my_local_id) !=
        SMPI_TOTALOUT(vc->smp.local_nodes, g_smpi.my_local_id))
    {
        DEBUG_PRINT(
            "remote %d, local %d, total in %d, total out %d\n",
            vc->smp.local_nodes,
            g_smpi.my_local_id,
            SMPI_TOTALIN(vc->smp.local_nodes, g_smpi.my_local_id),
            SMPI_TOTALOUT(vc->smp.local_nodes, g_smpi.my_local_id)
        );

        READBAR();
        s_current_ptr[vc->smp.local_nodes] = (void*)(g_smpi_shmem->pool +
            SMPI_CURRENT(vc->smp.local_nodes, g_smpi.my_local_id));
        WRITEBAR();
        s_total_bytes[vc->smp.local_nodes] = *((int*) s_current_ptr[vc->smp.local_nodes]);
        WRITEBAR();
        *pkt_head = (void*) ((unsigned long) s_current_ptr[vc->smp.local_nodes] + sizeof(int));
        DEBUG_PRINT(
            "bytes arrived %d, head type %d, headersize %d\n",
            s_total_bytes[vc->smp.local_nodes],
            ((MPIDI_CH3_Pkt_t*) *pkt_head)->type,
            MPIDI_CH3U_PKT_SIZE(*pkt_head)
        );
        s_current_bytes[vc->smp.local_nodes] = s_total_bytes[vc->smp.local_nodes] - MPIDI_CH3U_PKT_SIZE(*pkt_head);
        READBAR();
        s_current_ptr[vc->smp.local_nodes] = (void*)(
            (unsigned long) s_current_ptr[vc->smp.local_nodes]
            + sizeof(int)
            + MPIDI_CH3U_PKT_SIZE(*pkt_head)
        );
        DEBUG_PRINT("current bytes %d\n", s_current_bytes[vc->smp.local_nodes]);
    }
    else
    {
        *pkt_head = NULL;
    }

    return MPI_SUCCESS;
}

#if defined(USE_PROCESSOR_AFFINITY)

#if defined(_SMP_HWLOC_)

/*
 * Find next processor in obj_tree, history stored in obj_tree.
 * Yields "scatter" affinity scenario in core_mapping.
 */

void map_scatter(hwloc_obj_t obj, int depth)
{
    int i = depth * num_cpus + obj->logical_index;

    if(obj->type == HWLOC_OBJ_PROC) {                                    /* found a processor */
      core_mapping[ip++] = obj->os_index;
    } else if((obj_tree[i] == -1) || (obj_tree[i] == obj->arity - 1)) {  /* init tree or restart */
      obj_tree[i] = 0;
      map_scatter(obj->children[0], depth + 1);
    } else {                                                             /* next child */
      obj_tree[i]++;
      map_scatter(obj->children[obj_tree[i]], depth + 1);    
    }
return; 
}

int get_cpu_mapping_hwloc(long N_CPUs_online)
{
    hwloc_topology_t topology = NULL;
    hwloc_obj_t      sysobj;
    unsigned         topodepth = -1, depth = -1;
    int              num_sockets = 0, cpu_model = 0, rc = 0, i;
    char             line[MAX_LINE_LENGTH], input[MAX_NAME_LENGTH], *s, *key;
    FILE            *fp;
    cpu_type_t       cpu_type = CPU_FAMILY_NONE;
    
    /* Init topology object */
    if(hwloc_topology_init(&topology) != 0) {
        fprintf(stderr, "Warning: %s: Failed to init CPU topology.\n", __func__);
        goto error_free;
    }

    /*
    * Load topology object.
    * If socket count is wanted, the full topology has to be loaded.
    * If socket count is not wanted, remove the comment below.
    */
    
    /* hwloc_topology_ignore_all_keep_structure(topology); */
    if(hwloc_topology_load(topology) != 0) {
      fprintf(stderr, "Warning: %s: Failed to detect topology.\n", __func__);
      goto error_free;
    }

    /* Determine topology depth */
    topodepth = hwloc_topology_get_depth(topology);
    if(topodepth == HWLOC_TYPE_DEPTH_UNKNOWN) {
      fprintf(stderr, "Warning: %s: Failed to determine topology depth.\n", __func__);
      goto error_free;
    }

    /* Count number of (logical) processors */
    depth = hwloc_get_type_depth(topology, HWLOC_OBJ_PROC);
    if(depth == HWLOC_TYPE_DEPTH_UNKNOWN) {
      fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__);
      goto error_free;
    }
    if(! (num_cpus = hwloc_get_nbobjs_by_depth(topology, depth))) {
      fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__);
      goto error_free;
    }

    /* Count number of sockets */
    depth = hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET);
    if(depth == HWLOC_TYPE_DEPTH_UNKNOWN) {
      fprintf(stderr, "Warning: %s: Failed to determine number of sockets.\n", __func__);
    } else {
      num_sockets = hwloc_get_nbobjs_by_depth(topology, depth);
    }

    /* Init maps */
    if(! (custom_cpu_mapping = (char *) MPIU_Malloc(sizeof(char) * num_cpus*2))) { 
      goto error_free;
    } 
    memset(custom_cpu_mapping, 0, sizeof(char) * num_cpus*2);
    if(! (core_mapping = (int *) MPIU_Malloc(num_cpus * sizeof(*core_mapping)))) { 
      goto error_free;
    } 
    for(i = 0; i < num_cpus; i++) { 
      core_mapping[i] = -1;
    } 
    if(! (obj_tree = (int *) MPIU_Malloc(num_cpus * topodepth * sizeof(*obj_tree)))) { 
      goto error_free;
    } 
    for(i = 0; i < num_cpus * topodepth; i++) { 
      obj_tree[i] = -1;
    } 
    /* Scatter */
    ip = 0;
    sysobj = hwloc_get_system_obj(topology);
    for(i = 0; i < num_cpus; i++) { 
      map_scatter(sysobj, 0);
    } 

    /* Assemble custom_cpu_mapping string */
    s = custom_cpu_mapping;
    for(i = 0; i < num_cpus; i++) {
       sprintf(s, "%d:", core_mapping[i]);
       s = custom_cpu_mapping + strlen(custom_cpu_mapping);
    }
    i = strlen(custom_cpu_mapping);
    if(i) { 
       custom_cpu_mapping[i-1] = '\0';
    }

    printf("custom_cpu_mapping = %s \n",custom_cpu_mapping);

    /* Parse /proc/cpuinfo for additional useful things */
    if(fp = fopen(CONFIG_FILE, "r")) {

      while(! feof(fp)) {
        memset(line, 0, MAX_LINE_LENGTH);
        fgets(line, MAX_LINE_LENGTH - 1, fp);

        if(! (key = strtok(line, "\t:"))) { 
          continue;
        } 

        if(cpu_type == CPU_FAMILY_NONE) {
          if(! strcmp(key, "vendor_id")) {
            strtok(NULL, " ");
            s = strtok(NULL, " ");
            if (! strcmp(s, "AuthenticAMD")) { 
               cpu_type = CPU_FAMILY_AMD;
            } else { 
               cpu_type = CPU_FAMILY_INTEL;
            } 
            continue;
          }
        }

        if(! cpu_model) {
          if(! strcmp(key, "model")) {
             strtok(NULL, " ");
             s = strtok(NULL, " ");
             sscanf(s, "%d", &cpu_model);
             continue;
          }
        }
      }

      fclose(fp);

      /* Very crude guesses, should be extended to AMD and other Intel CPUs, if necessary */

      if(cpu_type == CPU_FAMILY_INTEL) {
         if(num_sockets == 2) {
              if(num_cpus == 4) {  
                    arch_type = MULTI_CORE_ARCH_XEON_DUAL;
              }
              if(num_cpus == 8) {
                  if(cpu_model == CLOVERTOWN_MODEL) { 
                      arch_type = MULTI_CORE_ARCH_CLOVERTOWN;
                  }
                  if(cpu_model == HARPERTOWN_MODEL) {
                      arch_type = MULTI_CORE_ARCH_HARPERTOWN;
                  }
                  if(cpu_model == NEHALEM_MODEL) { 
                      arch_type = MULTI_CORE_ARCH_NEHALEM;
                  }
              }
              if(num_cpus == 16) {
                  if(cpu_model == NEHALEM_MODEL) {  /* nehalem with smt on */
                      arch_type = MULTI_CORE_ARCH_NEHALEM;
                   }
              }	
         }
      }
    } else {
             fprintf(stderr, "Warning: %s: Failed to open \"%s\".\n", __func__, CONFIG_FILE);
    }

    /* Done */
    rc = MPI_SUCCESS;

    error_free:

    if(obj_tree)     MPIU_Free(obj_tree);
    if(core_mapping) MPIU_Free(core_mapping);
    if(topology)     hwloc_topology_destroy(topology);
    
    MPIU_DBG_MSG_FMT(OTHER,TYPICAL,(MPIU_DBG_FDEST,"num_cpus=%d, num_sockets=%d, custom_cpu_mapping=\"%s\"",
                   num_cpus, num_sockets, custom_cpu_mapping));

return rc;
}

#endif /* _SMP_HWLOC_ */

int get_cpu_mapping(long N_CPUs_online)
{
    char line[MAX_LINE_LENGTH];
    char input[MAX_NAME_LENGTH];
    char bogus1[MAX_NAME_LENGTH];
    char bogus2[MAX_NAME_LENGTH];
    char bogus3[MAX_NAME_LENGTH];
    int physical_id; //return value
    int core_mapping[num_cpus];
    int core_index = 0;
    cpu_type_t cpu_type;
    int i;
    int model;
    int vendor_set=0, model_set=0;
    int mpi_errno = MPI_SUCCESS;

    FILE* fp=fopen(CONFIG_FILE,"r");
    if (fp == NULL){
        printf("can not open cpuinfo file \n");
        return 0;
    }
  
    memset(core_mapping, 0, sizeof(core_mapping));
    custom_cpu_mapping = (char *) MPIU_Malloc(sizeof(char)*N_CPUs_online*2);
    if(custom_cpu_mapping == NULL) { 
          return 0;
    } 
    memset(custom_cpu_mapping, 0, sizeof(char)*N_CPUs_online*2);

    while(!feof(fp)){
        memset(line,0,MAX_LINE_LENGTH);
        fgets(line, MAX_LINE_LENGTH, fp);

        memset(input, 0, MAX_NAME_LENGTH);
        sscanf(line, "%s", input);

        if (!vendor_set) {
            if (strcmp(input, "vendor_id") == 0) {
              memset(input, 0, MAX_NAME_LENGTH);
              sscanf(line,"%s%s%s",bogus1, bogus2, input);

              if (strcmp(input, "AuthenticAMD") == 0) {
                cpu_type = CPU_FAMILY_AMD;
              } else {
                cpu_type = CPU_FAMILY_INTEL;
              }
          vendor_set = 1;
            }
        }

    if (!model_set){
            if (strcmp(input, "model") == 0) {
            sscanf(line, "%s%s%d", bogus1, bogus2, &model);
        model_set = 1;
        }
    }

    if (strcmp(input, "physical") == 0) {
            sscanf(line, "%s%s%s%d", bogus1, bogus2, bogus3, &physical_id);
            core_mapping[core_index++] = physical_id;
        }
    }

    num_cpus = core_index;
    if (num_cpus == 4) {
       if((memcmp(INTEL_XEON_DUAL_MAPPING,core_mapping, sizeof(int)*num_cpus) == 0) 
            && (cpu_type==CPU_FAMILY_INTEL)){ 
               arch_type =  MULTI_CORE_ARCH_XEON_DUAL;
               strcpy(custom_cpu_mapping , "0:2:1:3");
       } else if((memcmp(AMD_OPTERON_DUAL_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0)
            && (cpu_type==CPU_FAMILY_AMD)){ 
               arch_type =  MULTI_CORE_ARCH_OPTERON_DUAL;
               strcpy(custom_cpu_mapping , "0:1:2:3");
       }
    } else if (num_cpus == 8) {
        if(cpu_type == CPU_FAMILY_INTEL) {
     	   if(model == CLOVERTOWN_MODEL) {
	            arch_type = MULTI_CORE_ARCH_CLOVERTOWN;
    	        if(memcmp(INTEL_CLOVERTOWN_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
     	        strcpy(custom_cpu_mapping,"0:1:4:5:2:3:6:7");
                } 
	        }
        	else if(model == HARPERTOWN_MODEL) {
	            arch_type = MULTI_CORE_ARCH_HARPERTOWN;
      	        if(memcmp(INTEL_HARPERTOWN_LEG_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
         	        strcpy(custom_cpu_mapping,"0:1:4:5:2:3:6:7");
                } 
            	else if(memcmp(INTEL_HARPERTOWN_COM_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
             	    strcpy(custom_cpu_mapping,"0:4:2:6:1:5:3:7");
                } 
	        }
         	else if(model == NEHALEM_MODEL) {
	            arch_type = MULTI_CORE_ARCH_NEHALEM;
     	        if(memcmp(INTEL_NEHALEM_LEG_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
	                strcpy(custom_cpu_mapping, "0:2:4:6:1:3:5:7");
                }
             	else if(memcmp(INTEL_NEHALEM_COM_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
             	    strcpy(custom_cpu_mapping, "0:4:1:5:2:6:3:7");
                }
	        }
        }
    } else if (num_cpus == 16) {
        if(cpu_type == CPU_FAMILY_INTEL) {
	        if(model == NEHALEM_MODEL) {
      	        arch_type = MULTI_CORE_ARCH_NEHALEM;
            	if(memcmp(INTEL_NEHALEM_LEG_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
             	    strcpy(custom_cpu_mapping,"0:2:4:6:1:3:5:7:8:10:12:14:9:11:13:15");
                }
            	else if(memcmp(INTEL_NEHALEM_COM_MAPPING,core_mapping,sizeof(int)*num_cpus) == 0) { 
             	    strcpy(custom_cpu_mapping, "0:4:1:5:2:6:3:7:8:12:9:13:10:14:11:15");
                }
      	     }
         }
         else if(cpu_type == CPU_FAMILY_AMD) {
         	if(memcmp(AMD_BARCELONA_MAPPING,core_mapping, sizeof(int)*num_cpus) == 0) {
       	        arch_type = MULTI_CORE_ARCH_BARCELONA;
        	    strcpy(custom_cpu_mapping, "0:1:2:3:4:5:6:7:8:9:10:11:12:13:14:15");
	        }
         }
    }

    fclose(fp);

    MPIU_DBG_MSG_FMT(OTHER,TYPICAL,(MPIU_DBG_FDEST,"num_cpus=%d, custom_cpu_mapping=\"%s\"", num_cpus, custom_cpu_mapping));

return MPI_SUCCESS;
}

static void smpi_setaffinity ()
{
    int mpi_errno = MPI_SUCCESS;
    PLPA_NAME(api_type_t) plpa_ret = PLPA_NAME_CAPS(PROBE_UNSET);

    if (PLPA_NAME(api_probe)(&plpa_ret) && plpa_ret == PLPA_NAME_CAPS(PROBE_OK))
    {
        viadev_enable_affinity = 0;
        DEBUG_PRINT("Processor affinity is not supported on this platform\n");
    }

    if (viadev_enable_affinity > 0)
    {
        if (s_cpu_mapping)
        {
            /* If the user has specified how to map the processes,
             * use the mapping specified by the user
            */
            char* tp = s_cpu_mapping;
            char* cp = NULL;
            int j = 0;
            int i;
            char tp_str[s_cpu_mapping_line_max + 1];
            long N_CPUs_online = sysconf(_SC_NPROCESSORS_ONLN);

            if (N_CPUs_online < 1)
            {
                MPIU_ERR_SETFATALANDJUMP2(
                    mpi_errno,
                    MPI_ERR_OTHER,
                    "**fail",
                    "%s: %s",
                    "sysconf",
                    strerror(errno)
                );
            }

            /* Call the cpu_mapping function to find out about how the
             * processors are numbered on the different sockets.
             * The hardware information gathered from this function 
             * is required to determine the best set of intra-node thresholds. 
             * However, since the user has specified a mapping pattern, 
             * we are not going to use any of our proposed binding patterns
             */
#if defined(_SMP_HWLOC_)
            if(use_hwloc_cpu_binding == 1) { 
                mpi_errno = get_cpu_mapping_hwloc(N_CPUs_online);
            } else { 
#endif
                mpi_errno = get_cpu_mapping(N_CPUs_online);
#if defined(_SMP_HWLOC_)
            }
#endif
       

            while (*tp != '\0')
            {
                i = 0;
                cp = tp;

                while (*cp != '\0' && *cp != ':' && i < s_cpu_mapping_line_max)
                {
                    ++cp;
                    ++i;
                }

                strncpy(tp_str, tp, i);
                tp_str[i] = '\0';

                if (j == g_smpi.my_local_id)
                {
                    PLPA_NAME(setaffinity)(tp_str, getpid());
                    /* TODO: Evaluate return value of PLPA_NAME */
                    break;
                }

                if (*cp == '\0')
                {
                    break; 
                }

                tp = cp;
                ++tp;
                ++j;
            }

            MPIU_Free(s_cpu_mapping);
        }
        else
        {
            /* The user has not specified how to map the processes,
             * use the data available in /proc/cpuinfo file to decide 
             * on the best cpu mapping pattern
             */
            long N_CPUs_online = sysconf(_SC_NPROCESSORS_ONLN);

            if (N_CPUs_online < 1)
            {
                MPIU_ERR_SETFATALANDJUMP2(
                    mpi_errno,
                    MPI_ERR_OTHER,
                    "**fail",
                    "%s: %s",
                    "sysconf",
                    strerror(errno)
                );
            }

            /* Call the cpu_mapping function to find out about how the
             * processors are numbered on the different sockets. 
             */
#if defined(_SMP_HWLOC_)
            if(use_hwloc_cpu_binding == 1) { 
                mpi_errno = get_cpu_mapping_hwloc(N_CPUs_online);
            } else { 
#endif
                mpi_errno = get_cpu_mapping(N_CPUs_online);
#if defined(_SMP_HWLOC_)
            } 
#endif
            
            if(mpi_errno != MPI_SUCCESS || use_optimal_cpu_binding == 0 
                   || arch_type == 0) { 
                /* For some reason, we were not able to retrieve the cpu mapping
                * information. We are falling back on the linear mapping. 
                * This may not deliver the best performace 
                */
                PLPA_NAME(cpu_set_t) cpuset;
                PLPA_CPU_ZERO(&cpuset);
                PLPA_CPU_SET(g_smpi.my_local_id % N_CPUs_online, &cpuset);

                if (PLPA_NAME(sched_setaffinity) (0, sizeof(cpuset), &cpuset))
                {
                    MPIU_Error_printf("sched_setaffinity: %s\n", strerror(errno));
                }
            } 
            else { 
             /* We have all the information that we need. We will bind the processes
              * to the cpu's now
              */
                int linelen = strlen(custom_cpu_mapping);

                if (linelen < custom_cpu_mapping_line_max)
                {
                  custom_cpu_mapping_line_max = linelen;
                }

                char* tp = custom_cpu_mapping;
                char* cp = NULL;
                int j = 0;
                int i;
                char tp_str[custom_cpu_mapping_line_max + 1];
               
                while (*tp != '\0')
                {
                    i = 0;
                    cp = tp;

                    while (*cp != '\0' && *cp != ':' && i < custom_cpu_mapping_line_max)
                    {
                        ++cp;
                        ++i;
                    }   
    
                    strncpy(tp_str, tp, i);
                    tp_str[i] = '\0';
    
                    if (j == g_smpi.my_local_id)
                    {
                        PLPA_NAME(setaffinity)(tp_str, getpid());
                        /* TODO: Evaluate return value of PLPA_NAME */
                        break;
                    }

                    if (*cp == '\0')
                    {
                        break;
                    }

                    tp = cp;
                    ++tp;
                    ++j;
                }
            } 
        }
        MPIU_Free(custom_cpu_mapping);
    }

fn_exit:
    return;

fn_fail:
    goto fn_exit;
}
#endif /* defined(USE_PROCESSOR_AFFINITY) */

static int smpi_exchange_info(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int pg_rank, pg_size;
    int hostid;

    int *hostnames_j = NULL;
    int *smpi_ptr = NULL;
    int i = 0;
    int j;

    /** variables needed for PMI exchange **/
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];

    MPIDI_VC_t* vc = NULL;

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);

    hostid = get_host_id(s_hostname, HOSTNAME_LEN);

    hostnames_j = (int *) MPIU_Malloc(pg_size * sizeof(int));

    if (hostnames_j == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
		"**nomem %s", "host names");
    }

    /** exchange address hostid using PMI interface **/
    if (pg_size > 1) {
	int need_exchange = 0;

	for (; i < pg_size; ++i) {
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

	    ++key_max_sz;
	    ++val_max_sz;

	    key = MPIU_Malloc(key_max_sz);
	    val = MPIU_Malloc(val_max_sz);

	    if (key == NULL || val == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "pmi key");
	    }

		sprintf(rdmakey, "%08d", pg_rank);
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

	    if(PMI_Barrier() != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", mpi_errno);
	    }

	    /* Here, all the key and value pairs are put, now we can get them */
	    for (i = 0; i < pg_size; ++i) {
		if (pg_rank == i) {
		    hostnames_j[i] = hostid;
		    continue;
		}

		/* generate the key */
		sprintf(rdmakey, "%08d", i);
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
    /* g_smpi.local_nodes = (unsigned int *) MPIU_Malloc(pg_size * sizeof(int)); */

    smpi_ptr = (int *) MPIU_Malloc(pg_size * sizeof(int));

    if(smpi_ptr == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
		"**nomem %s", "smpi_ptr");
    }

    g_smpi.only_one_device = 1;
    SMP_ONLY = 1;
    g_smpi.num_local_nodes = 0;
    for (j = 0; j < pg_size; ++j)
    {
        MPIDI_PG_Get_vc(pg, j, &vc);

        if (hostnames_j[pg_rank] == hostnames_j[j])
        {
            if (j == pg_rank)
            {
		g_smpi.my_local_id = g_smpi.num_local_nodes;
#if defined(USE_PROCESSOR_AFFINITY)
                if (viadev_enable_affinity)
                {
                    smpi_setaffinity();
                }
#endif /* defined(USE_PROCESSOR_AFFINITY) */
            }

	    vc->smp.local_nodes = g_smpi.num_local_nodes;
	    smpi_ptr[g_smpi.num_local_nodes] = j;
	    ++g_smpi.num_local_nodes;
	}
        else
        {
	    g_smpi.only_one_device = 0;
	    SMP_ONLY = 0;
	    vc->smp.local_nodes = -1;
	}
    }

    if (MPIDI_CH3I_Process.has_dpm)
        SMP_ONLY = 0;

    DEBUG_PRINT("num local nodes %d, my local id %d\n",
	    g_smpi.num_local_nodes, g_smpi.my_local_id);

    g_smpi.l2g_rank = (unsigned int *) MPIU_Malloc(g_smpi.num_local_nodes * sizeof(int));
    if(g_smpi.l2g_rank == NULL) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "g_smpi.12g_rank");
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
	MPIU_Error_printf(
		"malloc: in ib_rank_lid_table for SMP");
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(default, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    }

    for (j = 0; j < g_smpi.num_local_nodes; ++j) {
	g_smpi.l2g_rank[j] = smpi_ptr[j];
    }
    MPIU_Free(smpi_ptr);

    MPIU_Free(hostnames_j);

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

    if (s_sh_buf_pool.free_head == -1) 
	return NULL;

    ptr = &s_my_buffer_head[s_sh_buf_pool.free_head];
    s_sh_buf_pool.free_head = ptr->next;
    ptr->next = -1;

    MPIU_Assert (ptr->busy == 0);

    return ptr;
}

    static inline void
send_buf_reclaim ()
{
    int i, index, last_index;
    SEND_BUF_T *ptr;

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
	if (i != g_smpi.my_local_id) {
	    index = s_sh_buf_pool.send_queue[i];
	    last_index = -1;
	    ptr = NULL;
	    while (index != -1) {
		ptr = &s_my_buffer_head[index];
		if(ptr->busy == 1)
		    break;
		last_index = index;
		index = ptr->next;
	    }
	    if (last_index != -1)
		put_buf_to_pool (s_sh_buf_pool.send_queue[i], last_index);
	    s_sh_buf_pool.send_queue[i] = index;
	    if (s_sh_buf_pool.send_queue[i] == -1)
		s_sh_buf_pool.tail[i] = -1;
	}
    }
}

    static inline void
put_buf_to_pool (int head, int tail)
{
    SEND_BUF_T *ptr;

    MPIU_Assert (head != -1);
    MPIU_Assert (tail != -1);

    ptr = &s_my_buffer_head[tail];

    ptr->next = s_sh_buf_pool.free_head;
    s_sh_buf_pool.free_head = head;
}

    static inline void
link_buf_to_send_queue (int dest, int index)
{
    if (s_sh_buf_pool.send_queue[dest] == -1) {
	s_sh_buf_pool.send_queue[dest] = s_sh_buf_pool.tail[dest] = index;
	return;
    }

    s_my_buffer_head[(s_sh_buf_pool.tail[dest])].next = index;
    s_sh_buf_pool.tail[dest] = index;
}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME adjust_lu_info
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void adjust_lu_info(struct limic_user *lu, int old_len)
{
    unsigned long va = lu->va + lu->length;
    int pgcount, len = old_len - lu->length;
    int pagesize = getpagesize();

    MPIDI_STATE_DECL(MPID_STATE_ADJUST_LU_INFO);
    MPIDI_FUNC_ENTER(MPID_STATE_ADJUST_LU_INFO);

    pgcount = (va + len + pagesize - 1)/pagesize - va/pagesize;
    MPIU_Assert(pgcount);

    lu->va = va;
    lu->nr_pages = pgcount;
    lu->offset = va & (pagesize-1);
    lu->length = len;

    MPIDI_FUNC_EXIT(MPID_STATE_ADJUST_LU_INFO);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_send_limic_comp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* l_header contains send_req_id from the sender,
 * nb is the number bytes received
 */
void MPIDI_CH3I_SMP_send_limic_comp(struct limic_header *l_header,
                                    MPIDI_VC_t* vc, int nb)
{
    MPIDI_CH3_Pkt_limic_comp_t pkt;
    int pkt_sz = sizeof(MPIDI_CH3_Pkt_limic_comp_t);
    int pkt_avail;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    MPID_Request *sreq = NULL;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);

    pkt.type = MPIDI_CH3_PKT_LIMIC_COMP;
    pkt.mrail.send_req_id = l_header->send_req_id;
    pkt.nb = nb;
    
    /*make sure the complete message not sent between other unfinished message */
    if (MPIDI_CH3I_SMP_SendQ_head(vc)) {
        sreq = create_request(&pkt, pkt_sz, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        return;
    }

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);
    if (pkt_avail < pkt_sz+sizeof(int)) {
        /* queue the message */
        sreq = create_request(&pkt, pkt_sz, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        return;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
            + SMPI_NEXT(g_smpi.my_local_id,
                vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    ptr = (void *) ((unsigned long) ptr + sizeof(int));
    memcpy(ptr, &pkt, pkt_sz);

    ptr = (void *) ((unsigned long) ptr + pkt_sz);
    *((int *) ptr_head) = pkt_sz;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        pkt_sz + sizeof(int));

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);
    return;
}

#endif /* _SMP_LIMIC_ */

/* vi:set sw=4 */
