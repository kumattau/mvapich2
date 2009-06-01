/*  (C)Copyright IBM Corp.  2007, 2008  */
/**
 * \file src/coll/bcast/mpido_bcast.c
 * \brief ???
 */

#include "mpido_coll.h"

#pragma weak PMPIDO_Bcast = MPIDO_Bcast
/**
 * **************************************************************************
 * \brief "Done" callback for collective broadcast message.
 * **************************************************************************
 */

static void cb_done (void *clientdata)
{
   volatile unsigned *work_left = (unsigned *) clientdata;
   *work_left = 0;
   MPID_Progress_signal();
   return;
}


static int tree_bcast(void * buffer,
                      int bytes,
                      int root,
                      DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   extern int DCMF_TREE_SMP_SHORTCUT;
if (DCMF_TREE_SMP_SHORTCUT) {
   rc = DCMF_GlobalBcast(&MPIDI_Protocols.globalbcast,
			(DCMF_Request_t *)&request,
			callback,
			DCMF_MATCH_CONSISTENCY,
			root,
			buffer,
			bytes);
} else {
   rc = DCMF_Broadcast(&MPIDI_CollectiveProtocols.broadcast.tree,
                       &request,
                       callback,
                       DCMF_MATCH_CONSISTENCY,
                       geometry,
                       root,
                       buffer,
                       bytes);
}
   MPID_PROGRESS_WAIT_WHILE(active);
   return rc;
}

static int async_binom_bcast(void * buffer,
                             int bytes,
                             int root,
                             DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = {cb_done, (void *)&active };
   rc = DCMF_AsyncBroadcast(
                     &MPIDI_CollectiveProtocols.broadcast.async_binomial,
                     &request,
                     callback,
                     DCMF_MATCH_CONSISTENCY,
                     geometry,
                     root,
                     buffer,
                     bytes);
   MPID_PROGRESS_WAIT_WHILE(active);

   return rc;
}

static int sync_binom_bcast(void * buffer,
                       int bytes,
                       int root,
                       DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   rc = DCMF_Broadcast(&MPIDI_CollectiveProtocols.broadcast.binomial,
			 &request,
			 callback,
			 DCMF_MATCH_CONSISTENCY,
			 geometry,
			 root,
			 buffer,
			 bytes);
   MPID_PROGRESS_WAIT_WHILE(active);

   return rc;
}
static int async_rect_bcast(void * buffer,
                            int bytes,
                            int root,
                            DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   rc = DCMF_AsyncBroadcast(
               &MPIDI_CollectiveProtocols.broadcast.async_rectangle,
			      &request,
			      callback,
			      DCMF_MATCH_CONSISTENCY,
			      geometry,
			      root,
			      buffer,
			      bytes);

     MPID_PROGRESS_WAIT_WHILE(active);

     return rc;
}

static int sync_rect_bcast(void * buffer,
                           int bytes,
                           int root,
                           DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };

   rc = DCMF_Broadcast(&MPIDI_CollectiveProtocols.broadcast.rectangle,
			 &request,
			 callback,
			 DCMF_MATCH_CONSISTENCY,
			 geometry,
			 root,
			 buffer,
			 bytes);

   MPID_PROGRESS_WAIT_WHILE(active);

   return rc;
}


