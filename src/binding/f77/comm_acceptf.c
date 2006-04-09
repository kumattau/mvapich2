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
extern FORT_DLL_SPEC void FORT_CALL MPI_COMM_ACCEPT( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept__( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept_( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL pmpi_comm_accept_( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak MPI_COMM_ACCEPT = pmpi_comm_accept__
#pragma weak mpi_comm_accept__ = pmpi_comm_accept__
#pragma weak mpi_comm_accept_ = pmpi_comm_accept__
#pragma weak mpi_comm_accept = pmpi_comm_accept__
#pragma weak pmpi_comm_accept_ = pmpi_comm_accept__


#elif defined(HAVE_PRAGMA_WEAK)

#if defined(F77_NAME_UPPER)
extern FORT_DLL_SPEC void FORT_CALL MPI_COMM_ACCEPT( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak MPI_COMM_ACCEPT = PMPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept__( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak mpi_comm_accept__ = pmpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak mpi_comm_accept = pmpi_comm_accept
#else
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept_( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak mpi_comm_accept_ = pmpi_comm_accept_
#endif

#elif defined(HAVE_PRAGMA_HP_SEC_DEF)
#if defined(F77_NAME_UPPER)
#pragma _HP_SECONDARY_DEF PMPI_COMM_ACCEPT  MPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_accept__  mpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _HP_SECONDARY_DEF pmpi_comm_accept  mpi_comm_accept
#else
#pragma _HP_SECONDARY_DEF pmpi_comm_accept_  mpi_comm_accept_
#endif

#elif defined(HAVE_PRAGMA_CRI_DUP)
#if defined(F77_NAME_UPPER)
#pragma _CRI duplicate MPI_COMM_ACCEPT as PMPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#pragma _CRI duplicate mpi_comm_accept__ as pmpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#pragma _CRI duplicate mpi_comm_accept as pmpi_comm_accept
#else
#pragma _CRI duplicate mpi_comm_accept_ as pmpi_comm_accept_
#endif
#endif /* HAVE_PRAGMA_WEAK */
#endif /* USE_WEAK_SYMBOLS */
/* End MPI profiling block */


/* These definitions are used only for generating the Fortran wrappers */
#if defined(USE_WEAK_SYBMOLS) && defined(HAVE_MULTIPLE_PRAGMA_WEAK) && \
    defined(USE_ONLY_MPI_NAMES)
extern FORT_DLL_SPEC void FORT_CALL MPI_COMM_ACCEPT( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept__( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );
extern FORT_DLL_SPEC void FORT_CALL mpi_comm_accept_( char * FORT_MIXED_LEN_DECL, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint *, MPI_Fint * FORT_END_LEN_DECL );

#pragma weak MPI_COMM_ACCEPT = mpi_comm_accept__
#pragma weak mpi_comm_accept_ = mpi_comm_accept__
#pragma weak mpi_comm_accept = mpi_comm_accept__
#endif

/* Map the name to the correct form */
#ifndef MPICH_MPI_FROM_PMPI
#ifdef F77_NAME_UPPER
#define mpi_comm_accept_ PMPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_accept_ pmpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_accept_ pmpi_comm_accept
#else
#define mpi_comm_accept_ pmpi_comm_accept_
#endif
/* This defines the routine that we call, which must be the PMPI version
   since we're renameing the Fortran entry as the pmpi version */
#define MPI_Comm_accept PMPI_Comm_accept 

#else

#ifdef F77_NAME_UPPER
#define mpi_comm_accept_ MPI_COMM_ACCEPT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpi_comm_accept_ mpi_comm_accept__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpi_comm_accept_ mpi_comm_accept
/* Else leave name alone */
#endif


#endif /* MPICH_MPI_FROM_PMPI */

/* Prototypes for the Fortran interfaces */
#include "fproto.h"
FORT_DLL_SPEC void FORT_CALL mpi_comm_accept_ ( char *v1 FORT_MIXED_LEN(d1), MPI_Fint *v2, MPI_Fint *v3, MPI_Fint *v4, MPI_Fint *v5, MPI_Fint *ierr FORT_END_LEN(d1) ){
    char *p1;

    {char *p = v1 + d1 - 1;
     int  li;
        while (*p == ' ' && p > v1) p--;
        p++;
        p1 = (char *)MPIU_Malloc( p-v1 + 1 );
        for (li=0; li<(p-v1); li++) { p1[li] = v1[li]; }
        p1[li] = 0; 
    }
    *ierr = MPI_Comm_accept( p1, (MPI_Info)(*v2), *v3, (MPI_Comm)(*v4), (MPI_Comm *)(v5) );
    MPIU_Free( p1 );
}
