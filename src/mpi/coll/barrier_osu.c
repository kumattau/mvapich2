/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2011, The Ohio State University. All rights
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
#if defined(_OSU_MVAPICH_)
#include "coll_shmem.h"
#endif /* defined(_OSU_MVAPICH_) */

/* This is the default implementation of the barrier operation.  The
   algorithm is:
   
   Algorithm: MPI_Barrier

   We use the dissemination algorithm described in:
   Debra Hensgen, Raphael Finkel, and Udi Manbet, "Two Algorithms for
   Barrier Synchronization," International Journal of Parallel
   Programming, 17(1):1-17, 1988.  

   It uses ceiling(lgp) steps. In step k, 0 <= k <= (ceiling(lgp)-1),
   process i sends to process (i + 2^k) % p and receives from process 
   (i - 2^k + p) % p.

   Possible improvements: 

   End Algorithm: MPI_Barrier

   This is an intracommunicator barrier only!
*/

/* not declared static because it is called in ch3_comm_connect/accept */
int MPIR_Barrier_OSU( MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Barrier_OSU";
    int size, rank, src, dst, mask, mpi_errno=MPI_SUCCESS;
    MPI_Comm comm;

	/* begin shmem_comm delaration */
	MPI_Comm shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr = NULL, *leader_commptr = NULL;
    int local_rank = -1, local_size=0, my_rank;
    int total_size, shmem_comm_rank;
    /* end shmem_comm declaration */

	size = comm_ptr->local_size;
    /* Trivial barriers return immediately */
    if (size == 1) return MPI_SUCCESS;

    rank = comm_ptr->rank;
    comm = comm_ptr->handle;

    /* Only one collective operation per communicator can be active at any
       time */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

#if defined(_OSU_MVAPICH_)
    if (enable_shmem_collectives)
	{
        shmem_comm = comm_ptr->shmem_comm;                                              
        leader_comm = comm_ptr->leader_comm;
	    
		if ((disable_shmem_barrier == 0) && (shmem_comm != 0)&&(leader_comm !=0) &&(comm_ptr->shmem_coll_ok == 1 ))
	    {

#if defined(CKPT)
            MPIDI_CH3I_CR_lock();
#endif
            my_rank = comm_ptr->rank;
	        /* MPI_Comm_size(comm, &total_size); */
		    total_size = comm_ptr->local_size;
		    shmem_comm = comm_ptr->shmem_comm;
        
		    /*  MPI_Comm_rank(shmem_comm, &local_rank);
		    MPI_Comm_size(shmem_comm, &local_size); */

            MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
            local_rank = shmem_commptr->rank;
            local_size = shmem_commptr->local_size;
            shmem_comm_rank = shmem_commptr->shmem_comm_rank;
            leader_comm = comm_ptr->leader_comm;
            MPID_Comm_get_ptr(leader_comm, leader_commptr);
		 
            if (local_size > 1)
		    {
                MPIDI_CH3I_SHMEM_COLL_Barrier_gather(local_size, local_rank, shmem_comm_rank);
            }
		
	        if ((local_rank == 0) && (local_size != total_size))
		    {
                mpi_errno = MPIR_Barrier( leader_commptr );
		    }

            if (local_size > 1)
		    {
                MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(local_size, local_rank, shmem_comm_rank);
            }
#if defined(CKPT)
            MPIDI_CH3I_CR_unlock();
#endif
        }
		else
		{
            mpi_errno = MPIR_Barrier( comm_ptr );
        }
    }   
	else
	{
#endif /*#if defined(_OSU_MVAPICH_)*/
        mask = 0x1;
        while (mask < size) 
		{
            dst = (rank + mask) % size;
            src = (rank - mask + size) % size;
            mpi_errno = MPIC_Sendrecv(NULL, 0, MPI_BYTE, dst,
                                  MPIR_BARRIER_TAG, NULL, 0, MPI_BYTE,
                                  src, MPIR_BARRIER_TAG, comm,                          
                                  MPI_STATUS_IGNORE);
        
	        /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno)
	        {
	            mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	            return mpi_errno;
	        }
	        /* --END ERROR HANDLING-- */
            mask <<= 1;
        }

#if defined(_OSU_MVAPICH_)
    }
#endif /* #if defined(_OSU_MVAPICH_) */
    
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return mpi_errno;
}


#if 0

