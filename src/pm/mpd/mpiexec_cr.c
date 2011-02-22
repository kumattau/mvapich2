 /*
  *
  *  (C) 2001 by Argonne National Laboratory.
  *      See COPYRIGHT in top-level directory.
  */

/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/wait.h>
#include <libcr.h>

#define MAX_CR_MSG_LEN 256
#define CRU_MAX_KEY_LEN  64
#define CRU_MAX_VAL_LEN  64
struct CRU_keyval_pairs {
    char key[CRU_MAX_KEY_LEN];
    char value[CRU_MAX_VAL_LEN];	
};

static struct CRU_keyval_pairs CRU_keyval_tab[64] = { { {0} } };
static int  CRU_keyval_tab_idx = 0;

#define DEFAULT_CHECKPOINT_FILENAME  "/tmp/ckpt"
#define DEFAULT_THREAD_STACKSIZE (1024*1024)
#define DEFAULT_MPIEXEC_PORT     14678
#define DEFAULT_MPD_PORT            24678

#define CR_MAX_FILENAME  128
#define CR_DICT_MSG_LEN   256
#define CR_MAX_HOSTNAME 128

#define CR_RSRT_PORT_CHANGE 16


#define MAX_PATH_LEN 512
#define MPIRUN      "mpiexec.py"

#define CR_ERR_ABORT(args...)  do {                                      \
    fprintf(stderr, "[mpiexec_cr][%s: line %d]", __FILE__, __LINE__);    \
    fprintf(stderr, args);                                               \
    if (mpiexec_listen_fd > 0)                                           \
        close(mpiexec_listen_fd);                                        \
    if (mpiexec_fd > 0)                                                  \
        close(mpiexec_fd);                                               \
    exit(-1);                                                            \
}while(0)

#if defined(CR_DEBUG)
#define CR_DBG(args...)  do {                                            \
    fprintf(stderr, "[mpiexec_cr][%s: line %d]", __FILE__, __LINE__);    \
    fprintf(stderr, args);                                               \
}while(0)
#else /* defined(CR_DEBUG) */
#define CR_DBG(args...)
#endif /* defined(CR_DEBUG) */

typedef enum {
    CR_INIT,
    CR_READY,
    CR_CHECKPOINT,
    CR_CHECKPOINT_CONFIRM,
    CR_CHECKPOINT_ABORT,
    CR_RESTART,
    CR_RESTART_CONFIRM,
    CR_FINALIZED,
}CR_state_t;

static int checkpoint_count;
static int restart_count;

static pid_t mpiexec_pid;
static int mpiexec_listen_port;
static int mpiexec_listen_fd;
static int mpiexec_fd;
static int mpd_base_port;
static int max_save_ckpts;
static int max_ckpts;

static int enable_sync_ckpt = 0;

static pthread_mutex_t cr_lock;

static cr_callback_id_t cr_callback_id;
static cr_client_id_t cr_id;
static CR_state_t cr_state;
static char ckpt_filename[CR_MAX_FILENAME];/*filename.<count>.<rank>*/
static int checkpoint_interval;

/*time info in seconds*/
static unsigned long starting_time;
static unsigned long last_ckpt;

static char *cr_cmd_mpirun;
static int cr_argc;
static char **cr_argv;

int CR_MPDU_readline( int fd, char *buf, int maxlen);
int CR_MPDU_writeline( int fd, char *buf);
int CR_MPDU_parse_keyvals( char *st );
char* CR_MPDU_getval( const char *keystr, char *valstr, int vallen);


#define CR_MUTEX_LOCK do {           \
    pthread_mutex_lock(&cr_lock);    \
}while(0)

#define CR_MUTEX_UNLOCK do{            \
    pthread_mutex_unlock(&cr_lock);    \
}while(0)
    
void Int_handler(int signal)
{
    if (signal==SIGINT) {
        if (mpiexec_listen_fd > 0)
            close(mpiexec_listen_fd);
        if (mpiexec_fd > 0)
            close(mpiexec_fd);
        fprintf(stderr,"CTRL+C Caught... exiting\n");
        if (mpiexec_pid > 0)
            kill(mpiexec_pid, SIGINT);
        exit(-1);
    }
}

