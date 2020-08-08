/*! \file move.cpp
 * \brief Routines for moving about.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "mathutil.h"
#include "powers.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

/* ---------------------------------------------------------------------------
 * process_leave_loc: Generate messages and actions resulting from leaving a
 * place.
 */

static void process_leave_loc(dbref thing, dbref dest, dbref cause, bool canhear, int hush)
{
    dbref loc = Location(thing);
    if ((loc == NOTHING) || (loc == dest))
    {
        return;
    }

    if (dest == HOME)
    {
        dest = Home(thing);
    }

    if (Html(thing))
    {
        notify_html(thing, T("<xch_page clear=links>"));
    }

    // Run the LEAVE attributes in the current room if we meet any of
    // following criteria:
    //
    //   - The current room has wizard privs.
    //   - Neither the current room nor the moving object are dark.
    //   - The moving object can hear and does not hav wizard privs.
    //
    // EXCEPT if we were called with the HUSH_LEAVE key.
    //

#ifdef REALITY_LVLS
    bool quiet = (  (hush & HUSH_LEAVE) || !IsReal(loc, thing)
#else
    bool quiet = (  (hush & HUSH_LEAVE)
#endif // REALITY_LVLS
                 || (  !Wizard(loc)
                    && (  Dark(thing)
                       || Dark(loc))
                    && (  !canhear
                       || (  Wizard(thing)
                          && Dark(thing)))));

    int oattr = quiet ? 0 : A_OLEAVE;
    int aattr = quiet ? 0 : A_ALEAVE;
    int pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_LEAVE;
    did_it(thing, loc, pattr, nullptr, oattr, nullptr, aattr, 0, nullptr, 0);

    // Do OXENTER for receiving room
    //
    if ((dest != NOTHING) && !quiet)
    {
        did_it(thing, dest, 0, nullptr, A_OXENTER, nullptr, 0, 0, nullptr, 0);
    }

    // Display the 'has left' message if we meet any of the following
    // criteria:
    //
    //   - Neither the current room nor the moving object are dark.
    //   - The object can hear and is not a dark wizard.
    //
    if (  !quiet
       && !Blind(thing)
       && !Blind(loc))
    {
        if (  (  !Dark(thing)
              && !Dark(loc))
           || (  canhear
#if defined(FIRANMUX)
              && !Dark(thing)))
#else
              && !(Wizard(thing) && Dark(thing))))
#endif // FIRANMUX
        {
#ifdef REALITY_LVLS
            notify_except2_rlevel(loc, thing, thing, cause,
#else
            notify_except2(loc, thing, thing, cause,
#endif // REALITY_LVLS
                       tprintf(T("%s has left."), Moniker(thing)));
        }
    }
}

/*---------------------------------------------------------------------------
 * process_enter_loc: Generate messages and actions resulting from entering
 * a place.
 */

static void process_enter_loc(dbref thing, dbref src, dbref cause, bool canhear, int hush)
{
    dbref loc = Location(thing);
    if (  loc == NOTHING
       || loc == src)
    {
        return;
    }

    show_vrml_url(thing, loc);

    // Run the ENTER attributes in the current room if we meet any of following
    // criteria:
    //
    //  - The current room has wizard privs.
    //  - Neither the current room nor the moving object are dark.
    //  - The moving object can hear and does not have wizard privs.
    //
    // EXCEPT if we were called with the HUSH_ENTER key.
    //
#ifdef REALITY_LVLS
    bool quiet = (  (hush & HUSH_ENTER) || !IsReal(loc, thing)
#else
    bool quiet = (  (hush & HUSH_ENTER)
#endif // REALITY_LVLS
                 || (  !Wizard(loc)
                    && (  Dark(thing)
                       || Dark(loc))
                    && (  !canhear
#if defined(FIRANMUX)
                       || Dark(thing))));
#else
                       || (  Wizard(thing)
                          && Dark(thing)))));
#endif // FIRANMUX

    int oattr = quiet ? 0 : A_OENTER;
#if defined(FIRANMUX)
    int aattr = (hush & HUSH_ENTER) ? 0 : A_AENTER;
