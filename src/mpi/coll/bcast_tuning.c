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
#include "bcast_tuning.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "mv2_arch_hca_detect.h"
/* array used to tune bcast */

int mv2_size_bcast_tuning_table = 0;
mv2_bcast_tuning_table *mv2_bcast_thresholds_table = NULL;

int MV2_set_bcast_tuning_table(int heterogeneity)
{
#if defined(_OSU_MVAPICH_) && !defined(_OSU_PSM_) && !defined (DAPL_DEFAULT_PROVIDER)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        /*Trestles Table*/
        mv2_size_bcast_tuning_table=6;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                 sizeof (mv2_bcast_tuning_table));

         mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[]={
           {
           32,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 0, 1, 1, 1},
           9,
           {
             {0, 64, &MPIR_Bcast_binomial_MV2, -1},
             {64, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {128, 16384, &MPIR_Bcast_binomial_MV2, -1},
             {16384, 32768, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {32768, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {65536, 131072, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {131072, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
             {262144, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
             {524288, -1, &MPIR_Bcast_binomial_MV2, -1}
           },
           9,
           {
             {0, 64, &MPIR_Shmem_Bcast_MV2, -1},
             {64, 128, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {128, 16384, &MPIR_Shmem_Bcast_MV2, -1},
             {16384, 32768, &MPIR_Shmem_Bcast_MV2, -1},
             {32768, 65536, &MPIR_Knomial_Bcast_intra_node_MV2, 8},
             {65536, 131072, NULL, -1},
             {131072, 262144, &MPIR_Shmem_Bcast_MV2, -1},
             {262144, 524288, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           64,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0},
           11,
           {
             {0, 8, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {8, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {16, 32, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {32, 64, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {64, 256, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {256, 512, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {512, 4096, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {4096, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {131072, 262144, &MPIR_Bcast_binomial_MV2, -1},
             {262144, 524288, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
           },
           11,
           {
             {0, 8, &MPIR_Shmem_Bcast_MV2, -1},
             {8, 16, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {16, 32, &MPIR_Shmem_Bcast_MV2, -1},
             {32, 64, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {64, 256, &MPIR_Shmem_Bcast_MV2, -1},
             {256, 512, NULL, -1},
             {512, 4096, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {4096, 131072, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {131072, 262144, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {262144, 524288, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {524288, -1, NULL, -1}
           }
           },
           {
           128,
           8192, 4, 4,
           {0, 0, 1, 1, 0, 1, 1, 1, 1, 1},
           10,
           {
             {0, 32, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {32, 64, &MPIR_Bcast_binomial_MV2, -1},
             {64, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {128, 256, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {256, 2048, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {2048, 4096, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {4096, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {65536, 262144, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {262144, 524288, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {524288, -1, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1}
           },
           10,
           {
             {0, 32, NULL, -1},
             {32, 64, NULL, -1},
             {64, 128, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {128, 256, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {256, 2048, NULL, -1},
             {2048, 4096, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {4096, 65536, &MPIR_Knomial_Bcast_intra_node_MV2, 4},
             {65536, 262144, &MPIR_Shmem_Bcast_MV2, -1},
             {262144, 524288, &MPIR_Shmem_Bcast_MV2, -1},
             {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           256,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1},
           15,
           {
             {0, 1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {1, 2, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
             {2, 4, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {4, 8, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {8, 32, &MPIR_Pipelined_Bcast_MV2, -1},
             {32, 64, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {64, 128, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {128, 256, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {256, 512, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
             {512, 2048, &MPIR_Pipelined_Bcast_MV2, -1},
             {2048, 4096, &MPIR_Bcast_binomial_MV2, -1},
             {4096, 65536, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {65536, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
             {131072, 262144, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {262144, -1, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1}
           },
           15,
           {
             {0, 1, &MPIR_Knomial_Bcast_intra_node_MV2, 8},
             {1, 2, &MPIR_Shmem_Bcast_MV2, -1},
             {2, 4, &MPIR_Knomial_Bcast_intra_node_MV2, 8},
             {4, 8, &MPIR_Shmem_Bcast_MV2, -1},
             {8, 32, &MPIR_Shmem_Bcast_MV2, -1},
             {32, 64, &MPIR_Knomial_Bcast_intra_node_MV2, 8},
             {64, 128, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {128, 256, &MPIR_Shmem_Bcast_MV2, -1},
             {256, 512, &MPIR_Shmem_Bcast_MV2, -1},
             {512, 2048, &MPIR_Shmem_Bcast_MV2, -1},
             {2048, 4096, NULL, -1},
             {4096, 65536, &MPIR_Shmem_Bcast_MV2, -1},
             {65536, 131072, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {131072, 262144, &MPIR_Knomial_Bcast_intra_node_MV2, -1},
             {262144, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           512,
           8192, 4, 4,
           {1, 0, 0, 0, 1, 0, 1, 1, 0, 1},
           10,
           {
             {0, 2, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {2, 4, &MPIR_Bcast_binomial_MV2, -1},
             {4, 8, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {8, 32, &MPIR_Bcast_binomial_MV2, -1},
             {32, 64, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {64, 128, &MPIR_Bcast_binomial_MV2, -1},
             {128, 1024, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {1024, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
             {262144, 524288, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
             {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
           },
           10,
           {
             {0, 2, &MPIR_Knomial_Bcast_intra_node_MV2, 4},
             {2, 4, NULL, -1},
             {4, 8, NULL, -1},
             {8, 32, NULL, -1},
             {32, 64, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {64, 128, NULL, -1},
             {128, 1024, &MPIR_Knomial_Bcast_intra_node_MV2, 2},
             {1024, 262144, &MPIR_Shmem_Bcast_MV2, -1},
             {262144, 524288, NULL, -1},
             {524288, -1, &MPIR_Knomial_Bcast_intra_node_MV2, -1}
           }
           },
           {
           1024,
           8192, 4, 4,
           {1, 1, 0},
           3, {{0, 32768, &MPIR_Bcast_binomial_MV2, -1},
              {32768, 131072, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
              {131072, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
              },
           1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
           }
        };
        MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                    mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    }else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_INTEL_XEON_E5_2670_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){

        /*Gordon Table*/
        mv2_size_bcast_tuning_table=6;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                 sizeof (mv2_bcast_tuning_table));

         mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[]={
           {
           16,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
           14,
           {
             {0, 1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {1, 2, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {2, 4, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {4, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {16, 64, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {64, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {128, 1024, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {1024, 2048, &MPIR_Bcast_binomial_MV2, -1},
             {2048, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {8192, 16384, &MPIR_Bcast_binomial_MV2, -1},
             {16384, 32768, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
             {32768, 65536, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
             {65536, 524288, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
             {524288, -1, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1}
           },
           14,
           {
             {0, 1, &MPIR_Shmem_Bcast_MV2, 8},
             {1, 2, &MPIR_Shmem_Bcast_MV2, 2},
             {2, 4, &MPIR_Shmem_Bcast_MV2, 8},
             {4, 16, &MPIR_Shmem_Bcast_MV2, 4},
             {16, 64, &MPIR_Shmem_Bcast_MV2, 2},
             {64, 128, &MPIR_Shmem_Bcast_MV2, 8},
             {128, 1024, &MPIR_Shmem_Bcast_MV2, 2},
             {1024, 2048, &MPIR_Shmem_Bcast_MV2, -1},
             {2048, 8192, &MPIR_Shmem_Bcast_MV2, 2},
             {8192, 16384, &MPIR_Shmem_Bcast_MV2, -1},
             {16384, 32768, &MPIR_Shmem_Bcast_MV2, -1},
             {32768, 65536, &MPIR_Shmem_Bcast_MV2, -1},
             {65536, 524288, &MPIR_Shmem_Bcast_MV2, -1},
             {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           32,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
           10,
           {
             {0, 4, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {4, 8, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {8, 32, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {32, 64, &MPIR_Bcast_binomial_MV2, -1},
             {64, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {128, 256, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {256, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {2048, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {4096, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {65536, -1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4}
           },
           10,
           {
             {0, 4, &MPIR_Shmem_Bcast_MV2, 8},
             {4, 8, &MPIR_Shmem_Bcast_MV2, 2},
             {8, 32, &MPIR_Shmem_Bcast_MV2, 8},
             {32, 64, &MPIR_Shmem_Bcast_MV2, -1},
             {64, 128, &MPIR_Shmem_Bcast_MV2, 2},
             {128, 256, &MPIR_Shmem_Bcast_MV2, 4},
             {256, 2048, &MPIR_Shmem_Bcast_MV2, 8},
             {2048, 4096, &MPIR_Shmem_Bcast_MV2, 2},
             {4096, 65536, &MPIR_Shmem_Bcast_MV2, 8},
             {65536, -1, &MPIR_Shmem_Bcast_MV2, 4}
           }
           },
           {
           64,
           8192, 4, 4,
           {1, 1, 1, 1},
           4,
           {
             {0, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {8192, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {131072, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
             {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
           },
           4,
           {
             {0, 8192, &MPIR_Shmem_Bcast_MV2, 8},
             {8192, 131072, &MPIR_Shmem_Bcast_MV2, 2},
             {131072, 524288, &MPIR_Shmem_Bcast_MV2, -1},
             {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           128,
           8192, 4, 4,
           {1, 1, 1, 0},
           4,
           {
             {0, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {8192, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {32768, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
             {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
           },
           4,
           {
             {0, 8192, &MPIR_Shmem_Bcast_MV2, 8},
             {8192, 32768, &MPIR_Shmem_Bcast_MV2, 2},
             {32768, 524288, &MPIR_Shmem_Bcast_MV2, -1},
             {524288, -1, NULL, -1}
           }
           },
           {
           256,
           8192, 4, 4,
           {1, 1, 1, 1},
           4,
           {
             {0, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {16384, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {32768, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
             {131072, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
           },
           4,
           {
             {0, 16384, &MPIR_Shmem_Bcast_MV2, 4},
             {16384, 32768, &MPIR_Shmem_Bcast_MV2, 2},
             {32768, 131072, &MPIR_Shmem_Bcast_MV2, -1},
             {131072, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           512,
           8192, 4, 4,
           {1, 1, 1, 1, 1},
           5,
           {
             {0, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {2048, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {8192, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {32768, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
             {131072, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
           },
           5,
           {
             {0, 2048, &MPIR_Shmem_Bcast_MV2, 8},
             {2048, 8192, &MPIR_Shmem_Bcast_MV2, 4},
             {8192, 32768, &MPIR_Shmem_Bcast_MV2, 2},
             {32768, 131072, &MPIR_Shmem_Bcast_MV2, -1},
             {131072, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           },
           {
           1024,
           8192, 4, 4,
           {1, 1, 1, 1, 1, 1, 1},
           7,
           {
             {0, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
             {2048, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
             {8192, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {16384, 32768, &MPIR_Pipelined_Bcast_MV2, -1},
             {32768, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
             {65536, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
             {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
           },
           7,
           {
             {0, 2048, &MPIR_Shmem_Bcast_MV2, 8},
             {2048, 8192, &MPIR_Shmem_Bcast_MV2, 4},
             {8192, 16384, &MPIR_Shmem_Bcast_MV2, 2},
             {16384, 32768, &MPIR_Shmem_Bcast_MV2, -1},
             {32768, 65536, &MPIR_Shmem_Bcast_MV2, 2},
             {65536, 262144, &MPIR_Shmem_Bcast_MV2, -1},
             {262144, -1, &MPIR_Shmem_Bcast_MV2, -1}
           }
           }
         };

        MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                    mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    }
    else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        /*Stampede,*/
        mv2_size_bcast_tuning_table=8;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                 sizeof (mv2_bcast_tuning_table));

         mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[]={
            {
            16,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
            11,
            {
              {0, 8, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {8, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16, 1024, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {1024, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {8192, 16384, &MPIR_Bcast_binomial_MV2, -1},
              {16384, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {32768, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {65536, 131072, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
              {131072, 262144, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
              {262144, 524288, &MPIR_Bcast_scatter_doubling_allgather_MV2, -1},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
            11,
            {
              {0, 8, &MPIR_Shmem_Bcast_MV2, 2},
              {8, 16, &MPIR_Shmem_Bcast_MV2, 4},
              {16, 1024, &MPIR_Shmem_Bcast_MV2, 2},
              {1024, 8192, &MPIR_Shmem_Bcast_MV2, 4},
              {8192, 16384, &MPIR_Shmem_Bcast_MV2, -1},
              {16384, 32768, &MPIR_Shmem_Bcast_MV2, 4},
              {32768, 65536, &MPIR_Shmem_Bcast_MV2, 2},
              {65536, 131072, &MPIR_Shmem_Bcast_MV2, -1},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, -1},
              {262144, 524288, &MPIR_Shmem_Bcast_MV2, -1},
              {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            },
            {
            32,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1, 1},
            8,
            {
              {0, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {128, 256, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {256, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {32768, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {65536, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {131072, 262144, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {262144, 524288, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {524288, -1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8}
            },
            8,
            {
              {0, 128, &MPIR_Shmem_Bcast_MV2, 2},
              {128, 256, &MPIR_Shmem_Bcast_MV2, 4},
              {256, 32768, &MPIR_Shmem_Bcast_MV2, 2},
              {32768, 65536, &MPIR_Shmem_Bcast_MV2, 4},
              {65536, 131072, &MPIR_Shmem_Bcast_MV2, 2},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, 8},
              {262144, 524288, &MPIR_Shmem_Bcast_MV2, 2},
              {524288, -1, &MPIR_Shmem_Bcast_MV2, 8}
            }
            },
            {
            64,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1, 1, 1},
            9,
            {
              {0, 2, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {2, 4, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {4, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16, 32, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {32, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {128, 256, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {256, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {4096, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {32768, -1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2}
            },
            9,
            {
              {0, 2, &MPIR_Shmem_Bcast_MV2, 4},
              {2, 4, &MPIR_Shmem_Bcast_MV2, 8},
              {4, 16, &MPIR_Shmem_Bcast_MV2, 4},
              {16, 32, &MPIR_Shmem_Bcast_MV2, 8},
              {32, 128, &MPIR_Shmem_Bcast_MV2, 4},
              {128, 256, &MPIR_Shmem_Bcast_MV2, 8},
              {256, 4096, &MPIR_Shmem_Bcast_MV2, 4},
              {4096, 32768, &MPIR_Shmem_Bcast_MV2, 8},
              {32768, -1, &MPIR_Shmem_Bcast_MV2, 2}
            }
            },
            {
            128,
            8192, 4, 4,
            {1, 1, 1, 0},
            4,
            {
              {0, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {8192, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16384, 524288, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
            4,
            {
              {0, 8192, &MPIR_Shmem_Bcast_MV2, 8},
              {8192, 16384, &MPIR_Shmem_Bcast_MV2, 4},
              {16384, 524288, &MPIR_Shmem_Bcast_MV2, 2},
              {524288, -1, NULL, -1}
            }
            },
            {
            256,
            8192, 4, 4,
            {1, 1, 1, 1, 1},
            5,
            {
              {0, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16384, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {131072, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
              {262144, 524288, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
            5,
            {
              {0, 16384, &MPIR_Shmem_Bcast_MV2, 4},
              {16384, 131072, &MPIR_Shmem_Bcast_MV2, 2},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, -1},
              {262144, 524288, &MPIR_Shmem_Bcast_MV2, 2},
              {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            },
            {
            512,
            8192, 4, 4,
            {1, 1, 1, 1, 1},
            5,
            {
              {0, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {4096, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16384, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {131072, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
              {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
            5,
            {
              {0, 4096, &MPIR_Shmem_Bcast_MV2, 8},
              {4096, 16384, &MPIR_Shmem_Bcast_MV2, 4},
              {16384, 131072, &MPIR_Shmem_Bcast_MV2, 2},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, -1},
              {262144, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            },
            {
            1024,
            8192, 4, 4,
            {1, 1, 1, 1, 1},
            5,
            {
              {0, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {8192, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16384, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {65536, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
            5,
            {
              {0, 8192, &MPIR_Shmem_Bcast_MV2, 8},
              {8192, 16384, &MPIR_Shmem_Bcast_MV2, 4},
              {16384, 65536, &MPIR_Shmem_Bcast_MV2, 2},
              {65536, 524288, &MPIR_Shmem_Bcast_MV2, -1},
              {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            },
            {
            2048,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1},
            7,
            {
              {0, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {16, 32, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {32, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {4096, 16384, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {16384, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {32768, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
            7,
            {
              {0, 16, &MPIR_Shmem_Bcast_MV2, 8},
              {16, 32, &MPIR_Shmem_Bcast_MV2, 4},
              {32, 4096, &MPIR_Shmem_Bcast_MV2, 8},
              {4096, 16384, &MPIR_Shmem_Bcast_MV2, 4},
              {16384, 32768, &MPIR_Shmem_Bcast_MV2, 2},
              {32768, 524288, &MPIR_Shmem_Bcast_MV2, -1},
              {524288, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            }
      };

        MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                    mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    } else 

    {
        /*RI*/
        mv2_size_bcast_tuning_table = 7;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                  sizeof (mv2_bcast_tuning_table));
        mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[] = {
            {8,
             8192, 4, 4,
             {1},
             1, {{0, -1, &MPIR_Bcast_binomial_MV2, -1}},
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {
            16,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
            14,
            {
              {0, 1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {1, 2, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {2, 8, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {8, 16, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {16, 32, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {32, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {128, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {2048, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {4096, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {8192, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {32768, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {65536, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {131072, 262144, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {262144, -1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8}
            },
            14,
            {
              {0, 1, &MPIR_Shmem_Bcast_MV2, 8},
              {1, 2, &MPIR_Shmem_Bcast_MV2, 2},
              {2, 8, &MPIR_Shmem_Bcast_MV2, 8},
              {8, 16, &MPIR_Shmem_Bcast_MV2, 2},
              {16, 32, &MPIR_Shmem_Bcast_MV2, 8},
              {32, 128, &MPIR_Shmem_Bcast_MV2, 2},
              {128, 2048, &MPIR_Shmem_Bcast_MV2, 8},
              {2048, 4096, &MPIR_Shmem_Bcast_MV2, 2},
              {4096, 8192, &MPIR_Shmem_Bcast_MV2, 8},
              {8192, 32768, &MPIR_Shmem_Bcast_MV2, 4},
              {32768, 65536, &MPIR_Shmem_Bcast_MV2, 2},
              {65536, 131072, &MPIR_Shmem_Bcast_MV2, 4},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, 2},
              {262144, -1, &MPIR_Shmem_Bcast_MV2, 8}
            }
            },
            {
            32,
            8192, 4, 4,
            {1, 1, 1, 1},
            4,
            {
              {0, 64, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {64, 256, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {256, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {8192, -1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2}
            },
            4,
            {
              {0, 64, &MPIR_Shmem_Bcast_MV2, 8},
              {64, 256, &MPIR_Shmem_Bcast_MV2, 4},
              {256, 8192, &MPIR_Shmem_Bcast_MV2, 8},
              {8192, -1, &MPIR_Shmem_Bcast_MV2, 2}
            }
            },
            {
            64,
            8192, 4, 4,
            {1, 1, 1, 0},
            4,
            {
              {0, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {2048, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {8192, 131072, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {131072, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
            4,
            {
              {0, 2048, &MPIR_Shmem_Bcast_MV2, 8},
              {2048, 8192, &MPIR_Shmem_Bcast_MV2, 4},
              {8192, 131072, &MPIR_Shmem_Bcast_MV2, 2},
              {131072, -1, NULL, -1}
            }
            },
            {
            128,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 1, 1, 1, 0},
            9,
            {
              {0, 1, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {1, 2, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {2, 128, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {128, 512, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {512, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {8192, 65536, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {65536, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
              {131072, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
              {262144, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
            9,
            {
              {0, 1, &MPIR_Shmem_Bcast_MV2, 4},
              {1, 2, &MPIR_Shmem_Bcast_MV2, 8},
              {2, 128, &MPIR_Shmem_Bcast_MV2, 4},
              {128, 512, &MPIR_Shmem_Bcast_MV2, 8},
              {512, 8192, &MPIR_Shmem_Bcast_MV2, 4},
              {8192, 65536, &MPIR_Shmem_Bcast_MV2, 2},
              {65536, 131072, &MPIR_Shmem_Bcast_MV2, -1},
              {131072, 262144, &MPIR_Shmem_Bcast_MV2, -1},
              {262144, -1, NULL, -1}
            }
            },
            {
            256,
            8192, 4, 4,
            {1, 1, 1, 1, 1, 0},
            6,
            {
              {0, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {4096, 8192, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {8192, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {32768, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
              {131072, 524288, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
              {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
            6,
            {
              {0, 4096, &MPIR_Shmem_Bcast_MV2, 8},
              {4096, 8192, &MPIR_Shmem_Bcast_MV2, 4},
              {8192, 32768, &MPIR_Shmem_Bcast_MV2, 2},
              {32768, 131072, &MPIR_Shmem_Bcast_MV2, -1},
              {131072, 524288, &MPIR_Shmem_Bcast_MV2, -1},
              {524288, -1, NULL, -1}
            }
            },
            {
            512,
            8192, 4, 4,
            {1, 1, 1, 1, 1},
            5,
            {
              {0, 2048, &MPIR_Pipelined_Bcast_Zcpy_MV2, 8},
              {2048, 4096, &MPIR_Pipelined_Bcast_Zcpy_MV2, 4},
              {4096, 32768, &MPIR_Pipelined_Bcast_Zcpy_MV2, 2},
              {32768, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
              {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
            5,
            {
              {0, 2048, &MPIR_Shmem_Bcast_MV2, 8},
              {2048, 4096, &MPIR_Shmem_Bcast_MV2, 4},
              {4096, 32768, &MPIR_Shmem_Bcast_MV2, 2},
              {32768, 262144, &MPIR_Shmem_Bcast_MV2, -1},
              {262144, -1, &MPIR_Shmem_Bcast_MV2, -1}
            }
            }
      };
    
      MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                  mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    }
#else  /* defined (DAPL_DEFAULT_PROVIDER) || defined(_OSU_PSM_) */
#if defined(DAPL_DEFAULT_PROVIDER)
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_size_bcast_tuning_table=6;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                 sizeof (mv2_bcast_tuning_table));

         mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[]={
            {32,
             8192, 4, 4,
             {1, 0},
             2, {{0, 16384, &MPIR_Bcast_binomial_MV2, -1}, 
                 {16384, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}},
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {64,
             8192, 4, 4,
             {1, 0},
             2, {{0, 8192, &MPIR_Bcast_binomial_MV2, -1}, 
                 {8192, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}},
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {128,
             8192, 4, 4,
             {1, 1, 0},
             3, {{0, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 65536, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {65536, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {256,
             8192, 4, 4,
             {1, 1, 0},
             3, {{0, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 65536, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1},
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {512,
             8192, 4, 4,
             {1, 1, 0},
             3, {{0, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {1024,
             8192, 4, 4,
             {1, 1, 0},
             3, {{0, 32768, &MPIR_Bcast_binomial_MV2, -1},
                 {32768, 131072, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {131072, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            }
        };
        MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                    mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity){
        /*Stampede,*/
        mv2_size_bcast_tuning_table=8;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                 sizeof (mv2_bcast_tuning_table));

         mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[]={
            {16,
            8192, 4, 4,
             {1, 1},
             2, {{0, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, -1, &MPIR_Pipelined_Bcast_MV2, -1},
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {32,
            8192, 4, 4,
             {1, 1, 1},
             3, {{0, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
                 {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {64,
            8192, 4, 4,
             {1, 1, 1, 0},
             4, {{0, 4096, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {4096, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {128,
            8192, 4, 4,
            {1, 1, 1, 0},
             4, {{0, 2048, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {2048, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {16384, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
                 {52488, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {256,
            8192, 4, 4,
             {1, 1, 1, 0},
             4, {{0, 2048, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {2048, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
                 {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {512,
            8192, 4, 4,
             {1, 1, 1, 1},
             4, {{0,  4096, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {4096, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {16384, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
              {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
            },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {1024,
             8192, 4, 4,
             {1, 1, 1, 1},
             4, {{0,  4096, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {4096, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {16384, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {2048,
             8192, 4, 4,
             {1, 1, 1},
             3, {{0,  8192, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {8192, 16384, &MPIR_Bcast_binomial_MV2, -1},
                 {16384, -1, &MPIR_Pipelined_Bcast_MV2, -1}
                },
             3, {{0, 8192, &MPIR_Shmem_Bcast_MV2, -1},
                 {8192, 16384, &MPIR_Shmem_Bcast_MV2, -1},
                 {16384, -1, &MPIR_Shmem_Bcast_MV2, -1}
                }
            }

      };

        MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                    mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    } else 

#endif /* defined(_OSU_PSM_) */
    {
        mv2_size_bcast_tuning_table = 7;
        mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                                  sizeof (mv2_bcast_tuning_table));
        mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[] = {
            {8,
             8192, 4, 4,
             {1},
             1, {{0, -1, &MPIR_Bcast_binomial_MV2, -1}},
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {16,
             8192, 4, 4,
             {1, 1},
             2, {{0, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, -1, &MPIR_Pipelined_Bcast_MV2, -1},
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {32,
             8192, 4, 4,
             {1, 1, 1},
             3, {{0, 8192, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {8192, 524288, &MPIR_Pipelined_Bcast_MV2, -1},
                 {524288, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {64,
             8192, 4, 4,
             {1, 1, 1, 0},
             4, {{0, 2048, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {2048, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {128,
             8192, 4, 4,
             {1, 1, 1, 0},
             4, {{0, 8192, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {8192, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
                 {131072, 262144, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {256,
             8192, 4, 4,
             {1, 1, 1, 1, 0},
             5, {{0, 2048, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {2048, 8192, &MPIR_Bcast_binomial_MV2, -1},
                 {8192, 131072, &MPIR_Pipelined_Bcast_MV2, -1},
                 {131072, 524288, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1},
                 {524288, -1, &MPIR_Bcast_scatter_ring_allgather_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            },
            {512,
             8192, 4, 4,
             {1, 1, 1},
             3, {{0,  8192, &MPIR_Knomial_Bcast_inter_node_wrapper_MV2, -1},
                 {8192, 262144, &MPIR_Pipelined_Bcast_MV2, -1},
                 {262144, -1, &MPIR_Bcast_scatter_ring_allgather_shm_MV2, -1}
                },
             1, {{0, -1, &MPIR_Shmem_Bcast_MV2, -1}}
            }
      };
    
      MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table,
                  mv2_size_bcast_tuning_table * sizeof (mv2_bcast_tuning_table));
    }
#endif
    return 0;
}

void MV2_cleanup_bcast_tuning_table()
{
    if (mv2_bcast_thresholds_table != NULL) {
        MPIU_Free(mv2_bcast_thresholds_table);
    }

}

/* Return the number of separator inside a string */
static int count_sep(char *string)
{
    return *string == '\0' ? 0 : (count_sep(string + 1) + (*string == ','));
}

int MV2_internode_Bcast_is_define(char *mv2_user_bcast_inter, char *mv2_user_bcast_intra)
{

    int i;
    int nb_element = count_sep(mv2_user_bcast_inter) + 1;

    /* If one bcast tuning table is already defined */
    if (mv2_bcast_thresholds_table != NULL) {
        MPIU_Free(mv2_bcast_thresholds_table);
    }

    mv2_bcast_tuning_table mv2_tmp_bcast_thresholds_table[1];
    mv2_size_bcast_tuning_table = 1;

    /* We realloc the space for the new bcast tuning table */
    mv2_bcast_thresholds_table = MPIU_Malloc(mv2_size_bcast_tuning_table *
                                             sizeof (mv2_bcast_tuning_table));

    if (nb_element == 1) {
        mv2_tmp_bcast_thresholds_table[0].numproc = 1;
        mv2_tmp_bcast_thresholds_table[0].bcast_segment_size = bcast_segment_size;
        mv2_tmp_bcast_thresholds_table[0].inter_node_knomial_factor = mv2_inter_node_knomial_factor;
        mv2_tmp_bcast_thresholds_table[0].intra_node_knomial_factor = mv2_intra_node_knomial_factor;
        mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
        mv2_tmp_bcast_thresholds_table[0].size_inter_table = 1;
        mv2_tmp_bcast_thresholds_table[0].inter_leader[0].min = 0;
        mv2_tmp_bcast_thresholds_table[0].inter_leader[0].max = -1;
        switch (atoi(mv2_user_bcast_inter)) {
        case 1:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_binomial_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 0;
            break;
        case 2:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_binomial_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
        case 3:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_scatter_doubling_allgather_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 0;
            break;
        case 4:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_scatter_doubling_allgather_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
        case 5:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_scatter_ring_allgather_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 0;
            break;
        case 6:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_scatter_ring_allgather_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
        case 7:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_scatter_ring_allgather_shm_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
        case 8:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Knomial_Bcast_inter_node_wrapper_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
        case 9:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Pipelined_Bcast_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
#if defined (_OSU_MVAPICH_)  && !defined (DAPL_DEFAULT_PROVIDER) 
        case 10:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Pipelined_Bcast_Zcpy_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 1;
            break;
#endif
        default:
            mv2_tmp_bcast_thresholds_table[0].inter_leader[0].MV2_pt_Bcast_function =
                &MPIR_Bcast_binomial_MV2;
            mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[0] = 0;
        }
        if (mv2_user_bcast_intra == NULL) {
            mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
                &MPIR_Shmem_Bcast_MV2;
        } else {
            if (atoi(mv2_user_bcast_intra) == 1) {
                mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
                    &MPIR_Knomial_Bcast_intra_node_MV2;
            } else {
                mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
                    &MPIR_Shmem_Bcast_MV2;
            }

        }
    } else {
        char *dup, *p, *save_p;
        regmatch_t match[NMATCH];
        regex_t preg;
        const char *regexp = "([0-9]+):([0-9]+)-([0-9]+|\\+)";

        if (!(dup = MPIU_Strdup(mv2_user_bcast_inter))) {
            fprintf(stderr, "failed to duplicate `%s'\n", mv2_user_bcast_inter);
            return 1;
        }

        if (regcomp(&preg, regexp, REG_EXTENDED)) {
            fprintf(stderr, "failed to compile regexp `%s'\n", mv2_user_bcast_inter);
            MPIU_Free(dup);
            return 2;
        }

        mv2_tmp_bcast_thresholds_table[0].numproc = 1;
        mv2_tmp_bcast_thresholds_table[0].bcast_segment_size = bcast_segment_size;
        mv2_tmp_bcast_thresholds_table[0].inter_node_knomial_factor = mv2_inter_node_knomial_factor;
        mv2_tmp_bcast_thresholds_table[0].intra_node_knomial_factor = mv2_intra_node_knomial_factor;
        mv2_tmp_bcast_thresholds_table[0].size_inter_table = nb_element;
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
            case 1:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_binomial_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 0;
                break;
            case 2:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_binomial_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
            case 3:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_scatter_doubling_allgather_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 0;
                break;
            case 4:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_scatter_doubling_allgather_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
            case 5:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_scatter_ring_allgather_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 0;
                break;
            case 6:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_scatter_ring_allgather_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
            case 7:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_scatter_ring_allgather_shm_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
            case 8:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Knomial_Bcast_inter_node_wrapper_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
            case 9:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Pipelined_Bcast_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
#if defined (_OSU_MVAPICH_)  && !defined (DAPL_DEFAULT_PROVIDER) 
            case 10:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Pipelined_Bcast_Zcpy_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 1;
                break;
#endif
            default:
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].MV2_pt_Bcast_function =
                    &MPIR_Bcast_binomial_MV2;
                mv2_tmp_bcast_thresholds_table[0].is_two_level_bcast[i] = 0;
            }
            mv2_tmp_bcast_thresholds_table[0].inter_leader[i].min = atoi(p +
                                                                         match[2].rm_so);
            if (p[match[3].rm_so] == '+') {
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].max = -1;
            } else {
                mv2_tmp_bcast_thresholds_table[0].inter_leader[i].max =
                    atoi(p + match[3].rm_so);
            }

            i++;
        }
        MPIU_Free(dup);
        regfree(&preg);
    }
    mv2_tmp_bcast_thresholds_table[0].size_intra_table = 1;
    if (mv2_user_bcast_intra == NULL) {
        mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
            &MPIR_Shmem_Bcast_MV2;
    } else {
        if (atoi(mv2_user_bcast_intra) == 1) {
            mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
                &MPIR_Knomial_Bcast_intra_node_MV2;
        } else {
            mv2_tmp_bcast_thresholds_table[0].intra_node[0].MV2_pt_Bcast_function =
                &MPIR_Shmem_Bcast_MV2;
        }
    }
    MPIU_Memcpy(mv2_bcast_thresholds_table, mv2_tmp_bcast_thresholds_table, sizeof
                (mv2_bcast_tuning_table));
    return 0;
}

int MV2_intranode_Bcast_is_define(char *mv2_user_bcast_intra)
{

    int i, j;
    for (i = 0; i < mv2_size_bcast_tuning_table; i++) {
        for (j = 0; j < mv2_bcast_thresholds_table[i].size_intra_table; j++) {
            if (atoi(mv2_user_bcast_intra) == 1) {
                mv2_bcast_thresholds_table[i].intra_node[j].MV2_pt_Bcast_function =
                    &MPIR_Knomial_Bcast_intra_node_MV2;
            } else {
                mv2_bcast_thresholds_table[i].intra_node[j].MV2_pt_Bcast_function =
                    &MPIR_Shmem_Bcast_MV2;
            }
        }
    }
    return 0;
}

#endif                          /* if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
