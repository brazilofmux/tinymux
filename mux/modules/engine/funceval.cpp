/*! \file funceval.cpp
 * \brief MUX function handlers.
 *
 * This file began as a place to put function handlers ported from other
 * MU* servers, but has also become home to miscellaneous new functions.
 * These handlers include side-effect functions, comsys / mail functions,
 * ansi functions, zone functions, encrypt / decrypt, random functions,
 * some text-formatting and list-munging functions, deprecated stack
 * functions, regexp functions, etc.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ast.h"

extern "C" {
#include "color_ops.h"
}

/* Note: Many functions in this file have been taken, whole or in part, from
 * PennMUSH 1.50, and TinyMUSH 2.2, for softcode compatibility. The
 * maintainers of MUX would like to thank those responsible for PennMUSH 1.50
 * and TinyMUSH 2.2, and hope we have adequately noted in the source where
 * credit is due.
 */

bool parse_and_get_attrib
(
    dbref   executor,
    UTF8   *fargs[],
    UTF8  **atext,
    dbref  *thing,
    dbref  *paowner,
    dbref  *paflags,
    UTF8   *buff,
    UTF8  **bufc
)
{
    // Check for #lambda/body -- inline anonymous softcode.
    //
    if (string_prefix(fargs[0], T("#lambda/")) > 0)
    {
        *atext = alloc_lbuf("lambda");
        mux_strncpy(*atext, fargs[0] + 8, LBUF_SIZE - 1);
        *thing = executor;
        *paowner = executor;
        *paflags = 0;
        return true;
    }

    // Check for #apply[N]/funcname -- synthesize funcname(%0,...,%N-1).
    //
    if (string_prefix(fargs[0], T("#apply")) > 0)
    {
        const UTF8 *p = fargs[0] + 6;
        int nargs = 1;
        if (mux_isdigit(*p))
        {
            nargs = mux_atol(p);
            while (mux_isdigit(*p)) p++;
            if (nargs < 1)  nargs = 1;
            if (nargs > 10) nargs = 10;
        }
        if ('/' != *p)
        {
            return false;
        }
        p++;

        // Synthesize: funcname(%0,%1,...,%N-1)
        //
        *atext = alloc_lbuf("apply");
        UTF8 *bp = *atext;
        safe_str(p, *atext, &bp);
        safe_chr('(', *atext, &bp);
        for (int i = 0; i < nargs; i++)
        {
            if (i > 0) safe_chr(',', *atext, &bp);
            safe_chr('%', *atext, &bp);
            safe_chr('0' + i, *atext, &bp);
        }
        safe_chr(')', *atext, &bp);
        *bp = '\0';
        *thing = executor;
        *paowner = executor;
        *paflags = 0;
        return true;
    }

    ATTR *ap;

    // Two possibilities for the first arg: <obj>/<attr> and <attr>.
    //
    if (!parse_attrib(executor, fargs[0], thing, &ap))
    {
        *thing = executor;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return false;
    }

    // Use it if we can access it, otherwise return an error.
    //
    if (!See_attr(executor, *thing, ap))
    {
        safe_noperm(buff, bufc);
        return false;
    }

    *atext = atr_pget(*thing, ap->number, paowner, paflags);
    if (!*atext)
    {
        return false;
    }
    else if (!**atext)
    {
        free_lbuf(*atext);
        return false;
    }
    return true;
}

#define CWHO_ON  0
#define CWHO_OFF 1
#define CWHO_ALL 2

FUNCTION(fun_cwho)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct channel *ch = select_channel(fargs[0]);
    if (!ch)
    {
        safe_str(T("#-1 CHANNEL NOT FOUND"), buff, bufc);
        return;
    }
    if (  !Comm_All(executor)
       && !Controls(executor, ch->charge_who))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int match_type = CWHO_ON;
    if (nfargs >= 2)
    {
        if (mux_stricmp(fargs[1], T("all")) == 0)
        {
            match_type = CWHO_ALL;
        }
        else if (mux_stricmp(fargs[1], T("off")) == 0)
        {
            match_type = CWHO_OFF;
        }
        else if (mux_stricmp(fargs[1], T("on")) == 0)
        {
            match_type = CWHO_ON;
        }
    }

    int pg_offset = 0;
    int pg_limit  = 0;
    if (nfargs >= 3) pg_offset = mux_atol(fargs[2]);
    if (nfargs >= 4) pg_limit  = mux_atol(fargs[3]);
    if (pg_offset < 0) pg_offset = 0;
    if (pg_limit < 0)  pg_limit = 0;

    int pos = 0;
    int count = 0;
    ITL list_context;
    ItemToList_Init(&list_context, buff, bufc, '#');
    if (CWHO_ALL == match_type)
    {
        for (auto &kv : ch->users)
        {
            const comuser &user = kv.second;
            if (  !Hidden(user.who)
               || Wizard_Who(executor)
               || See_Hidden(executor))
            {
                if (pos < pg_offset)
                {
                    pos++;
                    continue;
                }
                if (pg_limit > 0 && count >= pg_limit)
                    break;
                if (ItemToList_AddInteger(&list_context, user.who))
                    break;
                count++;
                pos++;
            }
        }
    }
    else
    {
        for (auto &kv : ch->users)
        {
            const comuser &user = kv.second;
            if (  user.bConnected
               && (Connected(user.who) || isThing(user.who))
               && (  (match_type == CWHO_ON && user.bUserIsOn)
                  || (match_type == CWHO_OFF && !user.bUserIsOn)))
            {
                if (pos < pg_offset)
                {
                    pos++;
                    continue;
                }
                if (pg_limit > 0 && count >= pg_limit)
                    break;
                if (ItemToList_AddInteger(&list_context, user.who))
                    break;
                count++;
                pos++;
            }
        }
    }
    ItemToList_Final(&list_context);
}

#ifndef BEEP_CHAR
#define BEEP_CHAR '\07'
#endif

FUNCTION(fun_beep)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_chr(BEEP_CHAR, buff, bufc);
}

// This function was originally taken from PennMUSH 1.50
//
FUNCTION(fun_ansi)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iArg0;
    for (iArg0 = 0; iArg0 + 1 < nfargs; iArg0 += 2)
    {
        LBuf tmp = LBuf_Src("fun_translate");
        UTF8 *bp = tmp;

        safe_str(LettersToBinary(fargs[iArg0]), tmp, &bp);
        safe_str(fargs[iArg0+1], tmp, &bp);
        *bp = '\0';

        size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
        size_t nLen = TruncateToBuffer(tmp, *bufc, nBufferAvailable);
        *bufc += nLen;
    }
}

FUNCTION(fun_zone)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_zones)
    {
        safe_str(T("#-1 ZONES DISABLED"), buff, bufc);
        return;
    }
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (Examinable(executor, it))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), Zone(it));
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}


bool check_command(dbref player, const UTF8 *name, UTF8 *buff, UTF8 **bufc)
{
    size_t nName = strlen(reinterpret_cast<const char *>(name));
    auto it_cmd = mudstate.command_htab.find(std::vector<UTF8>(name, name + nName));
    CMDENT *cmdp = (it_cmd != mudstate.command_htab.end()) ? static_cast<CMDENT*>(it_cmd->second) : nullptr;
    if (cmdp)
    {
        // Perform checks similiar to (but not exactly like) the
        // ones in process_cmdent(): object type checks, permission
        // checks, ands global flags.
        //
        if (  Invalid_Objtype(player)
           || !check_access(player, cmdp->perms)
           || (  !Builder(player)
              && Protect(CA_GBL_BUILD)
              && !(mudconf.control_flags & CF_BUILD)))
        {
            safe_noperm(buff, bufc);
            return true;
        }
    }
    return false;
}

FUNCTION(fun_link)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@link"), buff, bufc))
    {
        return;
    }
    do_link(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], nullptr, 0);
}

FUNCTION(fun_trigger)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@trigger"), buff, bufc))
    {
        return;
    }
    do_trigger(executor, caller, enactor, eval, TRIG_QUIET, fargs[0], fargs+1, nfargs-1, nullptr, 0);
}

FUNCTION(fun_wipe)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@wipe"), buff, bufc))
    {
        return;
    }
    do_wipe(executor, caller, enactor, eval, 0, fargs[0], nullptr, 0);
}

FUNCTION(fun_tel)
{
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@teleport"), buff, bufc))
    {
        return;
    }

    int key = 0;
    if (3 <= nfargs)
    {
        const UTF8 *p = fargs[2];
        for (int i = 0; '\0' != p[i] && key != (TELEPORT_QUIET|TELEPORT_LIST); i++)
        {
            switch (p[i])
            {
            case 'q':
            case 'Q':
                key |= TELEPORT_QUIET;
                break;

            case 'l':
            case 'L':
                key |= TELEPORT_LIST;
                break;
            }
        }
    }

    do_teleport(executor, caller, enactor, eval, key, 2, fargs[0], fargs[1], nullptr, 0);
}

FUNCTION(fun_prompt)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = match_thing(executor, fargs[0]);
    if (!Good_obj(target))
    {
        safe_str(T("#-1 NO MATCH"), buff, bufc);
        return;
    }
    if (!Controls(executor, target) && !nearby(executor, target))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (!Connected(target))
    {
        return;
    }

    // Send text without trailing newline, then send telnet GA.
    //
    send_text_to_player(target, fargs[1]);
    const UTF8 aGoAhead[2] = { NVT_IAC, NVT_GA };
    send_raw_to_player(target, aGoAhead, sizeof(aGoAhead));
}

FUNCTION(fun_pemit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@pemit"), buff, bufc))
    {
        return;
    }
    do_pemit_list(executor, PEMIT_PEMIT, false, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_oemit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@oemit"), buff, bufc))
    {
        return;
    }
    do_pemit_list(executor, PEMIT_OEMIT, false, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_emit)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@emit"), buff, bufc))
    {
        return;
    }
    do_say(executor, caller, enactor, 0, SAY_EMIT, fargs[0], nullptr, 0);
}

// ------------------------------------------------------------------------
// fun_pose: Pure formatting function.  Returns the string that would be
// produced by say/pose/semipose without actually emitting it.
//
// pose(<player>, <text>[, <prefix>])
//
FUNCTION(fun_pose)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(target))
    {
        safe_match_result(target, buff, bufc);
        return;
    }

    UTF8 *message = fargs[1];
    int key;

    // Parse the prefix character the same way do_say handles SAY_PREFIX.
    //
    switch (message[0])
    {
    case '"':
        message++;
        key = SAY_SAY;
        break;

    case ':':
        message++;
        if (*message == ' ')
        {
            message++;
            key = SAY_POSE_NOSPC;
        }
        else
        {
            key = SAY_POSE;
        }
        break;

    case ';':
        message++;
        key = SAY_POSE_NOSPC;
        break;

    case 0xE2:
        if (  0x80 == message[1]
           && 0x9C == message[2])
        {
            // U+201C - Unicode opening double quote.
            //
            message += 3;
            key = SAY_SAY;
        }
        else
        {
            key = SAY_SAY;
        }
        break;

    default:
        key = SAY_SAY;
        break;
    }

    // Apply speechmod to the message text.
    //
    const UTF8 *command;
    if (SAY_SAY == key)
    {
        command = T("say");
    }
    else
    {
        command = T("pose");
    }

    UTF8 *messageOrig = message;
    UTF8 *messageNew = modSpeech(target, message, true, command);
    if (messageNew)
    {
        message = messageNew;
    }

    // Write the optional prefix first.
    //
    if (nfargs >= 3)
    {
        safe_str(fargs[2], buff, bufc);
    }

    // Format the result.
    //
    switch (key)
    {
    case SAY_SAY:
        {
            UTF8 *saystring = modSpeech(target, messageOrig, false, command);
            if (saystring)
            {
                safe_tprintf_str(buff, bufc, T("%s %s \xE2\x80\x9C%s\xE2\x80\x9D"),
                    Moniker(target), saystring, message);
                free_lbuf(saystring);
            }
            else
            {
                safe_tprintf_str(buff, bufc, T("%s says, \xE2\x80\x9C%s\xE2\x80\x9D"),
                    Moniker(target), message);
            }
        }
        break;

    case SAY_POSE:
        safe_tprintf_str(buff, bufc, T("%s %s"), Moniker(target), message);
        break;

    case SAY_POSE_NOSPC:
        safe_tprintf_str(buff, bufc, T("%s%s"), Moniker(target), message);
        break;
    }

    if (messageNew)
    {
        free_lbuf(messageNew);
    }
}

