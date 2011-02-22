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

#ifndef NEMESIS_BUILD
#include "mpidi_ch3i_rdma_conf.h"
#else
#define _GNU_SOURCE 1
#endif

#if !defined(DISABLE_PTMALLOC)
#include "mem_hooks.h"
#include "ibv_param.h"
#include "dreg.h"
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

static int mem_hook_init = 0;
static pthread_t lock_holder = -1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int recurse_level = 0;
static int resolving_munmap = 0;
static void * store_buf = NULL;
static size_t store_len = 0;

#if !defined(DISABLE_MUNMAP_HOOK)
#include <dlfcn.h>

static void set_real_munmap_ptr()
{
    munmap_t munmap = (munmap_t) dlsym(RTLD_NEXT, "munmap");
    char* dlerror_str = dlerror();

    if(NULL != dlerror_str) {
        fprintf(stderr,"Error resolving munmap (%s)\n",
                dlerror_str);
    }       

    mvapich2_minfo.munmap = munmap;
}
#endif /* !defined(DISABLE_MUNMAP_HOOK) */

static int lock_hooks(void)
{
    int ret;

    if(pthread_self() == lock_holder) {
        recurse_level++;
        return 0;
    } else {
        if(0 != (ret = pthread_mutex_lock(&lock))) {
            perror("pthread_mutex_lock");
            return ret;
        }
        lock_holder = pthread_self();
        recurse_level++;
    }
    return 0;
}

static int unlock_hooks(void)
{
    int ret;
    if(pthread_self() != lock_holder) {
        return 1;
    } else {
        recurse_level--;
        if(0 == recurse_level) {
            lock_holder = -1;
            if(0 != (ret = pthread_mutex_unlock(&lock))) {
                perror("pthread_mutex_unlock");
                return ret;
            }
        }
    }
    return 0;
}

void mvapich2_mem_unhook(void *ptr, size_t size)
{
    if(mem_hook_init && 
            (size > 0) && 
            !mvapich2_minfo.is_mem_hook_finalized) {
        find_and_free_dregs_inside(ptr, size);
    }
}

int mvapich2_minit()
{
    void *ptr_malloc = NULL;
    void *ptr_calloc = NULL;
    void *ptr_valloc = NULL;
    void *ptr_realloc = NULL;
    void *ptr_memalign = NULL;

    assert(0 == mem_hook_init);

    if(lock_hooks()) {
        return 1;
    }

    memset(&mvapich2_minfo, 0, sizeof(mvapich2_malloc_info_t));

    ptr_malloc = malloc(64);
    ptr_calloc = calloc(64, 1);
    ptr_realloc = realloc(ptr_malloc, 64);
    ptr_valloc = valloc(64);
    ptr_memalign = memalign(64, 64);


    free(ptr_calloc);
    free(ptr_valloc);
    free(ptr_memalign);

    /* ptr_realloc already contains the
     * memory allocated by malloc */
    free(ptr_realloc);

    if(!(mvapich2_minfo.is_our_malloc &&
            mvapich2_minfo.is_our_calloc &&
            mvapich2_minfo.is_our_realloc &&
            mvapich2_minfo.is_our_valloc &&
            mvapich2_minfo.is_our_memalign &&
            mvapich2_minfo.is_our_free)) {
        return 1;
    }

#if !defined(DISABLE_MUNMAP_HOOK)
    dlerror(); /* Clear the error string */
    resolving_munmap = 1;
    set_real_munmap_ptr();
    resolving_munmap = 0;
    if(NULL != store_buf) {
        mvapich2_minfo.munmap(store_buf, store_len);
        store_buf = NULL; store_len = 0;
    }
#endif /* !defined(DISABLE_MUNMAP_HOOK) */

    mem_hook_init = 1;

    if(unlock_hooks()) {
        return 1;
    }

    return 0;
}

void mvapich2_mfin()
{
    assert(1 == mem_hook_init);
    mvapich2_minfo.is_mem_hook_finalized = 1;
}

#if !defined(DISABLE_MUNMAP_HOOK)
int mvapich2_munmap(void *buf, size_t len)
{
    if(lock_hooks()) {
        return 1;
    }

    if(!mvapich2_minfo.munmap &&
            !resolving_munmap) {
        resolving_munmap = 1;
        set_real_munmap_ptr();
        resolving_munmap = 0;
        if(NULL != store_buf) {
            /* resolved munmap ptr successfully,
             * but in the meantime successive
             * stack frame stored a ptr to
             * be really munmap'd. do it now. */

            /* addtional note: since munmap ptr
             * was not resolved, therefore we must
             * not have inited mem hooks, i.e.
             * assert(0 == mem_hook_init); therefore
             * this memory does not need to be unhooked
             * (since no MPI call has been issued yet) */

            mvapich2_minfo.munmap(store_buf, store_len);
            store_buf = NULL; store_len = 0;
        }
    }

    if(!mvapich2_minfo.munmap &&
            resolving_munmap) {
        /* prev stack frame is resolving
         * munmap ptr. 
         * store the ptr to be munmap'd
         * for now and return */
        store_buf = buf;
        store_len = len;
        return 0;
    }

    if(mem_hook_init &&
            !mvapich2_minfo.is_mem_hook_finalized) {
        mvapich2_mem_unhook(buf, len);
    }

    if(unlock_hooks()) {
        return 1;
    }

    return mvapich2_minfo.munmap(buf, len);
}

int munmap(void *buf, size_t len)
{
    return mvapich2_munmap(buf, len);
}
#endif /* !defined(DISABLE_MUNMAP_HOOK) */

#if !defined(DISABLE_TRAP_SBRK)
void *mvapich2_sbrk(intptr_t delta)
{
    if (delta < 0) {

        void *current_brk = sbrk(0);

        mvapich2_mem_unhook((void *)
                ((uintptr_t) current_brk + delta), -delta);

        /* -delta is actually a +ve number */
    }

    return sbrk(delta);
}
#endif /* !defined(DISABLE_TRAP_SBRK) */
#endif /* !defined(DISABLE_PTMALLOC) */
