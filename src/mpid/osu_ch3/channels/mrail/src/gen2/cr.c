/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifdef CKPT

#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "libcr.h"
#include "rdma_impl.h"
#include "pmi.h"
#include "cm.h"
#include "mem_hooks.h"

/*Connection info to MPD*/
#define CR_RSRT_PORT_CHANGE  16
#define MAX_CR_MSG_LEN 256
#define CRU_MAX_KEY_LEN  64
#define CRU_MAX_VAL_LEN  64
struct CRU_keyval_pairs {
    char key[CRU_MAX_KEY_LEN];
    char value[CRU_MAX_VAL_LEN];	
};
static struct CRU_keyval_pairs CRU_keyval_tab[64] = { { {0} } };
static int  CRU_keyval_tab_idx = 0;
static int MPICR_MPD_fd = -1;
static int MPICR_MPD_port;

static cr_callback_id_t MPICR_callback_id;
static cr_client_id_t  MPICR_cli_id;
static pthread_t MPICR_child_thread;
static MPIDI_PG_t * MPICR_pg = NULL;
static int MPICR_pg_size = -1;
static int MPICR_pg_rank = -1;
static int MPICR_is_initialized = 0;
static int MPICR_is_restarting = 0;
static int checkpoint_count = 0;
static int restart_count = 0;
static int MPICR_max_save_ckpts = 0;
volatile int MPICR_callback_fin = 0;

volatile MPICR_cr_state MPICR_state = MPICR_STATE_ERROR;
/*
static pthread_mutex_t MPICR_cond_callback_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t MPICR_cond_callback = PTHREAD_COND_INITIALIZER;
*/
static pthread_mutex_t MPICR_cs_lock;
    
#define CR_ERR_ABORT(args...)  do {\
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
    exit(-1);\
}while(0)

#define CR_ERR(args...)  do {\
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
}while(0)

#ifdef CR_DEBUG
#define CR_DBG(args...)  do {\
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__); \
    fprintf(stderr, args); \
}while(0)
#else
#define CR_DBG(args...)
#endif

typedef struct MPICR_remote_update_msg 
{
  void * recv_mem_addr;
  uint32_t recv_buf_rkey[MAX_NUM_HCAS];
}MPICR_remote_update_msg;


int CR_IBU_Reactivate_channels();
int CR_IBU_Suspend_channels();

int CR_MPDU_readline( int fd, char *buf, int maxlen);
int CR_MPDU_writeline( int fd, char *buf);
int CR_MPDU_parse_keyvals( char *st );
char* CR_MPDU_getval( const char *keystr, char *valstr, int vallen);

int CR_MPDU_connect_MPD()
{
    int optval = 1;
    struct hostent     *hp;
    struct sockaddr_in sa;

    MPICR_MPD_fd = socket(AF_INET, SOCK_STREAM, 0 );
    if (MPICR_MPD_fd < 0)
        return -1;

    hp = gethostbyname("localhost");
    bzero( (void *)&sa, sizeof(sa) );
    bcopy( (void *)hp->h_addr, (void *)&sa.sin_addr, hp->h_length);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(MPICR_MPD_port+MPICR_pg_rank+restart_count*CR_RSRT_PORT_CHANGE);
    if (setsockopt( MPICR_MPD_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&optval, sizeof(optval) ))
        CR_ERR_ABORT("setsockopt failed\n"); 
    if (connect( MPICR_MPD_fd, (struct sockaddr *)&sa, sizeof(sa) ) < 0)
        CR_ERR_ABORT("connect %d failed\n",MPICR_MPD_port+MPICR_pg_rank+restart_count);
    return 0;
}

void CR_MPDU_Ckpt_succeed()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf,"cmd=ckpt_rep result=succeed\n");
    CR_MPDU_writeline(MPICR_MPD_fd,cr_msg_buf);
}

void CR_MPDU_Ckpt_fail()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf,"cmd=ckpt_rep result=fail\n");
    CR_MPDU_writeline(MPICR_MPD_fd,cr_msg_buf);
}

void CR_MPDU_Rsrt_succeed()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf,"cmd=rsrt_rep result=succeed\n");
    CR_MPDU_writeline(MPICR_MPD_fd,cr_msg_buf);
}

void CR_MPDU_Rsrt_fail()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf,"cmd=rsrt_rep result=fail\n");
    CR_MPDU_writeline(MPICR_MPD_fd,cr_msg_buf);
}

int CR_MPDU_Reset_PMI_port()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    char value_str[CRU_MAX_VAL_LEN];
    /*Get pmi port number*/
    sprintf(cr_msg_buf,"cmd=query_pmi_port\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
    CR_MPDU_readline(MPICR_MPD_fd, cr_msg_buf, MAX_CR_MSG_LEN);
    CR_DBG("received msg from MPD: %s\n",cr_msg_buf);
    CR_MPDU_parse_keyvals(cr_msg_buf);
    CR_MPDU_getval("cmd",value_str, CRU_MAX_VAL_LEN);
    if (strcmp(value_str,"reply_pmi_port")!=0)
        return -1;
    CR_MPDU_getval("val",value_str, CRU_MAX_VAL_LEN);
    setenv("PMI_PORT",value_str,1);
    return 0;
}


