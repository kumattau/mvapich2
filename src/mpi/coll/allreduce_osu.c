/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2001-2013, The Ohio State University. All rights
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
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "coll_shmem.h"
#include "allreduce_tuning.h"
#include "bcast_tuning.h"
#ifdef MRAIL_GEN2_INTERFACE
#include <cr.h>
#endif

int (*MV2_Allreduce_function)(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)=NULL;


int (*MV2_Allreduce_intra_function)(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)=NULL;

/* This is the default implementation of allreduce. The algorithm is:
   
   Algorithm: MPI_Allreduce

   For the heterogeneous case, we call MPI_Reduce followed by MPI_Bcast
   in order to meet the requirement that all processes must have the
   same result. For the homogeneous case, we use the following algorithms.

   For long messages and for builtin ops and if count >= pof2 (where
   pof2 is the nearest power-of-two less than or equal to the number
   of processes), we use Rabenseifner's algorithm (see 
   http://www.hlrs.de/organization/par/services/models/mpi/myreduce.html ).
   This algorithm implements the allreduce in two steps: first a
   reduce-scatter, followed by an allgather. A recursive-halving
   algorithm (beginning with processes that are distance 1 apart) is
   used for the reduce-scatter, and a recursive doubling 
   algorithm is used for the allgather. The non-power-of-two case is
   handled by dropping to the nearest lower power-of-two: the first
   few even-numbered processes send their data to their right neighbors
   (rank+1), and the reduce-scatter and allgather happen among the remaining
   power-of-two processes. At the end, the first few even-numbered
   processes get the result from their right neighbors.

   For the power-of-two case, the cost for the reduce-scatter is 
   lgp.alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma. The cost for the
   allgather lgp.alpha + n.((p-1)/p).beta. Therefore, the
   total cost is:
   Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta + n.((p-1)/p).gamma

   For the non-power-of-two case, 
   Cost = (2.floor(lgp)+2).alpha + (2.((p-1)/p) + 2).n.beta + n.(1+(p-1)/p).gamma

   
   For short messages, for user-defined ops, and for count < pof2 
   we use a recursive doubling algorithm (similar to the one in
   MPI_Allgather). We use this algorithm in the case of user-defined ops
   because in this case derived datatypes are allowed, and the user
   could pass basic datatypes on one process and derived on another as
   long as the type maps are the same. Breaking up derived datatypes
   to do the reduce-scatter is tricky. 

   Cost = lgp.alpha + n.lgp.beta + n.lgp.gamma

   Possible improvements: 

   End Algorithm: MPI_Allreduce
*/

int MPIR_Allreduce_mcst_reduce_two_level_helper_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{ 
    return 0;
}

int MPIR_Allreduce_mcst_reduce_redscat_gather_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    return 0;
}

/* not declared static because a machine-specific function may call this one 
   in some cases */
/* This is flat p2p recursive-doubling allreduce */
#undef FCNAME
#define FCNAME "MPIR_Allreduce_pt2pt_rd_MV2"
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_pt2pt_rd_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int mask, dst, is_commutative, pof2, newrank = 0, rem, newdst;
    MPI_Aint true_lb, true_extent, extent;
    void *tmp_buf;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(3);

    if (count == 0) {
        return MPI_SUCCESS;
    }
    comm = comm_ptr->handle;

    MPIU_THREADPRIV_GET;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

    
    /* homogeneous */

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_FIELD(op_errno) = 0;

    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op % 16 - 1];
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }
#ifdef HAVE_CXX_BINDING
        if (op_ptr->language == MPID_LANG_CXX) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
            is_cxx_uop = 1;
        } else {
#endif
           if ((op_ptr->language == MPID_LANG_C)) {
               uop = (MPI_User_function *) op_ptr->function.c_function;
           } else {
               uop = (MPI_User_function *) op_ptr->function.f77_function;
           }
#ifdef HAVE_CXX_BINDING
        }
#endif
    }

    /* need to allocate temporary buffer to store incoming data */
    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);

    MPIU_CHKLMEM_MALLOC(tmp_buf, void *,
                        count * (MPIR_MAX(extent, true_extent)), mpi_errno,
                        "temporary buffer");

    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *) ((char *) tmp_buf - true_lb);

    /* copy local data into recvbuf */
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno =
            MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count,
                           datatype);
        MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                            "**fail");
    }

    /* find nearest power-of-two less than or equal to comm_size */
    pof2 = comm_ptr->ch.gpof2;

    rem = comm_size - pof2;

    /* In the non-power-of-two case, all even-numbered
       processes of rank < 2*rem send their data to
       (rank+1). These even-numbered processes no longer
       participate in the algorithm until the very end. The
       remaining processes form a nice power-of-two. */

    if (rank < 2 * rem) {
        if (rank % 2 == 0) {
            /* even */
            mpi_errno = MPIC_Send_ft(recvbuf, count, datatype, rank + 1,
                                     MPIR_ALLREDUCE_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            /* temporarily set the rank to -1 so that this
               process does not pariticipate in recursive
               doubling */
            newrank = -1;
        } else {
            /* odd */
            mpi_errno = MPIC_Recv_ft(tmp_buf, count, datatype, rank - 1,
                                     MPIR_ALLREDUCE_TAG, comm,
                                     MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            /* do the reduction on received data. since the
               ordering is right, it doesn't matter whether
               the operation is commutative or not. */
#ifdef HAVE_CXX_BINDING
            if (is_cxx_uop) {
                (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf, count,
                                                datatype, uop);
            } else {
#endif
               (*uop) (tmp_buf, recvbuf, &count, &datatype);

               /* change the rank */
               newrank = rank / 2;
#ifdef HAVE_CXX_BINDING
            }
#endif
        }
    } else {                /* rank >= 2*rem */
        newrank = rank - rem;
    }

    /* If op is user-defined or count is less than pof2, use
       recursive doubling algorithm. Otherwise do a reduce-scatter
       followed by allgather. (If op is user-defined,
       derived datatypes are allowed and the user could pass basic
       datatypes on one process and derived on another as long as
       the type maps are the same. Breaking up derived
       datatypes to do the reduce-scatter is tricky, therefore
       using recursive doubling in that case.) */

    if (newrank != -1) {
        mask = 0x1;
        while (mask < pof2) {
            newdst = newrank ^ mask;
            /* find real rank of dest */
            dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

            /* Send the most current data, which is in recvbuf. Recv
               into tmp_buf */
            mpi_errno = MPIC_Sendrecv_ft(recvbuf, count, datatype,
                                         dst, MPIR_ALLREDUCE_TAG,
                                         tmp_buf, count, datatype, dst,
                                         MPIR_ALLREDUCE_TAG, comm,
                                         MPI_STATUS_IGNORE, errflag);

            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            /* tmp_buf contains data received in this step.
               recvbuf contains data accumulated so far */

            if (is_commutative || (dst < rank)) {
                /* op is commutative OR the order is already right */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf,
                                                    count, datatype,
                                                    uop);
                } else {
#endif
                    (*uop) (tmp_buf, recvbuf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                }
#endif
            } else {
                /* op is noncommutative and the order is not right */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn) (recvbuf, tmp_buf,
                                                    count, datatype,
                                                    uop);
                } else {
#endif
                    (*uop) (recvbuf, tmp_buf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                }
#endif
                    /* copy result back into recvbuf */
                    mpi_errno = MPIR_Localcopy(tmp_buf, count, datatype,
                                                   recvbuf, count, datatype);
                    MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno,
                                            MPI_ERR_OTHER, "**fail");
                }
                    mask <<= 1;
            }
        }

    /* In the non-power-of-two case, all odd-numbered
       processes of rank < 2*rem send the result to
       (rank-1), the ranks who didn't participate above. */
    if (rank < 2 * rem) {
        if (rank % 2) {     /* odd */
            mpi_errno = MPIC_Send_ft(recvbuf, count,
                                     datatype, rank - 1,
                                     MPIR_ALLREDUCE_TAG, comm, errflag);
        } else {            /* even */

            mpi_errno = MPIC_Recv(recvbuf, count,
                                  datatype, rank + 1,
                                  MPIR_ALLREDUCE_TAG, comm,
                                  MPI_STATUS_IGNORE);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
    }

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;


}

