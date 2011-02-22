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

#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <wait.h>
#include <string.h>
#include "mpirun_util.h"
#include "mpispawn_tree.h"
#include "pmi_tree.h"
#include "mpmd.h"
#include <math.h>
#include "mpirunconf.h"
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "crfs.h"

#ifdef MPISPAWN_DEBUG
#include <stdio.h>
#define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define debug(...) ((void)0)
#endif

#ifdef CKPT

#define CR_RESTART_CMD      "cr_restart"
#define MAX_CR_MSG_LEN      256
#define DEFAULT_MPIRUN_PORT 14678
#define DEFAULT_MPD_PORT    24678
#define CR_RSRT_PORT_CHANGE 16

#define DEFAULT_CHECKPOINT_FILENAME "/tmp/ckpt"
#define CR_MAX_FILENAME     128

#ifndef CR_FTB
static volatile int cr_cond = 0;
static pthread_t worker_tid;

static int *mpispawn_fd;
static int mpirun_fd;
static int mpispawn_port;
static volatile int cr_worker_can_exit = 0;
#endif

static int restart_context;
static int checkpoint_count;

static char *sessionid;
static char session_file[CR_MAX_FILENAME];

static char ckpt_filename[CR_MAX_FILENAME];

static int CR_Init(int);


#ifdef CR_FTB

#include <libftb.h>

#define FTB_MAX_SUBSCRIPTION_STR 128

/////////////////////////////////////////////////////////
    // max-event-name-len=32,  max-severity-len=16
#define CR_FTB_EVENT_INFO {               \
        {"CR_FTB_CHECKPOINT",    "info"}, \
        {"CR_FTB_MIGRATE",       "info"}, \
        {"CR_FTB_MIGRATE_PIIC",  "info"}, \
        {"CR_FTB_CKPT_DONE",     "info"}, \
        {"CR_FTB_CKPT_FAIL",     "info"}, \
        {"CR_FTB_RSRT_DONE",     "info"}, \
        {"CR_FTB_RSRT_FAIL",     "info"}, \
        {"CR_FTB_APP_CKPT_REQ",  "info"}, \
        {"CR_FTB_CKPT_FINALIZE", "info"}, \
        {"CR_FTB_MIGRATE_PIC",   "info"}, \
        {"CR_FTB_RTM",           "info"},  \
        {"MPI_PROCS_CKPTED", "info"},       \
        {"MPI_PROCS_CKPT_FAIL", "info"},    \
        {"MPI_PROCS_RESTARTED", "info"},    \
        {"MPI_PROCS_RESTART_FAIL", "info"}, \
        {"MPI_PROCS_MIGRATED", "info"},     \
        {"MPI_PROCS_MIGRATE_FAIL", "info"} \
}

    // Index into the Event Info Table
#define CR_FTB_CHECKPOINT    0
#define CR_FTB_MIGRATE       1
#define CR_FTB_MIGRATE_PIIC  2
#define CR_FTB_CKPT_DONE     3
#define CR_FTB_CKPT_FAIL     4
#define CR_FTB_RSRT_DONE     5
#define CR_FTB_RSRT_FAIL     6
#define CR_FTB_APP_CKPT_REQ  7
#define CR_FTB_CKPT_FINALIZE 8
#define CR_FTB_MIGRATE_PIC   9
#define CR_FTB_RTM           10
    // start of standard FTB MPI events
#define MPI_PROCS_CKPTED        11
#define MPI_PROCS_CKPT_FAIL     12
#define MPI_PROCS_RESTARTED     13
#define MPI_PROCS_RESTART_FAIL  14
#define MPI_PROCS_MIGRATED      15
#define MPI_PROCS_MIGRATE_FAIL 16

#define CR_FTB_EVENTS_MAX    17
////////////////////////////////////////////////////



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

static volatile int cr_mig_spare_cond = 0;
static volatile int cr_mig_src_can_exit = 0;
static int eNCHILD;
volatile int cr_mig_src = 0;
volatile int cr_mig_tgt = 0;
volatile int num_migrations = 0;
static char cr_mig_src_host[32];
static char cr_mig_tgt_host[32];

static int *migrank;
static char my_hostname[MAX_HOST_LEN];

/*struct spawn_info_s {
    char spawnhost[32];
    int  sparenode;
};*/
struct spawn_info_s *spawninfo;
int exclude_spare;
static FTB_client_t        ftb_cinfo;
static FTB_client_handle_t ftb_handle;
static FTB_event_info_t    cr_ftb_events[] = CR_FTB_EVENT_INFO;
static FTB_subscribe_handle_t shandle;
static int ftb_init_done;

static int cr_ftb_init(char *);
static void cr_ftb_finalize();
static int cr_ftb_callback(FTB_receive_event_t *, void *);
static int get_src_tgt(char *, char *, char *);
static int get_tgt_rank_list(char *, int *, int **);

static pthread_t CR_wfe_tid;
static void* CR_wait_for_errors(void *);
static int done_mtpmi_processop = 0;
#else /* ! CR_FTB */

static void *CR_Worker(void *);
static int Connect_MPI_Procs(int);

extern char *CR_MPDU_getval(const char *, char *, int);
extern int CR_MPDU_parse_keyvals(char *);
extern int CR_MPDU_readline(int, char *, int);
extern int CR_MPDU_writeline(int, char *);

#endif /* CR_FTB */

#ifdef SPAWN_DEBUG
#define DBG(_stmt_) _stmt_;
#else
#define DBG(_stmt_)

#endif
static int cr_spawn_degree;

#ifdef CR_AGGRE
static int use_aggre_mig=0; // whether we enable migration func in CRFS
static int use_aggre = 0;
extern char crfs_mig_filename[128];
#endif

#endif				/* CKPT */

#define DBG(_stmt_)
typedef struct {
    char *viadev_device;
    char *viadev_default_port;
    char *mpirun_rank;
} lvalues;

process_info_t *local_processes;
size_t npids = 0;

int N;
int MPISPAWN_HAS_PARENT;
int MPISPAWN_NCHILD;
int checkin_sock;
int mt_id;
int USE_LINEAR_SSH;
int NCHILD;
int *mpispawn_fds;
int NCHILD_INCL;
int ROOT_FD;
int **ranks;
int mpirun_socket;
pid_t *mpispawn_pids;

static in_port_t c_port;
child_t *children;

static struct timeval  rst_begin, rst_end; // keep track of time spent to restart

//#define dbg(fmt, args...)   do{ \
//    fprintf(stderr, "%s: [spawn_%d]: "fmt, __func__, mt_id, ##args );fflush(stderr);} while(0)
#define dbg(fmt, args...) 

void cleanup(void);

void mpispawn_abort(int abort_code)
{
    int sock, id = env2int("MPISPAWN_ID");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int connect_attempt = 0, max_connect_attempts = 5;
    struct sockaddr_in sockaddr;
    struct hostent *mpirun_hostent;
    if (sock < 0) {
	/* Oops! */
	perror("socket");
	exit(EXIT_FAILURE);
    }

    mpirun_hostent = gethostbyname(env2str("MPISPAWN_MPIRUN_HOST"));
    if (NULL == mpirun_hostent) {
	/* Oops! */
	herror("gethostbyname");
	exit(EXIT_FAILURE);
    }

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
    sockaddr.sin_port = htons(env2int("MPISPAWN_CHECKIN_PORT"));

    while (connect(sock, (struct sockaddr *) &sockaddr, sizeof(sockaddr)) <
	   0) {
	if (++connect_attempt > max_connect_attempts) {
	    perror("connect");
	    exit(EXIT_FAILURE);
	}
    }
    if (sock) {
	write_socket(sock, &abort_code, sizeof(int));
	write_socket(sock, &id, sizeof(int));
	close(sock);
    }
    dbg("Will abort now, code=%d...\n", abort_code);
    cleanup();
}

lvalues get_lvalues(int i)
{
    lvalues v;
    char *buffer = NULL;
    if (USE_LINEAR_SSH) {
	buffer = mkstr("MPISPAWN_MPIRUN_RANK_%d", i);
	if (!buffer) {
	    fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
		    __LINE__);
	    exit(EXIT_FAILURE);
	}

	v.mpirun_rank = env2str(buffer);
	free(buffer);
    } else
	v.mpirun_rank = mkstr("%d", ranks[mt_id][i]);
    return v;
}

