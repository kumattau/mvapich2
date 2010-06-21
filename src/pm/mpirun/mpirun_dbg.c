#include "mpirun_dbg.h"

/* Start mpirun_rsh totalview integration */

struct MPIR_PROCDESC *MPIR_proctable = 0;
int MPIR_proctable_size = 0;
int MPIR_i_am_starter = 1;
int MPIR_debug_state = 0;
char *MPIR_dll_name = "MVAPICH2";
char *MV2_XRC_FILE;

/* End mpirun_rsh totalview integration */

int debug_on = 0;
int param_debug = 0;
int use_totalview = 0;


/**
 *  Totalview intercepts MPIR_Breakpoint
 */
int MPIR_Breakpoint(void)
{
    return 0;
}
