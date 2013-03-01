## Copyright (c) 2001-2013, The Ohio State University. All rights
## reserved.
##
## This file is part of the MVAPICH2 software package developed by the
## team members of The Ohio State University's Network-Based Computing
## Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
##
## For detailed copyright and licensing information, please refer to the
## copyright file COPYRIGHT in the top level MVAPICH2 directory.
##

## todo: provide configure args for path to nvcc and specification of -arch or
## 	-maxrregcount values

NVCC = nvcc
NVCFLAGS = -cuda -arch sm_13 -maxrregcount 32
SUFFIXES = .cu .cpp
.cu.cpp:
	$(NVCC) $(NVCFLAGS) $(INCLUDES) $(CPPFLAGS) --output-file $@ $<

lib_lib@MPILIBNAME@_la_SOURCES += \
    src/mpid/ch3/channels/mrail/src/cuda/pack_unpack.cu
