/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: param.c,v 1.1.1.1 2006/01/18 21:09:48 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"
#include "param.h"
#ifdef HAVE_REGISTERED_PARAMS
#include "defparams.h"
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifndef isascii
#define isascii(c) (((c)&~0x7f)==0)
#endif

/*
  This file implements the parameter routines.  These routines provide a 
  uniform mechanism to access parameters that are used within the mpich2 code.

  This implements a relatively simple system that stores key/value pairs.
  Values are normally set at initialization and then not changed, so
  a simple sorted array of entries can be used.
*/

typedef struct {
    char *name;
    enum { MPIU_STRING, MPIU_INT } kind;
    union {
	char *string_value;
	int  int_value;
    } val;
} Param_entry;

#define MAX_PARAM_TABLE 128
static int nentries = 0;
static Param_entry *param_table = 0;

#define MAX_LINE_SIZE 1024

/*@
  MPIU_Param_init - Initialize the parameter code

  Input/Output Parameters:
+ argc_p - Pointer to argument count
- argv   - Argument vector

  Comments:
  This routine extracts parameter values from the command line, as
  well as initializing any values from the environment and from 
  a defaults file.  The default file is read by only one process; the
  routine 'MPIU_Param_bcast' propagates the values to other processes.
  
  See Also:
  MPIU_Param_bcast, MPIU_Param_get_int, MPIU_Param_get_string,
  MPIU_Param_finalize
  @*/
int MPIU_Param_init( int *argc_p, char *argv_p[], const char def_file[] )
{
    /* Allocate a parameter table */
    param_table = (Param_entry *)MPIU_Malloc( 
				     MAX_PARAM_TABLE * sizeof(Param_entry) );
    if (!param_table) {
	/* Error - cannot allocate table */
	return 0;
    }
	
    if (def_file && def_file[0]) {
	/* Read the file */
	FILE *fp;
	char buf[MAX_LINE_SIZE];

	fp = fopen( def_file, "r" );
	if (fp) {
	    while (fgets( buf, MAX_LINE_SIZE, fp )) {
		char *p;
		char *key, *keyend, *value, *nextval;
		long val;

		/* Make sure that there is a null at the end */
		buf[MAX_LINE_SIZE-1] = 0;
		p = buf;
		/* Find the first non-blank */
		while (*p && isascii(*p) && isspace(*p)) p++;
		/* Check for comments */
		if (*p == '#') {
		    continue; 
		}
	    
		/* Check for key = value */
		key = p;
		while (*p && *p != '=') p++;
	    
		if (*p != '=') {
		    /* Error - key without = value */
		
		    continue;
		}
		/* Null terminate the key */
		keyend = p - 1;
		while (*keyend && isascii(*keyend) && isspace(*keyend)) 
		    keyend--;
		*++keyend = 0;

		/* skip over the = */
		p++;
		/* Find the value */
		while (*p && isascii(*p) && isspace(*p)) p++;

		value = p;
	    
		if (!*value) {
		    /* Error - key without value */

		    continue;
		}

		if (nentries == MAX_PARAM_TABLE) {
		    /* Error - out of room in the table */
		    break;
		}

		/* At this point we can save a value */
		val = strtol( value, &nextval, 0 );
		param_table[nentries].name = MPIU_Strdup( key );
		if (nextval != value) {
		    param_table[nentries].kind = MPIU_INT;
		    param_table[nentries].val.int_value = (int)val;
		}
		else {
		    param_table[nentries].kind = MPIU_STRING;
		    param_table[nentries].val.string_value = MPIU_Strdup( value );
		}
		nentries++;
	    }
	    fclose( fp );
	}
    }

#ifdef HAVE_REGISTERED_PARAMS    
    /* Now, process any command line choices.  This must look for registered
       names. */
    if (argc_p) {
	int argc;
	for (argc=0; argc<*argc_p; argc++) {
	    ;
	}
    }

    /* Acquire any environment variables that have been registered.  (
       registration is done by a separate source tool) */
#endif
    
    return 0;
}

int MPIU_Param_bcast( void )
{
    return 0;
}

int MPIU_Param_register( const char name[], const char envname[], 
                         const char description[] )
{
    return 0;
}

/* Search through the ordered table that is param_table.  
   Linear search for now; binary tree search is almost as easy */
static Param_entry *find_entry( const char name[] )
{
    int i, cmp;

    for (i=0; i<nentries; i++) {
	cmp = strcmp( param_table[i].name, name );
	if (cmp == 0) {
	    return &param_table[i];
	}
	else if (cmp < 0) {
	    return 0;
	}
    }
    return 0;
}

int MPIU_Param_get_int( const char name[], int default_val, int *value )
{
    Param_entry *entry; 
    int rc = 0;

    entry = find_entry( name );
    if (entry) {
	if (entry->kind == MPIU_INT) {
	    *value = entry->val.int_value;
	    rc = 0;
	}
	else {
	    rc = 2;
	}
    }
    else  {
	*value = default_val;
	rc = 1;
    }
    return rc;
}

int MPIU_Param_get_string( const char name[], const char *default_val,
                           char **value )
{
    Param_entry *entry; 

    entry = find_entry( name );
    if (entry) {
	if (entry->kind == MPIU_STRING) {
	    *value = entry->val.string_value;
	    return 0;
	}
	else {
	    return 2;
	}
    }
    else {
	*value = (char *)default_val;
	return 1;
    }
}

void MPIU_Param_finalize( void )
{
    return;
}

/* 
 * FIXME:
 * These are simple standins for the scalable parameter functions that we
 * need.
 */

/* This is taken from src/pm/util/pmiport.c */
int MPIU_GetEnvRange( const char *envName, int *lowPtr, int *highPtr )
{
    const char *range_ptr;
    int low=0, high=0;

    /* Get the low and high range.  */
    range_ptr = getenv( envName );
    if (range_ptr) {
	const char *p;
	/* Look for n:m format */
	p = range_ptr;
	while (*p && isspace(*p)) p++;
	while (*p && isdigit(*p)) low = 10 * low + (*p++ - '0');
	if (*p == ':') {
	    p++;
	    while (*p && isdigit(*p)) high = 10 * high + (*p++ - '0');
	}
	if (*p) {
	    MPIU_Error_printf( "Invalid character %c in %s\n", 
			       *p, envName );
	    return -1;
	}
	*lowPtr  = low;
	*highPtr = high;
    }
    return 0;
}
