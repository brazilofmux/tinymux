// version.cpp -- Version information.
//
// $Id: version.cpp,v 1.3 2006-01-08 20:12:48 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "_build.h"
#include "command.h"

void do_version(dbref executor, dbref caller, dbref enactor, int extra)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(extra);

    notify(executor, mudstate.version);
    char *buff = alloc_mbuf("do_version");
    mux_sprintf(buff, MBUF_SIZE, "Build date: %s", MUX_BUILD_DATE);
    notify(executor, buff);
    free_mbuf(buff);
}

void build_version(void)
{
#ifdef WIN32
#if defined(ALPHA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s for Win32 #%s [ALPHA]", MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s Alpha Win32", MUX_VERSION);
#elif defined(BETA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s for Win32 #%s [BETA]", MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s Beta Win32", MUX_VERSION);
#else // RELEASED
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s for Win32 #%s [%s]", MUX_VERSION, MUX_BUILD_NUM,
            MUX_RELEASE_DATE);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s Win32", MUX_VERSION);
#endif // ALPHA, BETA, RELEASED
#else // WIN32
#if defined(ALPHA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s #%s [ALPHA]", MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s Alpha", MUX_VERSION);
#elif defined(BETA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s #%s [BETA]", MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s Beta", MUX_VERSION);
#else // RELEASED
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            "MUX %s #%s [%s]", MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            "MUX %s", MUX_VERSION);
#endif // ALPHA, BETA, RELEASED
#endif // WIN32
}

void init_version(void)
{
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text("Starting: ");
    log_text(mudstate.version);
    ENDLOG;
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text("Build date: ");
    log_text(MUX_BUILD_DATE);
    ENDLOG;
}
