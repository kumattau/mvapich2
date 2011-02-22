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

/* Implements the "mirror permutation" of "bits" bits of an integer "x".

   positions 76543210, bits==3 yields 76543012.

   This function could/should be moved to a common utility location for use in
   other collectives as well. */
ATTRIBUTE((const)) /* tells the compiler that this func only depends on its args
                      and may be optimized much more aggressively, similar to "pure" */
static inline int mirror_permutation(unsigned int x, int bits)
{
    /* a mask for the high order bits that should be copied as-is */
    int high_mask = ~((0x1 << bits) - 1);
    int retval = x & high_mask;
    int i;

    for (i = 0; i < bits; ++i) {
        unsigned int bitval = (x & (0x1 << i)) >> i; /* 0x1 or 0x0 */
        retval |= bitval << ((bits - i) - 1);
    }

    return retval;
}

/* FIXME should we be checking the op_errno here? */
#ifdef HAVE_CXX_BINDING
/* NOTE: assumes 'uop' is the operator function pointer and
   that 'is_cxx_uop' is is a boolean indicating the obvious */
#define call_uop(in_, inout_, count_, datatype_)                                     \
do {                                                                                 \
    if (is_cxx_uop) {                                                                \
        (*MPIR_Process.cxx_call_op_fn)((in_), (inout_), (count_), (datatype_), uop); \
    }                                                                                \
    else {                                                                           \
        (*uop)((in_), (inout_), &(count_), &(datatype_));                            \
    }                                                                                \
} while (0)

#else
#define call_uop(in_, inout_, count_, datatype_)      \
    (*uop)((in_), (inout_), &(count_), &(datatype_))
#endif

/* Implements the reduce-scatter butterfly algorithm described in J. L. Traff's
 * "An Improved Algorithm for (Non-commutative) Reduce-Scatter with an Application"
 * from EuroPVM/MPI 2005.  This function currently only implements support for
 * the power-of-2, block-regular case (all receive counts are equal). */
#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_scatter_noncomm
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Reduce_scatter_noncomm (
    void *sendbuf,
    void *recvbuf,
    int *recvcnts,
    MPI_Datatype datatype,
    MPI_Op op,
    MPID_Comm *comm_ptr )
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size = comm_ptr->local_size;
    int rank = comm_ptr->rank;
    int pof2;
    int log2_comm_size;
    int i, k;
    int recv_offset, send_offset;
    int block_size, total_count, size;
    MPI_Aint extent, true_extent, true_lb;
    int is_commutative;
    int buf0_was_inout;
    void *tmp_buf0;
    void *tmp_buf1;
    void *result_ptr;
    MPI_Comm comm = comm_ptr->handle;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(3);

    MPID_Datatype_get_extent_macro(datatype, extent);
    /* assumes nesting is handled by the caller right now, may not be true in the future */
    mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb, &true_extent);

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op%16 - 1];
    }
    else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE)
            is_commutative = 0;
        else
            is_commutative = 1;

#ifdef HAVE_CXX_BINDING
        if (op_ptr->language == MPID_LANG_CXX) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
            is_cxx_uop = 1;
        }
        else
