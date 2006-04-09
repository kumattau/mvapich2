/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2006, The Ohio State University. All rights
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

volatile unsigned int MPIDI_CH3I_progress_completion_count = 0;
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    volatile int MPIDI_CH3I_progress_blocked = FALSE;
    volatile int MPIDI_CH3I_progress_wakeup_signalled = FALSE;
    static int MPIDI_CH3I_Progress_delay(unsigned int completion_count);
    static int MPIDI_CH3I_Progress_continue(unsigned int completion_count);
    MPID_Thread_cond_t MPIDI_CH3I_progress_completion_cond;
#endif

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

    /* There is no post_close for shm connections so handle them as closed immediately. */
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

# if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    {
    if (MPIDI_CH3I_progress_blocked == TRUE) {
        MPIDI_DBG_PRINTF((50, FCNAME, "Progress engine is busy"));
        MPIDI_CH3I_Progress_delay(MPIDI_CH3I_progress_completion_count);
        goto fn_exit;
    } else {
        MPIDI_CH3I_progress_blocked = TRUE;
    }
    }
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
        mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer);
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
            MPID_Thread_mutex_unlock(&MPIR_Process.global_mutex);
#endif
            if (spin_count >= MPIDI_Process.my_pg->ch.nRDMAWaitSpinCount) {
#ifdef USE_SLEEP_YIELD
                if (spin_count >=
                    MPIDI_Process.my_pg->ch.nRDMAWaitYieldCount) {
                    MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_SLEEP_YIELD);
                    MPIDU_Sleep_yield();
                    MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_SLEEP_YIELD);
                } else {
                    MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
                    MPIDU_Yield();
                    MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
                }
#else
                MPIDI_FUNC_ENTER(MPID_STATE_MPIDU_YIELD);
                MPIDU_Yield();
                MPIDI_FUNC_EXIT(MPID_STATE_MPIDU_YIELD);
#endif
            }
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
            MPID_Thread_mutex_lock(&MPIR_Process.global_mutex);
#endif
            spin_count++;
        } else {
            spin_count = 1;
#ifdef USE_SLEEP_YIELD
            MPIDI_Sleep_yield_count = 0;
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
        } else {
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
            MPID_Thread_mutex_unlock(&MPIR_Process.global_mutex);
            MPID_Thread_mutex_lock(&MPIR_Process.global_mutex);
#endif
        }
#endif
        if (flowlist)
            MPIDI_CH3I_MRAILI_Process_rndv();
    }
    while (completions == MPIDI_CH3I_progress_completion_count
           && is_blocking);

fn_completion:
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
{
    MPIDI_CH3I_progress_blocked = FALSE;
    MPIDI_CH3I_Progress_continue(MPIDI_CH3I_progress_completion_count);
}
#endif

fn_exit:
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
    int mpi_errno;
    int completion_count = MPIDI_CH3I_progress_completion_count;
    vbuf *buffer = NULL;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PROGRESS);
    MPIDI_STATE_DECL(MPID_STATE_MPIDU_YIELD);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PROGRESS);

#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    {
    if (MPIDI_CH3I_progress_blocked == TRUE) {
        goto fn_exit;
    }
    }
#endif

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

        /* check if we made any progress */
        if(completion_count != MPIDI_CH3I_progress_completion_count) {
            goto fn_exit;
        }

        MPIDI_CH3I_SMP_read_progress(MPIDI_Process.my_pg);
    }

    if (!SMP_ONLY) {
#endif
        mpi_errno = MPIDI_CH3I_read_progress(&vc_ptr, &buffer);
        if (mpi_errno != MPI_SUCCESS) {
            mpi_errno =
                MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                        __LINE__, MPI_ERR_OTHER,
                        "**ch3progress", 0);
            MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
            return mpi_errno;
        }

        if(vc_ptr != NULL) {
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

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PROGRESS);
    DEBUG_PRINT("Exiting ch3 progress test\n");
    return MPI_SUCCESS;
}


