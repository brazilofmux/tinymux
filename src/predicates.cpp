// predicates.cpp
//
// $Id: predicates.cpp,v 1.41 2001-11-20 04:54:40 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <signal.h>

#include "mudconf.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "command.h"
#include "alloc.h"
#include "attrs.h"
#include "powers.h"
#include "ansi.h"
#include "htab.h"

extern int FDECL(do_command, (DESC *, char *, int));
extern void NDECL(dump_database);

char * DCL_CDECL tprintf(const char *fmt,...)
{
    static char buff[LBUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    Tiny_vsnprintf(buff, LBUF_SIZE, fmt, ap);
    va_end(ap);
    return buff;
}

void DCL_CDECL safe_tprintf_str(char *str, char **bp, const char *fmt,...)
{
    va_list ap;
    va_start(ap, fmt);
    int nAvailable = LBUF_SIZE - (*bp - str);
    int len = Tiny_vsnprintf(*bp, nAvailable, fmt, ap);
    va_end(ap);
    *bp += len;
}

/*
 * ---------------------------------------------------------------------------
 * * insert_first, remove_first: Insert or remove objects from lists.
 */

dbref insert_first(dbref head, dbref thing)
{
    s_Next(thing, head);
    return thing;
}

dbref remove_first(dbref head, dbref thing)
{
    dbref prev;

    if (head == thing)
        return (Next(thing));

    DOLIST(prev, head) {
        if (Next(prev) == thing) {
            s_Next(prev, Next(thing));
            return head;
        }
    }
    return head;
}

/*
 * ---------------------------------------------------------------------------
 * * reverse_list: Reverse the order of members in a list.
 */

dbref reverse_list(dbref list)
{
    dbref newlist, rest;

    newlist = NOTHING;
    while (list != NOTHING) {
        rest = Next(list);
        s_Next(list, newlist);
        newlist = list;
        list = rest;
    }
    return newlist;
}

/*
 * ---------------------------------------------------------------------------
 * * member - indicate if thing is in list
 */

int member(dbref thing, dbref list)
{
    DOLIST(list, list) {
        if (list == thing)
            return 1;
    }
    return 0;
}

#ifndef STANDALONE

int could_doit(dbref player, dbref thing, int locknum)
{
    // If nonplayer tries to get key, then no.
    //
    if (!isPlayer(player) && Key(thing))
    {
        return 0;
    }
    if (Pass_Locks(player))
    {
        return 1;
    }

    dbref aowner;
    int   aflags;
    char *key = atr_get(thing, locknum, &aowner, &aflags);
    int doit = eval_boolexp_atr(player, thing, thing, key);
    free_lbuf(key);
    return doit;
}

int can_see(dbref player, dbref thing, int can_see_loc)
{
    // Don't show if all the following apply: Sleeping players should not be
    // seen.  The thing is a disconnected player.  The player is not a
    // puppet.
    //
    if (mudconf.dark_sleepers && isPlayer(thing) &&
        !Connected(thing) && !Puppet(thing))
    {
        return 0;
    }

    /*
     * You don't see yourself or exits
     */
    if ((player == thing) || isExit(thing))
    {
        return 0;
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

static int pay_quota(dbref who, int cost)
{
    dbref aowner;
    int quota, aflags;
    char buf[20], *quota_str;

    /*
     * If no cost, succeed
     */

    if (cost <= 0)
        return 1;

    /*
     * determine quota
     */

    quota = Tiny_atol(quota_str = atr_get(Owner(who), A_RQUOTA, &aowner, &aflags));
    free_lbuf(quota_str);

    /*
     * enough to build?  Wizards always have enough.
     */

    quota -= cost;
    if ((quota < 0) && !Free_Quota(who) && !Free_Quota(Owner(who)))
        return 0;

    // Dock the quota.
    //
    Tiny_ltoa(quota, buf);
    atr_add_raw(Owner(who), A_RQUOTA, buf);

    return 1;
}

int canpayfees(dbref player, dbref who, int pennies, int quota)
{
    if (!Wizard(who) && !Wizard(Owner(who)) &&
        !Free_Money(who) && !Free_Money(Owner(who)) &&
        (Pennies(Owner(who)) < pennies)) {
        if (player == who) {
            notify(player,
                   tprintf("Sorry, you don't have enough %s.",
                       mudconf.many_coins));
        } else {
            notify(player,
            tprintf("Sorry, that player doesn't have enough %s.",
                mudconf.many_coins));
        }
        return 0;
    }
    if (mudconf.quotas) {
        if (!pay_quota(who, quota)) {
            if (player == who) {
                notify(player,
                       "Sorry, your building contract has run out.");
            } else {
                notify(player,
                       "Sorry, that player's building contract has run out.");
            }
            return 0;
        }
    }
    payfor(who, pennies);
    return 1;
}

int payfor(dbref who, int cost)
{
    dbref tmp;

    if (Wizard(who) || Wizard(Owner(who)) ||
        Free_Money(who) || Free_Money(Owner(who)) ||
        Immortal(who) || Immortal(Owner(who))) {
        return 1;
    }
    who = Owner(who);
    if ((tmp = Pennies(who)) >= cost) {
        s_Pennies(who, tmp - cost);
        return 1;
    }
    return 0;
}

#endif /*
        * STANDALONE
        */

void add_quota(dbref who, int payment)
{
    dbref aowner;
    int aflags;
    char buf[20], *quota;

    quota = atr_get(who, A_RQUOTA, &aowner, &aflags);
    Tiny_ltoa(Tiny_atol(quota) + payment, buf);
    free_lbuf(quota);
    atr_add_raw(who, A_RQUOTA, buf);
}

void giveto(dbref who, int pennies)
{
    if (Wizard(who) || Wizard(Owner(who)) ||
        Free_Money(who) || Free_Money(Owner(who)) ||
        Immortal(who) || Immortal(Owner(who))) {
        return;
    }
    who = Owner(who);
    s_Pennies(who, Pennies(who) + pennies);
}

// The following function validates that the object names (which will be
// used for things, exits, and rooms, but not for players) and generates
// a canonical form of that name (with optimized ANSI).
//
char *MakeCanonicalObjectName(const char *pName, int *pnName, BOOL *pbValid)
{
    static char Buf[MBUF_SIZE];

    *pnName = 0;
    *pbValid = FALSE;

    if (!pName)
    {
        return NULL;
    }

    // Build up what the real name would be. If we pass all the
    // checks, this is what we will return as a result.
    //
    int nVisualWidth;
    int nBuf = ANSI_TruncateToField(pName, sizeof(Buf), Buf, MBUF_SIZE, &nVisualWidth, ANSI_ENDGOAL_NORMAL);

    // Disallow pure ANSI names. There must be at least -something-
    // visible.
    //
    if (nVisualWidth <= 0)
    {
        return NULL;
    }

    // Get the stripped version (Visible parts without color info).
    //
    unsigned int nStripped;
    char *pStripped = strip_ansi(Buf, &nStripped);

    // Do not allow LOOKUP_TOKEN, NUMBER_TOKEN, NOT_TOKEN, or SPACE
    // as the first character, or SPACE as the last character
    //
    if (  strchr("*!#", *pStripped)
       || Tiny_IsSpace[(unsigned char)pStripped[0]]
       || Tiny_IsSpace[(unsigned char)pStripped[nStripped-1]])
    {
        return NULL;
    }

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    for (unsigned int i = 0; i < nStripped; i++)
    {
        if (!Tiny_IsObjectNameCharacter[(unsigned char)pStripped[i]])
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
    *pbValid = TRUE;
    return Buf;
}

// The following function validates the player name. ANSI is not
// allowed in player names. However, a player name must satisfy
// the requirements of a regular name as well.
//
BOOL ValidatePlayerName(const char *pName)
{
    if (!pName)
    {
        return FALSE;
    }
    unsigned int nName = strlen(pName);

    // Verify that name is not empty, but not too long, either.
    //
    if (nName <= 0 || PLAYER_NAME_LIMIT <= nName)
    {
        return FALSE;
    }

    // Do not allow LOOKUP_TOKEN, NUMBER_TOKEN, NOT_TOKEN, or SPACE
    // as the first character, or SPACE as the last character
    //
    if (  strchr("*!#", *pName)
       || Tiny_IsSpace[(unsigned char)pName[0]]
       || Tiny_IsSpace[(unsigned char)pName[nName-1]])
    {
        return FALSE;
    }

#ifndef STANDALONE
    if (mudconf.name_spaces)
    {
        Tiny_IsPlayerNameCharacter[(unsigned char)' '] = 1;
    }
    else
    {
        Tiny_IsPlayerNameCharacter[(unsigned char)' '] = 0;
    }
#endif

    // Only printable characters besides ARG_DELIMITER, AND_TOKEN,
    // and OR_TOKEN are allowed.
    //
    for (unsigned int i = 0; i < nName; i++)
    {
        if (!Tiny_IsObjectNameCharacter[(unsigned char)pName[i]])
        {
            return FALSE;
        }
    }

    // Special names are specifically dis-allowed.
    //
    if (  (nName == 2 && memcmp("me", pName, 2) == 0)
       || (nName == 4 && (  memcmp("home", pName, 4) == 0
                         || memcmp("here", pName, 4) == 0)))
    {
        return FALSE;
    }

    return TRUE;
}

int ok_password(const char *password, dbref player)
{
    const char *scan;
    int num_upper = 0;
    int num_special = 0;
    int num_lower = 0;

    if (*password == '\0')
    {
#ifndef STANDALONE
        notify_quiet(player, "Null passwords are not allowed.");
#endif
        return 0;
    }

    for (scan = password; *scan; scan++)
    {
        if (  !Tiny_IsPrint[(unsigned char)*scan]
           || Tiny_IsSpace[(unsigned char)*scan])
        {
#ifndef STANDALONE
            notify_quiet(player, "Illegal character in password.");
#endif
            return 0;
        }
        if (Tiny_IsUpper[(unsigned char)*scan])
            num_upper++;
        else if (Tiny_IsLower[(unsigned char)*scan])
            num_lower++;
        else if ((*scan != '\'') && (*scan != '-'))
            num_special++;
    }

    // Needed.  Change it if you like, but be sure yours is the same.
    //
    if ((strlen(password) == 13) &&
        (password[0] == 'X') &&
        (password[1] == 'X'))
    {
#ifndef STANDALONE
        notify_quiet(player, "Please choose another password.");
#endif
        return 0;
    }

#ifndef STANDALONE
    if (mudconf.safer_passwords)
    {
        if (num_upper < 1)
        {
            notify_quiet(player, "The password must contain at least one capital letter.");
            return 0;
        }
        if (num_lower < 1)
        {
            notify_quiet(player, "The password must contain at least one lowercase letter.");
            return 0;
        }
        if (num_special < 1)
        {
            notify_quiet(player, "The password must contain at least one number or a symbol other than the apostrophe or dash.");
            return 0;
        }
    }
#endif // STANDALONE

    return 1;
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * handle_ears: Generate the 'grows ears' and 'loses ears' messages.
 */

void handle_ears(dbref thing, int could_hear, int can_hear)
{
    char *buff, *bp;
    int gender;
    static const char *poss[5] =
    {"", "its", "her", "his", "their"};

    if (!could_hear && can_hear) {
        buff = alloc_lbuf("handle_ears.grow");
        StringCopy(buff, Name(thing));
        if (isExit(thing)) {
            for (bp = buff; *bp && (*bp != ';'); bp++) ;
            *bp = '\0';
        }
        gender = get_gender(thing);
        notify_check(thing, thing,
                 tprintf("%s grow%s ears and can now hear.",
                     buff, (gender == 4) ? "" : "s"),
                 (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
        free_lbuf(buff);
    } else if (could_hear && !can_hear) {
        buff = alloc_lbuf("handle_ears.lose");
        StringCopy(buff, Name(thing));
        if (isExit(thing)) {
            for (bp = buff; *bp && (*bp != ';'); bp++) ;
            *bp = '\0';
        }
        gender = get_gender(thing);
        notify_check(thing, thing,
                 tprintf("%s lose%s %s ears and become%s deaf.",
                     buff, (gender == 4) ? "" : "s",
                     poss[gender], (gender == 4) ? "" : "s"),
                 (MSG_ME | MSG_NBR | MSG_LOC | MSG_INV));
        free_lbuf(buff);
    }
}

// For lack of better place the @switch code is here.
//
void do_switch
(
    dbref player,
    dbref cause,
    int   key,
    char *expr,
    char *args[],
    int   nargs,
    char *cargs[],
    int   ncargs
)
{
    int a, any;
    char *buff, *bp, *str;

    if (!expr || (nargs <= 0))
    {
        return;
    }

    if (key == SWITCH_DEFAULT)
    {
        if (mudconf.switch_df_all)
            key = SWITCH_ANY;
        else
            key = SWITCH_ONE;
    }

    // Now try a wild card match of buff with stuff in coms.
    //
    any = 0;
    buff = bp = alloc_lbuf("do_switch");
    CLinearTimeAbsolute lta;
    for (a = 0; (a < (nargs - 1)) && args[a] && args[a + 1]; a += 2)
    {
        bp = buff;
        str = args[a];
        TinyExec(buff, &bp, 0, player, cause, EV_FCHECK | EV_EVAL | EV_TOP,
            &str, cargs, ncargs);
        *bp = '\0';
        if (wild_match(buff, expr))
        {
            wait_que(player, cause, FALSE, lta, NOTHING, 0,
                args[a+1], cargs, ncargs, mudstate.global_regs);
            if (key == SWITCH_ONE)
            {
                free_lbuf(buff);
                return;
            }
            any = 1;
        }
    }
    free_lbuf(buff);
    if ((a < nargs) && !any && args[a])
    {
        wait_que(player, cause, FALSE, lta, NOTHING, 0, args[a],
            cargs, ncargs, mudstate.global_regs);
    }
}

void do_addcommand
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    CMDENT *old, *cmd;
    ADDENT *add, *nextp;

    dbref thing;
    int atr;

    if (  nargs != 2
       || name[0] == '\0')
    {
        notify(player, "Sorry.");
        return;
    }

    if (  !parse_attrib(player, command, &thing, &atr)
       || atr == NOTHING)
    {
        notify(player, "No such attribute.");
        return;
    }

    // Let's make this case insensitive...
    //
    _strlwr(name);
    old = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);

    if (old && (old->callseq & CS_ADDED))
    {
        // If it's already found in the hash table, and it's being
        // added using the same object and attribute...
        //
        for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next)
        {
            if ((nextp->thing == thing) && (nextp->atr == atr))
            {
                notify(player, tprintf("%s already added.", name));
                return;
            }
        }

        // Else tack it on to the existing entry...
        //
        add = (ADDENT *)MEMALLOC(sizeof(ADDENT));
        ISOUTOFMEMORY(add);
        add->thing = thing;
        add->atr = atr;
        add->name = StringClone(name);
        add->next = old->addent;
        old->addent = add;
    }
    else
    {
        if (old)
        {
            // Delete the old built-in and rename it __name.
            //
            hashdeleteLEN(name, strlen(name), &mudstate.command_htab);
        }

        cmd = (CMDENT *)MEMALLOC(sizeof(CMDENT));
        ISOUTOFMEMORY(cmd);
        cmd->cmdname = StringClone(name);
        cmd->switches = NULL;
        cmd->perms = 0;
        cmd->extra = 0;
        if (old && (old->callseq & CS_LEADIN))
        {
            cmd->callseq = CS_ADDED|CS_ONE_ARG|CS_LEADIN;
        }
        else
        {
            cmd->callseq = CS_ADDED|CS_ONE_ARG;
        }
        add = (ADDENT *)MEMALLOC(sizeof(ADDENT));
        ISOUTOFMEMORY(add);
        add->thing = thing;
        add->atr = atr;
        add->name = StringClone(name);
        add->next = NULL;
        cmd->addent = add;

        hashaddLEN(name, strlen(name), (int *)cmd, &mudstate.command_htab);

        if (old)
        {
            // Fix any aliases of this command.
            //
            hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
            char *p = tprintf("__%s", name);
            hashaddLEN(p, strlen(p), (int *)old, &mudstate.command_htab);
        }
    }

    // We reset the one letter commands here so you can overload them.
    //
    set_prefix_cmds();
    notify(player, tprintf("%s added.", name));
}

void do_listcommands(dbref player, dbref cause, int key, char *name)
{
    CMDENT *old;
    ADDENT *nextp;
    int didit = 0;

    char *keyname;

    // Let's make this case insensitive...
    //
    _strlwr(name);

    if (*name)
    {
        old = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);

        if (old && (old->callseq & CS_ADDED))
        {
            // If it's already found in the hash table, and it's being added
            // using the same object and attribute...
            //
            for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next)
            {
                notify(player, tprintf("%s: #%d/%s", nextp->name, nextp->thing, ((ATTR *)atr_num(nextp->atr))->name));
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
        int nKeyLength;
        for (keyname = hash_firstkey(&mudstate.command_htab, &nKeyLength); keyname != NULL;
             keyname = hash_nextkey(&mudstate.command_htab, &nKeyLength))
        {

            old = (CMDENT *)hashfindLEN(keyname, nKeyLength, &mudstate.command_htab);

            if (old && (old->callseq & CS_ADDED)) {

                for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next) {
                    if (strncmp(keyname, nextp->name, nKeyLength))
                        continue;
                    notify(player, tprintf("%s: #%d/%s", nextp->name, nextp->thing, ((ATTR *)atr_num(nextp->atr))->name));
                    didit = 1;
                }
            }
        }
    }
    if (!didit)
        notify(player, "No added commands found in command table.");
}

void do_delcommand
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    CMDENT *old, *cmd;
    ADDENT *prev = NULL, *nextp;

    dbref thing = NOTHING;
    int atr = NOTHING;

    if (!*name)
    {
        notify(player, "Sorry.");
        return;
    }

    if (*command)
    {
        if (!parse_attrib(player, command, &thing, &atr) || (atr == NOTHING))
        {
            notify(player, "No such attribute.");
            return;
        }
    }

    // Let's make this case insensitive...
    //
    _strlwr(name);

    old = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);

    if (old && (old->callseq & CS_ADDED))
    {
        char *p__Name = tprintf("__%s", name);
        unsigned int n__Name = strlen(p__Name);
        unsigned int nName = strlen(name);

        if (!*command)
        {
            for (prev = (ADDENT *)old->handler; prev != NULL; prev = nextp)
            {
                nextp = prev->next;
                /* Delete it! */
                MEMFREE(prev->name);
                prev->name = NULL;
                MEMFREE(prev);
                prev = NULL;
            }
            hashdeleteLEN(name, nName, &mudstate.command_htab);
            if ((cmd = (CMDENT *)hashfindLEN(p__Name, n__Name, &mudstate.command_htab)) != NULL)
            {
                hashdeleteLEN(p__Name, n__Name, &mudstate.command_htab);
                hashaddLEN(name, nName, (int *)cmd, &mudstate.command_htab);
                hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
            }
            MEMFREE(old);
            old = NULL;
            set_prefix_cmds();
            notify(player, "Done.");
            return;
        }
        else
        {
            for (nextp = (ADDENT *)old->handler; nextp != NULL; nextp = nextp->next)
            {
                if ((nextp->thing == thing) && (nextp->atr == atr))
                {
                    /* Delete it! */
                    MEMFREE(nextp->name);
                    nextp->name = NULL;
                    if (!prev)
                    {
                        if (!nextp->next)
                        {
                            hashdeleteLEN(name, nName, &mudstate.command_htab);
                            if ((cmd = (CMDENT *)hashfindLEN(p__Name, n__Name, &mudstate.command_htab)) != NULL)
                            {
                                hashdeleteLEN(p__Name, n__Name, &mudstate.command_htab);
                                hashaddLEN(name, nName, (int *)cmd, &mudstate.command_htab);
                                hashreplall((int *)old, (int *)cmd, &mudstate.command_htab);
                            }
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
    } else {
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
    DESC *all;
    char *cmd;
    dbref aowner;
    int aflags, i;

    // Allow the player to pipe a command while in interactive mode.
    //
    if (*message == '|')
    {
        do_command(d, message + 1, 1);

        // Use telnet protocol's GOAHEAD command to show prompt
        //
        if (d->program_data != NULL)
        {
            queue_string(d, tprintf("%s>%s \377\371", ANSI_HILITE, ANSI_NORMAL));
        }
        return;
    }
    cmd = atr_get(d->player, A_PROGCMD, &aowner, &aflags);
    CLinearTimeAbsolute lta;
    wait_que(d->program_data->wait_cause, d->player, FALSE, lta,
        NOTHING, 0, cmd, (char **)&message, 1,
        (char **)d->program_data->wait_regs);

    // First, set 'all' to a descriptor we find for this player.
    //
    all = (DESC *)hashfindLEN(&(d->player), sizeof(d->player), &mudstate.desc_htab) ;

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

void do_quitprog(dbref player, dbref cause, int key, char *name)
{
    DESC *d;
    dbref doer;
    int i, isprog = 0;

    if (*name) {
        doer = match_thing(player, name);
    } else {
        doer = player;
    }

    if (!(Prog(player) || Prog(Owner(player))) && (player != doer))
    {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (!isPlayer(doer) || !Good_obj(doer))
    {
        notify(player, "That is not a player.");
        return;
    }
    if (!Connected(doer))
    {
        notify(player, "That player is not connected.");
        return;
    }
    DESC_ITER_PLAYER(doer, d)
    {
        if (d->program_data != NULL)
        {
            isprog = 1;
        }
    }

    if (!isprog)
    {
        notify(player, "Player is not in an @program.");
        return;
    }

    d = (DESC *)hashfindLEN(&doer, sizeof(doer), &mudstate.desc_htab) ;

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

        /* Set info for all player descriptors to NULL */

        DESC_ITER_PLAYER(doer, d)
            d->program_data = NULL;
    }

    atr_clr(doer, A_PROGCMD);
    notify(player, "@program cleared.");
    notify(doer, "Your @program has been terminated.");
}

void do_prog
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *name,
    char *command
)
{
    DESC *d;
    PROG *program;
    int i, atr, aflags;
    dbref doer, thing, aowner;
    ATTR *ap;
    char *attrib, *msg;

    if (!name || !*name) {
        notify(player, "No players specified.");
        return;
    }
    doer = match_thing(player, name);

    if (!(Prog(player) || Prog(Owner(player))) && (player != doer)) {
        notify(player, NOPERM_MESSAGE);
        return;
    }
    if (!isPlayer(doer) || !Good_obj(doer)) {
        notify(player, "That is not a player.");
        return;
    }
    if (!Connected(doer)) {
        notify(player, "That player is not connected.");
        return;
    }
    msg = command;
    attrib = parse_to(&msg, ':', 1);

    if (msg && *msg)
    {
        notify(doer, msg);
    }
    parse_attrib(player, attrib, &thing, &atr);
    if (atr != NOTHING)
    {
        char *pBuffer = atr_get(thing, atr, &aowner, &aflags);
        if (*pBuffer)
        {
            ap = atr_num(atr);
            if (  God(player)
               || (  !God(thing)
                  && See_attr(player, thing, ap, aowner, aflags)
                  && (Wizard(player) || (aowner == Owner(player)))))
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

    // Check to see if the cause already has an @prog input pending.
    //
    DESC_ITER_PLAYER(doer, d)
    {
        if (d->program_data != NULL)
        {
            notify(player, "Input already pending.");
            return;
        }
    }

    program = (PROG *)MEMALLOC(sizeof(PROG));
    ISOUTOFMEMORY(program);
    program->wait_cause = player;
    for (i = 0; i < MAX_GLOBAL_REGS; i++)
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

/*
 * ---------------------------------------------------------------------------
 * * do_restart: Restarts the game.
 */
void do_restart(dbref player, dbref cause, int key)
{
    BOOL bDenied = FALSE;
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(player, "Dumping. Please try again later.");
        bDenied = TRUE;
    }
#endif // !WIN32
    if (!mudstate.bCanRestart)
    {
        notify(player, "Server just started. Please try again in a few seconds.");
        bDenied = TRUE;
    }
    if (bDenied)
    {
        STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
        log_text("Restart requested but not executed by ");
        log_name(player);
        ENDLOG;
        return;
    }

    raw_broadcast(0, "Game: Restart by %s, please wait.", Name(Owner(player)));
    STARTLOG(LOG_ALWAYS, "WIZ", "RSTRT");
    log_text("Restart by ");
    log_name(player);
    ENDLOG;

    dump_database_internal(DUMP_I_RESTART);
    SYNC;
    CLOSE;

#ifdef WIN32 // WIN32

    WSACleanup();
    exit(12345678);

#else // WIN32

extern int slave_pid;
extern SOCKET slave_socket;
    shutdown(slave_socket, SD_BOTH);
    close(slave_socket);
    slave_socket = INVALID_SOCKET;
    if (slave_pid > 0)
    {
        kill(slave_pid, SIGKILL);
    }
    slave_pid = 0;
    dump_restart_db();
#ifdef GAME_DOOFERMUX
    execl("bin/netmux", mudconf.mud_name, mudconf.config_file, NULL);
#else
    execl("bin/netmux", "netmux", mudconf.config_file, NULL);
#endif // GAME_DOOFERMUX
#endif // !WIN32
}

/*
 * ---------------------------------------------------------------------------
 * * do_backup: Backs up and restarts the game
 * * By Wadhah Al-Tailji (7-21-97), altailji@nmt.edu
 * * Ported to MUX2 by Patrick Hill (7-5-2001), hellspawn@anomux.org
 */

#ifdef WIN32

void do_backup(dbref player, int cause, int key)
{
    notify(player, "This feature is not yet available on Win32-hosted MUX.");
}

#else // WIN32

void do_backup(dbref player, int cause, int key)
{
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(player, "Dumping. Please try again later.");
    }
#endif // !WIN32

    raw_broadcast(0, "GAME: Backing up database. Please wait.");
    STARTLOG(LOG_ALWAYS, "WIZ", "BACK");
    log_text((char *)"Backup by ");
    log_name(player);
    ENDLOG;

    dump_database_internal(DUMP_I_FLAT);
    system(tprintf("./_backupflat.sh %s.FLAT 1>&2", mudconf.outdb));
    raw_broadcast(0, "GAME: Backup finished.");
}
#endif // WIN32

/*
 * ---------------------------------------------------------------------------
 * * do_comment: Implement the @@ (comment) command. Very cpu-intensive :-)
 */

void do_comment(dbref player, dbref cause, int key)
{
}

static dbref promote_dflt(dbref old, dbref new0)
{
    switch (new0) {
    case NOPERM:
        return NOPERM;
    case AMBIGUOUS:
        if (old == NOPERM)
            return old;
        else
            return new0;
    }

    if ((old == NOPERM) || (old == AMBIGUOUS))
        return old;

    return NOTHING;
}

dbref match_possessed(dbref player, dbref thing, char *target, dbref dflt, int check_enter)
{
    dbref result, result1;
    int control;
    char *buff, *start, *place, *s1, *d1, *temp;

    // First, check normally.
    //
    if (Good_obj(dflt))
        return dflt;

    // Didn't find it directly.  Recursively do a contents check.
    //
    start = target;
    while (*target)
    {
        // Fail if no ' characters.
        //
        place = target;
        target = (char *)strchr(place, '\'');
        if ((target == NULL) || !*target)
            return dflt;

        /*
         * If string started with a ', skip past it
         */

        if (place == target) {
            target++;
            continue;
        }
        /*
         * If next character is not an s or a space, skip past
         */

        temp = target++;
        if (!*target)
            return dflt;
        if ((*target != 's') && (*target != 'S') && (*target != ' '))
            continue;

        // If character was not a space make sure the following character is
        // a space.
        //
        if (*target != ' ')
        {
            target++;
            if (!*target)
                return dflt;
            if (*target != ' ')
                continue;
        }

        // Copy the container name to a new buffer so we can terminate it.
        //
        buff = alloc_lbuf("is_posess");
        for (s1 = start, d1 = buff; *s1 && (s1 < temp); *d1++ = (*s1++)) ;
        *d1 = '\0';

        // Look for the container here and in our inventory.  Skip past if we
        // can't find it.
        //
        init_match(thing, buff, NOTYPE);
        if (player == thing) {
            match_neighbor();
            match_possession();
        } else {
            match_possession();
        }
        result1 = match_result();

        free_lbuf(buff);
        if (!Good_obj(result1)) {
            dflt = promote_dflt(dflt, result1);
            continue;
        }

        // If we don't control it and it is either dark or opaque, skip past.
        //
        control = Controls(player, result1);
        if ((Dark(result1) || Opaque(result1)) && !control) {
            dflt = promote_dflt(dflt, NOTHING);
            continue;
        }

        // Validate object has the ENTER bit set, if requested.
        //
        if ((check_enter) && !Enter_ok(result1) && !control)
        {
            dflt = promote_dflt(dflt, NOPERM);
            continue;
        }

        // Look for the object in the container.
        //
        init_match(result1, target, NOTYPE);
        match_possession();
        result = match_result();
        result = match_possessed(player, result1, target, result,
                     check_enter);
        if (Good_obj(result))
            return result;
        dflt = promote_dflt(dflt, result);
    }
    return dflt;
}

/*
 * ---------------------------------------------------------------------------
 * * parse_range: break up <what>,<low>,<high> syntax
 */

void parse_range(char **name, dbref *low_bound, dbref *high_bound)
{
    char *buff1, *buff2;

    buff1 = *name;
    if (buff1 && *buff1)
        *name = parse_to(&buff1, ',', EV_STRIP_TS);
    if (buff1 && *buff1)
    {
        buff2 = parse_to(&buff1, ',', EV_STRIP_TS);
        if (buff1 && *buff1)
        {
            while (Tiny_IsSpace[(unsigned char)*buff1])
                buff1++;

            if (*buff1 == NUMBER_TOKEN)
                buff1++;

            *high_bound = Tiny_atol(buff1);
            if (*high_bound >= mudstate.db_top)
                *high_bound = mudstate.db_top - 1;
        }
        else
        {
            *high_bound = mudstate.db_top - 1;
        }

        while (Tiny_IsSpace[(unsigned char)*buff2])
            buff2++;

        if (*buff2 == NUMBER_TOKEN)
            buff2++;

        *low_bound = Tiny_atol(buff2);
        if (*low_bound < 0)
            *low_bound = 0;
    }
    else
    {
        *low_bound = 0;
        *high_bound = mudstate.db_top - 1;
    }
}

int parse_thing_slash(dbref player, char *thing, char **after, dbref *it)
{
    char *str;

    // Get name up to '/'.
    //
    for (str = thing; *str && (*str != '/'); str++) ;

    // If no '/' in string, return failure.
    //
    if (!*str) {
        *after = NULL;
        *it = NOTHING;
        return 0;
    }
    *str++ = '\0';
    *after = str;

    /*
     * Look for the object.
     */

    init_match(player, thing, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    *it = match_result();

    /*
     * Return status of search.
     */

    return (Good_obj(*it));
}

extern NAMETAB lock_sw[];

int get_obj_and_lock(dbref player, char *what, dbref *it, ATTR **attr, char *errmsg, char **bufc)
{
    char *str, *tbuf;
    int anum;

    tbuf = alloc_lbuf("get_obj_and_lock");
    StringCopy(tbuf, what);
    if (parse_thing_slash(player, tbuf, &str, it)) {

        /*
         * <obj>/<lock> syntax, use the named lock.
         */

        anum = search_nametab(player, lock_sw, str);
        if (anum < 0) {
            free_lbuf(tbuf);
            safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
            return 0;
        }
    } else {

        /*
         * Not <obj>/<lock>, do a normal get of the default lock.
         */

        *it = match_thing(player, what);
        if (!Good_obj(*it)) {
            free_lbuf(tbuf);
            safe_str("#-1 NOT FOUND", errmsg, bufc);
            return 0;
        }
        anum = A_LOCK;
    }

    /*
     * Get the attribute definition, fail if not found.
     */

    free_lbuf(tbuf);
    *attr = atr_num(anum);
    if (!(*attr)) {
        safe_str("#-1 LOCK NOT FOUND", errmsg, bufc);
        return 0;
    }
    return 1;
}

#endif /*
        * STANDALONE
        */

/*
 * ---------------------------------------------------------------------------
 * * where_is: Returns place where obj is linked into a list.
 * * ie. location for players/things, source for exits, NOTHING for rooms.
 */

dbref where_is(dbref what)
{
    dbref loc;

    if (!Good_obj(what))
        return NOTHING;

    switch (Typeof(what)) {
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

/*
 * ---------------------------------------------------------------------------
 * * where_room: Return room containing player, or NOTHING if no room or
 * * recursion exceeded.  If player is a room, returns itself.
 */

dbref where_room(dbref what)
{
    int count;

    for (count = mudconf.ntfy_nest_lim; count > 0; count--) {
        if (!Good_obj(what))
            break;
        if (isRoom(what))
            return what;
        if (!Has_location(what))
            break;
        what = Location(what);
    }
    return NOTHING;
}

int locatable(dbref player, dbref it, dbref cause)
{
    dbref loc_it, room_it;
    int findable_room;

    /*
     * No sense if trying to locate a bad object
     */

    if (!Good_obj(it))
        return 0;

    loc_it = where_is(it);

    // Succeed if we can examine the target, if we are the target, if we can
    // examine the location, if a wizard caused the lookup, or if the target
    // caused the lookup.
    //
    if (Examinable(player, it) ||
        Find_Unfindable(player) ||
        (loc_it == player) ||
        ((loc_it != NOTHING) &&
         (Examinable(player, loc_it) || loc_it == where_is(player))) ||
        Wizard(cause) ||
        (it == cause))
        return 1;

    room_it = where_room(it);
    if (Good_obj(room_it))
        findable_room = !Hideout(room_it);
    else
        findable_room = 1;

    // Succeed if we control the containing room or if the target is findable
    // and the containing room is not unfindable.
    //
    if (((room_it != NOTHING) && Examinable(player, room_it)) ||
        Find_Unfindable(player) || (Findable(it) && findable_room))
        return 1;

    // We can't do it.
    //
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * nearby: Check if thing is nearby player (in inventory, in same room, or
 * * IS the room.
 */

int nearby(dbref player, dbref thing)
{
    int thing_loc, player_loc;

    if (!Good_obj(player) || !Good_obj(thing))
        return 0;
    thing_loc = where_is(thing);
    if (thing_loc == player)
        return 1;
    player_loc = where_is(player);
    if ((thing_loc == player_loc) || (thing == player_loc))
        return 1;
    return 0;
}

/*
 * ---------------------------------------------------------------------------
 * * exit_visible, exit_displayable: Is exit visible?
 */
int exit_visible(dbref exit, dbref player, int key)
{
#if defined(WOD_REALMS) && !defined(STANDALONE)
    int iRealmDirective = DoThingToThingVisibility(player, exit, ACTION_IS_STATIONARY);
    if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
    {
        return 0;
    }
#endif

    // Exam exit's location
    //
    if (key & VE_LOC_XAM)
        return 1;

    // Exam exit
    //
    if (Examinable(player, exit))
        return 1;

    // Exit is light
    //
    if (Light(exit))
        return 1;

    // Dark location or base
    //
    if (key & (VE_LOC_DARK | VE_BASE_DARK))
        return 0;

    // Dark exit
    //
    if (Dark(exit))
        return 0;

    // Default
    //
    return 1;
}

// Exit visible to look
//
int exit_displayable(dbref exit, dbref player, int key)
{
    // Dark exit
    //
    if (Dark(exit))
        return 0;

#if defined(WOD_REALMS) && !defined(STANDALONE)
    int iRealmDirective = DoThingToThingVisibility(player, exit, ACTION_IS_STATIONARY);
    if (REALM_DO_HIDDEN_FROM_YOU == iRealmDirective)
    {
        return 0;
    }
#endif

    // Light exit
    //
    if (Light(exit))
        return 1;

    // Dark location or base.
    //
    if (key & (VE_LOC_DARK | VE_BASE_DARK))
        return 0;

    // Default
    //
    return 1;
}
/*
 * ---------------------------------------------------------------------------
 * * next_exit: return next exit that is ok to see.
 */

dbref next_exit(dbref player, dbref this0, int exam_here)
{
    if (isRoom(this0))
        return NOTHING;
    if (isExit(this0) && exam_here)
        return this0;

    while ((this0 != NOTHING) && Dark(this0) && !Light(this0) &&
           !Examinable(player, this0))
        this0 = Next(this0);

    return this0;
}

#ifndef STANDALONE

/*
 * ---------------------------------------------------------------------------
 * * did_it: Have player do something to/with thing
 */

void did_it(dbref player, dbref thing, int what, const char *def, int owhat, const char *odef, int awhat, char *args[], int nargs)
{
    char *d, *buff, *act, *charges, *bp, *str;
    dbref loc, aowner;
    int num, aflags;
    char *preserve[MAX_GLOBAL_REGS];
    int preserve_len[MAX_GLOBAL_REGS];

    // If we need to call exec() from within this function, we first save
    // the state of the global registers, in order to avoid munging them
    // inappropriately. Do note that the restoration to their original
    // values occurs BEFORE the execution of the @a-attribute. Therefore,
    // any changing of setq() values done in the @-attribute and @o-attribute
    // will NOT be passed on. This prevents odd behaviors that result from
    // odd @verbs and so forth (the idea is to preserve the caller's control
    // of the global register values).
    //

    int need_pres = 0;

    // message to player.
    //

    if (what > 0)
    {
        d = atr_pget(thing, what, &aowner, &aflags);
        if (*d)
        {
            need_pres = 1;
            save_global_regs("did_it_save", preserve, preserve_len);
            buff = bp = alloc_lbuf("did_it.1");
            str = d;
            TinyExec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_FCHECK | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if (what == A_HTDESC) {
                safe_str("\r\n", buff, &bp);
                *bp = '\0';
                notify_html(player, buff);
            } else
                notify(player, buff);
            free_lbuf(buff);
        } else if (def) {
            notify(player, def);
        }
        free_lbuf(d);
    } else if ((what < 0) && def) {
        notify(player, def);
    }
    /*
     * message to neighbors
     */

    if ((owhat > 0) && Has_location(player) && Good_obj(loc = Location(player)))
    {
        d = atr_pget(thing, owhat, &aowner, &aflags);
        if (*d)
        {
            if (!need_pres)
            {
                need_pres = 1;
                save_global_regs("did_it_save", preserve, preserve_len);
            }
            buff = bp = alloc_lbuf("did_it.2");
            str = d;
            TinyExec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_FCHECK | EV_TOP, &str, args, nargs);
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
    else if ((owhat < 0) && odef && Has_location(player) && Good_obj(loc = Location(player)))
    {
        notify_except2(loc, player, player, thing, tprintf("%s %s", Name(player), odef));
    }

    // If we preserved the state of the global registers, restore them.
    //
    if (need_pres)
    {
        restore_global_regs("did_it_restore", preserve, preserve_len);
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
                num = Tiny_atol(charges);
                if (num > 0)
                {
                    buff = alloc_sbuf("did_it.charges");
                    Tiny_ltoa(num-1, buff);
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
            wait_que(thing, player, FALSE, lta, NOTHING, 0, act,
                args, nargs, mudstate.global_regs);
        }
        free_lbuf(act);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * do_verb: Command interface to did_it.
 */

void do_verb(dbref player, dbref cause, int key, char *victim_str, char *args[], int nargs)
{
    dbref actor, victim;
    dbref aowner = NOTHING;
    int what, owhat, awhat, nxargs, restriction;
    int aflags = NOTHING;
    ATTR *ap;
    const char *whatd, *owhatd;
    char *xargs[10];

    // Look for the victim.
    //
    if (!victim_str || !*victim_str)
    {
        notify(player, "Nothing to do.");
        return;
    }

    // Get the victim.
    //
    init_match(player, victim_str, NOTYPE);
    match_everything(MAT_EXIT_PARENTS);
    victim = noisy_match_result();
    if (!Good_obj(victim))
    {
        return;
    }

    // Get the actor.  Default is my cause.
    //
    if ((nargs >= 1) && args[0] && *args[0])
    {
        init_match(player, args[0], NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        actor = noisy_match_result();
        if (!Good_obj(actor))
        {
            return;
        }
    }
    else
    {
        actor = cause;
    }

    // Check permissions.  There are two possibilities:
    //
    //    1. Player controls both victim and actor. In this case,
    //       victim runs his action list.
    //
    //    2. Player controls actor. In this case victim does not run
    //       his action list and any attributes that player cannot read
    //       from victim are defaulted.
    //
    if (!controls(player, actor))
    {
        notify_quiet(player, "Permission denied,");
        return;
    }
    restriction = !controls(player, victim);

    what = -1;
    owhat = -1;
    awhat = -1;
    whatd = NULL;
    owhatd = NULL;
    nxargs = 0;

    // Get invoker message attribute.
    //
    if (nargs >= 2)
    {
        ap = atr_str(args[1]);
        if (ap && (ap->number > 0))
        {
            what = ap->number;
        }
    }

    // Get invoker message default.
    //
    if ((nargs >= 3) && args[2] && *args[2])
    {
        whatd = args[2];
    }

    // Get others message attribute.
    //
    if (nargs >= 4)
    {
        ap = atr_str(args[3]);
        if (ap && (ap->number > 0))
        {
            owhat = ap->number;
        }
    }

    // Get others message default.
    //
    if ((nargs >= 5) && args[4] && *args[4])
    {
        owhatd = args[4];
    }

    // Get action attribute.
    //
    if (nargs >= 6)
    {
        ap = atr_str(args[5]);
        if (ap)
        {
            awhat = ap->number;
        }
    }

    // Get arguments.
    //
    if (nargs >= 7)
    {
        parse_arglist(victim, actor, args[6], '\0',
            EV_STRIP_LS | EV_STRIP_TS, xargs, 10, (char **)NULL, 0, &nxargs);
    }

    // If player doesn't control both, enforce visibility restrictions.
    //
    if (restriction)
    {
        ap = NULL;
        if (what != -1)
        {
            atr_get_info(victim, what, &aowner, &aflags);
            ap = atr_num(what);
        }
        if (!ap || !Read_attr(player, victim, ap, aowner, aflags) ||
            ((ap->number == A_DESC) && !mudconf.read_rem_desc &&
             !Examinable(player, victim) && !nearby(player, victim)))
        {
            what = -1;
        }

        ap = NULL;
        if (owhat != -1)
        {
            atr_get_info(victim, owhat, &aowner, &aflags);
            ap = atr_num(owhat);
        }
        if (!ap || !Read_attr(player, victim, ap, aowner, aflags) ||
            ((ap->number == A_DESC) && !mudconf.read_rem_desc &&
             !Examinable(player, victim) && !nearby(player, victim)))
        {
            owhat = -1;
        }

        awhat = 0;
    }

    // Go do it.
    //
    did_it(actor, victim, what, whatd, owhat, owhatd, awhat,
           xargs, nxargs);

    // Free user args.
    //
    for (int i = 0; i < nxargs; i++)
    {
        free_lbuf(xargs[i]);
    }
}

#endif // STANDALONE

// --------------------------------------------------------------------------
// OutOfMemory: handle an out of memory condition.
//
BOOL OutOfMemory(const char *SourceFile, unsigned int LineNo)
{
    Log.tinyprintf("%s(%u): Out of memory." ENDLINE, SourceFile, LineNo);
    Log.Flush();
#ifdef STANDALONE
    abort();
#else // STANDALONE
    if (mudstate.bCanRestart)
    {
        abort();
    }
    else
    {
        do_restart(GOD, GOD, 0);
    }
#endif // STANDALONE
    return TRUE;
}

// --------------------------------------------------------------------------
// AssertionFailed: A logical assertion has failed.
//
BOOL AssertionFailed(const char *SourceFile, unsigned int LineNo)
{
    Log.tinyprintf("%s(%u): Assertion failed." ENDLINE, SourceFile, LineNo);
    Log.Flush();
#ifdef STANDALONE
    abort();
#else // STANDALONE
    if (mudstate.bCanRestart)
    {
        abort();
    }
    else
    {
        do_restart(GOD, GOD, 0);
    }
#endif // STANDALONE
    return FALSE;
}

