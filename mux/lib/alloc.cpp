/*! \file alloc.cpp
 * \brief Memory Allocation Subsystem.
 *
 * The functions here manage pools of often-used buffers of fixed-size.  It
 * adds value by greatly reducing the number and strength of calls to the
 * underlying platform's memory management.  Headers and footers detect
 * misuse of buffers by callers.
 *
 * Buffer tracking and freelists use std::vector instead of intrusive
 * linked lists, eliminating the next/nxtfree pointers from POOLHDR.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "modules.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

// libmux.so cannot access mudconf directly (it lives in engine.so).
// This flag mirrors g_paranoid_alloc; the engine sets it after
// loading configuration via g_paranoid_alloc in alloc.h (future work:
// libmux config broadcast).  For now, default to false.
//
bool g_paranoid_alloc = false;

// Output callback for @list buffers — set by engine at startup.
//
ALLOC_NOTIFY_FN g_alloc_notify_fn = nullptr;

/*! \brief Per-buffer header to manage and organize client allocation.
 *
 * The POOLHDR structure precedes a client area which must be properly
 * aligned to avoid faults when the client accesses structure members within
 * the client area.  64-bit alignment should be sufficient.
 *
 * The magicnum and pool_size are chosen when the pool is initialized.
 * buf_tag comes from the client so that buffers can be associated with the
 * places that allocated them.
 */

typedef struct pool_header
{
    unsigned int    magicnum;       // For consistency check
    size_t          pool_size;      // For consistency check
    union
    {
        const UTF8 *buf_tag;        // Debugging/trace tag
        uint64_t    align;          // Alignment padding
    } u;
} POOLHDR;

/*! \brief Per-buffer footer to catch buffer overruns.
 *
 * The POOLFTR structure helps detect when a client has written beyond the
 * bounds of the buffer.
 */

typedef struct pool_footer
{
    unsigned int magicnum;          // For consistency check
} POOLFTR;

/*! \brief Per-pool structure containing statistics and vector-based lists.
 *
 * The freelist is a stack (vector) of raw allocation pointers.
 * all_buffers tracks every allocation for diagnostics and cleanup.
 */

typedef struct pooldata
{
    size_t pool_client_size;        // Size in bytes of a buffer as seen by client.
    size_t pool_alloc_size;         // Size as allocated from system.
    unsigned int poolmagic;         // Magic number specific to this pool
    std::vector<char *> free_stack; // Free buffers (stack — push/pop from back)
    std::vector<char *> all_buffers;// All allocated raw blocks (for diagnostics)
    uint64_t tot_alloc;             // Total buffers allocated
    uint64_t num_alloc;             // Number of buffers currently allocated
    uint64_t max_alloc;             // Max # buffers allocated at one time
    uint64_t num_lost;              // Buffers lost due to corruption
} POOL;

static POOL pools[NUM_POOLS];
static const UTF8 *poolnames[] =
{
    T("Lbufs"),
    T("Sbufs"),
    T("Mbufs"),
    T("Bools"),
    T("Descs"),
    T("Qentries"),
    T("Pcaches")
};

/*! \brief Initialize a buffer pool.
 *
 * This is done once. The client size, magic, and allocation size are chosen
 * at this time.  After this initialization, allocations can be done.
 *
 * \param poolnum  An integer uniquely indicating which pool.
 * \param poolsize The size of the client area this pool supports.
 */
void pool_init(int poolnum, int poolsize)
{
    pools[poolnum].pool_client_size = poolsize;
    pools[poolnum].pool_alloc_size  = poolsize + sizeof(POOLHDR) + sizeof(POOLFTR);
    mux_assert(pools[poolnum].pool_client_size < pools[poolnum].pool_alloc_size);
    pools[poolnum].poolmagic = CRC32_ProcessInteger2(poolnum, poolsize);
    pools[poolnum].free_stack.clear();
    pools[poolnum].all_buffers.clear();
    pools[poolnum].tot_alloc = 0;
    pools[poolnum].num_alloc = 0;
    pools[poolnum].max_alloc = 0;
    pools[poolnum].num_lost = 0;
}

/*! \brief Helper function for logging pool errors.
 */
static void pool_err
(
    const UTF8 *logsys,
    int                logflag,
    int                poolnum,
    const UTF8 *tag,
    POOLHDR* ph,
    const UTF8 *action,
    const UTF8 *reason,
    const UTF8 *file,
    const int          line
)
{
    UNUSED_PARAMETER(logflag);

    mux_fprintf(stderr, T("%s %s[%d] (tag %s) %s in %s line %d at %p." ENDLINE),
        logsys, action, pools[poolnum].pool_client_size, tag, reason,
        file, line, ph);
}

