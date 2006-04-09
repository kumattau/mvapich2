/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* FIXME: This could go into util/port as a general utility routine */
/* FIXME: This is needed/used only if dynamic processes are supported 
   (e.g., another reason to place it into util/port) */

#include "mpidi_ch3_impl.h"

/*
 * This file replaces ch3u_comm_connect.c and ch3u_comm_accept.c .  These
 * routines need to be used together, particularly since they must exchange
 * information.  In addition, many of the steps that the take are identical,
 * such as building the new process group information after a connection.
 * By having these routines in the same file, it is easier for them
 * to share internal routines and it is easier to ensure that communication
 * between the two root processes (the connector and acceptor) are
 * consistent.
 */

/* FIXME: If dynamic processes are not supported, this file will contain
   no code and some compilers may warn about an "empty translation unit */
#ifndef MPIDI_CH3_HAS_NO_DYNAMIC_PROCESS

typedef struct pg_translation {
    int pg_index;
    int pg_rank;
} pg_translation;

typedef struct pg_node {
    int index;
    char *pg_id;
    char *str;
    struct pg_node *next;
} pg_node;

/* These functions help implement the connect/accept algorithm */
static int ExtractLocalPGInfo( MPID_Comm *, pg_translation [], 
			       pg_node **, int * );
static int ReceivePGAndDistribute( MPID_Comm *, MPID_Comm *, int, int *,
				   int, MPIDI_PG_t *[] );
static int SendPGtoPeerAndFree( MPID_Comm *, int *, pg_node * );
static int FreeNewVC( MPIDI_VC_t *new_vc );
static int SetupNewIntercomm( MPID_Comm *comm_ptr, int remote_comm_size, 
			      pg_translation remote_translation[],
			      MPIDI_PG_t **remote_pg, 
			      MPID_Comm *intercomm );

#undef FUNCNAME
#define FUNCNAME MPIDI_Create_inter_root_communicator_connect
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_Create_inter_root_communicator_connect(const char *port_name, 
							MPID_Comm **comm_pptr, 
							MPIDI_VC_t **vc_pptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *tmp_comm;
    MPIDI_VC_t *connect_vc = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_CONNECT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_CONNECT);

    /* Connect to the root on the other side. Create a
       temporary intercommunicator between the two roots so that
       we can use MPI functions to communicate data between them. */

    mpi_errno = MPIDI_CH3_Connect_to_root(port_name, &connect_vc);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    mpi_errno = MPIDI_CH3I_Initialize_tmp_comm(&tmp_comm, connect_vc, 1);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

#ifdef MPIDI_CH3_HAS_CONN_ACCEPT_HOOK
    /* If the VC creates non-duplex connections then the acceptor will
     * need to connect back to form the other half of the connection. */
    mpi_errno = MPIDI_CH3_Complete_unidirectional_connection( connect_vc );
#endif

    *comm_pptr = tmp_comm;
    *vc_pptr = connect_vc;

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_CONNECT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


/*
   MPIDI_Comm_connect()

   Algorithm: First create a connection (vc) between this root and the
   root on the accept side. Using this vc, create a temporary
   intercomm between the two roots. Use MPI functions to communicate
   the other information needed to create the real intercommunicator
   between the processes on the two sides. Then free the
   intercommunicator between the roots. Most of the complexity is
   because there can be multiple process groups on each side. 
*/ 

