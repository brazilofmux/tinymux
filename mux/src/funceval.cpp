/*! \file funceval.cpp
 * \brief MUX function handlers.
 *
 * $Id$
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

#include <limits.h>
#include <math.h>

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "help.h"
#include "mail.h"
#include "misc.h"
#include "pcre.h"
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
          && executor != ch->charge_who))
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

void SimplifyColorLetters(UTF8 Out[8], UTF8 *pIn)
{
    UTF8 *pOut = Out;
    if (  pIn[0] == 'n'
       && pIn[1] == '\0')
    {
        pOut[0] = 'n';
        pOut[1] = '\0';
        return;
    }
    ColorState have = 0;
    size_t nIn = strlen((char *)pIn);
    for (size_t i = 1; i <= nIn && pIn[nIn - i] != 'n'; i++)
    {
        ColorState mask = aColors[ColorTable[pIn[nIn - i]]].csMask;
        if (  mask
           && (have & mask) == 0)
        {
            *pOut++ = pIn[nIn - i];
            have |= mask;
        }
    }
    *pOut = '\0';
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
        UTF8   pOut[8];
        SimplifyColorLetters(pOut, fargs[iArg0]);
        UTF8 tmp[LBUF_SIZE];
        UTF8 *bp = tmp;

        for (size_t i = 0; '\0' != pOut[i]; i++)
        {
            unsigned int iColor = ColorTable[pOut[i]];
            if (0 < iColor)
            {
                safe_str(aColors[iColor].pUTF, tmp, &bp);
            }
        }
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
        safe_tprintf_str(buff, bufc, "#%d", Zone(it));
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
    do_link(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], NULL, 0);
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
    do_parent(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], NULL, 0);
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
    do_name(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1], NULL, 0);
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
    do_trigger(executor, caller, enactor, eval, 0, fargs[0], fargs+1, nfargs-1, NULL, 0);
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
    do_wipe(executor, caller, enactor, eval, 0, fargs[0], NULL, 0);
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

    do_teleport(executor, caller, enactor, eval, key, 2, fargs[0], fargs[1], NULL, 0);
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
    do_say(executor, caller, enactor, 0, SAY_EMIT, fargs[0], NULL, 0);
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
    do_cemit(executor, caller, enactor, eval, 0, nfargs, fargs[0], fargs[1], NULL, 0);
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
#if defined(HAVE_DLOPEN) || defined(WIN32)
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (NULL != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
#endif
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
#if defined(HAVE_DLOPEN) || defined(WIN32)
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (NULL != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
#endif
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
#if defined(HAVE_DLOPEN) || defined(WIN32)
            ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
            while (NULL != p)
            {
                p->pSink->data_create(thing);
                p = p->pNext;
            }
#endif
        }
        break;
    }
    safe_tprintf_str(buff, bufc, "#%d", thing);
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
    do_destroy(executor, caller, enactor, 0, DEST_ONE, fargs[0], NULL, 0);
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
        notify_quiet(player, T("You shouldn't be rummaging through the garbage."));
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

    reg_ref **preserve = NULL;
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

    UTF8 *cp = trim_space_sep(fargs[0], &sep);
    if (!*cp)
    {
        return;
    }

    mux_string *sStr = NULL;
    mux_words *words = NULL;
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
    if (  NULL == sStr
       || NULL == words
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
    UTF8 *pPaddingStart = NULL;
    UTF8 *pPaddingEnd = NULL;
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
    UTF8 *pNext = trim_space_sep(fargs[0], &sep);
    if (!*pNext)
    {
        return;
    }

    UTF8 *pCurrent = split_token(&pNext, &sep);
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

        pCurrent = split_token(&pNext, &sep);
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

    mux_string *sStr = NULL;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == sStr)
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
        if (!is_integer(fargs[0], NULL))
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
                safe_tprintf_str(buff, bufc, "%d %d %d", rc, uc, cc);
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

#ifdef FIRANMUX
FUNCTION(fun_mailsize)
{
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
        int totalsize = 0;
        MailList ml(playerask);
        struct mail *mp;
        for (mp = ml.FirstItem(); !ml.IsEnd(); mp = ml.NextItem())
        {
            totalsize += MessageFetchSize(mp->number) + 1;
        }
        safe_ltoa(totalsize, buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_mailsubj)
{
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
        if (!is_integer(fargs[0], NULL))
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
                safe_tprintf_str(buff, bufc, "%d %d %d", rc, uc, cc);
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
        safe_tprintf_str(buff, bufc, "#%d", from);
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
                result = (tbuf != NULL);
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
                     NULL, 0);
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

    UTF8 *p;
    bool result = true;

    for (p = fargs[0]; *p; p++)
    {
        if (!mux_isalpha(*p))
        {
            result = false;
            break;
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
    mux_string *sStr = NULL;
    mux_words *words = NULL;
    try
    {
        sStr  = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  NULL == sStr
       || NULL == words)
    {
        delete sStr;
        delete words;
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str);

    bool bFirst = true;
    UTF8 *s = trim_space_sep(fargs[1], &sepSpace);

    // Go through the second list, grabbing the numbers and finding the
    // corresponding elements.
    //
    do
    {
        UTF8 *r = split_token(&s, &sepSpace);
        int cur = mux_atol(r) - 1;
        if (  0 <= cur
           && cur < static_cast<int>(nWords))
        {
            if (!bFirst)
            {
                print_sep(&osep, buff, bufc);
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

/* ---------------------------------------------------------------------------
 * fun_grab: a combination of extract() and match(), sortof. We grab the
 *           single element that we match.
 *
 *  grab(Test:1 Ack:2 Foof:3,*:2)    => Ack:2
 *  grab(Test-1+Ack-2+Foof-3,*o*,+)  => Foof-3
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_grab)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Walk the wordstring, until we find the word we want.
    //
    UTF8 *s = trim_space_sep(fargs[0], &sep);
    do
    {
        UTF8 *r = split_token(&s, &sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            safe_str(r, buff, bufc);
            return;
        }
    } while (s);
}

FUNCTION(fun_graball)
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

    bool bFirst = true;
    UTF8 *s = trim_space_sep(fargs[0], &sep);
    do
    {
        UTF8 *r = split_token(&s, &sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            if (!bFirst)
            {
                print_sep(&osep, buff, bufc);
            }
            else
            {
                bFirst = false;
            }
            safe_str(r, buff, bufc);
        }
    } while (s);
}

/* ---------------------------------------------------------------------------
 * fun_scramble:  randomizes the letters in a string.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_scramble)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_string *sStr = NULL;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == sStr)
    {
        return;
    }

    LBUF_OFFSET nPoints = sStr->length_cursor().m_point;

    if (2 <= nPoints)
    {
        mux_string *sOut = NULL;
        try
        {
            sOut = new mux_string;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == sOut)
        {
            delete sStr;
            return;
        }

        LBUF_OFFSET iPoint;
        mux_cursor iStart, iEnd;
        while (0 < nPoints)
        {
            iPoint = static_cast<LBUF_OFFSET>(RandomINT32(0, static_cast<INT32>(nPoints-1)));
            sStr->cursor_from_point(iStart, iPoint);
            sStr->cursor_from_point(iEnd, iPoint + 1);
            sOut->append(*sStr, iStart, iEnd);
            sStr->delete_Chars(iStart, iEnd);
            nPoints--;
        }
        *bufc += sOut->export_TextColor(*bufc, CursorMin, CursorMax, buff + (LBUF_SIZE-1) - *bufc);
        delete sOut;
    }
    else
    {
        safe_str(fargs[0], buff, bufc);
    }

    delete sStr;
}

/* ---------------------------------------------------------------------------
 * fun_shuffle: randomize order of words in a list.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_shuffle)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    mux_string *sIn = NULL;
    try
    {
        sIn = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == sIn)
    {
        return;
    }

    mux_words *words = NULL;
    try
    {
        words = new mux_words(*sIn);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == words)
    {
        delete sIn;
        return;
    }

    LBUF_OFFSET n = words->find_Words(sep.str);
    mux_string *sOut = NULL;
    try
    {
        sOut = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == sOut)
    {
        delete sIn;
        delete words;
        return;
    }

    bool bFirst = true;
    LBUF_OFFSET i = 0;
    mux_cursor iStart = CursorMin, iEnd = CursorMin;

    while (n > 0)
    {
        if (bFirst)
        {
            bFirst = false;
        }
        else
        {
            sOut->append(osep.str, osep.n);
        }
        i = static_cast<LBUF_OFFSET>(RandomINT32(0, static_cast<INT32>(n-1)));
        iStart = words->wordBegin(i);
        iEnd = words->wordEnd(i);
        sOut->append(*sIn, iStart, iEnd);
        words->ignore_Word(i);
        n--;
    }
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sOut->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete words;
    delete sIn;
    delete sOut;
}

// pickrand -- choose a random item from a list.
//
FUNCTION(fun_pickrand)
{
    if (  nfargs == 0
       || fargs[0][0] == '\0')
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *s = trim_space_sep(fargs[0], &sep);
    if (s[0] == '\0')
    {
        return;
    }

    mux_string *sStr = NULL;
    mux_words *words = NULL;
    try
    {
        sStr = new mux_string(s);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  NULL != sStr
       && NULL != words)
    {
        INT32 n = static_cast<INT32>(words->find_Words(sep.str));

        if (0 < n)
        {
            LBUF_OFFSET w = static_cast<LBUF_OFFSET>(RandomINT32(0, n-1));
            words->export_WordColor(w, buff, bufc);
        }
    }
    delete sStr;
    delete words;
}

// sortby()
//
typedef struct
{
    UTF8  *buff;;
    dbref executor;
    dbref caller;
    dbref enactor;
    int   aflags;
} ucomp_context;

static int u_comp(ucomp_context *pctx, const void *s1, const void *s2)
{
    if (  mudstate.func_invk_ctr > mudconf.func_invk_lim
       || mudstate.func_nest_lev > mudconf.func_nest_lim
       || MuxAlarm.bAlarmed)
    {
        return 0;
    }

    const UTF8 *elems[2] = { T(s1), T(s2) };

    UTF8 *tbuf = alloc_lbuf("u_comp");
    mux_strncpy(tbuf, pctx->buff, LBUF_SIZE-1);
    UTF8 *result = alloc_lbuf("u_comp");
    UTF8 *bp = result;
    mux_exec(tbuf, LBUF_SIZE-1, result, &bp, pctx->executor, pctx->caller, pctx->enactor,
             AttrTrace(pctx->aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), elems, 2);
    *bp = '\0';
    int n = mux_atol(result);
    free_lbuf(result);
    free_lbuf(tbuf);
    return n;
}

inline int ucomp_bsearch(ucomp_context* pctx, void* arr[], int sz, void* ndl)
{
    int l = 0;
    int r = sz;
    while (l < r)
    {
        int m = (l + r) >> 1;
        if (m == sz)
        {
            return sz;
        }

        if (u_comp(pctx, ndl, arr[m]) < 0)
        {
            r = m;
        }
        else
        {
            l = m + 1;
        }
    }
    return l;
}

static void mincomp_sort(ucomp_context* pctx, void* arr[], int sz)
{
    if (sz <= 1)
    {
        return;
    }

    for (int i = 1; i < sz; i++)
    {
        void* t = arr[i];
        int n = ucomp_bsearch(pctx, arr, i, t);
        for (int j = i; j > n; j--)
        {
            arr[j] = arr[j-1];
        }
        arr[n] = t;
    }
}

FUNCTION(fun_sortby)
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

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    ucomp_context ctx;
    ctx.buff = alloc_lbuf("fun_sortby.ctx");
    mux_strncpy(ctx.buff, atext, LBUF_SIZE-1);
    ctx.executor = thing;
    ctx.caller   = executor;
    ctx.enactor  = enactor;
    ctx.aflags   = aflags;

    UTF8 *list = alloc_lbuf("fun_sortby");
    mux_strncpy(list, fargs[1], LBUF_SIZE-1);
    UTF8 *ptrs[LBUF_SIZE / 2];
    int nptrs = list2arr(ptrs, LBUF_SIZE / 2, list, &sep);

    if (nptrs > 1)
    {
        mincomp_sort(&ctx, (void**)ptrs, nptrs);
    }

    arr2list(ptrs, nptrs, buff, bufc, &osep);
    free_lbuf(list);
    free_lbuf(ctx.buff);
    free_lbuf(atext);
}

// fun_last: Returns last word in a string. Borrowed from TinyMUSH 2.2.
//
FUNCTION(fun_last)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    mux_string *sStr = NULL;
    mux_words *words = NULL;
    try
    {
        sStr = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  NULL != sStr
       && NULL != words)
    {
        LBUF_OFFSET nWords = words->find_Words(sep.str);
        words->export_WordColor(nWords-1, buff, bufc);
    }
    delete sStr;
    delete words;
}


// For an named object, or the executor, find the last created object
// (optionally qualified by type).
//
FUNCTION(fun_lastcreate)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(cargs);

    // Determine the target by name, or use the executor if no name is given.
    //
    dbref target = executor;
    if (  0 < nfargs
       && '\0' != fargs[0][0])
    {
        target = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(target))
        {
            safe_nomatch(buff, bufc);
            return;
        }

        // Verify that the executor has access to the named object.  Notice
        // that an executor always has access to itself.
        //
        if (  !WizRoy(executor)
           && !Controls(executor, target))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    // If a type is given, qualify the result.
    //
    int iObjectPosition = 4;
    if (  1 < nfargs
       && '\0' != fargs[1][0])
    {
        switch (fargs[1][0])
        {
        case 'R':
        case 'r':
            iObjectPosition = 0;
            break;

        case 'T':
        case 't':
            iObjectPosition = 1;
            break;

        case 'E':
        case 'e':
            iObjectPosition = 2;
            break;

        case 'P':
        case 'p':
            iObjectPosition = 3;
            break;
        }
    }

    int aowner;
    int aflags;

    UTF8* newobject_string = atr_get("fun_lastcreate.2998", target,
            A_NEWOBJS, &aowner, &aflags);

    if (  NULL == newobject_string
       || '\0' == newobject_string)
    {
        safe_str(T("#-1"), buff, bufc);
        return;
    }

    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, newobject_string);
    mux_strtok_ctl(&tts, T(" "));

    int i;
    UTF8* ptr;
    for ( ptr = mux_strtok_parse(&tts), i = 0;
          NULL != ptr && i < 5;
          ptr = mux_strtok_parse(&tts), i++)
    {
        if (i == iObjectPosition)
        {
            dbref jLastCreated = mux_atol(ptr);
            safe_tprintf_str(buff, bufc, "#%d", jLastCreated);
            break;
        }
    }
    free_lbuf(newobject_string);
}

// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_matchall)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    int wcount;
    UTF8 *r, *s, *old, tbuf[I32BUF_SIZE];
    old = *bufc;

    // Check each word individually, returning the word number of all that
    // match. If none match, return 0.
    //
    wcount = 1;
    s = trim_space_sep(fargs[0], &sep);
    do
    {
        r = split_token(&s, &sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            mux_ltoa(wcount, tbuf);
            if (old != *bufc)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_str(tbuf, buff, bufc);
        }
        wcount++;
    } while (s);

    if (*bufc == old)
    {
        safe_chr('0', buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_ports: Returns a list of ports for a user.
// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_ports)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        if (target == executor || Wizard(executor))
        {
            if (Connected(target))
            {
                make_portlist(executor, target, buff, bufc);
            }
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_mix: Like map, but operates on up to ten lists simultaneously, passing
 * the elements as %0 - %10.
 * Borrowed from PennMUSH 1.50, upgraded by RhostMUSH.
 */
