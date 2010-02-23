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
#endif

static int restart_context;
static int checkpoint_count;

static char *sessionid;
static char session_file[CR_MAX_FILENAME];

static char ckpt_filename[CR_MAX_FILENAME];

static int   CR_Init(int);
#ifndef CR_FTB
static void* CR_Worker(void *);
static int   Connect_MPI_Procs(int);

extern char* CR_MPDU_getval(const char *, char *, int);
extern int   CR_MPDU_parse_keyvals(char *);
extern int   CR_MPDU_readline(int , char *, int);
extern int   CR_MPDU_writeline(int , char *);
#endif

#endif /* CKPT */

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

void cleanup (void);

void mpispawn_abort (int abort_code)
{
	int sock, id = env2int ("MPISPAWN_ID");
	sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	int connect_attempt = 0, max_connect_attempts = 5;
	struct sockaddr_in sockaddr;
	struct hostent *mpirun_hostent;
	if (sock < 0) {
		/* Oops! */
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	mpirun_hostent = gethostbyname (env2str ("MPISPAWN_MPIRUN_HOST"));
	if (NULL == mpirun_hostent) {
		/* Oops! */
		herror ("gethostbyname");
		exit (EXIT_FAILURE);
	}

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
	sockaddr.sin_port = htons (env2int ("MPISPAWN_CHECKIN_PORT"));

	while (connect (sock, (struct sockaddr *) &sockaddr,
					sizeof (sockaddr)) < 0) {
		if (++connect_attempt > max_connect_attempts) {
			perror ("connect");
			exit (EXIT_FAILURE);
		}
	}
	if (sock) {
		write_socket (sock, &abort_code, sizeof (int));
		write_socket (sock, &id, sizeof (int));
		close (sock);
	}
	cleanup ();
}

lvalues get_lvalues (int i)
{
	lvalues v;
	char *buffer = NULL;
    if (USE_LINEAR_SSH) {
	    buffer = mkstr ("MPISPAWN_MPIRUN_RANK_%d", i);
	    if (!buffer) {
	    	fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
	    			 __LINE__);
	    	exit (EXIT_FAILURE);
	    }

	    v.mpirun_rank = env2str (buffer);
	    free (buffer);
    }
    else
        v.mpirun_rank = mkstr ("%d", ranks[mt_id][i]);
	return v;
}

void setup_global_environment ()
{
	char my_host_name[MAX_HOST_LEN + MAX_PORT_LEN];

	int i = env2int ("MPISPAWN_GENERIC_ENV_COUNT");

	setenv ("MPIRUN_MPD", "0", 1);
	setenv ("MPIRUN_NPROCS", getenv ("MPISPAWN_GLOBAL_NPROCS"), 1);
	setenv ("MPIRUN_ID", getenv ("MPISPAWN_MPIRUN_ID"), 1);

	/* Ranks now connect to mpispawn */
	gethostname (my_host_name, MAX_HOST_LEN);

	sprintf (my_host_name, "%s:%d", my_host_name, c_port);

	setenv ("PMI_PORT", my_host_name, 2);

	if (env2int ("MPISPAWN_USE_TOTALVIEW")) {
		setenv ("USE_TOTALVIEW", "1", 1);
	} else {
		setenv ("USE_TOTALVIEW", "0", 1);
	}

	while (i--) {
		char *buffer, *name, *value;

		buffer = mkstr ("MPISPAWN_GENERIC_NAME_%d", i);
		if (!buffer) {
			fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
					 __LINE__);
			exit (EXIT_FAILURE);
		}

		name = env2str (buffer);
		if (!name) {
			fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
					 __LINE__);
			exit (EXIT_FAILURE);
		}

		free (buffer);

		buffer = mkstr ("MPISPAWN_GENERIC_VALUE_%d", i);
		if (!buffer) {
			fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
					 __LINE__);
			exit (EXIT_FAILURE);
		}

		value = env2str (buffer);
		if (!value) {
			fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
					 __LINE__);
			exit (EXIT_FAILURE);
		}

#ifdef CKPT
		if (strcmp(name, "MV2_CKPT_FILE") == 0)
			strncpy(ckpt_filename, value, CR_MAX_FILENAME);
