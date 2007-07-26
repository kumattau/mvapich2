/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* Complete a request, saving the status data if necessary.
   "active" has meaning only if the request is a persistent request; this 
   allows the completion routines to indicate that a persistent request 
   was inactive and did not require any extra completion operation.

   If debugger information is being provided for pending (user-initiated) 
   send operations, the macros MPIR_SENDQ_FORGET will be defined to 
   call the routine MPIR_Sendq_forget; otherwise that macro will be a no-op.
   The implementation of the MPIR_Sendq_xxx is in src/mpi/debugger/dbginit.c .
*/
int MPIR_Request_complete(MPI_Request * request, MPID_Request * request_ptr, 
			  MPI_Status * status, int * active)
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
	    /* FIXME: are Ibsend requests added to the send queue? */
	    MPIR_SENDQ_FORGET(request_ptr);
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
		
		if (prequest_ptr->kind != MPID_UREQUEST)
		{
		    if (status != MPI_STATUS_IGNORE)
		    {
			status->cancelled = prequest_ptr->status.cancelled;
		    }
		    mpi_errno = prequest_ptr->status.MPI_ERROR;
		}
		else
		{
		    /* This is needed for persistent Bsend requests */
		    MPIU_THREADPRIV_DECL;
		    MPIU_THREADPRIV_GET;
		    MPIR_Nest_incr();
		    {
			int rc;
			
			rc = MPIR_Grequest_query(prequest_ptr);
			if (mpi_errno == MPI_SUCCESS)
			{
			    mpi_errno = rc;
			}
			if (status != MPI_STATUS_IGNORE)
			{
			    status->cancelled = prequest_ptr->status.cancelled;
			}
			if (mpi_errno == MPI_SUCCESS)
			{
			    mpi_errno = prequest_ptr->status.MPI_ERROR;
			}
			rc = MPIR_Grequest_free(prequest_ptr);
			if (mpi_errno == MPI_SUCCESS)
			{
			    mpi_errno = rc;
			}
		    }
		    MPIR_Nest_decr();
		}

		/* FIXME: MPIR_SENDQ_FORGET(request_ptr); -- it appears that
		   persistent sends are not currently being added to the send
		   queue.  should they be, or should this release be
		   conditional? */
		MPID_Request_release(prequest_ptr);
	    }
	    else
	    {
		if (request_ptr->status.MPI_ERROR != MPI_SUCCESS)
		{
		    /* if the persistent request failed to start then make the
		       error code available */
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
		    /* if the persistent request failed to start then make the
		       error code available */
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
	    MPIU_THREADPRIV_DECL;

	    MPIU_THREADPRIV_GET;

	    /* The user error handler may make calls to MPI routines, so the
	       nesting counter must be incremented before the handler is
	       called */
	    MPIR_Nest_incr();
	    {
		int rc;
		
		rc = MPIR_Grequest_query(request_ptr);
		if (mpi_errno == MPI_SUCCESS)
		{
		    mpi_errno = rc;
		}
		MPIR_Request_extract_status(request_ptr, status);
		rc = MPIR_Grequest_free(request_ptr);
		if (mpi_errno == MPI_SUCCESS)
		{
		    mpi_errno = rc;
		}
		
		MPID_Request_release(request_ptr);
		*request = MPI_REQUEST_NULL;
	    }
	    MPIR_Nest_decr();
	    
	    break;
	}
	
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase",
		"**badcase %d", request_ptr->kind);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}


/* FIXME: What is this routine for?
 *  
 * [BRT] it is used by testall, although looking at testall now, I think the
 * algorithm can be change slightly and eliminate the need for this routine
 */
int MPIR_Request_get_error(MPID_Request * request_ptr)
{
    static const char FCNAME[] = "MPIR_Request_get_error";
    int mpi_errno = MPI_SUCCESS;
    MPIU_THREADPRIV_DECL;

    MPIU_THREADPRIV_GET;

    switch(request_ptr->kind)
    {
	case MPID_REQUEST_SEND:
	case MPID_REQUEST_RECV:
	{
	    mpi_errno = request_ptr->status.MPI_ERROR;
	    break;
	}

	case MPID_PREQUEST_SEND:
	{
	    if (request_ptr->partner_request != NULL)
	    {
		if (request_ptr->partner_request->kind == MPID_UREQUEST)
		{
		    /* This is needed for persistent Bsend requests */
		    mpi_errno = MPIR_Grequest_query(
			request_ptr->partner_request);
		}
		if (mpi_errno == MPI_SUCCESS)
		{
		    mpi_errno = request_ptr->partner_request->status.MPI_ERROR;
		}
	    }
	    else
	    {
		mpi_errno = request_ptr->status.MPI_ERROR;
	    }

	    break;
	}

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
	    
	    /* The user error handler may make calls to MPI routines, so the 
	       nesting counter must be incremented before the handler 
	       is called */
	    MPIU_THREADPRIV_DECL;
	    MPIU_THREADPRIV_GET;
	    MPIR_Nest_incr();
    
	    switch (request_ptr->greq_lang)
	    {
		case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
		case MPID_LANG_CXX:
#endif
		    rc = (request_ptr->query_fn)(
			request_ptr->grequest_extra_state,
			&request_ptr->status);
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno,
			MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
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
		    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno,
			MPI_ERR_OTHER,;, "**user", "**userquery %d", rc);
		    break;
		}
#endif
		
		default:
		{
		    /* --BEGIN ERROR HANDLING-- */
		    /* This should not happen */
		    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, 
				 "**badcase", 
				 "**badcase %d", request_ptr->greq_lang);
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
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase", 
				 "**badcase %d", request_ptr->kind);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}

