#include "upmi.h"
#include <stdlib.h>

struct PMI_keyval_t;
int _size, _rank, _appnum;

int UPMI_INIT( int *spawned ) {
    #ifdef USE_PMI2_API
    return PMI2_Init( spawned, &_size, &_rank, &_appnum );
    #else
    return PMI_Init( spawned );
    #endif
}

int UPMI_INITIALIZED( int *initialized ) { 
    #ifdef USE_PMI2_API
    *initialized = PMI2_Initialized();
    return UPMI_SUCCESS;
    #else
    return PMI_Initialized( initialized );
    #endif
}

int UPMI_FINALIZE( void ) { 
    #ifdef USE_PMI2_API
    return PMI2_Finalize();
    #else
    return PMI_Finalize();
    #endif
}

int UPMI_GET_SIZE( int *size ) { 
    #ifdef USE_PMI2_API
    *size = _size;
    return UPMI_SUCCESS;
    #else
    return PMI_Get_size( size );
    #endif
}

int UPMI_GET_RANK( int *rank ) { 
    #ifdef USE_PMI2_API
    *rank = _rank;
    return UPMI_SUCCESS;
    #else
    return PMI_Get_rank( rank );
    #endif
}

int UPMI_GET_APPNUM( int *appnum ) { 
    #ifdef USE_PMI2_API
    *appnum = _appnum;
    return UPMI_SUCCESS;
    #else
    return PMI_Get_appnum( appnum );
    #endif
}

int UPMI_GET_UNIVERSE_SIZE( int *size ) { 
    #ifdef USE_PMI2_API
    char name[] = "universeSize";
    int outlen, found;
    PMI2_Info_GetJobAttrIntArray( name, size, sizeof (int), &outlen, &found );
    if( found && outlen==1 ) {
        return UPMI_SUCCESS;
    } else {
        return UPMI_FAIL;
    }
    #else
    return PMI_Get_universe_size( size );
    #endif
}

int UPMI_BARRIER( void ) { 
    #ifdef USE_PMI2_API
    return PMI2_KVS_Fence();
    #else
    return PMI_Barrier();
    #endif
}

int UPMI_ABORT( int exit_code, const char error_msg[] ) { 
    #ifdef USE_PMI2_API
    return PMI2_Abort( 1, error_msg );    //flag = 1, abort all processes
    #else
    return PMI_Abort( exit_code, error_msg );
    #endif
}

int UPMI_KVS_GET_KEY_LENGTH_MAX( int *length ) { 
    #ifdef USE_PMI2_API
    *length = PMI2_MAX_KEYLEN;
    return UPMI_SUCCESS;
    #else
    return PMI_KVS_Get_key_length_max( length );
    #endif
}

int UPMI_KVS_GET_NAME_LENGTH_MAX( int *length ) { 
    #ifdef USE_PMI2_API
    *length = PMI2_MAX_KEYLEN; //TODO is this correct?
    return UPMI_SUCCESS;
    #else
    return PMI_KVS_Get_name_length_max( length );
    #endif
}

int UPMI_KVS_GET_VALUE_LENGTH_MAX( int *length ) { 
    #ifdef USE_PMI2_API
    *length = PMI2_MAX_VALLEN;
    return UPMI_SUCCESS;
    #else
    return PMI_KVS_Get_value_length_max( length );
    #endif
}

int UPMI_KVS_GET_MY_NAME( char kvsname[], int length ) {
    #ifdef USE_PMI2_API
    return PMI2_Job_GetId( kvsname, length );
    #else
    return PMI_KVS_Get_my_name( kvsname, length );
    #endif
}

int UPMI_KVS_PUT( const char kvsname[], const char key[], const char value[] ) { 
    #ifdef USE_PMI2_API
    return PMI2_KVS_Put( key, value );
    #else
    return PMI_KVS_Put( kvsname, key, value );
    #endif
}

