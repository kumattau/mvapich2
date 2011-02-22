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

#include "mpirun_ckpt.h"

#define MPIRUN_ERR_ABORT(args...)  do {                                                \
    fprintf(stderr, "[%s:%d] ",__FILE__, __LINE__);                                    \
    fprintf(stderr, args);                                                             \
    exit(-1);                                                                          \
}while(0)

#define MPIRUN_ERR(args...)  do {                                                      \
    fprintf(stderr, "[%s:%d] ",__FILE__, __LINE__);                                    \
    fprintf(stderr, args);                                                             \
}while(0)


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
static int cr_work_can_exit=0;
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

/*#define CR_FTB_EVENT_INFO {               \
  {"CR_FTB_CHECKPOINT", "info"}, \
  {"CR_FTB_CKPT_DONE", "info"},  \
  {"CR_FTB_CKPT_FAIL", "info"},  \
  {"CR_FTB_RSRT_DONE", "info"},  \
  {"CR_FTB_RSRT_FAIL", "info"},  \
  {"CR_FTB_APP_CKPT_REQ", "info"}, \
  {"CR_FTB_CKPT_FINALIZE", "info"} \
}   */

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

/*struct spawn_info_s {
    char spawnhost[32];
    int  sparenode;
}; */

char * current_spare_host;

struct spawn_info_s *spawninfo;

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
pthread_cond_t cr_mig_req_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cr_mig_req_mutex = PTHREAD_MUTEX_INITIALIZER;

//static int  cr_ftb_init(int, char *);
//int  cr_ftb_init(int nprocs);
static void cr_ftb_finalize();
static int cr_ftb_callback(FTB_receive_event_t *, void *);
static int cr_ftb_wait_for_resp(int);

/* Start - CR_MIG */
//#define HOSTFILE_LEN 256
int  sparehosts_on;
char sparehostfile[HOSTFILE_LEN+1];
char **sparehosts;
int  nsparehosts;
static int  sparehosts_idx;

static char cr_mig_src_host[32];
static char cr_mig_tgt_host[32];

//static int read_sparehosts(char *, char ***, int *);
static int get_src_tgt(char *, char *, char *);
/* End - CR_MIG */

#else

//extern char *CR_MPDU_getval(const char *, char *, int);
//extern int   CR_MPDU_parse_keyvals(char *);
//extern int   CR_MPDU_readline(int , char *, int);
//extern int   CR_MPDU_writeline(int , char *);

int *mpirun_fd;
int mpirun_port;

#endif				/* CR_FTB */

#if defined(CKPT) && defined(CR_AGGRE)
extern int use_aggre; // by default we use CR-aggregation
extern int use_aggre_mig; // by default, enable aggre-mig
#endif

