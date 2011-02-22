/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
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

#include "mpiimpl.h"
#if defined(_OSU_MVAPICH_)
#include "coll_shmem.h"
#endif /* defined(_OSU_MVAPICH_) */

/* This is the default implementation of reduce. The algorithm is:

   Algorithm: MPI_Reduce

   For long messages and for builtin ops and if count >= pof2 (where
   pof2 is the nearest power-of-two less than or equal to the number
   of processes), we use Rabenseifner's algorithm (see
   http://www.hlrs.de/organization/par/services/models/mpi/myreduce.html ).
   This algorithm implements the reduce in two steps: first a
   reduce-scatter, followed by a gather to the root. A
   recursive-halving algorithm (beginning with processes that are
   distance 1 apart) is used for the reduce-scatter, and a binomial tree
   algorithm is used for the gather. The non-power-of-two case is
   handled by dropping to the nearest lower power-of-two: the first
   few odd-numbered processes send their data to their left neighbors
   (rank-1), and the reduce-scatter happens among the remaining
   power-of-two processes. If the root is one of the excluded
   processes, then after the reduce-scatter, rank 0 sends its result to
   the root and exits; the root now acts as rank 0 in the binomial tree
   algorithm for gather.

   For the power-of-two case, the cost for the reduce-scatter is 
   lgp.alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma. The cost for the
   gather to root is lgp.alpha + n.((p-1)/p).beta. Therefore, the
   total cost is:
   Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta + n.((p-1)/p).gamma

   For the non-power-of-two case, assuming the root is not one of the
   odd-numbered processes that get excluded in the reduce-scatter,
   Cost = (2.floor(lgp)+1).alpha + (2.((p-1)/p) + 1).n.beta + 
           n.(1+(p-1)/p).gamma


   For short messages, user-defined ops, and count < pof2, we use a
   binomial tree algorithm for both short and long messages.

   Cost = lgp.alpha + n.lgp.beta + n.lgp.gamma


   We use the binomial tree algorithm in the case of user-defined ops
   because in this case derived datatypes are allowed, and the user
   could pass basic datatypes on one process and derived on another as
   long as the type maps are the same. Breaking up derived datatypes
   to do the reduce-scatter is tricky.

   Possible improvements:

   End Algorithm: MPI_Reduce
*/

/* begin:nested */
/* not declared static because a machine-specific function may call this one
   in some cases */

int MPIR_Reduce_OSU (
    void *sendbuf, 
    void *recvbuf, 
    int count, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    int root, 
    MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Reduce";
    MPI_Status status;
    int comm_size, rank, is_commutative, type_size, pof2, rem, newrank;
    int mask, relrank, source, lroot, *cnts, *disps, i, j, send_idx=0;
    int mpi_errno = MPI_SUCCESS, recv_idx, last_idx=0, newdst;
    int dst, send_cnt, recv_cnt, newroot, newdst_tree_root,
        newroot_tree_root;
	MPI_User_function *uop;
    MPI_Aint   true_lb, true_extent, extent;
	void *tmp_buf;
    MPID_Op *op_ptr;
	MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
#if defined(_OSU_MVAPICH_)
    char* shmem_buf = NULL;
    MPI_Comm shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr = NULL, *leader_commptr = NULL;
    int local_rank = -1, global_rank = -1, local_size=0, my_rank;
    void* local_buf = NULL, *tmpbuf = NULL, *tmpbuf1 = NULL;
    extent = 0;
    int stride = 0;
	is_commutative = 0;
    int leader_root = 0, total_size, shmem_comm_rank;
#endif /* defined(_OSU_MVAPICH_) */

#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(4);

    if (count == 0) {
      return MPI_SUCCESS;
    }

    MPIU_THREADPRIV_GET;
    MPIR_Nest_incr();

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_FIELD(op_errno) = 0;
    
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );


    /* Get the operator and check whether it is commutative or not
    * */
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
		/* get the function by indexing into the op table */
		uop = MPIR_Op_table[op%16 - 1];
    } else {
	MPID_Op_get_ptr(op, op_ptr);
       if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
   	    is_commutative = 0;
        } else  {
            is_commutative = 1;
	}