/* not declared static because a machine-specific function may call this one 
   in some cases */
/* This is flat reduce-scatter-allgather allreduce */
#undef FCNAME
#define FCNAME "MPIR_Allreduce_pt2pt_rs_MV2"
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_pt2pt_rs_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int mask, dst, is_commutative, pof2, newrank = 0, rem, newdst, i,
        send_idx, recv_idx, last_idx, send_cnt, recv_cnt, *cnts, *disps;
    MPI_Aint true_lb, true_extent, extent;
    void *tmp_buf;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(3);

    if (count == 0) {
        return MPI_SUCCESS;
    }
    comm = comm_ptr->handle;

    MPIU_THREADPRIV_GET;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

    /* homogeneous */

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_FIELD(op_errno) = 0;

    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op % 16 - 1];
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }
#ifdef HAVE_CXX_BINDING
        if (op_ptr->language == MPID_LANG_CXX) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
            is_cxx_uop = 1;
        } else {
#endif
            if ((op_ptr->language == MPID_LANG_C)) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
            } else {
                uop = (MPI_User_function *) op_ptr->function.f77_function;
            }
#ifdef HAVE_CXX_BINDING
        }
#endif
    }

    /* need to allocate temporary buffer to store incoming data */
    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);

    MPIU_CHKLMEM_MALLOC(tmp_buf, void *,
                        count * (MPIR_MAX(extent, true_extent)), mpi_errno,
                        "temporary buffer");

    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *) ((char *) tmp_buf - true_lb);

    /* copy local data into recvbuf */
    if (sendbuf != MPI_IN_PLACE) {
        mpi_errno =
            MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count,
                           datatype);
        MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                            "**fail");
    }

    /* find nearest power-of-two less than or equal to comm_size */
    pof2 = comm_ptr->ch.gpof2;

    rem = comm_size - pof2;

    /* In the non-power-of-two case, all even-numbered
       processes of rank < 2*rem send their data to
       (rank+1). These even-numbered processes no longer
       participate in the algorithm until the very end. The
       remaining processes form a nice power-of-two. */

    if (rank < 2 * rem) {
        if (rank % 2 == 0) {
            /* even */
            mpi_errno = MPIC_Send_ft(recvbuf, count, datatype, rank + 1,
                                     MPIR_ALLREDUCE_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            /* temporarily set the rank to -1 so that this
               process does not pariticipate in recursive
               doubling */
            newrank = -1;
        } else {
            /* odd */
            mpi_errno = MPIC_Recv_ft(tmp_buf, count, datatype, rank - 1,
                                     MPIR_ALLREDUCE_TAG, comm,
                                     MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

            /* do the reduction on received data. since the
               ordering is right, it doesn't matter whether
               the operation is commutative or not. */
#ifdef HAVE_CXX_BINDING
            if (is_cxx_uop) {
                (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf, count,
                                                datatype, uop);
            } else {
#endif
                (*uop) (tmp_buf, recvbuf, &count, &datatype);

                /* change the rank */
                newrank = rank / 2;
#ifdef HAVE_CXX_BINDING
            }
#endif
        }
    } else {                /* rank >= 2*rem */
        newrank = rank - rem;
    }

    /* If op is user-defined or count is less than pof2, use
       recursive doubling algorithm. Otherwise do a reduce-scatter
       followed by allgather. (If op is user-defined,
       derived datatypes are allowed and the user could pass basic
       datatypes on one process and derived on another as long as
       the type maps are the same. Breaking up derived
       datatypes to do the reduce-scatter is tricky, therefore
       using recursive doubling in that case.) */

    if (newrank != -1) {
        if ((HANDLE_GET_KIND(op) != HANDLE_KIND_BUILTIN) || (count < pof2)) {  /* use recursive doubling */
            mask = 0x1;
            while (mask < pof2) {
                newdst = newrank ^ mask;
                /* find real rank of dest */
                dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                /* Send the most current data, which is in recvbuf. Recv
                   into tmp_buf */
                mpi_errno = MPIC_Sendrecv_ft(recvbuf, count, datatype,
                                             dst, MPIR_ALLREDUCE_TAG,
                                             tmp_buf, count, datatype, dst,
                                             MPIR_ALLREDUCE_TAG, comm,
                                             MPI_STATUS_IGNORE, errflag);

                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                /* tmp_buf contains data received in this step.
                   recvbuf contains data accumulated so far */

                if (is_commutative || (dst < rank)) {
                    /* op is commutative OR the order is already right */
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf,
                                                        count, datatype,
                                                        uop);
                    } else {
#endif
                        (*uop) (tmp_buf, recvbuf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                    }
#endif
                } else {
                    /* op is noncommutative and the order is not right */
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn) (recvbuf, tmp_buf,
                                                        count, datatype,
                                                        uop);
                    } else {
#endif
                        (*uop) (recvbuf, tmp_buf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                    }
#endif
                    /* copy result back into recvbuf */
                    mpi_errno = MPIR_Localcopy(tmp_buf, count, datatype,
                                               recvbuf, count, datatype);
                    MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno,
                                        MPI_ERR_OTHER, "**fail");
                }
                mask <<= 1;
            }
        } else {

            /* do a reduce-scatter followed by allgather */

            /* for the reduce-scatter, calculate the count that
               each process receives and the displacement within
               the buffer */

            MPIU_CHKLMEM_MALLOC(cnts, int *, pof2 * sizeof (int), mpi_errno,
                                "counts");
            MPIU_CHKLMEM_MALLOC(disps, int *, pof2 * sizeof (int),
                                mpi_errno, "displacements");

            for (i = 0; i < (pof2 - 1); i++) {
                cnts[i] = count / pof2;
            }
            cnts[pof2 - 1] = count - (count / pof2) * (pof2 - 1);

            disps[0] = 0;
            for (i = 1; i < pof2; i++) {
                disps[i] = disps[i - 1] + cnts[i - 1];
            }

            mask = 0x1;
            send_idx = recv_idx = 0;
            last_idx = pof2;
            while (mask < pof2) {
                newdst = newrank ^ mask;
                /* find real rank of dest */
                dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                send_cnt = recv_cnt = 0;
                if (newrank < newdst) {
                    send_idx = recv_idx + pof2 / (mask * 2);
                    for (i = send_idx; i < last_idx; i++)
                        send_cnt += cnts[i];
                    for (i = recv_idx; i < send_idx; i++)
                        recv_cnt += cnts[i];
                } else {
                    recv_idx = send_idx + pof2 / (mask * 2);
                    for (i = send_idx; i < recv_idx; i++)
                        send_cnt += cnts[i];
                    for (i = recv_idx; i < last_idx; i++)
                        recv_cnt += cnts[i];
                }

                /* Send data from recvbuf. Recv into tmp_buf */
                mpi_errno = MPIC_Sendrecv_ft((char *) recvbuf +
                                             disps[send_idx] * extent,
                                             send_cnt, datatype,
                                             dst, MPIR_ALLREDUCE_TAG,
                                             (char *) tmp_buf +
                                             disps[recv_idx] * extent,
                                             recv_cnt, datatype, dst,
                                             MPIR_ALLREDUCE_TAG, comm,
                                             MPI_STATUS_IGNORE, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                /* tmp_buf contains data received in this step.
                   recvbuf contains data accumulated so far */

                /* This algorithm is used only for predefined ops
                   and predefined ops are always commutative. */

                (*uop) ((char *) tmp_buf + disps[recv_idx] * extent,
                        (char *) recvbuf + disps[recv_idx] * extent,
                        &recv_cnt, &datatype);

                /* update send_idx for next iteration */
                send_idx = recv_idx;
                mask <<= 1;

                /* update last_idx, but not in last iteration
                   because the value is needed in the allgather
                   step below. */
                if (mask < pof2)
                    last_idx = recv_idx + pof2 / mask;
            }

            /* now do the allgather */

            mask >>= 1;
            while (mask > 0) {
                newdst = newrank ^ mask;
                /* find real rank of dest */
                dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                send_cnt = recv_cnt = 0;
                if (newrank < newdst) {
                    /* update last_idx except on first iteration */
                    if (mask != pof2 / 2) {
                        last_idx = last_idx + pof2 / (mask * 2);
                    }

                    recv_idx = send_idx + pof2 / (mask * 2);
                    for (i = send_idx; i < recv_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i = recv_idx; i < last_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                } else {
                    recv_idx = send_idx - pof2 / (mask * 2);
                    for (i = send_idx; i < last_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i = recv_idx; i < send_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                }

                mpi_errno = MPIC_Sendrecv_ft((char *) recvbuf +
                                             disps[send_idx] * extent,
                                             send_cnt, datatype,
                                             dst, MPIR_ALLREDUCE_TAG,
                                             (char *) recvbuf +
                                             disps[recv_idx] * extent,
                                             recv_cnt, datatype, dst,
                                             MPIR_ALLREDUCE_TAG, comm,
                                             MPI_STATUS_IGNORE, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                if (newrank > newdst) {
                    send_idx = recv_idx;
                }

                mask >>= 1;
            }
        }
    }

    /* In the non-power-of-two case, all odd-numbered
       processes of rank < 2*rem send the result to
       (rank-1), the ranks who didn't participate above. */
    if (rank < 2 * rem) {
        if (rank % 2) {     /* odd */
            mpi_errno = MPIC_Send_ft(recvbuf, count,
                                     datatype, rank - 1,
                                     MPIR_ALLREDUCE_TAG, comm, errflag);
        } else {            /* even */

            mpi_errno = MPIC_Recv(recvbuf, count,
                                  datatype, rank + 1,
                                  MPIR_ALLREDUCE_TAG, comm,
                                  MPI_STATUS_IGNORE);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
    }

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}



/* not declared static because a machine-specific function may call this one 
   in some cases */
#undef FCNAME
#define FCNAME "MPIR_Allreduce_pt2pt_old_MV2"
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_pt2pt_old_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
#ifdef MPID_HAS_HETERO
    int rc;
    int is_homogeneous = 1;
#endif
    int comm_size, rank, type_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int mask, dst, is_commutative, pof2, newrank = 0, rem, newdst, i,
        send_idx, recv_idx, last_idx, send_cnt, recv_cnt, *cnts, *disps;
    MPI_Aint true_lb, true_extent, extent;
    void *tmp_buf;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(3);

    if (count == 0) {
        return MPI_SUCCESS;
    }
    comm = comm_ptr->handle;

    MPIU_THREADPRIV_GET;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero) {
        is_homogeneous = 0;
    }

    if (!is_homogeneous) {
        /* heterogeneous. To get the same result on all processes, we
           do a reduce to 0 and then broadcast. */
        mpi_errno = MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype,
                                    op, 0, comm, errflag);
        /* 
           FIXME: mpi_errno is error CODE, not necessarily the error
           class MPI_ERR_OP.  In MPICH2, we can get the error class 
           with errorclass = mpi_errno & ERROR_CLASS_MASK;
         */
        if (mpi_errno == MPI_ERR_OP || mpi_errno == MPI_SUCCESS) {
            /* Allow MPI_ERR_OP since we can continue from this error */
            rc = MPIR_Bcast_impl(recvbuf, count, datatype, 0, comm_ptr,
                                 errflag);
            if (rc)
                mpi_errno = rc;
        }
    } else
#endif                          /* MPID_HAS_HETERO */
    {
        /* homogeneous */

        /* set op_errno to 0. stored in perthread structure */
        MPIU_THREADPRIV_FIELD(op_errno) = 0;

        comm_size = comm_ptr->local_size;
        rank = comm_ptr->rank;

        if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
            is_commutative = 1;
            /* get the function by indexing into the op table */
            uop = MPIR_Op_table[op % 16 - 1];
        } else {
            MPID_Op_get_ptr(op, op_ptr);
            if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
                is_commutative = 0;
            } else {
                is_commutative = 1;
            }
#ifdef HAVE_CXX_BINDING
            if (op_ptr->language == MPID_LANG_CXX) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
                is_cxx_uop = 1;
            } else {
#endif
                if ((op_ptr->language == MPID_LANG_C)) {
                    uop = (MPI_User_function *) op_ptr->function.c_function;
                } else {
                    uop = (MPI_User_function *) op_ptr->function.f77_function;
                }
#ifdef HAVE_CXX_BINDING
            }
