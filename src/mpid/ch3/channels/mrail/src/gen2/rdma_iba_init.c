/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2008, The Ohio State University. All rights
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
#include "mem_hooks.h"
#include "rdma_impl.h"
#include "vbuf.h"
#include "cm.h"
#include "rdma_cm.h"

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

static int g_cached_pg_rank;

MPIDI_PG_t* g_cached_pg;

static inline int MPIDI_CH3I_PG_Compare_ids(void *id1, void *id2)
{
    return (strcmp((char *) id1, (char *) id2) == 0) ? TRUE : FALSE;
}

static inline int MPIDI_CH3I_PG_Destroy(MPIDI_PG_t * pg, void *id)
{
    if (pg->ch.kvs_name != NULL) {
	MPIU_Free(pg->ch.kvs_name);
    }

    if (id != NULL) {
	MPIU_Free(id);
    }

    return MPI_SUCCESS;
}

static inline uint16_t get_local_lid(struct ibv_context *ctx, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(ctx, port, &attr)) {
	return -1;
    }

    MPIDI_CH3I_RDMA_Process.lmc = attr.lmc;

    return attr.lid;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_init(MPIDI_PG_t * pg, int pg_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_INIT);

    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */

    MPIDI_VC_t* vc = NULL;
    int i = 0;
    int error;
    int rail_index;
    char* key = NULL;
    char* val = NULL;
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];
    char* buf = NULL;
    char tmp_hname[256];
    int mpi_errno = MPI_SUCCESS;
    struct process_init_info *init_info = NULL;
    uint32_t my_hca_type;

    gethostname(tmp_hname, 255);
    g_cached_pg = pg;
    g_cached_pg_rank = pg_rank;
    int pg_size = MPIDI_PG_Get_size(pg);

    /* Reading the values from user first and 
     * then allocating the memory */
    if ((mpi_errno = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);

#if !defined(DISABLE_PTMALLOC)
    if (mvapich2_minit()) {
        MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister = 0;
    }
#else /* !defined(DISABLE_PTMALLOC) */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
    MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister = 0;
#endif /* !defined(DISABLE_PTMALLOC) */

    if(!MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
        rdma_r3_threshold = rdma_r3_threshold_nocache;
    }

    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d\n", rdma_num_qp_per_port,
	    rdma_num_rails);

    init_info = alloc_process_init_info(pg_size, rdma_num_rails);
    if (!init_info) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
                        "**nomem %s", "init_info");
    }

    if (pg_size > 1) {
        /* Check heterogenity */
        mpi_errno = rdma_setup_startup_ring(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }

        my_hca_type = MPIDI_CH3I_RDMA_Process.hca_type;
        rdma_ring_based_allgather(&my_hca_type, sizeof my_hca_type, pg_rank,
                                  init_info->hca_type, pg_size, 
                                  &MPIDI_CH3I_RDMA_Process);
        rdma_param_handle_heterogenity(init_info->hca_type, pg_size);
    }

    if(MPIDI_CH3I_RDMA_Process.has_apm) {
        init_apm_lock(); 
    }

    if (MPIDI_CH3I_RDMA_Process.has_srq)
    {
	if ((mpi_errno = init_vbuf_lock()) != MPI_SUCCESS)
        {
	    MPIU_ERR_POP(mpi_errno);
        }

	MPIDI_CH3I_RDMA_Process.vc_mapping = (MPIDI_VC_t **) MPIU_Malloc(sizeof(MPIDI_VC_t) * pg_size);
    }

    /* the vc structure has to be initialized */

    for (i = 0; i < pg_size; ++i) {

	MPIDI_PG_Get_vc(pg, i, &vc);
	memset(&(vc->mrail), 0, sizeof(vc->mrail));

	/* This assmuption will soon be done with */
	vc->mrail.num_rails = rdma_num_rails;

	if (MPIDI_CH3I_RDMA_Process.has_srq) {
	    MPIDI_CH3I_RDMA_Process.vc_mapping[vc->pg_rank] = vc;
	}
    }

    /* Open the device and create cq and qp's */
    if ((mpi_errno = rdma_iba_hca_init(
        &MPIDI_CH3I_RDMA_Process,
        pg_rank,
        pg_size,
        init_info)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

    if (pg_size > 1) {

	if (!MPIDI_CH3I_RDMA_Process.has_ring_startup) {
	    /*Exchange the information about HCA_lid and qp_num */
	    /* Allocate space for pmi keys and values */
	    error = PMI_KVS_Get_key_length_max(&key_max_sz);
	    if(error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max key length");
	    }

	    ++key_max_sz;
	    key = MPIU_Malloc(key_max_sz);

	    if (key == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "pmi key");
	    }

	    error = PMI_KVS_Get_value_length_max(&val_max_sz);
	    if(error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max value length");
	    }

	    ++val_max_sz;
	    val = MPIU_Malloc(val_max_sz);

	    if (val == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "pmi value");
	    }

	    /* For now, here exchange the information of each LID separately */
	    for (i = 0; i < pg_size; i++)
            {
		if (pg_rank == i)
                {
		    continue;
		}

		/* generate the key and value pair for each connection */
		MPIU_Snprintf(rdmakey, 512, "%08d-%08d", pg_rank, i);
		buf = rdmavalue;

		for (rail_index = 0; rail_index < rdma_num_rails;
			rail_index++) {
		    sprintf(buf, "%08d", init_info->lid[i][rail_index]);
		    DEBUG_PRINT("put my hca %d lid %d\n", rail_index,
			        init_info->lid[i][rail_index]);
		    buf += 8;
		}

                sprintf(buf, "%08d", init_info->hca_type[i]);
                buf += 8;

		/* put the kvs into PMI */
		MPIU_Strncpy(key, rdmakey, key_max_sz);
		MPIU_Strncpy(val, rdmavalue, val_max_sz);
		error = PMI_KVS_Put(pg->ch.kvs_name, key, val);

		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_put", "**pmi_kvs_put %d", error);
		}

		DEBUG_PRINT("after put, before barrier\n");

		error = PMI_KVS_Commit(pg->ch.kvs_name);

		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
		}

	    }
	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", error);
	    }


	    /* Here, all the key and value pairs are put, now we can get them */
	    for (i = 0; i < pg_size; i++)
            {
		if (pg_rank == i) {
		    init_info->lid[i][0] =
			get_local_lid(MPIDI_CH3I_RDMA_Process.
				nic_context[0], rdma_default_port);
                    init_info->hca_type[i] = MPIDI_CH3I_RDMA_Process.hca_type;
		    continue;
		}

		/* generate the key */
		MPIU_Snprintf(rdmakey, 512, "%08d-%08d", i, pg_rank);
		MPIU_Strncpy(key, rdmakey, key_max_sz);
		error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_get", "**pmi_kvs_get %d", error);
		}

		MPIU_Strncpy(rdmavalue, val, val_max_sz);
		buf = rdmavalue;

		for (rail_index = 0; rail_index < rdma_num_rails;
			rail_index++) {
		    sscanf(buf, "%08d", 
                           (unsigned int *)&init_info->lid[i][rail_index]);
		    buf += 8;
		    DEBUG_PRINT("get rail %d, lid %08d\n", rail_index,
			    (int)init_info->lid[i][rail_index]);
		}

                sscanf(buf, "%08d", &init_info->hca_type[i]);
                buf += 8;
	    }

	    /* this barrier is to prevent some process from
	       overwriting values that has not been get yet */
	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", error);
	    }

	    /* STEP 2: Exchange qp_num */
	    for (i = 0; i < pg_size; i++)
            {
		if (pg_rank == i)
                {
		    continue;
		}

		/* generate the key and value pair for each connection */
		MPIU_Snprintf(rdmakey, 512, "%08d-%08d", pg_rank, i);
		buf = rdmavalue;

		for (rail_index = 0; rail_index < rdma_num_rails;
			rail_index++) {
		    sprintf(buf, "%08X", init_info->qp_num_rdma[i][rail_index]);
		    buf += 8;
		    DEBUG_PRINT("target %d, put qp %d, num %08X \n", i,
			    rail_index, init_info->qp_num_rdma[i][rail_index]);
		}

		DEBUG_PRINT("put rdma value %s\n", rdmavalue);
		/* put the kvs into PMI */
		MPIU_Strncpy(key, rdmakey, key_max_sz);
		MPIU_Strncpy(val, rdmavalue, val_max_sz);

		error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_put", "**pmi_kvs_put %d", error);
		}

		error = PMI_KVS_Commit(pg->ch.kvs_name);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
		}
	    }

	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", error);
	    }

	    /* Here, all the key and value pairs are put, now we can get them */
	    for (i = 0; i < pg_size; i++)
            {
		if (pg_rank == i)
                {
		    continue;
		}

		/* generate the key */
		MPIU_Snprintf(rdmakey, 512, "%08d-%08d", i, pg_rank);
		MPIU_Strncpy(key, rdmakey, key_max_sz);
		error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);

		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_get", "**pmi_kvs_get %d", error);
		}
		MPIU_Strncpy(rdmavalue, val, val_max_sz);

		buf = rdmavalue;
		DEBUG_PRINT("get rdmavalue %s\n", rdmavalue);
		for (rail_index = 0; rail_index < rdma_num_rails;
			rail_index++) {
		    sscanf(buf, "%08X", &init_info->qp_num_rdma[i][rail_index]);
		    buf += 8;
		    DEBUG_PRINT("get qp %d,  num %08X \n", i,
                                init_info->qp_num_rdma[i][rail_index]);
		}
	    }

	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", error);
	    }
	    DEBUG_PRINT("After barrier\n");

	    if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
		/* Exchange qp_num */
		for (i = 0; i < pg_size; i++)
                {
		    if (pg_rank == i)
                    {
			continue;
		    }

		    /* generate the key and value pair for each connection */
		    MPIU_Snprintf(rdmakey, 512, "%08d-%08d", pg_rank, i);
		    buf = rdmavalue;

		    for (rail_index = 0; rail_index < rdma_num_rails;
			 rail_index++) {
			sprintf(buf, "%08X", 
                                init_info->qp_num_onesided[i][rail_index]);
			buf += 8;
			DEBUG_PRINT("Put key %s, onesided qp %d, num %08X\n",
			            rdmakey, i,
                                    init_info->qp_num_onesided[i][rail_index]);
		    }

		    /* put the kvs into PMI */
		    DEBUG_PRINT("Put a string %s\n", rdmavalue);
		    MPIU_Strncpy(key, rdmakey, key_max_sz);
		    MPIU_Strncpy(val, rdmavalue, val_max_sz);

		    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
		    if (error != PMI_SUCCESS) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_kvs_put", "**pmi_kvs_put %d", error);
		    }

		    error = PMI_KVS_Commit(pg->ch.kvs_name);
		    if (error != PMI_SUCCESS) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
		    }
		}

		error = PMI_Barrier();
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
		}

		/*
		 * Here, all the key and value pairs are put, now we can get
		 * them
		 */
		for (i = 0; i < pg_size; i++)
                {
		    if (pg_rank == i)
                    {
			continue;
		    }

		    /* generate the key */
		    MPIU_Snprintf(rdmakey, 512, "%08d-%08d", i, pg_rank);
		    MPIU_Strncpy(key, rdmakey, key_max_sz);
		    error =
			PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
		    if (error != PMI_SUCCESS) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_kvs_get", "**pmi_kvs_get %d", error);
		    }

		    MPIU_Strncpy(rdmavalue, val, val_max_sz);
		    DEBUG_PRINT("Get a string %s\n", rdmavalue);
		    buf = rdmavalue;

		    for (rail_index = 0; rail_index < rdma_num_rails;
			    rail_index++) {
			sscanf(buf, "%08X", 
                               &init_info->qp_num_onesided[i][rail_index]);
			buf += 8;
			DEBUG_PRINT("Get key %s, onesided qp %d, num %08X\n",
			            rdmakey, i,
			            init_info->qp_num_onesided[i][rail_index]);
		    }
		}

		error = PMI_Barrier();
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
		}

		MPIU_Free(val);
		MPIU_Free(key);
	    }
	} else {
	    /* Exchange the information about HCA_lid, qp_num, and memory,
	     * With the ring-based queue pair */
	    rdma_ring_boot_exchange(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size, init_info);
	}
    }

    /* Initialize the registration cache */
    dreg_init();

    /* Allocate RDMA Buffers */
    if ((mpi_errno = rdma_iba_allocate_memory(
        &MPIDI_CH3I_RDMA_Process,
	pg_rank,
        pg_size)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    /* Enable all the queue pair connections */
    DEBUG_PRINT
	("Address exchange finished, proceed to enabling connection\n");
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process, pg_rank,
	                        pg_size, init_info);
    DEBUG_PRINT("Finishing enabling connection\n");

    /*barrier to make sure queues are initialized before continuing */
    /*  error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
     *  */
    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", error);
    }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; i++)
    {
	if (i == pg_rank)
        {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);
	vc->state = MPIDI_VC_STATE_ACTIVE;
	MRAILI_Init_vc(vc, pg_rank);
    }

    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", error);
    }

    if (pg_size > 1) {
	/* clean up the bootstrap qps and free memory */
        if ((mpi_errno = rdma_cleanup_startup_ring(&MPIDI_CH3I_RDMA_Process)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    DEBUG_PRINT("Finished MPIDI_CH3I_RDMA_init()\n");

fn_exit:
    if (init_info)
    {
        free_process_init_info(init_info, pg_size);
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Flush
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Flush()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);
    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int i, pg_rank, pg_size, rail;
    int mpi_errno = MPI_SUCCESS;

    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    for (i = 0; i < pg_size; i++)
    {
        if (i == pg_rank)
        {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);

	/* Skip SMP VCs */
	if (SMP_INIT && (vc->smp.local_nodes >= 0))
        {
	    continue;
        }

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
        {
	    continue;
	}

	for (rail = 0; rail < vc->mrail.num_rails; rail++)
        {
	    while (0 != vc->mrail.srp.credits[rail].backlog.len)
            {
                if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
	    }

	    while (NULL != vc->mrail.rails[rail].ext_sendq_head)
            {
                if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
                {
		    MPIU_ERR_POP(mpi_errno);
                }
	    }
	}
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_finalize()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);

    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    int error;
    int pg_rank;
    int pg_size;
    int i;
    int rail_index;
    int hca_index;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int err;
    int mpi_errno = MPI_SUCCESS;

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    /* make sure everything has been sent */
    MPIDI_CH3I_MRAILI_Flush();
    for (i = 0; i < pg_size; i++) {
	if (i == pg_rank) {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);

	for (rail_index = 0; rail_index < vc->mrail.num_rails;
		rail_index++) {
	    while((rdma_default_max_send_wqe) != 
		    vc->mrail.rails[rail_index].send_wqes_avail) {
		MPIDI_CH3I_Progress_test();
	    } 
	}
    }

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", error);
    }

    for (i = 0; i < pg_size; i++) {
	if (i == pg_rank) {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);

	for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
	    if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index]) {
		err = ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
		if (err)
		    MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
	    }
	    if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]) {
		err = ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
		if (err)
		    MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
	    }
	}

	if (vc->mrail.rfp.RDMA_send_buf_DMA)
	    MPIU_Free(vc->mrail.rfp.RDMA_send_buf_DMA);
	if (vc->mrail.rfp.RDMA_recv_buf_DMA)
	    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf_DMA);
	if (vc->mrail.rfp.RDMA_send_buf)
	    MPIU_Free(vc->mrail.rfp.RDMA_send_buf);
	if (vc->mrail.rfp.RDMA_recv_buf)
	    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf);
    }

    /* STEP 2: destroy all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
	MPIDI_PG_Get_vc(pg, i, &vc);

	if (pg_rank == i) {
	    continue;
	}

	for (rail_index = 0; rail_index < vc->mrail.num_rails;
		rail_index++) {
	    err = ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
	    if (err)
		MPIU_Error_printf("Failed to destroy QP (%d)\n", err);

	}

#ifdef USE_HEADER_CACHING
	MPIU_Free(vc->mrail.rfp.cached_incoming);
	MPIU_Free(vc->mrail.rfp.cached_outgoing);
#endif
    }

    /* STEP 3: release all the cq resource, 
     * release all the unpinned buffers, 
     * release the ptag and finally, 
     * release the hca */

    for (i = 0; i < rdma_num_hcas; i++) {
	if (MPIDI_CH3I_RDMA_Process.has_srq) {
	    pthread_cond_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_cond[i]);
	    pthread_mutex_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i]);
	    pthread_mutex_lock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
	    pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
	    pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i], NULL);
	    err = ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
	    pthread_mutex_unlock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
	    pthread_mutex_destroy(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
	    if (err)
		MPIU_Error_printf("Failed to destroy SRQ (%d)\n", err);
	}

	err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
	if (err)
	    MPIU_Error_printf("Failed to destroy CQ (%d)\n", err);

	if(rdma_use_blocking) {
	    err = ibv_destroy_comp_channel(MPIDI_CH3I_RDMA_Process.comp_channel[i]);
	    if(err)
		MPIU_Error_printf("Failed to destroy CQ channel (%d)\n", err);
	}

	deallocate_vbufs(i);

	while (dreg_evict());

	err = ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);

	if (err)  {
	    MPIU_Error_printf("[%d] Failed to dealloc pd (%s)\n", 
		    pg_rank, strerror(errno));
	}

	err = ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);

	if (err) {
	    MPIU_Error_printf("[%d] Failed to close ib device (%s)\n", 
		    pg_rank, strerror(errno));
	}

    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Init(MPIDI_PG_t * pg, int pg_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_CH3I_CM_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_CH3I_CM_INIT);

    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    MPIDI_VC_t *vc;
    int pg_size;
    int i, error;
    int key_max_sz;
    int val_max_sz;
    int *hosts = NULL;
    uint32_t *ud_qpn_all;
    uint32_t ud_qpn_self;
    uint16_t *lid_all;
    uint32_t *hca_type_all;
    uint32_t my_hca_type;
    char tmp_hname[256];
    int mpi_errno = MPI_SUCCESS;