void setup_global_environment()
{
    char my_host_name[MAX_HOST_LEN + MAX_PORT_LEN];

    int i = env2int("MPISPAWN_GENERIC_ENV_COUNT");

    setenv("MPIRUN_MPD", "0", 1);
    setenv("MPIRUN_NPROCS", getenv("MPISPAWN_GLOBAL_NPROCS"), 1);
    setenv("MPIRUN_ID", getenv("MPISPAWN_MPIRUN_ID"), 1);
    setenv("MV2_NUM_NODES_IN_JOB", getenv("MPISPAWN_NNODES"), 1);

    /* Ranks now connect to mpispawn */
    gethostname(my_host_name, MAX_HOST_LEN);

    sprintf(my_host_name, "%s:%d", my_host_name, c_port);

    setenv("PMI_PORT", my_host_name, 2);

    if (env2int("MPISPAWN_USE_TOTALVIEW")) {
	setenv("USE_TOTALVIEW", "1", 1);
    } else {
	setenv("USE_TOTALVIEW", "0", 1);
    }

    while (i--) {
	char *buffer, *name, *value;

	buffer = mkstr("MPISPAWN_GENERIC_NAME_%d", i);
	if (!buffer) {
	    fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
		    __LINE__);
	    exit(EXIT_FAILURE);
	}

	name = env2str(buffer);
	if (!name) {
	    fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
		    __LINE__);
	    exit(EXIT_FAILURE);
	}

	free(buffer);

	buffer = mkstr("MPISPAWN_GENERIC_VALUE_%d", i);
	if (!buffer) {
	    fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
		    __LINE__);
	    exit(EXIT_FAILURE);
	}

	value = env2str(buffer);
	if (!value) {
	    fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
		    __LINE__);
	    exit(EXIT_FAILURE);
	}
#ifdef CKPT
#ifndef CR_AGGRE
	if (strcmp(name, "MV2_CKPT_FILE") == 0)
	    strncpy(ckpt_filename, value, CR_MAX_FILENAME);
#endif
#endif				/* CKPT */

	setenv(name, value, 1);

	free(name);
	free(value);
    }
}

void setup_local_environment(lvalues lv)
{
    setenv("PMI_ID", lv.mpirun_rank, 1);

#ifdef CKPT
    setenv("MV2_CKPT_FILE", ckpt_filename, 1);
    setenv("MV2_CKPT_SESSIONID", sessionid, 1);

    /* Setup MV2_CKPT_MPD_BASE_PORT for legacy reasons */
    setenv("MV2_CKPT_MPD_BASE_PORT", "0", 1);
#ifdef CR_AGGRE
    dbg("========  ckpt-file=%s, mig-file= %s\n", ckpt_filename, crfs_mig_filename);
#endif
#endif

}

void spawn_processes(int n)
{
    char my_host_name[MAX_HOST_LEN + MAX_PORT_LEN];
    gethostname (my_host_name, MAX_HOST_LEN);
    int i, j;
    npids = n;
    local_processes = (process_info_t *) malloc(process_info_s * n);

    if (!local_processes) {
	perror("malloc");
	exit(EXIT_FAILURE);
    }

    DBG(fprintf(stderr, "[%s]%s:%d:\n",my_host_name,__FILE__,__LINE__));

#ifdef CKPT
#ifdef CR_FTB
       int cached_cr_mig_tgt = cr_mig_tgt;
       cr_mig_tgt = 0;
       if( cached_cr_mig_tgt ){
               gettimeofday(&rst_begin,NULL); // begin timing
               dbg("begin restart at %d.%d sec\n", rst_begin.tv_sec,rst_begin.tv_usec);
       }
#endif
#endif

    for (i = 0; i < n; i++) {
	local_processes[i].pid = fork();
        DBG(fprintf(stderr, "[%s]%s:%d:\n",my_host_name,__FILE__,__LINE__));
	if (local_processes[i].pid == 0) {

#ifdef CKPT
	    char **cr_argv;
	    char str[32];
	    int rank;
            DBG(fprintf(stderr, "[%s]%s:%d :i=%d\n",my_host_name,__FILE__,__LINE__,i));
 	    if (restart_context) {

                DBG(fprintf(stderr, "[%s]%s:%d: i=%d: restart-context\n",my_host_name,__FILE__,__LINE__,i));
                restart_context = 0;
		cr_argv = (char **) malloc(sizeof(char *) * 3);
		if (!cr_argv) {
		    perror("malloc(cr_argv)");
		    exit(EXIT_FAILURE);
		}

		cr_argv[0] =
		    malloc(sizeof(char) * (strlen(CR_RESTART_CMD) + 1));
		if (!cr_argv[0]) {
		    perror("malloc(cr_argv[0])");
		    exit(EXIT_FAILURE);
		}
		strcpy(cr_argv[0], CR_RESTART_CMD);

		cr_argv[1] = malloc(sizeof(char) * CR_MAX_FILENAME);
		if (!cr_argv[1]) {
		    perror("malloc(cr_argv[1])");
		    exit(EXIT_FAILURE);
		}
#ifdef CR_FTB
        if (cached_cr_mig_tgt)
        {
            DBG(fprintf(stderr,"%s:%d:[spawn_tgt:%d]\n",__FILE__,__LINE__, cr_mig_tgt));
    #ifdef CR_AGGRE
            if( use_aggre && use_aggre_mig )
            {   //use Fuse-mig-fs mnt point as ckpt_filename
                snprintf(cr_argv[1], CR_MAX_FILENAME,"%s.0.%d", crfs_mig_filename, migrank[i]);
                int tmpfd = open(cr_argv[1], O_RDWR|O_CREAT, 0644 );
                close(tmpfd);
            }
            else //simple strategy for migration
    #endif
            snprintf(cr_argv[1], CR_MAX_FILENAME,"%s.0.%d", ckpt_filename, migrank[i]);

            rank = migrank[i];
        } else
#endif
        {

		snprintf(str, 32, "MPISPAWN_MPIRUN_RANK_%d", i);
		rank = atoi(getenv(str));
		snprintf(cr_argv[1], CR_MAX_FILENAME, "%s.%d.%d",ckpt_filename, checkpoint_count, rank);
        }
		cr_argv[2] = NULL;
        dbg(" -------- restart arg: %s %s\n\n",cr_argv[0], cr_argv[1]  );
		execvp(CR_RESTART_CMD, cr_argv);

		perror("[CR Restart] execvp");
		fflush(stderr);
#if defined(CKPT) && defined(CR_AGGRE)
        //if( use_aggre )
        //    stop_crfs();
#endif
		exit(EXIT_FAILURE);
	    }
#endif
	    int argc, nwritten;
	    char **argv, buffer[80];
	    lvalues lv = get_lvalues(i);

	    setup_local_environment(lv);

	    argc = env2int("MPISPAWN_ARGC");


	    argv = malloc(sizeof(char *) * (argc + 1));
	    if (!argv) {
		fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
			__LINE__);
		exit(EXIT_FAILURE);
	    }

	    argv[argc] = NULL;
	    j = argc;

	    while (argc--) {
            nwritten = snprintf(buffer, 80, "MPISPAWN_ARGV_%d", argc);
            if (nwritten < 0 || nwritten > 80) {
                fprintf(stderr, "%s:%d Overflow\n", __FILE__,
                    __LINE__);
                exit(EXIT_FAILURE);
		}


		/* if executable is not in working directory */
		if (argc == 0 && getenv("MPISPAWN_BINARY_PATH")) {
		    char *tmp = env2str(buffer);
		    if (tmp[0] != '/') {
			snprintf(buffer, 80, "%s/%s",
				 getenv("MPISPAWN_BINARY_PATH"), tmp);
		    }
		    free(tmp);
		    argv[argc] = strdup(buffer);
		} else
		    argv[argc] = env2str(buffer);
	    }

	    /*Check if the executable is in the working directory*/
	    char *tmp_argv =  strdup(argv[0]);
	    if (tmp_argv[0] != '.' && tmp_argv[0] != '/') {
		 char * tmp = malloc(sizeof(char *) * (strlen(argv[0]) + 2));;
	         sprintf(tmp, "%s%s","./",argv[0]);
	         if ( access(tmp,F_OK) == 0 )
	             argv[0] = strdup(tmp);

	         free(tmp);
	     }
	    free(tmp_argv);

        dbg("will run MPI proc: %s %s\n", argv[0], argv[1]);
	    execvp(argv[0], argv);
	    perror("execvp");

	    for (i = 0; i < j; i++) {
	        fprintf(stderr, "%s ", argv[i]);
	    }

	    fprintf(stderr, "\n");
#if defined(CKPT) && defined(CR_AGGRE)
        //if( use_aggre )
        //    stop_crfs();
#endif
	    exit(EXIT_FAILURE);
	} else {

	    char *buffer;
	    buffer = mkstr("MPISPAWN_MPIRUN_RANK_%d", i);
	    if (!buffer) {
		fprintf(stderr, "%s:%d Insufficient memory\n", __FILE__,
			__LINE__);
		exit(EXIT_FAILURE);
	    }
	    local_processes[i].rank = env2int(buffer);
	    free(buffer);
	}
    }