/*
CR lock to protect upper layers from accessing communication channel
*/
inline void MPIDI_CH3I_CR_lock()
{
/*
    pthread_t current_thread = pthread_self();
    if (current_thread != MPICR_child_thread) {
*/  
    pthread_mutex_lock(&MPICR_cs_lock);
/*    }*/
}

inline void MPIDI_CH3I_CR_unlock()
{
/*    
    pthread_t current_thread = pthread_self();
    if (current_thread != MPICR_child_thread) {
*/
        pthread_mutex_unlock(&MPICR_cs_lock);
/*    }*/
}

MPICR_cr_state MPIDI_CH3I_CR_Get_state()
{
    return MPICR_state;
}

int CR_Set_state(MPICR_cr_state state)
{
    CR_DBG("Change state to");
    switch (state) {
        case MPICR_STATE_CHECKPOINTING:
            CR_DBG("MPICR_STATE_CHECKPOINTING\n");
            break;
        case MPICR_STATE_ERROR:
            CR_DBG("MPICR_STATE_ERROR\n");
            break;
        case MPICR_STATE_POST_COORDINATION:
            if (MPICR_is_restarting) {
            }
            else {
            }
            CR_DBG("MPICR_STATE_POST_COORDINATION\n");
            break;
        case MPICR_STATE_PRE_COORDINATION:
            CR_DBG("MPICR_STATE_PRE_COORDINATION\n");
            break;
        case MPICR_STATE_REQUESTED:
            CR_DBG("MPICR_STATE_REQUESTED\n");
            break;
        case MPICR_STATE_RESTARTING:
            CR_DBG("MPICR_STATE_RESTARTING\n");
            MPICR_is_restarting = 1;
            break;
        case MPICR_STATE_RUNNING:
            if (MPICR_STATE_ERROR!=state)
            {
                if (MPICR_is_restarting) {
                }
                else {
                }
            }
            CR_DBG("MPICR_STATE_RUNNING\n");
            break;
        default:
            CR_DBG("Unknown state\n");
            return -1;
    }

      MPICR_state = state;
      return 0;
}


int CR_Thread_loop()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    fd_set set;
    char valstr[CRU_MAX_VAL_LEN];
    while(1)
    {
        FD_ZERO(&set);
        FD_SET(MPICR_MPD_fd, &set);
        if (select(MPICR_MPD_fd+1, &set, NULL, NULL, NULL)<0)
        {
            CR_ERR_ABORT("select failed\n");
        }
        CR_MPDU_readline(MPICR_MPD_fd, cr_msg_buf, MAX_CR_MSG_LEN);
        CR_DBG("Got request from MPD %s\n",cr_msg_buf);
        CR_MPDU_parse_keyvals(cr_msg_buf);
        CR_MPDU_getval("cmd",valstr, CRU_MAX_VAL_LEN);
        if (strcmp(valstr,"ckpt_req")==0) {
            char cr_file[CRU_MAX_VAL_LEN];
            int cr_file_fd;
            checkpoint_count++;
            CR_MPDU_getval("file",valstr,CRU_MAX_VAL_LEN);
            CR_DBG("Got checkpoint request %s\n",valstr);
            sprintf(cr_file,"%s.%d.%d",valstr,checkpoint_count,MPICR_pg_rank);
            CR_Set_state(MPICR_STATE_REQUESTED);
            CR_DBG("locking CR\n");
            MPIDI_CH3I_CR_lock();
            CR_DBG("locking CM\n");
            MPICM_lock(); /*Lock will be finalized in suspension*/
            CR_DBG("locked\n");
            CR_Set_state(MPICR_STATE_PRE_COORDINATION);
            if (CR_IBU_Suspend_channels()) {
                CR_MPDU_Ckpt_fail();
                CR_ERR_ABORT("CR_IBU_Suspend_channels failed\n");
            }

            CR_Set_state(MPICR_STATE_CHECKPOINTING);
            cr_file_fd = open(cr_file, O_CREAT | O_WRONLY | O_TRUNC ,0666);
            if (cr_file_fd<0)
            {
                CR_MPDU_Ckpt_fail();
                CR_ERR_ABORT("checkpoint file creation failed\n");
            }
            MPICR_callback_fin=0;
            cr_request_fd(cr_file_fd);
            CR_DBG("cr_request_fd\n");
            while(MPICR_callback_fin==0);
            /*
            CR_DBG("checkpointing, wait for callback to finish\n");
            pthread_mutex_lock(&MPICR_cond_callback_lock);
            pthread_cond_wait(&MPICR_cond_callback, &MPICR_cond_callback_lock);
            pthread_mutex_unlock(&MPICR_cond_callback_lock);
            */
            if (getenv("MV2_CKPT_NO_SYNC")==NULL) {
                CR_DBG("fsync\n");
                fsync(cr_file_fd);
                CR_DBG("fsync done\n");
            }
            close(cr_file_fd);
            CR_Set_state(MPICR_STATE_POST_COORDINATION);

            if (CR_IBU_Reactivate_channels())
            {
                if (MPICR_is_restarting)
                    CR_MPDU_Rsrt_fail();
                else
                    CR_MPDU_Ckpt_fail();
                CR_ERR_ABORT("CR_IBU_Reactivate_channels failed\n");
            }
            CR_Set_state(MPICR_STATE_RUNNING);
            if (MPICR_is_restarting)
                CR_MPDU_Rsrt_succeed();
            else {
                CR_MPDU_Ckpt_succeed();
                if (MPICR_max_save_ckpts > 0 && MPICR_max_save_ckpts < checkpoint_count)
                {
                    /*remove the ealier checkpoints*/
                    sprintf(cr_file,"%s.%d.%d",valstr,checkpoint_count-MPICR_max_save_ckpts,MPICR_pg_rank);
                    unlink(cr_file);
                }
            }
            MPIDI_CH3I_CR_unlock();
            MPICR_is_restarting = 0;
        }
        else {
            CR_ERR_ABORT("Unknown command\n");
        }
    }
}

