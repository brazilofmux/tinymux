// _build.h
//
// $Id: _build.h,v 1.11 2003-04-04 15:11:15 sdennis Exp $
//
// MUX 2.3
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

#define MUX_VERSION       "2.3.0.9"      // Version number
#define MUX_RELEASE_DATE  "2003-APR-04"  // Source release date

// Define if this release is qualified as ALPHA or BETA.
//
#define ALPHA
//#define BETA