////////////////
#ifdef CR_FTB  
    if( cached_cr_mig_tgt ){
        gettimeofday(&rst_end,NULL); // begin timing
        int tp = (rst_end.tv_sec - rst_begin.tv_sec)*1000 + 
                 (rst_end.tv_usec - rst_begin.tv_usec)/1000;
        dbg("Restart cost time %d msec\n", tp );
    }
#endif
///////////////

}

void cleanup(void)
{

#ifdef CKPT
#ifdef CR_FTB
    cr_ftb_finalize();
#else
    cr_worker_can_exit = 1;
    pthread_kill(worker_tid, SIGTERM);
    pthread_join(worker_tid, NULL);
#endif  
    unlink(session_file);
#ifdef CR_AGGRE
    if( use_aggre ){
        stop_crfs();
    }
#endif 
#endif

    int i;
    for (i = 0; i < npids; i++) {
	kill(local_processes[i].pid, SIGINT);
    }
    if (!USE_LINEAR_SSH)
	for (i = 0; i < MPISPAWN_NCHILD; i++) {
	    kill(mpispawn_pids[i], SIGINT);
	}

    sleep(1);

    for (i = 0; i < npids; i++) {
	kill(local_processes[i].pid, SIGTERM);
    }
    if (!USE_LINEAR_SSH)
	for (i = 0; i < MPISPAWN_NCHILD; i++) {
	    kill(mpispawn_pids[i], SIGTERM);
	}

    sleep(1);

    for (i = 0; i < npids; i++) {
	kill(local_processes[i].pid, SIGKILL);
    }
    if (!USE_LINEAR_SSH)
	for (i = 0; i < MPISPAWN_NCHILD; i++) {
	    kill(mpispawn_pids[i], SIGKILL);
	}

    free(local_processes);
    free(children);
    exit(EXIT_FAILURE);
}

void cleanup_handler(int sig)
{
    mpispawn_abort(MPISPAWN_PROCESS_ABORT);
}

void child_handler(int signal)
{
    static int num_exited = 0;
    int status, pid, rank, i;
    char my_host_name[MAX_HOST_LEN];
    gethostname (my_host_name, MAX_HOST_LEN);
                           
    rank = mt_id;
    DBG(fprintf (stderr, " XXX mpispawn child_handler: rank=%d, host=%s got"
		"signal %d\n", rank, my_host_name, signal));
    while (1) {
        pid = waitpid(-1, &status, WNOHANG);
        //dbg(" Got child-sig: pid=%d,errno=%d, num_exited=%d\n", pid, errno, num_exited);
        if (pid == 0)
	        return;
        if ( ( ( pid > 0 ) && WIFEXITED (status) && WEXITSTATUS (status) == 0)
                           ||( (pid < 0) && (errno == ECHILD) ) )
        {
            if(++num_exited == npids)
            {

#ifdef CKPT
#ifdef CR_FTB
                FTB_event_properties_t eprop;
                FTB_event_handle_t     ehandle;
                if (cr_mig_src)
                {
        #ifdef CR_AGGRE
                    if( use_aggre && use_aggre_mig){
                        // I'm src in aggregation-based migration
                        cr_mig_src_can_exit = 1;
                        return;
                    }
        #endif
                    //cr_mig_src = 0;
                    snprintf(my_host_name, MAX_HOST_LEN, "%s %s", cr_mig_src_host, cr_mig_tgt_host);
                    SET_EVENT(eprop, FTB_EVENT_NORMAL,my_host_name);
                    dbg(" at %s: Sending out CR_FTB_MIGRATE_PIC\n",cr_mig_src_host);
                    ///////////
                    status = FTB_Publish(ftb_handle,EVENT(CR_FTB_MIGRATE_PIC),&eprop, &ehandle);
                    cr_mig_src_can_exit = 1;
                    ///////////////////////
                    return;
                }
#endif

				unlink(session_file);
#ifdef CR_AGGRE
                if( use_aggre ){
                    dbg("At child_handler: will stop crfs\n");
                    stop_crfs();
                }
#endif
        #ifdef CR_FTB
                dbg("At %s: Will close FTB...\n", my_host_name);
                cr_ftb_finalize();
        #endif
#endif
	            dbg(" XXX will exit..:  rank=%d,host=%s: EXIT_SUCCESS\n", rank, my_host_name);
                ///////////////////////
				exit(EXIT_SUCCESS);
			}
		} else {
		    rank = -1;
		    gethostname(my_host_name, MAX_HOST_LEN);
			for (i = 0; i < npids; i++) {
				if (pid == local_processes[i].pid) {
					rank = local_processes[i].rank;
				}
		    }
		    if (rank != -1) {
				fprintf(stderr, "MPI process (rank: %d) terminated "
					"unexpectedly on %s\n", rank, my_host_name);
		    } else {
				fprintf(stderr, "Process terminated "
					"unexpectedly on %s\n", my_host_name);
		    }
		    fflush(stderr);
			mpispawn_abort(MPISPAWN_PROCESS_ABORT);
		}
    }
}