int CR_Reset_proc_info()
{
    int has_parent;
    int pg_id_sz;
    int kvs_name_sz;
    free(MPIDI_Process.my_pg->id);
    free(MPIDI_Process.my_pg->ch.kvs_name);
    unsetenv("PMI_FD");
    CR_DBG("unset PMI_FD\n");
    if (PMI_Init(&has_parent)) {
        CR_ERR_ABORT("PMI_Init failed\n");
    }
    CR_DBG("PMI_Init\n");
    if (PMI_Get_id_length_max(&pg_id_sz))  {
        CR_ERR_ABORT("PMI_Get_id_length_max failed\n");
    }
    CR_DBG("PMI_Get_id_length_max\n");
    MPIDI_Process.my_pg->id = MPIU_Malloc(pg_id_sz+1);
    if (NULL == MPIDI_Process.my_pg->id) {
        CR_ERR_ABORT("MPIU_Malloc failed\n");
    }
    if (PMI_Get_id(MPIDI_Process.my_pg->id,pg_id_sz)) {
        CR_ERR_ABORT("PMI_Get_id failed\n");
    }
    if (PMI_KVS_Get_name_length_max(&kvs_name_sz)) {
        CR_ERR_ABORT("PMI_KVS_Get_name_length_max failed\n");
    }
    MPIDI_Process.my_pg->ch.kvs_name = MPIU_Malloc(kvs_name_sz+1);
    if (NULL == MPIDI_Process.my_pg->id) {
        CR_ERR_ABORT("MPIU_Malloc failed\n");
    }
    if (PMI_KVS_Get_my_name(MPIDI_Process.my_pg->ch.kvs_name,kvs_name_sz)) {
        CR_ERR_ABORT("PMI_KVS_Get_my_name failed\n");
    }
    return 0;    
}

static int CR_Callback(void *arg)
{
    int rc;

    CR_DBG("In CR_Callback\n");
    rc = cr_checkpoint(0);

    if (rc < 0) {
        CR_MPDU_Ckpt_fail();
        CR_ERR_ABORT("cr_checkpoint failed\n");
    }
    else if (rc)
    { 
        /*Build the pipe between mpdman and app procs*/
        CR_Set_state(MPICR_STATE_RESTARTING);
        restart_count++;
        if (CR_MPDU_connect_MPD())
        {
            CR_ERR_ABORT("CR_MPDU_connect_MPD failed\n");
        }
        CR_DBG("MPD connected\n");
        if (CR_MPDU_Reset_PMI_port())
        {
            CR_MPDU_Rsrt_fail();
            CR_ERR_ABORT("CR_MPDU_Reset_PMI_port failed\n");
        }
        CR_DBG("PMI_port reset\n");
        if (CR_Reset_proc_info())
        {
            CR_MPDU_Rsrt_fail();
            CR_ERR_ABORT("CR_Reset_proc_info failed\n");
        }
        CR_DBG("proc info reset\n");
    }

    CR_DBG("Out CR_Callback\n");
    MPICR_callback_fin = 1;
/*
    pthread_cond_signal(&MPICR_cond_callback);
*/
    return 0;
}