//#define dbg(fmt, args...)   do{ \
//        fprintf(stderr,"%s [mpirun_rsh]: "fmt, __func__, ##args); fflush(stderr); }while(0)
#define dbg(fmt, args...)

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
        MPIRUN_ERR_ABORT("BLCR call cr_init() failed\n");
        return (-1);
    }

    if (cr_register_callback(CR_Callback, (void *) NULL, CR_THREAD_CONTEXT) == -1) {
        MPIRUN_ERR_ABORT("BLCR call cr_register_callback() failed with error %d: %s\n",errno,cr_strerror(errno));
        return (-2);
    }

    if (pthread_mutex_init(&cr_lock, NULL)) {
	perror("[mpirun_rsh:main] pthread_mutex_init(cr_lock)");
        return (-3);
    }

    strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME);

    tm = time(NULL);
    if ((time_t) tm == -1) {
	fprintf(stderr, "[mpirun_rsh:main] time() failed\n");
        return (-4);
    }

    stm = localtime(&tm);
    if (!stm) {
	fprintf(stderr, "[mpirun_rsh:main] localtime() failed\n");
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
        // Ignore all EINTR 'Interrupted system call' errors
        do {
            mpirun_fd[i] = accept(mpirun_listen_fd, NULL, NULL);
        } while ( mpirun_fd[i] < 0 && errno == EINTR );
        if ( mpirun_fd[i] < 0 ) {
            snprintf(cr_errmsg, CR_ERRMSG_SZ, "[mpirun_rsh] accept(mpirun_fd[%d])", i);
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

    cr_work_can_exit = 0;
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
    cr_work_can_exit = 1;
	//pthread_kill(cr_tid, SIGTERM);
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

    if(cr_work_can_exit)    break;
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
            int ret = cr_initialize_checkpoint_args_t(&cr_file_args);
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_initialize_checkpoint_args_t() failed\n");
            }
            cr_file_args.cr_scope = CR_SCOPE_PROC;
		    cr_file_args.cr_target = getpid();
		    cr_file_args.cr_fd =
			open(buf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		    cr_file_args.cr_signal = 0;
		    cr_file_args.cr_timeout = 0;
		    cr_file_args.cr_flags &= ~CR_CHKPT_DUMP_ALL;	// Save None
            ret = cr_request_checkpoint(&cr_file_args, &cr_handle);
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_request_checkpoint() failed with error %d: %s\n",errno,cr_strerror(errno));
            }
            // Retry while interrupted
            do {
                ret = cr_poll_checkpoint(&cr_handle, NULL);
            } while ( ret == CR_POLL_CHKPT_ERR_PRE && errno == EINTR );
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_poll_checkpoint() failed with error %d: %s\n",errno,cr_strerror(errno));
            } else if ( ret == 0 ) {
                // 0 means that the checkpoint is in progress
                // It should never happen because we don't specify any timeout when calling cr_poll_checkpoint()
                MPIRUN_ERR_ABORT("Bad assertion\n");
            }
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
            int ret = cr_initialize_checkpoint_args_t(&cr_file_args);
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_initialize_checkpoint_args_t() failed");
            }
            cr_file_args.cr_scope = CR_SCOPE_PROC;
		    cr_file_args.cr_target = getpid();
		    cr_file_args.cr_fd =
			open(buf, O_CREAT | O_WRONLY | O_TRUNC, 0666);
		    cr_file_args.cr_signal = 0;
		    cr_file_args.cr_timeout = 0;
		    cr_file_args.cr_flags &= ~CR_CHKPT_DUMP_ALL;	// Save None
		    ret = cr_request_checkpoint(&cr_file_args, &cr_handle);
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_request_checkpoint() failed with error %d: %s\n",errno,cr_strerror(errno));
            }
            // Retry while interrupted
            do {
                ret = cr_poll_checkpoint(&cr_handle, NULL);
            } while ( ret == CR_POLL_CHKPT_ERR_PRE && errno == EINTR );
            if (ret < 0) {
                MPIRUN_ERR_ABORT("BLCR call cr_poll_checkpoint() failed with error %d: %s\n",errno,cr_strerror(errno));
            } else if ( ret == 0 ) {
                // 0 means that the checkpoint is in progress
                // It should never happen because we don't specify any timeout when calling cr_poll_checkpoint()
                MPIRUN_ERR_ABORT("Bad assertion\n");
            }
            
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

/*
 * This stuff may be in the environment as well.  Call this function before
 * save_ckpt_vars is called so the old way takes precedence.
 */
