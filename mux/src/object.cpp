/*! \file object.cpp
 * \brief Low-level object manipulation routines.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "mathutil.h"
#include "mguests.h"
#include "powers.h"

#define IS_CLEAN(i) (isGarbage(i) && Going(i) && \
             ((i) >= 0) && ((i) < mudstate.db_top) && \
             (Location(i) == NOTHING) && \
             (Contents(i) == NOTHING) && (Exits(i) == NOTHING) && \
             (Next(i) == NOTHING) && (Owner(i) == GOD))

static int check_type;

/*
 * ---------------------------------------------------------------------------
 * * Log_pointer_err, Log_header_err, Log_simple_damage: Write errors to the
 * * log file.
 */

static void Log_pointer_err(dbref prior, dbref obj, dbref loc, dbref ref,
    const UTF8 *reftype, const UTF8 *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG");
    log_type_and_name(obj);
    if (loc != NOTHING)
    {
        log_text(T(" in "));
        log_type_and_name(loc);
    }
    log_text(T(": "));
    if (prior == NOTHING)
    {
        log_text(reftype);
    }
    else
    {
        log_text(T("Next pointer"));
    }
    log_text(T(" "));
    log_type_and_name(ref);
    log_text(T(" "));
    log_text(errtype);
    ENDLOG;
}

static void Log_header_err(dbref obj, dbref loc, dbref val, bool is_object,
    const UTF8 *valtype, const UTF8 *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG");
    log_type_and_name(obj);
    if (loc != NOTHING)
    {
        log_text(T(" in "));
        log_type_and_name(loc);
    }
    log_text(T(": "));
    log_text(valtype);
    log_text(T(" "));
    if (is_object)
    {
        log_type_and_name(val);
    }
    else
    {
        log_number(val);
    }
    log_text(T(" "));
    log_text(errtype);
    ENDLOG;
}

static void Log_simple_err(dbref obj, dbref loc, const UTF8 *errtype)
{
    STARTLOG(LOG_PROBLEMS, "OBJ", "DAMAG");
    log_type_and_name(obj);
    if (loc != NOTHING)
    {
        log_text(T(" in "));
        log_type_and_name(loc);
    }
    log_text(T(": "));
    log_text(errtype);
    ENDLOG;
}

/*
 * ---------------------------------------------------------------------------
 * * start_home, default_home, can_set_home, new_home, clone_home:
 * * Routines for validating and determining homes.
 */

dbref start_home(void)
{
    if (mudconf.start_home != NOTHING)
    {
        return mudconf.start_home;
    }
    return mudconf.start_room;
}

dbref default_home(void)
{
    if (mudconf.default_home != NOTHING)
    {
        return mudconf.default_home;
    }
    if (mudconf.start_home != NOTHING)
    {
        return mudconf.start_home;
    }
    return mudconf.start_room;
}

bool can_set_home(dbref player, dbref thing, dbref home)
{
    if (  !Good_obj(player)
       || !Good_obj(home)
       || thing == home)
    {
        return false;
    }

    switch (Typeof(home))
    {
    case TYPE_PLAYER:
    case TYPE_ROOM:
    case TYPE_THING:
        if (Going(home))
        {
            return false;
        }
        if (  Controls(player, home)
           || Abode(home))
        {
            return true;
        }
    }
    return false;
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
    dbref loc = Home(thing);
    if (can_set_home(Owner(player), player, loc))
    {
        return loc;
    }
    return new_home(player);
}