void* CR_Thread_entry(void *arg)
{
    checkpoint_count = restart_count = 0;
    MPICR_cli_id = cr_init();
    if (MPICR_cli_id < 0)
        CR_ERR_ABORT("cr_init failed\n");
    MPICR_callback_id = cr_register_callback(CR_Callback,NULL,CR_THREAD_CONTEXT);
    MPICR_is_initialized = 1;
    /*set MPICR_MPD_port*/
    {
        char *temp;
        temp = getenv("MV2_CKPT_MPD_BASE_PORT");
        if (temp) {
            MPICR_MPD_port = atoi(temp);
        }
        else {
            CR_ERR_ABORT("MV2_CKPT_MPD_BASE_PORT is not set\n");
        }
    }
    {
        char *temp = getenv("MV2_CKPT_MAX_SAVE_CKPTS");
        if (temp) {
            MPICR_max_save_ckpts = atoi(temp);
            CR_DBG("MV2_CKPT_MAX_SAVE_CKPTS  %s\n",temp);
        }
        else {
            MPICR_max_save_ckpts = 0;
        }
    }

    CR_DBG("Connecting to MPD\n");
    if (CR_MPDU_connect_MPD())  {
        CR_ERR_ABORT("CR_MPDU_connect_MPD failed\n");
    }
    CR_Set_state(MPICR_STATE_RUNNING);
    CR_DBG("Finish initialization, going to CR_Thread_loop\n");
    if (CR_Thread_loop())
      printf("cr_loop terminated\n");
    return NULL;
}

int MPIDI_CH3I_CR_Init(MPIDI_PG_t *pg, int rank, int size)
{
    MPICR_pg = pg;
    MPICR_pg_rank = rank;
    MPICR_pg_size = size;
    /*Initialize the cr lock MPICR_cs_lock*/
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
        if (pthread_mutex_init(&MPICR_cs_lock, &attr)) {
            CR_ERR_ABORT("pthread_mutex_init failed\n");    
        }
        pthread_mutexattr_destroy(&attr);
    }
    /*Create a new thread*/
    CR_DBG("Creating a new thread for running cr controller\n");
    pthread_create(&MPICR_child_thread,NULL,CR_Thread_entry,NULL);
    return 0;
}

int MPIDI_CH3I_CR_Finalize()
{
    if (0==MPICR_is_initialized)
        return 0;
    pthread_cancel(MPICR_child_thread);
    pthread_join(MPICR_child_thread, NULL);
    pthread_mutex_destroy(&MPICR_cs_lock);
    if (MPICR_MPD_fd>0)
        close(MPICR_MPD_fd);
    MPICR_is_initialized = 0;
    return 0;
}

/*
CR message handler in progress engine
*/
void MPIDI_CH3I_CR_Handle_recv(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf * v)
{
    MPICR_remote_update_msg *msg;
    struct MPID_Request *sreq;
    if (msg_type == MPIDI_CH3_PKT_CR_REMOTE_UPDATE) {
        CR_DBG("Received MPIDI_CH3_PKT_CR_REMOTE_UPDATE\n");
        msg = (MPICR_remote_update_msg *)
            (v->buffer+sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header));
        sreq = (struct MPID_Request *)vc->mrail.sreq_head;
        CR_DBG("Looking for address match %p\n",msg->recv_mem_addr);
        while (sreq) {
            CR_DBG("Verifying request %p\n",sreq);
            if (sreq->mrail.remote_addr == msg->recv_mem_addr) {
                CR_DBG("Found a address match req: %p\n",sreq);
                /*FIXME: Is address match enough ?*/
                memcpy(sreq->mrail.rkey, msg->recv_buf_rkey, sizeof(uint32_t)*MAX_NUM_HCAS);
                CR_DBG("rkey updated hca0:%x\n",sreq->mrail.rkey[0]);
            }
            sreq = sreq->mrail.next_inflow;
        }
    }
    else {
        /*Unkonwn*/
        CR_ERR_ABORT("unknown message type: %d\n",msg_type);
    }
}

void MPIDI_CH3I_CR_Handle_send_completion(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf * v)
{
    if (msg_type == MPIDI_CH3_PKT_CR_REMOTE_UPDATE) {
        /*No handling for this case*/
    }
    else {
        /*Unkonwn*/
        CR_ERR_ABORT("unknown message type: %d\n",msg_type);
    }
}


/*==================*/
/*  IB mangaement functions */
/*==================*/

/*The request involving memory registration. e.g. rndv recv*/
struct MPID_Request* MPICR_req_list_head = NULL;
struct MPID_Request* MPICR_req_list_tail = NULL;

int CR_IBU_Release_network()
{
    int error;
    int pg_rank;
    int pg_size;
    int i;
    int rail_index;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    error = MPICM_Finalize_UD();
    if (error != 0) {
        CR_ERR_ABORT("MPICM_Finalize_UD failed\n");
    }

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank) {
            continue;
        }
        MPIDI_PG_Get_vc(pg, i, &vc);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
            CR_ERR_ABORT("Having active vc when releasing networks\n");
        }
        
        if (vc->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDED)
            continue;

        /*Not support rdma_fast_path*/
        /*        
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
            if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index])
                ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
            if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index])
                ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
        }
        */

        for (rail_index = 0; rail_index < vc->mrail.num_rails;
             rail_index++) {
            ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
        }

