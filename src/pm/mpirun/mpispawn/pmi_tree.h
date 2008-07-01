/* Copyright (c) 2002-2008, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */
#ifndef __PMI_TREE_H
#define __PMI_TREE_H

#define ERR_MEM         -1
#define ERR_REQ         -2
#define ERR_CMD         -3
#define ERR_RC          -4
#define ERR_STR         -5
#define ERR_WRITELINE   -6
#define ERR_READLINE    -7
#define ERR_DEF         -100 

#define MT_RANK     0
#define MT_CHILD    1
#define MT_PARENT   2

#define MAXLINE         1024    // PMIU_MAXLINE == 1024
#define PMI_VERSION     '1'
#define PMI_SUBVERSION  '1'

#define KVS_MAX_NAME    64
#define KVS_MAX_KEY     64
#define KVS_MAX_VAL     64

#define MT_MSG_UNDEF        0       /* Undefined */
#define MT_MSG_BPUTS        1       /* Bunched puts */
#define MT_MSG_GET          2       /* Get msg, shouldn't be used */
#define MT_MSG_BIN          3       /* barrier_in */
#define MT_MSG_BOUT         4       /* barrier_out */
#define MT_MSG_FIN          5       /* finalize */
#define MT_MSG_FACK         6       /* finalize_ack */

#ifdef DBG
#define MSG(args...) do { \
    fprintf (stderr, "MPISPAWN_%d: ", mt_id); \
    fprintf (stderr, ## args); \
    fflush (stderr); \
} while (0);
#else
#define MSG(...) 
#endif

#define MPISPAWN_PARENT_FD mpispawn_fds[0]
#define MPISPAWN_CHILD_FDS (&mpispawn_fds[MPISPAWN_HAS_PARENT])

typedef struct _req_list {
    struct _req_list *req_next;
    struct _req_list *req_prev;
  
    int     req_rank;
    char    *req_key;
    int     req_src_fd;
} req_list_t;
#define req_list_s (sizeof (req_list_t))

typedef struct _msg_hdr {
    int msg_rank;
    int msg_len;
    int msg_type;
} msg_hdr_t;
#define msg_hdr_s (sizeof (msg_hdr_t))

typedef struct _kv_cache{
    struct _kv_cache *kvc_hash_next;
    struct _kv_cache *kvc_list_next;
    char *kvc_key;
    char *kvc_val;
} kv_cache_t;
#define kv_cache_s sizeof (kv_cache_t)

#define KVC_HASH_SIZE 32
#define KVC_HASH_MASK (KVC_HASH_SIZE - 1)


int get_req_dest (int req_rank, char **key);
int save_pending_req (int req_rank, char *req_key, int req_fd);
unsigned int kvc_hash (char *s);
int add_kvc (char *key, char *val, int from_parent);
char *check_kvc (char *key);
int clear_kvc (void);
int writeline (int fd, char *msg, int msglen);
int read_size (int fd, void *msg, int size);
int write_size (int fd, void *msg, int size);
int readline (int fd, char *msg, int maxlen);
int send_parent (int src, char *msg, int msg_len);
int check_pending_puts (void);
int parse_str (int rank, int fd, char *msg, int msg_len, int src);
int handle_mt_peer (int fd, msg_hdr_t *phdr);
int mtpmi_init (void);
int mtpmi_processops (void);

#endif