#if defined(HAVE_CXX_BINDING)
        if (op_ptr->language == MPID_LANG_CXX) {
               uop = (MPI_User_function *) op_ptr->function.c_function;
  	       is_cxx_uop = 1;
	} else {
#endif /* defined(HAVE_CXX_BINDING) */
            if ((op_ptr->language == MPID_LANG_C)) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
            } else  {
                uop = (MPI_User_function *) op_ptr->function.f77_function;
            }
#if defined(HAVE_CXX_BINDING)
        }
#endif /* defined(HAVE_CXX_BINDING) */
    }
    
    mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
     
#if defined(_OSU_MVAPICH_)    
    
    MPIU_ERR_CHKANDJUMP((mpi_errno), mpi_errno, MPI_ERR_OTHER, "**fail");
    stride = count*MPIR_MAX(extent,true_extent);
    
    if ((comm_ptr->shmem_coll_ok == 1)&&(stride < coll_param.reduce_2level_threshold) &&
		(disable_shmem_reduce == 0) && (is_commutative==1) &&
		(enable_shmem_collectives) && (check_comm_registry(comm))) {
        MPIR_Nest_incr();
	my_rank = comm_ptr->rank;
        PMPI_Comm_size(comm, &total_size);
        shmem_comm = comm_ptr->shmem_comm;
        PMPI_Comm_rank(shmem_comm, &local_rank);
        PMPI_Comm_size(shmem_comm, &local_size);
        MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
        shmem_comm_rank = shmem_commptr->shmem_comm_rank;
        
        leader_comm = comm_ptr->leader_comm;
	MPID_Comm_get_ptr(leader_comm, leader_commptr);
	MPIR_Nest_decr();

        if(local_rank == 0){ 
	    global_rank = leader_commptr->rank;
            MPIU_CHKLMEM_MALLOC(tmpbuf, void *, count*(MPIR_MAX(extent,true_extent)), 
                                       mpi_errno, "receive buffer");
	    tmpbuf = (void *)((char*)tmpbuf - true_lb);
  	    MPIU_CHKLMEM_MALLOC(tmpbuf1, void *, count*(MPIR_MAX(extent,true_extent)), 
                                       mpi_errno, "receive buffer");
			
      	    tmpbuf1 = (void *)((char*)tmpbuf1 - true_lb);
	    MPIR_Nest_incr();
	    if( sendbuf == MPI_IN_PLACE ) { 
		    mpi_errno = MPIR_Localcopy(recvbuf, count, datatype, tmpbuf, count, 
                                               datatype);
       	    } else {
		    mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, tmpbuf, count, 
                                               datatype);
            }
            MPIR_Nest_decr();
        }
#if defined(CKPT)
        MPIDI_CH3I_CR_lock();