#endif
        }

        /* need to allocate temporary buffer to store incoming data */
        MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
        MPID_Datatype_get_extent_macro(datatype, extent);

        MPIU_CHKLMEM_MALLOC(tmp_buf, void *,
                            count * (MPIR_MAX(extent, true_extent)), mpi_errno,
                            "temporary buffer");

        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *) ((char *) tmp_buf - true_lb);

        /* copy local data into recvbuf */
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno =
                MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count,
                               datatype);
            MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                                "**fail");
        }

        MPID_Datatype_get_size_macro(datatype, type_size);

        /* find nearest power-of-two less than or equal to comm_size */
        pof2 = comm_ptr->ch.gpof2;

        rem = comm_size - pof2;

        /* In the non-power-of-two case, all even-numbered
           processes of rank < 2*rem send their data to
           (rank+1). These even-numbered processes no longer
           participate in the algorithm until the very end. The
           remaining processes form a nice power-of-two. */

        if (rank < 2 * rem) {
            if (rank % 2 == 0) {
                /* even */
                mpi_errno = MPIC_Send_ft(recvbuf, count, datatype, rank + 1,
                                         MPIR_ALLREDUCE_TAG, comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                /* temporarily set the rank to -1 so that this
                   process does not pariticipate in recursive
                   doubling */
                newrank = -1;
            } else {
                /* odd */
                mpi_errno = MPIC_Recv_ft(tmp_buf, count, datatype, rank - 1,
                                         MPIR_ALLREDUCE_TAG, comm,
                                         MPI_STATUS_IGNORE, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                /* do the reduction on received data. since the
                   ordering is right, it doesn't matter whether
                   the operation is commutative or not. */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf, count,
                                                    datatype, uop);
                } else {
#endif
                    (*uop) (tmp_buf, recvbuf, &count, &datatype);

                    /* change the rank */
                    newrank = rank / 2;
#ifdef HAVE_CXX_BINDING
                }
#endif
            }
        } else {                /* rank >= 2*rem */
            newrank = rank - rem;
        }

        /* If op is user-defined or count is less than pof2, use
           recursive doubling algorithm. Otherwise do a reduce-scatter
           followed by allgather. (If op is user-defined,
           derived datatypes are allowed and the user could pass basic
           datatypes on one process and derived on another as long as
           the type maps are the same. Breaking up derived
           datatypes to do the reduce-scatter is tricky, therefore
           using recursive doubling in that case.) */

        if (newrank != -1) {
            if ((count * type_size <= mv2_coll_param.allreduce_short_msg) || (HANDLE_GET_KIND(op) != HANDLE_KIND_BUILTIN) || (count < pof2)) {  /* use recursive doubling */
                mask = 0x1;
                while (mask < pof2) {
                    newdst = newrank ^ mask;
                    /* find real rank of dest */
                    dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                    /* Send the most current data, which is in recvbuf. Recv
                       into tmp_buf */
                    mpi_errno = MPIC_Sendrecv_ft(recvbuf, count, datatype,
                                                 dst, MPIR_ALLREDUCE_TAG,
                                                 tmp_buf, count, datatype, dst,
                                                 MPIR_ALLREDUCE_TAG, comm,
                                                 MPI_STATUS_IGNORE, errflag);

                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }

                    /* tmp_buf contains data received in this step.
                       recvbuf contains data accumulated so far */

                    if (is_commutative || (dst < rank)) {
                        /* op is commutative OR the order is already right */
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn) (tmp_buf, recvbuf,
                                                            count, datatype,
                                                            uop);
                        } else {
#endif
                            (*uop) (tmp_buf, recvbuf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                        }
#endif
                    } else {
                        /* op is noncommutative and the order is not right */
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn) (recvbuf, tmp_buf,
                                                            count, datatype,
                                                            uop);
                        } else {
#endif
                            (*uop) (recvbuf, tmp_buf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                        }
#endif
                        /* copy result back into recvbuf */
                        mpi_errno = MPIR_Localcopy(tmp_buf, count, datatype,
                                                   recvbuf, count, datatype);
                        MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno,
                                            MPI_ERR_OTHER, "**fail");
                    }
                    mask <<= 1;
                }
            } else {

                /* do a reduce-scatter followed by allgather */

                /* for the reduce-scatter, calculate the count that
                   each process receives and the displacement within
                   the buffer */

                MPIU_CHKLMEM_MALLOC(cnts, int *, pof2 * sizeof (int), mpi_errno,
                                    "counts");
                MPIU_CHKLMEM_MALLOC(disps, int *, pof2 * sizeof (int),
                                    mpi_errno, "displacements");

                for (i = 0; i < (pof2 - 1); i++) {
                    cnts[i] = count / pof2;
                }
                cnts[pof2 - 1] = count - (count / pof2) * (pof2 - 1);

                disps[0] = 0;
                for (i = 1; i < pof2; i++) {
                    disps[i] = disps[i - 1] + cnts[i - 1];
                }

                mask = 0x1;
                send_idx = recv_idx = 0;
                last_idx = pof2;
                while (mask < pof2) {
                    newdst = newrank ^ mask;
                    /* find real rank of dest */
                    dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                    send_cnt = recv_cnt = 0;
                    if (newrank < newdst) {
                        send_idx = recv_idx + pof2 / (mask * 2);
                        for (i = send_idx; i < last_idx; i++)
                            send_cnt += cnts[i];
                        for (i = recv_idx; i < send_idx; i++)
                            recv_cnt += cnts[i];
                    } else {
                        recv_idx = send_idx + pof2 / (mask * 2);
                        for (i = send_idx; i < recv_idx; i++)
                            send_cnt += cnts[i];
                        for (i = recv_idx; i < last_idx; i++)
                            recv_cnt += cnts[i];
                    }

                    /* Send data from recvbuf. Recv into tmp_buf */
                    mpi_errno = MPIC_Sendrecv_ft((char *) recvbuf +
                                                 disps[send_idx] * extent,
                                                 send_cnt, datatype,
                                                 dst, MPIR_ALLREDUCE_TAG,
                                                 (char *) tmp_buf +
                                                 disps[recv_idx] * extent,
                                                 recv_cnt, datatype, dst,
                                                 MPIR_ALLREDUCE_TAG, comm,
                                                 MPI_STATUS_IGNORE, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }

                    /* tmp_buf contains data received in this step.
                       recvbuf contains data accumulated so far */

                    /* This algorithm is used only for predefined ops
                       and predefined ops are always commutative. */

                    (*uop) ((char *) tmp_buf + disps[recv_idx] * extent,
                            (char *) recvbuf + disps[recv_idx] * extent,
                            &recv_cnt, &datatype);

                    /* update send_idx for next iteration */
                    send_idx = recv_idx;
                    mask <<= 1;

                    /* update last_idx, but not in last iteration
                       because the value is needed in the allgather
                       step below. */
                    if (mask < pof2)
                        last_idx = recv_idx + pof2 / mask;
                }

                /* now do the allgather */

                mask >>= 1;
                while (mask > 0) {
                    newdst = newrank ^ mask;
                    /* find real rank of dest */
                    dst = (newdst < rem) ? newdst * 2 + 1 : newdst + rem;

                    send_cnt = recv_cnt = 0;
                    if (newrank < newdst) {
                        /* update last_idx except on first iteration */
                        if (mask != pof2 / 2) {
                            last_idx = last_idx + pof2 / (mask * 2);
                        }

                        recv_idx = send_idx + pof2 / (mask * 2);
                        for (i = send_idx; i < recv_idx; i++) {
                            send_cnt += cnts[i];
                        }
                        for (i = recv_idx; i < last_idx; i++) {
                            recv_cnt += cnts[i];
                        }
                    } else {
                        recv_idx = send_idx - pof2 / (mask * 2);
                        for (i = send_idx; i < last_idx; i++) {
                            send_cnt += cnts[i];
                        }
                        for (i = recv_idx; i < send_idx; i++) {
                            recv_cnt += cnts[i];
                        }
                    }

                    mpi_errno = MPIC_Sendrecv_ft((char *) recvbuf +
                                                 disps[send_idx] * extent,
                                                 send_cnt, datatype,
                                                 dst, MPIR_ALLREDUCE_TAG,
                                                 (char *) recvbuf +
                                                 disps[recv_idx] * extent,
                                                 recv_cnt, datatype, dst,
                                                 MPIR_ALLREDUCE_TAG, comm,
                                                 MPI_STATUS_IGNORE, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }

                    if (newrank > newdst) {
                        send_idx = recv_idx;
                    }

                    mask >>= 1;
                }
            }
        }

        /* In the non-power-of-two case, all odd-numbered
           processes of rank < 2*rem send the result to
           (rank-1), the ranks who didn't participate above. */
        if (rank < 2 * rem) {
            if (rank % 2) {     /* odd */
                mpi_errno = MPIC_Send_ft(recvbuf, count,
                                         datatype, rank - 1,
                                         MPIR_ALLREDUCE_TAG, comm, errflag);
            } else {            /* even */

                mpi_errno = MPIC_Recv(recvbuf, count,
                                      datatype, rank + 1,
                                      MPIR_ALLREDUCE_TAG, comm,
                                      MPI_STATUS_IGNORE);
            }
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
    }

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}

