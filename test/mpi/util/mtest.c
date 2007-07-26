/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include "mpitestconf.h"
#include "mpitest.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

/*
 * Utility routines for writing MPI tests.
 *
 * We check the return codes on all MPI routines (other than INIT)
 * to allow the program that uses these routines to select MPI_ERRORS_RETURN
 * as the error handler.  We do *not* set MPI_ERRORS_RETURN because
 * the code that makes use of these routines may not check return
 * codes.
 * 
 */

/* Here is where we could put the includes and definitions to enable
   memory testing */

static int dbgflag = 0;         /* Flag used for debugging */
static int wrank = -1;          /* World rank */
static int verbose = 0;         /* Message level (0 is none) */
/* 
 * Initialize and Finalize MTest
 */

/*
   Initialize MTest, initializing MPI if necessary.  

 Environment Variables:
+ MPITEST_DEBUG - If set (to any value), turns on debugging output
- MPITEST_VERBOSE - If set to a numeric value, turns on that level of
  verbose output.  This is used by the routine 'MTestPrintfMsg'

*/
void MTest_Init( int *argc, char ***argv )
{
    int flag;
    char *envval = 0;

    MPI_Initialized( &flag );
    if (!flag) {
	MPI_Init( argc, argv );
    }
    /* Check for debugging control */
    if (getenv( "MPITEST_DEBUG" )) {
	dbgflag = 1;
	MPI_Comm_rank( MPI_COMM_WORLD, &wrank );
    }

    /* Check for verbose control */
    envval = getenv( "MPITEST_VERBOSE" );
    if (envval) {
	char *s;
	long val = strtol( envval, &s, 0 );
	if (s == envval) {
	    /* This is the error case for strtol */
	    fprintf( stderr, "Warning: %s not valid for MPITEST_VERBOSE\n", 
		     envval );
	    fflush( stderr );
	}
	else {
	    if (val >= 0) {
		verbose = val;
	    }
	    else {
		fprintf( stderr, "Warning: %s not valid for MPITEST_VERBOSE\n", 
			 envval );
		fflush( stderr );
	    }
	}
    }
}

/*
  Finalize MTest.  errs is the number of errors on the calling process; 
  this routine will write the total number of errors over all of MPI_COMM_WORLD
  to the process with rank zero, or " No Errors".
  It does *not* finalize MPI.
 */
void MTest_Finalize( int errs )
{
    int rank, toterrs, merr;

    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    if (merr) MTestPrintError( merr );

    merr = MPI_Reduce( &errs, &toterrs, 1, MPI_INT, MPI_SUM, 
		      0, MPI_COMM_WORLD );
    if (merr) MTestPrintError( merr );
    if (rank == 0) {
	if (toterrs) {
	    printf( " Found %d errors\n", toterrs );
	}
	else {
	    printf( " No Errors\n" );
	}
	fflush( stdout );
    }
}

/*
 * Miscellaneous utilities, particularly to eliminate OS dependencies
 * from the tests.
 * MTestSleep( seconds )
 */
#ifdef HAVE_WINDOWS_H
#include <windows.h>
void MTestSleep( int sec )
{
    Sleep( 1000 * sec );
}
#else
#include <unistd.h>
void MTestSleep( int sec )
{
    sleep( sec );
}
#endif

/*
 * Datatypes
 *
 * Eventually, this could read a description of a file.  For now, we hard 
 * code the choices.
 *
 * Each kind of datatype has the following functions:
 *    MTestTypeXXXInit     - Initialize a send buffer for that type
 *    MTestTypeXXXInitRecv - Initialize a receive buffer for that type
 *    MTestTypeXXXFree     - Free any buffers associate with that type
 *    MTestTypeXXXCheckbuf - Check that the buffer contains the expected data
 * These routines work with (nearly) any datatype that is of type XXX, 
 * allowing the test codes to create a variety of contiguous, vector, and
 * indexed types, then test them by calling these routines.
 *
 * Available types (for the XXX) are
 *    Contig   - Simple contiguous buffers
 *    Vector   - Simple strided "vector" type
 *    Indexed  - Indexed datatype.  Only for a count of 1 instance of the 
 *               datatype
 */
static int datatype_index = 0;

/* ------------------------------------------------------------------------ */
/* Datatype routines for contiguous datatypes                               */
/* ------------------------------------------------------------------------ */
/* 
 * Setup contiguous buffers of n copies of a datatype.
 */
static void *MTestTypeContigInit( MTestDatatype *mtype )
{
    MPI_Aint size;
    int merr;

    if (mtype->count > 0) {
	signed char *p;
	int  i, totsize;
	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize = size * mtype->count;
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p = (signed char *)(mtype->buf);
	if (!p) {
	    /* Error - out of memory */
	    MTestError( "Out of memory in type buffer init" );
	}
	for (i=0; i<totsize; i++) {
	    p[i] = 0xff ^ (i & 0xff);
	}
    }
    else {
	if (mtype->buf) {
	    free( mtype->buf );
	}
	mtype->buf = 0;
    }
    return mtype->buf;
}

/* 
 * Setup contiguous buffers of n copies of a datatype.  Initialize for
 * reception (e.g., set initial data to detect failure)
 */
