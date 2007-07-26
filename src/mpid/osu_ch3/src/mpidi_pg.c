/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"
#include "pmi.h"

/* FIXME: These routines need a description.  What is their purpose?  Who
   calls them and why?  What does each one do?
*/
static MPIDI_PG_t * MPIDI_PG_list = NULL;
static MPIDI_PG_t * MPIDI_PG_iterator_next = NULL;
static MPIDI_PG_Compare_ids_fn_t MPIDI_PG_Compare_ids_fn;
static MPIDI_PG_Destroy_fn_t MPIDI_PG_Destroy_fn;

/* Set verbose to 1 to record changes to the process group structure. */
static int verbose = 0;

/* Key track of the process group corresponding to the MPI_COMM_WORLD 
   of this process */
static MPIDI_PG_t *pg_world = NULL;

#define MPIDI_MAX_KVS_KEY_LEN      256

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Init(int *argc_p, char ***argv_p, 
		  MPIDI_PG_Compare_ids_fn_t compare_ids_fn, 
		  MPIDI_PG_Destroy_fn_t destroy_fn)
{
    int mpi_errno = MPI_SUCCESS;
    char *p;
    
    MPIDI_PG_Compare_ids_fn = compare_ids_fn;
    MPIDI_PG_Destroy_fn     = destroy_fn;

    /* Check for debugging options.  We use MPICHD_DBG and -mpichd-dbg 
       to avoid confusion with the code in src/util/dbg/dbg_printf.c */
    p = getenv( "MPICHD_DBG_PG" );
    if (p && ( strcmp( p, "YES" ) == 0 || strcmp( p, "yes" ) == 0) )
	verbose = 1;
    if (argc_p && argv_p) {
	int argc = *argc_p, i;
	char **argv = *argv_p;
	for (i=1; i<=argc && argv[i]; i++) {
	    if (strcmp( "-mpichd-dbg-pg", argv[i] ) == 0) {
		int j;
		verbose = 1;
		for (j=i; j<=argc && argv[i]; j++) {
		    argv[j] = argv[j+1];
		}
		*argc_p = argc - 1;
		break;
	    }
	}
    }

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/*@ 
   MPIDI_PG_Finalize - Finalize the process groups, including freeing all
   process group structures
  @*/
int MPIDI_PG_Finalize(void)
{
    int mpi_errno = MPI_SUCCESS;
    int inuse;
    MPIDI_PG_t *pg, *pgNext;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_FINALIZE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_FINALIZE);

    /* Print the state of the process groups */
    if (verbose) {
	MPIU_PG_Printall( stdout );
    }

    /* FIXME - straighten out the use of PG_Finalize - no use after 
       PG_Finalize */
    if (pg_world->connData) {
	int rc;
	rc = PMI_Finalize();
	if (rc) {
	    MPIU_ERR_SET1(mpi_errno,MPI_ERR_OTHER, 
			  "**ch3|pmi_finalize", 
			  "**ch3|pmi_finalize %d", rc);
	}
    }

    /* Free the storage associated with the process groups */
    MPIDI_PG_release_ref(MPIDI_Process.my_pg, &inuse);
    pg = MPIDI_PG_list;
    while (pg) {
	pgNext = pg->next;
	
	if (pg->ref_count == 0) {
	    MPIDI_PG_Destroy(pg);
	}
	pg     = pgNext;
    }
    MPIDI_Process.my_pg = NULL;

    /* ifdefing out this check because the list will not be NULL in 
       Ch3_finalize because
       one additional reference is retained in MPIDI_Process.my_pg. 
       That reference is released
       only after ch3_finalize returns. If I release it before ch3_finalize, 
       the ssm channel crashes. */

#if 0

    if (MPIDI_PG_list != NULL)
    { 
	/* --BEGIN ERROR HANDLING-- */
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN,
        "**dev|pg_finalize|list_not_empty", NULL); 
	/* --END ERROR HANDLING-- */
    }
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_FINALIZE);
    return mpi_errno;
}

/* FIXME: This routine needs to make it clear that the pg_id, for example
   is saved; thus, if the pg_id is a string, then that string is not 
   copied and must be freed by a PG_Destroy routine */