#endif
        if ((op_ptr->language == MPID_LANG_C))
            uop = (MPI_User_function *) op_ptr->function.c_function;
        else
            uop = (MPI_User_function *) op_ptr->function.f77_function;
    }

    pof2 = 1;
    log2_comm_size = 0;
    while (pof2 < comm_size) {
        pof2 <<= 1;
        ++log2_comm_size;
    }

    /* begin error checking */
    MPIU_Assert(pof2 == comm_size); /* FIXME this version only works for power of 2 procs */

    for (i = 0; i < (comm_size - 1); ++i) {
        MPIU_Assert(recvcnts[i] == recvcnts[i+1]);
    }
    /* end error checking */

    /* size of a block (count of datatype per block, NOT bytes per block) */
    block_size = recvcnts[0];
    total_count = block_size * comm_size;

    MPIU_CHKLMEM_MALLOC(tmp_buf0, void *, true_extent * total_count, mpi_errno, "tmp_buf0");
    MPIU_CHKLMEM_MALLOC(tmp_buf1, void *, true_extent * total_count, mpi_errno, "tmp_buf1");
    /* adjust for potential negative lower bound in datatype */
    tmp_buf0 = (void *)((char*)tmp_buf0 - true_lb);
    tmp_buf1 = (void *)((char*)tmp_buf1 - true_lb);

    /* Copy our send data to tmp_buf0.  We do this one block at a time and
       permute the blocks as we go according to the mirror permutation. */
    for (i = 0; i < comm_size; ++i) {
        mpi_errno = MPIR_Localcopy((char *)(sendbuf == MPI_IN_PLACE ? recvbuf : sendbuf) + (i * true_extent * block_size), block_size, datatype,
                                   (char *)tmp_buf0 + (mirror_permutation(i, log2_comm_size) * true_extent * block_size), block_size, datatype);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }
    buf0_was_inout = 1;

    send_offset = 0;
    recv_offset = 0;
    size = total_count;
    for (k = 0; k < log2_comm_size; ++k) {
        /* use a double-buffering scheme to avoid local copies */
        char *incoming_data = (buf0_was_inout ? tmp_buf1 : tmp_buf0);
        char *outgoing_data = (buf0_was_inout ? tmp_buf0 : tmp_buf1);
        int peer = rank ^ (0x1 << k);
        size /= 2;

        if (rank > peer) {
            /* we have the higher rank: send top half, recv bottom half */
            recv_offset += size;
        }
        else {
            /* we have the lower rank: recv top half, send bottom half */
            send_offset += size;
        }

        mpi_errno = MPIC_Sendrecv(outgoing_data + send_offset*true_extent,
                                  size, datatype, peer, MPIR_REDUCE_SCATTER_TAG,
                                  incoming_data + recv_offset*true_extent,
                                  size, datatype, peer, MPIR_REDUCE_SCATTER_TAG,
                                  comm, MPI_STATUS_IGNORE);
        /* always perform the reduction at recv_offset, the data at send_offset
           is now our peer's responsibility */
        if (rank > peer) {
            /* higher ranked value so need to call op(received_data, my_data) */
            call_uop(incoming_data + recv_offset*true_extent,
                     outgoing_data + recv_offset*true_extent,
                     size, datatype);
            buf0_was_inout = buf0_was_inout;
        }
        else {
            /* lower ranked value so need to call op(my_data, received_data) */
            call_uop(outgoing_data + recv_offset*true_extent,
                     incoming_data + recv_offset*true_extent,
                     size, datatype);
            buf0_was_inout = !buf0_was_inout;
        }

        /* the next round of send/recv needs to happen within the block (of size
           "size") that we just received and reduced */
        send_offset = recv_offset;
    }

    MPIU_Assert(size == recvcnts[rank]);

    /* copy the reduced data to the recvbuf */
    result_ptr = (char *)(buf0_was_inout ? tmp_buf0 : tmp_buf1) + recv_offset * true_extent;
    mpi_errno = MPIR_Localcopy(result_ptr, size, datatype,
                               recvbuf, size, datatype);
fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

/* This is the default implementation of reduce_scatter. The algorithm is:

   Algorithm: MPI_Reduce_scatter

   If the operation is commutative, for short and medium-size
   messages, we use a recursive-halving
   algorithm in which the first p/2 processes send the second n/2 data
   to their counterparts in the other half and receive the first n/2
   data from them. This procedure continues recursively, halving the
   data communicated at each step, for a total of lgp steps. If the
   number of processes is not a power-of-two, we convert it to the
   nearest lower power-of-two by having the first few even-numbered
   processes send their data to the neighboring odd-numbered process
   at (rank+1). Those odd-numbered processes compute the result for
   their left neighbor as well in the recursive halving algorithm, and
   then at  the end send the result back to the processes that didn't
   participate.
   Therefore, if p is a power-of-two,
   Cost = lgp.alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma
   If p is not a power-of-two,
   Cost = (floor(lgp)+2).alpha + n.(1+(p-1+n)/p).beta + n.(1+(p-1)/p).gamma
   The above cost in the non power-of-two case is approximate because
   there is some imbalance in the amount of work each process does
   because some processes do the work of their neighbors as well.

   For commutative operations and very long messages we use
   we use a pairwise exchange algorithm similar to
   the one used in MPI_Alltoall. At step i, each process sends n/p
   amount of data to (rank+i) and receives n/p amount of data from
   (rank-i).
   Cost = (p-1).alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma


   If the operation is not commutative, we do the following:

   We use a recursive doubling algorithm, which
   takes lgp steps. At step 1, processes exchange (n-n/p) amount of
   data; at step 2, (n-2n/p) amount of data; at step 3, (n-4n/p)
   amount of data, and so forth.

   Cost = lgp.alpha + n.(lgp-(p-1)/p).beta + n.(lgp-(p-1)/p).gamma

   Possible improvements:

   End Algorithm: MPI_Reduce_scatter
*/

