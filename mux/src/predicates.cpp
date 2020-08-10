/*! \file predicates.cpp
 * \brief Miscellaneous commands and functions.
 *
 * In theory, most of these functions could plausibly be called
 * "predicates", either because they determine some boolean property
 * of the input, or because they perform some action that makes them
 * verb-like.  In practice, this is a miscellany.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <csignal>

#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "mathutil.h"
#include "powers.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

UTF8 *DCL_CDECL tprintf(__in_z const UTF8 *fmt,...)
{
    static UTF8 buff[LBUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);
    return buff;
}

void DCL_CDECL safe_tprintf_str(UTF8 *str, UTF8 **bp, __in_z const UTF8 *fmt,...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t nAvailable = LBUF_SIZE - (*bp - str);
    size_t len = mux_vsnprintf(*bp, (int)nAvailable, fmt, ap);
    va_end(ap);
    *bp += len;
}

/* ---------------------------------------------------------------------------
 * insert_first, remove_first: Insert or remove objects from lists.
 */

dbref insert_first(dbref head, dbref thing)
{
    s_Next(thing, head);
    return thing;
}

dbref remove_first(dbref head, dbref thing)
{
    if (head == thing)
    {
        return Next(thing);
    }

    dbref prev;

    DOLIST(prev, head)
    {
        if (Next(prev) == thing)
        {
            s_Next(prev, Next(thing));
            return head;
        }
    }
    return head;
}

/* ---------------------------------------------------------------------------
 * reverse_list: Reverse the order of members in a list.
 */

dbref reverse_list(dbref list)
{
    dbref newlist, rest;

    newlist = NOTHING;
    while (list != NOTHING)
    {
        rest = Next(list);
        s_Next(list, newlist);
        newlist = list;
        list = rest;
    }
    return newlist;
}

/* ---------------------------------------------------------------------------
 * member - indicate if thing is in list
 */

bool member(dbref thing, dbref list)
{
    DOLIST(list, list)
    {
        if (list == thing)
        {
            return true;
        }
    }
    return false;
}

bool could_doit(dbref player, dbref thing, int locknum)
{
    if (thing == HOME)
    {
        return true;
    }

    // If nonplayer tries to get key, then no.
    //
    if (  !isPlayer(player)
       && Key(thing))
    {
        return false;
    }
    if (Pass_Locks(player))
    {
        return true;
    }

    dbref aowner;
    int   aflags;
    UTF8 *key = atr_get("could_doit.134", thing, locknum, &aowner, &aflags);
    bool doit = eval_boolexp_atr(player, thing, thing, key);
    free_lbuf(key);
    return doit;
}

bool can_see(dbref player, dbref thing, bool can_see_loc)
{
    // Don't show if all the following apply: Sleeping players should not be
    // seen.  The thing is a disconnected player.  The player is not a
    // puppet.
    //
    if (  mudconf.dark_sleepers
       && isPlayer(thing)
       && !Connected(thing)
       && !Puppet(thing))
    {
        return false;
    }

    // You don't see yourself or exits.
    //
    if (  player == thing
       || isExit(thing))
    {
        return false;
    }

    // To be visible, light must come from either the location (can_see_loc)
    // or the object itself (Light(thing)).  This light is then blocked
    // by the object itself being dark (it blocked its own light), by not
    // passing the visibility lock, or by being in a different reality.
    //
    // The exception to the above is mudconf.see_own_dark which allows a
    // myopic self-examination.
    //
    return (  (  (  can_see_loc
                 || Light(thing))
              && !Dark(thing)
#ifdef REALITY_LVLS
              && IsReal(player, thing)
#endif // REALITY_LVLS
              && could_doit(player, thing, A_LVISIBLE))
           || (  mudconf.see_own_dark
              && MyopicExam(player, thing)));
}

static bool pay_quota(dbref who, int cost)
{
    // If no cost, succeed
    //
    if (cost <= 0)
    {
        return true;
    }

    // determine quota
    //
    dbref aowner;
    int aflags;
    UTF8 *quota_str = atr_get("pay_quota.200", Owner(who), A_RQUOTA, &aowner, &aflags);
    int quota = mux_atol(quota_str);
    free_lbuf(quota_str);

    // enough to build?  Wizards always have enough.
    //
    quota -= cost;
    if (  quota < 0
       && !Free_Quota(who)
       && !Free_Quota(Owner(who)))
    {
        return false;
    }

    // Dock the quota.
    //
    UTF8 buf[I32BUF_SIZE];
    mux_ltoa(quota, buf);
    atr_add_raw(Owner(who), A_RQUOTA, buf);

    return true;
}

bool canpayfees(dbref player, dbref who, int pennies, int quota)
{
    if (  !Wizard(who)
       && !Wizard(Owner(who))
       && !Free_Money(who)
       && !Free_Money(Owner(who))
       && (Pennies(Owner(who)) < pennies))
    {
        if (player == who)
        {
            notify(player, tprintf(T("Sorry, you don\xE2\x80\x99t have enough %s."),
                       mudconf.many_coins));
        }
        else
        {
            notify(player, tprintf(T("Sorry, that player doesn\xE2\x80\x99t have enough %s."),
                mudconf.many_coins));
        }
        return false;
    }
    if (mudconf.quotas)
    {
        if (!pay_quota(who, quota))
        {
            if (player == who)
            {
                notify(player, T("Sorry, your building contract has run out."));
            }
            else
            {
                notify(player,
                    T("Sorry, that player\xE2\x80\x99s building contract has run out."));
            }
            return false;
        }
    }
    payfor(who, pennies);
    return true;
}

bool payfor(dbref who, int cost)
{
    if (  Wizard(who)
       || Wizard(Owner(who))
       || Free_Money(who)
       || Free_Money(Owner(who)))
    {
        return true;
    }
    who = Owner(who);
    int tmp;
    if ((tmp = Pennies(who)) >= cost)
    {
        s_Pennies(who, tmp - cost);
        return true;
    }
    return false;
}

void add_quota(dbref who, int payment)
{
    dbref aowner;
    int aflags;
    UTF8 buf[I32BUF_SIZE];

    UTF8 *quota = atr_get("add_quota.288", who, A_RQUOTA, &aowner, &aflags);
    mux_ltoa(mux_atol(quota) + payment, buf);
    free_lbuf(quota);
    atr_add_raw(who, A_RQUOTA, buf);
}

void giveto(dbref who, int pennies)
{
    if (  Wizard(who)
       || Wizard(Owner(who))
       || Free_Money(who)
       || Free_Money(Owner(who)))
    {
        return;
    }
    who = Owner(who);
    s_Pennies(who, Pennies(who) + pennies);
}

// Every character in the name must be allowed by one of the character sets mentioned.
// If no character sets are mentions, everything is allowed.
//
bool IsRestricted(const UTF8 *pName, int charset)
{
    if (0 == charset)
    {
        return false;
    }

    while ('\0' != pName[0])
    {
        bool bAllowed = false;
        if (  (ALLOW_CHARSET_ASCII & charset)
           && (0x80 & pName[0]) == 0)
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_8859_1 & charset)
                && mux_is8859_1(pName))
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_8859_2 & charset)
                && mux_is8859_2(pName))
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_HANGUL & charset)
                && mux_ishangul(pName))
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_HIRAGANA & charset)
                && mux_ishiragana(pName))
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_KANJI & charset)
                && mux_iskanji(pName))
        {
            bAllowed = true;
        }
        else if (  (ALLOW_CHARSET_KATAKANA & charset)
                && mux_iskatakana(pName))
        {
            bAllowed = true;
        }

        if (!bAllowed)
        {
            return true;
        }
        pName = utf8_NextCodePoint(pName);
    }
    return false;
}

// The following function validates that the object names (which will be
// used for things and rooms, but not for players or exits) and generates
// a canonical form of that name (with optimized ANSI).
//
UTF8 *MakeCanonicalObjectName(const UTF8 *pName, size_t *pnName, bool *pbValid, int charset)
{
    static UTF8 Buf[MBUF_SIZE];

    *pnName = 0;
    *pbValid = false;

    if (!pName)
    {
        return nullptr;
    }

    // Build up what the real name would be. If we pass all the
    // checks, this is what we will return as a result.
    //
    mux_field fldLen = StripTabsAndTruncate(pName, Buf, MBUF_SIZE-1, MBUF_SIZE-1);

    // Disallow pure ANSI names. There must be at least -something-
    // visible.
    //
    if (0 == fldLen.m_column)
    {
        return nullptr;
    }

    // Get the stripped version (Visible parts without color info).
    //
    size_t nStripped;
    const UTF8 *pStripped = strip_color(Buf, &nStripped);

    // Do not allow LOOKUP_TOKEN, NUMBER_TOKEN, NOT_TOKEN, or SPACE
    // as the first character, or SPACE as the last character
    //
    if (  (UTF8 *)strchr((char *)"*!#", pStripped[0])
       || mux_isspace(pStripped[0])
       || mux_isspace(pStripped[nStripped-1]))
    {
        return nullptr;
    }

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    const UTF8 *p = pStripped;
    while ('\0' != *p)
    {
        if (!mux_isobjectname(p))
        {
            return nullptr;
        }
        p = utf8_NextCodePoint(p);
    }

    // Special names are specifically dis-allowed.
    //
    if (  (nStripped == 2 && memcmp("me", pStripped, 2) == 0)
       || (nStripped == 4 && (  memcmp("home", pStripped, 4) == 0
                             || memcmp("here", pStripped, 4) == 0)))
    {
        return nullptr;
    }

    if (IsRestricted(pStripped, charset))
    {
        return nullptr;
    }

    *pnName = fldLen.m_byte;
    *pbValid = true;
    return Buf;
}

