/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licencing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3i_rdma_conf.h"

#if !defined(DISABLE_PTMALLOC)

#include "mem_hooks.h"
#include "dreg.h"
#include <stdio.h>
#include <unistd.h>

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

void mvapich2_mem_unhook(void *ptr, size_t size)
{
    if((size > 0) && 
            !mvapich2_minfo.is_mem_hook_finalized) {
        find_and_free_dregs_inside(ptr, size);
    }
}

int mvapich2_minit()
{
    int ret = 0;
    void *ptr_malloc = NULL;
    void *ptr_calloc = NULL;
    void *ptr_valloc = NULL;
    void *ptr_realloc = NULL;
    void *ptr_memalign = NULL;

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
    set_real_munmap_ptr();
#endif /* !defined(DISABLE_MUNMAP_HOOK) */

    return ret;
}

void mvapich2_mfin()
{
    mvapich2_minfo.is_mem_hook_finalized = 1;
}

#if !defined(DISABLE_MUNMAP_HOOK)
int mvapich2_munmap(void *buf, size_t len)
{
    if(!mvapich2_minfo.munmap) {
        set_real_munmap_ptr();
    }

    if(!mvapich2_minfo.is_mem_hook_finalized) {
        mvapich2_mem_unhook(buf, len);
    }

    return mvapich2_minfo.munmap(buf, len);
}

int munmap(void *buf, size_t len)
{
    return mvapich2_munmap(buf, len);
}
#endif /* !defined(DISABLE_MUNMAP_HOOK) */

#if !defined(DISABLE_TRAP_SBRK)
void *mvapich2_sbrk(int delta)
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