// Manage A_NEWOBJS stored on each player recording the newest object of
// each type created.  Data is ROOM, THING, EXIT, PLAYER, and the Latest
//
static void update_newobjects(dbref player, dbref object_num, int object_type)
{
    int aowner;
    int aflags;

    UTF8 *newobject_string = atr_get("update_newobjs.189", player, A_NEWOBJS,
            &aowner, &aflags);

    // This leaves room for Room, Thing, Exit, and Player.
    //
    int i;
    dbref object_list[4];
    if (  NULL == newobject_string
       || '\0' == newobject_string[0])
    {
        for (i = 0; i < 4; i++)
        {
            object_list[i] = -1;
        }
    }
    else
    {
        MUX_STRTOK_STATE tts;
        mux_strtok_src(&tts, newobject_string);
        mux_strtok_ctl(&tts, T(" "));

        UTF8* ptr;
        for ( ptr = mux_strtok_parse(&tts), i = 0;
              NULL != ptr && i < 4;
              ptr = mux_strtok_parse(&tts), i++)
        {
            object_list[i] = mux_atol(ptr);
        }
    }

    free_lbuf(newobject_string);

    switch (object_type)
    {
    case TYPE_ROOM:
        object_list[0] = object_num;
        break;

    case TYPE_THING:
        object_list[1] = object_num;
        break;

    case TYPE_EXIT:
        object_list[2] = object_num;
        break;

    case TYPE_PLAYER:
        object_list[3] = object_num;
        break;
    }

    UTF8 tbuf[SBUF_SIZE];
    mux_sprintf(tbuf, SBUF_SIZE, T("%d %d %d %d %d"), object_list[0],
            object_list[1], object_list[2], object_list[3], object_num);
    atr_add_raw(player, A_NEWOBJS, tbuf);
}