#undef FUNCNAME
#define FUNCNAME MPIDI_Comm_connect
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Comm_connect(const char *port_name, MPID_Info *info, int root, 
		       MPID_Comm *comm_ptr, MPID_Comm **newcomm)
{
    int mpi_errno=MPI_SUCCESS;
    int j, i, rank, recv_ints[3], send_ints[2], context_id;
    int remote_comm_size=0;
    MPID_Comm *tmp_comm = NULL, *intercomm;
    MPIDI_VC_t *new_vc;
    int sendtag=100, recvtag=100, n_remote_pgs;
    int n_local_pgs=1, local_comm_size;
    pg_translation *local_translation = NULL, *remote_translation = NULL;
    pg_node *pg_list = NULL;
    MPIDI_PG_t **remote_pg = NULL;
    MPIU_CHKLMEM_DECL(3);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_COMM_CONNECT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_COMM_CONNECT);

    rank = comm_ptr->rank;
    local_comm_size = comm_ptr->local_size;

    if (rank == root)
    {
	/* Establish a communicator to communicate with the root on the other side. */
	mpi_errno = MPIDI_Create_inter_root_communicator_connect(port_name, &tmp_comm, &new_vc);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}

	/* Make an array to translate local ranks to process group index 
	   and rank */
	MPIU_CHKLMEM_MALLOC(local_translation,pg_translation*,
			    local_comm_size*sizeof(pg_translation),
			    mpi_errno,"local_translation");

	/* Make a list of the local communicator's process groups and encode 
	   them in strings to be sent to the other side.
	   The encoded string for each process group contains the process 
	   group id, size and all its KVS values */
	mpi_errno = ExtractLocalPGInfo( comm_ptr, local_translation, 
					&pg_list, &n_local_pgs );


	/* Send the remote root: n_local_pgs, local_comm_size,
           Recv from the remote root: n_remote_pgs, remote_comm_size,
           context_id for newcomm */

        send_ints[0] = n_local_pgs;
        send_ints[1] = local_comm_size;

	MPIU_DBG_MSG_FMT(CH3_CONNECT,VERBOSE,(MPIU_DBG_FDEST,
		  "sending two ints, %d and %d, and receiving 3 ints", 
                  send_ints[0], send_ints[1]));
        mpi_errno = MPIC_Sendrecv(send_ints, 2, MPI_INT, 0,
                                  sendtag++, recv_ints, 3, MPI_INT,
                                  0, recvtag++, tmp_comm->handle,
                                  MPI_STATUS_IGNORE);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
    }

    /* broadcast the received info to local processes */
    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"broadcasting the received 3 ints");
    mpi_errno = MPIR_Bcast(recv_ints, 3, MPI_INT, root, comm_ptr);
    if (mpi_errno) {
	MPIU_ERR_POP(mpi_errno);
    }

    n_remote_pgs = recv_ints[0];
    remote_comm_size = recv_ints[1];
    context_id = recv_ints[2];
    MPIU_CHKLMEM_MALLOC(remote_pg,MPIDI_PG_t**,
			n_remote_pgs * sizeof(MPIDI_PG_t*),
			mpi_errno,"remote_pg");
    MPIU_CHKLMEM_MALLOC(remote_translation,pg_translation*,
			remote_comm_size * sizeof(pg_translation),
			mpi_errno,"remote_translation");
    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"allocated remote process groups");

    /* Exchange the process groups and their corresponding KVSes */
    if (rank == root)
    {
	mpi_errno = SendPGtoPeerAndFree( tmp_comm, &sendtag, pg_list );
	mpi_errno = ReceivePGAndDistribute( tmp_comm, comm_ptr, root, &recvtag,
					n_remote_pgs, remote_pg );
	/* Receive the translations from remote process rank to process group 
	   index */
	MPIU_DBG_MSG_FMT(CH3_CONNECT,VERBOSE,(MPIU_DBG_FDEST,
               "sending %d ints, receiving %d ints", 
	      local_comm_size * 2, remote_comm_size * 2));
	mpi_errno = MPIC_Sendrecv(local_translation, local_comm_size * 2, 
				  MPI_INT, 0, sendtag++,
				  remote_translation, remote_comm_size * 2, 
				  MPI_INT, 0, recvtag++, tmp_comm->handle, 
				  MPI_STATUS_IGNORE);
	if (mpi_errno) {
	    MPIU_ERR_POP(mpi_errno);
	}

#ifdef MPICH_DBG_OUTPUT
	MPIU_DBG_PRINTF(("[%d]connect:Received remote_translation:\n", rank));
	for (i=0; i<remote_comm_size; i++)
	{
	    MPIU_DBG_PRINTF((" remote_translation[%d].pg_index = %d\n remote_translation[%d].pg_rank = %d\n",
		i, remote_translation[i].pg_index, i, remote_translation[i].pg_rank));
	}
#endif
    }
    else
    {
	mpi_errno = ReceivePGAndDistribute( tmp_comm, comm_ptr, root, &recvtag,
					n_remote_pgs, remote_pg );
    }

    /* Broadcast out the remote rank translation array */
    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"Broadcasting remote translation");
    mpi_errno = MPIR_Bcast(remote_translation, remote_comm_size * 2, MPI_INT,
			   root, comm_ptr);
    if (mpi_errno) {
	MPIU_ERR_POP(mpi_errno);
    }
#ifdef MPICH_DBG_OUTPUT
    MPIU_DBG_PRINTF(("[%d]connect:Received remote_translation after broadcast:\n", rank));
    for (i=0; i<remote_comm_size; i++)
    {
	MPIU_DBG_PRINTF((" remote_translation[%d].pg_index = %d\n remote_translation[%d].pg_rank = %d\n",
	    i, remote_translation[i].pg_index, i, remote_translation[i].pg_rank));
    }
#endif


    /* create and fill in the new intercommunicator */
    mpi_errno = MPIR_Comm_create(comm_ptr, newcomm);
    if (mpi_errno) {
	MPIU_ERR_POP(mpi_errno);
    }
    
    intercomm = *newcomm;
    intercomm->context_id = context_id;
    intercomm->is_low_group = 1;

    mpi_errno = SetupNewIntercomm( comm_ptr, remote_comm_size, 
				   remote_translation, remote_pg, intercomm );
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    /* synchronize with remote root */
    if (rank == root)
    {
	MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"sync with peer");
        mpi_errno = MPIC_Sendrecv(&i, 0, MPI_INT, 0,
                                  sendtag++, &j, 0, MPI_INT,
                                  0, recvtag++, tmp_comm->handle,
                                  MPI_STATUS_IGNORE);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
        }

        /* All communication with remote root done. Release the communicator. */
        MPIR_Comm_release(tmp_comm);
    }

    /*printf("connect:barrier\n");fflush(stdout);*/
    mpi_errno = MPIR_Barrier(comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    /* Free new_vc. It was explicitly allocated in MPIDI_CH3_Connect_to_root.*/
    if (rank == root) {
	FreeNewVC( new_vc );
    }

 fn_exit: 
    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"Exiting ch3u_comm_connect");
    MPIU_CHKLMEM_FREEALL();
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_COMM_CONNECT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/*
 * Extract all of the process groups from the given communicator and 
 * form a list (returned in pg_list) that contains that information.
 * Also returned is an array (local_translation) that contains tuples mapping
 * rank in process group to rank in that communicator (local translation
 * must be allocated before this routine is called).  The number of 
 * distinct process groups is returned in n_local_pgs_p .
 */
