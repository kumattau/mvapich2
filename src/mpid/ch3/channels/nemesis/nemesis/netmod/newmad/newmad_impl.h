/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef NEWMAD_MODULE_IMPL_H
#define NEWMAD_MODULE_IMPL_H
#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <nm_public.h>
#include <nm_sendrecv_interface.h>
#include <nm_predictions.h>
#include "mpid_nem_impl.h"

int MPID_nem_newmad_init (MPID_nem_queue_ptr_t proc_recv_queue, MPID_nem_queue_ptr_t proc_free_queue, 
		      MPID_nem_cell_ptr_t proc_elements,int num_proc_elements, 
		      MPID_nem_cell_ptr_t module_elements, int num_module_elements,
		      MPID_nem_queue_ptr_t *module_free_queue,
		      MPIDI_PG_t *pg_p, int pg_rank, char **bc_val_p, int *val_max_sz_p);
int MPID_nem_newmad_finalize (void);
int MPID_nem_newmad_ckpt_shutdown (void);
int MPID_nem_newmad_poll(int in_blocking_progress);
int MPID_nem_newmad_send (MPIDI_VC_t *vc, MPID_nem_cell_ptr_t cell, int datalen);
int MPID_nem_newmad_get_business_card (int my_rank, char **bc_val_p, int *val_max_sz_p);
int MPID_nem_newmad_connect_to_root (const char *business_card, MPIDI_VC_t *new_vc);
int MPID_nem_newmad_vc_init (MPIDI_VC_t *vc);
int MPID_nem_newmad_vc_destroy(MPIDI_VC_t *vc);
int MPID_nem_newmad_vc_terminate (MPIDI_VC_t *vc);
 
/* alternate interface */
int MPID_nem_newmad_iSendContig(MPIDI_VC_t *vc, MPID_Request *sreq, void *hdr, MPIDI_msg_sz_t hdr_sz, 
			    void *data, MPIDI_msg_sz_t data_sz);
int MPID_nem_newmad_iStartContigMsg(MPIDI_VC_t *vc, void *hdr, MPIDI_msg_sz_t hdr_sz, void *data, 
				MPIDI_msg_sz_t data_sz, MPID_Request **sreq_ptr);
int MPID_nem_newmad_SendNoncontig(MPIDI_VC_t *vc, MPID_Request *sreq, void *header, MPIDI_msg_sz_t hdr_sz);

/* Direct Routines */
int  MPID_nem_newmad_directSend(MPIDI_VC_t *vc, const void * buf, int count, MPI_Datatype datatype, int dest, int tag, 
			    MPID_Comm * comm, int context_offset, MPID_Request **sreq_p);
int  MPID_nem_newmad_directSsend(MPIDI_VC_t *vc, const void * buf, int count, MPI_Datatype datatype, int dest, int tag, 
			    MPID_Comm * comm, int context_offset,MPID_Request **sreq_p);
int MPID_nem_newmad_directRecv(MPIDI_VC_t *vc, MPID_Request *rreq);
int MPID_nem_newmad_cancel_send(MPIDI_VC_t *vc, MPID_Request *sreq);
int MPID_nem_newmad_cancel_recv(MPIDI_VC_t *vc, MPID_Request *rreq);
int MPID_nem_newmad_probe(MPIDI_VC_t *vc,  int source, int tag, MPID_Comm *comm, 
			  int context_offset, MPI_Status *status);
int MPID_nem_newmad_iprobe(MPIDI_VC_t *vc,  int source, int tag, MPID_Comm *comm, 
			   int context_offset, int *flag, MPI_Status *status);
/* Any source management */
void MPID_nem_newmad_anysource_posted(MPID_Request *rreq);
int MPID_nem_newmad_anysource_matched(MPID_Request *rreq);

/* Callbacks for events */
int MPID_nem_newmad_post_init(void);
void MPID_nem_newmad_get_adi_msg(nm_sr_event_t event, const nm_sr_event_info_t*info);

/* Dtype management */
int MPID_nem_newmad_process_sdtype(MPID_Request **sreq_p,  MPI_Datatype datatype,  MPID_Datatype * dt_ptr, const void *buf, 
				   int count, MPIDI_msg_sz_t data_sz, struct iovec **newmad_iov, int  *num_iov, int first_taken);
int MPID_nem_newmad_process_rdtype(MPID_Request **rreq_p, MPID_Datatype * dt_ptr, MPIDI_msg_sz_t data_sz,struct iovec **newmad_iov, 
				   int *num_iov);

