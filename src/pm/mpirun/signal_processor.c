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

#include <signal_processor.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*func_t)(int);

struct sp_params {
    sigset_t sigmask;
    func_t processor;
    int copied;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static void
signal_thread (struct sp_params * params)
{
    sigset_t sigmask = params->sigmask;
    func_t processor = params->processor;
    int error, signal;

    if ((error = pthread_detach(pthread_self()))) {
        PRINT_ERROR_ERRNO("pthread_detach", error);
        exit(EXIT_FAILURE);
    }

    /*
     * Signal the completion of copying the signal mask and function pointer
     * to signal processor so any pending resources can be reclaimed.
     */
    pthread_mutex_lock(&params->mutex);
    params->copied = 1;
    pthread_cond_signal(&params->cond);
    pthread_mutex_unlock(&params->mutex);

    for (;;) {
        if ((error = sigwait(&sigmask, &signal))) {
            PRINT_ERROR_ERRNO("sigwait", error);
            exit(EXIT_FAILURE);
        }

        processor(signal);
    }
}

extern void
start_signal_processor (sigset_t sigmask, void (*processor)(int))
{
    pthread_t thread;
    int error;
    struct sp_params params = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
        .copied = 0
    };

    params.sigmask = sigmask;
    params.processor = processor;

    if ((error = pthread_sigmask(SIG_SETMASK, &params.sigmask, NULL))) {
        PRINT_ERROR_ERRNO("pthread_sigmask", error);
        exit(EXIT_FAILURE);
    }

    if ((error = pthread_create(&thread, NULL, (void * (*)(void
                        *))signal_thread, (void *)&params))) {
        PRINT_ERROR_ERRNO("pthread_create", error);
        exit(EXIT_FAILURE);
    }

    /*
     * Do not exit function until the newly initialized thread has copied
     * over the signal mask and pointer to the signal_processor.
     */
    pthread_mutex_lock(&params.mutex);
    while (!params.copied) pthread_cond_wait(&params.cond, &params.mutex);
    pthread_mutex_unlock(&params.mutex);

    /*
     * Done with mutex and cond
     */
    pthread_mutex_destroy(&params.mutex);
    pthread_cond_destroy(&params.cond);
}

/* vi:set sw=4 sts=4 tw=76 expandtab: */
