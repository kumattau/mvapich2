/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"

static int create_derived_datatype(MPID_Request * rreq, MPID_Datatype ** dtp);
static int do_accumulate_op(MPID_Request * rreq);
static int do_simple_accumulate(MPIDI_PT_single_op *single_op);
static int do_simple_get(MPID_Win *win_ptr, MPIDI_Win_lock_queue *lock_queue);

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Handle_recv_req
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Handle_recv_req(MPIDI_VC_t * vc, MPID_Request * rreq, int * complete)
{
    static int in_routine = FALSE;
    MPID_Win *win_ptr;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3U_HANDLE_RECV_REQ);
    MPIDI_STATE_DECL(MPID_STATE_CH3_CA_COMPLETE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3U_HANDLE_RECV_REQ);

    MPIU_Assert(in_routine == FALSE);
    in_routine = TRUE;
    
    switch(rreq->dev.ca)
    {
	case MPIDI_CH3_CA_COMPLETE:
	{
	    MPIDI_FUNC_ENTER(MPID_STATE_CH3_CA_COMPLETE)
	    /* FIXME: put ONC operations into their own completion action */
	    
	    if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_RECV)
	    {
                /* mark data transfer as complete and decrement CC */
		MPIDI_CH3U_Request_complete(rreq);
		*complete = TRUE;
	    }
            else if ((MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PUT_RESP) ||
                     (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RESP))
	    {
                if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RESP) {
                    /* accumulate data from tmp_buf into user_buf */
                    mpi_errno = do_accumulate_op(rreq);
                    if (mpi_errno) {
			MPIU_ERR_POP(mpi_errno);
                    }
                }

                MPID_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);
                
                /* if passive target RMA, increment counter */
                if (win_ptr->current_lock_type != MPID_LOCK_NONE)
                    win_ptr->my_pt_rma_puts_accs++;

                if (rreq->dev.source_win_handle != MPI_WIN_NULL) {
                    /* Last RMA operation from source. If active
                       target RMA, decrement window counter. If
                       passive target RMA, release lock on window and
                       grant next lock in the lock queue if there is
                       any. If it's a shared lock or a lock-put-unlock
                       type of optimization, we also need to send an
                       ack to the source. */ 

                    if (win_ptr->current_lock_type == MPID_LOCK_NONE) {
                        /* FIXME: MT: this has to be done atomically */
                        win_ptr->my_counter -= 1;
                    }
                    else {
                        if ((win_ptr->current_lock_type == MPI_LOCK_SHARED) ||
                            (rreq->dev.single_op_opt == 1)) {
                            mpi_errno = MPIDI_CH3I_Send_pt_rma_done_pkt(vc, 
                                                  rreq->dev.source_win_handle);
			    if (mpi_errno) {
				MPIU_ERR_POP(mpi_errno);
			    }
                        }
                        mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
                    }
                }
		
                /* mark data transfer as complete and decrement CC */
		MPIDI_CH3U_Request_complete(rreq);
		*complete = TRUE;
            }

            else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PUT_RESP_DERIVED_DT)
	    {
                MPID_Datatype *new_dtp;
                
                /* create derived datatype */
                create_derived_datatype(rreq, &new_dtp);

                /* update request to get the data */
                MPIDI_Request_set_type(rreq, MPIDI_REQUEST_TYPE_PUT_RESP);
                rreq->dev.datatype = new_dtp->handle;
                rreq->dev.recv_data_sz = new_dtp->size *
                                           rreq->dev.user_count; 
                
                rreq->dev.datatype_ptr = new_dtp;
                /* this will cause the datatype to be freed when the
                   request is freed. free dtype_info here. */
                MPIU_Free(rreq->dev.dtype_info);

                MPID_Segment_init(rreq->dev.user_buf,
                                  rreq->dev.user_count,
                                  rreq->dev.datatype,
                                  &rreq->dev.segment, 0);
                rreq->dev.segment_first = 0;
                rreq->dev.segment_size = rreq->dev.recv_data_sz;

                mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
                if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
					"**ch3|loadrecviov");
                }

		*complete = FALSE;
            }
            else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RESP_DERIVED_DT)
	    {
                MPID_Datatype *new_dtp;
                MPI_Aint true_lb, true_extent, extent;
                void *tmp_buf;
               
                /* create derived datatype */
                create_derived_datatype(rreq, &new_dtp);

                /* update new request to get the data */
                MPIDI_Request_set_type(rreq, MPIDI_REQUEST_TYPE_ACCUM_RESP);

                /* first need to allocate tmp_buf to recv the data into */

		MPIR_Nest_incr();
                mpi_errno = NMPI_Type_get_true_extent(new_dtp->handle, 
                                                      &true_lb, &true_extent);
		MPIR_Nest_decr();
                if (mpi_errno) {
		    MPIU_ERR_POP(mpi_errno);
		}

                MPID_Datatype_get_extent_macro(new_dtp->handle, extent); 

                tmp_buf = MPIU_Malloc(rreq->dev.user_count * 
                                      (MPIR_MAX(extent,true_extent)));  
		/* --BEGIN ERROR HANDLING-- */
                if (!tmp_buf)
		{
                    mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER,
						      "**nomem", 0 );
                    goto fn_fail;
                }
		/* --END ERROR HANDLING-- */

                /* adjust for potential negative lower bound in datatype */
                tmp_buf = (void *)((char*)tmp_buf - true_lb);

                rreq->dev.user_buf = tmp_buf;
                rreq->dev.datatype = new_dtp->handle;
                rreq->dev.recv_data_sz = new_dtp->size *
                                           rreq->dev.user_count; 
                rreq->dev.datatype_ptr = new_dtp;
                /* this will cause the datatype to be freed when the
                   request is freed. free dtype_info here. */
                MPIU_Free(rreq->dev.dtype_info);

                MPID_Segment_init(rreq->dev.user_buf,
                                  rreq->dev.user_count,
                                  rreq->dev.datatype,
                                  &rreq->dev.segment, 0);
                rreq->dev.segment_first = 0;
                rreq->dev.segment_size = rreq->dev.recv_data_sz;

                mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
                if (mpi_errno != MPI_SUCCESS) {
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,
					"**ch3|loadrecviov");
                }

		*complete = FALSE;
            }
            else if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_GET_RESP_DERIVED_DT)
	    {
                MPID_Datatype *new_dtp;
                MPIDI_CH3_Pkt_t upkt;
                MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt = &upkt.get_resp;
                MPID_IOV iov[MPID_IOV_LIMIT];
		MPID_Request * sreq;
                int iov_n;
                
                /* create derived datatype */
                create_derived_datatype(rreq, &new_dtp);
                MPIU_Free(rreq->dev.dtype_info);

                /* create request for sending data */
		sreq = MPID_Request_create();
                if (sreq == NULL) {
                    /* --BEGIN ERROR HANDLING-- */
                    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                    goto fn_exit;
                    /* --END ERROR HANDLING-- */
                }
                sreq->kind = MPID_REQUEST_SEND;
                MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_GET_RESP);
                sreq->dev.user_buf = rreq->dev.user_buf;
                sreq->dev.user_count = rreq->dev.user_count;
                sreq->dev.datatype = new_dtp->handle;
                sreq->dev.datatype_ptr = new_dtp;
		sreq->dev.target_win_handle = rreq->dev.target_win_handle;
		sreq->dev.source_win_handle = rreq->dev.source_win_handle;
		
                MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
                get_resp_pkt->request_handle = rreq->dev.request_handle;
                
                iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) get_resp_pkt;
                iov[0].MPID_IOV_LEN = sizeof(*get_resp_pkt);

                MPID_Segment_init(sreq->dev.user_buf,
                                  sreq->dev.user_count,
                                  sreq->dev.datatype,
                                  &sreq->dev.segment, 0);
                sreq->dev.segment_first = 0;
		sreq->dev.segment_size = new_dtp->size * sreq->dev.user_count;

                iov_n = MPID_IOV_LIMIT - 1;
                mpi_errno = MPIDI_CH3U_Request_load_send_iov(sreq, &iov[1], &iov_n);
                if (mpi_errno == MPI_SUCCESS)
                {
                    iov_n += 1;
		
                    mpi_errno = MPIDI_CH3_iSendv(vc, sreq, iov, iov_n);
		    /* --BEGIN ERROR HANDLING-- */
                    if (mpi_errno != MPI_SUCCESS)
                    {
                        MPIU_Object_set_ref(sreq, 0);
                        MPIDI_CH3_Request_destroy(sreq);
                        sreq = NULL;
                        mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
							 "**ch3|rmamsg", 0);
                        goto fn_exit;
                    }
		    /* --END ERROR HANDLING-- */
                }

                /* mark receive data transfer as complete and decrement CC in receive request */
		MPIDI_CH3U_Request_complete(rreq);
		*complete = TRUE;
            }
            else if ((MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PT_SINGLE_PUT) ||
                     (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PT_SINGLE_ACCUM))
	    {
                /* received all the data for single lock-put(accum)-unlock 
                   optimization where the lock was not acquired in 
                   ch3u_handle_recv_pkt. Try to acquire the lock and do the 
                   operation. */

                MPID_Win *win_ptr;
                MPIDI_Win_lock_queue *lock_queue_entry, *curr_ptr, **curr_ptr_ptr;

                MPID_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);

                lock_queue_entry = rreq->dev.lock_queue_entry;
                
                if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, 
                                             lock_queue_entry->lock_type) == 1)
                {

                    if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PT_SINGLE_PUT) {
                        /* copy the data over */
                        mpi_errno = MPIR_Localcopy(rreq->dev.user_buf,
                                                   rreq->dev.user_count,
                                                   rreq->dev.datatype,
                                         lock_queue_entry->pt_single_op->addr,
                                         lock_queue_entry->pt_single_op->count,
                                     lock_queue_entry->pt_single_op->datatype);
                    }
                    else {
                        mpi_errno = do_simple_accumulate(lock_queue_entry->pt_single_op);
                    }

		    if (mpi_errno) {
			MPIU_ERR_POP(mpi_errno);
		    }

                    /* increment counter */
                    win_ptr->my_pt_rma_puts_accs++;

                    /* send done packet */
                    mpi_errno = MPIDI_CH3I_Send_pt_rma_done_pkt(vc, 
                                         lock_queue_entry->source_win_handle);
		    if (mpi_errno) {
			MPIU_ERR_POP(mpi_errno);
		    }

                    /* free lock_queue_entry including data buffer and remove 
                       it from the queue. */
                    curr_ptr = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
                    curr_ptr_ptr = (MPIDI_Win_lock_queue **) &(win_ptr->lock_queue);
                    while (curr_ptr != lock_queue_entry) {
                        curr_ptr_ptr = &(curr_ptr->next);
                        curr_ptr = curr_ptr->next;
                    }                    
                    *curr_ptr_ptr = curr_ptr->next;

                    MPIU_Free(lock_queue_entry->pt_single_op->data);
                    MPIU_Free(lock_queue_entry->pt_single_op);
                    MPIU_Free(lock_queue_entry);

                    /* Release lock and grant next lock if there is one. */
                    mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
                }
                else {
                    /* could not acquire lock. mark data recd as 1 */
                    lock_queue_entry->pt_single_op->data_recd = 1;
                }

                /* mark data transfer as complete and decrement CC */
		MPIDI_CH3U_Request_complete(rreq);
		*complete = TRUE;
            }
	    /* --BEGIN ERROR HANDLING-- */
	    else
	    {
		/* We shouldn't reach this code because the only other request types are sends */
		MPIU_Assert(MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_RECV);
		MPIDI_CH3U_Request_complete(rreq);
		*complete = TRUE;
	    }
	    /* --END ERROR HANDLING-- */
	    
	    MPIDI_FUNC_EXIT(MPID_STATE_CH3_CA_COMPLETE)
	    break;
	}
	
	case MPIDI_CH3_CA_UNPACK_UEBUF_AND_COMPLETE:
	{
	    int recv_pending;
	    
            MPIDI_Request_recv_pending(rreq, &recv_pending);
	    if (!recv_pending)
	    { 
		if (rreq->dev.recv_data_sz > 0)
		{
		    MPIDI_CH3U_Request_unpack_uebuf(rreq);
		    MPIU_Free(rreq->dev.tmpbuf);
		}
	    }
	    else
	    {
		/* The receive has not been posted yet.  MPID_{Recv/Irecv}() is responsible for unpacking the buffer. */
	    }
	    
	    /* mark data transfer as complete and decrement CC */
	    MPIDI_CH3U_Request_complete(rreq);
	    *complete = TRUE;
	    
	    break;
	}
	
	case MPIDI_CH3_CA_UNPACK_SRBUF_AND_COMPLETE:
	{
	    MPIDI_CH3U_Request_unpack_srbuf(rreq);

            if ((MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_PUT_RESP) ||
               (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RESP))

	    {
                if (MPIDI_Request_get_type(rreq) == MPIDI_REQUEST_TYPE_ACCUM_RESP) {
                    /* accumulate data from tmp_buf into user_buf */
                    mpi_errno = do_accumulate_op(rreq);
		    if (mpi_errno) {
			MPIU_ERR_POP(mpi_errno);
		    }
                }

                MPID_Win_get_ptr(rreq->dev.target_win_handle, win_ptr);
                
                /* if passive target RMA, increment counter */
                if (win_ptr->current_lock_type != MPID_LOCK_NONE)
                    win_ptr->my_pt_rma_puts_accs++;
                
                if (rreq->dev.source_win_handle != MPI_WIN_NULL) {
                    /* Last RMA operation from source. If active
                       target RMA, decrement window counter. If
                       passive target RMA, release lock on window and
                       grant next lock in the lock queue if there is
                       any. If it's a shared lock or a lock-put-unlock
                       type of optimization, we also need to send an
                       ack to the source. */ 
                    
                    if (win_ptr->current_lock_type == MPID_LOCK_NONE) {
                        /* FIXME: MT: this has to be done atomically */
                        win_ptr->my_counter -= 1;
                    }
                    else {
                        if ((win_ptr->current_lock_type == MPI_LOCK_SHARED) ||
                            (rreq->dev.single_op_opt == 1)) {
                            mpi_errno = MPIDI_CH3I_Send_pt_rma_done_pkt(vc, 
                                                          rreq->dev.source_win_handle);
			    if (mpi_errno) {
				MPIU_ERR_POP(mpi_errno);
			    }
                        }
                        mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
                    }
                }
            }

	    /* mark data transfer as complete and decrement CC */
	    MPIDI_CH3U_Request_complete(rreq);
	    *complete = TRUE;
	    
	    break;
	}
	
	case MPIDI_CH3_CA_UNPACK_SRBUF_AND_RELOAD_IOV:
	{
	    MPIDI_CH3U_Request_unpack_srbuf(rreq);
	    mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|loadrecviov",
						 "**ch3|loadrecviov %s", "MPIDI_CH3_CA_UNPACK_SRBUF_AND_RELOAD_IOV");
		goto fn_fail;
	    }
	    /* --END ERROR HANDLING-- */
	    *complete = FALSE;
	    break;
	}
	
	case MPIDI_CH3_CA_RELOAD_IOV:
	{
	    mpi_errno = MPIDI_CH3U_Request_load_recv_iov(rreq);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
	    {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**ch3|loadrecviov",
						 "**ch3|loadrecviov %s", "MPIDI_CH3_CA_RELOAD_IOV");
		goto fn_fail;
	    }
	    /* --END ERROR HANDLING-- */
	    *complete = FALSE;
	    break;
	}

	/* --BEGIN ERROR HANDLING-- */
	default:
	{
	    *complete = TRUE;
	    mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN, "**ch3|badca",
					     "**ch3|badca %d", rreq->dev.ca);
	    break;
	}
	/* --END ERROR HANDLING-- */
    }

  fn_exit:
    in_routine = FALSE;
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3U_HANDLE_RECV_REQ);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}



