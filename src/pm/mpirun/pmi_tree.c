/* Copyright (c) 2002-2010, The Ohio State University. All rights
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
#include "pmi_tree.h"
#include "mpispawn_tree.h"
#include "mpirun_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sendfile.h>


extern int mt_id;
extern process_info_t *local_processes;
extern fd_set child_socks;
extern child_t *children;
extern int NCHILD;
extern int NCHILD_INCL;
extern int N;
extern int MPISPAWN_HAS_PARENT;
extern int MPISPAWN_NCHILD;
extern int *mpispawn_fds;

/* list of pending requests that we've sent elsewhere. Change to a hash table 
 * when needed */

req_list_t *pending_req_head = NULL;
req_list_t *pending_req_tail = NULL;

kv_cache_t *kv_cache[KVC_HASH_SIZE];
kv_cache_t *kv_pending_puts;

static int npending_puts;

char *handle_spawn_request (int fd, char *buf, int buflen);

int get_req_dest (int req_rank, char **key)
{
	req_list_t *iter = pending_req_head;
	int ret_fd;

	while (iter != NULL) {
		if (iter->req_rank == req_rank) {
			ret_fd = iter->req_src_fd;

			if (iter->req_prev)
				iter->req_prev->req_next = iter->req_next;
			else
				pending_req_head = iter->req_next;
			if (iter->req_next)
				iter->req_next->req_prev = iter->req_prev;
			else
				pending_req_tail = iter->req_prev;
			if (iter->req_key) {
				*key = iter->req_key;
			}
			free (iter);
			return ret_fd;
		}
		iter = iter->req_next;
	}
	mpispawn_abort (ERR_REQ);
	return -1;
}

int save_pending_req (int req_rank, char *req_key, int req_fd)
{
	req_list_t *preq = (req_list_t *) malloc (req_list_s);

	if (!preq) {
		mpispawn_abort (ERR_MEM);
		return -1;
	}
	if (req_key) {
		preq->req_key =
			(char *) malloc ((strlen (req_key) + 1) * sizeof (char));
		if (!preq->req_key) {
			mpispawn_abort (ERR_MEM);
			return -1;
		}
		strcpy (preq->req_key, req_key);
		preq->req_key[strlen (req_key)] = 0;
	} else
		preq->req_key = NULL;
	preq->req_rank = req_rank;
	preq->req_src_fd = req_fd;
	preq->req_prev = pending_req_tail;
	if (pending_req_tail != NULL)
		pending_req_tail->req_next = preq;
	pending_req_tail = preq;

	preq->req_next = NULL;
	if (pending_req_head == NULL)
		pending_req_head = preq;

	return 0;
}

unsigned int kvc_hash (char *s)
{

	unsigned int hash = 0;
	while (*s)
		hash ^= *s++;
	return hash & KVC_HASH_MASK;
}

void delete_kvc(char *key) 
{
    kv_cache_t *iter, *prev;
    unsigned int hash = kvc_hash(key);

    prev = iter = kv_cache[hash];

    while(NULL != iter) {
        if(!strcmp(iter->kvc_key, key)) {
            if(iter == kv_cache[hash]) {
                kv_cache[hash] = iter->kvc_hash_next;
            } else {
                prev->kvc_hash_next = iter->kvc_hash_next;
            }
            free(iter->kvc_val);
            free(iter->kvc_key);
            free(iter);
            return;
        }
        prev = iter;
        iter = iter->kvc_hash_next;
    }
}

