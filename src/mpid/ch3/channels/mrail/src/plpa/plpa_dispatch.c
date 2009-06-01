/* Copyright (c) 2003-2009, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */
/* -*- c -*-
 *
 * Copyright (c) 2004-2006 The Trustees of Indiana University.
 *                         All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2007-2008 Cisco Systems, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "plpa.h"
#include "plpa_internal.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(PLPA_DEBUG) && PLPA_DEBUG
static void append(char *str, int val);
static char *cpu_set_to_list(const PLPA_NAME(cpu_set_t) *cpu_set);
#endif /* defined(PLPA_DEBUG) && PLPA_DEBUG */

int PLPA_NAME(setaffinity)(char* config, int pid)
{
    if (!config)
    {
        return 1;
    }

    /* Convert the config string to a PLPA cpu set. */
    PLPA_NAME(cpu_set_t) cpu_set;
    PLPA_CPU_ZERO(&cpu_set);
    parser_setup_string(config);
    int ret = token_parse(&cpu_set);

    if (ret != 0)
    {
        return ret;
    }

    ret = PLPA_NAME(sched_setaffinity)((pid_t) pid, sizeof(cpu_set), &cpu_set);
    switch (ret)
    {
    case 0:
#if defined(PLPA_DEBUG) && PLPA_DEBUG
        printf("pid %d's new affinity list: %s\n", pid, cpu_set_to_list(&cpu_set));
#endif /* defined(PLPA_DEBUG) && PLPA_DEBUG */
        break;

    case ENOSYS:
        printf("sched_setaffinity: processor affinity is not supported on this kernel\n");
        printf("failed to set pid %d's affinity.\n", pid);
        break;

    default:
        perror("sched_setaffinity");
        printf("failed to set pid %d's affinity.\n", pid);
        break;
    }

    return ret;
}

/**
 * Call the kernel's setaffinity, massaging the user's input
 * parameters as necessary
 */
int PLPA_NAME(sched_setaffinity)(pid_t pid, size_t cpusetsize,
                                 const PLPA_NAME(cpu_set_t) *cpuset)
{
    int ret;
    size_t i;
    PLPA_NAME(cpu_set_t) tmp;
    PLPA_NAME(api_type_t) api;

    /* Check to see that we're initialized */
    if (!PLPA_NAME(initialized)) {
        if (0 != (ret = PLPA_NAME(init)())) {
            return ret;
        }
    }

    /* Check for bozo arguments */
    if (NULL == cpuset) {
        return EINVAL;
    }

    /* Probe the API type */
    if (0 != (ret = PLPA_NAME(api_probe)(&api))) {
        return ret;
    }
    switch (api) {
    case PLPA_NAME_CAPS(PROBE_OK):
        /* This shouldn't happen, but check anyway */
        if (cpusetsize > sizeof(*cpuset)) {
            return EINVAL;
        }

        /* If the user-supplied bitmask is smaller than what the
           kernel wants, zero out a temporary buffer of the size that
           the kernel wants and copy the user-supplied bitmask to the
           lower part of the temporary buffer.  This could be done
           more efficiently, but we're looking for clarity/simplicity
           of code here -- this is not intended to be
           performance-critical. */
        if (cpusetsize < PLPA_NAME(len)) {
            memset(&tmp, 0, sizeof(tmp));
            for (i = 0; i < cpusetsize * 8; ++i) {
                if (PLPA_CPU_ISSET(i, cpuset)) {
                    PLPA_CPU_SET(i, &tmp);
                }
            }
        }

        /* If the user-supplied bitmask is larger than what the kernel
           will accept, scan it and see if there are any set bits in
           the part larger than what the kernel will accept.  If so,
           return EINVAL.  Otherwise, copy the part that the kernel
           will accept into a temporary and use that.  Again,
           efficinency is not the issue of this code -- clarity is. */
        else if (cpusetsize > PLPA_NAME(len)) {
            for (i = PLPA_NAME(len) * 8; i < cpusetsize * 8; ++i) {
                if (PLPA_CPU_ISSET(i, cpuset)) {
                    return EINVAL;
                }
            }
            /* No upper-level bits are set, so now copy over the bits
               that the kernel will look at */
            memset(&tmp, 0, sizeof(tmp));
            for (i = 0; i < PLPA_NAME(len) * 8; ++i) {
                if (PLPA_CPU_ISSET(i, cpuset)) {
                    PLPA_CPU_SET(i, &tmp);
                }
            }
        }

        /* Otherwise, the user supplied a buffer that is exactly the
           right size.  Just for clarity of code, copy the user's
           buffer into the temporary and use that. */
        else {
            memcpy(&tmp, cpuset, cpusetsize);
        }

        /* Now do the syscall:
           Return 0 upon success.  According to
           http://www.open-mpi.org/community/lists/plpa-users/2006/02/0016.php,
           all the kernel implementations return >= 0 upon success. */
        return (ret = syscall(__NR_sched_setaffinity, pid, PLPA_NAME(len), &tmp)) >= 0 ? 0 : ret; 
        break;

    case PLPA_NAME_CAPS(PROBE_NOT_SUPPORTED):
        /* Process affinity not supported here */
        return ENOSYS;
        break;

    default:
        /* Something went wrong */
        /* JMS: would be good to have something other than EINVAL here
           -- suggestions? */
        return EINVAL;
        break;
    }
}


