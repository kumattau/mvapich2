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
extern FORT_DLL_SPEC void FORT_CALL MPI_ALLGATHER( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather__( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather_( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_allgather_( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ALLGATHER = pmpi_allgather__
#pragma weak mpi_allgather__ = pmpi_allgather__
#pragma weak mpi_allgather_ = pmpi_allgather__
#pragma weak mpi_allgather = pmpi_allgather__
#pragma weak pmpi_allgather_ = pmpi_allgather__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_ALLGATHER( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ALLGATHER = PMPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather__( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_allgather__ = pmpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_allgather = pmpi_allgather
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather_( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak mpi_allgather_ = pmpi_allgather_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_ALLGATHER  MPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgather__  mpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_allgather  mpi_allgather
#else
#pragma _HP_SECONDARY_DEF pmpi_allgather_  mpi_allgather_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_ALLGATHER as PMPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_allgather__ as pmpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_allgather as pmpi_allgather
#else
#pragma _CRI duplicate mpi_allgather_ as pmpi_allgather_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_ALLGATHER( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather__( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_allgather_( void*, MPI_Fint *, MPI_Fint *, void*, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * );

#pragma weak MPI_ALLGATHER = mpi_allgather__
#pragma weak mpi_allgather_ = mpi_allgather__
#pragma weak mpi_allgather = mpi_allgather__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_allgather_ PMPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgather_ pmpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgather_ pmpi_allgather
#else
#define mpi_allgather_ pmpi_allgather_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renaming the Fortran entry as the pmpi version.  The MPI name
   must be undefined first to prevent any conflicts with previous renamings,
   such as those put in place by the globus device when it is building on
   top of a vendor MPI. */
#undef MPI_Allgather
#define MPI_Allgather PMPI_Allgather 

#else

#ifdef F77_NAME_UPPER
#define mpi_allgather_ MPI_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_allgather_ mpi_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_allgather_ mpi_allgather
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_allgather_ ( void*v1, MPI_Fint *v2, MPI_Fint *v3, void*v4, MPI_Fint *v5, MPI_Fint *v6, MPI_Fint *v7, MPI_Fint *ierr ){

    if (MPIR_F_NeedInit){ mpirinitf_(); MPIR_F_NeedInit = 0; }
    if (v1 == MPIR_F_MPI_IN_PLACE) v1 = MPI_IN_PLACE;
    *ierr = MPI_Allgather( v1, *v2, (MPI_Datatype)(*v3), v4, *v5, (MPI_Datatype)(*v6), (MPI_Comm)(*v7) );
}
