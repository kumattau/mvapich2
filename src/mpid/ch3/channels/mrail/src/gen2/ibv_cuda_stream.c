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

#include "mpidi_ch3_impl.h"
#include "mpiutil.h"
#include "rdma_impl.h"
#include "dreg.h"

#ifdef _ENABLE_CUDA_
void *cuda_stream_region;
cuda_stream_t *free_cuda_stream_list_head;
cuda_stream_t *busy_cuda_stream_list_head;
cuda_stream_t *busy_cuda_stream_list_tail;

int allocate_cuda_streams()
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
        result = cudaStreamCreate(&(curr->stream));
        if (result != cudaSuccess) {
            ibv_error_abort(GEN_EXIT_ERR, "Cuda Stream Creation failed \n");
        }
        curr->op_type = -1;
        curr = curr->next;
        curr->prev = NULL;
    }
    result = cudaStreamCreate(&(curr->stream));
    if (result != cudaSuccess) {
        ibv_error_abort(GEN_EXIT_ERR, "Cuda Stream Creation failed \n");
    }
    curr->next = curr->prev = NULL;
    busy_cuda_stream_list_head = NULL;
    return 0;
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

void process_cuda_stream_op(cuda_stream_t * stream)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *req = stream->req;
    MPIDI_VC_t *vc = (MPIDI_VC_t *) stream->vc;
    vbuf *cuda_vbuf = stream->cuda_vbuf;
    int displacement = stream->displacement;
    int is_finish = stream->is_finish;
    int is_pipeline = (!displacement && is_finish) ? 0 : 1;
    int size = stream->size;
    int rail = 0;
    vbuf *v;

    if (stream->op_type == SEND) {

        v = cuda_vbuf;
        v->sreq = req;

        req->mrail.pipeline_nm++;
        if (req->mrail.cuda_transfer_mode == CONT_DEVICE_TO_DEVICE) {
            MRAILI_RDMA_Put(vc, v,
                (char *) (v->buffer),
                v->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                (char *) (req->mrail.cuda_remote_addr[req->mrail.num_remote_cuda_done]),
                req->mrail.cuda_remote_rkey[req->mrail.num_remote_cuda_done]
                        [vc->mrail.rails[rail].hca_index], size, rail);

            MRAILI_RDMA_Put_finish_cuda(vc, req, rail, is_pipeline, is_finish,
                                        rdma_cuda_block_size * displacement);
            req->mrail.num_remote_cuda_done++;

        } else if (req->mrail.cuda_transfer_mode == CONT_DEVICE_TO_HOST) {
            MRAILI_RDMA_Put(vc, v,
                (char *) (v->buffer),
                v->region->mem_handle[vc->mrail.rails[rail].hca_index]->lkey,
                (char *) (req->mrail.remote_addr) + displacement * 
                rdma_cuda_block_size,
                req->mrail.rkey[vc->mrail.rails[rail].hca_index],
                size, rail);

            if (is_finish)
                MRAILI_RDMA_Put_finish_cuda(vc, req, rail, is_pipeline,
                                            is_finish,
                                            rdma_cuda_block_size *
                                            displacement);
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
        PRINT_DEBUG(DEBUG_CUDA_verbose > 2, "CUDA copy block: "
                    "cuda block offset:%d , block addr offset:%d buf:%p\n",
                    displacement, rdma_cuda_block_size * displacement, cuda_vbuf->buffer);

        release_cuda_vbuf(cuda_vbuf);
        if (stream->is_finish) {
            int complete = 0;
            MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(req);

            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, req, &complete);
            if (mpi_errno != MPI_SUCCESS) {
                ibv_error_abort(IBV_RETURN_ERR,
                        "MPIDI_CH3U_Handle_recv_req returned error");
            }
            MPIU_Assert(complete == TRUE);
            vc->ch.recv_active = NULL;
        }
    } else {
        ibv_error_abort(IBV_RETURN_ERR, "Invalid op type in stream");
    }
}

void progress_cuda_streams()
{
    cudaError_t result = cudaSuccess;
    cuda_stream_t *curr_stream = busy_cuda_stream_list_head;
    cuda_stream_t *next_stream;

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
                //break;
            } else {
                process_cuda_stream_op(curr_stream);
                next_stream = curr_stream->next;
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
                curr_stream->next = free_cuda_stream_list_head;
                free_cuda_stream_list_head = curr_stream;
                curr_stream = next_stream;
            }
        } else {
            curr_stream = curr_stream->next;
        }
    }
}


cuda_stream_t *get_cuda_stream()
{

    cuda_stream_t *curr_stream;

    if (NULL == free_cuda_stream_list_head) {
        return NULL;
    }
    curr_stream = free_cuda_stream_list_head;
    free_cuda_stream_list_head = free_cuda_stream_list_head->next;
    curr_stream->next = curr_stream->prev = NULL;

    if (NULL == busy_cuda_stream_list_head) {
        busy_cuda_stream_list_head = curr_stream;
        busy_cuda_stream_list_tail = curr_stream;
    } else {
        busy_cuda_stream_list_tail->next = curr_stream;
        curr_stream->prev = busy_cuda_stream_list_tail;
        busy_cuda_stream_list_tail = curr_stream;
    }
    curr_stream->is_query_done = 0;
    return curr_stream;

}

#endif /*_ENABLE_CUDA_ */
