/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef _TRMEM_H_INCLUDED
#define _TRMEM_H_INCLUDED

/* FIXME: Consider an option of specifying __attribute__((malloc)) for 
   gcc - this lets gcc-style compilers know that the returned pointer 
   does not alias any pointer prior to the call.
 */
void MPIU_trinit( int );
void *MPIU_trmalloc( size_t, int, const char [] );
void MPIU_trfree( void *, int, const char [] );
int MPIU_trvalid( const char [] );
void MPIU_trspace( int *, int * );
void MPIU_trid( int );
void MPIU_trlevel( int );
void MPIU_trDebugLevel( int );
void *MPIU_trcalloc( size_t, size_t, int, const char [] );
void *MPIU_trrealloc( void *, size_t, int, const char[] );
void *MPIU_trstrdup( const char *, int, const char[] );
void MPIU_TrSetMaxMem( int );

/* Make sure that FILE is defined */
#include <stdio.h>
void MPIU_trdump( FILE *, int );
void MPIU_trSummary( FILE *, int );
void MPIU_trdumpGrouped( FILE *, int );

#endif
