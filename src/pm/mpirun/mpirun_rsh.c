/*RAM
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2002-2009, The Ohio State University. All rights
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

#include "mpirunconf.h"
#include "mpirun_rsh.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <math.h>
#include "mpispawn_tree.h"
#include "mpirun_util.h"

#if defined(_NSIG)
#define NSIG _NSIG
#endif							/* defined(_NSIG) */

#ifdef CKPT

#include <sys/time.h>
#include <libcr.h>
#include <pthread.h>

static pthread_t       cr_tid;
static cr_client_id_t  cr_id;
static pthread_mutex_t cr_lock;

static pthread_spinlock_t flock;
static int fcnt;

#define CR_ERRMSG_SZ 64
static char cr_errmsg[CR_ERRMSG_SZ];

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

static int restart_context;
static int cached_restart_context;

static void *CR_Loop(void *);
static int   CR_Callback(void *);

extern char *CR_MPDU_getval(const char *, char *, int);
extern int   CR_MPDU_parse_keyvals(char *);
extern int   CR_MPDU_readline(int , char *, int);
extern int   CR_MPDU_writeline(int , char *);

typedef enum {
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

static int enable_sync_ckpt;
static int checkpoint_count;
static char sessionid[CR_SESSION_MAX];
static int checkpoint_interval;
static int max_save_ckpts;
static int max_ckpts;

static char ckpt_filename[CR_MAX_FILENAME];

int *mpirun_fd;
int mpirun_port;

#endif /* CKPT */

void spawn_one (int argc, char *argv[], char *totalview_cmd, char *env);

process_groups *pglist = NULL;
process *plist = NULL;
int nprocs = 0;
int aout_index, port;
char *wd;						/* working directory of current process */
char *custpath, *custwd;
#define MAX_HOST_LEN 256
char mpirun_host[MAX_HOST_LEN];	/* hostname of current process */
/* xxx need to add checking for string overflow, do this more carefully ... */
char *mpispawn_param_env = NULL;
int param_count = 0, legacy_startup = 0, dpm = 0;
int USE_LINEAR_SSH = 1;         /* By default, use linear ssh. Enable 
                                   -fastssh for tree based ssh */
int NSPAWNS;
char *spawnfile, *dpmenv;
int dpmenvlen;
#define ENV_LEN 1024
#define MAXLINE 1024
#define LINE_LEN 256
#define TMP_PFX "/tmp/tempfile_"
#define END     "endcmd"
#define END_s   strlen(END)
#define PORT    "PARENT_ROOT_PORT_NAME"
#define PORT_s  strlen(PORT)
#define ARG     "arg"
#define ARG_s   strlen(ARG)
#define ENDARG  "endarg"
#define ENDARG_s strlen(ENDARG)
#define INFN    "info_num="
#define INFN_s  strlen(INFN)


struct spawn_info_t {
	int totspawns;
	int spawnsdone;
	int dpmtot;
	int dpmindex;
	int launch_num;
	char buf[MAXLINE];
	char linebuf[MAXLINE];
	char runbuf[MAXLINE];
	char argbuf[MAXLINE];
	char *spawnfile;
};
struct spawn_info_t spinf;

/*
 * Message notifying user of what timed out
 */
static const char *alarm_msg = NULL;

void free_memory (void);
void pglist_print (void);
void pglist_insert (const char *const, const int);
void rkill_fast (void);
void rkill_linear (void);
void spawn_fast (int, char *[], char *, char *);
void spawn_linear (int, char *[], char *, char *);
void cleanup_handler (int);
void nostop_handler (int);
void alarm_handler (int);
void child_handler (int);
void usage (void);
void cleanup (void);
char *skip_white (char *s);
int read_param_file (char *paramfile, char **env);
void wait_for_mpispawn (int s, struct sockaddr_in *sockaddr,
						unsigned int sockaddr_len);
int set_fds (fd_set * rfds, fd_set * efds);
static int read_hostfile (char *hostfile_name);
void make_command_strings (int argc, char *argv[], char *totalview_cmd,
						   char *command_name, char *command_name_tv);
void mpispawn_checkin (int, struct sockaddr *, unsigned int);
void handle_spawn_req (int readsock);
void launch_newmpirun (int total);
static void get_line (void *buf, char *fill, int buf_or_file);
static void store_info(char *key, char *val);
static int check_info(char *str);
void dpm_add_env(char *, char *);

#if defined(USE_RSH)
int use_rsh = 1;
#else							/* defined(USE_RSH) */
int use_rsh = 0;
#endif							/* defined(USE_RSH) */

#define SH_NAME_LEN	(128)
char sh_cmd[SH_NAME_LEN];

//#define SPAWN_DEBUG
#ifdef SPAWN_DEBUG
#define DBG(_stmt_) _stmt_;
#else
#define DBG(_stmt_)
#endif

static struct option option_table[] = {
	{"np", required_argument, 0, 0},
	{"debug", no_argument, 0, 0},
	{"xterm", no_argument, 0, 0},
	{"hostfile", required_argument, 0, 0},
	{"paramfile", required_argument, 0, 0},
	{"show", no_argument, 0, 0},
	{"rsh", no_argument, 0, 0},
	{"ssh", no_argument, 0, 0},
	{"help", no_argument, 0, 0},
	{"v", no_argument, 0, 0},
	{"tv", no_argument, 0, 0},
	{"legacy", no_argument, 0, 0},
	{"startedByTv", no_argument, 0, 0},
	{"spawnfile", required_argument, 0, 0},
	{"dpm", no_argument, 0, 0},
    {"fastssh", no_argument, 0, 0},
	{0, 0, 0, 0}
};

#if !defined(HAVE_GET_CURRENT_DIR_NAME)
char *get_current_dir_name ()
{
	struct stat64 dotstat;
	struct stat64 pwdstat;
	char *pwd = getenv ("PWD");

	if (pwd != NULL
		&& stat64 (".", &dotstat) == 0
		&& stat64 (pwd, &pwdstat) == 0
		&& pwdstat.st_dev == dotstat.st_dev
		&& pwdstat.st_ino == dotstat.st_ino) {
		/* The PWD value is correct. */
		return strdup (pwd);
	}

	size_t size = 1;
	char *buffer;

	for (;; ++size) {
		buffer = malloc (size);

		if (!buffer) {
			return NULL;
		}

		if (getcwd (buffer, size) == buffer) {
			break;
		}

		free (buffer);

		if (errno != ERANGE) {
			return NULL;
		}
	}

	return buffer;
}
#endif							/* !defined(HAVE_GET_CURRENT_DIR_NAME) */

#if !defined(HAVE_STRNDUP)
char *strndup (const char *s, size_t n)
{
	size_t len = strlen (s);

	if (n < len) {
		len = n;
	}

	char *result = malloc (len + 1);

	if (!result) {
		return NULL;
	}

	result[len] = '\0';
	return memcpy (result, s, len);
}
#endif							/* !defined(HAVE_STRNDUP) */

int debug_on = 0, xterm_on = 0, show_on = 0;
int param_debug = 0;
int use_totalview = 0;
int server_socket;
char display[200];
char *binary_dirname;
char *binary_name;
int use_dirname = 1;
int hostfile_on = 0;
#define HOSTFILE_LEN 256
char hostfile[HOSTFILE_LEN + 1];

static void get_display_str ()
{
	char *p;
	char str[200];

	if ((p = getenv ("DISPLAY")) != NULL) {
		strcpy (str, p);		/* For X11 programs */
		sprintf (display, "DISPLAY=%s", str);
	}
}

/* Start mpirun_rsh totalview integration */

#define MPIR_DEBUG_SPAWNED                1
#define MPIR_DEBUG_ABORTING               2

struct MPIR_PROCDESC *MPIR_proctable = 0;
int MPIR_proctable_size = 0;
int MPIR_i_am_starter = 1;
int MPIR_debug_state = 0;
char *MPIR_dll_name = "MVAPICH2";
char *MV2_XRC_FILE;
/* Totalview intercepts MPIR_Breakpoint */
int MPIR_Breakpoint (void)
{
	return 0;
}

/* End mpirun_rsh totalview integration */

int main (int argc, char *argv[])
{
	int i, s, c, option_index;
	int paramfile_on = 0;
#define PARAMFILE_LEN 256
	char paramfile[PARAMFILE_LEN + 1];
	char *param_env;
	struct sockaddr_in sockaddr;
	unsigned int sockaddr_len = sizeof (sockaddr);

	char *env = "\0";
	int num_of_params = 0;

	char totalview_cmd[200];
	char *tv_env;

	int timeout, fastssh_threshold;

	atexit (free_memory);

#ifdef CKPT

	int val;
	int mpirun_listen_fd = 0;
	time_t tm;
	struct tm *stm;

	struct sockaddr_in cr_sa;

	if (pthread_spin_init(&flock, PTHREAD_PROCESS_PRIVATE) != 0) {
		DBG(fprintf(stderr, "[mpirun_rsh:main] pthread_spin_init(flock) failed\n"));
		return(-6);
	}

	cr_id = cr_init();
	if (cr_id < 0) {
		fprintf(stderr, "CR Initialization failed\n");
		return(-1);
	}

	if (cr_register_callback(CR_Callback, (void *) NULL, CR_THREAD_CONTEXT) < 0) {
		fprintf(stderr, "CR Callback Registration failed\n");
		return(-2);
	}

	if (pthread_mutex_init(&cr_lock, NULL)) {
		DBG(perror("[mpirun_rsh:main] pthread_mutex_init(cr_lock)"));
		return(-3);
	}

	strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME);

	tm = time(NULL);
	if ((time_t) tm == -1) {
		DBG(fprintf(stderr, "[mpirun_rsh:main] time() failed\n"));
		return(-4);
	}

	stm = localtime(&tm);
	if (!stm) {
		DBG(fprintf(stderr, "[mpirun_rsh:main] localtime() failed\n"));
		return(-5);
	}

	snprintf(sessionid, CR_SESSION_MAX, "%d%d%d%d%d",
		 stm->tm_yday, stm->tm_hour, stm->tm_min, stm->tm_sec, getpid());
	sessionid[CR_SESSION_MAX-1] = '\0';

restart_from_ckpt:

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

#endif /* CKPT */

	size_t hostname_len = 0;
	totalview_cmd[199] = 0;
	display[0] = '\0';

	/* mpirun [-debug] [-xterm] -np N [-hostfile hfile | h1 h2 h3 ... hN] a.out [args] */

	do {
		c = getopt_long_only (argc, argv, "+", option_table,
							  &option_index);
		switch (c) {
		case '?':
		case ':':
			usage ();
			exit (EXIT_FAILURE);
			break;
		case EOF:
			break;
		case 0:
			switch (option_index) {
			case 0:
				nprocs = atoi (optarg);
				if (nprocs < 1) {
					usage ();
					exit (EXIT_FAILURE);
				}
				break;
			case 1:
				debug_on = 1;
				xterm_on = 1;
				break;
			case 2:
				xterm_on = 1;
				break;
			case 3:
				hostfile_on = 1;
				strncpy (hostfile, optarg, HOSTFILE_LEN);
				if (strlen (optarg) >= HOSTFILE_LEN - 1)
					hostfile[HOSTFILE_LEN] = '\0';
				break;
			case 4:
				paramfile_on = 1;
				strncpy (paramfile, optarg, PARAMFILE_LEN);
				if (strlen (optarg) >= PARAMFILE_LEN - 1) {
					paramfile[PARAMFILE_LEN] = '\0';
				}
				break;
			case 5:
				show_on = 1;
				break;
			case 6:
				use_rsh = 1;
				break;
			case 7:
				use_rsh = 0;
				break;
			case 8:
				usage ();
				exit (EXIT_SUCCESS);
				break;
			case 10:
				{
					/* -tv */
					int count, idx;
					char **new_argv;
					tv_env = getenv ("TOTALVIEW");
					if (tv_env != NULL) {
						strcpy (totalview_cmd, tv_env);
					} else {
						fprintf (stderr,
								 "TOTALVIEW env is NULL, use default: %s\n",
								 TOTALVIEW_CMD);
						sprintf (totalview_cmd, "%s", TOTALVIEW_CMD);
					}
					new_argv =
						(char **) malloc (sizeof (char **) * argc + 3);
					new_argv[0] = totalview_cmd;
					new_argv[1] = argv[0];
					new_argv[2] = "-a";
					new_argv[3] = "-startedByTv";
					idx = 4;
					for (count = 1; count < argc; count++) {
						if (strcmp (argv[count], "-tv"))
							new_argv[idx++] = argv[count];
					}
					new_argv[idx] = NULL;
					if (execv (new_argv[0], new_argv)) {
						perror ("execv");
						exit (EXIT_FAILURE);
					}

				}
				break;
			case 12:
				/* -startedByTv */
				use_totalview = 1;
				debug_on = 1;
				break;
			case 11:
				legacy_startup = 1;
				break;
			case 13:			/* spawnspec given */
				spawnfile = strdup (optarg);
				DBG (fprintf
					 (stderr, "spawn spec file = %s\n", spawnfile));
				break;
			case 14:
				dpm = 1;
				break;
            case 15: /* -fastssh */
#ifndef CKPT
                USE_LINEAR_SSH = 0;
#endif /* CKPT */
                break;
			case 16:
				usage ();
				exit (EXIT_SUCCESS);
				break;
			default:
				fprintf (stderr, "Unknown option\n");
				usage ();
				exit (EXIT_FAILURE);
				break;
			}
			break;
		default:
			fprintf (stderr, "Unreachable statement!\n");
			usage ();
			exit (EXIT_FAILURE);
			break;
		}
	} while (c != EOF);

	if (!nprocs) {
		usage ();
		exit (EXIT_FAILURE);
	}

	binary_dirname = dirname (strdup (argv[0]));
	if (strlen (binary_dirname) == 1 && argv[0][0] != '.') {
		use_dirname = 0;
	}

	if (!hostfile_on) {
		/* get hostnames from argument list */
        if (strchr (argv[optind], '=') || argc - optind < nprocs + 1) {
            sprintf (hostfile, "%s/.mpirun_hosts", env2str ("HOME"));
            if (file_exists (hostfile)) {
                hostfile_on = 1;
                aout_index = optind;
                goto cont;
            }
            else {
			    fprintf (stderr, "Without hostfile option, hostnames must be "
			    		 "specified on command line.\n");
			    usage ();
			    exit (EXIT_FAILURE);
            }
		}
		aout_index = nprocs + optind;
	} else {
		aout_index = optind;
	}
cont:
	/* reading default param file */
	if (0 == (access (PARAM_GLOBAL, R_OK))) {
		num_of_params += read_param_file (PARAM_GLOBAL, &env);
	}

	/* reading file specified by user env */
	if ((param_env = getenv ("MVAPICH_DEF_PARAMFILE")) != NULL) {
		num_of_params += read_param_file (param_env, &env);
	}

	if (paramfile_on) {
		/* construct a string of environment variable definitions from
		 * the entries in the paramfile.  These environment variables
		 * will be available to the remote processes, which
		 * will use them to over-ride default parameter settings
		 */
		num_of_params += read_param_file (paramfile, &env);
	}

	plist = malloc (nprocs * sizeof (process));
	if (plist == NULL) {
		perror ("malloc");
		exit (EXIT_FAILURE);
	}

	for (i = 0; i < nprocs; i++) {
		plist[i].state = P_NOTSTARTED;
		plist[i].device = NULL;
		plist[i].port = -1;
		plist[i].remote_pid = 0;
	}

	/* grab hosts from command line or file */

	if (hostfile_on) {
		hostname_len = read_hostfile (hostfile);
	} else {
		for (i = 0; i < nprocs; i++) {
			plist[i].hostname = (char *) strndup (argv[optind + i], 100);
			hostname_len = hostname_len > strlen (plist[i].hostname) ?
				hostname_len : strlen (plist[i].hostname);
		}
	}

	if (use_totalview) {
		MPIR_proctable = (struct MPIR_PROCDESC *) malloc
			(MPIR_PROCDESC_s * nprocs);
		MPIR_proctable_size = nprocs;

		for (i = 0; i < nprocs; ++i) {
			MPIR_proctable[i].host_name = plist[i].hostname;
		}
	}

	wd = get_current_dir_name ();
	gethostname (mpirun_host, MAX_HOST_LEN);

	get_display_str ();

	server_socket = s = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0) {
		perror ("socket");
		exit (EXIT_FAILURE);
	}
	sockaddr.sin_addr.s_addr = INADDR_ANY;
	sockaddr.sin_port = 0;
	if (bind (s, (struct sockaddr *) &sockaddr, sockaddr_len) < 0) {
		perror ("bind");
		exit (EXIT_FAILURE);
	}

	if (getsockname (s, (struct sockaddr *) &sockaddr, &sockaddr_len) < 0) {
		perror ("getsockname");
		exit (EXIT_FAILURE);
	}

	port = (int) ntohs (sockaddr.sin_port);
	listen (s, nprocs);

	if (!show_on) {
		struct sigaction signal_handler;
		signal_handler.sa_handler = cleanup_handler;
		sigfillset (&signal_handler.sa_mask);
		signal_handler.sa_flags = 0;

		sigaction (SIGHUP, &signal_handler, NULL);
		sigaction (SIGINT, &signal_handler, NULL);
		sigaction (SIGTERM, &signal_handler, NULL);

		signal_handler.sa_handler = nostop_handler;

		sigaction (SIGTSTP, &signal_handler, NULL);

		signal_handler.sa_handler = alarm_handler;

		sigaction (SIGALRM, &signal_handler, NULL);

		signal_handler.sa_handler = child_handler;
		sigemptyset (&signal_handler.sa_mask);

		sigaction (SIGCHLD, &signal_handler, NULL);
	}

	for (i = 0; i < nprocs; i++) {
		/*
		 * I should probably do some sort of hostname lookup to account for
		 * situations where people might use two different hostnames for the
		 * same host.
		 */
		pglist_insert (plist[i].hostname, i);
	}

	timeout = env2int ("MV2_MPIRUN_TIMEOUT");
	if (timeout <= 0) {
		timeout = pglist ? pglist->npgs : nprocs;
		if (timeout < 30)
			timeout = 30;
		else if (timeout > 1000)
			timeout = 1000;
		if (debug_on) {
			/* Timeout of 24 hours so that we don't interrupt debugging */
			timeout += 86400;
		}
	}

    if (!dpm) {
        srand (getuid ());
    	i = rand () % 1000;
    	MV2_XRC_FILE = mkstr ("mv2_xrc_%03d_%s_%d", i, mpirun_host, getpid ());
        dpm_add_env ("MV2_XRC_FILE", MV2_XRC_FILE);
    }

	alarm (timeout);
	alarm_msg = "Timeout during client startup.\n";

	fastssh_threshold = env2int ("MV2_FASTSSH_THRESHOLD");

    if (!fastssh_threshold) 
        fastssh_threshold = 1 << 8;

    if (pglist->npgs < fastssh_threshold) {
        USE_LINEAR_SSH = 1;
    }

    USE_LINEAR_SSH = dpm ? 1 : USE_LINEAR_SSH;

    if (USE_LINEAR_SSH) {
        NSPAWNS = pglist->npgs;

#ifdef CKPT

        if (!show_on) {

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
            cr_sa.sin_port        = 0;

            if (bind(mpirun_listen_fd, (struct sockaddr *) &cr_sa, sizeof(cr_sa)) < 0) {
                perror("[mpirun_rsh] bind(mpirun_listen_fd)");
                exit(EXIT_FAILURE);
            }

            val = sizeof(cr_sa);
            if (getsockname(mpirun_listen_fd, &cr_sa, (socklen_t *) &val) < 0) {
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

        } /* if (!show_on) */

#endif /* CKPT */

	    spawn_fast (argc, argv, totalview_cmd, env);

#ifdef CKPT

        if (!show_on) {

            for (i=0; i<NSPAWNS; i++)
            {
                if ((mpirun_fd[i] = accept(mpirun_listen_fd, NULL, NULL)) < 0) {
                    snprintf(cr_errmsg, CR_ERRMSG_SZ, "[mpirun_rsh] accept(mpirun_fd[%d])", i);
                    perror(cr_errmsg);
                    close(mpirun_listen_fd);
                    cleanup();
                }
            }

            close(mpirun_listen_fd);

            CR_MUTEX_LOCK;
            cr_state = CR_READY;
            CR_MUTEX_UNLOCK;

        } /* if (!show_on) */

#endif /* CKPT */

    }
    else {
        NSPAWNS = 1;
        spawn_one (argc, argv, totalview_cmd, env);
    }

	if (show_on)
		exit (EXIT_SUCCESS);
	mpispawn_checkin (server_socket, (struct sockaddr *) &sockaddr,
					  sockaddr_len);
	alarm (0);
	wait_for_mpispawn (server_socket, &sockaddr, sockaddr_len);

#ifdef CKPT
	if (restart_context) {

		/* Wait for previous instance of CR_Loop to exit */
		if (cr_tid) {
			pthread_join(cr_tid, NULL);
			cr_tid = 0;
		}

		/* Flush out stale data */
		free_memory();
		free(mpirun_fd);

		fprintf(stderr, "Restarting...\n");
		fflush(stderr);
		goto restart_from_ckpt;
	}
#endif /* CKPT */

	/*
	 * This return should never be reached.
	 */
	return EXIT_FAILURE;
}

