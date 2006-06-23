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

/* Copyright (c) 2002-2006, The Ohio State University. All rights
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

/* Thanks to Voltaire for contributing enhancements to
 * registration cache implementation
 */

#include <stdlib.h>

#include "dreg.h"
#include "avl.h"

#include "ibv_priv.h"

#if (defined (MALLOC_HOOK) &&           \
        defined (LAZY_MEM_UNREGISTER))
/************************
 * For overriding malloc
 *************************/ 
#error this path should never be used

#include <malloc.h>

/***************************
 * Global Hash Table
 ***************************/
Hash_Table *my_hash_table = NULL;
#endif                          /* MALLOC_HOOK && VIADEV_RPUT_SUPPORT &&
                                   LAZY_MEM_UNREGISTER */

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */


/* statistic */
unsigned long dreg_stat_cache_hit, dreg_stat_cache_miss, dreg_stat_evicted;
static unsigned long pinned_pages_count;

struct dreg_entry *dreg_free_list;
struct dreg_entry *dreg_unused_list;
struct dreg_entry *dreg_unused_tail;

/*
 * Delete entry d from the double-linked unused list.
 * Take care if d is either the head or the tail of the list.
 */

#define DREG_REMOVE_FROM_UNUSED_LIST(d) {                           \
    dreg_entry *prev = (d)->prev_unused;                            \
        dreg_entry *next = (d)->next_unused;                        \
        (d)->next_unused = NULL;                                    \
        (d)->prev_unused = NULL;                                    \
        if (prev != NULL) {                                         \
            prev->next_unused = next;                               \
        }                                                           \
    if (next != NULL) {                                             \
        next->prev_unused = prev;                                   \
    }                                                               \
    if (dreg_unused_list == (d)) {                                  \
        dreg_unused_list = next;                                    \
    }                                                               \
    if (dreg_unused_tail == (d)) {                                  \
        dreg_unused_tail = prev;                                    \
    }                                                               \
}

/*
 * Add entries to the head of the unused list. dreg_evict() takes
 * them from the tail. This gives us a simple LRU mechanism 
 */

#define DREG_ADD_TO_UNUSED_LIST(d) {                                \
    d->next_unused = dreg_unused_list;                              \
    d->prev_unused = NULL;                                          \
    if (dreg_unused_list != NULL) {                                 \
        dreg_unused_list->prev_unused = d;                          \
    }                                                               \
    dreg_unused_list = d;                                           \
    if (NULL == dreg_unused_tail) {                                 \
        dreg_unused_tail = d;                                       \
    }                                                               \
}

#define DREG_GET_FROM_FREE_LIST(d) {                                \
    d = dreg_free_list;                                             \
    if (dreg_free_list != NULL) {                                   \
        dreg_free_list = dreg_free_list->next;                      \
    }                                                               \
}

#define DREG_ADD_TO_FREE_LIST(d) {                                  \
    d->next = dreg_free_list;                                       \
    dreg_free_list = d;                                             \
}


#define DREG_BEGIN(R) ((R)->pagenum)
#define DREG_END(R) ((R)->pagenum + (R)->npages - 1)

/* list element */
typedef struct _entry
{
	dreg_entry *reg;
	struct _entry *next;
} entry_t;

typedef struct _vma 
{
	unsigned long start;      /* first page number of the area */
	unsigned long end;        /* last page number of the area */
	entry_t *list;	  	  /* all dregs on this virtual memory region */
	unsigned long list_count;  /* number of elements on the list */
	struct _vma *next, *prev; /* double linked list of vmas */
} vma_t;

vma_t vma_list;
AVL_TREE *vma_tree;

/* Tree functions */
static long vma_compare (void *a, void *b)
{
	const vma_t *vma1 = *(vma_t**)a, *vma2 = *(vma_t**)b;
	return vma1->start - vma2->start;
}

static long vma_compare_search (void *a, void *b)
{
	const vma_t *vma = *(vma_t**)b;
	const unsigned long addr = (unsigned long)a;

	if (vma->end < addr)
		return 1;

	if (vma->start <= addr)
		return 0;

	return -1;
}

static long vma_compare_closest (void *a, void *b)
{
	const vma_t *vma = *(vma_t**)b;
	const unsigned long addr = (unsigned long)a;

	if (vma->end < addr)
		return 1;

	if (vma->start <= addr)
		return 0;

	if (vma->prev->end < addr)
		return 0;

	return -1;
}

