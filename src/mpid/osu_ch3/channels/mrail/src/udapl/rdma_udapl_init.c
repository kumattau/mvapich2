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


#include "rdma_impl.h"
#include "pmi.h"
#include "udapl_priv.h"
#include "udapl_util.h"

#include "malloc.h"

#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

static int MPIDI_CH3I_PG_Compare_ids (void *id1, void *id2);
static int MPIDI_CH3I_PG_Destroy (MPIDI_PG_t * pg, void *id);

static int cached_pg_size;
static int cached_pg_rank;

extern DAT_EP_HANDLE temp_ep_handle;
extern void
MPIDI_CH3I_RDMA_util_atos (char *str, DAT_SOCK_ADDR * addr);
extern void
MPIDI_CH3I_RDMA_util_stoa (DAT_SOCK_ADDR * addr, char *str);


#ifdef MAC_OSX
extern int mvapich_need_malloc_init;
extern void *vt_dylib;
extern void *(*vt_malloc_ptr) (size_t);
extern void (*vt_free_ptr) (void *);
#endif

MPIDI_PG_t *cached_pg;
rdma_iba_addr_tb_t rdma_iba_addr_table;

#if 0
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_init_process_group
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_RDMA_init_process_group (int *has_parent,
                                    MPIDI_PG_t ** pg_pptr, int *pg_rank_ptr)
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
    rc = PMI_Init (has_parent);
    if (rc != 0)
      {
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_init",
                                    "**pmi_init %d", rc);
          return mpi_errno;
      }
    rc = PMI_Get_rank (&pg_rank);
    if (rc != 0)
      {
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER,
                                    "**pmi_get_rank", "**pmi_get_rank %d",
                                    rc);
          return mpi_errno;
      }
    rc = PMI_Get_size (&pg_size);
    if (rc != 0)
      {
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER,
                                    "**pmi_get_size", "**pmi_get_size %d",
                                    rc);
          return mpi_errno;
      }

    /*
     * Get the process group id
     */
    pmi_errno = PMI_Get_id_length_max (&pg_id_sz);
    if (pmi_errno != PMI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**pmi_get_id_length_max",
                                    "**pmi_get_id_length_max %d", pmi_errno);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }

    pg_id = MPIU_Malloc (pg_id_sz + 1);
    if (pg_id == NULL)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**nomem", NULL);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }

    pmi_errno = PMI_Get_id (pg_id, pg_id_sz);
    if (pmi_errno != PMI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
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
        MPIDI_PG_Init (MPIDI_CH3I_PG_Compare_ids, MPIDI_CH3I_PG_Destroy);
    if (mpi_errno != MPI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**ch3|pg_init", NULL);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }

    /*
     * Create a new structure to track the process group
     */
    mpi_errno = MPIDI_PG_Create (pg_size, pg_id, &cached_pg);
    pg = cached_pg;

    if (mpi_errno != MPI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**ch3|pg_create", NULL);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }
    pg->ch.kvs_name = NULL;


    /*
     * Get the name of the key-value space (KVS)
     */
    pmi_errno = PMI_KVS_Get_name_length_max (&kvs_name_sz);
    if (pmi_errno != PMI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**pmi_kvs_get_name_length_max",
                                    "**pmi_kvs_get_name_length_max %d",
                                    pmi_errno);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }

    pg->ch.kvs_name = MPIU_Malloc (kvs_name_sz + 1);
    if (pg->ch.kvs_name == NULL)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                    FCNAME, __LINE__, MPI_ERR_OTHER,
                                    "**nomem", NULL);
          goto fn_fail;
          /* --END ERROR HANDLING-- */
      }

    pmi_errno = PMI_KVS_Get_my_name (pg->ch.kvs_name, kvs_name_sz);
    if (pmi_errno != PMI_SUCCESS)
      {
          /* --BEGIN ERROR HANDLING-- */
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
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

#if defined(MAC_OSX) && !defined(SOLARIS)
    create_hash_table ();
#elif !defined(SOLARIS)
    mallopt (M_TRIM_THRESHOLD, -1);
    mallopt (M_MMAP_MAX, 0);
#endif
  fn_exit:
    return MPI_SUCCESS;

  fn_fail:
    if (pg != NULL)
      {
          MPIDI_PG_Destroy (pg);
      }
    goto fn_exit;
    return MPI_SUCCESS;
}
#endif

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
    int act_num_cqe;
    DAT_EP_ATTR ep_attr;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;
    DAT_EVENT event;
    int count;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN dummy_registered_size;
    DAT_VADDR dummy_registered_addr;
    DAT_IA_ATTR ia_attr;
    int tmp1;
    int step;
    int num_connected = 0;
    int num_connected_1sc = 0;

    DAT_CONN_QUAL local_service_id;
    DAT_CONN_QUAL local_service_id_1sc;
    DAT_SOCK_ADDR local_ia_addr;

    MPIDI_VC_t *vc;
    int         pg_size;
    int i, error;
    char *key;
    char *val;
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];
    char tmp[512];
    char tmp_hname[256];

