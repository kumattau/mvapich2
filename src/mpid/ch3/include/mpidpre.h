/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#if !defined(MPICH_MPIDPRE_H_INCLUDED)
#define MPICH_MPIDPRE_H_INCLUDED

#include "mpidi_ch3_conf.h"

#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif

#if defined(HAVE_SYS_TYPES_H)
#include <sys/types.h>
#endif

#include "mpid_dataloop.h"
struct MPID_Datatype;

typedef MPI_Aint MPIDI_msg_sz_t;
#define MPIDI_MSG_SZ_FMT "%d"

/* Include definitions from the channel which must exist before items in this file (mpidimpl.h) or the file it includes
   (mpiimpl.h) can be defined. */
#include "mpidi_ch3_pre.h"

#if defined (MPIDI_CH3_MSGS_UNORDERED)
#define MPID_USE_SEQUENCE_NUMBERS
#endif

#if defined(MPID_USE_SEQUENCE_NUMBERS)
typedef unsigned long MPID_Seqnum_t;
#endif

#include "mpichconf.h"
typedef struct MPIDI_Message_match
{
    int32_t tag;
    int16_t rank;
    int16_t context_id;
}
MPIDI_Message_match;
#define MPIDI_TAG_UB (0x7fffffff)

/*
 * MPIDI_CH3_Pkt_type_t
 *
 */
typedef enum MPIDI_CH3_Pkt_type
{
    MPIDI_CH3_PKT_EAGER_SEND = 0,
    MPIDI_CH3_PKT_EAGER_SYNC_SEND,    /* FIXME: no sync eager */
    MPIDI_CH3_PKT_EAGER_SYNC_ACK,
    MPIDI_CH3_PKT_READY_SEND,
    MPIDI_CH3_PKT_RNDV_REQ_TO_SEND,
    MPIDI_CH3_PKT_RNDV_CLR_TO_SEND,
    MPIDI_CH3_PKT_RNDV_SEND,          /* FIXME: should be stream put */
    MPIDI_CH3_PKT_CANCEL_SEND_REQ,
    MPIDI_CH3_PKT_CANCEL_SEND_RESP,
    MPIDI_CH3_PKT_PUT,
    MPIDI_CH3_PKT_GET,
    MPIDI_CH3_PKT_GET_RESP,
    MPIDI_CH3_PKT_ACCUMULATE,
    MPIDI_CH3_PKT_LOCK,
    MPIDI_CH3_PKT_LOCK_GRANTED,
    MPIDI_CH3_PKT_PT_RMA_DONE,
    MPIDI_CH3_PKT_LOCK_PUT_UNLOCK, /* optimization for single puts */
    MPIDI_CH3_PKT_LOCK_GET_UNLOCK, /* optimization for single gets */
    MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK, /* optimization for single accumulates */
    MPIDI_CH3_PKT_FLOW_CNTL_UPDATE,  /* FIXME: Unused */
    MPIDI_CH3_PKT_CLOSE,
    MPIDI_CH3_PKT_END_CH3
# if defined(MPIDI_CH3_PKT_ENUM)
    , MPIDI_CH3_PKT_ENUM
# endif    
}
MPIDI_CH3_Pkt_type_t;

typedef struct MPIDI_CH3_Pkt_send
{
    MPIDI_CH3_Pkt_type_t type;  /* XXX - uint8_t to conserve space ??? */
    MPIDI_Message_match match;
    MPI_Request sender_req_id;	/* needed for ssend and send cancel */
    MPIDI_msg_sz_t data_sz;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif    
}
MPIDI_CH3_Pkt_send_t;

/* NOTE: Normal and synchronous eager sends, as well as all ready-mode sends, use the same structure but have a different type
   value. */
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_eager_send_t;
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_eager_sync_send_t;
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_ready_send_t;

typedef struct MPIDI_CH3_Pkt_eager_sync_ack
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Request sender_req_id;
}
MPIDI_CH3_Pkt_eager_sync_ack_t;

typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_rndv_req_to_send_t;

typedef struct MPIDI_CH3_Pkt_rndv_clr_to_send
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Request sender_req_id;
    MPI_Request receiver_req_id;
}
MPIDI_CH3_Pkt_rndv_clr_to_send_t;

typedef struct MPIDI_CH3_Pkt_rndv_send
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Request receiver_req_id;
}
MPIDI_CH3_Pkt_rndv_send_t;

