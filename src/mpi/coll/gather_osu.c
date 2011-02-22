/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

/* This is the default implementation of gather. The algorithm is:
   
   Algorithm: MPI_Gather_OSU

   We use a binomial tree algorithm for both short and
   long messages. At nodes other than leaf nodes we need to allocate
   a temporary buffer to store the incoming message. If the root is
   not rank 0, we receive data in a temporary buffer on the root and
   then reorder it into the right order. In the heterogeneous case
   we first pack the buffers by using MPI_Pack and then do the gather.

   Cost = lgp.alpha + n.((p-1)/p).beta
   where n is the total size of the data gathered at the root.

   Possible improvements: 

   End Algorithm: MPI_Gather_OSU
*/

/* not declared static because it is called in intercomm. allgather */
/* begin:nested */
#if defined(_OSU_MVAPICH_)
#include "coll_shmem.h" 

int MPIR_Gather_OSU_two_level (
        void *sendbuf,
        int sendcnt,
        MPI_Datatype sendtype,
        void *recvbuf,
        int recvcnt,
        MPI_Datatype recvtype,
        int root,
        MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Gather_OSU_two_level";
    int comm_size, rank;
    int local_rank, local_size; 
    int leader_comm_rank, leader_comm_size; 
    int mpi_errno = MPI_SUCCESS;
    int recvtype_size, sendtype_size, nbytes; 
    void *tmp_buf=NULL;
    void *leader_gather_buf = NULL; 
    MPI_Status status;
    MPI_Aint   sendtype_extent=0, recvtype_extent=0;       /* Datatype extent */
    MPI_Comm comm;
    int i=0;
    MPIU_THREADPRIV_DECL;
    int leader_root, leader_of_root; 
    MPI_Comm shmem_comm, leader_comm; 
    MPID_Comm *shmem_commptr, *leader_commptr; 

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    
     MPID_Datatype_get_extent_macro(recvtype, recvtype_extent);
     MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
     if ( ((rank == root) && (recvcnt == 0)) ||
         ((rank != root) && (sendcnt == 0)) ) {
        return MPI_SUCCESS;
    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    
    /* extract the rank,size information for the intra-node
     * communicator */
    shmem_comm = comm_ptr->shmem_comm; 
    mpi_errno = PMPI_Comm_rank(shmem_comm, &local_rank);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(shmem_comm, &local_size);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);

    if(local_rank == 0) { 
         /* Node leader. Extract the rank, size information for the leader
          * communicator */
	    leader_comm = comm_ptr->leader_comm;
	    mpi_errno = PMPI_Comm_rank(leader_comm, &leader_comm_rank);
	    if(mpi_errno) {
		    MPIU_ERR_POP(mpi_errno);
	    }
	    mpi_errno = PMPI_Comm_size(leader_comm, &leader_comm_size);
	    if(mpi_errno) {
		    MPIU_ERR_POP(mpi_errno);
	    }
	    MPID_Comm_get_ptr(leader_comm, leader_commptr);
     } 

     MPID_Datatype_get_size_macro(recvtype, recvtype_size);
     MPID_Datatype_get_size_macro(sendtype, sendtype_size);

     if(rank == root ) { 
         nbytes = recvcnt*recvtype_size; 
     } else { 
         nbytes = sendcnt*sendtype_size; 
     } 

     /* First do the intra-node gather */ 
     if(local_rank == 0 ) { 
            /* Node leader, allocate tmp_buffer */
         tmp_buf = MPIU_Malloc(nbytes*local_size);
     } 

     MPIU_THREADPRIV_GET;
     MPIR_Nest_incr();
     if(rank == root && sendbuf == MPI_IN_PLACE) {  
          mpi_errno = MPIR_Gather_OSU_Direct(recvbuf + rank*recvcnt*recvtype_extent, 
                                        recvcnt, recvtype, 
					tmp_buf, nbytes, MPI_BYTE, 
					0, shmem_commptr); 
     } else { 
	  mpi_errno = MPIR_Gather_OSU_Direct(sendbuf, sendcnt, sendtype, 
					tmp_buf, nbytes, MPI_BYTE, 
					0, shmem_commptr);  
     } 
     MPIR_Nest_decr();
     if(mpi_errno) {
          MPIU_ERR_POP(mpi_errno);
     }

      leader_of_root = comm_ptr->leader_map[root]; 
      /* leader_of_root is the global rank of the leader of the root */
      leader_root = comm_ptr->leader_rank[leader_of_root]; 
      /* leader_root is the rank of the leader of the root in leader_comm. 
       * leader_root is to be used as the root of the inter-leader gather ops 
       */ 
      if(comm_ptr->is_uniform != 1) { 
	      if(local_rank == 0) {
		  int *displs;
		  int *recvcnts;
		  int *node_sizes; 
		  int i=0;
		  /* Node leaders have all the data. But, different nodes can have
		   * different number of processes. Do a Gather first to get the 
		   * buffer lengths at each leader, followed by a Gatherv to move
		   * the actual data */ 
		  

		  if(leader_comm_rank == leader_root && root != leader_of_root) { 
		      /* The root of the Gather operation is not a node-level leader 
		       * and this process's rank in the leader_comm is the same 
		       * as leader_root */ 
		      leader_gather_buf = MPIU_Malloc(nbytes*comm_size); 
		  } 

                  node_sizes = comm_ptr->node_sizes; 

		  if(leader_comm_rank == leader_root) {
			  displs = MPIU_Malloc(sizeof(int)*leader_comm_size);
			  recvcnts = MPIU_Malloc(sizeof(int)*leader_comm_size);
			  recvcnts[0] = node_sizes[0]*nbytes;
			  displs[0] = 0; 

			  for(i=1; i< leader_comm_size ; i++) {
				displs[i] = displs[i-1] + node_sizes[i-1]*nbytes;
				recvcnts[i] = node_sizes[i]*nbytes;
			  } 
		  }

                  MPIR_Nest_incr();
		  if(root == leader_of_root) { 
		      /* The root of the gather operation is also the node leader. Receive
		       * into recvbuf and we are done */ 
		      mpi_errno = MPIR_Gatherv_OSU(tmp_buf, local_size*nbytes, 
				  MPI_BYTE, recvbuf, recvcnts, displs, MPI_BYTE,
				  leader_root, leader_commptr);
		  } else { 
		      /* The root of the gather operation is not the node leader. Receive
		       * into leader_gather_buf and then send to the root */ 
		      mpi_errno = MPIR_Gatherv_OSU(tmp_buf, local_size*nbytes, 
				  MPI_BYTE, leader_gather_buf, recvcnts, displs, MPI_BYTE,
				  leader_root, leader_commptr);
		  }
                  MPIR_Nest_decr();
		  if(mpi_errno) {
			    MPIU_ERR_POP(mpi_errno);
		  }
                  if(leader_comm_rank == leader_root) { 
                      MPIU_Free(displs); 
                      MPIU_Free(recvcnts); 
                  } 
	     }
     } else { 
             /* All nodes have the same number of processes. Just do one Gather to get all 
              * the data */ 
	     if(local_rank == 0) { 
		  if(leader_comm_rank == leader_root && root != leader_of_root) {
		      /* The root of the Gather operation is not a node-level leader
		       */
		      leader_gather_buf = MPIU_Malloc(nbytes*comm_size);
		  }
                  MPIR_Nest_incr();
                  if(nbytes*local_size >MPIR_GATHER_TWO_LEVEL_SMALL_MSG) { 
                         /* For larger messages, use direct algorithm in the inter-leader phase */  
			  if(root == leader_of_root) { 
			      mpi_errno = MPIR_Gather_OSU_Direct(tmp_buf, nbytes*local_size, MPI_BYTE,
					  recvbuf, recvcnt*local_size, recvtype,
					  leader_root, leader_commptr);
			  } else { 
			      mpi_errno = MPIR_Gather_OSU_Direct(tmp_buf, nbytes*local_size, MPI_BYTE,
					  leader_gather_buf, nbytes*local_size, MPI_BYTE,
					  leader_root, leader_commptr);
			  }
                  } else {
                         /* For small messages, use binomial algorithm in the inter-leader phase */  
                         if(root == leader_of_root) {
                              mpi_errno = MPIR_Gather_OSU_Binomial(tmp_buf, nbytes*local_size, MPI_BYTE,
                                          recvbuf, recvcnt*local_size, recvtype,
                                          leader_root, leader_commptr);
                          } else {
                              mpi_errno = MPIR_Gather_OSU_Binomial(tmp_buf, nbytes*local_size, MPI_BYTE,
                                          leader_gather_buf, nbytes*local_size, MPI_BYTE,
                                          leader_root, leader_commptr);
                          }
                  } 
                  MPIR_Nest_decr();
		  if(mpi_errno) {
			    MPIU_ERR_POP(mpi_errno);
		  }
	     } 
      } 
     if ((local_rank == 0) && (root != rank)
           && (leader_of_root == rank)) {
           mpi_errno  = MPIC_Send( leader_gather_buf, nbytes*comm_size, MPI_BYTE, 
                                        root, MPIR_GATHER_TAG, comm );
           if(mpi_errno) {
                    MPIU_ERR_POP(mpi_errno);
           }
     } 


     if(rank == root  && local_rank != 0) { 
         /* The root of the gather operation is not the node leader. Receive
          * data from the node leader */ 
          mpi_errno = MPIC_Recv(recvbuf, recvcnt*comm_size, recvtype, 
                                        leader_of_root, MPIR_GATHER_TAG, comm, &status); 
          if(mpi_errno) {
                    MPIU_ERR_POP(mpi_errno);
          }
     }
          
 fn_fail:
    /* check if multiple threads are calling this collective function */
    if(local_rank == 0) { 
          MPIU_Free(tmp_buf); 
          if(leader_comm_rank == 0) { 
               MPIU_Free(leader_gather_buf); 
          } 
    }  
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

    return (mpi_errno);
}
#endif /* #if defined(_OSU_MVAPICH_) */ 




