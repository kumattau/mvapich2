/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPID_NEM_IMPL_H
#define MPID_NEM_IMPL_H

#include "my_papi_defs.h"
#include "mpidi_ch3_impl.h"
#include "mpid_nem_net_module_defs.h"
#include "mpid_nem_atomics.h"
#include "mpid_nem_defs.h"
#include "mpid_nem_memdefs.h"
#include "mpid_nem_fbox.h"
#include "mpid_nem_nets.h"
#include "mpid_nem_queue.h"
#include "mpid_nem_generic_queue.h"

#define MPID_NEM__BYPASS_Q_MAX_VAL  ((MPID_NEM_MPICH2_DATA_LEN) - (sizeof(MPIDI_CH3_Pkt_t)))

int MPID_nem_seg_create(MPID_nem_seg_ptr_t, int, int num_local, int local_rank, MPIDI_PG_t *pg_p);
int MPID_nem_seg_destroy (void);
int MPID_nem_seg_alloc( MPID_nem_seg_ptr_t, MPID_nem_seg_info_ptr_t, int);
int MPID_nem_check_alloc(int);
int MPID_nem_mpich2_init (int ckpt_restart);
int MPID_nem_mpich2_send_ckpt_marker (unsigned short wave, MPIDI_VC_t *vc, int *try_again);
int MPID_nem_coll_barrier_init (void);
int MPID_nem_send_iov(MPIDI_VC_t *vc, MPID_Request **sreq_ptr, MPID_IOV *iov, int n_iov);
int MPID_nem_lmt_pkthandler_init(MPIDI_CH3_PktHandler_Fcn *pktArray[], int arraySize);
int MPID_nem_register_initcomp_cb(int (* callback)(void));

#define MPID_nem_mpich2_release_fbox(cell) (MPID_nem_mem_region.mailboxes.in[(cell)->pkt.mpich2.source]->mpich2.flag.value = 0, \
					    MPI_SUCCESS)

/* Shared memory allocation utility functions */
/* MPID_nem_allocate_shared_memory allocates a shared mem region of size "length" and attaches to it.  "handle" points to a string
   descriptor for the region to be passed in to MPID_nem_attach_shared_memory.  "handle" is dynamically allocated and should be
   freed by the caller.*/
int MPID_nem_allocate_shared_memory (char **buf_p, const int length, char *handle[]);
/* MPID_nem_attach_shared_memory attaches to shared memory previously allocated by MPID_nem_allocate_shared_memory */
/*int MPID_nem_attach_shared_memory (char **buf_p, const int length, const char const handle[]);*/
int MPID_nem_attach_shared_memory (char **buf_p, const int length, const char handle[]);
/* MPID_nem_remove_shared_memory removes the OS descriptor associated with the handle.  Once all processes detatch from the region
   the OS resource will be destroyed. */
/*int MPID_nem_remove_shared_memory (const char const handle[]);*/
int MPID_nem_remove_shared_memory (const char handle[]);
/* MPID_nem_detach_shared_memory detaches the shared memory region from this process */
int MPID_nem_detach_shared_memory (const char *buf_p, const int length);

/* initialize shared-memory MPI_Barrier variables */
int MPID_nem_barrier_vars_init (MPID_nem_barrier_vars_t *barrier_region);

static inline void
MPID_nem_waitforlock (MPID_nem_fbox_common_ptr_t pbox, int value, int count)
{
    DO_PAPI2 (PAPI_reset (PAPI_EventSet));
    while (pbox->flag.value != value)
    {
	if(--count == 0)
	{
	    sched_yield();
	}
	DO_PAPI2 (PAPI_reset (PAPI_EventSet));
    }  
    DO_PAPI2 (PAPI_accum_var (PAPI_EventSet, PAPI_vvalues8));
}

static inline int
MPID_nem_islocked (MPID_nem_fbox_common_ptr_t pbox, int value, int count)
{
    while (pbox->flag.value != value && --count == 0)
    {
    }
    return (pbox->flag.value != value);
}

/* Nemesis packets */

typedef enum MPID_nem_pkt_type
{
    MPIDI_NEM_PKT_LMT_RTS = MPIDI_CH3_PKT_END_ALL+1,
    MPIDI_NEM_PKT_LMT_CTS,
    MPIDI_NEM_PKT_LMT_DONE,
    MPIDI_NEM_PKT_LMT_COOKIE,
    MPIDI_NEM_PKT_END    
} MPID_nem_pkt_type_t;

