/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "mpitest.h"

static char MTEST_Descrip[] = "Threaded Send-Recv";

/* The buffer size needs to be large enough to cause the rndv protocol to be used.
   If the MPI provider doesn't use a rndv protocol then the size doesn't matter.
 */
#define MSG_SIZE 1024*1024

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define sleep(a) Sleep(a*1000)
#define THREAD_RETURN_TYPE DWORD
int start_send_thread(THREAD_RETURN_TYPE (*fn)(void *p))
{
    HANDLE hThread;
    hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, NULL, 0, NULL);
    if (hThread == NULL)
    {
	return GetLastError();
    }
    CloseHandle(hThread);
    return 0;
}
#else
#include <pthread.h>
#define THREAD_RETURN_TYPE void *
int start_send_thread(THREAD_RETURN_TYPE (*fn)(void *p))
{
    int err;
    pthread_t thread;
    /*pthread_attr_t attr;*/
    err = pthread_create(&thread, NULL/*&attr*/, fn, NULL);
    return err;
}
#endif

THREAD_RETURN_TYPE send_thread(void *p)
{
    int err;
    char *buffer;
    int length;
    int rank;

    buffer = malloc(sizeof(char)*MSG_SIZE);
    if (buffer == NULL)
    {
	printf("malloc failed to allocate %d bytes for the send buffer.\n", MSG_SIZE);
	fflush(stdout);
	return (THREAD_RETURN_TYPE)-1;
    }
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    err = MPI_Send(buffer, MSG_SIZE, MPI_CHAR, rank == 0 ? 1 : 0, 0, MPI_COMM_WORLD);
    if (err)
    {
	MPI_Error_string(err, buffer, &length);
	printf("MPI_Send of %d bytes from %d to %d failed, error: %s\n",
	    MSG_SIZE, rank, rank == 0 ? 1 : 0, buffer);
	fflush(stdout);
    }
    return (THREAD_RETURN_TYPE)err;
}

int main( int argc, char *argv[] )
{
    int err;
    int rank, size;
    int provided;
    char *buffer;
    int length;
    MPI_Status status;

    err = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (err != MPI_SUCCESS)
    {
	MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (provided != MPI_THREAD_MULTIPLE)
    {
	if (rank == 0)
	{
	    printf("MPI_Init_thread must return MPI_THREAD_MULTIPLE in order for this test to run.\n");
	    fflush(stdout);
	}
	MPI_Finalize();
	return -1;
    }

    if (size > 2)
    {
	printf("please run with exactly two processes.\n");
	MPI_Finalize();
	return -1;
    }

    start_send_thread(send_thread);

    sleep(3); /* give the send thread time to start up and begin sending the message */

    buffer = malloc(sizeof(char)*MSG_SIZE);
    if (buffer == NULL)
    {
	printf("malloc failed to allocate %d bytes for the recv buffer.\n", MSG_SIZE);
	fflush(stdout);
	MPI_Abort(MPI_COMM_WORLD, -1);
    }
    err = MPI_Recv(buffer, MSG_SIZE, MPI_CHAR, rank == 0 ? 1 : 0, 0, MPI_COMM_WORLD, &status);
    if (err)
    {
	MPI_Error_string(err, buffer, &length);
	printf("MPI_Recv of %d bytes from %d to %d failed, error: %s\n",
	    MSG_SIZE, rank, rank == 0 ? 1 : 0, buffer);
	fflush(stdout);
    }

    MTest_Finalize(err != MPI_SUCCESS ? 1 : 0);
    MPI_Finalize();
    return 0;
}
