// alloc.cpp -- Memory Allocation Subsystem.
//
// $Id: alloc.cpp,v 1.13 2001-11-24 20:07:09 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "alloc.h"
#include "mudconf.h"

// Do not use the following structure. It is only used to define the
// POOLHDR that follows. The fields in the following structure must
// match POOLHDR in type and order. Doing it this way is a workaround
// for compilers not supporting #pragma pack(sizeof(INT64)).
//
typedef struct pool_header_unaligned
{
    unsigned int        magicnum;   // For consistency check
    int                 pool_size;  // For consistency check
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
    int                 pool_size;  // For consistency check
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
    int pool_size;                  // Size in bytes of a buffer
    POOLHDR *free_head;             // Buffer freelist head
    POOLHDR *chain_head;            // Buffer chain head
    int tot_alloc;                  // Total buffers allocated
    int num_alloc;                  // Number of buffers currently allocated
    int max_alloc;                  // Max # buffers allocated at one time
    int num_lost;                   // Buffers lost due to corruption
} POOL;

POOL pools[NUM_POOLS];
const char *poolnames[] =
{
    "Sbufs", "Mbufs", "Lbufs", "Bools", "Descs", "Qentries", "Pcaches"
};

#define POOL_MAGICNUM 0xdeadbeefU

void pool_init(int poolnum, int poolsize)
{
    pools[poolnum].pool_size = poolsize;
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
    const char *reason
)
{
    if (mudstate.logging == 0)
    {
        STARTLOG(logflag, logsys, "ALLOC");
        Log.tinyprintf("%s[%d] (tag %s) %s at %lx. (%s)", action,
            pools[poolnum].pool_size, tag, reason, (long)ph,
            mudstate.debug_cmd);
        ENDLOG;
    }
    else if (logflag != LOG_ALLOCATE)
    {
        Log.tinyprintf(ENDLINE "***< %s[%d] (tag %s) %s at %lx. >***",
            action, pools[poolnum].pool_size, tag, reason, (long)ph);
    }
}

static void pool_vfy(int poolnum, const char *tag)
{
    POOLHDR *ph, *lastph;
    POOLFTR *pf;
    char *h;
    int psize;

    lastph = NULL;
    psize = pools[poolnum].pool_size;
    for (ph = pools[poolnum].chain_head; ph; lastph = ph, ph = ph->next)
    {
        h = (char *)ph;
        h += sizeof(POOLHDR);
        h += pools[poolnum].pool_size;
        pf = (POOLFTR *) h;

        if (ph->magicnum != POOL_MAGICNUM)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Verify",
                     "header corrupted (clearing freelist)");

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
            return; // not safe to continue
        }
        if (pf->magicnum != POOL_MAGICNUM)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
                 "Verify", "footer corrupted");
            pf->magicnum = POOL_MAGICNUM;
        }
        if (ph->pool_size != psize) {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph,
                 "Verify", "header has incorrect size");
        }
    }
}

void pool_check(const char *tag)
{
    pool_vfy(POOL_LBUF, tag);
    pool_vfy(POOL_MBUF, tag);
    pool_vfy(POOL_SBUF, tag);
    pool_vfy(POOL_BOOL, tag);
    pool_vfy(POOL_DESC, tag);
    pool_vfy(POOL_QENTRY, tag);
}

char *pool_alloc(int poolnum, const char *tag)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag);
    }

    char *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[poolnum].free_head;
    if (  ph
       && ph->magicnum == POOL_MAGICNUM)
    {
        p = (char *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_size);
        pools[poolnum].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != POOL_MAGICNUM)
        {
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Alloc",
                "corrupted buffer footer");
            pf->magicnum = POOL_MAGICNUM;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err("BUG", LOG_ALWAYS, poolnum, tag, ph, "Alloc",
                "corrupted buffer header");

            // Start a new free list and record stats.
            //
            pools[poolnum].free_head = NULL;
            pools[poolnum].num_lost += (pools[poolnum].tot_alloc
                                     -  pools[poolnum].num_alloc);
            pools[poolnum].tot_alloc = pools[poolnum].num_alloc;
        }

        ph = (POOLHDR *)MEMALLOC(pools[poolnum].pool_size + sizeof(POOLHDR)
           + sizeof(POOLFTR));
        ISOUTOFMEMORY(ph);
        p = (char *)(ph + 1);
        pf = (POOLFTR *)(p + pools[poolnum].pool_size);

        // Initialize.
        //
        ph->next = pools[poolnum].chain_head;
        ph->nxtfree = NULL;
        ph->magicnum = POOL_MAGICNUM;
        ph->pool_size = pools[poolnum].pool_size;
        pf->magicnum = POOL_MAGICNUM;
        *((unsigned int *)p) = POOL_MAGICNUM;
        pools[poolnum].chain_head = ph;
        pools[poolnum].max_alloc++;
    }

    ph->buf_tag = (char *)tag;
    pools[poolnum].tot_alloc++;
    pools[poolnum].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options) != 0
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) buffer at %lx. (%s)",
            pools[poolnum].pool_size, tag, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != POOL_MAGICNUM)
    {
        pool_err("BUG", LOG_PROBLEMS, poolnum, tag, ph, "Alloc",
            "buffer modified after free");
    }
    *pui = 0;
    return p;
}