typedef struct MPIDI_CH3_Pkt_cancel_send_req
{
    MPIDI_CH3_Pkt_type_t type;
    MPIDI_Message_match match;
    MPI_Request sender_req_id;
}
MPIDI_CH3_Pkt_cancel_send_req_t;

typedef struct MPIDI_CH3_Pkt_cancel_send_resp
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Request sender_req_id;
    int ack;
}
MPIDI_CH3_Pkt_cancel_send_resp_t;

#if defined(MPIDI_CH3_PKT_DEFS)
MPIDI_CH3_PKT_DEFS
#endif

typedef struct MPIDI_CH3_Pkt_put
{
    MPIDI_CH3_Pkt_type_t type;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
}
MPIDI_CH3_Pkt_put_t;

typedef struct MPIDI_CH3_Pkt_get
{
    MPIDI_CH3_Pkt_type_t type;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Request request_handle;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
}
MPIDI_CH3_Pkt_get_t;

typedef struct MPIDI_CH3_Pkt_get_resp
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Request request_handle;
}
MPIDI_CH3_Pkt_get_resp_t;

typedef struct MPIDI_CH3_Pkt_accum
{
    MPIDI_CH3_Pkt_type_t type;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Op op;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
}
MPIDI_CH3_Pkt_accum_t;

typedef struct MPIDI_CH3_Pkt_lock
{
    MPIDI_CH3_Pkt_type_t type;
    int lock_type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
}
MPIDI_CH3_Pkt_lock_t;

typedef struct MPIDI_CH3_Pkt_lock_granted
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Win source_win_handle;
}
MPIDI_CH3_Pkt_lock_granted_t;

typedef MPIDI_CH3_Pkt_lock_granted_t MPIDI_CH3_Pkt_pt_rma_done_t;

typedef struct MPIDI_CH3_Pkt_lock_put_unlock
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    void *addr;
    int count;
    MPI_Datatype datatype;
}
MPIDI_CH3_Pkt_lock_put_unlock_t;

typedef struct MPIDI_CH3_Pkt_lock_get_unlock
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    void *addr;
    int count;
    MPI_Datatype datatype;
    MPI_Request request_handle;
}
MPIDI_CH3_Pkt_lock_get_unlock_t;

typedef struct MPIDI_CH3_Pkt_lock_accum_unlock
{
    MPIDI_CH3_Pkt_type_t type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    void *addr;
    int count;
    MPI_Datatype datatype;
    MPI_Op op;
}
MPIDI_CH3_Pkt_lock_accum_unlock_t;


typedef struct MPIDI_CH3_Pkt_close
{
    MPIDI_CH3_Pkt_type_t type;
    int ack;
}
MPIDI_CH3_Pkt_close_t;

typedef union MPIDI_CH3_Pkt
{
    MPIDI_CH3_Pkt_type_t type;
    MPIDI_CH3_Pkt_eager_send_t eager_send;
    MPIDI_CH3_Pkt_eager_sync_send_t eager_sync_send;
    MPIDI_CH3_Pkt_eager_sync_ack_t eager_sync_ack;
    MPIDI_CH3_Pkt_eager_send_t ready_send;
    MPIDI_CH3_Pkt_rndv_req_to_send_t rndv_req_to_send;
    MPIDI_CH3_Pkt_rndv_clr_to_send_t rndv_clr_to_send;
    MPIDI_CH3_Pkt_rndv_send_t rndv_send;
    MPIDI_CH3_Pkt_cancel_send_req_t cancel_send_req;
    MPIDI_CH3_Pkt_cancel_send_resp_t cancel_send_resp;
    MPIDI_CH3_Pkt_put_t put;
    MPIDI_CH3_Pkt_get_t get;
    MPIDI_CH3_Pkt_get_resp_t get_resp;
    MPIDI_CH3_Pkt_accum_t accum;
    MPIDI_CH3_Pkt_lock_t lock;
    MPIDI_CH3_Pkt_lock_granted_t lock_granted;
    MPIDI_CH3_Pkt_pt_rma_done_t pt_rma_done;    
    MPIDI_CH3_Pkt_lock_put_unlock_t lock_put_unlock;
    MPIDI_CH3_Pkt_lock_get_unlock_t lock_get_unlock;
    MPIDI_CH3_Pkt_lock_accum_unlock_t lock_accum_unlock;
    MPIDI_CH3_Pkt_close_t close;
# if defined(MPIDI_CH3_PKT_DECL)
    MPIDI_CH3_PKT_DECL
# endif
}
MPIDI_CH3_Pkt_t;

