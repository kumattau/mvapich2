/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: commutil.c,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
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
#include "mpiimpl.h"
#include "mpicomm.h"

/* This is the utility file for comm that contains the basic comm items
   and storage management */
#ifndef MPID_COMM_PREALLOC 
#define MPID_COMM_PREALLOC 8
#endif

/* Preallocated comm objects */
MPID_Comm MPID_Comm_builtin[MPID_COMM_N_BUILTIN] = { {0} };
MPID_Comm MPID_Comm_direct[MPID_COMM_PREALLOC] = { {0} };
MPIU_Object_alloc_t MPID_Comm_mem = { 0, 0, 0, 0, MPID_COMM, 
				      sizeof(MPID_Comm), MPID_Comm_direct,
                                      MPID_COMM_PREALLOC};

/* FIXME :
   Reusing context ids can lead to a race condition if (as is desirable)
   MPI_Comm_free does not include a barrier.  Consider the following:
   Process A frees the communicator.
   Process A creates a new communicator, reusing the just released id
   Process B sends a message to A on the old communicator.
   Process A receives the message, and believes that it belongs to the
   new communicator.
   Process B then cancels the message, and frees the communicator.

   The likelyhood of this happening can be reduced by introducing a gap
   between when a context id is released and when it is reused.  An alternative
   is to use an explicit message (in the implementation of MPI_Comm_free)
   to indicate that a communicator is being freed; this will often require
   less communication than a barrier in MPI_Comm_free, and will ensure that 
   no messages are later sent to the same communicator (we may also want to
   have a similar check when building fault-tolerant versions of MPI).
 */

/* Create a new communicator with a context.  
   Do *not* initialize the other fields except for the reference count.
   See MPIR_Comm_copy for a function to produce a copy of part of a
   communicator 
*/
/*  FIXME : comm_create can't use this because the context id must be
   created separately from the communicator (creating the context
   is collective over oldcomm_ptr, but this routine may be called only
   by a subset of processes in the new communicator)

   Only Comm_split currently uses this
 */