static int CR_callback(void *arg)
{
    int ret;
    char buf[MAX_CR_MSG_LEN];
    char val[CRU_MAX_VAL_LEN];
    struct timeval now;
    fd_set set;

    CR_MUTEX_LOCK;
    if (cr_state != CR_READY) {
        ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
        if (ret != CR_ETEMPFAIL) {
            CR_ERR_ABORT("abort checkpoint failed\n");
        }
        CR_MUTEX_UNLOCK;
        return 0;
    }

    gettimeofday(&now,NULL);
    last_ckpt = now.tv_sec;
    checkpoint_count++;
    cr_state = CR_CHECKPOINT;
    sprintf(buf,"cmd=ckpt_req file=%s\n",ckpt_filename);
    CR_MPDU_writeline(mpiexec_fd, buf);

    /*Wait for checkpoint to finish*/
    FD_ZERO(&set);
    FD_SET(mpiexec_fd,&set);
    if (select(mpiexec_fd+1,&set,NULL,NULL,NULL)<0) {
        cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
        CR_ERR_ABORT("select failed\n");
    }
    CR_MPDU_readline(mpiexec_fd, buf, MAX_CR_MSG_LEN);
    CR_DBG("Got message from MPD %s\n",buf);
    if(CR_MPDU_parse_keyvals(buf)<0) {
       CR_ERR_ABORT("CR_MPDU_parse_keyvals failed\n");
    }
    CR_MPDU_getval("ckpt_result",val, CRU_MAX_VAL_LEN);
    if (strcmp(val,"succeed")!=0) {
        if (strcmp(val,"finalize_ckpt")==0) {
            /*MPI Process finalized*/
            cr_state = CR_FINALIZED;
            CR_MUTEX_UNLOCK;
            return 0;
        }
        CR_DBG("MPI process checkpoint failed\n");
        ret = cr_checkpoint(CR_CHECKPOINT_TEMP_FAILURE);
        if (ret != CR_ETEMPFAIL) {
            CR_ERR_ABORT("abort: checkpoint failed\n");
        }
        CR_MUTEX_UNLOCK;
        return 0;
    }
    
    CR_DBG("MPI procs succeed, now checkpointing console\n");
    ret = cr_checkpoint(CR_CHECKPOINT_READY);

    if (ret < 0) {
        CR_ERR_ABORT("abort: checkpoint failed\n");
        return -1;
    } 
    else if (ret)   {
        char buf[CR_MAX_FILENAME];
        cr_state = CR_RESTART;
        CR_DBG("restarting from checkpoint\n");
        sprintf(buf,"%s.%d",ckpt_filename,checkpoint_count);
        setenv("MV2_CR_RESTART_FILE",buf,1);
        gettimeofday(&now,NULL);
        restart_count++;
        sprintf(buf,"%d",mpiexec_listen_port+restart_count);
        setenv("MV2_CKPT_MPIEXEC_PORT",buf,1);
        sprintf(buf,"%d",mpd_base_port+CR_RSRT_PORT_CHANGE*restart_count);
        setenv("MV2_CKPT_MPD_BASE_PORT",buf,1);
        starting_time = last_ckpt = now.tv_sec;
        mpiexec_pid = fork();
        if (mpiexec_pid==0){
            execvp(cr_cmd_mpirun,cr_argv);
            perror("execvp:");
            CR_ERR_ABORT("execvp failed\n");
        }
        CR_connect_mpd(mpiexec_listen_port+restart_count);
        
        FD_ZERO(&set);
        FD_SET(mpiexec_fd,&set);
        if (select(mpiexec_fd+1,&set,NULL,NULL,NULL)<0) {
            CR_ERR_ABORT("select failed\n");
        }
        CR_MPDU_readline(mpiexec_fd, buf, MAX_CR_MSG_LEN);
        CR_DBG("Got message from MPD %s\n",buf);
        if(CR_MPDU_parse_keyvals(buf)<0) {
            CR_ERR_ABORT("CR_MPDU_parse_keyvals failed\n");
        }
        CR_MPDU_getval("rsrt_result",val, CRU_MAX_VAL_LEN);
        if (strcmp(val,"succeed")!=0) {
            CR_ERR_ABORT("abort: restart failed\n");
            CR_MUTEX_UNLOCK;
            return 0;
        }
        CR_DBG("restart done\n");
    }
    else {
        CR_DBG("checkpointing done\n");
    }

    cr_state = CR_READY;    
    CR_MUTEX_UNLOCK;
    return 0;
}


