/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"
#include "pmi.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

/*  MPIDI_CH3U_Init_sock - does socket specific channel initialization
 *     publish_bc_p - if non-NULL, will be a pointer to the original position 
 *                    of the bc_val and should
 *                    do KVS Put/Commit/Barrier on business card before 
 *                    returning
 *     bc_key_p     - business card key buffer pointer.  freed if successfully
 *                    published
 *     bc_val_p     - business card value buffer pointer, updated to the next
 *                    available location or freed if published.
 *     val_max_sz_p - ptr to maximum value buffer size reduced by the number 
 *                    of characters written
 *                               
 */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3U_Init_sock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3U_Init_sock(int has_parent, MPIDI_PG_t *pg_p, int pg_rank,
                         char **publish_bc_p, char **bc_key_p, 
			 char **bc_val_p, int *val_max_sz_p)
{
    int mpi_errno = MPI_SUCCESS;
    int pmi_errno;
    int pg_size;
    int p;

    /*
     * Initialize the VCs associated with this process group (and thus MPI_COMM_WORLD)
     */

    pmi_errno = PMI_Get_size(&pg_size);
    if (pmi_errno != 0) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_get_size",
			     "**pmi_get_size %d", pmi_errno);
    }

    /* FIXME: This should probably be the same as MPIDI_VC_InitSock.  If
       not, why not? */
    for (p = 0; p < pg_size; p++)
    {
	pg_p->vct[p].ch.sendq_head = NULL;
	pg_p->vct[p].ch.sendq_tail = NULL;
	pg_p->vct[p].ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;
	pg_p->vct[p].ch.sock = MPIDU_SOCK_INVALID_SOCK;
	pg_p->vct[p].ch.conn = NULL;
    }    

    mpi_errno = MPIDI_CH3U_Get_business_card_sock(bc_val_p, val_max_sz_p);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**init_buscard");
    }

    /* might still have something to add (e.g. ssm channel) so don't publish */
    if (publish_bc_p != NULL)
    {
	pmi_errno = PMI_KVS_Put(pg_p->ch.kvs_name, *bc_key_p, *publish_bc_p);
	if (pmi_errno != PMI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_kvs_put",
				 "**pmi_kvs_put %d", pmi_errno);
	}
	pmi_errno = PMI_KVS_Commit(pg_p->ch.kvs_name);
	if (pmi_errno != PMI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_kvs_commit",
				 "**pmi_kvs_commit %d", pmi_errno);
	}

	pmi_errno = PMI_Barrier();
	if (pmi_errno != PMI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_barrier",
				 "**pmi_barrier %d", pmi_errno);
	}
    }

 fn_exit:
    
    return mpi_errno;
    
 fn_fail:
    /* --BEGIN ERROR HANDLING-- */
    if (pg_p != NULL)
    {
	/* MPIDI_CH3I_PG_Destroy(), which is called by MPIDI_PG_Destroy(), frees pg->ch.kvs_name */
	MPIDI_PG_Destroy(pg_p);
    }

    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

/* This routine initializes Sock-specific elements of the VC */
int MPIDI_VC_InitSock( MPIDI_VC_t *vc ) 
{
    vc->ch.sock               = MPIDU_SOCK_INVALID_SOCK;
    vc->ch.conn               = NULL;
    return 0;
}
