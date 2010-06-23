/* Copyright (c) 2003-2010, The Ohio State University. All rights
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

#include "mpirun_ckpt.h"

#ifdef CKPT

#include <sys/time.h>
#include <libcr.h>
#include <pthread.h>
/*
static pthread_t       cr_tid;
static cr_client_id_t  cr_id;
static pthread_mutex_t cr_lock;

static pthread_spinlock_t flock;
static int fcnt;

//#define CR_ERRMSG_SZ 64
static char cr_errmsg[CR_ERRMSG_SZ];
*/
pthread_t cr_tid = 0;
cr_client_id_t cr_id = 0;
pthread_mutex_t cr_lock;

pthread_spinlock_t flock;
int fcnt = 0;
static int num_procs = 0;
#define CR_ERRMSG_SZ 64
char cr_errmsg[CR_ERRMSG_SZ];

/*
#define CR_MUTEX_LOCK do {          \
    pthread_mutex_lock(&cr_lock);   \
} while(0)

#define CR_MUTEX_UNLOCK do {        \
    pthread_mutex_unlock(&cr_lock); \
} while(0)

#define MAX_CR_MSG_LEN  256
#define CRU_MAX_VAL_LEN 64

#define DEFAULT_CHECKPOINT_FILENAME "/tmp/ckpt"
#define CR_MAX_FILENAME 128
#define CR_SESSION_MAX  16
*/



/*static int restart_context;
static int cached_restart_context;

static int   CR_Callback(void *);
*/
static void *CR_Loop(void *);

int restart_context = 0;
int cached_restart_context = 0;

static void *CR_Loop(void *);
static int CR_Callback(void *);


extern char *CR_MPDU_getval(const char *, char *, int);
extern int CR_MPDU_parse_keyvals(char *);
extern int CR_MPDU_readline(int, char *, int);
extern int CR_MPDU_writeline(int, char *);

/*typedef enum {
    CR_INIT,
    CR_READY,
    CR_CHECKPOINT,
    CR_CHECKPOINT_CONFIRM,
    CR_CHECKPOINT_ABORT,
    CR_RESTART,
    CR_RESTART_CONFIRM,
    CR_FINALIZED,
} CR_state_t;

static CR_state_t cr_state;

static unsigned long starting_time;
static unsigned long last_ckpt;

static int checkpoint_count;
static char sessionid[CR_SESSION_MAX];
static int checkpoint_interval;
static int max_save_ckpts;
static int max_ckpts;

static char ckpt_filename[CR_MAX_FILENAME];
*/

CR_state_t cr_state = 0;

unsigned long starting_time = 0;
unsigned long last_ckpt = 0;

int checkpoint_count = 0;
char sessionid[CR_SESSION_MAX];
int checkpoint_interval = 0;
int max_save_ckpts = 0;
int max_ckpts = 0;

char ckpt_filename[CR_MAX_FILENAME];

#ifdef CR_FTB

//#include <libftb.h>

//#define FTB_MAX_SUBSCRIPTION_STR 64

#define CR_FTB_EVENT_INFO {               \
  {"CR_FTB_CHECKPOINT", "info"}, \
  {"CR_FTB_CKPT_DONE", "info"},  \
  {"CR_FTB_CKPT_FAIL", "info"},  \
  {"CR_FTB_RSRT_DONE", "info"},  \
  {"CR_FTB_RSRT_FAIL", "info"},  \
  {"CR_FTB_APP_CKPT_REQ", "info"}, \
  {"CR_FTB_CKPT_FINALIZE", "info"} \
}

/* Index into the Event Info Table */
//#define CR_FTB_CHECKPOINT    0
//#define CR_FTB_EVENTS_MAX    1 /* HACK */
//#define CR_FTB_CKPT_DONE     1
//#define CR_FTB_CKPT_FAIL     2
//#define CR_FTB_RSRT_DONE     3
//#define CR_FTB_RSRT_FAIL     4
//#define CR_FTB_APP_CKPT_REQ  5
//#define CR_FTB_CKPT_FINALIZE 6