int add_kvc (char *key, char *val, int from_parent)
{
	kv_cache_t *pkvc;
	unsigned int hash = kvc_hash (key);
	pkvc = (kv_cache_t *) malloc (kv_cache_s);
	if (!pkvc) {
		mpispawn_abort (ERR_MEM);
		return -1;
	}

	pkvc->kvc_key = (char *) malloc ((strlen (key) + 1) * sizeof (char));
	pkvc->kvc_val = (char *) malloc ((strlen (val) + 1) * sizeof (char));;
	if (!pkvc->kvc_key || !pkvc->kvc_val) {
		mpispawn_abort (ERR_MEM);
		return -1;
	}
	strcpy (pkvc->kvc_key, key);
	strcpy (pkvc->kvc_val, val);
	if (val[strlen (val) - 1] == '\n')
		pkvc->kvc_val[strlen (val) - 1] = 0;
	pkvc->kvc_val[strlen (val)] = 0;
	pkvc->kvc_key[strlen (key)] = 0;
	pkvc->kvc_hash_next = NULL;

	kv_cache_t *iter = kv_cache[hash];

	if (NULL == iter) {
		kv_cache[hash] = pkvc;
	} else {
                pkvc->kvc_hash_next = kv_cache[hash];
                kv_cache[hash]      = pkvc;
	}
	if (!from_parent) {
		pkvc->kvc_list_next = kv_pending_puts;
		kv_pending_puts = pkvc;
		npending_puts++;
	}
	return 0;
}

char *check_kvc (char *key)
{
	kv_cache_t *iter;
	unsigned int hash = kvc_hash (key);

	iter = kv_cache[hash];

	while (NULL != iter) {
		if (!strcmp (iter->kvc_key, key)) {
			return iter->kvc_val;
		}
		iter = iter->kvc_hash_next;
	}
	return NULL;
}

int clear_kvc (void)
{
	int i;
	kv_cache_t *iter, *tmp;

	for (i = 0; i < KVC_HASH_SIZE; i++) {
		iter = kv_cache[i];
		while (iter) {
			tmp = iter;
			iter = iter->kvc_hash_next;
			free (tmp->kvc_key);
			free (tmp->kvc_val);
			free (tmp);
		}
		kv_cache[1] = 0;
	}
	return 0;
}

int writeline (int fd, char *msg, int msglen)
{
	int n;
	MT_ASSERT (msg[msglen - 1] == '\n');

	do {
		n = write (fd, msg, msglen);
	} while (n == -1 && errno == EINTR);

	if (n < 0 || n < msglen) {
		perror ("writeline");
		mpispawn_abort (ERR_WRITELINE);
	}
	return n;
}

int read_size (int fd, void *msg, int size)
{
	int n = 0, rc;
	char *offset = (char *) msg;

	while (n < size) {
		rc = read (fd, offset, size - n);

		if (rc < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return rc;
		} else if (0 == rc)
			return n;

		offset += rc;
		n += rc;
	}
	return n;
}

int write_size (int fd, void *msg, int size)
{
	int rc, n = 0;
	char *offset = (char *) msg;

	while (n < size) {
		rc = write (fd, offset, size - n);

		if (rc < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return rc;
		} else if (0 == rc)
			return n;

		offset += rc;
		n += rc;
	}
	return n;
}

int readline (int fd, char *msg, int maxlen)
{
	int n;
	MT_ASSERT (maxlen == MAXLINE);

	do {
		n = read (fd, msg, maxlen);
	} while (n == -1 && errno == EINTR);

	if (n < 0) {
		perror ("readline");
		mpispawn_abort (ERR_READLINE);
	}
	if (n < MAXLINE) {
		msg[n] = '\0';
	}

	MT_ASSERT (n <= MAXLINE);
	MT_ASSERT (msg[n - 1] == '\n');
	return n;
}

/* send_parent
 * src: -1 Propagate put message 
 *       n Propagate get request from rank n */

int send_parent (int src, char *msg, int msg_len)
{
	msg_hdr_t hdr = { src, msg_len, -1 };
	write (MPISPAWN_PARENT_FD, &hdr, msg_hdr_s);	// new
	writeline (MPISPAWN_PARENT_FD, msg, msg_len);
	return 0;
}

#define CHECK(s1, s2, dst) if (strcmp(s1, s2) == 0) { \
    MT_ASSERT(end-start); \
    dst = (char *) malloc (sizeof (char) * (end-start + 1)); \
    if (!dst) { \
        rv = ERR_MEM; \
        goto exit_err; \
    } \
    strncpy (dst, start, end-start); \
    dst[end-start] = 0; \
}