/*! \brief Validates the buffers in the all_buffers list.
 *
 * Walks all allocated buffers and checks that magic numbers are correct.
 * Reports errors but does not unlink or leak buffers — that strategy was
 * a relic of the intrusive-list era.
 */
static void pool_vfy
(
    int poolnum,
    const UTF8 *tag,
    const UTF8 *file,
    const int line
)
{
    const size_t psize = pools[poolnum].pool_client_size;
    for (char *raw : pools[poolnum].all_buffers)
    {
        auto ph = reinterpret_cast<POOLHDR *>(raw);
        auto h = raw + sizeof(POOLHDR);
        auto pf = reinterpret_cast<POOLFTR *>(h + pools[poolnum].pool_client_size);

        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Verify"),
                     T("header corrupted"), file, line);
            continue;
        }

        if (pf->magicnum != pools[poolnum].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Verify"),
                T("footer corrupted"), file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }

        if (ph->pool_size != psize)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph,
                 T("Verify"), T("header has incorrect size"), file, line);
        }
    }
}

static void pool_check(const UTF8 *tag, const UTF8 *file, const int line)
{
    for (int i = 0; i < NUM_POOLS; i++)
    {
        pool_vfy(i, tag, file, line);
    }
}

UTF8 *pool_alloc(int poolnum, const UTF8 *tag, const UTF8 *file, const int line)
{
    if (g_paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    UTF8 *p;
    POOLHDR *ph;
    POOLFTR *pf;

    if (!pools[poolnum].free_stack.empty())
    {
        char *raw = pools[poolnum].free_stack.back();
        pools[poolnum].free_stack.pop_back();
        ph = reinterpret_cast<POOLHDR *>(raw);
        p = reinterpret_cast<UTF8 *>(raw + sizeof(POOLHDR));
        pf = reinterpret_cast<POOLFTR *>(p + pools[poolnum].pool_client_size);

        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Alloc"),
                T("corrupted buffer header on freelist"), file, line);

            // Discard the corrupted freelist and fall through to allocate new.
            //
            pools[poolnum].num_lost += pools[poolnum].free_stack.size() + 1;
            pools[poolnum].free_stack.clear();
        }
        else
        {
            // Check for corrupted footer, just report and fix it.
            //
            if (pf->magicnum != pools[poolnum].poolmagic)
            {
                pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Alloc"),
                    T("corrupted buffer footer"), file, line);
                pf->magicnum = pools[poolnum].poolmagic;
            }

            ph->u.buf_tag = tag;
            pools[poolnum].tot_alloc++;
            pools[poolnum].num_alloc++;

            // If the buffer was modified after it was last freed, log it.
            //
            auto pui = reinterpret_cast<unsigned *>(p);
            if (*pui != pools[poolnum].poolmagic)
            {
                pool_err(T("BUG"), LOG_PROBLEMS, poolnum, tag, ph, T("Alloc"),
                    T("buffer modified after free"), file, line);
            }
            *pui = 0;
            return p;
        }
    }

    // Allocate a new buffer from the system.
    //
    char *raw = nullptr;
    try
    {
        raw = new char[pools[poolnum].pool_alloc_size];
    }
    catch (...)
    {
    }

    if (nullptr == raw)
    {
        OutOfMemory(reinterpret_cast<const UTF8 *>(__FILE__), __LINE__);
        return nullptr;
    }

    pools[poolnum].all_buffers.push_back(raw);

    ph = reinterpret_cast<POOLHDR *>(raw);
    p = reinterpret_cast<UTF8 *>(raw + sizeof(POOLHDR));
    pf = reinterpret_cast<POOLFTR *>(p + pools[poolnum].pool_client_size);

    ph->magicnum = pools[poolnum].poolmagic;
    ph->pool_size = pools[poolnum].pool_client_size;
    pf->magicnum = pools[poolnum].poolmagic;
    *reinterpret_cast<unsigned *>(p) = pools[poolnum].poolmagic;
    pools[poolnum].max_alloc++;

    ph->u.buf_tag = tag;
    pools[poolnum].tot_alloc++;
    pools[poolnum].num_alloc++;

    auto pui = reinterpret_cast<unsigned *>(p);
    *pui = 0;
    return p;
}

