/* Copyright (c) 2001-2014, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include <mpidimpl.h>
#include <mpimem.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <search.h>
#include <stdint.h>

#ifdef USE_MEMORY_TRACING
#   define mpit_malloc(a, line, file)           \
        MPIU_trmalloc(a, line, file)
#   define mpit_calloc(a, b, line, file)        \
        MPIU_trcalloc(a, b, line, file)
#   define mpit_free(a, line, file)             \
        MPIU_trfree(a, line, file)
#   define mpit_strdup(a, line, file)           \
        MPIU_trstrdup(a, line, file)
#   define mpit_realloc(a, b, line, file)       \
        MPIU_trrealloc(a, b, line, file)
#   define mpit_memalign(a, b, c, line, file)   \
        posix_memalign(a, b, c)
#else
#   define mpit_malloc(a, line, file)           \
        malloc(a)
#   define mpit_calloc(a, b, line, file)        \
        calloc(a, b)
#   define mpit_free(a, line, file)             \
        free(a)
#   define mpit_strdup(a, line, file)           \
        strdup(a)
#   define mpit_realloc(a, b, line, file)       \
        realloc(a, b)
#   define mpit_memalign(a, b, c, line, file)   \
        posix_memalign(a, b, c)
#endif

MPIR_T_PVAR_ULONG2_LEVEL_DECL_STATIC(MV2, mem_allocated);
MPIR_T_PVAR_ULONG2_HIGHWATERMARK_DECL_STATIC(MV2, mem_allocated);

typedef struct {
    void * addr;
    size_t size;
} MPIT_MEMORY_T;

static void * oracle = NULL;

/*
 * This variable is used to count memory before MPIT is initialized
 */
static size_t unaccounted = 0;
static int initialized = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t oracle_mutex = PTHREAD_MUTEX_INITIALIZER;

static int
ptr_cmp (const void * mptr1, const void * mptr2)
{
    uintptr_t addr1 = (uintptr_t)((MPIT_MEMORY_T *)mptr1)->addr;
    uintptr_t addr2 = (uintptr_t)((MPIT_MEMORY_T *)mptr2)->addr;

    if (addr1 == addr2) {
        return 0;
    }

    return addr1 < addr2 ? -1 : 1;
}

#if 0
/* 
 * This function is used for debugging purposes only
 */
static void
ptr_print (const void * node, const VISIT which, const int depth)
{
    MPIT_MEMORY_T * data;
    int i = 0;

    switch (which) {
        case preorder:
            break;
        case postorder:
            data = *(MPIT_MEMORY_T **)node;
            for (i = 0; i < depth; i++) printf("*");
            printf("[%p: %ld]\n", data->addr, data->size);
            fflush(stdout);
            break;
        case endorder:
            break;
        case leaf:
            data = *(MPIT_MEMORY_T **)node;
            for (i = 0; i < depth; i++) printf("*");
            printf("[%p: %ld]\n", data->addr, data->size);
            fflush(stdout);
            break;
    }
}
#endif

static MPIT_MEMORY_T *
oracle_insert (void * ptr, size_t size)
{
    MPIT_MEMORY_T * mptr = mpit_malloc(sizeof (MPIT_MEMORY_T), __LINE__,
            __FILE__);
    void * result;

    pthread_mutex_lock(&oracle_mutex);

    if (mptr) {
        mptr->addr = ptr;
        mptr->size = size;
        result = tsearch(mptr, &oracle, ptr_cmp);
        mptr = result ? *(MPIT_MEMORY_T **)result : NULL;
        fflush(stdout);
    }

    pthread_mutex_unlock(&oracle_mutex);

    return mptr;
}

static MPIT_MEMORY_T *
oracle_find (void * ptr)
{
    MPIT_MEMORY_T m = { .addr = ptr };
    MPIT_MEMORY_T * mptr;
    void * result;

    pthread_mutex_lock(&oracle_mutex);
    result = tfind(&m, &oracle, ptr_cmp);
    mptr = result ? *(MPIT_MEMORY_T **)result : NULL;
    pthread_mutex_unlock(&oracle_mutex);

    return mptr;
}

static void
oracle_delete (MPIT_MEMORY_T * ptr)
{
    pthread_mutex_lock(&oracle_mutex);
    tdelete(ptr, &oracle, ptr_cmp);
    mpit_free(ptr, __LINE__, __FILE__);
    pthread_mutex_unlock(&oracle_mutex);
}

