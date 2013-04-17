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
#include "reduce_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"

enum {
    REDUCE_BINOMIAL = 1,  //1 MPIR_Reduce_binomial_MV2
    REDUCE_INTER_KNOMIAL, //2 MPIR_Reduce_inter_knomial_wrapper_MV2
    REDUCE_INTRA_KNOMIAL, //3 MPIR_Reduce_intra_knomial_wrapper_MV2
    REDUCE_SHMEM,         //4 MPIR_Reduce_shmem_MV2
    REDUCE_RDSC_GATHER,   //5 MPIR_Reduce_redscat_gather_MV2
};

int mv2_size_reduce_tuning_table = 0;
mv2_reduce_tuning_table *mv2_reduce_thresholds_table = NULL;

int MV2_set_reduce_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_reduce_tuning_table = 6;
        mv2_reduce_thresholds_table = MPIU_Malloc(mv2_size_reduce_tuning_table *
                                                  sizeof (mv2_reduce_tuning_table));
        mv2_reduce_tuning_table mv2_tmp_reduce_thresholds_table[] = {
            {
                12,
                4,
                4,
                {1, 1, 0, 0},
                4,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 32768, &MPIR_Reduce_binomial_MV2},
                    {32768, 262144,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 32768, &MPIR_Reduce_binomial_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {4096, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                24,
                4,
                4,
                {1, 1, 1, 0},
                4,
                {
                    {0, -1, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {0, 262144, &MPIR_Reduce_redscat_gather_MV2},
                    {262144, 1048576, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                48,
                4,
                4,
                {1, 1, 1, 0},
                4,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 131072,  &MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                96,
                4,
                4,
                {1, 1, 1, 0},
                4,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {4096, -1,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                },
            },
            {
                192,
                4,
                4,
                {1, 1, 1, 0},
                4,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {131072, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {16384, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                384,
                4,
                4,
                {1, 1, 1, 1, 0},
                5,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 8192,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 65536,&MPIR_Reduce_binomial_MV2},
                    {65536, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                3,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {4096, 8192, &MPIR_Reduce_binomial_MV2},
                    {16384, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
        }; 
        MPIU_Memcpy(mv2_reduce_thresholds_table, mv2_tmp_reduce_thresholds_table,
                  mv2_size_reduce_tuning_table * sizeof (mv2_reduce_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        /*Stampede*/
        mv2_size_reduce_tuning_table = 8;
        mv2_reduce_thresholds_table = MPIU_Malloc(mv2_size_reduce_tuning_table *
                                                  sizeof (mv2_reduce_tuning_table));
        mv2_reduce_tuning_table mv2_tmp_reduce_thresholds_table[] = {
            {
                16,
                4,
                4,
                {1, 0, 0},
                3,
                {
                    {0, 262144, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {262144, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536,-1,  &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                32,
                4,
                4,
                {1, 1, 1, 1, 0, 0, 0},
                7,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 32768, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {32768, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {262144, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                6,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 32768, &MPIR_Reduce_shmem_MV2},
                    {32768, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_shmem_MV2},
                    {262144,-1,  &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                64,
                4,
                4,
                {1, 1, 1, 1, 0},
                5,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                5,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                128,
                4,
                4,
                {1, 0, 1, 0, 1, 0},
                6,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {262144, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                5,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                256,
                4,
                4,
                {1, 1, 1, 0, 1, 1, 0},
                7,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 32768, &MPIR_Reduce_binomial_MV2},
                    {32768, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_binomial_MV2},
                    {262144, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                6,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 32768, &MPIR_Reduce_shmem_MV2},
                    {32768, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                512,
                4,
                4,
                {1, 0, 1, 1, 1, 0},
                6,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_binomial_MV2},
                    {262144, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                5,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                1024,
                4,
                4,
                {1, 0, 1, 1, 1},
                5,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 16384, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 262144, &MPIR_Reduce_binomial_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
                5,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, 65536, &MPIR_Reduce_shmem_MV2},
                    {65536, 262144, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {262144, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                2048,
                4,
                4,
                {1, 0, 1, 1, 1,1},
                6,
                {
                    {0, 2048, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {2048, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384, &MPIR_Reduce_binomial_MV2},
                    {16384, 65536, &MPIR_Reduce_binomial_MV2},
                    {65536, 131072, &MPIR_Reduce_binomial_MV2},
                    {131072, -1, &MPIR_Reduce_binomial_MV2},
                },
                6,
                {
                    {0, 2048, &MPIR_Reduce_shmem_MV2},
                    {2048, 4096, &MPIR_Reduce_shmem_MV2},
                    {4096, 16384, &MPIR_Reduce_shmem_MV2},
                    {16384, 65536, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {65536, 131072, &MPIR_Reduce_binomial_MV2},
                    {131072, -1, &MPIR_Reduce_shmem_MV2},
                },
            },

        }; 
        MPIU_Memcpy(mv2_reduce_thresholds_table, mv2_tmp_reduce_thresholds_table,
                  mv2_size_reduce_tuning_table * sizeof (mv2_reduce_tuning_table));
    } else

#endif /* (_OSU_MVAPICH_) && !defined(_OSU_PSM_) */
    {
        mv2_size_reduce_tuning_table = 7;
        mv2_reduce_thresholds_table = MPIU_Malloc(mv2_size_reduce_tuning_table *
                                                  sizeof (mv2_reduce_tuning_table));
        mv2_reduce_tuning_table mv2_tmp_reduce_thresholds_table[] = {
            {
                8,
                4,
                4,
                {1, 0, 0},
                3,
                {
                    {0, 524288, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {524288, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 131072, &MPIR_Reduce_shmem_MV2},
                    {131072, -1, &MPIR_Reduce_binomial_MV2},
                },
            },
            {
                16,
                4,
                4,
                {1, 0},
                2,
                {
                    {0, 1048576, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                2,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                32,
                4,
                4,
                {1, 1, 1, 0},
                4,
                {
                    {0, 8192, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {8192, 131072,  &MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_redscat_gather_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                3,
                {
                    {0, 8192, &MPIR_Reduce_shmem_MV2},
                    {8192, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                64,
                4,
                4,
                {1, 0, 1, 1, 0},
                5,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 131072,&MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_redscat_gather_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                4,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                128,
                4,
                4,
                {1, 0, 1, 1, 0},
                5,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 131072,&MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                4,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                256,
                4,
                4,
                {1, 0, 1, 1, 0},
                5,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 131072,&MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                4,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },
            {
                512,
                4,
                4,
                {1, 0, 1, 1, 0},
                5,
                {
                    {0, 4096, &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {4096, 16384,  &MPIR_Reduce_inter_knomial_wrapper_MV2},
                    {16384, 131072,&MPIR_Reduce_binomial_MV2},
                    {131072, 1048576, &MPIR_Reduce_binomial_MV2},
                    {1048576, -1, &MPIR_Reduce_redscat_gather_MV2},
                },
                4,
                {
                    {0, 4096, &MPIR_Reduce_shmem_MV2},
                    {8192, 16384, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                    {16384, -1, &MPIR_Reduce_intra_knomial_wrapper_MV2},
                },
            },

        };
        MPIU_Memcpy(mv2_reduce_thresholds_table, mv2_tmp_reduce_thresholds_table,
                  mv2_size_reduce_tuning_table * sizeof (mv2_reduce_tuning_table));

    }
    return 0;
}

void MV2_cleanup_reduce_tuning_table()
{
    if (mv2_reduce_thresholds_table != NULL) {
        MPIU_Free(mv2_reduce_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}


int MV2_internode_Reduce_is_define(char *mv2_user_reduce_inter, char
                                    *mv2_user_reduce_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_reduce_inter) + 1;

    /* If one reduce tuning table is already defined */
    if (mv2_reduce_thresholds_table != NULL) {
        MPIU_Free(mv2_reduce_thresholds_table);
    }

    mv2_reduce_tuning_table mv2_tmp_reduce_thresholds_table[1];
    mv2_size_reduce_tuning_table = 1;

    /* We realloc the space for the new reduce tuning table */
    mv2_reduce_thresholds_table = MPIU_Malloc(mv2_size_reduce_tuning_table *
                                             sizeof (mv2_reduce_tuning_table));

    if (nb_element == 1) {

        if(mv2_user_reduce_two_level == 1){
            mv2_tmp_reduce_thresholds_table[0].is_two_level_reduce[0]=1;
        } else {
            mv2_tmp_reduce_thresholds_table[0].is_two_level_reduce[0]=0;
        }

        mv2_tmp_reduce_thresholds_table[0].numproc = 1;
        mv2_tmp_reduce_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_reduce_thresholds_table[0].size_intra_table = 1;
        mv2_tmp_reduce_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_reduce_thresholds_table[0].inter_leader[0].max = -1;
        mv2_tmp_reduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_tmp_reduce_thresholds_table[0].intra_node[0].max = -1;
        mv2_tmp_reduce_thresholds_table[0].intra_k_degree = 4;
        mv2_tmp_reduce_thresholds_table[0].inter_k_degree = 4;
    
        switch (atoi(mv2_user_reduce_inter)) {
        case REDUCE_BINOMIAL:
            mv2_tmp_reduce_thresholds_table[0].inter_leader[0].MV2_pt_Reduce_function =
                &MPIR_Reduce_binomial_MV2;
            break;
        case REDUCE_INTER_KNOMIAL:
            mv2_tmp_reduce_thresholds_table[0].inter_leader[0].MV2_pt_Reduce_function =
                &MPIR_Reduce_inter_knomial_wrapper_MV2;
            break;
        case REDUCE_RDSC_GATHER:
            mv2_tmp_reduce_thresholds_table[0].inter_leader[0].MV2_pt_Reduce_function =
                &MPIR_Reduce_redscat_gather_MV2;
            break;
        default:
            mv2_tmp_reduce_thresholds_table[0].inter_leader[0].MV2_pt_Reduce_function =
                &MPIR_Reduce_inter_knomial_wrapper_MV2;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_reduce_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_reduce_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_reduce_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_reduce_thresholds_table[0].numproc = 1;
        mv2_tmp_reduce_thresholds_table[0].size_inter_table = nb_element;
        mv2_tmp_reduce_thresholds_table[0].size_intra_table = 2;
        mv2_tmp_reduce_thresholds_table[0].intra_k_degree = 4;
        mv2_tmp_reduce_thresholds_table[0].inter_k_degree = 4;

        i = 0;
        for (p = strtok_r(dup, ",", &save_p); p; p = strtok_r(NULL, ",", &save_p)) {
            if (regexec(&preg, p, NMATCH, match, 0)) {
                fprintf(stderr, "failed to match on `%s'\n", p);
                regfree(&preg);
                MPIU_Free(dup);
                return 2;
            }
            /* given () start at 1 */
            switch (atoi(p + match[1].rm_so)) {
                case REDUCE_BINOMIAL:
                    mv2_tmp_reduce_thresholds_table[0].inter_leader[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_binomial_MV2;
                    break;
                case REDUCE_INTER_KNOMIAL:
                    mv2_tmp_reduce_thresholds_table[0].inter_leader[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_inter_knomial_wrapper_MV2;
                    break;
                case REDUCE_RDSC_GATHER:
                    mv2_tmp_reduce_thresholds_table[0].inter_leader[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_redscat_gather_MV2;
                    break;
                default:
                    mv2_tmp_reduce_thresholds_table[0].inter_leader[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_inter_knomial_wrapper_MV2;
            }

            mv2_tmp_reduce_thresholds_table[0].inter_leader[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_tmp_reduce_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_reduce_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
                }
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    mv2_tmp_reduce_thresholds_table[0].size_intra_table = 2;

    MPIU_Memcpy(mv2_reduce_thresholds_table, mv2_tmp_reduce_thresholds_table, sizeof
                (mv2_reduce_tuning_table));
    if (mv2_user_reduce_intra != NULL) {
        MV2_intranode_Reduce_is_define(mv2_user_reduce_intra);
    } else {
        mv2_reduce_thresholds_table[0].size_intra_table = 1;
        mv2_reduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_reduce_thresholds_table[0].intra_node[0].max = -1;
        mv2_reduce_thresholds_table[0].intra_node[0].MV2_pt_Reduce_function =
            &MPIR_Reduce_binomial_MV2;
   }

    return 0;
}


int MV2_intranode_Reduce_is_define(char *mv2_user_reduce_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_reduce_intra) + 1;

    if (nb_element == 1) {
        mv2_reduce_thresholds_table[0].size_intra_table = 1;
        mv2_reduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_reduce_thresholds_table[0].intra_node[0].max = -1;
        mv2_reduce_thresholds_table[0].intra_k_degree = 4;
        mv2_reduce_thresholds_table[0].inter_k_degree = 4;

        if(mv2_user_reduce_two_level == 1){
            mv2_reduce_thresholds_table[0].is_two_level_reduce[0]=1;
        } else {
            mv2_reduce_thresholds_table[0].is_two_level_reduce[0]=0;
        }
    
        switch (atoi(mv2_user_reduce_intra)) {
            case REDUCE_BINOMIAL:
                mv2_reduce_thresholds_table[0].intra_node[0].MV2_pt_Reduce_function =
                    &MPIR_Reduce_binomial_MV2;
                break;
            case REDUCE_INTRA_KNOMIAL:
                mv2_reduce_thresholds_table[0].intra_node[0].MV2_pt_Reduce_function =
                    &MPIR_Reduce_intra_knomial_wrapper_MV2;
                break;
            case REDUCE_SHMEM:
                mv2_reduce_thresholds_table[0].intra_node[0].MV2_pt_Reduce_function =
                    &MPIR_Reduce_shmem_MV2;
                break;
            default:
                mv2_reduce_thresholds_table[0].intra_node[0].MV2_pt_Reduce_function =
                    &MPIR_Reduce_intra_knomial_wrapper_MV2;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_reduce_intra))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_reduce_intra);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_reduce_intra);
            MPIU_Free(dup);
            return 2;
        }

        mv2_reduce_thresholds_table[0].numproc = 1;
        mv2_reduce_thresholds_table[0].size_intra_table = 2;
        mv2_reduce_thresholds_table[0].intra_k_degree = 4;
        mv2_reduce_thresholds_table[0].inter_k_degree = 4;

        i = 0;
        for (p = strtok_r(dup, ",", &save_p); p; p = strtok_r(NULL, ",", &save_p)) {
            if (regexec(&preg, p, NMATCH, match, 0)) {
                fprintf(stderr, "failed to match on `%s'\n", p);
                regfree(&preg);
                MPIU_Free(dup);
                return 2;
            }
            /* given () start at 1 */
            switch (atoi(p + match[1].rm_so)) {
                case REDUCE_BINOMIAL:
                    mv2_reduce_thresholds_table[0].intra_node[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_binomial_MV2;
                    break;
                case REDUCE_INTRA_KNOMIAL:
                    mv2_reduce_thresholds_table[0].intra_node[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_intra_knomial_wrapper_MV2;
                    break;
                case REDUCE_SHMEM:
                    mv2_reduce_thresholds_table[0].intra_node[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_shmem_MV2;
                    break;
                default:
                    mv2_reduce_thresholds_table[0].intra_node[i].MV2_pt_Reduce_function =
                        &MPIR_Reduce_intra_knomial_wrapper_MV2;
            }

            mv2_reduce_thresholds_table[0].intra_node[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_reduce_thresholds_table[0].intra_node[i].max = -1;
            } else {
                mv2_reduce_thresholds_table[0].intra_node[i].max =
                    atoi(p + match[3].rm_so);
            }
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    return 0;
}


#endif                          /* if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
