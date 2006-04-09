/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Handle_send_req
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Handle_send_req(MPIDI_VC_t * vc, MPID_Request * sreq, int *complete)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_HANDLE_SEND_REQ);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_HANDLE_SEND_REQ);

    MPIU_UNREFERENCED_ARG(vc);

    switch(sreq->dev.ca)
    {
	case MPIDI_CH3_CA_COMPLETE:
	{
            if (MPIDI_Request_get_type(sreq) == MPIDI_REQUEST_TYPE_GET_RESP)
	    { 
                if (sreq->dev.source_win_handle != MPI_WIN_NULL) {
                    MPID_Win *win_ptr;
                    /* Last RMA operation (get) from source. If active target RMA,
                       decrement window counter. If passive target RMA, 
                       release lock on window and grant next lock in the 
                       lock queue if there is any; no need to send rma done 
                       packet since the last operation is a get. */

                    MPID_Win_get_ptr(sreq->dev.target_win_handle, win_ptr);
                    if (win_ptr->current_lock_type == MPID_LOCK_NONE) {
                        /* FIXME: MT: this has to be done atomically */
                        win_ptr->my_counter -= 1;
                    }
                    else {
                        mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
                    }
                }
            }

	    /* mark data transfer as complete and decrement CC */
	    MPIDI_CH3U_Request_complete(sreq);
	    *complete = TRUE;

	    break;
	}
	
	case MPIDI_CH3_CA_RELOAD_IOV:
	{
	    sreq->dev.iov_count = MPID_IOV_LIMIT;
	    mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq, sreq->dev.iov, &sreq->dev.iov_count);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
						 "**ch3|loadsendiov", 0);
		goto fn_exit;
	    }
	    /* --END ERROR HANDLING-- */
	    
	    *complete = FALSE;
	    break;
	}
	/* --BEGIN ERROR HANDLING-- */
	default:
	{
	    *complete = FALSE;
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN, "**ch3|badca",
					     "**ch3|badca %d", sreq->dev.ca);
	    break;
	}
	/* --END ERROR HANDLING-- */
    }

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_HANDLE_SEND_REQ);
    return mpi_errno;
}

