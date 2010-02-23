/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2009, The Ohio State University. All rights
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
#include <mpimem.h>
#include "rdma_impl.h"
#include "pmi.h"
#include "vbuf.h"
#include "rdma_cm.h"
#include "cm.h"

#ifdef RDMA_CM

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...)                                      \
do {                                                              \
    int __rank;                                                   \
    PMI_Get_rank(&__rank);                                        \
    fprintf(stderr, "[%d][%s:%d] ", __rank, __FILE__, __LINE__);  \
    fprintf(stderr, args);                                        \
    fflush(stderr);                                               \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#define MV2_RDMA_CM_MIN_PORT_LIMIT  1024
#define MV2_RDMA_CM_MAX_PORT_LIMIT  65536

int *rdma_base_listen_port;
int *rdma_cm_host_list;
int *rdma_cm_local_ips;
int *rdma_cm_accept_count;
volatile int *rdma_cm_connect_count;
volatile int *rdma_cm_iwarp_msg_count;
volatile int rdma_cm_connected_count = 0;
volatile int rdma_cm_finalized = 0;
int rdma_cm_arp_timeout = 2000;
int g_num_smp_peers = 0;

char *init_message_buf;		/* Used for message exchange in RNIC case */
struct ibv_mr *init_mr;
struct ibv_sge init_send_sge;
struct ibv_recv_wr init_rwr;
struct ibv_send_wr init_swr;
struct rdma_cm_id *tmpcmid;    
sem_t rdma_cm_addr;

/* Handle the connection events */
int ib_cma_event_handler(struct rdma_cm_id *cma_id,
			 struct rdma_cm_event *event);

/* Thread to poll and handle CM events */
void *cm_thread(void *arg);

/* Obtain the information of local RNIC IP from the mv2.conf file */
int rdma_cm_get_local_ip();

/* create qp's for a ongoing connection request */
int rdma_cm_create_qp(MPIDI_VC_t *vc, int rail_index);

/* Initialize pd and cq associated with one rail */
int rdma_cm_init_pd_cq();

/* Get the rank of an active connect request */
int get_remote_rank(struct rdma_cm_id *cmid);

/* Get the rank of an active connect request */
int get_remote_rail(struct rdma_cm_id *cmid);

/* Get the rank of an active connect request */
int get_remote_qp_type(struct rdma_cm_id *cmid);

/* Exchange init messages for iWARP compliance */
int init_messages(int *hosts, int pg_rank, int pg_size);

/* RDMA_CM specific method implementations */

#undef FUNCNAME
#define FUNCNAME ib_cma_event_handler
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int ib_cma_event_handler(struct rdma_cm_id *cma_id,
			  struct rdma_cm_event *event)
{
    int ret = 0, rank, rail_index = 0;
    int pg_size, pg_rank, tmplen;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_VC_t  *vc, *gotvc;
    MPIDI_PG_t *pg_tmp;
    struct rdma_conn_param conn_param;
    char *pg_id;

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);


    switch (event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:
    DEBUG_PRINT("case RDMA_CM_ADDR_RESOLVED\n");
	if (cma_id == tmpcmid) {
            sem_post(&rdma_cm_addr);
	    break;
	}

	ret = rdma_resolve_route(cma_id, rdma_cm_arp_timeout);
	if (ret) {
	    ibv_va_error_abort(IBV_RETURN_ERR,
			    "rdma_resolve_route error %d\n", ret);
	}

	break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:
    DEBUG_PRINT("case RDMA_CM_EVENT_ROUTE_RESOLVED\n");

    /* VC pointer is stored in cm_id->context at cm_id creation */
    vc = (MPIDI_VC_t *) cma_id->context;
    rank = vc->pg_rank;
	rail_index = get_remote_rail(cma_id);

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI){
	    /* Switched into server mode */
	    break;
	}
	
    if (rank < 0 || rail_index < 0) {
	    DEBUG_PRINT("Unexpected error occured\n");
	}

	rdma_cm_create_qp(vc, rail_index);

	/* Connect to remote node */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = rdma_default_rnr_retry;
	conn_param.rnr_retry_count = rdma_default_rnr_retry;

    tmplen = 3 * sizeof(uint64_t) + strlen(MPIDI_Process.my_pg->id) + 1;
    if(tmplen > MAX_PG_ID_SIZE) {
    	ibv_error_abort(GEN_EXIT_ERR, "PG ID too long. Cannot use RDMA CM\n");
    }

    DEBUG_PRINT("allocating %d bytes for private_data\n", tmplen);
    conn_param.private_data = MPIU_Malloc(tmplen);

    if (!conn_param.private_data) {
        ibv_error_abort(GEN_EXIT_ERR, "Error allocating memory\n");
    }
	
    conn_param.private_data_len = tmplen;
    ((uint64_t *) conn_param.private_data)[0] = pg_rank;
    ((uint64_t *) conn_param.private_data)[1] = rail_index;
    ((uint64_t *) conn_param.private_data)[2] = (uint64_t) vc;
    pg_id = (char *) conn_param.private_data + 3*sizeof(uint64_t);

    MPIU_Strncpy(pg_id, MPIDI_Process.my_pg->id, MAX_PG_ID_SIZE);
    DEBUG_PRINT("Sending connection request to [rank = %d], [rail = %d] [vc = %x] [pg = %s]\n", 
            ((uint64_t *) conn_param.private_data)[0],
            ((uint64_t *) conn_param.private_data)[1],
            ((uint64_t *) conn_param.private_data)[2],
            pg_id);

	ret = rdma_connect(cma_id, &conn_param);
    if (ret) {
        ibv_va_error_abort(IBV_RETURN_ERR, 
            "rdma_connect error %d\n", ret);
    }

	break;
    case RDMA_CM_EVENT_CONNECT_REQUEST:
    DEBUG_PRINT("case RDMA_CM_EVENT_CONNECT_REQUEST\n");

#ifndef OFED_VERSION_1_1        /* OFED 1.2 */
	if (!event->param.conn.private_data_len){
            ibv_error_abort(IBV_RETURN_ERR,
			    "Error obtaining remote data from event private data\n");
	}
    rank       = ((uint64_t *) event->param.conn.private_data)[0];
    rail_index = ((uint64_t *) event->param.conn.private_data)[1];
    gotvc 	   = (MPIDI_VC_t *) ((uint64_t *) event->param.conn.private_data)[2];	
    pg_id      = (char *) event->param.conn.private_data + 3*sizeof(uint64_t);
#else  /* OFED 1.1 */
	if (!event->private_data_len){
            ibv_error_abort(IBV_RETURN_ERR,
			    "Error obtaining remote data from event private data\n");
	}
    rank       = ((uint64_t *) event->private_data)[0];
    rail_index = ((uint64_t *) event->private_data)[1];
    gotvc      = (MPIDI_VC_t*) ((uint64_t *) event->private_data)[2];
    pg_id      = event->private_data + 3*sizeof(uint64_t);
#endif

    DEBUG_PRINT("Passive side recieved connect request: [%d] :[%d] [vc: %x] [pg id: %s]\n",
                rank, rail_index, gotvc, pg_id);
	
    MPIDI_PG_Find(pg_id, &pg_tmp);
    if(pg_tmp == NULL) 
        ibv_error_abort(GEN_EXIT_ERR, "Could not find PG in conn request\n");
		
    MPIDI_PG_Get_vc(pg_tmp, rank, &vc);
    cma_id->context = vc;
    vc->mrail.remote_vc_addr = (uint64_t) gotvc;

	/* Both ranks are trying to connect. Clearing race condition */
	if (((vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI) && (pg_rank > rank)) ||
	    vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE)
	{
		DEBUG_PRINT("Passive size rejecting connect request: Crossing connection requests expected\n");
		ret = rdma_reject(cma_id, NULL, 0);
		if (ret){
		    ibv_va_error_abort(IBV_RETURN_ERR,
				    "rdma_reject error: %d\n", ret);
		}
		break;
	}
	
	/* Accepting the connection */
	rdma_cm_accept_count[rank]++;
	
	if (proc->use_iwarp_mode)
	    vc->ch.state = MPIDI_CH3I_VC_STATE_IWARP_SRV_WAITING;
	else
	    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;

	vc->mrail.rails[rail_index].cm_ids = cma_id;
	    
	/* Create qp */
        rdma_cm_create_qp(vc, rail_index);

        /* Posting a single buffer to cover for iWARP MPA requirement. */
        if (proc->use_iwarp_mode && !proc->has_srq)
        {
            PREPOST_VBUF_RECV(vc, rail_index);
        }

        if (rdma_cm_accept_count[rank] == rdma_num_rails)
        {
            MRAILI_Init_vc(vc);
        }

	/* Accept remote connection - passive connect */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = rdma_default_rnr_retry;
	conn_param.rnr_retry_count = rdma_default_rnr_retry;
    conn_param.private_data_len = sizeof (uint64_t);
    conn_param.private_data = MPIU_Malloc(conn_param.private_data_len);
    ((uint64_t *) conn_param.private_data)[0] = (uint64_t) vc;
	ret = rdma_accept(cma_id, &conn_param);
	if (ret) {
	    ibv_va_error_abort(IBV_RETURN_ERR,
			    "rdma_accept error: %d\n", ret);
	}
	
	break;
    case RDMA_CM_EVENT_ESTABLISHED:
    DEBUG_PRINT("case RDMA_CM_EVENT_ESTABLISHED\n");
    vc = (MPIDI_VC_t *) cma_id->context;
    rank = vc->pg_rank;

#ifndef OFED_VERSION_1_1        /* OFED 1.2 */
    if (event->param.conn.private_data_len) 
        vc->mrail.remote_vc_addr = ((uint64_t *) event->param.conn.private_data)[0];
#else  /* OFED 1.1 */
    if (event->private_data_len) 
        vc->mrail.remote_vc_addr = ((uint64_t *) event->private_data)[0];
#endif

	if (rank < 0) {		/* Overlapping connections */
	    DEBUG_PRINT("Got event for overlapping connections? removing...\n");
	    break;
	}

	rdma_cm_connect_count[rank]++;

	if (rdma_cm_connect_count[rank] == rdma_num_rails)
        {
	    if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI) {

        MRAILI_Init_vc(vc); /* Server has init'ed before accepting */

		/* Sending a noop for handling the iWARP requirement */
		if (proc->use_iwarp_mode) {
		    int i;
		    for (i = 0; i < rdma_num_rails; i++){
			MRAILI_Send_noop(vc, i);
			DEBUG_PRINT("Sending noop to [%d]\n", rank);
		    }
		    vc->ch.state = MPIDI_CH3I_VC_STATE_IWARP_CLI_WAITING;
		}
		else {
		    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		    vc->state = MPIDI_VC_STATE_ACTIVE;
		    MPIDI_CH3I_Process.new_conn_complete = 1;
		    DEBUG_PRINT("Connection Complete - Client: %d->%d\n", pg_rank, rank);
		}
	    }
	    else { 		/* Server side */
		if (!proc->use_iwarp_mode 
		    || (rdma_cm_iwarp_msg_count[vc->pg_rank] >= rdma_num_rails)) {

		    if ((vc->ch.state == MPIDI_CH3I_VC_STATE_IWARP_SRV_WAITING)
			|| (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV)) {
			vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
			vc->state = MPIDI_VC_STATE_ACTIVE;
			MPIDI_CH3I_Process.new_conn_complete = 1;
			MRAILI_Send_noop(vc, 0);
			DEBUG_PRINT("Connection Complete - Server: %d->%d\n", pg_rank, rank);
		    }
		}
	    }
	    rdma_cm_connected_count++;
	}

	/* All connections connected? Used only for non-on_demand case */
	if (rdma_cm_connected_count == (pg_size - 1 - g_num_smp_peers)) {
	    sem_post(&proc->rdma_cm);	    
	}

	break;

    case RDMA_CM_EVENT_ADDR_ERROR:
	ibv_va_error_abort(IBV_RETURN_ERR,
			"RDMA CM Address error: rdma cma event %d, error %d\n", event->event,
			event->status);
    case RDMA_CM_EVENT_ROUTE_ERROR:
	ibv_va_error_abort(IBV_RETURN_ERR,
			"RDMA CM Route error: rdma cma event %d, error %d\n", event->event,
			event->status);
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_UNREACHABLE:
	ibv_va_error_abort(IBV_RETURN_ERR,
            "rdma cma event %d, error %d\n", event->event, 
			event->status);
	break;
