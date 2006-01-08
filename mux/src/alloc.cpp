// alloc.cpp -- Memory Allocation Subsystem.
//
// $Id: alloc.cpp,v 1.11 2006-01-08 10:11:59 sdennis Exp $
//
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
    char               *buf_tag;    // Debugging/trace tag
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
    char               *buf_tag;    // Debugging/trace tag
    char  PaddingTo64bits[7 - ((sizeof(POOLHDR_UNALIGNED)-1) & 7)];
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
static const char *poolnames[] =
{
    "Lbufs", "Sbufs", "Mbufs", "Bools", "Descs", "Qentries", "Pcaches"
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
    const char *logsys,
    int         logflag,
    int         poolnum,
    const char *tag,
    POOLHDR    *ph,
    const char *action,
    const char *reason,
    const char *file,
    const int   line
)
{
    if (mudstate.logging == 0)
    {
        STARTLOG(logflag, logsys, "ALLOC");
        Log.tinyprintf("%s[%d] (tag %s) %s in %s line %d at %lx. (%s)", action,
            pools[poolnum].pool_client_size, tag, reason, file, line, (long)ph,
            mudstate.debug_cmd);
        ENDLOG;
    }
    else if (logflag != LOG_ALLOCATE)
    {
        Log.tinyprintf(ENDLINE "***< %s[%d] (tag %s) %s in %s line %d at %lx. >***",
            action, pools[poolnum].pool_client_size, tag, reason, file, line, (long)ph);
    }
}

static void pool_vfy(int poolnum, const char *tag, const char *file, const int line)
{
    POOLHDR *ph, *lastph;
    POOLFTR *pf;
    char *h;

    lastph = NULL;
    size_t psize = pools[poolnum].pool_client_size;
    for (ph = pools[poolnum].chain_head; ph; lastph = ph, ph = ph->next)
    {
        h = (char *)ph;
        h += sizeof(POOLHDR);
        h += pools[poolnum].pool_client_size;
        pf = (POOLFTR *) h;

        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Verify",
                     "header corrupted (clearing freelist)", file, line);

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
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Verify",
                "footer corrupted", file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }
        if (ph->pool_size != psize)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
                 "Verify", "header has incorrect size", file, line);
        }
    }
}

static void pool_check(const char *tag, const char *file, const int line)
{
    int i;
    for (i = 0; i < NUM_POOLS; i++)
    {
        pool_vfy(i, tag, file, line);
    }
}

char *pool_alloc(int poolnum, const char *tag, const char *file, const int line)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    char *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[poolnum].free_head;
    if (  ph
       && ph->magicnum == pools[poolnum].poolmagic)
    {
        p = (char *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_client_size);
        pools[poolnum].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != pools[poolnum].poolmagic)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Alloc",
                "corrupted buffer footer", file, line);
            pf->magicnum = pools[poolnum].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Alloc",
                "corrupted buffer header", file, line);

            // Start a new free list and record stats.
            //
            pools[poolnum].free_head = NULL;
            pools[poolnum].num_lost += (pools[poolnum].tot_alloc
                                     -  pools[poolnum].num_alloc);
            pools[poolnum].tot_alloc = pools[poolnum].num_alloc;
        }

        ph = (POOLHDR *)MEMALLOC(pools[poolnum].pool_alloc_size);
        ISOUTOFMEMORY(ph);
        p = (char *)(ph + 1);
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

    ph->buf_tag = (char *)tag;
    pools[poolnum].tot_alloc++;
    pools[poolnum].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) in %s line %d buffer at %lx. (%s)",
            pools[poolnum].pool_client_size, tag, file, line, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[poolnum].poolmagic)
    {
        pool_err("BUG", LOG_PROBLEMS, poolnum, tag, ph, "Alloc",
            "buffer modified after free", file, line);
    }
    *pui = 0;
    return p;
}

char *pool_alloc_lbuf(const char *tag, const char *file, const int line)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag, file, line);
    }

    char *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[POOL_LBUF].free_head;
    if (  ph
       && ph->magicnum == pools[POOL_LBUF].poolmagic)
    {
        p = (char *)(ph + 1);
        pf = (POOLFTR *)(p + LBUF_SIZE);
        pools[POOL_LBUF].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, "Alloc",
                "corrupted buffer footer", file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, "Alloc",
                "corrupted buffer header", file, line);

            // Start a new free list and record stats.
            //
            pools[POOL_LBUF].free_head = NULL;
            pools[POOL_LBUF].num_lost += (pools[POOL_LBUF].tot_alloc
                                     -  pools[POOL_LBUF].num_alloc);
            pools[POOL_LBUF].tot_alloc = pools[POOL_LBUF].num_alloc;
        }

        ph = (POOLHDR *)MEMALLOC(LBUF_SIZE + sizeof(POOLHDR)
           + sizeof(POOLFTR));
        ISOUTOFMEMORY(ph);
        p = (char *)(ph + 1);
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

    ph->buf_tag = (char *)tag;
    pools[POOL_LBUF].tot_alloc++;
    pools[POOL_LBUF].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) in %s line %d buffer at %lx. (%s)",
            LBUF_SIZE, tag, file, line, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != pools[POOL_LBUF].poolmagic)
    {
        pool_err("BUG", LOG_PROBLEMS, POOL_LBUF, tag, ph, "Alloc",
            "buffer modified after free", file, line);
    }
    *pui = 0;
    return p;
}

