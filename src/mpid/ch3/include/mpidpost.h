/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#if !defined(MPICH_MPIDPOST_H_INCLUDED)
#define MPICH_MPIDPOST_H_INCLUDED

/* FIXME: mpidpost.h is included by mpiimpl.h .  However, mpiimpl.h should 
   refer only to the ADI3 prototypes and should never include prototypes 
   specific to any particular device.  Factor the include files to maintain
   better modularity by providing mpiimpl.h with only the definitions that it
   needs */
/*
 * Channel API prototypes
 */

/* FIXME: *E is the "enum" doctext structure comment marker; these should be
 *@ instead.  Also, these may be out of date; not all of these are referenced
 * (They should all be used in the ch3/src directory; otherwise they're not
 * part of the channel API). 
 */

/*E
  MPIDI_CH3_Init - Initialize the channel implementation.

  Input Parameters:
+ has_parent - boolean value that is true if this MPI job was spawned by another set of MPI processes
. pg_ptr - the new process group representing MPI_COMM_WORLD
- pg_rank - my rank in the process group

  Return value:
  A MPI error code.

  Notes:
  MPID_Init has called 'PMI_Init' and created the process group structure 
  before this routine is called.
E*/
int MPIDI_CH3_Init(int has_parent, MPIDI_PG_t *pg_ptr, int pg_rank );

/*E
  MPIDI_CH3_Finalize - Shutdown the channel implementation.

  Return value:
  A MPI error class.
E*/
int MPIDI_CH3_Finalize(void);


/*E
  MPIDI_CH3_Get_parent_port - obtain the port name associated with the parent

  Output Parameters:
.  parent_port_name - the port name associated with the parent communicator

  Return value:
  A MPI error code.
  
  NOTE:
  'MPIDI_CH3_Get_parent_port' should only be called if the initialization
  (in the current implementation, done with the static function 
  'InitPGFromPMI' in 'mpid_init.c') has determined that this process
  in fact has a parent.
E*/
int MPIDI_CH3_Get_parent_port(char ** parent_port_name);

/*E
  MPIDI_CH3_iStartMsg - A non-blocking request to send a CH3 packet.  A request object is allocated only if the send could not be
  completed immediately.

  Input Parameters:
+ vc - virtual connection to send the message over
. pkt - pointer to a MPIDI_CH3_Pkt_t structure containing the substructure to be sent
- pkt_sz - size of the packet substucture

  Output Parameters:
. sreq_ptr - send request or NULL if the send completed immediately

  Return value:
  An mpi error code.
  
  NOTE:
  The packet structure may be allocated on the stack.

  IMPLEMETORS:
  If the send can not be completed immediately, the CH3 packet structure must be stored internally until the request is complete.
  
  If the send completes immediately, the channel implementation shold return NULL and must not call MPIDI_CH3U_Handle_send_req().
E*/
int MPIDI_CH3_iStartMsg(MPIDI_VC_t * vc, void * pkt, MPIDI_msg_sz_t pkt_sz, MPID_Request **sreq_ptr);


/*E
  MPIDI_CH3_iStartMsgv - A non-blocking request to send a CH3 packet and associated data.  A request object is allocated only if
  the send could not be completed immediately.

  Input Parameters:
+ vc - virtual connection to send the message over
. iov - a vector of a structure contains a buffer pointer and length
- iov_n - number of elements in the vector

  Output Parameters:
. sreq_ptr - send request or NULL if the send completed immediately

  Return value:
  An mpi error code.
  
  NOTE:
  The first element in the vector must point to the packet structure.   The packet structure and the vector may be allocated on
  the stack.

  IMPLEMENTORS:
  If the send can not be completed immediately, the CH3 packet structure and the vector must be stored internally until the
  request is complete.
  
  If the send completes immediately, the channel implementation shold return NULL and must not call MPIDI_CH3U_Handle_send_req().
E*/
int MPIDI_CH3_iStartMsgv(MPIDI_VC_t * vc, MPID_IOV * iov, int iov_n, MPID_Request **sreq_ptr);


