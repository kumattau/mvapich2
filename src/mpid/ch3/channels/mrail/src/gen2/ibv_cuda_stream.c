/* Copyright (c) 2001-2013, The Ohio State University. All rights
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

#include "mpidi_ch3_impl.h"
#include "mpiutil.h"
#include "rdma_impl.h"
#include "dreg.h"

#ifdef _ENABLE_CUDA_
void *cuda_stream_region;
cuda_stream_t *free_cuda_stream_list_head = NULL;
cuda_stream_t *busy_cuda_stream_list_head = NULL;
cuda_stream_t *busy_cuda_stream_list_tail = NULL;
cudaStream_t  stream_d2h = 0, stream_h2d = 0, stream_kernel = 0;
cudaEvent_t cuda_nbstream_sync_event = 0;

void allocate_cuda_streams()
{
    int j;
    cudaError_t result;
    cuda_stream_t *curr;
    cuda_stream_region = (void *) MPIU_Malloc(sizeof(cuda_stream_t)
                                              * rdma_cuda_stream_count);
    free_cuda_stream_list_head = cuda_stream_region;
    curr = (cuda_stream_t *) cuda_stream_region;
    for (j = 1; j < rdma_cuda_stream_count; j++) {
        curr->next = (cuda_stream_t *) ((size_t) cuda_stream_region +
                                        j * sizeof(cuda_stream_t));
        if (rdma_cuda_nonblocking_streams) { 
            result = cudaStreamCreateWithFlags(&(curr->stream), cudaStreamNonBlocking);
        } else { 
            result = cudaStreamCreate(&(curr->stream));
        }
        if (result != cudaSuccess) {
            ibv_error_abort(GEN_EXIT_ERR, "Cuda Stream Creation failed \n");
        }
        curr->op_type = -1;
        curr->flags = CUDA_STREAM_FREE_POOL; 
        curr = curr->next;
        curr->prev = NULL;
    }
    if (rdma_cuda_nonblocking_streams) { 
        result = cudaStreamCreateWithFlags(&(curr->stream), cudaStreamNonBlocking);
    } else { 
        result = cudaStreamCreate(&(curr->stream));
    }
    if (result != cudaSuccess) {
        ibv_error_abort(GEN_EXIT_ERR, "Cuda Stream Creation failed \n");
    }
    curr->next = curr->prev = NULL;
    busy_cuda_stream_list_head = NULL;
}

void deallocate_cuda_streams()
{
    MPIU_Assert(busy_cuda_stream_list_head == NULL);
    while(free_cuda_stream_list_head) {
        cudaStreamDestroy(free_cuda_stream_list_head->stream);
        free_cuda_stream_list_head = free_cuda_stream_list_head->next;
    }
    if (cuda_stream_region != NULL) {
        MPIU_Free(cuda_stream_region);
        cuda_stream_region = NULL;
    }
}

int allocate_cuda_stream(cuda_stream_t **cuda_stream)
{
    cudaError_t result;
    *cuda_stream = (cuda_stream_t *) MPIU_Malloc(sizeof(cuda_stream_t));
    if (rdma_cuda_nonblocking_streams) { 
        result = cudaStreamCreateWithFlags(&((*cuda_stream)->stream), cudaStreamNonBlocking);
    } else {
        result = cudaStreamCreate(&((*cuda_stream)->stream));
    }
    if (result != cudaSuccess) {
        ibv_error_abort(GEN_EXIT_ERR, "Cuda Stream Creation failed \n");
    }
    (*cuda_stream)->next = (*cuda_stream)->prev = NULL;
    (*cuda_stream)->cuda_vbuf_head = (*cuda_stream)->cuda_vbuf_tail = NULL;
    (*cuda_stream)->flags = CUDA_STREAM_DEDICATED;
    return 0;
}

void deallocate_cuda_stream(cuda_stream_t **cuda_stream)
{
    cudaStreamDestroy((*cuda_stream)->stream);
    MPIU_Free(*cuda_stream);
    *cuda_stream = NULL;
}

void allocate_cuda_rndv_streams()
{
    cudaError_t result;
    if (rdma_cuda_nonblocking_streams) { 
        CUDA_CHECK(cudaStreamCreateWithFlags(&stream_d2h, cudaStreamNonBlocking));
        CUDA_CHECK(cudaStreamCreateWithFlags(&stream_h2d, cudaStreamNonBlocking));
        CUDA_CHECK(cudaStreamCreateWithFlags(&stream_kernel, cudaStreamNonBlocking));
        CUDA_CHECK(cudaEventCreateWithFlags(&cuda_nbstream_sync_event,
                  cudaEventDisableTiming));
    } else { 
        CUDA_CHECK(cudaStreamCreate(&stream_d2h));
        CUDA_CHECK(cudaStreamCreate(&stream_h2d));
        CUDA_CHECK(cudaStreamCreate(&stream_kernel));
    }
}

void deallocate_cuda_rndv_streams()
{
    if (stream_d2h) {
        cudaStreamDestroy(stream_d2h);
    }
    if (stream_h2d) {
        cudaStreamDestroy(stream_h2d);
    }
    if (stream_kernel) {
        cudaStreamDestroy(stream_kernel);
    }
    if (cuda_nbstream_sync_event) {
        cudaEventDestroy(cuda_nbstream_sync_event); 
    }
}

void process_cuda_stream_op(cuda_stream_t * stream)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req = stream->req;
    MPIDI_VC_t *vc = (MPIDI_VC_t *) stream->vc;
    vbuf *cuda_vbuf = stream->cuda_vbuf_head; 
    int displacement = stream->displacement;
    int is_finish = stream->is_finish;
    int is_pipeline = (!displacement && is_finish) ? 0 : 1;
    int size = stream->size;
    int rail, stripe_avg, stripe_size, offset;
    vbuf *v, *orig_vbuf = NULL;

    if (stream->op_type == SEND) {

        req->mrail.pipeline_nm++;
        is_finish = (req->mrail.pipeline_nm == req->mrail.num_cuda_blocks)? 1 : 0;

        rail = MRAILI_Send_select_rail(vc);
        
        if (req->mrail.cuda_transfer_mode == DEVICE_TO_DEVICE) {
            if (size <= rdma_large_msg_rail_sharing_threshold) {
                rail = MRAILI_Send_select_rail(vc);

                v = cuda_vbuf;
                v->sreq = req;

                MRAILI_RDMA_Put(vc, v,
                   (char *) (v->buffer),
                    v->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (req->mrail.cuda_remote_addr[req->mrail.num_remote_cuda_done]),
                    req->mrail.cuda_remote_rkey[req->mrail.num_remote_cuda_done]
                            [vc->mrail.rails[rail].hca_index], size, rail);
            } else {
                stripe_avg = size / rdma_num_rails;
                orig_vbuf = cuda_vbuf;

                for (rail = 0; rail < rdma_num_rails; rail++) {
                    offset = rail*stripe_avg;
                    stripe_size = (rail < rdma_num_rails-1) ? stripe_avg : (size - offset);

                    v = get_vbuf();
                    v->sreq = req;
                    v->orig_vbuf = orig_vbuf;

                    MRAILI_RDMA_Put(vc, v,
                        (char *) (orig_vbuf->buffer) + offset,
                        orig_vbuf->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                        (char *) (req->mrail.cuda_remote_addr[req->mrail.num_remote_cuda_done])
                        + offset,
                        req->mrail.cuda_remote_rkey[req->mrail.num_remote_cuda_done]
                                [vc->mrail.rails[rail].hca_index], stripe_size, rail);
                }
            }

            for(rail = 0; rail < rdma_num_rails; rail++) {
                MRAILI_RDMA_Put_finish_cuda(vc, req, rail, is_pipeline, is_finish,
                                        rdma_cuda_block_size * displacement);
            }
            req->mrail.num_remote_cuda_done++;
        } else if (req->mrail.cuda_transfer_mode == DEVICE_TO_HOST) {
            if (size <= rdma_large_msg_rail_sharing_threshold) {
                rail = MRAILI_Send_select_rail(vc);

                v = cuda_vbuf;
                v->sreq = req;

                MRAILI_RDMA_Put(vc, v,
                    (char *) (v->buffer),
                    v->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                    (char *) (req->mrail.remote_addr) + displacement *
                    rdma_cuda_block_size,
                    req->mrail.rkey[vc->mrail.rails[rail].hca_index],
                    size, rail);
            } else {
                stripe_avg = size / rdma_num_rails;
                orig_vbuf = cuda_vbuf;

                for (rail = 0; rail < rdma_num_rails; rail++) {
                    offset = rail*stripe_avg;
                    stripe_size = (rail < rdma_num_rails-1) ? stripe_avg : (size - offset);

                    v = get_vbuf();
                    v->sreq = req;
                    v->orig_vbuf = orig_vbuf;

                    MRAILI_RDMA_Put(vc, v,
                        (char *) (orig_vbuf->buffer) + offset,
                        orig_vbuf->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                        (char *) (req->mrail.remote_addr) + displacement *
                        rdma_cuda_block_size + offset,
                        req->mrail.rkey[vc->mrail.rails[rail].hca_index],
                        stripe_size, rail);
                }
            }

            if (is_finish) {
               for(rail = 0; rail < rdma_num_rails; rail++) {
                    MRAILI_RDMA_Put_finish_cuda(vc, req, rail, is_pipeline,
                                        is_finish,
                                        rdma_cuda_block_size *
                                        displacement);
               }
            }
        }

        PRINT_DEBUG(DEBUG_CUDA_verbose > 1, "RDMA write block: "
                    "send offset:%d  rem idx: %d is_fin:%d"
                    "addr offset:%d strm:%p\n", displacement,
                    req->mrail.num_remote_cuda_done, is_finish,
                    rdma_cuda_block_size * displacement, stream);

        req->mrail.num_remote_cuda_pending--;
        if (req->mrail.num_send_cuda_copy != req->mrail.num_cuda_blocks) {
            PUSH_FLOWLIST(vc);
        }

    } else if (stream->op_type == RECV) {

        vbuf *temp_buf;
        /* release all the vbufs in the recv stream */
        while(cuda_vbuf)
        {
            PRINT_DEBUG(DEBUG_CUDA_verbose > 2, "CUDA copy block: "
                "buf:%p\n", cuda_vbuf->buffer);
            temp_buf = cuda_vbuf->next;
            cuda_vbuf->next = NULL;
            release_cuda_vbuf(cuda_vbuf);
            cuda_vbuf = temp_buf;
        }
        stream->cuda_vbuf_head = stream->cuda_vbuf_tail = NULL; 
        
        if (stream->is_finish) {
            int complete = 0;
            MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(req);
            /* deallocate the stream if request is finished */
            if (req->mrail.cuda_stream) {
                deallocate_cuda_stream(&req->mrail.cuda_stream);
            }

            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);
            if (mpi_errno != MPI_SUCCESS) {
                ibv_error_abort(IBV_RETURN_ERR,
                        "MPIDI_CH3U_Handle_recv_req returned error");
            }
            MPIU_Assert(complete == TRUE);
            vc->ch.recv_active = NULL;
        }