void mpispawn_checkin(in_port_t l_port)
{
    int connect_attempt = 0, max_connect_attempts = 5, i, sock;
    struct hostent *mpirun_hostent;
    struct sockaddr_in sockaddr;
    /*struct sockaddr_in c_sockaddr; */
    int offset = 0, id;
    pid_t pid = getpid();
    int port;

    if (!USE_LINEAR_SSH) {
	if (mt_id != 0) {
	    offset = 1;
	    MPISPAWN_HAS_PARENT = 1;
	}
	mpispawn_fds = (int *) malloc(sizeof(int) * (MPISPAWN_NCHILD +
						     MPISPAWN_HAS_PARENT));
	if (MPISPAWN_NCHILD) {
	    mpispawn_pids =
		(pid_t *) malloc(sizeof(pid_t) * MPISPAWN_NCHILD);
	    for (i = 0; i < MPISPAWN_NCHILD; i++) {
		while ((sock = accept(checkin_sock, NULL, 0)) < 0) {
		    if (errno == EINTR || errno == EAGAIN)
			continue;
		    perror("accept [mt_checkin]");
		}
		mpispawn_fds[i + offset] = sock;
		if (read_socket(sock, &id, sizeof(int)) ||
		    read_socket(sock, &mpispawn_pids[i],
				sizeof(pid_t)) || read_socket(sock, &port,
							      sizeof
							      (in_port_t)))
		{
		    cleanup();
		}

	    }
	}
    }
    mpirun_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!USE_LINEAR_SSH && mt_id != 0)
	mpispawn_fds[0] = mpirun_socket;
    if (mpirun_socket < 0) {
	perror("socket");
	exit(EXIT_FAILURE);
    }

    mpirun_hostent = gethostbyname(getenv("MPISPAWN_MPIRUN_HOST"));
    if (mpirun_hostent == NULL) {
	herror("gethostbyname");
	exit(EXIT_FAILURE);
    }

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
    sockaddr.sin_port = htons(env2int("MPISPAWN_CHECKIN_PORT"));

    while (connect(mpirun_socket, (struct sockaddr *) &sockaddr,
		   sizeof(sockaddr)) < 0) {
	if (++connect_attempt > max_connect_attempts) {
	    perror("connect [mt_checkin]");
	    exit(EXIT_FAILURE);
	}
    }

    if (write_socket(mpirun_socket, &mt_id, sizeof(int))) {
	fprintf(stderr, "Error writing id [%d]!\n", mt_id);
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    if (write_socket(mpirun_socket, &pid, sizeof(pid_t))) {
	fprintf(stderr, "Error writing pid [%d]!\n", pid);
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    if (write_socket(mpirun_socket, &l_port, sizeof(in_port_t))) {
	fprintf(stderr, "Error writing l_port!\n");
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    if (USE_LINEAR_SSH
	&& !(mt_id == 0 && env2int("MPISPAWN_USE_TOTALVIEW")))
	close(mpirun_socket);
}

in_port_t init_listening_socket(int *mc_socket)
{
    struct sockaddr_in mc_sockaddr;
    socklen_t mc_sockaddr_len = sizeof(mc_sockaddr);

    *mc_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (*mc_socket < 0) {
	perror("socket");
	exit(EXIT_FAILURE);
    }

    mc_sockaddr.sin_addr.s_addr = INADDR_ANY;
    mc_sockaddr.sin_port = 0;

    if (bind(*mc_socket, (struct sockaddr *) &mc_sockaddr, mc_sockaddr_len)
	< 0) {
	perror("bind");
	exit(EXIT_FAILURE);
    }

    if (getsockname(*mc_socket, (struct sockaddr *) &mc_sockaddr,
		    &mc_sockaddr_len) < 0) {
	perror("getsockname");
	exit(EXIT_FAILURE);
    }

    listen(*mc_socket, MT_MAX_DEGREE);

    return mc_sockaddr.sin_port;
}

void wait_for_errors(int s, struct sockaddr *sockaddr, unsigned int
		     sockaddr_len)
{
    int wfe_socket, wfe_abort_code, wfe_abort_rank, wfe_abort_msglen;

    char my_host_name[MAX_HOST_LEN];
           gethostname (my_host_name, MAX_HOST_LEN);
       //fprintf (stderr, "hostname %s wait_for_errors:inside :\n",my_host_name);

  WFE:
    while ((wfe_socket = accept(s, sockaddr, &sockaddr_len)) < 0) {
	if (errno == EINTR || errno == EAGAIN)
	    continue;
	perror("accept");
	mpispawn_abort(MPISPAWN_RANK_ERROR);
    }

    if (read_socket(wfe_socket, &wfe_abort_code, sizeof(int))
	|| read_socket(wfe_socket, &wfe_abort_rank, sizeof(int))
	|| read_socket(wfe_socket, &wfe_abort_msglen, sizeof(int))) {
	fprintf(stderr, "Termination socket read failed!\n");
	mpispawn_abort(MPISPAWN_RANK_ERROR);
    } else {
	char wfe_abort_message[wfe_abort_msglen];
	fprintf(stderr, "Abort signaled by rank %d: ", wfe_abort_rank);
	if (!read_socket(wfe_socket, &wfe_abort_message, wfe_abort_msglen))
	    fprintf(stderr, "%s\n", wfe_abort_message);
	mpispawn_abort(MPISPAWN_RANK_ERROR);
    }
    goto WFE;
}


/*Obtain the host_ist from a file. This function is used when the number of
 * processes is beyond the threshold. */
char *obtain_host_list_from_file()
{

    //Obtain id of the host file and number of byte to read
    //Number of bytes sent when it is used the file approach to exachange
    //the host_list
    int num_bytes;
    FILE *fp;
    char *host_list_file = NULL, *host_list = NULL;

    host_list_file = env2str("HOST_LIST_FILE");
    num_bytes = env2int("HOST_LIST_NBYTES");

    fp = fopen(host_list_file, "r");
    if (fp == NULL) {

	fprintf(stderr, "host list temp file could not be read\n");
    }

    host_list = malloc(num_bytes);
    fscanf(fp, "%s", host_list);
    fclose(fp);
    return host_list;
}


fd_set child_socks;
#define MPISPAWN_PARENT_FD mpispawn_fds[0]
#define MPISPAWN_CHILD_FDS (&mpispawn_fds[MPISPAWN_HAS_PARENT])
#define ENV_CMD		    "/usr/bin/env"
#define MAX_HOST_LEN 256

int c_socket;
struct sockaddr_in c_sockaddr, checkin_sockaddr;
unsigned int sockaddr_len = sizeof (c_sockaddr);

extern char **environ;

static void    dump_fds()
{
    int i;

    dbg("  has-parent = %d, MT_CHILD=%d,  NCHILD=%d\n", 
        MPISPAWN_HAS_PARENT, MPISPAWN_NCHILD, NCHILD );

    if( MPISPAWN_HAS_PARENT ){
        dbg("   parent-fd=%d\n", MPISPAWN_PARENT_FD);
    }
    
    for(i=0; i<MPISPAWN_NCHILD; i++ ){
        dbg("   MT_CHILD_%d: fd=%d\n", i, MPISPAWN_CHILD_FDS[i]);
    }     
    for(i=0; i<NCHILD; i++){
        dbg("   NCLD_%d:  fd=%d\n", i, children[i].fd );
    }
}


int main(int argc, char *argv[])
{
    struct sigaction signal_handler;
    int l_socket, i;
    in_port_t l_port = init_listening_socket(&l_socket);

    int mt_degree, mt_nnodes;
    char *portname;
    int target, n, j, k;
    char *host_list;
    char *nargv[7];
    int nargc;
    char **host;
    int *np;
    char *command, *args, *mpispawn_env = NULL;
    char hostname[MAX_HOST_LEN];
    int port;

    FD_ZERO(&child_socks);

    mt_id = env2int("MPISPAWN_ID");
    mt_nnodes = env2int("MPISPAWN_NNODES");
    USE_LINEAR_SSH = env2int("USE_LINEAR_SSH");

    NCHILD = env2int("MPISPAWN_LOCAL_NPROCS");
    N = env2int("MPISPAWN_GLOBAL_NPROCS");
    children = (child_t *) malloc(NCHILD * child_s);

    portname = getenv("PARENT_ROOT_PORT_NAME");
    if (portname) {
	add_kvc("PARENT_ROOT_PORT_NAME", portname, 1);
    }

    gethostname(hostname, MAX_HOST_LEN);

#if defined(CKPT) && defined(CR_AGGRE)
    char* str = getenv("MV2_CKPT_USE_AGGREGATION");
    if ( str == NULL ) {
        // Use default value, ie 1 since aggregation has been enabled at configure time
        use_aggre = 1;
    } else {
        // Use the value forwarded by mpirun_rsh in MV2_CKPT_USE_AGGREGATION
        use_aggre = atoi( str );
    }
    dbg(" *********  [mpispawn-%d]: use-aggre=%d, use-aggre-mig=%d\n", 
             mt_id, use_aggre, use_aggre_mig);

    if( getenv("MV2_CKPT_FILE") ){
        strncpy(ckpt_filename, getenv("MV2_CKPT_FILE"), CR_MAX_FILENAME); 
    } else {
        strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME ); 
    }

    if ( use_aggre || use_aggre_mig ) {
        // Check for fusermount
        int status = system("fusermount -V > /dev/null");
        if ( status == -1 ) {
            // The 'system' call failed (because of failed fork, missing sh, ...)
            // This is a serious error, aborting
            fprintf(stderr, "[%s] Fatal error: Failed to call 'system()'\n", hostname );
            exit(EXIT_FAILURE);
        } else {
            if ( !(WIFEXITED(status) && WEXITSTATUS(status) == 0) ) {
                // Debug information
                if ( WIFEXITED(status) ) {
                    dbg("'sh -c fusermount -V' exited with status %d\n", WEXITSTATUS(status));
                } else if ( WIFSIGNALED(status) ) {
                    dbg("'sh -c fusermount -V' terminated with signal %d\n", WTERMSIG(status));
                } else {
                    dbg("fail to execute 'sh -c fusermount -V' process for an unknown reason\n");
                }

                // Failed to run 'fusermount -V', disabling aggregation and RDMA migration
                fprintf(stderr, "[%s] Cannot enable Write Aggregation for Checkpoint/Restart. Aborting...\n", hostname );
                fprintf(stderr, "[%s] Please check for 'fusermount' in your PATH.\n", hostname );
                fprintf(stderr, "[%s] To disable Write Aggregation, use MV2_CKPT_USE_AGGREGATION=0.\n", hostname );
                exit(EXIT_FAILURE);
            }
        }
    }

    if( !use_aggre ){
        //strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME ); 
        goto no_cr_aggre;
    }
    
    if( start_crfs(getenv("MPISPAWN_CR_SESSIONID") , ckpt_filename, use_aggre_mig )!= 0 )
    {
        fprintf(stderr, "[%s] Failed to initialize Write Aggregation for Checkpoint/Restart. Aborting...\n", hostname );
        fprintf(stderr, "[%s] Please check that the fuse module is loaded on this node.\n", hostname );
        fprintf(stderr, "[%s] To disable Write Aggregation, use MV2_CKPT_USE_AGGREGATION=0.\n", hostname );
        exit(EXIT_FAILURE);
    }
    debug("Now, ckptname is: %s\n", ckpt_filename );
no_cr_aggre:
#endif

    signal_handler.sa_handler = cleanup_handler;
    sigfillset(&signal_handler.sa_mask);
    signal_handler.sa_flags = 0;

    sigaction(SIGHUP, &signal_handler, NULL);
    sigaction(SIGINT, &signal_handler, NULL);
    sigaction(SIGTERM, &signal_handler, NULL);

    signal_handler.sa_handler = child_handler;
    sigemptyset(&signal_handler.sa_mask);

    sigaction(SIGCHLD, &signal_handler, NULL);

    /* Create listening socket for ranks */
    /* Doesn't need to be TCP as we're all on local node */
    c_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c_socket < 0) {
	perror("socket");
	exit(EXIT_FAILURE);
    }
    c_sockaddr.sin_addr.s_addr = INADDR_ANY;
    c_sockaddr.sin_port = 0;

    if (bind(c_socket, (struct sockaddr *) &c_sockaddr, sockaddr_len) < 0) {
	perror("bind");
	exit(EXIT_FAILURE);
    }
    if (getsockname
	(c_socket, (struct sockaddr *) &c_sockaddr, &sockaddr_len) < 0) {
	perror("getsockname");
	exit(EXIT_FAILURE);
    }
    listen(c_socket, NCHILD);
    c_port = (int) ntohs(c_sockaddr.sin_port);

#ifdef CKPT
    CR_Init(NCHILD);
#endif

    n = atoi(argv[1]);

    if (!USE_LINEAR_SSH) {
	checkin_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (checkin_sock < 0) {
	    perror("socket");
	    exit(EXIT_FAILURE);
	}
	checkin_sockaddr.sin_addr.s_addr = INADDR_ANY;
	checkin_sockaddr.sin_port = 0;
	if (bind(checkin_sock, (struct sockaddr *) &checkin_sockaddr,
		 sockaddr_len) < 0) {
	    perror("bind");
	    exit(EXIT_FAILURE);
	}
	if (getsockname
	    (checkin_sock, (struct sockaddr *) &checkin_sockaddr,
	     &sockaddr_len) < 0) {
	    perror("getsockname");
	    exit(EXIT_FAILURE);
	}
	port = (int) ntohs(checkin_sockaddr.sin_port);
	listen(checkin_sock, 64);
	char *mpmd_on = env2str("MPISPAWN_MPMD");
	nargc = env2int("MPISPAWN_NARGC");
	for (i = 0; i < nargc; i++) {
	    char buf[20];
	    sprintf(buf, "MPISPAWN_NARGV_%d", i);
	    nargv[i] = env2str(buf);
	}


	host_list = env2str("MPISPAWN_HOSTLIST");


	//If the number of processes is beyond or equal the PROCS_THRES it
	//receives the host list in a file
	if (host_list == NULL) {
	    host_list = obtain_host_list_from_file();
	}


	command = mkstr("cd %s; %s", env2str("MPISPAWN_WD"), ENV_CMD);

	mpispawn_env = mkstr("MPISPAWN_MPIRUN_HOST=%s"
			     " MPISPAWN_CHECKIN_PORT=%d MPISPAWN_MPIRUN_PORT=%d",
			     hostname, port, port);

	i = 0;
	while (environ[i] != NULL) {
	    char *var, *val;
	    char *dup = strdup(environ[i]);
	    var = strtok(dup, "=");
	    val = strtok(NULL, "=");
	    if (val &&
		0 != strcmp(var, "MPISPAWN_ID") &&
		0 != strcmp(var, "MPISPAWN_LOCAL_NPROCS") &&
		0 != strcmp(var, "MPISPAWN_MPIRUN_HOST") &&
		0 != strcmp(var, "MPISPAWN_CHECKIN_PORT") &&
		0 != strcmp(var, "MPISPAWN_MPIRUN_PORT")) {

		if (strchr(val, ' ') != NULL) {
		    mpispawn_env =
			mkstr("%s %s='%s'", mpispawn_env, var, val);

		} else {
		    /*If mpmd is selected the name and args of the executable are written in the HOST_LIST, not in the
		     * MPISPAWN_ARGV and MPISPAWN_ARGC. So the value of these varibles is not exact and we don't
		     * read this value.*/
		    if (mpmd_on) {
			if (strstr(var, "MPISPAWN_ARGV_") == NULL
			    && strstr(var, "MPISPAWN_ARGC") == NULL) {

			    mpispawn_env =
				mkstr("%s %s=%s", mpispawn_env, var, val);
			}
		    } else
			mpispawn_env =
			    mkstr("%s %s=%s", mpispawn_env, var, val);
		}
	    }

	    free(dup);
	    i++;
	}

	args = mkstr("%s", argv[0]);
	for (i = 1; i < argc - 1; i++) {
	    args = mkstr("%s %s", args, argv[i]);
	}
	nargv[nargc + 2] = NULL;

	host = (char **) malloc(mt_nnodes * sizeof(char *));
	np = (int *) malloc(mt_nnodes * sizeof(int));
	ranks = (int **) malloc(mt_nnodes * sizeof(int *));
	/* These three variables are used to collect information on name, args and number of args in case of mpmd */
	char **exe = (char **) malloc(mt_nnodes * sizeof(char *));
	char **args_exe = (char **) malloc(mt_nnodes * sizeof(char *));
	int *num_args = (int *) malloc(mt_nnodes * sizeof(int));

	i = mt_nnodes;
	j = 0;

	while (i > 0) {
	    if (i == mt_nnodes)
		host[j] = strtok(host_list, ":");
	    else
		host[j] = strtok(NULL, ":");
	    np[j] = atoi(strtok(NULL, ":"));
	    ranks[j] = (int *) malloc(np[j] * sizeof(int));
	    for (k = 0; k < np[j]; k++) {
		ranks[j][k] = atoi(strtok(NULL, ":"));
	    }
	    /*If mpmd is selected the executable name and the arguments are written in the hostlist.
	     * So we need to read these information from the hostlist.*/
	    if (mpmd_on) {
		exe[j] = strtok(NULL, ":");
		num_args[j] = atoi(strtok(NULL, ":"));
		if (num_args[j] > 1) {
		    k = 0;
		    char *arg_tmp = NULL;
		    while (k < num_args[j] - 1) {
			if (k == 0)
			    arg_tmp = strtok(NULL, ":");
			else
			    arg_tmp =
				mkstr("%s:%s", arg_tmp, strtok(NULL, ":"));

			k++;

		    }
		    args_exe[j] = strdup(arg_tmp);
		}
	    }

	    i--;
	    j++;
	}


	/* Launch mpispawns */

	while (n > 1) {

	    target = mt_id + ceil(n / 2.0);
	    /*If mpmd is selected we need to add the MPISPAWN_ARGC and MPISPAWN_ARGV to the mpispwan
	     * environment using the information we have read in the host_list.*/
	    if (mpmd_on) {
		//We need to add MPISPAWN_ARGV
		mpispawn_env =
		    mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env,
			  num_args[target]);
		mpispawn_env =
		    mkstr("%s MPISPAWN_ARGV_0=%s", mpispawn_env,
			  exe[target]);
		char **tmp_arg = tokenize(args_exe[target], ":");

		for (i = 0; i < num_args[target] - 1; i++)
		{
		    mpispawn_env =
			mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env,
			      i + 1, tmp_arg[i]);

		}
	    }

	    nargv[nargc] = host[target];

	    MPISPAWN_NCHILD++;
	    if (0 == fork()) {
		mpispawn_env =
		    mkstr("%s MPISPAWN_ID=%d MPISPAWN_LOCAL_NPROCS=%d",
			  mpispawn_env, target, np[target]);
		command =
		    mkstr("%s %s %s %d", command, mpispawn_env, args,
			  n / 2);

		nargv[nargc + 1] = command;

		execv(nargv[0], (char *const *) nargv);
		perror("execv");
	    } else
		n = ceil(n / 2.0);
	}

    }
    /* if (!USE_LINEAR_SSH) */
    setup_global_environment();

    if (chdir(getenv("MPISPAWN_WORKING_DIR"))) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    mpispawn_checkin(l_port);

    if (USE_LINEAR_SSH) {
#ifdef CR_FTB
        mt_degree = MT_MAX_DEGREE;
#else /* !defined(CR_FTB) */
        mt_degree = ceil(pow(mt_nnodes, (1.0 / (MT_MAX_LEVEL - 1))));

        if (mt_degree < MT_MIN_DEGREE) {
            mt_degree = MT_MIN_DEGREE;
        }

        if (mt_degree > MT_MAX_DEGREE) {
            mt_degree = MT_MAX_DEGREE;
        }
#endif /* !defined(CR_FTB) */

#ifdef CKPT
        mpispawn_fds = mpispawn_tree_init (mt_id, mt_degree, mt_nnodes,
                l_socket);
        if (mpispawn_fds == NULL) {
            exit (EXIT_FAILURE);
        }
    }

    mtpmi_init();
