/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"

/*
 * MPIDI_CH3_Comm_spawn()
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Comm_spawn
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Comm_spawn(const char *command, const char *argv[],
                         const int maxprocs, MPI_Info info, const int root,
                         MPID_Comm * comm, MPID_Comm * intercomm,
                         int array_of_errcodes[])
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_COMM_SPAWN);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_COMM_SPAWN);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_COMM_SPAWN);
    return mpi_errno;
}
