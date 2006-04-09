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
extern FORT_DLL_SPEC void FORT_CALL MPI_KEYVAL_FREE( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free__( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free_( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_keyval_free_( MPI_Fint *, MPI_Fint * );

#pragma weak MPI_KEYVAL_FREE = pmpi_keyval_free__
#pragma weak mpi_keyval_free__ = pmpi_keyval_free__
#pragma weak mpi_keyval_free_ = pmpi_keyval_free__
#pragma weak mpi_keyval_free = pmpi_keyval_free__
#pragma weak pmpi_keyval_free_ = pmpi_keyval_free__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_KEYVAL_FREE( MPI_Fint *, MPI_Fint * );

#pragma weak MPI_KEYVAL_FREE = PMPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free__( MPI_Fint *, MPI_Fint * );

#pragma weak mpi_keyval_free__ = pmpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free( MPI_Fint *, MPI_Fint * );

#pragma weak mpi_keyval_free = pmpi_keyval_free
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free_( MPI_Fint *, MPI_Fint * );

#pragma weak mpi_keyval_free_ = pmpi_keyval_free_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_KEYVAL_FREE  MPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_free__  mpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_keyval_free  mpi_keyval_free
#else
#pragma _HP_SECONDARY_DEF pmpi_keyval_free_  mpi_keyval_free_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_KEYVAL_FREE as PMPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_keyval_free__ as pmpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_keyval_free as pmpi_keyval_free
#else
#pragma _CRI duplicate mpi_keyval_free_ as pmpi_keyval_free_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_KEYVAL_FREE( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free__( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free( MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_keyval_free_( MPI_Fint *, MPI_Fint * );

#pragma weak MPI_KEYVAL_FREE = mpi_keyval_free__
#pragma weak mpi_keyval_free_ = mpi_keyval_free__
#pragma weak mpi_keyval_free = mpi_keyval_free__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_keyval_free_ PMPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_free_ pmpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_free_ pmpi_keyval_free
#else
#define mpi_keyval_free_ pmpi_keyval_free_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_Keyval_free PMPI_Keyval_free 

#else

#ifdef F77_NAME_UPPER
#define mpi_keyval_free_ MPI_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_keyval_free_ mpi_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_keyval_free_ mpi_keyval_free
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_keyval_free_ ( MPI_Fint *v1, MPI_Fint *ierr ){
    *ierr = MPI_Keyval_free( v1 );
}
