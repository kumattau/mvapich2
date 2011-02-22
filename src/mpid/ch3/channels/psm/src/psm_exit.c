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

#include "psmpriv.h"

#undef FUNCNAME
#define FUNCNAME psm_dofinalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_dofinalize()
{
    int mpi_errno = MPI_ERR_INTERN;
    psm_error_t psmerr;

    if((psmerr = psm_mq_finalize(psmdev_cw.mq)) != PSM_OK) {
        PSM_ERR_ABORT("psm_mq_finalize failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }

    if((psmerr = psm_ep_close(psmdev_cw.ep, PSM_EP_CLOSE_GRACEFUL, 
                     5 * SEC_IN_NS)) != PSM_OK) {
        PSM_ERR_ABORT("psm_ep_close failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }

    if((psmerr = psm_finalize() != PSM_OK)) {
        PSM_ERR_ABORT("psm_finalize failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }
    MPIU_Free(psmdev_cw.epaddrs);

    psm_deallocate_vbuf();

    mpi_errno = MPI_SUCCESS;
fn_fail:
    return mpi_errno;
}