FUNCTION(fun_mix)
{
    // Check to see if we have an appropriate number of arguments.
    // If there are more than three arguments, the last argument is
    // ALWAYS assumed to be a delimiter.
    //
    SEP sep;
    int lastn;

    if (nfargs < 4)
    {
        sep.n = 1;
        sep.str[0] = ' ';
        sep.str[1] = '\0';
        lastn = nfargs - 1;
    }
    else if (!OPTIONAL_DELIM(nfargs, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    else
    {
        lastn = nfargs - 2;
    }

    // Get the attribute. Check the permissions.
    //
    dbref thing;
    UTF8 *atext;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Process the lists, one element at a time.
    //
    int i;
    int nwords = 0;
    UTF8 *cp[NUM_ENV_VARS];
    for (i = 0; i < lastn; i++)
    {
        cp[i] = trim_space_sep(fargs[i+1], &sep);
        int twords = countwords(cp[i], &sep);
        if (nwords < twords)
        {
            nwords = twords;
        }
    }

    const UTF8 *os[NUM_ENV_VARS];
    bool bFirst = true;
    for (  int wc = 0;
           wc < nwords
        && mudstate.func_invk_ctr < mudconf.func_invk_lim
        && !MuxAlarm.bAlarmed;
           wc++)
    {
        if (!bFirst)
        {
            print_sep(&sep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }

        for (i = 0; i < lastn; i++)
        {
            os[i] = split_token(&cp[i], &sep);
            if (NULL == os[i])
            {
                os[i] = T("");
            }
        }
        mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            os, lastn);
    }
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_step: A little like a fusion of iter() and mix(), it takes elements
 * of a list X at a time and passes them into a single function as %0, %1,
 * etc.   step(<attribute>,<list>,<step size>,<delim>,<outdelim>)
 */

FUNCTION(fun_step)
{
    int i;

    SEP isep;
    if (!OPTIONAL_DELIM(4, isep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = isep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    int step_size = mux_atol(fargs[2]);
    if (  step_size < 1
       || NUM_ENV_VARS < step_size)
    {
        notify(executor, T("Illegal step size."));
        return;
    }

    // Get attribute. Check permissions.
    //
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    UTF8 *cp = trim_space_sep(fargs[1], &isep);

    const UTF8 *os[NUM_ENV_VARS];
    bool bFirst = true;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        if (!bFirst)
        {
            print_sep(&osep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }

        for (i = 0; cp && i < step_size; i++)
        {
            os[i] = split_token(&cp, &isep);
        }
        mux_exec(atext, LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
             AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), os, i);
    }
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_foreach: like map(), but it operates on a string, rather than on a list,
 * calling a user-defined function for each character in the string.
 * No delimiter is inserted between the results.
 * Borrowed from TinyMUSH 2.2
 */
FUNCTION(fun_foreach)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  nfargs != 2
       && nfargs != 4)
    {
        safe_str(T("#-1 FUNCTION (FOREACH) EXPECTS 2 OR 4 ARGUMENTS"), buff, bufc);
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    UTF8 cbuf[5] = {'\0', '\0', '\0', '\0', '\0'};
    const UTF8 *bp = cbuf;
    mux_string *sStr = NULL;
    try
    {
        sStr = new mux_string(fargs[1]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == sStr)
    {
        return;
    }

    sStr->trim();
    size_t nStr = sStr->length_byte();
    LBUF_OFFSET i = 0, nBytes = 0;

    if (  4 == nfargs
       && '\0' != fargs[2]
       && '\0' != fargs[3])
    {
        bool flag = false;
        UTF8 prev = '\0';

        while (  i < nStr
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !MuxAlarm.bAlarmed)
        {
            nBytes = sStr->export_Char_UTF8(i, cbuf);
            i = i + nBytes;

            if (flag)
            {
                if (  cbuf[0] == *fargs[3]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = false;
                    continue;
                }
            }
            else
            {
                if (  cbuf[0] == *fargs[2]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = true;
                    continue;
                }
                else
                {
                    safe_copy_buf(cbuf, nBytes, buff, bufc);
                    continue;
                }
            }

            mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), &bp, 1);
            prev = cbuf[0];
        }
    }
    else
    {
        while (  i < nStr
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !MuxAlarm.bAlarmed)
        {
            nBytes = sStr->export_Char_UTF8(i, cbuf);

            mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), &bp, 1);
            i = i + nBytes;
        }
    }
    free_lbuf(atext);
    delete sStr;
}

/* ---------------------------------------------------------------------------
 * fun_munge: combines two lists in an arbitrary manner.
 * Borrowed from TinyMUSH 2.2
 * Hash table rewrite by Ian and Alierak.
 */
#if LBUF_SIZE < UINT16_MAX_VALUE
typedef UINT16 NHASH;
#define ShiftHash(x) (x) >>= 16
#else
typedef UINT32 NHASH;
#define ShiftHash(x)
#endif

typedef struct munge_htab_rec
{
    NHASH       nHash;         // partial hash value of this record's key
    LBUF_OFFSET iNext;         // index of next record in this hash chain
    LBUF_OFFSET nKeyOffset;    // offset of key string (incremented by 1),
                               //     zero indicates empty record.
    LBUF_OFFSET nValueOffset;  // offset of value string
} munge_htab_rec;

FUNCTION(fun_munge)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Find our object and attribute.
    //
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Copy list1 for later evaluation of the attribute.
    //
    UTF8 *list1 = alloc_lbuf("fun_munge.list1");
    mux_strncpy(list1, fargs[1], LBUF_SIZE - 1);

    // Prepare data structures for a hash table that will map
    // elements of list1 to corresponding elements of list2.
    //
    int nWords = countwords(fargs[1], &sep);
    if (0 == nWords)
    {
        free_lbuf(atext);
        free_lbuf(list1);
        return;
    }

    munge_htab_rec *htab = NULL;
    UINT16 *tails = NULL;
    try
    {
        htab = new munge_htab_rec[1 + 2 * nWords];
        tails = new UINT16[1 + nWords];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  NULL == htab
       || NULL == tails)
    {
        free_lbuf(atext);
        free_lbuf(list1);
        if (NULL != htab)
        {
            delete [] htab;
        }
        else if (NULL != tails)
        {
            delete [] tails;
        }
        return;
    }
    memset(htab, 0, sizeof(munge_htab_rec) * (1 + 2 * nWords));
    memset(tails, 0, sizeof(UINT16) * (1 + nWords));

    int iNext = 1 + nWords;  // first unused hash slot past starting area

    // Chop up the lists, converting them into a hash table that
    // maps elements of list1 to corresponding elements of list2.
    //
    UTF8 *p1 = trim_space_sep(fargs[1], &sep);
    UTF8 *p2 = trim_space_sep(fargs[2], &sep);
    UTF8 *pKey, *pValue;
    for (pKey = split_token(&p1, &sep), pValue = split_token(&p2, &sep);
         NULL != pKey && NULL != pValue;
         pKey = split_token(&p1, &sep), pValue = split_token(&p2, &sep))
    {
        UINT32 nHash = munge_hash(pKey);
        int nHashSlot = 1 + (nHash % nWords);
        ShiftHash(nHash);

        if (0 != tails[nHashSlot])
        {
            // there is already a hash chain starting in this slot,
            // insert at the tail to preserve order.
            nHashSlot = tails[nHashSlot] =
                htab[tails[nHashSlot]].iNext = static_cast<LBUF_OFFSET>(iNext++);
        }
        else
        {
            tails[nHashSlot] = static_cast<LBUF_OFFSET>(nHashSlot);
        }

        htab[nHashSlot].nHash = static_cast<NHASH>(nHash);
        htab[nHashSlot].nKeyOffset = static_cast<LBUF_OFFSET>(1 + pKey - fargs[1]);
        htab[nHashSlot].nValueOffset = static_cast<LBUF_OFFSET>(pValue - fargs[2]);
    }
    delete [] tails;

    if (  NULL != pKey
       || NULL != pValue)
    {
        safe_str(T("#-1 LISTS MUST BE OF EQUAL SIZE"), buff, bufc);
        free_lbuf(atext);
        free_lbuf(list1);
        delete [] htab;
        return;
    }

    // Call the u-function with the first list as %0.
    //
    UTF8 *rlist, *bp;
    const UTF8 *uargs[2];

    bp = rlist = alloc_lbuf("fun_munge");
    uargs[0] = list1;
    uargs[1] = sep.str;
    mux_exec(atext, LBUF_SIZE-1, rlist, &bp, executor, caller, enactor,
             AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), uargs, 2);
    *bp = '\0';
    free_lbuf(atext);
    free_lbuf(list1);

    // Now that we have our result, put it back into array form.
    // Translate its elements according to the mappings in our hash table.
    //
    bool bFirst = true;
    bp = trim_space_sep(rlist, &sep);
    if ('\0' != *bp)
    {
        UTF8 *result;
        for (result = split_token(&bp, &sep);
             NULL != result;
             result = split_token(&bp, &sep))
        {
            UINT32 nHash = munge_hash(result);
            int nHashSlot = 1 + (nHash % nWords);
            ShiftHash(nHash);

            while (  0 != htab[nHashSlot].nKeyOffset
                  && (  nHash != htab[nHashSlot].nHash
                     || 0 != strcmp((char *)result,
                                    (char *)(fargs[1] +
                                     htab[nHashSlot].nKeyOffset - 1))))
            {
                nHashSlot = htab[nHashSlot].iNext;
            }
            if (0 != htab[nHashSlot].nKeyOffset)
            {
                if (!bFirst)
                {
                    print_sep(&sep, buff, bufc);
                }
                else
                {
                    bFirst = false;
                }
                safe_str(fargs[2] + htab[nHashSlot].nValueOffset, buff, bufc);
                // delete from the hash table
                memcpy(&htab[nHashSlot], &htab[htab[nHashSlot].iNext],
                       sizeof(munge_htab_rec));
            }
        }
    }
    delete [] htab;
    free_lbuf(rlist);
}

