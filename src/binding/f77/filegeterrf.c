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
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_GET_ERRHANDLER( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler_( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_file_get_errhandler_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_GET_ERRHANDLER = pmpi_file_get_errhandler__
#pragma weak mpi_file_get_errhandler__ = pmpi_file_get_errhandler__
#pragma weak mpi_file_get_errhandler_ = pmpi_file_get_errhandler__
#pragma weak mpi_file_get_errhandler = pmpi_file_get_errhandler__
#pragma weak pmpi_file_get_errhandler_ = pmpi_file_get_errhandler__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_GET_ERRHANDLER( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_GET_ERRHANDLER = PMPI_FILE_GET_ERRHANDLER
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler__( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_get_errhandler__ = pmpi_file_get_errhandler__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_get_errhandler = pmpi_file_get_errhandler
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_get_errhandler_ = pmpi_file_get_errhandler_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_FILE_GET_ERRHANDLER  MPI_FILE_GET_ERRHANDLER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_errhandler__  mpi_file_get_errhandler__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_get_errhandler  mpi_file_get_errhandler
#else
#pragma _HP_SECONDARY_DEF pmpi_file_get_errhandler_  mpi_file_get_errhandler_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_FILE_GET_ERRHANDLER as PMPI_FILE_GET_ERRHANDLER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_file_get_errhandler__ as pmpi_file_get_errhandler__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_file_get_errhandler as pmpi_file_get_errhandler
#else
#pragma _CRI duplicate mpi_file_get_errhandler_ as pmpi_file_get_errhandler_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_GET_ERRHANDLER( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler__( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler( MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler_( MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_GET_ERRHANDLER = mpi_file_get_errhandler__
#pragma weak mpi_file_get_errhandler_ = mpi_file_get_errhandler__
#pragma weak mpi_file_get_errhandler = mpi_file_get_errhandler__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_file_get_errhandler_ PMPI_FILE_GET_ERRHANDLER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_file_get_errhandler_ pmpi_file_get_errhandler__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_file_get_errhandler_ pmpi_file_get_errhandler
#else
#define mpi_file_get_errhandler_ pmpi_file_get_errhandler_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_File_get_errhandler PMPI_File_get_errhandler 

#else

#ifdef F77_NAME_UPPER
#define mpi_file_get_errhandler_ MPI_FILE_GET_ERRHANDLER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_file_get_errhandler_ mpi_file_get_errhandler__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_file_get_errhandler_ mpi_file_get_errhandler
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_file_get_errhandler_ ( MPI_Fint *v1, MPI_Fint *v2, MPI_Fint *ierr ){
    *ierr = MPI_File_get_errhandler( MPI_File_f2c(*v1), v2 );
}