int check_pending_puts (void)
{
	kv_cache_t *iter, *tmp;
	msg_hdr_t hdr = { -1, -1, MT_MSG_BPUTS };
	char *buf, *pbuf;
	int i;

	if (npending_puts != NCHILD + NCHILD_INCL)
		return 0;

#define REC_SIZE (KVS_MAX_KEY + KVS_MAX_VAL + 2)
	hdr.msg_len = REC_SIZE * npending_puts + 1;
	buf = (char *) malloc (hdr.msg_len * sizeof (char));
	pbuf = buf;
	iter = kv_pending_puts;
	while (iter) {
		snprintf (pbuf, KVS_MAX_KEY, "%s", iter->kvc_key);
		pbuf[KVS_MAX_KEY] = 0;
		pbuf += KVS_MAX_KEY + 1;
		snprintf (pbuf, KVS_MAX_VAL, "%s", iter->kvc_val);
		pbuf[KVS_MAX_VAL] = 0;
		pbuf += KVS_MAX_VAL + 1;

		tmp = iter->kvc_list_next;
		iter->kvc_list_next = NULL;
		iter = tmp;

		npending_puts--;
	}
	MT_ASSERT (npending_puts == 0);
	kv_pending_puts = NULL;
#undef REC_SIZE

	buf[hdr.msg_len - 1] = '\n';
	if (MPISPAWN_HAS_PARENT) {
		write (MPISPAWN_PARENT_FD, &hdr, msg_hdr_s);
		write_size (MPISPAWN_PARENT_FD, buf, hdr.msg_len);
	} else {
		/* If I'm root, send it down the tree */
		for (i = 0; i < MPISPAWN_NCHILD; i++) {
			write (MPISPAWN_CHILD_FDS[i], &hdr, msg_hdr_s);
			write_size (MPISPAWN_CHILD_FDS[i], buf, hdr.msg_len);
		}
	}
	free (buf);
	return 0;
}

