/*! \file alloc.cpp
 * \brief Memory Allocation Subsystem.
 *
 * The functions here manage pools of often-used buffers of fixed-size.  It
 * adds value by greatly reducing the number and strength of calls to the
 * underlying platform's memory management. It also adds headers and footer to
 * detect misuse of buffers by its callers.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

/*! \brief Per-buffer header to manage and organize client allocation.
 *
 * The POOLHDR structure preceeds a client area which must be properly
 * aligned to avoid faults when the client accesses structure members within
 * in the client area.  64-bit alignment should be sufficient.
 *
 * The magicnum and pool_size are chosen when the pool it initialized. next
 * and nxtfree are managed as buffers are allocated and freed. buf_tag comes
 * from the client so that buffers can be associated with the places that
 * allocated them.
 */

typedef struct pool_header
{
    unsigned int        magicnum;   // For consistency check
    size_t              pool_size;  // For consistency check
    struct pool_header *next;       // Next pool header in chain
    struct pool_header *nxtfree;    // Next pool header in freelist
    union
    {
        const UTF8     *buf_tag;    // Debugging/trace tag
        UINT64 align;               // Not used.
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

/*! \brief Per-pool structure containing statistics and list heads.
 *
 * The head of the free list and in-use list are contained here.
 */

typedef struct pooldata
{
    size_t pool_client_size;        // Size in bytes of a buffer as seen by client.
    size_t pool_alloc_size;         // Size as allocated from system.
    unsigned int poolmagic;         // Magic number specific to this pool
    POOLHDR *free_head;             // Buffer freelist head
    POOLHDR *chain_head;            // Buffer chain head
    UINT64 tot_alloc;               // Total buffers allocated
    UINT64 num_alloc;               // Number of buffers currently allocated
    UINT64 max_alloc;               // Max # buffers allocated at one time
    UINT64 num_lost;                // Buffers lost due to corruption
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
    T("Pcaches"),
    T("Lbufrefs"),
    T("Regrefs"),
    T("Strings")
};

/*! \brief Initialize a buffer pool.
 *
 * This is done once. The client size, magic, and allocation size are chosen
 * at this time, and the free list and in-use list are initialized to empty.
 * After this initialization, allocations can be done.
 *
 * \param poolnum  An integer uniquely indicating which pool.
 * \param poolsize The size of the client area this pool supports.
 * \return         None.
 */

void pool_init(int poolnum, int poolsize)
{
    pools[poolnum].pool_client_size = poolsize;
    pools[poolnum].pool_alloc_size  = poolsize + sizeof(POOLHDR) + sizeof(POOLFTR);
    mux_assert(pools[poolnum].pool_client_size < pools[poolnum].pool_alloc_size);
    pools[poolnum].poolmagic = CRC32_ProcessInteger2(poolnum, poolsize);
    pools[poolnum].free_head = nullptr;
    pools[poolnum].chain_head = nullptr;
    pools[poolnum].tot_alloc = 0;
    pools[poolnum].num_alloc = 0;
    pools[poolnum].max_alloc = 0;
    pools[poolnum].num_lost = 0;
}

/*! \brief Helper function for logging pool errors.
 *
 * Rather than have similar code spread throughout the pool manager, this
 * helper function glosses over access to the logging functions.
 *
 * \param logsys   Primary logging tag.
 * \param logflag  Event class.
 * \param poolnum  Which pool.
 * \param tag      Client tag of problem buffer.
 * \param ph       Pool header address.
 * \param action   Action that discovered the problem.
 * \param reason   prose to explain.
 * \param file     File name of caller.
 * \param line     Line number of caller.
 * \return         None.
 */

static void pool_err
(
    __in const UTF8 *logsys,
    int              logflag,
    int              poolnum,
    __in const UTF8 *tag,
    __in POOLHDR    *ph,
    __in const UTF8 *action,
    __in const UTF8 *reason,
    __in const UTF8 *file,
    const int        line
)
{
    if (0 == mudstate.logging)
    {
        STARTLOG(logflag, logsys, "ALLOC");
        Log.tinyprintf(T("%s[%d] (tag %s) %s in %s line %d at %p. (%s)"), action,
            pools[poolnum].pool_client_size, tag, reason, file, line, ph,
            mudstate.debug_cmd);
        ENDLOG;
    }
    else if (LOG_ALLOCATE != logflag)
    {
        Log.tinyprintf(T(ENDLINE "***< %s[%d] (tag %s) %s in %s line %d at %p. >***"),
            action, pools[poolnum].pool_client_size, tag, reason, file, line, ph);
    }
}

/*! \brief Validates the buffers in the in-use list.
 *
 * This walks the in-use list, validates that all the buffers in the list go
 * with the pool in which they appear and that all the magic is correct.  If
 * a buffer is found to be compromised, part of the list is intentionally
 * leaked rather than risk a crash.
 *
 * \param poolnum  Which pool.
 * \param tag      Client tag of problem buffer.
 * \param file     File name of caller.
 * \param line     Line number of caller.
 * \return         None.
 */

static void pool_vfy
(
    int poolnum,
    __in const UTF8 *tag,
    __in const UTF8 *file,
    const int line
)
{
    POOLHDR *lastph = nullptr;
    size_t psize = pools[poolnum].pool_client_size;
    for (POOLHDR *ph = pools[poolnum].chain_head; nullptr != ph; lastph = ph, ph = ph->next)
    {
        UTF8 *h = (UTF8 *)ph;
        h += sizeof(POOLHDR);
        h += pools[poolnum].pool_client_size;
        POOLFTR *pf = (POOLFTR *) h;

        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Verify"),
                     T("header corrupted (clearing freelist)"), file, line);

            // Break the header chain at this point so we don't
            // generate an error for EVERY alloc and free, also we
            // can't continue the scan because the next pointer might
            // be trash too.
            //
            if (nullptr != lastph)
            {
                lastph->next = nullptr;
            }
            else
            {
                pools[poolnum].chain_head = nullptr;
            }

            // It's not safe to continue.
            //
            return;
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
    int i;
    for (i = 0; i < NUM_POOLS; i++)
    {
        pool_vfy(i, tag, file, line);
    }
}

UTF8 *pool_alloc(int poolnum, __in const UTF8 *tag, __in const UTF8 *file, const int line)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    UTF8 *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[poolnum].free_head;
    if (  ph
       && ph->magicnum == pools[poolnum].poolmagic)
    {
        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_client_size);
        pools[poolnum].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != pools[poolnum].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Alloc"),
                T("corrupted buffer footer"), file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err(T("BUG"), LOG_ALWAYS, poolnum, tag, ph, T("Alloc"),
                T("corrupted buffer header"), file, line);

            // Start a new free list and record stats.
            //
            pools[poolnum].free_head = nullptr;
            pools[poolnum].num_lost += (pools[poolnum].tot_alloc
                                     -  pools[poolnum].num_alloc);
            pools[poolnum].tot_alloc = pools[poolnum].num_alloc;
        }

        ph = nullptr;
        try
        {
            ph = reinterpret_cast<POOLHDR *>(new char[pools[poolnum].pool_alloc_size]);
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == ph)
        {
            ISOUTOFMEMORY(ph);
            return nullptr;
        }

        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_client_size);

        // Initialize.
        //
        ph->next = pools[poolnum].chain_head;
        ph->nxtfree = nullptr;
        ph->magicnum = pools[poolnum].poolmagic;
        ph->pool_size = pools[poolnum].pool_client_size;
        pf->magicnum = pools[poolnum].poolmagic;
        *((unsigned int *)p) = pools[poolnum].poolmagic;
        pools[poolnum].chain_head = ph;
        pools[poolnum].max_alloc++;
    }

    ph->u.buf_tag = tag;
    pools[poolnum].tot_alloc++;
    pools[poolnum].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log(T("DBG"), T("ALLOC")))
    {
        Log.tinyprintf(T("Alloc[%d] (tag %s) in %s line %d buffer at %p. (%s)"),
            pools[poolnum].pool_client_size, tag, file, line, ph,
            mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[poolnum].poolmagic)
    {
        pool_err(T("BUG"), LOG_PROBLEMS, poolnum, tag, ph, T("Alloc"),
            T("buffer modified after free"), file, line);
    }
    *pui = 0;
    return p;
}

