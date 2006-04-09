/* -*- Mode: C; c-basic-offset:4 ; -*- */

/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include <mpid_dataloop.h>
#include <mpiimpl.h>

/*@
   Dataloop_contiguous - create the dataloop representation for a
   contiguous datatype

   Arguments:
+  int count,
.  MPI_Datatype oldtype,
.  MPID_Dataloop **dlp_p,
.  int *dlsz_p,
.  int *dldepth_p,
-  int flags

.N Errors
.N Returns 0 on success, -1 on failure.
@*/
int PREPEND_PREFIX(Dataloop_create_contiguous)(int count,
					       DLOOP_Type oldtype,
					       DLOOP_Dataloop **dlp_p,
					       int *dlsz_p,
					       int *dldepth_p,
					       int flags)
{
    int is_builtin, apply_contig_coalescing = 0;
    int new_loop_sz, new_loop_depth;

    DLOOP_Dataloop *new_dlp;

    is_builtin = (DLOOP_Handle_hasloop_macro(oldtype)) ? 0 : 1;

    if (is_builtin)
    {
	new_loop_depth = 1;
    }
    else
    {
	int old_loop_sz = 0, old_loop_depth = 0, old_size = 0;
	DLOOP_Offset old_extent = 0;
	DLOOP_Dataloop *old_loop_ptr;

	DLOOP_Handle_get_loopsize_macro(oldtype, old_loop_sz, 0);
	DLOOP_Handle_get_loopdepth_macro(oldtype, old_loop_depth, 0);
	DLOOP_Handle_get_loopptr_macro(oldtype, old_loop_ptr, 0);
	DLOOP_Handle_get_size_macro(oldtype, old_size);
	DLOOP_Handle_get_extent_macro(oldtype, old_extent);

	/* if we have a simple combination of contigs, coalesce */
	if (((old_loop_ptr->kind & DLOOP_KIND_MASK) == DLOOP_KIND_CONTIG)
	    && (old_size == old_extent))
	{
	    /* will just copy contig and multiply count */
	    apply_contig_coalescing = 1;
	    new_loop_depth          = old_loop_depth;
	}
	else
	{
	    new_loop_depth = old_loop_depth + 1;
	}
    }

    if (is_builtin)
    {
	int basic_sz = 0;

	PREPEND_PREFIX(Dataloop_alloc)(DLOOP_KIND_CONTIG,
				       count,
				       &new_dlp,
				       &new_loop_sz);
	/* --BEGIN ERROR HANDLING-- */
	if (!new_dlp) return -1;
	/* --END ERROR HANDLING-- */

	DLOOP_Handle_get_size_macro(oldtype, basic_sz);
	new_dlp->kind = DLOOP_KIND_CONTIG | DLOOP_FINAL_MASK;

	if (flags & MPID_DATALOOP_ALL_BYTES)
	{
	    count             *= basic_sz;
	    new_dlp->el_size   = 1;
	    new_dlp->el_extent = 1;
	    new_dlp->el_type   = MPI_BYTE;
	}
	else
	{
	    new_dlp->el_size   = basic_sz;
	    new_dlp->el_extent = new_dlp->el_size;
	    new_dlp->el_type   = oldtype;
	}

	new_dlp->loop_params.c_t.count = count;
    }
    else
    {
	/* user-defined base type (oldtype) */
	DLOOP_Dataloop *old_loop_ptr;
	int old_loop_sz = 0;

	DLOOP_Handle_get_loopptr_macro(oldtype, old_loop_ptr, 0);
	DLOOP_Handle_get_loopsize_macro(oldtype, old_loop_sz, 0);

	if (apply_contig_coalescing)
	{
	    /* make a copy of the old loop and multiply the count */
	    PREPEND_PREFIX(Dataloop_dup)(old_loop_ptr,
					 old_loop_sz,
					 &new_dlp);
	    /* --BEGIN ERROR HANDLING-- */
	    if (!new_dlp) return -1;
	    /* --END ERROR HANDLING-- */

	    new_dlp->loop_params.c_t.count *= count;

	    new_loop_sz = old_loop_sz;
	    DLOOP_Handle_get_loopdepth_macro(oldtype, new_loop_depth, 0);
	}
	else
	{
	    DLOOP_Dataloop *old_loop_ptr;
	    int old_loop_sz = 0;

	    DLOOP_Handle_get_loopptr_macro(oldtype, old_loop_ptr, 0);
	    DLOOP_Handle_get_loopsize_macro(oldtype, old_loop_sz, 0);

	    /* allocate space for new loop including copy of old */
	    PREPEND_PREFIX(Dataloop_alloc_and_copy)(DLOOP_KIND_CONTIG,
						    count,
						    old_loop_ptr,
						    old_loop_sz,
						    &new_dlp,
						    &new_loop_sz);
	    /* --BEGIN ERROR HANDLING-- */
	    if (!new_dlp) return -1;
	    /* --END ERROR HANDLING-- */

	    new_dlp->kind = DLOOP_KIND_CONTIG;
	    DLOOP_Handle_get_size_macro(oldtype, new_dlp->el_size);
	    DLOOP_Handle_get_extent_macro(oldtype, new_dlp->el_extent);
	    DLOOP_Handle_get_basic_type_macro(oldtype, new_dlp->el_type);
	    
	    new_dlp->loop_params.c_t.count = count;
	}
    }

    *dlp_p     = new_dlp;
    *dlsz_p    = new_loop_sz;
    *dldepth_p = new_loop_depth;

    return 0;
}
