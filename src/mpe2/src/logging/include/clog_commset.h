/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#if !defined( _CLOG_COMM )
#define _CLOG_COMM

/*
#if defined( HAVE_UUID_UUID_H )
#include <uuid/uuid.h>
#define CLOG_CommGID_t         uuid_t
#endif
*/

#include "clog_uuid.h"

#define CLOG_CommGID_t            CLOG_Uuid_t
#define CLOG_CommLID_t            int
#define CLOG_ThreadLID_t          int

#define CLOG_COMM_TABLE_INCRE     10
#define CLOG_COMM_LID_NULL       -999999999
#define CLOG_COMM_RANK_NULL      -1
#define CLOG_COMM_WRANK_NULL     -1
#define CLOG_COMM_TAG_START       100000

/* Define CLOG communicator event types */
#define CLOG_COMM_WORLD_CREATE    0    /* MPI_COMM_WORLD creation */
#define CLOG_COMM_SELF_CREATE     1    /* MPI_COMM_SELF  creation */
#define CLOG_COMM_FREE            10   /* MPI_Comm_free() */
#define CLOG_COMM_INTRA_CREATE    100  /* intracomm creation */
#define CLOG_COMM_INTRA_LOCAL     101  /* local intracomm of intercomm */
#define CLOG_COMM_INTRA_REMOTE    102  /* remote intracomm of intercomm */
#define CLOG_COMM_INTER_CREATE    1000 /* intercomm creation */

#define CLOG_COMM_KIND_UNKNOWN   -1    /* is UNKNOWN */
#define CLOG_COMM_KIND_INTER      0    /* is intercommunicator */
#define CLOG_COMM_KIND_INTRA      1    /* is intracommunicator */
#define CLOG_COMM_KIND_LOCAL      2    /* is local  intracommunicator */
#define CLOG_COMM_KIND_REMOTE     3    /* is remote intracommunicator */

#if !defined( CLOG_NOMPI )
#include "mpi.h"

typedef struct CLOG_CommIDs_t_ {
          CLOG_CommGID_t   global_ID;  /* global comm ID */
          CLOG_CommLID_t   local_ID;   /* local comm ID */
          int              kind;       /* value = CLOG_COMM_KIND_xxxx */
          int              world_rank; /* rank in MPI_COMM_WORLD */
          int              comm_rank;  /* rank of comm labelled by global_ID */
          MPI_Comm         comm;
   struct CLOG_CommIDs_t_ *next;       /* related CLOG_CommIDs_t* */
} CLOG_CommIDs_t;

/* Definition for CLOG Communicator control data structure */
typedef struct {
    int                 LID_key;
    int                 world_size;    /* size returned by MPI_Comm_size */
    int                 world_rank;    /* rank returned by MPI_Comm_rank */
    unsigned int        max;
    CLOG_CommLID_t      count;
    CLOG_CommIDs_t     *table;
} CLOG_CommSet_t;

CLOG_CommSet_t* CLOG_CommSet_create( void );

void CLOG_CommSet_free( CLOG_CommSet_t **comm_handle );

void CLOG_CommSet_init( CLOG_CommSet_t *commset );

const CLOG_CommIDs_t* CLOG_CommSet_add_intracomm( CLOG_CommSet_t *commset,
                                                  MPI_Comm comm );

const CLOG_CommIDs_t*
CLOG_CommSet_add_intercomm(       CLOG_CommSet_t *commset,
                                  MPI_Comm        intercomm,
                            const CLOG_CommIDs_t *orig_intracommIDs );

CLOG_CommLID_t CLOG_CommSet_get_LID( CLOG_CommSet_t *commset, MPI_Comm comm );

const CLOG_CommIDs_t* CLOG_CommSet_get_IDs( CLOG_CommSet_t *commset,
                                            MPI_Comm comm );

const CLOG_CommIDs_t* CLOG_CommTable_get( const CLOG_CommIDs_t *table,
                                                int             table_count,
                                          const CLOG_CommGID_t  commgid );

const CLOG_CommIDs_t *CLOG_CommSet_add_GID( CLOG_CommSet_t *commset,
                                            CLOG_CommGID_t  commgid );

void CLOG_CommSet_merge( CLOG_CommSet_t *commset );

#else  /* of if !defined( CLOG_NOMPI ) */
#define MPI_Comm                    int
#define MPI_COMM_WORLD              0
#define CLOG_CommSet_t              char
#define CLOG_CommIDs_t              void
#define CLOG_CommSet_get_IDs(A,B)   NULL
#endif /* of if !defined( CLOG_NOMPI ) */

#endif /* of _CLOG_COMM */
