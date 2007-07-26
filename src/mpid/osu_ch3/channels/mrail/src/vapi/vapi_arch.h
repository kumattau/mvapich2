/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

/* Copyright (c) 2003-2007, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#ifndef _VIA64_H
#define _VIA64_H


/*
 * typedefs and macros that make it easier to make things
 * portable between 32 and 64 bit platforms. 
 * 
 * aint_t is a typedef that is an unsigned integer of the same size as a pointer
 * AINT_FORMAT is a printf format string for aint_t
 * uint64_t is a typedef that is a 64-bit unsigned integer 
 * UINT64_FORMAT is a printf format string for uint64_t
 * UINT32_FORMAT is a printf format string for uint32_t
 * 
 */


#if !defined(_IA32_) && !defined(_IA64_) && !defined(_X86_64_)      \
&& !defined(_EM64T_) && !defined(MAC_OSX)

#error Either _IA32_ or _IA64_ or _X86_64_ or _EM64T_ or MAC_OSX must be defined
#endif

#if defined(_IA32_) && defined(_IA64_)
#error Only one of IA32 and IA64 can be defined
#endif

#if defined(_IA32_) && defined(_X86_64_)
#error Only one of _IA32_ and _X86_64_ can be defined
#endif

#if defined(_IA32_) && defined(_EM64T_)
#error Only one of _IA32_ and _EM64T_ can be defined
#endif

#if defined(_IA64_) && defined(_X86_64_)
#error Only one of _IA64_ and _X86_64_ can be defined
#endif

#if defined(_IA64_) && defined(_EM64T_)
#error Only one of _IA64_ and _EM64T_ can be defined
#endif

#if defined(_X86_64_) && defined(_EM64T_)
#error Only one of _X86_64_ and _EM64T_ can be defined
#endif

#if defined(MAC_OSX) && defined(_IA64_)
#error Only one of MAC_OSX and _IA64_ can be defined
#endif

#if defined(MAC_OSX) && defined(_X86_64_)
#error Only one of MAC_OSX and _X86_64_ can be defined
#endif

#if defined(MAC_OSX) && defined(_EM64T_)
#error Only one of MAC_OSX  and _EM64T_ can be defined
#endif

#if defined(MAC_OSX) && defined(_IA32_)
#error Only one of _IA32_ and MAC_OSX can be defined
#endif

#if defined(_DDR_) && defined(_SDR_)
#error Only one of _DDR_ and _SDR_ can be defined
#endif

#if defined(_PCI_X_) && defined(_PCI_EX_)
#error Only one of _PCI_X_ and _PCI_EX_ can be defined
#endif

#if (defined(_IA64_) || defined(_X86_64_) || defined(_EM64T_) \
    || defined(MAC_OSX))

typedef unsigned long aint_t;
#define AINT_FORMAT "%lx"

#define UINT32_FORMAT "%u"

#elif defined(_IA32_)

/*
 * note that aint_t could be unsigned long for x86. 
 * unsigned int is the same. Leave it this way so
 * that the compiler gives us a warning message if
 * we have accidentally hardcoded %lu explicitly. 
 */
typedef unsigned int aint_t;
#define AINT_FORMAT "%x"

#define UINT32_FORMAT "%u"

#else

#error Either _IA32_ or _IA64_ or _X86_64_ or _EM64T_ or MAC_OSX must be defined.
#endif

#include "stdint.h"

#endif                          /* _VIA64_H */
