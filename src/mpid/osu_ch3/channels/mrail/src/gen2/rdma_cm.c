/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2007, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include <malloc.h>

#include "rdma_impl.h"
#include "pmi.h"
#include "vbuf.h"
#include "rdma_cm.h"
#include "cm.h"

int num_smp_peers = 0;

#ifdef RDMA_CM

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...)                                      \
do {                                                              \
    int pt_rank;                                                  \
    PMI_Get_rank(&pt_rank);                                       \
    fprintf(stderr, "[%d][%s:%d] ", pt_rank, __FILE__, __LINE__); \
    fprintf(stderr, args);                                        \
    fflush(stderr);                                               \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

int *rdma_base_listen_port;
int *rdma_cm_host_list;
int rdma_cm_local_ips[MAX_NUM_HCAS];
int *rdma_cm_accept_count;
volatile int *rdma_cm_connect_count;
volatile int *rdma_cm_iwarp_msg_count;
volatile int rdma_cm_connected_count = 0;
volatile int rdma_cm_finalized = 0;
int rdma_cm_arp_timeout = 2000;

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

/* Bind to a random port between 12000 and 18000 */
int bind_listen_port(int pg_rank, int pg_size); 

/* Get user defined port number */
int get_base_listen_port();

/* Obtain the information of local RNIC IP from the mv2.conf file */
int rdma_cm_get_local_ip();

/* create qp's for a ongoing connection request */
int rdma_cm_create_qp(int rank, int rail_index, int one_sided);

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

int ib_cma_event_handler(struct rdma_cm_id *cma_id,
			  struct rdma_cm_event *event)
{
    int ret = 0, rank, rail_index = 0, one_sided = 0, max_count = 0;
    int pg_size, pg_rank;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_VC_t  *vc;
    struct rdma_conn_param conn_param;

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);

    max_count = rdma_num_rails * (proc->has_one_sided ? 2 : 1);

    switch (event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:

	if (cma_id == tmpcmid) {
            sem_post(&rdma_cm_addr);
	    break;
	}

	ret = rdma_resolve_route(cma_id, rdma_cm_arp_timeout);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_resolve_route error %d\n", ret);
	}

	break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:

	/* Create qp */
	rank = get_remote_rank(cma_id);
	rail_index = get_remote_rail(cma_id);
	one_sided = get_remote_qp_type(cma_id);

	MPIDI_PG_Get_vc(cached_pg, rank, &vc);

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI){
	    /* Switched into server mode */
	    break;
	}
	
	if (rank < 0 || rail_index < 0 || one_sided < 0)
	    DEBUG_PRINT("Unexpected error occured\n");

	rdma_cm_create_qp(rank, rail_index, one_sided);

	/* Connect to remote node */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = rdma_default_rnr_retry;
	conn_param.rnr_retry_count = rdma_default_rnr_retry;
	conn_param.private_data_len = 3 * sizeof(int);
	conn_param.private_data = malloc(3 * sizeof(int));

	if (!conn_param.private_data)
	    fprintf(stderr, "Error allocating memory\n");
	((int *)conn_param.private_data)[0] = pg_rank;
	((int *)conn_param.private_data)[1] = rail_index;
	((int *)conn_param.private_data)[2] = one_sided;

	ret = rdma_connect(cma_id, &conn_param);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_connect error %d\n", ret);
	}

	break;
    case RDMA_CM_EVENT_CONNECT_REQUEST:

#ifndef OFED_VERSION_1_1        /* OFED 1.2 */
	if (!event->param.conn.private_data_len){
            ibv_error_abort(IBV_RETURN_ERR,
			    "Error obtaining remote data from event private data\n");
	}
	
	rank = ((int *)event->param.conn.private_data)[0];
	rail_index = ((int *)event->param.conn.private_data)[1];
	one_sided = ((int *)event->param.conn.private_data)[2];
#else  /* OFED 1.1 */
	if (!event->private_data_len){
            ibv_error_abort(IBV_RETURN_ERR,
			    "Error obtaining remote data from event private data\n");
	}
	
	rank = ((int *)event->private_data)[0];
	rail_index = ((int *)event->private_data)[1];
	one_sided = ((int *)event->private_data)[2];
