/* Copyright (c) 2003-2011, The Ohio State University. All rights
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
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <infiniband/verbs.h>
#include <infiniband/umad.h>
#include "ibv_param.h"
#include "vbuf.h"
#include "rdma_impl.h"
#include "sysreport.h"
#include "smp_smpi.h"
#include "mv2_utils.h"

/* Extra buffer space for header(s); used to adjust the eager-threshold */
#define EAGER_THRESHOLD_ADJUST    0
#define INLINE_THRESHOLD_ADJUST  (20)
/*
 * ==============================================================
 * Initialize global parameter variables to default values
 * ==============================================================
 */


int           rdma_num_hcas   = 1;
int           rdma_num_req_hcas   = 0;
int           rdma_num_ports  = 1;
int           rdma_num_qp_per_port = 1;
int           rdma_num_rails;
int           rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
unsigned long rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int           rdma_default_port = RDMA_DEFAULT_PORT;
unsigned long rdma_default_max_send_wqe = RDMA_DEFAULT_MAX_SEND_WQE;
unsigned long rdma_default_max_recv_wqe = RDMA_DEFAULT_MAX_RECV_WQE;
uint32_t      rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
uint16_t      rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
uint16_t      rdma_default_pkey = RDMA_DEFAULT_PKEY;
uint8_t       rdma_default_qp_ous_rd_atom; 
uint8_t       rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
enum ibv_mtu  rdma_default_mtu;
uint32_t      rdma_default_psn = RDMA_DEFAULT_PSN;
uint8_t       rdma_default_min_rnr_timer = RDMA_DEFAULT_MIN_RNR_TIMER;
uint8_t       rdma_default_service_level = RDMA_DEFAULT_SERVICE_LEVEL;
uint8_t       rdma_default_static_rate = RDMA_DEFAULT_STATIC_RATE;
uint8_t       rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;
uint8_t       rdma_default_time_out = RDMA_DEFAULT_TIME_OUT;
uint8_t       rdma_default_retry_count = RDMA_DEFAULT_RETRY_COUNT;
uint8_t       rdma_default_rnr_retry = RDMA_DEFAULT_RNR_RETRY;
int           rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int           rdma_read_reserve = RDMA_READ_RESERVE;
long          rdma_eagersize_1sc;
int           rdma_put_fallback_threshold;
int           rdma_get_fallback_threshold;
int           rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
int           rdma_polling_set_limit = -1;
int           rdma_polling_set_threshold = 10;
int           rdma_fp_sendconn_accepted = 0;
int           rdma_pending_conn_request = 0;
int           rdma_eager_limit = 32;
int           rdma_iba_eager_threshold;
char          rdma_iba_hcas[MAX_NUM_HCAS][32];
int           rdma_max_inline_size;
unsigned int  rdma_ndreg_entries = RDMA_NDREG_ENTRIES;
int           rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
int           rdma_r3_threshold = 4096;
int           rdma_r3_threshold_nocache = 8192 * 4;
int           rdma_max_r3_pending_data = 512 * 1024;
int           num_rdma_buffer;
int           rdma_use_smp = 1;
int           rdma_use_qos = 0;
#ifdef ENABLE_3DTORUS_SUPPORT
int           rdma_3dtorus_support = 1;
#else
int           rdma_3dtorus_support = 0;
#endif /* ENABLE_3DTORUS_SUPPORT */
int           rdma_path_sl_query = 0;
int           rdma_num_sa_query_retries = RDMA_DEFAULT_NUM_SA_QUERY_RETRIES;
MPID_Node_id_t rdma_num_nodes_in_job = 0;
int           enable_knomial_2level_bcast=1;
int           inter_node_knomial_factor=4;
int           intra_node_knomial_factor=4;
int           knomial_2level_bcast_message_size_threshold=2048;
int           knomial_2level_bcast_system_size_threshold=64;
int           max_num_win = MAX_NUM_WIN;
int           rdma_qos_num_sls = RDMA_QOS_DEFAULT_NUM_SLS;
int           max_rdma_connect_attempts = DEFAULT_RDMA_CONNECT_ATTEMPTS;
int           rdma_cm_connect_retry_interval = RDMA_DEFAULT_CONNECT_INTERVAL;
int           rdma_default_async_thread_stack_size = RDMA_DEFAULT_ASYNC_THREAD_STACK_SIZE;
int           rdma_num_rails_per_hca = 1;
int           rdma_process_binding_rail_offset = 0;
int           rdma_multirail_usage_policy = MV2_MRAIL_BINDING;
int           rdma_small_msg_rail_sharing_policy = ROUND_ROBIN;
int           rdma_med_msg_rail_sharing_policy = ROUND_ROBIN;
int           rdma_med_msg_rail_sharing_threshold = RDMA_DEFAULT_MED_MSG_RAIL_SHARING_THRESHOLD;
int           rdma_large_msg_rail_sharing_threshold = RDMA_DEFAULT_LARGE_MSG_RAIL_SHARING_THRESHOLD;

/* This is to allow users to specify rail mapping at run time */
int           mrail_user_defined_p2r_mapping = -1;
int           mrail_p2r_length;
int           mrail_use_default_mapping = 0;
char*         mrail_p2r_string = NULL;
/* Threshold of job size beyond which we want to use 2-cq approach */
int           rdma_iwarp_multiple_cq_threshold = RDMA_IWARP_DEFAULT_MULTIPLE_CQ_THRESHOLD;
int           rdma_iwarp_use_multiple_cq = 0;
/* Force to use rendezvous if extended sendq size exceeds this value */
int           rdma_rndv_ext_sendq_size = 5;
/* Global count of extended sendq size across all rails*/
int           rdma_global_ext_sendq_size = 0;
/* Number of times to poll while global ext sendq has outstanding requests */
int           rdma_num_extra_polls = 1;
int           rdma_local_id = -1;
int           rdma_num_local_procs = -1;
/* Whether coalescing of messages should be attempted */
int           rdma_use_coalesce = 1;
int           use_osu_collectives = 1; 
int           use_anl_collectives = 0; 
unsigned long rdma_polling_spin_count_threshold = 5; 
int           use_thread_yield=0; 
/* If this number of eager sends are already outstanding
 * the message can be coalesced with other messages (and
 * will not be sent until a previous message completes)
 */
int           rdma_coalesce_threshold = 6;

/* max (total) number of vbufs to allocate, after which process
 * terminates with a fatal error.
 * -1 means no limit.
 */
int rdma_vbuf_max = -1;
/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int rdma_vbuf_secondary_pool_size = RDMA_VBUF_SECONDARY_POOL_SIZE;

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int rdma_vbuf_pool_size         = RDMA_VBUF_POOL_SIZE;
int rdma_prepost_depth          = RDMA_PREPOST_DEPTH;
int rdma_initial_prepost_depth  = RDMA_INITIAL_PREPOST_DEPTH;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
int rdma_prepost_noop_extra     = 6;
int rdma_credit_preserve;
int rdma_initial_credits        = 0; 

/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + rdma_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
int rdma_rq_size;
int using_mpirun_rsh            = 1;

uint32_t viadev_srq_size = 512;
uint32_t viadev_srq_limit = 30;
uint32_t viadev_max_r3_oust_send = 32;


/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is rdma_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int rdma_prepost_rendezvous_extra   = 10;
int rdma_dynamic_credit_threshold   = 10;
int rdma_credit_notify_threshold    = 10;
int rdma_prepost_threshold          = 5;

unsigned long rdma_max_registered_pages = RDMA_MAX_REGISTERED_PAGES;
unsigned long rdma_dreg_cache_limit = 0;

/* Blocking mode progress */
int rdma_use_blocking = 0;
unsigned long rdma_blocking_spin_count_threshold = 5000;

/* The total size of each vbuf. Used to be the eager threshold, but
 * it can be smaller, so that each eager message will span over few
 * vbufs
 */
int rdma_vbuf_total_size; 

/* Small message scheduling policy
 * Was earlier set to USE_FIRST, optimized for minimal QP cache misses
 * Now setting it to FIXED_MAPPING as we get better performance.
 * 10/06/2010
 */
int rdma_rail_sharing_policy = FIXED_MAPPING;

