/* Copyright (c) 2003-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH2 in the top level MVAPICH2 directory.
 *
 */

#include "vapi.h"
#include "vapi_util.h"

void Print_err(int s)
{
    switch (s) {
    case VAPI_LOC_LEN_ERR:
        printf("VAPI_LOC_LEN_ERR\n");
        break;
    case VAPI_LOC_PROT_ERR:
        printf("VAPI_LOC_PROT_ERR\n");
        break;
    case VAPI_WR_FLUSH_ERR:
        printf("VAPI_WR_FLUSH_ERR\n");
        break;
    case VAPI_MW_BIND_ERR:
        printf("VAPU_MW_BIND_ERR\n");
        break;
    case VAPI_REM_INV_REQ_ERR:
        printf("VAPI_REM_INV_REQ_ERR\n");
        break;
    case VAPI_REM_ACCESS_ERR:
        printf("VAPI_REM_ACCESS_ERR\n");
        break;
    case VAPI_REM_OP_ERR:
        printf("VAPI_REM_OP_ERR\n");
        break;
    case VAPI_RETRY_EXC_ERR:
        printf("VAPI_RETRY_EXC_ERR\n");
        break;
    case VAPI_RNR_RETRY_EXC_ERR:
        printf("VAPI_RNR_RETRY_EXC_ERR\n");
        break;
    case VAPI_INV_EECN_ERR:
        printf("VAPI_INV_EECN_ERR\n");
        break;
    case VAPI_INV_EEC_STATE_ERR:
        printf("VAPI_INV_EEC_STATE_ERR\n");
        break;
    case VAPI_SUCCESS:
        printf("VAPI_SUCCESS\n");
        break;
    default:
        fprintf(stdout, "unmatched\n");
        fflush(stdout);
    }
}

