/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpidimpl.h"
#include "mpidi_ch3i_rdma_conf.h"

#if defined(CKPT)

#include <mpimem.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <libcr.h>
#include "rdma_impl.h"
#include "pmi.h"
#include "cm.h"
#include "mem_hooks.h"
#include "mpiutil.h"

#ifdef CR_FTB

#include <libftb.h>

#define FTB_MAX_SUBSCRIPTION_STR 128

#define CR_FTB_EVENT_INFO {               \
        {"CR_FTB_CKPT_DONE",     "info"}, \
        {"CR_FTB_CKPT_FAIL",     "info"}, \
        {"CR_FTB_RSRT_DONE",     "info"}, \
        {"CR_FTB_RSRT_FAIL",     "info"}, \
        {"CR_FTB_APP_CKPT_REQ",  "info"}, \
        {"CR_FTB_CKPT_FINALIZE", "info"}, \
        {"CR_FTB_CHECKPOINT",    "info"}  \
}

/* Index into the Event Info Table */
#define CR_FTB_CKPT_DONE     0
#define CR_FTB_CKPT_FAIL     1
#define CR_FTB_RSRT_DONE     2
#define CR_FTB_RSRT_FAIL     3
#define CR_FTB_APP_CKPT_REQ  4
#define CR_FTB_CKPT_FINALIZE 5
#define CR_FTB_EVENTS_MAX    6 /* HACK */

#define CR_FTB_CHECKPOINT    6

/* Type of event to throw */
#define FTB_EVENT_NORMAL   1
#define FTB_EVENT_RESPONSE 2

/* Macro to initialize the event property structure */
#define SET_EVENT(_eProp, _etype, _payload...)             \
do {                                                       \
    _eProp.event_type = _etype;                            \
    snprintf(_eProp.event_payload, FTB_MAX_PAYLOAD_DATA,   \
                _payload);                                 \
} while(0)

/* Macro to pick an CR_FTB event */
#define EVENT(n) (cr_ftb_events[n].event_name)

static FTB_client_t        ftb_cinfo;
static FTB_client_handle_t ftb_handle;
static FTB_event_info_t    cr_ftb_events[] = CR_FTB_EVENT_INFO;
static FTB_subscribe_handle_t shandle;
static int ftb_init_done;

static pthread_cond_t  cr_ftb_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cr_ftb_mutex = PTHREAD_MUTEX_INITIALIZER;
static int cr_ftb_ckpt_req;
static int cr_ftb_ckpt_finalize;

static int  CR_FTB_Init(int, char *);
static void CR_FTB_Finalize();
static int  CR_FTB_Callback(FTB_receive_event_t *, void *);
#define CRU_MAX_VAL_LEN 64

#else

/*Connection info to MPD*/
#define CR_RSRT_PORT_CHANGE 16
#define MAX_CR_MSG_LEN 256
#define CRU_MAX_KEY_LEN 64
#define CRU_MAX_VAL_LEN 64

struct CRU_keyval_pairs {
    char key[CRU_MAX_KEY_LEN];
    char value[CRU_MAX_VAL_LEN];	
};

static struct CRU_keyval_pairs CRU_keyval_tab[64] = { { {0} } };
static int  CRU_keyval_tab_idx = 0;
static int MPICR_MPD_fd = -1;

#endif /* CR_FTB */

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

extern int enable_shmem_collectives;
static pthread_mutex_t MPICR_SMC_lock;
static pthread_cond_t MPICR_SMC_cond = PTHREAD_COND_INITIALIZER;
int g_cr_in_progress;


///////////////////////////for test only
int CR_done = 0;
int CR_show_print = 0;
///////////////////////////////


inline void MPIDI_CH3I_SMC_lock()
{
    pthread_mutex_lock(&MPICR_SMC_lock);
    g_cr_in_progress = 1;
    pthread_mutex_unlock(&MPICR_SMC_lock);
}

inline void MPIDI_CH3I_SMC_unlock()
{
    pthread_mutex_lock(&MPICR_SMC_lock);
    g_cr_in_progress = 0;
    pthread_mutex_unlock(&MPICR_SMC_lock);
    pthread_cond_signal(&MPICR_SMC_cond);
}

void Wait_for_CR_Completion()
{
    pthread_mutex_lock(&MPICR_SMC_lock);
    if (g_cr_in_progress) {
	MPIDI_CH3I_CR_unlock();
	pthread_cond_wait(&MPICR_SMC_cond, &MPICR_SMC_lock);
	MPIDI_CH3I_CR_lock();
    }
    pthread_mutex_unlock(&MPICR_SMC_lock);
}

int MPIDI_SMC_CR_Init()
{
    pthread_mutexattr_t attr;
    g_cr_in_progress = 0;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&MPICR_SMC_lock, &attr)) {
	return(1);
    }
    return(0);
}

inline int MPIDI_SMC_CR_Finalize()
{
    pthread_cond_destroy(&MPICR_SMC_cond);
    pthread_mutex_destroy(&MPICR_SMC_lock);
    g_cr_in_progress = 0;
    return(0);
}

volatile MPICR_cr_state MPICR_state = MPICR_STATE_ERROR;
static pthread_rwlock_t MPICR_cs_lock;

#define CR_ERR_ABORT(args...)  do {                                                             \
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                                      \
    exit(-1);                                                                                   \
}while(0)

#define CR_ERR(args...)  do {                                                                   \
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                                      \
}while(0)

#ifdef CR_DEBUG
#define CR_DBG(args...)  do {                                                                   \
    fprintf(stderr, "[Rank %d][%s: line %d]", MPIDI_Process.my_pg_rank ,__FILE__, __LINE__);    \
    fprintf(stderr, args);                                                                      \
}while(0)
#else
#define CR_DBG(args...)
#endif

typedef struct MPICR_remote_update_msg 
{
  void* recv_mem_addr;
  uint32_t recv_buf_rkey[MAX_NUM_HCAS];
} MPICR_remote_update_msg;


int CR_IBU_Reactivate_channels();
int CR_IBU_Suspend_channels();

#ifndef CR_FTB
int CR_MPDU_readline (int fd, char* buf, int maxlen);
int CR_MPDU_writeline (int fd, char* buf);
int CR_MPDU_parse_keyvals (char* st );
char* CR_MPDU_getval (const char* keystr, char* valstr, int vallen);

int CR_MPDU_connect_MPD()
{
    int optval = 1;
    struct sockaddr_in sa;
    int fd, val;
    char session_file[32];

    MPICR_MPD_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (MPICR_MPD_fd < 0)
    {
        return -1;
    }

    struct hostent* hp = gethostbyname("localhost");
    bzero((void*) &sa, sizeof(sa));
    bcopy((void*) hp->h_addr, (void*) &sa.sin_addr, hp->h_length);
    sa.sin_family = AF_INET;

    char *str = getenv("MPIRUN_RSH_LAUNCH");
    if ( str && (atoi(str) == 1) ) {
        snprintf(session_file, 32, "/tmp/cr.session.%s", getenv("MV2_CKPT_SESSIONID"));
        fd = open(session_file, O_RDWR);
        if (fd < 0) {
            perror("open");
            CR_ERR_ABORT("Could not get the Session Id\n");
        }
        if (read(fd, &val, sizeof(val)) < 0) {
            perror("read");
            close(fd);
            CR_ERR_ABORT("Could not read the Spawn Port\n");
        }
        close(fd);
        sa.sin_port = htons(val);
    }
    else {
        /* Legacy Mode */
        sa.sin_port = htons(MPICR_MPD_port + MPICR_pg_rank + restart_count * CR_RSRT_PORT_CHANGE);
    }

    if (setsockopt(MPICR_MPD_fd, IPPROTO_TCP, TCP_NODELAY, (char*) &optval, sizeof(optval)))
    {
        CR_ERR_ABORT("setsockopt failed\n");
    }

    if (connect(MPICR_MPD_fd, (struct sockaddr*) &sa, sizeof(sa)) < 0)
    {
        CR_ERR_ABORT("connect %d failed\n", MPICR_MPD_port + MPICR_pg_rank + restart_count * CR_RSRT_PORT_CHANGE);
    }

    return 0;
}