#if defined(MAC_OSX) && !defined(SOLARIS)
    create_hash_table ();
#elif !defined(SOLARIS)
    mallopt (M_TRIM_THRESHOLD, -1);
    mallopt (M_MMAP_MAX, 0);
#endif

    gethostname (tmp_hname, 255);
    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size (pg);

    /* Currently only single nic per pg is allowed */
    rdma_iba_addr_table.ia_addr =
        (DAT_SOCK_ADDR **) malloc (pg_size * sizeof (DAT_SOCK_ADDR *));
    rdma_iba_addr_table.hostid = (int **) malloc (pg_size * sizeof (int *));
    rdma_iba_addr_table.service_id =
        (DAT_CONN_QUAL **) malloc (pg_size * sizeof (DAT_CONN_QUAL *));
    rdma_iba_addr_table.service_id_1sc =
        (DAT_CONN_QUAL **) malloc (pg_size * sizeof (DAT_CONN_QUAL *));

    if (!rdma_iba_addr_table.ia_addr
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.service_id
        || !rdma_iba_addr_table.service_id_1sc)
      {
          fprintf (stderr, "Error %s:%d out of memory\n", __FILE__, __LINE__);
          exit (1);
      }


    for (i = 0; i < pg_size; i++)
      {
          rdma_iba_addr_table.ia_addr[i] =
              (DAT_SOCK_ADDR *) malloc (MAX_NUM_HCAS *
                                        sizeof (DAT_SOCK_ADDR));

          rdma_iba_addr_table.hostid[i] =
              (int *) malloc (MAX_NUM_HCAS * sizeof (int));

          rdma_iba_addr_table.service_id[i] =
              (DAT_CONN_QUAL *) malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));

          rdma_iba_addr_table.service_id_1sc[i] =
              (DAT_CONN_QUAL *) malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));
          if (!rdma_iba_addr_table.ia_addr[i]
              || !rdma_iba_addr_table.hostid[i]
              || !rdma_iba_addr_table.service_id[i]
              || !rdma_iba_addr_table.service_id_1sc[i])
            {
                fprintf (stderr, "Error %s:%d out of memory\n",
                         __FILE__, __LINE__);
                exit (1);
            }
      }

    rdma_init_parameters (pg_size, pg_rank);

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++)
      {
          MPIDI_PG_Get_vc (pg, i, &vc);
          memset (&(vc->mrail), 0, sizeof (vc->mrail));
          vc->mrail.num_total_subrails = 1;
          vc->mrail.subrail_per_hca = 1;
      }

    /* Open the device and create cq and qp's */
    MPIDI_CH3I_RDMA_Process.num_hcas = 1;
    rdma_iba_hca_init (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);
    MPIDI_CH3I_RDMA_Process.maxtransfersize = UDAPL_MAX_RDMA_SIZE;
    /* init dreg entry */
    dreg_init (MPIDI_CH3I_RDMA_Process.nic[0],
               MPIDI_CH3I_RDMA_Process.ptag[0]);
    DEBUG_PRINT ("vc->num_subrails %d\n", vc->mrail.num_subrails);
    /* Allocate memory and handlers */
    rdma_iba_allocate_memory (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);

    if (pg_size > 1)
      {

#if !defined(USE_MPD_RING)
          /*Exchange the information about HCA_lid and qp_num */
          /* Allocate space for pmi keys and values */
          error = PMI_KVS_Get_key_length_max (&key_max_sz);
          assert (error == PMI_SUCCESS);
          key_max_sz++;
          key = MPIU_Malloc (key_max_sz);

          if (key == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi key");
                return error;
            }

          PMI_KVS_Get_value_length_max (&val_max_sz);
          assert (error == PMI_SUCCESS);
          val_max_sz++;
          val = MPIU_Malloc (val_max_sz);

          if (val == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi value");
                return error;
            }
          /* STEP 1: Exchange HCA_lid */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;

                sprintf (rdmakey, "%08d-%08d", pg_rank, i);

                MPIDI_CH3I_RDMA_util_atos (rdmavalue,
                                           &(rdma_iba_addr_table.
                                             ia_addr[pg_rank][0]));

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }

                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }

            }
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                  {
                      continue;
                  }
                /* generate the key */
                sprintf (rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                MPIDI_CH3I_RDMA_util_stoa (&
                                           (rdma_iba_addr_table.
                                            ia_addr[i][0]), rdmavalue);
            }

          /* this barrier is to prevent some process from
             overwriting values that has not been get yet */
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* STEP 2: Exchange qp_num */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key and value pair for each connection */
                sprintf (rdmakey, "%08d-%08d", pg_rank, i);
                sprintf (rdmavalue, "%08d",
                         (int) rdma_iba_addr_table.service_id[pg_rank][0]);

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }
                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                rdma_iba_addr_table.service_id[i][0] = atoll (rdmavalue);

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
          DEBUG_PRINT ("After barrier\n");

