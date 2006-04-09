/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*  
 *  (C) 2005 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef MPIDBG_H_INCLUDED
#define MPIDBG_H_INCLUDED

#ifdef USE_DBG_LOGGING
#define MPIU_DBG_MSG(_class,_level,_string)  \
   {if ( (MPIU_DBG_##_class & MPIU_DBG_ActiveClasses) && \
          MPIU_DBG_##_level <= MPIU_DBG_MaxLevel ) {\
     MPIU_DBG_Outevent( __FILE__, __LINE__, MPIU_DBG_##_class, 0, _string ); }}
#define MPIU_DBG_MSG_S(_class,_level,_fmat,_string) \
   {if ( (MPIU_DBG_##_class & MPIU_DBG_ActiveClasses) && \
          MPIU_DBG_##_level <= MPIU_DBG_MaxLevel ) {\
     MPIU_DBG_Outevent( __FILE__, __LINE__, MPIU_DBG_##_class, 1, _fmat, _string ); }}
#define MPIU_DBG_MSG_D(_class,_level,_fmat,_int) \
   {if ( (MPIU_DBG_##_class & MPIU_DBG_ActiveClasses) && \
          MPIU_DBG_##_level <= MPIU_DBG_MaxLevel ) {\
     MPIU_DBG_Outevent( __FILE__, __LINE__, MPIU_DBG_##_class, 2, _fmat, _int ); }}

#define MPIU_DBG_MAXLINE 256
#define MPIU_DBG_FDEST _s,MPIU_DBG_MAXLINE
#define MPIU_DBG_MSG_FMT(_class,_level,_fmatargs) \
   {if ( (MPIU_DBG_##_class & MPIU_DBG_ActiveClasses) && \
          MPIU_DBG_##_level <= MPIU_DBG_MaxLevel ) {\
          char _s[MPIU_DBG_MAXLINE]; \
          MPIU_Snprintf _fmatargs ; \
     MPIU_DBG_Outevent( __FILE__, __LINE__, MPIU_DBG_##_class, 0, _s ); }}
#else
#define MPIU_DBG_MSG(_class,_level,_string) 
#define MPIU_DBG_MSG_S(_class,_level,_fmat,_string)
#define MPIU_DBG_MSG_D(_class,_level,_fmat,_int)
#define MPIU_DBG_MSG_FMT(_class,_level,_fmatargs)
#endif

/* Special constants */
enum MPIU_DBG_LEVEL { MPIU_DBG_TERSE   = 0, 
		      MPIU_DBG_TYPICAL = 50,
		      MPIU_DBG_VERBOSE = 99 };
/* Any change in MPIU_DBG_CLASS must be matched by changes in 
   MPIU_Classname and MPIU_Classbits in src/util/dbg/dbg_printf.c */
enum MPIU_DBG_CLASS { MPIU_DBG_PT2PT         = 0x1,
		      MPIU_DBG_RMA           = 0x2,
		      MPIU_DBG_THREAD        = 0x4,
		      MPIU_DBG_PM            = 0x8,
		      MPIU_DBG_ROUTINE_ENTER = 0x10,
		      MPIU_DBG_ROUTINE_EXIT  = 0x20,
		      MPIU_DBG_SYSCALL       = 0x40,
		      MPIU_DBG_CH3_CONNECT   = 0x80,
		      MPIU_DBG_CH3_PROGRESS  = 0x100,
		      MPIU_DBG_CH3           = 0x180,
		      MPIU_DBG_ALL           = (~0) };

extern int MPIU_DBG_ActiveClasses;
extern int MPIU_DBG_MaxLevel;
int MPIU_DBG_Outevent(const char *, int, int, int, const char *, ...) 
                                        ATTRIBUTE((format(printf,5,6)));
int MPIU_DBG_Init( int *, char ***, int );

#endif
