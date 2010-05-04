/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "hydra_utils.h"
#include "pmi_proxy.h"
#include "demux.h"
#include "ckpoint.h"

struct HYD_pmcd_pmip HYD_pmcd_pmip;

HYD_status HYD_pmcd_pmi_proxy_control_connect_cb(int fd, HYD_event_t events, void *userp)
{
    int accept_fd = -1;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (events & HYD_STDIN)
        HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "stdout handler got stdin event\n");

    /* We got a connection from upstream */
    status = HYDU_sock_accept(fd, &accept_fd);
    HYDU_ERR_POP(status, "accept error\n");

    status = HYDT_dmx_register_fd(1, &accept_fd, HYD_STDOUT, NULL,
                                  HYD_pmcd_pmi_proxy_control_cmd_cb);
    HYDU_ERR_POP(status, "unable to register fd\n");

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYD_pmcd_pmi_proxy_control_cmd_cb(int fd, HYD_event_t events, void *userp)
{
    int cmd_len;
    enum HYD_pmcd_pmi_proxy_cmds cmd;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    if (events & HYD_STDIN)
        HYDU_ERR_SETANDJUMP(status, HYD_INTERNAL_ERROR, "stdout handler got stdin event\n");

    /* We got a command from upstream */
    status = HYDU_sock_read(fd, &cmd, sizeof(enum HYD_pmcd_pmi_proxy_cmds), &cmd_len,
                            HYDU_SOCK_COMM_MSGWAIT);
    HYDU_ERR_POP(status, "error reading command from launcher\n");
    if (cmd_len == 0) {
        /* The connection has closed */
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");
        close(fd);
        goto fn_exit;
    }

    if (cmd == PROC_INFO) {
        status = HYD_pmcd_pmi_proxy_procinfo(fd);
    }
    else if (cmd == USE_AS_STDOUT) {
        HYD_pmcd_pmip.upstream.out = fd;
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");
    }
    else if (cmd == USE_AS_STDERR) {
        HYD_pmcd_pmip.upstream.err = fd;
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");
    }
    else if (cmd == USE_AS_STDIN) {
        HYD_pmcd_pmip.upstream.in = fd;
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");
    }
    else if (cmd == KILL_JOB) {
        HYD_pmcd_pmi_proxy_killjob();
        status = HYD_SUCCESS;
    }
    else if (cmd == PROXY_SHUTDOWN) {
        /* FIXME: shutdown should be handled more cleanly. That is,
         * check if there are other processes still running and kill
         * them before exiting. */
        exit(-1);
    }
    else if (cmd == CKPOINT) {
        HYDU_dump(stdout, "requesting checkpoint\n");
        status = HYDT_ckpoint_suspend();
        HYDU_dump(stdout, "checkpoint completed\n");
    }
    else {
        status = HYD_INTERNAL_ERROR;
    }

    HYDU_ERR_POP(status, "error handling proxy command\n");

    /* One of these commands can trigger the start of the application
     * since they can arrive in any order. */
    if ((cmd == PROC_INFO) || (cmd == USE_AS_STDOUT) || (cmd == USE_AS_STDERR) ||
        (cmd == USE_AS_STDIN))
        if ((HYD_pmcd_pmip.start_pid != -1) &&
            (HYD_pmcd_pmip.upstream.out != -1) && (HYD_pmcd_pmip.upstream.err != -1))
            if ((HYD_pmcd_pmip.start_pid != 0) || (HYD_pmcd_pmip.upstream.in != -1)) {
                status = HYD_pmcd_pmi_proxy_launch_procs();
                HYDU_ERR_POP(status, "HYD_pmcd_pmi_proxy_launch_procs returned error\n");
            }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}

HYD_status HYD_pmcd_pmi_proxy_stdout_cb(int fd, HYD_event_t events, void *userp)
{
    int closed, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDU_sock_stdout_cb(fd, events, HYD_pmcd_pmip.upstream.out, &closed);
    HYDU_ERR_POP(status, "stdout callback error\n");

    if (closed) {
        /* The connection has closed */
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");

        for (i = 0; i < HYD_pmcd_pmip.local.proxy_process_count; i++)
            if (HYD_pmcd_pmip.downstream.out[i] == fd)
                HYD_pmcd_pmip.downstream.out[i] = -1;

        close(fd);
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_stderr_cb(int fd, HYD_event_t events, void *userp)
{
    int closed, i;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDU_sock_stdout_cb(fd, events, HYD_pmcd_pmip.upstream.err, &closed);
    HYDU_ERR_POP(status, "stdout callback error\n");

    if (closed) {
        /* The connection has closed */
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");

        for (i = 0; i < HYD_pmcd_pmip.local.proxy_process_count; i++)
            if (HYD_pmcd_pmip.downstream.err[i] == fd)
                HYD_pmcd_pmip.downstream.err[i] = -1;

        close(fd);
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}


HYD_status HYD_pmcd_pmi_proxy_stdin_cb(int fd, HYD_event_t events, void *userp)
{
    int closed;
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    status = HYDU_sock_stdin_cb(HYD_pmcd_pmip.downstream.in, events,
                                HYD_pmcd_pmip.upstream.in,
                                HYD_pmcd_pmip.local.stdin_tmp_buf,
                                &HYD_pmcd_pmip.local.stdin_buf_count,
                                &HYD_pmcd_pmip.local.stdin_buf_offset, &closed);
    HYDU_ERR_POP(status, "stdin callback error\n");

    if (closed) {
        /* The connection has closed */
        status = HYDT_dmx_deregister_fd(fd);
        HYDU_ERR_POP(status, "unable to deregister fd\n");

        close(HYD_pmcd_pmip.upstream.in);
        close(HYD_pmcd_pmip.downstream.in);
        HYD_pmcd_pmip.downstream.in = -1;
    }

  fn_exit:
    HYDU_FUNC_EXIT();
    return status;

  fn_fail:
    goto fn_exit;
}
