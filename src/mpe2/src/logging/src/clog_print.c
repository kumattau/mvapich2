/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#include "mpe_logging_conf.h"

#if defined( STDC_HEADERS ) || defined( HAVE_STDIO_H )
#include <stdio.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif
#if defined( HAVE_FCNTL_H )
#include <fcntl.h>
#endif
#ifdef HAVE_IO_H
#include <io.h>
#endif

#include "clog_const.h"
#include "clog_record.h"
#include "clog_preamble.h"
#include "clog_block.h"

int main( int argc, char *argv[] )
{
    CLOG_Preamble_t   *preamble;
    CLOG_BlockData_t  *blkdata;
    int                logfd;                  /* logfile */
    int                ierr;

    if ( argc < 2 ) {
        fprintf( stderr,"usage: %s <logfile>\n", argv[0] );
        exit( -1 );
    }

    logfd = OPEN( argv[1], O_RDONLY, 0 );
    if ( logfd == -1 ) {
        printf( "Could not open file %s for reading\n", argv[1] );
        exit( -2 );
    }

    CLOG_Rec_sizes_init();

    preamble = CLOG_Preamble_create();
    CLOG_Preamble_read( preamble, logfd );
    CLOG_Preamble_print( preamble, stdout );

    blkdata  = CLOG_BlockData_create( preamble->block_size );
    do {
        ierr = read( logfd, blkdata->head, preamble->block_size );
        if ( ierr > 0 ) {
            if ( ierr == preamble->block_size ) {
#if defined( WORDS_BIGENDIAN )
                if ( preamble->is_big_endian != CLOG_BOOL_TRUE )
                    CLOG_BlockData_swap_bytes_first( blkdata );
#else
                if ( preamble->is_big_endian == CLOG_BOOL_TRUE )
                    CLOG_BlockData_swap_bytes_first( blkdata );
#endif
                CLOG_BlockData_reset( blkdata );
                CLOG_BlockData_print( blkdata, stdout );
            }
            else
                fprintf( stderr, "%s: %d bytes out of expected %d bytes read\n",
                         argv[0], ierr, preamble->block_size );
        }
    } while ( ierr > 0 );

    if ( ierr < 0 ) {
        fprintf( stderr, "%s: could not read %d bytes with error code=%d\n",
                         argv[0], preamble->block_size, ierr );
        exit( -3 );
    }

    CLOG_BlockData_free( &(blkdata) );
    CLOG_Preamble_free( &(preamble) );
    close( logfd );

    return( 0 );
}
