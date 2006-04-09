/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#if !defined( _CLOG_PREAMBLE )
#define _CLOG_PREAMBLE

/*
   the function of the CLOG logging routines is to write log records into
   buffers, which are processed later.
*/
#define CLOG_PREAMBLE_SIZE   1024
#define CLOG_VERSION_STRLEN    12

typedef struct {
    char          version[ CLOG_VERSION_STRLEN ];
    int           is_big_endian; /* either CLOG_BOOL_TRUE or CLOG_BOOL_FALSE */
    unsigned int  block_size;
    unsigned int  num_buffered_blocks;
    unsigned int  comm_world_size;
    unsigned int  known_eventID_start;
    unsigned int  user_eventID_start;
    unsigned int  user_solo_eventID_start;
    unsigned int  known_stateID_count;
    unsigned int  user_stateID_count;
    unsigned int  user_solo_eventID_count;
} CLOG_Preamble_t;

CLOG_Preamble_t *CLOG_Preamble_create( void );

void CLOG_Preamble_free( CLOG_Preamble_t **preamble );

void CLOG_Preamble_env_init( CLOG_Preamble_t *preamble );

void CLOG_Preamble_write( const CLOG_Preamble_t *preamble,
                                int              is_always_big_endian,
                                int              fd );

void CLOG_Preamble_read( CLOG_Preamble_t *preamble, int fd );

void CLOG_Preamble_print( const CLOG_Preamble_t *preamble, FILE *stream );

#endif /* of _CLOG_PREAMBLE */
