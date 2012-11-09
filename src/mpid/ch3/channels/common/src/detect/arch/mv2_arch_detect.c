/* Copyright (c) 2001-2012, The Ohio State University. All rights
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

#include <stdio.h>
#include <string.h>
#include <infiniband/verbs.h>

#include <mpichconf.h>

#ifdef HAVE_LIBHWLOC
#include <hwloc.h>
#include <dirent.h>
#endif /* #ifdef HAVE_LIBHWLOC */

#include "mv2_arch_hca_detect.h"

#if defined(_SMP_LIMIC_)
#define SOCKETS 32
#define CORES 32
#define HEX_FORMAT 16
#define CORES_REP_AS_BITS 32

/*global variables*/
static int node[SOCKETS][CORES] = {{0}};
static int no_sockets=0;
static int socket_bound=-1; 
static int numcores_persocket[SOCKETS]={0};
#endif /*#if defined(_SMP_LIMIC_)*/

static mv2_arch_type g_mv2_arch_type = MV2_ARCH_UNKWN;
static int g_mv2_num_cpus = -1;

#define CONFIG_FILE         "/proc/cpuinfo"
#define MAX_LINE_LENGTH     512
#define MAX_NAME_LENGTH     512

#define CLOVERTOWN_MODEL    15
#define HARPERTOWN_MODEL    23
#define NEHALEM_MODEL       26
#define INTEL_E5630_MODEL   44
#define INTEL_X5650_MODEL   44
#define INTEL_E5_2670_MODEL 45

#define MV2_STR_VENDOR_ID    "vendor_id"
#define MV2_STR_AUTH_AMD     "AuthenticAMD"
#define MV2_STR_MODEL        "model"
#define MV2_STR_WS            " "
#define MV2_STR_PHYSICAL     "physical"
#define MV2_STR_MODEL_NAME   "model name"

#define INTEL_E5_2670_MODEL_NAME    "Intel(R) Xeon(R) CPU E5-2670 0 @ 2.60GHz"
#define INTEL_E5_2680_MODEL_NAME    "Intel(R) Xeon(R) CPU E5-2680 0 @ 2.70GHz"

#ifndef HAVE_LIBHWLOC

#define MAX_NUM_CPUS 256
int INTEL_XEON_DUAL_MAPPING[]      = {0,1,0,1};
int INTEL_CLOVERTOWN_MAPPING[]     = {0,0,1,1,0,0,1,1};                  /*        ((0,1),(4,5))((2,3),(6,7))             */
int INTEL_HARPERTOWN_LEG_MAPPING[] = {0,1,0,1,0,1,0,1};                  /* legacy ((0,2),(4,6))((1,3),(5,7))             */
int INTEL_HARPERTOWN_COM_MAPPING[] = {0,0,0,0,1,1,1,1};                  /* common ((0,1),(2,3))((4,5),(6,7))             */
int INTEL_NEHALEM_LEG_MAPPING[]    = {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1};  /* legacy (0,2,4,6)(1,3,5,7) with hyperthreading */
int INTEL_NEHALEM_COM_MAPPING[]    = {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1};  /* common (0,1,2,3)(4,5,6,7) with hyperthreading */
int AMD_OPTERON_DUAL_MAPPING[]     = {0,0,1,1};
int INTEL_E2_2670_MAPPING[]	   = {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1};
int AMD_BARCELONA_MAPPING[]        = {0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3};
int AMD_MAGNY_CRS_MAPPING[]        = {1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2};
int AMD_OPTERON_32_MAPPING[]       = {1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4};
int AMD_OPTERON_64_MAPPING[]	   = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
int AMD_BULLDOZER_4274HE_16_MAPPING[] = {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1};

#endif /* #ifndef HAVE_LIBHWLOC */

/* CPU Family */
typedef enum{
    CPU_FAMILY_NONE=0,
    CPU_FAMILY_INTEL,
    CPU_FAMILY_AMD,
} mv2_cpu_type;

