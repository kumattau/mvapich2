/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

/* FIXME:
   This global appears to be shared with mpid_finalize.c and mpid_vc.c only.
   It would be better to encapsulate this rather than using a global 
   variable */
volatile int MPIDI_Outstanding_close_ops = 0;

/* FIXME: What is this routine for?
   It appears to be used only in ch3_progress, ch3_progress_connect, or
   ch3_progress_sock files.  Is this a general operation, or does it 
   belong in util/sock ? It appears to be used in multiple channels, 
   but probably belongs in mpid_vc, along with the vc exit code that 
   is currently in MPID_Finalize */

/* FIXME: The only event is event_terminated.  Should this have 
   a different name/expected function? */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Handle_connection
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Handle_connection(MPIDI_VC_t * vc, MPIDI_VC_Event_t event)
{
    int inuse;
    int mpi_errno = MPI_SUCCESS;

    MPIDI_DBG_PRINTF((10, FCNAME, "entering"));

    switch (event)
    {
	case MPIDI_VC_EVENT_TERMINATED:
	{
	    switch (vc->state)
	    {
		case MPIDI_VC_STATE_CLOSE_ACKED:
		{
		    MPIU_DBG_PrintVCState2(vc, MPIDI_VC_STATE_INACTIVE);
		    MPIU_DBG_MSG(CH3_CONNECT,TYPICAL,"Setting state to VC_STATE_INACTIVE");
		    vc->state = MPIDI_VC_STATE_INACTIVE;
		    /* MPIU_Object_set_ref(vc, 0); ??? */

		    /*
		     * FIXME: The VC used in connect accept has a NULL process group
		     */
		    if (vc->pg != NULL)
		    { 
			MPIDI_PG_Release_ref(vc->pg, &inuse);
			if (inuse == 0)
			{
			    MPIDI_PG_Destroy(vc->pg);
			}
		    }

		    /* MT: this is not thread safe */
		    MPIDI_Outstanding_close_ops -= 1;
		    MPIDI_DBG_PRINTF((30, FCNAME, "outstanding close operations = %d", MPIDI_Outstanding_close_ops));
	    
		    if (MPIDI_Outstanding_close_ops == 0)
		    {
			MPIDI_CH3_Progress_signal_completion();
		    }

		    break;
		}

		default:
		{
		    mpi_errno = MPIR_Err_create_code(
			MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN, "**ch3|unhandled_connection_state",
			"**ch3|unhandled_connection_state %p %d", vc, event);
		    break;
		}
	    }

	    break;
	}
    
	default:
	{
	    break;
	}
    }
	
    MPIDI_DBG_PRINTF((10, FCNAME, "exiting"));
    return mpi_errno;
}