/* This value should increase with the increase in number
 * of rails */
int striping_threshold = STRIPING_THRESHOLD;

/* Used IBoEth mode */
int use_iboeth = 0;

/* Linear update factor for HSAM */
int alpha = 0.9;
int stripe_factor = 1;
int apm_tester = 0;
int apm_count;

typedef enum _mv2_user_defined_mapping_policies {

    MV2_UDEF_POLICY_BUNCH = 1,
    MV2_UDEF_POLICY_SCATTER,
    MV2_UDEF_POLICY_NONE,

} user_defined_mapping_policies;

/* Optimal CPU Binding parameters */
#ifdef HAVE_LIBHWLOC
int use_hwloc_cpu_binding=1;
#else 
int use_hwloc_cpu_binding=0;
#endif

/* Use of LIMIC of RMA Communication */
int limic_put_threshold;
int limic_get_threshold;

static int check_hsam_parameters(void);

static inline int log_2(int np)
{
    int lgN, t;
    for (lgN = 0, t = 1; t < np; lgN++, t += t);
    return lgN;
}

#undef FUNCNAME
#define FUNCNAME get_hca_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline int get_hca_type (struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i = 0;
    int ret = 0;
    char* dev_name = NULL;
    int mpi_errno = MPI_SUCCESS;
    int num_devices = 0;
    struct ibv_device_attr dev_attr;
    struct ibv_device **dev_list = NULL;

    MPIDI_STATE_DECL(MPID_STATE_GET_HCA_TYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_GET_HCA_TYPE);


    dev_list = ibv_get_device_list(&num_devices);

    MPIU_Memset(&dev_attr, 0, sizeof(struct ibv_device_attr));

    for (i = 0; i < num_devices; ++i) {

        dev_name = (char*) ibv_get_device_name(proc->ib_dev[i]);
    
        if (!dev_name) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER,
                                "**ibv_get_device_name");
        }
    
        ret = ibv_query_device(proc->nic_context[i], &dev_attr);
    
        if (ret) {
            MPIU_ERR_SETANDJUMP1(
                mpi_errno,
                MPI_ERR_OTHER,
                "**ibv_query_device",
                "**ibv_query_device %s",
                dev_name
            );
        }

        proc->hca_type = mv2_get_hca_type( dev_list[i] );
        proc->arch_hca_type = mv2_get_arch_hca_type( dev_list[i] );
        if ( MV2_HCA_UNKWN != proc->hca_type ) {
            /* We've found the HCA */
            break;
        }
    }

fn_fail:
    ibv_free_device_list(dev_list);
    MPIDI_FUNC_EXIT(MPID_STATE_GET_HCA_TYPE);

    return mpi_errno;
}

/*
 * Function: rdma_cm_get_hca_type
 *
 * Description:
 *      Finds out the type of the HCA on the system.
 *
 * Input:
 *      use_iwarp_mode  - Command line input specifying whether we need to use
 *                        iWARP mode.
 * Output:
 *      hca_type        - The type of HCA we are going to use.
 *
 * Return:
 *      Success:    MPI_SUCCESS.
 *      Failure:    ERROR (-1).
 */
#if defined(RDMA_CM)
#undef FUNCNAME
#define FUNCNAME rdma_cm_get_hca_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_cm_get_hca_type (struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i = 0;
    int ret = 0;
    int mpi_errno = MPI_SUCCESS;
    int numdevices = 0;
    struct ibv_device_attr dev_attr;
    char* dev_name = NULL;
    struct ibv_context** ctx = rdma_get_devices(&numdevices);

    MPIDI_STATE_DECL(MPID_STATE_RDMA_CM_GET_HCA_TYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_RDMA_CM_GET_HCA_TYPE);

    for (i = 0; i < numdevices; ++i) {
        proc->hca_type = MV2_HCA_UNKWN;
        dev_name = (char*) ibv_get_device_name(ctx[i]->device);

        if (!dev_name) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER,
                "**ibv_get_device_name");
        }
 
        if ((ret = ibv_query_device(ctx[i], &dev_attr))) {
            MPIU_ERR_SETANDJUMP1(
                mpi_errno,
                MPI_ERR_OTHER,
                "**ibv_query_device",
                "**ibv_query_device %s",
                dev_name
            );
        }

        if (ERROR == rdma_find_active_port(ctx[i], ctx[i]->device)) {
        /* Trac #376 The device has no active ports, continue to next device */
            continue;
        }

        proc->hca_type = mv2_get_hca_type( ctx[i]->device );
        proc->arch_hca_type = mv2_get_arch_hca_type( ctx[i]->device );

        if ( MV2_HCA_CHELSIO_T3 == proc->hca_type ) {
        /* Trac #376 recognize chelsio nic even if it's not the first */
		    proc->use_rdma_cm = 1;
		    proc->use_iwarp_mode = 1;
            MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_RDMA_CM;
            strncpy(rdma_iba_hcas[0], CHELSIO_RNIC, 32);
        } else if ( MV2_HCA_INTEL_NE020 == proc->hca_type ) {
		    proc->use_rdma_cm = 1;
		    proc->use_iwarp_mode = 1;
            MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_RDMA_CM;
            strncpy(rdma_iba_hcas[0], INTEL_NE020_RNIC, 32);
        }

        if ( MV2_HCA_UNKWN != proc->hca_type ) {
            /* We've found the HCA */
            break;
        }
    }

fn_fail:
    rdma_free_devices(ctx);
    MPIDI_FUNC_EXIT(MPID_STATE_RDMA_CM_GET_HCA_TYPE);
    return mpi_errno;
}

#endif /* defined(RDMA_CM) */

/* The rdma_get_process_to_rail_mapping function is called from 
 * ch3_smp_progress.c to set the mapping given by the user at run time
 */
#undef FUNCNAME
#define FUNCNAME rdma_get_process_to_rail_mapping
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_get_process_to_rail_mapping(int mrail_user_defined_p2r_type) 
{
    char* p2r_only_numbers = (char*) MPIU_Malloc((mrail_p2r_length + 1) * sizeof(char));
    int i, j=0;
    int num_devices = 0;
    char* tp = mrail_p2r_string;
    char* cp = NULL;
    char tp_str[mrail_p2r_length + 1];
    struct ibv_device **dev_list = NULL;
    int bunch_hca_count;

    dev_list = ibv_get_device_list(&num_devices);
    if(rdma_num_req_hcas) {
        num_devices = rdma_num_req_hcas;
    }
    switch (mrail_user_defined_p2r_type) {


        case MV2_UDEF_POLICY_NONE:

            if (((mrail_p2r_length + 1)/2) != rdma_num_local_procs) {
                if (rdma_local_id == 0) {
                    fprintf(stderr, "Mapping should contain %d values. "
                    "Falling back to default scheme.\n", rdma_num_local_procs);
                }
                mrail_use_default_mapping = 1;
                rdma_rail_sharing_policy = FIXED_MAPPING;  
            } else {

                while (*tp != '\0') {
                    i = 0;
                    cp = tp;
                    while (*cp != '\0' && *cp != ':' && i < mrail_p2r_length) {
                        ++cp;
                        ++i;
                    }

                    strncpy(tp_str, tp, i);
                    if (atoi(tp) < 0 || atoi(tp) >= num_devices) {
                        if (rdma_local_id == 0) {
                            fprintf(stderr, "\nWarning! : HCA #%d does not "
                                "exist on this machine. Falling back to "
                                "default scheme\n", atoi(tp));
                            }
                        mrail_use_default_mapping = 1;
                        rdma_rail_sharing_policy = FIXED_MAPPING;
                        goto fn_exit;
                    }
                    tp_str[i] = '\0';

                    if (j == rdma_local_id) {
                        mrail_user_defined_p2r_mapping = atoi(tp_str);
                        break;
                    }

                    if (*cp == '\0') {
                        break;
                    }

                    tp = cp;
                    ++tp;
                    ++j;
                }
            }
            break;

        case MV2_UDEF_POLICY_SCATTER:
            mrail_user_defined_p2r_mapping = rdma_local_id % num_devices;
            break;
        case MV2_UDEF_POLICY_BUNCH:
            bunch_hca_count = rdma_num_local_procs / num_devices;
            if((bunch_hca_count * num_devices) < rdma_num_local_procs) {
                bunch_hca_count++; 
            }
            mrail_user_defined_p2r_mapping = rdma_local_id / bunch_hca_count;
            break;
        default:
            if (rdma_local_id == 0) {
                fprintf(stderr, "\nError determining type of user defined"
                    " binding. Falling back to default scheme...!");
            }
            mrail_use_default_mapping = 1;
            rdma_rail_sharing_policy = FIXED_MAPPING;  
            break;
    }
fn_exit:
    /* Housekeeping operations*/
    ibv_free_device_list(dev_list);
    MPIU_Free(p2r_only_numbers);
    MPIU_Free(mrail_p2r_string);
    mrail_p2r_string = NULL;
    p2r_only_numbers = NULL;

    return 0;
}