/*E
  MPIDI_CH3_iSend - A non-blocking request to send a CH3 packet using an existing request object.  When the send is complete
  the channel implementation will call MPIDI_CH3U_Handle_send_req().

  Input Parameters:
+ vc - virtual connection over which to send the CH3 packet
. sreq - pointer to the send request object
. pkt - pointer to a MPIDI_CH3_Pkt_t structure containing the substructure to be sent
- pkt_sz - size of the packet substucture

  Return value:
  An mpi error code.
  
  NOTE:
  The packet structure may be allocated on the stack.

  IMPLEMETORS:
  If the send can not be completed immediately, the packet structure must be stored internally until the request is complete.

  If the send completes immediately, the channel implementation still must call MPIDI_CH3U_Handle_send_req().
E*/
int MPIDI_CH3_iSend(MPIDI_VC_t * vc, MPID_Request * sreq, void * pkt, MPIDI_msg_sz_t pkt_sz);


/*E
  MPIDI_CH3_iSendv - A non-blocking request to send a CH3 packet and associated data using an existing request object.  When
  the send is complete the channel implementation will call MPIDI_CH3U_Handle_send_req().

  Input Parameters:
+ vc - virtual connection over which to send the CH3 packet and data
. sreq - pointer to the send request object
. iov - a vector of a structure contains a buffer pointer and length
- iov_n - number of elements in the vector

  Return value:
  An mpi error code.
  
  NOTE:
  The first element in the vector must point to the packet structure.   The packet structure and the vector may be allocated on
  the stack.

  IMPLEMENTORS:
  If the send can not be completed immediately, the packet structure and the vector must be stored internally until the request is
  complete.

  If the send completes immediately, the channel implementation still must call MPIDI_CH3U_Handle_send_req().
E*/
int MPIDI_CH3_iSendv(MPIDI_VC_t * vc, MPID_Request * sreq, MPID_IOV * iov, int iov_n);


/*E
  MPIDI_CH3_Cancel_send - Attempt to cancel a send request by removing the request from the local send queue.

  Input Parameters:
+ vc - virtual connection over which to send the data 
- sreq - pointer to the send request object

  Output Parameters:
. cancelled - TRUE if the send request was successful.  FALSE otherwise.

  Return value:
  An mpi error code.
  
  IMPLEMENTORS:
  The send request may not be removed from the send queue if one or more bytes of the message have already been sent.
E*/
int MPIDI_CH3_Cancel_send(MPIDI_VC_t * vc, MPID_Request * sreq, int *cancelled);


/*E
  MPIDI_CH3_Request_create - Allocate and initialize a new request object.

  Return value:
  A pointer to an initialized request object, or NULL if an error occurred.
  
  IMPLEMENTORS:
  MPIDI_CH3_Request_create() must call MPIDI_CH3U_Request_create() after the request object has been allocated.
E*/
MPID_Request * MPIDI_CH3_Request_create(void);


/*E
  MPIDI_CH3_Request_add_ref - Increment the reference count associated with a request object

  Input Parameters:
. req - pointer to the request object
E*/
void MPIDI_CH3_Request_add_ref(MPID_Request * req);

/*E
  MPIDI_CH3_Request_release_ref - Decrement the reference count associated with a request object.

  Input Parameters:
. req - pointer to the request object

  Output Parameters:
. inuse - TRUE if the object is still inuse; FALSE otherwise.
E*/
void MPIDI_CH3_Request_release_ref(MPID_Request * req, int * inuse);


/*E
  MPIDI_CH3_Request_destroy - Release resources in use by an existing request object.

  Input Parameters:
. req - pointer to the request object

  IMPLEMENTORS:
  MPIDI_CH3_Request_destroy() must call MPIDI_CH3U_Request_destroy() before request object is freed.
E*/
void MPIDI_CH3_Request_destroy(MPID_Request * req);


/*E
  MPIDI_CH3_Progress_start - Mark the beginning of a progress epoch.

  Input Parameters:
. state - pointer to a MPID_Progress_state object

  Return value:
  An MPI error code.
  
  NOTE:
  This routine need only be called if the code might call MPIDI_CH3_Progress_wait().  It is normally used as follows example:
.vb
      if (*req->cc_ptr != 0)
      {
          MPID_Progress_state state;
          
          MPIDI_CH3_Progress_start(&state);
          {
              while(*req->cc_ptr != 0)
              {
                  MPIDI_CH3_Progress_wait(&state);
              }
          }
          MPIDI_CH3_Progress_end(&state);
      }
.ve

  IMPLEMENTORS:
  A multi-threaded implementation might save the current value of a request completion counter in the state.
E*/
void MPIDI_CH3_Progress_start(MPID_Progress_state * state);


