/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_base.h"
#include "rmki.h"
#include "rmk_pbs.h"

static struct HYD_node *global_node_list = NULL;

static HYD_status process_mfile_token(char *token, int newline)
{
    int num_procs;
    char *hostname, *procs;
    HYD_status status = HYD_SUCCESS;

    if (newline) {      /* The first entry gives the hostname and processes */
        hostname = strtok(token, ":");
        procs = strtok(NULL, ":");
        num_procs = procs ? atoi(procs) : 1;

        status = HYDU_add_to_node_list(hostname, num_procs, &global_node_list);
        HYDU_ERR_POP(status, "unable to initialize proxy\n");
    }
    else {      /* Not a new line */
        HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR,
                            "token %s not supported at this time\n", token);
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYDT_rmkd_pbs_query_node_list(struct HYD_node **node_list)
{
    char *hostfile;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (MPL_env2str("PBS_NODEFILE", (const char **) &hostfile) == 0)
        hostfile = NULL;

    if (hostfile == NULL) {
        *node_list = NULL;
        HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "No PBS nodefile found\n");
    }
    else {
        status = HYDU_parse_hostfile(hostfile, process_mfile_token);
        HYDU_ERR_POP(status, "error parsing hostfile\n");
    }
    *node_list = global_node_list;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