static inline vma_t *vma_search (unsigned long addr)
{
	vma_t **vma;
	vma = avlfindex (vma_compare_search, (void*)addr, vma_tree);
	return vma?(*vma):NULL;
}


static unsigned long  avldatasize (void)
{
	return (unsigned long) (sizeof (void *));
}

static inline vma_t *vma_new (unsigned long start, unsigned long end)
{
	vma_t *vma;

	if (rdma_dreg_cache_limit &&
			pinned_pages_count + (end - start + 1) > 
				rdma_dreg_cache_limit)
		return NULL;
			
	vma = malloc (sizeof (vma_t));

	if (vma == NULL)
		return NULL;

	vma->start = start;
	vma->end = end;
	vma->next = vma->prev = NULL;
	vma->list = NULL;
	vma->list_count = 0;

	avlins (&vma, vma_tree);

	pinned_pages_count += (vma->end - vma->start + 1);

	return vma;
}

static inline void vma_remove (vma_t *vma)
{
	avldel (&vma, vma_tree);
	pinned_pages_count -= (vma->end - vma->start + 1);
}

static inline void vma_destroy (vma_t *vma)
{
	entry_t *e = vma->list;

	while (e)
	{
		entry_t *t = e;
		e = e->next;
		free (t);
	}

	free (vma);
}

static inline long compare_dregs (entry_t *e1, entry_t *e2)
{
	if (DREG_END (e1->reg) != DREG_END (e2->reg))
		return DREG_END (e1->reg) - DREG_END (e2->reg);

	/* tie breaker */
	return (unsigned long)e1->reg - (unsigned long)e2->reg;
}

/* add entry to list of dregs. List sorted by region last page number */
static inline void add_entry (vma_t *vma, dreg_entry *r)
{
	entry_t **i, *e;

	e = malloc (sizeof (entry_t));
	if (e == NULL)
		return;

	e->reg = r;
	
	for (i = &vma->list; *i != NULL && compare_dregs (*i, e) > 0; i=&(*i)->next);

	e->next = (*i);
	(*i) = e;
	vma->list_count++;
}

static inline void remove_entry (vma_t *vma, dreg_entry *r)
{
	entry_t **i;

	for (i = &vma->list; *i != NULL && (*i)->reg != r; i=&(*i)->next);

	if (*i)
	{
		entry_t *e = *i;
		*i = (*i)->next;
		free (e);
		vma->list_count--;
	}
}

static inline void copy_list (vma_t *to, vma_t *from)
{
	entry_t *f = from->list, **t = &to->list;

	while (f)
	{
		entry_t *e = malloc (sizeof (entry_t));

		e->reg = f->reg;
		e->next = NULL;

		*t = e;
		t = &e->next;
		f = f->next;
	}
	to->list_count = from->list_count;
}

/* returns 1 iff two lists contain the same entries */
static inline int compare_lists (vma_t *vma1, vma_t *vma2)
{
	entry_t *e1 = vma1->list, *e2 = vma2->list;

	if (vma1->list_count != vma2->list_count)
		return 0;

	while (1)
	{
		if (e1 == NULL || e2 == NULL)
			return 1;

		if (e1->reg != e2->reg)
			break;

		e1 = e1->next;
		e2 = e2->next;
	}

	return 0;
}

static int dreg_remove (dreg_entry *r)
{
	vma_t  *vma;


	vma = vma_search (DREG_BEGIN (r));


	if (vma == NULL)  /* no such region in database */
		return -1;

	while (vma != &vma_list && vma->start <= DREG_END (r))
	{
		remove_entry (vma, r);


		if (vma->list == NULL)
		{
			vma_t *next = vma->next;

			vma_remove (vma);
			vma->prev->next = vma->next;
			vma->next->prev = vma->prev;
			vma_destroy (vma);
			vma = next;
		}
		else
		{
			int merged;

			do {
				merged = 0;
				if (vma->start == vma->prev->end + 1 &&
						compare_lists (vma, vma->prev))
				{
					vma_t *t = vma;
					vma = vma->prev;
					vma->end = t->end;
					vma->next = t->next;
					vma->next->prev = vma;
					vma_remove (t);
					vma_destroy (t);
					merged = 1;
				}
				if (vma->end + 1 == vma->next->start &&
						compare_lists (vma, vma->next))
				{
					vma_t *t = vma->next;
					vma->end = t->end;
					vma->next = t->next;
					vma->next->prev = vma;
					vma_remove (t);
					vma_destroy (t);
					merged = 1;
				}
			} while (merged);
			vma = vma->next;
		}
	}

	return 0;
}

