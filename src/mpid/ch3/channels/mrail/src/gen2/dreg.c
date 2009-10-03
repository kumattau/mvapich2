/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2002-2009, The Ohio State University. All rights
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

/* Thanks to Voltaire for contributing enhancements to
 * registration cache implementation
 */

#include "mpidi_ch3i_rdma_conf.h"

#include <mpimem.h>
#include <stdlib.h>

#include "dreg.h"
#include "avl.h"
#include "rdma_impl.h"
#include "mpiutil.h"

#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...)                                          \
    do {                                                              \
        int rank;                                                     \
        PMI_Get_rank(&rank);                                          \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);    \
        fprintf(stderr, args);                                        \
    } while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */

unsigned long dreg_stat_cache_hit;
unsigned long dreg_stat_cache_miss;
unsigned long dreg_stat_evicted;
static unsigned long g_pinned_pages_count;

struct dreg_entry* dreg_free_list;
struct dreg_entry* dreg_unused_list;
struct dreg_entry* dreg_unused_tail;

static int g_is_dreg_initialized = 0;

#if defined(CKPT)
struct dreg_entry *dreg_all_list;
#endif /* defined(CKPT) */

#define DREG_BEGIN(R) ((R)->pagenum)
#define DREG_END(R) ((R)->pagenum + (R)->npages - 1)

/* list element */
typedef struct _entry
{
    dreg_entry* reg;
    struct _entry* next;
} entry_t;

typedef struct _vma 
{
    unsigned long start;         /* first page number of the area */
    unsigned long end;           /* last page number of the area */
    entry_t* list;	  	 /* all dregs on this virtual memory region */
    unsigned long list_count;    /* number of elements on the list */

    /* Double linked list of vma's. */
    struct _vma* next;
    struct _vma* prev;
} vma_t;

vma_t vma_list;
AVL_TREE* vma_tree;

#if !defined(DISABLE_PTMALLOC)
/* 
static pthread_spinlock_t dreg_lock = 0;
static pthread_spinlock_t dereg_lock = 0;
static pthread_t th_id_of_lock;
static pthread_t th_id_of_dereg_lock = -1;
*/

/* Array which stores the memory regions 
 * ptrs which are to be deregistered after 
 * free hook pulls them out of the reg cache
 */
static dreg_region *deregister_mr_array;

/* Number of pending deregistration
 * operations 
 * Note: This number can never exceed
 * the total number of reg. cache
 * entries
 */
static int n_dereg_mr;

/* Keep free list of VMA data structs
 * and entries */
static vma_t vma_free_list;
static entry_t entry_free_list;

#define INIT_FREE_LIST(_list)    \
{                                \
    (_list)->next = NULL;        \
}

#define ADD_FREE_LIST(_list, _v)    \
{                                   \
    (_v)->next = (_list)->next;     \
    (_list)->next = (_v);           \
}

#define GET_FREE_LIST(_list, _v)                \
{                                               \
    *(_v) = (_list)->next;                      \
    if((_list)->next)                           \
    {                                           \
        (_list)->next = (_list)->next->next;    \
    }                                           \
}

#endif /* !defined(DISABLE_PTMALLOC) */

/* Tree functions */
static long vma_compare (void* a, void* b)
{
    const vma_t* vma1 = *(vma_t**)a;
    const vma_t* vma2 = *(vma_t**)b;
    return vma1->start - vma2->start;
}

static long vma_compare_search (void* a, void* b)
{
    const vma_t *vma = *(vma_t**)b;
    const unsigned long addr = (unsigned long)a;

    if (vma->end < addr)
    {
        return 1;
    }

    if (vma->start <= addr)
    {
        return 0;
    }

    return -1;
}

static long vma_compare_closest (void* a, void* b)
{
    const vma_t* vma = *(vma_t**)b;
    const unsigned long addr = (unsigned long)a;

    if (vma->end < addr)
    {
        return 1;
    }

    if (vma->start <= addr || vma->prev->end < addr)
    {
        return 0;
    }

    return -1;
}

