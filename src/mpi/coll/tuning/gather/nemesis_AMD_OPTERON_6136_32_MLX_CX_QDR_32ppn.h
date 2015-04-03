/*
 * Copyright (c) 2001-2015, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#define NEMESIS__AMD_OPTERON_6136_32__MLX_CX_QDR__32PPN {		\
	{		\
	  64,		\
	  20,		\
	  {		\
	    {1, &MPIR_Gather_MV2_two_level_Direct},		\
	    {2, &MPIR_Gather_MV2_two_level_Direct},		\
	    {4, &MPIR_Gather_MV2_two_level_Direct},		\
	    {8, &MPIR_Gather_MV2_two_level_Direct},		\
	    {16, &MPIR_Gather_MV2_two_level_Direct},		\
	    {32, &MPIR_Gather_MV2_two_level_Direct},		\
	    {64, &MPIR_Gather_MV2_two_level_Direct},		\
	    {128, &MPIR_Gather_MV2_two_level_Direct},		\
	    {256, &MPIR_Gather_MV2_two_level_Direct},		\
	    {512, &MPIR_Gather_MV2_two_level_Direct},		\
	    {1024, &MPIR_Gather_MV2_Direct},		\
	    {2048, &MPIR_Gather_MV2_Direct},		\
	    {4096, &MPIR_Gather_MV2_Direct},		\
	    {8192, &MPIR_Gather_MV2_Direct},		\
	    {16384, &MPIR_Gather_MV2_two_level_Direct},		\
	    {32768, &MPIR_Gather_MV2_two_level_Direct},		\
	    {65536, &MPIR_Gather_MV2_two_level_Direct},		\
	    {131072, &MPIR_Gather_MV2_two_level_Direct},		\
	    {262144, &MPIR_Gather_MV2_two_level_Direct},		\
	    {524288, &MPIR_Gather_MV2_two_level_Direct},		\
	    {1048576, &MPIR_Gather_MV2_two_level_Direct}		\
	  },		\
	  20,		\
	  {		\
	    {1, &MPIR_Gather_MV2_Direct},		\
	    {2, &MPIR_Gather_MV2_Direct},		\
	    {4, &MPIR_Gather_MV2_Direct},		\
	    {8, &MPIR_Gather_MV2_Direct},		\
	    {16, &MPIR_Gather_MV2_Direct},		\
	    {32, &MPIR_Gather_MV2_Direct},		\
	    {64, &MPIR_Gather_MV2_Direct},		\
	    {128, &MPIR_Gather_MV2_Direct},		\
	    {256, &MPIR_Gather_MV2_Direct},		\
	    {512, &MPIR_Gather_MV2_Direct},		\
	    {1024, &MPIR_Gather_MV2_Direct},		\
	    {2048, &MPIR_Gather_MV2_Direct},		\
	    {4096, &MPIR_Gather_MV2_Direct},		\
	    {8192, &MPIR_Gather_MV2_Direct},		\
	    {16384, &MPIR_Gather_MV2_Direct},		\
	    {32768, &MPIR_Gather_MV2_Direct},		\
	    {65536, &MPIR_Gather_MV2_Direct},		\
	    {131072, &MPIR_Gather_MV2_Direct},		\
	    {262144, &MPIR_Gather_MV2_Direct},		\
	    {524288, &MPIR_Gather_MV2_Direct},		\
	    {1048576, &MPIR_Gather_MV2_Direct}		\
	  }		\
	},		\
		\
	{		\
	  128,		\
	  20,		\
	  {		\
	    {1, &MPIR_Gather_MV2_two_level_Direct},		\
	    {2, &MPIR_Gather_MV2_two_level_Direct},		\
	    {4, &MPIR_Gather_MV2_two_level_Direct},		\
	    {8, &MPIR_Gather_MV2_two_level_Direct},		\
	    {16, &MPIR_Gather_MV2_two_level_Direct},		\
	    {32, &MPIR_Gather_MV2_two_level_Direct},		\
	    {64, &MPIR_Gather_MV2_two_level_Direct},		\
	    {128, &MPIR_Gather_MV2_two_level_Direct},		\
	    {256, &MPIR_Gather_MV2_two_level_Direct},		\
	    {512, &MPIR_Gather_MV2_two_level_Direct},		\
	    {1024, &MPIR_Gather_MV2_two_level_Direct},		\
	    {2048, &MPIR_Gather_MV2_Direct},		\
	    {4096, &MPIR_Gather_MV2_Direct},		\
	    {8192, &MPIR_Gather_MV2_Direct},		\
	    {16384, &MPIR_Gather_MV2_two_level_Direct},		\
	    {32768, &MPIR_Gather_MV2_two_level_Direct},		\
	    {65536, &MPIR_Gather_MV2_two_level_Direct},		\
	    {131072, &MPIR_Gather_MV2_two_level_Direct},		\
	    {262144, &MPIR_Gather_MV2_two_level_Direct},		\
	    {524288, &MPIR_Gather_MV2_two_level_Direct},		\
	    {1048576, &MPIR_Gather_MV2_two_level_Direct}		\
	  },		\
	  20,		\
	  {		\
	    {1, &MPIR_Gather_MV2_Direct},		\
	    {2, &MPIR_Gather_MV2_Direct},		\
	    {4, &MPIR_Gather_MV2_Direct},		\
	    {8, &MPIR_Gather_MV2_Direct},		\
	    {16, &MPIR_Gather_MV2_Direct},		\
	    {32, &MPIR_Gather_MV2_Direct},		\
	    {64, &MPIR_Gather_MV2_Direct},		\
	    {128, &MPIR_Gather_MV2_Direct},		\
	    {256, &MPIR_Gather_MV2_Direct},		\
	    {512, &MPIR_Gather_MV2_Direct},		\
	    {1024, &MPIR_Gather_MV2_Direct},		\
	    {2048, &MPIR_Gather_MV2_Direct},		\
	    {4096, &MPIR_Gather_MV2_Direct},		\
	    {8192, &MPIR_Gather_MV2_Direct},		\
	    {16384, &MPIR_Gather_MV2_Direct},		\
	    {32768, &MPIR_Gather_MV2_Direct},		\
	    {65536, &MPIR_Gather_MV2_Direct},		\
	    {131072, &MPIR_Gather_MV2_Direct},		\
	    {262144, &MPIR_Gather_MV2_Direct},		\
	    {524288, &MPIR_Gather_MV2_Direct},		\
	    {1048576, &MPIR_Gather_MV2_Direct}		\
	  }		\
	}		\
};		