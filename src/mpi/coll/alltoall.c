/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Alltoall */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Alltoall = PMPI_Alltoall
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Alltoall  MPI_Alltoall
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Alltoall as PMPI_Alltoall
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Alltoall
#define MPI_Alltoall PMPI_Alltoall

/* This is the default implementation of alltoall. The algorithm is:
   
   Algorithm: MPI_Alltoall

   We use four algorithms for alltoall. For short messages and
   (comm_size >= 8), we use the algorithm by Jehoshua Bruck et al,
   IEEE TPDS, Nov. 1997. It is a store-and-forward algorithm that
   takes lgp steps. Because of the extra communication, the bandwidth
   requirement is (n/2).lgp.beta.

   Cost = lgp.alpha + (n/2).lgp.beta

   where n is the total amount of data a process needs to send to all
   other processes.

   For medium size messages and (short messages for comm_size < 8), we
   use an algorithm that posts all irecvs and isends and then does a
   waitall. We scatter the order of sources and destinations among the
   processes, so that all processes don't try to send/recv to/from the
   same process at the same time.

   *** Modification: We post only a small number of isends and irecvs 
   at a time and wait on them as suggested by Tony Ladd. ***

   For long messages and power-of-two number of processes, we use a
   pairwise exchange algorithm, which takes p-1 steps. We
   calculate the pairs by using an exclusive-or algorithm:
           for (i=1; i<comm_size; i++)
               dest = rank ^ i;
   This algorithm doesn't work if the number of processes is not a power of
   two. For a non-power-of-two number of processes, we use an
   algorithm in which, in step i, each process  receives from (rank-i)
   and sends to (rank+i). 

   Cost = (p-1).alpha + n.beta

   where n is the total amount of data a process needs to send to all
   other processes.

   Possible improvements: 

   End Algorithm: MPI_Alltoall
*/

