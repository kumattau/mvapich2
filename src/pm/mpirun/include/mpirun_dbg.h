#ifndef _MPIRUN_DBG_H
#define _MPIRUN_DBG_H

/**
 *  Totalview intercepts MPIR_Breakpoint
 */
int MPIR_Breakpoint (void);

//#define SPAWN_DEBUG
#ifdef SPAWN_DEBUG
#define DBG(_stmt_) _stmt_;
#else
#define DBG(_stmt_)
#endif


#define TOTALVIEW_CMD_LEN       200

/* Start mpirun_rsh totalview integration */

#define MPIR_DEBUG_SPAWNED                1
#define MPIR_DEBUG_ABORTING               2

extern struct MPIR_PROCDESC *MPIR_proctable;
extern int MPIR_proctable_size;
extern int MPIR_i_am_starter;
extern int MPIR_debug_state;
extern char *MPIR_dll_name;
extern char *MV2_XRC_FILE;

extern int use_totalview;

/* End mpirun_rsh totalview integration */

extern int debug_on;
extern int param_debug;
extern int use_totalview;

#endif