void save_ckpt_vars_env(void)
{
    if (getenv("MV2_CKPT_FILE")) {
	strncpy(ckpt_filename, getenv("MV2_CKPT_FILE"), CR_MAX_FILENAME);
    }

    if (getenv("MV2_CKPT_INTERVAL")) {
	checkpoint_interval = atoi(getenv("MV2_CKPT_INTERVAL")) * 60;
    }
    
    if (getenv("MV2_CKPT_MAX_SAVE_CKPTS")) {
	max_save_ckpts = atoi(getenv("MV2_CKPT_MAX_SAVE_CKPTS"));
    }

    if (getenv("MV2_CKPT_MAX_CKPTS")) {
	max_ckpts = atoi(getenv("MV2_CKPT_MAX_CKPTS"));
    }
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

    DBG(fprintf(stderr, "mpirun_rsh- [CR_Callback] -->v\n"));
    int ret,i;
    struct timeval now;
    char buf[MAX_CR_MSG_LEN];

#ifdef CR_FTB
    FILE *fp;
#else
    int Progressing, nfd = 0;
    char val[CRU_MAX_VAL_LEN];
    fd_set set;
#endif

    if (cr_state != CR_READY) {
        MPIRUN_ERR("[CR_Callback] CR Subsystem not ready\n");
        ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
        if (ret != -CR_ETEMPFAIL){
            MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
        }
        return (0);
    }

    gettimeofday(&now, NULL);
    last_ckpt = now.tv_sec;

    checkpoint_count++;
    cr_state = CR_CHECKPOINT;

    dbg("mpirun_rsh [CR_Callback](v) cr_checkpoint_count=%d\n",checkpoint_count);

#ifdef CR_FTB
    FTB_event_properties_t eprop;
    FTB_event_handle_t ehandle;

	//if (sparehosts_on)
    if(0)
    {
		dbg( "OPEN MIGRATION FILE \n");
		char *cr_mig_dat;
		cr_mig_dat = malloc (sizeof(getenv("HOME")) + 12);
		cr_mig_dat = getenv("HOME");

        strcat(cr_mig_dat,"/cr_mig.dat");
        fp = fopen(cr_mig_dat, "r");
        if (!fp)
        {
            fprintf(stderr, "[Migration] cr_mig.dat not found\n");
            goto no_mig_req;
        }

        if (sparehosts_idx == 2 * nsparehosts)
        {
            MPIRUN_ERR("\n mpirun_rsh [Migration] Out of Spares\n");
            ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
            if (ret != -CR_ETEMPFAIL){
                MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
            }
            return(0);
        }
   	    // migrate src
		fgets(buf, MAX_CR_MSG_LEN, fp);
        i = strlen(buf);
        if (buf[i-1] == '\n') buf[i-1] = '\0';

        current_spare_host = strdup(buf); // mig-src will become spare node
        //current_spare_host = strdup(sparehosts[sparehosts_idx]);
		strncat(buf, " ", MAX_CR_MSG_LEN);
        //migrate tgt
        if(checkpoint_count == 2)
        {
			printf("checkpoint_count == 2 \n");
            strncat(buf, "wci11", MAX_CR_MSG_LEN);
            fprintf(stderr, "buffer =%s:\n",buf);
        }
        else
        {
          strncat(buf, sparehosts[sparehosts_idx++], MAX_CR_MSG_LEN);
        }

        dbg("*** FTB_Publish(CR_FTB_MIGRATE): %s \n",buf);
        SET_EVENT(eprop, FTB_EVENT_NORMAL, buf);
        ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_MIGRATE), &eprop, &ehandle);
        if (ret != FTB_SUCCESS)
        {
            fprintf(stderr, "[CR_Callback] FTB_Publish(CR_FTB_MIGRATE) "
                                          "failed with %d\n", ret);
        }
        dbg( "***  Sent CR_FTB_MIGRATE:%s: count=%d\n", buf, checkpoint_count);
       
        ret = cr_checkpoint(CR_CHECKPOINT_OMIT);
        if (ret != -CR_EOMITTED){
            MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
        }
        return(0);
    }
    else
    {
no_mig_req:
        SET_EVENT(eprop, FTB_EVENT_NORMAL, "");
        ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_CHECKPOINT), &eprop, &ehandle);
        if (ret != FTB_SUCCESS) {
            MPIRUN_ERR("[CR_Callback] FTB_Publish() failed with %d\n", ret);
            ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
            if (ret != -CR_ETEMPFAIL){
                MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
            }
            return(-1);
        }

        //ret = cr_ftb_wait_for_resp(nprocs);
        ret = cr_ftb_wait_for_resp(num_procs);
        if (ret) {
            MPIRUN_ERR("[CR_Callback] Error in getting a response\n");
            ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
            if (ret != -CR_ETEMPFAIL){
                MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
            }
            return(-2);
        }

        dbg("===  mpirun_rsh: have published CKPT event and get resp...\n");
        cr_ftb_finalize();
    }

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
	    ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
            if (ret != -CR_ETEMPFAIL){
                MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
            }
	    return (-1);
	}

	for (i = 0; i < NSPAWNS; i++) {

	    if (!FD_ISSET(mpirun_fd[i], &set))
		continue;

	    CR_MPDU_readline(mpirun_fd[i], buf, MAX_CR_MSG_LEN);

	    if (CR_MPDU_parse_keyvals(buf) < 0) {
		fprintf(stderr,
			"[CR_Callback] CR_MPDU_parse_keyvals() failed\n");
		ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
                if (ret != -CR_ETEMPFAIL){
                    MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
                }
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
		ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
                if (ret != -CR_ETEMPFAIL ){
                    MPIRUN_ERR_ABORT("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
                }
		return (-3);
	    }
	}
    }				/* while(Progressing) */