void wait_for_mpispawn (int s, struct sockaddr_in *sockaddr,
						unsigned int sockaddr_len)
{
	int wfe_socket, wfe_mpispawn_code, wfe_abort_mid;

  listen:

#ifdef CKPT
	if (restart_context) return;
#endif

	while ((wfe_socket = accept (s, (struct sockaddr *) sockaddr,
								 &sockaddr_len)) < 0) {
#ifdef CKPT
		if (restart_context) return;
#endif

		if (errno == EINTR || errno == EAGAIN)
			continue;
		perror ("accept");
		cleanup ();
	}

	if (read_socket (wfe_socket, &wfe_mpispawn_code, sizeof (int))
		|| read_socket (wfe_socket, &wfe_abort_mid, sizeof (int))) {
		fprintf (stderr, "Termination socket read failed!\n");
	}

	else if (wfe_mpispawn_code == MPISPAWN_DPM_REQ) {
		DBG (fprintf
			 (stderr, "Dynamic spawn request from %d\n", wfe_abort_mid));
		handle_spawn_req (wfe_socket);
		close (wfe_socket);
		goto listen;
	} else {
		fprintf (stderr, "Exit code %d signaled from %s\n",
				 wfe_mpispawn_code,
				 pglist->index[wfe_abort_mid]->hostname);
	}

	close (wfe_socket);
	cleanup ();
}

