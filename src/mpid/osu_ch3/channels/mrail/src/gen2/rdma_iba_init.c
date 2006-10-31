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

#include "mem_hooks.h"
#include "rdma_impl.h"
#include "vbuf.h"
#include "cm.h"

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

static int cached_pg_rank;

MPIDI_PG_t *cached_pg;
rdma_iba_addr_tb_t rdma_iba_addr_table;

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

    return attr.lid;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMDA_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RMDA_init(MPIDI_PG_t * pg, int pg_rank)
{
    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    int ret;

    MPIDI_VC_t *vc;
    int pg_size;
    int i, error;
    int rail_index;
    char *key;
    char *val;
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];
    char *buf;
    char tmp_hname[256];

#ifndef DISABLE_PTMALLOC
    if(mvapich2_minit()) {
        fprintf(stderr,
                "[%s:%d] Error initializing MVAPICH2 malloc library\n",
                __FILE__, __LINE__);
        return MPI_ERR_OTHER;
    }
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    gethostname(tmp_hname, 255);
    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    /* Reading the values from user first and 
     * then allocating the memory */

    ret = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process);

    if (ret) {
        return ret;
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);

    rdma_get_user_parameters(pg_size, pg_rank);

    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;

    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d\n",
                rdma_num_qp_per_port, rdma_num_rails);

    rdma_iba_addr_table.lid = (uint16_t **)
        malloc(pg_size * sizeof(uint16_t *));

    rdma_iba_addr_table.hostid = (int **)
        malloc(pg_size * sizeof(int *));

    rdma_iba_addr_table.qp_num_rdma =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));

    rdma_iba_addr_table.qp_num_onesided =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));

    if (!rdma_iba_addr_table.lid
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.qp_num_rdma
        || !rdma_iba_addr_table.qp_num_onesided) {
        fprintf(stderr, "[%s:%d] Could not allocate initialization"
                " Data Structrues\n", __FILE__, __LINE__);
        exit(1);
    }

    for (i = 0; i < pg_size; i++) {

        rdma_iba_addr_table.qp_num_rdma[i] =
            (uint32_t *) malloc(rdma_num_rails * sizeof(uint32_t));

        rdma_iba_addr_table.lid[i] =
            (uint16_t *) malloc(rdma_num_rails * sizeof(uint16_t));

        rdma_iba_addr_table.hostid[i] =
            (int *) malloc(rdma_num_rails * sizeof(int));

        rdma_iba_addr_table.qp_num_onesided[i] =
            (uint32_t *) malloc(rdma_num_rails * sizeof(uint32_t));

        if (!rdma_iba_addr_table.lid[i]
            || !rdma_iba_addr_table.hostid[i]
            || !rdma_iba_addr_table.qp_num_rdma[i]
            || !rdma_iba_addr_table.qp_num_onesided[i]) {
            fprintf(stderr, "Error %s:%d out of memory\n",
                    __FILE__, __LINE__);
            exit(1);
        }
    }

    if (MPIDI_CH3I_RDMA_Process.has_srq) {

        init_vbuf_lock();

        MPIDI_CH3I_RDMA_Process.vc_mapping =
            (MPIDI_VC_t **) malloc(sizeof(MPIDI_VC_t) * pg_size);
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

    /* Open the device and create cq and qp's */
    ret = rdma_iba_hca_init(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);

    if (ret) {
        fprintf(stderr, "Failed to Initialize HCA type\n");
        return -1;
    }

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

    /* Initialize the registration cache */
    dreg_init();

    /* Allocate RDMA Buffers */
    ret = rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process,
                                   pg_rank, pg_size);

    if (ret) {
        /* Clearly, this is some error condition */
        return ret;
    }

    if (pg_size > 1) {

        if (!MPIDI_CH3I_RDMA_Process.has_ring_startup) {
            /*Exchange the information about HCA_lid and qp_num */
            /* Allocate space for pmi keys and values */
            error = PMI_KVS_Get_key_length_max(&key_max_sz);
            assert(error == PMI_SUCCESS);
            key_max_sz++;
            key = MPIU_Malloc(key_max_sz);

            if (key == NULL) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**nomem", "**nomem %s",
                                         "pmi key");
                return error;
            }

            PMI_KVS_Get_value_length_max(&val_max_sz);
            assert(error == PMI_SUCCESS);
            val_max_sz++;
            val = MPIU_Malloc(val_max_sz);

            if (val == NULL) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**nomem", "**nomem %s",
                                         "pmi value");
                return error;
            }
            /* For now, here exchange the information of each LID separately */
            for (i = 0; i < pg_size; i++) {

                if (pg_rank == i) {
                    continue;
                }

                /* generate the key and value pair for each connection */
                sprintf(rdmakey, "%08d-%08d", pg_rank, i);
                buf = rdmavalue;

                for (rail_index = 0; rail_index < rdma_num_rails;
                     rail_index++) {
                    sprintf(buf, "%08d",
                            rdma_iba_addr_table.lid[i][rail_index]);
                    DEBUG_PRINT("put my hca %d lid %d\n", rail_index,
                                rdma_iba_addr_table.lid[i][rail_index]);
                    buf += 8;
                }

                /* put the kvs into PMI */
                MPIU_Strncpy(key, rdmakey, key_max_sz);
                MPIU_Strncpy(val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put(pg->ch.kvs_name, key, val);

                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_put",
                                             "**pmi_kvs_put %d", error);
                    return error;
                }

                DEBUG_PRINT("after put, before barrier\n");

                error = PMI_KVS_Commit(pg->ch.kvs_name);

                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_commit",
                                             "**pmi_kvs_commit %d", error);
                    return error;
                }

            }
            error = PMI_Barrier();
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_barrier",
                                         "**pmi_barrier %d", error);
                return error;
            }


            /* Here, all the key and value pairs are put, now we can get them */
            for (i = 0; i < pg_size; i++) {
                if (pg_rank == i) {
                    rdma_iba_addr_table.lid[i][0] =
                        get_local_lid(MPIDI_CH3I_RDMA_Process.
                                      nic_context[0], rdma_default_port);
                    continue;
                }

                /* generate the key */
                sprintf(rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy(key, rdmakey, key_max_sz);
                error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_get",
                                             "**pmi_kvs_get %d", error);
                    return error;
                }

                MPIU_Strncpy(rdmavalue, val, val_max_sz);
                buf = rdmavalue;

                for (rail_index = 0; rail_index < rdma_num_rails;
                     rail_index++) {
                    int lid;
                    sscanf(buf, "%08d", &lid);
                    buf += 8;
                    rdma_iba_addr_table.lid[i][rail_index] = lid;
                    DEBUG_PRINT("get rail %d, lid %08d\n", rail_index,
                                (int) rdma_iba_addr_table.
                                lid[i][rail_index]);
                }
            }

            /* this barrier is to prevent some process from
               overwriting values that has not been get yet */
            error = PMI_Barrier();
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_barrier",
                                         "**pmi_barrier %d", error);
                return error;
            }

            /* STEP 2: Exchange qp_num */
            for (i = 0; i < pg_size; i++) {

                if (pg_rank == i) {
                    continue;
                }

                /* generate the key and value pair for each connection */
                sprintf(rdmakey, "%08d-%08d", pg_rank, i);
                buf = rdmavalue;

                for (rail_index = 0; rail_index < rdma_num_rails;
                     rail_index++) {
                    sprintf(buf, "%08X",
                            rdma_iba_addr_table.
                            qp_num_rdma[i][rail_index]);
                    buf += 8;
                    DEBUG_PRINT("target %d, put qp %d, num %08X \n", i,
                                rail_index,
                                rdma_iba_addr_table.
                                qp_num_rdma[i][rail_index]);
                }

                DEBUG_PRINT("put rdma value %s\n", rdmavalue);
                /* put the kvs into PMI */
                MPIU_Strncpy(key, rdmakey, key_max_sz);
                MPIU_Strncpy(val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_put",
                                             "**pmi_kvs_put %d", error);
                    return error;
                }

                error = PMI_KVS_Commit(pg->ch.kvs_name);

                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_commit",
                                             "**pmi_kvs_commit %d", error);
                    return error;
                }
            }

            error = PMI_Barrier();
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_barrier",
                                         "**pmi_barrier %d", error);
                return error;
            }

            /* Here, all the key and value pairs are put, now we can get them */
            for (i = 0; i < pg_size; i++) {

                if (pg_rank == i) {
                    continue;
                }

                /* generate the key */
                sprintf(rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy(key, rdmakey, key_max_sz);
                error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);

                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_kvs_get",
                                             "**pmi_kvs_get %d", error);
                    return error;
                }
                MPIU_Strncpy(rdmavalue, val, val_max_sz);

                buf = rdmavalue;
                DEBUG_PRINT("get rdmavalue %s\n", rdmavalue);
                for (rail_index = 0; rail_index < rdma_num_rails;
                     rail_index++) {
                    sscanf(buf, "%08X",
                           &rdma_iba_addr_table.
                           qp_num_rdma[i][rail_index]);
                    buf += 8;
                    DEBUG_PRINT("get qp %d,  num %08X \n", i,
                                rdma_iba_addr_table.
                                qp_num_rdma[i][rail_index]);
                }
            }

            error = PMI_Barrier();
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_barrier",
                                         "**pmi_barrier %d", error);
                return error;
            }
            DEBUG_PRINT("After barrier\n");

            if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
                /* Exchange qp_num */
                for (i = 0; i < pg_size; i++) {

                    if (pg_rank == i) {
                        continue;
                    }

                    /* generate the key and value pair for each connection */
                    sprintf(rdmakey, "%08d-%08d", pg_rank, i);
                    buf = rdmavalue;

                    for (rail_index = 0; rail_index < rdma_num_rails;
                         rail_index++) {
                        sprintf(buf, "%08X",
                                rdma_iba_addr_table.
                                qp_num_onesided[i][rail_index]);
                        buf += 8;
                        DEBUG_PRINT
                            ("Put key %s, onesided qp %d, num %08X\n",
                             rdmakey, i,
                             rdma_iba_addr_table.
                             qp_num_onesided[i][rail_index]);
                    }

                    /* put the kvs into PMI */
                    DEBUG_PRINT("Put a string %s\n", rdmavalue);
                    MPIU_Strncpy(key, rdmakey, key_max_sz);
                    MPIU_Strncpy(val, rdmavalue, val_max_sz);
                    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
                    if (error != 0) {
                        error =
                            MPIR_Err_create_code(MPI_SUCCESS,
                                                 MPIR_ERR_FATAL, FCNAME,
                                                 __LINE__, MPI_ERR_OTHER,
                                                 "**pmi_kvs_put",
                                                 "**pmi_kvs_put %d",
                                                 error);
                        return error;
                    }
                    error = PMI_KVS_Commit(pg->ch.kvs_name);
                    if (error != 0) {
                        error =
                            MPIR_Err_create_code(MPI_SUCCESS,
                                                 MPIR_ERR_FATAL, FCNAME,
                                                 __LINE__, MPI_ERR_OTHER,
                                                 "**pmi_kvs_commit",
                                                 "**pmi_kvs_commit %d",
                                                 error);
                        return error;
                    }
                }

                error = PMI_Barrier();
                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_barrier",
                                             "**pmi_barrier %d", error);
                    return error;
                }

                /* Here, all the key and value pairs are put, now we can get them */
                for (i = 0; i < pg_size; i++) {

                    if (pg_rank == i) {
                        continue;
                    }

                    /* generate the key */
                    sprintf(rdmakey, "%08d-%08d", i, pg_rank);
                    MPIU_Strncpy(key, rdmakey, key_max_sz);
                    error =
                        PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
                    if (error != 0) {
                        error =
                            MPIR_Err_create_code(MPI_SUCCESS,
                                                 MPIR_ERR_FATAL, FCNAME,
                                                 __LINE__, MPI_ERR_OTHER,
                                                 "**pmi_kvs_get",
                                                 "**pmi_kvs_get %d",
                                                 error);
                        return error;
                    }

                    MPIU_Strncpy(rdmavalue, val, val_max_sz);
                    DEBUG_PRINT("Get a string %s\n", rdmavalue);
                    buf = rdmavalue;

                    for (rail_index = 0; rail_index < rdma_num_rails;
                         rail_index++) {
                        sscanf(buf, "%08X",
                               &rdma_iba_addr_table.
                               qp_num_onesided[i][rail_index]);
                        buf += 8;
                        DEBUG_PRINT
                            ("Get key %s, onesided qp %d, num %08X\n",
                             rdmakey, i,
                             rdma_iba_addr_table.
                             qp_num_onesided[i][rail_index]);
                    }
                }

                error = PMI_Barrier();
                if (error != 0) {
                    error =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                             FCNAME, __LINE__,
                                             MPI_ERR_OTHER,
                                             "**pmi_barrier",
                                             "**pmi_barrier %d", error);
                    return error;
                }


                MPIU_Free(val);
                MPIU_Free(key);
            }
        } else {
            /* Exchange the information about HCA_lid, qp_num, and memory,
             * With the ring-based queue pair */
            MPD_Ring_Startup(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
        }
    }

    /* Enable all the queue pair connections */
    DEBUG_PRINT
        ("Address exchange finished, proceed to enabling connection\n");
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process, pg_rank,
                                pg_size);
    DEBUG_PRINT("Finishing enabling connection\n");

    /*barrier to make sure queues are initialized before continuing */
    /*  error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
     *  */
    error = PMI_Barrier();

    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
    }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; i++) {

        if (i == pg_rank) {
            continue;
        }

        MPIDI_PG_Get_vc(pg, i, &vc);

        MRAILI_Init_vc(vc, pg_rank);
    }

    error = PMI_Barrier();

    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
    }
    if (pg_size > 1 && MPIDI_CH3I_RDMA_Process.has_ring_startup) {
        /* clean up the bootstrap qps and free memory */
        rdma_iba_bootstrap_cleanup(&MPIDI_CH3I_RDMA_Process);
    }

    DEBUG_PRINT("Done MPIDI_CH3I_RDMA_init()!!\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMDA_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RMDA_finalize()
{
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

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
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
		    fprintf(stderr, "Failed to deregister mr (%d)\n", err);
	    }
            if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]) {
                err = ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
		if (err)
		    fprintf(stderr, "Failed to deregister mr (%d)\n", err);
	    }
        }

        if (vc->mrail.rfp.RDMA_send_buf_DMA)
            free(vc->mrail.rfp.RDMA_send_buf_DMA);
        if (vc->mrail.rfp.RDMA_recv_buf_DMA)
            free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        if (vc->mrail.rfp.RDMA_send_buf)
            free(vc->mrail.rfp.RDMA_send_buf);
        if (vc->mrail.rfp.RDMA_recv_buf)
            free(vc->mrail.rfp.RDMA_recv_buf);
    }

    /* STEP 2: destry all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);

        if (pg_rank == i) {
            if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
                for (rail_index = 0; rail_index < vc->mrail.num_rails;
                     rail_index++) {
                    err = ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl_1sc);
		    if (err) 
		        fprintf(stderr, "Failed to destroy one sided QP (%d)\n", err);
                }
            }
            continue;
        }

        for (rail_index = 0; rail_index < vc->mrail.num_rails;
             rail_index++) {
 	    err = ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
	    if (err)
	        fprintf(stderr, "Failed to destroy QP (%d)\n", err);
            if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
                err = ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl_1sc);
		if (err)
	            fprintf(stderr, "Failed to destroy one sided QP (%d)\n", err);
            }

        }

#ifdef USE_HEADER_CACHING
        free(vc->mrail.rfp.cached_incoming);
        free(vc->mrail.rfp.cached_outgoing);
#endif
    }
    /* free all the spaces */
    for (i = 0; i < pg_size; i++) {
        if (rdma_iba_addr_table.qp_num_rdma[i])
            free(rdma_iba_addr_table.qp_num_rdma[i]);
        if (rdma_iba_addr_table.lid[i])
            free(rdma_iba_addr_table.lid[i]);
        if (rdma_iba_addr_table.hostid[i])
            free(rdma_iba_addr_table.hostid[i]);
        if (rdma_iba_addr_table.qp_num_onesided[i])
            free(rdma_iba_addr_table.qp_num_onesided[i]);
    }

    free(rdma_iba_addr_table.lid);
    free(rdma_iba_addr_table.hostid);
    free(rdma_iba_addr_table.qp_num_rdma);
    free(rdma_iba_addr_table.qp_num_onesided);

    /* STEP 3: release all the cq resource, 
     * release all the unpinned buffers, 
     * release the ptag and finally, 
     * release the hca */

    for (i = 0; i < rdma_num_hcas; i++) {

        if (MPIDI_CH3I_RDMA_Process.has_srq) {
            pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
            err = ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
	    if (err)
	       fprintf(stderr, "Failed to destroy SRQ (%d)\n", err);
        }

        err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
	if (err)
	    fprintf(stderr, "Failed to destroy CQ (%d)\n", err);

        if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
	    err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[i]);
	    if (err)
		fprintf(stderr , "Failed to Destroy one sided CQ (%d)\n", err);
	}

        deallocate_vbufs(i);

        while (dreg_evict());

        err = ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
	if (err) 
	    fprintf(stderr, "Failed to dealloc pd (%d)\n", err);
        err = ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
	if (err)
	    fprintf(stderr, "Failed to close ib device (%d)\n", err);
	
    }


    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Init(MPIDI_PG_t * pg, int pg_rank)
{
    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    int ret;

    MPIDI_VC_t *vc;
    int pg_size;
    int i, error;
    int key_max_sz;
    int val_max_sz;

    uint32_t *ud_qpn_all;
    uint32_t ud_qpn_self;
    uint16_t *lid_all;
    char tmp_hname[256];

#ifndef DISABLE_PTMALLOC
    if(mvapich2_minit()) {
        fprintf(stderr,
                "[%s:%d] Error initializing MVAPICH2 malloc library\n",
                __FILE__, __LINE__);
        return MPI_ERR_OTHER;
    }
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    gethostname(tmp_hname, 255);
    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);
    /* Reading the values from user first and 
     * then allocating the memory */
    ret = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process);
    if (ret) {
        return ret;
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);

    ud_qpn_all = (uint32_t *) malloc(pg_size * sizeof(uint32_t));
    lid_all = (uint16_t *) malloc(pg_size * sizeof(uint16_t));
    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d\n",
                rdma_num_qp_per_port, rdma_num_rails);
    rdma_iba_addr_table.lid = (uint16_t **)
        malloc(pg_size * sizeof(uint16_t *));
    rdma_iba_addr_table.hostid = (int **)
        malloc(pg_size * sizeof(int *));
    rdma_iba_addr_table.qp_num_rdma =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));
    rdma_iba_addr_table.qp_num_onesided =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));
    if (!rdma_iba_addr_table.lid
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.qp_num_rdma
        || !rdma_iba_addr_table.qp_num_onesided) {
        fprintf(stderr, "[%s:%d] Could not allocate initialization"
                " Data Structrues\n", __FILE__, __LINE__);
        exit(1);
    }

    for (i = 0; i < pg_size; i++) {
        rdma_iba_addr_table.qp_num_rdma[i] =
            (uint32_t *) malloc(rdma_num_rails * sizeof(uint32_t));
        rdma_iba_addr_table.lid[i] =
            (uint16_t *) malloc(rdma_num_rails * sizeof(uint16_t));
        rdma_iba_addr_table.hostid[i] =
            (int *) malloc(rdma_num_rails * sizeof(int));
        rdma_iba_addr_table.qp_num_onesided[i] =
            (uint32_t *) malloc(rdma_num_rails * sizeof(uint32_t));
        if (!rdma_iba_addr_table.lid[i]
            || !rdma_iba_addr_table.hostid[i]
            || !rdma_iba_addr_table.qp_num_rdma[i]
            || !rdma_iba_addr_table.qp_num_onesided[i]) {
            fprintf(stderr, "Error %s:%d out of memory\n",
                    __FILE__, __LINE__);
            exit(1);
        }
    }

    init_vbuf_lock();
    if (MPIDI_CH3I_RDMA_Process.has_srq) {
        MPIDI_CH3I_RDMA_Process.vc_mapping =
            (MPIDI_VC_t **) malloc(sizeof(MPIDI_VC_t) * pg_size);
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

    /* Open the device and create cq and qp's */
    ret = rdma_iba_hca_init_noqp(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
    if (ret) {
        fprintf(stderr, "Failed to Initialize HCA type\n");
        return -1;
    }

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

    /* Initialize the registration cache */
    dreg_init();
    /* Allocate RDMA Buffers */
    ret = rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process,
				   pg_rank, pg_size);
    if (ret) {
	return ret;
    }

    {
        /*Init UD*/
        cm_ib_context.rank = pg_rank;
        cm_ib_context.size = pg_size;
        cm_ib_context.pg = pg;
        cm_ib_context.conn_state = (MPIDI_CH3I_VC_state_t **)malloc
            (pg_size*sizeof(MPIDI_CH3I_VC_state_t *));
        for (i=0;i<pg_size;i++)  {
            if (i == pg_rank)
                continue;
            MPIDI_PG_Get_vc(pg, i, &vc);
            cm_ib_context.conn_state[i] = &(vc->ch.state);
        }
        ret  = MPICM_Init_UD(&ud_qpn_self);
        if (ret) {
            return ret;
        }
    }

    if (pg_size > 1) {
        char *key;
        char *val;
        /*Exchange the information about HCA_lid and qp_num */
        /* Allocate space for pmi keys and values */
        error = PMI_KVS_Get_key_length_max(&key_max_sz);
        assert(error == PMI_SUCCESS);
        key_max_sz++;
        key = MPIU_Malloc(key_max_sz);
        if (key == NULL) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**nomem", "**nomem %s",
                                     "pmi key");
            return error;
        }
        PMI_KVS_Get_value_length_max(&val_max_sz);
        assert(error == PMI_SUCCESS);
        val_max_sz++;
        val = MPIU_Malloc(val_max_sz);
        if (val == NULL) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**nomem", "**nomem %s",
                                     "pmi value");
            return error;
        }
        if (key_max_sz < 20 || val_max_sz < 20) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**size_too_small", "**size_too_small %s",
                                     "pmi value");
            return error;
        }
        /*Just put lid for default port and ud_qpn is sufficient*/
        sprintf(key,"ud_info_%08d",pg_rank);
        sprintf(val,"%08x:%08x",MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);
        /*
        printf("Rank %d, my lid: %08x, my qpn: %08x\n", pg_rank,
            MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);
        */
        error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
        if (error != 0) {
            error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER,
                                     "**pmi_kvs_put",
                                     "**pmi_kvs_put %d", error);
            return error;
        }
        error = PMI_KVS_Commit(pg->ch.kvs_name);
        if (error != 0) {
            error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER,
                                     "**pmi_kvs_commit",
                                     "**pmi_kvs_commit %d", error);
            return error;
        }
        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier",
                                     "**pmi_barrier %d", error);
            return error;
        }
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i) {
                lid_all[i] = MPIDI_CH3I_RDMA_Process.lids[0][0];
                continue;
            }
            sprintf(key,"ud_info_%08d",i);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", error);
                return error;
            }
            sscanf(val,"%08x:%08x",&(lid_all[i]),&(ud_qpn_all[i]));
        /*    
            printf("Rank %d: from rank %d, lid: %08x, qpn: %08x\n",pg_rank,i,lid_all[i],ud_qpn_all[i]);
        */
        }
    }

    ret  = MPICM_Connect_UD(ud_qpn_all, lid_all);
    if (ret) {
        return ret;
    }

    /*barrier to make sure queues are initialized before continuing */
    /*  error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
     *  */
    error = PMI_Barrier();
    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
    }

    DEBUG_PRINT("Done MPIDI_CH3I_CM_Init()\n");
    return MPI_SUCCESS;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Finalize()
{
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

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
    }


    if (cm_ib_context.conn_state)
        free(cm_ib_context.conn_state);

    error = MPICM_Finalize_UD();

    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                __LINE__, MPI_ERR_OTHER,
                "**MPICM_Finalize_UD", "**MPICM_Finalize_UD %d",
                error);
        return error;
    }

    for (i = 0; i < pg_size; i++) {

        if (i == pg_rank) {
            continue;
        }
        
        MPIDI_PG_Get_vc(pg, i, &vc);

        if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
            continue;
        
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
            if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index])
                ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
            if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index])
                ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
        }

        for (rail_index = 0; rail_index < vc->mrail.num_rails;
             rail_index++) {
	    ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
        }

