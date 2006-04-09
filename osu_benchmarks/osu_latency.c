/*
 * Copyright (C) 2002-2005 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.  
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include "mpi.h"
#define MESSAGE_ALIGNMENT 64
#define MAX_MSG_SIZE (1<<22)
#define MYBUFSIZE (MAX_MSG_SIZE + MESSAGE_ALIGNMENT)

char        s_buf_original[MYBUFSIZE];
char        r_buf_original[MYBUFSIZE];

int         skip = 1000;
int         loop = 10000;
int         skip_large = 10;
int         loop_large = 100;
int         large_message_size = 8192;

int main (int argc, char *argv[])
{
    int         myid, numprocs, i;
    int         size;
    MPI_Status  reqstat;
    char       *s_buf, *r_buf;
    int         align_size;

    double      t_start = 0.0, t_end = 0.0;

    MPI_Init (&argc, &argv);
    MPI_Comm_size (MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank (MPI_COMM_WORLD, &myid);

    align_size = MESSAGE_ALIGNMENT;

    s_buf =
        (char *) (((unsigned long) s_buf_original + (align_size - 1)) /
                  align_size * align_size);
    r_buf =
        (char *) (((unsigned long) r_buf_original + (align_size - 1)) /
                  align_size * align_size);

    if (myid == 0) {
        fprintf (stdout, "# OSU MPI Latency Test (Version 2.1)\n");
        fprintf (stdout, "# Size\t\tLatency (us) \n");
    }

    for (size = 0; size <= MAX_MSG_SIZE;
         size = (size ? size * 2 : size + 1)) {

        /* touch the data */
        for (i = 0; i < size; i++) {
            s_buf[i] = 'a';
            r_buf[i] = 'b';
        }

        if (size > large_message_size) {
            loop = loop_large;
            skip = skip_large;
        }

        MPI_Barrier (MPI_COMM_WORLD);

        if (myid == 0) {
            for (i = 0; i < loop + skip; i++) {
                if (i == skip)
                    t_start = MPI_Wtime ();
                MPI_Send (s_buf, size, MPI_CHAR, 1, 1, MPI_COMM_WORLD);
                MPI_Recv (r_buf, size, MPI_CHAR, 1, 1, MPI_COMM_WORLD,
                          &reqstat);
            }
            t_end = MPI_Wtime ();

        } else if (myid == 1) {
            for (i = 0; i < loop + skip; i++) {
                MPI_Recv (r_buf, size, MPI_CHAR, 0, 1, MPI_COMM_WORLD,
                          &reqstat);
                MPI_Send (s_buf, size, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
            }
        }

        if (myid == 0) {
            double      latency;
            latency = (t_end - t_start) * 1.0e6 / (2.0 * loop);
            fprintf (stdout, "%d\t\t%0.2f\n", size, latency);
        }
    }

    MPI_Finalize ();
    return 0;
}