static char *resbuf = "cmd=spawn_result rc=0\n";
int parse_str (int rank, int fd, char *msg, int msg_len, int src)
{
	static int barrier_count;
	int rv = 0, i;
	char *p = msg, *start = NULL, *end = NULL;
	char *command = NULL, *key = NULL, *val = NULL, *pmi_version = NULL,
		*pmi_subversion = NULL, *kvsname = NULL, *rc = NULL, *pmiid = NULL;
	char name[KVS_MAX_NAME];
	char resp[MAXLINE];
	char *kvstmplate;

	msg_hdr_t hdr;

	if (!p)
		return -1;

	start = p;
	while (*p != '\n') {
		if (*p == '=') {
			end = p;
			strncpy (name, start, end - start);
			name[end - start] = 0;
			p++;
			start = p;
			while (*p != ' ' && *p != '\n') {
				p++;
			}
			end = p;
			switch (strlen (name)) {
			case 3:			/* cmd, key */
				CHECK (name, "cmd", command)
					else
				CHECK (name, "key", key)
					break;
			case 4:
				CHECK (name, "mcmd", command)
                    else
                CHECK (name, "port", val)        
				break;
			case 7:			/* kvsname */
				CHECK (name, "kvsname", kvsname)
                    else
                CHECK (name, "service", key)    
                    break;
			case 5:			/* value, pmiid */
				CHECK (name, "value", val)
					else
				CHECK (name, "pmiid", pmiid)
					break;
			case 11:			/* pmi_version */
				CHECK (name, "pmi_version", pmi_version)
					break;
			case 14:			/* pmi_subversion */
				CHECK (name, "pmi_subversion", pmi_subversion)
					break;
			case 2:			/* rc */
				CHECK (name, "rc", rc)
					if (0 == strcmp (name, "rc")) {
					if (strcmp (rc, "0")) {
						rv = ERR_RC;
						goto exit_err;
					}
				}
				break;
			default:
				rv = ERR_STR;
				break;
			}
			if (*p != '\n') {
				start = ++p;
			}
		}
		if (*p != '\n')
			p++;
	}
	switch (strlen (command)) {
	case 3:					/* get, put */
		if (0 == strcmp (command, "get")) {
			char *kvc_val = check_kvc (key);
			hdr.msg_rank = rank;
			if (kvc_val) {
				sprintf (resp, "cmd=get_result rc=0 value=%s\n", kvc_val);
				hdr.msg_len = strlen (resp);
				if (src == MT_CHILD) {
					write (fd, &hdr, msg_hdr_s);
				}
				writeline (fd, resp, hdr.msg_len);
			} else {
				MT_ASSERT (0);
				/* add pending req */
				save_pending_req (rank, key, fd);
				/* send req to parent */
				send_parent (rank, msg, msg_len);
			}
		}
		/* cmd=put */
		else if (0 == strcmp (command, "put")) {
			hdr.msg_rank = rank;
			hdr.msg_len = msg_len;
			add_kvc (key, val, 0);
			check_pending_puts ();
			if (src == MT_RANK) {
				sprintf (resp, "cmd=put_result rc=0\n");
				writeline (fd, resp, strlen (resp));
			}
		} else
			goto invalid_cmd;
		break;
	case 4:					/* init */
		if (0 == strcmp (command, "init")) {
			if (pmi_version[0] == PMI_VERSION && pmi_version[1] == '\0' &&
				pmi_subversion[0] == PMI_SUBVERSION &&
				pmi_subversion[1] == '\0') {
				sprintf (resp, "cmd=response_to_init pmi_version=%c "
						 "pmi_subversion=%c rc=0\n", PMI_VERSION,
						 PMI_SUBVERSION);
				writeline (fd, resp, strlen (resp));
			} else {
				sprintf (resp, "cmd=response_to_init pmi_version=%c "
						 "pmi_subversion=%c rc=1\n", PMI_VERSION,
						 PMI_SUBVERSION);
				writeline (fd, resp, strlen (resp));
			}
		} else
			goto invalid_cmd;
		break;
	case 5:					/* spawn */
		if (0 == strcmp (command, "spawn")) {
			handle_spawn_request (fd, msg, msg_len);
			/* send response to spawn request */
			write (fd, resbuf, strlen (resbuf));
		}
		break;
	case 7:					/* initack */
		if (0 == strcmp (command, "initack")) {
			for (i = 0; i < NCHILD; i++) {
				if (children[i].fd == fd) {
					children[i].rank = atoi (pmiid);
					/* TD validate rank */
					goto initack;
				}
			}
			if (i == NCHILD) {
				rv = ERR_DEF;
				goto exit_err;
			}
		  initack:
			sprintf (resp, "cmd=initack rc=0\ncmd=set size=%d\n"
					 "cmd=set rank=%d\ncmd=set debug=0\n", N,
					 children[i].rank);
			writeline (fd, resp, strlen (resp));
		}
		break;
	case 8:					/* finalize */
		if (0 == strcmp (command, "finalize")) {
			barrier_count++;
			if (barrier_count == (NCHILD + MPISPAWN_NCHILD)) {
				if (MPISPAWN_HAS_PARENT)
					send_parent (rank, msg, msg_len);
				else {
					goto finalize_ack;
				}
			}
		} else
			goto invalid_cmd;
		break;
	case 9:					/* get_maxes */
		if (0 == strcmp (command, "get_maxes")) {
			sprintf (resp, "cmd=maxes kvsname_max=%d keylen_max=%d "
					 "vallen_max=%d\n", KVS_MAX_NAME, KVS_MAX_KEY,
					 KVS_MAX_VAL);
			writeline (fd, resp, strlen (resp));
		} else
			goto invalid_cmd;
		break;
	case 10:					/* get_appnum, get_result, put_result, barrier_in */
		if (0 == strcmp (command, "get_result")) {
			char *pkey;
			int child_fd;
			hdr.msg_rank = rank;
			hdr.msg_len = msg_len;
			child_fd = get_req_dest (rank, &pkey);
			add_kvc (pkey, val, 0);
			free (pkey);
			for (i = 0; i < MPISPAWN_NCHILD; i++) {
				if (child_fd == MPISPAWN_CHILD_FDS[i]) {
					write (child_fd, &hdr, msg_hdr_s);
				}
			}
			writeline (child_fd, msg, msg_len);
		} else if (0 == strcmp (command, "get_appnum")) {
            char *val;
            int multi, respval = 0;
            val = getenv("MPIRUN_COMM_MULTIPLE");
            if(val) {
                multi = atoi(val);
                if(multi)
                    respval = children[0].rank;
            }
			sprintf (resp, "cmd=appnum appnum=%d\n", respval);
			writeline (fd, resp, strlen (resp));
		} else if (0 == strcmp (command, "barrier_in")) {
			barrier_count++;
			if (barrier_count == (NCHILD + MPISPAWN_NCHILD)) {
				if (MPISPAWN_HAS_PARENT) {
					/* msg_type */
					send_parent (rank, msg, msg_len);
				} else {
					goto barrier_out;
				}
			}
		} else
			goto invalid_cmd;
		break;
	case 11:
		if (0 == strcmp (command, "barrier_out")) {
		  barrier_out:
			{
				sprintf (resp, "cmd=barrier_out\n");
				hdr.msg_rank = -1;
				hdr.msg_len = strlen (resp);
				hdr.msg_type = MT_MSG_BOUT;
				for (i = 0; i < MPISPAWN_NCHILD; i++) {
					write (MPISPAWN_CHILD_FDS[i], &hdr, msg_hdr_s);
					writeline (MPISPAWN_CHILD_FDS[i], resp, hdr.msg_len);
				}
				for (i = 0; i < NCHILD; i++) {
					writeline (children[i].fd, resp, hdr.msg_len);
				}
				barrier_count = 0;
				goto ret;
			}
		} else if(0 == strcmp(command, "lookup_name")) {
            char *valptr;
            valptr = check_kvc(key);
            if(valptr) {
                sprintf(resp, "cmd=lookup_result info=ok port=%s\n", valptr);
                hdr.msg_len = strlen(resp);
                hdr.msg_rank = rank;
                if(src == MT_CHILD) {
                    write(fd, &hdr, msg_hdr_s);
                }
                writeline (fd, resp, strlen(resp));
            } else {
                if(MPISPAWN_HAS_PARENT) {
                    save_pending_req(rank, key, fd);
                    send_parent(rank, msg, msg_len);
                } else {
                    sprintf(resp, "cmd=lookup_result info=notok\n");
                    hdr.msg_len = strlen(resp);
                    hdr.msg_rank = rank;
                    if(src == MT_CHILD) {
                        write(fd, &hdr, msg_hdr_s);
                    }
                    writeline (fd, resp, strlen(resp));
                }
            }
            goto ret;
        } else 
			goto invalid_cmd;
		break;
	case 12:					/* finalize_ack */
		if (0 == strcmp (command, "finalize_ack")) {
			close (MPISPAWN_PARENT_FD);
		  finalize_ack:
			{
				hdr.msg_rank = -1;
				hdr.msg_type = MT_MSG_FACK;
				sprintf (resp, "cmd=finalize_ack\n");
				hdr.msg_len = strlen (resp);
				for (i = 0; i < MPISPAWN_NCHILD; i++) {
					write (MPISPAWN_CHILD_FDS[i], &hdr, msg_hdr_s);
					writeline (MPISPAWN_CHILD_FDS[i], resp, hdr.msg_len);
					close (MPISPAWN_CHILD_FDS[i]);
				}
				for (i = 0; i < NCHILD; i++) {
					writeline (children[i].fd, resp, hdr.msg_len);
					close (children[i].fd);
				}
				barrier_count = 0;
				rv = 1;
				clear_kvc ();
				goto ret;
			}
		} else if(0 == strcmp(command, "publish_name")) {
            add_kvc(key, val, 1);
            if(MPISPAWN_HAS_PARENT) {
                save_pending_req(rank, key, fd);    
                send_parent(rank, msg, msg_len);
            } else {
                sprintf(resp, "cmd=publish_result info=ok\n");
                writeline (fd, resp, strlen(resp));
            }
            goto ret;
        } else
			goto invalid_cmd;
		break;
    case 13:
        if(0 == strcmp(command, "lookup_result")) {
            char *pkey;
            int tfd;
            hdr.msg_rank = rank;
            hdr.msg_len = msg_len;
            tfd = get_req_dest(rank, &pkey);
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
				if (tfd == MPISPAWN_CHILD_FDS[i]) {
					write (tfd, &hdr, msg_hdr_s);
				}
			}
			writeline (tfd, msg, msg_len);
            free(pkey);
            goto ret;
        } else 
            goto invalid_cmd;
	case 14:					/* get_my_kvsname */
		if (0 == strcmp (command, "get_my_kvsname")) {
			kvstmplate = getenv ("MPDMAN_KVS_TEMPLATE");
			if (kvstmplate)
				sprintf (resp, "cmd=my_kvsname kvsname=%s_0\n",
						 kvstmplate);
			else
				sprintf (resp, "cmd=my_kvsname kvsname=kvs_0\n");
			writeline (fd, resp, strlen (resp));
		} else if(0 == strcmp(command, "publish_result")) {
            int tfd;
            char *pkey;
            tfd = get_req_dest(rank, &pkey);
            hdr.msg_rank = rank;
            hdr.msg_len = msg_len;
            free(pkey);
            for (i = 0; i < MPISPAWN_NCHILD; i++) {
				if (tfd == MPISPAWN_CHILD_FDS[i]) {
					write (tfd, &hdr, msg_hdr_s);
				}
			}
            writeline(tfd, msg, msg_len);
            goto ret;
        } else if(0 == strcmp(command, "unpublish_name")) {
            delete_kvc(key);
            if(MPISPAWN_HAS_PARENT) {
                save_pending_req(rank, key, fd);
                send_parent(rank, msg, msg_len);
            } else {
                sprintf(resp, "cmd=unpublish_result info=ok\n");
                writeline(fd, resp, strlen(resp));
            }
            goto ret;
        } else
			goto invalid_cmd;
		break;
    case 16:
        if(0 == strcmp (command, "unpublish_result")) {
            char *pkey;
            int tfd;
            hdr.msg_rank = rank;
            hdr.msg_len = msg_len;
            tfd = get_req_dest(rank, &pkey);
            for(i = 0; i < MPISPAWN_NCHILD; i++) {
                if(tfd == MPISPAWN_CHILD_FDS[i]) {
                    write(tfd, &hdr, msg_hdr_s);
                }
            }
            writeline(tfd, msg, msg_len);
            free(pkey);
            goto ret;           
        } else 
            goto invalid_cmd;    
	case 17:					/* get_universe_size */
		if (0 == strcmp (command, "get_universe_size")) {
			sprintf (resp, "cmd=universe_size size=%d rc=0\n", N);
			writeline (fd, resp, strlen (resp));
		} else
			goto invalid_cmd;
	}
	goto ret;
  invalid_cmd:
    printf("invalid %s\n", msg);
	rv = ERR_CMD;
  exit_err:
	mpispawn_abort (rv);
  ret:
	if (command != NULL)
		free (command);
	if (key != NULL)
		free (key);
	if (val != NULL)
		free (val);
	if (pmi_version != NULL)
		free (pmi_version);
	if (pmi_subversion != NULL)
		free (pmi_subversion);
	if (kvsname != NULL)
		free (kvsname);
	if (rc != NULL)
		free (rc);
	if (pmiid != NULL)
		free (pmiid);
	return rv;
}

