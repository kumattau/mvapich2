/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
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

#include "rdma_impl.h"
#include "pmi.h"
#ifdef MAC_OSX
#include <dlfcn.h>
#else
#include <malloc.h>
#endif
#include "vapi_priv.h"
#include "vapi_util.h"

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

static int MPIDI_CH3I_PG_Compare_ids(void *id1, void *id2);
static int MPIDI_CH3I_PG_Destroy(MPIDI_PG_t * pg, void *id);

static int cached_pg_size;
static int cached_pg_rank;

#ifdef MAC_OSX
extern int mvapich_need_malloc_init;
extern void *vt_dylib;
extern void *(*vt_malloc_ptr) (size_t);
extern void (*vt_free_ptr) (void *);

void mvapich_malloc_init (void);

#endif

MPIDI_PG_t *cached_pg;
rdma_iba_addr_tb_t rdma_iba_addr_table;

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

static EVAPI_async_handler_hndl_t async_handler;


static void async_event_handler(VAPI_hca_hndl_t hca_hndl,
                                VAPI_event_record_t * event_p,
                                void *priv_data)
{
    int pg_rank;
    PMI_Get_rank(&pg_rank);
    switch (event_p->type) {
    case VAPI_QP_PATH_MIGRATED:
    case VAPI_EEC_PATH_MIGRATED:
    case VAPI_QP_COMM_ESTABLISHED:
    case VAPI_EEC_COMM_ESTABLISHED:
    case VAPI_SEND_QUEUE_DRAINED:
    case VAPI_PORT_ACTIVE:
        {
            fprintf(stderr, "rank %d, Got an asynchronous event: %s\n",
                    pg_rank, VAPI_event_record_sym(event_p->type));
            break;
        }
    case VAPI_CQ_ERROR:
    case VAPI_LOCAL_WQ_INV_REQUEST_ERROR:
    case VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR:
    case VAPI_LOCAL_WQ_CATASTROPHIC_ERROR:
    case VAPI_PATH_MIG_REQ_ERROR:
    case VAPI_LOCAL_EEC_CATASTROPHIC_ERROR:
    case VAPI_LOCAL_CATASTROPHIC_ERROR:
    case VAPI_PORT_ERROR:
        {
            fprintf(stderr,
                    "rank %d, Got an asynchronous event: %s (%s)",
                    pg_rank, VAPI_event_record_sym(event_p->type),
                    VAPI_event_syndrome_sym(event_p->syndrome));
            break;
        }
    default:
        fprintf(stderr, "Warning!! Got an undefined asynchronous event\n");
    }
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

    int pg_size;
    MPIDI_VC_t *vc;
    int i, error;
    char *key;
    char *val;
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];
    char tmp[512];
    char tmp_hname[256];

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
    rdma_iba_addr_table.lid = (int **) malloc(pg_size * sizeof(int *));
    rdma_iba_addr_table.hostid = (int **) malloc(pg_size * sizeof(int *));
    rdma_iba_addr_table.qp_num_rdma =
        (int **) malloc(pg_size * sizeof(int *));
    rdma_iba_addr_table.qp_num_onesided =
        (int **) malloc(pg_size * sizeof(int *));

    if (!rdma_iba_addr_table.lid
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.qp_num_rdma
        || !rdma_iba_addr_table.qp_num_onesided) {
        fprintf(stderr, "Error %s:%d out of memory\n", __FILE__, __LINE__);
        exit(1);
    }

    for (i = 0; i < pg_size; i++) {
        rdma_iba_addr_table.qp_num_rdma[i] =
            (int *) malloc(MAX_NUM_HCAS * sizeof(int));
        rdma_iba_addr_table.lid[i] =
            (int *) malloc(MAX_NUM_HCAS * sizeof(int));
        rdma_iba_addr_table.hostid[i] =
            (int *) malloc(MAX_NUM_HCAS * sizeof(int));
        rdma_iba_addr_table.qp_num_onesided[i] =
            (int *) malloc(MAX_NUM_HCAS * sizeof(int));
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

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);
        memset(&(vc->mrail), 0, sizeof(vc->mrail));
        vc->mrail.num_total_subrails = 1;
        vc->mrail.subrail_per_hca = 1;
    }

    /* Open the device and create cq and qp's */
    MPIDI_CH3I_RDMA_Process.num_hcas = 1;
    rdma_iba_hca_init(&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);
    MPIDI_CH3I_RDMA_Process.maxtransfersize = VAPI_MAX_RDMA_SIZE;
    /* init dreg entry */
    dreg_init(MPIDI_CH3I_RDMA_Process.nic[0],
              MPIDI_CH3I_RDMA_Process.ptag[0]);
    /* Allocate memory and handlers */
    rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process, vc, pg_rank,
                             pg_size);

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
            if (pg_rank == i)
                continue;
            /* generate the key and value pair for each connection */
            sprintf(rdmakey, "%08d-%08d", pg_rank, i);
            sprintf(rdmavalue, "%08d",
                    MPIDI_CH3I_RDMA_Process.hca_port[0].lid);

            DEBUG_PRINT("put my hca lid %X \n",
                        MPIDI_CH3I_RDMA_Process.hca_port[0].lid);

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
                    MPIDI_CH3I_RDMA_Process.hca_port[0].lid;
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
            rdma_iba_addr_table.lid[i][0] = atoi(rdmavalue);
            DEBUG_PRINT("get hca %d, lid %08X \n", i,
                        rdma_iba_addr_table.lid[i]);
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
                        rdma_iba_addr_table.qp_num_rdma[i]);
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

            rdma_iba_addr_table.qp_num_rdma[i][0] = atoi(rdmavalue);
            DEBUG_PRINT("get qp %d,  num %08X \n", i,
                        rdma_iba_addr_table.qp_num_rdma[i]);
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
                    (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                    (VAPI_rkey_t) vc->mrail.rfp.RDMA_recv_buf_hndl[0].
                    rkey);

            DEBUG_PRINT("Address excange for RDMA_FAST_PATH, dest %d recv_buf %016lX, key %08X\n",
                        i, (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                        (VAPI_rkey_t) vc->mrail.rfp.RDMA_recv_buf_hndl[0].
                        rkey);

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

            /* we get the remote addresses, now the SendBufferTail.buffer
               is the address for remote SendBuffer.tail;
               the RecvBufferHead.buffer is the address for
               the remote RecvBuffer.head */
            /*format : "%032d-%016d-%032d-%016d" */
            strncpy(tmp, rdmavalue, 32);
            tmp[32] = '\0';
            vc->mrail.rfp.remote_RDMA_buf = (void *) atol(tmp);
            strncpy(tmp, rdmavalue + 32 + 1, 16);
            tmp[16] = '\0';
            vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey =
                (VAPI_rkey_t) atol(tmp);
            strncpy(tmp, rdmavalue + 32 + 1 + 16 + 1, 32);
            tmp[32] = '\0';
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
#else   /*  end if !defined (USE_MPD_RING) */
        /* Exchange the information about HCA_lid, qp_num, and memory,
         * With the ring-based queue pair */
        rdma_iba_exchange_info(&MPIDI_CH3I_RDMA_Process,
                               vc, pg_rank, pg_size);
#endif
    }

    /* Enable all the queue pair connections */
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process,
                                vc, pg_rank, pg_size);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

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