#undef FUNCNAME
#define FUNCNAME create_derived_datatype
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int create_derived_datatype(MPID_Request *req, MPID_Datatype **dtp)
{
    MPIDI_RMA_dtype_info *dtype_info;
    void *dataloop;
    MPID_Datatype *new_dtp;
    int mpi_errno=MPI_SUCCESS;
    MPI_Aint ptrdiff;

    dtype_info = req->dev.dtype_info;
    dataloop = req->dev.dataloop;

    /* allocate new datatype object and handle */
    new_dtp = (MPID_Datatype *) MPIU_Handle_obj_alloc(&MPID_Datatype_mem);
    /* --BEGIN ERROR HANDLING-- */
    if (!new_dtp)
    {
        mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    *dtp = new_dtp;
            
    /* Note: handle is filled in by MPIU_Handle_obj_alloc() */
    MPIU_Object_set_ref(new_dtp, 1);
    new_dtp->is_permanent = 0;
    new_dtp->is_committed = 1;
    new_dtp->attributes   = 0;
    new_dtp->cache_id     = 0;
    new_dtp->name[0]      = 0;
    new_dtp->is_contig = dtype_info->is_contig;
    new_dtp->n_contig_blocks = dtype_info->n_contig_blocks; 
    new_dtp->size = dtype_info->size;
    new_dtp->extent = dtype_info->extent;
    new_dtp->dataloop_size = dtype_info->dataloop_size;
    new_dtp->dataloop_depth = dtype_info->dataloop_depth; 
    new_dtp->eltype = dtype_info->eltype;
    /* set dataloop pointer */
    new_dtp->dataloop = req->dev.dataloop;
    
    new_dtp->ub = dtype_info->ub;
    new_dtp->lb = dtype_info->lb;
    new_dtp->true_ub = dtype_info->true_ub;
    new_dtp->true_lb = dtype_info->true_lb;
    new_dtp->has_sticky_ub = dtype_info->has_sticky_ub;
    new_dtp->has_sticky_lb = dtype_info->has_sticky_lb;
    /* update pointers in dataloop */
    ptrdiff = (MPI_Aint)((char *) (new_dtp->dataloop) - (char *)
                         (dtype_info->dataloop));
    
    MPID_Dataloop_update(new_dtp->dataloop, ptrdiff);

    new_dtp->contents = NULL;

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME do_accumulate_op
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int do_accumulate_op(MPID_Request *rreq)
{
    int mpi_errno = MPI_SUCCESS;
    MPI_Aint true_lb, true_extent;
    MPI_User_function *uop;

    if (rreq->dev.op == MPI_REPLACE)
    {
        /* simply copy the data */
        mpi_errno = MPIR_Localcopy(rreq->dev.user_buf, rreq->dev.user_count,
                                   rreq->dev.datatype,
                                   rreq->dev.real_user_buf,
                                   rreq->dev.user_count,
                                   rreq->dev.datatype);
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        goto fn_exit;
    }

    if (HANDLE_GET_KIND(rreq->dev.op) == HANDLE_KIND_BUILTIN)
    {
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[(rreq->dev.op)%16 - 1];
    }
    else
    {
	/* --BEGIN ERROR HANDLING-- */
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OP, "**opnotpredefined", "**opnotpredefined %d", rreq->dev.op );
        return mpi_errno;
	/* --END ERROR HANDLING-- */
    }
    
    if (HANDLE_GET_KIND(rreq->dev.datatype) == HANDLE_KIND_BUILTIN)
    {
        (*uop)(rreq->dev.user_buf, rreq->dev.real_user_buf,
               &(rreq->dev.user_count), &(rreq->dev.datatype));
    }
    else
    {
	/* derived datatype */
        MPID_Segment *segp;
        DLOOP_VECTOR *dloop_vec;
        MPI_Aint first, last;
        int vec_len, i, type_size, count;
        MPI_Datatype type;
        MPID_Datatype *dtp;
        
        segp = MPID_Segment_alloc();
	/* --BEGIN ERROR HANDLING-- */
        if (!segp)
	{
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 ); 
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        MPID_Segment_init(NULL, rreq->dev.user_count,
			  rreq->dev.datatype, segp, 0);
        first = 0;
        last  = SEGMENT_IGNORE_LAST;
        
        MPID_Datatype_get_ptr(rreq->dev.datatype, dtp);
        vec_len = dtp->n_contig_blocks * rreq->dev.user_count + 1; 
        /* +1 needed because Rob says so */
        dloop_vec = (DLOOP_VECTOR *)
            MPIU_Malloc(vec_len * sizeof(DLOOP_VECTOR));
	/* --BEGIN ERROR HANDLING-- */
        if (!dloop_vec)
	{
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 ); 
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
        
        MPID_Segment_pack_vector(segp, first, &last, dloop_vec, &vec_len);
        
        type = dtp->eltype;
        type_size = MPID_Datatype_get_basic_size(type);
        for (i=0; i<vec_len; i++)
	{
            count = (dloop_vec[i].DLOOP_VECTOR_LEN)/type_size;
            (*uop)((char *)rreq->dev.user_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                   (char *)rreq->dev.real_user_buf + MPIU_PtrToAint(dloop_vec[i].DLOOP_VECTOR_BUF),
                   &count, &type);
        }
        
        MPID_Segment_free(segp);
        MPIU_Free(dloop_vec);
    }

 fn_exit:
    /* free the temporary buffer */
    MPIR_Nest_incr();
    mpi_errno = NMPI_Type_get_true_extent(rreq->dev.datatype, &true_lb, &true_extent);
    MPIR_Nest_decr();
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    
    MPIU_Free((char *) rreq->dev.user_buf + true_lb);

    return mpi_errno;
}



/* Release the current lock on the window and grant the next lock in the
   queue if any */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Release_lock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Release_lock(MPID_Win *win_ptr)
{
    MPIDI_Win_lock_queue *lock_queue, **lock_queue_ptr;
    int requested_lock, mpi_errno = MPI_SUCCESS;

    if (win_ptr->current_lock_type == MPI_LOCK_SHARED) {
        /* decr ref cnt */
        /* FIXME: MT: Must be done atomically */
        win_ptr->shared_lock_ref_cnt--;
    }

    /* If shared lock ref count is 0 (which is also true if the lock is an
       exclusive lock), release the lock. */
    if (win_ptr->shared_lock_ref_cnt == 0) {
        /* FIXME: MT: The setting of the lock type must be done atomically */
        win_ptr->current_lock_type = MPID_LOCK_NONE;

        /* If there is a lock queue, try to satisfy as many lock requests as 
           possible. If the first one is a shared lock, grant it and grant all 
           other shared locks. If the first one is an exclusive lock, grant 
           only that one. */
        
        /* FIXME: MT: All queue accesses need to be made atomic */
        lock_queue = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
        lock_queue_ptr = (MPIDI_Win_lock_queue **) &(win_ptr->lock_queue);
        while (lock_queue) {
            /* if it is not a lock-op-unlock type case or if it is a 
               lock-op-unlock type case but all the data has been received, 
               try to acquire the lock */
            if ((lock_queue->pt_single_op == NULL) || 
                (lock_queue->pt_single_op->data_recd == 1)) {

                requested_lock = lock_queue->lock_type;
                if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, requested_lock) 
                                                                       == 1) {

                    if (lock_queue->pt_single_op != NULL) {
                        /* single op. do it here */
                        MPIDI_PT_single_op * single_op;

                        single_op = lock_queue->pt_single_op;
                        if (single_op->type == MPIDI_RMA_PUT) {
                            mpi_errno = MPIR_Localcopy(single_op->data,
                                                       single_op->count,
                                                       single_op->datatype,
                                                       single_op->addr,
                                                       single_op->count,
                                                       single_op->datatype);
                        }   
                        else if (single_op->type == MPIDI_RMA_ACCUMULATE) {
                            mpi_errno = do_simple_accumulate(single_op);
                        }
                        else if (single_op->type == MPIDI_RMA_GET) {
                            mpi_errno = do_simple_get(win_ptr, lock_queue);
                        }

                        /* --BEGIN ERROR HANDLING-- */
                        if (mpi_errno != MPI_SUCCESS) goto fn_exit;
                        /* --END ERROR HANDLING-- */

                        /* if put or accumulate, send rma done packet and release lock. */
                        if (single_op->type != MPIDI_RMA_GET) {
                            /* increment counter */
                            win_ptr->my_pt_rma_puts_accs++;

                            mpi_errno = 
                               MPIDI_CH3I_Send_pt_rma_done_pkt(lock_queue->vc, 
                                         lock_queue->source_win_handle);
                            /* --BEGIN ERROR HANDLING-- */
                            if (mpi_errno != MPI_SUCCESS) goto fn_exit;
                            /* --END ERROR HANDLING-- */

                            /* release the lock */
                            if (win_ptr->current_lock_type == MPI_LOCK_SHARED) {
                                /* decr ref cnt */
                                /* FIXME: MT: Must be done atomically */
                                win_ptr->shared_lock_ref_cnt--;
                            }
                        
                            /* If shared lock ref count is 0 
                               (which is also true if the lock is an
                               exclusive lock), release the lock. */
                            if (win_ptr->shared_lock_ref_cnt == 0) {
                                /* FIXME: MT: The setting of the lock type 
                                   must be done atomically */
                                win_ptr->current_lock_type = MPID_LOCK_NONE;
                            }

                            /* dequeue entry from lock queue */
                            MPIU_Free(single_op->data);
                            MPIU_Free(single_op);
                            *lock_queue_ptr = lock_queue->next;
                            MPIU_Free(lock_queue);
                            lock_queue = *lock_queue_ptr;
                        }

                        else {
                            /* it's a get. The operation is not complete. It 
                               will be completed in ch3u_handle_send_req.c. 
                               Free the single_op structure. If it's an 
                               exclusive lock, break. Otherwise continue to the
                               next operation. */

                            MPIU_Free(single_op);
                            *lock_queue_ptr = lock_queue->next;
                            MPIU_Free(lock_queue);
                            lock_queue = *lock_queue_ptr;

                            if (requested_lock == MPI_LOCK_EXCLUSIVE)
                                break;
                        }
                    }

                    else {
                        /* send lock granted packet. */
                        mpi_errno = 
                            MPIDI_CH3I_Send_lock_granted_pkt(lock_queue->vc,
                                                lock_queue->source_win_handle);
                    
                        /* dequeue entry from lock queue */
                        *lock_queue_ptr = lock_queue->next;
                        MPIU_Free(lock_queue);
                        lock_queue = *lock_queue_ptr;
                        
                        /* if the granted lock is exclusive, 
                           no need to continue */
                        if (requested_lock == MPI_LOCK_EXCLUSIVE)
                            break;
                    }
                }
            }
            else {
                lock_queue_ptr = &(lock_queue->next);
                lock_queue = lock_queue->next;
            }
        }
    }

 fn_exit:
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_pt_rma_done_pkt
#undef FCNAME 
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Send_pt_rma_done_pkt(MPIDI_VC_t *vc, MPI_Win source_win_handle)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_pt_rma_done_t *pt_rma_done_pkt = &upkt.pt_rma_done;
    MPID_Request *req;
    int mpi_errno=MPI_SUCCESS;

    MPIDI_Pkt_init(pt_rma_done_pkt, MPIDI_CH3_PKT_PT_RMA_DONE);
    pt_rma_done_pkt->source_win_handle = source_win_handle;

    mpi_errno = MPIDI_CH3_iStartMsg(vc, pt_rma_done_pkt,
                                    sizeof(*pt_rma_done_pkt), &req);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS) {
        mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**ch3|rmamsg", 0);
        return mpi_errno;
    }
    /* --END ERROR HANDLING-- */
    if (req != NULL)
    {
        MPID_Request_release(req);
    }

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME do_simple_accumulate
#undef FCNAME 
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int do_simple_accumulate(MPIDI_PT_single_op *single_op)
{
    int mpi_errno = MPI_SUCCESS;
    MPI_User_function *uop;

    if (single_op->op == MPI_REPLACE)
    {
        /* simply copy the data */
        mpi_errno = MPIR_Localcopy(single_op->data, single_op->count,
                                   single_op->datatype, single_op->addr,
                                   single_op->count, single_op->datatype);
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        goto fn_exit;
    }

    if (HANDLE_GET_KIND(single_op->op) == HANDLE_KIND_BUILTIN)
    {
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[(single_op->op)%16 - 1];
    }
    else
    {
	/* --BEGIN ERROR HANDLING-- */
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OP, "**opnotpredefined", "**opnotpredefined %d", single_op->op );
        return mpi_errno;
	/* --END ERROR HANDLING-- */
    }
    
    /* only basic datatypes supported for this optimization. */
    (*uop)(single_op->data, single_op->addr,
           &(single_op->count), &(single_op->datatype));

 fn_exit:
    return mpi_errno;
}



