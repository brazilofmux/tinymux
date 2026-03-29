/*! \file routing.h
 * \brief Phase 1 routing: static unconditional next-hop tables.
 *
 * BFS over NAVIGABLE rooms, compressed next-hop table, generation-counter
 * invalidation, SQLite persistence.  See docs/design-routing.md.
 */

#ifndef ROUTING_H
#define ROUTING_H

#include "copyright.h"

// Routing option flags (bitmask, passed as options argument to route()).
//
constexpr int ROUTE_OPT_DISTANCE = 0x01;  // Return hop count, not exit dbref.
constexpr int ROUTE_OPT_PATH     = 0x02;  // Return full exit list.
constexpr int ROUTE_OPT_REBUILD  = 0x04;  // Force table recomputation.

// Initialize the routing subsystem.  Called once at startup after the
// database is fully loaded.
//
void route_init(void);

// Shut down the routing subsystem.  Called at shutdown.
//
void route_shutdown(void);

// Bump the routing generation counter, marking the cached table stale.
// Called by topology-changing commands (@dig, @destroy, @link, @open,
// @unlink) and by the NAVIGABLE flag set/clear handler.
//
void route_invalidate(void);

// Core query: look up a route from source to destination.
// Writes the result into buff/bufc using the standard softcode output
// convention.  options is a bitmask of ROUTE_OPT_*.
//
void route_query(dbref executor, dbref source, dbref destination,
                 int options, UTF8 *buff, UTF8 **bufc);

#endif // !ROUTING_H