static void *MTestTypeContigInitRecv( MTestDatatype *mtype )
{
    MPI_Aint size;
    int      merr;

    if (mtype->count > 0) {
	signed char *p;
	int  i, totsize;
	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize = size * mtype->count;
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p = (signed char *)(mtype->buf);
	if (!p) {
	    /* Error - out of memory */
	    MTestError( "Out of memory in type buffer init" );
	}
	for (i=0; i<totsize; i++) {
	    p[i] = 0xff;
	}
    }
    else {
	if (mtype->buf) {
	    free( mtype->buf );
	}
	mtype->buf = 0;
    }
    return mtype->buf;
}
static void *MTestTypeContigFree( MTestDatatype *mtype )
{
    if (mtype->buf) {
	free( mtype->buf );
	mtype->buf = 0;
    }
    return 0;
}
static int MTestTypeContigCheckbuf( MTestDatatype *mtype )
{
    unsigned char *p;
    unsigned char expected;
    int  i, totsize, err = 0, merr;
    MPI_Aint size;

    p = (unsigned char *)mtype->buf;
    if (p) {
	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize = size * mtype->count;
	for (i=0; i<totsize; i++) {
	    expected = (0xff ^ (i & 0xff));
	    if (p[i] != expected) {
		err++;
		if (mtype->printErrors && err < 10) {
		    printf( "Data expected = %x but got %x for %dth entry\n",
			    expected, p[i], i );
		    fflush( stdout );
		}
	    }
	}
    }
    return err;
}

/* ------------------------------------------------------------------------ */
/* Datatype routines for vector datatypes                                   */
/* ------------------------------------------------------------------------ */

static void *MTestTypeVectorInit( MTestDatatype *mtype )
{
    MPI_Aint size;
    int      merr;

    if (mtype->count > 0) {
	unsigned char *p;
	int  i, j, k, nc, totsize;

	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize	   = mtype->count * size;
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p	   = (unsigned char *)(mtype->buf);
	if (!p) {
	    /* Error - out of memory */
	    MTestError( "Out of memory in type buffer init" );
	}

	/* First, set to -1 */
	for (i=0; i<totsize; i++) p[i] = 0xff;

	/* Now, set the actual elements to the successive values.
	   To do this, we need to run 3 loops */
	nc = 0;
	/* count is usually one for a vector type */
	for (k=0; k<mtype->count; k++) {
	    /* For each element (block) */
	    for (i=0; i<mtype->nelm; i++) {
		/* For each value */
		for (j=0; j<mtype->blksize; j++) {
		    p[j] = (0xff ^ (nc & 0xff));
		    nc++;
		}
		p += mtype->stride;
	    }
	}
    }
    else {
	mtype->buf = 0;
    }
    return mtype->buf;
}

static void *MTestTypeVectorFree( MTestDatatype *mtype )
{
    if (mtype->buf) {
	free( mtype->buf );
	mtype->buf = 0;
    }
    return 0;
}

# if 0
/* The following is the contig init code */
static void *MTestTypeVectorInitRecv( MTestDatatype *mtype )
{
    MPI_Aint size;
    int      merr;

    if (mtype->count > 0) {
	signed char *p;
	int  i, totsize;
	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize = size * mtype->count;
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p = (signed char *)(mtype->buf);
	if (!p) {
	    /* Error - out of memory */
	    MTestError( "Out of memory in type buffer init" );
	}
	for (i=0; i<totsize; i++) {
	    p[i] = 0xff;
	}
    }
    else {
	if (mtype->buf) {
	    free( mtype->buf );
	}
	mtype->buf = 0;
    }
    return mtype->buf;
}

