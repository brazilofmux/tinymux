// move.cpp -- Routines for moving about.
//
// $Id: move.cpp,v 1.14 2002-07-14 00:42:19 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "interface.h"
#include "powers.h"

/*
 * ---------------------------------------------------------------------------
 * * process_leave_loc: Generate messages and actions resulting from leaving
 * * a place.
 */

static void process_leave_loc(dbref thing, dbref dest, dbref cause, BOOL canhear, int hush)
{
    dbref loc = Location(thing);
    if ((loc == NOTHING) || (loc == dest))
    {
        return;
    }

    int pattr, oattr, aattr;
    BOOL quiet;

    if (dest == HOME)
        dest = Home(thing);

    if (Html(thing))
    {
        notify_html(thing, "<xch_page clear=links>");
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

    quiet = (  (hush & HUSH_LEAVE)
            || (  (  !Wizard(loc)
                  && (  Dark(thing)
                     || Dark(loc))
                  && (  !canhear
                     || (Wizard(thing) && Dark(thing))))));

    oattr = quiet ? 0 : A_OLEAVE;
    aattr = quiet ? 0 : A_ALEAVE;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_LEAVE;
    did_it(thing, loc, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);

    // Do OXENTER for receiving room
    //
    if ((dest != NOTHING) && !quiet)
    {
        did_it(thing, dest, 0, NULL, A_OXENTER, NULL, 0, (char **)NULL, 0);
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
              && !(Wizard(thing) && Dark(thing))))
        {
            notify_except2(loc, thing, thing, cause,
                       tprintf("%s has left.", Name(thing)));
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * process_enter_loc: Generate messages and actions resulting from entering
 * * a place.
 */

static void process_enter_loc(dbref thing, dbref src, dbref cause, BOOL canhear, int hush)
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
    int quiet =    (hush & HUSH_ENTER)
                || (  !Wizard(loc)
                   && (  Dark(thing)
                      || Dark(loc))
                   && (  !canhear
                      || (  Wizard(thing)
                         && Dark(thing))));

    int oattr = quiet ? 0 : A_OENTER;
    int aattr = quiet ? 0 : A_AENTER;
    int pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_ENTER;

    did_it(thing, loc, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);

    // Do OXLEAVE for sending room.
    //
    if (  src != NOTHING
       && !quiet)
    {
        did_it(thing, src, 0, NULL, A_OXLEAVE, NULL, 0, (char **)NULL, 0);
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
       && !(Dark(thing) && Wizard(thing)))
    {
        notify_except2(loc, thing, thing, cause,
                   tprintf("%s has arrived.", Name(thing)));
    }
}

/*
 * ---------------------------------------------------------------------------
 * * move_object: Physically move an object from one place to another.
 * * Does not generate any messages or actions.
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
        notify(thing, tprintf("You found a %s!", mudconf.one_coin));
    }
}

// move_the_exit: Move an exit silently from its location to its destination
//
void move_the_exit(dbref thing, dbref dest)
{
    dbref exitloc = Exits(thing);
    s_Exits(exitloc, remove_first(Exits(exitloc), thing));
    s_Exits(dest, insert_first(Exits(dest), thing));
    s_Exits(thing, dest);
}

/*
 * ---------------------------------------------------------------------------
 * * send_dropto, process_sticky_dropto, process_dropped_dropto,
 * * process_sacrifice_dropto: Check for and process droptos.
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
    dbref dropto, thing, next;

    // Do nothing if checking anything but a sticky room 
    //
    if (!Good_obj(loc) || !Has_dropto(loc) || !Sticky(loc))
        return;

    // Make sure dropto loc is valid 
    //
    dropto = Dropto(loc);
    if ((dropto == NOTHING) || (dropto == loc))
        return;

    // Make sure no players hanging out 
    //
    DOLIST(thing, Contents(loc)) 
    {
        if ((Connected(Owner(thing)) && Hearer(thing)))
            return;
    }

    // Send everything through the dropto 
    //
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

/*
 * ---------------------------------------------------------------------------
 * * move_via_generic: Generic move routine, generates standard messages and
 * * actions.
 */

void move_via_generic(dbref thing, dbref dest, dbref cause, int hush)
{
    if (dest == HOME)
    {
        dest = Home(thing);
    }

    dbref src = Location(thing);
    BOOL canhear = Hearer(thing);
    process_leave_loc(thing, dest, cause, canhear, hush);
    move_object(thing, dest);
    did_it(thing, thing, A_MOVE, NULL, A_OMOVE, NULL, A_AMOVE,
           (char **)NULL, 0);
    process_enter_loc(thing, src, cause, canhear, hush);
}

/*
 * ---------------------------------------------------------------------------
 * * move_via_exit: Exit move routine, generic + exit messages + dropto check.
 */

void move_via_exit(dbref thing, dbref dest, dbref cause, dbref exit, int hush)
{
    if (dest == HOME)
    {
        dest = Home(thing);
    }
    dbref src = Location(thing);
    BOOL canhear = Hearer(thing);
    BOOL quiet = (  (Wizard(thing) && Dark(thing)) // Dark wizards don't trigger OSUCC/ASUCC
                 || (hush & HUSH_EXIT));

    int oattr = quiet ? 0 : A_OSUCC;
    int aattr = quiet ? 0 : A_ASUCC;
    int pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_SUCC;
    did_it(thing, exit, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);
    process_leave_loc(thing, dest, cause, canhear, hush);
    move_object(thing, dest);

    // Dark wizards don't trigger ODROP/ADROP
    //
    oattr = quiet ? 0 : A_ODROP;
    aattr = quiet ? 0 : A_ADROP;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_DROP;
    did_it(thing, exit, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);

    did_it(thing, thing, A_MOVE, NULL, A_OMOVE, NULL, A_AMOVE,
           (char **)NULL, 0);
    process_enter_loc(thing, src, cause, canhear, hush);
    process_sticky_dropto(src, thing);
}

/*
 * ---------------------------------------------------------------------------
 * * move_via_teleport: Teleport move routine, generic + teleport messages +
 * * divestiture + dropto check.
 */

BOOL move_via_teleport(dbref thing, dbref dest, dbref cause, int hush)
{
    dbref curr;
    int count;
    char *failmsg;

    dbref src = Location(thing);
    if ((dest != HOME) && Good_obj(src))
    {
        curr = src;
        for (count = mudconf.ntfy_nest_lim; count > 0; count--)
        {
            if (!could_doit(thing, curr, A_LTELOUT))
            {
                if ((thing == cause) || (cause == NOTHING))
                {
                    failmsg = "You can't teleport out!";
                }
                else
                {
                    failmsg = "You can't be teleported out!";
                    notify_quiet(cause, "You can't teleport that out!");
                }
                did_it(thing, src,
                       A_TOFAIL, failmsg, A_OTOFAIL, NULL,
                       A_ATOFAIL, (char **)NULL, 0);
                return FALSE;
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
        return TRUE;
    }
    if (dest == HOME)
    {
        dest = Home(thing);
    }
    BOOL canhear = Hearer(thing);
    if (!(hush & HUSH_LEAVE))
    {
        did_it(thing, thing, 0, NULL, A_OXTPORT, NULL, 0,
            (char **)NULL, 0);
    }
    process_leave_loc(thing, dest, NOTHING, canhear, hush);

    move_object(thing, dest);

    if (!(hush & HUSH_ENTER))
    {
        did_it(thing, thing, A_TPORT, NULL, A_OTPORT, NULL, A_ATPORT,
            (char **)NULL, 0);
    }
    did_it(thing, thing, A_MOVE, NULL, A_OMOVE, NULL, A_AMOVE,
        (char **)NULL, 0);
    process_enter_loc(thing, src, NOTHING, canhear, hush);
    divest_object(thing);
    process_sticky_dropto(src, thing);
    return TRUE;
}

/*
 * ---------------------------------------------------------------------------
 * * move_exit: Try to move a player through an exit.
 */

void move_exit(dbref player, dbref exit, BOOL divest, const char *failmsg, int hush)
{
    int oattr, aattr;
    BOOL bDoit = FALSE;

    dbref loc = Location(exit);
    if (loc == HOME)
    {
        loc = Home(player);
    }

#ifdef WOD_REALMS
    if (Good_obj(loc) && (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player, exit, ACTION_IS_MOVING)))
    {
        if (isShroud(player))
        {
            bDoit = TRUE;
            int iShroudWarded = get_atr("SHROUD_WARDED");
            if (iShroudWarded > 0)
            {
                int owner, flags;
                char *buff = atr_pget(exit, iShroudWarded, &owner, &flags);
                if (buff)
                {
                    if (*buff)
                    {
                        bDoit = FALSE;
                    }
                    free_lbuf(buff);
                }
            }
        }

        if (!bDoit && isUmbra(player))
        {
            bDoit = TRUE;
            int iUmbraWarded = get_atr("UMBRA_WARDED");
            if (iUmbraWarded > 0)
            {
                int owner, flags;
                char *buff = atr_pget(exit, iUmbraWarded, &owner, &flags);
                if (buff)
                {
                    if (*buff)
                    {
                        bDoit = FALSE;
                    }
                    free_lbuf(buff);
                }
            }
        }

        if (!bDoit && could_doit(player, exit, A_LOCK))
        {
            bDoit = TRUE;
        }
    }
#else
    if (Good_obj(loc) && could_doit(player, exit, A_LOCK))
    {
        bDoit = TRUE;
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
                notify(player, "You can't go that way.");
                return;
            }
            move_via_exit(player, loc, NOTHING, exit, hush);
            divest_object(player);
            break;
        case TYPE_EXIT:
            notify(player, "You can't go that way.");
            return;
        }
    }
    else
    {
        if ((Wizard(player) && Dark(player)) || (hush & HUSH_EXIT))
        {
            oattr = 0;
            aattr = 0;
        }
        else
        {
            oattr = A_OFAIL;
            aattr = A_AFAIL;
        }
        did_it(player, exit, A_FAIL, failmsg, oattr, NULL, aattr, (char **)NULL, 0);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_move: Move from one place to another via exits or 'home'.
 */

void do_move(dbref executor, dbref caller, dbref enactor, int key, char *direction)
{
    dbref exit, loc;
    int i, quiet;

    if (!string_compare(direction, "home")) 
    {   
        // Go home w/o stuff.
        //
        if ((Fixed(executor) || Fixed(Owner(executor))) &&
            !(WizRoy(executor))) 
        {
            notify(executor, mudconf.fixed_home_msg);
            return;
        }

        if ((loc = Location(executor)) != NOTHING &&
            !Dark(executor) && !Dark(loc)) 
        {
            // Tell all 
            //
            notify_except(loc, executor, executor, tprintf("%s goes home.", Name(executor)), 0);
        }
        // Give the player the messages 
        //
        for (i = 0; i < 3; i++)
            notify(executor, "There's no place like home...");
        move_via_generic(executor, HOME, NOTHING, 0);
        divest_object(executor);
        process_sticky_dropto(loc, executor);
        return;
    }
    // Hind the exit
    //
    init_match_check_keys(executor, direction, TYPE_EXIT);
    match_exit();
    exit = match_result();
    switch (exit) {
    case NOTHING:       // Try to force the object
        notify(executor, "You can't go that way.");
        break;
    case AMBIGUOUS:
        notify(executor, "I don't know which way you mean!");
        break;
    default:
        quiet = 0;
        if ((key & MOVE_QUIET) && Controls(executor, exit))
            quiet = HUSH_EXIT;
        move_exit(executor, exit, FALSE, "You can't go that way.", quiet);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_get: Get an object.
 */

void do_get(dbref executor, dbref caller, dbref enactor, int key, char *what)
{
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
            thing, TRUE));

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
    char *failmsg;
    int oattr, aattr;
    BOOL quiet = FALSE;
    switch (Typeof(thing))
    {
    case TYPE_PLAYER:
    case TYPE_THING:

        // You can't take what you already have.
        //
        if (thingloc == executor)
        {
            notify(executor, "You already have that!");
            break;
        }
        if (  (key & GET_QUIET)
           && Controls(executor, thing))
        {
            quiet = TRUE;
        }

        if (thing == executor)
        {
            notify(executor, "You cannot get yourself!");
        }
        else if (could_doit(executor, thing, A_LOCK))
        {
            if (thingloc != playerloc)
            {
                notify(thingloc, tprintf("%s was taken from you.",
                    Name(thing)));
            }
            move_via_generic(thing, executor, executor, 0);
            notify(thing, "Taken.");
            oattr = quiet ? 0 : A_OSUCC;
            aattr = quiet ? 0 : A_ASUCC;
            did_it(executor, thing, A_SUCC, "Taken.", oattr, NULL,
                   aattr, (char **)NULL, 0);
        }
        else
        {
            oattr = quiet ? 0 : A_OFAIL;
            aattr = quiet ? 0 : A_AFAIL;
            if (thingloc != playerloc)
            {
                failmsg = (char *)"You can't take that from there.";
            }
            else
            {
                failmsg = (char *)"You can't pick that up.";
            }
            did_it(executor, thing, A_FAIL, failmsg, oattr, NULL, aattr,
                (char **)NULL, 0);
        }
        break;

    case TYPE_EXIT:

        // You can't take what you already have.
        //
        thingloc = Exits(thing);
        if (thingloc == executor)
        {
            notify(executor, "You already have that!");
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
            notify(executor, "Exit taken.");
        }
        break;

    default:

        notify(executor, "You can't take that!");
        break;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_drop: Drop an object.
 */

void do_drop(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    dbref loc = Location(executor);
    if (!Good_obj(loc))
        return;

    dbref exitloc, thing;
    char *buf, *bp;
    int oattr, aattr;
    BOOL quiet;

    init_match(executor, name, TYPE_THING);
    match_possession();
    match_carried_exit();

    switch (thing = match_result()) 
    {
    case NOTHING:
        notify(executor, "You don't have that!");
        return;
    case AMBIGUOUS:
        notify(executor, "I don't know which you mean!");
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
            did_it(executor, thing, A_DFAIL, "You can't drop that.",
                   A_ODFAIL, NULL, A_ADFAIL, (char **)NULL, 0);
            return;
        }

        // Move it
        //
        move_via_generic(thing, Location(executor), executor, 0);
        notify(thing, "Dropped.");
        quiet = FALSE;
        if ((key & DROP_QUIET) && Controls(executor, thing))
            quiet = TRUE;
        bp = buf = alloc_lbuf("do_drop.did_it");
        safe_tprintf_str(buf, &bp, "dropped %s.", Name(thing));
        oattr = quiet ? 0 : A_ODROP;
        aattr = quiet ? 0 : A_ADROP;
        did_it(executor, thing, A_DROP, "Dropped.", oattr, buf,
               aattr, (char **)NULL, 0);
        free_lbuf(buf);

        // Process droptos
        //
        process_dropped_dropto(thing, executor);

        break;
    case TYPE_EXIT:

        // You have to be carrying it.
        //
        if ((Exits(thing) != executor) && !Wizard(executor)) 
        {
            notify(executor, "You can't drop that.");
            return;
        }
        if (!Controls(executor, loc)) 
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }

        // Do it
        //
        exitloc = Exits(thing);
        s_Exits(exitloc, remove_first(Exits(exitloc), thing));
        s_Exits(loc, insert_first(Exits(loc), thing));
        s_Exits(thing, loc);

        if (!Quiet(executor))
            notify(executor, "Exit dropped.");
        break;
    default:
        notify(executor, "You can't drop that.");
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_enter, do_leave: The enter and leave commands.
 */

void do_enter_internal(dbref player, dbref thing, BOOL quiet)
{
    int oattr, aattr;

    if (!Enter_ok(thing) && !Controls(player, thing))
    {
        oattr = quiet ? 0 : A_OEFAIL;
        aattr = quiet ? 0 : A_AEFAIL;
        did_it(player, thing, A_EFAIL, NOPERM_MESSAGE,
               oattr, NULL, aattr, (char **)NULL, 0);
    }
    else if (player == thing)
    {
        notify(player, "You can't enter yourself!");
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
        did_it(player, thing, A_EFAIL, "You can't enter that.",
               oattr, NULL, aattr, (char **)NULL, 0);
    }
}

void do_enter(dbref executor, dbref caller, dbref enactor, int key, char *what)
{
    init_match(executor, what, TYPE_THING);
    match_neighbor();
    if (Long_Fingers(executor))
        match_absolute();   // the wizard has long fingers

    dbref thing = noisy_match_result();
    BOOL bQuiet = FALSE;

    if (thing == NOTHING)
        return;

    switch (Typeof(thing)) 
    {
    case TYPE_PLAYER:
    case TYPE_THING:
        if ((key & MOVE_QUIET) && Controls(executor, thing))
            bQuiet = TRUE;
        do_enter_internal(executor, thing, bQuiet);
        break;
    default:
        notify(executor, NOPERM_MESSAGE);
    }
    return;
}

void do_leave(dbref executor, dbref caller, dbref enactor, int key)
{
    dbref loc = Location(executor);
    dbref newLoc = loc;

    if (  !Good_obj(loc)
       || Going(loc)
       || !Has_location(loc)
       || !Good_obj(newLoc = Location(loc))
       || Going(newLoc))
    {
        notify(executor, "You can't leave.");
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
        did_it(executor, loc, A_LFAIL, "You can't leave.",
               oattr, NULL, aattr, (char **)NULL, 0);
    }
}
