// create.cpp -- Commands that create new objects.
//
// $Id: create.cpp,v 1.23 2006-01-31 00:16:23 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "powers.h"

// ---------------------------------------------------------------------------
// parse_linkable_room: Get a location to link to.
//
static dbref parse_linkable_room(dbref player, char *room_name)
{
    init_match(player, room_name, NOTYPE);
    match_everything(MAT_NO_EXITS | MAT_NUMERIC | MAT_HOME);
    dbref room = match_result();

    // HOME is always linkable
    //
    if (room == HOME)
    {
        return HOME;
    }

    // Make sure we can link to it
    //
    if (!Good_obj(room))
    {
        notify_quiet(player, "That's not a valid object.");
        return NOTHING;
    }
    else if (  !Has_contents(room)
            || !Linkable(player, room))
    {
        notify_quiet(player, "You can't link to that.");
        return NOTHING;
    }
    else
    {
        return room;
    }
}

// ---------------------------------------------------------------------------
// open_exit, do_open: Open a new exit and optionally link it somewhere.
//
static void open_exit(dbref player, dbref loc, char *direction, char *linkto)
{
    if (!Good_obj(loc))
    {
        return;
    }
    if (!direction || !*direction)
    {
        notify_quiet(player, "Open where?");
        return;
    }
    else if (!Controls(player, loc))
    {
        if(!(Open_ok(loc) && could_doit(player, loc, A_LOPEN)))
        {
            notify_quiet(player, NOPERM_MESSAGE);
            return;
        }
    }
    dbref exit = create_obj(player, TYPE_EXIT, direction, 0);
    if (exit == NOTHING)
    {
        return;
    }

    // Initialize everything and link it in.
    //
    s_Exits(exit, loc);
    s_Next(exit, Exits(loc));
    s_Exits(loc, exit);
    local_data_create(exit);

    // and we're done
    //
    notify_quiet(player, "Opened.");

    // See if we should do a link
    //
    if (!linkto || !*linkto)
    {
        return;
    }

    loc = parse_linkable_room(player, linkto);
    if (Good_obj(loc) || loc == HOME)
    {
        // Make sure the player passes the link lock
        //
        if (!could_doit(player, loc, A_LLINK))
        {
            notify_quiet(player, "You can't link to there.");
            return;
        }

        // Link it if the player can pay for it
        //
        if (!payfor(player, mudconf.linkcost))
        {
            notify_quiet(player,
                tprintf("You don't have enough %s to link.",
                    mudconf.many_coins));
        }
        else
        {
            s_Location(exit, loc);
            notify_quiet(player, "Linked.");
        }
    }
}

void do_open(dbref executor, dbref caller, dbref enactor, int key,
             char *direction, char *links[], int nlinks)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    char *dest;

    // Create the exit and link to the destination, if there is one
    //
    if (nlinks >= 1)
    {
        dest = links[0];
    }
    else
    {
        dest = NULL;
    }

    dbref loc;
    if (key == OPEN_INVENTORY)
    {
        loc = executor;
    }
    else
    {
        loc = Location(executor);
    }

    open_exit(executor, loc, direction, dest);

    // Open the back link if we can.
    //
    if (nlinks >= 2)
    {
        dbref destnum = parse_linkable_room(executor, dest);
        if (Good_obj(destnum) || destnum == HOME)
        {
            char buff[12];
            mux_ltoa(loc, buff);
            open_exit(executor, destnum, links[1], buff);
        }
    }
}

// ---------------------------------------------------------------------------
// link_exit, do_link: Set destination(exits), dropto(rooms) or
// home(player,thing)

