/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMIP_H_INCLUDED
#define PMIP_H_INCLUDED

#include "hydra_base.h"
#include "hydra_utils.h"
#include "pmi_common.h"

#define HYD_pmcd_pmi_proxy_dump(_status, _fd, ...)                      \
    {                                                                   \
        char _str[HYD_TMPBUF_SIZE];                                     \
        struct HYD_pmcd_stdio_hdr _hdr;                                 \
        int _recvd, _closed;                                            \
        MPL_snprintf(_str, HYD_TMPBUF_SIZE, "[%s] ", HYD_dbg_prefix);   \
        MPL_snprintf(_str + strlen(_str), HYD_TMPBUF_SIZE - strlen(_str), __VA_ARGS__); \
        if (HYD_pmcd_pmip.user_global.prepend_rank) {                   \
            (_hdr).rank = -1;                                           \
            (_hdr).buflen = strlen(_str);                               \
            (_status) = HYDU_sock_write((_fd), &(_hdr), sizeof((_hdr)), &(_recvd), &(_closed)); \
            HYDU_ERR_POP((_status), "sock write error\n");              \
            HYDU_ASSERT(!(_closed), (_status));                         \
        }                                                               \
        (_status) = HYDU_sock_write((_fd), &(_str), strlen(_str), &(_recvd), &(_closed)); \
        HYDU_ERR_POP((_status), "sock write error\n");                  \
        HYDU_ASSERT(!(_closed), (_status));                             \
    }

struct HYD_pmcd_pmip {
    struct HYD_user_global user_global;

    struct {
        int enable_stdin;
        int global_core_count;
        int global_process_count;

        /* PMI */
        char *pmi_fd;
        char *pmi_port;
        int pmi_rank;           /* If this is -1, we auto-generate it */
        char *pmi_process_mapping;
    } system_global;            /* Global system parameters */

    struct {
        /* Upstream server contact information */
        char *server_name;
        int server_port;
        int control;
    } upstream;

    /* Currently our downstream only consists of actual MPI
     * processes */
    struct {
        int *out;
        int *err;
        int in;

        int *pid;
        int *exit_status;

        int *pmi_rank;
        int *pmi_fd;
        int *pmi_fd_active;
    } downstream;

    /* Proxy details */
    struct {
        int id;
        int pgid;
        char *interface_env_name;
        char *hostname;
        char *local_binding;

        int proxy_core_count;
        int proxy_process_count;

        char *spawner_kvs_name;
        struct HYD_pmcd_pmi_kvs *kvs;   /* Node-level KVS space for node attributes */
    } local;

    /* Process segmentation information for this proxy */
    int start_pid;
    struct HYD_exec *exec_list;
};

extern struct HYD_pmcd_pmip HYD_pmcd_pmip;
extern struct HYD_arg_match_table HYD_pmcd_pmip_match_table[];

HYD_status HYD_pmcd_pmip_get_params(char **t_argv);
void HYD_pmcd_pmip_killjob(void);
HYD_status HYD_pmcd_pmip_control_cmd_cb(int fd, HYD_event_t events, void *userp);

#endif /* PMIP_H_INCLUDED */
