/*! \file attrcache.cpp
 * \brief Attribute caching module.
 *
 * The functions here manage the upper-level attribute value cache for
 * disk-based mode. It's not used in memory-based builds. The lower-level
 * storage is either CHashFile (.dir/.pag) or SQLite (.db).
 *
 * The upper-level cache is organized by an unordered_map and a linked list.
 * The former allows random access while the linked list helps find the
 * least-recently-used attribute.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
using namespace std;

#include <unordered_set>

#include "sqlite_backend.h"

CSQLiteBackend *g_pSQLiteBackend = nullptr;

static bool cache_initted = false;

// SQLite backend uses its own buffer for attribute retrieval.
//
static UTF8 sqlite_attr_buf[LBUF_SIZE];

static size_t cache_size = 0;
static uint64_t cache_hits = 0;
static uint64_t cache_misses = 0;

int cache_init(const UTF8 *indb)
{
    if (cache_initted)
    {
        return HF_OPEN_STATUS_ERROR;
    }

    g_pSQLiteBackend = new CSQLiteBackend();

    // Derive SQLite database path from the input database name.
    // Replace .db extension with .sqlite, or append .sqlite if no .db suffix.
    //
    char szPath[LBUF_SIZE];
    mux_strncpy((UTF8 *)szPath, indb, sizeof(szPath) - 1);
    szPath[sizeof(szPath) - 1] = '\0';
    size_t n = strlen(szPath);
    if (n > 3 && strcmp(szPath + n - 3, ".db") == 0)
    {
        strcpy(szPath + n - 3, ".sqlite");
    }
    else
    {
        strcat(szPath, ".sqlite");
    }

    // Check if the database file exists before opening.
    // sqlite3_open creates the file if it doesn't exist.
    //
#if defined(WINDOWS_FILES)
    bool bNewDatabase = (_access(szPath, 0) != 0);
#else
    bool bNewDatabase = (access(szPath, F_OK) != 0);
#endif

    if (!g_pSQLiteBackend->Open(szPath))
    {
        delete g_pSQLiteBackend;
        g_pSQLiteBackend = nullptr;
        return HF_OPEN_STATUS_ERROR;
    }

    cache_initted = true;

    return bNewDatabase ? HF_OPEN_STATUS_NEW : HF_OPEN_STATUS_OLD;
}

void cache_close(void)
{
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Close();
        delete g_pSQLiteBackend;
        g_pSQLiteBackend = nullptr;
    }
    cache_initted = false;
}

void cache_tick(void)
{
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Tick();
    }
}

static void trim_attribute_cache(void)
{
    // -1 means unlimited: never evict.
    //
    if (mudconf.max_cache_size < 0)
    {
        return;
    }

    // Check to see if the cache needs to be trimmed.
    //
    while (cache_size > static_cast<size_t>(mudconf.max_cache_size))
    {
        if (mudstate.attribute_lru_cache_list.empty())
        {
            cache_size = 0;
            break;
        }

        // Blow the oldest thing away.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(mudstate.attribute_lru_cache_list.front());
        cache_size -= it->second.data.size();
        mudstate.attribute_lru_cache_map.erase(it);
        mudstate.attribute_lru_cache_list.pop_front();
    }
}

const UTF8 *cache_get(Aname *nam, size_t *pLen, dbref *owner, int *flags)
{
    if (  nam == static_cast<Aname*>(nullptr)
       || !cache_initted)
    {
        *pLen = 0;
        *owner = NOTHING;
        *flags = 0;
        return nullptr;
    }

    if (!mudstate.bStandAlone)
    {
        // Check the cache, first.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache, so indicate this entry as the newest and return it.
            //
            cache_hits++;
            mudstate.attribute_lru_cache_list.splice(
                mudstate.attribute_lru_cache_list.end(),
                mudstate.attribute_lru_cache_list,
                it->second.lru_it
            );
        	*pLen = it->second.data.size();
            *owner = it->second.attr_owner;
            *flags = it->second.attr_flags;
            return it->second.data.data();
        }
        cache_misses++;
    }

    size_t nLength = 0;
    int db_owner = NOTHING;
    int db_flags = 0;
    if (g_pSQLiteBackend->Get(nam->object, nam->attrnum,
                              sqlite_attr_buf, sizeof(sqlite_attr_buf), &nLength,
                              &db_owner, &db_flags))
    {
        *pLen = nLength;
        *owner = static_cast<dbref>(db_owner);
        *flags = db_flags;
        if (!mudstate.bStandAlone)
        {
            // Add this information to the cache.
            //
            statedata::AttrCacheEntry entry;
            entry.data.assign(sqlite_attr_buf, sqlite_attr_buf + nLength);
            entry.lru_it = mudstate.attribute_lru_cache_list.insert(
                mudstate.attribute_lru_cache_list.end(), *nam);
            entry.attr_owner = static_cast<dbref>(db_owner);
            entry.attr_flags = db_flags;
            cache_size += entry.data.size();
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, std::move(entry)));
            trim_attribute_cache();
        }
        return sqlite_attr_buf;
    }

    *pLen = 0;
    *owner = NOTHING;
    *flags = 0;
    return nullptr;
}


// cache_put no longer frees the pointer.
//
bool cache_put(Aname *nam, const UTF8 *value, size_t len, dbref owner, int flags)
{
    if (  !value
       || !nam
       || !cache_initted
       || len == 0)
    {
        return false;
    }
#if defined(HAVE_WORKING_FORK)
    if (mudstate.write_protect)
    {
        Log.tinyprintf(T("cache_put((%d,%d), \xE2\x80\x98%s\xE2\x80\x99, %u) while database is write-protected" ENDLINE),
            nam->object, nam->attrnum, value, len);
        return false;
    }
#endif // HAVE_WORKING_FORK

    if (len > LBUF_SIZE)
    {
        len = LBUF_SIZE;
    }

    // Write-through: write to SQLite immediately with separate owner/flags.
    //
    if (!g_pSQLiteBackend->Put(nam->object, nam->attrnum, value, len,
                               static_cast<int>(owner), flags))
    {
        Log.tinyprintf(T("cache_put((%d,%d), \xE2\x80\x98%s\xE2\x80\x99, %u) failed" ENDLINE),
            nam->object, nam->attrnum, value, len);
        return false;
    }

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        statedata::AttrCacheEntry entry;
        entry.data.assign(value, value + len);
        entry.lru_it = mudstate.attribute_lru_cache_list.insert(
            mudstate.attribute_lru_cache_list.end(), *nam);
        entry.attr_owner = owner;
        entry.attr_flags = flags;

        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache map, so replace and delete the old list entry.
            //
            cache_size += entry.data.size() - it->second.data.size();
            mudstate.attribute_lru_cache_list.erase(it->second.lru_it);
            it->second = std::move(entry);
        }
        else
        {
            // It wasn't in the cache map, so create a mapping.
            //
            cache_size += entry.data.size();
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, std::move(entry)));
        }
        trim_attribute_cache();
    }
    return true;
}

bool cache_sync(void)
{
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Sync();
    }
    return true;
}

// Delete this attribute from the database.
//
bool cache_del(Aname *nam)
{
    if (  !nam
       || !cache_initted)
    {
        return false;
    }

#if defined(HAVE_WORKING_FORK)
    if (mudstate.write_protect)
    {
        Log.tinyprintf(T("cache_del((%d,%d)) while database is write-protected" ENDLINE),
            nam->object, nam->attrnum);
        return false;
    }
#endif // HAVE_WORKING_FORK

    if (!g_pSQLiteBackend->Del(nam->object, nam->attrnum))
    {
        Log.tinyprintf(T("cache_del((%d,%d)) failed" ENDLINE),
            nam->object, nam->attrnum);
        return false;
    }

    if (!mudstate.bStandAlone)
    {
        // Update cache.
        //
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            // It was in the cache, so delete it.
            //
            cache_size -= it->second.data.size();
            mudstate.attribute_lru_cache_list.erase(it->second.lru_it);
            mudstate.attribute_lru_cache_map.erase(it);
        }
    }
    return true;
}

// Bulk-load attributes for an object into the LRU cache.
// bAll=true loads all attributes; bAll=false loads only builtins (attrnum < 256).
//
// Count the number of attributes stored on an object.
//
int cache_count(dbref obj)
{
    if (!cache_initted || !g_pSQLiteBackend)
    {
        return 0;
    }
    return g_pSQLiteBackend->GetDB().CountAttributes(obj);
}

void cache_preload_obj(dbref obj, bool bAll)
{
    if (  !cache_initted
       || mudstate.bStandAlone)
    {
        return;
    }

    auto loader = [obj](unsigned int attrnum, const UTF8 *value, size_t len,
                        int db_owner, int db_flags)
    {
        Aname nam;
        nam.object = static_cast<unsigned int>(obj);
        nam.attrnum = attrnum;

        // Skip if already in cache.
        //
        if (mudstate.attribute_lru_cache_map.find(nam)
            != mudstate.attribute_lru_cache_map.end())
        {
            return;
        }

        statedata::AttrCacheEntry entry;
        entry.data.assign(value, value + len);
        entry.lru_it = mudstate.attribute_lru_cache_list.insert(
            mudstate.attribute_lru_cache_list.end(), nam);
        entry.attr_owner = static_cast<dbref>(db_owner);
        entry.attr_flags = db_flags;
        cache_size += entry.data.size();
        mudstate.attribute_lru_cache_map.insert(
            std::make_pair(nam, std::move(entry)));
    };

    bool ok;
    if (bAll)
    {
        ok = g_pSQLiteBackend->GetAll(static_cast<unsigned int>(obj), loader);
    }
    else
    {
        ok = g_pSQLiteBackend->GetBuiltin(static_cast<unsigned int>(obj), loader);
    }

    if (!ok)
    {
        Log.tinyprintf(T("cache_preload: failed bulk preload for #%d" ENDLINE), obj);
    }

    trim_attribute_cache();
}

// Public entry point: preload all attributes for a single object.
//
void cache_preload(dbref obj)
{
    cache_preload_obj(obj, true);
}

// BFS preload of rooms reachable via exits from 'room', loading builtin
// attrs only.  'room' itself is assumed already preloaded and is placed
// in the visited set but not re-queried.
//
static void cache_preload_bfs(dbref room, int depth)
{
    if (  !cache_initted
       || mudstate.bStandAlone
       || !Good_obj(room)
       || !isRoom(room)
       || depth <= 0)
    {
        return;
    }

    if (depth > 3) depth = 3;

    std::vector<dbref> current;
    std::vector<dbref> next;
    std::unordered_set<dbref, dbrefHasher> visited;

    visited.insert(room);
    current.push_back(room);

    for (int d = 0; d < depth && !current.empty(); d++)
    {
        next.clear();
        for (dbref r : current)
        {
            for (dbref ex = Exits(r); ex != NOTHING; ex = Next(ex))
            {
                if (!Good_obj(ex) || !isExit(ex))
                {
                    continue;
                }
                dbref dest = Location(ex);
                if (  Good_obj(dest)
                   && isRoom(dest)
                   && visited.find(dest) == visited.end())
                {
                    cache_preload_obj(dest, false);
                    visited.insert(dest);
                    next.push_back(dest);
                }
            }
        }
        current.swap(next);
    }
}

// Preload attributes for an object and optionally nearby rooms.
//
// Unlimited cache (cache_max_size -1):
//   GetAll for the player and location, GetBuiltin across BFS neighbors.
//
// Bounded cache (cache_max_size > 0):
//   GetBuiltin for the player and location, no BFS.
//
void cache_preload_nearby(dbref obj, int depth)
{
    if (  !cache_initted
       || mudstate.bStandAlone
       || !Good_obj(obj))
    {
        return;
    }

    bool bUnlimited = (mudconf.max_cache_size < 0);

    // Primary objects (player + location) get full load in unlimited mode,
    // builtin-only in bounded mode.
    //
    cache_preload_obj(obj, bUnlimited);

    dbref room = isRoom(obj) ? obj : Location(obj);
    if (Good_obj(room) && isRoom(room) && room != obj)
    {
        cache_preload_obj(room, bUnlimited);
    }

    // BFS across exits only in unlimited mode.
    //
    if (bUnlimited && Good_obj(room) && isRoom(room))
    {
        cache_preload_bfs(room, depth);
    }
}

// Deferred task for BFS preloading of neighbors.  The primary room is
// assumed already preloaded by the caller; only exit destinations are
// loaded here.
//
static void Task_CachePreloadBFS(void *arg, int depth)
{
    dbref room = static_cast<dbref>(reinterpret_cast<intptr_t>(arg));
    if (Good_obj(room) && isRoom(room))
    {
        cache_preload_bfs(room, depth);
    }
}

void cache_preload_deferred_bfs(dbref room, int depth)
{
    scheduler.DeferImmediateTask(PRIORITY_SYSTEM,
        Task_CachePreloadBFS,
        reinterpret_cast<void *>(static_cast<intptr_t>(room)),
        depth);
}

// Format a byte count as a human-readable string with K/M/G suffix.
//
static void format_size(UTF8 *buf, size_t buflen, int64_t bytes)
{
    if (bytes < 0)
    {
        mux_strncpy(buf, T("unlimited"), buflen - 1);
    }
    else if (bytes >= 1024LL * 1024 * 1024)
    {
        mux_sprintf(buf, buflen, T("%.1f GB"),
            static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    else if (bytes >= 1024 * 1024)
    {
        mux_sprintf(buf, buflen, T("%.1f MB"),
            static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    else if (bytes >= 1024)
    {
        mux_sprintf(buf, buflen, T("%.1f KB"),
            static_cast<double>(bytes) / 1024.0);
    }
    else
    {
        mux_sprintf(buf, buflen, T("%lld bytes"),
            static_cast<long long>(bytes));
    }
}

void list_cache_stats(dbref player)
{
    size_t nEntries = mudstate.attribute_lru_cache_map.size();
    uint64_t total = cache_hits + cache_misses;
    double hit_pct = (total > 0) ? (100.0 * cache_hits / total) : 0.0;

    UTF8 szSize[64];
    UTF8 szMax[64];
    format_size(szSize, sizeof(szSize), static_cast<int64_t>(cache_size));
    format_size(szMax, sizeof(szMax), mudconf.max_cache_size);

    notify(player, T("--- Attribute Cache ---"));
    notify(player, tprintf(T("Entries: %lu   Size: %s   Max: %s   Preload depth: %d"),
        static_cast<unsigned long>(nEntries),
        szSize, szMax,
        mudconf.cache_preload_depth));
    notify(player, tprintf(T("Hits: %llu   Misses: %llu   Hit rate: %.1f%%"),
        static_cast<unsigned long long>(cache_hits),
        static_cast<unsigned long long>(cache_misses),
        hit_pct));

    CSQLiteDB::Stats st = g_pSQLiteBackend->GetDB().GetStats();

    notify(player, T("--- SQLite Storage ---"));
    notify(player, tprintf(T("Attr gets: %llu   puts: %llu   dels: %llu   bulk loads: %llu"),
        static_cast<unsigned long long>(st.attr_gets),
        static_cast<unsigned long long>(st.attr_puts),
        static_cast<unsigned long long>(st.attr_dels),
        static_cast<unsigned long long>(st.attr_bulk_loads)));
    notify(player, tprintf(T("Obj inserts: %llu   updates: %llu   loads: %llu"),
        static_cast<unsigned long long>(st.obj_inserts),
        static_cast<unsigned long long>(st.obj_updates),
        static_cast<unsigned long long>(st.obj_loads)));
}