static inline void
increment_counter (signed long size)
{
    pthread_mutex_lock(&mutex);

    if (initialized) {
        MPIR_T_PVAR_LEVEL_INC(MV2, mem_allocated, size);
        MPIR_T_PVAR_ULONG2_HIGHWATERMARK_UPDATE(MV2, mem_allocated,
                PVAR_LEVEL_mem_allocated);
    }

    else {
        unaccounted += size;
    }

    pthread_mutex_unlock(&mutex);
}

void
MPIT_MEM_REGISTER_PVARS (void)
{
    MPIR_T_PVAR_LEVEL_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mem_allocated,
            0, /* initial value */
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Current level of allocated memory within the MPI library");
    MPIR_T_PVAR_HIGHWATERMARK_REGISTER_STATIC(
            MV2,
            MPI_UNSIGNED_LONG_LONG,
            mem_allocated,
            0, /* initial value */
            MPI_T_VERBOSITY_USER_BASIC,
            MPI_T_BIND_NO_OBJECT,
            (MPIR_T_PVAR_FLAG_READONLY | MPIR_T_PVAR_FLAG_CONTINUOUS),
            "CH3", /* category name */
            "Maximum level of memory ever allocated within the MPI library");

    pthread_mutex_lock(&mutex);
    initialized = 1;
    MPIR_T_PVAR_LEVEL_INC(MV2, mem_allocated, unaccounted);
    MPIR_T_PVAR_ULONG2_HIGHWATERMARK_UPDATE(MV2, mem_allocated,
            PVAR_LEVEL_mem_allocated);
    pthread_mutex_unlock(&mutex);
}

void *
MPIT_malloc (size_t size, int lineno, char const * filename)
{
    void * ptr = mpit_malloc(size, lineno, filename);

    if (ptr) {
        increment_counter(size);
        oracle_insert(ptr, size);
    }

    return ptr;
}

void *
MPIT_calloc (size_t nelements, size_t elementSize, int lineno, char const *
        filename)
{
    void * ptr = mpit_calloc(nelements, elementSize, lineno, filename);
    size_t size = nelements * elementSize;

    if (ptr) {
        increment_counter(size);
        oracle_insert(ptr, size);
    }

    return ptr;
}

int
MPIT_memalign (void ** ptr, size_t alignment, size_t size, int lineno, char
        const * filename)
{
    int rv = mpit_memalign(ptr, alignment, size, lineno, filename);

    if (!rv) {
        increment_counter(size);
        oracle_insert(*ptr, size);
    }

    return rv;
}

char *
MPIT_strdup (const char * s, int lineno, char const * filename)
{
    char * ptr = mpit_strdup(s, lineno, filename);
    size_t size = strlen(s);

    if (ptr) {
        increment_counter(size);
        oracle_insert(ptr, size);
    }

    return ptr;
}

void *
MPIT_realloc (void * ptr, size_t size, int lineno, char const * filename)
{
    if (ptr) {
        MPIT_MEMORY_T * mptr = oracle_find(ptr);
        size_t oldsize;

        MPIU_Assert(NULL != mptr);
        oldsize = mptr->size;

        ptr = mpit_realloc(ptr, size, lineno, filename);
        if (ptr) {
            oracle_delete(mptr);
            oracle_insert(ptr, size);
            increment_counter(size - oldsize);
        }

        else if (!size) {
            oracle_delete(mptr);
            increment_counter(size - oldsize);
        }
    }

    else {
        ptr = mpit_realloc(ptr, size, lineno, filename);
        if (ptr) {
            oracle_insert(ptr, size);
            increment_counter(size);
        }
    }

    return ptr;
}

void
MPIT_free (void * ptr, int lineno, char const * filename)
{
    size_t oldsize = 0;

    if (ptr) {
        MPIT_MEMORY_T * mptr = oracle_find(ptr);

        if (mptr) {
            oldsize = mptr->size;
            oracle_delete(mptr);
        }
    }

    mpit_free(ptr, lineno, filename);
    increment_counter(0 - oldsize);
}

void
MPIT_memalign_free (void * ptr, int lineno, char const * filename)
{
    size_t oldsize = 0;

    if (ptr) {
        MPIT_MEMORY_T * mptr = oracle_find(ptr);

        if (mptr) {
            oldsize = mptr->size;
            oracle_delete(mptr);
        }
    }

    Real_Free(ptr);
    increment_counter(0 - oldsize);
}
