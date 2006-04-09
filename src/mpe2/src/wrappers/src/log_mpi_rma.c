/*
   (C) 2001 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
/*
   This file should be INCLUDED into log_mpi_core.c when adding the
   RMA routines to the profiling list

   Also set MPE_MAX_KNOWN_STATES >= 200
*/
#define MPE_ACCUMULATE_ID 181
#define MPE_ALLOC_MEM_ID 182
#define MPE_FREE_MEM_ID 183
#define MPE_GET_ID 184
#define MPE_PUT_ID 185
#define MPE_WIN_COMPLETE_ID 186
#define MPE_WIN_CREATE_ID 187
#define MPE_WIN_FENCE_ID 188
#define MPE_WIN_FREE_ID 189
#define MPE_WIN_GET_GROUP_ID 190
#define MPE_WIN_GET_NAME_ID 191
#define MPE_WIN_LOCK_ID 192
#define MPE_WIN_POST_ID 193
#define MPE_WIN_SET_NAME_ID 194
#define MPE_WIN_START_ID 195
#define MPE_WIN_TEST_ID 196
#define MPE_WIN_UNLOCK_ID 197
#define MPE_WIN_WAIT_ID 198

void MPE_Init_MPIRMA( void )
{
  MPE_State *state;

  state = &states[MPE_ACCUMULATE_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Accumulate";
  state->color = "purple";

  state = &states[MPE_ALLOC_MEM_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Alloc_mem";
  state->color = "purple";

  state = &states[MPE_FREE_MEM_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Free_mem";
  state->color = "purple";

  state = &states[MPE_GET_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Get";
  state->color = "LightGreen";

  state = &states[MPE_PUT_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Put";
  state->color = "LightBlue";

  state = &states[MPE_WIN_COMPLETE_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_complete";
  state->color = "purple";

  state = &states[MPE_WIN_CREATE_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_create";
  state->color = "purple";

  state = &states[MPE_WIN_FENCE_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_fence";
  state->color = "tomato";

  state = &states[MPE_WIN_FREE_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_free";
  state->color = "purple";

  state = &states[MPE_WIN_GET_GROUP_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_get_group";
  state->color = "purple";

  state = &states[MPE_WIN_GET_NAME_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_get_name";
  state->color = "purple";

  state = &states[MPE_WIN_LOCK_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_lock";
  state->color = "purple";

  state = &states[MPE_WIN_POST_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_post";
  state->color = "DarkGreen";

  state = &states[MPE_WIN_SET_NAME_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_set_name";
  state->color = "purple";

  state = &states[MPE_WIN_START_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_start";
  state->color = "purple";

  state = &states[MPE_WIN_TEST_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_test";
  state->color = "DarkOrange";

  state = &states[MPE_WIN_UNLOCK_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_unlock";
  state->color = "purple";

  state = &states[MPE_WIN_WAIT_ID];
  state->kind_mask = MPE_KIND_RMA;
  state->name = "MPI_Win_wait";
  state->color = "maroon";
}

int MPI_Accumulate( void *origin_addr, int origin_count,
                    MPI_Datatype origin_datatype, int target_rank,
                    MPI_Aint target_disp, int target_count,
                    MPI_Datatype target_datatype, MPI_Op op, MPI_Win win )
{
  int returnVal;

/*
      MPI_Accumulate - prototyping replacement for MPI_Accumulate
      Log the beginning and ending of the time spent in MPI_Accumulate calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_ACCUMULATE_ID)

  returnVal = PMPI_Accumulate( origin_addr, origin_count,
                               origin_datatype, target_rank,
                               target_disp, target_count,
                               target_datatype, op, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Alloc_mem( MPI_Aint size, MPI_Info info, void *baseptr )
{
  int returnVal;

/*
      MPI_Alloc_mem - prototyping replacement for MPI_Alloc_mem
      Log the beginning and ending of the time spent in MPI_Alloc_mem calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_ALLOC_MEM_ID)

  returnVal = PMPI_Alloc_mem( size, info, baseptr );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Free_mem( void *base )
{
  int returnVal;

/*
      MPI_Free_mem - prototyping replacement for MPI_Free_mem
      Log the beginning and ending of the time spent in MPI_Free_mem calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_FREE_MEM_ID)

  returnVal = PMPI_Free_mem( base );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Get( void *origin_addr, int origin_count,
             MPI_Datatype origin_datatype, int target_rank,
             MPI_Aint target_disp, int target_count,
             MPI_Datatype target_datatype, MPI_Win win )
{
  int returnVal;

/*
      MPI_Get - prototyping replacement for MPI_Get
      Log the beginning and ending of the time spent in MPI_Get calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_GET_ID)

  returnVal = PMPI_Get( origin_addr, origin_count,
                        origin_datatype, target_rank,
                        target_disp, target_count,
                        target_datatype, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Put( void *origin_addr, int origin_count,
             MPI_Datatype origin_datatype, int target_rank,
             MPI_Aint target_disp, int target_count,
             MPI_Datatype target_datatype, MPI_Win win )
{
  int returnVal;

/*
      MPI_Put - prototyping replacement for MPI_Put
      Log the beginning and ending of the time spent in MPI_Put calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_PUT_ID)

  returnVal = PMPI_Put( origin_addr, origin_count,
                        origin_datatype, target_rank,
                        target_disp, target_count,
                        target_datatype, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_complete( MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_complete - prototyping replacement for MPI_Win_complete
      Log the beginning and ending of the time spent in MPI_Win_complete calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_COMPLETE_ID)

  returnVal = PMPI_Win_complete( win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_create( void *base, MPI_Aint size, int disp_unit,
                    MPI_Info info, MPI_Comm comm, MPI_Win *win )
{
  int returnVal;

/*
      MPI_Win_create - prototyping replacement for MPI_Win_create
      Log the beginning and ending of the time spent in MPI_Win_create calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(comm,MPE_WIN_CREATE_ID)

  returnVal = PMPI_Win_create( base, size, disp_unit, info, comm, win );

  MPE_LOG_STATE_END(comm)

  return returnVal;
}

int MPI_Win_fence( int assert, MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_fence - prototyping replacement for MPI_Win_fence
      Log the beginning and ending of the time spent in MPI_Win_fence calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_FENCE_ID)

  returnVal = PMPI_Win_fence( assert, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_free( MPI_Win *win )
{
  int returnVal;

/*
      MPI_Win_free - prototyping replacement for MPI_Win_free
      Log the beginning and ending of the time spent in MPI_Win_free calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_FREE_ID)

  returnVal = PMPI_Win_free( win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_get_group( MPI_Win win, MPI_Group *group )
{
  int returnVal;

/*
      MPI_Win_get_group - prototyping replacement for MPI_Win_get_group
      Log the beginning and ending of the time spent in MPI_Win_get_group calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_GET_GROUP_ID)

  returnVal = PMPI_Win_get_group( win, group );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_get_name( MPI_Win win, char *win_name, int *resultlen )
{
  int returnVal;

/*
      MPI_Win_get_name - prototyping replacement for MPI_Win_get_name
      Log the beginning and ending of the time spent in MPI_Win_get_name calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_GET_NAME_ID)

  returnVal = PMPI_Win_get_name( win, win_name, resultlen );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

#if defined( HAVE_MPI_RMA_LOCK )
int MPI_Win_lock( int lock_type, int rank, int assert, MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_lock - prototyping replacement for MPI_Win_lock
      Log the beginning and ending of the time spent in MPI_Win_lock calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_LOCK_ID)

  returnVal = PMPI_Win_lock( lock_type, rank, assert, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}
#endif

int MPI_Win_post( MPI_Group group, int assert, MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_post - prototyping replacement for MPI_Win_post
      Log the beginning and ending of the time spent in MPI_Win_post calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_POST_ID)

  returnVal = PMPI_Win_post( group, assert, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_set_name( MPI_Win win, char *win_name )
{
  int returnVal;

/*
      MPI_Win_set_name - prototyping replacement for MPI_Win_set_name
      Log the beginning and ending of the time spent in MPI_Win_set_name calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_SET_NAME_ID)

  returnVal = PMPI_Win_set_name( win, win_name );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

int MPI_Win_start( MPI_Group group, int assert, MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_start - prototyping replacement for MPI_Win_start
      Log the beginning and ending of the time spent in MPI_Win_start calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_START_ID)

  returnVal = PMPI_Win_start( group, assert, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}

#if defined( HAVE_MPI_RMA_TEST )
int MPI_Win_test( MPI_Win win, int *flag )
{
  int returnVal;

/*
      MPI_Win_test - prototyping replacement for MPI_Win_test
      Log the beginning and ending of the time spent in MPI_Win_test calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_TEST_ID)

  returnVal = PMPI_Win_test( win, flag );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}
#endif

#if defined( HAVE_MPI_RMA_LOCK )
int MPI_Win_unlock( int rank, MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_unlock - prototyping replacement for MPI_Win_unlock
      Log the beginning and ending of the time spent in MPI_Win_unlock calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_UNLOCK_ID)

  returnVal = PMPI_Win_unlock( rank, win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}
#endif

int MPI_Win_wait( MPI_Win win )
{
  int returnVal;

/*
      MPI_Win_wait - prototyping replacement for MPI_Win_wait
      Log the beginning and ending of the time spent in MPI_Win_wait calls.
*/
  MPE_LOG_STATE_DECL

  MPE_LOG_STATE_BEGIN(MPE_COMM_NULL,MPE_WIN_WAIT_ID)

  returnVal = PMPI_Win_wait( win );

  MPE_LOG_STATE_END(MPE_COMM_NULL)

  return returnVal;
}
