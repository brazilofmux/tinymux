// set.cpp -- Commands which set parameters.
//
// $Id: set.cpp,v 1.19 2002-02-24 21:04:27 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "match.h"
#include "powers.h"
#include "attrs.h"
#include "ansi.h"

extern NAMETAB indiv_attraccess_nametab[];

dbref match_controlled(dbref player, const char *name)
{
    dbref mat;

    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    mat = noisy_match_result();
    if (Good_obj(mat) && !Controls(player, mat))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return NOTHING;
    }
    else
    {
        return mat;
    }
}

dbref match_controlled_quiet(dbref player, const char *name)
{
    dbref mat;

    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    mat = match_result();
    if (Good_obj(mat) && !Controls(player, mat)) {
        return NOTHING;
    } else {
        return (mat);
    }
}

dbref match_affected(dbref player, const char *name)
{
    dbref mat;

    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    mat = noisy_match_result();
    if (mat != NOTHING && !Affects(player, mat))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return NOTHING;
    }
    else
    {
        return mat;
    }
}

dbref match_examinable(dbref player, const char *name)
{
    dbref mat;

    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    mat = noisy_match_result();
    if (mat != NOTHING && !Examinable(player, mat))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return NOTHING;
    }
    else
    {
        return mat;
    }
}


void do_chzone
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *newobj
)
{
    if (!mudconf.have_zones)
    {
        notify(player, "Zones disabled.");
        return;
    }
    init_match(player, name, NOTYPE);
    match_everything(0);
    dbref thing = noisy_match_result();
    if (thing == NOTHING)
    {
        return;
    }

    dbref zone;
    if (!_stricmp(newobj, "none"))
    {
        zone = NOTHING;
    }
    else
    {
        init_match(player, newobj, NOTYPE);
        match_everything(0);
        zone = noisy_match_result();
        if (zone == NOTHING)
        {
            return;
        }
        if (  !isThing(zone)
           && !isRoom(zone))
        {
            notify(player, "Invalid zone object type.");
            return;
        }
    }

    if (  !Wizard(player)
       && !Controls(player, thing)
       && !check_zone_for_player(player, thing)
       && db[player].owner != db[thing].owner)
    {
        notify(player, "You don't have the power to shift reality.");
        return;
    }

    // A player may change an object's zone to NOTHING or to an object he owns.
    //
    if (  zone != NOTHING
       && !Wizard(player)
       && !Controls(player, zone)
       && db[player].owner != db[zone].owner)
    {
        notify(player, "You cannot move that object to that zone.");
        return;
    }

    // Only rooms may be zoned to other rooms.
    //
    if (  zone != NOTHING
       && isRoom(zone)
       && !isRoom(thing))
    {
        notify(player, "Only rooms may have parent rooms.");
        return;
    }

    // Everything is okay, do the change.
    //
    db[thing].zone = zone;
    if (!isPlayer(thing))
    {
        // If the object is a player, resetting these flags is rather
        // inconvenient -- although this may pose a bit of a security risk. Be
        // careful when @chzone'ing wizard or royal players.
        //
        Flags(thing) &= ~(WIZARD | ROYALTY | INHERIT);

#ifdef USE_POWERS
        // Wipe out all powers.
        //
        Powers(thing) = 0;
#endif // USE_POWERS
    }
    notify(player, "Zone changed.");
}

