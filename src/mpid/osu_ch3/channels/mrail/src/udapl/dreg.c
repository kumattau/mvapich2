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

/* Copyright (c) 2003-2006, The Ohio State University. All rights
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
#include "pmi.h"
#include "dreg.h"
#include "udapl_util.h"
#include "assert.h"

#if (defined (MALLOC_HOOK) &&                                       \
        defined (VIADEV_RPUT_SUPPORT) &&                            \
        defined (LAZY_MEM_UNREGISTER))
/************************
 * For overriding malloc
 *************************/
#include <malloc.h>
/***************************
 * Global Hash Table
 ***************************/
Hash_Table *my_hash_table = NULL;
#endif /* MALLOC_HOOK && VIADEV_RPUT_SUPPORT &&
          LAZY_MEM_UNREGISTER */

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */

struct dreg_entry *dreg_table[DREG_HASHSIZE];

struct dreg_entry *dreg_free_list;
struct dreg_entry *dreg_unused_list;
struct dreg_entry *dreg_unused_tail;

static DAT_IA_HANDLE _dreg_nic;
static DAT_PZ_HANDLE _dreg_ptag;

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

void
dreg_init (DAT_IA_HANDLE nic, DAT_PZ_HANDLE ptag)
{
    int i;

    /* Setup original malloc hooks */
    SET_ORIGINAL_MALLOC_HOOKS;

    for (i = 0; i < DREG_HASHSIZE; i++)
      {
          dreg_table[i] = NULL;
      }

    dreg_free_list = (dreg_entry *)
        malloc (sizeof (dreg_entry) * udapl_ndreg_entries);
    if (dreg_free_list == NULL)
      {
          udapl_error_abort (GEN_EXIT_ERR,
                             "dreg_init: unable to malloc %d bytes",
                             (int) sizeof (dreg_entry) * udapl_ndreg_entries);
      }

    SAVE_MALLOC_HOOKS;

    /* Setup our hooks again */
    SET_MVAPICH_MALLOC_HOOKS;


    for (i = 0; i < udapl_ndreg_entries - 1; i++)
      {
          dreg_free_list[i].next = &dreg_free_list[i + 1];
      }
    dreg_free_list[udapl_ndreg_entries - 1].next = NULL;

    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;
    _dreg_nic = nic;
    _dreg_ptag = ptag;
}

/* will return a NULL pointer if registration fails */
dreg_entry *
dreg_register (void *buf, int len)
{
    struct dreg_entry *d;
    int rc;
    d = dreg_find (buf, len);

    if (d != NULL)
      {
          D_PRINT ("find an old one \n");
          dreg_incr_refcount (d);
          D_PRINT ("dreg_register: found registered buffer for "
                   AINT_FORMAT " length %d", (aint_t) buf, len);
      }
    else
      {
          while ((d = dreg_new_entry (buf, len)) == NULL)
            {
                /* either was not able to obtain a dreg_entry data strucrure
                 * or was not able to register memory.  In either case,
                 * attempt to evict a currently unused entry and try again.
                 */
                D_PRINT ("dreg_register: no entries available. Evicting.");
                rc = dreg_evict ();
                if (rc == 0)
                  {
                      /* could not evict anything, will not be able to
                       * register this memory.  Return failure.
                       */
                      D_PRINT ("dreg_register: evict failed, cant register");
                      return NULL;
                  }
                /* eviction successful, try again */
            }
          dreg_incr_refcount (d);
          D_PRINT ("dreg_register: created new entry for buffer "
                   AINT_FORMAT " length %d", (aint_t) buf, len);
      }

    return d;
}

void
dreg_unregister (dreg_entry * d)
{
    dreg_decr_refcount (d);
}


