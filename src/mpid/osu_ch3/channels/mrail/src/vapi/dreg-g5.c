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

#include <stdlib.h>
#include "vapi_header.h"
#include "dreg.h"
#include "vapi_util.h"
#include "vapi_priv.h"            /* for debugging macro */
#include "assert.h"

#if (defined (MALLOC_HOOK) && defined(LAZY_MEM_UNREGISTER))
/************************
 * For overriding malloc
 *************************/
struct freemem_list
{
    void *ptr;
    unsigned int size;
    unsigned char flag;
    struct freemem_list *prev;
    struct freemem_list *next;
};

/***************************
 * Global Hash Table
 ***************************/
Hash_Table *my_hash_table = NULL;


/* Freelist management functions */
struct freemem_list *freelist_head = NULL;
unsigned long long total_freelist_len = 0ll;

#endif                          /* MALLOC_HOOK */


/* New malloc overloading function pointers */
void *(*vt_malloc_ptr)(size_t) = NULL;
void (*vt_free_ptr)(void *) = NULL;
void *vt_dylib = NULL;
int mvapich_need_malloc_init = 1;
                                   
unsigned long long current_epoch = 0ll;
unsigned long long total_dart_size = 0ll;

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */

struct dreg_entry *dreg_table[DREG_HASHSIZE];

struct dreg_entry *dreg_free_list;
struct dreg_entry *dreg_unused_list;
struct dreg_entry *dreg_unused_tail;

static VAPI_hca_hndl_t _dreg_nic;
static VAPI_pd_hndl_t _dreg_ptag;

/* Declare freelist management functions */
static inline int return_mem_tofreelist(void *ptr, unsigned int len);
static inline void *get_mem_fromfreelist(unsigned int size);
static inline unsigned int test_dart_bound(unsigned int len);
/* static inline int in_mallochash(void *ptr); */

#define VT_DART_BOUND 512*1024*1024

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

/*
 * adding to the hash table is just 
 * putting it on the head of the hash chain
 */

#define DREG_ADD_TO_HASH_TABLE(addr, d) {                           \
    int hash = DREG_HASH(addr);                                     \
        dreg_entry *d1 = dreg_table[hash];                          \
        dreg_table[hash] = d;                                       \
        d->next = d1;                                               \
}

/*
 * deleting from the hash table (hopefully a rare event)
 * may be expensive because we have to walk the single-linked
 * hash chain until we find the entry. The special and easy
 * case is if the entry is at the head of the chain
 */

#define DREG_DELETE_FROM_HASH_TABLE(d) {                            \
    aint_t addr = d->pagenum << DREG_PAGEBITS;                      \
    int hash = DREG_HASH(addr);                                     \
    dreg_entry *d1 = dreg_table[hash];                              \
    if (d1 == d) {                                                  \
        dreg_table[hash] = d1->next;                                \
    } else {                                                        \
        while(d1->next != d) {                                      \
            assert(d1->next != NULL);                               \
            d1 = d1->next;                                          \
        }                                                           \
        d1->next = d->next;                                         \
    }                                                               \
}

#define OSX_PAGESIZE 4096
static inline unsigned int calculate_dart_len(unsigned int len)
{
  unsigned int size = len + OSX_PAGESIZE;
  int highest_one = 0, num_ones = 0;
  int t;

  for (t=0; t<sizeof(int)*8; t++)
  {
    if ((size >> t) & 1)
    {
      highest_one = t;
      num_ones++;
    }
  }

  if (num_ones > 1) return(1 << (highest_one + 1));

  return(size);
}
void dreg_init(VAPI_hca_hndl_t nic, VAPI_pd_hndl_t ptag)
{
    int i;

    /* Setup original malloc hooks */
    SET_ORIGINAL_MALLOC_HOOKS;

    for (i = 0; i < DREG_HASHSIZE; i++) {
        dreg_table[i] = NULL;
    }

    dreg_free_list = (dreg_entry *)
        malloc(sizeof(dreg_entry) * vapi_ndreg_entries);
    if (dreg_free_list == NULL) {
        vapi_error_abort(GEN_EXIT_ERR,
                            "dreg_init: unable to malloc %d bytes",
                            (int) sizeof(dreg_entry) * vapi_ndreg_entries);
    }

    SAVE_MALLOC_HOOKS;

    /* Setup our hooks again */
    SET_MVAPICH_MALLOC_HOOKS;


    for (i = 0; i < vapi_ndreg_entries - 1; i++) {
        dreg_free_list[i].next = &dreg_free_list[i + 1];
    }
    dreg_free_list[vapi_ndreg_entries - 1].next = NULL;

    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;
    _dreg_nic = nic;
    _dreg_ptag = ptag;
}

