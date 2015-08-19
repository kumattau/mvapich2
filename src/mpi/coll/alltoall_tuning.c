/* Copyright (c) 2001-2015, The Ohio State University. All rights
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
#include "tuning/alltoall_arch_tuning.h"
#include "mv2_arch_hca_detect.h"
/* array used to tune alltoall */


int *mv2_alltoall_table_ppn_conf = NULL;
int mv2_alltoall_num_ppn_conf = 1;
int *mv2_size_alltoall_tuning_table = NULL;
mv2_alltoall_tuning_table **mv2_alltoall_thresholds_table = NULL;

int *mv2_alltoall_indexed_table_ppn_conf = NULL;
int mv2_alltoall_indexed_num_ppn_conf = 1;
int *mv2_size_alltoall_indexed_tuning_table = NULL;
mv2_alltoall_indexed_tuning_table **mv2_alltoall_indexed_thresholds_table = NULL;

int MV2_set_alltoall_tuning_table(int heterogeneity)
{
  int agg_table_sum = 0;
  int i;
  
    if (mv2_use_indexed_tuning || mv2_use_indexed_alltoall_tuning) {
      mv2_alltoall_indexed_tuning_table **table_ptrs = NULL;
#ifndef CHANNEL_PSM
#ifdef CHANNEL_MRAIL_GEN2
      if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
			       MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Lonestar Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_X5650_12__MLX_CX_QDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_X5650_12__MLX_CX_QDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 12;
	mv2_size_alltoall_indexed_tuning_table[2] = 6;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_12ppn[] =
	  GEN2__INTEL_XEON_X5650_12__MLX_CX_QDR__12PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_12ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      if ((MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                   MV2_ARCH_INTEL_XEON_E5_2690_V2_2S_20, MV2_HCA_MLX_CX_CONNIB) ||
          MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
                   MV2_ARCH_INTEL_XEON_E5_2680_V2_2S_20, MV2_HCA_MLX_CX_CONNIB)) && !heterogeneity) {
	/*PSG Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 3;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_E5_2690_V2_2S_20__MLX_CX_CONNIB__1PPN;
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 4;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_E5_2690_V2_2S_20__MLX_CX_CONNIB__2PPN;
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 20;
	mv2_size_alltoall_indexed_tuning_table[2] = 4;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_20ppn[] =
	  GEN2__INTEL_XEON_E5_2690_V2_2S_20__MLX_CX_CONNIB__20PPN;
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_20ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Trestles Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 4;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__AMD_OPTERON_6136_32__MLX_CX_QDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 3;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__AMD_OPTERON_6136_32__MLX_CX_QDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 32;
	mv2_size_alltoall_indexed_tuning_table[2] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_32ppn[] =
	  GEN2__AMD_OPTERON_6136_32__MLX_CX_QDR__32PPN 
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_32ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2670_16, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Gordon Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_E5_2670_16__MLX_CX_QDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_E5_2670_16__MLX_CX_QDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 8;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  GEN2__INTEL_XEON_E5_2670_16__MLX_CX_QDR__16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2670_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	/*Yellowstone Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_E5_2670_16__MLX_CX_FDR__1PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2670_16__MLX_CX_FDR__1PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[0] = 3;
	  table_ptrs[0] = mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[0] = 2;
	  table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_E5_2670_16__MLX_CX_FDR__2PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2670_16__MLX_CX_FDR__2PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[1] = 3;
	  table_ptrs[1] = mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[1] = 2;
	  table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
        mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn[] =
          GEN2_CMA__INTEL_XEON_E5_2670_16__MLX_CX_FDR__16PPN;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
          GEN2__INTEL_XEON_E5_2670_16__MLX_CX_FDR__16PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[2] = 4;
	  table_ptrs[2] = mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[2] = 5;
	  table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[2] = 5;
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
#endif
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	/*Stampede Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[0] = 4;
	  table_ptrs[0] = mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[0] = 5;
	  table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[1] = 4;
	  table_ptrs[1] = mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[1] = 6;
	  table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[1] = 6;
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[2] = 5;
	  table_ptrs[2] = mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[2] = 7;
	  table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[2] = 7;
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
#endif
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5630_8, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*RI Table*/
        mv2_alltoall_indexed_num_ppn_conf = 3;
        mv2_alltoall_indexed_thresholds_table
          = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
                        * mv2_alltoall_indexed_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
                                 * mv2_alltoall_indexed_num_ppn_conf);
        mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
                                                          mv2_alltoall_indexed_num_ppn_conf);
        mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));

        mv2_alltoall_indexed_table_ppn_conf[0] = 1;
        mv2_size_alltoall_indexed_tuning_table[0] = 2;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
          GEN2__RI__1PPN
        table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;

        mv2_alltoall_indexed_table_ppn_conf[1] = 2;
        mv2_size_alltoall_indexed_tuning_table[1] = 2;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
          GEN2__RI__2PPN
        table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;

        mv2_alltoall_indexed_table_ppn_conf[2] = 8;
        mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_8ppn[] =
          GEN2_CMA__RI__8PPN;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_8ppn[] =
          GEN2__RI__8PPN;
