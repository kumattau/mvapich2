/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/*
 * This file provides a set of routines that can be used to record debug
 * messages in a ring so that the may be dumped at a later time.  For example,
 * this can be used to record debug messages without printing them; when
 * a special event, such as an error occurs, a call to 
 * MPIU_dump_dbg_memlog( stderr ) will print the contents of the file ring
 * to stderr.
 */
#include "mpiimpl.h"
#include <stdio.h>
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

/* Temporary.  sig values will change */
/* style: allow:vprintf:3 sig:0 */
/* style: allow:fputs:1 sig:0 */
/* style: allow:printf:2 sig:0 */
/* style: allow:fprintf:7 sig:0 */

#ifdef HAVE_VA_COPY
# define va_copy_end(a) va_end(a)
#else
# ifdef HAVE___VA_COPY
#  define va_copy(a,b) __va_copy(a,b)
#  define va_copy_end(a) 
# else
#  define va_copy(a,b) ((a) = (b))
/* Some writers recommend define va_copy(a,b) memcpy(&a,&b,sizeof(va_list)) */
#  define va_copy_end(a)
# endif
#endif

#if !defined(MPICH_DBG_MEMLOG_NUM_LINES)
#define MPICH_DBG_MEMLOG_NUM_LINES 1024
#endif
#if !defined(MPICH_DBG_MEMLOG_LINE_SIZE)
#define MPICH_DBG_MEMLOG_LINE_SIZE 256
#endif

MPIU_dbg_state_t MPIUI_dbg_state = MPIU_DBG_STATE_UNINIT;
FILE * MPIUI_dbg_fp = NULL;
static int dbg_memlog_num_lines = MPICH_DBG_MEMLOG_NUM_LINES;
static int dbg_memlog_line_size = MPICH_DBG_MEMLOG_LINE_SIZE;
static char **dbg_memlog = NULL;
static int dbg_memlog_next = 0;
static int dbg_memlog_count = 0;
static int dbg_rank = -1;

static void dbg_init(void);

int MPIU_dbg_init(int rank)
{
    dbg_rank = rank;

    if (MPIUI_dbg_state == MPIU_DBG_STATE_UNINIT)
    {
	dbg_init();
    }

    /* If file logging is enable, we need to open a file */
    if (MPIUI_dbg_state & MPIU_DBG_STATE_FILE)
    {
	char fn[128];

	/* Only open the file only once in case MPIU_dbg_init is called more 
	   than once */
	if (MPIUI_dbg_fp == NULL)
	{
	    MPIU_Snprintf(fn, 128, "mpich2-dbg-%d.log", dbg_rank);
	    MPIUI_dbg_fp = fopen(fn, "w");
	    setvbuf(MPIUI_dbg_fp, NULL, _IONBF, 0);
	}
    }
    
    return 0;
}

static void dbg_init(void)
{
    char * envstr;
    
    MPIUI_dbg_state = MPIU_DBG_STATE_NONE;

    /* FIXME: This should use MPIU_Param_get_string */
    envstr = getenv("MPICH_DBG_OUTPUT");
    if (envstr == NULL)
    {
	return;
    }

    /*
     * TODO:
     *
     * - parse environment variable to determine number of log lines, etc.
     *
     * - add support for writing to a (per-process or global?) file
     *
     * - add support for sending to a log server, perhaps with global time
     *   sequencing information ???
     */
    if (strstr(envstr, "stdout"))
    {
	MPIUI_dbg_state = (MPIU_dbg_state_t)( MPIU_DBG_STATE_STDOUT | 
					      MPIUI_dbg_state );
    }
    if (strstr(envstr, "memlog"))
    {
	MPIUI_dbg_state = (MPIU_dbg_state_t)( MPIU_DBG_STATE_MEMLOG |
					      MPIUI_dbg_state );
    }
    if (strstr(envstr, "file"))
    {
	MPIUI_dbg_state = (MPIU_dbg_state_t) ( MPIU_DBG_STATE_FILE |
					       MPIUI_dbg_state );
    }

    /* If memlog is enabled, the we need to allocate some memory for it */
    if (MPIUI_dbg_state & MPIU_DBG_STATE_MEMLOG)
    {
	dbg_memlog = MPIU_Malloc(dbg_memlog_num_lines * sizeof(char *) +
				 dbg_memlog_num_lines * dbg_memlog_line_size);
	if (dbg_memlog != NULL)
	{
	    int i;
	    
	    for (i = 0; i < dbg_memlog_num_lines ; i++)
	    {
		dbg_memlog[i] = ((char *) &dbg_memlog[dbg_memlog_num_lines]) + 
		    i * dbg_memlog_line_size;
	    }
	}
	else
	{
	    MPIUI_dbg_state = (MPIU_dbg_state_t)( MPIUI_dbg_state & 
						  ~MPIU_DBG_STATE_MEMLOG );
	}
    }
}