#endif
	DEBUG_PRINT("Passive side recieved connect request: [%d] :[%d] -[%d]\n",
		    rank, rail_index, one_sided);

        MPIDI_PG_Get_vc(cached_pg, rank, &vc);

	if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI) {
	    /* Both ranks are trying to connect. Clearing race condition */
	    if (pg_rank > rank){
		DEBUG_PRINT("Passive size rejecting connect request: Crossing connection requests expected\n");
		ret = rdma_reject(cma_id, NULL, 0);
		if (ret){
		    ibv_error_abort(IBV_RETURN_ERR,
				    "rdma_reject error: %d\n", ret);
		}
		break;
	    }
	}
	
	/* Accepting the connection */
	rdma_cm_accept_count[rank]++;
	
	if (proc->use_iwarp_mode)
	    vc->ch.state = MPIDI_CH3I_VC_STATE_IWARP_SRV_WAITING;
	else
	    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_SRV;

	if (one_sided)
	    vc->mrail.rails[rail_index].cm_ids_1sc = cma_id;
	else
	    vc->mrail.rails[rail_index].cm_ids = cma_id;
	    
	/* Create qp */
        rdma_cm_create_qp(rank, rail_index, one_sided);

	if (rdma_cm_accept_count[rank] == max_count) {
	    MRAILI_Init_vc(vc, rank);
	}

	/* Accept remote connection - passive connect */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = rdma_default_rnr_retry;
	conn_param.rnr_retry_count = rdma_default_rnr_retry;
	ret = rdma_accept(cma_id, &conn_param);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_accept error: %d\n", ret);
	}
	
	break;
    case RDMA_CM_EVENT_ESTABLISHED:

	rank = get_remote_rank(cma_id);
	if (rank < 0) {		/* Overlapping connections */
	    DEBUG_PRINT("Got event for overlapping connections? removing...\n");
	    break;
	}

	MPIDI_PG_Get_vc(cached_pg, rank, &vc);

	rdma_cm_connect_count[rank]++; 

	if (rdma_cm_connect_count[rank] == max_count){
	    if (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI) {

		MRAILI_Init_vc(vc, rank); /* Server has init'ed before accepting */

		/* Sending a noop for handling the iWARP requirement */
		if (proc->use_iwarp_mode) {
		    int i;
		    for (i = 0; i < rdma_num_rails; i++){
			MRAILI_Send_noop(vc, i);
			DEBUG_PRINT("Sending noop to [%d]\n", rank);
		    }
		    if (rdma_num_rails == 1) {
			vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
			vc->state = MPIDI_VC_STATE_ACTIVE;
			MPIDI_CH3I_Process.new_conn_complete = 1;
			DEBUG_PRINT("Connection Complete: %d->%d\n", pg_rank, rank);
		    }
		    else {
                        /* This case is needed only for multi-rail */
			vc->ch.state = MPIDI_CH3I_VC_STATE_IWARP_CLI_WAITING;
		    }
		}
		else {
		    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		    vc->state = MPIDI_VC_STATE_ACTIVE;
		    MPIDI_CH3I_Process.new_conn_complete = 1;
		    DEBUG_PRINT("Connection Complete: %d->%d\n", pg_rank, rank);
		}
	    }
	    else { 		/* Server side */
		if (!proc->use_iwarp_mode 
		    || (rdma_cm_iwarp_msg_count[vc->pg_rank] >= rdma_num_rails)) {
		    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		    vc->state = MPIDI_VC_STATE_ACTIVE;
		    MPIDI_CH3I_Process.new_conn_complete = 1;
                    if (rdma_num_rails > 1)
                        MRAILI_Send_noop(vc, 0);
		    DEBUG_PRINT("Connection Complete: %d->%d\n", pg_rank, rank);
		}
	    }
	    rdma_cm_connected_count++;
	}

	/* All connections connected? Used only for non-on_demand case */
	if (rdma_cm_connected_count == (pg_size - 1 - num_smp_peers)) {
	    sem_post(&proc->rdma_cm);	    
	}

	break;

    case RDMA_CM_EVENT_ADDR_ERROR:
	ibv_error_abort(IBV_RETURN_ERR,
			"RDMA CM Address error: rdma cma event %d, error %d\n", event->event,
			event->status);
    case RDMA_CM_EVENT_ROUTE_ERROR:
	ibv_error_abort(IBV_RETURN_ERR,
			"RDMA CM Route error: rdma cma event %d, error %d\n", event->event,
			event->status);
    case RDMA_CM_EVENT_CONNECT_ERROR:
    case RDMA_CM_EVENT_UNREACHABLE:
	ibv_error_abort(IBV_RETURN_ERR,
			"rdma cma event %d, error %d\n", event->event,
			event->status);
	break;
    case RDMA_CM_EVENT_REJECTED:
	DEBUG_PRINT("RDMA CM Reject Event %d, error %d\n", event->event, event->status);
	break;

    case RDMA_CM_EVENT_DISCONNECTED:
	break;

    case RDMA_CM_EVENT_DEVICE_REMOVAL:

    default:
	ibv_error_abort(IBV_RETURN_ERR,
			"bad event type\n");
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
	    ibv_error_abort(IBV_RETURN_ERR,
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

    proc->cm_channel = rdma_create_event_channel();

    rdma_base_listen_port = (int *) malloc (pg_size * sizeof(int));
    rdma_cm_connect_count = (int *) malloc (pg_size * sizeof(int));
    rdma_cm_accept_count = (int *) malloc (pg_size * sizeof(int));
    rdma_cm_iwarp_msg_count = (int *) malloc (pg_size * sizeof(int));

    if (!rdma_base_listen_port 
	|| !rdma_cm_connect_count 
	|| !rdma_cm_accept_count
	|| !rdma_cm_iwarp_msg_count) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Memory allocation error");
    }

    for (i = 0; i < pg_size; i++) {
	rdma_cm_connect_count[i] = 0;
	rdma_cm_accept_count[i] = 0;
	rdma_cm_iwarp_msg_count[i] = 0;
    }

    for (i = 0; i < rdma_num_hcas; i++){
	proc->ptag[i] = NULL;
	proc->cq_hndl[i] = NULL;
	proc->cq_hndl_1sc[i] = NULL;
    }

    if (!proc->cm_channel) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Cannot create rdma_create_event_channel");
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
	ibv_error_abort(IBV_RETURN_ERR,
			"rdma_create_id error %d: Could not create listen cm_id\n", ret);
    }

    /* Create the connection management thread */
    pthread_create(&proc->cmthread, NULL, cm_thread, NULL);

    /* Find a base port, relay it to the peers and listen */
    bind_listen_port(pg_rank, pg_size);

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
int rdma_cm_connect_all(int *hosts, int pg_rank, int pg_size)
{
    int i, j, k, ret, rail_index;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    if (!proc->use_rdma_cm_on_demand){
	/* Initiate non-smp active connect requests */
	for (i = 0; i < pg_rank; i++){

	    if (hosts[i * rdma_num_hcas] != hosts[pg_rank * rdma_num_hcas]){

		MPIDI_PG_Get_vc(cached_pg, i, &vc);
		vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;

		/* Initiate all needed qp connections */
		for (j = 0; j < rdma_num_hcas; j++){
		    for (k = 0; k < rdma_num_ports * rdma_num_qp_per_port; k++){
			rail_index = j * rdma_num_ports * rdma_num_qp_per_port + k;
			ret = rdma_cm_connect_to_server(i, hosts[i*rdma_num_hcas + j], rail_index, 0);
			if (proc->has_one_sided)
			    ret = rdma_cm_connect_to_server(i, hosts[i*rdma_num_hcas + j], rail_index, 1);
		    }
		}
	    }
	}
	
	/* Wait for all non-smp connections to complete */
	if (pg_size - 1 - num_smp_peers > 0)
	    sem_wait(&proc->rdma_cm);

	/* RDMA CM Connection Setup Complete */
	DEBUG_PRINT("RDMA CM based connection setup complete\n");
    }

    rdma_cm_host_list = hosts;

    return 0;
}