/* This routine creates a new process group description and appends it to 
   the list of the known process groups.  The pg_id is saved, not copied.
   The PG_Destroy routine that was set with MPIDI_PG_Init is responsible for
   freeing any storage associated with the pg_id. 

   The new process group is returned in pg_ptr 
*/
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Create(int vct_sz, void * pg_id, MPIDI_PG_t ** pg_ptr)
{
    MPIDI_PG_t * pg = NULL, *pgnext;
    int p;
    int mpi_errno = MPI_SUCCESS;
    MPIU_CHKPMEM_DECL(2);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_CREATE);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_CREATE);
    
    MPIU_CHKPMEM_MALLOC(pg,MPIDI_PG_t*,sizeof(MPIDI_PG_t),mpi_errno,"pg");
    MPIU_CHKPMEM_MALLOC(pg->vct,MPIDI_VC_t *,sizeof(MPIDI_VC_t)*vct_sz,
			mpi_errno,"pg->vct");

    if (verbose) {
	fprintf( stdout, "Creating a process group of size %d\n", vct_sz );
	fflush(stdout);
    }

    pg->handle = 0;
    /* The reference count indicates the number of vc's that are or 
       have been in use and not disconnected. It starts at zero,
       except for MPI_COMM_WORLD. */
    MPIU_Object_set_ref(pg, 0);
    pg->size = vct_sz;
    pg->id   = pg_id;
    
    for (p = 0; p < vct_sz; p++)
    {
	/* Initialize device fields in the VC object */
	MPIDI_VC_Init(&pg->vct[p], pg, p);
    }

    /* We may first need to initialize the channel before calling the channel 
       VC init functions.  This routine may be a no-op; look in the 
       ch3_init.c file in each channel */
    MPIDI_CH3_PG_Init( pg );

    for (p = 0; p < vct_sz; p++)
    {
	/* Initialize the channel fields in the VC object */
	MPIDI_CH3_VC_Init( &pg->vct[p] );
    }
    
    /* Initialize the connection information to null.  Use
       the appropriate MPIDI_PG_InitConnXXX routine to set up these 
       fields */
    pg->connData           = 0;
    pg->getConnInfo        = 0;
    pg->connInfoToString   = 0;
    pg->connInfoFromString = 0;
    pg->freeConnInfo       = 0;

    /* The first process group is always the world group */
    if (!pg_world) { pg_world = pg; }

    /* Add pg's at the tail so that comm world is always the first pg */
    pg->next = 0;
    if (!MPIDI_PG_list)
    {
	MPIDI_PG_list = pg;
    }
    else
    {
	pgnext = MPIDI_PG_list; 
	while (pgnext->next)
	{
	    pgnext = pgnext->next;
	}
	pgnext->next = pg;
    }
    *pg_ptr = pg;
    
  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_CREATE);
    return mpi_errno;
    
  fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Destroy(MPIDI_PG_t * pg)
{
    /*int i;*/
    MPIDI_PG_t * pg_prev;
    MPIDI_PG_t * pg_cur;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_DESTROY);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_DESTROY);

    pg_prev = NULL;
    pg_cur = MPIDI_PG_list;
    while(pg_cur != NULL)
    {
	if (pg_cur == pg)
	{
	    if (MPIDI_PG_iterator_next == pg)
	    { 
		MPIDI_PG_iterator_next = MPIDI_PG_iterator_next->next;
	    }

            if (pg_prev == NULL)
                MPIDI_PG_list = pg->next; 
            else
                pg_prev->next = pg->next;

	    /*
	    for (i=0; i<pg->size; i++)
	    {
		printf("[%s%d]freeing vc%d - %p (%s)\n", MPIU_DBG_parent_str, 
		MPIR_Process.comm_world->rank, i, &pg->vct[i], pg->id);
		fflush(stdout);
	    }
	    */
	    if (verbose) {
		fprintf( stdout, "Destroying process group %s\n", 
			 (char *)pg->id ); fflush(stdout);
	    }
	    MPIDI_PG_Destroy_fn(pg);
	    MPIU_Free(pg->vct);
	    if (pg->connData) {
		MPIU_Free(pg->connData);
	    }
	    MPIU_Free(pg);

	    goto fn_exit;
	}

	pg_prev = pg_cur;
	pg_cur = pg_cur->next;
    }

    /* PG not found if we got here */
    MPIU_ERR_SET1(mpi_errno,MPI_ERR_OTHER,
		  "**dev|pg_not_found", "**dev|pg_not_found %p", pg);

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_DESTROY);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Find
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Find(void * id, MPIDI_PG_t ** pg_ptr)
{
    MPIDI_PG_t * pg;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_FIND);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_FIND);
    
    pg = MPIDI_PG_list;
    while (pg != NULL)
    {
	if (MPIDI_PG_Compare_ids_fn(id, pg->id) != FALSE)
	{
	    *pg_ptr = pg;
	    goto fn_exit;
	}

	pg = pg->next;
    }

    *pg_ptr = NULL;

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_FIND);
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Id_compare
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Id_compare(void * id1, void *id2)
{
    return MPIDI_PG_Compare_ids_fn(id1, id2);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Get_next
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Get_next(MPIDI_PG_t ** pg_ptr)
{
    *pg_ptr = MPIDI_PG_iterator_next;
    if (MPIDI_PG_iterator_next != NULL)
    { 
	MPIDI_PG_iterator_next = MPIDI_PG_iterator_next->next;
    }

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Iterate_reset
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Iterate_reset()
{
    MPIDI_PG_iterator_next = MPIDI_PG_list;
    return MPI_SUCCESS;
}

/* FIXME: What does DEV_IMPLEMENTS_KVS mean?  Why is it used?  Who uses 
   PG_To_string and why?  */

#ifdef MPIDI_DEV_IMPLEMENTS_KVS

/* PG_To_string is used in the implementation of connect/accept (and 
   hence in spawn) in ch3u_port.c */
/* Note: Allocated memory that is returned in str_ptr.  The user of
   this routine must free that data */
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_To_string
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_To_string(MPIDI_PG_t *pg_ptr, char **str_ptr, int *lenStr)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_TO_STRING);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_TO_STRING);

    /* Replace this with the new string */
    if (pg_ptr->connInfoToString) {
	(*pg_ptr->connInfoToString)( str_ptr, lenStr, pg_ptr );
    }
    else {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_INTERN,"**noConnInfoToString");
    }

    /*printf( "PgToString: Pg string is %s\n", *str_ptr ); fflush(stdout);*/
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_TO_STRING);
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

