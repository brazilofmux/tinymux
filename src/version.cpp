//
// version.cpp - version information 
//
// $Id: version.cpp,v 1.18 2001-07-03 19:44:18 sdennis Exp $ 
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
        sprintf( mudstate.version, "MUX %s for Win32 #%s [ALPHA]",
            MUX_VERSION, MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %s Alpha Win32", MUX_VERSION);
#else // ALPHA
        sprintf( mudstate.version, "MUX %s for Win32 #%s [%s]",
            MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %s Win32", MUX_VERSION);
#endif // ALPHA
#else // WIN32
#ifdef ALPHA
        sprintf( mudstate.version, "MUX %s #%s [ALPHA]", MUX_VERSION,
            MUX_BUILD_NUM);
        sprintf( mudstate.short_ver, "MUX %s Alpha", MUX_VERSION);
#else // ALPHA
        sprintf( mudstate.version, "MUX %s #%s [%s]", MUX_VERSION,
            MUX_BUILD_NUM, MUX_RELEASE_DATE);
        sprintf( mudstate.short_ver, "MUX %s", MUX_VERSION);
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
