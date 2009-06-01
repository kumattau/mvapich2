/*  (C)Copyright IBM Corp.  2007, 2008  */

/**
 * \file src/coll/allreduce/mpido_allreduce.c
 * \brief ???
 */

#include "mpido_coll.h"

#pragma weak PMPIDO_Allreduce = MPIDO_Allreduce

/**
 * **************************************************************************
 * \brief "Done" callback for collective allreduce message.
 * **************************************************************************
 */


static void cb_done (void *clientdata)
{
   volatile unsigned *work_left = (unsigned *) clientdata;
   *work_left = 0;
   MPID_Progress_signal();

   return;

}

static int tree_global_allreduce(void * sendbuf,
                                 void * recvbuf,
                                 int count,
                                 DCMF_Dt dcmf_dt,
                                 DCMF_Op dcmf_op,
                                 DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   int root = -1;
   rc = DCMF_GlobalAllreduce(&MPIDI_Protocols.globalallreduce,
                             (DCMF_Request_t *)&request,
                             callback,
                             DCMF_MATCH_CONSISTENCY,
                             root,
                             sendbuf,
                             recvbuf,
                             count,
                             dcmf_dt,
                             dcmf_op);
   MPID_PROGRESS_WAIT_WHILE(active);

   return rc;
}

static int tree_pipelined_allreduce(void * sendbuf,
				    void * recvbuf,
				    int count,
				    DCMF_Dt dcmf_dt,
				    DCMF_Op dcmf_op,
				    DCMF_Geometry_t * geometry)
{
   int rc;
   unsigned local_alignment = 0;
   volatile int global_alignment = 0;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   DCMF_CollectiveProtocol_t *protocol = &(MPIDI_CollectiveProtocols.allreduce.pipelinedtree);

   /* Short messages use the unaligned optimizations */
   if (count < 1024) {
     rc = DCMF_Allreduce(protocol,
			 &request,
			 callback,
			 DCMF_MATCH_CONSISTENCY,
			 geometry,
			 sendbuf,
			 recvbuf,
			 count,
			 dcmf_dt,
			 dcmf_op);
     MPID_PROGRESS_WAIT_WHILE(active);
   }
   else {
     /*First we need to verify alignment */
     local_alignment  = ( (((unsigned)sendbuf & 0x0f)==0) && (((unsigned)recvbuf & 0x0f)==0) );
     global_alignment = 0;

     /* Avoid the worst case in ccmi where two different protocols
	alternate on the same communicator, resulting in temporary
	buffers being freed and re-allocated. The fix would be to keep
	the allreducestate persistent across allreduce calls that
	different protocols.  - SK 04/04/08 */
     tree_global_allreduce((char *)&local_alignment,
			   (char *)&global_alignment,
			   1,
			   DCMF_UNSIGNED_INT,
			   DCMF_LAND,
			   geometry);

     if (global_alignment) { /*src and dst buffers are globally aligned*/
       protocol = &MPIDI_CollectiveProtocols.allreduce.pipelinedtree_dput;
     }

     active = 1;
     rc = DCMF_Allreduce(protocol,
			 &request,
			 callback,
			 DCMF_MATCH_CONSISTENCY,
			 geometry,
			 sendbuf,
			 recvbuf,
			 count,
			 dcmf_dt,
			 dcmf_op);
     MPID_PROGRESS_WAIT_WHILE(active);
   }

   return rc;
}


static int tree_allreduce(void * sendbuf,
                          void * recvbuf,
                          int count,
                          DCMF_Dt dcmf_dt,
                          DCMF_Op dcmf_op,
                          DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };
   rc = DCMF_Allreduce(&MPIDI_CollectiveProtocols.allreduce.tree,
                       &request,
                       callback,
                       DCMF_MATCH_CONSISTENCY,
                       geometry,
                       sendbuf,
                       recvbuf,
                       count,
                       dcmf_dt,
                       dcmf_op);
   MPID_PROGRESS_WAIT_WHILE(active);

   return rc;
}