/* Type of event to throw */
//#define FTB_EVENT_NORMAL   1
//#define FTB_EVENT_RESPONSE 2

/* Macro to initialize the event property structure */
//#define SET_EVENT(_eProp, _etype, _payload...)             \
do {\
    _eProp.event_type = _etype;\
    snprintf(_eProp.event_payload, FTB_MAX_PAYLOAD_DATA, _payload);\
} while (0)

/* Macro to pick an CR_FTB event */
//#define EVENT(n) (cr_ftb_events[n].event_name)

/*static FTB_client_t        ftb_cinfo;
static FTB_client_handle_t ftb_handle;
static FTB_event_info_t    cr_ftb_events[] = CR_FTB_EVENT_INFO;
static FTB_subscribe_handle_t shandle;
static int ftb_init_done;

static pthread_cond_t  cr_ftb_ckpt_req_cond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cr_ftb_ckpt_req_mutex = PTHREAD_MUTEX_INITIALIZER;
static int cr_ftb_ckpt_req;
static int cr_ftb_app_ckpt_req;
static int cr_ftb_finalize_ckpt;

static int  cr_ftb_init(int, char *);
static void cr_ftb_finalize();
static int  cr_ftb_callback(FTB_receive_event_t *, void *);
static int  cr_ftb_wait_for_resp(int);
*/

FTB_client_t ftb_cinfo;
FTB_client_handle_t ftb_handle;
FTB_event_info_t cr_ftb_events[] = CR_FTB_EVENT_INFO;
FTB_subscribe_handle_t shandle;
int ftb_init_done;

pthread_cond_t cr_ftb_ckpt_req_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cr_ftb_ckpt_req_mutex = PTHREAD_MUTEX_INITIALIZER;
int cr_ftb_ckpt_req;
int cr_ftb_app_ckpt_req;
int cr_ftb_finalize_ckpt;

//static int  cr_ftb_init(int, char *);
//int  cr_ftb_init(int nprocs);
static void cr_ftb_finalize();
static int cr_ftb_callback(FTB_receive_event_t *, void *);
static int cr_ftb_wait_for_resp(int);

#else

//extern char *CR_MPDU_getval(const char *, char *, int);
//extern int   CR_MPDU_parse_keyvals(char *);
//extern int   CR_MPDU_readline(int , char *, int);
//extern int   CR_MPDU_writeline(int , char *);

int *mpirun_fd;
int mpirun_port;

#endif				/* CR_FTB */

static int mpirun_listen_fd = 0;

void set_ckpt_nprocs(int nprocs)
{
    num_procs = nprocs;
}
int ckptInit()
{
    int val;
    time_t tm;
    struct tm *stm;

    if (pthread_spin_init(&flock, PTHREAD_PROCESS_PRIVATE) != 0) {
	DBG(fprintf
	    (stderr,
	     "[mpirun_rsh:main] pthread_spin_init(flock) failed\n"));
	return (-6);
    }

    cr_id = cr_init();
    if (cr_id < 0) {
        fprintf(stderr, "CR Initialization failed\n");
        return (-1);
    }

    if (cr_register_callback(CR_Callback, (void *) NULL, CR_THREAD_CONTEXT) < 0) {
        fprintf(stderr, "CR Callback Registration failed\n");
        return (-2);
    }

    if (pthread_mutex_init(&cr_lock, NULL)) {
        DBG(perror("[mpirun_rsh:main] pthread_mutex_init(cr_lock)"));
        return (-3);
    }

    strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME);

    tm = time(NULL);
    if ((time_t) tm == -1) {
        DBG(fprintf(stderr, "[mpirun_rsh:main] time() failed\n"));
        return (-4);
    }

    stm = localtime(&tm);
    if (!stm) {
        DBG(fprintf(stderr, "[mpirun_rsh:main] localtime() failed\n"));
        return (-5);
    }

    snprintf(sessionid, CR_SESSION_MAX, "%d%d%d%d%d",
	     stm->tm_yday, stm->tm_hour, stm->tm_min, stm->tm_sec,
	     getpid());
    sessionid[CR_SESSION_MAX - 1] = '\0';
    restart_from_ckpt();

  return 0;
}