static void link_exit(dbref player, dbref exit, dbref dest)
{
    // Make sure we can link there
    //
    if (  dest != HOME
       && (  (  !Controls(player, dest)
             && !Link_ok(dest))
          || !could_doit(player, dest, A_LLINK)))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return;
    }

    // Exit must be unlinked or controlled by you
    //
    if (  Location(exit) != NOTHING
       && !Controls(player, exit))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return;
    }

    // Handle costs
    //
    int cost = mudconf.linkcost;
    int quot = 0;
    if (Owner(exit) != Owner(player))
    {
        cost += mudconf.opencost;
        quot += mudconf.exit_quota;
    }
    if (!canpayfees(player, player, cost, quot))
    {
        return;
    }

    // Pay the owner for his loss.
    //
    if (Owner(exit) != Owner(player))
    {
        giveto(Owner(exit), mudconf.opencost);
        add_quota(Owner(exit), quot);
        s_Owner(exit, Owner(player));
        db[exit].fs.word[FLAG_WORD1] &= ~(INHERIT | WIZARD);
        db[exit].fs.word[FLAG_WORD1] |= HALT;
    }

    // Link has been validated and paid for, do it and tell the player
    //
    s_Location(exit, dest);
    if (!Quiet(player))
    {
        notify_quiet(player, "Linked.");
    }
}

void do_link
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *what,
    char *where
)
{
    UNUSED_PARAMETER(nargs);

    // Find the thing to link
    //
    init_match(executor, what, TYPE_EXIT);
    match_everything(0);
    dbref thing = noisy_match_result();
    if (thing == NOTHING)
    {
        return;
    }

    // Allow unlink if where is not specified
    //
    if (!where || !*where)
    {
        do_unlink(executor, caller, enactor, key, what);
        return;
    }

    dbref room;
    char *buff;

    switch (Typeof(thing))
    {
    case TYPE_EXIT:

        // Set destination
        //
        room = parse_linkable_room(executor, where);
        if (Good_obj(room) || room == HOME)
        {
            link_exit(executor, thing, room);
        }
        break;

    case TYPE_PLAYER:
    case TYPE_THING:

        // Set home.
        //
        if (!Controls(executor, thing))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            break;
        }
        init_match(executor, where, NOTYPE);
        match_everything(MAT_NO_EXITS);
        room = noisy_match_result();
        if (!Good_obj(room))
        {
            break;
        }
        if (!Has_contents(room))
        {
            notify_quiet(executor, "Can't link to an exit.");
            break;
        }
        if (  !can_set_home(executor, thing, room)
           || !could_doit(executor, room, A_LLINK))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
        }
        else if (room == HOME)
        {
            notify_quiet(executor, "Can't set home to home.");
        }
        else
        {
            dbref nHomeOrig = Home(thing);
            dbref nHomeNew  = room;
            s_Home(thing, nHomeNew);
            if (!Quiet(executor))
            {
                char *buff1 = alloc_lbuf("do_link.notify");
                char *bp = buff1;

                char *p;
                p = tprintf("Home of %s(#%d) changed from ", Name(thing), thing);
                safe_str(p, buff1, &bp);
                p = tprintf("%s(#%d) to ", Name(nHomeOrig), nHomeOrig);
                safe_str(p, buff1, &bp);
                p = tprintf("%s(#%d).", Name(nHomeNew), nHomeNew);
                safe_str(p, buff1, &bp);
                *bp = '\0';
                notify_quiet(executor, buff1);
                free_lbuf(buff1);
            }
        }
        break;

    case TYPE_ROOM:

        // Set dropto.
        //
        if (!Controls(executor, thing))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            break;
        }
        room = parse_linkable_room(executor, where);
        if (  !Good_obj(room)
            && room != HOME)
        {
            break;
        }

        if (  room != HOME
           && !isRoom(room))
        {
            notify_quiet(executor, "That is not a room!");
        }
        else if (  room != HOME
                && (  (  !Controls(executor, room)
                      && !Link_ok(room))
                   || !could_doit(executor, room, A_LLINK)))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
        }
        else
        {
            dbref nDroptoOrig = Dropto(thing);
            dbref nDroptoNew  = room;
            s_Dropto(thing, room);
            if (!Quiet(executor))
            {
                char *buff1 = alloc_lbuf("do_link2.notify");
                char *bp = buff1;

                char *p;
                p = tprintf("Dropto of %s(#%d) changed from ", Name(thing), thing);
                safe_str(p, buff1, &bp);
                p = tprintf("%s(#%d) to ", Name(nDroptoOrig), nDroptoOrig);
                safe_str(p, buff1, &bp);
                p = tprintf("%s(#%d).", Name(nDroptoNew), nDroptoNew);
                safe_str(p, buff1, &bp);
                *bp = '\0';
                notify_quiet(executor, buff1);
                free_lbuf(buff1);
            }
        }
        break;

    case TYPE_GARBAGE:

        notify_quiet(executor, NOPERM_MESSAGE);
        break;

    default:

        STARTLOG(LOG_BUGS, "BUG", "OTYPE");
        buff = alloc_mbuf("do_link.LOG.badtype");
        mux_sprintf(buff, MBUF_SIZE, "Strange object type: object #%d = %d",
            thing, Typeof(thing));
        log_text(buff);
        free_mbuf(buff);
        ENDLOG;
    }
}

