/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *
 * This file is automatically generated by buildiface 
 * DO NOT EDIT
 */
#include "mpi_fortimpl.h"

#ifdef MPI_WIN_NULL_COPY_FN
#undef MPI_WIN_NULL_COPY_FN
#endif

/* Begin MPI profiling block */
#if defined(USE_WEAK_SYMBOLS) && !defined(USE_ONLY_MPI_NAMES) 
#if defined(HAVE_MULTIPLE_PRAGMA_WEAK) && defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_NULL_COPY_FN( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn__( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn_( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_win_null_copy_fn_( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_WIN_NULL_COPY_FN = pmpi_win_null_copy_fn__
#pragma weak mpi_win_null_copy_fn__ = pmpi_win_null_copy_fn__
#pragma weak mpi_win_null_copy_fn_ = pmpi_win_null_copy_fn__
#pragma weak mpi_win_null_copy_fn = pmpi_win_null_copy_fn__
#pragma weak pmpi_win_null_copy_fn_ = pmpi_win_null_copy_fn__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_NULL_COPY_FN( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_WIN_NULL_COPY_FN = PMPI_WIN_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn__( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_win_null_copy_fn__ = pmpi_win_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_win_null_copy_fn = pmpi_win_null_copy_fn
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn_( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_win_null_copy_fn_ = pmpi_win_null_copy_fn_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WIN_NULL_COPY_FN  MPI_WIN_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_win_null_copy_fn__  mpi_win_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_win_null_copy_fn  mpi_win_null_copy_fn
#else
#pragma _HP_SECONDARY_DEF pmpi_win_null_copy_fn_  mpi_win_null_copy_fn_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WIN_NULL_COPY_FN as PMPI_WIN_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_win_null_copy_fn__ as pmpi_win_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_win_null_copy_fn as pmpi_win_null_copy_fn
#else
#pragma _CRI duplicate mpi_win_null_copy_fn_ as pmpi_win_null_copy_fn_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_NULL_COPY_FN( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn__( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn_( MPI_Fint*, MPI_Fint*, void*, void*, void*, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_WIN_NULL_COPY_FN = mpi_win_null_copy_fn__
#pragma weak mpi_win_null_copy_fn_ = mpi_win_null_copy_fn__
#pragma weak mpi_win_null_copy_fn = mpi_win_null_copy_fn__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_win_null_copy_fn_ PMPI_WIN_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_win_null_copy_fn_ pmpi_win_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_win_null_copy_fn_ pmpi_win_null_copy_fn
#else
#define mpi_win_null_copy_fn_ pmpi_win_null_copy_fn_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_mpi_win_null_copy_fn PMPI_mpi_win_null_copy_fn 

#else

#ifdef F77_NAME_UPPER
#define mpi_win_null_copy_fn_ MPI_WIN_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_win_null_copy_fn_ mpi_win_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_win_null_copy_fn_ mpi_win_null_copy_fn
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_win_null_copy_fn_ ( MPI_Fint*v1, MPI_Fint*v2, void*v3, void*v4, void*v5, MPI_Fint *v6, MPI_Fint *ierr ){
        *ierr = MPI_SUCCESS;
        *v6 = MPIR_TO_FLOG(0);
}
