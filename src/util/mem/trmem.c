/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2009 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
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

#include "mpiimpl.h"
#include "mpimem.h"

#if defined (_OSU_MVAPICH_) && OSU_MPIT

#define MPI_T_COOKIE_VALUE   0xf0e0d0c9

unsigned long long mpit_mem_allocated_current=0;
unsigned long long mpit_mem_allocated_max=0;
unsigned long long mpit_mem_allocated_min=UINT_MAX;

void *MPIT_malloc(size_t size) {

    void *ptr;

    ptr = malloc(sizeof(size_t) + sizeof(int) + size);
    *((size_t*) ptr) = size;
    ptr = (size_t*) ptr + 1;
    *((int*) ptr) = MPI_T_COOKIE_VALUE;
    ptr = (int*) ptr + 1;
    mpit_mem_allocated_current += size;
    if (mpit_mem_allocated_max < mpit_mem_allocated_current)
        mpit_mem_allocated_max = mpit_mem_allocated_current;
    if (mpit_mem_allocated_min > mpit_mem_allocated_current)
        mpit_mem_allocated_min = mpit_mem_allocated_current;

    return ptr;
}

void *MPIT_calloc(size_t nelements, size_t elementSize) {

    void *ptr;
    size_t size;

    size = nelements * elementSize;
    ptr = malloc(sizeof(size_t) + sizeof(int) + size);
    *((size_t*) ptr) = size;
    ptr = (size_t*) ptr + 1;
    *((int*) ptr) = MPI_T_COOKIE_VALUE;
    ptr = (int*) ptr + 1;
    memset(ptr, 0, size);
    mpit_mem_allocated_current += size;
    if (mpit_mem_allocated_max < mpit_mem_allocated_current)
        mpit_mem_allocated_max = mpit_mem_allocated_current;
    if (mpit_mem_allocated_min > mpit_mem_allocated_current)
        mpit_mem_allocated_min = mpit_mem_allocated_current;

    return ptr;
}

void *MPIT_realloc(void * ptr, size_t size) {

    void *newptr;

    if (ptr != NULL) {
        ptr = (int*) ptr - 1;
        if (*((int*) ptr) == MPI_T_COOKIE_VALUE) {
            ptr = (size_t*) ptr - 1;
            mpit_mem_allocated_current -= *(size_t*) ptr;
        } else {
            ptr = (int*) ptr + 1;
        }
    }
    newptr = realloc(ptr, sizeof(size_t) + sizeof(int) + size);
    *((size_t*) newptr) = size;
    newptr = (size_t*) newptr + 1;
    *((int*) newptr) = MPI_T_COOKIE_VALUE;
    newptr = (int*) newptr + 1;
    mpit_mem_allocated_current += size;
    if (mpit_mem_allocated_max < mpit_mem_allocated_current)
        mpit_mem_allocated_max = mpit_mem_allocated_current;
    if (mpit_mem_allocated_min > mpit_mem_allocated_current)
        mpit_mem_allocated_min = mpit_mem_allocated_current;

    return newptr;
}

void *MPIT_strdup(const char * s) {

    void *ptr;
    size_t size;

    size = (strlen(s) + 1) * sizeof(char);
    ptr = malloc(sizeof(size_t) + sizeof(int) + size);
    *((size_t*) ptr) = size;
    ptr = (size_t*) ptr + 1;
    *((int*) ptr) = MPI_T_COOKIE_VALUE;
    ptr = (int*) ptr + 1;
    memcpy(ptr, s, size);
    mpit_mem_allocated_current += size;
    if (mpit_mem_allocated_max < mpit_mem_allocated_current)
        mpit_mem_allocated_max = mpit_mem_allocated_current;
    if (mpit_mem_allocated_min > mpit_mem_allocated_current)
        mpit_mem_allocated_min = mpit_mem_allocated_current;
    return ptr;
}

void MPIT_free(void *ptr) {

    if (ptr != NULL) {
        ptr = (int*) ptr - 1;
        if (*((int*) ptr) == MPI_T_COOKIE_VALUE) {
            ptr = (size_t*) ptr - 1;
            mpit_mem_allocated_current -= *(size_t*) ptr;
        } else {
            ptr = (int*) ptr + 1;
        }
        free(ptr);
        ptr = NULL;
    }
    if (mpit_mem_allocated_max < mpit_mem_allocated_current)
        mpit_mem_allocated_max = mpit_mem_allocated_current;
    if (mpit_mem_allocated_min > mpit_mem_allocated_current)
        mpit_mem_allocated_min = mpit_mem_allocated_current;
}

#endif /* (_OSU_MVAPICH_) && OSU_MPIT */

/* Various helper functions add thread safety to the MPL_tr* functions.  These
 * have to be functions because we cannot portably wrap the calls as macros and
 * still use real (non-out-argument) return values. */

void MPIU_trinit(int rank)
{
    MPL_trinit(rank);
}

void MPIU_trdump(FILE *fp, int minid)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trdump(fp, minid);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

void *MPIU_trmalloc(size_t a, int lineno, const char fname[])
{
    void *retval;
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    retval = MPL_trmalloc(a, lineno, fname);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
    return retval;
}

void MPIU_trfree(void *a_ptr, int line, const char fname[])
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trfree(a_ptr, line, fname);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

int MPIU_trvalid(const char str[])
{
    int retval;
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    retval = MPL_trvalid(str);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
    return retval;
}

void MPIU_trspace(int *space, int *fr)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trspace(space, fr);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

void MPIU_trid(int id)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trid(id);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

void MPIU_trlevel(int level)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trlevel(level);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

void MPIU_trDebugLevel(int level)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_trDebugLevel(level);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

void *MPIU_trcalloc(size_t nelem, size_t elsize, int lineno, const char fname[])
{
    void *retval;
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    retval = MPL_trcalloc(nelem, elsize, lineno, fname);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
    return retval;
}

void *MPIU_trrealloc(void *p, size_t size, int lineno, const char fname[])
{
    void *retval;
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    retval = MPL_trrealloc(p, size, lineno, fname);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
    return retval;
}

void *MPIU_trstrdup(const char *str, int lineno, const char fname[])
{
    void *retval;
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    retval = MPL_trstrdup(str, lineno, fname);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
    return retval;
}

void MPIU_TrSetMaxMem(size_t size)
{
    MPIU_THREAD_CS_ENTER(MEMALLOC,);
    MPL_TrSetMaxMem(size);
    MPIU_THREAD_CS_EXIT(MEMALLOC,);
}