// ---------------------------------------------------------------------------
// do_parent: Set an object's parent field.
//
void do_parent
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *tname,
    char *pname
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);

    dbref thing, parent, curr;
    int lev;

    // Get victim.
    //
    init_match(executor, tname, NOTYPE);
    match_everything(0);
    thing = noisy_match_result();
    if (!Good_obj(thing))
    {
        return;
    }

    // Make sure we can do it.
    //
    if (  Going(thing)
       || !Controls(executor, thing))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // Find out what the new parent is.
    //
    if (*pname)
    {
        init_match(executor, pname, Typeof(thing));
        match_everything(0);
        parent = noisy_match_result();
        if (!Good_obj(parent))
        {
            return;
        }

        // Make sure we have rights to set parent.
        //
        if (!Parentable(executor, parent))
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            return;
        }

        // Verify no recursive reference
        //
        ITER_PARENTS(parent, curr, lev)
        {
            if (curr == thing)
            {
                notify_quiet(executor, "You can't have yourself as a parent!");
                return;
            }
        }
    }
    else
    {
        parent = NOTHING;
    }

    s_Parent(thing, parent);
    if (!Quiet(thing) && !Quiet(executor))
    {
        if (parent == NOTHING)
            notify_quiet(executor, "Parent cleared.");
        else
            notify_quiet(executor, "Parent set.");
    }
}

// ---------------------------------------------------------------------------
// do_dig: Create a new room.
//
void do_dig(dbref executor, dbref caller, dbref enactor, int key, char *name,
            char *args[], int nargs)
{
    UNUSED_PARAMETER(caller);

    // we don't need to know player's location!  hooray!
    //
    if (!name || !*name)
    {
        notify_quiet(executor, "Dig what?");
        return;
    }
    dbref room = create_obj(executor, TYPE_ROOM, name, 0);
    if (room == NOTHING)
    {
        return;
    }

    local_data_create(room);
    notify(executor, tprintf("%s created as room #%d.", name, room));

    char *buff = alloc_sbuf("do_dig");
    if (  nargs >= 1
       && args[0]
       && *args[0])
    {
        mux_ltoa(room, buff);
        open_exit(executor, Location(executor), args[0], buff);
    }
    if (  nargs >= 2
       && args[1]
       && *args[1])
    {
        mux_ltoa(Location(executor), buff);
        open_exit(executor, room, args[1], buff);
    }
    free_sbuf(buff);
    if (key == DIG_TELEPORT)
    {
        (void)move_via_teleport(executor, room, enactor, 0);
    }
}

// ---------------------------------------------------------------------------
// do_create: Make a new object.
//
void do_create
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *coststr
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(key);

    int cost = 0;
    if (!name || !*name)
    {
        notify_quiet(executor, "Create what?");
        return;
    }
    else if (  nargs == 2
            && (cost = mux_atol(coststr)) < 0)
    {
        notify_quiet(executor, "You can't create an object for less than nothing!");
        return;
    }
    dbref thing = create_obj(executor, TYPE_THING, name, cost);
    if (thing == NOTHING)
    {
        return;
    }

    move_via_generic(thing, executor, NOTHING, 0);
    s_Home(thing, new_home(executor));
    if (!Quiet(executor))
    {
        notify(executor, tprintf("%s created as object #%d", Name(thing), thing));
    }

    local_data_create(thing);
}