void usage (void)
{
	fprintf (stderr, "usage: mpirun_rsh [-v] [-rsh|-ssh] "
			 "[-paramfile=pfile] "
			 "[-debug] -[tv] [-xterm] [-show] [-legacy] -np N"
			 "(-hostfile hfile | h1 h2 ... hN) a.out args\n");
	fprintf (stderr, "Where:\n");
	fprintf (stderr, "\trsh        => " "to use rsh for connecting\n");
	fprintf (stderr, "\tssh        => " "to use ssh for connecting\n");
	fprintf (stderr, "\tparamfile  => "
			 "file containing run-time MVICH parameters\n");
	fprintf (stderr, "\tdebug      => "
			 "run each process under the control of gdb\n");
	fprintf (stderr, "\ttv         => "
			 "run each process under the control of totalview\n");
	fprintf (stderr, "\txterm      => "
			 "run remote processes under xterm\n");
	fprintf (stderr, "\tshow       => "
			 "show command for remote execution but dont run it\n");
	fprintf (stderr, "\tlegacy     => "
			 "use old startup method (1 ssh/process)\n");
	fprintf (stderr, "\tnp         => "
			 "specify the number of processes\n");
	fprintf (stderr, "\th1 h2...   => "
			 "names of hosts where processes should run\n");
	fprintf (stderr, "or\thostfile   => "
			 "name of file contining hosts, one per line\n");
	fprintf (stderr, "\ta.out      => " "name of MPI binary\n");
	fprintf (stderr, "\targs       => " "arguments for MPI binary\n");
	fprintf (stderr, "\n");
}

/* finds first non-whitespace char in input string */
char *skip_white (char *s)
{
	int len;
	/* return pointer to first non-whitespace char in string */
	/* Assumes string is null terminated */
	/* Clean from start */
	while ((*s == ' ') || (*s == '\t'))
		s++;
	/* Clean from end */
	len = strlen (s) - 1;

	while (((s[len] == ' ')
			|| (s[len] == '\t')) && (len >= 0)) {
		s[len] = '\0';
		len--;
	}
	return s;
}

/* Read hostfile */
static int read_hostfile (char *hostfile_name)
{
	size_t j, hostname_len = 0;
	int i;
	FILE *hf = fopen (hostfile_name, "r");

	if (hf == NULL) {
		fprintf (stderr, "Can't open hostfile %s\n", hostfile_name);
		perror ("open");
		exit (EXIT_FAILURE);
	}

	for (i = 0; i < nprocs; i++) {
		char line[100];
		char *trimmed_line;
		int separator_count = 0, prev_j = 0;

	  reread_host:
		if (fgets (line, 100, hf) != NULL) {
			size_t len = strlen (line);

			if (line[len - 1] == '\n') {
				line[len - 1] = '\0';
			}

			/* Remove comments and empty lines */
			if (strchr (line, '#') != NULL) {
				line[strlen (line) - strlen (strchr (line, '#'))] = '\0';
			}

			trimmed_line = skip_white (line);

			if (strlen (trimmed_line) == 0) {
				/* The line is empty, drop it */
				i--;
				continue;
			}

			/*Update len and continue patch ?! move it to func ? */
			len = strlen (trimmed_line);

			/* Parsing format:
			 * hostname SEPARATOR hca_name SEPARATOR port
			 */

			for (j = 0; j < len; j++) {
				if (trimmed_line[j] == SEPARATOR && separator_count == 0) {
					plist[i].hostname =
						(char *) strndup (trimmed_line, j + 1);
					plist[i].hostname[j] = '\0';
					prev_j = j;
					separator_count++;
					hostname_len = hostname_len > len ? hostname_len : len;
					continue;
				}
				if (trimmed_line[j] == SEPARATOR && separator_count == 1) {
					plist[i].device =
						(char *) strndup (&trimmed_line[prev_j + 1],
										  j - prev_j);
					plist[i].device[j - prev_j - 1] = '\0';
					separator_count++;
					continue;
				}
				if (separator_count == 2) {
					plist[i].port = atoi (&trimmed_line[j]);
					break;
				}
			}
			if (0 == separator_count) {
				plist[i].hostname = strdup (trimmed_line);
				hostname_len = hostname_len > len ? hostname_len : len;
			}
			if (1 == separator_count) {
				plist[i].device =
					(char *) strdup (&trimmed_line[prev_j + 1]);
			}
		} else {
			/* if less than available hosts, rewind file and re-use hosts */
			rewind (hf);
			goto reread_host;
		}
	}
	fclose (hf);
	return hostname_len;
}

/*
 * reads the param file and constructs the environment strings
 * for each of the environment variables.
 * The caller is responsible for de-allocating the returned string.
 *
 * NOTE: we cant just append these to our current environment because
 * RSH and SSH do not export our environment vars to the remote host.
 * Rather, the RSH command that starts the remote process looks
 * something like:
 *    rsh remote_host "cd workdir; env ENVNAME=value ... command"
 */
int read_param_file (char *paramfile, char **env)
{
	FILE *pf;
	char errstr[256];
	char name[128], value[193];
	char buf[384];
	char line[LINE_LEN];
	char *p, *tmp;
	int num, e_len;
	int env_left = 0;
	int num_params = 0;

	if ((pf = fopen (paramfile, "r")) == NULL) {
		sprintf (errstr, "Cant open paramfile = %s", paramfile);
		perror (errstr);
		exit (EXIT_FAILURE);
	}

	if (strlen (*env) == 0) {
		/* Allocating space for env first time */
		if ((*env = malloc (ENV_LEN)) == NULL) {
			fprintf (stderr, "Malloc of env failed in read_param_file\n");
			exit (EXIT_FAILURE);
		}
		env_left = ENV_LEN - 1;
	} else {
		/* already allocated */
		env_left = ENV_LEN - (strlen (*env) + 1) - 1;
	}

	while (fgets (line, LINE_LEN, pf) != NULL) {
		p = skip_white (line);
		if (*p == '#' || *p == '\n') {
			/* a comment or a blank line, ignore it */
			continue;
		}
		/* look for NAME = VALUE, where NAME == MVICH_... */
		name[0] = value[0] = '\0';
		if (param_debug) {
			printf ("Scanning: %s\n", p);
		}
		if ((num = sscanf (p, "%64[A-Z_] = %192s", name, value)) != 2) {
			/* debug */
			if (param_debug) {
				printf ("FAILED: matched = %d, name = %s, "
						"value = %s in \n\t%s\n", num, name, value, p);
			}
			continue;
		}

		/* construct the environment string */
		buf[0] = '\0';
		sprintf (buf, "%s=%s ", name, value);
		++num_params;
        dpm_add_env(buf, NULL);

		if (mpispawn_param_env) {
			tmp = mkstr ("%s MPISPAWN_GENERIC_NAME_%d=%s"
						 " MPISPAWN_GENERIC_VALUE_%d=%s",
						 mpispawn_param_env, param_count, name,
						 param_count, value);

			free (mpispawn_param_env);

			if (tmp) {
				mpispawn_param_env = tmp;
				param_count++;
			}

			else {
				fprintf (stderr, "malloc failed in read_param_file\n");
				exit (EXIT_FAILURE);
			}
		}

		else {
			mpispawn_param_env = mkstr ("MPISPAWN_GENERIC_NAME_%d=%s"
										" MPISPAWN_GENERIC_VALUE_%d=%s",
										param_count, name, param_count,
										value);

			if (!mpispawn_param_env) {
				fprintf (stderr, "malloc failed in read_param_file\n");
				exit (EXIT_FAILURE);
			}

			param_count++;
		}

		/* concat to actual environment string */
		e_len = strlen (buf);
		if (e_len > env_left) {
			/* oops, need to grow env string */
			int newlen =
				(ENV_LEN >
				 e_len + 1 ? ENV_LEN : e_len + 1) + strlen (*env);
			if ((*env = realloc (*env, newlen)) == NULL) {
				fprintf (stderr, "realloc failed in read_param_file\n");
				exit (EXIT_FAILURE);
			}
			if (param_debug) {
				printf ("realloc to %d\n", newlen);
			}
			env_left = ENV_LEN - 1;
		}
		strcat (*env, buf);
		env_left -= e_len;
		if (param_debug) {
			printf ("Added: [%s]\n", buf);
			printf ("env len = %d, env left = %d\n", (int) strlen (*env),
					env_left);
		}
	}
	fclose (pf);

	return num_params;
}

void cleanup_handler (int sig)
{
	cleanup ();
	exit (EXIT_FAILURE);
}

void pglist_print (void)
{
	if (pglist) {
		size_t i, j, npids = 0, npids_allocated = 0;

		fprintf (stderr, "\n--pglist--\ndata:\n");
		for (i = 0; i < pglist->npgs; i++) {
			fprintf (stderr, "%p - %s:", &pglist->data[i],
					 pglist->data[i].hostname);
			fprintf (stderr, " %d (", pglist->data[i].pid);

			for (j = 0; j < pglist->data[i].npids;
				 fprintf (stderr, ", "), j++) {
				fprintf (stderr, "%d", pglist->data[i].plist_indices[j]);
			}

			fprintf (stderr, ")\n");
			npids += pglist->data[i].npids;
			npids_allocated += pglist->data[i].npids_allocated;
		}

		fprintf (stderr, "\nindex:");
		for (i = 0; i < pglist->npgs; i++) {
			fprintf (stderr, " %p", pglist->index[i]);
		}

		fprintf (stderr, "\nnpgs/allocated: %d/%d (%d%%)\n",
				 (int) pglist->npgs, (int) pglist->npgs_allocated,
				 (int) (pglist->npgs_allocated ? 100. * pglist->npgs /
						pglist->npgs_allocated : 100.));
		fprintf (stderr, "npids/allocated: %d/%d (%d%%)\n", (int) npids,
				 (int) npids_allocated,
				 (int) (npids_allocated ? 100. * npids /
						npids_allocated : 100.));
		fprintf (stderr, "--pglist--\n\n");
	}
}

