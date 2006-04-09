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
#include "clog_uuid.h"
#include "clog_commset.h"
#include "clog_util.h"

#if !defined( CLOG_NOMPI )

#if !defined( HAVE_PMPI_COMM_CREATE_KEYVAL )
#define PMPI_Comm_create_keyval PMPI_Keyval_create
#endif

#if !defined( HAVE_PMPI_COMM_FREE_KEYVAL )
#define PMPI_Comm_free_keyval PMPI_Keyval_free
#endif

#if !defined( HAVE_PMPI_COMM_SET_ATTR )
#define PMPI_Comm_set_attr PMPI_Attr_put
#endif

#if !defined( HAVE_PMPI_COMM_GET_ATTR )
#define PMPI_Comm_get_attr PMPI_Attr_get
#endif

CLOG_CommSet_t* CLOG_CommSet_create( void )
{
    CLOG_CommSet_t  *commset;
    int              table_size;

    commset = (CLOG_CommSet_t *) MALLOC( sizeof(CLOG_CommSet_t) );
    if ( commset == NULL ) {
        fprintf( stderr, __FILE__":CLOG_CommSet_create() - MALLOC() fails.\n" );
        fflush( stderr );
        return NULL;
    }

    /* LID_key Initialized to 'unallocated' */
    commset->LID_key   = MPI_KEYVAL_INVALID;
    commset->max       = CLOG_COMM_TABLE_INCRE;
    commset->count     = 0;
    table_size         = commset->max * sizeof(CLOG_CommIDs_t);
    commset->table     = (CLOG_CommIDs_t *) MALLOC( table_size );

    return commset;
}

void CLOG_CommSet_free( CLOG_CommSet_t **comm_handle )
{
    CLOG_CommSet_t  *commset;

    commset = *comm_handle;
    if ( commset->table != NULL )
        FREE( commset->table );
    PMPI_Comm_free_keyval( &(commset->LID_key) );
    if ( commset != NULL )
        FREE( commset );
    *comm_handle = NULL;
}

/*
   CLOG_CommSet_init() should only be called if MPI is involved.
*/
void CLOG_CommSet_init( CLOG_CommSet_t *commset )
{
    int          *extra_state = NULL; /* unused variable */

    CLOG_Uuid_init();

    /* create LID_Key */
    PMPI_Comm_create_keyval( MPI_COMM_NULL_COPY_FN,
                             MPI_COMM_NULL_DELETE_FN, 
                             &(commset->LID_key), extra_state );

    PMPI_Comm_size( MPI_COMM_WORLD, &(commset->world_size) );
    PMPI_Comm_rank( MPI_COMM_WORLD, &(commset->world_rank) );

    CLOG_CommSet_add_intracomm( commset, MPI_COMM_WORLD );
    CLOG_CommSet_add_intracomm( commset, MPI_COMM_SELF );
}

static CLOG_CommIDs_t* CLOG_CommSet_get_new_IDs( CLOG_CommSet_t *commset )
{
    CLOG_CommIDs_t  *new_table;
    CLOG_CommIDs_t  *new_entry;
    int              new_size;

    /* Enlarge the CLOG_CommSet_t.table if necessary */
    if ( commset->count >= commset->max ) {
        commset->max += CLOG_COMM_TABLE_INCRE;
        new_size      = commset->max * sizeof(CLOG_CommIDs_t);
        new_table     = (CLOG_CommIDs_t *) REALLOC( commset->table, new_size );
        if ( new_table == NULL ) {
            fprintf( stderr, __FILE__":CLOG_CommSet_get_next_IDs() - \n"
                             "\t""REALLOC(%p,%d) fails!\n",
                             commset->table, new_size );
            fflush( stderr );
            CLOG_Util_abort( 1 );
        }
        commset->table = new_table;
    }

    /* return the next available table entry in CLOG_CommSet_t */
    new_entry  = &(commset->table[ commset->count ]);

    /* Set the local Comm ID temporarily equal to the table entry's index */
    new_entry->local_ID   = commset->count;

    /* set the new entry with the process rank in MPI_COMM_WORLD */
    new_entry->world_rank = commset->world_rank;

    /* Set the related CLOG_CommIDs_t* as NULL(Most commIDs refer intracomms. */
    new_entry->next      = NULL;

    /*
       Increment the count to next available slot in the table.
       Also, the count indicates the current used slot in the table.
    */
    commset->count++;

    return new_entry;
}