int handle_mt_peer (int fd, msg_hdr_t * phdr)
{
	int rv = -1, n, i;
	char *buf = (char *) malloc (phdr->msg_len * sizeof (char));
	char *pkey, *pval;
	if (phdr->msg_type == MT_MSG_BPUTS) {
#define REC_SIZE (KVS_MAX_VAL + KVS_MAX_KEY + 2)
		if (read_size (fd, buf, phdr->msg_len) > 0) {
			if (MPISPAWN_HAS_PARENT && fd == MPISPAWN_PARENT_FD) {
				for (i = 0; i < MPISPAWN_NCHILD; i++) {
					write (MPISPAWN_CHILD_FDS[i], phdr, msg_hdr_s);
					write_size (MPISPAWN_CHILD_FDS[i], buf, phdr->msg_len);
				}
			}
			n = (phdr->msg_len - 1) / REC_SIZE;
			for (i = 0; i < n; i++) {
				pkey = buf + i * REC_SIZE;
				pval = pkey + KVS_MAX_KEY + 1;
				add_kvc (pkey, pval,
						 (MPISPAWN_HAS_PARENT
						  && fd == MPISPAWN_PARENT_FD));
			}
			rv = 0;
		}
#undef REC_SIZE
		check_pending_puts ();
	} else if (read_size (fd, buf, phdr->msg_len) > 0)
		rv = parse_str (phdr->msg_rank, fd, buf, phdr->msg_len, MT_CHILD);
	free (buf);
	return rv;
}

