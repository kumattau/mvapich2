/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2003 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
/* ----------------------------------------------------------------------- */
/* A simple resource manager.  
 * This file implements a simple resource manager.  By implementing the 
 * interfaces in this file, other resource managers can be used.
 *
 * The interfaces are:
 * int MPIE_ChooseHosts( ProcessList *plist, int nplist, 
 *                         ProcessTable *ptable )
 *    Given the list of processes in plist, set the host field for each of the 
 *    processes in the ptable (ptable is already allocated)
 *
 * int MPIE_RMProcessArg( int argc, char *argv[], void *extra )
 *    Called by the top-level argument processor for unrecognized arguments;
 *    allows the resource manager to use the command line.  If no command
 *    line options are allowed, this routine simply returns zero.
 *
 */
/* ----------------------------------------------------------------------- */

#include "pmutilconf.h"

#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "pmutil.h"
#include "process.h"
#include "rm.h"

#ifndef isascii
#define isascii(c) (((c)&~0x7f)==0)
#endif

/* ----------------------------------------------------------------------- */
/* Determine the hosts                                                     */
/*                                                                         */
/* For each requested process that does not have an assigned host yet,     */
/* use information from a machines file to fill in the choices             */
/* ----------------------------------------------------------------------- */
/* These structures are used as part of the code to assign machines to 
   processes */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Choose the hosts for the processes in the ProcessList.  In
   addition, expand the list into a table with one entry per process.
   
   The function "readDB" is used to read information from a database
   of available machines and return a "machine table" that is used to
   supply hosts.  A default routine, MPIE_ReadMachines, is available.
*/
int MPIE_ChooseHosts( ProcessWorld *pWorld, 
		      MachineTable* (*readDB)(const char *, int, void *), 
		      void *readDBdata )
{
    int i, nNeeded=0;
    MachineTable *mt;
    ProcessApp   *app;
    ProcessState *pState;

    /* First, determine how many processes require host names */
    app = pWorld->apps;
    while (app) {
	if (!app->pState) {
	    pState = (ProcessState *)MPIU_Malloc( 
		app->nProcess * sizeof(ProcessState) );
	    if (!pState) {
		return -1;
	    }
	    /* Set the defaults (this should be in process.c?) */
	    for (i=0; i<app->nProcess; i++) {
		pState[i].hostname    = 0;
		pState[i].app         = app;
		pState[i].wRank       = -1;  /* Unassigned */
		pState[i].initWithEnv = -1;
		pState[i].pid         = -1;
		pState[i].status      = PROCESS_UNINITIALIZED;
		pState[i].exitStatus.exitReason = EXIT_NOTYET;
	    }
	    app->pState = pState;
	}
	pState = app->pState;
	for (i=0; i<app->nProcess; i++) {
	    if (!pState[i].hostname) nNeeded++;
	}
	app = app->nextApp;
    }
    if (nNeeded == 0) return 0;

    /* Now, for each app, find the hostnames by reading the
       machine table associate with the architecture */
    app = pWorld->apps;
    while (app && nNeeded > 0) {
	int nForApp = 0;

	pState = app->pState;
	for (i=0; i<app->nProcess; i++) {
	    if (!pState[i].hostname) nForApp++;
	}
	
	mt = (*readDB)( app->arch, nForApp, readDBdata );
#if 0
    /* Read the appropriate machines file.  There may be multiple files, 
       one for each requested architecture.  We'll read one machine file
       at a time, filling in all of the processes for each particular 
       architecture */
    /* ntest is used to ensure that we exit this loop in the case that 
       there are no machines of the requested architecture */
    ntest = ptable->nProcesses;
    while (nNeeded && ntest--) {
	for (i=0; i<ptable->nProcesses; i++) {
	    if (!ptable->table[i].spec.hostname) break;
	}
	/* Read the machines file for this architecture.  Use the
	   default architecture if none selected */
	arch = ptable->table[i].spec.arch;
	mt = (*readDB)( arch, nNeeded, readDBdata );
	if (!mt) {
	    /* FIXME : needs an error message */
	    /* By default, run on local host? */
	    if (1) {
		for (; i<ptable->nProcesses; i++) {
		    if ((!arch || 
			 (strcmp( ptable->table[i].spec.arch, arch )== 0)) &&
			!ptable->table[i].spec.hostname) {
			ptable->table[i].spec.hostname = "localhost";
			nNeeded--;
		    }
		}
		continue;
	    }
	    return 1;
	}
	if (mt->nHosts == 0) {
	    if (arch) {
		MPIU_Error_printf( "No machines specified for %s\n", arch );
	    }
	    else {
		MPIU_Error_printf( "No machines specified\n" );
	    }
		
	    return 1;
	}
	/* Assign machines to all processes with this arch */
	k = 0;
	/* Start from the first process that needs this arch */
	for (; i<ptable->nProcesses; i++) {
	    if ((!arch || (strcmp( ptable->table[i].spec.arch, arch )== 0)) &&
		!ptable->table[i].spec.hostname) {
		ptable->table[i].spec.hostname = mt->desc[k++].name;
		if (k >= mt->nHosts) k = 0;
		nNeeded--;
	    }
	}
	/* We can't free the machines table because we made references
	   to storage (hostnames) in the table */
	/* FIXME: Must be able to free the table */
    }
#endif
	app = app->nextApp;
    }
    return nNeeded != 0;   /* Return nonzero on failure */
}

