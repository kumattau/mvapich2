/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpi.h"
#include <stdio.h>
#include "mpitestconf.h"
#ifdef HAVE_STRING_H
#include <string.h>
#endif

static int verbose = 0;

static struct { MPI_Datatype atype, ptype; char name[32]; }
pairtypes[] =
    { {MPI_FLOAT, MPI_FLOAT_INT, "MPI_FLOAT_INT"},
      {MPI_DOUBLE, MPI_DOUBLE_INT, "MPI_DOUBLE_INT"},
      {MPI_LONG, MPI_LONG_INT, "MPI_LONG_INT"},
      {MPI_SHORT, MPI_SHORT_INT, "MPI_SHORT_INT"},
      {MPI_LONG_DOUBLE, MPI_LONG_DOUBLE_INT, "MPI_LONG_DOUBLE_INT"},
      {(MPI_Datatype) -1, (MPI_Datatype) -1, "end"}
    };

int parse_args(int argc, char **argv);

MPI_Aint pairtype_displacement(MPI_Datatype type, int *out_size_p);

MPI_Aint pairtype_displacement(MPI_Datatype type, int *out_size_p)
{
    MPI_Aint disp;

    switch(type) {
	case MPI_FLOAT_INT:
	    {
		struct { float a; int b; } foo;
		disp = (MPI_Aint) ((char *) &foo.b - (char *) &foo.a);
		*out_size_p = sizeof(foo);
	    }
	    break;
	case MPI_DOUBLE_INT:
	    {
		struct { double a; int b; } foo;
		disp = (MPI_Aint) ((char *) &foo.b - (char *) &foo.a);
		*out_size_p = sizeof(foo);
	    }
	    break;
	case MPI_LONG_INT:
	    {
		struct { long a; int b; } foo;
		disp = (MPI_Aint) ((char *) &foo.b - (char *) &foo.a);
		*out_size_p = sizeof(foo);
	    }
	    break;
	case MPI_SHORT_INT:
	    {
		struct { short a; int b; } foo;
		disp = (MPI_Aint) ((char *) &foo.b - (char *) &foo.a);
		*out_size_p = sizeof(foo);
	    }
	    break;
	case MPI_LONG_DOUBLE_INT:
	    {
		struct { long double a; int b; } foo;
		disp = (MPI_Aint) ((char *) &foo.b - (char *) &foo.a);
		*out_size_p = sizeof(foo);
	    }
	    break;
	default:
	    disp = -1;
    }
    return disp;
}

int main(int argc, char *argv[])
{
    int errs = 0;

    int i;
    int blks[2] = {1, 1};
    MPI_Aint disps[2] = {0, 0};
    MPI_Datatype types[2] = {MPI_INT, MPI_INT};
    MPI_Datatype stype;
    
    MPI_Init(&argc, &argv);
    parse_args(argc, argv);

    for (i=0; pairtypes[i].atype != (MPI_Datatype) -1; i++) {
	int atype_size, ptype_size, stype_size, handbuilt_extent;
	MPI_Aint ptype_extent, stype_extent, dummy_lb;

	types[0] = pairtypes[i].atype;

	MPI_Type_size(types[0], &atype_size);
	disps[1] = pairtype_displacement(pairtypes[i].ptype,
					 &handbuilt_extent);

	MPI_Type_create_struct(2, blks, disps, types, &stype);

	MPI_Type_size(stype, &stype_size);
	MPI_Type_size(pairtypes[i].ptype, &ptype_size);
	if (stype_size != ptype_size) {
	    errs++;

	    if (verbose) fprintf(stderr,
				 "size of %s (%d) does not match size of hand-built MPI struct (%d)\n",
				 pairtypes[i].name, ptype_size, stype_size);
	}

	MPI_Type_get_extent(stype, &dummy_lb, &stype_extent);
	MPI_Type_get_extent(pairtypes[i].ptype, &dummy_lb, &ptype_extent);
	if (stype_extent != ptype_extent) {
	    errs++;

	    if (verbose) fprintf(stderr,
				 "extent of %s (%d) does not match extent of hand-built MPI struct (%d)\n   NOTE: equivalent C struct has size of %d\n",
				 pairtypes[i].name, (int) ptype_extent,
				 (int) stype_extent,
				 handbuilt_extent);
	}
	MPI_Type_free( &stype );
    }
    

    /* print message and exit */
    if (errs) {
	fprintf(stderr, "Found %d errors\n", errs);
    }
    else {
	printf(" No Errors\n");
    }
    MPI_Finalize();
    return 0;
}

int parse_args(int argc, char **argv)
{
    /*
    int ret;

    while ((ret = getopt(argc, argv, "v")) >= 0)
    {
	switch (ret) {
	    case 'v':
		verbose = 1;
		break;
	}
    }
    */
    if (argc > 1 && strcmp(argv[1], "-v") == 0)
	verbose = 1;
    return 0;
}