static inline vma_t *vma_search (unsigned long addr)
{
    vma_t** vma = avlfindex (vma_compare_search, (void*)addr, vma_tree);
    return vma ? *vma : NULL;
}


static unsigned long  avldatasize (void)
{
    return (unsigned long) (sizeof (void*));
}

static inline vma_t *vma_new (unsigned long start, unsigned long end)
{
    vma_t *vma = NULL;

    if (rdma_dreg_cache_limit
        && g_pinned_pages_count + (end - start + 1) > rdma_dreg_cache_limit)
    {
        return NULL;
    }

#if !defined(DISABLE_PTMALLOC)
    GET_FREE_LIST(&vma_free_list, &vma);

    if(NULL == vma) {
        vma = MPIU_Malloc(sizeof (vma_t));
    }
#else /* !defined(DISABLE_PTMALLOC) */
    vma = MPIU_Malloc (sizeof (vma_t));
#endif /* !defined(DISABLE_PTMALLOC) */

    if (vma == NULL)
    {
        return NULL;
    }

    vma->start = start;
    vma->end = end;
    vma->next = vma->prev = NULL;
    vma->list = NULL;
    vma->list_count = 0;

    avlins (&vma, vma_tree);
    g_pinned_pages_count += vma->end - vma->start + 1;
    return vma;
}

static inline void vma_remove (vma_t* vma)
{
    avldel (&vma, vma_tree);
    g_pinned_pages_count -= vma->end - vma->start + 1;
}

static inline void vma_destroy (vma_t* vma)
{
    entry_t* e = vma->list;
    entry_t* t = NULL;

    while (e)
    {
        t = e;
        e = e->next;
#if !defined(DISABLE_PTMALLOC)
        ADD_FREE_LIST(&entry_free_list, t);
#else /* !defined(DISABLE_PTMALLOC) */
        MPIU_Free(t);
#endif /* !defined(DISABLE_PTMALLOC) */
    }

#if !defined(DISABLE_PTMALLOC)
    ADD_FREE_LIST(&vma_free_list, vma);
#else /* !defined(DISABLE_PTMALLOC) */
    MPIU_Free(vma);
#endif /* !defined(DISABLE_PTMALLOC) */
}

static inline long compare_dregs (entry_t* e1, entry_t* e2)
{
    if (DREG_END(e1->reg) != DREG_END(e2->reg))
    {
        return DREG_END(e1->reg) - DREG_END(e2->reg);
    }

    /* tie breaker */
    return (unsigned long)e1->reg - (unsigned long)e2->reg;
}

/* add entry to list of dregs. List sorted by region last page number */
static inline void add_entry (vma_t* vma, dreg_entry* r)
{
    entry_t** i = &vma->list;
    entry_t* e = NULL;

#if !defined(DISABLE_PTMALLOC)
    GET_FREE_LIST(&entry_free_list, &e);

    if (NULL == e)
    {
        e = MPIU_Malloc(sizeof(entry_t));
    }
#else /* !defined(DISABLE_PTMALLOC) */
    e = MPIU_Malloc (sizeof (entry_t));
#endif /* !defined(DISABLE_PTMALLOC) */

    if (e == NULL)
    {
        return;
    }

    e->reg = r;

    for (; *i != NULL && compare_dregs(*i, e) > 0; i = &(*i)->next);

    e->next = *i;
    *i = e;
    ++vma->list_count;
}

static inline void remove_entry (vma_t* vma, dreg_entry* r)
{
    entry_t** i = &vma->list;

    for (; *i != NULL && (*i)->reg != r; i = &(*i)->next);

    if (*i)
    {
        entry_t* e = *i;
        *i = (*i)->next;
#if !defined(DISABLE_PTMALLOC)
        ADD_FREE_LIST(&entry_free_list, e);
#else /* defined(DISABLE_PTMALLOC) */
        MPIU_Free(e);
#endif /* defined(DISABLE_PTMALLOC) */
        --vma->list_count;
    }
}