void pglist_insert (const char *const hostname, const int plist_index)
{
	const size_t increment = nprocs > 4 ? nprocs / 4 : 1;
	size_t i, index = 0;
	static size_t alloc_error = 0;
	int strcmp_result, bottom = 0, top;
	process_group *pg;
	void *backup_ptr;

	if (alloc_error)
		return;
	if (pglist == NULL)
		goto init_pglist;

	top = pglist->npgs - 1;
	index = (top + bottom) / 2;

	while ((strcmp_result =
			strcmp (hostname, pglist->index[index]->hostname))) {
		if (strcmp_result > 0) {
			bottom = index + 1;
		}

		else {
			top = index - 1;
		}

		if (bottom > top)
			break;
		index = (top + bottom) / 2;
	}

	if (!dpm && !strcmp_result)
		goto insert_pid;

	if (strcmp_result > 0)
		index++;

	goto add_process_group;

  init_pglist:
	pglist = malloc (sizeof (process_groups));

	if (pglist) {
		pglist->data = NULL;
		pglist->index = NULL;
		pglist->npgs = 0;
		pglist->npgs_allocated = 0;
	}

	else {
		goto register_alloc_error;
	}

  add_process_group:
	if (pglist->npgs == pglist->npgs_allocated) {
		process_group *pglist_data_backup = pglist->data;
		ptrdiff_t offset;

		pglist->npgs_allocated += increment;

		backup_ptr = pglist->data;
		pglist->data = realloc (pglist->data, sizeof (process_group) *
								pglist->npgs_allocated);

		if (pglist->data == NULL) {
			pglist->data = backup_ptr;
			goto register_alloc_error;
		}

		backup_ptr = pglist->index;
		pglist->index = realloc (pglist->index, sizeof (process_group *) *
								 pglist->npgs_allocated);

		if (pglist->index == NULL) {
			pglist->index = backup_ptr;
			goto register_alloc_error;
		}

		if ((offset = (size_t) pglist->data - (size_t) pglist_data_backup)) {
			for (i = 0; i < pglist->npgs; i++) {
				pglist->index[i] =
					(process_group *) ((size_t) pglist->index[i] + offset);
			}
		}
	}

	for (i = pglist->npgs; i > index; i--) {
		pglist->index[i] = pglist->index[i - 1];
	}

	pglist->data[pglist->npgs].hostname = hostname;
	pglist->data[pglist->npgs].pid = -1;
	pglist->data[pglist->npgs].plist_indices = NULL;
	pglist->data[pglist->npgs].npids = 0;
	pglist->data[pglist->npgs].npids_allocated = 0;

	pglist->index[index] = &pglist->data[pglist->npgs++];

  insert_pid:
	pg = pglist->index[index];

	if (pg->npids == pg->npids_allocated) {
		if (pg->npids_allocated) {
			pg->npids_allocated <<= 1;

			if (pg->npids_allocated < pg->npids)
				pg->npids_allocated = SIZE_MAX;
			if (pg->npids_allocated > (size_t) nprocs)
				pg->npids_allocated = nprocs;
		}

		else {
			pg->npids_allocated = 1;
		}

		backup_ptr = pg->plist_indices;
		pg->plist_indices =
			realloc (pg->plist_indices,
					 pg->npids_allocated * sizeof (int));

		if (pg->plist_indices == NULL) {
			pg->plist_indices = backup_ptr;
			goto register_alloc_error;
		}
	}

	pg->plist_indices[pg->npids++] = plist_index;

	return;

  register_alloc_error:
	if (pglist) {
		if (pglist->data) {
			for (pg = pglist->data; pglist->npgs--; pg++) {
				if (pg->plist_indices)
					free (pg->plist_indices);
			}

			free (pglist->data);
		}

		if (pglist->index)
			free (pglist->index);

		free (pglist);
	}

	alloc_error = 1;
}

void free_memory (void)
{
#ifdef CKPT
	pthread_spin_lock(&flock);
	if (fcnt) {
		pthread_spin_unlock(&flock);
		return;
	}
	fcnt = 1;
	pthread_spin_unlock(&flock);
#endif
	if (pglist) {
		if (pglist->data) {
			process_group *pg = pglist->data;

			while (pglist->npgs--) {
				if (pg->plist_indices)
					free (pg->plist_indices);
				pg++;
			}

			free (pglist->data);
		}

		if (pglist->index)
			free (pglist->index);

		free (pglist);
                pglist = NULL;
	}

	if (plist) {
		while (nprocs--) {
			if (plist[nprocs].device)
				free (plist[nprocs].device);
			if (plist[nprocs].hostname)
				free (plist[nprocs].hostname);
		}

		free (plist);
                plist = NULL;
	}
}

void cleanup (void)
{
	int i;

#ifdef CKPT
	if (cr_tid) {
		pthread_kill(cr_tid, SIGTERM);
		cr_tid = 0;
	}
#endif /* CKPT */

	if (use_totalview) {
		fprintf (stderr, "Cleaning up all processes ...");
	}
	if (use_totalview)
		MPIR_debug_state = MPIR_DEBUG_ABORTING;
	for (i = 0; i < NSIG; i++) {
		signal (i, SIG_DFL);
	}

	if (pglist) {
		rkill_fast ();
	}

	else {
		for (i = 0; i < nprocs; i++) {
			if (RUNNING (i)) {
				/* send terminal interrupt, which will hopefully 
				   propagate to the other side. (not sure what xterm will
				   do here.
				 */
				kill (plist[i].pid, SIGINT);
			}
		}

		sleep (1);

		for (i = 0; i < nprocs; i++) {
			if (plist[i].state != P_NOTSTARTED) {
				/* send regular interrupt to rsh */
				kill (plist[i].pid, SIGTERM);
			}
		}

		sleep (1);

		for (i = 0; i < nprocs; i++) {
			if (plist[i].state != P_NOTSTARTED) {
				/* Kill the processes */
				kill (plist[i].pid, SIGKILL);
			}
		}

		rkill_linear ();
	}

	exit (EXIT_FAILURE);
}

void rkill_fast (void)
{
	int tryagain, spawned_pid[pglist->npgs];
	size_t i, j;

	for (i = 0; i < NSPAWNS; i++) {
		if (0 == (spawned_pid[i] = fork ())) {
			if (pglist->index[i]->npids) {
				const size_t bufsize = 40 + 10 * pglist->index[i]->npids;
				const process_group *pg = pglist->index[i];
				char kill_cmd[bufsize], tmp[10];

				kill_cmd[0] = '\0';

				if (legacy_startup) {
					strcat (kill_cmd, "kill -s 9");
					for (j = 0; j < pg->npids; j++) {
						snprintf (tmp, 10, " %d",
								  plist[pg->plist_indices[j]].remote_pid);
						strcat (kill_cmd, tmp);
					}
				}

				else {
					strcat (kill_cmd, "kill");
					snprintf (tmp, 10, " %d", pg->pid);
					strcat (kill_cmd, tmp);
				}

				strcat (kill_cmd, " >&/dev/null");

				if (use_rsh) {
					execl (RSH_CMD, RSH_CMD, pg->hostname, kill_cmd, NULL);
				}

				else {
					execl (SSH_CMD, SSH_CMD, SSH_ARG, "-x", pg->hostname,
						   kill_cmd, NULL);
				}

				perror (NULL);
				exit (EXIT_FAILURE);
			}

			else {
				exit (EXIT_SUCCESS);
			}
		}
	}

	while (1) {
		static int iteration = 0;
		tryagain = 0;

		sleep (1 << iteration);

		for (i = 0; i < pglist->npgs; i++) {
			if (spawned_pid[i]) {
				if (!
					(spawned_pid[i] =
					 waitpid (spawned_pid[i], NULL, WNOHANG))) {
					tryagain = 1;
				}
			}
		}

		if (++iteration == 5 || !tryagain) {
			break;
		}
	}

	if (tryagain) {
		fprintf (stderr,
				 "The following processes may have not been killed:\n");
		for (i = 0; i < pglist->npgs; i++) {
			if (spawned_pid[i]) {
				const process_group *pg = pglist->index[i];

				fprintf (stderr, "%s:", pg->hostname);

				for (j = 0; j < pg->npids; j++) {
					fprintf (stderr, " %d",
							 plist[pg->plist_indices[j]].remote_pid);
				}

				fprintf (stderr, "\n");
			}
		}
	}
}

void rkill_linear (void)
{
	int i, tryagain, spawned_pid[nprocs];

	fprintf (stderr, "Killing remote processes...");

	for (i = 0; i < nprocs; i++) {
		if (0 == (spawned_pid[i] = fork ())) {
			char kill_cmd[80];

			if (!plist[i].remote_pid)
				exit (EXIT_SUCCESS);

			snprintf (kill_cmd, 80, "kill -s 9 %d >&/dev/null",
					  plist[i].remote_pid);

			if (use_rsh) {
				execl (RSH_CMD, RSH_CMD, plist[i].hostname, kill_cmd,
					   NULL);
			}

			else {
				execl (SSH_CMD, SSH_CMD, SSH_ARG, "-x",
					   plist[i].hostname, kill_cmd, NULL);
			}

			perror (NULL);
			exit (EXIT_FAILURE);
		}
	}

	while (1) {
		static int iteration = 0;
		tryagain = 0;

		sleep (1 << iteration);

		for (i = 0; i < nprocs; i++) {
			if (spawned_pid[i]) {
				if (!
					(spawned_pid[i] =
					 waitpid (spawned_pid[i], NULL, WNOHANG))) {
					tryagain = 1;
				}
			}
		}

		if (++iteration == 5 || !tryagain) {
			break;
		}
	}

	if (tryagain) {
		fprintf (stderr,
				 "The following processes may have not been killed:\n");
		for (i = 0; i < nprocs; i++) {
			if (spawned_pid[i]) {
				fprintf (stderr, "%s [%d]\n", plist[i].hostname,
						 plist[i].remote_pid);
			}
		}
	}
}

int file_exists (char *filename)
{
	FILE *fp = fopen (filename, "r");
	if (fp) {
		fclose (fp);
		return 1;
	}
	return 0;
}

