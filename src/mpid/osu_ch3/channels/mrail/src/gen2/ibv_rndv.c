/* Copyright (c) 2002-2006, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "rdma_impl.h"
#include "vbuf.h"
#include "dreg.h"

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
    do {                                                          \
        int rank;                                                 \
        PMI_Get_rank(&rank);                                      \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
        fprintf(stderr, args);                                    \
    } while (0)
#else
#define DEBUG_PRINT(args...)
#endif

int MPIDI_CH3I_MRAIL_Prepare_rndv(MPIDI_VC_t * vc, MPID_Request * req)
{
    dreg_entry *reg_entry;
    DEBUG_PRINT
        ("[prepare cts] rput protocol, recv size %d, segsize %d, \
         io count %d, rreq ca %d\n",
         req->dev.recv_data_sz, req->dev.segment_size, req->dev.iov_count,
         req->dev.ca);

    req->mrail.protocol = VAPI_PROTOCOL_RPUT;
    /* Step 1: ready for user space (user buffer or pack) */
    if (1 == req->dev.iov_count && MPIDI_CH3_CA_COMPLETE == req->dev.ca) {
        req->mrail.rndv_buf = req->dev.iov[0].MPID_IOV_BUF;
        req->mrail.rndv_buf_sz = req->dev.iov[0].MPID_IOV_LEN;
        req->mrail.rndv_buf_alloc = 0;
    } else {
        req->mrail.rndv_buf_sz = req->dev.segment_size;
        req->mrail.rndv_buf = MPIU_Malloc(req->mrail.rndv_buf_sz);

        if (req->mrail.rndv_buf == NULL) {
            /* fall back to r3 if cannot allocate tmp buf */
            DEBUG_PRINT("[rndv sent] set info: cannot allocate space\n");
            req->mrail.protocol = VAPI_PROTOCOL_R3;
            req->mrail.rndv_buf_sz = 0;
        } else {
            req->mrail.rndv_buf_alloc = 1;
        }
    }
    req->mrail.rndv_buf_off = 0;

    /* Step 2: try register and decide the protocol */
    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol) {
        DEBUG_PRINT("[cts] size registered %d, addr %p\n",
                req->mrail.rndv_buf_sz, req->mrail.rndv_buf);
        reg_entry =
            dreg_register(req->mrail.rndv_buf, req->mrail.rndv_buf_sz);
        if (NULL == reg_entry) {
            req->mrail.protocol = VAPI_PROTOCOL_R3;
            if (1 == req->mrail.rndv_buf_alloc) {
                MPIU_Free(req->mrail.rndv_buf);
                req->mrail.rndv_buf_alloc = 0;
                req->mrail.rndv_buf_sz = 0;
                req->mrail.rndv_buf = NULL;
            }
            req->mrail.rndv_buf_alloc = 0;
            /*MRAILI_Prepost_R3(); */
        }
        DEBUG_PRINT("[prepare cts] register success\n");
    }

    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol) {
	req->mrail.completion_counter = 0;
	req->mrail.d_entry = reg_entry;
        return 1;
    } else
        return 0;
}

int MPIDI_CH3I_MRAIL_Prepare_rndv_transfer(MPID_Request * sreq, /* contains local info */
        MPIDI_CH3I_MRAILI_Rndv_info_t *rndv)
{
    int hca_index;

    if (rndv->protocol == VAPI_PROTOCOL_R3) {
        if (sreq->mrail.d_entry != NULL) {
            dreg_unregister(sreq->mrail.d_entry);
            sreq->mrail.d_entry = NULL;
        }
        if (1 == sreq->mrail.rndv_buf_alloc
                && NULL != sreq->mrail.rndv_buf) {
            MPIU_Free(sreq->mrail.rndv_buf);
            sreq->mrail.rndv_buf_alloc = 0;
            sreq->mrail.rndv_buf = NULL;
        }
        sreq->mrail.remote_addr = NULL;
        /* Initialize this completion counter to 0
         * required for even striping */
        sreq->mrail.completion_counter = 0;
        
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++)
            sreq->mrail.rkey[hca_index] = 0;
        sreq->mrail.protocol = VAPI_PROTOCOL_R3;
    } else {
        sreq->mrail.remote_addr = rndv->buf_addr;
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++)
            sreq->mrail.rkey[hca_index] = rndv->rkey[hca_index];

        DEBUG_PRINT("[add rndv list] addr %p, key %p\n",
                sreq->mrail.remote_addr,
                sreq->mrail.rkey[0]);
        if (1 == sreq->mrail.rndv_buf_alloc) {
            int mpi_errno = MPI_SUCCESS;
            int i;
            uintptr_t buf;

            buf = (uintptr_t) sreq->mrail.rndv_buf;
            for (i = 0; i < sreq->dev.iov_count; i++) {
                memcpy((void *) buf, sreq->dev.iov[i].MPID_IOV_BUF,
                        sreq->dev.iov[i].MPID_IOV_LEN);
                buf += sreq->dev.iov[i].MPID_IOV_LEN;
            }
            /* TODO: Following part is a workaround to deal with datatype with large number
             * of segments. We check if the datatype has finished loading and reload if not.
             * May be better interface with upper layer should be considered*/
            while (sreq->dev.ca != MPIDI_CH3_CA_COMPLETE) {
                sreq->dev.iov_count = MPID_IOV_LIMIT;
                mpi_errno =
                    MPIDI_CH3U_Request_load_send_iov(sreq,
                            sreq->dev.iov,
                            &sreq->dev.iov_count);
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno != MPI_SUCCESS) {
                    ibv_error_abort(IBV_STATUS_ERR, "Reload iov error");
                }
                for (i = 0; i < sreq->dev.iov_count; i++) {
                    memcpy((void *) buf, sreq->dev.iov[i].MPID_IOV_BUF,
                            sreq->dev.iov[i].MPID_IOV_LEN);
                    buf += sreq->dev.iov[i].MPID_IOV_LEN;
                }
            }
        }
    }
    return MPI_SUCCESS;
}

