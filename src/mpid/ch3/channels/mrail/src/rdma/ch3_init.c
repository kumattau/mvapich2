/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#include "mpidi_ch3_impl.h"
#include "mpid_mrail_rndv.h"

MPIDI_CH3I_Process_t MPIDI_CH3I_Process;

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Init(int has_parent, MPIDI_PG_t * pg, int pg_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_INIT);
    int mpi_errno = MPI_SUCCESS; 
 
    if (MPIDI_CH3_Pkt_size_index[MPIDI_CH3_PKT_CLOSE] != sizeof (MPIDI_CH3_Pkt_close_t))
    {
        MPIU_ERR_SETFATALANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "**fail %s", 
            "Failed sanity check! Packet size table mismatch");
    }
    
    int pg_size = MPIDI_PG_Get_size(pg);

    /*Determine to use which connection management*/
    int threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;

    /*check ON_DEMAND_THRESHOLD*/
    char* value = getenv("MV2_ON_DEMAND_THRESHOLD");

    if (value != NULL)
    {
        threshold = atoi(value);
    }

    if (pg_size > threshold)
    {
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;

#if defined(DISABLE_PTMALLOC) && !defined(SOLARIS)
        MPIU_Error_printf("Error: On-demand connection management does "
            "not work without registration caching.\nPlease ensure "
            "this is enabled when configuring and compiling MVAPICH2 "
            "or set MV2_ON_DEMAND_THRESHOLD to a value greater than the "
            "number of processes to use all-to-all connections.\n");
        MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**fail");
#endif /* defined (DISABLE_PTMALLOC) && !defined(SOLARIS) */ 
    }
    else
    {
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_BASIC_ALL2ALL;
    }

#if defined(RDMA_CM)
    if (((value = getenv("MV2_USE_RDMA_CM")) != NULL
        || (value = getenv("MV2_USE_IWARP_MODE")) != NULL)
        && atoi(value))
    {
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_RDMA_CM;
    }
#endif /* defined(RDMA_CM) */

    MPIDI_PG_GetConnKVSname(&pg->ch.kvs_name);

#if defined(CKPT)
#if defined(RDMA_CM)
    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_RDMA_CM)
    {
        MPIU_Error_printf("Error: Checkpointing does not work with RDMA CM.\n"
            "Please configure and compile MVAPICH2 with checkpointing disabled "
            "or without support for RDMA CM.\n");
	MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**fail");
    }
#endif /* defined(RDMA_CM) */

#if defined(DISABLE_PTMALLOC)
    MPIU_Error_printf("Error: Checkpointing does not work without registration "
        "caching enabled.\nPlease configure and compile MVAPICH2 without checkpointing "
        " or enable registration caching.\n");
    MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**fail");
#endif /* defined(DISABLE_PTMALLOC) */

    MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;

    if ((mpi_errno = MPIDI_CH3I_CR_Init(pg, pg_rank, pg_size)))
    {
        MPIU_ERR_POP(mpi_errno);
    }