#if defined(HAVE_CUDA_IPC)
    } else if (stream->op_type == RGET) {
        cudaError_t cudaerr = cudaSuccess;

        cudaerr = cudaEventRecord(req->mrail.ipc_event, stream->stream);
        if (cudaerr != cudaSuccess) {
           ibv_error_abort(IBV_STATUS_ERR,
                    "cudaEventRecord failed");
        }

        cudaerr = cudaStreamWaitEvent(0, req->mrail.ipc_event, 0);
        if (cudaerr != cudaSuccess) {
            ibv_error_abort(IBV_RETURN_ERR,"cudaStreamWaitEvent failed\n");
        }

        cudaerr = cudaEventDestroy(req->mrail.ipc_event);
        if (cudaerr != cudaSuccess) {
            ibv_error_abort(IBV_RETURN_ERR,"cudaEventDestroy failed\n");
        }
        
        if (req->mrail.cuda_reg) {
            cudaipc_deregister(req->mrail.cuda_reg);
            req->mrail.cuda_reg = NULL;
        }

        /* deallocate the stream if it is allocated */
        if (req->mrail.cuda_stream) {
            deallocate_cuda_stream(&req->mrail.cuda_stream);
        }

        MRAILI_RDMA_Get_finish(vc, req, 0);
    } else if (stream->op_type == CUDAIPC_SEND) {
        int complete;
        if (req->mrail.rndv_buf_alloc == 1 && 
                req->mrail.rndv_buf != NULL) { 
            /* a temporary host rndv buffer would have been allocaed only when the 
               sender buffers is noncontiguous and is in the host memory */
            MPIU_Assert(req->mrail.cuda_transfer_mode == HOST_TO_DEVICE);
            MPIU_Free_CUDA_HOST(req->mrail.rndv_buf);
            req->mrail.rndv_buf_alloc = 0;
            req->mrail.rndv_buf = NULL;
        }
        MPIDI_CH3U_Handle_send_req(vc, req, &complete);
        MPIU_Assert(complete == TRUE);
        if (stream->flags == CUDA_STREAM_DEDICATED) {
            deallocate_cuda_stream(&req->mrail.cuda_stream);
        }
    } else if (stream->op_type == CUDAIPC_RECV) {
        int complete;
        int mpi_errno = MPI_SUCCESS;
        mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);
        if (mpi_errno != MPI_SUCCESS) {
            ibv_error_abort(IBV_RETURN_ERR,
                    "MPIDI_CH3U_Handle_recv_req returned error");
        }
        MPIU_Assert(complete == TRUE);
        if (stream->flags == CUDA_STREAM_DEDICATED) {
            deallocate_cuda_stream(&req->mrail.cuda_stream);
        }