#if 0
        /*
         * These events don't really need a case since they are currently no
         * ops.
         */
    case RDMA_CM_EVENT_REJECTED:
	DEBUG_PRINT("RDMA CM Reject Event %d, error %d\n", event->event, event->status);
	break;

    case RDMA_CM_EVENT_DISCONNECTED:
	break;

    case RDMA_CM_EVENT_TIMEWAIT_EXIT:
    DEBUG_PRINT("caught RDMA_CM_EVENT_TIMEWAIT_EXIT \n");
    break;

    case RDMA_CM_EVENT_DEVICE_REMOVAL:
#endif

    default:
        DEBUG_PRINT("%s: Caught unhandled rdma cm event - %s\n",
                __FUNCTION__, rdma_event_str(event->event));
        break;
    }
    return ret;
}

void *cm_thread(void *arg)
{
    struct rdma_cm_event *event;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;    
    int ret;

    while (1) {

	ret = rdma_get_cm_event(proc->cm_channel, &event);
	if (rdma_cm_finalized) {
	    return NULL;
	}
	if (ret) {
	    ibv_va_error_abort(IBV_RETURN_ERR,
			    "rdma_get_cm_event err %d\n", ret);
	}

	DEBUG_PRINT("rdma cm event[id: %p]: %d\n", event->id, event->event);
	{
	    
	    MPICM_lock();
	    ret = ib_cma_event_handler(event->id, event);
	    MPICM_unlock();
	}

	rdma_ack_cm_event(event);
    }
}

