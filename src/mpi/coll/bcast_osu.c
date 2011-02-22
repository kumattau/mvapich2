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
#include <unistd.h>
#if defined(_OSU_MVAPICH_)
#include "coll_shmem.h"
#endif /* defined(_OSU_MVAPICH_) */

#if defined(_OSU_MVAPICH_)
int intra_shmem_Bcast_Large(
	void *buffer,
	int count,
        MPI_Datatype datatype,
	int nbytes,
	int root,
	MPID_Comm *comm );

#define SHMEM_BCST_THRESHOLD 1<<20

int shmem_bcast_threshold = SHMEM_BCST_THRESHOLD;
int bcast_short_msg_threshold = -1;
int enable_shmem_bcast = 1;

int bcast_tuning(int nbytes, MPID_Comm *comm_ptr) 
{ 
   int bcast_short_msg; 
   int node_size, comm_size, num_nodes; 
   MPI_Comm comm, shmem_comm; 

   if(bcast_short_msg_threshold != -1)  {
             /* User has set the run-time parameter "MV2_BCAST_SHORT_MSG".
              * Just use that value */
              return bcast_short_msg_threshold;
   } else { 
       /* User has not used the run-time parameter for 
        * setting the short message bcast threshold. 
  	* Lets try to determine the number of nodes and
        * choose an appropriate threshold */ 
       if(use_osu_collectives == 0 || comm_ptr->shmem_coll_ok != 1 || 
          enable_knomial_2level_bcast == 0 || 
          enable_shmem_collectives == 0 || comm_ptr->shmem_comm == 0) { 
            /*Either shared-memory collectives, or knomial-2level 
            *Bcast was disabled. Or this already is an internal 
            *communicator that does not have a valid shmem-comm
            *structure. Either way, we have nothing left to do. 
            *Set the bcast_short_msg to the existing 
            *"MPIR_BCAST_SHORT_MSG" value and return */ 
            return MPIR_BCAST_SHORT_MSG; 
        } else { 
            comm = comm_ptr->handle; 
            shmem_comm = comm_ptr->shmem_comm; 
            PMPI_Comm_size(comm, &comm_size); 
            PMPI_Comm_size(shmem_comm, &node_size); 
            /* This is currently based on large scale experiments on the 
             * TACC Ranger (16 cores/node AMD Barcelona, InfiniBand SDR)
             * In the near future, we will be making this function robust 
             * to consider various network speeds. */
            num_nodes = comm_size/node_size; 
            if(num_nodes <= 64) { 
                   return MPIR_BCAST_SHORT_MSG; 
            } else if(num_nodes > 64) { 
                   return (64*1024);
            } 
        } 
   }
} 
#endif /* #if defined(_OSU_MVAPICH_) */


#include <unistd.h>

/* This is the default implementation of broadcast. The algorithm is:
   
   Algorithm: MPI_Bcast

   For short messages, we use a binomial tree algorithm. 
   Cost = lgp.alpha + n.lgp.beta

   For long messages, we do a scatter followed by an allgather. 
   We first scatter the buffer using a binomial tree algorithm. This costs
   lgp.alpha + n.((p-1)/p).beta
   If the datatype is contiguous and the communicator is homogeneous,
   we treat the data as bytes and divide (scatter) it among processes
   by using ceiling division. For the noncontiguous or heterogeneous
   cases, we first pack the data into a temporary buffer by using
   MPI_Pack, scatter it as bytes, and unpack it after the allgather.

   For the allgather, we use a recursive doubling algorithm for 
   medium-size messages and power-of-two number of processes. This
   takes lgp steps. In each step pairs of processes exchange all the
   data they have (we take care of non-power-of-two situations). This
   costs approximately lgp.alpha + n.((p-1)/p).beta. (Approximately
   because it may be slightly more in the non-power-of-two case, but
   it's still a logarithmic algorithm.) Therefore, for long messages
   Total Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta

   Note that this algorithm has twice the latency as the tree algorithm
   we use for short messages, but requires lower bandwidth: 2.n.beta
   versus n.lgp.beta. Therefore, for long messages and when lgp > 2,
   this algorithm will perform better.

   For long messages and for medium-size messages and non-power-of-two 
   processes, we use a ring algorithm for the allgather, which 
   takes p-1 steps, because it performs better than recursive doubling.
   Total Cost = (lgp+p-1).alpha + 2.n.((p-1)/p).beta

   Possible improvements: 
   For clusters of SMPs, we may want to do something differently to
   take advantage of shared memory on each node.

   End Algorithm: MPI_Bcast
*/

/* begin:nested */
/* not declared static because it is called in intercomm. allgatherv */
int MPIR_Bcast_OSU ( 
	void *buffer,
	int count,
	MPI_Datatype datatype,
	int root,
	MPID_Comm *comm_ptr )
{
  static const char FCNAME[] = "MPIR_Bcast_OSU";
  MPI_Status status;
  int        rank, comm_size, src, dst;
  int        relative_rank, mask;
  int        mpi_errno = MPI_SUCCESS;
  int scatter_size, nbytes=0, curr_size, recv_size = 0, send_size;
  int type_size, j, k, i, tmp_mask, is_contig, is_homogeneous;
  int relative_dst, dst_tree_root, my_tree_root, send_offset;
  int recv_offset, tree_root, nprocs_completed, offset, position;
#if defined(_OSU_MVAPICH_)
  int *recvcnts, *displs, left, right, jnext;
  int bcast_short_msg; 
#else /* defined(_OSU_MVAPICH_) */
  int *recvcnts, *displs, left, right, jnext, pof2, comm_size_is_pof2;
#endif /* defined(_OSU_MVAPICH_) */
  void *tmp_buf=NULL;
  MPI_Comm comm;
  MPID_Datatype *dtp;
  MPI_Aint true_extent, true_lb;

  if (count == 0) {
     return MPI_SUCCESS;
  }
  comm = comm_ptr->handle;
  comm_size = comm_ptr->local_size;
  rank = comm_ptr->rank;

  /* If there is only one process, return */
  if (comm_size == 1) {
     return MPI_SUCCESS;
  }

  if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN) {
      is_contig = 1;
  } else {
      MPID_Datatype_get_ptr(datatype, dtp);
      is_contig = dtp->is_contig;
  }
  
  is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
  if (comm_ptr->is_hetero) {
      is_homogeneous = 0;
  }