#ifdef USE_HEADER_CACHING
        free(vc->mrail.rfp.cached_incoming);
        free(vc->mrail.rfp.cached_outgoing);
#endif    

        if (vc->mrail.rfp.RDMA_send_buf_DMA)
            free(vc->mrail.rfp.RDMA_send_buf_DMA);
        if (vc->mrail.rfp.RDMA_recv_buf_DMA)
            free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        if (vc->mrail.rfp.RDMA_send_buf)
            free(vc->mrail.rfp.RDMA_send_buf);
        if (vc->mrail.rfp.RDMA_recv_buf)
            free(vc->mrail.rfp.RDMA_recv_buf);
    }

    /* free all the spaces */
    for (i = 0; i < pg_size; i++) {
        if (rdma_iba_addr_table.qp_num_rdma[i])
            free(rdma_iba_addr_table.qp_num_rdma[i]);
        if (rdma_iba_addr_table.lid[i])
            free(rdma_iba_addr_table.lid[i]);
        if (rdma_iba_addr_table.hostid[i])
            free(rdma_iba_addr_table.hostid[i]);
        if (rdma_iba_addr_table.qp_num_onesided[i])
            free(rdma_iba_addr_table.qp_num_onesided[i]);
    }

    free(rdma_iba_addr_table.lid);
    free(rdma_iba_addr_table.hostid);
    free(rdma_iba_addr_table.qp_num_rdma);
    free(rdma_iba_addr_table.qp_num_onesided);

    /* STEP 3: release all the cq resource, 
     * release all the unpinned buffers, 
     * release the ptag and finally, release the hca */

    for (i = 0; i < rdma_num_hcas; i++) {

        if (MPIDI_CH3I_RDMA_Process.has_srq) {
            pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
            ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
        }

        ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);

        deallocate_vbufs(i);

        while (dreg_evict());

        ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
    }

    return MPI_SUCCESS;
}