#if defined(MPID_USE_SEQUENCE_NUMBERS)
typedef struct MPIDI_CH3_Pkt_send_container
{
    MPIDI_CH3_Pkt_send_t pkt;
    struct MPIDI_CH3_Pkt_send_container_s * next;
}
MPIDI_CH3_Pkt_send_container_t;
#endif

/*
 * MPIDI_CH3_CA_t
 *
 * An enumeration of the actions to perform when the requested I/O operation has completed.
 *
 * MPIDI_CH3_CA_COMPLETE - The last operation for this request has completed.  The completion counter should be decremented.  If
 * it has reached zero, then the request should be released by calling MPID_Request_release().
 *
 * MPIDI_CH3_CA_UNPACK_UEBUF_AND_COMPLETE - This is a special case of the MPIDI_CH3_CA_COMPLETE.  The data for an unexpected
 * eager messaage has been stored into a temporary buffer and needs to be copied/unpacked into the user buffer before the
 * completion counter can be decremented, etc.
 *
 * MPIDI_CH3_CA_UNPACK_SRBUF_AND_COMPLETE - This is a special case of the MPIDI_CH3_CA_COMPLETE.  The data from the completing
 * read has been stored into a temporary send/receive buffer and needs to be copied/unpacked into the user buffer before the
 * completion counter can be decremented, etc.
 *
 * MPIDI_CH3_CA_RELOAD_IOV - This request contains more segments of data than the IOV or buffer space allow.  Since the
 * previously request operation has completed, the IOV in the request should be reload at this time.
 *
 * MPIDI_CH3_CA_UNPACK_SRBUF_AND_RELOAD_IOV - This is a special case of the MPIDI_CH3_CA_RELOAD_IOV.  The data from the
 * completing read operation has been stored into a temporary send/receive buffer and needs to be copied/unpacked into the user
 * buffer before the IOV is reloaded.
 *
 * MPIDI_CH3_CA_END_CH3 - This not a real action, but rather a marker.  All actions numerically less than MPID_CA_END are defined
 * by channel device.  Any actions numerically greater than MPIDI_CA_END are internal to the channel instance and must be handled
 * by the channel instance.
 */
typedef enum MPIDI_CA
{
    MPIDI_CH3_CA_COMPLETE,
    MPIDI_CH3_CA_UNPACK_SRBUF_AND_COMPLETE,
    MPIDI_CH3_CA_UNPACK_UEBUF_AND_COMPLETE,
    MPIDI_CH3_CA_RELOAD_IOV,
    MPIDI_CH3_CA_UNPACK_SRBUF_AND_RELOAD_IOV,
    MPIDI_CH3_CA_END_CH3
# if defined(MPIDI_CH3_CA_ENUM)
    , MPIDI_CH3_CA_ENUM
# endif
}
MPIDI_CA_t;

typedef struct MPIDI_PG
{
    /* MPIU_Object field.  MPIDI_PG_t objects are not allocated using the MPIU_Object system, but we do use the associated
       reference counting routines.  Therefore, handle must be present, but is not used. */
    int handle;
    volatile int ref_count;

    /* Next pointer used to maintain a list of all process groups known to this process */
    struct MPIDI_PG * next;

    /* Number of processes in the process group */
    int size;

    /* VC table.  At present this is a pointer to an array of VC structures.  Someday we may want make this a pointer to an array
       of VC references.  Thus, it is important to use MPIDI_PG_Get_vc() instead of directly referencing this field. */
    struct MPIDI_VC * vct;

    /* Pointer to the process group ID.  The actual ID is defined and allocated by the process group.  The pointer is kept in the
       device space because it is necessary for the device to be able to find a particular process group. */
    void * id;
    
#if defined(MPIDI_CH3_PG_DECL)
    MPIDI_CH3_PG_DECL
#endif    
}
MPIDI_PG_t;

typedef enum MPIDI_VC_State
{
    MPIDI_VC_STATE_INACTIVE=1,
    MPIDI_VC_STATE_ACTIVE,
    MPIDI_VC_STATE_LOCAL_CLOSE,
    MPIDI_VC_STATE_REMOTE_CLOSE,
    MPIDI_VC_STATE_CLOSE_ACKED
} MPIDI_VC_State_t;