static int dreg_insert (dreg_entry *r)
{
	vma_t *i = &vma_list, **v;
	unsigned long begin = DREG_BEGIN (r), end = DREG_END (r);

	v = avlfindex (vma_compare_closest, (void*)begin, vma_tree);

	if (v)
		i = *v;

	while (begin <= end)
	{
		vma_t *vma;

		if (i == &vma_list)
		{
			vma = vma_new (begin, end);

			if (!vma)
				goto remove;

			vma->next = i;
			vma->prev = i->prev;
			i->prev->next = vma;
			i->prev = vma;

			begin = vma->end + 1;

			add_entry (vma, r);
		} 
		else if (i->start > begin)
		{
			vma = vma_new (begin, 
					(i->start <= end)?(i->start - 1):end);

			if (!vma)
				goto remove;

			/* insert before */
			vma->next = i;
			vma->prev = i->prev;
			i->prev->next = vma;
			i->prev = vma;

			i = vma;

			begin = vma->end + 1;

			add_entry (vma, r);
		}
		else if (i->start == begin)
		{
			if (i->end > end)
			{
				vma = vma_new (end+1, i->end);
				
				if (!vma)
					goto remove;

				i->end = end;

				copy_list (vma, i);

				/* add after */
				vma->next = i->next;
				vma->prev = i;
				i->next->prev = vma;
				i->next = vma;

				add_entry (i, r);
				begin = end + 1;
			}
			else
			{
				add_entry (i, r);
				begin = i->end + 1;
			}
		}
		else
		{
			vma = vma_new (begin, i->end);

			if (!vma)
				goto remove;

			i->end = begin - 1;

			copy_list (vma, i);

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
	dreg_remove (r);
	return -1;
}


static inline dreg_entry *dreg_lookup (unsigned long begin, unsigned long end )
{
	vma_t *vma;

	vma = vma_search (begin);

	if (!vma)
		return NULL;

	if (DREG_END (vma->list->reg) >= end)
		return vma->list->reg;

	return NULL;
}

void vma_db_init (void)
{
	vma_tree = avlinit (vma_compare, avldatasize);
	vma_list.next = &vma_list;
	vma_list.prev = &vma_list;
	vma_list.list = NULL; 
	vma_list.list_count = 0;
}

void dreg_init()
{
    int i;

    /* Setup original malloc hooks */
    SET_ORIGINAL_MALLOC_HOOKS;

    pinned_pages_count = 0;
    vma_db_init ();
    dreg_free_list = (dreg_entry *)
        malloc(sizeof(dreg_entry) * rdma_ndreg_entries);

    if (dreg_free_list == NULL) {
	   ibv_error_abort(GEN_EXIT_ERR,
                            "dreg_init: unable to malloc %d bytes",
                            (int) sizeof(dreg_entry) * rdma_ndreg_entries);
    }

    SAVE_MALLOC_HOOKS;

    /* Setup our hooks again */
    SET_MVAPICH_MALLOC_HOOKS;


    for (i = 0; i < (int) rdma_ndreg_entries - 1; i++) {
        dreg_free_list[i].next = &dreg_free_list[i + 1];
    }
    dreg_free_list[rdma_ndreg_entries - 1].next = NULL;

    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;
    /* cache hit and miss time stat variables initisalization */

}

/* will return a NULL pointer if registration fails */
dreg_entry *dreg_register(void *buf, int len)
{
    struct dreg_entry *d;
    int rc;

    d = dreg_find(buf, len);

    if (d != NULL) {
	dreg_stat_cache_hit++;
        dreg_incr_refcount(d);
	

    } else {
	    dreg_stat_cache_miss++;
           while ((d = dreg_new_entry(buf, len)) == NULL) {
            /* either was not able to obtain a dreg_entry data strucrure
             * or was not able to register memory.  In either case,
             * attempt to evict a currently unused entry and try again.
             */
            rc = dreg_evict();
            if (rc == 0) {
                /* could not evict anything, will not be able to
                 * register this memory.  Return failure.
                 */
                return NULL;
            }
            /* eviction successful, try again */
        }

        dreg_incr_refcount(d);

    }

    return d;
}

void dreg_unregister(dreg_entry * d)
{
    dreg_decr_refcount(d);
}


dreg_entry *dreg_find(void *buf, int len)
{
    unsigned long begin = ((unsigned long)buf) >> DREG_PAGEBITS;
    unsigned long end = ((unsigned long)(((char*)buf) + len - 1)) >> DREG_PAGEBITS;

    return dreg_lookup (begin, end);
}


/*
 * get a fresh entry off the free list. 
 * Ok to return NULL. Higher levels will deal with it. 
 */

dreg_entry *dreg_get()
{
    dreg_entry *d;
    DREG_GET_FROM_FREE_LIST(d);

    if (d != NULL) {
        d->refcount = 0;
        d->next_unused = NULL;
        d->prev_unused = NULL;
        d->next = NULL;
    } else {
        DEBUG_PRINT("dreg_get: no free dreg entries");
    }
    return (d);
}

void dreg_release(dreg_entry * d)
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
void dreg_decr_refcount(dreg_entry * d)
{
#ifndef LAZY_MEM_UNREGISTER
    int rc;
    void *buf;
    unsigned long bufint;
#endif

    assert(d->refcount > 0);
    d->refcount--;
    if (d->refcount == 0) {
#ifdef LAZY_MEM_UNREGISTER
        DREG_ADD_TO_UNUSED_LIST(d);
#else
        bufint = d->pagenum << DREG_PAGEBITS;
        buf = (void *) bufint;

        if (deregister_memory(d->memhandle)) {
            ibv_error_abort(IBV_RETURN_ERR, "deregister fails\n");
        }
	    dreg_remove (d);
        DREG_ADD_TO_FREE_LIST(d);
#endif

    }
}

/*
 * Increment reference count on a dreg entry. If reference count
 * was zero and it was on the unused list (meaning it had been
 * previously used, and the refcount had been decremented),
 * we should take it off
 */

void dreg_incr_refcount(dreg_entry * d)
{
    assert(d != NULL);
    if (d->refcount == 0) {
        DREG_REMOVE_FROM_UNUSED_LIST(d);
    }
    d->refcount++;
}

/*
 * Evict a registration. This means delete it from the unused list, 
 * add it to the free list, and deregister the associated memory.
 * Return 1 if success, 0 if nothing to evict.
 */
int dreg_evict()
{
    void *buf;
    dreg_entry *d;
    unsigned long bufint;

    d = dreg_unused_tail;
    if (d == NULL) {
        /* no entries left on unused list, return failure */
        return 0;
    }
    
    DREG_REMOVE_FROM_UNUSED_LIST(d);

    assert(d->refcount == 0);
    bufint = d->pagenum << DREG_PAGEBITS;
    buf = (void *) bufint;

    if (deregister_memory(d->memhandle)) {
        ibv_error_abort(IBV_RETURN_ERR,
                        "Deregister fails\n");
    }

    dreg_remove (d);

    DREG_ADD_TO_FREE_LIST(d);

    dreg_stat_evicted++;
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */

dreg_entry *dreg_new_entry(void *buf, int len)
{


    dreg_entry *d;
    unsigned long pagenum_low, pagenum_high;
    unsigned long  npages;
    /* user_low_a is the bottom address the user wants to register;
     * user_high_a is one greater than the top address the 
     * user wants to register
     */
    unsigned long user_low_a, user_high_a;
    /* pagebase_low_a and pagebase_high_a are the addresses of 
     * the beginning of the page containing user_low_a and 
     * user_high_a respectively. 
     */

    unsigned long pagebase_low_a, pagebase_high_a;
    void *pagebase_low_p;
    unsigned long register_nbytes;

    d = dreg_get();
    if (NULL == d) {
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

    if ( rdma_dreg_cache_limit != 0 && 
		  npages >= (int) rdma_dreg_cache_limit ) {
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

    d->memhandle = register_memory((void *)pagebase_low_p, register_nbytes);

    /* if not success, return NULL to indicate that we were unable to
     * register this memory.  */
    if (!d->memhandle) {
	dreg_remove (d);
        dreg_release(d);
        return NULL;
    }

    return d;
}


#if (defined (MALLOC_HOOK) &&                                              \
        defined (LAZY_MEM_UNREGISTER))

#error this path should never be used
/* Overriding normal malloc init */
void mvapich_init_malloc_hook(void)
{
    old_malloc_hook = __malloc_hook;
    old_free_hook = __free_hook;
    __malloc_hook = mvapich_malloc_hook;
    __free_hook = mvapich_free_hook;
}

/*
 * Malloc Hook
 * It is necessary to have the malloc hook
 * because we want to store the starting address
 * of the buffer and the size allocated. This information
 * is needed by the free hook, so that it can look through
 * the region and find out whether any portion of it was
 * registered before or not.
 *
 * Currently, the malloc hook simply stores addr & len
 * in a hash table.
 */
void *mvapich_malloc_hook(size_t size, const void *caller)
{
    void *result;
    unsigned int hash_value;
    Hash_Symbol *new_symbol = NULL;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;

    /* Restore old hooks */
    SET_ORIGINAL_MALLOC_HOOKS;

    /* Call the real malloc */
    result = malloc(size);

    /* Now we have to put this in the Hash Table */

    /* Check whether we've created the HT or not ! */
    if (my_hash_table != NULL) {
        hash_value = hash((unsigned long) result);

        if (NULL == my_hash_table[hash_value].symbol) {
            /* First element */
            new_symbol = (Hash_Symbol *) malloc(sizeof(Hash_Symbol));
            new_symbol->mem_ptr = result;
            new_symbol->len = (unsigned int) size;
            new_symbol->next = NULL;

            my_hash_table[hash_value].symbol = (void *) new_symbol;
        } else {
            /* There are other elements, walk to end of list
             * and then add at the end
             */
            for (sym_i = (Hash_Symbol *) my_hash_table[hash_value].symbol;
                 sym_i != NULL; sym_i = sym_i->next) {

                sym_temp = sym_i;

            }
            /* sym_temp is now pointing to the last element */
            new_symbol = (Hash_Symbol *) malloc(sizeof(Hash_Symbol));
            new_symbol->mem_ptr = result;
            new_symbol->len = (unsigned int) size;
            new_symbol->next = NULL;
            sym_temp->next = new_symbol;
        }
    }
    /* Save the hooks again */
    SAVE_MALLOC_HOOKS;

    /* Restore our hooks again */
    SET_MVAPICH_MALLOC_HOOKS;

    return result;
}

/*
 * Free hook.
 * Before freeing a buffer, we have to search whether
 * any portion of it was registered or not.
 * Look up in the Hash table.
 * If any part was registered, we have to de-register it
 * before we free the buffer.
 */
void mvapich_free_hook(void *ptr, const void *caller)
{
    unsigned int hash_value;
    int found_flag = 0;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;

    /* Restore all old hooks */
    SET_ORIGINAL_MALLOC_HOOKS;
    /* We have to look for the buffer we had put in
     * the HT */
    if (my_hash_table != NULL) {
        /*
         * OK, we've set up the table.
         * But is this buffer in it ?
         * If it is, then return the length *and* free the buffer
         */
        hash_value = hash((unsigned long) ptr);
        if (NULL == my_hash_table[hash_value].symbol) {
            /* It cannot possibly be in our HT */
            free(ptr);
        } else {
            /* It is here ... but where is it hiding ? */

            /* Check the first one */
            sym_i = (Hash_Symbol *) my_hash_table[hash_value].symbol;
            if (sym_i->mem_ptr == ptr) {
                /* Found it ! */
                find_and_free_dregs_inside(ptr, sym_i->len);
                if (NULL == sym_i->next) {
                    /* There are no more entries */
                    free(sym_i);
                    free(ptr);
                    my_hash_table[hash_value].symbol = NULL;
                } else {
                    sym_temp = sym_i->next;
                    my_hash_table[hash_value].symbol = sym_temp;
                    free(sym_i);
                    free(ptr);
                }
            } else {
                /* Follow the link list */
                /* sym_temp always points one behind sym_i */
                sym_temp =
                    (Hash_Symbol *) my_hash_table[hash_value].symbol;

                for (sym_i = sym_temp->next;
                     sym_i != NULL; sym_i = sym_i->next) {
                    if (sym_i->mem_ptr == ptr) {
                        /* Found it ! */
                        find_and_free_dregs_inside(ptr, sym_i->len);

                        if (sym_i->next != NULL) {
                            /* There is more to follow */
                            sym_temp->next = sym_i->next;
                            free(sym_i);
                            free(ptr);
                            found_flag = 1;
                            break;
                        } else {
                            /* This was the last one -- whew ! */
                            sym_temp->next = NULL;
                            free(sym_i);
                            free(ptr);
                            found_flag = 1;
                            break;
                        }
                    }
                    if (!found_flag) {
                        sym_temp = sym_i;
                    }
                }
                /* Wasn't the first. Did we find it in the link list search ?
                 *         * found_flag tells all !
                 *                 */
                if (!found_flag) {
                    free(ptr);
                }
            }
        }
    } else {
        /* Don't bother, just free the stuff */
        free(ptr);
    }

    /* Save underlying hooks */
    SAVE_MALLOC_HOOKS;
    /* Restore our own hooks */
    SET_MVAPICH_MALLOC_HOOKS;
}

/*
 * Hash Function
 * gives a number between 0 - HASH_TABLE_SIZE
 */
unsigned int hash(unsigned int key)
{
    key += ~(key << 15);
    key ^= (key >> 10);
    key += (key << 3);
    key ^= (key >> 6);
    key += ~(key << 11);
    key ^= (key >> 16);
    return (key % (HASH_TABLE_SIZE));
}

/*
 * Creates the Hash Table that stores the malloc'd
 * memory address and the length associated along
 * with it
 */
void create_hash_table(void)
{
    int i = 0;

    /* This malloc doesn't go in the Hash Table */
    SET_ORIGINAL_MALLOC_HOOKS;

    my_hash_table = (Hash_Table *)
        malloc(sizeof(Hash_Table) * HASH_TABLE_SIZE);
    if (NULL == my_hash_table) {
        ibv_error_abort(GEN_EXIT_ERR,
                            "Malloc failed, not enough space for Hash Table!\n");
    }

    /* Save the old hooks */
    SAVE_MALLOC_HOOKS;

    /* Initialize Hash Table */
    for (i = 0; i < HASH_TABLE_SIZE; i++) {
        my_hash_table[i].symbol = NULL;
    }

    /* Set up our hooks again */
    SET_MVAPICH_MALLOC_HOOKS;
}

/*
 * is_dreg_registered
 * A function which is called to find out whether
 * a particular address is registered or not.
 *
 * Returns :
 * . `NULL' if the address is not registered
 * . `dreg_entry*' ie the pointer to the registered region.
 */
dreg_entry *is_dreg_registered(void *buf)
{
    dreg_entry *d = NULL;
    d = dreg_table[DREG_HASH(buf)];
    return d;
}

/*
 * Finds if any portion of the buffer was
 * registered before. If it was then deregister it
 * otherwise leave it alone and return
 */
void find_and_free_dregs_inside(void *ptr, int len)
{
    int i = 0;
    int rc = 0;
    dreg_entry *d;
    int npages = 0;
    int pagesize = 0;
    int pagebase_a = 0;
    unsigned int buf_a;

    buf_a = (unsigned long) ptr;
    pagebase_a = buf_a & ~DREG_PAGEMASK;
    npages =
        1 + ((buf_a + (unsigned long) len - pagebase_a - 1) >> DREG_PAGEBITS);
    for (i = 0; i < npages; i++) {
        d = dreg_find((void *) ((unsigned long) ptr + i * pagesize),
                      pagesize);
        if (d) {
            if (d->refcount != 0) {
                /* Forcefully free it */
                if (deregister_memory(d->memhandle)) {
                    ibv_err_abort(IBV_RETURN_ERR, 
                                "unregister fails\n");
                }
                
		        dreg_remove (d)
                DREG_ADD_TO_FREE_LIST(d);
            } else {
                /* Safe to remove from unused list */
                DREG_REMOVE_FROM_UNUSED_LIST(d);
                if (deregister_memory(d->memhandle)) {
                    ibv_err_abort(IBV_RETURN_ERR,
                                "unregister fails\n");
                }
		        dreg_remove (d);
                DREG_ADD_TO_FREE_LIST(d);
            }
        }
    }
}

#endif                          /* MALLOC_HOOK */
