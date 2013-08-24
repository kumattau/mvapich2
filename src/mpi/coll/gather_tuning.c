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

#include <regex.h>
#include "gather_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"
/* array used to tune gather */
int mv2_size_gather_tuning_table=7;
mv2_gather_tuning_table * mv2_gather_thresholds_table=NULL; 

#if defined(_SMP_LIMIC_)
extern int use_limic_gather;
#endif /*#if defined(_SMP_LIMIC_)*/

int MV2_set_gather_tuning_table(int heterogeneity)
{

#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_gather_tuning_table=6;
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table*
                sizeof (mv2_gather_tuning_table)); 
#if defined(_SMP_LIMIC_)
        if((g_use_limic2_coll) && (use_limic_gather)) {
            mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
                {32,
                    3,{{0, 4096, &MPIR_Gather_MV2_Direct},
                        {4096, 1048576, &MPIR_Gather_intra},
                        {1048576, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    1,{{0, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    2,{{0, 131072, 0},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                },
                {64,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct},
                        {512, 4096, &MPIR_Gather_MV2_Direct},
                        {4096, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 512, &MPIR_Gather_MV2_Direct},
                        {512, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    3,{{0, 4096, 0},
                        {4096, 131072, USE_GATHER_LINEAR_PT_BINOMIAL},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                },
                {128,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct},
                        {1024, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 1024, &MPIR_Gather_MV2_Direct},
                        {1024, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    3,{{0, 8192, 0},
                        {8192, 131072, USE_GATHER_LINEAR_PT_BINOMIAL},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                },
                {256,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct},
                        {1024, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 1024, &MPIR_Gather_MV2_Direct},
                        {1024, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    3,{{0, 8192, 0},
                        {8192, 131072, USE_GATHER_LINEAR_PT_BINOMIAL},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                },
                {512,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct},
                        {512, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 512, &MPIR_Gather_MV2_Direct},
                        {512, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    3,{{0, 8192, 0},
                        {8192, 131072, USE_GATHER_LINEAR_PT_BINOMIAL},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                },
                {1024,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct},
                        {512, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 512, &MPIR_Gather_MV2_Direct},
                        {512, -1, &MPIR_Intra_node_LIMIC_Gather_MV2}},
                    3,{{0, 8192, 0},
                        {8192, 131072, USE_GATHER_LINEAR_PT_BINOMIAL},
                        {131072, -1, USE_GATHER_SINGLE_LEADER}}
                }                   
            };
            MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                    mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));

        } else if (g_smp_use_limic2 ) {
            mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
                {32,                         
                    2,{{0, 4096, &MPIR_Gather_MV2_Direct},
                        {4096, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, 0}}
                },
                {64,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 4096, &MPIR_Gather_MV2_Direct},
                        {4096, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                    1,{{0, -1, 0}}
                },
                {128,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct},
                        {1024, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, 0}}
                },
                {256,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                        {1024, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                    1,{{0, -1, 0}}
                },
                {512,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                    1,{{0, -1, 0}}
                },
                {1024,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 8192,&MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                    1,{{0, -1, 0}}
                }
            };
            MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                    mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));
        } 
        else                       
#endif /*if defined(_SMP_LIMIC_) */
        {
            mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
                {32,                         
                    2,{{0, 1048576, &MPIR_Gather_MV2_Direct},
                        {1048576, -1, &MPIR_Gather_intra}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}}},
                {64,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 1048576, &MPIR_Gather_MV2_Direct}, 
                        {1048576,-1, &MPIR_Gather_intra}}},
                {128,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                        {1024, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 1048576, &MPIR_Gather_MV2_Direct}, 
                        {1048576,-1, &MPIR_Gather_intra}}},
                {256,
                    3,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                        {1024, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    2,{{0, 1048576, &MPIR_Gather_MV2_Direct}, 
                        {1048576,-1, &MPIR_Gather_intra}}},
                {512,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}}}, 
                {1024,
                    3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                        {512, 8192, &MPIR_Gather_MV2_Direct},
                        {8192, -1, &MPIR_Gather_MV2_two_level_Direct}},
                    1,{{0, -1, &MPIR_Gather_MV2_Direct}}} 
            };
            MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                    mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));
        }

    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){

        mv2_size_gather_tuning_table=8;
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table*
                sizeof (mv2_gather_tuning_table)); 
