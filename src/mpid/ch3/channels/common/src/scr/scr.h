/*
 * Copyright (c) 2009, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Adam Moody <moody20@llnl.gov>.
 * LLNL-CODE-411039.
 * All rights reserved.
 * This file is part of The Scalable Checkpoint / Restart (SCR) library.
 * For details, see https://sourceforge.net/projects/scalablecr/
 * Please also read this file: LICENSE.TXT.
*/

/* Copyright (c) 2001-2013, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */


#define SCR_SUCCESS (0)
#define SCR_FAILURE (1)

#define SCR_MAX_FILENAME 1024

/*
==========================
USER INTERFACE FUNCTIONS
==========================
*/

int SCR_Init();
int SCR_Finalize();
int SCR_Donot_Finalize();
int SCR_Do_Finalize();

/* v1.1 API */
int SCR_Need_checkpoint(int* flag);
int SCR_Start_checkpoint();
int SCR_Route_file(const char* name, char* file);
int SCR_Complete_checkpoint(int valid);
