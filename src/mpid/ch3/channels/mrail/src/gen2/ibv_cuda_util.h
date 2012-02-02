
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

#ifndef _IBV_CUDA_UTIL_H_
#define _IBV_CUDA_UTIL_H_

#if defined(_ENABLE_CUDA_)
#include "cuda.h"
#include "cuda_runtime.h"
#include "vbuf.h"


typedef enum cuda_async_op {
    SEND = 0,
    RECV,
    RGET,
} cuda_async_op_t;

typedef struct cuda_stream {
    cudaStream_t stream;
    cuda_async_op_t op_type;
    uint8_t flags;
    uint8_t is_finish;
    uint8_t is_query_done;
    uint32_t size;
    uint32_t displacement;
    void *vc;
    void *req;
    struct vbuf *cuda_vbuf_head, *cuda_vbuf_tail;
    struct cuda_stream *next, *prev;
} cuda_stream_t;

/* cuda strem flags */
#define CUDA_STREAM_FREE_POOL 0x01
#define CUDA_STREAM_DEDICATED 0x02

typedef struct cuda_event {
    cudaEvent_t event;
    cuda_async_op_t op_type;
    uint8_t is_finish;
    uint8_t is_query_done;
    uint32_t size;
    uint32_t displacement;
    void *vc;
    void *req;
    struct vbuf *cuda_vbuf_head, *cuda_vbuf_tail;
    struct cuda_event *next, *prev;
} cuda_event_t;

typedef struct cuda_event_region {
    void *list;
    void *next;
}cuda_event_region_t;

void allocate_cuda_events();   /* allocate event pool */
void deallocate_cuda_events(); /* deallocate event pool */
void progress_cuda_events();
cuda_event_t *get_cuda_event();
cuda_event_t *get_free_cuda_event();
void release_cuda_event(cuda_event_t *); 

int allocate_cuda_stream();  /* allocate single stream */
void deallocate_cuda_stream(); /* deallocate single stream */
void allocate_cuda_streams();   /* allocate stream pool */
void deallocate_cuda_streams(); /* deallocate stream pool */
void progress_cuda_streams();
cuda_stream_t *get_cuda_stream();
void allocate_cuda_rndv_streams();
void deallocate_cuda_rndv_streams();

extern void *cuda_stream_region;
extern cuda_stream_t *free_cuda_stream_list_head;
extern cuda_stream_t *busy_cuda_stream_list_head;
extern cuda_stream_t *busy_cuda_stream_list_tail;
extern cudaStream_t stream_d2h, stream_h2d;

extern cuda_event_t *free_cuda_event_list_head;

#define MV2_CUDA_PROGRESS()                     \
do {                                            \
    if (rdma_enable_cuda) {                     \
        if (rdma_cuda_event_sync) {             \
            progress_cuda_events();             \
        } else {                                \
            progress_cuda_streams();            \
        }                                       \
    }                                           \
} while(0)

#define MPIU_Malloc_CUDA(_buf, _size)           \
do {                                            \
    cudaError_t cuerr = cudaSuccess;            \
    cuerr = cudaMalloc((void **) &_buf,_size);  \
    if (cuerr != cudaSuccess) {                 \
        PRINT_INFO(1, "cudaMalloc failed\n");   \
        exit(-1);                               \
    }                                           \
}while(0)

#define MPIU_Free_CUDA(_buf)                    \
do {                                            \
    cudaError_t cuerr = cudaSuccess;            \
    cuerr = cudaFree(_buf);                     \
    if (cuerr != cudaSuccess) {                 \
        PRINT_INFO(1, "cudaFree failed\n");     \
        exit(-1);                               \
    }                                           \
}while(0)


#define MPIU_Memcpy_CUDA(_dst, _src, _size, _type)      \
do {                                                    \
    cudaError_t cuerr = cudaSuccess;                    \
    cuerr = cudaMemcpy(_dst, _src, _size, _type);       \
    if (cuerr != cudaSuccess) {                         \
        PRINT_INFO(1, "cudaMemcpy failed\n");           \
        exit(-1);                                       \
    }                                                   \
}while(0)

void ibv_cuda_register(void * ptr, size_t size);
void ibv_cuda_unregister(void *ptr);
#if defined(HAVE_CUDA_IPC)
typedef struct cuda_regcache_entry {
    uint8_t flags;
    void *remote_base;
    void *addr;
    size_t size;
    uint64_t cuda_memHandle[8];
    int refcount;
    int rank;
    struct cuda_regcache_entry *next;
    struct cuda_regcache_entry *prev;
} cuda_regcache_entry_t;
extern cuda_regcache_entry_t **cudaipc_cache_list;
extern int *num_cudaipc_cache_entries;
extern int num_cuda_local_procs;
void cudaipc_register(void *base_ptr, size_t size, int rank,  
        cudaIpcMemHandle_t memhandle, cuda_regcache_entry_t **cuda_reg);
void cudaipc_deregister(cuda_regcache_entry_t *reg);
void cudaipc_flush_regcache(int rank, int count);
void cudaipc_initialize_cache(int num_procs);
#endif
#endif
#endif /* _IBV_CUDA_UTIL_H_ */