typedef struct MPIDI_VC
{
    /* XXX - need better comment */
    /* MPIU_Object fields.  MPIDI_VC_t objects are not allocated using the 
       MPIU_Object system, but we do use the associated
       reference counting routines.  The first field is normal a handle, but 
       since we are not using MPIU_Object for allocation,
       we instead replace the handle with a dummy field. */
    int dummy;
    volatile int ref_count;

    /* state of the VC */
    MPIDI_VC_State_t state;

    /* Process group to which this VC belongs */
    MPIDI_PG_t * pg;

    /* Rank of the process associated with this VC */
    int pg_rank;

    /* Local process ID */
    int lpid;
    
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    /* Sequence number of the next packet to be sent */
    MPID_Seqnum_t seqnum_send;
#endif
    
#if defined(MPIDI_CH3_MSGS_UNORDERED)
    /* Sequence number of the next packet we expect to receive */
    MPID_Seqnum_t seqnum_recv;

    /* Queue for holding packets received out of order.  NOTE: the CH3 device only orders packets.  Handling of out-of-order data
       is the responsibility of the channel. */
    MPIDI_CH3_Pkt_send_container_t * msg_reorder_queue;
#endif
    
# if defined(MPIDI_CH3_VC_DECL)
    MPIDI_CH3_VC_DECL
# endif
}
MPIDI_VC_t;

typedef enum MPIDI_VC_Event
{
    MPIDI_VC_EVENT_TERMINATED
}
MPIDI_VC_Event_t;

typedef struct MPIDI_VCRT * MPID_VCRT;
typedef struct MPIDI_VC * MPID_VCR;

#if defined(MPID_USE_SEQUENCE_NUMBERS)
#   define MPIDI_REQUEST_SEQNUM	\
        MPID_Seqnum_t seqnum;
#else
#   define MPIDI_REQUEST_SEQNUM
#endif

#define MPID_REQUEST_DECL													\
struct MPIDI_Request														\
{																\
    MPIDI_Message_match match;													\
																\
    /* TODO - user_buf, user_count, and datatype needed to process rendezvous messages. */					\
    void * user_buf;														\
    int user_count;														\
    MPI_Datatype datatype;													\
																\
    /* segment, segment_first, and segment_size are used when processing non-contiguous datatypes */				\
    MPID_Segment segment;													\
    MPIDI_msg_sz_t segment_first;												\
    MPIDI_msg_sz_t segment_size;												\
																\
    /* Pointer to datatype for reference counting purposes */									\
    struct MPID_Datatype * datatype_ptr;											\
																\
    /* iov and iov_count define the data to be transferred/received */								\
    MPID_IOV iov[MPID_IOV_LIMIT];												\
    int iov_count;														\
    MPID_IOV rdma_iov[MPID_IOV_LIMIT];												\
    int rdma_iov_count;														\
    int rdma_iov_offset;													\
    MPI_Request rdma_request;													\
																\
    /* ca (completion action) identifies the action to take once the operation described by the iov has completed */		\
    MPIDI_CA_t ca;														\
																\
    /* tmpbuf and tmpbuf_sz describe temporary storage used for things like unexpected eager messages and packing/unpacking	\
       buffers.  tmpuf_off is the current offset into the temporary buffer. */							\
    void * tmpbuf;														\
    int tmpbuf_off;														\
    MPIDI_msg_sz_t tmpbuf_sz;													\
																\
    MPIDI_msg_sz_t recv_data_sz;												\
    MPI_Request sender_req_id;													\
																\
    unsigned state;														\
    int cancel_pending;													        \
    int recv_pending_count;												        \
																\
    /* The next 6 are for RMA */                                                                                                \
    MPI_Op op;												                        \
    /* For accumulate, since data is first read into a tmp_buf */								\
    void *real_user_buf;													\
    /* For derived datatypes at target */								                        \
    MPIDI_RMA_dtype_info *dtype_info;												\
    void *dataloop;													        \
    /* req. handle needed to implement derived datatype gets  */					                        \
    MPI_Request request_handle;											                        \
    MPI_Win target_win_handle;   										                        \
    MPI_Win source_win_handle;   										                        \
    int single_op_opt;   /* to indicate a lock-put-unlock optimization case */                                                  \
    MPIDI_Win_lock_queue *lock_queue_entry; /* for single lock-put-unlock optimization */		                        \
																\
    MPIDI_REQUEST_SEQNUM													\
																\
    struct MPID_Request * next;													\
} dev;

#if defined(MPIDI_CH3_REQUEST_DECL)
#define MPID_DEV_REQUEST_DECL			\
MPID_REQUEST_DECL				\
MPIDI_CH3_REQUEST_DECL
#else
#define MPID_DEV_REQUEST_DECL			\
MPID_REQUEST_DECL
#endif

