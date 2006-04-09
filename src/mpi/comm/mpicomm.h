/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: mpicomm.h,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Function prototypes for communicator helper functions */
int MPIR_Get_contextid( MPID_Comm * );
int MPIR_Get_intercomm_contextid( MPID_Comm * );
void MPIR_Free_contextid( int );

