/* Copyright (c) 2002-2008, The Ohio State University. All rights
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
#include "mpirun_util.h"
#include "mpispawn_tree.h"

typedef struct {
    char * viadev_device;
    char * viadev_default_port;
    char * mpirun_rank;
} lvalues;

pid_t * pids = NULL;
size_t npids = 0;

int N;
int NCHILD;
int *mpispawn_fds;
int NCHILD_INCL;
int ROOT_FD;

static in_port_t c_port;
child_t *children;

static inline int env2int(char * env_ptr) {
    return (env_ptr = getenv(env_ptr)) ? atoi(env_ptr) : 0;
}

static inline char * env2str(char * env_ptr) {
    return (env_ptr = getenv(env_ptr)) ? strdup(env_ptr) : NULL;
}

void mpispawn_abort (int abort_code)
{
    int sock, id=env2int ("MPISPAWN_ID");
    sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        /* Oops! */
        perror ("socket");
        exit (EXIT_FAILURE);
    }
    
    struct sockaddr_in sockaddr;
    struct hostent *mpirun_hostent;
    mpirun_hostent = gethostbyname(env2str("MPISPAWN_MPIRUN_HOST"));
    if (NULL == mpirun_hostent) {
        /* Oops! */
        herror ("gethostbyname");
        exit (EXIT_FAILURE);
    }
    
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
    sockaddr.sin_port = htons(env2int("MPISPAWN_CHECKIN_PORT"));
    
    while (connect(sock, (struct sockaddr *) &sockaddr,
		    sizeof(sockaddr)) < 0);
    if (sock) {
        write_socket (sock, &abort_code, sizeof (int));
        write_socket (sock, &id, sizeof (int));
        close (sock);
    }
    cleanup ();
}

lvalues get_lvalues(int i) {
    lvalues v;
    char * buffer = NULL;

    buffer = mkstr("MPISPAWN_VIADEV_DEVICE_%d", i);
    if(!buffer) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    v.viadev_device = env2str(buffer);
    free(buffer);

    buffer = mkstr("MPISPAWN_VIADEV_DEFAULT_PORT_%d", i);
    if(!buffer) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    v.viadev_default_port = env2str(buffer);
    free(buffer);

    buffer = mkstr("MPISPAWN_MPIRUN_RANK_%d", i);
    if(!buffer) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    v.mpirun_rank = env2str(buffer);
    free(buffer);

    return v;
}

