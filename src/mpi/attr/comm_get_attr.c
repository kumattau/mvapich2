/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* -- Begin Profiling Symbol Block for routine MPI_Comm_get_attr */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_get_attr = PMPI_Comm_get_attr
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_get_attr  MPI_Comm_get_attr
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_get_attr as PMPI_Comm_get_attr
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Comm_get_attr
#define MPI_Comm_get_attr PMPI_Comm_get_attr

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Comm_get_attr

/*@
   MPI_Comm_get_attr - Retrieves attribute value by key

Input Parameters:
+ comm - communicator to which attribute is attached (handle) 
- keyval - key value (integer) 

Output Parameters:
+ attr_value - attribute value, unless 'flag' = false 
- flag -  true if an attribute value was extracted;  false if no attribute is
  associated with the key 

   Notes:
    Attributes must be extracted from the same language as they were inserted  
    in with 'MPI_Comm_set_attr'.  The notes for C and Fortran below explain 
    why. 

Notes for C:
    Even though the 'attr_value' arguement is declared as 'void *', it is
    really the address of a void pointer.  See the rationale in the 
    standard for more details. 

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_KEYVAL
@*/
int MPI_Comm_get_attr(MPI_Comm comm, int comm_keyval, void *attribute_val, int *flag)
{
    static const char FCNAME[] = "MPI_Comm_get_attr";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    static PreDefined_attrs attr_copy;    /* Used to provide a copy of the
					     predefined attributes */
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_COMM_GET_ATTR);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("attr");
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_COMM_GET_ATTR);
    
    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm, mpi_errno);
	    MPIR_ERRTEST_KEYVAL(comm_keyval, MPID_COMM, "communicator", mpi_errno);
#           ifdef NEEDS_POINTER_ALIGNMENT_ADJUST
            /* A common user error is to pass the address of a 4-byte
	       int when the address of a pointer (or an address-sized int)
	       should have been used.  We can test for this specific
	       case.  Note that this code assumes sizeof(MPI_Aint) is 
	       a power of 2. */
	    if ((MPI_Aint)attribute_val & (sizeof(MPI_Aint)-1)) {
		MPIU_ERR_SET(mpi_errno,MPI_ERR_ARG,"**attrnotptr");
	    }
