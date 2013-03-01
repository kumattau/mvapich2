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

#include "rdma_impl.h"
#include "ibv_param.h"
#include "mv2_utils.h"
#include "ibv_cuda_util.h"

#if defined(_ENABLE_CUDA_)
#define CUDA_DEBUG 0
#define INITIAL_PACKUNPACK_OPT -1
#define SUCCESS_PACKUNPACK_OPT 0
#define FAILURE_PACKUNPACK_OPT 1

static int cudaipc_init = 0;
static CUcontext mv2_cuda_context = NULL;

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

void vector_pack_cudabuf(void *buf, MPID_IOV *iov, int size)
{
    cudaError_t cerr = cudaSuccess;
    cerr = cudaMemcpy2D(buf,
                iov[0].MPID_IOV_LEN,
                iov[0].MPID_IOV_BUF,
                iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                iov[0].MPID_IOV_LEN,
                size / iov[0].MPID_IOV_LEN,
                cudaMemcpyDeviceToDevice);
    if (cerr != cudaSuccess) {
        PRINT_INFO(1,"Error in cudaMemcpy2D\n");
    }
    PRINT_DEBUG(CUDA_DEBUG, "cuda vector pack with cudaMemcpy2D\n");
}

void vector_unpack_cudabuf(void *buf, MPID_IOV *iov, int size)
{
    cudaError_t cerr = cudaSuccess;
    cerr = cudaMemcpy2D(iov[0].MPID_IOV_BUF,
                iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                buf,
                iov[0].MPID_IOV_LEN,
                iov[0].MPID_IOV_LEN,
                size / iov[0].MPID_IOV_LEN,
                cudaMemcpyDeviceToDevice);
    if (cerr != cudaSuccess) {
        PRINT_INFO(1,"Error in cudaMemcpy2D\n");
    }
    PRINT_DEBUG(CUDA_DEBUG, "cuda vector unpack with cudaMemcpy2D\n");
}