#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_scatter
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Reduce_scatter_OSU ( 
    void *sendbuf, 
    void *recvbuf, 
    int *recvcnts, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    MPID_Comm *comm_ptr )
{
    int   rank, comm_size, i;
    MPI_Aint extent, true_extent, true_lb; 
    int  *disps;
    void *tmp_recvbuf, *tmp_results;
    int   mpi_errno = MPI_SUCCESS;
    int type_size, dis[2], blklens[2], total_count, nbytes, src, dst;
    int mask, dst_tree_root, my_tree_root, j, k;
    int *newcnts, *newdisps, rem, newdst, send_idx, recv_idx,
        last_idx, send_cnt, recv_cnt;
    int pof2, old_i, newrank, received;
    MPI_Datatype sendtype, recvtype;
    int nprocs_completed, tmp_mask, tree_root, is_commutative;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(5);

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_GET;
    MPIU_THREADPRIV_FIELD(op_errno) = 0;

    MPIR_Nest_incr();

    MPID_Datatype_get_extent_macro(datatype, extent);
    mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb,
                                          &true_extent);  
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op%16 - 1];
    }
    else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE)
            is_commutative = 0;
        else
            is_commutative = 1;

#ifdef HAVE_CXX_BINDING            
	if (op_ptr->language == MPID_LANG_CXX) {
	    uop = (MPI_User_function *) op_ptr->function.c_function;
	    is_cxx_uop = 1;
	}
	else