/* intra-node shm reduce as the first reduce in allreduce */
int MPIR_Allreduce_reduce_shmem_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    int i = 0, is_commutative = 0;
    MPI_Aint true_lb, true_extent, extent;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    char *shmem_buf = NULL;
    MPI_Comm shmem_comm = MPI_COMM_NULL;
    MPID_Comm *shmem_commptr = NULL;
    int local_rank = -1, local_size = 0;
    void *local_buf = NULL;
    int stride = 0;
    is_commutative = 0;
    int shmem_comm_rank;

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
    stride = count * MPIR_MAX(extent, true_extent);

    /* Get the operator and check whether it is commutative or not */
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op % 16 - 1];
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }

#if defined(HAVE_CXX_BINDING)
        if (op_ptr->language == MPID_LANG_CXX) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
            is_cxx_uop = 1;
        } else
#endif                          /* defined(HAVE_CXX_BINDING) */
        if ((op_ptr->language == MPID_LANG_C)) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
        } else {
            uop = (MPI_User_function *) op_ptr->function.f77_function;
        }
    }

    shmem_comm = comm_ptr->ch.shmem_comm;
    PMPI_Comm_size(shmem_comm, &local_size);
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;
    shmem_comm_rank = shmem_commptr->ch.shmem_comm_rank;