FUNCTION(fun_remit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@pemit"), buff, bufc))
    {
        return;
    }
    do_pemit_single(executor, PEMIT_PEMIT, true, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_cemit)
{
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@cemit"), buff, bufc))
    {
        return;
    }
    do_cemit(executor, caller, enactor, eval, 0, nfargs, fargs[0], fargs[1], nullptr, 0);
}

// ---------------------------------------------------------------------------
// Nospoof emit functions — always prepend [Name(#dbref)] header.
// PennMUSH-compatible.
// ---------------------------------------------------------------------------

static void build_nospoof_msg(dbref executor, const UTF8 *msg,
    UTF8 *nsbuf, size_t nsbuf_size)
{
    mux_sprintf(nsbuf, nsbuf_size, T("[%s(#%d)] %s"),
        Moniker(executor), executor, msg);
}

FUNCTION(fun_nspemit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@pemit"), buff, bufc)) return;
    LBuf nsbuf = LBuf_Src("fun_nspemit");
    build_nospoof_msg(executor, fargs[1], nsbuf, LBUF_SIZE);
    do_pemit_list(executor, PEMIT_PEMIT, false, 0, fargs[0], 0, nsbuf);
}

FUNCTION(fun_nsemit)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@emit"), buff, bufc)) return;
    LBuf nsbuf = LBuf_Src("fun_nsemit");
    build_nospoof_msg(executor, fargs[0], nsbuf, LBUF_SIZE);
    do_say(executor, caller, enactor, 0, SAY_EMIT, nsbuf, nullptr, 0);
}

FUNCTION(fun_nsoemit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@oemit"), buff, bufc)) return;
    LBuf nsbuf = LBuf_Src("fun_nsoemit");
    build_nospoof_msg(executor, fargs[1], nsbuf, LBUF_SIZE);
    do_pemit_list(executor, PEMIT_OEMIT, false, 0, fargs[0], 0, nsbuf);
}

FUNCTION(fun_nsremit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@pemit"), buff, bufc)) return;
    LBuf nsbuf = LBuf_Src("fun_nsremit");
    build_nospoof_msg(executor, fargs[1], nsbuf, LBUF_SIZE);
    do_pemit_single(executor, PEMIT_PEMIT, true, 0, fargs[0], 0, nsbuf);
}

FUNCTION(fun_verb)
{
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@verb"), buff, bufc))
    {
        return;
    }
    do_verb(executor, caller, enactor, eval, 0,
        fargs[0], fargs + 1, nfargs - 1, nullptr, 0);
}

// ------------------------------------------------------------------------
// fun_create: Creates a room, thing or exit.
//
FUNCTION(fun_create)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT))
    {
        return;
    }

    UTF8 *name = fargs[0];

    if (!name || !*name)
    {
        safe_str(T("#-1 ILLEGAL NAME"), buff, bufc);
        return;
    }
    if (nfargs >= 3 && *fargs[2])
    {
        sep.str[0] = *fargs[2];
    }
    else
    {
        sep.str[0] = 't';
    }

    dbref thing;
    int cost;

    switch (sep.str[0])
    {
    case 'r':

        if (check_command(executor, T("@dig"), buff, bufc))
        {
            return;
        }
        thing = create_obj(executor, TYPE_ROOM, name, 0);
        if (thing != NOTHING)
        {
            local_data_create(thing);
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (nullptr != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
        }
        break;

    case 'e':

        if (check_command(executor, T("@open"), buff, bufc))
        {
            return;
        }
        thing = create_obj(executor, TYPE_EXIT, name, 0);
        if (thing != NOTHING)
        {
            s_Exits(thing, executor);
            s_Next(thing, Exits(executor));
            s_Exits(executor, thing);
            local_data_create(thing);
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (nullptr != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
        }
        break;

    default:

        if (check_command(executor, T("@create"), buff, bufc))
        {
            return;
        }
        if (*fargs[1])
        {
            cost = mux_atol(fargs[1]);
            if (  cost < mudconf.createmin
               || mudconf.createmax < cost)
            {
                safe_range(buff, bufc);
                return;
            }
        }
        else
        {
            cost = mudconf.createmin;
        }
        thing = create_obj(executor, TYPE_THING, name, cost);
        if (thing != NOTHING)
        {
            move_via_generic(thing, executor, NOTHING, 0);
            s_Home(thing, new_home(executor));
            local_data_create(thing);
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (nullptr != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
        }
        break;
    }
    safe_tprintf_str(buff, bufc, T("#%d"), thing);
}

// ------------------------------------------------------------------------
// fun_clone: Clones an object and returns its dbref.
//
FUNCTION(fun_clone)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@clone"), buff, bufc))
    {
        return;
    }

    // Find the object to clone.
    //
    init_match(executor, fargs[0], NOTYPE);
    match_everything(0);
    dbref thing = match_result();
    if (  NOTHING == thing
       || AMBIGUOUS == thing)
    {
        safe_nothing(buff, bufc);
        return;
    }

    if (!Examinable(executor, thing))
    {
        safe_nothing(buff, bufc);
        return;
    }
    if (isPlayer(thing))
    {
        safe_str(T("#-1 CANNOT CLONE PLAYERS"), buff, bufc);
        return;
    }

    // Determine location.
    //
    dbref loc;
    if (!Has_location(executor))
    {
        loc = executor;
    }
    else
    {
        loc = Location(executor);
    }
    if (!Good_obj(loc))
    {
        safe_nothing(buff, bufc);
        return;
    }

    // Determine cost.
    //
    int cost = 1;
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
            safe_nothing(buff, bufc);
            return;
        }
        cost = mudconf.digcost;
        break;
    }

    // Validate optional new name.
    //
    bool bValid = false;
    size_t nValidName;
    const UTF8 *pValidName = nullptr;
    UTF8 *arg2 = (nfargs >= 2) ? fargs[1] : nullptr;
    switch (Typeof(thing))
    {
    case TYPE_THING:
        pValidName = MakeCanonicalObjectName(arg2, &nValidName, &bValid, mudconf.thing_name_charset);
        break;

    case TYPE_ROOM:
        pValidName = MakeCanonicalObjectName(arg2, &nValidName, &bValid, mudconf.room_name_charset);
        break;

    case TYPE_EXIT:
        pValidName = MakeCanonicalExitName(arg2, &nValidName, &bValid);
        break;
    }
    const UTF8 *clone_name;
    if (bValid)
    {
        clone_name = pValidName;
    }
    else
    {
        clone_name = Name(thing);
    }

    // Create the clone.
    //
    const dbref new_owner = Owner(executor);
    const dbref clone = create_obj(new_owner, Typeof(thing), clone_name, cost);
    if (clone == NOTHING)
    {
        safe_nothing(buff, bufc);
        return;
    }

    // Copy attributes.
    //
    atr_cpy(clone, thing, false);

    // Reset name and pennies.
    //
    s_Name(clone, clone_name);
    s_Pennies(clone, OBJECT_ENDOWMENT(cost));

    // Strip flags.
    //
    FLAGSET clearflags;
    TranslateFlags_Clone(clearflags.word, executor, 0);
    SetClearFlags(clone, clearflags.word, nullptr);

    // Type-specific setup.
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

    // Handle ownership, parent, and ACLONE.
    //
    if (new_owner == Owner(thing))
    {
        s_Parent(clone, Parent(thing));
        did_it(executor, clone, 0, nullptr, 0, nullptr, A_ACLONE, 0, nullptr, 0);
    }
    else
    {
        if (Controls(executor, thing) || Parent_ok(thing))
        {
            s_Parent(clone, Parent(thing));
        }
        s_Halted(clone);
    }

    local_data_clone(clone, thing);

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->data_clone(clone, thing);
        p = p->pNext;
    }

    safe_tprintf_str(buff, bufc, T("#%d"), clone);
}

FUNCTION(fun_destroy)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@destroy"), buff, bufc))
    {
        return;
    }
    do_destroy(executor, caller, enactor, 0, DEST_ONE, fargs[0], nullptr, 0);
}

