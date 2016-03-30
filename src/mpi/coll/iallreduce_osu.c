/* Copyright (c) 2001-2016, The Ohio State University. All rights
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
#include "collutil.h"

#include "iallreduce_tuning.h"

#if defined(CHANNEL_MRAIL) || defined(CHANNEL_PSM)

int (*MV2_Iallreduce_function) (const void *sendbuf, void *recvbuf, int count,
				    MPI_Datatype datatype, MPI_Op op,
				    MPID_Comm *comm_ptr, MPID_Sched_t s) = NULL;

int (*MV2_Iallreduce_intra_node_function) (const void *sendbuf, void *recvbuf, int count,
				    MPI_Datatype datatype, MPI_Op op,
				    MPID_Comm *comm_ptr, MPID_Sched_t s) = NULL;

#undef FUNCNAME
#define FUNCNAME MPIR_Iallreduce_tune_helper_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Iallreduce_tune_helper_MV2(const void *sendbuf, void *recvbuf, int count,
				    MPI_Datatype datatype, MPI_Op op,
				    MPID_Comm *comm_ptr, MPID_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    int is_homogeneous, pof2, comm_size;

    MPIU_Assert(comm_ptr->comm_kind == MPID_INTRACOMM);

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif
    MPIU_Assert(is_homogeneous);

    comm_size = comm_ptr->local_size;
    pof2 = 1;
    while (pof2 <= comm_size) pof2 <<= 1;
    pof2 >>=1;
    
    if (MV2_Iallreduce_function == MPIR_Iallreduce_redscat_allgather) {
        if (count >= pof2) {
            mpi_errno = MV2_Iallreduce_function(sendbuf, recvbuf, count, datatype,
                                                op, comm_ptr, s);
        }
        else {
            mpi_errno = MPIR_Iallreduce_naive(sendbuf, recvbuf, count, datatype,
                                                op, comm_ptr, s);
        }
    }
    else {
        mpi_errno = MV2_Iallreduce_function(sendbuf, recvbuf, count, datatype,
					     op, comm_ptr, s);
    }

    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    
  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Iallreduce_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Iallreduce_intra_MV2(const void *sendbuf, void *recvbuf, int count,
				    MPI_Datatype datatype, MPI_Op op,
				    MPID_Comm *comm_ptr, MPID_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size, is_homogeneous;
    MPI_Aint sendtype_size, nbytes;
    
    int two_level_iallreduce = 1;
    int range = 0;
    int range_threshold = 0;
    int range_threshold_intra = 0;

    MPIU_Assert(comm_ptr->comm_kind == MPID_INTRACOMM);

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif
    MPIU_Assert(is_homogeneous); /* we don't handle the hetero case right now */
    comm_size = comm_ptr->local_size;
    MPID_Datatype_get_size_macro(datatype, sendtype_size);
    nbytes = count * sendtype_size;

    // Search for some parameters regardless of whether subsequent selected
    // algorithm is 2-level or not
    
    // Search for the corresponding system size inside the tuning table
    while ((range < (mv2_size_iallreduce_tuning_table - 1)) &&
           (comm_size > mv2_iallreduce_thresholds_table[range].numproc)) {
        range++;
    }
    
    // Search for corresponding inter-leader function
    while ((range_threshold < (mv2_iallreduce_thresholds_table[range].size_inter_table - 1))
           && (nbytes >
               mv2_iallreduce_thresholds_table[range].inter_leader[range_threshold].max)
           && (mv2_iallreduce_thresholds_table[range].inter_leader[range_threshold].max != -1)) {
        range_threshold++;
    }

    // Search for corresponding intra-node function
    
    // Commenting this for the time being as none of
    // the algorithms are 2-level
    /*
    while ((range_threshold_intra <
            (mv2_iallreduce_thresholds_table[range].size_intra_table - 1))
           && (nbytes >
               mv2_iallreduce_thresholds_table[range].intra_node[range_threshold_intra].max)
           && (mv2_iallreduce_thresholds_table[range].intra_node[range_threshold_intra].max !=
               -1)) {
        range_threshold_intra++;
    }
    */

    MV2_Iallreduce_function =
        mv2_iallreduce_thresholds_table[range].inter_leader[range_threshold].
        MV2_pt_Iallreduce_function;

    MV2_Iallreduce_intra_node_function =
        mv2_iallreduce_thresholds_table[range].
        intra_node[range_threshold_intra].MV2_pt_Iallreduce_function;

    /* There are currently no two-level nb-allreduce functions hence
       setting to 0 by default */
    two_level_iallreduce = 
        mv2_iallreduce_thresholds_table[range].is_two_level_iallreduce[range_threshold]; 
    if (1 != two_level_iallreduce) {
        mpi_errno = MPIR_Iallreduce_tune_helper_MV2(sendbuf, recvbuf, count, datatype,
                                     op, comm_ptr, s);
    }
    else {
        /* Code path should not enter this with the current algorithms*/
    }

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}
#endif                          /*#if defined(CHANNEL_MRAIL) || defined(CHANNEL_PSM) */

#undef FUNCNAME
#define FUNCNAME MPIR_Iallreduce_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Iallreduce_MV2(const void *sendbuf, void *recvbuf, int count,
				    MPI_Datatype datatype, MPI_Op op,
				    MPID_Comm *comm_ptr, MPID_Sched_t s)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPID_INTRACOMM) {    
#if defined(CHANNEL_MRAIL) || defined(CHANNEL_PSM)
      mpi_errno = MPIR_Iallreduce_intra_MV2(sendbuf, recvbuf, count, datatype,
					    op, comm_ptr, s);
#else
      mpi_errno = MPIR_Iallreduce_intra(sendbuf, recvbuf, count, datatype,
					op, comm_ptr, s);
#endif                          /*#if defined(CHANNEL_MRAIL) || defined(CHANNEL_PSM) */
    }
    else {
      mpi_errno = MPIR_Iallreduce_inter(sendbuf, recvbuf, count, datatype,
					op, comm_ptr, s);
    }

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}
