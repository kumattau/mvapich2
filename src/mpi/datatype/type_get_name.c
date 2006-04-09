/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "datatype.h"

/* -- Begin Profiling Symbol Block for routine MPI_Type_get_name */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Type_get_name = PMPI_Type_get_name
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Type_get_name  MPI_Type_get_name
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Type_get_name as PMPI_Type_get_name
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#define MPI_Type_get_name PMPI_Type_get_name

#endif

/* This routine initializes all of the name fields in the predefined 
   datatypes */
#ifndef MPICH_MPI_FROM_PMPI
/* Include these definitions only with the PMPI version */
typedef struct mpi_names_t { MPI_Datatype dtype; const char *name; } mpi_names_t;
/* The MPI standard specifies that the names must be the MPI names,
   not the related language names (e.g., MPI_CHAR, not char) */
static mpi_names_t mpi_names[] = {
    { MPI_CHAR, "MPI_CHAR" },
    { MPI_SIGNED_CHAR, "MPI_SIGNED_CHAR" },
    { MPI_UNSIGNED_CHAR, "MPI_UNSIGNED_CHAR" },
    { MPI_BYTE, "MPI_BYTE" },
    { MPI_WCHAR, "MPI_WCHAR" },
    { MPI_SHORT, "MPI_SHORT" },
    { MPI_UNSIGNED_SHORT, "MPI_UNSIGNED_SHORT" },
    { MPI_INT, "MPI_INT" },
    { MPI_UNSIGNED, "MPI_UNSIGNED" },
    { MPI_LONG, "MPI_LONG" },
    { MPI_UNSIGNED_LONG, "MPI_UNSIGNED_LONG" },
    { MPI_FLOAT, "MPI_FLOAT" },
    { MPI_DOUBLE, "MPI_DOUBLE" },
    { MPI_LONG_DOUBLE, "MPI_LONG_DOUBLE" },
    /* LONG_LONG_INT is allowed as an alias; we don't make it a separate
       type */
/*    { MPI_LONG_LONG_INT, "MPI_LONG_LONG_INT" }, */
    { MPI_LONG_LONG, "MPI_LONG_LONG" },
    { MPI_UNSIGNED_LONG_LONG, "MPI_UNSIGNED_LONG_LONG" }, 
    { MPI_PACKED, "MPI_PACKED" },
    { MPI_LB, "MPI_LB" },
    { MPI_UB, "MPI_UB" },

/* The maxloc/minloc types are not builtin because they're size and
   extent may not be the same. */
/*
    { MPI_FLOAT_INT, "MPI_FLOAT_INT" },
    { MPI_DOUBLE_INT, "MPI_DOUBLE_INT" },
    { MPI_LONG_INT, "MPI_LONG_INT" },
    { MPI_SHORT_INT, "MPI_SHORT_INT" },
*/
    /* Fortran */
    { MPI_COMPLEX, "MPI_COMPLEX" },
    { MPI_DOUBLE_COMPLEX, "MPI_DOUBLE_COMPLEX" },
    { MPI_LOGICAL, "MPI_LOGICAL" },
    { MPI_REAL, "MPI_REAL" },
    { MPI_DOUBLE_PRECISION, "MPI_DOUBLE_PRECISION" },
    { MPI_INTEGER, "MPI_INTEGER" },
    { MPI_2INTEGER, "MPI_2INTEGER" },
    { MPI_2COMPLEX, "MPI_2COMPLEX" },
    { MPI_2DOUBLE_COMPLEX, "MPI_2DOUBLE_COMPLEX" },
    { MPI_2REAL, "MPI_2REAL" },
    { MPI_2DOUBLE_PRECISION, "MPI_2DOUBLE_PRECISION" },
    { MPI_CHARACTER, "MPI_CHARACTER" },
    /* Size-specific types (C, Fortran, and C++) */
    { MPI_REAL4, "MPI_REAL4" },
    { MPI_REAL8, "MPI_REAL8" },
    { MPI_REAL16, "MPI_REAL16" },
    { MPI_COMPLEX8, "MPI_COMPLEX8" },
    { MPI_COMPLEX16, "MPI_COMPLEX16" },
    { MPI_COMPLEX32, "MPI_COMPLEX32" },
    { MPI_INTEGER1, "MPI_INTEGER1" },
    { MPI_INTEGER2, "MPI_INTEGER2" },
    { MPI_INTEGER4, "MPI_INTEGER4" },
    { MPI_INTEGER8, "MPI_INTEGER8" },
    { MPI_INTEGER16, "MPI_INTEGER16" },
    { 0, (char *)0 },  /* Sentinal used to indicate the last element */
};

static mpi_names_t mpi_maxloc_names[] = {
    { MPI_FLOAT_INT, "MPI_FLOAT_INT" },
    { MPI_DOUBLE_INT, "MPI_DOUBLE_INT" },
    { MPI_LONG_INT, "MPI_LONG_INT" },
    { MPI_SHORT_INT, "MPI_SHORT_INT" },
    { MPI_2INT, "MPI_2INT" },
    { MPI_LONG_DOUBLE_INT, "MPI_LONG_DOUBLE_INT" },
    { 0, (char *)0 },  /* Sentinal used to indicate the last element */
};
/* This routine is also needed by type_set_name */