/* will return a NULL pointer if registration fails */
dreg_entry *dreg_register(void *buf, int len)
{
    struct dreg_entry *d;
    int rc;
    d = dreg_find(buf, len);

    if (d != NULL) {
        D_PRINT("find an old one \n");
        dreg_incr_refcount(d);
        D_PRINT("dreg_register: found registered buffer for "
                AINT_FORMAT " length %d", (aint_t) buf, len);
    } else {
        while ((d = dreg_new_entry(buf, len)) == NULL) {
            /* either was not able to obtain a dreg_entry data strucrure
             * or was not able to register memory.  In either case,
             * attempt to evict a currently unused entry and try again.
             */
            D_PRINT("dreg_register: no entries available. Evicting.");
            rc = dreg_evict();
            if (rc == 0) {
                /* could not evict anything, will not be able to
                 * register this memory.  Return failure.
                 */
                D_PRINT("dreg_register: evict failed, cant register");
                return NULL;
            }
            /* eviction successful, try again */
        }
        dreg_incr_refcount(d);
        D_PRINT("dreg_register: created new entry for buffer "
                AINT_FORMAT " length %d", (aint_t) buf, len);
    }

    return d;
}

void dreg_unregister(dreg_entry * d)
{
    dreg_decr_refcount(d);
}


