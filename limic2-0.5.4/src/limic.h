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
 *
 *          Feb 27 2009
 *            Modified by Karthik Gopalakrishnan (gopalakk@cse.ohio-state.edu)
 */

#ifndef _LIMIC_H_INCLUDED_
#define _LIMIC_H_INCLUDED_

#define LIMIC_TX_DONE    1
#define LIMIC_RX_DONE    2
#define LIMIC_VERSION_OK 3
#define LIMIC_TXW_DONE   4

typedef struct limic_user{
    int nr_pages;   /* pages actually referenced */
    int offset;     /* offset to start of valid data */
    int length;     /* number of valid bytes of data */

    unsigned long va;
    void *mm;        /* struct mm_struct * */
    void *tsk;       /* struct task_struct * */
}limic_user;

extern int limic_open( void );
extern void limic_close( int fd );
extern int limic_tx_init( int fd, void *buf, int len, limic_user *lu );
extern int limic_rx_comp( int fd, void *buf, int len, limic_user *lu );
extern int limic_tx_comp( int fd, void *buf, int len, limic_user *lu );

#endif
