/* Copyright (c) 2001-2016, The Ohio State University. All rights
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

#ifndef _COMMON_TUNING_
#define _COMMON_TUNING_

#define MV2_COLL_TUNING_SETUP_TABLE(_cname)                     \
    int *mv2_##_cname##_table_ppn_conf = NULL;                  \
    int mv2_##_cname##_num_ppn_conf = 1;                        \
    int *mv2_size_##_cname##_tuning_table = NULL;               \
    mv2_##_cname##_tuning_table                                 \
        **mv2_##_cname##_thresholds_table = NULL;               \
    int *mv2_##_cname##_indexed_table_ppn_conf = NULL;          \
    int mv2_##_cname##_indexed_num_ppn_conf = 1;                \
    int *mv2_size_##_cname##_indexed_tuning_table = NULL;       \
    mv2_##_cname##_indexed_tuning_table                         \
        **mv2_##_cname##_indexed_thresholds_table = NULL;

#define MV2_COLL_TUNING_START_TABLE(_cname, _nconf)                             \
{                                                                               \
    int idx = -1, nconf = _nconf;                                               \
    mv2_##_cname##_indexed_num_ppn_conf = nconf;                                \
    mv2_##_cname##_indexed_thresholds_table = MPIU_Malloc(                      \
        sizeof(mv2_##_cname##_indexed_tuning_table *) * nconf);                 \
    table_ptrs = MPIU_Malloc(                                                   \
        sizeof(mv2_##_cname##_indexed_tuning_table *) * nconf);                 \
    mv2_size_##_cname##_indexed_tuning_table = MPIU_Malloc(                     \
            sizeof(int) * nconf);                                               \
    mv2_##_cname##_indexed_table_ppn_conf = MPIU_Malloc(                        \
            sizeof(int) * nconf);

#define MV2_COLL_TUNING_ADD_CONF(_cname, _ppn, _size, _name)                    \
  ++idx;                                                                        \
  mv2_##_cname##_indexed_tuning_table tmp_##_cname##_ppn[] = _name;             \
  mv2_##_cname##_indexed_table_ppn_conf[idx] = _ppn;                            \
  mv2_size_##_cname##_indexed_tuning_table[idx] = _size;                        \
  table_ptrs[idx] = tmp_##_cname##_ppn;                                         \

#if defined(_SMP_CMA_)
#define MV2_COLL_TUNING_ADD_CONF_CMA(_cname, _ppn, _size, _name)                \
  mv2_##_cname##_indexed_tuning_table tmp_cma_##_cname##_ppn[] = _name;         \
  if (g_smp_use_cma) {                                                          \
    mv2_##_cname##_indexed_table_ppn_conf[idx] = _ppn;                          \
    mv2_size_##_cname##_indexed_tuning_table[idx] = _size;                      \
    table_ptrs[idx] = tmp_cma_##_cname##_ppn;                                   \
  }
#else
#define MV2_COLL_TUNING_ADD_CONF_CMA(_cname, _ppn, _size, _name)
#endif

#define MV2_COLL_TUNING_FINISH_TABLE(_cname)                        \
    agg_table_sum = 0;                                              \
    for (i = 0; i < nconf; i++) {                                   \
        agg_table_sum +=                                            \
            mv2_size_##_cname##_indexed_tuning_table[i];            \
    }                                                               \
    mv2_##_cname##_indexed_thresholds_table[0] = MPIU_Malloc(       \
        sizeof (mv2_##_cname##_indexed_tuning_table) *              \
        agg_table_sum);                                             \
    MPIU_Memcpy(mv2_##_cname##_indexed_thresholds_table[0],         \
        table_ptrs[0],                                              \
        sizeof(mv2_##_cname##_indexed_tuning_table) *               \
        mv2_size_##_cname##_indexed_tuning_table[0]);               \
    for (i = 1; i < nconf; i++) {                                   \
        mv2_##_cname##_indexed_thresholds_table[i] =                \
            mv2_##_cname##_indexed_thresholds_table[i - 1]          \
            + mv2_size_##_cname##_indexed_tuning_table[i - 1];      \
        MPIU_Memcpy(mv2_##_cname##_indexed_thresholds_table[i],     \
            table_ptrs[i],                                          \
            sizeof(mv2_##_cname##_indexed_tuning_table) *           \
            mv2_size_##_cname##_indexed_tuning_table[i]);           \
    }                                                               \
    MPIU_Free(table_ptrs);                                          \
    return 0;                                                       \
}

#endif
