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
#include "scatter_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"

/*
  1 MV2_INTER_SCATTER_TUNING=1
  2 MV2_INTER_SCATTER_TUNING=2
  3 MV2_INTER_SCATTER_TUNING=3 MV2_INTRA_SCATTER_TUNING=1
  4 MV2_INTER_SCATTER_TUNING=3 MV2_INTRA_SCATTER_TUNING=2
  5 MV2_INTER_SCATTER_TUNING=4 MV2_INTRA_SCATTER_TUNING=1
  6 MV2_INTER_SCATTER_TUNING=4 MV2_INTRA_SCATTER_TUNING=2

64k-256k	6
16k-64k	2
1k-16k	6
128-1024	5
0-128	3
*/


enum {
    SCATTER_BINOMIAL = 1,             //1 &MPIR_Scatter_MV2_Binomial
    SCATTER_DIRECT,                   //2 &MPIR_Scatter_MV2_Direct
    SCATTER_TWO_LEVEL_BINOMIAL,       //3 &MPIR_Scatter_MV2_two_level_Binomial
    SCATTER_TWO_LEVEL_DIRECT,         //4 &MPIR_Scatter_MV2_two_level_Direct
    SCATTER_MCAST,                    //5 &MPIR_Scatter_mcst_wrap_MV2
};

int mv2_size_scatter_tuning_table = 0;
mv2_scatter_tuning_table *mv2_scatter_thresholds_table = NULL;