/* Connection management*/
int MPID_nem_newmad_send_conn_info (MPIDI_VC_t *vc);

#define MPID_NEM_NMAD_MAX_NETS 4
#define MPID_NEM_NMAD_MAX_SIZE (10*(MPID_NEM_MAX_NETMOD_STRING_LEN))
typedef nm_gate_t mpid_nem_newmad_p_gate_t;

typedef struct MPID_nem_newmad_init_req
{
   nm_sr_request_t           init_request;
   mpid_nem_newmad_p_gate_t  p_gate;
   int                       process_no;
}
MPID_nem_newmad_init_req_t;

typedef struct MPID_nem_newmad_vc_area_internal
{
    char                     hostname[MPID_NEM_NMAD_MAX_SIZE];
    char                     url[MPID_NEM_NMAD_MAX_NETS][MPID_NEM_NMAD_MAX_SIZE];
    uint8_t                  drv_id[MPID_NEM_NMAD_MAX_NETS];
    mpid_nem_newmad_p_gate_t p_gate;
} MPID_nem_newmad_vc_area_internal_t;

/* The current max size for the whole structure is 128 bytes */
typedef struct
{
    MPID_nem_newmad_vc_area_internal_t *area;
} MPID_nem_newmad_vc_area;
/* accessor macro to private fields in VC */
#define VC_FIELD(vcp, field) (((MPID_nem_newmad_vc_area *)((MPIDI_CH3I_VC *)((vcp)->channel_private))->netmod_area.padding)->area->field)

/* The req provides a generic buffer in which network modules can store
   private fields This removes all dependencies from the req structure
   on the network module, facilitating dynamic module loading. */
typedef struct 
{
    nm_sr_request_t newmad_req;
    void           *iov;
} MPID_nem_newmad_req_area;
/* accessor macro to private fields in REQ */
#define REQ_FIELD(reqp, field) (((MPID_nem_newmad_req_area *)((reqp)->ch.netmod_area.padding))->field)

/* The begining of this structure is the same as MPID_Request */
struct MPID_nem_newmad_internal_req
{
   int                    handle;     /* unused */
   volatile int           ref_count;  /* unused */
   MPID_Request_kind_t    kind;       /* used   */
   MPIDI_CH3_PktGeneric_t pending_pkt;
   MPIDI_VC_t            *vc;
   void                  *tmpbuf;
   MPIDI_msg_sz_t         tmpbuf_sz;
   nm_sr_request_t        newmad_req;
   struct  MPID_nem_mx_internal_req *next;
};

typedef struct MPID_nem_newmad_internal_req MPID_nem_newmad_internal_req_t;

typedef union
{   
   MPID_nem_newmad_internal_req_t nem_newmad_req;
   MPID_Request                   mpi_req;
} MPID_nem_newmad_unified_req_t ;

/* Internal Reqs management */
int MPID_nem_newmad_internal_req_queue_init(void);
int MPID_nem_newmad_internal_req_queue_destroy(void);
int MPID_nem_newmad_internal_req_dequeue(MPID_nem_newmad_internal_req_t **req);
int MPID_nem_newmad_internal_req_enqueue(MPID_nem_newmad_internal_req_t *req);

#if CH3_RANK_BITS == 16
#define NBITS_TAG  32
typedef int32_t Nmad_Nem_tag_t;
#elif CH3_RANK_BITS == 32
#define NBITS_TAG  16
typedef int16_t Nmad_Nem_tag_t;
#endif /* CH3_RANK_BITS */

#define NBITS_RANK CH3_RANK_BITS
#define NBITS_CTXT 16
#define NBITS_PGRANK (sizeof(int)*8)

#define NEM_NMAD_MATCHING_BITS (NBITS_TAG+NBITS_RANK+NBITS_CTXT)
#define SHIFT_TAG              (NBITS_RANK+NBITS_CTXT)
#define SHIFT_RANK             (NBITS_CTXT)
#define SHIFT_PGRANK           (NBITS_CTXT)
#define SHIFT_CTXT             (0)

#define NEM_NMAD_MAX_TAG       ((UINT64_C(1)<<NBITS_TAG)   -1)
#define NEM_NMAD_MAX_RANK      ((UINT64_C(1)<<NBITS_RANK)  -1)
#define NEM_NMAD_MAX_CTXT      ((UINT64_C(1)<<NBITS_CTXT)  -1)
#define NEM_NMAD_MAX_PGRANK    ((UINT64_C(1)<<NBITS_PGRANK)-1)