#ifndef DISABLE_PTMALLOC
    if(mvapich2_minit()) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "Error initializing MVAPICH2 malloc library");
    }
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    gethostname(tmp_hname, 255);
    g_cached_pg = pg;
    g_cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    /* Reading the values from user first and 
     * then allocating the memory */
    mpi_errno = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process);
    if (mpi_errno) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		"**fail %s", "rdma_get_control_parameters");
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);

    /*
     * TODO Local Memory, must free if error occurs
     */
    ud_qpn_all = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));
    lid_all = (uint16_t *) MPIU_Malloc(pg_size * sizeof(uint16_t));
    hca_type_all = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));
    if (!ud_qpn_all || !lid_all || !hca_type_all) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
                "**nomem %s", "structure to exchange information");
    }

    for(i = 0; i < rdma_num_hcas; i++) {
        pthread_mutex_init(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i], 0); 
    }

    if (pg_size > 1
#if defined(RDMA_CM)
        && !MPIDI_CH3I_RDMA_Process.use_rdma_cm
#endif /* defined(RDMA_CM) */
    ) {
        rdma_setup_startup_ring(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
        my_hca_type = MPIDI_CH3I_RDMA_Process.hca_type;
        rdma_ring_based_allgather(&my_hca_type, sizeof my_hca_type, pg_rank,
                hca_type_all, pg_size,
                &MPIDI_CH3I_RDMA_Process);
        rdma_param_handle_heterogenity(hca_type_all, pg_size);
    }

    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
    if(MPIDI_CH3I_RDMA_Process.has_apm) {
        init_apm_lock();
    }

    mpi_errno = init_vbuf_lock();
    if(mpi_errno) MPIU_ERR_POP(mpi_errno);

    if (MPIDI_CH3I_RDMA_Process.has_srq) {
	MPIDI_CH3I_RDMA_Process.vc_mapping =
	    (MPIDI_VC_t **) MPIU_Malloc(sizeof(MPIDI_VC_t) * pg_size);
    }

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++) {
	MPIDI_PG_Get_vc(pg, i, &vc);
	memset(&(vc->mrail), 0, sizeof(vc->mrail));
	/* This assmuption will soon be done with */
	vc->mrail.num_rails = rdma_num_rails;
	if (MPIDI_CH3I_RDMA_Process.has_srq) {
	    MPIDI_CH3I_RDMA_Process.vc_mapping[vc->pg_rank] = vc;
	}
    }