int MPIR_Gather_OSU_Direct ( 
	void *sendbuf, 
	int sendcnt, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt, 
	MPI_Datatype recvtype, 
	int root, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Gather_OSU_Direct";
    int        comm_size, rank;
    int        mpi_errno = MPI_SUCCESS;
    void *tmp_buf=NULL;
    MPI_Status status;
    MPI_Aint   extent=0;            /* Datatype extent */
    MPI_Comm comm;
    int displs[2];
    MPI_Aint struct_displs[2];
    int reqs=0, i=0;
    MPI_Request *reqarray;
    MPI_Status *starray;
    MPIU_CHKLMEM_DECL(2);

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if ( ((rank == root) && (recvcnt == 0)) ||
         ((rank != root) && (sendcnt == 0)) ) {
        return MPI_SUCCESS;
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    if (((comm_ptr->comm_kind == MPID_INTRACOMM) && (root == rank)) ||
        ((comm_ptr->comm_kind == MPID_INTERCOMM) && (root == MPI_ROOT))) {
        if (comm_ptr->comm_kind == MPID_INTRACOMM) { 
            comm_size = comm_ptr->local_size;
        } else { 
            comm_size = comm_ptr->remote_size;
        } 

        MPID_Datatype_get_extent_macro(recvtype, extent);
        /* each node can make sure it is not going to overflow aint */

        MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT recvbuf +
                                         displs[rank] * extent);

        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, comm_size * sizeof(MPI_Request), mpi_errno, "reqarray");
        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, comm_size * sizeof(MPI_Status), mpi_errno, "starray");

        reqs = 0;
        for (i = 0; i < comm_size; i++) {
                if ((comm_ptr->comm_kind == MPID_INTRACOMM) && (i == rank)) {
                    if (sendbuf != MPI_IN_PLACE) {
                        mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
                                                   ((char *)recvbuf+rank*recvcnt*extent),
                                                   recvcnt, recvtype);
                    }
                }
                else {
                    mpi_errno = MPIC_Irecv(((char *)recvbuf+i*recvcnt*extent),
                                           recvcnt, recvtype, i,
                                           MPIR_GATHER_TAG, comm,
                                           &reqarray[reqs++]);

                }
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno) {
                    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, 
                                                      __LINE__, MPI_ERR_OTHER, "**fail", 0);
                    return mpi_errno;
                }
                /* --END ERROR HANDLING-- */
        }
        /* ... then wait for *all* of them to finish: */
        mpi_errno = NMPI_Waitall(reqs, reqarray, starray);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < reqs; i++) {
                if (starray[i].MPI_ERROR != MPI_SUCCESS) { 
                    mpi_errno = starray[i].MPI_ERROR;
                 } 
            }
        }
        /* --END ERROR HANDLING-- */
    }

    else if (root != rank) { /* non-root nodes, and in the intercomm. case, non-root nodes on remote side */
        if (sendcnt) {
            /* we want local size in both the intracomm and intercomm cases
               because the size of the root's group (group A in the standard) is
               irrelevant here. */
            comm_size = comm_ptr->local_size;
            if(sendbuf != MPI_IN_PLACE) {
                   mpi_errno = MPIC_Send(sendbuf, sendcnt, sendtype, root,
                                      MPIR_GATHER_TAG, comm);
            } else { 
                   mpi_errno = MPIC_Send(recvbuf, sendcnt, sendtype, root,
                                      MPIR_GATHER_TAG, comm);
            } 
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno) {
                mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, 
                                                    __LINE__, MPI_ERR_OTHER, "**fail", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        }
     }
 fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIU_CHKLMEM_FREEALL();
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

    return (mpi_errno);
}

