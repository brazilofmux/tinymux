/*! \file routing.cpp
 * \brief Phase 1 routing: static unconditional next-hop tables.
 *
 * Implements BFS-based shortest-path routing over rooms marked NAVIGABLE.
 * The routing table stores only the next-hop exit for each (source, dest)
 * pair, compressed via three techniques:
 *
 *   1. Diagonal elimination  -- source == dest needs no entry.
 *   2. Adjacent marking      -- dest is one hop away; store the exit directly.
 *   3. Row redundancy        -- if every destination funnels through the same
 *      exit, store a single "always(exit)" sentinel instead of N entries.
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
#include <queue>
#include <cstring>

// ---------------------------------------------------------------------------
// In-memory routing table.
// ---------------------------------------------------------------------------

// Sentinel values for compressed table entries.
//
constexpr dbref ROUTE_DIRECT  = -2;  // Destination is adjacent (one hop).
constexpr dbref ROUTE_NONE    = -3;  // No route exists.

// Per-room routing row.  If always_exit != NOTHING, every destination from
// this room funnels through always_exit and the sparse map is empty.
//
struct RouteRow
{
    dbref always_exit;                          // NOTHING if not compressed.
    std::unordered_map<dbref, dbref> next_hop;  // dest -> exit dbref.
};

// Global routing state.
//
static int                                 g_route_generation = 0;
static int                                 g_table_generation = -1;
static std::unordered_map<dbref, int>      g_node_index;   // room -> dense index
static std::vector<dbref>                  g_index_to_room; // dense index -> room
static std::vector<RouteRow>               g_table;        // indexed by dense index
static bool                                g_table_valid = false;

// ---------------------------------------------------------------------------
// Forward declarations.
// ---------------------------------------------------------------------------

static void route_build_table(void);
static void route_persist_to_sqlite(void);
static void route_load_from_sqlite(void);

// ---------------------------------------------------------------------------
// Initialization / shutdown.
// ---------------------------------------------------------------------------

void route_init(void)
{
    g_route_generation = 0;
    g_table_generation = -1;
    g_table_valid = false;

    // Try to load a persisted table from SQLite.
    //
    route_load_from_sqlite();
}

void route_shutdown(void)
{
    g_node_index.clear();
    g_index_to_room.clear();
    g_table.clear();
    g_table_valid = false;
}

void route_invalidate(void)
{
    g_route_generation++;
}

// ---------------------------------------------------------------------------
// Ensure the table is current.  Lazy rebuild on generation mismatch.
// ---------------------------------------------------------------------------

static void route_ensure_current(void)
{
    if (g_table_valid && g_table_generation == g_route_generation)
    {
        return;
    }
    route_build_table();
    route_persist_to_sqlite();
}

// ---------------------------------------------------------------------------
// Collect navigable rooms and their static exits.
// ---------------------------------------------------------------------------

struct ExitEdge
{
    dbref exit_dbref;
    dbref dest_room;
};

static void collect_navigable_graph(
    std::unordered_map<dbref, int> &node_idx,
    std::vector<dbref> &idx_to_room,
    std::vector<std::vector<ExitEdge>> &adj)
{
    node_idx.clear();
    idx_to_room.clear();

    // Pass 1: identify all navigable rooms.
    //
    dbref thing;
    DO_WHOLE_DB(thing)
    {
        if (  isRoom(thing)
           && !isGarbage(thing)
           && Navigable(thing))
        {
            int idx = static_cast<int>(idx_to_room.size());
            node_idx[thing] = idx;
            idx_to_room.push_back(thing);
        }
    }

    int n = static_cast<int>(idx_to_room.size());
    adj.resize(n);

    // Pass 2: collect static exits between navigable rooms.
    //
    for (int i = 0; i < n; i++)
    {
        dbref room = idx_to_room[i];
        adj[i].clear();

        dbref exit_obj;
        DOLIST(exit_obj, Exits(room))
        {
            if (!isExit(exit_obj))
            {
                continue;
            }

            // Skip variable-destination exits.
            //
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

            // Destination must also be navigable.
            //
            auto it = node_idx.find(dest);
            if (it == node_idx.end())
            {
                continue;
            }

            ExitEdge edge;
            edge.exit_dbref = exit_obj;
            edge.dest_room  = dest;
            adj[i].push_back(edge);
        }
    }
}

// ---------------------------------------------------------------------------
// BFS from a single source.  Fills in next_hop[dest] = exit for all
// reachable destinations.
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

    // BFS state: visited[i] = true if node i has been reached.
    // first_exit[i] = the exit from source_idx that leads toward i.
    //
    std::vector<bool> visited(n, false);
    std::vector<dbref> first_exit(n, NOTHING);
    std::queue<int> queue;

    visited[source_idx] = true;

    // Seed BFS with direct neighbors.
    //
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

    // Standard BFS.
    //
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

    // Populate the row.
    //
    for (int i = 0; i < n; i++)
    {
        if (i == source_idx)
        {
            continue;  // Diagonal elimination.
        }
        if (first_exit[i] != NOTHING)
        {
            row.next_hop[idx_to_room[i]] = first_exit[i];
        }
    }

    // Row redundancy compression: if every reachable destination uses the
    // same exit, collapse to always_exit.
    //
    if (!row.next_hop.empty())
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
// Build the full routing table from scratch.
// ---------------------------------------------------------------------------

static void route_build_table(void)
{
    std::vector<std::vector<ExitEdge>> adj;

    collect_navigable_graph(g_node_index, g_index_to_room, adj);

    int n = static_cast<int>(g_index_to_room.size());
    g_table.resize(n);

    for (int i = 0; i < n; i++)
    {
        bfs_from_source(i, adj, g_node_index, g_index_to_room, g_table[i]);
    }

    g_table_generation = g_route_generation;
    g_table_valid = true;
}

// ---------------------------------------------------------------------------
// SQLite persistence.
// ---------------------------------------------------------------------------

static void route_persist_to_sqlite(void)
{
    if (  nullptr == g_pSQLiteBackend
       || !g_pSQLiteBackend->GetDB().IsOpen())
    {
        return;
    }

    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();

    // Store generation and node count via metadata key-value store.
    // The route_nodes / route_table SQL tables exist for future phases;
    // Phase 1 rebuilds lazily from NAVIGABLE flags on rooms.
    //
    sqldb.PutMeta("route_generation", g_route_generation);
    sqldb.PutMeta("route_node_count",
        static_cast<int>(g_index_to_room.size()));
}

static void route_load_from_sqlite(void)
{
    if (  nullptr == g_pSQLiteBackend
       || !g_pSQLiteBackend->GetDB().IsOpen())
    {
        return;
    }

    // Phase 1: always rebuild lazily on first query.  The persisted
    // generation counter is stored for future phases to use for
    // warm-start logic.
}

// ---------------------------------------------------------------------------
// Query interface.
// ---------------------------------------------------------------------------

// Reconstruct the full path from source to destination by following
// next-hop entries.
//
static void route_get_path(dbref source, dbref destination,
                           std::vector<dbref> &path)
{
    path.clear();

    dbref current = source;
    int max_hops = static_cast<int>(g_index_to_room.size());
    int hops = 0;

    while (current != destination && hops < max_hops)
    {
        auto src_it = g_node_index.find(current);
        if (src_it == g_node_index.end())
        {
            path.clear();
            return;
        }

        int src_idx = src_it->second;
        const RouteRow &row = g_table[src_idx];

        dbref next_exit;
        if (row.always_exit != NOTHING)
        {
            next_exit = row.always_exit;
        }
        else
        {
            auto hop_it = row.next_hop.find(destination);
            if (hop_it == row.next_hop.end())
            {
                path.clear();
                return;
            }
            next_exit = hop_it->second;
        }

        path.push_back(next_exit);

        // Follow the exit to the next room.
        //
        dbref next_room = Location(next_exit);
        if (!Good_obj(next_room) || next_room == current)
        {
            path.clear();
            return;
        }
        current = next_room;
        hops++;
    }

    if (current != destination)
    {
        path.clear();
    }
}

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

    // Ensure the table is up to date.
    //
    route_ensure_current();

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
            // No exit needed.
            safe_str(T("#-1"), buff, bufc);
        }
        return;
    }

    // Check that both rooms are navigable.
    //
    auto src_it = g_node_index.find(source);
    auto dst_it = g_node_index.find(destination);
    if (src_it == g_node_index.end() || dst_it == g_node_index.end())
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
    int src_idx = src_it->second;
    const RouteRow &row = g_table[src_idx];

    dbref next_exit;
    if (row.always_exit != NOTHING)
    {
        next_exit = row.always_exit;
    }
    else
    {
        auto hop_it = row.next_hop.find(destination);
        if (hop_it == row.next_hop.end())
        {
            safe_str(T("#-1 NO ROUTE"), buff, bufc);
            return;
        }
        next_exit = hop_it->second;
    }

    safe_tprintf_str(buff, bufc, T("#%d"), next_exit);
}
