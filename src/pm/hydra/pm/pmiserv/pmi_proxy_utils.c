/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "pmi_proxy.h"
#include "bsci.h"
#include "demux.h"
#include "bind.h"
#include "ckpoint.h"
#include "hydra_utils.h"

struct HYD_pmcd_pmip HYD_pmcd_pmip;

static HYD_status init_params(void)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_init_user_global(&HYD_pmcd_pmip.user_global);

    HYD_pmcd_pmip.system_global.global_core_count = 0;
    HYD_pmcd_pmip.system_global.pmi_port_str = NULL;

    HYD_pmcd_pmip.upstream.server_name = NULL;
    HYD_pmcd_pmip.upstream.server_port = -1;
    HYD_pmcd_pmip.upstream.out = -1;
    HYD_pmcd_pmip.upstream.err = -1;
    HYD_pmcd_pmip.upstream.in = -1;
    HYD_pmcd_pmip.upstream.control = -1;

    HYD_pmcd_pmip.downstream.out = NULL;
    HYD_pmcd_pmip.downstream.err = NULL;
    HYD_pmcd_pmip.downstream.in = -1;
    HYD_pmcd_pmip.downstream.pid = NULL;
    HYD_pmcd_pmip.downstream.exit_status = NULL;

    HYD_pmcd_pmip.local.id = -1;
    HYD_pmcd_pmip.local.hostname = NULL;
    HYD_pmcd_pmip.local.proxy_core_count = 0;
    HYD_pmcd_pmip.local.proxy_process_count = 0;
    HYD_pmcd_pmip.local.procs_are_launched = 0;
    HYD_pmcd_pmip.local.stdin_buf_offset = 0;
    HYD_pmcd_pmip.local.stdin_buf_count = 0;
    HYD_pmcd_pmip.local.stdin_tmp_buf[0] = '\0';

    HYD_pmcd_pmip.start_pid = -1;
    HYD_pmcd_pmip.exec_list = NULL;

    return status;
}

/* FIXME: This function performs minimal error checking as it is not
 * supposed to be called by the user, but rather by the process
 * management server. It will still be helpful for debugging to add
 * some error checks. */