/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoall( 
    void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype, 
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Alltoall";
    int          comm_size, i, j, pof2;
    MPI_Aint     sendtype_extent, recvtype_extent;
    MPI_Aint recvtype_true_extent, recvbuf_extent, recvtype_true_lb;
    int mpi_errno=MPI_SUCCESS, src, dst, rank, nbytes;
    MPI_Status status;
    int sendtype_size, pack_size, block, position, *displs, count;
    MPI_Datatype newtype;
    void *tmp_buf;
    MPI_Comm comm;
    MPI_Request *reqarray;
    MPI_Status *starray;
#ifdef MPIR_OLD_SHORT_ALLTOALL_ALG
    MPI_Aint sendtype_true_extent, sendbuf_extent, sendtype_true_lb;
    int k, p, curr_cnt, dst_tree_root, my_tree_root;
    int last_recv_cnt, mask, tmp_mask, tree_root, nprocs_completed;
#endif

    if (recvcount == 0) return MPI_SUCCESS;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);

    MPID_Datatype_get_size_macro(sendtype, sendtype_size);
    nbytes = sendtype_size * sendcount;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    if (sendbuf == MPI_IN_PLACE) {
        /* We use pair-wise sendrecv_replace in order to conserve memory usage,
         * which is keeping with the spirit of the MPI-2.2 Standard.  But
         * because of this approach all processes must agree on the global
         * schedule of sendrecv_replace operations to avoid deadlock.
         *
         * Note that this is not an especially efficient algorithm in terms of
         * time and there will be multiple repeated malloc/free's rather than
         * maintaining a single buffer across the whole loop.  Something like
         * MADRE is probably the best solution for the MPI_IN_PLACE scenario. */
        for (i = 0; i < comm_size; ++i) {
            /* start inner loop at i to avoid re-exchanging data */
            for (j = i; j < comm_size; ++j) {
                if (rank == i) {
                    /* also covers the (rank == i && rank == j) case */
                    mpi_errno = MPIC_Sendrecv_replace(((char *)recvbuf + j*recvcount*recvtype_extent),
                                                      recvcount, recvtype,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      comm, &status);
                    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                }
                else if (rank == j) {
                    /* same as above with i/j args reversed */
                    mpi_errno = MPIC_Sendrecv_replace(((char *)recvbuf + i*recvcount*recvtype_extent),
                                                      recvcount, recvtype,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      comm, &status);
                    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                }
            }
        }
    }
    else if ((nbytes <= MPIR_ALLTOALL_SHORT_MSG) && (comm_size >= 8)) {


        /* use the indexing algorithm by Jehoshua Bruck et al,
         * IEEE TPDS, Nov. 97 */ 

        /* allocate temporary buffer */
        NMPI_Pack_size(recvcount*comm_size, recvtype, comm, &pack_size);
        tmp_buf = MPIU_Malloc(pack_size);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */

        /* Do Phase 1 of the algorithim. Shift the data blocks on process i
         * upwards by a distance of i blocks. Store the result in recvbuf. */
        mpi_errno = MPIR_Localcopy((char *) sendbuf + 
			   rank*sendcount*sendtype_extent, 
                           (comm_size - rank)*sendcount, sendtype, recvbuf, 
                           (comm_size - rank)*recvcount, recvtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        mpi_errno = MPIR_Localcopy(sendbuf, rank*sendcount, sendtype, 
                        (char *) recvbuf + 
				   (comm_size-rank)*recvcount*recvtype_extent, 
                                   rank*recvcount, recvtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        /* Input data is now stored in recvbuf with datatype recvtype */

        /* Now do Phase 2, the communication phase. It takes
           ceiling(lg p) steps. In each step i, each process sends to rank+2^i
           and receives from rank-2^i, and exchanges all data blocks
           whose ith bit is 1. */

        /* allocate displacements array for indexed datatype used in
           communication */

        displs = MPIU_Malloc(comm_size * sizeof(int));
	/* --BEGIN ERROR HANDLING-- */
        if (!displs) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */

        pof2 = 1;
        while (pof2 < comm_size) {
            dst = (rank + pof2) % comm_size;
            src = (rank - pof2 + comm_size) % comm_size;

            /* Exchange all data blocks whose ith bit is 1 */
            /* Create an indexed datatype for the purpose */

            count = 0;
            for (block=1; block<comm_size; block++) {
                if (block & pof2) {
                    displs[count] = block * recvcount;
                    count++;
                }
            }

            mpi_errno = NMPI_Type_create_indexed_block(count, recvcount, 
                                               displs, recvtype, &newtype);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

            mpi_errno = NMPI_Type_commit(&newtype);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

            position = 0;
            mpi_errno = NMPI_Pack(recvbuf, 1, newtype, tmp_buf, pack_size, 
                                  &position, comm);

            mpi_errno = MPIC_Sendrecv(tmp_buf, position, MPI_PACKED, dst,
                                      MPIR_ALLTOALL_TAG, recvbuf, 1, newtype,
                                      src, MPIR_ALLTOALL_TAG, comm,
                                      MPI_STATUS_IGNORE);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

            mpi_errno = NMPI_Type_free(&newtype);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

            pof2 *= 2;
        }

        MPIU_Free(displs);
        MPIU_Free(tmp_buf);

        /* Rotate blocks in recvbuf upwards by (rank + 1) blocks. Need
         * a temporary buffer of the same size as recvbuf. */
        
        /* get true extent of recvtype */
        mpi_errno = NMPI_Type_get_true_extent(recvtype, &recvtype_true_lb,
                                              &recvtype_true_extent);  
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

        recvbuf_extent = recvcount * comm_size *
            (MPIR_MAX(recvtype_true_extent, recvtype_extent));
        tmp_buf = MPIU_Malloc(recvbuf_extent);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - recvtype_true_lb);

        mpi_errno = MPIR_Localcopy((char *) recvbuf + (rank+1)*recvcount*recvtype_extent, 
                       (comm_size - rank - 1)*recvcount, recvtype, tmp_buf, 
                       (comm_size - rank - 1)*recvcount, recvtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        mpi_errno = MPIR_Localcopy(recvbuf, (rank+1)*recvcount, recvtype, 
                       (char *) tmp_buf + (comm_size-rank-1)*recvcount*recvtype_extent, 
                       (rank+1)*recvcount, recvtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

        /* Blocks are in the reverse order now (comm_size-1 to 0). 
         * Reorder them to (0 to comm_size-1) and store them in recvbuf. */

        for (i=0; i<comm_size; i++) 
            MPIR_Localcopy((char *) tmp_buf + i*recvcount*recvtype_extent,
                           recvcount, recvtype, 
                           (char *) recvbuf + (comm_size-i-1)*recvcount*recvtype_extent, 
                           recvcount, recvtype); 

        MPIU_Free((char*)tmp_buf + recvtype_true_lb);



#ifdef MPIR_OLD_SHORT_ALLTOALL_ALG
        /* Short message. Use recursive doubling. Each process sends all
           its data at each step along with all data it received in
           previous steps. */
        
        /* need to allocate temporary buffer of size
           sendbuf_extent*comm_size */
        
        /* get true extent of sendtype */
        mpi_errno = NMPI_Type_get_true_extent(sendtype, &sendtype_true_lb,
                                              &sendtype_true_extent);  
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

        sendbuf_extent = sendcount * comm_size *
            (MPIR_MAX(sendtype_true_extent, sendtype_extent));
        tmp_buf = MPIU_Malloc(sendbuf_extent*comm_size);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - sendtype_true_lb);
        
        /* copy local sendbuf into tmp_buf at location indexed by rank */
        curr_cnt = sendcount*comm_size;
        mpi_errno = MPIR_Localcopy(sendbuf, curr_cnt, sendtype,
                                   ((char *)tmp_buf + rank*sendbuf_extent),
                                   curr_cnt, sendtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno);}
        
        mask = 0x1;
        i = 0;
        while (mask < comm_size) {
            dst = rank ^ mask;
            
            dst_tree_root = dst >> i;
            dst_tree_root <<= i;
            
            my_tree_root = rank >> i;
            my_tree_root <<= i;
            
            if (dst < comm_size) {
                mpi_errno = MPIC_Sendrecv(((char *)tmp_buf +
                                           my_tree_root*sendbuf_extent),
                                          curr_cnt, sendtype,
                                          dst, MPIR_ALLTOALL_TAG, 
                                          ((char *)tmp_buf +
                                           dst_tree_root*sendbuf_extent),
					  sendbuf_extent*(comm_size-dst_tree_root),
                                          sendtype, dst, MPIR_ALLTOALL_TAG, 
                                          comm, &status);
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                
                /* in case of non-power-of-two nodes, less data may be
                   received than specified */
                NMPI_Get_count(&status, sendtype, &last_recv_cnt);
                curr_cnt += last_recv_cnt;
            }
            
            /* if some processes in this process's subtree in this step
               did not have any destination process to communicate with
               because of non-power-of-two, we need to send them the
               result. We use a logarithmic recursive-halfing algorithm
               for this. */
            
            if (dst_tree_root + mask > comm_size) {
                nprocs_completed = comm_size - my_tree_root - mask;
                /* nprocs_completed is the number of processes in this
                   subtree that have all the data. Send data to others
                   in a tree fashion. First find root of current tree
                   that is being divided into two. k is the number of
                   least-significant bits in this process's rank that
                   must be zeroed out to find the rank of the root */ 
                j = mask;
                k = 0;
                while (j) {
                    j >>= 1;
                    k++;
                }
                k--;
                
                tmp_mask = mask >> 1;
                while (tmp_mask) {
                    dst = rank ^ tmp_mask;
                    
                    tree_root = rank >> k;
                    tree_root <<= k;
                    
                    /* send only if this proc has data and destination
                       doesn't have data. at any step, multiple processes
                       can send if they have the data */
                    if ((dst > rank) && 
                        (rank < tree_root + nprocs_completed)
                        && (dst >= tree_root + nprocs_completed)) {
                        /* send the data received in this step above */
                        mpi_errno = MPIC_Send(((char *)tmp_buf +
                                               dst_tree_root*sendbuf_extent),
                                              last_recv_cnt, sendtype,
                                              dst, MPIR_ALLTOALL_TAG,
                                              comm);  
			if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                    }
                    /* recv only if this proc. doesn't have data and sender
                       has data */
                    else if ((dst < rank) && 
                             (dst < tree_root + nprocs_completed) &&
                             (rank >= tree_root + nprocs_completed)) {
                        mpi_errno = MPIC_Recv(((char *)tmp_buf +
                                               dst_tree_root*sendbuf_extent),
					      sendbuf_extent*(comm_size-dst_tree_root),
                                              sendtype,   
                                              dst, MPIR_ALLTOALL_TAG,
                                              comm, &status); 
			if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                        NMPI_Get_count(&status, sendtype, &last_recv_cnt);
                        curr_cnt += last_recv_cnt;
                    }
                    tmp_mask >>= 1;
                    k--;
                }
            }
            
            mask <<= 1;
            i++;
        }
        
        /* now copy everyone's contribution from tmp_buf to recvbuf */
        for (p=0; p<comm_size; p++) {
            mpi_errno = MPIR_Localcopy(((char *)tmp_buf +
                                        p*sendbuf_extent +
                                        rank*sendcount*sendtype_extent),
                                        sendcount, sendtype, 
                                        ((char*)recvbuf +
                                         p*recvcount*recvtype_extent), 
                                        recvcount, recvtype);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        }
        
        MPIU_Free((char *)tmp_buf+sendtype_true_lb); 
#endif

    }

    else if (nbytes <= MPIR_ALLTOALL_MEDIUM_MSG) {  
        /* Medium-size message. Use isend/irecv with scattered
           destinations. Use Tony Ladd's modification to post only
           a small number of isends/irecvs at a time. */
        int ii, ss, bblock;

        bblock = MPIR_ALLTOALL_THROTTLE;
        if (bblock == 0) bblock = comm_size;

        reqarray = (MPI_Request *) MPIU_Malloc(2*bblock*sizeof(MPI_Request));
        /* --BEGIN ERROR HANDLING-- */
        if (!reqarray) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
        /* --END ERROR HANDLING-- */

        starray = (MPI_Status *) MPIU_Malloc(2*bblock*sizeof(MPI_Status));
        /* --BEGIN ERROR HANDLING-- */
        if (!starray) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
        /* --END ERROR HANDLING-- */

        for (ii=0; ii<comm_size; ii+=bblock) {
            ss = comm_size-ii < bblock ? comm_size-ii : bblock;
            /* do the communication -- post ss sends and receives: */
            for ( i=0; i<ss; i++ ) { 
                dst = (rank+i+ii) % comm_size;
                mpi_errno = MPIC_Irecv((char *)recvbuf +
                                       dst*recvcount*recvtype_extent, 
                                       recvcount, recvtype, dst,
                                       MPIR_ALLTOALL_TAG, comm,
                                       &reqarray[i]);
                if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
            }

            for ( i=0; i<ss; i++ ) { 
                dst = (rank-i-ii+comm_size) % comm_size;
                mpi_errno = MPIC_Isend((char *)sendbuf +
                                       dst*sendcount*sendtype_extent, 
                                       sendcount, sendtype, dst,
                                       MPIR_ALLTOALL_TAG, comm,
                                       &reqarray[i+ss]);
                if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
            }
  
            /* ... then wait for them to finish: */
            mpi_errno = NMPI_Waitall(2*ss,reqarray,starray);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno == MPI_ERR_IN_STATUS) {
                for (j=0; j<2*ss; j++) {
                    if (starray[j].MPI_ERROR != MPI_SUCCESS) 
                        mpi_errno = starray[j].MPI_ERROR;
                }
            }
            /* --END ERROR HANDLING-- */
        }
        MPIU_Free(starray);
        MPIU_Free(reqarray);
    }

    else {
        /* Long message. If comm_size is a power-of-two, do a pairwise
           exchange using exclusive-or to create pairs. Else send to
           rank+i, receive from rank-i. */
        
        /* Make local copy first */
        mpi_errno = MPIR_Localcopy(((char *)sendbuf + 
                                    rank*sendcount*sendtype_extent), 
                                   sendcount, sendtype, 
                                   ((char *)recvbuf +
                                    rank*recvcount*recvtype_extent),
                                   recvcount, recvtype);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno ); }

        /* Is comm_size a power-of-two? */
        i = 1;
        while (i < comm_size)
            i *= 2;
        if (i == comm_size)
            pof2 = 1;
        else 
            pof2 = 0;

        /* Do the pairwise exchanges */
        for (i=1; i<comm_size; i++) {
            if (pof2 == 1) {
                /* use exclusive-or algorithm */
                src = dst = rank ^ i;
            }
            else {
                src = (rank - i + comm_size) % comm_size;
                dst = (rank + i) % comm_size;
            }

            mpi_errno = MPIC_Sendrecv(((char *)sendbuf +
                                       dst*sendcount*sendtype_extent), 
                                      sendcount, sendtype, dst,
                                      MPIR_ALLTOALL_TAG, 
                                      ((char *)recvbuf +
                                       src*recvcount*recvtype_extent),
                                      recvcount, recvtype, src,
                                      MPIR_ALLTOALL_TAG, comm, &status);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        }
    }

 fn_fail:    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}