int CR_connect_mpd(int port)
{
    int i = 1;
    struct sockaddr_in sa;
    mpiexec_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (mpiexec_listen_fd < 0)
        CR_ERR_ABORT("socket failed\n");
    setsockopt(mpiexec_listen_fd, SOL_SOCKET, SO_REUSEADDR, (int *) &i, sizeof(i));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    CR_DBG("Listen port %d\n",port);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(mpiexec_listen_fd, (struct sockaddr*) &sa, sizeof(struct sockaddr_in)) < 0)
        CR_ERR_ABORT("bind failed\n");
    if (listen(mpiexec_listen_fd, 2) < 0)
        CR_ERR_ABORT("listen failed\n");
    if ((mpiexec_fd = accept(mpiexec_listen_fd, 0, 0)) < 0)   
        CR_ERR_ABORT("accept failed\n");
    close(mpiexec_listen_fd);
    mpiexec_listen_fd = 0;
    return 0;
}

void CR_timer_loop()
{
    /*Do scheduled jobs*/
    struct timeval starting, now;
    int time_counter;
    int interval1 = 10;
    cr_id = cr_init();
    if (cr_id < 0)
        CR_ERR_ABORT("cr_init failed\n");
    cr_callback_id = cr_register_callback(CR_callback,NULL,CR_THREAD_CONTEXT);
    CR_DBG("CR_initialized\n");
    gettimeofday(&starting, NULL);
    starting_time = last_ckpt = starting.tv_sec;
    while(1)
    {
        struct timeval tv;
        char cr_msg_buf[MAX_CR_MSG_LEN];
        fd_set set;
        char valstr[CRU_MAX_VAL_LEN];
        int ret;

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        CR_MUTEX_LOCK;
        if (cr_state == CR_FINALIZED)
            break;
        CR_MUTEX_UNLOCK;
        FD_ZERO(&set);
        FD_SET(mpiexec_fd, &set);
        ret = select(mpiexec_fd+1, &set, NULL, NULL, &tv);
        if (ret < 0 && errno != EINTR && errno != EBUSY)
        {
            CR_ERR_ABORT("select failed\n");
        }
        else if (ret > 0) {
            int n;
            if (cr_state != CR_READY)
            {
                continue;
            }
            n = CR_MPDU_readline(mpiexec_fd, cr_msg_buf, MAX_CR_MSG_LEN);
            if (n == 0) {
                cr_state = CR_FINALIZED;
                continue;
            }
            CR_DBG("Got message from MPI Process %s\n",cr_msg_buf);
            if (CR_MPDU_parse_keyvals(cr_msg_buf)<0)
                break;
            CR_MPDU_getval("cmd",valstr, CRU_MAX_VAL_LEN);
            if (strcmp(valstr,"app_ckpt_req")==0) {
                if (enable_sync_ckpt == 0) {
                    continue;
                }
                {
                    CR_MUTEX_LOCK;
                    char buf[CR_MAX_FILENAME];
                    sprintf(buf,"%s.%d.sync",ckpt_filename,checkpoint_count+1);
                    cr_request_file(buf);
                    last_ckpt = now.tv_sec;
                    CR_MUTEX_UNLOCK;
                }
            }
            else if (strcmp(valstr,"finalize_ckpt")==0) {
                cr_state = CR_FINALIZED;
                continue;
            }
        }
        else {
            CR_MUTEX_LOCK;
            gettimeofday(&now,NULL);
            time_counter = (now.tv_sec-starting_time);
            if (checkpoint_interval > 0 
             && now.tv_sec != last_ckpt 
             && time_counter%checkpoint_interval ==0
             && cr_state == CR_READY)
            {/*inject a checkpoint*/
                char buf[CR_MAX_FILENAME];
                if (max_ckpts == 0 || max_ckpts > checkpoint_count) {
                    sprintf(buf,"%s.%d.auto",ckpt_filename,checkpoint_count+1);
                    cr_request_file(buf);
                    last_ckpt = now.tv_sec;
                }
                if (max_save_ckpts > 0 && max_save_ckpts < checkpoint_count+1)
                {
                    /*remove the ealier checkpoints*/
                    sprintf(buf,"%s.%d.auto",ckpt_filename,checkpoint_count+1-max_save_ckpts);
                    /*Ignore errors because old checkpoints may be placed manually
                      with unknown file names*/
                    unlink(buf);
                }
            }
            CR_MUTEX_UNLOCK;
        }
    }
}