FUNCTION(fun_textfile)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nCased;
    UTF8  *pCased = mux_strlwr(fargs[0], nCased);

    auto it_cmd = mudstate.command_htab.find(std::vector<UTF8>(pCased, pCased + nCased));
    CMDENT_ONE_ARG *cmdp = (it_cmd != mudstate.command_htab.end()) ? static_cast<CMDENT_ONE_ARG*>(it_cmd->second) : nullptr;

    if (  !cmdp
       || cmdp->handler != do_help)
    {
        safe_str(T("#-1 NOT FOUND"), buff, bufc);
        return;
    }

    if (check_command(executor, pCased, buff, bufc))
    {
        return;
    }

    help_helper(executor, cmdp->extra, fargs[1], buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_set: sets an attribute on an object
 */

static void set_attr_internal(dbref player, dbref thing, int attrnum, UTF8 *attrtext, int key, UTF8 *buff, UTF8 **bufc)
{
    if (!Good_obj(thing))
    {
        safe_noperm(buff, bufc);
        notify_quiet(player, T("You shouldn\xE2\x80\x99t be rummaging through the garbage."));
        return;
    }

    dbref aowner;
    int aflags;
    ATTR *pattr = atr_num(attrnum);
    atr_pget_info(thing, attrnum, &aowner, &aflags);
    if (  pattr
       && bCanSetAttr(player, thing, pattr))
    {
        bool could_hear = Hearer(thing);
        atr_add(thing, attrnum, attrtext, Owner(player), aflags);
        handle_ears(thing, could_hear, Hearer(thing));
        if (  !(key & SET_QUIET)
           && !Quiet(player)
           && !Quiet(thing))
        {
            notify_quiet(player, T("Set."));
        }
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_set)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@set"), buff, bufc))
    {
        return;
    }

    dbref thing, aowner;
    int aflags;
    ATTR *pattr;

    // See if we have the <obj>/<attr> form, which is how you set
    // attribute flags.
    //
    if (parse_attrib(executor, fargs[0], &thing, &pattr))
    {
        if (  pattr
           && See_attr(executor, thing, pattr))
        {
            UTF8 *flagname = fargs[1];

            // You must specify a flag name.
            //
            if (flagname[0] == '\0')
            {
                safe_str(T("#-1 UNSPECIFIED PARAMETER"), buff, bufc);
                return;
            }

            // Check for clearing.
            //
            bool clear = false;
            if (flagname[0] == NOT_TOKEN)
            {
                flagname++;
                clear = true;
            }

            // Make sure player specified a valid attribute flag.
            //
            int flagvalue;
            if (!search_nametab(executor, indiv_attraccess_nametab, flagname, &flagvalue))
            {
                safe_str(T("#-1 CANNOT SET"), buff, bufc);
                return;
            }

            // Make sure the object has the attribute present.
            //
            if (!atr_get_info(thing, pattr->number, &aowner, &aflags))
            {
                safe_str(T("#-1 ATTRIBUTE NOT PRESENT ON OBJECT"), buff, bufc);
                return;
            }

            // Make sure we can write to the attribute.
            //
            if (!bCanSetAttr(executor, thing, pattr))
            {
                safe_noperm(buff, bufc);
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
            atr_set_flags(thing, pattr->number, aflags);
            return;
        }
    }

    // Find thing.
    //
    thing = match_controlled_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    // Check for attr set first.
    //
    UTF8 *p;
    for (p = fargs[1]; *p && *p != ':'; p++)
    {
        ; // Nothing
    }

    if (*p)
    {
        *p++ = 0;
        int atr = mkattr(executor, fargs[1]);
        if (atr <= 0)
        {
            safe_str(T("#-1 UNABLE TO CREATE ATTRIBUTE"), buff, bufc);
            return;
        }
        pattr = atr_num(atr);
        if (!pattr)
        {
            safe_noperm(buff, bufc);
            return;
        }
        if (!bCanSetAttr(executor, thing, pattr))
        {
            safe_noperm(buff, bufc);
            return;
        }

        // Check for _
        //
        if (*p == '_')
        {
            p++;
            ATTR *pattr2;
            dbref thing2;

            if (!( parse_attrib(executor, p, &thing2, &pattr2)
                && pattr2))
            {
                safe_nomatch(buff, bufc);
                return;
            }
            UTF8 *buff2 = alloc_lbuf("fun_set");
            atr_pget_str(buff2, thing2, pattr2->number, &aowner, &aflags);

            if (!See_attr(executor, thing2, pattr2))
            {
                safe_noperm(buff, bufc);
            }
            else
            {
                set_attr_internal(executor, thing, atr, buff2, 0, buff, bufc);
            }
            free_lbuf(buff2);
            return;
        }

        // Go set it.
        //
        set_attr_internal(executor, thing, atr, p, 0, buff, bufc);
        return;
    }

    // Set/clear a flag.
    //
    flag_set(thing, executor, fargs[1], 0);
}

FUNCTION(fun_attrib_set)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@set"), buff, bufc))
    {
        return;
    }

    // Parse <object>/<attr> from first argument.
    //
    const UTF8 *pAttrName;
    dbref thing;
    if (!parse_thing_slash(executor, fargs[0], &pAttrName, &thing))
    {
        safe_str(T("#-1 BAD ARGUMENT FORMAT TO ATTRIB_SET"), buff, bufc);
        return;
    }
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    // Create or find the attribute.
    //
    int atr = mkattr(executor, pAttrName);
    if (atr <= 0)
    {
        safe_str(T("#-1 UNABLE TO CREATE ATTRIBUTE"), buff, bufc);
        return;
    }
    ATTR *pattr = atr_num(atr);
    if (!pattr)
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (!bCanSetAttr(executor, thing, pattr))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // If two args, set the attribute; if one arg, clear it.
    //
    if (nfargs >= 2)
    {
        set_attr_internal(executor, thing, atr, fargs[1], 0, buff, bufc);
    }
    else
    {
        UTF8 empty[] = { '\0' };
        set_attr_internal(executor, thing, atr, empty, 0, buff, bufc);
    }
}

// Generate a substitution array.
//
static size_t GenCode(UTF8 *pCode, size_t nCode, const UTF8 *pCodeASCII)
{
    // Strip out the ANSI.
    //
    UTF8 *pIn = strip_color(pCodeASCII);

    // Process the printable characters.
    //
    size_t i = 0;
    size_t j = 0;
    while (  pIn[i]
          && j < nCode - 1)
    {
        UTF8 ch = pIn[i++];
        if (  ' ' <= ch
           && ch <= '~')
        {
            pCode[j++] = static_cast<UTF8>(ch - ' ');
        }
    }
    pCode[j] = '\0';
    return j;
}

static const UTF8 *crypt_code(UTF8 *code, UTF8 *text, bool type)
{
    if (  !text
       || text[0] == '\0')
    {
        return T("");
    }
    if (  !code
       || code[0] == '\0')
    {
        return text;
    }

    LBuf codebuff = LBuf_Src("accent_code");
    size_t nCode = GenCode(codebuff, LBUF_SIZE, code);
    if (0 == nCode)
    {
        return text;
    }

    thread_local UTF8 textbuff[LBUF_SIZE];
    UTF8 *p = strip_color(text);
    size_t nq = nCode;
    size_t ip = 0;
    size_t iq = 0;
    size_t ir = 0;

    int iMod    = '~' - ' ' + 1;

    // Encryption loop:
    //
    while (  p[ip]
          && ir < sizeof(textbuff) - 1)
    {
        UTF8 ch = p[ip++];
        if (  ' ' <= ch
           && ch <= '~')
        {
            int iCode = ch - ' ';
            if (type)
            {
                iCode += codebuff[iq++];
                if (iMod <= iCode)
                {
                    iCode -= iMod;
                }
            }
            else
            {
                iCode -= codebuff[iq++];
                if (iCode < 0)
                {
                    iCode += iMod;
                }
            }
            textbuff[ir++] = static_cast<UTF8>(iCode + ' ');

            nq--;
            if (0 == nq)
            {
                iq = 0;
                nq = nCode;
            }
        }
    }
    textbuff[ir] = '\0';
    return textbuff;
}

// Code for encrypt() and decrypt() was taken from the DarkZone
// server.
//
FUNCTION(fun_encrypt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(crypt_code(fargs[1], fargs[0], true), buff, bufc);
}

FUNCTION(fun_decrypt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(crypt_code(fargs[1], fargs[0], false), buff, bufc);
}

// Borrowed from DarkZone
//
static void scan_zone
(
    dbref executor,
    UTF8 *szZone,
    int   ObjectType,
    UTF8 *buff,
    UTF8 **bufc
)
{
    if (!mudconf.have_zones)
    {
        safe_str(T("#-1 ZONES DISABLED"), buff, bufc);
        return;
    }

    dbref it = match_thing_quiet(executor, szZone);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DO_WHOLE_DB(i)
    {
        if (  Typeof(i) == ObjectType
           && Zone(i) == it
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

FUNCTION(fun_zwho)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    scan_zone(executor, fargs[0], TYPE_PLAYER, buff, bufc);
}

FUNCTION(fun_inzone)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    scan_zone(executor, fargs[0], TYPE_ROOM, buff, bufc);
}

FUNCTION(fun_zexits)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    scan_zone(executor, fargs[0], TYPE_EXIT, buff, bufc);
}

FUNCTION(fun_zthings)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    scan_zone(executor, fargs[0], TYPE_THING, buff, bufc);
}

FUNCTION(fun_zrooms)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    scan_zone(executor, fargs[0], TYPE_ROOM, buff, bufc);
}

