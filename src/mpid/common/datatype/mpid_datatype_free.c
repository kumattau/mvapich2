/* -*- Mode: C; c-basic-offset:4 ; -*- */

/*
 *  (C) 2002 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include <mpiimpl.h>
#include <mpid_dataloop.h>
#include <stdlib.h>
#include <limits.h>

/* #define MPID_TYPE_ALLOC_DEBUG */

/*@

  MPID_Datatype_free

  Input Parameters:
. MPID_Datatype ptr - pointer to MPID datatype structure that is no longer
  referenced

  Output Parameters:
  none

  Return Value:
  none

  This function handles freeing dynamically allocated memory associated with
  the datatype.  In the process MPID_Datatype_free_contents() is also called,
  which handles decrementing reference counts to constituent types (in
  addition to freeing the space used for contents information).
  MPID_Datatype_free_contents() will call MPID_Datatype_free() on constituent
  types that are no longer referenced as well.

  @*/
void MPID_Datatype_free(MPID_Datatype *ptr)
{
#ifdef MPID_TYPE_ALLOC_DEBUG
    MPIU_dbg_printf("type %x freed.\n", ptr->handle);
#endif

    /* before freeing the contents, check whether the pointer is not
       null because it is null in the case of a datatype shipped to the target
       for RMA ops */  
    if (ptr->contents) {
        MPID_Datatype_free_contents(ptr);
    }
    MPID_Dataloop_free(&(ptr->dataloop));
    if (ptr->hetero_dloop) {
	MPID_Dataloop_free(&(ptr->hetero_dloop));
    }
    MPIU_Handle_obj_free(&MPID_Datatype_mem, ptr);
}