#endif /* !CR_FTB */

void CR_MPDU_Ckpt_succeed()
{
#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_CKPT_DONE), &eprop, &ehandle);
    if (ret != FTB_SUCCESS)
        fprintf(stdout, "CR_MPDU_Ckpt_succeed() failed\n");
#else
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf, "cmd=ckpt_rep result=succeed\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
#endif
}

void CR_MPDU_Ckpt_fail()
{
#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_CKPT_FAIL), &eprop, &ehandle);
    if (ret != FTB_SUCCESS)
        fprintf(stdout, "CR_MPDU_Ckpt_fail() failed\n");
#else
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf, "cmd=ckpt_rep result=fail\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
#endif
}

void CR_MPDU_Rsrt_succeed()
{
#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_RSRT_DONE), &eprop, &ehandle);
    if (ret != FTB_SUCCESS)
        fprintf(stdout, "CR_MPDU_Rsrt_succeed() failed\n");
#else
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf, "cmd=rsrt_rep result=succeed\n");
    CR_MPDU_writeline(MPICR_MPD_fd,cr_msg_buf);
#endif
}

void CR_MPDU_Rsrt_fail()
{
#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_RSRT_FAIL), &eprop, &ehandle);
    if (ret != FTB_SUCCESS)
        fprintf(stdout, "CR_MPDU_Rsrt_fail() failed\n");
#else
    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf, "cmd=rsrt_rep result=fail\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
#endif
}

#ifndef CR_FTB