#ifdef MPIDI_CH3_REQUEST_KIND_DECL
#define MPID_DEV_REQUEST_KIND_DECL MPIDI_CH3_REQUEST_KIND_DECL
#endif

#if defined(MPIDI_CH3_PROGRESS_STATE_DECL)
#   define MPID_PROGRESS_STATE_DECL MPIDI_CH3_PROGRESS_STATE_DECL
#else
#   define MPID_PROGRESS_STATE_DECL int foo;
#endif


/* to send derived datatype across in RMA ops */
typedef struct MPIDI_RMA_dtype_info { /* for derived datatypes */
    int           is_contig; 
    int           n_contig_blocks;
    int           size;     
    MPI_Aint      extent;   
    int           dataloop_size; /* not needed because this info is sent in packet header. remove it after lock/unlock is implemented in the device */
    void          *dataloop;  /* pointer needed to update pointers
                                 within dataloop on remote side */
    int           dataloop_depth; 
    int           eltype;
    MPI_Aint ub, lb, true_ub, true_lb;
    int has_sticky_ub, has_sticky_lb;
} MPIDI_RMA_dtype_info;

/* for keeping track of RMA ops, which will be executed at the next sync call */
typedef struct MPIDI_RMA_ops {
    struct MPIDI_RMA_ops *next;  /* pointer to next element in list */
    int type;  /* MPIDI_RMA_PUT, MPID_REQUEST_GET,
		  MPIDI_RMA_ACCUMULATE, MPIDI_RMA_LOCK */
    void *origin_addr;
    int origin_count;
    MPI_Datatype origin_datatype;
    int target_rank;
    MPI_Aint target_disp;
    int target_count;
    MPI_Datatype target_datatype;
    MPI_Op op;  /* for accumulate */
    int lock_type;  /* for win_lock */
} MPIDI_RMA_ops;

typedef struct MPIDI_PT_single_op {
    int type;  /* put, get, or accum. */
    void *addr;
    int count;
    MPI_Datatype datatype;
    MPI_Op op;
    void *data;  /* for queued puts and accumulates, data is copied here */
    MPI_Request request_handle;  /* for gets */
    int data_recd;  /* to indicate if the data has been received */
} MPIDI_PT_single_op;

typedef struct MPIDI_Win_lock_queue {
    struct MPIDI_Win_lock_queue *next;
    int lock_type;
    MPI_Win source_win_handle;
    MPIDI_VC_t * vc;
    struct MPIDI_PT_single_op *pt_single_op;  /* to store info for lock-put-unlock optimization */
} MPIDI_Win_lock_queue;

#define MPIDI_DEV_WIN_DECL                                                              \
    volatile int my_counter;  /* completion counter for operations                      \
                                 targeting this window */                               \
    void **base_addrs;     /* array of base addresses of the windows of                 \
                              all processes */                                          \
    int *disp_units;      /* array of displacement units of all windows */              \
    MPI_Win *all_win_handles;    /* array of handles to the window objects              \
                                          of all processes */                           \
    MPIDI_RMA_ops *rma_ops_list; /* list of outstanding RMA requests */                 \
    volatile int lock_granted;  /* flag to indicate whether lock has                    \
                                   been granted to this process (as source) for         \
                                   passive target rma */                                \
    volatile int current_lock_type;   /* current lock type on this window (as target)   \
                              * (none, shared, exclusive) */                            \
    volatile int shared_lock_ref_cnt;                                                   \
    struct MPIDI_Win_lock_queue volatile *lock_queue;  /* list of unsatisfied locks */  \
                                                                                        \
    int *pt_rma_puts_accs;  /* array containing the no. of passive target               \
                               puts/accums issued from this process to other            \
                               processes. */                                            \
    volatile int my_pt_rma_puts_accs;  /* no. of passive target puts/accums             \
                                          that this process has                         \
                                          completed as target */
 
#ifdef MPIDI_CH3_WIN_DECL
#define MPID_DEV_WIN_DECL \
MPIDI_DEV_WIN_DECL \
MPIDI_CH3_WIN_DECL
#else
#define MPID_DEV_WIN_DECL \
MPIDI_DEV_WIN_DECL
#endif

/* Tell Intercomm create and friends that the GPID routines have been
   implemented */
#define HAVE_GPID_ROUTINES

#endif /* !defined(MPICH_MPIDPRE_H_INCLUDED) */
