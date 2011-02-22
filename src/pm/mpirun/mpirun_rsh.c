
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

#include <mpirunconf.h>
#include "mpirun_rsh.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "mpispawn_tree.h"
#include "mpirun_util.h"
#include "mpmd.h"
#include "mpirun_dbg.h"
#include "mpirun_params.h"
#include "mpirun_ckpt.h"
#include <param.h>
#include <mv2_config.h>



#if defined(_NSIG)
#define NSIG _NSIG
#endif                /* defined(_NSIG) */

/*
 * When an error occurs in the init phase, mpirun_rsh doesn't have the pid
 * of all the mpispawns and in the cleanup it doesn't kill the mpispawns. To
 * solve this problem, we need to wait that it has all the right pids before
 * cleaning up the mpispawn. These two variables wait_socks_succ and
 * socket_error are used to keep trace of this situation.
 */

int wait_socks_succ = 0;
int socket_error = 0;


void spawn_one(int argc, char *argv[], char *totalview_cmd, char *env,
           int fastssh_nprocs_thres);

process_groups *pglist = NULL;
int totalprocs = 0;
char *TOTALPROCS;
int port;
char *wd;            /* working directory of current process */
char *custpath, *custwd;
#define MAX_HOST_LEN 256
char mpirun_host[MAX_HOST_LEN];    /* hostname of current process */

/* xxx need to add checking for string overflow, do this more carefully ... */

int NSPAWNS;
char *dpmenv;
int dpmenvlen;
int dpm_cnt = 0;
/*List of pids of the mpirun started by mpirun_rsh with dpm.*/
list_pid_mpirun_t * dpm_mpirun_pids = NULL;

#if defined(CKPT) && defined(CR_AGGRE)
int use_aggre = 1; // by default we use CR-aggregation
int use_aggre_mig = 0; 
#endif

//#define dbg(fmt, args...)   do{ \
//    fprintf(stderr,"%s: [mpirun]: "fmt, __func__, ##args); fflush(stderr); } while(0)
#define dbg(fmt, args...) 

/*struct spawn_info_t {
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
*/
spawn_info_t spinf;


/*
 * Message notifying user of what timed out
 */
static const char *alarm_msg = NULL;

void free_memory(void);
void pglist_print(void);
void pglist_insert(const char *const, const int);
void rkill_fast(void);
void rkill_linear(void);
void spawn_fast(int, char *[], char *, char *);
void spawn_linear(int, char *[], char *, char *);
void cleanup_handler(int);
void nostop_handler(int);
void alarm_handler(int);
void child_handler(int);
void usage(void);
void cleanup(void);
char *skip_white(char *s);
int read_param_file(char *paramfile, char **env);
void wait_for_mpispawn(int s, struct sockaddr_in *sockaddr,
               unsigned int sockaddr_len);
int set_fds(fd_set * rfds, fd_set * efds);
void make_command_strings(int argc, char *argv[], char *totalview_cmd,
              char *command_name, char *command_name_tv);
void mpispawn_checkin(int, struct sockaddr *, unsigned int);
void handle_spawn_req(int readsock);
void launch_newmpirun(int total);
static void get_line(void *buf, char *fill, int buf_or_file);
static void store_info(char *key, char *val);
static int check_info(char *str);
void dpm_add_env(char *, char *);

/*
#define SH_NAME_LEN    (128)
char sh_cmd[SH_NAME_LEN];
    if (tmp) {
        free (mpispawn_env);
        mpispawn_env = tmp;
    }
    else
    {
        goto allocation_error;
    }
*/

int server_socket;

char *binary_name;

#define DISPLAY_STR_LEN 200
char display[DISPLAY_STR_LEN];

static void get_display_str()
{
    char *p;
    /* char str[DISPLAY_STR_LEN]; */

    p = getenv("DISPLAY");
    if (p != NULL) {
    /* For X11 programs */
    assert(DISPLAY_STR_LEN > strlen("DISPLAY=") + strlen(p));
        snprintf( display, DISPLAY_STR_LEN, "DISPLAY=%s", p );
    }
}



//Define the max len of a pid
#define MAX_PID_LEN 22

/* The name of the file where to write the host_list. Used when the number
 * of processes is beyond NPROCS_THRES */
char *host_list_file = NULL;

/* This function is called at exit and remove the temporary file with the
 * host_list information*/
void remove_host_list_file()
{
    if (host_list_file) {
    if (unlink(host_list_file) < 0) {
        fprintf(stderr, "ERROR removing the %s\n", host_list_file);
    }
    free(host_list_file);
    host_list_file = NULL;
    }
}

void    dump_pgrps()
{
    int i;
    for(i=0; i< pglist->npgs; i++)
    {
        dbg( "pg_%d@%s: local_proc %d : rem_proc %d, has %d pids\n", i,
          pglist->data[i].hostname, pglist->data[i].local_pid, 
            pglist->data[i].pid, pglist->data[i].npids );
    }
}

int lookup_exit_pid(pid_t pid)
{
    int rv = -1;
    if( pid<0 ){
        dbg("invalid pid %d\n", pid);
        return rv;
    }
    int i;
    for(i=0; i< pglist->npgs; i++)
    {
        if( pid == pglist->data[i].local_pid )
        {
            dbg("proc exit: local_proc %d:%d for %s\n", 
                i, pid, pglist->data[i].hostname );
            return i;
        } 
    }
    dbg("exit pid %d cannot found\n", pid);
    return rv;
}

int main(int argc, char *argv[])
{
    int i, s;
    int ret;
    struct sockaddr_in sockaddr;
    unsigned int sockaddr_len = sizeof(sockaddr);
    unsigned long crc;

    char *env = "\0";

    char totalview_cmd[TOTALVIEW_CMD_LEN];

    int timeout, fastssh_threshold;

    atexit(remove_host_list_file);
    atexit(free_memory);

    totalview_cmd[TOTALVIEW_CMD_LEN - 1] = '\0';
    display[0] = '\0';

    if (read_configuration_files(&crc)) {
        fprintf(stderr, "mpirun_rsh: error reading configuration file\n");
        return EXIT_FAILURE;
    }

#ifdef CKPT
    int ret_ckpt;

    if (( ret_ckpt = ckptInit()) < 0 )
        return ret_ckpt;
restart_from_ckpt:
#endif                /* CKPT */

    commandLine(argc, argv, totalview_cmd, &env);

#ifdef CKPT
    set_ckpt_nprocs(nprocs);
#endif

    if (use_totalview) 
    {
        MPIR_proctable = (struct MPIR_PROCDESC *)
        malloc(MPIR_PROCDESC_s * nprocs);
        MPIR_proctable_size = nprocs;

        for (i = 0; i < nprocs; ++i) {
            MPIR_proctable[i].host_name = plist[i].hostname;
        }
    }

    wd = get_current_dir_name();
    gethostname(mpirun_host, MAX_HOST_LEN);

    get_display_str();

    server_socket = s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
    }
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = 0;
    if (bind(s, (struct sockaddr *) &sockaddr, sockaddr_len) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
    }

    if (getsockname(s, (struct sockaddr *) &sockaddr, &sockaddr_len) < 0) {
        perror("getsockname");
        exit(EXIT_FAILURE);
    }

    port = (int) ntohs(sockaddr.sin_port);

    listen(s, nprocs);

    if (!show_on) {
        struct sigaction signal_handler;
        signal_handler.sa_handler = cleanup_handler;
        sigfillset(&signal_handler.sa_mask);
        signal_handler.sa_flags = 0;

        sigaction(SIGHUP, &signal_handler, NULL);
        sigaction(SIGINT, &signal_handler, NULL);
        sigaction(SIGTERM, &signal_handler, NULL);

        signal_handler.sa_handler = nostop_handler;

        sigaction(SIGTSTP, &signal_handler, NULL);

        signal_handler.sa_handler = alarm_handler;

        sigaction(SIGALRM, &signal_handler, NULL);

        signal_handler.sa_handler = child_handler;
        sigemptyset(&signal_handler.sa_mask);

        sigaction(SIGCHLD, &signal_handler, NULL);
    }

    for (i = 0; i < nprocs; i++) {
    /* I should probably do some sort of hostname lookup to account for
     * situations where people might use two different hostnames for the
     * same host.
     */
        pglist_insert(plist[i].hostname, i);
    }

#if defined(CKPT) && defined(CR_FTB)
    dbg("****************  enabled CR_FTB && CKPT  ****************\n");
#endif
#if defined(CKPT) && defined(CR_FTB)
    if (sparehosts_on) 
    {
        /* Read Hot Spares */
        ret = read_sparehosts(sparehostfile, &sparehosts, &nsparehosts);
        if (ret) {
             DBG(fprintf(stderr, "Error Reading Spare Hosts (%d)\n", ret));
             exit(EXIT_FAILURE);
        }
        /* Add Hot Spares to pglist */
        for (i = 0; i < nsparehosts; i++) {
            pglist_insert(sparehosts[i], -1);
            dbg("sparehosts[%d] = %s\n", i, sparehosts[i] );
        }
    }