#else
    }
#endif

#ifdef CKPT
    cr_spawn_degree = mt_degree;
    dbg("mt_degree=%d\n", mt_degree);
    if (!NCHILD) {
        goto skip_spawn_processes;
    }

spawn_processes:
#endif
    spawn_processes(NCHILD);

    for (i = 0; i < NCHILD; i++) 
	{
        int sock;
        ACCEPT_HID:
        sock = accept(c_socket, (struct sockaddr *) &c_sockaddr, &sockaddr_len);
        if (sock < 0) {
	        printf("%d", errno);
            if ((errno == EINTR) || (errno == EAGAIN)) {
                goto ACCEPT_HID;
	        } else {
				perror("accept");
				return (EXIT_FAILURE);
			}
		}
		children[i].fd = sock;
		children[i].rank = 0;
		children[i].c_barrier = 0;
        dbg("has accept() child_%d of %d: fd=%d\n", i, NCHILD, sock ); 
    }

skip_spawn_processes:

#ifdef CKPT
#ifdef CR_FTB
    dump_fds();
	if (! done_mtpmi_processop )
	{
#endif
#else
    if (USE_LINEAR_SSH) {
        mpispawn_fds = mpispawn_tree_init (mt_id, mt_degree, mt_nnodes,
                l_socket);
        if (mpispawn_fds == NULL) {
            exit (EXIT_FAILURE);
        }
    }

    mtpmi_init();
#endif
        mtpmi_processops ();
        dbg(" ====  after mtpmi_processops...\n");

#if defined(CKPT) && defined(CR_FTB)
	}
	else
	{ 
       DBG(fprintf(stderr, "%s:%d:%s skipped mtpmi_processops \n",__FILE__,__LINE__,my_hostname));
	}
	// done_mtpmi_processop = 1;