int CR_MPDU_Reset_PMI_port()
{
    char cr_msg_buf[MAX_CR_MSG_LEN];
    char value_str[CRU_MAX_VAL_LEN];

    /*Get pmi port number*/
    sprintf(cr_msg_buf, "cmd=query_pmi_port\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
    CR_MPDU_readline(MPICR_MPD_fd, cr_msg_buf, MAX_CR_MSG_LEN);
    CR_DBG("received msg from MPD: %s\n", cr_msg_buf);
    CR_MPDU_parse_keyvals(cr_msg_buf);
    CR_MPDU_getval("cmd", value_str, CRU_MAX_VAL_LEN);

    if (strcmp(value_str, "reply_pmi_port") != 0)
    {
        return -1;
    }

    CR_MPDU_getval("val", value_str, CRU_MAX_VAL_LEN);
    setenv("PMI_PORT", value_str, 1);
    return 0;
}

#endif /* CR_FTB */


/*
CR lock to protect upper layers from accessing communication channel
*/
inline void MPIDI_CH3I_CR_lock()
{
  /*
  * If the current thread has already acquired the wrlock,
  * don't bother acquiring the rdlock.
  */
    if (MPICR_cs_lock.__data.__writer == syscall(SYS_gettid))
        return;

    pthread_rwlock_rdlock(&MPICR_cs_lock);
}

inline void MPIDI_CH3I_CR_unlock()
{
    /*
     * If the current thread has already acquired the wrlock,
     * you did not acquire the reader lock. So don't bother to
     * release the rdlock.
   */
    if (MPICR_cs_lock.__data.__writer == syscall(SYS_gettid))
        return;

    pthread_rwlock_unlock(&MPICR_cs_lock);
}

MPICR_cr_state MPIDI_CH3I_CR_Get_state()
{
    return MPICR_state;
}

int CR_Set_state(MPICR_cr_state state)
{
    switch (state)
    {
    case MPICR_STATE_CHECKPOINTING:
            CR_DBG("MPICR_STATE_CHECKPOINTING\n");
        break;
    case MPICR_STATE_ERROR:
            CR_DBG("MPICR_STATE_ERROR\n");
        break;
    case MPICR_STATE_POST_COORDINATION:
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
#ifndef CR_FTB
    char cr_msg_buf[MAX_CR_MSG_LEN];
    fd_set set;
#endif
    char valstr[CRU_MAX_VAL_LEN];

    cr_checkpoint_handle_t cr_handle;
    cr_checkpoint_args_t   cr_args;

    while (1)
    {

#ifdef CR_FTB
        pthread_mutex_lock(&cr_ftb_mutex);
        cr_ftb_ckpt_req = 0;
        pthread_cond_wait(&cr_ftb_cond, &cr_ftb_mutex);
        pthread_mutex_unlock(&cr_ftb_mutex);

        if (cr_ftb_ckpt_finalize) return;

        /* The FTB Callback thread will set cr_ftb_ckpt_req */
        if (cr_ftb_ckpt_req)
#else
        FD_ZERO(&set);
        FD_SET(MPICR_MPD_fd, &set);

        if (select(MPICR_MPD_fd + 1, &set, NULL, NULL, NULL) < 0)
        {
            CR_ERR_ABORT("select failed\n");
        }

        CR_MPDU_readline(MPICR_MPD_fd, cr_msg_buf, MAX_CR_MSG_LEN);
        CR_DBG("Got request from MPD %s\n", cr_msg_buf);
        CR_MPDU_parse_keyvals(cr_msg_buf);
        CR_MPDU_getval("cmd",valstr, CRU_MAX_VAL_LEN);

        if (strcmp(valstr,"ckpt_req") == 0)
#endif /* CR_FTB */

        {
            char cr_file[CRU_MAX_VAL_LEN];
            int cr_file_fd;

            ++checkpoint_count;

#ifdef CR_FTB
            strcpy(valstr, getenv("MV2_CKPT_FILE"));
#else
            CR_MPDU_getval("file",valstr,CRU_MAX_VAL_LEN);
#endif
            CR_DBG("Got checkpoint request %s\n",valstr);

            sprintf(cr_file,"%s.%d.%d",valstr,checkpoint_count,MPICR_pg_rank);
            CR_Set_state(MPICR_STATE_REQUESTED);

	    /*
	     * Let the shared memory collectives know that a checkpoint
	     * has been requested
	     */
	    if (enable_shmem_collectives)
		MPIDI_CH3I_SMC_lock();

            CR_DBG("locking CR\n");
            pthread_rwlock_wrlock(&MPICR_cs_lock);

            CR_DBG("locking CM\n");

            /* Lock will be finalized in suspension. */
            MPICM_lock();

            CR_DBG("locked\n");

            CR_Set_state(MPICR_STATE_PRE_COORDINATION);

            if (CR_IBU_Suspend_channels())
            {
                CR_MPDU_Ckpt_fail();
                CR_ERR_ABORT("CR_IBU_Suspend_channels failed\n");
            }

			if(MPICR_pg_rank==0) printf("[%d]: begin checkpoint...\n", MPICR_pg_rank);

            CR_Set_state(MPICR_STATE_CHECKPOINTING);
            cr_file_fd = open(cr_file, O_CREAT | O_WRONLY | O_TRUNC , 0666);

            if (cr_file_fd < 0)
            {
                CR_MPDU_Ckpt_fail();
                CR_ERR_ABORT("checkpoint file creation failed\n");
            }

            MPICR_callback_fin = 0;
            cr_initialize_checkpoint_args_t(&cr_args);
            cr_args.cr_scope   = CR_SCOPE_PROC;
            cr_args.cr_target  = getpid();
            cr_args.cr_fd      = cr_file_fd;
            cr_args.cr_signal  = 0;
            cr_args.cr_timeout = 0;
            cr_args.cr_flags   &= ~CR_CHKPT_DUMP_ALL; // Save None
            cr_request_checkpoint(&cr_args, &cr_handle);
            cr_poll_checkpoint(&cr_handle, NULL);

            CR_DBG("cr_request_fd\n");

            while (MPICR_callback_fin == 0);

            /*
            CR_DBG("checkpointing, wait for callback to finish\n");
            pthread_mutex_lock(&MPICR_cond_callback_lock);
            pthread_cond_wait(&MPICR_cond_callback, &MPICR_cond_callback_lock);
            pthread_mutex_unlock(&MPICR_cond_callback_lock);
            */

            if (getenv("MV2_CKPT_NO_SYNC") == NULL)
            {
				if(MPICR_pg_rank==0) printf("[%d]: fsync...\n", MPICR_pg_rank);
                CR_DBG("fsync\n");
                fsync(cr_file_fd);
                CR_DBG("fsync done\n");
            }

            CR_Set_state(MPICR_STATE_POST_COORDINATION);

			if(MPICR_pg_rank==0) printf("[%d]: Reactivate channels...\n", MPICR_pg_rank);

            if (CR_IBU_Reactivate_channels())
            {
                if (MPICR_is_restarting)
                {
                    CR_MPDU_Rsrt_fail();
                }
                else
                {
                    CR_MPDU_Ckpt_fail();
                }

                CR_ERR_ABORT("CR_IBU_Reactivate_channels failed\n");
            }

            //CR_Set_state(MPICR_STATE_RUNNING);

            if (MPICR_is_restarting)
            {
                CR_MPDU_Rsrt_succeed();
            }
            else
            {
                CR_MPDU_Ckpt_succeed();

                if (MPICR_max_save_ckpts > 0 && MPICR_max_save_ckpts < checkpoint_count)
                {
                    /*remove the ealier checkpoints*/
                    sprintf(cr_file,"%s.%d.%d", valstr, checkpoint_count - MPICR_max_save_ckpts, MPICR_pg_rank);
                    unlink(cr_file);
                }
            }

            CR_DBG("unlocking CR\n");
            pthread_rwlock_unlock(&MPICR_cs_lock);

	    /*
	     * Let the shared memory collectives know that the checkpoint
	     * request has completed
	     */
		    if (enable_shmem_collectives)
				MPIDI_CH3I_SMC_unlock();

            CR_Set_state(MPICR_STATE_RUNNING);

            if (MPIDI_Process.use_sync_ckpt)
            {
				pthread_mutex_lock(&MVAPICH2_sync_ckpt_lock);
                pthread_cond_signal(&MVAPICH2_sync_ckpt_cond);
				pthread_mutex_unlock(&MVAPICH2_sync_ckpt_lock);
            }

            MPICR_is_restarting = 0;
			if( MPICR_pg_rank==0 || MPICR_pg_rank == MPICR_pg_size-1 )
				printf("[%d]:  CR completed...\n", MPICR_pg_rank );

        }
        else
        {
            CR_ERR_ABORT("Unknown command\n");
        }
    }
}

int CR_Reset_proc_info()
{
    int has_parent;
    int pg_id_sz;
    int kvs_name_sz;    
    MPIU_Free(MPIDI_Process.my_pg->id);
    MPIU_Free(MPIDI_Process.my_pg->ch.kvs_name);
    unsetenv("PMI_FD");

    CR_DBG("unset PMI_FD\n");

    if (PMI_Init(&has_parent))
    {
        CR_ERR_ABORT("PMI_Init failed\n");
    }

    CR_DBG("PMI_Init\n");

    if (PMI_Get_id_length_max(&pg_id_sz))
    {
        CR_ERR_ABORT("PMI_Get_id_length_max failed\n");
    }

    CR_DBG("PMI_Get_id_length_max\n");
    MPIDI_Process.my_pg->id = MPIU_Malloc(pg_id_sz + 1);

    if (NULL == MPIDI_Process.my_pg->id)
    {
        CR_ERR_ABORT("MPIU_Malloc failed\n");
    }

    if (PMI_Get_id(MPIDI_Process.my_pg->id,pg_id_sz))
    {
        CR_ERR_ABORT("PMI_Get_id failed\n");
    }

    if (PMI_KVS_Get_name_length_max(&kvs_name_sz))
    {
        CR_ERR_ABORT("PMI_KVS_Get_name_length_max failed\n");
    }

    MPIDI_Process.my_pg->ch.kvs_name = MPIU_Malloc(kvs_name_sz + 1);
 
    if (NULL == MPIDI_Process.my_pg->id)
    {
        CR_ERR_ABORT("MPIU_Malloc failed\n");
    }

    if (PMI_KVS_Get_my_name(MPIDI_Process.my_pg->ch.kvs_name,kvs_name_sz))
    {
        CR_ERR_ABORT("PMI_KVS_Get_my_name failed\n");
    }

    MPIDI_PG_InitConnKVS(MPIDI_Process.my_pg);
    return 0;    
}

static int CR_Callback (void* arg)
{
    CR_DBG("In CR_Callback\n");

#ifdef CR_FTB
    CR_FTB_Finalize();
#endif

    int rc = cr_checkpoint(0);

#ifdef CR_FTB
        if (CR_FTB_Init(MPICR_pg_rank, getenv("MV2_CKPT_SESSIONID")) != 0)
            fprintf(stdout, "CR_FTB_Init() Failed\n");
#endif

    if (rc < 0)
    {
        CR_MPDU_Ckpt_fail();
        CR_ERR_ABORT("cr_checkpoint failed\n");
    }
    else if (rc)
    {
        /*Build the pipe between mpdman and app procs*/
        CR_Set_state(MPICR_STATE_RESTARTING);
        ++restart_count;

#ifndef CR_FTB
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
#endif /* !CR_FTB */

        if (CR_Reset_proc_info())
        {
            CR_MPDU_Rsrt_fail();
            CR_ERR_ABORT("CR_Reset_proc_info failed\n");
        }

        CR_DBG("proc info reset\n");
    }

    CR_DBG("Out CR_Callback\n");
    MPICR_callback_fin = 1;

    /* pthread_cond_signal(&MPICR_cond_callback); */
    return 0;
}

void* CR_Thread_entry(void* arg)
{
    checkpoint_count = restart_count = 0;
    MPICR_cli_id = cr_init();

    if (MPICR_cli_id < 0)
    {
        CR_ERR_ABORT("cr_init failed\n");
    }

    MPICR_callback_id = cr_register_callback(CR_Callback,NULL,CR_THREAD_CONTEXT);
    MPICR_is_initialized = 1;

    char* temp = getenv("MV2_CKPT_MPD_BASE_PORT");

    if (temp)
    {
        MPICR_MPD_port = atoi(temp);
    }
    else
    {
        CR_ERR_ABORT("MV2_CKPT_MPD_BASE_PORT is not set\n");
    }
        
    temp = getenv("MV2_CKPT_MAX_SAVE_CKPTS");

    if (temp)
    {
        MPICR_max_save_ckpts = atoi(temp);
        CR_DBG("MV2_CKPT_MAX_SAVE_CKPTS  %s\n",temp);
    }
    else
    {
        MPICR_max_save_ckpts = 0;
    }

#ifdef CR_FTB
    if (CR_FTB_Init(MPICR_pg_rank, getenv("MV2_CKPT_SESSIONID")) != 0)
        fprintf(stdout, "CR_FTB_Init() Failed\n");
#else
    CR_DBG("Connecting to MPD\n");

    if (CR_MPDU_connect_MPD())
    {
        CR_ERR_ABORT("CR_MPDU_connect_MPD failed\n");
    }
#endif /* CR_FTB */

    CR_Set_state(MPICR_STATE_RUNNING);
    CR_DBG("Finish initialization, going to CR_Thread_loop\n");

    CR_Thread_loop();

    return NULL;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CR_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CR_Init(MPIDI_PG_t* pg, int rank, int size)
{
    MPICR_pg = pg;
    MPICR_pg_rank = rank;
    MPICR_pg_size = size;
    int mpi_errno = MPI_SUCCESS;

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);

    if (pthread_rwlock_init(&MPICR_cs_lock, &attr))
    {
        MPIU_ERR_SETFATALANDJUMP2(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "%s: %s",
            "pthread_mutex_init",
            strerror(errno)
        );    
    }

    pthread_rwlockattr_destroy(&attr);
    
    CR_DBG("Creating a new thread for running cr controller\n");

    if (pthread_create(&MPICR_child_thread, NULL, CR_Thread_entry, NULL))
    {
	MPIU_ERR_SETFATALANDJUMP2(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "%s: %s",
            "pthread_create",
            strerror(errno)
        );
    }

    /* Initialize the shared memory collectives lock */
    MPIDI_SMC_CR_Init();

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

int MPIDI_CH3I_CR_Finalize()
{
    if (!MPICR_is_initialized)
    {
        return 0;
    }

#ifdef CR_FTB
    cr_ftb_ckpt_finalize = 1;
    pthread_cond_signal(&cr_ftb_cond);
#endif

    pthread_cancel(MPICR_child_thread);
    pthread_join(MPICR_child_thread, NULL);
    pthread_rwlock_destroy(&MPICR_cs_lock);

#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;

    if (ftb_init_done) {
        ftb_init_done = 0;

        SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
        ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_CKPT_FINALIZE),
                          &eprop, &ehandle);
        if (ret != FTB_SUCCESS)
            fprintf(stdout, "MPIDI_CH3I_CR_Finalize() failed\n", ret);

        FTB_Disconnect(ftb_handle);
    }
#else
    if (!MPICR_pg_rank)
    {
        char cr_msg_buf[MAX_CR_MSG_LEN];

        sprintf(cr_msg_buf, "cmd=finalize_ckpt\n");
        CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
    }

    if (MPICR_MPD_fd > 0)
    {
        close(MPICR_MPD_fd);
    }
#endif

    MPICR_is_initialized = 0;

    /* Uninitialize the shared memory collectives lock */
    MPIDI_SMC_CR_Finalize();

    return MPI_SUCCESS;
}

void MPIDI_CH3I_CR_Sync_ckpt_request()
{
    if (MPICR_state != MPICR_STATE_RUNNING)
    {
        /*Another checkpoint is going on */
        return;
    }

#ifdef CR_FTB
    int ret;
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_APP_CKPT_REQ), &eprop, &ehandle);
    if (ret != FTB_SUCCESS)
        fprintf(stdout, "MPIDI_CH3I_CR_Sync_ckpt_request() failed\n");
