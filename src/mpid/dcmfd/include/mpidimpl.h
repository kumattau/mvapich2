/*  (C)Copyright IBM Corp.  2007, 2008  */
/**
 * \file include/mpidimpl.h
 * \brief DCMF API MPID additions to MPI functions and structures
 */

/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPICH_DCMF_MPIDIMPL_H_INCLUDED
#define MPICH_DCMF_MPIDIMPL_H_INCLUDED

int SSM_ABORT();

/* ****************************************************************
 * Asserts are divided into three levels:
 * 1. abort  - Always active and always issues assert(0).
 *             Primarily used for unimplemented code paths.
 * 2. assert - Active by default, or when MPID_ASSERT_PROD is defined.
 *             Meant to flag user errors.
 * 3. assert_debug - Active by default.  Meant to flag coding
 *                   errors before shipping.
 * Only one of MPID_ASSERT_ABORT, MPID_ASSERT_PROD (or nothing) should
 * be specified.
 * - MPID_ASSERT_ABORT means that the "abort" level is the only level
 *   of asserts that is active.  Other levels are turned off.
 * - MPID_ASSERT_PROD means that "abort" and "assert" levels are active.
 *   "assert_debug" is turned off.
 * - Not specifying MPID_ASSERT_ABORT or MPID_ASSERT_PROD means that all
 *   levels of asserts ("abort", "assert", "assert_debug") are
 *   active.
 * ****************************************************************
 */
#include <mpid_config.h>
#include <assert.h>

#if ASSERT_LEVEL==0
#define MPID_abort()         assert(0)
#define MPID_assert(x)
#define MPID_assert_debug(x)
#elif ASSERT_LEVEL==1
#define MPID_abort()         assert(0)
#define MPID_assert(x)       assert(x)
#define MPID_assert_debug(x)
#else /* ASSERT_LEVEL==2 */
/** \brief Always exit--usually implies missing functionality */
#define MPID_abort()         assert(0)
/** \brief Tests for likely problems--may not be active in performance code  */
#define MPID_assert(x)       assert(x)
/** \brief Tests for rare problems--may not be active in production code */
#define MPID_assert_debug(x) assert(x)
#endif

#include "mpiimpl.h"
#include "mpidpre.h"
#include "mpidpost.h"

/**
 * \brief MPI Process descriptor
 *
 * This structure contains global configuration flags.
 */

typedef struct
{
  struct
  {
    unsigned topology;           /**< Enable optimized topology functions.   */
    unsigned collectives;        /**< Enable optimized collective functions. */
  }
  optimized;
  unsigned eager_limit;
  unsigned optrzv_limit;
  unsigned verbose        :  8;  /**< The current level of verbosity for end-of-job stats. */
  unsigned statistics     :  8;  /**< The current level of stats collection.               */
  unsigned use_interrupts :  1;  /**< Should interrupts be turned on.                      */
  unsigned use_ssm        :  1;  /**< Enable Sender-Side-Matching of messages.             */
  unsigned rma_pending    :  1;
/*unsigned unused_flags   : 13; */
}      MPIDI_Process_t;
extern MPIDI_Process_t MPIDI_Process;

typedef struct
{
  DCMF_Protocol_t send;
  DCMF_Protocol_t mrzv;
  DCMF_Protocol_t rzv;
  DCMF_Protocol_t get;
  DCMF_Protocol_t ssm_put;
  DCMF_Protocol_t ssm_cts;
  DCMF_Protocol_t ssm_ack;
  DCMF_Protocol_t protocol;
  DCMF_Protocol_t control;
  DCMF_Protocol_t globalbarrier;
  DCMF_Protocol_t globalbcast;
  DCMF_Protocol_t globalallreduce;
}      MPIDI_Protocol_t;
extern MPIDI_Protocol_t MPIDI_Protocols;