// ---------------------------------------------------------------------------
// do_clone: Create a copy of an object.
//
void do_clone
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *arg2
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nargs);

    dbref clone, thing, new_owner, loc;
    FLAG rmv_flags;
    int cost;

    if ((key & CLONE_INVENTORY) || !Has_location(executor))
        loc = executor;
    else
        loc = Location(executor);

    if (!Good_obj(loc))
        return;

    init_match(executor, name, NOTYPE);
    match_everything(0);
    thing = noisy_match_result();
    if ((thing == NOTHING) || (thing == AMBIGUOUS))
    {
        return;
    }

    // Let players clone things set VISUAL. It's easier than retyping
    // in all that data.
    //
    if (!Examinable(executor, thing))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }
    if (isPlayer(thing))
    {
        notify_quiet(executor, "You cannot clone players!");
        return;
    }

    // You can only make a parent link to what you control.
    //
    if (  !Controls(executor, thing)
       && !Parent_ok(thing)
       && (key & CLONE_FROM_PARENT))
    {
        notify_quiet(executor,
              tprintf("You don't control %s, ignoring /parent.",
                  Name(thing)));
        key &= ~CLONE_FROM_PARENT;
    }

    // Determine the cost of cloning
    //
    new_owner = (key & CLONE_PRESERVE) ? Owner(thing) : Owner(executor);
    if (key & CLONE_SET_COST)
    {
        cost = mux_atol(arg2);
        if (cost < mudconf.createmin)
            cost = mudconf.createmin;
        if (cost > mudconf.createmax)
            cost = mudconf.createmax;
        arg2 = NULL;
    }
    else
    {
        cost = 1;
        switch (Typeof(thing))
        {
        case TYPE_THING:
            cost = OBJECT_DEPOSIT((mudconf.clone_copy_cost) ?
                          Pennies(thing) : 1);
            break;
        case TYPE_ROOM:
            cost = mudconf.digcost;
            break;
        case TYPE_EXIT:

            if (!Controls(executor, loc))
            {
                notify_quiet(executor, NOPERM_MESSAGE);
                return;
            }
            cost = mudconf.digcost;
            break;
        }
    }

    // Go make the clone object.
    //
    bool bValid;
    size_t nValidName;
    char *pValidName = MakeCanonicalObjectName(arg2, &nValidName, &bValid);
    const char *clone_name;
    if (bValid)
    {
        clone_name = pValidName;
    }
    else
    {
        clone_name = Name(thing);
    }
    clone = create_obj(new_owner, Typeof(thing), clone_name, cost);

    if (clone == NOTHING)
    {
        return;
    }

    // Copy in the new data.
    //
    if (key & CLONE_FROM_PARENT)
    {
        s_Parent(clone, thing);
    }
    else
    {
        atr_cpy(clone, thing, false);
    }

    // Reset the name, since we cleared the attributes.
    //
    s_Name(clone, clone_name);

    // Reset the pennies, since it looks like we stamped on that, too.
    //
    s_Pennies(clone, OBJECT_ENDOWMENT(cost));

    // Clear out problem flags from the original
    //
    rmv_flags = WIZARD;
    if (!(key & CLONE_INHERIT) || !Inherits(executor))
    {
        rmv_flags |= INHERIT | IMMORTAL;
    }
    s_Flags(clone, FLAG_WORD1, Flags(thing) & ~rmv_flags);

    // Tell creator about it
    //
    if (!Quiet(executor))
    {
        if (arg2 && *arg2)
        {
            notify(executor,
             tprintf("%s cloned as %s, new copy is object #%d.",
                 Name(thing), arg2, clone));
        }
        else
        {
            notify(executor,
                   tprintf("%s cloned, new copy is object #%d.",
                       Name(thing), clone));
        }
    }

    // Put the new thing in its new home.  Break any dropto or link, then
    // try to re-establish it.
    //
    switch (Typeof(thing))
    {
    case TYPE_THING:

        s_Home(clone, clone_home(executor, thing));
        move_via_generic(clone, loc, executor, 0);
        break;

    case TYPE_ROOM:

        s_Dropto(clone, NOTHING);
        if (Dropto(thing) != NOTHING)
        {
            link_exit(executor, clone, Dropto(thing));
        }
        break;

    case TYPE_EXIT:

        s_Exits(loc, insert_first(Exits(loc), clone));
        s_Exits(clone, loc);
        s_Location(clone, NOTHING);
        if (Location(thing) != NOTHING)
        {
            link_exit(executor, clone, Location(thing));
        }
        break;
    }

    // If same owner run ACLONE, else halt it.  Also copy parent if we can.
    //
    if (new_owner == Owner(thing))
    {
        if (!(key & CLONE_FROM_PARENT))
        {
            s_Parent(clone, Parent(thing));
        }
        did_it(executor, clone, 0, NULL, 0, NULL, A_ACLONE,
               (char **)NULL, 0);
    }
    else
    {
        if (  !(key & CLONE_FROM_PARENT)
           && (Controls(executor, thing) || Parent_ok(thing)))
        {
            s_Parent(clone, Parent(thing));
        }
        s_Halted(clone);
    }

    local_data_clone(clone, thing);
}

