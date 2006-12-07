/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2006, The Ohio State University. All rights
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

int num_smp_peers = 0;

#ifdef RDMA_CM

#undef DEBUG_PRINT
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


int *rdma_base_listen_port;
int *rdma_cm_host_list;
volatile int rdma_cm_finalized = 0;

char *init_message_buf;		/* Used for message exchange in RNIC case */
struct ibv_mr *init_mr;
struct ibv_sge init_send_sge;
struct ibv_recv_wr init_rwr;
struct ibv_send_wr init_swr;

/* Handle the connection events */
int ib_cma_event_handler(struct rdma_cm_id *cma_id,
			 struct rdma_cm_event *event);

/* Thread to poll and handle CM events */
void *cm_thread(void *arg);

/* Bind to a random port between 12000 and 18000 */
int bind_listen_port(); 

/* Get user defined port number */
int get_base_listen_port();

/* Initialize listen cm_id and active side cm_ids */
int create_cm_ids(struct MPIDI_CH3I_RDMA_Process_t *proc, 
		  int pg_rank, int pg_size);

/* Obtain the information of local RNIC IP from the mv2.conf file */
int rdma_cm_get_local_ip();

/* Initiate single active connect request */
int rdma_cm_connect_to_server(int rank, int ipnum);

/* create qp's for a ongoing connection request */
int rdma_cm_create_qp(int rank);

/* Initialize pd and cq associated with one rail */
int rdma_cm_init_pd_cq(struct rdma_cm_id *cmid);

/* Get the rank of an active connect request */
int get_remote_rank(struct rdma_cm_id *cmid);

/* Exchange the base port of rank 0 with all other processes */
int exchange_ports(int port, int rank, int pg_size);

/* Exchange init messages for iWARP compliance */
int init_messages(int *hosts, int pg_rank, int pg_size);

/* Initialize the resources required for iWARP compilance */
int rnic_init();



/* RDMA_CM specific method implementations */

int ib_cma_event_handler(struct rdma_cm_id *cma_id,
			  struct rdma_cm_event *event)
{
    int ret = 0, rank, pg_rank, rail_index = 0;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_VC_t  *vc;
    struct rdma_conn_param conn_param;

    PMI_Get_rank(&pg_rank);

    switch (event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:

	ret = rdma_resolve_route(cma_id, 2000);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_resolve_route error %d\n", ret);
	}

	break;
    case RDMA_CM_EVENT_ROUTE_RESOLVED:

	/* Create qp */
	rank = get_remote_rank(cma_id);
	if (rank < 0)
	    fprintf(stderr, "Unexpected error occured\n");
	rdma_cm_create_qp(rank);
	
	/* Connect to remote node */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	conn_param.retry_count = 10;
	conn_param.private_data_len = sizeof(int);
	conn_param.private_data = malloc(sizeof(int));

	if (!conn_param.private_data)
	    fprintf(stderr, "Error allocating memory\n");
	*((int *)conn_param.private_data) = pg_rank;

	ret = rdma_connect(cma_id, &conn_param);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_connect error %d\n", ret);
	}

	break;
    case RDMA_CM_EVENT_CONNECT_REQUEST:

	if (!event->private_data_len){
            ibv_error_abort(IBV_RETURN_ERR,
			    "Error obtaining remote rank from event private data\n");
	}
	
	rank = *((int *)event->private_data);
        MPIDI_PG_Get_vc(cached_pg, rank, &vc);
	vc->mrail.rails[rail_index].cm_ids = cma_id;

	/* Create qp */
        rdma_cm_create_qp(rank);
	
	/* Accept remote connection - passive connect */
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 1;
	conn_param.initiator_depth = 1;
	ret = rdma_accept(cma_id, &conn_param);
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_accept error: %d\n", ret);
	}

#ifdef RDMA_CM_RNIC
	{
	    struct ibv_recv_wr *bad_wr;
	    ibv_post_recv(cma_id->qp, &init_rwr, &bad_wr);
	    DEBUG_PRINT("[%d] Posting recv \n", pg_rank);
	}
#endif
	break;
    case RDMA_CM_EVENT_ESTABLISHED:

#ifdef RDMA_CM_RNIC
        rank = get_remote_rank(cma_id);
	if (rank < pg_rank){
	    struct ibv_send_wr *bad_wr;
	    DEBUG_PRINT("[%d] Posting send to %d\n", pg_rank, rank);
	    ibv_post_send(cma_id->qp, &init_swr, &bad_wr);
	}
	    
#endif
	sem_post(&proc->rdma_cm);
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
    case RDMA_CM_EVENT_REJECTED:
	ibv_error_abort(IBV_RETURN_ERR,
			"rdma cma event %d, error %d\n", event->event,
			event->status);
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
	if (ret && !rdma_cm_finalized) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_get_cm_event err %d\n", ret);
	}
	DEBUG_PRINT("rdma cm event: %d\n", event->event);
	ret = ib_cma_event_handler(event->id, event);
	rdma_ack_cm_event(event);
    }
}

void ib_init_rdma_cm(struct MPIDI_CH3I_RDMA_Process_t *proc,
			    int pg_rank, int pg_size)
{
    int i = 0;
    char hostname[64];

    assert (rdma_num_rails == 1);

    gethostname(hostname, 64);
    DEBUG_PRINT("%s initializing...\n", hostname);

    sem_init(&(proc->rdma_cm), 0, 0);
    proc->cm_channel = rdma_create_event_channel();
    init_mr = NULL;

    rdma_base_listen_port = (int *) malloc (pg_size * sizeof(int));
    if (!rdma_base_listen_port) {
        ibv_error_abort(IBV_RETURN_ERR,
			"Memory allocation error\n");
    }

    for (i = 0; i < rdma_num_ports; i++){
	proc->ptag[i] = NULL;
	proc->cq_hndl[i] = NULL;
    }

    if (!proc->cm_channel) {
	ibv_error_abort(IBV_RETURN_ERR,
			"Cannot create rdma_create_event_channel\n");
    }

    /* Create all the active connect cm_ids */
    create_cm_ids(proc, pg_rank, pg_size);

    /* Create the connection management thread */
    pthread_create(&proc->cmthread, NULL, cm_thread, NULL);

    /* Create port to listen for connections */
    /* Find a base port, relay it to the peers and listen */
    rdma_base_listen_port[pg_rank] = bind_listen_port(pg_rank);

    exchange_ports(rdma_base_listen_port[pg_rank], pg_rank, pg_size);

}

int rdma_cm_connect_all(int *hosts, int pg_rank, int pg_size)
{
    int i, ret;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    /* Initiate non-smp active connect requests */
    for (i = 0; i < pg_rank; i++){
	if (hosts[i] != hosts[pg_rank])
	    ret = rdma_cm_connect_to_server(i, hosts[i]);
    }

    /* Wait for all non-smp connections to complete */
    for (i = 0; i < pg_size - 1 - num_smp_peers; i++){
	sem_wait(&proc->rdma_cm);
    }

#ifdef RDMA_CM_RNIC
    init_messages(hosts, pg_rank, pg_size);
#endif

    rdma_cm_host_list = hosts;
    /* RDMA CM Connection Setup Complete */
    DEBUG_PRINT("RDMA CM based connection setup complete\n");
    return 0;
}

int bind_listen_port(int pg_rank)
{
    struct sockaddr_in sin;
    int ret, count = 0;
    char ipaddr[16];
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    rdma_base_listen_port[pg_rank] = get_base_listen_port(pg_rank);

    sprintf(ipaddr, "0.0.0.0");	/* Listen on all devices */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ipaddr);
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

    ret = rdma_listen(proc->cm_listen_id, 10);
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
            rdma_cm_default_port = MPIDI_CH3I_RDMA_CM_DEFAULT_BASE_LISTEN_PORT;
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