#if defined(USE_GPU_KERNEL)
int hindexed_pack_cudabuf(void *dst, MPID_IOV *iov, MPID_Datatype *dtp, int size) 
{
    int i, element_size;
    int struct_sz = sizeof(MPID_Datatype_contents);
    int types_sz  = dtp->contents->nr_types * sizeof(MPI_Datatype);
    int ints_sz   = dtp->contents->nr_ints  * sizeof(int);
    int align_sz = 8, epsilon;

    int subarray_struct_sz = sizeof(MPID_Datatype_contents);
    int subarray_types_sz;
    int *subarray_array_of_ints;

    int array_numb = dtp->contents->nr_aints;

    MPID_Datatype *old_dtp, *sub_dtp;
    MPI_Datatype *array_of_types, *sub_array_of_types;
    MPI_Aint *array_of_aints;
    MPI_Aint base_displs, array_displs[array_numb];

    MPI_Address(iov[0].MPID_IOV_BUF, &base_displs);

    if ((epsilon = struct_sz % align_sz)) {
        struct_sz += align_sz - epsilon;
    }

    if ((epsilon = types_sz % align_sz)) {
        types_sz += align_sz - epsilon;
    }

    if ((epsilon = ints_sz % align_sz)) {
        ints_sz += align_sz - epsilon;
    }

    array_of_aints = (MPI_Aint *) ((char *)dtp->contents + struct_sz + types_sz + ints_sz);
    array_of_types = (MPI_Datatype *) ((char *)dtp->contents + struct_sz);

    for (i = 0; i < dtp->contents->nr_types; i++) {
        if (HANDLE_GET_KIND(array_of_types[i]) != HANDLE_KIND_BUILTIN) {
            MPID_Datatype_get_ptr(array_of_types[i], old_dtp);
            if (old_dtp->contents->combiner != MPI_COMBINER_SUBARRAY
		|| old_dtp->contents->nr_ints != 11) {
                /* only handle subarray type and nr_ints must be 11 for 3 ndims*/
                break;
            } else { continue; }
        } else { return FAILURE_PACKUNPACK_OPT; }
    }

    if (i != dtp->contents->nr_types) { return FAILURE_PACKUNPACK_OPT; }

    void *src;

    if (HANDLE_GET_KIND(array_of_types[0]) != HANDLE_KIND_BUILTIN) {

        MPID_Datatype_get_ptr(array_of_types[0], old_dtp);

        if (old_dtp->contents->combiner == MPI_COMBINER_SUBARRAY) {
            subarray_types_sz  = old_dtp->contents->nr_types * sizeof(MPI_Datatype);
            if ((epsilon = subarray_struct_sz % align_sz)) {
                subarray_struct_sz += align_sz - epsilon;
            }
            if ((epsilon = subarray_types_sz % align_sz)) {
                subarray_types_sz += align_sz - epsilon;
            }

            sub_array_of_types = (MPI_Datatype *) ((char *)old_dtp->contents + subarray_struct_sz);
            element_size = old_dtp->element_size;
            if (HANDLE_GET_KIND(sub_array_of_types[0]) != HANDLE_KIND_BUILTIN) {
                int cont_types_sz;
                MPI_Datatype *cont_array_of_types;

                MPID_Datatype_get_ptr(sub_array_of_types[0], sub_dtp);

                if (sub_dtp->contents->combiner == MPI_COMBINER_CONTIGUOUS) {
                    cont_types_sz = sub_dtp->contents->nr_types * sizeof(MPI_Datatype);
                    if ((epsilon = cont_types_sz % align_sz)) {
                        cont_types_sz += align_sz - epsilon;
                    }
                 
                    cont_array_of_types = (MPI_Datatype *) ((char *)sub_dtp->contents + subarray_struct_sz);
                 
                    if (HANDLE_GET_KIND(cont_array_of_types[0]) == HANDLE_KIND_BUILTIN) {
                        element_size = sub_dtp->n_elements * MPID_Datatype_get_basic_size(cont_array_of_types[0]);
                    } else { return FAILURE_PACKUNPACK_OPT; }
                } else { return FAILURE_PACKUNPACK_OPT; }
            }

            subarray_array_of_ints = (int *) ((char *)old_dtp->contents + subarray_struct_sz + subarray_types_sz);

            int ndims = subarray_array_of_ints[0];
            int order = subarray_array_of_ints[10];

            if (ndims == 3 && order == MPI_ORDER_FORTRAN) {
                int array_of_sizes[ndims];
                int array_of_subsizes[ndims];
                int array_of_starts[ndims];

                array_of_sizes[0] = subarray_array_of_ints[1];
                array_of_sizes[1] = subarray_array_of_ints[2];
                array_of_sizes[2] = subarray_array_of_ints[3];
                array_of_subsizes[0] = subarray_array_of_ints[4];
                array_of_subsizes[1] = subarray_array_of_ints[5];
                array_of_subsizes[2] = subarray_array_of_ints[6];
                array_of_starts[0] = subarray_array_of_ints[7];
                array_of_starts[1] = subarray_array_of_ints[8];
                array_of_starts[2] = subarray_array_of_ints[9];

                base_displs -= (array_of_starts[0] + array_of_sizes[0] * array_of_starts[1] + array_of_sizes[0] * array_of_sizes[1] * array_of_starts[2]) 
					* element_size;

                for (i = 0; i < array_numb; i++) {
                    
                    array_displs[i] = base_displs + array_of_aints[i];
                    src = (void *)array_displs[i];
      
                    if (element_size == 1 || element_size == 4 || element_size == 8) {
                        pack_subarray(dst, src, array_of_sizes[0], array_of_sizes[1], array_of_sizes[2], 
    			    			array_of_subsizes[0], array_of_subsizes[1], array_of_subsizes[2], 
    			    			array_of_starts[0], array_of_starts[1], array_of_starts[2], element_size);
                        dst += array_of_subsizes[0] * array_of_subsizes[1] * array_of_subsizes[2] * element_size;
                    } else {
                        return FAILURE_PACKUNPACK_OPT;
                    }
                }
            } else {
                return FAILURE_PACKUNPACK_OPT;
            }
            
        } else { return FAILURE_PACKUNPACK_OPT; }
    } else { return FAILURE_PACKUNPACK_OPT; }
    return SUCCESS_PACKUNPACK_OPT;

}