#undef FUNCNAME
#define FUNCNAME do_simple_get
#undef FCNAME 
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int do_simple_get(MPID_Win *win_ptr, MPIDI_Win_lock_queue *lock_queue)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt = &upkt.get_resp;
    MPID_Request *req;
    MPID_IOV iov[MPID_IOV_LIMIT];
    int type_size, mpi_errno=MPI_SUCCESS;

    req = MPID_Request_create();
    if (req == NULL) {
        /* --BEGIN ERROR HANDLING-- */
        mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
        return mpi_errno;
        /* --END ERROR HANDLING-- */
    }
    req->dev.target_win_handle = win_ptr->handle;
    req->dev.source_win_handle = lock_queue->source_win_handle;
    req->dev.single_op_opt = 1;
    req->dev.ca = MPIDI_CH3_CA_COMPLETE;
    
    MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_GET_RESP); 
    req->kind = MPID_REQUEST_SEND;
    
    MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
    get_resp_pkt->request_handle = lock_queue->pt_single_op->request_handle;
    
    iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) get_resp_pkt;
    iov[0].MPID_IOV_LEN = sizeof(*get_resp_pkt);
    
    iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)lock_queue->pt_single_op->addr;
    MPID_Datatype_get_size_macro(lock_queue->pt_single_op->datatype, type_size);
    iov[1].MPID_IOV_LEN = lock_queue->pt_single_op->count * type_size;
    
    mpi_errno = MPIDI_CH3_iSendv(lock_queue->vc, req, iov, 2);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS)
    {
        MPIU_Object_set_ref(req, 0);
        MPIDI_CH3_Request_destroy(req);
        mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER,
                                         "**ch3|rmamsg", 0);
    }
    /* --END ERROR HANDLING-- */

    return mpi_errno;
}