// Handle @acreate on master room objects
//
static void ProcessMasterRoomACreate(dbref creator, dbref thing)
{
    int nxargs = 2;
    const UTF8 *xargs[2];

    switch (Typeof(thing))
    {
    case TYPE_ROOM:
        xargs[1] = T("ROOM");
        break;

    case TYPE_EXIT:
        xargs[1] = T("EXIT");
        break;

    case TYPE_PLAYER:
        xargs[1] = T("PLAYER");
        break;

    case TYPE_THING:
        xargs[1] = T("THING");
        break;

    default:
        xargs[1] = T("#-1");
        break;
    }

    dbref master_room_obj = -1;
    DOLIST(master_room_obj, Contents(mudconf.master_room))
    {
        if (thing == master_room_obj)
        {
            break;
        }

        if (Controls(master_room_obj, thing))
        {
            int aowner, aflags;
            UTF8* act = atr_pget(master_room_obj, A_ACREATE, &aowner, &aflags);
            if ('\0' != act[0])
            {
                CLinearTimeAbsolute lta;
                UTF8 buf[SBUF_SIZE];
                mux_sprintf(buf, SBUF_SIZE, T("#%d"), thing);
                xargs[0] = buf;
                wait_que(master_room_obj, creator, creator,
                        AttrTrace(aflags, 0), false, lta, NOTHING, 0, act, nxargs,
                        (const UTF8 **) xargs, mudstate.global_regs);

            }
            free_lbuf(act);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * create_obj: Create an object of the indicated type IF the player can
 * * afford it.
 */

dbref create_obj(dbref player, int objtype, const UTF8 *name, int cost)
{
    dbref obj, owner;
    dbref target_parent  = NOTHING;
    int quota = 0, value = 0;
    size_t nValidName;
    FLAGSET f;
    UTF8 *buff;
    const UTF8 *pValidName;
    const UTF8 *tname;
    bool okname = false, self_owned = false, require_inherit = false;

    switch (objtype)
    {
    case TYPE_ROOM:

        cost = mudconf.digcost;
        quota = mudconf.room_quota;
        f = mudconf.room_flags;
        target_parent = mudconf.room_parent;
        pValidName = MakeCanonicalObjectName(name, &nValidName, &okname, mudconf.room_name_charset);
        tname = T("a room");
        break;

    case TYPE_THING:

        if (cost < mudconf.createmin)
            cost = mudconf.createmin;
        if (cost > mudconf.createmax)
            cost = mudconf.createmax;
        quota = mudconf.thing_quota;
        f = mudconf.thing_flags;
        target_parent = mudconf.thing_parent;
        value = OBJECT_ENDOWMENT(cost);
        pValidName = MakeCanonicalObjectName(name, &nValidName, &okname, mudconf.thing_name_charset);
        tname = T("a thing");
        break;

    case TYPE_EXIT:

        cost = mudconf.opencost;
        quota = mudconf.exit_quota;
        f = mudconf.exit_flags;
        target_parent = mudconf.exit_parent;
        pValidName = MakeCanonicalExitName(name, &nValidName, &okname);
        tname = T("an exit");
        break;

    case TYPE_PLAYER:

        if (cost)
        {
            cost = mudconf.robotcost;
            quota = mudconf.player_quota;
            f = mudconf.robot_flags;
            value = 0;
            tname = T("a robot");
            require_inherit = true;
        }
        else
        {
            cost = 0;
            quota = 0;
            f = mudconf.player_flags;
            value = mudconf.paystart;
            quota = mudconf.start_quota;
            self_owned = true;
            tname = T("a player");
        }
        target_parent = mudconf.player_parent;
        buff = munge_space(name);
        pValidName = name;
        if (!badname_check(buff))
        {
            notify(player, T("That name is not allowed."));
            free_lbuf(buff);
            return NOTHING;
        }
        if (*buff)
        {
            okname = ValidatePlayerName(buff);
            if (!okname)
            {
                notify(player, T("That\xE2\x80\x99s a silly name for a player."));
                free_lbuf(buff);
                return NOTHING;
            }
        }
        if (okname)
        {
            bool bAlias = false;
            okname = (lookup_player_name(buff, bAlias) == NOTHING);
            if (!okname)
            {
                notify(player, tprintf(T("The name %s is already taken."), name));
                free_lbuf(buff);
                return NOTHING;
            }
        }
        free_lbuf(buff);
        break;

    default:
        LOG_SIMPLE(LOG_BUGS, "BUG", "OTYPE", tprintf(T("Bad object type in create_obj: %d."), objtype));
        return NOTHING;
    }

    if (!okname)
    {
        notify(player, tprintf(T("That\xE2\x80\x99s a silly name for %s!"), tname));
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
        if (IS_CLEAN(obj))
        {
            mudstate.freelist = Link(obj);
        }
        else
        {
            LOG_SIMPLE(LOG_PROBLEMS, "FRL", "DAMAG", tprintf(T("Freelist damaged, bad object #%d."), obj));
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

    if (Good_obj(target_parent))
    {
        s_Parent(obj, target_parent);
    }
    else
    {
        s_Parent(obj, NOTHING);
    }

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
    buff = munge_space(pValidName);
    s_Name(obj, buff);
    free_lbuf(buff); buff = NULL;
    db[obj].cpu_time_used.Set100ns(0);

    db[obj].tThrottleExpired.Set100ns(0);
    s_ThAttrib(obj, 0);
    s_ThMail(obj, 0);
    s_ThRefs(obj, 0);

    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    buff = ltaNow.ReturnDateString(7);
    atr_add_raw(obj, A_CREATED, buff);
    atr_add_raw(obj, A_MODIFIED, buff);

    if (objtype == TYPE_PLAYER)
    {
        atr_add_raw(obj, A_LAST, buff);

        buff = alloc_sbuf("create_obj.quota");
        mux_ltoa(quota, buff);
        atr_add_raw(obj, A_QUOTA, buff);
        atr_add_raw(obj, A_RQUOTA, buff);
        add_player_name(obj, Name(obj), false);
        free_sbuf(buff);
        s_Zone(obj, NOTHING);
    }

    if (Good_obj(player))
    {
        update_newobjects(player, obj, objtype);
        ProcessMasterRoomACreate(player, obj);
    }

    return obj;
}

/*
 * ---------------------------------------------------------------------------
 * * destroy_obj: Destroy an object.  Assumes it has already been removed from
 * * all lists and has no contents or exits.
 */

static void destroy_bad_obj(dbref obj)
{
    if (!mudstate.bStandAlone)
    {
        halt_que(NOTHING, obj);
        nfy_que(obj, 0, NFY_DRAIN, 0);
        fwdlist_clr(obj);
#ifdef DEPRECATED
        stack_clr(obj);
#endif // DEPRECATED
        ReleaseAllResources(obj);
    }
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
        if (  (obj >= 0)
           && (obj < mudstate.db_top))
        {
            destroy_bad_obj(obj);
        }
        return;
    }

    // Validate the owner.
    //
    dbref owner = Owner(obj);
    bool good_owner = Good_owner(owner);

    // Halt any pending commands (waiting or semaphore).
    //
    if (!mudstate.bStandAlone)
    {
        if (  halt_que(NOTHING, obj) > 0
           && good_owner
           && !Quiet(obj)
           && !Quiet(owner))
        {
            notify(owner, T("Halted."));
        }
        nfy_que(obj, 0, NFY_DRAIN, 0);

        // Remove forwardlists and stacks.
        //
        fwdlist_clr(obj);
#ifdef DEPRECATED
        stack_clr(obj);
#endif // DEPRECATED
    }

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
            if (Location(obj) == NOTHING)
            {
                val = mudconf.opencost;
            }
            else
            {
                val = mudconf.opencost + mudconf.linkcost;
            }
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
            if (  !Quiet(owner)
               && !Quiet(obj))
            {
                notify(owner,
                    tprintf(T("You get back your %d %s deposit for %s(#%d)."),
                        val, mudconf.one_coin, Moniker(obj), obj));
            }
        }
        if (  mudconf.quotas
           && quota)
        {
            add_quota(owner, quota);
        }
    }
    if (!mudstate.bStandAlone)
    {
        ReleaseAllResources(obj);
    }
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

    local_data_free(obj);

#if defined(TINYMUX_MODULES)
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (NULL != p)
    {
        p->pSink->data_free(obj);
        p = p->pNext;
    }
#endif // TINYMUX_MODULES
}

/*
 * ---------------------------------------------------------------------------
 * * make_freelist: Build a freelist
 */

static void make_freelist(void)
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

    SAFE_DOLIST(curr, temp, Contents(thing))
    {
        if (  !Controls(thing, curr)
           && Has_location(curr)
           && Key(curr))
        {
            if (  !Good_obj(Home(curr))
               || Going(Home(curr)))
            {
                s_Home(curr, new_home(curr));
            }
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
                   T("Funny object type in contents list of GOING location. Flush terminated."));
            break;
        }
        else if (Location(targ) != obj)
        {
            Log_header_err(targ, obj, Location(targ), true,
                   T("Location"),
                   T("indicates object really in another location during cleanup of GOING location.  Flush terminated."));
            break;
        }
        else
        {
            s_Location(targ, NOTHING);
            s_Next(targ, NOTHING);
            if (  !Good_obj(Home(targ))
               || Going(Home(targ))
               || Home(targ) == obj)
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
            Log_simple_err(targ, obj, T("Funny object type in exit list of GOING location. Flush terminated."));
            break;
        }
        else if (Exits(targ) != obj)
        {
            Log_header_err(targ, obj, Exits(targ), true, T("Location"),
                   T("indicates exit really in another location during cleanup of GOING location.  Flush terminated."));
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
    dbref loc = Exits(exit);
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
    // Bye bye...
    //
    boot_off(victim, T("You have been destroyed!"));
    halt_que(victim, NOTHING);
    int count = chown_all(victim, player, player, CHOWN_NOZONE);

    // Remove the name from the name hash table.
    //
    delete_player_name(victim, Name(victim), false);
    dbref aowner;
    int aflags;
    UTF8 *buf = atr_pget(victim, A_ALIAS, &aowner, &aflags);
    delete_player_name(victim, buf, true);
    free_lbuf(buf);

    move_via_generic(victim, NOTHING, player, 0);
    destroy_obj(victim);
    notify_quiet(player, tprintf(T("(%d objects @chowned to you)"), count));
}

static void purge_going(void)
{
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (!Going(i))
        {
            continue;
        }

        const UTF8 *p;
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
                    log_text(T(" in "));
                    log_type_and_name(loc);
                }
                log_text(T("GOING object doesn\xE2\x80\x99t remember its destroyer. GOING reset."));
                ENDLOG;
                db[i].fs.word[FLAG_WORD1] &= ~GOING;
            }
            else
            {
                dbref player = (dbref) mux_atol(p);
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
              T("GOING object with unexpected type.  Destroyed."));
            destroy_obj(i);
        }
    }
}

