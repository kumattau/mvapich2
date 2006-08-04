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
#include "ibv_priv.h"
#include "vbuf.h"

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

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

static int cached_pg_rank;

#ifdef MAC_OSX
extern int mvapich_need_malloc_init;
extern void *vt_dylib;
extern void *(*vt_malloc_ptr) (size_t);
extern void (*vt_free_ptr) (void *);
#endif

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

static inline uint16_t get_local_lid(struct ibv_context * ctx, int port)
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
int MPIDI_CH3I_RMDA_init(MPIDI_PG_t *pg, int pg_rank)
{
    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    int ret;

    MPIDI_VC_t *vc;
    int         pg_size;
    int         i, error;
    int		rail_index; 
#ifdef RDMA_FAST_PATH
    int         hca_index;
#endif
    char        *key;
    char        *val;
    int         key_max_sz;
    int         val_max_sz;

    char        rdmakey[512];
    char        rdmavalue[512];
    char	*buf;
    char        tmp_hname[256];

#ifdef MAC_OSX
    create_hash_table();
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    gethostname(tmp_hname, 255);
    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    /* Reading the values from user first and then allocating the memory */
    rdma_init_parameters(pg_size, pg_rank);
    
    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
     
    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d\n", rdma_num_qp_per_port, rdma_num_rails);
    rdma_iba_addr_table.lid = (uint16_t **) malloc(pg_size * sizeof(uint16_t *));
    rdma_iba_addr_table.hostid = (int **) malloc(pg_size * sizeof(int *));
    rdma_iba_addr_table.qp_num_rdma =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));
    rdma_iba_addr_table.qp_num_onesided =
        (uint32_t **) malloc(pg_size * sizeof(uint32_t *));

    if (!rdma_iba_addr_table.lid
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.qp_num_rdma
        || !rdma_iba_addr_table.qp_num_onesided) {
        fprintf(stderr, "[%s:%d] Could not allocate initialization Data Structrues\n", __FILE__, __LINE__);
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

#ifdef SRQ
    init_vbuf_lock();

    MPIDI_CH3I_RDMA_Process.vc_mapping =
        (MPIDI_VC_t **) malloc(sizeof(MPIDI_VC_t) * pg_size);
#endif

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);
        memset(&(vc->mrail), 0, sizeof(vc->mrail));
        /* This assmuption will soon be done with */
        vc->mrail.num_rails = rdma_num_rails;
#ifdef SRQ
        MPIDI_CH3I_RDMA_Process.vc_mapping[vc->pg_rank] = vc;