/*E
  MPIDI_CH3_Progress_wait - Give the channel implementation an opportunity to make progress on outstanding communication requests.

  Input Parameters:
. state - pointer to the same MPID_Progress_state object passed to MPIDI_CH3_Progress_start

  Return value:
  An MPI error code.
  
  NOTE:
  MPIDI_CH3_Progress_start/end() need to be called.
  
  IMPLEMENTORS:
  A multi-threaded implementation would return immediately if the a request had been completed between the call to
  MPIDI_CH3_Progress_start() and MPIDI_CH3_Progress_wait().  This could be implemented by checking a request completion counter
  in the progress state against a global counter, and returning if they did not match.
E*/
int MPIDI_CH3_Progress_wait(MPID_Progress_state * state);


/*E
  MPIDI_CH3_Progress_end - Mark the end of a progress epoch.
  
  Input Parameters:
. state - pointer to the same MPID_Progress_state object passed to MPIDI_CH3_Progress_start

  Return value:
  An MPI error code.
E*/
void MPIDI_CH3_Progress_end(MPID_Progress_state * state);


/*E
  MPIDI_CH3_Progress_test - Give the channel implementation an opportunity to make progress on outstanding communication requests.

  Return value:
  An MPI error code.
  
  NOTE:
  This function implicitly marks the beginning and end of a progress epoch.
E*/
int MPIDI_CH3_Progress_test(void);


/*E
  MPIDI_CH3_Progress_poke - Give the channel implementation a moment of opportunity to make progress on outstanding communication.

  Return value:
  An mpi error code.
  
  IMPLEMENTORS:
  This routine is similar to MPIDI_CH3_Progress_test but may not be as thorough in its attempt to satisfy all outstanding
  communication.
E*/
int MPIDI_CH3_Progress_poke(void);


/*E
  MPIDI_CH3_Progress_signal_completion - Inform the progress engine that a pending request has completed.

  IMPLEMENTORS:
  In a single-threaded environment, this routine can be implemented by incrementing a request completion counter.  In a
  multi-threaded environment, the request completion counter must be atomically incremented, and any threaded blocking in the
  progress engine must be woken up when a request is completed.
E*/
void MPIDI_CH3_Progress_signal_completion(void);

int MPIDI_CH3_Open_port(char *port_name);

int MPIDI_GetTagFromPort( const char *, int * );

int MPIDI_CH3_Comm_spawn_multiple(int count, char ** commands, char *** argvs, int * maxprocs, MPID_Info ** info_ptrs, int root,
                                  MPID_Comm * comm_ptr, MPID_Comm ** intercomm, int * errcodes);

int MPIDI_CH3_Comm_accept(char * port_name, int root, MPID_Comm * comm_ptr, MPID_Comm ** newcomm); 

int MPIDI_CH3_Comm_connect(char * port_name, int root, MPID_Comm * comm_ptr, MPID_Comm ** newcomm);


/*E
  MPIDI_CH3_Connection_terminate - terminate the underlying connection associated with the specified VC

  Input Parameters:
. vc - virtual connection

  Return value:
  An MPI error code
E*/
int MPIDI_CH3_Connection_terminate(MPIDI_VC_t * vc);

/* MPIDI_CH3_Connect_to_root (really connect to peer) - channel routine
   for connecting to a process through a port, used in implementing
   MPID_Comm_connect and accept */
int MPIDI_CH3_Connect_to_root(const char *, MPIDI_VC_t **);


/*E
  MPIDI_CH3_Abort - Abort this process.

  Input Parameters:
+ exit_code - exit code to be returned by the process
- error_msg - error message to print

  Return value:
  This function should not return.
E*/
int MPIDI_CH3_Abort(int exit_code, char * error_msg);


/*
 * Channel upcall prototypes
 */

/*E
  MPIDI_CH3U_Handle_recv_pkt- Handle a freshly received CH3 packet.

  Input Parameters:
+ vc - virtual connection over which the packet was received
- pkt - pointer to the CH3 packet

  Output Parameter:
. rreqp - receive request defining data to be received; may be NULL

  NOTE:
  Multiple threads may not simultaneously call this routine with the same virtual connection.  This constraint eliminates the
  need to lock the VC and thus improves performance.  If simultaneous upcalls for a single VC are a possible, then the calling
  routine must serialize the calls (perhaps by locking the VC).  Special consideration may need to be given to packet ordering
  if the channel has made guarantees about ordering.
E*/
int MPIDI_CH3U_Handle_recv_pkt(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt, MPID_Request ** rreqp);

