//
// _build.h
// $Id: _build.h,v 1.3 2000-09-18 04:25:26 sdennis Exp $
//
#include "copyright.h"

#ifdef WIN32
extern char szBuildDate[];
extern char szBuildNum[];
extern char szBetaNum;
#endif // WIN32

#define MUX_VERSION       "2.0"         // Base version number
#define PATCHLEVEL         0            // Patch sequence number
#define MUX_RELEASE_DATE  "09/23/99"    // Source release date

#define BETA            1               // Define if a BETA release

