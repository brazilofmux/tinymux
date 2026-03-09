/*! \file driver_log.h
 * \brief Driver-side logging macros that route through mux_ILog COM interface.
 *
 * Include this AFTER externs.h in driver files (driver.cpp, bsd.cpp, net.cpp,
 * ganl_adapter.cpp, signals.cpp, modules.cpp, version.cpp) to override the
 * engine-side STARTLOG/ENDLOG macros with COM-routed versions.
 */

#ifndef DRIVER_LOG_H
#define DRIVER_LOG_H

#undef STARTLOG
#undef ENDLOG
#undef LOG_SIMPLE

// g_pILog->start_log() checks mudconf.log_options internally, so the driver
// doesn't need to reference mudconf at all.
//
#define STARTLOG(key,p,s) \
    { bool _bLogStarted_ = false; \
    if (  g_pILog \
       && MUX_SUCCEEDED(g_pILog->start_log(&_bLogStarted_, (key), T(p), T(s))) \
       && _bLogStarted_) {

#define ENDLOG \
    g_pILog->end_log(); } }

#define LOG_SIMPLE(key,p,s,m) \
    STARTLOG(key,p,s) \
        g_pILog->log_text(m); \
    ENDLOG

#endif // DRIVER_LOG_H