#endif /* CR_FTB */
    
    /*
     * Set alarm for mpirun initialization on MV2_MPIRUN_TIMEOUT if set.
     * Otherwise set to default value based on size of job and whether
     * debugging is set.
     */
    timeout = env2int("MV2_MPIRUN_TIMEOUT");

    if (timeout <= 0) {
    timeout = pglist ? pglist->npgs : nprocs;
    if (timeout < 30) {
        timeout = 30;
    } else if (timeout > 1000) {
        timeout = 1000;
    }
    }

    alarm_msg = "Timeout during client startup.\n";
    if (!debug_on) {
    alarm(timeout);
    }


    fastssh_threshold = env2int("MV2_FASTSSH_THRESHOLD");

    if (!fastssh_threshold)
    fastssh_threshold = 1 << 8;

#ifndef CKPT
    //Another way to activate hiearachical ssh is having a number of nodes
    //beyond a threshold
    if (pglist->npgs >= fastssh_threshold) {
        USE_LINEAR_SSH = 0;
    }
#endif

    USE_LINEAR_SSH = dpm ? 1 : USE_LINEAR_SSH;

    /*
     * Check to see of ckpt variables are in the environment
     */
#ifdef CKPT
    save_ckpt_vars_env();
#ifdef CR_AGGRE 
    if (getenv("MV2_CKPT_USE_AGGREGATION")) {
        use_aggre = atoi(getenv("MV2_CKPT_USE_AGGREGATION"));
    }
#endif
#endif

    if (USE_LINEAR_SSH) 
    {
        NSPAWNS = pglist->npgs;
        DBG(fprintf(stderr, "USE_LINEAR = %d \n", USE_LINEAR_SSH));

#ifdef CKPT
        if (!show_on) {
            create_connections(NSPAWNS);
        }
        /* if (!show_on) */
#endif                /* CKPT */
        spawn_fast(argc, argv, totalview_cmd, env);

#ifdef CKPT
        if (!show_on) {
            close_connections(NSPAWNS);
        }
        /* if (!show_on) */
#endif                /* CKPT */

    } 
    else 
    {
        NSPAWNS = 1;
        //Search if the number of processes is set up as a environment
        int fastssh_nprocs_thres = env2int("MV2_NPROCS_THRESHOLD");
        if (!fastssh_nprocs_thres)
            fastssh_nprocs_thres = 1 << 13;

        spawn_one(argc, argv, totalview_cmd, env, fastssh_nprocs_thres);
    }

    if (show_on)
        exit(EXIT_SUCCESS);

    mpispawn_checkin(server_socket, (struct sockaddr *) &sockaddr, sockaddr_len);

    /*
     * Disable alarm since mpirun has initialized.
     */
    //pglist_print();
    alarm(0);
    dump_pgrps();
    /*
     * Activate new alarm for mpi job based on MPIEXEC_TIMEOUT if set.
     */
    timeout = env2int("MPIEXEC_TIMEOUT");
    if (timeout > 0 && !debug_on) {
    alarm_msg = mkstr("Timeout [TIMEOUT = %d seconds].\n", timeout);
    alarm(timeout);
    }

    wait_for_mpispawn(server_socket, &sockaddr, sockaddr_len);
    
#ifdef CKPT
    dbg(" after wait_for_spawn: cached_restart_context=%d\n", cached_restart_context);
    finalize_ckpt();
    if(cached_restart_context)
        goto restart_from_ckpt;
#endif /* CKPT */

    /*
     * This return should never be reached.
     */
    return EXIT_FAILURE;
}


/**
 *
 */
void wait_for_mpispawn(int s, struct sockaddr_in *sockaddr,
               unsigned int sockaddr_len)
{
    int wfe_socket, wfe_mpispawn_code, wfe_abort_mid;

  listen:

#ifdef CKPT
    if (restart_context)
    return;
#endif

    while ((wfe_socket = accept(s, (struct sockaddr *) sockaddr, &sockaddr_len)) < 0) 
    {
#ifdef CKPT
        dbg("%s: got wfe %d\n", __func__, wfe_socket );
        if (restart_context)
            return;
#endif

        if (errno == EINTR || errno == EAGAIN)
            continue;
        perror("accept");
        cleanup();
    }

wait_for_non_spare:

    if (read_socket(wfe_socket, &wfe_mpispawn_code, sizeof(int))
            || read_socket(wfe_socket, &wfe_abort_mid, sizeof(int))) {
        fprintf(stderr, "Termination socket read failed!\n");

    } else if (wfe_mpispawn_code == MPISPAWN_DPM_REQ) {
        DBG(fprintf(stderr, "Dynamic spawn request from %d\n", wfe_abort_mid));
        handle_spawn_req(wfe_socket);
        dpm_cnt++;
        close(wfe_socket);
        goto listen;

    } else {
        fprintf(stderr, "Exit code %d signaled from %s\n",
        wfe_mpispawn_code, pglist->index[wfe_abort_mid]->hostname);
    }

#if defined(CKPT) && defined(CR_FTB)
      if(sparehosts_on && !strcmp(current_spare_host,
            pglist->index[wfe_abort_mid]->hostname))
      {
            dbg("Not Exited, only spare node!\n");
            goto wait_for_non_spare;
      }
      else
      {
            dbg("  current_spare=%s, host=%s\n",
              current_spare_host, pglist->index[wfe_abort_mid]->hostname);
      }
#endif
    close(wfe_socket);
    cleanup();
}

#if defined(CKPT) && defined(CR_AGGRE)
static int rkill_aggregation()
{
    int i, pid;
    char    cmd[256];

    for(i=0; i<NSPAWNS; i++)
    {
        extern char sessionid[16];
        dbg("before umnt for pg_%d @ %s...\n", i, pglist->index[i]->hostname );
        snprintf(cmd, 256, "%s %s fusermount -u /tmp/cr-%s/wa > /dev/null 2>&1", 
              SSH_CMD, pglist->index[i]->hostname, sessionid );
        system(cmd);
        snprintf(cmd, 256, "%s %s fusermount -u /tmp/cr-%s/mig > /dev/null 2>&1", 
              SSH_CMD, pglist->index[i]->hostname, sessionid );
        system(cmd);
        dbg("has finished umnt pg_%d @ %s\n", i, pglist->index[i]->hostname);
    }
}
#endif

void cleanup_handler(int sig)
{
    cleanup();
    #if defined(CKPT) && defined(CR_AGGRE) 
        rkill_aggregation();
    #endif
    exit(EXIT_FAILURE);
}




void pglist_print(void)
{
    if (pglist) {
    size_t i, j, npids = 0, npids_allocated = 0;

    fprintf(stderr, "\n--------------pglist-------------\ndata:\n");
    for (i = 0; i < pglist->npgs; i++) {
        fprintf(stderr, "%p - %s:", &pglist->data[i],
            pglist->data[i].hostname);
        fprintf(stderr, " %d (", pglist->data[i].pid);

        for (j = 0; j < pglist->data[i].npids;
         fprintf(stderr, ", "), j++) {
        fprintf(stderr, "%d", pglist->data[i].plist_indices[j]);
        }

        fprintf(stderr, ")\n");
        npids += pglist->data[i].npids;
        npids_allocated += pglist->data[i].npids_allocated;
    }

    fprintf(stderr, "\nindex:");
    for (i = 0; i < pglist->npgs; i++) {
        fprintf(stderr, " %p", pglist->index[i]);
    }

    fprintf(stderr, "\nnpgs/allocated: %d/%d (%d%%)\n",
        (int) pglist->npgs, (int) pglist->npgs_allocated,
        (int) (pglist->npgs_allocated ? 100. * pglist->npgs /
               pglist->npgs_allocated : 100.));
    fprintf(stderr, "npids/allocated: %d/%d (%d%%)\n", (int) npids,
        (int) npids_allocated,
        (int) (npids_allocated ? 100. * npids /
               npids_allocated : 100.));
    fprintf(stderr, "--pglist-end--\n\n");

        /// show all procs
        for(i=0; i<nprocs; i++)
        {
            fprintf(stderr, "proc_%d: at %s: pid=%d, remote_pid=%d\n",
                    i, plist[i].hostname, plist[i].pid, plist[i].remote_pid );
        }
    }
    
}
//Used when dpm is enabled
int index_dpm_spawn = 1;

