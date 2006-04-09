/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include <stdio.h>
#include "mpi.h"

int main( int argc, char *argv[] )
{
    int a, b, ierr;

    MPI_Init( &argc, &argv );
    
    MPI_Errhandler_set( MPI_COMM_WORLD, MPI_ERRORS_RETURN );
    
    ierr = MPI_Reduce( &a, &b, 1, MPI_BYTE, MPI_MAX, 0, MPI_COMM_WORLD );
    printf( "ierr = %d\n", ierr );
    MPI_Finalize( );
    return 0;
}