#undef FUNCNAME
#define FUNCNAME rdma_get_rail_sharing_policy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_get_rail_sharing_policy(char *value)
{
    int policy = FIXED_MAPPING;

    if (!strcmp(value, "USE_FIRST")) {
        policy = USE_FIRST;
    } else if (!strcmp(value, "ROUND_ROBIN")) {
        policy = ROUND_ROBIN;
    } else if (!strcmp(value, "FIXED_MAPPING")) {
        policy = FIXED_MAPPING;
    } else {
        MPIU_Usage_printf("Invalid small message scheduling\n");
    }
    return policy;
}

#undef FUNCNAME
#define FUNCNAME rdma_get_control_parameters
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_get_control_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i = 0;
    int size = -1;
    int my_rank = -1;
    char* value = NULL;
    int mpi_errno = MPI_SUCCESS;
    int mrail_user_defined_p2r_type = 0;

    MPIDI_STATE_DECL(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);
    MPIDI_FUNC_ENTER(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);

    proc->arch_type = mv2_get_arch_type();

    proc->global_used_send_cq = 0; 
    proc->global_used_recv_cq = 0;

    PMI_Get_rank(&my_rank);

    if ((value = getenv("MV2_NUM_NODES_IN_JOB")) != NULL) {
        rdma_num_nodes_in_job = atoi(value);
    } else {
        MPID_Get_max_node_id(NULL, &rdma_num_nodes_in_job);
        /* For some reason, MPID_Get_max_node_id does a '--' before
         * returning the num_nodes, hence incrementing it by 1 */
        rdma_num_nodes_in_job++;
    }

#ifdef ENABLE_QOS_SUPPORT
    if ((value = getenv("MV2_USE_QOS")) != NULL) {
        rdma_use_qos = !!atoi(value);
    }

    if ((value = getenv("MV2_3DTORUS_SUPPORT")) != NULL) {
        rdma_3dtorus_support = !!atoi(value);
    }

    if ((value = getenv("MV2_PATH_SL_QUERY")) != NULL) {
        rdma_path_sl_query = !!atoi(value);
    }

    if ((value = getenv("MV2_NUM_SLS")) != NULL) {
        rdma_qos_num_sls = atoi(value);
        if (rdma_qos_num_sls <= 0 && rdma_qos_num_sls > RDMA_QOS_MAX_NUM_SLS) {
            rdma_qos_num_sls = RDMA_QOS_DEFAULT_NUM_SLS;
        }
        /* User asked us to use multiple SL's without enabling QoS globally. */
        if (rdma_use_qos == 0) {
            rdma_use_qos = 1;
        }
    }
#endif /* ENABLE_QOS_SUPPORT */

    if ((value = getenv("MV2_NUM_SA_QUERY_RETRIES")) != NULL) {
        rdma_num_sa_query_retries = !!atoi(value);
        if (rdma_num_sa_query_retries < RDMA_DEFAULT_NUM_SA_QUERY_RETRIES) {
            rdma_num_sa_query_retries = RDMA_DEFAULT_NUM_SA_QUERY_RETRIES;
        }
    }

    if ((value = getenv("MV2_MAX_RDMA_CONNECT_ATTEMPTS")) != NULL) {
        max_rdma_connect_attempts = atoi(value);
        if (max_rdma_connect_attempts <= 0) {
            max_rdma_connect_attempts = DEFAULT_RDMA_CONNECT_ATTEMPTS;
        }
    }

    if ((value = getenv("MV2_RDMA_CM_CONNECT_RETRY_INTERVAL")) != NULL) {
        rdma_cm_connect_retry_interval = atoi(value);
        if (rdma_cm_connect_retry_interval <= 0) {
            rdma_cm_connect_retry_interval = RDMA_DEFAULT_CONNECT_INTERVAL;
        }
    }

    if ((value = getenv("MV2_NUM_HCAS")) != NULL) {
        rdma_num_req_hcas = atoi(value);

        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;

        if (rdma_num_req_hcas > MAX_NUM_HCAS) {
            rdma_num_req_hcas = MAX_NUM_HCAS;

	        MPIU_Msg_printf("Warning, max hca is %d, change %s in ibv_param.h "
		        "to overide the option\n", MAX_NUM_HCAS, "MAX_NUM_HCAS");
        }
    }

    for (i = 0; i < MAX_NUM_HCAS; ++i) {
        strncpy(rdma_iba_hcas[i], RDMA_IBA_NULL_HCA, 32);
    }

    if ((value = getenv("MV2_IBA_HCA")) != NULL) {
        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;
        rdma_num_req_hcas = 0;
        {
            char *tok = NULL;
            char *inp = value;

            tok = strtok(inp, ":");
            inp = NULL;
            while (tok != NULL) {
                strncpy(rdma_iba_hcas[rdma_num_req_hcas], tok, 32);
                tok = strtok(inp, ":");
                DEBUG_PRINT("tok = %s, hca name = %s, hca num = %d\n", tok,
                        rdma_iba_hcas[rdma_num_req_hcas], rdma_num_req_hcas);
                rdma_num_req_hcas++;
            }
        }
    }

    /* Parameters to decide the p2r mapping 
     * The check for this parameter should always be done before we check for 
     * MV2_SM_SCHEDULING below as we find out if the user has specified a
     * mapping for the user defined scheme to take effect */
    if ((value = getenv("MV2_PROCESS_TO_RAIL_MAPPING")) != NULL) {
        mrail_p2r_length = strlen(value);

        mrail_p2r_string = (char*) MPIU_Malloc(mrail_p2r_length * sizeof(char));

        strcpy(mrail_p2r_string, value);
        mrail_p2r_string[mrail_p2r_length] = '\0';
        if (!strcmp(value, "BUNCH")) {
            mrail_user_defined_p2r_type = MV2_UDEF_POLICY_BUNCH;
        }
        else if (!strcmp(value, "SCATTER")) {
            mrail_user_defined_p2r_type = MV2_UDEF_POLICY_SCATTER;
        } else {
            mrail_user_defined_p2r_type = MV2_UDEF_POLICY_NONE;
        }
        rdma_get_process_to_rail_mapping(mrail_user_defined_p2r_type);
    } else {
        mrail_use_default_mapping = 1;
    }

    /* Start HSAM Parameters */
    if ((value = getenv("MV2_USE_HSAM")) != NULL) {
        proc->has_hsam = atoi(value);
        if (proc->has_hsam) {
            check_hsam_parameters();
        }
    } else {
        /* By default disable the HSAM, due to problem with
         * multi-pathing with current version of opensm and
         * up/down */
        proc->has_hsam = 0;
    }

    proc->has_apm = (value = getenv("MV2_USE_APM")) != NULL ?
                                               (int) atoi(value) : 0;
    apm_tester = (value = getenv("MV2_USE_APM_TEST")) != NULL ?
                                               (int) atoi(value) : 0;
    apm_count = (value = getenv("MV2_APM_COUNT")) != NULL ?
                                               (int) atoi(value) : APM_COUNT;

    /* Scheduling Parameters */
    if ((value = getenv("MV2_SM_SCHEDULING")) != NULL) {
        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;
        rdma_rail_sharing_policy = rdma_get_rail_sharing_policy(value);
    }

    if ((value = getenv("MV2_SMALL_MSG_RAIL_SHARING_POLICY")) != NULL) {
        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;
        rdma_small_msg_rail_sharing_policy
                      = rdma_get_rail_sharing_policy(value);
    }

    if ((value = getenv("MV2_MED_MSG_RAIL_SHARING_POLICY")) != NULL) {
        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;
        rdma_med_msg_rail_sharing_policy
                      = rdma_get_rail_sharing_policy(value);
    }

    if ((value = getenv("MV2_RAIL_SHARING_POLICY")) != NULL) {
        rdma_multirail_usage_policy = MV2_MRAIL_SHARING;
        rdma_rail_sharing_policy
                      = rdma_med_msg_rail_sharing_policy
                      = rdma_small_msg_rail_sharing_policy
                      = rdma_get_rail_sharing_policy(value);
    }

    /* End : HSAM Parameters */

