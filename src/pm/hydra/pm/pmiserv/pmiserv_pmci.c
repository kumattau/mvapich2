/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "pmci.h"
#include "pmiserv_pmi.h"
#include "bsci.h"
#include "bind.h"
#include "pmiserv.h"
#include "pmiserv_utils.h"

static HYD_status cleanup_procs(void)
{
    static int user_abort_signal = 0;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();


    /* Sent kill signals to the processes. Wait for all processes to
     * exit. If they do not exit, allow the application to just force
     * kill the spawned processes. */
    if (user_abort_signal == 0) {
        HYDU_dump_noprefix(stdout, "Ctrl-C caught... cleaning up processes\n");

        status = HYD_pmcd_pmiserv_cleanup();
        HYDU_ERR_POP(status, "cleanup of processes failed\n");

        HYDU_dump_noprefix(stdout, "[press Ctrl-C again to force abort]\n");

        user_abort_signal = 1;
    }
    else {
        HYDU_dump_noprefix(stdout, "Ctrl-C caught... forcing cleanup\n");

        /* Something has gone really wrong! Ask the bootstrap server
         * to forcefully cleanup the proxies, but this may leave some
         * of the application processes still running. */
        status = HYDT_bsci_cleanup_procs();
        HYDU_ERR_POP(status, "error cleaning up processes\n");
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status ckpoint(void)
{
    struct HYD_pg *pg = &HYD_handle.pg_list;
    struct HYD_proxy *proxy;
    enum HYD_pmcd_pmi_cmd cmd;
    int sent, closed;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (pg->next)
        HYDU_ERR_POP(status, "checkpointing is not supported for dynamic processes\n");

    /* Connect to all proxies and send the checkpoint command */
    for (proxy = pg->proxy_list; proxy; proxy = proxy->next) {
        cmd = CKPOINT;
        status = HYDU_sock_write(proxy->control_fd, &cmd, sizeof(enum HYD_pmcd_pmi_cmd),
                                 &sent, &closed);
        HYDU_ERR_POP(status, "unable to send checkpoint message\n");
        HYDU_ASSERT(!closed, status);
    }

    HYDU_FUNC_EXIT();

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status ui_cmd_cb(int fd, HYD_event_t events, void *userp)
{
    enum HYD_cmd cmd;
    int count, closed;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDU_sock_read(fd, &cmd, sizeof(cmd), &count, &closed, HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "read error\n");
    HYDU_ASSERT(!closed, status);

    if (cmd == HYD_CLEANUP) {
        status = cleanup_procs();
        HYDU_ERR_POP(status, "error cleaning up processes\n");
    }
    else if (cmd == HYD_CKPOINT) {
        status = ckpoint();
        HYDU_ERR_POP(status, "error checkpointing processes\n");
    }

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status outerr(void *buf, int buflen, char **storage, int *storage_len,
                         HYD_status(*cb) (void *buf, int buflen))
{
    struct HYD_pmcd_stdio_hdr *hdr;
    char *rbuf, *tbuf, *tmp, str[HYD_TMPBUF_SIZE];
    int rlen, tcnt, restart, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (!HYD_handle.user_global.prepend_rank) {
        status = cb(buf, buflen);
        HYDU_ERR_POP(status, "error in the UI defined callback\n");
        goto fn_exit;
    }

    if (*storage_len)
        hdr = (struct HYD_pmcd_stdio_hdr *) *storage;
    else
        hdr = (struct HYD_pmcd_stdio_hdr *) buf;

    if (*storage_len || (buflen < sizeof(struct HYD_pmcd_stdio_hdr) + hdr->buflen)) {
        HYDU_MALLOC(tmp, char *, *storage_len + buflen, status);
        memcpy(tmp, *storage, *storage_len);
        memcpy(tmp + *storage_len, buf, buflen);
        HYDU_FREE(*storage);
        *storage = tmp;
        *storage_len += buflen;
        tmp = NULL;

        rbuf = *storage;
        rlen = *storage_len;
    }
    else {
        rbuf = buf;
        rlen = buflen;
    }

    while (1) {
        hdr = (struct HYD_pmcd_stdio_hdr *) rbuf;

        if (rlen < sizeof(struct HYD_pmcd_stdio_hdr) + hdr->buflen)
            break;

        rbuf += sizeof(struct HYD_pmcd_stdio_hdr);
        rlen -= sizeof(struct HYD_pmcd_stdio_hdr);

        tbuf = rbuf;
        tcnt = hdr->buflen;
        do {
            if (tcnt == 0)
                break;

            HYDU_snprintf(str, HYD_TMPBUF_SIZE, "[%d] ", hdr->rank);
            status = cb(str, strlen(str));
            HYDU_ERR_POP(status, "error in the UI defined callback\n");

            restart = 0;
            for (i = 0; i < tcnt; i++) {
                if (tbuf[i] == '\n') {
                    status = cb(tbuf, i + 1);
                    HYDU_ERR_POP(status, "error in the UI defined callback\n");

                    tbuf += i + 1;
                    tcnt -= i + 1;

                    restart = 1;
                    break;
                }
            }
            if (restart)
                continue;

            status = cb(tbuf, tcnt);
            HYDU_ERR_POP(status, "error in the UI defined callback\n");
            break;
        } while (1);

        rbuf += hdr->buflen;
        rlen -= hdr->buflen;

        if (!rlen)
            break;
    }

    if (rlen) { /* left overs */
        HYDU_MALLOC(tmp, char *, rlen, status);
        memcpy(tmp, rbuf, rlen);
        if (*storage)
            HYDU_FREE(*storage);
        *storage = tmp;
    }
    else {
        if (*storage)
            HYDU_FREE(*storage);
        *storage = NULL;
    }
    *storage_len = rlen;

  fn_exit:
    return status;

  fn_fail:
    goto fn_exit;
}

static HYD_status stdout_cb(void *buf, int buflen)
{
    static char *storage = NULL;
    static int storage_len = 0;

    return outerr(buf, buflen, &storage, &storage_len, HYD_handle.stdout_cb);
}

static HYD_status stderr_cb(void *buf, int buflen)
{
    static char *storage = NULL;
    static int storage_len = 0;

    return outerr(buf, buflen, &storage, &storage_len, HYD_handle.stderr_cb);
}

HYD_status HYD_pmci_launch_procs(void)
{
    struct HYD_proxy *proxy;
    struct HYD_node *node_list = NULL, *node, *tnode;
    char *proxy_args[HYD_NUM_TMP_STRINGS] = { NULL }, *control_port = NULL;
    int enable_stdin, node_count, i, *control_fd;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDT_dmx_register_fd(1, &HYD_handle.cleanup_pipe[0], POLLIN, NULL, ui_cmd_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

    status = HYD_pmcd_pmi_alloc_pg_scratch(&HYD_handle.pg_list);
    HYDU_ERR_POP(status, "error allocating pg scratch space\n");

    /* Copy the host list to pass to the bootstrap server */
    node_list = NULL;
    node_count = 0;
    for (proxy = HYD_handle.pg_list.proxy_list; proxy; proxy = proxy->next) {
        HYDU_alloc_node(&node);
        node->hostname = HYDU_strdup(proxy->node.hostname);
        node->core_count = proxy->node.core_count;
        node->next = NULL;

        if (node_list == NULL) {
            node_list = node;
        }
        else {
            for (tnode = node_list; tnode->next; tnode = tnode->next);
            tnode->next = node;
        }

        node_count++;
    }

    status = HYDU_sock_create_and_listen_portstr(HYD_handle.user_global.iface,
                                                 HYD_handle.port_range, &control_port,
                                                 HYD_pmcd_pmiserv_control_listen_cb,
                                                 (void *) (size_t) 0);
    HYDU_ERR_POP(status, "unable to create PMI port\n");
    if (HYD_handle.user_global.debug)
        HYDU_dump(stdout, "Got a control port string of %s\n", control_port);

    status = HYD_pmcd_pmi_fill_in_proxy_args(proxy_args, control_port, 0);
    HYDU_ERR_POP(status, "unable to fill in proxy arguments\n");

    status = HYD_pmcd_pmi_fill_in_exec_launch_info(&HYD_handle.pg_list);
    HYDU_ERR_POP(status, "unable to fill in executable arguments\n");

    status = HYDT_dmx_stdin_valid(&enable_stdin);
    HYDU_ERR_POP(status, "unable to check if stdin is valid\n");

    HYDU_MALLOC(control_fd, int *, node_count * sizeof(int), status);
    for (i = 0; i < node_count; i++)
        control_fd[i] = HYD_FD_UNSET;

    status = HYDT_bind_init(HYD_handle.user_global.binding, HYD_handle.user_global.bindlib);
    HYDU_ERR_POP(status, "unable to initializing binding library");

    status = HYDT_bsci_launch_procs(proxy_args, node_list, control_fd, enable_stdin, stdout_cb,
                                    stderr_cb);
    HYDU_ERR_POP(status, "bootstrap server cannot launch processes\n");

    for (i = 0, proxy = HYD_handle.pg_list.proxy_list; proxy; proxy = proxy->next, i++)
        if (control_fd[i] != HYD_FD_UNSET) {
            proxy->control_fd = control_fd[i];

            status = HYDT_dmx_register_fd(1, &control_fd[i], HYD_POLLIN, (void *) (size_t) 0,
                                          HYD_pmcd_pmiserv_proxy_init_cb);
            HYDU_ERR_POP(status, "unable to register fd\n");
        }

    HYDU_FREE(control_fd);

  fn_exit:
    if (control_port)
        HYDU_FREE(control_port);
    HYDU_free_strlist(proxy_args);
    HYDU_free_node_list(node_list);
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYD_pmci_wait_for_completion(int timeout)
{
    struct HYD_pg *pg;
    struct HYD_pmcd_pmi_pg_scratch *pg_scratch;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    /* We first wait for the exit statuses to arrive till the timeout
     * period */
    for (pg = &HYD_handle.pg_list; pg; pg = pg->next) {
        pg_scratch = (struct HYD_pmcd_pmi_pg_scratch *) pg->pg_scratch;

        while (pg_scratch->control_listen_fd != HYD_FD_CLOSED) {
            status = HYDT_dmx_wait_for_event(timeout);
            if (status == HYD_TIMED_OUT) {
                status = HYD_pmcd_pmiserv_cleanup();
                HYDU_ERR_POP(status, "cleanup of processes failed\n");
            }
            HYDU_ERR_POP(status, "error waiting for event\n");
        }

        status = HYD_pmcd_pmi_free_pg_scratch(pg);
        HYDU_ERR_POP(status, "error freeing PG scratch space\n");
    }

    /* Either all application processes exited or we have timed
     * out. We now wait for all the proxies to terminate. */
    status = HYDT_bsci_wait_for_completion(-1);
    HYDU_ERR_POP(status, "bootstrap server returned error waiting for completion\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYD_pmci_finalize(void)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYD_pmcd_pmi_finalize();
    HYDU_ERR_POP(status, "unable to finalize process manager utils\n");

    status = HYDT_bsci_finalize();
    HYDU_ERR_POP(status, "unable to finalize bootstrap server\n");

    status = HYDT_dmx_finalize();
    HYDU_ERR_POP(status, "error returned from demux finalize\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
