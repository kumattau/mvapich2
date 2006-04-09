/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"


int MPIR_Request_complete(MPI_Request * request, MPID_Request * request_ptr, MPI_Status * status, int * active)
{
    static const char FCNAME[] = "MPIR_Request_complete";
    int mpi_errno = MPI_SUCCESS;

    *active = TRUE;
    switch(request_ptr->kind)
    {
	case MPID_REQUEST_SEND:
	{
	    if (status != MPI_STATUS_IGNORE)
	    {
		status->cancelled = request_ptr->status.cancelled;
	    }
	    mpi_errno = request_ptr->status.MPI_ERROR;
	    MPID_Request_release(request_ptr);
	    *request = MPI_REQUEST_NULL;
	    break;
	}
	case MPID_REQUEST_RECV:
	{
	    MPIR_Request_extract_status(request_ptr, status);
	    mpi_errno = request_ptr->status.MPI_ERROR;
	    MPID_Request_release(request_ptr);
	    *request = MPI_REQUEST_NULL;
	    break;
	}
			
	case MPID_PREQUEST_SEND:
	{
	    if (request_ptr->partner_request != NULL)
	    {
		MPID_Request * prequest_ptr = request_ptr->partner_request;

		/* reset persistent request to inactive state */
		request_ptr->cc = 0;
		request_ptr->cc_ptr = &request_ptr->cc;
		request_ptr->partner_request = NULL;
		
		if (status != MPI_STATUS_IGNORE)
		{
		    status->cancelled = prequest_ptr->status.cancelled;
		}
		mpi_errno = prequest_ptr->status.MPI_ERROR;
	    
		MPID_Request_release(prequest_ptr);
	    }
	    else
	    {
		if (request_ptr->status.MPI_ERROR != MPI_SUCCESS)
		{
		    /* if the persistent request failed to start then make the error code available */
		    if (status != MPI_STATUS_IGNORE)
		    {
			status->cancelled = FALSE;
		    }
		    mpi_errno = request_ptr->status.MPI_ERROR;
		}
		else
		{
		    MPIR_Status_set_empty(status);
		    *active = FALSE;
		}
	    }
	    
	    break;
	}
	
	case MPID_PREQUEST_RECV:
	{
	    if (request_ptr->partner_request != NULL)
	    {
		MPID_Request * prequest_ptr = request_ptr->partner_request;

		/* reset persistent request to inactive state */
		request_ptr->cc = 0;
		request_ptr->cc_ptr = &request_ptr->cc;
		request_ptr->partner_request = NULL;
		
		MPIR_Request_extract_status(prequest_ptr, status);
		mpi_errno = prequest_ptr->status.MPI_ERROR;
	    
		MPID_Request_release(prequest_ptr);
	    }
	    else
	    {
		MPIR_Status_set_empty(status);
		/* --BEGIN ERROR HANDLING-- */
		if (request_ptr->status.MPI_ERROR != MPI_SUCCESS)
		{
		    /* if the persistent request failed to start then make the error code available */
		    mpi_errno = request_ptr->status.MPI_ERROR;
		}
		else
		{
		    *active = FALSE;
		}
		/* --END ERROR HANDLING-- */
	    }
	    
	    break;
	}

	case MPID_UREQUEST:
	{
	    int rc;

	    /* The user error handler may make calls to MPI routines, so the nesting counter must be incremented before the
	     * handler is called */
	    MPIR_Nest_incr();
    
	    switch (request_ptr->greq_lang)
	    {
		case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
		case MPID_LANG_CXX:
#endif
		    rc = (request_ptr->query_fn)(request_ptr->grequest_extra_state, &request_ptr->status);
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
		    break;
#ifdef HAVE_FORTRAN_BINDING
		case MPID_LANG_FORTRAN:
		case MPID_LANG_FORTRAN90:
		{
		    MPI_Fint ierr;
		    ((MPIR_Grequest_f77_query_function *)(request_ptr->query_fn))( 
			request_ptr->grequest_extra_state, &request_ptr->status, &ierr );
		    rc = (int)ierr;
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
		}
		break;
#endif	    
		default:
		{
		    /* --BEGIN ERROR HANDLING-- */
		    /* This should not happen */
		    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", "**badcase %d", request_ptr->greq_lang);
		    break;
		    /* --END ERROR HANDLING-- */
		}
	    }

	    MPIR_Request_extract_status(request_ptr, status);

	    switch (request_ptr->greq_lang)
	    {
		case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
		case MPID_LANG_CXX:
#endif
		    rc = (request_ptr->free_fn)(request_ptr->grequest_extra_state);
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userfree %d", rc);
		    break;
#ifdef HAVE_FORTRAN_BINDING
		case MPID_LANG_FORTRAN:
		case MPID_LANG_FORTRAN90:
		{
		    MPI_Fint ierr;
		    
		    ((MPIR_Grequest_f77_free_function *)(request_ptr->free_fn))(request_ptr->grequest_extra_state, &ierr);
		    rc = (int) ierr;
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userfree %d", rc);
		    break;
		}
#endif
		
		default:
		{
		    /* --BEGIN ERROR HANDLING-- */
		    /* This should not happen */
		    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", "**badcase %d", request_ptr->greq_lang);
		    break;
		    /* --END ERROR HANDLING-- */
		}
	    }
	    
	    MPID_Request_release(request_ptr);
	    *request = MPI_REQUEST_NULL;

	    MPIR_Nest_decr();
	    
	    break;
	}
	
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", "**badcase %d", request_ptr->kind);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}


int MPIR_Request_get_error(MPID_Request * request_ptr)
{
    static const char FCNAME[] = "MPIR_Request_get_error";
    int mpi_errno = MPI_SUCCESS;

    switch(request_ptr->kind)
    {
	case MPID_REQUEST_SEND:
	case MPID_REQUEST_RECV:
	{
	    mpi_errno = request_ptr->status.MPI_ERROR;
	    break;
	}

	case MPID_PREQUEST_SEND:
	case MPID_PREQUEST_RECV:
	{
	    if (request_ptr->partner_request != NULL)
	    {
		mpi_errno = request_ptr->partner_request->status.MPI_ERROR;
	    }
	    else
	    {
		mpi_errno = request_ptr->status.MPI_ERROR;
	    }

	    break;
	}

	case MPID_UREQUEST:
	{
	    int rc;
	    
	    /* The user error handler may make calls to MPI routines, so the nesting counter must be incremented before the
	       handler is called */
	    MPIR_Nest_incr();
    
	    switch (request_ptr->greq_lang)
	    {
		case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
		case MPID_LANG_CXX:
#endif
		    rc = (request_ptr->query_fn)(request_ptr->grequest_extra_state, &request_ptr->status);
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
		    break;
#ifdef HAVE_FORTRAN_BINDING
		case MPID_LANG_FORTRAN:
		case MPID_LANG_FORTRAN90:
		{
		    MPI_Fint ierr;
		    ((MPIR_Grequest_f77_query_function*)(request_ptr->query_fn))( 
			request_ptr->grequest_extra_state, &request_ptr->status,
			&ierr );
		    rc = (int) ierr;
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
		    break;
		}
#endif
		
		default:
		{
		    /* --BEGIN ERROR HANDLING-- */
		    /* This should not happen */
		    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", "**badcase %d", request_ptr->greq_lang);
		    break;
		    /* --END ERROR HANDLING-- */
		}
	    }

	    MPIR_Nest_decr();
	    break;
	}
	
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", "**badcase %d", request_ptr->kind);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}

#ifdef HAVE_FORTRAN_BINDING
/* Set the language type to Fortran for this request */
void MPIR_Grequest_set_lang_f77( MPI_Request greq )
{
    MPID_Request *greq_ptr;

    MPID_Request_get_ptr( greq, greq_ptr );

    greq_ptr->greq_lang = MPID_LANG_FORTRAN;
}
#endif