typedef struct MPID_nem_pkt_lmt_rts
{
    MPID_nem_pkt_type_t type;
    MPIDI_Message_match match;
    MPI_Request sender_req_id;
    MPIDI_msg_sz_t data_sz;
    MPIDI_msg_sz_t cookie_len;
}
MPID_nem_pkt_lmt_rts_t;

typedef struct MPID_nem_pkt_lmt_cts
{
    MPID_nem_pkt_type_t type;
    MPI_Request sender_req_id;
    MPI_Request receiver_req_id;
    MPIDI_msg_sz_t data_sz;
    MPIDI_msg_sz_t cookie_len;
}
MPID_nem_pkt_lmt_cts_t;

typedef struct MPID_nem_pkt_lmt_done
{
    MPID_nem_pkt_type_t type;
    MPI_Request req_id;
}
MPID_nem_pkt_lmt_done_t;

typedef struct MPID_nem_pkt_lmt_cookie
{
    MPID_nem_pkt_type_t type;
    MPI_Request req_id;
    MPIDI_msg_sz_t cookie_len;
}
MPID_nem_pkt_lmt_cookie_t;

typedef union MPIDI_CH3_nem_pkt
{
    MPID_nem_pkt_lmt_rts_t lmt_rts;
    MPID_nem_pkt_lmt_cts_t lmt_cts;
    MPID_nem_pkt_lmt_done_t lmt_done;
    MPID_nem_pkt_lmt_cookie_t lmt_cookie;
} MPIDI_CH3_nem_pkt_t;

    

/*  Macros for sending lmt messages from inside network modules.
    These assume mpi_errno is declared and the fn_exit and fn_fail
    labels are defined.
*/

#define MPID_nem_lmt_send_RTS(vc, rts_pkt, s_cookie_buf, s_cookie_len) do {                             \
        MPID_Request *_rts_req;                                                                         \
        MPID_IOV _iov[2];                                                                               \
                                                                                                        \
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending rndv RTS packet");                                      \
        (rts_pkt)->cookie_len = (s_cookie_len);                                                         \
                                                                                                        \
        _iov[0].MPID_IOV_BUF = (rts_pkt);                                                               \
        _iov[0].MPID_IOV_LEN = sizeof(*(rts_pkt));                                                      \
        _iov[1].MPID_IOV_BUF = (s_cookie_buf);                                                          \
        _iov[1].MPID_IOV_LEN = (s_cookie_len);                                                          \
                                                                                                        \
        MPIU_DBG_MSGPKT((vc), (rts_pkt)->match.tag, (rts_pkt)->match.context_id, (rts_pkt)->match.rank, \
                        (rts_pkt)->data_sz, "Rndv");                                                    \
                                                                                                        \
        mpi_errno = MPIDI_CH3_iStartMsgv((vc), _iov, ((s_cookie_len)) ? 2 : 1, &_rts_req);              \
        /* --BEGIN ERROR HANDLING-- */                                                                  \
        if (mpi_errno != MPI_SUCCESS)                                                                   \
        {                                                                                               \
            MPIU_Object_set_ref(_rts_req, 0);                                                           \
            MPIDI_CH3_Request_destroy(_rts_req);                                                        \
            MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**rtspkt");                             \
        }                                                                                               \
        /* --END ERROR HANDLING-- */                                                                    \
        if (_rts_req != NULL)                                                                           \
        {                                                                                               \
            if (_rts_req->status.MPI_ERROR != MPI_SUCCESS)                                              \
            {                                                                                           \
                MPIU_Object_set_ref(_rts_req, 0);                                                       \
                MPIDI_CH3_Request_destroy(_rts_req);                                                    \
                mpi_errno = MPIR_Err_create_code(_rts_req->status.MPI_ERROR, MPIR_ERR_FATAL,            \
                                                 FCNAME, __LINE__, MPI_ERR_OTHER, "**rtspkt", 0);       \
                MPID_Request_release(_rts_req);                                                         \
                goto fn_exit;                                                                           \
            }                                                                                           \
            MPID_Request_release(_rts_req);                                                             \
        }                                                                                               \
    } while (0)