#ifdef USE_HEADER_CACHING
        free(vc->mrail.rfp.cached_incoming);
        free(vc->mrail.rfp.cached_outgoing);
#endif    

        /*        
        if (vc->mrail.rfp.RDMA_send_buf_DMA)
            free(vc->mrail.rfp.RDMA_send_buf_DMA);
        if (vc->mrail.rfp.RDMA_recv_buf_DMA)
            free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        if (vc->mrail.rfp.RDMA_send_buf)
            free(vc->mrail.rfp.RDMA_send_buf);
        if (vc->mrail.rfp.RDMA_recv_buf)
            free(vc->mrail.rfp.RDMA_recv_buf);
        */
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

    if (MPIDI_CH3I_RDMA_Process.has_srq) {
        int hca_num = 0;
        for(hca_num = 0; hca_num < rdma_num_hcas; hca_num++) {
            pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[hca_num]);
            pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[hca_num], NULL);
            if (ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num])) {
                ibv_error_abort(IBV_RETURN_ERR, "Couldn't destroy SRQ\n");
            }
        }
    }

/*
    while (dreg_evict());
*/
    dreg_deregister_all();
    
    for (i = 0; i < rdma_num_hcas; i++) {
        ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
        if (MPIDI_CH3I_RDMA_Process.has_one_sided) 
            ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl_1sc[i]);
        deallocate_vbufs(i);
        ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
    }
    
#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    return 0;
}

int CR_IBU_Rebuild_network()
{
    int ret;

    int pg_size, pg_rank;
    MPIDI_PG_t * pg;
    MPIDI_VC_t * vc;
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
    pg = MPICR_pg;
    pg_rank = MPICR_pg_rank;
    pg_size = MPICR_pg_size;

    /* Reading the values from user first and 
     * then allocating the memory */
    /*No need to reinitialize parameters*/
    /*
    ret = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process);
    if (ret) {
        return ret;
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);
    */
    
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

    /*vc structure doesn't need to be initialized */
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);
/*
        memset(&(vc->mrail), 0, sizeof(vc->mrail));
*/
        vc->mrail.num_rails = rdma_num_rails;
        if (MPIDI_CH3I_RDMA_Process.has_srq) {
            MPIDI_CH3I_RDMA_Process.vc_mapping[vc->pg_rank] = vc;
        }
    }
    
    /* Open the device and create cq and qp's */
    ret = rdma_open_hca(&MPIDI_CH3I_RDMA_Process);
    if (ret) {
        fprintf(stderr, "rdma_open_hca failed\n");
        return -1;
    }

    ret = rdma_iba_hca_init_noqp(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
    if (ret) {
        fprintf(stderr, "Failed to Initialize HCA type\n");
        return -1;
    }

    /* 
    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;
    */
    
    /* Initialize the registration cache */
    /*
    dreg_init();
    */
    dreg_reregister_all();
    /* Allocate RDMA Buffers */
    /*
    ret = rdma_iba_allocate_memory(&MPIDI_CH3I_RDMA_Process,
                                   pg_rank, pg_size);
    if (ret) {
        return ret;
    }
    */
    vbuf_reregister_all();
    /* Post the buffers for the SRQ */
    
    if (MPIDI_CH3I_RDMA_Process.has_srq) {
        int hca_num = 0;
        
        pthread_spin_init(&MPIDI_CH3I_RDMA_Process.srq_post_lock, 0);
        pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);
        
        for(hca_num = 0; hca_num < rdma_num_hcas; hca_num++) { 
            MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] = 
                viadev_post_srq_buffers(viadev_srq_size, hca_num);
        
            {
                struct ibv_srq_attr srq_attr;
                srq_attr.max_wr = viadev_srq_size;
                srq_attr.max_sge = 1;
                srq_attr.srq_limit = viadev_srq_limit;
                if (ibv_modify_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                            &srq_attr, IBV_SRQ_LIMIT)) {
                    ibv_error_abort(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
                }
                /* Start the async thread which watches for SRQ limit events */
                pthread_create(&MPIDI_CH3I_RDMA_Process.async_thread[hca_num], NULL,
                        (void *) async_thread, (void *) MPIDI_CH3I_RDMA_Process.nic_context[hca_num]);
            }
        }
        pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_lock);
    }
    
    {
        /*Init UD*/
        /*
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
        */
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
            CR_ERR_ABORT("MPIU_Malloc failed\n");
        }
        PMI_KVS_Get_value_length_max(&val_max_sz);
        assert(error == PMI_SUCCESS);
        val_max_sz++;
        val = MPIU_Malloc(val_max_sz);
        if (val == NULL) {
            CR_ERR_ABORT("MPIU_Malloc failed\n");
        }
        if (key_max_sz < 20 || val_max_sz < 20) {
            CR_ERR_ABORT("key_max_sz val_max_sz too small\n");
        }
        /*Just put lid for default port and ud_qpn is sufficient*/
        sprintf(key,"ud_info_%08d",pg_rank);
        sprintf(val,"%08x:%08x",MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);
        /*printf("Rank %d, my lid: %08x, my qpn: %08x\n", pg_rank,
            MPIDI_CH3I_RDMA_Process.lids[0][0],ud_qpn_self);*/

        error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
        if (error != 0) {
            CR_ERR_ABORT("PMI_KVS_Put failed\n");
        }
        error = PMI_KVS_Commit(pg->ch.kvs_name);
        if (error != 0) {
            CR_ERR_ABORT("PMI_KVS_Commit failed\n");
        }
        error = PMI_Barrier();
        if (error != 0) {
            CR_ERR_ABORT("PMI_Barrier failed\n");
        }
        for (i = 0; i < pg_size; i++) {
            if (pg_rank == i) {
                lid_all[i] = MPIDI_CH3I_RDMA_Process.lids[0][0];
                continue;
            }
            sprintf(key,"ud_info_%08d",i);
            error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
            if (error != 0) {
                CR_ERR_ABORT("PMI_KVS_Get failed\n");
            }
            sscanf(val,"%08x:%08x",&(lid_all[i]),&(ud_qpn_all[i]));
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
        CR_ERR_ABORT("PMI_Barrier failed\n");
    }

    CR_DBG("CR_IBU_Rebuild_network finish\n");
    return 0;
}