#endif
        if ((op_ptr->language == MPID_LANG_C)) { 
            uop = (MPI_User_function *) op_ptr->function.c_function;
        } 
        else { 
            uop = (MPI_User_function *) op_ptr->function.f77_function;
        } 
    }

    MPIU_CHKLMEM_MALLOC(disps, int *, comm_size * sizeof(int), mpi_errno, "disps");

    total_count = 0;
    for (i=0; i<comm_size; i++) {
        disps[i] = total_count;
        total_count += recvcnts[i];
    }
    
    if (total_count == 0) {
        goto fn_exit;
    }

    MPID_Datatype_get_size_macro(datatype, type_size);
    nbytes = total_count * type_size;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    /* total_count*extent eventually gets malloced. it isn't added to
     * a user-passed in buffer */
    MPID_Ensure_Aint_fits_in_pointer(total_count * MPIR_MAX(true_extent, extent));

    if ((is_commutative) && (nbytes < MPIR_REDSCAT_COMMUTATIVE_LONG_MSG)) {
        /* commutative and short. use recursive halving algorithm */

        /* allocate temp. buffer to receive incoming data */
        MPIU_CHKLMEM_MALLOC(tmp_recvbuf, void *, total_count*(MPIR_MAX(true_extent,extent)), mpi_errno, "tmp_recvbuf");
        /* adjust for potential negative lower bound in datatype */
        tmp_recvbuf = (void *)((char*)tmp_recvbuf - true_lb);
            
        /* need to allocate another temporary buffer to accumulate
           results because recvbuf may not be big enough */
        MPIU_CHKLMEM_MALLOC(tmp_results, void *, total_count*(MPIR_MAX(true_extent,extent)), mpi_errno, "tmp_results");
        /* adjust for potential negative lower bound in datatype */
        tmp_results = (void *)((char*)tmp_results - true_lb);
        
        /* copy sendbuf into tmp_results */
        if (sendbuf != MPI_IN_PLACE) { 
            mpi_errno = MPIR_Localcopy(sendbuf, total_count, datatype,
                                       tmp_results, total_count, datatype);
        } 
        else { 
            mpi_errno = MPIR_Localcopy(recvbuf, total_count, datatype,
                                       tmp_results, total_count, datatype);
        } 
        
        if (mpi_errno) { 
            MPIU_ERR_POP(mpi_errno);
        } 

        pof2 = 1;
        while (pof2 <= comm_size) pof2 <<= 1;
        pof2 >>=1;

        rem = comm_size - pof2;

        /* In the non-power-of-two case, all even-numbered
           processes of rank < 2*rem send their data to
           (rank+1). These even-numbered processes no longer
           participate in the algorithm until the very end. The
           remaining processes form a nice power-of-two. */

        if (rank < 2*rem) {
            if (rank % 2 == 0) { /* even */
                mpi_errno = MPIC_Send(tmp_results, total_count, 
                                      datatype, rank+1,
                                      MPIR_REDUCE_SCATTER_TAG, comm);
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
                
                /* temporarily set the rank to -1 so that this
                   process does not pariticipate in recursive
                   doubling */
                newrank = -1; 
            }
            else { /* odd */
                mpi_errno = MPIC_Recv(tmp_recvbuf, total_count, 
                                      datatype, rank-1,
                                      MPIR_REDUCE_SCATTER_TAG, comm,
                                      MPI_STATUS_IGNORE);
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
                
                /* do the reduction on received data. since the
                   ordering is right, it doesn't matter whether
                   the operation is commutative or not. */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn)( tmp_recvbuf, tmp_results, 
                                                    total_count,
                                                    datatype,
                                                    uop ); 
                }
                else 
#endif
                    (*uop)(tmp_recvbuf, tmp_results, &total_count, &datatype);
                
                /* change the rank */
                newrank = rank / 2;
            }
        }
        else  /* rank >= 2*rem */
            newrank = rank - rem;

        if (newrank != -1) {
            /* recalculate the recvcnts and disps arrays because the
               even-numbered processes who no longer participate will
               have their result calculated by the process to their
               right (rank+1). */

            MPIU_CHKLMEM_MALLOC(newcnts, int *, pof2*sizeof(int), mpi_errno, "newcnts");
            MPIU_CHKLMEM_MALLOC(newdisps, int *, pof2*sizeof(int), mpi_errno, "newdisps");
            
            for (i=0; i<pof2; i++) {
                /* what does i map to in the old ranking? */
                old_i = (i < rem) ? i*2 + 1 : i + rem;
                if (old_i < 2*rem) {
                    /* This process has to also do its left neighbor's
                       work */
                    newcnts[i] = recvcnts[old_i] + recvcnts[old_i-1];
                }
                else
                    newcnts[i] = recvcnts[old_i];
            }
            
            newdisps[0] = 0;
            for (i=1; i<pof2; i++)
                newdisps[i] = newdisps[i-1] + newcnts[i-1];

            mask = pof2 >> 1;
            send_idx = recv_idx = 0;
            last_idx = pof2;
            while (mask > 0) {
                newdst = newrank ^ mask;
                /* find real rank of dest */
                dst = (newdst < rem) ? newdst*2 + 1 : newdst + rem;
                
                send_cnt = recv_cnt = 0;
                if (newrank < newdst) {
                    send_idx = recv_idx + mask;
                    for (i=send_idx; i<last_idx; i++)
                        send_cnt += newcnts[i];
                    for (i=recv_idx; i<send_idx; i++)
                        recv_cnt += newcnts[i];
                }
                else {
                    recv_idx = send_idx + mask;
                    for (i=send_idx; i<recv_idx; i++)
                        send_cnt += newcnts[i];
                    for (i=recv_idx; i<last_idx; i++)
                        recv_cnt += newcnts[i];
                }
                
                /* Send data from tmp_results. Recv into tmp_recvbuf */ 
                if ((send_cnt != 0) && (recv_cnt != 0)) 
                    mpi_errno = MPIC_Sendrecv((char *) tmp_results +
                                          newdisps[send_idx]*extent,
                                          send_cnt, datatype,  
                                          dst, MPIR_REDUCE_SCATTER_TAG, 
                                          (char *) tmp_recvbuf +
                                          newdisps[recv_idx]*extent,
                                          recv_cnt, datatype, dst,
                                          MPIR_REDUCE_SCATTER_TAG, comm,
                                          MPI_STATUS_IGNORE); 
                else if ((send_cnt == 0) && (recv_cnt != 0))
                    mpi_errno = MPIC_Recv((char *) tmp_recvbuf +
                                          newdisps[recv_idx]*extent,
                                          recv_cnt, datatype, dst,
                                          MPIR_REDUCE_SCATTER_TAG, comm,
                                          MPI_STATUS_IGNORE);
                else if ((recv_cnt == 0) && (send_cnt != 0))
                    mpi_errno = MPIC_Send((char *) tmp_results +
                                          newdisps[send_idx]*extent,
                                          send_cnt, datatype,  
                                          dst, MPIR_REDUCE_SCATTER_TAG,
                                          comm);  

                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                
                /* tmp_recvbuf contains data received in this step.
                   tmp_results contains data accumulated so far */
                
                if (recv_cnt) {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)((char *) tmp_recvbuf +
                                                   newdisps[recv_idx]*extent,
                                                   (char *) tmp_results + 
                                                   newdisps[recv_idx]*extent, 
                                                   recv_cnt, datatype, uop);
                    }
                    else 
#endif
                        (*uop)((char *) tmp_recvbuf + newdisps[recv_idx]*extent,
                             (char *) tmp_results + newdisps[recv_idx]*extent, 
                               &recv_cnt, &datatype);
                }

                /* update send_idx for next iteration */
                send_idx = recv_idx;
                last_idx = recv_idx + mask;
                mask >>= 1;
            }

            /* copy this process's result from tmp_results to recvbuf */
            if (recvcnts[rank]) {
                mpi_errno = MPIR_Localcopy((char *)tmp_results +
                                           disps[rank]*extent, 
                                           recvcnts[rank], datatype, recvbuf,
                                           recvcnts[rank], datatype);
                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            }
            
        }

        /* In the non-power-of-two case, all odd-numbered
           processes of rank < 2*rem send to (rank-1) the result they
           calculated for that process */
        if (rank < 2*rem) {
            if (rank % 2) { /* odd */
                if (recvcnts[rank-1]) 
                    mpi_errno = MPIC_Send((char *) tmp_results +
                                      disps[rank-1]*extent, recvcnts[rank-1],
                                      datatype, rank-1,
                                      MPIR_REDUCE_SCATTER_TAG, comm);
            }
            else  {   /* even */
                if (recvcnts[rank])  
                    mpi_errno = MPIC_Recv(recvbuf, recvcnts[rank],
                                      datatype, rank+1,
                                      MPIR_REDUCE_SCATTER_TAG, comm,
                                      MPI_STATUS_IGNORE); 
            }
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }
    
    if (is_commutative && (nbytes >= MPIR_REDSCAT_COMMUTATIVE_LONG_MSG)) {

        /* commutative and long message, or noncommutative and long message.
           use (p-1) pairwise exchanges */ 
        
        if (sendbuf != MPI_IN_PLACE) {
            /* copy local data into recvbuf */
            mpi_errno = MPIR_Localcopy(((char *)sendbuf+disps[rank]*extent),
                                       recvcnts[rank], datatype, recvbuf,
                                       recvcnts[rank], datatype);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        
        /* allocate temporary buffer to store incoming data */
        MPIU_CHKLMEM_MALLOC(tmp_recvbuf, void *, recvcnts[rank]*(MPIR_MAX(true_extent,extent))+1, mpi_errno, "tmp_recvbuf");
        /* adjust for potential negative lower bound in datatype */
        tmp_recvbuf = (void *)((char*)tmp_recvbuf - true_lb);
        
        for (i=1; i<comm_size; i++) {
            src = (rank - i + comm_size) % comm_size;
            dst = (rank + i) % comm_size;
            
            /* send the data that dst needs. recv data that this process
               needs from src into tmp_recvbuf */
            if (sendbuf != MPI_IN_PLACE) 
                mpi_errno = MPIC_Sendrecv(((char *)sendbuf+disps[dst]*extent), 
                                          recvcnts[dst], datatype, dst,
                                          MPIR_REDUCE_SCATTER_TAG, tmp_recvbuf,
                                          recvcnts[rank], datatype, src,
                                          MPIR_REDUCE_SCATTER_TAG, comm,
                                          MPI_STATUS_IGNORE);
            else
                mpi_errno = MPIC_Sendrecv(((char *)recvbuf+disps[dst]*extent), 
                                          recvcnts[dst], datatype, dst,
                                          MPIR_REDUCE_SCATTER_TAG, tmp_recvbuf,
                                          recvcnts[rank], datatype, src,
                                          MPIR_REDUCE_SCATTER_TAG, comm,
                                          MPI_STATUS_IGNORE);
            
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            
            if (is_commutative || (src < rank)) {
                if (sendbuf != MPI_IN_PLACE) {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)(tmp_recvbuf, 
                                                       recvbuf, 
                                                       recvcnts[rank], 
                                                       datatype, uop );
                    }
                    else 
#endif
                        (*uop)(tmp_recvbuf, recvbuf, &recvcnts[rank], 
                               &datatype); 
                }
                else {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)( tmp_recvbuf, 
                                                        ((char *)recvbuf+disps[rank]*extent), 
                                                        recvcnts[rank], datatype, uop ); 
                    }
                    else 
#endif
                        (*uop)(tmp_recvbuf, ((char *)recvbuf+disps[rank]*extent), 
                               &recvcnts[rank], &datatype); 
                    /* we can't store the result at the beginning of
                       recvbuf right here because there is useful data
                       there that other process/processes need. at the
                       end, we will copy back the result to the
                       beginning of recvbuf. */
                }
            }
            else {
                if (sendbuf != MPI_IN_PLACE) {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)( recvbuf, 
                                                        tmp_recvbuf, 
                                                        recvcnts[rank], 
                                                        datatype, uop );
                    }
                    else 
