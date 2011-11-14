
/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

#include "mpidi_ch3i_rdma_conf.h"
#include <mpimem.h>
#include "rdma_impl.h"
#include "ibv_impl.h"
#include "vbuf.h"
#include "dreg.h"


#ifdef _ENABLE_CUDA_

int MPIDI_CH3I_MRAIL_Prepare_rndv_cuda(MPIDI_VC_t * vc, MPID_Request * sreq,
                                       MPID_Request * rreq)
{
    MPID_Request *req = NULL;
    int i = 0;

    if (NULL == sreq) {
        req = rreq;
    } else {
        req = sreq;
    }

    DEBUG_PRINT
        ("[prepare cts] rput protocol, recv size %d, segsize %d, io count %d\n",
         req->dev.recv_data_sz, req->dev.segment_size, req->dev.iov_count);

    if (VAPI_PROTOCOL_RPUT == rdma_rndv_protocol) {
        req->mrail.protocol = VAPI_PROTOCOL_RPUT;
    } else {
        fprintf(stderr,
                "RGET and R3 are not supported for GPU to GPU transfer \n");
        fflush(stdout);
        MPIU_Assert(0);
    }

    /* Step 1: ready for user space (user buffer or pack) */
    if (1 == req->dev.iov_count && (req->dev.OnDataAvail == NULL ||
                                    (req->dev.OnDataAvail == req->dev.OnFinal)
                                    || (req->dev.OnDataAvail ==
                                        MPIDI_CH3_ReqHandler_UnpackSRBufComplete)))
    {

        req->mrail.rndv_buf = req->dev.iov[0].MPID_IOV_BUF;
        req->mrail.rndv_buf_sz = req->dev.iov[0].MPID_IOV_LEN;
        req->mrail.rndv_buf_alloc = 0;
    } else {

        req->mrail.rndv_buf_sz = req->dev.segment_size;
        req->mrail.rndv_buf = MPIU_Malloc(req->mrail.rndv_buf_sz);

        if (req->mrail.rndv_buf == NULL) {
            fprintf(stderr,
                    "RGET and R3 are not supported for GPU to GPU transfer \n");
            fflush(stdout);
            MPIU_Assert(0);
        } else {
            req->mrail.rndv_buf_alloc = 1;
        }

    }
    req->mrail.rndv_buf_off = 0;

    req->mrail.num_cuda_blocks = 
            ROUNDUP(req->mrail.rndv_buf_sz, rdma_cuda_block_size);
    for (i = 0; i < MIN(req->mrail.num_cuda_blocks, rdma_num_cuda_rndv_blocks);
         i++) {
        req->mrail.cuda_vbuf[i] = get_cuda_vbuf(CUDA_RNDV_BLOCK_BUF);
    }
    req->mrail.cuda_block_offset = 0;
    req->mrail.num_remote_cuda_pending =
        MIN(req->mrail.num_cuda_blocks, rdma_num_cuda_rndv_blocks);
    req->mrail.num_remote_cuda_done = 0;

    return 1;
}


void MPIDI_CH3I_MRAIL_Send_cuda_cts_conti(MPIDI_VC_t * vc, MPID_Request * req)
{

    MPIDI_CH3_Pkt_cuda_cts_cont_t *cts_pkt;
    vbuf *v;
    int i;

    MRAILI_Get_buffer(vc, v);
    cts_pkt = (MPIDI_CH3_Pkt_cuda_cts_cont_t *) v->buffer;
    MPIDI_Pkt_init(cts_pkt, MPIDI_CH3_PKT_CUDA_CTS_CONTI);
    cts_pkt->sender_req_id = req->dev.sender_req_id;
    cts_pkt->receiver_req_id = req->handle;

    for (i = 0;
         i < MIN((req->mrail.num_cuda_blocks - req->mrail.cuda_block_offset),
                 rdma_num_cuda_rndv_blocks); i++) {
        req->mrail.cuda_vbuf[i] = get_cuda_vbuf(CUDA_RNDV_BLOCK_BUF);
    }
    req->mrail.num_remote_cuda_pending =
        MIN((req->mrail.num_cuda_blocks - req->mrail.cuda_block_offset),
            rdma_num_cuda_rndv_blocks);
    MPIDI_CH3I_MRAIL_SET_PKT_RNDV_CUDA(cts_pkt, req);

    req->mrail.num_remote_cuda_done = 0;
    vbuf_init_send(v, sizeof(MPIDI_CH3_Pkt_cuda_cts_cont_t), 0);
    MPIDI_CH3I_RDMA_Process.post_send(vc, v, 0);

    PRINT_DEBUG(DEBUG_CUDA_verbose, "Send CTS conti : offset: %d"
                " no.of buffers:%d req:%p\n", req->mrail.cuda_block_offset,
                req->mrail.num_remote_cuda_pending, req);

}

