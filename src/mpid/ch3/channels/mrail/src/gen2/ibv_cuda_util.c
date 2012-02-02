/* Copyright (c) 2003-2012, The Ohio State University. All rights
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

#include "rdma_impl.h"
#include "ibv_param.h"
#include "ibv_cuda_util.h"

#if defined(_ENABLE_CUDA_)
#define CUDA_DEBUG 0

void MPIU_IOV_pack_cuda(void *buf, MPID_IOV *iov, int n_iov, int position) 
{
    int i;
    void *ptr;
    ptr = (char *) buf + position;
    for (i = 0; i < n_iov; i++) {
        MPIU_Memcpy_CUDA(ptr, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN,
                                    cudaMemcpyDefault);
        ptr = (char *)ptr + iov[i].MPID_IOV_LEN;
    }
    PRINT_DEBUG(CUDA_DEBUG, "CUDA pack: buf:%p n_iov: %d\n", buf, n_iov);
}

void MPIU_IOV_unpack_cuda(void *buf, MPID_IOV *iov, int n_iov, 
                            int position, int *bytes_unpacked)
{
    int i = 0;
    void *ptr = buf + position;
    int total_len = 0;

    for (i = 0; i < n_iov; i++) {
        MPIU_Memcpy_CUDA(iov[i].MPID_IOV_BUF, ptr, iov[i].MPID_IOV_LEN,
                                cudaMemcpyDefault);
        ptr = (char *)ptr + iov[i].MPID_IOV_LEN;
        total_len += iov[i].MPID_IOV_LEN;
    }
    PRINT_DEBUG(CUDA_DEBUG, "CUDA unpack: buf:%p n_iov: %d total_len:%d \n", 
                        buf, n_iov, total_len);
    *bytes_unpacked = total_len;
}

void vector_pack_cudabuf(MPID_Request *req, MPID_IOV *iov)
{
    cudaError_t cerr = cudaSuccess;
    cerr = cudaMemcpy2D(req->dev.tmpbuf,
                iov[0].MPID_IOV_LEN,
                iov[0].MPID_IOV_BUF,
                iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                iov[0].MPID_IOV_LEN,
                req->dev.segment_size / iov[0].MPID_IOV_LEN,
                cudaMemcpyDeviceToDevice);
    if (cerr != cudaSuccess) {
        PRINT_INFO(1,"Error in cudaMemcpy2D\n");
    }
    PRINT_DEBUG(CUDA_DEBUG, "cuda vector pack with cudaMemcpy2D\n");
}

void vector_unpack_cudabuf(MPID_Request *req, MPID_IOV *iov)
{
    cudaError_t cerr = cudaSuccess;
    cerr = cudaMemcpy2D(iov[0].MPID_IOV_BUF,
                iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                req->dev.tmpbuf,
                iov[0].MPID_IOV_LEN,
                iov[0].MPID_IOV_LEN,
                req->dev.segment_size / iov[0].MPID_IOV_LEN,
                cudaMemcpyDeviceToDevice);
    if (cerr != cudaSuccess) {
        PRINT_INFO(1,"Error in cudaMemcpy2D\n");
    }
    PRINT_DEBUG(CUDA_DEBUG, "cuda vector unpack with cudaMemcpy2D\n");
}

int MPIDI_CH3_ReqHandler_pack_cudabuf(MPIDI_VC_t *vc ATTRIBUTE((unused)), 
                    MPID_Request *req, int *complete ATTRIBUTE((unused)))
{
    MPI_Aint last;
    int iov_n;
    MPID_IOV iov[MPID_IOV_LIMIT];

    req->dev.segment_first = 0;
    do {
        last = req->dev.segment_size;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(req->dev.segment_first < last);
        MPIU_Assert(last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_pack_vector(req->dev.segment_ptr, 
                req->dev.segment_first, &last, iov, &iov_n);
        if (req->dev.datatype_ptr->contents->combiner == MPI_COMBINER_VECTOR
            && (req->dev.segment_ptr->builtin_loop.loop_params.count == 1)
            && rdma_cuda_vector_dt_opt)  {
            /* cuda optimization for vector */
            vector_pack_cudabuf(req, iov);
            last = req->dev.segment_size;
        } else {
            MPIU_IOV_pack_cuda(req->dev.tmpbuf, iov, iov_n, 
                    req->dev.segment_first);
        }

        req->dev.segment_first = last;
        PRINT_INFO(CUDA_DEBUG, "paked :%d start:%lu last:%lu\n", iov_n, req->dev.segment_first, last);
    } while(last != req->dev.segment_size);
    return MPI_SUCCESS;
}