#undef FUNCNAME
#define FUNCNAME get_base_listen_port
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int get_base_listen_port(int pg_rank, int* port)
{
    int mpi_errno = MPI_SUCCESS;
    char* cMaxPort = getenv("MV2_RDMA_CM_MAX_PORT");
    int maxPort = MV2_RDMA_CM_MAX_PORT_LIMIT;

    if (cMaxPort)
    {
        maxPort = atoi(cMaxPort);

        if (maxPort > MV2_RDMA_CM_MAX_PORT_LIMIT || maxPort < MV2_RDMA_CM_MIN_PORT_LIMIT)
        {
            MPIU_ERR_SETANDJUMP3(
                mpi_errno,
                MPI_ERR_OTHER,
                "**rdmacmmaxport",
                "**rdmacmmaxport %d %d %d",
                maxPort,
                MV2_RDMA_CM_MIN_PORT_LIMIT,
                MV2_RDMA_CM_MAX_PORT_LIMIT
            );
        }
    }

    char* cMinPort = getenv("MV2_RDMA_CM_MIN_PORT");
    int minPort = MV2_RDMA_CM_MIN_PORT_LIMIT;

    if (cMinPort)
    {
        minPort = atoi(cMinPort);

        if (minPort > MV2_RDMA_CM_MAX_PORT_LIMIT || minPort < MV2_RDMA_CM_MIN_PORT_LIMIT)
        {
            MPIU_ERR_SETANDJUMP3(
                mpi_errno,
                MPI_ERR_OTHER,
                "**rdmacmminport",
                "**rdmacmminport %d %d %d",
                minPort,
                MV2_RDMA_CM_MIN_PORT_LIMIT,
                MV2_RDMA_CM_MAX_PORT_LIMIT
            );
        }
    }

    int portRange = MPIDI_PG_Get_size(MPIDI_Process.my_pg) - g_num_smp_peers;
    DEBUG_PRINT("%s: portRange = %d\r\n", __FUNCTION__, portRange);

    if (maxPort - minPort < portRange)
    {
        MPIU_ERR_SETANDJUMP2(
            mpi_errno,
            MPI_ERR_OTHER,
            "**rdmacmportrange",
            "**rdmacmportrange %d %d",
            maxPort - minPort,
            portRange
        );
    }

    struct timeval seed;
    gettimeofday(&seed, NULL);
    char* envPort = getenv("MV2_RDMA_CM_PORT");
    int rdma_cm_default_port;

    if (envPort)
    {
        rdma_cm_default_port = atoi(envPort);

        if (rdma_cm_default_port == -1)
        {
            srand(seed.tv_usec);    /* Random seed for the port */
            rdma_cm_default_port = (rand() % (maxPort - minPort + 1)) + minPort;
        }
        else if (rdma_cm_default_port > maxPort || rdma_cm_default_port <= minPort)
        {
            MPIU_ERR_SETANDJUMP1(
                mpi_errno,
                MPI_ERR_OTHER,
                "**rdmacminvalidport",
                "**rdmacminvalidport %d",
                atoi(envPort)
            );
        }
    }
    else
    {
        srand(seed.tv_usec);    /* Random seed for the port */
        rdma_cm_default_port = rand() % (maxPort - minPort + 1) + minPort;
    }

    *port = htons(rdma_cm_default_port);

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME bind_listen_port
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int bind_listen_port(int pg_rank, int pg_size)
{
    struct sockaddr_in sin;
    int ret, count = 0;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    int mpi_errno = get_base_listen_port(pg_rank, &rdma_base_listen_port[pg_rank]);

    if (mpi_errno != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = rdma_base_listen_port[pg_rank];

    ret = rdma_bind_addr(proc->cm_listen_id, (struct sockaddr *) &sin);

    while (ret)
    {
        if ((mpi_errno = get_base_listen_port(pg_rank, &rdma_base_listen_port[pg_rank])) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        sin.sin_port = rdma_base_listen_port[pg_rank];
        ret = rdma_bind_addr(proc->cm_listen_id, (struct sockaddr *) &sin);
        DEBUG_PRINT("[%d] Port bind failed - %d. retrying %d\n", pg_rank,
                 rdma_base_listen_port[pg_rank], count++);
        if (count > 1000){
            ibv_error_abort(IBV_RETURN_ERR,
                            "Port bind failed\n");
        }
    }

    ret = rdma_listen(proc->cm_listen_id, 2 * (pg_size) * rdma_num_rails);
    if (ret) {
        ibv_va_error_abort(IBV_RETURN_ERR,
                        "rdma_listen failed: %d\n", ret);
    }

    DEBUG_PRINT("Listen port bind on %d\n", sin.sin_port);

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME ib_init_rdma_cm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int ib_init_rdma_cm(struct MPIDI_CH3I_RDMA_Process_t *proc,
			    int pg_rank, int pg_size)
{
    int i = 0, ret, num_interfaces;
    int mpi_errno = MPI_SUCCESS;
    char *value;

    if(sem_init(&(proc->rdma_cm), 0, 0)) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"sem_init", strerror(errno));
    }

    if(sem_init(&(rdma_cm_addr), 0, 0)) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"sem_init", strerror(errno));
    }

    if (!(proc->cm_channel = rdma_create_event_channel()))
    {
        MPIU_ERR_SETFATALANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "**fail %s",
            "Cannot create rdma_create_event_channel."
        );
    }

    rdma_base_listen_port = (int *) MPIU_Malloc (pg_size * sizeof(int));
    rdma_cm_connect_count = (int *) MPIU_Malloc (pg_size * sizeof(int));
    rdma_cm_accept_count = (int *) MPIU_Malloc (pg_size * sizeof(int));
    rdma_cm_iwarp_msg_count = (int *) MPIU_Malloc (pg_size * sizeof(int));

    if (!rdma_base_listen_port 
	|| !rdma_cm_connect_count 
	|| !rdma_cm_accept_count
	|| !rdma_cm_iwarp_msg_count) {
        MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
    }

    for (i = 0; i < pg_size; i++) {
	rdma_cm_connect_count[i] = 0;
	rdma_cm_accept_count[i] = 0;
	rdma_cm_iwarp_msg_count[i] = 0;
    }

    for (i = 0; i < rdma_num_hcas; i++){
	proc->ptag[i] = NULL;
	proc->cq_hndl[i] = NULL;
	proc->send_cq_hndl[i] = NULL;
	proc->recv_cq_hndl[i] = NULL;
    }

    if ((value = getenv("MV2_RDMA_CM_ARP_TIMEOUT")) != NULL) {
	rdma_cm_arp_timeout = atoi(value);
	if (rdma_cm_arp_timeout < 0) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
				      "**fail %s", "Invalid rdma cm arp timeout value specified\n");
	}
    }


    /* Init. list of local IPs to use */
    num_interfaces = rdma_cm_get_local_ip();

    if (num_interfaces < rdma_num_hcas * rdma_num_ports){
	ibv_error_abort(IBV_RETURN_ERR,
			"Not enough interfaces (ip addresses) specified in /etc/mv2.conf\n");
    }

    /* Create the listen cm_id */
    ret = rdma_create_id(proc->cm_channel, &proc->cm_listen_id, proc, RDMA_PS_TCP);
    if (ret) {
	ibv_va_error_abort(IBV_RETURN_ERR,
			"rdma_create_id error %d: Could not create listen cm_id\n", ret);
    }

    /* Create the connection management thread */
    pthread_create(&proc->cmthread, NULL, cm_thread, NULL);

    /* Find a base port, relay it to the peers and listen */
    if((mpi_errno = bind_listen_port(pg_rank, pg_size)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    /* Create CQ and PD */
    rdma_cm_init_pd_cq();

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/*
 * TODO add error handling
 */
int rdma_cm_connect_all(int *hosts, int pg_rank, MPIDI_PG_t *pg)
{
    int i, j, k, ret, rail_index, pg_size;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    int max_num_ips = rdma_num_hcas * rdma_num_ports;

    if (!proc->use_rdma_cm_on_demand){
	/* Initiate non-smp active connect requests */
	for (i = 0; i < pg_rank; i++){

	    if (!USE_SMP || hosts[i * max_num_ips] != hosts[pg_rank * max_num_ips]){

        MPIDI_PG_Get_vc(pg, i, &vc);
		vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;

		/* Initiate all needed qp connections */
		for (j = 0; j < rdma_num_hcas*rdma_num_ports; j++){
		    for (k = 0; k < rdma_num_qp_per_port; k++){
			rail_index = j * rdma_num_qp_per_port + k;
            ret = rdma_cm_connect_to_server(vc, hosts[i*max_num_ips + j], rail_index);
		    }
		}
	    }
	}
	
	/* Wait for all non-smp connections to complete */
    pg_size = MPIDI_PG_Get_size(pg);
	if (pg_size - 1 - g_num_smp_peers > 0)
	    sem_wait(&proc->rdma_cm);

	/* RDMA CM Connection Setup Complete */
	DEBUG_PRINT("RDMA CM based connection setup complete\n");
    }

    rdma_cm_host_list = hosts;

    return 0;
}

int rdma_cm_get_contexts(){
    int i, ret;
    struct sockaddr_in sin;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    for (i = 0; i < rdma_num_hcas*rdma_num_ports; i++){

	ret = rdma_create_id(proc->cm_channel, &tmpcmid, proc, RDMA_PS_TCP);
	if (ret) {
	    ibv_va_error_abort(IBV_RETURN_ERR,
			    "rdma_create_id error %d\n", ret);
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = rdma_cm_local_ips[i];
	ret = rdma_resolve_addr(tmpcmid, NULL, (struct sockaddr *) &sin, rdma_cm_arp_timeout);

	if (ret) {
	    ibv_va_error_abort(IBV_RETURN_ERR,
			    "rdma_resolve_addr error %d\n", ret);
	}

	sem_wait(&rdma_cm_addr);

	proc->nic_context[i] = tmpcmid->verbs;

	rdma_destroy_id(tmpcmid);
	tmpcmid = NULL;
    }

    return 0;
}

int rdma_cm_create_qp(MPIDI_VC_t *vc, int rail_index)
{
    struct ibv_qp_init_attr init_attr;
    int hca_index, ret;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    struct rdma_cm_id *cmid;

    hca_index = rail_index / (rdma_num_ports * rdma_num_qp_per_port);

    /* Create CM_ID */
    cmid = vc->mrail.rails[rail_index].cm_ids;

    {
	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_recv_sge = rdma_default_max_sg_list;
	init_attr.cap.max_send_sge = rdma_default_max_sg_list;
	init_attr.cap.max_inline_data = rdma_max_inline_size;
	
	init_attr.cap.max_send_wr = rdma_default_max_send_wqe;
    if (rdma_iwarp_use_multiple_cq && (proc->hca_type == CHELSIO_T3) &&
        (proc->cluster_size != VERY_SMALL_CLUSTER)) {
	    init_attr.send_cq = proc->send_cq_hndl[hca_index];
	    init_attr.recv_cq = proc->recv_cq_hndl[hca_index];
    } else {
	    init_attr.send_cq = proc->cq_hndl[hca_index];
	    init_attr.recv_cq = proc->cq_hndl[hca_index];
    }
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.sq_sig_all = 0;
    }

    /* SRQ based? */
    if (proc->has_srq) {
        init_attr.cap.max_recv_wr = 0;
        init_attr.srq = proc->srq_hndl[hca_index];
    } else {
        init_attr.cap.max_recv_wr = rdma_default_max_recv_wqe;
    }

    ret = rdma_create_qp(cmid, proc->ptag[hca_index], &init_attr);
    if (ret){
        ibv_va_error_abort(IBV_RETURN_ERR,
                "Error creating qp using rdma_cm.  %d [cmid: %p, pd: %p,"
                "send_cq: %p, recv_cq: %p] \n",
                ret, cmid, proc->ptag[hca_index],
                proc->send_cq_hndl[hca_index],
                proc->recv_cq_hndl[hca_index]);
    }

    /* Save required handles */
    vc->mrail.rails[rail_index].qp_hndl = cmid->qp;
    if (rdma_iwarp_use_multiple_cq && (proc->hca_type == CHELSIO_T3) &&
        (proc->cluster_size != VERY_SMALL_CLUSTER)) {
       vc->mrail.rails[rail_index].cq_hndl = NULL;
       vc->mrail.rails[rail_index].send_cq_hndl = proc->send_cq_hndl[hca_index];
       vc->mrail.rails[rail_index].recv_cq_hndl = proc->recv_cq_hndl[hca_index];
    } else {
       vc->mrail.rails[rail_index].cq_hndl = proc->cq_hndl[hca_index];
       vc->mrail.rails[rail_index].send_cq_hndl = NULL;
       vc->mrail.rails[rail_index].recv_cq_hndl = NULL;
    }

    vc->mrail.rails[rail_index].nic_context = cmid->verbs;
    vc->mrail.rails[rail_index].hca_index = hca_index;
    vc->mrail.rails[rail_index].port = 1;

    return ret;
}

#undef FUNCNAME
#define FUNCNAME rdma_cm_get_hostnames
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int *rdma_cm_get_hostnames(int pg_rank, MPIDI_PG_t *pg)
{
    int *hosts;
    int error, i,j,k;
    char *temp;
    int length = 32*rdma_num_hcas*rdma_num_ports;
    char rank[16];
    char buffer[length];
    int key_max_sz;
    int val_max_sz;
    char *key;
    char *val;
    int pg_size = MPIDI_PG_Get_size(pg);
    int max_num_ips = rdma_num_hcas * rdma_num_ports; 

    hosts = (int *) MPIU_Malloc (pg_size * max_num_ips * sizeof(int));
    if (!hosts){
	ibv_error_abort(IBV_RETURN_ERR, "Memory allocation error\n");
    }
    rdma_cm_host_list = hosts;
    
    sprintf(rank, "ip%d", pg_rank);
    sprintf(buffer, "%d", rdma_base_listen_port[pg_rank]);
    for(i=0; i<max_num_ips; i++)
    {
      sprintf( buffer+strlen(buffer), "-%d", rdma_cm_local_ips[i]);
      rdma_cm_host_list[pg_rank*max_num_ips + i] = rdma_cm_local_ips[i];
    }

    DEBUG_PRINT("[%d] message to be sent: %s\n", pg_rank, buffer);

    error = PMI_KVS_Get_key_length_max(&key_max_sz);
    key = MPIU_Malloc(key_max_sz+1);
    PMI_KVS_Get_value_length_max(&val_max_sz);
    val = MPIU_Malloc(val_max_sz+1);

    if (key == NULL || val == NULL) {
	   ibv_error_abort(GEN_EXIT_ERR, "Error allocating memory\n");
    }

    MPIU_Strncpy(key, rank, 16);
    MPIU_Strncpy(val, buffer, length);
    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
    if (error != 0) {
	ibv_error_abort(IBV_RETURN_ERR,
			"PMI put failed\n");
    }

    error = PMI_KVS_Commit(pg->ch.kvs_name);
    if (error != 0) {
        ibv_error_abort(IBV_RETURN_ERR,
                        "PMI put failed\n");
    }

    {
	error = PMI_Barrier();
	if (error != 0) {
            ibv_error_abort(IBV_RETURN_ERR,
                            "PMI Barrier failed\n");
	}
    }

    for (i = 0; i < pg_size; i++){    
     if(i != pg_rank) {
   	  sprintf(rank, "ip%d", i);
	  MPIU_Strncpy(key, rank, 16);
      error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
      if (error != 0) {
             ibv_error_abort(IBV_RETURN_ERR,
                             "PMI Lookup name failed\n");
      }
	  MPIU_Strncpy(buffer, val, length);

      sscanf(buffer, "%d", &rdma_base_listen_port[i]);
      temp = buffer;
      for(j=0; j<max_num_ips; j++)
      {
        temp = strchr(temp,'-') + 1; 
        sscanf(temp, "%d", &rdma_cm_host_list[i*max_num_ips + j]);
      }
      }   
    }

    /* Find smp processes */
    if (USE_SMP) {
       for (i = 0; i < pg_size; i++){
	   if (pg_rank == i)
	       continue;
   	   if (hosts[i * max_num_ips] == hosts[pg_rank * max_num_ips])
	       ++g_num_smp_peers;
       }
    }
    DEBUG_PRINT("Number of SMP peers for %d is %d\n", pg_rank, 
		g_num_smp_peers);

    MPIU_Free(val);
    MPIU_Free(key);

    return hosts;
}

/* Gets the ip address in network byte order */
/*
 * TODO add error handling
 */
int rdma_cm_get_local_ip(){
    FILE *fp_port;
    char ip[32];
    char fname[512];
    int i = 0;

    sprintf(fname, "/etc/mv2.conf");
    fp_port = fopen(fname, "r");

    if (NULL == fp_port){
	ibv_error_abort(GEN_EXIT_ERR, 
			"Error opening file \"/etc/mv2.conf\". Local rdma_cm address required in this file.\n");
    }

    rdma_cm_local_ips = MPIU_Malloc(rdma_num_hcas*rdma_num_ports*sizeof(int));

    while ((fscanf(fp_port, "%s\n", ip)) != EOF){
  	 rdma_cm_local_ips[i] = inet_addr(ip);
	 i++;
    }
    fclose(fp_port);

    return i;
}

int rdma_cm_connect_to_server(MPIDI_VC_t *vc, int ipnum, int rail_index){
    int ret = 0;
    struct sockaddr_in sin;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    /* store VC used for connection in the context, so we get back vc at event callbacks */
    ret = rdma_create_id(proc->cm_channel, &(vc->mrail.rails[rail_index].cm_ids), vc, RDMA_PS_TCP);
    if (ret) {
        ibv_va_error_abort(IBV_RETURN_ERR,
                        "rdma_create_id error %d\n", ret);
    }

    /* Resolve addr */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ipnum;
    sin.sin_port = rdma_base_listen_port[vc->pg_rank];

    ret = rdma_resolve_addr(vc->mrail.rails[rail_index].cm_ids, NULL, (struct sockaddr *) &sin, rdma_cm_arp_timeout);
    if (ret) {
        ibv_va_error_abort(IBV_RETURN_ERR,
                        "rdma_resolve_addr error %d\n", ret);
    }

    DEBUG_PRINT("Active connect initiated for %d [ip: %d:%d] [rail %d]\n", 
        vc->pg_rank, ipnum, rdma_base_listen_port[vc->pg_rank], rail_index);
    return ret;
}

#undef FUNCNAME
#define FUNCNAME rdma_cm_init_pd_cq
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_cm_init_pd_cq()
{
    MPIDI_CH3I_RDMA_Process_t* proc = &MPIDI_CH3I_RDMA_Process;
    int i = 0;
    int pg_rank;

    PMI_Get_rank(&pg_rank);
    rdma_cm_get_contexts();

    for (; i < rdma_num_hcas; ++i)
    {
        /* Allocate the protection domain for the HCA */
	proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);

	if (!proc->ptag[i]) {
	    ibv_va_error_abort(GEN_EXIT_ERR, "Failed to allocate pd %d\n", i);
	}

        /* Allocate the completion queue handle for the HCA */
        if(rdma_use_blocking)
        {
            proc->comp_channel[i] = ibv_create_comp_channel(proc->nic_context[i]);

            if (!proc->comp_channel[i]) {
		        ibv_error_abort(GEN_EXIT_ERR, "Create comp channel failed\n");
            }

            if (rdma_iwarp_use_multiple_cq && (proc->hca_type == CHELSIO_T3) &&
                (proc->cluster_size != VERY_SMALL_CLUSTER)) {
	            /* Allocate the completion queue handle for the HCA */
                /* Trac #423 */
	            proc->send_cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                NULL,
	                0);
	
	            if (!proc->send_cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }
	
	            if (ibv_req_notify_cq(proc->send_cq_hndl[i], 0)) {
	                ibv_error_abort(GEN_EXIT_ERR,
                                     "Request notify for CQ failed\n");
	            }

	            proc->recv_cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                NULL,
	                0);
	
	            if (!proc->recv_cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }

	            if (ibv_req_notify_cq(proc->recv_cq_hndl[i], 0)) {
	                ibv_error_abort(GEN_EXIT_ERR,
                                     "Request notify for CQ failed\n");
	            }
            } else {
	            proc->cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                proc->comp_channel[i],
	                0);
	
	            if (!proc->cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }
	
	            if (ibv_req_notify_cq(proc->cq_hndl[i], 0)) {
	                ibv_error_abort(GEN_EXIT_ERR,
                                     "Request notify for CQ failed\n");
	            }
            }
        }
        else
        {
            if (rdma_iwarp_use_multiple_cq && (proc->hca_type == CHELSIO_T3) &&
                (proc->cluster_size != VERY_SMALL_CLUSTER)) {
	            /* Allocate the completion queue handle for the HCA */
                /* Trac #423*/
	            proc->send_cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                NULL,
	                0);
	
	            if (!proc->send_cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }
	
	            proc->recv_cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                NULL,
	                0);
	
	            if (!proc->recv_cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }
            } else {
	            proc->cq_hndl[i] = ibv_create_cq(
	                proc->nic_context[i],
	                rdma_default_max_cq_size,
	                NULL,
	                NULL,
	                0);
	
	            if (!proc->cq_hndl[i]) {
			        ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	            }
            }
        }

        if (proc->has_srq && !proc->srq_hndl[i])
        {
            proc->srq_hndl[i] = create_srq(proc, i);
        }

	DEBUG_PRINT("[%d][rail %d] proc->ptag %p, proc->cq_hndl %p, proc->srq_hndl %p\n",
		    pg_rank, i, proc->ptag[i], proc->cq_hndl[i], proc->srq_hndl[i]);
    }

    return 0;
}