FUNCTION(fun_die)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int n   = mux_atol(fargs[0]);
    int die = mux_atol(fargs[1]);

    if (  n == 0
       || die <= 0)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    if (  n < 1
       || LBUF_SIZE <= n)
    {
        safe_range(buff, bufc);
        return;
    }

    if (  3 <= nfargs
       && isTRUE(mux_atol(fargs[2])))
    {
        ITL pContext;
        ItemToList_Init(&pContext, buff, bufc);
        for (int count = 0; count < n; count++)
        {
            if (!ItemToList_AddInteger(&pContext, RandomINT32(1, die)))
            {
                break;
            }
        }
        ItemToList_Final(&pContext);
        return;
    }

    int total = 0;
    for (int count = 0; count < n; count++)
    {
        total += RandomINT32(1, die);
    }

    safe_ltoa(total, buff, bufc);
}

FUNCTION(fun_lrand)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int n_times = mux_atol(fargs[2]);
    if (n_times < 1)
    {
        return;
    }
    if (n_times > LBUF_SIZE)
    {
        n_times = LBUF_SIZE;
    }
    INT32 iLower = mux_atol(fargs[0]);
    INT32 iUpper = mux_atol(fargs[1]);

    if (iLower <= iUpper)
    {
        for (int i = 0; i < n_times-1; i++)
        {
            INT32 val = RandomINT32(iLower, iUpper);
            safe_ltoa(val, buff, bufc);
            print_sep(&sep, buff, bufc);
        }
        INT32 val = RandomINT32(iLower, iUpper);
        safe_ltoa(val, buff, bufc);
    }
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Just returns the argument, literally.
    //
    safe_str(fargs[0], buff, bufc);
}