respawn_processes: //come back here for respawing again for subsequent restart process migration

    if (cr_mig_tgt) {
           while(!cr_mig_spare_cond);
       // cr_mig_tgt = 0;
           cr_mig_spare_cond = 0;
           NCHILD = eNCHILD;
           restart_context = 1;
           dbg("host %s: mig-tgt: NCHILD=%d, Jump to spawn_processes\n",my_hostname, NCHILD );
           //fflush(stdout);
           goto spawn_processes;
    }
    /*else if( cr_mig_src ) {
        while( !cr_mig_src_can_exit ) usleep(100000); 
        dbg("host %s: on mig-src, will exit...\n", my_hostname );
        // now, child MPI-proc has finished,can exit this mpispawn
        cr_mig_src_can_exit = 0;
        //cr_mig_src = 0;
        cleanup();
        return EXIT_FAILURE;  
    } */  
	//   Spawn wait_for_error_thread 
    if (pthread_create(&CR_wfe_tid, NULL, CR_wait_for_errors, NULL))
    {
        perror("[main:mpispawn] pthread_create()");
        exit(EXIT_FAILURE);
    }
    // Wait for Connect_MPI_Procs() to start listening
    dbg("has created wait_for_err thr, cr_mig_tgt=%d...\n", cr_mig_tgt);
    do{ sleep(1); }
    while(!cr_mig_tgt && num_migrations>0);
    // At src of migration. Keep idle till mpirun_rsh tells me to stop

    int ret = 0;
    dbg("%s pthread_cancel wfe_thread\n",my_hostname);
    ret =  pthread_cancel(CR_wfe_tid);
       
    pthread_join(CR_wfe_tid, NULL);
    dbg("%s: ******  will exit now... \n",my_hostname);
    //goto respawn_processes;
    return EXIT_FAILURE;

#else

    wait_for_errors(c_socket, (struct sockaddr *) &c_sockaddr,
		    sockaddr_len);
#if defined(CKPT) && defined(CR_AGGRE)
    if( use_aggre ){
        debug("%s: will stop crfs\n", __func__);
        stop_crfs();
    }
#endif
#endif
    return EXIT_FAILURE;
}

#ifdef CKPT

static void * CR_wait_for_errors(void *arg)
{
       DBG(fprintf(stderr,"%s:%s:%d: CR_wait_for_errors\n",my_hostname,
__FILE__,__LINE__));
   
       wait_for_errors (c_socket, (struct sockaddr *) &c_sockaddr,
                                        sockaddr_len);

       DBG(fprintf(stderr,"%s:%s:%d: CR_wait_for_errors done\n",my_hostname,
__FILE__,__LINE__));
}

#endif

#ifdef CKPT

static int CR_Init(int nProcs)
{
    char *temp;
    struct hostent *hp;
    struct sockaddr_in sa;

    int mpirun_port;

    temp = getenv("MPISPAWN_MPIRUN_CR_PORT");
    if (temp) {
	mpirun_port = atoi(temp);
    } else {
	fprintf(stderr, "[CR_Init] MPISPAWN_MPIRUN_CR_PORT unknown\n");
	exit(EXIT_FAILURE);
    }

    temp = getenv("MPISPAWN_CR_CONTEXT");
    if (temp) {
	restart_context = atoi(temp);
    } else {
	fprintf(stderr, "[CR_Init] MPISPAWN_CR_CONTEXT unknown\n");
	exit(EXIT_FAILURE);
    }

    sessionid = getenv("MPISPAWN_CR_SESSIONID");
    if (!sessionid) {
	fprintf(stderr, "[CR_Init] MPISPAWN_CR_SESSIONID unknown\n");
	exit(EXIT_FAILURE);
    }

    snprintf(session_file, CR_MAX_FILENAME, "/tmp/cr.session.%s",
	     sessionid);

    temp = getenv("MPISPAWN_CR_CKPT_CNT");
    if (temp) {
	checkpoint_count = atoi(temp);
    } else {
	fprintf(stderr, "[CR_Init] MPISPAWN_CR_CKPT_CNT unknown\n");
	exit(EXIT_FAILURE);
    }

#ifndef CR_AGGRE
    strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME);
#endif

#ifdef CR_FTB

    char pmi_port[MAX_HOST_LEN + MAX_PORT_LEN];
    char hostname[MAX_HOST_LEN];
    FILE *fp;

    /* Get PMI Port information */
    gethostname(hostname, MAX_HOST_LEN);
    sprintf(pmi_port, "%s:%d", hostname, c_port);

    /* Create the session file with PMI Port information */
    fp = fopen(session_file, "w+");
    if (!fp) {
	fprintf(stderr, "[CR_Init] Cannot create Session File\n");
	fflush(stderr);
	exit(EXIT_FAILURE);
    }
    else
    {
       DBG(fprintf(stderr, ":%s: [CR_Init  Created Session File=%s\n",hostname,
session_file));
    }
    if (fwrite(pmi_port, sizeof(pmi_port), 1, fp) == 0) {
	fprintf(stderr, "[CR_Init] Cannot write PMI Port number\n");
	fflush(stderr);
	exit(EXIT_FAILURE);
    }
    dbg("write pmi-port= %s to session-file %s\n", pmi_port, session_file );
    fclose(fp);

    if (cr_ftb_init(sessionid))
        exit(EXIT_FAILURE);