#else
    int aattr = quiet ? 0 : A_AENTER;
#endif // FIRANMUX
    int pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_ENTER;

    did_it(thing, loc, pattr, nullptr, oattr, nullptr, aattr, 0, nullptr, 0);

    // Do OXLEAVE for sending room.
    //
    if (  src != NOTHING
       && !quiet)
    {
        did_it(thing, src, 0, nullptr, A_OXLEAVE, nullptr, 0, 0, nullptr, 0);
    }

    // Display the 'has arrived' message if we meet all of the following
    // criteria:
    //
    //  - The moving object can hear.
    //  - The object is not a dark wizard.
    //
    if (  !quiet
       && canhear
       && !Blind(thing)
       && !Blind(loc)
#if defined(FIRANMUX)
       && !Dark(thing))
#else
       && !(Dark(thing) && Wizard(thing)))
#endif // FIRANMUX
    {
#ifdef REALITY_LVLS
        notify_except2_rlevel(loc, thing, thing, cause,
#else
        notify_except2(loc, thing, thing, cause,
#endif // REALITY_LVLS
                   tprintf(T("%s has arrived."), Moniker(thing)));
    }
}

/* ---------------------------------------------------------------------------
 * move_object: Physically move an object from one place to another.
 * Does not generate any messages or actions.
 */

void move_object(dbref thing, dbref dest)
{
    dbref src = Location(thing);

    // Remove from the source location
    //
    if (src != NOTHING)
    {
        s_Contents(src, remove_first(Contents(src), thing));
    }

    // Special check for HOME
    //
    if (dest == HOME)
    {
        dest = Home(thing);
    }

    // Add to destination location
    //
    if (dest != NOTHING)
    {
        s_Contents(dest, insert_first(Contents(dest), thing));
    }
    else
    {
        s_Next(thing, NOTHING);
    }
    s_Location(thing, dest);

    // Look around and do the penny check
    //
    look_in(thing, dest, (LK_SHOWEXIT | LK_OBEYTERSE));
    if (  isPlayer(thing)
       && mudconf.payfind > 0
       && Pennies(thing) < mudconf.paylimit
       && !Controls(thing, dest)
       && RandomINT32(0, mudconf.payfind-1) == 0)
    {
        giveto(thing, 1);
        notify(thing, tprintf(T("You found a %s!"), mudconf.one_coin));
    }
}

// move_the_exit: Move an exit silently from its location to its destination
//
static void move_the_exit(dbref thing, dbref dest)
{
    dbref exitloc = Exits(thing);
    s_Exits(exitloc, remove_first(Exits(exitloc), thing));
    s_Exits(dest, insert_first(Exits(dest), thing));
    s_Exits(thing, dest);
}

/* ---------------------------------------------------------------------------
 * send_dropto, process_sticky_dropto, process_dropped_dropto,
 * process_sacrifice_dropto: Check for and process droptos.
 */

// send_dropto: Send an object through the dropto of a room
//
static void send_dropto(dbref thing, dbref player)
{
    if (!Sticky(thing))
    {
        move_via_generic(thing, Dropto(Location(thing)), player, 0);
    }
    else
    {
        move_via_generic(thing, HOME, player, 0);
    }
    divest_object(thing);
}

// process_sticky_dropto: Call when an object leaves the room to see if
// we should empty the room
//
static void process_sticky_dropto(dbref loc, dbref player)
{
    // Do nothing if checking anything but a sticky room
    //
    if (  !Good_obj(loc)
       || !Has_dropto(loc)
       || !Sticky(loc))
    {
        return;
    }

    // Make sure dropto loc is valid
    //
    dbref dropto = Dropto(loc);
    if (  NOTHING == dropto
       || dropto == loc)
    {
        return;
    }

    // Make sure no players hanging out
    //
    dbref thing;
    DOLIST(thing, Contents(loc))
    {
        if (  Connected(Owner(thing))
           && Hearer(thing))
        {
            return;
        }
    }

    // Send everything through the dropto
    //
    dbref next;
    s_Contents(loc, reverse_list(Contents(loc)));
    SAFE_DOLIST(thing, next, Contents(loc))
    {
        send_dropto(thing, player);
    }
}

