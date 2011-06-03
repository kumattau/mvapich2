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

#include <string.h>
#include <stdlib.h>

// Prefix to distinguish output from different processes
#define OUTPUT_PREFIX_LENGTH 256
char output_prefix[OUTPUT_PREFIX_LENGTH] = "";

void set_output_prefix( char* prefix ) {
    strncpy( output_prefix, prefix, OUTPUT_PREFIX_LENGTH );
    output_prefix[OUTPUT_PREFIX_LENGTH-1]= '\0';
}

const char *get_output_prefix() {
    return output_prefix;
}



// Verbosity level for fork/kill/waitpid operations in mpirun_rsh and mpispawn
int DEBUG_Fork_verbose = 0;

// Verbosity level for Fault Tolerance operations (Checkpoint/Restart, Migration)
int DEBUG_FT_verbose = 0;



static inline int env2int (char *name)
{
    char* env_str = getenv( name );
    if ( env_str == NULL ) {
        return 0;
    } else {
        return atoi( env_str );
    }
}


// Initialize the verbosity level of the above variables
int initialize_debug_variables() {
    DEBUG_Fork_verbose = env2int( "MV2_DEBUG_FORK_VERBOSE" );
    DEBUG_FT_verbose = env2int( "MV2_DEBUG_FT_VERBOSE" );
    return 0;
}