#ifdef HAVE_FORTRAN_BINDING
/* Set the language type to Fortran for this (generalized) request */
void MPIR_Grequest_set_lang_f77( MPI_Request greq )
{
    MPID_Request *greq_ptr;

    MPID_Request_get_ptr( greq, greq_ptr );

    greq_ptr->greq_lang = MPID_LANG_FORTRAN;
}
#endif


#undef FUNCNAME
#define FUNCNAME MPIR_Grequest_cancel
int MPIR_Grequest_cancel(MPID_Request * request_ptr, int complete)
{
    static const char * FCNAME = MPIU_QUOTE(FUNCNAME);
    int rc;
    int mpi_errno = MPI_SUCCESS;
    
    switch (request_ptr->greq_lang)
    {
	case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	case MPID_LANG_CXX:
#endif
	    rc = (request_ptr->cancel_fn)(
		request_ptr->grequest_extra_state, complete);
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno,
		MPI_ERR_OTHER,;, "**user", "**usercancel %d", rc);
	    break;
#ifdef HAVE_FORTRAN_BINDING
	case MPID_LANG_FORTRAN:
	case MPID_LANG_FORTRAN90:
	{
	    MPI_Fint ierr;

	    ((MPIR_Grequest_f77_cancel_function *)(request_ptr->cancel_fn))(
		request_ptr->grequest_extra_state, &complete, &ierr);
	    rc = (int) ierr;
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,
		{;}, "**user", "**usercancel %d", rc);
	    break;
	}
#endif
		
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase",
		"**badcase %d", request_ptr->greq_lang);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIR_Grequest_query
int MPIR_Grequest_query(MPID_Request * request_ptr)
{
    static const char * FCNAME = MPIU_QUOTE(FUNCNAME);
    int rc;
    int mpi_errno = MPI_SUCCESS;
    
    switch (request_ptr->greq_lang)
    {
	case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	case MPID_LANG_CXX:
#endif
	    rc = (request_ptr->query_fn)(request_ptr->grequest_extra_state,
		&request_ptr->status);
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,
		{;}, "**user", "**userquery %d", rc);
	    break;
#ifdef HAVE_FORTRAN_BINDING
	case MPID_LANG_FORTRAN:
	case MPID_LANG_FORTRAN90:
	{
	    MPI_Fint ierr;
	    ((MPIR_Grequest_f77_query_function *)(request_ptr->query_fn))( 
		request_ptr->grequest_extra_state, &request_ptr->status, &ierr );
	    rc = (int)ierr;
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,
		{;}, "**user", "**userquery %d", rc);
	}
	break;
#endif	    
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN,;, "**badcase",
		"**badcase %d", request_ptr->greq_lang);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIR_Grequest_free
int MPIR_Grequest_free(MPID_Request * request_ptr)
{
    static const char * FCNAME = MPIU_QUOTE(FUNCNAME);
    int rc;
    int mpi_errno = MPI_SUCCESS;
    
    switch (request_ptr->greq_lang)
    {
	case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	case MPID_LANG_CXX:
#endif
	    rc = (request_ptr->free_fn)(request_ptr->grequest_extra_state);
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,
		{;}, "**user", "**userfree %d", rc);
	    break;
#ifdef HAVE_FORTRAN_BINDING
	case MPID_LANG_FORTRAN:
	case MPID_LANG_FORTRAN90:
	{
	    MPI_Fint ierr;
		    
	    ((MPIR_Grequest_f77_free_function *)(request_ptr->free_fn))(
		request_ptr->grequest_extra_state, &ierr);
	    rc = (int) ierr;
	    MPIU_ERR_CHKANDSTMT1((rc != MPI_SUCCESS), mpi_errno, MPI_ERR_OTHER,
		{;}, "**user", "**userfree %d", rc);
	    break;
	}
#endif
		
	default:
	{
	    /* --BEGIN ERROR HANDLING-- */
	    /* This should not happen */
	    MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_INTERN, {;}, "**badcase",
		"**badcase %d", request_ptr->greq_lang);
	    break;
	    /* --END ERROR HANDLING-- */
	}
    }

    return mpi_errno;
}