#if defined(_SMP_CMA_)
        if (g_smp_use_cma) {
          mv2_size_alltoall_indexed_tuning_table[2] = 5;
          table_ptrs[2] = mv2_tmp_cma_alltoall_indexed_thresholds_table_8ppn;
        }
        else {
          mv2_size_alltoall_indexed_tuning_table[2] = 8;
          table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_8ppn;
        }
#else
        mv2_size_alltoall_indexed_tuning_table[2] = 8;
        table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_8ppn;
#endif

        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
          agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
          MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
        MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_indexed_tuning_table)
                     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
          mv2_alltoall_indexed_thresholds_table[i] =
            mv2_alltoall_indexed_thresholds_table[i - 1]
            + mv2_size_alltoall_indexed_tuning_table[i - 1];
          MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_indexed_tuning_table)
                       * mv2_size_alltoall_indexed_tuning_table[i]));
        }
        MPIU_Free(table_ptrs);
        return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
			     MV2_ARCH_INTEL_XEON_E5_2680_V3_2S_24, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	/*Comet Table*/
	mv2_alltoall_indexed_num_ppn_conf = 1;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 24;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_24ppn[] =
	  GEN2__INTEL_XEON_E5_2680_24__MLX_CX_FDR__24PPN;
	/*
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_24ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_24__MLX_CX_FDR__24PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[0] = 5;
	  table_ptrs[0] = mv2_tmp_cma_alltoall_indexed_thresholds_table_24ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[0] = 5;
	  table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_24ppn;
	}
#else
	*/
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_24ppn;
	/*
#endif
	*/
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else {
	/*Stampede Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[0] = 4;
	  table_ptrs[0] = mv2_tmp_cma_alltoall_indexed_thresholds_table_1ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[0] = 5;
	  table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[1] = 4;
	  table_ptrs[1] = mv2_tmp_cma_alltoall_indexed_thresholds_table_2ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[1] = 6;
	  table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[1] = 6;
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
#endif
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  GEN2__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN;
	mv2_alltoall_indexed_tuning_table mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn[] =
	  GEN2_CMA__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN;
#if defined(_SMP_CMA_)
	if (g_smp_use_cma) {
	  mv2_size_alltoall_indexed_tuning_table[2] = 5;
	  table_ptrs[2] = mv2_tmp_cma_alltoall_indexed_thresholds_table_16ppn;
	}
	else {
	  mv2_size_alltoall_indexed_tuning_table[2] = 7;
	  table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
	}
#else
	mv2_size_alltoall_indexed_tuning_table[2] = 7;
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
#endif
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
#elif defined (CHANNEL_NEMESIS_IB)
      if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
			       MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Lonestar Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_X5650_12__MLX_CX_QDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_X5650_12__MLX_CX_QDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 8;
	mv2_size_alltoall_indexed_tuning_table[2] = 3;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_8ppn[] =
	  NEMESIS__INTEL_XEON_X5650_12__MLX_CX_QDR__12PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_8ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_AMD_OPTERON_6136_32, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Trestles Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 4;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__AMD_OPTERON_6136_32__MLX_CX_QDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 3;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__AMD_OPTERON_6136_32__MLX_CX_QDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 32;
	mv2_size_alltoall_indexed_tuning_table[2] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_32ppn[] =
	  NEMESIS__AMD_OPTERON_6136_32__MLX_CX_QDR__32PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_32ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2670_16, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*Gordon Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_QDR_1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_QDR_2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 4;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_QDR_16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2670_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	/*Yellowstone Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_FDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 2;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_FDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  NEMESIS__INTEL_XEON_E5_2670_16__MLX_CX_FDR__16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	/*Stampede Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 7;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
      else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				    MV2_ARCH_INTEL_XEON_E5630_8, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	/*RI Table*/
        mv2_alltoall_indexed_num_ppn_conf = 3;
        mv2_alltoall_indexed_thresholds_table
          = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
                        * mv2_alltoall_indexed_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
                                 * mv2_alltoall_indexed_num_ppn_conf);
        mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							     mv2_alltoall_indexed_num_ppn_conf);
        mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));

        mv2_alltoall_indexed_table_ppn_conf[0] = 1;
        mv2_size_alltoall_indexed_tuning_table[0] = 2;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
          NEMESIS__RI__1PPN
	  table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;

        mv2_alltoall_indexed_table_ppn_conf[1] = 2;
        mv2_size_alltoall_indexed_tuning_table[1] = 2;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
          NEMESIS__RI__2PPN
	  table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;

        mv2_alltoall_indexed_table_ppn_conf[2] = 8;
        mv2_size_alltoall_indexed_tuning_table[2] = 7;
        mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_8ppn[] =
          NEMESIS__RI__8PPN
	  table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_8ppn;

        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
          agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
        }
        mv2_alltoall_indexed_thresholds_table[0] =
          MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
        MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_indexed_tuning_table)
                     * mv2_size_alltoall_indexed_tuning_table[0]));
        for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
          mv2_alltoall_indexed_thresholds_table[i] =
            mv2_alltoall_indexed_thresholds_table[i - 1]
            + mv2_size_alltoall_indexed_tuning_table[i - 1];
          MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_indexed_tuning_table)
                       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
        return 0;
      }
      else {
	/*Stampede Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 7;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
#endif
#else /* !CHANNEL_PSM */
    if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
			     MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_QLGIC_QIB) && !heterogeneity) {
      /*Sierra Table*/
      mv2_alltoall_indexed_num_ppn_conf = 2;
      mv2_alltoall_indexed_thresholds_table
	= MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
		      * mv2_alltoall_indexed_num_ppn_conf);
      table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			       * mv2_alltoall_indexed_num_ppn_conf);
      mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							   mv2_alltoall_indexed_num_ppn_conf);
      mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
      mv2_alltoall_indexed_table_ppn_conf[0] = 1;
      mv2_size_alltoall_indexed_tuning_table[0] = 5;
      mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	PSM__INTEL_XEON_X5650_12__MV2_HCA_QLGIC_QIB__1PPN;
      table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
      mv2_alltoall_indexed_table_ppn_conf[1] = 12;
      mv2_size_alltoall_indexed_tuning_table[1] = 6;
      mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_12ppn[] =
	PSM__INTEL_XEON_X5650_12__MV2_HCA_QLGIC_QIB__12PPN;
      table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_12ppn;
      
      agg_table_sum = 0;
      for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
      }
      mv2_alltoall_indexed_thresholds_table[0] =
	MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
      MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		  (sizeof(mv2_alltoall_indexed_tuning_table)
		   * mv2_size_alltoall_indexed_tuning_table[0]));
      for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	mv2_alltoall_indexed_thresholds_table[i] =
	  mv2_alltoall_indexed_thresholds_table[i - 1]
	  + mv2_size_alltoall_indexed_tuning_table[i - 1];
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[i]));
      }
      MPIU_Free(table_ptrs);
      return 0;
    }
    else {
      /*Sierra Table*/
      mv2_alltoall_indexed_num_ppn_conf = 2;
      mv2_alltoall_indexed_thresholds_table
	= MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
		      * mv2_alltoall_indexed_num_ppn_conf);
      table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			       * mv2_alltoall_indexed_num_ppn_conf);
      mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							   mv2_alltoall_indexed_num_ppn_conf);
      mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
      mv2_alltoall_indexed_table_ppn_conf[0] = 1;
      mv2_size_alltoall_indexed_tuning_table[0] = 5;
      mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	PSM__INTEL_XEON_X5650_12__MV2_HCA_QLGIC_QIB__1PPN;
      table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
      mv2_alltoall_indexed_table_ppn_conf[1] = 12;
      mv2_size_alltoall_indexed_tuning_table[1] = 6;
      mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_12ppn[] =
	PSM__INTEL_XEON_X5650_12__MV2_HCA_QLGIC_QIB__12PPN;
      table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_12ppn;
      
      agg_table_sum = 0;
      for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
      }
      mv2_alltoall_indexed_thresholds_table[0] =
	MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
      MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		  (sizeof(mv2_alltoall_indexed_tuning_table)
		   * mv2_size_alltoall_indexed_tuning_table[0]));
      for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	mv2_alltoall_indexed_thresholds_table[i] =
	  mv2_alltoall_indexed_thresholds_table[i - 1]
	  + mv2_size_alltoall_indexed_tuning_table[i - 1];
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[i]));
      }
      MPIU_Free(table_ptrs);
      return 0;
    }
