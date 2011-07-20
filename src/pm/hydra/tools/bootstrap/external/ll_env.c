/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2008 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "hydra.h"
#include "bsci.h"
#include "common.h"

HYD_status HYDT_bscd_ll_query_env_inherit(const char *env_name, int *ret)
{
    const char *env_list[] = { "LOADL_STEP_CLASS", "LOADL_STEP_ARGS",
                               "LOADL_STEP_ID", "LOADL_STARTD_PORT",
                               "LOADL_STEP_NICE", "LOADL_STEP_IN", "LOADL_STEP_ERR",
                               "LOADL_STEP_GROUP", "LOADL_STEP_NAME", "LOADL_STEP_ACCT",
                               "LOADL_STEP_TYPE", "LOADL_STEP_OWNER", "LOADL_ACTIVE",
                               "LOADL_STEP_COMMAND", "LOADL_JOB_NAME", "LOADL_STEP_OUT",
                               "LOADL_STEP_INITDIR", "LOADL_PROCESSOR_LIST",
                               "LOADLBATCH",
                               "AIX_MINKTHREADS", "AIX_MNRATIO",
                               "AIX_PTHREAD_SET_STACKSIZE", "AIXTHREAD_COND_DEBUG",
                               "AIXTHREAD_MUTEX_DEBUG", "AIXTHREAD_RWLOCK_DEBUG",
                               "AIXTHREAD_SCOPE", "AIXTHREAD_SLPRATIO", "MALLOCDEBUG",
                               "MALLOCTYPE", "MALLOCMULTIHEAP", "MP_ADAPTER_USE",
                               "MP_BUFFER_MEM", "MP_CHECKDIR", "MP_CHECKFILE",
                               "MP_CLOCK_SOURCE", "MP_CMDFILE", "MP_COREDIR",
                               "MP_COREFILE_FORMAT", "MP_COREFILE_SIGTERM", "MP_CPU_USE",
                               "MP_CSS_INTERRUPT", "MP_DBXPROMPTMOD",
                               "MP_DEBUG_INITIAL_STOP", "MP_DEBUG_LOG", "MP_EAGER_LIMIT",
                               "MP_EUIDEVELOP", "MP_EUIDEVICE", "MP_EUILIB",
                               "MP_EUILIBPATH", "MP_FENCE", "MP_HINTS_FILTERED",
                               "MP_HOLD_STDIN", "MP_HOSTFILE", "MP_INFOLEVEL",
                               "MP_INTRDELAY", "MP_IONODEFILE", "MP_LABELIO",
                               "MP_LLFILE", "MP_MAX_TYPEDEPTH", "MP_MSG_API",
                               "MP_NEWJOB", "MP_NOARGLIST", "MP_NODES", "MP_PGMMODEL",
                               "MP_PMD_VERSION", "MP_PMDLOG", "MP_PMDSUFFIX",
                               "MP_PMLIGHTS", "MP_POLLING_INTERVAL", "MP_PRIORITY",
                               "MP_PROCS", "MP_PULSE", "MP_RESD", "MP_RETRY",
                               "MP_RETRYCOUNT", "MP_RMPOOL", "MP_SAMPLEFREQ",
                               "MP_SAVE_LLFILE", "MP_SAVEHOSTFILE", "MP_SHARED_MEMORY",
                               "MP_SINGLE_THREAD", "MP_STDINMODE", "MP_STDOUTMODE",
                               "MP_SYNC_ON_CONNECT", "MP_TASKS_PER_NODE", "MP_TBUFFSIZE",
                               "MP_TBUFFWRAP", "MP_THREAD_STACKSIZE", "MP_TIMEOUT",
                               "MP_TMPDIR", "MP_TRACEDIR", "MP_TRACELEVEL",
                               "MP_TTEMPSIZE", "MP_USE_FLOW_CONTROL", "MP_USRPORT",
                               "MP_WAIT_MODE", "PSALLOC", "RT_GRQ", "SPINLOOPTIME",
                               "YIELDLOOPTIME", "XLSMPOPTS", NULL
    };

    HYDU_FUNC_ENTER();

    *ret = !HYDTI_bscd_in_env_list(env_name, env_list);

    HYDU_FUNC_EXIT();

    return HYD_SUCCESS;
}
