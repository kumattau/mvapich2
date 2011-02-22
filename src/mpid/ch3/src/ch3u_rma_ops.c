/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3_impl.h"
#include "mpidrma.h"

#define MPIDI_PASSIVE_TARGET_DONE_TAG  348297
#define MPIDI_PASSIVE_TARGET_RMA_TAG 563924


#undef FUNCNAME
#define FUNCNAME MPIDI_Win_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_create(void *base, MPI_Aint size, int disp_unit, MPID_Info *info,
		     MPID_Comm *comm_ptr, MPID_Win **win_ptr )
{
    int mpi_errno=MPI_SUCCESS, i, comm_size, rank;
    MPI_Aint *tmp_buf;
    MPIU_CHKPMEM_DECL(4);
    MPIU_CHKLMEM_DECL(1);
    MPIU_THREADPRIV_DECL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_CREATE);
    
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_CREATE);

    /* FIXME: There should be no unreferenced args */
    MPIU_UNREFERENCED_ARG(info);

    MPIU_THREADPRIV_GET;

    MPIR_Nest_incr();
        
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
    *win_ptr = (MPID_Win *)MPIU_Handle_obj_alloc( &MPID_Win_mem );
    MPIU_ERR_CHKANDJUMP(!(*win_ptr),mpi_errno,MPI_ERR_OTHER,"**nomem");

    MPIU_Object_set_ref(*win_ptr, 1);

    (*win_ptr)->fence_cnt = 0;
    (*win_ptr)->base = base;
    (*win_ptr)->size = size;
    (*win_ptr)->disp_unit = disp_unit;
    (*win_ptr)->start_group_ptr = NULL; 
    (*win_ptr)->start_assert = 0; 
    (*win_ptr)->attributes = NULL;
    (*win_ptr)->rma_ops_list = NULL;
    (*win_ptr)->lock_granted = 0;
    (*win_ptr)->current_lock_type = MPID_LOCK_NONE;
    (*win_ptr)->shared_lock_ref_cnt = 0;
    (*win_ptr)->lock_queue = NULL;
    (*win_ptr)->my_counter = 0;
    (*win_ptr)->my_pt_rma_puts_accs = 0;
#if defined(_OSU_MVAPICH_)
    (*win_ptr)->outstanding_rma = 0;
#endif /* defined(_OSU_MVAPICH_) */
#if defined (_OSU_PSM_)
    /* my_rank is rank within this communicator. For psm-messaging
       we need actual COMM_WORLD rank. This is comm_rank */
    (*win_ptr)->comm_ptr = comm_ptr;
    (*win_ptr)->my_rank = rank;
#endif

    mpi_errno = NMPI_Comm_dup(comm_ptr->handle, &((*win_ptr)->comm));
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
   
#if defined (_OSU_PSM_)
    (*win_ptr)->rank_mapping = MPIU_Malloc(comm_size * sizeof(uint32_t));
    if((*win_ptr)->rank_mapping == NULL) {
        MPIU_ERR_SET(mpi_errno, MPI_ERR_NO_MEM, "**nomem");
        goto fn_fail;
    }