#endif /* CKPT */

		setenv (name, value, 1);

		free (name);
		free (value);
	}
}

void setup_local_environment (lvalues lv)
{
	setenv ("PMI_ID", lv.mpirun_rank, 1);

#ifdef CKPT
	setenv("MV2_CKPT_FILE", ckpt_filename, 1);
	setenv("MV2_CKPT_SESSIONID", sessionid, 1);

        /* Setup MV2_CKPT_MPD_BASE_PORT for legacy reasons */
	setenv("MV2_CKPT_MPD_BASE_PORT", "0", 1);
#endif

}

void spawn_processes (int n)
{

	int i, j;
	npids = n;
	local_processes = (process_info_t *) malloc (process_info_s * n);

	if (!local_processes) {
		perror ("malloc");
		exit (EXIT_FAILURE);
	}

	for (i = 0; i < n; i++) {
		local_processes[i].pid = fork ();

		if (local_processes[i].pid == 0) {

#ifdef CKPT
			char **cr_argv;
			char str[32];
			int rank;

			if (restart_context) {

				cr_argv = (char **) malloc(sizeof(char *) * 3);
				if (!cr_argv) {
					perror("malloc(cr_argv)");
					exit(EXIT_FAILURE);
				}

				cr_argv[0] = malloc(sizeof(char) * (strlen(CR_RESTART_CMD) + 1));
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
				snprintf(str, 32, "MPISPAWN_MPIRUN_RANK_%d", i);
				rank = atoi(getenv(str));
				snprintf(cr_argv[1], CR_MAX_FILENAME, "%s.%d.%d", ckpt_filename, checkpoint_count, rank);

				cr_argv[2] = NULL;

				execvp(CR_RESTART_CMD, cr_argv);

				perror("[CR Restart] execv");
				fflush(stderr);
				exit(EXIT_FAILURE);
			}
#endif
			int argc, nwritten;
			char **argv, buffer[80];
			lvalues lv = get_lvalues (i);

			setup_local_environment (lv);

			argc = env2int ("MPISPAWN_ARGC");


			argv = malloc (sizeof (char *) * (argc + 1));
			if (!argv) {
				fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
						 __LINE__);
				exit (EXIT_FAILURE);
			}

			argv[argc] = NULL;
			j = argc;

			while (argc--) {
				nwritten = snprintf (buffer, 80, "MPISPAWN_ARGV_%d", argc);
				if (nwritten < 0 || nwritten > 80) {
					fprintf (stderr, "%s:%d Overflow\n", __FILE__,
							 __LINE__);
					exit (EXIT_FAILURE);
				}  
            
                /* if executable is not in working directory */
                if(argc == 0 && getenv("MPISPAWN_BINARY_PATH")) {
                    char *tmp = env2str(buffer);
                    if(tmp[0] != '/') {
                        snprintf(buffer, 80, "%s/%s", getenv("MPISPAWN_BINARY_PATH"),
                                 tmp);
                    }
                    free(tmp);
    				argv[argc] = strdup (buffer);
                } else
    				argv[argc] = env2str (buffer);
			}
            

			execv (argv[0], argv);
			perror ("execv");

			for (i = 0; i < j; i++) {
				fprintf (stderr, "%s ", argv[i]);
			}

			fprintf (stderr, "\n");

			exit (EXIT_FAILURE);
		} else {

			char *buffer;
			buffer = mkstr ("MPISPAWN_MPIRUN_RANK_%d", i);
			if (!buffer) {
				fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__,
						 __LINE__);
				exit (EXIT_FAILURE);
			}
			local_processes[i].rank = env2int (buffer);
			free (buffer);
		}
	}
}

