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

#if !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED)
#define MPICH_MPIDI_CH3_IMPL_H_INCLUDED

#include "mpidi_ch3i_rdma_conf.h"
#include "mpidimpl.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_ASSERT_H
#include <assert.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#define MPIDI_CH3I_SPIN_COUNT_DEFAULT   100
#define MPIDI_CH3I_YIELD_COUNT_DEFAULT  5000

#define MPIDI_CH3I_READ_STATE_IDLE    0
#define MPIDI_CH3I_READ_STATE_READING 1


typedef struct MPIDI_CH3I_Process_s
{
    MPIDI_VC_t *vc;
}
MPIDI_CH3I_Process_t;

extern MPIDI_CH3I_Process_t MPIDI_CH3I_Process;

#define MPIDI_CH3I_SendQ_enqueue(vc, req)				\
{									\
    /* MT - not thread safe! */						\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue vc=%08p req=0x%08x",	\
	              vc, req->handle));		\
    req->dev.next = NULL;						\
    if (vc->ch.sendq_tail != NULL)					\
    {									\
	vc->ch.sendq_tail->dev.next = req;				\
    }									\
    else								\
    {									\
	vc->ch.sendq_head = req;					\
    }									\
    vc->ch.sendq_tail = req;						\
}

#define MPIDI_CH3I_SendQ_enqueue_head(vc, req)				     \
{									     \
    /* MT - not thread safe! */						     \
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue_head vc=%08p req=0x%08x", \
	              vc, req->handle));		     \
    req->dev.next = vc->ch.sendq_head;					     \
    if (vc->ch.sendq_tail == NULL)					     \
    {									     \
	vc->ch.sendq_tail = req;					     \
    }									     \
    vc->ch.sendq_head = req;						     \
}

#define MPIDI_CH3I_SendQ_dequeue(vc)					\
{									\
    /* MT - not thread safe! */						\
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_dequeue vc=%08p req=0x%08x",	\
	              vc, vc->ch.sendq_head));		\
    vc->ch.sendq_head = vc->ch.sendq_head->dev.next;			\
    if (vc->ch.sendq_head == NULL)					\
    {									\
	vc->ch.sendq_tail = NULL;					\
    }									\
}

#define MPIDI_CH3I_SendQ_head(vc) (vc->ch.sendq_head)

#define MPIDI_CH3I_SendQ_empty(vc) (vc->ch.sendq_head == NULL)

/* RDMA channel interface */

int MPIDI_CH3I_Progress_init(void);
int MPIDI_CH3I_Progress_finalize(void);
int MPIDI_CH3I_Request_adjust_iov(MPID_Request *, MPIDI_msg_sz_t);
int MPIDI_CH3_Rendezvous_push(MPIDI_VC_t * vc, MPID_Request * sreq);
void MPIDI_CH3_Rendezvous_r3_push(MPIDI_VC_t * vc, MPID_Request * sreq);
void MPIDI_CH3I_MRAILI_Process_rndv(void);

int MPIDI_CH3I_read_progress(MPIDI_VC_t **vc_pptr, vbuf **);
int MPIDI_CH3I_write_progress();
int MPIDI_CH3I_post_read(MPIDI_VC_t *vc, void *buf, int len);
int MPIDI_CH3I_post_readv(MPIDI_VC_t *vc, MPID_IOV *iov, int n);

/* RDMA implementation interface */

/*
MPIDI_CH3I_RDMA_init_process_group is called before the RDMA channel is initialized.  It's job is to initialize the
process group and determine the process rank in the group.  has_parent is used by spawn and you
can set it to FALSE.  You can copy the implementation found in the shm directory for this 
function if you want to use the PMI interface for process management (recommended).
*/
int MPIDI_CH3I_RDMA_init_process_group(int * has_parent, MPIDI_PG_t ** pg_pptr, int * pg_rank_ptr);

/*
MPIDI_CH3I_RMDA_init is called after the RDMA channel has been initialized and the VC structures have
been allocated.  VC stands for Virtual Connection.  This should be the main initialization
routine that fills in any implementation specific fields to the VCs, connects all the processes
to each other and performs all other global initialization.  After this function is called 
all the processes must be connected.  The ch channel assumes a fully connected network.
*/
int MPIDI_CH3I_RMDA_init();

/* finalize releases the RDMA memory and any other cleanup */
int MPIDI_CH3I_RMDA_finalize();

/*
MPIDI_CH3I_RDMA_put_datav puts data into the ch memory of the remote process specified by the vc.
It returns the number of bytes successfully written in the num_bytes_ptr parameter.  This may be zero 
or up to the total amount of data described by the input iovec.  The data does not have to arrive
at the destination before this function returns but the local buffers may be touched.
*/
int MPIDI_CH3I_RDMA_put_datav(MPIDI_VC_t *vc, MPID_IOV *iov, int n, int *num_bytes_ptr);

/*
MPIDI_CH3I_RDMA_read_datav reads data from the local ch memory into the user buffer described by 
the iovec.  This function sets num_bytes_ptr to the amout of data successfully read which may be zero.
This function only reads data that was previously put by the remote process indentified by the vc.
*/
int MPIDI_CH3I_RDMA_read_datav(MPIDI_VC_t *vc, MPID_IOV *iov, int n, int *num_bytes_ptr);

/********** Added interface for OSU-MPI2 ************/
int MPIDI_CH3_Rendezvous_rput_finish(MPIDI_VC_t *, MPIDI_CH3_Pkt_rput_finish_t *);

int MPIDI_CH3_Packetized_send(MPIDI_VC_t * vc, MPID_Request *);

int MPIDI_CH3_Packetized_recv_data(MPIDI_VC_t * vc, vbuf * v);
                                                                                                                                               
int MPIDI_CH3_Rendezvouz_r3_recv_data(MPIDI_VC_t * vc, vbuf * v);
                                                                                                                                               
int MPIDI_CH3_Packetized_recv_req(MPIDI_VC_t * vc, MPID_Request *);

/* Mrail interfaces*/
int MPIDI_CH3I_MRAIL_Prepare_rndv(
                MPIDI_VC_t * vc, MPID_Request * rreq);
                                                                                                                                               
int MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(MPID_Request * sreq,
                MPIDI_CH3I_MRAILI_Rndv_info_t *rndv);

int MPIDI_CH3I_MRAILI_Get_rndv_rput(MPIDI_VC_t *vc,
                                    MPID_Request * req,
                                    MPIDI_CH3I_MRAILI_Rndv_info_t * rndv);

int MPIDI_CH3I_MRAIL_Parse_header(MPIDI_VC_t * vc, vbuf * v, void **, int
            *headersize);
                                                                                                                                               
int MPIDI_CH3I_MRAIL_Fill_Request(MPID_Request *, vbuf *v, int header_size, int * nb);

void  MPIDI_CH3I_MRAIL_Release_vbuf(vbuf * v);

#ifdef _SMP_


#define MPIDI_CH3I_SMP_SendQ_enqueue(vc, req)               \
{                                   \
    /* MT - not thread safe! */                     \
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue vc=%08p req=0x%08x",   \
                  vc, req->handle));        \
    req->dev.next = NULL;                       \
    if (vc->smp.sendq_tail != NULL)                  \
    {                                   \
    vc->smp.sendq_tail->dev.next = req;              \
    }                                   \
    else                                \
    {                                   \
    vc->smp.sendq_head = req;                    \
    }                                   \
    vc->smp.sendq_tail = req;                        \
}
                                                                                                                                               
#define MPIDI_CH3I_SMP_SendQ_enqueue_head(vc, req)                   \
{                                        \
    /* MT - not thread safe! */                          \
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_enqueue_head vc=%08p req=0x%08x", \
                  vc, req->handle));             \
    req->dev.next = vc->smp.sendq_head;                       \
    if (vc->smp.sendq_tail == NULL)                       \
    {                                        \
    vc->smp.sendq_tail = req;                         \
    }                                        \
    vc->smp.sendq_head = req;                             \
}

#define MPIDI_CH3I_SMP_SendQ_dequeue(vc)                    \
{                                   \
    /* MT - not thread safe! */                     \
    MPIDI_DBG_PRINTF((50, FCNAME, "SendQ_dequeue vc=%08p req=0x%08x",   \
                  vc, vc->smp.sendq_head));      \
    vc->smp.sendq_head = vc->smp.sendq_head->dev.next;            \
    if (vc->smp.sendq_head == NULL)                  \
    {                                   \
    vc->smp.sendq_tail = NULL;                   \
    }                                   \
}
                                                                                                                                               
#define MPIDI_CH3I_SMP_SendQ_head(vc) (vc->smp.sendq_head)
                                                                                                                                               
#define MPIDI_CH3I_SMP_SendQ_empty(vc) (vc->smp.sendq_head == NULL)

#define SMPI_MAX_NUMLOCALNODES 8

extern int SMP_INIT;
extern int SMP_ONLY;

enum {
    MRAIL_RNDV_NOT_COMPLETE,
    MRAIL_RNDV_NEARLY_COMPLETE
#ifdef _SMP_
    ,
    MRAIL_SMP_RNDV_NOT_START
#endif
};

extern int      smp_eagersize;
extern int      smpi_length_queue;

/* management informations */
struct smpi_var {
    void *mmap_ptr;
    unsigned int my_local_id;
    unsigned int num_local_nodes;
    short int only_one_device;  /* to see if all processes are on one physical node */

    unsigned int l2g_rank[SMPI_MAX_NUMLOCALNODES];
    int available_queue_length;
    int fd;
    /*
    struct smpi_send_fifo_req *send_fifo_head;
    struct smpi_send_fifo_req *send_fifo_tail;
    unsigned int send_fifo_queued;
    unsigned int *local_nodes; 
    int pending;
    */
};

extern struct smpi_var smpi;

int MPIDI_CH3I_SMP_write_progress(MPIDI_PG_t *pg);

int MPIDI_CH3I_SMP_read_progress(MPIDI_PG_t *pg);

int MPIDI_CH3I_SMP_init(MPIDI_PG_t *pg);

int MPIDI_CH3I_SMP_finalize(void);

int MPIDI_CH3I_SMP_writev(MPIDI_VC_t * vc, const MPID_IOV * iov,
                          const int n, int *num_bytes_ptr);

int MPIDI_CH3I_SMP_readv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
                         const int iovlen, int
                         *num_bytes_ptr);

int MPIDI_CH3I_SMP_pull_header(MPIDI_VC_t * vc,
                               MPIDI_CH3_Pkt_t ** pkt_head);
#endif

/********* End of OSU-MPI2 *************************/

#endif /* !defined(MPICH_MPIDI_CH3_IMPL_H_INCLUDED) */
