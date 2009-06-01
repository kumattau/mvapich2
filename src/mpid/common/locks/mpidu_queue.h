/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPIDU_QUEUE_H_INCLUDED
#define MPIDU_QUEUE_H_INCLUDED

#include "mpi.h"
#include "mpidu_atomic_primitives.h"

/* This value is used to indicate NULL in the MPIDU_Shm_asymm_base_addr
   variable.  It is non-zero because one of the likely base addresses is zero
   (indicating memory is actually symmetrically mapped).  The value 64 was used
   because it is unlikely that mmap will choose an address this low for a
   mapping.  */
/* XXX DJG TODO put some conditionally compiled error checking in that uses this */
#define MPIDU_SHM_ASYMM_NULL_VAL    64

extern char *MPIDU_Shm_asymm_base_addr;

/* Used to initialize the base address for relative pointers.  This interface
   assumes that there is only one shared memory segment.  If this turns out to
   not be the case in the future, we should probably add support for multiple
   shm segments.
   
   This function will return an error if it has already been called. */
int MPIDU_Shm_asymm_init(char *base);

/* Relative addressing macros.  These are for manipulating addresses relative
   to the start of a shared memory region. */
#define MPIDU_SHM_REL_NULL (0x0)
#define MPIDU_SHM_IS_REL_NULL(rel_ptr) ((rel_ptr).offset == MPIDU_SHM_REL_NULL)
#define MPIDU_SHM_SET_REL_NULL(rel_ptr) ((rel_ptr).offset = MPIDU_SHM_REL_NULL)
#define MPIDU_SHM_REL_ARE_EQUAL(rel_ptr1, rel_ptr2) ((rel_ptr1).offset == (rel_ptr2).offset)

/* This structure exists such that it is possible to expand the expressiveness
   of a relative address at some point in the future.  It also provides a
   modicum of type safety to help prevent certain flavors of errors.
   
   For example, instead of referencing an offset from a global base address, it
   might make sense for there to be multiple base addresses.  These base
   addresses could correspond to the start of a segment or region of shared
   memory.  This structure could store the segment number that is used to lookup
   a base address in a non-shared table.  Note that you would have to be very
   careful about all of this because if you add the segment number as a separate
   field you can no longer (compare and) swap a relative address atomically.  So
   you'll either have to shave bits from the pointer or make some sort of
   requirement that relative addresses can only be swapped within the same
   segment.  */
typedef struct MPIDU_Shm_rel_addr_t {
    volatile MPI_Aint offset;
} MPIDU_Shm_rel_addr_t;

/* converts a relative pointer to an absolute pointer */
#undef FUNCNAME
#define FUNCNAME MPIDU_Shm_rel_to_abs
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
void *MPIDU_Shm_rel_to_abs(MPIDU_Shm_rel_addr_t r)
{
    MPIU_Assert((MPI_Aint)MPIDU_Shm_asymm_base_addr != MPIDU_SHM_ASYMM_NULL_VAL);
    return (void*)(MPIDU_Shm_asymm_base_addr + r.offset);
}

/* converts an absolute pointer to a relative pointer */
#undef FUNCNAME
#define FUNCNAME MPIDU_Shm_abs_to_rel
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
MPIDU_Shm_rel_addr_t MPIDU_Shm_abs_to_rel(volatile void *a)
{
    MPIDU_Shm_rel_addr_t ret;

    MPIU_Assert((MPI_Aint)MPIDU_Shm_asymm_base_addr != MPIDU_SHM_ASYMM_NULL_VAL);
    ret.offset = (MPI_Aint)a - (MPI_Aint)MPIDU_Shm_asymm_base_addr;
    return ret;
}

#undef FUNCNAME
#define FUNCNAME MPIDU_Shm_swap_rel
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
MPIDU_Shm_rel_addr_t MPIDU_Shm_swap_rel(MPIDU_Shm_rel_addr_t *addr, MPIDU_Shm_rel_addr_t newv) {
    MPIDU_Shm_rel_addr_t oldv;
    oldv.offset = MPIDU_Atomic_swap_aint(&(addr->offset), newv.offset);
    return oldv;
}

/* Compare the relative pointer to (relative) null and swap if equal.  Prevents
   the guts of the _rel_addr_t abstraction from bleeding up into the
   enqueue/dequeue operations. */
#undef FUNCNAME
#define FUNCNAME MPIDU_Shm_cas_rel_null
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
MPIDU_Shm_rel_addr_t MPIDU_Shm_cas_rel_null(MPIDU_Shm_rel_addr_t *addr, MPIDU_Shm_rel_addr_t oldv) {
    MPIDU_Shm_rel_addr_t prev;
    prev.offset = MPIDU_Atomic_cas_aint(&(addr->offset), oldv.offset, MPIDU_SHM_REL_NULL);
    return prev;
}

