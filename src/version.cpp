/*
 * version.c - version information 
 */
/*
 * $Id: version.cpp,v 1.6 2000-05-24 17:35:58 sdennis Exp $ 
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "mudconf.h"
#include "alloc.h"
#include "patchlevel.h"

void do_version(dbref player, dbref cause, int extra)
{
    char *buff;

    notify(player, mudstate.version);
    buff = alloc_mbuf("do_version");
    sprintf(buff, "Build date: %s", MUX_BUILD_DATE);
    notify(player, buff);
    free_mbuf(buff);
}

void NDECL(init_version)
{
#ifdef BETA
#if PATCHLEVEL > 0
    sprintf(mudstate.version, "MUX Beta 10 version %s patchlevel %d #%s",
        MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM);
#else
    sprintf(mudstate.version, "MUX Beta 10 version %s #%s",
        MUX_VERSION, MUX_BUILD_NUM);
#endif // PATCHLEVEL
#else // BETA

#if PATCHLEVEL > 0
    sprintf(mudstate.version, "MUX version %s patchlevel %d #%s [%s]",
        MUX_VERSION, PATCHLEVEL, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#else
    sprintf(mudstate.version, "MUX version %s #%s [%s]",
        MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
#endif // PATCHLEVEL
#endif // BETA

    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text((char *)"Starting: ");
    log_text(mudstate.version);
    ENDLOG;
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text((char *)"Build date: ");
    log_text((char *)MUX_BUILD_DATE);
    ENDLOG;
}
