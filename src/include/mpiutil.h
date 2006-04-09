/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: mpiutil.h,v 1.1.1.1 2006/01/18 21:09:42 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#if !defined(MPIUTIL_H_INCLUDED)
#define MPIUTIL_H_INCLUDED

/*
 * MPIU_Sterror()
 *
 * Thread safe implementation of strerror().  The multi-threaded version will need to use thread specific storage for the string.
 * This prevents the need for allocation of heap memory each time the function is called.  Granted, stack memory could be used,
 * but allocation of large strings on the stack in a multi-threaded environment is not wise since thread stack can be relatively
 * small and a deep nesting of routines that each allocate a reasonably size error for a message can result in stack overrun.
 */
#if defined(HAVE_STRERROR)
#   if (MPICH_THREAD_LEVEL < MPI_THREAD_MULTIPLE || USE_THREAD_IMPL == MPICH_THREAD_IMPL_GLOBAL_MUTEX)
#       define MPIU_Strerror(errno_) strerror(errno_)
#   else
#       error need a thread safe implementation of MPIU_Strerror
#   endif
#else
#   define MPIU_Strerror(errno_) "(strerror() not found)"
#endif

/*
 * MPIU_Assert()
 *
 * Similar to assert() except that it performs an MPID_Abort() when the assertion fails.  Also, for Windows, it doesn't popup a
 * mesage box on a remote machine.
 */
#if (!defined(NDEBUG) && defined(HAVE_ERROR_CHECKING))
#   define MPIU_Assert(a_)													\
    {																\
	if (!(a_))														\
	{															\
	    MPIU_Internal_error_printf("Assertion failed in file %s at line %d: %s\n", __FILE__, __LINE__, MPIU_QUOTE(a_));	\
            MPID_Abort(NULL, MPI_SUCCESS, 1, NULL);										\
	}															\
    }
#else
#   define MPIU_Assert(a_)
#endif

/*
 * MPIU_Assertp()
 *
 * Similar to MPIU_Assert() except that these assertions persist regardless of NDEBUG or HAVE_ERROR_CHECKING.  MPIU_Assertp() may
 * be used for error checking in prototype code, although it should be converted real error checking and reporting once the
 * prototype becomes part of the official and supported code base.
 */
#define MPIU_Assertp(a_)												\
{															\
    if (!(a_))														\
    {															\
        MPIU_Internal_error_printf("Assertion failed in file %s at line %d: %s\n", __FILE__, __LINE__, MPIU_QUOTE(a_));	\
        MPID_Abort(NULL, MPI_SUCCESS, 1, NULL);										\
    }															\
}

#endif /* !defined(MPIUTIL_H_INCLUDED) */
