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
#include "allreduce_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"

enum {
    ALLREDUCE_P2P_RD = 1,      //1 &MPIR_Allreduce_pt2pt_rd_MV2
    ALLREDUCE_P2P_RS,          //2 &MPIR_Allreduce_pt2pt_rs_MV2
    ALLREDUCE_MCAST_2LEVEL,    //3 &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2
    ALLREDUCE_MCAST_RSA,       //4 &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2
    ALLREDUCE_SHMEM_REDUCE,    //5 &MPIR_Allreduce_reduce_shmem_MV2
    ALLREDUCE_P2P_REDUCE,      //6 &MPIR_Allreduce_reduce_p2p_MV2
};

int mv2_size_allreduce_tuning_table = 0;
mv2_allreduce_tuning_table *mv2_allreduce_thresholds_table = NULL;

int MV2_set_allreduce_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_allreduce_tuning_table = 6;
        mv2_allreduce_thresholds_table = MPIU_Malloc(mv2_size_allreduce_tuning_table *
                                                  sizeof (mv2_allreduce_tuning_table));
        mv2_allreduce_tuning_table mv2_tmp_allreduce_thresholds_table[] = {
             {
                12,
                0,
                {1, 0},
                2,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                24,
                0,
                {1, 0},
                2,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 1024, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                48,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                96,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                192,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {2048, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                384,
                0,
                {1, 0},
                2,
                {
                    {0, 4096, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {4096, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
 
        }; 
        MPIU_Memcpy(mv2_allreduce_thresholds_table, mv2_tmp_allreduce_thresholds_table,
                  mv2_size_allreduce_tuning_table * sizeof (mv2_allreduce_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
        MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        /*Stampede,*/
        mv2_size_allreduce_tuning_table = 8;
        mv2_allreduce_thresholds_table = MPIU_Malloc(mv2_size_allreduce_tuning_table *
                                                  sizeof (mv2_allreduce_tuning_table));
        mv2_allreduce_tuning_table mv2_tmp_allreduce_thresholds_table[] = {
             {
                16,
                0,
                {1, 0},
                2,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 1024, &MPIR_Allreduce_reduce_shmem_MV2},
                    {1024, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                32,
                0,
                {1, 1, 0},
                3,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, 16384, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 1024, &MPIR_Allreduce_reduce_shmem_MV2},
                    {1024, 16384, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                64,
                0,
                {1, 1, 0},
                3,
                {
                    {0, 512, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {512, 16384, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, 16384, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                128,
                0,
                {1, 1, 0},
                3,
                {
                    {0, 512, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {512, 16384, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, 16384, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                256,
                0,
                {1, 1, 0},
                3,
                {
                    {0, 512, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {512, 16384, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                512,
                0,
                {1, 1, 0},
                3,
                {
                    {0, 512, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {512, 16384, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, 16384, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                1024,
                0,
                {1, 1, 1, 0},
                4,
                {
                    {0, 512, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {512, 8192, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {8192, 65536, &MPIR_Allreduce_pt2pt_rs_MV2},
                    {65536, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                2048,
                0,
                {1, 1, 1, 0},
                4,
                {
                    {0, 64, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {64, 512, &MPIR_Allreduce_reduce_p2p_MV2},
                    {512, 4096, &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2},
                    {4096, 16384, &MPIR_Allreduce_pt2pt_rs_MV2},
                    {16384, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 512, &MPIR_Allreduce_reduce_shmem_MV2},
                    {512, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
 
        }; 
        MPIU_Memcpy(mv2_allreduce_thresholds_table, mv2_tmp_allreduce_thresholds_table,
                  mv2_size_allreduce_tuning_table * sizeof (mv2_allreduce_tuning_table));
    } else

#endif /* (_OSU_MVAPICH_) && !defined(_OSU_PSM_) */
    {
        mv2_size_allreduce_tuning_table = 7;
        mv2_allreduce_thresholds_table = MPIU_Malloc(mv2_size_allreduce_tuning_table *
                                                  sizeof (mv2_allreduce_tuning_table));
        mv2_allreduce_tuning_table mv2_tmp_allreduce_thresholds_table[] = {
            {
                8,
                0,
                {1, 0},
                2,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                16,
                0,
                {1, 0},
                2,
                {
                    {0, 1024, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {1024, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 1024, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                32,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                64,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                128,
                0,
                {1, 0},
                2,
                {
                    {0, 2048, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {2048, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 2048, &MPIR_Allreduce_reduce_shmem_MV2},
                    {2048, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                256,
                0,
                {1, 0},
                2,
                {
                    {0, 4096, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {4096, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
            {
                512,
                0,
                {1, 0},
                2,
                {
                    {0, 4096, &MPIR_Allreduce_pt2pt_rd_MV2},
                    {4096, -1, &MPIR_Allreduce_pt2pt_rs_MV2},
                },
                2,
                {
                    {0, 4096, &MPIR_Allreduce_reduce_shmem_MV2},
                    {4096, -1, &MPIR_Allreduce_reduce_p2p_MV2},
                },
            },
        };
        MPIU_Memcpy(mv2_allreduce_thresholds_table, mv2_tmp_allreduce_thresholds_table,
                  mv2_size_allreduce_tuning_table * sizeof (mv2_allreduce_tuning_table));

    }
    return 0;
}

void MV2_cleanup_allreduce_tuning_table()
{
    if (mv2_allreduce_thresholds_table != NULL) {
        MPIU_Free(mv2_allreduce_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}


int MV2_internode_Allreduce_is_define(char *mv2_user_allreduce_inter, char
                                    *mv2_user_allreduce_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_allreduce_inter) + 1;

    /* If one allreduce tuning table is already defined */
    if (mv2_allreduce_thresholds_table != NULL) {
        MPIU_Free(mv2_allreduce_thresholds_table);
    }

    mv2_allreduce_tuning_table mv2_tmp_allreduce_thresholds_table[1];
    mv2_size_allreduce_tuning_table = 1;

    /* We realloc the space for the new allreduce tuning table */
    mv2_allreduce_thresholds_table = MPIU_Malloc(mv2_size_allreduce_tuning_table *
                                             sizeof (mv2_allreduce_tuning_table));

    if (nb_element == 1) {

        mv2_tmp_allreduce_thresholds_table[0].numproc = 1;
        if(mv2_user_allreduce_two_level == 1){
            mv2_tmp_allreduce_thresholds_table[0].is_two_level_allreduce[0]=1;
        } else {
            mv2_tmp_allreduce_thresholds_table[0].is_two_level_allreduce[0]=0;
        }
        mv2_tmp_allreduce_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_allreduce_thresholds_table[0].size_intra_table = 1;
        mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].max = -1;
        mv2_tmp_allreduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_tmp_allreduce_thresholds_table[0].intra_node[0].max = -1;
    
        switch (atoi(mv2_user_allreduce_inter)) {
        case ALLREDUCE_P2P_RD:
            mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_pt2pt_rd_MV2;
            break;
        case ALLREDUCE_P2P_RS:
            mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_pt2pt_rs_MV2;
            break;
#if defined(_MCST_SUPPORT_)
        case ALLREDUCE_MCAST_2LEVEL:
            mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2;
            break;
        case ALLREDUCE_MCAST_RSA:
            mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2;
            break;
#endif /* #if defined(_MCST_SUPPORT_) */
        default:
            mv2_tmp_allreduce_thresholds_table[0].inter_leader[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_pt2pt_rd_MV2;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_allreduce_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_allreduce_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_allreduce_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_allreduce_thresholds_table[0].numproc = 1;
        mv2_tmp_allreduce_thresholds_table[0].size_inter_table = nb_element;
        mv2_tmp_allreduce_thresholds_table[0].size_intra_table = 2;
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
            case ALLREDUCE_P2P_RD:
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_pt2pt_rd_MV2;
                break;
            case ALLREDUCE_P2P_RS:
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_pt2pt_rs_MV2;
                break;
#if defined(_MCST_SUPPORT_)
            case ALLREDUCE_MCAST_2LEVEL:
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2;
                break;
            case ALLREDUCE_MCAST_RSA:
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2;
                break;
#endif /* #if defined(_MCST_SUPPORT_) */
            default:
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_pt2pt_rd_MV2;
            }


            mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_allreduce_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
                }
            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    mv2_tmp_allreduce_thresholds_table[0].size_intra_table = 2;

    MPIU_Memcpy(mv2_allreduce_thresholds_table, mv2_tmp_allreduce_thresholds_table, sizeof
                (mv2_allreduce_tuning_table));
    if (mv2_user_allreduce_intra != NULL) {
        MV2_intranode_Allreduce_is_define(mv2_user_allreduce_intra);
    } else {
        mv2_allreduce_thresholds_table[0].size_intra_table = 1;
        mv2_allreduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_allreduce_thresholds_table[0].intra_node[0].max = -1;
        mv2_allreduce_thresholds_table[0].intra_node[0].MV2_pt_Allreduce_function =
            &MPIR_Allreduce_reduce_p2p_MV2;
   }

    return 0;
}


int MV2_intranode_Allreduce_is_define(char *mv2_user_allreduce_intra)
{
    int i = 0;
    int nb_element = count_sep(mv2_user_allreduce_intra) + 1;

    if (nb_element == 1) {
        mv2_allreduce_thresholds_table[0].size_intra_table = 1;
        mv2_allreduce_thresholds_table[0].intra_node[0].min = 0;
        mv2_allreduce_thresholds_table[0].intra_node[0].max = -1;
    
        switch (atoi(mv2_user_allreduce_intra)) {
        case ALLREDUCE_P2P_REDUCE:
            mv2_allreduce_thresholds_table[0].intra_node[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_reduce_p2p_MV2;
            break;
        case ALLREDUCE_SHMEM_REDUCE:
            mv2_allreduce_thresholds_table[0].intra_node[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_reduce_shmem_MV2;
            break;
        default:
            mv2_allreduce_thresholds_table[0].intra_node[0].MV2_pt_Allreduce_function =
                &MPIR_Allreduce_reduce_p2p_MV2;
        }
        
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_allreduce_intra))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_allreduce_intra);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_allreduce_intra);
            MPIU_Free(dup);
            return 2;
        }

        mv2_allreduce_thresholds_table[0].numproc = 1;
        mv2_allreduce_thresholds_table[0].size_intra_table = 2;
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

            case ALLREDUCE_P2P_REDUCE:
                mv2_allreduce_thresholds_table[0].intra_node[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_reduce_p2p_MV2;
                break;
            case ALLREDUCE_SHMEM_REDUCE:
                mv2_allreduce_thresholds_table[0].intra_node[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_reduce_shmem_MV2;
                break;
            default:
                mv2_allreduce_thresholds_table[0].intra_node[i].MV2_pt_Allreduce_function =
                    &MPIR_Allreduce_reduce_p2p_MV2;
            }

            mv2_allreduce_thresholds_table[0].intra_node[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_allreduce_thresholds_table[0].intra_node[i].max = -1;
            } else {
                mv2_allreduce_thresholds_table[0].intra_node[i].max =
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
