/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#include "mpe_logging_conf.h"

#if defined( HAVE_SYS_TYPES_H)
#include <sys/types.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDIO_H )
#include <stdio.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STRING_H )
#include <string.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif

#include "clog.h"
#include "clog_const.h"
#include "clog_util.h"
#include "clog_preamble.h"

#if !defined( CLOG_NOMPI )
#include "mpi.h"
#endif

#ifdef NEEDS_SNPRINTF_DECL
extern int snprintf( char *, size_t, const char *, ... );
#endif

#define ONE_GIGA  1073741824


CLOG_Preamble_t *CLOG_Preamble_create( void )
{
    CLOG_Preamble_t  *preamble;

    preamble = (CLOG_Preamble_t *) MALLOC( sizeof(CLOG_Preamble_t) );
    if ( preamble == NULL ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_create() - "
                         "MALLOC() fails.\n" );
        fflush( stderr );
        return NULL;
    }

    strcpy( preamble->version, "" );
    preamble->block_size              = 0;
    preamble->num_buffered_blocks     = 0;
    preamble->is_big_endian           = CLOG_BOOL_NULL;
    preamble->comm_world_size         = 0;
    preamble->known_eventID_start     = 0;
    preamble->user_eventID_start      = 0;
    preamble->user_solo_eventID_start = 0;
    preamble->known_stateID_count     = 0;
    preamble->user_stateID_count      = 0;
    preamble->user_solo_eventID_count = 0;

    return preamble;
}

void CLOG_Preamble_free( CLOG_Preamble_t **preamble_handle )
{
    CLOG_Preamble_t  *preamble;

    preamble = *preamble_handle;
    if ( preamble != NULL )
        FREE( preamble );
    *preamble_handle = NULL;
}

void CLOG_Preamble_env_init( CLOG_Preamble_t *preamble )
{
    char *env_block_size;
    char *env_buffered_blocks;
    int   my_rank, num_procs;
#if !defined( CLOG_NOMPI )
    int   ierr;
#endif


#if !defined( CLOG_NOMPI )
    PMPI_Comm_rank( MPI_COMM_WORLD, &my_rank );
    PMPI_Comm_size( MPI_COMM_WORLD, &num_procs );
#else
    my_rank   = 0;
    num_procs = 1;
#endif
    preamble->comm_world_size  = num_procs;

    strcpy( preamble->version, CLOG_VERSION );

    /* Set the OS's native byte ordering to the CLOG_Preamble_t */
#if defined( WORDS_BIGENDIAN )
    preamble->is_big_endian = CLOG_BOOL_TRUE;
#else
    preamble->is_big_endian = CLOG_BOOL_FALSE;
#endif

    if ( my_rank == 0 ) {
        env_block_size = (char *) getenv( "CLOG_BLOCK_SIZE" );
        if ( env_block_size != NULL ) {
            preamble->block_size = atoi( env_block_size );
            if (    preamble->block_size <= 0
                 || preamble->block_size >  ONE_GIGA )
                preamble->block_size = CLOG_DEFAULT_BLOCK_SIZE;
        }
        else
            preamble->block_size = CLOG_DEFAULT_BLOCK_SIZE;
                                                                                    
        env_buffered_blocks = (char *) getenv( "CLOG_BUFFERED_BLOCKS" );
        if ( env_buffered_blocks != NULL ) {
            preamble->num_buffered_blocks = atoi( env_buffered_blocks );
            if (    preamble->num_buffered_blocks <= 0
                 || preamble->num_buffered_blocks >  ONE_GIGA )
                preamble->num_buffered_blocks = CLOG_DEFAULT_BUFFERED_BLOCKS;
        }
        else
            preamble->num_buffered_blocks = CLOG_DEFAULT_BUFFERED_BLOCKS;
    }

#if !defined( CLOG_NOMPI )
    /*
       MPI_Bcast() from _root_ on preamble's block_size
       and num_buffered_blocks to all.
    */
    ierr = PMPI_Bcast( &(preamble->block_size), 1, MPI_INT,
                       0, MPI_COMM_WORLD );
    if ( ierr != MPI_SUCCESS ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_env_init() - \n"
                         "\t""MPI_Bcast(preamble->block_size) fails.\n" );
        fflush( stderr );
        PMPI_Abort( MPI_COMM_WORLD, 1 );
    }

    ierr = PMPI_Bcast( &(preamble->num_buffered_blocks), 1, MPI_INT,
                       0, MPI_COMM_WORLD );
    if ( ierr != MPI_SUCCESS ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_env_init() - \n"
                         "\t""MPI_Bcast(num_buffered_blocks) fails.\n" );
        fflush( stderr );
        PMPI_Abort( MPI_COMM_WORLD, 1 );
    }