#endif

  /* MPI_Type_size() might not give the accurate size of the packed
   * datatype for heterogeneous systems (because of padding, encoding,
   * etc). On the other hand, MPI_Pack_size() can become very
   * expensive, depending on the implementation, especially for
   * heterogeneous systems. We want to use MPI_Type_size() wherever
   * possible, and MPI_Pack_size() in other places.
   */
  if (is_homogeneous) {
      MPID_Datatype_get_size_macro(datatype, type_size);
  } else {
      mpi_errno = NMPI_Pack_size(1, datatype, comm, &type_size);
      if (mpi_errno != MPI_SUCCESS) {
	  MPIU_ERR_POP(mpi_errno);
      }
  }
  nbytes = type_size * count;
  if (!is_contig || !is_homogeneous) {
      tmp_buf = MPIU_Malloc(nbytes);
      /* --BEGIN ERROR HANDLING-- */
      if (!tmp_buf) {
          mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                            FCNAME, __LINE__, MPI_ERR_OTHER, 
                                            "**nomem", "**nomem %d", type_size );
          return mpi_errno;
      }
      /* --END ERROR HANDLING-- */

      /* TODO: Pipeline the packing and communication */
      position = 0;
      if (rank == root) {
	  NMPI_Pack(buffer, count, datatype, tmp_buf, nbytes,
		    &position, comm);
      }
  }

  relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

  /* check if multiple threads are calling this collective function */
  MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