// ---------------------------------------------------------------------------
// do_pcreate: Create new players and robots.
//
void do_pcreate
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *pass
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nargs);

    const char *pmsg;
    bool isrobot = (key == PCRE_ROBOT);
    dbref newplayer = create_player(name, pass, executor, isrobot, &pmsg);
    if (newplayer == NOTHING)
    {
        notify_quiet(executor, tprintf("Failure creating '%s'.  %s", name, pmsg));
        return;
    }
    AddToPublicChannel(newplayer);
    if (isrobot)
    {
        move_object(newplayer, Location(executor));
        notify_quiet(executor,
            tprintf("New robot '%s' (#%d) created with password '%s'",
                name, newplayer, pass));
        notify_quiet(executor, "Your robot has arrived.");
        STARTLOG(LOG_PCREATES, "CRE", "ROBOT");
        log_name(newplayer);
        log_text(" created by ");
        log_name(executor);
        ENDLOG;
    }
    else
    {
        move_object(newplayer, mudconf.start_room);
        notify_quiet(executor,
               tprintf("New player '%s' (#%d) created with password '%s'",
                   name, newplayer, pass));
        STARTLOG(LOG_PCREATES | LOG_WIZARD, "WIZ", "PCREA");
        log_name(newplayer);
        log_text(" created by ");
        log_name(executor);
        ENDLOG;
#ifdef GAME_DOOFERMUX
        // Added by D.Piper (del@doofer.org) 2000-APR
        //
        atr_add_raw(newplayer, A_REGINFO, "*Requires Registration*");
#endif
    }
}