#if defined(RDMA_CM)
    if ((value = getenv("MV2_USE_IWARP_MODE")) != NULL) {
        proc->use_rdma_cm = !!atoi(value);
        proc->use_iwarp_mode = !!atoi(value);
    }
    
    if (!proc->use_rdma_cm){
	    if ((value = getenv("MV2_USE_RDMA_CM")) != NULL) {
		    proc->use_rdma_cm = !!atoi(value);
	    } else {
		    proc->use_rdma_cm = 0;
		    proc->use_iwarp_mode = 0;
	    }
    }

    if ((value = getenv("MV2_SUPPORT_DPM")) && !!atoi(value)) {
        proc->use_rdma_cm = 0;
        proc->use_iwarp_mode = 0;
    }

    if (proc->use_rdma_cm) {
        int rank = ERROR;
	    int pg_size = ERROR;
	    int threshold = ERROR;

        if (proc->use_iwarp_mode) {
            /* Trac #423 */
	        threshold = MPIDI_CH3I_CM_DEFAULT_IWARP_ON_DEMAND_THRESHOLD;
        } else {
	        threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;
        }

	    PMI_Get_size(&pg_size);
	    PMI_Get_rank(&rank);                                      

	    if ((value = getenv("MV2_ON_DEMAND_THRESHOLD")) != NULL){
		    threshold = atoi(value);
	    }
	    if (pg_size > threshold) {
		    proc->use_rdma_cm_on_demand = 1;
	    }	    
    }
#endif

    if ((mpi_errno = rdma_open_hca(proc)) != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }
    
    /* Set default parameter acc. to the first hca */
#if defined(RDMA_CM)
    if (proc->use_rdma_cm) {
	    if ((mpi_errno = rdma_cm_get_hca_type(proc)) != MPI_SUCCESS) {
		    MPIU_ERR_POP(mpi_errno);
	    }
    }
    else 
#endif /* defined(RDMA_CM) */
    if ((mpi_errno = get_hca_type(proc)) != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }

    if (rdma_num_nodes_in_job == 0) {
        PMI_Get_size(&size); 
    } else {
        size = rdma_num_nodes_in_job;
    }

    if (size <= 8) {
        proc->cluster_size = VERY_SMALL_CLUSTER;
    } else if (size <= 32) {
        proc->cluster_size = SMALL_CLUSTER;
    } else if (size < 128) {
        proc->cluster_size = MEDIUM_CLUSTER;
    } else {
        proc->cluster_size = LARGE_CLUSTER;
    }

    proc->has_srq = (value = getenv("MV2_USE_SRQ")) != NULL ? !!atoi(value) : 1;

    if ((value = getenv("MV2_IWARP_MULTIPLE_CQ_THRESHOLD")) != NULL) {
        rdma_iwarp_multiple_cq_threshold = atoi(value);
        if (rdma_iwarp_multiple_cq_threshold < 0) {
            rdma_iwarp_multiple_cq_threshold =
                                    RDMA_IWARP_DEFAULT_MULTIPLE_CQ_THRESHOLD;
        }
    }

    if (size > rdma_iwarp_multiple_cq_threshold) {
        rdma_iwarp_use_multiple_cq = 1;
    }

#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        proc->has_srq = 1;
        MPIU_Assert (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND);
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
        rdma_use_coalesce = 0;
        rdma_use_blocking = 0;
    }
#endif /* _ENABLE_XRC_ */
   
    if (proc->has_srq
        && proc->hca_type != MV2_HCA_PATH_HT
        && proc->hca_type != MV2_HCA_MLX_PCI_X
        && proc->hca_type != MV2_HCA_IBM_EHCA
#if defined(RDMA_CM)
        && !proc->use_iwarp_mode
#endif /* defined(RDMA_CM) */
    )
    {
        proc->post_send = post_srq_send;
    }
    else
    {
        proc->has_srq = 0;
        proc->post_send = post_send;
    }

#if defined(CKPT)
    proc->has_adaptive_fast_path = 0;
    rdma_polling_set_limit = 0; 
#else /* defined(CKPT) */
    if ((value = getenv("MV2_USE_RDMA_FAST_PATH")) != NULL) {
        proc->has_adaptive_fast_path = !!atoi(value);

	    if (!proc->has_adaptive_fast_path) {
	        rdma_polling_set_limit = 0;
        }
    } else {
        proc->has_adaptive_fast_path = 1;
    }
#endif /* defined(CKPT) */
    
#if !defined(DISABLE_PTMALLOC)
    proc->has_lazy_mem_unregister = (value = getenv("MV2_USE_LAZY_MEM_UNREGISTER")) != NULL ? !!atoi(value) : 1;
#endif /* !defined(DISABLE_PTMALLOC) */

#if defined(CKPT)
    proc->has_one_sided = 0;
#else /* defined(CKPT) */
    proc->has_one_sided = (value = getenv("MV2_USE_RDMA_ONE_SIDED")) != NULL ? !!atoi(value) : 1; 

    if ((value = getenv("MV2_MAX_NUM_WIN")) != NULL) {
        max_num_win = atoi(value);
        if (max_num_win <= 0) {
             proc->has_one_sided = 0;
        }
    }

    proc->has_limic_one_sided = 
          (value = getenv("MV2_USE_LIMIC_ONE_SIDED")) != NULL ? !!atoi(value) : 1;
#endif /* defined(CKPT) */

    if ((value = getenv("MV2_RNDV_EXT_SENDQ_SIZE")) != NULL) {
        rdma_rndv_ext_sendq_size = atoi(value);
        if (rdma_rndv_ext_sendq_size <= 1) {
            MPIU_Usage_printf("Setting MV2_RNDV_EXT_SENDQ_SIZE smaller than 1 "
                              "will severely limit the MPI bandwidth.\n");
        }
    }

    if ((value = getenv("MV2_RDMA_NUM_EXTRA_POLLS")) != NULL) {
        rdma_num_extra_polls = atoi(value);
        if (rdma_num_extra_polls <= 0 ) {
                rdma_num_extra_polls = 1;
        }
    }

    if ((value = getenv("MV2_COALESCE_THRESHOLD")) != NULL) {
        rdma_coalesce_threshold = atoi(value);
        if (rdma_coalesce_threshold < 1) {
            MPIU_Usage_printf("MV2_COALESCE_THRESHOLD must be >= 1\n");
            rdma_coalesce_threshold = 1;
        }
    }

#ifdef _ENABLE_XRC_
    if (!USE_XRC) {
#endif
        if ((value = getenv("MV2_USE_COALESCE")) != NULL) {
            rdma_use_coalesce = !!atoi(value);
        }
#ifdef _ENABLE_XRC_
    }
#endif

    if (proc->hca_type == MV2_HCA_MLX_CX_DDR ||
        proc->hca_type == MV2_HCA_MLX_CX_SDR ||
        proc->hca_type == MV2_HCA_MLX_CX_QDR) {
        rdma_use_coalesce = 0;
    }

    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {
        rdma_use_smp = !!atoi(value);
    }

    if ((value = getenv("MV2_USE_RDMAOE")) != NULL) {
        use_iboeth = !!atoi(value);
        if (!rdma_use_smp) {
            if (0 == my_rank) {
                MPIU_Usage_printf("RDMAoE mode cannot function without SHMEM."
                                "Falling back to use SHMEM.\r\n"
                                "Please do NOT set MV2_USE_SHARED_MEM=0.\r\n");
            }
            rdma_use_smp = 1;
        }
    }