typedef struct
{
   unsigned char numcolors; /* number of colors for bcast/allreduce */
   /* Optimized barrier protocols and usage flags */
   struct
   {
      DCMF_CollectiveProtocol_t gi;
      unsigned char usegi;
      DCMF_CollectiveProtocol_t binomial;
      unsigned char usebinom;
   } barrier;
   unsigned char optbarrier; /* do we have an optimized barrier? */

   /* Optimized local barrier protocols and usage flags  (not used directly by MPICH
    * but stored in the geometry) */
   struct
   {
      DCMF_CollectiveProtocol_t lockbox;
      unsigned char uselockbox;
      DCMF_CollectiveProtocol_t binomial;
      unsigned char usebinom;
   } localbarrier;

   /* Optimized broadcast protocols and usage flags */
   struct
   {
      DCMF_CollectiveProtocol_t tree;
      unsigned char usetree;
      DCMF_CollectiveProtocol_t rectangle;
      DCMF_CollectiveProtocol_t async_rectangle;
      unsigned char userect;
      DCMF_CollectiveProtocol_t binomial;
      DCMF_CollectiveProtocol_t async_binomial;
      unsigned char usebinom;
      unsigned char useasyncbinom;
      unsigned char useasyncrect;
   } broadcast;
   unsigned char optbroadcast;

   /* Optimized alltoallv protocol and usage flag */
   struct
   {
      DCMF_CollectiveProtocol_t torus;
      unsigned char usetorus;
   } alltoallv;

   /* For consistancy, optimized alltoall flag. Uses alltoallv protocol */
   struct
   {
      unsigned char usetorus;
      unsigned char premalloc;
   } alltoall;

   struct
   {
      unsigned char usetorus;
   } alltoallw;


   /* Optimized allgather usage flag */
   struct
   {
      unsigned char useallreduce;
      unsigned char usebcast;
      unsigned char usealltoallv;
      unsigned char useasyncbcast;
   } allgather;
   unsigned char optallgather;

   /* Optimized allgatherv usage flag */
   struct
   {
      unsigned char useallreduce;
      unsigned char usebcast;
      unsigned char usealltoallv;
      unsigned char useasyncbcast;
   } allgatherv;
   unsigned char optallgatherv;

   /* Optimized scatter usage flag */
   struct
   {
      unsigned char usebcast;
   } scatter;
   unsigned char optscatter;

   /* Optimized scatterv usage flag */
   struct
   {
      unsigned char usealltoallv;
   } scatterv;
   unsigned char optscatterv;

   /* Optimized reduce_scatter usage flag */
   struct
   {
      unsigned char usereducescatter;
   } reduce_scatter;
   unsigned char optreducescatter;

   struct
   {
      unsigned char usereduce;
   } gather;
   unsigned char optgather;


   /* Optimized allreduce protocols and usage flags */
   struct
   {
      unsigned char reusestorage;
      unsigned char usetree;
      unsigned char useccmitree;
      unsigned char usepipelinedtree;
      unsigned char userect;
      unsigned char userectring;
      unsigned char usebinom;
      DCMF_CollectiveProtocol_t tree __attribute__((__aligned__(16)));
      DCMF_CollectiveProtocol_t pipelinedtree;
      DCMF_CollectiveProtocol_t pipelinedtree_dput;
      DCMF_CollectiveProtocol_t rectangle;
      DCMF_CollectiveProtocol_t rectanglering;
      DCMF_CollectiveProtocol_t binomial;
   } allreduce;
   unsigned char optallreduce;

   /* Optimized reduce protocols and usage flags */
   struct
   {
      unsigned char reusestorage;
      DCMF_CollectiveProtocol_t tree;
      unsigned char usetree;
      unsigned char useccmitree;
      DCMF_CollectiveProtocol_t rectangle;
      unsigned char userect;
      DCMF_CollectiveProtocol_t rectanglering;
      unsigned char userectring;
      DCMF_CollectiveProtocol_t binomial;
      unsigned char usebinom;
   } reduce;
   unsigned char optreduce;

   unsigned numrequests;

}      MPIDI_CollectiveProtocol_t;
extern MPIDI_CollectiveProtocol_t MPIDI_CollectiveProtocols;