int rdma_cm_get_contexts(){
    int i, ret, count = 0, pg_rank;
    struct sockaddr_in sin;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    for (i = 0; i < rdma_num_hcas; i++){

	ret = rdma_create_id(proc->cm_channel, &tmpcmid, proc, RDMA_PS_TCP);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_create_id error %d\n", ret);
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = rdma_cm_local_ips[i];
	ret = rdma_resolve_addr(tmpcmid, NULL, (struct sockaddr *) &sin, rdma_cm_arp_timeout);

	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_resolve_addr error %d\n", ret);
	}

	sem_wait(&rdma_cm_addr);

	proc->nic_context[i] = tmpcmid->verbs;

	rdma_destroy_id(tmpcmid);
	tmpcmid = NULL;
    }

    return 0;
}

int bind_listen_port(int pg_rank, int pg_size)
{
    struct sockaddr_in sin;
    int ret, count = 0;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    rdma_base_listen_port[pg_rank] = get_base_listen_port(pg_rank);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = 0;
    sin.sin_port = rdma_base_listen_port[pg_rank];

    ret = rdma_bind_addr(proc->cm_listen_id, (struct sockaddr *) &sin);
    while (ret) {
	rdma_base_listen_port[pg_rank] = get_base_listen_port(pg_rank);
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
	ibv_error_abort(IBV_RETURN_ERR,
			"rdma_listen failed: %d\n", ret);
    }

    DEBUG_PRINT("Listen port bind on %d\n", sin.sin_port);
    return rdma_base_listen_port[pg_rank];
}

