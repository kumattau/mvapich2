#define GEN2_CMA__FRONTERA__16PPN {		\
	{		\
	16,		\
	0,		\
	{0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{64, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{128, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{64, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16384, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	},		 \
	{		\
	32,		\
	0,		\
	{0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	}		\
	},		 \
	{		\
	64,		\
	0,		\
	{0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8192, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{2097152, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	},		 \
	{		\
	128,		\
	0,		\
	{1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{64, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{128, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{256, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{128, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{256, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{4096, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{8192, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	},		 \
	{		\
	256,		\
	0,		\
	{0, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{256, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{16, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{128, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{256, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{2097152, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	},		 \
	{		\
	512,		\
	0,		\
	{1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{512, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{1024, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{8, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{16, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{64, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{128, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{512, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{1024, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{2048, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{4096, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{4194304, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	},		 \
	{		\
	1024,		\
	0,		\
	{1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{16, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{32, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{256, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{512, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16384, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{32768, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{65536, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{131072, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{262144, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{524288, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{1048576, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{2097152, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2}		\
	},		\
	25,		\
	{		\
	{4, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{8, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{16, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{32, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{64, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{128, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{256, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{512, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{1024, &MPIR_Allreduce_socket_aware_two_level_MV2},		\
	{2048, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{4096, &MPIR_Allreduce_pt2pt_rs_MV2},		\
	{8192, &MPIR_Allreduce_pt2pt_rd_MV2},		\
	{16384, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{32768, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{65536, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{131072, &MPIR_Allreduce_reduce_shmem_MV2},		\
	{262144, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{524288, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{1048576, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{2097152, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{4194304, &MPIR_Allreduce_pt2pt_reduce_scatter_allgather_MV2},		\
	{8388608, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{16777216, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2},		\
	{33554432, &MPIR_Allreduce_reduce_p2p_MV2},		\
	{67108864, &MPIR_Allreduce_pt2pt_ring_wrapper_MV2}		\
	}		\
	}		 \
}