extern DCMF_Hardware_t mpid_hw;

/**
 * *************************************************************************
 * Low-level request utilities: allocation, release of
 * requests, with a holding pen for just-released requests. This is
 * code stolen from MPICH, now in mpidi_dcmfts_request.c
 * *************************************************************************
 */

/**
 * *************************************************************************
 * Request queue related utilities (stolen from CH3; in src/impl)
 * *************************************************************************
 */

/**
 * \addtogroup MPID_RECVQ
 * \{
 */
void MPIDI_Recvq_init();
void MPIDI_Recvq_finalize();
MPID_Request * MPIDI_Recvq_FU        (int s, int t, int c);
MPID_Request * MPIDI_Recvq_FDURSTC   (MPID_Request * req, int source, int tag, int context_id);
MPID_Request * MPIDI_Recvq_FDUR      (MPID_Request * req);
MPID_Request * MPIDI_Recvq_FDU_or_AEP(int s, int t, int c, int * foundp);
int            MPIDI_Recvq_FDPR      (MPID_Request * req);
MPID_Request * MPIDI_Recvq_FDP_or_AEU(int s, int t, int c, int * foundp);
void MPIDI_Recvq_DumpQueues(int verbose);
/**\}*/

void MPIDI_DCMF_Buffer_copy(const void     * const sbuf,
                            int                    scount,
                            MPI_Datatype           sdt,
                            int            *       smpi_errno,
                            void           * const rbuf,
                            int                    rcount,
                            MPI_Datatype           rdt,
                            MPIDI_msg_sz_t *       rsz,
                            int            *       rmpi_errno);

/**
 * \addtogroup MPID_PROGRESS
 * \{
 */
void MPID_Progress_start (MPID_Progress_state * state);
void MPID_Progress_end   (MPID_Progress_state * state);
int  MPID_Progress_wait  (MPID_Progress_state * state);
int  MPID_Progress_poke  ();
int  MPID_Progress_test  ();
void MPID_Progress_signal();
/**
 * \brief A macro to easily implement advancing until a specific
 * condition becomes false.
 *
 * \param COND This is not a true parameter.  It is *specifically*
 * designed to be evaluated several times, allowing for the result to
 * change.  The condition would generally look something like
 * "(cb.client == 0)".  This would be used as the condition on a while
 * loop.
 *
 * \returns MPI_SUCCESS
 *
 * This correctly checks the condition before attempting to loop,
 * since the call to MPID_Progress_wait() may not return if the event
 * is already complete.  Any ssytem *not* using this macro *must* use
 * a similar check before waiting.
 */
#define MPID_PROGRESS_WAIT_WHILE(COND)          \
({                                              \
   if (COND)                                    \
     {                                          \
       MPID_Progress_state dummy;               \
                                                \
       MPID_Progress_start(&dummy);             \
       while (COND)                             \
         MPID_Progress_wait(&dummy);            \
       MPID_Progress_end(&dummy);               \
     }                                          \
    MPI_SUCCESS;                                \
})
/**\}*/


/**
 * \brief Gets significant info regarding the datatype
 * Used in mpid_send, mpidi_send.  Stolen from CH3 channel implementation.
 */
#define MPIDI_Datatype_get_info(_count, _datatype,              \
_dt_contig_out, _data_sz_out, _dt_ptr, _dt_true_lb)             \
{                                                               \
  if (HANDLE_GET_KIND(_datatype) == HANDLE_KIND_BUILTIN)        \
    {                                                           \
        (_dt_ptr) = NULL;                                       \
        (_dt_contig_out) = TRUE;                                \
        (_dt_true_lb)  = 0;                                     \
        (_data_sz_out) = (_count) *                             \
        MPID_Datatype_get_basic_size(_datatype);                \
    }                                                           \
  else                                                          \
    {                                                           \
        MPID_Datatype_get_ptr((_datatype), (_dt_ptr));          \
        (_dt_contig_out) = (_dt_ptr)->is_contig;                \
        (_dt_true_lb) = (_dt_ptr)->true_lb;                     \
        (_data_sz_out) = (_count) * (_dt_ptr)->size;            \
    }                                                           \
}

