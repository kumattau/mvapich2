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
/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"

#define NULL_CONTEXT_ID -1

static MPID_Collops collective_functions_osu = {
    0,    /* ref_count */
    MPIR_Barrier_OSU, /* Barrier */ 
    MPIR_Barrier_inter_OSU, /* Barrier Inter*/
    MPIR_Bcast_OSU, /* Bcast intra*/
    MPIR_Bcast_inter_OSU, /*Bcast inter */
    MPIR_Gather_OSU, /* Gather */
    MPIR_Gather_inter_OSU, /* Gather inter */
    MPIR_Gatherv_OSU, /* Gatherv */
    MPIR_Scatter_OSU, /* Scatter */
    MPIR_Scatter_inter_OSU, /* Scatter inter */
    MPIR_Scatterv_OSU, /* Scatterv */
    MPIR_Allgather_OSU, /* Allgather */
    MPIR_Allgather_inter_OSU, /* Allgather inter */
    MPIR_Allgatherv_OSU, /* Allgatherv */
    MPIR_Allgatherv_inter_OSU, /* Allgatherv inter */
    MPIR_Alltoall_OSU, /* Alltoall */
    MPIR_Alltoall_inter_OSU, /* Alltoall inter */
    MPIR_Alltoallv_OSU, /* Alltoallv */
    MPIR_Alltoallv_inter_OSU, /* Alltoallv inter*/
    MPIR_Alltoallw_OSU, /* Alltoallw */
    MPIR_Alltoallw_inter_OSU, /* Alltoallw inter */
    MPIR_Reduce_OSU, /* Reduce */
    MPIR_Reduce_inter_OSU, /* Reduce inter */
    MPIR_Allreduce_OSU, /* Allreduce */
    MPIR_Allreduce_inter_OSU, /* Allreduce inter*/
    MPIR_Reduce_scatter_OSU, /* Reduce_scatter */
	MPIR_Reduce_scatter_inter_OSU, /* Reduce_scatter */
    MPIR_Scan, /* Scan */
    NULL  /* Exscan */
};

static MPID_Collops collective_functions_anl = {
    0,    /* ref_count */
    MPIR_Barrier, /* Barrier */
    MPIR_Barrier_inter, /* Barrier Inter*/
    MPIR_Bcast, /* Bcast intra*/
    MPIR_Bcast_inter, /*Bcast inter */
    MPIR_Gather, /* Gather */
    MPIR_Gather_inter, /* Gather inter */
    MPIR_Gatherv, /* Gatherv */
    MPIR_Scatter, /* Scatter */
    MPIR_Scatter_inter, /* Scatter inter */
    MPIR_Scatterv, /* Scatterv */
    MPIR_Allgather, /* Allgather */
    MPIR_Allgather_inter, /* Allgather inter */
    MPIR_Allgatherv, /* Allgatherv */
    MPIR_Allgatherv_inter, /* Allgather inter */
    MPIR_Alltoall, /* Alltoall */
    MPIR_Alltoall_inter, /* Alltoall inter */
    MPIR_Alltoallv, /* Alltoallv */
    MPIR_Alltoallv_inter, /* Alltoallv inter*/
    MPIR_Alltoallw, /* Alltoallw */
    MPIR_Alltoallw_inter, /* Alltoallw inter */
    MPIR_Reduce, /* Reduce */
	MPIR_Reduce_inter, /* Reduce inter */
    MPIR_Allreduce, /* Allreduce */
    MPIR_Allreduce_inter, /* Allreduce inter*/
	MPIR_Reduce_scatter, /* Reduce_scatter */
	MPIR_Reduce_scatter_inter, /* Reduce_scatter */
    MPIR_Scan, /* Scan */
    NULL  /* Exscan */
};


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_comm_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_comm_create (MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_COMM_CREATE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_COMM_CREATE);

    if(use_osu_collectives == 1)  { 
        comm->coll_fns = &collective_functions_osu;
    }
    if(use_anl_collectives == 1)  {
        comm->coll_fns = &collective_functions_anl;
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_COMM_CREATE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_comm_destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_comm_destroy (MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);
    return mpi_errno;
}

 