/* This routine takes a string description of a process group (created with 
   MPIDI_PG_To_string, usually on a different process) and returns a pointer to
   the matching process group.  If the group already exists, flag is set to 
   false.  If the group does not exist, it is created with MPIDI_PG_Create (and
   hence is added to the list of active process groups) and flag is set to 
   true.  In addition, the connection information is set up using the 
   information in the input string.
*/
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Create_from_string
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Create_from_string(const char * str, MPIDI_PG_t ** pg_pptr, 
				int *flag)
{
    int mpi_errno = MPI_SUCCESS;
    const char *p;
    int vct_sz;
    MPIDI_PG_t *existing_pg, *pg_ptr=0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_CREATE_FROM_STRING);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_CREATE_FROM_STRING);

    /*printf( "PgCreateFromString: Creating pg from %s\n", str ); 
      fflush(stdout); */
    /* The pg_id is at the beginning of the string, so we can just pass
       it to the find routine */
    /* printf( "Looking for pg with id %s\n", str );fflush(stdout); */
    mpi_errno = MPIDI_PG_Find((void *)str, &existing_pg);
    if (mpi_errno != PMI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    if (existing_pg != NULL) {
	/* return the existing PG */
	*pg_pptr = existing_pg;
	*flag = 0;
	/* Note that the memory for the pg_id is freed in the exit */
	goto fn_exit;
    }
    *flag = 1;

    /* Get the size from the string */
    p = str;
    while (*p) p++; p++;
    vct_sz = atoi(p);

    mpi_errno = MPIDI_PG_Create(vct_sz, (void *)str, pg_pptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    
    pg_ptr = *pg_pptr;
    pg_ptr->id = MPIU_Strdup( str );
    
    /* Set up the functions to use strings to manage connection information */
    MPIDI_PG_InitConnString( pg_ptr );
    (*pg_ptr->connInfoFromString)( str, pg_ptr );

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_CREATE_FROM_STRING);
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#ifdef HAVE_CTYPE_H
/* Needed for isdigit */
#include <ctype.h>
#endif

/*
 * Convert a process group id into a number.  We use a simple process that
 * simple combines all of the characters in the id.  If the PM uses similar
 * names containing an incrementing count, then this number should be
 * distinct.  It would really be best if the PM could give us this value.
 */
void MPIDI_PG_IdToNum( MPIDI_PG_t *pg, int *id )
{
    const char *p = (const char *)pg->id;
    int pgid = 0;

    while (*p) { pgid += *p++ - ' '; pgid &= 0x7ffffff; }
    *id = pgid;
}
#else
/* FIXME: This is a temporary hack for devices that do not define
   MPIDI_DEV_IMPLEMENTS_KVS
   FIXME: MPIDI_DEV_IMPLEMENTS_KVS should be removed
 */
void MPIDI_PG_IdToNum( MPIDI_PG_t *pg, int *id )
{
    *id = 0;
}
#endif

/*
 * Managing connection information for process groups
 * 
 *
 */

/* Setting a process's connection information 
   
   This is a collective call (for scalability) over all of the processes in 
   the same MPI_COMM_WORLD.
*/
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_SetConnInfo
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_SetConnInfo( int rank, const char *connString )
{
    int mpi_errno = MPI_SUCCESS;
    int pmi_errno;
    int len;
    char key[128];
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_SetConnInfo);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_SetConnInfo);

    MPIU_Assert(pg_world->connData);
    
    len = MPIU_Snprintf(key, sizeof(key), "P%d-businesscard", rank);
    if (len < 0 || len > sizeof(key)) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**snprintf",
			     "**snprintf %d", len);
    }
    pmi_errno = PMI_KVS_Put(pg_world->connData, key, connString );
    if (pmi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_kvs_put",
			     "**pmi_kvs_put %d", pmi_errno);
    }
    pmi_errno = PMI_KVS_Commit(pg_world->connData);
    if (pmi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_kvs_commit",
			     "**pmi_kvs_commit %d", pmi_errno);
    }
    
    pmi_errno = PMI_Barrier();
    if (pmi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_barrier",
			     "**pmi_barrier %d", pmi_errno);
    }
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_SetConnInfo);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

