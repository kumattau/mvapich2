/* Copyright (c) 2003-2011, The Ohio State University. All rights
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

#include "mpirunconf.h"
#include "mpirun_util.h"
#include "string.h"
#include "stdio.h"
#include <errno.h>

/*
 * ptr must be suitable for a call to realloc
 */
char *vedit_str(char *const ptr, const char *format, va_list args)
{
    va_list ap;
    int size;
    char *str;

    va_copy(ap, args);
    size = vsnprintf(NULL, 0, format, ap);
    va_end(ap);

    if (size++ < 0)
        return NULL;

    str = realloc(ptr, sizeof(char) * size);

    if (!str) {
        perror("vedit_str [realloc]");
        exit(EXIT_FAILURE);
    }

    va_copy(ap, args);
    size = vsnprintf(str, size, format, ap);
    va_end(ap);

    if (size < 0)
        return NULL;

    return str;
}

/*
 * ptr must be suitable for a call to realloc
 */
char *edit_str(char *const ptr, char const *const format, ...)
{
    va_list ap;
    char *str;

    va_start(ap, format);
    str = vedit_str(ptr, format, ap);
    va_end(ap);

    return str;
}

char *mkstr(char const *const format, ...)
{
    va_list ap;
    char *str;

    va_start(ap, format);
    str = vedit_str(NULL, format, ap);
    va_end(ap);

    return str;
}

/*
 * ptr must be dynamically allocated
 */
char *append_str(char *ptr, char const *const suffix)
{

    ptr = realloc(ptr, sizeof(char) * (strlen(ptr) + strlen(suffix) + 1));

    if (!ptr) {
        perror("append_str [realloc]");
        exit(EXIT_FAILURE);
    }

    strcat(ptr, suffix);

    return ptr;
}

int read_socket(int socket, void *buffer, size_t bytes)
{
    char *data = buffer;
    ssize_t rv;

    while (bytes != 0) {
        if ((rv = read(socket, data, bytes)) == -1) {
            switch (errno) {
            case EINTR:
            case EAGAIN:
                continue;
            default:
                perror("read");
                return -1;
            }
        }

        data += rv;
        bytes -= rv;
    }

    return 0;
}

int write_socket(int socket, void *buffer, size_t bytes)
{
    char *data = buffer;
    ssize_t rv;

    while (bytes != 0) {
        if ((rv = write(socket, data, bytes)) == -1) {
            switch (errno) {
            case EINTR:
            case EAGAIN:
                continue;
            default:
                perror("write");
                return -1;
            }
        }

        data += rv;
        bytes -= rv;
    }

    return 0;
}

#ifdef CKPT

#define MAX_CR_MSG_LEN  256
#define CRU_MAX_KEY_LEN 64
#define CRU_MAX_VAL_LEN 64

struct CRU_keyval_pairs {
    char key[CRU_MAX_KEY_LEN];
    char value[CRU_MAX_VAL_LEN];
};

static struct CRU_keyval_pairs CRU_keyval_tab[64] = { {{0}} };

static int CRU_keyval_tab_idx = 0;

int CR_MPDU_writeline(int, char *);
int CR_MPDU_readline(int, char *, int);
int CR_MPDU_parse_keyvals(char *);
char *CR_MPDU_getval(const char *, char *, int);

int CR_MPDU_writeline(int fd, char *buf)
{
    int size, n;

    size = strlen(buf);

    if (size > MAX_CR_MSG_LEN) {
        buf[MAX_CR_MSG_LEN - 1] = '\0';
    } else {
        n = write(fd, buf, size);
        if (n < 0) {
            return (-1);
        }
    }
    return 0;
}

int CR_MPDU_readline(int fd, char *buf, int maxlen)
{
    int n = 1;
    int rc;
    char c, *ptr;

    for (ptr = buf; n < maxlen; ++n) {
      again:
        rc = read(fd, &c, 1);
        if (rc == 1) {
            *ptr++ = c;
            if (c == '\n')
                break;
        } else if (rc == 0) {
            if (n == 1)
                return (0);
            else
                break;
        } else {
            if (errno == EINTR)
                goto again;
            return (-1);
        }
    }
    *ptr = 0;
    return (n);
}

int CR_MPDU_parse_keyvals(char *st)
{
    char *p, *keystart, *valstart;

    if (!st) {
        return (-1);
    }

    CRU_keyval_tab_idx = 0;
    p = st;
    while (1) {
        while (*p == ' ') {
            ++p;
        }

        /* got non-blank */
        if (*p == '=') {
            return (-2);
        }
        if ((*p == '\n') || (*p == '\0'))
            return (0);         /* normal exit */

        keystart = p;
        while ((*p != ' ') && (*p != '=') && (*p != '\n') && (*p != '\0')) {
            ++p;
        }
        if ((*p == ' ') || (*p == '\n') || (*p == '\0')) {
            return (-3);
        }
        strncpy(CRU_keyval_tab[CRU_keyval_tab_idx].key, keystart, CRU_MAX_KEY_LEN);
        CRU_keyval_tab[CRU_keyval_tab_idx].key[p - keystart] = '\0';    /* store key */

        valstart = ++p;
        while ((*p != ' ') && (*p != '\n') && (*p != '\0')) {
            ++p;
        }
        strncpy(CRU_keyval_tab[CRU_keyval_tab_idx].value, valstart, CRU_MAX_VAL_LEN);
        CRU_keyval_tab[CRU_keyval_tab_idx].value[p - valstart] = '\0';  /* store value */
        ++CRU_keyval_tab_idx;
        if (*p == ' ') {
            continue;
        }
        if (*p == '\n' || *p == '\0') {
            return (0);         /* value has been set to empty */
        }
    }

    return (-4);
}

char *CR_MPDU_getval(const char *keystr, char *valstr, int vallen)
{
    int i;

    for (i = 0; i < CRU_keyval_tab_idx; ++i) {
        if (strcmp(keystr, CRU_keyval_tab[i].key) == 0) {
            strncpy(valstr, CRU_keyval_tab[i].value, vallen - 1);
            valstr[vallen - 1] = '\0';
            return (valstr);
        }
    }
    valstr[0] = '\0';
    return (NULL);
}

#endif                          /* CKPT */

/* vi:set sw=4 sts=4 tw=80 */
