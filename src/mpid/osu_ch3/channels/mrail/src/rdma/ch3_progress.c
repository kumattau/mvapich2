/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3_impl.h"
#include "mpidu_process_locks.h"        /* for MPIDU_Yield */


#ifdef DEBUG
#include "pmi.h"
#define DEBUG_PRINT(args...)  \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
    fflush(stderr); \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

static int handle_read(MPIDI_VC_t * vc, vbuf * v);
static int handle_read_individual(MPIDI_VC_t * vc, 
        vbuf * buffer, int *header_type);

static int cm_handle_pending_send();

extern volatile int *rdma_cm_iwarp_msg_count;
extern volatile int *rdma_cm_connect_count;

#ifdef CKPT
static int cm_handle_reactivation_complete();
static int cm_send_pending_msg(MPIDI_VC_t * vc);
#endif

volatile unsigned int MPIDI_CH3I_progress_completion_count = 0;
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    volatile int MPIDI_CH3I_progress_blocked = FALSE;
    volatile int MPIDI_CH3I_progress_wakeup_signalled = FALSE;
    static int MPIDI_CH3I_Progress_delay(unsigned int completion_count);
    static int MPIDI_CH3I_Progress_continue(unsigned int completion_count);
    MPID_Thread_cond_t MPIDI_CH3I_progress_completion_cond;
#endif

inline static int MPIDI_CH3I_Seq(int type)
{ 

    switch(type) {
        case MPIDI_CH3_PKT_EAGER_SEND:
        case MPIDI_CH3_PKT_READY_SEND:
        case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
        case MPIDI_CH3_PKT_RNDV_REQ_TO_SEND:
        case MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND:
        case MPIDI_CH3_PKT_RNDV_CLR_TO_SEND:
        case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
        case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
        case MPIDI_CH3_PKT_RNDV_R3_DATA:
#ifdef USE_HEADER_CACHING
        case MPIDI_CH3_PKT_FAST_EAGER_SEND:
        case MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ:
#endif
        case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
        case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
        case MPIDI_CH3_PKT_PUT:
        case MPIDI_CH3_PKT_GET:
        case MPIDI_CH3_PKT_GET_RESP:
        case MPIDI_CH3_PKT_ACCUMULATE:
        case MPIDI_CH3_PKT_LOCK:
        case MPIDI_CH3_PKT_LOCK_GRANTED:
        case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:
        case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:
        case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:
        case MPIDI_CH3_PKT_PT_RMA_DONE:
        case MPIDI_CH3_PKT_PUT_RNDV:
        case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
        case MPIDI_CH3_PKT_GET_RNDV:
        case MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND:
            return 1;
        default:
            return 0;
    }
}


#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_wakeup
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_Progress_wakeup(void)
{

}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connection_terminate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Connection_terminate(MPIDI_VC_t * vc)
{
    int mpi_errno = MPI_SUCCESS;

    /* There is no post_close for shm connections so 
     * handle them as closed immediately. */
    mpi_errno =
        MPIDI_CH3U_Handle_connection(vc, MPIDI_VC_EVENT_TERMINATED);
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**fail", NULL);
    }

    return mpi_errno;
}


#ifndef MPIDI_CH3_Progress_start
void MPIDI_CH3_Progress_start(MPID_Progress_state * state)
{
    /* MT - This function is empty for the single-threaded implementation */
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress(int is_blocking, MPID_Progress_state * state)
{
    MPIDI_VC_t *vc_ptr = NULL;
    int mpi_errno;
    int spin_count = 1;
    unsigned completions = MPIDI_CH3I_progress_completion_count;
    vbuf *buffer = NULL;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS);
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);
#ifdef USE_SLEEP_YIELD
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_SLEEP_YIELD);
#endif

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS);

    MPIDI_DBG_PRINTF((50, FCNAME, "entering, blocking=%s",
                      is_blocking ? "true" : "false"));
    DEBUG_PRINT("Entering ch3 progress\n");

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    do {
#ifdef _SMP_ 
        /*needed if early send complete doesnot occur */
        if (SMP_INIT) {
            mpi_errno = MPIDI_CH3I_SMP_write_progress(MPIDI_Process.my_pg);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**ch3progress", 0);
                MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
                return mpi_errno;
            }
        }