extern int mpirun_socket;
int mtpmi_init (void)
{
	int i, nchild_subtree = 0, tmp;
	int *children_subtree =
		(int *) malloc (sizeof (int) * MPISPAWN_NCHILD);

	for (i = 0; i < MPISPAWN_NCHILD; i++) {
		read (MPISPAWN_CHILD_FDS[i], &tmp, sizeof (int));
		children_subtree[i] = tmp;
		nchild_subtree += tmp;
	}
	NCHILD_INCL = nchild_subtree;
	nchild_subtree += NCHILD;
	if (MPISPAWN_HAS_PARENT)
		write (MPISPAWN_PARENT_FD, &nchild_subtree, sizeof (int));

	if (env2int ("MPISPAWN_USE_TOTALVIEW") == 1) {
		process_info_t *all_pinfo;
		int iter = 0;
		if (MPISPAWN_NCHILD) {
			all_pinfo = (process_info_t *) malloc
				(process_info_s * nchild_subtree);
			if (!all_pinfo) {
				mpispawn_abort (ERR_MEM);
			}
			/* Read pid table from child MPISPAWNs */
			for (i = 0; i < MPISPAWN_NCHILD; i++) {
				read_socket (MPISPAWN_CHILD_FDS[i], &all_pinfo[iter],
							 children_subtree[i] * process_info_s);
				iter += children_subtree[i];
			}
			for (i = 0; i < NCHILD; i++, iter++) {
				all_pinfo[iter].rank = local_processes[i].rank;
				all_pinfo[iter].pid = local_processes[i].pid;
			}
		} else {
			all_pinfo = local_processes;
		}

		if (MPISPAWN_HAS_PARENT) {
			write_socket (MPISPAWN_PARENT_FD, all_pinfo,
						  nchild_subtree * process_info_s);
		} else if (mt_id == 0) {
			/* Send to mpirun_rsh */
			write_socket (mpirun_socket, all_pinfo,
						  nchild_subtree * process_info_s);
			/* Wait for Totalview to be ready */
			read_socket (mpirun_socket, &tmp, sizeof (int));
			close (mpirun_socket);
		}
		/* Barrier */
		if (MPISPAWN_HAS_PARENT) {
			read_socket (MPISPAWN_PARENT_FD, &tmp, sizeof (int));
		}
		if (MPISPAWN_NCHILD) {
			for (i = 0; i < MPISPAWN_NCHILD; i++) {
				write_socket (MPISPAWN_CHILD_FDS[i], &tmp, sizeof (int));
			}
		}
	}
	return 0;
}

