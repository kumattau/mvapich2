/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpid_nem_impl.h"
#include <sched.h>

static int sense;
static int barrier_init = 0;


#undef FUNCNAME
#define FUNCNAME MPID_nem_barrier_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_barrier_init (MPID_nem_barrier_t *barrier_region)
{
    MPID_nem_mem_region.barrier = barrier_region;
    MPID_nem_mem_region.barrier->val = 0;
    MPID_nem_mem_region.barrier->wait = 0;
    sense = 0;
    barrier_init = 1;
    MPID_NEM_WRITE_BARRIER();
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_barrier
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* FIXME: this is not a scalable algorithm because everyone is polling on the same cacheline */
int MPID_nem_barrier (int num_processes, int rank)
{
    int mpi_errno = MPI_SUCCESS;
    MPIU_ERR_CHKANDJUMP1 (!barrier_init, mpi_errno, MPI_ERR_INTERN, "**intern", "**intern %s", "barrier not initialized");
    
    if (MPID_NEM_FETCH_AND_INC (&MPID_nem_mem_region.barrier->val) == MPID_nem_mem_region.num_local - 1)
    {
	MPID_nem_mem_region.barrier->val = 0;
	MPID_nem_mem_region.barrier->wait = 1 - sense;
	MPID_NEM_WRITE_BARRIER();
    }
    else
    {
	/* wait */
	while (MPID_nem_mem_region.barrier->wait == sense)
            sched_yield(); /* skip */
    }
    sense = 1 - sense;

 fn_fail:
    return mpi_errno;
}
