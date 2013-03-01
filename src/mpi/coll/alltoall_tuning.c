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
#include "alltoall_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"
/* array used to tune alltoall */

int mv2_size_alltoall_tuning_table = 0;
mv2_alltoall_tuning_table *mv2_alltoall_thresholds_table = NULL;

int MV2_set_alltoall_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_alltoall_tuning_table = 6;
        mv2_alltoall_thresholds_table = MPIU_Malloc(mv2_size_alltoall_tuning_table *
                                                    sizeof (mv2_alltoall_tuning_table));
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table[] = {
            {12,
                2, 
                {{0, 65536, &MPIR_Alltoall_Scatter_dest_MV2},
                {65536, -1,  &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{32768, -1, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {24,
                3,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, 131072, &MPIR_Alltoall_Scatter_dest_MV2},
                {131072, -1,  &MPIR_Alltoall_pairwise_MV2},
                },
                
                {{16384, -1, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {48,
                3,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, 131072, &MPIR_Alltoall_Scatter_dest_MV2},
                {131072, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{32768, 131072, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {96,
                2,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384,65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {192,
                2,
                {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                {1024, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384, 65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {384,
                2,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384, 65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
        };
        MPIU_Memcpy(mv2_alltoall_thresholds_table, mv2_tmp_alltoall_thresholds_table,
                    mv2_size_alltoall_tuning_table * sizeof (mv2_alltoall_tuning_table));

    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        mv2_size_alltoall_tuning_table = 7;
        mv2_alltoall_thresholds_table = MPIU_Malloc(mv2_size_alltoall_tuning_table *
                                                    sizeof (mv2_alltoall_tuning_table));
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table[] = {
            {16,
                2, 
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, -1,  &MPIR_Alltoall_Scatter_dest_MV2},
                },
  
                {{32768, -1, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {32,
                2,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
                },
                
                {{16384, -1, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {64,
                3,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, 16384, &MPIR_Alltoall_Scatter_dest_MV2},
                {16384, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{32768, 131072, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {128,
                2,
                {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                {2048, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384,65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {256,
                2,
                {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                {1024, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384, 65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
            {512,
                2,
                {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                {1024, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384, 65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
            {1024,
                2,
                {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                {1024, -1, &MPIR_Alltoall_pairwise_MV2},
                },
  
                {{16384, 65536, &MPIR_Alltoall_inplace_MV2},
                },
            },
  
        };
        MPIU_Memcpy(mv2_alltoall_thresholds_table, mv2_tmp_alltoall_thresholds_table,
                    mv2_size_alltoall_tuning_table * sizeof (mv2_alltoall_tuning_table));

    } else

#endif /* (_OSU_MVAPICH_) && !defined(_OSU_PSM_) */
    {
        mv2_size_alltoall_tuning_table = 7;
        mv2_alltoall_thresholds_table = MPIU_Malloc(mv2_size_alltoall_tuning_table *
                                                    sizeof (mv2_alltoall_tuning_table));
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table[] = {
             {8,
                 1, 
                 {{0, -1, &MPIR_Alltoall_Scatter_dest_MV2},
                 },
  
                 {{65536, -1, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {16,
                 2,
                 {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                 {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
                 },
                 
                 {{65536, -1, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {32,
                 2,
                 {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                 {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
                 },
  
                 {{16384, 262144, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {64,
                 3,
                 {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                 {2048, 8192, &MPIR_Alltoall_Scatter_dest_MV2},
                 {8192, -1, &MPIR_Alltoall_pairwise_MV2},
                 },
  
                 {{16384,131072, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {128,
                 3,
                 {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                 {1024, 4096, &MPIR_Alltoall_Scatter_dest_MV2},
                 {16384, -1, &MPIR_Alltoall_pairwise_MV2},
                 },
  
                 {{16384,131072, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {256,
                 2,
                 {{0, 2048, &MPIR_Alltoall_bruck_MV2},
                 {2048, -1, &MPIR_Alltoall_pairwise_MV2},
                 },
  
                 {{16384,131072, &MPIR_Alltoall_inplace_MV2},
                 },
             },
  
             {512,
                 2,
                 {{0, 1024, &MPIR_Alltoall_bruck_MV2},
                 {1024, -1, &MPIR_Alltoall_pairwise_MV2},
                 },
  
                 {{16384, -1, &MPIR_Alltoall_inplace_MV2},
                 },
             },
        };
        MPIU_Memcpy(mv2_alltoall_thresholds_table, mv2_tmp_alltoall_thresholds_table,
                    mv2_size_alltoall_tuning_table * sizeof (mv2_alltoall_tuning_table));
            
    }
    return 0;
}

void MV2_cleanup_alltoall_tuning_table()
{
    if (mv2_alltoall_thresholds_table != NULL) {
        MPIU_Free(mv2_alltoall_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}

int MV2_Alltoall_is_define(char *mv2_user_alltoall)
{

    int i;
    int nb_element = count_sep(mv2_user_alltoall) + 1;

    /* If one alltoall tuning table is already defined */
    if (mv2_alltoall_thresholds_table != NULL) {
        MPIU_Free(mv2_alltoall_thresholds_table);
    }

    mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table[1];
    MPIU_Memset(&mv2_tmp_alltoall_thresholds_table, 0, 
                sizeof( mv2_alltoall_tuning_table)); 
    mv2_size_alltoall_tuning_table = 1;

    /* We realloc the space for the new alltoall tuning table */
    mv2_alltoall_thresholds_table = MPIU_Malloc(mv2_size_alltoall_tuning_table *
                                             sizeof (mv2_alltoall_tuning_table));

    if (nb_element == 1) {
        mv2_tmp_alltoall_thresholds_table[0].numproc = 1;
        mv2_tmp_alltoall_thresholds_table[0].size_table = 1;
        mv2_tmp_alltoall_thresholds_table[0].algo_table[0].min = 0;
        mv2_tmp_alltoall_thresholds_table[0].algo_table[0].max = -1;
        switch (atoi(mv2_user_alltoall)) {
        case ALLTOALL_BRUCK_MV2:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_bruck_MV2;
            break;
        case ALLTOALL_RD_MV2:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_RD_MV2;
            break;
        case ALLTOALL_SCATTER_DEST_MV2:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_Scatter_dest_MV2;
            break;
        case ALLTOALL_PAIRWISE_MV2:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_pairwise_MV2;
            break;
        case ALLTOALL_INPLACE_MV2:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_inplace_MV2;
            break;
        default:
            mv2_tmp_alltoall_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
                &MPIR_Alltoall_bruck_MV2;
        }
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_alltoall))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_alltoall);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_alltoall);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_alltoall_thresholds_table[0].numproc = 1;
        mv2_tmp_alltoall_thresholds_table[0].size_table = nb_element;
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
            case ALLTOALL_BRUCK_MV2:
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].MV2_pt_Alltoall_function =
                    &MPIR_Alltoall_bruck_MV2;
                break;
            case ALLTOALL_RD_MV2:
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].MV2_pt_Alltoall_function =
                    &MPIR_Alltoall_RD_MV2;
                break;
            case ALLTOALL_SCATTER_DEST_MV2:
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].MV2_pt_Alltoall_function =
                    &MPIR_Alltoall_Scatter_dest_MV2;
                break;
            case ALLTOALL_PAIRWISE_MV2:
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].MV2_pt_Alltoall_function =
                    &MPIR_Alltoall_pairwise_MV2;
                break;
            case ALLTOALL_INPLACE_MV2:
                mv2_tmp_alltoall_thresholds_table[0].in_place_algo_table[i].
                    MV2_pt_Alltoall_function = &MPIR_Alltoall_inplace_MV2;
                break;
            default:
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].MV2_pt_Alltoall_function =
                    &MPIR_Alltoall_bruck_MV2;
            }
            if(atoi(p + match[1].rm_so) <= ALLTOALL_PAIRWISE_MV2) { 
                mv2_tmp_alltoall_thresholds_table[0].algo_table[i].min = atoi(p +
                                                                         match[2].rm_so);
                if (p[match[3].rm_so] == '+') {
                    mv2_tmp_alltoall_thresholds_table[0].algo_table[i].max = -1;
                } else {
                    mv2_tmp_alltoall_thresholds_table[0].algo_table[i].max =
                        atoi(p + match[3].rm_so);
                }
            } else {  
                int j=0; 
                mv2_tmp_alltoall_thresholds_table[0].in_place_algo_table[j].min = atoi(p +
                                                                         match[2].rm_so);
                if (p[match[3].rm_so] == '+') {
                    mv2_tmp_alltoall_thresholds_table[0].in_place_algo_table[j].max = -1;
                } else {
                    mv2_tmp_alltoall_thresholds_table[0].in_place_algo_table[j].max =
                        atoi(p + match[3].rm_so);
                }
            } 
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    MPIU_Memcpy(mv2_alltoall_thresholds_table, mv2_tmp_alltoall_thresholds_table, sizeof
                (mv2_alltoall_tuning_table));
    return 0;
}


#endif                          /* if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
