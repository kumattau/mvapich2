#define BENCHMARK "OSU MPI One Sided MPI_Fetch_and_op with Flush Latency Test"
/*
 * Copyright (C) 2002-2013 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University. 
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */

#include "mpi.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <inttypes.h>


#ifdef PACKAGE_VERSION
#   define HEADER "# " BENCHMARK " v" PACKAGE_VERSION "\n"
#else
#   define HEADER "# " BENCHMARK "\n"
#endif

#ifndef FIELD_WIDTH
#   define FIELD_WIDTH 20
#endif

#ifndef FLOAT_PRECISION
#   define FLOAT_PRECISION 2
#endif

#define MAX_ALIGNMENT 65536
#define MAX_MSG_SIZE sizeof(uint64_t)
#define MYBUFSIZE (MAX_MSG_SIZE + MAX_ALIGNMENT)

char s_buf_original[MYBUFSIZE];
char r_buf_original[MYBUFSIZE];
char t_buf_original[MYBUFSIZE];

long loop = 500;
long skip = 10;

int allocate_memory (uint64_t **sbuf, uint64_t **rbuf, uint64_t **t_buf, 
                     int myid);
void print_header (int rank); 

int 
main (int argc, char *argv[])
{
    int         myid, nprocs, destrank; 
    long        i, j, k, size, count;
    MPI_Group   comm_group, group;
    MPI_Win     win;
    double      t_start = 0.0, t_end = 0.0;
    uint64_t    *s_buf, *r_buf, *t_buf;
    long        buffer_size;
    size = sizeof(MPI_LONG_LONG);

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    MPI_Comm_group(MPI_COMM_WORLD, &comm_group);

    if (nprocs != 2) {
        if (myid == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }

        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    if (allocate_memory(&s_buf, &r_buf, &t_buf, myid)) {
        /* Error allocating memory */
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    print_header(myid);

    MPI_Win_create(r_buf, MAX_MSG_SIZE, sizeof(uint64_t), MPI_INFO_NULL, MPI_COMM_WORLD, &win);

    r_buf[0] = 1;
    s_buf[0] = 2;
    t_buf[0] = 0;

    MPI_Barrier(MPI_COMM_WORLD);

    if (myid == 1) { 
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
        for (i = 0; i < (loop + skip); i++) {
            if (i == skip) { 
                t_start = MPI_Wtime();
            }
            MPI_Fetch_and_op (s_buf, t_buf, MPI_LONG_LONG, 0, 0, MPI_SUM, win);                      
            MPI_Win_flush(0, win);
        }
        MPI_Win_unlock(0, win);
    } 

    t_end = MPI_Wtime();

    MPI_Barrier(MPI_COMM_WORLD);

    if (myid == 1) {
        fprintf(stdout, "%-*ld%*.*f\n", 10, size, FIELD_WIDTH,
                FLOAT_PRECISION, (t_end - t_start) * 1e6 / loop);
        fflush(stdout);
    }

    MPI_Win_free(&win);

    MPI_Finalize ();

    return EXIT_SUCCESS;
}

void *
align_buffer (void *ptr, unsigned long align_size)
{
    return (void *)(((unsigned long)ptr + (align_size - 1)) / align_size *
            align_size);
}

int
allocate_memory (uint64_t ** sbuf, uint64_t ** rbuf, uint64_t ** tbuf, 
                 int rank)
{
    unsigned long align_size = getpagesize();

    assert(align_size <= MAX_ALIGNMENT);

    switch (rank) {
        case 0: 
            *sbuf = align_buffer(s_buf_original, align_size);
            *rbuf = align_buffer(r_buf_original, align_size);
            *tbuf = align_buffer(t_buf_original, align_size);
            break;
        case 1:
            *sbuf = align_buffer(s_buf_original, align_size);
            *rbuf = align_buffer(r_buf_original, align_size);
            *tbuf = align_buffer(t_buf_original, align_size);
            break;
    }

    return 0;
}

void print_header (int rank)
{       
    if (0 == rank) {
        printf(HEADER, "");
        printf("%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Latency (us)");
        fflush(stdout);
    }
    
}
/* vi: set sw=4 sts=4 tw=80: */