static inline void copy_list (vma_t* to, vma_t* from)
{
    entry_t* f = from->list;
    entry_t** t = &to->list;
    entry_t* e = NULL;

    while (f)
    {
#if !defined(DISABLE_PTMALLOC)
        GET_FREE_LIST(&entry_free_list, &e);

        if (NULL == e)
        {
            e = MPIU_Malloc(sizeof(entry_t));
        }
#else /* !defined(DISABLE_PTMALLOC) */
        e = MPIU_Malloc (sizeof (entry_t));
#endif /* !defined(DISABLE_PTMALLOC) */

        e->reg = f->reg;
        e->next = NULL;

        *t = e;
        t = &e->next;
        f = f->next;
    }

    to->list_count = from->list_count;
}

/* returns 1 if the lists contain the same entries. */
static inline int compare_lists (vma_t* vma1, vma_t* vma2)
{
    entry_t* e1 = vma1->list;
    entry_t* e2 = vma2->list;

    if (vma1->list_count != vma2->list_count)
    {
        return 0;
    }

    while (1)
    {
        if (e1 == NULL || e2 == NULL)
        {
            return 1;
        }

        if (e1->reg != e2->reg)
        {
            break;
        }

        e1 = e1->next;
        e2 = e2->next;
    }

    return 0;
}

static inline int dreg_remove (dreg_entry* r)
{
    vma_t* vma = vma_search(DREG_BEGIN(r));

    if (vma == NULL)
    {
        /* no such region in database */
        return -1;
    }

    int merged;
    vma_t* t = NULL;

    while (vma != &vma_list && vma->start <= DREG_END(r))
    {
        remove_entry(vma, r);

        if (vma->list == NULL)
        {
            vma_t *next = vma->next;
            vma_remove(vma);
            vma->prev->next = vma->next;
            vma->next->prev = vma->prev;
            vma_destroy(vma);
            vma = next;
        }
        else
        {
            do
            {
                merged = 0;

                if (vma->start == vma->prev->end + 1
                    && compare_lists(vma, vma->prev))
                {
                    t = vma;
                    vma = vma->prev;
                    vma->end = t->end;
                    vma->next = t->next;
                    vma->next->prev = vma;
                    vma_remove (t);
                    vma_destroy (t);
                    merged = 1;
                }

                if (vma->end + 1 == vma->next->start
                    && compare_lists (vma, vma->next))
                {
                    t = vma->next;
                    vma->end = t->end;
                    vma->next = t->next;
                    vma->next->prev = vma;
                    vma_remove (t);
                    vma_destroy (t);
                    merged = 1;
                }
            }
            while (merged);

            vma = vma->next;
        }
    }

    return 0;
}

static int dreg_insert (dreg_entry* r)
{
    vma_t* i = &vma_list;
    unsigned long begin = DREG_BEGIN(r);
    unsigned long end = DREG_END(r);
    vma_t** v = avlfindex (vma_compare_closest, (void*)begin, vma_tree);

    if (v)
    {
        i = *v;
    }

    vma_t* vma = NULL;

    while (begin <= end)
    {
        if (i == &vma_list)
        {
            if (!(vma = vma_new(begin, end)))
            {
                goto remove;
            }

            vma->next = i;
            vma->prev = i->prev;
            i->prev->next = vma;
            i->prev = vma;

            begin = vma->end;
            ++begin;

            add_entry(vma, r);
        } 
        else if (i->start > begin)
        {
            if (!(vma = vma_new (begin, i->start <= end ? i->start - 1 : end)))
            {
                goto remove;
            }

            /* insert before */
            vma->next = i;
            vma->prev = i->prev;
            i->prev->next = vma;
            i->prev = vma;

            i = vma;

            begin = vma->end;
            ++begin;

            add_entry(vma, r);
        }
        else if (i->start == begin)
        {
            if (i->end > end)
            {
                if (!(vma = vma_new(end + 1, i->end)))
                {
                    goto remove;
                }

                i->end = end;
                copy_list(vma, i);

                /* add after */
                vma->next = i->next;
                vma->prev = i;
                i->next->prev = vma;
                i->next = vma;

                add_entry(i, r);
                begin = end;
                ++begin;
            }
            else
            {
                add_entry(i, r);
                begin = i->end;
                ++begin;
            }
        }
        else
        {
            if (!(vma = vma_new(begin, i->end)))
            {
                goto remove;
            }

            i->end = begin;
            --i->end;
            copy_list(vma, i);

            /* add after */
            vma->next = i->next;
            vma->prev = i;
            i->next->prev = vma;
            i->next = vma;
        }

        i = i->next;
    }

    return 0;

remove:
    dreg_remove(r);
    return -1;
}