FUNCTION(fun_dumping)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

#if !defined(HAVE_WORKING_FORK)
    safe_chr('0', buff, bufc);
#else // HAVE_WORKING_FORK
    safe_bool(mudstate.dumping, buff, bufc);
#endif // HAVE_WORKING_FORK
}

// The following table contains 64 symbols, so this supports -a-
// radix-64 encoding. It is not however 'unix-to-unix' encoding.
// All of the following characters are valid for an attribute
// name, but not for the first character of an attribute name.
//
static UTF8 aRadixTable[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@$";

FUNCTION(fun_unpack)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate radix if present.
    //
    INT64 iRadix = 64;
    if (nfargs == 2)
    {
        if (  !is_integer(fargs[1], NULL)
           || (iRadix = mux_atoi64(fargs[1])) < 2
           || 64 < iRadix)
        {
            safe_str(T("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
            return;
        }
    }

    // Build Table of valid characters.
    //
    UTF8 MatchTable[256];
    memset(MatchTable, 0, sizeof(MatchTable));
    for (int i = 0; i < iRadix; i++)
    {
        MatchTable[(unsigned char)aRadixTable[i]] = static_cast<UTF8>(i + 1);
    }

    // Validate that first argument contains only characters from the
    // subset of permitted characters.
    //
    UTF8 *pString = fargs[0];
    INT64 sum;
    int c;
    int LeadingCharacter;

    // Leading whitespace
    //
    while (mux_isspace(*pString))
    {
        pString++;
    }

    // Possible sign
    //
    LeadingCharacter = c = *pString++;
    if (c == '-' || c == '+')
    {
        c = *pString++;
    }

    sum = 0;

    // Convert symbols
    //
    for (int iValue = MatchTable[(unsigned int)c];
         iValue;
         iValue = MatchTable[(unsigned int)c])
    {
        sum = iRadix * sum + iValue - 1;
        c = *pString++;
    }

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        sum = -sum;
    }
    safe_i64toa(sum, buff, bufc);
}

static size_t mux_Pack(INT64 val, int iRadix, UTF8 *buf)
{
    UTF8 *p = buf;

    // Handle sign.
    //
    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }

    UTF8 *q = p;
    while (val > iRadix-1)
    {
        INT64 iDiv  = val / iRadix;
        INT64 iTerm = val - iDiv * iRadix;
        val = iDiv;
        *p++ = aRadixTable[iTerm];
    }
    *p++ = aRadixTable[val];

    size_t nLength = p - buf;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    return nLength;
}

