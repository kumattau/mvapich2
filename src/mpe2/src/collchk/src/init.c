/*
   (C) 2004 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#include "collchk.h" 

int MPI_Init(int * c, char *** v)
{
    int ret;

    fprintf( stdout, "Starting MPI Collective and Datatype Checking!\n" );
    /* make the call */
    ret = PMPI_Init(c, v);

    /* the main error class for all the errors */
    MPI_Add_error_class(&COLLCHK_ERRORS);

    /* the error codes for the profile */
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_NOT_INIT);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_CALL);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_ROOT);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_OP);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_INPLACE);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_DTYPE);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_HIGH_LOW);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_LL);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_TAG);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_DIMS);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_GRAPH);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_AMODE);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_WHENCE);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_DATAREP);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_PREVIOUS_BEGIN);
    MPI_Add_error_code(COLLCHK_ERRORS, &COLLCHK_ERR_FILE_NOT_OPEN);

#if defined( HAVE_MPI_IO )
    /* setup the fh_list counter */
    CollChk_fh_cnt = 0;
#endif
#if defined( HAVE_MPI_RMA )
    /* setup the win_list counter */
    CollChk_win_cnt = 0;
#endif
    /* setup the begin flag */
    COLLCHK_CALLED_BEGIN = 0;

    return ret;
}
