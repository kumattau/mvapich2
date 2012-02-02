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
#if defined(HAVE_CUDA_IPC)
#define CUDA_IPC_DEBUG 0

#define CUDAIPC_SUCESS 0
#define CUDAIPC_OPENMEM_FAILED 1

#define CUDAIPC_CACHE_INVALID 0x01

cuda_regcache_entry_t **cudaipc_cache_list = NULL;
int *num_cudaipc_cache_entries = NULL;
int num_cuda_local_procs = 0;

static inline int cudaipc_openmemhandle(cuda_regcache_entry_t *reg)
{
    cudaError_t cudaerr = cudaSuccess;
    cudaIpcMemHandle_t memhandle;
    MPIU_Memcpy(&memhandle, reg->cuda_memHandle, sizeof(cudaIpcMemHandle_t));
    cudaerr = cudaIpcOpenMemHandle(&reg->remote_base, memhandle, 
                                        cudaIpcMemLazyEnablePeerAccess);
    if (cudaerr != cudaSuccess) {
        return CUDAIPC_OPENMEM_FAILED;
    } 

    return CUDAIPC_SUCESS;
}
static inline int cudaipc_closememhandle(cuda_regcache_entry_t *reg)
{
    cudaError_t cudaerr = cudaSuccess;
    cudaerr = cudaIpcCloseMemHandle(reg->remote_base);
    if (cudaerr != cudaSuccess) {
        ibv_error_abort(IBV_RETURN_ERR,"cudaIpcCloseMemHandle failed\n");
    }
    return CUDAIPC_SUCESS;
}

static inline int is_cudaipc_cachable(cuda_regcache_entry_t *reg)
{
    return !(reg->flags & CUDAIPC_CACHE_INVALID);
}


static inline void cudaipc_regcache_find (void *addr, int size, 
                            cuda_regcache_entry_t **reg, int rank)
{
    cuda_regcache_entry_t *temp = NULL;
    temp = cudaipc_cache_list[rank];
    *reg = NULL;
    while(temp) {
        if(!(((uint64_t)temp->addr + temp->size <= (uint64_t)addr) 
                || ((uint64_t)temp->addr >= (uint64_t)addr + size))) {
            *reg = temp;
            break;
        }
        temp = temp->next; 
    }
}

static inline void cudaipc_regcache_insert(cuda_regcache_entry_t *new_reg)
{
    int rank = new_reg->rank;
    if (num_cudaipc_cache_entries[rank] >= cudaipc_cache_max_entries) {
        /*flush a entry from the cache */
        cudaipc_flush_regcache(rank, 1);
    }
        
    num_cudaipc_cache_entries[rank]++;
    
    new_reg->next = new_reg->prev = NULL;
    if (cudaipc_cache_list[rank] == NULL) {
        cudaipc_cache_list[rank] = new_reg;
    } else {
        new_reg->next = cudaipc_cache_list[rank];
        cudaipc_cache_list[rank]->prev = new_reg;
        cudaipc_cache_list[rank] = new_reg;
    }
}

void cudaipc_regcache_delete(cuda_regcache_entry_t *reg)
{
    if (reg == NULL) {
        return;
    }
    int rank =  reg->rank;
    if (cudaipc_cache_list[rank] == NULL) {
        return;
    }

    if (cudaipc_cache_list[rank] == reg) {
        cudaipc_cache_list[rank] = reg->next;
    }

    if (reg->next) {
        reg->next->prev = reg->prev;
    }

    if (reg->prev) {
        reg->prev->next = reg->next;
    }
    num_cudaipc_cache_entries[rank]--;
}

