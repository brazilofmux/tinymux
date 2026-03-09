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

#endif // DRIVERSTATE_H
