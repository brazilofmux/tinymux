// wiz.cpp -- Wizard-only commands.
//
// $Id: wiz.cpp,v 1.24 2002-02-13 18:57:29 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "file_c.h"
#include "match.h"
#include "command.h"
#include "attrs.h"
#include "powers.h"

extern char *FDECL(crypt, (const char *, const char *));

void do_teleport
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    dbref victim, destination, loc;
    char *to;
    int hush = 0;

    if (  (  Fixed(player)
          || Fixed(Owner(player))
          )
       && !Tel_Anywhere(player)
       )
    {
        notify(player, mudconf.fixed_tel_msg);
        return;
    }

    // Get victim.
    //
    if (nargs == 1)
    {
        victim = player;
        to = arg1;
    }
    else if (nargs == 2)
    {
        init_match(player, arg1, NOTYPE);
        match_everything(0);
        victim = noisy_match_result();

        if (victim == NOTHING)
        {
            return;
        }
        to = arg2;
    }
    else
    {
        return;
    }

    // Validate type of victim.
    //
    if (  isGarbage(victim)
       || isRoom(victim))
    {
        notify_quiet(player, "You can't teleport that.");
        return;
    }

    // Fail if we don't control the victim or the victim's location.
    //
    if (  !Controls(player, victim)
       && (  (isExit(victim) && !Controls(player, Home(victim)))
	      || !Controls(player, Location(victim)))
       && !Tel_Anything(player))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return;
    }

    // Check for teleporting home
    //
    if (!string_compare(to, "home"))
    {
        if (isExit(victim))
        {
            notify_quiet(player, "Bad destination.");
        }
        else
        {
            move_via_teleport(victim, HOME, cause, 0);
        }
        return;
    }

    // Find out where to send the victim.
    //
    init_match(player, to, NOTYPE);
    match_everything(0);
    destination = match_result();

    switch (destination)
    {
    case NOTHING:

        notify_quiet(player, "No match.");
        return;

    case AMBIGUOUS:

        notify_quiet(player, "I don't know which destination you mean!");
        return;

    default:

        if (victim == destination)
        {
            notify_quiet(player, "Bad destination.");
            return;
        }
    }

    // If fascist teleport is on, you must control the victim's ultimate
    // location (after LEAVEing any objects) or it must be JUMP_OK.
    //
    if (mudconf.fascist_tport)
    {
        if (isExit(victim))
        {
			loc = where_room(Home(victim));
        }
		else
        {
			loc = where_room(victim);
        }

        if (  !Good_obj(loc)
           || !isRoom(loc)
           || !( Controls(player, loc)
              || Jump_ok(loc)
              || Tel_Anywhere(player)
              )
           )
        {
            notify_quiet(player, NOPERM_MESSAGE);
            return;
        }
    }
    if (  isGarbage(destination)
       || (  Has_location(destination)
          && isGarbage(Location(destination))))
    {
        // @Teleporting into garbage is never permitted.
        //
        notify_quiet(player, "Bad destination.");
        return;
    }
    else if (Has_contents(destination))
    {
        // You must control the destination, or it must be a JUMP_OK
        // room where you pass its TELEPORT lock.
        //
        if (  !( Controls(player, destination)
                 || Jump_ok(destination)
                 || Tel_Anywhere(player)
               )
              || !could_doit(player, destination, A_LTPORT)
              || ( isExit(victim) && God(destination) && !God(player) )
           )
        {
            // Nope, report failure.
            //
            if (player != victim)
            {
                notify_quiet(player, NOPERM_MESSAGE);
            }
            did_it(victim, destination,
                   A_TFAIL, "You can't teleport there!",
                   A_OTFAIL, 0, A_ATFAIL, (char **)NULL, 0);
            return;
        }

        // We're OK, do the teleport.
        //
        if (key & TELEPORT_QUIET)
        {
            hush = HUSH_ENTER | HUSH_LEAVE;
        }

        if (move_via_teleport(victim, destination, cause, hush))
        {
            if (player != victim)
            {
                if (!Quiet(player))
                {
                    notify_quiet(player, "Teleported.");
                }
            }
        }
    }
    else if ( isExit(destination) )
    {
        if ( isExit(victim) )
        {
            if ( player != victim )
            {
                notify_quiet(player, "Bad destination.");
            }
            did_it(victim, destination,
                   A_TFAIL, "You can't teleport there!",
                   A_OTFAIL, 0, A_ATFAIL, (char **)NULL, 0);
            return;
        }
        else
        {
            if (Exits(destination) == Location(victim))
            {
                move_exit(victim, destination, 0, "You can't go that way.", 0);
            }
            else
            {
                notify_quiet(player, "I can't find that exit.");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_force_prefixed: Interlude to do_force for the # command
//
void do_force_prefixed( dbref player, dbref cause, int key, char *command,
                        char *args[], int nargs )
{
    char *cp;

    cp = parse_to(&command, ' ', 0);
    if (!command)
    {
        return;
    }
    while (Tiny_IsSpace[(unsigned char)*command])
    {
        command++;
    }
    if (*command)
    {
        do_force(player, cause, key, cp, command, args, nargs);
    }
}

// ---------------------------------------------------------------------------
// do_force: Force an object to do something.
//
void do_force( dbref player, dbref cause, int key, char *what, char *command,
               char *args[], int nargs )
{
    dbref victim = match_controlled(player, what);
    if (victim != NOTHING)
    {
        // Force victim to do command.
        //
        CLinearTimeAbsolute lta;
        wait_que(victim, player, FALSE, lta, NOTHING, 0, command,
            args, nargs, mudstate.global_regs);
    }
}

// ---------------------------------------------------------------------------
// do_toad: Turn a player into an object.
//
void do_toad
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *toad,
    char *newowner
)
{
    dbref victim, recipient, loc, aowner;
    char *buf;
    int count, aflags;

    init_match(player, toad, TYPE_PLAYER);
    match_neighbor();
    match_absolute();
    match_player();
    if ((victim = noisy_match_result()) == NOTHING)
    {
        return;
    }

    if (!isPlayer(victim))
    {
        notify_quiet(player, "Try @destroy instead.");
        return;
    }
    if (No_Destroy(victim))
    {
        notify_quiet(player, "You can't toad that player.");
        return;
    }
    if (  nargs == 2
       && *newowner )
    {
        init_match(player, newowner, TYPE_PLAYER);
        match_neighbor();
        match_absolute();
        match_player();
        if ((recipient = noisy_match_result()) == NOTHING)
        {
            return;
        }
    }
    else
    {
        if (mudconf.toad_recipient == -1)
        {
            recipient = player;
        }
        else
        {
            recipient =  mudconf.toad_recipient;
        }
    }

    STARTLOG(LOG_WIZARD, "WIZ", "TOAD");
    log_name_and_loc(victim);
    log_text(" was @toaded by ");
    log_name(player);
    ENDLOG;


    // Clear everything out.
    //
    if (key & TOAD_NO_CHOWN)
    {
        count = -1;
    }
    else
    {
        // You get it.
        //
        count = chown_all(victim, recipient, player, CHOWN_NOZONE);
        s_Owner(victim, recipient);
        s_Zone(victim, NOTHING);
    }
    s_Flags(victim, FLAG_WORD1, TYPE_THING | HALT);
    s_Flags(victim, FLAG_WORD2, 0);
    s_Flags(victim, FLAG_WORD3, 0);
    s_Pennies(victim, 1);

    // Notify people.
    //
    loc = Location(victim);
    buf = alloc_mbuf("do_toad");
    char *pVictimName = Name(victim);
    sprintf(buf, "%s has been turned into a slimy toad!", pVictimName);
    notify_except2(loc, player, victim, player, buf);
    sprintf(buf, "You toaded %s! (%d objects @chowned)", pVictimName, count+1);
    notify_quiet(player, buf);

    // Zap the name from the name hash table.
    //
    sprintf(buf, "a slimy toad named %s", pVictimName);
    delete_player_name(victim, pVictimName);
    s_Name(victim, buf);
    free_mbuf(buf);

    // Zap the alias, too.
    //
    buf = atr_pget(victim, A_ALIAS, &aowner, &aflags);
    delete_player_name(victim, buf);
    free_lbuf(buf);

    // Boot off.
    //
    count = boot_off(victim, "You have been turned into a slimy toad!");

    // Release comsys and @mail resources.
    //
    ReleaseAllResources(victim);

    buf = tprintf("%d connection%s closed.", count, (count == 1 ? "" : "s"));
    notify_quiet(player, buf);
}

void do_newpassword
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *password
)
{
    dbref victim;
    char *buf;

    if ((victim = lookup_player(player, name, 0)) == NOTHING)
    {
        notify_quiet(player, "No such player.");
        return;
    }
    if (*password != '\0' && !ok_password(password, player))
    {
        // Can set null passwords, but not bad passwords.
        // Notification of reason done by ok_password().
        //
        return;
    }
    if (God(victim))
    {
        notify_quiet(player, "You cannot change that player's password.");
        return;
    }
    STARTLOG(LOG_WIZARD, "WIZ", "PASS");
    log_name(player);
    log_text(" changed the password of ");
    log_name(victim);
    ENDLOG;

    // It's ok, do it.
    //
    s_Pass(victim, crypt((const char *)password, "XX"));
    notify_quiet(player, "Password changed.");
    buf = alloc_lbuf("do_newpassword");
    char *bp = buf;
    safe_tprintf_str(buf, &bp, "Your password has been changed by %s.", Name(player));
    notify_quiet(victim, buf);
    free_lbuf(buf);
}

void do_boot(dbref player, dbref cause, int key, char *name)
{
    dbref victim;
    char *buf, *bp;
    int count;

    if (!(Can_Boot(player)))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (key & BOOT_PORT)
    {
        if (is_number(name))
        {
            victim = Tiny_atol(name);
        }
        else
        {
            notify_quiet(player, "That's not a number!");
            return;
        }
        STARTLOG(LOG_WIZARD, "WIZ", "BOOT");
        buf = alloc_sbuf("do_boot.port");
        sprintf(buf, "Port %d", victim);
        log_text(buf);
        log_text(" was @booted by ");
        log_name(player);
        free_sbuf(buf);
        ENDLOG;
    }
    else
    {
        init_match(player, name, TYPE_PLAYER);
        match_neighbor();
        match_absolute();
        match_player();
        if ((victim = noisy_match_result()) == NOTHING)
        {
            return;
        }

        if (God(victim))
        {
            notify_quiet(player, "You cannot boot that player!");
            return;
        }
        if (  (  !isPlayer(victim)
              && !God(player)
              )
           || player == victim
           )
        {
            notify_quiet(player, "You can only boot off other players!");
            return;
        }
        STARTLOG(LOG_WIZARD, "WIZ", "BOOT");
        log_name_and_loc(victim);
        log_text(" was @booted by ");
        log_name(player);
        ENDLOG;
        notify_quiet(player, tprintf("You booted %s off!", Name(victim)));
    }
    if (key & BOOT_QUIET)
    {
        buf = NULL;
    }
    else
    {
        bp = buf = alloc_lbuf("do_boot.msg");
        safe_str(Name(player), buf, &bp);
        safe_str(" gently shows you the door.", buf, &bp);
        *bp = '\0';
    }

    if (key & BOOT_PORT)
    {
        count = boot_by_port(victim, !God(player), buf);
    }
    else
    {
        count = boot_off(victim, buf);
    }
    if (buf)
    {
        free_lbuf(buf);
    }
    buf = tprintf("%d connection%s closed.", count, (count == 1 ? "" : "s"));
    notify_quiet(player, buf);
}

// ---------------------------------------------------------------------------
// do_poor: Reduce the wealth of anyone over a specified amount.
//
void do_poor(dbref player, dbref cause, int key, char *arg1)
{
    dbref a;
    int amt, curamt;

    if (!is_number(arg1))
    {
        return;
    }
    amt = Tiny_atol(arg1);
    DO_WHOLE_DB(a)
    {
        if (isPlayer(a))
        {
            curamt = Pennies(a);
            if (amt < curamt)
            {
                s_Pennies(a, amt);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_cut: Chop off a contents or exits chain after the named item.
//
void do_cut(dbref player, dbref cause, int key, char *thing)
{
    dbref object;

    object = match_controlled(player, thing);
    switch (object)
    {
    case NOTHING:

        notify_quiet(player, "No match.");
        break;

    case AMBIGUOUS:

        notify_quiet(player, "I don't know which one");
        break;

    default:

        s_Next(object, NOTHING);
        notify_quiet(player, "Cut.");
    }
}

// --------------------------------------------------------------------------
// do_motd: Wizard-settable message of the day (displayed on connect)
//
void do_motd(dbref player, dbref cause, int key, char *message)
{
    int is_brief;

    is_brief = 0;
    if (key & MOTD_BRIEF)
    {
        is_brief = 1;
        key = key & ~MOTD_BRIEF;
        if (key == MOTD_ALL)
            key = MOTD_LIST;
        else if (key != MOTD_LIST)
            key |= MOTD_BRIEF;
    }
    switch (key)
    {
    case MOTD_ALL:

        strncpy(mudconf.motd_msg, message, GBUF_SIZE-1);
        mudconf.motd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(player))
        {
            notify_quiet(player, "Set: MOTD.");
        }
        break;

    case MOTD_WIZ:

        strncpy(mudconf.wizmotd_msg, message, GBUF_SIZE-1);
        mudconf.wizmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(player))
        {
            notify_quiet(player, "Set: Wizard MOTD.");
        }
        break;

    case MOTD_DOWN:

        strncpy(mudconf.downmotd_msg, message, GBUF_SIZE-1);
        mudconf.downmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(player))
        {
            notify_quiet(player, "Set: Down MOTD.");
        }
        break;

    case MOTD_FULL:

        strncpy(mudconf.fullmotd_msg, message, GBUF_SIZE-1);
        mudconf.fullmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(player))
        {
            notify_quiet(player, "Set: Full MOTD.");
        }
        break;

    case MOTD_LIST:

        if (Wizard(player))
        {
            if (!is_brief)
            {
                notify_quiet(player, "----- motd file -----");
                fcache_send(player, FC_MOTD);
                notify_quiet(player, "----- wizmotd file -----");
                fcache_send(player, FC_WIZMOTD);
                notify_quiet(player, "----- motd messages -----");
            }
            notify_quiet(player, tprintf("MOTD: %s", mudconf.motd_msg));
            notify_quiet( player,
                          tprintf("Wizard MOTD: %s", mudconf.wizmotd_msg) );
            notify_quiet( player,
                          tprintf("Down MOTD: %s", mudconf.downmotd_msg) );
            notify_quiet( player,
                          tprintf("Full MOTD: %s", mudconf.fullmotd_msg) );
        }
        else
        {
            if (Guest(player))
            {
                fcache_send(player, FC_CONN_GUEST);
            }
            else
            {
                fcache_send(player, FC_MOTD);
            }
            notify_quiet(player, mudconf.motd_msg);
        }
        break;

    default:

        notify_quiet(player, "Illegal combination of switches.");
    }
}

// ---------------------------------------------------------------------------
// do_enable: enable or disable global control flags
//
NAMETAB enable_names[] =
{
    {"building",        1,  CA_PUBLIC,  CF_BUILD},
    {"checkpointing",   2,  CA_PUBLIC,  CF_CHECKPOINT},
    {"cleaning",        2,  CA_PUBLIC,  CF_DBCHECK},
    {"dequeueing",      1,  CA_PUBLIC,  CF_DEQUEUE},
#ifdef MUSH3
    {"god_monitoring",  1,  CA_PUBLIC,  CF_GODMONITOR},
#endif
    {"idlechecking",    2,  CA_PUBLIC,  CF_IDLECHECK},
    {"interpret",       2,  CA_PUBLIC,  CF_INTERP},
    {"logins",          3,  CA_PUBLIC,  CF_LOGIN},
    {"guests",          2,  CA_PUBLIC,  CF_GUEST},
    {"eventchecking",   2,  CA_PUBLIC,  CF_EVENTCHECK},
    { NULL,             0,  0,          0}
};

void do_global(dbref player, dbref cause, int key, char *flag)
{
    int flagvalue;

    // Set or clear the indicated flag.
    //
    flagvalue = search_nametab(player, enable_names, flag);
    if (flagvalue < 0)
    {
        notify_quiet(player, "I don't know about that flag.");
    }
    else if (key == GLOB_ENABLE)
    {
        // Check for CF_DEQUEUE
        //
        if (flagvalue == CF_DEQUEUE)
        {
            scheduler.SetMinPriority(PRIORITY_CF_DEQUEUE_ENABLED);
        }
        mudconf.control_flags |= flagvalue;
        STARTLOG(LOG_CONFIGMODS, "CFG", "GLOBAL");
        log_name(player);
        log_text(" enabled: ");
        log_text(flag);
        ENDLOG;
        if (!Quiet(player))
        {
            notify_quiet(player, "Enabled.");
        }
    }
    else if (key == GLOB_DISABLE)
    {
        if (flagvalue == CF_DEQUEUE)
        {
            scheduler.SetMinPriority(PRIORITY_CF_DEQUEUE_DISABLED);
        }
        mudconf.control_flags &= ~flagvalue;
        STARTLOG(LOG_CONFIGMODS, "CFG", "GLOBAL");
        log_name(player);
        log_text(" disabled: ");
        log_text(flag);
        ENDLOG;
        if (!Quiet(player))
        {
            notify_quiet(player, "Disabled.");
        }
    }
    else
    {
        notify_quiet(player, "Illegal combination of switches.");
    }
}