#ifdef RDMA_FAST_PATH
          /* STEP 3: exchange the information about remote buffer */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;

                MPIDI_PG_Get_vc (pg, i, &vc);

                DEBUG_PRINT ("vc: %p, %p, i %d, pg_rank %d\n", vc,
                             vc->mrail.rfp.RDMA_recv_buf, i, pg_rank);

                /* generate the key and value pair for each connection */
                sprintf (rdmakey, "%08d-%08d", pg_rank, i);
                sprintf (rdmavalue, "%032ld-%016ld",
                         (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                         (DAT_RMR_CONTEXT) vc->mrail.rfp.
                         RDMA_recv_buf_hndl[0].rkey);

                DEBUG_PRINT ("Put %d recv_buf %016lX, key %08X\n",
                             i, (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                             (DAT_RMR_CONTEXT) vc->mrail.rfp.
                             RDMA_recv_buf_hndl[0].rkey);

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }
                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;
                MPIDI_PG_Get_vc (pg, i, &vc);

                /* generate the key */
                sprintf (rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);

                /* we get the remote addresses, now the SendBufferTail.buffer
                   is the address for remote SendBuffer.tail;
                   the RecvBufferHead.buffer is the address for
                   the remote RecvBuffer.head */
                /*format : "%032d-%016d-%032d-%016d" */
                vc->mrail.rfp.remote_RDMA_buf_hndl =
                    malloc (sizeof (VIP_MEM_HANDLE) *
                            MPIDI_CH3I_RDMA_Process.num_hcas);
                strncpy (tmp, rdmavalue, 32);
                tmp[32] = '\0';
                vc->mrail.rfp.remote_RDMA_buf = (void *) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1, 16);
                tmp[16] = '\0';
                vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey =
                    (DAT_RMR_CONTEXT) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1 + 16 + 1, 32);
                tmp[32] = '\0';

                DEBUG_PRINT ("Get %d recv_buf %016lX, key %08X, local_credit %016lX, credit \
                    key %08X\n",
                             i, (aint_t) vc->mrail.rfp.remote_RDMA_buf, (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey,
                             (aint_t) (vc->mrail.rfp.remote_credit_array), (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_credit_update_hndl.
                             rkey);
            }
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
#endif
#ifdef ONE_SIDED
          /* Exchange qp_num */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key and value pair for each connection */
                sprintf (rdmakey, "%08d-%08d", pg_rank, i);
                sprintf (rdmavalue, "%08d",
                         (int) rdma_iba_addr_table.
                         service_id_1sc[pg_rank][0]);

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }
                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; i++)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                rdma_iba_addr_table.service_id_1sc[i][0] = atoll (rdmavalue);

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          MPIU_Free (val);
          MPIU_Free (key);