FUNCTION(fun_zchildren)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_zones)
    {
        safe_str(T("#-1 ZONES DISABLED"), buff, bufc);
        return;
    }

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DO_WHOLE_DB(i)
    {
        if (  Zone(i) == it
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

// Borrowed from DarkZone
//
FUNCTION(fun_children)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DO_WHOLE_DB(i)
    {
        if (  Parent(i) == it
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

FUNCTION(fun_objeval)
{
    UNUSED_PARAMETER(nfargs);

    if (!*fargs[0])
    {
        return;
    }
    UTF8 *name = alloc_lbuf("fun_objeval");
    UTF8 *bp = name;
    mux_exec(fargs[0], LBUF_SIZE-1, name, &bp, executor, caller, enactor,
             eval|EV_FCHECK|EV_STRIP_CURLY|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    dbref obj = match_thing_quiet(executor, name);
    free_lbuf(name);
    if (!Good_obj(obj))
    {
        safe_match_result(obj, buff, bufc);
        return;
    }

    if (!Controls(executor, obj))
    {
        // The right circumstances were not met, so we are evaluating
        // as the executor who gave the command instead of the
        // requested object.
        //
        obj = executor;
    }

    mudstate.nObjEvalNest++;
    mux_exec(fargs[1], LBUF_SIZE-1, buff, bufc, obj, executor, enactor,
             eval|EV_FCHECK|EV_STRIP_CURLY|EV_EVAL, cargs, ncargs);
    mudstate.nObjEvalNest--;
}

FUNCTION(fun_localize)
{
    UNUSED_PARAMETER(nfargs);

    reg_ref **preserve = nullptr;
    preserve = PushRegisters(MAX_GLOBAL_REGS);
    save_global_regs(preserve);

    mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
        eval|EV_FCHECK|EV_STRIP_CURLY|EV_EVAL, cargs, ncargs);

    restore_global_regs(preserve);
    PopRegisters(preserve, MAX_GLOBAL_REGS);
}

// ---------------------------------------------------------------------------
// fun_letq: letq(name, value, ..., body)
//
// Scoped named registers.  Assigns name/value pairs, evaluates the body
// expression, then restores all registers to their prior state.  The body
// sees both the letq bindings and any pre-existing registers.
//
// Requires an odd number of args >= 3 (pairs + body).
//
FUNCTION(fun_letq)
{
    if (nfargs < 3 || (nfargs % 2) != 1)
    {
        safe_str(T("#-1 FUNCTION (LETQ) EXPECTS AN ODD NUMBER OF ARGUMENTS"), buff, bufc);
        return;
    }

    // Save all registers (global + named).
    //
    reg_ref **preserve = PushRegisters(MAX_GLOBAL_REGS);
    save_global_regs(preserve);

    // Evaluate and assign each name/value pair.
    //
    UTF8 *tbuf = alloc_lbuf("fun_letq.name");
    UTF8 *vbuf = alloc_lbuf("fun_letq.value");
    bool bError = false;

    for (int i = 0; i < nfargs - 1; i += 2)
    {
        // Evaluate the register name.
        //
        UTF8 *tp = tbuf;
        mux_exec(fargs[i], LBUF_SIZE-1, tbuf, &tp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *tp = '\0';

        // Evaluate the value.
        //
        UTF8 *vp = vbuf;
        mux_exec(fargs[i + 1], LBUF_SIZE-1, vbuf, &vp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *vp = '\0';

        size_t nVal = vp - vbuf;

        int regnum;
        if (IsSingleCharReg(tbuf, regnum))
        {
            RegAssign(&mudstate.global_regs[regnum], nVal, vbuf);
        }
        else
        {
            size_t nName = tp - tbuf;
            if (IsValidNamedReg(tbuf, nName))
            {
                NamedRegAssign(mudstate.named_regs, tbuf, nName, nVal, vbuf);
            }
            else
            {
                safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
                bError = true;
                break;
            }
        }
    }

    free_lbuf(vbuf);
    free_lbuf(tbuf);

    // Evaluate the body (last argument).
    //
    if (!bError)
    {
        mux_exec(fargs[nfargs - 1], LBUF_SIZE-1, buff, bufc, executor,
            caller, enactor, eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL,
            cargs, ncargs);
    }

    // Restore all registers.
    //
    restore_global_regs(preserve);
    PopRegisters(preserve, MAX_GLOBAL_REGS);
}

FUNCTION(fun_null)
{
    UNUSED_PARAMETER(buff);
    UNUSED_PARAMETER(bufc);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    return;
}

FUNCTION(fun_squish)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    if (sep.n <= 1)
    {
        // Single-char compress (or default space): use co_compress.
        //
        unsigned char compress_char = (sep.n == 1) ? sep.str[0] : ' ';
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_compress(out,
                                  reinterpret_cast<const unsigned char *>(fargs[0]),
                                  nLen, compress_char);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
    else
    {
        // Multi-char separator: use co_compress_str.
        //
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_compress_str(out,
                          reinterpret_cast<const unsigned char *>(fargs[0]),
                          nLen,
                          reinterpret_cast<const unsigned char *>(sep.str),
                          sep.n);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
}

FUNCTION(fun_stripansi)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(strip_color(fargs[0]), buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_zfun)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.have_zones)
    {
        safe_str(T("#-1 ZONES DISABLED"), buff, bufc);
        return;
    }

    // Support obj/attr syntax: zfun(obj/attr, args...)
    //
    const UTF8 *pAttrName;
    dbref thing;
    dbref zone;
    if (parse_thing_slash(executor, fargs[0], &pAttrName, &thing))
    {
        zone = Zone(thing);
    }
    else
    {
        zone = Zone(executor);
        pAttrName = fargs[0];
    }

    if (!Good_obj(zone))
    {
        safe_str(T("#-1 INVALID ZONE"), buff, bufc);
        return;
    }

    // Find the user function attribute.
    //
    int attrib = get_atr(pAttrName);
    if (!attrib)
    {
        safe_str(T("#-1 NO SUCH USER FUNCTION"), buff, bufc);
        return;
    }
    dbref aowner;
    int aflags;
    ATTR *pattr = atr_num(attrib);
    UTF8 *tbuf1 = atr_pget(zone, attrib, &aowner, &aflags);
    if (  !pattr
       || !See_attr(executor, zone, pattr))
    {
        safe_noperm(buff, bufc);
        free_lbuf(tbuf1);
        return;
    }
    mux_exec(tbuf1, LBUF_SIZE-1, buff, bufc, zone, executor, enactor,
       AttrTrace(aflags, EV_EVAL|EV_STRIP_CURLY|EV_FCHECK),
        (const UTF8 **)fargs + 1, nfargs - 1);
    free_lbuf(tbuf1);
}

FUNCTION(fun_columns)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_STRING))
    {
        return;
    }

    int nWidth = mux_atol(fargs[1]);
    int nIndent = 0;
    if (nfargs == 4)
    {
        nIndent = mux_atol(fargs[3]);
        if (nIndent < 0 || 77 < nIndent)
        {
            nIndent = 1;
        }
    }

    int nRight = nIndent + nWidth;
    if (  nWidth < 1
       || 78 < nWidth
       || nRight < 1
       || 78 < nRight)
    {
        safe_range(buff, bufc);
        return;
    }

    UTF8 *cp = trim_space_sep(fargs[0], sep);
    if (!*cp)
    {
        return;
    }

    const unsigned char *pData = reinterpret_cast<const unsigned char *>(cp);
    size_t nLen_d = strlen(reinterpret_cast<const char *>(cp));

    size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
    size_t nWords = co_split_words(pData, nLen_d,
                        reinterpret_cast<const unsigned char *>(sep.str),
                        sep.n, wstarts, wends, LBUF_SIZE);

    if (0 == nWords)
    {
        return;
    }

    int nColumns = (78-nIndent)/nWidth;
    int iColumn = 0;
    int nLen = 0;

    size_t nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
    bool bNeedCRLF = false;
    for (size_t i = 0; i < nWords && 0 < nBufferAvailable; i++)
    {
        if (iColumn == 0)
        {
            safe_fill(buff, bufc, ' ', nIndent);
        }

        const unsigned char *pWord = pData + wstarts[i];
        size_t nWordBytes = wends[i] - wstarts[i];
        nLen = static_cast<int>(co_visual_width(pWord, nWordBytes));
        if (nWidth < nLen)
        {
            nLen = nWidth;
        }

        /* Copy up to nLen display columns of this word. */
        unsigned char wordOut[LBUF_SIZE];
        size_t nOut = co_copy_columns(wordOut, pWord,
                          pWord + nWordBytes, static_cast<size_t>(nLen));
        nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
        if (nOut > nBufferAvailable) nOut = nBufferAvailable;
        memcpy(*bufc, wordOut, nOut);
        *bufc += nOut;

        if (nColumns-1 <= iColumn)
        {
            iColumn = 0;
            safe_copy_buf(T("\r\n"), 2, buff, bufc);
            bNeedCRLF = false;
        }
        else
        {
            iColumn++;
            safe_fill(buff, bufc, ' ', nWidth - nLen);
            bNeedCRLF = true;
        }
        nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
    }

    if (bNeedCRLF)
    {
        safe_copy_buf(T("\r\n"), 2, buff, bufc);
    }
}

// table(<list>,<field width>,<line length>,<delimiter>,<output separator>, <padding>)
//
// Ported from PennMUSH 1.7.3 by Morgan.
//
FUNCTION(fun_table)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Check argument numbers, assign values and defaults if necessary.
    //
    const unsigned char *pFill = nullptr;
    size_t lenFill = 0;
    if (nfargs == 6 && *fargs[5])
    {
        pFill = reinterpret_cast<const unsigned char *>(fargs[5]);
        lenFill = strlen(reinterpret_cast<const char *>(pFill));
    }

    // Get single-character separator.
    //
    UTF8 cSeparator = ' ';
    if (nfargs >= 5 && fargs[4][0] != '\0')
    {
        if (fargs[4][1] == '\0')
        {
            cSeparator = *fargs[4];
        }
        else
        {
            safe_str(T("#-1 SEPARATOR MUST BE ONE CHARACTER"), buff, bufc);
            return;
        }
    }

    // Get single-character delimiter.
    //
    UTF8 cDelimiter = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0')
    {
        if (fargs[3][1] == '\0')
        {
            cDelimiter = *fargs[3];
        }
        else
        {
            safe_str(T("#-1 DELIMITER MUST BE ONE CHARACTER"), buff, bufc);
            return;
        }
    }

    // Get line length.
    //
    int nLineLength = 78;
    if (nfargs >= 3)
    {
        nLineLength = mux_atol(fargs[2]);
    }

    // Get field width.
    //
    int nFieldWidth = 10;
    if (nfargs >= 2)
    {
        nFieldWidth = mux_atol(fargs[1]);
    }
    else
    {
        nFieldWidth = 10;
    }

    // Validate nFieldWidth and nLineLength.
    //
    if (  nLineLength < 1
       || LBUF_SIZE   <= nLineLength
       || nFieldWidth < 1
       || nLineLength < nFieldWidth)
    {
        safe_range(buff, bufc);
        return;
    }

    int nNumCols = nLineLength / nFieldWidth;
    SEP sep;
    sep.n = 1;
    sep.str[0] = cDelimiter;
    UTF8 *pNext = trim_space_sep(fargs[0], sep);
    if (!*pNext)
    {
        return;
    }

    UTF8 *pCurrent = split_token(&pNext, sep);
    if (!pCurrent)
    {
        return;
    }

    size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    size_t nCurrentCol = nNumCols - 1;
    for (;;)
    {
        // Left-justify the field with ANSI-aware padding via co_ljust.
        //
        unsigned char ljBuf[LBUF_SIZE];
        size_t nOut = co_ljust(ljBuf,
            reinterpret_cast<const unsigned char *>(pCurrent),
            strlen(reinterpret_cast<const char *>(pCurrent)),
            static_cast<size_t>(nFieldWidth),
            pFill, lenFill, 1);

        if (nOut > nBufferAvailable)
        {
            nOut = nBufferAvailable;
        }
        memcpy(*bufc, ljBuf, nOut);
        *bufc += nOut;
        nBufferAvailable -= nOut;

        pCurrent = split_token(&pNext, sep);
        if (!pCurrent)
        {
            break;
        }

        if (!nCurrentCol)
        {
            nCurrentCol = nNumCols - 1;
            if (nBufferAvailable >= 2)
            {
                UTF8 *p = *bufc;
                p[0] = '\r';
                p[1] = '\n';

                nBufferAvailable -= 2;
                *bufc += 2;
            }
            else
            {
                // nBufferAvailable has less than 2 characters left, if there's
                // no room left just break out.
                //
                if (!nBufferAvailable)
                {
                    break;
                }
            }
        }
        else
        {
            nCurrentCol--;
            if (!nBufferAvailable)
            {
                break;
            }
            **bufc = cSeparator;
            (*bufc)++;
            nBufferAvailable--;
        }
    }
}

// Code for objmem and playmem borrowed from PennMUSH 1.50
//
static size_t mem_usage(dbref thing)
{
    size_t k = sizeof(struct object) + strlen(reinterpret_cast<const char *>(Name(thing))) + 1;

    unsigned char *as;
    for (int ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        size_t nLen;
        const UTF8 *str = atr_get_raw_LEN(thing, ca, &nLen);
        k += nLen+1;
        ATTR *pattr = atr_num(ca);
        if (pattr)
        {
            str = pattr->name;
            if (  str
               && *str)
            {
                k += strlen(reinterpret_cast<const char *>(str))+1;
            }
        }
    }
    return k;
}

FUNCTION(fun_objmem)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
    }
    else if (Examinable(executor, thing))
    {
        safe_ltoa(static_cast<long>(mem_usage(thing)), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_playmem)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing;
    if (nfargs == 1)
    {
        thing = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(thing))
        {
            safe_match_result(thing, buff, bufc);
            return;
        }
        else if (!Examinable(executor, thing))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }
    else
    {
        thing = executor;
    }
    size_t tot = 0;
    dbref j;
    DO_WHOLE_DB(j)
    {
        if (Owner(j) == thing)
        {
            tot += mem_usage(j);
        }
    }
    safe_ltoa(static_cast<long>(tot), buff, bufc);
}

// Code for andflags() and orflags() borrowed from PennMUSH 1.50
// false for orflags, true for andflags
//
static bool handle_flaglists(dbref player, UTF8 *name, UTF8 *fstr, bool type)
{
    dbref it = match_thing_quiet(player, name);
    if (!Good_obj(it))
    {
        return false;
    }

    UTF8 *s;
    UTF8 flagletter[2];
    FLAGSET fset;
    FLAG p_type;
    bool negate = false;
    bool temp = false;
    bool ret = type;

    for (s = fstr; *s; s++)
    {
        // Check for a negation sign. If we find it, we note it and
        // increment the pointer to the next character.
        //
        if (*s == '!')
        {
            negate = true;
            s++;
        }
        else
        {
            negate = false;
        }

        if (!*s)
        {
            return false;
        }
        flagletter[0] = *s;
        flagletter[1] = '\0';

        if (!convert_flags(player, flagletter, &fset, &p_type))
        {
            // Either we got a '!' that wasn't followed by a letter, or we
            // couldn't find that flag. For AND, since we've failed a check,
            // we can return false. Otherwise we just go on.
            //
            if (type)
            {
                return false;
            }
            else
            {
                continue;
            }
        }
        else
        {
            // Does the object have this flag?
            //
            if (  (Flags(it) & fset.word[FLAG_WORD1])
               || (Flags2(it) & fset.word[FLAG_WORD2])
               || (Flags3(it) & fset.word[FLAG_WORD3])
               || Typeof(it) == p_type)
            {
                if (  isPlayer(it)
                   && fset.word[FLAG_WORD2] == CONNECTED
                   && Hidden(it)
                   && !See_Hidden(player))
                {
                    temp = false;
                }
                else
                {
                    temp = true;
                }
            }
            else
            {
                temp = false;
            }

            if (  type
               && (  (negate && temp)
                  || (!negate && !temp)))
            {
                // Too bad there's no NXOR function. At this point we've
                // either got a flag and we don't want it, or we don't have a
                // flag and we want it. Since it's AND, we return false.
                //
                return false;

            }
            else if (  !type
                    && (  (!negate && temp)
                       || (negate && !temp)))
            {
                // We've found something we want, in an OR. We OR a true with
                // the current value.
                //
                ret |= true;
            }

            // Otherwise, we don't need to do anything.
            //
        }
    }
    return ret;
}

FUNCTION(fun_orflags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(handle_flaglists(executor, fargs[0], fargs[1], false), buff, bufc);
}

FUNCTION(fun_andflags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(handle_flaglists(executor, fargs[0], fargs[1], true), buff, bufc);
}

FUNCTION(fun_strtrunc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nLeft = mux_atol(fargs[1]);
    if (nLeft < 0)
    {
        safe_range(buff, bufc);
        return;
    }
    else if (0 == nLeft)
    {
        return;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));
    size_t nClusters = co_cluster_count(p, slen);

    if (static_cast<size_t>(nLeft) < nClusters)
    {
        unsigned char out[LBUF_SIZE];
        size_t n = co_mid_cluster(out, p, slen, 0, static_cast<size_t>(nLeft));

        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (n > nMax) n = nMax;
        memcpy(*bufc, out, n);
        *bufc += n;
        **bufc = '\0';
    }
    else if (co_visible_length(p, slen) > 0)
    {
        safe_str(fargs[0], buff, bufc);
    }
}

