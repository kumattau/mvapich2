#include "reduce/gen2_RI_1ppn.h"
#include "reduce/gen2_RI_2ppn.h"
#include "reduce/gen2_RI_8ppn.h"
#include "reduce/gen2_cma_RI_8ppn.h"
#include "reduce/psm_RI_1ppn.h"
#include "reduce/psm_RI_2ppn.h"
#include "reduce/psm_RI_8ppn.h"
#include "reduce/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_1ppn.h"
#include "reduce/psm_INTEL_XEON_X5650_12_MV2_HCA_QLGIC_QIB_12ppn.h"
#include "reduce/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "reduce/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "reduce/gen2_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "reduce/gen2_cma_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "reduce/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_1ppn.h"
#include "reduce/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_2ppn.h"
#include "reduce/gen2_INTEL_XEON_X5650_12_MLX_CX_QDR_12ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_1ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_2ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2690_V2_2S_20_MLX_CX_CONNIB_20ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_1ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_2ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2630_V2_2S_12_MLX_CX_CONNIB_12ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "reduce/gen2_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "reduce/gen2_cma_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
#include "reduce/nemesis_RI_1ppn.h"
#include "reduce/nemesis_RI_2ppn.h"
#include "reduce/nemesis_RI_8ppn.h"
#include "reduce/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_1ppn.h"
#include "reduce/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_2ppn.h"
#include "reduce/nemesis_AMD_OPTERON_6136_32_MLX_CX_QDR_32ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_1ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_2ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_QDR_16ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_1ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_2ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2670_16_MLX_CX_FDR_16ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_1ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_2ppn.h"
#include "reduce/nemesis_INTEL_XEON_E5_2680_16_MLX_CX_FDR_16ppn.h"
