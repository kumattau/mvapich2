/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: simple_pmiutil.h,v 1.5 2006/11/01 15:13:44 gropp Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* maximum sizes for arrays */
#define PMIU_MAXLINE 1024
#define PMIU_IDSIZE    32

/* prototypes for PMIU routines */
void PMIU_Set_rank( int PMI_rank );
void PMIU_SetServer( void );
void PMIU_printf( int print_flag, char *fmt, ... );
int  PMIU_readline( int fd, char *buf, int max );
int  PMIU_writeline( int fd, char *buf );
int  PMIU_parse_keyvals( char *st );
void PMIU_dump_keyvals( void );
char *PMIU_getval( const char *keystr, char *valstr, int vallen );
void PMIU_chgval( const char *keystr, char *valstr );