#endif				/* CR_FTB */

   ret = cr_checkpoint(CR_CHECKPOINT_READY);
   dbg("%s:%d: [CR_Callback] cr_checkpoint done\n",__FILE__,__LINE__);

    if (ret < 0)
    {
        MPIRUN_ERR("BLCR call cr_checkpoint() failed with error %d: %s\n",ret,cr_strerror(-ret));
        cr_state = CR_READY;
        return (-4);
    }
    else if (ret == 0) 
    {
#if defined(CKPT) && defined(CR_FTB)
        cr_ftb_init(num_procs);
#endif
	dbg("%s:%d: [CR_Callback] CR_READY\n",__FILE__,__LINE__);
	cr_state = CR_READY;
    }
    else if (ret) 
    {
        dbg("****  is restart...\n");
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
    if( ftb_init_done )  return 0;
    ftb_init_done = 1;

    memset(&ftb_cinfo, 0, sizeof(ftb_cinfo));
    strcpy(ftb_cinfo.client_schema_ver, "0.5");
    strcpy(ftb_cinfo.event_space, "FTB.STARTUP.MV2_MPIRUN");
    strcpy(ftb_cinfo.client_name, "MV2_MPIRUN");

    /* sessionid should be <= 16 bytes since client_jobid is 16 bytes. */
    snprintf(ftb_cinfo.client_jobid, FTB_MAX_CLIENT_JOBID, "%s",
	     sessionid);

    strcpy(ftb_cinfo.client_subscription_style, "FTB_SUBSCRIPTION_BOTH");
    ftb_cinfo.client_polling_queue_len = 64; //nprocs;

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

    snprintf(subscription_str, FTB_MAX_SUBSCRIPTION_STR,
             "event_space=FTB.STARTUP.MV2_MPISPAWN , jobid=%s", sessionid);
             //"event_space=FTB.STARTUP.MV2_MPISPAWN" );
    ret = FTB_Subscribe(&shandle, ftb_handle, subscription_str,
			cr_ftb_callback, NULL);

    /// subscribe to migration trigger
    snprintf(subscription_str, FTB_MAX_SUBSCRIPTION_STR, "event_space=FTB.MPI.MIG_TRIGGER");
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
    int ret=0;
    if (ftb_init_done){
        ftb_init_done = 0;
        ret = FTB_Unsubscribe(&shandle);
        usleep(20000);
	    ret = FTB_Disconnect(ftb_handle);
    }
    dbg("Has close FTB: ftb_init_done=%d, ftb-disconnect ret %d\n", ftb_init_done, ret ); 
}

static int cr_ftb_wait_for_resp(int nprocs)
{
    pthread_mutex_lock(&cr_ftb_ckpt_req_mutex);
    dbg("wait for nprocs %d \n", nprocs);
    cr_ftb_ckpt_req += nprocs;
    while( cr_ftb_ckpt_req > 0 )
        pthread_cond_wait(&cr_ftb_ckpt_req_cond, &cr_ftb_ckpt_req_mutex);
    pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);

    if (cr_ftb_ckpt_req == 0) {
        return (0);
    } else {
    	DBG(fprintf(stderr, "cr_ftb_wait_for_resp() returned %d\n", cr_ftb_ckpt_req));
   	return (-1);
    }
}