int MPIR_Gather_OSU(
        void *sendbuf,
        int sendcnt,
        MPI_Datatype sendtype,
        void *recvbuf,
        int recvcnt,
        MPI_Datatype recvtype,
        int root,
        MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Gather_OSU";
    int        mpi_errno = MPI_SUCCESS;
    int rank, nbytes, comm_size; 
    int recvtype_size, sendtype_size; 
    MPIU_THREADPRIV_DECL;

    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    mpi_errno = PMPI_Comm_size(comm_ptr->handle, &comm_size); 
    if (mpi_errno) { 
          MPIU_ERR_POP(mpi_errno); 
    }
    mpi_errno = PMPI_Comm_rank(comm_ptr->handle, &rank); 
    if (mpi_errno) { 
          MPIU_ERR_POP(mpi_errno); 
    }
    MPIU_THREADPRIV_GET;
    MPIR_Nest_incr();
     if(rank == root ) {
         MPID_Datatype_get_size_macro(recvtype, recvtype_size);
         nbytes = recvcnt*recvtype_size;
     } else {
         MPID_Datatype_get_size_macro(sendtype, sendtype_size);
         nbytes = sendcnt*sendtype_size;
     }

#if defined(_OSU_MVAPICH_)
    if(comm_size <= gather_direct_system_size_small && use_direct_gather == 1) { 
         if(nbytes <= MPIR_GATHER_SMALL_MSG) { 
              if(comm_ptr->shmem_coll_ok == 1) { 
                    mpi_errno = MPIR_Gather_OSU_two_level( sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);   
               } else { 
                    mpi_errno = MPIR_Gather_OSU_Binomial( sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype,
                                       root, comm_ptr);
               } 
         } else { 
               mpi_errno = MPIR_Gather_OSU_Direct( sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);  
         } 
    } 
    else if(comm_size > gather_direct_system_size_small && 
          comm_size <= gather_direct_system_size_medium  && use_direct_gather == 1) {
         if(nbytes <= MPIR_GATHER_MEDIUM_MSG) {
               mpi_errno = MPIR_Gather_OSU_Binomial( sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype,
                                       root, comm_ptr);
         } else { 
               mpi_errno = MPIR_Gather_OSU_Direct( sendbuf, sendcnt, sendtype,
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);  
         }
    } 
    else if(comm_ptr->shmem_coll_ok == 1 && use_two_level_gather == 1) { 
         if(nbytes <= MPIR_GATHER_BINOMIAL_MEDIUM_MSG) { 
              mpi_errno = MPIR_Gather_OSU_Binomial(sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);  
         } else {  
              mpi_errno = MPIR_Gather_OSU_two_level(sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);  
         } 
    } else { 
#endif /* #if defined(_OSU_MVAPICH_) */ 
         mpi_errno = MPIR_Gather_OSU_Binomial( sendbuf, sendcnt, sendtype, 
                                       recvbuf, recvcnt, recvtype, 
                                       root, comm_ptr);  
#if defined(_OSU_MVAPICH_)
    } 
#endif /* #if defined(_OSU_MVAPICH_) */ 
    MPIR_Nest_decr();
    if (mpi_errno) { 
          MPIU_ERR_POP(mpi_errno); 
    }


 fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);


} 