#undef FUNCNAME
#define FUNCNAME ExtractLocalPGInfo
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int ExtractLocalPGInfo( MPID_Comm *comm_p, 
			       pg_translation local_translation[], 
			       pg_node **pg_list_p,
			       int *n_local_pgs_p )
{
    pg_node        *pg_list = 0, *pg_iter, *pg_trailer;
    int            i, cur_index = 0, local_comm_size, mpi_errno = 0;
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_EXTRACTLOCALPGINFO);

    MPIDI_FUNC_ENTER(MPID_STATE_EXTRACTLOCALPGINFO);
    local_comm_size = comm_p->local_size;

    /* Make a list of the local communicator's process groups and encode 
       them in strings to be sent to the other side.
       The encoded string for each process group contains the process 
       group id, size and all its KVS values */
    
    cur_index = 0;
    MPIU_CHKPMEM_MALLOC(pg_list,pg_node*,sizeof(pg_node),mpi_errno,
			"pg_list");
    
    pg_list->pg_id = MPIU_Strdup(comm_p->vcr[0]->pg->id);
    pg_list->index = cur_index++;
    pg_list->next = NULL;
    mpi_errno = MPIDI_PG_To_string(comm_p->vcr[0]->pg, &pg_list->str);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    local_translation[0].pg_index = 0;
    local_translation[0].pg_rank = comm_p->vcr[0]->pg_rank;
    pg_iter = pg_list;
    for (i=1; i<local_comm_size; i++) {
	pg_iter = pg_list;
	pg_trailer = pg_list;
	while (pg_iter != NULL) {
	    if (MPIDI_PG_Id_compare(comm_p->vcr[i]->pg->id, pg_iter->pg_id)) {
		local_translation[i].pg_index = pg_iter->index;
		local_translation[i].pg_rank = comm_p->vcr[i]->pg_rank;
		break;
	    }
	    if (pg_trailer != pg_iter)
		pg_trailer = pg_trailer->next;
	    pg_iter = pg_iter->next;
	}
	if (pg_iter == NULL) {
	    /* We use MPIU_Malloc directly because we do not know in 
	       advance how many nodes we may allocate */
	    pg_iter = (pg_node*)MPIU_Malloc(sizeof(pg_node));
	    if (!pg_iter) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    pg_iter->pg_id = MPIU_Strdup(comm_p->vcr[i]->pg->id);
	    pg_iter->index = cur_index++;
	    pg_iter->next = NULL;
	    mpi_errno = MPIDI_PG_To_string(comm_p->vcr[i]->pg, &pg_iter->str);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    local_translation[i].pg_index = pg_iter->index;
	    local_translation[i].pg_rank = comm_p->vcr[i]->pg_rank;
	    pg_trailer->next = pg_iter;
	}
    }

    *n_local_pgs_p = cur_index;
    *pg_list_p     = pg_list;
    
#ifdef MPICH_DBG_OUTPUT
    pg_iter = pg_list;
    while (pg_iter != NULL) {
	MPIU_DBG_PRINTF(("connect:PG: '%s'\n<%s>\n", pg_iter->pg_id, pg_iter->str));
	pg_iter = pg_iter->next;
    }
#endif


 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_EXTRACTLOCALPGINFO);
    return mpi_errno;
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
}


/* The root process in comm_ptr receives strings describing the
   process groups and then distributes them to the other processes
   in comm_ptr.
   See SendPGToPeer for the routine that sends the descriptions */
