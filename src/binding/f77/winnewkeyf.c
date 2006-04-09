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
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_CREATE_KEYVAL( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval__( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval_( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_win_create_keyval_( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak MPI_WIN_CREATE_KEYVAL = pmpi_win_create_keyval__
#pragma weak mpi_win_create_keyval__ = pmpi_win_create_keyval__
#pragma weak mpi_win_create_keyval_ = pmpi_win_create_keyval__
#pragma weak mpi_win_create_keyval = pmpi_win_create_keyval__
#pragma weak pmpi_win_create_keyval_ = pmpi_win_create_keyval__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_CREATE_KEYVAL( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak MPI_WIN_CREATE_KEYVAL = PMPI_WIN_CREATE_KEYVAL
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval__( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak mpi_win_create_keyval__ = pmpi_win_create_keyval__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak mpi_win_create_keyval = pmpi_win_create_keyval
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval_( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak mpi_win_create_keyval_ = pmpi_win_create_keyval_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_WIN_CREATE_KEYVAL  MPI_WIN_CREATE_KEYVAL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_win_create_keyval__  mpi_win_create_keyval__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_win_create_keyval  mpi_win_create_keyval
#else
#pragma _HP_SECONDARY_DEF pmpi_win_create_keyval_  mpi_win_create_keyval_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_WIN_CREATE_KEYVAL as PMPI_WIN_CREATE_KEYVAL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_win_create_keyval__ as pmpi_win_create_keyval__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_win_create_keyval as pmpi_win_create_keyval
#else
#pragma _CRI duplicate mpi_win_create_keyval_ as pmpi_win_create_keyval_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_WIN_CREATE_KEYVAL( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval__( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval_( MPI_Win_copy_attr_function*, MPI_Win_delete_attr_function*, MPI_Fint *, void*, MPI_Fint * );

#pragma weak MPI_WIN_CREATE_KEYVAL = mpi_win_create_keyval__
#pragma weak mpi_win_create_keyval_ = mpi_win_create_keyval__
#pragma weak mpi_win_create_keyval = mpi_win_create_keyval__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_win_create_keyval_ PMPI_WIN_CREATE_KEYVAL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_win_create_keyval_ pmpi_win_create_keyval__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_win_create_keyval_ pmpi_win_create_keyval
#else
#define mpi_win_create_keyval_ pmpi_win_create_keyval_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_Win_create_keyval PMPI_Win_create_keyval 

#else

#ifdef F77_NAME_UPPER
#define mpi_win_create_keyval_ MPI_WIN_CREATE_KEYVAL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_win_create_keyval_ mpi_win_create_keyval__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_win_create_keyval_ mpi_win_create_keyval
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_win_create_keyval_ ( MPI_Win_copy_attr_function*v1, MPI_Win_delete_attr_function*v2, MPI_Fint *v3, void*v4, MPI_Fint *ierr ){
    *ierr = MPI_Win_create_keyval( v1, v2, v3, v4 );

    if (*ierr == MPI_SUCCESS) {
         MPIR_Keyval_set_fortran90( *v3 );
    }
}