#ifdef MAC_OSX
    mvapich_malloc_init ();
    mvapich_need_malloc_init = 0;
#endif

    error = PMI_Barrier();
    if (error != 0) {
        error =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                 "**pmi_barrier %d", error);
        return error;
    }

#ifdef ONE_SIDED
    {
        int i;
        for (i = 0; i < MAX_WIN_NUM; i++)
            MPIDI_CH3I_RDMA_Process.win_index2address[i] = 0;
    }
#endif

    ret = EVAPI_set_async_event_handler(MPIDI_CH3I_RDMA_Process.nic[0],
                                        async_event_handler, 0,
                                        &async_handler);

    if (VAPI_OK != ret) {
        fprintf(stderr, "unable to set async handler\n");
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
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */

    ret =
        EVAPI_clear_async_event_handler(MPIDI_CH3I_RDMA_Process.nic[0],
                                        async_handler);
    if (VAPI_OK != ret) {
        fprintf(stderr, "unable to release async handler\n");
    }

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
        error =
            VAPI_deregister_mr(MPIDI_CH3I_RDMA_Process.nic[0],
                               vc->mrail.rfp.RDMA_send_buf_hndl[0].hndl);
        CHECK_RETURN(error, "could unpin send rdma buffer");
        error =
            VAPI_deregister_mr(MPIDI_CH3I_RDMA_Process.nic[0],
                               vc->mrail.rfp.RDMA_recv_buf_hndl[0].hndl);
        CHECK_RETURN(error, "could unpin recv rdma buffer");
        /* free the buffers */
        free(vc->mrail.rfp.RDMA_send_buf_orig);
        free(vc->mrail.rfp.RDMA_recv_buf_orig);
        free(vc->mrail.rfp.RDMA_send_buf_hndl);
        free(vc->mrail.rfp.RDMA_recv_buf_hndl);
    }
