//
// version.cpp - version information 
//
// $Id: version.cpp,v 1.11 2000-09-18 04:25:26 sdennis Exp $ 
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "mudconf.h"
#include "alloc.h"
#include "_build.h"
#ifdef WIN32
#define MUX_BUILD_DATE szBuildDate
#endif // WIN32

void do_version(dbref player, dbref cause, int extra)
{
    notify(player, mudstate.version);
    char *buff = alloc_mbuf("do_version");
    sprintf(buff, "Build date: %s", MUX_BUILD_DATE);
    notify(player, buff);
    free_mbuf(buff);
}

void NDECL(init_version)
{
#ifdef WIN32
#ifdef BETA
#if PATCHLEVEL > 0
    sprintf( mudstate.version,
             "MUX for Win32 Beta %s version %s patchlevel %d #%s",
             szBetaNum, MUX_VERSION, PATCHLEVEL, szBuildNum);
#else // PATCHLEVEL
    sprintf( mudstate.version,
             "MUX for Win32 Beta %s version %s #%s",
             szBetaNum, MUX_VERSION, szBuildNum);
#endif // PATCHLEVEL
#else // BETA
#if PATCHLEVEL > 0 
    sprintf( mudstate.version,
             "MUX for Win32 version %s patchlevel %d #%s [%s]",
             MUX_VERSION, PATCHLEVEL, szBuildNum, MUX_RELEASE_DATE);
#else // PATCHLEVEL
    sprintf( mudstate.version,
             "MUX for Win32 version %s #%s [%s]",
             MUX_VERSION, szBuildNum, MUX_RELEASE_DATE);
#endif // PATCHLEVEL
#endif // BETA
#else // WIN32
#ifdef BETA
#if PATCHLEVEL > 0
    sprintf(mudstate.version, "MUX Beta 12 version %s patchlevel %d #%s",
        MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM);
#else // PATCHLEVEL
    sprintf(mudstate.version, "MUX Beta 12 version %s #%s", MUX_VERSION,
        MUX_BUILD_NUM);
#endif // PATCHLEVEL
#else // BETA
#if PATCHLEVEL > 0
    sprintf(mudstate.version, "MUX version %s patchlevel %d #%s [%s]",
        MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#else // PATCHLEVEL
    sprintf(mudstate.version, "MUX version %s #%s [%s]", MUX_VERSION,
        MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif // PATCHLEVEL
#endif // BETA
#endif // WIN32

    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text("Starting: ");
    log_text(mudstate.version);
    ENDLOG;
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text("Build date: ");
    log_text(MUX_BUILD_DATE);
    ENDLOG;
}