void do_name
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *newname
)
{
    char *buff;

    dbref thing = match_controlled(player, name);
    if (thing == NOTHING)
    {
        return;
    }

    // Check for bad name.
    //
    if (  nargs < 2
       || newname[0] == '\0')
    {
        notify_quiet(player, "Give it what new name?");
        return;
    }

    // Check for renaming a player.
    //
    if (isPlayer(thing))
    {
        buff = trim_spaces(newname);
        if (  !ValidatePlayerName(buff)
           || !badname_check(buff))
        {
            notify_quiet(player, "You can't use that name.");
            free_lbuf(buff);
            return;
        }
        else if (  string_compare(buff, Name(thing))
                && lookup_player(NOTHING, buff, 0) != NOTHING)
        {
            // string_compare allows changing foo to Foo, etc.
            //
            notify_quiet(player, "That name is already in use.");
            free_lbuf(buff);
            return;
        }

        // Everything ok, notify.
        //
        STARTLOG(LOG_SECURITY, "SEC", "CNAME");
        log_name(thing),
        log_text(" renamed to ");
        log_text(buff);
        ENDLOG;
        if (Suspect(thing))
        {
            raw_broadcast(WIZARD, "[Suspect] %s renamed to %s", Name(thing), buff);
        }
        delete_player_name(thing, Name(thing));
        s_Name(thing, buff);
        add_player_name(thing, Name(thing));
        if (!Quiet(player) && !Quiet(thing))
        {
            notify_quiet(player, "Name set.");
        }
        free_lbuf(buff);
        return;
    }
    else
    {
        int nValidName;
        BOOL bValid;
        char *pValidName = MakeCanonicalObjectName(newname, &nValidName, &bValid);
        if (!bValid)
        {
            notify_quiet(player, "That is not a reasonable name.");
            return;
        }

        // Everything ok, change the name.
        //
        s_Name(thing, pValidName);
        if (!Quiet(player) && !Quiet(thing))
        {
            notify_quiet(player, "Name set.");
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_alias: Make an alias for a player or object.
 */

void do_alias
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *alias
)
{
    dbref aowner;
    int aflags;
    ATTR *ap;
    char *oldalias, *trimalias;

    dbref thing = match_controlled(player, name);
    if (thing == NOTHING)
    {
        return;
    }

    // Check for renaming a player.
    //
    ap = atr_num(A_ALIAS);
    if (isPlayer(thing))
    {
        // Fetch the old alias.
        //
        oldalias = atr_pget(thing, A_ALIAS, &aowner, &aflags);
        trimalias = trim_spaces(alias);

        if (!Controls(player, thing))
        {
            // Make sure we have rights to do it. We can't do the
            // normal Set_attr check because ALIAS is only
            // writable by GOD and we want to keep people from
            // doing &ALIAS and bypassing the player name checks.
            //
            notify_quiet(player, NOPERM_MESSAGE);
        }
        else if (!*trimalias)
        {
            // New alias is null, just clear it.
            //
            delete_player_name(thing, oldalias);
            atr_clr(thing, A_ALIAS);
            if (!Quiet(player))
            {
                notify_quiet(player, "Alias removed.");
            }
        }
        else if (lookup_player(NOTHING, trimalias, 0) != NOTHING)
        {
            // Make sure new alias isn't already in use.
            //
            notify_quiet(player, "That name is already in use.");
        }
        else if (  !(badname_check(trimalias)
                && ValidatePlayerName(trimalias)))
        {
            notify_quiet(player, "That's a silly name for a player!");
        }
        else
        {
            // Remove the old name and add the new name.
            //
            delete_player_name(thing, oldalias);
            atr_add(thing, A_ALIAS, trimalias, Owner(player), aflags);
            if (add_player_name(thing, trimalias))
            {
                if (!Quiet(player))
                    notify_quiet(player, "Alias set.");
            }
            else
            {
                notify_quiet(player,
                         "That name is already in use or is illegal, alias cleared.");
                atr_clr(thing, A_ALIAS);
            }
        }
        free_lbuf(trimalias);
        free_lbuf(oldalias);
    }
    else
    {
        atr_pget_info(thing, A_ALIAS, &aowner, &aflags);

        // Make sure we have rights to do it.
        //
        if (!Set_attr(player, thing, ap, aflags))
        {
            notify_quiet(player, NOPERM_MESSAGE);
        }
        else
        {
            atr_add(thing, A_ALIAS, alias, Owner(player), aflags);
            if (!Quiet(player))
            {
                notify_quiet(player, "Set.");
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_lock: Set a lock on an object or attribute.
 */

void do_lock
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *keytext
)
{
    dbref thing, aowner;
    int atr, aflags;
    ATTR *ap;
    struct boolexp *okey;

    if (parse_attrib(player, name, &thing, &atr))
    {
        if (atr != NOTHING)
        {
            if (!atr_get_info(thing, atr, &aowner, &aflags))
            {
                notify_quiet(player, "Attribute not present on object.");
                return;
            }
            ap = atr_num(atr);

            // You may lock an attribute if: you could write the
            // attribute if it were stored on yourself --and-- you
            // own the attribute or are a wizard as  long as you are
            // not #1 and are trying to do something to #1.
            //
            if (  ap
               && (  God(player)
                  || (  !God(thing)
                     && Set_attr(player, player, ap, 0)
                     && (  Wizard(player)
                        || (aowner == Owner(player))))))
            {
                aflags |= AF_LOCK;
                atr_set_flags(thing, atr, aflags);
                if (!Quiet(player) && !Quiet(thing))
                {
                    notify_quiet(player, "Attribute locked.");
                }
            }
            else
            {
                notify_quiet(player, NOPERM_MESSAGE);
            }
            return;
        }
    }
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    thing = match_result();

    switch (thing)
    {
    case NOTHING:
        notify_quiet(player, "I don't see what you want to lock!");
        return;

    case AMBIGUOUS:
        notify_quiet(player, "I don't know which one you want to lock!");
        return;

    default:
        if (!controls(player, thing))
        {
            notify_quiet(player, "You can't lock that!");
            return;
        }
    }

    char *pRestrictedKeyText = RemoveSetOfCharacters(keytext, "\r\n\t");
    okey = parse_boolexp(player, pRestrictedKeyText, 0);
    if (okey == TRUE_BOOLEXP)
    {
        notify_quiet(player, "I don't understand that key.");
    }
    else
    {
        // Everything ok, do it.
        //
        if (!key)
        {
            key = A_LOCK;
        }
        atr_add_raw(thing, key, unparse_boolexp_quiet(player, okey));
        if (!Quiet(player) && !Quiet(thing))
        {
            notify_quiet(player, "Locked.");
        }
    }
    free_boolexp(okey);
}

/*
 * ---------------------------------------------------------------------------
 * * Remove a lock from an object of attribute.
 */

void do_unlock(dbref player, dbref cause, int key, char *name)
{
    dbref thing, aowner;
    int atr, aflags;
    ATTR *ap;

    if (  parse_attrib(player, name, &thing, &atr)
       && atr != NOTHING)
    {
        // We have been asked to change the ownership of an attribute.
        //
        if (!atr_get_info(thing, atr, &aowner, &aflags))
        {
            notify_quiet(player, "Attribute not present on object.");
            return;
        }
        ap = atr_num(atr);

        // You may unlock an attribute if: you could write the attribute
        // if it were stored on yourself --and-- you own the attribute or
        // are a wizard as long as you are not #1 and are trying to do
        // something to #1.
        //
        if (  ap
           && (  God(player)
              || (  !God(thing)
                 && Set_attr(player, player, ap, 0)
                 && (  Wizard(player)
                    || aowner == Owner(player)))))
        {
            aflags &= ~AF_LOCK;
            atr_set_flags(thing, atr, aflags);
            if (  Owner(player) != Owner(thing)
               && !Quiet(player)
               && !Quiet(thing))
            {
                notify_quiet(player, "Attribute unlocked.");
            }
        }
        else
        {
            notify_quiet(player, NOPERM_MESSAGE);
        }
        return;
    }

    // We have been asked to change the ownership of an object.
    //
    if (!key)
    {
        key = A_LOCK;
    }
    thing = match_controlled(player, name);
    if (thing != NOTHING)
    {
        atr_clr(thing, key);
        if (!Quiet(player) && !Quiet(thing))
        {
            notify_quiet(player, "Unlocked.");
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_unlink: Unlink an exit from its destination or remove a dropto.
 */

void do_unlink(dbref player, dbref cause, int key, char *name)
{
    dbref exit;

    init_match(player, name, TYPE_EXIT);
    match_everything(0);
    exit = match_result();

    switch (exit)
    {
    case NOTHING:

        notify_quiet(player, "Unlink what?");
        break;

    case AMBIGUOUS:

        notify_quiet(player, "I don't know which one you mean!");
        break;

    default:

        if (!controls(player, exit))
        {
            notify_quiet(player, NOPERM_MESSAGE);
        }
        else
        {
            switch (Typeof(exit))
            {
            case TYPE_EXIT:

                s_Location(exit, NOTHING);
                if (!Quiet(player))
                {
                    notify_quiet(player, "Unlinked.");
                }
                giveto(Owner(exit), mudconf.linkcost);
                break;

            case TYPE_ROOM:

                s_Dropto(exit, NOTHING);
                if (!Quiet(player))
                {
                    notify_quiet(player, "Dropto removed.");
                }
                break;

            default:

                notify_quiet(player, "You can't unlink that!");
                break;
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_chown: Change ownership of an object or attribute.
 */

void do_chown
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *newown
)
{
    dbref nOwnerOrig;
    dbref nOwnerNew;
    int do_it, cost, quota;
    ATTR *ap;

    dbref thing;
    int   atr;
    if (  parse_attrib(player, name, &thing, &atr)
       && atr != NOTHING)
    {
        // An attribute was given, so we worry about changing the owner of the
        // attribute.
        //
        if (isGarbage(thing))
        {
            notify_quiet(player, "You shouldn't be rummaging through the garbage.");
            return;
        }
        nOwnerOrig = Owner(thing);
        if (!*newown)
        {
            nOwnerNew = nOwnerOrig;
        }
        else if (!(string_compare(newown, "me")))
        {
            nOwnerNew = Owner(player);
        }
        else
        {
            nOwnerNew = lookup_player(player, newown, 1);
        }

        // You may chown an attr to yourself if you own the object and the attr
        // is not locked. You may chown an attr to the owner of the object if
        // you own the attribute. To do anything else you must be a wizard.
        // Only #1 can chown attributes on #1.
        //
        dbref aowner;
        int   aflags;
        if (!atr_get_info(thing, atr, &aowner, &aflags))
        {
            notify_quiet(player, "Attribute not present on object.");
            return;
        }
        do_it = 0;
        if (nOwnerNew == NOTHING)
        {
            notify_quiet(player, "I couldn't find that player.");
        }
        else if (God(thing) && !God(player))
        {
            notify_quiet(player, NOPERM_MESSAGE);
        }
        else if (Wizard(player))
        {
            do_it = 1;
        }
        else if (nOwnerNew == Owner(player))
        {
            // Chown to me: only if I own the obj and !locked
            //
            if (  !Controls(player, thing)
               || (aflags & AF_LOCK))
            {
                notify_quiet(player, NOPERM_MESSAGE);
            }
            else
            {
                do_it = 1;
            }
        }
        else if (nOwnerNew == nOwnerOrig)
        {
            // chown to obj owner: only if I own attr and !locked
            //
            if (  Owner(player) != aowner
               || (aflags & AF_LOCK))
            {
                notify_quiet(player, NOPERM_MESSAGE);
            }
            else
            {
                do_it = 1;
            }
        }
        else
        {
            notify_quiet(player, NOPERM_MESSAGE);
        }

        if (!do_it)
        {
            return;
        }

        ap = atr_num(atr);
        if (  !ap
           || !Set_attr(player, player, ap, aflags))
        {
            notify_quiet(player, NOPERM_MESSAGE);
            return;
        }
        atr_set_owner(thing, atr, nOwnerNew);
        if (!Quiet(player))
        {
            notify_quiet(player, "Attribute owner changed.");
        }
        return;
    }

    // An attribute was not specified, so we are being asked to change the
    // owner of the object.
    //
    init_match(player, name, TYPE_THING);
    match_possession();
    match_here();
    match_exit();
    match_me();
    if (Chown_Any(player))
    {
        match_player();
        match_absolute();
    }
    switch (thing = match_result())
    {
    case NOTHING:

        notify_quiet(player, "You don't have that!");
        return;

    case AMBIGUOUS:

        notify_quiet(player, "I don't know which you mean!");
        return;
    }
    nOwnerOrig = Owner(thing);

    if (!*newown || !(string_compare(newown, "me")))
    {
        nOwnerNew = Owner(player);
    }
    else
    {
        nOwnerNew = lookup_player(player, newown, 1);
    }

    cost = 1;
    quota = 1;
    switch (Typeof(thing))
    {
    case TYPE_ROOM:

        cost = mudconf.digcost;
        quota = mudconf.room_quota;
        break;

    case TYPE_THING:

        cost = OBJECT_DEPOSIT(Pennies(thing));
        quota = mudconf.thing_quota;
        break;

    case TYPE_EXIT:

        cost = mudconf.opencost;
        quota = mudconf.exit_quota;
        break;

    case TYPE_PLAYER:

        cost = mudconf.robotcost;
        quota = mudconf.player_quota;
        break;
    }

    BOOL bPlayerControlsThing = controls(player, thing);
    if (  isGarbage(thing)
       && bPlayerControlsThing)
    {
        notify_quiet(player, "You shouldn't be rummaging through the garbage.");
    }
    else if (nOwnerNew == NOTHING)
    {
        notify_quiet(player, "I couldn't find that player.");
    }
    else if (  isPlayer(thing)
            && !God(player))
    {
        notify_quiet(player, "Players always own themselves.");
    }
    else if (  (  !bPlayerControlsThing
               && !Chown_Any(player)
               && !Chown_ok(thing))
            || (  isThing(thing)
               && Location(thing) != player
               && !Chown_Any(player))
            || !controls(player, nOwnerNew))
    {
        notify_quiet(player, NOPERM_MESSAGE);
    }
    else if (canpayfees(player, nOwnerNew, cost, quota))
    {
        giveto(nOwnerOrig, cost);
        if (mudconf.quotas)
        {
            add_quota(nOwnerOrig, quota);
        }
        if (!God(player))
        {
            nOwnerNew = Owner(nOwnerNew);
        }
        s_Owner(thing, nOwnerNew);
        atr_chown(thing);
        db[thing].fs.word[FLAG_WORD1] &= ~(CHOWN_OK | INHERIT);
        db[thing].fs.word[FLAG_WORD1] |= HALT;
        s_Powers(thing, 0);
        s_Powers2(thing, 0);
        halt_que(NOTHING, thing);
        if (!Quiet(player))
        {
            char *buff = alloc_lbuf("do_chown.notify");
            char *bp = buff;

            char *p;
            p = tprintf("Owner of %s(#%d) changed from ", Name(thing), thing);
            safe_str(p, buff, &bp);
            p = tprintf("%s(#%d) to ", Name(nOwnerOrig), nOwnerOrig);
            safe_str(p, buff, &bp);
            p = tprintf("%s(#%d).", Name(nOwnerNew), nOwnerNew);
            safe_str(p, buff, &bp);
            notify_quiet(player, buff);
            free_lbuf(buff);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_set: Set flags or attributes on objects, or flags on attributes.
 */

static void set_attr_internal(dbref player, dbref thing, int attrnum, char *attrtext, int key)
{
    dbref aowner;
    int aflags, could_hear;
    ATTR *attr;

    attr = atr_num(attrnum);
    atr_pget_info(thing, attrnum, &aowner, &aflags);
    if (attr && Set_attr(player, thing, attr, aflags)) {
        if ((attr->check != NULL) &&
            (!(*attr->check) (0, player, thing, attrnum, attrtext)))
            return;
        could_hear = Hearer(thing);
        atr_add(thing, attrnum, attrtext, Owner(player), aflags);
        handle_ears(thing, could_hear, Hearer(thing));
        if (!(key & SET_QUIET) && !Quiet(player) && !Quiet(thing))
            notify_quiet(player, "Set.");
    } else {
        notify_quiet(player, NOPERM_MESSAGE);
    }
}

void do_set
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *flag
)
{
    dbref thing, thing2, aowner;
    char *p, *buff;
    int atr, atr2, aflags, clear, flagvalue, could_hear;
    ATTR *attr, *attr2;

    // See if we have the <obj>/<attr> form, which is how you set
    // attribute flags.
    //
    if (parse_attrib(player, name, &thing, &atr))
    {
        if (atr != NOTHING)
        {
            // You must specify a flag name.
            //
            if (!flag || !*flag)
            {
                notify_quiet(player, "I don't know what you want to set!");
                return;
            }

            // Check for clearing.
            //
            clear = 0;
            if (*flag == NOT_TOKEN)
            {
                flag++;
                clear = 1;
            }

            // Make sure player specified a valid attribute flag.
            //
            flagvalue = search_nametab(player, indiv_attraccess_nametab, flag);
            if (flagvalue < 0)
            {
                notify_quiet(player, "You can't set that!");
                return;
            }

            // Make sure the object has the attribute present.
            //
            if (!atr_get_info(thing, atr, &aowner, &aflags))
            {
                notify_quiet(player, "Attribute not present on object.");
                return;
            }

            // Make sure we can write to the attribute.
            //
            attr = atr_num(atr);
            if (!attr || !Set_attr(player, thing, attr, aflags))
            {
                notify_quiet(player, NOPERM_MESSAGE);
                return;
            }

            // Go do it.
            //
            if (clear)
            {
                aflags &= ~flagvalue;
            }
            else
            {
                aflags |= flagvalue;
            }
            could_hear = Hearer(thing);
            atr_set_flags(thing, atr, aflags);

            // Tell the player about it.
            //
            handle_ears(thing, could_hear, Hearer(thing));
            if (  !(key & SET_QUIET)
               && !Quiet(player)
               && !Quiet(thing))
            {
                if (clear)
                {
                    notify_quiet(player, "Cleared.");
                }
                else
                {
                    notify_quiet(player, "Set.");
                }
            }
            return;
        }
    }

    // Find thing.
    //
    if ((thing = match_controlled(player, name)) == NOTHING)
    {
        return;
    }

    if (isGarbage(thing))
    {
        notify_quiet(player, "You shouldn't be rummaging through the garbage.");
        return;
    }


    // Check for attribute set first.
    //
    for (p = flag; *p && (*p != ':'); p++)
    {
        // Nothing
        ;
    }

    if (*p)
    {
        *p++ = 0;
        atr = mkattr(flag);
        if (atr <= 0)
        {
            notify_quiet(player, "Couldn't create attribute.");
            return;
        }
        attr = atr_num(atr);
        if (!attr)
        {
            notify_quiet(player, NOPERM_MESSAGE);
            return;
        }
        atr_get_info(thing, atr, &aowner, &aflags);
        if (!Set_attr(player, thing, attr, aflags))
        {
            notify_quiet(player, NOPERM_MESSAGE);
            return;
        }
        buff = alloc_lbuf("do_set");

        // Check for _
        //
        if (*p == '_')
        {
            strcpy(buff, p + 1);
            if (  !parse_attrib(player, p + 1, &thing2, &atr2)
               || (atr2 == NOTHING))
            {
                notify_quiet(player, "No match.");
                free_lbuf(buff);
                return;
            }
            attr2 = atr_num(atr2);
            p = buff;
            atr_pget_str(buff, thing2, atr2, &aowner, &aflags);

            if (  !attr2
               || !See_attr(player, thing2, attr2, aowner, aflags))
            {
                notify_quiet(player, NOPERM_MESSAGE);
                free_lbuf(buff);
                return;
            }
        }

        // Go set it.
        //
        set_attr_internal(player, thing, atr, p, key);
        free_lbuf(buff);
        return;
    }

    // Set or clear a flag.
    //
    flag_set(thing, player, flag, key);
}

void do_power
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *flag
)
{
    if (!flag || !*flag)
    {
        notify_quiet(player, "I don't know what you want to set!");
        return;
    }

    // Find thing.
    //
    dbref thing = match_controlled(player, name);
    if (thing == NOTHING)
    {
        return;
    }
    power_set(thing, player, flag, key);
}

void do_setattr
(
    dbref player,
    dbref cause,
    int   attrnum,
    int   nargs,
    char *name,
    char *attrtext
)
{
    init_match(player, name, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    dbref thing = noisy_match_result();

    if (thing == NOTHING)
    {
        return;
    }
    if (isGarbage(thing))
    {
        notify_quiet(player, "You shouldn't be rummaging through the garbage.");
        return;
    }
    set_attr_internal(player, thing, attrnum, attrtext, 0);
}



void do_cpattr(dbref player, dbref cause, int key, char *oldpair, char *newpair[], int nargs)
{
    int i;
    char *oldthing, *oldattr, *newthing, *newattr;

    if (!*oldpair || !**newpair || !oldpair || !*newpair)
        return;

    if (nargs < 1)
        return;

    oldattr = oldpair;
    oldthing = parse_to(&oldattr, '/', 1);

    for (i = 0; i < nargs; i++)
    {
        newattr = newpair[i];
        newthing = parse_to(&newattr, '/', 1);

        if (!oldattr)
        {
            if (!newattr)
            {
                do_set(player, cause, 0, 2, newthing, tprintf("%s:_%s/%s", oldthing, "me", oldthing));
            }
            else
            {
                do_set(player, cause, 0, 2, newthing, tprintf("%s:_%s/%s", newattr, "me", oldthing));
            }
        }
        else
        {
            if (!newattr)
            {
                do_set(player, cause, 0, 2, newthing, tprintf("%s:_%s/%s", oldattr, oldthing, oldattr));
            }
            else
            {
                do_set(player, cause, 0, 2, newthing, tprintf("%s:_%s/%s", newattr, oldthing, oldattr));
            }
        }
    }
}


void do_mvattr(dbref player, dbref cause, int key, char *what, char *args[], int nargs)
{
    dbref thing, aowner, axowner;
    ATTR *in_attr, *out_attr;
    int i, anum, in_anum, aflags = 0, axflags, no_delete;
    char *astr;

    // Make sure we have something to do.
    //
    if (nargs < 2)
    {
        notify_quiet(player, "Nothing to do.");
        return;
    }

    // Find and make sure we control the target object.
    //
    thing = match_controlled(player, what);
    if (thing == NOTHING)
    {
        return;
    }

    // Look up the source attribute.  If it either doesn't exist or isn't
    // readable, use an empty string.
    //
    in_anum = -1;
    astr = alloc_lbuf("do_mvattr");
    in_attr = atr_str(args[0]);
    if (in_attr == NULL)
    {
        *astr = '\0';
    }
    else
    {
        atr_get_str(astr, thing, in_attr->number, &aowner, &aflags);
        if (!See_attr(player, thing, in_attr, aowner, aflags))
        {
            *astr = '\0';
        }
        else
        {
            in_anum = in_attr->number;
        }
    }

    // Copy the attribute to each target in turn.
    //
    no_delete = 0;
    for (i = 1; i < nargs; i++)
    {
        anum = mkattr(args[i]);
        if (anum <= 0)
        {
            notify_quiet(player, tprintf("%s: That's not a good name for an attribute.", args[i]));
            continue;
        }
        out_attr = atr_num(anum);
        if (!out_attr)
        {
            notify_quiet(player, tprintf("%s: Permission denied.", args[i]));
            no_delete++;
        }
        else if (out_attr->number == in_anum)
        {
            // The following causes the attribute to -not- be deleted on the source.
            //
            no_delete = nargs-1;
        }
        else
        {
            atr_get_info(thing, out_attr->number, &axowner, &axflags);
            if (!Set_attr(player, thing, out_attr, axflags))
            {
                notify_quiet(player, tprintf("%s: Permission denied.", args[i]));
                no_delete++;
            }
            else
            {
                atr_add(thing, out_attr->number, astr, Owner(player), aflags);
                if (!Quiet(player))
                {
                    notify_quiet(player, tprintf("%s: Set.", out_attr->name));
                }
            }
        }
    }

    // Remove the source attribute if we were able to copy is successfully to
    // even one destination object.
    //
    if ((in_anum > 0) && no_delete < nargs-1)
    {
        in_attr = atr_num(in_anum);
        if (in_attr && Set_attr(player, thing, in_attr, aflags))
        {
            atr_clr(thing, in_attr->number);
            if (!Quiet(player))
            {
                notify_quiet(player, tprintf("%s: Cleared.", in_attr->name));
            }
        }
        else
        {
            if (in_attr)
            {
                notify_quiet(player, tprintf("%s: Could not remove old attribute.  Permission denied.", in_attr->name));
            }
        }
    }
    free_lbuf(astr);
}

/*
 * ---------------------------------------------------------------------------
 * * parse_attrib, parse_attrib_wild: parse <obj>/<attr> tokens.
 */

int parse_attrib(dbref player, char *str, dbref *thing, int *atr)
{
    ATTR *attr;
    char *buff;
    dbref aowner;
    int aflags;

    *thing = NOTHING;
    *atr = NOTHING;

    if (!str)
        return 0;

    // Break apart string into obj and attr.  Return on failure.
    //
    buff = alloc_lbuf("parse_attrib");
    StringCopy(buff, str);
    if (!parse_thing_slash(player, buff, &str, thing))
    {
        free_lbuf(buff);
        return 0;
    }

    // Get the named attribute from the object if we can.
    //
    attr = atr_str(str);
    free_lbuf(buff);
    if (attr)
    {
        atr_pget_info(*thing, attr->number, &aowner, &aflags);
        if (See_attr(player, *thing, attr, aowner, aflags))
        {
            *atr = attr->number;
        }
    }
    return 1;
}

static void find_wild_attrs(dbref player, dbref thing, char *str, int check_exclude, int hash_insert, int get_locks)
{
    ATTR *attr;
    char *as;
    dbref aowner;
    int ca, ok, aflags;

    // Walk the attribute list of the object.
    //
    atr_push();
    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        attr = atr_num(ca);

        // Discard bad attributes and ones we've seen before.
        //
        if (!attr)
        {
            continue;
        }

        if (  check_exclude
           && (  (attr->flags & AF_PRIVATE)
              || hashfindLEN(&ca, sizeof(ca), &mudstate.parent_htab)))
        {
            continue;
        }

        // If we aren't the top level remember this attr so we exclude it in
        // any parents.
        //
        atr_get_info(thing, ca, &aowner, &aflags);
        if (  check_exclude
           && (aflags & AF_PRIVATE))
        {
            continue;
        }

        if (get_locks)
        {
            ok = Read_attr(player, thing, attr, aowner, aflags);
        }
        else
        {
            ok = See_attr(player, thing, attr, aowner, aflags);
        }

        // Enforce locality restriction on descriptions.
        //
        if (  ok
           && attr->number == A_DESC
           && !mudconf.read_rem_desc
           && !Examinable(player, thing)
           && !nearby(player, thing))
        {
            ok = 0;
        }

        if (  ok
           && quick_wild(str, (char *)attr->name))
        {
            olist_add(ca);
            if (hash_insert)
            {
                hashaddLEN(&ca, sizeof(ca), (int *)attr, &mudstate.parent_htab);
            }
        }
    }
    atr_pop();
}

int parse_attrib_wild(dbref player, char *str, dbref *thing, int check_parents, int get_locks, int df_star)
{
    char *buff;
    dbref parent;
    int check_exclude, hash_insert, lev;

    if (!str)
    {
        return 0;
    }

    buff = alloc_lbuf("parse_attrib_wild");
    strcpy(buff, str);

    // Separate name and attr portions at the first /.
    //
    if (!parse_thing_slash(player, buff, &str, thing))
    {
        // Not in obj/attr format, return if not defaulting to.
        //
        if (!df_star)
        {
            free_lbuf(buff);
            return 0;
        }

        // Look for the object, return failure if not found.
        //
        init_match(player, buff, NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        *thing = match_result();

        if (!Good_obj(*thing))
        {
            free_lbuf(buff);
            return 0;
        }
        str = (char *)"*";
    }

    // Check the object (and optionally all parents) for attributes.
    //
    if (check_parents)
    {
        check_exclude = 0;
        hash_insert = check_parents;
        hashflush(&mudstate.parent_htab);
        ITER_PARENTS(*thing, parent, lev)
        {
            if (!Good_obj(Parent(parent)))
                hash_insert = 0;
            find_wild_attrs(player, parent, str, check_exclude, hash_insert, get_locks);
            check_exclude = 1;
        }
    }
    else
    {
        find_wild_attrs(player, *thing, str, 0, 0, get_locks);
    }
    free_lbuf(buff);
    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * edit_string, edit_string_ansi, do_edit: Modify attributes.
 */

void edit_string(char *src, char **dst, char *from, char *to)
{
    char *cp;

    // Do the substitution.  Idea for prefix/suffix from R'nice@TinyTIM.
    //
    if (!strcmp(from, "^"))
    {
        // Prepend 'to' to string.
        //
        *dst = alloc_lbuf("edit_string.^");
        cp = *dst;
        safe_str(to, *dst, &cp);
        safe_str(src, *dst, &cp);
        *cp = '\0';
    }
    else if (!strcmp(from, "$"))
    {
        // Append 'to' to string.
        //
        *dst = alloc_lbuf("edit_string.$");
        cp = *dst;
        safe_str(src, *dst, &cp);
        safe_str(to, *dst, &cp);
        *cp = '\0';
    }
    else
    {
        // Replace all occurances of 'from' with 'to'. Handle the special
        // cases of from = \$ and \^.
        //
        if (  (  from[0] == '\\'
              || from[0] == '%')
           && (  from[1] == '$'
              || from[1] == '^')
           && from[2] == '\0')
        {
            from++;
        }
        *dst = replace_string(from, to, src);
    }
}

void edit_string_ansi(char *src, char **dst, char **returnstr, char *from, char *to)
{
    char *cp, *rp;

    // Do the substitution.  Idea for prefix/suffix from R'nice@TinyTIM
    //
    if (!strcmp(from, "^"))
    {
        // Prepend 'to' to string.
        //
        *dst = alloc_lbuf("edit_string.^");
        cp = *dst;
        safe_str(to, *dst, &cp);
        safe_str(src, *dst, &cp);
        *cp = '\0';

        // Do the ansi string used to notify.
        //
        *returnstr = alloc_lbuf("edit_string_ansi.^");
        rp = *returnstr;
        safe_str(ANSI_HILITE, *returnstr, &rp);
        safe_str(to, *returnstr, &rp);
        safe_str(ANSI_NORMAL, *returnstr, &rp);
        safe_str(src, *returnstr, &rp);
        *rp = '\0';

    }
    else if (!strcmp(from, "$"))
    {
        // Append 'to' to string
        //
        *dst = alloc_lbuf("edit_string.$");
        cp = *dst;
        safe_str(src, *dst, &cp);
        safe_str(to, *dst, &cp);
        *cp = '\0';

        // Do the ansi string used to notify.
        //
        *returnstr = alloc_lbuf("edit_string_ansi.$");
        rp = *returnstr;
        safe_str(src, *returnstr, &rp);
        safe_str(ANSI_HILITE, *returnstr, &rp);
        safe_str(to, *returnstr, &rp);
        safe_str(ANSI_NORMAL, *returnstr, &rp);
        *rp = '\0';

    }
    else
    {
        // Replace all occurances of 'from' with 'to'. Handle the special
        // cases of from = \$ and \^.
        //
        if (  ((from[0] == '\\') || (from[0] == '%'))
           && ((from[1] == '$')  || (from[1] == '^'))
           && ( from[2] == '\0'))
        {
            from++;
        }

        *dst = replace_string(from, to, src);
        *returnstr = replace_string(from, tprintf("%s%s%s", ANSI_HILITE,
                             to, ANSI_NORMAL), src);
    }
}

void do_edit(dbref player, dbref cause, int key, char *it, char *args[], int nargs)
{
    dbref thing, aowner;
    int attr, got_one, aflags, doit;
    char *from, *to, *result, *returnstr, *atext;
    ATTR *ap;

    // Make sure we have something to do.
    //
    if ((nargs < 1) || !*args[0])
    {
        notify_quiet(player, "Nothing to do.");
        return;
    }
    from = args[0];
    to = (nargs >= 2) ? args[1] : (char *)"";

    // Look for the object and get the attribute (possibly wildcarded)
    //
    olist_push();
    if (!it || !*it || !parse_attrib_wild(player, it, &thing, 0, 0, 0))
    {
        notify_quiet(player, "No match.");
        return;
    }

    // Iterate through matching attributes, performing edit.
    //
    got_one = 0;
    atext = alloc_lbuf("do_edit.atext");
    int could_hear = Hearer(thing);

    for (attr = olist_first(); attr != NOTHING; attr = olist_next())
    {
        ap = atr_num(attr);
        if (ap)
        {
            // Get the attr and make sure we can modify it.
            //
            atr_get_str(atext, thing, ap->number, &aowner, &aflags);
            if (Set_attr(player, thing, ap, aflags))
            {
                // Do the edit and save the result
                //
                got_one = 1;
                edit_string_ansi(atext, &result, &returnstr, from, to);
                if (ap->check != NULL)
                {
                    doit = (*ap->check) (0, player, thing,
                            ap->number, result);
                }
                else
                {
                    doit = 1;
                }
                if (doit)
                {
                    atr_add(thing, ap->number, result,
                        Owner(player), aflags);
                    if (!Quiet(player))
                        notify_quiet(player,
                             tprintf("Set - %s: %s",
                                 ap->name,
                                 returnstr));
                }
                free_lbuf(result);
                free_lbuf(returnstr);
            }
            else
            {
                // No rights to change the attr.
                //
                notify_quiet(player,
                       tprintf("%s: Permission denied.",
                           ap->name));
            }

        }
    }

    // Clean up.
    //
    free_lbuf(atext);
    olist_pop();

    if (!got_one)
    {
        notify_quiet(player, "No matching attributes.");
    }
    else
    {
        handle_ears(thing, could_hear, Hearer(thing));
    }
}

void do_wipe(dbref player, dbref cause, int key, char *it)
{
    dbref thing, aowner;
    int attr, got_one, aflags;
    ATTR *ap;
    char *atext;

    olist_push();
    if (!it || !*it || !parse_attrib_wild(player, it, &thing, 0, 0, 1))
    {
        notify_quiet(player, "No match.");
        return;
    }
    if (  mudconf.safe_wipe
       && has_flag(NOTHING, thing, "SAFE"))
    {
        notify_quiet(player, "SAFE objects may not be @wiped.");
        return;
    }

    // Iterate through matching attributes, zapping the writable ones
    //
    got_one = 0;
    atext = alloc_lbuf("do_wipe.atext");
    int could_hear = Hearer(thing);

    for (attr = olist_first(); attr != NOTHING; attr = olist_next())
    {
        ap = atr_num(attr);
        if (ap)
        {
            // Get the attr and make sure we can modify it.
            //
            atr_get_str(atext, thing, ap->number, &aowner, &aflags);
            if (Set_attr(player, thing, ap, aflags))
            {
                atr_clr(thing, ap->number);
                got_one = 1;
            }
        }
    }

    // Clean up
    //
    free_lbuf(atext);
    olist_pop();

    if (!got_one)
    {
        notify_quiet(player, "No matching attributes.");
    }
    else
    {
        handle_ears(thing, could_hear, Hearer(thing));
        if (!Quiet(player))
        {
            notify_quiet(player, "Wiped.");
        }
    }
}

void do_trigger(dbref player, dbref cause, int key, char *object, char *argv[], int nargs)
{
    dbref thing;
    int attrib;

    if (  !parse_attrib(player, object, &thing, &attrib)
       || attrib == NOTHING)
    {
        notify_quiet(player, "No match.");
        return;
    }
    if (!controls(player, thing))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        return;
    }
    did_it(player, thing, 0, NULL, 0, NULL, attrib, argv, nargs);

    // TODO: Be more descriptive as to what was triggered?
    //
    if (  !(key & TRIG_QUIET)
       && !Quiet(player))
    {
        notify_quiet(player, "Triggered.");
    }
}

void do_use(dbref player, dbref cause, int key, char *object)
{
    char *df_use, *df_ouse, *temp;
    dbref thing, aowner;
    int aflags, doit;

    init_match(player, object, NOTYPE);
    match_neighbor();
    match_possession();
    if (Wizard(player)) {
        match_absolute();
        match_player();
    }
    match_me();
    match_here();
    thing = noisy_match_result();
    if (thing == NOTHING)
        return;

    // Make sure player can use it.
    //
    if (!could_doit(player, thing, A_LUSE))
    {
        did_it(player, thing, A_UFAIL,
               "You can't figure out how to use that.",
               A_OUFAIL, NULL, A_AUFAIL, (char **)NULL, 0);
        return;
    }
    temp = alloc_lbuf("do_use");
    doit = 0;
    if (*atr_pget_str(temp, thing, A_USE, &aowner, &aflags))
    {
        doit = 1;
    }
    else if (*atr_pget_str(temp, thing, A_OUSE, &aowner, &aflags))
    {
        doit = 1;
    }
    else if (*atr_pget_str(temp, thing, A_AUSE, &aowner, &aflags))
    {
        doit = 1;
    }
    free_lbuf(temp);

    if (doit)
    {
        df_use = alloc_lbuf("do_use.use");
        df_ouse = alloc_lbuf("do_use.ouse");
        sprintf(df_use, "You use %s", Name(thing));
        sprintf(df_ouse, "uses %s", Name(thing));
        did_it(player, thing, A_USE, df_use, A_OUSE, df_ouse, A_AUSE,
               (char **)NULL, 0);
        free_lbuf(df_use);
        free_lbuf(df_ouse);
    }
    else
    {
        notify_quiet(player, "You can't figure out how to use that.");
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_setvattr: Set a user-named (or possibly a predefined) attribute.
 */

void do_setvattr
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    char *s;
    int anum;

    // Skip the '&'
    //
    arg1++;

    // Take to the space
    //
    for (s = arg1; *s && !Tiny_IsSpace[(unsigned char)*s]; s++) ;

    // Split it
    //
    if (*s)
        *s++ = '\0';

    // Get or make attribute
    //
    anum = mkattr(arg1);

    if (anum <= 0)
    {
        notify_quiet(player, "That's not a good name for an attribute.");
        return;
    }
    do_setattr(player, cause, anum, 2, s, arg2);
}