/* For all of these routines, the format of the process group description
   that is created and used by the connTo/FromString routines is this:
   (All items are strings, terminated by null)

   process group id string
   sizeof process group (as string)
   conninfo for rank 0
   conninfo for rank 1
   ...

   The "conninfo for rank 0" etc. for the original (MPI_COMM_WORLD)
   process group are stored in the PMI_KVS space with the keys 
   p<rank>-businesscard .  

   Fixme: Add a routine to publish the connection info to this file so that
   the key for the businesscard is defined in just this one file.
*/


/* The "KVS" versions are for the process group to which the calling 
   process belongs.  These use the PMI_KVS routines to access the
   process information */
static int getConnInfoKVS( int rank, char *buf, int bufsize, MPIDI_PG_t *pg )
{
    char key[MPIDI_MAX_KVS_KEY_LEN];
    int  mpi_errno = MPI_SUCCESS, rc, pmi_errno;;

    rc = MPIU_Snprintf(key, MPIDI_MAX_KVS_KEY_LEN, "P%d-businesscard", rank );
    if (rc < 0 || rc > MPIDI_MAX_KVS_KEY_LEN) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomem");
    }
    pmi_errno = PMI_KVS_Get(pg->connData, key, buf, bufsize );
    if (pmi_errno) {
	MPIDI_PG_CheckForSingleton();
	pmi_errno = PMI_KVS_Get(pg->connData, key, buf, bufsize );
    }
    if (pmi_errno) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**pmi_kvs_get");
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

