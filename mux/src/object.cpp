// object.cpp -- Low-level object manipulation routines.
//
// $Id: object.cpp,v 1.8 2002-06-13 15:29:15 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "db.h"
#include "mudconf.h"
#include "command.h"
#include "flags.h"
#include "powers.h"
#include "attrs.h"
#include "alloc.h"
#include "mguests.h"

#define IS_CLEAN(i) (IS(i, TYPE_GARBAGE, GOING) && \
             (Location(i) == NOTHING) && \
             (Contents(i) == NOTHING) && (Exits(i) == NOTHING) && \
             (Next(i) == NOTHING) && (Owner(i) == GOD))

static int check_type;

#ifdef STANDALONE

// Functions needed in standalone mode.
//

// move_object: taken from move.c with look and penny check extracted.
//
void move_object(dbref thing, dbref dest)
{
    // Remove from the source location.
    //
    dbref src = Location(thing);
    if (src != NOTHING)
    {
        s_Contents(src, remove_first(Contents(src), thing));
    }

    // Special check for HOME.
    //
    if (dest == HOME)
    {
        dest = Home(thing);
    }

    // Add to destination location.
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
}

#define move_via_generic(obj,where,extra,hush) move_object(obj,where)

#endif

/*
 * ---------------------------------------------------------------------------
 * * Log_pointer_err, Log_header_err, Log_simple_damage: Write errors to the
 * * log file.
 */

static void Log_pointer_err(dbref prior, dbref obj, dbref loc, dbref ref, const char *reftype, const char *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG")
        log_type_and_name(obj);
    if (loc != NOTHING) {
        log_text((char *)" in ");
        log_type_and_name(loc);
    }
    log_text((char *)": ");
    if (prior == NOTHING) {
        log_text((char *)reftype);
    } else {
        log_text((char *)"Next pointer");
    }
    log_text((char *)" ");
    log_type_and_name(ref);
    log_text((char *)" ");
    log_text((char *)errtype);
    ENDLOG
}

static void Log_header_err(dbref obj, dbref loc, dbref val, int is_object, const char *valtype, const char *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG")
        log_type_and_name(obj);
    if (loc != NOTHING) {
        log_text((char *)" in ");
        log_type_and_name(loc);
    }
    log_text((char *)": ");
    log_text((char *)valtype);
    log_text((char *)" ");
    if (is_object)
        log_type_and_name(val);
    else
        log_number(val);
    log_text((char *)" ");
    log_text((char *)errtype);
    ENDLOG
}

static void Log_simple_err(dbref obj, dbref loc, const char *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG")
        log_type_and_name(obj);
    if (loc != NOTHING) {
        log_text((char *)" in ");
        log_type_and_name(loc);
    }
    log_text((char *)": ");
    log_text((char *)errtype);
    ENDLOG
}

/*
 * ---------------------------------------------------------------------------
 * * start_home, default_home, can_set_home, new_home, clone_home:
 * * Routines for validating and determining homes.
 */

dbref NDECL(start_home)
{
    if (mudconf.start_home != NOTHING)
        return mudconf.start_home;
    return mudconf.start_room;
}

dbref NDECL(default_home)
{
    if (mudconf.default_home != NOTHING)
        return mudconf.default_home;
    if (mudconf.start_home != NOTHING)
        return mudconf.start_home;
    return mudconf.start_room;
}

int can_set_home(dbref player, dbref thing, dbref home)
{
    if (  !Good_obj(player)
       || !Good_obj(home)
       || thing == home)
    {
        return 0;
    }

    switch (Typeof(home))
    {
    case TYPE_PLAYER:
    case TYPE_ROOM:
    case TYPE_THING:
        if (Going(home))
        {
            return 0;
        }
        if (  Controls(player, home)
           || Abode(home))
        {
            return 1;
        }
    }
    return 0;
}

dbref new_home(dbref player)
{
    dbref loc = Location(player);
    if (can_set_home(Owner(player), player, loc))
    {
        return loc;
    }
    loc = Home(Owner(player));
    if (can_set_home(Owner(player), player, loc))
    {
        return loc;
    }
    return default_home();
}

dbref clone_home(dbref player, dbref thing)
{
    dbref loc;

    loc = Home(thing);
    if (can_set_home(Owner(player), player, loc))
        return loc;
    return new_home(player);
}

/*
 * ---------------------------------------------------------------------------
 * * create_obj: Create an object of the indicated type IF the player can
 * * afford it.
 */

#ifndef STANDALONE

