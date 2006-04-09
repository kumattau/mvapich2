/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidimpl.h"
#ifdef MPIDI_DEV_IMPLEMENTS_KVS
#include "pmi.h"
#endif

/* FIXME: These routines need a description.  What is their purpose?  Who
   calls them and why?  What does each one do?
*/
static MPIDI_PG_t * MPIDI_PG_list = NULL;
static MPIDI_PG_t * MPIDI_PG_iterator_next = NULL;
static MPIDI_PG_Compare_ids_fn_t MPIDI_PG_Compare_ids_fn;
static MPIDI_PG_Destroy_fn_t MPIDI_PG_Destroy_fn;


#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Init(MPIDI_PG_Compare_ids_fn_t compare_ids_fn, 
		  MPIDI_PG_Destroy_fn_t destroy_fn)
{
    int mpi_errno = MPI_SUCCESS;
    
    MPIDI_PG_Compare_ids_fn = compare_ids_fn;
    MPIDI_PG_Destroy_fn     = destroy_fn;

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Finalize(void)
{
    int mpi_errno = MPI_SUCCESS;

    /* FIXME - straighten out the use of PG_Finalize - no use after 
       PG_Finalize */
    /* ifdefing out this check because the list will not be NULL in 
       Ch3_finalize because
       one additional reference is retained in MPIDI_Process.my_pg. 
       That reference is released
       only after ch3_finalize returns. If I release it before ch3_finalize, 
       the ssm channel crashes. */
    
#ifdef FOO

    if (MPIDI_PG_list != NULL)
    { 
	/* --BEGIN ERROR HANDLING-- */
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME, __LINE__, MPI_ERR_INTERN,
        "**dev|pg_finalize|list_not_empty", NULL); 
	/* --END ERROR HANDLING-- */
    }
#endif

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
    
    MPIU_CHKPMEM_MALLOC(pg,MPIDI_PG_t*,sizeof(MPIDI_PG_t),mpi_errno,"pg");
    MPIU_CHKPMEM_MALLOC(pg->vct,MPIDI_VC_t *,sizeof(MPIDI_VC_t)*vct_sz,
			mpi_errno,"pg->vct");

    pg->handle = 0;
    MPIU_Object_set_ref(pg, vct_sz);
    pg->size = vct_sz;
    pg->id = pg_id;
    
    for (p = 0; p < vct_sz; p++)
    {
	/* Initialize device fields in the VC object */
	MPIDI_VC_Init(&pg->vct[p], pg, p);
    }

#if 0
    /* Add pg's to the head */
    pg->next = MPIDI_PG_list;
    if (MPIDI_PG_iterator_next == MPIDI_PG_list)
    {
	MPIDI_PG_iterator_next = pg;
    }
    MPIDI_PG_list = pg;
#else
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
#endif    
    *pg_ptr = pg;
    
  fn_exit:
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
		printf("[%s%d]freeing vc%d - %p (%s)\n", MPIU_DBG_parent_str, MPIR_Process.comm_world->rank, i, &pg->vct[i], pg->id);fflush(stdout);
	    }
	    */
	    MPIDI_PG_Destroy_fn(pg);
	    MPIU_Free(pg->vct);
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

#undef FUNCNAME
#define FUNCNAME MPIDI_Allocate_more
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_Allocate_more(char **str, char **cur_pos, int *cur_len)
{
    char *longer;
    size_t orig_length, longer_length;

    orig_length = (*cur_pos - *str);
    /* FIXME: Really try to add a MB here?!  Why not just double? */
    longer_length = (*cur_pos - *str) + (1024*1024);

    longer = (char*)MPIU_Malloc(longer_length * sizeof(char));
    if (longer == NULL)
    {
	return MPI_ERR_NO_MEM;
    }
    memcpy(longer, *str, orig_length);
    longer[orig_length] = '\0';
    MPIU_Free(*str);
    
    *str = longer;
    *cur_pos = &longer[orig_length];
    *cur_len = (int)(longer_length - orig_length);

    return MPI_SUCCESS;
}

/* Note: Allocated memory that is returned in str_ptr.  The user of
   this routine must free that data */
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_To_string
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_To_string(MPIDI_PG_t *pg_ptr, char **str_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    char key[MPIDI_MAX_KVS_KEY_LEN];
    char val[MPIDI_MAX_KVS_VALUE_LEN];
    char *str, *cur_pos;
    int cur_len;
    char len_str[20];

    cur_len = 1024*1024;
    str = (char*)MPIU_Malloc(cur_len*sizeof(char));
    if (str == NULL)
    {
	mpi_errno = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
	goto fn_fail;
    }
    cur_pos = str;
    /* Save the PG id */
    mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, pg_ptr->id);
    if (mpi_errno != MPIU_STR_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    /* Save the PG size */
    MPIU_Snprintf(len_str, 20, "%d", pg_ptr->size);
    mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, len_str);
    if (mpi_errno != MPIU_STR_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    /* Save all the KVS entries for this PG */
    mpi_errno = MPIDI_KVS_First(pg_ptr->ch.kvs_name, key, val);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    while (mpi_errno == MPI_SUCCESS && key[0] != '\0')
    {
	mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, key);
	if (mpi_errno != MPIU_STR_SUCCESS)
	{
	    mpi_errno = MPIDI_Allocate_more(&str, &cur_pos, &cur_len);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, key);
	    if (mpi_errno != MPIU_STR_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	}
#ifndef USE_PERSISTENT_SHARED_MEMORY
	/* FIXME: This is a hack to avoid including shared-memory 
	   queue names in the buisness card that may be used
	   by processes that were not part of the same COMM_WORLD. */
/*	printf( "Adding key %s value %s\n", key, val ); */
	if (strstr( key, "businesscard" )) {
	    char *p = strstr( val, "$shm_host" );
	    if (p) p[1] = 0;
/*	    printf( "(fixed) Adding key %s value %s\n", key, val ); */
	}
#endif
	mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, val);
	if (mpi_errno != MPIU_STR_SUCCESS)
	{
	    mpi_errno = MPIDI_Allocate_more(&str, &cur_pos, &cur_len);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	    mpi_errno = MPIU_Str_add_string(&cur_pos, &cur_len, val);
	    if (mpi_errno != MPIU_STR_SUCCESS) {
		MPIU_ERR_POP(mpi_errno);
	    }
	}
	mpi_errno = MPIDI_KVS_Next(pg_ptr->ch.kvs_name, key, val);
    }
    *str_ptr = str;

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