int MPIU_dbglog_printf(const char *str, ...)
{
    int n = 0;
    va_list list;

    if (MPIUI_dbg_state == MPIU_DBG_STATE_UNINIT)
    {
	dbg_init();
    }

    if (MPIUI_dbg_state & MPIU_DBG_STATE_MEMLOG)
    {
	/* FIXME: put everything on one line until a \n is found */
	
	dbg_memlog[dbg_memlog_next][0] = '\0';
	va_start(list, str);
	n = vsnprintf(dbg_memlog[dbg_memlog_next], dbg_memlog_line_size, str, 
		      list);
	va_end(list);

	/* if the output was truncated, we null terminate the end of the
	   string, on the off chance that vsnprintf() didn't do that.  we also
	   check to see if any data has been written over the null we set at
	   the beginning of the string.  this is mostly paranoia, but the man
	   page does not clearly state what happens when truncation occurs.  if
	   data was written to the string, we would like to output it, but we
	   want to avoid reading past the end of the array or outputing garbage
	   data. */

	if (n < 0 || n >= dbg_memlog_line_size)
	{
	    dbg_memlog[dbg_memlog_next][dbg_memlog_line_size - 1] = '\0';
	    n = (int)strlen(dbg_memlog[dbg_memlog_next]);
	}

	if (dbg_memlog[dbg_memlog_next][0] != '\0')
	{
	    dbg_memlog_next = (dbg_memlog_next + 1) % dbg_memlog_num_lines;
	    dbg_memlog_count++;
	}
    }

    if (MPIUI_dbg_state & MPIU_DBG_STATE_STDOUT)
    {
	va_start(list, str);
	n = vprintf(str, list);
	va_end(list);
    }

    if ((MPIUI_dbg_state & MPIU_DBG_STATE_FILE) && MPIUI_dbg_fp != NULL)
    {
	va_start(list, str);
	n = vfprintf(MPIUI_dbg_fp, str, list);
	va_end(list);
    }

    return n;
}

int MPIU_dbglog_vprintf(const char *str, va_list ap)
{
    int n = 0;
    va_list list;

    if (MPIUI_dbg_state == MPIU_DBG_STATE_UNINIT)
    {
	dbg_init();
    }

    if (MPIUI_dbg_state & MPIU_DBG_STATE_MEMLOG)
    {
	va_copy(list,ap);
	dbg_memlog[dbg_memlog_next][0] = '\0';
	n = vsnprintf(dbg_memlog[dbg_memlog_next], dbg_memlog_line_size, str, 
		      list);
        va_copy_end(list);

	/* if the output was truncated, we null terminate the end of the
	   string, on the off chance that vsnprintf() didn't do that.  we also
	   check to see if any data has been written over the null we set at
	   the beginning of the string.  this is mostly paranoia, but the man
	   page does not clearly state what happens when truncation occurs.  if
	   data was written to the string, we would like to output it, but we
	   want to avoid reading past the end of the array or outputing garbage
	   data. */

	if (n < 0 || n >= dbg_memlog_line_size)
	{
	    dbg_memlog[dbg_memlog_next][dbg_memlog_line_size - 1] = '\0';
	    n = (int)strlen(dbg_memlog[dbg_memlog_next]);
	}

	if (dbg_memlog[dbg_memlog_next][0] != '\0')
	{
	    dbg_memlog_next = (dbg_memlog_next + 1) % dbg_memlog_num_lines;
	    dbg_memlog_count++;
	}
    }

    if (MPIUI_dbg_state & MPIU_DBG_STATE_STDOUT)
    {
	va_copy(list, ap);
	n = vprintf(str, list);
	va_copy_end(list);
    }

    if ((MPIUI_dbg_state & MPIU_DBG_STATE_FILE) && MPIUI_dbg_fp != NULL)
    {
	va_copy(list, ap);
	n = vfprintf(MPIUI_dbg_fp, str, list);
	va_end(list);
    }

    return n;
}