/*E
  MPIDI_CH3U_Handle_recv_req - Process a receive request for which all of the data has been received (and copied) into the
  buffers described by the request's IOV.

  Input Parameters:
+ vc - virtual connection over which the data was received
- rreq - pointer to the receive request object

  Output Parameter:
. complete - data transfer for the request has completed
E*/
int MPIDI_CH3U_Handle_recv_req(MPIDI_VC_t * vc, MPID_Request * rreq, int * complete);


/*E
  MPIDI_CH3U_Handle_send_req - Process a send request for which all of the data described the request's IOV has been completely
  buffered and/or sent.

  Input Parameters:
+ vc - virtual connection over which the data was sent
- sreq - pointer to the send request object

  Output Parameter:
. complete - data transfer for the request has completed
E*/
int MPIDI_CH3U_Handle_send_req(MPIDI_VC_t * vc, MPID_Request * sreq, int * complete);


/*E
  MPIDI_CH3U_Handle_connection - handle connection event

  Input Parameters:
+ vc - virtual connection
. event - connection event

  NOTE:
  At present this function is only used for connection termination
E*/
int MPIDI_CH3U_Handle_connection(MPIDI_VC_t * vc, MPIDI_VC_Event_t event);


/*E
  MPIDI_CH3U_Request_create - Initialize the channel device (ch3) component of a request.

  Input Parameters:
. req - pointer to the request object

  IMPLEMENTORS:
  This routine must be called by MPIDI_CH3_Request_create().
E*/
void MPIDI_CH3U_Request_create(MPID_Request * req);


/*E
  MPIDI_CH3U_Request_destroy - Free resources associated with the channel device (ch3) component of a request.

  Input Parameters:
. req - pointer to the request object

  IMPLEMENTORS:
  This routine must be called by MPIDI_CH3_Request_destroy().
E*/
void MPIDI_CH3U_Request_destroy(MPID_Request * req);


/* BEGIN EXPERIMENTAL BLOCK */

/* The following functions enable RDMA capabilities in the CH3 device.
 * These functions may change in future releases.
 * There usage is protected in the code by #ifdef MPIDI_CH3_CHANNEL_RNDV
 */

/*E
  MPIDI_CH3U_Handle_recv_rndv_pkt - This function is used by RDMA enabled channels to handle a rts packet.

  Input Parameters:
+ vc - virtual connection over which the packet was received
- pkt - pointer to the CH3 packet

  Output Parameters:
+ rreqp - request pointer
- foundp - found

  Return value:
  An mpi error code.

  Notes:
  This is the handler function to be called when the channel receives a rndv rts packet.
  After this function is called the channel is returned a request and a found flag.  The channel may set any channel
  specific fields in the request at this time.  Then the channel should call MPIDI_CH3U_Post_data_receive() and 
  MPIDI_CH3_iStartRndvTransfer() if the found flag is set.
E*/
int MPIDI_CH3U_Handle_recv_rndv_pkt(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_t * pkt, MPID_Request ** rreqp, int *foundp);

/*E
  MPIDI_CH3_iStartRndvMsg - This function is used to initiate a rendezvous
  send.

  NOTE: An "rts packet" is provided which must be passed to
  handle_recv_rndv_pkt on the remote side.  The first iov is also provided
  so the channel can register buffers, etc., if neccessary.

  Input Parameters:
+ vc - virtual connection over which the rendezvous will be performed
. sreq - pointer to the send request object
- rts_pkt - CH3 packet to be delivered to CH3 on remote side

  Return value:
  An mpi error code.

  IMPLEMENTORS:
E*/
int MPIDI_CH3_iStartRndvMsg (MPIDI_VC_t * vc, MPID_Request * sreq, MPIDI_CH3_Pkt_t * rts_pkt);

