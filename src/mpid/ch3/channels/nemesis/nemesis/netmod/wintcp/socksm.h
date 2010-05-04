/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef SOCKSM_H_INCLUDED
#define SOCKSM_H_INCLUDED

#ifdef HAVE_SYS_POLL_H
    #include <sys/poll.h>
#endif
#include <stddef.h>
#include "mpiu_os_wrappers.h"

enum SOCK_CONSTS {  /* more type safe than #define's */
    LISTENQLEN = 10,
    POLL_CONN_TIME = 2
};

typedef struct sockaddr_in SA_IN;
typedef struct sockaddr SA;

enum CONSTS {
    CONN_TBL_INIT_SIZE = 20,
    CONN_TBL_GROW_SIZE = 10,
    CONN_WAITSET_INIT_SIZE = 20,
    CONN_INVALID_RANK = -1,
    NEGOMSG_DATA_LEN = 4, /* Length of data during negotiatiion message exchanges. */
    SLEEP_INTERVAL = 1500,
    PROGSM_TIMES = 20
};


typedef enum {
    MPID_NEM_NEWTCP_MODULE_SOCK_ERROR_EOF, /* either a socket error or EOF received from peer */
    MPID_NEM_NEWTCP_MODULE_SOCK_CONNECTED,
    MPID_NEM_NEWTCP_MODULE_SOCK_NOEVENT /*  No poll event on socket */
}MPID_NEM_NEWTCP_MODULE_SOCK_STATUS_t;

#define M_(x) x
#define CONN_TYPE_ M_(TYPE_CONN), M_(TYPE_ACPT)

/*  Note :  '_' denotes sub-states */
/*  For example, CSO_DISCCONNECTING_DISCREQSENT, CSO_DISCONNECTING_DISCRSPRCVD are sub-states */
/*  of CSO_DISCONNECTING */
/*  LSO - Listening SOcket states */
#define LISTEN_STATE_                           \
    M_(LISTEN_STATE_CLOSED),                    \
    M_(LISTEN_STATE_LISTENING)

/*
  CONN_STATE - Connection states of socket
  TC = Type connected (state of a socket that was issued a connect on)
  TA = Type Accepted (state of a socket returned by accept)
  TS = Type Shared (state of either TC or TA)

  C - Connection sub-states
  D - Disconnection sub-states
*/

#define CONN_STATE_                             \
    M_(CONN_STATE_TS_CLOSED),                   \
    M_(CONN_STATE_TC_C_CNTING),                 \
    M_(CONN_STATE_TC_C_CNTD),                   \
    M_(CONN_STATE_TC_C_RANKSENT),               \
    M_(CONN_STATE_TC_C_TMPVCSENT),              \
    M_(CONN_STATE_TA_C_CNTD),                   \
    M_(CONN_STATE_TA_C_RANKRCVD),               \
    M_(CONN_STATE_TA_C_TMPVCRCVD),              \
    M_(CONN_STATE_TS_COMMRDY),                  \
    M_(CONN_STATE_TS_D_QUIESCENT)

/* REQ - Request, RSP - Response */

typedef enum CONN_TYPE {CONN_TYPE_, CONN_TYPE_SIZE} Conn_type_t;
typedef enum MPID_nem_newtcp_module_Listen_State {LISTEN_STATE_, LISTEN_STATE_SIZE} 
    MPID_nem_newtcp_module_Listen_State_t ;

typedef enum MPID_nem_newtcp_module_Conn_State {CONN_STATE_, CONN_STATE_SIZE} 
    MPID_nem_newtcp_module_Conn_State_t;

#define CONN_STATE_TO_STRING(_cstate) \
    (( (_cstate) >= CONN_STATE_TS_CLOSED && (_cstate) < CONN_STATE_SIZE ) ? CONN_STATE_STR[_cstate] : "out_of_range")

/*
  Note: event numbering starts from 1, as 0 is assumed to be the state of all-events cleared
 */
typedef enum sockconn_event {EVENT_CONNECT = 1, EVENT_DISCONNECT} 
    sockconn_event_t;

#undef M_
#define M_(x) #x

#if defined(SOCKSM_H_DEFGLOBALS_)
const char *const CONN_TYPE_STR[CONN_TYPE_SIZE] = {CONN_TYPE_};
const char *const LISTEN_STATE_STR[LISTEN_STATE_SIZE] = {LISTEN_STATE_};
const char *const CONN_STATE_STR[CONN_STATE_SIZE] = {CONN_STATE_};
#elif defined(SOCKSM_H_EXTERNS_)
extern const char *const CONN_TYPE_STR[];
extern const char *const LISTEN_STATE_STR[];
extern const char *const CONN_STATE_STR[];
#endif
#undef M_


