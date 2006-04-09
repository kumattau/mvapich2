/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include "mpitest.h"

static char MTEST_Descrip[] = "Test comm_call_errhandler";

static int calls = 0;
static int errs = 0;
static MPI_Comm mycomm;
void eh( MPI_Comm *comm, int *err, ... )
{
    if (*err != MPI_ERR_OTHER) {
	errs++;
	printf( "Unexpected error code\n" );
    }
    if (*comm != mycomm) {
	errs++;
	printf( "Unexpected communicator\n" );
    }
    calls++;
    return;
}
int main( int argc, char *argv[] )
{
    MPI_Comm      comm;
    MPI_Errhandler newerr;

    MTest_Init( &argc, &argv );

    comm = MPI_COMM_WORLD;
    mycomm = comm;

    MPI_Comm_create_errhandler( eh, &newerr );

    MPI_Comm_set_errhandler( comm, newerr );
    MPI_Comm_call_errhandler( comm, MPI_ERR_OTHER );
    MPI_Errhandler_free( &newerr );
    if (calls != 1) {
	errs++;
	printf( "Error handler not called\n" );
    }

    MTest_Finalize( errs );
    MPI_Finalize();
    return 0;
}