#endif
        if (completions != MPIDI_CH3I_progress_completion_count) {
            goto fn_completion;
        }
#ifdef _SMP_
        if (SMP_INIT)
            MPIDI_CH3I_SMP_read_progress(MPIDI_Process.my_pg);
        if (!SMP_ONLY) {
#endif
        /*CM code*/
        if (MPIDI_CH3I_Process.new_conn_complete) {
            /*New connection has been established*/
            MPIDI_CH3I_Process.new_conn_complete = 0;
            cm_handle_pending_send();
        }

#ifdef CKPT
        if (MPIDI_CH3I_Process.reactivation_complete) {
            /*Some channel has been reactivated*/
            MPIDI_CH3I_Process.reactivation_complete = 0;
            cm_handle_reactivation_complete();
        }
#endif

        mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer, is_blocking);
        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                     __LINE__, MPI_ERR_OTHER,
                                     "**ch3progress", 0);
            MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
            return mpi_errno;
        }
        if (vc_ptr == NULL) {
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
            MPIU_THREAD_CHECK_BEGIN
#ifdef SOLARIS
            if(spin_count > 100) {
#else
            if(spin_count > 5) {
#endif
                spin_count = 0;
                MPID_Thread_mutex_unlock(&MPIR_Process.global_mutex);
#ifdef CKPT
                MPIDI_CH3I_CR_unlock();
#endif                
                MPIDU_Yield();
                MPID_Thread_mutex_lock(&MPIR_Process.global_mutex);
#ifdef CKPT
                MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END
#endif
            spin_count++;
        } else {
            spin_count = 1;
#ifdef USE_SLEEP_YIELD
            MPIDI_Sleep_yield_count = 0;
#endif
            /*CM code*/
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV) {
                /*newly established connection on server side*/
                MPIDI_CH3I_CM_Establish(vc_ptr);
                cm_handle_pending_send();
            }
#ifdef CKPT
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV) {
                MPIDI_CH3I_CM_Establish(vc_ptr);
                MPIDI_CH3I_CM_Send_logged_msg(vc_ptr);
                if (vc_ptr->mrail.sreq_head) /*has rndv*/
                    PUSH_FLOWLIST(vc_ptr);
                if (!MPIDI_CH3I_CM_SendQ_empty(vc_ptr))
                    cm_send_pending_msg(vc_ptr);
            }
#endif

            mpi_errno = handle_read(vc_ptr, buffer);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER,
                                         "**ch3progress", 0);
                MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
                return mpi_errno;
            }
        }
#ifdef _SMP_
        } else {
            if (SMP_INIT) {
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                MPIU_THREAD_CHECK_BEGIN
                spin_count++;
                if(spin_count > 10) {
                    spin_count = 0;
                    MPID_Thread_mutex_unlock(&MPIR_Process.global_mutex);
#ifdef CKPT
                    MPIDI_CH3I_CR_unlock();
#endif
                    MPIDU_Yield();
                    MPID_Thread_mutex_lock(&MPIR_Process.global_mutex);
#ifdef CKPT
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END
#endif
            }
        }
#endif
        if (flowlist)
            MPIDI_CH3I_MRAILI_Process_rndv();
#ifdef CKPT
        if (MPIDI_CH3I_CR_Get_state()==MPICR_STATE_REQUESTED)
            /*Release the lock if it is about to checkpoint*/
            break;
#endif
    }
    while (completions == MPIDI_CH3I_progress_completion_count
           && is_blocking);

fn_completion:

fn_exit:
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting, count=%d",
                      MPIDI_CH3I_progress_completion_count - completions));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    DEBUG_PRINT("Exiting ch3 progress\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_test()
{
    MPIDI_VC_t *vc_ptr = NULL;
    int completion_count = MPIDI_CH3I_progress_completion_count;
    vbuf *buffer = NULL;
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS);
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS);

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    {
    if (MPIDI_CH3I_progress_blocked == TRUE) {
        DEBUG_PRINT("progress blocked\n");
        goto fn_exit;
    }
    }
