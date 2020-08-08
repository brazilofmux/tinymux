/*! \file walkdb.cpp
 * \brief Support for commands that walk the entire db.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "mathutil.h"
#include "misc.h"

// Bind occurances of the universal var in ACTION to ARG, then run ACTION.
// Cmds run in low-prio Q after a 1 sec delay for the first one.
//
static void bind_and_queue(dbref executor, dbref caller, dbref enactor,
                           int eval, UTF8 *action, UTF8 *argstr,
                           const UTF8 *cargs[], int ncargs, int number)
{
    UTF8 *command = replace_tokens(action, argstr, mux_ltoa_t(number), nullptr);
    CLinearTimeAbsolute lta;
    wait_que(executor, caller, enactor, eval, false, lta, NOTHING, 0,
        command,
        ncargs, cargs,
        mudstate.global_regs);
    free_lbuf(command);
}

// New @dolist.  i.e.:
// @dolist #12 #34 #45 #123 #34644=@emit [name(##)]
//
// New switches added 12/92, /space (default) delimits list using spaces,
// and /delimit allows specification of a delimiter.
//
void do_dolist(dbref executor, dbref caller, dbref enactor, int eval, int key,
               int nargs, UTF8 *list, UTF8 *command, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(nargs);

    if (!list || *list == '\0')
    {
        notify(executor, T("That\xE2\x80\x99s terrific, but what should I do with the list?"));
        return;
    }
    UTF8 *objstring, delimiter = ' ';
    int number = 0;
    UTF8 *curr = list;

    if (key & DOLIST_DELIMIT)
    {
        UTF8 *tempstr = parse_to(&curr, ' ', EV_STRIP_CURLY);
        if (1 < strlen((char *)tempstr))
        {
            notify(executor, T("The delimiter must be a single character!"));
            return;
        }
        delimiter = *tempstr;
    }
    while (curr && *curr)
    {
        while (*curr == delimiter)
        {
            curr++;
        }
        if (*curr)
        {
            number++;
            objstring = parse_to(&curr, delimiter, EV_STRIP_CURLY);
            bind_and_queue(executor, caller, enactor, eval, command, objstring,
                cargs, ncargs, number);
        }
    }

    if (key & DOLIST_NOTIFY)
    {
        UTF8 *tbuf = alloc_lbuf("dolist.notify_cmd");
        mux_strncpy(tbuf, T("@notify/quiet me"), LBUF_SIZE-1);
        CLinearTimeAbsolute lta;
        wait_que(executor, caller, enactor, eval, false, lta, NOTHING,
            A_SEMAPHORE,
            tbuf,
            ncargs, cargs,
            mudstate.global_regs);
        free_lbuf(tbuf);
    }
}

// Regular @find command
//
void do_find(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *buff;

    if (!payfor(executor, mudconf.searchcost))
    {
        buff = tprintf(T("You don\xE2\x80\x99t have enough %s."), mudconf.many_coins);
        notify_quiet(executor, buff);
        return;
    }

    dbref i, low_bound, high_bound;
    parse_range(&name, &low_bound, &high_bound);
    for (i = low_bound; i <= high_bound; i++)
    {
        if (  (Typeof(i) != TYPE_EXIT)
           && Controls(executor, i)
           && (!*name || string_match(PureName(i), name)))
        {
            buff = unparse_object(executor, i, false);
            notify(executor, buff);
            free_lbuf(buff);
        }
    }
    notify(executor, T("***End of List***"));
}

// ---------------------------------------------------------------------------
// get_stats, do_stats: Get counts of items in the db.
//
bool get_stats(dbref player, dbref who, STATS *info)
{
    // Do we have permission?
    //
    if (Good_obj(who) && !Controls(player, who) && !Stat_Any(player))
    {
        notify(player, NOPERM_MESSAGE);
        return false;
    }

    // Can we afford it?
    //
    if (!payfor(player, mudconf.searchcost))
    {
        notify(player, tprintf(T("You don\xE2\x80\x99t have enough %s."), mudconf.many_coins));
        return false;
    }
    info->s_total = 0;
    info->s_rooms = 0;
    info->s_exits = 0;
    info->s_things = 0;
    info->s_players = 0;
    info->s_garbage = 0;

    dbref i;
    DO_WHOLE_DB(i)
    {
        if ((who == NOTHING) || (who == Owner(i)))
        {
            info->s_total++;
            if (Going(i) && (Typeof(i) != TYPE_ROOM))
            {
                info->s_garbage++;
                continue;
            }
            switch (Typeof(i))
            {
            case TYPE_ROOM:

                info->s_rooms++;
                break;

            case TYPE_EXIT:

                info->s_exits++;
                break;

            case TYPE_THING:

                info->s_things++;
                break;

            case TYPE_PLAYER:

                info->s_players++;
                break;

            default:

                info->s_garbage++;
            }
        }
    }
    return true;
}

// Reworked by R'nice
//
void do_stats(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref owner;

    switch (key)
    {
    case STAT_ALL:

        owner = NOTHING;
        break;

    case STAT_ME:

        owner = Owner(executor);
        break;

    case STAT_PLAYER:

        if (!(name && *name))
        {
            int nNextFree = mudstate.freelist;
            if (mudstate.freelist == NOTHING)
            {
                nNextFree = mudstate.db_top;
            }
            notify(executor, tprintf(T("The universe contains %d objects (next free is #%d)."),
                mudstate.db_top, nNextFree));
            return;
        }
        owner = lookup_player(executor, name, true);
        if (owner == NOTHING)
        {
            notify(executor, T("Not found."));
            return;
        }
        break;

    default:

        notify(executor, T("Illegal combination of switches."));
        return;
    }

    STATS statinfo;
    if (!get_stats(executor, owner, &statinfo))
    {
        return;
    }
    notify(executor,
        tprintf(T("%d objects = %d rooms, %d exits, %d things, %d players. (%d garbage)"),
            statinfo.s_total, statinfo.s_rooms, statinfo.s_exits,
            statinfo.s_things, statinfo.s_players,
            statinfo.s_garbage));
}

int chown_all(dbref from_player, dbref to_player, dbref acting_player, int key)
{
    if (!isPlayer(from_player))
    {
        from_player = Owner(from_player);
    }

    if (!isPlayer(to_player))
    {
        to_player = Owner(to_player);
    }

    int count = 0;
    if (  God(from_player)
       && !God(acting_player))
    {
        notify(acting_player, NOPERM_MESSAGE);
    }
    else
    {
        int i;
        int quota_out = 0;
        int quota_in  = 0;

        FLAGSET clearflags;
        FLAGSET setflags;
        bool    bClearPowers;

        TranslateFlags_Chown(clearflags.word, setflags.word, &bClearPowers, acting_player, key);

        DO_WHOLE_DB(i)
        {
            if (  Owner(i) == from_player
               && Owner(i) != i)
            {
                switch (Typeof(i))
                {
                case TYPE_PLAYER:

                    s_Owner(i, i);
                    quota_out += mudconf.player_quota;
                    break;

                case TYPE_THING:

                    s_Owner(i, to_player);
                    quota_out += mudconf.thing_quota;
                    quota_in -= mudconf.thing_quota;
                    break;

                case TYPE_ROOM:

                    s_Owner(i, to_player);
                    quota_out += mudconf.room_quota;
                    quota_in -= mudconf.room_quota;
                    break;

                case TYPE_EXIT:

                    s_Owner(i, to_player);
                    quota_out += mudconf.exit_quota;
                    quota_in -= mudconf.exit_quota;
                    break;

                default:

                    s_Owner(i, to_player);
                }

                if (key & CHOWN_NOZONE)
                {
                    s_Zone(i, NOTHING);
                }

                SetClearFlags(i, clearflags.word, setflags.word);
                if (bClearPowers)
                {
                    s_Powers(i, 0);
                    s_Powers2(i, 0);
                }

                // Always halt the queue.
                //
                halt_que(NOTHING, i);

                count++;
            }
        }
        add_quota(from_player, quota_out);
        add_quota(to_player, quota_in);
    }
    return count;
}

void do_chownall
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *from,
    UTF8 *to,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    init_match(executor, from, TYPE_PLAYER);
    match_neighbor();
    match_absolute();
    match_player();

    dbref victim = noisy_match_result();
    if (NOTHING == victim)
    {
        return;
    }
    else if (!isPlayer(victim))
    {
        notify(executor, T("Victim must be a player."));
        return;
    }

    dbref recipient = executor;
    if (  nullptr  != to
       && to[0] != '\0')
    {
        init_match(executor, to, TYPE_PLAYER);
        match_neighbor();
        match_absolute();
        match_player();
        recipient = noisy_match_result();
        if (NOTHING == recipient)
        {
            return;
        }
    }

    int count = chown_all(victim, recipient, executor, key);
    if (!Quiet(executor))
    {
        notify(executor, tprintf(T("%d objects @chowned."), count));
    }
}

#define ANY_OWNER (-2)

static void er_mark_disabled(dbref player)
{
    notify(player,
     T("The mark commands are not allowed while DB cleaning is enabled."));
    notify(player,
     T("Use the \xE2\x80\x98@disable cleaning\xE2\x80\x99 command to disable automatic cleaning."));
    notify(player,
     T("Remember to \xE2\x80\x98@unmark_all\xE2\x80\x99 before re-enabling automatic cleaning."));
}


// ---------------------------------------------------------------------------
// do_search: Walk the db reporting various things (or setting/clearing mark
// bits)
//
bool search_setup(dbref player, UTF8 *searchfor, SEARCH *parm)
{
    // Crack arg into <pname> <type>=<targ>,<low>,<high>
    //
    UTF8 *pname = parse_to(&searchfor, '=', EV_STRIP_TS);
    if (!pname || !*pname)
    {
        pname = (UTF8 *)"me";
    }
    else
    {
        size_t nCased;
        pname = mux_strlwr(pname, nCased);
    }

    UTF8 *searchtype;
    if (searchfor && *searchfor)
    {
        searchtype = (UTF8 *)strrchr((char *)pname, ' ');
        if (searchtype)
        {
            *searchtype++ = '\0';
        }
        else
        {
            searchtype = pname;
            pname = (UTF8 *)"";
        }
    }
    else
    {
        searchtype = (UTF8 *)"";
    }

    // If the player name is quoted, strip the quotes.
    //
    if (*pname == '"')
    {
        size_t k = strlen((char *)pname) - 1;
        if (pname[k] == '"')
        {
            pname[k] = '\0';
            pname++;
        }
    }

    // Strip any range arguments.
    //
    parse_range(&searchfor, &parm->low_bound, &parm->high_bound);


    // Set limits on who we search.
    //
    parm->s_owner = Owner(player);
    parm->s_wizard = Search(player);
    parm->s_rst_owner = NOTHING;
    if (!*pname)
    {
        parm->s_rst_owner = parm->s_wizard ? ANY_OWNER : player;
    }
    else if (pname[0] == '#')
    {
        parm->s_rst_owner = mux_atol(&pname[1]);
        if (!Good_obj(parm->s_rst_owner))
        {
            parm->s_rst_owner = NOTHING;
        }
        else if (Typeof(parm->s_rst_owner) != TYPE_PLAYER)
        {
            parm->s_rst_owner = NOTHING;
        }

    }
    else if (strcmp((char *)pname, "me") == 0)
    {
        parm->s_rst_owner = player;
    }
    else
    {
        parm->s_rst_owner = lookup_player(player, pname, true);
    }

    if (parm->s_rst_owner == NOTHING)
    {
        notify(player, tprintf(T("%s: No such player"), pname));
        return false;
    }

    // Set limits on what we search for.
    //
    int err = 0;
    parm->s_rst_name = nullptr;
    parm->s_rst_eval = nullptr;
    parm->s_rst_type = NOTYPE;
    parm->s_parent = NOTHING;
    parm->s_zone = NOTHING;
    for (int i = FLAG_WORD1; i <= FLAG_WORD3; i++)
    {
        parm->s_fset.word[i] = 0;
    }
    parm->s_pset.word1 = 0;
    parm->s_pset.word2 = 0;

    switch (searchtype[0])
    {
    case '\0':

        // The no class requested class  :)
        //
        break;

    case 'e':

        if (string_prefix(T("exits"), searchtype))
        {
            parm->s_rst_name = searchfor;
            parm->s_rst_type = TYPE_EXIT;
        }
        else if (string_prefix(T("evaluate"), searchtype))
        {
            parm->s_rst_eval = searchfor;
        }
        else if (string_prefix(T("eplayer"), searchtype))
        {
            parm->s_rst_type = TYPE_PLAYER;
            parm->s_rst_eval = searchfor;
        }
        else if (string_prefix(T("eroom"), searchtype))
        {
            parm->s_rst_type = TYPE_ROOM;
            parm->s_rst_eval = searchfor;
        }
        else if (string_prefix(T("eobject"), searchtype))
        {
            parm->s_rst_type = TYPE_THING;
            parm->s_rst_eval = searchfor;
        }
        else if (string_prefix(T("ething"), searchtype))
        {
            parm->s_rst_type = TYPE_THING;
            parm->s_rst_eval = searchfor;
        }
        else if (string_prefix(T("eexit"), searchtype))
        {
            parm->s_rst_type = TYPE_EXIT;
            parm->s_rst_eval = searchfor;
        }
        else
        {
            err = 1;
        }
        break;

    case 'f':

        if (string_prefix(T("flags"), searchtype))
        {
            // convert_flags ignores previous values of flag_mask and
            // s_rst_type while setting them.
            //
            if ( !convert_flags( player, searchfor, &parm->s_fset,
                                &parm->s_rst_type) )
            {
                return false;
            }
        }
        else
        {
            err = 1;
        }
        break;

    case 'n':

        if (string_prefix(T("name"), searchtype))
        {
            parm->s_rst_name = searchfor;
        }
        else
        {
            err = 1;
        }
        break;

    case 'o':

        if (string_prefix(T("objects"), searchtype))
        {
            parm->s_rst_name = searchfor;
            parm->s_rst_type = TYPE_THING;
        }
        else
        {
            err = 1;
        }
        break;

    case 'p':

        if (string_prefix(T("players"), searchtype))
        {
            parm->s_rst_name = searchfor;
            parm->s_rst_type = TYPE_PLAYER;
            if (!*pname)
            {
                parm->s_rst_owner = ANY_OWNER;
            }
        }
        else if (string_prefix(T("parent"), searchtype))
        {
            parm->s_parent = match_controlled(player, searchfor);
            if (!Good_obj(parm->s_parent))
            {
                return false;
            }
            if (!*pname)
            {
                parm->s_rst_owner = ANY_OWNER;
            }
        }
        else if (string_prefix(T("power"), searchtype))
        {
            if (!decode_power(player, searchfor, &parm->s_pset))
            {
                return false;
            }
        }
        else
        {
            err = 1;
        }
        break;

    case 'r':

        if (string_prefix(T("rooms"), searchtype))
        {
            parm->s_rst_name = searchfor;
            parm->s_rst_type = TYPE_ROOM;
        }
        else
        {
            err = 1;
        }
        break;

    case 't':

        if (string_prefix(T("type"), searchtype))
        {
            if (searchfor[0] == '\0')
            {
                break;
            }
            if (string_prefix(T("rooms"), searchfor))
            {
                parm->s_rst_type = TYPE_ROOM;
            }
            else if (string_prefix(T("exits"), searchfor))
            {
                parm->s_rst_type = TYPE_EXIT;
            }
            else if (string_prefix(T("objects"), searchfor))
            {
                parm->s_rst_type = TYPE_THING;
            }
            else if (string_prefix(T("things"), searchfor))
            {
                parm->s_rst_type = TYPE_THING;
            }
            else if (string_prefix(T("players"), searchfor))
            {
                parm->s_rst_type = TYPE_PLAYER;
                if (!*pname)
                {
                    parm->s_rst_owner = ANY_OWNER;
                }
            }
            else
            {
                notify(player, tprintf(T("%s: unknown type"), searchfor));
                return false;
            }
        }
        else if (string_prefix(T("things"), searchtype))
        {
            parm->s_rst_name = searchfor;
            parm->s_rst_type = TYPE_THING;
        }
        else
        {
            err = 1;
        }
        break;

    case 'z':

        if (string_prefix(T("zone"), searchtype))
        {
            parm->s_zone = match_controlled(player, searchfor);
            if (!Good_obj(parm->s_zone))
            {
                return false;
            }
            if (!*pname)
            {
                parm->s_rst_owner = ANY_OWNER;
            }
        }
        else
        {
            err = 1;
        }
        break;

    default:

        err = 1;
    }

    if (err)
    {
        notify(player, tprintf(T("%s: unknown class"), searchtype));
        return false;
    }

    // Make sure player is authorized to do the search.
    //
    if (  !parm->s_wizard
       && (parm->s_rst_type != TYPE_PLAYER)
       && (parm->s_rst_owner != player)
       && (parm->s_rst_owner != ANY_OWNER))
    {
        notify(player, T("You need a search warrant to do that!"));
        return false;
    }

    // Make sure player has money to do the search.
    //
    if (!payfor(player, mudconf.searchcost))
    {
        notify(player,
            tprintf(T("You don\xE2\x80\x99t have enough %s to search. (You need %d)"),
                 mudconf.many_coins, mudconf.searchcost));
        return false;
    }
    return true;
}

void search_perform(dbref executor, dbref caller, dbref enactor, SEARCH *parm)
{
    POWER thing1powers, thing2powers;
    UTF8 *result, *bp;

    UTF8 *buff = alloc_sbuf("search_perform.num");
    int save_invk_ctr = mudstate.func_invk_ctr;

    dbref thing;
    for (thing = parm->low_bound; thing <= parm->high_bound; thing++)
    {
        mudstate.func_invk_ctr = save_invk_ctr;

        // Check for matching type.
        //
        if (  (parm->s_rst_type != NOTYPE)
           && (parm->s_rst_type != Typeof(thing)))
        {
            continue;
        }

        // Check for matching owner.
        //
        if (  (parm->s_rst_owner != ANY_OWNER)
           && (parm->s_rst_owner != Owner(thing)))
        {
            continue;
        }

        // Toss out destroyed things.
        //
        if (Going(thing))
        {
            continue;
        }

        // Check for matching parent.
        //
        if (  (parm->s_parent != NOTHING)
           && (parm->s_parent != Parent(thing)))
        {
            continue;
        }

        // Check for matching zone.
        //
        if (  (parm->s_zone != NOTHING)
           && (parm->s_zone != Zone(thing)))
        {
            continue;
        }

        // Check for matching flags.
        //
        bool b = false;
        for (int i = FLAG_WORD1; i <= FLAG_WORD3; i++)
        {
            FLAG f = parm->s_fset.word[i];
            if ((db[thing].fs.word[i] & f) != f)
            {
                b = true;
                break;
            }
        }
        if (b)
        {
            continue;
        }

        // Check for matching power.
        //
        thing1powers = Powers(thing);
        thing2powers = Powers2(thing);
        if ((thing1powers & parm->s_pset.word1) != parm->s_pset.word1)
        {
            continue;
        }
        if ((thing2powers & parm->s_pset.word2) != parm->s_pset.word2)
        {
            continue;
        }

        // Check for matching name.
        //
        if (parm->s_rst_name != nullptr)
        {
            if (!string_prefix(PureName(thing), parm->s_rst_name))
                continue;
        }

        // Check for successful evaluation.
        //
        if (parm->s_rst_eval != nullptr)
        {
            buff[0] = '#';
            mux_ltoa(thing, buff+1);
            UTF8 *buff2 = replace_tokens(parm->s_rst_eval, buff, nullptr, nullptr);
            result = bp = alloc_lbuf("search_perform");
            mux_exec(buff2, LBUF_SIZE-1, result, &bp, executor, caller, enactor,
                EV_FCHECK | EV_EVAL | EV_NOTRACE, nullptr, 0);
            *bp = '\0';
            free_lbuf(buff2);
            if (!*result || !xlate(result))
            {
                free_lbuf(result);
                continue;
            }
            free_lbuf(result);
        }

        // It passed everything. Amazing.
        //
        olist_add(thing);
    }
    free_sbuf(buff);
    mudstate.func_invk_ctr = save_invk_ctr;
}

static void search_mark(dbref player, int key)
{
    dbref thing;
    bool is_marked;

    int nchanged = 0;
    for (thing = olist_first(); thing != NOTHING; thing = olist_next())
    {
        is_marked = Marked(thing);

        // Don't bother checking if marking and already marked (or if
        // unmarking and not marked)
        //
        if (  ((key == SRCH_MARK) && is_marked)
           || ((key == SRCH_UNMARK) && !is_marked))
        {
            continue;
        }

        // Toggle the mark bit and update the counters.
        //
        if (key == SRCH_MARK)
        {
            Mark(thing);
            nchanged++;
        }
        else
        {
            Unmark(thing);
            nchanged++;
        }
    }
    notify( player, tprintf(T("%d objects %smarked"), nchanged,
            ((key == SRCH_MARK) ? "" : "un")) );
    return;
}

void do_search(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *buff, *outbuf, *bp;
    dbref thing, from, to;
    SEARCH searchparm;

    if ((key != SRCH_SEARCH) && (mudconf.control_flags & CF_DBCHECK))
    {
        er_mark_disabled(executor);
        return;
    }
    if (!search_setup(executor, arg, &searchparm))
    {
        return;
    }
    olist_push();
    search_perform(executor, caller, enactor, &searchparm);
    bool destitute = true;
    bool flag;

    // If we are doing a @mark command, handle that here.
    //
    if (key != SRCH_SEARCH)
    {
        search_mark(executor, key);
        olist_pop();
        return;
    }
    outbuf = alloc_lbuf("do_search.outbuf");

    int rcount = 0;
    int ecount = 0;
    int tcount = 0;
    int pcount = 0;

    // Room search.
    //
    if (  searchparm.s_rst_type == TYPE_ROOM
       || searchparm.s_rst_type == NOTYPE)
    {
        flag = true;
        for (thing = olist_first(); thing != NOTHING; thing = olist_next())
        {
            if (Typeof(thing) != TYPE_ROOM)
            {
                continue;
            }
            if (flag)
            {
                flag = false;
                destitute = false;
                notify(executor, T("\nROOMS:"));
            }
            buff = unparse_object(executor, thing, false);
            notify(executor, buff);
            free_lbuf(buff);
            rcount++;
        }
    }

    // Exit search.
    //
    if (  searchparm.s_rst_type == TYPE_EXIT
       || searchparm.s_rst_type == NOTYPE)
    {
        flag = true;
        for (thing = olist_first(); thing != NOTHING; thing = olist_next())
        {
            if (Typeof(thing) != TYPE_EXIT)
            {
                continue;
            }
            if (flag)
            {
                flag = false;
                destitute = false;
                notify(executor, T("\nEXITS:"));
            }
            from = Exits(thing);
            to = Location(thing);

            bp = outbuf;
            buff = unparse_object(executor, thing, false);
            safe_str(buff, outbuf, &bp);
            free_lbuf(buff);

            safe_str(T(" [from "), outbuf, &bp);
            buff = unparse_object(executor, from, false);
            safe_str(((from == NOTHING) ? T("NOWHERE") : buff), outbuf, &bp);
            free_lbuf(buff);

            safe_str(T(" to "), outbuf, &bp);
            buff = unparse_object(executor, to, false);
            safe_str(((to == NOTHING) ? T("NOWHERE") : buff), outbuf, &bp);
            free_lbuf(buff);

            safe_chr(']', outbuf, &bp);
            *bp = '\0';
            notify(executor, outbuf);
            ecount++;
        }
    }

    // Object search
    //
    if (  searchparm.s_rst_type == TYPE_THING
       || searchparm.s_rst_type == NOTYPE)
    {
        flag = true;
        for (thing = olist_first(); thing != NOTHING; thing = olist_next())
        {
            if (Typeof(thing) != TYPE_THING)
            {
                continue;
            }
            if (flag)
            {
                flag = false;
                destitute = false;
                notify(executor, T("\nOBJECTS:"));
            }
            bp = outbuf;
            buff = unparse_object(executor, thing, false);
            safe_str(buff, outbuf, &bp);
            free_lbuf(buff);

            safe_str(T(" [owner: "), outbuf, &bp);
            buff = unparse_object(executor, Owner(thing), false);
            safe_str(buff, outbuf, &bp);
            free_lbuf(buff);

            safe_chr(']', outbuf, &bp);
            *bp = '\0';
            notify(executor, outbuf);
            tcount++;
        }
    }

    // Player search
    //
    if (  searchparm.s_rst_type == TYPE_PLAYER
       || searchparm.s_rst_type == NOTYPE)
    {
        flag = true;
        for (thing = olist_first(); thing != NOTHING; thing = olist_next())
        {
            if (Typeof(thing) != TYPE_PLAYER)
            {
                continue;
            }
            if (flag)
            {
                flag = false;
                destitute = false;
                notify(executor, T("\nPLAYERS:"));
            }
            bp = outbuf;
            buff = unparse_object(executor, thing, 0);
            safe_str(buff, outbuf, &bp);
            free_lbuf(buff);
            if (searchparm.s_wizard)
            {
                safe_str(T(" [location: "), outbuf, &bp);
                buff = unparse_object(executor, Location(thing), false);
                safe_str(buff, outbuf, &bp);
                free_lbuf(buff);
                safe_chr(']', outbuf, &bp);
            }
            *bp = '\0';
            notify(executor, outbuf);
            pcount++;
        }
    }

    // If nothing found matching search criteria.
    //
    if (destitute)
    {
        notify(executor, T("Nothing found."));
    }
    else
    {
        mux_sprintf(outbuf, LBUF_SIZE,
            T("\nFound:  Rooms...%d  Exits...%d  Objects...%d  Players...%d"),
            rcount, ecount, tcount, pcount);
        notify(executor, outbuf);
    }
    free_lbuf(outbuf);
    olist_pop();
}

// ---------------------------------------------------------------------------
// do_markall: set or clear the mark bits of all objects in the db.
//
void do_markall(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);

    int i;

    if (mudconf.control_flags & CF_DBCHECK)
    {
        er_mark_disabled(executor);
        return;
    }
    if (key == MARK_SET)
    {
        Mark_all(i);
    }
    else if (key == MARK_CLEAR)
    {
        Unmark_all(i);
    }
    if (!Quiet(executor))
    {
        notify(executor, T("Done."));
    }
}

// ---------------------------------------------------------------------------
// do_apply_marked: Perform a command for each marked obj in the db.
//
void do_apply_marked( dbref executor, dbref caller, dbref enactor, int eval,
                      int key, UTF8 *command, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    if (mudconf.control_flags & CF_DBCHECK)
    {
        er_mark_disabled(executor);
        return;
    }
    UTF8 *buff = alloc_sbuf("do_apply_marked");
    int i;
    int number = 0;
    DO_WHOLE_DB(i)
    {
        if (Marked(i))
        {
            buff[0] = '#';
            mux_ltoa(i, buff+1);
            number++;
            bind_and_queue(executor, caller, enactor, eval, command, buff,
                cargs, ncargs, number);
        }
    }
    free_sbuf(buff);
    if (!Quiet(executor))
    {
        notify(executor, T("Done."));
    }
}

// ---------------------------------------------------------------------------
// Object list management routines: olist_push, olist_pop, olist_add,
//   olist_first, olist_next
//

// olist_push: Create a new object list at the top of the object list stack.
//
void olist_push(void)
{
    OLSTK *ol = nullptr;
    try
    {
        ol = new OLSTK;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != ol)
    {
        ol->next = mudstate.olist;
        mudstate.olist = ol;

        ol->head = nullptr;
        ol->tail = nullptr;
        ol->cblock = nullptr;
        ol->count = 0;
        ol->citm = 0;
    }
    else
    {
        ISOUTOFMEMORY(ol);
    }
}

// olist_pop: Pop one entire list off the object list stack.
//
void olist_pop(void)
{
    OLSTK *ol = mudstate.olist->next;
    OBLOCK *op, *onext;
    for (op = mudstate.olist->head; op != nullptr; op = onext)
    {
        onext = op->next;
        free_lbuf(op);
    }
    delete mudstate.olist;
    mudstate.olist = ol;
}

// olist_add: Add an entry to the object list.
//
void olist_add(dbref item)
{
    OBLOCK *op;

    if (!mudstate.olist->head)
    {
        op = (OBLOCK *) alloc_lbuf("olist_add.first");
        mudstate.olist->head = mudstate.olist->tail = op;
        mudstate.olist->count = 0;
        op->next = nullptr;
    }
    else if (mudstate.olist->count >= OBLOCK_SIZE)
    {
        op = (OBLOCK *) alloc_lbuf("olist_add.next");
        mudstate.olist->tail->next = op;
        mudstate.olist->tail = op;
        mudstate.olist->count = 0;
        op->next = nullptr;
    }
    else
    {
        op = mudstate.olist->tail;
    }
    op->data[mudstate.olist->count++] = item;
}

// olist_first: Return the first entry in the object list.
//
dbref olist_first(void)
{
    if (!mudstate.olist->head)
    {
        return NOTHING;
    }
    if (  (mudstate.olist->head == mudstate.olist->tail)
       && (mudstate.olist->count == 0))
    {
        return NOTHING;
    }
    mudstate.olist->cblock = mudstate.olist->head;
    mudstate.olist->citm = 0;
    return mudstate.olist->cblock->data[mudstate.olist->citm++];
}

dbref olist_next(void)
{
    if (!mudstate.olist->cblock)
    {
        return NOTHING;
    }
    if (  (mudstate.olist->cblock == mudstate.olist->tail)
       && (mudstate.olist->citm >= mudstate.olist->count))
    {
        return NOTHING;
    }
    dbref thing = mudstate.olist->cblock->data[mudstate.olist->citm++];
    if (mudstate.olist->citm >= OBLOCK_SIZE)
    {
        mudstate.olist->cblock = mudstate.olist->cblock->next;
        mudstate.olist->citm = 0;
    }
    return thing;
}

#define HOURS_PER_PERIOD  4
#define DAYS_PER_REPORT   (2*7)
#define NPERIODS          (24*DAYS_PER_REPORT/HOURS_PER_PERIOD)

void do_report(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    UTF8 *buff = alloc_mbuf("do_report");
    int nBin[NPERIODS];
    int i;

    for (i = 0; i < NPERIODS; i++)
    {
        nBin[i] = 0;
    }

    CLinearTimeAbsolute ltaNow, ltaPlayer;
    ltaNow.GetLocal();

    const int SecondsPerPeriod = HOURS_PER_PERIOD*60*60;

    int iPlayer;
    DO_WHOLE_DB(iPlayer)
    {
        if (isPlayer(iPlayer))
        {
            int aowner, aflags;
            UTF8 *player_last = atr_get("do_report.43", iPlayer, A_LAST, &aowner, &aflags);

            if (ltaPlayer.SetString(player_last))
            {
                CLinearTimeDelta ltd(ltaPlayer, ltaNow);
                int ltdSeconds = ltd.ReturnSeconds();
                int iBin = ltdSeconds / SecondsPerPeriod;
                if (0 <= iBin && iBin < NPERIODS)
                {
                    nBin[iBin]++;
                }
            }
            free_lbuf(player_last);
        }
    }

    int iHour, nSum = 0;
    notify(executor, T("Day   Hours     Players  Total"));
    for (i = 0, iHour = 0; i < NPERIODS; i++, iHour += HOURS_PER_PERIOD)
    {
        nSum += nBin[i];
        mux_sprintf(buff, MBUF_SIZE, T("%3d %03d - %03d: %6d %6d"),
            iHour/24 + 1, iHour, iHour + HOURS_PER_PERIOD, nBin[i], nSum);
        notify(executor, buff);
    }
    free_mbuf(buff);
}