void cleanup (void)
{

#ifdef CKPT
#ifdef CR_FTB
	/* Nothing to be done */
#else
	pthread_kill(worker_tid, SIGTERM);
	pthread_join(worker_tid, NULL);
#endif
	unlink(session_file);
#endif

	int i;
	for (i = 0; i < npids; i++) {
		kill (local_processes[i].pid, SIGINT);
	}
    if (!USE_LINEAR_SSH)
    for (i = 0; i < MPISPAWN_NCHILD; i++) {
		kill (mpispawn_pids[i], SIGINT);
    }

	sleep (1);

	for (i = 0; i < npids; i++) {
		kill (local_processes[i].pid, SIGTERM);
	}
    if (!USE_LINEAR_SSH)
    for (i = 0; i < MPISPAWN_NCHILD; i++) {
		kill (mpispawn_pids[i], SIGTERM);
    }

	sleep (1);

	for (i = 0; i < npids; i++) {
		kill (local_processes[i].pid, SIGKILL);
	}
    if (!USE_LINEAR_SSH)
    for (i = 0; i < MPISPAWN_NCHILD; i++) {
		kill (mpispawn_pids[i], SIGKILL);
    }

	free (local_processes);
	free (children);
	exit (EXIT_FAILURE);
}

void cleanup_handler (int sig)
{
	mpispawn_abort (MPISPAWN_PROCESS_ABORT);
}

void child_handler (int signal)
{
	static int num_exited = 0;
	int status, pid, rank, i;
	char my_host_name[MAX_HOST_LEN];

	while (1) {
		pid = waitpid (-1, &status, WNOHANG);
		if (pid == 0) return;

		if ( ( (pid > 0) && WIFEXITED (status) && WEXITSTATUS (status) == 0) ||
                     ( (pid < 0) && (errno == ECHILD) ) ) {
			if(++num_exited == npids) {
#ifdef CKPT
				unlink(session_file);
#endif
				exit (EXIT_SUCCESS);
			}
		}
		else {
            rank = -1;
            gethostname (my_host_name, MAX_HOST_LEN);
	        for (i = 0; i < npids; i++) {
                if (pid == local_processes[i].pid) {
                    rank = local_processes[i].rank;
                }
	        }
            if (rank != -1) {
			    fprintf (stderr, "MPI process (rank: %d) terminated "
                        "unexpectedly on %s\n",  rank, my_host_name);
            }
            else {
			    fprintf (stderr, "Process terminated "
                        "unexpectedly on %s\n", my_host_name);
            }
            fflush (stderr);
			mpispawn_abort (MPISPAWN_PROCESS_ABORT);
		}
	}
}

void mpispawn_checkin (in_port_t l_port)
{
	int connect_attempt = 0, max_connect_attempts = 5, i, sock;
	struct hostent *mpirun_hostent;
	struct sockaddr_in sockaddr, c_sockaddr;
    int offset = 0, id;
	pid_t pid = getpid ();
	int port;

    if (!USE_LINEAR_SSH) {
        if (mt_id != 0) {
            offset = 1;
	        MPISPAWN_HAS_PARENT = 1;
        }
        mpispawn_fds = (int *) malloc (sizeof (int) * (MPISPAWN_NCHILD + 
                MPISPAWN_HAS_PARENT));
        if (MPISPAWN_NCHILD) {
            mpispawn_pids = (pid_t *) malloc (sizeof (pid_t) * 
                    MPISPAWN_NCHILD);
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
                while ((sock = accept (checkin_sock, NULL, 0)) < 0) {
	    		    if (errno == EINTR || errno == EAGAIN)
	    			    continue;
                    perror ("accept [mt_checkin]");
                }
                mpispawn_fds [i+offset] = sock;
                if (read_socket (sock, &id, sizeof (int)) ||
	    		        read_socket (sock, &mpispawn_pids[i], 
                        sizeof (pid_t)) || read_socket (sock, &port, 
                        sizeof (in_port_t))) {
	    		    cleanup ();
	    	    }

            } 
        }
    }
	mpirun_socket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!USE_LINEAR_SSH && mt_id != 0)
        mpispawn_fds[0] = mpirun_socket;
	if (mpirun_socket < 0) {
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	mpirun_hostent = gethostbyname (getenv ("MPISPAWN_MPIRUN_HOST"));
	if (mpirun_hostent == NULL) {
		herror ("gethostbyname");
		exit (EXIT_FAILURE);
	}

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
	sockaddr.sin_port = htons (env2int ("MPISPAWN_CHECKIN_PORT"));

	while (connect (mpirun_socket, (struct sockaddr *) &sockaddr,
					sizeof (sockaddr)) < 0) {
		if (++connect_attempt > max_connect_attempts) {
			perror ("connect [mt_checkin]");
			exit (EXIT_FAILURE);
		}
	}

	if (write_socket (mpirun_socket, &mt_id, sizeof (int))) {
		fprintf (stderr, "Error writing id [%d]!\n", mt_id);
		close (mpirun_socket);
		exit (EXIT_FAILURE);
	}

	if (write_socket (mpirun_socket, &pid, sizeof (pid_t))) {
		fprintf (stderr, "Error writing pid [%d]!\n", pid);
		close (mpirun_socket);
		exit (EXIT_FAILURE);
	}

	if (write_socket (mpirun_socket, &l_port, sizeof (in_port_t))) {
		fprintf (stderr, "Error writing l_port!\n");
		close (mpirun_socket);
		exit (EXIT_FAILURE);
	}
	
    if (USE_LINEAR_SSH && !(mt_id == 0 && env2int ("MPISPAWN_USE_TOTALVIEW")))
		close (mpirun_socket);
}