// process_dropped_dropto: Check what to do when someone drops an object.
//
static void process_dropped_dropto(dbref thing, dbref player)
{
    // If STICKY, send home
    //
    if (Sticky(thing))
    {
        move_via_generic(thing, HOME, player, 0);
        divest_object(thing);
        return;
    }

    // Process the dropto if location is a room and is not STICKY
    //
    dbref loc = Location(thing);
    if (Has_dropto(loc) && (Dropto(loc) != NOTHING) && !Sticky(loc))
        send_dropto(thing, player);
}

/* ---------------------------------------------------------------------------
 * move_via_generic: Generic move routine, generates standard messages and
 * actions.
 */

void move_via_generic(dbref thing, dbref dest, dbref cause, int hush)
{
    if (dest == HOME)
    {
        dest = Home(thing);
    }

    dbref src = Location(thing);
    bool canhear = Hearer(thing);
    process_leave_loc(thing, dest, cause, canhear, hush);
    move_object(thing, dest);
    did_it(thing, thing, A_MOVE, nullptr, A_OMOVE, nullptr, A_AMOVE, 0, nullptr, 0);

#if defined(FIRANMUX)
    did_it(thing, thing, A_LEAD, nullptr, A_OLEAD, nullptr, A_ALEAD, 0, nullptr, 0);
#endif // FIRANMUX

    process_enter_loc(thing, src, cause, canhear, hush);
}

/* ---------------------------------------------------------------------------
 * move_via_exit: Exit move routine, generic + exit messages + dropto check.
 */

static void move_via_exit(dbref thing, dbref dest, dbref cause, dbref exit, int hush)
{
    if (dest == HOME)
    {
        dest = Home(thing);
    }
    dbref src = Location(thing);
    bool canhear = Hearer(thing);

#if defined(FIRANMUX)
    bool quiet = Dark(thing) || (hush & HUSH_EXIT);
    int aattr = (hush & HUSH_EXIT) ? 0 : A_ASUCC;
#else
    // Dark wizards don't trigger OSUCC/ASUCC
    bool quiet = (  (Wizard(thing) && Dark(thing))
                 || (hush & HUSH_EXIT));
    int aattr = quiet ? 0 : A_ASUCC;
#endif // FIRANMUX

    int oattr = quiet ? 0 : A_OSUCC;
    int pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_SUCC;
    did_it(thing, exit, pattr, nullptr, oattr, nullptr, aattr, 0, nullptr, 0);
    process_leave_loc(thing, dest, cause, canhear, hush);
    move_object(thing, dest);

#if defined(FIRANMUX)
    aattr = (hush & HUSH_EXIT) ? 0 : A_ADROP;
#else
    aattr = quiet ? 0 : A_ADROP;
#endif // FIRANMUX

    oattr = quiet ? 0 : A_ODROP;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_DROP;
    did_it(thing, exit, pattr, nullptr, oattr, nullptr, aattr, 0, nullptr, 0);

    did_it(thing, thing, A_MOVE, nullptr, A_OMOVE, nullptr, A_AMOVE, 0, nullptr, 0);

#if defined(FIRANMUX)
    did_it(thing, thing, A_LEAD, nullptr, A_OLEAD, nullptr, A_ALEAD, 0, nullptr, 0);
#endif

    process_enter_loc(thing, src, cause, canhear, hush);
    process_sticky_dropto(src, thing);
}

/* ---------------------------------------------------------------------------
 * move_via_teleport: Teleport move routine, generic + teleport messages +
 * divestiture + dropto check.
 */

