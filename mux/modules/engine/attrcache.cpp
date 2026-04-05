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
thread_local UTF8 sqlite_attr_buf[LBUF_SIZE];

static size_t cache_size = 0;
static uint64_t cache_hits = 0;
static uint64_t cache_misses = 0;

// ---------------------------------------------------------------------------
// Write queue: batches Put/Del operations and flushes them in a single
// BEGIN/COMMIT transaction.  Flushed on threshold, on demand-driven
// deferred task, and before sync/close/tick.
// ---------------------------------------------------------------------------

struct CacheWriteOp
{
    enum OpType { OP_PUT, OP_DEL, OP_CODE_CACHE_PUT };
    OpType          op;
    unsigned int    object;
    unsigned int    attrnum;
    vector<UTF8>    value;      // empty for OP_DEL
    int             owner;
    int             flags;

    // OP_CODE_CACHE_PUT fields.
    //
    string          cc_source_hash;
    string          cc_blob_hash;
    vector<char>    cc_memory;
    vector<char>    cc_code;
    int64_t         cc_entry_pc;
    int64_t         cc_code_size;
    vector<char>    cc_str;
    int64_t         cc_str_pool_end;
    vector<char>    cc_fargs;
    int64_t         cc_fargs_pool_end;
    int64_t         cc_out_pool_end;
    int64_t         cc_out_addr;
    int             cc_needs_jit;
    int             cc_folds;
    int             cc_ecalls;
    int             cc_tier2_calls;
    int             cc_native_ops;
    vector<char>    cc_deps;
};

static vector<CacheWriteOp> s_write_queue;
static unordered_map<Aname, size_t, AnameHasher> s_attr_write_index;
static bool s_flush_scheduled = false;
static const size_t WRITE_QUEUE_THRESHOLD = 50;

// Forward declaration.
//
void cache_flush_writes(void);
static void trim_attribute_cache(void);
static bool cache_obj_preloaded(dbref obj, bool bAll);

static void Task_WriteQueueFlush(void *pUnused, int iUnused)
{
    UNUSED_PARAMETER(pUnused);
    UNUSED_PARAMETER(iUnused);

    s_flush_scheduled = false;
    cache_flush_writes();
}

static void schedule_flush(void)
{
    if (!s_flush_scheduled && !mudstate.bStandAlone)
    {
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();
        scheduler.DeferTask(ltaNow + time_250ms, PRIORITY_SYSTEM,
            Task_WriteQueueFlush, nullptr, 0);
        s_flush_scheduled = true;
    }
}

void cache_flush_writes(void)
{
    if (s_write_queue.empty() || !g_pSQLiteBackend)
    {
        return;
    }

#if defined(HAVE_WORKING_FORK)
    if (mudstate.write_protect)
    {
        return;
    }
#endif

    // If we're inside a caller-managed transaction (e.g., flatfile import),
    // skip the Begin/Commit wrapper — the caller owns the transaction.
    //
    bool bOwnTransaction = !mudstate.bSQLiteLoading;
    CSQLiteDB &db = g_pSQLiteBackend->GetDB();

    if (bOwnTransaction)
    {
        db.Begin();
    }

    for (const auto &op : s_write_queue)
    {
        if (op.op == CacheWriteOp::OP_PUT)
        {
            g_pSQLiteBackend->Put(op.object, op.attrnum,
                op.value.data(), op.value.size(), op.owner, op.flags);
        }
        else if (op.op == CacheWriteOp::OP_DEL)
        {
            g_pSQLiteBackend->Del(op.object, op.attrnum);
        }
        else if (op.op == CacheWriteOp::OP_CODE_CACHE_PUT)
        {
            db.CodeCachePut(
                op.cc_source_hash.data(),
                static_cast<int>(op.cc_source_hash.size()),
                op.cc_blob_hash.data(),
                static_cast<int>(op.cc_blob_hash.size()),
                op.cc_memory.data(),
                static_cast<int>(op.cc_memory.size()),
                op.cc_code.data(),
                static_cast<int>(op.cc_code.size()),
                op.cc_entry_pc,
                op.cc_code_size,
                op.cc_str.data(),
                static_cast<int>(op.cc_str.size()),
                op.cc_str_pool_end,
                op.cc_fargs.data(),
                static_cast<int>(op.cc_fargs.size()),
                op.cc_fargs_pool_end,
                op.cc_out_pool_end,
                op.cc_out_addr,
                op.cc_needs_jit,
                op.cc_folds, op.cc_ecalls,
                op.cc_tier2_calls, op.cc_native_ops,
                op.cc_deps.data(),
                static_cast<int>(op.cc_deps.size()));
        }
    }

    if (bOwnTransaction)
    {
        db.Commit();
    }

    // Unpin flushed entries: tombstones are removed from cache entirely;
    // dirty puts are cleared and moved from pinned list to LRU list.
    //
    if (!mudstate.bStandAlone)
    {
        for (const auto &op : s_write_queue)
        {
            if (op.op == CacheWriteOp::OP_PUT || op.op == CacheWriteOp::OP_DEL)
            {
                Aname nam;
                nam.object  = op.object;
                nam.attrnum = op.attrnum;
                auto it = mudstate.attribute_lru_cache_map.find(nam);
                if (it != mudstate.attribute_lru_cache_map.end())
                {
                    if (it->second.tombstone)
                    {
                        cache_size -= it->second.data.size();
                        mudstate.attribute_pinned_list.erase(it->second.lru_it);
                        mudstate.attribute_lru_cache_map.erase(it);
                    }
                    else if (it->second.dirty)
                    {
                        it->second.dirty = false;
                        // Move from pinned to LRU (evictable).
                        mudstate.attribute_lru_cache_list.splice(
                            mudstate.attribute_lru_cache_list.end(),
                            mudstate.attribute_pinned_list,
                            it->second.lru_it);
                    }
                }
            }
        }
        trim_attribute_cache();
    }
    s_write_queue.clear();
    s_attr_write_index.clear();
}

