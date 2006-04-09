/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: simple_pmiutil.c,v 1.1.1.1 2006/01/18 21:09:48 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Allow fprintf to logfile */
/* style: allow:fprintf:1 sig:0 */

/* Utility functions associated with PMI implementation, but not part of
   the PMI interface itself.  Reading and writing on pipes, signals, and parsing
   key=value messages
*/
#include "pmiconf.h"

#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include "simple_pmiutil.h"

/* Use the memory definitions from mpich2/src/include */
#include "mpimem.h"

#define MAXVALLEN 1024
#define MAXKEYLEN   32

/* These are not the keyvals in the keyval space that is part of the PMI specification.
   They are just part of this implementation's internal utilities.
*/
struct PMIU_keyval_pairs {
    char key[MAXKEYLEN];
    char value[MAXVALLEN];	
};
static struct PMIU_keyval_pairs PMIU_keyval_tab[64] = { { {0} } };
static int  PMIU_keyval_tab_idx = 0;

/* This is used to prepend printed output.  Set the initial value to 
   "unset" */
static char PMIU_print_id[PMIU_IDSIZE] = "unset";

void PMIU_Set_rank( int PMI_rank )
{
    MPIU_Snprintf( PMIU_print_id, PMIU_IDSIZE, "cli_%d", PMI_rank );
}

/* style: allow:fprintf:1 sig:0 */
/* style: allow:vfprintf:1 sig:0 */
/* This should be combined with the message routines */
void PMIU_printf( int print_flag, char *fmt, ... )
{
    va_list ap;
    static FILE *logfile= 0;
    
    /* In some cases when we are debugging, the handling of stdout or
       stderr may be unreliable.  In that case, we make it possible to
       select an output file. */
    if (!logfile) {
	char *p;
	p = getenv("PMI_USE_LOGFILE");
	if (p) {
	    char filename[1024];
	    p = getenv("PMI_ID");
	    if (p) {
		MPIU_Snprintf( filename, sizeof(filename), 
			       "testclient-%s.out", p );
		logfile = fopen( filename, "w" );
	    }
	    else {
		logfile = fopen( "testserver.out", "w" );
	    }
	}
	else 
	    logfile = stderr;
    }

    if ( print_flag ) {
	/* MPIU_Error_printf( "[%s]: ", PMIU_print_id ); */
	/* FIXME: Decide what role PMIU_printf should have (if any) and
	   select the appropriate MPIU routine */
	fprintf( logfile, "[%s]: ", PMIU_print_id );
	va_start( ap, fmt );
	vfprintf( logfile, fmt, ap );
	va_end( ap );
	fflush( logfile );
    }
}

/* This function reads until it finds a newline character.  It returns the number of
   characters read, including the newline character.  The newline character is stored
   in buf, as in fgets.  It does not supply a string-terminating null character.
*/
int PMIU_readline( int fd, char *buf, int maxlen )
{
    int n, rc;
    char c, *ptr;

    ptr = buf;
    for ( n = 1; n < maxlen; n++ ) {
      again:
	rc = read( fd, &c, 1 );
	if ( rc == 1 ) {
	    *ptr++ = c;
	    if ( c == '\n' )	/* note \n is stored, like in fgets */
		break;
	}
	else if ( rc == 0 ) {
	    if ( n == 1 )
		return( 0 );	/* EOF, no data read */
	    else
		break;		/* EOF, some data read */
	}
	else {
	    if ( errno == EINTR )
		goto again;
	    return ( -1 );	/* error, errno set by read */
	}
    }
    *ptr = 0;			/* null terminate, like fgets */
    PMIU_printf( 0, " received :%s:\n", buf );
    return( n );
}