#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    /* Doing the shared memory gather and reduction by the leader */
    if (local_rank == 0) {
        /* Message size is smaller than the shmem_reduce threshold. 
         * The intra-node communication is done through shmem */
        if (local_size > 1) {
            /* Node leader waits till all the non-leaders have written 
             * the data into the shmem buffer */
            MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                              shmem_comm_rank,
                                              (void *) &shmem_buf);
            if (is_commutative) {
                for (i = 1; i < local_size; i++) {
                    local_buf = (char *) shmem_buf + stride * i;
#if defined(HAVE_CXX_BINDING)
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn) (local_buf, recvbuf,
                                                        count, datatype,
                                                        uop);
                    } else {
#endif                          /* defined(HAVE_CXX_BINDING) */
                        (*uop) (local_buf, recvbuf, &count, &datatype);
#if defined(HAVE_CXX_BINDING)
                    }
#endif                          /* defined(HAVE_CXX_BINDING) */

                }
                MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size,
                                                        local_rank,
                                                        shmem_comm_rank);
            }
        }
    } else {
        MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                          shmem_comm_rank,
                                          (void *) &shmem_buf);
        local_buf = (char *) shmem_buf + stride * local_rank;
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, local_buf,
                                       count, datatype);
        } else {
            mpi_errno = MPIR_Localcopy(recvbuf, count, datatype, local_buf,
                                       count, datatype);
        }
        MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                            "**fail");
        MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size, local_rank,
                                                    shmem_comm_rank);
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif

  fn_exit:
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}

/* intra-node p2p reduce as the first reduce in allreduce */
int MPIR_Allreduce_reduce_p2p_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Aint true_lb, true_extent;
    MPI_Comm shmem_comm = MPI_COMM_NULL;
    MPID_Comm *shmem_commptr = NULL;
    int local_rank = -1, local_size = 0;

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

    shmem_comm = comm_ptr->ch.shmem_comm;
    PMPI_Comm_size(shmem_comm, &local_size);
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;

#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    /* Doing the shared memory gather and reduction by the leader */
    if (local_rank == 0) {
        /* Message size is larger than the shmem_reduce threshold. 
         * The leader will spend too much time doing the math operation
         * for messages that are larger. So, we use a point-to-point
         * based reduce to balance the load across all the processes within
         * the same node*/
        mpi_errno =
            MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype, op, 0,
                            shmem_commptr, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }

    } else {
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno =
                MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype, op, 0,
                                shmem_commptr, errflag);
        } else {
            /* MPI_Allreduce was called with MPI_IN_PLACE as the sendbuf.
             * Since we are doing Reduce now, we need to be careful. In
             * MPI_Reduce, only the root can use MPI_IN_PLACE as sendbuf.
             * Also, the recvbuf is not relevant at all non-root processes*/
            mpi_errno = MPIR_Reduce_MV2(recvbuf, NULL, count, datatype, op,
                                        0, shmem_commptr, errflag);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif

    return (mpi_errno);
}

/* general two level allreduce helper function */
int MPIR_Allreduce_two_level_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int total_size = 0;
    MPI_Aint true_lb, true_extent;
    MPI_Comm shmem_comm = MPI_COMM_NULL, leader_comm = MPI_COMM_NULL;
    MPID_Comm *shmem_commptr = NULL, *leader_commptr = NULL;
    int local_rank = -1, local_size = 0;

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

    total_size = comm_ptr->local_size;
    shmem_comm = comm_ptr->ch.shmem_comm;
    PMPI_Comm_size(shmem_comm, &local_size);
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;

    leader_comm = comm_ptr->ch.leader_comm;
    MPID_Comm_get_ptr(leader_comm, leader_commptr);

    if (local_rank == 0) {
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf,
                                       count, datatype);
            MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                                "**fail");
        }
    }
#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    /* Doing the shared memory gather and reduction by the leader */
    if (local_rank == 0) {
        mpi_errno =
        MV2_Allreduce_intra_function(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, errflag);

        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }

        if (local_size != total_size) {
            /* inter-node allreduce */
            if(MV2_Allreduce_function == &MPIR_Allreduce_pt2pt_rd_MV2){
                mpi_errno =
                    MPIR_Allreduce_pt2pt_rd_MV2(MPI_IN_PLACE, recvbuf, count, datatype, op,
                                      leader_commptr, errflag);
            } else {
                mpi_errno =
                    MPIR_Allreduce_pt2pt_rs_MV2(MPI_IN_PLACE, recvbuf, count, datatype, op,
                                      leader_commptr, errflag);
            }
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    } else {
        /* insert the first reduce here */
        mpi_errno =
        MV2_Allreduce_intra_function(sendbuf, recvbuf, count, datatype,
                                    op, comm_ptr, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
    }

    /* Broadcasting the mesage from leader to the rest */
    /* Note: shared memory broadcast could improve the performance */
    if (local_size > 1) {
        MPIR_Shmem_Bcast_MV2(recvbuf, count, datatype, 0, shmem_commptr, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif

  fn_exit:
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}


int MPIR_Allreduce_shmem_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int i = 0, is_commutative = 0;
    MPI_Aint true_lb, true_extent, extent;
    MPI_User_function *uop;
    MPID_Op *op_ptr;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    char *shmem_buf = NULL;
    MPI_Comm shmem_comm = MPI_COMM_NULL, leader_comm = MPI_COMM_NULL;
    MPID_Comm *shmem_commptr = NULL, *leader_commptr = NULL;
    int local_rank = -1, local_size = 0;
    void *local_buf = NULL;
    int stride = 0;
    is_commutative = 0;
    int total_size, shmem_comm_rank;

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
    stride = count * MPIR_MAX(extent, true_extent);

    /* Get the operator and check whether it is commutative or not */
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op % 16 - 1];
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }

#if defined(HAVE_CXX_BINDING)
        if (op_ptr->language == MPID_LANG_CXX) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
            is_cxx_uop = 1;
        } else
#endif                          /* defined(HAVE_CXX_BINDING) */
        if ((op_ptr->language == MPID_LANG_C)) {
            uop = (MPI_User_function *) op_ptr->function.c_function;
        } else {
            uop = (MPI_User_function *) op_ptr->function.f77_function;
        }
    }

    total_size = comm_ptr->local_size;
    shmem_comm = comm_ptr->ch.shmem_comm;
    PMPI_Comm_size(shmem_comm, &local_size);
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;
    shmem_comm_rank = shmem_commptr->ch.shmem_comm_rank;

    leader_comm = comm_ptr->ch.leader_comm;
    MPID_Comm_get_ptr(leader_comm, leader_commptr);

    if (local_rank == 0) {
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf,
                                       count, datatype);
            MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                                "**fail");
        }
    }
#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    /* Doing the shared memory gather and reduction by the leader */
    if (local_rank == 0) {
        if (stride <= mv2_coll_param.shmem_allreduce_msg) {
            /* Message size is smaller than the shmem_reduce threshold. 
             * The intra-node communication is done through shmem */
            if (local_size > 1) {
                /* Node leader waits till all the non-leaders have written 
                 * the data into the shmem buffer */
                MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                                  shmem_comm_rank,
                                                  (void *) &shmem_buf);
                if (is_commutative) {
                    for (i = 1; i < local_size; i++) {
                        local_buf = (char *) shmem_buf + stride * i;
#if defined(HAVE_CXX_BINDING)
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn) (local_buf, recvbuf,
                                                            count, datatype,
                                                            uop);
                        } else {
#endif                          /* defined(HAVE_CXX_BINDING) */
                            (*uop) (local_buf, recvbuf, &count, &datatype);
#if defined(HAVE_CXX_BINDING)
                        }
#endif                          /* defined(HAVE_CXX_BINDING) */

                    }
                    MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size,
                                                            local_rank,
                                                            shmem_comm_rank);
                }
            }
        } else {
            /* Message size is larger than the shmem_reduce threshold. 
             * The leader will spend too much time doing the math operation
             * for messages that are larger. So, we use a point-to-point
             * based reduce to balance the load across all the processes within
             * the same node*/
            mpi_errno =
                MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype, op, 0,
                                shmem_commptr, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }

        }
        if (local_size != total_size) {
            mpi_errno =
                MPIR_Allreduce_MV2(MPI_IN_PLACE, recvbuf, count, datatype, op,
                                   leader_commptr, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    } else {
        if (stride <= mv2_coll_param.shmem_allreduce_msg) {
            MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                              shmem_comm_rank,
                                              (void *) &shmem_buf);
            local_buf = (char *) shmem_buf + stride * local_rank;
            if (sendbuf != MPI_IN_PLACE) {
                mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, local_buf,
                                           count, datatype);
            } else {
                mpi_errno = MPIR_Localcopy(recvbuf, count, datatype, local_buf,
                                           count, datatype);
            }
            MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER,
                                "**fail");
            MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size, local_rank,
                                                    shmem_comm_rank);
        } else {
            if (sendbuf != MPI_IN_PLACE) {
                mpi_errno =
                    MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype, op, 0,
                                    shmem_commptr, errflag);
            } else {
                /* MPI_Allreduce was called with MPI_IN_PLACE as the sendbuf.
                 * Since we are doing Reduce now, we need to be careful. In
                 * MPI_Reduce, only the root can use MPI_IN_PLACE as sendbuf.
                 * Also, the recvbuf is not relevant at all non-root processes*/
                mpi_errno = MPIR_Reduce_MV2(recvbuf, NULL, count, datatype, op,
                                            0, shmem_commptr, errflag);
            }
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    }

    /* Broadcasting the mesage from leader to the rest */
    /* Note: shared memory broadcast could improve the performance */
    if (local_size > 1) {
        MPIR_Bcast_MV2(recvbuf, count, datatype, 0, shmem_commptr, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }
#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif

  fn_exit:
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}


#if defined(_MCST_SUPPORT_)
#undef FCNAME
#define FCNAME "MPIR_Allreduce_mcst_MV2"
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_mcst_MV2(const void *sendbuf,
                             void *recvbuf,
                             int count,
                             MPI_Datatype datatype,
                             MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    MPI_Aint true_lb, true_extent;
   /*We use reduce (at rank =0) followed by mcst-bcast to implement the 
    * allreduce operation */
    int root=0, nbytes=0, position=0;
    int type_size=0; 
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret=MPI_SUCCESS;
    int rank = comm_ptr->rank, is_contig=0, is_commutative=0;
    MPIU_CHKLMEM_DECL(1);
    MPID_Datatype *dtp=NULL;
    void *tmp_buf=NULL; 
    MPID_Op *op_ptr=NULL;
    MPID_Datatype_get_size_macro(datatype, type_size);
    nbytes = type_size * count;

 
    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN) { 
        is_contig = 1;
    } else {
        MPID_Datatype_get_ptr(datatype, dtp);
        is_contig = dtp->is_contig;
    }

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

   if(is_commutative == 0) { 
       reduce_fn = &MPIR_Reduce_binomial_MV2; 
   } else { 
       if(MV2_Allreduce_function == &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2) {
            reduce_fn = &MPIR_Reduce_MV2;
       } else {
            reduce_fn = &MPIR_Reduce_redscat_gather_MV2;
       } 
   } 

    /* First do a reduction at rank = 0 */
    if(rank == root) {
        mpi_errno = reduce_fn(sendbuf, recvbuf, count, datatype,
                                op, root, comm_ptr, errflag);
    } else {
        if(sendbuf != MPI_IN_PLACE) {
            mpi_errno = reduce_fn(sendbuf, recvbuf, count, datatype,
                                op, root, comm_ptr, errflag);
        } else {
            mpi_errno = reduce_fn(recvbuf, NULL, count, datatype,
                                op, root, comm_ptr, errflag);
        }
    }
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    /* Now do a mcst-bcast operation with rank0 as the root */
    if(!is_contig) {
        /* Mcast cannot handle non-regular datatypes. We need to pack
         * as bytes before sending it*/ 
        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, nbytes, mpi_errno, "tmp_buf");

        position = 0;
        if (rank == root) {
            mpi_errno = MPIR_Pack_impl(recvbuf, count, datatype, tmp_buf, nbytes,
                                       &position);
            if (mpi_errno)
                MPIU_ERR_POP(mpi_errno);
        }
        mpi_errno = MPIR_Mcast_inter_node_MV2(tmp_buf, nbytes, MPI_BYTE,
                                     root, comm_ptr, errflag);
    } else { 
        mpi_errno = MPIR_Mcast_inter_node_MV2(recvbuf, count, datatype,
                                     root, comm_ptr, errflag);
    } 
   
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }
    
    if (!is_contig) {
        /* We are done, lets pack the data back the way the user 
         * needs it */ 
        if (rank != root) {
            position = 0;
            mpi_errno = MPIR_Unpack_impl(tmp_buf, nbytes, &position, recvbuf,
                                         count, datatype);
            if (mpi_errno)
                MPIU_ERR_POP(mpi_errno);
        }
    }

    /* check to see if the intra-node mcast is not done. 
     * if this is the case, do it either through shmem or knomial */ 
    if(comm_ptr->ch.intra_node_done == 0) { 
        MPID_Comm *shmem_commptr=NULL; 
        MPID_Comm_get_ptr(comm_ptr->ch.shmem_comm, shmem_commptr); 
        int local_size = shmem_commptr->local_size; 
        if (local_size > 1) {
            MPIR_Bcast_MV2(recvbuf, count, datatype, 0, shmem_commptr, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    } 


  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}
#endif /*  #if defined(_MCST_SUPPORT_) */ 
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

#undef FUNCNAME
#define FUNCNAME MPIR_Allreduce_new_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_new_MV2(const void *sendbuf,
                       void *recvbuf,
                       int count,
                       MPI_Datatype datatype,
                       MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
#ifdef MPID_HAS_HETERO
    int rc;
    int is_homogeneous = 1;
#endif

    int mpi_errno = MPI_SUCCESS;
    int rank = 0, comm_size = 0;
   
    mpi_errno = PMPI_Comm_size(comm_ptr->handle, &comm_size);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_rank(comm_ptr->handle, &rank);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int sendtype_size = 0, nbytes = 0;
    int range = 0, range_threshold = 0, range_threshold_intra = 0;
    int is_two_level = 0;
    int is_commutative = 0;
    MPI_Aint true_lb, true_extent;

    MPID_Datatype_get_size_macro(datatype, sendtype_size);
    nbytes = count * sendtype_size;

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Op *op_ptr;

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }
    }

