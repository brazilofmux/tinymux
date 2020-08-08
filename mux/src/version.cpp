/*! \file version.cpp
 * \brief Version information.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "_build.h"
#include "command.h"

void do_version(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    notify(executor, mudstate.version);
    UTF8 *buff = alloc_mbuf("do_version");
    mux_sprintf(buff, MBUF_SIZE, T("Build date: %s"), MUX_BUILD_DATE);
    notify(executor, buff);
    free_mbuf(buff);
}

void build_version(void)
{
#if defined(WIN64)
#if defined(ALPHA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win64 #%s [ALPHA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Alpha Win64"), MUX_VERSION);
#elif defined(BETA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win64 #%s [BETA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Beta Win64"), MUX_VERSION);
#else // RELEASED
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win64 #%s [%s]"), MUX_VERSION, MUX_BUILD_NUM,
            MUX_RELEASE_DATE);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Win64"), MUX_VERSION);
#endif // ALPHA, BETA, RELEASED
#elif defined(WIN32)
#if defined(ALPHA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win32 #%s [ALPHA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Alpha Win32"), MUX_VERSION);
#elif defined(BETA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win32 #%s [BETA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Beta Win32"), MUX_VERSION);
#else // RELEASED
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s for Win32 #%s [%s]"), MUX_VERSION, MUX_BUILD_NUM,
            MUX_RELEASE_DATE);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Win32"), MUX_VERSION);
#endif // ALPHA, BETA, RELEASED
#else // WIN32
#if defined(ALPHA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s #%s [ALPHA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Alpha"), MUX_VERSION);
#elif defined(BETA)
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s #%s [BETA]"), MUX_VERSION, MUX_BUILD_NUM);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s Beta"), MUX_VERSION);
#else // RELEASED
        mux_sprintf(mudstate.version, sizeof(mudstate.version),
            T("MUX %s #%s [%s]"), MUX_VERSION, MUX_BUILD_NUM, MUX_RELEASE_DATE);
        mux_sprintf(mudstate.short_ver, sizeof(mudstate.short_ver),
            T("MUX %s"), MUX_VERSION);
#endif // ALPHA, BETA, RELEASED
#endif // WIN32
}

void init_version(void)
{
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text(T("Starting: "));
    log_text(mudstate.version);
    ENDLOG;
    STARTLOG(LOG_ALWAYS, "INI", "START");
    log_text(T("Build date: "));
    log_text((UTF8 *)MUX_BUILD_DATE);
    ENDLOG;
}