#if !defined(_OSU_MVAPICH_)
  if ((nbytes <= MPIR_BCAST_SHORT_MSG) || (comm_size < MPIR_BCAST_MIN_PROCS)) {
#else 
  if ((nbytes <= bcast_tuning(nbytes, comm_ptr)) || (comm_size < MPIR_BCAST_MIN_PROCS)) {

     if(enable_knomial_2level_bcast &&  enable_shmem_collectives  &&
             comm_ptr->shmem_coll_ok == 1 &&
             comm_ptr->leader_comm != 0 && comm_ptr->shmem_comm != 0 &&
             comm_size > knomial_2level_bcast_system_size_threshold) {
           if ( !is_contig || !is_homogeneous) {
                mpi_errno = knomial_2level_Bcast(tmp_buf, nbytes,
                                           MPI_BYTE,nbytes,root,comm_ptr);
           }else {
                mpi_errno = knomial_2level_Bcast(buffer,count,datatype,nbytes,
                            root,comm_ptr);
           }
        }
     else {
#endif /* #if defined(_OSU_MVAPICH_) */ 
      /* Use short message algorithm, namely, binomial tree */

      /* Algorithm:
         This uses a fairly basic recursive subdivision algorithm.
         The root sends to the process comm_size/2 away; the receiver becomes
         a root for a subtree and applies the same process.

         So that the new root can easily identify the size of its
         subtree, the (subtree) roots are all powers of two (relative
         to the root) If m = the first power of 2 such that 2^m >= the
         size of the communicator, then the subtree at root at 2^(m-k)
         has size 2^k (with special handling for subtrees that aren't
         a power of two in size).

         Do subdivision.  There are two phases:
         1. Wait for arrival of data.  Because of the power of two nature
         of the subtree roots, the source of this message is alwyas the
         process whose relative rank has the least significant 1 bit CLEARED.
         That is, process 4 (100) receives from process 0, process 7 (111)
         from process 6 (110), etc.
         2. Forward to my subtree

         Note that the process that is the tree root is handled automatically
         by this code, since it has no bits set.  */
             

      mask = 0x1;
      while (mask < comm_size)
      {
          if (relative_rank & mask)
	  {
              src = rank - mask;
              if (src < 0) {
                   src += comm_size;
              }
	      if (!is_contig || !is_homogeneous) {
		  mpi_errno = MPIC_Recv(tmp_buf,nbytes,MPI_BYTE,src,
					MPIR_BCAST_TAG,comm,MPI_STATUS_IGNORE);
              } else {
		  mpi_errno = MPIC_Recv(buffer,count,datatype,src,
					MPIR_BCAST_TAG,comm,MPI_STATUS_IGNORE);
              }
              if (mpi_errno != MPI_SUCCESS) {
		  MPIU_ERR_POP(mpi_errno);
	      }
              break;
          }
          mask <<= 1;
      }
      /* This process is responsible for all processes that have bits
         set from the LSB upto (but not including) mask.  Because of
         the "not including", we start by shifting mask back down one.

         We can easily change to a different algorithm at any power of two
          by changing the test (mask > 1) to (mask > block_size)

         One such version would use non-blocking operations for the last 2-4
         steps (this also bounds the number of MPI_Requests that would
         be needed).  */
      mask >>= 1;
      while (mask > 0)
      {
          if (relative_rank + mask < comm_size)
	  {
              dst = rank + mask;
              if (dst >= comm_size) dst -= comm_size;
	      if (!is_contig || !is_homogeneous) {
		  mpi_errno = MPIC_Send(tmp_buf,nbytes,MPI_BYTE,dst,
					MPIR_BCAST_TAG,comm);
              } else {
		  mpi_errno = MPIC_Send(buffer,count,datatype,dst,
					MPIR_BCAST_TAG,comm);
              }
              if (mpi_errno != MPI_SUCCESS) {
		  MPIU_ERR_POP(mpi_errno);
	      }
          }
          mask >>= 1;
      }
#if defined(_OSU_MVAPICH_)
    }
#endif
  }
#if defined(_OSU_MVAPICH_)
  else if (enable_shmem_collectives && (comm_ptr->shmem_coll_ok == 1) && 
                      (nbytes < shmem_bcast_threshold) && enable_shmem_bcast) {
      if ( !is_contig || !is_homogeneous) {
          mpi_errno = intra_shmem_Bcast_Large(tmp_buf, nbytes, MPI_BYTE, nbytes, root, comm_ptr);
      } else {
          mpi_errno = intra_shmem_Bcast_Large(buffer, count, datatype, nbytes, root, comm_ptr);
      }

      if (mpi_errno == -1) {
          /* use long message algorithm: binomial tree scatter followed by an
             allgather */

          /* The scatter algorithm divides the buffer into nprocs pieces and
             scatters them among the processes. Root gets the first piece,
             root+1 gets the second piece, and so forth. Uses the same binomial
             tree algorithm as above. Ceiling division
             is used to compute the size of each piece. This means some
             processes may not get any data. For example if bufsize = 97 and
             nprocs = 16, ranks 15 and 16 will get 0 data. On each process, the
             scattered data is stored at the same offset in the buffer as it is
             on the root process. */

          if (is_contig && is_homogeneous)
          {
              /* contiguous and homogeneous. no need to pack. */
              mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb,
                                                    &true_extent);
              if (mpi_errno) {
    	      MPIU_ERR_POP(mpi_errno);
    	      }
              tmp_buf = (char *) buffer + true_lb;
          }

          scatter_size = (nbytes + comm_size - 1)/comm_size; /* ceiling division */
          curr_size = (rank == root) ? nbytes : 0; /* root starts with all the
                                                      data */

          mask = 0x1;
          while (mask < comm_size)
          {
              if (relative_rank & mask)
    	      {
                  src = rank - mask;
                  if (src < 0) {
                     src += comm_size;
                  }
                  recv_size = nbytes - relative_rank*scatter_size;
                  /* recv_size is larger than what might actually be sent by the
                     sender. We don't need compute the exact value because MPI
                     allows you to post a larger recv.*/
                  if (recv_size <= 0) {
                      curr_size = 0; /* this process doesn't receive any data
                                        because of uneven division */
    	          } else {
                      mpi_errno = MPIC_Recv(((char *)tmp_buf +
                                             relative_rank*scatter_size),
                                            recv_size, MPI_BYTE, src,
                                            MPIR_BCAST_TAG, comm, &status);
                      if (mpi_errno != MPI_SUCCESS) {
    		         MPIU_ERR_POP(mpi_errno);
    		      }

                      /* query actual size of data received */
                      NMPI_Get_count(&status, MPI_BYTE, &curr_size);
                  }
                  break;
              }
              mask <<= 1;
          }

          /* This process is responsible for all processes that have bits
             set from the LSB upto (but not including) mask.  Because of
             the "not including", we start by shifting mask back down
             one. */

          mask >>= 1;
          while (mask > 0)
          {
              if (relative_rank + mask < comm_size)
    	      {
                  send_size = curr_size - scatter_size * mask;
                  /* mask is also the size of this process's subtree */

                  if (send_size > 0)
    	          {
                      dst = rank + mask;
                      if (dst >= comm_size) {
                         dst -= comm_size;
                      }
                      mpi_errno = MPIC_Send (((char *)tmp_buf +
                                             scatter_size*(relative_rank+mask)),
                                            send_size, MPI_BYTE, dst,
                                            MPIR_BCAST_TAG, comm);
                      if (mpi_errno != MPI_SUCCESS) {
    		         MPIU_ERR_POP(mpi_errno);
    		      }
                      curr_size -= send_size;
                  }
              }
              mask >>= 1;
          }

          /* Scatter complete. Now do an allgather .  */

          /* check if comm_size is a power of two */
    #if defined(_OSU_MVAPICH_)
          if (nbytes < MPIR_BCAST_LONG_MSG
              && (comm_size & (comm_size - 1)) == 0)
    #else /* defined(_OSU_MVAPICH_) */
          pof2 = 1;
          while (pof2 < comm_size) {
              pof2 *= 2;
          }
          if (pof2 == comm_size) {
               comm_size_is_pof2 = 1;
          } else {
             comm_size_is_pof2 = 0;
          }

          if ((nbytes < MPIR_BCAST_LONG_MSG) && (comm_size_is_pof2))
    #endif /* defined(_OSU_MVAPICH_) */
          {
              /* medium size allgather and pof2 comm_size. use recurive doubling. */

              mask = 0x1;
              i = 0;
              while (mask < comm_size)
    	      {
                  relative_dst = relative_rank ^ mask;

                  dst = (relative_dst + root) % comm_size;

                  /* find offset into send and recv buffers.
                     zero out the least significant "i" bits of relative_rank and
                     relative_dst to find root of src and dst
                     subtrees. Use ranks of roots as index to send from
                     and recv into  buffer */

                  dst_tree_root = relative_dst >> i;
                  dst_tree_root <<= i;

                  my_tree_root = relative_rank >> i;
                  my_tree_root <<= i;

                  send_offset = my_tree_root * scatter_size;
                  recv_offset = dst_tree_root * scatter_size;

                  if (relative_dst < comm_size) {
                      mpi_errno = MPIC_Sendrecv(((char *)tmp_buf + send_offset),
                                                curr_size, MPI_BYTE, dst, MPIR_BCAST_TAG,
                                                ((char *)tmp_buf + recv_offset),
                                                (nbytes-recv_offset < 0 ? 0 : nbytes-recv_offset),
    					    MPI_BYTE, dst, MPIR_BCAST_TAG, comm, &status);
                      if (mpi_errno != MPI_SUCCESS) {
    		         MPIU_ERR_POP(mpi_errno);
    		      }
                      NMPI_Get_count(&status, MPI_BYTE, &recv_size);
                      curr_size += recv_size;
                  }

                  /* if some processes in this process's subtree in this step
                     did not have any destination process to communicate with
                     because of non-power-of-two, we need to send them the
                     data that they would normally have received from those
                     processes. That is, the haves in this subtree must send to
                     the havenots. We use a logarithmic recursive-halfing algorithm
                     for this. */

                  /* This part of the code will not currently be
                     executed because we are not using recursive
                     doubling for non power of two. Mark it as experimental
                     so that it doesn't show up as red in the coverage tests. */

    	      /* --BEGIN EXPERIMENTAL-- */
                  if (dst_tree_root + mask > comm_size) {
                      nprocs_completed = comm_size - my_tree_root - mask;
                      /* nprocs_completed is the number of processes in this
                         subtree that have all the data. Send data to others
                         in a tree fashion. First find root of current tree
                         that is being divided into two. k is the number of
                         least-significant bits in this process's rank that
                         must be zeroed out to find the rank of the root */
                      j = mask;
                      k = 0;
                      while (j) {
                          j >>= 1;
                          k++;
                      }
                      k--;

                      offset = scatter_size * (my_tree_root + mask);
                      tmp_mask = mask >> 1;

                      while (tmp_mask) {
                          relative_dst = relative_rank ^ tmp_mask;
                          dst = (relative_dst + root) % comm_size;

                          tree_root = relative_rank >> k;
                          tree_root <<= k;

                          /* send only if this proc has data and destination
                             doesn't have data. */

                          if ((relative_dst > relative_rank) &&
                              (relative_rank < tree_root + nprocs_completed)
                              && (relative_dst >= tree_root + nprocs_completed)) {

                              mpi_errno = MPIC_Send(((char *)tmp_buf + offset),
                                                    recv_size, MPI_BYTE, dst,
                                                    MPIR_BCAST_TAG, comm);
                              /* recv_size was set in the previous
                                 receive. that's the amount of data to be
                                 sent now. */
                              if (mpi_errno != MPI_SUCCESS) {
    			         MPIU_ERR_POP(mpi_errno);
    			      }
                           } else if ((relative_dst < relative_rank) &&
                                   (relative_dst < tree_root + nprocs_completed) &&
                                   (relative_rank >= tree_root + nprocs_completed)) {
                             /* recv only if this proc. doesn't have data and sender
                                has data */
                              mpi_errno = MPIC_Recv(((char *)tmp_buf + offset),
                                                    nbytes - offset,
                                                    MPI_BYTE, dst, MPIR_BCAST_TAG,
                                                    comm, &status);
                              /* nprocs_completed is also equal to the no. of processes
                                 whose data we don't have */
                              if (mpi_errno != MPI_SUCCESS) {
    			         MPIU_ERR_POP(mpi_errno);
    			      }
                              NMPI_Get_count(&status, MPI_BYTE, &recv_size);
                              curr_size += recv_size;
                           }
                           tmp_mask >>= 1;
                           k--;
                      }
                  }
                  /* --END EXPERIMENTAL-- */

                  mask <<= 1;
                  i++;
              }
          } else {
              /* long-message allgather or medium-size but non-power-of-two. use ring algorithm. */

              recvcnts = MPIU_Malloc(comm_size*sizeof(int));
    	      /* --BEGIN ERROR HANDLING-- */
              if (!recvcnts) {
                  mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                    FCNAME, __LINE__, MPI_ERR_OTHER, 
                                                   "**nomem", "**nomem %d", 
                                                    comm_size * sizeof(int));
                  return mpi_errno;
              }
    	      /* --END ERROR HANDLING-- */
              displs = MPIU_Malloc(comm_size*sizeof(int));
    	      /* --BEGIN ERROR HANDLING-- */
              if (!displs) {
                  mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                    FCNAME, __LINE__, MPI_ERR_OTHER, 
                                                    "**nomem", "**nomem %d", 
                                                    comm_size * sizeof(int));
                  return mpi_errno;
              }
    	      /* --END ERROR HANDLING-- */

              for (i=0; i<comm_size; i++) {
                  recvcnts[i] = nbytes - i*scatter_size;
                  if (recvcnts[i] > scatter_size) {
                      recvcnts[i] = scatter_size;
                  }
                  if (recvcnts[i] < 0) {
                      recvcnts[i] = 0;
                  }
              }

              displs[0] = 0;
              for (i=1; i<comm_size; i++) {
                  displs[i] = displs[i-1] + recvcnts[i-1];
              }

              left  = (comm_size + rank - 1) % comm_size;
              right = (rank + 1) % comm_size;

              j     = rank;
              jnext = left;
              for (i=1; i<comm_size; i++)
    	      {
                  mpi_errno =
                      MPIC_Sendrecv((char *)tmp_buf +
                                    displs[(j-root+comm_size)%comm_size],
                                    recvcnts[(j-root+comm_size)%comm_size],
                                    MPI_BYTE, right, MPIR_BCAST_TAG,
                                    (char *)tmp_buf +
                                    displs[(jnext-root+comm_size)%comm_size],
                                    recvcnts[(jnext-root+comm_size)%comm_size],
                                    MPI_BYTE, left,
                                    MPIR_BCAST_TAG, comm, MPI_STATUS_IGNORE);
                  if (mpi_errno != MPI_SUCCESS) {
    		     MPIU_ERR_POP(mpi_errno);
    	          }
                  j	    = jnext;
                  jnext = (comm_size + jnext - 1) % comm_size;
              }

              MPIU_Free(recvcnts);
              MPIU_Free(displs);
          }
      } 
  }