#endif
    /* STEP 2: destry all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i)
            continue;
        MPIDI_PG_Get_vc(pg, i, &vc);
        for (j = 0; j < vc->mrail.num_total_subrails; j++) {
            ret = VAPI_destroy_qp(MPIDI_CH3I_RDMA_Process.nic[0],
                                  vc->mrail.qp_hndl[j]);
            if (VAPI_OK != ret) {
                vapi_error_abort(GEN_EXIT_ERR,
                                 "Error while trying to free queue pairs\n");
            }
        }
#ifdef  ONE_SIDED
        ret = VAPI_destroy_qp(MPIDI_CH3I_RDMA_Process.nic[0],
                              vc->mrail.qp_hndl_1sc);
        if (VAPI_OK != ret) {
            vapi_error_abort(GEN_EXIT_ERR,
                             "Error while trying to free queue pairs\n");
        }
#endif
#if defined(USE_HEADER_CACHING) && defined(RDMA_FAST_PATH)
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
        ret = VAPI_destroy_cq(MPIDI_CH3I_RDMA_Process.nic[i],
                              MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
        if (VAPI_OK != ret) {
            vapi_error_abort(GEN_EXIT_ERR,
                             "Error while trying to free the completion queue\n");
        }
        /* release registered vbuf and regions */
        deallocate_vbufs(MPIDI_CH3I_RDMA_Process.nic);
        while (dreg_evict());
        ret = VAPI_dealloc_pd(MPIDI_CH3I_RDMA_Process.nic[i],
                              MPIDI_CH3I_RDMA_Process.ptag[i]);

        if (VAPI_OK != ret) {
        }

        /* to release all resources */
        EVAPI_release_hca_hndl(MPIDI_CH3I_RDMA_Process.nic[i]);
    }
    return MPI_SUCCESS;
}

static int MPIDI_CH3I_PG_Compare_ids(void *id1, void *id2)
{
    return (strcmp((char *) id1, (char *) id2) == 0) ? TRUE : FALSE;
}

static int MPIDI_CH3I_PG_Destroy(MPIDI_PG_t * pg, void *id)
{
    if (pg->ch.kvs_name != NULL) {
        MPIU_Free(pg->ch.kvs_name);
    }

    if (id != NULL) {
        MPIU_Free(id);
    }

    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Init(MPIDI_PG_t * pg, int pg_rank)
{
    int p;
    int ret;
    MPIDI_VC_t *vc;
    ret = MPIDI_CH3I_RMDA_init(pg, pg_rank);
    if (ret != MPI_SUCCESS)
        return ret;
    /*Mark all connections ready*/
    for (p = 0; p < pg->size; p++) {
        MPIDI_PG_Get_vcr(pg, p, &vc);
        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    }
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Finalize()
{
    return MPIDI_CH3I_RMDA_finalize();
}

int MPIDI_CH3I_CM_Connect(MPIDI_VC_t * vc)
{
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Establish(MPIDI_VC_t * vc)
{
    return MPI_SUCCESS;
}

#ifdef MAC_OSX
void
mvapich_malloc_init ()
{
    /* This dylib is Mac OS X specific. For other unix platforms
       replace libc.dylib with libc.so */

    if (vt_malloc_ptr != NULL)
        return;
    vt_dylib = dlopen ("/usr/lib/libc.dylib", RTLD_LAZY);

    if (vt_dylib == NULL) {
        printf ("Open error: Unable to open dynamic library ...\n");
        exit (1);
    }

    vt_malloc_ptr = dlsym (vt_dylib, "malloc");

    if (vt_malloc_ptr == NULL) {
        printf ("Resolution error: Unable to locate symbol for malloc ...\n");
        exit (1);
    }

    vt_free_ptr = dlsym (vt_dylib, "free");

    if (vt_free_ptr == NULL) {
        printf ("Resolution error: Unable to locate symbol for free ...\n");
        exit (1);
    }
}
#endif