#endif
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
    ret = rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
    
    if (ret) {
        /* Clearly, this is some error condition */
        return ret;
    }

    if (pg_size > 1) {

#if !defined(USE_MPD_RING)
        /*Exchange the information about HCA_lid and qp_num */
        /* Allocate space for pmi keys and values */
        error = PMI_KVS_Get_key_length_max(&key_max_sz);
        assert(error == PMI_SUCCESS);
        key_max_sz++;
        key = MPIU_Malloc(key_max_sz);

        if (key == NULL) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**nomem",
                                     "**nomem %s", "pmi key");
            return error;
        }

        PMI_KVS_Get_value_length_max(&val_max_sz);
        assert(error == PMI_SUCCESS);
        val_max_sz++;
        val = MPIU_Malloc(val_max_sz);

        if (val == NULL) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**nomem",
                                     "**nomem %s", "pmi value");
            return error;
        }
        /* For now, here exchange the information of each LID separately */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;

            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
	    buf = rdmavalue;
        for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++ ) {
            sprintf(buf, "%08d", rdma_iba_addr_table.lid[i][rail_index]);
            DEBUG_PRINT("put my hca %d lid %d\n", 
                    rail_index, rdma_iba_addr_table.lid[i][rail_index]);
            buf += 8;
        }

            /* put the kvs into PMI */
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            MPIU_Strncpy(val, rdmavalue, val_max_sz);
            error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_put",
                                         "**pmi_kvs_put %d", error);
                return error;
            }

            DEBUG_PRINT("after put, before barrier\n");
            error = PMI_KVS_Commit(pg->ch.kvs_name);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_commit",
                                         "**pmi_kvs_commit %d", error);
                return error;
            }

        }
        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }


        /* Here, all the key and value pairs are put, now we can get them */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i) {
                rdma_iba_addr_table.lid[i][0] =
                    get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], rdma_default_port);
                continue;
            }
            /* generate the key */
            sprintf(rdmakey, "%08d-%08d", i, pg_rank);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", error);
                return error;
            }
            MPIU_Strncpy(rdmavalue, val, val_max_sz);
	    buf = rdmavalue;
	    
        for (rail_index = 0; rail_index < 
                rdma_num_rails; rail_index ++) {
            int lid;
            sscanf(buf, "%08d", &lid);
            buf += 8;
            rdma_iba_addr_table.lid[i][rail_index] = lid;
            DEBUG_PRINT("get rail %d, lid %08d\n", rail_index,
                    (int)rdma_iba_addr_table.lid[i][rail_index]);
        }
        }

        /* this barrier is to prevent some process from
           overwriting values that has not been get yet */
        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }

        /* STEP 2: Exchange qp_num */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;
            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
	    buf = rdmavalue;
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++ ) {
                sprintf(buf, "%08X",
                    rdma_iba_addr_table.qp_num_rdma[i][rail_index]);
                buf += 8;
                DEBUG_PRINT("target %d, put qp %d, num %08X \n", i, rail_index,
                    rdma_iba_addr_table.qp_num_rdma[i][rail_index]);
            }
	    DEBUG_PRINT("put rdma value %s\n", rdmavalue);
            /* put the kvs into PMI */
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            MPIU_Strncpy(val, rdmavalue, val_max_sz);
            error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_put",
                                         "**pmi_kvs_put %d", error);
                return error;
            }
            error = PMI_KVS_Commit(pg->ch.kvs_name);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_commit",
                                         "**pmi_kvs_commit %d", error);
                return error;
            }
        }

        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }

        /* Here, all the key and value pairs are put, now we can get them */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;
            /* generate the key */
            sprintf(rdmakey, "%08d-%08d", i, pg_rank);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", error);
                return error;
            }
            MPIU_Strncpy(rdmavalue, val, val_max_sz);

	    buf = rdmavalue;
	    DEBUG_PRINT("get rdmavalue %s\n", rdmavalue);
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
        	sscanf(buf, "%08X", &rdma_iba_addr_table.qp_num_rdma[i][rail_index]);
        	buf += 8;
        	DEBUG_PRINT("get qp %d,  num %08X \n", i,
                    rdma_iba_addr_table.qp_num_rdma[i][rail_index]);
            }
        }

        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }
        DEBUG_PRINT("After barrier\n");

#ifdef RDMA_FAST_PATH
        /* STEP 3: exchange the information about remote buffer */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;

            MPIDI_PG_Get_vc(pg, i, &vc);

            DEBUG_PRINT("vc: %p, %p, i %d, pg_rank %d\n", vc,
                        vc->mrail.rfp.RDMA_recv_buf_DMA, i, pg_rank);

            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            sprintf(rdmavalue, "%016X",
                    (uintptr_t) vc->mrail.rfp.RDMA_recv_buf_DMA);
	    buf = rdmavalue + 16;
	    for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++) {
		sprintf(buf, "%08X",
                    (uint32_t) vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]->rkey);
	   	buf += 8; 
            	DEBUG_PRINT("Put %d recv_buf %016lX, key %08X\n",
                        i, (uintptr_t) vc->mrail.rfp.RDMA_recv_buf_DMA,
                        (uint32_t)vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]->rkey);
	    }
            /* put the kvs into PMI */
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            MPIU_Strncpy(val, rdmavalue, val_max_sz);
            error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_put",
                                         "**pmi_kvs_put %d", error);
                return error;
            }
            error = PMI_KVS_Commit(pg->ch.kvs_name);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_commit",
                                         "**pmi_kvs_commit %d", error);
                return error;
            }
        }

        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }

        /* Here, all the key and value pairs are put, now we can get them */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;
            MPIDI_PG_Get_vc(pg, i, &vc);

            /* generate the key */
            sprintf(rdmakey, "%08d-%08d", i, pg_rank);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", error);
                return error;
            }
            MPIU_Strncpy(rdmavalue, val, val_max_sz);

            sscanf(rdmavalue, "%016X", &vc->mrail.rfp.remote_RDMA_buf);
	    buf = rdmavalue + 16;
	    for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++) { 
        	sscanf(buf, "%08X", &vc->mrail.rfp.RDMA_remote_buf_rkey[hca_index]);

            	DEBUG_PRINT("Get %d recv_buf %016lX, key %08X\n",
                  i, (uintptr_t) vc->mrail.rfp.remote_RDMA_buf, 
		  vc->mrail.rfp.RDMA_remote_buf_rkey[hca_index]);
	    }
        }
        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }
