/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_utils.h"
#include "bsci.h"
#include "bscu.h"

HYD_status HYDT_bsci_query_proxy_id(int *proxy_id)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDT_bsci_fns.query_proxy_id(proxy_id);
    HYDU_ERR_POP(status, "bootstrap device returned error while querying proxy ID\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
