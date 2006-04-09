/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef MPIERROR_H_INCLUDED
#define MPIERROR_H_INCLUDED

/* Error severity */
#define MPIR_ERR_FATAL 1
#define MPIR_ERR_RECOVERABLE 0

struct MPID_Comm;
struct MPID_Win;
/*struct MPID_File;*/

/* Bindings for internal routines */
int MPIR_Err_return_comm( struct MPID_Comm *, const char [], int );
int MPIR_Err_return_win( struct MPID_Win *, const char [], int );
/*int MPIR_Err_return_file( struct MPID_File *, const char [], int );*/
int MPIR_Err_return_file( MPI_File, const char [], int ); /* Romio version */
/* FIXME:
 * The following description is out of date and should not be used
 */
/*@
  MPIR_Err_create_code - Create an error code and associated message
  to report an error

  Input Parameters:
+ class - Error class
. generic_msg - A generic message to be used if not instance-specific
 message is available
. instance_msg - A message containing printf-style formatting commands
  that, when combined with the instance_parameters, specify an error
  message containing instance-specific data.
- instance_parameters - The remaining parameters.  These must match
 the formatting commands in 'instance_msg'.

 Notes:
 A typical use is\:
.vb
   mpi_errno = MPIR_Err_create_code( MPI_ERR_RANK, "Invalid Rank",
                                "Invalid rank %d", rank );
.ve
 
  Predefined message may also be used.  Any message that uses the
  prefix '"**"' will be looked up in a table.  This allows standardized 
  messages to be used for a message that is used in several different locations
  in the code.  For example, the name '"**rank"' might be used instead of
  '"Invalid Rank"'; this would also allow the message to be made more
  specific and useful, such as 
.vb
   Invalid rank provided.  The rank must be between 0 and the 1 less than
   the size of the communicator in this call.
.ve
  
  This interface is compatible with the 'gettext' interface for 
  internationalization, in the sense that the 'generic_msg' and 'instance_msg' 
  may be used as arguments to 'gettext' to return a string in the appropriate 
  language; the implementation of 'MPID_Err_create_code' can then convert
  this text into the appropriate code value.

  Module:
  Error

  @*/
int MPIR_Err_create_code( int, int, const char [], int, int, const char [], const char [], ... );

#ifdef USE_ERR_CODE_VALIST
int MPIR_Err_create_code_valist( int, int, const char [], int, int, const char [], const char [], va_list );
#endif

int MPIR_Err_is_fatal(int);
void MPIR_Err_init(void);
void MPIR_Err_preinit( void );
/*@
  MPID_Err_get_string - Get the message string that corresponds to an error
  class or code

  Input Parameter:
+ code - An error class or code.  If a code, it must have been created by 
  'MPID_Err_create_code'.
- msg_len - Length of 'msg'.

  Output Parameter:
. msg - A null-terminated text string of length (including the null) of no
  more than 'msg_len'.  

  Return value:
  Zero on success.  Non-zero returns indicate either (a) 'msg_len' is too
  small for the message or (b) the value of 'code' is neither a valid 
  error class or code.

  Notes:
  This routine is used to implement 'MPI_ERROR_STRING'.

  Module:
  Error 

  Question:
  What values should be used for the error returns?  Should they be
  valid error codes?

  How do we get a good value for 'MPI_MAX_ERROR_STRING' for 'mpi.h'?
  See 'errgetmsg' for one idea.

  @*/
typedef int (* MPIR_Err_get_class_string_func_t)(int error, char *str, int length);
void MPIR_Err_get_string( int, char *, int, MPIR_Err_get_class_string_func_t );
void MPIR_Err_print_stack(FILE *, int);
extern int MPIR_Err_print_stack_flag;
void MPIR_Err_print_stack_string(int errcode, char *str, int maxlen);
void MPIR_Err_print_stack_string_ext(int errcode, char *str, int maxlen, MPIR_Err_get_class_string_func_t fn);

int MPIR_Err_set_msg( int code, const char *msg_string );

#define MPIR_ERR_CLASS_MASK 0x0000007f
#define MPIR_ERR_CLASS_SIZE 128
#define MPIR_ERR_GET_CLASS(mpi_errno_) (mpi_errno_ & MPIR_ERR_CLASS_MASK)

#endif