int MV2_set_scatter_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_scatter_tuning_table = 6;
        mv2_scatter_thresholds_table = MPIU_Malloc(mv2_size_scatter_tuning_table *
                                                  sizeof(mv2_scatter_tuning_table));
        MPIU_Memset(mv2_scatter_thresholds_table, 0, mv2_size_scatter_tuning_table *
                    sizeof(mv2_scatter_tuning_table)); 
        mv2_scatter_tuning_table mv2_tmp_scatter_thresholds_table[] = {
            {
                12,
                2,
                { 
                    {0, 512, &MPIR_Scatter_MV2_Binomial}, 
                    {512, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                24,
                2,
                {
                    {0, 512, &MPIR_Scatter_MV2_Binomial}, 
                    {512, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                48,
                2,
                {
                    {0, 512, &MPIR_Scatter_MV2_Binomial},
                    {512, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                96,
                3,
                {
                    {0, 256, &MPIR_Scatter_MV2_two_level_Direct},
                    {256, 8192, &MPIR_Scatter_MV2_two_level_Direct},
                    {8192, -1, &MPIR_Scatter_MV2_Direct},
                },
                2,
                {
                    { 0, 256, &MPIR_Scatter_MV2_Binomial},
                    { 256, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                192,
                3,
                {
                    {1, 2, &MPIR_Scatter_MV2_Binomial},
                    {2, 2048, &MPIR_Scatter_MV2_two_level_Direct},
                    {2048, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Binomial},
                },
            },

            {
                384,
                3,
                {
                    {1, 32, &MPIR_Scatter_MV2_Binomial},
                    {32, 4096, &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Binomial},
                },  
            },  
        };
    
        MPIU_Memcpy(mv2_scatter_thresholds_table, mv2_tmp_scatter_thresholds_table,
                  mv2_size_scatter_tuning_table * sizeof (mv2_scatter_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        /*Stampede,*/
        mv2_size_scatter_tuning_table = 8;
        mv2_scatter_thresholds_table = MPIU_Malloc(mv2_size_scatter_tuning_table *
                                                  sizeof(mv2_scatter_tuning_table));
        MPIU_Memset(mv2_scatter_thresholds_table, 0, mv2_size_scatter_tuning_table * 
                    sizeof(mv2_scatter_tuning_table)); 
        mv2_scatter_tuning_table mv2_tmp_scatter_thresholds_table[] = {
            {
                16,
                2,
                { 
                    {0, 256, &MPIR_Scatter_MV2_Binomial}, 
                    {256, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                32,
                2,
                {
                    {0, 512, &MPIR_Scatter_MV2_Binomial}, 
                    {512, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                64,
                2,
                {
                    {0, 1024, &MPIR_Scatter_MV2_two_level_Direct},
                    {1024, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                128,
                4,
                {
                    {0, 16, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 16, &MPIR_Scatter_MV2_two_level_Direct},
                    {16, 2048, &MPIR_Scatter_MV2_two_level_Direct},
                    {2048, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                256,
                4,
                {
                    {0, 16, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 16, &MPIR_Scatter_MV2_two_level_Direct},
                    {16, 2048, &MPIR_Scatter_MV2_two_level_Direct},
                    {2048, -1,  &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                512,
                4,
                {
                    {0, 16, &MPIR_Scatter_mcst_wrap_MV2},
                    {16, 16, &MPIR_Scatter_MV2_two_level_Direct},
                    {16, 4096, &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Binomial},
                }, 
            },  
            {
                1024,
                5,
                {
                    {0, 16, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 16,  &MPIR_Scatter_MV2_Binomial},
                    {16, 32, &MPIR_Scatter_MV2_Binomial},
                    {32, 4096, &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, -1, &MPIR_Scatter_MV2_Direct},
                },
                1,
                {
                    { 0, -1, &MPIR_Scatter_MV2_Binomial},
                },  
            },  
            {
                2048,
                7,
                {
                    {0, 16, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 16,  &MPIR_Scatter_MV2_two_level_Binomial},
                    {16, 128, &MPIR_Scatter_MV2_two_level_Binomial},
                    {128, 1024, &MPIR_Scatter_MV2_two_level_Direct},
                    {1024, 16384, &MPIR_Scatter_MV2_two_level_Direct},
                    {16384, 65536, &MPIR_Scatter_MV2_Direct},
                    {65536, -1, &MPIR_Scatter_MV2_two_level_Direct},
                },
                6,
                {
                    {0, 16, &MPIR_Scatter_MV2_Binomial},
                    {16, 128, &MPIR_Scatter_MV2_Binomial},
                    {128, 1024, &MPIR_Scatter_MV2_Binomial},
                    {1024, 16384, &MPIR_Scatter_MV2_Direct},
                    {16384, 65536, &MPIR_Scatter_MV2_Direct},
                    {65536, -1, &MPIR_Scatter_MV2_Direct},
                },
            }, 
        };
    
        MPIU_Memcpy(mv2_scatter_thresholds_table, mv2_tmp_scatter_thresholds_table,
                  mv2_size_scatter_tuning_table * sizeof (mv2_scatter_tuning_table));
    } else

#endif /* (_OSU_MVAPICH_) && !defined(_OSU_PSM_) */
    {
        mv2_size_scatter_tuning_table = 7;
        mv2_scatter_thresholds_table = MPIU_Malloc(mv2_size_scatter_tuning_table *
                                                  sizeof(mv2_scatter_tuning_table));
        MPIU_Memset(mv2_scatter_thresholds_table, 0, mv2_size_scatter_tuning_table * 
                    sizeof(mv2_scatter_tuning_table)); 
        mv2_scatter_tuning_table mv2_tmp_scatter_thresholds_table[] = {
            {
                8,
                2,
                { 
                    {0, 256, &MPIR_Scatter_MV2_Binomial}, 
                    {256, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                16,
                2,
                {
                    {0, 512, &MPIR_Scatter_MV2_Binomial}, 
                    {512, -1, &MPIR_Scatter_MV2_Direct},
                },
                1, 
                { 
                    { 0, -1, &MPIR_Scatter_MV2_Direct},
                },
            },

            {
                32,
                3,
                {
                    {0, 256, &MPIR_Scatter_MV2_two_level_Direct},
                    {256, 2048, &MPIR_Scatter_MV2_two_level_Direct},
                    {2048, -1,  &MPIR_Scatter_MV2_Direct},
                },
                2,
                {
                    { 0, 256, &MPIR_Scatter_MV2_Direct},
                    { 256, -1, &MPIR_Scatter_MV2_Binomial},
                },
            },

            {
                64,
                5,
                {
                    {0, 32, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 32, &MPIR_Scatter_MV2_two_level_Direct},
                    {32, 256, &MPIR_Scatter_MV2_two_level_Direct},
                    {256, 2048,  &MPIR_Scatter_MV2_two_level_Direct},
                    {2048, -1, &MPIR_Scatter_MV2_Direct},
                },
                2,
                {
                    { 0, 256, &MPIR_Scatter_MV2_Direct},
                    { 256, -1, &MPIR_Scatter_MV2_Binomial},
                },
            },

            {
                128,
                4,
                {
                    {0, 64, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 64, &MPIR_Scatter_MV2_Binomial},
                    {64, 4096, &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, -1, &MPIR_Scatter_MV2_Direct},
                },
                2,
                {
                    { 0, 1024, &MPIR_Scatter_MV2_Direct},
                    { 1024, -1, &MPIR_Scatter_MV2_Binomial},
                },
            },

            {
                256,
                5,
                {
                    {0, 64, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 64, &MPIR_Scatter_MV2_Binomial},
                    {64, 256,  &MPIR_Scatter_MV2_two_level_Direct},
                    {256, 4096,  &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, -1, &MPIR_Scatter_MV2_Direct},
                },
                2,
                {
                    { 0, 256, &MPIR_Scatter_MV2_Binomial},
                    { 256, -1, &MPIR_Scatter_MV2_Binomial},
                },  
            },  

            {
                512,
                5,
                {
                    {0, 32, &MPIR_Scatter_mcst_wrap_MV2},
                    {0, 32, &MPIR_Scatter_MV2_Binomial},
                    {32, 4096, &MPIR_Scatter_MV2_two_level_Direct},
                    {4096, 32768, &MPIR_Scatter_MV2_Direct},
                    {32768, -1, &MPIR_Scatter_MV2_two_level_Direct},
                },
                2,
                {
                    { 0, 1024, &MPIR_Scatter_MV2_Binomial},
                    { 1024, -1, &MPIR_Scatter_MV2_Binomial},
                },
            },
        };
    
        MPIU_Memcpy(mv2_scatter_thresholds_table, mv2_tmp_scatter_thresholds_table,
                  mv2_size_scatter_tuning_table * sizeof (mv2_scatter_tuning_table));

    }
    return 0;
}

void MV2_cleanup_scatter_tuning_table()
{
    if (mv2_scatter_thresholds_table != NULL) {
        MPIU_Free(mv2_scatter_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}


int MV2_internode_Scatter_is_define(char *mv2_user_scatter_inter, char
                                    *mv2_user_scatter_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_scatter_inter) + 1;

    /* If one scatter tuning table is already defined */
    if (mv2_scatter_thresholds_table != NULL) {
        MPIU_Free(mv2_scatter_thresholds_table);
    }

    mv2_scatter_tuning_table mv2_tmp_scatter_thresholds_table[1];
    mv2_size_scatter_tuning_table = 1;

    /* We realloc the space for the new scatter tuning table */
    mv2_scatter_thresholds_table = MPIU_Malloc(mv2_size_scatter_tuning_table *
                                             sizeof(mv2_scatter_tuning_table));

    MPIU_Memset(mv2_scatter_thresholds_table, 0, mv2_size_scatter_tuning_table * 
                sizeof(mv2_scatter_tuning_table)); 
    MPIU_Memset(&mv2_tmp_scatter_thresholds_table, 0, sizeof(mv2_scatter_tuning_table)); 
    if (nb_element == 1) {
        mv2_tmp_scatter_thresholds_table[0].numproc = 1;
        mv2_tmp_scatter_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_scatter_thresholds_table[0].size_intra_table = 1;
        mv2_tmp_scatter_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_scatter_thresholds_table[0].inter_leader[0].max = -1;
        mv2_tmp_scatter_thresholds_table[0].intra_node[0].min = 0;
        mv2_tmp_scatter_thresholds_table[0].intra_node[0].max = -1;
    
        switch (atoi(mv2_user_scatter_inter)) {
        case SCATTER_BINOMIAL:
            mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Binomial;
            break;
        case SCATTER_DIRECT:
            mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Direct;
            break;
        case SCATTER_TWO_LEVEL_BINOMIAL:
            mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_two_level_Binomial;
            break;
        case SCATTER_TWO_LEVEL_DIRECT:
            mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_two_level_Direct;
            break;
#if defined(_MCST_SUPPORT_)
        case SCATTER_MCAST:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                    &MPIR_Scatter_mcst_wrap_MV2;
                break;
#endif /* #if defined(_MCST_SUPPORT_) */
        default:
            mv2_tmp_scatter_thresholds_table[0].inter_leader[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Binomial;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_scatter_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_scatter_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_scatter_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_scatter_thresholds_table[0].numproc = 1;
        mv2_tmp_scatter_thresholds_table[0].size_inter_table = nb_element;
        mv2_tmp_scatter_thresholds_table[0].size_intra_table = 2;
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

            case SCATTER_BINOMIAL:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Binomial;
                break;
            case SCATTER_DIRECT:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Direct;
                break;
            case SCATTER_TWO_LEVEL_BINOMIAL:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_two_level_Binomial;
                break;
            case SCATTER_TWO_LEVEL_DIRECT:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_two_level_Direct;
                break;
#if defined(_MCST_SUPPORT_)
            case SCATTER_MCAST:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_mcst_wrap_MV2;
                break;
#endif /* #if defined(_MCST_SUPPORT_) */
            default:
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Binomial;
            }

            mv2_tmp_scatter_thresholds_table[0].inter_leader[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_scatter_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
                }
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    mv2_tmp_scatter_thresholds_table[0].size_intra_table = 2;

    MPIU_Memcpy(mv2_scatter_thresholds_table, mv2_tmp_scatter_thresholds_table, sizeof
                (mv2_scatter_tuning_table));
    if (mv2_user_scatter_intra != NULL) {
        MV2_intranode_Scatter_is_define(mv2_user_scatter_intra);
    } else {
        mv2_scatter_thresholds_table[0].size_intra_table = 1;
        mv2_scatter_thresholds_table[0].intra_node[0].min = 0;
        mv2_scatter_thresholds_table[0].intra_node[0].max = -1;
        mv2_scatter_thresholds_table[0].intra_node[0].MV2_pt_Scatter_function =
            &MPIR_Scatter_MV2_Direct;
   }

    return 0;
}


int MV2_intranode_Scatter_is_define(char *mv2_user_scatter_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_scatter_intra) + 1;

    if (nb_element == 1) {
        mv2_scatter_thresholds_table[0].size_intra_table = 1;
        mv2_scatter_thresholds_table[0].intra_node[0].min = 0;
        mv2_scatter_thresholds_table[0].intra_node[0].max = -1;
    
        switch (atoi(mv2_user_scatter_intra)) {
        case SCATTER_DIRECT:
            mv2_scatter_thresholds_table[0].intra_node[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Direct;
            break;
        case SCATTER_BINOMIAL:
            mv2_scatter_thresholds_table[0].intra_node[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Binomial;
            break;
        default:
            mv2_scatter_thresholds_table[0].intra_node[0].MV2_pt_Scatter_function =
                &MPIR_Scatter_MV2_Direct;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_scatter_intra))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_scatter_intra);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_scatter_intra);
            MPIU_Free(dup);
            return 2;
        }

        mv2_scatter_thresholds_table[0].numproc = 1;
        mv2_scatter_thresholds_table[0].size_intra_table = 2;
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

            case SCATTER_DIRECT:
                mv2_scatter_thresholds_table[0].intra_node[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Direct;
                break;
            case SCATTER_BINOMIAL:
                mv2_scatter_thresholds_table[0].intra_node[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Binomial;
                break;
            default:
                mv2_scatter_thresholds_table[0].intra_node[i].MV2_pt_Scatter_function =
                    &MPIR_Scatter_MV2_Direct;
            }

            mv2_scatter_thresholds_table[0].intra_node[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_scatter_thresholds_table[0].intra_node[i].max = -1;
            } else {
                mv2_scatter_thresholds_table[0].intra_node[i].max =
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
