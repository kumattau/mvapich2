/* Copyright (c) 2001-2013, The Ohio State University. All rights
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

typedef enum {
    POLICY_BUNCH,
    POLICY_SCATTER,
} policy_type_t;

typedef enum {
    LEVEL_CORE,
    LEVEL_SOCKET,
    LEVEL_NUMANODE,
} level_type_t;

typedef struct {
    hwloc_obj_t obja;
    hwloc_obj_t objb;
    hwloc_obj_t ancestor;
} ancestor_type;

typedef struct {
    hwloc_obj_t obj;
    cpu_set_t cpuset;
    float load;
} obj_attribute_type;

extern policy_type_t policy;
extern level_type_t level;
extern hwloc_topology_t topology;
extern unsigned int mv2_enable_affinity;
extern unsigned int mv2_enable_leastload;

extern int s_cpu_mapping_line_max;
extern char *s_cpu_mapping;

void map_scatter_load(obj_attribute_type * tree);
void map_bunch_load(obj_attribute_type * tree);
void map_scatter_core(int num_cpus);
void map_scatter_socket(int num_sockets, hwloc_obj_type_t binding_level);
void map_bunch_core(int num_cpus);
void map_bunch_socket(int num_sockets, hwloc_obj_type_t binding_level);
int get_cpu_mapping_hwloc(long N_CPUs_online, hwloc_topology_t topology);
int get_cpu_mapping(long N_CPUs_online);
int smpi_setaffinity(int my_local_id);

#endif

#endif /* CH3_HWLOC_BIND_H_ */