// The following function validates exit names.
//
UTF8 *MakeCanonicalExitName(const UTF8 *pName, size_t *pnName, bool *pbValid)
{
    static UTF8 Buf[MBUF_SIZE];

    *pnName = 0;
    *pbValid = false;

    if (!pName)
    {
        return nullptr;
    }

    mux_strncpy(Buf, pName, mux_strlen(pName));

    // Sanitize the input before processing.
    //
    MUX_STRTOK_STATE tts;
    mux_strtok_src(&tts, Buf);
    mux_strtok_ctl(&tts, T(";"));

    // Break the exitname down into semi-colon-separated segments.  The first
    // segment can contain color as it is used for showing the exit, but the
    // remaining segments are stripped of color.  A valid exitname requires
    // at least one (display) segment.
    //
    UTF8 *ptr;
    mux_string clean_names;
    bool bHaveDisplay = false;
    for (ptr = mux_strtok_parse(&tts); ptr; ptr = mux_strtok_parse(&tts))
    {
        UTF8 *pTrimmedSegment = nullptr;
        if (bHaveDisplay)
        {
            // No color allowed in segments after the first one.
            //
            UTF8 *pNoColor = strip_color(ptr);
            pTrimmedSegment = trim_spaces(pNoColor);
        }
        else
        {
            // Color allowed in first segment.
            //
            pTrimmedSegment = trim_spaces(ptr);
        }

        // Ignore segments which contained nothing but spaces.
        //
        if ('\0' != pTrimmedSegment[0])
        {
            bool valid = false;
            size_t len = 0;

            UTF8 *pValidSegment = MakeCanonicalObjectName(pTrimmedSegment, &len, &valid, mudconf.exit_name_charset);
            if (valid)
            {
                if (bHaveDisplay)
                {
                    clean_names.append(T(";"));
                    clean_names.append(mux_string(pValidSegment));
                }
                else
                {
                    clean_names.prepend(pValidSegment);
                    bHaveDisplay = true;
                }
            }
        }
        free_lbuf(pTrimmedSegment);
        pTrimmedSegment = nullptr;
    }

    *pbValid = bHaveDisplay;
    if (!bHaveDisplay)
    {
        *pnName = 0;
        return Buf;
    }

    clean_names.export_TextColor(Buf);
    *pnName = mux_strlen(Buf);

    return Buf;
}

// The following function validates the player name. ANSI is not
// allowed in player names. However, a player name must satisfy
// the requirements of a regular name as well.
//
bool ValidatePlayerName(const UTF8 *pName)
{
    if (!pName)
    {
        return false;
    }
    size_t nName = strlen((char *)pName);

    // Verify that name is not empty, but not too long, either.
    //
    if (  nName <= 0
       || PLAYER_NAME_LIMIT <= nName)
    {
        return false;
    }

    // Do not allow LOOKUP_TOKEN, NUMBER_TOKEN, NOT_TOKEN, or SPACE
    // as the first character, or SPACE as the last character
    //
    if (  (UTF8 *)strchr((char *)"*!#", pName[0])
       || mux_isspace(pName[0])
       || mux_isspace(pName[nName-1]))
    {
        return false;
    }

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    if (  mudstate.bStandAlone
       || mudconf.name_spaces)
    {
        const UTF8 *p = pName;
        while ('\0' != *p)
        {
            if (  !mux_isplayername(p)
               && ' ' != *p)
            {
                return false;
            }
            p = utf8_NextCodePoint(p);
        }
    }
    else
    {
        const UTF8 *p = pName;
        while ('\0' != *p)
        {
            if (!mux_isplayername(p))
            {
                return false;
            }
            p = utf8_NextCodePoint(p);
        }
    }

    // Special names are specifically dis-allowed.
    //
    if (  (nName == 2 && memcmp("me", pName, 2) == 0)
       || (nName == 4 && (  memcmp("home", pName, 4) == 0
                         || memcmp("here", pName, 4) == 0)))
    {
        return false;
    }

    if (IsRestricted(pName, mudconf.player_name_charset))
    {
        return false;
    }
    return true;
}