int MPIDO_Bcast(void * buffer,
                int count,
                MPI_Datatype datatype,
                int root,
                MPID_Comm * comm_ptr)
{
   int data_sz, dt_contig, rc;

   MPID_Datatype *dt_ptr;
   MPI_Aint dt_true_lb;
   MPID_Segment segment;

   char *data_buffer;
   char *noncontigbuf = NULL;

   unsigned treeavail, binomavail=1, rectavail=1;

   if(comm_ptr->comm_kind != MPID_INTRACOMM || count == 0)
      return MPIR_Bcast(buffer, count, datatype, root, comm_ptr);

//   assert(comm_ptr->comm_kind != MPID_INTRACOMM);

   treeavail = comm_ptr->dcmf.bcasttree;

   rectavail =
      MPIDI_CollectiveProtocols.broadcast.userect &&
         DCMF_Geometry_analyze(&comm_ptr->dcmf.geometry,
         &MPIDI_CollectiveProtocols.broadcast.rectangle);

   binomavail =
      MPIDI_CollectiveProtocols.broadcast.usebinom &&
         DCMF_Geometry_analyze(&comm_ptr->dcmf.geometry,
         &MPIDI_CollectiveProtocols.broadcast.binomial);
         



#warning need benchmark data here
   int asyncrect = MPIDI_CollectiveProtocols.broadcast.useasyncrect;
   int asyncbinom = MPIDI_CollectiveProtocols.broadcast.useasyncbinom;

   if(!binomavail && !rectavail && !treeavail && 
      !asyncrect && !asyncbinom)
      return MPIR_Bcast(buffer, count, datatype, root, comm_ptr);


   MPIDI_Datatype_get_info(count,
                           datatype,
                           dt_contig,
                           data_sz,
                           dt_ptr,
                           dt_true_lb);

   MPID_Ensure_Aint_fits_in_pointer ( 
      (MPI_VOID_PTR_CAST_TO_MPI_AINT buffer + dt_true_lb));
   data_buffer = (char *)buffer+dt_true_lb;

  /* tree asserts if the data type size is actually 0. should we make
   * tree deal with a 0-byte bcast? */
   if(data_sz ==0)
      treeavail = 0;

   if(!dt_contig)
   {
      noncontigbuf = MPIU_Malloc(data_sz);
      data_buffer = noncontigbuf;
      if (noncontigbuf == NULL)
      {
         fprintf(stderr,
            "Pack: Tree Bcast cannot allocate local non-contig pack buffer\n");
         MPID_Dump_stacks();
         MPID_Abort(NULL, MPI_ERR_NO_SPACE, 1,
            "Fatal:  Cannot allocate pack buffer");
      }


      if(comm_ptr->rank == root)
      {
         /* Root:  Pack Data */
         DLOOP_Offset last = data_sz;
         MPID_Segment_init (buffer, count, datatype, &segment, 0);
         MPID_Segment_pack (&segment, 0, &last, noncontigbuf);
      }
   }


   if(treeavail)
   {
//      fprintf(stderr,
//          "tree: root: %d, comm size: %d %d, context_id: %d\n",
//               root, comm_ptr->local_size,
//               comm_ptr->remote_size,  comm_ptr->context_id);

      rc = tree_bcast(data_buffer,
                      data_sz,
                      comm_ptr->vcr[root]->lpid,
                      &comm_ptr->dcmf.geometry);
   }

   else if(rectavail && asyncrect)
   {
      if(data_sz < 131072 && comm_ptr->dcmf.bcastiter < 32)
      {
         comm_ptr->dcmf.bcastiter++;
         rc = async_rect_bcast(data_buffer,
                               data_sz,
                               comm_ptr->vcr[root]->lpid,
                               &comm_ptr->dcmf.geometry);
      }
      else
      {
         if(comm_ptr->dcmf.bcastiter==32)
            comm_ptr->dcmf.bcastiter = 0;
     
         rc = sync_rect_bcast(data_buffer,
                              data_sz,
                              comm_ptr->vcr[root]->lpid,
                              &comm_ptr->dcmf.geometry);
      }
   }
   else if(rectavail)
   {
         rc = sync_rect_bcast(data_buffer,
                              data_sz,
                              comm_ptr->vcr[root]->lpid,
                              &comm_ptr->dcmf.geometry);
   }
   else if(binomavail && asyncbinom)
   {
      if(data_sz < 262144 && comm_ptr->dcmf.bcastiter < 32)
      {
         comm_ptr->dcmf.bcastiter++;
         rc = async_binom_bcast(data_buffer,
                                data_sz,
                                comm_ptr->vcr[root]->lpid,
                                &comm_ptr->dcmf.geometry);
      }
      else
      {
         if(comm_ptr->dcmf.bcastiter==32)
            comm_ptr->dcmf.bcastiter = 0;

         rc = sync_binom_bcast(data_buffer,
                               data_sz,
                               comm_ptr->vcr[root]->lpid,
                               &comm_ptr->dcmf.geometry);
      }
   }
   else // if(binomaval)
   {
         rc = sync_binom_bcast(data_buffer,
                               data_sz,
                               comm_ptr->vcr[root]->lpid,
                               &comm_ptr->dcmf.geometry);

   }


   if(!dt_contig)
   {
      if(comm_ptr->rank != root)
      {
         int smpi_errno, rmpi_errno;
         MPIDI_msg_sz_t rcount;
         MPIDI_DCMF_Buffer_copy (noncontigbuf,
                                 data_sz,
                                 MPI_CHAR,
                                 &smpi_errno,
                                 buffer,
                                 count,
                                 datatype,
                                 &rcount,
                                 &rmpi_errno);
      }
      MPIU_Free(noncontigbuf);
      noncontigbuf = NULL;
   }
   return rc;

}