char *pool_alloc_lbuf(const char *tag)
{
    if (mudconf.paranoid_alloc)
    {
        pool_check(tag);
    }

    char *p;
    POOLFTR *pf;
    POOLHDR *ph = (POOLHDR *)pools[POOL_LBUF].free_head;
    if (  ph
       && ph->magicnum == POOL_MAGICNUM)
    {
        p = (char *)(ph + 1);
        pf = (POOLFTR *)(p + LBUF_SIZE);
        pools[POOL_LBUF].free_head = ph->nxtfree;

        // Check for corrupted footer, just report and fix it.
        //
        if (pf->magicnum != POOL_MAGICNUM)
        {
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, "Alloc",
                "corrupted buffer footer");
            pf->magicnum = POOL_MAGICNUM;
        }
    }
    else
    {
        if (ph)
        {
            // Header is corrupt. Throw away the freelist and start a new
            // one.
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, tag, ph, "Alloc",
                "corrupted buffer header");

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
        ph->magicnum = POOL_MAGICNUM;
        ph->pool_size = LBUF_SIZE;
        pf->magicnum = POOL_MAGICNUM;
        *((unsigned int *)p) = POOL_MAGICNUM;
        pools[POOL_LBUF].chain_head = ph;
        pools[POOL_LBUF].max_alloc++;
    }

    ph->buf_tag = (char *)tag;
    pools[POOL_LBUF].tot_alloc++;
    pools[POOL_LBUF].num_alloc++;

    if (  (LOG_ALLOCATE & mudconf.log_options) != 0
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Alloc[%d] (tag %s) buffer at %lx. (%s)",
            LBUF_SIZE, tag, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // If the buffer was modified after it was last freed, log it.
    //
    unsigned int *pui = (unsigned int *)p;
    if (*pui != POOL_MAGICNUM)
    {
        pool_err("BUG", LOG_PROBLEMS, POOL_LBUF, tag, ph, "Alloc",
            "buffer modified after free");
    }
    *pui = 0;
    return p;
}

void pool_free(int poolnum, char *buf)
{
    POOLHDR *ph = ((POOLHDR *)(buf)) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + pools[poolnum].pool_size);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
    {
        pool_check(ph->buf_tag);
    }

    // Make sure the buffer header is good.  If it isn't, log the error and
    // throw away the buffer.
    //
    if (ph->magicnum != POOL_MAGICNUM)
    {
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
                 "corrupted buffer header");
        pools[poolnum].num_lost++;
        pools[poolnum].num_alloc--;
        pools[poolnum].tot_alloc--;
        return;
    }

    // Verify the buffer footer.  Don't unlink if damaged, just repair.
    //
    if (pf->magicnum != POOL_MAGICNUM)
    {
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
             "corrupted buffer footer");
        pf->magicnum = POOL_MAGICNUM;
    }

    // Verify that we are not trying to free someone else's buffer.
    //
    if (ph->pool_size != pools[poolnum].pool_size)
    {
        pool_err("BUG", LOG_ALWAYS, poolnum, ph->buf_tag, ph, "Free",
                 "Attempt to free into a different pool.");
        return;
    }

    if (  (LOG_ALLOCATE & mudconf.log_options) != 0
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) buffer at %lx. (%s)",
            pools[poolnum].pool_size, ph->buf_tag, (long)ph,
            mudstate.debug_cmd);
        end_log();
    }

    // Make sure we aren't freeing an already free buffer.  If we are, log an
    // error, otherwise update the pool header and stats.
    //
    if (*pui == POOL_MAGICNUM)
    {
        pool_err("BUG", LOG_BUGS, poolnum, ph->buf_tag, ph, "Free",
                 "buffer already freed");
    }
    else
    {
        *pui = POOL_MAGICNUM;
        ph->nxtfree = pools[poolnum].free_head;
        pools[poolnum].free_head = ph;
        pools[poolnum].num_alloc--;
    }
}

