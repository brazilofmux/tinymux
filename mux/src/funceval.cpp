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

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "help.h"
#include "mail.h"
#include "misc.h"
#include "mathutil.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

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
    if (  !mudconf.have_comsys
       || (  !Comm_All(executor)
          && !Controls(executor, ch->charge_who)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int match_type = CWHO_ON;
    if (nfargs == 2)
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

    ITL pContext;
    struct comuser *user;
    ItemToList_Init(&pContext, buff, bufc, '#');
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (  (  match_type == CWHO_ALL
              || (  (Connected(user->who) || isThing(user->who))
                 && (  (match_type == CWHO_ON && user->bUserIsOn)
                    || (match_type == CWHO_OFF && !(user->bUserIsOn)))))
           && !ItemToList_AddInteger(&pContext, user->who))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
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
        UTF8 tmp[LBUF_SIZE];
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
    CMDENT *cmdp = (CMDENT *)hashfindLEN(name, strlen((const char *)name), &mudstate.command_htab);
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

#if defined(FIRANMUX)
FUNCTION(fun_setparent)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@parent"), buff, bufc))
    {
        return;
    }
    do_parent(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], nullptr, 0);
}

FUNCTION(fun_setname)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  !fargs[0]
       || !fargs[1]
       || check_command(executor, T("@name"), buff, bufc))
    {
        return;
    }
    do_name(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], nullptr, 0);
}

#endif // FIRANMUX

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
    do_pemit_single(executor, PEMIT_OEMIT, false, 0, fargs[0], 0, fargs[1]);
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

    CMDENT_ONE_ARG *cmdp = (CMDENT_ONE_ARG *)hashfindLEN(pCased, nCased,
        &mudstate.command_htab);

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
        safe_nothing(buff, bufc);
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

