/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/*
 * Notes: This is a temporary file; the various rlog routines will 
 * eventually be moved into src/util/logging/rlog/, where they 
 * belong.  This file will then contain only the utility functions
 */

#include "mpiimpl.h"
#ifdef HAVE_TIMING

#include <math.h>


/* global variables */
#if (USE_LOGGING == MPID_LOGGING_RLOG)
RLOG_Struct *g_pRLOG = NULL;
#endif

/* utility funcions */
#ifndef RGB
#define RGB(r,g,b)      ((unsigned long)(((unsigned char)(r)|((unsigned short)((unsigned char)(g))<<8))|(((unsigned long)(unsigned char)(b))<<16)))
#endif

static unsigned long getColorRGB(double fraction, double intensity, unsigned char *r, unsigned char *g, unsigned char *b)
{
    double red, green, blue;
    double dtemp;

    fraction = fabs(modf(fraction, &dtemp));
    
    if (intensity > 2.0)
	intensity = 2.0;
    if (intensity < 0.0)
	intensity = 0.0;
    
    dtemp = 1.0/6.0;
    
    if (fraction < 1.0/6.0)
    {
	red = 1.0;
	green = fraction / dtemp;
	blue = 0.0;
    }
    else
    {
	if (fraction < 1.0/3.0)
	{
	    red = 1.0 - ((fraction - dtemp) / dtemp);
	    green = 1.0;
	    blue = 0.0;
	}
	else
	{
	    if (fraction < 0.5)
	    {
		red = 0.0;
		green = 1.0;
		blue = (fraction - (dtemp*2.0)) / dtemp;
	    }
	    else
	    {
		if (fraction < 2.0/3.0)
		{
		    red = 0.0;
		    green = 1.0 - ((fraction - (dtemp*3.0)) / dtemp);
		    blue = 1.0;
		}
		else
		{
		    if (fraction < 5.0/6.0)
		    {
			red = (fraction - (dtemp*4.0)) / dtemp;
			green = 0.0;
			blue = 1.0;
		    }
		    else
		    {
			red = 1.0;
			green = 0.0;
			blue = 1.0 - ((fraction - (dtemp*5.0)) / dtemp);
		    }
		}
	    }
	}
    }
    
    if (intensity > 1)
    {
	intensity = intensity - 1.0;
	red = red + ((1.0 - red) * intensity);
	green = green + ((1.0 - green) * intensity);
	blue = blue + ((1.0 - blue) * intensity);
    }
    else
    {
	red = red * intensity;
	green = green * intensity;
	blue = blue * intensity;
    }
    
    *r = (unsigned char)(red * 255.0);
    *g = (unsigned char)(green * 255.0);
    *b = (unsigned char)(blue * 255.0);

    return RGB(*r,*g,*b);
}

static unsigned long random_color(unsigned char *r, unsigned char *g, unsigned char *b)
{
    double d1, d2;

    d1 = (double)rand() / (double)RAND_MAX;
    d2 = (double)rand() / (double)RAND_MAX;

    return getColorRGB(d1, d2 + 0.5, r, g, b);
}

/* This section of code is for the RLOG logging library */
#if (USE_LOGGING == MPID_LOGGING_RLOG)

#define MAX_RANDOM_COLOR_STR 40
static char random_color_str[MAX_RANDOM_COLOR_STR];
static char *get_random_color_str()
{
    unsigned char r,g,b;
    random_color(&r, &g, &b);
    MPIU_Snprintf(random_color_str, MAX_RANDOM_COLOR_STR, 
		  "%3d %3d %3d", (int)r, (int)g, (int)b);
    return random_color_str;
}

static int s_RLOG_Initialized = 0;
int MPIU_Timer_init(int rank, int size)
{
    if (s_RLOG_Initialized)
    {
	/* MPIU_Timer_init already called. */
	return -1;
    }
    g_pRLOG = RLOG_InitLog(rank, size);
    if (g_pRLOG == NULL)
	return -1;

    RLOG_EnableLogging(g_pRLOG);

    RLOG_SaveFirstTimestamp(g_pRLOG);

    RLOG_LogCommID(g_pRLOG, (int)MPI_COMM_WORLD);

    /* arrow state */
    RLOG_DescribeState(g_pRLOG, RLOG_ARROW_EVENT_ID, "Arrow", "255 255 255");

    MPIR_Describe_timer_states();

    s_RLOG_Initialized = 1;
    return MPI_SUCCESS;
}

int MPIU_Timer_finalize()
{
    if (g_pRLOG == NULL)
	return -1;
    if (!s_RLOG_Initialized)
	return 0;

    RLOG_DisableLogging(g_pRLOG);

    MPIU_Msg_printf( "Writing logfile.\n");fflush(stdout);
    RLOG_FinishLog(g_pRLOG);
    MPIU_Msg_printf("finished.\n");fflush(stdout);
    s_RLOG_Initialized = 0;

    return MPI_SUCCESS;
}
#endif /* MPID_LOGGING_RLOG */

#endif /* HAVE_TIMING */
