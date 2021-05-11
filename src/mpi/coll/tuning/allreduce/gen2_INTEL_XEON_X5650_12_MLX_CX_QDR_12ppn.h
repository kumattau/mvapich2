/*
 * Copyright (c) 2001-2021, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#define GEN2__INTEL_XEON_X5650_12__MLX_CX_QDR__12PPN {		\
	{		\
	  12,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {16, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {512, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {1024, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {2048, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	},		\
		\
	{		\
	  24,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {16, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {512, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	},		\
		\
	{		\
	  48,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {16, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {1024, &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2},		\
	    {2048, &MPIR_Allreduce_mcst_reduce_two_level_helper_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {1024, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2048, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	},		\
		\
	{		\
	  96,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {16, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {1024, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {1024, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	},		\
		\
	{		\
	  192,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {16, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {64, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	},		\
		\
	{		\
	  384,		\
	  0,		\
	  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {8, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {512, &MPIR_Allreduce_mcst_reduce_redscat_gather_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {262144, &MPIR_Allreduce_pt2pt_rs_MV2}		\
	  },		\
	  18,		\
	  {		\
	    {1, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {2, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {64, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {256, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	    {1024, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {2048, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	    {4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	    {262144, &MPIR_Allreduce_reduce_p2p_MV2}		\
	  }		\
	}		\
  };
