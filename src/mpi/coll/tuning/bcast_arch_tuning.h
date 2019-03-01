/*
 * Copyright (c) 2001-2019, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include "bcast/gen2_cma_RI2_1ppn.h"
#include "bcast/gen2_cma_RI2_2ppn.h"
#include "bcast/gen2_cma_RI2_4ppn.h"
#include "bcast/gen2_cma_RI2_8ppn.h"
#include "bcast/gen2_cma_RI2_16ppn.h"
#include "bcast/gen2_cma_RI2_28ppn.h"
#include "bcast/gen2_RI2_1ppn.h"
#include "bcast/gen2_RI2_2ppn.h"
#include "bcast/gen2_RI2_4ppn.h"
#include "bcast/gen2_RI2_8ppn.h"
#include "bcast/gen2_RI2_16ppn.h"
#include "bcast/gen2_RI2_28ppn.h"
#include "bcast/gen2_RI_1ppn.h"
#include "bcast/gen2_RI_2ppn.h"
#include "bcast/gen2_RI_4ppn.h"
#include "bcast/gen2_RI_8ppn.h"
#include "bcast/psm_RI_1ppn.h"
#include "bcast/psm_RI_2ppn.h"
#include "bcast/psm_RI_8ppn.h"
#include "bcast/gen2_cma_RI_1ppn.h"
#include "bcast/gen2_cma_RI_2ppn.h"
#include "bcast/gen2_cma_RI_4ppn.h"
#include "bcast/gen2_cma_RI_8ppn.h"
#include "bcast/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_1ppn.h"
#include "bcast/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_12ppn.h"
#include "bcast/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "bcast/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "bcast/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "bcast/gen2_cma_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "bcast/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "bcast/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "bcast/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_1ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_2ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_20ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_1ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_2ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_12ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_4ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_4ppn.h"
#include "bcast/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "bcast/gen2_INTEL_XEON_E5_2680_24_MLX_CX_FDR_24ppn.h"
#include "bcast/nemesis_RI_1ppn.h"
#include "bcast/nemesis_RI_2ppn.h"
#include "bcast/nemesis_RI_8ppn.h"
#include "bcast/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "bcast/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "bcast/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "bcast/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_1ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_2ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_4ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_8ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_16ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_28ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_1ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_4ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_8ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_16ppn.h"
#include "bcast/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_36ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_1ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_4ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_8ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_32ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_16ppn.h"
#include "bcast/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_64ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_1ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_2ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_4ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_8ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_16ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_24ppn.h"
#include "bcast/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_48ppn.h"
#include "bcast/gen2_cma_ARM_CAVIUM_V8_MLX_CX_FDR_1ppn.h"
#include "bcast/gen2_cma_ARM_CAVIUM_V8_MLX_CX_FDR_4ppn.h"
#include "bcast/gen2_cma_ARM_CAVIUM_V8_MLX_CX_FDR_8ppn.h"
#include "bcast/gen2_cma_ARM_CAVIUM_V8_MLX_CX_FDR_16ppn.h"
#include "bcast/gen2_cma_ARM_CAVIUM_V8_MLX_CX_FDR_24ppn.h"
#include "bcast/gen2_cma_IBM_POWER8_MLX_CX_EDR_2ppn.h"
#include "bcast/gen2_cma_IBM_POWER8_MLX_CX_EDR_4ppn.h"
#include "bcast/gen2_cma_IBM_POWER8_MLX_CX_EDR_8ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_1ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_2ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_4ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_8ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_16ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_22ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_32ppn.h"
#include "bcast/gen2_IBM_POWER9_MLX_CX_EDR_44ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_1ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_4ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_8ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_16ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_22ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_32ppn.h"
#include "bcast/gen2_cma_IBM_POWER9_MLX_CX_EDR_44ppn.h"
#include "bcast/gen2_AMD_EPYC_1ppn.h"
#include "bcast/gen2_AMD_EPYC_2ppn.h"
#include "bcast/gen2_AMD_EPYC_4ppn.h"
#include "bcast/gen2_AMD_EPYC_8ppn.h"
#include "bcast/gen2_AMD_EPYC_16ppn.h"
#include "bcast/gen2_AMD_EPYC_32ppn.h"
#include "bcast/gen2_AMD_EPYC_64ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_1ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_2ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_4ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_8ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_16ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_32ppn.h"
#include "bcast/gen2_cma_AMD_EPYC_64ppn.h"