int mtpmi_processops (void)
{
	int ready, i, rv = 0;
	char buf[MAXLINE];
	msg_hdr_t hdr;

	while (rv == 0) {
		if (MPISPAWN_HAS_PARENT)
			FD_SET (MPISPAWN_PARENT_FD, &child_socks);
		for (i = 0; i < MPISPAWN_NCHILD; i++) {
			FD_SET (MPISPAWN_CHILD_FDS[i], &child_socks);
		}
		for (i = 0; i < NCHILD; i++) {
			FD_SET (children[i].fd, &child_socks);
		}

		ready = select (FD_SETSIZE, &child_socks, NULL, NULL, NULL);

		if (ready < 0) {
			perror ("select");
			mpispawn_abort (ERR_DEF);
		}
		if (MPISPAWN_HAS_PARENT && FD_ISSET (MPISPAWN_PARENT_FD,
											 &child_socks)) {
			ready--;
			read (MPISPAWN_PARENT_FD, &hdr, msg_hdr_s);
			rv = handle_mt_peer (MPISPAWN_PARENT_FD, &hdr);
		}
		for (i = 0; rv == 0 && ready > 0 && i < MPISPAWN_NCHILD; i++) {
			if (FD_ISSET (MPISPAWN_CHILD_FDS[i], &child_socks)) {
				ready--;
				read (MPISPAWN_CHILD_FDS[i], &hdr, msg_hdr_s);
				rv = handle_mt_peer (MPISPAWN_CHILD_FDS[i], &hdr);
			}
		}
		for (i = 0; 0 == rv && ready > 0 && i < NCHILD; i++) {
			if (FD_ISSET (children[i].fd, &child_socks)) {
				ready--;
				if (readline (children[i].fd, buf, MAXLINE) > 0)
					rv = parse_str (children[i].rank, children[i].fd, buf,
									strlen (buf), MT_RANK);
				else
					rv = -1;
			}
		}
	}
	return 0;
}