FUNCTION(fun_pack)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate the arguments are numeric.
    //
    if (  !is_integer(fargs[0], NULL)
       || (nfargs == 2 && !is_integer(fargs[1], NULL)))
    {
        safe_str(T("#-1 ARGUMENTS MUST BE NUMBERS"), buff, bufc);
        return;
    }
    INT64 val = mux_atoi64(fargs[0]);

    // Validate the radix is between 2 and 64.
    //
    int iRadix = 64;
    if (nfargs == 2)
    {
        iRadix = mux_atol(fargs[1]);
        if (iRadix < 2 || 64 < iRadix)
        {
            safe_str(T("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
            return;
        }
    }

    UTF8 TempBuffer[76]; // 1 '-', 63 binary digits, 1 '\0', 11 for safety.
    size_t nLength = mux_Pack(val, iRadix, TempBuffer);
    safe_copy_buf(TempBuffer, nLength, buff, bufc);
}

FUNCTION(fun_strcat)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int i;
    for (i = 0; i < nfargs; i++)
    {
        safe_str(fargs[i], buff, bufc);
    }
}

// grep() and grepi() code borrowed from PennMUSH 1.50
//
static UTF8 *grep_util(dbref player, dbref thing, const UTF8 *pattern, const UTF8 *lookfor, size_t len, bool insensitive)
{
    // Returns a list of attributes which match <pattern> on <thing>
    // whose contents have <lookfor>.
    //
    olist_push();
    find_wild_attrs(player, thing, pattern, false, false, false);
    BMH_State bmhs;
    if (insensitive)
    {
        BMH_PrepareI(&bmhs, len, lookfor);
    }
    else
    {
        BMH_Prepare(&bmhs, len, lookfor);
    }

    UTF8 *tbuf1 = alloc_lbuf("grep_util");
    UTF8 *bp = tbuf1;

    dbref aowner;
    int aflags;
    for (int ca = olist_first(); ca != NOTHING && !MuxAlarm.bAlarmed; ca = olist_next())
    {
        size_t nText;
        UTF8 *attrib = atr_get_LEN(thing, ca, &aowner, &aflags, &nText);
        size_t i;
        bool bSucceeded;
        if (insensitive)
        {
            bSucceeded = BMH_ExecuteI(&bmhs, &i, len, lookfor, nText, attrib);
        }
        else
        {
            bSucceeded = BMH_Execute(&bmhs, &i, len, lookfor, nText, attrib);
        }
        if (bSucceeded)
        {
            if (bp != tbuf1)
            {
                safe_chr(' ', tbuf1, &bp);
            }
            ATTR *ap = atr_num(ca);
            const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
            if (ap)
            {
                pName = ap->name;
            }
            safe_str(pName, tbuf1, &bp);
        }
        free_lbuf(attrib);
    }
    *bp = '\0';
    olist_pop();
    return tbuf1;
}