#endif /* #if defined(_OSU_MVAPICH_) */
  else {
      /* use long message algorithm: binomial tree scatter followed by an allgather */
      /* The scatter algorithm divides the buffer into nprocs pieces and
         scatters them among the processes. Root gets the first piece,
         root+1 gets the second piece, and so forth. Uses the same binomial
         tree algorithm as above. Ceiling division
         is used to compute the size of each piece. This means some
         processes may not get any data. For example if bufsize = 97 and
         nprocs = 16, ranks 15 and 16 will get 0 data. On each process, the
         scattered data is stored at the same offset in the buffer as it is
         on the root process. */

      if (is_contig && is_homogeneous) {
          /* contiguous and homogeneous. no need to pack. */
          mpi_errno = NMPI_Type_get_true_extent(datatype, &true_lb,
                                                &true_extent);
          if (mpi_errno) {
	      MPIU_ERR_POP(mpi_errno);
	  }
          tmp_buf = (char *) buffer + true_lb;
      }

      scatter_size = (nbytes + comm_size - 1)/comm_size; /* ceiling division */
      curr_size = (rank == root) ? nbytes : 0; /* root starts with all the
                                                  data */

      mask = 0x1;
      while (mask < comm_size)
      {
          if (relative_rank & mask)
	  {
              src = rank - mask;
              if (src < 0) {
                 src += comm_size;
              }
              recv_size = nbytes - relative_rank*scatter_size;
              /* recv_size is larger than what might actually be sent by the
                 sender. We don't need compute the exact value because MPI
                 allows you to post a larger recv.*/
              if (recv_size <= 0) {
                  curr_size = 0; /* this process doesn't receive any data
                                    because of uneven division */
	      } else {
                  mpi_errno = MPIC_Recv(((char *)tmp_buf +
                                         relative_rank*scatter_size),
                                        recv_size, MPI_BYTE, src,
                                        MPIR_BCAST_TAG, comm, &status);
                  if (mpi_errno != MPI_SUCCESS) {
		      MPIU_ERR_POP(mpi_errno);
		  }

                  /* query actual size of data received */
                  NMPI_Get_count(&status, MPI_BYTE, &curr_size);
              }
              break;
          }
          mask <<= 1;
      }

      /* This process is responsible for all processes that have bits
         set from the LSB upto (but not including) mask.  Because of
         the "not including", we start by shifting mask back down
         one. */

      mask >>= 1;
      while (mask > 0)
      {
          if (relative_rank + mask < comm_size)
	  {
              send_size = curr_size - scatter_size * mask;
              /* mask is also the size of this process's subtree */

              if (send_size > 0)
	      {
                  dst = rank + mask;
                  if (dst >= comm_size)  {
                     dst -= comm_size;
                  }
                  mpi_errno = MPIC_Send (((char *)tmp_buf +
                                         scatter_size*(relative_rank+mask)),
                                        send_size, MPI_BYTE, dst,
                                        MPIR_BCAST_TAG, comm);
                  if (mpi_errno != MPI_SUCCESS) {
		      MPIU_ERR_POP(mpi_errno);
		  }
                  curr_size -= send_size;
              }
          }
          mask >>= 1;
      }

      /* Scatter complete. Now do an allgather .  */

      /* check if comm_size is a power of two */
#if defined(_OSU_MVAPICH_)
      if (nbytes < MPIR_BCAST_LONG_MSG
          && (comm_size & (comm_size - 1)) == 0)
#else /* defined(_OSU_MVAPICH_) */
      pof2 = 1;
      while (pof2 < comm_size) {
          pof2 *= 2;
      }
      if (pof2 == comm_size) {
          comm_size_is_pof2 = 1;
      } else {
         comm_size_is_pof2 = 0;
      }

      if ((nbytes < MPIR_BCAST_LONG_MSG) && (comm_size_is_pof2))
#endif /* defined(_OSU_MVAPICH_) */
      {
          /* medium size allgather and pof2 comm_size. use recurive doubling. */

          mask = 0x1;
          i = 0;
          while (mask < comm_size)
	  {
              relative_dst = relative_rank ^ mask;

              dst = (relative_dst + root) % comm_size;

              /* find offset into send and recv buffers.
                 zero out the least significant "i" bits of relative_rank and
                 relative_dst to find root of src and dst
                 subtrees. Use ranks of roots as index to send from
                 and recv into  buffer */

              dst_tree_root = relative_dst >> i;
              dst_tree_root <<= i;

              my_tree_root = relative_rank >> i;
              my_tree_root <<= i;

              send_offset = my_tree_root * scatter_size;
              recv_offset = dst_tree_root * scatter_size;

              if (relative_dst < comm_size)
	      {
                  mpi_errno = MPIC_Sendrecv(((char *)tmp_buf + send_offset),
                                            curr_size, MPI_BYTE, dst, MPIR_BCAST_TAG,
                                            ((char *)tmp_buf + recv_offset),
                                            (nbytes-recv_offset < 0 ? 0 : nbytes-recv_offset),
					    MPI_BYTE, dst, MPIR_BCAST_TAG, comm, &status);
                  if (mpi_errno != MPI_SUCCESS) {
		      MPIU_ERR_POP(mpi_errno);
		  }
                  NMPI_Get_count(&status, MPI_BYTE, &recv_size);
                  curr_size += recv_size;
              }

              /* if some processes in this process's subtree in this step
                 did not have any destination process to communicate with
                 because of non-power-of-two, we need to send them the
                 data that they would normally have received from those
                 processes. That is, the haves in this subtree must send to
                 the havenots. We use a logarithmic recursive-halfing algorithm
                 for this. */

              /* This part of the code will not currently be
                 executed because we are not using recursive
                 doubling for non power of two. Mark it as experimental
                 so that it doesn't show up as red in the coverage tests. */

	      /* --BEGIN EXPERIMENTAL-- */
              if (dst_tree_root + mask > comm_size)
	      {
                  nprocs_completed = comm_size - my_tree_root - mask;
                  /* nprocs_completed is the number of processes in this
                     subtree that have all the data. Send data to others
                     in a tree fashion. First find root of current tree
                     that is being divided into two. k is the number of
                     least-significant bits in this process's rank that
                     must be zeroed out to find the rank of the root */
                  j = mask;
                  k = 0;
                  while (j)
		  {
                      j >>= 1;
                      k++;
                  }
                  k--;

                  offset = scatter_size * (my_tree_root + mask);
                  tmp_mask = mask >> 1;

                  while (tmp_mask)
		  {
                      relative_dst = relative_rank ^ tmp_mask;
                      dst = (relative_dst + root) % comm_size;

                      tree_root = relative_rank >> k;
                      tree_root <<= k;

                      /* send only if this proc has data and destination
                         doesn't have data. */

                      if ((relative_dst > relative_rank) &&
                          (relative_rank < tree_root + nprocs_completed)
                          && (relative_dst >= tree_root + nprocs_completed)) {

                          mpi_errno = MPIC_Send(((char *)tmp_buf + offset),
                                                recv_size, MPI_BYTE, dst,
                                                MPIR_BCAST_TAG, comm);
                          /* recv_size was set in the previous
                             receive. that's the amount of data to be
                             sent now. */
                          if (mpi_errno != MPI_SUCCESS) {
			      MPIU_ERR_POP(mpi_errno);
			  }
                      } else if ((relative_dst < relative_rank) &&
                               (relative_dst < tree_root + nprocs_completed) &&
                               (relative_rank >= tree_root + nprocs_completed)) {
                          /* recv only if this proc. doesn't have data and sender
                             has data */
                          mpi_errno = MPIC_Recv(((char *)tmp_buf + offset),
                                                nbytes - offset,
                                                MPI_BYTE, dst, MPIR_BCAST_TAG,
                                                comm, &status);
                          /* nprocs_completed is also equal to the no. of processes
                             whose data we don't have */
                          if (mpi_errno != MPI_SUCCESS) {
			      MPIU_ERR_POP(mpi_errno);
			  }
                          NMPI_Get_count(&status, MPI_BYTE, &recv_size);
                          curr_size += recv_size;
                      }
                      tmp_mask >>= 1;
                      k--;
                  }
              }
              /* --END EXPERIMENTAL-- */

              mask <<= 1;
              i++;
          }
      } else {
          /* long-message allgather or medium-size but non-power-of-two. use ring algorithm. */

          recvcnts = MPIU_Malloc(comm_size*sizeof(int));
	  /* --BEGIN ERROR HANDLING-- */
          if (!recvcnts) {
              mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem",
						"**nomem %d", comm_size * sizeof(int));
              return mpi_errno;
          }
	  /* --END ERROR HANDLING-- */
          displs = MPIU_Malloc(comm_size*sizeof(int));
	  /* --BEGIN ERROR HANDLING-- */
          if (!displs) {
              mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem",
						"**nomem %d", comm_size * sizeof(int));
              return mpi_errno;
          }
	  /* --END ERROR HANDLING-- */

          for (i=0; i<comm_size; i++)
	  {
              recvcnts[i] = nbytes - i*scatter_size;
              if (recvcnts[i] > scatter_size) {
                 recvcnts[i] = scatter_size;
              }
              if (recvcnts[i] < 0) {
                 recvcnts[i] = 0;
              }
          }

          displs[0] = 0;
          for (i=1; i<comm_size; i++) {
              displs[i] = displs[i-1] + recvcnts[i-1];
          }

          left  = (comm_size + rank - 1) % comm_size;
          right = (rank + 1) % comm_size;

          j     = rank;
          jnext = left;
          for (i=1; i<comm_size; i++)
	  {
              mpi_errno =
                  MPIC_Sendrecv((char *)tmp_buf +
                                displs[(j-root+comm_size)%comm_size],
                                recvcnts[(j-root+comm_size)%comm_size],
                                MPI_BYTE, right, MPIR_BCAST_TAG,
                                (char *)tmp_buf +
                                displs[(jnext-root+comm_size)%comm_size],
                                recvcnts[(jnext-root+comm_size)%comm_size],
                                MPI_BYTE, left,
                                MPIR_BCAST_TAG, comm, MPI_STATUS_IGNORE);
              if (mpi_errno != MPI_SUCCESS) {
		  MPIU_ERR_POP(mpi_errno);
	      }
              j	    = jnext;
              jnext = (comm_size + jnext - 1) % comm_size;
          }

          MPIU_Free(recvcnts);
          MPIU_Free(displs);
      }
  }

  if (!is_contig || !is_homogeneous)
  {
      if (rank != root)
      {
	  position = 0;
	  NMPI_Unpack(tmp_buf, nbytes, &position, buffer, count,
		      datatype, comm);
      }
      MPIU_Free(tmp_buf);
  }

 fn_exit:
  /* check if multiple threads are calling this collective function */
  MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

  return mpi_errno;
 fn_fail:
  goto fn_exit;
}
/* end:nested */

