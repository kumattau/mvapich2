/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra_utils.h"
#include "bsci.h"
#include "bscu.h"
#include "slurm.h"

static void full_str_to_groups(char *str, char **list)
{
    char *tmp;
    char new[MAX_HOSTNAME_LEN];
    int nesting, i, arg;

    tmp = str;
    i = 0;
    nesting = 0;
    arg = 0;

    while (1) {
        new[i] = *tmp;
        if (*tmp == '[')
            nesting++;
        if (*tmp == ']')
            nesting--;
        if (*tmp == ',' && nesting == 0) {
            new[i] = 0;
            list[arg++] = HYDU_strdup(new);
            i = -1;
        }
        if (*tmp == 0) {
            new[++i] = 0;
            list[arg++] = HYDU_strdup(new);
            break;
        }

        i++;
        tmp++;
    }
    list[arg++] = NULL;
}

static HYD_status group_to_individual_nodes(char *str, char **list)
{
    char *pre = NULL, *nodes = NULL, *tmp;
    int start_node, end_node;
    char new[MAX_HOSTNAME_LEN], *node_str[MAX_HOSTNAME_LEN];
    int arg, i;
    HYD_status status = HYD_SUCCESS;

    tmp = str;
    i = 0;
    while (1) {
        new[i] = *tmp;

        if (*tmp == '[') {
            new[i] = 0;
            pre = HYDU_strdup(new);
            i = -1;
        }

        if (*tmp == ']' || *tmp == 0) {
            new[i] = 0;
            nodes = HYDU_strdup(new);
            break;
        }

        i++;
        tmp++;
    }

    arg = 0;
    if (pre == NULL) {
        list[arg++] = nodes;
    }
    else {
        start_node = atoi(strtok(nodes, "-"));
        end_node = atoi(strtok(NULL, "-"));
        for (i = start_node; i <= end_node; i++) {
            node_str[0] = HYDU_strdup(pre);
            node_str[1] = HYDU_int_to_str(i);
            node_str[2] = NULL;

            status = HYDU_str_alloc_and_join(node_str, &list[arg++]);
            HYDU_ERR_POP(status, "unable to join strings\n");

            HYDU_free_strlist(node_str);
        }
    }
    list[arg++] = NULL;

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYDT_bscd_slurm_query_node_list(int *num_cores, struct HYD_proxy **proxy_list)
{
    char *str, *num_procs;
    char *tmp1[HYD_NUM_TMP_STRINGS], *tmp2[HYD_NUM_TMP_STRINGS];
    int i, j, start_pid;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    str = getenv("SLURM_NODELIST");
    num_procs = getenv("SLURM_JOB_CPUS_PER_NODE");

    if (str == NULL || num_procs == NULL) {
        *proxy_list = NULL;
    }
    else {
        full_str_to_groups(str, tmp1);
        num_procs = strtok(num_procs, "(");

        start_pid = 0;
        for (i = 0; tmp1[i]; i++) {
            status = group_to_individual_nodes(tmp1[i], tmp2);
            HYDU_ERR_POP(status, "unable to parse node list\n");

            for (j = 0; tmp2[j]; j++) {
                status = HYDU_merge_proxy_segment(tmp2[j], start_pid, atoi(num_procs),
                                                  proxy_list);
                HYDU_ERR_POP(status, "merge proxy segment failed\n");

                start_pid += atoi(num_procs);
            }
        }
        *num_cores = start_pid;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
