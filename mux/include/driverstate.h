/*! \file driverstate.h
 * \brief Driver-local state that does not cross into the engine.
 *
 * These globals were previously stored in mudstate (STATEDATA) but are
 * only read/written by driver-side code.  The engine accesses descriptor
 * containers through the mux_IConnectionManager COM interface.
 *
 * Fields that are shared between driver and engine (debug_cmd,
 * curr_executor, etc.) stay in mudstate for now and will migrate to
 * COM interfaces as those are added.
 */

#ifndef DRIVERSTATE_H
#define DRIVERSTATE_H

#include <list>
#include <unordered_map>
#include <map>

// Forward declarations.
//
struct descriptor_data;
typedef struct descriptor_data DESC;

// Descriptor tracking containers.  The engine accesses these through
// the mux_IConnectionManager COM interface; only the driver mutates
// them directly.
//
struct DriverPointerHasher {
    size_t operator()(const DESC *p) const noexcept {
        return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(p));
    }
};

extern std::list<DESC*> g_descriptors_list;
extern std::unordered_map<DESC*, std::list<DESC*>::iterator, DriverPointerHasher> g_descriptors_map;
extern std::multimap<dbref, DESC*> g_dbref_to_descriptors_map;

// Version strings — built by build_version() (driver), read by driver.
//
extern UTF8 g_version[128];
extern UTF8 g_short_ver[64];

// Driver-side COM interface pointers.  The driver creates these via
// mux_CreateInstance after init_modules/LoadGame.
//
class mux_IGameEngine;
class mux_IPlayerSession;
class mux_INotify;
class mux_IObjectInfo;
class mux_ISlaveControl;
extern mux_IGameEngine  *g_pIGameEngine;
extern mux_IPlayerSession *g_pIPlayerSession;
extern mux_INotify        *g_pINotify;
extern mux_IObjectInfo    *g_pIObjectInfo;

#if defined(STUB_SLAVE)
// StubSlave lifetime management.  The driver creates this proxy via
// mux_CreateInstance(CID_StubSlave, UseSlaveProcess) and uses it to
// start up and shut down the stubslave process.  Engine-side code that
// needs mux_ISlaveControl obtains its own proxy through libmux.
//
extern mux_ISlaveControl *g_pISlaveControl;
#endif // STUB_SLAVE

// CLI mode flag — true when running as dbconvert.  The engine has its
// own copy (mudstate.bStandAlone) set in CGameEngine::DbConvert().
//
extern bool g_bStandAlone;

// Shutdown flag — set by signal handlers (driver) or by
// mux_IDriverControl::ShutdownRequest() (engine's @shutdown command).
// The GANL main loop checks this to know when to exit.
//
// volatile sig_atomic_t for safe access from signal handlers.
//
extern volatile sig_atomic_t g_shutdown_flag;

// Restart flag — driver sets during @restart sequence, engine reads
// via mux_IDriverControl::GetRestarting().
//
extern bool g_restarting;

// Panic guard — prevents recursive signal handler entry.  If we
// SIGSEGV inside a SIGSEGV handler, we know we're the biggest risk
// to the data and bail immediately.
//
// volatile sig_atomic_t for safe access from signal handlers.
//
extern volatile sig_atomic_t g_panicking;

// Dump child PID — set by SIGCHLD handler, consumed by main loop.
// Nonzero means a dump child exited; the main loop reports it to
// the engine via DumpChildExited() COM call where it's safe.
//
extern volatile pid_t g_dump_child_pid;

// Site access list — driver owns, engine mutates via
// mux_IDriverControl::SiteUpdate().  Driver reads directly (hot path).
//
extern mux_subnets g_access_list;

// Logged-out command table — built and used only by driver.
// StringPtrMap is defined in mudconf.h (included via externs.h).
//
extern StringPtrMap g_logout_cmd_htab;

#endif // DRIVERSTATE_H