#ifdef _ENABLE_CUDA_
    MPI_Aint extent;
    MPID_Datatype_get_extent_macro(datatype, extent);
    int stride = 0;
    stride = count * MPIR_MAX(extent, true_extent);
    cudaError_t  cuerr = cudaSuccess;
    int recv_mem_type = 0;
    int send_mem_type = 0;
    char *recv_host_buf = NULL;
    char *send_host_buf = NULL;
    char *temp_recvbuf = recvbuf;

    if (rdma_enable_cuda) {
       recv_mem_type = is_device_buffer(recvbuf);
       if ( sendbuf != MPI_IN_PLACE ){
           send_mem_type = is_device_buffer(sendbuf);
       }
    }

    if(rdma_enable_cuda && send_mem_type){
        send_host_buf = (char*) MPIU_Malloc(stride);
        cuerr = cudaMemcpy((void *)send_host_buf, 
                            (void *)sendbuf, 
                            stride, 
                            cudaMemcpyDeviceToHost);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
        sendbuf = send_host_buf;
    }

    if(rdma_enable_cuda && recv_mem_type){
        recv_host_buf = (char*) MPIU_Malloc(stride);
        cuerr = cudaMemcpy((void *)recv_host_buf, 
                            (void *)recvbuf, 
                            stride, 
                            cudaMemcpyDeviceToHost);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
        recvbuf = recv_host_buf;
    }
#endif

#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero) {
        is_homogeneous = 0;
    }

    if (!is_homogeneous) {
        /* heterogeneous. To get the same result on all processes, we
           do a reduce to 0 and then broadcast. */
        mpi_errno = MPIR_Reduce_MV2(sendbuf, recvbuf, count, datatype,
                                    op, 0, comm, errflag);
        /* 
           FIXME: mpi_errno is error CODE, not necessarily the error
           class MPI_ERR_OP.  In MPICH2, we can get the error class 
           with errorclass = mpi_errno & ERROR_CLASS_MASK;
         */
        if (mpi_errno == MPI_ERR_OP || mpi_errno == MPI_SUCCESS) {
            /* Allow MPI_ERR_OP since we can continue from this error */
            rc = MPIR_Bcast_impl(recvbuf, count, datatype, 0, comm_ptr,
                                 errflag);
            if (rc)
                mpi_errno = rc;
        }
    } else
#endif /* MPID_HAS_HETERO */
    {
        /* Search for the corresponding system size inside the tuning table */
        while ((range < (mv2_size_allreduce_tuning_table - 1)) &&
               (comm_size > mv2_allreduce_thresholds_table[range].numproc)) {
            range++;
        }
        /* Search for corresponding inter-leader function */
        /* skip mcast poiters if mcast is not available */
        if(mv2_allreduce_thresholds_table[range].mcast_enabled != 1){
            while ((range_threshold < (mv2_allreduce_thresholds_table[range].size_inter_table - 1)) 
                    && ((mv2_allreduce_thresholds_table[range].
                    inter_leader[range_threshold].MV2_pt_Allreduce_function 
                    == &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2) ||
                    (mv2_allreduce_thresholds_table[range].
                    inter_leader[range_threshold].MV2_pt_Allreduce_function
                    == &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2)
                    )) {
                    range_threshold++;
            }
        }
        while ((range_threshold < (mv2_allreduce_thresholds_table[range].size_inter_table - 1))
               && (nbytes >
               mv2_allreduce_thresholds_table[range].inter_leader[range_threshold].max)
               && (mv2_allreduce_thresholds_table[range].inter_leader[range_threshold].max != -1)) {
               range_threshold++;
        }
        if(mv2_allreduce_thresholds_table[range].is_two_level_allreduce[range_threshold] == 1){
               is_two_level = 1;    
        }
        /* Search for corresponding intra-node function */
        while ((range_threshold_intra <
               (mv2_allreduce_thresholds_table[range].size_intra_table - 1))
                && (nbytes >
                mv2_allreduce_thresholds_table[range].intra_node[range_threshold_intra].max)
                && (mv2_allreduce_thresholds_table[range].intra_node[range_threshold_intra].max !=
                -1)) {
                range_threshold_intra++;
        }

        MV2_Allreduce_function = mv2_allreduce_thresholds_table[range].inter_leader[range_threshold]
                                .MV2_pt_Allreduce_function;

        MV2_Allreduce_intra_function = mv2_allreduce_thresholds_table[range].intra_node[range_threshold_intra]
                                .MV2_pt_Allreduce_function;

        /* check if mcast is ready, otherwise replace mcast with other algorithm */
        if((MV2_Allreduce_function == &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2)||
          (MV2_Allreduce_function == &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2)){
#if defined(_MCST_SUPPORT_)
            if(comm_ptr->ch.is_mcast_ok == 1
                && comm_ptr->ch.shmem_coll_ok == 1
                && mv2_use_mcast_allreduce == 1){
            } else
#endif  /* #if defined(_MCST_SUPPORT_) */
            {
                MV2_Allreduce_function = &MPIR_Allreduce_pt2pt_rd_MV2;
            }
            if(is_two_level != 1) {
                MV2_Allreduce_function = &MPIR_Allreduce_pt2pt_rd_MV2;
            }
        } 

        if(is_two_level == 1){
#if defined(_MCST_SUPPORT_)
            if((MV2_Allreduce_function == &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2)||
            (MV2_Allreduce_function == &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2)){ 

                mpi_errno = MPIR_Allreduce_mcst_MV2(sendbuf, recvbuf, count,
                                               datatype, op, comm_ptr, errflag);
            } else
#endif  /* #if defined(_MCST_SUPPORT_) */
            { 
                /* check if shm is ready, if not use other algorithm first */
                if ((comm_ptr->ch.shmem_coll_ok == 1)
                    && (mv2_disable_shmem_allreduce == 0)
                    && (is_commutative)
                    && (mv2_enable_shmem_collectives)) {
                    mpi_errno = MPIR_Allreduce_two_level_MV2(sendbuf, recvbuf, count,
                                                     datatype, op, comm_ptr, errflag);
                } else {
                    mpi_errno = MPIR_Allreduce_pt2pt_rd_MV2(sendbuf, recvbuf, count,
                                                     datatype, op, comm_ptr, errflag);
                }
            }
        } else { 
            mpi_errno = MV2_Allreduce_function(sendbuf, recvbuf, count,
                                           datatype, op, comm_ptr, errflag);
        }
    } 