/* The shadow head pointer makes this queue implementation perform much better
   than a standard queue.  Unfortunately, it also makes it a bit non-intuitive
   when read the code.  The following is an excerpt from "Design and Evaluation
   of Nemesis,  a Scalable, Low-Latency, Message-Passing Communication
   Subsystem" by D. Buntinas, G.  Mercier, and W. Gropp that gives an
   explanation:

      A process must access both the head and tail of the queue when it is
      enqueuing an element on an empty queue or when it is dequeuing an element
      that is the last element in the queue. In these cases, if the head and
      tail were in the same cache line, only one L2 cache miss would be
      encountered. If the queue has more elements in it, however, then the
      enqueuer only needs to access the tail, and the dequeuer only needs to
      access the head. If the head and tail were in the same cache line, then
      there would be L2 misses encountered as a result of false sharing each
      time a process enqueues an element after another has been dequeued from
      the same queue, and vice versa. In this case it would be better if the
      head and tail were in separate cache lines.

      Our solution is to put the head and tail in the same cache line and have a
      shadow head pointer in a separate cache line. The shadow head is
      initialized to NULL. The dequeuer uses the shadow head in place of the
      real head except when the shadow head is NULL, meaning that the queue has
      become empty. If the shadow head is NULL when the dequeuer tries to
      dequeue, it checks the value of the real head. If the real head is not
      NULL, meaning that an element has been enqueued on the queue since the
      last time the queue became empty, the dequeuer initializes its shadow head
      to the value of the real head and sets the real head to NULL. In this way,
      only one L2 cache miss is encountered when enqueuing onto an empty queue
      or dequeuing from a queue with one element. And because the tail and
      shadow head are in separate cache lines, there are no L2 cache misses from
      false sharing. 

      We found that using a shadow head pointer reduced one-way latency by about
      200 ns on a dual 2 GHz Xeon node.
*/

/* Pick an arbitrary cacheline size for now, we can setup a mechanism to detect
   it at build time later on.  This should work well on most intel systems at
   the very least. */
#define MPIDU_QUEUE_CACHELINE_PADDING 128

/* All absolute and relative pointers point to the start of the enclosing element. */
typedef struct MPIDU_Queue_info_t {
    MPIDU_Shm_rel_addr_t head; /* holds the offset pointer, not the abs ptr */
    MPIDU_Shm_rel_addr_t tail; /* holds the offset pointer, not the abs ptr */
    char padding1[MPIDU_QUEUE_CACHELINE_PADDING-2*sizeof(MPIDU_Shm_rel_addr_t)];
    MPIDU_Shm_rel_addr_t  shadow_head; /* holds the offset pointer, not the abs ptr */
    char padding2[MPIDU_QUEUE_CACHELINE_PADDING-sizeof(MPIDU_Shm_rel_addr_t)];
} MPIDU_Queue_info_t;

/* Using this header struct even though it's just one element gives us the
   opportunity to vary the implementation more easily in the future without
   updating all callers. */
typedef struct MPIDU_Queue_element_hdr_t {
    MPIDU_Shm_rel_addr_t next; /* holds the offset pointer, not the abs ptr */
} MPIDU_Queue_element_hdr_t;


/* Used to initialize a queue structure. */
void MPIDU_Queue_init(MPIDU_Queue_info_t *qhead);

/* Used to initialize a queue element header. */
void MPIDU_Queue_header_init(volatile MPIDU_Queue_element_hdr_t *hdr);

#undef FUNCNAME
#define FUNCNAME MPIDU_Queue_is_empty
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
int MPIDU_Queue_is_empty(MPIDU_Queue_info_t *qhead)
{
    int __ret = 0;
    if (MPIDU_SHM_IS_REL_NULL (qhead->shadow_head)) {
        if (MPIDU_SHM_IS_REL_NULL(qhead->head)) {
            __ret = 1;
        }
        else {
            qhead->shadow_head = qhead->head;
            MPIDU_SHM_SET_REL_NULL(qhead->head); /* reset it for next time */
        }
    }
    return __ret;
}

/* Returns a pointer to the element at the head of the queue.  The current
   implementation of these queue algorithms imposes several notable
   restrictions on the use of this function:
    - The caller must currently hold the critical section, just as if you were
      calling MPIDU_Queue_dequeue.  Failure to do could easily produce incorrect
      results.
    - MPIDU_Queue_is_empty must be called on this queue prior to calling this
      function.  Furthermore, there must be no intervening calls to any
      MPIDU_Queue_* functions (for this queue) between _is_empty and _peek_head.
      Failure to do so will produce incorrect results.

   This operation is effectively the same as the dequeue operation (insofar as
   it shares the same calling restrictions) but it does not disturb the actual
   contents of the queue. */
#undef FUNCNAME
#define FUNCNAME MPIDU_Queue_peek_head
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline
volatile void *MPIDU_Queue_peek_head(MPIDU_Queue_info_t *qhead_ptr)
{
    MPIU_Assert(qhead_ptr != NULL);

    if (MPIDU_SHM_IS_REL_NULL(qhead_ptr->shadow_head)) {
        return NULL;
    }
    return MPIDU_Shm_rel_to_abs(qhead_ptr->shadow_head);
}