bool ok_password(const UTF8 *password, const UTF8 **pmsg)
{
    *pmsg = nullptr;

    if (*password == '\0')
    {
        *pmsg = T("Null passwords are not allowed.");
        return false;
    }

    int num_upper = 0;
    int num_special = 0;
    int num_lower = 0;

    const UTF8 *scan = password;
    for ( ; *scan; scan = utf8_NextCodePoint(scan))
    {
        if (  !mux_isprint(scan)
           || mux_isspace(*scan))
        {
            *pmsg = T("Illegal character in password.");
            return false;
        }
        if (mux_isupper_ascii(*scan))
        {
            num_upper++;
        }
        else if (mux_islower_ascii(*scan))
        {
            num_lower++;
        }
        else if (  *scan != '\''
                && *scan != '-')
        {
            num_special++;
        }
    }

    if (  !mudstate.bStandAlone
       && mudconf.safer_passwords)
    {
        if (num_upper < 1)
        {
            *pmsg = T("The password must contain at least one capital letter.");
            return false;
        }
        if (num_lower < 1)
        {
            *pmsg = T("The password must contain at least one lowercase letter.");
            return false;
        }
        if (num_special < 1)
        {
            *pmsg = T("The password must contain at least one number or a symbol other than the apostrophe or dash.");
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * handle_ears: Generate the 'grows ears' and 'loses ears' messages.
 */

void handle_ears(dbref thing, bool could_hear, bool can_hear)
{
    static const UTF8 *poss[5] =
    {
        T(""),
        T("its"),
        T("her"),
        T("his"),
        T("their")
    };

    if (could_hear != can_hear)
    {
        mux_string *sStr = new mux_string(Moniker(thing));
        if (isExit(thing))
        {
            mux_cursor iPos;
            if (sStr->search(T(";"), &iPos))
            {
                sStr->truncate(iPos);
            }
        }
        int gender = get_gender(thing);

        if (can_hear)
        {
            sStr->append_TextPlain(tprintf(T(" grow%s ears and can now hear."),
                                 (gender == 4) ? "" : "s"));
        }
        else
        {
            sStr->append_TextPlain(tprintf(T(" lose%s %s ears and become%s deaf."),
                                 (gender == 4) ? "" : "s", poss[gender],
                                 (gender == 4) ? "" : "s"));
        }
        notify_check(thing, thing, *sStr, MSG_ME | MSG_NBR | MSG_LOC | MSG_INV);
        delete sStr;
    }
}

// For lack of better place the @switch code is here.
//
void do_switch
(
    dbref executor, dbref caller, dbref enactor,
    int eval, int key,
    UTF8 *expr,
    UTF8 *args[], int nargs,
    const UTF8 *cargs[], int ncargs
)
{
    if (  !expr
       || nargs <= 0)
    {
        return;
    }

    bool bMatchOne;
    switch (key & SWITCH_MASK)
    {
    case SWITCH_DEFAULT:
        if (mudconf.switch_df_all)
        {
            bMatchOne = false;
        }
        else
        {
            bMatchOne = true;
        }
        break;

    case SWITCH_ANY:
        bMatchOne = false;
        break;

    case SWITCH_ONE:
    default:
        bMatchOne = true;
        break;
    }

    // Now try a wild card match of buff with stuff in coms.
    //
    bool bAny = false;
    int a;
    UTF8 *buff, *bp;
    buff = bp = alloc_lbuf("do_switch");
    CLinearTimeAbsolute lta;
    for (  a = 0;
              (  !bMatchOne
              || !bAny)
           && a < nargs - 1
           && args[a]
           && args[a + 1];
           a += 2)
    {
        bp = buff;
        mux_exec(args[a], LBUF_SIZE-1, buff, &bp, executor, caller, enactor, eval|EV_FCHECK|EV_EVAL|EV_TOP,
            cargs, ncargs);
        *bp = '\0';
        if (wild_match(buff, expr))
        {
            UTF8 *tbuf = replace_tokens(args[a+1], nullptr, nullptr, expr);
            wait_que(executor, caller, enactor, eval, false, lta, NOTHING, 0,
                tbuf,
                ncargs, cargs,
                mudstate.global_regs);
            free_lbuf(tbuf);
            bAny = true;
        }
    }

    free_lbuf(buff);
    if (  a < nargs
       && !bAny
       && args[a])
    {
        UTF8 *tbuf = replace_tokens(args[a], nullptr, nullptr, expr);
        wait_que(executor, caller, enactor, eval, false, lta, NOTHING, 0,
            tbuf,
            ncargs, cargs,
            mudstate.global_regs);
        free_lbuf(tbuf);
    }

    if (key & SWITCH_NOTIFY)
    {
        UTF8 *tbuf = alloc_lbuf("switch.notify_cmd");
        mux_strncpy(tbuf, T("@notify/quiet me"), LBUF_SIZE-1);
        wait_que(executor, caller, enactor, eval, false, lta, NOTHING, A_SEMAPHORE,
            tbuf,
            ncargs, cargs,
            mudstate.global_regs);
        free_lbuf(tbuf);
    }
}

// Also for lack of better place the @ifelse code is here.
// Idea for @ifelse from ChaoticMUX.
//
void do_if
(
    dbref player, dbref caller, dbref enactor,
    int eval, int key,
    UTF8 *expr,
    UTF8 *args[], int nargs,
    const UTF8 *cargs[], int ncargs
)
{
    UNUSED_PARAMETER(key);

    if (  !expr
       || nargs <= 0)
    {
        return;
    }

    UTF8 *buff, *bp;
    CLinearTimeAbsolute lta;
    buff = bp = alloc_lbuf("do_if");

    mux_exec(expr, LBUF_SIZE-1, buff, &bp, player, caller, enactor, eval|EV_FCHECK|EV_EVAL|EV_TOP,
        cargs, ncargs);
    *bp = '\0';

    int a = !xlate(buff);
    free_lbuf(buff);

    if (a < nargs)
    {
        wait_que(player, caller, enactor, eval, false, lta, NOTHING, 0,
            args[a],
            ncargs, cargs,
            mudstate.global_regs);
    }
}

void do_addcommand
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *name,
    UTF8 *command,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate command name.
    //
    static UTF8 pName[LBUF_SIZE];
    if (1 <= nargs)
    {
        mux_string *sName = new mux_string(name);
        sName->strip(T("\r\n\t "));
        sName->LowerCase();
        sName->export_TextPlain(pName);
        delete sName;
    }
    if (  0 == nargs
       || '\0' == pName[0]
       || (  pName[0] == '_'
          && pName[1] == '_'))
    {
        notify(player, T("That is not a valid command name."));
        return;
    }

    // Validate object/attribute.
    //
    dbref thing;
    ATTR *pattr;
    if (  !parse_attrib(player, command, &thing, &pattr)
       || !pattr)
    {
        notify(player, T("No such attribute."));
        return;
    }
    if (!See_attr(player, thing, pattr))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }

    CMDENT *old = (CMDENT *)hashfindLEN(pName, strlen((char *)pName),
        &mudstate.command_htab);

    CMDENT *cmd;
    ADDENT *add, *nextp;

    if (  old
       && (old->callseq & CS_ADDED))
    {
        // Don't allow the same (thing,atr) in the list.
        //
        for (nextp = old->addent; nextp != nullptr; nextp = nextp->next)
        {
            if (  nextp->thing == thing
               && nextp->atr == pattr->number)
            {
                notify(player, tprintf(T("%s already added."), pName));
                return;
            }
        }

        // Otherwise, add another (thing,atr) to the list.
        //
        add = (ADDENT *)MEMALLOC(sizeof(ADDENT));
        ISOUTOFMEMORY(add);
        add->thing = thing;
        add->atr = pattr->number;
        add->name = StringClone(pName);
        add->next = old->addent;
        old->addent = add;
    }
    else
    {
        if (old)
        {
            // Delete the old built-in (which will later be added back as
            // __name).
            //
            hashdeleteLEN(pName, strlen((char *)pName), &mudstate.command_htab);
        }

        cmd = nullptr;
        try
        {
            cmd = new CMDENT;
        }
        catch (...)
        {
            ; // Nothing.
        }
        ISOUTOFMEMORY(cmd);
        cmd->cmdname = StringClone(pName);
        cmd->switches = nullptr;
        cmd->perms = 0;
        cmd->extra = 0;
        if (  old
           && (old->callseq & CS_LEADIN))
        {
            cmd->callseq = CS_ADDED|CS_ONE_ARG|CS_LEADIN;
        }
        else
        {
            cmd->callseq = CS_ADDED|CS_ONE_ARG;
        }
        cmd->flags = CEF_ALLOC;
        add = (ADDENT *)MEMALLOC(sizeof(ADDENT));
        ISOUTOFMEMORY(add);
        add->thing = thing;
        add->atr = pattr->number;
        add->name = StringClone(pName);
        add->next = nullptr;
        cmd->addent = add;

        hashaddLEN(pName, strlen((char *)pName), cmd, &mudstate.command_htab);

        if (  old
           && strcmp((char *)pName, (char *)old->cmdname) == 0)
        {
            // We are @addcommand'ing over a built-in command by its
            // unaliased name, therefore, we want to re-target all the
            // aliases.
            //
            UTF8 *p = tprintf(T("__%s"), pName);
            hashdeleteLEN(p, strlen((char *)p), &mudstate.command_htab);
            hashreplall(old, cmd, &mudstate.command_htab);
            hashaddLEN(p, strlen((char *)p), old, &mudstate.command_htab);
        }
    }

    // We reset the one letter commands here so you can overload them.
    //
    cache_prefix_cmds();
    notify(player, tprintf(T("Command %s added."), pName));
}

void do_listcommands(dbref player, dbref caller, dbref enactor, int eval,
                     int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CMDENT *old;
    ADDENT *nextp;
    bool didit = false;

    // Let's make this case insensitive...
    //
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    if (*pCased)
    {
        old = (CMDENT *)hashfindLEN(pCased, nCased, &mudstate.command_htab);

        if (  old
           && (old->callseq & CS_ADDED))
        {
            // If it's already found in the hash table, and it's being added
            // using the same object and attribute...
            //
            for (nextp = old->addent; nextp != nullptr; nextp = nextp->next)
            {
                ATTR *ap = (ATTR *)atr_num(nextp->atr);
                const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
                if (ap)
                {
                    pName = ap->name;
                }
                notify(player, tprintf(T("%s: #%d/%s"), nextp->name, nextp->thing, pName));
            }
        }
        else
        {
            notify(player, tprintf(T("%s not found in command table."), pCased));
        }
        return;
    }
    else
    {
        UTF8 *pKeyName;
        int  nKeyName;
        for (old = (CMDENT *)hash_firstkey(&mudstate.command_htab, &nKeyName, &pKeyName);
             old != nullptr;
             old = (CMDENT *)hash_nextkey(&mudstate.command_htab, &nKeyName, &pKeyName))
        {
            if (old->callseq & CS_ADDED)
            {
                pKeyName[nKeyName] = '\0';
                for (nextp = old->addent; nextp != nullptr; nextp = nextp->next)
                {
                    if (strcmp((char *)pKeyName, (char *)nextp->name) != 0)
                    {
                        continue;
                    }
                    ATTR *ap = (ATTR *)atr_num(nextp->atr);
                    const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
                    if (ap)
                    {
                        pName = ap->name;
                    }
                    notify(player, tprintf(T("%s: #%d/%s"), nextp->name,
                        nextp->thing, pName));
                    didit = true;
                }
            }
        }
    }

    if (!didit)
    {
        notify(player, T("No added commands found in command table."));
    }
}

void do_delcommand
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *name,
    UTF8 *command,
    const UTF8 *cargs[],
    int  ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!*name)
    {
        notify(player, T("Sorry."));
        return;
    }

    dbref thing = NOTHING;
    int atr = NOTHING;
    ATTR *pattr;
    if (*command)
    {
        if (  !parse_attrib(player, command, &thing, &pattr)
           || !pattr)
        {
            notify(player, T("No such attribute."));
            return;
        }
        if (!See_attr(player, thing, pattr))
        {
            notify(player, NOPERM_MESSAGE);
            return;
        }
        atr = pattr->number;
    }

    // Let's make this case insensitive...
    //
    size_t nCased;
    UTF8  *pCased = mux_strlwr(name, nCased);

    CMDENT *old, *cmd;
    ADDENT *prev = nullptr, *nextp;
    old = (CMDENT *)hashfindLEN(pCased, nCased, &mudstate.command_htab);

    if (  old
       && (old->callseq & CS_ADDED))
    {
        UTF8 *p__Name = tprintf(T("__%s"), pCased);
        size_t n__Name = strlen((char *)p__Name);

        if (command[0] == '\0')
        {
            // Delete all @addcommand'ed associations with the given name.
            //
            for (prev = old->addent; prev != nullptr; prev = nextp)
            {
                nextp = prev->next;
                MEMFREE(prev->name);
                prev->name = nullptr;
                MEMFREE(prev);
                prev = nullptr;
            }
            hashdeleteLEN(pCased, nCased, &mudstate.command_htab);
            cmd = (CMDENT *)hashfindLEN(p__Name, n__Name, &mudstate.command_htab);
            if (cmd)
            {
                hashaddLEN(cmd->cmdname, strlen((char *)cmd->cmdname), cmd,
                    &mudstate.command_htab);
                if (strcmp((char *)pCased, (char *)cmd->cmdname) != 0)
                {
                    hashaddLEN(pCased, nCased, cmd, &mudstate.command_htab);
                }

                hashdeleteLEN(p__Name, n__Name, &mudstate.command_htab);
                hashaddLEN(p__Name, n__Name, cmd, &mudstate.command_htab);
                hashreplall(old, cmd, &mudstate.command_htab);
            }
            else
            {
                // TODO: Delete everything related to 'old'.
                //
            }
            MEMFREE(old->cmdname);
            old->cmdname = nullptr;
            MEMFREE(old);
            old = nullptr;
            cache_prefix_cmds();
            notify(player, T("Done."));
        }
        else
        {
            // Remove only the (name,thing,atr) association.
            //
            for (nextp = old->addent; nextp != nullptr; nextp = nextp->next)
            {
                if (  nextp->thing == thing
                   && nextp->atr == atr)
                {
                    MEMFREE(nextp->name);
                    nextp->name = nullptr;
                    if (!prev)
                    {
                        if (!nextp->next)
                        {
                            hashdeleteLEN(pCased, nCased, &mudstate.command_htab);
                            cmd = (CMDENT *)hashfindLEN(p__Name, n__Name,
                                &mudstate.command_htab);
                            if (cmd)
                            {
                                hashaddLEN(cmd->cmdname, strlen((char *)cmd->cmdname),
                                    cmd, &mudstate.command_htab);
                                if (strcmp((char *)pCased, (char *)cmd->cmdname) != 0)
                                {
                                    hashaddLEN(pCased, nCased, cmd,
                                        &mudstate.command_htab);
                                }

                                hashdeleteLEN(p__Name, n__Name,
                                    &mudstate.command_htab);
                                hashaddLEN(p__Name, n__Name, cmd,
                                    &mudstate.command_htab);
                                hashreplall(old, cmd,
                                    &mudstate.command_htab);
                            }
                            MEMFREE(old->cmdname);
                            old->cmdname = nullptr;
                            MEMFREE(old);
                            old = nullptr;
                        }
                        else
                        {
                            old->addent = nextp->next;
                            MEMFREE(nextp);
                            nextp = nullptr;
                        }
                    }
                    else
                    {
                        prev->next = nextp->next;
                        MEMFREE(nextp);
                        nextp = nullptr;
                    }
                    cache_prefix_cmds();
                    notify(player, T("Done."));
                    return;
                }
                prev = nextp;
            }
            notify(player, T("Command not found in command table."));
        }
    }
    else
    {
        notify(player, T("Command not found in command table."));
    }
}

