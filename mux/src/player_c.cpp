// player_c.cpp -- Player cache routines.
//
// $Id: player_c.cpp,v 1.7 2005-01-04 20:54:07 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"

typedef struct player_cache {
    dbref player;
    int money;
    int queue;
    int qmax;
    int cflags;
    struct player_cache *next;
} PCACHE;

CHashTable pcache_htab;
PCACHE *pcache_head;

#define PF_DEAD     0x0001
#define PF_REF      0x0002
#define PF_MONEY_CH 0x0004
#define PF_QMAX_CH  0x0008

void pcache_init(void)
{
    pool_init(POOL_PCACHE, sizeof(PCACHE));
    pcache_head = NULL;
}

static void pcache_reload1(dbref player, PCACHE *pp)
{
    const char *cp = atr_get_raw(player, A_MONEY);
    if (cp && *cp)
        pp->money = mux_atol(cp);
    else
        pp->money = 0;

    cp = atr_get_raw(player, A_QUEUEMAX);
    if (cp && *cp)
        pp->qmax = mux_atol(cp);
    else if (!Wizard(player))
        pp->qmax = mudconf.queuemax;
    else
        pp->qmax = -1;
}


PCACHE *pcache_find(dbref player)
{
    if (!Good_obj(player) || !OwnsOthers(player))
    {
        return NULL;
    }
    PCACHE *pp = (PCACHE *)hashfindLEN(&player, sizeof(player), &pcache_htab);
    if (pp)
    {
        pp->cflags |= PF_REF;
        return pp;
    }
    pp = alloc_pcache("pcache_find");
    pp->queue = 0;
    pp->cflags = PF_REF;
    pp->player = player;
    pcache_reload1(player, pp);
    pp->next = pcache_head;
    pcache_head = pp;
    hashaddLEN(&player, sizeof(player), pp, &pcache_htab);
    return pp;
}

void pcache_reload(dbref player)
{
    PCACHE *pp = pcache_find(player);
    if (!pp)
    {
        return;
    }
    pcache_reload1(player, pp);
}

static void pcache_save(PCACHE *pp)
{
    IBUF tbuf;

    if (pp->cflags & PF_DEAD)
    {
        return;
    }
    if (pp->cflags & PF_MONEY_CH)
    {
        mux_ltoa(pp->money, tbuf);
        atr_add_raw(pp->player, A_MONEY, tbuf);
    }
    if (pp->cflags & PF_QMAX_CH)
    {
        mux_ltoa(pp->qmax, tbuf);
        atr_add_raw(pp->player, A_QUEUEMAX, tbuf);
    }
    pp->cflags &= ~(PF_MONEY_CH | PF_QMAX_CH);
}

void pcache_trim(void)
{
    PCACHE *pp = pcache_head;
    PCACHE *pplast = NULL;
    while (pp)
    {
        if (  !(pp->cflags & PF_DEAD)
           && (  pp->queue
              || (pp->cflags & PF_REF)))
        {
            pp->cflags &= ~PF_REF;
            pplast = pp;
            pp = pp->next;
        }
        else
        {
            PCACHE *ppnext = pp->next;
            if (pplast)
            {
                pplast->next = ppnext;
            }
            else
            {
                pcache_head = ppnext;
            }
            if (!(pp->cflags & PF_DEAD))
            {
                pcache_save(pp);
                hashdeleteLEN(&(pp->player), sizeof(pp->player), &pcache_htab);
            }
            free_pcache(pp);
            pp = ppnext;
        }
    }
}

void pcache_sync(void)
{
    PCACHE *pp = pcache_head;
    while (pp)
    {
        pcache_save(pp);
        pp = pp->next;
    }
}

int a_Queue(dbref player, int adj)
{
    if (OwnsOthers(player))
    {
        PCACHE *pp = pcache_find(player);
        if (pp)
        {
            pp->queue += adj;
            return pp->queue;
        }
    }
    return 0;
}

int QueueMax(dbref player)
{
    int m = 0;
    if (OwnsOthers(player))
    {
        PCACHE *pp = pcache_find(player);
        if (pp)
        {
            if (pp->qmax >= 0)
            {
                m = pp->qmax;
            }
            else
            {
                m = mudstate.db_top + 1;
                if (m < mudconf.queuemax)
                {
                    m = mudconf.queuemax;
                }
            }
        }
    }
    return m;
}

int Pennies(dbref obj)
{
    if (  !mudstate.bStandAlone
       && OwnsOthers(obj))
    {
        PCACHE *pp = pcache_find(obj);
        if (pp)
        {
            return pp->money;
        }
    }
    const char *cp = atr_get_raw(obj, A_MONEY);
    if (cp)
    {
        return mux_atol(cp);
    }
    return 0;
}

void s_Pennies(dbref obj, int howfew)
{
    if (mudstate.bStandAlone)
    {
        IBUF tbuf;
        mux_ltoa(howfew, tbuf);
        atr_add_raw(obj, A_MONEY, tbuf);
    }
    else if (OwnsOthers(obj))
    {
        PCACHE *pp = pcache_find(obj);
        if (pp)
        {
            pp->money = howfew;
            pp->cflags |= PF_MONEY_CH;
        }
    }
}