/* FIXME: */
int MPIU_dbg_printf(const char * str, ...)
{
    int n;
    
    /* MPID_Common_thread_lock(); */
    {
	va_list list;

	MPIU_dbglog_printf("[%d]", dbg_rank);
	va_start(list, str);
	n = MPIU_dbglog_vprintf(str, list);
	va_end(list);
	MPIU_dbglog_flush();
    }
    /* MPID_Common_thread_unlock(); */
    
    return n;
}

void MPIU_dump_dbg_memlog_to_stdout(void)
{
    MPIU_dump_dbg_memlog(stdout);
}

void MPIU_dump_dbg_memlog_to_file(const char *filename)
{
    FILE *fout;
    fout = fopen(filename, "wb");
    if (fout != NULL)
    {
	MPIU_dump_dbg_memlog(fout);
	fclose(fout);
    }
}

void MPIU_dump_dbg_memlog(FILE * fp)
{
    if (dbg_memlog_count != 0)
    {
	int ent;
	int last_ent;

	/* there is a small issue with counter rollover which will need to be
	   fixed if more than 2^32 lines are going to be logged */
	ent = (dbg_memlog_next == dbg_memlog_count) ? 0 : dbg_memlog_next;
	last_ent = (ent + dbg_memlog_num_lines - 1) % dbg_memlog_num_lines;
	
	do
	{
	    fputs(dbg_memlog[ent], fp);
	    ent = (ent + 1) % dbg_memlog_num_lines;
	}
	while(ent != last_ent);
	fflush(fp);
    }
}

#ifdef USE_DBG_LOGGING
/* 
 * NEW ROUTINES FOR DEBUGGING
 */
int MPIU_DBG_ActiveClasses = 0;
int MPIU_DBG_MaxLevel      = MPIU_DBG_TYPICAL;
static int mpiu_dbg_initialized = 0;  /* 0, 1 (preinit), or 2 (init) */
static FILE *MPIU_DBG_fp = 0;
static char *filePattern = "-stdout-"; /* "log%d.log"; */
static char *defaultFilePattern = "dbg@W%w-@%d@T-%t@.log";
static int worldNum  = 0;
static int worldRank = -1;
static int whichRank = -1;             /* all ranks */
static int threadID  = 0;
static double timeOrigin = 0.0;

static int MPIU_DBG_Usage( const char *, const char * );
static int MPIU_DBG_OpenFile( void );
static int setDBGClass( const char * );
static int SetDBGLevel( const char *, const char *([]) );

int MPIU_DBG_Outevent( const char *file, int line, int class, int kind, 
		       const char *fmat, ... )
{
    va_list list;
    char *str, stmp[MPIU_DBG_MAXLINE];
    int  i;
    void *p;
    MPID_Time_t t;
    double  curtime;

    if (!mpiu_dbg_initialized) return 0;

#if MPICH_IS_THREADED
    {
	MPE_Thread_id_t tid;
	MPE_Thread_self(&tid);
	threadID = (int)tid;
    }
#endif
    if (!MPIU_DBG_fp) {
	MPIU_DBG_OpenFile();
    }

    MPID_Wtime( &t );
    MPID_Wtime_todouble( &t, &curtime );
    curtime = curtime - timeOrigin;

    /* The kind values are used with the macros to simplify these cases */
    switch (kind) {
	case 0:
	    fprintf( MPIU_DBG_fp, "%d\t%d\t%d\t%d\t%f\t%s\t%d\t%s\n",
		     worldNum, worldRank, threadID, class, curtime, 
		     file, line, fmat );
	    break;
	case 1:
	    va_start(list,fmat);
	    str = va_arg(list,char *);
	    MPIU_Snprintf( stmp, sizeof(stmp), fmat, str );
	    va_end(list);
	    fprintf( MPIU_DBG_fp, "%d\t%d\t%d\t%d\t%f\t%s\t%d\t%s\n",
		     worldNum, worldRank, threadID, class, curtime, 
		     file, line, stmp );
	    break;
	case 2: 
	    va_start(list,fmat);
	    i = va_arg(list,int);
	    MPIU_Snprintf( stmp, sizeof(stmp), fmat, i);
	    va_end(list);
	    fprintf( MPIU_DBG_fp, "%d\t%d\t%d\t%d\t%f\t%s\t%d\t%s\n",
		     worldNum, worldRank, threadID, class, curtime, 
		     file, line, stmp );
	    break;
	case 3: 
	    va_start(list,fmat);
	    p = va_arg(list,void *);
	    MPIU_Snprintf( stmp, sizeof(stmp), fmat, p);
	    va_end(list);
	    fprintf( MPIU_DBG_fp, "%d\t%d\t%d\t%d\t%f\t%s\t%d\t%s\n",
		     worldNum, worldRank, threadID, class, curtime, 
		     file, line, stmp );
	    break;
        default:
	    break;
    }
    fflush(MPIU_DBG_fp);
    return 0;
}