void pglist_insert(const char *const hostname, const int plist_index)
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
        strcmp(hostname, pglist->index[index]->hostname))) {
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

    //We need to add another control (we need to understand if the exe is different from the others inserted)
    if (configfile_on && strcmp_result == 0) {
    /* Check if the previous name of exexutable and args in the pglist are equal.
     * If they are different we need to add this exe to another group.*/
    int index_previous = pglist->index[index]->plist_indices[0];
    if ((strcmp_result =
         strcmp(plist[plist_index].executable_name,
            plist[index_previous].executable_name)) == 0) {
        //If both the args are different from NULL we need to compare these
        if (plist[plist_index].executable_args != NULL
        && plist[index_previous].executable_args != NULL)
        strcmp_result =
            strcmp(plist[plist_index].executable_args,
               plist[index_previous].executable_args);
        //If both are null they are the same
        else if (plist[plist_index].executable_args == NULL
             && plist[index_previous].executable_args == NULL)
        strcmp_result = 0;
        //If one is null and the other one is not null they are different
        else
        strcmp_result = 1;
    }
    }
    //if (!dpm && !strcmp_result)
      //  goto insert_pid;
    //If dpm is enabled mpirun_rsh should know how many spawn to start
    if ( dpm ) {
        if (index_dpm_spawn < spinf.totspawns) {
            index_dpm_spawn++;
            if (strcmp_result > 0)
                index++;
            goto add_process_group;
        }
        else
            goto insert_pid;
    }
    if (!strcmp_result)
        goto insert_pid;

    if (strcmp_result > 0)
    index++;

    goto add_process_group;

  init_pglist:
    pglist = malloc(sizeof(process_groups));

    if (pglist) {
    pglist->data = NULL;
    pglist->index = NULL;
    pglist->npgs = 0;
    pglist->npgs_allocated = 0;
    } else {
    goto register_alloc_error;
    }

  add_process_group:
    if (pglist->npgs == pglist->npgs_allocated) {
    process_group *pglist_data_backup = pglist->data;
    ptrdiff_t offset;

    pglist->npgs_allocated += increment;

    backup_ptr = pglist->data;
    pglist->data = realloc(pglist->data, sizeof(process_group) *
                   pglist->npgs_allocated);

    if (pglist->data == NULL) {
        pglist->data = backup_ptr;
        goto register_alloc_error;
    }

    backup_ptr = pglist->index;
    pglist->index = realloc(pglist->index, sizeof(process_group *) *
                pglist->npgs_allocated);

    if (pglist->index == NULL) {
        pglist->index = backup_ptr;
        goto register_alloc_error;
    }

    offset = (size_t) pglist->data - (size_t) pglist_data_backup;
    if (offset) {
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
#if defined(CKPT) && defined(CR_FTB)
       /* This is a spare host. Create a PG but do not insert a PID */
       if (plist_index == -1) return;
#endif
    pg = pglist->index[index];

    if (pg->npids == pg->npids_allocated) {
    if (pg->npids_allocated) {
        pg->npids_allocated <<= 1;

        if (pg->npids_allocated < pg->npids)
        pg->npids_allocated = SIZE_MAX;
        if (pg->npids_allocated > (size_t) nprocs)
        pg->npids_allocated = nprocs;
    } else {
        pg->npids_allocated = 1;
    }

    backup_ptr = pg->plist_indices;
    pg->plist_indices =
        realloc(pg->plist_indices, pg->npids_allocated * sizeof(int));

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
        if (pg->plist_indices) {
            free(pg->plist_indices);
            pg->plist_indices = NULL;
        }
        }
        free(pglist->data);
        pglist->data = NULL;
    }

    if (pglist->index) {
        free(pglist->index);
        pglist->index = NULL;
    }

    free(pglist);
    pglist = NULL;
    }

    alloc_error = 1;
}

void free_memory(void)
{
#ifdef CKPT
    free_locks();
#endif
    if (pglist) {
        if (pglist->data) {
            process_group *pg = pglist->data;

            while (pglist->npgs--) {
                if (pg->plist_indices) {
                    free(pg->plist_indices);
                    pg->plist_indices = NULL;
                }
            pg++;
            }

            free(pglist->data);
            pglist->data = NULL;
        }

        if (pglist->index) {
            free(pglist->index);
            pglist->index = NULL;
        }

        free(pglist);
        pglist = NULL;
    }

    if (plist) {
        while (nprocs--) {
            if (plist[nprocs].device)
                free(plist[nprocs].device);
            if (plist[nprocs].hostname)
                free(plist[nprocs].hostname);
        }

        free(plist);
        plist = NULL;
    }

}

void cleanup(void)
{
    int i;

#ifdef CKPT
    cr_cleanup();
#endif                /* CKPT */

    if (use_totalview) {
        fprintf(stderr, "Cleaning up all processes ...\n");
    }
    if (use_totalview) {
        MPIR_debug_state = MPIR_DEBUG_ABORTING;
    }
    for (i = 0; i < NSIG; i++) {
        signal(i, SIG_DFL);
    }

    if (pglist) {
        dbg("will do rkill_fast...\n");
        rkill_fast();
    }

    else {
    for (i = 0; i < nprocs; i++) {
        if (RUNNING(i)) {
        /* send terminal interrupt, which will hopefully
           propagate to the other side. (not sure what xterm will
           do here.
         */
        kill(plist[i].pid, SIGINT);
        }
    }

    sleep(1);

    for (i = 0; i < nprocs; i++) {
        if (plist[i].state != P_NOTSTARTED) {
        /* send regular interrupt to rsh */
        kill(plist[i].pid, SIGTERM);
        }
    }

    sleep(1);

    for (i = 0; i < nprocs; i++) {
        if (plist[i].state != P_NOTSTARTED) {
        /* Kill the processes */
        kill(plist[i].pid, SIGKILL);
        }
    }

    rkill_linear();
    }

    exit(EXIT_FAILURE);
}

void rkill_fast(void)
{
    int tryagain, spawned_pid[pglist->npgs];
    size_t i, j;

    for (i = 0; i < NSPAWNS; i++) {
    if (0 == (spawned_pid[i] = fork())) {
        dbg("pglist->index[%d]->pid=%d\n", i, pglist->index[i]->pid );
        if (pglist->index[i]->pid != -1) {
        const size_t bufsize = 40 + 10 * pglist->index[i]->npids;
        const process_group *pg = pglist->index[i];
        char kill_cmd[bufsize], tmp[10];

        kill_cmd[0] = '\0';

        if (legacy_startup) {
            strcat(kill_cmd, "kill -s 9");
            for (j = 0; j < pg->npids; j++) {
            snprintf(tmp, 10, " %d",
                 plist[pg->plist_indices[j]].remote_pid);
            strcat(kill_cmd, tmp);
            }
        } else {
            strcat(kill_cmd, "kill");
            snprintf(tmp, 10, " %d", pg->pid);
            strcat(kill_cmd, tmp);
        }

        strcat(kill_cmd, " >&/dev/null");

        if (use_rsh) {
            dbg("will kill mpispawn_%d: %s\n", i, kill_cmd );
            execl(RSH_CMD, RSH_CMD, pg->hostname, kill_cmd, NULL);
        } else {
            dbg("use SSH_CMD: will kill mpispawn_%d of %d @ %s: %s\n", 
                i,NSPAWNS,pg->hostname, kill_cmd );
            execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x", pg->hostname,
              kill_cmd, NULL);
        }

        perror("Here");
        exit(EXIT_FAILURE);
        } else {
        exit(EXIT_SUCCESS);
        }
    }
    }
#if defined(CKPT) && defined(CR_AGGRE)
    rkill_aggregation();
#endif
    while (1) {
    static int iteration = 0;
    tryagain = 0;

    sleep(1 << iteration);

    for (i = 0; i < pglist->npgs; i++) {
        if (spawned_pid[i]) {
        spawned_pid[i] = waitpid(spawned_pid[i], NULL, WNOHANG);
        if (!spawned_pid[i]) {
            tryagain = 1;
        }
        }
    }

    if (++iteration == 5 || !tryagain) {
        break;
    }
    }

    if (tryagain) {
    fprintf(stderr,
        "The following processes may have not been killed:\n");
    for (i = 0; i < pglist->npgs; i++) {
        if (spawned_pid[i]) {
        const process_group *pg = pglist->index[i];

        fprintf(stderr, "%s:", pg->hostname);

        for (j = 0; j < pg->npids; j++) {
            fprintf(stderr, " %d",
                plist[pg->plist_indices[j]].remote_pid);
        }

        fprintf(stderr, "\n");
        }
    }
    }
}



void rkill_linear(void)
{
    int i, tryagain, spawned_pid[nprocs];

    fprintf(stderr, "Killing remote processes...");

    for (i = 0; i < nprocs; i++) {
    if (0 == (spawned_pid[i] = fork())) {
        char kill_cmd[80];

        if (!plist[i].remote_pid)
        exit(EXIT_SUCCESS);

        snprintf(kill_cmd, 80, "kill -s 9 %d >&/dev/null",
             plist[i].remote_pid);

        if (use_rsh) {
        execl(RSH_CMD, RSH_CMD, plist[i].hostname, kill_cmd, NULL);
        } else {
        execl(SSH_CMD, SSH_CMD, SSH_ARG, "-x",
              plist[i].hostname, kill_cmd, NULL);
        }

        perror(NULL);
        exit(EXIT_FAILURE);
    }
    }

    while (1) {
    static int iteration = 0;
    tryagain = 0;

    sleep(1 << iteration);

    for (i = 0; i < nprocs; i++) {
        if (spawned_pid[i]) {
        if (!
            (spawned_pid[i] =
             waitpid(spawned_pid[i], NULL, WNOHANG))) {
            tryagain = 1;
        }
        }
    }

    if (++iteration == 5 || !tryagain) {
        break;
    }
    }

    if (tryagain) {
    fprintf(stderr,
        "The following processes may have not been killed:\n");
    for (i = 0; i < nprocs; i++) {
        if (spawned_pid[i]) {
        fprintf(stderr, "%s [%d]\n", plist[i].hostname,
            plist[i].remote_pid);
        }
    }
    }
}



