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

#include "mpidi_ch3_impl.h"
#include "mpid_mrail_rndv.h"
#include "rdma_impl.h"

#define MPIDI_CH3I_HOST_DESCRIPTION_KEY "description"

MPIDI_CH3I_Process_t MPIDI_CH3I_Process;

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Init(int has_parent, MPIDI_PG_t * pg, int pg_rank)
{
    int mpi_errno = MPI_SUCCESS;
    int pg_size, threshold, dpm = 0, p;
    char *dpm_str, *value, *conn_info = NULL;
    MPIDI_VC_t *vc;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_INIT);
 
    if (MPIDI_CH3_Pkt_size_index[MPIDI_CH3_PKT_CLOSE] != sizeof (MPIDI_CH3_Pkt_close_t))
    {
        MPIU_ERR_SETFATALANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "**fail %s", 
            "Failed sanity check! Packet size table mismatch");
    }
    
    pg_size = MPIDI_PG_Get_size(pg);

    /*Determine to use which connection management*/
    threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;

    /*check ON_DEMAND_THRESHOLD*/
    value = getenv("MV2_ON_DEMAND_THRESHOLD");
    if (value)
    {
        threshold = atoi(value);
    }

    dpm_str = getenv("MV2_SUPPORT_DPM");
    if (dpm_str) {
        dpm = !!atoi(dpm_str);
    }
    MPIDI_CH3I_Process.has_dpm = dpm;
    if(MPIDI_CH3I_Process.has_dpm) {
        setenv("MV2_ENABLE_AFFINITY", "0", 1);
    }

#ifdef _ENABLE_XRC_
    value = getenv ("MV2_USE_XRC");
    if (value) {
        USE_XRC = atoi(value);
        if (USE_XRC) {
            /* Enable on-demand */
            threshold = 1;
        }
    }
