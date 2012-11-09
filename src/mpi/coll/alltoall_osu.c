/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2001-2012, The Ohio State University. All rights
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
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include <math.h>
#include <unistd.h>
#include "coll_shmem.h"
#include "alltoall_tuning.h"

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


int (*MV2_Alltoall_function) (const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                              void *recvbuf, int recvcount, MPI_Datatype recvtype,
                              MPID_Comm *comm_ptr, int *errflag)=NULL;

#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_inplace_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_inplace_MV2(
    const void *sendbuf,
    int sendcount,
    MPI_Datatype sendtype,
    void *recvbuf,
    int recvcount,
    MPI_Datatype recvtype,
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int          comm_size, i, j;
    MPI_Aint     recvtype_extent;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int rank;
    MPI_Status status;
    MPI_Comm comm;
 
    if (recvcount == 0) return MPI_SUCCESS;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* Get extent of recv type */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
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
                mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + 
                                                      j*recvcount*recvtype_extent),
                                                      recvcount, recvtype,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      comm, &status, errflag);

                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
            else if (rank == j) {
                /* same as above with i/j args reversed */
                mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + 
                                                     i*recvcount*recvtype_extent),
                                                     recvcount, recvtype,
                                                     i, MPIR_ALLTOALL_TAG,
                                                     i, MPIR_ALLTOALL_TAG,
                                                     comm, &status, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
        }
    }
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    return (mpi_errno);
}




#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_bruck_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_bruck_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag )
{

    int          comm_size, i, pof2;
    MPI_Aint     sendtype_extent, recvtype_extent;
    MPI_Aint recvtype_true_extent, recvbuf_extent, recvtype_true_lb;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int src, dst, rank;
    int pack_size, block, position, *displs, count;
    MPI_Datatype newtype;
    void *tmp_buf;
    MPI_Comm comm;
    
    if (recvcount == 0) return MPI_SUCCESS;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
    
    /* use the indexing algorithm by Jehoshua Bruck et al,
     * IEEE TPDS, Nov. 97 */
    
    /* allocate temporary buffer */
    MPIR_Pack_size_impl(recvcount*comm_size, recvtype, &pack_size);
    tmp_buf = MPIU_Malloc(pack_size);
	/* --BEGIN ERROR HANDLING-- */
    if (!tmp_buf) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                            FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
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
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                            FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
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
        
        mpi_errno = MPIR_Type_create_indexed_block_impl(count, recvcount,
                                                  displs, recvtype, &newtype);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        
        mpi_errno = MPIR_Type_commit_impl(&newtype);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        
        position = 0;
        mpi_errno = MPIR_Pack_impl(recvbuf, 1, newtype, tmp_buf, 
                                   pack_size, &position);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        
        mpi_errno = MPIC_Sendrecv_ft(tmp_buf, position, MPI_PACKED, dst,
                                     MPIR_ALLTOALL_TAG, recvbuf, 1, newtype,
                                     src, MPIR_ALLTOALL_TAG, comm,
                                     MPI_STATUS_IGNORE, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
        
        MPIR_Type_free_impl(&newtype);
        
        pof2 *= 2;
    }
    
    MPIU_Free(displs);
    MPIU_Free(tmp_buf);
    
    /* Rotate blocks in recvbuf upwards by (rank + 1) blocks. Need
     * a temporary buffer of the same size as recvbuf. */
    
    /* get true extent of recvtype */
    MPIR_Type_get_true_extent_impl(recvtype, &recvtype_true_lb,
                                   &recvtype_true_extent);
    recvbuf_extent = recvcount * comm_size *
    (MPIR_MAX(recvtype_true_extent, recvtype_extent));
    tmp_buf = MPIU_Malloc(recvbuf_extent);
	/* --BEGIN ERROR HANDLING-- */
    if (!tmp_buf) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                            FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
        return mpi_errno;
    }
	/* --END ERROR HANDLING-- */
    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *)((char*)tmp_buf - recvtype_true_lb);
    
    mpi_errno = MPIR_Localcopy((char *) recvbuf + 
                               (rank+1)*recvcount*recvtype_extent,
                               (comm_size - rank - 1)*recvcount, recvtype, tmp_buf,
                               (comm_size - rank - 1)*recvcount, recvtype);
	if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = MPIR_Localcopy(recvbuf, (rank+1)*recvcount, recvtype,
                              (char *) tmp_buf + 
                              (comm_size-rank-1)*recvcount*recvtype_extent,
                              (rank+1)*recvcount, recvtype);
	if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    
    /* Blocks are in the reverse order now (comm_size-1 to 0).
     * Reorder them to (0 to comm_size-1) and store them in recvbuf. */
    
    for (i=0; i<comm_size; i++)
        MPIR_Localcopy((char *) tmp_buf + i*recvcount*recvtype_extent,
                       recvcount, recvtype,
                       (char *) recvbuf + (comm_size-i-1)*recvcount*recvtype_extent,
                       recvcount, recvtype);
    
    MPIU_Free((char*)tmp_buf + recvtype_true_lb);
    
    
fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
    
}

#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_RD_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_RD_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag )
{
    int          comm_size, i, j;
    MPI_Aint     sendtype_extent, recvtype_extent;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int dst, rank;
    MPI_Status status;
    void *tmp_buf;
    MPI_Comm comm;
    MPIU_CHKLMEM_DECL(1);

    MPI_Aint sendtype_true_extent, sendbuf_extent, sendtype_true_lb;
    int k, p, curr_cnt, dst_tree_root, my_tree_root;
    int last_recv_cnt, mask, tmp_mask, tree_root, nprocs_completed;
    
    if (recvcount == 0) return MPI_SUCCESS;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );


    /* Short message. Use recursive doubling. Each process sends all
     its data at each step along with all data it received in
     previous steps. */
    
    /* need to allocate temporary buffer of size
     sendbuf_extent*comm_size */


    MPIR_Type_get_true_extent_impl(sendtype, &sendtype_true_lb, &sendtype_true_extent);

    sendbuf_extent = sendcount * comm_size *
        (MPIR_MAX(sendtype_true_extent, sendtype_extent));
    MPIU_CHKLMEM_MALLOC(tmp_buf, void *, sendbuf_extent*comm_size, mpi_errno, "tmp_buf");

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
                mpi_errno = MPIC_Sendrecv_ft(((char *)tmp_buf +
                                              my_tree_root*sendbuf_extent),
                                             curr_cnt, sendtype,
                                             dst, MPIR_ALLTOALL_TAG,
                                             ((char *)tmp_buf +
                                              dst_tree_root*sendbuf_extent),
                                             sendbuf_extent*(comm_size-dst_tree_root),
                                             sendtype, dst, MPIR_ALLTOALL_TAG,
                                             comm, &status, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but
                     * continue */
                    *errflag = TRUE;
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    last_recv_cnt = 0;
                } else
                    /* in case of non-power-of-two nodes, less data may be
                     *    received than specified */
                    MPIR_Get_count_impl(&status, sendtype, &last_recv_cnt);
                curr_cnt += last_recv_cnt;
            }

            /* if some processes in this process's subtree in this step
            did not have any destination process to communicate with
             because of non-power-of-two, we need to send
             them the  result. We use a logarithmic
             recursive-halfing algorithm
             for this. */

            if (dst_tree_root + mask > comm_size) {
                nprocs_completed = comm_size - my_tree_root - mask;
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
                   can send if they have the data
                 */
                if ((dst > rank) &&
                    (rank < tree_root + nprocs_completed)
                    && (dst >= tree_root + nprocs_completed)) {
                    /* send the data received in this step above */
                    mpi_errno = MPIC_Send_ft(((char *)tmp_buf +
                                             dst_tree_root*sendbuf_extent),
                                             last_recv_cnt, sendtype,
                                             dst, MPIR_ALLTOALL_TAG,
                                             comm, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error
                         * but continue */
                         *errflag = TRUE;
                         MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                         MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
                /* recv only if this proc. doesn't have data and sender
                 has data */
                else if ((dst < rank) &&
                (dst < tree_root + nprocs_completed) &&
                (rank >= tree_root + nprocs_completed)) {
                        mpi_errno = MPIC_Recv_ft(((char *)tmp_buf +
                        dst_tree_root*sendbuf_extent),
                        sendbuf_extent*(comm_size-dst_tree_root),
                        sendtype,
                        dst, MPIR_ALLTOALL_TAG,
                        comm, &status, errflag);
                        if (mpi_errno) {
                            /* for communication errors, just record the error
                             * but continue */
                            *errflag = TRUE;
                            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                            last_recv_cnt = 0;
                        } else
                            MPIR_Get_count_impl(&status, sendtype, &last_recv_cnt);
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

fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

}


#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_Scatter_dest_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_Scatter_dest_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag )
{
    
    int          comm_size, i, j;
    MPI_Aint     sendtype_extent = 0, recvtype_extent = 0;
    int mpi_errno=MPI_SUCCESS;
    int dst, rank;
    MPI_Comm comm;
    MPI_Request *reqarray;
    MPI_Status *starray;
    
    if (recvcount == 0) return MPI_SUCCESS;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    
    /* Medium-size message. Use isend/irecv with scattered
     destinations. Use Tony Ladd's modification to post only
     a small number of isends/irecvs at a time. */
    /* FIXME: This converts the Alltoall to a set of blocking phases.
     Two alternatives should be considered:
     1) the choice of communication pattern could try to avoid
     contending routes in each phase
     2) rather than wait for all communication to finish (waitall),
     we could maintain constant queue size by using waitsome
     and posting new isend/irecv as others complete.  This avoids
     synchronization delays at the end of each block (when
     there are only a few isend/irecvs left)
     */
    int ii, ss, bblock;
    
    MPIU_CHKLMEM_DECL(2);
	
    bblock = mv2_coll_param.alltoall_throttle_factor;
    
    if (bblock >= comm_size) bblock = comm_size;
    /* If throttle_factor is n, each process posts n pairs of isend/irecv
     in each iteration. */
    
    /* FIXME: This should use the memory macros (there are storage
     leaks here if there is an error, for example) */
    MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, 2*bblock*sizeof(MPI_Request),
                        mpi_errno, "reqarray");
    
    MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, 2*bblock*sizeof(MPI_Status),
                        mpi_errno, "starray");
 
    for (ii=0; ii<comm_size; ii+=bblock) {
        ss = comm_size-ii < bblock ? comm_size-ii : bblock;
        /* do the communication -- post ss sends and receives: */
        for ( i=0; i<ss; i++ ) {
            dst = (rank+i+ii) % comm_size;
            mpi_errno = MPIC_Irecv_ft((char *)recvbuf +
                                      dst*recvcount*recvtype_extent,
                                      recvcount, recvtype, dst,
                                      MPIR_ALLTOALL_TAG, comm,
                                      &reqarray[i]);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        for ( i=0; i<ss; i++ ) {
            dst = (rank-i-ii+comm_size) % comm_size;
            mpi_errno = MPIC_Isend_ft((char *)sendbuf +
                                          dst*sendcount*sendtype_extent,
                                          sendcount, sendtype, dst,
                                          MPIR_ALLTOALL_TAG, comm,
                                          &reqarray[i+ss], errflag);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        
        /* ... then wait for them to finish: */
        mpi_errno = MPIC_Waitall_ft(2*ss,reqarray,starray, errflag);
        if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) {
            MPIU_ERR_POP(mpi_errno);
        }
       
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
                for (j=0; j<2*ss; j++) {
                     if (starray[j].MPI_ERROR != MPI_SUCCESS) {
                         mpi_errno = starray[j].MPI_ERROR;
                     }
                }
        }
    }
    /* --END ERROR HANDLING-- */
    MPIU_CHKLMEM_FREEALL();
    
    
fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
    
}



#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_pairwise_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_pairwise_MV2(
                            const void *sendbuf,
                            int sendcount,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcount,
                            MPI_Datatype recvtype,
                            MPID_Comm *comm_ptr,
                            int *errflag )
{
    
    int          comm_size, i, pof2;
    MPI_Aint     sendtype_extent, recvtype_extent;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int src, dst, rank;
    MPI_Status status;
    MPI_Comm comm;
    
    if (recvcount == 0) return MPI_SUCCESS;
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    /* Get extent of send and recv types */
    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
    
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
    
	if (i == comm_size && mv2_use_xor_alltoall == 1) {
        pof2 = 1;
    } else  {
        pof2 = 0;
    }
    
    /* Do the pairwise exchanges */
    for (i=1; i<comm_size; i++) {
        if (pof2 == 1) {
            /* use exclusive-or algorithm */
            src = dst = rank ^ i;
        } else {
            src = (rank - i + comm_size) % comm_size;
            dst = (rank + i) % comm_size;
        }
        mpi_errno = MPIC_Sendrecv_ft(((char *)sendbuf +
                                     dst*sendcount*sendtype_extent),
                                     sendcount, sendtype, dst,
                                     MPIR_ALLTOALL_TAG,
                                     ((char *)recvbuf +
                                     src*recvcount*recvtype_extent),
                                     recvcount, recvtype, src,
                                     MPIR_ALLTOALL_TAG, comm, &status,
                                     errflag);

        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }
    
    
    
fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
    
}


