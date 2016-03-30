/*
 * Copyright (c) 2001-2016, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include "allgather/gen2_RI_1ppn.h"
#include "allgather/gen2_RI_2ppn.h"
#include "allgather/gen2_RI_8ppn.h"
#include "allgather/gen2_cma_RI_8ppn.h"
#include "allgather/psm_RI_1ppn.h"
#include "allgather/psm_RI_2ppn.h"
#include "allgather/psm_RI_8ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_1ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_2ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_20ppn.h"
#include "allgather/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_1ppn.h"
#include "allgather/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_12ppn.h"
#include "allgather/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "allgather/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "allgather/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "allgather/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "allgather/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "allgather/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "allgather/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "allgather/gen2_INTEL_XEON_E5_2680_24_MLX_CX_FDR_24ppn.h"
#include "allgather/nemesis_RI_1ppn.h"
#include "allgather/nemesis_RI_2ppn.h"
#include "allgather/nemesis_RI_8ppn.h"
#include "allgather/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "allgather/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "allgather/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "allgather/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "allgather/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "allgather/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "allgather/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