static int binom_allreduce(void * sendbuf,
                           void * recvbuf,
                           int count,
                           DCMF_Dt dcmf_dt,
                           DCMF_Op dcmf_op,
                           DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };


   rc = DCMF_Allreduce(&MPIDI_CollectiveProtocols.allreduce.binomial,
                       &request,
                       callback,
                       DCMF_MATCH_CONSISTENCY,
                       geometry,
                       sendbuf,
                       recvbuf,
                       count,
                       dcmf_dt,
                       dcmf_op);


   MPID_PROGRESS_WAIT_WHILE(active);
   return rc;
}

static int rect_allreduce(void * sendbuf,
                          void * recvbuf,
                          int count,
                          DCMF_Dt dcmf_dt,
                          DCMF_Op dcmf_op,
                          DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };

   rc = DCMF_Allreduce(&MPIDI_CollectiveProtocols.allreduce.rectangle,
                       &request,
                       callback,
                       DCMF_MATCH_CONSISTENCY,
                       geometry,
                       sendbuf,
                       recvbuf,
                       count,
                       dcmf_dt,
                       dcmf_op);

   MPID_PROGRESS_WAIT_WHILE(active);
   return rc;
}



static int rectring_allreduce(void * sendbuf,
                          void * recvbuf,
                          int count,
                          DCMF_Dt dcmf_dt,
                          DCMF_Op dcmf_op,
                          DCMF_Geometry_t * geometry)
{
   int rc;
   DCMF_CollectiveRequest_t request;
   volatile unsigned active = 1;
   DCMF_Callback_t callback = { cb_done, (void *) &active };

   rc = DCMF_Allreduce(&MPIDI_CollectiveProtocols.allreduce.rectanglering,
                       &request,
                       callback,
                       DCMF_MATCH_CONSISTENCY,
                       geometry,
                       sendbuf,
                       recvbuf,
                       count,
                       dcmf_dt,
                       dcmf_op);

   MPID_PROGRESS_WAIT_WHILE(active);
   return rc;
}



