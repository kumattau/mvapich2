/* Copyright (c) 2003-2009, The Ohio State University. All rights
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
/* -*- c -*-
 *
 * Copyright (c) 2004-2005 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007      Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef PLPA_INTERNAL_H
#define PLPA_INTERNAL_H

#include <plpa.h>

/* Have we initialized yet? */
extern int PLPA_NAME(initialized);

/* Cached size of the affinity buffers that the kernel expects */
extern size_t PLPA_NAME(len);

/* Setup topology information */
int PLPA_NAME(map_init)(void);

/* Setup API type */
int PLPA_NAME(api_probe_init)(void);

/* Map (socket,core) tuple to virtual processor ID.  processor_id is
 * then suitable for use with the PLPA_CPU_* macros, probably leading
 * to a call to plpa_sched_setaffinity().  Returns 0 upon success. */
int PLPA_NAME(map_to_processor_id)(int socket, int core, int *processor_id);

/* Map processor_id to (socket,core) tuple.  The processor_id input is
 * usually obtained from the return from the plpa_sched_getaffinity()
 * call, using PLPA_CPU_ISSET to find individual bits in the map that
 * were set/unset.  plpa_map_to_socket_core() can map the bit indexes
 * to a socket/core tuple.  Returns 0 upon success. */
int PLPA_NAME(map_to_socket_core)(int processor_id, int *socket, int *core);

/* Free all mapping memory */
void PLPA_NAME(map_finalize)(void);

/*
 * Function in flexer to set up the parser to read from a string
 * (vs. reading from a file)
 */
void parser_setup_string(char* str);

/*
 * Main bison parser.
 */
int token_parse(PLPA_NAME(cpu_set_t) *cpu_set);

/*
 *  * Main flex parser
 *   */
int mvapich_yylex(void);

#endif /* PLPA_INTERNAL_H */

