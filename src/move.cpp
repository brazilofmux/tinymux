// move.cpp -- Routines for moving about 
//
// $Id: move.cpp,v 1.8 2001-07-17 15:41:11 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mudconf.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "attrs.h"
#include "powers.h"

/*
 * ---------------------------------------------------------------------------
 * * process_leave_loc: Generate messages and actions resulting from leaving
 * * a place.
 */

static void process_leave_loc(dbref thing, dbref dest, dbref cause, int canhear, int hush)
{
    dbref loc;
    int quiet, pattr, oattr, aattr;

    loc = Location(thing);
    if ((loc == NOTHING) || (loc == dest))
        return;


    if (dest == HOME)
        dest = Home(thing);

    if (Html(thing)) {
        notify_html(thing, "<xch_page clear=links>");
    }

    /*
     * Run the LEAVE attributes in the current room if we meet any of * * 
     * 
     * *  * * following criteria: * - The current room has wizard privs.
     * * - * * * Neither the current room nor the moving object are dark. 
     * * - The * *  * moving object can hear and does not hav wizard
     * privs. * EXCEPT  * if * * we were called with the HUSH_LEAVE key. 
     */

    quiet = (!(Wizard(loc) ||
           (!Dark(thing) && !Dark(loc)) ||
           (canhear && !(Wizard(thing) && Dark(thing))))) ||
        (hush & HUSH_LEAVE);
    oattr = quiet ? 0 : A_OLEAVE;
    aattr = quiet ? 0 : A_ALEAVE;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_LEAVE;
    did_it(thing, loc, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);

    /*
     * Do OXENTER for receiving room 
     */

    if ((dest != NOTHING) && !quiet)
    {
        did_it(thing, dest, 0, NULL, A_OXENTER, NULL, 0, (char **)NULL, 0);
    }

    /*
     * Display the 'has left' message if we meet any of the following * * 
     * 
     * *  * * criteria: * - Neither the current room nor the moving
     * object are  * *  * dark. * - The object can hear and is not a dark 
     * wizard. 
     */

    if (!quiet)
    {
        if ((!Dark(thing) && !Dark(loc)) ||
            (canhear && !(Wizard(thing) && Dark(thing))))
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

static void process_enter_loc(dbref thing, dbref src, dbref cause, int canhear, int hush)
{
    dbref loc;
    int quiet, pattr, oattr, aattr;

    loc = Location(thing);
    if ((loc == NOTHING) || (loc == src))
        return;

    show_vrml_url(thing, loc);
    
    /*
     * Run the ENTER attributes in the current room if we meet any of * * 
     * 
     * *  * * following criteria: * - The current room has wizard privs.
     * * - * * * Neither the current room nor the moving object are dark. 
     * * - The * *  * moving object can hear and does not hav wizard
     * privs. * EXCEPT  * if * * we were called with the HUSH_ENTER key. 
     */

    quiet = (!(Wizard(loc) ||
           (!Dark(thing) && !Dark(loc)) ||
           (canhear && !(Wizard(thing) && Dark(thing))))) ||
        (hush & HUSH_ENTER);
    oattr = quiet ? 0 : A_OENTER;
    aattr = quiet ? 0 : A_AENTER;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_ENTER;
    did_it(thing, loc, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);

    /*
     * Do OXLEAVE for sending room 
     */

    if ((src != NOTHING) && !quiet)
    {
        did_it(thing, src, 0, NULL, A_OXLEAVE, NULL, 0, (char **)NULL, 0);
    }

    /*
     * Display the 'has arrived' message if we meet all of the following
     * * * * * criteria: * - The moving object can hear. * - The object
     * is * * not * a dark wizard. 
     */

    if (!quiet && canhear && !(Dark(thing) && Wizard(thing)))
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
    dbref src;

    /*
     * Remove from the source location 
     */

    src = Location(thing);
    if (src != NOTHING)
        s_Contents(src, remove_first(Contents(src), thing));

    /*
     * Special check for HOME 
     */

    if (dest == HOME)
        dest = Home(thing);

    /*
     * Add to destination location 
     */

    if (dest != NOTHING)
        s_Contents(dest, insert_first(Contents(dest), thing));
    else
        s_Next(thing, NOTHING);
    s_Location(thing, dest);

    /*
     * Look around and do the penny check 
     */

    look_in(thing, dest, (LK_SHOWEXIT | LK_OBEYTERSE));
    if (isPlayer(thing) &&
        (mudconf.payfind > 0) &&
        (Pennies(thing) < mudconf.paylimit) &&
        (!Controls(thing, dest)) &&
        RandomINT32(0, mudconf.payfind-1) == 0)
    {
        giveto(thing, 1);
        notify(thing, tprintf("You found a %s!", mudconf.one_coin));
    }
}

/*   
 * move_the_exit: Move an exit silently from it's location to it's destination
 */
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

/*
 * send_dropto: Send an object through the dropto of a room 
 */

static void send_dropto(dbref thing, dbref player)
{
    if (!Sticky(thing))
        move_via_generic(thing, Dropto(Location(thing)), player, 0);
    else
        move_via_generic(thing, HOME, player, 0);
    divest_object(thing);

}

/*
 * process_sticky_dropto: Call when an object leaves the room to see if
 * * we should empty the room
 */

static void process_sticky_dropto(dbref loc, dbref player)
{
    dbref dropto, thing, next;

    /*
     * Do nothing if checking anything but a sticky room 
     */

    if (!Good_obj(loc) || !Has_dropto(loc) || !Sticky(loc))
        return;

    /*
     * Make sure dropto loc is valid 
     */

    dropto = Dropto(loc);
    if ((dropto == NOTHING) || (dropto == loc))
        return;

    /*
     * Make sure no players hanging out 
     */

    DOLIST(thing, Contents(loc)) {
        if (Dropper(thing))
            return;
    }

    /*
     * Send everything through the dropto 
     */

    s_Contents(loc, reverse_list(Contents(loc)));
    SAFE_DOLIST(thing, next, Contents(loc)) {
        send_dropto(thing, player);
    }
}

/*
 * process_dropped_dropto: Check what to do when someone drops an object. 
 */

static void process_dropped_dropto(dbref thing, dbref player)
{
    dbref loc;

    /*
     * If STICKY, send home 
     */

    if (Sticky(thing)) {
        move_via_generic(thing, HOME, player, 0);
        divest_object(thing);
        return;
    }
    /*
     * Process the dropto if location is a room and is not STICKY 
     */

    loc = Location(thing);
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
    dbref src;
    int canhear;

    if (dest == HOME)
        dest = Home(thing);
    src = Location(thing);
    canhear = Hearer(thing);
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
    dbref src;
    int canhear, darkwiz, quiet, pattr, oattr, aattr;

    if (dest == HOME)
        dest = Home(thing);
    src = Location(thing);
    canhear = Hearer(thing);

    /*
     * Dark wizards don't trigger OSUCC/ASUCC 
     */

    darkwiz = (Wizard(thing) && Dark(thing));
    quiet = darkwiz || (hush & HUSH_EXIT);

    oattr = quiet ? 0 : A_OSUCC;
    aattr = quiet ? 0 : A_ASUCC;
    pattr = (!mudconf.terse_movemsg && Terse(thing)) ? 0 : A_SUCC;
    did_it(thing, exit, pattr, NULL, oattr, NULL, aattr,
           (char **)NULL, 0);
    process_leave_loc(thing, dest, cause, canhear, hush);
    move_object(thing, dest);

    /*
     * Dark wizards don't trigger ODROP/ADROP 
     */

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

int move_via_teleport(dbref thing, dbref dest, dbref cause, int hush)
{
    dbref curr;
    int canhear, count;
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
                return 0;
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
        return 1;
    }
    if (dest == HOME)
    {
        dest = Home(thing);
    }
    canhear = Hearer(thing);
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
    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * move_exit: Try to move a player through an exit.
 */

void move_exit(dbref player, dbref exit, int divest, const char *failmsg, int hush)
{
    dbref loc;
    int oattr, aattr;
    BOOL bDoit = FALSE;

    loc = Location(exit);
    if (loc == HOME)
        loc = Home(player);

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

void do_move(dbref player, dbref cause, int key, char *direction)
{
    dbref exit, loc;
    int i, quiet;

    if (!string_compare(direction, "home")) {   /*
                             * go home w/o stuff 
                             */
        if ((Fixed(player) || Fixed(Owner(player))) &&
            !(WizRoy(player))) {
            notify(player, mudconf.fixed_home_msg);
            return;
        }
        
        if ((loc = Location(player)) != NOTHING &&
            !Dark(player) && !Dark(loc)) {

            /*
             * tell all 
             */

            notify_except(loc, player, player, tprintf("%s goes home.", Name(player)), 0);
        }
        /*
         * give the player the messages 
         */

        for (i = 0; i < 3; i++)
            notify(player, "There's no place like home...");
        move_via_generic(player, HOME, NOTHING, 0);
        divest_object(player);
        process_sticky_dropto(loc, player);
        return;
    }
    /*
     * find the exit 
     */

    init_match_check_keys(player, direction, TYPE_EXIT);
    match_exit();
    exit = match_result();
    switch (exit) {
    case NOTHING:       /*
                 * try to force the object 
                 */
        notify(player, "You can't go that way.");
        break;
    case AMBIGUOUS:
        notify(player, "I don't know which way you mean!");
        break;
    default:
        quiet = 0;
        if ((key & MOVE_QUIET) && Controls(player, exit))
            quiet = HUSH_EXIT;
        move_exit(player, exit, 0, "You can't go that way.", quiet);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_get: Get an object.
 */

void do_get(dbref player, dbref cause, int key, char *what)
{
    dbref thing, playerloc, thingloc;
    char *failmsg;
    int oattr, aattr, quiet;

    playerloc = Location(player);
    if (!Good_obj(playerloc))
        return;

    /*
     * You can only pick up things in rooms and ENTER_OK objects/players 
     */

    if (!isRoom(playerloc) && !Enter_ok(playerloc) &&
        !controls(player, playerloc)) {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    /*
     * Look for the thing locally 
     */

    init_match_check_keys(player, what, TYPE_THING);
    match_neighbor();
    match_exit();
    if (Long_Fingers(player))
        match_absolute();   /*
                     * long fingers 
                     */
    thing = match_result();

    /*
     * Look for the thing in other people's inventories 
     */

    if (!Good_obj(thing))
        thing = match_status(player,
               match_possessed(player, player, what, thing, 1));
    if (!Good_obj(thing))
        return;

    /*
     * If we found it, get it 
     */

    quiet = 0;
    switch (Typeof(thing)) {
    case TYPE_PLAYER:
    case TYPE_THING:
        /*
         * You can't take what you already have 
         */

        thingloc = Location(thing);
        if (thingloc == player) {
            notify(player, "You already have that!");
            break;
        }
        if ((key & GET_QUIET) && Controls(player, thing))
            quiet = 1;

        if (thing == player) {
            notify(player, "You cannot get yourself!");
        } else if (could_doit(player, thing, A_LOCK)) {
            if (thingloc != Location(player)) {
                notify(thingloc,
                       tprintf("%s was taken from you.",
                           Name(thing)));
            }
            move_via_generic(thing, player, player, 0);
            notify(thing, "Taken.");
            oattr = quiet ? 0 : A_OSUCC;
            aattr = quiet ? 0 : A_ASUCC;
            did_it(player, thing, A_SUCC, "Taken.", oattr, NULL,
                   aattr, (char **)NULL, 0);
        } else {
            oattr = quiet ? 0 : A_OFAIL;
            aattr = quiet ? 0 : A_AFAIL;
            if (thingloc != Location(player))
                failmsg = (char *)"You can't take that from there.";
            else
                failmsg = (char *)"You can't pick that up.";
            did_it(player, thing,
                   A_FAIL, failmsg,
                   oattr, NULL, aattr, (char **)NULL, 0);
        }
        break;
    case TYPE_EXIT:
        /*
         * You can't take what you already have 
         */

        thingloc = Exits(thing);
        if (thingloc == player) {
            notify(player, "You already have that!");
            break;
        }
        /*
         * You must control either the exit or the location 
         */

        playerloc = Location(player);
        if (!Controls(player, thing) && !Controls(player, playerloc)) {
            notify(player, NOPERM_MESSAGE);
            break;
        }
        /*
         * Do it 
         */

        s_Exits(thingloc, remove_first(Exits(thingloc), thing));
        s_Exits(player, insert_first(Exits(player), thing));
        s_Exits(thing, player);
        if (!Quiet(player))
            notify(player, "Exit taken.");
        break;
    default:
        notify(player, "You can't take that!");
        break;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_drop: Drop an object.
 */

void do_drop(dbref player, dbref cause, int key, char *name)
{
    dbref loc, exitloc, thing;
    char *buf, *bp;
    int quiet, oattr, aattr;

    loc = Location(player);
    if (!Good_obj(loc))
        return;

    init_match(player, name, TYPE_THING);
    match_possession();
    match_carried_exit();

    switch (thing = match_result()) {
    case NOTHING:
        notify(player, "You don't have that!");
        return;
    case AMBIGUOUS:
        notify(player, "I don't know which you mean!");
        return;
    }

    switch (Typeof(thing)) {
    case TYPE_THING:
    case TYPE_PLAYER:

        /*
         * You have to be carrying it 
         */

        if (((Location(thing) != player) && !Wizard(player)) ||
            (!could_doit(player, thing, A_LDROP))) {
            did_it(player, thing, A_DFAIL, "You can't drop that.",
                   A_ODFAIL, NULL, A_ADFAIL, (char **)NULL, 0);
            return;
        }
        /*
         * Move it 
         */

        move_via_generic(thing, Location(player), player, 0);
        notify(thing, "Dropped.");
        quiet = 0;
        if ((key & DROP_QUIET) && Controls(player, thing))
            quiet = 1;
        bp = buf = alloc_lbuf("do_drop.did_it");
        safe_tprintf_str(buf, &bp, "dropped %s.", Name(thing));
        oattr = quiet ? 0 : A_ODROP;
        aattr = quiet ? 0 : A_ADROP;
        did_it(player, thing, A_DROP, "Dropped.", oattr, buf,
               aattr, (char **)NULL, 0);
        free_lbuf(buf);

        /*
         * Process droptos 
         */

        process_dropped_dropto(thing, player);

        break;
    case TYPE_EXIT:

        /*
         * You have to be carrying it 
         */

        if ((Exits(thing) != player) && !Wizard(player)) {
            notify(player, "You can't drop that.");
            return;
        }
        if (!Controls(player, loc)) {
            notify(player, NOPERM_MESSAGE);
            return;
        }
        /*
         * Do it 
         */

        exitloc = Exits(thing);
        s_Exits(exitloc, remove_first(Exits(exitloc), thing));
        s_Exits(loc, insert_first(Exits(loc), thing));
        s_Exits(thing, loc);

        if (!Quiet(player))
            notify(player, "Exit dropped.");
        break;
    default:
        notify(player, "You can't drop that.");
    }

}

/*
 * ---------------------------------------------------------------------------
 * * do_enter, do_leave: The enter and leave commands.
 */

void do_enter_internal(dbref player, dbref thing, int quiet)
{
    dbref loc;
    int oattr, aattr;

    if (!Enter_ok(thing) && !controls(player, thing))
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
        loc = Location(player);
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

void do_enter(dbref player, dbref cause, int key, char *what)
{
    dbref thing;
    int quiet;

    init_match(player, what, TYPE_THING);
    match_neighbor();
    if (Long_Fingers(player))
        match_absolute();   /*
                     * the wizard has long fingers 
                     */

    if ((thing = noisy_match_result()) == NOTHING)
        return;

    switch (Typeof(thing)) {
    case TYPE_PLAYER:
    case TYPE_THING:
        quiet = 0;
        if ((key & MOVE_QUIET) && Controls(player, thing))
            quiet = 1;
        do_enter_internal(player, thing, quiet);
        break;
    default:
        notify(player, NOPERM_MESSAGE);
    }
    return;
}

void do_leave(dbref player, dbref cause, int key)
{
    dbref loc = Location(player);
    dbref newLoc = loc;

    if (  !Good_obj(loc)
       || Going(loc)
       || !Has_location(loc)
       || isGarbage(newLoc = Location(loc))
       || !Good_obj(newLoc)
       || Going(newLoc))
    {
        notify(player, "You can't leave.");
        return;
    }
    int quiet = 0;
    if (  (key & MOVE_QUIET)
       && Controls(player, loc))
    {
        quiet = HUSH_LEAVE;
    }
    if (could_doit(player, loc, A_LLEAVE))
    {
        move_via_generic(player, newLoc, NOTHING, quiet);
    }
    else
    {
        int oattr = quiet ? 0 : A_OLFAIL;
        int aattr = quiet ? 0 : A_ALFAIL;
        did_it(player, loc, A_LFAIL, "You can't leave.",
               oattr, NULL, aattr, (char **)NULL, 0);
    }
}