int CR_Init()
{
    char *temp;
    char hostname[CR_MAX_HOSTNAME];
    
    /*Initialize ftc_info*/
    pthread_mutex_init(&cr_lock,NULL);

    checkpoint_count = 0;
    restart_count = 0;
    cr_state = CR_INIT;
    mpiexec_pid = 0;
    mpiexec_listen_fd = 0;
    mpiexec_fd = 0;
    enable_sync_ckpt = 1;
    
    temp = getenv("MV2_CKPT_FILE");
    if (temp) {
        strncpy(ckpt_filename,temp,CR_MAX_FILENAME);
    }
    else {
        strncpy(ckpt_filename,DEFAULT_CHECKPOINT_FILENAME,CR_MAX_FILENAME);
    }
    
    temp = getenv("MV2_CKPT_INTERVAL");
    if (temp) {
        checkpoint_interval = atoi(temp)*60;
    }
    else {
        checkpoint_interval = -1;
    }

    unsetenv("MV2_CR_RESTART_FILE");

    /*Set env for listen port and ftc hostname*/
    setenv("MV2_CR_ENABLED","1",1);
    
    temp = getenv("MV2_CKPT_MPIEXEC_PORT");
    if (temp) {
        mpiexec_listen_port = atoi(temp);
        CR_DBG("MV2_CKPT_MPIEXEC_PORT %s\n",temp);
    }
    else {
        char buf[10];
        mpiexec_listen_port = DEFAULT_MPIEXEC_PORT;
        sprintf(buf,"%d",DEFAULT_MPIEXEC_PORT);
        setenv("MV2_CKPT_MPIEXEC_PORT",buf,1);
        CR_DBG("MV2_CKPT_MPIEXEC_PORT %s\n",buf);
    }

    temp = getenv("MV2_CKPT_MPD_BASE_PORT");
    if (temp) {
        mpd_base_port = atoi(temp);
        CR_DBG("MV2_CKPT_MPD_BASE_PORT %s\n",temp);
    }
    else {
        char buf[10];
        mpd_base_port = DEFAULT_MPD_PORT;
        sprintf(buf,"%d",DEFAULT_MPD_PORT);
        setenv("MV2_CKPT_MPD_BASE_PORT",buf,1);
        CR_DBG("MV2_CKPT_MPD_BASE_PORT %s\n",buf);
    }

    temp = getenv("MV2_CKPT_MAX_SAVE_CKPTS");
    if (temp) {
        max_save_ckpts = atoi(temp);
        CR_DBG("MV2_CKPT_MAX_SAVE_CKPTS  %s\n",temp);
    }
    else {
        max_save_ckpts = 0;
    }

    temp = getenv("MV2_CKPT_MAX_CKPTS");
    if (temp) {
        max_ckpts = atoi(temp);
        CR_DBG("MV2_CKPT_MAX_CKPTS  %s\n",temp);
    }
    else {
        max_ckpts = 0;
    }

    signal(SIGINT,Int_handler);
    signal(SIGCHLD,Int_handler);

    return 0;
}