#undef FUNCNAME
#define FUNCNAME ReceivePGAndDistribute
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int ReceivePGAndDistribute( MPID_Comm *tmp_comm, MPID_Comm *comm_ptr, 
				   int root, int *recvtag_p, 
				   int n_remote_pgs, MPIDI_PG_t *remote_pg[] )
{
    char *pg_str = 0;
    int  i, j, flag, p;
    int  rank = comm_ptr->rank;
    int  mpi_errno = 0;
    int  recvtag = *recvtag_p;
    MPIDI_STATE_DECL(MPID_STATE_RECEIVEPGANDDISTRIBUTE);

    MPIDI_FUNC_ENTER(MPID_STATE_RECEIVEPGANDDISTRIBUTE);

    for (i=0; i<n_remote_pgs; i++) {

	if (rank == root) {
	    /* First, receive the pg description from the partner */
	    mpi_errno = MPIC_Recv(&j, 1, MPI_INT, 0, recvtag++, 
				  tmp_comm->handle, MPI_STATUS_IGNORE);
	    *recvtag_p = recvtag;
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    pg_str = (char*)MPIU_Malloc(j);
	    if (pg_str == NULL) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    mpi_errno = MPIC_Recv(pg_str, j, MPI_CHAR, 0, recvtag++, 
				  tmp_comm->handle, MPI_STATUS_IGNORE);
	    *recvtag_p = recvtag;
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	}

	/* Broadcast the size and data to the local communicator */
	/*printf("accept:broadcasting 1 int\n");fflush(stdout);*/
	mpi_errno = MPIR_Bcast(&j, 1, MPI_INT, root, comm_ptr);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}

	if (rank != root) {
	    /* The root has already allocated this string */
	    pg_str = (char*)MPIU_Malloc(j);
	    if (pg_str == NULL) {
		MPIU_ERR_POP(mpi_errno);
	    }
	}
	/*printf("accept:broadcasting string of length %d\n", j);fflush(stdout);*/
	mpi_errno = MPIR_Bcast(pg_str, j, MPI_CHAR, root, comm_ptr);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
	/* Then reconstruct the received process group */
	mpi_errno = MPIDI_PG_Create_from_string(pg_str, &remote_pg[i], &flag);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
	
	MPIU_Free(pg_str);
	if (flag) {
#ifdef MPIDI_CH3_USES_SSHM
	    /* extra pg ref needed for shared memory modules because the 
	     * shm_XXXXing_list's
	     * need to be walked though in the later stages of finalize to
	     * free queue_info's.
	     */
	    MPIDI_PG_Add_ref(remote_pg[i]);
#endif
	    for (p=0; p<remote_pg[i]->size; p++) {
		MPIDI_VC_t *vc;
		MPIDI_PG_Get_vcr(remote_pg[i], p, &vc);
		MPIDI_CH3_VC_Init( vc );
	    }
	}
    }
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_RECEIVEPGANDDISTRIBUTE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* Used internally to broadcast process groups belonging to peercomm to
 all processes in comm.  The process with rank root in comm is the 
 process in peercomm from which the process groups are taken. */
#undef FUNCNAME
#define FUNCNAME MPID_PG_BCast
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_PG_BCast( MPID_Comm *peercomm_p, MPID_Comm *comm_p, int root )
{
    int n_local_pgs=0, mpi_errno = 0;
    pg_translation *local_translation = 0;
    pg_node *pg_list, *pg_next, *pg_head = 0;
    int rank, i, peer_comm_size;
    MPIU_CHKLMEM_DECL(1);

    peer_comm_size = comm_p->local_size;
    rank            = comm_p->rank;

    MPIU_CHKLMEM_MALLOC(local_translation,pg_translation*,
			peer_comm_size*sizeof(pg_translation),
			mpi_errno,"local_translation");
    
    if (rank == root) {
	/* Get the process groups known to the *peercomm* */
	ExtractLocalPGInfo( peercomm_p, local_translation, &pg_head, 
			    &n_local_pgs );
    }

    /* Now, broadcast the number of local pgs */
    NMPI_Bcast( &n_local_pgs, 1, MPI_INT, root, comm_p->handle );

    /* printf( "Number of pgs = %d\n", n_local_pgs ); fflush(stdout);  */
    pg_list = pg_head;
    for (i=0; i<n_local_pgs; i++) {
	int len, flag;
	char *pg_str=0;
	MPIDI_PG_t *pgptr;

	if (rank == root) {
	    if (!pg_list) {
		/* FIXME: Error, the pg_list is broken */
		printf( "Unexpected end of pg_list\n" ); fflush(stdout);
		break;
	    }
	    pg_str  = pg_list->str;
	    pg_list = pg_list->next;
	    len     = (int)strlen(pg_str) + 1;
	}
	NMPI_Bcast( &len, 1, MPI_INT, root, comm_p->handle );
	if (rank != root) {
	    pg_str = (char *)MPIU_Malloc(len);
	}
	NMPI_Bcast( pg_str, len, MPI_CHAR, root, comm_p->handle );
	if (rank != root) {
	    /* flag is true if the pg was created, false if it
	       already existed */
	    MPIDI_PG_Create_from_string( pg_str, &pgptr, &flag );
	    if (flag) {
		int p;
		/*printf( "[%d]Added pg named %s to list\n", rank, 
			(char *)pgptr->id );
			fflush(stdout); */
		/* FIXME: This initalization should be done
		   when the pg is created ? */
		for (p=0; p<pgptr->size; p++) {
		    MPIDI_VC_t *vc;
		    MPIDI_PG_Get_vcr(pgptr, p, &vc);
		    MPIDI_CH3_VC_Init( vc );
		}
	    }
	    MPIU_Free( pg_str );
	}
    }

    /* Free pg_list */
    pg_list = pg_head;
    /* FIXME: We should use the PG destroy function for this, and ensure that
       the PG fields are valid for that function */
    while (pg_list) {
	pg_next = pg_list->next;
	MPIU_Free( pg_list->str );
	if (pg_list->pg_id ) {
	    MPIU_Free( pg_list->pg_id );
	}
	MPIU_Free( pg_list );
	pg_list = pg_next;
    }

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}
/* Sends the process group information to the peer and frees the 
   pg_list */