int getpath (char *buf, int buf_len)
{
	char link[32];
	pid_t pid;
	unsigned len;
	pid = getpid ();
	snprintf (&link[0], sizeof (link), "/proc/%i/exe", pid);

	if ((len = readlink (&link[0], buf, buf_len)) == -1) {
		buf[0] = 0;
		return 0;
	} else {
		buf[len] = 0;
		while (len && buf[--len] != '/');
		if (buf[len] == '/')
			buf[len] = 0;
		return len;
	}
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CHECK_ALLOC() do { \
    if (tmp) { \
        free (mpispawn_env); \
        mpispawn_env = tmp; \
    } \
    else goto allocation_error; \
} while (0);

void spawn_fast (int argc, char *argv[], char *totalview_cmd, char *env)
{
	char *mpispawn_env, *tmp, *ld_library_path, *template, *template2;
	char *name, *value;
	int i, n, tmp_i, argind, multichk = 1, multival = 0;
	FILE *fp;
	char pathbuf[PATH_MAX];

	if ((ld_library_path = getenv ("LD_LIBRARY_PATH"))) {
		mpispawn_env = mkstr ("LD_LIBRARY_PATH=%s:%s",
							  LD_LIBRARY_PATH_MPI, ld_library_path);
	}

	else {
		mpispawn_env = mkstr ("LD_LIBRARY_PATH=%s", LD_LIBRARY_PATH_MPI);
	}

	if (!mpispawn_env)
		goto allocation_error;

	tmp = mkstr ("%s MPISPAWN_MPIRUN_MPD=0", mpispawn_env);
	CHECK_ALLOC ();

	tmp = mkstr ("%s USE_LINEAR_SSH=%d", mpispawn_env, USE_LINEAR_SSH);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_HOST=%s", mpispawn_env, mpirun_host);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPIRUN_RSH_LAUNCH=1", mpispawn_env);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_CHECKIN_PORT=%d", mpispawn_env, port);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_PORT=%d", mpispawn_env, port);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_GLOBAL_NPROCS=%d", mpispawn_env, nprocs);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_ID=%d", mpispawn_env, getpid ());
	CHECK_ALLOC ();

	if (use_totalview) {
		tmp = mkstr ("%s MPISPAWN_USE_TOTALVIEW=1", mpispawn_env);
		CHECK_ALLOC ();

	}

#ifdef CKPT
        tmp = mkstr ("%s MPISPAWN_MPIRUN_CR_PORT=%d", mpispawn_env, mpirun_port);
        CHECK_ALLOC ();

        tmp = mkstr ("%s MPISPAWN_CR_CONTEXT=%d", mpispawn_env, cached_restart_context);
        CHECK_ALLOC ();

        tmp = mkstr ("%s MPISPAWN_CR_SESSIONID=%s", mpispawn_env, sessionid);
        CHECK_ALLOC ();

        tmp = mkstr ("%s MPISPAWN_CR_CKPT_CNT=%d", mpispawn_env, checkpoint_count);
        CHECK_ALLOC ();