UTF8 *pool_alloc_lbuf(const UTF8 *tag, const UTF8 *file, const int line)
{
    if (g_paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    UTF8 *p;
    POOLHDR *ph;
    POOLFTR *pf;

    if (!pools[POOL_LBUF].free_stack.empty())
    {
        char *raw = pools[POOL_LBUF].free_stack.back();
        pools[POOL_LBUF].free_stack.pop_back();
        ph = reinterpret_cast<POOLHDR *>(raw);
        p = reinterpret_cast<UTF8 *>(raw + sizeof(POOLHDR));
        pf = reinterpret_cast<POOLFTR *>(p + LBUF_SIZE);

        if (ph->magicnum != pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, tag, ph, T("Alloc"),
                T("corrupted buffer header on freelist"), file, line);

            pools[POOL_LBUF].num_lost += pools[POOL_LBUF].free_stack.size() + 1;
            pools[POOL_LBUF].free_stack.clear();
        }
        else
        {
            if (pf->magicnum != pools[POOL_LBUF].poolmagic)
            {
                pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, tag, ph, T("Alloc"),
                    T("corrupted buffer footer"), file, line);
                pf->magicnum = pools[POOL_LBUF].poolmagic;
            }

            ph->u.buf_tag = tag;
            pools[POOL_LBUF].tot_alloc++;
            pools[POOL_LBUF].num_alloc++;

            auto pui = reinterpret_cast<unsigned *>(p);
            if (*pui != pools[POOL_LBUF].poolmagic)
            {
                pool_err(T("BUG"), LOG_PROBLEMS, POOL_LBUF, tag, ph, T("Alloc"),
                    T("buffer modified after free"), file, line);
            }
            *pui = 0;
            return p;
        }
    }

    char *raw = nullptr;
    try
    {
        raw = new char[LBUF_SIZE + sizeof(POOLHDR) + sizeof(POOLFTR)];
    }
    catch (...)
    {
    }

    if (nullptr == raw)
    {
        OutOfMemory(reinterpret_cast<const UTF8 *>(__FILE__), __LINE__);
        return nullptr;
    }

    pools[POOL_LBUF].all_buffers.push_back(raw);

    ph = reinterpret_cast<POOLHDR *>(raw);
    p = reinterpret_cast<UTF8 *>(raw + sizeof(POOLHDR));
    pf = reinterpret_cast<POOLFTR *>(p + LBUF_SIZE);

    ph->magicnum = pools[POOL_LBUF].poolmagic;
    ph->pool_size = LBUF_SIZE;
    pf->magicnum = pools[POOL_LBUF].poolmagic;
    *reinterpret_cast<unsigned *>(p) = pools[POOL_LBUF].poolmagic;
    pools[POOL_LBUF].max_alloc++;

    ph->u.buf_tag = tag;
    pools[POOL_LBUF].tot_alloc++;
    pools[POOL_LBUF].num_alloc++;

    auto pui = reinterpret_cast<unsigned *>(p);
    *pui = 0;
    return p;
}

void pool_free(int poolnum, UTF8* buf, const UTF8* file, const int line)
{
    if (buf == nullptr)
    {
        mux_fprintf(stderr, T("BUG ALLOC: Attempt to free null pointer in %s line %d." ENDLINE), file, line);
        return;
    }

    char *raw = reinterpret_cast<char *>(buf) - sizeof(POOLHDR);
    POOLHDR *ph = reinterpret_cast<POOLHDR *>(raw);
    const auto pf = reinterpret_cast<POOLFTR *>(buf + pools[poolnum].pool_client_size);
    const auto pui = reinterpret_cast<unsigned *>(buf);

    if (g_paranoid_alloc)
    {
        pool_check(ph->u.buf_tag, file, line);
    }

    // Make sure the buffer header is good.  If it isn't, log the error and
    // throw away the buffer.
    //
    if (ph->magicnum != pools[poolnum].poolmagic)
    {
        pool_err(T("BUG"), LOG_ALWAYS, poolnum, ph->u.buf_tag, ph, T("Free"),
                 T("corrupted buffer header"), file, line);
        pools[poolnum].num_lost++;
        pools[poolnum].num_alloc--;
        pools[poolnum].tot_alloc--;
        return;
    }

    // Verify the buffer footer.  Don't unlink if damaged, just repair.
    //
    if (pf->magicnum != pools[poolnum].poolmagic)
    {
        pool_err(T("BUG"), LOG_ALWAYS, poolnum, ph->u.buf_tag, ph, T("Free"),
             T("corrupted buffer footer"), file, line);
        pf->magicnum = pools[poolnum].poolmagic;
    }

    // Verify that we are not trying to free someone else's buffer.
    //
    if (ph->pool_size != pools[poolnum].pool_client_size)
    {
        pool_err(T("BUG"), LOG_ALWAYS, poolnum, ph->u.buf_tag, ph, T("Free"),
                 T("Attempt to free into a different pool."), file, line);
        return;
    }

    // Make sure we aren't freeing an already free buffer.  If we are, log an
    // error, otherwise update the pool header and stats.
    //
    if (*pui == pools[poolnum].poolmagic)
    {
        pool_err(T("BUG"), LOG_BUGS, poolnum, ph->u.buf_tag, ph, T("Free"),
                 T("buffer already freed"), file, line);
    }
    else
    {
        *pui = pools[poolnum].poolmagic;
        pools[poolnum].free_stack.push_back(raw);
        pools[poolnum].num_alloc--;
    }
}