/*
 * @prog 'glues' a user's input to a command. Once executed, the first string
 * input from any of the doers's logged in descriptors, will go into
 * A_PROGMSG, which can be substituted in <command> with %0. Commands already
 * queued by the doer will be processed normally.
 */

void handle_prog(DESC *d, UTF8 *message)
{
    // Allow the player to pipe a command while in interactive mode.
    //
    if (*message == '|')
    {
        do_command(d, message + 1);

        if (d->program_data != nullptr)
        {
            queue_string(d, tprintf(T("%s>%s "), COLOR_INTENSE, COLOR_RESET));

            if (OPTION_YES == us_state(d, TELNET_EOR))
            {
                // Use telnet protocol's EOR command to show prompt.
                //
                const UTF8 aEOR[2] = { NVT_IAC, NVT_EOR };
                queue_write_LEN(d, aEOR, sizeof(aEOR));
            }
            else if (OPTION_YES != us_state(d, TELNET_SGA))
            {
                // Use telnet protocol's GOAHEAD command to show prompt.
                //
                const UTF8 aGoAhead[2] = { NVT_IAC, NVT_GA };
                queue_write_LEN(d, aGoAhead, sizeof(aGoAhead));
            }
        }
        return;
    }
    dbref aowner;
    int aflags, i;
    UTF8 *cmd = atr_get("handle_prog.1215", d->player, A_PROGCMD, &aowner, &aflags);
    CLinearTimeAbsolute lta;
    wait_que(d->program_data->wait_enactor, d->player, d->player,
        AttrTrace(aflags, 0), false, lta, NOTHING, 0,
        cmd,
        1, (const UTF8 **)&message,
        d->program_data->wait_regs);

    // First, set 'all' to a descriptor we find for this player.
    //
    DESC *all = (DESC *)hashfindLEN(&(d->player), sizeof(d->player), &mudstate.desc_htab) ;

    if (  all
       && all->program_data)
    {
        PROG *program = all->program_data;
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (program->wait_regs[i])
            {
                RegRelease(program->wait_regs[i]);
                program->wait_regs[i] = nullptr;
            }
        }

        // Set info for all player descriptors to nullptr
        //
        DESC_ITER_PLAYER(d->player, all)
        {
            mux_assert(program == all->program_data);
            all->program_data = nullptr;
        }

        MEMFREE(program);
    }
    atr_clr(d->player, A_PROGCMD);
    free_lbuf(cmd);
}

void do_quitprog(dbref player, dbref caller, dbref enactor, int eval, int key, UTF8 *name, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (*name)
    {
        doer = match_thing(player, name);
    }
    else
    {
        doer = player;
    }

    if (  !(  Prog(player)
           || Prog(Owner(player)))
       && player != doer)
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (  !Good_obj(doer)
       || !isPlayer(doer))
    {
        notify(player, T("That is not a player."));
        return;
    }
    if (!Connected(doer))
    {
        notify(player, T("That player is not connected."));
        return;
    }
    DESC *d;
    bool isprog = false;
    DESC_ITER_PLAYER(doer, d)
    {
        if (nullptr != d->program_data)
        {
            isprog = true;
        }
    }

    if (!isprog)
    {
        notify(player, T("Player is not in an @program."));
        return;
    }

    d = (DESC *)hashfindLEN(&doer, sizeof(doer), &mudstate.desc_htab);
    int i;

    if (  d
       && d->program_data)
    {
        PROG *program = d->program_data;
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (program->wait_regs[i])
            {
                RegRelease(program->wait_regs[i]);
                program->wait_regs[i] = nullptr;
            }
        }

        // Set info for all player descriptors to nullptr.
        //
        DESC_ITER_PLAYER(doer, d)
        {
            mux_assert(program == d->program_data);
            d->program_data = nullptr;
        }

        MEMFREE(program);
    }

    atr_clr(doer, A_PROGCMD);
    notify(player, T("@program cleared."));
    notify(doer, T("Your @program has been terminated."));
}

void do_prog
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *name,
    UTF8 *command,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  !name
       || !*name)
    {
        notify(player, T("No players specified."));
        return;
    }

    dbref doer = match_thing(player, name);
    if (  !(  Prog(player)
           || Prog(Owner(player)))
       && player != doer)
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (  !Good_obj(doer)
       || !isPlayer(doer))
    {
        notify(player, T("That is not a player."));
        return;
    }
    if (!Connected(doer))
    {
        notify(player, T("That player is not connected."));
        return;
    }

    // Check to see if the enactor already has an @prog input pending.
    //
    DESC *d;
    DESC_ITER_PLAYER(doer, d)
    {
        if (d->program_data != nullptr)
        {
            notify(player, T("Input already pending."));
            return;
        }
    }

    UTF8 *msg = command;
    UTF8 *attrib = parse_to(&msg, ':', 1);

    if (msg && *msg)
    {
        notify(doer, msg);
    }

    dbref thing;
    ATTR *ap;
    if (!parse_attrib(player, attrib, &thing, &ap))
    {
        notify(player, NOMATCH_MESSAGE);
        return;
    }
    if (ap)
    {
        dbref aowner;
        int   aflags;
        int   lev;
        dbref parent;
        UTF8 *pBuffer = nullptr;
        bool bFound = false;
        ITER_PARENTS(thing, parent, lev)
        {
            pBuffer = atr_get("do_prog.1405", parent, ap->number, &aowner, &aflags);
            if (pBuffer[0])
            {
                bFound = true;
                break;
            }
            free_lbuf(pBuffer);
        }
        if (bFound)
        {
            if (  (   God(player)
                  || !God(thing))
               && See_attr(player, thing, ap))
            {
                atr_add_raw(doer, A_PROGCMD, pBuffer);
            }
            else
            {
                notify(player, NOPERM_MESSAGE);
                free_lbuf(pBuffer);
                return;
            }
            free_lbuf(pBuffer);
        }
        else
        {
            notify(player, T("Attribute not present on object."));
            return;
        }
    }
    else
    {
        notify(player, T("No such attribute."));
        return;
    }

    PROG *program = (PROG *)MEMALLOC(sizeof(PROG));
    ISOUTOFMEMORY(program);
    program->wait_enactor = player;
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        program->wait_regs[i] = mudstate.global_regs[i];
        if (mudstate.global_regs[i])
        {
            RegAddRef(mudstate.global_regs[i]);
        }
    }

    // Now, start waiting.
    //
    DESC_ITER_PLAYER(doer, d)
    {
        d->program_data = program;

        queue_string(d, tprintf(T("%s>%s "), COLOR_INTENSE, COLOR_RESET));

        if (OPTION_YES == us_state(d, TELNET_EOR))
        {
            // Use telnet protocol's EOR command to show prompt.
            //
            const UTF8 aEOR[2] = { NVT_IAC, NVT_EOR };
            queue_write_LEN(d, aEOR, sizeof(aEOR));
        }
        else if (OPTION_YES != us_state(d, TELNET_SGA))
        {
            // Use telnet protocol's GOAHEAD command to show prompt.
            //
            const UTF8 aGoAhead[2] = { NVT_IAC, NVT_GA };
            queue_write_LEN(d, aGoAhead, sizeof(aGoAhead));
        }
    }
}