#define NEM_NMAD_TAG_MASK      (NEM_NMAD_MAX_TAG <<SHIFT_TAG )
#define NEM_NMAD_RANK_MASK     (NEM_NMAD_MAX_RANK<<SHIFT_RANK)
#define NEM_NMAD_CTXT_MASK     (NEM_NMAD_MAX_CTXT<<SHIFT_CTXT)
#define NEM_NMAD_PGRANK_MASK   (NEM_NMAD_MAX_PGRANK<<SHIFT_PGRANK)

#define NEM_NMAD_SET_TAG(_match, _tag)  do {                              \
    MPIU_Assert((_tag >= 0)&&(_tag <= (NEM_NMAD_MAX_TAG)));               \
    ((_match) |= (((nm_tag_t)((_tag)&(NEM_NMAD_MAX_TAG))) << SHIFT_TAG)); \
    }while(0)
#define NEM_NMAD_SET_SRC(_match, _src) do {               \
    MPIU_Assert(_src >= 0)&&(_src<=(NEM_NMAD_MAX_RANK))); \
    ((_match) |= (((nm_tag_t)(_src)) << SHIFT_RANK));     \
    }while(0)
#define NEM_NMAD_SET_CTXT(_match, _ctxt) do {              \
    MPIU_Assert(_ctxt >= 0)&&(_ctxt<=(NEM_NMAD_MAX_CTXT)));\
    ((_match) |= (((nm_tag_t)(_ctxt)) << SHIFT_CTXT));	   \
    }while(0)
#define NEM_NMAD_SET_PGRANK(_match, _pg_rank)  do {        \
    ((_match) |= (((nm_tag_t)(_pg_rank)) << SHIFT_PGRANK));\
    }while(0)

#define NEM_NMAD_MATCH_GET_TAG(_match, _tag)   do{                             \
    ((_tag)  = ((Nmad_Nem_tag_t)(((_match) & NEM_NMAD_TAG_MASK)  >> SHIFT_TAG)));\
    }while(0)
#define NEM_NMAD_MATCH_GET_RANK(_match, _rank) do{                             \
    ((_rank) = ((MPIR_Rank_t)(((_match) & NEM_NMAD_RANK_MASK) >> SHIFT_RANK)));\
    }while(0)
#define NEM_NMAD_MATCH_GET_CTXT(_match, _ctxt) do{                                   \
    ((_ctxt) = ((MPIR_Context_id_t)(((_match) & NEM_NMAD_CTXT_MASK) >> SHIFT_CTXT)));\
    }while(0)
#define NEM_NMAD_MATCH_GET_PGRANK(_match, _pg_rank) do{                        \
    ((_pg_rank) = ((int)(((_match) & NEM_NMAD_PGRANK_MASK) >> SHIFT_PGRANK))); \
    }while(0)

#define NEM_NMAD_INTRA_CTXT (0x0000000c)
#define NEM_NMAD_SET_MATCH(_match,_tag,_rank,_context ) do{        \
    MPIU_Assert((_tag >= 0)    &&(_tag <= (NEM_NMAD_MAX_TAG)));    \
    MPIU_Assert((_rank >= 0)   &&(_rank<=(NEM_NMAD_MAX_RANK)));    \
    MPIU_Assert((_context >= 0)&&(_context<=(NEM_NMAD_MAX_CTXT))); \
    (_match)=((((nm_tag_t)(_tag))    << SHIFT_TAG)                 \
   	     |(((nm_tag_t)(_rank))   << SHIFT_RANK)                \
	     |(((nm_tag_t)(_context))<< SHIFT_CTXT));              \
    }while(0)
#define NEM_NMAD_DIRECT_MATCH(_match,_tag,_rank,_context) NEM_NMAD_SET_MATCH(_match,_tag,_rank,_context)
#define NEM_NMAD_ADI_MATCH(_match)                        NEM_NMAD_SET_MATCH(_match,0,0,NEM_NMAD_INTRA_CTXT)

extern nm_core_t  mpid_nem_newmad_pcore;
extern int        mpid_nem_newmad_pending_send_req;

#define NMAD_IOV_MAX_DEPTH 15 /* NM_SO_PREALLOC_IOV_LEN */
//#define DEBUG

#endif //NEWMAD_MODULE_IMPL_H