#else                           /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    mpi_errno = MPIR_Allreduce_intra(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, errflag);

#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

#ifdef _ENABLE_CUDA_
    cuerr = cudaSuccess;
    if(rdma_enable_cuda && recv_mem_type){
        recvbuf = temp_recvbuf;
        cuerr = cudaMemcpy((void *)recvbuf, 
                            (void *)recv_host_buf, 
                            stride, 
                            cudaMemcpyHostToDevice);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
    }
    if(rdma_enable_cuda && recv_mem_type){
        if(recv_host_buf){
            free(recv_host_buf);
            recv_host_buf = NULL;
        }
    }
    if(rdma_enable_cuda && send_mem_type){
        if(send_host_buf){
            free(send_host_buf);
            send_host_buf = NULL;
        }
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
	comm_ptr->ch.intra_node_done=0;
#endif
	
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
    }

  fn_exit:
    return (mpi_errno);

  fn_fail:
    goto fn_exit;

}


#undef FUNCNAME
#define FUNCNAME MPIR_Allreduce_old_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_old_MV2(const void *sendbuf,
                       void *recvbuf,
                       int count,
                       MPI_Datatype datatype,
                       MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

    if (count == 0) {
        return MPI_SUCCESS;
    }

    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int stride = 0, is_commutative = 0;
    MPI_Aint true_lb, true_extent, extent;
    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
    stride = count * MPIR_MAX(extent, true_extent);
    MPID_Op *op_ptr;

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
    } else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else {
            is_commutative = 1;
        }
    }

#ifdef _ENABLE_CUDA_
    cudaError_t  cuerr = cudaSuccess;
    int recv_mem_type = 0;
    int send_mem_type = 0;
    char *recv_host_buf = NULL;
    char *send_host_buf = NULL;
    char *temp_recvbuf = recvbuf;

    if (rdma_enable_cuda) {
       recv_mem_type = is_device_buffer(recvbuf);
       if ( sendbuf != MPI_IN_PLACE ){
           send_mem_type = is_device_buffer(sendbuf);
       }
    }

    if(rdma_enable_cuda && send_mem_type){
        send_host_buf = (char*) MPIU_Malloc(stride);
        cuerr = cudaMemcpy((void *)send_host_buf, 
                            (void *)sendbuf, 
                            stride, 
                            cudaMemcpyDeviceToHost);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
        sendbuf = send_host_buf;
    }

    if(rdma_enable_cuda && recv_mem_type){
        recv_host_buf = (char*) MPIU_Malloc(stride);
        cuerr = cudaMemcpy((void *)recv_host_buf, 
                            (void *)recvbuf, 
                            stride, 
                            cudaMemcpyDeviceToHost);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
        recvbuf = recv_host_buf;
    }
#endif

#if defined(_MCST_SUPPORT_)
    if(comm_ptr->ch.is_mcast_ok == 1
       && comm_ptr->ch.shmem_coll_ok == 1
       && mv2_use_mcast_allreduce == 1
       && stride >= mv2_mcast_allreduce_small_msg_size 
       && stride <= mv2_mcast_allreduce_large_msg_size){
        mpi_errno = MPIR_Allreduce_mcst_MV2(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, errflag);
    } else
#endif /* #if defined(_MCST_SUPPORT_) */ 
    {
        if ((comm_ptr->ch.shmem_coll_ok == 1)
            && (stride < mv2_coll_param.allreduce_2level_threshold)
            && (mv2_disable_shmem_allreduce == 0)
            && (is_commutative)
            && (mv2_enable_shmem_collectives)) {
            mpi_errno = MPIR_Allreduce_shmem_MV2(sendbuf, recvbuf, count, datatype,
                                                 op, comm_ptr, errflag);

        } else {
       
            mpi_errno = MPIR_Allreduce_pt2pt_old_MV2(sendbuf, recvbuf, count, 
                                            datatype, op, comm_ptr, errflag);

        }
    } 
#else                           /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    mpi_errno = MPIR_Allreduce_intra(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, errflag);

#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

#ifdef _ENABLE_CUDA_
    cuerr = cudaSuccess;
    if(rdma_enable_cuda && recv_mem_type){
        recvbuf = temp_recvbuf;
        cuerr = cudaMemcpy((void *)recvbuf, 
                            (void *)recv_host_buf, 
                            stride, 
                            cudaMemcpyHostToDevice);
        if(cudaSuccess != cuerr){
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**cudamemcpy");
        }
    }
    if(rdma_enable_cuda && recv_mem_type){
        if(recv_host_buf){
            free(recv_host_buf);
            recv_host_buf = NULL;
        }
    }
    if(rdma_enable_cuda && send_mem_type){
        if(send_host_buf){
            free(send_host_buf);
            send_host_buf = NULL;
        }
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
	comm_ptr->ch.intra_node_done=0;
#endif
	
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
    }

  fn_exit:
    return (mpi_errno);

  fn_fail:
    goto fn_exit;

}

#undef FUNCNAME
#define FUNCNAME MPIR_Allreduce_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allreduce_MV2(const void *sendbuf,
                       void *recvbuf,
                       int count,
                       MPI_Datatype datatype,
                       MPI_Op op, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    if (count == 0) {
        return MPI_SUCCESS;
    }
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if(mv2_use_old_allreduce == 1){
        mpi_errno = MPIR_Allreduce_old_MV2(sendbuf, recvbuf, count,
                                        datatype, op, comm_ptr, errflag);
    } else { 
         mpi_errno = MPIR_Allreduce_new_MV2(sendbuf, recvbuf, count,
                                        datatype, op, comm_ptr, errflag);
    }
#else
    mpi_errno = MPIR_Allreduce_intra(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, errflag);
#endif
    return (mpi_errno);
}