int create_cm_ids(struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank, int pg_size)
{
    int ret, i; 
    MPIDI_VC_t  *vc;
    int rail_index = 0;

    /* Create a port for listening */
#if (defined(RDMA_CM_RNIC) || defined(RDMA_CM_OFED_1_0))
    ret = rdma_create_id(proc->cm_channel, &proc->cm_listen_id, proc);
#else
    ret = rdma_create_id(proc->cm_channel, &proc->cm_listen_id, proc, RDMA_PS_TCP);
#endif
    if (ret) {
	ibv_error_abort(IBV_RETURN_ERR,
			"rdma_create_id error %d\n", ret);
    }

    for (i = 0; i < pg_rank; i++){
        MPIDI_PG_Get_vc(cached_pg, i, &vc);
#if (defined(RDMA_CM_RNIC) || defined(RDMA_CM_OFED_1_0))
	ret = rdma_create_id(proc->cm_channel, &(vc->mrail.rails[rail_index].cm_ids), proc);
#else
	ret = rdma_create_id(proc->cm_channel, &(vc->mrail.rails[rail_index].cm_ids), proc, RDMA_PS_TCP);
#endif
	if (ret) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "rdma_create_id error %d\n", ret);
	}
    }

    proc->nic_context[0] = proc->cm_listen_id->verbs;

    return 0;
}

int rdma_cm_create_qp(int cm_rank)
{
    struct ibv_qp_init_attr init_attr;
    int hca_index = 0, rail_index = 0, ret;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_VC_t  *vc;
    struct rdma_cm_id *cmid;

    MPIDI_PG_Get_vc(cached_pg, cm_rank, &vc);
    cmid = vc->mrail.rails[rail_index].cm_ids;

    rdma_cm_init_pd_cq(cmid);

    {
	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.cap.max_recv_sge = rdma_default_max_sg_list;
	init_attr.cap.max_send_sge = rdma_default_max_sg_list;
	init_attr.cap.max_inline_data = rdma_max_inline_size;
	
	init_attr.cap.max_send_wr = rdma_default_max_wqe;
	init_attr.send_cq = proc->cq_hndl[hca_index];
	init_attr.recv_cq = proc->cq_hndl[hca_index];
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.sq_sig_all = 0;
    }

    /* SRQ based? */
    if (proc->has_srq) {
	init_attr.cap.max_recv_wr = 0;
	init_attr.srq = proc->srq_hndl[hca_index];
    } else {
	init_attr.cap.max_recv_wr = rdma_default_max_wqe;
    }

    ret = rdma_create_qp(cmid, proc->ptag[rail_index], &init_attr);
    if (ret){
	ibv_error_abort(IBV_RETURN_ERR,
			"error creating qp using rdma_cm.  %d\n", ret);
    }

    /* Save required handles */
    vc->mrail.rails[rail_index].qp_hndl = vc->mrail.rails[rail_index].cm_ids->qp;
    vc->mrail.rails[rail_index].cq_hndl = proc->cq_hndl[rail_index];
    vc->mrail.rails[rail_index].nic_context = vc->mrail.rails[rail_index].cm_ids->verbs;
    vc->mrail.rails[rail_index].hca_index = hca_index;
    vc->mrail.rails[rail_index].port = 1;

    return ret;
}

int *rdma_cm_get_hostnames(int pg_rank, int pg_size)
{
    int *hosts;
    int error, i;
    char rank[16];
    char port[16];
    int key_max_sz;
    int val_max_sz;
    char *key;
    char *val;

    sprintf(rank, "%d ", pg_rank);
    sprintf(port, "%d ", rdma_cm_get_local_ip());

    hosts = (int *) malloc (pg_size * sizeof(int));
    if (!hosts){
	ibv_error_abort(IBV_RETURN_ERR,
			"Memory allocation error\n");
    }

    error = PMI_KVS_Get_key_length_max(&key_max_sz);
    assert(error == PMI_SUCCESS);
    key = MPIU_Malloc(16);
    PMI_KVS_Get_value_length_max(&val_max_sz);
    assert(error == PMI_SUCCESS);
    val = MPIU_Malloc(16);

    if (key == NULL || val == NULL) {
	fprintf(stderr, "Error allocating memory\n");
    }

    MPIU_Strncpy(key, rank, 16);
    MPIU_Strncpy(val, port, 16);
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
	sprintf(rank, "%d ", i);
	MPIU_Strncpy(key, rank, 16);
	error = PMI_KVS_Get(cached_pg->ch.kvs_name, key, val, 16);
        if (error != 0) {
            ibv_error_abort(IBV_RETURN_ERR,
                            "PMI Lookup name failed\n");
        }
	MPIU_Strncpy(port, val, 16);
	hosts[i] = atoi(port);
    }

    /* Find smp processes */
    for (i = 0; i < pg_size; i++){
	if (pg_rank == i)
	    continue;
	if (hosts[i] == hosts[pg_rank])
	    num_smp_peers++;
    }
    DEBUG_PRINT("Number of SMP peers for %d is %d\n", pg_rank, 
		num_smp_peers);

    return hosts;
}