#undef FUNCNAME
#define FUNCNAME SendPGtoPeerAndFree
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int SendPGtoPeerAndFree( MPID_Comm *tmp_comm, int *sendtag_p, 
				pg_node *pg_list )
{
    int mpi_errno = 0;
    int sendtag = *sendtag_p, i;
    pg_node *pg_iter;
    MPIDI_STATE_DECL(MPID_STATE_SENDPGTOPEERANDFREE);

    MPIDI_FUNC_ENTER(MPID_STATE_SENDPGTOPEERANDFREE);

    while (pg_list != NULL) {
	pg_iter = pg_list;
	i = (int)(strlen(pg_iter->str) + 1);
	/*printf("connect:sending 1 int: %d\n", i);fflush(stdout);*/
	mpi_errno = MPIC_Send(&i, 1, MPI_INT, 0, sendtag++, tmp_comm->handle);
	*sendtag_p = sendtag;
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
	
	/*printf("connect:sending string length %d\n", i);fflush(stdout);*/
	mpi_errno = MPIC_Send(pg_iter->str, i, MPI_CHAR, 0, sendtag++, 
			      tmp_comm->handle);
	*sendtag_p = sendtag;
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
	
	pg_list = pg_list->next;
	MPIU_Free(pg_iter->str);
	MPIU_Free(pg_iter->pg_id);
	MPIU_Free(pg_iter);
    }

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_SENDPGTOPEERANDFREE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* ---------------------------------------------------------------------- */
#undef FUNCNAME
#define FUNCNAME MPIDI_Create_inter_root_communicator_accept
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_Create_inter_root_communicator_accept(const char *port_name, 
						MPID_Comm **comm_pptr, 
						MPIDI_VC_t **vc_pptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *tmp_comm;
    MPIDI_VC_t *new_vc = NULL;
    MPID_Progress_state progress_state;
    int port_name_tag;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_ACCEPT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_ACCEPT);

    /* FIXME: This code should parallel the MPIDI_CH3_Connect_to_root
       code used in the MPIDI_Create_inter_root_communicator_connect */
    /* extract the tag from the port_name */
    mpi_errno = MPIDI_GetTagFromPort( port_name, &port_name_tag);
    if (mpi_errno != MPIU_STR_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    /* dequeue the accept queue to see if a connection with the
       root on the connect side has been formed in the progress
       engine (the connection is returned in the form of a vc). If
       not, poke the progress engine. */

    MPID_Progress_start(&progress_state);
    for(;;)
    {
	MPIDI_CH3I_Acceptq_dequeue(&new_vc, port_name_tag);
	if (new_vc != NULL)
	{
	    break;
	}

	mpi_errno = MPID_Progress_wait(&progress_state);
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno)
	{
	    MPID_Progress_end(&progress_state);
	    MPIU_ERR_POP(mpi_errno);
	}
	/* --END ERROR HANDLING-- */
    }
    MPID_Progress_end(&progress_state);

    mpi_errno = MPIDI_CH3I_Initialize_tmp_comm(&tmp_comm, new_vc, 0);

    /* If the VC creates non-duplex connections then the acceptor will
     * need to connect back to form the other half of the connection. */
#ifdef MPIDI_CH3_HAS_CONN_ACCEPT_HOOK
    mpi_errno = MPIDI_CH3_Complete_unidirectional_connection2( new_vc );
#endif

    *comm_pptr = tmp_comm;
    *vc_pptr = new_vc;

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CREATE_INTER_ROOT_COMMUNICATOR_ACCEPT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/*
 * MPIDI_Comm_accept()

   Algorithm: First dequeue the vc from the accept queue (it was
   enqueued by the progress engine in response to a connect request
   from the root on the connect side). Use this vc to create an
   intercommunicator between this root and the root on the connect
   side. Use this intercomm. to communicate the other information
   needed to create the real intercommunicator between the processes
   on the two sides. Then free the intercommunicator between the
   roots. Most of the complexity is because there can be multiple
   process groups on each side.

 */
#undef FUNCNAME
#define FUNCNAME MPIDI_Comm_accept
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Comm_accept(const char *port_name, MPID_Info *info, int root, 
		      MPID_Comm *comm_ptr, MPID_Comm **newcomm)
{
    int mpi_errno=MPI_SUCCESS;
    int i, j, rank, recv_ints[2], send_ints[3];
    int remote_comm_size=0;
    MPID_Comm *tmp_comm = NULL, *intercomm;
    MPIDI_VC_t *new_vc = NULL;
    int n_local_pgs=1, n_remote_pgs;
    int sendtag=100, recvtag=100, local_comm_size;
    pg_translation *local_translation = NULL, *remote_translation = NULL;
    pg_node *pg_list = NULL;
    MPIDI_PG_t **remote_pg = NULL;
    MPIU_CHKLMEM_DECL(3);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_COMM_ACCEPT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_COMM_ACCEPT);

    /* Create the new intercommunicator here. We need to send the
       context id to the other side. */
    /* FIXME: There is a danger that the context id won't be unique
       on the other side of this connection */
    mpi_errno = MPIR_Comm_create(comm_ptr, newcomm);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    rank = comm_ptr->rank;
    local_comm_size = comm_ptr->local_size;
    
    if (rank == root)
    {
	/* Establish a communicator to communicate with the root on the other side. */
	mpi_errno = MPIDI_Create_inter_root_communicator_accept(port_name, 
						&tmp_comm, &new_vc);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}

	/* Make an array to translate local ranks to process group index and 
	   rank */
	MPIU_CHKLMEM_MALLOC(local_translation,pg_translation*,
			    local_comm_size*sizeof(pg_translation),
			    mpi_errno,"local_translation");

	/* Make a list of the local communicator's process groups and encode 
	   them in strings to be sent to the other side.
	   The encoded string for each process group contains the process 
	   group id, size and all its KVS values */
	mpi_errno = ExtractLocalPGInfo( comm_ptr, local_translation, 
					&pg_list, &n_local_pgs );
        /* Send the remote root: n_local_pgs, local_comm_size, context_id for 
	   newcomm.
           Recv from the remote root: n_remote_pgs, remote_comm_size */

        send_ints[0] = n_local_pgs;
        send_ints[1] = local_comm_size;
        send_ints[2] = (*newcomm)->context_id;

	/*printf("accept:sending 3 ints, %d, %d, %d, and receiving 2 ints\n", send_ints[0], send_ints[1], send_ints[2]);fflush(stdout);*/
        mpi_errno = MPIC_Sendrecv(send_ints, 3, MPI_INT, 0,
                                  sendtag++, recv_ints, 2, MPI_INT,
                                  0, recvtag++, tmp_comm->handle,
                                  MPI_STATUS_IGNORE);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
    }

    /* broadcast the received info to local processes */
    /*printf("accept:broadcasting 2 ints - %d and %d\n", recv_ints[0], recv_ints[1]);fflush(stdout);*/
    mpi_errno = MPIR_Bcast(recv_ints, 2, MPI_INT, root, comm_ptr);
    if (mpi_errno) {
	MPIU_ERR_POP(mpi_errno);
    }

    n_remote_pgs = recv_ints[0];
    remote_comm_size = recv_ints[1];
    MPIU_CHKLMEM_MALLOC(remote_pg,MPIDI_PG_t**,
			n_remote_pgs * sizeof(MPIDI_PG_t*),
			mpi_errno,"remote_pg");
    MPIU_CHKLMEM_MALLOC(remote_translation,pg_translation*,
			remote_comm_size * sizeof(pg_translation),
			mpi_errno, "remote_translation");
    MPIU_DBG_PRINTF(("[%d]accept:remote process groups: %d\nremote comm size: %d\n", rank, n_remote_pgs, remote_comm_size));

    /* Exchange the process groups and their corresponding KVSes */
    if (rank == root)
    {
	/* The root receives the PG from the peer (in tmp_comm) and
	   distributes them to the processes in comm_ptr */
	mpi_errno = ReceivePGAndDistribute( tmp_comm, comm_ptr, root, &recvtag,
					n_remote_pgs, remote_pg );
	
	mpi_errno = SendPGtoPeerAndFree( tmp_comm, &sendtag, pg_list );

	/* Receive the translations from remote process rank to process group index */
	/*printf("accept:sending %d ints and receiving %d ints\n", local_comm_size * 2, remote_comm_size * 2);fflush(stdout);*/
	mpi_errno = MPIC_Sendrecv(local_translation, local_comm_size * 2, 
				  MPI_INT, 0, sendtag++,
				  remote_translation, remote_comm_size * 2, 
				  MPI_INT, 0, recvtag++, tmp_comm->handle, 
				  MPI_STATUS_IGNORE);
#ifdef MPICH_DBG_OUTPUT
	MPIU_DBG_PRINTF(("[%d]accept:Received remote_translation:\n", rank));
	for (i=0; i<remote_comm_size; i++)
	{
	    MPIU_DBG_PRINTF((" remote_translation[%d].pg_index = %d\n remote_translation[%d].pg_rank = %d\n",
		i, remote_translation[i].pg_index, i, remote_translation[i].pg_rank));
	}
#endif
    }
    else
    {
	mpi_errno = ReceivePGAndDistribute( tmp_comm, comm_ptr, root, &recvtag,
					    n_remote_pgs, remote_pg );
    }

    /* Broadcast out the remote rank translation array */
    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"Broadcast remote_translation");
    mpi_errno = MPIR_Bcast(remote_translation, remote_comm_size * 2, MPI_INT, 
			   root, comm_ptr);
