/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "mpicomm.h"

/* -- Begin Profiling Symbol Block for routine MPI_Intercomm_merge */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Intercomm_merge = PMPI_Intercomm_merge
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Intercomm_merge  MPI_Intercomm_merge
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Intercomm_merge as PMPI_Intercomm_merge
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#define MPI_Intercomm_merge PMPI_Intercomm_merge

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Intercomm_merge

/*@
MPI_Intercomm_merge - Creates an intracommuncator from an intercommunicator

Input Parameters:
+ comm - Intercommunicator (handle)
- high - Used to order the groups within comm (logical)
  when creating the new communicator.  This is a boolean value; the group
  that sets high true has its processes ordered `after` the group that sets 
  this value to false.  If all processes in the intercommunicator provide
  the same value, the choice of which group is ordered first is arbitrary.

Output Parameter:
. comm_out - Created intracommunicator (handle)

Notes:
 While all processes may provide the same value for the 'high' parameter,
 this requires the MPI implementation to determine which group of 
 processes should be ranked first.  The MPICH implementation uses various
 techniques to determine which group should go first, but there is a 
 possibility that the implementation will be unable to break the tie. 
 Robust applications should avoid using the same value for 'high' in 
 both groups.

.N ThreadSafe

.N Fortran

Algorithm:
.Es
.i Allocate contexts 
.i Local and remote group leaders swap high values
.i Determine the high value.
.i Merge the two groups and make the intra-communicator
.Ee

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_EXHAUSTED

.seealso: MPI_Intercomm_create, MPI_Comm_free
@*/
int MPI_Intercomm_merge(MPI_Comm intercomm, int high, MPI_Comm *newintracomm)
{
    static const char FCNAME[] = "MPI_Intercomm_merge";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPID_Comm *newcomm_ptr;
    int  local_high, remote_high, i, j, new_size, new_context_id;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_INTERCOMM_MERGE);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPID_CS_ENTER();
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_INTERCOMM_MERGE);
    
    MPIR_Nest_incr();

    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(intercomm, mpi_errno);
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr( intercomm, comm_ptr );
    
    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
	    /* If comm_ptr is not valid, it will be reset to null */
	    if (comm_ptr && comm_ptr->comm_kind != MPID_INTERCOMM) {
		mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, 
		    MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_COMM,
						  "**commnotinter", 0 );
	    }
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ... */
    
    /* Make sure that we have a local intercommunicator */
    if (!comm_ptr->local_comm) {
	/* Manufacture the local communicator */
	MPIR_Setup_intercomm_localcomm( comm_ptr );
    }