#endif

#ifdef _SMP_ 
    /*needed if early send complete doesnot occur */
    if (SMP_INIT) {
	mpi_errno = MPIDI_CH3I_SMP_write_progress(MPIDI_Process.my_pg);

	if(mpi_errno) {
	    MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**progress");
	}

	/* check if we made any progress */
	if(completion_count != MPIDI_CH3I_progress_completion_count) {
	    goto fn_exit;
	}

	mpi_errno = MPIDI_CH3I_SMP_read_progress(MPIDI_Process.my_pg);
	if(mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

    if (!SMP_ONLY) {
#endif

        /*CM code*/
        if (MPIDI_CH3I_Process.new_conn_complete) {
            /*New connection has been established*/
            MPIDI_CH3I_Process.new_conn_complete = 0;
            cm_handle_pending_send();
        }

#ifdef CKPT
        if (MPIDI_CH3I_Process.reactivation_complete) {
            /*Some channel has been reactivated*/
            MPIDI_CH3I_Process.reactivation_complete = 0;
            cm_handle_reactivation_complete();
        }
#endif

        /* SS: Progress test should not be blocking */
        mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer, 0);
        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                        __LINE__, MPI_ERR_OTHER,
                        "**ch3progress", 0);
            MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
            return mpi_errno;
        }

        if(vc_ptr != NULL) {
            /*CM code*/
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV) {
                /*newly established connection on server side*/
                MPIDI_CH3I_CM_Establish(vc_ptr);
                cm_handle_pending_send();
            }

#ifdef CKPT
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV) {
                MPIDI_CH3I_CM_Establish(vc_ptr);
                MPIDI_CH3I_CM_Send_logged_msg(vc_ptr);
                if (vc_ptr->mrail.sreq_head) /*has rndv*/
                    PUSH_FLOWLIST(vc_ptr);
                if (!MPIDI_CH3I_CM_SendQ_empty(vc_ptr))
                    cm_send_pending_msg(vc_ptr);
            }
#endif

            mpi_errno = handle_read(vc_ptr, buffer);
            if (mpi_errno != MPI_SUCCESS) {
                DEBUG_PRINT("fail\n");
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER,
                                         "**ch3progress", 0);
                MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
                return mpi_errno;
            }
        }
#ifdef _SMP_
        }
#endif
        /* issue RDMA write ops if we got a clr_to_send */
        if (flowlist)
            MPIDI_CH3I_MRAILI_Process_rndv();

fn_exit:

#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    DEBUG_PRINT("Exiting ch3 progress test\n");
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_delay
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Progress_delay(unsigned int completion_count)
{
    int mpi_errno = MPI_SUCCESS;
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_continue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Progress_continue(unsigned int completion_count)
{
    return MPI_SUCCESS;
}

#endif /* MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE */

#ifndef MPIDI_CH3_Progress_poke
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_poke
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Progress_poke()
{
    int mpi_errno;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    mpi_errno = MPIDI_CH3I_Progress_test();
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**poke", 0);
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_POKE);
    return mpi_errno;
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_end
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#ifndef MPIDI_CH3_Progress_end
void MPIDI_CH3_Progress_end(MPID_Progress_state * state)
{
    /* MT - This function is empty for the single-threaded implementation */
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_init()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_INIT);

#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    MPIU_THREAD_CHECK_BEGIN
    MPID_Thread_cond_create(
            &MPIDI_CH3I_progress_completion_cond, NULL);
    MPIU_THREAD_CHECK_END
#endif

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_INIT);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_INIT);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_finalize()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS_FINALIZE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS_FINALIZE);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS_FINALIZE);
    return MPI_SUCCESS;
}

