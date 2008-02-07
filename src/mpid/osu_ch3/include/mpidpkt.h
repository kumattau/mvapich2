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

#ifndef HAVE_MPIDPKT_H
#define HAVE_MPIDPKT_H
/*
 * MPIDI_CH3_Pkt_type_t
 *
 */
typedef enum MPIDI_CH3_Pkt_type
{
    MPIDI_CH3_PKT_EAGER_SEND = 0,
    /* OSU-MPI2 */
#if defined (USE_HEADER_CACHING)
    MPIDI_CH3_PKT_FAST_EAGER_SEND,
    MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ,
#endif
    MPIDI_CH3_PKT_RPUT_FINISH,
    MPIDI_CH3_PKT_RGET_FINISH,
    MPIDI_CH3_PKT_NOOP,
    MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND,
    MPIDI_CH3_PKT_PUT_RNDV,
    MPIDI_CH3_PKT_ACCUMULATE_RNDV,  /*8*/
    MPIDI_CH3_PKT_GET_RNDV,         /*9*/
    MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND,
    MPIDI_CH3_PKT_PACKETIZED_SEND_START,
    MPIDI_CH3_PKT_PACKETIZED_SEND_DATA,
    MPIDI_CH3_PKT_RNDV_R3_DATA,
    MPIDI_CH3_PKT_ADDRESS,
#if defined(CKPT)
    MPIDI_CH3_PKT_CM_SUSPEND,
    MPIDI_CH3_PKT_CM_REACTIVATION_DONE,
    MPIDI_CH3_PKT_CR_REMOTE_UPDATE,
#endif
    /* End of OSU-MPI2 */
    MPIDI_CH3_PKT_EAGERSHORT_SEND,
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
    /* The channel can define additional types by defining the value
       MPIDI_CH3_PKT_ENUM */
# if defined(MPIDI_CH3_PKT_ENUM)
    , MPIDI_CH3_PKT_ENUM 
# endif    
    , MPIDI_CH3_PKT_END_ALL
}
MPIDI_CH3_Pkt_type_t;

typedef struct MPIDI_CH3_Pkt_send
{
    uint8_t type;  /* XXX - uint8_t to conserve space ??? */
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
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
/* Enable the use of data within the message packet for small messages */
/* #define USE_EAGER_SHORT  */
#define MPIDI_EAGER_SHORT_INTS 4
#define MPIDI_EAGER_SHORT_SIZE 16
typedef struct MPIDI_CH3_Pkt_eagershort_send
{
    uint8_t type;  /* XXX - uint8_t to conserve space ??? */
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPIDI_Message_match match;
    MPIDI_msg_sz_t data_sz;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif
    int  data[MPIDI_EAGER_SHORT_INTS];    /* FIXME: Experimental for now */
}
MPIDI_CH3_Pkt_eagershort_send_t;

typedef struct MPIDI_CH3_Pkt_eager_sync_ack
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPI_Request sender_req_id;
}
MPIDI_CH3_Pkt_eager_sync_ack_t;

/* OSU-MPI2 */
typedef struct MPIDI_CH3_Pkt_rndv_req_to_send
{
    uint8_t type;  /* XXX - uint8_t to conserve space ??? */
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPIDI_Message_match match;
    MPI_Request sender_req_id;  /* needed for ssend and send cancel */
    MPIDI_msg_sz_t data_sz;
    /* Newly added packet fields for OSU-MPI2 */
    MPID_Seqnum_t seqnum;
    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
    /* End of OSU-MPI2 */
} MPIDI_CH3_Pkt_rndv_req_to_send_t;

typedef struct MPIDI_CH3I_Pkt_address {
    uint8_t type;  /* XXX - uint8_t to conserve space ??? */
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPIDI_CH3I_MRAILI_PKT_ADDRESS_DECL
} MPIDI_CH3_Pkt_address_t;
/* End of OSU-MPI2 */

typedef struct MPIDI_CH3_Pkt_rndv_clr_to_send
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPI_Request sender_req_id;
    MPI_Request receiver_req_id;
    /* OSU-MPI2 */
    MPID_Seqnum_t seqnum;
    int         recv_sz;
    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
    /* End of OSU-MPI2 */
}
MPIDI_CH3_Pkt_rndv_clr_to_send_t;

typedef struct MPIDI_CH3_Pkt_rndv_send
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    /* End of OSU-MPI2 */
    MPI_Request receiver_req_id;
}
MPIDI_CH3_Pkt_rndv_send_t;

/* OSU-MPI2 */
typedef struct MPIDI_CH3_Pkt_packetized_send_start {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    MPIDI_msg_sz_t origin_head_size;
} MPIDI_CH3_Pkt_packetized_send_start_t;

