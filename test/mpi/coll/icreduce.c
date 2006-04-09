/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "mpitest.h"

static char MTEST_Descrip[] = "Simple intercomm reduce test";

int main( int argc, char *argv[] )
{
    int errs = 0, err;
    int *sendbuf = 0, *recvbuf=0;
    int leftGroup, i, count, rank;
    MPI_Comm comm;
    MPI_Datatype datatype;

    MTest_Init( &argc, &argv );

    datatype = MPI_INT;
    while (MTestGetIntercomm( &comm, &leftGroup, 4 )) {
	for (count = 1; count < 65000; count = 2 * count) {
	    sendbuf = (int *)malloc( count * sizeof(int) );
	    recvbuf = (int *)malloc( count * sizeof(int) );
	    /* Get an intercommunicator */
	    for (i=0; i<count; i++) {
		sendbuf[i] = -1;
		recvbuf[i] = -1;
	    }
	    if (leftGroup) {
		MPI_Comm_rank( comm, &rank );
		err = MPI_Reduce( sendbuf, recvbuf, count, datatype, MPI_SUM,
				 (rank == 0) ? MPI_ROOT : MPI_PROC_NULL,
				 comm );
		if (err) {
		    errs++;
		    MTestPrintError( err );
		}
		/* Test that no other process in this group received the 
		   broadcast, and that we got the right answers */
		if (rank == 0) {
		    int rsize;
		    MPI_Comm_remote_size( comm, &rsize );
		    for (i=0; i<count; i++) {
			if (recvbuf[i] != i * rsize) {
			    errs++;
			}
		    }
		}
		else {
		    for (i=0; i<count; i++) {
			if (recvbuf[i] != -1) {
			    errs++;
			}
		    }
		}
	    }
	    else {
		/* In the right group */
		for (i=0; i<count; i++) sendbuf[i] = i;
		err = MPI_Reduce( sendbuf, recvbuf, count, datatype, MPI_SUM, 
				  0, comm );
		if (err) {
		    errs++;
		    MTestPrintError( err );
		}
		/* Check that we have received no data */
		for (i=0; i<count; i++) {
		    if (recvbuf[i] != -1) {
			errs++;
		    }
		}
	    }
	free( sendbuf ); 
	free( recvbuf );
	}
	MTestFreeComm( &comm );
    }

    MTest_Finalize( errs );
    MPI_Finalize();
    return 0;
}
