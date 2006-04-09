/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* style: allow:fprintf:4 sig:0 */

/* stdarg is required to handle the variable argument lists for 
   MPIR_Err_create_code */
#include <stdarg.h>
/* Define USE_ERR_CODE_VALIST to get the prototype for the valist version
   of MPIR_Err_create_code in mpiimpl.h */
#define USE_ERR_CODE_VALIST

#include "mpiimpl.h"
#include "errcodes.h"

/* defmsg is generated automatically from the source files and contains
   all of the error messages */
#include "defmsg.h" 


/* stdio is needed for vsprintf and vsnprintf */
#include <stdio.h>

static int convertErrcodeToIndexes( int errcode, int *ring_idx, int *ring_id,
				    int *generic_idx );
static const char *get_class_msg( int error_class );

/* FIXME: The following comment was the original description and was never 
   changed to match massive changes in the code and associated data structures
*/
/*
 * Instance-specific error messages are stored in a ring.
 * Messages are written into the error_ring; the corresponding entry in
 * error_ring_seq is used to keep a sort of "checkvalue" to ensure that the
 * error code that points at this message is in fact for this particular 
 * message.  This is used to handle the unlikely but possible situation where 
 * so many error messages are generated that the ring is overlapped.
 *
 * The message arrays are preallocated to ensure that there is space for these
 * messages when an error occurs.  One variation would be to allow these
 * to be dynamically allocated, but it is probably better to either preallocate
 * these or turn off all error message generation (which will eliminate these
 * arrays).
 *
 * One possible alternative is to use the message ring *only* for instance
 * messages and use the predefined messages in-place for the generic
 * messages.  This approach is used to provide uniform handling of all 
 * error messages.
 */
#if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
#define MAX_ERROR_RING ERROR_SPECIFIC_INDEX_SIZE
#define MAX_FCNAME_LEN 63

typedef struct MPIR_Err_msg
{
    /* identifier used to check for validity of instance-specific messages; consists of the class, generic index and hash of the
       specific message */
    int  id;
    
    /* The previous error code that caused this error to be generated; this allows errors to be chained together */
    int  prev_error;
    
    /* function name and line number where the error occurred */
    char fcname[MAX_FCNAME_LEN+1];
    
    /* actual error message */
    char msg[MPI_MAX_ERROR_STRING+1];
    int use_user_error_code;
    int user_error_code;
}
MPIR_Err_msg_t;

static MPIR_Err_msg_t ErrorRing[MAX_ERROR_RING];
static volatile unsigned int error_ring_loc = 0;
#if !defined(MPICH_SINGLE_THREADED)
static MPID_Thread_lock_t error_ring_mutex;
#define error_ring_mutex_create() MPID_Thread_mutex_create(&error_ring_mutex)
#define error_ring_mutex_destroy() MPID_Thread_mutex_create(&error_ring_mutex)
#define error_ring_mutex_lock() MPID_Thread_mutex_lock(&error_ring_mutex)
#define error_ring_mutex_unlock() MPID_Thread_mutex_unlock(&error_ring_mutex)
#else
#define error_ring_mutex_create()
#define error_ring_mutex_destroy()
#define error_ring_mutex_lock()
#define error_ring_mutex_unlock()
#endif

#endif /* (MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL) */

/* turn this flag on until we debug and release mpich2 */
int MPIR_Err_print_stack_flag = TRUE;
static int MPIR_Err_abort_on_error = FALSE;
static int MPIR_Err_chop_error_stack = FALSE;
static int MPIR_Err_chop_width = 80;

void MPIR_Err_init( void )
{
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	char *env;
	int n;

	error_ring_mutex_create();

	/* FIXME: Use a more general parameter mechanism */
	/* FIXME: Don't cut and paste code like this; instead, use 
	   a routine to ensure uniform handling (not just here but anywhere
	   that any of these values is allowed for on/off) */
	env = getenv("MPICH_ABORT_ON_ERROR");
	if (env)
	{
	    if (strcmp(env, "1") == 0 || strcmp(env, "on") == 0 || strcmp(env, "yes") == 0)
	    { 
		MPIR_Err_abort_on_error = TRUE;
	    }
	    if (strcmp(env, "0") == 0 || strcmp(env, "off") == 0 || strcmp(env, "no") == 0)
	    { 
		MPIR_Err_abort_on_error = FALSE;
	    }
	}
	
	env = getenv("MPICH_PRINT_ERROR_STACK");
	if (env)
	{
	    if (strcmp(env, "1") == 0 || strcmp(env, "on") == 0 || strcmp(env, "yes") == 0)
	    { 
		MPIR_Err_print_stack_flag = TRUE;
	    }
	    if (strcmp(env, "0") == 0 || strcmp(env, "off") == 0 || strcmp(env, "no") == 0)
	    {
		MPIR_Err_print_stack_flag = FALSE;
	    }
	}

	env = getenv("MPICH_CHOP_ERROR_STACK");
	if (env)
	{
#ifdef HAVE_WINDOWS_H
	    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	    if (hConsole != INVALID_HANDLE_VALUE)
	    {
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (GetConsoleScreenBufferInfo(hConsole, &info))
		{
		    MPIR_Err_chop_width = info.dwMaximumWindowSize.X;
		}
	    }
#endif
	    /* FIXME: atoi will not signal an error */
	    n = atoi(env);
	    if (n > 0)
	    {
		MPIR_Err_chop_error_stack = TRUE;
		MPIR_Err_chop_width = n;
	    }
	    else if (n == 0)
	    {
		if (*env == '\0')
		{
		    MPIR_Err_chop_error_stack = TRUE;
		}
		else
		{
		    MPIR_Err_chop_error_stack = FALSE;
		}
	    }
	    else
	    {
		MPIR_Err_chop_error_stack = TRUE;
	    }
	}
    }
#   endif
}

/* Special error handler to call if we are not yet initialized */
void MPIR_Err_preinit( void )
{
    MPIU_Error_printf("Error encountered before initializing MPICH\n");
    exit(1);
}

/*
 * This is the routine that is invoked by most MPI routines to 
 * report an error 
 */
int MPIR_Err_return_comm( MPID_Comm  *comm_ptr, const char fcname[], 
			  int errcode )
{
    const int error_class = ERROR_GET_CLASS(errcode);
    char error_msg[4096];
    int len;
    
    if (error_class > MPICH_ERR_LAST_CLASS)
    {
	if (errcode & ~ERROR_CLASS_MASK)
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  The error stack follows:\n", error_class, fcname);
	    MPIR_Err_print_stack(stderr, errcode);
	}
	else
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  No error stack is available.\n", error_class, fcname);
	}
	
	errcode = (errcode & ~ERROR_CLASS_MASK) | MPI_ERR_UNKNOWN;
    }
    
    /* First, check the nesting level */
    if (MPIR_Nest_value()) return errcode;
    
    if (!comm_ptr || comm_ptr->errhandler == NULL) {
	/* Try to replace with the default handler, which is the one on MPI_COMM_WORLD.  This gives us correct behavior for the
	   case where the error handler on MPI_COMM_WORLD has been changed. */
	if (MPIR_Process.comm_world)
	{
	    comm_ptr = MPIR_Process.comm_world;
	}
    }

    if (MPIR_Err_is_fatal(errcode) ||
	comm_ptr == NULL || comm_ptr->errhandler == NULL || 
	comm_ptr->errhandler->handle == MPI_ERRORS_ARE_FATAL)
    {
	MPIU_Snprintf(error_msg, 4096, "Fatal error in %s: ", fcname);
	len = (int)strlen(error_msg);
	MPIR_Err_get_string(errcode, &error_msg[len], 4096-len, NULL);
	MPID_Abort(comm_ptr, MPI_SUCCESS, 13, error_msg);
    }

    /* If the last error in the stack is a user function error, return that 
       error instead of the corresponding mpi error code? */
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    if (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;

		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		}
		else {
		    /* Can we get a more specific error message */
		    if (generic_idx >= 0 && ErrorRing[ring_idx].id == ring_id && ErrorRing[ring_idx].use_user_error_code)
			{
			    errcode = ErrorRing[ring_idx].user_error_code;
			}
		}
	    }
	}
	error_ring_mutex_unlock();
    }