/* send the spawn request fully to the mpirun on the root node.
   if the spawn succeeds the mpirun will send a status. Form a response and
   send back to the waiting PMIU_readline */

char *handle_spawn_request (int fd, char *buf, int buflen)
{
	int sock;
	int event = MPISPAWN_DPM_REQ, id = env2int ("MPISPAWN_ID");
	FILE *fp;
	struct stat statb;

	sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		/* Oops! */
		perror ("socket");
		exit (EXIT_FAILURE);
	}

	struct sockaddr_in sockaddr;
	struct hostent *mpirun_hostent;
	int spcnt, j, size;
	uint32_t totsp, retval;
	char *fname;
	mpirun_hostent = gethostbyname (env2str ("MPISPAWN_MPIRUN_HOST"));
	if (NULL == mpirun_hostent) {
		/* Oops! */
		herror ("gethostbyname");
		exit (EXIT_FAILURE);
	}

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr = *(struct in_addr *) (*mpirun_hostent->h_addr_list);
	sockaddr.sin_port = htons (env2int ("MPISPAWN_CHECKIN_PORT"));

	while (connect (sock, (struct sockaddr *) &sockaddr,
					sizeof (sockaddr)) < 0);
	if (!sock) {
		perror ("connect");
		exit (EXIT_FAILURE);
	}
	/* now mpirun_rsh is waiting for the spawn request to be sent,
	   read MAXLINE at a time and pump it to mpirun. */
	read (fd, &spcnt, sizeof (uint32_t));
	/* read in spawn cnt */
/*    fprintf(stderr, "spawn count = %d\n", spcnt); */
	read (fd, &totsp, sizeof (uint32_t));
/*    fprintf(stderr, "total spawn datasets = %d\n", totsp); */

	sprintf (buf, "/tmp/tempfile_socket_dump.%d", getpid ());
	fname = strdup (buf);
	fp = fopen (fname, "w");
	for (j = 0; j < spcnt; j++) {
		read (fd, &size, sizeof (int));
/*        fprintf(stderr, "size of message = %d\n", size); */
		buflen = read (fd, buf, size);
		write (fileno (fp), buf, buflen);
	}
	fclose (fp);
	/* now we're connected to mpirun on root node */
	write (sock, &event, sizeof (int));
	write (sock, &id, sizeof (int));
	fp = fopen (fname, "r");
	fstat (fileno (fp), &statb);
	retval = statb.st_size;
	write (sock, &totsp, sizeof (uint32_t));
	write (sock, &retval, sizeof (uint32_t));

	sendfile (sock, fileno (fp), 0, retval);
	fclose (fp);
	unlink (fname);
	return NULL;
}