/*
 * MPIDI_CH3I_Request_adjust_iov()
 *
 * Adjust the iovec in the request by the supplied number of bytes.  If the iovec has been consumed, return true; otherwise return
 * false.
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Request_adjust_iov
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Request_adjust_iov(MPID_Request * req, MPIDI_msg_sz_t nb)
{
    int offset = req->ch.iov_offset;
    const int count = req->dev.iov_count;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);

    while (offset < count) {
        if (req->dev.iov[offset].MPID_IOV_LEN <= (unsigned int) nb) {
            nb -= req->dev.iov[offset].MPID_IOV_LEN;
            offset++;
        } else {
            req->dev.iov[offset].MPID_IOV_BUF =
                ((char *) req->dev.iov[offset].MPID_IOV_BUF) + nb;
            req->dev.iov[offset].MPID_IOV_LEN -= nb;
            req->ch.iov_offset = offset;
            DEBUG_PRINT("offset after adjust %d, count %d, remaining %d\n", 
                offset, req->dev.iov_count, req->dev.iov[offset].MPID_IOV_LEN);
            MPIDI_DBG_PRINTF((60, FCNAME, "adjust_iov returning FALSE"));
            MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);
            return FALSE;
        }
    }

    req->ch.iov_offset = 0;

    MPIDI_DBG_PRINTF((60, FCNAME, "adjust_iov returning TRUE"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);
    return TRUE;
}

#ifdef CKPT
#undef FUNCNAME
#define FUNCNAME cm_handle_reactivation_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_handle_reactivation_complete()
{
    int i;
    MPIDI_VC_t *vc;
    MPIDI_PG_t *pg;
    pg = MPIDI_Process.my_pg;
    for (i = 0; i < MPIDI_PG_Get_size(pg); i++) {
        if (i == MPIDI_Process.my_pg_rank)
            continue;
        MPIDI_PG_Get_vcr(pg, i, &vc);
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2) {
            MPIDI_CH3I_CM_Send_logged_msg(vc);
            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
            if (vc->mrail.sreq_head) /*has pending rndv*/ {
                PUSH_FLOWLIST(vc);
            }
            if (!MPIDI_CH3I_CM_SendQ_empty(vc))
                cm_send_pending_msg(vc);
        }
    }
    return MPI_SUCCESS;
}
#endif

