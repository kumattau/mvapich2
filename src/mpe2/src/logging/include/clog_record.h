/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#if !defined( _CLOG_RECORD )
#define _CLOG_RECORD

#include "clog_block.h"
#include "clog_timer.h"
#include "clog_commset.h"

/*
   We distinguish between record types and event types (kinds), and have a
   small number of pre-defined record types, including a raw one.  We keep all
   records double-aligned for the sake of the double timestamp field.  Lengths
   are given in doubles.  Log records will usually consist of a
   CLOG_Rec_header_t followed by one of the types that follow it below,
   but record types CLOG_Rec_endblock_t and CLOG_Rec_endlog_t consist of
   the header alone.
*/

/*
   Whenever fields are add/deleted or padding is added or made meaningful,
   synchronize the changes with all CLOG_Rec_xxx_swap_bytes() routines.
*/
typedef struct {
    CLOG_Time_t       time;    /* Crucial to be the 1st item */
    CLOG_CommLID_t    icomm;   /* LOCAL intracomm's internal local/global ID */
    int               rank;    /* rank within communicator labelled by icomm */
    int               thread;  /* local thread ID */
    int               rectype;
    CLOG_DataUnit_t   rest[1];
} CLOG_Rec_Header_t;
/*  3 CLOG_Time_t's */
#define CLOG_RECLEN_HEADER     ( sizeof(CLOG_Time_t) + 4 * sizeof(int) )

typedef char CLOG_Str_Color_t[ 3 * sizeof(CLOG_Time_t) ];
typedef char CLOG_Str_Desc_t[ 4 * sizeof(CLOG_Time_t) ];
typedef char CLOG_Str_File_t[ 5 * sizeof(CLOG_Time_t) ];
typedef char CLOG_Str_Format_t[ 5 * sizeof(CLOG_Time_t) ];
typedef char CLOG_Str_Bytes_t[ 4 * sizeof(CLOG_Time_t) ];

/* Rec_StateDef defines the attributes of a state which consists of 2 events */
typedef struct {
    int               stateID;   /* integer identifier for state */
    int               startetype;/* beginning event for state */
    int               finaletype;/* ending event for state */
    int               pad;
    CLOG_Str_Color_t  color;     /* string for color */
    CLOG_Str_Desc_t   name;      /* string describing state */
    CLOG_Str_Format_t format;    /* format string for state's decription data */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_StateDef_t;
/* 14 CLOG_Time_t's */
#define CLOG_RECLEN_STATEDEF   ( 4 * sizeof(int) \
                               + sizeof(CLOG_Str_Color_t) \
                               + sizeof(CLOG_Str_Desc_t) \
                               + sizeof(CLOG_Str_Format_t) )

/* Rec_EventDef defines the attributes of a event */
typedef struct {
    int               etype;     /* event ID */
    int               pad;
    CLOG_Str_Color_t  color;     /* string for color */
    CLOG_Str_Desc_t   name;      /* string describing event */
    CLOG_Str_Format_t format;    /* format string for the event */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_EventDef_t;
/* 13 CLOG_Time_t's */
#define CLOG_RECLEN_EVENTDEF   ( 2 * sizeof(int) \
                               + sizeof(CLOG_Str_Color_t) \
                               + sizeof(CLOG_Str_Desc_t) \
                               + sizeof(CLOG_Str_Format_t) )

/* Rec_Const defines some specific constant of the logfile */
typedef struct {
    int               etype;     /* raw event */
    int               value;     /* uninterpreted data */
    CLOG_Str_Desc_t   name;      /* uninterpreted string */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_ConstDef_t;
/*  5 CLOG_Time_t's */
#define CLOG_RECLEN_CONSTDEF   ( 2 * sizeof(int) \
                               + sizeof(CLOG_Str_Desc_t) )