#endif /* defined(CKPT) */

    MPIDI_VC_t* vc = NULL;
    int p = 0;

    /* Initialize the VC table associated with this process
       group (and thus COMM_WORLD) */
    for (; p < pg_size; ++p)
    {
	MPIDI_PG_Get_vc(pg, p, &vc);
	vc->ch.sendq_head = NULL;
	vc->ch.sendq_tail = NULL;

	if ((vc->ch.req = (MPID_Request*) MPIU_Malloc(sizeof(MPID_Request))) == NULL)
        {
            MPIU_CHKMEM_SETERR(mpi_errno, sizeof(MPID_Request), "MPID Request");
        }

	vc->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;
	vc->ch.read_state = MPIDI_CH3I_READ_STATE_IDLE;
	vc->ch.recv_active = NULL;
	vc->ch.send_active = NULL;
	vc->ch.cm_sendq_head = NULL;
	vc->ch.cm_sendq_tail = NULL;
#if defined(USE_RDMA_UNEX)
	vc->ch.unex_finished_next = NULL;
	vc->ch.unex_list = NULL;
#endif /* defined(USE_RDMA_UNEX) */
	vc->smp.hostid = -1;
#if defined(CKPT)
	vc->ch.rput_stop = 0;
#endif /* defined(CKPT) */
    }

    /* override rendezvous functions */
    vc->rndvSend_fn = MPID_MRAIL_RndvSend;
    vc->rndvRecv_fn = MPID_MRAIL_RndvRecv;

    /* save my vc_ptr for easy access */
    MPIDI_PG_Get_vc(pg, pg_rank, &MPIDI_CH3I_Process.vc);

    /* Initialize Progress Engine */
    if ((mpi_errno = MPIDI_CH3I_Progress_init()))
    {
        MPIU_ERR_POP(mpi_errno);
    }

    switch (MPIDI_CH3I_Process.cm_type)
    {
    /* allocate rmda memory and set up the queues */
    case MPIDI_CH3I_CM_ON_DEMAND:
#if defined(RDMA_CM)
    case MPIDI_CH3I_CM_RDMA_CM:
#endif /* defined(RDMA_CM) */
	    if ((mpi_errno = MPIDI_CH3I_CM_Init(pg, pg_rank)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        break;
    default:
            /*call old init to setup all connections*/
            if ((mpi_errno = MPIDI_CH3I_RDMA_init(pg, pg_rank)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }

            for (p = 0; p < pg_size; ++p)
            {
                MPIDI_PG_Get_vc(pg, p, &vc);
                vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
            }
        break;
    }

    /* Initialize the smp channel */
    if ((mpi_errno = MPIDI_CH3I_SMP_init(pg)))
    {
        MPIU_ERR_POP(mpi_errno);
    }

    if (SMP_INIT)
    {
        for (p = 0; p < pg_size; ++p)
        {
            MPIDI_PG_Get_vc(pg, p, &vc);
            vc->smp.sendq_head = NULL;
            vc->smp.sendq_tail = NULL;
            vc->smp.recv_active = NULL;
            vc->smp.send_active = NULL;

	    /* Mark the SMP VC as Idle */
	    if (vc->smp.local_nodes >= 0)
            {
                vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
            }
        }
    }

    /* Set the eager max msg size now that we know SMP and RDMA are initialized.
     * The max message size is also set during VC initialization, but the state
     * of SMP is unknown at that time.
     */
    for (p = 0; p < pg_size; ++p)
    {
        MPIDI_PG_Get_vc(pg, p, &vc);
        vc->eager_max_msg_sz = MPIDI_CH3_EAGER_MAX_MSG_SIZE(vc);
        vc->force_rndv = 0;
    }
    
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_VC_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_VC_Init (MPIDI_VC_t* vc)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_VC_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_VC_INIT);
    int mpi_errno = MPI_SUCCESS;

    if (SMP_INIT) {
        vc->smp.sendq_head = NULL; 
        vc->smp.sendq_tail = NULL; 
        vc->smp.recv_active = NULL; 
        vc->smp.send_active = NULL; 
    }

    vc->ch.sendq_head = NULL; 
    vc->ch.sendq_tail = NULL; 

    if ((vc->ch.req = (MPID_Request *) MPIU_Malloc(sizeof(MPID_Request))) == NULL)
    {
        MPIU_CHKMEM_SETERR(mpi_errno, sizeof(MPID_Request), "MPID Request");
    }

    vc->ch.read_state = MPIDI_CH3I_READ_STATE_IDLE;
    vc->ch.recv_active = NULL; 
    vc->ch.send_active = NULL; 
#ifdef USE_RDMA_UNEX
    vc->ch.unex_finished_next = NULL; 
    vc->ch.unex_list = NULL; 
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_VC_INIT);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PortFnsInit
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PortFnsInit (MPIDI_PortFns* portFns)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PORTFNSINIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PORTFNSINIT);

    portFns->OpenPort    = 0;    
    portFns->ClosePort   = 0;
    portFns->CommAccept  = 0;
    portFns->CommConnect = 0;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PORTFNSINIT);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_RMAFnsInit
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_RMAFnsInit(MPIDI_RMAFns* RMAFns)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_RMAFNSINIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_RMAFNSINIT);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_RMAFNSINIT);
    return MPI_SUCCESS;
} 

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connect_to_root
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Connect_to_root(const char* port_name, MPIDI_VC_t** new_vc)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);
    return MPIR_Err_create_code(
        MPI_SUCCESS,
        MPIR_ERR_FATAL,
        FCNAME,
	__LINE__,
        MPI_ERR_OTHER,
        "**notimpl",
        0);
}

/* This routine is a hook for initializing information for a process
   group before the MPIDI_CH3_VC_Init routine is called */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PG_Init(MPIDI_PG_t* pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PG_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PG_INIT);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PG_INIT);
    return MPI_SUCCESS;
}

/* This routine is a hook for any operations that need to be performed before
   freeing a process group */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PG_Destroy(struct MPIDI_PG* pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PG_DESTROY);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PG_DESTROY);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PG_DESTROY);
    return MPI_SUCCESS;
}

/* This routine is a hook for any operations that need to be performed before
   freeing a virtual connection */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_VC_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_VC_Destroy(struct MPIDI_VC* vc)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_VC_DESTROY);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_VC_DESTROY);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_VC_DESTROY);
    return MPI_SUCCESS;
}

/* A dummy function so that all channels provide the same set of functions, 
   enabling dll channels */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_InitCompleted
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_InitCompleted(void)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_INITCOMPLETED);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_INITCOMPLETED);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_INITCOMPLETED);
    return MPI_SUCCESS;
}

/* vi: set sw=4 */
