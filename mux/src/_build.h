// _build.h
//
// $Id: _build.h,v 1.65 2003-03-14 15:38:38 sdennis Exp $
//
// MUX 2.2
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#ifndef MUX_BUILD_NUM
extern char szBuildNum[];
#define MUX_BUILD_NUM szBuildNum
#endif // MUX_BUILD_NUM

#ifndef MUX_BUILD_DATE
extern char szBuildDate[];
#define MUX_BUILD_DATE szBuildDate
#endif // MUX_BUILD_DATE

#define MUX_VERSION       "2.2.8.62"     // Version number
#define MUX_RELEASE_DATE  "2003-MAR-15"  // Source release date

// Define if this release is qualified as ALPHA or BETA.
//
//#define ALPHA
//#define BETA