void MRAILI_RDMA_Put_finish(MPIDI_VC_t * vc, MPID_Request * sreq, int rail)
{
    MPIDI_CH3_Pkt_rput_finish_t rput_pkt;
    MPID_IOV iov;
    int n_iov = 1;
    int nb;
    int mpi_errno = MPI_SUCCESS;

    vbuf *buf;

    rput_pkt.type = MPIDI_CH3_PKT_RPUT_FINISH;
    rput_pkt.receiver_req_id = sreq->mrail.partner_id;
    iov.MPID_IOV_BUF = &rput_pkt;
    iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rput_finish_t);

    DEBUG_PRINT("Sending RPUT FINISH\n");

    {
        mpi_errno =
            MPIDI_CH3I_MRAILI_rput_complete(vc, &iov, n_iov, &nb, &buf, rail);
        if (mpi_errno != MPI_SUCCESS && mpi_errno != MPI_MRAIL_MSG_QUEUED) {
            ibv_error_abort(IBV_STATUS_ERR,
                    "Cannot send rput through send/recv path");
        }

    }
    buf->sreq = (void *) sreq;
    /* mark MPI send complete when VIA send completes */
    DEBUG_PRINT("VBUF ASSOCIATED: %p, %08x\n", buf, buf->desc.sr.wr_id);
}

void MPIDI_CH3I_MRAILI_Rendezvous_rput_push(MPIDI_VC_t * vc,
        MPID_Request * sreq)
{
    vbuf *v;
    int rail;
    int nbytes;

    if (sreq->mrail.rndv_buf_off != 0) {
        ibv_error_abort(GEN_ASSERT_ERR,
                "s->bytes_sent != 0 Rendezvous Push, %d",
                sreq->mrail.nearly_complete);
    }

    sreq->mrail.completion_counter = 0;
    if (sreq->mrail.rndv_buf_sz > 0) {
#ifdef DEBUG
        assert(sreq->mrail.d_entry != NULL);
        assert(sreq->mrail.remote_addr != NULL);
#endif
    }

    while (sreq->mrail.rndv_buf_off < sreq->mrail.rndv_buf_sz) {
        nbytes = sreq->mrail.rndv_buf_sz - sreq->mrail.rndv_buf_off;
        int inc;

        if (nbytes > MPIDI_CH3I_RDMA_Process.maxtransfersize) {
            nbytes = MPIDI_CH3I_RDMA_Process.maxtransfersize;
        }

        inc = nbytes / rdma_num_rails;

        DEBUG_PRINT("[buffer content]: %02x,%02x,%02x, offset %d, remote buf %p\n",
                ((char *) sreq->mrail.rndv_buf)[0],
                ((char *) sreq->mrail.rndv_buf)[1],
                ((char *) sreq->mrail.rndv_buf)[2],
                sreq->mrail.rndv_buf_off, sreq->mrail.remote_addr);
        for(rail = 0; rail < rdma_num_rails - 1; rail++) {
            v = get_vbuf();
            MRAILI_RDMA_Put(vc, v,
                    (char *) (sreq->mrail.rndv_buf) +
                    sreq->mrail.rndv_buf_off + rail * inc,
                    ((dreg_entry *)sreq->mrail.d_entry)->
                    memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (sreq->mrail.remote_addr) +
                    sreq->mrail.rndv_buf_off + rail * inc,
                    sreq->mrail.rkey[vc->mrail.rails[rail].hca_index], inc, rail);
            /* Send the finish message immediately after the data */  
        }
        v = get_vbuf();
        MRAILI_RDMA_Put(vc, v,
                (char *) (sreq->mrail.rndv_buf) +
                sreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                ((dreg_entry *)sreq->mrail.d_entry)->
                memhandle[vc->mrail.rails[rail].hca_index]->lkey,
                (char *) (sreq->mrail.remote_addr) +
                sreq->mrail.rndv_buf_off + inc * (rdma_num_rails - 1),
                sreq->mrail.rkey[vc->mrail.rails[rail].hca_index], 
                nbytes - (rdma_num_rails - 1) * inc, rail);

        /* Send the finish message immediately after the data */  
        sreq->mrail.rndv_buf_off += nbytes; 
    }       
#ifdef DEBUG
    assert(sreq->mrail.rndv_buf_off == sreq->mrail.rndv_buf_sz);
#endif

    for(rail = 0; rail < rdma_num_rails; rail++) { 
        MRAILI_RDMA_Put_finish(vc, sreq, rail);
    }
    sreq->mrail.nearly_complete = 1;
}

int MPIDI_CH3I_MRAIL_Finish_request(MPID_Request *rreq)
{
    rreq->mrail.completion_counter++;
    if(rreq->mrail.completion_counter < rdma_num_rails)
        return 0;

    return 1;
}