/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Alltoall_tune_intra_MV2( 
    const void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype, 
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr, 
    int *errflag )
{
    int sendtype_size, recvtype_size, nbytes, comm_size;
    char * tmp_buf = NULL;
    int mpi_errno=MPI_SUCCESS;
    int range = 0;
    int range_threshold = 0;
    comm_size = comm_ptr->local_size;

    MPID_Datatype_get_size_macro(sendtype, sendtype_size);
    MPID_Datatype_get_size_macro(recvtype, recvtype_size);
    nbytes = sendtype_size * sendcount;

    /* Search for the corresponding system size inside the tuning table */
    while ((range < (mv2_size_alltoall_tuning_table - 1)) &&
           (comm_size > mv2_alltoall_thresholds_table[range].numproc)) {
        range++;
    }    
    /* Search for corresponding inter-leader function */
    while ((range_threshold < (mv2_alltoall_thresholds_table[range].size_table - 1))
           && (nbytes >
               mv2_alltoall_thresholds_table[range].algo_table[range_threshold].max)
           && (mv2_alltoall_thresholds_table[range].algo_table[range_threshold].max != -1)) {
        range_threshold++;
    }     
    MV2_Alltoall_function = mv2_alltoall_thresholds_table[range].algo_table[range_threshold]
                                .MV2_pt_Alltoall_function;

    if(sendbuf != MPI_IN_PLACE) {  
        mpi_errno = MV2_Alltoall_function(sendbuf, sendcount, sendtype,
            	                              recvbuf, recvcount, recvtype,
                    	                      comm_ptr, errflag );
    } else {
        range_threshold = 0; 
        if(nbytes < 
          mv2_alltoall_thresholds_table[range].in_place_algo_table[range_threshold].min
          ||nbytes > mv2_alltoall_thresholds_table[range].in_place_algo_table[range_threshold].max
          ) {
            tmp_buf = (char *)MPIU_Malloc( comm_size * recvcount * recvtype_size );
            mpi_errno = MPIR_Localcopy((char *)recvbuf,
                                       comm_size*recvcount, recvtype,
                                       (char *)tmp_buf,
                                       comm_size*recvcount, recvtype);

            mpi_errno = MV2_Alltoall_function(tmp_buf, recvcount, recvtype,
                                               recvbuf, recvcount, recvtype,
                                               comm_ptr, errflag );        
            MPIU_Free(tmp_buf);
        } else { 
            mpi_errno = MPIR_Alltoall_inplace_MV2(sendbuf, sendcount, sendtype,
                                              recvbuf, recvcount, recvtype,
                                              comm_ptr, errflag );
        } 
    }
    return (mpi_errno);
}


