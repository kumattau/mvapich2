/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: mpi_fortimpl.h,v 1.1.1.1 2006/01/18 21:09:42 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpichconf.h"

/* Handle different mechanisms for passing Fortran CHARACTER to routines */
#ifdef USE_FORT_MIXED_STR_LEN
#define FORT_MIXED_LEN_DECL   , MPI_Fint
#define FORT_END_LEN_DECL
#define FORT_MIXED_LEN(a)     , MPI_Fint a
#define FORT_END_LEN(a)
#else
#define FORT_MIXED_LEN_DECL
#define FORT_END_LEN_DECL     , MPI_Fint
#define FORT_MIXED_LEN(a)
#define FORT_END_LEN(a)       , MPI_Fint a
#endif

/* ------------------------------------------------------------------------- */
/* The following definitions are used to support the Microsoft compilers and

   The following C preprocessor macros are not discoved by configure.  
   Instead, they must be defined separately; this is normally done as part of 
   the Windows-specific configuration process.

   USE_FORT_STDCALL - Use __stdcall for the calling convention
   USE_FORT_CDECL   - Use __cdelc for the calling convention
       These define the macro FORT_CALL ; for other systems, FORT_CALL is 
       empty

       Note: It may be that these should be USE_MSC_xxx to indicate that
       these can only be used with the MS C compiler.

   USE_MSC_DLLSPEC - Use __declspec to control the import or export of
                     symbols in a generated dynamic link library (DLL)
       This defines the macros FORT_DECL_SPEC and FORT_C_DECL_SPEC ; for 
       other systems, these names both expand to empty
       If USE_MSC_DLLSPEC is defined, then the macros FORTRAN_EXPORTS and
       FORTRAN_FROM_C_EXPORTS controls whether dllexport or dllimport is 
       specified.       

       The name (USE_MSC_xxx) here indicates that the MS C compiler is 
       required for this.

 */

/* Support Windows extension to specify calling convention */
#ifdef USE_FORT_CDECL
#define FORT_CALL __cdecl
#elif defined (USE_FORT_STDCALL)
#define FORT_CALL __stdcall
#else
#define FORT_CALL
#endif

/* Support Windows extension to specify which functions are exported from
   shared (DLL) libraries */
/* Backward compatibility (was HAVE_FORTRAN_API) */
#if defined(HAVE_FORTRAN_API) && !defined(USE_MSC_DLLSPEC)
#define USE_MSC_DLLSPEC
#endif

#ifdef USE_MSC_DLLSPEC
# ifdef FORTRAN_EXPORTS
#  define FORT_DLL_SPEC __declspec(dllexport)
# else
#  define FORT_DLL_SPEC __declspec(dllimport)
# endif
#else
# define FORT_DLL_SPEC
#endif


/* ------------------------------------------------------------------------- */

/* Support an alternate approach for declaring a weak symbol supported by
   some versions of gcc */
#ifdef USE_WEAK_ATTRIBUTE
#define FUNC_ATTRIBUTES(name) __attribute__ ((weak,alias(#name)))
#else
#define FUNC_ATTRIBUTES(name)
#endif

/* ------------------------------------------------------------------------- */

/* mpi.h includes the definitions of MPI_Fint */
#include "mpi.h"

/* Include prototypes of helper functions.
   These include MPIR_Keyval_set_fortran, fortran90, and 
   Grequest_set_lang_f77 */
#include "mpi_f77interface.h"

/* If there is no MPI I/O support, and we are still using MPIO_Request,
   make sure that one is defined */
#ifndef MPIO_REQUEST_DEFINED
#define MPIO_Request MPI_Request
#endif

/* MPI_FAint is used as the C type corresponding to the Fortran type 
   used for addresses.  For now, we make this the same as MPI_Aint.  
   Note that since this is defined only for this private include file,
   we can get away with calling MPI_xxx */
typedef MPI_Aint MPI_FAint;

/* Utility functions */

/* Define the internal values needed for Fortran support */

/* Fortran logicals */

/* Fortran logical values */
#ifndef _CRAY
#ifdef F77_USE_BOOLEAN_LITERALS
#define MPIR_F_TRUE  F77_TRUE_VALUE
#define MPIR_F_FALSE F77_FALSE_VALUE
#else
#if !defined(F77_RUNTIME_VALUES) && defined(F77_TRUE_VALUE_SET)
extern const MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE;
#else
extern MPI_Fint MPIR_F_TRUE, MPIR_F_FALSE;
#endif
#endif
#define MPIR_TO_FLOG(a) ((a) ? MPIR_F_TRUE : MPIR_F_FALSE)
/* 
   Note on true and false.  This code is only an approximation.
   Some systems define either true or false, and allow some or ALL other
   patterns for the other.  This is just like C, where 0 is false and 
   anything not zero is true.  Modify this test as necessary for your
   system.

   We check against FALSE instead of TRUE because many (perhaps all at this
   point) Fortran compilers use 0 for false and some non-zero value for
   true.  By using this test, it is possible to use the same Fortran
   interface library for multiple compilers that differ only in the 
   value used for Fortran .TRUE. .
 */