typedef struct _mv2_arch_types_log_t{
    uint64_t arch_type;
    char *arch_name;
}mv2_arch_types_log_t;

#define MV2_ARCH_LAST_ENTRY -1
static mv2_arch_types_log_t mv2_arch_types_log[] = 
{
    /* Intel Architectures */
    {MV2_ARCH_INTEL_GENERIC,        "MV2_ARCH_INTEL_GENERIC"},
    {MV2_ARCH_INTEL_CLOVERTOWN_8,   "MV2_ARCH_INTEL_CLOVERTOWN_8"},
    {MV2_ARCH_INTEL_NEHALEM_8,      "MV2_ARCH_INTEL_NEHALEM_8"},
    {MV2_ARCH_INTEL_NEHALEM_16,     "MV2_ARCH_INTEL_NEHALEM_16"},
    {MV2_ARCH_INTEL_HARPERTOWN_8,   "MV2_ARCH_INTEL_HARPERTOWN_8"},
    {MV2_ARCH_INTEL_XEON_DUAL_4,    "MV2_ARCH_INTEL_XEON_DUAL_4"},
    {MV2_ARCH_INTEL_XEON_E5630_8,   "MV2_ARCH_INTEL_XEON_E5630_8"},
    {MV2_ARCH_INTEL_XEON_X5650_12,  "MV2_ARCH_INTEL_XEON_X5650_12"},
    {MV2_ARCH_INTEL_XEON_E5_2670_16,"MV2_ARCH_INTEL_XEON_E5_2670_16"},
    {MV2_ARCH_INTEL_XEON_E5_2680_16,"MV2_ARCH_INTEL_XEON_E5_2680_16"},

    /* AMD Architectures */
    {MV2_ARCH_AMD_GENERIC,          "MV2_ARCH_AMD_GENERIC"},
    {MV2_ARCH_AMD_BARCELONA_16,     "MV2_ARCH_AMD_BARCELONA_16"},
    {MV2_ARCH_AMD_MAGNY_COURS_24,   "MV2_ARCH_AMD_MAGNY_COURS_24"},
    {MV2_ARCH_AMD_OPTERON_DUAL_4,   "MV2_ARCH_AMD_OPTERON_DUAL_4"},
    {MV2_ARCH_AMD_OPTERON_6136_32,  "MV2_ARCH_AMD_OPTERON_6136_32"},
    {MV2_ARCH_AMD_OPTERON_6276_64,  "MV2_ARCH_AMD_OPTERON_6276_64"},
    {MV2_ARCH_AMD_BULLDOZER_4274HE_16,"MV2_ARCH_AMD_BULLDOZER_4274HE_16"},

    /* IBM Architectures */
    {MV2_ARCH_IBM_PPC,              "MV2_ARCH_IBM_PPC"},

    /* Unknown */
    {MV2_ARCH_UNKWN,                "MV2_ARCH_UNKWN"},
    {MV2_ARCH_LAST_ENTRY,           "MV2_ARCH_LAST_ENTRY"},
};

char*  mv2_get_arch_name(mv2_arch_type arch_type)
{
    int i=0;
    while(mv2_arch_types_log[i].arch_type != MV2_ARCH_LAST_ENTRY){

        if(mv2_arch_types_log[i].arch_type == arch_type){
            return(mv2_arch_types_log[i].arch_name);
        }
        i++;
    }
    return("MV2_ARCH_UNKWN");
}