#   endif
    
    if (comm_ptr->errhandler->handle == MPI_ERRORS_RETURN)
    {
	return errcode;
    }
    else
    {
	/* The user error handler may make calls to MPI routines, so the
	 * nesting counter must be incremented before the handler is called */
	MPIR_Nest_incr();
    
	/* We pass a final 0 (for a null pointer) to these routines
	   because MPICH-1 expected that */
	switch (comm_ptr->errhandler->language)
	{
	case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	case MPID_LANG_CXX:
#endif
	    (*comm_ptr->errhandler->errfn.C_Comm_Handler_function)( 
		&comm_ptr->handle, &errcode, 0 );
	    break;
#ifdef HAVE_FORTRAN_BINDING
	case MPID_LANG_FORTRAN90:
	case MPID_LANG_FORTRAN:
	    (*comm_ptr->errhandler->errfn.F77_Handler_function)( 
		(MPI_Fint *)&comm_ptr->handle, &errcode, 0 );
	    break;
#endif
	}

	MPIR_Nest_decr();
    }
    return errcode;
}

/* 
 * MPI routines that detect errors on window objects use this to report errors
 */
int MPIR_Err_return_win( MPID_Win  *win_ptr, const char fcname[], int errcode )
{
    const int error_class = ERROR_GET_CLASS(errcode);
    char error_msg[4096];
    int len;

    if (win_ptr == NULL || win_ptr->errhandler == NULL)
	return MPIR_Err_return_comm(NULL, fcname, errcode);

    if (error_class > MPICH_ERR_LAST_CLASS)
    {
	if (errcode & ~ERROR_CLASS_MASK)
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  The error stack follows:\n", error_class, fcname);
	    MPIR_Err_print_stack(stderr, errcode);
	}
	else
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  No error stack is available.\n", error_class, fcname);
	}
	
	errcode = (errcode & ~ERROR_CLASS_MASK) | MPI_ERR_UNKNOWN;
    }

    /* First, check the nesting level */
    if (MPIR_Nest_value()) return errcode;

    if (MPIR_Err_is_fatal(errcode) ||
	win_ptr == NULL || win_ptr->errhandler == NULL || win_ptr->errhandler->handle == MPI_ERRORS_ARE_FATAL)
    {
	MPIU_Snprintf(error_msg, 4096, "Fatal error in %s: ", fcname);
	len = (int)strlen(error_msg);
	MPIR_Err_get_string(errcode, &error_msg[len], 4096-len, NULL);
	MPID_Abort(NULL, MPI_SUCCESS, 13, error_msg);
    }

    /* If the last error in the stack is a user function error, return that error instead of the corresponding mpi error code? */
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    if (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;
		
		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		}
		else {
		    if (generic_idx >= 0 && ErrorRing[ring_idx].id == ring_id &&
			ErrorRing[ring_idx].use_user_error_code)
			{
			    errcode = ErrorRing[ring_idx].user_error_code;
			}
		}
	    }
	}
	error_ring_mutex_unlock();
    }
#   endif
    
    if (win_ptr->errhandler->handle == MPI_ERRORS_RETURN)
    {
	return errcode;
    }
    else
    {
	/* Now, invoke the error handler for the window */

	/* The user error handler may make calls to MPI routines, so the
	 * nesting counter must be incremented before the handler is called */
	MPIR_Nest_incr();
    
	/* We pass a final 0 (for a null pointer) to these routines
	   because MPICH-1 expected that */
	switch (win_ptr->errhandler->language)
	{
	    case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	    case MPID_LANG_CXX:
#endif
		(*win_ptr->errhandler->errfn.C_Win_Handler_function)( 
		    &win_ptr->handle, &errcode, 0 );
		break;
#ifdef HAVE_FORTRAN_BINDING
	    case MPID_LANG_FORTRAN90:
	    case MPID_LANG_FORTRAN:
		(*win_ptr->errhandler->errfn.F77_Handler_function)( 
		    (MPI_Fint *)&win_ptr->handle, &errcode, 0 );
		break;
#endif
	}

	MPIR_Nest_decr();
    }
    return errcode;
}

/* 
 * MPI routines that detect errors on files use this to report errors 
 */
#if 0
int MPIR_Err_return_file( MPID_File  *file_ptr, const char fcname[], 
			  int errcode )
{
    const int error_class = ERROR_GET_CLASS(errcode);
    char error_msg[4096];
    int len;

    if (file_ptr == NULL || file_ptr->errhandler == NULL)
	return MPIR_Err_return_comm(NULL, fcname, errcode);

    if (error_class > MPICH_ERR_LAST_CLASS)
    {
	if (errcode & ~ERROR_CLASS_MASK)
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  The error stack follows:\n", error_class, fcname);
	    MPIR_Err_print_stack(stderr, errcode);
	}
	else
	{
	    MPIU_Error_printf("INTERNAL ERROR: Invalid error class (%d) encountered while returning from\n"
			      "%s.  Please file a bug report.  No error stack is available.\n", error_class, fcname);
	}
	
	errcode = (errcode & ~ERROR_CLASS_MASK) | MPI_ERR_UNKNOWN;
    }
    
    /* First, check the nesting level */
    if (MPIR_Nest_value()) return errcode;

    if (MPIR_Err_is_fatal(errcode) || file_ptr->errhandler->handle == MPI_ERRORS_ARE_FATAL)
    {
	MPIU_Snprintf(error_msg, 4096, "Fatal error in %s: ", fcname);
	len = (int)strlen(error_msg);
	MPIR_Err_get_string(errcode, &error_msg[len], 4096-len, NULL);
	MPID_Abort(NULL, MPI_SUCCESS, 13, error_msg);
    }

    /* If the last error in the stack is a user function error, return that error instead of the corresponding mpi error code? */
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    if (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;
		
		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		}
		else {
		    if (generic_idx >= 0 && ErrorRing[ring_idx].id == ring_id && ErrorRing[ring_idx].use_user_error_code)
			{
			    errcode = ErrorRing[ring_idx].user_error_code;
			}
		}
	    }
	}
	error_ring_mutex_unlock();
    }
#   endif
    
    if (file_ptr->errhandler->handle == MPI_ERRORS_RETURN)
    {
	return errcode;
    }
    else
    {
	/* Invoke the provided error handler */

	/* The user error handler may make calls to MPI routines, so the
	 * nesting counter must be incremented before the handler is called */
	MPIR_Nest_incr();
    
	switch (file_ptr->errhandler->language)
	{
	    case MPID_LANG_C:
#ifdef HAVE_CXX_BINDING
	    case MPID_LANG_CXX:
#endif
		(*file_ptr->errhandler->errfn.C_File_Handler_function)( 
		(MPI_File *)&file_ptr->handle, &errcode );
		break;
#ifdef HAVE_FORTRAN_BINDING
	    case MPID_LANG_FORTRAN90:
	    case MPID_LANG_FORTRAN:
		(*file_ptr->errhandler->errfn.F77_Handler_function)( 
		    (MPI_Fint *)&file_ptr->handle, &errcode );
		break;
#endif
	}

	MPIR_Nest_decr();
    }
    return errcode;
}
#endif

