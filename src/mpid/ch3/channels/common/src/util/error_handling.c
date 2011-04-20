/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <execinfo.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#define print_error(fmt, args...) \
{ fprintf( stderr, "[%s:%i] %s: "fmt, __FILE__, __LINE__, __func__ , ##args); }


#define MAX_MSG_SIZE 200
#define print_error_errno(fmt, args...) \
{ char err_msg[MAX_MSG_SIZE]; \
  int rv = strerror_r( errno, err_msg, MAX_MSG_SIZE ); \
  if ( rv != 0 ) err_msg[0] = '\0'; \
  print_error( fmt": %s (%i)\n", ##args, err_msg, errno); }


#define MAX_DEPTH 100

// Basic principle
//
// From signal(7):
// Signal such as SIGSEGV and SIGFPE, generated as a consequence 
// of executing a specific machine-language instruction are thread directed, 
// as are signals targeted at a specific thread using pthread_kill(3).
//
// It means that the signal handler will be executed in the thread 
// that caused the error. So, we can explore its stack using backtrace(3).


// Prefix to distinguish output from different processes
#define ERROR_PREFIX_LENGTH 256
char error_prefix[ERROR_PREFIX_LENGTH];
int error_prefix_length = 0;
void set_error_prefix( char* prefix ) {
    strncpy( error_prefix, prefix, ERROR_PREFIX_LENGTH);
    error_prefix[ERROR_PREFIX_LENGTH-1]= '\0';
    error_prefix_length = strlen(error_prefix);
}


// Print backtrace of the current thread
int print_backtrace()
{
    void *trace[MAX_DEPTH];
    unsigned int trace_size;
    char **trace_strings;

    // Get backtrace and symbols
    trace_size = backtrace(trace, MAX_DEPTH);
    trace_strings = backtrace_symbols(trace, trace_size);
    if ( trace_strings == NULL ) {
        print_error( "backtrace_symbols: error\n" );
        return -1;
    }

    // Print backtrace
    unsigned int i;
    for ( i = 0 ; i < trace_size ; ++i )
    {
        fprintf( stderr, "[%s] %3i: %s\n", error_prefix, i, trace_strings[i] );
    }

    // Free trace_strings allocated by backtrace_symbols()
    free(trace_strings);

    return 0;
}

// Enable/disable backtrace on error
int show_backtrace = 0;

// Signal handler for errors
void error_sighandler(int sig, siginfo_t *info, void *secret) {
    // Always print error
    fprintf( stderr, "[%s] ", error_prefix);
    psignal( sig, "Caught error");
    // Show backtrace if required
    if ( show_backtrace ) print_backtrace();
    // Raise the signal again with default handler
    raise( sig );
}

// Configure the error signal handler
int setup_error_sighandler( int backtrace ) {
    // Enable backtrace?
    show_backtrace = backtrace;

    // If not set, set null error_prefix
    if (error_prefix_length == 0) {
        set_error_prefix( "" );
    }

    // Setup the handler for all targeted errors
    struct sigaction sa;
    sa.sa_sigaction = error_sighandler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_RESETHAND;

    if ( 0 != sigaction(SIGILL , &sa, NULL) ) {
        print_error_errno( "sigaction" );
        return -1;
    }

    if ( 0 != sigaction(SIGABRT , &sa, NULL) ) {
        print_error_errno( "sigaction" );
        return -1;
    }

    if ( 0 != sigaction(SIGFPE , &sa, NULL) ) {
        print_error_errno( "sigaction" );
        return -1;
    }

    if ( 0 != sigaction(SIGSEGV , &sa, NULL) ) {
        print_error_errno( "sigaction" );
        return -1;
    }

    if ( 0 != sigaction(SIGBUS , &sa, NULL) ) {
        print_error_errno( "sigaction" );
        return -1;
    }

    return 0;
}

// Set the core dump size according to coresize parameter
int set_coresize_limit( const char* coresize )
{
    if ( coresize != NULL && strcmp( coresize, "default" ) != 0 ) {
        struct rlimit core_limit;
        int rv;
        // read current rlimit structure
        rv = getrlimit( RLIMIT_CORE, &core_limit );
        if ( rv != 0 ) {
            print_error_errno( "getrlimit" );
            return -1;
        }
        // update the rlimit structure
        if ( strcmp( coresize, "unlimited") == 0 ) {
            core_limit.rlim_cur = RLIM_INFINITY;
        } else {
            core_limit.rlim_cur = atoi( coresize );
        }
        // apply new rlimit structure
        rv = setrlimit(RLIMIT_CORE,&core_limit);
        if ( rv != 0 )
        {
            print_error_errno( "setrlimit" );
            return -1;
        }
    }
    return 0;
}