// ---------------------------------------------------------------------------
// check_dead_refs: Look for references to GOING or illegal objects.
//
static void check_pennies(dbref thing, int limit, const UTF8 *qual)
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
            Log_header_err(thing, NOTHING, j, false, qual, T("is strange.  Reset."));
            s_Pennies(thing, 0);
        }
        else if (j < 0)
        {
            Log_header_err(thing, NOTHING, j, false, qual, T("is negative."));
        }
        else if (limit < j)
        {
            Log_header_err(thing, NOTHING, j, false, qual, T("is excessive."));
        }
    }
    else
    {
        if (isPlayer(thing) || isThing(thing))
        {
            Log_header_err(thing, NOTHING, j, false, qual, T("is zero."));
        }
    }
}

static void check_dead_refs(void)
{
    dbref targ, owner, i, j;
    int aflags;
    UTF8 *str;
    FWDLIST *fp;
    bool dirty;

    DO_WHOLE_DB(i)
    {
        // Check the owner.
        //
        owner = Owner(i);
        if (!Good_obj(owner))
        {
            if (isPlayer(i))
            {
                Log_header_err(i, NOTHING, owner, true, T("Owner"),
                    T("is invalid.  Set to player."));
                owner = i;
            }
            else
            {
                Log_header_err(i, NOTHING, owner, true, T("Owner"),
                    T("is invalid.  Set to GOD."));
                owner = GOD;
            }
            s_Owner(i, owner);
            if (!mudstate.bStandAlone)
            {
                halt_que(NOTHING, i);
            }
            s_Halted(i);
        }
        else if (check_type & DBCK_FULL)
        {
            if (Going(owner))
            {
                if (isPlayer(i))
                {
                    Log_header_err(i, NOTHING, owner, true,
                       T("Owner"), T("is set GOING.  Set to player."));
                    owner = i;
                }
                else
                {
                    Log_header_err(i, NOTHING, owner, true,
                       T("Owner"), T("is set GOING.  Set to GOD."));
                    owner = GOD;
                }
                s_Owner(i, owner);
                if (!mudstate.bStandAlone)
                {
                    halt_que(NOTHING, i);
                }
                s_Halted(i);
            }
            else if (!OwnsOthers(owner))
            {
                if (isPlayer(i))
                {
                    Log_header_err(i, NOTHING, owner, true,
                       T("Owner"), T("is not a valid owner type.  Set to player."));
                    owner = i;
                }
                else
                {
                    Log_header_err(i, NOTHING, owner, true,
                       T("Owner"), T("is not a valid owner type.  Set to GOD."));
                    owner = GOD;
                }
                s_Owner(i, owner);
            }
        }

        // Check the parent
        //
        targ = Parent(i);
        if (Good_obj(targ))
        {
            if (Going(targ))
            {
                s_Parent(i, NOTHING);
                if (!mudstate.bStandAlone)
                {
                    if (  !Quiet(i)
                       && !Quiet(owner))
                    {
                        notify(owner, tprintf(T("Parent cleared on %s(#%d)"),
                            Moniker(i), i));
                    }
                }
                else
                {
                    Log_header_err(i, Location(i), targ, true, T("Parent"),
                        T("is invalid.  Cleared."));
                }
            }
        }
        else if (targ != NOTHING)
        {
            Log_header_err(i, Location(i), targ, true,
                T("Parent"), T("is invalid.  Cleared."));
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
                if (!mudstate.bStandAlone)
                {
                    owner = Owner(i);
                    if (  !Quiet(i)
                       && !Quiet(owner))
                    {
                        notify(owner, tprintf(T("Zone cleared on %s(#%d)"),
                            Moniker(i), i));
                    }
                }
                else
                {
                    Log_header_err(i, Location(i), targ, true, T("Zone"),
                        T("is invalid.  Cleared."));
                }
            }
        }
        else if (targ != NOTHING)
        {
            Log_header_err(i, Location(i), targ, true, T("Zone"),
                T("is invalid.  Cleared."));
            s_Zone(i, NOTHING);
        }

        // Check forwardlist
        //
        fp = fwdlist_get(i);
        dirty = false;
        if (fp)
        {
            for (j = 0; j < fp->count; j++)
            {
                targ = fp->data[j];
                if (  Good_obj(targ)
                   && Going(targ))
                {
                    fp->data[j] = NOTHING;
                    dirty = true;
                }
                else if (  !Good_obj(targ)
                        && targ != NOTHING)
                {
                    fp->data[j] = NOTHING;
                    dirty = true;
                }
            }
        }
        if (dirty)
        {
            str = alloc_lbuf("purge_going");
            (void)fwdlist_rewrite(fp, str);
            atr_get_info(i, A_FORWARDLIST, &owner, &aflags);
            atr_add(i, A_FORWARDLIST, str, owner, aflags);
            free_lbuf(str);
        }

        if (check_type & DBCK_FULL)
        {
            // Check for wizards
            //
            if (RealWizard(i))
            {
                if (isPlayer(i))
                {
                    Log_simple_err(i, NOTHING, T("Player is a WIZARD."));
                }
                if (!Wizard(Owner(i)))
                {
                    Log_header_err(i, NOTHING, Owner(i), true,
                               T("Owner"), T("of a WIZARD object is not a wizard"));
                }
            }
        }

        switch (Typeof(i))
        {
        case TYPE_PLAYER:
            // Check home.
            //
            targ = Home(i);
            if (  !Good_obj(targ)
               || !Has_contents(targ))
            {
                Log_simple_err(i, Location(i), T("Bad home. Reset."));
                s_Home(i, default_home());
            }

            // Check the location.
            //
            targ = Location(i);
            if (  !Good_obj(targ)
               || !Has_contents(targ))
            {
                Log_pointer_err(NOTHING, i, NOTHING, targ, T("Location"),
                    T("is invalid.  Moved to home."));
                move_object(i, Home(i));
            }

            // Check for self-referential Next().
            //
            if (Next(i) == i)
            {
                Log_simple_err(i, NOTHING,
                     T("Next points to self.  Next cleared."));
                s_Next(i, NOTHING);
            }

            if (check_type & DBCK_FULL)
            {
                // Check wealth.
                //
                targ = mudconf.paylimit;
                check_pennies(i, targ, T("Wealth"));
            }
            break;

        case TYPE_THING:

            // Check home.
            //
            targ = Home(i);
            if (  !Good_obj(targ)
               || !Has_contents(targ))
            {
                if (!mudstate.bStandAlone)
                {
                    if (  !Quiet(i)
                       && !Quiet(owner))
                    {
                        notify(owner, tprintf(T("Home reset on %s(#%d)"),
                            Moniker(i), i));
                    }
                    else
                    {
                        Log_header_err(i, Location(i), targ, true, T("Home"),
                            T("is invalid.  Cleared."));
                    }
                }
                s_Home(i, new_home(i));
            }

            // Check the location.
            //
            targ = Location(i);
            if (  !Good_obj(targ)
               || !Has_contents(targ))
            {
                Log_pointer_err(NOTHING, i, NOTHING, targ, T("Location"),
                    T("is invalid.  Moved to home."));
                move_object(i, HOME);
            }

            // Check for self-referential Next().
            //
            if (Next(i) == i)
            {
                Log_simple_err(i, NOTHING,
                    T("Next points to self.  Next cleared."));
                s_Next(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // Check value.
                //
                targ = OBJECT_ENDOWMENT(mudconf.createmax);
                check_pennies(i, targ, T("Value"));
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
                    if (!mudstate.bStandAlone)
                    {
                        if (  !Quiet(i)
                           && !Quiet(owner))
                        {
                            notify(owner, tprintf(T("Dropto removed from %s(#%d)"),
                                Moniker(i), i));
                        }
                    }
                    else
                    {
                        Log_header_err(i, NOTHING, targ, true, T("Dropto"),
                            T("is invalid.  Removed."));
                    }
                }
            }
            else if (  targ != NOTHING
                    && targ != HOME)
            {
                Log_header_err(i, NOTHING, targ, true, T("Dropto"),
                    T("is invalid.  Cleared."));
                s_Dropto(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // NEXT should be null.
                //
                if (Next(i) != NOTHING)
                {
                    Log_header_err(i, NOTHING, Next(i), true, T("Next pointer"),
                        T("should be NOTHING.  Reset."));
                    s_Next(i, NOTHING);
                }

                // LINK should be null.
                //
                if (Link(i) != NOTHING)
                {
                    Log_header_err(i, NOTHING, Link(i), true, T("Link pointer "),
                        T("should be NOTHING.  Reset."));
                    s_Link(i, NOTHING);
                }

                // Check value.
                //
                check_pennies(i, 1, T("Value"));
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
                Log_header_err(i, Exits(i), targ, true, T("Destination"),
                    T("is invalid.  Exit destroyed."));
                s_Going(i);
            }
            else
            {
                if (!Has_contents(targ))
                {
                    Log_header_err(i, Exits(i), targ, true, T("Destination"),
                        T("is not a valid type.  Exit destroyed."));
                    s_Going(i);
                }
            }

            // Check for self-referential Next().
            //
            if (Next(i) == i)
            {
                Log_simple_err(i, NOTHING,
                    T("Next points to self.  Next cleared."));
                s_Next(i, NOTHING);
            }
            if (check_type & DBCK_FULL)
            {
                // CONTENTS should be null.
                //
                if (Contents(i) != NOTHING)
                {
                    Log_header_err(i, Exits(i), Contents(i), true, T("Contents"),
                        T("should be NOTHING.  Reset."));
                    s_Contents(i, NOTHING);
                }

                // LINK should be null.
                //
                if (Link(i) != NOTHING)
                {
                    Log_header_err(i, Exits(i), Link(i), true, T("Link"),
                        T("should be NOTHING.  Reset."));
                    s_Link(i, NOTHING);
                }

                // Check value.
                //
                check_pennies(i, 1, T("Value"));
            }
            break;

        case TYPE_GARBAGE:
            break;

        default:

            // Funny object type, destroy it.
            //
            Log_simple_err(i, NOTHING, T("Funny object type.  Destroyed."));
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
    if (!Good_obj(loc))
    {
        return;
    }

    // Only check players, rooms, and things that aren't GOING.
    //
    if (  isExit(loc)
       || Going(loc))
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
    dbref temp, exitloc, dest;
    dbref back = NOTHING;
    dbref exit = Exits(loc);
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
            Log_pointer_err(back, loc, NOTHING, exit, T("Exit list"),
                T("is invalid.  List nulled."));
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
            Log_pointer_err(back, loc, NOTHING, exit, T("Exitlist member"),
                T("is not an exit.  List terminated."));
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
            Log_pointer_err(back, loc, NOTHING, exit, T("Exitlist member"),
                T("is in another exitlist.  Cleared."));
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
            Log_pointer_err(back, loc, NOTHING, exit, T("Destination"),
                T("is invalid.  Cleared."));
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
                Log_pointer_err(back, loc, NOTHING, exit, T(""),
                    T("is in another exitlist.  List terminated."));
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
                Log_header_err(exit, loc, exitloc, true,
                    T("Not on chain for location"), T("Reset."));
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
                   && !(  Good_dbref(dest)
                       && temp == Owner(dest)))
                {
                    Log_header_err(exit, loc, temp, true, T("Owner"),
                        T("does not own either the source or destination."));
                }
            }
            Mark(exit);
            back = exit;
            exit = Next(exit);
        }
    }
    return;
}