int getpath(char *buf, int buf_len)
{
    char link[32];
    pid_t pid;
    unsigned len;
    pid = getpid();
    snprintf(&link[0], sizeof(link), "/proc/%i/exe", pid);

    len = readlink(&link[0], buf, buf_len);
    if (len == -1) {
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

/*
#define CHECK_ALLOC() do { \
    if (tmp) { \
        free (mpispawn_env); \
        mpispawn_env = tmp; \
    } \
    else goto allocation_error; \
} while (0);
*/
// Append "name=value" into env string.
// the old env-string is freed, the return value shall be assigned to the env-string
static inline  char* append2env(char* env, char* name, char* value)
{
    if( !env || !name || !value ){
        fprintf(stderr, "%s: Invalid params:  env=%s, name=%s, val=%s\n",
            __func__, env, name, value);
        return NULL;
    }
    char* tmp = mkstr("%s %s=%s", env, name, value);
    free(env);
    return tmp;
}

/**
* Spawn the processes using a linear method.
*/
void spawn_fast(int argc, char *argv[], char *totalview_cmd, char *env)
{
    char *mpispawn_env, *tmp, *ld_library_path, *template;
    char *template2 = NULL;
    char *name, *value;
    int i, n, tmp_i, argind, multichk = 1, multival = 0;
    FILE *fp = NULL;
    char pathbuf[PATH_MAX];

    if ((ld_library_path = getenv("LD_LIBRARY_PATH"))) {
        mpispawn_env = mkstr("LD_LIBRARY_PATH=%s", ld_library_path);
    } else {
        mpispawn_env = mkstr("");
    }

    if (!mpispawn_env)
    goto allocation_error;

    /*
     * Forward mpirun parameters to mpispawn
     */
    mpispawn_env = append_mpirun_parameters(mpispawn_env);

    if (!mpispawn_env) {
        goto allocation_error;
    }

    tmp = mkstr("%s MPISPAWN_MPIRUN_MPD=0", mpispawn_env);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s USE_LINEAR_SSH=%d", mpispawn_env, USE_LINEAR_SSH);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_HOST=%s", mpispawn_env, mpirun_host);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPIRUN_RSH_LAUNCH=1", mpispawn_env);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_CHECKIN_PORT=%d", mpispawn_env, port);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_PORT=%d", mpispawn_env, port);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_NNODES=%d", mpispawn_env, pglist->npgs);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }
 
    tmp = mkstr("%s MPISPAWN_GLOBAL_NPROCS=%d", mpispawn_env, nprocs);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_ID=%d", mpispawn_env, getpid());
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    if (use_totalview) {
    tmp = mkstr("%s MPISPAWN_USE_TOTALVIEW=1", mpispawn_env);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }
    }
#ifdef CKPT
    mpispawn_env = create_mpispawn_vars(mpispawn_env);

#endif				/* CKPT */

    /*
     * mpirun_rsh allows env variables to be set on the commandline
     */
    if (!mpispawn_param_env) {
    mpispawn_param_env = mkstr("");
    if (!mpispawn_param_env)
        goto allocation_error;
    }

    while (aout_index != argc && strchr(argv[aout_index], '=')) {
    name = strdup(argv[aout_index++]);
    value = strchr(name, '=');
    value[0] = '\0';
    value++;
    dpm_add_env(name, value);

#ifdef CKPT
	save_ckpt_vars(name, value);
#ifdef CR_AGGRE
    if( strcmp(name, "MV2_CKPT_FILE")==0){
        mpispawn_env = append2env(mpispawn_env, name, value);
        if( !mpispawn_env ) goto allocation_error;
    }
    else if( strcmp(name, "MV2_CKPT_USE_AGGREGATION")==0 ){
        use_aggre = atoi(value);
    }
    else if( strcmp(name, "MV2_CKPT_AGGREGATION_BUFPOOL_SIZE")==0 ){
        mpispawn_env = append2env(mpispawn_env, name, value);
        if( !mpispawn_env ) goto allocation_error;
    }
    else if( strcmp(name, "MV2_CKPT_AGGREGATION_CHUNK_SIZE")==0 ){
        mpispawn_env = append2env(mpispawn_env, name, value);
        if( !mpispawn_env ) goto allocation_error;
    }
#endif
#endif				/* CKPT */

    tmp = mkstr("%s MPISPAWN_GENERIC_NAME_%d=%s"
            " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
            param_count, name, param_count, value);

    free(name);
    free(mpispawn_param_env);

    if (tmp) {
        mpispawn_param_env = tmp;
        param_count++;
    }

    else {
        goto allocation_error;
    }
    }
#if defined(CKPT) && defined(CR_AGGRE)
    if( use_aggre> 0 )
        mpispawn_env = append2env(mpispawn_env, "MV2_CKPT_USE_AGGREGATION","1");
    else
        mpispawn_env = append2env(mpispawn_env, "MV2_CKPT_USE_AGGREGATION","0");
    if( !mpispawn_env ) goto allocation_error;
