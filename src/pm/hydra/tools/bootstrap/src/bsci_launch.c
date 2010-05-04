/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_utils.h"
#include "bsci.h"

HYD_status HYDT_bsci_launch_procs(char **global_args, const char *proxy_id_str,
                                  struct HYD_proxy *proxy_list)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDT_bsci_fns.launch_procs(global_args, proxy_id_str, proxy_list);
    HYDU_ERR_POP(status, "bootstrap device returned error while launching processes\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
