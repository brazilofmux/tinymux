// predicates.cpp
//
// $Id: predicates.cpp,v 1.61 2005-06-28 20:25:56 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <signal.h>

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "interface.h"
#include "powers.h"

extern bool do_command(DESC *, char *);
extern void pcache_sync(void);

char * DCL_CDECL tprintf(const char *fmt,...)
{
    static char buff[LBUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    mux_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);
    return buff;
}

void DCL_CDECL safe_tprintf_str(char *str, char **bp, const char *fmt,...)
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
    char *key = atr_get(thing, locknum, &aowner, &aflags);
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

    // If loc is not dark, you see it if it's not dark or you control it.  If
    // loc is dark, you see it if you control it.  Seeing your own dark
    // objects is controlled by mudconf.see_own_dark.  In dark locations, you
    // also see things that are LIGHT and !DARK.
    //
    if (can_see_loc)
    {
        return (!Dark(thing) ||
            (mudconf.see_own_dark && MyopicExam(player, thing)));
    }
    else
    {
        return ((Light(thing) && !Dark(thing)) ||
            (mudconf.see_own_dark && MyopicExam(player, thing)));
    }
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
    char *quota_str = atr_get(Owner(who), A_RQUOTA, &aowner, &aflags);
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
    char buf[20];
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
            notify(player, tprintf("Sorry, you don't have enough %s.",
                       mudconf.many_coins));
        }
        else
        {
            notify(player, tprintf("Sorry, that player doesn't have enough %s.",
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
                notify(player, "Sorry, your building contract has run out.");
            }
            else
            {
                notify(player,
                    "Sorry, that player's building contract has run out.");
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
    char buf[20];

    char *quota = atr_get(who, A_RQUOTA, &aowner, &aflags);
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

// The following function validates that the object names (which will be
// used for things and rooms, but not for players or exits) and generates
// a canonical form of that name (with optimized ANSI).
//
char *MakeCanonicalObjectName(const char *pName, int *pnName, bool *pbValid)
{
    static char Buf[MBUF_SIZE];

    *pnName = 0;
    *pbValid = false;

    if (!pName)
    {
        return NULL;
    }

    // Build up what the real name would be. If we pass all the
    // checks, this is what we will return as a result.
    //
    int nVisualWidth;
    int nBuf = ANSI_TruncateToField(pName, sizeof(Buf), Buf, MBUF_SIZE,
        &nVisualWidth, ANSI_ENDGOAL_NORMAL);

    // Disallow pure ANSI names. There must be at least -something-
    // visible.
    //
    if (nVisualWidth <= 0)
    {
        return NULL;
    }

    // Get the stripped version (Visible parts without color info).
    //
    size_t nStripped;
    char *pStripped = strip_ansi(Buf, &nStripped);

    // Do not allow LOOKUP_TOKEN, NUMBER_TOKEN, NOT_TOKEN, or SPACE
    // as the first character, or SPACE as the last character
    //
    if (  strchr("*!#", *pStripped)
       || mux_isspace(pStripped[0])
       || mux_isspace(pStripped[nStripped-1]))
    {
        return NULL;
    }

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    for (unsigned int i = 0; i < nStripped; i++)
    {
        if (!mux_ObjectNameSet(pStripped[i]))
        {
            return NULL;
        }
    }

    // Special names are specifically dis-allowed.
    //
    if (  (nStripped == 2 && memcmp("me", pStripped, 2) == 0)
       || (nStripped == 4 && (  memcmp("home", pStripped, 4) == 0
                             || memcmp("here", pStripped, 4) == 0)))
    {
        return NULL;
    }

    *pnName = nBuf;
    *pbValid = true;
    return Buf;
}

// The following function validates exit names.
//
char *MakeCanonicalExitName(const char *pName, int *pnName, bool *pbValid)
{
    static char Buf[MBUF_SIZE];
    static char Out[MBUF_SIZE];

    *pnName = 0;
    *pbValid = false;

    if (!pName)
    {
        return NULL;
    }

    // Build the non-ANSI version so that we can parse for semicolons
    // safely.
    //
    char *pStripped = strip_ansi(pName);
    char *pBuf = Buf;
    safe_mb_str(pStripped, Buf, &pBuf);
    *pBuf = '\0';

    size_t nBuf = pBuf - Buf;
    pBuf = Buf;

    bool bHaveDisplay = false;

    char *pOut = Out;

    for (; nBuf;)
    {
        // Build (q,n) as the next segment.  Leave the the remaining segments as
        // (pBuf,nBuf).
        //
        char *q = strchr(pBuf, ';');
        size_t n;
        if (q)
        {
            *q = '\0';
            n = q - pBuf;
            q = pBuf;
            pBuf += n + 1;
            nBuf -= n + 1;
        }
        else
        {
            n = nBuf;
            q = pBuf;
            pBuf += nBuf;
            nBuf = 0;
        }

        if (bHaveDisplay)
        {
            // We already have the displayable name. We don't allow ANSI in
            // any segment but the first, so we can pull them directly from
            // the stripped buffer.
            //
            int  nN;
            bool bN;
            char *pN = MakeCanonicalObjectName(q, &nN, &bN);
            if (  bN
               && nN < MBUF_SIZE - (pOut - Out) - 1)
            {
                safe_mb_chr(';', Out, &pOut);
                safe_mb_str(pN, Out, &pOut);
            }
        }
        else
        {
            // We don't have the displayable name, yet. We know where the next
            // semicolon occurs, so we limit the visible width of the
            // truncation to that.  We should be picking up all the visible
            // characters leading up to the semicolon, but not including the
            // semi-colon.
            //
            int vw;
            ANSI_TruncateToField(pName, sizeof(Out), Out, n, &vw,
                ANSI_ENDGOAL_NORMAL);

            // vw should always be equal to n, but we'll just make sure.
            //
            if ((size_t)vw == n)
            {
                int  nN;
                bool bN;
                char *pN = MakeCanonicalObjectName(Out, &nN, &bN);
                if (  bN
                   && nN <= MBUF_SIZE - 1)
                {
                    safe_mb_str(pN, Out, &pOut);
                    bHaveDisplay = true;
                }
            }
        }
    }
    if (bHaveDisplay)
    {
        *pnName = pOut - Out;
        *pbValid = true;
        *pOut = '\0';
        return Out;
    }
    else
    {
        return NULL;
    }
}

// The following function validates the player name. ANSI is not
// allowed in player names. However, a player name must satisfy
// the requirements of a regular name as well.
//
bool ValidatePlayerName(const char *pName)
{
    if (!pName)
    {
        return false;
    }
    size_t nName = strlen(pName);

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
    if (  strchr("*!#", *pName)
       || mux_isspace(pName[0])
       || mux_isspace(pName[nName-1]))
    {
        return false;
    }

    if (  mudstate.bStandAlone
       || mudconf.name_spaces)
    {
        mux_PlayerNameSet[' '] = 1;
    }
    else
    {
        mux_PlayerNameSet[' '] = 0;
    }

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    for (unsigned int i = 0; i < nName; i++)
    {
        if (!mux_PlayerNameSet(pName[i]))
        {
            return false;
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
    return true;
}

bool ok_password(const char *password, const char **pmsg)
{
    *pmsg = NULL;

    if (*password == '\0')
    {
        *pmsg = "Null passwords are not allowed.";
        return false;
    }

    const char *scan;
    int num_upper = 0;
    int num_special = 0;
    int num_lower = 0;

    for (scan = password; *scan; scan++)
    {
        if (  !mux_isprint(*scan)
           || mux_isspace(*scan))
        {
            *pmsg = "Illegal character in password.";
            return false;
        }
        if (mux_isupper(*scan))
        {
            num_upper++;
        }
        else if (mux_islower(*scan))
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
            *pmsg = "The password must contain at least one capital letter.";
            return false;
        }
        if (num_lower < 1)
        {
            *pmsg = "The password must contain at least one lowercase letter.";
            return false;
        }
        if (num_special < 1)
        {
            *pmsg = "The password must contain at least one number or a symbol other than the apostrophe or dash.";
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
    char *buff, *bp;
    int gender;
    static const char *poss[5] =
    {"", "its", "her", "his", "their"};

    if (  !could_hear
       && can_hear)
    {
        buff = alloc_lbuf("handle_ears.grow");
        strcpy(buff, Name(thing));
        if (isExit(thing))
        {
            for (bp = buff; *bp && *bp != ';'; bp++)
            {
                ; // Nothing.
            }
            *bp = '\0';
        }
        gender = get_gender(thing);
        notify_check(thing, thing, tprintf("%s grow%s ears and can now hear.",
            buff, (gender == 4) ? "" : "s"),
            (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
        free_lbuf(buff);
    }
    else if (  could_hear
            && !can_hear)
    {
        buff = alloc_lbuf("handle_ears.lose");
        strcpy(buff, Name(thing));
        if (isExit(thing))
        {
            for (bp = buff; *bp && *bp != ';'; bp++)
            {
                ; // Nothing.
            }
            *bp = '\0';
        }
        gender = get_gender(thing);
        notify_check(thing, thing,
            tprintf("%s lose%s %s ears and become%s deaf.", buff,
            (gender == 4) ? "" : "s", poss[gender], (gender == 4) ? "" : "s"),
            (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
        free_lbuf(buff);
    }
}

// For lack of better place the @switch code is here.
//
void do_switch
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   key,
    char *expr,
    char *args[],
    int   nargs,
    char *cargs[],
    int   ncargs
)
{
    if (  !expr
       || nargs <= 0)
    {
        return;
    }

    if (key == SWITCH_DEFAULT)
    {
        if (mudconf.switch_df_all)
        {
            key = SWITCH_ANY;
        }
        else
        {
            key = SWITCH_ONE;
        }
    }

    // Now try a wild card match of buff with stuff in coms.
    //
    bool any = false;
    int a;
    char *buff, *bp, *str;
    buff = bp = alloc_lbuf("do_switch");
    CLinearTimeAbsolute lta;
    for (  a = 0;
              a < nargs - 1
           && args[a]
           && args[a + 1];
           a += 2)
    {
        bp = buff;
        str = args[a];
        mux_exec(buff, &bp, player, caller, enactor, EV_FCHECK | EV_EVAL | EV_TOP,
            &str, cargs, ncargs);
        *bp = '\0';
        if (wild_match(buff, expr))
        {
            char *tbuf = replace_tokens(args[a+1], NULL, NULL, expr);
            wait_que(player, caller, enactor, false, lta, NOTHING, 0,
                tbuf, cargs, ncargs, mudstate.global_regs);
            free_lbuf(tbuf);
            if (key == SWITCH_ONE)
            {
                free_lbuf(buff);
                return;
            }
            any = true;
        }
    }
    free_lbuf(buff);
    if (  a < nargs
       && !any
       && args[a])
    {
        char *tbuf = replace_tokens(args[a], NULL, NULL, expr);
        wait_que(player, caller, enactor, false, lta, NOTHING, 0, tbuf,
            cargs, ncargs, mudstate.global_regs);
        free_lbuf(tbuf);
    }
}

// Also for lack of better place the @ifelse code is here.
// Idea for @ifelse from ChaoticMUX.
//
void do_if
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   key,
    char *expr,
    char *args[],
    int   nargs,
    char *cargs[],
    int   ncargs
)
{
    if (  !expr
       || nargs <= 0)
    {
        return;
    }

    char *buff, *bp;
    CLinearTimeAbsolute lta;
    buff = bp = alloc_lbuf("do_if");

    mux_exec(buff, &bp, player, caller, enactor, EV_FCHECK | EV_EVAL | EV_TOP,
        &expr, cargs, ncargs);
    int a = !xlate(buff);
    free_lbuf(buff);

    if (a < nargs)
    {
        wait_que(player, caller, enactor, false, lta, NOTHING, 0, args[a],
            cargs, ncargs, mudstate.global_regs);
    }
}

void do_addcommand
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    // Validate command name.
    //
    char *pName = NULL;
    if (1 <= nargs)
    {
        char *pStripped = strip_ansi(name);
        pName = RemoveSetOfCharacters(pStripped, "\r\n\t ");
        mux_strlwr(pName);
    }
    if (  !pName
       || pName[0] == '\0'
       || (  pName[0] == '_'
          && pName[1] == '_'))
    {
        notify(player, "That is not a valid command name.");
        return;
    }

    // Validate object/attribute.
    //
    dbref thing;
    ATTR *pattr;
    if (  !parse_attrib(player, command, &thing, &pattr)
       || !pattr)
    {
        notify(player, "No such attribute.");
        return;
    }
    if (!See_attr(player, thing, pattr))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }

    CMDENT *old = (CMDENT *)hashfindLEN(pName, strlen(pName),
        &mudstate.command_htab);

    CMDENT *cmd;
    ADDENT *add, *nextp;

    if (  old
       && (old->callseq & CS_ADDED))
    {
        // Don't allow the same (thing,atr) in the list.
        //
        for (nextp = old->addent; nextp != NULL; nextp = nextp->next)
        {
            if (  nextp->thing == thing
               && nextp->atr == pattr->number)
            {
                notify(player, tprintf("%s already added.", pName));
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
            hashdeleteLEN(pName, strlen(pName), &mudstate.command_htab);
        }

        cmd = (CMDENT *)MEMALLOC(sizeof(CMDENT));
        ISOUTOFMEMORY(cmd);
        cmd->cmdname = StringClone(pName);
        cmd->switches = NULL;
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
        cmd->hookmask = 0;
        add = (ADDENT *)MEMALLOC(sizeof(ADDENT));
        ISOUTOFMEMORY(add);
        add->thing = thing;
        add->atr = pattr->number;
        add->name = StringClone(pName);
        add->next = NULL;
        cmd->addent = add;

        hashaddLEN(pName, strlen(pName), cmd, &mudstate.command_htab);

        if (  old
           && strcmp(pName, old->cmdname) == 0)
        {
            // We are @addcommand'ing over a built-in command by its
            // unaliased name, therefore, we want to re-target all the
            // aliases.
            //
            char *p = tprintf("__%s", pName);
            hashdeleteLEN(p, strlen(p), &mudstate.command_htab);
            hashreplall(old, cmd, &mudstate.command_htab);
            hashaddLEN(p, strlen(p), old, &mudstate.command_htab);
        }
    }

    // We reset the one letter commands here so you can overload them.
    //
    set_prefix_cmds();
    notify(player, tprintf("Command %s added.", pName));
}

void do_listcommands(dbref player, dbref caller, dbref enactor, int key,
                     char *name)
{
    CMDENT *old;
    ADDENT *nextp;
    bool didit = false;

    // Let's make this case insensitive...
    //
    mux_strlwr(name);

    if (*name)
    {
        old = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);

        if (  old
           && (old->callseq & CS_ADDED))
        {
            // If it's already found in the hash table, and it's being added
            // using the same object and attribute...
            //
            for (nextp = old->addent; nextp != NULL; nextp = nextp->next)
            {
                ATTR *ap = (ATTR *)atr_num(nextp->atr);
                const char *pName = "(WARNING: Bad Attribute Number)";
                if (ap)
                {
                    pName = ap->name;
                }
                notify(player, tprintf("%s: #%d/%s", nextp->name, nextp->thing, pName));
            }
        }
        else
        {
            notify(player, tprintf("%s not found in command table.",name));
        }
        return;
    }
    else
    {
        char *pKeyName;
        int  nKeyName;
        for (old = (CMDENT *)hash_firstkey(&mudstate.command_htab, &nKeyName, &pKeyName);
             old != NULL;
             old = (CMDENT *)hash_nextkey(&mudstate.command_htab, &nKeyName, &pKeyName))
        {
            if (old->callseq & CS_ADDED)
            {
                pKeyName[nKeyName] = '\0';
                for (nextp = old->addent; nextp != NULL; nextp = nextp->next)
                {
                    if (strcmp(pKeyName, nextp->name) != 0)
                    {
                        continue;
                    }
                    ATTR *ap = (ATTR *)atr_num(nextp->atr);
                    const char *pName = "(WARNING: Bad Attribute Number)";
                    if (ap)
                    {
                        pName = ap->name;
                    }
                    notify(player, tprintf("%s: #%d/%s", nextp->name,
                        nextp->thing, pName));
                    didit = true;
                }
            }
        }
    }
    if (!didit)
    {
        notify(player, "No added commands found in command table.");
    }
}

void do_delcommand
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    if (!*name)
    {
        notify(player, "Sorry.");
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
            notify(player, "No such attribute.");
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
    mux_strlwr(name);

    CMDENT *old, *cmd;
    ADDENT *prev = NULL, *nextp;
    size_t nName = strlen(name);
    old = (CMDENT *)hashfindLEN(name, nName, &mudstate.command_htab);

    if (  old
       && (old->callseq & CS_ADDED))
    {
        char *p__Name = tprintf("__%s", name);
        size_t n__Name = strlen(p__Name);

        if (command[0] == '\0')
        {
            // Delete all @addcommand'ed associations with the given name.
            //
            for (prev = old->addent; prev != NULL; prev = nextp)
            {
                nextp = prev->next;
                MEMFREE(prev->name);
                prev->name = NULL;
                MEMFREE(prev);
                prev = NULL;
            }
            hashdeleteLEN(name, nName, &mudstate.command_htab);
            cmd = (CMDENT *)hashfindLEN(p__Name, n__Name, &mudstate.command_htab);
            if (cmd)
            {
                hashaddLEN(cmd->cmdname, strlen(cmd->cmdname), cmd,
                    &mudstate.command_htab);
                if (strcmp(name, cmd->cmdname) != 0)
                {
                    hashaddLEN(name, nName, cmd, &mudstate.command_htab);
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
            old->cmdname = NULL;
            MEMFREE(old);
            old = NULL;
            set_prefix_cmds();
            notify(player, "Done.");
        }
        else
        {
            // Remove only the (name,thing,atr) association.
            //
            for (nextp = old->addent; nextp != NULL; nextp = nextp->next)
            {
                if (  nextp->thing == thing
                   && nextp->atr == atr)
                {
                    MEMFREE(nextp->name);
                    nextp->name = NULL;
                    if (!prev)
                    {
                        if (!nextp->next)
                        {
                            hashdeleteLEN(name, nName, &mudstate.command_htab);
                            cmd = (CMDENT *)hashfindLEN(p__Name, n__Name,
                                &mudstate.command_htab);
                            if (cmd)
                            {
                                hashaddLEN(cmd->cmdname, strlen(cmd->cmdname),
                                    cmd, &mudstate.command_htab);
                                if (strcmp(name, cmd->cmdname) != 0)
                                {
                                    hashaddLEN(name, nName, cmd,
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
                            old->cmdname = NULL;
                            MEMFREE(old);
                            old = NULL;
                        }
                        else
                        {
                            old->addent = nextp->next;
                            MEMFREE(nextp);
                            nextp = NULL;
                        }
                    }
                    else
                    {
                        prev->next = nextp->next;
                        MEMFREE(nextp);
                        nextp = NULL;
                    }
                    set_prefix_cmds();
                    notify(player, "Done.");
                    return;
                }
                prev = nextp;
            }
            notify(player, "Command not found in command table.");
        }
    }
    else
    {
        notify(player, "Command not found in command table.");
    }
}

/*
 * @prog 'glues' a user's input to a command. Once executed, the first string
 * input from any of the doers's logged in descriptors, will go into
 * A_PROGMSG, which can be substituted in <command> with %0. Commands already
 * queued by the doer will be processed normally.
 */

void handle_prog(DESC *d, char *message)
{

    // Allow the player to pipe a command while in interactive mode.
    //
    if (*message == '|')
    {
        do_command(d, message + 1);

        // Use telnet protocol's GOAHEAD command to show prompt
        //
        if (d->program_data != NULL)
        {
            queue_string(d, tprintf("%s>%s \377\371", ANSI_HILITE, ANSI_NORMAL));
        }
        return;
    }
    dbref aowner;
    int aflags, i;
    char *cmd = atr_get(d->player, A_PROGCMD, &aowner, &aflags);
    CLinearTimeAbsolute lta;
    wait_que(d->program_data->wait_enactor, d->player, d->player, false, lta,
        NOTHING, 0, cmd, (char **)&message, 1,
        (char **)d->program_data->wait_regs);

    // First, set 'all' to a descriptor we find for this player.
    //
    DESC *all = (DESC *)hashfindLEN(&(d->player), sizeof(d->player), &mudstate.desc_htab) ;

    if (all && all->program_data)
    {
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (all->program_data->wait_regs[i])
            {
                free_lbuf(all->program_data->wait_regs[i]);
                all->program_data->wait_regs[i] = NULL;
            }
        }

        MEMFREE(all->program_data);
        all->program_data = NULL;

        // Set info for all player descriptors to NULL
        //
        DESC_ITER_PLAYER(d->player, all)
            all->program_data = NULL;
    }
    atr_clr(d->player, A_PROGCMD);
    free_lbuf(cmd);
}

void do_quitprog(dbref player, dbref caller, dbref enactor, int key, char *name)
{
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
        notify(player, "That is not a player.");
        return;
    }
    if (!Connected(doer))
    {
        notify(player, "That player is not connected.");
        return;
    }
    DESC *d;
    bool isprog = false;
    DESC_ITER_PLAYER(doer, d)
    {
        if (d->program_data != NULL)
        {
            isprog = true;
        }
    }

    if (!isprog)
    {
        notify(player, "Player is not in an @program.");
        return;
    }

    d = (DESC *)hashfindLEN(&doer, sizeof(doer), &mudstate.desc_htab);
    int i;

    if (d && d->program_data)
    {
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (d->program_data->wait_regs[i])
            {
                free_lbuf(d->program_data->wait_regs[i]);
                d->program_data->wait_regs[i] = NULL;
            }
        }
        MEMFREE(d->program_data);
        d->program_data = NULL;

        // Set info for all player descriptors to NULL.
        //
        DESC_ITER_PLAYER(doer, d)
        {
            d->program_data = NULL;
        }
    }

    atr_clr(doer, A_PROGCMD);
    notify(player, "@program cleared.");
    notify(doer, "Your @program has been terminated.");
}

void do_prog
(
    dbref player,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    if (  !name
       || !*name)
    {
        notify(player, "No players specified.");
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
        notify(player, "That is not a player.");
        return;
    }
    if (!Connected(doer))
    {
        notify(player, "That player is not connected.");
        return;
    }
    char *msg = command;
    char *attrib = parse_to(&msg, ':', 1);

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
        char *pBuffer = NULL;
        bool bFound = false;
        ITER_PARENTS(thing, parent, lev)
        {
            pBuffer = atr_get(parent, ap->number, &aowner, &aflags);
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
            notify(player, "Attribute not present on object.");
            return;
        }
    }
    else
    {
        notify(player, "No such attribute.");
        return;
    }

    // Check to see if the enactor already has an @prog input pending.
    //
    DESC *d;
    DESC_ITER_PLAYER(doer, d)
    {
        if (d->program_data != NULL)
        {
            notify(player, "Input already pending.");
            return;
        }
    }

    PROG *program = (PROG *)MEMALLOC(sizeof(PROG));
    ISOUTOFMEMORY(program);
    program->wait_enactor = player;
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        program->wait_regs[i] = alloc_lbuf("prog_regs");
        memcpy(program->wait_regs[i], mudstate.global_regs[i], mudstate.glob_reg_len[i]+1);
    }

    // Now, start waiting.
    //
    DESC_ITER_PLAYER(doer, d)
    {
        d->program_data = program;

        // Use telnet protocol's GOAHEAD command to show prompt.
        //
        queue_string(d, tprintf("%s>%s \377\371", ANSI_HILITE, ANSI_NORMAL));
    }
}

/* ---------------------------------------------------------------------------
 * do_restart: Restarts the game.
 */
void do_restart(dbref executor, dbref caller, dbref enactor, int key)
{
    if (!Can_SiteAdmin(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    bool bDenied = false;
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(executor, "Dumping. Please try again later.");
        bDenied = true;
    }
#endif // !WIN32

#if BT_ENABLED
    ResetSpecialObjects();
#endif

    if (!mudstate.bCanRestart)
    {
        notify(executor, "Server just started. Please try again in a few seconds.");
        bDenied = true;
    }
    if (bDenied)
    {
        STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
        log_text("Restart requested but not executed by ");
        log_name(executor);
        ENDLOG;
        return;
    }

    raw_broadcast(0, "GAME: Restart by %s, please wait.", Name(Owner(executor)));
    STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
    log_text("Restart by ");
    log_name(executor);
    ENDLOG;

#ifndef MEMORY_BASED
    al_store();
#endif
    pcache_sync();
    dump_database_internal(DUMP_I_RESTART);
    SYNC;
    CLOSE;

#ifdef WIN32 // WIN32

    WSACleanup();
    exit(12345678);

#else // WIN32

    dump_restart_db();

    CleanUpSlaveSocket();
    CleanUpSlaveProcess();

    Log.StopLogging();

#ifdef GAME_DOOFERMUX
    execl("bin/netmux", mudconf.mud_name, "-c", mudconf.config_file, NULL);
#else
    execl("bin/netmux", "netmux", "-c", mudconf.config_file, NULL);
#endif // GAME_DOOFERMUX
#endif // !WIN32
}

/* ---------------------------------------------------------------------------
 * do_backup: Backs up and restarts the game
 * By Wadhah Al-Tailji (7-21-97), altailji@nmt.edu
 * Ported to MUX2 by Patrick Hill (7-5-2001), hellspawn@anomux.org
 */

#ifdef WIN32

void do_backup(dbref player, dbref caller, dbref enactor, int key)
{
    notify(player, "This feature is not yet available on Win32-hosted MUX.");
}

#else // WIN32

void do_backup(dbref player, dbref caller, dbref enactor, int key)
{
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(player, "Dumping. Please try again later.");
    }
#endif // !WIN32

    raw_broadcast(0, "GAME: Backing up database. Please wait.");
    STARTLOG(LOG_ALWAYS, "WIZ", "BACK");
    log_text("Backup by ");
    log_name(player);
    ENDLOG;

#ifdef MEMORY_BASED
    // Invoking _backupflat.sh with an argument prompts the backup script
    // to use it as the flatfile.
    //
    dump_database_internal(DUMP_I_FLAT);
    system(tprintf("./_backupflat.sh %s.FLAT 1>&2", mudconf.indb));
#else // MEMORY_BASED
    // Invoking _backupflat.sh without an argument prompts the backup script
    // to use dbconvert itself.
    //
    dump_database_internal(DUMP_I_NORMAL);
    system(tprintf("./_backupflat.sh 1>&2"));
#endif // MEMORY_BASED
    raw_broadcast(0, "GAME: Backup finished.");
}
#endif // WIN32

/* ---------------------------------------------------------------------------
 * do_comment: Implement the @@ (comment) command. Very cpu-intensive :-)
 */

void do_comment(dbref player, dbref caller, dbref enactor, int key)
{
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

dbref match_possessed(dbref player, dbref thing, char *target, dbref dflt, bool check_enter)
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
    char *buff, *place, *s1, *d1, *temp;
    char *start = target;
    while (*target)
    {
        // Fail if no ' characters.
        //
        place = target;
        target = strchr(place, '\'');
        if (  target == NULL
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

void parse_range(char **name, dbref *low_bound, dbref *high_bound)
{
    char *buff1 = *name;
    if (buff1 && *buff1)
    {
        *name = parse_to(&buff1, ',', EV_STRIP_TS);
    }
    if (buff1 && *buff1)
    {
        char *buff2 = parse_to(&buff1, ',', EV_STRIP_TS);
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

bool parse_thing_slash(dbref player, char *thing, char **after, dbref *it)
{
    // Get name up to '/'.
    //
    char *str = thing;
    while (  *str != '\0'
          && *str != '/')
    {
        str++;
    }

    // If no '/' in string, return failure.
    //
    if (*str == '\0')
    {
        *after = NULL;
        *it = NOTHING;
        return false;
    }
    *str++ = '\0';
    *after = str;

    // Look for the object.
    //
    init_match(player, thing, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    *it = match_result();

    // Return status of search.
    //
    return Good_obj(*it);
}

extern NAMETAB lock_sw[];

bool get_obj_and_lock(dbref player, char *what, dbref *it, ATTR **attr, char *errmsg, char **bufc)
{
    char *str, *tbuf;
    int anum;

    tbuf = alloc_lbuf("get_obj_and_lock");
    strcpy(tbuf, what);
    if (parse_thing_slash(player, tbuf, &str, it))
    {
        // <obj>/<lock> syntax, use the named lock.
        //
        if (!search_nametab(player, lock_sw, str, &anum))
        {
            free_lbuf(tbuf);
            safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
            return false;
        }
    }
    else
    {
        // Not <obj>/<lock>, do a normal get of the default lock.
        //
        *it = match_thing_quiet(player, what);
        if (!Good_obj(*it))
        {
            free_lbuf(tbuf);
            safe_match_result(*it, errmsg, bufc);
            return false;
        }
        anum = A_LOCK;
    }

    // Get the attribute definition, fail if not found.
    //
    free_lbuf(tbuf);
    *attr = atr_num(anum);
    if (!(*attr))
    {
        safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
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

    int mAllow = AF_VISUAL;
    if (  (tattr->flags & mAllow)
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
        if (  (tattr->flags & mDeny)
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
    if (  (tattr->flags & mDeny)
       || (  atr_get_info(target, tattr->number, &aowner, &aflags)
          && (aflags & mDeny)))
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
    if (  (tattr->flags & mDeny)
       || !atr_get_info(target, tattr->number, &aowner, &aflags)
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
    // No sense if trying to locate a bad object
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
             || loc_it == where_is(player)))
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
       || Find_Unfindable(player)
       || (  Findable(it)
          && findable_room))
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

void did_it(dbref player, dbref thing, int what, const char *def, int owhat,
            const char *odef, int awhat, char *args[], int nargs)
{
    if (MuxAlarm.bAlarmed)
    {
        return;
    }

    char *d, *buff, *act, *charges, *bp, *str;
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
    char **preserve = NULL;
    int *preserve_len = NULL;

    // message to player.
    //
    if (what > 0)
    {
        d = atr_pget(thing, what, &aowner, &aflags);
        if (*d)
        {
            need_pres = true;
            preserve = PushPointers(MAX_GLOBAL_REGS);
            preserve_len = PushIntegers(MAX_GLOBAL_REGS);
            save_global_regs("did_it_save", preserve, preserve_len);
            buff = bp = alloc_lbuf("did_it.1");
            str = d;
            mux_exec(buff, &bp, thing, player, player,
                EV_EVAL | EV_FIGNORE | EV_FCHECK | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if (  (aflags & AF_HTML)
               && Html(player))
            {
                safe_str("\r\n", buff, &bp);
                *bp = '\0';
                notify_html(player, buff);
            }
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
    if (what < 0 && def)
    {
        notify(player, def);
    }

    // message to neighbors.
    //
    if (  owhat > 0
       && Has_location(player)
       && Good_obj(loc = Location(player)))
    {
        d = atr_pget(thing, owhat, &aowner, &aflags);
        if (*d)
        {
            if (!need_pres)
            {
                need_pres = true;
                preserve = PushPointers(MAX_GLOBAL_REGS);
                preserve_len = PushIntegers(MAX_GLOBAL_REGS);
                save_global_regs("did_it_save", preserve, preserve_len);
            }
            buff = bp = alloc_lbuf("did_it.2");
            str = d;
            mux_exec(buff, &bp, thing, player, player,
                     EV_EVAL | EV_FIGNORE | EV_FCHECK | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if (*buff)
            {
                notify_except2(loc, player, player, thing, tprintf("%s %s", Name(player), buff));
            }
            free_lbuf(buff);
        }
        else if (odef)
        {
            notify_except2(loc, player, player, thing, tprintf("%s %s", Name(player), odef));
        }
        free_lbuf(d);
    }
    if (  owhat < 0
       && odef
       && Has_location(player)
       && Good_obj(loc = Location(player)))
    {
        notify_except2(loc, player, player, thing, tprintf("%s %s", Name(player), odef));
    }

    // If we preserved the state of the global registers, restore them.
    //
    if (need_pres)
    {
        restore_global_regs("did_it_restore", preserve, preserve_len);
        PopIntegers(preserve_len, MAX_GLOBAL_REGS);
        PopPointers(preserve, MAX_GLOBAL_REGS);
    }

    // do the action attribute.
    //
    if (awhat > 0)
    {
        if (*(act = atr_pget(thing, awhat, &aowner, &aflags)))
        {
            charges = atr_pget(thing, A_CHARGES, &aowner, &aflags);
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
                else if (*(buff = atr_pget(thing, A_RUNOUT, &aowner, &aflags)))
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
            wait_que(thing, player, player, false, lta, NOTHING, 0, act,
                args, nargs, mudstate.global_regs);
        }
        free_lbuf(act);
    }
}

/* ---------------------------------------------------------------------------
 * do_verb: Command interface to did_it.
 */

void do_verb(dbref executor, dbref caller, dbref enactor, int key,
             char *victim_str, char *args[], int nargs)
{
    // Look for the victim.
    //
    if (  !victim_str
       || !*victim_str)
    {
        notify(executor, "Nothing to do.");
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
        notify_quiet(executor, "Permission denied,");
        return;
    }

    ATTR *ap;
    int what = -1;
    int owhat = -1;
    int awhat = -1;
    const char *whatd = NULL;
    const char *owhatd = NULL;
    int nxargs = 0;
    dbref aowner = NOTHING;
    int aflags = NOTHING;
    char *xargs[10];

    switch (nargs) // Yes, this IS supposed to fall through.
    {
    case 7:
        // Get arguments.
        //
        parse_arglist(victim, actor, actor, args[6], '\0',
            EV_STRIP_LS | EV_STRIP_TS, xargs, 10, (char **)NULL, 0, &nxargs);

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
        ap = NULL;
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

        ap = NULL;
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
    did_it(actor, victim, what, whatd, owhat, owhatd, awhat, xargs, nxargs);

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
void OutOfMemory(const char *SourceFile, unsigned int LineNo)
{
    Log.tinyprintf("%s(%u): Out of memory." ENDLINE, SourceFile, LineNo);
    Log.Flush();
    if (  !mudstate.bStandAlone
       && mudstate.bCanRestart)
    {
        do_restart(GOD, GOD, GOD, 0);
    }
    else
    {
        abort();
    }
}

// --------------------------------------------------------------------------
// AssertionFailed: A logical assertion has failed.
//
bool AssertionFailed(const char *SourceFile, unsigned int LineNo)
{
    Log.tinyprintf("%s(%u): Assertion failed." ENDLINE, SourceFile, LineNo);
    Log.Flush();
    if (  !mudstate.bStandAlone
       && mudstate.bCanRestart)
    {
        do_restart(GOD, GOD, GOD, 0);
    }
    else
    {
        abort();
    }
    return false;
}