#endif
    if (!configfile_on) {
    if (!dpm && aout_index == argc) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        usage();
        exit(EXIT_FAILURE);
    }
    }

    i = argc - aout_index;
    if (debug_on && !use_totalview)
    i++;

    if (dpm == 0 && !configfile_on) {
    tmp =
        mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env, argc - aout_index);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }

    }

    i = 0;

    if (debug_on && !use_totalview) {
    tmp = mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++, DEBUGGER);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }
    }

    if (use_totalview) {
    int j;
    if (!configfile_on) {
        for (j = 0; j < MPIR_proctable_size; j++) {
        MPIR_proctable[j].executable_name = argv[aout_index];
        }
    } else {
        for (j = 0; j < MPIR_proctable_size; j++) {
        MPIR_proctable[j].executable_name =
            plist[j].executable_name;
        }

    }
    }

    tmp_i = i;

    /* to make sure all tasks generate same pg-id we send a kvs_template */
    srand(getpid());
    i = rand() % MAXLINE;
    tmp = mkstr("%s MPDMAN_KVS_TEMPLATE=kvs_%d_%s_%d", mpispawn_env, i,
        mpirun_host, getpid());
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    if (dpm) {
        fp = fopen(spawnfile, "r");
        if (!fp) {
            fprintf(stderr, "spawn specification file not found\n");
            goto allocation_error;
        }
        spinf.dpmindex = spinf.dpmtot = 0;
        tmp = mkstr("%s PMI_SPAWNED=1", mpispawn_env);
        if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
        } else {
            goto allocation_error;
        }


    }

    template = strdup(mpispawn_env);    /* save the string at this level */
    argind = tmp_i;
    i = 0;
    dbg("%d forks to be done, with env:=  %s\n", pglist->npgs, mpispawn_env);
    for (i = 0; i < pglist->npgs; i++) {

        free(mpispawn_env);
        mpispawn_env = strdup(template);
        tmp_i = argind;
        if (dpm) {
            int ret = 1, keyin = 0;
            char *key = NULL;
            int tot_args = 1, arg_done = 0, infonum = 0;

            ++tmp_i;        /* argc index starts from 1 */
            if (spinf.dpmindex < spinf.dpmtot) {
                DBG(fprintf(stderr, "one more fork of same binary\n"));
                ++spinf.dpmindex;
                free(mpispawn_env);
                mpispawn_env = strdup(template2);
                goto done_spawn_read;
            }

            spinf.dpmindex = 0;
            get_line(fp, spinf.linebuf, 1);
            spinf.dpmtot = atoi(spinf.linebuf);
            get_line(fp, spinf.runbuf, 1);
            DBG(fprintf
            (stderr, "Spawning %d instances of %s\n", spinf.dpmtot,
             spinf.runbuf));
            if (multichk) {
            if (spinf.dpmtot == pglist->npgs)
                multival = 0;
            else
                multival = 1;
            multichk = 0;
            }

            ret = 1;
            /* first data is arguments */
            while (ret) {
            get_line(fp, spinf.linebuf, 1);

            if (infonum) {
                char *infokey;
                /* is it an info we are about ? */
                if (check_info(spinf.linebuf)) {
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
                sprintf(spinf.buf, "%s='%s'", key, spinf.linebuf);
                free(key);
                keyin = 0;
            }

            if (0 == (strncmp(spinf.linebuf, ARG, strlen(ARG)))) {
                key = index(spinf.linebuf, '=');
                ++key;
                tmp =
                mkstr("%s MPISPAWN_ARGV_%d=\"%s\"", mpispawn_env,
                      tmp_i++, key);
                if (tmp) {
                    free(mpispawn_env);
                    mpispawn_env = tmp;
                } else {
                    goto allocation_error;
                }

                ++tot_args;
            }

            if (0 == (strncmp(spinf.linebuf, ENDARG, strlen(ENDARG)))) {

                tmp =
                mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env,
                      tot_args);
                if (tmp) {
                    free(mpispawn_env);
                    mpispawn_env = tmp;
                } else {
                    goto allocation_error;
                }

                tot_args = 0;
                arg_done = 1;
            }

            if (0 == (strncmp(spinf.linebuf, END, strlen(END)))) {
                ret = 0;
                ++spinf.dpmindex;
            }
            if (0 == (strncmp(spinf.linebuf, PORT, strlen(PORT)))) {
                keyin = 1;
                key = strdup(spinf.linebuf);
            }
            if (0 == (strncmp(spinf.linebuf, INFN, strlen(INFN)))) {
                /* info num parsed */
                infonum = atoi(index(spinf.linebuf, '=') + 1);
            }

            }
            if (!arg_done) {
            tmp = mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env, 1);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }
            }

            tmp = mkstr("%s MPISPAWN_LOCAL_NPROCS=%d",
mpispawn_env,pglist->data[i].npids);
            if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
            } else {
            goto allocation_error;
            }


            tmp =
            mkstr("%s MPIRUN_COMM_MULTIPLE=%d", mpispawn_env,
                  multival);
            if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
            } else {
            goto allocation_error;
            }
            template2 = strdup(mpispawn_env);
        } else {
            tmp = mkstr("%s MPISPAWN_LOCAL_NPROCS=%d", mpispawn_env,
                pglist->data[i].npids);
            if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
            } else {
            goto allocation_error;
            }
        }

          done_spawn_read:

        if (!(pglist->data[i].pid = fork())) {
            size_t arg_offset = 0;
            const char *nargv[7];
            char *command;

#if defined(CKPT) && defined(CR_FTB) && defined(CR_AGGRE)
            mpispawn_env = append2env(mpispawn_env, "MV2_CKPT_AGGRE_MIG_ROLE","0");
#endif
            if (dpm) {
            tmp = mkstr("%s MPISPAWN_ARGV_0=%s", mpispawn_env,
                    spinf.runbuf);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            tmp = mkstr("%s %s", mpispawn_env, spinf.buf);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            }
            //If the config option is activated, the executable information are taken from the pglist.
            if (configfile_on) {
            int index_plist = pglist->data[i].plist_indices[0];
            /* When the config option is activated we need to put in the mpispawn the number of argument of the exe. */
            tmp =
                mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env,
                  plist[index_plist].argc);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }
            /*Add the executable name and args in the list of arguments from the pglist. */
            tmp =
                add_argv(mpispawn_env,
                     plist[index_plist].executable_name,
                     plist[index_plist].executable_args, tmp_i);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            } else {

            if (!dpm) {
                while (aout_index < argc) {
                tmp =
                    mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env,
                      tmp_i++, argv[aout_index++]);
                if (tmp) {
                    free(mpispawn_env);
                    mpispawn_env = tmp;
                } else {
                    goto allocation_error;
                }

                }
            }
            }

            if (mpispawn_param_env) {
            tmp =
                mkstr("%s MPISPAWN_GENERIC_ENV_COUNT=%d %s",
                  mpispawn_env, param_count, mpispawn_param_env);

            free(mpispawn_param_env);
            free(mpispawn_env);

            if (tmp) {
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }
            }

            tmp = mkstr("%s MPISPAWN_ID=%d", mpispawn_env, i);
            if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
            } else {
            goto allocation_error;
            }


            /* user may specify custom binary path via MPI_Info */
            if (custpath) {
            tmp =
                mkstr("%s MPISPAWN_BINARY_PATH=%s", mpispawn_env,
                  custpath);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }
            free(custpath);
            custpath = NULL;
            }

            /* user may specifiy custom working dir via MPI_Info */
            if (custwd) {
            tmp =
                mkstr("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env,
                  custwd);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            free(custwd);
            custwd = NULL;
            } else {
            tmp =
                mkstr("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, wd);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            }

            for (n = 0; n < pglist->data[i].npids; n++) {
            tmp =
                mkstr("%s MPISPAWN_MPIRUN_RANK_%d=%d", mpispawn_env,
                  n, pglist->data[i].plist_indices[n]);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }


            if (plist[pglist->data[i].plist_indices[n]].device != NULL) {
                tmp =
                mkstr("%s MPISPAWN_VIADEV_DEVICE_%d=%s",
                      mpispawn_env, n,
                      plist[pglist->data[i].plist_indices[n]].
                      device);
                if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
                } else {
                goto allocation_error;
                }

            }

            tmp = mkstr("%s MPISPAWN_VIADEV_DEFAULT_PORT_%d=%d",
                    mpispawn_env, n,
                    plist[pglist->data[i].plist_indices[n]].port);
            if (tmp) {
                free(mpispawn_env);
                mpispawn_env = tmp;
            } else {
                goto allocation_error;
            }

            }
            
            int local_hostname = 0;
            if ((strcmp(pglist->data[i].hostname,mpirun_host) == 0) && (!xterm_on)) {                
                local_hostname = 1;
                nargv[arg_offset++] = BASH_CMD;
                nargv[arg_offset++] = BASH_ARG;
            }
            else {

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
            }
            if (getpath(pathbuf, PATH_MAX) && file_exists(pathbuf)) {
              command =
                mkstr("cd %s; %s %s %s %s/mpispawn 0", wd, ENV_CMD,
                  mpispawn_env, env, pathbuf);
            } else if (use_dirname) {
              command =
                mkstr("cd %s; %s %s %s %s/mpispawn 0", wd, ENV_CMD,
                  mpispawn_env, env, binary_dirname);
            } else {
              command = mkstr("cd %s; %s %s %s mpispawn 0", wd, ENV_CMD,
                    mpispawn_env, env);
            }
            
            /* If the user request an execution with an alternate group
               use the 'sg' command to run mpispawn */
            if (change_group!=NULL) {
               command = mkstr( "sg %s -c '%s'", change_group, command );
            }

            if (!command) {
              fprintf(stderr,
                "Couldn't allocate string for remote command!\n");
              exit(EXIT_FAILURE);
            }
            if ( local_hostname == 0)
                nargv[arg_offset++] = pglist->data[i].hostname;

            nargv[arg_offset++] = command;
            nargv[arg_offset++] = NULL;

            if (show_on) {
              size_t arg = 0;
              fprintf(stdout, "\n");
              while (nargv[arg] != NULL)
                  fprintf(stdout, "%s ", nargv[arg++]);
              fprintf(stdout, "\n");

              exit(EXIT_SUCCESS);
            }

            if (strcmp(pglist->data[i].hostname, plist[0].hostname)) {
              int fd = open("/dev/null", O_RDWR, 0);
              dup2(fd, STDIN_FILENO);
            }
            /*
            int myti = 0;
            for(myti=0; myti<arg_offset; myti++)
                printf("%s: before exec-%d:  argv[%d] = %s\n", __func__, i, myti, nargv[myti] );
            */
            DBG(fprintf(stderr, "final cmd line = %s\n", mpispawn_env));
            execv(nargv[0], (char *const *) nargv);
            perror("execv");

            for (i = 0; i < argc; i++) {
              fprintf(stderr, "%s ", nargv[i]);
            }

            fprintf(stderr, "\n");

            exit(EXIT_FAILURE);
        }
        pglist->data[i].local_pid = pglist->data[i].pid;
    }

    if (spawnfile) {
    unlink(spawnfile);
    }
    return;

  allocation_error:
    perror("spawn_fast");
    if (mpispawn_env) {
    fprintf(stderr, "%s\n", mpispawn_env);
    free(mpispawn_env);
    }

    exit(EXIT_FAILURE);
}

/*
 * Spawn the processes using the hierarchical way.
 */