int MPIR_Gather_OSU_Binomial ( 
	void *sendbuf, 
	int sendcnt, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt, 
	MPI_Datatype recvtype, 
	int root, 
	MPID_Comm *comm_ptr )
{
    static const char FCNAME[] = "MPIR_Gather_OSU_Binomial";
    int        comm_size, rank;
    int        mpi_errno = MPI_SUCCESS;
    int curr_cnt=0, relative_rank, nbytes, is_homogeneous;
    int mask, sendtype_size, recvtype_size, src, dst, relative_src;
    int recvblks;
    int tmp_buf_size, missing;
    void *tmp_buf=NULL;
    MPI_Status status;
    MPI_Aint   extent=0;            /* Datatype extent */
    MPI_Comm comm;
    int blocks[2];
    int displs[2];
    MPI_Aint struct_displs[2];
    MPI_Datatype types[2], tmp_type;
    int copy_offset = 0, copy_blks = 0;

#ifdef MPID_HAS_HETERO
    int position, recv_size;
#endif
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if ( ((rank == root) && (recvcnt == 0)) ||
         ((rank != root) && (sendcnt == 0)) ) { 
        return MPI_SUCCESS;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero) { 
        is_homogeneous = 0;
    }
#endif

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    /* Use binomial tree algorithm. */
    
	relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    if (rank == root) {
        MPID_Datatype_get_extent_macro(recvtype, extent);
    }

    if (is_homogeneous)
    {

        /* communicator is homogeneous. no need to pack buffer. */

        if (rank == root)
	{
	    MPID_Datatype_get_size_macro(recvtype, recvtype_size);
            nbytes = recvtype_size * recvcnt;
        }
        else
	{
	    MPID_Datatype_get_size_macro(sendtype, sendtype_size);
            nbytes = sendtype_size * sendcnt;
        }

	/* Find the number of missing nodes in my sub-tree compared to
	 * a balanced tree */
	for (mask = 1; mask < comm_size; mask <<= 1);
	--mask;
	while (relative_rank & mask) mask >>= 1;
	missing = (relative_rank | mask) - comm_size + 1;
	if (missing < 0) missing = 0;
	tmp_buf_size = (mask - missing);

	/* If the message is smaller than the threshold, we will copy
	 * our message in there too */
	if (nbytes < MPIR_GATHER_VSMALL_MSG) { 
           tmp_buf_size++;
        }

	tmp_buf_size *= nbytes;

	/* For zero-ranked root, we don't need any temporary buffer */
	if ((rank == root) && (!root || (nbytes >= MPIR_GATHER_VSMALL_MSG))) { 
	    tmp_buf_size = 0;
        }

	if (tmp_buf_size) {
	    tmp_buf = MPIU_Malloc(tmp_buf_size);
	    /* --BEGIN ERROR HANDLING-- */
	    if (!tmp_buf)
	    {
		mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */
	}

        if (rank == root)
	{
	    if (sendbuf != MPI_IN_PLACE)
	    {
		mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
					   ((char *) recvbuf + extent*recvcnt*rank), recvcnt, recvtype);
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    }
        }
	else if (tmp_buf_size && (nbytes < MPIR_GATHER_VSMALL_MSG))
	{
            /* copy from sendbuf into tmp_buf */
            mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
                                       tmp_buf, nbytes, MPI_BYTE);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        }
	curr_cnt = nbytes;
        
        mask = 0x1;
        while (mask < comm_size)
	{
            if ((mask & relative_rank) == 0)
	    {
                src = relative_rank | mask;
                if (src < comm_size)
		{
                    src = (src + root) % comm_size;

		    if (rank == root)
		    {
			recvblks = mask;
			if ((2 * recvblks) > comm_size)
			    recvblks = comm_size - recvblks;

			if ((rank + mask + recvblks == comm_size) ||
			    (((rank + mask) % comm_size) <
			     ((rank + mask + recvblks) % comm_size))) {
			    /* If the data contiguously fits into the
			     * receive buffer, place it directly. This
			     * should cover the case where the root is
			     * rank 0. */
			    mpi_errno = MPIC_Recv(((char *)recvbuf +
						   (((rank + mask) % comm_size)*recvcnt*extent)),
						  recvblks * recvcnt, recvtype, src,
						  MPIR_GATHER_TAG, comm,
						  &status);
			}
			else if (nbytes < MPIR_GATHER_VSMALL_MSG) {
			    mpi_errno = MPIC_Recv(tmp_buf, recvblks * nbytes, MPI_BYTE,
						  src, MPIR_GATHER_TAG, comm, &status);
			    copy_offset = rank + mask;
			    copy_blks = recvblks;
			}
			else {
			    blocks[0] = recvcnt * (comm_size - root - mask);
			    displs[0] = recvcnt * (root + mask);
			    blocks[1] = (recvcnt * recvblks) - blocks[0];
			    displs[1] = 0;
			    
			    NMPI_Type_indexed(2, blocks, displs, recvtype, &tmp_type);
			    NMPI_Type_commit(&tmp_type);
			    
			    mpi_errno = MPIC_Recv(recvbuf, 1, tmp_type, src,
						  MPIR_GATHER_TAG, comm, &status);

			    NMPI_Type_free(&tmp_type);
			}
		    }
                    else /* Intermediate nodes store in temporary buffer */
		    {
			int offset;

			/* Estimate the amount of data that is going to come in */
			recvblks = mask;
			relative_src = ((src - root) < 0) ? (src - root + comm_size) : (src - root);
			if (relative_src + mask > comm_size) {
			    recvblks -= (relative_src + mask - comm_size);
                        }

			if (nbytes < MPIR_GATHER_VSMALL_MSG) {
			    offset = mask * nbytes;
                        }
			else {
			    offset = (mask - 1) * nbytes;
                        }
			mpi_errno = MPIC_Recv(((char *)tmp_buf + offset),
					      recvblks * nbytes, MPI_BYTE, src,
					      MPIR_GATHER_TAG, comm,
					      &status);
			curr_cnt += (recvblks * nbytes);
                    }
		    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                }
            }
            else
	    {
                dst = relative_rank ^ mask;
                dst = (dst + root) % comm_size;

		if (!tmp_buf_size)
		{
                    /* leaf nodes send directly from sendbuf */
                    mpi_errno = MPIC_Send(sendbuf, sendcnt, sendtype, dst,
                                          MPIR_GATHER_TAG, comm);
                }
                else if (nbytes < MPIR_GATHER_VSMALL_MSG) {
		    mpi_errno = MPIC_Send(tmp_buf, curr_cnt, MPI_BYTE, dst,
					  MPIR_GATHER_TAG, comm);
		}
		else {
		    blocks[0] = sendcnt;
		    struct_displs[0] = (MPI_Aint) sendbuf;
		    types[0] = sendtype;
		    blocks[1] = curr_cnt - nbytes;
		    struct_displs[1] = (MPI_Aint) tmp_buf;
		    types[1] = MPI_BYTE;

		    NMPI_Type_create_struct(2, blocks, struct_displs, types, &tmp_type);
		    NMPI_Type_commit(&tmp_type);

		    mpi_errno = MPIC_Send(MPI_BOTTOM, 1, tmp_type, dst,
					  MPIR_GATHER_TAG, comm);

		    NMPI_Type_free(&tmp_type);
		}
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

                break;
            }
            mask <<= 1;
        }

        if ((rank == root) && root && (nbytes < MPIR_GATHER_VSMALL_MSG) && copy_blks)
	{
            /* reorder and copy from tmp_buf into recvbuf */
	    MPIR_Localcopy(tmp_buf,
			   nbytes * (comm_size - copy_offset), MPI_BYTE,  
			   ((char *) recvbuf + extent * recvcnt * copy_offset),
			   recvcnt * (comm_size - copy_offset), recvtype);
	    MPIR_Localcopy((char *) tmp_buf + nbytes * (comm_size - copy_offset),
			   nbytes * (copy_blks - comm_size + copy_offset), MPI_BYTE,  
			   recvbuf,
			   recvcnt * (copy_blks - comm_size + copy_offset), recvtype);
        }

	if (tmp_buf) {
            MPIU_Free(tmp_buf);
        }
    }
    
