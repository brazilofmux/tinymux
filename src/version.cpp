//
// version.cpp - version information 
//
// $Id: version.cpp,v 1.16 2001-02-11 00:07:44 sdennis Exp $ 
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "mudconf.h"
#include "alloc.h"
#include "_build.h"

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
#ifdef ALPHA
#if PATCHLEVEL > 0
        sprintf( mudstate.version, "MUX %sp%d for Win32 #%s [ALPHA]",
            MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %sp%d Alpha Win32", MUX_VERSION, PATCHLEVEL);
#else // PATCHLEVEL
        sprintf( mudstate.version, "MUX %s for Win32 #%s [ALPHA]",
            MUX_VERSION, MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %s Alpha Win32", MUX_VERSION);
#endif // PATCHLEVEL
#else // ALPHA
#if PATCHLEVEL > 0 
        sprintf( mudstate.version, "MUX %sp%d for Win32 #%s [%s]",
            MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %sp%d Win32", MUX_VERSION, PATCHLEVEL);
#else // PATCHLEVEL
        sprintf( mudstate.version, "MUX %s for Win32 #%s [%s]",
            MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %s Win32", MUX_VERSION);
#endif // PATCHLEVEL
#endif // ALPHA
#else // WIN32
#ifdef ALPHA
#if PATCHLEVEL > 0
        sprintf( mudstate.version, "MUX %sp%d #%s [ALPHA]", MUX_VERSION,
            PATCHLEVEL, MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %sp%d Alpha", MUX_VERSION, PATCHLEVEL);
#else // PATCHLEVEL
        sprintf( mudstate.version, "MUX %s #%s [ALPHA]", MUX_VERSION,
            MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %s Alpha", MUX_VERSION);
#endif // PATCHLEVEL
#else // ALPHA
#if PATCHLEVEL > 0 
        sprintf( mudstate.version, "MUX %sp%d #%s [%s]", MUX_VERSION,
            PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %sp%d", MUX_VERSION, PATCHLEVEL);
#else // PATCHLEVEL
        sprintf( mudstate.version, "MUX %s #%s [%s]", MUX_VERSION,
            MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %s Win32", MUX_VERSION);
#endif // PATCHLEVEL
#endif // ALPHA
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