#define MAXLINE 256
#ifndef DEFAULT_MACHINES_PATH
#define DEFAULT_MACHINES_PATH "."
#endif
static const char defaultMachinesPath[] = DEFAULT_MACHINES_PATH;

/* Read the associate machines file for the given architecture, returning
   a table with nNeeded machines.  The format of this file is

   # comments
   hostname
   
   hostname [ : [ nproc ] [ : [ login ] [ : [ netname ] ] ]

   Eventually, we may want to allow a more complex description,
   using key=value pairs

   The files are for the format:

   path/machines.<arch>
   or
   path/machines 
   (if no arch is specified)
   
   The files are found by looking in

   env{MPIEXEC_MACHINES_PATH}/machines.<archname>

   or, if archname is null, 

   env{MPIEXEC_MACHINES_PATH}/machines

   Question: We could set this with a -machinefile and -machinefilepath
   option, and pass this information in through the "data" argument.

   See the MPIE_RMProcessArg routine for a place to put the tests for the
   arguments.
*/

/* These allow a command-line option to override the path and filename
   for the machines file */
static char *machinefilePath = 0;
static char *machinefile = 0;

MachineTable *MPIE_ReadMachines( const char *arch, int nNeeded, 
				 void *data )
{
    FILE *fp=0;
    char buf[MAXLINE+1];
    char machinesfile[PATH_MAX];
    char dirname[PATH_MAX];
    const char *path=getenv("MPIEXEC_MACHINES_PATH");
    MachineTable *mt;
    int len, nFound = 0;
    
    /* Try to open the machines file.  arch may be null, in which 
       case we open the default file */
    /* FIXME: need path and external designation of file names */
    /* Partly done */
    if (!path) path = defaultMachinesPath;

    while (path) {
	char *next_path;
	/* Get next path member */
	next_path = strchr( path, ':' );
	if (next_path) 
	    len = next_path - path;
	else
	    len = strlen(path);
	
	/* Copy path into the file name */
	MPIU_Strncpy( dirname, path, len );

	dirname[len] = 0;

	/* Construct the final path name */
	if (arch) {
	    MPIU_Snprintf( machinesfile, PATH_MAX, 
			   "%s/machines.%s", dirname, arch );
	}
	else {
	    MPIU_Strncpy( machinesfile, dirname, PATH_MAX );
	    MPIU_Strnapp( machinesfile, "/machines", PATH_MAX );
	}
	DBG_PRINTF( ("Attempting to open %s\n", machinesfile) );
	fp = fopen( machinesfile, "r" );
	if (fp) break;  /* Found one */

	if (next_path) 
	    path = next_path + 1;
	else
	    path = 0;
    }
	
    if (!fp) {
	MPIU_Error_printf( "Could not open machines file %s\n", machinesfile );
	return 0;
    }
    mt = (MachineTable *)MPIU_Malloc( sizeof(MachineTable) );
    if (!mt) {
	MPIU_Internal_error_printf( "Could not allocate machine table\n" );
	return 0;
    }
    
    /* This may be larger than needed if the machines file has
       fewer entries than nNeeded */
    mt->desc = (MachineDesc *)MPIU_Malloc( nNeeded * sizeof(MachineDesc) );
    if (!mt->desc) {
	return 0;
    }

    /* Order of fields
       hostname [ : [ nproc ] [ : [ login ] [ : [ netname ] ] ]
    */
    while (nNeeded) {
	char *name=0, *login=0, *netname=0, *npstring=0;
	char *p, *p1;
	if (!fgets( buf, MAXLINE, fp )) {
	    break;
	}
	DBG_PRINTF( ("line: %s", buf) );
	/* Skip comment lines */
	p = buf;
	p[MAXLINE] = 0;
	while (isascii(*p) && isspace(*p)) p++;
	if (*p == '#') continue;

	/* To simplify the handling of the end-of-line, remove any return
	   or newline chars.  Include \r for DOS files */
	p1 = p;
	while (*p1 && (*p1 != '\r' && *p1 != '\n')) p1++;
	*p1 = 0;
	
	/* Parse the line by 
	   setting pointers to the fields
	   replacing : by null
	*/
	name = p;

	/* Skip over the value */
	p1 = p;
	while (*p1 && !isspace(*p1) && *p1 != ':') p1++;
	if (*p1 == ':') *p1++ = 0;

	p = p1;
	while (isascii(*p) && isspace(*p)) p++;
	
	npstring = p;

	/* Skip over the value */
	p1 = p;
	while (*p1 && !isspace(*p1) && *p1 != ':') p1++;
	if (*p1 == ':') *p1++ = 0;

	p = p1;
	while (isascii(*p) && isspace(*p)) p++;
	
	login = p;

	/* Skip over the value */
	p1 = p;
	while (*p1 && !isspace(*p1) && *p1 != ':') p1++;
	if (*p1 == ':') *p1++ = 0;

	p = p1;
	while (isascii(*p) && isspace(*p)) p++;
	
	netname = p;

	/* Skip over the value */
	p1 = p;
	while (*p1 && !isspace(*p1) && *p1 != ':') p1++;
	if (*p1 == ':') *p1++ = 0;
	
	/* Save the names */

	/* Initialize the fields for this new entry */
	mt->desc[nFound].hostname    = MPIU_Strdup( name );
	if (login) 
	    mt->desc[nFound].login   = MPIU_Strdup( login );
	else
	    mt->desc[nFound].login = 0;
	if (npstring) {
	    char *newp;
	    int n = strtol( npstring, &newp, 0 );
	    if (newp == npstring) {
		/* This indicates an error in the file.  How do we
		   report that? */
		n = 1;
	    }
	    mt->desc[nFound].np      = n;
	}
	else 
	    mt->desc[nFound].np      = 1;
	if (netname) 
	    mt->desc[nFound].netname = MPIU_Strdup( netname );
	else
	    mt->desc[nFound].netname = 0;

	nFound++;
	nNeeded--;
    }
    mt->nHosts = nFound;
    return mt;	
}

int MPIE_RMProcessArg( int argc, char *argv[], void *extra )
{
    char *cmd;
    int   incr = 0;
    if (strncmp( argv[0], "-machinefile", 12 ) == 0) {
	/* Check for machinefile and machinefilepath */
	cmd = argv[0] + 12;

	if (cmd[0] == 0) {
	    machinefile = MPIU_Strdup( argv[1] );
	    incr = 2;
	}
	else if (strcmp( cmd, "path" ) == 0) {
	    machinefilePath = MPIU_Strdup( argv[1] );
	    incr = 2;
	}
	/* else not an argument for this routine */
    }
    return incr;
}