/* Gets the ip address in network byte order */
int rdma_cm_get_local_ip(){
    FILE *fp_port;
    char ip[32];
    char fname[512];
    int ipnum;

    sprintf(fname, "/etc/mv2.conf");
    fp_port = fopen(fname, "r");

    if (NULL == fp_port){
	ibv_error_abort(GEN_EXIT_ERR, "Error opening file \"/etc/mv2.conf\". Local rdma_cm address required in this file.\n");	
    }

    fscanf(fp_port, "%s\n", ip);
    ipnum = inet_addr(ip);
    fclose(fp_port);

    return ipnum;
}

int rdma_cm_connect_to_server(int rank, int ipnum){
    int ret = 0;
    struct sockaddr_in sin;
    MPIDI_VC_t  *vc;
    int rail_index = 0;

    MPIDI_PG_Get_vc(cached_pg, rank, &vc);

    /* Resolve addr */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ipnum;
    sin.sin_port = rdma_base_listen_port[rank];
    ret = rdma_resolve_addr(vc->mrail.rails[rail_index].cm_ids, NULL, (struct sockaddr *) &sin,
			    2000);

    DEBUG_PRINT("Active connect initiated for %d\n", rank);
    return ret;
}

int rdma_cm_init_pd_cq(struct rdma_cm_id *cmid){
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    int i;

    for (i = 0; i < rdma_num_ports; i++){
	proc->nic_context[i] = cmid->verbs;

        /* Allocate the protection domain for the HCA */
	if (!proc->ptag[i]) {
	    proc->ptag[i] = ibv_alloc_pd(cmid->verbs);
	    if (!proc->ptag[i]) {
		fprintf(stderr, "Fail to alloc pd number %d\n", i);
		ibv_error_abort(GEN_EXIT_ERR, "Error allocating PD\n");
	    }
        }

        /* Allocate the completion queue handle for the HCA */
	if (!proc->cq_hndl[i]){
	    proc->cq_hndl[i] = ibv_create_cq(cmid->verbs,
					     rdma_default_max_cq_size, NULL, NULL, 0);
	    if (!proc->cq_hndl[i]) {
		fprintf(stderr, "cannot create cq\n");
		ibv_error_abort(GEN_EXIT_ERR, "Error allocating CQ");
	    }
	}

	if (proc->has_srq)
	    if (!proc->srq_hndl[i]){
		proc->srq_hndl[i] = create_srq(proc, i);
	    }

	if (proc->has_one_sided) {
	    if (!proc->cq_hndl_1sc[i]){
		proc->cq_hndl_1sc[i] = ibv_create_cq(cmid->verbs,
						     rdma_default_max_cq_size, NULL, NULL, 0);
		if (!proc->cq_hndl_1sc[i]) {
		    ibv_error_abort(GEN_EXIT_ERR, "Error Creating onesided CQ\n");
		}
	    }
	}
    }

#ifdef RDMA_CM_RNIC
    if (!init_mr) 
	rnic_init();
#endif

    return 0;
}

int get_remote_rank(struct rdma_cm_id *cmid)
{
    int pg_size, pg_rank, i, rail_index = 0;
    MPIDI_VC_t  *vc;

    PMI_Get_size(&pg_size);
    PMI_Get_rank(&pg_rank);

    for (i = 0; i < pg_size; i++){
	if ( pg_rank == i)
	    continue;
	MPIDI_PG_Get_vc(cached_pg, i, &vc);
	if (cmid == vc->mrail.rails[rail_index].cm_ids)
	    return i;
    }
    return -1;
}