#if defined(_SMP_LIMIC_)
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {12,
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, 0}}
            },
            {24,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, {512, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {48,                  
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {96,
                2,{{0, 2048, &MPIR_Gather_MV2_two_level_Direct}, {2048, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {192,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {384,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {768,
                2,{{0, 64, &MPIR_Gather_intra}, {64, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {1024,
                2,{{0, 32, &MPIR_Gather_intra}, {32, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}, 
                1,{{0, -1, 0}}
            }
        };

#else /*#if defined(_SMP_LIMIC_)*/
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {12,
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}},
            {24,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, {512, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {48,                  
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {96,
                2,{{0, 2048, &MPIR_Gather_MV2_two_level_Direct}, {2048, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {192,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {384,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, {1024, -1,
                    &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {768,
                2,{{0, 64, &MPIR_Gather_intra}, {64, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {1024,
                2,{{0, 32, &MPIR_Gather_intra}, {32, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}
            }
        };

#endif        
        MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));
    } else if(MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
              MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        mv2_size_gather_tuning_table=7;
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table*
                sizeof (mv2_gather_tuning_table)); 
#if defined(_SMP_LIMIC_)
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {16,
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, 0}}
            },
            {24,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512,-1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1,  &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {32,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                    {1024,-1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {128,
                2,{{0, 2048, &MPIR_Gather_MV2_two_level_Direct}, 
                    {2048,-1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {256,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}, 
                1,{{0, -1, 0}}
            },
            {512,
                3,{{0, 32, &MPIR_Gather_intra}, 
                    {32, 8196, &MPIR_Gather_MV2_two_level_Direct},
                    {8196, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}, 
                1,{{0, -1, 0}}
            },
            {1024,
                2,{{0, 32, &MPIR_Gather_intra}, 
                    {32, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}, 
                1,{{0, -1, 0}}
            }
        };
#else /*#if defined(_SMP_LIMIC_)*/
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {16,
                2,{{0, 524288, &MPIR_Gather_MV2_Direct},
                   {524288, -1, &MPIR_Gather_intra}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}},
            {32,
                3,{{0, 16384, &MPIR_Gather_MV2_Direct}, 
                    {16384, 131072, &MPIR_Gather_intra},
                    {131072, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {64,
                3,{{0, 256, &MPIR_Gather_MV2_two_level_Direct}, 
                    {256, 16384, &MPIR_Gather_MV2_Direct},
                    {256, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {128,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 16384, &MPIR_Gather_MV2_Direct},
                    {16384, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {256,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 16384, &MPIR_Gather_MV2_Direct},
                    {16384, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {512,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 16384, &MPIR_Gather_MV2_Direct},
                    {8196, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {1024,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 16384, &MPIR_Gather_MV2_Direct},
                    {8196, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
        };
#endif

        MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));
    } else
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    { 
        mv2_size_gather_tuning_table=7;
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table*
                sizeof (mv2_gather_tuning_table)); 
#if defined(_SMP_LIMIC_)
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {16,
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, 0}}
            },
            {24,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512,-1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1,  &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {32,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                    {1024,-1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {128,
                3,{{0, 2048, &MPIR_Gather_MV2_two_level_Direct}, 
                    {2048,65536, &MPIR_Gather_MV2_Direct},
                    {65536,-1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}},
                1,{{0, -1, 0}}
            },
            {256,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 65536, &MPIR_Gather_MV2_Direct},
                    {65536, -1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}}, 
                1,{{0, -1, 0}}
            },
            {512,
                4,{{0, 32, &MPIR_Gather_intra}, 
                    {32, 8196, &MPIR_Gather_MV2_two_level_Direct},
                    {8196, 65536, &MPIR_Gather_MV2_Direct},
                    {65536, -1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}}, 
                1,{{0, -1, 0}}
            },
            {1024,
                2,{{0, 32, &MPIR_Gather_intra}, 
                    {32, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}, 
                1,{{0, -1, 0}}
            }
        };
#else /*#if defined(_SMP_LIMIC_)*/
        mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[]={
            {16,
                1,{{0, -1, &MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}},
            {24,
                2,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, -1,&MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {32,
                2,{{0, 1024, &MPIR_Gather_MV2_two_level_Direct}, 
                    {1024, -1,&MPIR_Gather_MV2_Direct}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {128,
                3,{{0, 2048, &MPIR_Gather_MV2_two_level_Direct}, 
                    {2048, 65536, &MPIR_Gather_MV2_Direct},
                    {65536, -1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {256,
                3,{{0, 512, &MPIR_Gather_MV2_two_level_Direct}, 
                    {512, 65536, &MPIR_Gather_MV2_Direct},
                    {65536, -1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {512,
                4,{{0, 32, &MPIR_Gather_intra}, 
                    {32, 8196, &MPIR_Gather_MV2_two_level_Direct},
                    {8196, 65536, &MPIR_Gather_MV2_Direct},
                    {65536, -1, &MPIR_Gather_MV2_Direct_Blk}},
                1,{{0, -1, &MPIR_Gather_intra}}},
            {1024,
                2,{{0, 32, &MPIR_Gather_intra}, 
                    {32, -1, &MPIR_Gather_MV2_two_level_Direct}},
                1,{{0, -1, &MPIR_Gather_MV2_Direct}}
            }
        };
#endif

        MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table,
                mv2_size_gather_tuning_table * sizeof (mv2_gather_tuning_table));
    }

    return 0;
}

void MV2_cleanup_gather_tuning_table()
{ 
    if(mv2_gather_thresholds_table != NULL) { 
       MPIU_Free(mv2_gather_thresholds_table); 
    } 
    
} 

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}

#if defined(_SMP_LIMIC_)
int  MV2_intranode_multi_lvl_Gather_is_define(char *mv2_user_gather_inter,
                                              char *mv2_user_gather_intra, 
                                              char *mv2_user_gather_intra_multi_lvl)
{

    int i;
    int nb_element = count_sep(mv2_user_gather_inter) + 1;

    // If one gather tuning table is already defined 
    if(mv2_gather_thresholds_table != NULL) {
        MPIU_Free(mv2_gather_thresholds_table);
    }


    mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[1];
    mv2_size_gather_tuning_table = 1;
    
    // We realloc the space for the new gather tuning table
    mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table *
                                             sizeof (mv2_gather_tuning_table));

    if (nb_element == 1) {
        mv2_tmp_gather_thresholds_table[0].numproc = 1;
        mv2_tmp_gather_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_gather_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_gather_thresholds_table[0].inter_leader[0].max = -1;
        switch (atoi(mv2_user_gather_inter)) {
        case 1:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_intra;
            break;
        case 2:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
            break;
        case 3:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_two_level_Direct;
            break;
        default:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
        }
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_gather_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n",
                    mv2_user_gather_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n",
                    mv2_user_gather_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_gather_thresholds_table[0].numproc = 1;
        mv2_tmp_gather_thresholds_table[0].size_inter_table = nb_element;
        i = 0;
        for (p = strtok_r(dup, ",", &save_p); p;
                p = strtok_r(NULL, ",", &save_p)) {
            if (regexec(&preg, p, NMATCH, match, 0)) {
                fprintf(stderr, "failed to match on `%s'\n", p);
                regfree(&preg);
                MPIU_Free(dup);
                return 2;
            }
            /* given () start at 1 */
            switch (atoi(p + match[1].rm_so)) {
            case 1:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_intra;
                break;
            case 2:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
                break;
            case 3:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_two_level_Direct;
                break;
            default:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
            }
            mv2_tmp_gather_thresholds_table[0].inter_leader[i].min = atoi(p + match[2].rm_so);

            if (p[match[3].rm_so] == '+') {
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
            }

            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    
    mv2_tmp_gather_thresholds_table[0].size_intra_table = 1;
    if (mv2_user_gather_intra == NULL) {
        int multi_lvl_scheme = atoi(mv2_user_gather_intra_multi_lvl);

        if(multi_lvl_scheme >=1 && multi_lvl_scheme <=8){

            mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                &MPIR_Intra_node_LIMIC_Gather_MV2;
        } else {
            /*If mv2_user_gather_intra_multi_lvl == 0 or anyother value
             * we do use the any limic schemes. so use the default gather
             * algorithms*/
            mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                &MPIR_Gather_intra;
        }
    } else {
        /*Tuning for intra-node schemes*/
        int nb_intra_element = count_sep(mv2_user_gather_intra) + 1;

        // If one gather tuning table is already defined 
        if(mv2_gather_thresholds_table != NULL) {
            MPIU_Free(mv2_gather_thresholds_table);
        }


        //mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[1];
        mv2_size_gather_tuning_table = 1;

        // We realloc the space for the new gather tuning table
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table *
                sizeof (mv2_gather_tuning_table));

        if (nb_intra_element == 1) {
            mv2_tmp_gather_thresholds_table[0].numproc = 1;
            mv2_tmp_gather_thresholds_table[0].intra_node[0].min = 0;
            mv2_tmp_gather_thresholds_table[0].intra_node[0].max = -1;
            switch (atoi(mv2_user_gather_intra)) {
                case 2:
                    mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                        &MPIR_Intra_node_LIMIC_Gather_MV2;
                    break;
                case 0: 
                case 1:
                      /*0- Direct algo*/
                      /*1- Binomial algo*/

                     /*For limic gather schemes, we only use
                      * MPIR_Intra_node_LIMIC_Gather_MV2 for
                      * intra node communication. So all the other 
                      * intra node algo are just place holders*/
                default:          
                    mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                        &MPIR_Intra_node_LIMIC_Gather_MV2;
            }
        } else {
            char *dup, *p, *save_p;
            regmatch_t match[NMATCH];
            regex_t preg;
            const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

            if (!(dup = MPIU_Strdup(mv2_user_gather_intra))) {
                fprintf(stderr, "failed to duplicate `%s'\n",
                        mv2_user_gather_intra);
                return 1;
            }

            if (regcomp(&preg, regexp, REG_EXTENDED)) {
                fprintf(stderr, "failed to compile regexp `%s'\n",
                        mv2_user_gather_intra);
                MPIU_Free(dup);
                return 2;
            }

            mv2_tmp_gather_thresholds_table[0].numproc = 1;
            mv2_tmp_gather_thresholds_table[0].size_intra_table = nb_intra_element;
            i = 0;
            for (p = strtok_r(dup, ",", &save_p); p;
                    p = strtok_r(NULL, ",", &save_p)) {
                if (regexec(&preg, p, NMATCH, match, 0)) {
                    fprintf(stderr, "failed to match on `%s'\n", p);
                    regfree(&preg);
                    MPIU_Free(dup);
                    return 2;
                }
                /* given () start at 1 */
                switch (atoi(p + match[1].rm_so)) {
                    case 2:
                        mv2_tmp_gather_thresholds_table[0].intra_node[i].MV2_pt_Gather_function =
                            &MPIR_Intra_node_LIMIC_Gather_MV2;
                        break;
                    case 0:                                  
                    case 1:
                        /*0- Direct algo*/
                        /*1- Binomial algo*/

                        /*For limic gather schemes, we only use
                         * MPIR_Intra_node_LIMIC_Gather_MV2 for
                         * intra node communication. So all the other 
                         * intra node algo are just place holders*/
                    default:
                        mv2_tmp_gather_thresholds_table[0].intra_node[i].MV2_pt_Gather_function =
                            &MPIR_Intra_node_LIMIC_Gather_MV2;
                }
                mv2_tmp_gather_thresholds_table[0].intra_node[i].min = atoi(p + match[2].rm_so);

                if (p[match[3].rm_so] == '+') {
                    mv2_tmp_gather_thresholds_table[0].intra_node[i].max = -1;
                } else {
                    mv2_tmp_gather_thresholds_table[0].intra_node[i].max =
                        atoi(p + match[3].rm_so);
                }

                i++;
            }
            MPIU_Free(dup);
            regfree(&preg);
        }
    }

    /*Tuning for intra-node multi-leader limic schemes*/
    mv2_tmp_gather_thresholds_table[0].nb_limic_scheme = 1;
    if(mv2_user_gather_intra_multi_lvl == NULL) {
        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = 0; 
    } else {
        i=0;
        int nb_intra_multi_lvl_element = count_sep(mv2_user_gather_intra_multi_lvl) + 1;

        // If one gather tuning table is already defined 
        if(mv2_gather_thresholds_table != NULL) {
            MPIU_Free(mv2_gather_thresholds_table);
        }


        //mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[1];
        mv2_size_gather_tuning_table = 1;

        // We realloc the space for the new gather tuning table
        mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table *
                sizeof (mv2_gather_tuning_table));

        if (nb_intra_multi_lvl_element == 1) {
            mv2_tmp_gather_thresholds_table[0].numproc = 1;
            mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].min = 0;
            mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].max = -1;
            switch (atoi(mv2_user_gather_intra_multi_lvl)) {
                case 1:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_PT_PT_BINOMIAL;
                    break;
                case 2:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_PT_PT_DIRECT;
                    break;
                case 3:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_PT_LINEAR_BINOMIAL;
                    break;
                case 4:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_PT_LINEAR_DIRECT;
                    break;
                case 5:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_LINEAR_PT_BINOMIAL;
                    break;
                case 6:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_LINEAR_PT_DIRECT;
                    break;
                case 7:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_LINEAR_LINEAR;
                    break;
                case 8:
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = USE_GATHER_SINGLE_LEADER;
                    break;
                default:
                    /*None of the limic schemes are selected. Fallback to default mode*/
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[0].scheme = 0; 
            }
        } else {
            char *dup, *p, *save_p;
            regmatch_t match[NMATCH];
            regex_t preg;
            const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

            if (!(dup = MPIU_Strdup(mv2_user_gather_intra_multi_lvl))) {
                fprintf(stderr, "failed to duplicate `%s'\n",
                        mv2_user_gather_intra_multi_lvl);
                return 1;
            }

            if (regcomp(&preg, regexp, REG_EXTENDED)) {
                fprintf(stderr, "failed to compile regexp `%s'\n",
                        mv2_user_gather_intra_multi_lvl);
                MPIU_Free(dup);
                return 2;
            }

            mv2_tmp_gather_thresholds_table[0].numproc = 1;
            mv2_tmp_gather_thresholds_table[0].nb_limic_scheme= nb_intra_multi_lvl_element;
            i = 0;
            for (p = strtok_r(dup, ",", &save_p); p;
                    p = strtok_r(NULL, ",", &save_p)) {
                if (regexec(&preg, p, NMATCH, match, 0)) {
                    fprintf(stderr, "failed to match on `%s'\n", p);
                    regfree(&preg);
                    MPIU_Free(dup);
                    return 2;
                }
                /* given () start at 1 */
                switch (atoi(p + match[1].rm_so)) {
                    case 1:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_PT_PT_BINOMIAL;
                        break;
                    case 2:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_PT_PT_DIRECT;
                        break;
                    case 3:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_PT_LINEAR_BINOMIAL;
                        break;
                    case 4:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_PT_LINEAR_DIRECT;
                        break;
                    case 5:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_LINEAR_PT_BINOMIAL;
                        break;
                    case 6:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_LINEAR_PT_DIRECT;
                        break;
                    case 7:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_LINEAR_LINEAR;
                        break;
                    case 8:
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = USE_GATHER_SINGLE_LEADER;
                        break;
                    default:
                        /*None of the limic schemes are selected. Fallback to default mode*/
                        mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].scheme = 0; 
                }
                mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].min = atoi(p + match[2].rm_so);

                if (p[match[3].rm_so] == '+') {
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].max = -1;
                } else {
                    mv2_tmp_gather_thresholds_table[0].limic_gather_scheme[i].max =
                        atoi(p + match[3].rm_so);
                }

                i++;
            }
            MPIU_Free(dup);
            regfree(&preg);
        }
    }
    MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table, sizeof
           (mv2_gather_tuning_table));
    return 0;
}
#endif /*#if defined(_SMP_LIMIC_) */

int MV2_internode_Gather_is_define(char *mv2_user_gather_inter,
                                   char *mv2_user_gather_intra)
{

    int i;
    int nb_element = count_sep(mv2_user_gather_inter) + 1;

    // If one gather tuning table is already defined 
    if(mv2_gather_thresholds_table != NULL) {
        MPIU_Free(mv2_gather_thresholds_table);
    }


    mv2_gather_tuning_table mv2_tmp_gather_thresholds_table[1];
    mv2_size_gather_tuning_table = 1;
    
    // We realloc the space for the new gather tuning table
    mv2_gather_thresholds_table = MPIU_Malloc(mv2_size_gather_tuning_table *
                                             sizeof (mv2_gather_tuning_table));

    if (nb_element == 1) {
        mv2_tmp_gather_thresholds_table[0].numproc = 1;
        mv2_tmp_gather_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_gather_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_gather_thresholds_table[0].inter_leader[0].max = -1;
        switch (atoi(mv2_user_gather_inter)) {
        case 1:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_intra;
            break;
        case 2:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
            break;
        case 3:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_two_level_Direct;
            break;
        default:
            mv2_tmp_gather_thresholds_table[0].inter_leader[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
        }
        if (mv2_user_gather_intra == NULL) {
            mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
        } else {
            if (atoi(mv2_user_gather_intra) == 1) {
                mv2_gather_thresholds_table[0].
                intra_node[0].MV2_pt_Gather_function = &MPIR_Gather_intra;
            } else {
                mv2_gather_thresholds_table[0].
                intra_node[0].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
            }

        }
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_gather_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n",
                    mv2_user_gather_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n",
                    mv2_user_gather_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_gather_thresholds_table[0].numproc = 1;
        mv2_tmp_gather_thresholds_table[0].size_inter_table = nb_element;
        i = 0;
        for (p = strtok_r(dup, ",", &save_p); p;
                p = strtok_r(NULL, ",", &save_p)) {
            if (regexec(&preg, p, NMATCH, match, 0)) {
                fprintf(stderr, "failed to match on `%s'\n", p);
                regfree(&preg);
                MPIU_Free(dup);
                return 2;
            }
            /* given () start at 1 */
            switch (atoi(p + match[1].rm_so)) {
            case 1:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_intra;
                break;
            case 2:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
                break;
            case 3:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_two_level_Direct;
                break;
            default:
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
            }
            mv2_tmp_gather_thresholds_table[0].inter_leader[i].min = atoi(p + match[2].rm_so);

            if (p[match[3].rm_so] == '+') {
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_gather_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
            }

            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    mv2_tmp_gather_thresholds_table[0].size_intra_table = 1;
    if (mv2_user_gather_intra == NULL) {
        mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
            &MPIR_Gather_MV2_Direct;
    } else {
        if (atoi(mv2_user_gather_intra) == 1) {
            mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                &MPIR_Gather_intra;
        } else {
            mv2_tmp_gather_thresholds_table[0].intra_node[0].MV2_pt_Gather_function =
                &MPIR_Gather_MV2_Direct;
        }
    }
    MPIU_Memcpy(mv2_gather_thresholds_table, mv2_tmp_gather_thresholds_table, sizeof
           (mv2_gather_tuning_table));
    return 0;
}

int MV2_intranode_Gather_is_define(char *mv2_user_gather_intra)
{

    int i, j;
    for (i = 0; i < mv2_size_gather_tuning_table; i++) {
        for (j = 0; j < mv2_gather_thresholds_table[i].size_intra_table; j++) {
            if (atoi(mv2_user_gather_intra) == 1) {
                mv2_gather_thresholds_table[i].
                intra_node[j].MV2_pt_Gather_function = &MPIR_Gather_intra;
            } else {
                mv2_gather_thresholds_table[i].
                intra_node[j].MV2_pt_Gather_function =
                    &MPIR_Gather_MV2_Direct;
            }
        }
    }
    return 0;
}

void MV2_user_gather_switch_point_is_define(int mv2_user_gather_switch_point)
{
    int i;
    for (i = 0; i < mv2_size_gather_tuning_table; i++) {
        mv2_gather_thresholds_table[0].inter_leader[1].min =
            mv2_user_gather_switch_point;
        mv2_gather_thresholds_table[0].inter_leader[0].max =
            mv2_user_gather_switch_point;
    }
}

#endif /* if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
