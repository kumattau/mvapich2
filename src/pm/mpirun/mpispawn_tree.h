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

/*
 * MPISPAWN INTERFACE FOR BUILDING DYNAMIC SOCKET TREES
 */
#ifndef MPISPAWN_TREE_H
#define MPISPAWN_TREE_H

#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>

typedef enum {
    MPISPAWN_MPIPROCESS_ERROR = 1,         // An MPI process got an error
    MPISPAWN_MPIPROCESS_NONZEROEXIT = 2,   // An MPI process returned a non-zero exit code
    MPISPAWN_DPM_REQ = 3,                  // DPM request
    MPISPAWN_PMI_READ_ERROR = 4,           // MPISPAWN got an error while reading a PMI socket
    MPISPAWN_PMI_WRITE_ERROR = 5,          // MPISPAWN got an error while writing a PMI socket
    MPISPAWN_INTERNAL_ERROR = 6,           // MPISPAWN got an internal error
    MPISPAWN_CLEANUP_SIGNAL = 7,           // MPISPAWN received a cleanup signal
} mpispawn_error_code;


#define MAX_HOST_LEN    256
#define MAX_PORT_LEN    8

#define MT_MAX_LEVEL    4
#define MT_MIN_DEGREE   4
#define MT_MAX_DEGREE   64

typedef struct {
    int rank;
    int fd;
    int c_barrier;
#define c_finalize c_barrier
} child_t;
#define child_s sizeof (child_t)

extern int *mpispawn_tree_init(size_t me, const size_t degree, const size_t node_count, int req_socket);
void mpispawn_abort(int code);
int mtpmi_init(void);
int mtpmi_processops(void);

#if defined(CKPT) && defined(CR_FTB)
extern volatile int cr_mig_tgt;
#endif
/*struct spawn_info_s {
    char spawnhost[32];
    int  sparenode;
};*/

#if defined(MPISPAWN_DEBUG)
#define MT_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf (stderr, "\n%s:%d Assert failed (%s)\n", __FILE__, \
                __LINE__, #cond); \
    }\
} while (0);
#else                           /* defined(MPISPAWN_DEBUG) */
#define MT_ASSERT(cond)
#endif                          /* defined(MPISPAWN_DEBUG) */

#endif
