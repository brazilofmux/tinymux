/*! \file player_c.cpp
 * Player cache routines.
 *
 * $Id: player_c.cpp,v 1.13 2005-10-30 06:09:35 sdennis Exp $
 *
 * Frequenty-used items which appear on every object generally find a home in
 * the db[] structure managed in db.cpp. However, there are a few items
 * related only to players which are still accessed frequently enough that
 * they should still be cached. These items are money, count queued commands, 
 * and the limit of queued commands.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"

/*! \brief structure to hold cached data for player-type objects.
 */

typedef struct player_cache
{
    dbref player;
    int   money;
    int   queue;
    int   qmax;
    int   cflags;
    struct player_cache *next;
} PCACHE;

/*! \brief Hash Table which maps player dbref to PCACHE entry.
 */
CHashTable pcache_htab;

/*! \brief The head of a singly-linked list of all PCACHE entries.
 */
PCACHE *pcache_head;

#define PF_REF      0x0002
#define PF_MONEY_CH 0x0004

/*! \brief Initializes the player cache.
 *
 * This is called once to initialize the player cache and supporting
 * data structures:  Player cache structures are pooled, the Hash Table
 * initializes itself, and the singley-linked list is started.
 *
 * \return         None.
 */

void pcache_init(void)
{
    pool_init(POOL_PCACHE, sizeof(PCACHE));
    pcache_head = NULL;
}

/*! \brief Updates player cache items from the database.
 *
 * The Money and QueueMax attributes are used to initialize the corresponding
 * items in the player cache.  If a Money attribute does not exist for some
 * strange reason, it it initialized to zero and marked as dirty. If a
 * QueueMax attribute doesn't exist or is negative, then the game will
 * choose a reasonable limit later.
 *
 * \param player   player object to begin caching.
 * \param pp       pointer to PCACHE structure.
 * \return         None.
 */

static void pcache_reload1(dbref player, PCACHE *pp)
{
    const char *cp = atr_get_raw(player, A_MONEY);
    if (cp && *cp)
    {
        pp->money = mux_atol(cp);
    }
    else
    {
        pp->cflags |= PF_MONEY_CH;
        pp->money = 0;
    }

    int m = -1;
    cp = atr_get_raw(player, A_QUEUEMAX);
    if (cp && *cp)
    {
        m = mux_atol(cp);
        if (m < 0)
        {
            m = -1;
        }
    }
    pp->qmax = m;
}

/*! \brief Returns a player's cache record.
 *
 * Whether created from scratch or found in the cache, pcache_find() always
 * returns a valid player cache record for the requested player object dbref.
 * This function uses Hash Table access primarily, but it maintains the
 * single-linked list as well.
 *
 * \param player   player object dbref.
 * \return         Pointer to new or existing player cache record.
 */