int get_base_listen_port(int pg_rank)
{
    int rdma_cm_default_port = MPIDI_CH3I_RDMA_CM_DEFAULT_BASE_LISTEN_PORT + pg_rank;
    struct timeval seed;
    char *value = getenv("MV2_RDMA_CM_PORT");

    gettimeofday(&seed, NULL);
    if (NULL != value) {
        rdma_cm_default_port = atoi(value);
        if (rdma_cm_default_port == -1){
            srand(seed.tv_usec);    /* Random seed for the port */
            rdma_cm_default_port = rand() % (65536-1025) + 1024;
        }
        else if (rdma_cm_default_port > 60000 || rdma_cm_default_port <= 1024) {
            rdma_cm_default_port = MPIDI_CH3I_RDMA_CM_DEFAULT_BASE_LISTEN_PORT + pg_rank;
            fprintf(stderr, "Invalid port number: %d, using %d\n",
                    atoi(value), rdma_cm_default_port);
        }
    }
    else {
        srand(seed.tv_usec);    /* Random seed for the port */
        rdma_cm_default_port = rand() % (65536 - 1025) + 1024;
    }

    return rdma_cm_default_port;
}

int rdma_cm_create_qp(int cm_rank, int rail_index, int one_sided)
{
    struct ibv_qp_init_attr init_attr;
    int hca_index, ret;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_VC_t  *vc;
    struct rdma_cm_id *cmid;
    struct ibv_cq *current_cq;

    MPIDI_PG_Get_vc(cached_pg, cm_rank, &vc);

    hca_index = rail_index / (rdma_num_ports * rdma_num_qp_per_port);

    /* Create CM_ID */
    if (one_sided){
	cmid = vc->mrail.rails[rail_index].cm_ids_1sc;
    }else{
        cmid = vc->mrail.rails[rail_index].cm_ids;
    }

    if (one_sided){
	current_cq = proc->cq_hndl_1sc[hca_index];
    }else{
	current_cq = proc->cq_hndl[hca_index];
    }

    {
	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_recv_sge = rdma_default_max_sg_list;
	init_attr.cap.max_send_sge = rdma_default_max_sg_list;
	init_attr.cap.max_inline_data = rdma_max_inline_size;
	
	init_attr.cap.max_send_wr = rdma_default_max_wqe;
	init_attr.send_cq = current_cq;
	init_attr.recv_cq = current_cq;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.sq_sig_all = 0;
    }

    /* SRQ based? */
    if (proc->has_srq && !one_sided) {
	init_attr.cap.max_recv_wr = 0;
	init_attr.srq = proc->srq_hndl[hca_index];
    } else {
	init_attr.cap.max_recv_wr = rdma_default_max_wqe;
    }

    ret = rdma_create_qp(cmid, proc->ptag[hca_index], &init_attr);
    if (ret){
	ibv_error_abort(IBV_RETURN_ERR,
			"Error creating qp using rdma_cm.  %d [cmid: %p, pd: %p, cq: %p] \n",
			ret, cmid, proc->ptag[hca_index], current_cq);
    }

    /* Save required handles */
    if (one_sided){
	vc->mrail.rails[rail_index].qp_hndl_1sc = cmid->qp;
    }
    else{
	vc->mrail.rails[rail_index].qp_hndl = cmid->qp;
	vc->mrail.rails[rail_index].cq_hndl = current_cq;
    }

    vc->mrail.rails[rail_index].nic_context = cmid->verbs;
    vc->mrail.rails[rail_index].hca_index = hca_index;
    vc->mrail.rails[rail_index].port = 1;

    return ret;
}