#ifdef RDMA_CM
    if (MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
	if ((mpi_errno = rdma_iba_hca_init(
            &MPIDI_CH3I_RDMA_Process,
            pg_rank,
            pg_size,
            NULL)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

	hosts = rdma_cm_get_hostnames(pg_rank, pg_size);
	if(!hosts) 
	    MPIU_Error_printf("Error obtaining hostnames\n");

	if (g_num_smp_peers + 1 < pg_size)
	{
	    /* Initialize the registration cache. */
	    dreg_init();
	    
	    if ((mpi_errno = rdma_iba_allocate_memory(
		     &MPIDI_CH3I_RDMA_Process,
		     pg_rank,
		     pg_size)) != MPI_SUCCESS)
	    {
                MPIU_ERR_POP(mpi_errno);
	    }
	}

    }
#endif

    /* Open the device and create cq and qp's */
#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
#endif /* defined(RDMA_CM) */
        if ((mpi_errno = rdma_iba_hca_init_noqp(
            &MPIDI_CH3I_RDMA_Process,
            pg_rank,
            pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
	}
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

#ifdef RDMA_CM
    {
	if (MPIDI_CH3I_RDMA_Process.use_rdma_cm)
	{
	    cm_ib_context.rank = pg_rank;
	    cm_ib_context.size = pg_size;
	    cm_ib_context.pg = pg;

	    {
		error = PMI_Barrier();
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s", "PMI Barrier failed");
		}
	    }

	    if ((mpi_errno = rdma_cm_connect_all(hosts, pg_rank, pg_size)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
	    }

	    if (g_num_smp_peers + 1 == pg_size)
            {
		MPIDI_CH3I_RDMA_Process.has_one_sided = 0;
            }

	    DEBUG_PRINT("Done RDMA_CM based MPIDI_CH3I_CM_Init()\n");
	    goto fn_exit;
	}
    }
#endif /* RDMA_CM */

    cm_ib_context.rank = pg_rank;
    cm_ib_context.size = pg_size;
    cm_ib_context.pg = pg;

    if ((mpi_errno = MPICM_Init_UD(&ud_qpn_self)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    if (pg_size > 1)
    {
        if (MPIDI_CH3I_RDMA_Process.has_ring_startup)
        {
	    ud_addr_info_t self_info;
            ud_addr_info_t *all_info;
            
	    char hostname[HOSTNAME_LEN + 1];
	    int hostid;
	    struct hostent *hostent;

	    gethostname(hostname, HOSTNAME_LEN);

	    if (!hostname)
            {
	        MPIU_ERR_SETFATALANDJUMP1(
                    mpi_errno,
                    MPI_ERR_OTHER,
	            "**fail",
                    "**fail %s",
                    "Could not get hostname"
                );
	    }

	    hostent = gethostbyname(hostname);
	    hostid = (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;
	    self_info.hostid = hostid;
	    self_info.lid = MPIDI_CH3I_RDMA_Process.lids[0][0];
	    self_info.qpn = ud_qpn_self;
	    all_info = (ud_addr_info_t *) MPIU_Malloc(sizeof(ud_addr_info_t)*pg_size); 
	    /*will be freed in rdma_cleanup_startup_ring */

	    rdma_ring_based_allgather(
                &self_info,
                sizeof(self_info),
                pg_rank,
                all_info,
                pg_size,
                &MPIDI_CH3I_RDMA_Process
            );

            MPIDI_VC_t* vc = NULL;

	    for (i = 0; i < pg_size; ++i)
            {
	        MPIDI_PG_Get_vc(pg, i, &vc);
	        vc->smp.hostid = all_info[i].hostid;
		ud_qpn_all[i] = all_info[i].qpn;
		lid_all[i] = all_info[i].lid;
	    }
	}
	else {
	    char *key;
	    char *val;
	    /*Exchange the information about HCA_lid and qp_num */
	    /* Allocate space for pmi keys and values */
	    error = PMI_KVS_Get_key_length_max(&key_max_sz);
	    if(error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max key length");
	    }

	    ++key_max_sz;
	    key = MPIU_Malloc(key_max_sz);
	    if (key == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "PMI key");
	    }

	    error = PMI_KVS_Get_value_length_max(&val_max_sz);
	    if(error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "Error getting max value length");
	    }

	    ++val_max_sz;
	    val = MPIU_Malloc(val_max_sz);
	    if (val == NULL) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			"**nomem %s", "PMI value");
	    }

	    if (key_max_sz < 20 || val_max_sz < 30) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**fail", "**fail %s", "PMI value too small");
	    }

	    /*Just put lid for default port and ud_qpn is sufficient*/
	    MPIU_Snprintf(key, key_max_sz, "ud_info_%08d", pg_rank);
	    MPIU_Snprintf(val, val_max_sz, "%08x:%08x",
		    MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);
	    /*
	       printf("Rank %d, my lid: %08x, my qpn: %08x\n", pg_rank,
	       MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);
	     */
	    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_kvs_put", "**pmi_kvs_put %d", error);
	    }

	    error = PMI_KVS_Commit(pg->ch.kvs_name);
	    if (error != PMI_SUCCESS) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
	    }

	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS)
            {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**pmi_barrier", "**pmi_barrier %d", error);
	    }

	    for (i = 0; i < pg_size; i++)
            {
		if (pg_rank == i)
                {
		    lid_all[i] = MPIDI_CH3I_RDMA_Process.lids[0][0];
		    ud_qpn_all[i] = ud_qpn_self;
		    continue;
		}

		MPIU_Snprintf(key, key_max_sz, "ud_info_%08d", i);

		error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_get", "**pmi_kvs_get %d", error);
		}

		sscanf(val,"%08x:%08x",
                       (unsigned int *)&(lid_all[i]),&(ud_qpn_all[i]));
		/*    
		      printf("Rank %d: from rank %d, lid: %08x, qpn: %08x\n",pg_rank,i,lid_all[i],ud_qpn_all[i]);
		 */
	    }
	}
    }
    else
    {
        lid_all[0] = MPIDI_CH3I_RDMA_Process.lids[0][0];
        ud_qpn_all[0] = ud_qpn_self;
    }

    if (pg_size > 1
#if defined(RDMA_CM)
        && !MPIDI_CH3I_RDMA_Process.use_rdma_cm
#endif /* defined(RDMA_CM) */
    )
    {
        rdma_cleanup_startup_ring(&MPIDI_CH3I_RDMA_Process);
    }