int MPIR_Comm_create( MPID_Comm *oldcomm_ptr, MPID_Comm **newcomm_ptr )
{   
    int mpi_errno, new_context_id;
    MPID_Comm *newptr;

    newptr = (MPID_Comm *)MPIU_Handle_obj_alloc( &MPID_Comm_mem );
    /* --BEGIN ERROR HANDLING-- */
    if (!newptr) {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
		   "MPIR_Comm_create", __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    *newcomm_ptr = newptr;
    MPIU_Object_set_ref( newptr, 1 );

    /* If there is a context id cache in oldcomm, use it here.  Otherwise,
       use the appropriate algorithm to get a new context id */
    newptr->context_id = new_context_id = 
	MPIR_Get_contextid( oldcomm_ptr );
    newptr->attributes = 0;
    /* --BEGIN ERROR HANDLING-- */
    if (new_context_id == 0) {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                 "MPIR_Comm_create", __LINE__, MPI_ERR_OTHER,
					  "**toomanycomm", 0 );
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    /* Insert this new communicator into the list of known communicators.
       Make this conditional on debugger support to match the test in
       MPIR_Comm_release . */
    MPIR_COMML_REMEMBER( newptr );

    return 0;
}

/* Create a local intra communicator from the local group of the 
   specified intercomm. */
/* FIXME : 
   For the context id, use the intercomm's context id + 2.  (?)
 */
int MPIR_Setup_intercomm_localcomm( MPID_Comm *intercomm_ptr )
{
    MPID_Comm *localcomm_ptr;
    int mpi_errno;

    localcomm_ptr = (MPID_Comm *)MPIU_Handle_obj_alloc( &MPID_Comm_mem );
    /* --BEGIN ERROR HANDLING-- */
    if (!localcomm_ptr) {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                 "MPIR_Setup_intercomm_localcomm", __LINE__,
					  MPI_ERR_OTHER, "**nomem", 0 );
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    MPIU_Object_set_ref( localcomm_ptr, 1 );
    /* Note that we must not free this context id since we are sharing it
       with the intercomm's context */
    /* FIXME: This was + 2 (in agreement with the docs) but that
       caused some errors with an apparent use of the same context id
       by operations in different communicators.  Switching this to +1
       seems to have fixed that problem, but this isn't the right answer. */
    localcomm_ptr->context_id = intercomm_ptr->context_id + 1;

    /* Duplicate the VCRT references */
    MPID_VCRT_Add_ref( intercomm_ptr->local_vcrt );
    localcomm_ptr->vcrt = intercomm_ptr->local_vcrt;
    localcomm_ptr->vcr  = intercomm_ptr->local_vcr;

    /* Save the kind of the communicator */
    localcomm_ptr->comm_kind   = MPID_INTRACOMM;
    
    /* Set the sizes and ranks */
    localcomm_ptr->remote_size = intercomm_ptr->local_size;
    localcomm_ptr->local_size  = intercomm_ptr->local_size;
    localcomm_ptr->rank        = intercomm_ptr->rank;

    /* More advanced version: if the group is available, dup it by 
       increasing the reference count */
    localcomm_ptr->local_group  = 0;
    localcomm_ptr->remote_group = 0;

    /* This is an internal communicator, so ignore */
    localcomm_ptr->errhandler = 0;
    
    /* FIXME  : No local functions for the collectives */
    localcomm_ptr->coll_fns = 0;

    /* FIXME  : No local functions for the topology routines */
    localcomm_ptr->topo_fns = 0;

    /* We do *not* inherit any name */
    localcomm_ptr->name[0] = 0;

    localcomm_ptr->attributes = 0;

    intercomm_ptr->local_comm = localcomm_ptr;
    return 0;
}
/*
 * Here are the routines to find a new context id.  The algorithm is discussed 
 * in detail in the mpich2 coding document.  There are versions for
 * single threaded and multithreaded MPI.
 * 
 * These assume that int is 32 bits; they should use uint_32 instead, 
 * and an MPI_UINT32 type (should be able to use MPI_INTEGER4)
 */

/* Both the threaded and non-threaded routines use the same mask of available
   context id values. */
#define MAX_CONTEXT_MASK 32
static unsigned int context_mask[MAX_CONTEXT_MASK];
static int initialize_context_mask = 1;

#ifdef MPICH_DEBUG_INTERNAL
static void MPIR_PrintContextMask( FILE *fp )
{
    int i;
    int maxset=0;
    for (i=MAX_CONTEXT_MASK-1; i>=0; i--) {
	if (context_mask[i] != 0) break;
    }
    maxset = i;
    DBG_FPRINTF( fp, "Context mask: " );
    for (i=0; i<maxset; i++) {
	DBG_FPRINTF( fp, "%.8x ", context_mask[i] );
    }
    DBG_FPRINTF( fp, "\n" );
}
#endif

static void MPIR_Init_contextid (void) {
    int i;
    for (i=1; i<MAX_CONTEXT_MASK; i++) {
	context_mask[i] = 0xFFFFFFFF;
    }
    /* the first two values are already used */
    context_mask[0] = 0xFFFFFFFC; 
    initialize_context_mask = 0;
}
/* Return the context id corresponding to the first set bit in the mask.
   Return 0 if no bit found */
static int MPIR_Find_context_bit( unsigned int local_mask[] ) {
    int i, j, context_id = 0;
    for (i=0; i<MAX_CONTEXT_MASK; i++) {
	if (local_mask[i]) {
	    /* There is a bit set in this word. */
	    register unsigned int val, nval;
	    /* The following code finds the highest set bit by recursively
	       checking the top half of a subword for a bit, and incrementing
	       the bit location by the number of bit of the lower sub word if 
	       the high subword contains a set bit.  The assumption is that
	       full-word bitwise operations and compares against zero are 
	       fast */
	    val = local_mask[i];
	    j   = 0;
	    nval = val & 0xFFFF0000;
	    if (nval) {
		j += 16;
		val = nval;
	    }
	    nval = val & 0xFF00FF00;
	    if (nval) {
		j += 8;
		val = nval;
	    }
	    nval = val & 0xF0F0F0F0;
	    if (nval) {
		j += 4;
		val = nval;
	    }
	    nval = val & 0xCCCCCCCC;
	    if (nval) {
		j += 2;
		val = nval;
	    }
	    if (val & 0xAAAAAAAA) { 
		j += 1;
	    }
	    context_mask[i] &= ~(1<<j);
	    context_id = 4 * (32 * i + j);
#ifdef MPICH_DEBUG_INTERNAL
	    if (MPIR_IDebug("context")) {
		DBG_FPRINTF( stderr, "allocating contextid = %d\n", context_id ); 
		DBG_FPRINTF( stderr, "(mask[%d], bit %d\n", i, j );
	    }
#endif
	    return context_id;
	}
    }
    return 0;
}
#if MPID_MAX_THREAD_LEVEL <= MPI_THREAD_SERIALIZED
/* Unthreaded (only one MPI call active at any time) */

int MPIR_Get_contextid( MPID_Comm *comm_ptr )
{
    int          context_id = 0;
    unsigned int local_mask[MAX_CONTEXT_MASK];

    if (initialize_context_mask) {
	MPIR_Init_contextid();
    }
    memcpy( local_mask, context_mask, MAX_CONTEXT_MASK * sizeof(int) );
    MPIR_Nest_incr();
#ifdef _SMP_
    comm_ptr->shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
#endif
    /* Comm must be an intracommunicator */
    NMPI_Allreduce( MPI_IN_PLACE, local_mask, MAX_CONTEXT_MASK, MPI_INT, 
		    MPI_BAND, comm_ptr->handle );
    MPIR_Nest_decr();

    context_id = MPIR_Find_context_bit( local_mask );

    /* return 0 if no context id found.  The calling routine should 
       check for this and generate the appropriate error code */
#ifdef MPICH_DEBUG_INTERNAL
    if (MPIR_IDebug("context"))
	MPIR_PrintContextMask( stderr );
#endif
    return context_id;
}

#else

/* Additional values needed to maintain thread safety */
static volatile int mask_in_use = 0;
/* lowestContextId is used to break ties when multiple threads
   are contending for the mask */
#define MPIR_MAXID (1 << 30)
static volatile int lowestContextId = MPIR_MAXID;

int MPIR_Get_contextid( MPID_Comm *comm_ptr )
{
    int          context_id = 0;
    unsigned int local_mask[MAX_CONTEXT_MASK];
    int          own_mask = 0;

    /* We lock only around access to the mask.  If another thread is
       using the mask, we take a mask of zero */
    MPIU_DBG_PRINTF_CLASS( MPIU_DBG_COMM | MPIU_DBG_CONTEXTID, 1,
			   ( "Entering; shared state is %d:%d\n", mask_in_use, 
			     lowestContextId ) );
    while (context_id == 0) {
	MPID_Common_thread_lock();
	if (initialize_context_mask) {
	    MPIR_Init_contextid();
	}
	if (mask_in_use || comm_ptr->context_id > lowestContextId) {
	    memset( local_mask, 0, MAX_CONTEXT_MASK * sizeof(int) );
	    if (comm_ptr->context_id < lowestContextId) {
		lowestContextId = comm_ptr->context_id;
	    }
	    MPIU_DBG_PRINTF_CLASS( MPIU_DBG_COMM | MPIU_DBG_CONTEXTID, 2,
				   ( "In in-use, sed lowestContextId to %d\n", 
				     lowestContextId ) );
	}
	else {
	    memcpy( local_mask, context_mask, MAX_CONTEXT_MASK * sizeof(int) );
	    mask_in_use     = 1;
	    own_mask        = 1;
	    lowestContextId = comm_ptr->context_id;
	    MPIU_DBG_PRINTF_CLASS( MPIU_DBG_COMM | MPIU_DBG_CONTEXTID, 2, 
				   ( "Copied local_mask\n" ) );
	}
	MPID_Common_thread_unlock();
	
	/* Now, try to get a context id */
	MPIR_Nest_incr();
#ifdef _SMP_
    comm_ptr->shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
#endif
	/* Comm must be an intracommunicator */
	NMPI_Allreduce( MPI_IN_PLACE, local_mask, MAX_CONTEXT_MASK, MPI_INT, 
			MPI_BAND, comm_ptr->handle );
	MPIR_Nest_decr();

	if (own_mask) {
	    /* There is a chance that we've found a context id */
	    MPID_Common_thread_lock();
	    /* Find_context_bit updates the context array if it finds a match */
	    context_id = MPIR_Find_context_bit( local_mask );
	    MPIU_DBG_PRINTF_CLASS( MPIU_DBG_COMM | MPIU_DBG_CONTEXTID, 1, 
				   ( "Context id is now %d\n", context_id ) );
	    if (context_id > 0) {
		/* If we were the lowest context id, reset the value to
		   allow the other threads to compete for the mask */
		if (lowestContextId == comm_ptr->context_id) {
		    lowestContextId = MPIR_MAXID;
		    /* Else leave it alone; there is another thread waiting */
		}
	    }
	    /* else we did not find a context id. Give up the mask in case
	       there is another thread (with a lower context id) waiting for
	       it */
	    mask_in_use = 0;
	    MPID_Common_thread_unlock();
	}
    }

    return context_id;
}
#endif

/* Get a context for a new intercomm.  There are two approaches 
   here (for MPI-1 codes only)
   (a) Each local group gets a context; the groups exchange, and
       the low value is accepted and the high one returned.  This
       works because the context ids are taken from the same pool.
   (b) Form a temporary intracomm over all processes and use that
       with the regular algorithm.
   
   In some ways, (a) is the better approach because it is the one that
   extends to MPI-2 (where the last step, returning the context, is 
   not used and instead separate send and receive context id value 
   are kept).  For this reason, we'll use (a).

   Even better is to separate the local and remote context ids.  Then
   each group of processes can manage their context ids separately.
*/
/* FIXME : This approach for intercomm context will not work for MPI-2
 *
 * This uses the thread-safe (if necessary) routine to get a context id
 * and does not need its own thread-safe version.
 */
int MPIR_Get_intercomm_contextid( MPID_Comm *comm_ptr )
{
    int context_id, remote_context_id, final_context_id;
    int tag = 31567; /* FIXME  - we need an internal tag or 
		        communication channel.  Can we use a different
		        context instead?.  Or can we use the tag 
		        provided in the intercomm routine? (not on a dup, 
			but in that case it can use the collective context) */

    if (!comm_ptr->local_comm) {
	/* Manufacture the local communicator */
	MPIR_Setup_intercomm_localcomm( comm_ptr );
    }

    /*printf( "local comm size is %d and intercomm local size is %d\n",
      comm_ptr->local_comm->local_size, comm_ptr->local_size );*/
    context_id = MPIR_Get_contextid( comm_ptr->local_comm );
    if (context_id == 0) return 0;

    /* MPIC routine uses an internal context id.  The local leads (process 0)
       exchange data */
    remote_context_id = -1;
    if (comm_ptr->rank == 0) {
	MPIC_Sendrecv( &context_id, 1, MPI_INT, 0, tag,
		       &remote_context_id, 1, MPI_INT, 0, tag, 
		       comm_ptr->handle, MPI_STATUS_IGNORE );

	/* FIXME : We need to do something with the context ids.  For 
	   MPI1, we can just take the min of the two context ids and
	   use that value.  For MPI2, we'll need to have separate
	   send and receive context ids */
	if (remote_context_id < context_id)
	    final_context_id = remote_context_id;
	else 
	    final_context_id = context_id;
    }

    /* Make sure that all of the local processes now have this
       id */
    MPIR_Nest_incr();
    NMPI_Bcast( &final_context_id, 1, MPI_INT, 
		0, comm_ptr->local_comm->handle );
    MPIR_Nest_decr();
    /* FIXME : If we did not choose this context, free it.  We won't do this
       once we have MPI2 intercomms (at least, not for intercomms that
       are not subsets of MPI_COMM_WORLD) */
    if (final_context_id != context_id) {
	MPIR_Free_contextid( context_id );
    }
    /* printf( "intercomm context = %d\n", final_context_id ); */
    return final_context_id;
}

void MPIR_Free_contextid( int context_id )
{
    int idx, bitpos;
    /* Convert the context id to the bit position */
    /* printf( "Freed id = %d\n", context_id ); */
    context_id >>= 2;       /* Remove the shift of a factor of four */
    idx    = context_id / 32;
    bitpos = context_id % 32;

    /* --BEGIN ERROR HANDLING-- */
    if (idx < 0 || idx >= MAX_CONTEXT_MASK) {
	MPID_Abort( 0, MPI_ERR_INTERN, 1, 
		    "In MPIR_Free_contextid, idx is out of range" );
    }
    /* --END ERROR HANDLING-- */
    /* This update must be done atomically in the multithreaded case */
#if MPID_MAX_THREAD_LEVEL <= MPI_THREAD_SERIALIZED
    context_mask[idx] |= (0x1 << bitpos);
#else
    MPID_Common_thread_lock();
    context_mask[idx] |= (0x1 << bitpos);
    MPID_Common_thread_unlock();
#endif

#ifdef MPICH_DEBUG_INTERNAL
    if (MPIR_IDebug("context")) {
	DBG_FPRINTF( stderr, "Freed context %d\n", context_id );
	DBG_FPRINTF( stderr, "mask[%d] bit %d\n", idx, bitpos );
    }
#endif
}

/*
 * Copy a communicator, including creating a new context and copying the
 * virtual connection tables and clearing the various fields.
 * Does *not* copy attributes.  If size is < the size of the input
 * communicator, copy only the first size elements.  If this process
 * is not a member, return a null pointer in outcomm_ptr.
 *
 * Used by comm_create, cart_create, graph_create, and dup_create 
 */
int MPIR_Comm_copy( MPID_Comm *comm_ptr, int size, MPID_Comm **outcomm_ptr )
{
    int mpi_errno = MPI_SUCCESS;
    int new_context_id;
    MPID_Comm *newcomm_ptr;

    /* Get a new context first.  We need this to be collective over the
       input communicator */
    /* If there is a context id cache in oldcomm, use it here.  Otherwise,
       use the appropriate algorithm to get a new context id.  Be careful
       of intercomms here */
    if (comm_ptr->comm_kind == MPID_INTERCOMM) {
	new_context_id = MPIR_Get_intercomm_contextid( comm_ptr );
    }
    else {
	new_context_id = MPIR_Get_contextid( comm_ptr );
    }
    /* --BEGIN ERROR HANDLING-- */
    if (new_context_id == 0) {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
               "MPIR_Comm_copy", __LINE__, MPI_ERR_OTHER, "**toomanycomm", 0 );
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    if (comm_ptr->rank >= size) {
	*outcomm_ptr = 0;
	return MPI_SUCCESS;
    }

    /* We're left with the processes that will have a non-null communicator.
       Create the object, initialize the data, and return the result */

    newcomm_ptr = (MPID_Comm *)MPIU_Handle_obj_alloc( &MPID_Comm_mem );
    /* --BEGIN ERROR HANDLING-- */
    if (!newcomm_ptr) {
	mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                     "MPIR_Comm_copy", __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    MPIU_Object_set_ref( newcomm_ptr, 1 );
    newcomm_ptr->context_id = new_context_id;

    /* Save the kind of the communicator */
    newcomm_ptr->comm_kind   = comm_ptr->comm_kind;
    newcomm_ptr->local_comm  = 0;
    
    /* Duplicate the VCRT references */
    MPID_VCRT_Add_ref( comm_ptr->vcrt );
    newcomm_ptr->vcrt = comm_ptr->vcrt;
    newcomm_ptr->vcr  = comm_ptr->vcr;

    /* If it is an intercomm, duplicate the local vcrt references */
    if (comm_ptr->comm_kind == MPID_INTERCOMM) {
	MPID_VCRT_Add_ref( comm_ptr->local_vcrt );
	newcomm_ptr->local_vcrt = comm_ptr->local_vcrt;
	newcomm_ptr->local_vcr  = comm_ptr->local_vcr;
    }

    /* Set the sizes and ranks */
    newcomm_ptr->remote_size = comm_ptr->remote_size;
    newcomm_ptr->rank        = comm_ptr->rank;
    newcomm_ptr->local_size  = comm_ptr->local_size;

    /* More advanced version: if the group is available, dup it by 
       increasing the reference count */
    newcomm_ptr->local_group  = 0;
    newcomm_ptr->remote_group = 0;

    /* Inherit the error handler (if any) */
    newcomm_ptr->errhandler = comm_ptr->errhandler;
    if (comm_ptr->errhandler) {
	MPIU_Object_add_ref( comm_ptr->errhandler );
    }
    /* We could also inherit the communicator function pointer */
    newcomm_ptr->coll_fns = 0;

    /* Similarly, we could also inherit the topology function pointer */
    newcomm_ptr->topo_fns = 0;

    /* We do *not* inherit any name */
    newcomm_ptr->name[0] = 0;

    /* Start with no attributes on this communicator */
    newcomm_ptr->attributes = 0;
    *outcomm_ptr = newcomm_ptr;
    return MPI_SUCCESS;
}


int MPIR_Comm_release(MPID_Comm * comm_ptr)
{
    static const char FCNAME[] = "MPIR_Comm_release";
    int mpi_errno = MPI_SUCCESS;
    int inuse;
    
    MPIU_Object_release_ref( comm_ptr, &inuse);
    if (!inuse) {

        if (MPIR_Process.comm_parent == comm_ptr)
            MPIR_Process.comm_parent = NULL;

	/* Remove the attributes, executing the attribute delete routine.  
           Do this only if the attribute functions are defined. */
	if (MPIR_Process.attr_free && comm_ptr->attributes) {
	    mpi_errno = MPIR_Process.attr_free( comm_ptr->handle, 
						comm_ptr->attributes );
	}

	if (mpi_errno == MPI_SUCCESS) {
	    /* Notify the device that the communicator is about to be 
	       destroyed */
	    MPID_Dev_comm_destroy_hook(comm_ptr);
	    
	    /* Free the VCRT */
	    mpi_errno = MPID_VCRT_Release(comm_ptr->vcrt);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
            if (comm_ptr->comm_kind == MPID_INTERCOMM) {
                mpi_errno = MPID_VCRT_Release(comm_ptr->local_vcrt);
		if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_POP(mpi_errno);
		}
                if (comm_ptr->local_comm) 
                    MPIR_Comm_release(comm_ptr->local_comm);
            }

	    /* Free the context value */
	    MPIR_Free_contextid( comm_ptr->context_id );

	    /* Free the local and remote groups, if they exist */
            if (comm_ptr->local_group)
                MPIR_Group_release(comm_ptr->local_group);
            if (comm_ptr->remote_group)
                MPIR_Group_release(comm_ptr->remote_group);

  	    MPIU_Handle_obj_free( &MPID_Comm_mem, comm_ptr );  

            MPIR_COMML_FORGET( comm_ptr );
	}
	else {
	    /* If the user attribute free function returns an error,
	       then do not free the communicator */
	    MPIU_Object_add_ref( comm_ptr );
	}
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