dreg_entry *
dreg_find (void *buf, int len)
{


    /* this should be consolidated with dreg_register, where
     * the same calculation for number of pages is done. 
     */

    dreg_entry *d = dreg_table[DREG_HASH (buf)];
    aint_t pagebase_a, buf_a, pagenum;
    int npages;

    if (NULL == d)
      {
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

    npages = 1 + ((buf_a + (aint_t) len - 1 - pagebase_a) >> DREG_PAGEBITS);

    while (d != NULL)
      {
          if (d->pagenum == pagenum && d->npages >= npages)
              break;
          d = d->next;
      }

    if (d != NULL)
      {
          D_PRINT ("dreg_find: found entry pagebase="
                   AINT_FORMAT " npages=%d",
                   d->pagenum << DREG_PAGEBITS, d->npages);
      }

    return d;
}


/*
 * get a fresh entry off the free list. 
 * Ok to return NULL. Higher levels will deal with it. 
 */

dreg_entry *
dreg_get ()
{
    dreg_entry *d;
    DREG_GET_FROM_FREE_LIST (d);

    if (d != NULL)
      {
          d->refcount = 0;
          d->next_unused = NULL;
          d->prev_unused = NULL;
          d->next = NULL;
      }
    else
      {
          D_PRINT ("dreg_get: no free dreg entries");
      }
    return (d);
}

void
dreg_release (dreg_entry * d)
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
void
dreg_decr_refcount (dreg_entry * d)
{
#ifndef LAZY_MEM_UNREGISTER
    DAT_RETURN rc;
    void *buf;
    aint_t bufint;
#endif

    assert (d->refcount > 0);
    d->refcount--;
    if (d->refcount == 0)
      {
#ifdef LAZY_MEM_UNREGISTER
          DREG_ADD_TO_UNUSED_LIST (d);
#else
          bufint = d->pagenum << DREG_PAGEBITS;
          buf = (void *) bufint;

          rc = dat_lmr_free (d->memhandle.hndl);
          if (rc != DAT_SUCCESS)
            {
                udapl_error_abort (UDAPL_RETURN_ERR,
                                   "UDAPL error[%d] \"%s\" in routine dreg_decr_refcount: dat_lmr_free",
                                   rc);
            }
          DREG_DELETE_FROM_HASH_TABLE (d);
          DREG_ADD_TO_FREE_LIST (d);
#endif

      }
    D_PRINT ("decr_refcount: entry " AINT_FORMAT
             " refcount" AINT_FORMAT " memhandle" AINT_FORMAT,
             (aint_t) d, (aint_t) d->refcount, (aint_t) d->memhandle.hndl);
}

/*
 * Increment reference count on a dreg entry. If reference count
 * was zero and it was on the unused list (meaning it had been
 * previously used, and the refcount had been decremented),
 * we should take it off
 */

void
dreg_incr_refcount (dreg_entry * d)
{
    assert (d != NULL);
    if (d->refcount == 0)
      {
          DREG_REMOVE_FROM_UNUSED_LIST (d);
      }
    d->refcount++;
}

/*
 * Evict a registration. This means delete it from the unused list, 
 * add it to the free list, and deregister the associated memory.
 * Return 1 if success, 0 if nothing to evict.
 */

int
dreg_evict ()
{
    int rc;
    void *buf;
    dreg_entry *d;
    aint_t bufint;

    d = dreg_unused_tail;
    if (d == NULL)
      {
          /* no entries left on unused list, return failure */
          return 0;
      }
    DREG_REMOVE_FROM_UNUSED_LIST (d);

    assert (d->refcount == 0);

    bufint = d->pagenum << DREG_PAGEBITS;
    buf = (void *) bufint;


    D_PRINT ("dreg_evict: deregistering address="
             AINT_FORMAT " memhandle = %x",
             bufint, (unsigned int) d->memhandle.hndl);

    rc = dat_lmr_free (d->memhandle.hndl);
    if (rc != DAT_SUCCESS)
      {
          udapl_error_abort (UDAPL_RETURN_ERR,
                             "uDAPL error: cannot free lmr");
      }

    D_PRINT ("dreg_evict: dat_lmr_free\n");

    DREG_DELETE_FROM_HASH_TABLE (d);
    DREG_ADD_TO_FREE_LIST (d);
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */

dreg_entry *
dreg_new_entry (void *buf, int len)
{


    dreg_entry *d;
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
    unsigned long register_nbytes;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN reg_size;
    DAT_VADDR reg_addr;

    d = dreg_get ();
    if (NULL == d)
      {
          return d;
      }

    /* calculate base page address for registration */
    user_low_a = (aint_t) buf;
    user_high_a = user_low_a + (aint_t) len - 1;

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = 1 + (pagenum_high - pagenum_low);

    pagebase_low_p = (void *) pagebase_low_a;
    register_nbytes = npages * DREG_PAGESIZE;

    region.for_va = pagebase_low_p;
    /* add later */
    rc = dat_lmr_create (_dreg_nic,
                         DAT_MEM_TYPE_VIRTUAL, region, register_nbytes,
                         _dreg_ptag, DAT_MEM_PRIV_ALL_FLAG,
                         &d->memhandle.hndl, &d->memhandle.lkey,
                         &d->memhandle.rkey, &reg_size, &reg_addr);

    /* if not success, return NULL to indicate that we were unable to
     * register this memory.  */
    if (rc != DAT_SUCCESS)
      {
          dreg_release (d);
          return NULL;
      }



    d->pagenum = pagenum_low;
    d->npages = npages;

    DREG_ADD_TO_HASH_TABLE (pagebase_low_a, d);

    return d;

}


#if (defined (MALLOC_HOOK) &&                                       \
        defined (VIADEV_RPUT_SUPPORT) &&                            \
        defined (LAZY_MEM_UNREGISTER))
/* Overriding normal malloc init */
void
mvapich_init_malloc_hook (void)
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
void *
mvapich_malloc_hook (size_t size, const void *caller)
{
    void *result;
    unsigned int hash_value;
    Hash_Symbol *new_symbol = NULL;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;

    /* Restore old hooks */
    SET_ORIGINAL_MALLOC_HOOKS;

    /* Call the real malloc */
    result = malloc (size);

    /* Now we have to put this in the Hash Table */

    /* Check whether we've created the HT or not ! */
    if (my_hash_table != NULL)
      {
          hash_value = hash ((aint_t) result);

          if (NULL == my_hash_table[hash_value].symbol)
            {
                /* First element */
                new_symbol = (Hash_Symbol *) malloc (sizeof (Hash_Symbol));
                new_symbol->mem_ptr = result;
                new_symbol->len = (unsigned int) size;
                new_symbol->next = NULL;

                my_hash_table[hash_value].symbol = (void *) new_symbol;
            }
          else
            {
                /* There are other elements, walk to end of list
                 * and then add at the end
                 */
                for (sym_i = (Hash_Symbol *) my_hash_table[hash_value].symbol;
                     sym_i != NULL; sym_i = sym_i->next)
                  {

                      sym_temp = sym_i;

                  }
                /* sym_temp is now pointing to the last element */
                new_symbol = (Hash_Symbol *) malloc (sizeof (Hash_Symbol));
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
void
mvapich_free_hook (void *ptr, const void *caller)
{
    unsigned int hash_value;
    int found_flag = 0;
    Hash_Symbol *sym_i = NULL;
    Hash_Symbol *sym_temp = NULL;

    /* Restore all old hooks */
    SET_ORIGINAL_MALLOC_HOOKS;
    /* We have to look for the buffer we had put in
     * the HT */
    if (my_hash_table != NULL)
      {
          /*
           * OK, we've set up the table.
           * But is this buffer in it ?
           * If it is, then return the length *and* free the buffer
           */
          hash_value = hash ((aint_t) ptr);
          if (NULL == my_hash_table[hash_value].symbol)
            {
                /* It cannot possibly be in our HT */
                free (ptr);
            }
          else
            {
                /* It is here ... but where is it hiding ? */

                /* Check the first one */
                sym_i = (Hash_Symbol *) my_hash_table[hash_value].symbol;
                if (sym_i->mem_ptr == ptr)
                  {
                      /* Found it ! */
                      find_and_free_dregs_inside (ptr, sym_i->len);
                      if (NULL == sym_i->next)
                        {
                            /* There are no more entries */
                            free (sym_i);
                            free (ptr);
                            my_hash_table[hash_value].symbol = NULL;
                        }
                      else
                        {
                            sym_temp = sym_i->next;
                            my_hash_table[hash_value].symbol = sym_temp;
                            free (sym_i);
                            free (ptr);
                        }
                  }
                else
                  {
                      /* Follow the link list */
                      /* sym_temp always points one behind sym_i */
                      sym_temp =
                          (Hash_Symbol *) my_hash_table[hash_value].symbol;

                      for (sym_i = sym_temp->next;
                           sym_i != NULL; sym_i = sym_i->next)
                        {
                            if (sym_i->mem_ptr == ptr)
                              {
                                  /* Found it ! */
                                  find_and_free_dregs_inside (ptr,
                                                              sym_i->len);

                                  if (sym_i->next != NULL)
                                    {
                                        /* There is more to follow */
                                        sym_temp->next = sym_i->next;
                                        free (sym_i);
                                        free (ptr);
                                        found_flag = 1;
                                        break;
                                    }
                                  else
                                    {
                                        /* This was the last one -- whew ! */
                                        sym_temp->next = NULL;
                                        free (sym_i);
                                        free (ptr);
                                        found_flag = 1;
                                        break;
                                    }
                              }
                            if (!found_flag)
                              {
                                  sym_temp = sym_i;
                              }
                        }
                      /* Wasn't the first. Did we find it in the link list search ?
                       *         * found_flag tells all !
                       *                 */
                      if (!found_flag)
                        {
                            free (ptr);
                        }
                  }
            }
      }
    else
      {
          /* Don't bother, just free the stuff */
          free (ptr);
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
unsigned int
hash (unsigned int key)
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
void
create_hash_table (void)
{
    int i = 0;

    /* This malloc doesn't go in the Hash Table */
    SET_ORIGINAL_MALLOC_HOOKS;

    my_hash_table = (Hash_Table *)
        malloc (sizeof (Hash_Table) * HASH_TABLE_SIZE);
    if (NULL == my_hash_table)
      {
          udapl_error_abort (GEN_EXIT_ERR,
                             "Malloc failed, not enough space for Hash Table!\n");
      }

    /* Save the old hooks */
    SAVE_MALLOC_HOOKS;

    /* Initialize Hash Table */
    for (i = 0; i < HASH_TABLE_SIZE; i++)
      {
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
dreg_entry *
is_dreg_registered (void *buf)
{
    dreg_entry *d = NULL;
    d = dreg_table[DREG_HASH (buf)];
    return d;
}

/*
 * Finds if any portion of the buffer was
 * registered before. If it was then deregister it
 * otherwise leave it alone and return
 */
void
find_and_free_dregs_inside (void *ptr, int len)
{
    int i = 0;
    DAT_RETURN rc = 0;
    dreg_entry *d;
    int npages = 0;
    int pagesize = 0;
    int pagebase_a = 0;
    unsigned int buf_a;

    buf_a = (aint_t) ptr;
    pagebase_a = buf_a & ~DREG_PAGEMASK;
    npages = 1 + ((buf_a + (aint_t) len - pagebase_a - 1) >> DREG_PAGEBITS);
    for (i = 0; i < npages; i++)
      {
          d = dreg_find ((void *) ((aint_t) ptr + i * pagesize), pagesize);
          if (d)
            {
                if (d->refcount != 0)
                  {
                      /* Forcefully free it */
                      rc = dat_lmr_free (d->memhandle.hndl);
                      if (rc != DAT_SUCCESS)
                        {
                            udapl_error_abort (UDAPL_RETURN_ERR,
                                               "uDAPL error in find_and_free_dregs_inside: cannot create lmr");
                        }
                      DREG_DELETE_FROM_HASH_TABLE (d);
                      DREG_ADD_TO_FREE_LIST (d);
                  }
                else
                  {
                      /* Safe to remove from unused list */
                      DREG_REMOVE_FROM_UNUSED_LIST (d);
                      rc = dat_lmr_free (d->memhandle.hndl);
                      if (rc != DAT_SUCCESS)
                        {
                            udapl_error_abort (UDAPL_RETURN_ERR,
                                               "uDAPL error in find_and_free_dregs_inside: cannot create lmr");
                        }

                      DREG_DELETE_FROM_HASH_TABLE (d);
                      DREG_ADD_TO_FREE_LIST (d);
                  }
            }
      }
}

#endif /* MALLOC_HOOK */