void create_connections(int NSPAWNS)
{
  struct sockaddr_in cr_sa;
  int val;
#ifdef CR_FTB
    /* Nothing to be done */
#else
    mpirun_fd = malloc(NSPAWNS * sizeof(int));
    if (!mpirun_fd) {
        perror("[mpirun_rsh] malloc(mpirun_fd)");
        exit(EXIT_FAILURE);
    }

    /* Create connections for the mpispawns to connect back */
    mpirun_listen_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mpirun_listen_fd < 0) {
        perror("[mpirun_rsh] socket(mpirun_listen_fd)");
        exit(EXIT_FAILURE);
    }

    memset(&cr_sa, 0, sizeof(cr_sa));
    cr_sa.sin_addr.s_addr = INADDR_ANY;
    cr_sa.sin_port = 0;

    if (bind(mpirun_listen_fd, (struct sockaddr *) &cr_sa, sizeof(cr_sa)) <
	0) {
        perror("[mpirun_rsh] bind(mpirun_listen_fd)");
        exit(EXIT_FAILURE);
    }

    val = sizeof(cr_sa);
    if (getsockname(mpirun_listen_fd, &cr_sa, (socklen_t *) & val) < 0) {
        perror("[mpirun_rsh] getsockname(mpirun_listen_fd)");
        close(mpirun_listen_fd);
        exit(EXIT_FAILURE);
    }

    mpirun_port = ntohs(cr_sa.sin_port);

    if (listen(mpirun_listen_fd, NSPAWNS) < 0) {
        perror("[mpirun_rsh] listen(mpirun_listen_fd)");
        close(mpirun_listen_fd);
        exit(EXIT_FAILURE);
    }
#endif				/* CR_FTB */
}

char *create_mpispawn_vars(char *mpispawn_env)
{

    char *tmp = NULL;
#ifdef CR_FTB
    /* Keep mpispawn happy. Pass some junk value */
    tmp = mkstr("%s MPISPAWN_MPIRUN_CR_PORT=%d", mpispawn_env, 0);
#else
    tmp =
	mkstr("%s MPISPAWN_MPIRUN_CR_PORT=%d", mpispawn_env, mpirun_port);
#endif
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    tmp =
	mkstr("%s MPISPAWN_CR_CONTEXT=%d", mpispawn_env,
	      cached_restart_context);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_CR_SESSIONID=%s", mpispawn_env, sessionid);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    //tmp = mkstr ("%s MPISPAWN_CR_CKPT_CNT=%d", mpispawn_env, checkpoint_count);
    tmp =
	mkstr("%s MPISPAWN_CR_CKPT_CNT=%d", mpispawn_env,
	      get_checkpoint_count());
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }

    return tmp;

  allocation_error:
    perror("spawn_fast");
    if (mpispawn_env) {
        fprintf(stderr, "%s\n", mpispawn_env);
        free(mpispawn_env);
    }
    exit(EXIT_FAILURE);


}

void close_connections(int NSPAWNS)
{
  int i;
#ifdef CR_FTB
    /* Nothing to be done */
#else
    for (i = 0; i < NSPAWNS; i++) {
	if ((mpirun_fd[i] = accept(mpirun_listen_fd, NULL, NULL)) < 0) {
	    snprintf(cr_errmsg, CR_ERRMSG_SZ,
		     "[mpirun_rsh] accept(mpirun_fd[%d])", i);
	    perror(cr_errmsg);
	    close(mpirun_listen_fd);
	    /*Let mpirun_rsh handle the error. In this way, it can terminate the right pids.*/
	    //cleanup();
	}
    }

    close(mpirun_listen_fd);
    mpirun_listen_fd = 0;

#endif				/* CR_FTB */

    CR_MUTEX_LOCK;
    cr_state = CR_READY;
    CR_MUTEX_UNLOCK;



}