static int cm_send_pending_msg(MPIDI_VC_t * vc)
{
    assert(vc->ch.state==MPIDI_CH3I_VC_STATE_IDLE);
    while (!MPIDI_CH3I_CM_SendQ_empty(vc)) {
        int i;
        int mpi_errno;
        struct MPID_Request * sreq;
        MPID_IOV * iov;
        int n_iov;

        sreq = MPIDI_CH3I_CM_SendQ_head(vc);
        iov=sreq->dev.iov;
        n_iov = sreq->dev.iov_count;
        void *databuf = NULL;

        {
            /*Code copied from ch3_isendv*/
            int nb;
            int pkt_len;
            int complete;
            /* MT - need some signalling to lock down our right to use the
               channel, thus insuring that the progress engine does also try to
               write */
            Calculate_IOV_len(iov, n_iov, pkt_len);

            if (pkt_len > MRAIL_MAX_EAGER_SIZE) {
                memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
                sreq->dev.iov_count = n_iov;
                mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq);
                if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                    mpi_errno = MPI_SUCCESS;
                }
                goto loop_exit;
            }

            if (sreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_SendReloadIOV) {
                /*reload iov */
                void *tmpbuf;
                int iter_iov;

                tmpbuf = MPIU_Malloc(sreq->dev.segment_size + pkt_len);
                databuf = tmpbuf;
                pkt_len = 0;
                /* First copy whatever has already been in iov set */
                for (iter_iov = 0; iter_iov < n_iov; iter_iov++) {
                    memcpy(tmpbuf, iov[iter_iov].MPID_IOV_BUF,
                            iov[iter_iov].MPID_IOV_LEN);
                    tmpbuf = (void *) ((unsigned long) tmpbuf +
                            iov[iter_iov].MPID_IOV_LEN);
                    pkt_len += iov[iter_iov].MPID_IOV_LEN;
                }
                DEBUG_PRINT("Pkt len after first stage %d\n", pkt_len);
                /* Second reload iov and copy */
                do {
                    sreq->dev.iov_count = MPID_IOV_LIMIT;
                    mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq,
                            sreq->dev.iov,
                            &sreq->dev.
                            iov_count);
                    /* --BEGIN ERROR HANDLING-- */
                    if (mpi_errno != MPI_SUCCESS) {
                        mpi_errno =
                            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                    FCNAME, __LINE__,
                                    MPI_ERR_OTHER,
                                    "**ch3|loadsendiov", 0);
                        goto loop_exit;
                    }
                    for (iter_iov = 0; iter_iov < sreq->dev.iov_count;
                            iter_iov++) {
                        memcpy(tmpbuf, sreq->dev.iov[iter_iov].MPID_IOV_BUF,
                                sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                        tmpbuf =
                            (void *) ((unsigned long) tmpbuf +
                                      sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                        pkt_len += sreq->dev.iov[iter_iov].MPID_IOV_LEN;
                    }
                } while (sreq->dev.OnDataAvail ==
			 MPIDI_CH3_ReqHandler_SendReloadIOV);
                iov[0].MPID_IOV_BUF = databuf;
                iov[0].MPID_IOV_LEN = pkt_len;
                n_iov = 1;
            }

            if (pkt_len > MRAIL_MAX_EAGER_SIZE) {
                memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
                sreq->dev.iov_count = n_iov;
                mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq);
                if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                    mpi_errno = MPI_SUCCESS;
                }
                goto loop_exit;
            }

            {
                /* TODO: Codes to send pkt through send/recv path */
                vbuf *buf;
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, pkt_len, &nb, &buf);
                DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);
                if (mpi_errno == MPI_SUCCESS) {
                    DEBUG_PRINT("[send path] eager send return %d bytes\n",
                            nb);
                    if (nb == 0) {
                        /* under layer cannot send out the msg because there is no credit or
                         * no send wqe available 
                         DEBUG_PRINT("Send 0 bytes\n");
                         create_request(sreq, iov, n_iov, 0, 0);
                         MPIDI_CH3I_SendQ_enqueue(vc, sreq);
                         */
                    } else {
                        MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
                        if (!complete) {
                            /*should not happen*/
                            assert(0);
                        } else {
                            vc->ch.send_active = MPIDI_CH3I_CM_SendQ_head(vc);
                        }
                    }
                } else if (MPI_MRAIL_MSG_QUEUED == mpi_errno) {
                    buf->sreq = (void *) sreq;
                    mpi_errno = MPI_SUCCESS;
                } else {
                    /* Connection just failed.  Mark the request complete and return an
                     * error. */
                    vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                    /* TODO: Create an appropriate error message based on the value of errno */
                    sreq->status.MPI_ERROR = MPI_ERR_INTERN;
                    /* MT - CH3U_Request_complete performs write barrier */
                    MPIDI_CH3U_Request_complete(sreq);
                }
                goto loop_exit;
            }
        }
loop_exit:
        if (databuf)
            MPIU_Free(databuf);
        /*If mpi_errno is not MPI_SUCCESS, error should be reported?*/
        /*Does sreq need to be freed? or upper layer will take care*/
        MPIDI_CH3I_CM_SendQ_dequeue(vc);
    }
    
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME cm_handle_pending_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_handle_pending_send()
{
    int i;
    MPIDI_VC_t *vc;
    MPIDI_PG_t *pg;
    int mpi_errno = MPI_SUCCESS;

    pg = MPIDI_Process.my_pg;
    for (i = 0; i < MPIDI_PG_Get_size(pg); i++) {
	MPIDI_PG_Get_vc(pg, i, &vc);
	if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
		&& !MPIDI_CH3I_CM_SendQ_empty(vc)) {
	    mpi_errno = cm_send_pending_msg(vc);
	    if(mpi_errno) MPIU_ERR_POP(mpi_errno);
	}
    }