/* Identify architecture type */
#ifdef HAVE_LIBHWLOC
mv2_arch_type mv2_get_arch_type()
{
    if ( MV2_ARCH_UNKWN == g_mv2_arch_type ) {
        FILE *fp;
        int num_sockets = 0, cpu_model = 0, num_cpus = 0, ret;
        int model_name_set=0;
        unsigned topodepth = -1, depth = -1;
        char line[MAX_LINE_LENGTH], *tmp, *key;
        char model_name[MAX_NAME_LENGTH]={0};

        mv2_arch_type arch_type = MV2_ARCH_UNKWN;
        mv2_cpu_type cpu_type = CPU_FAMILY_NONE;
        hwloc_topology_t topology;

        /* Initialize hw_loc */
        ret = hwloc_topology_init(&topology);
        if ( 0 != ret ){
            fprintf( stderr, "Warning: %s: Failed to initialize hwloc\n",
                    __func__ );
            return arch_type;
        }
        hwloc_topology_load(topology);

        /* Determine topology depth */
        topodepth = hwloc_topology_get_depth(topology);
        if( HWLOC_TYPE_DEPTH_UNKNOWN == topodepth ) {
            fprintf(stderr, "Warning: %s: Failed to determine topology depth.\n", __func__ );
            return arch_type;
        }

        /* Count number of (logical) processors */
        depth = hwloc_get_type_depth(topology, HWLOC_OBJ_PU);

        if( HWLOC_TYPE_DEPTH_UNKNOWN == depth ) {
            fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__ );
            return arch_type;
        }
        if(! (num_cpus = hwloc_get_nbobjs_by_type(topology, HWLOC_OBJ_CORE))){
            fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__);
            return arch_type;
        }
        g_mv2_num_cpus = num_cpus;

        /* Count number of sockets */
        depth = hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET);
        if( HWLOC_TYPE_DEPTH_UNKNOWN == depth ) {
            fprintf(stderr, "Warning: %s: Failed to determine number of sockets.\n", __func__);
            return arch_type;
        } else {
            num_sockets = hwloc_get_nbobjs_by_depth(topology, depth);
        }

        /* free topology info */
        hwloc_topology_destroy( topology );

        /* Parse /proc/cpuinfo for additional useful things */
        if((fp = fopen(CONFIG_FILE, "r"))) { 

            while(! feof(fp)) {
                memset(line, 0, MAX_LINE_LENGTH);
                fgets(line, MAX_LINE_LENGTH - 1, fp); 

                if(! (key = strtok(line, "\t:"))) {
                    continue;
                }

                /* Identify the CPU Family */
                if(! strcmp(key, MV2_STR_VENDOR_ID)) {
                    strtok(NULL, MV2_STR_WS);
                    tmp = strtok(NULL, MV2_STR_WS);

                    if (! strncmp(tmp, MV2_STR_AUTH_AMD, strlen( MV2_STR_AUTH_AMD
                                    ))) {
                        cpu_type = CPU_FAMILY_AMD;

                    } else {
                        cpu_type = CPU_FAMILY_INTEL;
                    }
                    continue;
                }

                if( !cpu_model ) {

                    if(! strcmp(key, MV2_STR_MODEL)) {
                        strtok(NULL, MV2_STR_WS);
                        tmp = strtok(NULL, MV2_STR_WS);
                        sscanf(tmp, "%d", &cpu_model);
                        continue;
                    }
                }
           
                if (!model_name_set){
                    if (strncmp(key, MV2_STR_MODEL_NAME, strlen(MV2_STR_MODEL_NAME)) == 0) {
                        strtok(NULL, MV2_STR_WS);
                        tmp = strtok(NULL, "\n");
                        sscanf(tmp, "%[^\n]\n", model_name);
                        model_name_set = 1;
                    }
                }
            }
            fclose(fp);

            if( CPU_FAMILY_INTEL == cpu_type ) {
                arch_type = MV2_ARCH_INTEL_GENERIC;

                if(2 == num_sockets ) {

                    if(4 == num_cpus) {
                        arch_type = MV2_ARCH_INTEL_XEON_DUAL_4;

                    } else if(8 == num_cpus) {

                        if(CLOVERTOWN_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_CLOVERTOWN_8;

                        } else if(HARPERTOWN_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_HARPERTOWN_8;

                        } else if(NEHALEM_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_NEHALEM_8;
                        
                        } else if(INTEL_E5630_MODEL == cpu_model){
                            arch_type = MV2_ARCH_INTEL_XEON_E5630_8;
                        } 

                    } else if(12 == num_cpus) {
                        if(INTEL_X5650_MODEL == cpu_model) {  
                            /* Westmere EP model, Lonestar */
                            arch_type = MV2_ARCH_INTEL_XEON_X5650_12;
                        }
                    } else if(16 == num_cpus) {

                        if(NEHALEM_MODEL == cpu_model) {  /* nehalem with smt on */
                            arch_type = MV2_ARCH_INTEL_NEHALEM_16;
                        
			}else if(INTEL_E5_2670_MODEL == cpu_model) {
                            if(strncmp(model_name, INTEL_E5_2670_MODEL_NAME, 
                                        strlen(INTEL_E5_2670_MODEL_NAME)) == 0){
                                arch_type = MV2_ARCH_INTEL_XEON_E5_2670_16;

                            } else if(strncmp(model_name, INTEL_E5_2680_MODEL_NAME, 
                                        strlen(INTEL_E5_2680_MODEL_NAME)) == 0){
                                arch_type = MV2_ARCH_INTEL_XEON_E5_2680_16;

                            } else {
                                arch_type = MV2_ARCH_INTEL_GENERIC;
                            }
                        }
                    }
                }

            } else if(CPU_FAMILY_AMD == cpu_type) {
                arch_type = MV2_ARCH_AMD_GENERIC;

                if(2 == num_sockets) {

                    if(4 == num_cpus) {
                        arch_type = MV2_ARCH_AMD_OPTERON_DUAL_4;
                    
                    } else if(16 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_BULLDOZER_4274HE_16;

                    } else if(24 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_MAGNY_COURS_24;
                    }
                } else if(4 == num_sockets) {

                    if(16 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_BARCELONA_16;

                    } else if(32 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_OPTERON_6136_32;
                    
		    } else if(64 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_OPTERON_6276_64;
                    }
                }
            }
        } else {
            fprintf(stderr, "Warning: %s: Failed to open \"%s\".\n", __func__,
                    CONFIG_FILE);
        }
        g_mv2_arch_type = arch_type;
        return arch_type;
    } else {
        return g_mv2_arch_type;
    }

}