/* This macro atomically enqueues an element (elt for short) into the queue
   indicated by qhead_ptr.  You need to pass several arguments:
     qhead_ptr - a pointer to a MPIDU_Queue_info_t structure to which the
                 element should be enqueued
     elt_ptr - a pointer to an element structure type that is to be enqueued
     elt_type - The base type of elt_ptr.  That is, if elt_ptr is a
                '(struct example_t *)' then elt_type is 'struct example_t'.
     elt_hdr_field - This is the member name of elt_type that is a
                     MPIDU_Queue_element_hdr_t.

    This queue implementation is loosely modeled after the linked lists used in
    the linux kernel.  You put the list header structure inside of the client
    structure, rather than the other way around.
   */
#define MPIDU_Queue_enqueue(qhead_ptr, elt_ptr, elt_type, elt_hdr_field) \
do {                                                                     \
    MPIDU_Shm_rel_addr_t r_prev;                                         \
    MPIDU_Shm_rel_addr_t r_elt = MPIDU_Shm_abs_to_rel(elt_ptr);          \
                                                                         \
    MPIDU_SHM_SET_REL_NULL((elt_ptr)->elt_hdr_field.next);               \
                                                                         \
    r_prev = MPIDU_Shm_swap_rel(&((qhead_ptr)->tail), r_elt);            \
                                                                         \
    if (MPIDU_SHM_IS_REL_NULL(r_prev)) {                                 \
        (qhead_ptr)->head = r_elt;                                       \
    }                                                                    \
    else {                                                               \
        elt_type *abs_prev = (elt_type *)MPIDU_Shm_rel_to_abs(r_prev);   \
        abs_prev->elt_hdr_field.next = r_elt;                            \
    }                                                                    \
} while (0)

/* This macro atomically dequeues an element (elt for short) from the queue
   indicated by qhead_ptr.  You need to pass several arguments:
     qhead_ptr - a pointer to a MPIDU_Queue_info_t structure to which the
                 element should be dequeued
     elt_ptr - A pointer to an element structure type that should be populated
               with the dequeued value.  Must be an lvalue.
     elt_type - The base type of elt_ptr.  That is, if elt_ptr is a
                '(struct example_t *)' then elt_type is 'struct example_t'.
     elt_hdr_field - This is the member name of elt_type that is a
                     MPIDU_Queue_element_hdr_t.

    This queue implementation is loosely modeled after the linked lists used in
    the linux kernel.  You put the list header structure inside of the client
    structure, rather than the other way around.
    
    XXX DJG NOTE: this operation is *NOT* thread-safe, it must be called from
                  within the critical section at the moment.
    XXX DJG NOTE: you must *always* call _is_empty() prior to this function */
#define MPIDU_Queue_dequeue(qhead_ptr, elt_ptr, elt_type, elt_hdr_field) \
do {                                                                         \
    register elt_type *_e;                                                   \
    register MPIDU_Shm_rel_addr_t _r_e;                                      \
                                                                             \
    /* TODO: fix the following macro in mpiimplthread.h, right now it just  
       doesn't do anything */                                                \
    MPIU_THREAD_SINGLE_CS_ASSERT_WITHIN("must hold critical section");       \
                                                                             \
    _r_e = (qhead_ptr)->shadow_head;                                         \
    _e = MPIDU_Shm_rel_to_abs(_r_e);                                         \
                                                                             \
    /* XXX DJG why isn't this a bug, _r_e could be rel null */               \
    if (!MPIDU_SHM_IS_REL_NULL(_e->elt_hdr_field.next)) {                    \
        (qhead_ptr)->shadow_head = _e->elt_hdr_field.next;                   \
    }                                                                        \
    else {                                                                   \
        MPIDU_Shm_rel_addr_t old_tail;                                       \
                                                                             \
        MPIDU_SHM_SET_REL_NULL((qhead_ptr)->shadow_head);                    \
                                                                             \
        old_tail = MPIDU_Shm_cas_rel_null(&((qhead_ptr)->tail), _r_e);       \
                                                                             \
        if (!MPIDU_SHM_REL_ARE_EQUAL(old_tail, _r_e)) {                      \
            while (MPIDU_SHM_IS_REL_NULL(_e->elt_hdr_field.next)) {          \
                MPIDU_Atomic_busy_wait();                                    \
            }                                                                \
            (qhead_ptr)->shadow_head = _e->elt_hdr_field.next;               \
        }                                                                    \
    }                                                                        \
    MPIDU_SHM_SET_REL_NULL(_e->elt_hdr_field.next);                          \
    elt_ptr = _e;                                                            \
} while (0)                                             

#endif /* MPIDU_QUEUE_H_INCLUDED */