#ifdef MPID_HAS_HETERO
    else
    { /* communicator is heterogeneous. pack data into tmp_buf. */
        if (rank == root) {
            NMPI_Pack_size(recvcnt*comm_size, recvtype, comm,
                           &tmp_buf_size); 
        }
        else {
            NMPI_Pack_size(sendcnt*(comm_size/2), sendtype, comm,
                           &tmp_buf_size);
        }

        tmp_buf = MPIU_Malloc(tmp_buf_size);
	/* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf)
	{ 
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */

        position = 0;
        if (sendbuf != MPI_IN_PLACE)
	{
            NMPI_Pack(sendbuf, sendcnt, sendtype, tmp_buf,
                      tmp_buf_size, &position, comm);
            nbytes = position;
        }
        else
	{
            /* do a dummy pack just to calculate nbytes */
            NMPI_Pack(recvbuf, 1, recvtype, tmp_buf,
                      tmp_buf_size, &position, comm);
            nbytes = position*recvcnt;
        }
        
        curr_cnt = nbytes;
        
        mask = 0x1;
        while (mask < comm_size)
	{
            if ((mask & relative_rank) == 0)
	    {
                src = relative_rank | mask;
                if (src < comm_size)
		{
                    src = (src + root) % comm_size;
                    mpi_errno = MPIC_Recv(((char *)tmp_buf + curr_cnt), 
                                          tmp_buf_size-curr_cnt, MPI_BYTE, src,
                                          MPIR_GATHER_TAG, comm, 
                                          &status);
		    /* --BEGIN ERROR HANDLING-- */
                    if (mpi_errno)
		    {
			mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
			return mpi_errno;
		    }
		    /* --END ERROR HANDLING-- */
                    /* the recv size is larger than what may be sent in
                       some cases. query amount of data actually received */
                    NMPI_Get_count(&status, MPI_BYTE, &recv_size);
                    curr_cnt += recv_size;
                }
            }
            else
	    {
                dst = relative_rank ^ mask;
                dst = (dst + root) % comm_size;
                mpi_errno = MPIC_Send(tmp_buf, curr_cnt, MPI_BYTE, dst,
                                      MPIR_GATHER_TAG, comm); 
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
                break;
            }
            mask <<= 1;
        }
        
        if (rank == root)
	{
            /* reorder and copy from tmp_buf into recvbuf */
            if (sendbuf != MPI_IN_PLACE)
	    {
                position = 0;
                NMPI_Unpack(tmp_buf, tmp_buf_size, &position,
                            ((char *) recvbuf + extent*recvcnt*rank),
                            recvcnt*(comm_size-rank), recvtype, comm); 
            }
            else
	    {
                position = nbytes;
                NMPI_Unpack(tmp_buf, tmp_buf_size, &position,
                            ((char *) recvbuf + extent*recvcnt*(rank+1)),
                            recvcnt*(comm_size-rank-1), recvtype,
                            comm);
            }
            if (root != 0)
                NMPI_Unpack(tmp_buf, tmp_buf_size, &position, recvbuf,
                            recvcnt*rank, recvtype, comm); 
        }
        
        MPIU_Free(tmp_buf);
    }