#define MPID_nem_lmt_send_CTS(vc, rreq, r_cookie_buf, r_cookie_len) do {                                \
        MPIDI_CH3_Pkt_t _upkt;                                                                          \
        MPID_nem_pkt_lmt_cts_t * const _cts_pkt = (MPID_nem_pkt_lmt_cts_t *)&_upkt;                     \
        MPID_Request *_cts_req;                                                                         \
        MPID_IOV _iov[2];                                                                               \
                                                                                                        \
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending rndv CTS packet");                                      \
        MPIDI_Pkt_init(_cts_pkt, MPIDI_NEM_PKT_LMT_CTS);                                                \
        _cts_pkt->sender_req_id = (rreq)->ch.lmt_req_id;                                                \
        _cts_pkt->receiver_req_id = (rreq)->handle;                                                     \
        _cts_pkt->cookie_len = (r_cookie_len);                                                          \
        _cts_pkt->data_sz = (rreq)->ch.lmt_data_sz;                                                     \
                                                                                                        \
        _iov[0].MPID_IOV_BUF = _cts_pkt;                                                                \
        _iov[0].MPID_IOV_LEN = sizeof(*_cts_pkt);                                                       \
        _iov[1].MPID_IOV_BUF = (r_cookie_buf);                                                          \
        _iov[1].MPID_IOV_LEN = (r_cookie_len);                                                          \
                                                                                                        \
        mpi_errno = MPIDI_CH3_iStartMsgv((vc), _iov, (r_cookie_len) ? 2 : 1, &_cts_req);                \
        MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ctspkt");                           \
        if (_cts_req != NULL)                                                                           \
        {                                                                                               \
            MPIU_ERR_CHKANDJUMP(_cts_req->status.MPI_ERROR, mpi_errno, MPI_ERR_OTHER, "**ctspkt");      \
            MPID_Request_release(_cts_req);                                                             \
        }                                                                                               \
    } while (0)   
        
#define MPID_nem_lmt_send_COOKIE(vc, rreq, r_cookie_buf, r_cookie_len) do {                                     \
        MPIDI_CH3_Pkt_t _upkt;                                                                                  \
        MPID_nem_pkt_lmt_cookie_t * const _cookie_pkt = &_upkt.lmt_cookie;                                      \
        MPID_Request *_cookie_req;                                                                              \
        MPID_IOV _iov[2];                                                                                       \
                                                                                                                \
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending rndv COOKIE packet");                                           \
        MPIDI_Pkt_init(_cookie_pkt, MPIDI_NEM_PKT_LMT_COOKIE);                                                  \
        _cookie_pkt->req_id = (rreq)->ch.lmt_req_id;                                                            \
        _cookie_pkt->cookie_len = (r_cookie_len);                                                               \
                                                                                                                \
        _iov[0].MPID_IOV_BUF = _cookie_pkt;                                                                     \
        _iov[0].MPID_IOV_LEN = sizeof(*_cookie_pkt);                                                            \
        _iov[1].MPID_IOV_BUF = (r_cookie_buf);                                                                  \
        _iov[1].MPID_IOV_LEN = (r_cookie_len);                                                                  \
                                                                                                                \
        mpi_errno = MPIDI_CH3_iStartMsgv((vc), _iov, (r_cookie_len) ? 2 : 1, &_cookie_req);                     \
        MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**cookiepkt");                                \
        if (_cookie_req != NULL)                                                                                \
        {                                                                                                       \
            MPIU_ERR_CHKANDJUMP(_cookie_req->status.MPI_ERROR, mpi_errno, MPI_ERR_OTHER, "**cookiepkt");        \
            MPID_Request_release(_cookie_req);                                                                  \
        }                                                                                                       \
    } while (0)   
        
#define MPID_nem_lmt_send_DONE(vc, rreq) do {                                                                   \
        MPIDI_CH3_Pkt_t _upkt;                                                                                  \
        MPID_nem_pkt_lmt_done_t * const _done_pkt = &_upkt.lmt_done;                                            \
        MPID_Request *_done_req;                                                                                \
                                                                                                                \
        MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"sending rndv DONE packet");                                             \
        MPIDI_Pkt_init(_done_pkt, MPIDI_NEM_PKT_LMT_DONE);                                                      \
        _done_pkt->req_id = (rreq)->ch.lmt_req_id;                                                              \
                                                                                                                \
        mpi_errno = MPIDI_CH3_iStartMsg((vc), done_pkt, sizeof(*_done_pkt), &_done_req);                        \
        MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**donepkt");                                  \
        if (_done_req != NULL)                                                                                  \
        {                                                                                                       \
            MPIU_ERR_CHKANDJUMP(_done_req->status.MPI_ERROR, mpi_errno, MPI_ERR_OTHER, "**donepkt");            \
            MPID_Request_release(_done_req);                                                                    \
        }                                                                                                       \
    } while (0)   


#endif /* MPID_NEM_IMPL_H */