/* These are used to simplify the handling of options.  
   To add a new name, add an MPIU_DBG_ClassName element to the array
   MPIU_Classnames.  The "classbits" values are defined by MPIU_DBG_CLASS
   in src/include/mpidbg.h 
 */

typedef struct MPIU_DBG_ClassName {
    int        classbits;
    const char *UCName, *LCName; 
} MPIU_DBG_ClassName;

static const MPIU_DBG_ClassName MPIU_Classnames[] = {
    { MPIU_DBG_PT2PT,         "PT2PT",         "pt2pt" },
    { MPIU_DBG_RMA,           "RMA",           "rma"   },
    { MPIU_DBG_THREAD,        "THREAD",        "thread" },
    { MPIU_DBG_PM,            "PM",            "pm" },
    { MPIU_DBG_ROUTINE_ENTER, "ROUTINE_ENTER", "routine_enter" },
    { MPIU_DBG_ROUTINE_EXIT,  "ROUTINE_EXIT",  "routine_exit" },
    { MPIU_DBG_ROUTINE_ENTER |
      MPIU_DBG_ROUTINE_EXIT,  "ROUTINE",       "routine" },
    { MPIU_DBG_SYSCALL,       "SYSCALL",       "syscall" },
    { MPIU_DBG_DATATYPE,      "DATATYPE",      "datatype" },
    { MPIU_DBG_HANDLE,        "HANDLE",        "handle" },
    { MPIU_DBG_COMM,          "COMM",          "comm" },
    { MPIU_DBG_BSEND,         "BSEND",         "bsend" },
    { MPIU_DBG_OTHER,         "OTHER",         "other" },
    { MPIU_DBG_CH3_CONNECT,   "CH3_CONNECT",   "ch3_connect" },
    { MPIU_DBG_CH3_DISCONNECT,"CH3_DISCONNECT","ch3_disconnect" },
    { MPIU_DBG_CH3_PROGRESS,  "CH3_PROGRESS",  "ch3_progress" },
    { MPIU_DBG_CH3_CHANNEL,   "CH3_CHANNEL",   "ch3_channel" },
    { MPIU_DBG_CH3_OTHER,     "CH3_OTHER",     "ch3_other" },
    { MPIU_DBG_CH3,           "CH3",           "ch3" },
    { MPIU_DBG_NEM_SOCK_FUNC, "NEM_SOCK_FUNC", "nem_sock_func"},
    { MPIU_DBG_NEM_SOCK_DET,  "NEM_SOCK_DET",  "nem_sock_det"},
    { MPIU_DBG_VC,            "VC",            "vc"},
    { MPIU_DBG_REFCOUNT,      "REFCOUNT",      "refcount"},
    { MPIU_DBG_ALL,           "ALL",           "all" }, 
    { 0,                      0,               0 }
};

/* Because the level values are simpler and are rarely changed, these
   use a simple set of parallel arrays */
static const int  MPIU_Levelvalues[] = { MPIU_DBG_TERSE,
					 MPIU_DBG_TYPICAL,
					 MPIU_DBG_VERBOSE, 100 };
static const char *MPIU_Levelname[] = { "TERSE", "TYPICAL", "VERBOSE", 0 };
static const char *MPIU_LCLevelname[] = { "terse", "typical", "verbose", 0 };

/* 
 * Initialize the DBG_MSG system.  This is called during MPI_Init to process
 * command-line arguments as well as checking the MPICH_DBG environment
 * variables.  The initialization is split into two steps: a preinit and an 
 * init. This makes it possible to enable most of the features before calling 
 * MPID_Init, where a significant amount of the initialization takes place.
 */