static void queue_attr_write(const CacheWriteOp &new_op)
{
    Aname nam;
    nam.object = new_op.object;
    nam.attrnum = new_op.attrnum;

    const auto it = s_attr_write_index.find(nam);
    if (it != s_attr_write_index.end())
    {
        CacheWriteOp &existing = s_write_queue[it->second];
        existing.op = new_op.op;
        existing.object = new_op.object;
        existing.attrnum = new_op.attrnum;
        existing.owner = new_op.owner;
        existing.flags = new_op.flags;
        existing.value = new_op.value;
        return;
    }

    s_write_queue.push_back(new_op);
    s_attr_write_index.insert(make_pair(nam, s_write_queue.size() - 1));
}

void cache_queue_code_cache_put(
    const char *source_hash, int source_hash_len,
    const char *blob_hash, int blob_hash_len,
    const void *memory_blob, int memory_len,
    const void *code_blob, int code_len,
    int64_t entry_pc, int64_t code_size,
    const void *str_blob, int str_len, int64_t str_pool_end,
    const void *fargs_blob, int fargs_len, int64_t fargs_pool_end,
    int64_t out_pool_end,
    int64_t out_addr, int needs_jit,
    int folds, int ecalls, int tier2_calls, int native_ops,
    const void *deps_blob, int deps_len)
{
    CacheWriteOp op;
    op.op = CacheWriteOp::OP_CODE_CACHE_PUT;
    op.object  = 0;
    op.attrnum = 0;
    op.owner   = 0;
    op.flags   = 0;
    op.cc_source_hash.assign(source_hash, source_hash_len);
    op.cc_blob_hash.assign(blob_hash, blob_hash_len);
    if (memory_len > 0)
    {
        op.cc_memory.assign(static_cast<const char *>(memory_blob),
                            static_cast<const char *>(memory_blob) + memory_len);
    }
    if (code_len > 0)
    {
        op.cc_code.assign(static_cast<const char *>(code_blob),
                          static_cast<const char *>(code_blob) + code_len);
    }
    op.cc_entry_pc      = entry_pc;
    op.cc_code_size     = code_size;
    if (str_len > 0)
    {
        op.cc_str.assign(static_cast<const char *>(str_blob),
                         static_cast<const char *>(str_blob) + str_len);
    }
    op.cc_str_pool_end  = str_pool_end;
    if (fargs_len > 0)
    {
        op.cc_fargs.assign(static_cast<const char *>(fargs_blob),
                           static_cast<const char *>(fargs_blob) + fargs_len);
    }
    op.cc_fargs_pool_end = fargs_pool_end;
    op.cc_out_pool_end  = out_pool_end;
    op.cc_out_addr     = out_addr;
    op.cc_needs_jit    = needs_jit;
    op.cc_folds        = folds;
    op.cc_ecalls       = ecalls;
    op.cc_tier2_calls  = tier2_calls;
    op.cc_native_ops   = native_ops;
    op.cc_deps.assign(static_cast<const char *>(deps_blob),
                      static_cast<const char *>(deps_blob) + deps_len);

    s_write_queue.push_back(std::move(op));

    if (s_write_queue.size() >= WRITE_QUEUE_THRESHOLD)
    {
        cache_flush_writes();
    }
    else
    {
        schedule_flush();
    }
}

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
    mudstate.attribute_preloaded_builtin_objects.clear();
    mudstate.attribute_preloaded_all_objects.clear();

    return bNewDatabase ? HF_OPEN_STATUS_NEW : HF_OPEN_STATUS_OLD;
}