#endif
        int leader_of_root = comm_ptr->leader_map[root];
	if (local_rank == 0) {
            if(stride <= coll_param.shmem_reduce_msg) { 
                /* Message size is smaller than the shmem_reduce threshold. 
                 * The intra-node communication is done through shmem */ 
                if (local_size > 1) {
                    /* Node leader waits till all the non-leaders have written 
                     * the data into the shmem buffer */  
    	            MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank, 
                                      shmem_comm_rank, (void *)&shmem_buf);
		    if (is_commutative) { 
		        for (i = 1; i < local_size; i++) {
			    local_buf = (char*)shmem_buf + stride*i;
#if defined(HAVE_CXX_BINDING)
   		            if (is_cxx_uop)  {
			         (*MPIR_Process.cxx_call_op_fn)( local_buf, tmpbuf, 
			 			       count, datatype, uop );
  			    } else { 
#endif /* defined(HAVE_CXX_BINDING) */
         			 (*uop)(local_buf, tmpbuf, &count, &datatype);
#if defined(HAVE_CXX_BINDING)
	 	            }
#endif
		        }
			MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size, 
							   local_rank, shmem_comm_rank);
		    }
		}
            } else {
                   /* Message size is larger than the shmem_reduce threshold. 
                    * The leader will spend too much time doing the math operation
                    * for messages that are larger. So, we use a point-to-point
                    * based reduce to balance the load across all the processes within 
                    * the same node*/
                   mpi_errno = PMPI_Reduce(MPI_IN_PLACE, tmpbuf, count, datatype, op, local_rank, 
                                                shmem_comm); 
            } 
            leader_root = comm_ptr->leader_rank[leader_of_root];
	    if (local_size != total_size) {
                MPIR_Nest_incr();
                /* The leaders perform the inter-leader reduce operation */ 
                mpi_errno = MPIR_Reduce(tmpbuf, tmpbuf1, count, datatype, op, leader_root, 
                                        leader_commptr);
				MPIR_Nest_decr();
	    }  else if (root == my_rank) { 
                MPIR_Nest_incr();
		mpi_errno = MPIR_Localcopy(tmpbuf, count, datatype, recvbuf, 
                                            count, datatype);
		MPIR_Nest_decr();
#if defined(CKPT)
		MPIDI_CH3I_CR_unlock();
#endif
                goto fn_exit;
				
	    }
        } else {
                if(stride <=  coll_param.shmem_reduce_msg) { 
    	                MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank, 
                                      shmem_comm_rank, (void *)&shmem_buf);
			local_buf = (char*)shmem_buf + stride*local_rank;
			MPIR_Nest_incr();
                        if(sendbuf != MPI_IN_PLACE) {
 		   	       mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, 
						       local_buf, count, datatype);
                        } else { 
 		   	       mpi_errno = MPIR_Localcopy(recvbuf, count, datatype, 
                 				       local_buf, count, datatype);
                        } 
                    	MPIR_Nest_decr();
			MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size, local_rank, 
								shmem_comm_rank);
                }
                else {  
                      mpi_errno = PMPI_Reduce(sendbuf, recvbuf, count, datatype, op, 0, 
                                        shmem_comm); 
                } 
	}

#if defined(CKPT)
        MPIDI_CH3I_CR_unlock();
#endif
	if ((local_rank == 0) && (root == my_rank)) {
 		MPIR_Nest_incr();
        	mpi_errno = MPIR_Localcopy(tmpbuf1, count, datatype, recvbuf, 
                                           count, datatype);
		MPIR_Nest_decr();
		goto fn_exit;
        }

        /* Copying data from leader to the root incase leader is
		* not the root */
	if (local_size > 1) {
       	        MPIR_Nest_incr();
	        /* Send the message to the root if the leader is not the
		 * root of the reduce operation */
		if ((local_rank == 0) && (root != my_rank) 
                     && (leader_root == global_rank)) { 	
        	      if (local_size == total_size) { 
     		           mpi_errno  = MPIC_Send( tmpbuf, count, datatype, root,  
                                                      MPIR_REDUCE_TAG, comm );
		      } else {
			   mpi_errno  = MPIC_Send( tmpbuf1, count, datatype, root, 
                                                      MPIR_REDUCE_TAG, comm );
		      }
	        }

		if ((local_rank != 0) && (root == my_rank)) {
       		     mpi_errno = MPIC_Recv ( recvbuf, count, datatype, leader_of_root, 
                                             MPIR_REDUCE_TAG, comm, &status);
                }
		MPIR_Nest_decr();
			
       }
    } else {
#endif /* defined(_OSU_MVAPICH_) */ 
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

    MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*(MPIR_MAX(extent,true_extent)),
			mpi_errno, "temporary buffer");
    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *)((char*)tmp_buf - true_lb);
    
    /* If I'm not the root, then my recvbuf may not be valid, therefore
       I have to allocate a temporary one */
    if (rank != root) {
	MPIU_CHKLMEM_MALLOC(recvbuf, void *,
			    count*(MPIR_MAX(extent,true_extent)),
			    mpi_errno, "receive buffer");
        recvbuf = (void *)((char*)recvbuf - true_lb);
    }

    if ((rank != root) || (sendbuf != MPI_IN_PLACE)) {
        mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf,
                                   count, datatype);
	if (mpi_errno) {
           MPIU_ERR_POP(mpi_errno);
        }
    }

    MPID_Datatype_get_size_macro(datatype, type_size);

    /* find nearest power-of-two less than or equal to comm_size */
    pof2 = 1;
    while (pof2 <= comm_size) pof2 <<= 1;
    pof2 >>=1;

