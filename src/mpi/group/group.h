/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  $Id: group.h,v 1.1.1.1 2006/01/18 21:09:43 huangwei Exp $
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* MPIR_Group_create is needed by some of the routines that return groups
   from communicators, so it is in mpidimpl.h */
void MPIR_Group_setup_lpid_list( MPID_Group * );
int MPIR_Group_check_valid_ranks( MPID_Group *, int [], int );
int MPIR_Group_check_valid_ranges( MPID_Group *, int [][3], int );
void MPIR_Group_setup_lpid_pairs( MPID_Group *, MPID_Group * );