UTF8 *pool_alloc_lbuf(__in const UTF8 *tag, __in const UTF8 *file, const int line)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    UTF8 *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[POOL_LBUF].free_head;
    if (  ph
       && ph->magicnum == pools[POOL_LBUF].poolmagic)
    {
        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + LBUF_SIZE);
        pools[POOL_LBUF].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, tag, ph, T("Alloc"),
                T("corrupted buffer footer"), file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, tag, ph, T("Alloc"),
                T("corrupted buffer header"), file, line);

            // Start a new free list and record stats.
            //
            pools[POOL_LBUF].free_head = nullptr;
            pools[POOL_LBUF].num_lost += (pools[POOL_LBUF].tot_alloc
                                     -  pools[POOL_LBUF].num_alloc);
            pools[POOL_LBUF].tot_alloc = pools[POOL_LBUF].num_alloc;
        }

        ph = nullptr;
        try
        {
            ph = reinterpret_cast<POOLHDR *>(new char[LBUF_SIZE + sizeof(POOLHDR) + sizeof(POOLFTR)]);
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == ph)
        {
            ISOUTOFMEMORY(ph);
            return nullptr;
        }

        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + LBUF_SIZE);

        // Initialize.
        //
        ph->next = pools[POOL_LBUF].chain_head;
        ph->nxtfree = nullptr;
        ph->magicnum = pools[POOL_LBUF].poolmagic;
        ph->pool_size = LBUF_SIZE;
        pf->magicnum = pools[POOL_LBUF].poolmagic;
        *((unsigned int *)p) = pools[POOL_LBUF].poolmagic;
        pools[POOL_LBUF].chain_head = ph;
        pools[POOL_LBUF].max_alloc++;
    }

    ph->u.buf_tag = tag;
    pools[POOL_LBUF].tot_alloc++;
    pools[POOL_LBUF].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log(T("DBG"), T("ALLOC")))
    {
        Log.tinyprintf(T("Alloc[%d] (tag %s) in %s line %d buffer at %p. (%s)"),
            LBUF_SIZE, tag, file, line, ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[POOL_LBUF].poolmagic)
    {
        pool_err(T("BUG"), LOG_PROBLEMS, POOL_LBUF, tag, ph, T("Alloc"),
            T("buffer modified after free"), file, line);
    }
    *pui = 0;
    return p;
}