#endif /* !CHANNEL_PSM */
    {
	/*Stampede Table*/
	mv2_alltoall_indexed_num_ppn_conf = 3;
	mv2_alltoall_indexed_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
			* mv2_alltoall_indexed_num_ppn_conf);
	table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_indexed_tuning_table *)
				 * mv2_alltoall_indexed_num_ppn_conf);
	mv2_size_alltoall_indexed_tuning_table = MPIU_Malloc(sizeof(int) *
							  mv2_alltoall_indexed_num_ppn_conf);
	mv2_alltoall_indexed_table_ppn_conf = MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      
	mv2_alltoall_indexed_table_ppn_conf[0] = 1;
	mv2_size_alltoall_indexed_tuning_table[0] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_1ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__1PPN
	table_ptrs[0] = mv2_tmp_alltoall_indexed_thresholds_table_1ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[1] = 2;
	mv2_size_alltoall_indexed_tuning_table[1] = 5;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_2ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__2PPN
	table_ptrs[1] = mv2_tmp_alltoall_indexed_thresholds_table_2ppn;
      
	mv2_alltoall_indexed_table_ppn_conf[2] = 16;
	mv2_size_alltoall_indexed_tuning_table[2] = 7;
	mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table_16ppn[] =
	  NEMESIS__INTEL_XEON_E5_2680_16__MLX_CX_FDR__16PPN
	table_ptrs[2] = mv2_tmp_alltoall_indexed_thresholds_table_16ppn;
      
	agg_table_sum = 0;
	for (i = 0; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_indexed_tuning_table[i];
	}
	mv2_alltoall_indexed_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_indexed_tuning_table));
	MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], table_ptrs[0],
		    (sizeof(mv2_alltoall_indexed_tuning_table)
		     * mv2_size_alltoall_indexed_tuning_table[0]));
	for (i = 1; i < mv2_alltoall_indexed_num_ppn_conf; i++) {
	  mv2_alltoall_indexed_thresholds_table[i] =
	    mv2_alltoall_indexed_thresholds_table[i - 1]
	    + mv2_size_alltoall_indexed_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[i], table_ptrs[i],
		      (sizeof(mv2_alltoall_indexed_tuning_table)
		       * mv2_size_alltoall_indexed_tuning_table[i]));
	}
	MPIU_Free(table_ptrs);
	return 0;
      }
    }
    else {
      mv2_alltoall_tuning_table **table_ptrs = NULL;
#ifndef CHANNEL_PSM
#ifdef CHANNEL_MRAIL_GEN2
      if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
			       MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity){
        mv2_alltoall_num_ppn_conf = 1;
        mv2_alltoall_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			* mv2_alltoall_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				 * mv2_alltoall_num_ppn_conf);
        mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						     mv2_alltoall_num_ppn_conf);
        mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
        mv2_alltoall_table_ppn_conf[0] = 12;
        mv2_size_alltoall_tuning_table[0] = 6;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_12ppn[] = {
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
        table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_12ppn;
        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_tuning_table[i];
        }
        mv2_alltoall_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
        MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_tuning_table)
                     * mv2_size_alltoall_tuning_table[0]));
        for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
	  mv2_alltoall_thresholds_table[i] =
            mv2_alltoall_thresholds_table[i - 1]
            + mv2_size_alltoall_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_tuning_table)
                       * mv2_size_alltoall_tuning_table[i]));
        }
        MPIU_Free(table_ptrs);
	return 0;
      } else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				      MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
        mv2_alltoall_num_ppn_conf = 3;
        mv2_alltoall_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			* mv2_alltoall_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				 * mv2_alltoall_num_ppn_conf);
        mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						     mv2_alltoall_num_ppn_conf);
        mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
        mv2_alltoall_table_ppn_conf[0] = 1;
        mv2_size_alltoall_tuning_table[0] = 6;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_1ppn[] = {
	  {2,
	   1, 
	   {{0, -1, &MPIR_Alltoall_pairwise_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {4,
	   2,
	   {{0, 262144, &MPIR_Alltoall_Scatter_dest_MV2},
	    {262144, -1, &MPIR_Alltoall_pairwise_MV2},
	   },
                
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {8,
	   2,
	   {{0, 8, &MPIR_Alltoall_RD_MV2},
	    {8, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {16,
	   3,
	   {{0, 64, &MPIR_Alltoall_RD_MV2},
	    {64, 512, &MPIR_Alltoall_bruck_MV2},
	    {512, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0,-1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {32,
	   3,
	   {{0, 32, &MPIR_Alltoall_RD_MV2},
	    {32, 2048, &MPIR_Alltoall_bruck_MV2},
	    {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {64,
	   3,
	   {{0, 8, &MPIR_Alltoall_RD_MV2},
	    {8, 1024, &MPIR_Alltoall_bruck_MV2},
	    {1024, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
        };
        table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_1ppn;
        mv2_alltoall_table_ppn_conf[1] = 2;
        mv2_size_alltoall_tuning_table[1] = 6;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_2ppn[] = {
	  {4,
	   2,
	   {{0, 32, &MPIR_Alltoall_RD_MV2},
	    {32, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
                
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {8,
	   2,
	   {{0, 64, &MPIR_Alltoall_RD_MV2},
	    {64, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
                
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {16,
	   3,
	   {{0, 64, &MPIR_Alltoall_RD_MV2},
	    {64, 2048, &MPIR_Alltoall_bruck_MV2},
	    {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0,-1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {32,
	   3,
	   {{0, 16, &MPIR_Alltoall_RD_MV2},
	    {16, 2048, &MPIR_Alltoall_bruck_MV2},
	    {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
  
	  {64,
	   3,
	   {{0, 8, &MPIR_Alltoall_RD_MV2},
	    {8, 1024, &MPIR_Alltoall_bruck_MV2},
	    {1024, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },

	  {128,
	   3,
	   {{0, 4, &MPIR_Alltoall_RD_MV2},
	    {4, 2048, &MPIR_Alltoall_bruck_MV2},
	    {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	   },
  
	   {{0, -1, &MPIR_Alltoall_inplace_MV2},
	   },
	  },
        };
        table_ptrs[1] = mv2_tmp_alltoall_thresholds_table_2ppn;
        mv2_alltoall_table_ppn_conf[2] = 16;
        mv2_size_alltoall_tuning_table[2] = 7;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_16ppn[] = {
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
        table_ptrs[2] = mv2_tmp_alltoall_thresholds_table_16ppn;
        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_tuning_table[i];
        }
        mv2_alltoall_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
        MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_tuning_table)
                     * mv2_size_alltoall_tuning_table[0]));
        for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
	  mv2_alltoall_thresholds_table[i] =
            mv2_alltoall_thresholds_table[i - 1]
            + mv2_size_alltoall_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_tuning_table)
                       * mv2_size_alltoall_tuning_table[i]));
        }
        MPIU_Free(table_ptrs);
	return 0;
      } else {
        mv2_alltoall_num_ppn_conf = 1;
        mv2_alltoall_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			* mv2_alltoall_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				 * mv2_alltoall_num_ppn_conf);
        mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						     mv2_alltoall_num_ppn_conf);
        mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
        mv2_alltoall_table_ppn_conf[0] = 8;
        mv2_size_alltoall_tuning_table[0] = 7;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_8ppn[] = {
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
        table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_8ppn;
        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_tuning_table[i];
        }
        mv2_alltoall_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
        MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_tuning_table)
                     * mv2_size_alltoall_tuning_table[0]));
        for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
	  mv2_alltoall_thresholds_table[i] =
            mv2_alltoall_thresholds_table[i - 1]
            + mv2_size_alltoall_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_tuning_table)
                       * mv2_size_alltoall_tuning_table[i]));
        }
        MPIU_Free(table_ptrs);
	return 0;
      }
#elif defined (CHANNEL_NEMESIS_IB)
	if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
				 MV2_ARCH_INTEL_XEON_X5650_12, MV2_HCA_MLX_CX_QDR) && !heterogeneity) {
	  mv2_alltoall_num_ppn_conf = 1;
	  mv2_alltoall_thresholds_table
            = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			  * mv2_alltoall_num_ppn_conf);
	  table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				   * mv2_alltoall_num_ppn_conf);
	  mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						       mv2_alltoall_num_ppn_conf);
	  mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
	  mv2_alltoall_table_ppn_conf[0] = 12;
	  mv2_size_alltoall_tuning_table[0] = 6;
	  mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_12ppn[] = {
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
	  table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_12ppn;
	  agg_table_sum = 0;
	  for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
            agg_table_sum += mv2_size_alltoall_tuning_table[i];
	  }
	  mv2_alltoall_thresholds_table[0] =
            MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
		      (sizeof(mv2_alltoall_tuning_table)
		       * mv2_size_alltoall_tuning_table[0]));
	  for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
            mv2_alltoall_thresholds_table[i] =
	      mv2_alltoall_thresholds_table[i - 1]
	      + mv2_size_alltoall_tuning_table[i - 1];
            MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
			(sizeof(mv2_alltoall_tuning_table)
			 * mv2_size_alltoall_tuning_table[i]));
	  }
	  MPIU_Free(table_ptrs);
	  return 0;
	} else if (MV2_IS_ARCH_HCA_TYPE(MV2_get_arch_hca_type(),
					MV2_ARCH_INTEL_XEON_E5_2680_16, MV2_HCA_MLX_CX_FDR) && !heterogeneity) {
	  mv2_alltoall_num_ppn_conf = 3;
	  mv2_alltoall_thresholds_table
            = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			  * mv2_alltoall_num_ppn_conf);
	  table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				   * mv2_alltoall_num_ppn_conf);
	  mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						       mv2_alltoall_num_ppn_conf);
	  mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
	  mv2_alltoall_table_ppn_conf[0] = 1;
	  mv2_size_alltoall_tuning_table[0] = 6;
	  mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_1ppn[] = {
            {2,
	     1, 
	     {{0, -1, &MPIR_Alltoall_pairwise_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {4,
	     2,
	     {{0, 262144, &MPIR_Alltoall_Scatter_dest_MV2},
	      {262144, -1, &MPIR_Alltoall_pairwise_MV2},
	     },
                
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {8,
	     2,
	     {{0, 8, &MPIR_Alltoall_RD_MV2},
	      {8, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {16,
	     3,
	     {{0, 64, &MPIR_Alltoall_RD_MV2},
	      {64, 512, &MPIR_Alltoall_bruck_MV2},
	      {512, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0,-1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {32,
	     3,
	     {{0, 32, &MPIR_Alltoall_RD_MV2},
	      {32, 2048, &MPIR_Alltoall_bruck_MV2},
	      {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {64,
	     3,
	     {{0, 8, &MPIR_Alltoall_RD_MV2},
	      {8, 1024, &MPIR_Alltoall_bruck_MV2},
	      {1024, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
	  };
	  table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_1ppn;
	  mv2_alltoall_table_ppn_conf[1] = 2;
	  mv2_size_alltoall_tuning_table[1] = 6;
	  mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_2ppn[] = {
            {4,
	     2,
	     {{0, 32, &MPIR_Alltoall_RD_MV2},
	      {32, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
                
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {8,
	     2,
	     {{0, 64, &MPIR_Alltoall_RD_MV2},
	      {64, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
                
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {16,
	     3,
	     {{0, 64, &MPIR_Alltoall_RD_MV2},
	      {64, 2048, &MPIR_Alltoall_bruck_MV2},
	      {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0,-1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {32,
	     3,
	     {{0, 16, &MPIR_Alltoall_RD_MV2},
	      {16, 2048, &MPIR_Alltoall_bruck_MV2},
	      {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
  
            {64,
	     3,
	     {{0, 8, &MPIR_Alltoall_RD_MV2},
	      {8, 1024, &MPIR_Alltoall_bruck_MV2},
	      {1024, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },

            {128,
	     3,
	     {{0, 4, &MPIR_Alltoall_RD_MV2},
	      {4, 2048, &MPIR_Alltoall_bruck_MV2},
	      {2048, -1, &MPIR_Alltoall_Scatter_dest_MV2},
	     },
  
	     {{0, -1, &MPIR_Alltoall_inplace_MV2},
	     },
            },
	  };
	  table_ptrs[1] = mv2_tmp_alltoall_thresholds_table_2ppn;
	  mv2_alltoall_table_ppn_conf[2] = 16;
	  mv2_size_alltoall_tuning_table[2] = 7;
	  mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_16ppn[] = {
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
	  table_ptrs[2] = mv2_tmp_alltoall_thresholds_table_16ppn;
	  agg_table_sum = 0;
	  for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
            agg_table_sum += mv2_size_alltoall_tuning_table[i];
	  }
	  mv2_alltoall_thresholds_table[0] =
            MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
		      (sizeof(mv2_alltoall_tuning_table)
		       * mv2_size_alltoall_tuning_table[0]));
	  for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
            mv2_alltoall_thresholds_table[i] =
	      mv2_alltoall_thresholds_table[i - 1]
	      + mv2_size_alltoall_tuning_table[i - 1];
            MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
			(sizeof(mv2_alltoall_tuning_table)
			 * mv2_size_alltoall_tuning_table[i]));
	  }
	  MPIU_Free(table_ptrs);
	  return 0;
	} else {
	  mv2_alltoall_num_ppn_conf = 1;
	  mv2_alltoall_thresholds_table
            = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			  * mv2_alltoall_num_ppn_conf);
	  table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				   * mv2_alltoall_num_ppn_conf);
	  mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						       mv2_alltoall_num_ppn_conf);
	  mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
	  mv2_alltoall_table_ppn_conf[0] = 8;
	  mv2_size_alltoall_tuning_table[0] = 7;
	  mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_8ppn[] = {
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
	  table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_8ppn;
	  agg_table_sum = 0;
	  for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
            agg_table_sum += mv2_size_alltoall_tuning_table[i];
	  }
	  mv2_alltoall_thresholds_table[0] =
            MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
		      (sizeof(mv2_alltoall_tuning_table)
		       * mv2_size_alltoall_tuning_table[0]));
	  for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
            mv2_alltoall_thresholds_table[i] =
	      mv2_alltoall_thresholds_table[i - 1]
	      + mv2_size_alltoall_tuning_table[i - 1];
            MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
			(sizeof(mv2_alltoall_tuning_table)
			 * mv2_size_alltoall_tuning_table[i]));
	  }
	  MPIU_Free(table_ptrs);
	  return 0;
	}
#endif
#endif /* !CHANNEL_PSM */
      {
        mv2_alltoall_num_ppn_conf = 1;
        mv2_alltoall_thresholds_table
	  = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
			* mv2_alltoall_num_ppn_conf);
        table_ptrs = MPIU_Malloc(sizeof(mv2_alltoall_tuning_table *)
				 * mv2_alltoall_num_ppn_conf);
        mv2_size_alltoall_tuning_table = MPIU_Malloc(sizeof(int) *
						     mv2_alltoall_num_ppn_conf);
        mv2_alltoall_table_ppn_conf = MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
        mv2_alltoall_table_ppn_conf[0] = 8;
        mv2_size_alltoall_tuning_table[0] = 7;
        mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table_8ppn[] = {
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
        table_ptrs[0] = mv2_tmp_alltoall_thresholds_table_8ppn;
        agg_table_sum = 0;
        for (i = 0; i < mv2_alltoall_num_ppn_conf; i++) {
	  agg_table_sum += mv2_size_alltoall_tuning_table[i];
        }
        mv2_alltoall_thresholds_table[0] =
	  MPIU_Malloc(agg_table_sum * sizeof (mv2_alltoall_tuning_table));
        MPIU_Memcpy(mv2_alltoall_thresholds_table[0], table_ptrs[0],
                    (sizeof(mv2_alltoall_tuning_table)
                     * mv2_size_alltoall_tuning_table[0]));
        for (i = 1; i < mv2_alltoall_num_ppn_conf; i++) {
	  mv2_alltoall_thresholds_table[i] =
            mv2_alltoall_thresholds_table[i - 1]
            + mv2_size_alltoall_tuning_table[i - 1];
	  MPIU_Memcpy(mv2_alltoall_thresholds_table[i], table_ptrs[i],
                      (sizeof(mv2_alltoall_tuning_table)
                       * mv2_size_alltoall_tuning_table[i]));
        }
        MPIU_Free(table_ptrs);
	return 0;
      }
    }
    return 0;
}

void MV2_cleanup_alltoall_tuning_table()
{
  if (mv2_use_indexed_tuning || mv2_use_indexed_alltoall_tuning) {
    MPIU_Free(mv2_alltoall_indexed_thresholds_table[0]);
    MPIU_Free(mv2_alltoall_indexed_table_ppn_conf);
    MPIU_Free(mv2_size_alltoall_indexed_tuning_table);
    if (mv2_alltoall_indexed_thresholds_table != NULL) {
      MPIU_Free(mv2_alltoall_indexed_thresholds_table);
    }
  }
  else {
    MPIU_Free(mv2_alltoall_thresholds_table[0]);
    MPIU_Free(mv2_alltoall_table_ppn_conf);
    MPIU_Free(mv2_size_alltoall_tuning_table);
    if (mv2_alltoall_thresholds_table != NULL) {
        MPIU_Free(mv2_alltoall_thresholds_table);
    }
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
    if (mv2_use_indexed_tuning || mv2_use_indexed_alltoall_tuning) {
      mv2_alltoall_indexed_num_ppn_conf = 1;

      if (mv2_size_alltoall_indexed_tuning_table == NULL) {
        mv2_size_alltoall_indexed_tuning_table =
	  MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      }
      mv2_size_alltoall_indexed_tuning_table[0] = 1;
    
      if (mv2_alltoall_indexed_table_ppn_conf == NULL) {
        mv2_alltoall_indexed_table_ppn_conf =
	  MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf * sizeof(int));
      }
      mv2_alltoall_indexed_table_ppn_conf[0] = -1;

      mv2_alltoall_indexed_tuning_table mv2_tmp_alltoall_indexed_thresholds_table[1];

      /* If one alltoall_indexed tuning table is already defined */
      if (mv2_alltoall_indexed_thresholds_table != NULL) {
        MPIU_Free(mv2_alltoall_indexed_thresholds_table);
      }

      /* We realloc the space for the new alltoall_indexed tuning table */
      mv2_alltoall_indexed_thresholds_table =
	MPIU_Malloc(mv2_alltoall_indexed_num_ppn_conf *
		    sizeof(mv2_alltoall_indexed_tuning_table *));
      mv2_alltoall_indexed_thresholds_table[0] =
	MPIU_Malloc(mv2_size_alltoall_indexed_tuning_table[0] *
		    sizeof(mv2_alltoall_indexed_tuning_table));

      if (nb_element == 1) {
        mv2_tmp_alltoall_indexed_thresholds_table[0].numproc = 1;
        mv2_tmp_alltoall_indexed_thresholds_table[0].size_table = 1;
        mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].msg_sz = 1;
        switch (atoi(mv2_user_alltoall)) {
        case ALLTOALL_BRUCK_MV2:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_bruck_MV2;
	  break;
        case ALLTOALL_RD_MV2:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_RD_MV2;
	  break;
        case ALLTOALL_SCATTER_DEST_MV2:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_Scatter_dest_MV2;
	  break;
        case ALLTOALL_PAIRWISE_MV2:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_pairwise_MV2;
	  break;
        case ALLTOALL_INPLACE_MV2:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_inplace_MV2;
	  break;
        default:
	  mv2_tmp_alltoall_indexed_thresholds_table[0].algo_table[0].MV2_pt_Alltoall_function =
	    &MPIR_Alltoall_bruck_MV2;
        }
      }
      MPIU_Memcpy(mv2_alltoall_indexed_thresholds_table[0], mv2_tmp_alltoall_indexed_thresholds_table, sizeof
		  (mv2_alltoall_indexed_tuning_table));
    }
    else {
      mv2_alltoall_num_ppn_conf = 1;

      if (mv2_size_alltoall_tuning_table == NULL) {
        mv2_size_alltoall_tuning_table =
	  MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
      }
      mv2_size_alltoall_tuning_table[0] = 1;
    
      if (mv2_alltoall_table_ppn_conf == NULL) {
        mv2_alltoall_table_ppn_conf =
	  MPIU_Malloc(mv2_alltoall_num_ppn_conf * sizeof(int));
      }
      mv2_alltoall_table_ppn_conf[0] = -1;

      mv2_alltoall_tuning_table mv2_tmp_alltoall_thresholds_table[1];

      /* If one alltoall tuning table is already defined */
      if (mv2_alltoall_thresholds_table != NULL) {
        MPIU_Free(mv2_alltoall_thresholds_table);
      }

      /* We realloc the space for the new alltoall tuning table */
      mv2_alltoall_thresholds_table =
	MPIU_Malloc(mv2_alltoall_num_ppn_conf *
		    sizeof(mv2_alltoall_tuning_table *));
      mv2_alltoall_thresholds_table[0] =
	MPIU_Malloc(mv2_size_alltoall_tuning_table[0] *
		    sizeof(mv2_alltoall_tuning_table));

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
      MPIU_Memcpy(mv2_alltoall_thresholds_table[0], mv2_tmp_alltoall_thresholds_table, sizeof
		  (mv2_alltoall_tuning_table));
    }
    return 0;
}
