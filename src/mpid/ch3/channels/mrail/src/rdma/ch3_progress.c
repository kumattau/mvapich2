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
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3_impl.h"
#include "mpidu_process_locks.h"        /* for MPIDU_Yield */
#include "mpiutil.h"

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
    }

    return 0;
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
#define FUNCNAME MPIDI_CH3_Get_business_card
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Get_business_card(int myRank, char *value, int length)
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connection_terminate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Connection_terminate(MPIDI_VC_t * vc)
{
    /* There is no post_close for shm connections so 
     * handle them as closed immediately. */
    int mpi_errno = MPIDI_CH3U_Handle_connection(vc, MPIDI_VC_EVENT_TERMINATED);

    if (mpi_errno != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:
    return mpi_errno;
}


#ifndef MPIDI_CH3_Progress_start
void MPIDI_CH3_Progress_start(MPID_Progress_state * state)
{
    /* MT - This function is empty for the single-threaded implementation */
}
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress(int is_blocking, MPID_Progress_state * state)
{
    MPIDI_VC_t *vc_ptr = NULL;
    int mpi_errno = MPI_SUCCESS;
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

    do
    {
        /*needed if early send complete does not occur */
        if (SMP_INIT && (mpi_errno = MPIDI_CH3I_SMP_write_progress(MPIDI_Process.my_pg)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if (completions != MPIDI_CH3I_progress_completion_count) {
            goto fn_completion;
        }

        if (SMP_INIT)
            MPIDI_CH3I_SMP_read_progress(MPIDI_Process.my_pg);
        if (!SMP_ONLY) {

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

        if((mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer, is_blocking)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if (vc_ptr == NULL) {
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
            MPIU_THREAD_CHECK_BEGIN
#ifdef SOLARIS
            if(spin_count > 100)
#else
            if(spin_count > 5)
#endif
	    {
                spin_count = 0;
                MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
#ifdef CKPT
                MPIDI_CH3I_CR_unlock();
#endif                
                MPIDU_Yield();
                MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
#ifdef CKPT
                MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END
#endif
            ++spin_count;
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

            if ((mpi_errno = handle_read(vc_ptr, buffer)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }
        } else {
            if (SMP_INIT) {
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                MPIU_THREAD_CHECK_BEGIN
                ++spin_count;
                if(spin_count > 10) {
                    spin_count = 0;
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
#ifdef CKPT
                    MPIDI_CH3I_CR_unlock();
#endif
                    MPIDU_Yield();
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
#ifdef CKPT
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END
#endif
            }
        }

        if (flowlist)
            MPIDI_CH3I_MRAILI_Process_rndv();
#ifdef CKPT
        if (MPIDI_CH3I_CR_Get_state()==MPICR_STATE_REQUESTED)
        {
            /*Release the lock if it is about to checkpoint*/
            break;
        }
#endif
    }
    while (completions == MPIDI_CH3I_progress_completion_count
           && is_blocking);

fn_completion:
fn_fail:
fn_exit:
#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif
    MPIDI_DBG_PRINTF((50, FCNAME, "exiting, count=%d",
                      MPIDI_CH3I_progress_completion_count - completions));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    DEBUG_PRINT("Exiting ch3 progress\n");
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Progress_test()
{
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS);
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS);

#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif /* defined(CKPT) */

#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    if (MPIDI_CH3I_progress_blocked == TRUE)
    {
        DEBUG_PRINT("progress blocked\n");
        goto fn_exit;
    }
#endif /* (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE) */

    /*needed if early send complete doesnot occur */
    if (SMP_INIT)
    {
	if ((mpi_errno = MPIDI_CH3I_SMP_write_progress(MPIDI_Process.my_pg)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
	}

        int completion_count = MPIDI_CH3I_progress_completion_count;

	/* check if we made any progress */
	if (completion_count != MPIDI_CH3I_progress_completion_count)
        {
	    goto fn_exit;
	}

	if ((mpi_errno = MPIDI_CH3I_SMP_read_progress(MPIDI_Process.my_pg)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    if (!SMP_ONLY) 
    {
        /*CM code*/
        if (MPIDI_CH3I_Process.new_conn_complete)
        {
            /*New connection has been established*/
            MPIDI_CH3I_Process.new_conn_complete = 0;
            cm_handle_pending_send();
        }

#if defined(CKPT)
        if (MPIDI_CH3I_Process.reactivation_complete)
        {
            /*Some channel has been reactivated*/
            MPIDI_CH3I_Process.reactivation_complete = 0;
            cm_handle_reactivation_complete();
        }
#endif /* defined(CKPT) */

        MPIDI_VC_t *vc_ptr = NULL;
        vbuf *buffer = NULL;

        /* SS: Progress test should not be blocking */
        if ((mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer, 0)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if (vc_ptr != NULL)
        {
            /*CM code*/
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV)
            {
                /*newly established connection on server side*/
                MPIDI_CH3I_CM_Establish(vc_ptr);
                cm_handle_pending_send();
            }

#if defined(CKPT)
            if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_SRV)
            {
                MPIDI_CH3I_CM_Establish(vc_ptr);
                MPIDI_CH3I_CM_Send_logged_msg(vc_ptr);

                if (vc_ptr->mrail.sreq_head)
                {
                    /* has rndv */
                    PUSH_FLOWLIST(vc_ptr);
                }

                if (!MPIDI_CH3I_CM_SendQ_empty(vc_ptr))
                {
                    cm_send_pending_msg(vc_ptr);
                }
            }
#endif /* defined(CKPT) */

            if ((mpi_errno = handle_read(vc_ptr, buffer)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }
    }

    /* issue RDMA write ops if we got a clr_to_send */
    if (flowlist)
    {
        MPIDI_CH3I_MRAILI_Process_rndv();
    }

fn_fail:
fn_exit:
#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif /* defined(CKPT) */

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    DEBUG_PRINT("Exiting ch3 progress test\n");
    return mpi_errno;
}


#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_delay
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Progress_delay(unsigned int completion_count)
{
    return MPI_SUCCESS;
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

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_end
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#if !defined(MPIDI_CH3_Progress_end)
void MPIDI_CH3_Progress_end(MPID_Progress_state * state)
{
    /* MT - This function is empty for the single-threaded implementation */
}
#endif /* !defined(MPIDI_CH3_Progress_end) */

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
#endif /* (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE) */

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
    int offset = req->dev.iov_offset;
    const int count = req->dev.iov_count;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);

    while (offset < count) {
        if (req->dev.iov[offset].MPID_IOV_LEN <= (unsigned int) nb) {
            nb -= req->dev.iov[offset].MPID_IOV_LEN;
            ++offset;
        } else {
            req->dev.iov[offset].MPID_IOV_BUF =
                ((char *) req->dev.iov[offset].MPID_IOV_BUF) + nb;
            req->dev.iov[offset].MPID_IOV_LEN -= nb;
            req->dev.iov_offset = offset;
            DEBUG_PRINT("offset after adjust %d, count %d, remaining %d\n", 
                offset, req->dev.iov_count, req->dev.iov[offset].MPID_IOV_LEN);
            MPIDI_DBG_PRINTF((60, FCNAME, "adjust_iov returning FALSE"));
            MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);
            return FALSE;
        }
    }

    req->dev.iov_offset = 0;

    MPIDI_DBG_PRINTF((60, FCNAME, "adjust_iov returning TRUE"));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_REQUEST_ADJUST_IOV);
    return TRUE;
}

#if defined(CKPT)
#undef FUNCNAME
#define FUNCNAME cm_handle_reactivation_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_handle_reactivation_complete()
{
    int i = 0;
    MPIDI_VC_t* vc = NULL;
    MPIDI_PG_t* pg = MPIDI_Process.my_pg;

    for (; i < MPIDI_PG_Get_size(pg); ++i)
    {
        if (i == MPIDI_Process.my_pg_rank)
        {
            continue;
        }

        MPIDI_PG_Get_vc(pg, i, &vc);

        if (vc->ch.state == MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2)
        {
            MPIDI_CH3I_CM_Send_logged_msg(vc);
            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;

            if (vc->mrail.sreq_head)
            {
                /*has pending rndv*/
                PUSH_FLOWLIST(vc);
            }

            if (!MPIDI_CH3I_CM_SendQ_empty(vc))
            {
                cm_send_pending_msg(vc);
            }
        }
    }

    return MPI_SUCCESS;
}
#endif /* defined(CKPT) */

#undef FUNCNAME
#define FUNCNAME cm_send_pending_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
static int cm_send_pending_msg(MPIDI_VC_t * vc)
{
    MPIDI_STATE_DECL(MPID_STATE_CM_SENDING_PENDING_MSG);
    MPIDI_FUNC_ENTER(MPID_STATE_CM_SENDING_PENDING_MSG);
    int mpi_errno = MPI_SUCCESS;

    MPIU_Assert(vc->ch.state==MPIDI_CH3I_VC_STATE_IDLE);

    while (!MPIDI_CH3I_CM_SendQ_empty(vc)) {
        int i;
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

            if (pkt_len > MRAIL_MAX_EAGER_SIZE)
            {
                memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
                sreq->dev.iov_count = n_iov;

                switch ((mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq)))
                {
                    case MPI_MRAIL_MSG_QUEUED:
                            mpi_errno = MPI_SUCCESS;
                        break;
                    case MPI_SUCCESS:
                        break;
                    default:
                        MPIU_ERR_POP(mpi_errno);
                }
                
                goto loop_exit;
            }

            if (sreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_SendReloadIOV)
            {
                /*reload iov */
                void *tmpbuf = NULL;
                int iter_iov = 0;

                if ((tmpbuf = MPIU_Malloc(sreq->dev.segment_size + pkt_len)) == NULL)
                {
                    MPIU_CHKMEM_SETERR(mpi_errno, sreq->dev.segment_size + pkt_len, "temporary iov buffer");
                }

                databuf = tmpbuf;
                pkt_len = 0;

                /* First copy whatever has already been in iov set */
                for (; iter_iov < n_iov; ++iter_iov)
                {
                    memcpy(tmpbuf, iov[iter_iov].MPID_IOV_BUF,
                            iov[iter_iov].MPID_IOV_LEN);
                    tmpbuf = (void *) ((unsigned long) tmpbuf +
                            iov[iter_iov].MPID_IOV_LEN);
                    pkt_len += iov[iter_iov].MPID_IOV_LEN;
                }

                DEBUG_PRINT("Pkt len after first stage %d\n", pkt_len);
                /* Second reload iov and copy */
                do
                {
                    sreq->dev.iov_count = MPID_IOV_LIMIT;

                    if ((mpi_errno = MPIDI_CH3U_Request_load_send_iov(
                        sreq,
                        sreq->dev.iov,
                        &sreq->dev.
                        iov_count)) != MPI_SUCCESS)
                    {
                        MPIU_ERR_POP(mpi_errno);
                    }

                    for (iter_iov = 0; iter_iov < sreq->dev.iov_count; ++iter_iov)
                    {
                        memcpy(tmpbuf, sreq->dev.iov[iter_iov].MPID_IOV_BUF,
                                sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                        tmpbuf =
                            (void *) ((unsigned long) tmpbuf +
                                      sreq->dev.iov[iter_iov].MPID_IOV_LEN);
                        pkt_len += sreq->dev.iov[iter_iov].MPID_IOV_LEN;
                    }
                }
                while (sreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_SendReloadIOV);
                
                iov[0].MPID_IOV_BUF = databuf;
                iov[0].MPID_IOV_LEN = pkt_len;
                n_iov = 1;
            }

            if (pkt_len > MRAIL_MAX_EAGER_SIZE)
            {
                memcpy(sreq->dev.iov, iov, n_iov * sizeof(MPID_IOV));
                sreq->dev.iov_count = n_iov;

                switch ((mpi_errno = MPIDI_CH3_Packetized_send(vc, sreq)))
                {
                    case MPI_MRAIL_MSG_QUEUED:
                            mpi_errno = MPI_SUCCESS;
                        break;
                    case MPI_SUCCESS:
                        break;
                    default:
                        MPIU_ERR_POP(mpi_errno);
                }

                goto loop_exit;
            }

            {
                /* TODO: Codes to send pkt through send/recv path */
                vbuf *buf;
                mpi_errno =
                    MPIDI_CH3I_MRAILI_Eager_send(vc, iov, n_iov, pkt_len, &nb, &buf);
                DEBUG_PRINT("[istartmsgv] mpierr %d, nb %d\n", mpi_errno, nb);

                switch(mpi_errno)
                {
                case MPI_SUCCESS:
                        DEBUG_PRINT("[send path] eager send return %d bytes\n", nb);
#if 0
                        if (nb == 0)
                        {
                            /* under layer cannot send out the msg because there is no credit or
                             * no send wqe available 
                             * DEBUG_PRINT("Send 0 bytes\n");
                             * create_request(sreq, iov, n_iov, 0, 0);
                             * MPIDI_CH3I_SendQ_enqueue(vc, sreq);
                             */
                        }
                        else
#endif /* 0 */

                        if (nb)
                        {
                            MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);

                            /* TODO: Check return code of MPIDI_CH3U_Handle_send_req
                             * and produce an error on failure.
                             */

                            vc->ch.send_active = MPIDI_CH3I_CM_SendQ_head(vc);
                        }
                    break;
                case MPI_MRAIL_MSG_QUEUED:
                        buf->sreq = (void *) sreq;
                        mpi_errno = MPI_SUCCESS;
                    break;
                default:
                        /* Connection just failed.  Mark the request complete and return an
                         * error. */
                        vc->ch.state = MPIDI_CH3I_VC_STATE_FAILED;
                        /* TODO: Create an appropriate error message based on the value of errno */
                        sreq->status.MPI_ERROR = MPI_ERR_INTERN;
                        /* MT - CH3U_Request_complete performs write barrier */
                        MPIDI_CH3U_Request_complete(sreq);
                        MPIU_ERR_POP(mpi_errno);
                    break;
                }

                goto loop_exit;
            }
        }

loop_exit:
        if (databuf)
        {
            MPIU_Free(databuf);
        }

        /*Does sreq need to be freed? or upper layer will take care*/
        MPIDI_CH3I_CM_SendQ_dequeue(vc);
    }

    MPIDI_FUNC_EXIT(MPID_STATE_CM_SENDING_PENDING_MSG);
    return mpi_errno;

fn_fail:
    goto loop_exit;
}

#undef FUNCNAME
#define FUNCNAME cm_handle_pending_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int cm_handle_pending_send()
{
    int i = 0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t* vc = NULL;
    MPIDI_PG_t* pg = MPIDI_Process.my_pg;

    for (; i < MPIDI_PG_Get_size(pg); ++i)
    {
	MPIDI_PG_Get_vc(pg, i, &vc);

	if (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE
            && !MPIDI_CH3I_CM_SendQ_empty(vc))
        {
	    if ((mpi_errno = cm_send_pending_msg(vc)) != MPI_SUCCESS)
	    {
                MPIU_ERR_POP(mpi_errno);
            }
	}
    }

fn_fail:
fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME handle_read
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int handle_read(MPIDI_VC_t * vc, vbuf * buffer)
{
    int mpi_errno = MPI_SUCCESS;
    int header_type;

#if defined(MPIDI_MRAILI_COALESCE_ENABLED)
    /* we don't know how many packets may be combined, so
     * we check after reading a packet to see if there are
     * more bytes yet to be consumed
     */

    buffer->content_consumed = 0;
    unsigned char* original_buffer = buffer->buffer;

    DEBUG_PRINT("[handle read] buffer %p\n", buffer);

    int total_consumed = 0;

    /* TODO: Refactor this algorithm so the first flag is not used. */
    int first = 1;

    do
    {
        vc->ch.recv_active = vc->ch.req;
        buffer->content_consumed = 0;

        if ((mpi_errno = handle_read_individual(vc, buffer, &header_type)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        buffer->pheader = (void *) ((uintptr_t) buffer->pheader + buffer->content_consumed);
        total_consumed += buffer->content_consumed;

        if (MPIDI_CH3I_Seq(header_type) && !first)
        {
            ++vc->seqnum_recv;
        }

        first = 0;
    }
    while (total_consumed != buffer->content_size);

    DEBUG_PRINT("Finished with buffer -- size: %d, consumed: %d\n", buffer->content_size, buffer->content_consumed);

    /* put the original buffer address back */
    buffer->buffer = original_buffer; 
    buffer->pheader = original_buffer;
    
    DEBUG_PRINT("buffer set to: %p\n", buffer->buffer);
#else /* defined(MPIDI_MRAILI_COALESCE_ENABLED) */
    if ((mpi_errno = handle_read_individual(vc, buffer, &header_type)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }
#endif /* defined(MPIDI_MRAILI_COALESCE_ENABLED) */

fn_fail:
    /* by this point we can always free the vbuf */
    MPIDI_CH3I_MRAIL_Release_vbuf(buffer);

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME handle_read_individual
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int handle_read_individual(MPIDI_VC_t* vc, vbuf* buffer, int* header_type)
{
    int mpi_errno = MPI_SUCCESS;
    int header_size = 0;
    MPIDI_CH3_Pkt_send_t* header = NULL;
    int packetized_recv = 0;

    /* Step one, ask lower level to provide header */
    /*  save header at req->dev.pending_pkt, and return the header size */
    /*  ??TODO: Possibly just return the address of the header */

    DEBUG_PRINT("[handle read] pheader: %p\n", buffer->pheader);

    MPIDI_CH3I_MRAIL_Parse_header(vc, buffer, (void **)&header, &header_size);

#if defined(MPIDI_MRAILI_COALESCE_ENABLED)
    buffer->content_consumed = header_size;
#endif /* defined(MPIDI_MRAILI_COALESCE_ENABLED) */

    DEBUG_PRINT("[handle read] header type %d\n", header->type);

    *header_type = header->type;
    MPID_Request* req = vc->ch.recv_active;

    switch(header->type)
    {
#if defined(CKPT)
    case MPIDI_CH3_PKT_CM_SUSPEND:
    case MPIDI_CH3_PKT_CM_REACTIVATION_DONE:
            MPIDI_CH3I_CM_Handle_recv(vc, header->type, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_CR_REMOTE_UPDATE:
            MPIDI_CH3I_CR_Handle_recv(vc, header->type, buffer);
        goto fn_exit;
#endif /* defined(CKPT) */
    case MPIDI_CH3_PKT_NOOP: 
#if defined(RDMA_CM)
            if (vc->ch.state == MPIDI_CH3I_VC_STATE_IWARP_CLI_WAITING)
            {
		/* This case is needed only for multi-rail with rdma_cm */
		vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		vc->state = MPIDI_VC_STATE_ACTIVE;
		MPIDI_CH3I_Process.new_conn_complete = 1;
                DEBUG_PRINT("NOOP Received, RDMA CM is setting the proper status on the client side for multirail.\n");
	    }

	    if ((vc->ch.state == MPIDI_CH3I_VC_STATE_IWARP_SRV_WAITING)
		|| (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_SRV))
            {
                ++rdma_cm_iwarp_msg_count[vc->pg_rank];

                if (rdma_cm_iwarp_msg_count[vc->pg_rank] >= rdma_num_rails 
                    && rdma_cm_connect_count[vc->pg_rank] >= rdma_num_rails)
                {
                    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
		    vc->state = MPIDI_VC_STATE_ACTIVE;
                    MPIDI_CH3I_Process.new_conn_complete = 1;
		    MRAILI_Send_noop(vc, 0);
                }

                DEBUG_PRINT("NOOP Received, RDMA CM is setting up the proper status on the server side.\n");
            }
#endif /* defined(RDMA_CM) */
    case MPIDI_CH3_PKT_ADDRESS:
            DEBUG_PRINT("NOOP received, don't need to proceed\n");
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
            DEBUG_PRINT("Packetized data received, don't need to proceed\n");
            MPIDI_CH3_Packetized_recv_data(vc, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RNDV_R3_DATA:
            DEBUG_PRINT("R3 data received, don't need to proceed\n");
            MPIDI_CH3_Rendezvouz_r3_recv_data(vc, buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RPUT_FINISH:
            DEBUG_PRINT("RPUT finish received, don't need to proceed\n");
            MPIDI_CH3_Rendezvous_rput_finish(vc, (void*) header);
        goto fn_exit;
    case MPIDI_CH3_PKT_RGET_FINISH:
            DEBUG_PRINT("RGET finish received\n");
            MPIDI_CH3_Rendezvous_rget_send_finish(vc, (void*) header);
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
            packetized_recv = 1;
            header_size += ((MPIDI_CH3_Pkt_packetized_send_start_t*) header)->origin_head_size;
#if defined(MPIDI_MRAILI_COALESCE_ENABLED)
            buffer->content_consumed = header_size;
#endif /* defined(MPIDI_MRAILI_COALESCE_ENABLED) */
            header = (void *)((uintptr_t) header + sizeof(MPIDI_CH3_Pkt_packetized_send_start_t));
        break;
    }

    DEBUG_PRINT("[handle read] header eager %d, headersize %d", header->type, header_size);

    MPIDI_msg_sz_t buflen = sizeof(MPIDI_CH3_Pkt_t);

    /* Step two, load request according to the header content */
    if ((mpi_errno = MPIDI_CH3U_Handle_recv_pkt(
        vc,
        (void*) header,
        &buflen,
        &vc->ch.recv_active)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    DEBUG_PRINT("[recv: progress] about to fill request, recv_active %p\n", vc->ch.recv_active);

    if (vc->ch.recv_active != NULL)
    {
        /* Step three, ask lower level to fill the request */
        /*      request is vc->ch.recv_active */

        if (packetized_recv == 1)
        {
            if ((mpi_errno = MPIDI_CH3_Packetized_recv_req(
                    vc,
                    vc->ch.recv_active)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }

        int nb;

        if ((mpi_errno = MPIDI_CH3I_MRAIL_Fill_Request(
                vc->ch.recv_active,
                buffer,
                header_size,
                &nb)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        req = vc->ch.recv_active;
        DEBUG_PRINT(
            "recv: handle read] nb %d, iov n %d, len %d, VBUFSIZE %d\n", 
            nb,
            req->dev.iov_count,
            req->dev.iov[0].MPID_IOV_LEN,
            VBUF_BUFFER_SIZE
        );

        if (MPIDI_CH3I_Request_adjust_iov(req, nb))
        {
            /* Read operation complete */
            DEBUG_PRINT("[recv: handle read] adjust iov correct\n");
            int complete;

            if ((mpi_errno = MPIDI_CH3U_Handle_recv_req(
                    vc,
                    req, 
                    &complete)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }

            DEBUG_PRINT("[recv: handle read] adjust req fine, complete %d\n", complete);

            while (!complete)
            {
                header_size += nb;

                /* Fill request again */
                if ((mpi_errno = MPIDI_CH3I_MRAIL_Fill_Request(
                        req,
                        buffer,
                        header_size,
                        &nb)) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }

                if (!MPIDI_CH3I_Request_adjust_iov(req, nb))
                {
                    if (!packetized_recv)
                    {
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    }

                    goto fn_exit;
                }

                if ((mpi_errno = MPIDI_CH3U_Handle_recv_req(
                        vc,
                        req,
                        &complete)
                    ) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
            }

            /* If the communication is packetized, we are expecing more packets for the
             * request. We encounter an error if the request finishes at this stage */
            if (packetized_recv)
            {
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            }
        }
        else if (!packetized_recv)
        {
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        }
#if defined(DEBUG)
        else
        {
            DEBUG_PRINT("unfinished req left to packetized send\n");
        }
#endif /* defined(DEBUG) */
        vc->ch.recv_active = NULL;
    }
#if defined(DEBUG)
    else
    {
        /* we are getting a 0 byte msg header */
    }
#endif /* if defined(DEBUG) */

fn_fail:
fn_exit:
    DEBUG_PRINT("exiting handle read\n");
    return mpi_errno;
}

/* vi:set sw=4 */