#if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_CLASS
/* Given a message string abbreviation (e.g., one that starts "**"), return the corresponding index.  For the generic (non
 * parameterized messages), use idx = FindGenericMsgIndex( "**msg" );
 */
static int FindGenericMsgIndex( const char *msg )
{
    int i, c;
    for (i=0; i<generic_msgs_len; i++) {
	/* Check the sentinals to insure that the values are ok first */
	if (generic_err_msgs[i].sentinal1 != 0xacebad03 ||
	    generic_err_msgs[i].sentinal2 != 0xcb0bfa11) {
	    /* Something bad has happened! Don't risk trying the
	       short_name pointer; it may have been corrupted */
	    break;
	}
	c = strcmp( generic_err_msgs[i].short_name, msg );
	if (c == 0) return i;
	if (c > 0)
	{
	    /* don't return here if the string partially matches */
	    if (strncmp(generic_err_msgs[i].short_name, msg, strlen(msg)) != 0)
		return -1;
	}
    }
    return -1;
}
/* Here is an alternate search routine based on bisection.
   int i_low, i_mid, i_high, c;
   i_low = 0; i_high = generic_msg_len - 1;
   while (i_high - i_low >= 0) {
       i_mid = (i_high + i_low) / 2;
       c = strcmp( generic_err_msgs[i].short_name, msg );
       if (c == 0) return i_mid;
       if (c < 0) { i_low = i_mid + 1; }
       else       { i_high = i_mid - 1; }
   }
   return -1;
*/
#endif

#if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
/* Given a message string abbreviation (e.g., one that starts "**"), return the corresponding index.  For the specific
 * (parameterized messages), use idx = FindSpecificMsgIndex( "**msg" );
 */
static int FindSpecificMsgIndex( const char *msg )
{
    int i, c;
    for (i=0; i<specific_msgs_len; i++) {
	c = strcmp( specific_err_msgs[i].short_name, msg );
	if (c == 0) return i;
	if (c > 0)
	{
	    /* don't return here if the string partially matches */
	    if (strncmp(specific_err_msgs[i].short_name, msg, strlen(msg)) != 0)
		return -1;
	}
    }
    return -1;
}
#endif

int MPIR_Err_is_fatal(int errcode)
{
    return (errcode & ERROR_FATAL_MASK) ? TRUE : FALSE;
}

#if 0
char * simplify_fmt_string(const char *str)
{
    char *result;
    char *p;

    result = MPIU_Strdup(str);

    /* communicator */
    p = strstr(result, "%C");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%C");
    }

    /* info */
    p = strstr(result, "%I");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%I");
    }

    /* datatype */
    p = strstr(result, "%D");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%D");
    }

    /* file */
    p = strstr(result, "%F");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%F");
    }

    /* window */
    p = strstr(result, "%W");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%W");
    }

    /* group */
    p = strstr(result, "%G");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%G");
    }

    /* op */
    p = strstr(result, "%O");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%O");
    }

    /* request */
    p = strstr(result, "%R");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%R");
    }

    /* errhandler */
    p = strstr(result, "%E");
    while (p)
    {
	p++;
	*p = 'd';
	p = strstr(p, "%E");
    }

    return result;
}
#endif

#define ASSERT_STR_MAXLEN 256

static const char * GetAssertString(int d)
{
    static char str[ASSERT_STR_MAXLEN] = "";
    char *cur;
    size_t len = ASSERT_STR_MAXLEN;
    size_t n;

    if (d == 0)
    {
	MPIU_Strncpy(str, "assert=0", ASSERT_STR_MAXLEN);
	return str;
    }
    cur = str;
    if (d & MPI_MODE_NOSTORE)
    {
	MPIU_Strncpy(cur, "MPI_MODE_NOSTORE", len);
	n = strlen(cur);
	cur += n;
	len -= n;
	d ^= MPI_MODE_NOSTORE;
    }
    if (d & MPI_MODE_NOCHECK)
    {
	if (len < ASSERT_STR_MAXLEN)
	    MPIU_Strncpy(cur, " | MPI_MODE_NOCHECK", len);
	else
	    MPIU_Strncpy(cur, "MPI_MODE_NOCHECK", len);
	n = strlen(cur);
	cur += n;
	len -= n;
	d ^= MPI_MODE_NOCHECK;
    }
    if (d & MPI_MODE_NOPUT)
    {
	if (len < ASSERT_STR_MAXLEN)
	    MPIU_Strncpy(cur, " | MPI_MODE_NOPUT", len);
	else
	    MPIU_Strncpy(cur, "MPI_MODE_NOPUT", len);
	n = strlen(cur);
	cur += n;
	len -= n;
	d ^= MPI_MODE_NOPUT;
    }
    if (d & MPI_MODE_NOPRECEDE)
    {
	if (len < ASSERT_STR_MAXLEN)
	    MPIU_Strncpy(cur, " | MPI_MODE_NOPRECEDE", len);
	else
	    MPIU_Strncpy(cur, "MPI_MODE_NOPRECEDE", len);
	n = strlen(cur);
	cur += n;
	len -= n;
	d ^= MPI_MODE_NOPRECEDE;
    }
    if (d & MPI_MODE_NOSUCCEED)
    {
	if (len < ASSERT_STR_MAXLEN)
	    MPIU_Strncpy(cur, " | MPI_MODE_NOSUCCEED", len);
	else
	    MPIU_Strncpy(cur, "MPI_MODE_NOSUCCEED", len);
	n = strlen(cur);
	cur += n;
	len -= n;
	d ^= MPI_MODE_NOSUCCEED;
    }
    if (d)
    {
	if (len < ASSERT_STR_MAXLEN)
	    MPIU_Snprintf(cur, len, " | 0x%x", d);
	else
	    MPIU_Snprintf(cur, len, "assert=0x%x", d);
    }
    return str;
}

static const char * GetDTypeString(MPI_Datatype d)
{
    static char default_str[64];
    int num_integers, num_addresses, num_datatypes, combiner = 0;
    char *str;

    if (d == MPI_DATATYPE_NULL)
	return "MPI_DATATYPE_NULL";

    if (d == 0)
    {
	MPIU_Strncpy(default_str, "dtype=0x0", 64);
	return default_str;
    }

    MPID_Type_get_envelope(d, &num_integers, &num_addresses, &num_datatypes, &combiner);
    if (combiner == MPI_COMBINER_NAMED)
    {
	str = MPIDU_Datatype_builtin_to_string(d);
	if (str == NULL)
	{
	    MPIU_Snprintf(default_str, 64, "dtype=0x%08x", d);
	    return default_str;
	}
	return str;
    }
    
    /* default is not thread safe */
    str = MPIDU_Datatype_combiner_to_string(combiner);
    if (str == NULL)
    {
	MPIU_Snprintf(default_str, 64, "dtype=USER<0x%08x>", d);
	return default_str;
    }
    MPIU_Snprintf(default_str, 64, "dtype=USER<%s>", str);
    return default_str;
}

static const char * GetMPIOpString(MPI_Op o)
{
    static char default_str[64];

    switch (o)
    {
    case MPI_OP_NULL:
	return "MPI_OP_NULL";
    case MPI_MAX:
	return "MPI_MAX";
    case MPI_MIN:
	return "MPI_MIN";
    case MPI_SUM:
	return "MPI_SUM";
    case MPI_PROD:
	return "MPI_PROD";
    case MPI_LAND:
	return "MPI_LAND";
    case MPI_BAND:
	return "MPI_BAND";
    case MPI_LOR:
	return "MPI_LOR";
    case MPI_BOR:
	return "MPI_BOR";
    case MPI_LXOR:
	return "MPI_LXOR";
    case MPI_BXOR:
	return "MPI_BXOR";
    case MPI_MINLOC:
	return "MPI_MINLOC";
    case MPI_MAXLOC:
	return "MPI_MAXLOC";
    case MPI_REPLACE:
	return "MPI_REPLACE";
    }
    /* default is not thread safe */
    MPIU_Snprintf(default_str, 64, "op=0x%x", o);
    return default_str;
}

