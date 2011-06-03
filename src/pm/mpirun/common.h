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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <sys/time.h>

//#include <locale.h>
//#include "list.h"

//#define unsigned long     unsigned long
//#define unsigned int      unsigned int
#define MAX_FILENAME_LENGTH    (64)
typedef struct RDMA_info {
    ///// client fills this, srv performs RR on this buf
    unsigned long raddr;        // remote addr on client, to be read by server
    unsigned int rkey;          // rkey of the raddr
    unsigned int rbuf_id;       // id of remote buffer, on client

    unsigned long size;         // size of RR data
    unsigned long offset;       // the data's offset in original file

    unsigned int is_last_chunk; // is this chunk the last of this ckpt-file?

    unsigned long rarg1;        // a general arg used by client.

    unsigned int rprocid;       // proc who gen this ckpt, not used...
    unsigned int rckptid;       // ckpt-id for this chunk, not used...

    char filename[MAX_FILENAME_LENGTH]; // ckpt-filename
    int namelen;                // filename length

    ////////////////////////////        
    ///// server fills this, and uses it at poll_cq
    unsigned int lbuf_id;       // id of local RDMA-buf used by server            
    unsigned long laddr;        // local-buf addr, where the RR data is stored

    unsigned long larg1;        // generic arg 1, server uses this at callback

} __attribute__ ((packed)) RDMA_info_t; // size=136

/*
A packet of ibv_send() and ibv_recv()
*/
typedef struct ib_packet {
    unsigned int command;

    union {
        // in RR, client sends a request to srv(with addr, rkey, pid)
        // server performs the RR
        RDMA_info_t RR;

        unsigned char dummy[60];
    };

} __attribute__ ((packed)) ib_packet_t; // total size=140

/*
A reply pkt
*/
typedef struct reply_pkt {
    int type;
    unsigned char dummy[64 - 4];

} __attribute__ ((packed)) reply_pkt_t;

typedef struct cmd_arg_s {
    int argc;
    char **argv;
//  sem_t   sem;
} cmd_arg_t;

#endif