int *rdma_cm_get_hostnames(int pg_rank, int pg_size)
{
    int *hosts;
    int error, i, j;
    int length = 64;
    char rank[16];
    char buffer[length];
    int key_max_sz;
    int val_max_sz;
    char *key;
    char *val;

    hosts = (int *) malloc (pg_size * MAX_NUM_HCAS * sizeof(int));
    if (!hosts){
	ibv_error_abort(IBV_RETURN_ERR,
			"Memory allocation error\n");
    }
    rdma_cm_host_list = hosts;
    
    sprintf(rank, "ip%d ", pg_rank);
    sprintf(buffer, "%d-%d-%d-%d-%d ", 
	    rdma_base_listen_port[pg_rank],
	    rdma_cm_local_ips[0], rdma_cm_local_ips[1],
	    rdma_cm_local_ips[2], rdma_cm_local_ips[3]);
    DEBUG_PRINT("[%d] message to be sent: %s\n", pg_rank, buffer);

    error = PMI_KVS_Get_key_length_max(&key_max_sz);
    key = MPIU_Malloc(key_max_sz+1);
    PMI_KVS_Get_value_length_max(&val_max_sz);
    val = MPIU_Malloc(val_max_sz+1);

    if (key == NULL || val == NULL) {
	fprintf(stderr, "Error allocating memory\n");
    }

    MPIU_Strncpy(key, rank, 16);
    MPIU_Strncpy(val, buffer, length);
    error = PMI_KVS_Put(cached_pg->ch.kvs_name, key, val);
    if (error != 0) {
	ibv_error_abort(IBV_RETURN_ERR,
			"PMI put failed\n");
    }

    error = PMI_KVS_Commit(cached_pg->ch.kvs_name);
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
	sprintf(rank, "ip%d ", i);
	MPIU_Strncpy(key, rank, 16);
	error = PMI_KVS_Get(cached_pg->ch.kvs_name, key, val, val_max_sz);
        if (error != 0) {
            ibv_error_abort(IBV_RETURN_ERR,
                            "PMI Lookup name failed\n");
        }
	MPIU_Strncpy(buffer, val, length);

	sscanf(buffer, "%d-%d-%d-%d-%d ", 
		&rdma_base_listen_port[i],
		&rdma_cm_host_list[i*rdma_num_hcas], &rdma_cm_host_list[i*rdma_num_hcas + 1],
		&rdma_cm_host_list[i*rdma_num_hcas + 2], &rdma_cm_host_list[i*rdma_num_hcas + 3]);
    }

    /* Find smp processes */
    for (i = 0; i < pg_size; i++){
	if (pg_rank == i)
	    continue;
	if (hosts[i * rdma_num_hcas] == hosts[pg_rank * rdma_num_hcas])
	    num_smp_peers++;
    }
    DEBUG_PRINT("Number of SMP peers for %d is %d\n", pg_rank, 
		num_smp_peers);

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

    while ((fscanf(fp_port, "%s\n", ip)) != EOF){
	rdma_cm_local_ips[i] = inet_addr(ip);
	i++;
    }
    fclose(fp_port);

    return i;
}