static HYD_status parse_params(char **t_argv)
{
    char **argv = t_argv, *str, *argtype;
    int arg, i, count;
    HYD_env_t *env;
    struct HYD_proxy_exec *exec = NULL;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    for (; *argv; ++argv) {
        /* Working directory */
        if (!strcmp(*argv, "--wdir")) {
            argv++;
            HYD_pmcd_pmip.user_global.wdir = HYDU_strdup(*argv);
            continue;
        }

        /* PMI port string */
        if (!strcmp(*argv, "--pmi-port-str")) {
            argv++;
            if (!strcmp(*argv, "HYDRA_NULL"))
                HYD_pmcd_pmip.system_global.pmi_port_str = NULL;
            else
                HYD_pmcd_pmip.system_global.pmi_port_str = HYDU_strdup(*argv);
            continue;
        }

        /* Binding */
        if (!strcmp(*argv, "--binding")) {
            argv++;
            if (!strcmp(*argv, "HYDRA_NULL"))
                HYD_pmcd_pmip.user_global.binding = NULL;
            else
                HYD_pmcd_pmip.user_global.binding = HYDU_strdup(*argv);

            continue;
        }

        /* Binding library */
        if (!strcmp(*argv, "--bindlib")) {
            argv++;
            HYD_pmcd_pmip.user_global.bindlib = HYDU_strdup(*argv);
            continue;
        }

        /* Checkpointing library */
        if (!strcmp(*argv, "--ckpointlib")) {
            argv++;
            HYD_pmcd_pmip.user_global.ckpointlib = HYDU_strdup(*argv);
            continue;
        }

        if (!strcmp(*argv, "--ckpoint-prefix")) {
            argv++;
            if (!strcmp(*argv, "HYDRA_NULL"))
                HYD_pmcd_pmip.user_global.ckpoint_prefix = NULL;
            else
                HYD_pmcd_pmip.user_global.ckpoint_prefix = HYDU_strdup(*argv);
            continue;
        }

        if (!strcmp(*argv, "--ckpoint-restart")) {
            HYD_pmcd_pmip.user_global.ckpoint_restart = 1;
            continue;
        }

        /* Global env */
        if ((!strcmp(*argv, "--global-inherited-env")) ||
            (!strcmp(*argv, "--global-system-env")) || (!strcmp(*argv, "--global-user-env"))) {

            argtype = *argv;

            argv++;
            count = atoi(*argv);
            for (i = 0; i < count; i++) {
                argv++;
                str = *argv;

                /* Some bootstrap servers remove the quotes that we
                 * added, while some others do not. For the cases
                 * where they are not removed, we do it ourselves. */
                if (*str == '\'') {
                    str++;
                    str[strlen(str) - 1] = 0;
                }
                status = HYDU_str_to_env(str, &env);
                HYDU_ERR_POP(status, "error converting string to env\n");

                if (!strcmp(argtype, "--global-inherited-env"))
                    HYDU_append_env_to_list(*env,
                                            &HYD_pmcd_pmip.user_global.global_env.inherited);
                else if (!strcmp(argtype, "--global-system-env"))
                    HYDU_append_env_to_list(*env,
                                            &HYD_pmcd_pmip.user_global.global_env.system);
                else if (!strcmp(argtype, "--global-user-env"))
                    HYDU_append_env_to_list(*env, &HYD_pmcd_pmip.user_global.global_env.user);

                HYDU_FREE(env);
            }
            continue;
        }

        /* Global environment type */
        if (!strcmp(*argv, "--genv-prop")) {
            argv++;
            if (strcmp(*argv, "HYDRA_NULL"))
                HYD_pmcd_pmip.user_global.global_env.prop = HYDU_strdup(*argv);
            else
                HYD_pmcd_pmip.user_global.global_env.prop = NULL;
            continue;
        }

        /* One-pass Count */
        if (!strcmp(*argv, "--global-core-count")) {
            argv++;
            HYD_pmcd_pmip.system_global.global_core_count = atoi(*argv);
            continue;
        }

        /* Version string comparison */
        if (!strcmp(*argv, "--version")) {
            argv++;
            if (strcmp(*argv, HYDRA_VERSION)) {
                HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR,
                                    "UI version string does not match proxy version\n");
            }
            continue;
        }

        /* Hostname (as specified by the user) */
        if (!strcmp(*argv, "--hostname")) {
            argv++;
            HYD_pmcd_pmip.local.hostname = HYDU_strdup(*argv);
            continue;
        }

        /* Process count */
        if (!strcmp(*argv, "--proxy-core-count")) {
            argv++;
            HYD_pmcd_pmip.local.proxy_core_count = atoi(*argv);
            continue;
        }

        /* Process count */
        if (!strcmp(*argv, "--start-pid")) {
            argv++;
            HYD_pmcd_pmip.start_pid = atoi(*argv);
            continue;
        }

        /* New executable */
        if (!strcmp(*argv, "--exec")) {
            if (HYD_pmcd_pmip.exec_list == NULL) {
                status = HYDU_alloc_proxy_exec(&HYD_pmcd_pmip.exec_list);
                HYDU_ERR_POP(status, "unable to allocate proxy exec\n");
            }
            else {
                for (exec = HYD_pmcd_pmip.exec_list; exec->next; exec = exec->next);
                status = HYDU_alloc_proxy_exec(&exec->next);
                HYDU_ERR_POP(status, "unable to allocate proxy exec\n");
            }
            continue;
        }

        /* Process count */
        if (!strcmp(*argv, "--exec-proc-count")) {
            argv++;
            for (exec = HYD_pmcd_pmip.exec_list; exec->next; exec = exec->next);
            exec->proc_count = atoi(*argv);
            continue;
        }

        /* Local env */
        if (!strcmp(*argv, "--exec-local-env")) {
            argv++;
            count = atoi(*argv);
            for (i = 0; i < count; i++) {
                argv++;
                str = *argv;

                /* Some bootstrap servers remove the quotes that we
                 * added, while some others do not. For the cases
                 * where they are not removed, we do it ourselves. */
                if (*str == '\'') {
                    str++;
                    str[strlen(str) - 1] = 0;
                }
                status = HYDU_str_to_env(str, &env);
                HYDU_ERR_POP(status, "error converting string to env\n");
                HYDU_append_env_to_list(*env, &exec->user_env);
                HYDU_FREE(env);
            }
            continue;
        }

        /* Global environment type */
        if (!strcmp(*argv, "--exec-env-prop")) {
            argv++;
            if (strcmp(*argv, "HYDRA_NULL"))
                exec->env_prop = HYDU_strdup(*argv);
            else
                exec->env_prop = NULL;
            continue;
        }

        /* Fall through case is application parameters. Load
         * everything into the args variable. */
        for (exec = HYD_pmcd_pmip.exec_list; exec->next; exec = exec->next);
        for (arg = 0; *argv && strcmp(*argv, "--exec");) {
            exec->exec[arg++] = HYDU_strdup(*argv);
            ++argv;
        }
        exec->exec[arg++] = NULL;

        /* If we already touched the next --exec, step back */
        if (*argv && !strcmp(*argv, "--exec"))
            argv--;

        if (!(*argv))
            break;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_get_params(char **t_argv)
{
    char **argv = t_argv;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = init_params();
    HYDU_ERR_POP(status, "Error initializing proxy params\n");

    while (++argv && *argv) {
        if (!strcmp(*argv, "--launch-mode")) {
            ++argv;
            HYD_pmcd_pmip.user_global.launch_mode =
                (HYD_launch_mode_t) (unsigned int) atoi(*argv);
            continue;
        }
        if (!strcmp(*argv, "--proxy-port")) {
            ++argv;
            if (HYD_pmcd_pmip.user_global.launch_mode == HYD_LAUNCH_RUNTIME) {
                HYD_pmcd_pmip.upstream.server_name = HYDU_strdup(strtok(*argv, ":"));
                HYD_pmcd_pmip.upstream.server_port = atoi(strtok(NULL, ":"));
            }
            else {
                HYD_pmcd_pmip.upstream.server_port = atoi(*argv);
            }
            continue;
        }
        if (!strcmp(*argv, "--proxy-id")) {
            ++argv;
            HYD_pmcd_pmip.local.id = atoi(*argv);
            continue;
        }
        if (!strcmp(*argv, "--debug")) {
            HYD_pmcd_pmip.user_global.debug = 1;
            continue;
        }
        if (!strcmp(*argv, "--enable-x")) {
            HYD_pmcd_pmip.user_global.enablex = 1;
            continue;
        }
        if (!strcmp(*argv, "--disable-x")) {
            HYD_pmcd_pmip.user_global.enablex = 0;
            continue;
        }
        if (!strcmp(*argv, "--bootstrap")) {
            ++argv;
            HYD_pmcd_pmip.user_global.bootstrap = HYDU_strdup(*argv);
            continue;
        }
        if (!strcmp(*argv, "--bootstrap-exec")) {
            ++argv;
            HYD_pmcd_pmip.user_global.bootstrap_exec = HYDU_strdup(*argv);
            continue;
        }
    }

    status = HYDT_bsci_init(HYD_pmcd_pmip.user_global.bootstrap,
                            HYD_pmcd_pmip.user_global.bootstrap_exec,
                            HYD_pmcd_pmip.user_global.enablex,
                            HYD_pmcd_pmip.user_global.debug);
    HYDU_ERR_POP(status, "proxy unable to initialize bootstrap\n");

    if (HYD_pmcd_pmip.local.id == -1) {
        /* We didn't get a proxy ID during launch; query the
         * bootstrap server for it. */
        status = HYDT_bsci_query_proxy_id(&HYD_pmcd_pmip.local.id);
        HYDU_ERR_POP(status, "unable to query bootstrap server for proxy ID\n");
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_cleanup_params(void)
{
    struct HYD_proxy_exec *exec, *texec;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (HYD_pmcd_pmip.upstream.server_name)
        HYDU_FREE(HYD_pmcd_pmip.upstream.server_name);

    if (HYD_pmcd_pmip.user_global.bootstrap)
        HYDU_FREE(HYD_pmcd_pmip.user_global.bootstrap);

    if (HYD_pmcd_pmip.user_global.bootstrap_exec)
        HYDU_FREE(HYD_pmcd_pmip.user_global.bootstrap_exec);

    if (HYD_pmcd_pmip.user_global.wdir)
        HYDU_FREE(HYD_pmcd_pmip.user_global.wdir);

    if (HYD_pmcd_pmip.system_global.pmi_port_str)
        HYDU_FREE(HYD_pmcd_pmip.system_global.pmi_port_str);

    if (HYD_pmcd_pmip.user_global.binding)
        HYDU_FREE(HYD_pmcd_pmip.user_global.binding);

    if (HYD_pmcd_pmip.user_global.bindlib)
        HYDU_FREE(HYD_pmcd_pmip.user_global.bindlib);

    if (HYD_pmcd_pmip.user_global.ckpointlib)
        HYDU_FREE(HYD_pmcd_pmip.user_global.ckpointlib);

    if (HYD_pmcd_pmip.user_global.ckpoint_prefix)
        HYDU_FREE(HYD_pmcd_pmip.user_global.ckpoint_prefix);

    if (HYD_pmcd_pmip.user_global.global_env.system)
        HYDU_env_free_list(HYD_pmcd_pmip.user_global.global_env.system);

    if (HYD_pmcd_pmip.user_global.global_env.user)
        HYDU_env_free_list(HYD_pmcd_pmip.user_global.global_env.user);

    if (HYD_pmcd_pmip.user_global.global_env.inherited)
        HYDU_env_free_list(HYD_pmcd_pmip.user_global.global_env.inherited);

    if (HYD_pmcd_pmip.exec_list) {
        exec = HYD_pmcd_pmip.exec_list;
        while (exec) {
            texec = exec->next;
            HYDU_free_strlist(exec->exec);
            if (exec->user_env)
                HYDU_env_free(exec->user_env);
            HYDU_FREE(exec);
            exec = texec;
        }
    }

    if (HYD_pmcd_pmip.downstream.pid)
        HYDU_FREE(HYD_pmcd_pmip.downstream.pid);

    if (HYD_pmcd_pmip.downstream.out)
        HYDU_FREE(HYD_pmcd_pmip.downstream.out);

    if (HYD_pmcd_pmip.downstream.err)
        HYDU_FREE(HYD_pmcd_pmip.downstream.err);

    if (HYD_pmcd_pmip.downstream.exit_status)
        HYDU_FREE(HYD_pmcd_pmip.downstream.exit_status);

    if (HYD_pmcd_pmip.local.hostname)
        HYDU_FREE(HYD_pmcd_pmip.local.hostname);

    HYDT_bind_finalize();

    /* Reinitialize all params to set everything to "NULL" or
     * equivalent. */
    status = init_params();
    HYDU_ERR_POP(status, "unable to initialize params\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_procinfo(int fd)
{
    char **arglist;
    int num_strings, str_len, recvd, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* Read information about the application to launch into a string
     * array and call parse_params() to interpret it and load it into
     * the proxy handle. */
    status = HYDU_sock_read(fd, &num_strings, sizeof(int), &recvd, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "error reading data from upstream\n");

    HYDU_MALLOC(arglist, char **, (num_strings + 1) * sizeof(char *), status);

    for (i = 0; i < num_strings; i++) {
        status = HYDU_sock_read(fd, &str_len, sizeof(int), &recvd, HYDU_SOCK_COMM_MSGWAIT);
        HYDU_ERR_POP(status, "error reading data from upstream\n");

        HYDU_MALLOC(arglist[i], char *, str_len, status);

        status = HYDU_sock_read(fd, arglist[i], str_len, &recvd, HYDU_SOCK_COMM_MSGWAIT);
        HYDU_ERR_POP(status, "error reading data from upstream\n");
    }
    arglist[num_strings] = NULL;

    /* Get the parser to fill in the proxy params structure. */
    status = parse_params(arglist);
    HYDU_ERR_POP(status, "unable to parse argument list\n");

    HYDU_free_strlist(arglist);
    HYDU_FREE(arglist);

    /* Save this fd as we need to send back the exit status on
     * this. */
    HYD_pmcd_pmip.upstream.control = fd;

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_launch_procs(void)
{
    int i, j, arg, stdin_fd, process_id, os_index, pmi_id;
    char *str, *envstr, *list;
    char *client_args[HYD_NUM_TMP_STRINGS];
    HYD_env_t *env, *prop_env = NULL;
    struct HYD_proxy_exec *exec;
    HYD_status status = HYD_SUCCESS;
    int *pmi_ids;

    HYDU_FUNC_ENTER();

    HYD_pmcd_pmip.local.proxy_process_count = 0;
    for (exec = HYD_pmcd_pmip.exec_list; exec; exec = exec->next)
        HYD_pmcd_pmip.local.proxy_process_count += exec->proc_count;

    HYDU_MALLOC(pmi_ids, int *, HYD_pmcd_pmip.local.proxy_process_count * sizeof(int), status);
    for (i = 0; i < HYD_pmcd_pmip.local.proxy_process_count; i++) {
        pmi_ids[i] =
            HYDU_local_to_global_id(i, HYD_pmcd_pmip.start_pid,
                                    HYD_pmcd_pmip.local.proxy_core_count,
                                    HYD_pmcd_pmip.system_global.global_core_count);
    }

    HYDU_MALLOC(HYD_pmcd_pmip.downstream.out, int *,
                HYD_pmcd_pmip.local.proxy_process_count * sizeof(int), status);
    HYDU_MALLOC(HYD_pmcd_pmip.downstream.err, int *,
                HYD_pmcd_pmip.local.proxy_process_count * sizeof(int), status);
    HYDU_MALLOC(HYD_pmcd_pmip.downstream.pid, int *,
                HYD_pmcd_pmip.local.proxy_process_count * sizeof(int), status);
    HYDU_MALLOC(HYD_pmcd_pmip.downstream.exit_status, int *,
                HYD_pmcd_pmip.local.proxy_process_count * sizeof(int), status);

    /* Initialize the exit status */
    for (i = 0; i < HYD_pmcd_pmip.local.proxy_process_count; i++)
        HYD_pmcd_pmip.downstream.exit_status[i] = -1;

    status = HYDT_bind_init(HYD_pmcd_pmip.user_global.binding,
                            HYD_pmcd_pmip.user_global.bindlib);
    HYDU_ERR_POP(status, "unable to initialize process binding\n");

    status = HYDT_ckpoint_init(HYD_pmcd_pmip.user_global.ckpointlib,
                               HYD_pmcd_pmip.user_global.ckpoint_prefix);
    HYDU_ERR_POP(status, "unable to initialize checkpointing\n");

    if (HYD_pmcd_pmip.user_global.ckpoint_restart) {
        status = HYDU_env_create(&env, "PMI_PORT", HYD_pmcd_pmip.system_global.pmi_port_str);
        HYDU_ERR_POP(status, "unable to create env\n");

        /* Restart the proxy.  Specify stdin fd only if pmi_id 0 is in this proxy. */
        status = HYDT_ckpoint_restart(env, HYD_pmcd_pmip.local.proxy_process_count,
                                      pmi_ids,
                                      pmi_ids[0] ? NULL :
                                      &HYD_pmcd_pmip.downstream.in,
                                      HYD_pmcd_pmip.downstream.out,
                                      HYD_pmcd_pmip.downstream.err);
        HYDU_ERR_POP(status, "checkpoint restart failure\n");
        goto fn_spawn_complete;
    }

    /* Spawn the processes */
    process_id = 0;
    for (exec = HYD_pmcd_pmip.exec_list; exec; exec = exec->next) {

        /*
         * Increasing priority order:
         *    - Global inherited env
         *    - Global user env
         *    - Local user env
         *    - System env
         */

        /* Global inherited env */
        if ((exec->env_prop && !strcmp(exec->env_prop, "all")) ||
            (!exec->env_prop && !strcmp(HYD_pmcd_pmip.user_global.global_env.prop, "all"))) {
            for (env = HYD_pmcd_pmip.user_global.global_env.inherited; env; env = env->next) {
                status = HYDU_append_env_to_list(*env, &prop_env);
                HYDU_ERR_POP(status, "unable to add env to list\n");
            }
        }
        else if ((exec->env_prop && !strncmp(exec->env_prop, "list", strlen("list"))) ||
                 (!exec->env_prop &&
                  !strncmp(HYD_pmcd_pmip.user_global.global_env.prop, "list",
                           strlen("list")))) {
            if (exec->env_prop)
                list = HYDU_strdup(exec->env_prop + strlen("list:"));
            else
                list = HYDU_strdup(HYD_pmcd_pmip.user_global.global_env.prop +
                                   strlen("list:"));

            envstr = strtok(list, ",");
            while (envstr) {
                env = HYDU_env_lookup(envstr, HYD_pmcd_pmip.user_global.global_env.inherited);
                if (env) {
                    status = HYDU_append_env_to_list(*env, &prop_env);
                    HYDU_ERR_POP(status, "unable to add env to list\n");
                }
                envstr = strtok(NULL, ",");
            }
        }

        /* Next priority order is the global user env */
        for (env = HYD_pmcd_pmip.user_global.global_env.user; env; env = env->next) {
            status = HYDU_append_env_to_list(*env, &prop_env);
            HYDU_ERR_POP(status, "unable to add env to list\n");
        }

        /* Next priority order is the local user env */
        for (env = exec->user_env; env; env = env->next) {
            status = HYDU_append_env_to_list(*env, &prop_env);
            HYDU_ERR_POP(status, "unable to add env to list\n");
        }

        /* Highest priority is the system env */
        for (env = HYD_pmcd_pmip.user_global.global_env.system; env; env = env->next) {
            status = HYDU_append_env_to_list(*env, &prop_env);
            HYDU_ERR_POP(status, "unable to add env to list\n");
        }

        /* Set the PMI port string to connect to. We currently just
         * use the global PMI port. */
        if (HYD_pmcd_pmip.system_global.pmi_port_str) {
            status = HYDU_env_create(&env, "PMI_PORT",
                                     HYD_pmcd_pmip.system_global.pmi_port_str);
            HYDU_ERR_POP(status, "unable to create env\n");

            status = HYDU_append_env_to_list(*env, &prop_env);
            HYDU_ERR_POP(status, "unable to add env to list\n");
        }

        /* Set the MPICH_INTERFACE_HOSTNAME based on what the user provided */
        if (HYD_pmcd_pmip.local.hostname) {
            status = HYDU_env_create(&env, "MPICH_INTERFACE_HOSTNAME",
                                     HYD_pmcd_pmip.local.hostname);
            HYDU_ERR_POP(status, "unable to create env\n");

            status = HYDU_append_env_to_list(*env, &prop_env);
            HYDU_ERR_POP(status, "unable to add env to list\n");
        }

        for (i = 0; i < exec->proc_count; i++) {
            pmi_id = HYDU_local_to_global_id(process_id,
                                             HYD_pmcd_pmip.start_pid,
                                             HYD_pmcd_pmip.local.proxy_core_count,
                                             HYD_pmcd_pmip.system_global.global_core_count);

            if (HYD_pmcd_pmip.system_global.pmi_port_str) {
                str = HYDU_int_to_str(pmi_id);
                status = HYDU_env_create(&env, "PMI_ID", str);
                HYDU_ERR_POP(status, "unable to create env\n");
                HYDU_FREE(str);
                status = HYDU_append_env_to_list(*env, &prop_env);
                HYDU_ERR_POP(status, "unable to add env to list\n");
            }

            if (chdir(HYD_pmcd_pmip.user_global.wdir) < 0)
                HYDU_ERR_SETANDJUMP1(status, HYD_INTERNAL_ERROR,
                                     "unable to change wdir (%s)\n", HYDU_strerror(errno));

            for (j = 0, arg = 0; exec->exec[j]; j++)
                client_args[arg++] = HYDU_strdup(exec->exec[j]);
            client_args[arg++] = NULL;

            os_index = HYDT_bind_get_os_index(process_id);
            if (pmi_id == 0) {
                status = HYDU_create_process(client_args, prop_env,
                                             &HYD_pmcd_pmip.downstream.in,
                                             &HYD_pmcd_pmip.downstream.out[process_id],
                                             &HYD_pmcd_pmip.downstream.err[process_id],
                                             &HYD_pmcd_pmip.downstream.pid[process_id],
                                             os_index);

                HYD_pmcd_pmip.local.stdin_buf_offset = 0;
                HYD_pmcd_pmip.local.stdin_buf_count = 0;

                status = HYDU_sock_set_nonblock(HYD_pmcd_pmip.upstream.in);
                HYDU_ERR_POP(status, "unable to set upstream stdin fd to nonblocking\n");

                stdin_fd = HYD_pmcd_pmip.downstream.in;
                status = HYDT_dmx_register_fd(1, &stdin_fd, HYD_STDIN, NULL,
                                              HYD_pmcd_pmi_proxy_stdin_cb);
                HYDU_ERR_POP(status, "unable to register fd\n");
            }
            else {
                status = HYDU_create_process(client_args, prop_env, NULL,
                                             &HYD_pmcd_pmip.downstream.out[process_id],
                                             &HYD_pmcd_pmip.downstream.err[process_id],
                                             &HYD_pmcd_pmip.downstream.pid[process_id],
                                             os_index);
            }
            HYDU_ERR_POP(status, "create process returned error\n");

            process_id++;
        }

        HYDU_env_free_list(prop_env);
        prop_env = NULL;
    }

  fn_spawn_complete:
    /* Everything is spawned, register the required FDs  */
    status = HYDT_dmx_register_fd(HYD_pmcd_pmip.local.proxy_process_count,
                                  HYD_pmcd_pmip.downstream.out,
                                  HYD_STDOUT, NULL, HYD_pmcd_pmi_proxy_stdout_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

    status = HYDT_dmx_register_fd(HYD_pmcd_pmip.local.proxy_process_count,
                                  HYD_pmcd_pmip.downstream.err,
                                  HYD_STDOUT, NULL, HYD_pmcd_pmi_proxy_stderr_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

    /* Indicate that the processes have been launched */
    HYD_pmcd_pmip.local.procs_are_launched = 1;

  fn_exit:
    if (pmi_ids)
        HYDU_FREE(pmi_ids);
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


void HYD_pmcd_pmi_proxy_killjob(void)
{
    int i;

    HYDU_FUNC_ENTER();

    /* Send the kill signal to all processes */
    for (i = 0; i < HYD_pmcd_pmip.local.proxy_process_count; i++) {
        if (HYD_pmcd_pmip.downstream.pid[i] != -1) {
            kill(HYD_pmcd_pmip.downstream.pid[i], SIGTERM);
            kill(HYD_pmcd_pmip.downstream.pid[i], SIGKILL);
        }
    }

    HYDU_FUNC_EXIT();
}
