#!/bin/sh

# Copyright (c) 2002-2006, The Ohio State University. All rights
# reserved.
# This file is part of the MVAPICH software package developed by the
# team members of The Ohio State University's Network-Based Computing
# Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
#
# For detailed copyright and licencing information, please refer to the
# copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.

MPI_CONSOLE_NAMES="mpiexec mpirun"

echo "pid  username  tty      command      cpu  mem   time     full_command"
for i in $MPI_CONSOLE_NAMES
do
  ps -e -o pid,user,tty,comm:11,pcpu,vsize,bsdstart,cmd | grep $i |  grep -v grep | grep -v python
done