void MPIDI_CH3_Rendezvous_cuda_cts_conti(MPIDI_VC_t * vc,
                     MPIDI_CH3_Pkt_cuda_cts_cont_t * cts_pkt)
{
    int i, hca_index;
    MPID_Request *sreq;
    MPID_Request_get_ptr(cts_pkt->sender_req_id, sreq);

    for (i = 0; i < cts_pkt->rndv.num_cuda_blocks; i++) {
        sreq->mrail.cuda_remote_addr[i] = cts_pkt->rndv.buffer_addr[i];
        for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
            sreq->mrail.cuda_remote_rkey[i][hca_index] =
                cts_pkt->rndv.buffer_rkey[i][hca_index];
        }
        sreq->mrail.num_remote_cuda_pending = cts_pkt->rndv.num_cuda_blocks;
        sreq->mrail.cuda_block_offset = cts_pkt->rndv.cuda_block_offset;
        sreq->mrail.num_remote_cuda_done = 0;
    }
    PRINT_DEBUG(DEBUG_CUDA_verbose, "received cuda CTS conti: sreq:%p\n", sreq);

    progress_cuda_streams();

    PUSH_FLOWLIST(vc);

}

void MRAILI_RDMA_Put_finish_cuda(MPIDI_VC_t * vc,
                                 MPID_Request * sreq, int rail,
                                 int is_cuda_pipeline, int cuda_pipeline_finish,
                                 int offset)
{
    MPIDI_CH3_Pkt_rput_finish_t rput_pkt;
    MPID_IOV iov;
    int n_iov = 1;
    int nb;
    vbuf *buf;
    MPID_Seqnum_t seqnum;

    MPIDI_Pkt_init(&rput_pkt, MPIDI_CH3_PKT_RPUT_FINISH);
    rput_pkt.receiver_req_id = sreq->mrail.partner_id;
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(&rput_pkt, seqnum);

    rput_pkt.is_cuda = 1;
    rput_pkt.is_cuda_pipeline = is_cuda_pipeline;
    rput_pkt.cuda_pipeline_finish = cuda_pipeline_finish;
    rput_pkt.cuda_offset = offset;

    iov.MPID_IOV_BUF = &rput_pkt;
    iov.MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_rput_finish_t);

    DEBUG_PRINT("Sending RPUT FINISH\n");

    int rc = MPIDI_CH3I_MRAILI_rput_complete(vc, &iov, n_iov, &nb, &buf, rail);
    buf->cuda_pipeline_finish = cuda_pipeline_finish;

    if (rc != 0 && rc != MPI_MRAIL_MSG_QUEUED) {
        ibv_error_abort(IBV_STATUS_ERR,
                        "Cannot send rput through send/recv path");
    }

    buf->sreq = (void *) sreq;

    /* mark MPI send complete when VIA send completes */

    DEBUG_PRINT("VBUF ASSOCIATED: %p, %08x\n", buf, buf->desc.u.sr.wr_id);
}