#else
    CR_DBG("Send ckpt request to MPD\n");

    char cr_msg_buf[MAX_CR_MSG_LEN];
    sprintf(cr_msg_buf, "cmd=app_ckpt_req\n");
    CR_MPDU_writeline(MPICR_MPD_fd, cr_msg_buf);
#endif
}

/* CR message handler in progress engine */
void MPIDI_CH3I_CR_Handle_recv(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf* v)
{
    MPICR_remote_update_msg* msg = NULL;
    struct MPID_Request* sreq = NULL;
    int found = 0;
	CR_DBG("%s: [%d <= %d]: got msg: %s(%d)\n", __func__, MPIDI_Process.my_pg_rank, 
			vc->pg_rank, MPIDI_CH3_Pkt_type_to_string[msg_type], msg_type  );
    switch (msg_type)
    {
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
            CR_DBG("Received MPIDI_CH3_PKT_CR_REMOTE_UPDATE\n");

            msg = (MPICR_remote_update_msg*)(v->buffer + sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header));
            sreq = (struct MPID_Request*) vc->mrail.sreq_head;

            CR_DBG("Looking for address match %p\n", msg->recv_mem_addr);
	    	found = 0;

            while (sreq)
            {
                CR_DBG("Verifying request %p\n", sreq);

                if (sreq->mrail.remote_addr == msg->recv_mem_addr)
                {
                    CR_DBG("Found a address match req: %p\n", sreq);

                    /*FIXME: Is address match enough? */
                    MPIU_Memcpy(sreq->mrail.rkey, msg->recv_buf_rkey, sizeof(uint32_t) * MAX_NUM_HCAS);
                    found++;
                    if( vc->mrail.sreq_to_update <= 0 ){
                         CR_DBG("[%d <== %d]: %s: Warn: REM_UPDATE: sreq-to-up=%d...\n", 
                             MPICR_pg_rank, vc->pg_rank, __func__,vc->mrail.sreq_to_update );
                         //MPIU_Assert( vc->mrail.sreq_to_update > 0 );
                    }
                    else //( vc->mrail.sreq_to_update > 0 )
                    	vc->mrail.sreq_to_update--; // has updated one sender' rndv(sreq)
					if( vc->mrail.sreq_to_update == 0 ){
                           //vc->ch.rput_stop = 0; // all pending sender rndv have been updated
                    }
                    
	            	CR_DBG("rkey updated hca0:%x\n", sreq->mrail.rkey[0]);
                }

                sreq = sreq->mrail.next_inflow;
            }
            if( !found ){
                 CR_DBG("[%d <== %d]: %s: Warn: REM_UPDATE: no match found...\n", 
                 	MPICR_pg_rank,  vc->pg_rank, __func__  );
			}
#if defined(MPIDI_MRAILI_COALESCE_ENABLED)
            v->content_consumed += sizeof(MPICR_remote_update_msg);
#endif /* defined(MPIDI_MRAILI_COALESCE_ENABLED) */
        break;
    default:
            CR_ERR_ABORT("unknown message type: %d\n", msg_type);
        break;
    }
}

void MPIDI_CH3I_CR_Handle_send_completion(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_type_t msg_type, vbuf* v)
{
    switch (msg_type)
    {
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
        break;
    default:
            CR_ERR_ABORT("unknown message type: %d\n",msg_type);
        break;
    }
}


/*===========================*/
/*  IB management functions  */
/*===========================*/

/* The request involving memory registration (e.g. rndv recv). */
struct MPID_Request* MPICR_req_list_head = NULL;
struct MPID_Request* MPICR_req_list_tail = NULL;

int CR_IBU_Release_network()
{
    int i = 0;
    int rail_index;

    MPIDI_PG_t* pg = MPIDI_Process.my_pg;
    MPIDI_VC_t* vc = NULL;

    int pg_rank = MPIDI_Process.my_pg_rank;
    int pg_size = MPIDI_PG_Get_size(pg);

    if (MPICM_Finalize_UD() != 0)
    {
        CR_ERR_ABORT("MPICM_Finalize_UD failed\n");
    }

    for (; i < pg_size; ++i)
    {
        if (i == pg_rank)
        {
            continue;
        }

        MPIDI_PG_Get_vc(pg, i, &vc);

        switch (vc->ch.state)
        {
        case MPIDI_CH3I_VC_STATE_SUSPENDED:
            break;
        case MPIDI_CH3I_VC_STATE_IDLE:
            CR_ERR_ABORT("Having active vc when releasing networks\n");
        default:
            continue;
        }

	/*
	 * Don't try to destroy the QP when SMP is used.
	 */
	if (!(SMP_INIT && (vc->smp.local_nodes >= 0))) {
	    for (rail_index = 0; rail_index < vc->mrail.num_rails; ++rail_index)
	    {
		ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
	    }
	}

#if defined(USE_HEADER_CACHING)
	if (vc->mrail.rfp.cached_incoming)
	    MPIU_Free(vc->mrail.rfp.cached_incoming);
	if (vc->mrail.rfp.cached_outgoing)
	    MPIU_Free(vc->mrail.rfp.cached_outgoing);
#endif /* defined(USE_HEADER_CACHING) */
    }

    if (MPIDI_CH3I_RDMA_Process.has_srq)
    {
        for (i = 0; i < rdma_num_hcas; ++i)
        {
            pthread_cond_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_cond[i]);
            pthread_mutex_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i]);
            pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
            pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i], NULL);

            if (ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]))
            {
                ibv_error_abort(IBV_RETURN_ERR, "Couldn't destroy SRQ\n");
            }
        }
    }

    dreg_deregister_all();
    
    for (i = 0; i < rdma_num_hcas; ++i)
    {
        if (rdma_iwarp_use_multiple_cq &&
            (MPIDI_CH3I_RDMA_Process.hca_type == CHELSIO_T3) &&
            (MPIDI_CH3I_RDMA_Process.cluster_size != VERY_SMALL_CLUSTER)) {
            /* Trac #423 */
            ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]);
            ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]);
        } else {
            ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
        }
        deallocate_vbufs(i);
        ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
    }
    