static void grep_handler(UTF8 *buff, UTF8 **bufc, dbref executor, UTF8 *fargs[],
                   bool bCaseInsens)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }

    if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Make sure there's an attribute and a pattern
    //
    if (!fargs[1] || !*fargs[1])
    {
        safe_str(T("#-1 NO SUCH ATTRIBUTE"), buff, bufc);
        return;
    }
    if (!fargs[2] || !*fargs[2])
    {
        safe_str(T("#-1 INVALID GREP PATTERN"), buff, bufc);
        return;
    }
    UTF8 *tp = grep_util(executor, it, fargs[1], fargs[2], strlen((char *)fargs[2]), bCaseInsens);
    safe_str(tp, buff, bufc);
    free_lbuf(tp);
}

FUNCTION(fun_grep)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    grep_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_grepi)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    grep_handler(buff, bufc, executor, fargs, true);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamax)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *amax = fargs[0];
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp((char *)amax, (char *)fargs[i]) < 0)
        {
            amax = fargs[i];
        }
    }
    safe_str(amax, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamin)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *amin = fargs[0];
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp((char *)amin, (char *)fargs[i]) > 0)
        {
            amin = fargs[i];
        }
    }
    safe_str(amin, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_valid)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Checks to see if a given <something> is valid as a parameter of
    // a given type (such as an object name)
    //
    size_t nValidName;
    bool bValid;
    if (!*fargs[0] || !*fargs[1])
    {
        bValid = false;
    }
    else if (!mux_stricmp(fargs[0], T("attrname")))
    {
        MakeCanonicalAttributeName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("comalias")))
    {
        MakeCanonicalComAlias(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("doing")))
    {
        MakeCanonicalDoing(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("exitname")))
    {
        MakeCanonicalExitName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("malias")))
    {
        MakeCanonicalMailAlias(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("maliasdesc")))
    {
        size_t vw;
        MakeCanonicalMailAliasDesc(fargs[1], &nValidName, &bValid, &vw);
    }
    else if (  !mux_stricmp(fargs[0], T("name"))
            || !mux_stricmp(fargs[0], T("thingname")))
    {
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid, mudconf.thing_name_charset);
    }
    else if (!mux_stricmp(fargs[0], T("roomname")))
    {
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid, mudconf.room_name_charset);
    }
    else if (!mux_stricmp(fargs[0], T("password")))
    {
        const UTF8 *msg;
        bValid = ok_password(fargs[1], &msg);
    }
    else if (!mux_stricmp(fargs[0], T("playername")))
    {
        bValid = ValidatePlayerName(fargs[1]);
    }
    else
    {
        safe_nothing(buff, bufc);
        return;
    }
    safe_bool(bValid, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_hastype)
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
    bool bResult = false;
    switch (fargs[1][0])
    {
    case 'r':
    case 'R':

        bResult = isRoom(it);
        break;

    case 'e':
    case 'E':

        bResult = isExit(it);
        break;

    case 'p':
    case 'P':

        bResult = isPlayer(it);
        break;

    case 't':
    case 'T':

        bResult = isThing(it);
        break;

    default:

        safe_str(T("#-1 NO SUCH TYPE"), buff, bufc);
        break;
    }
    safe_bool(bResult, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lparent)
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
    else if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    if (!ItemToList_AddInteger(&pContext, it))
    {
        ItemToList_Final(&pContext);
        return;
    }

    dbref par = Parent(it);

    int iNestLevel = 1;
    while (  Good_obj(par)
          && Examinable(executor, it)
          && iNestLevel < mudconf.parent_nest_lim)
    {
        if (!ItemToList_AddInteger(&pContext, par))
        {
            break;
        }
        it = par;
        par = Parent(par);
        iNestLevel++;
    }
    ItemToList_Final(&pContext);
}

#ifdef DEPRECATED

// stacksize - returns how many items are stuffed onto an object stack
//
static int stacksize(dbref doer)
{
    int i;
    MUX_STACK *sp;
    for (i = 0, sp = Stack(doer); sp != NULL; sp = sp->next, i++)
    {
        // Nothing
        ;
    }
    return i;
}

FUNCTION(fun_lstack)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    MUX_STACK *sp;
    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    for (sp = Stack(doer); sp != NULL; sp = sp->next)
    {
        safe_str(sp->data, buff, bufc);
        if (sp->next != NULL)
        {
            safe_chr(' ', buff, bufc);
        }
    }
}

// stack_clr - clear the stack.
//
void stack_clr(dbref obj)
{
    // Clear the stack.
    //
    MUX_STACK *sp, *next;
    for (sp = Stack(obj); sp != NULL; sp = next)
    {
        next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
    s_Stack(obj, NULL);
}

FUNCTION(fun_empty)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    stack_clr(doer);
}

FUNCTION(fun_items)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    safe_ltoa(stacksize(doer), buff, bufc);
}

FUNCTION(fun_peek)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    MUX_STACK *sp;
    dbref doer;
    int count, pos;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }

    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str(T("#-1 POSITION TOO LARGE"), buff, bufc);
        return;
    }
    count = 0;
    sp = Stack(doer);
    while (count != pos)
    {
        if (sp == NULL)
        {
            return;
        }
        count++;
        sp = sp->next;
    }

    safe_str(sp->data, buff, bufc);
}