dbref create_obj(dbref player, int objtype, char *name, int cost)
{
    dbref obj, owner;
    int quota = 0, value = 0, self_owned = 0, require_inherit = 0, nValidName;
    FLAGSET f;
    char *buff, *pValidName;
    const char *tname;
    BOOL okname = FALSE;

    switch (objtype)
    {
    case TYPE_ROOM:

        cost = mudconf.digcost;
        quota = mudconf.room_quota;
        f = mudconf.room_flags;
        pValidName = MakeCanonicalObjectName(name, &nValidName, &okname);
        tname = "a room";
        break;

    case TYPE_THING:

        if (cost < mudconf.createmin)
            cost = mudconf.createmin;
        if (cost > mudconf.createmax)
            cost = mudconf.createmax;
        quota = mudconf.thing_quota;
        f = mudconf.thing_flags;
        value = OBJECT_ENDOWMENT(cost);
        pValidName = MakeCanonicalObjectName(name, &nValidName, &okname);
        tname = "a thing";
        break;

    case TYPE_EXIT:

        cost = mudconf.opencost;
        quota = mudconf.exit_quota;
        f = mudconf.exit_flags;
        pValidName = MakeCanonicalObjectName(name, &nValidName, &okname);
        tname = "an exit";
        break;

    case TYPE_PLAYER:

        if (cost)
        {
            cost = mudconf.robotcost;
            quota = mudconf.player_quota;
            f = mudconf.robot_flags;
            value = 0;
            tname = "a robot";
            require_inherit = 1;
        }
        else
        {
            cost = 0;
            quota = 0;
            f = mudconf.player_flags;
            value = mudconf.paystart;
            quota = mudconf.start_quota;
            self_owned = 1;
            tname = "a player";
        }
        buff = munge_space(name);
        pValidName = name;
        if (!badname_check(buff))
        {
            notify(player, "That name is not allowed.");
            free_lbuf(buff);
            return NOTHING;
        }
        if (*buff)
        {
            okname = ValidatePlayerName(buff);
            if (!okname)
            {
                notify(player, "That's a silly name for a player.");
                free_lbuf(buff);
                return NOTHING;
            }
        }
        if (okname)
        {
            okname = lookup_player(NOTHING, buff, 0) == NOTHING;
            if (!okname)
            {
                notify(player, tprintf("The name %s is already taken.", name));
                free_lbuf(buff);
                return NOTHING;
            }
        }
        free_lbuf(buff);
        break;

    default:
        LOG_SIMPLE(LOG_BUGS, "BUG", "OTYPE", tprintf("Bad object type in create_obj: %d.", objtype));
        return NOTHING;
    }

    if (!okname)
    {
        notify(player, tprintf("That's a silly name for %s!", tname));
        return NOTHING;
    }

    if (!self_owned)
    {
        if (!Good_obj(player))
        {
            return NOTHING;
        }
        owner = Owner(player);
        if (!Good_obj(owner))
        {
            return NOTHING;
        }
    }
    else
    {
        owner = NOTHING;
    }

    if (require_inherit)
    {
        if (!Inherits(player))
        {
            notify(player, NOPERM_MESSAGE);
            return NOTHING;
        }
    }

    // Make sure the creator can pay for the object.
    //
    if ((player != NOTHING) && !canpayfees(player, player, cost, quota))
    {
        return NOTHING;
    }

    // Get the first object from the freelist.  If the object is not clean,
    // discard the remainder of the freelist and go get a completely new object.
    //
    obj = NOTHING;
    if (mudstate.freelist != NOTHING)
    {
        obj = mudstate.freelist;
        if (  Good_obj(obj)
           && IS_CLEAN(obj))
        {
            mudstate.freelist = Link(obj);
        }
        else
        {
            LOG_SIMPLE(LOG_PROBLEMS, "FRL", "DAMAG", tprintf("Freelist damaged, bad object #%d.", obj));
            obj = NOTHING;
            mudstate.freelist = NOTHING;
        }
    }
    if (obj == NOTHING)
    {
        obj = mudstate.db_top;
        db_grow(mudstate.db_top + 1);
    }
    atr_free(obj);  // just in case.

    // Set things up according to the object type.
    //
    s_Location(obj, NOTHING);
    s_Contents(obj, NOTHING);
    s_Exits(obj, NOTHING);
    s_Next(obj, NOTHING);
    s_Link(obj, NOTHING);
    s_Parent(obj, NOTHING);

    if (mudconf.autozone && player != NOTHING)
    {
        s_Zone(obj, Zone(player));
    }
    else
    {
        s_Zone(obj, NOTHING);
    }
    f.word[FLAG_WORD1] |= objtype;
    db[obj].fs = f;
    s_Owner(obj, (self_owned ? obj : owner));
    s_Pennies(obj, value);
    Unmark(obj);
    pValidName = munge_space((char *)pValidName);
    s_Name(obj, pValidName);
    free_lbuf(pValidName);
    db[obj].cpu_time_used.Set100ns(0);

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    buff = ltaNow.ReturnDateString();
    atr_add_raw(obj, A_CREATED, buff);

    if (objtype == TYPE_PLAYER)
    {
        atr_add_raw(obj, A_LAST, buff);

        buff = alloc_sbuf("create_obj.quota");
        Tiny_ltoa(quota, buff);
        atr_add_raw(obj, A_QUOTA, buff);
        atr_add_raw(obj, A_RQUOTA, buff);
        add_player_name(obj, Name(obj));
        free_sbuf(buff);
        s_Zone(obj, NOTHING);
    }
    return obj;
}

#endif

#ifndef STANDALONE
extern void stack_clr(dbref obj);
#endif