static void check_exit_chains(void)
{
    dbref i;

    Unmark_all(i);
    DO_WHOLE_DB(i)
    {
        check_loc_exits(i);
    }
    DO_WHOLE_DB(i)
    {
        if (  isExit(i)
           && !Marked(i))
        {
            Log_simple_err(i, NOTHING, T("Disconnected exit.  Destroyed."));
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

static void check_loc_contents(dbref);

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
        Log_pointer_err(back, loc, NOTHING, *obj, T(""),
            T("is in another contents list.  Cleared."));
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
        Log_header_err(*obj, loc, Contents(*obj), true, T("Location"),
            T("is invalid.  Reset."));
        s_Contents(*obj, loc);
    }
    return;
}

static void check_loc_contents(dbref loc)
{
    if (!Good_obj(loc))
    {
        return;
    }

    // Only check players, rooms, and things that aren't GOING.
    //
    if (  isExit(loc)
       || Going(loc))
    {
        return;
    }

    dbref back = NOTHING;
    dbref obj = Contents(loc);
    while (obj != NOTHING)
    {
        if (!Good_obj(obj))
        {
            // A bad pointer - terminate chain.
            //
            Log_pointer_err(back, loc, NOTHING, obj, T("Contents list"),
                T("is invalid.  Cleared."));
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
            Log_pointer_err(back, loc, NOTHING, obj, T(""),
                T("is not a player or thing.  Cleared."));
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
                // Check for wizard command-handlers inside non-wizard. Just
                // warn if we find one.
                //
                if (  RealWizard(obj)
                   && !Wizard(loc))
                {
                    if (Commer(obj))
                    {
                        Log_simple_err(obj, loc,
                            T("Wizard command handling object inside non-wizard."));
                    }
                }

                // Check for non-wizard objects inside wizard objects.
                //
                if (  RealWizard(loc)
                   && !Wizard(obj)
                   && !Wizard(Owner(obj)))
                {
                    Log_simple_err(obj, loc, T("Non-wizard object inside wizard."));
                }
            }
            Mark(obj);
            back = obj;
            obj = Next(obj);
        }
    }
}