#if !defined(DISABLE_PTMALLOC)
    mvapich2_mfin();
#endif /* !defined(DISABLE_PTMALLOC) */

    /* Release the SMP channel */
    if (SMP_INIT) {
	if (MPIDI_CH3I_SMP_finalize()) {
	    fprintf(stderr, "[%s:%d] MPIDI_CH3I_SMP_finalize() returned error\n",
				__FILE__, __LINE__);
	    return(-1);
	}
    }

    return 0;
}

/*
 * TODO finish adding error handling
 */
#undef FUNCNAME
#define FUNCNAME CR_IBU_Rebuild_network
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int CR_IBU_Rebuild_network()
{
    int mpi_errno = MPI_SUCCESS;

#if !defined(DISABLE_PTMALLOC)
    if (mvapich2_minit())
    {
	MPIU_ERR_SETFATALANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**fail",
            "**fail %s",
            "Error initializing MVAPICH2 malloc library");
    }
#else /* !defined(DISABLE_PTMALLOC) */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif /* !defined(DISABLE_PTMALLOC) */

    char tmp_hname[256];
    gethostname(tmp_hname, 255);
    MPIDI_PG_t* pg = MPICR_pg;
    int pg_rank = MPICR_pg_rank;
    int pg_size = MPICR_pg_size;

    uint32_t* ud_qpn_all = (uint32_t*) MPIU_Malloc(pg_size * sizeof(uint32_t));
    uint16_t* lid_all = (uint16_t*) MPIU_Malloc(pg_size * sizeof(uint16_t));
    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;

    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d\n", rdma_num_qp_per_port, rdma_num_rails);

    init_vbuf_lock();

    MPIDI_VC_t* vc = NULL;
    int i = 0;

    /*vc structure doesn't need to be initialized */
    for (; i < pg_size; ++i)
    {
        MPIDI_PG_Get_vc(pg, i, &vc);
        vc->mrail.num_rails = rdma_num_rails;
    }
    
    /* Open the device and create cq and qp's */
    if (rdma_open_hca(&MPIDI_CH3I_RDMA_Process))
    {
        MPIU_Error_printf(stderr, "rdma_open_hca failed\n");
        return -1;
    }

    mpi_errno = rdma_iba_hca_init_noqp(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
    if(mpi_errno) {
        MPIU_Error_printf("Failed to Initialize HCA type\n");
        MPIU_ERR_POP(mpi_errno);
    }

    dreg_reregister_all();
    vbuf_reregister_all();

    /* Post the buffers for the SRQ */
    if (MPIDI_CH3I_RDMA_Process.has_srq)
    {    
        pthread_spin_init(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock, 0);
        pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

        struct ibv_srq_attr srq_attr;        

        for (i = 0; i < rdma_num_hcas; ++i)
        { 
            pthread_mutex_init(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i], 0);
            pthread_cond_init(&MPIDI_CH3I_RDMA_Process.srq_post_cond[i], 0);

            MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[i] = 0;
            MPIDI_CH3I_RDMA_Process.posted_bufs[i] = viadev_post_srq_buffers(viadev_srq_size, i);
        
            srq_attr.max_wr = viadev_srq_size;
            srq_attr.max_sge = 1;
            srq_attr.srq_limit = viadev_srq_limit;

            if (ibv_modify_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i], &srq_attr, IBV_SRQ_LIMIT))
            {
                ibv_error_abort(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
            }

            /* Start the async thread which watches for SRQ limit events. */
            pthread_create(
                &MPIDI_CH3I_RDMA_Process.async_thread[i],
                NULL,
                (void*) async_thread,
                (void*) MPIDI_CH3I_RDMA_Process.nic_context[i]
            );
        }

        pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);
    }

    uint32_t ud_qpn_self;    
    mpi_errno = MPICM_Init_UD(&ud_qpn_self);

    if (mpi_errno)
    {
        MPIU_ERR_POP(mpi_errno);
    }
    
    if (pg_size > 1)
    {
        if (MPIDI_CH3I_RDMA_Process.has_ring_startup)
        {
            ud_addr_info_t self_info;
            CR_DBG("Ring-based exchange\n");

            char hostname[HOSTNAME_LEN + 1];

	    /* Recalculate your hostid on restart */
	    gethostname(hostname, HOSTNAME_LEN);

	    if (!hostname) {
		MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			"**fail", "**fail %s", "Could not get hostname");
	    }

	    struct hostent* hostent = gethostbyname(hostname);
	    int hostid = (int) ((struct in_addr *)
	    hostent->h_addr_list[0])->s_addr;
	    self_info.hostid = hostid;

            self_info.lid = MPIDI_CH3I_RDMA_Process.lids[0][0];
            self_info.qpn = ud_qpn_self;

            ud_addr_info_t * all_info = (ud_addr_info_t *) MPIU_Malloc(sizeof(ud_addr_info_t)*pg_size);
            /*will be freed in rdma_iba_bootstrap_cleanup*/
            rdma_setup_startup_ring(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);

            mpi_errno = rdma_ring_based_allgather(&self_info, sizeof(self_info), pg_rank, all_info, pg_size, &MPIDI_CH3I_RDMA_Process);
            if(mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
            for (i = 0; i < pg_size; ++i)
            {
                /* Use the recalculated hostids on restart */
                MPIDI_VC_t *vc;
                MPIDI_PG_Get_vc(pg, i, &vc);
                vc->smp.hostid = all_info[i].hostid;

                ud_qpn_all[i] = all_info[i].qpn;
                lid_all[i] = all_info[i].lid;
            }

            mpi_errno = rdma_cleanup_startup_ring(&MPIDI_CH3I_RDMA_Process);

	    if (mpi_errno)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }
        else
        {
            /* Exchange the information about HCA_lid and qp_num. */
            /* Allocate space for pmi keys and values. */

            CR_DBG("Normal PMI exchange\n");
            int key_max_sz;
        
            if (PMI_KVS_Get_key_length_max(&key_max_sz) != 0)
            {
                CR_ERR_ABORT("PMI_KVS_Get_key_length_max failed\n");
            }

            char* key = MPIU_Malloc(++key_max_sz);
        
            if (key == NULL)
            {
                CR_ERR_ABORT("MPIU_Malloc failed\n");
            }

            int val_max_sz;

            if (PMI_KVS_Get_value_length_max(&val_max_sz) != 0)
            {
                CR_ERR_ABORT("PMI_KVS_Get_value_length_max failed\n");
            }

            char* val = MPIU_Malloc(++val_max_sz);

            if (val == NULL)
            {
                CR_ERR_ABORT("MPIU_Malloc failed\n");
            }

            if (key_max_sz < 20 || val_max_sz < 20)
            {
                CR_ERR_ABORT("key_max_sz val_max_sz too small\n");
            }

            sprintf(key, "ud_info_%08d", pg_rank);
            sprintf(val, "%08x:%08x", MPIDI_CH3I_RDMA_Process.lids[0][0], ud_qpn_self);

            if (PMI_KVS_Put(pg->ch.kvs_name, key, val) != 0)
            {
                CR_ERR_ABORT("PMI_KVS_Put failed\n");
            }

            if (PMI_KVS_Commit(pg->ch.kvs_name) != 0)
            {
                CR_ERR_ABORT("PMI_KVS_Commit failed\n");
            }

            if (PMI_Barrier() != 0)
            {
                CR_ERR_ABORT("PMI_Barrier failed\n");
            }

            for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                {
                    lid_all[i] = MPIDI_CH3I_RDMA_Process.lids[0][0];
                    ud_qpn_all[i] = ud_qpn_self;
                    continue;
                }

                sprintf(key, "ud_info_%08d", i);

                if (PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz) != 0)
                {
                    CR_ERR_ABORT("PMI_KVS_Get failed\n");
                }

                sscanf(val, "%08x:%08x", &(lid_all[i]), &(ud_qpn_all[i]));
            }
        }
    }

    CR_DBG("Exchanging parameters done\n");

    mpi_errno = MPICM_Init_UD_struct(MPICR_pg, ud_qpn_all, lid_all); 
    MPICM_Create_UD_threads();

    if (mpi_errno)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    if (PMI_Barrier() != 0)
    {
        CR_ERR_ABORT("PMI_Barrier failed\n");
    }

    CR_DBG("CR_IBU_Rebuild_network finish\n");

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