static int connToStringKVS( char **buf_p, int *slen, MPIDI_PG_t *pg )
{
    char *string = 0;
    char *pg_idStr = (char *)pg->id;      /* In the PMI/KVS space,
					     the pg id is a string */
    char buf[MPIDI_MAX_KVS_VALUE_LEN];
    int   i, j, vallen, rc, mpi_errno = MPI_SUCCESS, len;
    int   curSlen;

    /* Make an initial allocation of a string with an estimate of the
       needed space */
    len = 0;
    curSlen = 10 + pg->size * 128;
    string = (char *)MPIU_Malloc( curSlen );

    /* Start with the id of the pg */
    while (*pg_idStr && len < curSlen) 
	string[len++] = *pg_idStr++;
    string[len++] = 0;
    
    /* Add the size of the pg */
    MPIU_Snprintf( &string[len], curSlen, "%d", pg->size );
    while (string[len]) len++;
    len++;

    for (i=0; i<pg->size; i++) {
	rc = getConnInfoKVS( i, buf, MPIDI_MAX_KVS_VALUE_LEN, pg );
	if (rc) {
	    MPIU_Internal_error_printf( 
		    "Panic: getConnInfoKVS failed for %s (rc=%d)\n", 
		    (char *)pg->id, rc );
	}
#ifndef USE_PERSISTENT_SHARED_MEMORY
	/* FIXME: This is a hack to avoid including shared-memory 
	   queue names in the business card that may be used
	   by processes that were not part of the same COMM_WORLD. 
	   To fix this, the shared memory channels should look at the
	   returned connection info and decide whether to use 
	   sockets or shared memory by determining whether the
	   process is in the same MPI_COMM_WORLD. */
	/* FIXME: The more general problem is that the connection information
	   needs to include some information on the range of validity (e.g.,
	   all processes, same comm world, particular ranks), and that
	   representation needs to be scalable */
/*	printf( "Adding key %s value %s\n", key, val ); */
	{
	char *p = strstr( buf, "$shm_host" );
	if (p) p[1] = 0;
	/*	    printf( "(fixed) Adding key %s value %s\n", key, val ); */
	}
#endif
	/* Add the information to the output buffer */
	vallen = strlen(buf);
	/* Check that this will fix in the remaining space */
	if (len + vallen + 1 >= curSlen) {
	    char *nstring = 0;
	    nstring = MPIU_Realloc( string, 
				   curSlen + (pg->size - i) * (vallen + 1 ));
	    if (!nstring) {
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomem");
	    }
	    string = nstring;
	}
	/* Append to string */
	for (j=0; j<vallen+1; j++) {
	    string[len++] = buf[j];
	}
    }

    *buf_p = string;
    *slen  = len;
 fn_exit:
    return mpi_errno;
 fn_fail:
    if (string) MPIU_Free(string);
    goto fn_exit;
}
static int connFromStringKVS( const char *buf, MPIDI_PG_t *pg )
{
    /* Fixme: this should be a failure to call this routine */
    return MPI_SUCCESS;
}
static int connFreeKVS( MPIDI_PG_t *pg )
{
    /* In this implementation, there is no local data */
    return MPI_SUCCESS;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_PG_InitConnKVS
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_InitConnKVS( MPIDI_PG_t *pg )
{
    int pmi_errno, kvs_name_sz;
    int mpi_errno = MPI_SUCCESS;

    pmi_errno = PMI_KVS_Get_name_length_max( &kvs_name_sz );
    if (pmi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
			     "**pmi_kvs_get_name_length_max", 
			     "**pmi_kvs_get_name_length_max %d", pmi_errno);
    }
    
    pg->connData = (char *)MPIU_Malloc(kvs_name_sz + 1);
    if (pg->connData == NULL) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**nomem");
    }
    
    pmi_errno = PMI_KVS_Get_my_name(pg->connData, kvs_name_sz);
    if (pmi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
			     "**pmi_kvs_get_my_name", 
			     "**pmi_kvs_get_my_name %d", pmi_errno);
    }
    
    pg->getConnInfo        = getConnInfoKVS;
    pg->connInfoToString   = connToStringKVS;
    pg->connInfoFromString = connFromStringKVS;
    pg->freeConnInfo       = connFreeKVS;

 fn_exit:
    return mpi_errno;
 fn_fail:
    if (pg->connData) { MPIU_Free(pg->connData); }
    goto fn_exit;
}

/* Return the kvsname associated with the MPI_COMM_WORLD of this process. */
int MPIDI_PG_GetConnKVSname( char ** kvsname )
{
    *kvsname = pg_world->connData;
    return MPI_SUCCESS;
}

/* For process groups that are not our MPI_COMM_WORLD, store the connection
   information in an array of strings.  These routines and structure
   implement the access to this information. */