#endif /* MPID_HAS_HETERO */

 fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);
}
/* end:nested */

/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
int MPIR_Gather_inter_OSU ( 
	void *sendbuf, 
	int sendcnt, 
	MPI_Datatype sendtype, 
	void *recvbuf, 
	int recvcnt, 
	MPI_Datatype recvtype, 
	int root, 
	MPID_Comm *comm_ptr )
{
/*  Intercommunicator gather.
    For short messages, remote group does a local intracommunicator
    gather to rank 0. Rank 0 then sends data to root.

    Cost: (lgp+1).alpha + n.((p-1)/p).beta + n.beta
   
    For long messages, we use linear gather to avoid the extra n.beta.

    Cost: p.alpha + n.beta
*/

    static const char FCNAME[] = "MPIR_Gather_inter_OSU";
    int rank, local_size, remote_size, mpi_errno=MPI_SUCCESS;
    int i, nbytes, sendtype_size, recvtype_size;
    MPI_Status status;
    MPI_Aint extent, true_extent, true_lb = 0;
    void *tmp_buf=NULL;
    MPID_Comm *newcomm_ptr = NULL;
    MPI_Comm comm;

    if (root == MPI_PROC_NULL)
    {
        /* local processes other than root do nothing */
        return MPI_SUCCESS;
    }
    
    comm = comm_ptr->handle;
    remote_size = comm_ptr->remote_size; 
    local_size = comm_ptr->local_size; 

    if (root == MPI_ROOT)
    {
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        nbytes = recvtype_size * recvcnt * remote_size;
    }
    else
    {
        /* remote side */
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);
        nbytes = sendtype_size * sendcnt * local_size;
    }

    if (nbytes < MPIR_GATHER_SHORT_MSG)
    {
        if (root == MPI_ROOT)
	{
            /* root receives data from rank 0 on remote group */
            MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
            mpi_errno = MPIC_Recv(recvbuf, recvcnt*remote_size,
                                  recvtype, 0, MPIR_GATHER_TAG, comm,
                                  &status);
            MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr ); 
 
            return mpi_errno;
        }
        else
	{
            /* remote group. Rank 0 allocates temporary buffer, does
               local intracommunicator gather, and then sends the data
               to root. */
            
            rank = comm_ptr->rank;
            
            if (rank == 0)
	    {
                mpi_errno = NMPI_Type_get_true_extent(sendtype, &true_lb,
                                                      &true_extent);  
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
                MPID_Datatype_get_extent_macro(sendtype, extent);
 
                tmp_buf =   MPIU_Malloc(sendcnt*local_size*(MPIR_MAX(extent,true_extent)));  
		/* --BEGIN ERROR HANDLING-- */
                if (!tmp_buf)
		{
                    mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
                    return mpi_errno;
                }
		/* --END ERROR HANDLING-- */
                /* adjust for potential negative lower bound in datatype */
                tmp_buf = (void *)((char*)tmp_buf - true_lb);
            }
            
            /* all processes in remote group form new intracommunicator */
            if (!comm_ptr->local_comm) {
                MPIR_Setup_intercomm_localcomm( comm_ptr );
            }

            newcomm_ptr = comm_ptr->local_comm;

            /* now do the a local gather on this intracommunicator */
            mpi_errno = MPIR_Gather_OSU(sendbuf, sendcnt, sendtype,
                                    tmp_buf, sendcnt, sendtype, 0,
                                    newcomm_ptr); 
            if (rank == 0)
	    {
                MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
                mpi_errno = MPIC_Send(tmp_buf, sendcnt*local_size,
                                      sendtype, root,
                                      MPIR_GATHER_TAG, comm); 
                MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr ); 
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
                MPIU_Free(((char*)tmp_buf+true_lb));
            }
        }
    }
    else
    {
        /* long message. use linear algorithm. */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
        if (root == MPI_ROOT)
	{
            MPID_Datatype_get_extent_macro(recvtype, extent);
            for (i=0; i<remote_size; i++)
	    {
                mpi_errno = MPIC_Recv(((char *)recvbuf+recvcnt*i*extent), 
                                      recvcnt, recvtype, i,
                                      MPIR_GATHER_TAG, comm, &status);
		/* --BEGIN ERROR HANDLING-- */
                if (mpi_errno)
		{
		    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
		    return mpi_errno;
		}
		/* --END ERROR HANDLING-- */
            }
        }
        else
	{
            mpi_errno = MPIC_Send(sendbuf,sendcnt,sendtype,root,
                                  MPIR_GATHER_TAG,comm);
        }
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr ); 
    }

    return mpi_errno;
}
/* end:nested */