int CR_IBU_Prep_remote_update()
{
    struct MPID_Request* temp = MPICR_req_list_head;
    /*Using this to reduce the number of update messages 
    since all consecutive request to a same memory address 
    will be combined to a single update message*/
    void *last_mem_addr = NULL; 
    MPIDI_VC_t *vc;
    vbuf *v;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
    MPICR_remote_update_msg msg;
    int i;
    MPIDI_CH3I_CR_msg_log_queue_entry_t * entry;
    /*each req->mrail.rkey is:*/
    uint32_t rkey[MAX_NUM_HCAS];

    while (temp!=NULL) {
        if (temp->mrail.rndv_buf == last_mem_addr) {
            if (temp->mrail.d_entry->memhandle[0]->rkey != rkey[0])
                CR_ERR_ABORT("Same addr %p, different rkey %x, %x\n",
                        temp->mrail.rndv_buf,temp->mrail.d_entry->memhandle[0]->rkey, rkey[0]);
            temp = temp->ch.cr_queue_next;
            continue;
        }
        CR_DBG("Found a new memory address %p registered\n", temp->mrail.rndv_buf );
        last_mem_addr = temp->mrail.rndv_buf;

        memset(rkey,0,sizeof(uint32_t)*MAX_NUM_HCAS);
        /*Prepare update for rkeys*/
        for (i=0;i<rdma_num_hcas;i++) {
            rkey[i] = temp->mrail.d_entry->memhandle[i]->rkey;
        }
        
        /*Check msg_log_queue because back_log_queue and ext send queue should be empty*/
        vc = temp->ch.vc;
        for (i=0;i<rdma_num_hcas;i++) {
            assert(vc->mrail.srp.credits[i].backlog.len == 0);
            assert(vc->mrail.rails[i].ext_sendq_head == NULL);
        }
        i = 0;
        CR_DBG("Search msg log queue for CTS message with the address %p, in vc %p \n", temp->mrail.rndv_buf, vc);
        entry = vc->mrail.msg_log_queue_head;
        while (NULL!=entry) {
            v = entry->buf;
            i++;
            p = (MPIDI_CH3I_MRAILI_Pkt_comm_header *) v->pheader;
            CR_DBG("In msg_log_Q, type %d\n",p->type);
            if (p->type == MPIDI_CH3_PKT_RNDV_CLR_TO_SEND) {
                struct MPIDI_CH3_Pkt_rndv_clr_to_send * cts_header = (struct MPIDI_CH3_Pkt_rndv_clr_to_send *)p;
                CR_DBG("CTS in msg_log_Q addr %p\n",cts_header->rndv.buf_addr);
                if (cts_header->rndv.buf_addr == temp->mrail.rndv_buf)  {
                    CR_DBG("Found a match in local pending cts\n");
                    memcpy(cts_header->rndv.rkey, rkey, sizeof(uint32_t)*MAX_NUM_HCAS);
                }
            }
            entry = entry->next;
        }
        CR_DBG("Searched %d packets in msg_log_Q\n",i);

        CR_DBG("Checking CM queue for vc %p\n",vc);
        i=0;
        /*Check cm_sendq for all cts packets*/
        if(!MPIDI_CH3I_CM_SendQ_empty(vc)) {
            struct MPID_Request *req;
            CR_DBG("cm_send_Q is not empty\n");
            req = MPIDI_CH3I_CM_SendQ_head(vc);
            CR_DBG("req %p\n",req);
            while (req != NULL) {
                MPIDI_CH3_Pkt_t *upkt = &(req->ch.pkt);
                i++;
                CR_DBG("In cm_send_Q, type %d\n",upkt->type);
                if (upkt->type == MPIDI_CH3_PKT_RNDV_CLR_TO_SEND) {
                    struct MPIDI_CH3_Pkt_rndv_clr_to_send * cts_header = &(upkt->rndv_clr_to_send);
                    CR_DBG("CTS in cm_send_Q addr %p\n",cts_header->rndv.buf_addr);
                    if (cts_header->rndv.buf_addr == temp->mrail.rndv_buf) {
                        CR_DBG("Found a match in local cm queue\n");
                        memcpy(cts_header->rndv.rkey, rkey, sizeof(uint32_t)*MAX_NUM_HCAS);
                    }
                }
                req = req->dev.next;
            }
        }
        CR_DBG("Searched %d packets in cm_send_Q\n",i);

        /*Prepare remote update packet */
        msg.recv_mem_addr = temp->mrail.rndv_buf;
        memcpy(msg.recv_buf_rkey, rkey, sizeof(uint32_t)*MAX_NUM_HCAS);
        
        /*FIXME: use recv_mem_addr as only identifier*/ 
        CR_DBG("recv_mem_addr %p, rkey0 %x\n", msg.recv_mem_addr, msg.recv_buf_rkey[0]);

        v = get_vbuf();
        p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader;
        p->type = MPIDI_CH3_PKT_CR_REMOTE_UPDATE;
        memcpy(v->buffer+sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header)
            ,&msg,sizeof(msg));
            
        /*push update packet to message log queue*/
        entry = (MPIDI_CH3I_CR_msg_log_queue_entry_t *)malloc(sizeof(MPIDI_CH3I_CR_msg_log_queue_entry_t));
        entry->len = sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header)+sizeof(msg);
        entry->buf = v;
        MSG_LOG_ENQUEUE(vc, entry);
        
        temp = temp->ch.cr_queue_next;
    }
    return 0;
}