#endif

    /*
       user_stateID_count and user_solo_eventID_count are set with
       some typical values, just in case the the program crashes
       before CLOG_Merger_init() is called.
    */
    preamble->known_eventID_start     = CLOG_KNOWN_EVENTID_START;
    preamble->user_eventID_start      = CLOG_USER_EVENTID_START;
    preamble->user_solo_eventID_start = CLOG_USER_SOLO_EVENTID_START;
    preamble->known_stateID_count     = CLOG_USER_STATEID_START
                                      - CLOG_KNOWN_STATEID_START;
    preamble->user_stateID_count      = 100;
    preamble->user_solo_eventID_count = 100;
}

#define CLOG_PREAMBLE_STRLEN  32

/*
    is_always_big_endian != CLOG_BOOL_TRUE
    => saving the OS native byte ordering determined by configure.
*/
void CLOG_Preamble_write( const CLOG_Preamble_t *preamble,
                                int              is_always_big_endian,
                                int              fd )
{
    char  buffer[ CLOG_PREAMBLE_SIZE ];
    char  value_str[ CLOG_PREAMBLE_STRLEN ];
    char *buf_ptr, *buf_tail;
    int   ierr;

    buf_ptr  = (char *) buffer;
    buf_tail = buf_ptr + CLOG_PREAMBLE_SIZE - 1;

    /* Write the CLOG version ID */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    preamble->version,
                                    "CLOG Version ID" );

    /* Write the CLOG Endianess */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "is_big_endian=",
                                    "CLOG Endianess Title" );
    if ( is_always_big_endian == CLOG_BOOL_TRUE )
        strcpy( value_str, "TRUE " );  /* Always BIG_ENDIAN (Java byteorder) */
    else {  /* This is OS native byte ordering */
        if ( preamble->is_big_endian == CLOG_BOOL_TRUE )
            strcpy( value_str, "TRUE " );
        else
            strcpy( value_str, "FALSE" );
    }
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG Endianess Value" );

    /* Write the CLOG Block Size */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "block_size=",
                                    "CLOG Block Size Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->block_size );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG Block Size Value" );

    /* Write the CLOG Number of Buffered Blocks */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "num_buffered_blocks=",
                                    "CLOG Buffered Blocks Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->num_buffered_blocks );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG Buffered Blocks Value" );

    /* Write the MPI_COMM_WORLD's size */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "comm_world_size=",
                                    "MPI_COMM_WORLD Size Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->comm_world_size );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "MPI_COMM_WORLD Size Value" );

    /* Write the CLOG_KNOWN_EVENTID_START */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "known_eventID_start=",
                                    "CLOG_KNOWN_EVENTID_START Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->known_eventID_start );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG_KNOWN_EVENTID_START Value" );

    /* Write the CLOG_USER_EVENTID_START */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "user_eventID_start=",
                                    "CLOG_USER_EVENTID_START Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->user_eventID_start );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG_USER_EVENTID_START Value" );

    /* Write the CLOG_USER_SOLO_EVENTID_START */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "user_eventID_start=",
                                    "CLOG_USER_SOLO_EVENTID_START Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->user_solo_eventID_start );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG_USER_SOLO_EVENTID_START Value" );

    /* Write the CLOG known_stateID_count */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "known_stateID_count=",
                                    "CLOG known_stateID_count Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->known_stateID_count );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG known_stateID_count Value" );

    /* Write the CLOG user_stateID_count */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "user_stateID_count=",
                                    "CLOG user_stateID_count Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->user_stateID_count );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG user_stateID_count Value" );

    /* Write the CLOG user_solo_eventID_count */
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail,
                                    "user_solo_eventID_count=",
                                    "CLOG user_solo_eventID_count Title" );
    snprintf( value_str, CLOG_PREAMBLE_STRLEN, "%d",
              preamble->user_solo_eventID_count );
    /* just in case, there isn't \0 in value_str  */
    value_str[ CLOG_PREAMBLE_STRLEN-1 ] = '\0';
    buf_ptr = CLOG_Util_strbuf_put( buf_ptr, buf_tail, value_str,
                                    "CLOG user_solo_eventID_count Value" );

    if ( buf_ptr > buf_tail ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_write() - Error \n"
                         "\t""Internal buffer overflows!.\n" );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }

    /* Initialize the rest of the buffer[] to zero to keep valgrind happy */
    while ( buf_ptr <= buf_tail ) {
        *buf_ptr = 0;
        buf_ptr++;
    }

    ierr = write( fd, buffer, CLOG_PREAMBLE_SIZE );
    if ( ierr != CLOG_PREAMBLE_SIZE ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_write() - Error \n"
                         "\t""Write to the logfile fails.\n" );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }
}