#endif /* CKPT */

	/* 
	 * mpirun_rsh allows env variables to be set on the commandline
	 */
	if (!mpispawn_param_env) {
		mpispawn_param_env = mkstr ("");
		if (!mpispawn_param_env)
			goto allocation_error;
	}

	while (aout_index != argc && strchr (argv[aout_index], '=')) {
		name = strdup (argv[aout_index++]);
		value = strchr (name, '=');
		value[0] = '\0';
		value++;
        dpm_add_env(name, value);

#ifdef CKPT
		if (strcmp(name, "MV2_CKPT_FILE") == 0) {
			strncpy(ckpt_filename, value, CR_MAX_FILENAME);
		}
		else if (strcmp(name, "MV2_CKPT_INTERVAL") == 0) {
			checkpoint_interval = atoi(value) * 60;
		}
		else if (strcmp(name, "MV2_CKPT_MAX_SAVE_CKPTS") == 0) {
			max_save_ckpts = atoi(value);
		}
		else if (strcmp(name, "MV2_CKPT_MAX_CKPTS") == 0) {
			max_ckpts = atoi(value);
		}
#endif /* CKPT */

		tmp = mkstr ("%s MPISPAWN_GENERIC_NAME_%d=%s"
					 " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
					 param_count, name, param_count, value);

		free (name);
		free (mpispawn_param_env);

		if (tmp) {
			mpispawn_param_env = tmp;
			param_count++;
		}

		else {
			goto allocation_error;
		}
	}
  
    if (!dpm) {
        tmp = mkstr ("%s MPISPAWN_GENERIC_NAME_%d=MV2_XRC_FILE"
                " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env, 
                param_count, param_count, MV2_XRC_FILE);
        if (tmp) {
            mpispawn_param_env = tmp;
            param_count++;
        }
        else 
            goto allocation_error;
    }

	if (!dpm && aout_index == argc) {
		fprintf (stderr, "Incorrect number of arguments.\n");
		usage ();
		exit (EXIT_FAILURE);
	}

	i = argc - aout_index;
	if (debug_on && !use_totalview)
		i++;

	if (dpm == 0) {
		tmp =
			mkstr ("%s MPISPAWN_ARGC=%d", mpispawn_env, argc - aout_index);
		CHECK_ALLOC ();
	}

	i = 0;

	if (debug_on && !use_totalview) {
		tmp =
			mkstr ("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++, DEBUGGER);

		CHECK_ALLOC ();
	}

	if (use_totalview) {
		int j;
		for (j = 0; j < MPIR_proctable_size; j++) {
			MPIR_proctable[j].executable_name = argv[aout_index];
		}
	}

	tmp_i = i;
		
    /* to make sure all tasks generate same pg-id we send a kvs_template */
    srand (getpid ());
	i = rand () % MAXLINE;
	tmp = mkstr ("%s MPDMAN_KVS_TEMPLATE=kvs_%d_%s_%d", mpispawn_env, i,
            mpirun_host, getpid ());
	CHECK_ALLOC ();

	if (dpm) {
		fp = fopen (spawnfile, "r");
		if (!fp) {
			fprintf (stderr, "spawn specification file not found\n");
			goto allocation_error;
		}
		spinf.dpmindex = spinf.dpmtot = 0;
		tmp = mkstr ("%s PMI_SPAWNED=1", mpispawn_env);
		CHECK_ALLOC ();

	}

    DBG(fprintf(stderr, "%d forks to be done\n", pglist->npgs));
    template = strdup(mpispawn_env);    /* save the string at this level */
    argind = tmp_i;
    i = 0;
	for(i = 0; i < pglist->npgs; i++) {

        free(mpispawn_env);
        mpispawn_env = strdup(template);
        tmp_i = argind;
		if (dpm) {
			int ret = 1, keyin = 0;
			char *key, tot_args = 1, arg_done = 0, infonum = 0;

			++tmp_i;			/* argc index starts from 1 */
			if (spinf.dpmindex < spinf.dpmtot) {
				DBG (fprintf (stderr, "one more fork of same binary\n"));
				++spinf.dpmindex;
                free(mpispawn_env);
                mpispawn_env = strdup(template2);
				goto done_spawn_read;
			}

			spinf.dpmindex = 0;
			get_line (fp, spinf.linebuf, 1);
			spinf.dpmtot = atoi (spinf.linebuf);
			get_line (fp, spinf.runbuf, 1);
			DBG (fprintf
				 (stderr, "Spawning %d instances of %s\n", spinf.dpmtot,
				  spinf.runbuf));
            if(multichk) {
                if(spinf.dpmtot == pglist->npgs)
                    multival = 0;
                else
                    multival = 1;
                multichk = 0;
            }
    
            ret = 1;
			/* first data is arguments */
			while (ret) {
				get_line (fp, spinf.linebuf, 1);
                
                if(infonum) {
                    char *infokey;
                    /* is it an info we are about ? */
                    if(check_info(spinf.linebuf)) {
                        infokey = strdup(spinf.linebuf);
                        get_line(fp, spinf.linebuf, 1);
                        store_info(infokey, spinf.linebuf);
                        free(infokey);
                    } else {
                        get_line(fp, spinf.linebuf, 1);
                    }
                    --infonum;
                    continue;
                }

				if (keyin) {
					sprintf (spinf.buf, "%s='%s'", key, spinf.linebuf);
					free (key);
					keyin = 0;
				}

				if (0 == (strncmp (spinf.linebuf, ARG, ARG_s))) {
					key = index (spinf.linebuf, '=');
					++key;
					tmp =
						mkstr ("%s MPISPAWN_ARGV_%d=\"%s\"", mpispawn_env,
							   tmp_i++, key);
					CHECK_ALLOC ();
					++tot_args;
				}

				if (0 == (strncmp (spinf.linebuf, ENDARG, ENDARG_s))) {
					tmp = mkstr ("%s MPISPAWN_ARGC=%d", mpispawn_env, tot_args);
					CHECK_ALLOC ();
					tot_args = 0;
					arg_done = 1;
				}

				if (0 == (strncmp (spinf.linebuf, END, END_s))) {
					ret = 0;
					++spinf.dpmindex;
				}
				if (0 == (strncmp (spinf.linebuf, PORT, PORT_s))) {
					keyin = 1;
					key = strdup (spinf.linebuf);
				}
                if (0 == (strncmp (spinf.linebuf, INFN, INFN_s))) {
                    /* info num parsed */
                    infonum = atoi(index(spinf.linebuf, '=') + 1);
                }
                
			}
			if (!arg_done) {
				tmp = mkstr ("%s MPISPAWN_ARGC=%d", mpispawn_env, 1);
				CHECK_ALLOC ();
			}

			tmp = mkstr ("%s MPISPAWN_LOCAL_NPROCS=1", mpispawn_env);
			CHECK_ALLOC ();
        
            tmp = mkstr("%s MPIRUN_COMM_MULTIPLE=%d", mpispawn_env,
                        multival);
            CHECK_ALLOC();
            template2 = strdup(mpispawn_env);
		} else {
			tmp = mkstr ("%s MPISPAWN_LOCAL_NPROCS=%d", mpispawn_env,
						 pglist->data[i].npids);
			CHECK_ALLOC ();
        }

	  done_spawn_read:

		if (!(pglist->data[i].pid = fork ())) {
			size_t arg_offset = 0;
			const char *nargv[7];
			char *command;

			if (dpm) {
				tmp = mkstr ("%s MPISPAWN_ARGV_0=%s", mpispawn_env,
							 spinf.runbuf);
				CHECK_ALLOC ();
				tmp = mkstr ("%s %s", mpispawn_env, spinf.buf);
				CHECK_ALLOC ();
			}

			if (!dpm) {
				while (aout_index < argc) {
					tmp =
						mkstr ("%s MPISPAWN_ARGV_%d=%s", mpispawn_env,
							   tmp_i++, argv[aout_index++]);
					CHECK_ALLOC ();
				}
			}

			if (mpispawn_param_env) {
				tmp =
					mkstr ("%s MPISPAWN_GENERIC_ENV_COUNT=%d %s",
						   mpispawn_env, param_count, mpispawn_param_env);

				free (mpispawn_param_env);
				free (mpispawn_env);

				if (tmp) {
					mpispawn_env = tmp;
				} else {
					goto allocation_error;
				}
			}

			tmp = mkstr ("%s MPISPAWN_ID=%d", mpispawn_env, i);
			CHECK_ALLOC ();

            /* user may specify custom binary path via MPI_Info */
            if(custpath) {
                tmp = mkstr("%s MPISPAWN_BINARY_PATH=%s", mpispawn_env, custpath);
                CHECK_ALLOC();
                free(custpath);
                custpath = NULL;
            }

            /* user may specifiy custom working dir via MPI_Info */
            if(custwd) {
    			tmp = mkstr ("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, custwd);
			    CHECK_ALLOC ();
                free(custwd);
                custwd = NULL;
            } else {
    			tmp = mkstr ("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, wd);
			    CHECK_ALLOC ();
            }

			for (n = 0; n < pglist->data[i].npids; n++) {
				tmp =
					mkstr ("%s MPISPAWN_MPIRUN_RANK_%d=%d", mpispawn_env,
						   n, pglist->data[i].plist_indices[n]);
				CHECK_ALLOC ();

				if (plist[pglist->data[i].plist_indices[n]].device != NULL) {
					tmp =
						mkstr ("%s MPISPAWN_VIADEV_DEVICE_%d=%s",
							   mpispawn_env, n,
							   plist[pglist->data[i].plist_indices[n]].
							   device);
					CHECK_ALLOC ();
				}

				tmp = mkstr ("%s MPISPAWN_VIADEV_DEFAULT_PORT_%d=%d",
							 mpispawn_env, n,
							 plist[pglist->data[i].plist_indices[n]].port);
				CHECK_ALLOC ();
			}

			if (xterm_on) {
				nargv[arg_offset++] = XTERM;
				nargv[arg_offset++] = "-e";
			}

			if (use_rsh) {
				nargv[arg_offset++] = RSH_CMD;
			}

			else {
				nargv[arg_offset++] = SSH_CMD;
				nargv[arg_offset++] = SSH_ARG;
			}

			if (getpath (pathbuf, PATH_MAX) && file_exists (pathbuf)) {
				command =
					mkstr ("cd %s; %s %s %s %s/mpispawn 0", wd, ENV_CMD,
						   mpispawn_env, env, pathbuf);
			} else if (use_dirname) {
				command =
					mkstr ("cd %s; %s %s %s %s/mpispawn 0", wd, ENV_CMD,
						   mpispawn_env, env, binary_dirname);
			} else {
				command = mkstr ("cd %s; %s %s %s mpispawn 0", wd, ENV_CMD,
								 mpispawn_env, env);
			}

			if (!command) {
				fprintf (stderr,
						 "Couldn't allocate string for remote command!\n");
				exit (EXIT_FAILURE);
			}

			nargv[arg_offset++] = pglist->data[i].hostname;
			nargv[arg_offset++] = command;
			nargv[arg_offset++] = NULL;

			if (show_on) {
				size_t arg = 0;
				fprintf (stdout, "\n");
				while (nargv[arg] != NULL)
					fprintf (stdout, "%s ", nargv[arg++]);
				fprintf (stdout, "\n");

				exit (EXIT_SUCCESS);
			}

			if (strcmp (pglist->data[i].hostname, plist[0].hostname)) {
				int fd = open ("/dev/null", O_RDWR, 0);
				dup2 (fd, STDIN_FILENO);
			}

			DBG (fprintf (stderr, "final cmd line = %s\n", mpispawn_env));
			execv (nargv[0], (char *const *) nargv);
			perror ("execv");

			for (i = 0; i < argc; i++) {
				fprintf (stderr, "%s ", nargv[i]);
			}

			fprintf (stderr, "\n");

			exit (EXIT_FAILURE);
		}
	}

	if (spawnfile) {
		unlink (spawnfile);
	}
	return;

  allocation_error:
	perror ("spawn_fast");
	if (mpispawn_env) {
		fprintf (stderr, "%s\n", mpispawn_env);
		free (mpispawn_env);
	}

	exit (EXIT_FAILURE);
}

void spawn_one (int argc, char *argv[], char *totalview_cmd, char *env)
{
	char *mpispawn_env, *tmp, *ld_library_path;
	char *name, *value;
	int j, i, n, tmp_i;
	FILE *fp;
	char pathbuf[PATH_MAX];
    char *host_list = NULL;
    int k;

	if ((ld_library_path = getenv ("LD_LIBRARY_PATH"))) {
		mpispawn_env = mkstr ("LD_LIBRARY_PATH=%s:%s",
							  LD_LIBRARY_PATH_MPI, ld_library_path);
	}

	else {
		mpispawn_env = mkstr ("LD_LIBRARY_PATH=%s", LD_LIBRARY_PATH_MPI);
	}

	if (!mpispawn_env)
		goto allocation_error;

	tmp = mkstr ("%s MPISPAWN_MPIRUN_MPD=0", mpispawn_env);
	CHECK_ALLOC ();
	
    tmp = mkstr ("%s USE_LINEAR_SSH=%d", mpispawn_env, USE_LINEAR_SSH);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_HOST=%s", mpispawn_env, mpirun_host);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPIRUN_RSH_LAUNCH=1", mpispawn_env);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_CHECKIN_PORT=%d", mpispawn_env, port);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_PORT=%d", mpispawn_env, port);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_GLOBAL_NPROCS=%d", mpispawn_env, nprocs);
	CHECK_ALLOC ();

	tmp = mkstr ("%s MPISPAWN_MPIRUN_ID=%d", mpispawn_env, getpid ());
	CHECK_ALLOC ();
			
    for (k = 0; k < pglist->npgs; k++) {
        /* Make a list of hosts and the number of processes on each host */
        /* NOTE: RFCs do not allow : or ; in hostnames */
        if (host_list)
            host_list = mkstr ("%s:%s:%d", host_list, 
                    pglist->data[k].hostname, pglist->data[k].npids );
        else 
            host_list = mkstr ("%s:%d", pglist->data[k].hostname, 
                    pglist->data[k].npids );
        if (!host_list) 
            goto allocation_error; 
        for (n = 0; n < pglist->data[k].npids; n++) {
            host_list = mkstr ("%s:%d", host_list, 
                    pglist->data[k].plist_indices[n]);
            if (!host_list) 
                goto allocation_error; 
        }
    }

    tmp = mkstr ("%s MPISPAWN_HOSTLIST=%s", mpispawn_env, host_list);
    CHECK_ALLOC ();
    
    tmp = mkstr ("%s MPISPAWN_NNODES=%d", mpispawn_env, pglist->npgs);
    CHECK_ALLOC ();
	
    if (use_totalview) {
		tmp = mkstr ("%s MPISPAWN_USE_TOTALVIEW=1", mpispawn_env);
		CHECK_ALLOC ();
	}

	/* 
	 * mpirun_rsh allows env variables to be set on the commandline
	 */
	if (!mpispawn_param_env) {
		mpispawn_param_env = mkstr ("");
		if (!mpispawn_param_env)
			goto allocation_error;
	}

	while (aout_index != argc && strchr (argv[aout_index], '=')) {
		name = strdup (argv[aout_index++]);
		value = strchr (name, '=');
		value[0] = '\0';
		value++;

		tmp = mkstr ("%s MPISPAWN_GENERIC_NAME_%d=%s"
					 " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
					 param_count, name, param_count, value);

		free (name);
		free (mpispawn_param_env);

		if (tmp) {
			mpispawn_param_env = tmp;
			param_count++;
		}

		else {
			goto allocation_error;
		}
	}
    
    tmp = mkstr ("%s MPISPAWN_GENERIC_NAME_%d=MV2_XRC_FILE"
            " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env, 
            param_count, param_count, MV2_XRC_FILE);
    if (tmp) {
        mpispawn_param_env = tmp;
        param_count++;
    }
    else 
        goto allocation_error;

	if (aout_index == argc) {
		fprintf (stderr, "Incorrect number of arguments.\n");
		usage ();
		exit (EXIT_FAILURE);
	}

	i = argc - aout_index;
	if (debug_on && !use_totalview)
		i++;

	tmp = mkstr ("%s MPISPAWN_ARGC=%d", mpispawn_env, argc - aout_index);
    CHECK_ALLOC ();

	i = 0;

	if (debug_on && !use_totalview) {
		tmp =
			mkstr ("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++, DEBUGGER);

		CHECK_ALLOC ();
	}

	if (use_totalview) {
		int j;
		for (j = 0; j < MPIR_proctable_size; j++) {
			MPIR_proctable[j].executable_name = argv[aout_index];
		}
	}

	tmp_i = i;

    i = 0;      /* Spawn root mpispawn */
	{
		if (!(pglist->data[i].pid = fork ())) {
			size_t arg_offset = 0;
			const char *nargv[7];
			char *command;

			while (aout_index < argc) {
				tmp = mkstr ("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, tmp_i++, 
                        argv[aout_index++]);
				CHECK_ALLOC ();
			}

			if (mpispawn_param_env) {
				tmp =
					mkstr ("%s MPISPAWN_GENERIC_ENV_COUNT=%d %s",
						   mpispawn_env, param_count, mpispawn_param_env);

				free (mpispawn_param_env);
				free (mpispawn_env);

				if (tmp) {
					mpispawn_env = tmp;
				} else {
					goto allocation_error;
				}
			}

			tmp = mkstr ("%s MPISPAWN_ID=%d", mpispawn_env, i);
			CHECK_ALLOC ();

			tmp = mkstr ("%s MPISPAWN_LOCAL_NPROCS=%d", mpispawn_env,
						 pglist->data[i].npids);
			CHECK_ALLOC ();

			tmp = mkstr ("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, wd);
			CHECK_ALLOC ();

			if (xterm_on) {
				nargv[arg_offset++] = XTERM;
				nargv[arg_offset++] = "-e";
			}

			if (use_rsh) {
				nargv[arg_offset++] = RSH_CMD;
			}

			else {
				nargv[arg_offset++] = SSH_CMD;
				nargv[arg_offset++] = SSH_ARG;
			}
            tmp = mkstr ("%s MPISPAWN_WD=%s", mpispawn_env, wd);
			CHECK_ALLOC ();

            for (j = 0; j < arg_offset; j++) {
                tmp = mkstr ("%s MPISPAWN_NARGV_%d=%s", mpispawn_env, j, 
                        nargv[j]);
                CHECK_ALLOC ();
            }
            tmp = mkstr ("%s MPISPAWN_NARGC=%d", mpispawn_env, 
                    arg_offset);
            CHECK_ALLOC ();

			if (getpath (pathbuf, PATH_MAX) && file_exists (pathbuf)) {
				command =
					mkstr ("cd %s; %s %s %s %s/mpispawn %d", wd, ENV_CMD,
						   mpispawn_env, env, pathbuf, pglist->npgs);
			} else if (use_dirname) {
				command =
					mkstr ("cd %s; %s %s %s %s/mpispawn %d", wd, ENV_CMD,
						   mpispawn_env, env, binary_dirname, pglist->npgs);
			} else {
				command = mkstr ("cd %s; %s %s %s mpispawn %d", wd, ENV_CMD,
								 mpispawn_env, env, pglist->npgs);
			}

			if (!command) {
				fprintf (stderr,
						 "Couldn't allocate string for remote command!\n");
				exit (EXIT_FAILURE);
			}

			nargv[arg_offset++] = pglist->data[i].hostname;
			nargv[arg_offset++] = command;
			nargv[arg_offset++] = NULL;

			if (show_on) {
				size_t arg = 0;
				fprintf (stdout, "\n");
				while (nargv[arg] != NULL)
					fprintf (stdout, "%s ", nargv[arg++]);
				fprintf (stdout, "\n");

				exit (EXIT_SUCCESS);
			}

			if (strcmp (pglist->data[i].hostname, plist[0].hostname)) {
				int fd = open ("/dev/null", O_RDWR, 0);
				dup2 (fd, STDIN_FILENO);
			}
			DBG (fprintf (stderr, "final cmd line = %s\n", mpispawn_env));
			execv (nargv[0], (char *const *) nargv);
			perror ("execv");

			for (i = 0; i < argc; i++) {
				fprintf (stderr, "%s ", nargv[i]);
			}

			fprintf (stderr, "\n");

			exit (EXIT_FAILURE);
		}
	}

	if (spawnfile) {
		unlink (spawnfile);
	}
	return;

  allocation_error:
	perror ("spawn_one");
	if (mpispawn_env) {
		fprintf (stderr, "%s\n", mpispawn_env);
		free (mpispawn_env);
	}

	exit (EXIT_FAILURE);
}

#undef CHECK_ALLOC

void make_command_strings (int argc, char *argv[], char *totalview_cmd,
						   char *command_name, char *command_name_tv)
{
	int i;
	if (debug_on) {
		fprintf (stderr, "debug enabled !\n");
		char keyval_list[COMMAND_LEN];
		sprintf (keyval_list, "%s", " ");
		/* Take more env variables if present */
		while (strchr (argv[aout_index], '=')) {
			strcat (keyval_list, argv[aout_index]);
			strcat (keyval_list, " ");
			aout_index++;
		}
		if (use_totalview) {
			sprintf (command_name_tv, "%s %s %s", keyval_list,
					 totalview_cmd, argv[aout_index]);
			sprintf (command_name, "%s %s ", keyval_list,
					 argv[aout_index]);
		} else {
			sprintf (command_name, "%s %s %s", keyval_list,
					 DEBUGGER, argv[aout_index]);
		}
	} else {
		sprintf (command_name, "%s", argv[aout_index]);
	}

	if (use_totalview) {
		/* Only needed for root */
		strcat (command_name_tv, " -a ");
	}

	/* add the arguments */
	for (i = aout_index + 1; i < argc; i++) {
		strcat (command_name, " ");
		strcat (command_name, argv[i]);
	}

	if (use_totalview) {
		/* Complete the command for non-root processes */
		strcat (command_name, " -mpichtv");

		/* Complete the command for root process */
		for (i = aout_index + 1; i < argc; i++) {
			strcat (command_name_tv, " ");
			strcat (command_name_tv, argv[i]);
		}
		strcat (command_name_tv, " -mpichtv");
	}
}

void nostop_handler (int signal)
{
	printf ("Stopping from the terminal not allowed\n");
}

void alarm_handler (int signal)
{
	extern const char *alarm_msg;

	if (use_totalview) {
		fprintf (stderr, "Timeout alarm signaled\n");
	}

	if (alarm_msg)
		fprintf (stderr, "%s", alarm_msg);
	cleanup ();
}


void child_handler (int signal)
{
	int status, pid;

	while (1) {

		pid = wait (&status);

		/* No children to wait on */
		if ( (pid < 0) && (errno == ECHILD) ) {
			if (legacy_startup)
				close (server_socket);
			exit (EXIT_SUCCESS);
		}

		if ( !WIFEXITED (status) || WEXITSTATUS (status) ) {
			cleanup ();
		}
	}
}

void mpispawn_checkin (int s, struct sockaddr *sockaddr, unsigned int
					   sockaddr_len)
{
	int sock, id, i, n, mpispawn_root;
	in_port_t port;
	socklen_t addrlen;
	struct sockaddr_storage addr, address[pglist->npgs];
	int mt_degree;
	mt_degree = env2int ("MV2_MT_DEGREE");
	if (!mt_degree) {
		mt_degree = ceil (pow (pglist->npgs, (1.0 / (MT_MAX_LEVEL - 1))));
		if (mt_degree < MT_MIN_DEGREE)
			mt_degree = MT_MIN_DEGREE;
		if (mt_degree > MT_MAX_DEGREE)
			mt_degree = MT_MAX_DEGREE;
	} else {
		if (mt_degree < 2) {
			fprintf (stderr, "mpirun_rsh: MV2_MT_DEGREE too low");
			cleanup ();
		}
	}
	for (i = 0; i < NSPAWNS; i++) {
		addrlen = sizeof (addr);

		while ((sock =
				accept (s, (struct sockaddr *) &addr, &addrlen)) < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;

			perror ("accept [mpispawn_checkin]");
			cleanup ();
		}

		if (read_socket (sock, &id, sizeof (int))
			|| read_socket (sock, &pglist->data[id].pid, sizeof (pid_t))
			|| read_socket (sock, &port, sizeof (in_port_t))) {
			cleanup ();
		}

		address[id] = addr;
		((struct sockaddr_in *) &address[id])->sin_port = port;

		if (!(id == 0 && use_totalview))
			close (sock);
		else
			mpispawn_root = sock;

		for (n = 0; n < pglist->data[id].npids; n++) {
			plist[pglist->data[id].plist_indices[n]].state = P_STARTED;
		}
	}
    if (USE_LINEAR_SSH) {
	    sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

	    if (sock < 0) {
	    	perror ("socket [mpispawn_checkin]");
	    	cleanup ();
	    }

	    if (connect (sock, (struct sockaddr *) &address[0],
	    			 sizeof (struct sockaddr)) < 0) {
	    	perror ("connect");
	    	cleanup ();
	    }

	    /*
	     * Send address array to address[0] (mpispawn with id 0).  The mpispawn
	     * processes will propagate this information to each other after connecting
	     * in a tree like structure.
	     */
	    if (write_socket (sock, &pglist->npgs, sizeof (pglist->npgs))
	    	|| write_socket (sock, &address, sizeof (addr) * pglist->npgs)
	    	|| write_socket (sock, &mt_degree, sizeof (int))) {
	    	cleanup ();
	    }

	    close (sock);
    }
	if (use_totalview) {
		int id, j;
		process_info_t *pinfo = (process_info_t *) malloc
			(process_info_s * nprocs);
		read_socket (mpispawn_root, pinfo, process_info_s * nprocs);
		for (j = 0; j < nprocs; j++) {
			MPIR_proctable[pinfo[j].rank].pid = pinfo[j].pid;
		}
		free (pinfo);
		/* We're ready for totalview */
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		MPIR_Breakpoint ();

		/* MPI processes can proceed now */
		id = 0;
		write_socket (mpispawn_root, &id, sizeof (int));
		if (USE_LINEAR_SSH)
            close (mpispawn_root);
	}
}

#define TEST_STR(_key_, _val_, _set_) do {            \
    if(0 == strncmp(_key_, #_val_, strlen(#_val_))) { \
        type = _set_;                                 \
    }                                                 \
} while(0)

int check_token (FILE * fp, char *tmpbuf)
{
	char *tokptr, *val, *key;
	int type = 0;

	TEST_STR (tmpbuf, nprocs, 7);
	TEST_STR (tmpbuf, execname, 1);
	TEST_STR (tmpbuf, preput_key_, 1);
	TEST_STR (tmpbuf, totspawns, 2);
	TEST_STR (tmpbuf, endcmd, 3);
	TEST_STR (tmpbuf, preput_val_, 4);
	TEST_STR (tmpbuf, arg, 5);
	TEST_STR (tmpbuf, argcnt, 6);
    TEST_STR (tmpbuf, info_num, 8);
    TEST_STR (tmpbuf, info_key_, 9);
    TEST_STR (tmpbuf, info_val_, 10);

	switch (type) {
	case 4:
    case 10:                /* info_val_ */
		tokptr = strtok (tmpbuf, "=");
		key = strtok (NULL, "=");
		fprintf (fp, "%s\n", key);
		return 0;
	case 1:					/* execname, preput.. */
	case 7:
    case 9:                 /* info_key_ */
		tokptr = strtok (tmpbuf, "=");
		val = strtok (NULL, "=");
		fprintf (fp, "%s\n", val);
		if (type == 7) {
			spinf.launch_num = atoi (val);
		}
		return 0;
	case 2:					/* totspawns */
		tokptr = strtok (tmpbuf, "=");
		spinf.totspawns = atoi (strtok (NULL, "="));
		return 0;
	case 3:					/* endcmd */
		fprintf (fp, "endcmd\n");
		spinf.spawnsdone = spinf.spawnsdone + 1;
		if (spinf.spawnsdone >= spinf.totspawns)
			return 1;
		return 0;
	case 8:                 /* info num */
        fprintf (fp, "%s\n", tmpbuf);
		return 0;
	case 5:					/* arguments */
		fprintf (fp, "%s\n", tmpbuf);
		return 0;
	case 6:					/* args end */
		fprintf (fp, "endarg\n");
		return 0;
	}

	return 0;
}
#undef TEST_STR


void handle_spawn_req (int readsock)
{
	FILE *fp;
	int done = 0;
	char *chptr, *hdptr;
	uint32_t size, spcnt;

	bzero (&spinf, sizeof (struct spawn_info_t));

	sprintf (spinf.linebuf, TMP_PFX "%d_spawn_spec.file", getpid ());
	spinf.spawnfile = strdup (spinf.linebuf);

	fp = fopen (spinf.linebuf, "w");
	if (NULL == fp) {
		fprintf (stderr, "temp file could not be created\n");
	;}

	read_socket (readsock, &spcnt, sizeof (uint32_t));
	read_socket (readsock, &size, sizeof (uint32_t));
	hdptr = chptr = malloc (size);
	read_socket (readsock, chptr, size);

	do {
		get_line (chptr, spinf.linebuf, 0);
		chptr = chptr + strlen (spinf.linebuf) + 1;
		done = check_token (fp, spinf.linebuf);
	} while (!done);

	fsync (fileno (fp));
	fclose (fp);
	free (hdptr);
	launch_newmpirun (spcnt);
	return;

	DBG (perror ("Fatal error:"));
	return;
}

static void get_line (void *ptr, char *fill, int is_file)
{
	int i = 0, ch;
	FILE *fp;
	char *buf;

	if (is_file) {
		fp = ptr;
		while ((ch = fgetc (fp)) != '\n') {
			fill[i] = ch;
			++i;
		}
	} else {
		buf = ptr;
		while (buf[i] != '\n') {
			fill[i] = buf[i];
			++i;
		}
	}
	fill[i] = '\0';
}

void launch_newmpirun (int total)
{
	FILE *fp;
	int i, j;
	char *newbuf;

	if (!hostfile_on) {
		sprintf (spinf.linebuf, TMP_PFX "%d_hostlist.file", getpid ());
		fp = fopen (spinf.linebuf, "w");
		for (j = 0, i = 0; i < total; i++) {
			fprintf (fp, "%s\n", plist[j].hostname);
			j = (j + 1) % nprocs;
		}
		fclose (fp);
	} else
		strcpy (spinf.linebuf, hostfile);

	sprintf (spinf.buf, "%d", total);

	if (fork ())
		return;

	newbuf = (char *) malloc (PATH_MAX + MAXLINE);
	strcpy (newbuf, binary_dirname);
	strcat (newbuf, "/mpirun_rsh");
	DBG (fprintf (stderr, "launching %s\n", newbuf));
	DBG (fprintf (stderr, "numhosts = %s, hostfile = %s, spawnfile = %s\n",
				  spinf.buf, spinf.linebuf, spinf.spawnfile));

    if(dpmenv) {
    	execl (newbuf, newbuf, "-np", spinf.buf, "-hostfile", spinf.linebuf,
	    	   "-spawnfile", spinf.spawnfile, "-dpm", dpmenv, NULL);
    } else {
    	execl (newbuf, newbuf, "-np", spinf.buf, "-hostfile", spinf.linebuf,
	    	   "-spawnfile", spinf.spawnfile, "-dpm", NULL);
    }
    
	perror ("execl failed\n");
	exit (EXIT_FAILURE);
}

static int check_info(char *str)
{
    if(0 == (strcmp(str, "wdir")))
        return 1;
    if(0 == (strcmp(str, "path")))
        return 1;
    return 0;
}

static void store_info(char *key, char *val)
{
    if(0 == (strcmp(key, "wdir")))
        custwd = strdup(val);
    
    if(0 == (strcmp(key, "path")))
        custpath = strdup(val);
}

#if 0
void create_paramfile()
{
    char bufname[128];
    FILE *fp;

    if(dpmenv == NULL)
        return;

    if(dpmparamfile == NULL) {
        sprintf(bufname, "%sparamfile_%d_%d", TMP_PFX, getpid(), (rand()%512));
        dpmparamfile = strdup(bufname);
        fp = fopen(bufname, "w");
        fprintf(fp, "%s", dpmenv);
        fclose(fp);
    } 
}
#endif

#define ENVLEN 20480
void dpm_add_env(char *buf, char *optval)
{
    int len;

    if(dpmenv == NULL) {
        dpmenv = (char *)malloc(ENVLEN);
        dpmenv[0]= '\0';
        dpmenvlen = ENVLEN;
    } 
    
    if(optval == NULL) {
        strcat(dpmenv, buf);
        strcat(dpmenv, " ");
    } else {
        strcat(dpmenv, buf);
        strcat(dpmenv, "=");
        strcat(dpmenv, optval);
        strcat(dpmenv, " ");
    }
}
#undef ENVLEN

#ifdef CKPT

static void *CR_Loop(void *arg)
{
    struct timeval starting, now, tv;
    int time_counter;

    char cr_msg_buf[MAX_CR_MSG_LEN];
    char valstr[CRU_MAX_VAL_LEN];
    char buf[CR_MAX_FILENAME];

    fd_set set;
    int i, n, nfd = 0, ret;

    cr_checkpoint_args_t   cr_file_args;
    cr_checkpoint_handle_t cr_handle;

    while (1) {
        CR_MUTEX_LOCK;
        if (cr_state == CR_INIT) {
            CR_MUTEX_UNLOCK;
            sleep(1);
        }
        else {
            CR_MUTEX_UNLOCK;
            break;
        }
    }

    gettimeofday(&starting, NULL);
    starting_time = last_ckpt = starting.tv_sec;

    while(1)
    {
        if (restart_context) break;

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        CR_MUTEX_LOCK;
        if (cr_state == CR_FINALIZED) {
            CR_MUTEX_UNLOCK;
            break;
        }
        CR_MUTEX_UNLOCK;

        FD_ZERO(&set);
        for (i=0; i<NSPAWNS; i++) {
            FD_SET(mpirun_fd[i], &set);
            nfd = (nfd >= mpirun_fd[i]) ? nfd : mpirun_fd[i];
        }
        nfd += 1;

        ret = select(nfd, &set, NULL, NULL, &tv);

        if ( (ret < 0) && (errno != EINTR) && (errno != EBUSY) ) {
            perror("[CR_Loop] select()");
            return((void *)-1);
        }
        else if (ret > 0) {

            CR_MUTEX_LOCK;
            if (cr_state != CR_READY) {
                CR_MUTEX_UNLOCK;
                continue;
            }
            CR_MUTEX_UNLOCK;

            for (i=0; i<NSPAWNS; i++) {

                if (!FD_ISSET(mpirun_fd[i], &set))
                    continue;

                n = CR_MPDU_readline(mpirun_fd[i], cr_msg_buf, MAX_CR_MSG_LEN);
                if (n == 0) continue;

                if (CR_MPDU_parse_keyvals(cr_msg_buf) < 0) break;

                CR_MPDU_getval("cmd", valstr, CRU_MAX_VAL_LEN);

                if (strcmp(valstr,"app_ckpt_req")==0) {
                    if (enable_sync_ckpt == 0)
                        continue;
                    CR_MUTEX_LOCK;
                    sprintf(buf,"%s.%d.sync", ckpt_filename, checkpoint_count+1);
                    cr_initialize_checkpoint_args_t(&cr_file_args);
                    cr_file_args.cr_scope   = CR_SCOPE_PROC;
                    cr_file_args.cr_target  = getpid();
                    cr_file_args.cr_fd      = open(buf, O_CREAT | O_WRONLY | O_TRUNC , 0666);
                    cr_file_args.cr_signal  = 0;
                    cr_file_args.cr_timeout = 0;
                    cr_file_args.cr_flags   &= ~CR_CHKPT_DUMP_ALL; // Save None
                    cr_request_checkpoint(&cr_file_args, &cr_handle);
                    cr_poll_checkpoint(&cr_handle, NULL);
                    last_ckpt = now.tv_sec;
                    CR_MUTEX_UNLOCK;
                }
                else if (strcmp(valstr, "finalize_ckpt") == 0) {
                    CR_MUTEX_LOCK;
                    cr_state = CR_FINALIZED;
                    CR_MUTEX_UNLOCK;
                    break;
                }
            }
        }
        else {

            CR_MUTEX_LOCK;

            gettimeofday(&now, NULL);
            time_counter = (now.tv_sec - starting_time);

            if ( (checkpoint_interval > 0) &&
                 (now.tv_sec != last_ckpt) &&
                 (cr_state == CR_READY)    &&
                 (time_counter%checkpoint_interval == 0) )
            {
                /* Inject a checkpoint */
                if ( (max_ckpts == 0) || (max_ckpts > checkpoint_count) ) {
                    sprintf(buf, "%s.%d.auto", ckpt_filename, checkpoint_count+1);
                    cr_initialize_checkpoint_args_t(&cr_file_args);
                    cr_file_args.cr_scope   = CR_SCOPE_PROC;
                    cr_file_args.cr_target  = getpid();
                    cr_file_args.cr_fd      = open(buf, O_CREAT | O_WRONLY | O_TRUNC , 0666);
                    cr_file_args.cr_signal  = 0;
                    cr_file_args.cr_timeout = 0;
                    cr_file_args.cr_flags   &= ~CR_CHKPT_DUMP_ALL; // Save None
                    cr_request_checkpoint(&cr_file_args, &cr_handle);
                    cr_poll_checkpoint(&cr_handle, NULL);
                    last_ckpt = now.tv_sec;
                }

                /* Remove the ealier checkpoints */
                if ( (max_save_ckpts > 0) && (max_save_ckpts < checkpoint_count+1) ) {
                    sprintf(buf, "%s.%d.auto", ckpt_filename, checkpoint_count+1-max_save_ckpts);
                    unlink(buf);
                }
            }

            CR_MUTEX_UNLOCK;
        }
    }

    return(0);
}

static int CR_Callback(void *arg)
{
    int ret, i;
    int Progressing, nfd = 0;
    char buf[MAX_CR_MSG_LEN];
    char val[CRU_MAX_VAL_LEN];
    struct timeval now;
    fd_set set;

    if (cr_state != CR_READY) {
        cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
        fprintf(stderr, "[CR_Callback] CR Subsystem not ready\n");
        return(0);
    }

    gettimeofday(&now,NULL);
    last_ckpt = now.tv_sec;

    checkpoint_count++;
    cr_state = CR_CHECKPOINT;

    sprintf(buf, "cmd=ckpt_req file=%s\n", ckpt_filename);

    for (i=0; i<NSPAWNS; i++)
        CR_MPDU_writeline(mpirun_fd[i], buf);

    /* Wait for Checkpoint to finish */
    Progressing = nprocs;
    while (Progressing) {

        FD_ZERO(&set);
        for (i=0; i<NSPAWNS; i++) {
            FD_SET(mpirun_fd[i], &set);
            nfd = (nfd >= mpirun_fd[i]) ? nfd : mpirun_fd[i];
        }
        nfd += 1;

        ret = select(nfd, &set, NULL, NULL, NULL);

        if (ret < 0) {
            perror("[CR_Callback] select()");
            cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
            return(-1);
        }

        for (i=0; i<NSPAWNS; i++) {

            if (!FD_ISSET(mpirun_fd[i], &set))
                continue;

            CR_MPDU_readline(mpirun_fd[i], buf, MAX_CR_MSG_LEN);

            if (CR_MPDU_parse_keyvals(buf) < 0) {
                fprintf(stderr, "[CR_Callback] CR_MPDU_parse_keyvals() failed\n");
                cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
                return(-2);
            }

            CR_MPDU_getval("result", val, CRU_MAX_VAL_LEN);

            if (strcmp(val, "succeed") == 0) {
                --Progressing;
                continue;
            }

            if (strcmp(val, "finalize_ckpt") == 0) {
                /* MPI Process finalized */
                cr_state = CR_FINALIZED;
                return(0);
            }

            if (strcmp(val,"fail") == 0) {
                fprintf(stderr, "[CR_Callback] Checkpoint of a Process Failed\n");
                fflush(stderr);
                cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
                return(-3);
            }
        }
    } /* while(Progressing) */

    ret = cr_checkpoint(CR_CHECKPOINT_READY);

    if (ret < 0) {
        fprintf(stderr, "[CR_Callback] Checkpoint of Console Failed\n");
        fflush(stderr);
        cr_state = CR_READY;
        return(-4);
    }
    else if (ret == 0) {
        cr_state = CR_READY;
    }
    else if (ret)   {

        restart_context = 1;
        cr_state = CR_RESTART;

    }

    return(0);
}

#endif /* CKPT */

/* vi:set sw=4 sts=4 tw=76 expandtab: */