void spawn_one(int argc, char *argv[], char *totalview_cmd, char *env,
           int fastssh_nprocs_thres)
{
    char *mpispawn_env, *tmp, *ld_library_path;
    char *name, *value;
    int j, i, n, tmp_i, numBytes = 0;
    FILE *fp, *host_list_file_fp;
    char pathbuf[PATH_MAX];
    char *host_list = NULL;
    int k;

    if ((ld_library_path = getenv("LD_LIBRARY_PATH"))) {
        mpispawn_env = mkstr("LD_LIBRARY_PATH=%s", ld_library_path);
    } else {
        mpispawn_env = mkstr("");
    }

    if (!mpispawn_env)
    goto allocation_error;

    /*
     * Forward mpirun parameters to mpispawn
     */
    mpispawn_env = append_mpirun_parameters(mpispawn_env);

    if (!mpispawn_env) {
        goto allocation_error;
    }

    tmp = mkstr("%s MPISPAWN_MPIRUN_MPD=0", mpispawn_env);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s USE_LINEAR_SSH=%d", mpispawn_env, USE_LINEAR_SSH);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_HOST=%s", mpispawn_env, mpirun_host);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPIRUN_RSH_LAUNCH=1", mpispawn_env);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_CHECKIN_PORT=%d", mpispawn_env, port);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_PORT=%d", mpispawn_env, port);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_GLOBAL_NPROCS=%d", mpispawn_env, nprocs);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    tmp = mkstr("%s MPISPAWN_MPIRUN_ID=%d", mpispawn_env, getpid());
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }

    if (!configfile_on) {
    for (k = 0; k < pglist->npgs; k++) {
        /* Make a list of hosts and the number of processes on each host */
        /* NOTE: RFCs do not allow : or ; in hostnames */
        if (host_list)
        host_list = mkstr("%s:%s:%d", host_list,
                  pglist->data[k].hostname,
                  pglist->data[k].npids);
        else
        host_list = mkstr("%s:%d", pglist->data[k].hostname,
                  pglist->data[k].npids);
        if (!host_list)
        goto allocation_error;
        for (n = 0; n < pglist->data[k].npids; n++) {
        host_list = mkstr("%s:%d", host_list,
                  pglist->data[k].plist_indices[n]);
        if (!host_list)
            goto allocation_error;
        }
    }
    } else {
    /*In case of mpmd activated we need to pass to mpispawn the different names and arguments of executables */
    host_list = create_host_list_mpmd(pglist, plist);
    tmp = mkstr("%s MPISPAWN_MPMD=%d", mpispawn_env, 1);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    }

    //If we have a number of processes >= PROCS_THRES we use the file approach
    //Write the hostlist in a file and send the filename and the number of
    //bytes to the other procs
    if (nprocs >= fastssh_nprocs_thres) {

    int pathlen = strlen(wd);
    host_list_file =
        (char *) malloc(sizeof(char) *
                (pathlen + strlen("host_list_file.tmp") +
                 MAX_PID_LEN + 1));
    sprintf(host_list_file, "%s/host_list_file%d.tmp", wd, getpid());

    /* open the host list file and write the host_list in it */
    DBG(fprintf(stderr, "OPEN FILE %s\n", host_list_file));
    host_list_file_fp = fopen(host_list_file, "w");

    if (host_list_file_fp == NULL) {
        fprintf(stderr, "host list temp file could not be created\n");
        goto allocation_error;
    }

    fprintf(host_list_file_fp, "%s", host_list);
    fclose(host_list_file_fp);

    tmp = mkstr("%s HOST_LIST_FILE=%s", mpispawn_env, host_list_file);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    numBytes = (strlen(host_list) + 1) * sizeof(char);
    DBG(fprintf(stderr, "WRITTEN %d bytes\n", numBytes));

    tmp = mkstr("%s HOST_LIST_NBYTES=%d", mpispawn_env, numBytes);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }


    } else {

    /*This is the standard approach used when the number of processes < PROCS_THRES */
    tmp = mkstr("%s MPISPAWN_HOSTLIST=%s", mpispawn_env, host_list);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }
    }

    tmp = mkstr("%s MPISPAWN_NNODES=%d", mpispawn_env, pglist->npgs);
    if (tmp) {
    free(mpispawn_env);
    mpispawn_env = tmp;
    } else {
    goto allocation_error;
    }


    if (use_totalview) {
    tmp = mkstr("%s MPISPAWN_USE_TOTALVIEW=1", mpispawn_env);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }
    }

    /*
     * mpirun_rsh allows env variables to be set on the commandline
     */
    if (!mpispawn_param_env) {
    mpispawn_param_env = mkstr("");
    if (!mpispawn_param_env)
        goto allocation_error;
    }

    while (aout_index != argc && strchr(argv[aout_index], '=')) {
    name = strdup(argv[aout_index++]);
    value = strchr(name, '=');
    value[0] = '\0';
    value++;

    tmp = mkstr("%s MPISPAWN_GENERIC_NAME_%d=%s"
            " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env,
            param_count, name, param_count, value);

    free(name);
    free(mpispawn_param_env);

    if (tmp) {
        mpispawn_param_env = tmp;
        param_count++;
    }

    else {
        goto allocation_error;
    }
    }

    if (!configfile_on) {
    if (aout_index == argc) {
        fprintf(stderr, "Incorrect number of arguments.\n");
        usage();
        exit(EXIT_FAILURE);
    }


    i = argc - aout_index;
    if (debug_on && !use_totalview)
        i++;

    tmp =
        mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env, argc - aout_index);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }

    }
    i = 0;

    if (debug_on && !use_totalview) {
    tmp = mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env, i++, DEBUGGER);
    if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
    } else {
        goto allocation_error;
    }
    }

    if (use_totalview) {
    int j;
    if (!configfile_on) {
        for (j = 0; j < MPIR_proctable_size; j++) {
        MPIR_proctable[j].executable_name = argv[aout_index];
        }
    } else {
        for (j = 0; j < MPIR_proctable_size; j++) {
        MPIR_proctable[j].executable_name =
            plist[j].executable_name;
        }
    }
    }

    tmp_i = i;

    i = 0;            /* Spawn root mpispawn */
    {
    if (!(pglist->data[i].pid = fork())) {
        size_t arg_offset = 0;
        const char *nargv[7];
        char *command;
        //If the config option is activated, the executable information are taken from the pglist.
        if (configfile_on) {
        int index_plist = pglist->data[i].plist_indices[0];

        /* When the config option is activated we need to put in the mpispawn the number of argument of the exe. */
        tmp =
            mkstr("%s MPISPAWN_ARGC=%d", mpispawn_env,
              plist[index_plist].argc);
        if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
        } else {
            goto allocation_error;
        }
        /*Add the executable name and args in the list of arguments from the pglist. */
        tmp =
            add_argv(mpispawn_env,
                 plist[index_plist].executable_name,
                 plist[index_plist].executable_args, tmp_i);
        if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
        } else {
            goto allocation_error;
        }
        } else {
        while (aout_index < argc) {
            tmp =
            mkstr("%s MPISPAWN_ARGV_%d=%s", mpispawn_env,
                  tmp_i++, argv[aout_index++]);
            if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
            } else {
            goto allocation_error;
            }

        }
        }

        if (mpispawn_param_env) {
        tmp =
            mkstr("%s MPISPAWN_GENERIC_ENV_COUNT=%d %s",
              mpispawn_env, param_count, mpispawn_param_env);

        free(mpispawn_param_env);
        free(mpispawn_env);

        if (tmp) {
            mpispawn_env = tmp;
        } else {
            goto allocation_error;
        }
        }

        tmp = mkstr("%s MPISPAWN_ID=%d", mpispawn_env, i);
        if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
        } else {
        goto allocation_error;
        }


        tmp = mkstr("%s MPISPAWN_LOCAL_NPROCS=%d", mpispawn_env,
            pglist->data[i].npids);
        if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
        } else {
        goto allocation_error;
        }


        tmp = mkstr("%s MPISPAWN_WORKING_DIR=%s", mpispawn_env, wd);
        if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
        } else {
        goto allocation_error;
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
        tmp = mkstr("%s MPISPAWN_WD=%s", mpispawn_env, wd);
        if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
        } else {
        goto allocation_error;
        }


        for (j = 0; j < arg_offset; j++) {
        tmp = mkstr("%s MPISPAWN_NARGV_%d=%s", mpispawn_env, j,
                nargv[j]);
        if (tmp) {
            free(mpispawn_env);
            mpispawn_env = tmp;
        } else {
            goto allocation_error;
        }
        }
        tmp = mkstr("%s MPISPAWN_NARGC=%d", mpispawn_env, arg_offset);
        if (tmp) {
        free(mpispawn_env);
        mpispawn_env = tmp;
        } else {
        goto allocation_error;
        }


        if (getpath(pathbuf, PATH_MAX) && file_exists(pathbuf)) {
        command =
            mkstr("cd %s; %s %s %s %s/mpispawn %d", wd, ENV_CMD,
              mpispawn_env, env, pathbuf, pglist->npgs);
        } else if (use_dirname) {
        command =
            mkstr("cd %s; %s %s %s %s/mpispawn %d", wd, ENV_CMD,
              mpispawn_env, env, binary_dirname, pglist->npgs);
        } else {
        command = mkstr("cd %s; %s %s %s mpispawn %d", wd, ENV_CMD,
                mpispawn_env, env, pglist->npgs);
        }

        if (!command) {
        fprintf(stderr,
            "Couldn't allocate string for remote command!\n");
        exit(EXIT_FAILURE);
        }

        nargv[arg_offset++] = pglist->data[i].hostname;
        nargv[arg_offset++] = command;
        nargv[arg_offset++] = NULL;

        if (show_on) {
        size_t arg = 0;
        fprintf(stdout, "\n");
        while (nargv[arg] != NULL)
            fprintf(stdout, "%s ", nargv[arg++]);
        fprintf(stdout, "\n");

        exit(EXIT_SUCCESS);
        }

        if (strcmp(pglist->data[i].hostname, plist[0].hostname)) {
        int fd = open("/dev/null", O_RDWR, 0);
        dup2(fd, STDIN_FILENO);
        }
        DBG(fprintf(stderr, "final cmd line = %s\n", mpispawn_env));
        execv(nargv[0], (char *const *) nargv);
        perror("execv");

        for (i = 0; i < argc; i++) {
        fprintf(stderr, "%s ", nargv[i]);
        }

        fprintf(stderr, "\n");

        exit(EXIT_FAILURE);
    }
    }

    if (spawnfile) {
    unlink(spawnfile);
    }
    return;

  allocation_error:
    perror("spawn_one");
    if (mpispawn_env) {
    fprintf(stderr, "%s\n", mpispawn_env);
    free(mpispawn_env);
    }

    exit(EXIT_FAILURE);
}

/* #undef CHECK_ALLOC */