void CLOG_Preamble_read( CLOG_Preamble_t *preamble, int fd )
{
    char  buffer[ CLOG_PREAMBLE_SIZE ];
    char  value_str[ CLOG_PREAMBLE_STRLEN ];
    char *buf_ptr, *buf_tail;
    int   ierr;

    ierr = read( fd, buffer, CLOG_PREAMBLE_SIZE );
    if ( ierr != CLOG_PREAMBLE_SIZE ) {
        fprintf( stderr, __FILE__":CLOG_Preamble_read() - \n"
                         "\t""read(%d) fails to read CLOG Preamble buffer.\n ",
                         CLOG_PREAMBLE_SIZE );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }

    buf_ptr  = (char *) buffer;
    buf_tail = buf_ptr + CLOG_PREAMBLE_SIZE - 1;

    /* Read the CLOG version ID */
    buf_ptr = CLOG_Util_strbuf_get( preamble->version,
                                    &(preamble->version[CLOG_VERSION_STRLEN-1]),
                                    buf_ptr, "CLOG Version ID" );

    if ( strncmp( preamble->version, CLOG_VERSION, CLOG_VERSION_STRLEN ) != 0 )
    {
        fprintf( stderr, __FILE__":CLOG_Preamble_read() - ERROR:\n"
                         "\t""The input version ID %s is not %s expected!\n",
                         preamble->version, CLOG_VERSION );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }
    /*  Need to be checking to make sure this is the same CLOG version */

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Endianess Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Endianess Value" );
    if ( strcmp( value_str, "TRUE " ) == 0 )
        preamble->is_big_endian = CLOG_BOOL_TRUE;
    else
        preamble->is_big_endian = CLOG_BOOL_FALSE;

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Block Size Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Block Size Value" );
    preamble->block_size = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Buffered Blocks Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG Buffered Blocks Value" );
    preamble->num_buffered_blocks = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "MPI_COMM_WORLD Size Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "MPI_COMM_WORLD Size Value" );
    preamble->comm_world_size     = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG_KNOWN_EVENTID_START Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG_KNOWN_EVENTID_START Value" );
    preamble->known_eventID_start = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG_USER_EVENTID_START Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG_USER_EVENTID_START Value" );
    preamble->user_eventID_start = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr,
                                    "CLOG_USER_SOLO_EVENTID_START Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr,
                                    "CLOG_USER_SOLO_EVENTID_START Value" );
    preamble->user_solo_eventID_start = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG known_stateID_count Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG known_stateID_count Value" );
    preamble->known_stateID_count = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG user_stateID_count Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr, "CLOG user_stateID_count Value" );
    preamble->user_stateID_count = (unsigned int) atoi( value_str );

    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr,
                                    "CLOG user_solo_eventID_count Title" );
    buf_ptr = CLOG_Util_strbuf_get( value_str,
                                    &(value_str[ CLOG_PREAMBLE_STRLEN-1 ]),
                                    buf_ptr,
                                    "CLOG user_solo_eventID_count Value" );
    preamble->user_solo_eventID_count = (unsigned int) atoi( value_str );
}

void CLOG_Preamble_print( const CLOG_Preamble_t *preamble, FILE *stream )
{
    fprintf( stream, "%s\n", preamble->version );

    if ( preamble->is_big_endian == CLOG_BOOL_TRUE )
        fprintf( stream, "is_big_endian = TRUE\n" );
    else
        fprintf( stream, "is_big_endian = FALSE\n" );

    fprintf( stream, "num_buffered_blocks = %d\n",
                     preamble->num_buffered_blocks );
    fprintf( stream, "block_size = %d\n",
                     preamble->block_size );
    fprintf( stream, "comm_world_size = %d\n",
                     preamble->comm_world_size );
    fprintf( stream, "known_eventID_start = %d\n",
                     preamble->known_eventID_start );
    fprintf( stream, "user_eventID_start = %d\n",
                     preamble->user_eventID_start );
    fprintf( stream, "user_solo_eventID_start = %d\n",
                     preamble->user_solo_eventID_start );
    fprintf( stream, "known_stateID_count = %d\n",
                     preamble->known_stateID_count );
    fprintf( stream, "user_stateID_count = %d\n",
                     preamble->user_stateID_count );
    fprintf( stream, "user_solo_eventID_count = %d\n",
                     preamble->user_solo_eventID_count );
}