dreg_entry *dreg_find(void *buf, int len)
{

    /* this should be consolidated with dreg_register, where
     * the same calculation for number of pages is done. 
     */

    dreg_entry *d = dreg_table[DREG_HASH(buf)];
    aint_t pagebase_a, buf_a, pagenum;
    int tmp_len, npages;

    if (NULL == d) {
        return NULL;
    }
    buf_a = (aint_t) buf;
    pagebase_a = buf_a & ~DREG_PAGEMASK;
    pagenum = buf_a >> DREG_PAGEBITS;
    /*
     * how many pages should we register? worst case is that
     * address is just below a page boundary and extends just above
     * a page boundary. In that case, we need to register both 
     * pages.
     */
    
    tmp_len = (buf_a - pagebase_a + (aint_t)len);
    npages = tmp_len>>DREG_PAGEBITS;
    if ((tmp_len & DREG_PAGEMASK)!=0)  npages++;

    while (d != NULL) {
        if (d->pagenum == pagenum && d->npages >= npages)
            break;
        d = d->next;
    }

    if (d != NULL) {
        D_PRINT("dreg_find: found entry pagebase="
                AINT_FORMAT " npages=%d",
                d->pagenum << DREG_PAGEBITS, d->npages);
    d->epoch = current_epoch; 
    current_epoch++;
    }

    return d;
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
        d->epoch = current_epoch; 
        current_epoch ++;         
        d->next_unused = NULL;
        d->prev_unused = NULL;
        d->next = NULL;
    } else {
        D_PRINT("dreg_get: no free dreg entries");
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
    int found_flag = 0; 
    dreg_entry *dptr;   
    assert(d->refcount > 0);
    d->refcount--;
    if (d->refcount == 0) {
        dptr = dreg_unused_list;
        while (dptr != NULL)
        {
            if (dptr->epoch < d->epoch)
            {
                d->next_unused = dptr;
                d->prev_unused = dptr->prev_unused;

                if (d->prev_unused) (d->prev_unused)->next_unused = d;

                dptr->prev_unused = d;
                if (dreg_unused_list == dptr) dreg_unused_list = d;
                found_flag = 1;
                break;
            }
            dptr = dptr->next_unused;
        }

        /* Missed */
        if (!found_flag)
        {
            d->next_unused= NULL;
            d->prev_unused = NULL;
            if (dreg_unused_list == NULL) dreg_unused_list = d;
            if (dreg_unused_tail == NULL) dreg_unused_tail = d;
            else
            {
                d->prev_unused = dreg_unused_tail;
                dreg_unused_tail->next_unused = d;
                dreg_unused_tail = d;
            }
        }

    }
    D_PRINT("decr_refcount: entry " AINT_FORMAT
            " refcount" AINT_FORMAT " memhandle" AINT_FORMAT,
            (aint_t) d, (aint_t) d->refcount, (aint_t) d->memhandle.hndl);
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
    int rc;
    void *buf;
    dreg_entry *d;
    aint_t bufint;
    d = dreg_unused_tail;
    if (d == NULL) {
        /* no entries left on unused list, return failure */
        return 0;
    }
    DREG_REMOVE_FROM_UNUSED_LIST(d);

    assert(d->refcount == 0);

    bufint = d->pagenum << DREG_PAGEBITS;
    buf = (void *) bufint;


    D_PRINT("dreg_evict: deregistering address="
            AINT_FORMAT " memhandle = %x",
            bufint, (unsigned int) d->memhandle.hndl);

    rc = VAPI_deregister_mr(_dreg_nic, d->memhandle.hndl);

    if (rc != VAPI_OK) {
        vapi_error_abort(VAPI_RETURN_ERR,
                            "VAPI error[%d] \"%s\" in routine dreg_evict: VAPI_deregister_mr",
                            rc, VAPI_strerror(rc));
    }
    D_PRINT("dreg_evict: VAPI_deregister_mr\n");

    DREG_DELETE_FROM_HASH_TABLE(d);
    DREG_ADD_TO_FREE_LIST(d);
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */

dreg_entry *dreg_new_entry(void *buf, int len)
{

    dreg_entry *d,  *dptr, *dptr_prev;
    aint_t pagenum_low, pagenum_high;
    int npages;
    /* user_low_a is the bottom address the user wants to register;
     * user_high_a is one greater than the top address the 
     * user wants to register
     */
    aint_t user_low_a, user_high_a;
    /* pagebase_low_a and pagebase_high_a are the addresses of 
     * the beginning of the page containing user_low_a and 
     * user_high_a respectively. 
     */

    aint_t pagebase_low_a, pagebase_high_a;
    void *pagebase_low_p;
    int rc;
    unsigned int dart_size;
    unsigned long register_nbytes;
    VAPI_mrw_t mr_in, mr_out;
    VAPI_mr_hndl_t mr_hndl;
    d = dreg_get();
    if (NULL == d) {
        return d;
    }
    /* calculate base page address for registration */
    user_low_a = (aint_t) buf;
    user_high_a = user_low_a + (aint_t) len /* - 1*/ ; 

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = (pagenum_high - pagenum_low); 
    
    if ((user_high_a & DREG_PAGEMASK) != 0) npages ++; 
    pagebase_low_p = (void *) pagebase_low_a;
    register_nbytes = npages * DREG_PAGESIZE;
    /* Check if we are going to hit DART bound. If so, eliminate entries
     * from
     the unused list, till we have enough space */
    dart_size = calculate_dart_len(npages << DREG_PAGEBITS);
    if ((dart_size + total_dart_size) >= VT_DART_BOUND)
    {
        dptr = dreg_unused_tail;
        while (dptr != NULL)
        {
            dptr_prev = dptr->prev_unused;
            total_dart_size -= (unsigned long long)
                calculate_dart_len((dptr->npages)
                        << DREG_PAGEBITS);
            assert(dptr->refcount == 0);
            DREG_REMOVE_FROM_UNUSED_LIST(dptr);
            rc = VAPI_deregister_mr(_dreg_nic, dptr->memhandle.hndl);
            DREG_DELETE_FROM_HASH_TABLE(dptr);
            DREG_ADD_TO_FREE_LIST(dptr);
            if (dart_size + total_dart_size < VT_DART_BOUND) break;
            dptr = dptr_prev;
        }

        if (dptr == NULL)
        {
            printf("PANIC. Unable to find memory to register new MR\n");
            exit(1);
        }

    }
    mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ;
    mr_in.l_key = 0;
    mr_in.pd_hndl = _dreg_ptag;
    mr_in.r_key = 0;
    mr_in.size = register_nbytes;
    mr_in.start = (VAPI_virt_addr_t) (virt_addr_t) pagebase_low_p;
    mr_in.type = VAPI_MR;
    D_PRINT(" register_nbytes = %d\n", register_nbytes);

    D_PRINT("registering address %lx (orig: %lx) "
            "length %lu (orig: %d)",
            (unsigned long) pagebase_low_a,
            (unsigned long) buf, (unsigned long) register_nbytes, len);
    D_PRINT("\tuser_low_a = " AINT_FORMAT
            "; user_high_a = " AINT_FORMAT, user_low_a, user_high_a);

    D_PRINT("\tpagebase_low_a = " AINT_FORMAT
            "; pagebase_high_a = " AINT_FORMAT,
            pagebase_low_a, pagebase_high_a);
    rc = VAPI_register_mr(_dreg_nic, &mr_in, &mr_hndl, &mr_out);
    d->memhandle.lkey = mr_out.l_key;
    d->memhandle.rkey = mr_out.r_key;
    d->memhandle.hndl = mr_hndl;
    /* if not success, return NULL to indicate that we were unable to
     * register this memory.  */
    if (rc != VAPI_OK) {
        dreg_release(d);
        return NULL;
    }

    D_PRINT("registration of address "
            AINT_FORMAT " length %lu -> memhandle %x",
            pagebase_low_a, register_nbytes,
            (unsigned int) d->memhandle.hndl);

    d->pagenum = pagenum_low;
    d->npages = npages;
    total_dart_size += (unsigned long long)(calculate_dart_len(npages <<
                DREG_PAGEBITS));
    d->ptr = buf;
    DREG_ADD_TO_HASH_TABLE(pagebase_low_a, d);

    return d;

}


#if (defined (MALLOC_HOOK) && defined (LAZY_MEM_UNREGISTER)) 
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
void *malloc(size_t size)
{
    void *result;
    unsigned int hash_value;
    Hash_Symbol *new_symbol = NULL;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;
    /* Make sure we have our dynamic lib open before we do
     * anything
     This is to ensure that the malloc overloading is done before
     any calls to malloc */
    if (vt_malloc_ptr == NULL)
    {
        mvapich_malloc_init();
        create_hash_table();
    }

    /* Call the original function pointer */

    if ((result = get_mem_fromfreelist(size)) == NULL) {
        result = (*vt_malloc_ptr)(size);
    }
    /* Now we have to put this in the Hash Table */

    /* Check whether we've created the HT or not ! */
    if (my_hash_table != NULL) {
        hash_value = hash((unsigned int) result);

        if (NULL == my_hash_table[hash_value].symbol) {
            /* First element */
            new_symbol = (Hash_Symbol *) (*vt_malloc_ptr)(sizeof(Hash_Symbol));
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
            new_symbol = (Hash_Symbol *)(*vt_malloc_ptr) (sizeof(Hash_Symbol));
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
void free(void *ptr)
{
    unsigned int hash_value;
    int found_flag = 0;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;
    if (NULL == vt_free_ptr) {
        mvapich_malloc_init();
        create_hash_table();
    }
    /* We have to look for the buffer we had put in
     * the HT */
    if (NULL != my_hash_table) {
        /*
         * OK, we've set up the table.
         * But is this buffer in it ?
         * If it is, then return the length *and* free the buffer
         */
        hash_value = hash((unsigned int) ptr);
        if (NULL == my_hash_table[hash_value].symbol) {
            /* It cannot possibly be in our HT */
            /* DON"T FREE. THIS MIGHT BE A WIRED PAGE */
        } else {
            /* It is here ... but where is it hiding ? */

            /* Check the first one */
            sym_i = (Hash_Symbol *) my_hash_table[hash_value].symbol;
            if (sym_i->mem_ptr == ptr) {
                /* Found it ! */
                if (NULL == sym_i->next) {
                    /* There are no more entries */
                    if (return_mem_tofreelist(ptr, sym_i->len))
                    {
                        find_and_free_dregs_inside(ptr, sym_i->len);
                        (*vt_free_ptr)(ptr);
                    }
                    my_hash_table[hash_value].symbol = NULL;
                } else {
                    sym_temp = sym_i->next;
                    my_hash_table[hash_value].symbol = sym_temp;
                    if (return_mem_tofreelist(ptr, sym_i->len))
                    {
                        find_and_free_dregs_inside(ptr, sym_i->len);
                        (*vt_free_ptr)(ptr);
                    }
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

                        if (sym_i->next != NULL) {
                            /* There is more to follow */
                            sym_temp->next = sym_i->next;
                            if (return_mem_tofreelist(ptr, sym_i->len))
                            {
                                find_and_free_dregs_inside(ptr, sym_i->len);
                                (*vt_free_ptr)(ptr);
                            }
                            found_flag = 1;
                            break;
                        } else {
                            /* This was the last one -- whew ! */
                            sym_temp->next = NULL;
                            if (return_mem_tofreelist(ptr, sym_i->len))
                            {
                                find_and_free_dregs_inside(ptr, sym_i->len);
                                (*vt_free_ptr)(ptr);
                            }
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
                    (*vt_free_ptr)(ptr);
                }
            }
        }
    } else {
        /* Don't bother, just free the stuff */
        (*vt_free_ptr)(ptr); 
    }

    /* Save underlying hooks */
    SAVE_MALLOC_HOOKS;
    /* Restore our own hooks */
    SET_MVAPICH_MALLOC_HOOKS;
}

void *realloc(void *ptr, size_t size)
{
    printf("NOTE: The realloc function is currently not implemented. \
            Ignoring call ...\n");
    return NULL;
}

void *calloc(size_t number, size_t size)
{
    void *ptr;

    ptr = malloc(number*size);
    if (ptr != NULL) memset(ptr, 0, number*size);

    return(ptr);
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
    static int already_initialized = 0;
    /* This malloc doesn't go in the Hash Table */
    SET_ORIGINAL_MALLOC_HOOKS;
    if (!already_initialized) already_initialized = 1;
    else return;
    if (vt_malloc_ptr == NULL) mvapich_malloc_init();
    my_hash_table = (Hash_Table *)
        (*vt_malloc_ptr)(sizeof(Hash_Table) * HASH_TABLE_SIZE);
    if (NULL == my_hash_table) {
        vapi_error_abort(GEN_EXIT_ERR,
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

    buf_a = (unsigned int) ptr;
    pagebase_a = buf_a & ~DREG_PAGEMASK;
    npages =
        1 + ((buf_a + (aint_t) len - pagebase_a - 1) >> DREG_PAGEBITS);
    for (i = 0; i < npages; i++) {
        while(1) {
            d = dreg_find((void *) ((unsigned int) ptr + i * pagesize),
                    pagesize);
            if (d) {
                if (d->refcount != 0) {
                    /* Forcefully free it */
                    rc = VAPI_deregister_mr(_dreg_nic, d->memhandle.hndl);
                    if (rc != VAPI_OK) {
                        vapi_error_abort(VAPI_RETURN_ERR,
                                "VAPI error[%d] \"%s\" in routine find_and_free_dregs_inside: VAPI_deregister_mr",
                                rc, VAPI_strerror(rc));
                    }
                    DREG_DELETE_FROM_HASH_TABLE(d);
                    DREG_ADD_TO_FREE_LIST(d);
                } else {
                    /* Safe to remove from unused list */
                    DREG_REMOVE_FROM_UNUSED_LIST(d);
                    rc = VAPI_deregister_mr(_dreg_nic, d->memhandle.hndl);
                    if (rc != VAPI_OK) {
                        vapi_error_abort(VAPI_RETURN_ERR,
                                "VAPI error[%d] \"%s\" in routine find_and_free_dregs_inside: VAPI_deregister_mr",
                                rc, VAPI_strerror(rc));
                    }
                    DREG_DELETE_FROM_HASH_TABLE(d);
                    DREG_ADD_TO_FREE_LIST(d);
                }
            }
            else break;
        }
    }
}

#define VT_MEMLIST_BOUND 1024*1024*1024
#define FREELIST_MATCH_BOUND 8
#define VT_MIN_MEMTHRESHOLD 0

/* Adds the pointer to the free list if doing so does not exceed our DART
 * bound.
 Returns 1 on failure */
static inline int return_mem_tofreelist(void *ptr, unsigned int len)
{
    struct freemem_list *f, *ftmp = NULL;
    unsigned int max_size =0;
    if (len < VT_MIN_MEMTHRESHOLD) return(1);
    if (len > VT_MEMLIST_BOUND/8) return(1);

    if (test_dart_bound(len))
    {
        /* printf("Reached memlist bound. Requested size %d bytes ...\n", len); */

        f = freelist_head;
        while (f != NULL)
        {
            if (f->size > max_size)
            {
                ftmp = f;
                max_size = f->size;
            }
            f = f->next;
        }

        if (ftmp != NULL)
        {
            if (freelist_head == ftmp)
            {
                freelist_head = ftmp->next;
                if (ftmp->next) (ftmp->next)->prev = NULL;
            }
            else
            {
                if (ftmp->prev) (ftmp->prev)->next = ftmp->next;
                if (ftmp->next) (ftmp->next)->prev = ftmp->prev;
            }

            find_and_free_dregs_inside(ftmp->ptr, ftmp->size); 
            /* Free the actual data */
            (*vt_free_ptr)(ftmp->ptr);
            total_freelist_len -= (unsigned long long) ftmp->size;
            (*vt_free_ptr)(ftmp);

            if (test_dart_bound(len))
            {
                return(1);
            }
        }
        else return(1);
    }


    f = (*vt_malloc_ptr)(sizeof(struct freemem_list));
    if (f == NULL)
    {
        printf("Couldn't create freelist entry. Ignoring ...\n");
        return(1);
    }

    f->ptr = ptr;
    f->size = len;
    total_freelist_len += (unsigned long long) len;
    f->next = freelist_head;
    f->prev = NULL;
    if (freelist_head != NULL) freelist_head->prev = f;
    freelist_head = f;
    return(0);
}

/* Returns the closest match (in size) memory from the free list or 
   returns NULL */
static inline void *get_mem_fromfreelist(unsigned int size)
{
    unsigned int closest_match = 0xffffffff;
    struct freemem_list *f, *ftmp=NULL;
    void *ptr;
    if (size < VT_MIN_MEMTHRESHOLD) return(NULL);

    f = freelist_head;
    while (f != NULL)
    {
        if (f->size >= size && f->size - size < closest_match)
        {
            ftmp = f;
            closest_match = f->size - size;
        }
        f = f->next;
    }

    if (closest_match == 0xffffffff)
    {
        return(NULL);
    }

    if (closest_match > size && closest_match/size > FREELIST_MATCH_BOUND)
        return(NULL);


    if (freelist_head == ftmp)
    {
        freelist_head = ftmp->next;
        if (ftmp->next) (ftmp->next)->prev = NULL;
    }
    else
    {
        if (ftmp->prev) (ftmp->prev)->next = ftmp->next;
        if (ftmp->next) (ftmp->next)->prev = ftmp->prev;
    }

    ptr = ftmp->ptr;

    (*vt_free_ptr)(ftmp);
    total_freelist_len -= (unsigned long long) size;

    return(ptr);
}


/* Will return 1 if adding the memory to our free list and not
   actually freeing it will exceed the DART bound. We internally
   set a smaller DART bound */
static inline unsigned int test_dart_bound(unsigned int len)
{
    if ((unsigned long long) len + total_freelist_len > (unsigned long long)
            VT_MEMLIST_BOUND) return
        (1);
    return(0);
}

#endif                          /* MALLOC_HOOK */