typedef struct MPIDI_CH3_Pkt_packetized_send_data {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    MPI_Request receiver_req_id;
} MPIDI_CH3_Pkt_packetized_send_data_t;

typedef MPIDI_CH3_Pkt_packetized_send_data_t MPIDI_CH3_Pkt_rndv_r3_data_t;

typedef struct MPIDI_CH3_Pkt_rput_finish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    MPI_Request receiver_req_id; /* echoed*/
} MPIDI_CH3_Pkt_rput_finish_t;

typedef struct MPIDI_CH3_Pkt_rget_finish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request sender_req_id;
} MPIDI_CH3_Pkt_rget_finish_t;
/* End of OSU-MPI2 */

typedef struct MPIDI_CH3_Pkt_cancel_send_req
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    /* End of OSU-MPI2 */
    MPIDI_Message_match match;
    MPI_Request sender_req_id;
}
MPIDI_CH3_Pkt_cancel_send_req_t;

typedef struct MPIDI_CH3_Pkt_cancel_send_resp
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    /* End of OSU-MPI2 */
    MPI_Request sender_req_id;
    int ack;
}
MPIDI_CH3_Pkt_cancel_send_resp_t;

#if defined(MPIDI_CH3_PKT_DEFS)
MPIDI_CH3_PKT_DEFS
#endif

typedef struct MPIDI_CH3_Pkt_put
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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

/* Newly added packet type for OSU-MPI2 */
typedef struct MPIDI_CH3_Pkt_put_rndv
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
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
    MPI_Request sender_req_id;
    int data_sz;
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;
}
MPIDI_CH3_Pkt_put_rndv_t;
/* End of OSU-MPI2 */

typedef struct MPIDI_CH3_Pkt_get
{
    uint8_t type;
    /* OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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

/* Newly added packet type for OSU-MPI2 */
typedef struct MPIDI_CH3_Pkt_get_rndv
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
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
    int data_sz;
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;
}
MPIDI_CH3_Pkt_get_rndv_t;
/* End of OSU-MPI2 */

typedef struct MPIDI_CH3_Pkt_get_resp
{
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    int protocol;
    /* End of OSU-MPI2 */
    MPI_Request request_handle;
}
MPIDI_CH3_Pkt_get_resp_t;

/* OSU-MPI2 */
typedef struct MPIDI_CH3_Pkt_accum_rndv
{
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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
    MPI_Request sender_req_id;
    int data_sz;

    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
}
MPIDI_CH3_Pkt_accum_rndv_t;
/* End of OSU-MPI2 */

typedef struct MPIDI_CH3_Pkt_accum
{
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    /* End of OSU-MPI2 */
    int lock_type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
}
MPIDI_CH3_Pkt_lock_t;

typedef struct MPIDI_CH3_Pkt_lock_granted
{
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    /* End of OSU-MPI2 */
    MPI_Win source_win_handle;
}
MPIDI_CH3_Pkt_lock_granted_t;

typedef MPIDI_CH3_Pkt_lock_granted_t MPIDI_CH3_Pkt_pt_rma_done_t;

typedef struct MPIDI_CH3_Pkt_lock_put_unlock
{
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    uint32_t rma_issued;
    /* End of OSU-MPI2 */
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
    uint8_t type;
    /* Newly added packet fields for OSU-MPI2 */
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPID_Seqnum_t seqnum;
    /* End of OSU-MPI2 */
    int ack;
}
MPIDI_CH3_Pkt_close_t;

typedef union MPIDI_CH3_Pkt
{
    uint8_t type;
    MPIDI_CH3_Pkt_eager_send_t eager_send;
    MPIDI_CH3_Pkt_eagershort_send_t eagershort_send;
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
    /* OSU-MPI2 */
    MPIDI_CH3_Pkt_address_t address;
    MPIDI_CH3_Pkt_rput_finish_t rput_finish;
    MPIDI_CH3_Pkt_put_rndv_t put_rndv;
    MPIDI_CH3_Pkt_get_rndv_t get_rndv;
    MPIDI_CH3_Pkt_accum_rndv_t accum_rndv;
    /* End of OSU-MPI2 */
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

/* OSU-MPI2 */
extern int MPIDI_CH3_Pkt_size_index[];
/* End of OSU-MPI2 */

#if defined(MPID_USE_SEQUENCE_NUMBERS)
typedef struct MPIDI_CH3_Pkt_send_container
{
    MPIDI_CH3_Pkt_send_t pkt;
    struct MPIDI_CH3_Pkt_send_container_s * next;
}
MPIDI_CH3_Pkt_send_container_t;
#endif

#endif
