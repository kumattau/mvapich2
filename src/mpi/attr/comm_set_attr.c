/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "attr.h"

/* -- Begin Profiling Symbol Block for routine MPI_Comm_set_attr */
#if defined(HAVE_PRAGMA_WEAK)
#pragma weak MPI_Comm_set_attr = PMPI_Comm_set_attr
#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#pragma _HP_SECONDARY_DEF PMPI_Comm_set_attr  MPI_Comm_set_attr
#elif defined(HAVE_PRAGMA_CRI_DUP)
#pragma _CRI duplicate MPI_Comm_set_attr as PMPI_Comm_set_attr
#endif
/* -- End Profiling Symbol Block */

/* Define MPICH_MPI_FROM_PMPI if weak symbols are not supported to build
   the MPI routines */
#ifndef MPICH_MPI_FROM_PMPI
#undef MPI_Comm_set_attr
#define MPI_Comm_set_attr PMPI_Comm_set_attr

#endif

#undef FUNCNAME
#define FUNCNAME MPI_Comm_set_attr

/*@
   MPI_Comm_set_attr - Stores attribute value associated with a key

Input Parameters:
+ comm - communicator to which attribute will be attached (handle) 
. keyval - key value, as returned by  'MPI_Comm_create_keyval' (integer) 
- attribute_val - attribute value 

Notes:
Values of the permanent attributes 'MPI_TAG_UB', 'MPI_HOST', 'MPI_IO', 
'MPI_WTIME_IS_GLOBAL', 'MPI_UNIVERSE_SIZE', 'MPI_LASTUSEDCODE', and 
'MPI_APPNUM' may not be changed. 

The type of the attribute value depends on whether C, C++, or Fortran
is being used. 
In C and C++, an attribute value is a pointer ('void *'); in Fortran, it is an 
address-sized integer.

If an attribute is already present, the delete function (specified when the
corresponding keyval was created) will be called.

.N ThreadSafe

.N Fortran

.N Errors
.N MPI_SUCCESS
.N MPI_ERR_COMM
.N MPI_ERR_KEYVAL
.N MPI_ERR_PERM_KEY

.seealso MPI_Comm_create_keyval, MPI_Comm_delete_attr
@*/
int MPI_Comm_set_attr(MPI_Comm comm, int comm_keyval, void *attribute_val)
{
    static const char FCNAME[] = "MPI_Comm_set_attr";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr = NULL;
    MPID_Keyval *keyval_ptr = NULL;
    MPID_Attribute *p, **old_p;
    MPID_MPI_STATE_DECL(MPID_STATE_MPI_COMM_SET_ATTR);

    MPIR_ERRTEST_INITIALIZED_ORDIE();
    
    MPIU_THREAD_SINGLE_CS_ENTER("attr");
    MPID_MPI_FUNC_ENTER(MPID_STATE_MPI_COMM_SET_ATTR);

    /* Validate parameters, especially handles needing to be converted */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
	    MPIR_ERRTEST_COMM(comm, mpi_errno);
	    MPIR_ERRTEST_KEYVAL(comm_keyval, MPID_COMM, "communicator", mpi_errno);
	    MPIR_ERRTEST_KEYVAL_PERM(comm_keyval, mpi_errno);
            if (mpi_errno != MPI_SUCCESS) goto fn_fail;
        }
        MPID_END_ERROR_CHECKS;
    }
#   endif

    /* Convert MPI object handles to object pointers */
    MPID_Comm_get_ptr( comm, comm_ptr );
    MPID_Keyval_get_ptr( comm_keyval, keyval_ptr );

    /* Validate parameters and objects (post conversion) */
#   ifdef HAVE_ERROR_CHECKING
    {
        MPID_BEGIN_ERROR_CHECKS;
        {
            /* Validate comm_ptr */
            MPID_Comm_valid_ptr( comm_ptr, mpi_errno );
	    /* If comm_ptr is not valid, it will be reset to null */
	    /* Validate keyval_ptr */
	    MPID_Keyval_valid_ptr( keyval_ptr, mpi_errno );
            if (mpi_errno) goto fn_fail;
	}
        MPID_END_ERROR_CHECKS;
    }
#   endif /* HAVE_ERROR_CHECKING */

    /* ... body of routine ...  */
    
    /* Look for attribute.  They are ordered by keyval handle.  This uses 
       a simple linear list algorithm because few applications use more than a 
       handful of attributes */
    
    old_p = &comm_ptr->attributes;
    p = comm_ptr->attributes;
    while (p) {
	if (p->keyval->handle == keyval_ptr->handle) {
	    /* If found, call the delete function before replacing the 
	       attribute */
	    mpi_errno = MPIR_Call_attr_delete( comm, p );
	    if (mpi_errno) {
		goto fn_fail;
	    }
	    p->value = attribute_val;
	    /* Does not change the reference count on the keyval */
	    break;
	}
	else if (p->keyval->handle > keyval_ptr->handle) {
	    MPID_Attribute *new_p = (MPID_Attribute *)MPIU_Handle_obj_alloc( &MPID_Attr_mem );
	    MPIU_ERR_CHKANDJUMP(!new_p,mpi_errno,MPI_ERR_OTHER,"**nomem");
	    new_p->keyval	 = keyval_ptr;
	    new_p->pre_sentinal	 = 0;
	    new_p->value	 = attribute_val;
	    new_p->post_sentinal = 0;
	    new_p->next		 = p->next;
	    MPIR_Keyval_add_ref( keyval_ptr );
	    p->next		 = new_p;
	    break;
	}
	old_p = &p->next;
	p = p->next;
    }
    if (!p) {
	MPID_Attribute *new_p = (MPID_Attribute *)MPIU_Handle_obj_alloc( &MPID_Attr_mem );
	MPIU_ERR_CHKANDJUMP(!new_p,mpi_errno,MPI_ERR_OTHER,"**nomem");
	/* Did not find in list.  Add at end */
	new_p->keyval	     = keyval_ptr;
	new_p->pre_sentinal  = 0;
	new_p->value	     = attribute_val;
	new_p->post_sentinal = 0;
	new_p->next	     = 0;
	MPIR_Keyval_add_ref( keyval_ptr );
	*old_p		     = new_p;
    }
    
    /* Here is where we could add a hook for the device to detect attribute
       value changes, using something like
       MPID_Dev_comm_attr_hook( comm_ptr, keyval, attribute_val );
    */
    
    /* ... end of body of routine ... */

  fn_exit:
    MPID_MPI_FUNC_EXIT(MPID_STATE_MPI_COMM_SET_ATTR);
    MPIU_THREAD_SINGLE_CS_EXIT("attr");
    return mpi_errno;

  fn_fail:
    /* --BEGIN ERROR HANDLING-- */
#   ifdef HAVE_ERROR_CHECKING
    {
	mpi_errno = MPIR_Err_create_code(
	    mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**mpi_comm_set_attr",
	    "**mpi_comm_set_attr %C %d %p", comm, comm_keyval, attribute_val);
    }
#   endif
    mpi_errno = MPIR_Err_return_comm( comm_ptr, FCNAME, mpi_errno );
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}