#ifdef _ENABLE_XRC_
    if (!USE_XRC) {
#endif
    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        rdma_use_blocking = !!atoi(value);

        /* Automatically turn off RDMA fast path */
        if(rdma_use_blocking) {
            rdma_use_smp = 0;
            proc->has_adaptive_fast_path = 0;
        }
    }
#ifdef _ENABLE_XRC_
    }
#endif

    if ((value = getenv("MV2_SPIN_COUNT")) != NULL) {
        rdma_blocking_spin_count_threshold = atol(value);
    }

    if ((value = getenv("MV2_RNDV_PROTOCOL")) != NULL) {
        if (strncmp(value,"RPUT", 4) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        } else if (strncmp(value,"RGET", 4) == 0
#ifdef _ENABLE_XRC_
                && !USE_XRC
#endif
                ) {
#if defined(CKPT)
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\" or \"R3\" when checkpoint is enabled\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
#else /* defined(CKPT) */
            rdma_rndv_protocol = VAPI_PROTOCOL_RGET;
#endif /* defined(CKPT) */
        } else if (strncmp(value,"R3", 2) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_R3;
        } else {
#ifdef _ENABLE_XRC_
            if(!USE_XRC)
#endif
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\", \"RGET\", or \"R3\"\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        }
    }

    if ((value = getenv("MV2_R3_THRESHOLD")) != NULL) {
        rdma_r3_threshold = user_val_to_bytes(value,"MV2_R3_THRESHOLD");
        if (rdma_r3_threshold < 0) {
            rdma_r3_threshold = 0;
        }
    }

    if ((value = getenv("MV2_R3_NOCACHE_THRESHOLD")) != NULL) {
        rdma_r3_threshold_nocache = user_val_to_bytes(value,"MV2_R3_NOCACHE_THRESHOLD");
        if (rdma_r3_threshold_nocache < 0) {
            rdma_r3_threshold_nocache = 0;
        }
    }

    if ((value = getenv("MV2_MAX_R3_PENDING_DATA")) !=NULL) {
        rdma_max_r3_pending_data = user_val_to_bytes(value,"MV2_MAX_R3_PENDING_DATA");
        if (rdma_max_r3_pending_data < 0) {
            rdma_max_r3_pending_data = 0;
        }
    }

#if defined(RDMA_CM)
    if (proc->use_rdma_cm_on_demand){
	    proc->use_iwarp_mode = 1;
    }
#endif /* defined(RDMA_CM) */

/* Reading SMP user parameters */

    g_smp_eagersize = SMP_EAGERSIZE;
    s_smpi_length_queue = SMPI_LENGTH_QUEUE;
    s_smp_num_send_buffer = SMP_NUM_SEND_BUFFER;
    s_smp_batch_size = SMP_BATCH_SIZE;

    if ((value = getenv("SMP_EAGERSIZE")) != NULL) {
        g_smp_eagersize = user_val_to_bytes(value,"SMP_EAGERSIZE");
        default_eager_size = 0;
    }

    if ((value = getenv("SMPI_LENGTH_QUEUE")) != NULL) {
        s_smpi_length_queue = user_val_to_bytes(value,"SMPI_LENGTH_QUEUE");
    }

    if ((value = getenv("SMP_NUM_SEND_BUFFER")) != NULL ) {
        s_smp_num_send_buffer = atoi(value);
    }
    if ((value = getenv("SMP_BATCH_SIZE")) != NULL ) {
       s_smp_batch_size = atoi(value);
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_RDMA_GET_CONTROL_PARAMETERS);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/* Set params based on cluster size */
static void rdma_set_params_based_on_cluster_size ( int cluster_size,
        int lc_vbuf_total_size, int lc_num_rdma_buff,
        int mc_vbuf_total_size, int mc_num_rdma_buff,
        int sc_vbuf_total_size, int sc_num_rdma_buff,
        int vsc_vbuf_total_size, int vsc_num_rdma_buff,
        int def_vbuf_total_size, int def_num_rdma_buff )
{
    switch( cluster_size ){

        case LARGE_CLUSTER:
            rdma_vbuf_total_size = lc_vbuf_total_size + EAGER_THRESHOLD_ADJUST;
            num_rdma_buffer = lc_num_rdma_buff;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            break;

        case MEDIUM_CLUSTER:
            rdma_vbuf_total_size = mc_vbuf_total_size + EAGER_THRESHOLD_ADJUST;
            num_rdma_buffer = mc_num_rdma_buff;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            break;
        case SMALL_CLUSTER:
            rdma_vbuf_total_size = sc_vbuf_total_size + EAGER_THRESHOLD_ADJUST;
            num_rdma_buffer = sc_num_rdma_buff;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            break;
        case VERY_SMALL_CLUSTER:
            rdma_vbuf_total_size = vsc_vbuf_total_size + EAGER_THRESHOLD_ADJUST;
            num_rdma_buffer = vsc_num_rdma_buff;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            break;
        default:
            rdma_vbuf_total_size = def_vbuf_total_size + EAGER_THRESHOLD_ADJUST;
            num_rdma_buffer = def_num_rdma_buff;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            break;
    }
}

/* Set thresholds for Nnum_rail=4 */
static void  rdma_set_default_parameters_numrail_4(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->arch_hca_type) {
        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 8 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 4 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 128;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X:
        CASE_MV2_ANY_ARCH_WITH_IBM_EHCA:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024, 16, /* Values for medium cluster size */
                    12*1024, 32, /* Values for small cluster size */
                    12*1024, 32, /* Values for very small cluster size */
                    12*1024, 32);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 64;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                    16*1024, 16, /* Values for large cluster size */
                    16*1024, 16, /* Values for medium cluster size */
                    16*1024, 16, /* Values for small cluster size */
                    16*1024, 16, /* Values for very small cluster size */
                    16*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }
}

/* Set thresholds for Nnum_rail=3 */
static void  rdma_set_default_parameters_numrail_3(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->arch_hca_type) {
        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 8 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 4 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 128;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR:
        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X:
        CASE_MV2_ANY_ARCH_WITH_IBM_EHCA:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024, 16, /* Values for medium cluster size */
                    12*1024, 32, /* Values for small cluster size */
                    12*1024, 32, /* Values for very small cluster size */
                    12*1024, 32);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 64;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                    16*1024, 16, /* Values for large cluster size */
                    16*1024, 16, /* Values for medium cluster size */
                    16*1024, 16, /* Values for small cluster size */
                    16*1024, 16, /* Values for very small cluster size */
                    16*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }
}

/* Set thresholds for Nnum_rail=2 */
static void  rdma_set_default_parameters_numrail_2(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->arch_hca_type) {
        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 8 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 4 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;
    
        case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 128;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X:
        CASE_MV2_ANY_ARCH_WITH_IBM_EHCA:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024, 16, /* Values for medium cluster size */
                    12*1024, 32, /* Values for small cluster size */
                    12*1024, 32, /* Values for very small cluster size */
                    12*1024, 32);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 64;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                    16*1024, 16, /* Values for large cluster size */
                    16*1024, 16, /* Values for medium cluster size */
                    16*1024, 16, /* Values for small cluster size */
                    16*1024, 16, /* Values for very small cluster size */
                    16*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }
}

/* Set thresholds for Nnum_rail=1 */
static void  rdma_set_default_parameters_numrail_1(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->arch_hca_type) {
        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 8 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 4 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 128;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR: 
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X:
        CASE_MV2_ANY_ARCH_WITH_IBM_EHCA:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024, 16, /* Values for medium cluster size */
                    12*1024, 32, /* Values for small cluster size */
                    12*1024, 32, /* Values for very small cluster size */
                    12*1024, 32);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 64;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                    16*1024, 16, /* Values for large cluster size */
                    16*1024, 16, /* Values for medium cluster size */
                    16*1024, 16, /* Values for small cluster size */
                    16*1024, 16, /* Values for very small cluster size */
                    16*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }
}


