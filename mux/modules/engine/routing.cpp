/*! \file routing.cpp
 * \brief Routing: per-zone next-hop tables with cross-zone meta-table.
 *
 * Implements BFS-based shortest-path routing over rooms marked NAVIGABLE,
 * partitioned by zone.  Each zone has an independent routing table with
 * its own generation counter.  Cross-zone routing uses a gateway-edge
 * meta-table with Dijkstra over the (small) zone graph.
 *
 * Compression techniques per zone:
 *   1. Diagonal elimination  -- source == dest needs no entry.
 *   2. Row redundancy        -- if every reachable destination funnels
 *      through the same exit, store a single "always(exit)" sentinel.
 *
 * See docs/design-routing.md for the full design.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "flags.h"
#include "routing.h"
#include "sqlite_backend.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <climits>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Data structures.
// ---------------------------------------------------------------------------

struct ExitEdge
{
    dbref exit_dbref;
    dbref dest_room;
};

// Per-room routing row within a zone.
//
struct RouteRow
{
    dbref always_exit;                          // NOTHING if not compressed.
    std::unordered_map<dbref, dbref> next_hop;  // dest_room -> exit dbref.
};

// Per-zone routing table.
//
struct ZoneTable
{
    int  generation;                            // Bumped on invalidation.
    int  table_generation;                      // Generation at last rebuild.
    bool valid;

    std::unordered_map<dbref, int> node_index;  // room -> dense index.
    std::vector<dbref> index_to_room;           // dense index -> room.
    std::vector<RouteRow> table;                // indexed by dense index.
    std::vector<std::vector<ExitEdge>> adj;     // adjacency lists.

    ZoneTable() : generation(0), table_generation(-1), valid(false) {}
};

// Gateway edge: a cross-zone exit connecting two zones.
//
struct GatewayEdge
{
    dbref source_zone;
    dbref dest_zone;
    dbref gate_room;       // Room in source_zone containing the exit.
    dbref gate_exit;       // The exit itself.
    dbref target_room;     // Room in dest_zone the exit leads to.
};

// Meta-table for inter-zone routing.
//
struct MetaTable
{
    int  generation;
    int  table_generation;
    bool valid;

    std::vector<GatewayEdge> edges;

    MetaTable() : generation(0), table_generation(-1), valid(false) {}
};

// ---------------------------------------------------------------------------
// Global state.
// ---------------------------------------------------------------------------

// All navigable rooms, keyed by room dbref -> zone dbref.
//
static std::unordered_map<dbref, dbref>     g_room_to_zone;

// Per-zone tables, keyed by zone dbref (NOTHING for orphan rooms).
//
static std::unordered_map<dbref, ZoneTable> g_zone_tables;

// Meta-table for cross-zone routing.
//
static MetaTable                            g_meta;

// Global node index across all zones (for navigable-check in route_query).
//
static std::unordered_set<dbref>            g_all_navigable;

// ---------------------------------------------------------------------------
// Forward declarations.
// ---------------------------------------------------------------------------

static void route_scan_navigable(void);
static void route_build_zone(dbref zone_id);
static void route_build_meta(void);
static void route_ensure_zone_current(dbref zone_id);
static void route_ensure_meta_current(void);
static void route_persist_to_sqlite(void);

// ---------------------------------------------------------------------------
// Initialization / shutdown.
// ---------------------------------------------------------------------------

void route_init(void)
{
    g_room_to_zone.clear();
    g_zone_tables.clear();
    g_all_navigable.clear();
    g_meta = MetaTable();
}

void route_shutdown(void)
{
    g_room_to_zone.clear();
    g_zone_tables.clear();
    g_all_navigable.clear();
    g_meta = MetaTable();
}

void route_invalidate(void)
{
    for (auto &kv : g_zone_tables)
    {
        kv.second.generation++;
    }
    g_meta.generation++;

    // Also invalidate the navigable scan so new rooms are picked up.
    //
    g_all_navigable.clear();
    g_room_to_zone.clear();
}

void route_invalidate_zone(dbref zone_id)
{
    auto it = g_zone_tables.find(zone_id);
    if (it != g_zone_tables.end())
    {
        it->second.generation++;
    }

    // A zone-local change might also affect gateway edges (e.g., a new
    // room at the border).  Invalidate the scan and meta conservatively.
    //
    g_all_navigable.clear();
    g_room_to_zone.clear();
    g_meta.generation++;
}

void route_invalidate_meta(void)
{
    g_meta.generation++;
    g_all_navigable.clear();
    g_room_to_zone.clear();
}

// ---------------------------------------------------------------------------
// Scan all navigable rooms and partition by zone.
// ---------------------------------------------------------------------------

static void route_scan_navigable(void)
{
    if (!g_all_navigable.empty())
    {
        return;  // Already scanned since last invalidation.
    }

    g_room_to_zone.clear();

    // Discover all navigable rooms and their zones.
    //
    dbref thing;
    DO_WHOLE_DB(thing)
    {
        if (  isRoom(thing)
           && !isGarbage(thing)
           && Navigable(thing))
        {
            dbref z = Zone(thing);
            g_room_to_zone[thing] = z;
            g_all_navigable.insert(thing);

            // Ensure a ZoneTable entry exists (preserve generation if
            // already present).
            //
            if (g_zone_tables.find(z) == g_zone_tables.end())
            {
                g_zone_tables[z] = ZoneTable();
            }
        }
    }

    // Prune zone tables for zones that no longer have navigable rooms.
    //
    std::unordered_set<dbref> active_zones;
    for (const auto &kv : g_room_to_zone)
    {
        active_zones.insert(kv.second);
    }
    for (auto it = g_zone_tables.begin(); it != g_zone_tables.end(); )
    {
        if (active_zones.find(it->first) == active_zones.end())
        {
            it = g_zone_tables.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Collect edges for a single room (walks parent chain).
// ---------------------------------------------------------------------------

static void collect_room_edges(
    dbref room,
    const std::unordered_map<dbref, int> &node_idx,
    std::vector<ExitEdge> &edges)
{
    edges.clear();

    std::unordered_set<dbref> seen_exits;
    int level;
    dbref parent;
    ITER_PARENTS(room, parent, level)
    {
        dbref exit_obj;
        DOLIST(exit_obj, Exits(parent))
        {
            if (  !isExit(exit_obj)
               || !seen_exits.insert(exit_obj).second)
            {
                continue;
            }

            const UTF8 *vdest = atr_get_raw(exit_obj, A_EXITVARDEST);
            if (vdest && *vdest)
            {
                continue;
            }

            dbref dest = Location(exit_obj);
            if (  !Good_obj(dest)
               || !isRoom(dest))
            {
                continue;
            }

            // Destination must be navigable (in any zone).
            //
            if (g_all_navigable.find(dest) == g_all_navigable.end())
            {
                continue;
            }

            // For zone-local edges, destination must be in the same zone
            // OR we accept it as a gateway edge if it's in a different
            // zone.  The node_idx check restricts to same-zone nodes.
            // Cross-zone edges are still recorded (dest is navigable)
            // but won't match node_idx -- that's fine, they are handled
            // by the meta-table.
            //
            auto it = node_idx.find(dest);
            if (it == node_idx.end())
            {
                continue;
            }

            ExitEdge edge;
            edge.exit_dbref = exit_obj;
            edge.dest_room  = dest;
            edges.push_back(edge);
        }
    }
}

// ---------------------------------------------------------------------------
// BFS from a single source within a zone.
// ---------------------------------------------------------------------------

static void bfs_from_source(
    int source_idx,
    const std::vector<std::vector<ExitEdge>> &adj,
    const std::unordered_map<dbref, int> &node_idx,
    const std::vector<dbref> &idx_to_room,
    RouteRow &row)
{
    int n = static_cast<int>(idx_to_room.size());
    row.always_exit = NOTHING;
    row.next_hop.clear();

    std::vector<bool> visited(n, false);
    std::vector<dbref> first_exit(n, NOTHING);
    std::queue<int> queue;

    visited[source_idx] = true;

    for (const auto &edge : adj[source_idx])
    {
        auto it = node_idx.find(edge.dest_room);
        if (it == node_idx.end())
        {
            continue;
        }
        int dest_idx = it->second;
        if (!visited[dest_idx])
        {
            visited[dest_idx] = true;
            first_exit[dest_idx] = edge.exit_dbref;
            queue.push(dest_idx);
        }
    }

    while (!queue.empty())
    {
        int cur = queue.front();
        queue.pop();

        for (const auto &edge : adj[cur])
        {
            auto it = node_idx.find(edge.dest_room);
            if (it == node_idx.end())
            {
                continue;
            }
            int next_idx = it->second;
            if (!visited[next_idx])
            {
                visited[next_idx] = true;
                first_exit[next_idx] = first_exit[cur];
                queue.push(next_idx);
            }
        }
    }

    for (int i = 0; i < n; i++)
    {
        if (i == source_idx)
        {
            continue;
        }
        if (first_exit[i] != NOTHING)
        {
            row.next_hop[idx_to_room[i]] = first_exit[i];
        }
    }

    // Row redundancy compression: only safe when all non-diagonal
    // destinations are reachable.
    //
    if (  !row.next_hop.empty()
       && row.next_hop.size() == static_cast<size_t>(n - 1))
    {
        dbref candidate = row.next_hop.begin()->second;
        bool all_same = true;
        for (const auto &kv : row.next_hop)
        {
            if (kv.second != candidate)
            {
                all_same = false;
                break;
            }
        }
        if (all_same)
        {
            row.always_exit = candidate;
            row.next_hop.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Build one zone's routing table.
// ---------------------------------------------------------------------------

static void route_build_zone(dbref zone_id)
{
    route_scan_navigable();

    auto zt_it = g_zone_tables.find(zone_id);
    if (zt_it == g_zone_tables.end())
    {
        return;
    }
    ZoneTable &zt = zt_it->second;

    zt.node_index.clear();
    zt.index_to_room.clear();

    // Collect rooms in this zone.
    //
    for (const auto &kv : g_room_to_zone)
    {
        if (kv.second == zone_id)
        {
            int idx = static_cast<int>(zt.index_to_room.size());
            zt.node_index[kv.first] = idx;
            zt.index_to_room.push_back(kv.first);
        }
    }

    int n = static_cast<int>(zt.index_to_room.size());
    zt.adj.resize(n);

    for (int i = 0; i < n; i++)
    {
        collect_room_edges(zt.index_to_room[i], zt.node_index, zt.adj[i]);
    }

    zt.table.resize(n);
    for (int i = 0; i < n; i++)
    {
        bfs_from_source(i, zt.adj, zt.node_index, zt.index_to_room,
                        zt.table[i]);
    }

    zt.table_generation = zt.generation;
    zt.valid = true;
}

// ---------------------------------------------------------------------------
// Build the inter-zone meta-table: collect gateway edges.
// ---------------------------------------------------------------------------

static void route_build_meta(void)
{
    route_scan_navigable();

    g_meta.edges.clear();

    // Scan all navigable rooms.  For each exit that leads to a room in
    // a different zone, emit a gateway edge.
    //
    for (const auto &kv : g_room_to_zone)
    {
        dbref room = kv.first;
        dbref src_zone = kv.second;

        std::unordered_set<dbref> seen_exits;
        int level;
        dbref parent;
        ITER_PARENTS(room, parent, level)
        {
            dbref exit_obj;
            DOLIST(exit_obj, Exits(parent))
            {
                if (  !isExit(exit_obj)
                   || !seen_exits.insert(exit_obj).second)
                {
                    continue;
                }

                const UTF8 *vdest = atr_get_raw(exit_obj, A_EXITVARDEST);
                if (vdest && *vdest)
                {
                    continue;
                }

                dbref dest = Location(exit_obj);
                if (  !Good_obj(dest)
                   || !isRoom(dest))
                {
                    continue;
                }

                if (g_all_navigable.find(dest) == g_all_navigable.end())
                {
                    continue;
                }

                dbref dst_zone = g_room_to_zone[dest];
                if (dst_zone == src_zone)
                {
                    continue;  // Intra-zone, not a gateway.
                }

                GatewayEdge ge;
                ge.source_zone = src_zone;
                ge.dest_zone   = dst_zone;
                ge.gate_room   = room;
                ge.gate_exit   = exit_obj;
                ge.target_room = dest;
                g_meta.edges.push_back(ge);
            }
        }
    }

    g_meta.table_generation = g_meta.generation;
    g_meta.valid = true;
}

// ---------------------------------------------------------------------------
// Ensure tables are current.
// ---------------------------------------------------------------------------

static void route_ensure_zone_current(dbref zone_id)
{
    route_scan_navigable();

    auto it = g_zone_tables.find(zone_id);
    if (it == g_zone_tables.end())
    {
        return;
    }
    if (it->second.valid && it->second.table_generation == it->second.generation)
    {
        return;
    }
    route_build_zone(zone_id);
    route_persist_to_sqlite();
}

static void route_ensure_meta_current(void)
{
    route_scan_navigable();

    if (g_meta.valid && g_meta.table_generation == g_meta.generation)
    {
        return;
    }
    route_build_meta();
    route_persist_to_sqlite();
}

// ---------------------------------------------------------------------------
// Intra-zone next-hop lookup.
// ---------------------------------------------------------------------------

static dbref zone_next_hop(const ZoneTable &zt, dbref source, dbref destination)
{
    auto src_it = zt.node_index.find(source);
    if (src_it == zt.node_index.end())
    {
        return NOTHING;
    }

    const RouteRow &row = zt.table[src_it->second];
    if (row.always_exit != NOTHING)
    {
        return row.always_exit;
    }

    auto hop_it = row.next_hop.find(destination);
    if (hop_it == row.next_hop.end())
    {
        return NOTHING;
    }
    return hop_it->second;
}

// Append the exact intra-zone path from source to destination. Returns
// false if the destination is unreachable from source.
//
static bool append_zone_path(const ZoneTable &zt, dbref source,
                             dbref destination, std::vector<dbref> &path)
{
    if (source == destination)
    {
        return true;
    }

    auto src_it = zt.node_index.find(source);
    auto dst_it = zt.node_index.find(destination);
    if (src_it == zt.node_index.end() || dst_it == zt.node_index.end())
    {
        return false;
    }

    dbref current = source;
    int hops = 0;
    int max_hops = static_cast<int>(zt.index_to_room.size());

    while (current != destination && hops < max_hops)
    {
        dbref next_exit = zone_next_hop(zt, current, destination);
        if (next_exit == NOTHING)
        {
            return false;
        }

        dbref next_room = Location(next_exit);
        if (!Good_obj(next_room) || next_room == current)
        {
            return false;
        }

        path.push_back(next_exit);
        current = next_room;
        hops++;
    }

    return current == destination;
}

// Intra-zone hop count between two rooms (returns -1 if unreachable).
//
static int zone_hop_count(const ZoneTable &zt, dbref source, dbref destination)
{
    if (source == destination)
    {
        return 0;
    }

    auto src_it = zt.node_index.find(source);
    auto dst_it = zt.node_index.find(destination);
    if (src_it == zt.node_index.end() || dst_it == zt.node_index.end())
    {
        return -1;
    }

    // Walk next-hop chain counting steps.
    //
    dbref current = source;
    int hops = 0;
    int max_hops = static_cast<int>(zt.index_to_room.size());

    while (current != destination && hops < max_hops)
    {
        dbref next_exit = zone_next_hop(zt, current, destination);
        if (next_exit == NOTHING)
        {
            return -1;
        }
        dbref next_room = Location(next_exit);
        if (!Good_obj(next_room) || next_room == current)
        {
            return -1;
        }
        current = next_room;
        hops++;
    }
    return (current == destination) ? hops : -1;
}

// ---------------------------------------------------------------------------
// Cross-zone Dijkstra: find the sequence of gateway edges from
// source_zone to dest_zone.
//
// Returns the gateway edges in order.  Empty result = no route.
// ---------------------------------------------------------------------------

static void meta_dijkstra(
    dbref source_room,
    dbref dest_room,
    std::vector<const GatewayEdge *> &result)
{
    result.clear();

    dbref source_zone = g_room_to_zone[source_room];
    dbref dest_zone = g_room_to_zone[dest_room];
    if (source_zone == dest_zone)
    {
        return;
    }

    // Build adjacency: zone -> list of edge indices.
    //
    std::unordered_map<dbref, std::vector<int>> adj;
    for (int i = 0; i < static_cast<int>(g_meta.edges.size()); i++)
    {
        adj[g_meta.edges[i].source_zone].push_back(i);
    }

    // Dijkstra over actual room states. The state is "currently standing in
    // room R", where R is the query source or the target room of a gateway
    // edge already crossed. This avoids choosing an unreachable gateway just
    // because its zone edge count looks shorter.
    //
    std::unordered_map<dbref, int> dist;
    std::unordered_map<dbref, dbref> prev_room;
    std::unordered_map<dbref, int> prev_edge;  // room -> gateway edge index.

    // Priority queue: (distance, room).
    //
    typedef std::pair<int, dbref> PQEntry;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    int best_goal = INT_MAX;
    dbref best_goal_room = NOTHING;

    dist[source_room] = 0;
    pq.push({0, source_room});

    while (!pq.empty())
    {
        auto [d, room] = pq.top();
        pq.pop();

        auto dist_it = dist.find(room);
        if (dist_it == dist.end() || d > dist_it->second)
        {
            continue;  // Stale entry.
        }

        if (d >= best_goal)
        {
            continue;
        }

        dbref zone = g_room_to_zone[room];
        route_ensure_zone_current(zone);
        auto zt_it = g_zone_tables.find(zone);
        if (zt_it == g_zone_tables.end())
        {
            continue;
        }

        // Any state already inside the destination zone can finish with an
        // exact local-table lookup.
        //
        if (zone == dest_zone)
        {
            int final_cost = zone_hop_count(zt_it->second, room, dest_room);
            if (  final_cost >= 0
               && d + final_cost < best_goal)
            {
                best_goal = d + final_cost;
                best_goal_room = room;
            }
        }

        auto adj_it = adj.find(zone);
        if (adj_it == adj.end())
        {
            continue;
        }

        for (int ei : adj_it->second)
        {
            const GatewayEdge &ge = g_meta.edges[ei];
            int approach_cost = zone_hop_count(zt_it->second, room, ge.gate_room);
            if (approach_cost < 0)
            {
                continue;
            }

            dbref next_room = ge.target_room;
            int new_dist = d + approach_cost + 1;

            auto next_it = dist.find(next_room);
            if (next_it == dist.end() || new_dist < next_it->second)
            {
                dist[next_room] = new_dist;
                prev_room[next_room] = room;
                prev_edge[next_room] = ei;
                pq.push({new_dist, next_room});
            }
        }
    }

    // Reconstruct path.
    //
    if (best_goal_room == NOTHING)
    {
        return;
    }

    std::vector<int> edge_path;
    dbref room = best_goal_room;
    while (room != source_room)
    {
        auto pe_it = prev_edge.find(room);
        if (pe_it == prev_edge.end())
        {
            result.clear();
            return;
        }
        edge_path.push_back(pe_it->second);
        room = prev_room[room];
    }

    // Reverse to get source-to-dest order.
    //
    std::reverse(edge_path.begin(), edge_path.end());
    for (int ei : edge_path)
    {
        result.push_back(&g_meta.edges[ei]);
    }
}

// ---------------------------------------------------------------------------
// Full path reconstruction (handles cross-zone).
// ---------------------------------------------------------------------------

static void route_get_path(dbref source, dbref destination,
                           std::vector<dbref> &path)
{
    path.clear();

    dbref src_zone = g_room_to_zone[source];
    dbref dst_zone = g_room_to_zone[destination];

    if (src_zone == dst_zone)
    {
        // Same zone: walk next-hop chain.
        //
        route_ensure_zone_current(src_zone);
        auto zt_it = g_zone_tables.find(src_zone);
        if (zt_it == g_zone_tables.end())
        {
            return;
        }
        const ZoneTable &zt = zt_it->second;
        if (!append_zone_path(zt, source, destination, path))
        {
            path.clear();
        }
        return;
    }

    // Cross-zone: use meta-table to find gateway chain.
    //
    route_ensure_meta_current();

    std::vector<const GatewayEdge *> gw_path;
    meta_dijkstra(source, destination, gw_path);
    if (gw_path.empty())
    {
        return;
    }

    dbref current = source;

    for (size_t gi = 0; gi < gw_path.size(); gi++)
    {
        const GatewayEdge *ge = gw_path[gi];
        dbref gate_room = ge->gate_room;

        // Route within current zone from current to gate_room.
        //
        dbref cur_zone = g_room_to_zone[current];
        route_ensure_zone_current(cur_zone);
        auto zt_it = g_zone_tables.find(cur_zone);
        if (zt_it == g_zone_tables.end())
        {
            path.clear();
            return;
        }
        const ZoneTable &zt = zt_it->second;
        if (!append_zone_path(zt, current, gate_room, path))
        {
            path.clear();
            return;
        }

        // Cross the gateway exit.
        //
        path.push_back(ge->gate_exit);
        current = ge->target_room;
    }

    // Final segment: route within destination zone to the destination.
    //
    if (current != destination)
    {
        route_ensure_zone_current(dst_zone);
        auto zt_it = g_zone_tables.find(dst_zone);
        if (zt_it == g_zone_tables.end())
        {
            path.clear();
            return;
        }
        const ZoneTable &zt = zt_it->second;
        if (!append_zone_path(zt, current, destination, path))
        {
            path.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// SQLite persistence (metadata only for now).
// ---------------------------------------------------------------------------

static void route_persist_to_sqlite(void)
{
    if (  nullptr == g_pSQLiteBackend
       || !g_pSQLiteBackend->GetDB().IsOpen())
    {
        return;
    }

    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();

    sqldb.PutMeta("route_zone_count",
        static_cast<int>(g_zone_tables.size()));
    sqldb.PutMeta("route_node_count",
        static_cast<int>(g_all_navigable.size()));
    sqldb.PutMeta("route_gateway_count",
        static_cast<int>(g_meta.edges.size()));
}

// ---------------------------------------------------------------------------
// Query interface.
// ---------------------------------------------------------------------------

void route_query(dbref executor, dbref source, dbref destination,
                 int options, UTF8 *buff, UTF8 **bufc)
{
    // Handle rebuild option (wizard-only).
    //
    if (options & ROUTE_OPT_REBUILD)
    {
        if (!Wizard(executor))
        {
            safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
            return;
        }
        route_invalidate();
    }

    // Validate source and destination.
    //
    if (  !Good_obj(source)
       || !isRoom(source)
       || !Good_obj(destination)
       || !isRoom(destination))
    {
        safe_str(T("#-2"), buff, bufc);
        return;
    }

    // Same room?
    //
    if (source == destination)
    {
        if (options & ROUTE_OPT_DISTANCE)
        {
            safe_chr('0', buff, bufc);
        }
        else if (options & ROUTE_OPT_PATH)
        {
            // Empty path -- already there.
        }
        else
        {
            safe_str(T("#-1"), buff, bufc);
        }
        return;
    }

    // Ensure navigable rooms are scanned.
    //
    route_scan_navigable();

    // Check that both rooms are navigable.
    //
    if (  g_all_navigable.find(source) == g_all_navigable.end()
       || g_all_navigable.find(destination) == g_all_navigable.end())
    {
        safe_str(T("#-1 NOT NAVIGABLE"), buff, bufc);
        return;
    }

    // Handle path option.
    //
    if (options & ROUTE_OPT_PATH)
    {
        std::vector<dbref> path;
        route_get_path(source, destination, path);
        if (path.empty())
        {
            safe_str(T("#-1 NO ROUTE"), buff, bufc);
            return;
        }

        for (size_t i = 0; i < path.size(); i++)
        {
            if (i > 0)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_tprintf_str(buff, bufc, T("#%d"), path[i]);
        }
        return;
    }

    // Handle distance option.
    //
    if (options & ROUTE_OPT_DISTANCE)
    {
        std::vector<dbref> path;
        route_get_path(source, destination, path);
        if (path.empty())
        {
            safe_str(T("#-1 NO ROUTE"), buff, bufc);
            return;
        }

        safe_ltoa(static_cast<long>(path.size()), buff, bufc);
        return;
    }

    // Default: return next-hop exit.
    //
    dbref src_zone = g_room_to_zone[source];
    dbref dst_zone = g_room_to_zone[destination];

    if (src_zone == dst_zone)
    {
        // Same zone: direct lookup.
        //
        route_ensure_zone_current(src_zone);
        auto zt_it = g_zone_tables.find(src_zone);
        if (zt_it == g_zone_tables.end())
        {
            safe_str(T("#-1 NO ROUTE"), buff, bufc);
            return;
        }

        dbref next_exit = zone_next_hop(zt_it->second, source, destination);
        if (next_exit == NOTHING)
        {
            safe_str(T("#-1 NO ROUTE"), buff, bufc);
            return;
        }
        safe_tprintf_str(buff, bufc, T("#%d"), next_exit);
        return;
    }

    // Cross-zone: resolve the full path, then return its first hop.
    //
    std::vector<dbref> path;
    route_get_path(source, destination, path);
    if (path.empty())
    {
        safe_str(T("#-1 NO ROUTE"), buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, T("#%d"), path[0]);
}