/*
 * ---------------------------------------------------------------------------
 * * destroy_obj: Destroy an object.  Assumes it has already been removed from
 * * all lists and has no contents or exits.
 */

void destroy_bad_obj(dbref obj)
{
#ifndef STANDALONE
    halt_que(NOTHING, obj);
    nfy_que(obj, A_SEMAPHORE, NFY_DRAIN, 0);
    fwdlist_clr(obj);
    stack_clr(obj);
    ReleaseAllResources(obj);
#endif // STANDALONE
    atr_free(obj);
    s_Name(obj, NULL);
    s_Flags(obj, FLAG_WORD1, (TYPE_GARBAGE | GOING));
    s_Flags(obj, FLAG_WORD2, 0);
    s_Flags(obj, FLAG_WORD3, 0);
    s_Powers(obj, 0);
    s_Powers2(obj, 0);
    s_Location(obj, NOTHING);
    s_Contents(obj, NOTHING);
    s_Exits(obj, NOTHING);
    s_Next(obj, NOTHING);
    s_Link(obj, NOTHING);
    s_Owner(obj, GOD);
    s_Pennies(obj, 0);
    s_Parent(obj, NOTHING);
    s_Zone(obj, NOTHING);
}

void destroy_obj(dbref obj)
{
    if (!Good_obj(obj))
    {
        destroy_bad_obj(obj);
        return;
    }

    // Validate the owner.
    //
    dbref owner = Owner(obj);
    int good_owner = Good_owner(owner);

    // Halt any pending commands (waiting or semaphore).
    //
#ifndef STANDALONE
    if (halt_que(NOTHING, obj) > 0)
    {
        if (good_owner && !Quiet(obj) && !Quiet(owner))
        {
            notify(owner, "Halted.");
        }
    }
    nfy_que(obj, A_SEMAPHORE, NFY_DRAIN, 0);

    // Remove forwardlists and stacks.
    //
    fwdlist_clr(obj);
    stack_clr(obj);
#endif

    // Compensate the owner for the object.
    //
    if (  good_owner
       && owner != obj)
    {
        int val = 0;
        int quota = 0;
        switch (Typeof(obj))
        {
        case TYPE_ROOM:
            val = mudconf.digcost;
            quota = mudconf.room_quota;
            break;

        case TYPE_THING:
            val = OBJECT_DEPOSIT(Pennies(obj));
            quota = mudconf.thing_quota;
            break;

        case TYPE_EXIT:
#ifdef STANDALONE
            val = mudconf.opencost;
#else
            if (Location(obj) == NOTHING)
            {
                val = mudconf.opencost;
            }
            else
            {
                val = mudconf.opencost + mudconf.linkcost;
            }
#endif
            quota = mudconf.exit_quota;
            break;

        case TYPE_PLAYER:
            if (Robot(obj))
            {
                val = mudconf.robotcost;
            }
            else
            {
                val = 0;
            }
            quota = mudconf.player_quota;
            break;
        }
        if (val)
        {
            giveto(owner, val);
#ifndef STANDALONE
            if (  !Quiet(owner)
               && !Quiet(obj))
            {
                char *p = tprintf(
                       "You get back your %d %s deposit for %s(#%d).",
                        val, mudconf.one_coin, Name(obj), obj);
                notify(owner, p);
            }
#endif // STANDALONE
        }
        if (  mudconf.quotas
           && quota)
        {
            add_quota(owner, quota);
        }
    }
#ifndef STANDALONE
    ReleaseAllResources(obj);
#endif // STANDALONE
    atr_free(obj);
    s_Name(obj, NULL);
    s_Flags(obj, FLAG_WORD1, (TYPE_GARBAGE | GOING));
    s_Flags(obj, FLAG_WORD2, 0);
    s_Flags(obj, FLAG_WORD3, 0);
    s_Powers(obj, 0);
    s_Powers2(obj, 0);
    s_Location(obj, NOTHING);
    s_Contents(obj, NOTHING);
    s_Exits(obj, NOTHING);
    s_Next(obj, NOTHING);
    s_Link(obj, NOTHING);
    s_Owner(obj, GOD);
    s_Pennies(obj, 0);
    s_Parent(obj, NOTHING);
    s_Zone(obj, NOTHING);
}

/*
 * ---------------------------------------------------------------------------
 * * make_freelist: Build a freelist
 */