int UPMI_KVS_GET( const char kvsname[], const char key[], char value[], int length ) { 
    #ifdef USE_PMI2_API
    int vallen;
    return PMI2_KVS_Get( kvsname, PMI2_ID_NULL, key, value, length, &vallen );
    #else
    return PMI_KVS_Get( kvsname, key, value, length );
    #endif
}

int UPMI_KVS_COMMIT( const char kvsname[] ) { 
    #ifdef USE_PMI2_API
    //return PMI2_KVS_Fence();
    return UPMI_SUCCESS;
    #else
    return PMI_KVS_Commit( kvsname );
    #endif
}

int UPMI_PUBLISH_NAME( const char service_name[], const char port[], const struct MPID_Info *info_ptr ) { 
    #ifdef USE_PMI2_API
    return PMI2_Nameserv_publish( service_name, info_ptr, port );
    #else
    return PMI_Publish_name( service_name, port );
    #endif
}

int UPMI_UNPUBLISH_NAME( const char service_name[], const struct MPID_Info *info_ptr ) { 
    #ifdef USE_PMI2_API
    return PMI2_Nameserv_unpublish( service_name, info_ptr );
    #else
    return PMI_Unpublish_name( service_name );
    #endif
}

int UPMI_LOOKUP_NAME( const char service_name[], char port[], const struct MPID_Info *info_ptr ) { 
    #ifdef USE_PMI2_API
    return PMI2_Nameserv_lookup( service_name, info_ptr, port, sizeof port );  
    #else
    return PMI_Lookup_name( service_name, port );
    #endif
}

int UPMI_GET_NODE_ATTR( const char name[], char value[], int valuelen, int *found, int waitfor ) {
    #ifdef USE_PMI2_API
    return PMI2_Info_GetNodeAttr( name, value, valuelen, found, waitfor );
    #else
    return UPMI_FAIL;
    #endif
}

int UPMI_GET_NODE_ATTR_INT_ARRAY( const char name[], int array[], int arraylen, int *outlen, int *found ) {
    #ifdef USE_PMI2_API
    return PMI2_Info_GetNodeAttrIntArray( name, array, arraylen, outlen, found );
    #else
    return UPMI_FAIL;
    #endif
}

int UPMI_PUT_NODE_ATTR( const char name[], const char value[] ) {
    #ifdef USE_PMI2_API
    return PMI2_Info_PutNodeAttr( name, value );
    #else
    return UPMI_FAIL;
    #endif
}

int UPMI_GET_JOB_ATTR( const char name[], char value[], int valuelen, int *found ) {
    #ifdef USE_PMI2_API
    return PMI2_Info_GetJobAttr( name, value, valuelen, found );
    #else
    return UPMI_FAIL;
    #endif
}

int UPMI_GET_JOB_ATTR_INT_ARRAY( const char name[], int array[], int arraylen, int *outlen, int *found ) {
    #ifdef USE_PMI2_API
    return PMI2_Info_GetJobAttrIntArray( name, array, arraylen, outlen, found );
    #else
    return UPMI_FAIL;
    #endif
}

int UPMI_JOB_SPAWN(int count,
                   const char * cmds[],
                   int argcs[],
                   const char ** argvs[],
                   const int maxprocs[],
                   const int info_keyval_sizes[],
                   const void *info_keyval_vectors[],
                   int preput_keyval_size,
                   const void *preput_keyval_vector[],
                   char jobId[],
                   int jobIdSize,
                   int errors[])
{
    #ifdef USE_PMI2_API
    return PMI2_Job_Spawn( count, cmds, argcs, argvs, maxprocs,
                           info_keyval_sizes, (const struct MPID_Info**)info_keyval_vectors,
                           preput_keyval_size, (const struct MPID_Info**)preput_keyval_vector,
                           jobId, jobIdSize, errors );
    #else
    return PMI_Spawn_multiple( count, cmds, argvs, maxprocs,
                               info_keyval_sizes, (const struct PMI_keyval_t**)info_keyval_vectors,
                               preput_keyval_size, (const struct PMI_keyval_t*)preput_keyval_vector[0],
                               errors );
    #endif
}