/* Rec_BareEvt defines the simplest event, i.e. bare */
typedef struct {
    int               etype;     /* basic event */
    int               pad;
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_BareEvt_t;
/*  1 CLOG_Time_t's */
#define CLOG_RECLEN_BAREEVT    ( 2 * sizeof(int) )

/* Rec_CargoEvt defines a event with a payload */
typedef struct {
    int               etype;     /* basic event */
    int               pad;
    CLOG_Str_Bytes_t  bytes;     /* byte storage */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_CargoEvt_t;
/*  5 CLOG_Time_t's */
#define CLOG_RECLEN_CARGOEVT   ( 2 * sizeof(int) \
                               + sizeof(CLOG_Str_Bytes_t) )

/* Rec_MsgEvt defines a message event pairs */
typedef struct {
    int               etype;   /* kind of message event */
    CLOG_CommLID_t    icomm;   /* REMOTE intracomm's internal local/global ID */
    int               rank;    /* src/dest rank in send/recv in REMOTE icomm */
    int               tag;     /* message tag */
    int               size;    /* length in bytes */
    int               pad;
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_MsgEvt_t;
/*  2 CLOG_Time_t's */
#define CLOG_RECLEN_MSGEVT        ( 6 * sizeof(int) )

/* Rec_CollEvt defines events for collective operation */
typedef struct {
    int               etype;   /* type of collective event */
    int               root;    /* root of collective op */
    int               size;    /* length in bytes */
    int               pad;
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_CollEvt_t;
/*  2 CLOG_Time_t's */
#define CLOG_RECLEN_COLLEVT    ( 4 * sizeof(int) )

/* Rec_CommEvt defines events for {Intra|Inter}Communicator */
/* Assume CLOG_CommGID_t is of size of multiple of sizeof(double) */
typedef struct {
    int               etype;      /* type of communicator creation */
    CLOG_CommLID_t    icomm;      /* communicator's internal local/global ID */
    int               rank;       /* rank in icomm */
    int               wrank;      /* rank in MPI_COMM_WORLD */
    CLOG_CommGID_t    gcomm;      /* globally unique ID */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_CommEvt_t;
/*  2 CLOG_Time_t's */
#define CLOG_RECLEN_COMMEVT    ( 4 * sizeof(int) + CLOG_UUID_SIZE )

typedef struct {
    int               srcloc;    /* id of source location */
    int               lineno;    /* line number in source file */
    CLOG_Str_File_t   filename;  /* source file of log statement */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_Srcloc_t;
/*  6 CLOG_Time_t's */
#define CLOG_RECLEN_SRCLOC     ( 2 * sizeof(int) \
                               + sizeof(CLOG_Str_File_t) )

typedef struct {
    CLOG_Time_t       timeshift; /* time shift for this process */
    CLOG_DataUnit_t   end[1];
} CLOG_Rec_Timeshift_t;
/*  1 CLOG_Time_t's */
#define CLOG_RECLEN_TIMESHIFT  ( sizeof(CLOG_Time_t) )

/*
   Total number of CLOG_REC_XXX listed below,
   starting from 0 to CLOG_REC_NUM, excluding CLOG_REC_UNDEF
*/
#define CLOG_REC_NUM         12

/* predefined record types (all include header) */
#define CLOG_REC_UNDEF       -1     /* undefined */
#define CLOG_REC_ENDLOG       0     /* end of log marker */
#define CLOG_REC_ENDBLOCK     1     /* end of block marker */
#define CLOG_REC_STATEDEF     2     /* state description */
#define CLOG_REC_EVENTDEF     3     /* event description */
#define CLOG_REC_CONSTDEF     4     /* constant description */
#define CLOG_REC_BAREEVT      5     /* arbitrary record */
#define CLOG_REC_CARGOEVT     6     /* arbitrary record */
#define CLOG_REC_MSGEVT       7     /* message event */
#define CLOG_REC_COLLEVT      8     /* collective event */
#define CLOG_REC_COMMEVT      9     /* communicator construction/destruction  */
#define CLOG_REC_SRCLOC      10     /* identifier of location in source */
#define CLOG_REC_TIMESHIFT   11     /* time shift calculated for this process */

/*
   Be sure NO CLOG internal states are overlapped with ANY of MPI states
   and MPE's internal states which are defined in log_mpi_core.c.
*/
/*
   280 <= CLOG's internal stateID < 300 = MPE_MAX_KNOWN_STATES
   whose start_evtID = 2 * stateID,  final_evtID = 2 * stateID + 1

   This is done so the CLOG internal stateID/evetIDs are included in
   the clog2TOslog2's predefined MPI uninitialized states.

   CLOG internal state, CLOG_Buffer_write2disk
*/
#define CLOG_STATEID_BUFFERWRITE     280
#define CLOG_EVT_BUFFERWRITE_START   560
#define CLOG_EVT_BUFFERWRITE_FINAL   561

/* special event type for message type events */
#define CLOG_EVT_SENDMSG   -101
#define CLOG_EVT_RECVMSG   -102

/* special event type for defining constants */
#define CLOG_EVT_CONST     -201

void CLOG_Rec_print_rectype( int rectype, FILE *stream );
void CLOG_Rec_print_msgtype( int etype, FILE *stream );
void CLOG_Rec_print_commtype( int etype, FILE *stream );
void CLOG_Rec_print_colltype( int etype, FILE *stream );

void CLOG_Rec_Header_swap_bytes( CLOG_Rec_Header_t *hdr );
void CLOG_Rec_Header_print( CLOG_Rec_Header_t *hdr, FILE *stream );

void CLOG_Rec_StateDef_swap_bytes( CLOG_Rec_StateDef_t *statedef );
void CLOG_Rec_StateDef_print( CLOG_Rec_StateDef_t *statedef, FILE *stream );

void CLOG_Rec_EventDef_swap_bytes( CLOG_Rec_EventDef_t *eventdef );
void CLOG_Rec_EventDef_print( CLOG_Rec_EventDef_t *eventdef, FILE *stream );

void CLOG_Rec_ConstDef_swap_bytes( CLOG_Rec_ConstDef_t *constdef );
void CLOG_Rec_ConstDef_print( CLOG_Rec_ConstDef_t *constdef, FILE *stream );

void CLOG_Rec_BareEvt_swap_bytes( CLOG_Rec_BareEvt_t *bare );
void CLOG_Rec_BareEvt_print( CLOG_Rec_BareEvt_t *bare, FILE *stream );

void CLOG_Rec_CargoEvt_swap_bytes( CLOG_Rec_CargoEvt_t *cargo );
void CLOG_Rec_CargoEvt_print( CLOG_Rec_CargoEvt_t *cargo, FILE *stream );

void CLOG_Rec_MsgEvt_swap_bytes( CLOG_Rec_MsgEvt_t *msg );
void CLOG_Rec_MsgEvt_print( CLOG_Rec_MsgEvt_t *msg, FILE *stream );

void CLOG_Rec_CollEvt_swap_bytes( CLOG_Rec_CollEvt_t *coll );
void CLOG_Rec_CollEvt_print( CLOG_Rec_CollEvt_t *coll, FILE *stream );

void CLOG_Rec_CommEvt_swap_bytes( CLOG_Rec_CommEvt_t *comm );
void CLOG_Rec_CommEvt_print( CLOG_Rec_CommEvt_t *comm, FILE *stream );

void CLOG_Rec_Srcloc_swap_bytes( CLOG_Rec_Srcloc_t *src );
void CLOG_Rec_Srcloc_print( CLOG_Rec_Srcloc_t *src, FILE *stream );

void CLOG_Rec_Timeshift_swap_bytes( CLOG_Rec_Timeshift_t *tshift );
void CLOG_Rec_Timeshift_print( CLOG_Rec_Timeshift_t *tshift, FILE *stream );

void CLOG_Rec_swap_bytes_last( CLOG_Rec_Header_t *hdr );
void CLOG_Rec_swap_bytes_first( CLOG_Rec_Header_t *hdr );
void CLOG_Rec_print( CLOG_Rec_Header_t *hdr, FILE *stream );
void CLOG_Rec_sizes_init( void );
int CLOG_Rec_size( unsigned int rectype );
int CLOG_Rec_size_max( void );

#endif  /* of _CLOG_RECORD */
