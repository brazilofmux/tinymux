// wiz.cpp -- Wizard-only commands.
//
// $Id: wiz.cpp,v 1.11 2004-05-20 04:31:19 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "file_c.h"
#include "powers.h"

void do_teleport
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    dbref victim, destination, loc;
    char *to;
    int hush = 0;

    if (  (  Fixed(executor)
          || Fixed(Owner(executor)))
       && !Tel_Anywhere(executor))
    {
        notify(executor, mudconf.fixed_tel_msg);
        return;
    }

    // Get victim.
    //
    if (nargs == 1)
    {
        victim = executor;
        to = arg1;
    }
    else if (nargs == 2)
    {
        init_match(executor, arg1, NOTYPE);
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
    if (  !Good_obj(victim)
       || isRoom(victim))
    {
        notify_quiet(executor, "You can't teleport that.");
        return;
    }

    // Fail if we don't control the victim or the victim's location.
    //
    if (  !Controls(executor, victim)
       && (  (  isExit(victim)
             && !Controls(executor, Home(victim)))
          || !Controls(executor, Location(victim)))
       && !Tel_Anything(executor))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // Check for teleporting home
    //
    if (!string_compare(to, "home"))
    {
        if (isExit(victim))
        {
            notify_quiet(executor, "Bad destination.");
        }
        else
        {
            move_via_teleport(victim, HOME, enactor, 0);
        }
        return;
    }

    // Find out where to send the victim.
    //
    init_match(executor, to, NOTYPE);
    match_everything(0);
    destination = match_result();

    switch (destination)
    {
    case NOTHING:

        notify_quiet(executor, "No match.");
        return;

    case AMBIGUOUS:

        notify_quiet(executor, "I don't know which destination you mean!");
        return;

    default:

        if (victim == destination)
        {
            notify_quiet(executor, "Bad destination.");
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
           || !( Controls(executor, loc)
              || Jump_ok(loc)
              || Tel_Anywhere(executor)))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            return;
        }
    }
    if (  isGarbage(destination)
       || (  Has_location(destination)
          && isGarbage(Location(destination))))
    {
        // @Teleporting into garbage is never permitted.
        //
        notify_quiet(executor, "Bad destination.");
        return;
    }
    else if (Has_contents(destination))
    {
        // You must control the destination, or it must be a JUMP_OK
        // room where you pass its TELEPORT lock.
        //
        if (  !(  Controls(executor, destination)
               || Jump_ok(destination)
               || Tel_Anywhere(executor))
           || !could_doit(executor, destination, A_LTPORT)
           || (  isExit(victim)
              && God(destination)
              && !God(executor)))
        {
            // Nope, report failure.
            //
            if (executor != victim)
            {
                notify_quiet(executor, NOPERM_MESSAGE);
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

        if (move_via_teleport(victim, destination, enactor, hush))
        {
            if (executor != victim)
            {
                if (!Quiet(executor))
                {
                    notify_quiet(executor, "Teleported.");
                }
            }
        }
    }
    else if (isExit(destination))
    {
        if (isExit(victim))
        {
            if (executor != victim)
            {
                notify_quiet(executor, "Bad destination.");
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
                move_exit(victim, destination, false, "You can't go that way.", 0);
            }
            else
            {
                notify_quiet(executor, "I can't find that exit.");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// do_force_prefixed: Interlude to do_force for the # command
//
void do_force_prefixed( dbref executor, dbref caller, dbref enactor, int key,
                        char *command, char *args[], int nargs )
{
    char *cp = parse_to(&command, ' ', 0);
    if (!command)
    {
        return;
    }
    while (mux_isspace(*command))
    {
        command++;
    }
    if (*command)
    {
        do_force(executor, caller, enactor, key, cp, command, args, nargs);
    }
}

// ---------------------------------------------------------------------------
// do_force: Force an object to do something.
//
void do_force( dbref executor, dbref caller, dbref enactor, int key,
               char *what, char *command, char *args[], int nargs )
{
    dbref victim = match_controlled(executor, what);
    if (victim != NOTHING)
    {
        // Force victim to do command.
        //
        CLinearTimeAbsolute lta;
        wait_que(victim, caller, executor, false, lta, NOTHING, 0, command,
            args, nargs, mudstate.global_regs);
    }
}

// ---------------------------------------------------------------------------
// do_toad: Turn a player into an object.
//
void do_toad
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *toad,
    char *newowner
)
{
    dbref victim, recipient, loc, aowner;
    char *buf;
    int count, aflags;

    init_match(executor, toad, TYPE_PLAYER);
    match_neighbor();
    match_absolute();
    match_player();
    if (!Good_obj(victim = noisy_match_result()))
    {
        return;
    }

    if (!isPlayer(victim))
    {
        notify_quiet(executor, "Try @destroy instead.");
        return;
    }
    if (No_Destroy(victim))
    {
        notify_quiet(executor, "You can't toad that player.");
        return;
    }
    if (  nargs == 2
       && *newowner )
    {
        init_match(executor, newowner, TYPE_PLAYER);
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
        if (mudconf.toad_recipient == NOTHING)
        {
            recipient = executor;
        }
        else
        {
            recipient = mudconf.toad_recipient;
        }
    }

    STARTLOG(LOG_WIZARD, "WIZ", "TOAD");
    log_name_and_loc(victim);
    log_text(" was @toaded by ");
    log_name(executor);
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
        count = chown_all(victim, recipient, executor, CHOWN_NOZONE);
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
    const char *pVictimName = Name(victim);
    sprintf(buf, "%s has been turned into a slimy toad!", pVictimName);
    notify_except2(loc, executor, victim, executor, buf);
    sprintf(buf, "You toaded %s! (%d objects @chowned)", pVictimName, count + 1);
    notify_quiet(executor, buf);

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
    notify_quiet(executor, buf);
}

void do_newpassword
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *password
)
{
    dbref victim = lookup_player(executor, name, false);
    if (victim == NOTHING)
    {
        notify_quiet(executor, "No such player.");
        return;
    }
    const char *pmsg;
    if (  *password != '\0'
       && !ok_password(password, &pmsg))
    {
        // Can set null passwords, but not bad passwords.
        //
        notify_quiet(executor, pmsg);
        return;
    }
    if (God(victim))
    {
        bool bCan = true;
        if (God(executor))
        {
            // God can change her own password if it's missing.
            //
            int   aflags;
            dbref aowner;
            char *target = atr_get(executor, A_PASS, &aowner, &aflags);
            if (target[0] != '\0')
            {
                bCan = false;
            }
            free_lbuf(target);
        }
        else
        {
            bCan = false;
        }
        if (!bCan)
        {
            notify_quiet(executor, "You cannot change that player's password.");
            return;
        }
    }
    STARTLOG(LOG_WIZARD, "WIZ", "PASS");
    log_name(executor);
    log_text(" changed the password of ");
    log_name(victim);
    ENDLOG;

    // It's ok, do it.
    //
    ChangePassword(victim, password);
    notify_quiet(executor, "Password changed.");
    char *buf = alloc_lbuf("do_newpassword");
    char *bp = buf;
    safe_tprintf_str(buf, &bp, "Your password has been changed by %s.", Name(executor));
    notify_quiet(victim, buf);
    free_lbuf(buf);
}

void do_boot(dbref executor, dbref caller, dbref enactor, int key, char *name)
{
    if (!Can_Boot(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    dbref victim;
    int count;
    if (key & BOOT_PORT)
    {
        if (is_integer(name, NULL))
        {
            victim = mux_atol(name);
        }
        else
        {
            notify_quiet(executor, "That's not a number!");
            return;
        }
        STARTLOG(LOG_WIZARD, "WIZ", "BOOT");
        log_text(tprintf("Port %d", victim));
        log_text(" was @booted by ");
        log_name(executor);
        ENDLOG;
    }
    else
    {
        init_match(executor, name, TYPE_PLAYER);
        match_neighbor();
        match_absolute();
        match_player();
        if ((victim = noisy_match_result()) == NOTHING)
        {
            return;
        }

        if (God(victim))
        {
            notify_quiet(executor, "You cannot boot that player!");
            return;
        }
        if (  (  !isPlayer(victim)
              && !God(executor))
           || executor == victim)
        {
            notify_quiet(executor, "You can only boot off other players!");
            return;
        }
        STARTLOG(LOG_WIZARD, "WIZ", "BOOT");
        log_name_and_loc(victim);
        log_text(" was @booted by ");
        log_name(executor);
        ENDLOG;
        notify_quiet(executor, tprintf("You booted %s off!", Name(victim)));
    }

    const char *buf;
    if (key & BOOT_QUIET)
    {
        buf = NULL;
    }
    else
    {
        buf = tprintf("%s gently shows you the door.", Name(executor));
    }

    if (key & BOOT_PORT)
    {
        count = boot_by_port(victim, God(executor), buf);
    }
    else
    {
        count = boot_off(victim, buf);
    }
    notify_quiet(executor, tprintf("%d connection%s closed.", count, (count == 1 ? "" : "s")));
}

// ---------------------------------------------------------------------------
// do_poor: Reduce the wealth of anyone over a specified amount.
//
void do_poor(dbref executor, dbref caller, dbref enactor, int key, char *arg1)
{
    if (!is_rational(arg1))
    {
        return;
    }

    int amt = mux_atol(arg1);
    int curamt;
    dbref a;
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
void do_cut(dbref executor, dbref caller, dbref enactor, int key, char *thing)
{
    dbref object = match_controlled(executor, thing);
    if (Good_obj(object))
    {
        s_Next(object, NOTHING);
        notify_quiet(executor, "Cut.");
    }
}

// --------------------------------------------------------------------------
// do_motd: Wizard-settable message of the day (displayed on connect)
//
void do_motd(dbref executor, dbref caller, dbref enactor, int key, char *message)
{
    bool is_brief = false;

    if (key & MOTD_BRIEF)
    {
        is_brief = true;
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
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Set: MOTD.");
        }
        break;

    case MOTD_WIZ:

        strncpy(mudconf.wizmotd_msg, message, GBUF_SIZE-1);
        mudconf.wizmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Set: Wizard MOTD.");
        }
        break;

    case MOTD_DOWN:

        strncpy(mudconf.downmotd_msg, message, GBUF_SIZE-1);
        mudconf.downmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Set: Down MOTD.");
        }
        break;

    case MOTD_FULL:

        strncpy(mudconf.fullmotd_msg, message, GBUF_SIZE-1);
        mudconf.fullmotd_msg[GBUF_SIZE-1] = '\0';
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Set: Full MOTD.");
        }
        break;

    case MOTD_LIST:

        if (Wizard(executor))
        {
            if (!is_brief)
            {
                notify_quiet(executor, "----- motd file -----");
                fcache_send(executor, FC_MOTD);
                notify_quiet(executor, "----- wizmotd file -----");
                fcache_send(executor, FC_WIZMOTD);
                notify_quiet(executor, "----- motd messages -----");
            }
            notify_quiet(executor, tprintf("MOTD: %s", mudconf.motd_msg));
            notify_quiet( executor,
                          tprintf("Wizard MOTD: %s", mudconf.wizmotd_msg) );
            notify_quiet( executor,
                          tprintf("Down MOTD: %s", mudconf.downmotd_msg) );
            notify_quiet( executor,
                          tprintf("Full MOTD: %s", mudconf.fullmotd_msg) );
        }
        else
        {
            if (Guest(executor))
            {
                fcache_send(executor, FC_CONN_GUEST);
            }
            else
            {
                fcache_send(executor, FC_MOTD);
            }
            notify_quiet(executor, mudconf.motd_msg);
        }
        break;

    default:

        notify_quiet(executor, "Illegal combination of switches.");
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
#endif // MUSH3
    {"idlechecking",    2,  CA_PUBLIC,  CF_IDLECHECK},
    {"interpret",       2,  CA_PUBLIC,  CF_INTERP},
    {"logins",          3,  CA_PUBLIC,  CF_LOGIN},
    {"guests",          2,  CA_PUBLIC,  CF_GUEST},
    {"eventchecking",   2,  CA_PUBLIC,  CF_EVENTCHECK},
    { NULL,             0,  0,          0}
};

void do_global(dbref executor, dbref caller, dbref enactor, int key, char *flag)
{
    // Set or clear the indicated flag.
    //
    int flagvalue;
    if (!search_nametab(executor, enable_names, flag, &flagvalue))
    {
        notify_quiet(executor, "I don't know about that flag.");
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
        log_name(executor);
        log_text(" enabled: ");
        log_text(flag);
        ENDLOG;
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Enabled.");
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
        log_name(executor);
        log_text(" disabled: ");
        log_text(flag);
        ENDLOG;
        if (!Quiet(executor))
        {
            notify_quiet(executor, "Disabled.");
        }
    }
    else
    {
        notify_quiet(executor, "Illegal combination of switches.");
    }
}