#ifdef MPICH_DBG_OUTPUT
    MPIU_DBG_PRINTF(("[%d]accept:Received remote_translation after broadcast:\n", rank));
    for (i=0; i<remote_comm_size; i++)
    {
	MPIU_DBG_PRINTF((" remote_translation[%d].pg_index = %d\n remote_translation[%d].pg_rank = %d\n",
	    i, remote_translation[i].pg_index, i, remote_translation[i].pg_rank));
    }
#endif


    /* Now fill in newcomm */
    intercomm = *newcomm;
    intercomm->is_low_group = 0;

    mpi_errno = SetupNewIntercomm( comm_ptr, remote_comm_size, 
				   remote_translation, remote_pg, intercomm );
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    /* synchronize with remote root */
    if (rank == root)
    {
	MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"sync with peer");
        mpi_errno = MPIC_Sendrecv(&i, 0, MPI_INT, 0,
                                  sendtag++, &j, 0, MPI_INT,
                                  0, recvtag++, tmp_comm->handle,
                                  MPI_STATUS_IGNORE);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
        }

        /* All communication with remote root done. Release the communicator. */
        MPIR_Comm_release(tmp_comm);
    }

    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"Barrier");
    mpi_errno = MPIR_Barrier(comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    /* Free new_vc once the connection is completed. It was explicitly 
       allocated in ch3_progress.c and returned by 
       MPIDI_CH3I_Acceptq_dequeue. */
    if (rank == root) {
	FreeNewVC( new_vc );
    }

fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_COMM_ACCEPT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/* This is a utility routine used to initialize temporary communicators
   used in connect/accept operations */
#undef FUNCNAME
#define FUNCNAME  MPIDI_CH3I_Initialize_tmp_comm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Initialize_tmp_comm(MPID_Comm **comm_pptr, MPIDI_VC_t *vc_ptr, int is_low_group)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *tmp_comm, *commself_ptr;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_INITIALIZE_TMP_COMM);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_INITIALIZE_TMP_COMM);

    MPID_Comm_get_ptr( MPI_COMM_SELF, commself_ptr );
    mpi_errno = MPIR_Comm_create(commself_ptr, &tmp_comm);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    /* fill in all the fields of tmp_comm. */

    tmp_comm->context_id = 4095;  /* FIXME - we probably need a unique context_id. */
    tmp_comm->remote_size = 1;

    /* Fill in new intercomm */
    /* FIXME: This should share a routine with the communicator code to
       ensure that the initialization is consistent */
    tmp_comm->attributes   = NULL;
    tmp_comm->local_size   = 1;
    tmp_comm->rank         = 0;
    tmp_comm->local_group  = NULL;
    tmp_comm->remote_group = NULL;
    tmp_comm->comm_kind    = MPID_INTERCOMM;
    tmp_comm->local_comm   = NULL;
    tmp_comm->is_low_group = is_low_group;
    tmp_comm->coll_fns     = NULL;

    /* No pg structure needed since vc has already been set up (connection has been established). */

    /* Point local vcr, vcrt at those of commself_ptr */
    tmp_comm->local_vcrt = commself_ptr->vcrt;
    MPID_VCRT_Add_ref(commself_ptr->vcrt);
    tmp_comm->local_vcr  = commself_ptr->vcr;

    /* No pg needed since connection has already been formed. 
       FIXME - ensure that the comm_release code does not try to
       free an unallocated pg */

    /* Set up VC reference table */
    mpi_errno = MPID_VCRT_Create(tmp_comm->remote_size, &tmp_comm->vcrt);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**init_vcrt");
    }
    mpi_errno = MPID_VCRT_Get_ptr(tmp_comm->vcrt, &tmp_comm->vcr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**init_getptr");
    }
    
    MPID_VCR_Dup(vc_ptr, tmp_comm->vcr);

    *comm_pptr = tmp_comm;

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_INITIALIZE_TMP_COMM);
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