#           endif
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr( comm, comm_ptr );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
	    /* If comm_ptr is not valid, it will be reset to null */
	    MPIR_ERRTEST_ARGNULL(attribute_val, "attr_val", mpi_errno);
	    MPIR_ERRTEST_ARGNULL(flag, "flag", mpi_errno);
            if (mpi_errno) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    /* Check for builtin attribute */
    /* This code is ok for correct programs, but it would be better
       to copy the values from the per-process block and pass the user
       a pointer to a copy */
    /* Note that if we are called from Fortran, we must return the values,
       not the addresses, of these attributes */
    if (HANDLE_GET_KIND(comm_keyval) == HANDLE_KIND_BUILTIN) {
	int attr_idx = comm_keyval & 0x0000000f;
	void **attr_val_p = (void **)attribute_val;
#ifdef HAVE_FORTRAN_BINDING
	/* This is an address-sized int instead of a Fortran (MPI_Fint)
	   integer because, even for the Fortran keyvals, the C interface is 
	   used which stores the result in a pointer (hence we need a
	   pointer-sized int).  Thus we use MPI_Aint instead of MPI_Fint.
	   On some 64-bit plaforms, such as Solaris-SPARC, using an MPI_Fint
	   will cause the value to placed into the high, rather than low,
	   end of the output value. */
	MPI_Aint  *attr_int = (MPI_Aint *)attribute_val;
#endif
	*flag = 1;

	/* FIXME : We could initialize some of these here; only tag_ub is 
	 used in the error checking. */
	/* 
	 * The C versions of the attributes return the address of a 
	 * *COPY* of the value (to prevent the user from changing it)
	 * and the Fortran versions provide the actual value (as an Fint)
	 */
	attr_copy = MPIR_Process.attrs;
	switch (attr_idx) {
	case 1: /* TAG_UB */
	    *attr_val_p = &attr_copy.tag_ub;
	    break;
	case 3: /* HOST */
	    *attr_val_p = &attr_copy.host;
	    break;
	case 5: /* IO */
	    *attr_val_p = &attr_copy.io;
	    break;
	case 7: /* WTIME */
	    *attr_val_p = &attr_copy.wtime_is_global;
	    break;
	case 9: /* UNIVERSE_SIZE */
	    /* This is a special case.  If universe is not set, then we
	       attempt to get it from the device.  If the device is doesn't
	       supply a value, then we set the flag accordingly */
	    if (attr_copy.universe >= 0)
	    { 
		*attr_val_p = &attr_copy.universe;
	    }
	    else if (attr_copy.universe == MPIR_UNIVERSE_SIZE_NOT_AVAILABLE)
	    {
		*flag = 0;
	    }
	    else
	    {
		mpi_errno = MPID_Get_universe_size(&attr_copy.universe);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    attr_copy.universe = MPIR_UNIVERSE_SIZE_NOT_AVAILABLE;
		    goto fn_fail;
		}
		/* --END ERROR HANDLING-- */
		
		if (attr_copy.universe >= 0)
		{
		    *attr_val_p = &attr_copy.universe;
		}
		else
		{
		    attr_copy.universe = MPIR_UNIVERSE_SIZE_NOT_AVAILABLE;
		    *flag = 0;
		}
	    }
	    break;
	case 11: /* LASTUSEDCODE */
	    *attr_val_p = &attr_copy.lastusedcode;
	    break;
	case 13: /* APPNUM */
	    /* This is another special case.  If appnum is negative,
	       we take that as indicating no value of APPNUM, and set
	       the flag accordingly */
	    if (attr_copy.appnum < 0) {
		*flag = 0;
	    }
	    else {
		*attr_val_p = &attr_copy.appnum;
	    }
	    break;
#ifdef HAVE_FORTRAN_BINDING
	case 2: /* Fortran TAG_UB */
	    *attr_int = attr_copy.tag_ub;
	    break;
	case 4: /* Fortran HOST */
	    *attr_int = attr_copy.host;
	    break;
	case 6: /* Fortran IO */
	    *attr_int = attr_copy.io;
	    break;
	case 8: /* Fortran WTIME */
	    *attr_int = attr_copy.wtime_is_global;
	    break;
	case 10: /* UNIVERSE_SIZE */
	    /* This is a special case.  If universe is not set, then we
	       attempt to get it from the device.  If the device is doesn't
	       supply a value, then we set the flag accordingly */
	    if (attr_copy.universe >= 0)
	    { 
		*attr_int = attr_copy.universe;
	    }
	    else if (attr_copy.universe == MPIR_UNIVERSE_SIZE_NOT_AVAILABLE)
	    {
		*flag = 0;
	    }
	    else
	    {
		mpi_errno = MPID_Get_universe_size(&attr_copy.universe);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS)
		{
		    attr_copy.universe = MPIR_UNIVERSE_SIZE_NOT_AVAILABLE;
		    goto fn_fail;
		}
		/* --END ERROR HANDLING-- */
		
		if (attr_copy.universe >= 0)
		{
		    *attr_int = attr_copy.universe;
		}
		else
		{
		    attr_copy.universe = MPIR_UNIVERSE_SIZE_NOT_AVAILABLE;
		    *flag = 0;
		}
	    }
	    break;
	case 12: /* LASTUSEDCODE */
	    *attr_int = attr_copy.lastusedcode;
	    break;
	case 14: /* APPNUM */
	    /* This is another special case.  If appnum is negative,
	       we take that as indicating no value of APPNUM, and set
	       the flag accordingly */
	    if (attr_copy.appnum < 0) {
		*flag = 0;
	    }
	    else {
		*attr_int = attr_copy.appnum;
	    }
	    break;
#endif
	}
    }
    else {
	MPID_Attribute *p = comm_ptr->attributes;

	*flag = 0;
	while (p) {
	    if (p->keyval->handle == comm_keyval) {
		*flag = 1;
		(*(void **)attribute_val) = p->value;
		break;
	    }
	    p = p->next;
	}
    }
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_COMM_GET_ATTR);
    MPIU_THREAD_SINGLE_CS_EXIT("attr");
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_comm_get_attr",
	    "**mpi_comm_get_attr %C %d %p %p", comm, comm_keyval, attribute_val, flag);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