int MPIR_Datatype_init_names( void ) 
{
    static const char FCNAME[] = "MPIR_Datatype_init_names";
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPID_Datatype *datatype_ptr = NULL;
    static int setup = 0;
    char error_msg[1024];
    
    if (setup)
    {
	return MPI_SUCCESS;
    }

    MPID_Common_thread_lock();
    {
	if (!setup) {

	    /* Make sure that the basics have datatype structures allocated
	     * and filled in for them.  They are just integers prior to this
	     * call.
             */
	    mpi_errno = MPIR_Datatype_builtin_fillin();
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, 
				 FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */

	    /* For each predefined type, ensure that there is a corresponding
	       object and that the object's name is set */
	    for (i=0; mpi_names[i].name != 0; i++) {
		/* The size-specific types may be DATATYPE_NULL */
		if (mpi_names[i].dtype == MPI_DATATYPE_NULL) continue;

		MPID_Datatype_get_ptr( mpi_names[i].dtype, datatype_ptr );

		/* --BEGIN ERROR HANDLING-- */
		if (datatype_ptr < MPID_Datatype_builtin || 
		    datatype_ptr > MPID_Datatype_builtin + MPID_DATATYPE_N_BUILTIN)
		{
		    MPIU_Snprintf(error_msg, 1024, 
		  "%dth builtin datatype handle references invalid memory", i);
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, 
		            MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN, 
			    "**fail", "**fail %s", error_msg);
		    return mpi_errno;
		}
		if (!datatype_ptr) {
		    MPIU_Snprintf(error_msg, 1024, "Did not initialize name for all of the predefined datatypes (only did first %d)\n", i-1 );
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN, "**fail", "**fail %s", error_msg);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
		/* MPIU_dbg_printf("mpi_names[%d].name = %x\n", i, (int) mpi_names[i].name ); */
		MPIU_Strncpy( datatype_ptr->name, mpi_names[i].name, 
			      MPI_MAX_OBJECT_NAME );
	    }
	    /* Handle the minloc/maxloc types */
	    for (i=0; mpi_maxloc_names[i].name != 0; i++) {
		MPID_Datatype_get_ptr( mpi_maxloc_names[i].dtype, 
				       datatype_ptr );
		/* --BEGIN ERROR HANDLING-- */
		if (!datatype_ptr) {
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, 
					MPIR_ERR_FATAL, FCNAME, __LINE__, 
					MPI_ERR_INTERN, "**fail", 0 );
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
		MPIU_Strncpy( datatype_ptr->name, mpi_maxloc_names[i].name, 
			      MPI_MAX_OBJECT_NAME );
	    }
	    setup = 1;
	}
	MPID_Common_thread_unlock();
    }
    return mpi_errno;
}
#endif

#undef FUNCNAME
#define FUNCNAME MPI_Type_get_name

/*@
   MPI_Type_get_name - Get the print name for a datatype

   Input Parameter:
. type - datatype whose name is to be returned (handle) 

   Output Parameters:
+ type_name - the name previously stored on the datatype, or a empty string 
  if no such name exists (string) 
- resultlen - length of returned name (integer) 

.N ThreadSafeNoUpdate

.N NULL

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_TYPE
.N MPI_ERR_ARG
@*/
int MPI_Type_get_name(MPI_Datatype datatype, char *type_name, int *resultlen)
{
    static const char FCNAME[] = "MPI_Type_get_name";
    int mpi_errno = MPI_SUCCESS;
    MPID_Datatype *datatype_ptr = NULL;
    static int setup = 0;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_TYPE_GET_NAME);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPID_CS_ENTER();
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_TYPE_GET_NAME);
    
    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_DATATYPE(datatype, "datatype", mpi_errno);
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif

    /* Note that MPI_DATATYPE_NULL is invalid input to this routine;
       it must not return a string for MPI_DATATYPE_NULL.  Instead, 
       it must return an error indicating an invalid datatype argument */

    /* Convert MPI object handles to object pointers */
    MPID_Datatype_get_ptr( datatype, datatype_ptr );
    
    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate datatype_ptr */
            MPID_Datatype_valid_ptr( datatype_ptr, mpi_errno );
	    /* If datatype_ptr is not valid, it will be reset to null */
	    MPIR_ERRTEST_ARGNULL(type_name,"type_name", mpi_errno);
	    MPIR_ERRTEST_ARGNULL(resultlen,"resultlen", mpi_errno);
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    /* If this is the first call, initialize all of the predefined names */
    if (!setup) { 
	mpi_errno = MPIR_Datatype_init_names();
	if (mpi_errno != MPI_SUCCESS) goto fn_fail;
	setup = 1;
    }

    /* Include the null in MPI_MAX_OBJECT_NAME */
    MPIU_Strncpy( type_name, datatype_ptr->name, MPI_MAX_OBJECT_NAME );
    *resultlen = (int)strlen( type_name );
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_TYPE_GET_NAME);
    MPID_CS_EXIT();
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_type_get_name", 
	    "**mpi_type_get_name %D %p %p", datatype, type_name, resultlen);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( NULL, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
