/*
 * limic.h
 *
 * LiMIC2:  Linux Kernel Module for High-Performance MPI Intra-Node
 *          Communication
 *
 * Author:  Hyun-Wook Jin <jinh@konkuk.ac.kr>
 *          System Software Laboratory
 *          Department of Computer Science and Engineering
 *          Konkuk University
 *
 * History: Jul 15 2007 Launch
 */

#ifndef _LIMIC_INCLUDED_
#define _LIMIC_INCLUDED_

/* /dev file name */
#define DEV_NAME "limic"

#define LIMIC_TX   0x1c01
#define LIMIC_RX   0x1c02

#define LIMIC_TX_DONE    1
#define LIMIC_RX_DONE    2

typedef struct limic_user{
    int nr_pages;   /* pages actually referenced */
    int offset;     /* offset to start of valid data */
    int length;     /* number of valid bytes of data */

    unsigned long va;
    void *mm;        /* struct mm_struct * */
    void *tsk;       /* struct task_struct * */
}limic_user;

typedef struct limic_request{
    void *buf;       /* user buffer */
    int len;         /* buffer length */
    limic_user *lu;  /* shandle or rhandle */
}limic_request;

#endif