static int MPIU_DBG_ProcessArgs( int *argc_p, char ***argv_p )
{
    int i, rc;

    /* Here's where we do the same thing with the command-line options */
    if (argc_p) {
	for (i=1; i<*argc_p; i++) {
	    if (strncmp((*argv_p)[i],"-mpich-dbg", 10) == 0) {
		char *s = (*argv_p)[i] + 10;
		/* Found a command */
		if (*s == 0) {
		    /* Just -mpich-dbg */
		    MPIU_DBG_MaxLevel      = MPIU_DBG_TYPICAL;
		    MPIU_DBG_ActiveClasses = MPIU_DBG_ALL;
		}
		else if (*s == '=') {
		    /* look for file */
		    MPIU_DBG_MaxLevel      = MPIU_DBG_TYPICAL;
		    MPIU_DBG_ActiveClasses = MPIU_DBG_ALL;
		    s++;
		    if (strncmp( s, "file", 4 ) == 0) {
			filePattern = defaultFilePattern;
		    }
		}
		else if (strncmp(s,"-level",6) == 0) {
		    char *p = s + 6;
		    if (*p == '=') {
			p++;
			rc = SetDBGLevel( p, MPIU_LCLevelname );
			if (rc) 
			    MPIU_DBG_Usage( "-mpich-dbg-level", "terse, typical, verbose" );
		    }
		}
		else if (strncmp(s,"-class",6) == 0) {
		    char *p = s + 6;
		    if (*p == '=') {
			p++;
			rc = setDBGClass( p );
			if (rc)
			    MPIU_DBG_Usage( "-mpich-dbg-class", 0 );
		    }
		}
		else if (strncmp( s, "-filename", 9 ) == 0) {
		    char *p = s + 9;
		    if (*p == '=') {
			p++;
			filePattern = MPIU_Strdup( p );
		    }
		}
		else if (strncmp( s, "-rank", 5 ) == 0) {
		    char *p = s + 5;
		    if (*p == '=' && p[1] != 0) {
			char *sOut;
			p++;
			whichRank = strtol( p, &sOut, 10 );
			if (p == sOut) {
			    MPIU_DBG_Usage( "-mpich-dbg-rank", 0 );
			    whichRank = -1;
			}
		    }
		}
		else {
		    MPIU_DBG_Usage( (*argv_p)[i], 0 );
		}
		
		/* Eventually, should null it out and reduce argc value */
	    }
	}
    }
    return MPI_SUCCESS;
}

static int MPIU_DBG_ProcessEnv( void )
{
    char *s;
    int rc;

    s = getenv( "MPICH_DBG" );
    if (s) {
	/* Set the defaults */
	MPIU_DBG_MaxLevel = MPIU_DBG_TYPICAL;
	MPIU_DBG_ActiveClasses = MPIU_DBG_ALL;
	if (strncmp(s,"FILE",4) == 0) {
	    filePattern = defaultFilePattern;
	}
    }
    s = getenv( "MPICH_DBG_LEVEL" );
    if (s) {
	rc = SetDBGLevel( s, MPIU_Levelname );
	if (rc) 
	    MPIU_DBG_Usage( "MPICH_DBG_LEVEL", "TERSE, TYPICAL, VERBOSE" );
    }

    s = getenv( "MPICH_DBG_CLASS" );
    rc = setDBGClass( s );
    if (rc) 
	MPIU_DBG_Usage( "MPICH_DBG_CLASS", 0 );

    s = getenv( "MPICH_DBG_FILENAME" );
    if (s) {
	filePattern = MPIU_Strdup( s );
    }

    s = getenv( "MPICH_DBG_RANK" );
    if (s) {
	char *sOut;
	whichRank = strtol( s, &sOut, 10 );
	if (s == sOut) {
	    MPIU_DBG_Usage( "MPICH_DBG_RANK", 0 );
	    whichRank = -1;
	}
    }
    return MPI_SUCCESS;
}

/*
 * Attempt to initialize the logging system.  This works only if MPID_Init
 * is not responsible for updating the environment and/or command-line
 * arguments. 
 */
int MPIU_DBG_PreInit( int *argc_p, char ***argv_p )
{
    MPID_Time_t t;

    /* if the DBG_MSG system was already initialized, say by the device, then
       return immediately */
    if (mpiu_dbg_initialized != 0) return 0;

    /* Check to see if any debugging was selected.  The order of these
       tests is important, as they allow general defaults to be set,
       followed by more specific modifications */
    /* First, the environment variables */
    MPIU_DBG_ProcessEnv();

    MPIU_DBG_ProcessArgs( argc_p, argv_p );

    MPID_Wtime( &t );
    MPID_Wtime_todouble( &t, &timeOrigin );

    mpiu_dbg_initialized = 1;

    return MPI_SUCCESS;
}