int CR_IBU_Prep_remote_update()
{
    struct MPID_Request* temp = MPICR_req_list_head;

    /*Using this to reduce the number of update messages 
    since all consecutive request to a same memory address 
    will be combined to a single update message*/
    void* last_mem_addr = NULL;

    MPIDI_VC_t* vc = NULL;
    vbuf* v = NULL;
    MPICR_remote_update_msg msg;
    MPIDI_CH3I_CR_msg_log_queue_entry_t* entry = NULL;
    MPIDI_CH3I_MRAILI_Pkt_comm_header* p = NULL;
    struct MPIDI_CH3_Pkt_rndv_clr_to_send* cts_header = NULL;

    /*each req->mrail.rkey is:*/
    uint32_t rkey[MAX_NUM_HCAS];

    int i;

    while (temp != NULL)
    {
        if (temp->mrail.rndv_buf == last_mem_addr)
        {
            if (temp->mrail.d_entry->memhandle[0]->rkey != rkey[0])
            {
                CR_ERR_ABORT(
                    "Same addr %p, different rkey %x, %x\n",
                    temp->mrail.rndv_buf,
                    temp->mrail.d_entry->memhandle[0]->rkey,
                    rkey[0]
                );
            }

            temp = temp->ch.cr_queue_next;
            continue;
        }

        CR_DBG("Found a new memory address %p registered\n", temp->mrail.rndv_buf);
        last_mem_addr = temp->mrail.rndv_buf;

        memset(rkey, 0, sizeof(uint32_t) * MAX_NUM_HCAS);
        vc = temp->ch.vc;

        for (i = 0; i < rdma_num_hcas; ++i)
        {
            rkey[i] = temp->mrail.d_entry->memhandle[i]->rkey;
#if !defined(NDEBUG)
            MPIU_Assert(vc->mrail.srp.credits[i].backlog.len == 0);
            MPIU_Assert(vc->mrail.rails[i].ext_sendq_head == NULL);
#endif /* !defined(NDEBUG) */ 
        }
        
        i = 0;
        CR_DBG("Search msg log queue for CTS message with the address %p, in vc %p \n", temp->mrail.rndv_buf, vc);
        entry = vc->mrail.msg_log_queue_head;

        while (NULL != entry)
        {
            v = entry->buf;
            ++i;
            p = (MPIDI_CH3I_MRAILI_Pkt_comm_header *) v->pheader;
            CR_DBG("In msg_log_Q, type %d\n",p->type);

            if (p->type == MPIDI_CH3_PKT_RNDV_CLR_TO_SEND)
            {
                cts_header = (struct MPIDI_CH3_Pkt_rndv_clr_to_send*) p;
                CR_DBG("CTS in msg_log_Q addr %p\n",cts_header->rndv.buf_addr);

                if (cts_header->rndv.buf_addr == temp->mrail.rndv_buf)
                {
                    CR_DBG("Found a match in local pending cts\n");
                    MPIU_Memcpy(cts_header->rndv.rkey, rkey, sizeof(uint32_t) * MAX_NUM_HCAS);
                }
            }

            entry = entry->next;
        }

        CR_DBG("Searched %d packets in msg_log_Q\n",i);
        CR_DBG("Checking CM queue for vc %p\n",vc);

        /* Check cm_sendq for all cts packets. */
        if (!MPIDI_CH3I_CM_SendQ_empty(vc))
        {
            CR_DBG("cm_send_Q is not empty\n");
            struct MPID_Request* req = MPIDI_CH3I_CM_SendQ_head(vc);
            CR_DBG("req %p\n", req);

            i = 0;

            while (req != NULL)
            {
                MPIDI_CH3_Pkt_t *upkt = &(req->dev.pending_pkt);
                ++i;
                CR_DBG("In cm_send_Q, type %d\n",upkt->type);

                if (upkt->type == MPIDI_CH3_PKT_RNDV_CLR_TO_SEND)
                {
                    cts_header = &(upkt->rndv_clr_to_send);
                    CR_DBG("CTS in cm_send_Q addr %p\n", cts_header->rndv.buf_addr);

                    if (cts_header->rndv.buf_addr == temp->mrail.rndv_buf)
                    {
                        CR_DBG("Found a match in local cm queue\n");
                        MPIU_Memcpy(cts_header->rndv.rkey, rkey, sizeof(uint32_t) * MAX_NUM_HCAS);
                    }
                }

                req = req->dev.next;
            }
        }

        CR_DBG("Searched %d packets in cm_send_Q\n",i);

        /* Prepare remote update packet. */
        msg.recv_mem_addr = temp->mrail.rndv_buf;
        MPIU_Memcpy(msg.recv_buf_rkey, rkey, sizeof(uint32_t) * MAX_NUM_HCAS);
        
        /*FIXME: use recv_mem_addr as only identifier*/ 
        CR_DBG("recv_mem_addr %p, rkey0 %x\n", msg.recv_mem_addr, msg.recv_buf_rkey[0]);

        v = get_vbuf();
        p = (MPIDI_CH3I_MRAILI_Pkt_comm_header*) v->pheader;
        p->type = MPIDI_CH3_PKT_CR_REMOTE_UPDATE;
        MPIU_Memcpy(v->buffer + sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header), &msg, sizeof(msg));

        /* Push update packet to message log queue. */
        entry = (MPIDI_CH3I_CR_msg_log_queue_entry_t*) MPIU_Malloc(sizeof(MPIDI_CH3I_CR_msg_log_queue_entry_t));
        entry->len = sizeof(MPIDI_CH3I_MRAILI_Pkt_comm_header) + sizeof(msg);
        entry->buf = v;
        MSG_LOG_ENQUEUE(vc, entry);
		CR_DBG("%s: [%d => %d]: enq REM_UPDATE...\n",__func__, MPICR_pg_rank, vc->pg_rank );

        temp = temp->ch.cr_queue_next;
    }

    return 0;
}


