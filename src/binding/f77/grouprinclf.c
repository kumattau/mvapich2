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
extern FORT_DLL_SPEC void FORT_CALL MPI_GROUP_RANGE_INCL( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl__( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl_( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL pmpi_group_range_incl_( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak MPI_GROUP_RANGE_INCL = pmpi_group_range_incl__
#pragma weak mpi_group_range_incl__ = pmpi_group_range_incl__
#pragma weak mpi_group_range_incl_ = pmpi_group_range_incl__
#pragma weak mpi_group_range_incl = pmpi_group_range_incl__
#pragma weak pmpi_group_range_incl_ = pmpi_group_range_incl__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_GROUP_RANGE_INCL( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak MPI_GROUP_RANGE_INCL = PMPI_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl__( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak mpi_group_range_incl__ = pmpi_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak mpi_group_range_incl = pmpi_group_range_incl
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl_( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak mpi_group_range_incl_ = pmpi_group_range_incl_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_GROUP_RANGE_INCL  MPI_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_range_incl__  mpi_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_group_range_incl  mpi_group_range_incl
#else
#pragma _HP_SECONDARY_DEF pmpi_group_range_incl_  mpi_group_range_incl_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_GROUP_RANGE_INCL as PMPI_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_group_range_incl__ as pmpi_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_group_range_incl as pmpi_group_range_incl
#else
#pragma _CRI duplicate mpi_group_range_incl_ as pmpi_group_range_incl_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_GROUP_RANGE_INCL( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl__( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );
extern FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl_( MPI_Fint *, MPI_Fint *, MPI_Fint [], MPI_Fint *, MPI_Fint * );

#pragma weak MPI_GROUP_RANGE_INCL = mpi_group_range_incl__
#pragma weak mpi_group_range_incl_ = mpi_group_range_incl__
#pragma weak mpi_group_range_incl = mpi_group_range_incl__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_group_range_incl_ PMPI_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_range_incl_ pmpi_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_range_incl_ pmpi_group_range_incl
#else
#define mpi_group_range_incl_ pmpi_group_range_incl_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_Group_range_incl PMPI_Group_range_incl 

#else

#ifdef F77_NAME_UPPER
#define mpi_group_range_incl_ MPI_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_group_range_incl_ mpi_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_group_range_incl_ mpi_group_range_incl
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_group_range_incl_ ( MPI_Fint *v1, MPI_Fint *v2, MPI_Fint v3[], MPI_Fint *v4, MPI_Fint *ierr ){
    *ierr = MPI_Group_range_incl( *v1, *v2, (int (*)[3])(v3), v4 );
}