#define MPIR_FROM_FLOG(a) ( (a) == MPIR_F_FALSE ? 0 : 1 )

#else
/* CRAY Vector processors only; these are defined in /usr/include/fortran.h 
   Thanks to lmc@cray.com */
#define MPIR_TO_FLOG(a) (_btol(a))
#define MPIR_FROM_FLOG(a) ( _ltob(&(a)) )    /* (a) must be a pointer */
#endif

/* If Cray-style pointers are supported, we don't need to check for a 
   "special" address. */
#ifdef USE_POINTER_FOR_BOTTOM
#define MPIR_F_PTR(a) (a)
#else
/* MPIR_F_MPI_BOTTOM is the address of the Fortran MPI_BOTTOM value */
extern FORT_DLL_SPEC int  MPIR_F_NeedInit;
extern FORT_DLL_SPEC void *MPIR_F_MPI_BOTTOM;
extern FORT_DLL_SPEC void *MPIR_F_MPI_IN_PLACE;
/* MPI_F_STATUS(ES)_IGNORE are defined in mpi.h and are intended for C 
   programs. */
/*
extern FORT_DLL_SPEC MPI_Fint *MPI_F_STATUS_IGNORE;
extern FORT_DLL_SPEC MPI_Fint *MPI_F_STATUSES_IGNORE;
*/
extern FORT_DLL_SPEC int  *MPI_F_ERRCODES_IGNORE;
extern FORT_DLL_SPEC void *MPI_F_ARGVS_NULL;
/* MPIR_F_PTR checks for the Fortran MPI_BOTTOM and provides the value 
   MPI_BOTTOM if found 
   See src/pt2pt/addressf.c for why MPIR_F_PTR(a) is just (a)
*/
/*  #define MPIR_F_PTR(a) (((a)==(MPIR_F_MPI_BOTTOM))?MPI_BOTTOM:a) */
#define MPIR_F_PTR(a) (a)

/* Define the name of the function that we call to initialize the
   special symbols */
#if defined(F77_NAME_LOWER_USCORE) || defined(F77_NAME_LOWER_2USCORE)
/* do nothing */
#elif defined(F77_NAME_UPPER)
#define mpirinitf_ MPIRINITF
#else
#define mpirinitf_ mpirinitf
#endif
/* Provide a prototype for the mpirinitf function */
extern void mpirinitf_( void );

#endif

/*  
 * These are hooks for Fortran characters.
 * MPID_FCHAR_T is the type of a Fortran character argument
 * MPID_FCHAR_LARG is the "other" argument that some Fortran compilers use
 * MPID_FCHAR_STR gives the pointer to the characters
 */
#ifdef MPID_CHARACTERS_ARE_CRAYPVP
typedef <whatever> MPID_FCHAR_T;
#define MPID_FCHAR_STR(a) (a)->characters   <or whatever>
#define MPID_FCHAR_LARG(d) 
#else
typedef char *MPID_FCHAR_T;
#define MPID_FCHAR_STR(a) a
#define MPID_FCHAR_LARG(d) ,d
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Temporary patch for the space routines.  Eventually, this should use
   (FIXME) *just* the memory definitions currently in mpiimpl.h */
/* style: allow:malloc:1 sig:0 */
/* style: allow:free:1 sig:0 */
/* style: allow:calloc:1 sig:0 */
#ifndef MPIU_Malloc
#define MPIU_Malloc(a)    malloc((unsigned)(a))
#define MPIU_Calloc(a,b)  calloc((unsigned)(a),(unsigned)(b))
#define MPIU_Free(a)      free((void *)(a))
#endif

#ifndef MPIR_USE_LOCAL_ARRAY
#define MPIR_USE_LOCAL_ARRAY 32
#endif

/* Undefine the names that are used in mpi.h for the predefined keyval
   copy and delete functions.  This is necessary in case the Fortran 
   compiler uses uppercase names, because in that case there would be 
   a conflict in the names */
#ifdef MPI_DUP_FN
#undef MPI_DUP_FN
#undef MPI_NULL_COPY_FN
#undef MPI_NULL_DELETE_FN
#undef MPI_COMM_NULL_COPY_FN
#undef MPI_COMM_NULL_DELETE_FN
#undef MPI_COMM_DUP_FN
#undef MPI_TYPE_NULL_COPY_FN
#undef MPI_TYPE_NULL_DELETE_FN
#undef MPI_TYPE_DUP_FN
#undef MPI_WIN_NULL_COPY_FN
#undef MPI_WIN_NULL_DELETE_FN
#undef MPI_WIN_DUP_FN

/* Ditto the null datarep conversion */
#undef MPI_CONVERSION_FN_NULL
#endif /* MPI_DUP_FN */

/* A special case to help out when ROMIO is disabled */
#ifndef MPI_MODE_RDONLY
#ifndef MPI_File_f2c
#define MPI_File_f2c(a) ((MPI_File*)(a))
#endif
#endif /* MPI_MODE_RDONLY */
