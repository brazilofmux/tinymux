// _build.h
//
// $Id: _build.h,v 1.5 2000-09-18 08:03:42 sdennis Exp $
//
#include "copyright.h"

#ifndef MUX_BUILD_NUM
extern char szBuildNum[];
#define MUX_BUILD_NUM szBuildNum
#endif // MUX_BUILD_NUM

#ifndef MUX_BUILD_DATE
extern char szBuildDate[];
#define MUX_BUILD_DATE szBuildDate
#endif // MUX_BUILD_DATE

#define MUX_VERSION       "2.0.12.278"  // Version number
#define PATCHLEVEL         0            // Patch sequence number
#define MUX_RELEASE_DATE  "2000-SEP-18" // Source release date

#define BETA            1               // Define if a BETA release