FUNCTION(fun_pop)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }
    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int pos;
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }
    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str(T("#-1 POSITION TOO LARGE"), buff, bufc);
        return;
    }

    MUX_STACK *sp = Stack(doer);
    MUX_STACK *prev = NULL;
    int count = 0;
    while (count != pos)
    {
        if (sp == NULL)
        {
            return;
        }
        prev = sp;
        sp = sp->next;
        count++;
    }

    safe_str(sp->data, buff, bufc);
    if (count == 0)
    {
        s_Stack(doer, sp->next);
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
    else
    {
        prev->next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
}

FUNCTION(fun_push)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;
    UTF8 *data;

    if (nfargs <= 1 || !*fargs[1])
    {
        doer = executor;
        data = fargs[0];
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
        data = fargs[1];
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (stacksize(doer) >= mudconf.stack_limit)
    {
        safe_str(T("#-1 STACK SIZE EXCEEDED"), buff, bufc);
        return;
    }
    MUX_STACK *sp = (MUX_STACK *)MEMALLOC(sizeof(MUX_STACK));
    ISOUTOFMEMORY(sp);
    sp->next = Stack(doer);
    sp->data = alloc_lbuf("push");
    mux_strncpy(sp->data, data, LBUF_SIZE-1);
    s_Stack(doer, sp);
}

#endif // DEPRECATED

/* ---------------------------------------------------------------------------
 * fun_regmatch: Return 0 or 1 depending on whether or not a regular
 * expression matches a string. If a third argument is specified, dump
 * the results of a regexp pattern match into a set of arbitrary r()-registers.
 *
 * regmatch(string, pattern, list of registers)
 * If the number of matches exceeds the registers, those bits are tossed
 * out.
 * If -1 is specified as a register number, the matching bit is tossed.
 * Therefore, if the list is "-1 0 3 5", the regexp $0 is tossed, and
 * the regexp $1, $2, and $3 become r(0), r(3), and r(5), respectively.
 */

static void real_regmatch(const UTF8 *search, const UTF8 *pattern, UTF8 *registers,
                   int nfargs, UTF8 *buff, UTF8 **bufc, bool cis)
{
    if (MuxAlarm.bAlarmed)
    {
        return;
    }

    const char *errptr;
    int erroffset;
    // To capture N substrings, you need space for 3(N+1) offsets in the
    // offset vector. We'll allow 2N-1 substrings and possibly ignore some.
    //
    const int ovecsize = 6 * MAX_GLOBAL_REGS;
    int ovec[ovecsize];

    pcre *re = pcre_compile((char *)pattern, PCRE_UTF8|(cis ? PCRE_CASELESS : 0),
        &errptr, &erroffset, NULL);
    if (!re)
    {
        // Matching error.
        //
        safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
        safe_str((UTF8 *)errptr, buff, bufc);
        return;
    }

    int matches = pcre_exec(re, NULL, (char *)search, static_cast<int>(strlen((char *)search)), 0, 0,
        ovec, ovecsize);
    if (matches == 0)
    {
        // There were too many substring matches. See docs for
        // pcre_copy_substring().
        //
        matches = ovecsize / 3;
    }
    safe_bool(matches > 0, buff, bufc);
    if (matches < 0)
    {
        matches = 0;
    }

    // If we don't have a third argument, we're done.
    //
    if (nfargs != 3)
    {
        MEMFREE(re);
        return;
    }

    // We need to parse the list of registers. If a register is
    // mentioned in the list, then either fill the register with the
    // subexpression, or if there wasn't a match, clear it.
    //
    const int NSUBEXP = 2 * MAX_GLOBAL_REGS;
    UTF8 *qregs[NSUBEXP];
    SEP sep;
    sep.n = 1;
    memcpy(sep.str, " ", 2);
    int nqregs = list2arr(qregs, NSUBEXP, registers, &sep);
    int i;
    for (i = 0; i < nqregs; i++)
    {
        int curq;
        if (  qregs[i]
           && *qregs[i]
           && (curq = mux_RegisterSet[(unsigned char)qregs[i][0]]) != -1
           && qregs[i][1] == '\0'
           && curq < MAX_GLOBAL_REGS)
        {
            UTF8 *p = alloc_lbuf("fun_regmatch");
            int len = pcre_copy_substring((char *)search, ovec, matches, i, (char *)p,
                LBUF_SIZE);
            len = (len > 0 ? len : 0);

            size_t n = len;
            RegAssign(&mudstate.global_regs[curq], n, p);
            free_lbuf(p);
        }
    }
    MEMFREE(re);
}

FUNCTION(fun_regmatch)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, false);
}

FUNCTION(fun_regmatchi)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, true);
}


/* ---------------------------------------------------------------------------
 * regrab(), regraball(). Like grab() and graball(), using a regular expression
 * instead of a wildcard pattern. The versions ending in i are case-insensitive.
 */

static void real_regrab(UTF8 *search, const UTF8 *pattern, SEP *psep, UTF8 *buff,
                 UTF8 **bufc, bool cis, bool all)
{
    if (MuxAlarm.bAlarmed)
    {
        return;
    }
    pcre *re;
    pcre_extra *study = NULL;
    const char *errptr;
    int erroffset;
    // To capture N substrings, you need space for 3(N+1) offsets in the
    // offset vector. We'll allow 2N-1 substrings and possibly ignore some.
    //
    const int ovecsize = 6 * MAX_GLOBAL_REGS;
    int ovec[ovecsize];

    re = pcre_compile((char *)pattern, PCRE_UTF8|(cis ? PCRE_CASELESS : 0),
        &errptr, &erroffset, NULL);
    if (!re)
    {
        // Matching error.
        //
        safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
        safe_str((UTF8 *)errptr, buff, bufc);
        return;
    }

    if (all)
    {
        study = pcre_study(re, 0, &errptr);
    }

    bool first = true;
    UTF8 *s = trim_space_sep(search, psep);
    do
    {
        UTF8 *r = split_token(&s, psep);
        if (  !MuxAlarm.bAlarmed
           && pcre_exec(re, study, (char *)r, static_cast<int>(strlen((char *)r)), 0, 0, ovec, ovecsize) >= 0)
        {
            if (first)
            {
                first = false;
            }
            else
            {
                print_sep(psep, buff, bufc);
            }
            safe_str(r, buff, bufc);
            if (!all)
            {
                break;
            }
        }
    } while (s);

    MEMFREE(re);
    if (study)
    {
        MEMFREE(study);
    }
}

FUNCTION(fun_regrab)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], &sep, buff, bufc, false, false);
}

FUNCTION(fun_regrabi)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], &sep, buff, bufc, true, false);
}

FUNCTION(fun_regraball)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], &sep, buff, bufc, false, true);
}

FUNCTION(fun_regraballi)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], &sep, buff, bufc, true, true);
}


/* ---------------------------------------------------------------------------
 * fun_translate: Takes a string and a second argument. If the second argument
 * is 0 or s, control characters are converted to spaces. If it's 1 or p,
 * they're converted to percent substitutions.
 */

FUNCTION(fun_translate)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int ch = fargs[1][0];
    bool type = (ch == 'p' || ch == '1');
    safe_str(translate_string(fargs[0], type), buff, bufc);
}