/**
 * \addtogroup MPID_REQUEST
 * \{
 */

MPID_Request * MPID_Request_create        ();
MPID_Request * MPID_SendRequest_create    ();
void           MPID_Request_destroy       (MPID_Request *req);
void           MPID_Request_release       (MPID_Request *req);

/* completion count */
void           MPID_Request_complete      (MPID_Request *req);
void           MPID_Request_set_completed (MPID_Request *req);

#define MPID_Request_decrement_cc(_req, _inuse) { *(_inuse) = --(*(_req)->cc_ptr)  ; }
#define MPID_Request_increment_cc(_req)         {               (*(_req)->cc_ptr)++; }

#define MPID_Request_add_ref(_req)                                      \
{                                                                       \
  MPID_assert(HANDLE_GET_MPI_KIND((_req)->handle) == MPID_REQUEST);     \
  MPIU_Object_add_ref(_req);                                            \
}

#define MPID_Request_setCA(_req, _ca)        { (_req)->dcmf.ca                     = (_ca);                   }
#define MPID_Request_setPeerRank(_req,_r)    { (_req)->dcmf.peerrank               = (_r);                    }
#define MPID_Request_setPeerRequest(_req,_r) { (_req)->dcmf.envelope.envelope.msginfo.msginfo.req    = (_r);  }
#define MPID_Request_setType(_req,_t)        { (_req)->dcmf.envelope.envelope.msginfo.msginfo.type   = (_t);  }
#define MPID_Request_setSelf(_req,_t)        { (_req)->dcmf.envelope.envelope.msginfo.msginfo.isSelf = (_t);  }
#define MPID_Request_setSync(_req,_t)        { (_req)->dcmf.envelope.envelope.msginfo.msginfo.isSync = (_t);  }
#define MPID_Request_setRzv(_req,_t)         { (_req)->dcmf.envelope.envelope.msginfo.msginfo.isRzv  = (_t);  }
#define MPID_Request_setMatch(_req,_tag,_rank,_ctxtid)                  \
{                                                                       \
  (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPItag=(_tag);         \
  (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPIrank=(_rank);       \
  (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPIctxt=(_ctxtid);     \
}

#define MPID_Request_getCA(_req)          ( (_req)->dcmf.ca                                        )
#define MPID_Request_getType(_req)        ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.type    )
#define MPID_Request_isSelf(_req)         ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.isSelf  )
#define MPID_Request_isSync(_req)         ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.isSync  )
#define MPID_Request_isRzv(_req)          ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.isRzv   )
#define MPID_Request_getMatchTag(_req)    ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPItag  )
#define MPID_Request_getMatchRank(_req)   ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPIrank )
#define MPID_Request_getMatchCtxt(_req)   ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.MPIctxt )
#define MPID_Request_getPeerRequest(_req) ( (_req)->dcmf.envelope.envelope.msginfo.msginfo.req     )
#define MPID_Request_getPeerRank(_req)    ( (_req)->dcmf.peerrank                                  )
/**\}*/


/**
 * \defgroup MPID_CALLBACKS MPID callbacks for DCMF communication
 *
 * These calls are used to manage message asynchronous start and completion
 */
/**
 * \addtogroup MPID_CALLBACKS
 * \{
 */
DCMF_Request_t * MPIDI_BG2S_RecvCB(void                     * clientdata,
                                   const MPIDI_DCMF_MsgInfo * msginfo,
                                   unsigned                   count,
                                   unsigned                   senderrank,
                                   const unsigned             sndlen,
                                   unsigned                 * rcvlen,
                                   char                    ** rcvbuf,
                                   DCMF_Callback_t    * const cb_info);
void MPIDI_BG2S_RecvShortCB(void                     * clientdata,
                            const MPIDI_DCMF_MsgInfo * msginfo,
                            unsigned                   count,
                            unsigned                   senderrank,
                            const char               * sndbuf,
                            unsigned                   sndlen);
void MPIDI_BG2S_RecvRzvCB(void                         * clientdata,
                          const MPIDI_DCMF_MsgEnvelope * rzv_envelope,
                          unsigned                       count,
                          unsigned                       senderrank,
                          const char                   * sndbuf,
                          unsigned                       sndlen);
void MPIDI_BG2S_SsmCtsCB(void                     * clientdata,
                         const MPIDI_DCMF_MsgInfo * msginfo,
                         unsigned                   count,
                         unsigned                   senderrank,
                         const char               * sndbuf,
                         unsigned                   sndlen);
void MPIDI_BG2S_SsmAckCB(void                     * clientdata,
                         const MPIDI_DCMF_MsgInfo * msginfo,
                         unsigned                   count,
                         unsigned                   senderrank,
                         const char               * sndbuf,
                         unsigned                   sndlen);
void MPIDI_DCMF_SendDoneCB    (MPID_Request * sreq);
void MPIDI_DCMF_RecvDoneCB    (MPID_Request * rreq);
void MPIDI_DCMF_RecvRzvDoneCB (MPID_Request * rreq);
void MPIDI_DCMF_StartMsg      (MPID_Request * sreq);
int  MPIDI_Irecv(void          * buf,
                 int             count,
                 MPI_Datatype    datatype,
                 int             rank,
                 int             tag,
                 MPID_Comm     * comm,
                 int             context_offset,
                 MPI_Status    * status,
                 MPID_Request ** request,
                 char          * func);
/** \} */


/** \brief Acknowledge an MPI_Ssend() */
int  MPIDI_DCMF_postSyncAck  (MPID_Request * req);
/** \brief Cancel an MPI_Send(). */
int  MPIDI_DCMF_postCancelReq(MPID_Request * req);
void MPIDI_DCMF_procCancelReq(const MPIDI_DCMF_MsgInfo *info, unsigned peer);
/** \brief This is the general PT2PT control message call-back */
void MPIDI_BG2S_ControlCB    (void * clientdata, const DCMF_Control_t * p, unsigned peer);
/**
 * \brief Mark a request as cancel-pending
 * \param[in]  _req  The request to cancel
 * \param[out] _flag The previous state
 */
#define MPIDI_DCMF_Request_cancel_pending(_req, _flag)  \
{                                                       \
  *(_flag) = (_req)->dcmf.cancel_pending;               \
  (_req)->dcmf.cancel_pending = TRUE;                   \
}


/** \brief Helper function when sending to self  */
int MPIDI_Isend_self(const void    * buf,
                     int             count,
                     MPI_Datatype    datatype,
                     int             rank,
                     int             tag,
                     MPID_Comm     * comm,
                     int             context_offset,
                     int             type,
                     MPID_Request ** request);

/** \brief Helper function to complete a rendevous transfer */
void MPIDI_DCMF_RendezvousTransfer (MPID_Request * rreq);

void MPID_Dump_stacks        ();


void MPIDI_Comm_create       (MPID_Comm *comm);
void MPIDI_Comm_destroy      (MPID_Comm *comm);
void MPIDI_Env_setup         ();

void MPIDI_Topo_Comm_create  (MPID_Comm *comm);
void MPIDI_Topo_Comm_destroy (MPID_Comm *comm);
int  MPID_Dims_create        (int nnodes, int ndims, int *dims);

void MPIDI_Coll_Comm_create  (MPID_Comm *comm);
void MPIDI_Coll_Comm_destroy (MPID_Comm *comm);
void MPIDI_Coll_register     (void);

#endif