static void NDECL(make_freelist)
{
    dbref i;

    mudstate.freelist = NOTHING;
    DO_WHOLE_DB_BACKWARDS(i)
    {
        if (IS_CLEAN(i))
        {
            s_Link(i, mudstate.freelist);
            mudstate.freelist = i;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * divest_object: Get rid of KEY contents of object.
 */

void divest_object(dbref thing)
{
    dbref curr, temp;

    SAFE_DOLIST(curr, temp, Contents(thing)) {
        if (!Controls(thing, curr) &&
            Has_location(curr) && Key(curr)) {
            move_via_generic(curr, HOME, NOTHING, 0);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * empty_obj, purge_going: Get rid of GOING objects in the db.
 */

void empty_obj(dbref obj)
{
    dbref targ, next;

    // Send the contents home
    //
    SAFE_DOLIST(targ, next, Contents(obj))
    {
        if (!Has_location(targ))
        {
            Log_simple_err(targ, obj,
                   "Funny object type in contents list of GOING location. Flush terminated.");
            break;
        }
        else if (Location(targ) != obj)
        {
            Log_header_err(targ, obj, Location(targ), 1,
                   "Location",
                   "indicates object really in another location during cleanup of GOING location.  Flush terminated.");
            break;
        }
        else
        {
            s_Location(targ, NOTHING); 
            s_Next(targ, NOTHING);
            if (Home(targ) == obj)
            {
                s_Home(targ, new_home(targ));
            }
            move_via_generic(targ, HOME, NOTHING, 0);
            divest_object(targ);
        }
    }

    // Destroy the exits.
    //
    SAFE_DOLIST(targ, next, Exits(obj))
    {
        if (!isExit(targ) && !isGarbage(targ))
        {
            Log_simple_err(targ, obj, "Funny object type in exit list of GOING location. Flush terminated.");
            break;
        }
        else if (Exits(targ) != obj)
        {
            Log_header_err(targ, obj, Exits(targ), 1,
                   "Location",
                   "indicates exit really in another location during cleanup of GOING location.  Flush terminated.");
            break;
        }
        else
        {
            destroy_obj(targ);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * destroy_exit, destroy_thing, destroy_player
 */

void destroy_exit(dbref exit)
{
    dbref loc;

    loc = Exits(exit);
    s_Exits(loc, remove_first(Exits(loc), exit));
    destroy_obj(exit);
}

void destroy_thing(dbref thing)
{
    move_via_generic(thing, NOTHING, Owner(thing), 0);
    empty_obj(thing);
    destroy_obj(thing);
}

void destroy_player(dbref player, dbref victim)
{
#ifndef STANDALONE
    // Bye bye...
    //
    boot_off(victim, (char *)"You have been destroyed!");
    halt_que(victim, NOTHING);
    int count = chown_all(victim, player, player, CHOWN_NOZONE);

    // Remove the name from the name hash table.
    //
    delete_player_name(victim, Name(victim));
    dbref aowner;
    int aflags;
    char *buf = atr_pget(victim, A_ALIAS, &aowner, &aflags);
    delete_player_name(victim, buf);
    free_lbuf(buf);

    move_via_generic(victim, NOTHING, player, 0);
    destroy_obj(victim);
    notify_quiet(player, tprintf("(%d objects @chowned to you)", count));
#endif
}

static void NDECL(purge_going)
{
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (!Going(i))
        {
            continue;
        }

        char *p;
        switch (Typeof(i))
        {
        case TYPE_PLAYER:
            p = atr_get_raw(i, A_DESTROYER);
            if (!p)
            {
                STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG");
                log_type_and_name(i);
                dbref loc = Location(i);
                if (loc != NOTHING)
                {
                    log_text(" in ");
                    log_type_and_name(loc);
                }
                log_text("GOING object doesn't remember it's destroyer. GOING reset.");
                ENDLOG;
                db[i].fs.word[FLAG_WORD1] &= ~GOING;
            }
            else
            {
                dbref player = (dbref) Tiny_atol(p);
                destroy_player(player, i);
            }
            break;

        case TYPE_ROOM:

            // Room scheduled for destruction... do it.
            //
            empty_obj(i);
            destroy_obj(i);
            break;

        case TYPE_THING:
            destroy_thing(i);
            break;

        case TYPE_EXIT:
            destroy_exit(i);
            break;

        case TYPE_GARBAGE:
            break;

        default:

            // Something else... How did this happen?
            //
            Log_simple_err(i, NOTHING,
              "GOING object with unexpected type.  Destroyed.");
            destroy_obj(i);
        }
    }
}

// ---------------------------------------------------------------------------
// check_dead_refs: Look for references to GOING or illegal objects.
//
static void check_pennies(dbref thing, int limit, const char *qual)
{
    if (Going(thing))
    {
        return;
    }
    int j = Pennies(thing);
    if (j)
    {
        if (isRoom(thing) || isExit(thing))
        {
            Log_header_err(thing, NOTHING, j, 0, qual, "is strange.  Reset.");
            s_Pennies(thing, 0);
        }
        else if (j < 0)
        {
            Log_header_err(thing, NOTHING, j, 0, qual, "is negative.");
        }
        else if (limit < j)
        {
            Log_header_err(thing, NOTHING, j, 0, qual, "is excessive.");
        }
    }
    else
    {
        if(isPlayer(thing) || isThing(thing))
        {
            Log_header_err(thing, NOTHING, j, 0, qual, "is zero.");
        }
    }
}

static NDECL(void check_dead_refs)
{
    dbref targ, owner, i, j;
    int aflags, dirty = 0;
    char *str;
    FWDLIST *fp;

    DO_WHOLE_DB(i)
    {
        // Check the owner.
        //
        owner = Owner(i);
        if (!Good_obj(owner)) {
            if(isPlayer(i))
            {
            Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is invalid.  Set to player.");
            owner = i;
            }
            else
            {
            Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is invalid.  Set to GOD.");
            owner = GOD;
            }
            s_Owner(i, owner);
#ifndef STANDALONE
            halt_que(NOTHING, i);
#endif
            s_Halted(i);
        } else if (check_type & DBCK_FULL) {
            if (Going(owner)) {
                if(isPlayer(i))
                {
                    Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is set GOING.  Set to player.");
                    owner = i;
                }
                else
                {
                    Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is set GOING.  Set to GOD.");
                    owner = GOD;
                }
                s_Owner(i, owner);
#ifndef STANDALONE
                halt_que(NOTHING, i);
#endif
                s_Halted(i);
            } else if (!OwnsOthers(owner)) {
                if(isPlayer(i))
                {
                    Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is not a valid owner type.  Set to player.");
                    owner = i;
                }
                else
                {
                    Log_header_err(i, NOTHING, owner, 1,
                       "Owner", "is not a valid owner type.  Set to GOD.");
                    owner = GOD;
                }
                s_Owner(i, owner);
            }
        }

        // Check the parent
        targ = Parent(i);
        if (Good_obj(targ)) {
            if (Going(targ)) {
                s_Parent(i, NOTHING);
#ifndef STANDALONE
                
                if (!Quiet(i) && !Quiet(owner))
                    notify(owner, tprintf("Parent cleared on %s(#%d)", Name(i), i));
#else
                Log_header_err(i, Location(i), targ, 1,
                     "Parent", "is invalid.  Cleared.");
#endif
            }
        } else if (targ != NOTHING) {
            Log_header_err(i, Location(i), targ, 1,
                "Parent", "is invalid.  Cleared.");
            s_Parent(i, NOTHING);
        }

        // Check the zone.
        //
        targ = Zone(i);
        if (Good_obj(targ))
        {
            if (Going(targ))
            {
                s_Zone(i, NOTHING);
#ifndef STANDALONE
                owner = Owner(i);
                if (!Quiet(i) && !Quiet(owner))
                {
                    notify(owner,
                      tprintf("Zone cleared on %s(#%d)", Name(i), i));
                }
#else
                Log_header_err(i, Location(i), targ, 1,
                    "Zone", "is invalid.  Cleared.");
#endif
            }
        }
        else if (targ != NOTHING)
        {
            Log_header_err(i, Location(i), targ, 1, "Zone",
                "is invalid.  Cleared.");
            s_Zone(i, NOTHING);
        }

        // Check forwardlist
        fp = fwdlist_get(i);
        if (fp) {
            for (j = 0; j < fp->count; j++) {
                targ = fp->data[j];
                if (Good_obj(targ) && Going(targ)) {
                    fp->data[j] = NOTHING;
                    dirty = 1;
                } else if (!Good_obj(targ) &&
                       (targ != NOTHING)) {
                    fp->data[j] = NOTHING;
                    dirty = 1;
                }
            }
        }
        if (dirty) {
            str = alloc_lbuf("purge_going");
            (void)fwdlist_rewrite(fp, str);
            atr_get_info(i, A_FORWARDLIST, &owner, &aflags);
            atr_add(i, A_FORWARDLIST, str, owner, aflags);
            free_lbuf(str);
        }

        if (check_type & DBCK_FULL) {

            // Check for wizards
            if (Wizard(i)) {
                if (isPlayer(i)) {
                    Log_simple_err(i, NOTHING,
                             "Player is a WIZARD.");
                }
                if (!Wizard(Owner(i))) {
                    Log_header_err(i, NOTHING, Owner(i), 1,
                               "Owner",
                               "of a WIZARD object is not a wizard");
                }
            }
        }

        switch (Typeof(i))
        {
        case TYPE_PLAYER:
            // check home
            targ = Home(i);
            if (!Good_obj(targ) || !Has_contents(targ))
            {
                Log_simple_err(i, Location(i), "Bad home. Reset.");
                s_Home(i, default_home());
            }

            // Check the location.
            //
            targ = Location(i);
            if (!Good_obj(targ) || !Has_contents(targ))
            {
                Log_pointer_err(NOTHING, i, NOTHING, targ, "Location",
                    "is invalid.  Moved to home.");
                move_object(i, Home(i));
            }
                        
            // Check for self-referential Next()
            if (Next(i) == i) {
                Log_simple_err(i, NOTHING,
                     "Next points to self.  Next cleared.");
                s_Next(i, NOTHING);
            }

            if (check_type & DBCK_FULL) {
            // check wealth
            targ = mudconf.paylimit;
            check_pennies(i, targ, "Wealth");
            }
            break;

        case TYPE_THING:
            
            // check home
            targ = Home(i);
            if (!Good_obj(targ) || !Has_contents(targ))
            {
#ifndef STANDALONE
                if (!Quiet(i) && !Quiet(owner))
                    notify(owner, tprintf("Home reset on %s(#%d)", Name(i), i));
#else
                Log_header_err(i, Location(i), targ, 1,
                       "Home", "is invalid.  Cleared.");
                s_Home(i, new_home(i));
#endif
            }

            // Check the location.
            //
            targ = Location(i);
            if (!Good_obj(targ) || !Has_contents(targ))
            {
                Log_pointer_err(NOTHING, i, NOTHING, targ, "Location",
                    "is invalid.  Moved to home.");
                move_object(i, HOME);
            }

            // Check for self-referential Next().
            //
            if (Next(i) == i)
            {
                Log_simple_err(i, NOTHING,
                    "Next points to self.  Next cleared.");
                s_Next(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // Check value.
                //
                targ = OBJECT_ENDOWMENT(mudconf.createmax);
                check_pennies(i, targ, "Value");
            }
            break;

        case TYPE_ROOM:

            // Check the dropto.
            //
            targ = Dropto(i);
            if (Good_obj(targ))
            {
                if (Going(targ))
                {
                    s_Dropto(i, NOTHING);
#ifndef STANDALONE
                    if (!Quiet(i) && !Quiet(owner))
                    {
                        notify(owner, tprintf("Dropto removed from %s(#%d)",
                            Name(i), i));
                    }
#else
                    Log_header_err(i, NOTHING, targ, 1, "Dropto",
                        "is invalid.  Removed.");
#endif
                }
            }
            else if (  targ != NOTHING
                    && targ != HOME)
            {
                Log_header_err(i, NOTHING, targ, 1, "Dropto",
                    "is invalid.  Cleared.");
                s_Dropto(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // NEXT should be null.
                //
                if (Next(i) != NOTHING)
                {
                    Log_header_err(i, NOTHING, Next(i), 1, "Next pointer",
                        "should be NOTHING.  Reset.");
                    s_Next(i, NOTHING);
                }

                // LINK should be null.
                //
                if (Link(i) != NOTHING)
                {
                    Log_header_err(i, NOTHING, Link(i), 1, "Link pointer ",
                        "should be NOTHING.  Reset.");
                    s_Link(i, NOTHING);
                }

                // Check value.
                //
                check_pennies(i, 1, "Value");
            }
            break;

        case TYPE_EXIT:

            // If it points to something GOING, set it going.
            //
            targ = Location(i);
            if (Good_obj(targ))
            {
                if (Going(targ))
                {
                    s_Going(i);
                }
            }
            else if (targ == HOME)
            {
                // null case, HOME is always valid.
                //
            }
            else if (targ != NOTHING)
            {
                Log_header_err(i, Exits(i), targ, 1, "Destination",
                    "is invalid.  Exit destroyed.");
                s_Going(i);
            }
            else
            {
                if (!Has_contents(targ))
                {
                    Log_header_err(i, Exits(i), targ, 1, "Destination",
                        "is not a valid type.  Exit destroyed.");
                    s_Going(i);
                }
            }

            // Check for self-referential Next().
            //
            if (Next(i) == i)
            {
                Log_simple_err(i, NOTHING,
                    "Next points to self.  Next cleared.");
                s_Next(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // CONTENTS should be null.
                //
                if (Contents(i) != NOTHING)
                {
                    Log_header_err(i, Exits(i), Contents(i), 1, "Contents",
                        "should be NOTHING.  Reset.");
                    s_Contents(i, NOTHING);
                }

                // LINK should be null.
                //
                if (Link(i) != NOTHING)
                {
                    Log_header_err(i, Exits(i), Link(i), 1, "Link",
                        "should be NOTHING.  Reset.");
                    s_Link(i, NOTHING);
                }

                // Check value.
                //
                check_pennies(i, 1, "Value");
            }
            break;

        case TYPE_GARBAGE:
            break;

        default:

            // Funny object type, destroy it.
            //
            Log_simple_err(i, NOTHING, "Funny object type.  Destroyed.");
            destroy_obj(i);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * check_loc_exits, check_exit_chains: Validate the exits chains
 * * of objects and attempt to correct problems.  The following errors are
 * * found and corrected:
 * *      Location not in database                        - skip it.
 * *      Location GOING                                  - skip it.
 * *      Location not a PLAYER, ROOM, or THING           - skip it.
 * *      Location already visited                        - skip it.
 * *      Exit/next pointer not in database               - NULL it.
 * *      Member is not an EXIT                           - terminate chain.
 * *      Member is GOING                                 - destroy exit.
 * *      Member already checked (is in another list)     - terminate chain.
 * *      Member in another chain (recursive check)       - terminate chain.
 * *      Location of member is not specified location    - reset it.
 */

static void check_loc_exits(dbref loc)
{
    dbref exit, back, temp, exitloc, dest;

    if (!Good_obj(loc))
    {
        return;
    }

    // Only check players, rooms, and things that aren't GOING.
    //
    if (isExit(loc) || Going(loc))
    {
        return;
    }

    // If marked, we've checked here already.
    //
    if (Marked(loc))
    {
        return;
    }
    Mark(loc);

    // Check all the exits.
    //
    back = NOTHING;
    exit = Exits(loc);
    while (exit != NOTHING)
    {
        exitloc = NOTHING;
        dest = NOTHING;

        if (Good_obj(exit))
        {
            exitloc = Exits(exit);
            dest = Location(exit);
        }
        if (!Good_obj(exit))
        {
            // A bad pointer - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, exit, "Exit list",
                "is invalid.  List nulled.");
            if (back != NOTHING)
            {
                s_Next(back, NOTHING);
            }
            else
            {
                s_Exits(loc, NOTHING);
            }
            exit = NOTHING;
        }
        else if (!isExit(exit))
        {
            // Not an exit - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, exit, "Exitlist member",
                "is not an exit.  List terminated.");
            if (back != NOTHING)
            {
                s_Next(back, NOTHING);
            }
            else
            {
                s_Exits(loc, NOTHING);
            }
            exit = NOTHING;
        }
        else if (Going(exit))
        {
            // Going - silently filter out.
            //
            temp = Next(exit);
            if (back != NOTHING)
            {
                s_Next(back, temp);
            }
            else
            {
                s_Exits(loc, temp);
            }
            destroy_obj(exit);
            exit = temp;
            continue;
        }
        else if (Marked(exit))
        {
            // Already in another list - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, exit, "Exitlist member",
                "is in another exitlist.  Cleared.");
            if (back != NOTHING)
            {
                s_Next(back, NOTHING);
            }
            else
            {
                s_Exits(loc, NOTHING);
            }
            exit = NOTHING;
        }
        else if (  !Good_obj(dest)
                && dest != HOME
                && dest != NOTHING)
        {
            // Destination is not in the db.  Null it.
            //
            Log_pointer_err(back, loc, NOTHING, exit, "Destination",
                "is invalid.  Cleared.");
            s_Location(exit, NOTHING);
        }
        else if (exitloc != loc)
        {
            // Exit thinks it's in another place.  Check the exitlist there
            // and see if it contains this exit. If it does, then our exitlist
            // somehow pointed into the middle of their exitlist. If not,
            // assume we own the exit.'
            //
            check_loc_exits(exitloc);
            if (Marked(exit))
            {
                // It's in the other list, give it up.
                //
                Log_pointer_err(back, loc, NOTHING, exit, "",
                    "is in another exitlist.  List terminated.");
                if (back != NOTHING)
                {
                    s_Next(back, NOTHING);
                }
                else
                {
                    s_Exits(loc, NOTHING);
                }
                exit = NOTHING;
            }
            else
            {
                // Not in the other list, assume in ours.
                //
                Log_header_err(exit, loc, exitloc, 1,
                    "Not on chain for location", "Reset.");
                s_Exits(exit, loc);
            }
        }
        if (exit != NOTHING)
        {
            // All OK (or all was made OK).
            //
            if (check_type & DBCK_FULL)
            {
                // Make sure exit owner owns at least one of the source or
                // destination.  Just warn if he doesn't.
                //
                temp = Owner(exit);
                if (  temp != Owner(loc)
                   && temp != Owner(Location(exit)))
                {
                    Log_header_err(exit, loc, temp, 1, "Owner",
                        "does not own either the source or destination.");
                }
            }
            Mark(exit);
            back = exit;
            exit = Next(exit);
        }
    }
    return;
}

static void NDECL(check_exit_chains)
{
    dbref i;

    Unmark_all(i);
    DO_WHOLE_DB(i)
    {
        check_loc_exits(i);
    }
    DO_WHOLE_DB(i)
    {
        if (isExit(i) && !Marked(i))
        {
            Log_simple_err(i, NOTHING, "Disconnected exit.  Destroyed.");
            destroy_obj(i);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * check_misplaced_obj, check_loc_contents, check_contents_chains: Validate
 * * the contents chains of objects and attempt to correct problems.  The
 * * following errors are found and corrected:
 * *      Location not in database                        - skip it.
 * *      Location GOING                                  - skip it.
 * *      Location not a PLAYER, ROOM, or THING           - skip it.
 * *      Location already visited                        - skip it.
 * *      Contents/next pointer not in database           - NULL it.
 * *      Member is not an PLAYER or THING                - terminate chain.
 * *      Member is GOING                                 - destroy exit.
 * *      Member already checked (is in another list)     - terminate chain.
 * *      Member in another chain (recursive check)       - terminate chain.
 * *      Location of member is not specified location    - reset it.
 */

static void FDECL(check_loc_contents, (dbref));

static void check_misplaced_obj(dbref *obj, dbref back, dbref loc)
{
    // Object thinks it's in another place.  Check the contents list there
    // and see if it contains this object.  If it does, then our contents
    // list somehow pointed into the middle of their contents list and we
    // should truncate our list. If not, assume we own the object.
    //
    if (!Good_obj(*obj))
    {
        return;
    }
    loc = Location(*obj);
    Unmark(*obj);
    if (Good_obj(loc))
    {
        check_loc_contents(loc);
    }
    if (Marked(*obj))
    {
        // It's in the other list, give it up.
        //
        Log_pointer_err(back, loc, NOTHING, *obj, "",
            "is in another contents list.  Cleared.");
        if (back != NOTHING)
        {
            s_Next(back, NOTHING);
        }
        else
        {
            s_Contents(loc, NOTHING);
        }
        *obj = NOTHING;
    }
    else
    {
        // Not in the other list, assume in ours.
        //
        Log_header_err(*obj, loc, Contents(*obj), 1, "Location",
            "is invalid.  Reset.");
        s_Contents(*obj, loc);
    }
    return;
}

static void check_loc_contents(dbref loc)
{
    dbref obj, back, temp;

    if (!Good_obj(loc))
    {
        return;
    }

    // Only check players, rooms, and things that aren't GOING.
    //
    if (isExit(loc) || Going(loc))
    {
        return;
    }

    //Check all the exits.
    //
    back = NOTHING;
    obj = Contents(loc);
    while (obj != NOTHING)
    {
        if (!Good_obj(obj))
        {
            // A bad pointer - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, obj, "Contents list",
                "is invalid.  Cleared.");
            if (back != NOTHING)
            {
                s_Next(back, NOTHING);
            }
            else
            {
                s_Contents(loc, NOTHING);
            }
            obj = NOTHING;
        }
        else if (!Has_location(obj))
        {
            // Not a player or thing - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, obj, "",
                "is not a player or thing.  Cleared.");
            if (back != NOTHING)
            {
                s_Next(back, NOTHING);
            }
            else
            {
                s_Contents(loc, NOTHING);
            }
            obj = NOTHING;
        }
        else if (Going(obj) && (Typeof(obj) == TYPE_GARBAGE))
        {
            // Going - silently filter out.
            //
            temp = Next(obj);
            if (back != NOTHING)
            {
                s_Next(back, temp);
            }
            else
            {
                s_Contents(loc, temp);
            }
            destroy_obj(obj);
            obj = temp;
            continue;
        }
        else if (Marked(obj))
        {
            // Already visited - either truncate or ignore.
            //
            if (Location(obj) != loc)
            {
                // Location wrong - either truncate or fix.
                //
                check_misplaced_obj(&obj, back, loc);
            }
            else
            {
                // Location right - recursive contents.
                //
            }
        }
        else if (Location(obj) != loc)
        {
            // Location wrong - either truncate or fix.
            //
            check_misplaced_obj(&obj, back, loc);
        }
        if (obj != NOTHING)
        {
            // All OK (or all was made OK).
            //
            if (check_type & DBCK_FULL)
            {
                // Check for wizard command-handlers inside nonwiz. Just warn
                // if we find one.
                //
                if (Wizard(obj) && !Wizard(loc))
                {
                    if (Commer(obj))
                    {
                        Log_simple_err(obj, loc,
                            "Wizard command handling object inside nonwizard.");
                    }
                }

                // Check for nonwizard objects inside wizard objects.
                //
                if (  Wizard(loc)
                   && !Wizard(obj)
                   && !Wizard(Owner(obj)))
                {
                    Log_simple_err(obj, loc, "Nonwizard object inside wizard.");
                }
            }
            Mark(obj);
            back = obj;
            obj = Next(obj);
        }
    }
    return;
}

static void NDECL(check_contents_chains)
{
    dbref i;

    Unmark_all(i);
    DO_WHOLE_DB(i)
    {
        check_loc_contents(i);
    }
    DO_WHOLE_DB(i)
    {
        if (!Going(i) && !Marked(i) && Has_location(i))
        {
            Log_simple_err(i, Location(i), "Orphaned object, moved home.");
            s_Location(i, NOTHING); 
            s_Next(i, NOTHING);
            move_via_generic(i, HOME, NOTHING, 0);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * mark_place, check_floating: Look for floating rooms not set FLOATING.
 */

static void mark_place(dbref loc)
{
    dbref exit;

    // If already marked, exit.  Otherwise set marked.
    //
    if (!Good_obj(loc))
    {
        return;
    }
    if (Marked(loc))
    {
        return;
    }
    Mark(loc);

    // Visit all places you can get to via exits from here.
    //
    for (exit = Exits(loc); exit != NOTHING; exit = Next(exit))
    {
        if (Good_obj(Location(exit)))
        {
            mark_place(Location(exit));
        }
    }
}

static NDECL(void check_floating)
{
    dbref owner, i;

    // Mark everyplace you can get to via exits from the starting room.
    //
    Unmark_all(i);
    mark_place(mudconf.start_room);

    // Look for rooms not marked and not set FLOATING.
    //
    DO_WHOLE_DB(i)
    {
        if (  isRoom(i)
           && !Floating(i)
           && !Going(i)
           && !Marked(i))
        {
            owner = Owner(i);
#ifndef STANDALONE
            if (Good_owner(owner))
            {
                notify(owner, tprintf( "You own a floating room: %s(#%d)",
                    Name(i), i));
            }
#else
            Log_simple_err(i, NOTHING, "Disconnected room.");
#endif
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_dbck: Perform a database consistency check and clean up damage.
 */

void do_dbck(dbref executor, dbref caller, dbref enactor, int key)
{
    check_type = key;
    check_dead_refs();
    check_exit_chains();
    check_contents_chains();
    check_floating();
#ifndef STANDALONE
    if (executor != NOTHING)
    {
        Guest.CleanUp();
    }
#endif
    purge_going();
    make_freelist();
#ifndef STANDALONE
    if (  executor != NOTHING
       && !Quiet(executor))
    {
        notify(executor, "Done.");
    }
#endif
}