/*
    This function should be used on every newly created communicator
    The input argument MPI_Comm is assumed to be not MPI_COMM_NULL
*/
const CLOG_CommIDs_t *CLOG_CommSet_add_intracomm( CLOG_CommSet_t *commset,
                                                  MPI_Comm        intracomm )
{
    CLOG_CommIDs_t  *intracommIDs;

    /* Update the next available table entry in CLOG_CommSet_t */
    intracommIDs         = CLOG_CommSet_get_new_IDs( commset );
    intracommIDs->kind   = CLOG_COMM_KIND_INTRA;

    /* Set the input MPI_Comm's LID_key attribute with new local CommID's LID */
    PMPI_Comm_set_attr( intracomm, commset->LID_key,
                        (void *) (MPI_Aint) intracommIDs->local_ID );

    /* Set the Comm field */
    intracommIDs->comm   = intracomm;
    PMPI_Comm_rank( intracommIDs->comm, &(intracommIDs->comm_rank) );

    /* Set the global Comm ID */
    if ( intracommIDs->comm_rank == 0 )
        CLOG_Uuid_generate( intracommIDs->global_ID );
    PMPI_Bcast( intracommIDs->global_ID, CLOG_UUID_SIZE, MPI_CHAR,
                0, intracomm );
#if defined( CLOG_COMMSET_PRINT )
    char uuid_str[CLOG_UUID_STR_SIZE] = {0};
    CLOG_Uuid_sprint( intracommIDs->global_ID, uuid_str );
    fprintf( stdout, "comm_rank=%d, comm_global_ID=%s\n",
                     intracommIDs->comm_rank, uuid_str );
#endif

    return intracommIDs;
}

/*
    Assume that "intracomm" be the LOCAL communicator of the "intercomm".
*/
const CLOG_CommIDs_t*
CLOG_CommSet_add_intercomm(       CLOG_CommSet_t *commset,
                                  MPI_Comm        intercomm,
                            const CLOG_CommIDs_t *orig_intracommIDs )
{
    CLOG_CommIDs_t  *intercommIDs;
    CLOG_CommIDs_t  *local_intracommIDs, *remote_intracommIDs;
    MPI_Status       status;
    MPI_Request      request;
    int              is_intercomm;

    /* Confirm if input intercomm is really an intercommunicator */
    PMPI_Comm_test_inter( intercomm, &is_intercomm );
    if ( !is_intercomm )
        return CLOG_CommSet_add_intracomm( commset, intercomm );

    /* Set the next available table entry in CLOG_CommSet_t with intercomm */
    intercommIDs         = CLOG_CommSet_get_new_IDs( commset );
    intercommIDs->kind   = CLOG_COMM_KIND_INTER;

    /* Set the input MPI_Comm's LID_key attribute with new local CommID */
    PMPI_Comm_set_attr( intercomm, commset->LID_key,
                        (void *) (MPI_Aint) intercommIDs->local_ID );

    /* Set the Comm field with the intercomm's info */
    intercommIDs->comm   = intercomm;
    PMPI_Comm_rank( intercommIDs->comm, &(intercommIDs->comm_rank) );

    /* Set the global Comm ID based on intercomm's local group rank */
    if ( intercommIDs->comm_rank == 0 )
        CLOG_Uuid_generate( intercommIDs->global_ID );

    /*
       Broadcast the (local side of) intercomm ID within local intracomm,
       i.e. orig_intracommIDs->comm
    */
    PMPI_Bcast( intercommIDs->global_ID, CLOG_UUID_SIZE, MPI_CHAR,
                0, orig_intracommIDs->comm );

#if defined( CLOG_COMMSET_PRINT )
    char uuid_str[CLOG_UUID_STR_SIZE] = {0};
    CLOG_Uuid_sprint( intercommIDs->global_ID, uuid_str );
    fprintf( stdout, "comm_rank=%d, comm_global_ID=%s\n",
                     intercommIDs->comm_rank, uuid_str );
#endif

    /* Set the next available table entry with the LOCAL intracomm's info */
    local_intracommIDs            = CLOG_CommSet_get_new_IDs( commset );
    local_intracommIDs->kind      = CLOG_COMM_KIND_LOCAL;
    local_intracommIDs->local_ID  = orig_intracommIDs->local_ID;
    CLOG_Uuid_copy( orig_intracommIDs->global_ID,
                    local_intracommIDs->global_ID );
    local_intracommIDs->comm      = orig_intracommIDs->comm;
    local_intracommIDs->comm_rank = orig_intracommIDs->comm_rank;
    /* NOTE: LOCAL intracommIDs->comm_rank == intercommIDs->comm_rank */

    /* Set the next available table entry with the REMOTE intracomm's info */
    remote_intracommIDs             = CLOG_CommSet_get_new_IDs( commset );
    remote_intracommIDs->kind       = CLOG_COMM_KIND_REMOTE;
    /*
       Broadcast local_intracommIDs's GID to everyone in remote_intracomm, i.e.
       Send local_intracommIDs' GID from the root of local intracomms to the
       root of the remote intracomms and save it as remote_intracommIDs' GID.
       Then broadcast the received GID to the rest of intracomm.
    */
    if ( intercommIDs->comm_rank == 0 ) {
       PMPI_Irecv( remote_intracommIDs->global_ID, CLOG_UUID_SIZE, MPI_CHAR,
                   0, 9999, intercomm, &request );            
       PMPI_Send( local_intracommIDs->global_ID, CLOG_UUID_SIZE, MPI_CHAR,
                  0, 9999, intercomm );
       PMPI_Wait( &request, &status );
    }
    PMPI_Bcast( remote_intracommIDs->global_ID, CLOG_UUID_SIZE, MPI_CHAR,
                0, orig_intracommIDs->comm );
    /*
       Since REMOTE intracomm is NOT known or undefinable in LOCAL intracomm,
       (that is why we have intercomm).  Set comm and comm_rank to NULL
    */
    remote_intracommIDs->comm       = MPI_COMM_NULL;
    remote_intracommIDs->comm_rank  = -1;

    /* Set the related CLOG_CommIDs_t* to the local and remote intracommIDs */
    intercommIDs->next              = local_intracommIDs;
    local_intracommIDs->next        = remote_intracommIDs;

    return intercommIDs;
}