/* This routine takes a string description of a process group (created with 
   MPIDI_PG_To_string, usually on a different process) and returns a pointer to
   the matching process group.  If the group already exists, flag is set to 
   false.  If the group does not exist, it is created with MPIDI_PG_Creat (and
   hence is added to the list of active process groups) and flag is set to 
   true.  In addition, the matching KVS space in the KVS cache is initialized
   from the input string, using the routines in mpidi_kvs.c 
*/
#undef FUNCNAME
#define FUNCNAME MPIDI_PG_Create_with_kvs
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_PG_Create_from_string(char * str, MPIDI_PG_t ** pg_pptr, int *flag)
{
    int mpi_errno = MPI_SUCCESS;
    char key[MPIDI_MAX_KVS_KEY_LEN];
    char val[MPIDI_MAX_KVS_VALUE_LEN];
    char *pg_id = 0;
    int pgid_len;
    char sz_str[20];
    int vct_sz;
    MPIDI_PG_t *existing_pg, *pg_ptr=0;
    MPIU_CHKPMEM_DECL(1);

    mpi_errno = PMI_Get_id_length_max(&pgid_len);
    if (mpi_errno != PMI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
          "**pmi_get_id_length_max", "**pmi_get_id_length_max %d", mpi_errno);
    }
    pgid_len = MPIDU_MAX(pgid_len, MPIDI_MAX_KVS_NAME_LEN);

    /* This memory will be attached to a process group */
    MPIU_CHKPMEM_MALLOC(pg_id,char *,pgid_len,mpi_errno,"pg_id");

    mpi_errno = MPIU_Str_get_string(&str, pg_id, pgid_len);
    if (mpi_errno != MPIU_STR_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = MPIU_Str_get_string(&str, sz_str, 20);
    if (mpi_errno != MPIU_STR_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    vct_sz = atoi(sz_str);

    mpi_errno = MPIDI_PG_Find(pg_id, &existing_pg);
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

    /* The pg_id, allocated above, is saved in the created process group */
    mpi_errno = MPIDI_PG_Create(vct_sz, pg_id, pg_pptr);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }
    /* The memory for pg_id that we allocated is now saved in *pg_pptr */
    MPIU_CHKPMEM_COMMIT();
    pg_ptr = *pg_pptr;
    pg_ptr->ch.kvs_name = (char*)MPIU_Malloc(MPIDI_MAX_KVS_NAME_LEN);
    if (pg_ptr->ch.kvs_name == NULL) {
	MPIU_ERR_POP(mpi_errno);
    }
    /* FIXME: This creates a new kvs name in the "cached" KVS space.
       What is the scope of this name (which processes know it)?  
       Is it a private (this process only) cache?  */
    mpi_errno = MPIDI_KVS_Create(pg_ptr->ch.kvs_name);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    key[0] = '\0';
    MPIU_Str_get_string(&str, key, MPIDI_MAX_KVS_KEY_LEN);
    MPIU_Str_get_string(&str, val, MPIDI_MAX_KVS_VALUE_LEN);
    while (key[0] != '\0')
    {
	mpi_errno = MPIDI_KVS_Put(pg_ptr->ch.kvs_name, key, val);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_POP(mpi_errno);
	}
	key[0] = '\0';
	MPIU_Str_get_string(&str, key, MPIDI_MAX_KVS_KEY_LEN);
	MPIU_Str_get_string(&str, val, MPIDI_MAX_KVS_VALUE_LEN);
    }

fn_exit:
    MPIU_CHKPMEM_REAP();
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#ifdef HAVE_CTYPE_H
/* Needed for isdigit */
#include <ctype.h>
#endif

void MPIDI_PG_IdToNum( MPIDI_PG_t *pg, int *id )
{
    const char *p = (const char *)pg->id;
    int pgid = 0;
    while (*p && !isdigit(*p)) p++;
    if (!*p) {
	p = (const char *)pg->id;
	while (*p) {
	    pgid += *p - ' ';
	}
	pgid = pgid ^ 0x100;
    }
    else {
	/* mpd uses (pid_num) as part of the kvs name, so
	   we skip over - and _ */
	while (*p) {
	    if (isdigit(*p)) {
		pgid = pgid * 10 + (*p - '0');
	    }
	    else if (*p != '-' && *p != '_') {
		break;
	    }
	    p++;
	}
    }
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