void cudaipc_register(void *base_ptr, size_t size, int rank, 
        cudaIpcMemHandle_t memhandle, cuda_regcache_entry_t **cuda_reg)
{
    cuda_regcache_entry_t *reg = NULL, *new_reg = NULL;

start_find:
    if (rdma_cuda_enable_ipc_cache) {
        cudaipc_regcache_find(base_ptr, size, &reg, rank);
    }

    if (reg != NULL) {
        PRINT_DEBUG(CUDA_IPC_DEBUG, "cache hit base_addr:%p size:%ld rank:%d \n", 
                base_ptr, size, rank);
        /* validate if the cached memhandle is valid */
        if ((0 == memcmp(&memhandle, reg->cuda_memHandle,sizeof(cudaIpcMemHandle_t))) 
                    && size == reg->size && base_ptr == reg->addr) {
            /* valid cache */
            new_reg = reg;
        } else {
            /* remove cache entry */
            PRINT_DEBUG(CUDA_IPC_DEBUG, "Mismatch. Evict the entry: "
                    "old base: %p (old size:%lu, old memhandle:%lu) rank:%d\n ",
                    base_ptr, reg->size, reg->cuda_memHandle[0], rank);
            reg->refcount++;
            reg->flags |= CUDAIPC_CACHE_INVALID;
            cudaipc_deregister(reg);
            reg = NULL;
            goto start_find;
        }
    }

    if (reg == NULL) {
        new_reg = MPIU_Malloc(sizeof (cuda_regcache_entry_t));
        new_reg->addr = base_ptr;
        new_reg->size = size;
        new_reg->refcount = 0;
        new_reg->rank = rank;
        MPIU_Memcpy(new_reg->cuda_memHandle, &memhandle, sizeof(cudaIpcMemHandle_t));
        if (cudaipc_openmemhandle(new_reg) != CUDAIPC_SUCESS) {
            PRINT_DEBUG(CUDA_IPC_DEBUG,"cudaIpcOpenMemHandle failed\n");
            /* try flush open it after flushing all the entries */
            cudaipc_flush_regcache(rank, num_cudaipc_cache_entries[rank]);
            if (cudaipc_openmemhandle(new_reg) != CUDAIPC_SUCESS) {
                ibv_error_abort(IBV_RETURN_ERR,"cudaIpcOpenMemHandle failed\n");
            }
        }
            
        if (rdma_cuda_enable_ipc_cache) {
            cudaipc_regcache_insert(new_reg);
            PRINT_DEBUG(CUDA_IPC_DEBUG, "cahce miss. New entry added: base_ptr: "
                    "%p end:%p size:%lu, memhandle:%lu rank:%d\n", base_ptr, 
                     (char *)base_ptr + size, size, new_reg->cuda_memHandle[0], rank);
        }
    }
    new_reg->refcount++;
    *cuda_reg = new_reg;
}

void cudaipc_deregister(cuda_regcache_entry_t *reg)
{
    MPIU_Assert(reg->refcount > 0);
    reg->refcount--;
    if (reg->refcount > 0) {
        return;
    }
    if (!(rdma_cuda_enable_ipc_cache && is_cudaipc_cachable(reg))) {
        if (rdma_cuda_enable_ipc_cache) {
            cudaipc_regcache_delete(reg);
        }
        PRINT_DEBUG(CUDA_IPC_DEBUG, "deleted entry: base_ptr:%p size:%lu, "
                "memhandle:%lu rank:%d\n", reg->addr, reg->size, 
                reg->cuda_memHandle[0], reg->rank);
        cudaipc_closememhandle(reg);
        MPIU_Free(reg);    
    }
}

void cudaipc_flush_regcache(int rank, int count)
{
    cuda_regcache_entry_t *reg = NULL;
    int n = 0;
    while(cudaipc_cache_list[rank] && n < count) {
        reg = cudaipc_cache_list[rank];
        PRINT_DEBUG(CUDA_IPC_DEBUG, "Free entry: base_ptr:%p size:%lu, "
                "memhandle:%lu rank:%d\n", reg->addr, reg->size, 
                 reg->cuda_memHandle[0], rank);
        reg->refcount++;
        reg->flags |= CUDAIPC_CACHE_INVALID;
        cudaipc_deregister(reg);
        n++;
    }
}

void cudaipc_initialize_cache(int num_procs)
{
    int i;
    num_cuda_local_procs = num_procs;
    cudaipc_cache_list = (cuda_regcache_entry_t **) 
                MPIU_Malloc(num_procs * sizeof(cuda_regcache_entry_t *));
    num_cudaipc_cache_entries = (int *) MPIU_Malloc(num_procs * sizeof(int));
    for (i=0; i< num_procs; i++) {
        num_cudaipc_cache_entries[i] = 0;
        cudaipc_cache_list[i] = NULL;
    }
}
#endif
#endif 