#if defined(_OSU_MVAPICH_)
    if ((count*type_size > coll_param.reduce_short_msg) &&
#else /* defined(_OSU_MVAPICH_) */
    if ((count*type_size > MPIR_REDUCE_SHORT_MSG) &&
#endif /* defined(_OSU_MVAPICH_) */
        (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) && (count >= pof2)) {
        /* do a reduce-scatter followed by gather to root. */

        rem = comm_size - pof2;

        /* In the non-power-of-two case, all odd-numbered
           processes of rank < 2*rem send their data to
           (rank-1). These odd-numbered processes no longer
           participate in the algorithm until the very end. The
           remaining processes form a nice power-of-two.

           Note that in MPI_Allreduce we have the even-numbered processes
           send data to odd-numbered processes. That is better for
           non-commutative operations because it doesn't require a
           buffer copy. However, for MPI_Reduce, the most common case
           is commutative operations with root=0. Therefore we want
           even-numbered processes to participate the computation for
           the root=0 case, in order to avoid an extra send-to-root
           communication after the reduce-scatter. In MPI_Allreduce it
           doesn't matter because all processes must get the result. */

        if (rank < 2*rem) {
            if (rank % 2 != 0) { /* odd */
                mpi_errno = MPIC_Send(recvbuf, count,
                                      datatype, rank-1,
                                      MPIR_REDUCE_TAG, comm);
		if (mpi_errno) {
                    MPIU_ERR_POP(mpi_errno);
                }

                /* temporarily set the rank to -1 so that this
                   process does not pariticipate in recursive
                   doubling */
                newrank = -1;
            } else { /* even */
                mpi_errno = MPIC_Recv(tmp_buf, count,
                                      datatype, rank+1,
                                      MPIR_REDUCE_TAG, comm,
                                      MPI_STATUS_IGNORE);
 		if (mpi_errno) {
                   MPIU_ERR_POP(mpi_errno);
                }

                /* do the reduction on received data. */
                /* This algorithm is used only for predefined ops
                   and predefined ops are always commutative. */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn)( tmp_buf, recvbuf,
                                                    count,
                                                    datatype,
                                                    uop );
                } else { 
#endif
                    (*uop)(tmp_buf, recvbuf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                } 
#endif

                /* change the rank */
                newrank = rank / 2;
            }
        } else {   /* rank >= 2*rem */
            newrank = rank - rem;
       }

        /* for the reduce-scatter, calculate the count that
           each process receives and the displacement within
           the buffer */

        /* We allocate these arrays on all processes, even if newrank=-1,
           because if root is one of the excluded processes, we will
           need them on the root later on below. */
	MPIU_CHKLMEM_MALLOC(cnts, int *, pof2*sizeof(int), mpi_errno, "counts");
	MPIU_CHKLMEM_MALLOC(disps, int *, pof2*sizeof(int), mpi_errno, "displacements");
        
        if (newrank != -1) {
            for (i=0; i<(pof2-1); i++)  {
                cnts[i] = count/pof2;
            }
            cnts[pof2-1] = count - (count/pof2)*(pof2-1);
            
            disps[0] = 0;
            for (i=1; i<pof2; i++) {
                disps[i] = disps[i-1] + cnts[i-1];
            }
            
            mask = 0x1;
            send_idx = recv_idx = 0;
            last_idx = pof2;
            while (mask < pof2) {
                newdst = newrank ^ mask;
                /* find real rank of dest */
                dst = (newdst < rem) ? newdst*2 : newdst + rem;

                send_cnt = recv_cnt = 0;
                if (newrank < newdst) {
                    send_idx = recv_idx + pof2/(mask*2);
                    for (i=send_idx; i<last_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i=recv_idx; i<send_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                } else {
                    recv_idx = send_idx + pof2/(mask*2);
                    for (i=send_idx; i<recv_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i=recv_idx; i<last_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                }

                /* Send data from recvbuf. Recv into tmp_buf */
                mpi_errno = MPIC_Sendrecv((char *) recvbuf +
                                          disps[send_idx]*extent,
                                          send_cnt, datatype,
                                          dst, MPIR_REDUCE_TAG,
                                          (char *) tmp_buf +
                                          disps[recv_idx]*extent,
                                          recv_cnt, datatype, dst,
                                          MPIR_REDUCE_TAG, comm,
                                          MPI_STATUS_IGNORE);
 		if (mpi_errno) {
                  MPIU_ERR_POP(mpi_errno);
                }

                /* tmp_buf contains data received in this step.
                   recvbuf contains data accumulated so far */

                /* This algorithm is used only for predefined ops
                   and predefined ops are always commutative. */
#ifdef HAVE_CXX_BINDING
                if (is_cxx_uop) {
                    (*MPIR_Process.cxx_call_op_fn)((char *) tmp_buf +
                                                   disps[recv_idx]*extent,
                                                   (char *) recvbuf +
                                                   disps[recv_idx]*extent,
                                                   recv_cnt, datatype, uop);
                } else { 
#endif
                    (*uop)((char *) tmp_buf + disps[recv_idx]*extent,
                           (char *) recvbuf + disps[recv_idx]*extent,
                           &recv_cnt, &datatype);
#ifdef HAVE_CXX_BINDING
                } 
#endif

                /* update send_idx for next iteration */
                send_idx = recv_idx;
                mask <<= 1;

                /* update last_idx, but not in last iteration
                   because the value is needed in the gather
                   step below. */
                if (mask < pof2) {
                    last_idx = recv_idx + pof2/mask;
                }
            }
        }

        /* now do the gather to root */

        /* Is root one of the processes that was excluded from the
           computation above? If so, send data from newrank=0 to
           the root and have root take on the role of newrank = 0 */

        if (root < 2*rem) {
            if (root % 2 != 0) {
                if (rank == root) {    /* recv */
                    /* initialize the arrays that weren't initialized */
                    for (i=0; i<(pof2-1); i++) {
                        cnts[i] = count/pof2;
                    }
                    cnts[pof2-1] = count - (count/pof2)*(pof2-1);

                    disps[0] = 0;
                    for (i=1; i<pof2; i++) {
                        disps[i] = disps[i-1] + cnts[i-1];
                   }

                    mpi_errno = MPIC_Recv(recvbuf, cnts[0], datatype,
                                          0, MPIR_REDUCE_TAG, comm,
                                          MPI_STATUS_IGNORE);
                    newrank = 0;
                    send_idx = 0;
                    last_idx = 2;
                } else if (newrank == 0) {  /* send */
                    mpi_errno = MPIC_Send(recvbuf, cnts[0], datatype,
                                          root, MPIR_REDUCE_TAG, comm);
                    newrank = -1;
                }
                newroot = 0;
            } else {
              newroot = root / 2;
            }
        } else {
            newroot = root - rem;
       }

        if (newrank != -1) {
            j = 0;
            mask = 0x1;
            while (mask < pof2) {
                mask <<= 1;
                j++;
            }
            mask >>= 1;
            j--;
            while (mask > 0) {
                newdst = newrank ^ mask;

                /* find real rank of dest */
                dst = (newdst < rem) ? newdst*2 : newdst + rem;
                /* if root is playing the role of newdst=0, adjust for
                   it */
                if ((newdst == 0) && (root < 2*rem) && (root % 2 != 0)) {
                    dst = root;
                }
                /* if the root of newdst's half of the tree is the
                   same as the root of newroot's half of the tree, send to
                   newdst and exit, else receive from newdst. */

                newdst_tree_root = newdst >> j;
                newdst_tree_root <<= j;

                newroot_tree_root = newroot >> j;
                newroot_tree_root <<= j;

                send_cnt = recv_cnt = 0;
                if (newrank < newdst) {
                    /* update last_idx except on first iteration */
                    if (mask != pof2/2) {
                        last_idx = last_idx + pof2/(mask*2);
                    }

                    recv_idx = send_idx + pof2/(mask*2);
                    for (i=send_idx; i<recv_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i=recv_idx; i<last_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                } else {
                    recv_idx = send_idx - pof2/(mask*2);
                    for (i=send_idx; i<last_idx; i++) {
                        send_cnt += cnts[i];
                    }
                    for (i=recv_idx; i<send_idx; i++) {
                        recv_cnt += cnts[i];
                    }
                }

                if (newdst_tree_root == newroot_tree_root) {
                    /* send and exit */
                    /* Send data from recvbuf. Recv into tmp_buf */
                    mpi_errno = MPIC_Send((char *) recvbuf +
                                          disps[send_idx]*extent,
                                          send_cnt, datatype,
                                          dst, MPIR_REDUCE_TAG,
                                          comm);
		    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                    break;
                } else {
                    /* recv and continue */
                    mpi_errno = MPIC_Recv((char *) recvbuf +
                                          disps[recv_idx]*extent,
                                          recv_cnt, datatype, dst,
                                          MPIR_REDUCE_TAG, comm,
                                          MPI_STATUS_IGNORE);
		    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                }
                
                if (newrank > newdst) {
                    send_idx = recv_idx;
                }
                
                mask >>= 1;
                j--;
            }
        }
    } else {  /* use a binomial tree algorithm */

    /* This code is from MPICH-1. */

    /* Here's the algorithm.  Relative to the root, look at the bit pattern in
       my rank.  Starting from the right (lsb), if the bit is 1, send to
       the node with that bit zero and exit; if the bit is 0, receive from the
       node with that bit set and combine (as long as that node is within the
       group)

       Note that by receiving with source selection, we guarentee that we get
       the same bits with the same input.  If we allowed the parent to receive
       the children in any order, then timing differences could cause different
       results (roundoff error, over/underflows in some cases, etc).

       Because of the way these are ordered, if root is 0, then this is correct
       for both commutative and non-commutitive operations.  If root is not
       0, then for non-commutitive, we use a root of zero and then send
       the result to the root.  To see this, note that the ordering is
       mask = 1: (ab)(cd)(ef)(gh)            (odds send to evens)
       mask = 2: ((ab)(cd))((ef)(gh))        (3,6 send to 0,4)
       mask = 4: (((ab)(cd))((ef)(gh)))      (4 sends to 0)

       Comments on buffering.
       If the datatype is not contiguous, we still need to pass contiguous
       data to the user routine.
       In this case, we should make a copy of the data in some format,
       and send/operate on that.

       In general, we can't use MPI_PACK, because the alignment of that
       is rather vague, and the data may not be re-usable.  What we actually
       need is a "squeeze" operation that removes the skips.
    */
        mask    = 0x1;
        if (is_commutative) {
            lroot   = root;
        } else {
            lroot   = 0;
        }
        relrank = (rank - lroot + comm_size) % comm_size;

        while (/*(mask & relrank) == 0 && */mask < comm_size) {
            /* Receive */
            if ((mask & relrank) == 0) {
                source = (relrank | mask);
                if (source < comm_size) {
                    source = (source + lroot) % comm_size;
                    mpi_errno = MPIC_Recv (tmp_buf, count, datatype, source,
                                           MPIR_REDUCE_TAG, comm, &status);
		    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

                    /* The sender is above us, so the received buffer must be
                       the second argument (in the noncommutative case). */
                    if (is_commutative) {
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn)( tmp_buf, recvbuf,
                                                            count, datatype, uop );
                        } else { 
#endif
                            (*uop)(tmp_buf, recvbuf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                        } 
#endif
                    } else {
#ifdef HAVE_CXX_BINDING
                        if (is_cxx_uop) {
                            (*MPIR_Process.cxx_call_op_fn)( recvbuf, tmp_buf,
                                                            count, datatype, uop );
                        } else { 
#endif
                            (*uop)(recvbuf, tmp_buf, &count, &datatype);
#ifdef HAVE_CXX_BINDING
                        } 
#endif
                        mpi_errno = MPIR_Localcopy(tmp_buf, count, datatype,
                                                   recvbuf, count, datatype);
			if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                    }
                }
            } else {
                /* I've received all that I'm going to.  Send my result to
                   my parent */
                source = ((relrank & (~ mask)) + lroot) % comm_size;
                mpi_errno  = MPIC_Send( recvbuf, count, datatype,
                                        source, MPIR_REDUCE_TAG, comm );
 		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                break;
            }
            mask <<= 1;
        }

        if (!is_commutative && (root != 0)) {
            if (rank == 0) {
                mpi_errno  = MPIC_Send( recvbuf, count, datatype, root,
                                        MPIR_REDUCE_TAG, comm );
            } else if (rank == root)
	    {
                mpi_errno = MPIC_Recv ( recvbuf, count, datatype, 0,
                                        MPIR_REDUCE_TAG, comm, &status);
            }
	    if (mpi_errno) {
              MPIU_ERR_POP(mpi_errno);
            }
        }
    }

#if defined(_OSU_MVAPICH_)  
  }
#endif /* defined(_OSU_MVAPICH_) */
  
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    /* --BEGIN ERROR HANDLING-- */
    if (MPIU_THREADPRIV_FIELD(op_errno)) {
	    mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
	    goto fn_fail;
    }
    /* --END ERROR HANDLING-- */

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIR_Nest_decr();
    return (mpi_errno);

  fn_fail:
    goto fn_exit;
}
/* end:nested */