/* end:nested */

/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoall_inter( 
    void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype, 
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr )
{
/* Intercommunicator alltoall. We use a pairwise exchange algorithm
   similar to the one used in intracommunicator alltoall for long
   messages. Since the local and remote groups can be of different
   sizes, we first compute the max of local_group_size,
   remote_group_size. At step i, 0 <= i < max_size, each process
   receives from src = (rank - i + max_size) % max_size if src <
   remote_size, and sends to dst = (rank + i) % max_size if dst <
   remote_size. 
*/
    static const char FCNAME[] = "MPIR_Alltoall_inter";
    int          local_size, remote_size, max_size, i;
    MPI_Aint     sendtype_extent, recvtype_extent;
    int          mpi_errno = MPI_SUCCESS;
    MPI_Status status;
    int src, dst, rank;
    char *sendaddr, *recvaddr;
    MPI_Comm comm;
    
    local_size = comm_ptr->local_size; 
    remote_size = comm_ptr->remote_size;
    rank = comm_ptr->rank;
    comm = comm_ptr->handle;

    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    /* Do the pairwise exchanges */
    max_size = MPIR_MAX(local_size, remote_size);
    MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT recvbuf +
				     max_size*recvcount*recvtype_extent);
    MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT sendbuf +
				     max_size*sendcount*sendtype_extent);
    for (i=0; i<max_size; i++) {
        src = (rank - i + max_size) % max_size;
        dst = (rank + i) % max_size;
        if (src >= remote_size) {
            src = MPI_PROC_NULL;
            recvaddr = NULL;
        }
        else {
            recvaddr = (char *)recvbuf + src*recvcount*recvtype_extent;
        }
        if (dst >= remote_size) {
            dst = MPI_PROC_NULL;
            sendaddr = NULL;
        }
        else {
            sendaddr = (char *)sendbuf + dst*sendcount*sendtype_extent;
        }

        mpi_errno = MPIC_Sendrecv(sendaddr, sendcount, sendtype, dst, 
                                  MPIR_ALLTOALL_TAG, recvaddr,
                                  recvcount, recvtype, src,
                                  MPIR_ALLTOALL_TAG, comm, &status);
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}
/* end:nested */
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Alltoall