int MPIU_DBG_Init( int *argc_p, char ***argv_p, int has_args, int has_env, 
		   int wrank )
{
    /* if the DBG_MSG system was already initialized, say by the device, then
       return immediately */
    if (mpiu_dbg_initialized == 2) return 0;

    /* Check to see if any debugging was selected.  The order of these
       tests is important, as they allow general defaults to be set,
       followed by more specific modifications. */
    /* Both of these may have already been set in the PreInit call; 
       if the command line and/or environment variables are set before
       MPID_Init, then don't call the routines to check those values 
       (as they were already handled in DBG_PreInit) */
    /* First, the environment variables */
    if (!has_env) 
	MPIU_DBG_ProcessEnv();
    /* Now the command-line arguments */
    if (!has_args) 
	MPIU_DBG_ProcessArgs( argc_p, argv_p );

    worldRank = wrank;

    if (whichRank >= 0 && whichRank != wrank) {
	/* Turn off logging on this process */
	MPIU_DBG_ActiveClasses = 0;
    }

    mpiu_dbg_initialized = 2;
    return 0;
}

/* Print the usage statement to stderr */
static int MPIU_DBG_Usage( const char *cmd, const char *vals )
{
    if (vals) {
	fprintf( stderr, "Incorrect value for %s, should be one of %s\n",
		 cmd, vals );
    }
    else {
	fprintf( stderr, "Incorrect value for %s\n", cmd );
    }
    fprintf( stderr, 
"Command line for debug switches\n\
    -mpich-dbg-class=name[,name,...]\n\
    -mpich-dbg-level=name   (one of terse, typical, verbose)\n\
    -mpich-dbg-filename=pattern (includes %%d for world rank, %%t for thread id\n\
    -mpich-dbg-rank=val    (only this rank in COMM_WORLD will be logged)\n\
    -mpich-dbg   (shorthand for -mpich-dbg-class=all -mpich-dbg-level=typical)\n\
    -mpich-dbg=file (shorthand for -mpich-dbg -mpich-dbg-filename=%s)\n\
Environment variables\n\
    MPICH_DBG_CLASS=NAME[,NAME...]\n\
    MPICH_DBG_LEVEL=NAME\n\
    MPICH_DBG_FILENAME=pattern\n\
    MPICH_DBG_RANK=val\n\
    MPICH_DBG=YES or FILE\n", defaultFilePattern );

    fflush(stderr);

    return 0;
}
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

/* This routine can make no MPI calls, since it may be logging those
   calls. */
