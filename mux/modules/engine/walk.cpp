/*! \file walk.cpp
 * \brief NPC movement primitives: @walk and @patrol.
 *
 * @walk <npc>=<destination> -- moves an NPC one hop per second along the
 * routed path until it reaches the destination.
 *
 * @patrol <npc>=<room1> <room2> ... -- continuously loops the NPC
 * between waypoints.
 *
 * Both commands use the routing tables built by routing.cpp and schedule
 * movement via the system scheduler (DeferTask).
 *
 * See docs/design-routing.md Phase 4.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "flags.h"
#include "routing.h"
#include "walk.h"

// ---------------------------------------------------------------------------
// Walk entry: tracks one active @walk or @patrol.
// ---------------------------------------------------------------------------

struct WALKTAB
{
    dbref   npc;                // Object being moved.
    dbref   executor;           // Who issued the command (for notifications).
    dbref   destination;        // Final destination (@walk) or NOTHING (@patrol).
    std::vector<dbref> waypoints; // Waypoint rooms (@patrol).
    int     current_wp;         // Current waypoint index (@patrol).
    int     options;            // ROUTE_OPT flags.
    bool    quiet;              // Suppress arrival/departure messages.
    WALKTAB *next;
};

static WALKTAB *walk_head = nullptr;

// Default interval between hops (1 second).
//
static const int WALK_INTERVAL_SECS = 1;

// ---------------------------------------------------------------------------
// Forward declarations.
// ---------------------------------------------------------------------------

static void dispatch_WalkEntry(void *pArg, int iUnused);
static void walk_schedule(WALKTAB *wp);
static void walk_remove(WALKTAB *wp);
static bool walk_next_hop_blocked(const WALKTAB *wp, dbref src, dbref target);

// ---------------------------------------------------------------------------
// Initialization / shutdown.
// ---------------------------------------------------------------------------

void walk_init(void)
{
    walk_head = nullptr;
}

void walk_shutdown(void)
{
    while (walk_head)
    {
        walk_remove(walk_head);
    }
}

void walk_clr(dbref npc)
{
    WALKTAB **pp = &walk_head;
    while (*pp)
    {
        if ((*pp)->npc == npc)
        {
            WALKTAB *wp = *pp;
            scheduler.CancelTask(dispatch_WalkEntry, wp, 0);
            *pp = wp->next;
            delete wp;
        }
        else
        {
            pp = &(*pp)->next;
        }
    }
}

// ---------------------------------------------------------------------------
// Schedule the next hop.
// ---------------------------------------------------------------------------

static void walk_schedule(WALKTAB *wp)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();

    CLinearTimeDelta ltd;
    ltd.SetSeconds(WALK_INTERVAL_SECS);

    CLinearTimeAbsolute ltaNext = ltaNow + ltd;
    scheduler.DeferTask(ltaNext, PRIORITY_SYSTEM,
                        dispatch_WalkEntry, wp, 0);
}

// ---------------------------------------------------------------------------
// Remove a walk entry.
// ---------------------------------------------------------------------------

static void walk_remove(WALKTAB *wp)
{
    scheduler.CancelTask(dispatch_WalkEntry, wp, 0);

    WALKTAB **pp = &walk_head;
    while (*pp)
    {
        if (*pp == wp)
        {
            *pp = wp->next;
            break;
        }
        pp = &(*pp)->next;
    }
    delete wp;
}

// Distinguish "no route exists" from "a route exists, but the locked
// next hop is currently impassable for this mover".
//
static bool walk_next_hop_blocked(const WALKTAB *wp, dbref src, dbref target)
{
    if (!(wp->options & ROUTE_OPT_LOCKED))
    {
        return false;
    }

    return route_next_exit(wp->npc, src, target, 0) != NOTHING;
}

// ---------------------------------------------------------------------------
// Dispatch callback: move the NPC one hop.
// ---------------------------------------------------------------------------

static void dispatch_WalkEntry(void *pArg, int iUnused)
{
    UNUSED_PARAMETER(iUnused);
    WALKTAB *wp = static_cast<WALKTAB *>(pArg);

    // Safety checks.
    //
    if (  !Good_obj(wp->npc)
       || Going(wp->npc)
       || Halted(wp->npc))
    {
        walk_remove(wp);
        return;
    }

    dbref src = Location(wp->npc);
    if (!Good_obj(src) || !isRoom(src))
    {
        walk_remove(wp);
        return;
    }

    // Determine current target.
    //
    dbref target;
    if (wp->destination != NOTHING)
    {
        // @walk mode.
        target = wp->destination;
    }
    else
    {
        // @patrol mode.
        if (wp->waypoints.empty())
        {
            walk_remove(wp);
            return;
        }
        target = wp->waypoints[wp->current_wp];
    }

    // Already at the target?
    //
    if (src == target)
    {
        if (wp->destination != NOTHING)
        {
            // @walk: arrived at destination.
            //
            if (!wp->quiet && Good_obj(wp->executor))
            {
                notify(wp->executor,
                    tprintf(T("%s has arrived at its destination."),
                            Moniker(wp->npc)));
            }
            walk_remove(wp);
            return;
        }
        else
        {
            // @patrol: advance to next waypoint.
            //
            wp->current_wp = (wp->current_wp + 1)
                           % static_cast<int>(wp->waypoints.size());
            target = wp->waypoints[wp->current_wp];

            if (src == target)
            {
                // All waypoints are the same room -- nothing to do.
                walk_schedule(wp);
                return;
            }
        }
    }

    // Get the next hop.
    //
    dbref next_exit = route_next_exit(wp->npc, src, target, wp->options);
    if (next_exit == NOTHING)
    {
        bool blocked = walk_next_hop_blocked(wp, src, target);

        if (!wp->quiet && Good_obj(wp->executor))
        {
            if (blocked)
            {
                notify(wp->executor,
                    tprintf(T("%s was blocked at an exit."),
                            Moniker(wp->npc)));
            }
            else
            {
                notify(wp->executor,
                    tprintf(T("%s can\xE2\x80\x99t find a route from here."),
                            Moniker(wp->npc)));
            }
        }

        if (wp->destination != NOTHING)
        {
            // @walk: stop on either no route or a blocked next hop.
            walk_remove(wp);
        }
        else
        {
            // @patrol: a blocked exit may clear later, so retry the same
            // waypoint. Only skip when no route exists at all.
            if (!blocked)
            {
                wp->current_wp = (wp->current_wp + 1)
                               % static_cast<int>(wp->waypoints.size());
            }
            walk_schedule(wp);
        }
        return;
    }

    // Move the NPC through the exit.
    //
    int hush = wp->quiet ? HUSH_EXIT : 0;
    move_exit(wp->npc, next_exit, false, T("Blocked."), hush);

    // Verify movement actually happened.
    //
    dbref new_loc = Location(wp->npc);
    if (new_loc == src)
    {
        // Movement failed (locked exit, etc.)
        if (!wp->quiet && Good_obj(wp->executor))
        {
            notify(wp->executor,
                tprintf(T("%s was blocked at an exit."),
                        Moniker(wp->npc)));
        }

        if (wp->destination != NOTHING)
        {
            walk_remove(wp);
        }
        else
        {
            // @patrol: try again next tick (maybe lock state changes).
            walk_schedule(wp);
        }
        return;
    }

    // Check if we arrived at the final destination (@walk mode).
    //
    if (wp->destination != NOTHING && new_loc == wp->destination)
    {
        if (!wp->quiet && Good_obj(wp->executor))
        {
            notify(wp->executor,
                tprintf(T("%s has arrived at its destination."),
                        Moniker(wp->npc)));
        }
        walk_remove(wp);
        return;
    }

    // Reschedule for next tick.
    //
    walk_schedule(wp);
}

// ---------------------------------------------------------------------------
// Command handlers.
// ---------------------------------------------------------------------------

void do_walk(dbref executor, dbref caller, dbref enactor, int eval,
             int key, int nargs, UTF8 *what, UTF8 *where,
             const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Find the NPC.
    //
    init_match(executor, what, NOTYPE);
    match_everything(0);
    dbref npc = noisy_match_result();
    if (!Good_obj(npc))
    {
        return;
    }

    // Must control the NPC.
    //
    if (!Controls(executor, npc))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // @walk/stop: cancel any active walk/patrol.
    //
    if (key & WALK_STOP)
    {
        walk_clr(npc);
        notify_quiet(executor, T("Walk cancelled."));
        return;
    }

    // Validate the NPC is a thing.
    //
    if (!isThing(npc))
    {
        notify_quiet(executor, T("Only things can walk."));
        return;
    }

    // Find the destination.
    //
    if (!where || !*where)
    {
        notify_quiet(executor, T("Walk where?"));
        return;
    }

    init_match(executor, where, TYPE_ROOM);
    match_everything(0);
    dbref dest = noisy_match_result();
    if (!Good_obj(dest) || !isRoom(dest))
    {
        notify_quiet(executor, T("That\xE2\x80\x99s not a valid destination."));
        return;
    }

    // Cancel any existing walk/patrol for this NPC.
    //
    walk_clr(npc);

    // Set up the walk entry.
    //
    WALKTAB *wp = new WALKTAB();
    wp->npc = npc;
    wp->executor = executor;
    wp->destination = dest;
    wp->current_wp = 0;
    wp->options = (key & WALK_LOCKED) ? ROUTE_OPT_LOCKED : 0;
    wp->quiet = (key & WALK_QUIET) ? true : false;
    wp->next = walk_head;
    walk_head = wp;

    if (!wp->quiet)
    {
        notify(executor,
            tprintf(T("%s begins walking toward #%d."),
                    Moniker(npc), dest));
    }

    // Schedule the first hop immediately (next tick).
    //
    walk_schedule(wp);
}

void do_patrol(dbref executor, dbref caller, dbref enactor, int eval,
               int key, int nargs, UTF8 *what, UTF8 *waypoint_str,
               const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Find the NPC.
    //
    init_match(executor, what, NOTYPE);
    match_everything(0);
    dbref npc = noisy_match_result();
    if (!Good_obj(npc))
    {
        return;
    }

    if (!Controls(executor, npc))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // @patrol/stop: cancel.
    //
    if (key & WALK_STOP)
    {
        walk_clr(npc);
        notify_quiet(executor, T("Patrol cancelled."));
        return;
    }

    if (!isThing(npc))
    {
        notify_quiet(executor, T("Only things can patrol."));
        return;
    }

    if (!waypoint_str || !*waypoint_str)
    {
        notify_quiet(executor, T("Patrol where?"));
        return;
    }

    // Parse space-separated list of room dbrefs.
    //
    std::vector<dbref> waypoints;
    LBuf wp_copy = LBuf_Src("do_patrol");
    mux_strncpy(wp_copy, waypoint_str, LBUF_SIZE - 1);

    UTF8 *opts = trim_space_sep(wp_copy, sepSpace);
    while (opts && *opts)
    {
        UTF8 *token = split_token(&opts, sepSpace);
        init_match(executor, token, TYPE_ROOM);
        match_everything(0);
        dbref room = match_result();
        if (!Good_obj(room) || !isRoom(room))
        {
            notify(executor,
                tprintf(T("\xE2\x80\x9C%s\xE2\x80\x9D is not a valid room."),
                        token));
            return;
        }
        waypoints.push_back(room);
    }

    if (waypoints.size() < 2)
    {
        notify_quiet(executor, T("Patrol requires at least two waypoints."));
        return;
    }

    // Cancel any existing walk/patrol for this NPC.
    //
    walk_clr(npc);

    // Set up the patrol entry.
    //
    WALKTAB *wp = new WALKTAB();
    wp->npc = npc;
    wp->executor = executor;
    wp->destination = NOTHING;  // Signals patrol mode.
    wp->waypoints = waypoints;
    wp->current_wp = 0;
    wp->options = (key & WALK_LOCKED) ? ROUTE_OPT_LOCKED : 0;
    wp->quiet = (key & WALK_QUIET) ? true : false;
    wp->next = walk_head;
    walk_head = wp;

    if (!wp->quiet)
    {
        notify(executor,
            tprintf(T("%s begins patrolling %d waypoints."),
                    Moniker(npc), static_cast<int>(waypoints.size())));
    }

    walk_schedule(wp);
}