static inline dreg_entry* dreg_lookup (unsigned long begin, unsigned long end)
{
    vma_t* vma = vma_search(begin);

    if (!vma)
    {
        return NULL;
    }

#if defined(CKPT)
#if 0
    if (vma->list->reg->npages==0)
    {
        return NULL;
    }
#endif /* 0 */
#endif /* defined(CKPT) */

    if (!vma->list)
    {
        return NULL;
    }

    if (DREG_END(vma->list->reg) >= end)
    {
        return vma->list->reg;
    }

    return NULL;
}

void vma_db_init (void)
{
    vma_tree = avlinit(vma_compare, avldatasize);
    vma_list.next = &vma_list;
    vma_list.prev = &vma_list;
    vma_list.list = NULL; 
    vma_list.list_count = 0;
}

#undef FUNCNAME
#define FUNCNAME dreg_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int dreg_init()
{
    int i = 0;
    int mpi_errno = MPI_SUCCESS;
    g_pinned_pages_count = 0;

    vma_db_init ();
    dreg_free_list = (dreg_entry*) MPIU_Malloc((unsigned)(sizeof(dreg_entry) * rdma_ndreg_entries));

    if (dreg_free_list == NULL) {
        MPIU_ERR_SETFATALANDJUMP2(mpi_errno,
                MPI_ERR_INTERN,
                "**fail",
                "**fail %s %d",
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(dreg_entry) * rdma_ndreg_entries);
    }

    memset(dreg_free_list, 0, sizeof(dreg_entry) * rdma_ndreg_entries);

#if defined(CKPT)
    dreg_all_list = dreg_free_list;
#endif /* defined(CKPT) */

    for (; i < (int) rdma_ndreg_entries - 1; ++i) {
        dreg_free_list[i].next = &dreg_free_list[i + 1];
    }

    dreg_free_list[rdma_ndreg_entries - 1].next = NULL;
    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;

    /* cache hit and miss time stat variables initizalization */
    g_is_dreg_initialized = 1;

#if !defined(DISABLE_PTMALLOC)
    pthread_spin_init(&dreg_lock, 0);
    pthread_spin_init(&dereg_lock, 0);

    deregister_mr_array = (dreg_region *) 
        MPIU_Malloc(sizeof(dreg_region) * rdma_ndreg_entries);

    if (NULL == deregister_mr_array) {
        MPIU_ERR_SETFATALANDJUMP2(mpi_errno,
                MPI_ERR_INTERN,
                "**fail",
                "**fail %s %d",
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(struct ibv_mr*) * rdma_ndreg_entries * MAX_NUM_HCAS);
    }

    memset(deregister_mr_array, 0, sizeof(dreg_region) * rdma_ndreg_entries);
    n_dereg_mr = 0;

    INIT_FREE_LIST(&vma_free_list);
    INIT_FREE_LIST(&entry_free_list);
#endif /* !defined(DISABLE_PTMALLOC) */

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#if !defined(DISABLE_PTMALLOC)


int have_dereg() 
{
    return pthread_equal(th_id_of_dereg_lock, pthread_self());
}

void lock_dereg()
{
    pthread_spin_lock(&dereg_lock);
    th_id_of_dereg_lock = pthread_self();
}

void unlock_dereg()
{
    th_id_of_dereg_lock = -1;
    pthread_spin_unlock(&dereg_lock);
}

int have_dreg() {
    return pthread_equal(th_id_of_lock, pthread_self());
}

void lock_dreg()
{
    pthread_spin_lock(&dreg_lock);
    th_id_of_lock = pthread_self();
}

void unlock_dreg()
{
    th_id_of_lock = -1;
    pthread_spin_unlock(&dreg_lock);
}


/* 
 * Check if we have to deregister some memory regions
 * which were previously marked invalid by free hook 
 */

void flush_dereg_mrs_external()
{
    unsigned long i, j, k;
    unsigned long pagenum_low, pagenum_high;
    unsigned long  npages, begin, end;
    unsigned long user_low_a, user_high_a;
    unsigned long pagebase_low_a, pagebase_high_a;
    struct dreg_entry *d;
    void *addr;

    if(n_dereg_mr == 0 || have_dreg() || have_dereg()) {
        return;
    }

    lock_dreg();
    lock_dereg();

    for(j = 0; j < n_dereg_mr; j++) {
        void *buf;
        size_t len;

        buf = deregister_mr_array[j].buf;
        len = deregister_mr_array[j].len;

        /* calculate base page address for registration */
        user_low_a = (unsigned long) buf;
        user_high_a = user_low_a + (unsigned long) len - 1;

        pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
        pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

        /* info to store in hash table */
        pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
        pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
        npages = 1 + (pagenum_high - pagenum_low);

        /* For every page in this buffer find out whether
         * it is registered or not. This is fine, since
         * we register only at a page granularity */

        for(i = 0; i < npages; i++) {
            addr = (void *) ((uintptr_t) pagebase_low_a + i * DREG_PAGESIZE);

            begin = ((unsigned long)addr) >> DREG_PAGEBITS;

            end = ((unsigned long)(((char*)addr) +
                        DREG_PAGESIZE - 1)) >> DREG_PAGEBITS;

            while( (d = dreg_lookup (begin, end)) != NULL) {
                if((d->refcount != 0) || (d->is_valid == 0)) {
                    /* This memory area is still being referenced
                     * by other pending MPI operations, which are
                     * expected to call dreg_unregister and thus
                     * unpin the buffer. We cannot deregister this
                     * page, since other ops are pending from here. */

                    /* OR: This memory region is in the process of
                     * being deregistered. Leave it alone! */
                    continue;
                }

                d->is_valid = 0;

                for (k = 0; k < rdma_num_hcas; k++) {
                    if(d->memhandle[k]) {
                        if (deregister_memory(d->memhandle[k]))
                            ibv_error_abort(IBV_RETURN_ERR, 
                                    "deregistration failed\n");
                        d->memhandle[k] = NULL;
                    }
                }

                if(d->refcount == 0) {
                    if(MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
                        DREG_REMOVE_FROM_UNUSED_LIST(d);
                    }
                } else {
                    d->refcount--;
                }

                dreg_remove (d);
                DREG_ADD_TO_FREE_LIST(d);
            }
        }

    }

    n_dereg_mr = 0;
    unlock_dereg();
    unlock_dreg();
}


#endif /* !defined(DISABLE_PTMALLOC) */

/* will return a NULL pointer if registration fails */
dreg_entry *dreg_register(void* buf, int len)
{
    int rc;

#if !defined(DISABLE_PTMALLOC)
    lock_dreg();
#endif /* !defined(DISABLE_PTMALLOC) */

    struct dreg_entry* d = dreg_find(buf, len);

    if (d != NULL)
    {
        ++dreg_stat_cache_hit;
        dreg_incr_refcount(d);
    }
    else
    {
        ++dreg_stat_cache_miss;

        while ((d = dreg_new_entry(buf, len)) == NULL)
        {
            /* either was not able to obtain a dreg_entry data strucrure
             * or was not able to register memory.  In either case,
             * attempt to evict a currently unused entry and try again.
             */
            rc = dreg_evict();

            if (rc == 0)
            {
                /* could not evict anything, will not be able to
                 * register this memory.  Return failure.
                 */
#if !defined(DISABLE_PTMALLOC)
                unlock_dreg();
#endif /* !defined(DISABLE_PTMALLOC) */
                return NULL;
            }
            /* eviction successful, try again */
        }

        dreg_incr_refcount(d);
    }

#if !defined(DISABLE_PTMALLOC)
    unlock_dreg();
#endif /* !defined(DISABLE_PTMALLOC) */

    return d;
}

void dreg_unregister(dreg_entry* d)
{
#if !defined(DISABLE_PTMALLOC)
    lock_dreg();
#endif /* !defined(DISABLE_PTMALLOC) */

    dreg_decr_refcount(d);

#if !defined(DISABLE_PTMALLOC)
    unlock_dreg();
#endif /* !defined(DISABLE_PTMALLOC) */
}


dreg_entry* dreg_find(void* buf, int len)
{
    unsigned long begin = (unsigned long)buf >> DREG_PAGEBITS;
    unsigned long end = (unsigned long)((char*)buf + len - 1) >> DREG_PAGEBITS;
    return g_is_dreg_initialized ? dreg_lookup(begin, end) : NULL;
}


/*
 * get a fresh entry off the free list. 
 * Ok to return NULL. Higher levels will deal with it. 
 */

dreg_entry* dreg_get(void)
{
    dreg_entry *d = NULL;
    DREG_GET_FROM_FREE_LIST(d);

    if (d != NULL)
    {
        d->refcount = 0;
        d->next_unused = NULL;
        d->prev_unused = NULL;
        d->next = NULL;
    } 
#if defined(DEBUG)
    else
    {
        DEBUG_PRINT("dreg_get: no free dreg entries");
    }
#endif /* defined(DEBUG) */
    return d;
}

void dreg_release(dreg_entry* d)
{
    /* note this correctly handles appending to empty free list */
    d->next = dreg_free_list;
    dreg_free_list = d;
}

/*
 * Decrement reference count on a dreg entry. If ref count goes to 
 * zero, don't free it, but put it on the unused list so we
 * can evict it if necessary. Put on head of unused list. 
 */
void dreg_decr_refcount(dreg_entry* d)
{
    int i = 0;
    MPIU_Assert(d->refcount > 0);
    --d->refcount;

    if (d->refcount == 0)
    {
        if (MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister)
        {
            DREG_ADD_TO_UNUSED_LIST(d);
        }
        else
        {
            for (; i < rdma_num_hcas; ++i)
            {
                if (d->memhandle[i])
                {
                    d->is_valid = 0;

                    if (deregister_memory(d->memhandle[i]))
                    {
                        ibv_error_abort(IBV_RETURN_ERR, "deregister fails\n");
                    }
                }
                d->memhandle[i] = NULL;
            }

            dreg_remove(d);
            DREG_ADD_TO_FREE_LIST(d);
        }
    }
}

/*
 * Increment reference count on a dreg entry. If reference count
 * was zero and it was on the unused list (meaning it had been
 * previously used, and the refcount had been decremented),
 * we should take it off
 */

void dreg_incr_refcount(dreg_entry* d)
{
    MPIU_Assert(d != NULL);

    if (d->refcount == 0)
    {
        DREG_REMOVE_FROM_UNUSED_LIST(d);
    }

    ++d->refcount;
}

/*
 * Evict a registration. This means delete it from the unused list, 
 * add it to the free list, and deregister the associated memory.
 * Return 1 if success, 0 if nothing to evict.
 *
 * If PTMALLOC is defined, its OK to call flush_dereg_mrs()
 * since dreg_evict() is called from within dreg_register()
 * which has the dreg_lock. Otherwise, it can only be called
 * from finalize, where there's only one thread executing
 * anyways.
 */
int dreg_evict()
{
    int hca_index = 0;

    dreg_entry* d = dreg_unused_tail;

    if (d == NULL)
    {
        /* no entries left on unused list, return failure */
        return 0;
    }

    DREG_REMOVE_FROM_UNUSED_LIST(d);
    MPIU_Assert(d->refcount == 0);

    for (; hca_index < rdma_num_hcas; ++hca_index)
    {          
        if (d->memhandle[hca_index])
        {
            d->is_valid = 0;

            if (deregister_memory(d->memhandle[hca_index]))
            {
                ibv_error_abort(IBV_RETURN_ERR, "Deregister fails\n");
            }
        }
    }

    dreg_remove(d);
    DREG_ADD_TO_FREE_LIST(d);
    ++dreg_stat_evicted;
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */
dreg_entry* dreg_new_entry(void* buf, int len)
{
    int i = 0;
    dreg_entry *d = NULL;
    unsigned long pagenum_low;
    unsigned long pagenum_high;
    unsigned long npages;

    /* user_low_a is the bottom address the user wants to register;
     * user_high_a is one greater than the top address the 
     * user wants to register
     */
    unsigned long user_low_a;
    unsigned long user_high_a;

    /* pagebase_low_a and pagebase_high_a are the addresses of 
     * the beginning of the page containing user_low_a and 
     * user_high_a respectively. 
     */
    unsigned long pagebase_low_a;
    unsigned long pagebase_high_a;
    void* pagebase_low_p = NULL;
    unsigned long register_nbytes;

    d = dreg_get();

    if (NULL == d)
    {
        return d;
    }

    /* calculate base page address for registration */
    user_low_a = (unsigned long) buf;
    user_high_a = user_low_a + (unsigned long) len - 1;

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = 1 + (pagenum_high - pagenum_low);

    if (rdma_dreg_cache_limit != 0
        && npages >= (int) rdma_dreg_cache_limit)
    {
        return NULL;
    }

    pagebase_low_p = (void *) pagebase_low_a;
    register_nbytes = npages * DREG_PAGESIZE;

    d->pagenum = pagenum_low;
    d->npages = npages;

    if (dreg_insert (d) < 0)
    {
        dreg_release(d);
        return NULL;
    }

    for (; i < rdma_num_hcas; ++i)
    {
        d->memhandle[i] = register_memory((void *)pagebase_low_p, register_nbytes, i);

        /* if not success, return NULL to indicate that we were unable to
         * register this memory.  */
        if (!d->memhandle[i])
        {
            dreg_remove(d);
            dreg_release(d);
            return NULL;
        }
    }

    d->is_valid = 1;
    return d;
}

#if !defined(DISABLE_PTMALLOC)
void find_and_free_dregs_inside(void* buf, size_t len)
{
    if(!g_is_dreg_initialized ||
            !MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
        return;
    }

    if(have_dereg() || have_dreg()) {
        return;
    }

    lock_dereg();
    deregister_mr_array[n_dereg_mr].buf = buf;
    deregister_mr_array[n_dereg_mr].len = len;

    n_dereg_mr++;
    unlock_dereg();
}
#endif /* !defined(DISABLE_PTMALLOC) */

#if defined(CKPT)
void dreg_deregister_all()
{
    int i = 0;
    int j;
    dreg_entry* d = NULL;

    for (; i < (int) rdma_ndreg_entries; ++i)
    {
        d = &(dreg_all_list[i]);

        if (d->is_valid)
        {
            for (j = 0; j < rdma_num_hcas; ++j)
            {
                if (deregister_memory(d->memhandle[j]))
                {
                    ibv_error_abort(IBV_RETURN_ERR, "deregister fails\n");
                }

                d->memhandle[j] = NULL;
            }
        }
    }
}

void dreg_reregister_all()
{
    int i = 0;
    int j;
    dreg_entry* d = NULL;
    void* pagebase_low_p = NULL;
    unsigned long register_nbytes;

    for (; i < (int) rdma_ndreg_entries; ++i)
    {
        d = &(dreg_all_list[i]);
        if (d->is_valid)
        {
            pagebase_low_p = (void *)(d->pagenum << DREG_PAGEBITS);
            register_nbytes = d->npages * DREG_PAGESIZE;
            for (j = 0; j < rdma_num_hcas; ++j)
            {
                d->memhandle[j] = register_memory(pagebase_low_p, register_nbytes, j);
                if (!d->memhandle[j])
                {
                    printf(
                        "%d: reregister dentry %p, addr %p pagebase_low_p, %lu register_nbytes\n",
                        MPIDI_Process.my_pg_rank,
                        d,
                        pagebase_low_p,
                        register_nbytes);
                    ibv_error_abort(IBV_RETURN_ERR, "reregister fails\n");
                }
            }
        }
    }
}
#endif /* defined(CKPT) */
