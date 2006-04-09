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
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_READ_SHARED( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_file_read_shared_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_READ_SHARED = pmpi_file_read_shared__
#pragma weak mpi_file_read_shared__ = pmpi_file_read_shared__
#pragma weak mpi_file_read_shared_ = pmpi_file_read_shared__
#pragma weak mpi_file_read_shared = pmpi_file_read_shared__
#pragma weak pmpi_file_read_shared_ = pmpi_file_read_shared__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_READ_SHARED( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_READ_SHARED = PMPI_FILE_READ_SHARED
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_read_shared__ = pmpi_file_read_shared__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_read_shared = pmpi_file_read_shared
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_file_read_shared_ = pmpi_file_read_shared_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_FILE_READ_SHARED  MPI_FILE_READ_SHARED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_read_shared__  mpi_file_read_shared__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_file_read_shared  mpi_file_read_shared
#else
#pragma _HP_SECONDARY_DEF pmpi_file_read_shared_  mpi_file_read_shared_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_FILE_READ_SHARED as PMPI_FILE_READ_SHARED
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_file_read_shared__ as pmpi_file_read_shared__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_file_read_shared as pmpi_file_read_shared
#else
#pragma _CRI duplicate mpi_file_read_shared_ as pmpi_file_read_shared_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_FILE_READ_SHARED( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared__( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared_( MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_FILE_READ_SHARED = mpi_file_read_shared__
#pragma weak mpi_file_read_shared_ = mpi_file_read_shared__
#pragma weak mpi_file_read_shared = mpi_file_read_shared__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_file_read_shared_ PMPI_FILE_READ_SHARED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_file_read_shared_ pmpi_file_read_shared__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_file_read_shared_ pmpi_file_read_shared
#else
#define mpi_file_read_shared_ pmpi_file_read_shared_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_File_read_shared PMPI_File_read_shared 

#else

#ifdef F77_NAME_UPPER
#define mpi_file_read_shared_ MPI_FILE_READ_SHARED
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_file_read_shared_ mpi_file_read_shared__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_file_read_shared_ mpi_file_read_shared
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_file_read_shared_ ( MPI_Fint *v1, void*v2, MPI_Fint *v3, MPI_Fint *v4, MPI_Fint *v5, MPI_Fint *ierr ){
#ifdef MPI_MODE_RDONLY
    *ierr = MPI_File_read_shared( MPI_File_f2c(*v1), v2, *v3, (MPI_Datatype)(*v4), (MPI_Status *)(v5) );
#else
*ierr = MPI_ERR_INTERN;
#endif
}