void pool_free(int poolnum, __in UTF8 *buf, __in const UTF8 *file, const int line)
{
    if (buf == nullptr)
    {
        STARTLOG(LOG_PROBLEMS, "BUG", "ALLOC")
        log_printf(T("Attempt to free null pointer in %s line %d."), file, line);
        ENDLOG
        return;
    }
    POOLHDR *ph = ((POOLHDR *)buf) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + pools[poolnum].pool_client_size);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
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

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log(T("DBG"), T("ALLOC")))
    {
        Log.tinyprintf(T("Free[%d] (tag %s) in %s line %d buffer at %p. (%s)"),
            pools[poolnum].pool_client_size, ph->u.buf_tag, file, line, ph,
            mudstate.debug_cmd);
        end_log();
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
        ph->nxtfree = pools[poolnum].free_head;
        pools[poolnum].free_head = ph;
        pools[poolnum].num_alloc--;
    }
}

void pool_free_lbuf(__in_ecount(LBUF_SIZE) UTF8 *buf, __in const UTF8 *file, const int line)
{
    if (buf == nullptr)
    {
        STARTLOG(LOG_PROBLEMS, "BUG", "ALLOC")
        log_printf(T("Attempt to free_lbuf null pointer in %s line %d."), file, line);
        ENDLOG
        return;
    }
    POOLHDR *ph = ((POOLHDR *)buf) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + LBUF_SIZE);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
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
            // The buffer header is damaged. Log the error and throw away the
            // buffer.
            //
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                     T("corrupted buffer header"), file, line);
            pools[POOL_LBUF].num_lost++;
            pools[POOL_LBUF].num_alloc--;
            pools[POOL_LBUF].tot_alloc--;
            return;
        }
        else if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            // The buffer footer is damaged.  Don't unlink, just repair.
            //
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("corrupted buffer footer"), file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
        else if (ph->pool_size != LBUF_SIZE)
        {
            // We are trying to free someone else's buffer.
            //
            pool_err(T("BUG"), LOG_ALWAYS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("Attempt to free into a different pool."), file, line);
            return;
        }

        // If we are freeing a buffer that was already free, report an error.
        //
        if (*pui == pools[POOL_LBUF].poolmagic)
        {
            pool_err(T("BUG"), LOG_BUGS, POOL_LBUF, ph->u.buf_tag, ph, T("Free"),
                T("buffer already freed"), file, line);
            return;
        }
    }

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log(T("DBG"), T("ALLOC")))
    {
        Log.tinyprintf(T("Free[%d] (tag %s) in %s line %d buffer at %p. (%s)"),
            LBUF_SIZE, ph->u.buf_tag, file, line, ph, mudstate.debug_cmd);
        end_log();
    }

    // Update the pool header and stats.
    //
    *pui = pools[POOL_LBUF].poolmagic;
    ph->nxtfree = pools[POOL_LBUF].free_head;
    pools[POOL_LBUF].free_head = ph;
    pools[POOL_LBUF].num_alloc--;
}