#endif /* _ENABLE_XRC_ */

    if (pg_size > threshold || dpm 
#ifdef _ENABLE_XRC_
            || USE_XRC
#endif /* _ENABLE_XRC_ */
            )
    {
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
	MPIDI_CH3I_Process.num_conn = 0;

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
        && atoi(value) && ! dpm)
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
	mpi_errno = MPIDI_CH3I_CM_Init(pg, pg_rank, &conn_info);
	if (mpi_errno != MPI_SUCCESS)
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

        /* All vc should be connected */
        for (p = 0; p < pg_size; ++p)
        {
            MPIDI_PG_Get_vc(pg, p, &vc);
            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
        }
        break;
    }

    /* set connection info for dynamic process management */
    if (conn_info) {
        MPIDI_PG_SetConnInfo(pg_rank, (const char *)conn_info);
        MPIU_Free(conn_info);
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
	    /* Mark the SMP VC as Idle */
	    if (vc->smp.local_nodes >= 0)
            {
                vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
#ifdef _ENABLE_XRC_
                VC_XST_SET (vc, XF_SMP_VC);
#endif
            }
        }
    } else {
        extern int enable_shmem_collectives;
        enable_shmem_collectives = SMP_INIT;
    }

    /* Set the eager max msg size now that we know SMP and RDMA are initialized.
     * The max message size is also set during VC initialization, but the state
     * of SMP is unknown at that time.
     */
    for (p = 0; p < pg_size; ++p)
    {
        MPIDI_PG_Get_vc(pg, p, &vc);
        vc->eager_max_msg_sz = MPIDI_CH3_EAGER_MAX_MSG_SIZE(vc);
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

    vc->smp.sendq_head = NULL; 
    vc->smp.sendq_tail = NULL; 
    vc->smp.recv_active = NULL; 
    vc->smp.send_active = NULL; 
    vc->smp.local_nodes = -1;
#ifdef _ENABLE_XRC_
    vc->mrail.rails = NULL;
    vc->mrail.srp.credits = NULL;
#endif

    vc->ch.sendq_head = NULL; 
    vc->ch.sendq_tail = NULL; 
    vc->ch.req = (MPID_Request *) MPIU_Malloc(sizeof(MPID_Request));
    if (!vc->ch.req)
    {
        MPIU_CHKMEM_SETERR(mpi_errno, sizeof(MPID_Request), "MPID Request");
    }
    /* vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;*/
    vc->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;
    vc->ch.read_state = MPIDI_CH3I_READ_STATE_IDLE;
    vc->ch.recv_active = NULL; 
    vc->ch.send_active = NULL; 
    vc->ch.cm_sendq_head = NULL;
    vc->ch.cm_sendq_tail = NULL;
    vc->ch.cm_1sc_sendq_head = NULL;
    vc->ch.cm_1sc_sendq_tail = NULL;
#ifdef _ENABLE_XRC_
    vc->ch.xrc_flags = 0;
    vc->ch.xrc_conn_queue = NULL;
    vc->ch.orig_vc = NULL;
    memset (vc->ch.xrc_srqn, 0, sizeof (uint32_t) * MAX_NUM_HCAS);
    memset (vc->ch.xrc_rqpn, 0, sizeof (uint32_t) * MAX_NUM_SUBRAILS);
    memset (vc->ch.xrc_my_rqpn, 0, sizeof (uint32_t) * MAX_NUM_SUBRAILS);
#endif

    vc->smp.hostid = -1;
    vc->force_rndv = 0;

    vc->rndvSend_fn = MPID_MRAIL_RndvSend;
    vc->rndvRecv_fn = MPID_MRAIL_RndvRecv;

#if defined(CKPT)
    vc->ch.rput_stop = 0;
#endif /* defined(CKPT) */

#ifdef USE_RDMA_UNEX
    vc->ch.unex_finished_next = NULL; 
    vc->ch.unex_list = NULL; 
#endif
    /* It is needed for temp vc */
    vc->eager_max_msg_sz = rdma_iba_eager_threshold;

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

    if (!MPIDI_CH3I_Process.has_dpm) {
        portFns->OpenPort    = 0;    
        portFns->ClosePort   = 0;
        portFns->CommAccept  = 0;
        portFns->CommConnect = 0;
    } else 
        MPIU_UNREFERENCED_ARG(portFns);

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
    int mpi_errno = MPI_SUCCESS;
    int str_errno;
    char ifname[MAX_HOST_DESCRIPTION_LEN];
    MPIDI_VC_t *vc;
    MPIDI_CH3_Pkt_cm_establish_t pkt;
    MPID_Request * sreq;
    int seqnum;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);

    *new_vc = NULL;
    if (!MPIDI_CH3I_Process.has_dpm)
        return MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, 
                                    __LINE__, MPI_ERR_OTHER, "**notimpl", 0);

    str_errno = MPIU_Str_get_string_arg(port_name, 
                                        MPIDI_CH3I_HOST_DESCRIPTION_KEY, 
                                        ifname, MAX_HOST_DESCRIPTION_LEN);
    if (str_errno != MPIU_STR_SUCCESS) {
        /* --BEGIN ERROR HANDLING */
        if (str_errno == MPIU_STR_FAIL) {
            MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**argstr_missinghost");
        }
        else {
            /* MPIU_STR_TRUNCATED or MPIU_STR_NONEM */
            MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**argstr_hostd");
        }
        /* --END ERROR HANDLING-- */
    }

    vc = MPIU_Malloc(sizeof(MPIDI_VC_t));
    if (!vc) {
        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**nomem");
    }
    MPIDI_VC_Init(vc, NULL, 0);

    mpi_errno = MPIDI_CH3I_CM_Connect_raw_vc(vc, ifname);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    
    while (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
        mpi_errno = MPID_Progress_test();
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    /* fprintf(stderr, "[###] vc state to idel, now send cm_establish msg\n") */
    /* Now a connection is created, send a cm_establish message */
    /* FIXME: vc->mrail.remote_vc_addr is used to find remote vc
     * A more elegant way is needed */
    MPIDI_Pkt_init(&pkt, MPIDI_CH3_PKT_CM_ESTABLISH);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(&pkt, seqnum);
    pkt.vc_addr = vc->mrail.remote_vc_addr;
    mpi_errno = MPIDI_GetTagFromPort(port_name, &pkt.port_name_tag);
    if (mpi_errno != MPIU_STR_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**argstr_port_name_tag");
    }

    mpi_errno = MPIDI_CH3_iStartMsg(vc, &pkt, sizeof(pkt), &sreq);
    if (mpi_errno != MPI_SUCCESS)
    {
        MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**fail", "**fail %s",
                             "Failed to send cm establish message");
    }

    if (sreq != NULL)
    {
        if (sreq->status.MPI_ERROR != MPI_SUCCESS)
        {
            mpi_errno = MPIR_Err_create_code(sreq->status.MPI_ERROR,
                                        MPIR_ERR_FATAL, FCNAME, __LINE__, 
                                        MPI_ERR_OTHER,
                                        "**fail", 0);
            MPID_Request_release(sreq);
            goto fn_fail;
        }
        MPID_Request_release(sreq);
    }

    *new_vc = vc;

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_CONNECT_TO_ROOT);

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Get_business_card
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Get_business_card(int myRank, char *value, int length)
{
    char ifname[MAX_HOST_DESCRIPTION_LEN];
    int mpi_errno;

    mpi_errno = MPIDI_CH3I_CM_Get_port_info(ifname, MAX_HOST_DESCRIPTION_LEN);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

    mpi_errno = MPIU_Str_add_string_arg(&value, &length,
                                        MPIDI_CH3I_HOST_DESCRIPTION_KEY,
                                        ifname);
    if (mpi_errno != MPIU_STR_SUCCESS) {
        if (mpi_errno == MPIU_STR_NOMEM) {
            MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**buscard_len");
        }
        else {
            MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**buscard");
        }
    }

fn_fail:
    return mpi_errno;
}

/* This routine is a hook for initializing information for a process
   group before the MPIDI_CH3_VC_Init routine is called */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PG_Init(MPIDI_PG_t* pg)
{
     return MPIDI_CH3I_MRAIL_PG_Init(pg);
}

/* This routine is a hook for any operations that need to be performed before
   freeing a process group */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PG_Destroy(struct MPIDI_PG* pg)
{
    return MPIDI_CH3I_MRAIL_PG_Destroy(pg);
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