/* This routine initializes the new intercomm, setting up the
   VCRT and other common structures.  The is_low_group and context_id
   fields are NOT set because they differ in the use of this 
   routine in Comm_accept and Comm_connect */
#undef FUNCNAME
#define FUNCNAME SetupNewIntercomm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int SetupNewIntercomm( MPID_Comm *comm_ptr, int remote_comm_size, 
			      pg_translation remote_translation[],
			      MPIDI_PG_t **remote_pg, 
			      MPID_Comm *intercomm )
{
    int mpi_errno = MPI_SUCCESS, i;

    intercomm->attributes   = NULL;
    intercomm->remote_size  = remote_comm_size;
    intercomm->local_size   = comm_ptr->local_size;
    intercomm->rank         = comm_ptr->rank;
    intercomm->local_group  = NULL;
    intercomm->remote_group = NULL;
    intercomm->comm_kind    = MPID_INTERCOMM;
    intercomm->local_comm   = NULL;
    intercomm->coll_fns     = NULL;

    /* Point local vcr, vcrt at those of incoming intracommunicator */
    intercomm->local_vcrt = comm_ptr->vcrt;
    MPID_VCRT_Add_ref(comm_ptr->vcrt);
    intercomm->local_vcr  = comm_ptr->vcr;

    /* Set up VC reference table */
    mpi_errno = MPID_VCRT_Create(intercomm->remote_size, &intercomm->vcrt);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**init_vcrt");
    }
    mpi_errno = MPID_VCRT_Get_ptr(intercomm->vcrt, &intercomm->vcr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**init_getptr");
    }
    for (i=0; i < intercomm->remote_size; i++)
    {
	MPIDI_VC_t *vc;
	MPIDI_PG_Get_vcr(remote_pg[remote_translation[i].pg_index], 
			 remote_translation[i].pg_rank, &vc);
        MPID_VCR_Dup(vc, &intercomm->vcr[i]);
    }

    MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"Barrier");
    mpi_errno = MPIR_Barrier(comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

 fn_exit:
    return mpi_errno;

 fn_fail:
    goto fn_exit;
}

