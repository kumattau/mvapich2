/*
 * limic_lib.h
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

#ifndef _LIMIC_LIB_INCLUDED_
#define _LIMIC_LIB_INCLUDED_


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "limic.h"


static int limic_open( void )
{
    int fd;

    fd = open("/dev/limic", O_RDONLY);
    if(fd == -1)
        printf("LiMIC: (limic_open) file open fail\n");
    
    return fd;
}


static void limic_close( int fd )
{
    close( fd );
}


static int limic_tx_init( int fd, void *buf, int len, limic_user *lu )
{
    int ret;
    limic_request sreq;

    sreq.buf = buf;
    sreq.len = len;
    sreq.lu = lu;
    
    ret = ioctl(fd, LIMIC_TX, &sreq);
    if( ret != LIMIC_TX_DONE ){
        printf("LiMIC: (limic_tx_init) LIMIC_TX fail\n");
        return 0;
    }

    return len;
}


static int limic_rx_comp( int fd, void *buf, int len, limic_user *lu )
{
    int ret;
    limic_request rreq;

    rreq.buf = buf;
    rreq.len = len;
    rreq.lu = lu;

    ret = ioctl(fd, LIMIC_RX, &rreq);
    if( ret != LIMIC_RX_DONE ){
        printf("LiMIC: (limic_rx_comp) LIMIC_RX fail\n");
        return 0;
    }

    return lu->length;
}

#endif