#ifdef CR_AGGRE
static int cr_ftb_aggre_based_mig(char* src)
{
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    char buf[MAX_CR_MSG_LEN];
    char tmpbuf[16];
    int i, ret;
    int isrc, itgt;
    char *tgt;
 
    tgt = sparehosts[sparehosts_idx];

    dbg("enter: src=%s, tgt=%s, tgt-idx=%d\n", src, tgt, sparehosts_idx );
    
    //// find src and tgt node idx
    for(i=0; i<pglist->npgs; i++){
        if (strcmp(pglist->data[i].hostname, src) == 0)
            isrc = i;
        if (strcmp(pglist->data[i].hostname, tgt) == 0)
            itgt = i;
    }

    snprintf(buf, MAX_CR_MSG_LEN, "%s %s %d ", src, tgt, pglist->data[isrc].npids);

    /// find all proc-ranks to be migrated
    for (i=0; i<pglist->data[isrc].npids; i++)
    {
        sprintf(tmpbuf, "%d ", pglist->data[isrc].plist_indices[i]);
        strncat(buf, tmpbuf, MAX_CR_MSG_LEN);
    }
    dbg("[Aggre-Based Mig]: init a mig: \"%s\"\n", buf);

    sparehosts_idx++;

    SET_EVENT(eprop, FTB_EVENT_NORMAL, buf);
    ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_MIGRATE), &eprop, &ehandle);
    if (ret != FTB_SUCCESS) {
        fprintf(stderr, "%s: FTB_Publish failed with %d\n", ret);
        return -1;
    }
    return 0; 
}
#endif