void pool_free_lbuf(char *buf)
{
    POOLHDR *ph = ((POOLHDR *)(buf)) - 1;
    POOLFTR *pf = (POOLFTR *)(buf + LBUF_SIZE);
    unsigned int *pui = (unsigned int *)buf;

    if (mudconf.paranoid_alloc)
    {
        pool_check(ph->buf_tag);
    }

    if (  ph->magicnum != POOL_MAGICNUM
       || pf->magicnum != POOL_MAGICNUM
       || ph->pool_size != LBUF_SIZE
       || *pui == POOL_MAGICNUM)
    {
        if (ph->magicnum != POOL_MAGICNUM)
        {
            // The buffer header is damaged. Log the error and throw away the
            // buffer.
            //
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                     "corrupted buffer header");
            pools[POOL_LBUF].num_lost++;
            pools[POOL_LBUF].num_alloc--;
            pools[POOL_LBUF].tot_alloc--;
            return;
        }
        else if (pf->magicnum != POOL_MAGICNUM)
        {
            // The buffer footer is damaged.  Don't unlink, just repair.
            //
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                "corrupted buffer footer");
            pf->magicnum = POOL_MAGICNUM;
        }
        else if (ph->pool_size != LBUF_SIZE)
        {
            // We are trying to free someone else's buffer.
            //
            pool_err("BUG", LOG_ALWAYS, POOL_LBUF, ph->buf_tag, ph, "Free",
                "Attempt to free into a different pool.");
            return;
        }

        // If we are freeing a buffer that was already free, report an error.
        //
        if (*pui == POOL_MAGICNUM)
        {
            pool_err("BUG", LOG_BUGS, POOL_LBUF, ph->buf_tag, ph, "Free",
                     "buffer already freed");
            return;
        }
    }

    if (  (LOG_ALLOCATE & mudconf.log_options) != 0
       && mudstate.logging == 0
       && start_log("DBG", "ALLOC"))
    {
        Log.tinyprintf("Free[%d] (tag %s) buffer at %lx. (%s)",
            LBUF_SIZE, ph->buf_tag, (long)ph, mudstate.debug_cmd);
        end_log();
    }

    // Update the pool header and stats.
    //
    *pui = POOL_MAGICNUM;
    ph->nxtfree = pools[POOL_LBUF].free_head;
    pools[POOL_LBUF].free_head = ph;
    pools[POOL_LBUF].num_alloc--;
}

static char *pool_stats(int poolnum, const char *text)
{
    char *buf;

    buf = alloc_mbuf("pool_stats");
    sprintf(buf, "%-15s %5d%9d%9d%9d%9d", text, pools[poolnum].pool_size,
        pools[poolnum].num_alloc, pools[poolnum].max_alloc,
        pools[poolnum].tot_alloc, pools[poolnum].num_lost);
    return buf;
}

static void pool_trace(dbref player, int poolnum, const char *text)
{
    POOLHDR *ph;
    int numfree;
    unsigned int *ibuf;
    char *h;

    numfree = 0;
    notify(player, tprintf("----- %s -----", text));
    for (ph = pools[poolnum].chain_head; ph != NULL; ph = ph->next)
    {
        if (ph->magicnum != POOL_MAGICNUM)
        {
            notify(player, "*** CORRUPTED BUFFER HEADER, ABORTING SCAN ***");
            notify(player, tprintf("%d free %s (before corruption)",
                       numfree, text));
            return;
        }
        h = (char *)ph;
        h += sizeof(POOLHDR);
        ibuf = (unsigned int *)h;
        if (*ibuf != POOL_MAGICNUM)
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

static void list_bufstat(dbref player, int poolnum)
{
    char *buff = pool_stats(poolnum, poolnames[poolnum]);
    notify(player, buff);
    free_mbuf(buff);
}

void list_bufstats(dbref player)
{
    int i;

    notify(player, "Buffer Stats     Size    InUse    Total   Allocs     Lost");
    for (i = 0; i < NUM_POOLS; i++)
    {
        list_bufstat(player, i);
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
    POOLHDR *ph, *phnext, *newchain;
    int i;
    unsigned int *ibuf;
    char *h;

    for (i = 0; i < NUM_POOLS; i++)
    {
        newchain = NULL;
        for (ph = pools[i].chain_head; ph != NULL; ph = phnext)
        {
            h = (char *)ph;
            phnext = ph->next;
            h += sizeof(POOLHDR);
            ibuf = (unsigned int *)h;
            if (*ibuf == POOL_MAGICNUM)
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