fn_exit:
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME handle_read
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int handle_read(MPIDI_VC_t * vc, vbuf * buffer)
{
    unsigned char *original_buffer;
    int mpi_errno;
    int total_consumed = 0;
    int header_type;
    int first = 1;

    MPIDI_CH3_Pkt_rput_finish_t * rf_pkt;
    MPID_Request *rreq;

#ifdef MPIDI_MRAILI_COALESCE_ENABLED
    /* we don't know how many packets may be combined, so
     * we check after reading a packet to see if there are
     * more bytes yet to be consumed
     */

    buffer->content_consumed = 0;
    original_buffer = buffer->buffer;

    DEBUG_PRINT("[handle read] buffer %p\n", buffer);

    do {
        vc->ch.recv_active = vc->ch.req;
        buffer->content_consumed = 0;

        mpi_errno = handle_read_individual(vc, buffer, &header_type);
        if(MPI_SUCCESS != mpi_errno) {
            return mpi_errno;
        }

        buffer->pheader = (void *) ((uintptr_t) buffer->pheader + 
                buffer->content_consumed);
        total_consumed += buffer->content_consumed;

        if(MPIDI_CH3I_Seq(header_type) && !first) {
            vc->seqnum_recv++;
        }

        first = 0;
    } while(total_consumed != buffer->content_size);

    DEBUG_PRINT("Finished with buffer -- size: %d, consumed: %d\n", 
            buffer->content_size, buffer->content_consumed);

    /* put the original buffer address back */
    buffer->buffer = original_buffer; 
    buffer->pheader = original_buffer;
    
    DEBUG_PRINT("buffer set to: %p\n", buffer->buffer);
#else
    mpi_errno = handle_read_individual(vc, buffer, &header_type);
#endif

    /* by this point we can always free the vbuf */
    MPIDI_CH3I_MRAIL_Release_vbuf(buffer);

    return mpi_errno;
}

