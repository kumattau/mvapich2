/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *
 * This file is automatically generated by buildiface 
 * DO NOT EDIT
 */
#include "mpi_fortimpl.h"


/* Begin MPI profiling block */
#if defined(USE_WEAK_SYMBOLS) && !defined(USE_ONLY_MPI_NAMES) 
#if defined(HAVE_MULTIPLE_PRAGMA_WEAK) && defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL MPI_ERRHANDLER_GET( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_errhandler_get_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ERRHANDLER_GET = pmpi_errhandler_get__
#pragma weak mpi_errhandler_get__ = pmpi_errhandler_get__
#pragma weak mpi_errhandler_get_ = pmpi_errhandler_get__
#pragma weak mpi_errhandler_get = pmpi_errhandler_get__
#pragma weak pmpi_errhandler_get_ = pmpi_errhandler_get__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_ERRHANDLER_GET( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ERRHANDLER_GET = PMPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get__( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_errhandler_get__ = pmpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_errhandler_get = pmpi_errhandler_get
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_errhandler_get_ = pmpi_errhandler_get_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ERRHANDLER_GET  MPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get__  mpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get  mpi_errhandler_get
#else
#pragma _HP_SECONDARY_DEF pmpi_errhandler_get_  mpi_errhandler_get_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ERRHANDLER_GET as PMPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_errhandler_get__ as pmpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_errhandler_get as pmpi_errhandler_get
#else
#pragma _CRI duplicate mpi_errhandler_get_ as pmpi_errhandler_get_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_ERRHANDLER_GET( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ERRHANDLER_GET = mpi_errhandler_get__
#pragma weak mpi_errhandler_get_ = mpi_errhandler_get__
#pragma weak mpi_errhandler_get = mpi_errhandler_get__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_errhandler_get_ PMPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_get_ pmpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_get_ pmpi_errhandler_get
#else
#define mpi_errhandler_get_ pmpi_errhandler_get_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_Errhandler_get PMPI_Errhandler_get 

#else

#ifdef F77_NAME_UPPER
#define mpi_errhandler_get_ MPI_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_errhandler_get_ mpi_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_errhandler_get_ mpi_errhandler_get
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_errhandler_get_ ( MPI_Fint *v1, MPI_Fint *v2, MPI_Fint *ierr ){
    *ierr = MPI_Errhandler_get( (MPI_Comm)(*v1), v2 );
}