/*
    The input argument MPI_Comm is assumed not to be MPI_COMM_NULL
*/
CLOG_CommLID_t CLOG_CommSet_get_LID( CLOG_CommSet_t *commset, MPI_Comm comm )
{
    MPI_Aint  ptrlen_value;
    int       istatus;

    PMPI_Comm_get_attr( comm, commset->LID_key, &ptrlen_value, &istatus );
    if ( !istatus ) {
        fprintf( stderr, __FILE__":CLOG_CommSet_get_LID() - \n"
                         "\t""PMPI_Comm_get_attr() fails!\n" );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }
    return (CLOG_CommLID_t) ptrlen_value;
}

const CLOG_CommIDs_t* CLOG_CommSet_get_IDs( CLOG_CommSet_t *commset,
                                            MPI_Comm comm )
{
    MPI_Aint  ptrlen_value;
    int       istatus;

    PMPI_Comm_get_attr( comm, commset->LID_key, &ptrlen_value, &istatus );
    if ( !istatus ) {
        fprintf( stderr, __FILE__":CLOG_CommSet_get_IDs() - \n"
                         "\t""PMPI_Comm_get_attr() fails!\n" );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }
    return &(commset->table[ (CLOG_CommLID_t) ptrlen_value ]);
}

const CLOG_CommIDs_t* CLOG_CommTable_get( const CLOG_CommIDs_t *table,
                                                int             table_count,
                                          const CLOG_CommGID_t  commgid )
{
    int              idx;

    for ( idx = 0; idx < table_count; idx++ ) {
        if ( CLOG_Uuid_compare( table[idx].global_ID, commgid ) == 0 )
            return &(table[idx]);
    }
    return NULL;
}

const CLOG_CommIDs_t *CLOG_CommSet_add_GID( CLOG_CommSet_t *commset,
                                            CLOG_CommGID_t  commgid )
{
    CLOG_CommIDs_t  *commIDs;

    /* Update the next available table entry in CLOG_CommSet_t */
    commIDs              = CLOG_CommSet_get_new_IDs( commset );
    commIDs->kind        = CLOG_COMM_KIND_UNKNOWN;

    /* Set the global Comm ID */
    CLOG_Uuid_copy( commgid, commIDs->global_ID );

    /* Set the Comm field's */
    commIDs->comm        = MPI_COMM_NULL;
    commIDs->comm_rank   = -1;

    return commIDs;
}