/* This is the default implementation of the barrier operation.  The
   algorithm is:
   
   Algorithm: MPI_Barrier

   Find the largest power of two that is less than or equal to the size of 
   the communicator.  Call tbis twon_within.

   Divide the communicator by rank into two groups: those with 
   rank < twon_within and those with greater rank.  The barrier
   executes in three steps.  First, the group with rank >= twon_within
   sends to the first (size-twon_within) ranks of the first group.
   That group then executes a recursive doubling algorithm for the barrier.
   For the third step, the first (size-twon_within) ranks send to the top
   group.  This is the same algorithm used in MPICH-1.

   Possible improvements: 
   The upper group could apply recursively this approach to reduce the 
   total number of messages sent (in the case of of a size of 2^n-1, there 
   are 2^(n-1) messages sent in the first and third steps).

   End Algorithm: MPI_Barrier

   This is an intracommunicator barrier only!
*/
int MPIR_Barrier( MPID_Comm *comm_ptr )
{
    int size, rank;
    int twon_within, n2, remaining, gap, partner;
    MPID_Request *request_ptr;
    int mpi_errno = MPI_SUCCESS;
    
    size = comm_ptr->remote_size;
    rank = comm_ptr->rank;

    /* Trivial barriers return immediately */
    if (size == 1) return MPI_SUCCESS;

    /* Only one collective operation per communicator can be active at any
       time */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
    /* Find the twon_within (this could be cached if more routines
     need it) */
    twon_within = 1;
    n2          = 2;
    while (n2 <= size) { twon_within = n2; n2 <<= 1; }
    remaining = size - twon_within;

    if (rank < twon_within) {
	/* First step: receive from the upper group */
	if (rank < remaining) {
	    MPID_Recv( 0, 0, MPI_BYTE, twon_within + rank, MPIR_BARRIER_TAG, 
		       comm_ptr, MPID_CONTEXT_INTRA_COLL, MPI_STATUS_IGNORE,
		       &request_ptr );
	    if (request_ptr) {
		mpi_errno = MPIC_Wait(request_ptr);
		MPID_Request_release(request_ptr);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    goto fn_exit;
		}
		/* --END ERROR HANDLING-- */
	    }
	}
	/* Second step: recursive doubling exchange */
	for (gap=1; gap<twon_within; gap <<= 1) {
	    partner = (rank ^ gap);
	    MPIC_Sendrecv( 0, 0, MPI_BYTE, partner, MPIR_BARRIER_TAG,
			   0, 0, MPI_BYTE, partner, MPIR_BARRIER_TAG,
			   comm_ptr->handle, MPI_STATUS_IGNORE );
	}

	/* Third step: send to the upper group */
	if (rank < remaining) {
	    MPID_Send( 0, 0, MPI_BYTE, rank + twon_within, MPIR_BARRIER_TAG,
		       comm_ptr, MPID_CONTEXT_INTRA_COLL, &request_ptr );
	    if (request_ptr) {
		mpi_errno = MPIC_Wait(request_ptr);
		MPID_Request_release(request_ptr);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    goto fn_exit;
		}
		/* --END ERROR HANDLING-- */
	    }
	}
    }
    else {
	/* For the upper group, step one is a send */
	MPID_Send( 0, 0, MPI_BYTE, rank - twon_within, MPIR_BARRIER_TAG,
		   comm_ptr, MPID_CONTEXT_INTRA_COLL, &request_ptr );
	if (request_ptr) {
	    mpi_errno = MPIC_Wait(request_ptr);
	    MPID_Request_release(request_ptr);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	}
	/* There is no second step; for the third step, recv */
	MPID_Recv( 0, 0, MPI_BYTE, rank - twon_within, MPIR_BARRIER_TAG, 
		   comm_ptr, MPID_CONTEXT_INTRA_COLL, MPI_STATUS_IGNORE,
		   &request_ptr );
	if (request_ptr) {
	    mpi_errno = MPIC_Wait(request_ptr);
	    MPID_Request_release(request_ptr);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	}
    }

  fn_exit:
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

    return mpi_errno;
}
#endif


/* not declared static because a machine-specific function may call this one 
   in some cases */
int MPIR_Barrier_inter_OSU( MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Barrier_inter";
    int rank, mpi_errno, i, root;
    MPID_Comm *newcomm_ptr = NULL;

    rank = comm_ptr->rank;

    /* Get the local intracommunicator */
    if (!comm_ptr->local_comm)
	MPIR_Setup_intercomm_localcomm( comm_ptr );

    newcomm_ptr = comm_ptr->local_comm;

    /* do a barrier on the local intracommunicator */
    mpi_errno = MPIR_Barrier(newcomm_ptr);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    /* rank 0 on each group does an intercommunicator broadcast to the
       remote group to indicate that all processes in the local group
       have reached the barrier. We do a 1-byte bcast because a 0-byte
       bcast will just return without doing anything. */
    
    /* first broadcast from left to right group, then from right to
       left group */
    if (comm_ptr->is_low_group) {
        /* bcast to right*/
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        /* receive bcast from right */
        root = 0;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }
    else {
        /* receive bcast from left */
        root = 0;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        /* bcast to left */
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }

    return mpi_errno;
}