static PCACHE *pcache_find(dbref player)
{
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

/*! \brief Saves any dirty player data items to the database.
 *
 * \param pp       pointer to potentially dirty PCACHE structure.
 * \return         None.
 */

static void pcache_save(PCACHE *pp)
{
    if (pp->cflags & PF_MONEY_CH)
    {
        IBUF tbuf;
        mux_ltoa(pp->money, tbuf);
        atr_add_raw(pp->player, A_MONEY, tbuf);
        pp->cflags &= ~PF_MONEY_CH;
    }
}

/*! \brief Re-initializes Money and QueueMax items from the database.
 *
 * Whether created from scratch or found in the cache, pcache_find() always
 * returns a valid player cache record for the requested player object dbref.
 * This function uses Hash Table access primarily, but it maintains the
 * single-linked list as well.
 *
 * \param player   player object dbref.
 * \return         None.
 */

void pcache_reload(dbref player)
{
    if (  Good_obj(player)
       && OwnsOthers(player))
    {
        PCACHE *pp = pcache_find(player);
        pcache_save(pp);
        pcache_reload1(player, pp);
    }
}

/*! \brief Ages and trims the player cache of stale entries.
 *
 * pcache_trim() relies primarily on the singly-linked list, but it also
 * maintains the Hash Table. To be trimmed, a player cache record must
 * not have outstanding commands in the command queue.
 *
 * The one level of aging is accomplished with PR_REF.  On the first pass
 * through the linked list, the PR_REF bit is removed. On the second pass
 * through the list, the record is trimmed.
 *
 * \return         None.
 */

void pcache_trim(void)
{
    PCACHE *pp = pcache_head;
    PCACHE *pplast = NULL;
    while (pp)
    {
        PCACHE *ppnext = pp->next;
        if (  pp->queue
           || (pp->cflags & PF_REF))
        {
            // This entry either has outstanding commands in the queue or we
            // need to let it age.
            //
            pp->cflags &= ~PF_REF;
            pplast = pp;
        }
        else
        {
            // Unlink and destroy this entry.
            //
            if (pplast)
            {
                pplast->next = ppnext;
            }
            else
            {
                pcache_head = ppnext;
            }

            pcache_save(pp);
            hashdeleteLEN(&(pp->player), sizeof(pp->player), &pcache_htab);
            free_pcache(pp);
        }
        pp = ppnext;
    }
}

/*! \brief Flushes any dirty player items to the database.
 *
 * The primary access is via the singly-linked list. Upon return, all the
 * player cache records are clean.
 *
 * \return         None.
 */

void pcache_sync(void)
{
    PCACHE *pp = pcache_head;
    while (pp)
    {
        pcache_save(pp);
        pp = pp->next;
    }
}

/*! \brief Adjusts the count of queued commands up or down.
 *
 * cque.cpp uses this as it schedules and performs queued commands.
 *
 * \param player   dbref of player object responsible for command.
 * \param adj      new (+) or completed (-) commands being queued.
 * \return         None.
 */

int a_Queue(dbref player, int adj)
{
    if (  Good_obj(player)
       && OwnsOthers(player))
    {
        PCACHE *pp = pcache_find(player);
        pp->queue += adj;
        return pp->queue;
    }
    return 0;
}

/*! \brief Returns the player's upper limit of queued commands.
 *
 * If a QueueMax is set on the player, we use that. Otherwise, there is
 * a configurable game-wide limit (given by player_queue_limit) unless the
 * player is a Wizard in which case, we reason that well behaved Wizard code
 * should be able to schedule as much work as there are objects in the
 * database -- larger game, more work to be expected in the queue.
 *
 * \param player   dbref of player object.
 * \return         None.
 */

int QueueMax(dbref player)
{
    int m = 0;
    if (  Good_obj(player)
       && OwnsOthers(player))
    {
        PCACHE *pp = pcache_find(player);
        if (pp->qmax >= 0)
        {
            m = pp->qmax;
        }
        else
        {
            // @queuemax was not valid so we use the game-wide limit.
            //
            m = mudconf.queuemax;
            if (  Wizard(player)
               && m < mudstate.db_top + 1)
            {
                m = mudstate.db_top + 1;
            }
        }
    }
    return m;
}

/*! \brief Returns how many coins are in a player's purse.
 *
 * \param player   dbref of player object.
 * \return         None.
 */

int Pennies(dbref obj)
{
    if (mudstate.bStandAlone)
    {
        const char *cp = atr_get_raw(obj, A_MONEY);
        if (cp)
        {
            return mux_atol(cp);
        }
    }
    else if (  Good_obj(obj)
            && OwnsOthers(obj))
    {
        PCACHE *pp = pcache_find(obj);
        return pp->money;
    }
    return 0;
}

/*! \brief Sets the number of coins in a player's purse.
 *
 * This changes the number of coins a player holds and sets this attribute
 * as dirty so that it will be updated in the attribute database later.
 *
 * \param player   dbref of player object responsible for command.
 * \param howfew   Number of coins
 * \return         None.
 */

void s_Pennies(dbref obj, int howfew)
{
    if (mudstate.bStandAlone)
    {
        IBUF tbuf;
        mux_ltoa(howfew, tbuf);
        atr_add_raw(obj, A_MONEY, tbuf);
    }
    else if (  Good_obj(obj)
            && OwnsOthers(obj))
    {
        PCACHE *pp = pcache_find(obj);
        pp->money = howfew;
        pp->cflags |= PF_MONEY_CH;
    }
}
