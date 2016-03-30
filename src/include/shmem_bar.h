/*
 * Copyright (c) 2001-2016, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
#ifndef SHMEM_BAR_H
#define SHMEM_BAR_H 1

#if defined(MAC_OSX) || defined(__powerpc__) || defined(__ppc__) || defined(__PPC__) || defined(__powerpc64__) || defined(__ppc64__)
#if defined(__GNUC__)
/* can't use -ansi for vxworks ccppc or this will fail with a syntax error
 * */
#define STBAR()  asm volatile ("dcs": : :"memory")     /* ": : :" for C++ */
#define READBAR() asm volatile ("dcs": : :"memory")
#define WRITEBAR() asm volatile ("dcs": : :"memory")
#else /* defined(__GNUC__) */
#if  defined(__IBMC__) || defined(__IBMCPP__)
extern void __iospace_eieio(void);
extern void __iospace_sync(void);
#define STBAR()   __iospace_sync ()
#define READBAR() __iospace_sync ()
#define WRITEBAR() __iospace_eieio ()
#else /* defined(__IBMC__) || defined(__IBMCPP__) */
#error Do not know how to make a store barrier for this system
#endif /* defined(__IBMC__) || defined(__IBMCPP__) */
#endif /* defined(__GNUC__) */

#if !defined(WRITEBAR)
#define WRITEBAR() STBAR()
#endif /* !defined(WRITEBAR) */
#if !defined(READBAR)
#define READBAR() STBAR()
#endif /* !defined(READBAR) */

#else /* defined(MAC_OSX) || defined(__powerpc__) || defined(__ppc__) || defined(__PPC__) || defined(__powerpc64__) || defined(__ppc64__) */
#define WRITEBAR()
#define READBAR()
#endif /* defined(MAC_OSX) || defined(__powerpc__) || defined(__ppc__) || defined(__PPC__) || defined(__powerpc64__) || defined(__ppc64__) */

#endif /* #ifndef SHMEM_BAR_H */