int rdma_cm_connect_to_server(int rrank, int ipnum, int rail_index, int one_sided){
    int ret = 0;
    struct sockaddr_in sin;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    MPIDI_PG_Get_vc(cached_pg, rrank, &vc);

    if (one_sided)
        ret = rdma_create_id(proc->cm_channel, &(vc->mrail.rails[rail_index].cm_ids_1sc), proc, RDMA_PS_TCP);
    else
	ret = rdma_create_id(proc->cm_channel, &(vc->mrail.rails[rail_index].cm_ids), proc, RDMA_PS_TCP);

    if (ret) {
        ibv_error_abort(IBV_RETURN_ERR,
                        "rdma_create_id error %d\n", ret);
    }

    /* Resolve addr */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ipnum;
    sin.sin_port = rdma_base_listen_port[rrank];
    if (one_sided){
	ret = rdma_resolve_addr(vc->mrail.rails[rail_index].cm_ids_1sc, NULL, (struct sockaddr *) &sin, rdma_cm_arp_timeout);
    }
    else {
	ret = rdma_resolve_addr(vc->mrail.rails[rail_index].cm_ids, NULL, (struct sockaddr *) &sin, rdma_cm_arp_timeout);
    }

    if (ret) {
        ibv_error_abort(IBV_RETURN_ERR,
                        "rdma_resolve_addr error %d\n", ret);
    }

    DEBUG_PRINT("Active connect initiated for %d [ip: %d:%d] [rail %d] [1sc %d]\n", 
		rrank, ipnum, rdma_base_listen_port[rrank], rail_index, one_sided);
    return ret;
}

int rdma_cm_init_pd_cq(){
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    int i, pg_rank;

    PMI_Get_rank(&pg_rank);
    rdma_cm_get_contexts();

    for (i = 0; i < rdma_num_hcas; i++){

        /* Allocate the protection domain for the HCA */
	proc->ptag[i] = ibv_alloc_pd(proc->nic_context[i]);
	if (!proc->ptag[i]) {
	    fprintf(stderr, "Failed to allocate pd %d\n", i);
	    ibv_error_abort(GEN_EXIT_ERR, "Error allocating PD\n");
	}

        /* Allocate the completion queue handle for the HCA */
        if(rdma_use_blocking) {
            proc->comp_channel[i] =
                ibv_create_comp_channel(proc->nic_context[i]);

            if(!proc->comp_channel[i]) {
		fprintf(stderr, "Create comp channel failed in rdma_cm.c\n");
		ibv_error_abort(GEN_EXIT_ERR, "Error comp channel");
            }

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);

            if (!proc->cq_hndl[i]) {
		fprintf(stderr, "Create CQ failed in rdma_cm.c\n");
		ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
            }

            if (ibv_req_notify_cq(proc->cq_hndl[i], 0)) {
                fprintf(stderr, "Request notify for CQ failed in rdma_cm.c\n");
                ibv_error_abort(GEN_EXIT_ERR, "cannot request cq notification");
            }
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);

            if (!proc->cq_hndl[i]) {
		fprintf(stderr, "Create CQ failed in rdma_cm.c\n");
		ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
            }
        }

	if (proc->has_srq)
	    if (!proc->srq_hndl[i]){
		proc->srq_hndl[i] = create_srq(proc, i);
	    }

	if (proc->has_one_sided) {
	    proc->cq_hndl_1sc[i] = ibv_create_cq(proc->nic_context[i],
						 rdma_default_max_cq_size, NULL, NULL, 0);
	    if (!proc->cq_hndl_1sc[i]) {
		fprintf(stderr, "Create one-sided CQ failed in rdma_cm.c\n");
		ibv_error_abort(GEN_EXIT_ERR, "Error Creating one-sided CQ\n");
	    }
	}
	DEBUG_PRINT("[%d][rail %d] proc->ptag %p, proc->cq_hndl %p, proc->srq_hndl %p\n",
		    pg_rank, i, proc->ptag[i], proc->cq_hndl[i], proc->srq_hndl[i]);
    }

    return 0;
}