/* FIXME: Remove assumption that mpi_errno is defined in caller */
#define UPDATE_WAITSET_EVENTS(_sc, _wSetHnd, _state)    do{         \
    mpi_errno = MPIU_SOCKW_Waitset_clr_sock(_wSetHnd, _sc->fd_ws_hnd,  \
        sc_state_info[(_sc)->state.cstate].sc_state_wait_events);   \
    if(mpi_errno != MPI_SUCCESS) { MPIU_ERR_POP(mpi_errno); }       \
    mpi_errno = MPIU_SOCKW_Waitset_set_sock(_wSetHnd, _sc->fd_ws_hnd,  \
        sc_state_info[_state].sc_state_wait_events);                \
    if(mpi_errno != MPI_SUCCESS) { MPIU_ERR_POP(mpi_errno); }       \
} while(0)

#define CHANGE_STATE(_sc, _wSetHnd, _state) do {                    \
    if((_sc)->fd != MPIU_SOCKW_SOCKFD_INVALID){                     \
        UPDATE_WAITSET_EVENTS(_sc, _wSetHnd, _state);               \
    }                                                               \
    (_sc)->state.cstate = _state;                                   \
    (_sc)->handler = sc_state_info[_state].sc_state_handler;        \
} while(0)


struct MPID_nem_new_tcp_module_sockconn;
typedef struct MPID_nem_new_tcp_module_sockconn sockconn_t;

/* FIXME: should plfd really be const const?  Some handlers may need to change the plfd entry. */
/* Note that a conn can potentially be associated with multiple waitSets - hence we need to pass the 
 *  sock handle in the waitSet to the handler 
 */
typedef int (*handler_func_t) (MPIU_SOCKW_Waitset_sock_hnd_t fd_ws_hnd, sockconn_t *const conn);

struct MPID_nem_new_tcp_module_sockconn{
    MPIU_SOCKW_Sockfd_t fd;
    MPIU_SOCKW_Waitset_sock_hnd_t fd_ws_hnd; /* Handle to fd in waitSet */
    int index;

    /* Used to prevent the usage of uninitialized pg info.  is_same_pg, pg_rank,
     * and pg_id are _ONLY VALID_ if this (pg_is_set) is true */
    int pg_is_set;   
    int is_same_pg;  /* TRUE/FALSE -  */
/*     FIXME: see whether this can be removed, by using only pg_id = NULL or non-NULL */
/*      NULL = if same_pg and valid pointer if different pgs. */

    int is_tmpvc;
    int pg_rank; /*  rank and id cached here to avoid chasing pointers in vc and vc->pg */
    char *pg_id; /*  MUST be used only if is_same_pg == FALSE */
    union {
        MPID_nem_newtcp_module_Conn_State_t cstate;
        MPID_nem_newtcp_module_Listen_State_t lstate; 
    }state;
    MPIDI_VC_t *vc;
    /* Conn_type_t conn_type;  Probably useful for debugging/analyzing purposes. */
    handler_func_t handler;
};

typedef enum MPIDI_nem_newtcp_module_pkt_type {
    MPIDI_NEM_NEWTCP_MODULE_PKT_ID_INFO, /*  ID = rank + pg_id */
    MPIDI_NEM_NEWTCP_MODULE_PKT_ID_ACK,
    MPIDI_NEM_NEWTCP_MODULE_PKT_ID_NAK,
    MPIDI_NEM_NEWTCP_MODULE_PKT_DISC_REQ,
    MPIDI_NEM_NEWTCP_MODULE_PKT_DISC_ACK,
    MPIDI_NEM_NEWTCP_MODULE_PKT_DISC_NAK,
    MPIDI_NEM_NEWTCP_MODULE_PKT_TMPVC_INFO,
    MPIDI_NEM_NEWTCP_MODULE_PKT_TMPVC_ACK,
    MPIDI_NEM_NEWTCP_MODULE_PKT_TMPVC_NAK
} MPIDI_nem_newtcp_module_pkt_type_t;
    
typedef struct MPIDI_nem_newtcp_module_header {
    MPIDI_nem_newtcp_module_pkt_type_t pkt_type;
    int datalen;
} MPIDI_nem_newtcp_module_header_t;

typedef struct MPIDI_nem_newtcp_module_idinfo {
    int pg_rank;
/*      Commented intentionally */
/*        char pg_id[pg_id_len+1];  Memory is dynamically allocated for pg_id_len+1 */
/*      Also, this is optional. Sent only, if the recipient belongs to a different pg. */
/*      As long as another variable length field needs to be sent across(if at all required */
/*      in the future), datalen of header itself is enough to find the offset of pg_id      */
/*      in the packet to be sent. */
} MPIDI_nem_newtcp_module_idinfo_t;

/* FIXME: bc actually contains port_name info */
typedef struct MPIDI_nem_newtcp_module_portinfo {
    int port_name_tag;
} MPIDI_nem_newtcp_module_portinfo_t;

#define MPID_nem_newtcp_module_vc_is_connected(vc) (VC_FIELD(vc, sc) && VC_FIELD(vc, sc)->state.cstate == CONN_STATE_TS_COMMRDY)

#endif