static void check_contents_chains(void)
{
    dbref i;

    Unmark_all(i);
    DO_WHOLE_DB(i)
    {
        check_loc_contents(i);
    }
    DO_WHOLE_DB(i)
    {
        if (  !Going(i)
           && !Marked(i)
           && Has_location(i))
        {
            Log_simple_err(i, Location(i), T("Orphaned object, moved home."));
            s_Location(i, NOTHING);
            s_Next(i, NOTHING);
            if (  !Good_obj(Home(i))
               || Going(Home(i)))
            {
                s_Home(i, new_home(i));
            }
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
    // If already marked, exit.  Otherwise set marked.
    //
    if (  !Good_obj(loc)
       || Marked(loc))
    {
        return;
    }
    Mark(loc);

    // Visit all places you can get to via exits from here.
    //
    dbref exit;
    for (exit = Exits(loc); exit != NOTHING; exit = Next(exit))
    {
        if (Good_obj(Location(exit)))
        {
            mark_place(Location(exit));
        }
    }
}

static void check_floating(void)
{
    dbref i;

    // Mark everyplace you can get to via exits from the starting room.
    //
    Unmark_all(i);
    mark_place(mudconf.start_room);

    // Mark every room you can get to from a floating room.
    //
    DO_WHOLE_DB(i)
    {
        if (  isRoom(i)
           && Floating(i)
           && !Going(i))
        {
            mark_place(i);
        }
    }

    // Look for unvisited rooms.
    //
    DO_WHOLE_DB(i)
    {
        if (  isRoom(i)
           && !Going(i)
           && !Marked(i))
        {
            if (!mudstate.bStandAlone)
            {
                dbref owner = Owner(i);
                if (Good_owner(owner))
                {
                    notify(owner, tprintf(T("You own a floating room: %s(#%d)"),
                        Moniker(i), i));
                }
            }
            else
            {
                Log_simple_err(i, NOTHING, T("Disconnected room."));
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_dbck: Perform a database consistency check and clean up damage.
 */

void do_dbck(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);

    check_type = key;
    check_dead_refs();
    check_exit_chains();
    check_contents_chains();
    check_floating();
    if (  !mudstate.bStandAlone
       && executor != NOTHING)
    {
        Guest.CleanUp();
    }
    purge_going();
    make_freelist();
    scheduler.Shrink();
#if defined(TINYMUX_MODULES)
    mux_ModuleMaintenance();
#if defined(STUB_SLAVE)
    if (NULL != mudstate.pISlaveControl)
    {
        mudstate.pISlaveControl->ModuleMaintenance();
    }
#endif // STUB_SLAVE
#endif // TINYMUX_MODULES

    // Allow the local extensions to do data checks.
    //
    local_dbck();
#if defined(TINYMUX_MODULES)
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (NULL != p)
    {
        p->pSink->dbck();
        p = p->pNext;
    }
#endif // TINYMUX_MODULES

    if (  !mudstate.bStandAlone
       && executor != NOTHING
       && !Quiet(executor))
    {
        notify(executor, T("Done."));
    }
}