void pool_free(int poolnum, char *buf, const char *file, const int line)
{
    if (buf == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "BUG", "ALLOC")
        log_text(tprintf("Attempt to free null pointer in %s line %d.", file, line));
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
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
                 "corrupted buffer header", file, line);
        pools[poolnum].num_lost++;
        pools[poolnum].num_alloc--;
        pools[poolnum].tot_alloc--;
        return;
    }

    // Verify the buffer footer.  Don't unlink if damaged, just repair.
    //
    if (pf->magicnum != pools[poolnum].poolmagic)
    {
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
             "corrupted buffer footer", file, line);
        pf->magicnum = pools[poolnum].poolmagic;
    }

    // Verify that we are not trying to free someone else's buffer.
    //
    if (ph->pool_size != pools[poolnum].pool_client_size)
    {
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
                 "Attempt to free into a different pool.", file, line);
        return;
    }

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) in %s line %d buffer at %lx. (%s)",
            pools[poolnum].pool_client_size, ph->buf_tag, file, line, (long)ph,
            mudstate.debug_cmd);
        end_log();
    }

    // Make sure we aren't freeing an already free buffer.  If we are, log an
    // error, otherwise update the pool header and stats.
    //
    if (*pui == pools[poolnum].poolmagic)
    {
        pool_err("BUG", LOG_BUGS, poolnum, ph->buf_tag, ph, "Free",
                 "buffer already freed", file, line);
    }
    else
    {
        *pui = pools[poolnum].poolmagic;
        ph->nxtfree = pools[poolnum].free_head;
        pools[poolnum].free_head = ph;
        pools[poolnum].num_alloc--;
    }
}

void pool_free_lbuf(char *buf, const char *file, const int line)
{
    if (buf == NULL)
    {
        STARTLOG(LOG_PROBLEMS, "BUG", "ALLOC")
        log_text(tprintf("Attempt to free_lbuf null pointer in %s line %d.", file, line));
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
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                     "corrupted buffer header", file, line);
            pools[POOL_LBUF].num_lost++;
            pools[POOL_LBUF].num_alloc--;
            pools[POOL_LBUF].tot_alloc--;
            return;
        }
        else if (pf->magicnum != pools[POOL_LBUF].poolmagic)
        {
            // The buffer footer is damaged.  Don't unlink, just repair.
            //
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                "corrupted buffer footer", file, line);
            pf->magicnum = pools[POOL_LBUF].poolmagic;
        }
        else if (ph->pool_size != LBUF_SIZE)
        {
            // We are trying to free someone else's buffer.
            //
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                "Attempt to free into a different pool.", file, line);
            return;
        }

        // If we are freeing a buffer that was already free, report an error.
        //
        if (*pui == pools[POOL_LBUF].poolmagic)
        {
            pool_err("BUG", LOG_BUGS, POOL_LBUF, ph->buf_tag, ph, "Free",
                     "buffer already freed", file, line);
            return;
        }
    }

    if (  (LOG_ALLOCATE & mudconf.log_options)
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) in %s line %d buffer at %lx. (%s)",
            LBUF_SIZE, ph->buf_tag, file, line, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // Update the pool header and stats.
    //
    *pui = pools[POOL_LBUF].poolmagic;
    ph->nxtfree = pools[POOL_LBUF].free_head;
    pools[POOL_LBUF].free_head = ph;
    pools[POOL_LBUF].num_alloc--;
}

static void pool_trace(dbref player, int poolnum, const char *text)
{
    POOLHDR *ph;
    int numfree = 0;
    notify(player, tprintf("----- %s -----", text));
    for (ph = pools[poolnum].chain_head; ph != NULL; ph = ph->next)
    {
        if (ph->magicnum != pools[poolnum].poolmagic)
        {
            notify(player, "*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***");
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
    char buff[MBUF_SIZE];

    notify(player, "Buffer Stats  Size     InUse     Total        Allocs   Lost");

    int i;
    for (i = 0; i < NUM_POOLS; i++)
    {
        char szNumAlloc[22];
        char szMaxAlloc[22];
        char szTotAlloc[22];
        char szNumLost[22];

        mux_i64toa(pools[i].num_alloc, szNumAlloc);
        mux_i64toa(pools[i].max_alloc, szMaxAlloc);
        mux_i64toa(pools[i].tot_alloc, szTotAlloc);
        mux_i64toa(pools[i].num_lost,  szNumLost);

        mux_sprintf(buff, MBUF_SIZE, "%-12s %5u%10s%10s%14s%7s",
            poolnames[i], pools[i].pool_client_size,
            szNumAlloc, szMaxAlloc, szTotAlloc, szNumLost);
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
                MEMFREE(ph);
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