int hindexed_unpack_cudabuf(void *src, MPID_IOV *iov, MPID_Datatype *dtp, int size) 
{
    int i, element_size;
    int struct_sz = sizeof(MPID_Datatype_contents);
    int types_sz  = dtp->contents->nr_types * sizeof(MPI_Datatype);
    int ints_sz   = dtp->contents->nr_ints  * sizeof(int);
    int align_sz = 8, epsilon;

    int subarray_struct_sz = sizeof(MPID_Datatype_contents);
    int subarray_types_sz;
    int *subarray_array_of_ints;

    int array_numb = dtp->contents->nr_aints;

    MPID_Datatype *old_dtp, *sub_dtp;
    MPI_Datatype *array_of_types, *sub_array_of_types;
    MPI_Aint *array_of_aints;
    MPI_Aint base_displs, array_displs[array_numb];

    MPI_Address(iov[0].MPID_IOV_BUF, &base_displs);

    if ((epsilon = struct_sz % align_sz)) {
        struct_sz += align_sz - epsilon;
    }

    if ((epsilon = types_sz % align_sz)) {
        types_sz += align_sz - epsilon;
    }

    if ((epsilon = ints_sz % align_sz)) {
        ints_sz += align_sz - epsilon;
    }

    array_of_aints = (MPI_Aint *) ((char *)dtp->contents + struct_sz + types_sz + ints_sz);
    array_of_types = (MPI_Datatype *) ((char *)dtp->contents + struct_sz);

    for (i = 0; i < dtp->contents->nr_types; i++) {
        if (HANDLE_GET_KIND(array_of_types[i]) != HANDLE_KIND_BUILTIN) {
            MPID_Datatype_get_ptr(array_of_types[i], old_dtp);
            if (old_dtp->contents->combiner != MPI_COMBINER_SUBARRAY
		|| old_dtp->contents->nr_ints != 11) {
                /* only handle subarray type and nr_ints must be 11 for 3 ndims*/
                break;
            } else { continue; }
        } else { return FAILURE_PACKUNPACK_OPT; }
    }

    if (i != dtp->contents->nr_types) { return FAILURE_PACKUNPACK_OPT; }

    void *dst;

    if (HANDLE_GET_KIND(array_of_types[0]) != HANDLE_KIND_BUILTIN) {

        MPID_Datatype_get_ptr(array_of_types[0], old_dtp);

        if (old_dtp->contents->combiner == MPI_COMBINER_SUBARRAY) {

            subarray_types_sz  = old_dtp->contents->nr_types * sizeof(MPI_Datatype);
            if ((epsilon = subarray_struct_sz % align_sz)) {
                subarray_struct_sz += align_sz - epsilon;
            }
            if ((epsilon = subarray_types_sz % align_sz)) {
                subarray_types_sz += align_sz - epsilon;
            }

            sub_array_of_types = (MPI_Datatype *) ((char *)old_dtp->contents + subarray_struct_sz);
            element_size = old_dtp->element_size;
            if (HANDLE_GET_KIND(sub_array_of_types[0]) != HANDLE_KIND_BUILTIN) {
                int cont_types_sz;
                MPI_Datatype *cont_array_of_types;

                MPID_Datatype_get_ptr(sub_array_of_types[0], sub_dtp);

                if (sub_dtp->contents->combiner == MPI_COMBINER_CONTIGUOUS) {
                    cont_types_sz = sub_dtp->contents->nr_types * sizeof(MPI_Datatype);
                    if ((epsilon = cont_types_sz % align_sz)) {
                        cont_types_sz += align_sz - epsilon;
                    }

                    cont_array_of_types = (MPI_Datatype *) ((char *)sub_dtp->contents + subarray_struct_sz);

                    if (HANDLE_GET_KIND(cont_array_of_types[0]) == HANDLE_KIND_BUILTIN) {
                        element_size = sub_dtp->n_elements * MPID_Datatype_get_basic_size(cont_array_of_types[0]);
                    } else { return FAILURE_PACKUNPACK_OPT; }
                } else { return FAILURE_PACKUNPACK_OPT; }
            }

            subarray_array_of_ints = (int *) ((char *)old_dtp->contents + subarray_struct_sz + subarray_types_sz);

            int ndims = subarray_array_of_ints[0];
            int order = subarray_array_of_ints[10];

            if (ndims == 3 && order == MPI_ORDER_FORTRAN) {
                int array_of_sizes[ndims];
                int array_of_subsizes[ndims];
                int array_of_starts[ndims];

                array_of_sizes[0] = subarray_array_of_ints[1];
                array_of_sizes[1] = subarray_array_of_ints[2];
                array_of_sizes[2] = subarray_array_of_ints[3];
                array_of_subsizes[0] = subarray_array_of_ints[4];
                array_of_subsizes[1] = subarray_array_of_ints[5];
                array_of_subsizes[2] = subarray_array_of_ints[6];
                array_of_starts[0] = subarray_array_of_ints[7];
                array_of_starts[1] = subarray_array_of_ints[8];
                array_of_starts[2] = subarray_array_of_ints[9];

                base_displs -= (array_of_starts[0] + array_of_sizes[0] * array_of_starts[1] + array_of_sizes[0] * array_of_sizes[1] * array_of_starts[2]) 
					* element_size;

                for (i = 0; i < array_numb; i++) {

                    array_displs[i] = base_displs + array_of_aints[i];
                    dst = (void *)array_displs[i];
                    
                    if (element_size == 1 || element_size == 4 || element_size == 8) {
                        unpack_subarray(dst, src, array_of_sizes[0], array_of_sizes[1], array_of_sizes[2], 
    	            				array_of_subsizes[0], array_of_subsizes[1], array_of_subsizes[2], 
    	            				array_of_starts[0], array_of_starts[1], array_of_starts[2], element_size);
                        src += array_of_subsizes[0] * array_of_subsizes[1] * array_of_subsizes[2] * element_size;
                    } else {
                        return FAILURE_PACKUNPACK_OPT;
                    }
                }                                 

            } else {
                return FAILURE_PACKUNPACK_OPT;
            }
        } else { return FAILURE_PACKUNPACK_OPT; }
    } else { return FAILURE_PACKUNPACK_OPT; }
    return SUCCESS_PACKUNPACK_OPT;

}
#endif

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
            && (rdma_cuda_vector_dt_opt || rdma_cuda_kernel_dt_opt))  {
#if defined(USE_GPU_KERNEL)
            if (rdma_cuda_kernel_dt_opt) {
                pack_unpack_vector_kernel(req->dev.tmpbuf,
                                          iov[0].MPID_IOV_LEN,
                                          iov[0].MPID_IOV_BUF,
                                          iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                                          iov[0].MPID_IOV_LEN,
                                          req->dev.segment_size / iov[0].MPID_IOV_LEN);
            } else
#endif
            {
                vector_pack_cudabuf(req->dev.tmpbuf, iov, req->dev.segment_size);
            }
            last = req->dev.segment_size;
#if defined(USE_GPU_KERNEL)
        } else if (req->dev.datatype_ptr->contents->combiner == MPI_COMBINER_HINDEXED
                   && rdma_cuda_kernel_dt_opt) {

            int return_hindexed_pack = INITIAL_PACKUNPACK_OPT;
            return_hindexed_pack = hindexed_pack_cudabuf((void *)req->dev.tmpbuf, iov, req->dev.datatype_ptr, req->dev.segment_size);
       
            if (return_hindexed_pack == SUCCESS_PACKUNPACK_OPT) {
                last = req->dev.segment_size;
            } else if (return_hindexed_pack == FAILURE_PACKUNPACK_OPT) {
                MPIU_IOV_pack_cuda(req->dev.tmpbuf, iov, iov_n,
                    req->dev.segment_first);
            } else {
                MPIU_IOV_pack_cuda(req->dev.tmpbuf, iov, iov_n,
                    req->dev.segment_first);
            }
#endif
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
            && (rdma_cuda_vector_dt_opt || rdma_cuda_kernel_dt_opt))  {
#if defined(USE_GPU_KERNEL)
            if (rdma_cuda_kernel_dt_opt) {
                pack_unpack_vector_kernel(iov[0].MPID_IOV_BUF,
                                          iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                                          req->dev.tmpbuf,
                                          iov[0].MPID_IOV_LEN,
                                          iov[0].MPID_IOV_LEN,
                                          req->dev.segment_size / iov[0].MPID_IOV_LEN);
            } else
#endif
            {
                vector_unpack_cudabuf(req->dev.tmpbuf, iov, req->dev.segment_size);
            }
            last = bytes_copied = req->dev.segment_size;
#if defined(USE_GPU_KERNEL)
        } else if (req->dev.datatype_ptr->contents->combiner == MPI_COMBINER_HINDEXED
                   && rdma_cuda_kernel_dt_opt) {

            int return_hindexed_unpack = INITIAL_PACKUNPACK_OPT;
            return_hindexed_unpack = hindexed_unpack_cudabuf(req->dev.tmpbuf, iov, req->dev.datatype_ptr, req->dev.segment_size);

            if (return_hindexed_unpack == SUCCESS_PACKUNPACK_OPT) {
                last = bytes_copied = req->dev.segment_size;
            } else if (return_hindexed_unpack == FAILURE_PACKUNPACK_OPT) {
                MPIU_IOV_unpack_cuda(req->dev.tmpbuf, iov, iov_n,
                    req->dev.segment_first, &bytes_copied);
            } else {
                MPIU_IOV_unpack_cuda(req->dev.tmpbuf, iov, iov_n,
                    req->dev.segment_first, &bytes_copied);
            }
#endif
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
    int segment_size;
    int sbuf_isdev = 0;
    int sbuf_isdev_check = 0;

    /* allocate temp device pack buffer */
    if (!is_device_buffer(streambuf)) {
        MPIU_Malloc_CUDA(tmpbuf, *lastp);
        device_pack_buf = 0;
    } else {
        tmpbuf = streambuf;
    }

    segment_first = first;
    segment_size = *lastp - segment_first;
    do {
        segment_last = *lastp;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(segment_first < segment_last);
        MPIU_Assert(segment_last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_pack_vector(segp, segment_first, &segment_last,
                iov, &iov_n);

        if (!sbuf_isdev_check)  {
            if (is_device_buffer(iov[0].MPID_IOV_BUF)) {
                sbuf_isdev = 1;
            }
            sbuf_isdev_check = 1;
        }

        if (sbuf_isdev && dt_ptr->contents->combiner == MPI_COMBINER_VECTOR
            && (segp->builtin_loop.loop_params.count == 1)
            && (rdma_cuda_vector_dt_opt || rdma_cuda_kernel_dt_opt))  {
#if defined(USE_GPU_KERNEL)
            if (rdma_cuda_kernel_dt_opt) {
                pack_unpack_vector_kernel(tmpbuf,
                                          iov[0].MPID_IOV_LEN,
                                          iov[0].MPID_IOV_BUF,
                                          iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                                          iov[0].MPID_IOV_LEN,
                                          segment_size / iov[0].MPID_IOV_LEN);
            } else
#endif
            {
                vector_pack_cudabuf(tmpbuf, iov, segment_size);
            }
            segment_last = *lastp;
#if defined(USE_GPU_KERNEL)
        } else if (sbuf_isdev && dt_ptr->contents->combiner == MPI_COMBINER_HINDEXED
                   && rdma_cuda_kernel_dt_opt) {

            int return_hindexed_pack = INITIAL_PACKUNPACK_OPT;
            return_hindexed_pack = hindexed_pack_cudabuf(tmpbuf, iov, dt_ptr, segment_size);

            if (return_hindexed_pack == SUCCESS_PACKUNPACK_OPT) {
                segment_last = *lastp;
            } else if (return_hindexed_pack == FAILURE_PACKUNPACK_OPT) {
                MPIU_IOV_pack_cuda((char *)tmpbuf, iov, iov_n, buff_off);
            } else {
                MPIU_IOV_pack_cuda((char *)tmpbuf, iov, iov_n, buff_off);
            }
#endif
        } else {
            MPIU_IOV_pack_cuda((char *)tmpbuf, iov, iov_n, buff_off);
        }

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
    int segment_size;
    int rbuf_isdev = 0;
    int rbuf_isdev_check = 0;

    /* allocate temp device unpack buffer */
    if (!is_device_buffer(inbuf)) {
        device_unpack_buf = 0;
        MPIU_Malloc_CUDA(tmpbuf, *lastp);
        MPIU_Memcpy_CUDA(tmpbuf, inbuf, *lastp, cudaMemcpyHostToDevice);
    } else {
        tmpbuf = inbuf;
    }
    
    segment_first = first;
    segment_size = *lastp - segment_first;

    do {
        segment_last = *lastp;
        iov_n = MPID_IOV_LIMIT;
        MPIU_Assert(segment_first < segment_last);
        MPIU_Assert(segment_last > 0);
        MPIU_Assert(iov_n > 0 && iov_n <= MPID_IOV_LIMIT);

        MPID_Segment_unpack_vector(segp, segment_first, &segment_last,
                                     iov, &iov_n);

        if (!rbuf_isdev_check)  {
            if (is_device_buffer(iov[0].MPID_IOV_BUF)) {
                rbuf_isdev = 1;
            }
            rbuf_isdev_check = 1;
        }

        if (rbuf_isdev && dt_ptr->contents->combiner == MPI_COMBINER_VECTOR
            && (segp->builtin_loop.loop_params.count == 1)
            && (rdma_cuda_vector_dt_opt || rdma_cuda_kernel_dt_opt))  {
#if defined(USE_GPU_KERNEL)
            if (rdma_cuda_kernel_dt_opt) {
                pack_unpack_vector_kernel(iov[0].MPID_IOV_BUF,
                                          iov[1].MPID_IOV_BUF - iov[0].MPID_IOV_BUF,
                                          tmpbuf,
                                          iov[0].MPID_IOV_LEN,
                                          iov[0].MPID_IOV_LEN,
                                          segment_size / iov[0].MPID_IOV_LEN);
            } else
#endif
            {
                vector_unpack_cudabuf(tmpbuf, iov, segment_size);
            }
            segment_last = *lastp;
            bytes_unpacked = segment_last - segment_first;
#if defined(USE_GPU_KERNEL)
        } else if (rbuf_isdev && dt_ptr->contents->combiner == MPI_COMBINER_HINDEXED
                   && rdma_cuda_kernel_dt_opt) {

            int return_hindexed_unpack = INITIAL_PACKUNPACK_OPT;
            return_hindexed_unpack = hindexed_unpack_cudabuf(tmpbuf, iov, dt_ptr, segment_size);

            if (return_hindexed_unpack == SUCCESS_PACKUNPACK_OPT) {
                segment_last = *lastp;
                bytes_unpacked = segment_last - segment_first;
            } else if (return_hindexed_unpack == FAILURE_PACKUNPACK_OPT) {
                MPIU_IOV_unpack_cuda(tmpbuf, iov, iov_n, buff_off, &bytes_unpacked);
            } else {
                MPIU_IOV_unpack_cuda(tmpbuf, iov, iov_n, buff_off, &bytes_unpacked);
            }
#endif
        } else {
                MPIU_IOV_unpack_cuda(tmpbuf, iov, iov_n, buff_off, &bytes_unpacked);
        }

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
    cudaError_t cuda_err = cudaSuccess;
    struct cudaPointerAttributes attributes;
    CUresult cu_err = CUDA_SUCCESS;

    if (!rdma_enable_cuda  || buffer == NULL || buffer == MPI_IN_PLACE) {
        return 0;
    }

    cu_err = cuPointerGetAttribute(&memory_type, 
                    CU_POINTER_ATTRIBUTE_MEMORY_TYPE, 
                    (CUdeviceptr) buffer);
    if (cu_err != CUDA_SUCCESS) {
        if (rdma_check_cuda_attribute) {
            cuda_err = cudaPointerGetAttributes (&attributes, buffer);
            if (cuda_err == cudaSuccess) {
                return(attributes.memoryType == cudaMemoryTypeDevice);
            }
        }
        return 0;
    } else {
        return (memory_type == CU_MEMORYTYPE_DEVICE);
    }
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

void cuda_get_user_parameters() {
    char *value = NULL; 

    if ((value = getenv("MV2_EAGER_CUDAHOST_REG")) != NULL) {
        rdma_eager_cudahost_reg = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_VECTOR_OPT")) != NULL) {
        rdma_cuda_vector_dt_opt= atoi(value);
    }

#if defined(USE_GPU_KERNEL)
    if ((value = getenv("MV2_CUDA_KERNEL_OPT")) != NULL) {
        rdma_cuda_kernel_dt_opt= atoi(value);
    }
#else
    {
        rdma_cuda_kernel_dt_opt = 0;
    }
#endif

    if ((value = getenv("MV2_CUDA_NUM_STREAMS")) != NULL) {
        rdma_cuda_stream_count = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_NUM_EVENTS")) != NULL) {
        rdma_cuda_event_count = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_EVENT_SYNC")) != NULL) {
        rdma_cuda_event_sync = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_INIT_CONTEXT")) != NULL) {
        rdma_cuda_init_context = atoi(value);
    }

    if ((value = getenv("MV2_CHECK_CUDA_ATTRIBUTE")) != NULL) {
        rdma_check_cuda_attribute = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_USE_NAIVE")) != NULL) {
        rdma_cuda_use_naive = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLTOALL_DYNAMIC")) != NULL) {
        rdma_cuda_alltoall_dynamic = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_REGISTER_NAIVE_BUF")) != NULL) {
        rdma_cuda_register_naive_buf = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_GATHER_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_gather_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_SCATTER_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_scatter_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLTOALL_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_alltoall_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLTOALLV_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_alltoallv_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLGATHER_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_allgather_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLGATHERV_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_allgatherv_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_BCAST_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_bcast_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_GATHERV_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_gatherv_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_SCATTERV_NAIVE_LIMIT")) != NULL) {
        rdma_cuda_scatterv_naive_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLGATHER_RD_LIMIT")) != NULL) {
        rdma_cuda_allgather_rd_limit = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_ALLGATHER_FGP")) != NULL) {
        rdma_cuda_allgather_fgp = atoi(value);
    }


#if defined(HAVE_CUDA_IPC)
    if ((value = getenv("MV2_CUDA_IPC")) != NULL) {
        rdma_cuda_ipc = atoi(value);
    }

    if ((value = getenv("MV2_CUDA_SMP_IPC")) != NULL) {
        rdma_cuda_smp_ipc = atoi(value);
    }
    if (!rdma_cuda_ipc) { 
        rdma_cuda_smp_ipc = 0;
    }

    if (rdma_cuda_ipc
        && CUDART_VERSION < 4010) {
        PRINT_DEBUG(DEBUG_CUDA_verbose > 1, "IPC is availabe only"
            "from version 4.1 or later, version availabe : %d",
            CUDART_VERSION);
        rdma_cuda_ipc = 0;
        rdma_cuda_smp_ipc = 0;
    }

    if ((value = getenv("MV2_CUDA_ENABLE_IPC_CACHE")) != NULL) {
        rdma_cuda_enable_ipc_cache= atoi(value);
    }

    if ((value = getenv("MV2_CUDA_IPC_MAX_CACHE_ENTRIES")) != NULL) {
        cudaipc_cache_max_entries = atoi(value);
    }
    
    if ((value = getenv("MV2_CUDA_IPC_NUM_STAGE_BUFFERS")) != NULL) {
        cudaipc_num_stage_buffers = atoi(value);
    }
    if ((value = getenv("MV2_CUDA_IPC_STAGE_BUF_SIZE")) != NULL) {
        cudaipc_stage_buffer_size = atoi(value);
    }
    if ((value = getenv("MV2_CUDA_IPC_BUFFERED")) != NULL) {
        cudaipc_stage_buffered = atoi(value);
    }
    if (cudaipc_stage_buffered) {
        rdma_cuda_ipc_threshold = 0;
    }
    if ((value = getenv("MV2_CUDA_IPC_THRESHOLD")) != NULL) {
        rdma_cuda_ipc_threshold = user_val_to_bytes(value, "MV2_CUDA_IPC_THRESHOLD");
    }
    if ((value = getenv("MV2_CUDA_IPC_BUFFERED_LIMIT")) != NULL) {
        cudaipc_stage_buffered_limit = atoi(value);
    }
    if ((value = getenv("MV2_CUDA_IPC_SYNC_LIMIT")) != NULL) {
        cudaipc_sync_limit = atoi(value);
    }
    MPIU_Assert(cudaipc_stage_buffered_limit >= rdma_cuda_ipc_threshold);

#endif
}

void cuda_init(MPIDI_PG_t * pg)
{
#if defined(HAVE_CUDA_IPC)
    int mpi_errno = MPI_SUCCESS, errflag = 0;
    int i, num_processes, my_rank, has_cudaipc_peer = 0;
    int my_local_rank, dev_count, my_dev_id; 
    int *device = NULL;
    MPID_Comm *comm_world = NULL;
    MPIDI_VC_t* vc = NULL;
    cudaError_t cudaerr = cudaSuccess;
    CUresult curesult = CUDA_SUCCESS;
    CUcontext user_context;
    CUdevice cudevice; 
    
    comm_world = MPIR_Process.comm_world;
    num_processes = comm_world->local_size;
    my_rank = comm_world->rank;

    curesult = cuCtxGetCurrent(&user_context); 
    if (curesult != CUDA_SUCCESS || user_context == NULL) { 
        if (rdma_cuda_init_context) { 
            /*use has not selected a device or not created a context, 
             *select device internally*/
            my_local_rank = MPIDI_Process.my_pg->ch.local_process_id; 
            curesult = cuInit(0);
            if (curesult != CUDA_SUCCESS) {
                ibv_error_abort(GEN_EXIT_ERR,"cuInit failed\n");
            }
            cudaerr = cudaGetDeviceCount(&dev_count);
            if (cudaerr != cudaSuccess) { 
                ibv_error_abort(GEN_EXIT_ERR,"get device count failed \n");
            }
            my_dev_id = my_local_rank % dev_count;
            curesult = cuDeviceGet(&cudevice, my_dev_id);
            if (curesult != CUDA_SUCCESS) {
                ibv_error_abort(GEN_EXIT_ERR,"cuDeviceGet failed \n");
            }
            curesult = cuCtxCreate(&mv2_cuda_context, 0, cudevice);
            if (curesult != CUDA_SUCCESS) {
                ibv_error_abort(GEN_EXIT_ERR,"cuCtxCreate failed \n");
            }
        } else { 
            ibv_error_abort(GEN_EXIT_ERR,"Active CUDA context not detected.\
                 Select a device and create context before MPI_Init.\n");
        }
    }

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
            if (vc->smp.can_access_peer) {
                has_cudaipc_peer = 1;
            }
        }
    }
    
    cudaipc_num_local_procs =  MPIDI_Num_local_processes(pg);
    cudaipc_my_local_id = MPIDI_Get_local_process_id(pg);
        
    mpi_errno = MPIR_Allreduce_impl(&has_cudaipc_peer, &cudaipc_init, 1, 
                            MPI_INT, MPI_SUM, comm_world, &errflag);
    if(mpi_errno) {
        ibv_error_abort (GEN_EXIT_ERR, "MPIR_Allgather_impl returned error");
    }

    if (rdma_cuda_ipc && cudaipc_stage_buffered) {
        if (cudaipc_init) {
            cudaipc_initialize(pg, num_processes, my_rank);
        }
    }

    if (rdma_cuda_ipc && !cudaipc_stage_buffered 
            && rdma_cuda_enable_ipc_cache && has_cudaipc_peer) {
        cudaipc_initialize_cache();
    }
    MPIU_Free(device);
#endif

    if (SMP_INIT) {
        MPIDI_CH3I_CUDA_SMP_cuda_init(pg);
    }
}

void cuda_cleanup()
{
    CUresult curesult = CUDA_SUCCESS; 

    deallocate_cuda_events();
    deallocate_cuda_rndv_streams();
    deallocate_cuda_streams();

#if defined(HAVE_CUDA_IPC)
    if (rdma_cuda_ipc && cudaipc_stage_buffered && cudaipc_init) {
        cudaipc_finalize();
    }
    if (rdma_cuda_ipc && rdma_cuda_enable_ipc_cache && !cudaipc_stage_buffered) {
        int i;
        for (i = 0; i < cudaipc_num_local_procs; i++) {
            cudaipc_flush_regcache(i, num_cudaipc_cache_entries[i]);
        }
        MPIU_Free(cudaipc_cache_list);
        MPIU_Free(num_cudaipc_cache_entries);
    }
#endif

    if (SMP_INIT) {
        MPIDI_CH3I_CUDA_SMP_cuda_finalize(MPIDI_Process.my_pg);
    }

    if (mv2_cuda_context != NULL) {
        curesult = cuCtxDestroy(mv2_cuda_context);
        if (curesult != CUDA_SUCCESS) {
            ibv_error_abort (GEN_EXIT_ERR, "cuCtxDestroy returned error in finalize\n");
        }
    }
}
#endif
