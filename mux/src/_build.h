// _build.h
//
// $Id: _build.h,v 1.40 2005-06-11 20:03:48 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 2005 Stephen Dennis. 
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd.
// All rights not explicitly given are reserved.
//
#ifndef MUX_BUILD_NUM
extern char szBuildNum[];
#define MUX_BUILD_NUM szBuildNum
#endif // MUX_BUILD_NUM

#ifndef MUX_BUILD_DATE
extern char szBuildDate[];
#define MUX_BUILD_DATE szBuildDate
#endif // MUX_BUILD_DATE

#ifdef BT_ENABLED
#define BT_VER            "+ExileBT"
#else
#define BT_VER            ""
#endif
#define MUX_VERSION       "2.4.0.16" BT_VER  // Version number
#define MUX_RELEASE_DATE  "2005-JUN-11"      // Source release date

// Define if this release is qualified as ALPHA or BETA.
//
#define ALPHA
//#define BETA