void cache_close(void)
{
    cache_flush_writes();
    if (g_pSQLiteBackend)
    {
        g_pSQLiteBackend->Close();
        delete g_pSQLiteBackend;
        g_pSQLiteBackend = nullptr;
    }
    mudstate.attribute_preloaded_builtin_objects.clear();
    mudstate.attribute_preloaded_all_objects.clear();
    cache_initted = false;
}

void cache_tick(void)
{
    cache_flush_writes();
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
    unordered_set<dbref> evicted_objects;
    while (cache_size > static_cast<size_t>(mudconf.max_cache_size))
    {
        if (mudstate.attribute_lru_cache_list.empty())
        {
            // All remaining bytes are pinned (dirty).  Stop evicting.
            //
            break;
        }

        // Blow the oldest thing away.
        //
        const Aname nam = mudstate.attribute_lru_cache_list.front();
        const auto it = mudstate.attribute_lru_cache_map.find(nam);
        cache_size -= it->second.data.size();
        evicted_objects.insert(static_cast<dbref>(nam.object));
        mudstate.attribute_lru_cache_map.erase(it);
        mudstate.attribute_lru_cache_list.pop_front();
    }

    for (dbref obj : evicted_objects)
    {
        mudstate.attribute_preloaded_builtin_objects.erase(obj);
        mudstate.attribute_preloaded_all_objects.erase(obj);
    }
}

static bool cache_obj_preloaded(dbref obj, bool bAll)
{
    if (mudstate.attribute_preloaded_all_objects.find(obj)
        != mudstate.attribute_preloaded_all_objects.end())
    {
        return true;
    }

    if (!bAll
        && mudstate.attribute_preloaded_builtin_objects.find(obj)
            != mudstate.attribute_preloaded_builtin_objects.end())
    {
        return true;
    }

    return false;
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
            // Tombstone: attribute was deleted but not yet flushed.
            //
            if (it->second.tombstone)
            {
                cache_hits++;
                *pLen = 0;
                *owner = NOTHING;
                *flags = 0;
                return nullptr;
            }

            // Cache hit — move to newest position in whichever list
            // the entry lives in (LRU for clean, pinned for dirty).
            //
            cache_hits++;
            auto &target_list = it->second.dirty
                ? mudstate.attribute_pinned_list
                : mudstate.attribute_lru_cache_list;
            target_list.splice(
                target_list.end(),
                target_list,
                it->second.lru_it
            );
        	*pLen = it->second.data.size();
            *owner = it->second.attr_owner;
            *flags = it->second.attr_flags;
            return it->second.data.data();
        }
        cache_misses++;
    }

    // Object-affinity prefetch: instead of loading one attribute,
    // bulk-load the entire object.  GetAll (~5.5 us) is cheaper than
    // 2 individual Gets (~3.6 us each), and most code that touches
    // one attribute on an object will touch more.
    //
    if (!mudstate.bStandAlone
        && !cache_obj_preloaded(static_cast<dbref>(nam->object), true))
    {
        cache_preload_obj(static_cast<dbref>(nam->object), true);

        // Retry the cache — the attribute should be there now.
        //
        const auto it2 = mudstate.attribute_lru_cache_map.find(*nam);
        if (it2 != mudstate.attribute_lru_cache_map.end())
        {
            if (it2->second.tombstone)
            {
                *pLen = 0;
                *owner = NOTHING;
                *flags = 0;
                return nullptr;
            }

            // Don't count as a hit — the miss already counted.
            //
            auto &list2 = it2->second.dirty
                ? mudstate.attribute_pinned_list
                : mudstate.attribute_lru_cache_list;
            list2.splice(list2.end(), list2, it2->second.lru_it);
            *pLen = it2->second.data.size();
            *owner = it2->second.attr_owner;
            *flags = it2->second.attr_flags;
            return it2->second.data.data();
        }

        // Attribute genuinely doesn't exist on this object.
        //
        *pLen = 0;
        *owner = NOTHING;
        *flags = 0;
        return nullptr;
    }

    // Standalone mode: single-attribute load (no cache).
    //
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

    // Queue the write for batched SQLite execution.  In standalone mode
    // (dbconvert), write through immediately — no scheduler is running.
    //
    if (mudstate.bStandAlone)
    {
        if (!g_pSQLiteBackend->Put(nam->object, nam->attrnum, value, len,
                                   static_cast<int>(owner), flags))
        {
            return false;
        }
        return true;
    }

    // Pin the cache entry BEFORE queueing the write.  If the queue
    // hits the threshold and triggers cache_flush_writes(), the flush
    // must see the pinned entry so it can unpin it after committing.
    //
    {
        statedata::AttrCacheEntry entry;
        entry.data.assign(value, value + len);
        entry.lru_it = mudstate.attribute_pinned_list.insert(
            mudstate.attribute_pinned_list.end(), *nam);
        entry.attr_owner = owner;
        entry.attr_flags = flags;
        entry.dirty     = true;
        entry.tombstone = false;

        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            cache_size += entry.data.size() - it->second.data.size();
            if (it->second.dirty)
            {
                mudstate.attribute_pinned_list.erase(it->second.lru_it);
            }
            else
            {
                mudstate.attribute_lru_cache_list.erase(it->second.lru_it);
            }
            it->second = std::move(entry);
        }
        else
        {
            cache_size += entry.data.size();
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, std::move(entry)));
        }
        trim_attribute_cache();
    }

    // Queue the write and check threshold.
    //
    {
        CacheWriteOp op;
        op.op      = CacheWriteOp::OP_PUT;
        op.object  = nam->object;
        op.attrnum = nam->attrnum;
        op.value.assign(value, value + len);
        op.owner   = static_cast<int>(owner);
        op.flags   = flags;
        queue_attr_write(op);

        if (s_write_queue.size() >= WRITE_QUEUE_THRESHOLD)
        {
            cache_flush_writes();
        }
        else
        {
            schedule_flush();
        }
    }
    return true;
}