int MPIDO_Allreduce(
            void * sendbuf,
            void * recvbuf,
            int count,
            MPI_Datatype datatype,
            MPI_Op op,
            MPID_Comm * comm_ptr)
{
   int dt_contig, dt_extent, rc;
   unsigned treeavail, rectavail, binomavail, rectringavail;

   MPID_Datatype *dt_ptr;
   MPI_Aint dt_true_lb=0;


   DCMF_Dt dcmf_dt = DCMF_UNDEFINED_DT;
   DCMF_Op dcmf_op = DCMF_UNDEFINED_OP;


   if(count == 0)
      return MPI_SUCCESS;
   treeavail = comm_ptr->dcmf.allreducetree | 
     comm_ptr->dcmf.allreduceccmitree | 
     comm_ptr->dcmf.allreducepipelinedtree;

   rc = MPIDI_ConvertMPItoDCMF(op, &dcmf_op, datatype, &dcmf_dt);

   extern int DCMF_TREE_SMP_SHORTCUT;

   if(rc == 0 && treeavail && comm_ptr->local_size > 2)
   {
      if(sendbuf == MPI_IN_PLACE)
         sendbuf = recvbuf;
      if(DCMF_TREE_SMP_SHORTCUT && comm_ptr->dcmf.allreducetree)
        rc = tree_global_allreduce(sendbuf,
                            recvbuf,
                            count,
                            dcmf_dt,
                            dcmf_op,
                            &comm_ptr->dcmf.geometry);
      else if (comm_ptr->dcmf.allreduceccmitree)
        rc = tree_allreduce(sendbuf,
                            recvbuf,
                            count,
                            dcmf_dt,
                            dcmf_op,
                            &comm_ptr->dcmf.geometry);
      else if (comm_ptr->dcmf.allreducepipelinedtree)
        rc = tree_pipelined_allreduce(sendbuf,
                                      recvbuf,
                                      count,
                                      dcmf_dt,
                                      dcmf_op,
                                      &comm_ptr->dcmf.geometry);
      return rc;
   }


   /* quick exit conditions */
   if(comm_ptr->comm_kind != MPID_INTRACOMM)
      return MPIR_Allreduce(sendbuf, recvbuf, count, datatype, op, comm_ptr);

   /* check geometry for possibilities */
   rectavail = MPIDI_CollectiveProtocols.allreduce.userect &&
               DCMF_Geometry_analyze(&comm_ptr->dcmf.geometry,
                     &MPIDI_CollectiveProtocols.allreduce.rectangle);

   rectringavail = MPIDI_CollectiveProtocols.allreduce.userectring &&
               DCMF_Geometry_analyze(&comm_ptr->dcmf.geometry,
                     &MPIDI_CollectiveProtocols.allreduce.rectanglering);

   binomavail = MPIDI_CollectiveProtocols.allreduce.usebinom &&
                DCMF_Geometry_analyze(&comm_ptr->dcmf.geometry,
                     &MPIDI_CollectiveProtocols.allreduce.binomial);

   if(sendbuf == MPI_IN_PLACE)
      binomavail = 0; /* temporary bug workaround */


//   assert(comm_ptr->comm_kind != MPID_INTRACOMM);

   MPIDI_Datatype_get_info(count,
                           datatype,
                           dt_contig,
                           dt_extent,
                           dt_ptr,
                           dt_true_lb);

   MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT recvbuf +
                                   dt_true_lb);
   recvbuf = ((char *)recvbuf + dt_true_lb);


   /* return conditions */
   if(
   // unsupported datatype or op
   rc == -1 ||
   // no optimized topologies for this geometry
   (!rectavail && !binomavail && !rectringavail) ||
   // return to mpich for 1 processor reduce
   (comm_ptr -> local_size <=2))
   {
      return MPIR_Allreduce(sendbuf, recvbuf, count, datatype, op, comm_ptr);
   }

#warning need benchmark data at this point
   /* at this point, decide which network/algorithm we are using based on
    * benchmark data, the op, the type, etc, etc
    * until then just pick rectangle then binomial based on availability*/
   unsigned usingbinom=1 && binomavail;
   unsigned usingrect=1 && rectavail;
   unsigned usingrectring=(rectringavail && count > 16384);


   if(sendbuf != MPI_IN_PLACE)
   {
      MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT sendbuf +
                                       dt_true_lb);
      sendbuf = ((char *)sendbuf + dt_true_lb);
     //      int err =
     //         MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count, datatype);
     //      if (err) return err;
   }
   else
     sendbuf = recvbuf;

   if(usingrectring)
   {
//         fprintf(stderr,"rectring allreduce, count: %d, dt: %d, op: %d, send: 0x%x, recv: 0x%x\n", count, dcmf_dt, dcmf_op, sendbuf, recvbuf);
      rc = rectring_allreduce(sendbuf,
                          recvbuf,
                          count,
                          dcmf_dt,
                          dcmf_op,
                          &comm_ptr->dcmf.geometry);
   }

   else if(usingrect)
   {
//         fprintf(stderr,"rect allreduce, count: %d, dt: %d, op: %d, send: 0x%x, recv: 0x%x\n", count, dcmf_dt, dcmf_op, sendbuf, recvbuf);
      rc = rect_allreduce(sendbuf,
                          recvbuf,
                          count,
                          dcmf_dt,
                          dcmf_op,
                          &comm_ptr->dcmf.geometry);
   }
   else if(usingbinom)
   {
//         fprintf(stderr,"binom allreduce, count: %d, dt: %d, op: %d, send: 0x%x, recv: 0x%x\n", count, dcmf_dt, dcmf_op, sendbuf, recvbuf);
      rc = binom_allreduce(sendbuf,
                           recvbuf,
                           count,
                           dcmf_dt,
                           dcmf_op,
                           &comm_ptr->dcmf.geometry);
   }

   return rc;
}