static int vsnprintf_mpi(char *str, size_t maxlen, const char *fmt_orig, va_list list)
{
    char *begin, *end, *fmt;
    size_t len;
    MPI_Comm C;
    MPI_Info I;
    MPI_Datatype D;
/*    MPI_File F;*/
    MPI_Win W;
    MPI_Group G;
    MPI_Op O;
    MPI_Request R;
    MPI_Errhandler E;
    char *s;
    int t, i, d, mpi_errno=MPI_SUCCESS;
    void *p;

    fmt = MPIU_Strdup(fmt_orig);
    if (fmt == NULL)
    {
	if (maxlen > 0 && str != NULL)
	    *str = '\0';
	return 0;
    }

    begin = fmt;
    end = strchr(fmt, '%');
    while (end)
    {
	len = maxlen;
	if (len > (size_t)(end - begin)) {
	    len = (size_t)(end - begin);
	}
	if (len)
	{
	    memcpy(str, begin, len);
	    str += len;
	    maxlen -= len;
	}
	end++;
	begin = end+1;
	switch ((int)(*end))
	{
	case (int)'s':
	    s = va_arg(list, char *);
	    MPIU_Strncpy(str, s, maxlen);
	    break;
	case (int)'d':
	    d = va_arg(list, int);
	    MPIU_Snprintf(str, maxlen, "%d", d);
	    break;
	case (int)'i':
	    i = va_arg(list, int);
	    switch (i)
	    {
	    case MPI_ANY_SOURCE:
		MPIU_Strncpy(str, "MPI_ANY_SOURCE", maxlen);
		break;
	    case MPI_PROC_NULL:
		MPIU_Strncpy(str, "MPI_PROC_NULL", maxlen);
		break;
	    case MPI_ROOT:
		MPIU_Strncpy(str, "MPI_ROOT", maxlen);
		break;
	    case MPI_UNDEFINED_RANK:
		MPIU_Strncpy(str, "MPI_UNDEFINED_RANK", maxlen);
		break;
	    default:
		MPIU_Snprintf(str, maxlen, "%d", i);
		break;
	    }
	    break;
	case (int)'t':
	    t = va_arg(list, int);
	    switch (t)
	    {
	    case MPI_ANY_TAG:
		MPIU_Strncpy(str, "MPI_ANY_TAG", maxlen);
		break;
	    case MPI_UNDEFINED:
		MPIU_Strncpy(str, "MPI_UNDEFINED", maxlen);
		break;
	    default:
		MPIU_Snprintf(str, maxlen, "%d", t);
		break;
	    }
	    break;
	case (int)'p':
	    p = va_arg(list, void *);
	    if (p == MPI_IN_PLACE)
	    {
		MPIU_Strncpy(str, "MPI_IN_PLACE", maxlen);
	    }
	    else
	    {
#ifdef HAVE_WINDOWS_H
		MPIU_Snprintf(str, maxlen, "0x%p", p);
#else
		MPIU_Snprintf(str, maxlen, "%p", p);
#endif
	    }
	    break;
	case (int)'C':
	    C = va_arg(list, MPI_Comm);
	    switch (C)
	    {
	    case MPI_COMM_WORLD:
		MPIU_Strncpy(str, "MPI_COMM_WORLD", maxlen);
		break;
	    case MPI_COMM_SELF:
		MPIU_Strncpy(str, "MPI_COMM_SELF", maxlen);
		break;
	    case MPI_COMM_NULL:
		MPIU_Strncpy(str, "MPI_COMM_NULL", maxlen);
		break;
	    default:
		MPIU_Snprintf(str, maxlen, "comm=0x%x", C);
		break;
	    }
	    break;
	case (int)'I':
	    I = va_arg(list, MPI_Info);
	    if (I == MPI_INFO_NULL)
	    {
		MPIU_Strncpy(str, "MPI_INFO_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "info=0x%x", I);
	    }
	    break;
	case (int)'D':
	    D = va_arg(list, MPI_Datatype);
	    MPIU_Snprintf(str, maxlen, "%s", GetDTypeString(D));
	    break;
#if 0
	case (int)'F':
	    F = va_arg(list, MPI_File);
	    if (F == MPI_FILE_NULL)
	    {
		MPIU_Strncpy(str, "MPI_FILE_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "file=0x%x", (unsigned long)F);
	    }
	    break;
#endif
	case (int)'W':
	    W = va_arg(list, MPI_Win);
	    if (W == MPI_WIN_NULL)
	    {
		MPIU_Strncpy(str, "MPI_WIN_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "win=0x%x", W);
	    }
	    break;
	case (int)'A':
	    d = va_arg(list, int);
	    MPIU_Snprintf(str, maxlen, "%s", GetAssertString(d));
	    break;
	case (int)'G':
	    G = va_arg(list, MPI_Group);
	    if (G == MPI_GROUP_NULL)
	    {
		MPIU_Strncpy(str, "MPI_GROUP_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "group=0x%x", G);
	    }
	    break;
	case (int)'O':
	    O = va_arg(list, MPI_Op);
	    MPIU_Snprintf(str, maxlen, "%s", GetMPIOpString(O));
	    break;
	case (int)'R':
	    R = va_arg(list, MPI_Request);
	    if (R == MPI_REQUEST_NULL)
	    {
		MPIU_Strncpy(str, "MPI_REQUEST_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "req=0x%x", R);
	    }
	    break;
	case (int)'E':
	    E = va_arg(list, MPI_Errhandler);
	    if (E == MPI_ERRHANDLER_NULL)
	    {
		MPIU_Strncpy(str, "MPI_ERRHANDLER_NULL", maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "errh=0x%x", E);
	    }
	    break;
	default:
	    /* Error: unhandled output type */
	    return 0;
	    /*
	    if (maxlen > 0 && str != NULL)
		*str = '\0';
	    break;
	    */
	}
	len = strlen(str);
	maxlen -= len;
	str += len;
	end = strchr(begin, '%');
    }
    if (*begin != '\0')
    {
	MPIU_Strncpy(str, begin, maxlen);
    }
    /* Free the dup'ed format string */
    MPIU_Free( fmt );

    return mpi_errno;
}

/* Err_create_code is just a shell that accesses the va_list and then
   calls the real routine.  */
int MPIR_Err_create_code( int lastcode, int fatal, const char fcname[], 
			  int line, int error_class, const char generic_msg[],
			  const char specific_msg[], ... )
{
    int rc;
    va_list Argp;
    va_start(Argp, specific_msg);
    rc = MPIR_Err_create_code_valist( lastcode, fatal, fcname, line,
				      error_class, generic_msg, specific_msg,
				      Argp );
    va_end(Argp);
    return rc;
}

/*
 * This is the real routine for generating an error code.  It takes
 * a va_list so that it can be called by any routine that accepts a 
 * variable number of arguments.
 */
int MPIR_Err_create_code_valist( int lastcode, int fatal, const char fcname[], 
				 int line, int error_class, 
				 const char generic_msg[],
				 const char specific_msg[], va_list Argp )
{
    int err_code;
    int generic_idx;
    int use_user_error_code = 0;
    /* Create the code from the class and the message ring index */

    if (MPIR_Err_abort_on_error)
    {
	/*printf("aborting from %s, line %d\n", fcname, line);fflush(stdout);*/
	abort();
    }

    /* va_start(Argp, specific_msg); */

    if (error_class == MPI_ERR_OTHER)
    {
        if (MPIR_ERR_GET_CLASS(lastcode) > MPI_SUCCESS && MPIR_ERR_GET_CLASS(lastcode) <= MPICH_ERR_LAST_CLASS)
	{
	    /* If the last class is more specific (and is valid), then pass it through */
	    error_class = MPIR_ERR_GET_CLASS(lastcode);
	}
	else
	{
	    error_class = MPI_ERR_OTHER;
	}
    }

    /* Handle special case of MPI_ERR_IN_STATUS.  According to the standard,
       the code must be equal to the class. See section 3.7.5.  
       Information on the particular error is in the MPI_ERROR field 
       of the status. */
    if (error_class == MPI_ERR_IN_STATUS)
    {
	return MPI_ERR_IN_STATUS;
    }

    err_code = error_class;

    /* Handle the generic message.  This selects a subclass, based on a text string */
#   if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_CLASS
    {
	generic_idx = FindGenericMsgIndex(generic_msg);
	if (generic_idx >= 0)
	{
	    if (strcmp( generic_err_msgs[generic_idx].short_name, "**user" ) == 0)
	    {
		use_user_error_code = 1;
	    }
	    err_code |= (generic_idx + 1) << ERROR_GENERIC_SHIFT;
	}
	else
	{
	    /* TODO: lookup index for class error message */
	    err_code &= ~ERROR_GENERIC_MASK;
	    
#           ifdef MPICH_DBG_OUTPUT
	    {
		if (generic_msg[0] == '*' && generic_msg[1] == '*')
		{
		    /* FIXME : Internal error.  Generate some debugging information; Fix for the general release */
		    fprintf( stderr, "Could not find %s in list of messages\n", generic_msg );
		}
	    }
#           endif
	}
    }
#   endif

    /* Handle the instance-specific part of the error message */
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	int specific_idx;
	const char * specific_fmt = 0;
	int  ring_idx, ring_seq=0;
	char * ring_msg;
	int i;
	
	error_ring_mutex_lock();
	{
	    ring_idx = error_ring_loc++;
	    if (error_ring_loc >= MAX_ERROR_RING) error_ring_loc %= MAX_ERROR_RING;
	
	    ring_msg = ErrorRing[ring_idx].msg;

	    if (specific_msg != NULL)
	    {
		specific_idx = FindSpecificMsgIndex(specific_msg);
		if (specific_idx >= 0)
		{
		    specific_fmt = specific_err_msgs[specific_idx].long_name;
		}
		else
		{
		    specific_fmt = specific_msg;
		}

		vsnprintf_mpi( ring_msg, MPI_MAX_ERROR_STRING, specific_fmt, Argp );
#if 0
		specific_fmt = simplify_fmt_string(specific_fmt);
#               ifdef HAVE_VSNPRINTF
		{
		    vsnprintf( ring_msg, MPI_MAX_ERROR_STRING, specific_fmt, Argp );
		}
#               elif defined(HAVE_VSPRINTF)
		{
		    vsprintf( ring_msg, specific_fmt, Argp );
		}
#               else
		{
		    /* For now, just punt */
		    if (generic_idx >= 0)
		    {
			MPIU_Strncpy( ring_msg, generic_err_msgs[generic_idx].long_name, MPI_MAX_ERROR_STRING );
		    }
		    else
		    {
			MPIU_Strncpy( ring_msg, generic_msg, MPI_MAX_ERROR_STRING );
		    }
		}
#               endif
		MPIU_Free(specific_fmt);
#endif
	    }
	    else if (generic_idx >= 0)
	    {
		MPIU_Strncpy( ring_msg, generic_err_msgs[generic_idx].long_name, MPI_MAX_ERROR_STRING );
	    }
	    else
	    {
		MPIU_Strncpy( ring_msg, generic_msg, MPI_MAX_ERROR_STRING );
	    }

	    ring_msg[MPI_MAX_ERROR_STRING] = '\0';
	
	    /* Create a simple hash function of the message to serve as the sequence number */
	    ring_seq = 0;
	    for (i=0; ring_msg[i]; i++)
	    {
		ring_seq += (unsigned int) ring_msg[i];
	    }
	    ring_seq %= ERROR_SPECIFIC_SEQ_SIZE;
	    
	    ErrorRing[ring_idx].id = error_class & ERROR_CLASS_MASK;
	    ErrorRing[ring_idx].id |= (generic_idx + 1) << ERROR_GENERIC_SHIFT;
	    ErrorRing[ring_idx].id |= ring_seq << ERROR_SPECIFIC_SEQ_SHIFT;
	    ErrorRing[ring_idx].prev_error = lastcode;
	    if (use_user_error_code)
	    {
		ErrorRing[ring_idx].use_user_error_code = 1;
		ErrorRing[ring_idx].user_error_code = va_arg(Argp, int);
	    }
	    else if (lastcode != MPI_SUCCESS)
	    {
		int last_ring_idx;
		int last_ring_id;
		int last_generic_idx;

		if (convertErrcodeToIndexes( lastcode, &last_ring_idx, 
					     &last_ring_id,
					     &last_generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  lastcode, last_ring_idx );
		}
		else {
		    if (last_generic_idx >= 0 && 
			ErrorRing[last_ring_idx].id == last_ring_id) {
			if (ErrorRing[last_ring_idx].use_user_error_code) {
			    ErrorRing[ring_idx].use_user_error_code = 1;
			    ErrorRing[ring_idx].user_error_code = 
				ErrorRing[last_ring_idx].user_error_code;
			}
		    }
		}
	    }

	    if (fcname != NULL)
	    {
		MPIU_Snprintf(ErrorRing[ring_idx].fcname, MAX_FCNAME_LEN, "%s(%d)", fcname, line);
		ErrorRing[ring_idx].fcname[MAX_FCNAME_LEN] = '\0';
	    }
	    else
	    {
		ErrorRing[ring_idx].fcname[0] = '\0';
	    }
	}
	error_ring_mutex_unlock();
	
	err_code |= ring_idx << ERROR_SPECIFIC_INDEX_SHIFT;
	err_code |= ring_seq << ERROR_SPECIFIC_SEQ_SHIFT;

    }
#   endif

    if (fatal || MPIR_Err_is_fatal(lastcode))
    {
	err_code |= ERROR_FATAL_MASK;
    }
    
    /* va_end( Argp ); */

    return err_code;
}

/*
 * Accessor routines for the predefined messages.  These can be
 * used by the other routines (such as MPI_Error_string) to
 * access the messages in this file, or the messages that may be
 * available through any message catalog facility 
 */
static const char *get_class_msg( int error_class )
{
#if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
    if (error_class >= 0 && error_class < MPIR_MAX_ERROR_CLASS_INDEX) {
	return generic_err_msgs[class_to_index[error_class]].long_name;
    }
    else {
	return "Unknown error class";
    }
#else 
    return "Error message texts are not available";
#endif
}

void MPIR_Err_get_string( int errorcode, char * msg, int length, 
			  MPIR_Err_get_class_string_func_t fn )
{
    int error_class;
    int len, num_remaining = length;
    
    if (num_remaining == 0)
	num_remaining = MPI_MAX_ERROR_STRING;

    /* Convert the code to a string.  The cases are:
       simple class.  Find the corresponding string.
       <not done>
       if (user code) { go to code that extracts user error messages }
       else {
           is specific message code set and available?  if so, use it
	   else use generic code (lookup index in table of messages)
       }
     */
    if (errorcode & ERROR_DYN_MASK)
    {
	/* This is a dynamically created error code (e.g., with 
	   MPI_Err_add_class) */
	/* If a dynamic error code was created, the function to convert
	   them into strings has been set.  Check to see that it was; this 
	   is a safeguard against a bogus error code */
	if (!MPIR_Process.errcode_to_string)
	{
	    /* --BEGIN ERROR HANDLING-- */
	    if (MPIU_Strncpy(msg, "Undefined dynamic error code", num_remaining))
	    {
		msg[num_remaining - 1] = '\0';
	    }
	    /* --END ERROR HANDLING-- */
	}
	else
	{
	    if (MPIU_Strncpy(msg, MPIR_Process.errcode_to_string( errorcode ), num_remaining))
	    {
		msg[num_remaining - 1] = '\0';
	    }
	}
    }
    else if ( (errorcode & ERROR_CLASS_MASK) == errorcode)
    {
	error_class = MPIR_ERR_GET_CLASS(errorcode);

	if (fn != NULL && error_class > MPICH_ERR_LAST_CLASS /*&& error_class < MPICH_ERR_MAX_EXT_CLASS*/)
	{
	    fn(errorcode, msg, length);
	}
	else
	{
	    if (MPIU_Strncpy(msg, get_class_msg( errorcode ), num_remaining))
	    {
		msg[num_remaining - 1] = '\0';
	    }
	}
    }
    else
    {
	/* print the class message first */
	error_class = MPIR_ERR_GET_CLASS(errorcode);

	if (fn != NULL && error_class > MPICH_ERR_LAST_CLASS /*&& error_class < MPICH_ERR_MAX_EXT_CLASS*/)
	{
	    fn(errorcode, msg, num_remaining);
	}
	else
	{
	    MPIU_Strncpy(msg, get_class_msg(ERROR_GET_CLASS(errorcode)), num_remaining);
	}
	msg[num_remaining - 1] = '\0';
	len = (int)strlen(msg);
	msg += len;
	num_remaining -= len;


	/* then print the stack or the last specific error message */

	if (MPIR_Err_print_stack_flag)
	{
	    MPIU_Strncpy(msg, ", error stack:\n", num_remaining);
	    msg[num_remaining - 1] = '\0';
	    len = (int)strlen(msg);
	    msg += len;
	    num_remaining -= len;
	    MPIR_Err_print_stack_string_ext(errorcode, msg, num_remaining, fn);
	    msg[num_remaining - 1] = '\0';
	}
	else
	{
#           if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
	    {
		error_ring_mutex_lock();
		{
		    while (errorcode != MPI_SUCCESS)
		    {
			int ring_idx;
			int ring_id;
			int generic_idx;

			if (convertErrcodeToIndexes( errorcode, &ring_idx, 
						     &ring_id,
						     &generic_idx ) != 0) {
			    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errorcode, ring_idx );
			    break;
			}
			
			if (generic_idx < 0)
			{
			    break;
			}

			if (ErrorRing[ring_idx].id == ring_id)
			{
			    /* just keep clobbering old values until the 
			       end of the stack is reached */
			    MPIU_Snprintf(msg, num_remaining, ", %s", ErrorRing[ring_idx].msg);
			    msg[num_remaining - 1] = '\0';
			    errorcode = ErrorRing[ring_idx].prev_error;
			}
			else
			{
			    break;
			}
		    }
		}
		error_ring_mutex_unlock();

		if (errorcode == MPI_SUCCESS)
		{
		    goto fn_exit;
		}
	    }
#           endif

#           if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
	    {
		int generic_idx;

		generic_idx = ((errorcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;

		if (generic_idx >= 0)
		{
		    MPIU_Snprintf(msg, num_remaining, ", %s", generic_err_msgs[generic_idx].long_name);
		    msg[num_remaining - 1] = '\0';
		    goto fn_exit;
		}
	    }
#           endif
	}
    }

fn_exit:
    return;
}


#if 0
void MPIR_Err_get_string_ext(int errorcode, char * msg, int maxlen, 
			     MPIR_Err_get_class_string_func_t fn)
{
    int error_class;
    int ring_idx;
    int ring_seq;
    int generic_idx;

    /* Convert the code to a string.  The cases are:
       simple class.  Find the corresponding string.
       <not done>
       if (user code) { go to code that extracts user error messages }
       else {
           is specific message code set and available?  if so, use it
	   else use generic code (lookup index in table of messages)
       }
     */
    if (errorcode & ERROR_DYN_MASK)
    {
	/* This is a dynamically created error code (e.g., with MPI_Err_add_class) */
	if (!MPIR_Process.errcode_to_string)
	{
	    if (MPIU_Strncpy(msg, "Undefined dynamic error code", MPI_MAX_ERROR_STRING))
	    {
		msg[MPI_MAX_ERROR_STRING - 1] = '\0';
	    }
	    
	}
	else
	{
	    if (MPIU_Strncpy(msg, MPIR_Process.errcode_to_string( errorcode ), MPI_MAX_ERROR_STRING))
	    {
		msg[MPI_MAX_ERROR_STRING - 1] = '\0';
	    }
	}
    }
    else if ( (errorcode & ERROR_CLASS_MASK) == errorcode)
    {
	error_class = MPIR_ERR_GET_CLASS(errorcode);

	if (error_class <= MPICH_ERR_LAST_CLASS)
	{
	    /* code is a raw error class.  Convert the class to an index */
	    if (MPIU_Strncpy(msg, get_class_msg( errorcode ), MPI_MAX_ERROR_STRING))
	    {
		msg[MPI_MAX_ERROR_STRING - 1] = '\0';
	    }
	}
	else
	{
	    fn(errorcode, msg, maxlen);
	}
    }
    else
    {
	/* error code encodes a message.  Find it and make sure that
	   it is still valid (seq number matches the stored value in the
	   error message ring).  If the seq number is *not* valid,
	   use the generic message.
	 */
	ring_idx    = (errorcode & ERROR_SPECIFIC_INDEX_MASK) >> ERROR_SPECIFIC_INDEX_SHIFT;
	ring_seq    = (errorcode & ERROR_SPECIFIC_SEQ_MASK) >> ERROR_SPECIFIC_SEQ_SHIFT;
	generic_idx = ((errorcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;

#       if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
	{
	    if (generic_idx >= 0)
	    {
		int flag = FALSE;

		error_ring_mutex_lock();
		{
		    int ring_id;

		    ring_id = errorcode & (ERROR_CLASS_MASK | ERROR_GENERIC_MASK | ERROR_SPECIFIC_SEQ_MASK);

		    if (ErrorRing[ring_idx].id == ring_id)
		    {
			if (MPIU_Strncpy(msg, ErrorRing[ring_idx].msg, MPI_MAX_ERROR_STRING))
			{
			    msg[MPI_MAX_ERROR_STRING - 1] = '\0';
			}
			flag = TRUE;
		    }
		}
		error_ring_mutex_unlock();

		if (flag)
		{
		    goto fn_exit;
		}
	    }
	}
#       endif
	
#       if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
	{
	    if (generic_idx >= 0)
	    {
		if (MPIU_Strncpy(msg, generic_err_msgs[generic_idx].long_name, MPI_MAX_ERROR_STRING))
		{
		    msg[MPI_MAX_ERROR_STRING - 1] = '\0';
		}
		goto fn_exit;
	    }
	}
#       endif

	error_class = ERROR_GET_CLASS(errorcode);

	if (error_class <= MPICH_ERR_LAST_CLASS)
	{
	    if (MPIU_Strncpy(msg, get_class_msg(error_class), MPI_MAX_ERROR_STRING))
	    {
		msg[MPI_MAX_ERROR_STRING - 1] = '\0';
	    }
	}
	else
	{
	    /*MPIU_Snprintf(msg, MPI_MAX_ERROR_STRING, "Error code contains an invalid class (%d)", error_class);*/
	    fn(errorcode, msg, maxlen);
	}
    }

fn_exit:
    return;
}
#endif

void MPIR_Err_print_stack(FILE * fp, int errcode)
{
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    while (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;

		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		    break;
		}

		if (generic_idx < 0)
		{
		    break;
		}
		    
		if (ErrorRing[ring_idx].id == ring_id)
		{
		    fprintf(fp, "%s: %s\n", ErrorRing[ring_idx].fcname, 
			    ErrorRing[ring_idx].msg);
		    errcode = ErrorRing[ring_idx].prev_error;
		}
		else
		{
		    break;
		}
	    }
	}
	error_ring_mutex_unlock();

	if (errcode == MPI_SUCCESS)
	{
	    goto fn_exit;
	}
    }
#   endif

#   if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
    {
	int generic_idx;
		    
	generic_idx = ((errcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;
	
	if (generic_idx >= 0)
	{
	    fprintf(fp, "(unknown)(): %s\n", generic_err_msgs[generic_idx].long_name);
	    goto fn_exit;
	}
    }
#   endif
    
    {
	int error_class;

	error_class = ERROR_GET_CLASS(errcode);
	
	if (error_class <= MPICH_ERR_LAST_CLASS)
	{
	    fprintf(fp, "(unknown)(): %s\n", get_class_msg(ERROR_GET_CLASS(errcode)));
	}
	else
	{
	    fprintf(fp, "Error code contains an invalid class (%d)\n", error_class);
	}
    }
    
  fn_exit:
    return;
}

void MPIR_Err_print_stack_string(int errcode, char *str, int maxlen)
{
    int len;
    char *str_orig = str;
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    /* Find the longest fcname in the stack */
	    int max_fcname_len = 0;
	    int tmp_errcode = errcode;
	    while (tmp_errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;

		if (convertErrcodeToIndexes( tmp_errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  tmp_errcode, ring_idx );
		    break;
		}

		if (generic_idx < 0)
		{
		    break;
		}
		    
		if (ErrorRing[ring_idx].id == ring_id) {
		    len = (int)strlen(ErrorRing[ring_idx].fcname);
		    max_fcname_len = MPIR_MAX(max_fcname_len, len);
		    tmp_errcode = ErrorRing[ring_idx].prev_error;
		}
		else
		{
		    break;
		}
	    }
	    max_fcname_len += 2; /* add space for the ": " */
	    /*printf("max_fcname_len = %d\n", max_fcname_len);fflush(stdout);*/
	    /* print the error stack */
	    while (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;
		int i;
		char *cur_pos;

		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		    break;
		}

		if (generic_idx < 0)
		{
		    break;
		}
		    
		if (ErrorRing[ring_idx].id == ring_id)
		{
		    MPIU_Snprintf(str, maxlen, "%s", ErrorRing[ring_idx].fcname);
		    len = (int)strlen(str);
		    maxlen -= len;
		    str += len;
		    for (i=0; i<max_fcname_len - (int)strlen(ErrorRing[ring_idx].fcname) - 2; i++)
		    {
			if (MPIU_Snprintf(str, maxlen, "."))
			{
			    maxlen--;
			    str++;
			}
		    }
		    if (MPIU_Snprintf(str, maxlen, ":"))
		    {
			maxlen--;
			str++;
		    }
		    if (MPIU_Snprintf(str, maxlen, " "))
		    {
			maxlen--;
			str++;
		    }

		    if (MPIR_Err_chop_error_stack)
		    {
			cur_pos = ErrorRing[ring_idx].msg;
			len = (int)strlen(cur_pos);
			if (len == 0)
			{
			    if (MPIU_Snprintf(str, maxlen, "\n"))
			    {
				maxlen--;
				str++;
			    }
			}
			while (len)
			{
			    if (len >= MPIR_Err_chop_width - max_fcname_len)
			    {
				if (len > maxlen)
				    break;
				MPIU_Snprintf(str, MPIR_Err_chop_width - 1 - max_fcname_len, "%s", cur_pos);
				str[MPIR_Err_chop_width - 1 - max_fcname_len] = '\n';
				cur_pos += MPIR_Err_chop_width - 1 - max_fcname_len;
				str += MPIR_Err_chop_width - max_fcname_len;
				maxlen -= MPIR_Err_chop_width - max_fcname_len;
				if (maxlen < max_fcname_len)
				    break;
				for (i=0; i<max_fcname_len; i++)
				{
				    MPIU_Snprintf(str, maxlen, " ");
				    maxlen--;
				    str++;
				}
				len = (int)strlen(cur_pos);
			    }
			    else
			    {
				MPIU_Snprintf(str, maxlen, "%s\n", cur_pos);
				len = (int)strlen(str);
				maxlen -= len;
				str += len;
				len = 0;
			    }
			}
		    }
		    else
		    {
			MPIU_Snprintf(str, maxlen, "%s\n", ErrorRing[ring_idx].msg);
			len = (int)strlen(str);
			maxlen -= len;
			str += len;
		    }
		    /*
		    MPIU_Snprintf(str, maxlen, "%s: %s\n", ErrorRing[ring_idx].fcname, ErrorRing[ring_idx].msg);
		    len = (int)strlen(str);
		    maxlen -= len;
		    str += len;
		    */
		    errcode = ErrorRing[ring_idx].prev_error;
		}
		else
		{
		    break;
		}
	    }
	}
	error_ring_mutex_unlock();

	if (errcode == MPI_SUCCESS)
	{
	    goto fn_exit;
	}
    }
#   endif

#   if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
    {
	int generic_idx;
		    
	generic_idx = ((errcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;
	
	if (generic_idx >= 0)
	{
	    MPIU_Snprintf(str, maxlen, "(unknown)(): %s\n", generic_err_msgs[generic_idx].long_name);
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	    goto fn_exit;
	}
    }
#   endif
    
    {
	int error_class;

	error_class = ERROR_GET_CLASS(errcode);
	
	if (error_class <= MPICH_ERR_LAST_CLASS)
	{
	    MPIU_Snprintf(str, maxlen, "(unknown)(): %s\n", get_class_msg(ERROR_GET_CLASS(errcode)));
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	}
	else
	{
	    MPIU_Snprintf(str, maxlen, "Error code contains an invalid class (%d)\n", error_class);
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	}
    }
    
  fn_exit:
    if (str_orig != str)
    {
	str--;
	*str = '\0'; /* erase the last \n */
    }
    return;
}

void MPIR_Err_print_stack_string_ext(int errcode, char *str, int maxlen, 
				     MPIR_Err_get_class_string_func_t fn)
{
    char *str_orig = str;
    int len;
#   if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
    {
	error_ring_mutex_lock();
	{
	    /* Find the longest fcname in the stack */
	    int max_fcname_len = 0;
	    int tmp_errcode = errcode;
	    while (tmp_errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;

		if (convertErrcodeToIndexes( tmp_errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		    break;
		}

		if (generic_idx < 0)
		{
		    break;
		}
		    
		if (ErrorRing[ring_idx].id == ring_id) {
		    len = (int)strlen(ErrorRing[ring_idx].fcname);
		    max_fcname_len = MPIR_MAX(max_fcname_len, len);
		    tmp_errcode = ErrorRing[ring_idx].prev_error;
		}
		else
		{
		    break;
		}
	    }
	    max_fcname_len += 2; /* add space for the ": " */
	    /*printf("max_fcname_len = %d\n", max_fcname_len);fflush(stdout);*/
	    /* print the error stack */
	    while (errcode != MPI_SUCCESS)
	    {
		int ring_idx;
		int ring_id;
		int generic_idx;
		int i;
		char *cur_pos;

		if (convertErrcodeToIndexes( errcode, &ring_idx, &ring_id,
					     &generic_idx ) != 0) {
		    MPIU_Error_printf( 
		  "Invalid error code (%d) (error ring index %d invalid)\n", 
		  errcode, ring_idx );
		}

		if (generic_idx < 0)
		{
		    break;
		}
		    
		if (ErrorRing[ring_idx].id == ring_id)
		{
		    MPIU_Snprintf(str, maxlen, "%s", ErrorRing[ring_idx].fcname);
		    len = (int)strlen(str);
		    maxlen -= len;
		    str += len;
		    for (i=0; i<max_fcname_len - (int)strlen(ErrorRing[ring_idx].fcname) - 2; i++)
		    {
			if (MPIU_Snprintf(str, maxlen, "."))
			{
			    maxlen--;
			    str++;
			}
		    }
		    if (MPIU_Snprintf(str, maxlen, ":"))
		    {
			maxlen--;
			str++;
		    }
		    if (MPIU_Snprintf(str, maxlen, " "))
		    {
			maxlen--;
			str++;
		    }

		    if (MPIR_Err_chop_error_stack)
		    {
			cur_pos = ErrorRing[ring_idx].msg;
			len = (int)strlen(cur_pos);
			if (len == 0)
			{
			    if (MPIU_Snprintf(str, maxlen, "\n"))
			    {
				maxlen--;
				str++;
			    }
			}
			while (len)
			{
			    if (len >= MPIR_Err_chop_width - max_fcname_len)
			    {
				if (len > maxlen)
				    break;
				MPIU_Snprintf(str, MPIR_Err_chop_width - 1 - max_fcname_len, "%s", cur_pos);
				str[MPIR_Err_chop_width - 1 - max_fcname_len] = '\n';
				cur_pos += MPIR_Err_chop_width - 1 - max_fcname_len;
				str += MPIR_Err_chop_width - max_fcname_len;
				maxlen -= MPIR_Err_chop_width - max_fcname_len;
				if (maxlen < max_fcname_len)
				    break;
				for (i=0; i<max_fcname_len; i++)
				{
				    MPIU_Snprintf(str, maxlen, " ");
				    maxlen--;
				    str++;
				}
				len = (int)strlen(cur_pos);
			    }
			    else
			    {
				MPIU_Snprintf(str, maxlen, "%s\n", cur_pos);
				len = (int)strlen(str);
				maxlen -= len;
				str += len;
				len = 0;
			    }
			}
		    }
		    else
		    {
			MPIU_Snprintf(str, maxlen, "%s\n", ErrorRing[ring_idx].msg);
			len = (int)strlen(str);
			maxlen -= len;
			str += len;
		    }
		    /*
		    MPIU_Snprintf(str, maxlen, "%s: %s\n", ErrorRing[ring_idx].fcname, ErrorRing[ring_idx].msg);
		    len = (int)strlen(str);
		    maxlen -= len;
		    str += len;
		    */
		    errcode = ErrorRing[ring_idx].prev_error;
		}
		else
		{
		    break;
		}
	    }
	}
	error_ring_mutex_unlock();

	if (errcode == MPI_SUCCESS)
	{
	    goto fn_exit;
	}
    }
#   endif

#   if MPICH_ERROR_MSG_LEVEL > MPICH_ERROR_MSG_NONE
    {
	int generic_idx;
		    
	generic_idx = ((errcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;
	
	if (generic_idx >= 0)
	{
	    const char *p;
	    /* FIXME: (Here and elsewhere)  Make sure any string is
	       non-null before you use it */
	    p = generic_err_msgs[generic_idx].long_name;
	    if (!p) { p = "<NULL>"; }
	    MPIU_Snprintf(str, maxlen, "(unknown)(): %s\n", p );
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	    goto fn_exit;
	}
    }
#   endif
    
    {
	int error_class;

	error_class = ERROR_GET_CLASS(errcode);
	
	if (error_class <= MPICH_ERR_LAST_CLASS)
	{
	    MPIU_Snprintf(str, maxlen, "(unknown)(): %s\n", get_class_msg(ERROR_GET_CLASS(errcode)));
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	}
	else
	{
	    if (fn != NULL)
	    {
		fn(errcode, str, maxlen);
	    }
	    else
	    {
		MPIU_Snprintf(str, maxlen, "Error code contains an invalid class (%d)\n", error_class);
	    }
	    len = (int)strlen(str);
	    maxlen -= len;
	    str += len;
	}
    }
    
  fn_exit:
    if (str_orig != str)
    {
	str--;
	*str = '\0';
    }
    return;
}

#if MPICH_ERROR_MSG_LEVEL >= MPICH_ERROR_MSG_ALL
/* Convert an error code into ring_idx, ring_id, and generic_idx.
   Return non-zero if there is a problem with the decode values
   (e.g., out of range for the ring index) */
static int convertErrcodeToIndexes( int errcode, int *ring_idx, int *ring_id,
				    int *generic_idx )
{
    *ring_idx = (errcode & ERROR_SPECIFIC_INDEX_MASK) >> 
	ERROR_SPECIFIC_INDEX_SHIFT;
    *ring_id = errcode & (ERROR_CLASS_MASK | 
			  ERROR_GENERIC_MASK | ERROR_SPECIFIC_SEQ_MASK);
    *generic_idx = ((errcode & ERROR_GENERIC_MASK) >> ERROR_GENERIC_SHIFT) - 1;
    
    if (*ring_idx < 0 || *ring_idx >= MAX_ERROR_RING) return 1;

    return 0;
}
#endif

/* 
   Nesting level for routines.
   Note that since these use per-thread data, no locks or atomic update
   routines are required.

   In a single-threaded environment, These are replaced with
   MPIR_Thread.nest_count ++, --.  These are defined in the mpiimpl.h file.
 */
#if (MPICH_THREAD_LEVEL >= MPI_THREAD_MULTIPLE)
void MPIR_Nest_incr( void )
{
    MPICH_PerThread_t *p;
    MPIR_GetPerThread(&p);
    p->nest_count++;
}
void MPIR_Nest_decr( void )
{
    MPICH_PerThread_t *p;
    MPIR_GetPerThread(&p);
    p->nest_count--;
}
int MPIR_Nest_value()
{
    MPICH_PerThread_t *p;
    MPIR_GetPerThread(&p);
    return p->nest_count;
}
#endif

/*
 * Error handlers.  These are handled just like the other opaque objects
 * in MPICH
 */

#ifndef MPID_ERRHANDLER_PREALLOC 
#define MPID_ERRHANDLER_PREALLOC 8
#endif

/* Preallocated errorhandler objects */
MPID_Errhandler MPID_Errhandler_builtin[2] = 
          { { MPI_ERRORS_ARE_FATAL, 0},
	    { MPI_ERRORS_RETURN, 0} }; 
MPID_Errhandler MPID_Errhandler_direct[MPID_ERRHANDLER_PREALLOC] = { {0} };
MPIU_Object_alloc_t MPID_Errhandler_mem = { 0, 0, 0, 0, MPID_ERRHANDLER, 
					    sizeof(MPID_Errhandler), 
					    MPID_Errhandler_direct,
					    MPID_ERRHANDLER_PREALLOC, };

void MPID_Errhandler_free(MPID_Errhandler *errhan_ptr)
{
    MPIU_Handle_obj_free(&MPID_Errhandler_mem, errhan_ptr);
}

#ifdef HAVE_CXX_BINDING
void MPIR_Errhandler_set_cxx( MPI_Errhandler errhand, void (*errcall)(void) )
{
    MPID_Errhandler *errhand_ptr;
    
    MPID_Errhandler_get_ptr( errhand, errhand_ptr );
    errhand_ptr->language		= MPID_LANG_CXX;
    MPIR_Process.cxx_call_errfn	= (void (*)( int, int *, int *, 
					    void (*)(void) ))errcall;
}
#endif
