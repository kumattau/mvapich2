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
#if defined( STDC_HEADERS ) || defined( HAVE_STRING_H )
#include <string.h>
#endif

#include "clog_const.h"
#include "clog_timer.h"
#include "clog_util.h"
#include "clog_sync.h"

#if !defined( CLOG_NOMPI )
#include "mpi.h"
#endif

CLOG_Sync_t *CLOG_Sync_create( int world_size, int world_rank )
{
    CLOG_Sync_t  *sync;
    int           idx;

    sync = (CLOG_Sync_t *) MALLOC( sizeof(CLOG_Sync_t) );
    if ( sync == NULL ) {
        fprintf( stderr, __FILE__":CLOG_Sync_create() - \n"
                         "\t""MALLOC() fails for CLOG_Sync_t!\n" );
        fflush( stderr );
        return NULL;
    }

    sync->is_ok_to_sync  = CLOG_BOOL_FALSE;
    sync->world_size     = world_size;
    sync->world_rank     = world_rank;

    /* Set the sync->timediffs[] */
    sync->timediffs = (CLOG_Time_t *)
                      MALLOC( sync->world_size * sizeof(CLOG_Time_t) );
    if ( sync->timediffs == NULL ) {
        fprintf( stderr, __FILE__":CLOG_Sync_create() - \n"
                         "\t""MALLOC() fails for CLOG_Sync_t.timediffs[]!\n" );
        fflush( stderr );
        return NULL;
    }

    /* Initialize CLOG_Sync_t.timediffs[] with 0.0 */
    for ( idx = 0; idx < sync->world_size; idx++ )
        sync->timediffs[ idx ] = 0.0;

    return sync;
}

void CLOG_Sync_free( CLOG_Sync_t **sync_handle )
{
    CLOG_Sync_t  *sync;

    sync = *sync_handle;
    if ( sync != NULL ) {
        if ( sync->timediffs != NULL ) {
            FREE( sync->timediffs );
            sync->timediffs = NULL;
        }
        FREE( sync );
        *sync_handle  = NULL;
    }
}

void CLOG_Sync_init( CLOG_Sync_t *sync )
{
#if !defined( CLOG_NOMPI )
    char         *env_forced_sync;
    int           local_is_ok_to_sync;

    if ( CLOG_Util_is_MPIWtime_synchronized() == CLOG_BOOL_TRUE )
        local_is_ok_to_sync = CLOG_BOOL_FALSE;
    else
        local_is_ok_to_sync = CLOG_BOOL_TRUE;

    env_forced_sync = (char *) getenv( "MPE_CLOCKS_SYNC" );
    if ( env_forced_sync != NULL) {
        if (    strcmp( env_forced_sync, "true" ) == 0
             || strcmp( env_forced_sync, "TRUE" ) == 0
             || strcmp( env_forced_sync, "yes" ) == 0
             || strcmp( env_forced_sync, "YES" ) == 0 )
            local_is_ok_to_sync = CLOG_BOOL_TRUE;
        else if (    strcmp( env_forced_sync, "false" ) == 0
                  || strcmp( env_forced_sync, "FALSE" ) == 0
                  || strcmp( env_forced_sync, "no" ) == 0
                  || strcmp( env_forced_sync, "NO" ) == 0 )
            local_is_ok_to_sync = CLOG_BOOL_FALSE;
        /*
        else
            Use What MPI_WTIME_IS_GLOBAL said.
        */
    }

    PMPI_Allreduce( &local_is_ok_to_sync, &(sync->is_ok_to_sync),
                    1, MPI_INT, MPI_MAX, MPI_COMM_WORLD );
#endif
}

/*@
    CLOG_Sync_set_timediffs - synchronize clocks for adjusting times in merge

This version is sequential and non-scalable.  The root process serially
synchronizes with each slave, using the first algorithm in Gropp, "Scalable
clock synchronization on distributed processors without a common clock".
The array is calculated on the root but broadcast and returned on all
processes.

Inout Parameters:

+ sync            - CLOG_Sync_t contains CLOG_Time_t[] to be filled in
- root            - process to serve as master

@*/
void CLOG_Sync_set_timediffs( CLOG_Sync_t *sync, int root )
{
#if !defined( CLOG_NOMPI )
    int          num_tests = 3;
    int          dummy, ii, jj;
    CLOG_Time_t  time_1, time_2, time_i, bestgap, bestshift = 0.0;
    MPI_Status   status;

    PMPI_Barrier( MPI_COMM_WORLD );
    PMPI_Barrier( MPI_COMM_WORLD ); /* approximate common starting point */

    if ( sync->world_rank == root ) {
        /* I am the master, but not nec. 0 */
        for ( ii = 0; ii < sync->world_size; ii++ ) {
            if ( ii != sync->world_rank ) {
                bestgap = 1000000.0; /* infinity, fastest turnaround so far */
                for ( jj = 0; jj < num_tests; jj++ ) {
                    PMPI_Send( &dummy, 0, MPI_INT, ii,
                               CLOG_MASTER_READY, MPI_COMM_WORLD );
                    PMPI_Recv( &dummy, 0, MPI_INT, ii,
                               CLOG_SLAVE_READY, MPI_COMM_WORLD, &status );
                    time_1 = CLOG_Timer_get();
                    PMPI_Send( &dummy, 0, MPI_INT, ii,
                               CLOG_TIME_QUERY, MPI_COMM_WORLD );
                    PMPI_Recv( &time_i, 1, CLOG_TIME_MPI_TYPE, ii,
                               CLOG_TIME_ANSWER, MPI_COMM_WORLD, &status );
                    time_2 = CLOG_Timer_get();
                    if ( (time_2 - time_1) < bestgap ) {
                        bestgap   = time_2 - time_1;
                        bestshift =  0.5 * (time_2 + time_1) - time_i;
                    }
                }
                sync->timediffs[ii] = bestshift;
            }
            else                 /* ii = root */
                sync->timediffs[ii] = 0.0;
        }
    }
    else {                        /* not the root */
        for ( jj = 0; jj < num_tests; jj++ ) {
            PMPI_Recv( &dummy, 0, MPI_INT, root,
                       CLOG_MASTER_READY, MPI_COMM_WORLD, &status );
            PMPI_Send( &dummy, 0, MPI_INT, root,
                       CLOG_SLAVE_READY, MPI_COMM_WORLD );
            PMPI_Recv( &dummy, 0, MPI_INT, root,
                       CLOG_TIME_QUERY, MPI_COMM_WORLD, &status );
            time_i = CLOG_Timer_get();
            PMPI_Send( &time_i, 1, CLOG_TIME_MPI_TYPE, root,
                       CLOG_TIME_ANSWER, MPI_COMM_WORLD );
        }
    }
    PMPI_Bcast( sync->timediffs, sync->world_size, CLOG_TIME_MPI_TYPE,
                root, MPI_COMM_WORLD );
#endif
}

/*
   Return local timediff after synchronization
*/
CLOG_Time_t CLOG_Sync_update_timediffs( CLOG_Sync_t *sync )
{
    CLOG_Sync_set_timediffs( sync, 0 );
    return sync->timediffs[ sync->world_rank ];
}