#endif
#else /*  end  f !defined (USE_MPD_RING) */

          /* Exchange the information about HCA_lid, qp_num, and memory,
           * With the ring-based queue pair */
          rdma_iba_exchange_info (&MPIDI_CH3I_RDMA_Process,
                                  vc, pg_rank, pg_size);
#endif
      }

    /* Enable all the queue pair connections */
    rdma_iba_enable_connections (&MPIDI_CH3I_RDMA_Process,
                                 vc, pg_rank, pg_size);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier ();

    if (error != 0)
      {
          error =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                    "**pmi_barrier %d", error);
          return error;
      }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; i++)
      {
          int j;
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);

          MRAILI_Init_vc (vc, pg_rank);
      }

    error = PMI_Barrier ();
    if (error != 0)
      {
          error =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                    "**pmi_barrier %d", error);
          return error;
      }

    DEBUG_PRINT ("Done MPIDI_CH3I_RDMA_init()!!\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RMDA_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_RMDA_finalize ()
{
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    DAT_RETURN error;
    int pg_rank;
    int pg_size;
    int i, j;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int ret;
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size (pg);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier ();
    if (error != 0)
      {
          error = MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                        __LINE__, MPI_ERR_OTHER,
                                        "**pmi_barrier", "**pmi_barrier %d",
                                        error);
          return error;
      }
#ifdef RDMA_FAST_PATH
    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);
          error = dat_lmr_free (vc->mrail.rfp.RDMA_send_buf_hndl[0].hndl);
          CHECK_RETURN (error, "could unpin send rdma buffer");
          error = dat_lmr_free (vc->mrail.rfp.RDMA_recv_buf_hndl[0].hndl);

          CHECK_RETURN (error, "could unpin recv rdma buffer");
          /* free the buffers */
          free (vc->mrail.rfp.RDMA_send_buf_orig);
          free (vc->mrail.rfp.RDMA_recv_buf_orig);
          free (vc->mrail.rfp.RDMA_send_buf_hndl);
          free (vc->mrail.rfp.RDMA_recv_buf_hndl);
      }
#endif
    deallocate_vbufs (MPIDI_CH3I_RDMA_Process.nic);
    while (dreg_evict ());

    /* STEP 2: destry all the eps, tears down all connections */
    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (pg, i, &vc);
          for (j = 0; j < vc->mrail.num_total_subrails; j++)
            {
                error =
                    dat_ep_disconnect (vc->mrail.qp_hndl[j],
                                       DAT_CLOSE_GRACEFUL_FLAG);
                CHECK_RETURN (error, "fail to disconnect EP");
            }
      }

    PMI_Barrier ();

    /* Be the server or the client, there will be a disconnection event */
    {
        int num_disconnected = 0;
        DAT_EVENT event;
        DAT_COUNT count;
        while (num_disconnected < pg_size - 1)
          {
              if ((DAT_SUCCESS ==
                   dat_evd_wait (MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0],
                                 DAT_TIMEOUT_INFINITE, 1, &event, &count))
                  && (event.event_number ==
                      DAT_CONNECTION_EVENT_DISCONNECTED))
                {
                    num_disconnected++;
                }
          }                     /* while */
    }