/**
 * Call the kernel's getaffinity, massaging the user's input
 * parameters as necessary
 */
int PLPA_NAME(sched_getaffinity)(pid_t pid, size_t cpusetsize,
                                PLPA_NAME(cpu_set_t) *cpuset)
{
    int ret;
    PLPA_NAME(api_type_t) api;

    /* Check to see that we're initialized */
    if (!PLPA_NAME(initialized)) {
        if (0 != (ret = PLPA_NAME(init)())) {
            return ret;
        }
    }

    /* Check for bozo arguments */
    if (NULL == cpuset) {
        return EINVAL;
    }
    /* Probe the API type */
    if (0 != (ret = PLPA_NAME(api_probe)(&api))) {
        return ret;
    }
    switch (api) {
    case PLPA_NAME_CAPS(PROBE_OK):
        /* This shouldn't happen, but check anyway */
        if (PLPA_NAME(len) > sizeof(*cpuset)) {
            return EINVAL;
        }

        /* If the user supplied a buffer that is too small, then don't
           even bother */
        if (cpusetsize < PLPA_NAME(len)) {
            return EINVAL;
        }

        /* Now we know that the user's buffer is >= the size required
           by the kernel.  If it's >, then zero it out so that the
           bits at the top are cleared (since they won't be set by the
           kernel) */
        if (cpusetsize > PLPA_NAME(len)) {
            memset(cpuset, 0, cpusetsize);
        }

        /* Now do the syscall: 
           Return 0 upon success.  According to
           http://www.open-mpi.org/community/lists/plpa-users/2006/02/0016.php,
           all the kernel implementations return >= 0 upon success. */
        return (ret = syscall(__NR_sched_getaffinity, pid, PLPA_NAME(len), cpuset)) >= 0 ? 0 : ret;
        break;

    case PLPA_NAME_CAPS(PROBE_NOT_SUPPORTED):
        /* Process affinity not supported here */
        return ENOSYS;
        break;

    default:
        /* Something went wrong */
        return EINVAL;
        break;
    }
}

#if defined(PLPA_DEBUG) && PLPA_DEBUG
static void append(char *str, int val)
{
    char temp[8];

    if ('\0' != str[0]) {
        strcat(str, ",");
    }
    snprintf(temp, sizeof(temp) - 1, "%d", val);
    strcat(str, temp);
}

static char *cpu_set_to_list(const PLPA_NAME(cpu_set_t) *cpu_set)
{
    size_t i, j, last_bit, size = PLPA_BITMASK_CPU_MAX;
    unsigned long long mask_value = 0;
    /* Upper bound on string length: 4 digits per
       PLPA_BITMASK_CPU_MAX + 1 comma for each */
    static char str[PLPA_BITMASK_CPU_MAX * 5];
    char temp[8];

    if (sizeof(mask_value) * 8 < size) {
        size = sizeof(mask_value) * 8;
    }
    /* Only print ranges for 3 or more consecutive bits, otherwise
       print individual numbers. */
    str[0] = '\0';
    for (i = 0; i < size; ++i) {
        if (PLPA_CPU_ISSET(i, cpu_set)) {
            /* This bit is set -- is it part of a longer series? */
            /* Simple answer: if this is the last or next-to-last bit,
               just print it */
            if (i == size - 1 || i == size - 2) {
                append(str, i);
                continue;
            }
            /* Simple answer: if next bit is not set, then just print
               it */
            else if (!PLPA_CPU_ISSET(i + 1, cpu_set)) {
                append(str, i);
                continue;
            }

            /* Look for the next unset bit */
            last_bit = i;
            for (j = i + 1; j < size; ++j) {
                if (!PLPA_CPU_ISSET(j, cpu_set)) {
                    last_bit = j - 1;
                    break;
                }
            }
            /* If we fell off the end of the array without finding an
               unset bit, then they're all set. */
            if (j >= size) {
                last_bit = size - 1;
            }

            if (i != last_bit) {
                /* last_bit is now the last bit set after i (and it
                   might actually be i).  So if last_bit > i+2, print
                   the range. */
                if (last_bit >= i + 2) {
                    append(str, i);
                    strcat(str, "-");
                    snprintf(temp, sizeof(temp) - 1, "%d", (int) last_bit);
                    strcat(str, temp);
                } else {
                    /* It wasn't worth printing a range, so print
                       i, and possibly print last_bit */
                    append(str, i);
                    if (last_bit != i) {
                        append(str, last_bit);
                    }
                }
                i = last_bit + 1;
            }
        }
    }
    return str;
}
#endif /* defined(PLPA_DEBUG) && PLPA_DEBUG */