#endif
                        (*uop)(recvbuf, tmp_recvbuf, &recvcnts[rank], &datatype); 
                    /* copy result back into recvbuf */
                    mpi_errno = MPIR_Localcopy(tmp_recvbuf, recvcnts[rank], 
                                               datatype, recvbuf,
                                               recvcnts[rank], datatype); 
                }
                else {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)( 
                            ((char *)recvbuf+disps[rank]*extent),
                            tmp_recvbuf, recvcnts[rank], datatype, uop );   
                        
                    }
                    else 
#endif
                        (*uop)(((char *)recvbuf+disps[rank]*extent),
                               tmp_recvbuf, &recvcnts[rank], &datatype);   
                    /* copy result back into recvbuf */
                    mpi_errno = MPIR_Localcopy(tmp_recvbuf, recvcnts[rank], 
                                               datatype, 
                                               ((char *)recvbuf +
                                                disps[rank]*extent), 
                                               recvcnts[rank], datatype); 
                }
                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            }
        }
        
        /* if MPI_IN_PLACE, move output data to the beginning of
           recvbuf. already done for rank 0. */
        if ((sendbuf == MPI_IN_PLACE) && (rank != 0)) {
            mpi_errno = MPIR_Localcopy(((char *)recvbuf +
                                        disps[rank]*extent),  
                                       recvcnts[rank], datatype, 
                                       recvbuf, 
                                       recvcnts[rank], datatype); 
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }
    
    if (!is_commutative) {
        int is_block_regular = 1;
        for (i = 0; i < (comm_size - 1); ++i) {
            if (recvcnts[i] != recvcnts[i+1]) {
                is_block_regular = 0;
                break;
            }
        }

        /* slightly retask pof2 to mean pof2 equal or greater, not always greater as it is above */
        pof2 = 1;
        while (pof2 < comm_size) pof2 <<= 1;

        if (pof2 == comm_size && is_block_regular) {
            /* noncommutative, pof2 size, and block regular */
            mpi_errno = MPIR_Reduce_scatter_noncomm(sendbuf, recvbuf, recvcnts, datatype, op, comm_ptr);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        else {
            /* noncommutative and (non-pof2 or block irregular), use recursive doubling. */

            /* need to allocate temporary buffer to receive incoming data*/
            MPIU_CHKLMEM_MALLOC(tmp_recvbuf, void *, total_count*(MPIR_MAX(true_extent,extent)), mpi_errno, "tmp_recvbuf");
            /* adjust for potential negative lower bound in datatype */
            tmp_recvbuf = (void *)((char*)tmp_recvbuf - true_lb);

            /* need to allocate another temporary buffer to accumulate
               results */
            MPIU_CHKLMEM_MALLOC(tmp_results, void *, total_count*(MPIR_MAX(true_extent,extent)), mpi_errno, "tmp_results");
            /* adjust for potential negative lower bound in datatype */
            tmp_results = (void *)((char*)tmp_results - true_lb);

            /* copy sendbuf into tmp_results */
            if (sendbuf != MPI_IN_PLACE)
                mpi_errno = MPIR_Localcopy(sendbuf, total_count, datatype,
                                           tmp_results, total_count, datatype);
            else
                mpi_errno = MPIR_Localcopy(recvbuf, total_count, datatype,
                                           tmp_results, total_count, datatype);

            if (mpi_errno) MPIU_ERR_POP(mpi_errno);

            mask = 0x1;
            i = 0;
            while (mask < comm_size) {
                dst = rank ^ mask;

                dst_tree_root = dst >> i;
                dst_tree_root <<= i;

                my_tree_root = rank >> i;
                my_tree_root <<= i;

                /* At step 1, processes exchange (n-n/p) amount of
                   data; at step 2, (n-2n/p) amount of data; at step 3, (n-4n/p)
                   amount of data, and so forth. We use derived datatypes for this.

                   At each step, a process does not need to send data
                   indexed from my_tree_root to
                   my_tree_root+mask-1. Similarly, a process won't receive
                   data indexed from dst_tree_root to dst_tree_root+mask-1. */

                /* calculate sendtype */
                blklens[0] = blklens[1] = 0;
                for (j=0; j<my_tree_root; j++)
                    blklens[0] += recvcnts[j];
                for (j=my_tree_root+mask; j<comm_size; j++)
                    blklens[1] += recvcnts[j];

                dis[0] = 0;
                dis[1] = blklens[0];
                for (j=my_tree_root; (j<my_tree_root+mask) && (j<comm_size); j++)
                    dis[1] += recvcnts[j];

                NMPI_Type_indexed(2, blklens, dis, datatype, &sendtype);
                NMPI_Type_commit(&sendtype);

                /* calculate recvtype */
                blklens[0] = blklens[1] = 0;
                for (j=0; j<dst_tree_root && j<comm_size; j++)
                    blklens[0] += recvcnts[j];
                for (j=dst_tree_root+mask; j<comm_size; j++)
                    blklens[1] += recvcnts[j];

                dis[0] = 0;
                dis[1] = blklens[0];
                for (j=dst_tree_root; (j<dst_tree_root+mask) && (j<comm_size); j++)
                    dis[1] += recvcnts[j];

                NMPI_Type_indexed(2, blklens, dis, datatype, &recvtype);
                NMPI_Type_commit(&recvtype);

                received = 0;
                if (dst < comm_size) {
                    /* tmp_results contains data to be sent in each step. Data is
                       received in tmp_recvbuf and then accumulated into
                       tmp_results. accumulation is done later below.   */ 

                    mpi_errno = MPIC_Sendrecv(tmp_results, 1, sendtype, dst,
                                              MPIR_REDUCE_SCATTER_TAG, 
                                              tmp_recvbuf, 1, recvtype, dst,
                                              MPIR_REDUCE_SCATTER_TAG, comm,
                                              MPI_STATUS_IGNORE); 
                    received = 1;
                    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
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
                            /* send the current result */
                            mpi_errno = MPIC_Send(tmp_recvbuf, 1, recvtype,
                                                  dst, MPIR_REDUCE_SCATTER_TAG,
                                                  comm);  
                            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                        }
                        /* recv only if this proc. doesn't have data and sender
                           has data */
                        else if ((dst < rank) && 
                                 (dst < tree_root + nprocs_completed) &&
                                 (rank >= tree_root + nprocs_completed)) {
                            mpi_errno = MPIC_Recv(tmp_recvbuf, 1, recvtype, dst,
                                                  MPIR_REDUCE_SCATTER_TAG,
                                                  comm, MPI_STATUS_IGNORE); 
                            received = 1;
                            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                        }
                        tmp_mask >>= 1;
                        k--;
                    }
                }

                /* The following reduction is done here instead of after 
                   the MPIC_Sendrecv or MPIC_Recv above. This is
                   because to do it above, in the noncommutative 
                   case, we would need an extra temp buffer so as not to
                   overwrite temp_recvbuf, because temp_recvbuf may have
                   to be communicated to other processes in the
                   non-power-of-two case. To avoid that extra allocation,
                   we do the reduce here. */
                if (received) {
                    if (is_commutative || (dst_tree_root < my_tree_root)) {
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn)( tmp_recvbuf, 
                                                            tmp_results, blklens[0],
                                                            datatype, uop); 
                            (*MPIR_Process.cxx_call_op_fn)( 
                                ((char *)tmp_recvbuf + dis[1]*extent),
                                ((char *)tmp_results + dis[1]*extent),
                                blklens[1], datatype, uop ); 
                        }
                        else
#endif
                        {
                            (*uop)(tmp_recvbuf, tmp_results, &blklens[0],
                                   &datatype); 
                            (*uop)(((char *)tmp_recvbuf + dis[1]*extent),
                                   ((char *)tmp_results + dis[1]*extent),
                                   &blklens[1], &datatype); 
                        }
                    }
                    else {
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn)( tmp_results, 
                                                            tmp_recvbuf, blklens[0],
                                                            datatype, uop ); 
                            (*MPIR_Process.cxx_call_op_fn)( 
                                ((char *)tmp_results + dis[1]*extent),
                                ((char *)tmp_recvbuf + dis[1]*extent),
                                blklens[1], datatype, uop ); 
                        }
                        else 
