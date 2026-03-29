/*! \file routing.h
 * \brief Routing: per-zone next-hop tables with cross-zone meta-table.
 *
 * BFS over NAVIGABLE rooms, compressed next-hop tables partitioned by
 * zone, gateway-edge meta-table for cross-zone Dijkstra.
 * See docs/design-routing.md.
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

// Invalidate all zone tables and the meta-table.  Backward-compatible
// catch-all used by most topology-changing commands.
//
void route_invalidate(void);

// Invalidate one zone's local table.  Use when the change is known to
// affect only one zone (e.g., NAVIGABLE flag toggle on a room).
//
void route_invalidate_zone(dbref zone_id);

// Invalidate the inter-zone meta-table.  Use when cross-zone exits are
// created or destroyed, or when a room changes zones.
//
void route_invalidate_meta(void);

// Core query: look up a route from source to destination.
// Writes the result into buff/bufc using the standard softcode output
// convention.  options is a bitmask of ROUTE_OPT_*.
//
void route_query(dbref executor, dbref source, dbref destination,
                 int options, UTF8 *buff, UTF8 **bufc);

#endif // !ROUTING_H