in_port_t init_listening_socket (int *mc_socket)
{
	struct sockaddr_in mc_sockaddr;
	socklen_t mc_sockaddr_len = sizeof (mc_sockaddr);

	*mc_socket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (*mc_socket < 0) {
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	mc_sockaddr.sin_addr.s_addr = INADDR_ANY;
	mc_sockaddr.sin_port = 0;

	if (bind
		(*mc_socket, (struct sockaddr *) &mc_sockaddr,
		 mc_sockaddr_len) < 0) {
		perror ("bind");
		exit (EXIT_FAILURE);
	}

	if (getsockname (*mc_socket, (struct sockaddr *) &mc_sockaddr,
					 &mc_sockaddr_len) < 0) {
		perror ("getsockname");
		exit (EXIT_FAILURE);
	}

	listen (*mc_socket, MT_MAX_DEGREE);

	return mc_sockaddr.sin_port;
}

void wait_for_errors (int s, struct sockaddr *sockaddr, unsigned int
					  sockaddr_len)
{
	int wfe_socket, wfe_abort_code, wfe_abort_rank, wfe_abort_msglen;

  WFE:
	while ((wfe_socket = accept (s, sockaddr, &sockaddr_len)) < 0) {
		if (errno == EINTR || errno == EAGAIN)
			continue;
		perror ("accept");
		mpispawn_abort (MPISPAWN_RANK_ERROR);
	}

	if (read_socket (wfe_socket, &wfe_abort_code, sizeof (int))
		|| read_socket (wfe_socket, &wfe_abort_rank, sizeof (int))
		|| read_socket (wfe_socket, &wfe_abort_msglen, sizeof (int))) {
		fprintf (stderr, "Termination socket read failed!\n");
		mpispawn_abort (MPISPAWN_RANK_ERROR);
	} else {
		char wfe_abort_message[wfe_abort_msglen];
		fprintf (stderr, "Abort signaled by rank %d: ", wfe_abort_rank);
		if (!read_socket
			(wfe_socket, &wfe_abort_message, wfe_abort_msglen))
			fprintf (stderr, "%s\n", wfe_abort_message);
		mpispawn_abort (MPISPAWN_RANK_ERROR);
	}
	goto WFE;
}


/*Obtain the host_ist from a file. This function is used when the number of
 * processes is beyond the threshold. */
char*  obtain_host_list_from_file( ) {

        //Obtain id of the host file and number of byte to read
        //Number of bytes sent when it is used the file approach to exachange
        //the host_list
        int num_bytes;
        FILE *fp;
        char *host_list_file = NULL,*host_list=NULL;

        host_list_file = env2str("HOST_LIST_FILE");
        num_bytes = env2int("HOST_LIST_NBYTES");

        fp = fopen (host_list_file, "r");
        if ( fp == NULL ) {
               
	     fprintf ( stderr, "host list temp file could not be read\n");
        }

        host_list = malloc(num_bytes);
        fscanf(fp, "%s",host_list);
        fclose(fp);
        return host_list;
}



fd_set child_socks;
#define MPISPAWN_PARENT_FD mpispawn_fds[0]
#define MPISPAWN_CHILD_FDS (&mpispawn_fds[MPISPAWN_HAS_PARENT])
#define ENV_CMD		    "/usr/bin/env"
#define MAX_HOST_LEN 256

extern char **environ;
int main (int argc, char *argv[])
{

	struct sigaction signal_handler;
	int l_socket, i;
	in_port_t l_port = init_listening_socket (&l_socket);

	int c_socket;
	struct sockaddr_in c_sockaddr, checkin_sockaddr;
	unsigned int sockaddr_len = sizeof (c_sockaddr);
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
    int s, port;

	FD_ZERO (&child_socks);

    mt_id = env2int ("MPISPAWN_ID");
    mt_nnodes = env2int ("MPISPAWN_NNODES");
    USE_LINEAR_SSH = env2int ("USE_LINEAR_SSH");
	
    NCHILD = env2int ("MPISPAWN_LOCAL_NPROCS");
	N = env2int ("MPISPAWN_GLOBAL_NPROCS");
	children = (child_t *) malloc (NCHILD * child_s);

	portname = getenv ("PARENT_ROOT_PORT_NAME");
	if (portname) {
		add_kvc ("PARENT_ROOT_PORT_NAME", portname, 1);
	}

	signal_handler.sa_handler = cleanup_handler;
	sigfillset (&signal_handler.sa_mask);
	signal_handler.sa_flags = 0;

	sigaction (SIGHUP, &signal_handler, NULL);
	sigaction (SIGINT, &signal_handler, NULL);
	sigaction (SIGTERM, &signal_handler, NULL);

	signal_handler.sa_handler = child_handler;
	sigemptyset (&signal_handler.sa_mask);

	sigaction (SIGCHLD, &signal_handler, NULL);

    /* Create listening socket for ranks */
	/* Doesn't need to be TCP as we're all on local node */
	c_socket = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (c_socket < 0) {
		perror ("socket");
		exit (EXIT_FAILURE);
	}
	c_sockaddr.sin_addr.s_addr = INADDR_ANY;
	c_sockaddr.sin_port = 0;

	if (bind (c_socket, (struct sockaddr *) &c_sockaddr, sockaddr_len) < 0) {
		perror ("bind");
		exit (EXIT_FAILURE);
	}
	if (getsockname
		(c_socket, (struct sockaddr *) &c_sockaddr, &sockaddr_len)
		< 0) {
		perror ("getsockname");
		exit (EXIT_FAILURE);
	}
	listen (c_socket, NCHILD);
	c_port = (int) ntohs (c_sockaddr.sin_port);

#ifdef CKPT
        CR_Init(NCHILD);
#endif

    n = atoi (argv[1]);

    if (!USE_LINEAR_SSH) {
	    checkin_sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	    if (checkin_sock < 0) {
	    	perror ("socket");
	    	exit (EXIT_FAILURE);
	    }
	    checkin_sockaddr.sin_addr.s_addr = INADDR_ANY;
	    checkin_sockaddr.sin_port = 0;
	    if (bind (checkin_sock, (struct sockaddr *) &checkin_sockaddr, 
                    sockaddr_len) < 0) {
	    	perror ("bind");
	    	exit (EXIT_FAILURE);
	    }
	    if (getsockname (checkin_sock, (struct sockaddr *) &checkin_sockaddr, 
                    &sockaddr_len) < 0) {
	    	perror ("getsockname");
	    	exit (EXIT_FAILURE);
	    }
	    port = (int) ntohs (checkin_sockaddr.sin_port);
	    listen (checkin_sock, 64);
	    char *mpmd_on = env2str ("MPISPAWN_MPMD");
	    nargc = env2int ("MPISPAWN_NARGC");
	    for (i = 0; i < nargc; i++) {
	    	char buf[20];
	    	sprintf (buf, "MPISPAWN_NARGV_%d", i);
	    	nargv[i] = env2str (buf);
	    }


	    host_list = env2str ("MPISPAWN_HOSTLIST");


		//If the number of processes is beyond or equal the PROCS_THRES it
		//receives the host list in a file
        if (host_list == NULL) {
            host_list = obtain_host_list_from_file( );
        }

   
        command = mkstr ("cd %s; %s", env2str ("MPISPAWN_WD"), ENV_CMD);
        
        gethostname (hostname, MAX_HOST_LEN);
        mpispawn_env = mkstr ("MPISPAWN_MPIRUN_HOST=%s"
                " MPISPAWN_CHECKIN_PORT=%d MPISPAWN_MPIRUN_PORT=%d", 
                hostname, port, port);

        i = 0;
        while (environ[i] != NULL) {
            char *var, *val;
            char *dup = strdup (environ[i]);
            var = strtok (dup, "=");
            val = strtok (NULL, "=");
            if (val &&
                0 != strcmp (var, "MPISPAWN_ID") &&
                0 != strcmp (var, "MPISPAWN_LOCAL_NPROCS") && 
                0 != strcmp (var, "MPISPAWN_MPIRUN_HOST") && 
                0 != strcmp (var, "MPISPAWN_CHECKIN_PORT") && 
                0 != strcmp (var, "MPISPAWN_MPIRUN_PORT"))
            {

            	if (strchr (val, ' ') != NULL)
            	{
            			mpispawn_env = mkstr ("%s %s='%s'", mpispawn_env, var, val);

            	}
                else
                {
                    /*If mpmd is selected the name and args of the executable are written in the HOST_LIST, not in the
                     * MPISPAWN_ARGV and MPISPAWN_ARGC. So the value of these varibles is not exact and we don't
                     * read this value.*/
                    if (mpmd_on)
                    {
                    	if (strstr(var, "MPISPAWN_ARGV_") == NULL  && strstr (var, "MPISPAWN_ARGC") == NULL)
						{

							mpispawn_env = mkstr ( "%s %s=%s", mpispawn_env, var, val );
						}
                    }
                    else
                    	mpispawn_env = mkstr ("%s %s=%s", mpispawn_env, var, val);
                    }
            }

            free (dup);
            i++;
        }

		args = mkstr ("%s", argv[0]);
		for (i = 1; i < argc - 1; i++) {
			args = mkstr ("%s %s", args, argv[i]);
		}
		nargv[nargc + 2] = NULL;

        host = (char **) malloc (mt_nnodes * sizeof (char*));
        np = (int *) malloc (mt_nnodes * sizeof (int));
        ranks = (int **) malloc (mt_nnodes * sizeof (int *));
        /* These three variables are used to collect information on name, args and number of args in case of mpmd*/
        char **exe = (char **) malloc (mt_nnodes * sizeof (char*));
        char ***args_exe = (char ***) malloc (mt_nnodes * sizeof (char *)* sizeof (char *));
        int *num_args = (int *) malloc (mt_nnodes * sizeof (int));

        i = mt_nnodes;
        j = 0;
        int glb_ind = 0;
		while (i > 0) {
			if (i == mt_nnodes)
				host[j] = strtok (host_list, ":");
			else
				host[j] = strtok (NULL, ":");
			np[j] = atoi (strtok (NULL, ":"));
				ranks[j] = (int *) malloc (np[j] * sizeof (int));
			for (k = 0; k < np[j]; k++) {
				ranks[j][k] = atoi (strtok (NULL, ":"));
			}
			/*If mpmd is selected the executable name and the arguments are written in the hostlist.
			 * So we need to read these information from the hostlist.*/
			if (mpmd_on)
			{
				exe[j] = strtok (NULL, ":");
				num_args[j] = atoi (strtok (NULL, ":"));
				if ( num_args[j] >1 )
				{
					k = 0;
					while ( k < num_args[j]-1 )
					{
						args_exe[j][k] = strtok (NULL, ":");
						k++;
					}
				}
			}

			i--;
			j++;
		}


        /* Launch mpispawns */ 

        while (n > 1) 
        {
            target = mt_id + ceil (n / 2.0);
            /*If mpmd is selected we need to add the MPISPAWN_ARGC and MPISPAWN_ARGV to the mpispwan
             * environment using the information we have read in the host_list.*/
            if (mpmd_on)
            {
            	//We need to add MPISPAWN_ARGV
            	mpispawn_env = mkstr ("%s MPISPAWN_ARGC=%d", mpispawn_env, num_args[target]);
            	mpispawn_env = mkstr ("%s MPISPAWN_ARGV_0=%s", mpispawn_env, exe[target]);
            	for(i=1; i<num_args[target]; i++)
            	{
            		mpispawn_env = mkstr ("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i, args_exe[target][i-1]);
            	}
            }

            nargv[nargc] = host[target];

            MPISPAWN_NCHILD ++; 
            if (0 == fork ())
            {
                mpispawn_env = mkstr ("%s MPISPAWN_ID=%d MPISPAWN_LOCAL_NPROCS=%d", 
                        mpispawn_env, target, np[target]);
                command = mkstr ("%s %s %s %d", command, mpispawn_env, args, n/2);

                nargv[nargc + 1] = command;

                execv (nargv[0], (char *const *) nargv);
                perror ("execv");
            }
            else 
                n = ceil (n / 2.0);
        }
    } /* if (!USE_LINEAR_SSH) */

	setup_global_environment ();

	if (chdir (getenv ("MPISPAWN_WORKING_DIR"))) {
		perror ("chdir");
		exit (EXIT_FAILURE);
	}

	mpispawn_checkin (l_port);

    if (USE_LINEAR_SSH) {
	    mt_degree = mpispawn_tree_init (mt_id, l_socket);
	    if (mt_degree == -1)
		    exit (EXIT_FAILURE);
	}

	spawn_processes (NCHILD);

	for (i = 0; i < NCHILD; i++) {
		int sock;
ACCEPT_HID:
		sock = accept (c_socket, (struct sockaddr *) &c_sockaddr,
					   &sockaddr_len);
		if (sock < 0) {
            printf ("%d", errno);
			if ((errno == EINTR) || (errno == EAGAIN)) {
				goto ACCEPT_HID;
            }
            else {
			    perror ("accept");
			    return (EXIT_FAILURE);
            }
		}

		children[i].fd = sock;
		children[i].rank = 0;
		children[i].c_barrier = 0;
	}

    if (USE_LINEAR_SSH)	{
        mpispawn_fds = mpispawn_tree_connect (0, mt_degree);

	    if (NULL == mpispawn_fds) {
		    return EXIT_FAILURE;
	    }
    }
	mtpmi_init ();
	mtpmi_processops ();
	wait_for_errors (c_socket, (struct sockaddr *) &c_sockaddr,
					 sockaddr_len);

	/* Should never get here */
	return EXIT_FAILURE;
}

#ifdef CKPT

static int CR_Init(int nProcs)
{
    char *temp;
    struct hostent* hp;
    struct sockaddr_in sa;

    int mpirun_port;

    temp = getenv("MPISPAWN_MPIRUN_CR_PORT");
    if (temp) {
        mpirun_port = atoi(temp);
    }
    else {
        fprintf(stderr, "[CR_Init] MPISPAWN_MPIRUN_CR_PORT unknown\n");
        exit(EXIT_FAILURE);
    }

    temp = getenv("MPISPAWN_CR_CONTEXT");
    if (temp) {
        restart_context = atoi(temp);
    }
    else {
        fprintf(stderr, "[CR_Init] MPISPAWN_CR_CONTEXT unknown\n");
        exit(EXIT_FAILURE);
    }

    sessionid = getenv("MPISPAWN_CR_SESSIONID");
    if (!sessionid) {
        fprintf(stderr, "[CR_Init] MPISPAWN_CR_SESSIONID unknown\n");
        exit(EXIT_FAILURE);
    }

    snprintf(session_file, CR_MAX_FILENAME, "/tmp/cr.session.%s", sessionid);

    temp = getenv("MPISPAWN_CR_CKPT_CNT");
    if (temp) {
        checkpoint_count = atoi(temp);
    }
    else {
        fprintf(stderr, "[CR_Init] MPISPAWN_CR_CKPT_CNT unknown\n");
        exit(EXIT_FAILURE);
    }

#ifdef CR_FTB

    char pmi_port[MAX_HOST_LEN+MAX_PORT_LEN];
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
    if (fwrite(pmi_port, sizeof(pmi_port), 1, fp) == 0) {
        fprintf(stderr, "[CR_Init] Cannot write PMI Port number\n");
        fflush(stderr);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

#else

    strncpy(ckpt_filename, DEFAULT_CHECKPOINT_FILENAME, CR_MAX_FILENAME);

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

    bzero((void*) &sa, sizeof(sa));
    bcopy((void*) hp->h_addr, (void*) &sa.sin_addr, hp->h_length);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(mpirun_port);

    if (connect(mpirun_fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
        perror("[CR_Init] connect()");
        exit(EXIT_FAILURE);
    }

    mpispawn_fd = malloc(nProcs * sizeof(int));
    if (!mpispawn_fd) {
        perror("[CR_Init] malloc()");
        exit(EXIT_FAILURE);
    }

    /* Spawn CR Worker Thread */
    if (pthread_create(&worker_tid, NULL, CR_Worker, (void *)(uintptr_t) nProcs)) {
        perror("[CR_Init] pthread_create()");
        exit(EXIT_FAILURE);
    }

    /* Wait for Connect_MPI_Procs() to start listening */
    while(!cr_cond);

#endif /* CR_FTB */

    return(0);
}

#ifndef CR_FTB
/* To be used only when not relying on FTB */

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

    if (bind(mpispawn_listen_fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
        perror("[Connect_MPI_Procs] bind()");
        exit(EXIT_FAILURE);
    }

    i = sizeof(sa);
    if (getsockname(mpispawn_listen_fd, (struct sockaddr *) &sa, (socklen_t *) &i) < 0) {
        perror("[Connect_MPI_Procs] getsockname()");
        close(mpispawn_listen_fd);
        exit(EXIT_FAILURE);
    }

    mpispawn_port = ntohs(sa.sin_port);

    fp = fopen(session_file, "w+");
    if (!fp) {
        fprintf(stderr, "[Connect_MPI_Procs] Cannot create Session File\n");
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

    for (i=0; i<nProcs; i++) {
        if ((mpispawn_fd[i] = accept(mpispawn_listen_fd, 0, 0)) < 0) {
            perror("[Connect_MPI_Procs] accept()");
            exit(EXIT_FAILURE);
        }
    }

    close(mpispawn_listen_fd);

    return(0);
}

static void *CR_Worker(void *arg)
{
    int ret, i, nProcs;
    char cr_msg_buf[MAX_CR_MSG_LEN];
    fd_set set;
    int max_fd;

    nProcs = (int)(uintptr_t) arg;

    Connect_MPI_Procs(nProcs);

    while(1) {

        FD_ZERO(&set);
        FD_SET(mpirun_fd, &set);
        max_fd = mpirun_fd;

        for (i=0; i<nProcs; i++) {
            FD_SET(mpispawn_fd[i], &set);
            max_fd = (max_fd >= mpispawn_fd[i]) ? max_fd : mpispawn_fd[i];
        }

        max_fd += 1;

        select(max_fd, &set, NULL, NULL, NULL);

        if (FD_ISSET(mpirun_fd, &set)) {

            /* We need to send a message from mpirun_rsh -> MPI Processes */

            ret = CR_MPDU_readline(mpirun_fd, cr_msg_buf, MAX_CR_MSG_LEN);
            if (!ret) continue;
            for (i = 0; i < nProcs; i++)
                CR_MPDU_writeline(mpispawn_fd[i], cr_msg_buf);

        } else {

            /* We need to send a message from MPI Processes -> mpirun_rsh */

            for (i=0; i<nProcs; i++) {
                if (FD_ISSET(mpispawn_fd[i], &set))
                break;
            }

            ret = CR_MPDU_readline(mpispawn_fd[i], cr_msg_buf, MAX_CR_MSG_LEN);
            if (!ret) continue;

            /* Received a PMI Port Query */
            if (strstr(cr_msg_buf, "query_pmi_port")) {
                snprintf(cr_msg_buf, MAX_CR_MSG_LEN,
                         "cmd=reply_pmi_port val=%s\n", getenv("PMI_PORT"));
                CR_MPDU_writeline(mpispawn_fd[i], cr_msg_buf);
                continue;
            }

            CR_MPDU_writeline(mpirun_fd, cr_msg_buf);

            /* Received a Finalize Checkpoint message */
            if (strstr(cr_msg_buf, "finalize_ckpt")) {
                return(0);
            }
                
        }

    } /* while(1) */

}

#endif /* not CR_FTB */

#endif /* CKPT */

/* vi:set sw=4 sts=4 tw=80: */