static int MPIU_DBG_OpenFile( void )
{
    int withinMworld = 0,         /* True if within an @W...@ */
	withinMthread = 0;        /* True if within an @T...@ */
    /* FIXME: Need to know how many MPI_COMM_WORLDs are known */
    int nWorld = 1;
#if MPICH_IS_THREADED
    int nThread = 2;
#else
    int nThread = 1;
#endif
    static char worldNumAsChar[10] = "0";

    /* FIXME: This is a hack to handle the common case of two worlds */
    if (MPIR_Process.comm_parent != NULL) {
	nWorld = 2;
	worldNumAsChar[0] = '1';
	worldNumAsChar[1] = '\0';
    }
	
    if (!filePattern || *filePattern == 0 ||
	strcmp(filePattern, "-stdout-" ) == 0) {
	MPIU_DBG_fp = stdout;
    }
    else if (strcmp( filePattern, "-stderr-" ) == 0) {
	MPIU_DBG_fp = stderr;
    }
    else {
	char filename[MAXPATHLEN], *pDest, *p;
	p     = filePattern;
	pDest = filename;
	*filename = 0;
	while (*p && (pDest-filename) < MAXPATHLEN) {
	    /* There are two special cases that allow text to
	       be optionally included.  Those patterns are
	       @T...@ (only if multi-threaded) and
	       @W...@ (only if more than one MPI_COMM_WORLD) 
	       UNIMPLEMENTED/UNTESTED */
	    if (*p == '@') {
		/* Escaped @? */
		if (p[1] == '@') {
		    *pDest++ = *++p;
		    continue;
		}
		/* If within an @...@, terminate it */
		if (withinMworld) {
		    withinMworld = 0;
		    p++;
		}
		else if (withinMthread) {
		    withinMthread = 0;
		    p++;
		}
		else {
		    /* Look for command */
		    p++;
		    if (*p == 'W') {
			p++;
			withinMworld = 1;
		    }
		    else if (*p == 'T') {
			p++;
			withinMthread = 1;
		    }
		    else {
			/* Unrecognized char */
			*pDest++ = *p++;
		    }
		}
	    }
	    else if ( (withinMworld && nWorld == 1) ||
		      (withinMthread && nThread == 1) ) {
		/* Simply skip this character since we're not showing
		   this string */
		p++;
	    }
	    else if (*p == '%') {
		p++;
		if (*p == 'd') {
		    char rankAsChar[20];
		    MPIU_Snprintf( rankAsChar, sizeof(rankAsChar), "%d", 
				   worldRank );
		    *pDest = 0;
		    MPIU_Strnapp( filename, rankAsChar, MAXPATHLEN );
		    pDest += strlen(rankAsChar);
		}
		else if (*p == 't') {
#if MPICH_IS_THREADED
		    char threadIDAsChar[20];
		    MPE_Thread_id_t tid;
		    MPE_Thread_self(&tid);
		    threadID = (int)tid;

		    MPIU_Snprintf( threadIDAsChar, sizeof(threadIDAsChar), 
				   "%d", threadID );
		    *pDest = 0;
		    MPIU_Strnapp( filename, threadIDAsChar, MAXPATHLEN );
		    pDest += strlen(threadIDAsChar);
#else
		    *pDest++ = '0';
#endif
		}
		else if (*p == 'w') {
		    /* FIXME: Get world number */
		    /* *pDest++ = '0'; */
		    *pDest = 0;
		    MPIU_Strnapp( filename, worldNumAsChar, MAXPATHLEN );
		    pDest += strlen(worldNumAsChar);
		}
		else {
		    *pDest++ = '%';
		    *pDest++ = *p;
		}
		p++;
	    }
	    else {
		*pDest++ = *p++;
	    }
	}
	*pDest = 0;
	MPIU_DBG_fp = fopen( filename, "w" );
	if (!MPIU_DBG_fp) {
	    MPIU_Error_printf( "Could not open log file %s\n", filename );
	}
    }
    return 0;
}

/* Support routines for processing mpich-dbg values */
/* Update the GLOBAL variable MPIU_DBG_ActiveClasses with
   the bits corresponding to this name */
static int setDBGClass( const char *s )
{
    int i;
    int slen = 0;
    int len = 0;
	    
    if (s && *s) slen = strlen(s);

    while (s && *s) {
	for (i=0; MPIU_Classnames[i].LCName; i++) {
	    /* The LCLen and UCLen *should* be the same, but
	       just in case, we separate them */
	    int LClen = strlen(MPIU_Classnames[i].LCName);
	    int UClen = strlen(MPIU_Classnames[i].UCName);
	    int matchClass = 0;

	    /* Allow the upper case and lower case in all cases */
	    if (slen >= LClen && 
		strncmp(s,MPIU_Classnames[i].LCName, LClen) == 0 &&
		(s[LClen] == ',' || s[LClen] == 0) ) {
		matchClass = 1;
		len = LClen;
	    }
	    else if (slen >= UClen && 
		strncmp(s,MPIU_Classnames[i].UCName, UClen) == 0 &&
		(s[UClen] == ',' || s[UClen] == 0) ) {
		matchClass = 1;
		len = UClen;
	    }
	    if (matchClass) {
		MPIU_DBG_ActiveClasses |= MPIU_Classnames[i].classbits;
		s += len;
		slen -= len;
		if (*s == ',') { s++; slen--; }
		/* If we found a name, we need to restart the for loop */
		break;
	    }
	}
	if (!MPIU_Classnames[i].LCName) {
	    return 1;
	}
    }
    return 0;
}

/* Set the global MPIU_DBG_MaxLevel if there is a match with the known level
   names 
*/
static int SetDBGLevel( const char *s, const char *(names[]) )
{
    int i;

    for (i=0; names[i]; i++) {
	if (strcmp( names[i], s ) == 0) {
	    MPIU_DBG_MaxLevel = MPIU_Levelvalues[i];
	    return 0;
	}
    }
    return 1;
}
#endif /* USE_DBG_LOGGING */