int get_remote_rank(struct rdma_cm_id *cmid)
{
    return -1;
}

int get_remote_rail(struct rdma_cm_id *cmid)
{
    int pg_size, pg_rank, i, rail_index = 0;
    MPIDI_VC_t  *vc = (MPIDI_VC_t *) cmid->context;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (i = 0; i < pg_size; i++){
	if ( pg_rank == i)
	    continue;
	for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
	    if (cmid == vc->mrail.rails[rail_index].cm_ids)
		return rail_index;
	}
    }
    return -1;
}

void ib_finalize_rdma_cm(int pg_rank, MPIDI_PG_t *pg)
{
    int i, rail_index = 0, pg_size;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    MPIU_Free(rdma_base_listen_port);
    MPIU_Free(rdma_cm_accept_count); 
    MPIU_Free(rdma_cm_local_ips);
    pg_size = MPIDI_PG_Get_size(pg);

    if ((g_num_smp_peers + 1) < pg_size){

	for (i = 0; i < pg_size; i++){
	    if (i == pg_rank)
		continue;
	    if (USE_SMP && (rdma_cm_host_list[i * rdma_num_hcas] == rdma_cm_host_list[pg_rank * rdma_num_hcas]))
		continue;
	    
        MPIDI_PG_Get_vc(pg, i, &vc); 
	    if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
		for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
		    if (vc->mrail.rails[rail_index].cm_ids != NULL) {
			rdma_disconnect(vc->mrail.rails[rail_index].cm_ids);
			rdma_destroy_qp(vc->mrail.rails[rail_index].cm_ids);
		    }
		}
	    }
	}
	
	for (i = 0; i < rdma_num_hcas; i++) {
	    if (MPIDI_CH3I_RDMA_Process.cq_hndl[i])
		ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);

	    if (MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]) {
		    ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]);
        }

	    if (MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]) {
		    ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]);
        }

	    if (MPIDI_CH3I_RDMA_Process.has_srq) {
		if (!MPIDI_CH3I_RDMA_Process.srq_hndl[i]){
		    pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
		    pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i],NULL);
		    ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
		}
	    }
	    if(rdma_use_blocking) {
		ibv_destroy_comp_channel(MPIDI_CH3I_RDMA_Process.comp_channel[i]);
	    }
	    deallocate_vbufs(i);
	    while (dreg_evict());

	    if (MPIDI_CH3I_RDMA_Process.ptag[i])
		ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
	}

	for (i = 0; i < pg_size; i++){
	    if (i == pg_rank)
		continue;
	    if (USE_SMP && (rdma_cm_host_list[i * rdma_num_hcas] == rdma_cm_host_list[pg_rank * rdma_num_hcas]))
		continue;
        MPIDI_PG_Get_vc(pg, i, &vc);
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
		for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
		    if (vc->mrail.rails[rail_index].cm_ids != NULL)
			rdma_destroy_id(vc->mrail.rails[rail_index].cm_ids);
		}
	    }
	}

    }

    if (pg_size > 1) {

	rdma_destroy_id(proc->cm_listen_id);
	rdma_cm_finalized = 1;
	rdma_destroy_event_channel(MPIDI_CH3I_RDMA_Process.cm_channel);

	pthread_cancel(proc->cmthread);
	pthread_join(proc->cmthread, NULL);

    }

    DEBUG_PRINT("RDMA CM resources finalized\n");
}


#endif /* RDMA_CM */