#endif /* _OSU_PSM_ */

    /* allocate memory for the base addresses, disp_units, and
       completion counters of all processes */ 
    MPIU_CHKPMEM_MALLOC((*win_ptr)->base_addrs, void **,
			comm_size*sizeof(void *), 
			mpi_errno, "(*win_ptr)->base_addrs");

    MPIU_CHKPMEM_MALLOC((*win_ptr)->disp_units, int *, comm_size*sizeof(int), 
			mpi_errno, "(*win_ptr)->disp_units");

    MPIU_CHKPMEM_MALLOC((*win_ptr)->all_win_handles, MPI_Win *, 
			comm_size*sizeof(MPI_Win), 
			mpi_errno, "(*win_ptr)->all_win_handles");
    
    MPIU_CHKPMEM_MALLOC((*win_ptr)->pt_rma_puts_accs, int *, 
			comm_size*sizeof(int), 
			mpi_errno, "(*win_ptr)->pt_rma_puts_accs");
    for (i=0; i<comm_size; i++)	(*win_ptr)->pt_rma_puts_accs[i] = 0;
    
    /* get the addresses of the windows, window objects, and completion
       counters of all processes.  allocate temp. buffer for communication */
    MPIU_CHKLMEM_MALLOC(tmp_buf, MPI_Aint *, 3*comm_size*sizeof(MPI_Aint),
			mpi_errno, "tmp_buf");
    
    /* FIXME: This needs to be fixed for heterogeneous systems */
    tmp_buf[3*rank] = MPIU_PtrToAint(base);
    tmp_buf[3*rank+1] = (MPI_Aint) disp_unit;
    tmp_buf[3*rank+2] = (MPI_Aint) (*win_ptr)->handle;
    
    mpi_errno = NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
			       tmp_buf, 3 * sizeof(MPI_Aint), MPI_BYTE, 
			       comm_ptr->handle);   
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    
    for (i=0; i<comm_size; i++)
    {
	(*win_ptr)->base_addrs[i] = MPIU_AintToPtr(tmp_buf[3*i]);
	(*win_ptr)->disp_units[i] = (int) tmp_buf[3*i+1];
	(*win_ptr)->all_win_handles[i] = (MPI_Win) tmp_buf[3*i+2];
    }

#if defined (_OSU_PSM_)
    /* tell everyone, what is my COMM_WORLD rank */
    (*win_ptr)->rank_mapping[rank] = MPIDI_Process.my_pg_rank;
    NMPI_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
           (*win_ptr)->rank_mapping, sizeof(uint32_t), MPI_BYTE,
          comm_ptr->handle);
#endif /* _OSU_PSM_ */
        
#if defined(_OSU_MVAPICH_)
    (*win_ptr)->my_id = rank;
    (*win_ptr)->comm_size = comm_size;
    /* -- OSU-MPI2 uses extended CH3 interface */
    if (comm_ptr->comm_kind != MPID_INTRACOMM)
    {
	/* Intercomm is not well supported currently,
	 * fall back to pt2pt implementation if we use inter
	 * communicator */
	(*win_ptr)->fall_back = 1;
#if defined (_SMP_LIMIC_) && !defined (DAPL_DEFAULT_PROVIDER)
	(*win_ptr)->limic_fallback = 1;
#endif /* _SMP_LIMIC_ && !DAPL_DEFAULT_PROVIDER */
    }
    else 
    {
	(*win_ptr)->fall_back = 0;
	MPIDI_CH3I_RDMA_win_create(base, size, comm_size, 
                        rank, win_ptr, comm_ptr);
#if defined (_SMP_LIMIC_) && !defined (DAPL_DEFAULT_PROVIDER)
        (*win_ptr)->limic_fallback = 0;
        MPIDI_CH3I_LIMIC_win_create(base, size, comm_size, 
                        rank, win_ptr, comm_ptr);
#endif /* _SMP_LIMIC_ && !DAPL_DEFAULT_PROVIDER */
    }
#endif /* defined(_OSU_MVAPICH_) */

#if defined (_OSU_PSM_)
    /* call psm to pre-post receive buffers for Puts */
    psm_prepost_1sc();
    NMPI_Barrier((*win_ptr)->comm);
#endif
    
 fn_exit:
    MPIR_Nest_decr();
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_CREATE);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}