#endif
    } else { 
        ibv_error_abort(IBV_RETURN_ERR, "Invalid op type in stream");
    }
}

void progress_cuda_streams()
{
    cudaError_t result = cudaSuccess;
    cuda_stream_t *curr_stream = busy_cuda_stream_list_head;
    cuda_stream_t *next_stream;
    uint8_t stream_pool_flag;

    while (NULL != curr_stream) {
        if (!curr_stream->is_query_done) {
            result = cudaStreamQuery(curr_stream->stream);
        }
        if (cudaSuccess == result || curr_stream->is_query_done) {
            curr_stream->is_query_done = 1;
            if ((SEND == curr_stream->op_type) &&
                ((1 != ((MPID_Request *) curr_stream->req)->mrail.cts_received)
                || !((MPID_Request *) curr_stream->req)->mrail.
                num_remote_cuda_pending)) {
                curr_stream = curr_stream->next;
            } else {
                next_stream = curr_stream->next;

                /* detach finished stream from the busy list*/    
                if (curr_stream->prev) {
                    curr_stream->prev->next = curr_stream->next;
                } else {
                    busy_cuda_stream_list_head = curr_stream->next;
                }
                if (curr_stream->next) {
                    curr_stream->next->prev = curr_stream->prev;
                } else {
                    busy_cuda_stream_list_tail = curr_stream->prev;
                }

                curr_stream->is_query_done = 0;
                curr_stream->next = curr_stream->prev = NULL;
                stream_pool_flag = curr_stream->flags;

                /* process finished stream */
                process_cuda_stream_op(curr_stream);

                /* Dedicated stream will be deallocated after the 
                ** request is finished*/
                if (stream_pool_flag == CUDA_STREAM_FREE_POOL) {
                    curr_stream->next = free_cuda_stream_list_head;
                    free_cuda_stream_list_head = curr_stream;
                }
                curr_stream = next_stream;
            }
        } else {
            curr_stream = curr_stream->next;
        }
    }
}

cuda_stream_t *get_cuda_stream(int add_to_polling)
{

    cuda_stream_t *curr_stream;

    if (NULL == free_cuda_stream_list_head) {
        if (NULL != busy_cuda_stream_list_head) {
            return NULL;
        } else {
            /* allocate cuda stream region */
            allocate_cuda_streams();
        }
    }
    curr_stream = free_cuda_stream_list_head;
    free_cuda_stream_list_head = free_cuda_stream_list_head->next;
    curr_stream->next = curr_stream->prev = NULL;

    if (add_to_polling) {
        /* Enqueue stream to the busy list*/
        CUDA_LIST_ADD(curr_stream, 
            busy_cuda_stream_list_head, busy_cuda_stream_list_tail);
    }
    curr_stream->is_query_done = 0;
    return curr_stream;

}

#endif /*_ENABLE_CUDA_ */