static int handle_read_individual(MPIDI_VC_t * vc, 
        vbuf * buffer, int *header_type)
{
    int mpi_errno;
    MPID_Request *req;
    int complete;
    int header_size = 0;
    int nb;
    MPIDI_CH3_Pkt_send_t *header;
    int packetized_recv = 0;
    int finished;

    /* Step one, ask lower level to provide header */
    /*  save header at req->ch.pkt, and return the header size */
    /*  ??TODO: Possibly just return the address of the header */

    DEBUG_PRINT("[handle read] pheader: %p\n", buffer->pheader);

    MPIDI_CH3I_MRAIL_Parse_header(vc, buffer, (void **)&header, &header_size);

#ifdef MPIDI_MRAILI_COALESCE_ENABLED
    buffer->content_consumed = header_size;
#endif

    DEBUG_PRINT("[handle read] header type %d\n", header->type);

    *header_type = header->type;

    req = vc->ch.recv_active;

    switch(header->type) {
#ifdef CKPT
    case MPIDI_CH3_PKT_CM_SUSPEND:
    case MPIDI_CH3_PKT_CM_REACTIVATION_DONE:
        MPIDI_CH3I_CM_Handle_recv(vc, header->type, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
        MPIDI_CH3I_CR_Handle_recv(vc, header->type, buffer);
        goto fn_exit;
#endif
    case MPIDI_CH3_PKT_NOOP: 
#ifdef RDMA_CM 
        {
	    if (vc->ch.state == MPIDI_CH3I_VC_STATE_IWARP_CLI_WAITING){

		/* This case is needed only for multi-rail with rdma_cm */

		vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		vc->state = MPIDI_VC_STATE_ACTIVE;
		MPIDI_CH3I_Process.new_conn_complete = 1;
                DEBUG_PRINT("NOOP Received, RDMA CM is setting up "
			    "the proper status on the client side for multirail.\n");
	    }

            if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {

                rdma_cm_iwarp_msg_count[vc->pg_rank]++;
                if (rdma_cm_iwarp_msg_count[vc->pg_rank] >= rdma_num_rails 
                        && rdma_cm_connect_count[vc->pg_rank] >= 
                        rdma_num_rails) {
                    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		    vc->state = MPIDI_VC_STATE_ACTIVE;
                    MPIDI_CH3I_Process.new_conn_complete = 1;
		    if (rdma_num_rails > 1)
			MRAILI_Send_noop(vc, 0);
                }
                DEBUG_PRINT("NOOP Received, RDMA CM is setting up "
			    "the proper status on the server side.\n");
            }
        }
#endif
    case MPIDI_CH3_PKT_ADDRESS:
        DEBUG_PRINT("NOOP received, don't need to proceed\n");
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
        DEBUG_PRINT("Packetized data received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Packetized_recv_data(vc, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RNDV_R3_DATA:
        DEBUG_PRINT("R3 data received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Rendezvouz_r3_recv_data(vc, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RPUT_FINISH:
        DEBUG_PRINT("RPUT finish received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Rendezvous_rput_finish(vc, (void *)header);
        goto fn_exit;
    case MPIDI_CH3_PKT_RGET_FINISH:
        DEBUG_PRINT("RGET finish received\n");
        mpi_errno = MPIDI_CH3_Rendezvous_rget_send_finish(vc, (void *)header);
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
        packetized_recv = 1;
        header_size +=
            ((MPIDI_CH3_Pkt_packetized_send_start_t *)(header))->origin_head_size;
#ifdef MPIDI_MRAILI_COALESCE_ENABLED
        buffer->content_consumed = header_size;
#endif
        header = (void *)((uintptr_t)header +
            sizeof(MPIDI_CH3_Pkt_packetized_send_start_t));
        break;
    }

    DEBUG_PRINT("[handle read] header eager %d, headersize %d",
                header->type, header_size);
    /* Step two, load request according to the header content */
    mpi_errno =
        MPIDI_CH3U_Handle_recv_pkt(vc, (void *)header, &vc->ch.recv_active);

    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**fail",
                                         0);
        return mpi_errno;
    }
    DEBUG_PRINT("[recv: progress] about to fill request, recv_active %p\n",
                vc->ch.recv_active);

    if (vc->ch.recv_active != NULL) {
        /* Step three, ask lower level to fill the request */
        /*      request is vc->ch.recv_active */

        if (1 == packetized_recv) {
            MPIDI_CH3_Packetized_recv_req(vc, vc->ch.recv_active);
            if (mpi_errno != MPI_SUCCESS) {
                return mpi_errno;
            }
        }

        mpi_errno =
            MPIDI_CH3I_MRAIL_Fill_Request(vc->ch.recv_active, buffer,
                                           header_size, &nb);

        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER, "**fail", 0);
            MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_READ);
            fprintf(stderr, "fail to fill request\n");
            return mpi_errno;
        }

        req = vc->ch.recv_active;
        DEBUG_PRINT("recv: handle read] nb %d, iov n %d, len %d, VBUFSIZE %d\n", 
            nb, req->dev.iov_count, req->dev.iov[0].MPID_IOV_LEN, VBUF_BUFFER_SIZE);

        finished = MPIDI_CH3I_Request_adjust_iov(req, nb);

        if (finished) {
            /* Read operation complete */
            DEBUG_PRINT("[recv: handle read] adjust iov correct\n");
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);
            DEBUG_PRINT("[recv: handle read] adjust req fine, complete %d\n",
                    complete);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno,
                                     MPIR_ERR_RECOVERABLE, FCNAME,
                                     __LINE__, MPI_ERR_OTHER, "**fail", 0);
            }

            while (!complete) {
                header_size += nb;
                /* Fill request again */
                mpi_errno =
                    MPIDI_CH3I_MRAIL_Fill_Request(req, buffer, header_size,
                                               &nb);

                if (mpi_errno != MPI_SUCCESS) {
                    mpi_errno =
                        MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER, "**fail | fill request error", 0);
                    return mpi_errno;
                }

                finished = MPIDI_CH3I_Request_adjust_iov(req, nb);

                if (!finished) {
                    if (!packetized_recv) {
                        mpi_errno =
                            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                         FCNAME, __LINE__,
                                         MPI_ERR_OTHER, "**fail | recv data doesn't \
                                         match", 0);
                        return mpi_errno;
                    }
                    goto fn_exit;
                }

                mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);
                if (mpi_errno != MPI_SUCCESS) {
                    mpi_errno =
                        MPIR_Err_create_code(mpi_errno,
                                         MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER,
                                         "**fail | recv req error", 0);
                    return mpi_errno;
                }
            }

            /* If the communication is packetized, we are expecing more packets for the
             * request. We encounter an error if the request finishes at this stage */
            if (packetized_recv) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                         FCNAME, __LINE__,
                         MPI_ERR_OTHER, "**fail | More data arrives than recv side's \
                         post size", 0);
                return mpi_errno;
            }
        } else if (!packetized_recv) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                     FCNAME, __LINE__,
                     MPI_ERR_OTHER, "**fail | recv data doesn't match", 0);
            return mpi_errno;
        } else {
            DEBUG_PRINT("unfinished req left to packetized send\n");
        }
        vc->ch.recv_active = NULL;
    } else {
        /* we are getting a 0 byte msg header */
    }
  fn_exit:
    DEBUG_PRINT("exiting handle read\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_write_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_write_progress()
{
    MPIDI_PG_t *pg;
    int mpi_errno;
    int nb, i;
    MPIDI_VC_t *vc;
    int complete;
    MPIDI_STATE_DECL(MPID_STATE_HANDLE_WRITTEN);

    MPIDI_FUNC_ENTER(MPID_STATE_HANDLE_WRITTEN);

    /*MPIDI_DBG_PRINTF((60, FCNAME, "entering")); */
    pg = MPIDI_Process.my_pg;
    for (i = 0; i < MPIDI_PG_Get_size(pg); i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);

        while (vc->ch.send_active != NULL) {
            MPID_Request *req = vc->ch.send_active;

            MPIU_Assert(req->ch.iov_offset < req->dev.iov_count);
            /*MPIDI_DBG_PRINTF((60, FCNAME, "calling rdma_put_datav")); */
            mpi_errno =
                MPIDI_CH3I_RDMA_put_datav(vc,
                                          req->dev.iov +
                                          req->ch.iov_offset,
                                          req->dev.iov_count -
                                          req->ch.iov_offset, &nb);
            if (mpi_errno != MPI_SUCCESS) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                         __LINE__, MPI_ERR_OTHER,
                                         "**write_progress", 0);
                return mpi_errno;
            }
            MPIDI_DBG_PRINTF((60, FCNAME, "shm_writev returned %d", nb));

            if (nb > 0) {
                if (MPIDI_CH3I_Request_adjust_iov(req, nb)) {
                    /* Write operation complete */
                    mpi_errno =
                        MPIDI_CH3U_Handle_send_req(vc, req, &complete);
                    if (mpi_errno != MPI_SUCCESS) {
                        mpi_errno =
                            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                                 FCNAME, __LINE__,
                                                 MPI_ERR_OTHER, "**fail",
                                                 0);
                        MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_WRITTEN);
                        return mpi_errno;
                    }
                    if (complete) {
                        MPIDI_CH3I_SendQ_dequeue(vc);
                    }
                    vc->ch.send_active = MPIDI_CH3I_SendQ_head(vc);
                } else {
                    MPIDI_DBG_PRINTF((65, FCNAME,
                                      "iovec updated by %d bytes but not complete",
                                      nb));
                    MPIU_Assert(req->ch.iov_offset < req->dev.iov_count);
                    break;
                }
            } else {
                MPIDI_DBG_PRINTF((65, FCNAME,
                                  "shm_post_writev returned %d bytes",
                                  nb));
                break;
            }
        }
    }

    /*MPIDI_DBG_PRINTF((60, FCNAME, "exiting")); */

    MPIDI_FUNC_EXIT(MPID_STATE_HANDLE_WRITTEN);
    return MPI_SUCCESS;
}

/* vi:set sw=4 tw=80: */
