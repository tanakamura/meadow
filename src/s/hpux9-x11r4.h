/* System description file for hpux version 9 using X11R4.  */

#include "hpux9.h"

#undef  C_SWITCH_X_SYSTEM
#define C_SWITCH_X_SYSTEM -I/usr/include/Motif1.1

#undef  LD_SWITCH_X_DEFAULT
#define LD_SWITCH_X_DEFAULT -L/usr/lib/Motif1.1