/* old version of MPIR_Alltoall_intra_MV2 */
int MPIR_Alltoall_intra_MV2( 
    const void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype, 
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr, 
    int *errflag )
{
    int          comm_size, i, j, pof2;
    MPI_Aint     sendtype_extent, recvtype_extent;
    MPI_Aint recvtype_true_extent, recvbuf_extent, recvtype_true_lb;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int src, dst, rank, nbytes;
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
                    mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + 
                                                      j*recvcount*recvtype_extent),
                                                      recvcount, recvtype,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      j, MPIR_ALLTOALL_TAG,
                                                      comm, &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
                else if (rank == j) {
                    /* same as above with i/j args reversed */
                    mpi_errno = MPIC_Sendrecv_replace_ft(((char *)recvbuf + 
                                                      i*recvcount*recvtype_extent),
                                                      recvcount, recvtype,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      i, MPIR_ALLTOALL_TAG,
                                                      comm, &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                }
            }
        }
    }  

    else if ((nbytes <= mv2_coll_param.alltoall_small_msg) && (comm_size >= 8)
#if defined(_ENABLE_CUDA_)
    /* use Isend/Irecv and pairwise in cuda configuration*/
    && !rdma_enable_cuda
#endif 
    ) {

        /* use the indexing algorithm by Jehoshua Bruck et al,
         * IEEE TPDS, Nov. 97 */ 

        /* allocate temporary buffer */
        MPIR_Pack_size_impl(recvcount*comm_size, recvtype, &pack_size);
        tmp_buf = MPIU_Malloc(pack_size);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                              FCNAME, __LINE__, 
                                              MPI_ERR_OTHER, "**nomem", 0 );
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
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                              FCNAME, __LINE__, 
                                              MPI_ERR_OTHER, "**nomem", 0 );
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

            mpi_errno = MPIR_Type_create_indexed_block_impl(count, recvcount, 
                                               displs, recvtype, &newtype);
	        if (mpi_errno) { 
                MPIU_ERR_POP(mpi_errno); 
            }

            mpi_errno = MPIR_Type_commit_impl(&newtype);
	        if (mpi_errno) { 
                MPIU_ERR_POP(mpi_errno); 
            }

            position = 0;
            mpi_errno = MPIR_Pack_impl(recvbuf, 1, newtype, tmp_buf, pack_size, &position);
	        if (mpi_errno) { 
                MPIU_ERR_POP(mpi_errno); 
            }

            mpi_errno = MPIC_Sendrecv_ft(tmp_buf, position, MPI_PACKED, dst,
                                      MPIR_ALLTOALL_TAG, recvbuf, 1, newtype,
                                      src, MPIR_ALLTOALL_TAG, comm,
                                      MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                   /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            MPIR_Type_free_impl(&newtype);

            pof2 *= 2;
        }

        MPIU_Free(displs);
        MPIU_Free(tmp_buf);

        /* Rotate blocks in recvbuf upwards by (rank + 1) blocks. Need
         * a temporary buffer of the same size as recvbuf. */
        
        /* get true extent of recvtype */
        MPIR_Type_get_true_extent_impl(recvtype, &recvtype_true_lb,
                                                    &recvtype_true_extent);  
        recvbuf_extent = recvcount * comm_size *
            (MPIR_MAX(recvtype_true_extent, recvtype_extent));
        tmp_buf = MPIU_Malloc(recvbuf_extent);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                              FCNAME, __LINE__, 
                                              MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - recvtype_true_lb);

        mpi_errno = MPIR_Localcopy((char *) recvbuf + (rank+1)*recvcount*recvtype_extent, 
                       (comm_size - rank - 1)*recvcount, recvtype, tmp_buf, 
                       (comm_size - rank - 1)*recvcount, recvtype);
	if (mpi_errno) { 
             MPIU_ERR_POP(mpi_errno); 
        }
        mpi_errno = MPIR_Localcopy(recvbuf, (rank+1)*recvcount, recvtype, 
                       (char *) tmp_buf + (comm_size-rank-1)*recvcount*recvtype_extent, 
                       (rank+1)*recvcount, recvtype);
	if (mpi_errno) { 
              MPIU_ERR_POP(mpi_errno); 
        }

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
        MPIR_Type_get_true_extent_impl(sendtype, &sendtype_true_lb,
                                              &sendtype_true_extent);  
        sendbuf_extent = sendcount * comm_size *
            (MPIR_MAX(sendtype_true_extent, sendtype_extent));
        tmp_buf = MPIU_Malloc(sendbuf_extent*comm_size);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                              FCNAME, __LINE__, 
                                              MPI_ERR_OTHER, "**nomem", 0 );
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
	if (mpi_errno) { 
           MPIU_ERR_POP(mpi_errno);
        }
        
        mask = 0x1;
        i = 0;
        while (mask < comm_size) {
            dst = rank ^ mask;
            
            dst_tree_root = dst >> i;
            dst_tree_root <<= i;
            
            my_tree_root = rank >> i;
            my_tree_root <<= i;
            
            if (dst < comm_size) {
                mpi_errno = MPIC_Sendrecv_ft(((char *)tmp_buf +
                                           my_tree_root*sendbuf_extent),
                                          curr_cnt, sendtype,
                                          dst, MPIR_ALLTOALL_TAG, 
                                          ((char *)tmp_buf +
                                           dst_tree_root*sendbuf_extent),
					  sendbuf_extent*(comm_size-dst_tree_root),
                                          sendtype, dst, MPIR_ALLTOALL_TAG, 
                                          comm, &status, errflag);
                if (mpi_errno) {
                       /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                
                /* in case of non-power-of-two nodes, less data may be
                   received than specified */
                MPIR_Get_count_impl(&status, sendtype, &last_recv_cnt);
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
                        mpi_errno = MPIC_Send_ft(((char *)tmp_buf +
                                               dst_tree_root*sendbuf_extent),
                                              last_recv_cnt, sendtype,
                                              dst, MPIR_ALLTOALL_TAG,
                                              comm, errflag);  
                        if (mpi_errno) {
                            /* for communication errors, just record the error but continue */
                             *errflag = TRUE;
                             MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                             MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        }
  
                    /* recv only if this proc. doesn't have data and sender
                       has data */
                    else if ((dst < rank) && 
                             (dst < tree_root + nprocs_completed) &&
                             (rank >= tree_root + nprocs_completed)) {
                        mpi_errno = MPIC_Recv_ft(((char *)tmp_buf +
                                               dst_tree_root*sendbuf_extent),
					      sendbuf_extent*(comm_size-dst_tree_root),
                                              sendtype,   
                                              dst, MPIR_ALLTOALL_TAG,
                                              comm, &status, errflag); 
                        if (mpi_errno) {
                             /* for communication errors, just record the error but continue */
                              *errflag = TRUE;
                              MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                              MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        }

			if (mpi_errno) { 
                            MPIU_ERR_POP(mpi_errno); 
                        }
                        MPIR_Get_count_impl(&status, sendtype, &last_recv_cnt);
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
	    if (mpi_errno) { 
              MPIU_ERR_POP(mpi_errno); 
            }
        }
        
        MPIU_Free((char *)tmp_buf+sendtype_true_lb); 
#endif

     } else if (nbytes <= mv2_coll_param.alltoall_medium_msg) {
        /* Medium-size message. Use isend/irecv with scattered
           destinations. Use Tony Ladd's modification to post only
           a small number of isends/irecvs at a time. */
        /* FIXME: This converts the Alltoall to a set of blocking phases.
           Two alternatives should be considered:
           1) the choice of communication pattern could try to avoid
              contending routes in each phase
           2) rather than wait for all communication to finish (waitall),
              we could maintain constant queue size by using waitsome
              and posting new isend/irecv as others complete.  This avoids
              synchronization delays at the end of each block (when
              there are only a few isend/irecvs left)
         */
        int ii, ss, bblock;
        
        MPIU_CHKLMEM_DECL(2);
	
        bblock = mv2_coll_param.alltoall_throttle_factor;

        if (bblock >= comm_size) bblock = comm_size;
        /* If throttle_factor is n, each process posts n pairs of isend/irecv 
           in each iteration. */ 

        /* FIXME: This should use the memory macros (there are storage
           leaks here if there is an error, for example) */
        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, 2*bblock*sizeof(MPI_Request), 
                            mpi_errno, "reqarray");

        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, 2*bblock*sizeof(MPI_Status), 
                            mpi_errno, "starray");

        for (ii=0; ii<comm_size; ii+=bblock) {
            ss = comm_size-ii < bblock ? comm_size-ii : bblock;
            /* do the communication -- post ss sends and receives: */
            for ( i=0; i<ss; i++ ) {
                dst = (rank+i+ii) % comm_size;
                mpi_errno = MPIC_Irecv_ft((char *)recvbuf +
                                          dst*recvcount*recvtype_extent,
                                          recvcount, recvtype, dst,
                                          MPIR_ALLTOALL_TAG, comm,
                                          &reqarray[i]);
                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            }

            for ( i=0; i<ss; i++ ) {
                dst = (rank-i-ii+comm_size) % comm_size;
                mpi_errno = MPIC_Isend_ft((char *)sendbuf +
                                          dst*sendcount*sendtype_extent,
                                          sendcount, sendtype, dst,
                                          MPIR_ALLTOALL_TAG, comm,
                                          &reqarray[i+ss], errflag);
                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            }

            /* ... then wait for them to finish: */
            mpi_errno = MPIC_Waitall_ft(2*ss,reqarray,starray, errflag);
            if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) { 
                  MPIU_ERR_POP(mpi_errno);
            } 

            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno == MPI_ERR_IN_STATUS) {
                for (j=0; j<2*ss; j++) {
                    if (starray[j].MPI_ERROR != MPI_SUCCESS) {
                        mpi_errno = starray[j].MPI_ERROR;
                    }
                }
            }
        }
    /* --END ERROR HANDLING-- */
        MPIU_CHKLMEM_FREEALL();
    } else {
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

	if (i == comm_size && mv2_use_xor_alltoall == 1) {
            pof2 = 1;
        } else  {
            pof2 = 0;
        }

        /* Do the pairwise exchanges */
        for (i=1; i<comm_size; i++) {
            if (pof2 == 1) {
                /* use exclusive-or algorithm */
                src = dst = rank ^ i;
            } else {
                src = (rank - i + comm_size) % comm_size;
                dst = (rank + i) % comm_size;
            }

            mpi_errno = MPIC_Sendrecv_ft(((char *)sendbuf +
                                       dst*sendcount*sendtype_extent), 
                                      sendcount, sendtype, dst,
                                      MPIR_ALLTOALL_TAG, 
                                      ((char *)recvbuf +
                                       src*recvcount*recvtype_extent),
                                      recvcount, recvtype, src,
                                      MPIR_ALLTOALL_TAG, comm, &status, 
                                      errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    }
 

 fn_fail:    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}

/* end:nested */
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

#undef FUNCNAME
#define FUNCNAME MPIR_Alltoall_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Alltoall_MV2(const void *sendbuf, int sendcount, MPI_Datatype sendtype,
                  void *recvbuf, int recvcount, MPI_Datatype recvtype,
                  MPID_Comm *comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#ifdef _ENABLE_CUDA_
    MPI_Aint sendtype_extent, recvtype_extent;
    int comm_size, nbytes = 0, snbytes = 0;
    int send_mem_type = 0, recv_mem_type = 0;

    if (rdma_enable_cuda) {
        if (sendbuf != MPI_IN_PLACE) { 
            send_mem_type = is_device_buffer(sendbuf);
        }
        recv_mem_type = is_device_buffer(recvbuf);
    }

    comm_size = comm_ptr->local_size;

    MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
    MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
    if (sendbuf != MPI_IN_PLACE) {
        snbytes = sendtype_extent * sendcount;
    }
    nbytes = recvtype_extent * recvcount;

    /*Handling Non-contig datatypes*/
    if (rdma_enable_cuda && (send_mem_type || recv_mem_type) &&
        (nbytes < rdma_cuda_block_size && snbytes < rdma_cuda_block_size)) {
        cuda_coll_pack(&sendbuf, &sendcount, &sendtype,
                        &recvbuf, &recvcount, &recvtype,
                        0, comm_size, comm_size);

        MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
        MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
        if (sendbuf != MPI_IN_PLACE) {
            snbytes = sendtype_extent * sendcount;
        }
        nbytes = recvtype_extent * recvcount;
    }

    if (rdma_enable_cuda && 
        rdma_cuda_alltoall_dynamic &&
        send_mem_type && recv_mem_type &&
        nbytes <= rdma_cuda_block_size &&
        snbytes <= rdma_cuda_block_size &&
        nbytes*comm_size > rdma_cuda_block_size) {
        mpi_errno = MPIR_Alltoall_CUDA_intra_MV2(sendbuf, sendcount, sendtype,
                                       recvbuf, recvcount, recvtype,
                                       comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        goto fn_exit;
    } else if (rdma_enable_cuda &&
        rdma_cuda_use_naive && 
        (send_mem_type || recv_mem_type) &&
        nbytes <= rdma_cuda_alltoall_naive_limit) {
        if (sendbuf != MPI_IN_PLACE) {
             mpi_errno = cuda_stage_alloc (&sendbuf,
                           sendcount*sendtype_extent*comm_size,
                           &recvbuf, recvcount*recvtype_extent*comm_size,
                           send_mem_type, recv_mem_type,
                           0);
        } else {
             mpi_errno = cuda_stage_alloc (&sendbuf,
                           recvcount*recvtype_extent*comm_size,
                           &recvbuf, recvcount*recvtype_extent*comm_size,
                           send_mem_type, recv_mem_type,
                           0);
        }
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
#endif /*#ifdef _ENABLE_CUDA_*/
    if(mv2_use_old_alltoall == 0 ) {
        mpi_errno = MPIR_Alltoall_tune_intra_MV2(sendbuf, sendcount, sendtype,
                	                        recvbuf, recvcount, recvtype,
        	                                comm_ptr, errflag);
     } else {	
    	mpi_errno = MPIR_Alltoall_intra_MV2(sendbuf, sendcount, sendtype,
        	                                recvbuf, recvcount, recvtype,
                	                        comm_ptr, errflag);
     }

#else /*#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)*/
    mpi_errno = MPIR_Alltoall_intra(sendbuf, sendcount, sendtype,
                                        recvbuf, recvcount, recvtype,
                                        comm_ptr, errflag);
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

#ifdef _ENABLE_CUDA_ 
    if (rdma_enable_cuda && 
        rdma_cuda_use_naive &&
        (send_mem_type || recv_mem_type) &&
        nbytes <= rdma_cuda_alltoall_naive_limit) {
        cuda_stage_free (&sendbuf, 
                        &recvbuf, recvcount*recvtype_extent*comm_size,
                        send_mem_type, recv_mem_type);
    }

#endif /*#ifdef _ENABLE_CUDA_*/
    
 fn_exit:
#ifdef _ENABLE_CUDA_
    if (rdma_enable_cuda && (send_mem_type || recv_mem_type)) {
        cuda_coll_unpack(&recvcount, comm_size);
    }
#endif
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