bool move_via_teleport(dbref thing, dbref dest, dbref cause, int hush)
{
    dbref curr;
    int count;
    const UTF8 *failmsg;

    dbref src = Location(thing);
    if (  HOME != dest
       && Good_obj(src))
    {
        curr = src;
        for (count = mudconf.ntfy_nest_lim; count > 0; count--)
        {
            if (!could_doit(thing, curr, A_LTELOUT))
            {
                if (  thing == cause
                   || NOTHING == cause)
                {
                    failmsg = T("You can\xE2\x80\x99t teleport out!");
                }
                else
                {
                    failmsg = T("You can\xE2\x80\x99t be teleported out!");
                    notify_quiet(cause, T("You can\xE2\x80\x99t teleport that out!"));
                }

                did_it(thing, src,
                       A_TOFAIL, failmsg, A_OTOFAIL, nullptr,
                       A_ATOFAIL, 0, nullptr, 0);
                return false;
            }

            if (isRoom(curr))
            {
                break;
            }
            curr = Location(curr);
        }
    }

    if (isExit(thing))
    {
        move_the_exit(thing, dest);
        return true;
    }

    if (dest == HOME)
    {
        dest = Home(thing);
    }

    bool canhear = Hearer(thing);
    if (!(hush & HUSH_LEAVE))
    {
        did_it(thing, thing, 0, nullptr, A_OXTPORT, nullptr, 0,
            0, nullptr, 0);
    }
    process_leave_loc(thing, dest, NOTHING, canhear, hush);

    move_object(thing, dest);

    if (!(hush & HUSH_ENTER))
    {
        did_it(thing, thing, A_TPORT, nullptr, A_OTPORT, nullptr, A_ATPORT,
            0, nullptr, 0);
    }
    did_it(thing, thing, A_MOVE, nullptr, A_OMOVE, nullptr, A_AMOVE,
        0, nullptr, 0);

#if defined(FIRANMUX)
    did_it(thing, thing, A_LEAD, nullptr, A_OLEAD, nullptr, A_ALEAD,
           0, nullptr, 0);
#endif // FIRANMUX

    process_enter_loc(thing, src, NOTHING, canhear, hush);
    divest_object(thing);
    process_sticky_dropto(src, thing);
    return true;
}

/* ---------------------------------------------------------------------------
 * move_exit: Try to move a player through an exit.
 */

static dbref get_exit_dest(dbref executor, dbref exit)
{
    dbref aowner;
    int   aflags;
    UTF8 *atr_gotten = atr_pget(exit, A_EXITVARDEST, &aowner, &aflags);

    UTF8 *result = alloc_lbuf("get_exit_dest");
    UTF8 *ref = result;
    mux_exec(atr_gotten, LBUF_SIZE-1, result, &ref, exit, executor, executor,
        AttrTrace(aflags, EV_TOP|EV_FCHECK|EV_EVAL), nullptr, 0);
    free_lbuf(atr_gotten);
    *ref = '\0';

    dbref dest = NOTHING;
    if (*result == NUMBER_TOKEN)
    {
        dest = mux_atol(result + 1);
    }

    free_lbuf(result);
    return dest;
}

void move_exit(dbref player, dbref exit, bool divest, const UTF8 *failmsg, int hush)
{
    int oattr, aattr;
    bool bDoit = false;

    dbref loc = Location(exit);
    if (atr_get_raw(exit, A_EXITVARDEST) != nullptr)
    {
        loc = get_exit_dest(player, exit);
    }

    if (loc == HOME)
    {
        loc = Home(player);
    }

#ifdef WOD_REALMS
    if (Good_obj(loc) && (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player, exit, ACTION_IS_MOVING)))
    {
        if (isShroud(player))
        {
            bDoit = true;
            int iShroudWarded = get_atr(T("SHROUD_WARDED"));
            if (iShroudWarded > 0)
            {
                int owner, flags;
                UTF8 *buff = atr_pget(exit, iShroudWarded, &owner, &flags);
                if (buff)
                {
                    if (*buff)
                    {
                        bDoit = false;
                    }
                    free_lbuf(buff);
                }
            }
        }

        if (!bDoit && isUmbra(player))
        {
            bDoit = true;
            int iUmbraWarded = get_atr(T("UMBRA_WARDED"));
            if (iUmbraWarded > 0)
            {
                int owner, flags;
                UTF8 *buff = atr_pget(exit, iUmbraWarded, &owner, &flags);
                if (buff)
                {
                    if (*buff)
                    {
                        bDoit = false;
                    }
                    free_lbuf(buff);
                }
            }
        }

        if (!bDoit && could_doit(player, exit, A_LOCK))
        {
            bDoit = true;
        }
    }