void MPIDI_CH3I_MRAILI_Rendezvous_rput_push_cuda(MPIDI_VC_t * vc,
        MPID_Request * sreq)
{
    int i = 0,rail;
    int nbytes, rail_index;
    uint32_t rkey = 0;
    char *remote_addr = NULL;
    cudaError_t cuda_error = cudaSuccess;
    vbuf *v;

    if (sreq->mrail.cuda_transfer_mode == CONT_DEVICE_TO_DEVICE 
        || sreq->mrail.cuda_transfer_mode == CONT_DEVICE_TO_HOST){

        sreq->mrail.num_cuda_blocks = 
            ROUNDUP(sreq->mrail.rndv_buf_sz, rdma_cuda_block_size);

        vbuf *cuda_vbuf;
        cuda_stream_t *cuda_stream;

        if (sreq->mrail.num_cuda_blocks == 1) {
            sreq->mrail.pipeline_nm++;   
            cuda_vbuf = get_cuda_vbuf(CUDA_RNDV_BLOCK_BUF);
            nbytes = sreq->mrail.rndv_buf_sz;

            cuda_error = cudaMemcpy(cuda_vbuf->buffer, 
                    (const void *)(sreq->dev.iov[0].MPID_IOV_BUF), nbytes,
                    cudaMemcpyDeviceToHost);
            if (cuda_error != cudaSuccess) {
                ibv_error_abort(IBV_RETURN_ERR,"cudaMemcpy to host failed\n");
            }

            rail = 0;
            rail_index = vc->mrail.rails[rail].hca_index;
            v = cuda_vbuf;
            v->sreq = sreq;

            if (sreq->mrail.cuda_transfer_mode == CONT_DEVICE_TO_DEVICE) {
                remote_addr = (char *)
                    sreq->mrail.cuda_remote_addr[0] + sreq->mrail.rndv_buf_off;
                rkey = sreq->mrail.cuda_remote_rkey[0][rail_index];
            } else if (sreq->mrail.cuda_transfer_mode == CONT_DEVICE_TO_HOST) {
                remote_addr = (char *)
                    sreq->mrail.remote_addr + sreq->mrail.rndv_buf_off;
                rkey = sreq->mrail.rkey[rail_index];
            }

            MRAILI_RDMA_Put(vc, v,
                    (char *) (v->buffer),
                    v->region->mem_handle[rail_index]->lkey,
                    remote_addr, rkey, nbytes, rail);

            MRAILI_RDMA_Put_finish_cuda(vc, sreq, rail, 0, 1, 0);
            sreq->mrail.num_send_cuda_copy++;
        } else {

            /* get cuda_vbuf and enqueue asynchronous stream copy*/
            i = sreq->mrail.num_send_cuda_copy;
            for (; i < sreq->mrail.num_cuda_blocks; i++) {
                cuda_stream = get_cuda_stream();
                if (cuda_stream == NULL) {
                    break;
                }
                nbytes = rdma_cuda_block_size;
                cuda_stream->is_finish = 0;
                if (i == (sreq->mrail.num_cuda_blocks - 1)) {
                    if (sreq->mrail.rndv_buf_sz % rdma_cuda_block_size) {
                        nbytes = sreq->mrail.rndv_buf_sz % rdma_cuda_block_size;
                    }
                    cuda_stream->is_finish = 1;
                }
                cuda_stream->op_type = SEND;
                cuda_stream->vc = vc;
                cuda_stream->req = sreq;
                cuda_stream->displacement = i;
                cuda_stream->size = nbytes; 
                cuda_stream->cuda_vbuf = get_cuda_vbuf(CUDA_RNDV_BLOCK_BUF);
                sreq->mrail.num_send_cuda_copy++;
                cuda_error = cudaMemcpyAsync(cuda_stream->cuda_vbuf->buffer, 
                        (const void *)(sreq->dev.iov[0].MPID_IOV_BUF + 
                            rdma_cuda_block_size * i), nbytes, 
                        cudaMemcpyDeviceToHost, cuda_stream->stream);
                if (cuda_error != cudaSuccess) {
                    ibv_error_abort(IBV_RETURN_ERR,"cudaMemcpyAsync to host failed\n");
                } 
                PRINT_DEBUG(DEBUG_CUDA_verbose>1, 
                    "Issue cudaMemcpyAsync :%d req:%p strm:%p\n",
                    sreq->mrail.num_send_cuda_copy, sreq, cuda_stream);
            }
        }
    } else if (rdma_enable_cuda 
            && sreq->mrail.cuda_transfer_mode == CONT_HOST_TO_DEVICE) {

        int i = 0, is_finish = 0, is_pipeline = 0;
        rail = 0;
        rail_index = vc->mrail.rails[rail].hca_index;
        sreq->mrail.num_cuda_blocks = 
            ROUNDUP(sreq->mrail.rndv_buf_sz, rdma_cuda_block_size);

        if (sreq->mrail.num_cuda_blocks > 1) {
            is_pipeline = 1;
        }

        i = sreq->mrail.num_send_cuda_copy;
        for (; i < sreq->mrail.num_cuda_blocks; i++) {

            if(sreq->mrail.num_remote_cuda_pending == 0) {
                PRINT_DEBUG(DEBUG_CUDA_verbose>1, "done:%d cuda_copy:%d "
                "num_blk:%d\n", sreq->mrail.num_remote_cuda_done, 
                sreq->mrail.num_send_cuda_copy, sreq->mrail.num_cuda_blocks);

                break;
            }
            sreq->mrail.pipeline_nm++;
            v = get_vbuf();
            nbytes = rdma_cuda_block_size;
            if (i == (sreq->mrail.num_cuda_blocks - 1)) {
                if (sreq->mrail.rndv_buf_sz % rdma_cuda_block_size) {
                    nbytes = sreq->mrail.rndv_buf_sz % rdma_cuda_block_size;
                }
                is_finish = 1;
            }

            MRAILI_RDMA_Put(vc, v,
                    (char *) (sreq->mrail.rndv_buf) +
                    sreq->mrail.rndv_buf_off + rdma_cuda_block_size * i,
                    ((dreg_entry *)sreq->mrail.d_entry)->memhandle[rail_index]->lkey,
                    (char *) (sreq->mrail.
                        cuda_remote_addr[sreq->mrail.num_remote_cuda_done]), 
                    sreq->mrail.
                    cuda_remote_rkey[sreq->mrail.num_remote_cuda_done][rail_index],
                    nbytes, rail);

            MRAILI_RDMA_Put_finish_cuda(vc, sreq, rail, is_pipeline, is_finish, 
                    rdma_cuda_block_size*i);

            sreq->mrail.num_remote_cuda_pending--;
            sreq->mrail.num_remote_cuda_done++;
            sreq->mrail.num_send_cuda_copy++;
        }
    }
    if (sreq->mrail.num_cuda_blocks == sreq->mrail.num_send_cuda_copy) {
        sreq->mrail.nearly_complete = 1;
    } else {
        sreq->mrail.nearly_complete = 0;
    }
}

