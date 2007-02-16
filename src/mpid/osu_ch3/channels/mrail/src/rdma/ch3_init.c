/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
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



#include "mpidi_ch3_impl.h"

MPIDI_CH3I_Process_t MPIDI_CH3I_Process;

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Init(int has_parent, MPIDI_PG_t * pg, int pg_rank)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t *vc = NULL;

    int pg_size;
    int p;

    /* initialize the process group setting the rank and size */
#if 0
    mpi_errno =
        MPIDI_CH3I_RDMA_init_process_group(has_parent, &pg, &pg_rank);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER,
                                 "**process_group", 0);
        return mpi_errno;
    }
#endif
    if (MPIDI_CH3_Pkt_size_index[MPIDI_CH3_PKT_CLOSE] 
	!= sizeof (MPIDI_CH3_Pkt_close_t)) {
	fprintf(stderr, "Failed sanity check! Packet size table mismatch\n");
	return -1;	
    }
   
    pg_size = MPIDI_PG_Get_size(pg);

    {/*Determine to use which connection management*/
        char *value;
        int threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;
        
        /*check ON_DEMAND_THRESHOLD*/
	value = getenv("MV2_ON_DEMAND_THRESHOLD");
	if (NULL != value)
	    threshold = atoi(value);
	if (pg_size > threshold) {
	    MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
        /*Check whether the DIRECT_ONE_SIDED or DISABLE PTMalloc is enabled*/
#ifdef DISABLE_PTMALLOC
            if (pg_rank==0) {
                fprintf(stderr,"Error: On-demand connection management does not work when PTmalloc is disabled\n"
                "Please recompile MVAPICH2 without the CFLAG -DDISABLE_PTMALLOC, or\n"
                "Set MV2_ON_DEMAND_THRESHOLD to a value more than the number of processes to use all-to-all connections\n");
            }
            return -1;
#endif            
	}
	else {
            MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_BASIC_ALL2ALL;
        }

#ifdef RDMA_CM
        if ((NULL != getenv("MV2_USE_RDMA_CM")) || (NULL != getenv("MV2_ENABLE_IWARP_MODE"))){
            MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_RDMA_CM;
        }
#endif /* RDMA_CM */
    }

#ifdef CKPT
#ifdef RDMA_CM
    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_RDMA_CM) {
	if (pg_rank==0) {
	    fprintf(stderr,"Error: Checkpointing support doesn't work with RDMA_CM support\n"
		    "Please recompile MVAPICH2 without the CFLAG -DCKPT to disable checkpointing support,\n"
		    "or without the CFLAG -DRDMA_CM to disable RDMA_CM support\n");
	}
	return -1;
    }
#endif
#ifdef _SMP_
    if (pg_rank==0) {
        fprintf(stderr,"Error: Checkpointing support doesn't work with shared memory channel support\n"
                "Please recompile MVAPICH2 without the CFLAG -DCKPT to disable checkpointing support,\n"
                "or without the CFLAG -D_SMP_ to disable shared memory channel support\n");
    }
    return -1;
#endif       
    MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
    MPIDI_CH3I_CR_Init(pg, pg_rank, pg_size);
#endif

    /* Initialize the VC table associated with this process
       group (and thus COMM_WORLD) */
    for (p = 0; p < pg_size; p++) {
        MPIDI_PG_Get_vcr(pg, p, &vc);
        vc->ch.sendq_head = NULL;
        vc->ch.sendq_tail = NULL;
        vc->ch.req = (MPID_Request *) MPIU_Malloc(sizeof(MPID_Request));
        vc->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;
        vc->ch.read_state = MPIDI_CH3I_READ_STATE_IDLE;
        vc->ch.recv_active = NULL;
        vc->ch.send_active = NULL;
        vc->ch.cm_sendq_head = NULL;
        vc->ch.cm_sendq_tail = NULL;
#ifdef USE_RDMA_UNEX
        vc->ch.unex_finished_next = NULL;
        vc->ch.unex_list = NULL;
#endif
#ifdef _SMP_
	vc->smp.hostid = -1;
#endif
#ifdef CKPT
    vc->ch.rput_stop = 0;
#endif

    }

    /* save my vc_ptr for easy access */
    MPIDI_PG_Get_vcr(pg, pg_rank, &MPIDI_CH3I_Process.vc);

    /* Initialize Progress Engine */
    mpi_errno = MPIDI_CH3I_Progress_init();
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**ch3_init", 0);
        return mpi_errno;
    }

    /* allocate rmda memory and set up the queues */
    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND) {
        /*FillMe:call MPIDI_CH3I_CM_Init here*/
        mpi_errno = MPIDI_CH3I_CM_Init(pg, pg_rank);
    }
