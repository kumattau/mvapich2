/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
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

#if !defined(MPICH_MPIDI_CH3_POST_H_INCLUDED)
#define MPICH_MPIDI_CH3_POST_H_INCLUDED

/* #define MPIDI_CH3_EAGER_MAX_MSG_SIZE (1500 - sizeof(MPIDI_CH3_Pkt_t)) */
/* #define MPIDI_CH3_EAGER_MAX_MSG_SIZE rdma_iba_eager_threshold */
extern int smp_eagersize;

static inline int MPIDI_CH3_EAGER_MAX_MSG_SIZE(MPIDI_VC_t *vc)
{
#ifdef _SMP_
    return (vc->smp.local_nodes >=0) ? smp_eagersize : rdma_iba_eager_threshold;
#else
    return rdma_iba_eager_threshold;
#endif
}

/*
 * Channel level request management macros
 */
#define MPIDI_CH3_Request_add_ref(req)				\
{								\
    MPIU_Assert(HANDLE_GET_MPI_KIND(req->handle) == MPID_REQUEST);	\
    MPIU_Object_add_ref(req);					\
}

#define MPIDI_CH3_Request_release_ref(req, req_ref_count)	\
{								\
    MPIU_Assert(HANDLE_GET_MPI_KIND(req->handle) == MPID_REQUEST);	\
    MPIU_Object_release_ref(req, req_ref_count);		\
    MPIU_Assert(req->ref_count >= 0);				\
}

/*
 * MPIDI_CH3_Progress_signal_completion() is used to notify the progress
 * engine that a completion has occurred.  The multi-threaded version will need
 * to wake up any (and all) threads blocking in MPIDI_CH3_Progress().
 */
extern volatile unsigned int MPIDI_CH3I_progress_completion_count;
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
    extern volatile int MPIDI_CH3I_progress_blocked;
    extern volatile int MPIDI_CH3I_progress_wakeup_signalled;

    void MPIDI_CH3I_Progress_wakeup(void);
#endif

#if (MPICH_THREAD_LEVEL != MPI_THREAD_MULTIPLE)
#   define MPIDI_CH3_Progress_signal_completion()	\
    {							\
        MPIDI_CH3I_progress_completion_count++;		\
    }
#else
#   define MPIDI_CH3_Progress_signal_completion()							\
    {													\
	MPIDI_CH3I_progress_completion_count++;								\
	if (MPIDI_CH3I_progress_blocked == TRUE && MPIDI_CH3I_progress_wakeup_signalled == FALSE)	\
	{												\
	    MPIDI_CH3I_progress_wakeup_signalled = TRUE;						\
	    MPIDI_CH3I_Progress_wakeup();								\
	}												\
    }
#endif

/*
 * CH3 Progress routines (implemented as macros for performanace)
 */

#if defined(MPICH_SINGLE_THREADED)
#define MPIDI_CH3_Progress_start(state)
#define MPIDI_CH3_Progress_end(state)
#else
#define MPIDI_CH3_Progress_start(progress_state_)					\
{											\
    (progress_state_)->ch.completion_count = MPIDI_CH3I_progress_completion_count;	\
}
#endif
#define MPIDI_CH3_Progress_poke() MPIDI_CH3_Progress_test()

#define MPIDI_CH3_Progress_test() (MPIDI_CH3I_Progress_test());

int MPIDI_CH3I_Progress(int blocking, MPID_Progress_state *state);
int MPIDI_CH3I_Progress_test(void);

#define MPIDI_CH3_Progress_wait(state) MPIDI_CH3I_Progress(TRUE, state)

#include "mpidi_ch3_rdma_post.h"

/*
 * Enable optional functionality
 */
#define MPIDI_CH3_Comm_Spawn MPIDI_CH3_Comm_Spawn

/* Macros for OSU-MPI2 */
#define MPIDI_CH3_RNDV_SET_REQ_INFO(rreq, rts_pkt) \
{       \
    rreq->mrail.protocol = rts_pkt->rndv.protocol;  \
}

#define MPIDI_CH3_RNDV_PROTOCOL_IS_READ(rts_pkt) \
    (VAPI_PROTOCOL_RGET == rts_pkt->rndv.protocol)

#define MPIDI_CH3_RECV_REQ_IS_READ(rreq) \
    (VAPI_PROTOCOL_RGET == rreq->mrail.protocol)

extern int SMP_INIT;
extern int SMP_ONLY;

/* End of OSU-MPI2 */

#endif /* !defined(MPICH_MPIDI_CH3_POST_H_INCLUDED) */