#else

mv2_arch_type mv2_get_arch_type()
{
    if ( MV2_ARCH_UNKWN == g_mv2_arch_type ) {
        char line[MAX_LINE_LENGTH];
        char input[MAX_NAME_LENGTH];
        char bogus1[MAX_NAME_LENGTH];
        char bogus2[MAX_NAME_LENGTH];
        char bogus3[MAX_NAME_LENGTH];
        char model_name[MAX_NAME_LENGTH] = {0};

        int physical_id;
        int core_mapping[MAX_NUM_CPUS];
        int core_index = 0, num_cpus;
        int model;
        int vendor_set=0, model_set=0, model_name_set=0;

        mv2_cpu_type cpu_type = CPU_FAMILY_NONE;
        mv2_arch_type arch_type = MV2_ARCH_UNKWN;

        FILE* fp=fopen(CONFIG_FILE,"r");
        if (fp == NULL){
            fprintf( stderr, "Cannot open cpuinfo file\n");
            return arch_type;
        }
        memset(core_mapping, 0, sizeof(int) * MAX_NUM_CPUS);

        while(!feof(fp)){
            memset(line,0,MAX_LINE_LENGTH);
            fgets(line, MAX_LINE_LENGTH, fp);

            memset(input, 0, MAX_NAME_LENGTH);
            sscanf(line, "%s", input);

            if (!vendor_set) {
                if (strcmp(input, MV2_STR_VENDOR_ID) == 0) {
                    memset(input, 0, MAX_NAME_LENGTH);
                    sscanf(line,"%s%s%s",bogus1, bogus2, input);
                    if (strcmp(input, MV2_STR_AUTH_AMD ) == 0) {
                        cpu_type = CPU_FAMILY_AMD;
                        arch_type = MV2_ARCH_AMD_GENERIC;
                    } else {
                        cpu_type = CPU_FAMILY_INTEL;
                        arch_type = MV2_ARCH_INTEL_GENERIC;
                    }
                    vendor_set = 1;
                }
            }

            if (!model_set){
                if (strcmp(input, MV2_STR_MODEL) == 0) {
                    sscanf(line, "%s%s%d", bogus1, bogus2, &model);
                    model_set = 1;
                }
            }
           
            if (!model_name_set){
                if (strncmp(line, MV2_STR_MODEL_NAME, strlen(MV2_STR_MODEL_NAME)) == 0) {
                    sscanf(line, "%s%s%[^\n]\n", bogus1, bogus2, model_name);
                    model_name_set = 1;
                }
            }

            if (strcmp(input, MV2_STR_PHYSICAL ) == 0) {
                sscanf(line, "%s%s%s%d", bogus1, bogus2, bogus3, &physical_id);
                core_mapping[core_index++] = physical_id;
            }
        }

        /* Set the number of cores per node */
        g_mv2_num_cpus = num_cpus = core_index;
        if ( 4 == num_cpus ) {
            if((memcmp(INTEL_XEON_DUAL_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) == 0)
                    && ( CPU_FAMILY_INTEL == cpu_type )){
                arch_type = MV2_ARCH_INTEL_XEON_DUAL_4;
            } else
                if((memcmp(AMD_OPTERON_DUAL_MAPPING,core_mapping,sizeof(int)*num_cpus)
                            == 0)
                        && ( CPU_FAMILY_AMD == cpu_type )){
                    arch_type = MV2_ARCH_AMD_OPTERON_DUAL_4;
                }
        } else if ( 8 == num_cpus ) {
            if( CPU_FAMILY_INTEL == cpu_type ) {
                if( CLOVERTOWN_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_CLOVERTOWN_8;
                }
                else if( HARPERTOWN_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_HARPERTOWN_8;
                }
                else if( NEHALEM_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_NEHALEM_8;
                }
                else if( INTEL_E5630_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_XEON_E5630_8;
                }
            }
        } else if ( 12 == num_cpus ) {
            if( CPU_FAMILY_INTEL == cpu_type ) {
                if(INTEL_X5650_MODEL == model) { 
                     /* Westmere EP model, Lonestar */
                     arch_type = MV2_ARCH_INTEL_XEON_X5650_12;
                }
            } 
        } else if ( 16 == num_cpus ) {

            if( CPU_FAMILY_INTEL == cpu_type ) {
                if( NEHALEM_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_NEHALEM_16;

                } else if((0 == memcmp(INTEL_E2_2670_MAPPING, core_mapping,
                                sizeof(int)*num_cpus)) && INTEL_E5_2670_MODEL == model) {
                    if(strncmp(model_name, INTEL_E5_2670_MODEL_NAME, 
                                strlen(INTEL_E5_2670_MODEL_NAME)) == 0){
                        arch_type = MV2_ARCH_INTEL_XEON_E5_2670_16;

                    } else if(strncmp(model_name, INTEL_E5_2680_MODEL_NAME, 
                                strlen(INTEL_E5_2680_MODEL_NAME)) == 0){
                        arch_type = MV2_ARCH_INTEL_XEON_E5_2680_16;

                    } else {
                        arch_type = MV2_ARCH_INTEL_GENERIC;
                    }
                }
            
	    } else if( CPU_FAMILY_AMD == cpu_type ) {
                if(0 == memcmp(AMD_BARCELONA_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_BARCELONA_16;
                
                } else if(0 == memcmp(AMD_BULLDOZER_4274HE_16_MAPPING, core_mapping,
                            sizeof(int)*num_cpus)){
                    arch_type = MV2_ARCH_AMD_BULLDOZER_4274HE_16;
                }
            }
        } else if  ( 24 == num_cpus ){
            if ( CPU_FAMILY_AMD == cpu_type ){
                if (0 == memcmp(AMD_MAGNY_CRS_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_MAGNY_COURS_24;
                }
            }
        } else if  ( 32 == num_cpus ){
            if ( CPU_FAMILY_AMD == cpu_type ){
                if (0 == memcmp(AMD_OPTERON_32_MAPPING, core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_OPTERON_6136_32;
                }
            }
        } else if  ( 64 == num_cpus ){
            if ( CPU_FAMILY_AMD == cpu_type ){
                if (0 == memcmp(AMD_OPTERON_64_MAPPING, core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_OPTERON_6276_64;
                }
            }
        }

        fclose(fp);
        g_mv2_arch_type = arch_type;
        return arch_type;

    }else {
        return g_mv2_arch_type;
    }
}


#endif

/* API for getting the number of cpus */
int mv2_get_num_cpus()
{
    /* Check if num_cores is already identified */
    if ( -1 == g_mv2_num_cpus ){
        g_mv2_arch_type = mv2_get_arch_type();
    }
    return g_mv2_num_cpus;
}

/* Check arch-hca type */
int mv2_is_arch_hca_type(mv2_arch_hca_type arch_hca_type, 
        mv2_arch_type arch_type, mv2_hca_type hca_type)
{
    int ret;
    if (MV2_ARCH_ANY == arch_type && MV2_HCA_ANY == hca_type){
        ret = 1;

    } else if (MV2_ARCH_ANY == arch_type){
        mv2_arch_hca_type tmp = UINT32_MAX;
        mv2_arch_hca_type input = arch_hca_type & tmp;
        ret = (input==hca_type) ? 1: 0;

    } else if (MV2_HCA_ANY == hca_type){
        mv2_arch_hca_type tmp = UINT32_MAX;
        tmp = tmp << 32;
        mv2_arch_hca_type input = arch_hca_type & tmp;
        ret = (input==arch_type) ? 1: 0;

    } else{
        uint64_t value = arch_type;
        value = value << 32 | hca_type;
        ret = (value==arch_hca_type) ? 1:0;
    }
    return ret;
}
#if defined(_SMP_LIMIC_) && defined(HAVE_LIBHWLOC)
void hwlocSocketDetection(int print_details)
{
    int depth;
    unsigned i, j;
    char *str;
    char * pEnd;
    char * pch;
    int more32bit=0,offset=0;
    long int core_cnt[2];
    hwloc_topology_t topology;
    hwloc_cpuset_t cpuset;
    hwloc_obj_t sockets;
    /* Allocate and initialize topology object. */
    hwloc_topology_init(&topology);
    
    /* Perform the topology detection. */
    hwloc_topology_load(topology);
    /*clear all the socket information and reset to -1*/
    for(i=0;i<SOCKETS;i++)
        for(j=0;j<CORES;j++)
            node[i][j]=-1;
    
    depth = hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET);
    no_sockets=hwloc_get_nbobjs_by_depth(topology, depth);
    if(print_details)
        printf("Total number of sockets=%d\n", no_sockets);
    for(i=0;i<no_sockets;i++)
    {   
        sockets = hwloc_get_obj_by_type(topology, HWLOC_OBJ_SOCKET, i); 
        cpuset = sockets->cpuset;
        hwloc_bitmap_asprintf(&str, cpuset);

        /*tokenize the str*/
        pch = strtok (str,",");
        while (pch != NULL)
        {   
            pch = strtok (NULL, ",");
            if(pch != NULL)
            {   
                more32bit=1;
                break;
            }   
        }   
        
        core_cnt[0]= strtol (str,&pEnd,HEX_FORMAT);
        /*if more than bits, then explore the values*/
        if(more32bit)
        {   
            /*tells multiple of 32 bits(eg if 0, then 64 bits)*/
            core_cnt[1] = strtol (pEnd,NULL,0);
            offset = (core_cnt[1] + 1)*CORES_REP_AS_BITS;
        }   

        for(j=0;j<CORES_REP_AS_BITS;j++)
        {   
            if(core_cnt[0] & 1)
            {   
                node[i][j]=j+offset;
                (numcores_persocket[i])++;
            }   
            core_cnt[0] = (core_cnt[0] >> 1); 
        } 
        
        if(print_details)
        {
            printf("Socket %d, num of cores / socket=%d\n", i, (numcores_persocket[i]));
            printf("core id\n");
        }
        
        for(j=0;j<CORES_REP_AS_BITS;j++)
        {                 
            if(print_details)
                printf("%d\t", node[i][j]); 
        }         
        if(print_details)
             printf("\n");
    }   
    free(str);

    hwloc_topology_destroy(topology);

}

//Check the core, where the process is bound to
int getProcessBinding(pid_t pid)
{
    int res,i=0,j=0;
    char *str=NULL;
    char *pEnd=NULL;
    char *pch=NULL;
    int more32bit=0,offset=0;
    unsigned int core_bind[2];
    hwloc_bitmap_t cpubind_set;
    hwloc_topology_t topology;

    /* Allocate and initialize topology object. */
    hwloc_topology_init(&topology);
    
    /* Perform the topology detection. */
    hwloc_topology_load(topology);
    cpubind_set = hwloc_bitmap_alloc();
    res = hwloc_get_proc_cpubind(topology, pid, cpubind_set, 0);
    if(-1 == res)
        printf("getProcessBinding(): Error in getting cpubinding of process");

    hwloc_bitmap_asprintf(&str, cpubind_set);
    
    /*tokenize the str*/
    pch = strtok (str,",");
    while (pch != NULL)
    {   
        pch = strtok (NULL, ",");
        if(pch != NULL)
        {   
            more32bit=1;
            break;
        }   
    }   

    core_bind[0]= strtol (str,&pEnd,HEX_FORMAT);
    
    /*if more than bits, then explore the values*/
    if(more32bit)
    {   
        /*tells multiple of 32 bits(eg if 0, then 64 bits)*/
        printf("more bits set\n");
        core_bind[1] = strtol (pEnd,NULL,0);
        printf("core_bind[1]=%x\n", core_bind[1]);
        offset = (core_bind[1] + 1)*CORES_REP_AS_BITS;
        printf("Offset=%d\n", offset);
    }   

    for(j=0;j<CORES_REP_AS_BITS;j++)
    {   
        if(core_bind[0] & 1)
        {   
            core_bind[0]=j+offset;
            break;
        }   
        core_bind[0]= (core_bind[0] >> 1); 
    }   

    /*find the socket, where the core is present*/
    for(i=0;i<no_sockets;i++)
    {   
        j=core_bind[0]-offset;
        if(node[i][j]== j+offset)
        {
	        free(str);
            hwloc_bitmap_free(cpubind_set);
            hwloc_topology_destroy(topology);
            return i; /*index of socket where the process is bound*/
        }
    }   
    printf("Error: Process not bound on any core ??\n");
    free(str);
    hwloc_bitmap_free(cpubind_set);
    hwloc_topology_destroy(topology);
    return -1;
}

int numOfCoresPerSocket(int socket)
{
    return numcores_persocket[socket];
}

int numofSocketsPerNode (void)
{
    return no_sockets;
}

int get_socket_bound(void)
{ 
   if(socket_bound == -1) { 
       socket_bound = getProcessBinding(getpid()); 
   } 
   return socket_bound; 
}
#else
void hwlocSocketDetection(int print_details) { }
int numOfCoresPerSocket(int socket) { return 0; }
int numofSocketsPerNode (void) { return 0; }
int get_socket_bound(void) { return -1; }
#endif /*#if defined(_SMP_LIMIC_)*/