static int cr_ftb_callback(FTB_receive_event_t * revent, void *arg)
{
    FTB_event_properties_t eprop;
    FTB_event_handle_t     ehandle;
    char buf[MAX_CR_MSG_LEN];
    int ret, isrc, itgt, i;
    char cnum[16];
    process_group tmp_pg;
    dbg("Got event %s from %s: payload=\"%s\"\n",
             revent->event_name, revent->client_name, revent->event_payload);

    /* TODO: Do some sanity checking to see if this is the intended target */

    if (!strcmp(revent->event_name, EVENT(MPI_PROCS_CKPTED))) {
        pthread_mutex_lock(&cr_ftb_ckpt_req_mutex);
    	if (cr_ftb_ckpt_req <= 0) {
	        fprintf(stderr, "Got CR_FTB_CKPT_DONE but "
		        "cr_ftb_ckpt_req not set\n");
    	    cr_ftb_ckpt_req = -1;
    	    pthread_cond_signal(&cr_ftb_ckpt_req_cond);
            pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);
    	    return (0);
    	}
    	--cr_ftb_ckpt_req;
    	if (!cr_ftb_ckpt_req) {
    	    pthread_cond_signal(&cr_ftb_ckpt_req_cond);
    	    pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);
    	    return (0);
        }
        pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);
    }

    if( !strcmp(revent->event_name, EVENT(CR_FTB_RSRT_DONE)) ){
        dbg("a proc has been migrated/restarted...\n");        
        return 0; 
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_CKPT_FAIL))) {
        pthread_mutex_lock(&cr_ftb_ckpt_req_mutex);
        fprintf(stderr, "Got CR_FTB_CKPT_FAIL\n");
        cr_ftb_ckpt_req = -2;
        pthread_cond_signal(&cr_ftb_ckpt_req_cond);
        pthread_mutex_unlock(&cr_ftb_ckpt_req_mutex);
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

    if (!strcmp(revent->event_name, EVENT(CR_FTB_RTM))) 
    {
        if (sparehosts_on) 
        {
            if (sparehosts_idx >= nsparehosts) {
                fprintf(stderr, "[Migration] Out of Spares\n");
                return(0);
            }
    #ifdef CR_AGGRE
            if( use_aggre>0 && use_aggre_mig>0 ){
                ret = cr_ftb_aggre_based_mig(revent->event_payload);
                if( ret!= 0 ){
                    dbg("Err!!:: Aggre-mig mig failed...\n");
                }
                return 0;
            }
    #endif
            snprintf(buf, MAX_CR_MSG_LEN, "%s %s",
                     revent->event_payload, sparehosts[sparehosts_idx++]);

            dbg("[Migration]: init a mig: \"%s\"\n", buf);
            SET_EVENT(eprop, FTB_EVENT_NORMAL, buf);
            ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_MIGRATE), &eprop, &ehandle);
            if (ret != FTB_SUCCESS) {
                fprintf(stderr, "FTB_Publish(CR_FTB_MIGRATE) failed with %d\n", ret);
            }
        }
        else 
        {
            fprintf(stderr, "[Migration] Don't know about spare nodes\n");
        }

        return(0);
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_MIGRATE_PIC)))
    {

        /* Find Source & Target in the pglist */
        get_src_tgt(revent->event_payload, cr_mig_src_host, cr_mig_tgt_host);

        dbg(" src_tgt payload=%s, src=%s:tgt=%s\n", revent->event_payload,
                       cr_mig_src_host,cr_mig_tgt_host);

        for (i=0; i<pglist->npgs; i++)
        {
            if (strcmp(pglist->data[i].hostname, cr_mig_src_host) == 0)
                isrc = i;
            if (strcmp(pglist->data[i].hostname, cr_mig_tgt_host) == 0)
                itgt = i;
        }

        /* Get the list of ranks */
        buf[0] = '\0';
        for (i=0; i<pglist->data[isrc].npids; i++)
        {
            sprintf(cnum, "%d ", pglist->data[isrc].plist_indices[i]);
            strncat(buf, cnum, MAX_CR_MSG_LEN);
        }
        i = strlen(buf);
        if (buf[i-1] == ' ') buf[i-1] = '\0';

        dbg("list of procs to migrate: %s\n", buf);
#ifdef SPAWN_DEBUG
        pglist_print();
#endif
        /* Fixup the pglist */
        // swap
        const char *src_hostname = pglist->data[isrc].hostname;
        const char *tgt_hostname = pglist->data[itgt].hostname;
        pid_t src_pid =  pglist->data[isrc].pid;
        pid_t tgt_pid =  pglist->data[itgt].pid;
        pid_t   local_src = pglist->data[isrc].local_pid;
        pid_t   local_tgt = pglist->data[itgt].local_pid;

        memcpy(&tmp_pg, &pglist->data[isrc], sizeof(process_group));
        memcpy(&pglist->data[isrc], &pglist->data[itgt], sizeof(process_group));
        memcpy(&pglist->data[itgt], &tmp_pg, sizeof(process_group));

        pglist->data[isrc].hostname = src_hostname;
        pglist->data[itgt].hostname = tgt_hostname;
        pglist->data[isrc].pid = src_pid;
        pglist->data[itgt].pid = tgt_pid;
        pglist->data[isrc].local_pid = local_src;
        pglist->data[itgt].local_pid = local_tgt;
        //I need to change also the plist_indice[itgt].hostname
        int index;
        for (index = 0; index < pglist->data[itgt].npids; index++)
        {
            plist[pglist->data[itgt].plist_indices[index]].hostname = (char *)strdup(tgt_hostname);
        }

        dbg("mpirun_rsh: will do migrate...\n");
//#ifdef SPAWN_DEBUG
        pglist_print();
        dump_pgrps();
//#endif
        /* Copy checkpointed image */
        char syscmd[256];
        //sprintf(syscmd, "scp %s:%s.0* %s:/tmp/", cr_mig_src_host, ckpt_filename, cr_mig_tgt_host);
        char    ckptdir[256], *tp;
        strncpy(ckptdir, ckpt_filename, 256);
        ckptdir[255]=0;
        tp = ckptdir + strlen(ckptdir) - 1;
        while(*tp != '/' && tp>=ckptdir )   tp--;
        if( tp>=ckptdir )  *(tp+1)=0;
        sprintf(syscmd, "scp %s:%s.0* %s:%s", 
                cr_mig_src_host, ckpt_filename, cr_mig_tgt_host, ckptdir);
        dbg("  syscmd=%s\n", syscmd);
        system(syscmd);

        /* Initiate Phase II */
        dbg("move ckpt img complete...started phase II: send: \"%s\"\n", buf);
        SET_EVENT(eprop, FTB_EVENT_NORMAL, buf);
               //sleep(1); //sleep(100000);
        ret = FTB_Publish(ftb_handle, EVENT(CR_FTB_MIGRATE_PIIC),
                          &eprop, &ehandle);
        if (ret != FTB_SUCCESS) {
            fprintf(stderr, "[CR_Callback] FTB_Publish(CR_FTB_MIGRATE_PIIC) "
                                          "failed with %d\n", ret);
        }

        return(0);
    }

}

