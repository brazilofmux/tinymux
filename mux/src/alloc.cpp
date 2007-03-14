/*! \file alloc.cpp
 * \brief Memory Allocation Subsystem.
 *
 * $Id$
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

// Do not use the following structure. It is only used to define the
// POOLHDR that follows. The fields in the following structure must
// match POOLHDR in type and order. Doing it this way is a workaround
// for compilers not supporting #pragma pack(sizeof(INT64)).
//
typedef struct pool_header_unaligned
{
    unsigned int        magicnum;   // For consistency check
    size_t              pool_size;  // For consistency check
    struct pool_header *next;       // Next pool header in chain
    struct pool_header *nxtfree;    // Next pool header in freelist
    const UTF8         *buf_tag;    // Debugging/trace tag
} POOLHDR_UNALIGNED;

// The following structure is 64-bit aligned. The fields in the
// following structure must match POOLHDR_UNALIGNED in type and
// order.
//
typedef struct pool_header
{
    unsigned int        magicnum;   // For consistency check
    size_t              pool_size;  // For consistency check
    struct pool_header *next;       // Next pool header in chain
    struct pool_header *nxtfree;    // Next pool header in freelist
    const UTF8         *buf_tag;    // Debugging/trace tag
    UTF8  PaddingTo64bits[7 - ((sizeof(POOLHDR_UNALIGNED)-1) & 7)];
} POOLHDR;

typedef struct pool_footer
{
    unsigned int magicnum;          // For consistency check
} POOLFTR;

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
    (UTF8 *)"Lbufs",
    (UTF8 *)"Sbufs",
    (UTF8 *)"Mbufs",
    (UTF8 *)"Bools",
    (UTF8 *)"Descs",
    (UTF8 *)"Qentries",
    (UTF8 *)"Pcaches",
    (UTF8 *)"Lbufrefs",
    (UTF8 *)"Regrefs",
    (UTF8 *)"Strings"
};

void pool_init(int poolnum, int poolsize)
{
    pools[poolnum].pool_client_size = poolsize;
    pools[poolnum].pool_alloc_size  = poolsize + sizeof(POOLHDR) + sizeof(POOLFTR);
    mux_assert(pools[poolnum].pool_client_size < pools[poolnum].pool_alloc_size);
    pools[poolnum].poolmagic = CRC32_ProcessInteger2(poolnum, poolsize);
    pools[poolnum].free_head = NULL;
    pools[poolnum].chain_head = NULL;
    pools[poolnum].tot_alloc = 0;
    pools[poolnum].num_alloc = 0;
    pools[poolnum].max_alloc = 0;
    pools[poolnum].num_lost = 0;
}

static void pool_err
(
    const UTF8 *logsys,
    int         logflag,
    int         poolnum,
    const UTF8 *tag,
    POOLHDR    *ph,
    const UTF8 *action,
    const UTF8 *reason,
    const UTF8 *file,
    const int   line
)
{
    if (mudstate.logging == 0)
    {
        STARTLOG(logflag, logsys, (UTF8 *)"ALLOC");
        Log.tinyprintf("%s[%d] (tag %s) %s in %s line %d at %p. (%s)", action,
            pools[poolnum].pool_client_size, tag, reason, file, line, ph,
            mudstate.debug_cmd);
        ENDLOG;
    }
    else if (logflag != LOG_ALLOCATE)
    {
        Log.tinyprintf(ENDLINE "***< %s[%d] (tag %s) %s in %s line %d at %p. >***",
            action, pools[poolnum].pool_client_size, tag, reason, file, line, ph);
    }
}

static void pool_vfy(int poolnum, const UTF8 *tag, const UTF8 *file, const int line)
{
    POOLHDR *ph, *lastph;
    POOLFTR *pf;
    UTF8 *h;

    lastph = NULL;
    size_t psize = pools[poolnum].pool_client_size;
    for (ph = pools[poolnum].chain_head; ph; lastph = ph, ph = ph->next)
    {
        h = (UTF8 *)ph;
        h += sizeof(POOLHDR);
        h += pools[poolnum].pool_client_size;
        pf = (POOLFTR *) h;

        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, tag, ph, (UTF8 *)"Verify",
                     (UTF8 *)"header corrupted (clearing freelist)", file, line);

            // Break the header chain at this point so we don't
            // generate an error for EVERY alloc and free, also we
            // can't continue the scan because the next pointer might
            // be trash too.
            //
            if (lastph)
            {
                lastph->next = NULL;
            }
            else
            {
                pools[poolnum].chain_head = NULL;
            }

            // It's not safe to continue.
            //
            return;
        }
        if (pf->magicnum != pools[poolnum].poolmagic)
        {
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, tag, ph, (UTF8 *)"Verify",
                (UTF8 *)"footer corrupted", file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }
        if (ph->pool_size != psize)
        {
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, tag, ph,
                 (UTF8 *)"Verify", (UTF8 *)"header has incorrect size", file, line);
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

UTF8 *pool_alloc(int poolnum, const UTF8 *tag, const UTF8 *file, const int line)
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
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, tag, ph, (UTF8 *)"Alloc",
                (UTF8 *)"corrupted buffer footer", file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, tag, ph, (UTF8 *)"Alloc",
                (UTF8 *)"corrupted buffer header", file, line);

            // Start a new free list and record stats.
            //
            pools[poolnum].free_head = NULL;
            pools[poolnum].num_lost += (pools[poolnum].tot_alloc
                                     -  pools[poolnum].num_alloc);
            pools[poolnum].tot_alloc = pools[poolnum].num_alloc;
        }

        ph = NULL;
        try
        {
            ph = reinterpret_cast<POOLHDR *>(new char[pools[poolnum].pool_alloc_size]);
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == ph)
        {
            ISOUTOFMEMORY(ph);
            return NULL;
        }

        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_client_size);

        // Initialize.
        //
        ph->next = pools[poolnum].chain_head;
        ph->nxtfree = NULL;
        ph->magicnum = pools[poolnum].poolmagic;
        ph->pool_size = pools[poolnum].pool_client_size;
        pf->magicnum = pools[poolnum].poolmagic;
        *((unsigned int *)p) = pools[poolnum].poolmagic;
        pools[poolnum].chain_head = ph;
        pools[poolnum].max_alloc++;
    }

    ph->buf_tag = tag;
    pools[poolnum].tot_alloc++;
    pools[poolnum].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log((UTF8 *)"DBG", (UTF8 *)"ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) in %s line %d buffer at %p. (%s)",
            pools[poolnum].pool_client_size, tag, file, line, ph,
            mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[poolnum].poolmagic)
    {
        pool_err((UTF8 *)"BUG", LOG_PROBLEMS, poolnum, tag, ph, (UTF8 *)"Alloc",
            (UTF8 *)"buffer modified after free", file, line);
    }
    *pui = 0;
    return p;
}

UTF8 *pool_alloc_lbuf(const UTF8 *tag, const UTF8 *file, const int line)
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
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, (UTF8 *)"Alloc",
                (UTF8 *)"corrupted buffer footer", file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, (UTF8 *)"Alloc",
                (UTF8 *)"corrupted buffer header", file, line);

            // Start a new free list and record stats.
            //
            pools[POOL_LBUF].free_head = NULL;
            pools[POOL_LBUF].num_lost += (pools[POOL_LBUF].tot_alloc
                                     -  pools[POOL_LBUF].num_alloc);
            pools[POOL_LBUF].tot_alloc = pools[POOL_LBUF].num_alloc;
        }

        ph = NULL;
        try
        {
            ph = reinterpret_cast<POOLHDR *>(new char[LBUF_SIZE + sizeof(POOLHDR) + sizeof(POOLFTR)]);
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == ph)
        {
            ISOUTOFMEMORY(ph);
            return NULL;
        }

        p = (UTF8 *)(ph + 1);
        pf = (POOLFTR *)(p + LBUF_SIZE);

        // Initialize.
        //
        ph->next = pools[POOL_LBUF].chain_head;
        ph->nxtfree = NULL;
        ph->magicnum = pools[POOL_LBUF].poolmagic;
        ph->pool_size = LBUF_SIZE;
        pf->magicnum = pools[POOL_LBUF].poolmagic;
        *((unsigned int *)p) = pools[POOL_LBUF].poolmagic;
        pools[POOL_LBUF].chain_head = ph;
        pools[POOL_LBUF].max_alloc++;
    }

    ph->buf_tag = tag;
    pools[POOL_LBUF].tot_alloc++;
    pools[POOL_LBUF].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log((UTF8 *)"DBG", (UTF8 *)"ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) in %s line %d buffer at %p. (%s)",
            LBUF_SIZE, tag, file, line, ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[POOL_LBUF].poolmagic)
    {
        pool_err((UTF8 *)"BUG", LOG_PROBLEMS, POOL_LBUF, tag, ph, (UTF8 *)"Alloc",
            (UTF8 *)"buffer modified after free", file, line);
    }
    *pui = 0;
    return p;
}

void pool_free(int poolnum, UTF8 *buf, const UTF8 *file, const int line)
{
    if (buf == NULL)
    {
        STARTLOG(LOG_PROBLEMS, (UTF8 *)"BUG", (UTF8 *)"ALLOC")
        log_printf("Attempt to free null pointer in %s line %d.", file, line);
        ENDLOG
        return;
    }
    POOLHDR *ph = ((POOLHDR *)(buf)) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + pools[poolnum].pool_client_size);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
    {
        pool_check(ph->buf_tag, file, line);
    }

    // Make sure the buffer header is good.  If it isn't, log the error and
    // throw away the buffer.
    //
    if (ph->magicnum != pools[poolnum].poolmagic)
    {
        pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, (UTF8 *)"Free",
                 (UTF8 *)"corrupted buffer header", file, line);
        pools[poolnum].num_lost++;
        pools[poolnum].num_alloc--;
        pools[poolnum].tot_alloc--;
        return;
    }

    // Verify the buffer footer.  Don't unlink if damaged, just repair.
    //
    if (pf->magicnum != pools[poolnum].poolmagic)
    {
        pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, (UTF8 *)"Free",
             (UTF8 *)"corrupted buffer footer", file, line);
        pf->magicnum = pools[poolnum].poolmagic;
    }

    // Verify that we are not trying to free someone else's buffer.
    //
    if (ph->pool_size != pools[poolnum].pool_client_size)
    {
        pool_err((UTF8 *)"BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, (UTF8 *)"Free",
                 (UTF8 *)"Attempt to free into a different pool.", file, line);
        return;
    }

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log((UTF8 *)"DBG", (UTF8 *)"ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) in %s line %d buffer at %p. (%s)",
            pools[poolnum].pool_client_size, ph->buf_tag, file, line, ph,
            mudstate.debug_cmd);
        end_log();
    }

    // Make sure we aren't freeing an already free buffer.  If we are, log an
    // error, otherwise update the pool header and stats.
    //
    if (*pui == pools[poolnum].poolmagic)
    {
        pool_err((UTF8 *)"BUG", LOG_BUGS, poolnum, ph->buf_tag, ph, (UTF8 *)"Free",
                 (UTF8 *)"buffer already freed", file, line);
    }
    else
    {
        *pui = pools[poolnum].poolmagic;
        ph->nxtfree = pools[poolnum].free_head;
        pools[poolnum].free_head = ph;
        pools[poolnum].num_alloc--;
    }
}

void pool_free_lbuf(UTF8 *buf, const UTF8 *file, const int line)
{
    if (buf == NULL)
    {
        STARTLOG(LOG_PROBLEMS, (UTF8 *)"BUG", (UTF8 *)"ALLOC")
        log_printf("Attempt to free_lbuf null pointer in %s line %d.", file, line);
        ENDLOG
        return;
    }
    POOLHDR *ph = ((POOLHDR *)(buf)) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + LBUF_SIZE);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
    {
        pool_check(ph->buf_tag, file, line);
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
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, (UTF8 *)"Free",
                     (UTF8 *)"corrupted buffer header", file, line);
            pools[POOL_LBUF].num_lost++;
            pools[POOL_LBUF].num_alloc--;
            pools[POOL_LBUF].tot_alloc--;
            return;
        }
        else if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            // The buffer footer is damaged.  Don't unlink, just repair.
            //
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, (UTF8 *)"Free",
                (UTF8 *)"corrupted buffer footer", file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
        else if (ph->pool_size != LBUF_SIZE)
        {
            // We are trying to free someone else's buffer.
            //
            pool_err((UTF8 *)"BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, (UTF8 *)"Free",
                (UTF8 *)"Attempt to free into a different pool.", file, line);
            return;
        }

        // If we are freeing a buffer that was already free, report an error.
        //
        if (*pui == pools[POOL_LBUF].poolmagic)
        {
            pool_err((UTF8 *)"BUG", LOG_BUGS, POOL_LBUF, ph->buf_tag, ph, (UTF8 *)"Free",
                (UTF8 *)"buffer already freed", file, line);
            return;
        }
    }

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log((UTF8 *)"DBG", (UTF8 *)"ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) in %s line %d buffer at %p. (%s)",
            LBUF_SIZE, ph->buf_tag, file, line, ph, mudstate.debug_cmd);
        end_log();
    }

    // Update the pool header and stats.
    //
    *pui = pools[POOL_LBUF].poolmagic;
    ph->nxtfree = pools[POOL_LBUF].free_head;
    pools[POOL_LBUF].free_head = ph;
    pools[POOL_LBUF].num_alloc--;
}

static void pool_trace(dbref player, int poolnum, const UTF8 *text)
{
    POOLHDR *ph;
    int numfree = 0;
    notify(player, tprintf("----- %s -----", text));
    for (ph = pools[poolnum].chain_head; ph != NULL; ph = ph->next)
    {
        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            notify(player, (UTF8 *)"*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***");
            notify(player, tprintf("%d free %s (before corruption)",
                       numfree, text));
            return;
        }
        char *h = (char *)ph;
        h += sizeof(POOLHDR);
        unsigned int *ibuf = (unsigned int *)h;
        if (*ibuf != pools[poolnum].poolmagic)
        {
            notify(player, ph->buf_tag);
        }
        else
        {
            numfree++;
        }
    }
    notify(player, tprintf("%d free %s", numfree, text));
}

void list_bufstats(dbref player)
{
    notify(player, (UTF8 *)"Buffer Stats  Size      InUse      Total           Allocs   Lost");
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
        POOLHDR *newchain = NULL;
        POOLHDR *phnext;
        POOLHDR *ph;
        for (ph = pools[i].chain_head; ph != NULL; ph = phnext)
        {
            char *h = (char *)ph;
            phnext = ph->next;
            h += sizeof(POOLHDR);
            unsigned int *ibuf = (unsigned int *)h;
            if (*ibuf == pools[i].poolmagic)
            {
                char *p = reinterpret_cast<char *>(ph);
                delete [] p;
                ph = NULL;
            }
            else
            {
                ph->next = newchain;
                newchain = ph;
                ph->nxtfree = NULL;
            }
        }
        pools[i].chain_head = newchain;
        pools[i].free_head = NULL;
        pools[i].max_alloc = pools[i].num_alloc;
    }
}