int PMIU_writeline( int fd, char *buf )	
{
    int size, n;

    size = strlen( buf );
    if ( size > PMIU_MAXLINE ) {
	buf[PMIU_MAXLINE-1] = '\0';
	PMIU_printf( 1, "write_line: message string too big: :%s:\n", buf );
    }
    else if ( buf[strlen( buf ) - 1] != '\n' )  /* error:  no newline at end */
	    PMIU_printf( 1, "write_line: message string doesn't end in newline: :%s:\n",
		       buf );
    else {
	n = write( fd, buf, size );
	if ( n < 0 ) {
	    PMIU_printf( 1, "write_line error; fd=%d buf=:%s:\n", fd, buf );
	    perror("system msg for write_line failure ");
	    return(-1);
	}
	if ( n < size)
	    PMIU_printf( 1, "write_line failed to write entire message\n" );
    }
    return 0;
}

/*
 * Given an input string st, parse it into internal storage that can be
 * queried by routines such as PMIU_getval.
 */
int PMIU_parse_keyvals( char *st )
{
    char *p, *keystart, *valstart;

    if ( !st )
	return( -1 );

    PMIU_keyval_tab_idx = 0;
    p = st;
    while ( 1 ) {
	while ( *p == ' ' )
	    p++;
	/* got non-blank */
	if ( *p == '=' ) {
	    PMIU_printf( 1, "PMIU_parse_keyvals:  unexpected = at character %d in %s\n",
		       p - st, st );
	    return( -1 );
	}
	if ( *p == '\n' || *p == '\0' )
	    return( 0 );	/* normal exit */
	/* got normal character */
	keystart = p;		/* remember where key started */
	while ( *p != ' ' && *p != '=' && *p != '\n' && *p != '\0' )
	    p++;
	if ( *p == ' ' || *p == '\n' || *p == '\0' ) {
	    PMIU_printf( 1,
	       "PMIU_parse_keyvals: unexpected key delimiter at character %d in %s\n",
		       p - st, st );
	    return( -1 );
	}
        MPIU_Strncpy( PMIU_keyval_tab[PMIU_keyval_tab_idx].key, keystart, MAXKEYLEN );
	PMIU_keyval_tab[PMIU_keyval_tab_idx].key[p - keystart] = '\0'; /* store key */

	valstart = ++p;			/* start of value */
	while ( *p != ' ' && *p != '\n' && *p != '\0' )
	    p++;
        MPIU_Strncpy( PMIU_keyval_tab[PMIU_keyval_tab_idx].value, valstart, MAXVALLEN );
	PMIU_keyval_tab[PMIU_keyval_tab_idx].value[p - valstart] = '\0'; /* store value */
	PMIU_keyval_tab_idx++;
	if ( *p == ' ' )
	    continue;
	if ( *p == '\n' || *p == '\0' )
	    return( 0 );	/* value has been set to empty */
    }
}

void PMIU_dump_keyvals( void )
{
    int i;
    for (i=0; i < PMIU_keyval_tab_idx; i++) 
	PMIU_printf(1, "  %s=%s\n",PMIU_keyval_tab[i].key, PMIU_keyval_tab[i].value);
}

char *PMIU_getval( const char *keystr, char *valstr, int vallen )
{
    int i;
    
    for (i = 0; i < PMIU_keyval_tab_idx; i++) {
	if ( strcmp( keystr, PMIU_keyval_tab[i].key ) == 0 ) { 
	    MPIU_Strncpy( valstr, PMIU_keyval_tab[i].value, vallen - 1 );
	    valstr[vallen - 1] = '\0';
	    return valstr;
       } 
    }
    valstr[0] = '\0';
    return NULL;
}

void PMIU_chgval( const char *keystr, char *valstr )
{
    int i;
    
    for ( i = 0; i < PMIU_keyval_tab_idx; i++ ) {
	if ( strcmp( keystr, PMIU_keyval_tab[i].key ) == 0 ) {
	    MPIU_Strncpy( PMIU_keyval_tab[i].value, valstr, MAXVALLEN - 1 );
	    PMIU_keyval_tab[i].value[MAXVALLEN - 1] = '\0';
	}
    }
}