/* ---------------------------------------------------------------------------
 * do_restart: Restarts the game.
 */
void do_restart(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    if (!Can_SiteAdmin(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    bool bDenied = false;
#if defined(HAVE_WORKING_FORK)
    if (mudstate.dumping)
    {
        notify(executor, T("Dumping. Please try again later."));
        bDenied = true;
    }
#endif // HAVE_WORKING_FORK


    if (!mudstate.bCanRestart)
    {
        notify(executor, T("Server just started. Please try again in a few seconds."));
        bDenied = true;
    }
    if (bDenied)
    {
        STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
        log_text(T("Restart requested but not executed by "));
        log_name(executor);
        ENDLOG;
        return;
    }

#ifdef UNIX_SSL
    raw_broadcast(0, T("GAME: Restart by %s, please wait.  (All SSL connections will be dropped.)"), Moniker(Owner(executor)));
#else
    raw_broadcast(0, T("GAME: Restart by %s, please wait."), Moniker(Owner(executor)));
#endif
    STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
    log_text(T("Restart by "));
    log_name(executor);
    ENDLOG;

#ifdef UNIX_SSL
    CleanUpSSLConnections();
#endif

    local_presync_database();
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->presync_database();
        p = p->pNext;
    }
#if defined(STUB_SLAVE)
    final_stubslave();
#endif // STUB_SLAVE
    final_modules();

#ifndef MEMORY_BASED
    al_store();
#endif
    pcache_sync();
    dump_database_internal(DUMP_I_RESTART);
    SYNC;
    CLOSE;

#if defined(WINDOWS_NETWORKING)
    WSACleanup();
#endif // WINDOWS_NETWORKING
#if defined(WINDOWS_PROCESSES)
    exit(12345678);
#elif defined(UNIX_PROCESSES)
#if defined(HAVE_WORKING_FORK)
    dump_restart_db();
    CleanUpSlaveSocket();
    CleanUpSlaveProcess();
#endif // HAVE_WORKING_FORK

    Log.StopLogging();

#ifdef GAME_DOOFERMUX
    execl("bin/netmux", mudconf.mud_name, "-c", mudconf.config_file, "-p",
        mudconf.pid_file, "-e", mudconf.log_dir, (char *)nullptr);
#else
    execl("bin/netmux", "netmux", "-c", mudconf.config_file, "-p",
        mudconf.pid_file, "-e", mudconf.log_dir, (char *)nullptr);
#endif // GAME_DOOFERMUX
    mux_assert(false);
#endif // UNIX_PROCESSES
}

/* ---------------------------------------------------------------------------
 * do_backup: Backs up and restarts the game
 * By Wadhah Al-Tailji (7-21-97), altailji@nmt.edu
 * Ported to MUX2 by Patrick Hill (7-5-2001), hellspawn@anomux.org
 */

#if defined(WINDOWS_PROCESSES)

void do_backup(dbref player, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    notify(player, T("This feature is not yet available on Windows-hosted MUX."));
}

#elif defined(UNIX_PROCESSES)

void do_backup(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

#if defined(HAVE_WORKING_FORK)
    if (mudstate.dumping)
    {
        notify(executor, T("Dumping. Please try again later."));
    }
#endif // HAVE_WORKING_FORK

    raw_broadcast(0, T("GAME: Backing up database. Please wait."));
    STARTLOG(LOG_ALWAYS, "WIZ", "BACK");
    log_text(T("Backup by "));
    log_name(executor);
    ENDLOG;

#ifdef MEMORY_BASED
    // Invoking _backupflat.sh with an argument prompts the backup script
    // to use it as the flatfile.
    //
    dump_database_internal(DUMP_I_FLAT);
    system((char *)tprintf(T("./_backupflat.sh %s.FLAT 1>&2"), mudconf.indb));
#else // MEMORY_BASED
    // Invoking _backupflat.sh without an argument prompts the backup script
    // to use dbconvert itself.
    //
    dump_database_internal(DUMP_I_NORMAL);
    system((char *)tprintf(T("./_backupflat.sh 1>&2")));
#endif // MEMORY_BASED
    raw_broadcast(0, T("GAME: Backup finished."));
}
#endif // UNIX_PROCESSES

/* ---------------------------------------------------------------------------
 * do_comment: Implement the @@ (comment) command. Very cpu-intensive :-)
 */

void do_comment(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
}

void do_eval(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg1, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(arg1);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);
}

static dbref promote_dflt(dbref old, dbref new0)
{
    if (  old == NOPERM
       || new0 == NOPERM)
    {
        return NOPERM;
    }
    if (  old == AMBIGUOUS
       || new0 == AMBIGUOUS)
    {
        return AMBIGUOUS;
    }
    return NOTHING;
}

dbref match_possessed(dbref player, dbref thing, UTF8 *target, dbref dflt, bool check_enter)
{
    // First, check normally.
    //
    if (Good_obj(dflt))
    {
        return dflt;
    }

    // Didn't find it directly.  Recursively do a contents check.
    //
    dbref result, result1;
    UTF8 *buff, *place, *s1, *d1, *temp;
    UTF8 *start = target;
    while (*target)
    {
        // Fail if no ' characters.
        //
        place = target;
        target = (UTF8 *)strchr((char *)place, '\'');
        if (  target == nullptr
           || !*target)
        {
            return dflt;
        }

        // If string started with a ', skip past it
        //
        if (place == target)
        {
            target++;
            continue;
        }

        // If next character is not an s or a space, skip past
        //
        temp = target++;
        if (!*target)
        {
            return dflt;
        }
        if (  *target != 's'
           && *target != 'S'
           && *target != ' ')
        {
            continue;
        }

        // If character was not a space make sure the following character is
        // a space.
        //
        if (*target != ' ')
        {
            target++;
            if (!*target)
            {
                return dflt;
            }
            if (*target != ' ')
            {
                continue;
            }
        }

        // Copy the container name to a new buffer so we can terminate it.
        //
        buff = alloc_lbuf("is_posess");
        for (s1 = start, d1 = buff; *s1 && (s1 < temp); *d1++ = (*s1++))
        {
            ; // Nothing.
        }
        *d1 = '\0';

        // Look for the container here and in our inventory.  Skip past if we
        // can't find it.
        //
        init_match(thing, buff, NOTYPE);
        if (player == thing)
        {
            match_neighbor();
            match_possession();
        }
        else
        {
            match_possession();
        }
        result1 = match_result();

        free_lbuf(buff);
        if (!Good_obj(result1))
        {
            dflt = promote_dflt(dflt, result1);
            continue;
        }

        // If we don't control it and it is either dark or opaque, skip past.
        //
        bool control = Controls(player, result1);
        if (  (  Dark(result1)
              || Opaque(result1))
           && !control)
        {
            dflt = promote_dflt(dflt, NOTHING);
            continue;
        }

        // Validate object has the ENTER bit set, if requested.
        //
        if (  check_enter
           && !Enter_ok(result1)
           && !control)
        {
            dflt = promote_dflt(dflt, NOPERM);
            continue;
        }

        // Look for the object in the container.
        //
        init_match(result1, target, NOTYPE);
        match_possession();
        result = match_result();
        result = match_possessed(player, result1, target, result, check_enter);
        if (Good_obj(result))
        {
            return result;
        }
        dflt = promote_dflt(dflt, result);
    }
    return dflt;
}

/* ---------------------------------------------------------------------------
 * parse_range: break up <what>,<low>,<high> syntax
 */

void parse_range(UTF8 **name, dbref *low_bound, dbref *high_bound)
{
    UTF8 *buff1 = *name;
    if (buff1 && *buff1)
    {
        *name = parse_to(&buff1, ',', EV_STRIP_TS);
    }
    if (buff1 && *buff1)
    {
        UTF8 *buff2 = parse_to(&buff1, ',', EV_STRIP_TS);
        if (buff1 && *buff1)
        {
            while (mux_isspace(*buff1))
            {
                buff1++;
            }

            if (*buff1 == NUMBER_TOKEN)
            {
                buff1++;
            }

            *high_bound = mux_atol(buff1);
            if (*high_bound >= mudstate.db_top)
            {
                *high_bound = mudstate.db_top - 1;
            }
        }
        else
        {
            *high_bound = mudstate.db_top - 1;
        }

        while (mux_isspace(*buff2))
        {
            buff2++;
        }

        if (*buff2 == NUMBER_TOKEN)
        {
            buff2++;
        }

        *low_bound = mux_atol(buff2);
        if (*low_bound < 0)
        {
            *low_bound = 0;
        }
    }
    else
    {
        *low_bound = 0;
        *high_bound = mudstate.db_top - 1;
    }
}