typedef struct {
    int     toStringLen;   /* Length needed to encode this connection info */
    char ** connStrings;   /* pointer to an array, indexed by rank, containing
			      connection information */
} MPIDI_ConnInfo;

static int getConnInfo( int rank, char *buf, int bufsize, MPIDI_PG_t *pg )
{
    MPIDI_ConnInfo *connInfo = (MPIDI_ConnInfo *)pg->connData;

    /* printf( "Entering getConnInfo\n" ); fflush(stdout); */
    if (!connInfo || !connInfo->connStrings || !connInfo->connStrings[rank]) {
	/* FIXME: Turn this into a valid error code create/return */
	printf( "Fatal error in getConnInfo (rank = %d)\n", rank );
	printf( "connInfo = %p\n", connInfo );fflush(stdout);
	if (connInfo) {
	    printf( "connInfo->connStrings = %p\n", connInfo->connStrings );
	}
	/* Fatal error.  Connection information missing */
	fflush(stdout);   
    }

    /* printf( "Copying %s to buf\n", connInfo->connStrings[rank] ); fflush(stdout); */
    
    MPIU_Strncpy( buf, connInfo->connStrings[rank], bufsize );
    return MPI_SUCCESS;
}
static int connToString( char **buf_p, int *slen, MPIDI_PG_t *pg )
{
    char *string = 0, *str, *pg_id;
    int  i, len=0;
    
    MPIDI_ConnInfo *connInfo = (MPIDI_ConnInfo *)pg->connData;

    /* Create this from the string array */
    string = (char *)MPIU_Malloc( connInfo->toStringLen );
    str = string;

    pg_id = pg->id;
    /* FIXME: This is a hack, and it doesn't even work */
    /*    MPIDI_PrintConnStrToFile( stdout, __FILE__, __LINE__, 
	  "connToString: pg id is", (char *)pg_id );*/
    /* This is intended to cause a process to transition from a singleton
       to a non-singleton. */
    if (strstr( pg_id, "singinit_kvs" ) == pg_id) {
	PMI_Get_id( pg->id, 256 );
    }
    while (*pg_id) str[len++] = *pg_id++;
    str[len++] = 0;
    
    MPIU_Snprintf( &str[len], 20, "%d", pg->size);
    /* Skip over the length */
    while (str[len++]);

    /* Copy each connection string */
    for (i=0; i<pg->size; i++) {
	char *p = connInfo->connStrings[i];
	while (*p) { str[len++] = *p++; }
	str[len++] = 0;
    }

    if (len > connInfo->toStringLen) {
	*buf_p = 0;
	*slen  = 0;
	return MPIR_Err_create_code(MPI_SUCCESS,MPIR_ERR_FATAL,"connToString",
			    __LINE__, MPI_ERR_INTERN, "**intern", NULL);
    }

    *buf_p = string;
    *slen = len;

    return MPI_SUCCESS;
}
static int connFromString( const char *buf, MPIDI_PG_t *pg )
{
    MPIDI_ConnInfo *conninfo = 0;
    int i, mpi_errno = MPI_SUCCESS;
    const char *buf0 = buf;   /* save the start of buf */

    /* printf( "Starting with buf = %s\n", buf );fflush(stdout); */

    /* Skip the pg id */
    while (*buf) buf++; buf++;

    /* Determine the size of the pg */
    pg->size = atoi( buf );
    while (*buf) buf++; buf++;

    conninfo = (MPIDI_ConnInfo *)MPIU_Malloc( sizeof(MPIDI_ConnInfo) );
    conninfo->connStrings = (char **)MPIU_Malloc( pg->size * sizeof(char *));

    /* For now, make a copy of each item */
    for (i=0; i<pg->size; i++) {
	/* printf( "Adding conn[%d] = %s\n", i, buf );fflush(stdout); */
	conninfo->connStrings[i] = MPIU_Strdup( buf );
	while (*buf) buf++;
	buf++;
    }
    pg->connData = conninfo;
	
    /* Save the length of the string needed to encode the connection
       information */
    conninfo->toStringLen = (int)(buf - buf0) + 1;

    return mpi_errno;
}
static int connFree( MPIDI_PG_t *pg )
{
    MPIDI_ConnInfo *conninfo = (MPIDI_ConnInfo *)pg->connData;
    int i;

    for (i=0; i<pg->size; i++) {
	MPIU_Free( conninfo->connStrings[i] );
    }
    MPIU_Free( conninfo->connStrings );
    MPIU_Free( conninfo );

    return MPI_SUCCESS;
}