int MPIDI_CH3_ReqHandler_unpack_cudabuf(MPIDI_VC_t *vc ATTRIBUTE((unused)), MPID_Request *req, int *complete)
{
    MPI_Aint last;
    int iov_n, bytes_copied;
    MPID_IOV iov[MPID_IOV_LIMIT];

    req->dev.segment_first = 0;
    do {
        last = req->dev.segment_size;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(req->dev.segment_first < last);
        MPIU_Assert(last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_unpack_vector(req->dev.segment_ptr, 
                req->dev.segment_first, &last, iov, &iov_n);

        if (req->dev.datatype_ptr->contents->combiner == MPI_COMBINER_VECTOR
            && (req->dev.segment_ptr->builtin_loop.loop_params.count == 1)
            && rdma_cuda_vector_dt_opt)  {
            /* cuda optimization for vector */
            vector_unpack_cudabuf(req, iov);
            last = bytes_copied = req->dev.segment_size;
        } else {
            MPIU_IOV_unpack_cuda(req->dev.tmpbuf, iov, iov_n, 
                    req->dev.segment_first, &bytes_copied);
        }

        MPIU_Assert(bytes_copied == (last - req->dev.segment_first));
        req->dev.segment_first = last;
        PRINT_INFO(CUDA_DEBUG, "unpaked :%d start:%lu last:%lu\n", iov_n, req->dev.segment_first, last);
    } while(last != req->dev.segment_size);

    MPIDI_CH3U_Request_complete(req);
    *complete = TRUE;
    return MPI_SUCCESS;
}

void MPID_Segment_pack_cuda(DLOOP_Segment *segp, DLOOP_Offset first,
        DLOOP_Offset *lastp, MPID_Datatype *dt_ptr, void *streambuf)
{
    int iov_n;
    int device_pack_buf = 1;
    int buff_off = 0;
    void *tmpbuf = NULL;
    MPID_IOV iov[MPID_IOV_LIMIT];
    DLOOP_Offset segment_first, segment_last;

    /* allocate temp device pack buffer */
    if (!is_device_buffer(streambuf)) {
        MPIU_Malloc_CUDA(tmpbuf, *lastp);
        device_pack_buf = 0;
    } else {
        tmpbuf = streambuf;
    }

    segment_first = first;
    do {
        segment_last = *lastp;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(segment_first < segment_last);
        MPIU_Assert(segment_last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_pack_vector(segp, segment_first, &segment_last,
                iov, &iov_n);
        MPIU_IOV_pack_cuda((char *)tmpbuf, iov, iov_n, buff_off);
        buff_off += (segment_last - segment_first);
        segment_first = segment_last;
        
    } while (segment_last != *lastp);

    /* copy to device pack buffer to host pack buffer */
    if (!device_pack_buf) {
        MPIU_Memcpy_CUDA(streambuf, tmpbuf, *lastp, 
                                    cudaMemcpyDeviceToHost);
        MPIU_Free_CUDA(tmpbuf);
    }
}

void MPID_Segment_unpack_cuda(DLOOP_Segment *segp, DLOOP_Offset first,
        DLOOP_Offset *lastp, MPID_Datatype *dt_ptr, void *inbuf)
{
    int iov_n;
    int bytes_unpacked;
    int device_unpack_buf = 1;
    int buff_off = 0;
    void *tmpbuf;
    MPID_IOV iov[MPID_IOV_LIMIT];
    DLOOP_Offset segment_first, segment_last;

    /* allocate temp device unpack buffer */
    if (!is_device_buffer(inbuf)) {
        device_unpack_buf = 0;
        MPIU_Malloc_CUDA(tmpbuf, *lastp);
        MPIU_Memcpy_CUDA(tmpbuf, inbuf, *lastp, cudaMemcpyHostToDevice);
    } else {
        tmpbuf = inbuf;
    }
    
    segment_first = first;
    do {
        segment_last = *lastp;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(segment_first < segment_last);
        MPIU_Assert(segment_last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_unpack_vector(segp, segment_first, &segment_last,
                                     iov, &iov_n);
        MPIU_IOV_unpack_cuda(tmpbuf, iov, iov_n, buff_off, &bytes_unpacked);
        MPIU_Assert(bytes_unpacked == (segment_last - segment_first));
        segment_first = segment_last;
        buff_off += bytes_unpacked;
        
    } while (segment_last != *lastp);
    if (!device_unpack_buf) {
        MPIU_Free_CUDA(tmpbuf);
    }
}

int is_device_buffer(void *buffer) 
{
    int memory_type;
    cudaError_t cu_err;

    if (!rdma_enable_cuda) {
        return 0;
    }

    cu_err = cudaSuccess;
    cu_err = cuPointerGetAttribute(&memory_type, 
                    CU_POINTER_ATTRIBUTE_MEMORY_TYPE, 
                    (CUdeviceptr) buffer);
    if (cu_err != cudaSuccess) {
        return 0;
    }
    return (memory_type == CU_MEMORYTYPE_DEVICE);
}

void ibv_cuda_register(void *ptr, size_t size)
{
    cudaError_t cuerr = cudaSuccess;
    if(ptr == NULL) {
        return;
    }
    cuerr = cudaHostRegister(ptr, size, cudaHostRegisterPortable);
    if (cuerr != cudaSuccess) {
        ibv_error_abort(GEN_EXIT_ERR, "cudaHostRegister Failed");
    }
    PRINT_DEBUG(DEBUG_CUDA_verbose, 
            "cudaHostRegister success ptr:%p size:%lu\n", ptr, size);
}

void ibv_cuda_unregister(void *ptr)
{
    cudaError_t cuerr = cudaSuccess;
    if (ptr == NULL) {
        return;
    }
    cuerr = cudaHostUnregister(ptr);
    if (cuerr != cudaSuccess) {
        ibv_error_abort(GEN_EXIT_ERR,"cudaHostUnegister Failed");
    }
    PRINT_DEBUG(DEBUG_CUDA_verbose, "cudaHostUnregister success ptr:%p\n", ptr);
}

void cuda_init(MPIDI_PG_t * pg)
{
#if defined(HAVE_CUDA_IPC)
    int mpi_errno = MPI_SUCCESS, errflag = 0;
    int i, num_processes, num_local_processes, my_rank; 
    int *device = NULL;
    MPID_Comm *comm_world = NULL;
    MPIDI_VC_t* vc = NULL;
    cudaError_t cudaerr = cudaSuccess;

    comm_world = MPIR_Process.comm_world;
    num_processes = comm_world->local_size;
    my_rank = comm_world->rank;

    device = (int *) MPIU_Malloc (sizeof(int)*num_processes);
    if (device == NULL) {
        ibv_error_abort(GEN_EXIT_ERR,"memory allocation failed");
    }
    MPIU_Memset(device, 0, sizeof(int)*num_processes);

    cudaerr = cudaGetDevice(&device[my_rank]);
    if (cudaerr != cudaSuccess) { 
        ibv_error_abort(GEN_EXIT_ERR,"cudaGetDevice failed");
    }

    mpi_errno = MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, device,
               sizeof(int), MPI_BYTE, comm_world, &errflag);
    if (mpi_errno != MPI_SUCCESS) {
        ibv_error_abort (GEN_EXIT_ERR, "MPIR_Allgather_impl returned error");
    }    
 
    for (i=0; i<num_processes; i++) {
        if (i == my_rank) continue;
        MPIDI_Comm_get_vc(comm_world, i, &vc);
        vc->smp.can_access_peer = 0;
        if (vc->smp.local_rank != -1) {
            /*if both processes are using the same device, IPC works 
              but cudaDeviceCanAccessPeer returns 0, or
              else decide based on result of cudaDeviceCanAccessPeer*/
            if (device[my_rank] == device[i]) { 
                vc->smp.can_access_peer = 1;
            } else { 
                cudaerr = cudaDeviceCanAccessPeer(&vc->smp.can_access_peer, device[my_rank], device[i]);
                if (cudaerr != cudaSuccess) {
                    ibv_error_abort(GEN_EXIT_ERR,"cudaDeviceCanAccessPeer failed");
                }
            }
        }
    }

    num_local_processes =  MPIDI_Num_local_processes(pg);
    if (rdma_cuda_ipc && rdma_cuda_enable_ipc_cache) {
        cudaipc_initialize_cache(num_local_processes);
    }

#endif
}
void cuda_cleanup()
{
    if (free_cuda_event_list_head) {
        deallocate_cuda_events();
        deallocate_cuda_rndv_streams();
    }
    if (!rdma_cuda_event_sync) {
        deallocate_cuda_streams();
    }
#if defined(HAVE_CUDA_IPC)
    if (rdma_cuda_ipc && rdma_cuda_enable_ipc_cache) {
        int i;
        for (i = 0; i < num_cuda_local_procs; i++) {
            cudaipc_flush_regcache(i, num_cudaipc_cache_entries[i]);
        }
        MPIU_Free(cudaipc_cache_list);
        MPIU_Free(num_cudaipc_cache_entries);
    }
#endif
}
#endif
