#ifndef GANL_DEBUG_H
#define GANL_DEBUG_H

// GANL debug logging: OFF unless the build defines GANL_DEBUG (#2049, #2054).
//
// #2054 gated the POSIX engines (epoll, kqueue, select, openssl) on this
// symbol.  Five files were left behind because their macros were hardcoded to
// `do {} while (0)` with the note "stdout/stderr not valid on Windows detached
// process": connection.cpp, io_buffer.cpp, iocp_network_engine.cpp,
// wselect_network_engine.cpp and schannel_transport.cpp.  That note is
// correct, which is why those sites cannot simply copy the POSIX shape --
// std::cerr on a service with no console goes nowhere.  Hardcoding them off
// answered it by making 459 log sites unreachable in every build, on every
// platform, with no way to switch them on.
//
// So the sink is chosen at runtime instead of assumed:
//
//   POSIX             std::cerr, matching the #2054 engines.
//   Windows, console  std::cerr, so a developer running netmux from a shell
//                     sees the same thing a POSIX developer sees.
//   Windows, detached OutputDebugStringA, which is always valid regardless of
//                     whether the process has a console -- readable with
//                     DebugView or any attached debugger.
//
// Nothing here is compiled into a default build.  Without GANL_DEBUG the
// macros expand to `do {} while (0)` exactly as before, `x` is never
// evaluated, and this header pulls in no additional includes.
//
// Cost when it IS on: every site is a formatted insertion plus a flush, which
// is a synchronous write per line on the network event path.  #2049 measured
// that at 40-90% of command throughput on the engines it gated.  This is a
// debugging build, not a slow production build.

#ifdef GANL_DEBUG

#include <sstream>
#include <string>
#include <iostream>

#ifdef _WIN32
// WIN32_LEAN_AND_MEAN keeps <windows.h> from dragging in the old <winsock.h>,
// which collides with the <winsock2.h> the GANL Windows sources use.  NOMINMAX
// suppresses its min/max object-like macros, which otherwise eat the `(` in
// std::max( -- io_buffer.cpp:151 is a real instance, and it is the only thing
// that broke when all 459 sites were compiled for the first time.  Both are
// only reached in a GANL_DEBUG build; a default build is untouched.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ganl {
namespace detail {

inline void debug_emit(const std::string &line)
{
#ifdef _WIN32
    // A detached service has no stderr; GetStdHandle reports that, and
    // OutputDebugStringA works either way.  Checked per call rather than
    // cached: a process can gain or lose a console at runtime, and this
    // path is already dominated by the formatting above it.
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (nullptr == h || INVALID_HANDLE_VALUE == h) {
        OutputDebugStringA(line.c_str());
        return;
    }
#endif
    std::cerr << line << std::flush;
}

}  // namespace detail
}  // namespace ganl

// tag identifies the subsystem, id the socket/handle/connection it concerns.
#define GANL_DEBUG_EMIT(tag, id, x)                                          \
    do {                                                                     \
        std::ostringstream ganl_dbg_;                                        \
        ganl_dbg_ << "[" tag ":" << (id) << "] " << x << "\n";               \
        ::ganl::detail::debug_emit(ganl_dbg_.str());                         \
    } while (0)

// For sites with nothing useful to key on (io_buffer.cpp).
#define GANL_DEBUG_EMIT_NOID(tag, x)                                         \
    do {                                                                     \
        std::ostringstream ganl_dbg_;                                        \
        ganl_dbg_ << "[" tag "] " << x << "\n";                              \
        ::ganl::detail::debug_emit(ganl_dbg_.str());                         \
    } while (0)

#else   // !GANL_DEBUG

#define GANL_DEBUG_EMIT(tag, id, x)   do {} while (0)
#define GANL_DEBUG_EMIT_NOID(tag, x)  do {} while (0)

#endif  // GANL_DEBUG

#endif  // GANL_DEBUG_H