#undef FUNCNAME
#define FUNCNAME MPIDI_Win_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_free(MPID_Win **win_ptr)
{
    int mpi_errno=MPI_SUCCESS, total_pt_rma_puts_accs, i, *recvcnts, comm_size;
    MPID_Comm *comm_ptr;
    int in_use;
    MPIU_CHKLMEM_DECL(1);
    MPIU_THREADPRIV_DECL;
    
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_FREE);
        
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_FREE);
        
    MPIU_THREADPRIV_GET;
    MPIR_Nest_incr();

    /* set up the recvcnts array for the reduce scatter to check if all
       passive target rma operations are done */
    MPID_Comm_get_ptr( (*win_ptr)->comm, comm_ptr );
    comm_size = comm_ptr->local_size;
        
    MPIU_CHKLMEM_MALLOC(recvcnts, int *, comm_size*sizeof(int), mpi_errno, 
			"recvcnts");
    for (i=0; i<comm_size; i++)  recvcnts[i] = 1;
        
    mpi_errno = NMPI_Reduce_scatter((*win_ptr)->pt_rma_puts_accs, 
				    &total_pt_rma_puts_accs, recvcnts, 
				    MPI_INT, MPI_SUM, (*win_ptr)->comm);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

    if (total_pt_rma_puts_accs != (*win_ptr)->my_pt_rma_puts_accs)
    {
	MPID_Progress_state progress_state;
            
	/* poke the progress engine until the two are equal */
	MPID_Progress_start(&progress_state);
	while (total_pt_rma_puts_accs != (*win_ptr)->my_pt_rma_puts_accs)
	{
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
	    }
	    /* --END ERROR HANDLING-- */
	}
	MPID_Progress_end(&progress_state);
    }

#if defined(_OSU_MVAPICH_)
    if ((*win_ptr)->fall_back != 1) {
	MPIDI_CH3I_RDMA_finish_rma(*win_ptr);
	MPIDI_CH3I_RDMA_win_free(win_ptr);
    }
#if defined(_SMP_LIMIC_) && !defined(DAPL_DEFAULT_PROVIDER)
    if (!(*win_ptr)->limic_fallback)
    {
        MPIDI_CH3I_LIMIC_win_free(win_ptr);
    }
#endif /* _SMP_LIMIC_ && !_DAPL_DEFAULT_PROVIDER */
#endif /* defined(_OSU_MVAPICH_) */

#if defined (_OSU_PSM_)
    MPIU_Free((*win_ptr)->rank_mapping);