FUNCTION(fun_ifelse)
{
    // This function assumes that its arguments have not been evaluated.
    //
    UTF8 *lbuff = alloc_lbuf("fun_ifelse");
    UTF8 *bp = lbuff;
    mux_exec(fargs[0], LBUF_SIZE-1, lbuff, &bp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    const UTF8 *save_switch = mudstate.switch_token;
    mudstate.switch_token = lbuff;
    if (!xlate(lbuff))
    {
        if (nfargs == 3)
        {
            mux_exec(fargs[2], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
                eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        }
    }
    else
    {
        mux_exec(fargs[1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    }
    mudstate.switch_token = save_switch;
    free_lbuf(lbuff);
}

// Mail functions borrowed from DarkZone.
//
// This function can take one of three formats:
//
// 1. mail(num)           --> returns message <num> for privs.
// 2. mail(executor)      --> returns number of messages for <executor>.
// 3. mail(executor, num) --> returns message <num> for <executor>.
// 4. mail()              --> returns number of messages for executor.
//
FUNCTION(fun_mail)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref playerask;
    int num, rc, uc, cc;

    // Make sure we have the right number of arguments.
    //
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        count_mail(executor, 0, &rc, &uc, &cc);
        safe_ltoa(rc + uc, buff, bufc);
        return;
    }
    else if (nfargs == 1)
    {
        if (!is_integer(fargs[0], nullptr))
        {
            // Handle the case of wanting to count the number of
            // messages.
            //
            playerask = lookup_player(executor, fargs[0], true);
            if (NOTHING == playerask)
            {
                playerask = match_thing_quiet(executor, fargs[0]);
                if (!isPlayer(playerask))
                {
                    safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
                    return;
                }
            }

            if (  playerask == executor
               || Wizard(executor))
            {
                count_mail(playerask, 0, &rc, &uc, &cc);
                safe_tprintf_str(buff, bufc, T("%d %d %d"), rc, uc, cc);
            }
            else
            {
                safe_noperm(buff, bufc);
            }
            return;
        }
        else
        {
            playerask = executor;
            num = mux_atol(fargs[0]);
        }
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (playerask == NOTHING)
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
        else if (  (playerask == executor && !mudstate.nObjEvalNest)
                || God(executor))
        {
            num = mux_atol(fargs[1]);
        }
        else
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    if (  num < 1
       || !isPlayer(playerask))
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    const UTF8 *p = mail_fetch_message(playerask, num);
    if (p)
    {
        safe_str(p, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
    }
}

FUNCTION(fun_mailsize)
{
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(caller);

    dbref playerask = lookup_player(executor, fargs[0], 1);
    if (!Good_obj(playerask))
    {
        safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
    }
    else if (  executor == playerask
            || Wizard(executor))
    {
        size_t totalsize = 0;
        MailList ml(playerask);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            totalsize += MessageFetchSize(mp->number) + 1;
        }
        safe_ltoa(static_cast<long>(totalsize), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_mailsubj)
{
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(caller);

    dbref playerask;
    int num;

    if (1 == nfargs)
    {
        playerask = executor;
        num = mux_atol(fargs[0]);
    }
    else
    {
        playerask = lookup_player(executor, fargs[0], 1);
        if (NOTHING == playerask)
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
        else if (  executor == playerask
                || Wizard(executor))
        {
            num = mux_atol(fargs[1]);
        }
        else
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    if (  num < 1
       || !isPlayer(playerask))
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
    }
    else
    {
        struct mail *mp = mail_fetch(playerask, num);
        if (mp)
        {
            safe_str(mp->subject, buff, bufc);
        }
        else
        {
            safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        }
    }
}

// This function can take these formats:
//
//  1) mailfrom(<num>)
//  2) mailfrom(<executor>,<num>)
//
// It returns the dbref of the executor the mail is from.
//
FUNCTION(fun_mailfrom)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Make sure we have the right number of arguments.
    //
    int num;
    dbref playerask;
    if (nfargs == 1)
    {
        playerask = executor;
        num = mux_atol(fargs[0]);
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (playerask == NOTHING)
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
        if (  playerask == executor
           || Wizard(executor))
        {
            num = mux_atol(fargs[1]);
        }
        else
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    if (  num < 1
       || !isPlayer(playerask))
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    int from = mail_fetch_from(playerask, num);
    if (NOTHING != from)
    {
        safe_tprintf_str(buff, bufc, T("#%d"), from);
        return;
    }

    // Ran off the end of the list without finding anything.
    //
    safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_mailreview: functional equivalent of @mail/review <player>.
//
//  1) mailreview(<player>)       -- count of messages executor sent to <player>
//  2) mailreview(<player>,<num>) -- text of executor's <num>-th sent message
//
FUNCTION(fun_mailreview)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (target == NOTHING)
    {
        safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
        return;
    }

    if (!isPlayer(target))
    {
        safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
        return;
    }

    if (nfargs == 1)
    {
        // Count messages the executor has sent to target.
        //
        int count = 0;
        MailList ml(target);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            if (mail_from_player(executor, mp))
            {
                count++;
            }
        }
        safe_ltoa(count, buff, bufc);
    }
    else // nfargs == 2
    {
        int num = mux_atol(fargs[1]);
        if (num < 1)
        {
            safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
            return;
        }

        // Find the num-th message sent by executor to target.
        //
        int count = 0;
        MailList ml(target);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            if (mail_from_player(executor, mp))
            {
                count++;
                if (count == num)
                {
                    safe_str(MessageFetch(mp->number), buff, bufc);
                    return;
                }
            }
        }
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// mailcount([player]) — total message count.
// ---------------------------------------------------------------------------
//
// Permission: self always; Wizard for others.
//
FUNCTION(fun_mailcount)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref playerask;
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        playerask = executor;
    }
    else
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (!Good_obj(playerask) || !isPlayer(playerask))
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
    }

    if (playerask != executor && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int rc, uc, cc;
    count_mail(playerask, 0, &rc, &uc, &cc);
    safe_ltoa(rc + uc, buff, bufc);
}

// ---------------------------------------------------------------------------
// mailstats([player]) — read/unread/cleared counts.
// ---------------------------------------------------------------------------
//
// Permission: self always; Wizard for others.
//
FUNCTION(fun_mailstats)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref playerask;
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        playerask = executor;
    }
    else
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (!Good_obj(playerask) || !isPlayer(playerask))
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
    }

    if (playerask != executor && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int rc, uc, cc;
    count_mail(playerask, 0, &rc, &uc, &cc);
    safe_tprintf_str(buff, bufc, T("%d %d %d"), rc, uc, cc);
}

// ---------------------------------------------------------------------------
// maillist([player]) — space-separated list of valid message numbers.
// ---------------------------------------------------------------------------
//
// Permission: self always; Wizard for others.
//
FUNCTION(fun_maillist)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref playerask;
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        playerask = executor;
    }
    else
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (!Good_obj(playerask) || !isPlayer(playerask))
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
    }

    if (playerask != executor && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int pg_offset = 0;
    int pg_limit  = 0;
    if (nfargs >= 2) pg_offset = mux_atol(fargs[1]);
    if (nfargs >= 3) pg_limit  = mux_atol(fargs[2]);
    if (pg_offset < 0) pg_offset = 0;
    if (pg_limit < 0)  pg_limit = 0;

    // Use count_mail to determine the total, then list 1..N.
    // This matches what mail_fetch() considers valid indices.
    //
    int rc, uc, cc;
    count_mail(playerask, 0, &rc, &uc, &cc);
    int total = rc + uc;

    // Optimize: maillist outputs sequential integers, so we can compute
    // the range directly instead of iterating.
    //
    int first = pg_offset + 1;
    int last  = total;
    if (pg_limit > 0 && first + pg_limit - 1 < last)
    {
        last = first + pg_limit - 1;
    }
    for (int i = first; i <= last; i++)
    {
        if (i > first)
        {
            safe_chr(' ', buff, bufc);
        }
        safe_ltoa(i, buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// mailinfo(msg#, field[, player]) — generic per-message field accessor.
// ---------------------------------------------------------------------------
//
// Per-field cross-player permissions:
//   body:  self (with nObjEvalNest guard) or God
//   all others: self or Wizard
//
FUNCTION(fun_mailinfo)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int num = mux_atol(fargs[0]);
    const UTF8 *field = fargs[1];

    dbref playerask;
    if (nfargs >= 3 && fargs[2][0] != '\0')
    {
        playerask = lookup_player(executor, fargs[2], true);
        if (!Good_obj(playerask) || !isPlayer(playerask))
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
    }
    else
    {
        playerask = executor;
    }

    // Permission check: body field uses God-only for cross-player,
    // all other fields use Wizard.
    //
    bool bIsBody = (0 == mux_stricmp(field, T("body")));
    if (playerask != executor)
    {
        if (bIsBody)
        {
            if (!God(executor))
            {
                safe_noperm(buff, bufc);
                return;
            }
        }
        else
        {
            if (!Wizard(executor))
            {
                safe_noperm(buff, bufc);
                return;
            }
        }
    }
    else if (bIsBody && mudstate.nObjEvalNest)
    {
        // Self-access body check: nObjEvalNest guard.
        //
        safe_noperm(buff, bufc);
        return;
    }

    if (num < 1 || !isPlayer(playerask))
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    struct mail *mp = mail_fetch(playerask, num);
    if (nullptr == mp)
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    if (0 == mux_stricmp(field, T("from")))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), mp->from);
    }
    else if (0 == mux_stricmp(field, T("to")))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), mp->to);
    }
    else if (0 == mux_stricmp(field, T("tolist")))
    {
        if (mp->tolist)
        {
            safe_str(mp->tolist, buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("subject")))
    {
        if (mp->subject)
        {
            safe_str(mp->subject, buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("time")))
    {
        if (mp->time)
        {
            safe_str(mp->time, buff, bufc);
        }
    }
    else if (bIsBody)
    {
        const UTF8 *msg = MessageFetch(mp->number);
        if (msg)
        {
            safe_str(msg, buff, bufc);
        }
    }
    else if (0 == mux_stricmp(field, T("size")))
    {
        safe_ltoa(static_cast<long>(MessageFetchSize(mp->number)), buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("flags")))
    {
        if (Read(mp))     safe_chr('R', buff, bufc);
        if (Urgent(mp))   safe_chr('U', buff, bufc);
        if (Cleared(mp))  safe_chr('C', buff, bufc);
        if (Tagged(mp))   safe_chr('T', buff, bufc);
        if (Forward(mp))  safe_chr('F', buff, bufc);
        if (mp->read & M_REPLY) safe_chr('P', buff, bufc);
    }
    else if (0 == mux_stricmp(field, T("read")))
    {
        safe_chr(Read(mp) ? '1' : '0', buff, bufc);
    }
    else
    {
        safe_str(T("#-1 INVALID FIELD"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// mailflags(msg#[, player]) — shorthand for mailinfo flags field.
// ---------------------------------------------------------------------------
//
// Permission: self always; Wizard for cross-player.
//
FUNCTION(fun_mailflags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int num = mux_atol(fargs[0]);

    dbref playerask;
    if (nfargs >= 2 && fargs[1][0] != '\0')
    {
        playerask = lookup_player(executor, fargs[1], true);
        if (!Good_obj(playerask) || !isPlayer(playerask))
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
    }
    else
    {
        playerask = executor;
    }

    if (playerask != executor && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    if (num < 1 || !isPlayer(playerask))
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    struct mail *mp = mail_fetch(playerask, num);
    if (nullptr == mp)
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
        return;
    }

    if (Read(mp))     safe_chr('R', buff, bufc);
    if (Urgent(mp))   safe_chr('U', buff, bufc);
    if (Cleared(mp))  safe_chr('C', buff, bufc);
    if (Tagged(mp))   safe_chr('T', buff, bufc);
    if (Forward(mp))  safe_chr('F', buff, bufc);
    if (mp->read & M_REPLY) safe_chr('P', buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_hasattr: does object X have attribute Y.
// Hasattr (and hasattrp, which is derived from hasattr) borrowed from
// TinyMUSH 2.2.

static void hasattr_handler(UTF8 *buff, UTF8 **bufc, dbref executor, UTF8 *fargs[],
                   bool bCheckParent)
{
    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    ATTR *pattr = atr_str(fargs[1]);
    bool result = false;
    if (pattr)
    {
        if (!bCanReadAttr(executor, thing, pattr, bCheckParent))
        {
            safe_noperm(buff, bufc);
            return;
        }
        else
        {
            if (bCheckParent)
            {
                dbref aowner;
                int aflags;
                UTF8 *tbuf = atr_pget(thing, pattr->number, &aowner, &aflags);
                result = (tbuf[0] != '\0');
                free_lbuf(tbuf);
            }
            else
            {
                const UTF8 *tbuf = atr_get_raw(thing, pattr->number);
                result = (tbuf != nullptr);
            }
        }
    }
    safe_bool(result, buff, bufc);
}

FUNCTION(fun_hasattr)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    hasattr_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_hasattrp)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    hasattr_handler(buff, bufc, executor, fargs, true);
}

/* ---------------------------------------------------------------------------
 * fun_default, fun_edefault, and fun_udefault:
 * These check for the presence of an attribute. If it exists, then it
 * is gotten, via the equivalent of get(), get_eval(), or u(), respectively.
 * Otherwise, the default message is used.
 * In the case of udefault(), the remaining arguments to the function
 * are used as arguments to the u().
 */

// default(), edefault(), and udefault() borrowed from TinyMUSH 2.2
//
#define DEFAULT_DEFAULT  1
#define DEFAULT_EDEFAULT 2
#define DEFAULT_UDEFAULT 4

static void default_handler(UTF8 *buff, UTF8 **bufc, dbref executor,
                            dbref caller, dbref enactor, int eval,
                            UTF8 *fargs[], int nfargs, const UTF8 *cargs[],
                            int ncargs, int key)
{
    // Evaluating the first argument.
    //
    UTF8 *objattr = alloc_lbuf("default_handler");
    UTF8 *bp = objattr;
    mux_exec(fargs[0], LBUF_SIZE-1, objattr, &bp, executor, caller, enactor,
             eval|EV_EVAL|EV_STRIP_CURLY|EV_FCHECK, cargs, ncargs);
    *bp = '\0';

    // Parse the first argument as either <dbref>/<attrname> or <attrname>.
    //
    dbref thing;
    ATTR *pattr;

    if (!parse_attrib(executor, objattr, &thing, &pattr))
    {
        thing = executor;
        pattr = atr_str(objattr);
    }
    free_lbuf(objattr);

    if (  pattr
       && See_attr(executor, thing, pattr))
    {
        dbref aowner;
        int   aflags;
        UTF8 *atr_gotten = atr_pget(thing, pattr->number, &aowner, &aflags);
        if (atr_gotten[0] != '\0')
        {
            switch (key)
            {
            case DEFAULT_DEFAULT:
                safe_str(atr_gotten, buff, bufc);
                break;

            case DEFAULT_EDEFAULT:
                mux_exec(atr_gotten, LBUF_SIZE-1, buff, bufc, thing, executor, executor,
                     AttrTrace(aflags, EV_FIGNORE|EV_EVAL),
                     nullptr, 0);
                break;

            case DEFAULT_UDEFAULT:
                {
                    UTF8 *xargs[MAX_ARG];
                    int  nxargs = nfargs-2;
                    int  i;
                    for (i = 0; i < nxargs; i++)
                    {
                        xargs[i] = alloc_lbuf("fun_udefault_args");
                        UTF8 *bp2 = xargs[i];

                        mux_exec(fargs[i+2], LBUF_SIZE-1, xargs[i], &bp2,
                            thing, caller, enactor,
                            eval|EV_TOP|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL,
                            cargs, ncargs);
                        *bp2 = '\0';
                    }

                    mux_exec(atr_gotten, LBUF_SIZE-1, buff, bufc, thing, caller, enactor,
                        AttrTrace(aflags, EV_FCHECK|EV_EVAL), (const UTF8 **)xargs,
                        nxargs);

                    for (i = 0; i < nxargs; i++)
                    {
                        free_lbuf(xargs[i]);
                    }
                }
                break;

            }
            free_lbuf(atr_gotten);
            return;
        }
        free_lbuf(atr_gotten);
    }

    // If we've hit this point, we've not gotten anything useful, so
    // we go and evaluate the default.
    //
    mux_exec(fargs[1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
             eval|EV_EVAL|EV_STRIP_CURLY|EV_FCHECK, cargs, ncargs);
}


FUNCTION(fun_default)
{
    default_handler(buff, bufc, executor, caller, enactor, eval, fargs, nfargs,
        cargs, ncargs, DEFAULT_DEFAULT);
}

FUNCTION(fun_edefault)
{
    default_handler(buff, bufc, executor, caller, enactor, eval, fargs, nfargs,
        cargs, ncargs, DEFAULT_EDEFAULT);
}

FUNCTION(fun_udefault)
{
    default_handler(buff, bufc, executor, caller, enactor, eval, fargs, nfargs,
        cargs, ncargs, DEFAULT_UDEFAULT);
}

/* ---------------------------------------------------------------------------
 * fun_findable: can X locate Y
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_findable)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref obj = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(obj))
    {
        safe_match_result(obj, buff, bufc);
        safe_str(T(" (ARG1)"), buff, bufc);
        return;
    }
    dbref victim = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(victim))
    {
        safe_match_result(victim, buff, bufc);
        safe_str(T(" (ARG2)"), buff, bufc);
        return;
    }
#ifndef WOD_REALMS
#ifndef REALITY_LVLS
    safe_bool(locatable(obj, victim, obj), buff, bufc);
#else
    if (IsReal(obj, victim))
    {
        safe_bool(locatable(obj, victim, obj), buff, bufc);
    }
    else safe_chr('0', buff, bufc);
#endif
#else
#ifndef REALITY_LVLS
    if (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(obj, victim, ACTION_IS_STATIONARY))
    {
        safe_bool(locatable(obj, victim, obj), buff, bufc);
    }
    else
    {
        safe_chr('0', buff, bufc);
    }

#else
    if (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(obj, victim, ACTION_IS_STATIONARY))
    {
        safe_bool(locatable(obj, victim, obj), buff, bufc);
    }
    else if (IsReal(obj, victim))
    {
        safe_bool(locatable(obj, victim, obj), buff, bufc);
    }
    else safe_chr('0', buff, bufc);
#endif
#endif
}

/* ---------------------------------------------------------------------------
 * isword: is every character in the argument a letter?
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_isword)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool result;
    UTF8 *p = fargs[0];
    if ('\0' == p[0])
    {
        result = false;
    }
    else
    {
        result = true;
        for (int i = 0; '\0' != p[i]; i++)
        {
            if (!mux_isalpha(p[i]))
            {
                result = false;
                break;
            }
        }
    }
    safe_bool(result, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_visible. Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_visible)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        safe_str(T(" (ARG1)"), buff, bufc);
        return;
    }
    else if (!Controls(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    bool  result = false;
    dbref thing;
    ATTR  *pattr;
    if (!parse_attrib(executor, fargs[1], &thing, &pattr))
    {
        thing = match_thing_quiet(executor, fargs[1]);
        if (!Good_obj(thing))
        {
            safe_match_result(thing, buff, bufc);
            safe_str(T(" (ARG2)"), buff, bufc);
            return;
        }
    }
    if (Good_obj(thing))
    {
        if (pattr)
        {
            result = (See_attr(it, thing, pattr));
        }
        else
        {
            result = (Examinable(it, thing));
        }
    }
    safe_bool(result, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_elements: given a list of numbers, get corresponding elements from
 * the list.  elements(ack bar eep foof yay,2 4) ==> bar foof
 * The function takes a separator, but the separator only applies to the
 * first list.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_elements)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    if (1 == sep.n && 1 == osep.n)
    {
        // Single-char delimiter: use co_words_count + co_extract.
        //
        UTF8 *bp = trim_space_sep(fargs[0], sep);
        const unsigned char *p = reinterpret_cast<const unsigned char *>(bp);
        size_t slen = strlen(reinterpret_cast<const char *>(p));
        unsigned char delim = static_cast<unsigned char>(sep.str[0]);
        unsigned char out_delim = static_cast<unsigned char>(osep.str[0]);

        size_t nWords = co_words_count(p, slen, delim);

        bool bFirst = true;
        UTF8 *s = trim_space_sep(fargs[1], sepSpace);

        do
        {
            UTF8 *r = split_token(&s, sepSpace);
            int cur = mux_atol(r) - 1;
            if (  0 <= cur
               && static_cast<size_t>(cur) < nWords)
            {
                if (!bFirst)
                {
                    safe_chr(static_cast<UTF8>(out_delim), buff, bufc);
                }
                else
                {
                    bFirst = false;
                }

                unsigned char word[LBUF_SIZE];
                size_t nWord = co_extract(word, p, slen,
                    static_cast<size_t>(cur + 1), 1, delim, delim);

                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                if (nWord > nMax) nWord = nMax;
                memcpy(*bufc, word, nWord);
                *bufc += nWord;
            }
        } while (s);
        **bufc = '\0';
    }
    else
    {
        // Multi-char delimiter: use co_split_words.
        //
        const unsigned char *pData = reinterpret_cast<const unsigned char *>(fargs[0]);
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
        size_t nWords = co_split_words(pData, nLen,
                            reinterpret_cast<const unsigned char *>(sep.str),
                            sep.n, wstarts, wends, LBUF_SIZE);

        bool bFirst = true;
        UTF8 *s = trim_space_sep(fargs[1], sepSpace);

        do
        {
            UTF8 *r = split_token(&s, sepSpace);
            int cur = mux_atol(r) - 1;
            if (  0 <= cur
               && cur < static_cast<int>(nWords))
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                else
                {
                    bFirst = false;
                }
                size_t nb = wends[cur] - wstarts[cur];
                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                if (nb > nMax) nb = nMax;
                memcpy(*bufc, pData + wstarts[cur], nb);
                *bufc += nb;
            }
        } while (s);
        **bufc = '\0';
    }
}

// ---------------------------------------------------------------------------
// fun_between: Boolean range test.
//   between(low, high, value) — exclusive (strict less/greater)
//   between(low, high, value, 1) — inclusive (<=, >=)
//
FUNCTION(fun_between)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double lo  = mux_atof(fargs[0], false);
    double hi  = mux_atof(fargs[1], false);
    double val = mux_atof(fargs[2], false);

    bool bInclusive = (nfargs >= 4 && xlate(fargs[3]));
    bool bResult;
    if (bInclusive)
    {
        bResult = (lo <= val && val <= hi);
    }
    else
    {
        bResult = (lo < val && val < hi);
    }
    safe_bool(bResult, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_delextract: Delete a range of elements from a list.
//   delextract(list, first, count[, sep])
//
FUNCTION(fun_delextract)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(executor);

    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    int iFirst = mux_atol(fargs[1]);
    int nCount = mux_atol(fargs[2]);
    if (iFirst < 1) iFirst = 1;
    if (nCount < 1)
    {
        // Nothing to delete — return entire list.
        //
        safe_str(fargs[0], buff, bufc);
        return;
    }
    int iLast = iFirst + nCount - 1;

    UTF8 *bp = trim_space_sep(fargs[0], sep);
    bool bFirst = true;
    int pos = 1;
    while (bp)
    {
        UTF8 *tok = split_token(&bp, sep);
        if (pos < iFirst || pos > iLast)
        {
            if (!bFirst)
            {
                print_sep(sep, buff, bufc);
            }
            else
            {
                bFirst = false;
            }
            safe_str(tok, buff, bufc);
        }
        pos++;
    }
}

// ---------------------------------------------------------------------------
// fun_garble: Garble text at a configurable percentage.
//   garble(string, percent)
//   percent is 0-100, the chance each character is garbled.
//   Garbled characters are replaced with random printable ASCII.
//   Spaces are preserved.
//
FUNCTION(fun_garble)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int pct = mux_atol(fargs[1]);
    if (pct < 0)   pct = 0;
    if (pct > 100)  pct = 100;

    const UTF8 *p = fargs[0];
    while (*p)
    {
        if (*p == ' ')
        {
            safe_chr(' ', buff, bufc);
        }
        else if (RandomINT32(0, 99) < pct)
        {
            // Replace with random printable ASCII (33-126).
            //
            safe_chr(static_cast<UTF8>(33 + RandomINT32(0, 93)), buff, bufc);
        }
        else
        {
            safe_chr(*p, buff, bufc);
        }
        p++;
    }
}

// ---------------------------------------------------------------------------
// fun_moon: Return moon phase information.
//   moon() or moon(secs) — descriptive string
//   moon(secs, 1) — numeric percentage (positive=waxing, negative=waning)
//
// Algorithm: simplified lunation cycle. Average synodic month = 29.53059 days.
//
FUNCTION(fun_moon)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Get timestamp.
    //
    double secs;
    if (nfargs >= 1 && fargs[0][0] != '\0')
    {
        secs = mux_atof(fargs[0], false);
    }
    else
    {
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();
        secs = static_cast<double>(ltaNow.ReturnSeconds());
    }

    // Known new moon: Jan 6, 2000 18:14 UTC = 947182440
    //
    const double NEW_MOON_EPOCH = 947182440.0;
    const double SYNODIC_MONTH  = 29.53059 * 86400.0;  // in seconds

    double phase = fmod(secs - NEW_MOON_EPOCH, SYNODIC_MONTH);
    if (phase < 0) phase += SYNODIC_MONTH;

    // Phase as fraction of cycle (0.0 = new, 0.5 = full, 1.0 = new again).
    //
    double frac = phase / SYNODIC_MONTH;

    // Illumination: 0% at new (0.0), 100% at full (0.5), 0% at new (1.0).
    // Uses cosine curve: illumination = (1 - cos(2*pi*frac)) / 2 * 100
    //
    double illum = (1.0 - cos(2.0 * 3.14159265358979323846 * frac)) / 2.0 * 100.0;
    int pct = static_cast<int>(illum + 0.5);
    bool bWaxing = (frac < 0.5);

    int numeric = (nfargs >= 2 && xlate(fargs[1]));
    if (numeric)
    {
        // Return signed percentage: positive = waxing, negative = waning.
        //
        safe_ltoa(bWaxing ? pct : -pct, buff, bufc);
    }
    else
    {
        // Descriptive string.
        //
        if (pct >= 100)
        {
            safe_str(T("The Moon is Full"), buff, bufc);
        }
        else if (pct <= 0)
        {
            safe_str(T("The Moon is New"), buff, bufc);
        }
        else if (49 <= pct && pct <= 51)
        {
            safe_tprintf_str(buff, bufc, T("The Moon is %s Half (%d%% of Full)"),
                bWaxing ? T("Waxing") : T("Waning"), pct);
        }
        else if (pct > 50)
        {
            safe_tprintf_str(buff, bufc, T("The Moon is %s Gibbous (%d%% of Full)"),
                bWaxing ? T("Waxing") : T("Waning"), pct);
        }
        else
        {
            safe_tprintf_str(buff, bufc, T("The Moon is %s Crescent (%d%% of Full)"),
                bWaxing ? T("Waxing") : T("Waning"), pct);
        }
    }
}

// ---------------------------------------------------------------------------
// fun_soundex: Return Soundex encoding of a word.
//   soundex(word)
//
static void compute_soundex(const UTF8 *word, UTF8 result[5])
{
    // Soundex mapping: BFPV=1, CGJKQSXZ=2, DT=3, L=4, MN=5, R=6
    // AEIOUHWY=0 (not coded)
    //
    static const char sdx[] = {
        '0','1','2','3','0','1','2','0','0','2','2','4','5','5','0','1','2','6','2','3','0','1','0','2','0','2'
    };

    result[0] = '\0';
    if (!word || !*word || !mux_isalpha(*word))
    {
        return;
    }

    // Capitalize first letter.
    //
    result[0] = static_cast<UTF8>(mux_toupper_ascii(*word));
    int idx = 1;
    int ci0 = mux_isupper_ascii(*word) ? (*word - 'A') : (*word - 'a');
    char last = (ci0 >= 0 && ci0 < 26) ? sdx[ci0] : '0';

    word++;
    while (*word && idx < 4)
    {
        if (mux_isalpha(*word))
        {
            int ci = mux_isupper_ascii(*word) ? (*word - 'A') : (*word - 'a');
            if (ci >= 0 && ci < 26)
            {
                char code = sdx[ci];
                if (code != '0' && code != last)
                {
                    result[idx++] = static_cast<UTF8>(code);
                }
                last = code;
            }
        }
        word++;
    }
    while (idx < 4) result[idx++] = '0';
    result[4] = '\0';
}

FUNCTION(fun_soundex)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(nfargs);

    // Validate: single word, starts with alpha.
    //
    if (  !fargs[0][0]
       || !mux_isalpha(fargs[0][0])
       || strchr(reinterpret_cast<const char*>(fargs[0]), ' '))
    {
        safe_str(T("#-1 FUNCTION (SOUNDEX) REQUIRES A SINGLE WORD ARGUMENT"), buff, bufc);
        return;
    }

    UTF8 result[5];
    compute_soundex(fargs[0], result);
    safe_str(result, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_soundlike: Compare Soundex encodings of two words.
//   soundlike(word1, word2)
//
FUNCTION(fun_soundlike)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(nfargs);

    if (  !fargs[0][0] || !mux_isalpha(fargs[0][0])
       || strchr(reinterpret_cast<const char*>(fargs[0]), ' ')
       || !fargs[1][0] || !mux_isalpha(fargs[1][0])
       || strchr(reinterpret_cast<const char*>(fargs[1]), ' '))
    {
        safe_str(T("#-1 FUNCTION (SOUNDLIKE) REQUIRES SINGLE WORD ARGUMENTS"), buff, bufc);
        return;
    }

    UTF8 s1[5], s2[5];
    compute_soundex(fargs[0], s1);
    compute_soundex(fargs[1], s2);
    safe_bool(strcmp(reinterpret_cast<const char*>(s1),
                     reinterpret_cast<const char*>(s2)) == 0, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_caplist: Title-case a list of words with intelligent article handling.
//   caplist(list[, sep, osep])
//   Capitalizes the first letter of each word.  The words "a", "an", "the",
//   "and", "or", "but", "for", "nor", "of", "in", "on", "at", "to", "by",
//   "is" are kept lowercase unless they are the first or last word.
//
FUNCTION(fun_caplist)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    // Articles and short conjunctions/prepositions that stay lowercase
    // in title case (unless first or last word).
    //
    static const char *smalls[] = {
        "a", "an", "the", "and", "or", "but", "for", "nor",
        "of", "in", "on", "at", "to", "by", "is", "as", "if",
        "it", "so", "vs", "via", nullptr
    };

    // First pass: collect words to determine first/last.
    //
    UTF8 *words[LBUF_SIZE / 2];
    int nWords = 0;
    UTF8 *bp = trim_space_sep(fargs[0], sep);
    while (bp)
    {
        words[nWords++] = split_token(&bp, sep);
        if (nWords >= static_cast<int>(sizeof(words)/sizeof(words[0])))
        {
            break;
        }
    }

    for (int i = 0; i < nWords; i++)
    {
        if (i > 0)
        {
            print_sep(osep, buff, bufc);
        }

        UTF8 *w = words[i];
        bool bSmall = false;
        if (i > 0 && i < nWords - 1)
        {
            for (const char **sp = smalls; *sp; sp++)
            {
                if (mux_stricmp(w, reinterpret_cast<const UTF8*>(*sp)) == 0)
                {
                    bSmall = true;
                    break;
                }
            }
        }

        if (bSmall)
        {
            // Output lowercase.
            //
            for (const UTF8 *p = w; *p; p++)
            {
                safe_chr(static_cast<UTF8>(mux_tolower_ascii(*p)), buff, bufc);
            }
        }
        else
        {
            // Capitalize first letter, pass through rest.
            //
            if (*w && mux_isalpha(*w))
            {
                safe_chr(static_cast<UTF8>(mux_toupper_ascii(*w)), buff, bufc);
                w++;
            }
            safe_str(w, buff, bufc);
        }
    }
}

// ---------------------------------------------------------------------------
// fun_while: Iterate over a list while a condition is met.
//   while(eval-attr, cond-attr, list, compval[, isep, osep])
//
// For each element in list:
//   1. Evaluate eval-attr with element as %0, append result to output.
//   2. Evaluate cond-attr with element as %0.
//   3. If cond result equals compval (string match), stop.
//
FUNCTION(fun_while)
{
    SEP sep;
    if (!OPTIONAL_DELIM(5, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(6, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    // Get the eval attribute (fargs[0]).
    //
    UTF8 *eval_atext;
    dbref eval_thing;
    dbref eval_aowner;
    int   eval_aflags;
    if (!parse_and_get_attrib(executor, fargs, &eval_atext,
            &eval_thing, &eval_aowner, &eval_aflags, buff, bufc))
    {
        return;
    }

    // Get the cond attribute (fargs[1]).  Temporarily swap fargs[0].
    //
    UTF8 *save_farg0 = fargs[0];
    fargs[0] = fargs[1];

    UTF8 *cond_atext;
    dbref cond_thing;
    dbref cond_aowner;
    int   cond_aflags;
    bool  bHaveCond = parse_and_get_attrib(executor, fargs, &cond_atext,
            &cond_thing, &cond_aowner, &cond_aflags, buff, bufc);
    fargs[0] = save_farg0;

    if (!bHaveCond)
    {
        free_lbuf(eval_atext);
        return;
    }

    // Are eval and cond the same attribute?
    //
    bool bSameAttr = (strcmp(reinterpret_cast<const char*>(eval_atext),
                            reinterpret_cast<const char*>(cond_atext)) == 0);

    // Iterate over the list.
    //
    UTF8 *cp = trim_space_sep(fargs[2], sep);
    bool bFirst = true;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        UTF8 *element = split_token(&cp, sep);

        if (!bFirst)
        {
            print_sep(osep, buff, bufc);
        }
        bFirst = false;

        // Evaluate the eval attribute.
        //
        // Use ast_exec (not mux_exec) to bypass the JIT compiler.
        // The JIT does not support dynamically-provided cargs.
        //
        const UTF8 *eval_args[1] = { element };
        UTF8 *eval_result = alloc_lbuf("fun_while.eval");
        UTF8 *erp = eval_result;
        ast_exec(eval_atext, LBUF_SIZE-1, eval_result, &erp,
            eval_thing, executor, enactor,
            AttrTrace(eval_aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            eval_args, 1);
        *erp = '\0';

        // Append eval result to output.
        //
        safe_str(eval_result, buff, bufc);

        // Check stop condition.
        //
        bool bStop;
        if (bSameAttr)
        {
            // Optimization: compare eval result directly.
            //
            bStop = (strcmp(reinterpret_cast<const char*>(eval_result),
                           reinterpret_cast<const char*>(fargs[3])) == 0);
        }
        else
        {
            // Evaluate the cond attribute separately.
            //
            UTF8 *cond_result = alloc_lbuf("fun_while.cond");
            UTF8 *crp = cond_result;
            const UTF8 *cond_args[1] = { element };
            ast_exec(cond_atext, LBUF_SIZE-1, cond_result, &crp,
                cond_thing, executor, enactor,
                AttrTrace(cond_aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
                cond_args, 1);
            *crp = '\0';
            bStop = (strcmp(reinterpret_cast<const char*>(cond_result),
                           reinterpret_cast<const char*>(fargs[3])) == 0);
            free_lbuf(cond_result);
        }
        free_lbuf(eval_result);

        if (bStop)
        {
            break;
        }
    }

    free_lbuf(eval_atext);
    free_lbuf(cond_atext);
}

// ---------------------------------------------------------------------------
// fun_crc32obj: CRC32 checksum across all visible attributes on an object.
//   crc32obj(object)
//   Returns the cumulative CRC32 of all attribute values the executor
//   can see on the object.  Useful for integrity checking, detecting
//   attribute changes, and softcoded version control.
//
FUNCTION(fun_crc32obj)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(nfargs);

    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }
    if (!Examinable(executor, thing))
    {
        safe_noperm(buff, bufc);
        return;
    }

    uint32_t ulCRC32 = 0;
    atr_push();
    unsigned char *as;
    for (int ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        ATTR *pa = atr_num(ca);
        if (!pa || !See_attr(executor, thing, pa))
        {
            continue;
        }
        dbref aowner;
        int aflags;
        UTF8 *aval = atr_get("crc32obj", thing, ca, &aowner, &aflags);
        if (*aval)
        {
            size_t n = strlen(reinterpret_cast<const char*>(aval));
            ulCRC32 = CRC32_ProcessBuffer(ulCRC32, aval, n);
        }
        free_lbuf(aval);
    }
    atr_pop();
    safe_i64toa(ulCRC32, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_subnetmatch: Test whether an IP address is within a subnet.
//   subnetmatch(ip, network/bits)   — CIDR notation
//   subnetmatch(ip, network, mask)  — explicit netmask
//   IPv4 only.  Returns 1 if ip is within the subnet, 0 otherwise.
//
FUNCTION(fun_subnetmatch)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    struct in_addr addr, net, mask;

    // Parse the test IP address.
    //
    if (inet_pton(AF_INET, reinterpret_cast<const char*>(fargs[0]), &addr) != 1)
    {
        safe_str(T("#-1 INVALID IP ADDRESS"), buff, bufc);
        return;
    }

    if (nfargs == 2)
    {
        // CIDR form: network/bits
        //
        UTF8 *slash = (UTF8*)strchr(reinterpret_cast<char*>(fargs[1]), '/');
        if (!slash)
        {
            safe_str(T("#-1 INVALID CIDR NOTATION"), buff, bufc);
            return;
        }
        *slash = '\0';
        if (inet_pton(AF_INET, reinterpret_cast<const char*>(fargs[1]), &net) != 1)
        {
            safe_str(T("#-1 INVALID NETWORK ADDRESS"), buff, bufc);
            return;
        }
        int bits = mux_atol(slash + 1);
        if (bits < 0 || bits > 32)
        {
            safe_str(T("#-1 INVALID PREFIX LENGTH"), buff, bufc);
            return;
        }
        if (bits == 0)
        {
            mask.s_addr = 0;
        }
        else
        {
            mask.s_addr = htonl(0xFFFFFFFFU << (32 - bits));
        }
    }
    else
    {
        // Explicit mask form: network, mask
        //
        if (inet_pton(AF_INET, reinterpret_cast<const char*>(fargs[1]), &net) != 1)
        {
            safe_str(T("#-1 INVALID NETWORK ADDRESS"), buff, bufc);
            return;
        }
        if (inet_pton(AF_INET, reinterpret_cast<const char*>(fargs[2]), &mask) != 1)
        {
            safe_str(T("#-1 INVALID NETMASK"), buff, bufc);
            return;
        }
    }

    bool bMatch = ((addr.s_addr & mask.s_addr) == (net.s_addr & mask.s_addr));
    safe_bool(bMatch, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_wrapcolumns: Multi-column text formatting with word wrap.
//   wrapcolumns(text, width, ncols[, just, lborder, mborder, rborder, order])
//
//   just: L(eft)/R(ight)/C(enter) — default L
//   lborder/mborder/rborder: strings placed at left/between/right of columns
//   order: 0 = down-then-over (newspaper), 1 = over-then-down (row fill)
//
FUNCTION(fun_wrapcolumns)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(executor);

    int colWidth = mux_atol(fargs[1]);
    int nCols    = mux_atol(fargs[2]);
    if (colWidth < 1) colWidth = 1;
    if (nCols < 1)    nCols = 1;

    // Parse optional args.
    //
    UTF8 cJust = 'L';
    if (nfargs >= 4 && fargs[3][0])
    {
        cJust = static_cast<UTF8>(mux_toupper_ascii(fargs[3][0]));
    }
    const UTF8 *lBorder = (nfargs >= 5) ? fargs[4] : T("");
    const UTF8 *mBorder = (nfargs >= 6) ? fargs[5] : T("");
    const UTF8 *rBorder = (nfargs >= 7) ? fargs[6] : T("");
    int order = (nfargs >= 8) ? mux_atol(fargs[7]) : 0;

    // Word-wrap the input text into lines of at most colWidth chars.
    //
    UTF8 *lines[LBUF_SIZE / 2];
    int nLines = 0;

    UTF8 *src = fargs[0];
    while (*src && nLines < static_cast<int>(sizeof(lines)/sizeof(lines[0])))
    {
        // Find next line break point.
        //
        int len = strlen(reinterpret_cast<const char*>(src));
        if (len <= colWidth)
        {
            lines[nLines++] = src;
            break;
        }

        // Look for a space within colWidth chars to break at.
        //
        int brk = colWidth;
        while (brk > 0 && src[brk] != ' ')
        {
            brk--;
        }
        if (brk == 0)
        {
            // No space found — hard break at colWidth.
            //
            brk = colWidth;
        }

        // Null-terminate this line segment.
        //
        lines[nLines++] = src;
        src[brk] = '\0';
        src += brk + 1;

        // Skip leading spaces on next line.
        //
        while (*src == ' ') src++;
    }

    // Compute grid dimensions.
    //
    int nRows;
    if (order == 0)
    {
        // Down-then-over: fill columns top to bottom.
        //
        nRows = (nLines + nCols - 1) / nCols;
    }
    else
    {
        // Over-then-down: fill rows left to right.
        //
        nRows = (nLines + nCols - 1) / nCols;
    }

    // Output grid.
    //
    for (int r = 0; r < nRows; r++)
    {
        if (r > 0)
        {
            safe_str(T("\r\n"), buff, bufc);
        }
        safe_str(lBorder, buff, bufc);

        for (int c = 0; c < nCols; c++)
        {
            if (c > 0)
            {
                safe_str(mBorder, buff, bufc);
            }

            // Determine which line goes in this cell.
            //
            int idx;
            if (order == 0)
            {
                idx = c * nRows + r;  // column-major
            }
            else
            {
                idx = r * nCols + c;  // row-major
            }

            const UTF8 *cell = (idx < nLines) ? lines[idx] : T("");
            int cellLen = strlen(reinterpret_cast<const char*>(cell));
            int pad = colWidth - cellLen;
            if (pad < 0) pad = 0;

            switch (cJust)
            {
            case 'R':
                for (int p = 0; p < pad; p++) safe_chr(' ', buff, bufc);
                safe_str(cell, buff, bufc);
                break;

            case 'C':
                {
                    int lpad = pad / 2;
                    int rpad = pad - lpad;
                    for (int p = 0; p < lpad; p++) safe_chr(' ', buff, bufc);
                    safe_str(cell, buff, bufc);
                    for (int p = 0; p < rpad; p++) safe_chr(' ', buff, bufc);
                }
                break;

            default: // 'L'
                safe_str(cell, buff, bufc);
                for (int p = 0; p < pad; p++) safe_chr(' ', buff, bufc);
                break;
            }
        }
        safe_str(rBorder, buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_sandbox: Evaluate an expression with a restricted set of functions.
//   sandbox(expression, funclist[, reverse])
//
//   expression: the softcode to evaluate (NOEVAL — sandbox evaluates it)
//   funclist: space-separated list of function names
//   reverse: if 0 (default), funclist names are BLOCKED
//            if 1, funclist names are the ONLY ones ALLOWED
//
//   Blocked functions return #-1 PERMISSION DENIED during evaluation.
//
FUNCTION(fun_sandbox)
{
    // sandbox() is FN_NOEVAL — we evaluate args ourselves.
    //
    // First, evaluate arg1 (the function list) to get the names.
    //
    UTF8 *funclist_buf = alloc_lbuf("sandbox.funclist");
    UTF8 *flp = funclist_buf;
    mux_exec(fargs[1], LBUF_SIZE-1, funclist_buf, &flp, executor, caller, enactor,
        EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *flp = '\0';

    bool bReverse = false;
    if (nfargs >= 3)
    {
        UTF8 *rev_buf = alloc_lbuf("sandbox.reverse");
        UTF8 *rp = rev_buf;
        mux_exec(fargs[2], LBUF_SIZE-1, rev_buf, &rp, executor, caller, enactor,
            EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *rp = '\0';
        bReverse = xlate(rev_buf);
        free_lbuf(rev_buf);
    }

    // Collect the FUN pointers we need to modify, and save their original
    // perms so we can restore them after evaluation.
    //
    struct sandbox_entry {
        FUN *fp;
        int  saved_perms;
    };
    sandbox_entry entries[512];
    int nEntries = 0;

    auto record_entry = [&entries, &nEntries](FUN *fp) -> bool
    {
        for (int i = 0; i < nEntries; i++)
        {
            if (entries[i].fp == fp)
            {
                return false;
            }
        }
        if (nEntries >= static_cast<int>(sizeof(entries)/sizeof(entries[0])))
        {
            return false;
        }
        entries[nEntries].fp = fp;
        entries[nEntries].saved_perms = fp->perms;
        nEntries++;
        return true;
    };

    // Parse the function name list.
    //
    FUN *listed[512];
    int nListed = 0;
    {
        SEP sepSpace;
        sepSpace.n = 1;
        sepSpace.str[0] = ' ';
        UTF8 *wp = funclist_buf;
        while (wp)
        {
            UTF8 *tok = split_token(&wp, sepSpace);
            if (tok && *tok)
            {
                size_t nCased;
                UTF8 *pCased = mux_strupr(tok, nCased);
                std::vector<UTF8> name(pCased, pCased + nCased);
                auto it = mudstate.builtin_functions.find(name);
                if (  it != mudstate.builtin_functions.end()
                   && nListed < static_cast<int>(sizeof(listed)/sizeof(listed[0])))
                {
                    FUN *fp = static_cast<FUN*>(it->second);
                    bool bSeen = false;
                    for (int i = 0; i < nListed; i++)
                    {
                        if (listed[i] == fp)
                        {
                            bSeen = true;
                            break;
                        }
                    }
                    if (!bSeen)
                    {
                        listed[nListed++] = fp;
                    }
                }
            }
        }
    }

    if (bReverse)
    {
        // Reverse mode: block ALL functions except those listed.
        // Disable everything, then re-enable the listed ones.
        //
        for (auto &kv : mudstate.builtin_functions)
        {
            FUN *fp = static_cast<FUN*>(kv.second);
            if (record_entry(fp))
            {
                fp->perms |= CA_DISABLED;
            }
        }
        // Re-enable the allowed functions.
        //
        for (int i = 0; i < nListed; i++)
        {
            listed[i]->perms &= ~CA_DISABLED;
        }
    }
    else
    {
        // Normal mode: block only the listed functions.
        //
        for (int i = 0; i < nListed; i++)
        {
            if (record_entry(listed[i]))
            {
                listed[i]->perms |= CA_DISABLED;
            }
        }
    }
    free_lbuf(funclist_buf);

    // Force AST-only evaluation so that the permission checks in the
    // AST evaluator (check_access on fp->perms) are respected.  The JIT
    // compiles functions to native intrinsics that bypass fp->perms.
    //
    bool bSavedSandbox = mudstate.bSandboxActive;
    mudstate.bSandboxActive = true;

    mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
        EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);

    mudstate.bSandboxActive = bSavedSandbox;

    // Restore original permissions.
    //
    for (int i = 0; i < nEntries; i++)
    {
        entries[i].fp->perms = entries[i].saved_perms;
    }
}
