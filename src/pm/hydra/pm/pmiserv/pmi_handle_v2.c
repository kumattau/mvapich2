/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "bsci.h"
#include "demux.h"
#include "pmi_handle.h"

static HYD_status fn_fullinit(int fd, char *args[]);
static HYD_status fn_job_getid(int fd, char *args[]);
static HYD_status fn_info_putnodeattr(int fd, char *args[]);
static HYD_status fn_info_getnodeattr(int fd, char *args[]);
static HYD_status fn_info_getjobattr(int fd, char *args[]);
static HYD_status fn_kvs_put(int fd, char *args[]);
static HYD_status fn_kvs_get(int fd, char *args[]);
static HYD_status fn_kvs_fence(int fd, char *args[]);
static HYD_status fn_finalize(int fd, char *args[]);

struct token {
    char *key;
    char *val;
};

static struct reqs {
    enum type {
        NODE_ATTR_GET,
        KVS_GET
    } type;

    int fd;
    char *thrid;
    char **args;

    struct reqs *next;
} *pending_reqs = NULL;

static HYD_status send_command(int fd, char *cmd)
{
    char cmdlen[7];
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_snprintf(cmdlen, 7, "%6u", (unsigned) strlen(cmd));
    status = HYDU_sock_write(fd, cmdlen, 6);
    HYDU_ERR_POP(status, "error writing PMI line\n");

    status = HYDU_sock_write(fd, cmd, strlen(cmd));
    HYDU_ERR_POP(status, "error writing PMI line\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status args_to_tokens(char *args[], struct token **tokens, int *count)
{
    int i;
    char *arg;
    HYD_status status = HYD_SUCCESS;

    for (i = 0; args[i]; i++);
    *count = i;
    HYDU_MALLOC(*tokens, struct token *, *count * sizeof(struct token), status);

    for (i = 0; args[i]; i++) {
        arg = HYDU_strdup(args[i]);
        (*tokens)[i].key = strtok(arg, "=");
        (*tokens)[i].val = strtok(NULL, "=");
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static char *find_token_keyval(struct token *tokens, int count, const char *key)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!strcmp(tokens[i].key, key))
            return tokens[i].val;
    }

    return NULL;
}

static int req_complete = 0;

static void free_req(struct reqs *req)
{
    HYDU_free_strlist(req->args);
    HYDU_FREE(req);
}

static HYD_status queue_req(int fd, enum type type, char *args[])
{
    struct reqs *req, *tmp;
    HYD_status status = HYD_SUCCESS;

    HYDU_MALLOC(req, struct reqs *, sizeof(struct reqs), status);
    req->fd = fd;
    req->type = type;
    req->next = NULL;

    status = HYDU_strdup_list(args, &req->args);
    HYDU_ERR_POP(status, "unable to dup args\n");

    if (pending_reqs == NULL)
        pending_reqs = req;
    else {
        for (tmp = pending_reqs; tmp->next; tmp = tmp->next);
        tmp->next = req;
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static struct reqs *dequeue_req_head(void)
{
    struct reqs *req = pending_reqs;

    if (pending_reqs) {
        pending_reqs = pending_reqs->next;
        req->next = NULL;
    }

    return req;
}

static HYD_status poke_progress(void)
{
    struct reqs *req;
    int i, count;
    HYD_status status = HYD_SUCCESS;

    for (count = 0, req = pending_reqs; req; req = req->next)
        count++;

    for (i = 0; i < count; i++) {
        req = dequeue_req_head();

        req_complete = 0;
        if (req->type == NODE_ATTR_GET) {
            status = fn_info_getnodeattr(req->fd, req->args);
            HYDU_ERR_POP(status, "getnodeattr returned error\n");
        }
        else if (req->type == KVS_GET) {
            status = fn_kvs_get(req->fd, req->args);
            HYDU_ERR_POP(status, "kvs_get returned error\n");
        }

        free_req(req);
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static void print_req_list(void) ATTRIBUTE((unused));
static void print_req_list(void)
{
    struct reqs *req;

    if (pending_reqs)
        HYDU_dump_noprefix(stdout, "(");
    for (req = pending_reqs; req; req = req->next)
        HYDU_dump_noprefix(stdout, "%s ",
                           (req->type == NODE_ATTR_GET) ? "NODE_ATTR_GET" : "KVS_GET");
    if (pending_reqs)
        HYDU_dump_noprefix(stdout, ")\n");
}

static HYD_status fn_fullinit(int fd, char *args[])
{
    int id, rank, i;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *rank_str;
    HYD_pmcd_pmi_pg_t *run;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    rank_str = find_token_keyval(tokens, token_count, "pmirank");
    HYDU_ERR_CHKANDJUMP(status, rank_str == NULL, HYD_INTERNAL_ERROR,
                        "unable to find pmirank token\n");
    id = atoi(rank_str);

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=fullinit-response;pmi-version=2;pmi-subversion=0;rank=");

    status = HYD_pmcd_pmi_id_to_rank(id, &rank);
    HYDU_ERR_POP(status, "unable to convert ID to rank\n");
    tmp[i++] = HYDU_int_to_str(rank);

    tmp[i++] = HYDU_strdup(";size=");
    tmp[i++] = HYDU_int_to_str(HYD_pg_list->num_procs);
    tmp[i++] = HYDU_strdup(";appnum=0;debugged=FALSE;pmiverbose=0;rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "error while joining strings\n");

    for (i = 0; tmp[i]; i++)
        HYDU_FREE(tmp[i]);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

    run = HYD_pg_list;
    while (run->next)
        run = run->next;

    /* Add the process to the last PG */
    status = HYD_pmcd_pmi_add_process_to_pg(run, fd, rank);
    HYDU_ERR_POP(status, "unable to add process to pg\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_job_getid(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *thrid;
    int i;
    HYD_pmcd_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=job-getid-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("jobid=");
    tmp[i++] = HYDU_strdup(process->node->pg->kvs->kvs_name);
    tmp[i++] = HYDU_strdup(";rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_info_putnodeattr(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    char *key, *val, *thrid;
    int i, ret;
    HYD_pmcd_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR, "unable to find key token\n");

    val = find_token_keyval(tokens, token_count, "value");
    HYDU_ERR_CHKANDJUMP(status, val == NULL, HYD_INTERNAL_ERROR,
                        "unable to find value token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    status = HYD_pmcd_pmi_add_kvs(key, val, process->node->kvs, &ret);
    HYDU_ERR_POP(status, "unable to put data into kvs\n");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=info-putnodeattr-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=");
    tmp[i++] = HYDU_int_to_str(ret);
    tmp[i++] = HYDU_strdup(";");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

    /* Poke the progress engine before exiting */
    status = poke_progress();
    HYDU_ERR_POP(status, "poke progress error\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_info_getnodeattr(int fd, char *args[])
{
    int i, found;
    HYD_pmcd_pmi_process_t *process;
    HYD_pmcd_pmi_kvs_pair_t *run;
    char *key, *waitval, *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS] = { 0 }, *cmd;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR, "unable to find key token\n");

    waitval = find_token_keyval(tokens, token_count, "wait");
    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    found = 0;
    for (run = process->node->kvs->key_pair; run; run = run->next) {
        if (!strcmp(run->key, key)) {
            found = 1;
            break;
        }
    }

    if (found) {        /* We found the attribute */
        i = 0;
        tmp[i++] = HYDU_strdup("cmd=info-getnodeattr-response;");
        if (thrid) {
            tmp[i++] = HYDU_strdup("thrid=");
            tmp[i++] = HYDU_strdup(thrid);
            tmp[i++] = HYDU_strdup(";");
        }
        tmp[i++] = HYDU_strdup("found=TRUE;value=");
        tmp[i++] = HYDU_strdup(run->val);
        tmp[i++] = HYDU_strdup(";rc=0;");
        tmp[i++] = NULL;

        status = HYDU_str_alloc_and_join(tmp, &cmd);
        HYDU_ERR_POP(status, "unable to join strings\n");
        HYDU_free_strlist(tmp);

        status = send_command(fd, cmd);
        HYDU_ERR_POP(status, "send command failed\n");
        HYDU_FREE(cmd);
    }
    else if (waitval && !strcmp(waitval, "TRUE")) {
        /* The client wants to wait for a response; queue up the request */
        status = queue_req(fd, NODE_ATTR_GET, args);
        HYDU_ERR_POP(status, "unable to queue request\n");

        goto fn_exit;
    }
    else {
        /* Tell the client that we can't find the attribute */
        i = 0;
        tmp[i++] = HYDU_strdup("cmd=info-getnodeattr-response;");
        if (thrid) {
            tmp[i++] = HYDU_strdup("thrid=");
            tmp[i++] = HYDU_strdup(thrid);
            tmp[i++] = HYDU_strdup(";");
        }
        tmp[i++] = HYDU_strdup("found=FALSE;rc=0;");
        tmp[i++] = NULL;

        status = HYDU_str_alloc_and_join(tmp, &cmd);
        HYDU_ERR_POP(status, "unable to join strings\n");
        HYDU_free_strlist(tmp);

        status = send_command(fd, cmd);
        HYDU_ERR_POP(status, "send command failed\n");
        HYDU_FREE(cmd);
    }

    /* Mark the global completion variable, in case the progress
     * engine is monitoring. */
    /* FIXME: This should be an output parameter. We need to change
     * the structure of the PMI function table to be able to take
     * additional arguments, and not just the ones passed on the
     * wire. */
    req_complete = 1;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_info_getjobattr(int fd, char *args[])
{
    int i, ret;
    HYD_pmcd_pmi_process_t *process;
    HYD_pmcd_pmi_kvs_pair_t *run;
    const char *key;
    char *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *node_list;
    struct token *tokens;
    int token_count, found;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR, "unable to find key token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    /* Try to find the key */
    found = 0;
    for (run = process->node->pg->kvs->key_pair; run; run = run->next) {
        if (!strcmp(run->key, key)) {
            found = 1;
            break;
        }
    }

    if (found == 0) {
        /* Didn't find the job attribute; see if we know how to
         * generate it */
        if (strcmp(key, "PMI_process_mapping") == 0) {
            /* Create a vector format */
            status = HYD_pmcd_pmi_process_mapping(process, HYD_pmcd_pmi_vector, &node_list);
            HYDU_ERR_POP(status, "Unable to get process mapping information\n");

            if (strlen(node_list) > MAXVALLEN)
                HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR,
                                    "key value larger than maximum allowed\n");

            status = HYD_pmcd_pmi_add_kvs("PMI_process_mapping", node_list,
                                          process->node->pg->kvs, &ret);
            HYDU_ERR_POP(status, "unable to add process_mapping to KVS\n");
        }

        /* Search for the key again */
        for (run = process->node->pg->kvs->key_pair; run; run = run->next) {
            if (!strcmp(run->key, key)) {
                found = 1;
                break;
            }
        }
    }

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=info-getjobattr-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("found=");
    if (found) {
        tmp[i++] = HYDU_strdup("TRUE;value=");
        tmp[i++] = HYDU_strdup(run->val);
        tmp[i++] = HYDU_strdup(";rc=0;");
    }
    else {
        tmp[i++] = HYDU_strdup("FALSE;rc=0;");
    }
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_kvs_put(int fd, char *args[])
{
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    char *key, *val, *thrid;
    int i, ret;
    HYD_pmcd_pmi_process_t *process;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR, "unable to find key token\n");

    val = find_token_keyval(tokens, token_count, "value");
    HYDU_ERR_CHKANDJUMP(status, val == NULL, HYD_INTERNAL_ERROR,
                        "unable to find value token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    status = HYD_pmcd_pmi_add_kvs(key, val, process->node->pg->kvs, &ret);
    HYDU_ERR_POP(status, "unable to put data into kvs\n");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=kvs-put-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=");
    tmp[i++] = HYDU_int_to_str(ret);
    tmp[i++] = HYDU_strdup(";");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");

    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);

    /* Poke the progress engine before exiting */
    status = poke_progress();
    HYDU_ERR_POP(status, "poke progress error\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_kvs_get(int fd, char *args[])
{
    int i, found, barrier, process_count;
    HYD_pmcd_pmi_process_t *process, *prun;
    HYD_pmcd_pmi_node_t *node;
    HYD_pmcd_pmi_kvs_pair_t *run;
    char *key, *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    struct token *tokens;
    int token_count;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    key = find_token_keyval(tokens, token_count, "key");
    HYDU_ERR_CHKANDJUMP(status, key == NULL, HYD_INTERNAL_ERROR, "unable to find key token\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    found = 0;
    for (run = process->node->pg->kvs->key_pair; run; run = run->next) {
        if (!strcmp(run->key, key)) {
            found = 1;
            break;
        }
    }

    if (!found) {
        barrier = 1;
        process_count = 0;
        for (node = process->node->pg->node_list; node; node = node->next) {
            for (prun = node->process_list; prun; prun = prun->next) {
                process_count++;
                if (prun->epoch < process->epoch) {
                    barrier = 0;
                    break;
                }
            }
            if (!barrier)
                break;
        }

        if (!barrier || process_count < HYD_pg_list->num_procs) {
            /* We haven't reached a barrier yet; queue up request */
            status = queue_req(fd, KVS_GET, args);
            HYDU_ERR_POP(status, "unable to queue request\n");

            /* We are done */
            goto fn_exit;
        }
    }

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=kvs-get-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    if (found) {
        tmp[i++] = HYDU_strdup("found=TRUE;value=");
        tmp[i++] = HYDU_strdup(run->val);
        tmp[i++] = HYDU_strdup(";");
    }
    else {
        tmp[i++] = HYDU_strdup("found=FALSE;");
    }
    tmp[i++] = HYDU_strdup("rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");
    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");

    HYDU_FREE(cmd);
    req_complete = 1;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_kvs_fence(int fd, char *args[])
{
    HYD_pmcd_pmi_process_t *process;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd, *thrid;
    struct token *tokens;
    int token_count, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    /* Find the group id corresponding to this fd */
    process = HYD_pmcd_pmi_find_process(fd);
    if (process == NULL)        /* We didn't find the process */
        HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                             "unable to find process structure for fd %d\n", fd);

    process->epoch++;   /* We have reached the next epoch */

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=kvs-fence-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");
    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");
    HYDU_FREE(cmd);

    /* Poke the progress engine before exiting */
    status = poke_progress();
    HYDU_ERR_POP(status, "poke progress error\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status fn_finalize(int fd, char *args[])
{
    char *thrid;
    char *tmp[HYD_NUM_TMP_STRINGS], *cmd;
    struct token *tokens;
    int token_count, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = args_to_tokens(args, &tokens, &token_count);
    HYDU_ERR_POP(status, "unable to convert args to tokens\n");

    thrid = find_token_keyval(tokens, token_count, "thrid");

    i = 0;
    tmp[i++] = HYDU_strdup("cmd=finalize-response;");
    if (thrid) {
        tmp[i++] = HYDU_strdup("thrid=");
        tmp[i++] = HYDU_strdup(thrid);
        tmp[i++] = HYDU_strdup(";");
    }
    tmp[i++] = HYDU_strdup("rc=0;");
    tmp[i++] = NULL;

    status = HYDU_str_alloc_and_join(tmp, &cmd);
    HYDU_ERR_POP(status, "unable to join strings\n");
    HYDU_free_strlist(tmp);

    status = send_command(fd, cmd);
    HYDU_ERR_POP(status, "send command failed\n");
    HYDU_FREE(cmd);

    if (status == HYD_SUCCESS) {
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to register fd\n");
        close(fd);
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

/* TODO: abort, create_kvs, destroy_kvs, getbyidx, spawn */
static struct HYD_pmcd_pmi_handle_fns pmi_v2_handle_fns_foo[] = {
    {"fullinit", fn_fullinit},
    {"job-getid", fn_job_getid},
    {"info-putnodeattr", fn_info_putnodeattr},
    {"info-getnodeattr", fn_info_getnodeattr},
    {"info-getjobattr", fn_info_getjobattr},
    {"kvs-put", fn_kvs_put},
    {"kvs-get", fn_kvs_get},
    {"kvs-fence", fn_kvs_fence},
    {"finalize", fn_finalize},
    {"\0", NULL}
};

struct HYD_pmcd_pmi_handle HYD_pmcd_pmi_v2 = { PMI_V2_DELIM, pmi_v2_handle_fns_foo };