/* Set thresholds for Nnum_rail=unknown */
static void  rdma_set_default_parameters_numrail_unknwn(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch(proc->arch_hca_type) {
        
        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024, 16, /* Values for medium cluster size */
                    12*1024, 32, /* Values for small cluster size */
                    12*1024, 32, /* Values for very small cluster size */
                    12*1024, 32);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 64;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                     2*1024,  4, /* Values for large cluster size */
                     4*1024,  8, /* Values for medium cluster size */
                     9*1024, 16, /* Values for small cluster size */
                    32*1024, 16, /* Values for very small cluster size */
                    32*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_set_params_based_on_cluster_size( proc->cluster_size, 
                    16*1024, 16, /* Values for large cluster size */
                    16*1024, 16, /* Values for medium cluster size */
                    16*1024, 16, /* Values for small cluster size */
                    16*1024, 16, /* Values for very small cluster size */
                    16*1024, 16);/* Values for unknown cluster size */
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }
}

/* Set thresholds for Nnum_rail=unknown */
static void  set_limic_thresholds (struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    switch ( proc->arch_type ) {
        case MV2_ARCH_AMD_BARCELONA_16:
            limic_put_threshold  = 1 * 1024;
            limic_get_threshold  = 256;
            break;
        case MV2_ARCH_INTEL_CLOVERTOWN_8:
            limic_put_threshold  = 1 * 1024;
            limic_get_threshold  = 1 * 1024;
            break; 
        case MV2_ARCH_INTEL_NEHALEM_8:
            limic_put_threshold  = 8 * 1024;
            limic_get_threshold  = 4 * 1024;
            break;
        default:
            limic_put_threshold  = 8 * 1024;
            limic_get_threshold  = 8 * 1024;
            break;
    }
}

void  rdma_set_default_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    mv2_multirail_info_type multirail_info = mv2_get_multirail_info();

    if ((LARGE_CLUSTER == proc->cluster_size) ||
            (MEDIUM_CLUSTER == proc->cluster_size)) {
        rdma_default_max_send_wqe = 16;
        rdma_max_inline_size = 0;
    }

    /* Setting the default values; these values are fine-tuned for specific platforms 
       in the following code */
    rdma_vbuf_total_size = 12 * 1024 + EAGER_THRESHOLD_ADJUST;
    num_rdma_buffer = 16;
    rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;

    rdma_eagersize_1sc = 4 * 1024;
    rdma_put_fallback_threshold = 2 * 1024;
    rdma_get_fallback_threshold = 192 * 1024;

    switch ( multirail_info ) {

        case mv2_num_rail_4:
            /* Set thresholds for Nnum_rail=4 */
            rdma_set_default_parameters_numrail_4( proc );
            break;

        case mv2_num_rail_3:
            /* Set thresholds for Nnum_rail=3 */
            rdma_set_default_parameters_numrail_3( proc );
            break;
        case mv2_num_rail_2:
            /* Set thresholds for Nnum_rail=2 */
            rdma_set_default_parameters_numrail_2( proc );
            break;

        case mv2_num_rail_1:
            /* Set thresholds for Nnum_rail=1 */
            rdma_set_default_parameters_numrail_1( proc );
            break;

            /* mv2_num_rail_unknwon */
        default:
            rdma_set_default_parameters_numrail_unknwn( proc );
            break;
    }

    if ( MV2_HCA_PATH_HT == proc->hca_type ) {
        rdma_default_qp_ous_rd_atom = 1;
    } else {
        rdma_default_qp_ous_rd_atom = 4;
    }

    if ( MV2_HCA_IBM_EHCA  == proc->hca_type ) {
        rdma_max_inline_size = -1;
    } else if ( MV2_HCA_CHELSIO_T3 == proc->hca_type ) {
        rdma_max_inline_size = 64;
    } else if ( MV2_HCA_INTEL_NE020 == proc->hca_type ) {
        rdma_max_inline_size = 64;
    } else {
        rdma_max_inline_size = 128 + INLINE_THRESHOLD_ADJUST;
    }

    if ( MV2_HCA_MLX_PCI_EX_DDR == proc->hca_type ) {
        rdma_default_mtu = IBV_MTU_2048;
    } else if( MV2_HCA_MLX_CX_QDR == proc->hca_type ) {
        rdma_default_mtu = IBV_MTU_2048;
    } else {
        rdma_default_mtu = IBV_MTU_1024;
    }

    if ( MV2_HCA_CHELSIO_T3 == proc->hca_type ) {
        /* Trac #423 */
        struct ibv_device_attr dev_attr;
        int mpi_errno = MPI_SUCCESS;

        /*quering device for cq depth*/
        mpi_errno = ibv_query_device(proc->nic_context[0], &dev_attr);

        if (!mpi_errno) {
            if (dev_attr.max_cqe < rdma_default_max_cq_size) {
                rdma_default_max_cq_size = dev_attr.max_cqe;
            }
        } else {
            rdma_default_max_cq_size = RDMA_DEFAULT_IWARP_CQ_SIZE;
        }
        rdma_prepost_noop_extra = 8;
    }

    if ( MV2_HCA_INTEL_NE020 == proc->hca_type ) {
        rdma_default_max_cq_size = 32766;
        rdma_prepost_noop_extra = 8;
    }

    if (proc->has_srq) {
        rdma_credit_preserve = 100;
    } else {
        rdma_credit_preserve = 3;
    }

    /* Set Limic Thresholds */
    set_limic_thresholds ( proc );
    return;
}

/* rdma_param_handle_heterogenity resets control parameters given the hca_type 
 * from all ranks. Parameters may change:
 *      rdma_default_mtu
 *      rdma_iba_eager_threshold
 *      proc->has_srq
 *      rdma_credit_preserve
 *      rdma_max_inline_size
 *      rdma_default_qp_ous_rd_atom
 *      rdma_put_fallback_threshold
 *      rdma_get_fallback_threshold
 *      num_rdma_buffer
 *      rdma_vbuf_total_size
 */
void rdma_param_handle_heterogenity(uint32_t hca_type[], int pg_size)
{
    uint32_t type;
    int heterogenous = 0;
    int i;

    type = hca_type[0];
    for (i = 0; i < pg_size; ++ i) {
        if (hca_type[i] == MV2_HCA_PATH_HT ||
            hca_type[i] == MV2_HCA_MLX_PCI_X ||
            hca_type[i] == MV2_HCA_IBM_EHCA ) {
            MPIDI_CH3I_RDMA_Process.has_srq = 0;
            MPIDI_CH3I_RDMA_Process.post_send = post_send;
            rdma_credit_preserve = 3;
        }

        if ( MV2_HCA_IBM_EHCA == hca_type[i] )
            rdma_max_inline_size = -1;                
        
        if ( MV2_HCA_PATH_HT == hca_type[i] )
            rdma_default_qp_ous_rd_atom = 1;

        if (hca_type[i] != type)
            heterogenous = 1;

        DEBUG_PRINT("rank %d, type %d\n", i, hca_type[i]);
    }

    if (heterogenous) {
        DEBUG_PRINT("heterogenous hcas detected\n");
        rdma_default_mtu = IBV_MTU_1024;
        rdma_vbuf_total_size = 8 * 1024 + EAGER_THRESHOLD_ADJUST;
        rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
        rdma_max_inline_size = (rdma_max_inline_size == -1) ? -1 : 64;
        rdma_put_fallback_threshold = 4 * 1024;
        rdma_get_fallback_threshold = 192 * 1024;
        num_rdma_buffer = 16;
    }
}