void pool_free_lbuf(UTF8 *buf, const UTF8 *file, const int line)
{
    if (buf == nullptr)
    {
        mux_fprintf(stderr, T("BUG ALLOC: Attempt to free_lbuf null pointer in %s line %d." ENDLINE), file, line);
        return;
    }

    char *raw = reinterpret_cast<char *>(buf) - sizeof(POOLHDR);
    POOLHDR *ph = reinterpret_cast<POOLHDR *>(raw);
    const auto pf = reinterpret_cast<POOLFTR *>(buf + LBUF_SIZE);
    const auto pui = reinterpret_cast<unsigned *>(buf);

    if (g_paranoid_alloc)
    {
        pool_check(ph->u.buf_tag, file, line);
    }

    if (  ph->magicnum != pools[POOL_LBUF].poolmagic
       || pf->magicnum != pools[POOL_LBUF].poolmagic
       || ph->pool_size != LBUF_SIZE
       || *pui == pools[POOL_LBUF].poolmagic)
    {
        if (ph->magicnum != pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                     T("corrupted buffer header"), file, line);
            pools[POOL_LBUF].num_lost++;
            pools[POOL_LBUF].num_alloc--;
            pools[POOL_LBUF].tot_alloc--;
            return;
        }
        else if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("corrupted buffer footer"), file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
        else if (ph->pool_size != LBUF_SIZE)
        {
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("Attempt to free into a different pool."), file, line);
            return;
        }

        if (*pui == pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_BUGS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("buffer already freed"), file, line);
            return;
        }
    }

    *pui = pools[POOL_LBUF].poolmagic;
    pools[POOL_LBUF].free_stack.push_back(raw);
    pools[POOL_LBUF].num_alloc--;
}

static inline void alloc_notify(dbref player, const UTF8 *msg)
{
    if (g_alloc_notify_fn)
    {
        g_alloc_notify_fn(player, msg);
    }
}

static void pool_trace(const dbref player, const int poolnum, const UTF8 *text)
{
    int numfree = 0;
    alloc_notify(player, tprintf(T("----- %s -----"), text));
    for (char *raw : pools[poolnum].all_buffers)
    {
        auto ph = reinterpret_cast<POOLHDR *>(raw);
        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            alloc_notify(player, T("*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***"));
            alloc_notify(player, tprintf(T("%d free %s (before corruption)"),
                       numfree, text));
            return;
        }
        auto ibuf = reinterpret_cast<unsigned *>(raw + sizeof(POOLHDR));
        if (*ibuf != pools[poolnum].poolmagic)
        {
            alloc_notify(player, ph->u.buf_tag);
        }
        else
        {
            numfree++;
        }
    }
    alloc_notify(player, tprintf(T("%d free %s"), numfree, text));
}

void list_bufstats(dbref player)
{
    alloc_notify(player, T("Buffer Stats  Size      InUse      Total           Allocs   Lost"));
    for (int i = 0; i < NUM_POOLS; i++)
    {
        UTF8 buff[MBUF_SIZE];
        UTF8 *p = buff;

        p += LeftJustifyString(p,  12, poolnames[i]);                   *p++ = ' ';
        p += RightJustifyNumber(p,  5, static_cast<int64_t>(pools[i].pool_client_size), ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 10, static_cast<int64_t>(pools[i].num_alloc),        ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 10, static_cast<int64_t>(pools[i].max_alloc),        ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 16, static_cast<int64_t>(pools[i].tot_alloc),        ' '); *p++ = ' ';
        p += RightJustifyNumber(p,  6, static_cast<int64_t>(pools[i].num_lost),         ' '); *p = '\0';
        alloc_notify(player, buff);
    }
}

void list_buftrace(dbref player)
{
    for (int i = 0; i < NUM_POOLS; i++)
    {
        pool_trace(player, i, poolnames[i]);
    }
}

void pool_reset(void)
{
    for (auto &pool : pools)
    {
        // Build a set of free buffer pointers for fast lookup.
        //
        std::unordered_set<char *> free_set(
            pool.free_stack.begin(), pool.free_stack.end());
        pool.free_stack.clear();

        // Walk all_buffers: delete free ones, keep in-use ones.
        //
        auto new_end = std::remove_if(pool.all_buffers.begin(),
            pool.all_buffers.end(),
            [&free_set](char *raw) -> bool
            {
                if (free_set.count(raw))
                {
                    delete[] raw;
                    return true;
                }
                return false;
            });
        pool.all_buffers.erase(new_end, pool.all_buffers.end());
        pool.max_alloc = pool.num_alloc;
    }
}