bool parse_thing_slash(dbref player, const UTF8 *thing, const UTF8 **after, dbref *it)
{
    // Get name up to '/'.
    //
    size_t i = 0;
    while (  thing[i] != '\0'
          && thing[i] != '/')
    {
        i++;
    }

    // If no '/' in string, return failure.
    //
    if (thing[i] == '\0')
    {
        *after = nullptr;
        *it = NOTHING;
        return false;
    }
    *after = thing + i + 1;

    // Look for the object.
    //
    init_match(player, thing, i, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    *it = match_result();

    // Return status of search.
    //
    return Good_obj(*it);
}

bool get_obj_and_lock(dbref player, const UTF8 *what, dbref *it, ATTR **attr, UTF8 *errmsg, UTF8 **bufc)
{
    // Get name up to '/'.
    //
    size_t i = 0;
    while (  what[i] != '\0'
          && what[i] != '/')
    {
        i++;
    }

    *it = match_thing_quiet(player, what, i);
    if (!Good_obj(*it))
    {
        safe_match_result(*it, errmsg, bufc);
        return false;
    }

    int anum;
    if (what[i] == '/')
    {
        // <obj>/<lock> syntax, use the named lock.
        //
        if (!search_nametab(player, lock_sw, what + i + 1, &anum))
        {
            safe_str(T("#-1 LOCK NOT FOUND"), errmsg, bufc);
            return false;
        }
    }
    else
    {
        // Not <obj>/<lock>, do a normal get of the default lock.
        //
        anum = A_LOCK;
    }

    // Get the attribute definition, fail if not found.
    //
    *attr = atr_num(anum);
    if (nullptr == *attr)
    {
        safe_str(T("#-1 LOCK NOT FOUND"), errmsg, bufc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// bCanReadAttr, bCanSetAttr: Verify permission to affect attributes.
// ---------------------------------------------------------------------------

bool bCanReadAttr(dbref executor, dbref target, ATTR *tattr, bool bCheckParent)
{
    if (!tattr)
    {
        return false;
    }

    dbref aowner;
    int aflags;

    if (  !mudstate.bStandAlone
       && bCheckParent)
    {
        atr_pget_info(target, tattr->number, &aowner, &aflags);
    }
    else
    {
        atr_get_info(target, tattr->number, &aowner, &aflags);
    }

    int test_flags = tattr->flags;

    if (mudstate.attrperm_list)
    {
        ATTRPERM *perm_walk = mudstate.attrperm_list;
        while (nullptr != perm_walk)
        {
            if (quick_wild(perm_walk->wildcard, tattr->name))
            {
                test_flags |= perm_walk->flags;
            }
            perm_walk = perm_walk->next;
        }
    }

    int mAllow = AF_VISUAL;
    if (  (test_flags & mAllow)
       || (aflags & mAllow))
    {
        if (  mudstate.bStandAlone
           || tattr->number != A_DESC
           || mudconf.read_rem_desc
           || nearby(executor, target))
        {
            return true;
        }
    }
    int mDeny = 0;
    if (WizRoy(executor))
    {
        if (God(executor))
        {
            mDeny = AF_INTERNAL;
        }
        else
        {
            mDeny = AF_INTERNAL|AF_DARK;
        }
    }
    else if (  Owner(executor) == aowner
            || Examinable(executor, target))
    {
        mDeny = AF_INTERNAL|AF_DARK|AF_MDARK;
    }
    if (mDeny)
    {
        if (  (test_flags & mDeny)
           || (aflags & mDeny))
        {
            return false;
        }
        else
        {
            return true;
        }
    }
    return false;
}

bool bCanSetAttr(dbref executor, dbref target, ATTR *tattr)
{
    if (!tattr)
    {
        return false;
    }

    int mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST;
    if (!God(executor))
    {
        if (God(target))
        {
            return false;
        }
        if (Wizard(executor))
        {
            mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST|AF_LOCK|AF_GOD;
        }
        else if (Controls(executor, target))
        {
            mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST|AF_LOCK|AF_WIZARD|AF_GOD;
        }
        else
        {
            return false;
        }
    }

    dbref aowner;
    int aflags;
    bool info = atr_get_info(target, tattr->number, &aowner, &aflags);
    int test_flags = tattr->flags;

    if (mudstate.attrperm_list)
    {
        ATTRPERM *perm_walk = mudstate.attrperm_list;
        while (nullptr != perm_walk)
        {
            if (quick_wild(perm_walk->wildcard,tattr->name))
            {
                test_flags |= perm_walk->flags;
            }
            perm_walk = perm_walk->next;
        }
    }

    if (  (test_flags & mDeny)
#ifdef FIRANMUX
       || Immutable(target)
#endif
       || (info && (aflags & mDeny)))
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool bCanLockAttr(dbref executor, dbref target, ATTR *tattr)
{
    if (!tattr)
    {
        return false;
    }

    int mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST;
    if (!God(executor))
    {
        if (God(target))
        {
            return false;
        }
        if (Wizard(executor))
        {
            mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST|AF_GOD;
        }
        else
        {
            mDeny = AF_INTERNAL|AF_IS_LOCK|AF_CONST|AF_WIZARD|AF_GOD;
        }
    }

    dbref aowner;
    int aflags;
    bool info = atr_get_info(target, tattr->number, &aowner, &aflags);
    int test_flags = tattr->flags;

    if (mudstate.attrperm_list)
    {
        ATTRPERM *perm_walk = mudstate.attrperm_list;
        while (nullptr != perm_walk)
        {
            if (quick_wild(perm_walk->wildcard,tattr->name))
            {
                test_flags |= perm_walk->flags;
            }
            perm_walk = perm_walk->next;
        }
    }

    if (  (test_flags & mDeny)
       || !info
       || (aflags & mDeny))
    {
        return false;
    }
    else if (  Wizard(executor)
            || Owner(executor) == aowner)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/* ---------------------------------------------------------------------------
 * where_is: Returns place where obj is linked into a list.
 * ie. location for players/things, source for exits, NOTHING for rooms.
 */

dbref where_is(dbref what)
{
    if (!Good_obj(what))
    {
        return NOTHING;
    }

    dbref loc;
    switch (Typeof(what))
    {
    case TYPE_PLAYER:
    case TYPE_THING:
        loc = Location(what);
        break;

    case TYPE_EXIT:
        loc = Exits(what);
        break;

    default:
        loc = NOTHING;
        break;
    }
    return loc;
}

/* ---------------------------------------------------------------------------
 * where_room: Return room containing player, or NOTHING if no room or
 * recursion exceeded.  If player is a room, returns itself.
 */

dbref where_room(dbref what)
{
    for (int count = mudconf.ntfy_nest_lim; count > 0; count--)
    {
        if (!Good_obj(what))
        {
            break;
        }
        if (isRoom(what))
        {
            return what;
        }
        if (!Has_location(what))
        {
            break;
        }
        what = Location(what);
    }
    return NOTHING;
}

bool locatable(dbref player, dbref it, dbref enactor)
{
    // No sense in trying to locate a bad object.
    //
    if (!Good_obj(it))
    {
        return false;
    }

    dbref loc_it = where_is(it);

    // Succeed if we can examine the target, if we are the target, if we can
    // examine the location, if a wizard caused the lookup, or if the target
    // caused the lookup.
    //
    if (  Examinable(player, it)
       || Find_Unfindable(player)
       || loc_it == player
       || (  loc_it != NOTHING
          && (  Examinable(player, loc_it)
             || loc_it == where_is(player))
          && (  !Hidden(it)
             || See_Hidden(player)))
       || Wizard(enactor)
       || it == enactor)
    {
        return true;
    }

    dbref room_it = where_room(it);
    bool findable_room;
    if (Good_obj(room_it))
    {
        findable_room = Findable(room_it);
    }
    else
    {
        findable_room = true;
    }

    // Succeed if we control the containing room or if the target is findable
    // and the containing room is not unfindable.
    //
    if (  (  room_it != NOTHING
          && Examinable(player, room_it))
          && (  !Hidden(it)
             || See_Hidden(player))
       || Find_Unfindable(player)
       || (  Findable(it)
          && findable_room)
          && (  !Hidden(it)
             || See_Hidden(player)))
    {
        return true;
    }

    // We can't do it.
    //
    return false;
}

/* ---------------------------------------------------------------------------
 * nearby: Check if thing is nearby player (in inventory, in same room, or
 * IS the room.
 */

bool nearby(dbref player, dbref thing)
{
    if (  !Good_obj(player)
       || !Good_obj(thing))
    {
        return false;
    }
    if (  Can_Hide(thing)
       && Hidden(thing)
       && !See_Hidden(player))
    {
        return false;
    }
    dbref thing_loc = where_is(thing);
    if (thing_loc == player)
    {
        return true;
    }
    dbref player_loc = where_is(player);
    if (  thing_loc == player_loc
       || thing == player_loc)
    {
        return true;
    }
    return false;
}

/*
 * ---------------------------------------------------------------------------
 * * exit_visible, exit_displayable: Is exit visible?
 */
bool exit_visible(dbref exit, dbref player, int key)
{
#ifdef WOD_REALMS
    if (!mudstate.bStandAlone)
    {
        int iRealmDirective = DoThingToThingVisibility(player, exit,
            ACTION_IS_STATIONARY);
        if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
        {
            return false;
        }
    }
#endif // WOD_REALMS

#ifdef REALITY_LVLS
    if (!mudstate.bStandAlone)
    {
        if (!IsReal(player, exit))
        {
            return false;
        }
    }
#endif // REALITY_LVLS

    // Exam exit's location
    //
    if (  (key & VE_LOC_XAM)
       || Examinable(player, exit)
       || Light(exit))
    {
        return true;
    }

    // Dark location or base
    //
    if (  (key & (VE_LOC_DARK | VE_BASE_DARK))
       || Dark(exit))
    {
        return false;
    }

    // Default
    //
    return true;
}

// Exit visible to look
//
bool exit_displayable(dbref exit, dbref player, int key)
{
#if !defined(WOD_REALMS) && !defined(REALITY_LVLS)
    UNUSED_PARAMETER(player);
#endif // WOD_REALMS

    // Dark exit
    //
    if (Dark(exit))
    {
        return false;
    }

#ifdef WOD_REALMS
    if (!mudstate.bStandAlone)
    {
        int iRealmDirective = DoThingToThingVisibility(player, exit,
            ACTION_IS_STATIONARY);
        if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
        {
            return false;
        }
    }
#endif // WOD_REALMS

#ifdef REALITY_LVLS
    if (!mudstate.bStandAlone)
    {
        if (!IsReal(player, exit))
        {
            return false;
        }
    }
#endif // REALITY_LVLS

    // Light exit
    //
    if (Light(exit))
    {
        return true;
    }

    // Dark location or base.
    //
    if (key & (VE_LOC_DARK | VE_BASE_DARK))
    {
        return false;
    }

    // Default
    //
    return true;
}

/* ---------------------------------------------------------------------------
 * did_it: Have player do something to/with thing
 */

void did_it(dbref player, dbref thing, int what, const UTF8 *def, int owhat,
            const UTF8 *odef, int awhat, int ctrl_flags,
            const UTF8 *args[], int nargs)
{
    if (alarm_clock.alarmed)
    {
        return;
    }

    UTF8 *d, *buff, *act, *charges, *bp;
    dbref loc, aowner;
    int num, aflags;

    // If we need to call exec() from within this function, we first save
    // the state of the global registers, in order to avoid munging them
    // inappropriately. Do note that the restoration to their original
    // values occurs BEFORE the execution of the @a-attribute. Therefore,
    // any changing of setq() values done in the @-attribute and @o-attribute
    // will NOT be passed on. This prevents odd behaviors that result from
    // odd @verbs and so forth (the idea is to preserve the caller's control
    // of the global register values).
    //

    bool need_pres = false;
    reg_ref **preserve = nullptr;

    // message to player.
    //
    if (what > 0)
    {
        d = atr_pget(thing, what, &aowner, &aflags);
        if (*d)
        {
            need_pres = true;
            preserve = PushRegisters(MAX_GLOBAL_REGS);
            save_global_regs(preserve);

            buff = bp = alloc_lbuf("did_it.1");
            mux_exec(d, LBUF_SIZE-1, buff, &bp, thing, player, player,
                AttrTrace(aflags, EV_EVAL|EV_FIGNORE|EV_FCHECK|EV_TOP),
                args, nargs);
            *bp = '\0';
            if (  (aflags & AF_HTML)
               && Html(player))
            {
                safe_str(T("\r\n"), buff, &bp);
                *bp = '\0';
                notify_html(player, buff);
            }
#if defined(FIRANMUX)
            else if (  A_DESC == what
                    && Linewrap(player)
                    && isPlayer(player)
                    && (  !Linewrap(thing)
                       || isPlayer(thing)))
            {
                UTF8 *p = alloc_lbuf("did_it.2");
                linewrap_general(buff, 71, p, LBUF_SIZE-1, T("     "), 5);
                notify(player, p);
                free_lbuf(p);
            }
#endif // FIRANMUX
            else
            {
                notify(player, buff);
            }
            free_lbuf(buff);
        }
        else if (def)
        {
            notify(player, def);
        }
        free_lbuf(d);
    }
    else if (what < 0 && def)
    {
        notify(player, def);
    }

    // message to neighbors.
    //
    if (  0 < owhat
       && Has_location(player)
       && Good_obj(loc = Location(player)))
    {
        d = atr_pget(thing, owhat, &aowner, &aflags);
        if (*d)
        {
            if (!need_pres)
            {
                need_pres = true;
                preserve = PushRegisters(MAX_GLOBAL_REGS);
                save_global_regs(preserve);
            }
            buff = bp = alloc_lbuf("did_it.2");
            mux_exec(d, LBUF_SIZE-1, buff, &bp, thing, player, player,
                 AttrTrace(aflags, EV_EVAL|EV_FIGNORE|EV_FCHECK|EV_TOP),
                 args, nargs);
            *bp = '\0';
#if !defined(FIRANMUX)
            if (*buff)
#endif // FIRANMUX
            {
#ifdef REALITY_LVLS
                if (aflags & AF_NONAME)
                {
                    notify_except2_rlevel(loc, player, player, thing, buff);
                }
                else
                {
                    notify_except2_rlevel(loc, player, player, thing,
                        tprintf(T("%s %s"), Moniker(player), buff));
                }
#else
                if (aflags & AF_NONAME)
                {
                    notify_except2(loc, player, player, thing, buff);
                }
                else
                {
                    notify_except2(loc, player, player, thing,
                        tprintf(T("%s %s"), Moniker(player), buff));
                }
#endif // REALITY_LVLS
            }
            free_lbuf(buff);
        }
        else if (odef)
        {
#ifdef REALITY_LVLS
            if (ctrl_flags & VERB_NONAME)
            {
                notify_except2_rlevel(loc, player, player, thing, odef);
            }
            else
            {
                notify_except2_rlevel(loc, player, player, thing,
                        tprintf(T("%s %s"), Moniker(player), odef));
            }
#else
            if (ctrl_flags & VERB_NONAME)
            {
                notify_except2(loc, player, player, thing, odef);
            }
            else
            {
                notify_except2(loc, player, player, thing,
                        tprintf(T("%s %s"), Moniker(player), odef));
            }
#endif // REALITY_LVLS
        }
        free_lbuf(d);
    } else if (  owhat < 0
              && odef
              && Has_location(player)
              && Good_obj(loc = Location(player)))
    {
#ifdef REALITY_LVLS
        if (ctrl_flags & VERB_NONAME)
        {
            notify_except2_rlevel(loc, player, player, thing, odef);
        }
        else
        {
            notify_except2_rlevel(loc, player, player, thing, tprintf(T("%s %s"), Name(player), odef));
        }
#else
        if (ctrl_flags & VERB_NONAME)
        {
            notify_except2(loc, player, player, thing, odef);
        }
        else
        {
            notify_except2(loc, player, player, thing, tprintf(T("%s %s"), Name(player), odef));
        }
#endif // REALITY_LVLS
    }

    // If we preserved the state of the global registers, restore them.
    //
    if (need_pres)
    {
        restore_global_regs(preserve);
        PopRegisters(preserve, MAX_GLOBAL_REGS);
    }

    // Do the action attribute.
    //
#ifdef REALITY_LVLS
    if (  0 < awhat
       && IsReal(thing, player))
#else
    if (0 < awhat)
#endif // REALITY_LVLS
    {
        if (*(act = atr_pget(thing, awhat, &aowner, &aflags)))
        {
            dbref aowner2;
            int   aflags2;
            charges = atr_pget(thing, A_CHARGES, &aowner2, &aflags2);
            if (*charges)
            {
                num = mux_atol(charges);
                if (num > 0)
                {
                    buff = alloc_sbuf("did_it.charges");
                    mux_ltoa(num-1, buff);
                    atr_add_raw(thing, A_CHARGES, buff);
                    free_sbuf(buff);
                }
                else if (*(buff = atr_pget(thing, A_RUNOUT, &aowner2, &aflags2)))
                {
                    free_lbuf(act);
                    act = buff;
                }
                else
                {
                    free_lbuf(act);
                    free_lbuf(buff);
                    free_lbuf(charges);
                    return;
                }
            }
            free_lbuf(charges);
            CLinearTimeAbsolute lta;
            wait_que(thing, player, player, AttrTrace(aflags, 0), false, lta,
                NOTHING, 0,
                act,
                nargs, args,
                mudstate.global_regs);
        }
        free_lbuf(act);
    }
}

/* ---------------------------------------------------------------------------
 * do_verb: Command interface to did_it.
 */

void do_verb(dbref executor, dbref caller, dbref enactor, int eval, int key,
             UTF8 *victim_str, UTF8 *args[], int nargs, const UTF8 *cargs[], int ncargs)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(key);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Look for the victim.
    //
    if (  !victim_str
       || !*victim_str)
    {
        notify(executor, T("Nothing to do."));
        return;
    }

    // Get the victim.
    //
    init_match(executor, victim_str, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    dbref victim = noisy_match_result();
    if (!Good_obj(victim))
    {
        return;
    }

    // Get the actor.  Default is my cause.
    //
    dbref actor;
    if (  nargs >= 1
       && args[0] && *args[0])
    {
        init_match(executor, args[0], NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        actor = noisy_match_result();
        if (!Good_obj(actor))
        {
            return;
        }
    }
    else
    {
        actor = enactor;
    }

    // Check permissions.  There are two possibilities:
    //
    //    1. Executor controls both victim and actor. In this case,
    //       victim runs his action list.
    //
    //    2. Executor controls actor. In this case victim does not run
    //       his action list and any attributes that executor cannot read
    //       from victim are defaulted.
    //
    if (!Controls(executor, actor))
    {
        notify_quiet(executor, T("Permission denied,"));
        return;
    }

    ATTR *ap;
    int what = -1;
    int owhat = -1;
    int awhat = -1;
    const UTF8 *whatd = nullptr;
    const UTF8 *owhatd = nullptr;
    int nxargs = 0;
    dbref aowner = NOTHING;
    int aflags = NOTHING;
    UTF8 *xargs[10];

    switch (nargs) // Yes, this IS supposed to fall through.
    {
    case 7:
        // Get arguments.
        //
        parse_arglist(victim, actor, actor, args[6],
            EV_STRIP_LS | EV_STRIP_TS, xargs, 10, nullptr, 0, &nxargs);

    case 6:
        // Get action attribute.
        //
        ap = atr_str(args[5]);
        if (ap)
        {
            awhat = ap->number;
        }

    case 5:
        // Get others message default.
        //
        if (args[4] && *args[4])
        {
            owhatd = args[4];
        }

    case 4:
        // Get others message attribute.
        //
        ap = atr_str(args[3]);
        if (ap && (ap->number > 0))
        {
            owhat = ap->number;
        }

    case 3:
        // Get enactor message default.
        //
        if (args[2] && *args[2])
        {
            whatd = args[2];
        }

    case 2:
        // Get enactor message attribute.
        //
        ap = atr_str(args[1]);
        if (ap && (ap->number > 0))
        {
            what = ap->number;
        }
    }

    // If executor doesn't control both, enforce visibility restrictions.
    //
    if (!Controls(executor, victim))
    {
        ap = nullptr;
        if (what != -1)
        {
            atr_get_info(victim, what, &aowner, &aflags);
            ap = atr_num(what);
        }
        if (  !ap
           || !bCanReadAttr(executor, victim, ap, false)
           || (  ap->number == A_DESC
              && !mudconf.read_rem_desc
              && !Examinable(executor, victim)
              && !nearby(executor, victim)))
        {
            what = -1;
        }

        ap = nullptr;
        if (owhat != -1)
        {
            atr_get_info(victim, owhat, &aowner, &aflags);
            ap = atr_num(owhat);
        }
        if (  !ap
           || !bCanReadAttr(executor, victim, ap, false)
           || (  ap->number == A_DESC
              && !mudconf.read_rem_desc
              && !Examinable(executor, victim)
              && !nearby(executor, victim)))
        {
            owhat = -1;
        }

        awhat = 0;
    }

    // Go do it.
    //
    did_it(actor, victim, what, whatd, owhat, owhatd, awhat,
        key & VERB_NONAME, (const UTF8 **)xargs, nxargs);

    // Free user args.
    //
    for (int i = 0; i < nxargs; i++)
    {
        free_lbuf(xargs[i]);
    }
}

// --------------------------------------------------------------------------
// OutOfMemory: handle an out of memory condition.
//
void OutOfMemory(const UTF8 *SourceFile, unsigned int LineNo)
{
    mudstate.asserting++;
    if (  1 <= mudstate.asserting
       && mudstate.asserting <= 2)
    {
        Log.tinyprintf(T("%s(%u): Out of memory." ENDLINE), SourceFile, LineNo);
        Log.Flush();
        if (  !mudstate.bStandAlone
           && mudstate.bCanRestart)
        {
            do_restart(GOD, GOD, GOD, 0, 0);
        }
        else
        {
            abort();
        }
    }
    mudstate.asserting--;
}

// --------------------------------------------------------------------------
// AssertionFailed: A logical assertion has failed.
//
bool AssertionFailed(const UTF8 *SourceFile, unsigned int LineNo)
{
    mudstate.asserting++;
    if (  1 <= mudstate.asserting
       && mudstate.asserting <= 2)
    {
        Log.tinyprintf(T("%s(%u): Assertion failed." ENDLINE), SourceFile, LineNo);
        report();
        Log.Flush();
        if (  !mudstate.bStandAlone
           && mudstate.bCanRestart)
        {
            do_restart(GOD, GOD, GOD, 0, 0);
        }
        else
        {
            abort();
        }
    }
    else
    {
        abort();
    }
    mudstate.asserting--;
    return false;
}

static void ListReferences(dbref executor, UTF8 *reference_name)
{
    dbref target = NOTHING;
    bool global_only = false;
    mux_string refstr(reference_name);

    if (  nullptr == reference_name
       || '\0' == reference_name[0])
    {
        global_only = true;
        refstr.prepend('_');
    }
    else
    {
        global_only = false;
        target = lookup_player(executor, reference_name, 1);
        if (!Good_obj(target))
        {
            raw_notify(executor, T("No such player."));
            return;
        }

        if (!Controls(executor, target))
        {
            raw_notify(executor, NOPERM_MESSAGE);
            return;
        }
    }

    //  Listing:
    //    - if global_only is true, list all references that begin with _
    //    - Otherwise, list all references whose owner is target
    //
    reference_entry *htab_entry;
    bool match_found = false;

    CHashTable* htab = &mudstate.reference_htab;
    for (  htab_entry = (struct reference_entry *) hash_firstentry(htab);
           nullptr != htab_entry;
           htab_entry = (struct reference_entry *) hash_nextentry(htab))
    {
        if (  (  global_only
              && '_' == htab_entry->name[0])
           || (  !global_only
              && target == htab_entry->owner))
        {
            if (!Good_obj(htab_entry->target))
            {
                continue;
            }

            if (!match_found)
            {
                match_found = true;
                raw_notify(executor, tprintf(T("%-12s %-20s %-20s"),
                            T("Reference"), T("Target"), T("Owner")));
                raw_notify(executor,
                        T("-------------------------------------------------------"));
            }

            UTF8 *object_buf =
                unparse_object(executor, htab_entry->target, false);

            raw_notify(executor, tprintf(T("%-12s %-20s %-20s"), htab_entry->name,
                        object_buf, Moniker(htab_entry->owner)));

            free_lbuf(object_buf);
        }
    }

    if (!match_found)
    {
        raw_notify(executor, T("GAME: No references found."));
    }
    else
    {
        raw_notify(executor,
                T("---------------- End of Reference List ----------------"));
    }
}

void do_reference
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *reference_name,
    UTF8 *object_name,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(cargs);

    if (key & REFERENCE_LIST)
    {
        ListReferences(executor, reference_name);
        return;
    }

    // References can only be set on objects the executor can examine.
    //
    dbref target = NOTHING;
    if (  nullptr != object_name
       && '\0' != object_name[0])
    {
        target = match_thing_quiet(executor, object_name);

        if (!Good_obj(target))
        {
            notify(executor, NOMATCH_MESSAGE);
            return;
        }
        else if (!Examinable(executor, target))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }
    }

    mux_string refstr(reference_name);
    if ('_' == reference_name[0])
    {
        if (!Wizard(executor))
        {
            notify(executor, NOPERM_MESSAGE);
            return;
        }
    }
    else
    {
        refstr.append(T("."));
        refstr.append(executor);
    }

    UTF8 tbuf[LBUF_SIZE];
    size_t tbuf_len = refstr.export_TextPlain(tbuf);
    struct reference_entry *result = (reference_entry *)hashfindLEN(
        tbuf, tbuf_len, &mudstate.reference_htab);

    enum { Delete, Add, Update, NotFound, Redundant, OutOfMemory } eOperation;

    if (nullptr != result)
    {
        if (NOTHING == target)
        {
            eOperation = Delete;
        }
        else if (result->target == target)
        {
            eOperation = Redundant;
        }
        else // if (result->target != target)
        {
            eOperation = Update;
        }
    }
    else
    {
        if (NOTHING == target)
        {
            eOperation = NotFound;
        }
        else
        {
            eOperation = Add;
            if (  !Wizard(executor)
               && ThrottleReferences(executor))
            {
                raw_notify(executor, T("References requested too quickly."));
                return;
            }
        }
    }

    if (  Delete == eOperation
       || Update == eOperation)
    {
        // Release the existing reference.
        //
        MEMFREE(result->name);
        result->name = nullptr;
        MEMFREE(result);
        result = nullptr;
        hashdeleteLEN(tbuf, tbuf_len, &mudstate.reference_htab);
    }

    if (  Update == eOperation
       || Add == eOperation)
    {
        try
        {
            result = (reference_entry *)MEMALLOC(sizeof(reference_entry));
        }
        catch(...)
        {
            ; // Nothing;
        }

        if (nullptr != result)
        {
            result->target = target;
            result->owner = executor;
            result->name = StringCloneLen(tbuf, tbuf_len);
            hashaddLEN(tbuf, tbuf_len, result, &mudstate.reference_htab);
        }
        else
        {
            eOperation = OutOfMemory;
        }
    }

    if (Delete == eOperation)
    {
        raw_notify(executor, T("Reference cleared."));
    }
    else if (Update == eOperation)
    {
        raw_notify(executor, T("Reference updated."));
    }
    else if (Redundant == eOperation)
    {
        raw_notify(executor, T("That reference already exists."));
    }
    else if (NotFound == eOperation)
    {
        raw_notify(executor, T("No such reference to clear."));
    }
    else if (Add == eOperation)
    {
        raw_notify(executor, T("Reference added."));
    }
    else if (OutOfMemory == eOperation)
    {
        raw_notify(executor, OUT_OF_MEMORY);
    }
}
