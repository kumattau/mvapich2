/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef SSH_H_INCLUDED
#define SSH_H_INCLUDED

#include "hydra_base.h"

HYD_status HYDT_bscd_ssh_launch_procs(char **global_args, const char *proxy_id_str,
                                      struct HYD_proxy *proxy_list);

#endif /* SSH_H_INCLUDED */