#endif
#ifdef ONE_SIDED
        /* Exchange qp_num */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;
            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            buf = rdmavalue;
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++ ) {
                  sprintf(buf, "%08X",
                          rdma_iba_addr_table.qp_num_onesided[i][rail_index]);
                  buf += 8;
                  DEBUG_PRINT("Put key %s, onesided qp %d, num %08X\n",
                               rdmakey, i,
                               rdma_iba_addr_table.qp_num_onesided[i][rail_index]);
            } 
            /* put the kvs into PMI */
            DEBUG_PRINT("Put a string %s\n", rdmavalue);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            MPIU_Strncpy(val, rdmavalue, val_max_sz);
            error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_put",
                                         "**pmi_kvs_put %d", error);
                return error;
            }
            error = PMI_KVS_Commit(pg->ch.kvs_name);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_commit",
                                         "**pmi_kvs_commit %d", error);
                return error;
            }
        }

        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }

        /* Here, all the key and value pairs are put, now we can get them */
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i)
                continue;
            /* generate the key */
            sprintf(rdmakey, "%08d-%08d", i, pg_rank);
            MPIU_Strncpy(key, rdmakey, key_max_sz);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                error =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**pmi_kvs_get",
                                         "**pmi_kvs_get %d", error);
                return error;
            }
  
            MPIU_Strncpy(rdmavalue, val, val_max_sz);
            DEBUG_PRINT("Get a string %s\n", rdmavalue);
            buf = rdmavalue;
            for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
                 sscanf(buf, "%08X", &rdma_iba_addr_table.qp_num_onesided[i][rail_index]);
                 buf += 8;
                 DEBUG_PRINT("Get key %s, onesided qp %d, num %08X\n",
                              rdmakey, i,
                              rdma_iba_addr_table.qp_num_onesided[i][rail_index]);
            }
        }

        error = PMI_Barrier();
        if (error != 0) {
            error =
                MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
            return error;
        }


        MPIU_Free(val);
        MPIU_Free(key);
#endif
#else                           /*  end  f !defined (USE_MPD_RING) */
        /* Exchange the information about HCA_lid, qp_num, and memory,
         * With the ring-based queue pair */
        MPD_Ring_Startup(&MPIDI_CH3I_RDMA_Process,
                               pg_rank, pg_size);
#endif
    }

    /* Enable all the queue pair connections */
    DEBUG_PRINT("Address exchange finished, proceed to enabling connection\n");
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
    DEBUG_PRINT("Finishing enabling connection\n");

    /*barrier to make sure queues are initialized before continuing */
#ifdef USE_MPD_RING
  /*  error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
   *  */
    error = PMI_Barrier();
#else
    error = PMI_Barrier();
#endif

    if (error != 0) {
        error =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                 "**pmi_barrier %d", error);
        return error;
    }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;
        MPIDI_PG_Get_vc(pg, i, &vc);

        MRAILI_Init_vc(vc, pg_rank);
    }

#ifdef USE_MPD_RING
   /* error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank,
    * pg_size);*/
    error = PMI_Barrier();
#else
    error = PMI_Barrier();
#endif

    if (error != 0) {
        error =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                 "**pmi_barrier %d", error);
        return error;
    }

#ifdef USE_MPD_RING
    if (pg_size > 1) {
        /* clean up the bootstrap qps and free memory */
        rdma_iba_bootstrap_cleanup(&MPIDI_CH3I_RDMA_Process);
    }
#endif

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
#ifdef RDMA_FAST_PATH
    int hca_index;
#endif

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();
    if (error != 0) {
        error = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**pmi_barrier", "**pmi_barrier %d",
                                     error);
        return error;
    }
#ifdef RDMA_FAST_PATH
    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;
        MPIDI_PG_Get_vc(pg, i, &vc);

	for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++) {	
            ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
            ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
	}
        free(vc->mrail.rfp.RDMA_send_buf_DMA);
        free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        free(vc->mrail.rfp.RDMA_send_buf);
        free(vc->mrail.rfp.RDMA_recv_buf);
    }
#endif
    /* STEP 2: destry all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i)
            continue;
        MPIDI_PG_Get_vc(pg, i, &vc);
        for (rail_index = 0; rail_index < vc->mrail.num_rails; rail_index++) {
            ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
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
    /* STEP 3: release all the cq resource, relaes all the unpinned buffers, release the
     * ptag 
     *   and finally, release the hca */
    for (i = 0; i < rdma_num_hcas; i++) {
        ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
#ifdef ONE_SIDED
        ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[i]);
#endif
        deallocate_vbufs(i);
        while (dreg_evict())
            ;

        ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
    }


    return MPI_SUCCESS;
}