#ifdef USE_DBG_LOGGING
/* This is a temporary routine that is used to print out the pg string.
   A better approach may be to convert it into a single (long) string
   with no nulls. */
int MPIDI_PrintConnStr( const char *file, int line, 
			const char *label, const char *str )
{
    int pg_size, i;

    MPIU_DBG_Outevent( file, line, MPIU_DBG_CH3_CONNECT, 0, label );
    MPIU_DBG_Outevent( file, line, MPIU_DBG_CH3_CONNECT, 0, str );
    
    /* Skip the pg id */
    while (*str) str++; str++;

    /* Determine the size of the pg */
    pg_size = atoi( str );
    while (*str) str++; str++;

    for (i=0; i<pg_size; i++) {
	MPIU_DBG_Outevent( file, line, MPIU_DBG_CH3_CONNECT, 0, str );
	while (*str) str++;
	str++;
    }
    return 0;
}
int MPIDI_PrintConnStrToFile( FILE *fd, const char *file, int line, 
			      const char *label, const char *str )
{
    int pg_size, i;

    fprintf( fd, "ConnStr from %s(%d); %s\n\t%s\n", file, line, label, str );
    
    /* Skip the pg id */
    while (*str) str++; str++;

    fprintf( fd, "\t%s\n", str );
    /* Determine the size of the pg */
    pg_size = atoi( str );
    while (*str) str++; str++;

    for (i=0; i<pg_size; i++) {
	fprintf( fd, "\t%s\n", str ); 
	while (*str) str++;
	str++;
    }
    fflush(stdout);
    return 0;
}
#endif

int MPIDI_PG_InitConnString( MPIDI_PG_t *pg )
{
    int mpi_errno = MPI_SUCCESS;

    pg->connData           = 0;
    pg->getConnInfo        = getConnInfo;
    pg->connInfoToString   = connToString;
    pg->connInfoFromString = connFromString;
    pg->freeConnInfo       = connFree;

    return mpi_errno;
}

/* Temp to get connection value for rank r */
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_GetConnString
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_GetConnString( MPIDI_PG_t *pg, int rank, char *val, int vallen )
{
    int mpi_errno = MPI_SUCCESS;

    if (pg->getConnInfo) {
	mpi_errno = (*pg->getConnInfo)( rank, val, vallen, pg );
    }
    else {
	MPIU_Internal_error_printf( "Panic: no getConnInfo defined!\n" );
    }

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Dup_vcr
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/*@
  MPIDI_PG_Dup_vcr - Duplicate a virtual connection from a process group

  Notes:
  This routine provides a dup of a virtual connection given a process group
  and a rank in that group.  This routine is used only in initializing
  the MPI-1 communicators 'MPI_COMM_WORLD' and 'MPI_COMM_SELF', and in creating
  the initial intercommunicator after an 'MPI_Comm_spawn', 
  'MPI_Comm_spawn_multiple', or 'MPI_Comm_connect/MPI_Comm_accept'.  

  In addition to returning a dup of the virtual connection, it manages the
  reference count of the process group, which is always the number of inuse
  virtual connections.
  @*/
int MPIDI_PG_Dup_vcr( MPIDI_PG_t *pg, int rank, MPIDI_VC_t **vc_p )
{
    MPIDI_VC_t *vc;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_DUP_VCR);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_DUP_VCR);

    vc = &pg->vct[rank];
    /* Increase the reference count of the vc.  If the reference count 
       increases from 0 to 1, increase the reference count of the 
       process group *and* the reference count of the vc (this
       allows us to distinquish between Comm_free and Comm_disconnect) */
    /* FIXME: This should be a fetch and increment for thread-safety */
    if (vc->ref_count == 0) {
	MPIDI_PG_add_ref(pg);
	MPIDI_VC_add_ref(vc);
    }
    MPIDI_VC_add_ref(vc);
    *vc_p = vc;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_DUP_VCR);
    return MPI_SUCCESS;
}