#ifdef  ONE_SIDED
    PMI_Barrier ();
    /* Disconnect all the EPs for 1sc  now */

    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (pg, i, &vc);
          error =
              dat_ep_disconnect (vc->mrail.qp_hndl_1sc,
                                 DAT_CLOSE_GRACEFUL_FLAG);
          CHECK_RETURN (error, "fail to disconnect EP");
      }
    /* Be the server or the client, there will be a disconnection event */

    PMI_Barrier ();

    {
        int num_disconnected_1sc = 0;
        DAT_EVENT event;
        DAT_COUNT count;
        while (num_disconnected_1sc < pg_size - 1)
          {
              if ((DAT_SUCCESS ==
                   dat_evd_wait (MPIDI_CH3I_RDMA_Process.conn_cq_hndl_1sc,
                                 DAT_TIMEOUT_INFINITE, 1, &event, &count))
                  && (event.event_number ==
                      DAT_CONNECTION_EVENT_DISCONNECTED))
                {
                    num_disconnected_1sc++;
                }
          }                     /* while */
    }                           /* disconnect event block */

#endif
#ifdef USE_HEADER_CACHING
    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);

          free (vc->mrail.rfp.cached_incoming);
          free (vc->mrail.rfp.cached_outgoing);
      }
#endif

    /* free all the spaces */
    for (i = 0; i < pg_size; i++)
      {
          if (rdma_iba_addr_table.ia_addr[i])
              free (rdma_iba_addr_table.ia_addr[i]);
          if (rdma_iba_addr_table.service_id[i])
              free (rdma_iba_addr_table.service_id[i]);
          if (rdma_iba_addr_table.hostid[i])
              free (rdma_iba_addr_table.hostid[i]);
          if (rdma_iba_addr_table.service_id_1sc[i])
              free (rdma_iba_addr_table.service_id_1sc[i]);
      }
    free (rdma_iba_addr_table.ia_addr);
    free (rdma_iba_addr_table.hostid);
    free (rdma_iba_addr_table.service_id);
    free (rdma_iba_addr_table.service_id_1sc);
    /* STEP 3: release all the cq resource, relaes all the unpinned buffers, release the
     * ptag
     *   and finally, release the hca */
    if (strcmp (dapl_provider, "ccil") == 0)
      {
          dat_ep_free (temp_ep_handle);
      }
    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);
          error = dat_ep_free (vc->mrail.qp_hndl[0]);
          CHECK_RETURN (error, "error freeing ep");
      }

    for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; i++)
      {

          error = dat_psp_free (MPIDI_CH3I_RDMA_Process.psp_hndl[i]);
          CHECK_RETURN (error, "error freeing psp");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.conn_cq_hndl[i]);
          CHECK_RETURN (error, "error freeing creq evd");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.creq_cq_hndl[i]);
          CHECK_RETURN (error, "error freeing conn evd");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
          CHECK_RETURN (error, "error freeing evd");
      }

#ifdef ONE_SIDED
    for (i = 0; i < pg_size; i++)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);
          error = dat_ep_free (vc->mrail.qp_hndl_1sc);
          CHECK_RETURN (error, "error freeing ep for 1SC");
      }

    error = dat_psp_free (MPIDI_CH3I_RDMA_Process.psp_hndl_1sc);
    CHECK_RETURN (error, "error freeing psp 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.conn_cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing creq evd 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.creq_cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing conn evd 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing evd 1SC");
#endif

    error = dat_pz_free (MPIDI_CH3I_RDMA_Process.ptag[0]);
    error =
        dat_ia_close (MPIDI_CH3I_RDMA_Process.nic[0],
                      DAT_CLOSE_GRACEFUL_FLAG);
    if (error != DAT_SUCCESS)
      {
          error =
              dat_ia_close (MPIDI_CH3I_RDMA_Process.nic[0],
                            DAT_CLOSE_ABRUPT_FLAG);
          CHECK_RETURN (error, "fail to close IA");
      }


    return MPI_SUCCESS;
}


static int
MPIDI_CH3I_PG_Compare_ids (void *id1, void *id2)
{
    return (strcmp ((char *) id1, (char *) id2) == 0) ? TRUE : FALSE;
}

static int
MPIDI_CH3I_PG_Destroy (MPIDI_PG_t * pg, void *id)
{
    if (pg->ch.kvs_name != NULL)
      {
          MPIU_Free (pg->ch.kvs_name);
      }

    if (id != NULL)
      {
          MPIU_Free (id);
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