static void pool_trace(dbref player, int poolnum, __in const UTF8 *text)
{
    POOLHDR *ph;
    int numfree = 0;
    notify(player, tprintf(T("----- %s -----"), text));
    for (ph = pools[poolnum].chain_head; ph != nullptr; ph = ph->next)
    {
        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            notify(player, T("*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***"));
            notify(player, tprintf(T("%d free %s (before corruption)"),
                       numfree, text));
            return;
        }
        char *h = (char *)ph;
        h += sizeof(POOLHDR);
        unsigned int *ibuf = (unsigned int *)h;
        if (*ibuf != pools[poolnum].poolmagic)
        {
            notify(player, ph->u.buf_tag);
        }
        else
        {
            numfree++;
        }
    }
    notify(player, tprintf(T("%d free %s"), numfree, text));
}

void list_bufstats(dbref player)
{
    notify(player, T("Buffer Stats  Size      InUse      Total           Allocs   Lost"));
    for (int i = 0; i < NUM_POOLS; i++)
    {
        UTF8 buff[MBUF_SIZE];
        UTF8 *p = buff;

        p += LeftJustifyString(p,  12, poolnames[i]);                   *p++ = ' ';
        p += RightJustifyNumber(p,  5, pools[i].pool_client_size, ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 10, pools[i].num_alloc,        ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 10, pools[i].max_alloc,        ' '); *p++ = ' ';
        p += RightJustifyNumber(p, 16, pools[i].tot_alloc,        ' '); *p++ = ' ';
        p += RightJustifyNumber(p,  6, pools[i].num_lost,         ' '); *p++ = '\0';
        notify(player, buff);
    }
}

void list_buftrace(dbref player)
{
    int i;
    for (i = 0; i < NUM_POOLS; i++)
    {
        pool_trace(player, i, poolnames[i]);
    }
}

void pool_reset(void)
{
    int i;
    for (i = 0; i < NUM_POOLS; i++)
    {
        POOLHDR *newchain = nullptr;
        POOLHDR *phnext;
        POOLHDR *ph;
        for (ph = pools[i].chain_head; ph != nullptr; ph = phnext)
        {
            char *h = (char *)ph;
            phnext = ph->next;
            h += sizeof(POOLHDR);
            unsigned int *ibuf = (unsigned int *)h;
            if (*ibuf == pools[i].poolmagic)
            {
                char *p = reinterpret_cast<char *>(ph);
                delete [] p;
                ph = nullptr;
            }
            else
            {
                ph->next = newchain;
                newchain = ph;
                ph->nxtfree = nullptr;
            }
        }
        pools[i].chain_head = newchain;
        pools[i].free_head = nullptr;
        pools[i].max_alloc = pools[i].num_alloc;
    }
}