void setup_global_environment() {
    char my_host_name[MAX_HOST_LEN+MAX_PORT_LEN];

    int i = env2int("MPISPAWN_GENERIC_ENV_COUNT");

    setenv("MPIRUN_MPD", "0", 1);
    setenv("MPIRUN_NPROCS", getenv("MPISPAWN_GLOBAL_NPROCS"), 1);
    setenv("MPIRUN_ID", getenv("MPISPAWN_MPIRUN_ID"), 1);

    /* Ranks now connect to mpispawn */ 
    gethostname (my_host_name, MAX_HOST_LEN);
    
    sprintf (my_host_name, "%s:%d", my_host_name, c_port);
    
    setenv ("PMI_PORT", my_host_name, 2);

    if(env2int("MPISPAWN_USE_TOTALVIEW")) {
	setenv("MPIRUN_PROCESSES", getenv("MPISPAWN_MPIRUN_PROCESSES"), 1);
    }

    else {
	setenv("NOT_USE_TOTALVIEW", "1", 1);
    }

    while(i--) {
	char * buffer, * name, * value;

	buffer = mkstr("MPISPAWN_GENERIC_NAME_%d", i);
	if(!buffer) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

	name = env2str(buffer);
	if(!name) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

	free(buffer);

	buffer = mkstr("MPISPAWN_GENERIC_VALUE_%d", i);
	if(!buffer) { 
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

	value = env2str(buffer);
	if(!value) {
        fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

	setenv(name, value, 1);

	free(name);
	free(value);
    }
}

void setup_local_environment(lvalues lv) {
    if(lv.viadev_device != NULL) {
	setenv("VIADEV_DEVICE", lv.viadev_device, 1);
    }

    if(atoi(lv.viadev_default_port) != -1) {
	setenv("VIADEV_DEFAULT_PORT", lv.viadev_default_port, 1);
    }

    setenv("PMI_ID", lv.mpirun_rank, 1);
}

void spawn_processes(int n) {
    int i, j;
    npids = n;
    pids = malloc(sizeof(pid_t) * n);

    if(!pids) {
	perror("malloc");
	exit(EXIT_FAILURE);
    }

    for(i = 0; i < n; i++) {
	pids[i] = fork();

	if(pids[i] == 0) {
	    int argc, nwritten, tv_offset = 0;
	    char ** argv, buffer[80];
	    lvalues lv = get_lvalues(i);

	    setup_local_environment(lv);

	    argc = env2int("MPISPAWN_ARGC");

	    if(lv.mpirun_rank == 0 && env2int("MPISPAWN_USE_TOTALVIEW")) {
		tv_offset = 1;
	    }

	    argv = malloc(sizeof(char *) * (argc + tv_offset + 1));
	    if(!argv) {
            fprintf (stderr, "%s:%d Insufficient memory\n", __FILE__, __LINE__);
            exit(EXIT_FAILURE);
        }

	    argv[argc + tv_offset] = NULL;
	    j = argc + tv_offset;

	    while(argc--) {
		nwritten = snprintf(buffer, 80, "MPISPAWN_ARGV_%d", argc);
		if(nwritten < 0 || nwritten > 80) {
            fprintf (stderr, "%s:%d Overflow\n", __FILE__, __LINE__);
            exit(EXIT_FAILURE);
        }

		argv[argc + tv_offset] = env2str(buffer);
	    }

	    if(tv_offset) {
		argv[0] = argv[1];
		argv[1] = strdup("-a");
	    }

            execv(argv[0], argv);
            perror("execv");

	    for(i = 0; i < j; i++) {
		fprintf(stderr, "%s ", argv[i]);
	    }

	    fprintf(stderr, "\n");

	    exit(EXIT_FAILURE);
	}
    }
}

void cleanup(void) {
    int i;
    for (i = 0; i < npids; i++) {
	kill(pids[i], SIGINT);
    }

    sleep(1);

    for (i = 0; i < npids; i++) {
	kill(pids[i], SIGTERM);
    }

    sleep(1);

    for (i = 0; i < npids; i++) {
	kill(pids[i], SIGKILL);
    }

    free(pids);
    free (children);
    exit(EXIT_FAILURE);
}

void cleanup_handler(int sig) {
    printf("Signal %d received.\n", sig);
	mpispawn_abort (MPISPAWN_PROCESS_ABORT);
}

void child_handler(int signal) {
    static int num_exited = 0;
    int status, pid;

    while(1) {
	pid = waitpid(-1, &status, WNOHANG);
	if(pid == 0) break;

	if(pid != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
	    if(++num_exited == npids) {
		free(pids);
        free (children);

		exit(WEXITSTATUS(status));
	    }
	}

	else {
        fprintf (stderr, "MPI process terminated unexpectedly\n");
	    mpispawn_abort (MPISPAWN_PROCESS_ABORT);
	}
    }
}

void mpispawn_checkin(int id, in_port_t l_port) 
{
    int mpirun_socket, connect_attempt = 0, max_connect_attempts = 5;
    struct hostent *mpirun_hostent;
    struct sockaddr_in sockaddr;
    pid_t pid = getpid();
    int port = env2int("MPISPAWN_CHECKIN_PORT");

    mpirun_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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
    sockaddr.sin_port = htons(port);

    while(connect(mpirun_socket, (struct sockaddr *) &sockaddr,
		sizeof(sockaddr)) < 0) {
	if(++connect_attempt > max_connect_attempts) {
	    perror("connect");
	    exit(EXIT_FAILURE);
	}
    }

    if(write_socket(mpirun_socket, &id, sizeof(int))) {
	fprintf(stderr, "Error writing id [%d]!\n", id);
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    if(write_socket(mpirun_socket, &pid, sizeof(pid_t))) {
	fprintf(stderr, "Error writing pid [%d]!\n", pid);
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    if(write_socket(mpirun_socket, &l_port, sizeof(in_port_t))) {
	fprintf(stderr, "Error writing l_port!\n");
	close(mpirun_socket);
	exit(EXIT_FAILURE);
    }

    close(mpirun_socket);
}

in_port_t init_listening_socket(int *mc_socket) {
    struct sockaddr_in mc_sockaddr;
    socklen_t mc_sockaddr_len = sizeof(mc_sockaddr);

    *mc_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(*mc_socket < 0) {
	perror("socket");
	exit(EXIT_FAILURE);
    }

    mc_sockaddr.sin_addr.s_addr = INADDR_ANY;
    mc_sockaddr.sin_port = 0;

    if(bind(*mc_socket, (struct sockaddr *)&mc_sockaddr, mc_sockaddr_len) < 0) {
	    perror("bind");
	    exit(EXIT_FAILURE);
    }

    if(getsockname(*mc_socket, (struct sockaddr *)&mc_sockaddr, 
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
WFE:
    while ((wfe_socket = accept(s, sockaddr, &sockaddr_len)) < 0) {
	    if (errno == EINTR || errno == EAGAIN) 
            continue;
	    perror("accept");
        mpispawn_abort (MPISPAWN_RANK_ERROR);
    }

    if(read_socket(wfe_socket, &wfe_abort_code, sizeof(int))
            || read_socket(wfe_socket, &wfe_abort_rank, sizeof(int))
            || read_socket(wfe_socket, &wfe_abort_msglen, sizeof(int))) {
        fprintf(stderr, "Termination socket read failed!\n");
        mpispawn_abort (MPISPAWN_RANK_ERROR);
    }
    else {
        char wfe_abort_message[wfe_abort_msglen];
	    fprintf (stderr, "Abort signaled by rank %d: ", wfe_abort_rank);
	    if (!read_socket(wfe_socket, &wfe_abort_message, wfe_abort_msglen)) 
	        fprintf(stderr, "%s\n", wfe_abort_message); 
        mpispawn_abort (MPISPAWN_RANK_ERROR);
    }
    goto WFE;
}

fd_set child_socks;
extern int MPISPAWN_HAS_PARENT;
extern int MPISPAWN_NCHILD;
#define MPISPAWN_PARENT_FD mpispawn_fds[0]
#define MPISPAWN_CHILD_FDS (&mpispawn_fds[MPISPAWN_HAS_PARENT])


int main(int argc, char *argv[]) {
    struct sigaction signal_handler;
    int l_socket, id = env2int("MPISPAWN_ID"), i;
    in_port_t l_port = init_listening_socket(&l_socket);

    int c_socket;
    struct sockaddr_in c_sockaddr;
    unsigned int sockaddr_len = sizeof (c_sockaddr);
    int mt_degree;

    FD_ZERO (&child_socks);
    
    NCHILD  = env2int ("MPISPAWN_LOCAL_NPROCS");
    N = env2int ("MPISPAWN_GLOBAL_NPROCS");
    children = (child_t *) malloc (NCHILD * child_s);

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
    if (getsockname (c_socket, (struct sockaddr *) &c_sockaddr, &sockaddr_len) 
            < 0) {
        perror ("getsockname");
        exit (EXIT_FAILURE);
    }
    listen (c_socket, NCHILD);
    c_port = (int) ntohs (c_sockaddr.sin_port);

    setup_global_environment();

    if(chdir(getenv("MPISPAWN_WORKING_DIR"))) {
	perror("chdir");
	exit(EXIT_FAILURE);
    }

    mpispawn_checkin (id, l_port);
    
    mt_degree = mpispawn_tree_init(id, l_socket);
    if (mt_degree == -1)
        exit (EXIT_FAILURE);
   
    spawn_processes (NCHILD);
    
    for (i = 0; i < NCHILD; i ++) {
        int sock;
ACCEPT_HID:
        sock = accept (c_socket, (struct sockaddr *) &c_sockaddr, 
                &sockaddr_len);
        if (sock < 0) {
            if ((errno == EINTR) || (errno == EAGAIN))
                goto ACCEPT_HID;
            perror ("accept");
            return (EXIT_FAILURE);
        }

        children[i].fd = sock;
        children[i].rank = 0;
        children[i].c_barrier = 0;
    }
   
    mpispawn_fds = mpispawn_tree_connect (0, mt_degree);
   
    if (NULL == mpispawn_fds) {
        return EXIT_FAILURE;
    }
    mtpmi_init ();
    mtpmi_processops ();
    wait_for_errors(c_socket, (struct sockaddr *)&c_sockaddr, sockaddr_len);
    
    /* Should never get here */
    return EXIT_FAILURE;
}

/* vi:set sw=4 sts=4 tw=80: */