/* return true if request if complete */
int MPIDI_CH3I_MRAILI_Process_cuda_finish(MPIDI_VC_t * vc, MPID_Request * rreq, 
                                    MPIDI_CH3_Pkt_rput_finish_t * rf_pkt)
{
    int nbytes;
    int rreq_complete = 0;
    cuda_stream_t *cuda_stream = NULL;
    cudaError_t cuda_error = cudaSuccess;

    progress_cuda_streams();
    
    if (rf_pkt->is_cuda && rf_pkt->is_cuda_pipeline) {
        rreq->mrail.pipeline_nm++;
        nbytes = rdma_cuda_block_size;
        if(rf_pkt->cuda_pipeline_finish) {
            if(rreq->mrail.rndv_buf_sz % rdma_cuda_block_size) {
                nbytes = rreq->mrail.rndv_buf_sz % rdma_cuda_block_size;
            }
        }
        //TODO: Handle NULL return
        cuda_stream = get_cuda_stream();
        if (cuda_stream == NULL) {

            PRINT_DEBUG(DEBUG_CUDA_verbose>1, "CudaMemCpy offset:%d "
            "is_fin:%d cuda_buf_idx:%d pending:%d rreq:%p\n", rf_pkt->cuda_offset,
            rf_pkt->cuda_pipeline_finish, rreq->mrail.num_remote_cuda_done, rreq->mrail.num_remote_cuda_pending, rreq);
            cuda_error = cudaMemcpy(rreq->mrail.rndv_buf + rf_pkt->cuda_offset,
                    (const void *)(rreq->mrail.cuda_vbuf
                        [rreq->mrail.num_remote_cuda_done]->buffer), 
                    nbytes, cudaMemcpyHostToDevice);
            if (cuda_error != cudaSuccess) {
                ibv_error_abort(IBV_RETURN_ERR,"cudaMemcpy to device failed\n");
            }
 
            release_cuda_vbuf(rreq->mrail.cuda_vbuf[rreq->mrail.num_remote_cuda_done++]);
            if (rf_pkt->cuda_pipeline_finish) {
                return 1;
            } else {
                rreq->mrail.num_remote_cuda_pending--;
                if (rreq->mrail.num_remote_cuda_pending == 0) {
                    rreq->mrail.cuda_block_offset += rdma_num_cuda_rndv_blocks;
                    MPIDI_CH3I_MRAIL_Send_cuda_cts_conti(vc, rreq);
                }
                return 0;
            }
        }

    
        cuda_stream->op_type = RECV;
        cuda_stream->is_finish = (rreq->mrail.pipeline_nm == 
                            rreq->mrail.num_cuda_blocks) ? 1 : 0;
        cuda_stream->displacement = rf_pkt->cuda_offset / rdma_cuda_block_size;
        cuda_stream->vc = vc;
        cuda_stream->req = rreq;
        cuda_stream->cuda_vbuf = 
            rreq->mrail.cuda_vbuf[rreq->mrail.num_remote_cuda_done];


        rreq->mrail.num_remote_cuda_done++;
        rreq->mrail.num_remote_cuda_pending--;
        if (rreq->mrail.num_remote_cuda_pending == 0 &&
            rreq->mrail.pipeline_nm != rreq->mrail.num_cuda_blocks) {
            rreq->mrail.cuda_block_offset += rdma_num_cuda_rndv_blocks;
            MPIDI_CH3I_MRAIL_Send_cuda_cts_conti(vc, rreq);
        }

        cuda_error = cudaMemcpyAsync(rreq->mrail.rndv_buf + rf_pkt->cuda_offset,
                (const void *)(cuda_stream->cuda_vbuf->buffer),
                nbytes, cudaMemcpyHostToDevice, cuda_stream->stream);
        if (cuda_error != cudaSuccess) {
            ibv_error_abort(IBV_RETURN_ERR,"cudaMemcpyAsync to device failed\n");
        }

        PRINT_DEBUG(DEBUG_CUDA_verbose>1, "RECV cudaMemcpyAsync :%d "
            "req:%p strm:%p\n", rreq->mrail.num_remote_cuda_done,
                                rreq, cuda_stream);
    } else {
        nbytes = rreq->mrail.rndv_buf_sz;
        cuda_error = cudaMemcpy(rreq->mrail.rndv_buf + rf_pkt->cuda_offset,
                (const void *)(rreq->mrail.cuda_vbuf[0]->buffer), nbytes,
                cudaMemcpyHostToDevice);
        if (cuda_error != cudaSuccess) {
            ibv_error_abort(IBV_RETURN_ERR,"cudaMemcpy Failed to device failed\n");
        }

        rreq->mrail.pipeline_nm++;
        release_cuda_vbuf(rreq->mrail.cuda_vbuf[0]);
        rreq_complete = 1;
    }
    return rreq_complete;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Prepare_rndv_cts_cuda
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Prepare_rndv_cts_cuda(MPIDI_VC_t * vc, 
        MPIDI_CH3_Pkt_rndv_clr_to_send_t * cts_pkt,
        MPID_Request * rreq)
{
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_PREPARE_RNDV_CTS_CUDA);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_PREPARE_RNDV_CTS_CUDA);

    cts_pkt->rndv.cuda_transfer_mode = CONT_DEVICE_TO_DEVICE;

#ifdef CKPT
    MPIDI_CH3I_CR_lock();
#endif

    switch (rreq->mrail.protocol) {
        case VAPI_PROTOCOL_RPUT:
            {
                MPIDI_CH3I_MRAIL_Prepare_rndv_cuda(vc, NULL, rreq);
                MPIDI_CH3I_MRAIL_SET_PKT_RNDV_CUDA(cts_pkt, rreq);
                PRINT_DEBUG(DEBUG_CUDA_verbose, "CTS: offset:%d "
                        " #blocks: %d rreq:%p\n", cts_pkt->rndv.cuda_block_offset, 
                        cts_pkt->rndv.num_cuda_blocks, rreq);

                MPIDI_CH3I_MRAIL_REVERT_RPUT(rreq);
                break;
            }
            break;
        default:
            {
                int rank;
                PMI_Get_rank(&rank);
                fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);
                fprintf(stderr,
                        "Unknown protocol %d type from rndv req to send\n",
                        rreq->mrail.protocol);
                mpi_errno = -1;
                MPIU_Assert(0);
                break;
            }
    }

#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_PREPARE_RNDV_CTS_CUDA);
    return mpi_errno;
}
#endif /* _ENABLE_CUDA_ */
