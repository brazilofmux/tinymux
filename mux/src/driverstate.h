/*! \file driverstate.h
 * \brief Driver-local state that does not cross into the engine.
 *
 * These globals were previously stored in mudstate (STATEDATA) but are
 * only read/written by driver-side code.  The engine accesses descriptor
 * containers through the mux_IConnectionManager COM interface.
 *
 * Fields that are shared between driver and engine (shutdown_flag,
 * debug_cmd, curr_executor, etc.) stay in mudstate for now and will
 * migrate to COM interfaces as those are added.
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
extern mux_IGameEngine  *g_pIGameEngine;
extern mux_IPlayerSession *g_pIPlayerSession;

// Logged-out command table — built and used only by driver.
// StringPtrMap is defined in mudconf.h (included via externs.h).
//
extern StringPtrMap g_logout_cmd_htab;

#endif // DRIVERSTATE_H
