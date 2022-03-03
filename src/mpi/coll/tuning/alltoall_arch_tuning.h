/*
 * Copyright (c) 2001-2022, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include "alltoall/gen2_cma_RI2_1ppn.h"
#include "alltoall/gen2_cma_RI2_2ppn.h"
#include "alltoall/gen2_cma_RI2_4ppn.h"
#include "alltoall/gen2_cma_RI2_8ppn.h"
#include "alltoall/gen2_cma_RI2_16ppn.h"
#include "alltoall/gen2_cma_RI2_28ppn.h"
#include "alltoall/gen2_RI2_1ppn.h"
#include "alltoall/gen2_RI2_2ppn.h"
#include "alltoall/gen2_RI2_4ppn.h"
#include "alltoall/gen2_RI2_8ppn.h"
#include "alltoall/gen2_RI2_16ppn.h"
#include "alltoall/gen2_RI2_28ppn.h"
#include "alltoall/gen2_RI_1ppn.h"
#include "alltoall/gen2_RI_2ppn.h"
#include "alltoall/gen2_RI_4ppn.h"
#include "alltoall/gen2_RI_8ppn.h"
#include "alltoall/gen2_cma_RI_4ppn.h"
#include "alltoall/gen2_cma_RI_8ppn.h"
#include "alltoall/psm_RI_1ppn.h"
#include "alltoall/psm_RI_2ppn.h"
#include "alltoall/psm_RI_8ppn.h"
#include "alltoall/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_1ppn.h"
#include "alltoall/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_12ppn.h"
#include "alltoall/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "alltoall/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "alltoall/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "alltoall/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "alltoall/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "alltoall/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_1ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_2ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_20ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_4ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_4ppn.h"
#include "alltoall/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/gen2_INTEL_XEON_E5_2680_24_MLX_CX_FDR_24ppn.h"
#include "alltoall/nemesis_RI_1ppn.h"
#include "alltoall/nemesis_RI_2ppn.h"
#include "alltoall/nemesis_RI_8ppn.h"
#include "alltoall/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "alltoall/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "alltoall/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "alltoall/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "alltoall/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "alltoall/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "alltoall/nemesis_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_1ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_2ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_4ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_8ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_16ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V3_2S_28_INTEL_HFI_100_28ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_1ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_4ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_8ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_16ppn.h"
#include "alltoall/psm_INTEL_XEON_E5_2695_V4_2S_36_INTEL_HFI_100_36ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_1ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_4ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_8ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_16ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_32ppn.h"
#include "alltoall/psm_INTEL_XEON_PHI_7250_68_INTEL_HFI_100_64ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_1ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_2ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_4ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_8ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_16ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_24ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_26ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_48ppn.h"
#include "alltoall/psm_INTEL_PLATINUM_8170_2S_52_INTEL_HFI_100_52ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_28_MLX_CX_FDR_1ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_28_MLX_CX_FDR_4ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_28_MLX_CX_FDR_8ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_28_MLX_CX_FDR_16ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_28_MLX_CX_FDR_24ppn.h"
#include "alltoall/gen2_cma_IBM_POWER8_MLX_CX_EDR_2ppn.h"
#include "alltoall/gen2_cma_IBM_POWER8_MLX_CX_EDR_4ppn.h"
#include "alltoall/gen2_cma_IBM_POWER8_MLX_CX_EDR_8ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_1ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_2ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_4ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_8ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_16ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_22ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_32ppn.h"
#include "alltoall/gen2_IBM_POWER9_MLX_CX_EDR_44ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_1ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_4ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_6ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_8ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_16ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_22ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_32ppn.h"
#include "alltoall/gen2_cma_IBM_POWER9_MLX_CX_EDR_44ppn.h"
#include "alltoall/gen2_AMD_EPYC_1ppn.h"
#include "alltoall/gen2_AMD_EPYC_2ppn.h"
#include "alltoall/gen2_AMD_EPYC_4ppn.h"
#include "alltoall/gen2_AMD_EPYC_8ppn.h"
#include "alltoall/gen2_AMD_EPYC_16ppn.h"
#include "alltoall/gen2_AMD_EPYC_32ppn.h"
#include "alltoall/gen2_AMD_EPYC_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_1ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_2ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_4ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_8ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_16ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_32ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_VENUS_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_1ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_4ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_8ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_16ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_32ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_120ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_ROME_128ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_1ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_2ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_4ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_8ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_16ppn.h"
#include "alltoall/gen2_cma_NOWHASWELL_20ppn.h"
#include "alltoall/gen2_cma_FRONTERA_1ppn.h"
#include "alltoall/gen2_cma_FRONTERA_2ppn.h"
#include "alltoall/gen2_cma_FRONTERA_4ppn.h"
#include "alltoall/gen2_cma_FRONTERA_8ppn.h"
#include "alltoall/gen2_cma_FRONTERA_16ppn.h"
#include "alltoall/gen2_cma_FRONTERA_28ppn.h"
#include "alltoall/gen2_cma_FRONTERA_32ppn.h"
#include "alltoall/gen2_cma_FRONTERA_56ppn.h"
#include "alltoall/gen2_cma_MAYER_1ppn.h"
#include "alltoall/gen2_cma_MAYER_2ppn.h"
#include "alltoall/gen2_cma_MAYER_4ppn.h"
#include "alltoall/gen2_cma_MAYER_8ppn.h"
#include "alltoall/gen2_cma_MAYER_16ppn.h"
#include "alltoall/gen2_cma_MAYER_28ppn.h"
#include "alltoall/gen2_cma_MAYER_32ppn.h"
#include "alltoall/gen2_cma_MAYER_56ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_1ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_2ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_4ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_8ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_16ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_32ppn.h"
#include "alltoall/gen2_cma_ARM_CAVIUM_V8_2S_32_MLX_CX_EDR_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_1ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_2ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_4ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_8ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_16ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_32ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7401_24_48ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_1ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_2ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_4ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_8ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_16ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_32ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_64ppn.h"
#include "alltoall/gen2_cma_AMD_EPYC_7763_128_128ppn.h"
