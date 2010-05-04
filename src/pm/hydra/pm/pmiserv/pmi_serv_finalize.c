/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "pmci.h"
#include "pmi_handle.h"
#include "bsci.h"
#include "demux.h"
#include "pmi_serv.h"

HYD_status HYD_pmci_finalize(void)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYD_pmcd_pmi_finalize();
    HYDU_ERR_POP(status, "unable to finalize process manager utils\n");

    if (HYD_handle.user_global.launch_mode == HYD_LAUNCH_RUNTIME) {
        status = HYDT_bsci_finalize();
        HYDU_ERR_POP(status, "unable to finalize bootstrap server\n");
    }

    status = HYDT_dmx_finalize();
    HYDU_ERR_POP(status, "error returned from demux finalize\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
