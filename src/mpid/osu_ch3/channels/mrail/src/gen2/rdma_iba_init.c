/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
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

static int cached_pg_size;
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

#if 0
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_init_process_group
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_init_process_group(int *has_parent,
                                       MPIDI_PG_t ** pg_pptr,
                                       int *pg_rank_ptr)
{
    /* Initialize the rdma implemenation. */
    /* No rdma functions will be called before this function */
    /* This first initializer is called before any RDMA channel initialization
       code has been executed.  The job of this function is to allocate
       a process group and set the rank and size fields. */
    int mpi_errno = MPI_SUCCESS;
    int pmi_errno = PMI_SUCCESS;
    int rc;
    MPIDI_PG_t *pg = NULL;
    int pg_rank, pg_size;
    char *pg_id;
    int pg_id_sz;
    int kvs_name_sz;


    /*
     * Extract process group related information from PMI
     */
    rc = PMI_Init(has_parent);
    if (rc != 0) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**pmi_init",
                                 "**pmi_init %d", rc);
        return mpi_errno;
    }
    rc = PMI_Get_rank(&pg_rank);
    if (rc != 0) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER,
                                 "**pmi_get_rank", "**pmi_get_rank %d",
                                 rc);
        return mpi_errno;
    }
    rc = PMI_Get_size(&pg_size);
    if (rc != 0) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER,
                                 "**pmi_get_size", "**pmi_get_size %d",
                                 rc);
        return mpi_errno;
    }

    /*
     * Get the process group id
     */
    pmi_errno = PMI_Get_id_length_max(&pg_id_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**pmi_get_id_length_max",
                                 "**pmi_get_id_length_max %d", pmi_errno);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    pg_id = MPIU_Malloc(pg_id_sz + 1);
    if (pg_id == NULL) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**nomem", NULL);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    pmi_errno = PMI_Get_id(pg_id, pg_id_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**pmi_get_id", "**pmi_get_id %d",
                                 pmi_errno);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    /*
     * Initialize the process group tracking subsystem
     */
    mpi_errno =
        MPIDI_PG_Init(MPIDI_CH3I_PG_Compare_ids, MPIDI_CH3I_PG_Destroy);
    if (mpi_errno != MPI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**ch3|pg_init", NULL);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    /*
     * Create a new structure to track the process group
     */
    mpi_errno = MPIDI_PG_Create(pg_size, pg_id, &cached_pg);
    pg = cached_pg;

    if (mpi_errno != MPI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**ch3|pg_create", NULL);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }
    pg->ch.kvs_name = NULL;


    /*
     * Get the name of the key-value space (KVS)
     */
    pmi_errno = PMI_KVS_Get_name_length_max(&kvs_name_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**pmi_kvs_get_name_length_max",
                                 "**pmi_kvs_get_name_length_max %d",
                                 pmi_errno);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    pg->ch.kvs_name = MPIU_Malloc(kvs_name_sz + 1);
    if (pg->ch.kvs_name == NULL) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**nomem", NULL);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }

    pmi_errno = PMI_KVS_Get_my_name(pg->ch.kvs_name, kvs_name_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                 FCNAME, __LINE__, MPI_ERR_OTHER,
                                 "**pmi_kvs_get_my_name",
                                 "**pmi_kvs_get_my_name %d", pmi_errno);
        goto fn_fail;
        /* --END ERROR HANDLING-- */
    }



    pg->ch.nRDMAWaitSpinCount = MPIDI_CH3I_SPIN_COUNT_DEFAULT;
    pg->ch.nRDMAWaitYieldCount = MPIDI_CH3I_YIELD_COUNT_DEFAULT;

    *pg_pptr = pg;
    *pg_rank_ptr = pg_rank;

    cached_pg_rank = pg_rank;
    cached_pg_size = pg_size;

#ifdef ONE_SIDED
    {
        int i;
        for (i = 0; i < MAX_WIN_NUM; i++)
            MPIDI_CH3I_RDMA_Process.win_index2address[i] = 0;
    }
#endif

    /*originally there is some one sided info here */
    /* not included in current implementation */

#ifdef MAC_OSX
    create_hash_table();
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif
  fn_exit:
    return MPI_SUCCESS;

  fn_fail:
    if (pg != NULL) {
        MPIDI_PG_Destroy(pg);
    }
    goto fn_exit;
    return MPI_SUCCESS;
}
#endif
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
    char        *key;
    char        *val;
    int         key_max_sz;
    int         val_max_sz;

    char        rdmakey[512];
    char        rdmavalue[512];
    char        tmp[512];
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

    /* Currently only single nic per pg is allowed */
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
        fprintf(stderr, "Error %s:%d out of memory\n", __FILE__, __LINE__);
        exit(1);
    }

    for (i = 0; i < pg_size; i++) {
        rdma_iba_addr_table.qp_num_rdma[i] =
            (uint32_t *) malloc(MAX_NUM_HCAS * sizeof(uint32_t));
        rdma_iba_addr_table.lid[i] =
            (uint16_t *) malloc(MAX_NUM_HCAS * sizeof(uint16_t));
        rdma_iba_addr_table.hostid[i] =
            (int *) malloc(MAX_NUM_HCAS * sizeof(int));
        rdma_iba_addr_table.qp_num_onesided[i] =
            (uint32_t *) malloc(MAX_NUM_HCAS * sizeof(uint32_t));
        if (!rdma_iba_addr_table.lid[i]
            || !rdma_iba_addr_table.hostid[i]
            || !rdma_iba_addr_table.qp_num_rdma[i]
            || !rdma_iba_addr_table.qp_num_onesided[i]) {
            fprintf(stderr, "Error %s:%d out of memory\n",
                    __FILE__, __LINE__);
            exit(1);
        }
    }

    rdma_init_parameters(pg_size, pg_rank);

    DEBUG_PRINT("End of init params\n");
    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);
        memset(&(vc->mrail), 0, sizeof(vc->mrail));
        vc->mrail.num_total_subrails = 1;
        vc->mrail.subrail_per_hca = 1;
    }

    /* Open the device and create cq and qp's */
    MPIDI_CH3I_RDMA_Process.num_hcas = 1;
    ret = rdma_iba_hca_init(&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);
    if (ret) {
        fprintf(stderr, "Fail to init hca\n");
        return -1;
    }

    DEBUG_PRINT("Init hca done...\n");
    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;
    /* init dreg entry */
    dreg_init();
    /* Allocate memory and handlers */
    ret = rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process, vc, pg_rank,
                             pg_size);
    if (ret) {
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
        /* STEP 1: Exchange HCA_lid */
        for (i = 0; i < pg_size; i++) {
            int lid;

            if (pg_rank == i)
                continue;

            lid = get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0], rdma_default_port);
            if (lid < 0) {
                fprintf(stderr, "[%s, %d] Fail to query local lid. Is OpenSM running?\n", 
                        __FILE__, __LINE__);
                return 1;
            }
            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            sprintf(rdmavalue, "%08d", lid);

            DEBUG_PRINT("put my hca lid %d \n", lid);

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
            rdma_iba_addr_table.lid[i][0] = (uint16_t)atoi(rdmavalue);
            DEBUG_PRINT("get hca %d, lid %08d \n", i,
                        rdma_iba_addr_table.lid[i][0]);
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
            sprintf(rdmavalue, "%08d",
                    rdma_iba_addr_table.qp_num_rdma[i][0]);

            DEBUG_PRINT("put qp %d, num %08X \n", i,
                        rdma_iba_addr_table.qp_num_rdma[i][0]);
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

            rdma_iba_addr_table.qp_num_rdma[i][0] = (uint32_t)atoi(rdmavalue);
            DEBUG_PRINT("get qp %d,  num %08X \n", i,
                        rdma_iba_addr_table.qp_num_rdma[i][0]);
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
                        vc->mrail.rfp.RDMA_recv_buf, i, pg_rank);

            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            sprintf(rdmavalue, "%032ld-%016ld",
                    (uintptr_t) vc->mrail.rfp.RDMA_recv_buf,
                    (unsigned long) vc->mrail.rfp.RDMA_recv_buf_mr[0]->rkey);

            DEBUG_PRINT("Put %d recv_buf %016lX, key %08X\n",
                        i, (uintptr_t) vc->mrail.rfp.RDMA_recv_buf,
                        vc->mrail.rfp.RDMA_recv_buf_mr[0]->rkey);

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

            /*format : "%032d-%016d-%032d-%016d" */
            strncpy(tmp, rdmavalue, 32);
            tmp[32] = '\0';
            vc->mrail.rfp.remote_RDMA_buf = (void *) atol(tmp);
            strncpy(tmp, rdmavalue + 32 + 1, 16);
            tmp[16] = '\0';
            vc->mrail.rfp.RDMA_remote_buf_rkey =
                (uint32_t) atol(tmp);
            strncpy(tmp, rdmavalue + 32 + 1 + 16 + 1, 32);
            tmp[32] = '\0';

            DEBUG_PRINT("Get %d recv_buf %016lX, key %08X\n",
                  i, (uintptr_t) vc->mrail.rfp.remote_RDMA_buf, vc->mrail.rfp.RDMA_remote_buf_rkey);
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
            sprintf(rdmavalue, "%08d",
                    rdma_iba_addr_table.qp_num_onesided[i][0]);

            DEBUG_PRINT("Put key %s, onesided qp %d, num %08X\n",
                        rdmakey, i,
                        rdma_iba_addr_table.qp_num_onesided[i][0]);

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
            rdma_iba_addr_table.qp_num_onesided[i][0] = atoi(rdmavalue);
            DEBUG_PRINT("Get key %s, onesided qp %d, num %08X\n",
                        rdmakey, i,
                        rdma_iba_addr_table.qp_num_onesided[i][0]);
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
        rdma_iba_exchange_info(&MPIDI_CH3I_RDMA_Process,
                               vc, pg_rank, pg_size);
#endif
    }

    /* Enable all the queue pair connections */
    DEBUG_PRINT("Address exchange finished, proceed to enabling connection\n");
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process,
                                vc, pg_rank, pg_size);
    DEBUG_PRINT("Finishing enabling connection\n");

    /*barrier to make sure queues are initialized before continuing */
#ifdef USE_MPD_RING
    error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
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
    error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
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
    int i, j;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int ret;

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
        ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[0]);
        ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[0]);
        free(vc->mrail.rfp.RDMA_send_buf_orig);
        free(vc->mrail.rfp.RDMA_recv_buf_orig);
    }
#endif
    /* STEP 2: destry all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i)
            continue;
        MPIDI_PG_Get_vc(pg, i, &vc);
        for (j = 0; j < vc->mrail.num_total_subrails; j++) {
            ibv_destroy_qp(vc->mrail.qp_hndl[j]);
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
    for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; i++) {
        ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
#ifdef ONE_SIDED
	ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc);
#endif
        deallocate_vbufs();
        while (dreg_evict())
            ;

        ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
    }

    return MPI_SUCCESS;
}

