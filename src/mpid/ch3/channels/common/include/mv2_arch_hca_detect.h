/* Copyright (c) 2003-2011, The Ohio State University. All rights
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
    MV2_HCA_CHELSIO_T4,
    MV2_HCA_INTEL_NE020,
    /* No. of different HCA Types */
    /* This has to come at the end, DO NOT change the position */
    MV2_NUM_HCA_TYPES
} mv2_hca_type;

/* Check if given card is IB card or not */
#define MV2_IS_IB_CARD( x ) ( (MV2_HCA_MLX_PCI_EX_SDR == (x)) || (MV2_HCA_MLX_PCI_EX_DDR == (x)) || (MV2_HCA_MLX_CX_SDR == (x) )|| (MV2_HCA_MLX_CX_DDR == (x)) || (MV2_HCA_MLX_CX_QDR == (x)) || (MV2_HCA_PATH_HT == (x)) || (MV2_HCA_MLX_PCI_X == (x)) || (MV2_HCA_IBM_EHCA == (x)) )

/* Check if given card is iWarp card or not */
#define MV2_IS_IWARP_CARD( x ) ( (MV2_HCA_CHELSIO_T3 == (x)) || (MV2_HCA_INTEL_NE020 == (x)) || (MV2_HCA_CHELSIO_T4 == (x)) )

/* Check if given card is Chelsio iWarp card or not */
#define MV2_IS_CHELSIO_IWARP_CARD( x ) ( (MV2_HCA_CHELSIO_T3 == (x)) || (MV2_HCA_CHELSIO_T4 == (x)) )

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
    MV2_ARCH_UNKWN_HCA_UNKWN=0,  
    MV2_ARCH_UNKWN_HCA_MLX_PCI_EX_SDR, 
    MV2_ARCH_UNKWN_HCA_MLX_PCI_EX_DDR, 
    MV2_ARCH_UNKWN_HCA_MLX_CX_SDR,
    MV2_ARCH_UNKWN_HCA_MLX_CX_DDR,
    MV2_ARCH_UNKWN_HCA_MLX_CX_QDR,
    MV2_ARCH_UNKWN_HCA_PATH_HT, 
    MV2_ARCH_UNKWN_HCA_MLX_PCI_X, 
    MV2_ARCH_UNKWN_HCA_IBM_EHCA,
    MV2_ARCH_UNKWN_HCA_CHELSIO_T3,
    MV2_ARCH_UNKWN_HCA_CHELSIO_T4,
    MV2_ARCH_UNKWN_HCA_INTEL_NE020,

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
    MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T4,
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
    MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T4,
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
    MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_HCA_CHELSIO_T4,
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
    MV2_ARCH_AMD_HCA_CHELSIO_T4,
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
    MV2_ARCH_IBM_PPC_HCA_CHELSIO_T4,
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
    MV2_ARCH_INTEL_XEON_E5630_8_HCA_CHELSIO_T4,
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

/* All arch combinations with Mellanox ConnectX DDR Cards */
#define CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR case MV2_ARCH_UNKWN_HCA_MLX_CX_DDR: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_DDR: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_HCA_MLX_CX_DDR: \
    case MV2_ARCH_AMD_HCA_MLX_CX_DDR: \
    case MV2_ARCH_IBM_PPC_HCA_MLX_CX_DDR: \
    case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_DDR
    
/* All arch combinations with Mellanox ConnectX QDR Cards */
#define CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR case MV2_ARCH_UNKWN_HCA_MLX_CX_QDR: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_QDR: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_QDR: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_QDR: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_QDR: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_QDR: \
    case MV2_ARCH_INTEL_HCA_MLX_CX_QDR: \
    case MV2_ARCH_AMD_HCA_MLX_CX_QDR: \
    case MV2_ARCH_IBM_PPC_HCA_MLX_CX_QDR

/* All arch combinations with Mellanox PCI-X Cards */
#define CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X case MV2_ARCH_UNKWN_HCA_MLX_PCI_X: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_X: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_X: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_X: \
    case MV2_ARCH_INTEL_HCA_MLX_PCI_X: \
    case MV2_ARCH_AMD_HCA_MLX_PCI_X: \
    case MV2_ARCH_IBM_PPC_HCA_MLX_PCI_X

/* All arch combinations with IBM EHCA Cards */
#define CASE_MV2_ANY_ARCH_WITH_IBM_EHCA case MV2_ARCH_UNKWN_HCA_IBM_EHCA: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_IBM_EHCA: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_XEON_DUAL_4_HCA_IBM_EHCA: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_IBM_EHCA: \
    case MV2_ARCH_INTEL_HCA_IBM_EHCA: \
    case MV2_ARCH_AMD_HCA_IBM_EHCA: \
    case MV2_ARCH_IBM_PPC_HCA_IBM_EHCA

/* All arch combinations with CHELSIO-T3 Cards */
#define CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3 case MV2_ARCH_UNKWN_HCA_CHELSIO_T3: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T3: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T3: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T3: \
    case MV2_ARCH_INTEL_HCA_CHELSIO_T3: \
    case MV2_ARCH_AMD_HCA_CHELSIO_T3: \
    case MV2_ARCH_IBM_PPC_HCA_CHELSIO_T3

/* All arch combinations with CHELSIO-T4 Cards */
#define CASE_MV2_ANY_ARCH_WITH_CHELSIO_T4 case MV2_ARCH_UNKWN_HCA_CHELSIO_T4: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T4: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T4: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T4: \
    case MV2_ARCH_INTEL_HCA_CHELSIO_T4: \
    case MV2_ARCH_AMD_HCA_CHELSIO_T4: \
    case MV2_ARCH_IBM_PPC_HCA_CHELSIO_T4

/* All arch combinations with Intel NE020 Cards */
#define CASE_MV2_ANY_ARCH_WITH_INTEL_NE020 case MV2_ARCH_UNKWN_HCA_INTEL_NE020: \
    case MV2_ARCH_AMD_BRCLNA_16_HCA_INTEL_NE020: \
    case MV2_ARCH_AMD_MGNYCRS_24_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_CLVRTWN_8_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_NEHLM_8_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_NEHLM_16_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_HRPRTWN_8_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_XEON_DUAL_4_HCA_INTEL_NE020: \
    case MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_INTEL_NE020: \
    case MV2_ARCH_INTEL_HCA_INTEL_NE020: \
    case MV2_ARCH_AMD_HCA_INTEL_NE020: \
    case MV2_ARCH_IBM_PPC_HCA_INTEL_NE020

/* ************************ FUNCTION DECLARATIONS ************************** */

/* API to get architecture-hca type */
mv2_arch_hca_type mv2_get_arch_hca_type ( struct ibv_device *dev );

/* API to check if the host has multiple rails or not */
mv2_multirail_info_type mv2_get_multirail_info(void);

/* API for getting architecture type */
mv2_arch_type mv2_get_arch_type(void);

/* API for getting the card type */
mv2_hca_type mv2_get_hca_type( struct ibv_device *dev );

/* API for getting the number of cpus */
int mv2_get_num_cpus(void);