#else

    /* Connect to mpirun_rsh */
    mpirun_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mpirun_fd < 0) {
	perror("[CR_Init] socket()");
	exit(EXIT_FAILURE);
    }

    hp = gethostbyname(getenv("MPISPAWN_MPIRUN_HOST"));
    if (!hp) {
	perror("[CR_Init] gethostbyname()");
	exit(EXIT_FAILURE);
    }

    bzero((void *) &sa, sizeof(sa));
    bcopy((void *) hp->h_addr, (void *) &sa.sin_addr, hp->h_length);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(mpirun_port);

    if (connect(mpirun_fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
	perror("[CR_Init] connect()");
	exit(EXIT_FAILURE);
    }

    mpispawn_fd = malloc(nProcs * sizeof(int));
    if (!mpispawn_fd) {
	perror("[CR_Init] malloc()");
	exit(EXIT_FAILURE);
    }

    /* Spawn CR Worker Thread */
    cr_worker_can_exit = 0;
    if (pthread_create
	(&worker_tid, NULL, CR_Worker, (void *) (uintptr_t) nProcs)) {
	perror("[CR_Init] pthread_create()");
	exit(EXIT_FAILURE);
    }

    /* Wait for Connect_MPI_Procs() to start listening */
    while (!cr_cond);

#endif				/* CR_FTB */

    return (0);
}

#ifdef CR_FTB

static int cr_ftb_init(char *sessionid)
{
    static int cnt=0;
    dbg(" ----- now init, cnt=%d\n", ++cnt);
    if( ftb_init_done ) return;
    ftb_init_done = 1;

    int  ret;
    char *subscription_str;

    memset(&ftb_cinfo, 0, sizeof(ftb_cinfo));
    strcpy(ftb_cinfo.client_schema_ver, "0.5");
    strcpy(ftb_cinfo.event_space, "FTB.STARTUP.MV2_MPISPAWN");
    strcpy(ftb_cinfo.client_name, "MV2_MPISPAWN");

    gethostname(my_hostname, MAX_HOST_LEN);

    /* sessionid should be <= 16 bytes since client_jobid is 16 bytes. */
    snprintf(ftb_cinfo.client_jobid, FTB_MAX_CLIENT_JOBID, "%s", sessionid);

    strcpy(ftb_cinfo.client_subscription_style, "FTB_SUBSCRIPTION_BOTH");

    ret = FTB_Connect(&ftb_cinfo, &ftb_handle);
    if (ret != FTB_SUCCESS) goto err_connect;

    ret = FTB_Declare_publishable_events(ftb_handle, NULL,
                                         cr_ftb_events, CR_FTB_EVENTS_MAX);
    if (ret != FTB_SUCCESS) goto err_declare_events;

    subscription_str = malloc(sizeof(char) * FTB_MAX_SUBSCRIPTION_STR);
    if (!subscription_str) goto err_malloc;

    snprintf(subscription_str, FTB_MAX_SUBSCRIPTION_STR,
             "event_space=FTB.STARTUP.MV2_MPIRUN , jobid=%s", sessionid);

    ret = FTB_Subscribe(&shandle, ftb_handle, subscription_str,
                        cr_ftb_callback, NULL);
    free(subscription_str);
    if (ret != FTB_SUCCESS) goto err_subscribe;

    ftb_init_done = 1;
    return(0);

err_connect:
    fprintf(stderr, "FTB_Connect() failed with %d\n", ret);
    ret = -1;
    goto exit_connect;

err_declare_events:
    fprintf(stderr, "FTB_Declare_publishable_events() failed with %d\n", ret);
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
    return(ret);
}

static void cr_ftb_finalize()
{
    static int cnt=0;

    dbg("finalize cnt=%d...\n", ++cnt);
    if (ftb_init_done){
        ftb_init_done= 0;
        FTB_Unsubscribe(&shandle);
        usleep(20000);
        FTB_Disconnect(ftb_handle);
    }
    ftb_init_done= 0;
}

#ifdef CR_AGGRE
/// we have started a proc-migration using Aggregation-based strategy
static int cr_ftb_aggre_based_mig(char* msg)
{
    int i,j;
    int num_mig_procs = 0;
    int tmp_rank[32];  // assume: not more than 32 pids in one run
    char    buf[256];

    strncpy(buf, msg, 255);
    buf[255] = 0;
    dbg("enter with buf = \"%s\"\n", buf);

    /// "buf" is in format:  "srcnode  tgtnode  proc_cnt  procid1  procid2 ..."
    // parse this string to extract all infor
    char *tok;
    
    tok = strtok(buf, " \n\t"); // src
    strcpy(cr_mig_src_host, tok);
    tok = strtok(NULL, " \n\t"); // tgt
    strcpy(cr_mig_tgt_host, tok);
    tok = strtok(NULL, " \n\t"); // proc-count
    num_mig_procs = atoi(tok);
    
    if (strstr(my_hostname, cr_mig_src_host) == my_hostname)
        cr_mig_src = 1;
    if (strstr(my_hostname, cr_mig_tgt_host) == my_hostname)
        cr_mig_tgt = 1;
    dbg(" src=%s, tgt=%s, mig-src=%d, mig-tgt=%d, num_procs = %d\n", 
        cr_mig_src_host, cr_mig_tgt_host, cr_mig_src, cr_mig_tgt, num_mig_procs );

    if( num_mig_procs <= 0 ){
        fprintf(stderr, "[mpispanw_%d]: %s: procs to be migrated wrong:: %s\n", 
            mt_id, __func__, tok);
        return -1;
    }

    if( cr_mig_tgt )
    {    
        migrank = (int*)malloc(num_mig_procs*sizeof(int));
        if( !migrank ){
            fprintf(stderr, "[mpispawn_%d]: %s: malloc migrank failed\n", 
                mt_id, __func__ );
            return -1;
        }

        tok = strtok(NULL, " \n\t"); // proc-count
        i = 0;
        while( tok )
        {
            if( i>= num_mig_procs){
                fprintf(stderr, "[mpispawn_%d]: %s: too many proc-ranks: %d\n", 
                            mt_id, __func__, i );
                free(migrank);
                migrank = NULL;
                return -1;
            }
            migrank[i] = atoi(tok);
            i++;
            tok = strtok(NULL, " \n\t");
        }
    }

    // adjust MPISPAWN_Tree's topology: num of MPISPAWN-child nodes not to
    // participate in a barrier
    for (i=0; i<MPISPAWN_NCHILD; i++)
    {
        dbg("[mt_id %d on %s] spawninfo[%d].spawnhost %s\n",
                mt_id,my_hostname,i,spawninfo[i].spawnhost);
        j = mt_id*cr_spawn_degree+i+1;
        dbg("[mt_id %d on %s] j spawninfo[%d].spawnhost %s\n",
                mt_id,my_hostname,j,spawninfo[j].spawnhost);
        if (!strcmp(spawninfo[j].spawnhost, cr_mig_src_host))
        {
            --exclude_spare;
            dbg("[%d on %s] --exclude_spare %d\n", mt_id,my_hostname,exclude_spare);
        }
        if (!strcmp(spawninfo[j].spawnhost, cr_mig_tgt_host))
        {
            ++exclude_spare;
            dbg("[%d on %s] ++exclude_spare %d\n", mt_id,my_hostname,exclude_spare);
        }
    }
   
    if (!cr_mig_tgt) return(0);
 
    //// if I'm target node, start new process now... 
    eNCHILD = num_mig_procs;
    children = (child_t *) malloc (eNCHILD * sizeof(child_t));
    cr_mig_spare_cond = 1;

    return(0);
}
#endif

