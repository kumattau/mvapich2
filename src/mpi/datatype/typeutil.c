/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "datatype.h"

/* This is the utility file for datatypes that contains the basic datatype 
   items and storage management.  It also contains a temporary routine
   that is used by ROMIO to test to see if datatypes are contiguous */
#ifndef MPID_DATATYPE_PREALLOC 
#define MPID_DATATYPE_PREALLOC 8
#endif

/* Preallocated datatype objects */
MPID_Datatype MPID_Datatype_builtin[MPID_DATATYPE_N_BUILTIN + 1] = { {0} };
MPID_Datatype MPID_Datatype_direct[MPID_DATATYPE_PREALLOC] = { {0} };
MPIU_Object_alloc_t MPID_Datatype_mem = { 0, 0, 0, 0, MPID_DATATYPE, 
			      sizeof(MPID_Datatype), MPID_Datatype_direct,
					  MPID_DATATYPE_PREALLOC};

static int MPIR_Datatype_finalize(void *dummy);

/* Call this routine to associate a MPID_Datatype with each predefined 
   datatype.  We do this with lazy initialization because many MPI 
   programs do not require anything except the predefined datatypes, and
   all of the necessary information about those is stored within the
   MPI_Datatype handle.  However, if the user wants to change the name
   (character string, set with MPI_Type_set_name) associated with a
   predefined name, then the structures must be allocated.
*/
static MPI_Datatype mpi_dtypes[] = {
    MPI_CHAR,
    MPI_UNSIGNED_CHAR,
    MPI_SIGNED_CHAR,
    MPI_BYTE,
    MPI_WCHAR,
    MPI_SHORT,
    MPI_UNSIGNED_SHORT,
    MPI_INT,
    MPI_UNSIGNED,
    MPI_LONG,
    MPI_UNSIGNED_LONG,
    MPI_FLOAT,
    MPI_DOUBLE,
    MPI_LONG_DOUBLE,
    MPI_LONG_LONG,
    MPI_UNSIGNED_LONG_LONG,
    MPI_PACKED,
    MPI_LB,
    MPI_UB,
    MPI_2INT,
#if 0
    /* no longer builtins */
    MPI_FLOAT_INT,
    MPI_DOUBLE_INT,
    MPI_LONG_INT,
    MPI_SHORT_INT,
    MPI_LONG_DOUBLE_INT,
#endif
/* Fortran types */
    MPI_COMPLEX,
    MPI_DOUBLE_COMPLEX,
    MPI_LOGICAL,
    MPI_REAL,
    MPI_DOUBLE_PRECISION,
    MPI_INTEGER,
    MPI_2INTEGER,
    MPI_2COMPLEX,
    MPI_2DOUBLE_COMPLEX,
    MPI_2REAL,
    MPI_2DOUBLE_PRECISION,
    MPI_CHARACTER,
#ifdef HAVE_FORTRAN_BINDING
/* Size-specific types; these are in section 10.2.4 (Extended Fortran Support)
   as well as optional in MPI-1
*/
    MPI_REAL4,
    MPI_REAL8,
    MPI_REAL16,
    MPI_COMPLEX8,
    MPI_COMPLEX16,
    MPI_COMPLEX32,
    MPI_INTEGER1,
    MPI_INTEGER2,
    MPI_INTEGER4,
    MPI_INTEGER8,
    MPI_INTEGER16,
#endif
    /* This entry is a guaranteed end-of-list item */
    (MPI_Datatype) -1,
};

/*
  MPIR_Datatype_init()

  Main purpose of this function is to set up the following pair types:
  - MPI_FLOAT_INT
  - MPI_DOUBLE_INT
  - MPI_LONG_INT
  - MPI_SHORT_INT
  - MPI_LONG_DOUBLE_INT

  The assertions in this code ensure that:
  - this function is called before other types are allocated
  - there are enough spaces in the direct block to hold our types
  - we actually get the values we expect (otherwise errors regarding
    these types could be terribly difficult to track down!)

 */
static MPI_Datatype mpi_pairtypes[] = {
    MPI_FLOAT_INT,
    MPI_DOUBLE_INT,
    MPI_LONG_INT,
    MPI_SHORT_INT,
    MPI_LONG_DOUBLE_INT,
    (MPI_Datatype) -1
};

