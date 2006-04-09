/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#if !defined( _CLOG )
#define _CLOG

/*
   Version 2.0: Initial rewrite of CLOG.
   Version 2.1: Clean up of CLOG record's data structure to minimize
                wasted disk space.
   Version 2.2: Added CLOG internal profiling state: CLOG_Buffer_write2disk
   Version 2.3: Added support of MPI_Comm.
   Version 2.4: Added support of user-defined event drawable/category.
                Updated CLOG_Preamble with MPE eventID and stateID info.
*/
#define CLOG_VERSION          "CLOG-02.40"

#include "clog_buffer.h"
#include "clog_sync.h"
#include "clog_merger.h"
#include "clog_record.h"

/*
   This is for MPI implementation, i.e., src/wrapper/log_mpi_core.c
   CLOG_KNOWN_EVENTID_START < CLOG_USER_EVENTID_START
*/
#define CLOG_KNOWN_EVENTID_START 0

/*
   This is for users: CLOG_KNOWN_EVENTID_START < CLOG_USER_EVENTID_START
   CLOG_USER_EVENTID_START = 2 * CLOG_USER_STATEID_START
*/
#define CLOG_USER_EVENTID_START 600

/* This is for users: CLOG_USER_EVENTID_START < CLOG_USER_SOLE_EVENTID_START */
#define CLOG_USER_SOLO_EVENTID_START 5000

/*
   This is for MPI implementation, i.e., src/wrapper/log_mpi_core.c
   CLOG_KNOWN_STATEID_START < CLOG_USER_STATEID_START
*/
#define CLOG_KNOWN_STATEID_START 0

/* This is for users: CLOG_KNOWN_STATEID_START < CLOG_USER_STATEID_START */
#define CLOG_USER_STATEID_START 300

#define CLOG_COMM_NULL          -1

typedef struct {
    CLOG_Buffer_t     *buffer;
    CLOG_Sync_t       *syncer;
    CLOG_Merger_t     *merger;
    int                known_eventID;
    int                known_stateID;
    int                user_eventID;
    int                user_stateID;
    int                user_solo_eventID;
} CLOG_Stream_t;

CLOG_Stream_t *CLOG_Open( void );

void CLOG_Close( CLOG_Stream_t **stream );

void CLOG_Local_init( CLOG_Stream_t *stream, const char *local_tmpfile_name );

void CLOG_Local_finalize( CLOG_Stream_t *stream );

int  CLOG_Get_known_eventID( CLOG_Stream_t *stream );

int  CLOG_Get_user_eventID( CLOG_Stream_t *stream );

int  CLOG_Get_user_solo_eventID( CLOG_Stream_t *stream );

int  CLOG_Get_known_stateID( CLOG_Stream_t *stream );

int  CLOG_Get_user_stateID( CLOG_Stream_t *stream );

int  CLOG_Check_known_stateID( CLOG_Stream_t *stream, int stateID );

void CLOG_Converge_init(       CLOG_Stream_t *stream,
                         const char          *merged_file_prefix );

void CLOG_Converge_finalize( CLOG_Stream_t *stream );

void CLOG_Converge_sort( CLOG_Stream_t *stream );

#endif  /* of _CLOG */
