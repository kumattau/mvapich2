/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/*
  Check that MPI_xxxx_c2f, applied to the same object several times,
  yields the same handle.  We do this because when MPI handles in 
  C are a different length than those in Fortran, care needs to 
  be exercised to ensure that the mapping from one to another is unique.
  (Test added to test a potential problem in ROMIO for handling MPI_File
  on 64-bit systems)
*/
#include "mpi.h"
#include <stdio.h>

int main( int argc, char *argv[] )
{
    MPI_Fint handleA, handleB;
    int      rc;
    int      errs = 0;
    int      rank;
    int      buf[1];
    MPI_Request cRequest;

    MPI_Init( &argc, &argv );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );

    /* Request */
    rc = MPI_Irecv( buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, &cRequest );
    if (rc) {
	errs++;
	printf( "Unable to create request\n" );
    }
    else {
	handleA = MPI_Request_c2f( cRequest );
	handleB = MPI_Request_c2f( cRequest );
	if (handleA != handleB) {
	    errs++;
	    printf( "MPI_Request_c2f does not give the same handle twice on the same MPI_Request\n" );
	}
    }
    MPI_Cancel( &cRequest );
    MPI_Request_free( &cRequest );

    if (rank == 0) {
	if (errs) {
	    fprintf(stderr, "Found %d errors\n", errs);
	}
	else {
	    printf(" No Errors\n");
	}
    }
    
    MPI_Finalize();
    
    return 0;
}