/* begin:nested */
/* Needed in intercommunicator allreduce */
int MPIR_Reduce_inter_OSU ( 
    void *sendbuf, 
    void *recvbuf, 
    int count, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    int root, 
    MPID_Comm *comm_ptr )
{
/*  Intercommunicator reduce.
    Remote group does a local intracommunicator
    reduce to rank 0. Rank 0 then sends data to root.

    Cost: (lgp+1).alpha + n.(lgp+1).beta
*/

    static const char FCNAME[] = "MPIR_Reduce_inter";
    int rank, mpi_errno;
    MPI_Status status;
    MPI_Aint true_extent, true_lb, extent;
    void *tmp_buf=NULL;
    MPID_Comm *newcomm_ptr = NULL;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
    MPIU_CHKLMEM_DECL(1);

    if (root == MPI_PROC_NULL) {
        /* local processes other than root do nothing */
        return MPI_SUCCESS;
    }

    MPIU_THREADPRIV_GET;
    MPIR_Nest_incr();
    
    comm = comm_ptr->handle;

    if (root == MPI_ROOT) {
            /* root receives data from rank 0 on remote group */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
        mpi_errno = MPIC_Recv(recvbuf, count, datatype, 0,
                              MPIR_REDUCE_TAG, comm, &status);
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr ); 
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    } else {
        /* remote group. Rank 0 allocates temporary buffer, does
           local intracommunicator reduce, and then sends the data
           to root. */
        
        rank = comm_ptr->rank;
        
        if (rank == 0) {
            mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb,
                                                  &true_extent);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

            MPID_Datatype_get_extent_macro(datatype, extent);
	    MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*(MPIR_MAX(extent,true_extent)), 
                                mpi_errno, "temporary buffer");
            /* adjust for potential negative lower bound in datatype */
            tmp_buf = (void *)((char*)tmp_buf - true_lb);
        }
        
        /* Get the local intracommunicator */
        if (!comm_ptr->local_comm) {
            MPIR_Setup_intercomm_localcomm( comm_ptr );
        }

        newcomm_ptr = comm_ptr->local_comm;
        
        /* now do a local reduce on this intracommunicator */
        mpi_errno = MPIR_Reduce(sendbuf, tmp_buf, count, datatype,
                                op, 0, newcomm_ptr);
	if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }

        if (rank == 0) {
            MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
            mpi_errno = MPIC_Send(tmp_buf, count, datatype, root,
                                  MPIR_REDUCE_TAG, comm); 
            MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
	    if (mpi_errno) {
              MPIU_ERR_POP(mpi_errno);
            }
        }
    }

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIR_Nest_decr();
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
/* end:nested */