// ---------------------------------------------------------------------------
// destroyable: Indicates if target of a @destroy is a 'special' object in
// the database.
//
static bool destroyable(dbref victim)
{
    if (  (victim == mudconf.default_home)
       || (victim == mudconf.start_home)
       || (victim == mudconf.start_room)
       || (victim == mudconf.master_room)
       || (victim == (dbref) 0)
       || (God(victim)))
    {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// can_destroy_player, do_destroy: Destroy things.
//
static bool can_destroy_player(dbref player, dbref victim)
{
    if (!Wizard(player))
    {
        notify_quiet(player, "Sorry, no suicide allowed.");
        return false;
    }
    if (Wizard(victim))
    {
        notify_quiet(player, "Even you can't do that!");
        return false;
    }
    return true;
}

void do_destroy(dbref executor, dbref caller, dbref enactor, int key, char *what)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);

    // You can destroy anything you control.
    //
    dbref thing = match_controlled_quiet(executor, what);

    // If you own a location, you can destroy its exits.
    //
    if (  thing == NOTHING
       && Controls(executor, Location(executor)))
    {
        init_match(executor, what, TYPE_EXIT);
        match_exit();
        thing = last_match_result();
    }

    // You may destroy DESTROY_OK things in your inventory.
    //
    if (thing == NOTHING)
    {
        init_match(executor, what, TYPE_THING);
        match_possession();
        thing = last_match_result();
        if ( thing != NOTHING
           && !(isThing(thing) && Destroy_ok(thing)))
        {
            thing = NOPERM;
        }
    }

    // Return an error if we didn't find anything to destroy.
    //
    if (match_status(executor, thing) == NOTHING)
    {
        return;
    }

    // Check SAFE and DESTROY_OK flags.
    //
    if (  Safe(thing, executor)
       && !(key & DEST_OVERRIDE)
       && !(isThing(thing) && Destroy_ok(thing)))
    {
        notify_quiet(executor, "Sorry, that object is protected.  Use @destroy/override to destroy it.");
        return;
    }

    // Make sure we're not trying to destroy a special object.
    //
    if (!destroyable(thing))
    {
        notify_quiet(executor, "You can't destroy that!");
        return;
    }

    // Make sure we can do it, on a type-specific basis.
    //
    if(  isPlayer(thing)
      && !can_destroy_player(executor, thing))
    {
        return;
    }

    char *NameOfType = alloc_sbuf("do_destroy.NameOfType");
    mux_strncpy(NameOfType, object_types[Typeof(thing)].name, SBUF_SIZE-1);
    mux_strlwr(NameOfType);
    if (Going(thing))
    {
        if (!mudconf.destroy_going_now)
        {
            notify_quiet(executor, tprintf("No sense beating a dead %s.", NameOfType));
            free_sbuf(NameOfType);
            return;
        }
        key |= DEST_INSTANT;
    }

    // Check whether we should perform instant destruction.
    //
    dbref ThingOwner = Owner(thing);
    bool bInstant = (key & DEST_INSTANT) || Destroy_ok(thing) || Destroy_ok(ThingOwner);

    if (!bInstant)
    {
        // Pre-destruction 'crumble' emits and one last possible showstopper.
        //
        switch (Typeof(thing))
        {
        case TYPE_ROOM:
            notify_all(thing, executor, "The room shakes and begins to crumble.");
            break;

        case TYPE_PLAYER:
            atr_add_raw(thing, A_DESTROYER, mux_ltoa_t(executor));
            if (!atr_get_raw(thing, A_DESTROYER))
            {
                // Not a likely situation, but the player has too many
                // attributes to remember it's destroyer, so we we need to
                // take care of this more immediately.
                //
                bInstant = true;
                notify(executor, "Player has a lot of attributes. Performing destruction immediately.");
                break;
            }

            // FALL THROUGH

        case TYPE_EXIT:
        case TYPE_THING:
            notify(executor, tprintf("The %s shakes and begins to crumble.",
                NameOfType));
            break;

        default:
            notify(executor, "Weird object type cannot be destroyed.");
            free_sbuf(NameOfType);
            return;
        }

        if (  !bInstant
           && !Quiet(thing)
           && !Quiet(ThingOwner))
        {
            notify_quiet(ThingOwner,
                tprintf("You will be rewarded shortly for %s(#%d).",
                Moniker(thing), thing));
        }
    }
    free_sbuf(NameOfType);

    // Imperative Destruction emits.
    //
    if (!Quiet(executor))
    {
        if (Good_owner(ThingOwner))
        {
            if (ThingOwner == Owner(executor))
            {
                if (!Quiet(thing))
                {
                    notify(executor, tprintf("Destroyed %s(#%d).",
                        Moniker(thing), thing));
                }
            }
            else if (ThingOwner == thing)
            {
                notify(executor, tprintf("Destroyed %s(#%d).",
                    Moniker(thing), thing));
            }
            else
            {
                char *tname = alloc_lbuf("destroy_obj");
                mux_strncpy(tname, Moniker(ThingOwner), LBUF_SIZE-1);
                notify(executor, tprintf("Destroyed %s's %s(#%d).",
                    tname, Moniker(thing), thing));
                free_lbuf(tname);
            }
        }
    }

    if (bInstant)
    {
        // Instant destruction by type.
        //
        switch (Typeof(thing))
        {
        case TYPE_EXIT:
            destroy_exit(thing);
            break;

        case TYPE_PLAYER:
            destroy_player(executor, thing);
            break;

        case TYPE_ROOM:
            empty_obj(thing);
            destroy_obj(thing);
            break;

        case TYPE_THING:
            destroy_thing(thing);
            break;

        default:
            notify(executor, "Weird object type cannot be destroyed.");
            return;
        }
    }
    s_Going(thing);
}
