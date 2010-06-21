#ifndef _MPIRUN_CKPT_H
#define _MPIRUN_CKPT_H

#include <mpirunconf.h>
#include "mpirun_rsh.h"
#include "mpirun_dbg.h"

#ifdef CKPT

int ckptInit();
//static void *CR_Loop(void *arg);
char *create_mpispawn_vars( char *mpispawn_env );

#include <sys/time.h>
#include <libcr.h>
#include <pthread.h>


#define CR_ERRMSG_SZ 64

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


extern int restart_context;
extern int cached_restart_context;

//static void *CR_Loop(void *);
//static int   CR_Callback(void *);

//extern char *CR_MPDU_getval(const char *, char *, int);
//extern int   CR_MPDU_parse_keyvals(char *);
//extern int   CR_MPDU_readline(int , char *, int);
//extern int   CR_MPDU_writeline(int , char *);

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

extern CR_state_t cr_state;
extern unsigned long starting_time;
extern unsigned long last_ckpt;

extern int checkpoint_count;
extern char sessionid[CR_SESSION_MAX];
extern int checkpoint_interval;
extern int max_save_ckpts;
extern int max_ckpts;

extern char ckpt_filename[CR_MAX_FILENAME];

#ifdef CR_FTB

#include <libftb.h>

#define FTB_MAX_SUBSCRIPTION_STR 64

#define CR_FTB_EVENT_INFO {               \
        {"CR_FTB_CHECKPOINT",    "info"}, \
        {"CR_FTB_CKPT_DONE",     "info"}, \
        {"CR_FTB_CKPT_FAIL",     "info"}, \
        {"CR_FTB_RSRT_DONE",     "info"}, \
        {"CR_FTB_RSRT_FAIL",     "info"}, \
        {"CR_FTB_APP_CKPT_REQ",  "info"}, \
        {"CR_FTB_CKPT_FINALIZE", "info"}  \
}

/* Index into the Event Info Table */
#define CR_FTB_CHECKPOINT    0
#define CR_FTB_EVENTS_MAX    1 /* HACK */
#define CR_FTB_CKPT_DONE     1
#define CR_FTB_CKPT_FAIL     2
#define CR_FTB_RSRT_DONE     3
#define CR_FTB_RSRT_FAIL     4
#define CR_FTB_APP_CKPT_REQ  5
#define CR_FTB_CKPT_FINALIZE 6

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

extern pthread_t       cr_tid;
extern cr_client_id_t  cr_id;
extern pthread_mutex_t cr_lock;

extern pthread_spinlock_t flock;
extern int fcnt;

#define CR_ERRMSG_SZ 64
extern char cr_errmsg[CR_ERRMSG_SZ];

extern FTB_client_t        ftb_cinfo;
extern FTB_client_handle_t ftb_handle;
//extern FTB_event_info_t    cr_ftb_events[] = CR_FTB_EVENT_INFO;
extern FTB_subscribe_handle_t shandle;
extern int ftb_init_done;

//extern pthread_cond_t  cr_ftb_ckpt_req_cond  = PTHREAD_COND_INITIALIZER;
//extern pthread_mutex_t cr_ftb_ckpt_req_mutex = PTHREAD_MUTEX_INITIALIZER;
extern int cr_ftb_ckpt_req;
extern int cr_ftb_app_ckpt_req;
extern int cr_ftb_finalize_ckpt;

//int  cr_ftb_init(int, char *);
int  cr_ftb_init( int );
//extern void cr_ftb_finalize();
//extern int  cr_ftb_callback(FTB_receive_event_t *, void *);
//extern int  cr_ftb_wait_for_resp(int);

#else

//extern char *CR_MPDU_getval(const char *, char *, int);
//extern int   CR_MPDU_parse_keyvals(char *);
//extern int   CR_MPDU_readline(int , char *, int);
//extern int   CR_MPDU_writeline(int , char *);

extern int *mpirun_fd;
extern int mpirun_port;

#endif /* CR_FTB */

void restart_from_ckpt();
void finalize_ckpt();
#endif
#endif