#endif /* _OSU_PSM_ */    

    NMPI_Comm_free(&((*win_ptr)->comm));

    MPIU_Free((*win_ptr)->base_addrs);
    MPIU_Free((*win_ptr)->disp_units);
    MPIU_Free((*win_ptr)->all_win_handles);
    MPIU_Free((*win_ptr)->pt_rma_puts_accs);

    MPIU_Object_release_ref(*win_ptr, &in_use);
    /* MPI windows don't have reference count semantics, so this should always be true */
    MPIU_Assert(!in_use);
    MPIU_Handle_obj_free( &MPID_Win_mem, *win_ptr );

 fn_exit:
    MPIR_Nest_decr();
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_FREE);
    return mpi_errno;

 fn_fail:
    goto fn_exit;
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Put
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Put(void *origin_addr, int origin_count, MPI_Datatype
            origin_datatype, int target_rank, MPI_Aint target_disp,
            int target_count, MPI_Datatype target_datatype, MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int dt_contig, rank, predefined;
    MPIDI_RMA_ops *curr_ptr, *prev_ptr, *new_ptr;
    MPID_Datatype *dtp;
    MPI_Aint dt_true_lb;
    MPIDI_msg_sz_t data_sz;
    MPIU_CHKPMEM_DECL(1);
    MPIU_THREADPRIV_DECL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PUT);
        
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_PUT);

    MPIU_THREADPRIV_GET;
    MPIDI_Datatype_get_info(origin_count, origin_datatype,
			    dt_contig, data_sz, dtp,dt_true_lb); 
    
    if ((data_sz == 0) || (target_rank == MPI_PROC_NULL))
    {
	goto fn_exit;
    }

    /* FIXME: It makes sense to save the rank (and size) of the
       communicator in the window structure to speed up these operations,
       or to save a pointer to the communicator structure, rather than
       just the handle 
    */
    MPIR_Nest_incr();
    NMPI_Comm_rank(win_ptr->comm, &rank);
    MPIR_Nest_decr();
    
    /* If the put is a local operation, do it here */
    if (target_rank == rank)
    {
	mpi_errno = MPIR_Localcopy(origin_addr, origin_count, origin_datatype,
				   (char *) win_ptr->base + win_ptr->disp_unit *
				   target_disp, target_count, target_datatype); 
    }
    else
    {
	/* queue it up */
	curr_ptr = win_ptr->rma_ops_list;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}

	/* FIXME: Where does this memory get freed? */
	MPIU_CHKPMEM_MALLOC(new_ptr, MPIDI_RMA_ops *, sizeof(MPIDI_RMA_ops), 
			    mpi_errno, "RMA operation entry");
	if (prev_ptr != NULL)
	    prev_ptr->next = new_ptr;
	else 
	    win_ptr->rma_ops_list = new_ptr;
	
	new_ptr->next = NULL;  
	new_ptr->type = MPIDI_RMA_PUT;
	new_ptr->origin_addr = origin_addr;
	new_ptr->origin_count = origin_count;
	new_ptr->origin_datatype = origin_datatype;
	new_ptr->target_rank = target_rank;
	new_ptr->target_disp = target_disp;
	new_ptr->target_count = target_count;
	new_ptr->target_datatype = target_datatype;
	
	/* if source or target datatypes are derived, increment their
	   reference counts */ 
	MPIDI_CH3I_DATATYPE_IS_PREDEFINED(origin_datatype, predefined);
	if (!predefined)
	{
	    MPID_Datatype_get_ptr(origin_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
	}
	MPIDI_CH3I_DATATYPE_IS_PREDEFINED(target_datatype, predefined);
	if (!predefined)
	{
	    MPID_Datatype_get_ptr(target_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
	}
    }

#if defined(_OSU_MVAPICH_) && !defined(_SCHEDULE)
    if (win_ptr->fall_back != 1 && win_ptr->using_lock != 1) {
        MPIDI_CH3I_RDMA_try_rma(win_ptr, &win_ptr->rma_ops_list, 0);
    }
#endif /* defined(_OSU_MVAPICH) && !defined(_SCHEDULE) */

  fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_PUT);    
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Get(void *origin_addr, int origin_count, MPI_Datatype
            origin_datatype, int target_rank, MPI_Aint target_disp,
            int target_count, MPI_Datatype target_datatype, MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_msg_sz_t data_sz;
    int dt_contig, rank, predefined;
    MPI_Aint dt_true_lb;
    MPIDI_RMA_ops *curr_ptr, *prev_ptr, *new_ptr;
    MPID_Datatype *dtp;
    MPIU_CHKPMEM_DECL(1);
    MPIU_THREADPRIV_DECL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_GET);
        
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_GET);

    MPIU_THREADPRIV_GET;
    MPIDI_Datatype_get_info(origin_count, origin_datatype,
			    dt_contig, data_sz, dtp, dt_true_lb); 

    if ((data_sz == 0) || (target_rank == MPI_PROC_NULL))
    {
	goto fn_exit;
    }

    /* FIXME: It makes sense to save the rank (and size) of the
       communicator in the window structure to speed up these operations */
    MPIR_Nest_incr();
    NMPI_Comm_rank(win_ptr->comm, &rank);
    MPIR_Nest_decr();
    
    /* If the get is a local operation, do it here */
    if (target_rank == rank)
    {
	mpi_errno = MPIR_Localcopy((char *) win_ptr->base +
				   win_ptr->disp_unit * target_disp,
				   target_count, target_datatype,
				   origin_addr, origin_count,
				   origin_datatype);  
    }
    else
    {
	/* queue it up */
	curr_ptr = win_ptr->rma_ops_list;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}
	
	MPIU_CHKPMEM_MALLOC(new_ptr, MPIDI_RMA_ops *, sizeof(MPIDI_RMA_ops), 
			    mpi_errno, "RMA operation entry");
	if (prev_ptr != NULL)
	{
	    prev_ptr->next = new_ptr;
	}
	else
	{
	    win_ptr->rma_ops_list = new_ptr;
	}
            
	new_ptr->next = NULL;  
	new_ptr->type = MPIDI_RMA_GET;
	new_ptr->origin_addr = origin_addr;
	new_ptr->origin_count = origin_count;
	new_ptr->origin_datatype = origin_datatype;
	new_ptr->target_rank = target_rank;
	new_ptr->target_disp = target_disp;
	new_ptr->target_count = target_count;
	new_ptr->target_datatype = target_datatype;
	
	/* if source or target datatypes are derived, increment their
	   reference counts */ 
	MPIDI_CH3I_DATATYPE_IS_PREDEFINED(origin_datatype, predefined);
	if (!predefined)
	{
	    MPID_Datatype_get_ptr(origin_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
	}
	MPIDI_CH3I_DATATYPE_IS_PREDEFINED(target_datatype, predefined);
	if (!predefined)
	{
	    MPID_Datatype_get_ptr(target_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
    }
    }

#if defined(_OSU_MVAPICH) && !defined(_SCHEDULE)
    if (win_ptr->fall_back != 1 && win_ptr->using_lock !=1) {
        MPIDI_CH3I_RDMA_try_rma(win_ptr, &win_ptr->rma_ops_list, 0);
    }
#endif /* defined(_OSU_MVAPICH_) && !defined(_SCHEDULE) */

  fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_GET);
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Accumulate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Accumulate(void *origin_addr, int origin_count, MPI_Datatype
                    origin_datatype, int target_rank, MPI_Aint target_disp,
                    int target_count, MPI_Datatype target_datatype, MPI_Op op,
                    MPID_Win *win_ptr)
{
    int nest_level_inc = FALSE;
    int mpi_errno=MPI_SUCCESS;
    MPIDI_msg_sz_t data_sz;
    int dt_contig, rank, origin_predefined, target_predefined;
    MPI_Aint dt_true_lb;
    MPIDI_RMA_ops *curr_ptr, *prev_ptr, *new_ptr;
    MPID_Datatype *dtp;
    MPIU_CHKLMEM_DECL(2);
    MPIU_CHKPMEM_DECL(1);
    MPIU_THREADPRIV_DECL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_ACCUMULATE);
    
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_ACCUMULATE);

    MPIU_THREADPRIV_GET;
    MPIDI_Datatype_get_info(origin_count, origin_datatype,
			    dt_contig, data_sz, dtp, dt_true_lb);  
    
    if ((data_sz == 0) || (target_rank == MPI_PROC_NULL))
    {
	goto fn_exit;
    }
    
    MPIR_Nest_incr();
    nest_level_inc = TRUE;
    
    /* FIXME: It makes sense to save the rank (and size) of the
       communicator in the window structure to speed up these operations,
       or to save a pointer to the communicator structure, rather than
       just the handle 
    */
    NMPI_Comm_rank(win_ptr->comm, &rank);
    
    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(origin_datatype, origin_predefined);
    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(target_datatype, target_predefined);

    if (target_rank == rank)
    {
	MPI_User_function *uop;
	
	if (op == MPI_REPLACE)
	{
	    mpi_errno = MPIR_Localcopy(origin_addr, origin_count, 
				origin_datatype,
				(char *) win_ptr->base + win_ptr->disp_unit *
				target_disp, target_count, target_datatype); 
	    goto fn_exit;
	}
	
	MPIU_ERR_CHKANDJUMP1((HANDLE_GET_KIND(op) != HANDLE_KIND_BUILTIN), 
			     mpi_errno, MPI_ERR_OP, "**opnotpredefined",
			     "**opnotpredefined %d", op );
	
	/* get the function by indexing into the op table */
	uop = MPIR_Op_table[(op)%16 - 1];
	
	if (origin_predefined && target_predefined)
	{    
	    (*uop)(origin_addr, (char *) win_ptr->base + win_ptr->disp_unit *
		   target_disp, &target_count, &target_datatype);
	}
	else
	{
	    /* derived datatype */
	    
	    MPID_Segment *segp;
	    DLOOP_VECTOR *dloop_vec;
	    MPI_Aint first, last;
	    int vec_len, i, type_size, count;
	    MPI_Datatype type;
	    MPI_Aint true_lb, true_extent, extent;
	    void *tmp_buf=NULL, *source_buf, *target_buf;
	    
	    if (origin_datatype != target_datatype)
	    {
		/* first copy the data into a temporary buffer with
		   the same datatype as the target. Then do the
		   accumulate operation. */
		
		mpi_errno = NMPI_Type_get_true_extent(target_datatype, 
						      &true_lb, &true_extent);
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
		
		MPID_Datatype_get_extent_macro(target_datatype, extent); 
		
		MPIU_CHKLMEM_MALLOC(tmp_buf, void *, 
			target_count * (MPIR_MAX(extent,true_extent)), 
			mpi_errno, "temporary buffer");
		/* adjust for potential negative lower bound in datatype */
		tmp_buf = (void *)((char*)tmp_buf - true_lb);
		
		mpi_errno = MPIR_Localcopy(origin_addr, origin_count,
					   origin_datatype, tmp_buf,
					   target_count, target_datatype);  
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    }

	    if (target_predefined) { 
		/* target predefined type, origin derived datatype */

		(*uop)(tmp_buf, (char *) win_ptr->base + win_ptr->disp_unit *
		   target_disp, &target_count, &target_datatype);
	    }
	    else {
	    
		segp = MPID_Segment_alloc();
		MPIU_ERR_CHKANDJUMP((!segp), mpi_errno, MPI_ERR_OTHER, "**nomem"); 
		MPID_Segment_init(NULL, target_count, target_datatype, segp, 0);
		first = 0;
		last  = SEGMENT_IGNORE_LAST;
		
		MPID_Datatype_get_ptr(target_datatype, dtp);
		vec_len = dtp->max_contig_blocks * target_count + 1; 
		/* +1 needed because Rob says so */
		MPIU_CHKLMEM_MALLOC(dloop_vec, DLOOP_VECTOR *, 
				    vec_len * sizeof(DLOOP_VECTOR), 
				    mpi_errno, "dloop vector");
		
		MPID_Segment_pack_vector(segp, first, &last, dloop_vec, &vec_len);
		
		source_buf = (tmp_buf != NULL) ? tmp_buf : origin_addr;
		target_buf = (char *) win_ptr->base + 
		    win_ptr->disp_unit * target_disp;
		type = dtp->eltype;
		type_size = MPID_Datatype_get_basic_size(type);
		for (i=0; i<vec_len; i++)
		{
		    count = (dloop_vec[i].DLOOP_VECTOR_LEN)/type_size;
		    (*uop)((char *)source_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
			   (char *)target_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
			   &count, &type);
		}
		
		MPID_Segment_free(segp);
	    }
	}
    }
    else
    {
	/* queue it up */
	curr_ptr = win_ptr->rma_ops_list;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}
	
	MPIU_CHKPMEM_MALLOC(new_ptr, MPIDI_RMA_ops *, sizeof(MPIDI_RMA_ops), 
			    mpi_errno, "RMA operation entry");
	if (prev_ptr != NULL)
	{
	    prev_ptr->next = new_ptr;
	}
	else
	{
	    win_ptr->rma_ops_list = new_ptr;
	}
        
	new_ptr->next = NULL;  
	new_ptr->type = MPIDI_RMA_ACCUMULATE;
	new_ptr->origin_addr = origin_addr;
	new_ptr->origin_count = origin_count;
	new_ptr->origin_datatype = origin_datatype;
	new_ptr->target_rank = target_rank;
	new_ptr->target_disp = target_disp;
	new_ptr->target_count = target_count;
	new_ptr->target_datatype = target_datatype;
	new_ptr->op = op;
	
	/* if source or target datatypes are derived, increment their
	   reference counts */ 
	if (!origin_predefined)
	{
	    MPID_Datatype_get_ptr(origin_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
	}
	if (!target_predefined)
	{
	    MPID_Datatype_get_ptr(target_datatype, dtp);
	    MPID_Datatype_add_ref(dtp);
	}
    }

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    if (nest_level_inc)
    { 
	MPIR_Nest_decr();
    }
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_ACCUMULATE);
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
  fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_Alloc_mem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void *MPIDI_Alloc_mem( size_t size, MPID_Info *info_ptr )
{
    void *ap;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_ALLOC_MEM);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_ALLOC_MEM);

    ap = MPIU_Malloc(size);
    
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_ALLOC_MEM);
    return ap;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_Free_mem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Free_mem( void *ptr )
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_FREE_MEM);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_FREE_MEM);

    MPIU_Free(ptr);
    
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_FREE_MEM);
    return mpi_errno;
}