void make_command_strings(int argc, char *argv[], char *totalview_cmd,
              char *command_name, char *command_name_tv)
{
    int i;
    if (debug_on) {
    fprintf(stderr, "debug enabled !\n");
    char keyval_list[COMMAND_LEN];
    sprintf(keyval_list, "%s", " ");
    /* Take more env variables if present */
    while (strchr(argv[aout_index], '=')) {
        strcat(keyval_list, argv[aout_index]);
        strcat(keyval_list, " ");
        aout_index++;
    }
    if (use_totalview) {
        sprintf(command_name_tv, "%s %s %s", keyval_list,
            totalview_cmd, argv[aout_index]);
        sprintf(command_name, "%s %s ", keyval_list, argv[aout_index]);
    } else {
        sprintf(command_name, "%s %s %s", keyval_list,
            DEBUGGER, argv[aout_index]);
    }
    } else {
    sprintf(command_name, "%s", argv[aout_index]);
    }

    if (use_totalview) {
    /* Only needed for root */
    strcat(command_name_tv, " -a ");
    }

    /* add the arguments */
    for (i = aout_index + 1; i < argc; i++) {
    strcat(command_name, " ");
    strcat(command_name, argv[i]);
    }

    if (use_totalview) {
    /* Complete the command for non-root processes */
    strcat(command_name, " -mpichtv");

    /* Complete the command for root process */
    for (i = aout_index + 1; i < argc; i++) {
        strcat(command_name_tv, " ");
        strcat(command_name_tv, argv[i]);
    }
    strcat(command_name_tv, " -mpichtv");
    }
}

void nostop_handler(int signal)
{
    printf("Stopping from the terminal not allowed\n");
}

void alarm_handler(int signal)
{
    extern const char *alarm_msg;

    if (use_totalview) {
    fprintf(stderr, "Timeout alarm signaled\n");
    }

    if (alarm_msg)
    fprintf(stderr, "%s", alarm_msg);
    cleanup();
}


void child_handler(int signal)
{
    int status, pid;
    static int count = 0;
    static int num_exited = 0;

    int check_mpirun = 1;
    int i = 0;
    list_pid_mpirun_t *curr = dpm_mpirun_pids;
    list_pid_mpirun_t *prev = NULL;

    while (1) 
    {
        dbg("pid count =%d num_exited %d\n",count, num_exited);
        
        // Retry while system call is interrupted
        do {
            pid = waitpid ( -1, &status, WNOHANG );
        } while ( pid == -1 && errno == EINTR );

        // Debug output
        if (pid < 0) {
            dbg("waitpid return pid = %d: errno = %d: %s\n", pid, errno, strerror_r(errno,NULL,0));
        } else {
            dbg("waitpid return pid = %d\n", pid);
            if ( WIFEXITED(status) ) {
                dbg("process %d exited with status %d\n", pid, WEXITSTATUS(status));
            } else if ( WIFSIGNALED(status) ) {
                dbg("process %d terminated with signal %d\n", pid, WTERMSIG(status));
            } else if ( WIFSTOPPED(status) ) {
                dbg("process %d stopped with signal %d\n", pid, WSTOPSIG(status));
            } else if ( WIFCONTINUED(status) ) {
                dbg("process %d continued\n", pid);
            }
        }

        if ( pid < 0 ) {
            if ( errno == ECHILD ) {
                // No more unwaited-for child -> end mpirun_rsh
                if (legacy_startup)
                    close(server_socket);
                exit(EXIT_SUCCESS);
            } else {
                // Unhandled cases -> error
                fprintf( stderr, "[mpirun_rsh][%s:%d] ", __FILE__, __LINE__ );
                fprintf( stderr, "Error in %s: ", __func__ );
                fprintf( stderr, "waitpid returned %d: %s\n", pid, strerror_r(errno,NULL,0) );
                exit(EXIT_FAILURE);
            }
        } else if ( pid == 0 ) {
            // No more exited child -> end handler
            return;
        }

        count++;
        /*With DPM if a SIGCHLD is sent by a child mpirun the mpirun
         *root must not end. It must continue its run and terminate only
         *when a signal is sent by a different child.*/
         if ( check_mpirun ){
                while( curr != NULL ) {
                   if ( pid == curr->pid ){
                        if ( prev == NULL )
                            dpm_mpirun_pids = (list_pid_mpirun_t *)curr->next;
                        else
                            prev->next = (list_pid_mpirun_t *)curr->next;
                        free(curr);
                        // Try with next exited child
                        continue;
                    }
                    prev = curr;
                    curr = curr->next;
                }
            }

            /*If this is the root mpirun and the signal was sent by another child (not mpirun)
             *it can wait the termination of the mpirun childs too.*/
        if (dpm_mpirun_pids != NULL && curr == NULL)
            check_mpirun = 0;
        dbg( "wait pid =%d count =%d, errno=%d\n",pid, count, errno);

#ifdef CKPT
        if( lookup_exit_pid(pid) >= 0 )
        num_exited++;
        dbg(" pid =%d count =%d, num_exited=%d,nspawn=%d, ckptcnt=%d, wait_socks_succ=%d\n",
                pid, count,num_exited, NSPAWNS, checkpoint_count, wait_socks_succ);
        // If number of active nodes have exited, ensure all 
        // other spare nodes are killed too.

#ifdef CR_FTB
        dbg("nsparehosts=%d\n", nsparehosts);
        if(sparehosts_on && (num_exited >= ( NSPAWNS - nsparehosts )))
        {
            dbg(" num_exit=%d, NSPAWNS=%d, nsparehosts=%d, will kill-fast\n",
                 num_exited, NSPAWNS, nsparehosts );
            rkill_fast();
        }
#endif

        if( num_exited < NSPAWNS ) {
            // Try with next exited child
            continue;
        }
#endif
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            /*
             *  If mpirun_rsh was not successful connected to all the
             * mpispawn, it cannot cleanup the mpispawn. It should
             * wait for the timeout.
             */
            if (wait_socks_succ < NSPAWNS) {
                fprintf(stderr,  "%s: Error in init phase...wait for cleanup! (%d/%d"
                    "mpispawn connections)\n", __func__, wait_socks_succ, NSPAWNS);
                alarm_msg =
                    "Failed in initilization phase, cleaned up all the mpispawn!\n";
                socket_error = 1;
                return;
            } else {
                cleanup();
            }
        }
    }
    dbg("mpirun_rsh EXIT FROM child_handler\n");
}

void mpispawn_checkin(int s, struct sockaddr *sockaddr, unsigned int
              sockaddr_len)
{
    int sock, id, i, n, mpispawn_root = -1;
    in_port_t port;
    socklen_t addrlen;
    struct sockaddr_storage addr, address[pglist->npgs];
    int mt_degree;
    mt_degree = env2int("MV2_MT_DEGREE");
    if (!mt_degree) {
    mt_degree = ceil(pow(pglist->npgs, (1.0 / (MT_MAX_LEVEL - 1))));
    if (mt_degree < MT_MIN_DEGREE)
        mt_degree = MT_MIN_DEGREE;
    if (mt_degree > MT_MAX_DEGREE)
        mt_degree = MT_MAX_DEGREE;
    } else {
    if (mt_degree < 2) {
        fprintf(stderr, "mpirun_rsh: MV2_MT_DEGREE too low");
        cleanup();
    }
    }


#if defined(CKPT) && defined(CR_FTB)
    mt_degree = MT_MAX_DEGREE;
       spawninfo = (struct spawn_info_s *) malloc(NSPAWNS*sizeof(struct spawn_info_s));
       if (!spawninfo) {
               perror("[CR_MIG:mpirun_rsh] malloc(spawninfo)");
               cleanup();
       }
#endif

    for (i = 0; i < NSPAWNS; i++) 
    {
        addrlen = sizeof(addr);

        while ((sock = accept(s, (struct sockaddr *) &addr, &addrlen)) < 0) 
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            socket_error = 1;
        }

    if (read_socket(sock, &id, sizeof(int))
        || read_socket(sock, &pglist->data[id].pid, sizeof(pid_t))
        || read_socket(sock, &port, sizeof(in_port_t))) {
        socket_error = 1;
    }

    address[id] = addr;
    ((struct sockaddr_in *) &address[id])->sin_port = port;

    if (!(id == 0 && use_totalview))
        close(sock);
    else
        mpispawn_root = sock;

    for (n = 0; n < pglist->data[id].npids; n++) {
        plist[pglist->data[id].plist_indices[n]].state = P_STARTED;
    }
        wait_socks_succ++;

#if defined(CKPT) && defined(CR_FTB)
        strncpy(spawninfo[i].spawnhost, pglist->data[i].hostname, 31);
        spawninfo[i].sparenode = (pglist->data[i].npids == 0) ? 1 : 0;
#endif 
    }

    /*
     *  If there was some errors in the socket mpirun_rsh can cleanup all the
     * mpispawns.    
     */
    if (socket_error) {
        cleanup();
    }

    if (USE_LINEAR_SSH) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            perror("socket [mpispawn_checkin]");
            cleanup();
        }

        if (connect(sock, (struct sockaddr *) &address[0],
            sizeof(struct sockaddr)) < 0) {
            perror("connect");
            cleanup();
        }

        /*
         * Send address array to address[0] (mpispawn with id 0).  The mpispawn
         * processes will propagate this information to each other after
         * connecting in a tree like structure.
         */
        if (write_socket(sock, &address, sizeof(addr) * pglist->npgs)
#if defined(CKPT) && defined(CR_FTB)
                || write_socket(sock, spawninfo, sizeof(struct
                        spawn_info_s)*(pglist->npgs))
#endif                                              
            ) {
            cleanup();
        }

        close(sock);
    }
    if (use_totalview) {
    int id, j;
    process_info_t *pinfo = (process_info_t *) malloc
        (process_info_s * nprocs);
    read_socket(mpispawn_root, pinfo, process_info_s * nprocs);
    for (j = 0; j < nprocs; j++) {
        MPIR_proctable[pinfo[j].rank].pid = pinfo[j].pid;
    }
    free(pinfo);
    /* We're ready for totalview */
    MPIR_debug_state = MPIR_DEBUG_SPAWNED;
    MPIR_Breakpoint();

    /* MPI processes can proceed now */
    id = 0;
    write_socket(mpispawn_root, &id, sizeof(int));
    if (USE_LINEAR_SSH)
        close(mpispawn_root);
    }
}