#if 0
    /* synchronize with remote root */
    if (rank == root)
    {
	MPIU_DBG_MSG(CH3_CONNECT,VERBOSE,"sync with peer");
        mpi_errno = MPIC_Sendrecv(&i, 0, MPI_INT, 0,
                                  sendtag++, &j, 0, MPI_INT,
                                  0, recvtag++, tmp_comm->handle,
                                  MPI_STATUS_IGNORE);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
        }

        /* All communication with remote root done. Release the communicator. */
        MPIR_Comm_release(tmp_comm);
    }

    /*printf("connect:barrier\n");fflush(stdout);*/
    mpi_errno = MPIR_Barrier(comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

}
#endif

/* Free new_vc. It was explicitly allocated in MPIDI_CH3_Connect_to_root. */
#undef FUNCNAME
#define FUNCNAME FreeNewVC
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int FreeNewVC( MPIDI_VC_t *new_vc )
{
    MPID_Progress_state progress_state;
    int mpi_errno = MPI_SUCCESS;
    
    if (new_vc->state != MPIDI_VC_STATE_INACTIVE) {
	MPID_Progress_start(&progress_state);
	while (new_vc->state != MPIDI_VC_STATE_INACTIVE) {
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS)
                {
                    MPID_Progress_end(&progress_state);
		    MPIU_ERR_POP(mpi_errno);
                }
	    /* --END ERROR HANDLING-- */
	}
	MPID_Progress_end(&progress_state);
    }

#ifdef MPIDI_CH3_HAS_CONN_ACCEPT_HOOK
    mpi_errno = MPIDI_CH3_Cleanup_after_connection( new_vc );
#endif
    MPIU_Free(new_vc);
 fn_fail:
    return mpi_errno;
}

/* FIXME: What is an Accept queue and who uses it?  
   Is this part of the connect/accept support?  
   These routines appear to be called by channel progress routines; 
   perhaps this belongs in util/sock (note the use of a port_name_tag in the 
   dequeue code, though this could be any string).

   Are the locks required?  If this is only called within the progress
   engine, then the progress engine locks should be sufficient.  If a
   finer grain lock model is used, it needs to be very carefully 
   designed and documented.
*/

typedef struct MPIDI_CH3I_Acceptq_s
{
    struct MPIDI_VC *vc;
    struct MPIDI_CH3I_Acceptq_s *next;
}
MPIDI_CH3I_Acceptq_t;

static MPIDI_CH3I_Acceptq_t * acceptq_head=0;
#if (USE_THREAD_IMPL == MPICH_THREAD_IMPL_NOT_IMPLEMENTED)
static MPID_Thread_lock_t acceptq_mutex;
#define MPIDI_Acceptq_lock() MPID_Thread_lock(&acceptq_mutex)
#define MPIDI_Acceptq_unlock() MPID_Thread_unlock(&acceptq_mutex)
#else
#define MPIDI_Acceptq_lock()
#define MPIDI_Acceptq_unlock()
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Acceptq_enqueue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Acceptq_enqueue(MPIDI_VC_t * vc)
{
    int mpi_errno=MPI_SUCCESS;
    MPIDI_CH3I_Acceptq_t *q_item;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_ACCEPTQ_ENQUEUE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_ACCEPTQ_ENQUEUE);

    q_item = (MPIDI_CH3I_Acceptq_t *)
        MPIU_Malloc(sizeof(MPIDI_CH3I_Acceptq_t)); 
    /* --BEGIN ERROR HANDLING-- */
    if (q_item == NULL)
    {
        mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
	goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    q_item->vc = vc;

    MPIDI_Acceptq_lock();

    q_item->next = acceptq_head;
    acceptq_head = q_item;
    
    MPIDI_Acceptq_unlock();

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_ACCEPTQ_ENQUEUE);
    return mpi_errno;
}


/* Attempt to dequeue a vc from the accept queue. If the queue is
   empty or the port_name_tag doesn't match, return a NULL vc. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Acceptq_dequeue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Acceptq_dequeue(MPIDI_VC_t ** vc, int port_name_tag)
{
    int mpi_errno=MPI_SUCCESS;
    MPIDI_CH3I_Acceptq_t *q_item, *prev;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_ACCEPTQ_DEQUEUE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_ACCEPTQ_DEQUEUE);

    MPIDI_Acceptq_lock();

    *vc = NULL;
    q_item = acceptq_head;
    prev = q_item;
    while (q_item != NULL)
    {
	if (q_item->vc->ch.port_name_tag == port_name_tag)
	{
	    *vc = q_item->vc;

	    if ( q_item == acceptq_head )
		acceptq_head = q_item->next;
	    else
		prev->next = q_item->next;

	    MPIU_Free(q_item);
	    break;;
	}
	else
	{
	    prev = q_item;
	    q_item = q_item->next;
	}
    }

    MPIDI_Acceptq_unlock();

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_ACCEPTQ_DEQUEUE);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Acceptq_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Acceptq_init(void)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_ACCEPTQ_INIT);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_ACCEPTQ_INIT);
#   if (USE_THREAD_IMPL == MPICH_THREAD_IMPL_NOT_IMPLEMENTED)
    {
	MPID_Thread_lock_init(&acceptq_mutex);
    }
#   endif
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_ACCEPTQ_INIT);
    return MPI_SUCCESS;
}

#else  /* MPIDI_CH3_HAS_NO_DYNAMIC_PROCESS is defined */

#endif /* MPIDI_CH3_HAS_NO_DYNAMIC_PROCESS */