#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    int acthigh;
	    /* Check for consistent valus of high in each local group.
	     The Intel test suite checks for this; it is also an easy
	     error to make */
	    acthigh = high ? 1 : 0;   /* Clamp high into 1 or 0 */
	    NMPI_Allreduce( MPI_IN_PLACE, &acthigh, 1, MPI_INT, MPI_SUM,
			    comm_ptr->local_comm->handle );

	    /* acthigh must either == 0 or the size of the local comm */
	    if (acthigh != 0 && acthigh != comm_ptr->local_size) {
		mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, 
		    MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_ARG, 
						  "**notsame",
						  "**notsame %s %s", "high", 
						  "MPI_Intercomm_merge" );
		goto fn_fail;
	    }
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* Find the "high" value of the other group of processes.  This
       will be used to determine which group is ordered first in
       the generated communicator.  high is logical */
    local_high = high;
    if (comm_ptr->rank == 0) {
	/* This routine allows use to use the collective communication
	   context rather than the point-to-point context. */
	MPIC_Sendrecv( &local_high, 1, MPI_INT, 0, 0, 
		       &remote_high, 1, MPI_INT, 0, 0, intercomm, 
		       MPI_STATUS_IGNORE );
	
	/* If local_high and remote_high are the same, then order is arbitrary.
	   we use the lpids of the rank 0 member of the local and remote
	   groups to choose an order in this case. */
	if (local_high == remote_high) {
	    int rpid, lpid;
	    int inpids[2], outpids[2];
	    
	    /* We can't guarantee that rpid will give us the same value
	       as the lpid on the partner process, since this
	       may not be different than the lpid if either of these
	       aren't in the same MPI_COMM_WORLD.  Instead, do the following:
	       1) Use the rpid and lpid.  Then, let the
	       lead processes exchange.  If the agree on which 
	       is high and which is low, then done
	       2) Else use MPI_Wtime to generate trial values.  May
	       5 trials (5 is arbitrarily chosen) with
	       the lead processes exchanging values.  Once the
	       tie is broken, done
	       3) If the tie is not broken, then issue an error message
	       asking the user to avoid using the same value of high 
	       on both processes.  See the manpage comments above.
	    */
	    
	    /* Start by getting the lpid and rpid.  This
	       works for processes within the same MPI_COMM_WORLD */
	    (void)MPID_VCR_Get_lpid( comm_ptr->vcr[0], &rpid );
	    (void)MPID_VCR_Get_lpid( comm_ptr->local_vcr[0], &lpid );

	    inpids[0] = rpid;
	    inpids[1] = lpid;
	    MPIC_Sendrecv( inpids, 2, MPI_INT, 0, 1,
			   outpids, 2, MPI_INT, 0, 1, intercomm, 
			   MPI_STATUS_IGNORE );

	    /* Do the values agree? */
	    if (outpids[0] != lpid || outpids[1] != rpid) {
		double tin, tout;
		int    cycle = 0;
		/* The don't.  This means that we can't use them */
		/* Get "alternate" values to use to decide who is low
		   and who is high */
		while (cycle < 5) {
		    tin = NMPI_Wtime();
		    MPIC_Sendrecv( &tin, 1, MPI_DOUBLE, 0, cycle+2, 
				   &tout, 1, MPI_DOUBLE, 0, cycle+2,
				   intercomm, MPI_STATUS_IGNORE );
		    /* Both processors now have the same values */
		    if (tout != tin) break;
		    cycle++;
		}
		if (tout == tin) {
		    /* Error.  The likelihood of this happening is small
		       but not zero.  Indicate an internal error and 
		       exit.  */
		    /* --BEGIN ERROR HANDLING-- */
		    mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, 
						      MPIR_ERR_RECOVERABLE,
		       FCNAME, __LINE__, MPI_ERR_INTERN, "**nouniquehigh", 0 );
		    goto fn_fail;
		    /* --END ERROR HANDLING-- */
		}
		if (tout < tin) {
		    local_high = 1;
		}
		else {
		    local_high = 0;
		}
	    }
	    else {
	    if (rpid < lpid) 
		local_high = 1;
	    else
		local_high = 0;
	    }
	}
    }

    /* 
       All processes in the local group now need to get the 
       value of local_high, which may have changed if both groups
       of processes had the same value for high
    */
    NMPI_Bcast( &local_high, 1, MPI_INT, 0, comm_ptr->local_comm->handle );
        

    newcomm_ptr = (MPID_Comm *)MPIU_Handle_obj_alloc( &MPID_Comm_mem );
    MPIU_ERR_CHKANDJUMP(!newcomm_ptr,mpi_errno,MPI_ERR_OTHER,"**nomem");

    new_size = comm_ptr->local_size + comm_ptr->remote_size;
    MPIU_Object_set_ref( newcomm_ptr, 1 );
    newcomm_ptr->context_id   = comm_ptr->context_id + 2; /* See below */
    newcomm_ptr->remote_size  = newcomm_ptr->local_size   = new_size;
    newcomm_ptr->rank         = -1;
    newcomm_ptr->local_group  = 0;
    newcomm_ptr->remote_group = 0;
    newcomm_ptr->comm_kind    = MPID_INTRACOMM;
    newcomm_ptr->attributes   = 0;

    /* Now we know which group comes first.  Build the new vcr 
       from the existing vcrs */
    MPID_VCRT_Create( new_size, &newcomm_ptr->vcrt );
    MPID_VCRT_Get_ptr( newcomm_ptr->vcrt, &newcomm_ptr->vcr );
    if (local_high) {
	/* remote group first */
	j = 0;
	for (i=0; i<comm_ptr->remote_size; i++) {
	    MPID_VCR_Dup( comm_ptr->vcr[i], &newcomm_ptr->vcr[j++] );
	}
	for (i=0; i<comm_ptr->local_size; i++) {
	    if (i == comm_ptr->rank) newcomm_ptr->rank = j;
	    MPID_VCR_Dup( comm_ptr->local_vcr[i], &newcomm_ptr->vcr[j++] );
	}
    }
    else {
	/* local group first */
	j = 0;
	for (i=0; i<comm_ptr->local_size; i++) {
	    if (i == comm_ptr->rank) newcomm_ptr->rank = j;
	    MPID_VCR_Dup( comm_ptr->local_vcr[i], &newcomm_ptr->vcr[j++] );
	}
	for (i=0; i<comm_ptr->remote_size; i++) {
	    MPID_VCR_Dup( comm_ptr->vcr[i], &newcomm_ptr->vcr[j++] );
	}
    }

    /* We've setup a temporary context id, based on the context id
       used by the intercomm.  This allows us to perform the allreduce
       operations within the context id algorithm, since we already
       have a valid (almost - see comm_create_hook) communicator.
    */
    /* printf( "About to get context id \n" ); fflush( stdout ); */
    new_context_id = MPIR_Get_contextid( newcomm_ptr );
    MPIU_ERR_CHKANDJUMP(new_context_id == 0,mpi_errno,MPI_ERR_OTHER,
			"**toomanycomm" );

    /* printf( "Resetting contextid\n" ); fflush( stdout ); */
    newcomm_ptr->context_id = new_context_id;

    /* Notify the device of this new communicator */
    MPID_Dev_comm_create_hook( newcomm_ptr );

    *newintracomm = newcomm_ptr->handle;

    /* ... end of body of routine ... */
    
  fn_exit:
    MPIR_Nest_decr();
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_INTERCOMM_MERGE);
    MPID_CS_EXIT();
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_intercomm_merge",
	    "**mpi_intercomm_merge %C %d %p", intercomm, high, newintracomm);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