void rdma_get_user_parameters(int num_proc, int me)
{
    char *value;

    /* Check for a system report. See sysreport.h and sysreport.c */
    value = getenv( SYSREPORT_ENABLE_PARAM_NAME );
    if (value != NULL)
    {
        enable_sysreport = atoi(value);
    }


    if ((value = getenv("MV2_DEFAULT_MTU")) != NULL) {

        if (strncmp(value,"IBV_MTU_256",11)==0) {
            rdma_default_mtu = IBV_MTU_256;
        } else if (strncmp(value,"IBV_MTU_512",11)==0) {
            rdma_default_mtu = IBV_MTU_512;
        } else if (strncmp(value,"IBV_MTU_1024",12)==0) {
            rdma_default_mtu = IBV_MTU_1024;
        } else if (strncmp(value,"IBV_MTU_2048",12)==0) {
            rdma_default_mtu = IBV_MTU_2048;
        } else if (strncmp(value,"IBV_MTU_4096",12)==0) {
            rdma_default_mtu = IBV_MTU_4096;
        } else {
            rdma_default_mtu = IBV_MTU_1024;
        }
    }


    if ((value = getenv("MV2_USE_KNOMIAL_2LEVEL_BCAST")) != NULL) { 
        enable_knomial_2level_bcast=!!atoi(value);
        if (enable_knomial_2level_bcast <= 0)  { 
            enable_knomial_2level_bcast = 0;
        } 
    }     

    if ((value = getenv("MV2_KNOMIAL_INTRA_NODE_FACTOR")) != NULL) {
        intra_node_knomial_factor=atoi(value);
        if (intra_node_knomial_factor < INTRA_NODE_KNOMIAL_FACTOR_MIN) { 
            intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if (intra_node_knomial_factor > INTRA_NODE_KNOMIAL_FACTOR_MAX) { 
            intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MAX;
        } 
    }     

    if ((value = getenv("MV2_KNOMIAL_INTER_NODE_FACTOR")) != NULL) {
        inter_node_knomial_factor=atoi(value);
        if (inter_node_knomial_factor < INTER_NODE_KNOMIAL_FACTOR_MIN) { 
            inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if (inter_node_knomial_factor > INTER_NODE_KNOMIAL_FACTOR_MAX) { 
            inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MAX;
        } 
    }     

    if ((value = getenv("MV2_KNOMIAL_2LEVEL_BCAST_MESSAGE_SIZE_THRESHOLD"))
        != NULL) {
        knomial_2level_bcast_message_size_threshold=atoi(value);
    }

    if ((value = getenv("MV2_KNOMIAL_2LEVEL_BCAST_SYSTEM_SIZE_THRESHOLD"))
        != NULL) {
        knomial_2level_bcast_system_size_threshold=atoi(value);
    }

    /* Get number of ports/HCA used by a process */
    if ((value = getenv("MV2_NUM_PORTS")) != NULL) {
        rdma_num_ports = atoi(value);
        if (rdma_num_ports > MAX_NUM_PORTS) {
            rdma_num_ports = MAX_NUM_PORTS;
	        MPIU_Usage_printf("Warning, max ports per hca is %d, change %s in "
		    "ibv_param.h to overide the option\n", MAX_NUM_PORTS,
		    "MAX_NUM_PORTS");
        }
    }

    /* Get number of qps/port used by a process */
    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {

        rdma_num_qp_per_port = atoi(value);

        if (rdma_num_qp_per_port > MAX_NUM_QP_PER_PORT) {
            rdma_num_qp_per_port = MAX_NUM_QP_PER_PORT;
            MPIU_Usage_printf("Warning, max qps per port is %d, change %s in "
		    "ibv_param.h to overide the option\n", MAX_NUM_QP_PER_PORT,
		    "MAX_NUM_QP_PER_PORT");
        }
    }

    if ((value = getenv("MV2_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = atoi(value);
    }
    if ((value = getenv("MV2_MAX_INLINE_SIZE")) != NULL) {
        rdma_max_inline_size = atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_CQ_SIZE")) != NULL) {
        rdma_default_max_cq_size = atoi(value);
    }
    if ((value = getenv("MV2_READ_RESERVE")) != NULL) {
        rdma_read_reserve = atoi(value);
    }
    if ((value = getenv("MV2_NUM_RDMA_BUFFER")) != NULL) { 
        num_rdma_buffer = atoi(value);
    }
    if ((value = getenv("MV2_POLLING_SET_THRESHOLD")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_threshold = atoi(value);
    }
    if ((value = getenv("MV2_RDMA_EAGER_LIMIT")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_eager_limit = atoi(value);
	    if (rdma_eager_limit < 0) {
	        rdma_eager_limit = 0;
        }
    }
    if ((value = getenv("MV2_POLLING_SET_LIMIT")) != NULL
        && MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_limit = atoi(value);
        if (rdma_polling_set_limit == -1) {
            rdma_polling_set_limit = log_2(num_proc);
        }
    } else if (MPIDI_CH3I_RDMA_Process.has_adaptive_fast_path) {
        rdma_polling_set_limit = RDMA_DEFAULT_POLLING_SET_LIMIT;
    }
    if ((value = getenv("MV2_VBUF_TOTAL_SIZE")) != NULL) {
        if (RDMA_MIN_VBUF_POOL_SIZE < user_val_to_bytes(value,"MV2_VBUF_TOTAL_SIZE")) {
            rdma_vbuf_total_size = user_val_to_bytes(value,"MV2_VBUF_TOTAL_SIZE") + EAGER_THRESHOLD_ADJUST;
        } else {
            /* We do not accept vbuf size < RDMA_MIN_VBUF_POOL_SIZE */
	        MPIU_Usage_printf("Warning, it is inefficient to use a value for"
                "VBUF which is less than %d. Retaining the system default"
                " value of %d\n", RDMA_MIN_VBUF_POOL_SIZE,
                rdma_vbuf_total_size);
        }
    }

    /* We have read the value of the rendezvous threshold, and the number of
     * rails used for communication, increase the striping threshold
     * accordingly */

    /* Messages in between will use the rendezvous protocol, however will
     * not be striped */

    striping_threshold = rdma_vbuf_total_size * rdma_num_ports *
                         rdma_num_qp_per_port * rdma_num_hcas;
       
    if ((value = getenv("MV2_SRQ_SIZE")) != NULL) {
        viadev_srq_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("MV2_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);

        if (viadev_srq_limit > viadev_srq_size) {
	        MPIU_Usage_printf("SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if ((value = getenv("MV2_IBA_EAGER_THRESHOLD")) != NULL) {
        rdma_iba_eager_threshold = user_val_to_bytes(value,"MV2_IBA_EAGER_THRESHOLD");
    }

    if ((value = getenv("MV2_STRIPING_THRESHOLD")) != NULL) {
        striping_threshold = user_val_to_bytes(value,"MV2_STRIPING_THRESHOLD");
        if (striping_threshold <= 0) {
            /* Invalid value - set to computed value */
            striping_threshold = rdma_vbuf_total_size * rdma_num_ports *
                                    rdma_num_qp_per_port * rdma_num_hcas;
        }
        if (striping_threshold < rdma_iba_eager_threshold) {
            /* checking to make sure that the striping threshold is not less
             * than the RNDV threshold since it won't work as expected.
             */
            striping_threshold = rdma_iba_eager_threshold;
        }
    }

    if ((value = getenv("MV2_RAIL_SHARING_MED_MSG_THRESHOLD")) != NULL) {
        rdma_med_msg_rail_sharing_threshold =
                  user_val_to_bytes(value,"MV2_RAIL_SHARING_MED_MSG_THRESHOLD");
        if (rdma_med_msg_rail_sharing_threshold <= 0) {
            rdma_med_msg_rail_sharing_threshold =
                                    RDMA_DEFAULT_MED_MSG_RAIL_SHARING_THRESHOLD;
        }
    }

    rdma_large_msg_rail_sharing_threshold = rdma_vbuf_total_size;

    if ((value = getenv("MV2_RAIL_SHARING_LARGE_MSG_THRESHOLD")) != NULL) {
        rdma_large_msg_rail_sharing_threshold =
                user_val_to_bytes(value,"MV2_RAIL_SHARING_LARGE_MSG_THRESHOLD");
        if (rdma_large_msg_rail_sharing_threshold <= 0) {
            rdma_large_msg_rail_sharing_threshold = rdma_vbuf_total_size;
        }
    }

    if ((value = getenv("MV2_INTEGER_POOL_SIZE")) != NULL) {
        rdma_integer_pool_size = atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PUT_GET_LIST_SIZE")) != NULL) {
        rdma_default_put_get_list_size = atoi(value);
    }
    if ((value = getenv("MV2_EAGERSIZE_1SC")) != NULL) {
        rdma_eagersize_1sc = atoi(value);
    }
    if ((value = getenv("MV2_PUT_FALLBACK_THRESHOLD")) != NULL) {
        rdma_put_fallback_threshold = atoi(value);
    }
    if ((value = getenv("MV2_GET_FALLBACK_THRESHOLD")) != NULL) {
        rdma_get_fallback_threshold = user_val_to_bytes(value,"MV2_GET_FALLBACK_THRESHOLD");
    }
    if ((value = getenv("MV2_LIMIC_PUT_THRESHOLD")) != NULL) {
        limic_put_threshold = 
               user_val_to_bytes(value,"MV2_LIMIC_PUT_THRESHOLD");
    }
    if ((value = getenv("MV2_LIMIC_GET_THRESHOLD")) != NULL) {
        limic_get_threshold = 
               user_val_to_bytes(value,"MV2_LIMIC_GET_THRESHOLD");
    }
    if ((value = getenv("MV2_DEFAULT_PORT")) != NULL) {
        rdma_default_port = atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_QP_OUS_RD_ATOM")) != NULL) {
        rdma_default_qp_ous_rd_atom = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PSN")) != NULL) {
        rdma_default_psn = (uint32_t)atoi(value);
    }

    if ((value = getenv("MV2_DEFAULT_PKEY")) != NULL) {
        rdma_default_pkey = (uint16_t)strtol(value, (char **) NULL,0)&PKEY_MASK;
    }

    if ((value = getenv("MV2_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        rdma_default_min_rnr_timer = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_SERVICE_LEVEL")) != NULL) {
        rdma_default_service_level = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_TIME_OUT")) != NULL) {
        rdma_default_time_out = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_STATIC_RATE")) != NULL) {
        rdma_default_static_rate = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_SRC_PATH_BITS")) != NULL) {
        rdma_default_src_path_bits = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_RETRY_COUNT")) != NULL) {
        rdma_default_retry_count = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_RNR_RETRY")) != NULL) {
        rdma_default_rnr_retry = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SG_LIST")) != NULL) {
        rdma_default_max_sg_list = (uint32_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SEND_WQE")) != NULL) {
        rdma_default_max_send_wqe = atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_RECV_WQE")) != NULL) {
        rdma_default_max_recv_wqe = atol(value);
    } 
    if ((value = getenv("MV2_NDREG_ENTRIES")) != NULL) {
        rdma_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = getenv("MV2_VBUF_MAX")) != NULL) {
        rdma_vbuf_max = atoi(value);
    }
    if ((value = getenv("MV2_INITIAL_PREPOST_DEPTH")) != NULL) {
        rdma_initial_prepost_depth = atoi(value);
    }    
    if ((value = getenv("MV2_PREPOST_DEPTH")) != NULL) {
        rdma_prepost_depth = atoi(value);
    }  
    if ((value = getenv("MV2_MAX_REGISTERED_PAGES")) != NULL) {
        rdma_max_registered_pages = atol(value);
    }
    if ((value = getenv("MV2_VBUF_POOL_SIZE")) != NULL) {
        rdma_vbuf_pool_size = atoi(value);
    }
    if ((value = getenv("MV2_DREG_CACHE_LIMIT")) != NULL) {
        rdma_dreg_cache_limit = atol(value);
    }
    if (rdma_vbuf_pool_size <= 10) {
        rdma_vbuf_pool_size = RDMA_VBUF_POOL_SIZE;
        MPIU_Usage_printf("Warning! Too small vbuf pool size (%d).  "
		"Reset to %d\n", rdma_vbuf_pool_size, RDMA_VBUF_POOL_SIZE);
    }
    if ((value = getenv("MV2_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
        rdma_vbuf_secondary_pool_size = atoi(value);
    }
    if (rdma_vbuf_secondary_pool_size <= 0) {
        rdma_vbuf_secondary_pool_size = RDMA_VBUF_SECONDARY_POOL_SIZE;
        MPIU_Usage_printf("Warning! Too small secondary vbuf pool size (%d).  "
                "Reset to %d\n", rdma_vbuf_secondary_pool_size,
                RDMA_VBUF_SECONDARY_POOL_SIZE);
    }
    if (rdma_initial_prepost_depth <= rdma_prepost_noop_extra) {
        rdma_initial_credits = rdma_initial_prepost_depth;
    } else {
        rdma_initial_credits =
            rdma_initial_prepost_depth - rdma_prepost_noop_extra;
    }

    rdma_rq_size = rdma_prepost_depth + rdma_prepost_rendezvous_extra +
                   rdma_prepost_noop_extra;
    
    if ((value = getenv("MV2_USE_HWLOC_CPU_BINDING")) != NULL) {
        use_hwloc_cpu_binding = atoi(value);
    }
    if ((value = getenv("MV2_THREAD_YIELD_SPIN_THRESHOLD")) != NULL) {
         rdma_polling_spin_count_threshold = atol(value);
    }
    if ((value = getenv("MV2_USE_THREAD_YIELD")) != NULL) {
         use_thread_yield = atoi(value);
    }
    
    if ((value = getenv("MV2_USE_OSU_COLLECTIVES")) != NULL) {
        if( atoi(value) == 1) { 
              use_osu_collectives = 1; 
        } 
        else { 
              use_osu_collectives = 0; 
              use_anl_collectives = 1; 
        } 
    }

    if ((value = getenv("MV2_ASYNC_THREAD_STACK_SIZE")) != NULL) {
        rdma_default_async_thread_stack_size = atoi(value);
        if(rdma_default_async_thread_stack_size < 1<<10) {
            MPIU_Usage_printf("Warning! Too small stack size for async thread (%d).  "
                    "Reset to %d\n", rdma_vbuf_secondary_pool_size, 
                    RDMA_DEFAULT_ASYNC_THREAD_STACK_SIZE);
            rdma_default_async_thread_stack_size = RDMA_DEFAULT_ASYNC_THREAD_STACK_SIZE;
        }
    }
}

/* This function is specifically written to make sure that HSAM
 * parameters are configured correctly */

static int check_hsam_parameters(void)
{
    char *value;
    int size;

    /* Get the number of processes */
    PMI_Get_size(&size);
 
    /* If the number of processes is less than 64, we can afford * to
     * have more RC QPs and hence a value of 4 is chosen, for * other
     * cases, a value of 2 is chosen */

    /* (rdma_num_qp_per_port/ stripe factor) represents the number
     * of QPs which will be chosen for data transfer at a given point */

    /* If the user has not specified any value, then perform
     * this tuning */

    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {
        rdma_num_qp_per_port = atoi(value);
        if(rdma_num_qp_per_port <= 2) {
            stripe_factor = 1;
        } else {
            stripe_factor = (rdma_num_qp_per_port / 2);
        }
    } else {
        /* Speculated value */

        /* The congestion is actually never seen for less
         * than 8 nodes */
        if((size > 8) && (size < 64)) {
            rdma_num_qp_per_port = 4;
            stripe_factor = (rdma_num_qp_per_port / 2);
        } else {
            rdma_num_qp_per_port = 2;
            stripe_factor = 1;
        }
    }

    return MPI_SUCCESS;
}

int rdma_get_pm_parameters(MPIDI_CH3I_RDMA_Process_t *proc)
{
    int ring_setup;
    char * value;
    value  = getenv("MPIRUN_RSH_LAUNCH");
    if (value == NULL || (atoi(value) != 1)) {
        using_mpirun_rsh = 0;
    }

#if defined(RDMA_CM)
    if ((value = getenv("MV2_USE_RDMA_CM")) != NULL) {
       proc->use_rdma_cm = !!atoi(value);
    }
#endif
    
    if ((value = getenv("MV2_USE_RDMAOE")) != NULL) {
        use_iboeth = !!atoi(value);
    }

    switch (MPIDI_CH3I_Process.cm_type)
    {
        case MPIDI_CH3I_CM_ON_DEMAND:
#if defined(RDMA_CM)
        case MPIDI_CH3I_CM_RDMA_CM:
#endif
            ring_setup = 1;
            if (using_mpirun_rsh) {
                ring_setup = 0;
#ifdef _ENABLE_XRC_
                if (USE_XRC) ring_setup = 1;
#endif 
            }
            proc->has_ring_startup = (value = getenv("MV2_USE_RING_STARTUP")) != NULL ? !!atoi(value) : ring_setup;
            break;
        default:
            proc->has_ring_startup = (value = getenv("MV2_USE_RING_STARTUP")) != NULL ? !!atoi(value) : 1;
            break;
    }
}

/* vi:set sw=4 */