#endif
                        {
                            (*uop)(tmp_results, tmp_recvbuf, &blklens[0],
                                   &datatype); 
                            (*uop)(((char *)tmp_results + dis[1]*extent),
                                   ((char *)tmp_recvbuf + dis[1]*extent),
                                   &blklens[1], &datatype); 
                        }
                        /* copy result back into tmp_results */
                        mpi_errno = MPIR_Localcopy(tmp_recvbuf, 1, recvtype, 
                                                   tmp_results, 1, recvtype);
                        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                    }
                }

                NMPI_Type_free(&sendtype);
                NMPI_Type_free(&recvtype);

                mask <<= 1;
                i++;
            }

            /* now copy final results from tmp_results to recvbuf */
            mpi_errno = MPIR_Localcopy(((char *)tmp_results+disps[rank]*extent),
                                       recvcnts[rank], datatype, recvbuf,
                                       recvcnts[rank], datatype); 
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }

fn_exit:
    MPIU_CHKLMEM_FREEALL();

    MPIR_Nest_decr();
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

    if (MPIU_THREADPRIV_FIELD(op_errno)) 
	mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);

    return (mpi_errno);
fn_fail:
    goto fn_exit;
}
/* end:nested */

#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_scatter
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Reduce_scatter_inter_OSU ( 
    void *sendbuf, 
    void *recvbuf, 
    int *recvcnts, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    MPID_Comm *comm_ptr )
{
/* Intercommunicator Reduce_scatter.
   We first do an intercommunicator reduce to rank 0 on left group,
   then an intercommunicator reduce to rank 0 on right group, followed
   by local intracommunicator scattervs in each group.
*/
    
    int rank, mpi_errno, root, local_size, total_count, i;
    MPI_Aint true_extent, true_lb = 0, extent;
    void *tmp_buf=NULL;
    int *disps=NULL;
    MPID_Comm *newcomm_ptr = NULL;

    rank = comm_ptr->rank;
    local_size = comm_ptr->local_size;

    total_count = 0;
    for (i=0; i<local_size; i++) total_count += recvcnts[i];

    if (rank == 0) {
        /* In each group, rank 0 allocates a temp. buffer for the 
           reduce */

        disps = MPIU_Malloc(local_size*sizeof(int));
	/* --BEGIN ERROR HANDLING-- */
        if (!disps) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */

        total_count = 0;
        for (i=0; i<local_size; i++) {
            disps[i] = total_count;
            total_count += recvcnts[i];
        }

        mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb,
                                              &true_extent);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        MPID_Datatype_get_extent_macro(datatype, extent);

        tmp_buf = MPIU_Malloc(total_count*(MPIR_MAX(extent,true_extent)));
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - true_lb);
    }

    /* first do a reduce from right group to rank 0 in left group,
       then from left group to rank 0 in right group*/
    if (comm_ptr->is_low_group) {
        /* reduce from right group to rank 0*/
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Reduce_inter(sendbuf, tmp_buf, total_count, datatype, op,
                                root, comm_ptr);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */

        /* reduce to rank 0 of right group */
        root = 0;
        mpi_errno = MPIR_Reduce_inter(sendbuf, tmp_buf, total_count, datatype, op,
                                root, comm_ptr);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }
    else {
        /* reduce to rank 0 of left group */
        root = 0;
        mpi_errno = MPIR_Reduce_inter(sendbuf, tmp_buf, total_count, datatype, op,
                                root, comm_ptr);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */

        /* reduce from right group to rank 0 */
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Reduce_inter(sendbuf, tmp_buf, total_count, datatype, op,
                                root, comm_ptr);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }

    /* Get the local intracommunicator */
    if (!comm_ptr->local_comm)
	MPIR_Setup_intercomm_localcomm( comm_ptr );

    newcomm_ptr = comm_ptr->local_comm;

    mpi_errno = MPIR_Scatterv(tmp_buf, recvcnts, disps, datatype, recvbuf,
                              recvcnts[rank], datatype, 0, newcomm_ptr);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    
    if (rank == 0) {
        MPIU_Free(disps);
        MPIU_Free((char*)tmp_buf+true_lb);
    }

    return mpi_errno;

}
/* end:nested */
