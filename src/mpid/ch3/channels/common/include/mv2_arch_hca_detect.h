/* Copyright (c) 2003-2010, The Ohio State University. All rights
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

#include <infiniband/verbs.h>
#include <infiniband/umad.h>

/* HCA type 
 * Note: Add new HCA types only at the end.
 */ 
typedef enum {
    MV2_HCA_UNKWN = 0, 
    MV2_HCA_MLX_PCI_EX_SDR, 
    MV2_HCA_MLX_PCI_EX_DDR, 
    MV2_HCA_MLX_CX_SDR,
    MV2_HCA_MLX_CX_DDR,
    MV2_HCA_MLX_CX_QDR,
    MV2_HCA_PATH_HT, 
    MV2_HCA_MLX_PCI_X, 
    MV2_HCA_IBM_EHCA,
    MV2_HCA_CHELSIO_T3,
    MV2_HCA_INTEL_NE020
} mv2_hca_type;

/* No. of different HCA Types */
#define MV2_NUM_HCA_TYPES 11


/* Architecture Type 
 * Note: Add new architecture types only at the end.
 */
typedef enum {
    MV2_ARCH_UNKWN = 0,
    MV2_ARCH_AMD_BARCELONA_16,
    MV2_ARCH_AMD_MAGNY_COURS_24,
    MV2_ARCH_INTEL_CLOVERTOWN_8,
    MV2_ARCH_INTEL_NEHALEM_8,
    MV2_ARCH_INTEL_NEHALEM_16,
    MV2_ARCH_INTEL_HARPERTOWN_8,
    MV2_ARCH_INTEL_XEON_DUAL_4,
    MV2_ARCH_AMD_OPTERON_DUAL_4,
    MV2_ARCH_INTEL,
    MV2_ARCH_AMD,
    MV2_ARCH_IBM_PPC,
    MV2_ARCH_INTEL_XEON_E5630_8,
} mv2_arch_type;

/* No. of different architecture types */
#define MV2_NUM_ARCH_TYPES 13

/* Possible Architecture- Card Combinations */
typedef enum {

    /* Arch Type = MV2_ARCH_UNKWN */
    MV2_ARHC_UNKWN_HCA_UNKWN=0,  
    MV2_ARHC_UNKWN_HCA_MLX_PCI_EX_SDR, 
    MV2_ARHC_UNKWN_HCA_MLX_PCI_EX_DDR, 
    MV2_ARHC_UNKWN_HCA_MLX_CX_SDR,
    MV2_ARHC_UNKWN_HCA_MLX_CX_DDR,
    MV2_ARHC_UNKWN_HCA_MLX_CX_QDR,
    MV2_ARHC_UNKWN_HCA_PATH_HT, 
    MV2_ARHC_UNKWN_HCA_MLX_PCI_X, 
    MV2_ARHC_UNKWN_HCA_IBM_EHCA,
    MV2_ARHC_UNKWN_HCA_CHELSIO_T3,
    MV2_ARHC_UNKWN_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_AMD_BARCELONA */
    MV2_ARCH_AMD_BRCLNA_16_HCA_UNKWN,    
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_SDR,
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR,
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_QDR,
    MV2_ARCH_AMD_BRCLNA_16_HCA_PATH_HT,
    MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_X,
    MV2_ARCH_AMD_BRCLNA_16_HCA_IBM_EHCA,
    MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T3,
    MV2_ARCH_AMD_BRCLNA_16_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_AMD_MAGNY_COURS */
    MV2_ARCH_AMD_MGNYCRS_24_HCA_UNKWN,    
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_SDR,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_DDR,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_PATH_HT,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_X,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_IBM_EHCA,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T3,
    MV2_ARCH_AMD_MGNYCRS_24_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL_CLOVERTOWN */
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_UNKWN,    
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_PATH_HT,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL_NEHALEM (8) */
    MV2_ARCH_INTEL_NEHLM_8_HCA_UNKWN,    
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_NEHLM_8_HCA_PATH_HT,
    MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_NEHLM_8_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_NEHLM_8_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL_NEHALEM (16)*/
    MV2_ARCH_INTEL_NEHLM_16_HCA_UNKWN,    
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_NEHLM_16_HCA_PATH_HT,
    MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_NEHLM_16_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_NEHLM_16_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL_HARPERTOWN_COM */
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_UNKWN,    
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_PATH_HT,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL_XEON_DUAL_4 */
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_UNKWN,    
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_PATH_HT,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_AMD_OPTERON_DUAL */
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_UNKWN,    
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_SDR,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_DDR,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_QDR,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_PATH_HT,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_X,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_IBM_EHCA,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T3,
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_INTEL */
    MV2_ARCH_INTEL_HCA_UNKWN,    
    MV2_ARCH_INTEL_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_HCA_PATH_HT,
    MV2_ARCH_INTEL_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_AMD */
    MV2_ARCH_AMD_HCA_UNKWN,    
    MV2_ARCH_AMD_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_AMD_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_AMD_HCA_MLX_CX_SDR,
    MV2_ARCH_AMD_HCA_MLX_CX_DDR,
    MV2_ARCH_AMD_HCA_MLX_CX_QDR,
    MV2_ARCH_AMD_HCA_PATH_HT,
    MV2_ARCH_AMD_HCA_MLX_PCI_X,
    MV2_ARCH_AMD_HCA_IBM_EHCA,
    MV2_ARCH_AMD_HCA_CHELSIO_T3,
    MV2_ARCH_AMD_HCA_INTEL_NE020,

    /* Arch Type = MV2_ARCH_IBM_PPC */
    MV2_ARCH_IBM_PPC_HCA_UNKWN,    
    MV2_ARCH_IBM_PPC_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_IBM_PPC_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_IBM_PPC_HCA_MLX_CX_SDR,
    MV2_ARCH_IBM_PPC_HCA_MLX_CX_DDR,
    MV2_ARCH_IBM_PPC_HCA_MLX_CX_QDR,
    MV2_ARCH_IBM_PPC_HCA_PATH_HT,
    MV2_ARCH_IBM_PPC_HCA_MLX_PCI_X,
    MV2_ARCH_IBM_PPC_HCA_IBM_EHCA,
    MV2_ARCH_IBM_PPC_HCA_CHELSIO_T3,
    MV2_ARCH_IBM_PPC_HCA_INTEL_NE020,

    /* Arch Type : MV2_ARCH_INTEL_XEON_E5630_8 */
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_UNKWN,    
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_EX_SDR,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_EX_DDR,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_SDR,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_DDR,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_PATH_HT,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_X,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_IBM_EHCA,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_CHELSIO_T3,
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_INTEL_NE020,
} mv2_arch_hca_type;


/* Multi-rail info */
typedef enum{
    mv2_num_rail_unknown = 0,
    mv2_num_rail_1,
    mv2_num_rail_2,
    mv2_num_rail_3,
    mv2_num_rail_4,
} mv2_multirail_info_type;

/* API to get architecture-hca type */
mv2_arch_hca_type mv2_get_arch_hca_type ( struct ibv_device *dev );

/* API to check if the host has multiple rails or not */
mv2_multirail_info_type mv2_get_multirail_info ();

/* API for getting architecture type */
mv2_arch_type mv2_get_arch_type();

/* API for getting the card type */
mv2_hca_type mv2_get_hca_type( struct ibv_device *dev );