/*E
  MPIDI_CH3_iStartRndvTransfer - This function is used to indicate that a previous
  rendezvous rts has been matched and data transfer can commence.

  Input Parameters:
+ vc - virtual connection over which the rendezvous will be performed
- rreq - pointer to the receive request object

  Return value:
  An mpi error code.

  IMPLEMENTORS:
E*/
int MPIDI_CH3_iStartRndvTransfer (MPIDI_VC_t * vc, MPID_Request * rreq);

/* END EXPERIMENTAL BLOCK */


/*
 * Channel utility prototypes
 */
int MPIDI_CH3U_Recvq_FU(int, int, int, MPI_Status * );
MPID_Request * MPIDI_CH3U_Recvq_FDU(MPI_Request, MPIDI_Message_match *);
MPID_Request * MPIDI_CH3U_Recvq_FDU_or_AEP(int, int, int, int * found);
int MPIDI_CH3U_Recvq_DP(MPID_Request * rreq);
MPID_Request * MPIDI_CH3U_Recvq_FDP(MPIDI_Message_match * match);
MPID_Request * MPIDI_CH3U_Recvq_FDP_or_AEU(MPIDI_Message_match * match, int * found);

void MPIDI_CH3U_Request_complete(MPID_Request * req);
void MPIDI_CH3U_Request_increment_cc(MPID_Request * req, int * was_incomplete);
void MPIDI_CH3U_Request_decrement_cc(MPID_Request * req, int * incomplete);

int MPIDI_CH3U_Request_load_send_iov(MPID_Request * const sreq, MPID_IOV * const iov, int * const iov_n);
int MPIDI_CH3U_Request_load_recv_iov(MPID_Request * const rreq);
int MPIDI_CH3U_Request_unpack_uebuf(MPID_Request * rreq);
int MPIDI_CH3U_Request_unpack_srbuf(MPID_Request * rreq);
void MPIDI_CH3U_Buffer_copy(const void * const sbuf, int scount, MPI_Datatype sdt, int * smpi_errno,
			    void * const rbuf, int rcount, MPI_Datatype rdt, MPIDI_msg_sz_t * rdata_sz, int * rmpi_errno);
int MPIDI_CH3U_Post_data_receive(int found, MPID_Request ** rreqp);


/* FIXME: Move these prototypes into header files in the appropriate 
   util directories  */
/* added by brad.  upcalls for MPIDI_CH3_Init that contain code which could be executed by two or more channels */
int MPIDI_CH3U_Init_sock(int has_parent, MPIDI_PG_t * pg_p, int pg_rank,
                         char **publish_bc_p, char **bc_key_p, char **bc_val_p, int *val_max_sz_p);                         
int MPIDI_CH3U_Init_sshm(int has_parent, MPIDI_PG_t * pg_p, int pg_rank,
                         char **publish_bc_p, char **bc_key_p, char **bc_val_p, int *val_max_sz_p);

/* added by brad.  business card related global and functions */
#define MAX_HOST_DESCRIPTION_LEN 256
/* FIXME: keep the listener port in one file and provide a method to
   set/retrieve it as needed */
extern int MPIDI_CH3I_listener_port;
int MPIDI_CH3U_Get_business_card_sock(char **bc_val_p, int *val_max_sz_p);
int MPIDI_CH3U_Get_business_card_sshm(char **bc_val_p, int *val_max_sz_p);
int MPIDI_CH3I_Get_business_card(char *value, int length);

/* added by brad.  finalization related upcalls */
int MPIDI_CH3U_Finalize_sshm(void);



/* Include definitions from the channel which require items defined by this file (mpidimpl.h) or the file it includes
   (mpiimpl.h). */
#include "mpidi_ch3_post.h"

#include "mpid_datatype.h"

/*
 * Request utility macros (public - can be used in MPID macros)
 *
 * MT: The inc/dec of the completion counter must be atomic since the progress engine could be completing the request in one
 * thread and the application could be cancelling the request in another thread.
 */
#if (USE_THREAD_IMPL != MPICH_THREAD_IMPL_NOT_IMPLEMENTED)
/* SHMEM: In the case of a single-threaded shmem channel sharing requests between processes, a write barrier must be performed
   before decrementing the completion counter.  This insures that other fields in the req structure are updated before the
   completion is signalled.  How should that be incorporated into this code from the channel level? */