int CR_IBU_Suspend_channels()
{
    int ret, i;
    MPIDI_VC_t **vc_vector = (MPIDI_VC_t **)malloc(sizeof(MPIDI_VC_t *)*MPICR_pg_size);
    MPIDI_VC_t * vc;

    for (i=0;i<MPICR_pg_size;i++) {
        if (i==MPICR_pg_rank)
            continue;
        MPIDI_PG_Get_vcr(MPICR_pg, i, &vc);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_CONNECTING_CLI);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_CONNECTING_SRV);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_REACTIVATING_SRV);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
        ||  vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING)
            vc_vector[i] = vc;
        else
            vc_vector[i] = NULL;
    }
    
    ret = MPIDI_CH3I_CM_Suspend(vc_vector);
    if (ret)
        return ret;
    ret = CR_IBU_Release_network();
    if (ret)
        return ret;
    return 0;
}

int CR_IBU_Reactivate_channels()
{
    int ret, i;
    MPIDI_VC_t **vc_vector = (MPIDI_VC_t **)malloc(sizeof(MPIDI_VC_t *)*MPICR_pg_size);
    MPIDI_VC_t * vc;

    ret = CR_IBU_Rebuild_network();
    if (ret)
        return ret;
    ret = CR_IBU_Prep_remote_update();
    if (ret)
        return ret;
    for (i=0;i<MPICR_pg_size;i++) {
        if (i==MPICR_pg_rank)
            continue;
        MPIDI_PG_Get_vcr(MPICR_pg, i, &vc);
        /*Now all calling, maybe only small rank reactivate to big rank*/
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_CONNECTING_CLI);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_CONNECTING_SRV);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_SUSPENDING);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_IDLE);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1);
        assert(vc->ch.state!=MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2);

        if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED
         || vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV)
            vc_vector[i] = vc;
        else
            vc_vector[i] = NULL;
    }
    
    ret = MPIDI_CH3I_CM_Reactivate(vc_vector);
    if (ret)
        return ret;
    return 0;
}

void MPIDI_CH3I_CR_req_enqueue(struct MPID_Request * req, MPIDI_VC_t * vc)
{
    if (req==NULL)
        return;
    req->ch.cr_queue_next = NULL;
    req->ch.vc = vc;
    if (NULL==MPICR_req_list_head) {
        MPICR_req_list_tail = MPICR_req_list_head = req;
    }
    else {
        MPICR_req_list_tail->ch.cr_queue_next = req;
        MPICR_req_list_tail = req;
    }
}