/**
Before ckpt, record the num of active rndv for each vc(as sender), so that
we know how many rndvs need to be updated with their rkey during restart
**/
void CR_record_rndv(MPIDI_VC_t** vc_vector)
{
    struct MPID_Request* sreq = NULL;
    MPIDI_VC_t *vc=NULL;

    int i,n;

    for(i=0; i<MPICR_pg_size; i++)
    {
        if( i== MPICR_pg_rank ) continue;

        vc = vc_vector[i];
        if( vc==NULL){
            continue;
        }

        n = 0;
        sreq = (struct MPID_Request*)vc->mrail.sreq_head;
        while (sreq) // record: num of rndvs as sender
        {
            n++;
            sreq = sreq->mrail.next_inflow;
        }
        // this many rndv need to be updated with new rkey at restart
        vc->mrail.sreq_to_update = n;
        if( n>0 || vc->ch.sendq_head || vc->ch.cm_sendq_head || vc->mrail.msg_log_queue_head ){
    	    CR_DBG("%s: [%d vc_%d]: has  %d  rndv-send to update, ch.sendq_head = %p, ch.cm_sendq_head=%p, vc.mrail.msg_log_q_head=%p\n",
                        __func__, MPICR_pg_rank, vc->pg_rank, n,
                       vc->ch.sendq_head, vc->ch.cm_sendq_head, vc->mrail.msg_log_queue_head );
		}

    }// end of for( each vc )
    
}

void CR_record_flowlist(char* title)
{
       extern MPIDI_VC_t *flowlist;

       MPIDI_VC_t *vc = flowlist;

       int n=0;
       while( vc ){
               n++;
               MPIU_Assert(vc->mrail.inflow==1);
               //CR_DBG("[%d -> %d]: flowlist_%d: ...\n", MPICR_pg_rank, vc->pg_rank, n );
               vc = vc->mrail.nextflow;
       }

       // cnt pending CTS at recv-side
       struct MPID_Request* temp = MPICR_req_list_head;
       int cts=0;
       while( temp ){
               cts++;
               temp = temp->ch.cr_queue_next;
       }
       if( n>0 || cts>0 )      
       CR_DBG("[%d]: %s:  flowlist = %d, CTS = %d\n", MPICR_pg_rank, title, n, cts );

}
   



int CR_IBU_Suspend_channels()
{
    int i = 0;
    MPIDI_VC_t** vc_vector = (MPIDI_VC_t**) MPIU_Malloc(sizeof(MPIDI_VC_t*) * MPICR_pg_size);
    MPIDI_VC_t* vc = NULL;

    MPIU_Assert( vc_vector );
    for (; i < MPICR_pg_size; ++i)
    {
        if ( i == MPICR_pg_rank)
        {
            continue;
        }

        MPIDI_PG_Get_vc(MPICR_pg, i, &vc);

#if !defined(NDEBUG)
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_SRV);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_SRV);
#endif /* !defined(NDEBUG) */
        ///////  init the fields to be used at resume
        if( vc ){
              pthread_spin_init( &vc->mrail.cr_lock, 0 );
              vc->mrail.react_send_ready = 0;
              vc->mrail.react_entry = NULL;
        }

        if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
            ||  vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING)
        {
            vc_vector[i] = vc;
        }
        else
        {
            vc_vector[i] = NULL;
        }
    }
    
    int retval = 0;

    if ((retval = MPIDI_CH3I_CM_Suspend(vc_vector)))
    {
        return retval;
    }

    if ((retval = CR_IBU_Release_network()))
    {
        return retval;
    }
    /// record num of rndvs(sender) to update at restart
    CR_record_rndv( vc_vector );
    //CR_record_flowlist("after susp-chan");
    //MPIU_Free(vc_vector);

    return retval;
}

int CR_IBU_Reactivate_channels()
{
    int retval = 0;
    CR_DBG("CR_IBU_Rebuild_network\n");
    
    if ((retval = CR_IBU_Rebuild_network()))
    {
        return retval;
    }
    
    /* Reinitialize the SMP channel */
    CR_DBG("MPIDI_CH3I_SMP_init()\n");
    retval = MPIDI_CH3I_SMP_init(MPICR_pg);
    if (retval) {
        fprintf(stderr, "[%s:%d] MPIDI_CH3I_SMP_init() returned %d\n",
			__FILE__, __LINE__, retval);
        return(retval);
    }

    CR_DBG("CR_IBU_Prep_remote_update\n");

    if ((retval = CR_IBU_Prep_remote_update()))
    {
        return retval;
    }

    MPIDI_VC_t** vc_vector = (MPIDI_VC_t**) MPIU_Malloc(sizeof(MPIDI_VC_t*) * MPICR_pg_size);
    MPIDI_VC_t* vc = NULL;
    int i = 0;

    for (; i < MPICR_pg_size; ++i)
    {
        if (i == MPICR_pg_rank)
        {
            continue;
        }

        MPIDI_PG_Get_vc(MPICR_pg, i, &vc);

//#if !defined(NDEBUG)
        /* Now all calling can only be small rank reactivate to big rank. */
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_CLI);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_CONNECTING_SRV);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_SUSPENDING);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1);
        MPIU_Assert(vc->ch.state != MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2);
//#endif /* !defined(NDEBUG) */

        if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED
            || vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV)
        {
            vc_vector[i] = vc;
			///////////// make sure: REACT_DONE are behind REM_UPDATE(if any)
            pthread_spin_lock( &vc->mrail.cr_lock);
            vc->mrail.react_send_ready = 1;
            if( vc->mrail.react_entry ) // cm_thread has gotten REACT_DONE from peer:
            { //  enquue the REACT_DONE to be sent to peer
              //CR_DBG("%s: [%d => %d]: REACT_DONE came earlier, enq now...\n",
             //      __func__, MPICR_pg_rank, vc->pg_rank );
                 MSG_LOG_ENQUEUE(vc, vc->mrail.react_entry);                     
            }
            pthread_spin_unlock( &vc->mrail.cr_lock);
        }
        else
        {
            vc_vector[i] = NULL;
        }
    }
    
    CR_DBG("MPIDI_CH3I_CM_Reactivate\n");

    if ((retval = MPIDI_CH3I_CM_Reactivate(vc_vector)))
    {
        return retval;
    }

    return retval;
}

void MPIDI_CH3I_CR_req_enqueue(struct MPID_Request* req, MPIDI_VC_t* vc)
{
    if (req == NULL)
    {
        return;
    }

    req->ch.cr_queue_next = NULL;
    req->ch.vc = vc;

    if (MPICR_req_list_head == NULL)
    {
        MPICR_req_list_tail = MPICR_req_list_head = req;
    }
    else
    {
        MPICR_req_list_tail->ch.cr_queue_next = req;
        MPICR_req_list_tail = req;
    }
}

void MPIDI_CH3I_CR_req_dequeue(struct MPID_Request* req)
{
    if (req == NULL)
    {
        return;
    }

    if (MPICR_req_list_head == NULL)
    {
        return;
    }

    if (MPICR_req_list_head == req)
    {
        MPICR_req_list_head = req->ch.cr_queue_next;

        if (MPICR_req_list_tail == req)
        {
            /* Last element in the list. */
            MPICR_req_list_tail = NULL;
        }

        return;
    }

    struct MPID_Request* temp = MPICR_req_list_head;

    while (temp->ch.cr_queue_next != NULL)
    {
        if (temp->ch.cr_queue_next == req)
        {
            temp->ch.cr_queue_next = req->ch.cr_queue_next;

            if (MPICR_req_list_tail == req)
            {
                /* Dequeue last element. */
                MPICR_req_list_tail = temp;
            }

            return;
        }

        temp = temp->ch.cr_queue_next;
    }
}

#ifdef CR_FTB