/* begin:nested */
/* Not PMPI_LOCAL because it is called in intercomm allgather */
int MPIR_Bcast_inter_OSU ( 
    void *buffer, 
    int count, 
    MPI_Datatype datatype, 
    int root, 
    MPID_Comm *comm_ptr )
{
/*  Intercommunicator broadcast.
    Root sends to rank 0 in remote group. Remote group does local
    intracommunicator broadcast.
*/
    static const char FCNAME[] = "MPIR_Bcast_inter_OSU";
    int rank, mpi_errno;
    MPI_Status status;
    MPID_Comm *newcomm_ptr = NULL;
    MPI_Comm comm;

    comm = comm_ptr->handle;

    if (root == MPI_PROC_NULL) {
        /* local processes other than root do nothing */
        mpi_errno = MPI_SUCCESS;
    } else if (root == MPI_ROOT) {
        /* root sends to rank 0 on remote group and returns */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
        mpi_errno =  MPIC_Send(buffer, count, datatype, 0,
                               MPIR_BCAST_TAG, comm); 
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS) {
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
					     FCNAME, __LINE__, MPI_ERR_OTHER,
					     "**fail", 0);
	}
	/* --END ERROR HANDLING-- */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
        return mpi_errno;
    } else {
        /* remote group. rank 0 on remote group receives from root */
        
        rank = comm_ptr->rank;
        
        if (rank == 0) {
            mpi_errno = MPIC_Recv(buffer, count, datatype, root,
                                  MPIR_BCAST_TAG, comm, &status);
	    /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS) {
		mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
						 FCNAME, __LINE__,
						 MPI_ERR_OTHER, "**fail", 0);
		return mpi_errno;
	    }
	    /* --END ERROR HANDLING-- */
        }
        
        /* Get the local intracommunicator */
        if (!comm_ptr->local_comm) {
            MPIR_Setup_intercomm_localcomm( comm_ptr );
        }

        newcomm_ptr = comm_ptr->local_comm;

        /* now do the usual broadcast on this intracommunicator
           with rank 0 as root. */
        mpi_errno = MPIR_Bcast_OSU(buffer, count, datatype, 0, newcomm_ptr);

	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS) {
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, 
                                             FCNAME, __LINE__, MPI_ERR_OTHER, 
                                             "**fail", 0);
	}
	/* --END ERROR HANDLING-- */
    }

    return mpi_errno;
}
/* end:nested */