void MPIDI_CH3I_CR_req_dequeue(struct MPID_Request * req)
{
    struct MPID_Request *temp;
    if (req==NULL)
        return;
    if (MPICR_req_list_head==NULL)
        return;
    if (MPICR_req_list_head== req) {
        MPICR_req_list_head=req->ch.cr_queue_next;
        if (MPICR_req_list_tail == req) /*Last element in the list*/
            MPICR_req_list_tail=NULL;
        return;
    }
    temp = MPICR_req_list_head;
    while (NULL!=temp->ch.cr_queue_next) {
        if (temp->ch.cr_queue_next == req) {
            temp->ch.cr_queue_next = req->ch.cr_queue_next;
            if (MPICR_req_list_tail == req) /*dequeue last element*/
                MPICR_req_list_tail = temp;
            return;
        }
        temp = temp->ch.cr_queue_next;
    }
}

/*==================*/
/*  MPD messaging functions  */
/*==================*/

int CR_MPDU_readline( int fd, char *buf, int maxlen)
{
    int n, rc;
    char c, *ptr;

    ptr = buf;
    for ( n = 1; n < maxlen; n++ ) {
again:
        rc = read( fd, &c, 1 );
        if ( rc == 1 ) {
            *ptr++ = c;
            if ( c == '\n' )    /* note \n is stored, like in fgets */
                break;
        }
        else if ( rc == 0 ) {
            if ( n == 1 )
                return( 0 );    /* EOF, no data read */
            else
                break;      /* EOF, some data read */
        }
        else {
            if ( errno == EINTR )
                goto again;
            return ( -1 );  /* error, errno set by read */
        }
    }
    *ptr = 0;           /* null terminate, like fgets */
    /*
     printf(" received :%s:\n", buf );
    */
    return( n );
}

int CR_MPDU_writeline( int fd, char *buf)
{
    int size, n;

    size = strlen( buf );
    if ( size > MAX_CR_MSG_LEN ) {
        buf[MAX_CR_MSG_LEN-1] = '\0';
        fprintf(stderr, "write_line: message string too big: :%s:\n", buf );
    }
    else if ( buf[strlen( buf ) - 1] != '\n' )  /* error:  no newline at end */
        fprintf(stderr, "write_line: message string doesn't end in newline: :%s:\n",
                buf );
    else {
        n = write( fd, buf, size );
        if ( n < 0 ) {
            fprintf(stderr, "write_line error; fd=%d buf=:%s:\n", fd, buf );
            return(-1);
        }
        if ( n < size)
            fprintf(stderr, "write_line failed to write entire message\n" );
    }
    return 0;
}
    
int CR_MPDU_parse_keyvals( char *st )
{
    char *p, *keystart, *valstart;

    if ( !st )
        return( -1 );

    CRU_keyval_tab_idx = 0;
    p = st;
    while ( 1 ) {
        while ( *p == ' ' )
            p++;
        /* got non-blank */
        if ( *p == '=' ) {
            fprintf(stderr, "CRU_parse_keyvals:  unexpected = at character %d in %s\n",
                   (int)(p - st), st );
            return( -1 );
        }
        if ( *p == '\n' || *p == '\0' )
            return( 0 );    /* normal exit */
        /* got normal
         * character */
        keystart = p;       /* remember where key started */
        while ( *p != ' ' && *p != '=' && *p != '\n' && *p != '\0' )
            p++;
        if ( *p == ' ' || *p == '\n' || *p == '\0' ) {
            fprintf(stderr,
                    "CRU_parse_keyvals: unexpected key delimiter at character %d in %s\n",
                    (int)(p - st), st );
            return( -1 );
        }
        strncpy( CRU_keyval_tab[CRU_keyval_tab_idx].key, keystart, CRU_MAX_KEY_LEN );
        CRU_keyval_tab[CRU_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

        valstart = ++p;         /* start of value */
        while ( *p != ' ' && *p != '\n' && *p != '\0' )
            p++;
        strncpy( CRU_keyval_tab[CRU_keyval_tab_idx].value, valstart, CRU_MAX_VAL_LEN );
        CRU_keyval_tab[CRU_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
        CRU_keyval_tab_idx++;
        if ( *p == ' ' )
            continue;
        if ( *p == '\n' || *p == '\0' )
            return( 0 );    /* value has been set to empty */
    }
}
    
char* CR_MPDU_getval( const char *keystr, char *valstr, int vallen)
{
    int i;

    for (i = 0; i < CRU_keyval_tab_idx; i++) {
        if ( strcmp( keystr, CRU_keyval_tab[i].key ) == 0 ) {
            strncpy( valstr, CRU_keyval_tab[i].value, vallen - 1 );
            valstr[vallen - 1] = '\0';
            return valstr;
        }
    }
    valstr[0] = '\0';
    return NULL;
}

#endif