/*@
MPI_Alltoall - Sends data from all to all processes

Input Parameters:
+ sendbuf - starting address of send buffer (choice) 
. sendcount - number of elements to send to each process (integer) 
. sendtype - data type of send buffer elements (handle) 
. recvcount - number of elements received from any process (integer) 
. recvtype - data type of receive buffer elements (handle) 
- comm - communicator (handle) 

Output Parameter:
. recvbuf - address of receive buffer (choice) 

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_ERR_COMM
.N MPI_ERR_COUNT
.N MPI_ERR_TYPE
.N MPI_ERR_BUFFER
@*/
int MPI_Alltoall(void *sendbuf, int sendcount, MPI_Datatype sendtype, 
                 void *recvbuf, int recvcount, MPI_Datatype recvtype, 
                 MPI_Comm comm)
{
    static const char FCNAME[] = "MPI_Alltoall";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPIU_THREADPRIV_DECL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_ALLTOALL);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_CS_ENTER(ALLFUNC,);
    MPID_MPI_COLL_FUNC_ENTER(MPID_STATE_MPI_ALLTOALL);

    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm, mpi_errno);
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr( comm, comm_ptr );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPID_Datatype *sendtype_ptr=NULL, *recvtype_ptr=NULL;
	    
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
	    MPIR_ERRTEST_COUNT(sendcount, mpi_errno);
	    MPIR_ERRTEST_COUNT(recvcount, mpi_errno);
	    MPIR_ERRTEST_DATATYPE(sendtype, "sendtype", mpi_errno);
	    MPIR_ERRTEST_DATATYPE(recvtype, "recvtype", mpi_errno);
            if (HANDLE_GET_KIND(sendtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype_get_ptr(sendtype, sendtype_ptr);
                MPID_Datatype_valid_ptr( sendtype_ptr, mpi_errno );
                MPID_Datatype_committed_ptr( sendtype_ptr, mpi_errno );
            }
            if (HANDLE_GET_KIND(recvtype) != HANDLE_KIND_BUILTIN) {
                MPID_Datatype_get_ptr(recvtype, recvtype_ptr);
                MPID_Datatype_valid_ptr( recvtype_ptr, mpi_errno );
                MPID_Datatype_committed_ptr( recvtype_ptr, mpi_errno );
            }

            if (comm_ptr->comm_kind == MPID_INTERCOMM) {
                MPIR_ERRTEST_SENDBUF_INPLACE(sendbuf, sendcount, mpi_errno);
            }
            MPIR_ERRTEST_RECVBUF_INPLACE(recvbuf, recvcount, mpi_errno);
            MPIR_ERRTEST_USERBUFFER(sendbuf,sendcount,sendtype,mpi_errno);
	    MPIR_ERRTEST_USERBUFFER(recvbuf,recvcount,recvtype,mpi_errno);

            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */

    if (comm_ptr->coll_fns != NULL && comm_ptr->coll_fns->Alltoall != NULL)
	{
#if defined(_OSU_MVAPICH_)
        MPIU_THREADPRIV_GET;
	    MPIR_Nest_incr();
#endif
        if (comm_ptr->comm_kind == MPID_INTRACOMM)
		{
	        /* intracommunicator */
            mpi_errno = comm_ptr->coll_fns->Alltoall(sendbuf, sendcount,
                                                 sendtype, recvbuf, recvcount,
                                                 recvtype, comm_ptr);
        } 
		else 
		{
		    /* intercommunicator */
			mpi_errno = comm_ptr->coll_fns->Alltoall_inter(sendbuf, sendcount,
		                                             sendtype, recvbuf,
		                                             recvcount, recvtype,
		                                             comm_ptr);
        }

#if defined(_OSU_MVAPICH_)
        MPIR_Nest_decr();
 #endif
	} 
	else 
	{
	MPIU_THREADPRIV_GET;

	MPIR_Nest_incr();
        if (comm_ptr->comm_kind == MPID_INTRACOMM) 
            /* intracommunicator */
            mpi_errno = MPIR_Alltoall(sendbuf, sendcount, sendtype,
                                      recvbuf, recvcount, recvtype, comm_ptr); 
        else {
            /* intercommunicator */
            mpi_errno = MPIR_Alltoall_inter(sendbuf, sendcount,
                                            sendtype, recvbuf,
                                            recvcount, recvtype,
                                            comm_ptr); 
        }
	MPIR_Nest_decr();
    }

    if (mpi_errno != MPI_SUCCESS) goto fn_fail;

    /* ... end of body of routine ... */
    
  fn_exit:
    MPID_MPI_COLL_FUNC_EXIT(MPID_STATE_MPI_ALLTOALL);
    MPIU_THREAD_CS_EXIT(ALLFUNC,);
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_alltoall",
	    "**mpi_alltoall %p %d %D %p %d %D %C", sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, comm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