#else
#if defined(FIRANMUX)
    if (Immobile(player))
    {
        notify(player, mudconf.immobile_msg);
        return;
    }
#endif // FIRANMUX
    if (Good_obj(loc) && could_doit(player, exit, A_LOCK))
    {
        bDoit = true;
    }
#endif

    if (bDoit)
    {
        switch (Typeof(loc))
        {
        case TYPE_ROOM:
            move_via_exit(player, loc, NOTHING, exit, hush);
            if (divest)
                divest_object(player);
            break;
        case TYPE_PLAYER:
        case TYPE_THING:
            if (Going(loc))
            {
                notify(player, T("You can\xE2\x80\x99t go that way."));
                return;
            }
            move_via_exit(player, loc, NOTHING, exit, hush);
            divest_object(player);
            break;
        case TYPE_EXIT:
            notify(player, T("You can\xE2\x80\x99t go that way."));
            return;
        }
    }
    else
    {
        if ((Wizard(player) && Dark(player)) || (hush & HUSH_EXIT))
        {
            oattr = 0;
#if defined(FIRANMUX)
            aattr = (hush & HUSH_EXIT) ? 0 : A_AFAIL;
#else
            aattr = 0;
#endif // FIRANMUX
        }
        else
        {
            oattr = A_OFAIL;
            aattr = A_AFAIL;
        }
        did_it(player, exit, A_FAIL, failmsg, oattr, nullptr, aattr, 0, nullptr, 0);
    }
}

/* ---------------------------------------------------------------------------
 * do_move: Move from one place to another via exits or 'home'.
 */

void do_move(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *direction, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref exit, loc;
    int i, quiet;

    if (!string_compare(direction, T("home")))
    {
        // Go home w/o stuff.
        //
        if (  (  Fixed(executor)
              || Fixed(Owner(executor)))
           && !(WizRoy(executor)))
        {
            notify(executor, mudconf.fixed_home_msg);
            return;
        }

#if defined(FIRANMUX)
        if (Immobile(executor))
        {
            notify(executor, mudconf.immobile_msg);
            return;
        }
#endif // FIRANMUX

        if (  (loc = Location(executor)) != NOTHING
           && !Dark(executor)
           && !Dark(loc))
        {
            // Tell all
            //
            notify_except(loc, executor, executor, tprintf(T("%s goes home."), Moniker(executor)), 0);
        }

        // Give the player the messages
        //
        for (i = 0; i < 3; i++)
        {
            notify(executor, T("There\xE2\x80\x99s no place like home..."));
        }
        move_via_generic(executor, HOME, NOTHING, 0);
        divest_object(executor);
        process_sticky_dropto(loc, executor);
        return;
    }

    // Find the exit.
    //
    init_match_check_keys(executor, direction, TYPE_EXIT);

#if defined(FIRANMUX)
    match_exit_with_parents();
#else
    match_exit();
#endif // FIRANMUX

    exit = match_result();
    switch (exit)
    {
    case NOTHING:       // Try to force the object
        notify(executor, T("You can\xE2\x80\x99t go that way."));
        break;
    case AMBIGUOUS:
        notify(executor, T("I don\xE2\x80\x99t know which way you mean!"));
        break;
    default:
        quiet = 0;
        if ((key & MOVE_QUIET) && Controls(executor, exit))
            quiet = HUSH_EXIT;
        move_exit(executor, exit, false, T("You can\xE2\x80\x99t go that way."), quiet);
    }
}

/* ---------------------------------------------------------------------------
 * do_get: Get an object.
 */