/*
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
    case 10:                / * info_val_ * /
        tokptr = strtok (tmpbuf, "=");
        key = strtok (NULL, "=");
        fprintf (fp, "%s\n", key);
        return 0;
    case 1:                    / * execname, preput.. * /
    case 7:
    case 9:                 / * info_key_ * /
        tokptr = strtok (tmpbuf, "=");
        val = strtok (NULL, "=");
        fprintf (fp, "%s\n", val);
        if (type == 7) {
            spinf.launch_num = atoi (val);
        }
        return 0;
    case 2:                    / * totspawns * /
        tokptr = strtok (tmpbuf, "=");
        spinf.totspawns = atoi (strtok (NULL, "="));
        return 0;
    case 3:                    / * endcmd * /
        fprintf (fp, "endcmd\n");
        spinf.spawnsdone = spinf.spawnsdone + 1;
        if (spinf.spawnsdone >= spinf.totspawns)
            return 1;
        return 0;
    case 8:                 / * info num * /
        fprintf (fp, "%s\n", tmpbuf);
        return 0;
    case 5:                    / * arguments * /
        fprintf (fp, "%s\n", tmpbuf);
        return 0;
    case 6:                    / * args end * /
        fprintf (fp, "endarg\n");
        return 0;
    }

    return 0;
}
#undef TEST_STR
*/

int check_token(FILE * fp, char *tmpbuf)
{
    char *tokptr, *val, *key;
    if (strncmp(tmpbuf, "preput_val_",11) == 0
        || strncmp(tmpbuf, "info_val_",9) == 0) {
        /* info_val_ */
        tokptr = strtok(tmpbuf, "=");
        key = strtok(NULL, "=");
        fprintf(fp, "%s\n", key);

        return 0;
    } else if (strncmp(tmpbuf, "execname",8) == 0
           || strncmp(tmpbuf, "preput_key_",11) == 0
           || strncmp(tmpbuf, "info_key_",9) == 0) {
        /* execname, preput.. */
        /* info_key_ */
        tokptr = strtok(tmpbuf, "=");
        val = strtok(NULL, "=");
        fprintf(fp, "%s\n", val);
        return 0;
    } else if (strncmp(tmpbuf, "nprocs",6) == 0) {
        tokptr = strtok(tmpbuf, "=");
        val = strtok(NULL, "=");
        fprintf(fp, "%s\n", val);
        spinf.launch_num = atoi(val);
        return 0;
    } else if (strncmp(tmpbuf, "totspawns",9) == 0) {
        /* totspawns */
        tokptr = strtok(tmpbuf, "=");
        spinf.totspawns = atoi(strtok(NULL, "="));
        return 0;
    } else if (strncmp(tmpbuf, "endcmd",6) == 0) {
        /* endcmd */
        fprintf(fp, "endcmd\n");
        spinf.spawnsdone = spinf.spawnsdone + 1;
        if (spinf.spawnsdone >= spinf.totspawns)
            return 1;
        return 0;
    } else if (strncmp(tmpbuf, "info_num",8) == 0) {
        /* info num */
        fprintf(fp, "%s\n", tmpbuf);
        return 0;
    } else if (strncmp(tmpbuf, "argcnt",6) == 0) {
        /* args end */
        fprintf(fp, "endarg\n");
        return 0;
     } else if (strncmp(tmpbuf, "arg",3) == 0) {
        /* arguments */
        fprintf(fp, "%s\n", tmpbuf);
        return 0;
   }

    return 0;
}

void handle_spawn_req(int readsock)
{
    FILE *fp;
    int done = 0;
    char *chptr, *hdptr;
    uint32_t size, spcnt;

    memset(&spinf, 0, sizeof(spawn_info_t));
    
    sprintf(spinf.linebuf, TMP_PFX "%d_%d_spawn_spec.file", getpid(),
dpm_cnt);
    spinf.spawnfile = strdup(spinf.linebuf);

    fp = fopen(spinf.linebuf, "w");
    if (NULL == fp) {
        fprintf(stderr, "temp file could not be created\n");
    }

    read_socket(readsock, &spcnt, sizeof(uint32_t));
    read_socket(readsock, &size, sizeof(uint32_t));
    hdptr = chptr = malloc(size);
    read_socket(readsock, chptr, size);

    do {
        get_line(chptr, spinf.linebuf, 0);
        chptr = chptr + strlen(spinf.linebuf) + 1;
        done = check_token(fp, spinf.linebuf);
    }
    while (!done);

    fsync(fileno(fp));
    fclose(fp);
    free(hdptr);

    if (totalprocs == 0)
        totalprocs = nprocs;
    TOTALPROCS = mkstr("TOTALPROCS=%d", totalprocs);
    putenv(TOTALPROCS);
    totalprocs = totalprocs + spcnt;

    launch_newmpirun(spcnt);
    return;

    DBG(perror("Fatal error:"));
    return;
}

static void get_line(void *ptr, char *fill, int is_file)
{
    int i = 0, ch;
    FILE *fp;
    char *buf;

    if (is_file) {
        fp = ptr;
        while ((ch = fgetc(fp)) != '\n') {
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



void launch_newmpirun(int total)
{
    FILE *fp;
    int i, j;
    char *newbuf;

    if (!hostfile_on) {
        sprintf(spinf.linebuf, TMP_PFX "%d_hostlist.file", getpid());
        fp = fopen(spinf.linebuf, "w");
        for (j = 0, i = 0; i < total; i++) {
            fprintf(fp, "%s\n", plist[j].hostname);
            j = (j + 1) % nprocs;
        }
        fclose(fp);
    } else {
        strncpy(spinf.linebuf, hostfile, MAXLINE);
    }

    sprintf(spinf.buf, "%d", total);

    /*It needs to maintain the list of mpirun pids to handle the termination.*/
    list_pid_mpirun_t *curr;
    curr = (list_pid_mpirun_t *)malloc(sizeof(list_pid_mpirun_t));
    curr->next  = (list_pid_mpirun_t *)dpm_mpirun_pids;
    dpm_mpirun_pids = curr;
    if (curr->pid = fork())
        return;

    newbuf = (char *) malloc(PATH_MAX + MAXLINE);
    if (use_dirname) {
        strcpy(newbuf, binary_dirname);
        strcat(newbuf, "/mpirun_rsh");
    } else {
        getpath(newbuf, PATH_MAX);
        strcat(newbuf, "/mpirun_rsh");
    }
    DBG(fprintf(stderr, "launching %s\n", newbuf));
    DBG(fprintf(stderr, "numhosts = %s, hostfile = %s, spawnfile = %s\n",
        spinf.buf, spinf.linebuf, spinf.spawnfile));
    char nspawns[MAXLINE];
    sprintf(nspawns, "%d", spinf.totspawns);
    if (dpmenv) {
        execl(newbuf, newbuf, "-np", spinf.buf, "-hostfile", spinf.linebuf,
          "-spawnfile", spinf.spawnfile, "-dpmspawn",nspawns,"-dpm", dpmenv, NULL);
    } else {
        execl(newbuf, newbuf, "-np", spinf.buf, "-hostfile", spinf.linebuf,
          "-spawnfile", spinf.spawnfile, "-dpmspawn",nspawns,"-dpm", NULL);
    }

    perror("execl failed\n");
    exit(EXIT_FAILURE);
}

/**
 *
 */
static int check_info(char *str)
{
    if (0 == (strcmp(str, "wdir")))
    return 1;
    if (0 == (strcmp(str, "path")))
    return 1;
    return 0;
}


/**
 *
 */
static void store_info(char *key, char *val)
{
    if (0 == (strcmp(key, "wdir")))
    custwd = strdup(val);

    if (0 == (strcmp(key, "path")))
    custpath = strdup(val);
}


#define ENVLEN 20480
void dpm_add_env(char *buf, char *optval)
{
    int len;

    if (dpmenv == NULL) {
        dpmenv = (char *) malloc(ENVLEN);
        dpmenv[0] = '\0';
        dpmenvlen = ENVLEN;
    }

    if (optval == NULL) {
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


/* vi:set sw=4 sts=4 tw=76 expandtab: */