#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_delay
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Progress_delay(unsigned int completion_count)
{
    int mpi_errno = MPI_SUCCESS;
    while (completion_count == MPIDI_CH3I_progress_completion_count
            && MPIDI_CH3I_progress_blocked == TRUE)
    {
        MPID_Thread_cond_wait(&MPIDI_CH3I_progress_completion_cond, 
                              &MPIR_Process.global_mutex);
    }
     DEBUG_PRINT("Exiting ch3 progress delay\n");
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_continue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Progress_continue(unsigned int completion_count)
{
    MPID_Thread_cond_broadcast(&MPIDI_CH3I_progress_completion_cond);
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
    MPID_Thread_cond_create(
            &MPIDI_CH3I_progress_completion_cond, NULL);
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

static inline int post_pkt_recv(MPIDI_VC_t * vc)
{
    int mpi_errno;
    MPIDI_STATE_DECL(MPID_STATE_POST_PKT_RECV);
    MPIDI_FUNC_ENTER(MPID_STATE_POST_PKT_RECV);
    vc->ch.req->dev.iov[0].MPID_IOV_BUF = (void *) &vc->ch.req->ch.pkt;
    vc->ch.req->dev.iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
    vc->ch.req->dev.iov_count = 1;
    vc->ch.req->ch.iov_offset = 0;
    vc->ch.req->dev.ca = MPIDI_CH3I_CA_HANDLE_PKT;
    vc->ch.recv_active = vc->ch.req;
    mpi_errno =
        MPIDI_CH3I_post_read(vc, &vc->ch.req->ch.pkt,
                             sizeof(vc->ch.req->ch.pkt));
    if (mpi_errno != MPI_SUCCESS)
        mpi_errno =
            MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**postpkt", 0);
    MPIDI_FUNC_EXIT(MPID_STATE_POST_PKT_RECV);
    return mpi_errno;
}

/*#define post_pkt_recv(vc) MPIDI_CH3I_post_read( vc , &(vc)->ch.pkt, sizeof((vc)->ch.pkt))*/

#undef FUNCNAME
#define FUNCNAME handle_read
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int handle_read(MPIDI_VC_t * vc, vbuf * buffer)
{
    int mpi_errno;
    MPID_Request *req;
    int complete;
    int header_size;
    int nb;
    MPIDI_CH3_Pkt_send_t *header;
    int packetized_recv = 0;
    int finished;
    
    DEBUG_PRINT("[handle read] buffer %p\n", buffer);

    vc->ch.recv_active = vc->ch.req;
    req = vc->ch.recv_active;
    /* Step one, ask lower level to provide header */
    /*  save header at req->ch.pkt, and return the header size */
    /*  ??TODO: Possibly just return the address of the header */
    MPIDI_CH3I_MRAIL_Parse_header(vc, buffer, (void **)&header, &header_size);

    DEBUG_PRINT("[handle read] header type %d\n", header->type);

    switch(header->type) {
    case MPIDI_CH3_PKT_NOOP: 
        DEBUG_PRINT("NOOP received, don't need to proceed\n");
        MPIDI_CH3I_MRAIL_Release_vbuf(buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
        DEBUG_PRINT("Packetized data received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Packetized_recv_data(vc, buffer);
        MPIDI_CH3I_MRAIL_Release_vbuf(buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RNDV_R3_DATA:
        DEBUG_PRINT("R3 data received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Rendezvouz_r3_recv_data(vc, buffer);
        MPIDI_CH3I_MRAIL_Release_vbuf(buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_RPUT_FINISH:
        DEBUG_PRINT("RPUT finish received, don't need to proceed\n");
        mpi_errno = MPIDI_CH3_Rendezvous_rput_finish(vc, (void *)header);
        MPIDI_CH3I_MRAIL_Release_vbuf(buffer);
        goto fn_exit;
    case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
        packetized_recv = 1;
        header_size +=
            ((MPIDI_CH3_Pkt_packetized_send_start_t *)(header))->origin_head_size;
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

        DEBUG_PRINT("data contained in buffer is %d bytes\n", buffer->head_flag);
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
    MPIDI_CH3I_MRAIL_Release_vbuf(buffer);
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