/* FIXME: This routine should invoke a close method on the connection,
   rather than have all of the code here */
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Close_VCs
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/*@
  MPIDI_PG_Close_VCs - Close all virtual connections on all process groups.
  
  Note:
  This routine is used in MPID_Finalize.  It is here to keep the process-group
  related functions together.
  @*/
int MPIDI_PG_Close_VCs( void )
{
    MPIDI_PG_t * pg = MPIDI_PG_list;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PG_CLOSE_VCS);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PG_CLOSE_VCS);

    while (pg) {
	int i, inuse;

	MPIU_DBG_MSG_S(CH3_DISCONNECT,VERBOSE,"Closing vcs for pg %s",
		       (char *)pg->id );


	for (i = 0; i < pg->size; i++)
	{
	    MPIDI_VC_t * vc = &pg->vct[i];
	    /* If the VC is myself then skip the close message */
	    if (pg == MPIDI_Process.my_pg && i == MPIDI_Process.my_pg_rank) {
                if (vc->ref_count != 0) {
                    MPIDI_PG_release_ref(pg, &inuse);
                }
		continue;
	    }

	    if (vc->state == MPIDI_VC_STATE_ACTIVE || 
		vc->state == MPIDI_VC_STATE_REMOTE_CLOSE
#ifdef MPIDI_CH3_USES_SSHM
		/* FIXME: Remove this IFDEF */
		/* FIXME: There should be no vc->ch.xxx refs in this code 
		   (those are channel-only fields) */
		/* sshm queues are uni-directional.  A VC that is connected 
		 * in the read direction is marked MPIDI_VC_STATE_INACTIVE
		 * so that a connection will be formed on the first write.  
		 * Since the other side is marked MPIDI_VC_STATE_ACTIVE for 
		 * writing 
		 * we need to initiate the close protocol on the read side 
		 * even if the write state is MPIDI_VC_STATE_INACTIVE. */
		|| ((vc->state == MPIDI_VC_STATE_INACTIVE) && vc->ch.shm_read_connected)
#endif
		)
	    {
		MPIDI_CH3U_VC_SendClose( vc, i );
	    }
	    else
	    {
                if (vc->state == MPIDI_VC_STATE_INACTIVE && vc->ref_count != 0) {
		    /* FIXME: If the reference count for the vc is not 0,
		       something is wrong */
                    MPIDI_PG_release_ref(pg, &inuse);
                }

		MPIU_DBG_MSG_FMT(CH3_DISCONNECT,VERBOSE,(MPIU_DBG_FDEST,
		     "vc=%p: not sending a close to %d, vc in state %s", vc,i,
		     MPIDI_VC_GetStateString(vc->state)));
	    }
	}
	pg = pg->next;
    }
    /* Note that we do not free the process groups within this routine, even
       if the reference counts have gone to zero. That is done once the 
       connections are in fact closed (by the final progress loop that
       handles any close requests that this code generates) */

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PG_CLOSE_VCS);
    return mpi_errno;
}

/*
 * This routine may be called to print the contents (including states and
 * reference counts) for process groups.
 */
int MPIU_PG_Printall( FILE *fp )
{
    MPIDI_PG_t *pg;
    int         i;
		  
    pg = MPIDI_PG_list;

    fprintf( fp, "Process groups:\n" );
    while (pg) {
	fprintf( fp, "size = %d, refcount = %d, id = %s\n", 
		 pg->size, pg->ref_count, (char *)pg->id );
	for (i=0; i<pg->size; i++) {
	    fprintf( fp, "\tVCT rank = %d, refcount = %d, lpid = %d, state = %d \n", 
		     pg->vct[i].pg_rank, pg->vct[i].ref_count,
		     pg->vct[i].lpid, (int)pg->vct[i].state );
	}
	fflush(fp);
	pg = pg->next;
    }

    return 0;
}

int MPIDI_PG_CheckForSingleton( void )
{
    if (strstr((char*)pg_world->id,"singinit_kvs") == (char *)pg_world->id) {
	char buf[256];
	/* Force an enroll */
	PMI_KVS_Get( "foobar", "foobar", buf, sizeof(buf) );
	PMI_Get_id( pg_world->id, 256 );
	PMI_KVS_Get_my_name( pg_world->connData, 256 );
    }
    return MPI_SUCCESS;
}