int read_sparehosts(char *hostfile, char ***hostarr, int *nhosts)
{

    FILE *fp;
    char line[HOSTFILE_LEN+1];
    int i, ret, line_len, n=0;
    char **hosts;

    if (!hostfile || !hostarr || !nhosts) {
        fprintf(stderr, "[read_sparehosts] Invalid Parameters\n");
        return(-1);
    }

    fp = fopen(hostfile, "r");
    if (!fp) goto err_fopen;

    /* Figure out the number of hosts */
    while(fgets(line, HOSTFILE_LEN, fp) != NULL) {

        line_len = strlen(line);
        if (line[line_len-1] == '\n')
            line[line_len-1] =  '\0';

        line_len = strlen(line);
        if (line_len == 0)  continue; /* Blank Lines */
        if (line[0] == '#') continue; /* Comments    */

        ++n;
    }

    *nhosts = n;

    hosts = (char **) malloc(n * sizeof(char *));
    if (!hosts) goto err_malloc_hosts;

    /* Reset File Pointer */
    rewind(fp);
    /* Store the list of hosts */
    n = 0;
    while(fgets(line, HOSTFILE_LEN, fp) != NULL) {

        line_len = strlen(line);
        if (line[line_len-1] == '\n')
            line[line_len-1] =  '\0';

        line_len = strlen(line);
        if (line_len == 0)  continue; /* Blank Lines */
        if (line[0] == '#') continue; /* Comments    */

        hosts[n] = (char *) malloc((line_len+1)*sizeof(char));
        if (!hosts[n]) goto err_malloc_hostn;

        strncpy(hosts[n], line, line_len+1);
        ++n;
    }

    *hostarr = hosts;

    ret = 0;

exit_malloc_hostn:
exit_malloc_hosts:
    fclose(fp);

exit_fopen:
    return(ret);

err_malloc_hostn:
    fprintf(stderr, "[read_sparehosts] Error allocating host[%d]\n", n);
    while (n > 0) {
        --n;
        free(hosts[n]);
    }
    free(hosts);
    ret = -4;
    goto exit_malloc_hostn;

err_malloc_hosts:
    fprintf(stderr, "[read_sparehosts] Error allocating hosts array\n");
    ret = -3;
    goto exit_malloc_hosts;

err_fopen:
    perror("[read_sparehosts] Error opening hostfile");
    ret = -2;
    goto exit_fopen;
}

/* FIXME: Need to fix possible overrun flaw */
static int get_src_tgt(char *str, char *src, char *tgt)
{
    int i, j, tgt_start;

    if (!str || !src || !tgt) return(-1);

    i = j = tgt_start = 0;

    while (str[i]) {

        if (str[i] == ' ') {
            tgt_start = 1;
            src[j] = '\0';
            j = 0;
            ++i;
            continue;
        }

        if (tgt_start)
            tgt[j++] = str[i++];
        else
            src[j++] = str[i++];
    }

    tgt[j] = '\0';

    return(0);
}
#endif				/* CR_FTB */

void finalize_ckpt()
{
    if (restart_context) {

	/* Wait for previous instance of CR_Loop to exit */
	if (cr_tid) {
        cr_work_can_exit = 1;
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