void restart_from_ckpt()
{
    cached_restart_context = restart_context;
    restart_context = 0;
    fcnt = 0;

    CR_MUTEX_LOCK;
    cr_state = CR_INIT;
    CR_MUTEX_UNLOCK;

    if (pthread_create(&cr_tid, NULL, CR_Loop, NULL) < 0) {
        fprintf(stderr, "CR Timer Thread creation failed.\n");
        fflush(stderr);
        cr_tid = 0;
    }

    /* Reset the parsing function */
    extern int optind;
    optind = 1;
}


void free_locks()
{
    pthread_spin_lock(&flock);
    if (fcnt) {
	pthread_spin_unlock(&flock);
	return;
    }
    fcnt = 1;
    pthread_spin_unlock(&flock);
}


void cr_cleanup()
{
    if (cr_tid) {
	pthread_kill(cr_tid, SIGTERM);
	cr_tid = 0;
    }
}

static void *CR_Loop(void *arg)
{
    struct timeval starting, now, tv;
    int time_counter;
    char buf[CR_MAX_FILENAME];

#ifndef CR_FTB
    char cr_msg_buf[MAX_CR_MSG_LEN];
    char valstr[CRU_MAX_VAL_LEN];
    fd_set set;
    int i, n, nfd = 0, ret;
#endif

    cr_checkpoint_args_t cr_file_args;
    cr_checkpoint_handle_t cr_handle;

    while (1) {
	CR_MUTEX_LOCK;
	if (cr_state == CR_INIT) {
	    CR_MUTEX_UNLOCK;
	    sleep(1);
	} else {
	    CR_MUTEX_UNLOCK;
	    break;
	}
    }

    gettimeofday(&starting, NULL);
    starting_time = last_ckpt = starting.tv_sec;

    while (1) {
	if (restart_context)
	    break;

	tv.tv_sec = 1;
	tv.tv_usec = 0;

	CR_MUTEX_LOCK;
	if (cr_state == CR_FINALIZED) {
	    CR_MUTEX_UNLOCK;
	    break;
	}
	CR_MUTEX_UNLOCK;

#ifdef CR_FTB
	sleep(1);
	if (cr_ftb_app_ckpt_req || cr_ftb_finalize_ckpt)
#else
	FD_ZERO(&set);
	for (i = 0; i < NSPAWNS; i++) {
	    FD_SET(mpirun_fd[i], &set);
	    nfd = (nfd >= mpirun_fd[i]) ? nfd : mpirun_fd[i];
	}
	nfd += 1;

	ret = select(nfd, &set, NULL, NULL, &tv);

	if ((ret < 0) && (errno != EINTR) && (errno != EBUSY)) {
	    perror("[CR_Loop] select()");
	    return ((void *) -1);
	} else if (ret > 0)
#endif
	{

	    CR_MUTEX_LOCK;
	    if (cr_state != CR_READY) {
		CR_MUTEX_UNLOCK;
		continue;
	    }
	    CR_MUTEX_UNLOCK;

#ifdef CR_FTB
	    if (cr_ftb_app_ckpt_req)
#else
	    for (i = 0; i < NSPAWNS; i++) {

		if (!FD_ISSET(mpirun_fd[i], &set))
		    continue;

		n = CR_MPDU_readline(mpirun_fd[i], cr_msg_buf,
				     MAX_CR_MSG_LEN);
		if (n == 0)
		    continue;

		if (CR_MPDU_parse_keyvals(cr_msg_buf) < 0)
		    break;

		CR_MPDU_getval("cmd", valstr, CRU_MAX_VAL_LEN);

		if (strcmp(valstr, "app_ckpt_req") == 0)
#endif
		{
#ifdef CR_FTB
		    cr_ftb_app_ckpt_req = 0;
#endif
		    CR_MUTEX_LOCK;
		    sprintf(buf, "%s.%d.sync", ckpt_filename,
			    checkpoint_count + 1);
		    cr_initialize_checkpoint_args_t(&cr_file_args);
		    cr_file_args.cr_scope = CR_SCOPE_PROC;
		    cr_file_args.cr_target = getpid();
		    cr_file_args.cr_fd =
			open(buf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		    cr_file_args.cr_signal = 0;
		    cr_file_args.cr_timeout = 0;
		    cr_file_args.cr_flags &= ~CR_CHKPT_DUMP_ALL;	// Save None
		    cr_request_checkpoint(&cr_file_args, &cr_handle);
		    cr_poll_checkpoint(&cr_handle, NULL);
		    last_ckpt = now.tv_sec;
		    CR_MUTEX_UNLOCK;
		}
#ifdef CR_FTB
		else if (cr_ftb_finalize_ckpt)
#else
		else if (strcmp(valstr, "finalize_ckpt") == 0)
#endif
		{
#ifdef CR_FTB
		    cr_ftb_finalize_ckpt = 0;
#endif
		    CR_MUTEX_LOCK;
		    cr_state = CR_FINALIZED;
		    CR_MUTEX_UNLOCK;
		    break;
		}
#ifndef CR_FTB
	    }
#endif
	} else {

	    CR_MUTEX_LOCK;

	    gettimeofday(&now, NULL);
	    time_counter = (now.tv_sec - starting_time);

	    if ((checkpoint_interval > 0) &&
		(now.tv_sec != last_ckpt) &&
		(cr_state == CR_READY) &&
		(time_counter % checkpoint_interval == 0)) {
		/* Inject a checkpoint */
		if ((max_ckpts == 0) || (max_ckpts > checkpoint_count)) {
		    sprintf(buf, "%s.%d.auto", ckpt_filename,
			    checkpoint_count + 1);
		    cr_initialize_checkpoint_args_t(&cr_file_args);
		    cr_file_args.cr_scope = CR_SCOPE_PROC;
		    cr_file_args.cr_target = getpid();
		    cr_file_args.cr_fd =
			open(buf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		    cr_file_args.cr_signal = 0;
		    cr_file_args.cr_timeout = 0;
		    cr_file_args.cr_flags &= ~CR_CHKPT_DUMP_ALL;	// Save None
		    cr_request_checkpoint(&cr_file_args, &cr_handle);
		    cr_poll_checkpoint(&cr_handle, NULL);
		    last_ckpt = now.tv_sec;
		}

		/* Remove the ealier checkpoints */
		if ((max_save_ckpts > 0)
		    && (max_save_ckpts < checkpoint_count + 1)) {
		    sprintf(buf, "%s.%d.auto", ckpt_filename,
			    checkpoint_count + 1 - max_save_ckpts);
		    unlink(buf);
		}
	    }

	    CR_MUTEX_UNLOCK;
	}
    }

    return (0);
}

int get_checkpoint_count()
{
    return checkpoint_count;
}

void save_ckpt_vars(char *name, char *value)
{
    if (strcmp(name, "MV2_CKPT_FILE") == 0) {
	strncpy(ckpt_filename, value, CR_MAX_FILENAME);
    } else if (strcmp(name, "MV2_CKPT_INTERVAL") == 0) {
	checkpoint_interval = atoi(value) * 60;
    } else if (strcmp(name, "MV2_CKPT_MAX_SAVE_CKPTS") == 0) {
	max_save_ckpts = atoi(value);
    } else if (strcmp(name, "MV2_CKPT_MAX_CKPTS") == 0) {
	max_ckpts = atoi(value);
    }
}

static int CR_Callback(void *arg)
{
    int ret;
    struct timeval now;

#ifndef CR_FTB
    int i, Progressing, nfd = 0;
    char buf[MAX_CR_MSG_LEN];
    char val[CRU_MAX_VAL_LEN];
    fd_set set;
#endif

    if (cr_state != CR_READY) {
	cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
	fprintf(stderr, "[CR_Callback] CR Subsystem not ready\n");
	return (0);
    }

    gettimeofday(&now, NULL);
    last_ckpt = now.tv_sec;

    checkpoint_count++;
    cr_state = CR_CHECKPOINT;

#ifdef CR_FTB
    FTB_event_properties_t eprop;
    FTB_event_handle_t ehandle;

    SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
    ret =
	FTB_Publish(ftb_handle, EVENT(CR_FTB_CHECKPOINT), &eprop,
		    &ehandle);
    if (ret != FTB_SUCCESS) {
	fprintf(stderr, "[CR_Callback] FTB_Publish() failed with %d\n",
		ret);
	cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
	return (-1);
    }


    ret = cr_ftb_wait_for_resp(num_procs);
    if (ret) {
	fprintf(stderr, "[CR_Callback] Error in getting a response\n");
	cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
	return (-2);
    }

    cr_ftb_finalize();
#else

    sprintf(buf, "cmd=ckpt_req file=%s\n", ckpt_filename);

    for (i = 0; i < NSPAWNS; i++)
	CR_MPDU_writeline(mpirun_fd[i], buf);
    /* Wait for Checkpoint to finish */
    Progressing = num_procs;
    while (Progressing) {

	FD_ZERO(&set);
	for (i = 0; i < NSPAWNS; i++) {
	    FD_SET(mpirun_fd[i], &set);
	    nfd = (nfd >= mpirun_fd[i]) ? nfd : mpirun_fd[i];
	}
	nfd += 1;

	ret = select(nfd, &set, NULL, NULL, NULL);

	if (ret < 0) {
	    perror("[CR_Callback] select()");
	    cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
	    return (-1);
	}

	for (i = 0; i < NSPAWNS; i++) {

	    if (!FD_ISSET(mpirun_fd[i], &set))
		continue;

	    CR_MPDU_readline(mpirun_fd[i], buf, MAX_CR_MSG_LEN);

	    if (CR_MPDU_parse_keyvals(buf) < 0) {
		fprintf(stderr,
			"[CR_Callback] CR_MPDU_parse_keyvals() failed\n");
		cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
		return (-2);
	    }

	    CR_MPDU_getval("result", val, CRU_MAX_VAL_LEN);

	    if (strcmp(val, "succeed") == 0) {
		--Progressing;
		continue;
	    }

	    if (strcmp(val, "finalize_ckpt") == 0) {
		/* MPI Process finalized */
		cr_state = CR_FINALIZED;
		return (0);
	    }

	    if (strcmp(val, "fail") == 0) {
		fprintf(stderr,
			"[CR_Callback] Checkpoint of a Process Failed\n");
		fflush(stderr);
		cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
		return (-3);
	    }
	}
    }				/* while(Progressing) */

#endif				/* CR_FTB */

    ret = cr_checkpoint(CR_CHECKPOINT_READY);

    if (ret < 0) {
	fprintf(stderr, "[CR_Callback] Checkpoint of Console Failed\n");
	fflush(stderr);
	cr_state = CR_READY;
	return (-4);
    } else if (ret == 0) {
	cr_state = CR_READY;
    } else if (ret) {

	restart_context = 1;
	cr_state = CR_RESTART;

    }

    return (0);
}

#ifdef CR_FTB

//static int cr_ftb_init(int nprocs, char *sessionid)
int cr_ftb_init(int nprocs)
{
    int ret;
    char *subscription_str;

    memset(&ftb_cinfo, 0, sizeof(ftb_cinfo));
    strcpy(ftb_cinfo.client_schema_ver, "0.5");
    strcpy(ftb_cinfo.event_space, "FTB.STARTUP.MV2_MPIRUN");
    strcpy(ftb_cinfo.client_name, "MV2_MPIRUN");

    /* sessionid should be <= 16 bytes since client_jobid is 16 bytes. */
    snprintf(ftb_cinfo.client_jobid, FTB_MAX_CLIENT_JOBID, "%s",
	     sessionid);

    strcpy(ftb_cinfo.client_subscription_style, "FTB_SUBSCRIPTION_BOTH");
    ftb_cinfo.client_polling_queue_len = nprocs;

    ret = FTB_Connect(&ftb_cinfo, &ftb_handle);
    if (ret != FTB_SUCCESS)
	goto err_connect;

    ret = FTB_Declare_publishable_events(ftb_handle, NULL,
					 cr_ftb_events, CR_FTB_EVENTS_MAX);
    if (ret != FTB_SUCCESS)
	goto err_declare_events;

    subscription_str = malloc(sizeof(char) * FTB_MAX_SUBSCRIPTION_STR);
    if (!subscription_str)
	goto err_malloc;

    snprintf(subscription_str, FTB_MAX_SUBSCRIPTION_STR,
	     "event_space=FTB.MPI.MVAPICH2 , jobid=%s", sessionid);

    ret = FTB_Subscribe(&shandle, ftb_handle, subscription_str,
			cr_ftb_callback, NULL);
    free(subscription_str);
    if (ret != FTB_SUCCESS)
	goto err_subscribe;

    ftb_init_done = 1;

    return (0);

  err_connect:
    fprintf(stderr, "FTB_Connect() failed with %d\n", ret);
    ret = -1;
    goto exit_connect;

  err_declare_events:
    fprintf(stderr, "FTB_Declare_publishable_events() failed with %d\n",
	    ret);
    ret = -2;
    goto exit_declare_events;

  err_malloc:
    fprintf(stderr, "Failed to malloc() subscription_str\n");
    ret = -3;
    goto exit_malloc;

  err_subscribe:
    fprintf(stderr, "FTB_Subscribe() failed with %d\n", ret);
    ret = -4;
    goto exit_subscribe;

  exit_subscribe:
  exit_malloc:
  exit_declare_events:
    FTB_Disconnect(ftb_handle);

  exit_connect:
    return (ret);
}

static void cr_ftb_finalize()
{
    if (ftb_init_done)
	FTB_Disconnect(ftb_handle);
}

static int cr_ftb_wait_for_resp(int nprocs)
{
    pthread_mutex_lock(&cr_ftb_ckpt_req_mutex);
    DBG(fprintf(stderr,"nprocs %d \n", nprocs));
    cr_ftb_ckpt_req = nprocs;
    pthread_cond_wait(&cr_ftb_ckpt_req_cond, &cr_ftb_ckpt_req_mutex);

    if (cr_ftb_ckpt_req == 0) {
        return (0);
    } else {
        fprintf(stderr, "cr_ftb_wait_for_resp() returned %d\n",
		cr_ftb_ckpt_req);
	return (-1);
    }
}

static int cr_ftb_callback(FTB_receive_event_t * revent, void *arg)
{
     //fprintf(stdout, "Got event %s from %s\n",
     //        revent->event_name, revent->client_name);

    /* TODO: Do some sanity checking to see if this is the intended target */

    if (!strcmp(revent->event_name, EVENT(CR_FTB_CKPT_DONE))) {
	if (cr_ftb_ckpt_req <= 0) {
	    fprintf(stderr, "Got CR_FTB_CKPT_DONE but "
		    "cr_ftb_ckpt_req not set\n");
	    cr_ftb_ckpt_req = -1;
	    pthread_cond_signal(&cr_ftb_ckpt_req_cond);
	    return (0);
	}
	pthread_mutex_lock(&cr_ftb_ckpt_req_mutex);
	--cr_ftb_ckpt_req;
	pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);
	if (!cr_ftb_ckpt_req) {
	    pthread_cond_signal(&cr_ftb_ckpt_req_cond);
	    return (0);
	}
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_CKPT_FAIL))) {
        fprintf(stderr, "Got CR_FTB_CKPT_FAIL\n");
        cr_ftb_ckpt_req = -2;
        pthread_cond_signal(&cr_ftb_ckpt_req_cond);
        return (0);
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_APP_CKPT_REQ))) {
        cr_ftb_app_ckpt_req = 1;
        return (0);
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_CKPT_FINALIZE))) {
        cr_ftb_finalize_ckpt = 1;
        return (0);
    }
}
#endif				/* CR_FTB */

void finalize_ckpt()
{
    if (restart_context) {

	/* Wait for previous instance of CR_Loop to exit */
	if (cr_tid) {
	    pthread_join(cr_tid, NULL);
	    cr_tid = 0;
	}

	/* Flush out stale data */
	free_memory();
#ifdef CR_FTB
	/* Nothing to be done */
#else
	free(mpirun_fd);
#endif

	fprintf(stderr, "Restarting...\n");
	fflush(stderr);

	restart_from_ckpt();
    }
}

#endif				/* CKPT */