#if defined(_OSU_MVAPICH_)
int knomial_2level_Bcast(
        void *buffer,
        int count,
        MPI_Datatype datatype,
        int nbytes,
        int root,
        MPID_Comm *comm_ptr)
{
    MPI_Comm comm, shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr = 0, *leader_commptr = 0;
    int local_rank = -1, global_rank = -1, local_size=0,rank,size;
    int leader_root = 0;
    int leader_of_root;
    int mpi_errno = MPI_SUCCESS;
    static const char FCNAME[] = "knomial_2level_Bcast";
    void *tmp_buf;
    int src,dst,mask,relative_rank,comm_size;
    int k;
    comm  = comm_ptr->handle;
    PMPI_Comm_size ( comm, &size );
    rank = comm_ptr->rank;

    
    shmem_comm = comm_ptr->shmem_comm;
    mpi_errno = PMPI_Comm_rank(shmem_comm, &local_rank);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(shmem_comm, &local_size);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }


    MPID_Comm_get_ptr(shmem_comm,shmem_commptr);

    leader_comm = comm_ptr->leader_comm;
    MPID_Comm_get_ptr(leader_comm,leader_commptr);
    
    if ((local_rank == 0)&&(local_size > 1)) {
        global_rank = leader_commptr->rank;
    }

    leader_of_root = comm_ptr->leader_map[root];
    leader_root = comm_ptr->leader_rank[leader_of_root];

    if (local_size > 1) {
        if ((local_rank == 0) &&
            (root != rank) &&
            (leader_root == global_rank)) {
             mpi_errno = MPIC_Recv (buffer,count,datatype, root,
                        MPIR_BCAST_TAG, comm, MPI_STATUS_IGNORE);
             if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
             }
        }
        if ((local_rank != 0) && (root == rank)) {
            mpi_errno  = MPIC_Send(buffer,count,datatype,
                                   leader_of_root, MPIR_BCAST_TAG,
                                   comm);
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
        }
    }

 /* inter-leader k-nomial broadcast */
      if (local_size != size && local_rank == 0) {
        rank = leader_commptr->rank;
        root = leader_root;
        comm_size = leader_commptr->local_size;

        relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;
        mask = 0x1;
        while (mask < comm_size) {
           if (relative_rank % (inter_node_knomial_factor*mask)) {
              src = relative_rank/(inter_node_knomial_factor*mask) * 
                           (inter_node_knomial_factor*mask) + root;
              if (src >= comm_size) { 
               src -= comm_size;
              }

            mpi_errno = MPIC_Recv(buffer,nbytes,MPI_BYTE,src,
                                  MPIR_BCAST_TAG,leader_comm,MPI_STATUS_IGNORE);
            if (mpi_errno != MPI_SUCCESS) {
                    MPIU_ERR_POP(mpi_errno);
             }
            break;
           }
        mask *= inter_node_knomial_factor;
       }
       mask /= inter_node_knomial_factor;
       while (mask > 0) {
         for(k=1;k<inter_node_knomial_factor;k++) {
            if (relative_rank + mask*k < comm_size) {
                dst = rank + mask*k;
                if (dst >= comm_size) {
                 dst -= comm_size;
                }
                mpi_errno = MPIC_Send (buffer,nbytes,MPI_BYTE,dst,
                                    MPIR_BCAST_TAG,leader_comm);
                if (mpi_errno != MPI_SUCCESS) {
                    MPIU_ERR_POP(mpi_errno);
                }
            }
         }
         mask /= inter_node_knomial_factor;
       }
     } 


   /* intra-node k-nomial bcast*/
    if(local_size > 1) { 
      rank = shmem_commptr->rank;
      root = 0;
      comm_size = shmem_commptr->local_size;

      relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;
      mask = 0x1;
      while (mask < comm_size) {
         if (relative_rank % (intra_node_knomial_factor*mask)) {
            src = relative_rank/(intra_node_knomial_factor*mask)*
                             (intra_node_knomial_factor*mask)+root;
            if (src >= comm_size) {
               src -= comm_size;
            }

          mpi_errno = MPIC_Recv(buffer,nbytes,MPI_BYTE,src,
                                MPIR_BCAST_TAG,shmem_comm,MPI_STATUS_IGNORE);
          if (mpi_errno != MPI_SUCCESS) {
                  MPIU_ERR_POP(mpi_errno);
           }
              break;
         }
         mask *= intra_node_knomial_factor;
      }
      mask /= intra_node_knomial_factor;
      while (mask > 0) {
       for(k=1;k<intra_node_knomial_factor;k++) {
          if (relative_rank + mask*k < comm_size) {
              dst = rank + mask*k;
              if (dst >= comm_size) {
                 dst -= comm_size;
              }
              mpi_errno = MPIC_Send (buffer,nbytes,MPI_BYTE,dst,
                                    MPIR_BCAST_TAG,shmem_comm);
              if (mpi_errno != MPI_SUCCESS) {
                  MPIU_ERR_POP(mpi_errno);
              }
           }
        }
         mask /= intra_node_knomial_factor;
      }
   } 

  fn_fail :
    return mpi_errno;
}


