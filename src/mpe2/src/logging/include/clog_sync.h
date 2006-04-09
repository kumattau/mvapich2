/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#if !defined( _CLOG_SYNC )
#define _CLOG_SYNC

#define CLOG_MASTER_READY      801
#define CLOG_SLAVE_READY       802
#define CLOG_TIME_QUERY        803
#define CLOG_TIME_ANSWER       804

typedef struct {
   int                 is_ok_to_sync;
   int                 world_size;
   int                 world_rank;
   CLOG_Time_t        *timediffs;
} CLOG_Sync_t;

CLOG_Sync_t *CLOG_Sync_create( int num_mpi_procs, int local_mpi_rank );

void CLOG_Sync_free( CLOG_Sync_t **sync_handle );

void CLOG_Sync_init( CLOG_Sync_t *sync );

void CLOG_Sync_set_timediffs( CLOG_Sync_t *sync, int root );

CLOG_Time_t CLOG_Sync_update_timediffs( CLOG_Sync_t *sync );

#endif