bool cache_sync(void)
{
    cache_flush_writes();
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

    if (mudstate.bStandAlone)
    {
        if (!g_pSQLiteBackend->Del(nam->object, nam->attrnum))
        {
            return false;
        }
        return true;
    }

    // Pin tombstone BEFORE queueing the delete — same ordering
    // rationale as cache_put (flush must see the pinned entry).
    //
    {
        const auto it = mudstate.attribute_lru_cache_map.find(*nam);
        if (it != mudstate.attribute_lru_cache_map.end())
        {
            it->second.tombstone = true;
            if (!it->second.dirty)
            {
                it->second.dirty = true;
                mudstate.attribute_pinned_list.splice(
                    mudstate.attribute_pinned_list.end(),
                    mudstate.attribute_lru_cache_list,
                    it->second.lru_it);
            }
        }
        else
        {
            statedata::AttrCacheEntry entry;
            entry.lru_it = mudstate.attribute_pinned_list.insert(
                mudstate.attribute_pinned_list.end(), *nam);
            entry.attr_owner = NOTHING;
            entry.attr_flags = 0;
            entry.dirty     = true;
            entry.tombstone = true;
            mudstate.attribute_lru_cache_map.insert(make_pair(*nam, std::move(entry)));
        }
    }

    // Queue the delete and check threshold.
    //
    {
        CacheWriteOp op;
        op.op      = CacheWriteOp::OP_DEL;
        op.object  = nam->object;
        op.attrnum = nam->attrnum;
        op.owner   = 0;
        op.flags   = 0;
        op.value.clear();
        queue_attr_write(op);

        if (s_write_queue.size() >= WRITE_QUEUE_THRESHOLD)
        {
            cache_flush_writes();
        }
        else
        {
            schedule_flush();
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
    return g_pSQLiteBackend->Count(static_cast<unsigned int>(obj));
}

void cache_preload_obj(dbref obj, bool bAll)
{
    if (  !cache_initted
       || mudstate.bStandAlone)
    {
        return;
    }

    if (cache_obj_preloaded(obj, bAll))
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
        entry.dirty     = false;
        entry.tombstone = false;
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
        return;
    }

    if (bAll)
    {
        mudstate.attribute_preloaded_all_objects.insert(obj);
        mudstate.attribute_preloaded_builtin_objects.insert(obj);
    }
    else
    {
        mudstate.attribute_preloaded_builtin_objects.insert(obj);
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

void cache_get_stats(CacheStats *pStats)
{
    pStats->hits = cache_hits;
    pStats->misses = cache_misses;
    pStats->entries = mudstate.attribute_lru_cache_map.size();
    pStats->size = cache_size;
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
