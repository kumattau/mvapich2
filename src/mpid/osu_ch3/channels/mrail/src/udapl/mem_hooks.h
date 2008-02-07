/* Copyright (c) 2003-2008, The Ohio State University. All rights
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

#ifndef _MEM_HOOKS_H
#define _MEM_HOOKS_H

#ifndef DISABLE_PTMALLOC

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ptmalloc2/malloc.h"
#include "ptmalloc2/sysdeps/pthread/malloc-machine.h"

typedef struct {
    int         is_our_malloc;
    int         is_our_free;
    int         is_our_calloc;
    int         is_our_realloc;
    int         is_our_valloc;
    int         is_our_memalign;
    int         is_inside_free;
    int         is_mem_hook_finalized;
#ifndef DISABLE_MUNMAP_HOOK
    int         (*munmap)(void*, size_t);
#endif
} mvapich2_malloc_info_t;

mvapich2_malloc_info_t mvapich2_minfo;

void mvapich2_mem_unhook(void *mem, size_t size);
int  mvapich2_minit(void);
void mvapich2_mfin(void);

#ifndef DISABLE_MUNMAP_HOOK
int mvapich2_munmap(void *buf, int len);
#endif

#ifndef DISABLE_TRAP_SBRK
void *mvapich2_sbrk(int delta);
#endif /* DISABLE_TRAP_SBRK */

#endif /* DISABLE_PTMALLOC */

#endif /* _MEM_HOOKS_H */