void CLOG_CommSet_merge( CLOG_CommSet_t *commset )
{
          int              comm_world_size, comm_world_rank;
          int              rank_sep, rank_quot, rank_rem;
          int              rank_src, rank_dst;
          int              recv_table_count, recv_table_size;
          CLOG_CommIDs_t  *recv_table;
    const CLOG_CommIDs_t  *commIDs;
          MPI_Status       status;
          int              idx;

    comm_world_rank  = commset->world_rank;
    comm_world_size  = commset->world_size;

    /* Collect CLOG_CommIDs_t table through Recursive Doubling algorithm */
    rank_sep   = 1;
    rank_quot  = comm_world_rank >> 1; /* rank_quot  = comm_world_rank / 2; */
    rank_rem   = comm_world_rank &  1; /* rank_rem   = comm_world_rank % 2; */

    while ( rank_sep < comm_world_size ) {
        if ( rank_rem == 0 ) {
            rank_src = comm_world_rank + rank_sep;
            if ( rank_src < comm_world_size ) {
                /* Recv from rank_src */
                PMPI_Recv( &recv_table_count, 1, MPI_INT, rank_src,
                           CLOG_COMM_TAG_START, MPI_COMM_WORLD, &status );
                recv_table_size  = recv_table_count * sizeof(CLOG_CommIDs_t);
                recv_table  = (CLOG_CommIDs_t *) MALLOC( recv_table_size );
                if ( recv_table == NULL ) {
                    fprintf( stderr, __FILE__":CLOG_CommSet_merge() - \n"
                                     "\t""MALLOC(%d) fails!\n",
                                     recv_table_size );
                    fflush( stderr );
                    CLOG_Util_abort( 1 );
                }
                /*
                   For simplicity, receive commset's whole table and uses only
                   CLOG_CommGID_t column from the table.  The other columns
                   are relevant only to the sending process.
                */
                PMPI_Recv( recv_table, recv_table_size, MPI_CHAR, rank_src,
                           CLOG_COMM_TAG_START+1, MPI_COMM_WORLD, &status );

                for ( idx = 0; idx < recv_table_count; idx++ ) {
                    /*
                       If recv_table[ idx ].global_ID is not in commset,
                       add recv_table[ idx ].global_ID to commset
                    */
                    commIDs = CLOG_CommTable_get( commset->table,
                                                  commset->count,
                                                  recv_table[ idx ].global_ID );
                    if ( commIDs == NULL ) 
                        CLOG_CommSet_add_GID( commset,
                                              recv_table[ idx ].global_ID );
                }
                if ( recv_table != NULL ) {
                    FREE( recv_table );
                    recv_table = NULL;
                }
            }
        }
        else /* if ( rank_rem != 0 ) */ {
            /* After sending CLOG_CommIDs_t table, the process does a barrier */
            rank_dst = comm_world_rank - rank_sep;
            if ( rank_dst >= 0 ) {
                recv_table_count  = commset->count;
                /* Send from rank_dst */
                PMPI_Send( &recv_table_count, 1, MPI_INT, rank_dst,
                           CLOG_COMM_TAG_START, MPI_COMM_WORLD );
                /*
                   For simplicity, send commset's whole table including
                   useless things even though only CLOG_CommGID_t column
                   will be used from the table in the receiving process.
                */
                recv_table_size  = recv_table_count * sizeof(CLOG_CommIDs_t);
                PMPI_Send( commset->table, recv_table_size, MPI_CHAR, rank_dst,
                           CLOG_COMM_TAG_START+1, MPI_COMM_WORLD );
                break;  /* get out of the while loop */
            }
        }

        rank_rem    = rank_quot & 1; /* rank_rem   = rank_quot % 2; */
        rank_quot >>= 1;             /* rank_quot /= 2; */
        rank_sep  <<= 1;             /* rank_sep  *= 2; */
    }   /* endof while ( rank_sep < comm_world_size ) */

    /*
       Synchronize everybody in MPI_COMM_WORLD
       before broadcasting result back to everybody.
    */
    PMPI_Barrier( MPI_COMM_WORLD );

    if ( comm_world_rank == 0 )
        recv_table_count  = commset->count;
    else
        recv_table_count  = 0;
    MPI_Bcast( &recv_table_count, 1, MPI_INT, 0, MPI_COMM_WORLD );

    /* Allocate a buffer for root process's commset->table */
    recv_table_size  = recv_table_count * sizeof(CLOG_CommIDs_t);
    recv_table  = (CLOG_CommIDs_t *) MALLOC( recv_table_size );
    if ( recv_table == NULL ) {
        fprintf( stderr, __FILE__":CLOG_CommSet_merge() - \n"
                         "\t""MALLOC(%d) fails!\n", recv_table_size );
        fflush( stderr );
        CLOG_Util_abort( 1 );
    }

    if ( comm_world_rank == 0 )
        memcpy( recv_table, commset->table, recv_table_size );
    MPI_Bcast( recv_table, recv_table_size, MPI_CHAR, 0, MPI_COMM_WORLD );

    /* Update the local_ID in CLOG_CommSet_t's table to globally unique ID */
    for ( idx = 0; idx < commset->count; idx++ ) {
        commIDs  = CLOG_CommTable_get( recv_table, recv_table_count,
                                       commset->table[idx].global_ID ); 
        if ( commIDs == NULL ) {
            char uuid_str[CLOG_UUID_STR_SIZE] = {0};
            CLOG_Uuid_sprint( commset->table[idx].global_ID, uuid_str );
            fprintf( stderr, __FILE__":CLOG_CommSet_merge() - \n"
                             "\t""collected table from root does not contain "
                             "%d-th GID %s/n", idx, uuid_str );
            fflush( stderr );
            CLOG_Util_abort( 1 );
        }
        /* if commIDs != NULL */
        commset->table[idx].local_ID = commIDs->local_ID;
    }
    if ( recv_table != NULL ) {
        FREE( recv_table );
        recv_table  = NULL;
    }

    PMPI_Barrier( MPI_COMM_WORLD );
}

#endif