#if defined(RDMA_CM)
    if (g_num_smp_peers + 1 < pg_size || !MPIDI_CH3I_RDMA_Process.use_rdma_cm){
#endif /* defined(RDMA_CM) */
	/* Initialize the registration cache */
	dreg_init();
	/* Allocate RDMA Buffers */
	/*
	 * Add Error Handling in Function
	 */
        if ((mpi_errno = rdma_iba_allocate_memory(
            &MPIDI_CH3I_RDMA_Process,
            pg_rank,
            pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
	}
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

    if ((mpi_errno = MPICM_Connect_UD(ud_qpn_all, lid_all)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    MPIU_Free(ud_qpn_all);
    MPIU_Free(lid_all);
    MPIU_Free(hca_type_all);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();
    if (error != PMI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", error);
    }

    DEBUG_PRINT("Done MPIDI_CH3I_CM_Init()\n");

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_CH3I_CM_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Finalize()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);

    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    int retval;
    int i = 0;
    int rail_index;
    int hca_index;
    int mpi_errno = MPI_SUCCESS;

    MPIDI_VC_t *vc = NULL;

    /* Insert implementation here */
    MPIDI_PG_t* pg = MPIDI_Process.my_pg;
    int pg_rank = MPIDI_Process.my_pg_rank;
    int pg_size = MPIDI_PG_Get_size(pg);

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    if ((retval = PMI_Barrier()) != 0)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", retval);
    }

#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
#endif /* defined(RDMA_CM) */
	if ((retval = MPICM_Finalize_UD()) != 0)
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**fail", "**fail %d", retval);
        }
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

    for (; i < pg_size; ++i)
    {
	if (i == pg_rank)
        {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);

	/* Skip SMP VCs */
	if (SMP_INIT && (vc->smp.local_nodes >= 0))
	    continue;

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
        {
	    continue;
        }

	for (hca_index = 0; hca_index < rdma_num_hcas; ++hca_index)
        {
	    if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index])
            {
                ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
            }

	    if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index])
            {
                ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
            }
	}

	for (rail_index = 0; rail_index < vc->mrail.num_rails; ++rail_index)
        {
#if defined(RDMA_CM)
	    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
            {
#endif /* defined(RDMA_CM) */
                ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
#if defined(RDMA_CM)
            }
#endif /* defined(RDMA_CM) */
	}