void do_get(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *what, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref playerloc;
    if (  !Has_location(executor)
       || !Good_obj(playerloc = Location(executor)))
    {
        return;
    }

    // You can only pick up things in rooms and ENTER_OK objects/players.
    //
    if (  !isRoom(playerloc)
       && !Enter_ok(playerloc)
       && !Controls(executor, playerloc))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    // Look for the thing locally.
    //
    init_match_check_keys(executor, what, TYPE_THING);
    match_neighbor();
    match_exit();
    if (Long_Fingers(executor))
    {
        match_absolute();
    }
    dbref thing = match_result();

    // Look for the thing in other people's inventories.
    //
    if (!Good_obj(thing))
    {
        thing = match_status(executor, match_possessed(executor, executor, what,
            thing, true));

        if (!Good_obj(thing))
        {
            return;
        }
    }

    // If we found it, check to see if we can get it.
    //
    dbref thingloc = Location(thing);
    if (Good_obj(thingloc))
    {
        if (!could_doit(executor, thingloc, A_LGET))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }
    }

    // If we can get it, get it.
    //
    const UTF8 *failmsg;
    int oattr, aattr;
    bool quiet = false;
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
    case TYPE_THING:

        // You can't take what you already have.
        //
        if (thingloc == executor)
        {
            notify(executor, T("You already have that!"));
            break;
        }
        if (  (key & GET_QUIET)
           && Controls(executor, thing))
        {
            quiet = true;
        }

        if (thing == executor)
        {
            notify(executor, T("You cannot get yourself!"));
        }
        else if (could_doit(executor, thing, A_LOCK))
        {
            if (thingloc != playerloc)
            {
                notify(thingloc, tprintf(T("%s was taken from you."),
                    Moniker(thing)));
            }
            move_via_generic(thing, executor, executor, 0);
            notify(thing, T("Taken."));
            oattr = quiet ? 0 : A_OSUCC;
            aattr = quiet ? 0 : A_ASUCC;
            did_it(executor, thing, A_SUCC, T("Taken."), oattr, nullptr,
                   aattr, 0, nullptr, 0);
        }
        else
        {
            oattr = quiet ? 0 : A_OFAIL;
            aattr = quiet ? 0 : A_AFAIL;
            if (thingloc != playerloc)
            {
                failmsg = T("You can\xE2\x80\x99t take that from there.");
            }
            else
            {
                failmsg = T("You can\xE2\x80\x99t pick that up.");
            }
            did_it(executor, thing, A_FAIL, failmsg, oattr, nullptr, aattr,
                0, nullptr, 0);
        }
        break;

    case TYPE_EXIT:

        // You can't take what you already have.
        //
        thingloc = Exits(thing);
        if (thingloc == executor)
        {
            notify(executor, T("You already have that!"));
            break;
        }

        // You must control either the exit or the location.
        //
        if (  !Controls(executor, thing)
           && !Controls(executor, playerloc))
        {
            notify(executor, NOPERM_MESSAGE);
            break;
        }

        // Do it.
        //
        s_Exits(thingloc, remove_first(Exits(thingloc), thing));
        s_Exits(executor, insert_first(Exits(executor), thing));
        s_Exits(thing, executor);
        if (!Quiet(executor))
        {
            notify(executor, T("Exit taken."));
        }
        break;

    default:

        notify(executor, T("You can\xE2\x80\x99t take that!"));
        break;
    }
}

/* ---------------------------------------------------------------------------
 * do_drop: Drop an object.
 */

