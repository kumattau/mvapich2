/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2006 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "newtcp_module_impl.h"

/* MPID_nem_newtcp_module_get_conninfo -- This function takes a VC
   pointer as input and outputs the sockaddr, pg_id, and pg_rank of
   the remote process associated with this VC.  [NOTE: I'm not sure
   yet, if the pg_id parameters will be char* or char**.  I'd like to
   avoid a copy on this.] */
#undef FUNCNAME
#define FUNCNAME MPID_nem_newtcp_module_get_conninfo
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_newtcp_module_get_conninfo (struct MPIDI_VC *vc, struct sockaddr_in *addr, char **pg_id, int *pg_rank)
{
    int mpi_errno = MPI_SUCCESS;

    *addr = VC_FIELD(vc, sock_id);
    *pg_id = (char *)vc->pg->id;
    *pg_rank = vc->pg_rank;
    
    return mpi_errno;
}

/* MPID_nem_newtcp_module_get_vc_from_conninfo -- This function takes
   the pg_id and pg_rank and returns the corresponding VC. */
#undef FUNCNAME
#define FUNCNAME MPID_nem_newtcp_module_get_vc_from_conninfo
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_newtcp_module_get_vc_from_conninfo (char *pg_id, int pg_rank, struct MPIDI_VC **vc)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_PG_t *pg;
    
    mpi_errno = MPIDI_PG_Find (pg_id, &pg);
    if (mpi_errno) MPIU_ERR_POP (mpi_errno);
    MPIU_ERR_CHKANDJUMP1 (pg_rank < 0 || pg_rank > MPIDI_PG_Get_size (pg), mpi_errno, MPI_ERR_OTHER, "**intern", "**intern %s", "invalid pg_rank");
        
    MPIDI_PG_Get_vc (pg, pg_rank, vc);
    
 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME set_sockopts
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_newtcp_module_set_sockopts (int fd)
{
    int mpi_errno = MPI_SUCCESS;
    int option, flags;
    int ret;
    socklen_t len;

/*     fprintf(stdout, FCNAME " Enter\n"); fflush(stdout); */
    /* I heard you have to read the options after setting them in some implementations */

    option = 1;
    len = sizeof(int);
    ret = setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &option, len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    ret = getsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &option, &len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);

    option = 128*1024;
    len = sizeof(int);
    setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &option, len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    getsockopt (fd, SOL_SOCKET, SO_RCVBUF, &option, &len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    setsockopt (fd, SOL_SOCKET, SO_SNDBUF, &option, len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    getsockopt (fd, SOL_SOCKET, SO_SNDBUF, &option, &len);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    
    flags = fcntl(fd, F_GETFL, 0);
    MPIU_ERR_CHKANDJUMP2 (flags == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    ret = fcntl(fd, F_SETFL, flags | SO_REUSEADDR);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);
    
    flags = fcntl(fd, F_GETFL, 0);
    MPIU_ERR_CHKANDJUMP2 (flags == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);    
    ret = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    MPIU_ERR_CHKANDJUMP2 (ret == -1, mpi_errno, MPI_ERR_OTHER, "**fail", "**fail %s %d", strerror (errno), errno);    

 fn_exit:
/*     fprintf(stdout, FCNAME " Exit\n"); fflush(stdout); */
    return mpi_errno;
 fn_fail:
/*     fprintf(stdout, "failure. mpi_errno = %d\n", mpi_errno); */
    goto fn_exit;
}


/*
  MPID_NEM_NEWTCP_MODULE_SOCK_ERROR_EOF : connection failed
  MPID_NEM_NEWTCP_MODULE_SOCK_CONNECTED : socket connected (connection success)
  MPID_NEM_NEWTCP_MODULE_SOCK_NOEVENT   : No event on socket

N1: some implementations do not set POLLERR when there is a pending error on socket.
So, solution is to check for readability/writeablility and then call getsockopt.
Again, getsockopt behaves differently in different implementations which  is handled
safely here (per Stevens-Unix Network Programming)

N2: As far as the socket code is concerned, it doesn't really differentiate whether
there is an error in the socket or whether the peer has closed it (i.e we have received
EOF and hence recv returns 0). Either way, we deccide the socket fd is not usable any
more. So, same return code is used.
A design decision is not to write also, if the peer has closed the socket. Please note that
write will still be succesful, even if the peer has sent us FIN. Only the subsequent 
write will fail. So, this function is made tight enough and this should be called
before doing any read/write at least in the connection establishment state machine code.

N3: return code MPID_NEM_NEWTCP_MODULE_SOCK_NOEVENT is used only by the code that wants to
know whether the connect is still not complete after a non-blocking connect is issued.

TODO: Make this a macro for performance, if needed based on the usage.
FIXME : Above comments are inconsistent now with the changes. No check for EOF is 
actually done now in this function.
*/

#undef FUNCNAME
#define FUNCNAME MPID_nem_newtcp_module_check_sock_status
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
MPID_NEM_NEWTCP_MODULE_SOCK_STATUS_t 
MPID_nem_newtcp_module_check_sock_status(const pollfd_t *const plfd)
{
    int rc = MPID_NEM_NEWTCP_MODULE_SOCK_NOEVENT;

    if (plfd->revents & POLLERR) 
    {
        rc = MPID_NEM_NEWTCP_MODULE_SOCK_ERROR_EOF;
        MPIU_DBG_MSG_FMT(NEM_SOCK_DET, VERBOSE, (MPIU_DBG_FDEST, "POLLERR on socket"));
        goto fn_exit;
    }
    if (plfd->revents & POLLIN || plfd->revents & POLLOUT) 
    {
        int error=0;
        socklen_t n = sizeof(error);

        n = sizeof(error);
        if (getsockopt(plfd->fd, SOL_SOCKET, SO_ERROR, &error, &n) < 0 || error != 0) 
        {
            rc = MPID_NEM_NEWTCP_MODULE_SOCK_ERROR_EOF; /*  (N1) */
            MPIU_DBG_MSG_FMT(NEM_SOCK_DET, VERBOSE, (MPIU_DBG_FDEST, "getsockopt failure. error=%d:%s", error, strerror(error)));
            goto fn_exit;
        }
        rc = MPID_NEM_NEWTCP_MODULE_SOCK_CONNECTED;
    }
 fn_exit:
    return rc;
}

/*
 */
#undef FUNCNAME
#define FUNCNAME MPID_nem_newtcp_module_is_sock_connected
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_newtcp_module_is_sock_connected(int fd)
{
    int rc = FALSE;
    char buf[1];
    int buf_len = sizeof(buf)/sizeof(buf[0]), ret_recv, error=0;
    socklen_t n = sizeof(error);

    n = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n) < 0 || error != 0) 
    {
        MPIU_DBG_MSG_FMT(NEM_SOCK_DET, VERBOSE, (MPIU_DBG_FDEST, "getsockopt failure. error=%d:%s", error, strerror(error)));
        rc = FALSE; /*  error */
        goto fn_exit;
    }

    CHECK_EINTR(ret_recv, recv(fd, buf, buf_len, MSG_PEEK));
    if (ret_recv == 0)
        rc = FALSE;
    else
        rc = TRUE;
 fn_exit:
    return rc;
}