int get_remote_rank(struct rdma_cm_id *cmid)
{
    int pg_size, pg_rank, i, rail_index = 0;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (i = 0; i < pg_size; i++){
	if ( pg_rank == i)
	    continue;
	MPIDI_PG_Get_vc(cached_pg, i, &vc);
	for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
	    if (cmid == vc->mrail.rails[rail_index].cm_ids)
		return i;
	    if (proc->has_one_sided){
		if (cmid == vc->mrail.rails[rail_index].cm_ids_1sc)
		    return i;
	    }
	}
    }
    return -1;
}

int get_remote_rail(struct rdma_cm_id *cmid)
{
    int pg_size, pg_rank, i, rail_index = 0;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (i = 0; i < pg_size; i++){
	if ( pg_rank == i)
	    continue;
	MPIDI_PG_Get_vc(cached_pg, i, &vc);
	for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
	    if (cmid == vc->mrail.rails[rail_index].cm_ids)
		return rail_index;
	    if (proc->has_one_sided){
		if (cmid == vc->mrail.rails[rail_index].cm_ids_1sc)
		    return rail_index;
	    }
	}
    }
    return -1;
}

int get_remote_qp_type(struct rdma_cm_id *cmid)
{
    int pg_size, pg_rank, i, rail_index = 0;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (i = 0; i < pg_size; i++){
	if ( pg_rank == i)
	    continue;
	MPIDI_PG_Get_vc(cached_pg, i, &vc);
	for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
	    if (cmid == vc->mrail.rails[rail_index].cm_ids)
		return 0;
	}
    }

    if (proc->has_one_sided){
	for (i = 0; i < pg_size; i++){
	    if ( pg_rank == i)
		continue;
	    MPIDI_PG_Get_vc(cached_pg, i, &vc);
	    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
		if (cmid == vc->mrail.rails[rail_index].cm_ids_1sc)
		    return 1;
	    }
	}
    }

    return -1;
}

void ib_finalize_rdma_cm(int pg_rank, int pg_size)
{
    int i, rail_index = 0;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    free(rdma_base_listen_port);
    free(rdma_cm_accept_count); 

    if ((num_smp_peers + 1) < pg_size){

	for (i = 0; i < pg_size; i++){
	    if (i == pg_rank)
		continue;
	    if (rdma_cm_host_list[i * rdma_num_hcas] == rdma_cm_host_list[pg_rank * rdma_num_hcas])
		continue;
	    
	    MPIDI_PG_Get_vc(cached_pg, i, &vc);
	    if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
		for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
		    if (vc->mrail.rails[rail_index].cm_ids != NULL) {
			rdma_disconnect(vc->mrail.rails[rail_index].cm_ids);
			rdma_destroy_qp(vc->mrail.rails[rail_index].cm_ids);
		    }
		    if (proc->has_one_sided){
			rdma_disconnect(vc->mrail.rails[rail_index].cm_ids_1sc);
			rdma_destroy_qp(vc->mrail.rails[rail_index].cm_ids_1sc);
		    }
		}
	    }
	}
	
	for (i = 0; i < rdma_num_hcas; i++) {
	    if (MPIDI_CH3I_RDMA_Process.cq_hndl[i])
		ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
	    if (MPIDI_CH3I_RDMA_Process.has_one_sided)
		if(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[i])
		    ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[i]);
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
	    if (rdma_cm_host_list[i * rdma_num_hcas] == rdma_cm_host_list[pg_rank * rdma_num_hcas])
		continue;

	    MPIDI_PG_Get_vc(cached_pg, i, &vc);
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
		for (rail_index = 0; rail_index < rdma_num_rails; rail_index++){
		    if (vc->mrail.rails[rail_index].cm_ids != NULL)
			rdma_destroy_id(vc->mrail.rails[rail_index].cm_ids);
		    if (proc->has_one_sided)
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