static UTF8 *crypt_code(UTF8 *code, UTF8 *text, bool type)
{
    if (  !text
       || text[0] == '\0')
    {
        return (UTF8 *)"";
    }
    if (  !code
       || code[0] == '\0')
    {
        return text;
    }

    UTF8 codebuff[LBUF_SIZE];
    size_t nCode = GenCode(codebuff, sizeof(codebuff), code);
    if (0 == nCode)
    {
        return text;
    }

    static UTF8 textbuff[LBUF_SIZE];
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

    mux_string *sStr = new mux_string(fargs[0]);

    sStr->compress(sep.str);
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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

    dbref zone = Zone(executor);
    if (!Good_obj(zone))
    {
        safe_str(T("#-1 INVALID ZONE"), buff, bufc);
        return;
    }

    // Find the user function attribute.
    //
    int attrib = get_atr(fargs[0]);
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

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(cp);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    LBUF_OFFSET nWords;
    if (  nullptr == sStr
       || nullptr == words
       || 0 == (nWords = words->find_Words(sep.str)))
    {
        delete sStr;
        delete words;
        return;
    }

    int nColumns = (78-nIndent)/nWidth;
    int iColumn = 0;
    int nLen = 0;
    mux_cursor iStart, iEnd, iWordStart, iWordEnd;

    size_t nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
    bool bNeedCRLF = false;
    for (LBUF_OFFSET i = 0; i < nWords && 0 < nBufferAvailable; i++)
    {
        if (iColumn == 0)
        {
            safe_fill(buff, bufc, ' ', nIndent);
        }

        iWordStart = words->wordBegin(i);
        iWordEnd = words->wordEnd(i);

        nLen = iWordEnd.m_point - iWordStart.m_point;
        if (nWidth < nLen)
        {
            nLen = nWidth;
        }

        iStart = iWordStart;
        sStr->cursor_from_point(iEnd, (LBUF_OFFSET)(iWordStart.m_point + nLen));
        nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
        *bufc += sStr->export_TextColor(*bufc, iStart, iEnd, nBufferAvailable);

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
    delete sStr;
    delete words;
}

// table(<list>,<field width>,<line length>,<delimiter>,<output separator>, <padding>)
//
// Ported from PennMUSH 1.7.3 by Morgan.
//
// TODO: Support ANSI in output separator and padding.
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
    UTF8 *pPaddingStart = nullptr;
    UTF8 *pPaddingEnd = nullptr;
    if (nfargs == 6 && *fargs[5])
    {
        pPaddingStart = strip_color(fargs[5]);
        pPaddingEnd = (UTF8 *)strchr((char *)pPaddingStart, '\0');
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
        mux_field fldLength = StripTabsAndTruncate( pCurrent, *bufc, nBufferAvailable,
                                                    static_cast<LBUF_OFFSET>(nFieldWidth));
        size_t nVisibleLength = fldLength.m_column;
        size_t nStringLength = fldLength.m_byte;

        *bufc += nStringLength;
        nBufferAvailable -= nStringLength;

        size_t nPaddingLength = nFieldWidth - nVisibleLength;
        if (nPaddingLength > nBufferAvailable)
        {
            nPaddingLength = nBufferAvailable;
        }
        if (nPaddingLength)
        {
            nBufferAvailable -= nPaddingLength;
            if (pPaddingStart)
            {
                for (  UTF8 *pPaddingCurrent = pPaddingStart;
                       nPaddingLength > 0;
                       nPaddingLength--)
                {
                    **bufc = *pPaddingCurrent;
                    (*bufc)++;
                    pPaddingCurrent++;

                    if (pPaddingCurrent == pPaddingEnd)
                    {
                        pPaddingCurrent = pPaddingStart;
                    }
                }
            }
            else
            {
                memset(*bufc, ' ', nPaddingLength);
                *bufc += nPaddingLength;
            }
        }

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
    size_t k = sizeof(struct object) + strlen((char *)Name(thing)) + 1;

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
                k += strlen((char *)str)+1;
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

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    mux_cursor nLen = sStr->length_cursor();

    if (nLeft < nLen.m_point)
    {
        mux_cursor iEnd;
        sStr->cursor_from_point(iEnd, (LBUF_OFFSET)nLeft);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, CursorMin, iEnd, nMax);
    }
    else if (0 < nLen.m_point)
    {
        safe_str(fargs[0], buff, bufc);
    }

    delete sStr;
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

    if (!mudconf.have_mailer)
    {
        safe_str(T("#-1 MAILER DISABLED."), buff, bufc);
        return;
    }

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

    if (!mudconf.have_mailer)
    {
        safe_str(T("#-1 MAILER DISABLED."), buff, bufc);
        return;
    }

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

    if (!mudconf.have_mailer)
    {
        safe_str(T("#-1 MAILER DISABLED."), buff, bufc);
        return;
    }

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

#ifdef FIRANMUX
// This function can take one of three formats:
//
//  1.  mailj(num)  --> returns message <num> for privs.
//  2.  mailj(executor)  --> returns number of messages for <executor>.
//  3.  mailj(executor, num)  --> returns message <num> for <executor>.
//
// It can now take one more format:
//
//  4.  mailj() --> returns number of messages for executor.
//
FUNCTION(fun_mailj)
{
    if (!mudconf.have_mailer)
    {
        safe_str(T("#-1 MAILER DISABLED."), buff, bufc);
        return;
    }

    dbref playerask;
    int num;
    struct mail *mp;

    if (  0 == nfargs
       || '\0' == fargs[0][0])
    {
        int cnt = 0;
        MailList ml(executor);
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            cnt++;
        }
        safe_ltoa(cnt, buff, bufc);
        return;
    }
    else if (1 == nfargs)
    {
        if (!is_integer(fargs[0], nullptr))
        {
            // Handle the case of wanting to count the number of messages.
            //
            playerask = lookup_player(executor, fargs[0], 1);
            if (NOTHING == playerask)
            {
                safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            }
            else if (  executor == playerask
                    || Wizard(executor))
            {
                int uc = 0;
                int rc = 0;
                int cc = 0;
                MailList ml(playerask);
                for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
                {
                    if (Read(mp))
                    {
                        rc++;
                    }
                    else
                    {
                        uc++;
                    }

                    if (Cleared(mp))
                    {
                        cc++;
                    }
                }
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
    else
    {
        playerask = lookup_player(executor, fargs[0], 1);
        if (NOTHING == playerask)
        {
            safe_str(T("#-1 NO SUCH PLAYER"), buff, bufc);
            return;
        }
        else if (  executor == playerask
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

    mp = mail_fetch(playerask, num);
    if (mp)
    {
        safe_str(MessageFetch(mp->number), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 NO SUCH MESSAGE"), buff, bufc);
    }
}
#endif // FIRANMUX

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

    if (!mudconf.have_mailer)
    {
        safe_str(T("#-1 MAILER DISABLED."), buff, bufc);
        return;
    }

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

    // Turn the first list into an array.
    //
    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr  = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr == sStr
       || nullptr == words)
    {
        delete sStr;
        delete words;
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str);

    bool bFirst = true;
    UTF8 *s = trim_space_sep(fargs[1], sepSpace);

    // Go through the second list, grabbing the numbers and finding the
    // corresponding elements.
    //
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
            words->export_WordColor(static_cast<LBUF_OFFSET>(cur), buff, bufc);
        }
    } while (s);

    delete sStr;
    delete words;
}