int exchange_ports(int base_port, int pg_rank, int pg_size)
{
    int error, i;
    char rank[16];
    char port[16];
    char *key;
    char *val;

    key = MPIU_Malloc(16);
    val = MPIU_Malloc(16);

    if (key == NULL || val == NULL) {
        fprintf(stderr, "Error allocating memory\n");
    }

    {
	sprintf(rank, "port%d ", pg_rank);
	sprintf(port, "%d ", base_port);
	
        MPIU_Strncpy(key, rank, 16);
        MPIU_Strncpy(val, port, 16);
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
    }

    {
        error = PMI_Barrier();
        if (error != 0) {
            ibv_error_abort(IBV_RETURN_ERR,
                            "PMI Barrier failed\n");
        }
    }

    for (i = 0; i < pg_size; i++) {
	sprintf(rank, "port%d ", i);
	MPIU_Strncpy(key, rank, 16);
	error = PMI_KVS_Get(cached_pg->ch.kvs_name, key, val, 16);
	if (error != 0) {
	    ibv_error_abort(IBV_RETURN_ERR,
			    "PMI Lookup name failed\n");
	}
	MPIU_Strncpy(port, val, 16);
	rdma_base_listen_port[i] = atoi(port);
	DEBUG_PRINT("Using port (rank i) as: %d\n", rdma_base_listen_port[i]);
    }

    return 0;
}


void ib_finalize_rdma_cm(int pg_rank, int pg_size)
{
    int i, rail_index = 0;
    MPIDI_VC_t  *vc;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    free(rdma_base_listen_port);

    for (i = 0; i < pg_size; i++){

	if (i == pg_rank)
	    continue;

	if (rdma_cm_host_list[i] == rdma_cm_host_list[pg_rank])
	    continue;

	MPIDI_PG_Get_vc(cached_pg, i, &vc);
	rdma_disconnect(vc->mrail.rails[rail_index].cm_ids);
	rdma_destroy_qp(vc->mrail.rails[rail_index].cm_ids);
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
		 ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
	     }
	 }
	 if ((num_smp_peers + 1) < pg_size){
	     deallocate_vbufs(i);
	     while (dreg_evict());
	 }
	 if (MPIDI_CH3I_RDMA_Process.ptag[i])
	     ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
     }

    for (i = 0; i < pg_size; i++){
	if (i == pg_rank)
	    continue;

	if (rdma_cm_host_list[i] == rdma_cm_host_list[pg_rank])
	    continue;

	rdma_destroy_id(vc->mrail.rails[rail_index].cm_ids);
    }

    if (pg_size > 1) {
	rdma_destroy_id(proc->cm_listen_id);
	rdma_cm_finalized = 1;
	rdma_destroy_event_channel(MPIDI_CH3I_RDMA_Process.cm_channel);
    }
    
    DEBUG_PRINT("RDMA CM resources finalized\n");
}

int rnic_init(){
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    int length = 1024;
    int i = 0;

    init_message_buf = (char *) malloc (length * sizeof (char));
    if (!init_message_buf) {
	ibv_error_abort(IBV_RETURN_ERR,
			"error allocating memory\n");
    }
    
    init_mr = ibv_reg_mr(proc->ptag[i], init_message_buf,
		    length, IBV_ACCESS_LOCAL_WRITE);

    init_send_sge.addr = (uint) init_message_buf;
    init_send_sge.length = 4;
    init_send_sge.lkey = init_mr->lkey;

    init_rwr.num_sge = 1;
    init_rwr.sg_list = &init_send_sge;

    init_swr.opcode = IBV_WR_SEND;
    init_swr.send_flags = IBV_SEND_SIGNALED;
    init_swr.num_sge = 1;
    init_swr.sg_list = &init_send_sge;

    return 0;
}

int init_messages(int *hosts, int pg_rank, int pg_size){
    int i = 0;
    int rail_index = 0;
    struct ibv_wc wc;
    int ret = 0;

    DEBUG_PRINT("[%d] Polling for %d completions\n", pg_rank, (pg_size - 1 - num_smp_peers));
    for (i = 0; i < pg_size - 1 - num_smp_peers; i++){ /* Poll for completions */

	while (ret == 0) {
	    ret = ibv_poll_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[rail_index], 1, &wc);
	    if (ret < 0) {
		fprintf(stderr, "Completion error in rdma_cm/iwarp exchange: status %d\n", wc.status);
	    }
	}
	ret = 0;
	DEBUG_PRINT("[%d] completion %d here: sttus: %d\n", pg_rank, wc.opcode, wc.status);
    }

    if (init_mr){
	ibv_dereg_mr(init_mr);
	free (init_message_buf);
    }

    return 0;
}


#endif /* RDMA_CM */
