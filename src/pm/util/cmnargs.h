/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2004 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */


#ifndef CMNARGS_H_INCLUDED
#define CMNARGS_H_INCLUDED

extern void mpiexec_usage( const char * );

int MPIE_Args( int, char *[], ProcessUniverse *, 
	       int (*)( int, char *[], void *), void * );
int MPIE_CheckEnv( ProcessUniverse *, 
		   int (*)( ProcessUniverse *, void * ), void * );
const char *MPIE_ArgDescription( void );
void MPIE_PrintProcessUniverse( FILE *, ProcessUniverse * );
void MPIE_PrintProcessWorld( FILE *, ProcessWorld * );
#endif