int MPIR_Datatype_init(void)
{
    int i;
    MPIU_Handle_common *ptr;

    MPIU_Handle_obj_alloc_start(&MPID_Datatype_mem);

    MPIU_Assert(MPID_Datatype_mem.initialized == 0);
    MPIU_Assert(MPID_DATATYPE_PREALLOC >= 5);

    /* initialize also gets us our first pointer */
    ptr = MPIU_Handle_direct_init(MPID_Datatype_mem.direct,
				  MPID_Datatype_mem.direct_size,
				  MPID_Datatype_mem.size,
				  MPID_Datatype_mem.kind);

    MPID_Datatype_mem.initialized = 1;
    MPID_Datatype_mem.avail = ptr->next;

    MPIU_Assert((void *) ptr == (void *) (MPID_Datatype_direct + HANDLE_INDEX(mpi_pairtypes[0])));
    MPID_Type_create_pairtype(mpi_pairtypes[0], (MPID_Datatype *) ptr);

    for (i=1; mpi_pairtypes[i] != (MPI_Datatype) -1; i++) {
	ptr = MPID_Datatype_mem.avail;
	MPID_Datatype_mem.avail = ptr->next;
	ptr->next = 0;
	
	MPIU_Assert(ptr);
	MPIU_Assert((void *) ptr ==
		    (void *) (MPID_Datatype_direct + HANDLE_INDEX(mpi_pairtypes[i])));

	MPID_Type_create_pairtype(mpi_pairtypes[i], (MPID_Datatype *) ptr);
    }

    MPIU_Handle_obj_alloc_complete(&MPID_Datatype_mem, 1);

    MPIR_Add_finalize(MPIR_Datatype_finalize, 0, 8);

    return MPI_SUCCESS;
}

static int MPIR_Datatype_finalize(void *dummy)
{
    int i;
    MPID_Datatype *dptr;

    for (i=0; mpi_pairtypes[i] != (MPI_Datatype) -1; i++) {
	if (mpi_pairtypes[i] != MPI_DATATYPE_NULL) {
	    MPID_Datatype_get_ptr(mpi_pairtypes[i], dptr);
	    MPID_Datatype_release(dptr);
	    mpi_pairtypes[i] = MPI_DATATYPE_NULL;
	}
    }
    return 0;
}

int MPIR_Datatype_builtin_fillin(void)
{
    static const char FCNAME[] = "MPIR_Datatype_builtin_fillin";
    int mpi_errno = MPI_SUCCESS;
    int i;
    MPID_Datatype *dptr;
    MPI_Datatype  d = MPI_DATATYPE_NULL;
    static int is_init = 0;
    char error_msg[1024];

    /* --BEGIN ERROR HANDLING-- */
    if (is_init)
    {
	return MPI_SUCCESS;
    }
    /* --END ERROR HANDLING-- */

    {
	MPID_Common_thread_lock();
	if (!is_init) { 
	    for (i=0; i<MPID_DATATYPE_N_BUILTIN; i++) {
		/* Compute the index from the value of the handle */
		d = mpi_dtypes[i];
		if (d == -1) {
		    /* At the end of mpi_dtypes */
		    break;
		}
		/* Some of the size-specific types may be null,
		   so skip that case */
		if (d == MPI_DATATYPE_NULL) continue;

		MPID_Datatype_get_ptr(d,dptr);
		/* --BEGIN ERROR HANDLING-- */
		if (dptr < MPID_Datatype_builtin || 
		    dptr > MPID_Datatype_builtin + MPID_DATATYPE_N_BUILTIN)
		{
		    MPIU_Snprintf(error_msg, 1024,
				  "%dth builtin datatype handle references invalid memory",
				  i);
		    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS,
						     MPIR_ERR_FATAL, FCNAME,
						     __LINE__, MPI_ERR_INTERN,
						     "**fail", "**fail %s",
						     error_msg);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */

		/* dptr will point into MPID_Datatype_builtin */
		dptr->handle	   = d;
		dptr->is_permanent = 1;
		dptr->is_contig	   = 1;
		MPIU_Object_set_ref( dptr, 1 );
		MPID_Datatype_get_size_macro(mpi_dtypes[i], dptr->size);
		dptr->extent	   = dptr->size;
		dptr->ub	   = dptr->size;
		dptr->true_ub	   = dptr->size;
		dptr->contents     = NULL; /* should never get referenced? */
	    }
	    /* --BEGIN ERROR HANDLING-- */
	    if (d != -1 && mpi_dtypes[i] != -1) {
		/* We did not hit the end-of-list */
		/*MPIU_Internal_error_printf( "Did not initialize all of the predefined datatypes (only did first %d)\n", i-1 );*/
		MPIU_Snprintf(error_msg, 1024,
			      "Did not initialize all of the predefined datatypes (only did first %d)\n",
			      i-1);

		mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
						 FCNAME, __LINE__,
						 MPI_ERR_INTERN, "**fail",
						 "**fail %s", error_msg);
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */
	    is_init = 1;
	}
	MPID_Common_thread_unlock();
    }
    return mpi_errno;
}

/* This will eventually be removed once ROMIO knows more about MPICH2 */
void MPIR_Datatype_iscontig(MPI_Datatype datatype, int *flag)
{
    MPID_Datatype *datatype_ptr;
    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN)
        *flag = 1;
    else  {
        MPID_Datatype_get_ptr(datatype, datatype_ptr);
        *flag = datatype_ptr->is_contig;
    }
}