#if defined(USE_HEADER_CACHING)
        MPIU_Free(vc->mrail.rfp.cached_incoming);
        MPIU_Free(vc->mrail.rfp.cached_outgoing);
#endif /* defined(USE_HEADER_CACHING) */

        if (vc->mrail.rfp.RDMA_send_buf_DMA)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_send_buf_DMA);
        }

        if (vc->mrail.rfp.RDMA_recv_buf_DMA)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        }

        if (vc->mrail.rfp.RDMA_send_buf)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_send_buf);
        }

        if (vc->mrail.rfp.RDMA_recv_buf)
        {
	    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf);
        }
    }

    /* STEP 3: release all the cq resource, relaes all the unpinned buffers, release the
     * ptag 
     *   and finally, release the hca */
#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
#endif /* defined(RDMA_CM) */
        for (i = 0; i < rdma_num_hcas; ++i)
        {
	    ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);

	    if (MPIDI_CH3I_RDMA_Process.has_srq)
            {
		pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
		pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i], NULL);
		ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
	    }

	    deallocate_vbufs(i);
	    while (dreg_evict());
	    ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
	    ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
	}
#if defined(RDMA_CM)
    }
    else 
    {
	ib_finalize_rdma_cm(pg_rank, pg_size);
	MPIU_Free(rdma_cm_host_list);
    }
#endif /* defined(RDMA_CM) */

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/* vi:set sw=4 tw=80: */