#define MPIDI_CH3U_Request_decrement_cc(req_, incomplete_)	\
{								\
    *(incomplete_) = --(*(req_)->cc_ptr);			\
}
#elif defined(USE_ATOMIC_UPDATES)
/* If locks are not used, a write barrier must be performed if *before* the completion counter reaches zero.  This insures that
   other fields in the req structure are updated before the completion is signalled. */
#define MPIDI_CH3U_Request_decrement_cc(req_, incomplete_)	\
{								\
    int new_cc__;						\
    								\
    MPID_Atomic_write_barrier();				\
    MPID_Atomic_decr_flag((req_)->cc_ptr, new_cc__);		\
    *(incomplete_) = new_cc__;					\
}
#else
#define MPIDI_CH3U_Request_decrement_cc(req_, incomplete_)	\
{								\
    MPID_Request_thread_lock(req_);				\
    {								\
	*(incomplete_) = --(*(req_)->cc_ptr);			\
    }								\
    MPID_Request_thread_unlock(req_);				\
}
#endif

#if (USE_THREAD_IMPL != MPICH_THREAD_IMPL_NOT_IMPLEMENTED)
#define MPIDI_CH3U_Request_increment_cc(req_, was_incomplete_)	\
{								\
    *(was_incomplete_) = (*(req_)->cc_ptr)++;			\
}
#elif defined(USE_ATOMIC_UPDATES)
#define MPIDI_CH3U_Request_increment_cc(req_, was_incomplete_)	\
{								\
    int old_cc__;						\
								\
    MPID_Atomic_fetch_and_incr((req_)->cc_ptr, old_cc__);	\
    *(was_incomplete_) = old_cc__;				\
}
#else
#define MPIDI_CH3U_Request_increment_cc(req_, was_incomplete_)	\
{								\
    MPID_Request_thread_lock(req_);				\
    {								\
	*(was_incomplete_) = (*(req_)->cc_ptr)++;		\
    }								\
    MPID_Request_thread_unlock(req_);				\
}
#endif

/*
 * Device level request management macros
 */
#define MPID_Request_create() (MPIDI_CH3_Request_create())

#define MPID_Request_add_ref(req_) MPIDI_CH3_Request_add_ref(req_)

#define MPID_Request_release(req_)			\
{							\
    int ref_count;					\
							\
    MPIDI_CH3_Request_release_ref((req_), &ref_count);	\
    if (ref_count == 0)					\
    {							\
	MPIDI_CH3_Request_destroy(req_);		\
    }							\
}

#if (USE_THREAD_IMPL != MPICH_THREAD_IMPL_NOT_IMPLEMENTED)
#define MPID_Request_set_completed(req_)	\
{						\
    *(req_)->cc_ptr = 0;			\
    MPIDI_CH3_Progress_signal_completion();	\
}
#else
/* MT - If locks are not used, a write barrier must be performed before zeroing the completion counter.  This insures that other
   fields in the req structure are updated before the completion is signaled. */
#define MPID_Request_set_completed(req_)	\
{						\
    MPID_Request_thread_lock(req_);		\
    {						\
	*(req_)->cc_ptr = 0;			\
    }						\
    MPID_Request_thread_unlock(req_);		\
    						\
    MPIDI_CH3_Progress_signal_completion();	\
}
#endif


/*
 * Device level progress engine macros
 */
#define MPID_Progress_start(progress_state_) MPIDI_CH3_Progress_start(progress_state_)
#define MPID_Progress_wait(progress_state_)  MPIDI_CH3_Progress_wait(progress_state_)
#define MPID_Progress_end(progress_state_)   MPIDI_CH3_Progress_end(progress_state_)
#define MPID_Progress_test()                 MPIDI_CH3_Progress_test()
#define MPID_Progress_poke()		     MPIDI_CH3_Progress_poke()

/* Dynamic process support */
int MPID_GPID_GetAllInComm( MPID_Comm *comm_ptr, int local_size, 
			    int local_gpids[], int *singlePG );
int MPID_GPID_ToLpidArray( int size, int gpid[], int lpid[] );
int MPID_VCR_CommFromLpids( MPID_Comm *newcomm_ptr, 
			    int size, const int lpids[] );
int MPID_PG_ForwardPGInfo( MPID_Comm *peer_ptr, MPID_Comm *comm_ptr, 
			   int nPGids, int gpids[], 
			   int root );

#endif /* !defined(MPICH_MPIDPOST_H_INCLUDED) */
