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

/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

/*
 *
 * dreg.h
 *
 * Interface for dynamic registration of memory.
 */

#ifndef _DREG_H
#define _DREG_H

#include "vapi_header.h"


typedef struct dreg_entry dreg_entry;

struct dreg_entry {
    aint_t pagenum;
    VIP_MEM_HANDLE memhandle;

    int refcount;

    /* page number, e.g. address >> DREG_PAGEBITS */
    int npages;

#ifdef MAC_OSX
     void *ptr;
     unsigned  long long epoch;
#endif
    /* for hash chain or free chain */
    dreg_entry *next;

    /* for zero refcount chain */
    dreg_entry *next_unused;
    dreg_entry *prev_unused;
};


/*
 * When an application needs to register memory, it
 * calls dreg_register. The application does not keep 
 * track of what has already been registered. This is
 * tracked inside the dreg interface. 
 * 
 * dreg stores dreg entries in a hash table for 
 * easy lookup. The table is indexed by a dreg 
 * page number (the dreg page size may be unrelated
 * to the machine page size, but is a power of two). 
 * 
 * The hash table size is DREG_HASHSIZE. Collisions
 * are handled by chaining through the "next" field
 * in dreg_entry. New entries are placed at the head
 * of the chain. 
 *
 * Registrations are reference-counted. An application 
 * is responsible for pairing dreg_register/unregister
 * calls. 
 * 
 * When the reference count reaches zero, the dreg_entry is moved to
 * the end of the hash chain (xxx this is NOT clearly the right
 * thing. For now, leave it where it is and see how it works) and also
 * placed on the unused_list (it is now on two lists instead of
 * one). Associated memory * is *not* unregistered with VIA, 
 * but is a candidate for VIA unregistration if needed. The
 * unused list is a doubly linked list so that entries can be removed
 * from the middle of the list if an application registration request
 * comes along before memory is actually unregistered. Also, the list
 * is FIFO rather than LIFO (e.g. it has both a tail and a head) so
 * that only entries that have been on the list a long time become
 * candidates for VIA deregistration. 
 * 
 * In summary, there are three lists.
 *  - the dreg free list. pool of dreg structures from which 
 *    we grab new dreg entries as needed. LIFO (head only) for
 *    cache reuse. Singly linked through next field in dreg_entry
 * 
 *  - hash list chain. When there is a hash collision, entries
 *    with the same hash are placed on this list. It is singly
 *    linked through the next field in dreg_entry. New entries
 *    are placed at the head of the list. Because the list is
 *    singly linked, removing an entry from the middle is potentially
 *    expensive. However, hash lists should be short, and the only 
 *    time we remove an entry is if it has zero ref count and we 
 *    have to unregister its memory. 
 *  
 *  - unused list. This is the list of dreg entries that represent
 *    registered memory, but the application is not currently using 
 *    this memory. Rather than deregister memory when the refcount
 *    goes to zero, we put it on the unused list. If resources 
 *    become scarce and we have to unregister memory, it is easy to
 *    find entries with zero ref count. 
 *    NOTE adding/removing entries to/from unused list is a critical
 *    path item that will happen all the time. Also,
 *    the need to find a unused item is very rare, and is associated
 *    with a VIA deregistration/registration. So why do we want
 *    the unused list? It is an LRU device that ensures that only
 *    memory that has not been used for a while will be freed. 
 *    This avoids a serious thrashing scenario. 
 *    xxx consider deleting/replacing unused list later on. 
 */

/* LAZY_MEM_UNREGISTER, if defined, will not un-register memory
 * after the ref-count drops to zero, rather the entry will
 * be put on the unused list in case the memory is to be
 * used again.  These are the semantics described above.
 * This can be a problem if the virtual to physical mapping
 * gets changed between calls.
 * By undefining this macro, we revert to semantics in which
 * memory is unregistered when the ref_count drops to zero.
 * In this case, the unused list should always be empty.
 *
 * NOTE: If not doing RDMA operations, we (MVICH layer)
 * controls all registered memory (VBUFs) and we don't
 * have to worry about the address translation getting
 * changed.  
 */

extern struct dreg_entry *dreg_free_list;

extern struct dreg_entry *dreg_unused_list;
extern struct dreg_entry *dreg_unused_tail;



/* DREG_PAGESIZE must be smaller than or equal to the hardware
 * pagesize. Otherwise we might register past the top page given 
 * to us. This page might be invalid (e.g. read-only). 
 */

#define DREG_PAGESIZE 4096      /* must be 2^DREG_PAGEBITS */
#define DREG_PAGEBITS 12        /* must be ln2 of DREG_PAGESIZE */
#define DREG_PAGEMASK (DREG_PAGESIZE - 1)

#define DREG_HASHSIZE 128       /* must be 2^DREG_HASHBITS */
#define DREG_HASHBITS 7         /* must be ln2 DREG_HASHSIZE */
#define DREG_HASHMASK (DREG_HASHSIZE-1)

#define DREG_HASH(a) ( ( ((aint_t)(a)) >> DREG_PAGEBITS) & DREG_HASHMASK )

void dreg_init(VAPI_hca_hndl_t nic, VAPI_pd_hndl_t ptag);

dreg_entry *dreg_register(void *buf, int len);
void dreg_unregister(dreg_entry * entry);
dreg_entry *dreg_find(void *buf, int len);
dreg_entry *dreg_get(void);
int dreg_evict(void);
void dreg_release(dreg_entry * d);
void dreg_decr_refcount(dreg_entry * d);
void dreg_incr_refcount(dreg_entry * d);
dreg_entry *dreg_new_entry(void *buf, int len);

#ifdef MAC_OSX
void mvapich_malloc_init(void);
#endif

#if (defined(MALLOC_HOOK) &&                                       \
        defined(LAZY_MEM_UNREGISTER)) 

dreg_entry *is_dreg_registered(void *buf);
void find_and_free_dregs_inside(void *buf, int len);
void mvapich_init_malloc_hook(void);
void *mvapich_malloc_hook(size_t, const void *);
void mvapich_free_hook(void *, const void *);

#ifndef MAC_OSX
void *old_malloc_hook;
void *old_free_hook;
#endif

enum { HASH_TABLE_SIZE = 4096 };

typedef struct _hash_table {
    void *symbol;
} Hash_Table;

/* Symbol data structure */
typedef struct _hash_symbol {
    void *mem_ptr;
    unsigned int len;
    struct _hash_symbol *next;
} Hash_Symbol;

unsigned int hash(unsigned int);
void create_hash_table(void);

#ifdef  MAC_OSX
#define SET_ORIGINAL_MALLOC_HOOKS
#define SET_MVAPICH_MALLOC_HOOKS
#define SAVE_MALLOC_HOOKS
#else
#define SET_ORIGINAL_MALLOC_HOOKS do {                              \
    __malloc_hook = old_malloc_hook;                                \
        __free_hook = old_free_hook;                                    \
} while(0);

#define SET_MVAPICH_MALLOC_HOOKS do {                               \
    __malloc_hook = mvapich_malloc_hook;                            \
        __free_hook = mvapich_free_hook;                                \
} while(0);

#define SAVE_MALLOC_HOOKS do {                                      \
    old_malloc_hook = __malloc_hook;                                \
        old_free_hook = __free_hook;                                    \
} while(0);

#endif  

#else

#define SET_ORIGINAL_MALLOC_HOOKS
#define SET_MVAPICH_MALLOC_HOOKS
#define SAVE_MALLOC_HOOKS


#endif                          /* MALLOC_HOOK && LAZY_MEM */

#endif                          /* _DREG_H */