// Construct a CBitField to hold (nMaximum_arg+1) bits numbered 0 through
// nMaximum_arg.
//
CBitField::CBitField(unsigned int nMaximum_arg)
{
    nMaximum = 0;
    nInts    = 0;
    pInts    = NULL;
    pMasks   = NULL;

    nBitsPer = sizeof(UINT32)*8;

    // Calculate Shift
    //
    nShift = 0;
    unsigned int i = 1;
    while (i < nBitsPer)
    {
        nShift++;
        i <<= 1;
    }

    // Calculate Mask
    //
    nMask = nBitsPer - 1;

    // Allocate array of UINT32s.
    //
    Resize(nMaximum_arg);
}

#define MINIMUM_RESIZE (4096*sizeof(UINT32))

void CBitField::Resize(unsigned int nMaximum_arg)
{
    if (  0 < nMaximum_arg
       && nMaximum < nMaximum_arg)
    {
        unsigned int nNewMaximum = nMaximum_arg;

        // This provides some assurances that we are not resizing too often.
        //
        if (  pMasks
           && nNewMaximum < nMaximum + MINIMUM_RESIZE)
        {
            nNewMaximum = nMaximum + MINIMUM_RESIZE;
        }

        size_t  nNewInts = (nNewMaximum+nBitsPer) >> nShift;
        UINT32 *pNewMasks = (UINT32 *)MEMALLOC((nNewInts+nBitsPer)
                          * sizeof(UINT32));
        ISOUTOFMEMORY(pNewMasks);
        UINT32 *pNewInts = pNewMasks + nBitsPer;

        // Is this the first sizing or a re-sizing?
        //
        if (pMasks)
        {
            // Copy existing masks and bits to the new location, and
            // clear the new bits.
            //
            memcpy(pNewMasks, pMasks, (nInts+nBitsPer)*sizeof(UINT32));
            memset(pNewInts + nInts, 0, (nNewInts - nInts)*sizeof(UINT32));

            // Free the previous allocation.
            //
            MEMFREE(pMasks);

            // A reallocation.
            //
            nMaximum = nNewMaximum;
            nInts    = nNewInts;
            pMasks   = pNewMasks;
            pInts    = pNewInts;
        }
        else
        {
            // First allocation.
            //
            nMaximum = nNewMaximum;
            nInts    = nNewInts;
            pMasks   = pNewMasks;
            pInts    = pNewInts;

            // Initialize masks by calculating all possible single bits.
            //
            for (unsigned int i = 0; i < nBitsPer; i++)
            {
                pMasks[i] = ((UINT32)1) << i;
            }

            // Initialize bits by clearing them all.
            //
            ClearAll();
        }
    }
}

CBitField::~CBitField(void)
{
    pInts  = NULL;
    if (pMasks)
    {
        MEMFREE(pMasks);
        pMasks = NULL;
    }
}

void CBitField::ClearAll(void)
{
    memset(pInts, 0, nInts*sizeof(UINT32));
}

void CBitField::Set(unsigned int i)
{
    if (i <= nMaximum)
    {
        pInts[i>>nShift] |= pMasks[i&nMask];
    }
}

void CBitField::Clear(unsigned int i)
{
    if (i <= nMaximum)
    {
        pInts[i>>nShift] &= ~pMasks[i&nMask];
    }
}

bool CBitField::IsSet(unsigned int i)
{
    if (i <= nMaximum)
    {
        if (pInts[i>>nShift] & pMasks[i&nMask])
        {
            return true;
        }
    }
    return false;
}


// -------------------------------------------------------------------------
// fun_lrooms:  Takes a dbref (room), an int (N), and an optional bool (B).
//
// MUX Syntax:  lrooms(<room> [,<N>[,<B>]])
//
// Returns a list of rooms <N>-levels deep from <room>. If <B> == 1, it will
//   return all room dbrefs between 0 and <N> levels, while <B> == 0 will
//   return only the room dbrefs on the Nth level. The default is to show all
//   rooms dbrefs between 0 and <N> levels.
//
// Written by Marlek.  Idea from RhostMUSH.
//
static void room_list
(
    dbref player,
    dbref enactor,
    dbref room,
    int   level,
    int   maxlevels,
    bool  showall
)
{
    // Make sure the player can really see this room from their location.
    //
    if (  (  level == maxlevels
          || showall)
       && (  Examinable(player, room)
          || Location(player) == room
          || room == enactor))
    {
        mudstate.bfReport.Set(room);
    }

    // If the Nth level has been reach, stop this branch in the recursion
    //
    if (level >= maxlevels)
    {
        return;
    }

    // Return info for all parent levels.
    //
    int lev;
    dbref parent;
    ITER_PARENTS(room, parent, lev)
    {
        // Look for exits at each level.
        //
        if (!Has_exits(parent))
        {
            continue;
        }
        int key = 0;
        if (Examinable(player, parent))
        {
            key |= VE_LOC_XAM;
        }
        if (Dark(parent))
        {
            key |= VE_LOC_DARK;
        }
        if (Dark(room))
        {
            key |= VE_BASE_DARK;
        }

        dbref thing;
        DOLIST(thing, Exits(parent))
        {
            dbref loc = Location(thing);
            if (  exit_visible(thing, player, key)
               && !mudstate.bfTraverse.IsSet(loc))
            {
                mudstate.bfTraverse.Set(loc);
                room_list(player, enactor, loc, (level + 1), maxlevels, showall);
            }
        }
    }
}

FUNCTION(fun_lrooms)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref room = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(room))
    {
        safe_match_result(room, buff, bufc);
        return;
    }
    else if (!isRoom(room))
    {
        safe_str(T("#-1 FIRST ARGUMENT MUST BE A ROOM"), buff, bufc);
        return;
    }

    int N = 1;
    if (nfargs >= 2)
    {
        N = mux_atol(fargs[1]);
        if (N < 0)
        {
            safe_str(T("#-1 SECOND ARGUMENT MUST BE A POSITIVE NUMBER"),
                buff, bufc);
            return;
        }
        else if (N > 50)
        {
            // Maybe this can be turned into a config parameter to prevent
            // misuse by putting in really large values.
            //
            safe_str(T("#-1 SECOND ARGUMENT IS TOO LARGE"), buff, bufc);
            return;
        }
    }

    bool B = true;
    if (nfargs == 3)
    {
        B = xlate(fargs[2]);
    }

    mudstate.bfReport.Resize(mudstate.db_top-1);
    mudstate.bfTraverse.Resize(mudstate.db_top-1);
    mudstate.bfReport.ClearAll();
    mudstate.bfTraverse.ClearAll();

    mudstate.bfTraverse.Set(room);
    room_list(executor, enactor, room, 0, N, B);
    mudstate.bfReport.Clear(room);

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (  mudstate.bfReport.IsSet(i)
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}