static int CR_FTB_Init(int rank, char *sessionid)
{
    int ret, fd;
    char *str;
    char session_file[64];

    memset(&ftb_cinfo, 0, sizeof(ftb_cinfo));
    strcpy(ftb_cinfo.client_schema_ver, "0.5");
    strcpy(ftb_cinfo.event_space, "FTB.MPI.MVAPICH2");
    snprintf(ftb_cinfo.client_name, FTB_MAX_CLIENT_NAME, "MVAPICH2.%d", rank);

    snprintf(ftb_cinfo.client_jobid, FTB_MAX_CLIENT_JOBID, "%s", sessionid);

    strcpy(ftb_cinfo.client_subscription_style, "FTB_SUBSCRIPTION_BOTH");
    ftb_cinfo.client_polling_queue_len = 4;

    ret = FTB_Connect(&ftb_cinfo, &ftb_handle);
    if (ret != FTB_SUCCESS) goto err_connect;

    ret = FTB_Declare_publishable_events(ftb_handle, NULL,
                                         cr_ftb_events, CR_FTB_EVENTS_MAX);
    if (ret != FTB_SUCCESS) goto err_declare_events;

    str = MPIU_Malloc(sizeof(char) * FTB_MAX_SUBSCRIPTION_STR);
    if (!str) goto err_malloc;

    snprintf(str, FTB_MAX_SUBSCRIPTION_STR,
             "event_space=FTB.STARTUP.MV2_MPIRUN , jobid=%s", sessionid);

    ret = FTB_Subscribe(&shandle, ftb_handle, str, CR_FTB_Callback, NULL);
    if (ret != FTB_SUCCESS) goto err_subscribe;

    /* Set PMI_PORT */
    snprintf(session_file, 64, "/tmp/cr.session.%s", getenv("MV2_CKPT_SESSIONID"));
    fd = open(session_file, O_RDWR);
    if (fd < 0) goto err_open;

    ret = read(fd, str, sizeof(char)*FTB_MAX_SUBSCRIPTION_STR);
    str[ret] = '\0';
    close(fd);
    if (ret < 0) goto err_read;

    setenv("PMI_PORT", str, 1);

    MPIU_Free(str);
    ftb_init_done = 1;
    return(0);

err_connect:
    fprintf(stdout, "FTB_Connect() failed with %d for rank %d\n", ret, rank);
    ret = -1;
    goto exit_connect;

err_declare_events:
    fprintf(stdout, "FTB_Declare_publishable_events() failed with %d"
                    " for rank %d\n", ret, rank);
    ret = -2;
    goto exit_declare_events;

err_malloc:
    fprintf(stdout, "Failed to malloc() subscription_str for rank %d\n", rank);
    ret = -3;
    goto exit_malloc;

err_subscribe:
    fprintf(stdout, "FTB_Subscribe() failed with %d for rank %d\n", ret, rank);
    ret = -4;
    goto exit_subscribe;

err_open:
    perror("open");
    fprintf(stdout, "Rank %d cannot open %s\n", rank, session_file);
    ret = -5;
    goto exit_open;

err_read:
    perror("read");
    fprintf(stdout, "Could not read the Spawn Port for rank %d\n", rank);
    ret = -6;
    goto exit_read;

exit_read:
exit_open:
exit_subscribe:
exit_malloc:
exit_declare_events:
    FTB_Disconnect(ftb_handle);

exit_connect:
    return(ret);
    
}

static void CR_FTB_Finalize()
{
    if (ftb_init_done) {
        FTB_Disconnect(ftb_handle);
        ftb_init_done = 0;
    }
}

static int CR_FTB_Callback(FTB_receive_event_t *revent, void *arg)
{
    // fprintf(stdout, "Got event %s from %s\n",
    //        revent->event_name, revent->client_name);
    /* TODO: Do some sanity checking */
    pthread_mutex_lock(&cr_ftb_mutex);
    cr_ftb_ckpt_req = 1;
    pthread_cond_signal(&cr_ftb_cond);
    pthread_mutex_unlock(&cr_ftb_mutex);
    return(0);
}

#else

/*===========================*/
/*  MPD messaging functions  */
/*===========================*/

int CR_MPDU_readline(int fd, char *buf, int maxlen)
{
    int n = 1;
    int rc;
    char c;
    char* ptr = buf;

    for (n=1; n < maxlen; ++n)
    {
again:
        rc = read(fd, &c, 1);

        if (rc == 1)
        {
            *ptr = c;
            ++ptr;

            /* Note \n is stored, like in fgets. */
            if (c == '\n')
            {
                break;
            }
        }
        else if (rc == 0)
        {
            if (n == 1)
            {
                /* EOF, no data read. */
                return 0;
            }
            else
            {
                /* EOF, some data read. */
                break;
            }
        }
        else
        {
            if (errno == EINTR)
            {
                goto again;
            }

            /* Error, errno set by read. */
            return -1;
        }
    }

    /* Null termination, like fgets. */
    *ptr = 0;
    return n;
}

int CR_MPDU_writeline (int fd, char* buf)
{
    int size = strlen(buf);

    if (size > MAX_CR_MSG_LEN)
    {
        buf[MAX_CR_MSG_LEN-1] = '\0';
        fprintf(stderr, "write_line: message string too big: :%s:\n", buf);
    }
    else if (buf[strlen( buf ) - 1] != '\n')
    {
        fprintf(stderr, "write_line: message string doesn't end in newline: :%s:\n", buf);
    }
    else
    {
        int n = write(fd, buf, size);

        if (n < 0)
        {
            fprintf(stderr, "write_line error; fd=%d buf=:%s:\n", fd, buf);
            return -1;
        }

        if (n < size)
        {
            fprintf(stderr, "write_line failed to write entire message\n");
        }
    }

    return 0;
}
    
int CR_MPDU_parse_keyvals (char* st)
{
    if (!st)
    {
        return -1;
    }

    CRU_keyval_tab_idx = 0;
    char* p = st;
    char* keystart = NULL;
    char* valstart = NULL;

    while (1)
    {
        /* Increment until a numeric digit or a letter of the alphabet is found. */
        while (p && !isalnum(*p))
        {
            ++p;
        }

        if (!p)
        {
            return 0;
        }

        keystart = p; /* remember where key started */

        while (*p != ' '
            && *p != '='
            && *p != '\n'
            && *p != '\0')
        {
            ++p;
        }

        if (*p == ' '
            || *p == '\n'
            || *p == '\0')
        {
            fprintf(stderr, "CRU_parse_keyvals: unexpected key delimiter at character %d in %s\n", (int)(p - st), st);
            return -1;
        }

        strncpy(CRU_keyval_tab[CRU_keyval_tab_idx].key, keystart, CRU_MAX_KEY_LEN);
        CRU_keyval_tab[CRU_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

        valstart = ++p; /* start of value */

        while (*p != ' '
            && *p != '\n'
            && *p != '\0')
        {
            ++p;
        }

        strncpy(CRU_keyval_tab[CRU_keyval_tab_idx].value, valstart, CRU_MAX_VAL_LEN);
        CRU_keyval_tab[CRU_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
        ++CRU_keyval_tab_idx;

        if (*p == ' ')
        {
            continue;
        }

        if (*p == '\n' || *p == '\0')
        {
            /* Value has been set to empty. */
            return 0;
        }
    }
}
    
char* CR_MPDU_getval(const char* keystr, char* valstr, int vallen)
{
    int i = 0;

    for (; i < CRU_keyval_tab_idx; ++i)
    {
        if (strcmp( keystr, CRU_keyval_tab[i].key ) == 0)
        {
            strncpy (valstr, CRU_keyval_tab[i].value, vallen - 1);
            valstr[vallen - 1] = '\0';
            return valstr;
        }
    }

    valstr[0] = '\0';
    return NULL;
}

#endif /* CR_FTB */

#endif /* ifdef CKPT */