int MPID_SHMEM_BCAST_init(int file_size, int shmem_comm_rank, int my_local_rank, 
	int* bcast_seg_size, char** bcast_shmem_file, int* fd);

int MPID_SHMEM_BCAST_mmap(void** mmap_ptr, int bcast_seg_size, int fd, 
	int my_local_rank, char* bcast_shmem_file);

int viadev_use_shmem_ring= 1;
int intra_shmem_Bcast_Large( 
	void *buffer, 
	int count, 
    MPI_Datatype datatype,
	int nbytes,
	int root, 
	MPID_Comm *comm )
{
	MPI_Status status;
	int        rank, size, src, dst;
	int        relative_rank, mask;
	int        mpi_errno = MPI_SUCCESS;
	int scatter_size, curr_size, recv_size, send_size;
	int j=0, i;
	int *recvcnts = NULL, *displs = NULL, left, right, jnext;
	void *tmp_buf = NULL;

	char* shmem_buf;
	MPI_Comm shmem_comm, leader_comm = 0;
	MPID_Comm *comm_ptr = 0,*shmem_commptr = 0;
	int local_rank = -1, local_size=0, relative_lcomm_rank;
	int leader_comm_size, leader_comm_rank;
	int shmem_comm_rank, num_bytes=0, shmem_offset=-1;
	int index;
	int file_size = shmem_bcast_threshold;
        int ret_val = 0, flag = 0;
	/* Get my rank and switch communicators to the hidden collective */
        rank = comm->rank;
	comm_ptr = comm;
        size = comm_ptr->local_size;
	index = comm_ptr->bcast_index;
	/* Obtaining the shared memory communicator information */
	shmem_comm = comm_ptr->shmem_comm;
	/* MPI_Comm_rank(shmem_comm, &local_rank);
	MPI_Comm_size(shmem_comm, &local_size); */
        MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
        local_rank  = shmem_commptr->rank;
        local_size = shmem_commptr->local_size;

	shmem_comm_rank = shmem_commptr->shmem_comm_rank;

	/* Obtaining the Leader Communicator information */
	if (local_rank == 0) {
		leader_comm = comm_ptr->leader_comm;
		PMPI_Comm_rank(leader_comm, &leader_comm_rank);
               
	}
	leader_comm_size = comm_ptr->leader_group_size;

	/* Initialize the bcast segment for the first time */
	if (comm_ptr->bcast_mmap_ptr == NULL) {
		ret_val = MPID_SHMEM_BCAST_init(file_size, shmem_comm_rank, 
                                                local_rank, &(comm_ptr->bcast_seg_size), 
         					&(comm_ptr->bcast_shmem_file), 
                                                &(comm_ptr->bcast_fd));
                MPIR_Allreduce(&ret_val, &flag, 1, MPI_INT, MPI_LAND, comm_ptr);
                if (flag == 0) {
                   return -1;
                }
		MPIR_Barrier(shmem_commptr);
		MPID_SHMEM_BCAST_mmap(&(comm_ptr->bcast_mmap_ptr), comm_ptr->bcast_seg_size, 
				      comm_ptr->bcast_fd, local_rank,
                                      comm_ptr->bcast_shmem_file);
		MPIR_Barrier(shmem_commptr);
		if (local_rank == 0) {
			unlink(comm_ptr->bcast_shmem_file);
		}
	}

	if ((local_rank == 0) || (root == rank)) {

		MPID_SHMEM_COLL_GetShmemBcastBuf((void *)&shmem_buf,comm_ptr->bcast_mmap_ptr);
		/* The collective uses the shared buffer for inter and intra node */
		tmp_buf = shmem_buf;
	}

	if (root == rank) {
		mpi_errno = MPIC_Sendrecv(buffer, count, datatype, rank,
				MPIR_BCAST_TAG, shmem_buf, count, datatype, rank, 
				MPIR_BCAST_TAG, comm->handle, &status);
	}
	
	MPIR_Barrier(shmem_commptr);


	/* The Leader for the given root of the broadcast */
	int leader_of_root = comm_ptr->leader_map[root];
        /* The rank of the leader process in the Leader communicator*/
	int leader_comm_root = comm_ptr->leader_rank[leader_of_root];	

	relative_rank = (rank >= root) ? rank - root : rank - root + size;


	/* use long message algorithm: binomial tree scatter followed by an allgather */

	/* Scatter algorithm divides the buffer into nprocs pieces and
	   scatters them among the processes. Root gets the first piece,
	   root+1 gets the second piece, and so forth. Uses the same binomial
	   tree algorithm as above. Ceiling division
	   is used to compute the size of each piece. This means some
	   processes may not get any data. For example if bufsize = 97 and
	   nprocs = 16, ranks 15 and 16 will get 0 data. On each process, the
	   scattered data is stored at the same offset in the buffer as it is
	   on the root process. */ 

	if (local_rank == 0) {

		relative_lcomm_rank = (leader_comm_rank >= leader_comm_root) ? 
		(leader_comm_rank - leader_comm_root) : (leader_comm_rank - leader_comm_root + leader_comm_size);

		scatter_size = (nbytes + leader_comm_size - 1)/leader_comm_size; /* ceiling division */
		curr_size = (leader_comm_rank == leader_comm_root) ? nbytes : 0; /* root starts with all the data */

		mask = 0x1;
		while (mask < leader_comm_size) {
			if (relative_lcomm_rank & mask) {
				src = leader_comm_rank - mask; 
				if (src < 0) { 
                                   src += leader_comm_size;
                                }
				recv_size = nbytes - relative_lcomm_rank*scatter_size;
				/* recv_size is larger than what might actually be sent by the
				   sender. We don't need compute the exact value because MPI
				   allows you to post a larger recv.*/ 
				if (recv_size <= 0) {
					curr_size = 0; /* this process doesn't receive any data
							  because of uneven division */
                                } else {
					mpi_errno = MPIC_Recv((void *)((char *)tmp_buf + relative_lcomm_rank*scatter_size),
							recv_size, MPI_BYTE, src,
							MPIR_BCAST_TAG, leader_comm, &status);
					if (mpi_errno) {
                                           return mpi_errno;
                                        }

					/* query actual size of data received */
					PMPI_Get_count(&status, MPI_BYTE, &curr_size);
				}
				break;
			}
			mask <<= 1;
		}

		/* This process is responsible for all processes that have bits
		   set from the LSB upto (but not including) mask.  Because of
		   the "not including", we start by shifting mask back down
		   one. */

		mask >>= 1;
		while (mask > 0) {
			if (relative_lcomm_rank + mask < leader_comm_size) {

				send_size = curr_size - scatter_size * mask; 
				/* mask is also the size of this process's subtree */

				if (send_size > 0) {
					dst = leader_comm_rank + mask;
					if (dst >= leader_comm_size) {
                                           dst -= leader_comm_size;
                                        }
					mpi_errno = MPIC_Send (((char *)tmp_buf + scatter_size*(relative_lcomm_rank+mask)),
							send_size, MPI_BYTE, dst,
							MPIR_BCAST_TAG, leader_comm);
					if (mpi_errno) {
                                           return mpi_errno;
                                        }
					curr_size -= send_size;
				}
			}
			mask >>= 1;
		}
	}

      /* Scatter complete. Now do an allgather. */
      /* use ring algorithm. */ 
      if (local_rank == 0) {
          recvcnts =  MPIU_Malloc(leader_comm_size*sizeof(int));
          displs =  MPIU_Malloc(leader_comm_size*sizeof(int));

          for (i=0; i<leader_comm_size; i++) {
              recvcnts[i] = nbytes - i*scatter_size;
              if (recvcnts[i] > scatter_size) {
                  recvcnts[i] = scatter_size;
              } 
              if (recvcnts[i] < 0) {
                  recvcnts[i] = 0;
              }
          }

          displs[0] = 0;
          for (i=1; i<leader_comm_size; i++) {
              displs[i] = displs[i-1] + recvcnts[i-1]; 
          }

          left  = (leader_comm_size + leader_comm_rank - 1) % leader_comm_size;
          right = (leader_comm_rank + 1) % leader_comm_size;

          j     = leader_comm_rank;
          jnext = left;
          for (i=1; i<leader_comm_size; i++) {
              signal_local_processes(i, index, 
                      (char *)tmp_buf+displs[(j-leader_comm_root+leader_comm_size)%leader_comm_size], 
                      displs[(j-leader_comm_root+leader_comm_size)%leader_comm_size], 
                      recvcnts[(j-leader_comm_root+leader_comm_size)%leader_comm_size],
                      comm_ptr->bcast_mmap_ptr);			
              mpi_errno = 
                  MPIC_Sendrecv((char *)tmp_buf+displs[(j-leader_comm_root+leader_comm_size)%leader_comm_size],
                          recvcnts[(j-leader_comm_root+leader_comm_size)%leader_comm_size], MPI_BYTE, right, MPIR_BCAST_TAG,
                          (char *)tmp_buf + displs[(jnext-leader_comm_root+leader_comm_size)%leader_comm_size],
                          recvcnts[(jnext-leader_comm_root+leader_comm_size)%leader_comm_size], MPI_BYTE, left, 
                          MPIR_BCAST_TAG, leader_comm, &status );
              if (mpi_errno) {
                 break;
              }
              j	    = jnext;
              jnext = (leader_comm_size + jnext - 1) % leader_comm_size;
          }

      } else {
          for (i=1; i<leader_comm_size; i++) {
              wait_for_signal(i, index, &shmem_buf, &shmem_offset, &num_bytes, comm_ptr->bcast_mmap_ptr);

              /* Copy the data out from shmem buf */
              mpi_errno = MPIC_Sendrecv((void *)shmem_buf,
                      num_bytes, MPI_BYTE, rank, MPIR_BCAST_TAG, 
                      (void *)((char *)buffer + shmem_offset),
                      num_bytes, MPI_BYTE, rank,
                      MPIR_BCAST_TAG, comm->handle, &status);

          }
      }
      /* The leader copies the data only in the end from the shmem buffer */
      if (local_rank == 0) {
          signal_local_processes(i, index, 
                  (char *)tmp_buf+displs[(j-leader_comm_root+leader_comm_size)%leader_comm_size], 
                  displs[(j-leader_comm_root+leader_comm_size)%leader_comm_size], 
                  recvcnts[(j-leader_comm_root+leader_comm_size)%leader_comm_size],
                  comm_ptr->bcast_mmap_ptr);			
          /* Copy the data out from shmem buf */
          mpi_errno = MPIC_Sendrecv((void *)shmem_buf,
                  nbytes, MPI_BYTE, rank, MPIR_BCAST_TAG, 
                  (void *)((char *)buffer),
                  nbytes, MPI_BYTE, rank,
                  MPIR_BCAST_TAG, comm->handle, &status);
          MPIU_Free(recvcnts);
          MPIU_Free(displs);
      }	else {
          wait_for_signal(i, index, &shmem_buf, &shmem_offset, &num_bytes, comm_ptr->bcast_mmap_ptr);

          /* Copy the data out from shmem buf */
          mpi_errno = MPIC_Sendrecv((void *)shmem_buf,
                  num_bytes, MPI_BYTE, rank, MPIR_BCAST_TAG, 
                  (void *)((char *)buffer + shmem_offset),
                  num_bytes, MPI_BYTE, rank,
                  MPIR_BCAST_TAG, comm->handle, &status);
      }

      MPIR_Barrier(shmem_commptr);
      /* For the bcast signalling flags */
      index = (index + 1)%3;
      comm_ptr->bcast_index = index;

      return (mpi_errno);
}

#endif /* #if defined(_OSU_MVAPICH_) */ 
