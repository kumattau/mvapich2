/* Copyright (c) 2003-2010, The Ohio State University. All rights
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

#ifndef CH3_HWLOC_BIND_H_
#define CH3_HWLOC_BIND_H_

#if defined(HAVE_LIBHWLOC)

#include <hwloc.h>
#include <dirent.h>

typedef enum{
        POLICY_BUNCH,
        POLICY_SCATTER,
} policy_type_t;

typedef struct {
    hwloc_obj_t obja;
    hwloc_obj_t objb;
    hwloc_obj_t ancestor;
} ancestor_type;

extern policy_type_t policy;
extern hwloc_topology_t topology;
extern unsigned int mv2_enable_affinity;

extern int s_cpu_mapping_line_max;
extern char* s_cpu_mapping;

int smpi_setaffinity ();

#endif

#endif /* CH3_HWLOC_BIND_H_ */