#ifdef RDMA_CM		/* Use the ON_DEMAND code itself */
    else if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_RDMA_CM) {
        /*FillMe:call RDMA_CM's initialization here*/
	mpi_errno = MPIDI_CH3I_CM_Init(pg, pg_rank);
        for (p = 0; p < pg_size; p++) {
            MPIDI_PG_Get_vcr(pg, p, &vc);
            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
        }
    }
#endif
    else {
        /*call old init to setup all connections*/
        mpi_errno = MPIDI_CH3I_RMDA_init(pg, pg_rank);
        for (p = 0; p < pg_size; p++) {
            MPIDI_PG_Get_vcr(pg, p, &vc);
            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
        }
    }
    
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**ch3_init", 0);
        return mpi_errno;
    }

    /* Initialize the smp channel */
#ifdef _SMP_
    mpi_errno = MPIDI_CH3I_SMP_init(pg);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**ch3_init", 0);
        return mpi_errno;
    }

    for (p = 0; p < pg_size; p++) {
        MPIDI_PG_Get_vcr(pg, p, &vc);
        vc->smp.sendq_head = NULL;
        vc->smp.sendq_tail = NULL;
        vc->smp.recv_active = NULL;
        vc->smp.send_active = NULL;
    }

#endif
#if 0
	No dynamic process management now
    /* The rdma interface needs to be adjusted to be able to retrieve the PARENT_ROOT_PORT_NAME */
    /* If this code copied from the shm channel were used, it assumes that PMI is used in 
     * MPIDI_CH3I_RDMA_init_process_group */
    if (has_parent) {
        /* This process was spawned. Create intercommunicator with parents. */

        if (pg_rank == 0) {
            /* get the port name of the root of the parents */
            mpi_errno =
                PMI_KVS_Get(pg->kvs_name, "PARENT_ROOT_PORT_NAME", val,
                            val_max_sz);
            if (mpi_errno != 0) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get_parent %d",
                                         mpi_errno);
                return mpi_errno;
            }
        }

        /* do a connect with the root */
        MPID_Comm_get_ptr(MPI_COMM_WORLD, commworld);
        mpi_errno = MPIDI_CH3_Comm_connect(val, 0, commworld, &intercomm);
        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**fail",
                                     "**fail %s",
                                     "spawned group unable to connect back to the parent");
            return mpi_errno;
        }

        MPIU_Strncpy(intercomm->name, "MPI_COMM_PARENT",
                     MPI_MAX_OBJECT_NAME);
        MPIR_Process.comm_parent = intercomm;

        /* TODO: Check that this intercommunicator gets freed in
           MPI_Finalize if not already freed.  */
    }
#endif
    return MPI_SUCCESS;
}

int MPIDI_CH3_VC_Init( MPIDI_VC_t *vc ) { 
#ifdef _SMP_
    vc->smp.sendq_head = NULL; 
    vc->smp.sendq_tail = NULL; 
    vc->smp.recv_active = NULL; 
    vc->smp.send_active = NULL; 
#endif
    vc->ch.sendq_head = NULL; 
    vc->ch.sendq_tail = NULL; 
    vc->ch.req = (MPID_Request *) MPIU_Malloc(sizeof(MPID_Request));
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    vc->ch.read_state = MPIDI_CH3I_READ_STATE_IDLE;
    vc->ch.recv_active = NULL; 
    vc->ch.send_active = NULL; 
#ifdef USE_RDMA_UNEX
    vc->ch.unex_finished_next = NULL; 
    vc->ch.unex_list = NULL; 
#endif
    return 0;
}

int MPIDI_CH3_PortFnsInit( MPIDI_PortFns *portFns ) {
   portFns->OpenPort    = 0;    
    portFns->ClosePort   = 0;
    portFns->CommAccept  = 0;
    portFns->CommConnect = 0;
    return MPI_SUCCESS;
}

int MPIDI_CH3_RMAFnsInit( MPIDI_RMAFns *RMAFns ) 
{
    return MPI_SUCCESS;
} 

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connect_to_root
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Connect_to_root(const char * port_name, MPIDI_VC_t ** new_vc)      {
    int mpi_errno;
    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**notimpl", 0);                                                   return mpi_errno;       
}