void do_drop(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref loc = Location(executor);
    if (!Good_obj(loc))
    {
        return;
    }

    dbref exitloc, thing;
    UTF8 *buf, *bp;
    int oattr, aattr;
    bool quiet;

    init_match(executor, name, TYPE_THING);
    match_possession();
    match_carried_exit();

    switch (thing = match_result())
    {
    case NOTHING:
        notify(executor, T("You don\xE2\x80\x99t have that!"));
        return;

    case AMBIGUOUS:
        notify(executor, T("I don\xE2\x80\x99t know which you mean!"));
        return;
    }

    switch (Typeof(thing))
    {
    case TYPE_THING:
    case TYPE_PLAYER:

        // You have to be carrying it.
        //
        if (  (  Location(thing) != executor
              && !Wizard(executor))
           || !could_doit(executor, thing, A_LDROP))
        {
            did_it(executor, thing, A_DFAIL, T("You can\xE2\x80\x99t drop that."),
                   A_ODFAIL, nullptr, A_ADFAIL, 0, nullptr, 0);
            return;
        }

        // Move it
        //
        move_via_generic(thing, Location(executor), executor, 0);
        notify(thing, T("Dropped."));

        quiet = false;
        if (  (key & DROP_QUIET)
           && Controls(executor, thing))
        {
            quiet = true;
        }

        bp = buf = alloc_lbuf("do_drop.did_it");
        safe_tprintf_str(buf, &bp, T("dropped %s."), Moniker(thing));
        oattr = quiet ? 0 : A_ODROP;
        aattr = quiet ? 0 : A_ADROP;
        did_it(executor, thing, A_DROP, T("Dropped."), oattr, buf,
               aattr, 0, nullptr, 0);
        free_lbuf(buf);

        // Process droptos
        //
        process_dropped_dropto(thing, executor);
        break;

    case TYPE_EXIT:

        // You have to be carrying it.
        //
        if (  Exits(thing) != executor
           && !Wizard(executor))
        {
            notify(executor, T("You can\xE2\x80\x99t drop that."));
            return;
        }

        if (!Controls(executor, loc))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }

        // Do it.
        //
        exitloc = Exits(thing);
        s_Exits(exitloc, remove_first(Exits(exitloc), thing));
        s_Exits(loc, insert_first(Exits(loc), thing));
        s_Exits(thing, loc);

        if (!Quiet(executor))
        {
            notify(executor, T("Exit dropped."));
        }
        break;

    default:
        notify(executor, T("You can\xE2\x80\x99t drop that."));
        break;
    }
}

/* ---------------------------------------------------------------------------
 * do_enter, do_leave: The enter and leave commands.
 */

void do_enter_internal(dbref player, dbref thing, bool quiet)
{
    int oattr, aattr;

    if (!Enter_ok(thing) && !Controls(player, thing))
    {
        oattr = quiet ? 0 : A_OEFAIL;
        aattr = quiet ? 0 : A_AEFAIL;
        did_it(player, thing, A_EFAIL, NOPERM_MESSAGE,
               oattr, nullptr, aattr, 0, nullptr, 0);
    }
    else if (player == thing)
    {
        notify(player, T("You can\xE2\x80\x99t enter yourself!"));
    }
    else if (could_doit(player, thing, A_LENTER))
    {
        dbref loc = Location(player);
        oattr = quiet ? HUSH_ENTER : 0;
        move_via_generic(player, thing, NOTHING, oattr);
        divest_object(player);
        process_sticky_dropto(loc, player);
    }
    else
    {
        oattr = quiet ? 0 : A_OEFAIL;
        aattr = quiet ? 0 : A_AEFAIL;
        did_it(player, thing, A_EFAIL, T("You can\xE2\x80\x99t enter that."),
               oattr, nullptr, aattr, 0, nullptr, 0);
    }
}

void do_enter(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *what, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    init_match(executor, what, TYPE_THING);
    match_neighbor();
    if (Long_Fingers(executor))
        match_absolute();   // the wizard has long fingers

    dbref thing = noisy_match_result();
    bool bQuiet = false;

    if (thing == NOTHING)
        return;

    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
    case TYPE_THING:
        if ((key & MOVE_QUIET) && Controls(executor, thing))
            bQuiet = true;
        do_enter_internal(executor, thing, bQuiet);
        break;
    default:
        notify(executor, NOPERM_MESSAGE);
    }
    return;
}

void do_leave(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);

    dbref loc = Location(executor);
    dbref newLoc = loc;

    if (  !Good_obj(loc)
       || Going(loc)
       || !Has_location(loc)
       || !Good_obj(newLoc = Location(loc))
       || Going(newLoc))
    {
        notify(executor, T("You can\xE2\x80\x99t leave."));
        return;
    }
    int quiet = 0;
    if (  (key & MOVE_QUIET)
       && Controls(executor, loc))
    {
        quiet = HUSH_LEAVE;
    }
    if (could_doit(executor, loc, A_LLEAVE))
    {
        move_via_generic(executor, newLoc, NOTHING, quiet);
    }
    else
    {
        int oattr = quiet ? 0 : A_OLFAIL;
        int aattr = quiet ? 0 : A_ALFAIL;
        did_it(executor, loc, A_LFAIL, T("You can\xE2\x80\x99t leave."),
               oattr, nullptr, aattr, 0, nullptr, 0);
    }
}