static int MTestTypeVectorCheckbuf( MTestDatatype *mtype )
{
    unsigned char *p;
    unsigned char expected;
    int  i, totsize, err = 0, merr;
    MPI_Aint size;

    p = (unsigned char *)mtype->buf;
    if (p) {
	merr = MPI_Type_extent( mtype->datatype, &size );
	if (merr) MTestPrintError( merr );
	totsize = size * mtype->count;

	/* count is usually one for a vector type */
	nc = 0;
	for (k=0; k<mtype->count; k++) {
	    /* For each element (block) */
	    for (i=0; i<mtype->nelm; i++) {
		expected = (0xff ^ (nc & 0xff));
		/* For each value */
		for (j=0; j<mtype->blksize; j++) {
		    if (p[j] != expected) {
			err++;
			if (mtype->printErrors && err < 10) {
			    printf( "Data expected = %x but got %x for %dth entry\n",
				    expected, p[j], j );
			    fflush( stdout );
			}
		    nc++;
		}
		p += mtype->stride;
	    }
	}
    }
    return err;
}
#endif
/* ------------------------------------------------------------------------ */
/* Datatype routines for indexed block datatypes                            */
/* ------------------------------------------------------------------------ */

/* 
 * Setup a buffer for one copy of an indexed datatype. 
 */
static void *MTestTypeIndexedInit( MTestDatatype *mtype )
{
    MPI_Aint totsize;
    int      merr;
    
    if (mtype->count > 1) {
	MTestError( "This datatype is supported only for a single count" );
    }
    if (mtype->count == 1) {
	signed char *p;
	int  i, k, offset, j;

	/* Allocate the send/recv buffer */
	merr = MPI_Type_extent( mtype->datatype, &totsize );
	if (merr) MTestPrintError( merr );
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p = (signed char *)(mtype->buf);
	if (!p) {
	    MTestError( "Out of memory in type buffer init\n" );
	}
	/* Initialize the elements */
	/* First, set to -1 */
	for (i=0; i<totsize; i++) p[i] = 0xff;

	/* Now, set the actual elements to the successive values.
	   We require that the base type is a contiguous type */
	k = 0;
	for (i=0; i<mtype->nelm; i++) {
	    int b;
	    /* Compute the offset: */
	    offset = mtype->displs[i] * mtype->basesize;
	    /* For each element in the block */
	    for (b=0; b<mtype->index[i]; b++) {
		for (j=0; j<mtype->basesize; j++) {
		    p[offset+j] = 0xff ^ (k++ & 0xff);
		}
		offset += mtype->basesize;
	    }
	}
    }
    else {
	/* count == 0 */
	if (mtype->buf) {
	    free( mtype->buf );
	}
	mtype->buf = 0;
    }
    return mtype->buf;
}

/* 
 * Setup indexed buffers for 1 copy of a datatype.  Initialize for
 * reception (e.g., set initial data to detect failure)
 */
static void *MTestTypeIndexedInitRecv( MTestDatatype *mtype )
{
    MPI_Aint totsize;
    int      merr;

    if (mtype->count > 1) {
	MTestError( "This datatype is supported only for a single count" );
    }
    if (mtype->count == 1) {
	signed char *p;
	int  i;
	merr = MPI_Type_extent( mtype->datatype, &totsize );
	if (merr) MTestPrintError( merr );
	if (!mtype->buf) {
	    mtype->buf = (void *) malloc( totsize );
	}
	p = (signed char *)(mtype->buf);
	if (!p) {
	    /* Error - out of memory */
	    MTestError( "Out of memory in type buffer init\n" );
	}
	for (i=0; i<totsize; i++) {
	    p[i] = 0xff;
	}
    }
    else {
	/* count == 0 */
	if (mtype->buf) {
	    free( mtype->buf );
	}
	mtype->buf = 0;
    }
    return mtype->buf;
}

static void *MTestTypeIndexedFree( MTestDatatype *mtype )
{
    if (mtype->buf) {
	free( mtype->buf );
	free( mtype->displs );
	free( mtype->index );
	mtype->buf    = 0;
	mtype->displs = 0;
	mtype->index  = 0;
    }
    return 0;
}

static int MTestTypeIndexedCheckbuf( MTestDatatype *mtype )
{
    unsigned char *p;
    unsigned char expected;
    int  i, err = 0, merr;
    MPI_Aint totsize;

    p = (unsigned char *)mtype->buf;
    if (p) {
	int j, k, offset;
	merr = MPI_Type_extent( mtype->datatype, &totsize );
	if (merr) MTestPrintError( merr );
	
	k = 0;
	for (i=0; i<mtype->nelm; i++) {
	    int b;
	    /* Compute the offset: */
	    offset = mtype->displs[i] * mtype->basesize;
	    for (b=0; b<mtype->index[i]; b++) {
		for (j=0; j<mtype->basesize; j++) {
		    expected = (0xff ^ (k & 0xff));
		    if (p[offset+j] != expected) {
			err++;
			if (mtype->printErrors && err < 10) {
			    printf( "Data expected = %x but got %x for %dth entry\n",
				    expected, p[offset+j], k );
			    fflush( stdout );
			}
		    }
		    k++;
		}
		offset += mtype->basesize;
	    }
	}
    }
    return err;
}


/* ------------------------------------------------------------------------ */
/* Routines to select a datatype and associated buffer create/fill/check    */
/* routines                                                                 */
/* ------------------------------------------------------------------------ */

/* 
   Create a range of datatypes with a given count elements.
   This uses a selection of types, rather than an exhaustive collection.
   It allocates both send and receive types so that they can have the same
   type signature (collection of basic types) but different type maps (layouts
   in memory) 
 */
int MTestGetDatatypes( MTestDatatype *sendtype, MTestDatatype *recvtype,
		       int count )
{
    int merr;
    int i;

    sendtype->InitBuf	  = 0;
    sendtype->FreeBuf	  = 0;
    sendtype->CheckBuf	  = 0;
    sendtype->datatype	  = 0;
    sendtype->isBasic	  = 0;
    sendtype->printErrors = 0;
    recvtype->InitBuf	  = 0;
    recvtype->FreeBuf	  = 0;

    recvtype->CheckBuf	  = 0;
    recvtype->datatype	  = 0;
    recvtype->isBasic	  = 0;
    recvtype->printErrors = 0;

    sendtype->buf	  = 0;
    recvtype->buf	  = 0;

    /* Set the defaults for the message lengths */
    sendtype->count	  = count;
    recvtype->count	  = count;
    /* Use datatype_index to choose a datatype to use.  If at the end of the
       list, return 0 */
    switch (datatype_index) {
    case 0:
	sendtype->datatype = MPI_INT;
	sendtype->isBasic  = 1;
	recvtype->datatype = MPI_INT;
	recvtype->isBasic  = 1;
	break;
    case 1:
	sendtype->datatype = MPI_DOUBLE;
	sendtype->isBasic  = 1;
	recvtype->datatype = MPI_DOUBLE;
	recvtype->isBasic  = 1;
	break;
    case 2:
	sendtype->datatype = MPI_INT;
	sendtype->isBasic  = 1;
	recvtype->datatype = MPI_BYTE;
	recvtype->isBasic  = 1;
	recvtype->count    *= sizeof(int);
	break;
    case 3:
	sendtype->datatype = MPI_FLOAT_INT;
	sendtype->isBasic  = 1;
	recvtype->datatype = MPI_FLOAT_INT;
	recvtype->isBasic  = 1;
	break;
    case 4:
	merr = MPI_Type_dup( MPI_INT, &sendtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( sendtype->datatype, "dup of MPI_INT" );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_dup( MPI_INT, &recvtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( recvtype->datatype, "dup of MPI_INT" );
	if (merr) MTestPrintError( merr );
	/* dup'ed types are already committed if the original type 
	   was committed (MPI-2, section 8.8) */
	break;
    case 5:
	/* vector send type and contiguous receive type */
	/* These sizes are in bytes (see the VectorInit code) */
 	sendtype->stride   = 3 * sizeof(int);
	sendtype->blksize  = sizeof(int);
	sendtype->nelm     = recvtype->count;

	merr = MPI_Type_vector( recvtype->count, 1, 3, MPI_INT, 
				&sendtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &sendtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( sendtype->datatype, "int-vector" );
	if (merr) MTestPrintError( merr );
	sendtype->count    = 1;
 	recvtype->datatype = MPI_INT;
	recvtype->isBasic  = 1;
	sendtype->InitBuf  = MTestTypeVectorInit;
	recvtype->InitBuf  = MTestTypeContigInitRecv;
	sendtype->FreeBuf  = MTestTypeVectorFree;
	recvtype->FreeBuf  = MTestTypeContigFree;
	sendtype->CheckBuf = 0;
	recvtype->CheckBuf = MTestTypeContigCheckbuf;
	break;

    case 6:
	/* Indexed send using many small blocks and contig receive */
	sendtype->blksize  = sizeof(int);
	sendtype->nelm     = recvtype->count;
	sendtype->basesize = sizeof(int);
	sendtype->displs   = (int *)malloc( sendtype->nelm * sizeof(int) );
	sendtype->index    = (int *)malloc( sendtype->nelm * sizeof(int) );
	if (!sendtype->displs || !sendtype->index) {
	    MTestError( "Out of memory in type init\n" );
	}
	/* Make the sizes larger (4 ints) to help push the total
	   size to over 256k in some cases, as the MPICH2 code as of
	   10/1/06 used large internal buffers for packing non-contiguous
	   messages */
	for (i=0; i<sendtype->nelm; i++) {
	    sendtype->index[i]   = 4;
	    sendtype->displs[i]  = 5*i;
	}
	merr = MPI_Type_indexed( sendtype->nelm,
				 sendtype->index, sendtype->displs, 
				 MPI_INT, &sendtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &sendtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( sendtype->datatype, "int-indexed(4-int)" );
	if (merr) MTestPrintError( merr );
	sendtype->count    = 1;
	sendtype->InitBuf  = MTestTypeIndexedInit;
	sendtype->FreeBuf  = MTestTypeIndexedFree;
	sendtype->CheckBuf = 0;

 	recvtype->datatype = MPI_INT;
	recvtype->isBasic  = 1;
	recvtype->count    = count * 4;
	recvtype->InitBuf  = MTestTypeContigInitRecv;
	recvtype->FreeBuf  = MTestTypeContigFree;
	recvtype->CheckBuf = MTestTypeContigCheckbuf;
	break;

    case 7:
	/* Indexed send using 2 large blocks and contig receive */
	sendtype->blksize  = sizeof(int);
	sendtype->nelm     = 2;
	sendtype->basesize = sizeof(int);
	sendtype->displs   = (int *)malloc( sendtype->nelm * sizeof(int) );
	sendtype->index    = (int *)malloc( sendtype->nelm * sizeof(int) );
	if (!sendtype->displs || !sendtype->index) {
	    MTestError( "Out of memory in type init\n" );
	}
	sendtype->index[0]   = (recvtype->count + 1) / 2;
	sendtype->displs[0]  = 0;
	sendtype->index[1]   = (recvtype->count + 1) / 2;
	sendtype->displs[1]  = sendtype->index[0] + 1;

	merr = MPI_Type_indexed( sendtype->nelm,
				 sendtype->index, sendtype->displs, 
				 MPI_INT, &sendtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &sendtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( sendtype->datatype, "int-indexed(2 blocks)" );
	if (merr) MTestPrintError( merr );
	sendtype->count    = 1;
	sendtype->InitBuf  = MTestTypeIndexedInit;
	sendtype->FreeBuf  = MTestTypeIndexedFree;
	sendtype->CheckBuf = 0;

 	recvtype->datatype = MPI_INT;
	recvtype->isBasic  = 1;
	recvtype->count    = sendtype->index[0] + sendtype->index[1];
	recvtype->InitBuf  = MTestTypeContigInitRecv;
	recvtype->FreeBuf  = MTestTypeContigFree;
	recvtype->CheckBuf = MTestTypeContigCheckbuf;
	break;

    case 8:
	/* Indexed receive using many small blocks and contig send */
	recvtype->blksize  = sizeof(int);
	recvtype->nelm     = recvtype->count;
	recvtype->basesize = sizeof(int);
	recvtype->displs   = (int *)malloc( recvtype->nelm * sizeof(int) );
	recvtype->index    = (int *)malloc( recvtype->nelm * sizeof(int) );
	if (!recvtype->displs || !recvtype->index) {
	    MTestError( "Out of memory in type recv init\n" );
	}
	/* Make the sizes larger (4 ints) to help push the total
	   size to over 256k in some cases, as the MPICH2 code as of
	   10/1/06 used large internal buffers for packing non-contiguous
	   messages */
	for (i=0; i<recvtype->nelm; i++) {
	    recvtype->index[i]   = 4;
	    recvtype->displs[i]  = 5*i;
	}
	merr = MPI_Type_indexed( recvtype->nelm,
				 recvtype->index, recvtype->displs, 
				 MPI_INT, &recvtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &recvtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( recvtype->datatype, "recv-int-indexed(4-int)" );
	if (merr) MTestPrintError( merr );
	recvtype->count    = 1;
	recvtype->InitBuf  = MTestTypeIndexedInitRecv;
	recvtype->FreeBuf  = MTestTypeIndexedFree;
	recvtype->CheckBuf = MTestTypeIndexedCheckbuf;

 	sendtype->datatype = MPI_INT;
	sendtype->isBasic  = 1;
	sendtype->count    = count * 4;
	sendtype->InitBuf  = MTestTypeContigInit;
	sendtype->FreeBuf  = MTestTypeContigFree;
	sendtype->CheckBuf = 0;
	break;

#if 0
    case 9:
	/* vector recv type and contiguous send type */
	/* These sizes are in bytes (see the VectorInit code) */
	recvtype->stride   = 4 * sizeof(int);
	recvtype->blksize  = sizeof(int);
	recvtype->nelm     = recvtype->count;

	merr = MPI_Type_vector( sendtype->count, 1, 4, MPI_INT, 
				&recvtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &recvtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( recvtype->datatype, "int-vector" );
	if (merr) MTestPrintError( merr );
	recvtype->count    = 1;
	sendtype->datatype = MPI_INT;
	sendtype->isBasic  = 1;
	recvtype->InitBuf  = MTestTypeVectorInit;
	sendtype->InitBuf  = MTestTypeContigInitRecv;
	recvtype->FreeBuf  = MTestTypeVectorFree;
	sendtype->FreeBuf  = MTestTypeContigFree;
	recvtype->CheckBuf = MTestTypeVectorCheckbuf;
	sendtype->CheckBuf = 0;
	break;
    case 10:
	/* contig send and block indexed recv */
	/* Make indexes 5*((count-1) - i), for i=0, ..., count-1, i.e., 
	   every 5th element, but starting from the end. */
	recvtype->blksize  = sizeof(int);
	recvtype->nelm     = recvtype->count;

	sendtype->displs = (int *) malloc( sendtype->count );
	if (!sendtype->displs) {
	    MTestError( "Out of memory in indexed block" );
	}
	for (i=0; i<sendtype->count; i++) {
	    sendtype->displs[i] = 5 * ( (count-1) - i );
	}
	sendtype->basesize = sizeof(int);
	sendtype->nelm     = sendtype->count;
	merr = MPI_Type_create_index_block( sendtype->count, 1, 
					    sendtype->displs, 
					    MPI_INT, &recvtype->datatype );
	if (merr) MTestPrintError( merr );
        merr = MPI_Type_commit( &recvtype->datatype );
	if (merr) MTestPrintError( merr );
	merr = MPI_Type_set_name( recvtype->datatype, 
				  "int-decreasing-indexed" );
	if (merr) MTestPrintError( merr );
	recvtype->count    = 1;
	sendtype->datatype = MPI_INT;
	sendtype->isBasic  = 1;
	recvtype->InitBuf  = MTestTypeIndexedInit;
	sendtype->InitBuf  = MTestTypeContigInitRecv;
	recvtype->FreeBuf  = MTestTypeIndexedFree;
	sendtype->FreeBuf  = MTestTypeContigFree;
	recvtype->CheckBuf = MTestTypeIndexedCheckBuf;
	sendtype->CheckBuf = 0;
	break;
    case 11: 
	/* index send and vector recv (using shorts) */
	break;
#endif
    default:
	datatype_index = -1;
    }

    if (!sendtype->InitBuf) {
	sendtype->InitBuf  = MTestTypeContigInit;
	recvtype->InitBuf  = MTestTypeContigInitRecv;
	sendtype->FreeBuf  = MTestTypeContigFree;
	recvtype->FreeBuf  = MTestTypeContigFree;
	sendtype->CheckBuf = MTestTypeContigCheckbuf;
	recvtype->CheckBuf = MTestTypeContigCheckbuf;
    }
    datatype_index++;

    if (dbgflag && datatype_index > 0) {
	int typesize;
	fprintf( stderr, "%d: sendtype is %s\n", wrank, MTestGetDatatypeName( sendtype ) );
	merr = MPI_Type_size( sendtype->datatype, &typesize );
	if (merr) MTestPrintError( merr );
	fprintf( stderr, "%d: sendtype size = %d\n", wrank, typesize );
	fprintf( stderr, "%d: recvtype is %s\n", wrank, MTestGetDatatypeName( recvtype ) );
	merr = MPI_Type_size( recvtype->datatype, &typesize );
	if (merr) MTestPrintError( merr );
	fprintf( stderr, "%d: recvtype size = %d\n", wrank, typesize );
	fflush( stderr );
	
    }
    return datatype_index;
}

/* Reset the datatype index (start from the initial data type.
   Note: This routine is rarely needed; MTestGetDatatypes automatically
   starts over after the last available datatype is used.
*/
void MTestResetDatatypes( void )
{
    datatype_index = 0;
}
/* Return the index of the current datatype.  This is rarely needed and
   is provided mostly to enable debugging of the MTest package itself */
int MTestGetDatatypeIndex( void )
{
    return datatype_index;
}

/* Free the storage associated with a datatype */
void MTestFreeDatatype( MTestDatatype *mtype )
{
    int merr;
    /* Invoke a datatype-specific free function to handle
       both the datatype and the send/receive buffers */
    if (mtype->FreeBuf) {
	(mtype->FreeBuf)( mtype );
    }
    /* Free the datatype itself if it was created */
    if (!mtype->isBasic) {
	merr = MPI_Type_free( &mtype->datatype );
	if (merr) MTestPrintError( merr );
    }
}

/* Check that a message was received correctly.  Returns the number of
   errors detected.  Status may be NULL or MPI_STATUS_IGNORE */
int MTestCheckRecv( MPI_Status *status, MTestDatatype *recvtype )
{
    int count;
    int errs = 0, merr;

    if (status && status != MPI_STATUS_IGNORE) {
	merr = MPI_Get_count( status, recvtype->datatype, &count );
	if (merr) MTestPrintError( merr );
	
	/* Check count against expected count */
	if (count != recvtype->count) {
	    errs ++;
	}
    }

    /* Check received data */
    if (!errs && recvtype->CheckBuf( recvtype )) {
	errs++;
    }
    return errs;
}

/* This next routine uses a circular buffer of static name arrays just to
   simplify the use of the routine */
const char *MTestGetDatatypeName( MTestDatatype *dtype )
{
    static char name[4][MPI_MAX_OBJECT_NAME];
    static int sp=0;
    int rlen, merr;

    if (sp >= 4) sp = 0;
    merr = MPI_Type_get_name( dtype->datatype, name[sp], &rlen );
    if (merr) MTestPrintError( merr );
    return (const char *)name[sp++];
}
/* ----------------------------------------------------------------------- */

/* 
 * Create communicators.  Use separate routines for inter and intra
 * communicators (there is a routine to give both)
 * Note that the routines may return MPI_COMM_NULL, so code should test for
 * that return value as well.
 * 
 */
static int interCommIdx = 0;
static int intraCommIdx = 0;
static const char *intraCommName = 0;
static const char *interCommName = 0;

/* 
 * Get an intracommunicator with at least min_size members.  If "allowSmaller"
 * is true, allow the communicator to be smaller than MPI_COMM_WORLD and
 * for this routine to return MPI_COMM_NULL for some values.  Returns 0 if
 * no more communicators are available.
 */
int MTestGetIntracommGeneral( MPI_Comm *comm, int min_size, int allowSmaller )
{
    int size, rank, merr;
    int done=0;
    int isBasic = 0;

    /* The while loop allows us to skip communicators that are too small.
       MPI_COMM_NULL is always considered large enough */
    while (!done) {
	intraCommName = "";
	switch (intraCommIdx) {
	case 0:
	    *comm = MPI_COMM_WORLD;
	    isBasic = 1;
	    intraCommName = "MPI_COMM_WORLD";
	    break;
	case 1:
	    /* dup of world */
	    merr = MPI_Comm_dup(MPI_COMM_WORLD, comm );
	    if (merr) MTestPrintError( merr );
	    intraCommName = "Dup of MPI_COMM_WORLD";
	    break;
	case 2:
	    /* reverse ranks */
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_split( MPI_COMM_WORLD, 0, size-rank, comm );
	    if (merr) MTestPrintError( merr );
	    intraCommName = "Rank reverse of MPI_COMM_WORLD";
	    break;
	case 3:
	    /* subset of world, with reversed ranks */
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_split( MPI_COMM_WORLD, (rank < size/2), 
				   size-rank, comm );
	    if (merr) MTestPrintError( merr );
	    intraCommName = "Rank reverse of half of MPI_COMM_WORLD";
	    break;
	case 4:
	    *comm = MPI_COMM_SELF;
	    isBasic = 1;
	    intraCommName = "MPI_COMM_SELF";
	    break;

	    /* These next cases are communicators that include some
	       but not all of the processes */
	case 5:
	case 6:
	case 7:
	case 8:
	{
	    int newsize;
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    newsize = size - (intraCommIdx - 4);
	    
	    if (allowSmaller && newsize >= min_size) {
		merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
		if (merr) MTestPrintError( merr );
		merr = MPI_Comm_split( MPI_COMM_WORLD, rank < newsize, rank, 
				       comm );
		if (merr) MTestPrintError( merr );
		if (rank >= newsize) {
		    merr = MPI_Comm_free( comm );
		    if (merr) MTestPrintError( merr );
		    *comm = MPI_COMM_NULL;
		}
		else {
		    intraCommName = "Split of WORLD";
		}
	    }
	    else {
		/* Act like default */
		*comm = MPI_COMM_NULL;
		isBasic = 1;
		intraCommName = "MPI_COMM_NULL";
		intraCommIdx = -1;
	    }
	}
	break;
	    
	    /* Other ideas: dup of self, cart comm, graph comm */
	default:
	    *comm = MPI_COMM_NULL;
	    isBasic = 1;
	    intraCommName = "MPI_COMM_NULL";
	    intraCommIdx = -1;
	    break;
	}

	if (*comm != MPI_COMM_NULL) {
	    merr = MPI_Comm_size( *comm, &size );
	    if (merr) MTestPrintError( merr );
	    if (size >= min_size) 
		done = 1;
	    else {
		/* Try again */
		if (!isBasic) {
		    merr = MPI_Comm_free( comm );
		    if (merr) MTestPrintError( merr );
		}
		intraCommIdx++;
	    }
	}
	else
	    done = 1;
    }

    intraCommIdx++;
    return intraCommIdx;
}

/* 
 * Get an intracommunicator with at least min_size members.
 */
int MTestGetIntracomm( MPI_Comm *comm, int min_size ) 
{
    return MTestGetIntracommGeneral( comm, min_size, 0 );
}

/* Return the name of an intra communicator */
const char *MTestGetIntracommName( void )
{
    return intraCommName;
}

/* 
 * Return an intercomm; set isLeftGroup to 1 if the calling process is 
 * a member of the "left" group.
 */
int MTestGetIntercomm( MPI_Comm *comm, int *isLeftGroup, int min_size )
{
    int size, rank, remsize, merr;
    int done=0;
    MPI_Comm mcomm;
    int rleader;

    /* The while loop allows us to skip communicators that are too small.
       MPI_COMM_NULL is always considered large enough.  The size is
       the sum of the sizes of the local and remote groups */
    while (!done) {
	switch (interCommIdx) {
	case 0:
	    /* Split comm world in half */
	    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    if (size > 1) {
		merr = MPI_Comm_split( MPI_COMM_WORLD, (rank < size/2), rank, 
				       &mcomm );
		if (merr) MTestPrintError( merr );
		if (rank == 0) {
		    rleader = size/2;
		}
		else if (rank == size/2) {
		    rleader = 0;
		}
		else {
		    /* Remote leader is signficant only for the processes
		       designated local leaders */
		    rleader = -1;
		}
		*isLeftGroup = rank < size/2;
		merr = MPI_Intercomm_create( mcomm, 0, MPI_COMM_WORLD, rleader,
					     12345, comm );
		if (merr) MTestPrintError( merr );
		merr = MPI_Comm_free( &mcomm );
		if (merr) MTestPrintError( merr );
		interCommName = "Intercomm by splitting MPI_COMM_WORLD";
	    }
	    else 
		*comm = MPI_COMM_NULL;
	    break;
	case 1:
	    /* Split comm world in to 1 and the rest */
	    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    if (size > 1) {
		merr = MPI_Comm_split( MPI_COMM_WORLD, rank == 0, rank, 
				       &mcomm );
		if (merr) MTestPrintError( merr );
		if (rank == 0) {
		    rleader = 1;
		}
		else if (rank == 1) {
		    rleader = 0;
		}
		else {
		    /* Remote leader is signficant only for the processes
		       designated local leaders */
		    rleader = -1;
		}
		*isLeftGroup = rank == 0;
		merr = MPI_Intercomm_create( mcomm, 0, MPI_COMM_WORLD, 
					     rleader, 12346, comm );
		if (merr) MTestPrintError( merr );
		merr = MPI_Comm_free( &mcomm );
		if (merr) MTestPrintError( merr );
		interCommName = "Intercomm by splitting MPI_COMM_WORLD into 1, rest";
	    }
	    else 
		*comm = MPI_COMM_NULL;
	    break;

	case 2:
	    /* Split comm world in to 2 and the rest */
	    merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_size( MPI_COMM_WORLD, &size );
	    if (merr) MTestPrintError( merr );
	    if (size > 3) {
		merr = MPI_Comm_split( MPI_COMM_WORLD, rank < 2, rank, 
				       &mcomm );
		if (merr) MTestPrintError( merr );
		if (rank == 0) {
		    rleader = 2;
		}
		else if (rank == 2) {
		    rleader = 0;
		}
		else {
		    /* Remote leader is signficant only for the processes
		       designated local leaders */
		    rleader = -1;
		}
		*isLeftGroup = rank < 2;
		merr = MPI_Intercomm_create( mcomm, 0, MPI_COMM_WORLD, 
					     rleader, 12347, comm );
		if (merr) MTestPrintError( merr );
		merr = MPI_Comm_free( &mcomm );
		if (merr) MTestPrintError( merr );
		interCommName = "Intercomm by splitting MPI_COMM_WORLD into 2, rest";
	    }
	    else 
		*comm = MPI_COMM_NULL;
	    break;
	    
	default:
	    *comm = MPI_COMM_NULL;
	    interCommName = "MPI_COMM_NULL";
	    interCommIdx = -1;
	    break;
	}
	if (*comm != MPI_COMM_NULL) {
	    merr = MPI_Comm_size( *comm, &size );
	    if (merr) MTestPrintError( merr );
	    merr = MPI_Comm_remote_size( *comm, &remsize );
	    if (merr) MTestPrintError( merr );
	    if (size + remsize >= min_size) done = 1;
	}
	else
	    done = 1;
    }

    interCommIdx++;
    return interCommIdx;
}
/* Return the name of an intercommunicator */
const char *MTestGetIntercommName( void )
{
    return interCommName;
}

/* Get a communicator of a given minimum size.  Both intra and inter 
   communicators are provided */
int MTestGetComm( MPI_Comm *comm, int min_size )
{
    int idx=0;
    static int getinter = 0;

    if (!getinter) {
	idx = MTestGetIntracomm( comm, min_size );
	if (idx == 0) {
	    getinter = 1;
	}
    }
    if (getinter) {
	int isLeft;
	idx = MTestGetIntercomm( comm, &isLeft, min_size );
	if (idx == 0) {
	    getinter = 0;
	}
    }

    return idx;
}

/* Free a communicator.  It may be called with a predefined communicator
 or MPI_COMM_NULL */
void MTestFreeComm( MPI_Comm *comm )
{
    int merr;
    if (*comm != MPI_COMM_WORLD &&
	*comm != MPI_COMM_SELF &&
	*comm != MPI_COMM_NULL) {
	merr = MPI_Comm_free( comm );
	if (merr) MTestPrintError( merr );
    }
}

/* ------------------------------------------------------------------------ */
void MTestPrintError( int errcode )
{
    int errclass, slen;
    char string[MPI_MAX_ERROR_STRING];
    
    MPI_Error_class( errcode, &errclass );
    MPI_Error_string( errcode, string, &slen );
    printf( "Error class %d (%s)\n", errclass, string );
    fflush( stdout );
}
void MTestPrintErrorMsg( const char msg[], int errcode )
{
    int errclass, slen;
    char string[MPI_MAX_ERROR_STRING];
    
    MPI_Error_class( errcode, &errclass );
    MPI_Error_string( errcode, string, &slen );
    printf( "%s: Error class %d (%s)\n", msg, errclass, string ); 
    fflush( stdout );
}
/* ------------------------------------------------------------------------ */
void MTestPrintfMsg( int level, const char format[], ... )
{
    va_list list;
    int n;

    if (verbose && level >= verbose) {
	va_start(list,format);
	n = vprintf( format, list );
	va_end(list);
	fflush(stdout);
    }
}
/* Fatal error.  Report and exit */
void MTestError( const char *msg )
{
    fprintf( stderr, "%s\n", msg );
    fflush( stderr );
    MPI_Abort( MPI_COMM_WORLD, 1 );
}
/* ------------------------------------------------------------------------ */
#ifdef HAVE_MPI_WIN_CREATE
/*
 * Create MPI Windows
 */
static int win_index = 0;
static const char *winName;
/* Use an attribute to remember the type of memory allocation (static,
   malloc, or MPI_Alloc_mem) */
static int mem_keyval = MPI_KEYVAL_INVALID;
int MTestGetWin( MPI_Win *win, int mustBePassive )
{
    static char actbuf[1024];
    static char *pasbuf;
    char        *buf;
    int         n, rank, merr;
    MPI_Info    info;

    if (mem_keyval == MPI_KEYVAL_INVALID) {
	/* Create the keyval */
	merr = MPI_Win_create_keyval( MPI_WIN_NULL_COPY_FN, 
				      MPI_WIN_NULL_DELETE_FN, 
				      &mem_keyval, 0 );
	if (merr) MTestPrintError( merr );

    }

    switch (win_index) {
    case 0:
	/* Active target window */
	merr = MPI_Win_create( actbuf, 1024, 1, MPI_INFO_NULL, MPI_COMM_WORLD, 
			       win );
	if (merr) MTestPrintError( merr );
	winName = "active-window";
	merr = MPI_Win_set_attr( *win, mem_keyval, (void *)0 );
	if (merr) MTestPrintError( merr );
	break;
    case 1:
	/* Passive target window */
	merr = MPI_Alloc_mem( 1024, MPI_INFO_NULL, &pasbuf );
	if (merr) MTestPrintError( merr );
	merr = MPI_Win_create( pasbuf, 1024, 1, MPI_INFO_NULL, MPI_COMM_WORLD, 
			       win );
	if (merr) MTestPrintError( merr );
	winName = "passive-window";
	merr = MPI_Win_set_attr( *win, mem_keyval, (void *)2 );
	if (merr) MTestPrintError( merr );
	break;
    case 2:
	/* Active target; all windows different sizes */
	merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	if (merr) MTestPrintError( merr );
	n = rank * 64;
	if (n) 
	    buf = (char *)malloc( n );
	else
	    buf = 0;
	merr = MPI_Win_create( buf, n, 1, MPI_INFO_NULL, MPI_COMM_WORLD, 
			       win );
	if (merr) MTestPrintError( merr );
	winName = "active-all-different-win";
	merr = MPI_Win_set_attr( *win, mem_keyval, (void *)1 );
	if (merr) MTestPrintError( merr );
	break;
    case 3:
	/* Active target, no locks set */
	merr = MPI_Comm_rank( MPI_COMM_WORLD, &rank );
	if (merr) MTestPrintError( merr );
	n = rank * 64;
	if (n) 
	    buf = (char *)malloc( n );
	else
	    buf = 0;
	merr = MPI_Info_create( &info );
	if (merr) MTestPrintError( merr );
	merr = MPI_Info_set( info, "nolocks", "true" );
	if (merr) MTestPrintError( merr );
	merr = MPI_Win_create( buf, n, 1, info, MPI_COMM_WORLD, win );
	if (merr) MTestPrintError( merr );
	merr = MPI_Info_free( &info );
	if (merr) MTestPrintError( merr );
	winName = "active-nolocks-all-different-win";
	merr = MPI_Win_set_attr( *win, mem_keyval, (void *)1 );
	if (merr) MTestPrintError( merr );
	break;
    default:
	win_index = -1;
    }
    win_index++;
    return win_index;
}
/* Return a pointer to the name associated with a window object */
const char *MTestGetWinName( void )
{
    return winName;
}
/* Free the storage associated with a window object */
void MTestFreeWin( MPI_Win *win )
{
    void *addr;
    int  flag, merr;

    merr = MPI_Win_get_attr( *win, MPI_WIN_BASE, &addr, &flag );
    if (merr) MTestPrintError( merr );
    if (!flag) {
	MTestError( "Could not get WIN_BASE from window" );
    }
    if (addr) {
	void *val;
	merr = MPI_Win_get_attr( *win, mem_keyval, &val, &flag );
	if (merr) MTestPrintError( merr );
	if (flag) {
	    if (val == (void *)1) {
		free( addr );
	    }
	    else if (val == (void *)2) {
		merr = MPI_Free_mem( addr );
		if (merr) MTestPrintError( merr );
	    }
	    /* if val == (void *)0, then static data that must not be freed */
	}
    }
    merr = MPI_Win_free(win);
    if (merr) MTestPrintError( merr );
}
#endif
