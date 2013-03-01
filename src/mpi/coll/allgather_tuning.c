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
#include "allgather_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"

enum {
    ALLGATHER_RD_ALLGATHER_COMM = 1,
    ALLGATHER_RD,
    ALLGATHER_BRUCK,
    ALLGATHER_RING,
};

int mv2_size_allgather_tuning_table = 0;
mv2_allgather_tuning_table *mv2_allgather_thresholds_table = NULL;

int MV2_set_allgather_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_allgather_tuning_table = 6;
        mv2_allgather_thresholds_table = MPIU_Malloc(mv2_size_allgather_tuning_table *
                                                  sizeof (mv2_allgather_tuning_table));
        mv2_allgather_tuning_table mv2_tmp_allgather_thresholds_table[] = {
            {
                12,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                24,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                48,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                96,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                192,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                384,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },

        }; 
        MPIU_Memcpy(mv2_allgather_thresholds_table, mv2_tmp_allgather_thresholds_table,
                  mv2_size_allgather_tuning_table * sizeof (mv2_allgather_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        mv2_size_allgather_tuning_table = 6;
        mv2_allgather_thresholds_table = MPIU_Malloc(mv2_size_allgather_tuning_table *
                                                  sizeof (mv2_allgather_tuning_table));
        mv2_allgather_tuning_table mv2_tmp_allgather_thresholds_table[] = {
            {
                16,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                32,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                64,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                128,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                256,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                512,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },

        }; 
        MPIU_Memcpy(mv2_allgather_thresholds_table, mv2_tmp_allgather_thresholds_table,
                  mv2_size_allgather_tuning_table * sizeof (mv2_allgather_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_allgather_tuning_table = 6;
        mv2_allgather_thresholds_table = MPIU_Malloc(mv2_size_allgather_tuning_table *
                                                  sizeof (mv2_allgather_tuning_table));
        mv2_allgather_tuning_table mv2_tmp_allgather_thresholds_table[] = {
            {
                32,
                {0,0},
                2,
                {
                    {0, 512, &MPIR_Allgather_RD_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                64,
                {1, 0, 0},
                3,
                {
                    {0, 8, &MPIR_Allgather_RD_MV2},
                    {8, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                128,
                {1, 0, 0},
                3,
                {
                    {0, 16, &MPIR_Allgather_RD_MV2},
                    {16, 512, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {512, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                256,
                {1, 0, 0},
                3,
                {
                    {0, 16, &MPIR_Allgather_RD_MV2},
                    {16, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                512,
                {1, 0, 1},
                3,
                {
                    {0, 16, &MPIR_Allgather_RD_MV2},
                    {16, 2048, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {2048, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                1024,
                {1, 0, 1},
                3,
                {
                    {0, 16, &MPIR_Allgather_RD_MV2},
                    {16, 2048, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {2048, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
        }; 
        MPIU_Memcpy(mv2_allgather_thresholds_table, mv2_tmp_allgather_thresholds_table,
                  mv2_size_allgather_tuning_table * sizeof (mv2_allgather_tuning_table));
    } else
#endif /* (_OSU_MVAPICH_) && !defined(_OSU_PSM_) */
    {
        mv2_size_allgather_tuning_table = 7;
        mv2_allgather_thresholds_table = MPIU_Malloc(mv2_size_allgather_tuning_table *
                                                  sizeof (mv2_allgather_tuning_table));
        mv2_allgather_tuning_table mv2_tmp_allgather_thresholds_table[] = {
            {
                8,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            { 
                16,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                32,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                64,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                128,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                256,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },
            {
                512,
                {0,0},
                2,
                {
                    {0, 1024, &MPIR_Allgather_RD_Allgather_Comm_MV2},
                    {1024, -1, &MPIR_Allgather_Ring_MV2},
                },
            },

        };
        MPIU_Memcpy(mv2_allgather_thresholds_table, mv2_tmp_allgather_thresholds_table,
                  mv2_size_allgather_tuning_table * sizeof (mv2_allgather_tuning_table));

    }
    return 0;
}

void MV2_cleanup_allgather_tuning_table()
{
    if (mv2_allgather_thresholds_table != NULL) {
        MPIU_Free(mv2_allgather_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}


int MV2_internode_Allgather_is_define(char *mv2_user_allgather_inter)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_allgather_inter) + 1;

    /* If one allgather tuning table is already defined */
    if (mv2_allgather_thresholds_table != NULL) {
        MPIU_Free(mv2_allgather_thresholds_table);
    }

    mv2_allgather_tuning_table mv2_tmp_allgather_thresholds_table[1];
    mv2_size_allgather_tuning_table = 1;

    /* We realloc the space for the new allgather tuning table */
    mv2_allgather_thresholds_table = MPIU_Malloc(mv2_size_allgather_tuning_table *
                                             sizeof (mv2_allgather_tuning_table));

    if (nb_element == 1) {

        mv2_tmp_allgather_thresholds_table[0].numproc = 1;
        mv2_tmp_allgather_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_allgather_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_allgather_thresholds_table[0].inter_leader[0].max = -1;
        mv2_tmp_allgather_thresholds_table[0].two_level[0] = mv2_user_allgather_two_level;
    
        switch (atoi(mv2_user_allgather_inter)) {
        case ALLGATHER_RD_ALLGATHER_COMM:
            mv2_tmp_allgather_thresholds_table[0].inter_leader[0].MV2_pt_Allgather_function =
                &MPIR_Allgather_RD_Allgather_Comm_MV2;
            break;
        case ALLGATHER_RD:
            mv2_tmp_allgather_thresholds_table[0].inter_leader[0].MV2_pt_Allgather_function =
                &MPIR_Allgather_RD_MV2;
            break;
        case ALLGATHER_BRUCK:
            mv2_tmp_allgather_thresholds_table[0].inter_leader[0].MV2_pt_Allgather_function =
                &MPIR_Allgather_Bruck_MV2;
            break;
        case ALLGATHER_RING:
            mv2_tmp_allgather_thresholds_table[0].inter_leader[0].MV2_pt_Allgather_function =
                &MPIR_Allgather_Ring_MV2;
            break;
        default:
            mv2_tmp_allgather_thresholds_table[0].inter_leader[0].MV2_pt_Allgather_function =
                &MPIR_Allgather_RD_MV2;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_allgather_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_allgather_inter);
            return -1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_allgather_inter);
            MPIU_Free(dup);
            return -1;
        }

        mv2_tmp_allgather_thresholds_table[0].numproc = 1;
        mv2_tmp_allgather_thresholds_table[0].size_inter_table = nb_element;

        i = 0;
        for (p = strtok_r(dup, ",", &save_p); p; p = strtok_r(NULL, ",", &save_p)) {
            if (regexec(&preg, p, NMATCH, match, 0)) {
                fprintf(stderr, "failed to match on `%s'\n", p);
                regfree(&preg);
                MPIU_Free(dup);
                return -1;
            }
            /* given () start at 1 */
            switch (atoi(p + match[1].rm_so)) {
            case ALLGATHER_RD_ALLGATHER_COMM:
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].MV2_pt_Allgather_function =
                    &MPIR_Allgather_RD_Allgather_Comm_MV2;
                break;
            case ALLGATHER_RD:
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].MV2_pt_Allgather_function =
                    &MPIR_Allgather_RD_MV2;
                break;
            case ALLGATHER_BRUCK:
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].MV2_pt_Allgather_function =
                    &MPIR_Allgather_Bruck_MV2;
                break;
            case ALLGATHER_RING:
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].MV2_pt_Allgather_function =
                    &MPIR_Allgather_Ring_MV2;
                break;
            default:
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].MV2_pt_Allgather_function =
                    &MPIR_Allgather_RD_MV2;

            }

            mv2_tmp_allgather_thresholds_table[0].inter_leader[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_allgather_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
                }
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }

    MPIU_Memcpy(mv2_allgather_thresholds_table, mv2_tmp_allgather_thresholds_table, sizeof
                (mv2_allgather_tuning_table));

    return 0;
}

#endif                          /* if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