static int cr_ftb_callback(FTB_receive_event_t *revent, void *arg)
{
    int i, j;
// char my_hostname[256];
// gethostname(my_hostname, 255);

    dbg( "  at %s: Got event %s from %s:payload=\"%s\"\n",  my_hostname, revent->event_name,
            revent->client_name,revent->event_payload);
    //fflush(stdout);

    /* TODO: Do some sanity checking to see if this is the intended target */

    if (!strcmp(revent->event_name, EVENT(CR_FTB_MIGRATE)))
    {
        num_migrations++;
#ifdef CR_AGGRE
         if( use_aggre && use_aggre_mig ){
            i = cr_ftb_aggre_based_mig(revent->event_payload);
            dbg("Aggre-based Mig: ret %d\n", i);
            return 0;
        }
#endif
        /* Arm source & target for Migration */
        get_src_tgt(revent->event_payload, cr_mig_src_host, cr_mig_tgt_host);
        if (strstr(my_hostname, cr_mig_src_host) == my_hostname)
            cr_mig_src = 1;
        if (strstr(my_hostname, cr_mig_tgt_host) == my_hostname)
            cr_mig_tgt = 1;
        dbg(" src=%s, tgt=%s, mig-src=%d, mig-tgt=%d\n", 
            cr_mig_src_host, cr_mig_tgt_host, cr_mig_src, cr_mig_tgt );
        return(0);
    }

    if (!strcmp(revent->event_name, EVENT(CR_FTB_MIGRATE_PIIC)))
    {
        char my_hostname[256];
        gethostname(my_hostname, 255);
        //int has_src = 0;
        //int has_tgt = 0;
        /* Adjust exclude_spares based on new process distribution */
        for (i=0; i<MPISPAWN_NCHILD; i++)
        {
            dbg("[mt_id %d on %s] spawninfo[%d].spawnhost %s\n",
                    mt_id,my_hostname,i,spawninfo[i].spawnhost);
            j = mt_id*cr_spawn_degree+i+1;
            dbg("[mt_id %d on %s] j spawninfo[%d].spawnhost %s\n",
                    mt_id,my_hostname,j,spawninfo[j].spawnhost);
            if (!strcmp(spawninfo[j].spawnhost, cr_mig_src_host))
            {
                //has_src = 1;
                --exclude_spare;
                dbg("[%d on %s] --exclude_spare %d\n",
                    mt_id,my_hostname,exclude_spare);
            }
            if (!strcmp(spawninfo[j].spawnhost, cr_mig_tgt_host))
            {
                ++exclude_spare;
                dbg("[%d on %s] ++exclude_spare %d\n",
                    mt_id,my_hostname,exclude_spare);
                //has_tgt =1;
            }

        }

        dbg( " [mpispawn:%s] cr_mig_tgt=%d\n", my_hostname, cr_mig_tgt);
        //fflush(stdout);
        if (!cr_mig_tgt) return(0);

        /* Setup environment for process launch if target */
        get_tgt_rank_list(revent->event_payload, &eNCHILD, &migrank);
        children = (child_t *) malloc (eNCHILD * child_s);
        cr_mig_spare_cond = 1;
        return(0);
    }
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
static int get_tgt_rank_list(char *str, int *n, int **lst)
{
    int i, j, ci, *list;
    char cnum[16];

    if (!str || !n || !lst) return(-1);

    /* Find number of ranks */
    ci = i = 0;
    while (str[i]) {
        if (str[i] == ' ') ++ci;
        ++i;
    }
    ++ci;
    *n = ci;

    list = (int *) malloc((*n) * sizeof(int));
    if (!list) {
        fprintf(stderr, "[get_tgt_rank_list] malloc failed\n");
        return(-1);
    }

    i = j = ci = 0;
    while (str[i]) {

        if (str[i] == ' ') {
            cnum[ci] = '\0'; ci = 0;
            list[j++] = atoi(cnum);
            ++i;
            continue;
        }

        cnum[ci++] = str[i++];
    }
    cnum[ci] = '\0';
    list[j++] = atoi(cnum);

    *lst = list;

    return(0);
}

#else /* To be used only when not relying on FTB */


static int Connect_MPI_Procs(int nProcs)
{
    int i;
    FILE *fp;
    int mpispawn_listen_fd;
    struct sockaddr_in sa;

    mpispawn_listen_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mpispawn_listen_fd < 0) {
	perror("[Connect_MPI_Procs] socket()");
	exit(EXIT_FAILURE);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = 0;

    if (bind(mpispawn_listen_fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
	perror("[Connect_MPI_Procs] bind()");
	exit(EXIT_FAILURE);
    }

    i = sizeof(sa);
    if (getsockname
	(mpispawn_listen_fd, (struct sockaddr *) &sa,
	 (socklen_t *) & i) < 0) {
	perror("[Connect_MPI_Procs] getsockname()");
	close(mpispawn_listen_fd);
	exit(EXIT_FAILURE);
    }

    mpispawn_port = ntohs(sa.sin_port);

    fp = fopen(session_file, "w+");
    if (!fp) {
	fprintf(stderr,
		"[Connect_MPI_Procs] Cannot create Session File\n");
	fflush(stderr);
	close(mpispawn_listen_fd);
	exit(EXIT_FAILURE);
    }
    if (fwrite(&mpispawn_port, sizeof(mpispawn_port), 1, fp) == 0) {
	fprintf(stderr, "[Connect_MPI_Procs] Cannot write Session Id\n");
	fflush(stderr);
	close(mpispawn_listen_fd);
	exit(EXIT_FAILURE);
    }
    fclose(fp);

    if (listen(mpispawn_listen_fd, nProcs) < 0) {
	perror("[Connect_MPI_Procs] listen()");
	exit(EXIT_FAILURE);
    }

    /* Signal CR_Init() that you are listening */
    cr_cond = 1;

    for (i = 0; i < nProcs; i++) {
    if ((mpispawn_fd[i] = accept(mpispawn_listen_fd, 0, 0)) < 0) {
        if( errno==EINTR || errno==EAGAIN ){
            i--;
            debug("%s: error::  errno=%d\n", __func__,  errno);
            continue;
        }
	    perror("[Connect_MPI_Procs] accept()");
	    exit(EXIT_FAILURE);
	}
    }

    close(mpispawn_listen_fd);

    return (0);
}

static void *CR_Worker(void *arg)
{
    int ret, i, nProcs;
    char cr_msg_buf[MAX_CR_MSG_LEN];
    fd_set set;
    int max_fd;

    nProcs = (int) (uintptr_t) arg;

    Connect_MPI_Procs(nProcs);
    struct timeval tv;
    int ready = 0;
    dbg("after connect-MPI-procs: \n");

    while (1) {

	FD_ZERO(&set);
	FD_SET(mpirun_fd, &set);
	max_fd = mpirun_fd;

	for (i = 0; i < nProcs; i++) {
	    FD_SET(mpispawn_fd[i], &set);
	    max_fd = (max_fd >= mpispawn_fd[i]) ? max_fd : mpispawn_fd[i];
	}

	max_fd += 1;

        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ready = select(max_fd, &set, NULL, NULL, &tv);
        if ( ready == 0 ){
           // time out, or interrupted
            if(cr_worker_can_exit){
                dbg("will exit CR_Worker()...\n");
                break;
            }
            continue;
                
        } else if ( ready < 0 ) {
            dbg("select has returned %d: %s", ready, strerror(errno) );
            continue;
        }

	if (FD_ISSET(mpirun_fd, &set)) {

	    /* We need to send a message from mpirun_rsh -> MPI Processes */

	    ret = CR_MPDU_readline(mpirun_fd, cr_msg_buf, MAX_CR_MSG_LEN);
	    if (!ret)
		continue;
	    for (i = 0; i < nProcs; i++)
		CR_MPDU_writeline(mpispawn_fd[i], cr_msg_buf);

	} else {

	    /* We need to send a message from MPI Processes -> mpirun_rsh */

	    for (i = 0; i < nProcs; i++) {
		if (FD_ISSET(mpispawn_fd[i], &set))
		    break;
	    }

	    ret =
		CR_MPDU_readline(mpispawn_fd[i], cr_msg_buf,
				 MAX_CR_MSG_LEN);
	    if (!ret)
		continue;

	    /* Received a PMI Port Query */
	    if (strstr(cr_msg_buf, "query_pmi_port")) {
		snprintf(cr_msg_buf, MAX_CR_MSG_LEN,
			 "cmd=reply_pmi_port val=%s\n",
			 getenv("PMI_PORT"));
		CR_MPDU_writeline(mpispawn_fd[i], cr_msg_buf);
		continue;
	    }

	    CR_MPDU_writeline(mpirun_fd, cr_msg_buf);

	    /* Received a Finalize Checkpoint message */
	    if (strstr(cr_msg_buf, "finalize_ckpt")) {
		return (0);
	    }

	}

    }				/* while(1) */

}

#endif				/* not CR_FTB */

#endif				/* CKPT */

/* vi:set sw=4 sts=4 tw=80: */