int CR_Fork_exec(char *cmd, int argc, char *argv[])
{
    /*fork a new process*/
    cr_cmd_mpirun = cmd;
    cr_argc = argc;
    cr_argv = argv;
    mpiexec_pid = fork();
    if (mpiexec_pid == 0) {/*Child*/
        execvp(cmd,argv);
        perror("execvp");
        CR_ERR_ABORT("execvp failed, cmd %s, argc %d\n",cmd, argc);
    } else {
        int status;
        CR_connect_mpd(mpiexec_listen_port);
        CR_DBG("mpd connected\n");
        cr_state = CR_READY;
        CR_timer_loop();
        CR_DBG("wait for child\n");
        wait(&status);
        CR_DBG("exiting\n");
        exit(WEXITSTATUS(status));
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char cmd[MAX_PATH_LEN];
    char **new_argv;
    char *rchar;
    int i = 1;
    int length;

    if (argc < 2)
    {
        fprintf(stderr,"mpiexec_cr: checkpointing enabled version of mpiexec \n");
        fprintf(stderr,"Please refer to user guide for detailed instruction \n");
        fprintf(stderr,"Usage: %s <arguments for mpiexec>\n", argv[0]);
        return 0;
    }
    CR_Init();
    /*replace the command line to mpiexec*/
    rchar = strrchr(argv[0],'/');
    /* strrchr returns NULL if char is not found */
    length = rchar ? rchar+1-argv[0] : 0;
    if (length+strlen(MPIRUN)>MAX_PATH_LEN) {
        fprintf(stderr,"command line too long\n");
        exit (1);
    }
    strncpy(cmd,argv[0],length);
    *(cmd+length)='\0';
    strncat(cmd,MPIRUN,MAX_PATH_LEN-length);
    *(cmd+MAX_PATH_LEN-1)='\0';

    new_argv = (char **)malloc(sizeof(char*)*(argc+1));
    new_argv[0]=cmd;
    for (; i < argc; ++i)
    {
        new_argv[i]=argv[i];
    }
    new_argv[argc]=NULL;

    CR_Fork_exec(cmd, argc, new_argv);

    free(new_argv);
    return 0;
}


/*==========================*
*  MPD messaging functions  *
*    Courtesy of PMI        *
*==========================*/

int CR_MPDU_readline( int fd, char *buf, int maxlen)
{
    int n = 1;
    int rc;
    char c, *ptr;

    ptr = buf;
    for (; n < maxlen; ++n) {
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

    if (!st)
    {
        return -1;
    }

    CRU_keyval_tab_idx = 0;
    p = st;
    while (1)
    {
        while (*p == ' ')
        {
            ++p;
        }
        
        /* got non-blank */
        if ( *p == '=' ) {
            fprintf(stderr, "CRU_parse_keyvals: Error parsing keyvals\n");
            return -1;
        }
        if ( *p == '\n' || *p == '\0' )
            return( 0 );    /* normal exit */
        /* got normal
         * character */
        keystart = p;       /* remember where key started */
        while (*p != ' ' && *p != '=' && *p != '\n' && *p != '\0')
        {
            ++p;
        }
        if ( *p == ' ' || *p == '\n' || *p == '\0' ) {
            fprintf(stderr,
                    "CRU_parse_keyvals: unexpected key delimiter at character %d in %s\n",
                    (int)(p - st), st );
            return -1;
        }
        strncpy( CRU_keyval_tab[CRU_keyval_tab_idx].key, keystart, CRU_MAX_KEY_LEN );
        CRU_keyval_tab[CRU_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

        valstart = ++p;         /* start of value */
        while (*p != ' ' && *p != '\n' && *p != '\0')
        {
            ++p;
        }
        strncpy( CRU_keyval_tab[CRU_keyval_tab_idx].value, valstart, CRU_MAX_VAL_LEN );
        CRU_keyval_tab[CRU_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
        ++CRU_keyval_tab_idx;
        if (*p == ' ')
        {
            continue;
        }
        if (*p == '\n' || *p == '\0')
        {
            return 0;    /* value has been set to empty */
        }
    }
    
    return -1;
}
    
char* CR_MPDU_getval( const char *keystr, char *valstr, int vallen)
{
    int i = 0;

    for (; i < CRU_keyval_tab_idx; ++i)
    {
        if (strcmp( keystr, CRU_keyval_tab[i].key ) == 0)
        {
            strncpy(valstr, CRU_keyval_tab[i].value, vallen - 1);
            valstr[vallen - 1] = '\0';
            return valstr;
        }
    }
    valstr[0] = '\0';
    return NULL;
}
