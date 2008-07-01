#ifndef MPIRUN_UTIL_H
#define MPIRUN_UTIL_H
/* Copyright (c) 2002-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

char * vedit_str(char * const, const char *, va_list);
char * edit_str(char * const, char const * const, ...);
char * mkstr(const char *, ...);
char * append_str(char *, char * const);

int read_socket(int, void *, size_t);
int write_socket(int, void *, size_t);

#endif
