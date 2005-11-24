// game.cpp
//
// $Id: game.cpp,v 1.80 2005-11-24 20:07:06 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/stat.h>
#include <signal.h>

#include "attrs.h"
#include "comsys.h"
#include "file_c.h"
#include "mguests.h"
#include "muxcli.h"
#include "pcre.h"
#include "powers.h"
#include "help.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif /* REALITY_LVLS */

extern void init_attrtab(void);
extern void init_cmdtab(void);
extern void cf_init(void);
extern void pcache_init(void);
extern int  cf_read(void);
extern void ValidateConfigurationDbrefs(void);
extern void init_functab(void);
extern void close_sockets(bool emergency, char *message);
extern void build_version(void);
extern void init_version(void);
extern void init_logout_cmdtab(void);
extern void raw_notify(dbref, const char *);
extern void do_dbck(dbref executor, dbref caller, dbref enactor, int);
extern void boot_slave(dbref executor, dbref caller, dbref enactor, int key);
#ifdef QUERY_SLAVE
extern void boot_sqlslave(dbref executor, dbref caller, dbref enactor, int key);
#endif // QUERY_SLAVE

void fork_and_dump(int);
void pcache_sync(void);
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
static void init_rlimit(void);
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

#ifdef WIN32
extern CRITICAL_SECTION csDescriptorList;      // for thread synchronisation
#endif // WIN32

void do_dump(dbref executor, dbref caller, dbref enactor, int key)
{
#ifndef WIN32
    if (mudstate.dumping)
    {
        notify(executor, "Dumping in progress. Try again later.");
        return;
    }
#endif
    notify(executor, "Dumping...");
    fork_and_dump(key);
}

// print out stuff into error file
//
void report(void)
{
    STARTLOG(LOG_BUGS, "BUG", "INFO");
    log_text("Command: '");
    log_text(mudstate.debug_cmd);
    log_text("'");
    ENDLOG;
    if (Good_obj(mudstate.curr_executor))
    {
        STARTLOG(LOG_BUGS, "BUG", "INFO");
        log_text("Player: ");
        log_name_and_loc(mudstate.curr_executor);
        if (  mudstate.curr_enactor != mudstate.curr_executor
           && Good_obj(mudstate.curr_enactor))
        {
            log_text(" Enactor: ");
            log_name_and_loc(mudstate.curr_enactor);
        }
        ENDLOG;
    }
}

/* ----------------------------------------------------------------------
 * regexp_match: Load a regular expression match and insert it into
 * registers.
 */

bool regexp_match
(
    char *pattern,
    char *str,
    int case_opt,
    char *args[],
    int nargs
)
{
    int matches;
    int i;
    const char *errptr;
    int erroffset;

    /*
     * Load the regexp pattern. This allocates memory which must be
     * later freed. A free() of the regexp does free all structures
     * under it.
     */

    pcre *re;
    if (  MuxAlarm.bAlarmed
       || (re = pcre_compile(pattern, case_opt, &errptr, &erroffset, NULL)) == NULL)
    {
        /*
         * This is a matching error. We have an error message in
         * regexp_errbuf that we can ignore, since we're doing
         * command-matching.
         */
        return false;
    }

    // To capture N substrings, you need space for 3(N+1) offsets in the
    // offset vector. We'll allow 2N-1 substrings and possibly ignore some.
    //
    const int ovecsize = 6 * nargs;
    int *ovec = new int[ovecsize];

    /*
     * Now we try to match the pattern. The relevant fields will
     * automatically be filled in by this.
     */
    matches = pcre_exec(re, NULL, str, strlen(str), 0, 0, ovec, ovecsize);
    if (matches < 0)
    {
        delete ovec;
        MEMFREE(re);
        return false;
    }

    if (matches == 0)
    {
        // There were too many substring matches. See docs for
        // pcre_copy_substring().
        //
        matches = ovecsize / 3;
    }

    /*
     * Now we fill in our args vector. Note that in regexp matching,
     * 0 is the entire string matched, and the parenthesized strings
     * go from 1 to 9. We DO PRESERVE THIS PARADIGM, for consistency
     * with other languages.
     */

    for (i = 0; i < nargs; ++i)
    {
        args[i] = alloc_lbuf("regexp_match");
        if (pcre_copy_substring(str, ovec, matches, i,
                                args[i], LBUF_SIZE) < 0)
        {
            free_lbuf(args[i]);
            args[i] = NULL;
        }
    }

    delete ovec;
    MEMFREE(re);
    return true;
}

/* ----------------------------------------------------------------------
 * atr_match: Check attribute list for wild card matches and queue them.
 */

static int atr_match1
(
    dbref thing,
    dbref parent,
    dbref player,
    char  type,
    char  *str,
    char  *raw_str,
    int   check_exclude,
    int   hash_insert
)
{
    // See if we can do it.  Silently fail if we can't.
    //
    if (!could_doit(player, parent, A_LUSE))
    {
        return -1;
    }

    int match = 0;
    if (  AMATCH_CMD == type
       && mudstate.bfNoCommands.IsSet(parent))
    {
        return match;
    }
    else if ( AMATCH_LISTEN == type
            && mudstate.bfNoListens.IsSet(parent))
    {
        return match;
    }

    bool bFoundCommands = false;
    bool bFoundListens  = false;

    char *as;
    atr_push();
    for (int atr = atr_head(parent, &as); atr; atr = atr_next(&as))
    {
        ATTR *ap = atr_num(atr);

        // Never check NOPROG attributes.
        //
        if (  !ap
           || (ap->flags & AF_NOPROG))
        {
            continue;
        }

        // We need to grab the attribute even before we know whether we'll use
        // it or not in order to maintain cached knowledge about ^-Commands
        // and $-Commands.
        //
        dbref aowner;
        int   aflags;
        char buff[LBUF_SIZE];
        atr_get_str(buff, parent, atr, &aowner, &aflags);

        if (aflags & AF_NOPROG)
        {
            continue;
        }

        char *s = NULL;
        if (  AMATCH_CMD    == buff[0]
           || AMATCH_LISTEN == buff[0])
        {
            s = strchr(buff+1, ':');
            if (s)
            {
                if (AMATCH_CMD == buff[0])
                {
                    bFoundCommands = true;
                }
                else
                {
                    bFoundListens = true;
                }
            }
        }

        // If we aren't the bottom level, check if we saw this attr
        // before. Also exclude it if the attribute type is PRIVATE.
        //
        if (  check_exclude
           && (  (ap->flags & AF_PRIVATE)
              || (aflags & AF_PRIVATE)
              || hashfindLEN(&(ap->number), sizeof(ap->number), &mudstate.parent_htab)))
        {
            continue;
        }

        // If we aren't the top level, remember this attr so we
        // exclude it from now on.
        //
        if (hash_insert)
        {
            hashaddLEN(&(ap->number), sizeof(ap->number), &atr, &mudstate.parent_htab);
        }

        // Check for the leadin character after excluding the attrib.
        // This lets non-command attribs on the child block commands
        // on the parent.
        //
        if (buff[0] != type)
        {
            continue;
        }

        // Was there a ':'?
        //
        if (!s)
        {
            continue;
        }
        *s++ = '\0';

        char *args[NUM_ENV_VARS];
        if (  (  0 != (aflags & AF_REGEXP)
            && regexp_match(buff + 1, (aflags & AF_NOPARSE) ? raw_str : str,
                ((aflags & AF_CASE) ? 0 : PCRE_CASELESS), args, NUM_ENV_VARS))
           || (  0 == (aflags & AF_REGEXP)
              && wild(buff + 1, (aflags & AF_NOPARSE) ? raw_str : str,
                args, NUM_ENV_VARS)))
        {
            match = 1;
            CLinearTimeAbsolute lta;
            wait_que(thing, player, player, false, lta, NOTHING, 0, s,
                args, NUM_ENV_VARS, mudstate.global_regs);

            for (int i = 0; i < NUM_ENV_VARS; i++)
            {
                if (args[i])
                {
                    free_lbuf(args[i]);
                }
            }
        }
    }
    atr_pop();

    if (bFoundCommands)
    {
        mudstate.bfNoCommands.Clear(parent);
        mudstate.bfCommands.Set(parent);
    }
    else
    {
        mudstate.bfCommands.Clear(parent);
        mudstate.bfNoCommands.Set(parent);
    }

    if (bFoundListens)
    {
        mudstate.bfNoListens.Clear(parent);
        mudstate.bfListens.Set(parent);
    }
    else
    {
        mudstate.bfListens.Clear(parent);
        mudstate.bfNoListens.Set(parent);
    }
    return match;
}

bool atr_match
(
    dbref thing,
    dbref player,
    char  type,
    char  *str,
    char  *raw_str,
    bool check_parents
)
{
    int lev, result;
    bool exclude, insert;
    dbref parent;

    // If thing is halted or we are matching $-commands on a NO_COMMAND
    // object, don't check anything
    //
    if (  Halted(thing)
       || (  AMATCH_CMD == type
          && No_Command(thing)))
    {
        return false;
    }

    // If we're matching ^-commands, strip ANSI
    //
    if (AMATCH_LISTEN == type)
    {
        // Remember, strip_ansi returns a pointer to a static buffer
        // within itself.
        //
        size_t junk;
        str = strip_ansi(str, &junk);
    }

    // If not checking parents, just check the thing
    //
    bool match = false;
    if (!check_parents)
    {
        return (atr_match1(thing, thing, player, type, str, raw_str, false, false) > 0);
    }

    // Check parents, ignoring halted objects
    //
    exclude = false;
    insert = true;
    hashflush(&mudstate.parent_htab);
    ITER_PARENTS(thing, parent, lev)
    {
        if (!Good_obj(Parent(parent)))
        {
            insert = false;
        }
        result = atr_match1(thing, parent, player, type, str, raw_str,
            exclude, insert);
        if (result > 0)
        {
            match = true;
        }
        else if (result < 0)
        {
            return match;
        }
        exclude = true;
    }
    return match;
}

/* ---------------------------------------------------------------------------
 * notify_check: notifies the object #target of the message msg, and
 * optionally notify the contents, neighbors, and location also.
 */

bool check_filter(dbref object, dbref player, int filter, const char *msg)
{
    int aflags;
    dbref aowner;
    char *buf, *nbuf, *cp, *dp, *str;

    buf = atr_pget(object, filter, &aowner, &aflags);
    if (!*buf)
    {
        free_lbuf(buf);
        return true;
    }
    char **preserve = NULL;
    int *preserve_len = NULL;
    preserve = PushPointers(MAX_GLOBAL_REGS);
    preserve_len = PushIntegers(MAX_GLOBAL_REGS);
    save_global_regs("check_filter_save", preserve, preserve_len);
    nbuf = dp = alloc_lbuf("check_filter");
    str = buf;
    mux_exec(nbuf, &dp, object, player, player,
             EV_FIGNORE | EV_EVAL | EV_TOP, &str, (char **)NULL, 0);
    *dp = '\0';
    dp = nbuf;
    free_lbuf(buf);
    restore_global_regs("check_filter_restore", preserve, preserve_len);
    PopIntegers(preserve_len, MAX_GLOBAL_REGS);
    PopPointers(preserve, MAX_GLOBAL_REGS);

    if (!(aflags & AF_REGEXP))
    {
        do
        {
            cp = parse_to(&dp, ',', EV_STRIP_CURLY);
            mudstate.wild_invk_ctr = 0;
            if (  MuxAlarm.bAlarmed
               || quick_wild(cp, msg))
            {
                free_lbuf(nbuf);
                return false;
            }
        } while (dp != NULL);
    }
    else
    {
        int case_opt = (aflags & AF_CASE) ? 0 : PCRE_CASELESS;
        do
        {
            int erroffset;
            const char *errptr;
            cp = parse_to(&dp, ',', EV_STRIP_CURLY);
            pcre *re;
            if (  !MuxAlarm.bAlarmed
               && (re = pcre_compile(cp, case_opt, &errptr, &erroffset, NULL)) != NULL)
            {
                const int ovecsize = 33;
                int ovec[ovecsize];
                int matches = pcre_exec(re, NULL, msg, strlen(msg), 0, 0,
                    ovec, ovecsize);
                if (0 <= matches)
                {
                    MEMFREE(re);
                    free_lbuf(nbuf);
                    return false;
                }
                MEMFREE(re);
            }
        } while (dp != NULL);
    }
    free_lbuf(nbuf);
    return true;
}

static char *add_prefix(dbref object, dbref player, int prefix,
                        const char *msg, const char *dflt)
{
    int aflags;
    dbref aowner;
    char *buf, *nbuf, *cp, *str;

    buf = atr_pget(object, prefix, &aowner, &aflags);
    if (!*buf)
    {
        cp = buf;
        safe_str(dflt, buf, &cp);
    }
    else
    {
        char **preserve = NULL;
        int *preserve_len = NULL;
        preserve = PushPointers(MAX_GLOBAL_REGS);
        preserve_len = PushIntegers(MAX_GLOBAL_REGS);
        save_global_regs("add_prefix_save", preserve, preserve_len);

        nbuf = cp = alloc_lbuf("add_prefix");
        str = buf;
        mux_exec(nbuf, &cp, object, player, player,
                 EV_FIGNORE | EV_EVAL | EV_TOP, &str, (char **)NULL, 0);
        free_lbuf(buf);

        restore_global_regs("add_prefix_restore", preserve, preserve_len);
        PopIntegers(preserve_len, MAX_GLOBAL_REGS);
        PopPointers(preserve, MAX_GLOBAL_REGS);

        buf = nbuf;
    }
    if (cp != buf)
    {
        safe_chr(' ', buf, &cp);
    }
    safe_str(msg, buf, &cp);
    *cp = '\0';
    return buf;
}

static char *dflt_from_msg(dbref sender, dbref sendloc)
{
    char *tp, *tbuff;

    tp = tbuff = alloc_lbuf("notify_check.fwdlist");
    safe_str("From ", tbuff, &tp);
    if (Good_obj(sendloc))
    {
        safe_str(Name(sendloc), tbuff, &tp);
    }
    else
    {
        safe_str(Name(sender), tbuff, &tp);
    }
    safe_chr(',', tbuff, &tp);
    *tp = '\0';
    return tbuff;
}

/* Do HTML escaping, converting < to &lt;, etc.  'dest' needs to be
 * allocated & freed by the caller.
 *
 * If you're using this to append to a string, you can pass in the
 * safe_{str|chr} (char **) so we can just do the append directly,
 * saving you an alloc_lbuf()...free_lbuf().  If you want us to append
 * from the start of 'dest', just pass in a 0 for 'destp'.
 *
 * Returns 0 if the copy succeeded, 1 if it failed.
 */
bool html_escape(const char *src, char *dest, char **destp)
{
    const char *msg_orig;
    bool ret = false;

    if (destp == 0)
    {
        char *temp = dest;
        destp = &temp;
    }

    for (msg_orig = src; msg_orig && *msg_orig && !ret; msg_orig++)
    {
        char *p = *destp;
        switch (*msg_orig)
        {
        case '<':
            safe_str("&lt;", dest, destp);
            break;

        case '>':
            safe_str("&gt;", dest, destp);
            break;

        case '&':
            safe_str("&amp;", dest, destp);
            break;

        case '\"':
            safe_str("&quot;", dest, destp);
            break;

        default:
            safe_chr(*msg_orig, dest, destp);
            break;
        }

        // For <>&\, this may cause an extra loop around before it figures out that we are
        // out of buffer, but no harm is done in this, and the common case is a single character.
        //
        if (p == *destp)
        {
            ret = true;
        }
    }
    **destp = 0;
    return ret;
}

void notify_check(dbref target, dbref sender, const char *msg, int key)
{
    // If speaker is invalid or message is empty, just exit.
    //
    if (  !Good_obj(target)
       || !msg
       || !*msg)
    {
        return;
    }

#ifdef WOD_REALMS
    if ((key & MSG_OOC) == 0)
    {
        if ((key & MSG_SAYPOSE) != 0)
        {
            if (REALM_DO_HIDDEN_FROM_YOU == DoThingToThingVisibility(target, sender, ACTION_IS_TALKING))
            {
                return;
            }
        }
        else
        {
            if (REALM_DO_HIDDEN_FROM_YOU == DoThingToThingVisibility(target, sender, ACTION_IS_MOVING))
            {
                return;
            }
        }
    }
#endif // WOD_REALMS

    // Enforce a recursion limit
    //
    mudstate.ntfy_nest_lev++;
    if (mudconf.ntfy_nest_lim <= mudstate.ntfy_nest_lev)
    {
        mudstate.ntfy_nest_lev--;
        return;
    }

    char *msg_ns, *mp, *tbuff, *tp, *buff;
    char *args[NUM_ENV_VARS];
    dbref aowner,  recip, obj;
    int i, nargs, aflags;
    FWDLIST *fp;

    // If we want NOSPOOF output, generate it.  It is only needed if we are
    // sending the message to the target object.
    //
    if (key & MSG_ME)
    {
        mp = msg_ns = alloc_lbuf("notify_check");
        if (  Nospoof(target)
           && target != sender
           && target != mudstate.curr_enactor
           && target != mudstate.curr_executor)
        {
            // I'd really like to use tprintf here but I can't because the
            // caller may have.  notify(target, tprintf(...)) is quite common
            // in the code.
            //
            tbuff = alloc_sbuf("notify_check.nospoof");
            safe_chr('[', msg_ns, &mp);
            safe_str(Name(sender), msg_ns, &mp);
            sprintf(tbuff, "(#%d)", sender);
            safe_str(tbuff, msg_ns, &mp);

            if (sender != Owner(sender))
            {
                safe_chr('{', msg_ns, &mp);
                safe_str(Name(Owner(sender)), msg_ns, &mp);
                safe_chr('}', msg_ns, &mp);
            }
            if (sender != mudstate.curr_enactor)
            {
                sprintf(tbuff, "<-(#%d)", mudstate.curr_enactor);
                safe_str(tbuff, msg_ns, &mp);
            }
            safe_str("] ", msg_ns, &mp);
            free_sbuf(tbuff);
        }
        safe_str(msg, msg_ns, &mp);
        *mp = '\0';
    }
    else
    {
        msg_ns = NULL;
    }

    // msg contains the raw message, msg_ns contains the NOSPOOFed msg.
    //
    bool check_listens = !Halted(target);
    switch (Typeof(target))
    {
    case TYPE_PLAYER:
        if (key & MSG_ME)
        {
            if (key & MSG_HTML)
            {
                raw_notify_html(target, msg_ns);
            }
            else
            {
                if (Html(target))
                {
                    char *msg_ns_escaped;

                    msg_ns_escaped = alloc_lbuf("notify_check_escape");
                    html_escape(msg_ns, msg_ns_escaped, 0);
                    raw_notify(target, msg_ns_escaped);
                    free_lbuf(msg_ns_escaped);
                }
                else
                {
                    raw_notify(target, msg_ns);
                }
            }
        }
        if (!mudconf.player_listen)
        {
            check_listens = false;
        }

        // FALLTHROUGH

    case TYPE_THING:
    case TYPE_ROOM:

        // If we're in a pipe, objects can receive raw_notify if
        // they're not a player. (players were already notified
        // above.
        //
        if (  mudstate.inpipe
           && !isPlayer(target))
        {
            raw_notify(target, msg_ns);
        }

        // Forward puppet message if it is for me.
        //
        bool has_neighbors = Has_location(target);
        dbref targetloc = where_is(target);
        bool is_audible = Audible(target);

        if ( (key & MSG_ME)
           && Puppet(target)
           && (target != Owner(target))
           && (  (key & MSG_PUP_ALWAYS)
              || (  targetloc != Location(Owner(target))
                 && targetloc != Owner(target))))
        {
            tp = tbuff = alloc_lbuf("notify_check.puppet");
            safe_str(Name(target), tbuff, &tp);
            safe_str("> ", tbuff, &tp);
            safe_str(msg_ns, tbuff, &tp);
            *tp = '\0';
            raw_notify(Owner(target), tbuff);
            free_lbuf(tbuff);
        }

        // Check for @Listen match if it will be useful.
        //
        bool pass_listen = false;
        nargs = 0;
        if (  check_listens
           && (key & (MSG_ME | MSG_INV_L))
           && H_Listen(target))
        {
            tp = atr_get(target, A_LISTEN, &aowner, &aflags);
            if (*tp && wild(tp, (char *)msg, args, NUM_ENV_VARS))
            {
                for (nargs = NUM_ENV_VARS; nargs && (!args[nargs - 1] || !(*args[nargs - 1])); nargs--)
                {
                    ; // Nothing
                }
                pass_listen = true;
            }
            free_lbuf(tp);
        }

        // If we matched the @listen or are monitoring, check the
        // USE lock.
        //
        bool pass_uselock = false;
        if (  (key & MSG_ME)
           && check_listens
           && (  pass_listen
              || Monitor(target)))
        {
            pass_uselock = could_doit(sender, target, A_LUSE);
        }

        // Process AxHEAR if we pass LISTEN, USElock and it's for me.
        //
        if (  (key & MSG_ME)
           && pass_listen
           && pass_uselock
           && mudstate.nHearNest <= 2)
        {
            mudstate.nHearNest++;
            if (sender != target)
            {
                did_it(sender, target, 0, NULL, 0, NULL, A_AHEAR, args, nargs);
            }
            else
            {
                did_it(sender, target, 0, NULL, 0, NULL, A_AMHEAR, args,
                    nargs);
            }
            did_it(sender, target, 0, NULL, 0, NULL, A_AAHEAR, args, nargs);
            mudstate.nHearNest--;
        }

        // Get rid of match arguments. We don't need them anymore.
        //
        if (pass_listen)
        {
            for (i = 0; i < nargs; i++)
            {
                if (args[i] != NULL)
                {
                    free_lbuf(args[i]);
                }
            }
        }

        // Process ^-listens if for me, MONITOR, and we pass USElock.
        //
        if (  (key & MSG_ME)
           && pass_uselock
           && sender != target
           && Monitor(target))
        {
            atr_match(target, sender, AMATCH_LISTEN, (char *)msg, (char *)msg,
                false);
        }

        // Deliver message to forwardlist members.
        //
        if ( (key & MSG_FWDLIST)
           && is_audible
           && check_filter(target, sender, A_FILTER, msg))
        {
            tbuff = dflt_from_msg(sender, target);
            buff = add_prefix(target, sender, A_PREFIX, msg, tbuff);
            free_lbuf(tbuff);

            fp = fwdlist_get(target);
            if (fp)
            {
                for (i = 0; i < fp->count; i++)
                {
                    recip = fp->data[i];
                    if (  !Good_obj(recip)
                       || recip == target)
                    {
                        continue;
                    }
                    notify_check(recip, sender, buff,
                             MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE);
                }
            }
            free_lbuf(buff);
        }

        // Deliver message through audible exits.
        //
        if (key & MSG_INV_EXITS)
        {
            DOLIST(obj, Exits(target))
            {
                recip = Location(obj);
                if (  Audible(obj)
                   && (  recip != target
                      && check_filter(obj, sender, A_FILTER, msg)))
                {
                    buff = add_prefix(obj, target, A_PREFIX, msg,
                        "From a distance,");
                    notify_check(recip, sender, buff,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE);
                    free_lbuf(buff);
                }
            }
        }

        // Deliver message through neighboring audible exits.
        //
        if (  has_neighbors
           && (  (key & MSG_NBR_EXITS)
              || (  (key & MSG_NBR_EXITS_A)
                 && is_audible)))
        {
            // If from inside, we have to add the prefix string of
            // the container.
            //
            if (key & MSG_S_INSIDE)
            {
                tbuff = dflt_from_msg(sender, target);
                buff = add_prefix(target, sender, A_PREFIX, msg, tbuff);
                free_lbuf(tbuff);
            }
            else
            {
                buff = (char *)msg;
            }

            DOLIST(obj, Exits(Location(target)))
            {
                recip = Location(obj);
                if (  Good_obj(recip)
                   && Audible(obj)
                   && recip != targetloc
                   && recip != target
                   && check_filter(obj, sender, A_FILTER, msg))
                {
                    tbuff = add_prefix(obj, target, A_PREFIX, buff,
                        "From a distance,");
                    notify_check(recip, sender, tbuff,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE);
                    free_lbuf(tbuff);
                }
            }
            if (key & MSG_S_INSIDE)
            {
                free_lbuf(buff);
            }
        }

        // Deliver message to contents.
        //
        if (  (  (key & MSG_INV)
              || (  (key & MSG_INV_L)
                 && pass_listen))
           && check_filter(target, sender, A_INFILTER, msg))
        {
            // Don't prefix the message if we were given the MSG_NOPREFIX key.
            //
            if (key & MSG_S_OUTSIDE)
            {
                buff = add_prefix(target, sender, A_INPREFIX, msg, "");
            }
            else
            {
                buff = (char *)msg;
            }
            DOLIST(obj, Contents(target))
            {
                if (obj != target)
                {
                    notify_check(obj, sender, buff,
                        MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | key & MSG_HTML);
                }
            }
            if (key & MSG_S_OUTSIDE)
            {
                free_lbuf(buff);
            }
        }

        // Deliver message to neighbors.
        //
        if (  has_neighbors
           && (  (key & MSG_NBR)
              || (  (key & MSG_NBR_A)
                 && is_audible
                 && check_filter(target, sender, A_FILTER, msg))))
        {
            if (key & MSG_S_INSIDE)
            {
                tbuff = dflt_from_msg(sender, target);
                buff = add_prefix(target, sender, A_PREFIX, msg, "");
                free_lbuf(tbuff);
            }
            else
            {
                buff = (char *)msg;
            }
            DOLIST(obj, Contents(targetloc))
            {
                if (  obj != target
                   && obj != targetloc)
                {
                    notify_check(obj, sender, buff,
                    MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE);
                }
            }
            if (key & MSG_S_INSIDE)
            {
                free_lbuf(buff);
            }
        }

        // Deliver message to container.
        //
        if (  has_neighbors
           && (  (key & MSG_LOC)
              || ( (key & MSG_LOC_A)
                 && is_audible
                 && check_filter(target, sender, A_FILTER, msg))))
        {
            if (key & MSG_S_INSIDE)
            {
                tbuff = dflt_from_msg(sender, target);
                buff = add_prefix(target, sender, A_PREFIX, msg, tbuff);
                free_lbuf(tbuff);
            }
            else
            {
                buff = (char *)msg;
            }
            notify_check(targetloc, sender, buff,
                MSG_ME | MSG_F_UP | MSG_S_INSIDE);
            if (key & MSG_S_INSIDE)
            {
                free_lbuf(buff);
            }
        }
    }
    if (msg_ns)
    {
        free_lbuf(msg_ns);
    }
    mudstate.ntfy_nest_lev--;
}

void notify_except(dbref loc, dbref player, dbref exception, const char *msg, int key)
{
    dbref first;

    if (loc != exception)
    {
        notify_check(loc, player, msg, (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A | key));
    }
    DOLIST(first, Contents(loc))
    {
        if (first != exception)
        {
            notify_check(first, player, msg, (MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | key));
        }
    }
}

void notify_except2(dbref loc, dbref player, dbref exc1, dbref exc2, const char *msg)
{
    dbref first;

    if (  loc != exc1
       && loc != exc2)
    {
        notify_check(loc, player, msg, (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A));
    }
    DOLIST(first, Contents(loc))
    {
        if (  first != exc1
           && first != exc2)
        {
            notify_check(first, player, msg, (MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE));
        }
    }
}

/* ----------------------------------------------------------------------
 * Reporting of CPU information.
 */

static void report_timecheck
(
    dbref player,
    bool yes_screen,
    bool yes_log,
    bool yes_clear
)
{
    int thing, obj_counted;
    CLinearTimeDelta ltdPeriod, ltdTotal;
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    ltdPeriod = ltaNow - mudstate.cpu_count_from;

    if (  yes_log
       && (LOG_TIMEUSE & mudconf.log_options))
    {
        start_log("OBJ", "CPU");
        log_name(player);
        log_text(" checks object time use over ");
        log_number(ltdPeriod.ReturnSeconds());
        log_text(" seconds" ENDLINE);
    }
    else
    {
        yes_log = false;
        STARTLOG(LOG_ALWAYS, "WIZ", "TIMECHECK");
        log_name(player);
        log_text(" checks object time use over ");
        log_number(ltdPeriod.ReturnSeconds());
        log_text(" seconds");
        ENDLOG;
    }

    obj_counted = 0;
    ltdTotal.Set100ns(0);

    // Step through the db. Care only about the ones that are nonzero.
    //
    DO_WHOLE_DB(thing)
    {
        CLinearTimeDelta &ltd = db[thing].cpu_time_used;
        if (ltd.Return100ns())
        {
            ltdTotal += ltd;
            long used_msecs = ltd.ReturnMilliseconds();
            obj_counted++;
            if (yes_log)
            {
                Log.tinyprintf("#%d\t%ld" ENDLINE, thing, used_msecs);
            }
            if (yes_screen)
            {
                raw_notify(player, tprintf("#%d\t%ld", thing, used_msecs));
            }
            if (yes_clear)
            {
                ltd.Set100ns(0);
            }
        }
    }

    if (yes_screen)
    {
        raw_notify(player,
            tprintf("Counted %d objects using %ld msecs over %d seconds.",
            obj_counted, ltdTotal.ReturnMilliseconds(), ltdPeriod.ReturnSeconds()));
    }

    if (yes_log)
    {
        Log.tinyprintf("Counted %d objects using %ld msecs over %d seconds.",
            obj_counted, ltdTotal.ReturnMilliseconds(), ltdPeriod.ReturnSeconds());
        end_log();
    }

    if (yes_clear)
    {
        mudstate.cpu_count_from = ltaNow;
    }
}

void do_timecheck(dbref executor, dbref caller, dbref enactor, int key)
{
    bool yes_screen, yes_log, yes_clear;

    yes_screen = yes_log = yes_clear = false;

    if (key == 0)
    {
        // No switches, default to printing to screen and clearing counters.
        //
        yes_screen = true;
        yes_clear = true;
    }
    else
    {
        if (key & TIMECHK_RESET)
        {
            yes_clear = true;
        }
        if (key & TIMECHK_SCREEN)
        {
            yes_screen = true;
        }
        if (key & TIMECHK_LOG)
        {
            yes_log = true;
        }
    }
    report_timecheck(executor, yes_screen, yes_log, yes_clear);
}

void do_shutdown
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    char *message
)
{
    if (!Can_SiteAdmin(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    raw_broadcast(0, "GAME: Shutdown by %s", Name(Owner(executor)));
    STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN");
    log_text("Shutdown by ");
    log_name(executor);
    ENDLOG;

    STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN");
    log_text("Shutdown status: ");
    log_text(message);
    ENDLOG;

    int fd = open(mudconf.status_file, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
    if (fd != -1)
    {
        write(fd, message, strlen(message));
        write(fd, ENDLINE, sizeof(ENDLINE)-1);
        DebugTotalFiles++;
        if (close(fd) == 0)
        {
            DebugTotalFiles--;
        }
    }

    // Do we perform a normal or an emergency shutdown? Normal
    // shutdown is handled by exiting the main loop in shovechars,
    // emergency shutdown is done here.
    //
    if (key & SHUTDN_PANIC)
    {
        // Close down the network interface.
        //
        emergency_shutdown();

        local_presync_database();

        // Close the attribute text db and dump the header db.
        //
#ifndef MEMORY_BASED
        // Save cached modified attribute list
        //
        al_store();
#endif // MEMORY_BASED

        pcache_sync();
        SYNC;
        CLOSE;

        STARTLOG(LOG_ALWAYS, "DMP", "PANIC");
        log_text("Panic dump: ");
        log_text(mudconf.crashdb);
        ENDLOG;
        dump_database_internal(DUMP_I_PANIC);
        STARTLOG(LOG_ALWAYS, "DMP", "DONE");
        log_text("Panic dump complete: ");
        log_text(mudconf.crashdb);
        ENDLOG;
    }

    // Set up for normal shutdown.
    //
    mudstate.shutdown_flag = true;
}

// There are several types of dumps:
//
// Type 0 - Normal   mudstate.dumping controlled
// Type 1 - Panic    uncontrolled but only one of these happening at a time.
// Type 2 - Restart  mudstate.dumping controlled.
// Type 3 - FLAT     mudstate.dumping controlled.
// Type 4 - signal   uncontrolled and if we fault twice, the game ends --
//                   see check_panicking.
//
// When changing this function and to keep forking dumps safe, keep in mind
// that the following combinations can be occuring at the same time. Don't
// touch each other's files.
//
// Type 0 and 2 are allowed to touch each other's files. Type 1 and 4 should not
// touch files used in Type 0 or Type 2.
//
typedef struct
{
    char **ppszOutputBase;
    char szOutputSuffix[14];
    bool bUseTemporary;
    int  fType;
    char *pszErrorMessage;
} DUMP_PROCEDURE;

DUMP_PROCEDURE DumpProcedures[NUM_DUMP_TYPES] =
{
    { 0,                ""       , false, 0,                             "" }, // 0 -- Handled specially.
    { &mudconf.crashdb, ""       , false, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening crash file" }, // 1
    { &mudconf.indb,    ""       , true,  OUTPUT_VERSION | OUTPUT_FLAGS, "Opening input file" }, // 2
    { &mudconf.indb,   ".FLAT"   , false, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening flatfile"   }, // 3
    { &mudconf.indb,   ".SIG"    , false, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening signalled flatfile"}  // 4
};

#ifdef WIN32
#define POPEN_READ_OP "rb"
#define POPEN_WRITE_OP "wb"
#else // WIN32
#define POPEN_READ_OP "r"
#define POPEN_WRITE_OP "w"
#endif // WIN32

void dump_database_internal(int dump_type)
{
    char tmpfile[SIZEOF_PATHNAME+32];
    char outfn[SIZEOF_PATHNAME+32];
    char prevfile[SIZEOF_PATHNAME+32];
    FILE *f;

    if (  dump_type < 0
       || NUM_DUMP_TYPES <= dump_type)
    {
        return;
    }

    bool bPotentialConflicts = false;
#ifndef WIN32
    // If we are already dumping for some reason, and suddenly get a type 1 or
    // type 4 dump, basically don't touch mail and comsys files. The other
    // dump will take care of them as well as can be expected for now, and if
    // we try to, we'll just step on them.
    //
    if (  mudstate.dumping
       && (  dump_type == DUMP_I_PANIC
          || dump_type == DUMP_I_SIGNAL))
    {
        bPotentialConflicts = true;
    }
#endif

    // Call the local dump function only if another dump is not already
    // in progress.
    //
    local_dump_database(dump_type);

    if (0 < dump_type)
    {
        DUMP_PROCEDURE *dp = &DumpProcedures[dump_type];

        sprintf(outfn, "%s%s", *(dp->ppszOutputBase), dp->szOutputSuffix);
        if (dp->bUseTemporary)
        {
            sprintf(tmpfile, "%s.#%d#", outfn, mudstate.epoch);
            RemoveFile(tmpfile);
            f = fopen(tmpfile, "wb");
        }
        else
        {
            RemoveFile(outfn);
            f = fopen(outfn, "wb");
        }

        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, NULL, _IOFBF, 16384);
            db_write(f, F_MUX, dp->fType);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }

            if (dp->bUseTemporary)
            {
                ReplaceFile(tmpfile, outfn);
            }
        }
        else
        {
            log_perror("DMP", "FAIL", dp->pszErrorMessage, outfn);
        }

        if (!bPotentialConflicts)
        {
            if (mudconf.have_mailer)
            {
                f = fopen(mudconf.mail_db, "wb");
                if (f)
                {
                    DebugTotalFiles++;
                    dump_mail(f);
                    if (fclose(f) == 0)
                    {
                        DebugTotalFiles--;
                    }
                }
            }
            if (mudconf.have_comsys)
            {
                save_comsys(mudconf.comsys_db);
            }
        }
        return;
    }

    // Nuke our predecessor
    //
    if (mudconf.compress_db)
    {
        sprintf(prevfile, "%s.prev.gz", mudconf.outdb);
        sprintf(tmpfile, "%s.#%d#.gz", mudconf.outdb, mudstate.epoch - 1);
        RemoveFile(tmpfile);
        sprintf(tmpfile, "%s.#%d#.gz", mudconf.outdb, mudstate.epoch);
        strcpy(outfn, mudconf.outdb);
        strcat(outfn, ".gz");

        f = popen(tprintf("%s > %s", mudconf.compress, tmpfile), POPEN_WRITE_OP);
        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, NULL, _IOFBF, 16384);
            db_write(f, F_MUX, OUTPUT_VERSION | OUTPUT_FLAGS);
            if (pclose(f) != -1)
            {
                DebugTotalFiles--;
            }
            ReplaceFile(outfn, prevfile);
            if (ReplaceFile(tmpfile, outfn) < 0)
            {
                log_perror("SAV", "FAIL", "Renaming output file to DB file", tmpfile);
            }
        }
        else
        {
            log_perror("SAV", "FAIL", "Opening", tmpfile);
        }
    }
    else
    {
        sprintf(prevfile, "%s.prev", mudconf.outdb);
        sprintf(tmpfile, "%s.#%d#", mudconf.outdb, mudstate.epoch - 1);
        RemoveFile(tmpfile);
        sprintf(tmpfile, "%s.#%d#", mudconf.outdb, mudstate.epoch);

        f = fopen(tmpfile, "wb");
        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, NULL, _IOFBF, 16384);
            db_write(f, F_MUX, OUTPUT_VERSION | OUTPUT_FLAGS);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
            ReplaceFile(mudconf.outdb, prevfile);
            if (ReplaceFile(tmpfile, mudconf.outdb) < 0)
            {
                log_perror("SAV", "FAIL", "Renaming output file to DB file", tmpfile);
            }
        }
        else
        {
            log_perror("SAV", "FAIL", "Opening", tmpfile);
        }
    }

    if (mudconf.have_mailer)
    {
        f = fopen(mudconf.mail_db, "wb");
        if (f)
        {
            DebugTotalFiles++;
            dump_mail(f);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
        }
    }

    if (mudconf.have_comsys)
    {
        save_comsys(mudconf.comsys_db);
    }
}

void dump_database(void)
{
    char *buff;

    mudstate.epoch++;

#ifndef WIN32
    if (mudstate.dumping)
    {
        STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
        log_text("Waiting on previously-forked child before dumping... ");
        ENDLOG;

        while (mudstate.dumping)
        {
            // We have a forked dump in progress, so we will wait until the
            // child exits.
            //
            MuxAlarm.Sleep(time_1s);
        }
    }
    mudstate.dumping = true;
#endif
    buff = alloc_mbuf("dump_database");
    sprintf(buff, "%s.#%d#", mudconf.outdb, mudstate.epoch);

    STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
    log_text("Dumping: ");
    log_text(buff);
    ENDLOG;

    local_presync_database();

#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    pcache_sync();

    dump_database_internal(DUMP_I_NORMAL);
    SYNC;

    STARTLOG(LOG_DBSAVES, "DMP", "DONE")
    log_text("Dump complete: ");
    log_text(buff);
    ENDLOG;
    free_mbuf(buff);

#ifndef WIN32
    // This doesn't matter. We are about the stop the game. However,
    // leave it in.
    //
    mudstate.dumping = false;
    local_dump_complete_signal();
#endif
}

void fork_and_dump(int key)
{
#ifndef WIN32
    static volatile bool bRequestAccepted = false;

    // fork_and_dump is never called with mudstate.dumping true, but we'll
    // ensure that assertion now.
    //
    if (  bRequestAccepted
       || mudstate.dumping)
    {
        return;
    }
    bRequestAccepted = true;
#endif

    // If no options were given, then it means DUMP_TEXT+DUMP_STRUCT.
    //
    if (key == 0)
    {
        key = DUMP_TEXT+DUMP_STRUCT;
    }

    if (*mudconf.dump_msg)
    {
        raw_broadcast(0, "%s", mudconf.dump_msg);
    }
    check_mail_expiration();
    char *buff = alloc_lbuf("fork_and_dump");
    if (key & (DUMP_TEXT|DUMP_STRUCT))
    {
        STARTLOG(LOG_DBSAVES, "DMP", "CHKPT");
        if (key & DUMP_TEXT)
        {
            log_text("SYNCing");
            if (key & DUMP_STRUCT)
            {
                log_text(" and ");
            }
        }
        if (key & DUMP_STRUCT)
        {
            mudstate.epoch++;
            sprintf(buff, "%s.#%d#", mudconf.outdb, mudstate.epoch);
            log_text("Checkpointing: ");
            log_text(buff);
        }
        ENDLOG;
    }
    if (key & DUMP_FLATFILE)
    {
        STARTLOG(LOG_DBSAVES, "DMP", "FLAT");
        log_text("Creating flatfile: ");
        sprintf(buff, "%s.FLAT", mudconf.outdb);
        log_text(buff);
        ENDLOG;
    }
    free_lbuf(buff);

    local_presync_database();

#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    pcache_sync();
    SYNC;

#ifndef WIN32
    mudstate.write_protect = true;
    int child = 0;
    bool bChildExists = false;
    mudstate.dumping = true;
    bool bAttemptFork = mudconf.fork_dump;
#if !defined(HAVE_PREAD) \
 || !defined(HAVE_PWRITE)
    if (key & DUMP_FLATFILE)
    {
        // Don't attempt a fork()'ed @dump/flat without pread()/pwrite()
        // support.
        //
        bAttemptFork = false;
    }
#endif // !HAVE_PREAD !HAVE_PWRITE
#endif // WIN32
    if (key & (DUMP_STRUCT|DUMP_FLATFILE))
    {
#ifndef WIN32
        if (bAttemptFork)
        {
            child = fork();
        }
        if (child == 0)
        {
#endif
            if (key & DUMP_STRUCT)
            {
                dump_database_internal(DUMP_I_NORMAL);
            }
            if (key & DUMP_FLATFILE)
            {
                dump_database_internal(DUMP_I_FLAT);
            }
#ifndef WIN32
            if (mudconf.fork_dump)
            {
                _exit(0);
            }
        }
        else if (child < 0)
        {
            log_perror("DMP", "FORK", NULL, "fork()");
        }
        else if (child != mudstate.dumper)
        {
            mudstate.dumper = child;
            bChildExists = true;
        }
        else if (child == mudstate.dumper)
        {
            // The child process executed and exited before fork() returned to
            // the parent process.  Without a process id, the parent's SIGCHLD
            // handler could not be certain that the pid of the exiting
            // process would match the pid of this child.
            //
            // However, at the this point, we can be sure.  But, there's
            // nothing much left to do.
            //
            // See SIGCHLD handler in bsd.cpp.
            //
            mudstate.dumper = 0;

            // There is no child (bChildExists == false), we aren't dumping
            // (mudstate.dumping == false) and there is no outstanding dumper
            // process (mudstate.dumper == 0).
        }
#endif
    }

#ifndef WIN32
    mudstate.write_protect = false;
    if (!bChildExists)
    {
        // We have the ability to fork children, but we are not configured to
        // use it; or, we tried to fork a child and failed; or, we didn't
        // need to dump the structure or a flatfile; or, the child has finished
        // dumping already.
        //
        mudstate.dumper = 0;
        mudstate.dumping = false;
        local_dump_complete_signal();
    }
    bRequestAccepted = false;
#endif

    if (*mudconf.postdump_msg)
    {
        raw_broadcast(0, "%s", mudconf.postdump_msg);
    }
}

#define LOAD_GAME_SUCCESS           0
#define LOAD_GAME_NO_INPUT_DB     (-1)
#define LOAD_GAME_CANNOT_OPEN     (-2)
#define LOAD_GAME_LOADING_PROBLEM (-3)

#ifdef MEMORY_BASED
static int load_game(void)
#else // MEMORY_BASED
static int load_game(int ccPageFile)
#endif // MEMORY_BASED
{
    FILE *f = NULL;
    char infile[SIZEOF_PATHNAME+8];
    struct stat statbuf;
    int db_format, db_version, db_flags;

    bool compressed = false;

    if (mudconf.compress_db)
    {
        strcpy(infile, mudconf.indb);
        strcat(infile, ".gz");
        if (stat(infile, &statbuf) == 0)
        {
            f = popen(tprintf(" %s < %s", mudconf.uncompress, infile), POPEN_READ_OP);
            if (f != NULL)
            {
                DebugTotalFiles++;
                compressed = true;
            }
        }
    }

    if (!compressed)
    {
        strcpy(infile, mudconf.indb);
        if (stat(infile, &statbuf) != 0)
        {
            // Indicate that we couldn't load because the input db didn't
            // exist.
            //
            return LOAD_GAME_NO_INPUT_DB;
        }
        if ((f = fopen(infile, "rb")) == NULL)
        {
            return LOAD_GAME_CANNOT_OPEN;
        }
        DebugTotalFiles++;
        setvbuf(f, NULL, _IOFBF, 16384);
    }

    // Ok, read it in.
    //
    STARTLOG(LOG_STARTUP, "INI", "LOAD")
    log_text("Loading: ");
    log_text(infile);
    ENDLOG
    if (db_read(f, &db_format, &db_version, &db_flags) < 0)
    {
        // Everything is not ok.
        //
        if (compressed)
        {
            if (pclose(f) != -1)
            {
                DebugTotalFiles--;
            }
        }
        else
        {
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
        }
        f = 0;

        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text("Error loading ");
        log_text(infile);
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }

    // Everything is ok.
    //
    if (compressed)
    {
        if (pclose(f) != -1)
        {
            DebugTotalFiles--;
        }
    }
    else
    {
        if (fclose(f) == 0)
        {
            DebugTotalFiles--;
        }
    }
    f = 0;

#ifndef MEMORY_BASED
    if (db_flags & V_DATABASE)
    {
        // It loaded an output file.
        //
        if (ccPageFile == HF_OPEN_STATUS_NEW)
        {
            STARTLOG(LOG_STARTUP, "INI", "LOAD");
            log_text("Attributes are not present in either the input file or the attribute database.");
            ENDLOG;
        }
    }
    else
    {
        // It loaded a flatfile.
        //
        if (ccPageFile == HF_OPEN_STATUS_OLD)
        {
            STARTLOG(LOG_STARTUP, "INI", "LOAD");
            log_text("Attributes present in both the input file and the attribute database.");
            ENDLOG;
        }
    }
#endif // !MEMORY_BASED

    if (mudconf.have_comsys)
    {
        load_comsys(mudconf.comsys_db);
    }

    if (mudconf.have_mailer)
    {
        f = fopen(mudconf.mail_db, "rb");
        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, NULL, _IOFBF, 16384);
            Log.tinyprintf("LOADING: %s" ENDLINE, mudconf.mail_db);
            load_mail(f);
            Log.tinyprintf("LOADING: %s (done)" ENDLINE, mudconf.mail_db);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
            f = 0;
        }
    }
    STARTLOG(LOG_STARTUP, "INI", "LOAD");
    log_text("Load complete.");
    ENDLOG;

    return LOAD_GAME_SUCCESS;
}


/*
 * match a list of things, using the no_command flag
 *
 * This seems to be always called with type == AMATCH_CMD...
 * So the fact that ansi_strip is done within atr_match only
 * brings about a if () performance hit...
 *
 */

bool list_check
(
    dbref thing,
    dbref player,
    char  type,
    char  *str,
    char  *raw_str,
    bool check_parent
)
{
    bool bMatch = false;

    int limit = mudstate.db_top;
    while (NOTHING != thing)
    {
#ifdef REALITY_LVLS
        if ((thing != player)
           && (!(No_Command(thing)))
           && IsReal(thing, player))
#else
        if (  thing != player
           && !No_Command(thing))
#endif /* REALITY_LVLS */
        {
            bMatch |= atr_match(thing, player, type, str, raw_str, check_parent);
        }

        // Non-authoritative test of circular reference.
        //
        dbref next;
        if (  thing == (next = Next(thing))
           || --limit < 0
           || MuxAlarm.bAlarmed)
        {
            break;
        }
        thing = next;
    }
    return bMatch;
}

bool Hearer(dbref thing)
{
    if (  mudstate.inpipe
       && thing == mudstate.poutobj)
    {
        return true;
    }

    if (  Connected(thing)
       || Puppet(thing)
       || H_Listen(thing)
       || mudstate.bfListens.IsSet(thing))
    {
        return true;
    }

    if (  !mudstate.bfNoListens.IsSet(thing)
       && Monitor(thing))
    {
        bool bFoundCommands = false;

        char *buff = alloc_lbuf("Hearer");
        char *as;
        atr_push();
        for (int atr = atr_head(thing, &as); atr; atr = atr_next(&as))
        {
            ATTR *ap = atr_num(atr);
            if (  !ap
               || (ap->flags & AF_NOPROG))
            {
                continue;
            }

            int   aflags;
            dbref aowner;
            atr_get_str(buff, thing, atr, &aowner, &aflags);

            if (aflags & AF_NOPROG)
            {
                continue;
            }

            char *s = NULL;
            if (  AMATCH_CMD    == buff[0]
               || AMATCH_LISTEN == buff[0])
            {
                s = strchr(buff+1, ':');
                if (s)
                {
                    if (AMATCH_CMD == buff[0])
                    {
                        bFoundCommands = true;
                    }
                    else
                    {
                        free_lbuf(buff);
                        atr_pop();
                        mudstate.bfListens.Set(thing);
                        return true;
                    }
                }
            }
        }
        free_lbuf(buff);
        atr_pop();

        mudstate.bfNoListens.Set(thing);

        if (bFoundCommands)
        {
            mudstate.bfNoCommands.Clear(thing);
            mudstate.bfCommands.Set(thing);
        }
        else
        {
            mudstate.bfCommands.Clear(thing);
            mudstate.bfNoCommands.Set(thing);
        }
    }
    return false;
}

void do_readcache(dbref executor, dbref caller, dbref enactor, int key)
{
    helpindex_load(executor);
    fcache_load(executor);
}

static void process_preload(void)
{
    dbref thing, parent, aowner;
    int aflags, lev, i;
    char *tstr;
    FWDLIST *fp;

    fp = (FWDLIST *) alloc_lbuf("process_preload.fwdlist");
    tstr = alloc_lbuf("process_preload.string");
    i = 0;
    DO_WHOLE_DB(thing)
    {
        // Ignore GOING objects.
        //
        if (Going(thing))
        {
            continue;
        }

        scheduler.RunTasks(10);

        // Look for a STARTUP attribute in parents.
        //
        if (mudconf.run_startup)
        {
            ITER_PARENTS(thing, parent, lev)
            {
                if (Flags(thing) & HAS_STARTUP)
                {
                    did_it(Owner(thing), thing, 0, NULL, 0, NULL, A_STARTUP,
                        (char **)NULL, 0);

                    // Process queue entries as we add them.
                    //
                    scheduler.RunTasks(10);
                    break;
                 }
            }
        }

        // Look for a FORWARDLIST attribute.
        //
        if (H_Fwdlist(thing))
        {
            atr_get_str(tstr, thing, A_FORWARDLIST, &aowner, &aflags);
            if (*tstr)
            {
                fwdlist_load(fp, GOD, tstr);
                if (fp->count > 0)
                {
                    fwdlist_set(thing, fp);
                }
            }
        }
    }
    free_lbuf(fp);
    free_lbuf(tstr);
}

#ifndef MEMORY_BASED

/*
 * ---------------------------------------------------------------------------
 * * info: display info about the file being read or written.
 */

void info(int fmt, int flags, int ver)
{
    const char *cp;

    if (fmt == F_MUX)
    {
        cp = "MUX";
    }
    else
    {
        cp = "*unknown*";
    }
    Log.tinyprintf("%s version %d:", cp, ver);
    if ((flags & MANDFLAGS) != MANDFLAGS)
    {
        Log.WriteString(" Unsupported flags");
    }
    if (flags & V_DATABASE)
        Log.WriteString(" Database");
    if (flags & V_ATRNAME)
        Log.WriteString(" AtrName");
    if (flags & V_ATRKEY)
        Log.WriteString(" AtrKey");
    if (flags & V_ATRMONEY)
        Log.WriteString(" AtrMoney");
    Log.WriteString("\n");
}

char *standalone_infile = NULL;
char *standalone_outfile = NULL;
char *standalone_basename = NULL;
bool standalone_check = false;
bool standalone_load = false;
bool standalone_unload = false;

void dbconvert(void)
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags;

    Log.SetBasename("-");
    Log.StartLogging();

    SeedRandomNumberGenerator();

    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));

    cf_init();

    // Decide what conversions to do and how to format the output file.
    //
    setflags = clrflags = ver = 0;
    bool do_redirect = false;

    bool do_write = true;
    if (standalone_check)
    {
        do_write = false;
    }
    if (standalone_load)
    {
        clrflags = 0xffffffff;
        setflags = OUTPUT_FLAGS;
        ver = OUTPUT_VERSION;
        do_redirect = true;
    }
    else if (standalone_unload)
    {
        clrflags = 0xffffffff;
        setflags = UNLOAD_FLAGS;
        ver = UNLOAD_VERSION;
    }

    // Open the database
    //
    init_attrtab();

    char dirfile[SIZEOF_PATHNAME];
    char pagfile[SIZEOF_PATHNAME];
    strcpy(dirfile, standalone_basename);
    strcat(dirfile, ".dir");
    strcpy(pagfile, standalone_basename);
    strcat(pagfile, ".pag");

    int cc = init_dbfile(dirfile, pagfile, 650);
    if (cc == HF_OPEN_STATUS_ERROR)
    {
        Log.tinyprintf("Can't open database in (%s, %s) files\n", dirfile, pagfile);
        exit(1);
    }
    else if (cc == HF_OPEN_STATUS_OLD)
    {
        if (setflags == OUTPUT_FLAGS)
        {
            Log.tinyprintf("Would overwrite existing database (%s, %s)\n", dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }
    else if (cc == HF_OPEN_STATUS_NEW)
    {
        if (setflags == UNLOAD_FLAGS)
        {
            Log.tinyprintf("Database (%s, %s) is empty.\n", dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }

    FILE *fpIn = fopen(standalone_infile, "rb");
    if (!fpIn)
    {
        exit(1);
    }

    // Go do it.
    //
    if (do_redirect)
    {
        extern void cache_redirect(void);
        cache_redirect();
    }
    setvbuf(fpIn, NULL, _IOFBF, 16384);
    db_read(fpIn, &db_format, &db_ver, &db_flags);
    if (do_redirect)
    {
        extern void cache_pass2(void);
        cache_pass2();
    }
    Log.WriteString("Input: ");
    info(db_format, db_flags, db_ver);

    if (standalone_check)
    {
        do_dbck(NOTHING, NOTHING, NOTHING, DBCK_FULL);
    }
    fclose(fpIn);

    if (do_write)
    {
        FILE *fpOut = fopen(standalone_outfile, "wb");
        if (!fpOut)
        {
            exit(1);
        }

        db_flags = (db_flags & ~clrflags) | setflags;
        if (db_format != F_MUX)
        {
            db_ver = 3;
        }
        if (ver != 0)
        {
            db_ver = ver;
        }
        Log.WriteString("Output: ");
        info(F_MUX, db_flags, db_ver);
        setvbuf(fpOut, NULL, _IOFBF, 16384);
#ifndef MEMORY_BASED
        // Save cached modified attribute list
        //
        al_store();
#endif // MEMORY_BASED
        db_write(fpOut, F_MUX, db_ver | db_flags);
        fclose(fpOut);
    }
    CLOSE;
    db_free();
    exit(0);
}
#endif // MEMORY_BASED

void write_pidfile(const char *pFilename)
{
    FILE *fp = fopen(pFilename, "wb");
    if (fp)
    {
        fprintf(fp, "%d" ENDLINE, game_pid);
        fclose(fp);
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "PID", "FAIL");
        Log.tinyprintf("Failed to write pidfile %s\n", pFilename);
        ENDLOG;
    }
}

long DebugTotalFiles = 3;
long DebugTotalSockets = 0;
#ifdef WIN32
long DebugTotalThreads = 1;
long DebugTotalSemaphores = 0;
#endif
#ifdef MEMORY_ACCOUNTING
long DebugTotalMemory = 0;
#endif

#define CLI_DO_CONFIG_FILE CLI_USER+0
#define CLI_DO_MINIMAL     CLI_USER+1
#define CLI_DO_VERSION     CLI_USER+2
#define CLI_DO_USAGE       CLI_USER+3
#define CLI_DO_INFILE      CLI_USER+4
#define CLI_DO_OUTFILE     CLI_USER+5
#define CLI_DO_CHECK       CLI_USER+6
#define CLI_DO_LOAD        CLI_USER+7
#define CLI_DO_UNLOAD      CLI_USER+8
#define CLI_DO_BASENAME    CLI_USER+9
#define CLI_DO_PID_FILE    CLI_USER+10
#define CLI_DO_ERRORPATH   CLI_USER+11

bool bMinDB = false;
bool bSyntaxError = false;
char *conffile = NULL;
bool bVersion = false;
char *pErrorBasename = "";
bool bServerOption = false;

#ifdef MEMORY_BASED
#define NUM_CLI_OPTIONS 6
#else
#define NUM_CLI_OPTIONS 12
#endif

CLI_OptionEntry OptionTable[NUM_CLI_OPTIONS] =
{
    { "c", CLI_REQUIRED, CLI_DO_CONFIG_FILE },
    { "s", CLI_NONE,     CLI_DO_MINIMAL     },
    { "v", CLI_NONE,     CLI_DO_VERSION     },
    { "h", CLI_NONE,     CLI_DO_USAGE       },
#ifndef MEMORY_BASED
    { "i", CLI_REQUIRED, CLI_DO_INFILE      },
    { "o", CLI_REQUIRED, CLI_DO_OUTFILE     },
    { "k", CLI_NONE,     CLI_DO_CHECK       },
    { "l", CLI_NONE,     CLI_DO_LOAD        },
    { "u", CLI_NONE,     CLI_DO_UNLOAD      },
    { "d", CLI_REQUIRED, CLI_DO_BASENAME    },
#endif // MEMORY_BASED
    { "p", CLI_REQUIRED, CLI_DO_PID_FILE    },
    { "e", CLI_REQUIRED, CLI_DO_ERRORPATH   }
};

void CLI_CallBack(CLI_OptionEntry *p, char *pValue)
{
    if (p)
    {
        switch (p->m_Unique)
        {
        case CLI_DO_PID_FILE:
            bServerOption = true;
            mudconf.pid_file = pValue;
            break;

        case CLI_DO_CONFIG_FILE:
            bServerOption = true;
            conffile = pValue;
            break;

        case CLI_DO_MINIMAL:
            bServerOption = true;
            bMinDB = true;
            break;

        case CLI_DO_VERSION:
            bServerOption = true;
            bVersion = true;
            break;

        case CLI_DO_ERRORPATH:
            bServerOption = true;
            pErrorBasename = pValue;
            break;

#ifndef MEMORY_BASED
        case CLI_DO_INFILE:
            mudstate.bStandAlone = true;
            standalone_infile = pValue;
            break;

        case CLI_DO_OUTFILE:
            mudstate.bStandAlone = true;
            standalone_outfile = pValue;
            break;

        case CLI_DO_CHECK:
            mudstate.bStandAlone = true;
            standalone_check = true;
            break;

        case CLI_DO_LOAD:
            mudstate.bStandAlone = true;
            standalone_load = true;
            break;

        case CLI_DO_UNLOAD:
            mudstate.bStandAlone = true;
            standalone_unload = true;
            break;

        case CLI_DO_BASENAME:
            mudstate.bStandAlone = true;
            standalone_basename = pValue;
            break;
#endif

        case CLI_DO_USAGE:
        default:
            bSyntaxError = true;
            break;
        }
    }
    else
    {
        bSyntaxError = true;
    }
}

#if defined(__INTEL_COMPILER)

extern "C" unsigned int __intel_cpu_indicator;

#define CPU_FD_ID 0x00200000UL

#define CPUID_0 0

// GenuineIntel
//
#define INTEL_MFGSTR0 'uneG'
#define INTEL_MFGSTR1 'letn'
#define INTEL_MFGSTR2 'Ieni'

// AuthenticAMD
//
#define AMD_MFGSTR0   'htuA'
#define AMD_MFGSTR1   'DMAc'
#define AMD_MFGSTR2   'itne'

#define CPUID_1 1

#define CPU_STEPPING(x)  ((x      ) & 0x00000000F)
#define CPU_MODEL(x)     ((x >>  4) & 0x00000000F)
#define CPU_FAMILY(x)    ((x >>  8) & 0x00000000F)
#define CPU_TYPE(x)      ((x >> 12) & 0x00000000F)
#define CPU_EXTMODEL(x)  ((x >> 16) & 0x00000000F)
#define CPU_EXTFAMILY(x) ((x >> 20) & 0x00000000F)

#define CPU_FEATURE_MMX  0x00800000UL
#define CPU_FEATURE_FSXR 0x01000000UL
#define CPU_FEATURE_SSE  0x02000000UL
#define CPU_FEATURE_SSE2 0x04000000UL
#define CPU_MSR_SSE3     0x00000001UL

// Indicators.
//
// OLDOS tags indicate that the CPU supports SSE[n], but the operating system
// will throw an exception when they are used.
//
//
#define CPU_TYPE_UNSPECIALIZED               0x00000001UL
#define CPU_TYPE_FAMILY_5                    0x00000002UL
#define CPU_TYPE_FAMILY_6                    0x00000004UL
#define CPU_TYPE_FAMILY_5_MMX                0x00000008UL
#define CPU_TYPE_FAMILY_6_MMX                0x00000010UL
#define CPU_TYPE_FAMILY_6_MMX_FSXR           0x00000020UL
#define CPU_TYPE_FAMILY_6_MMX_FSXR_SSE_OLDOS 0x00000040UL
#define CPU_TYPE_FAMILY_6_MMX_FSXR_SSE       0x00000080UL
#define CPU_TYPE_FAMILY_F_SSE2_OLDOS         0x00000100UL
#define CPU_TYPE_FAMILY_F_SSE2               0x00000200UL
#define CPU_TYPE_FAMILY_6_MMX_FSXR_SSE2      0x00000400UL
#define CPU_TYPE_FAMILY_6_MMX_FSXR_SSE3      0x00000800UL
#define CPU_TYPE_FAMILY_F_SSE3               0x00000800UL

void cpu_init(void)
{
    UINT32 dwCPUID;

    // Determine whether CPUID instruction is supported.
    //
    __asm
    {
        // Obtain a copy of the flags register.
        //
        pushfd
        pop     eax
        mov     dwCPUID,eax

        // Attempt to flip the ID bit.
        //
        xor     eax,CPU_FD_ID
        push    eax
        popfd

        // Obtain a second copy of the flags register.
        //
        pushfd
        pop     eax
        xor     dwCPUID,eax
    }

    // If the ID bit didn't toggle, the CPUID instruction is not supported.
    //
    if (CPU_FD_ID != dwCPUID)
    {
        // CPUID instruction is not supported.
        //
        __intel_cpu_indicator = CPU_TYPE_UNSPECIALIZED;
        return;
    }

    UINT32 dwHighest;
    UINT32 dwMfgStr0;
    UINT32 dwMfgStr1;
    UINT32 dwMfgStr2;

    // CPUID is supported.
    //
    __asm
    {
        mov eax,CPUID_0
        cpuid
        mov dwHighest,eax
        mov dwMfgStr0,ebx
        mov dwMfgStr1,ecx
        mov dwMfgStr2,edx
    }

    if (0 == dwHighest)
    {
        // We can't decipher anything with only CPUID (EAX=$0) available.
        //
        __intel_cpu_indicator = CPU_TYPE_UNSPECIALIZED;
        return;
    }

    typedef enum
    {
        Intel = 0,
        AMD
    } CPUMaker;

    CPUMaker maker;
    if (  INTEL_MFGSTR0 == dwMfgStr0
       && INTEL_MFGSTR1 == dwMfgStr1
       && INTEL_MFGSTR2 == dwMfgStr2)
    {
        maker = Intel;
    }
    else if (  AMD_MFGSTR0 == dwMfgStr0
            && AMD_MFGSTR1 == dwMfgStr1
            && AMD_MFGSTR2 == dwMfgStr2)
    {
        maker = AMD;
    }
    else
    {
        // It's not Intel or AMD.
        //
        __intel_cpu_indicator = CPU_TYPE_UNSPECIALIZED;
        return;
    }

    UINT32 dwSignature;
    UINT32 dwBrand;
    UINT32 dwMSR;
    UINT32 dwFeatures;

    __asm
    {
        mov eax,CPUID_1
        cpuid
        mov dwSignature,eax
        mov dwBrand,ebx
        mov dwMSR,ecx
        mov dwFeatures,edx
    }

    // Develop 'Effective' Family and Model.
    //
    UINT32 dwEffFamily;
    if (CPU_FAMILY(dwSignature) == 0xF)
    {
        dwEffFamily = CPU_FAMILY(dwSignature) + CPU_EXTFAMILY(dwSignature);
    }
    else
    {
        dwEffFamily = CPU_FAMILY(dwSignature);
    }
    UINT32 dwEffModel;
    if (CPU_MODEL(dwSignature) == 0xF)
    {
        dwEffModel = CPU_MODEL(dwSignature) + (CPU_EXTMODEL(dwSignature) << 4);
    }
    else
    {
        dwEffModel = CPU_MODEL(dwSignature);
    }

#define ADVF_MMX  0x00000001UL
#define ADVF_FSXR 0x00000002UL
#define ADVF_SSE  0x00000004UL
#define ADVF_SSE2 0x00000008UL
#define ADVF_SSE3 0x00000010UL

    UINT32 dwAdvFeatures = 0;

    // Decode the features the chips claim to possess.
    //
    if (dwFeatures & CPU_FEATURE_MMX)
    {
        dwAdvFeatures |= ADVF_MMX;
    }
    if (dwFeatures & CPU_FEATURE_FSXR)
    {
        dwAdvFeatures |= ADVF_FSXR;
    }
    if (dwFeatures & CPU_FEATURE_SSE)
    {
        dwAdvFeatures |= ADVF_SSE;
    }
    if (dwFeatures & CPU_FEATURE_SSE2)
    {
        dwAdvFeatures |= ADVF_SSE2;
    }
    if (  dwEffFamily <= 5
       && dwMSR & CPU_MSR_SSE3)
    {
        dwAdvFeatures |= ADVF_SSE3;
    }

    // Test whether operating system will allow use of these extensions.
    //
    UINT32 dwUseable = dwAdvFeatures;
    if (dwUseable & ADVF_MMX)
    {
        try
        {
            __asm
            {
                // Let's try a MMX instruction.
                //
                emms
            }
        }
        catch (...)
        {
            dwUseable &= ~(ADVF_MMX|ADVF_SSE|ADVF_SSE2|ADVF_SSE3);
        }
    }

    if (dwUseable & ADVF_SSE)
    {
        try
        {
            __asm
            {
                // Let's try a SSE instruction.
                //
                xorps xmm0, xmm0
            }
        }
        catch (...)
        {
            dwUseable &= ~(ADVF_SSE|ADVF_SSE2|ADVF_SSE3);
        }
    }

    if (dwUseable & ADVF_SSE2)
    {
        try
        {
            __asm
            {
                // Let's try a SSE2 instruction.
                //
                xorpd xmm0, xmm0
            }
        }
        catch (...)
        {
            dwUseable &= ~(ADVF_SSE2|ADVF_SSE3);
        }
    }

    if (dwUseable & ADVF_SSE3)
    {
        try
        {
            __asm
            {
                // Let's try a SSE3 instruction.
                //
                haddpd xmm1,xmm2
            }
        }
        catch (...)
        {
            dwUseable &= ~(ADVF_SSE3);
        }
    }

    // Map tested features to an indicator for CPU dispatching.
    //
    if (dwEffFamily <= 4)
    {
        __intel_cpu_indicator = CPU_TYPE_UNSPECIALIZED;
    }
    else if (5 == dwEffFamily)
    {
        if (dwUseable & ADVF_MMX)
        {
            __intel_cpu_indicator = CPU_TYPE_FAMILY_5_MMX;
        }
        else
        {
            __intel_cpu_indicator = CPU_TYPE_FAMILY_5;
        }
    }
    else
    {
        if (dwUseable & ADVF_MMX)
        {
            if (dwUseable & ADVF_FSXR)
            {
                if (dwUseable & ADVF_SSE)
                {
                    if (dwUseable & ADVF_SSE2)
                    {
                        if (dwUseable & ADVF_SSE3)
                        {
                            if (dwEffFamily < 15)
                            {
                                __intel_cpu_indicator = CPU_TYPE_FAMILY_6_MMX_FSXR_SSE3;
                            }
                            else
                            {
                                __intel_cpu_indicator = CPU_TYPE_FAMILY_F_SSE3;
                            }
                        }
                        else
                        {
                            if (dwEffFamily < 15)
                            {
                                __intel_cpu_indicator = CPU_TYPE_FAMILY_6_MMX_FSXR_SSE2;
                            }
                            else
                            {
                                __intel_cpu_indicator = CPU_TYPE_FAMILY_F_SSE2;
                            }
                        }
                    }
                    else
                    {
                        __intel_cpu_indicator = CPU_TYPE_FAMILY_6_MMX_FSXR_SSE;
                    }
                }
                else
                {
                    __intel_cpu_indicator = CPU_TYPE_FAMILY_6_MMX_FSXR;
                }
            }
            else
            {
                __intel_cpu_indicator = CPU_TYPE_FAMILY_6_MMX;
            }
        }
        else
        {
            __intel_cpu_indicator = CPU_TYPE_FAMILY_6;
        }
    }

    // Report findings to the log.
    //
    fprintf(stderr, "cpu_init: %s, Family %d, Model %d, %s%s%s%s%s" ENDLINE,
        (Intel == maker ? "Intel" : (AMD == maker ? "AMD" : "Unknown")),
        dwEffFamily,
        dwEffModel,
        (dwUseable & ADVF_MMX)  ? "MMX " : "",
        (dwUseable & ADVF_FSXR) ? "FSXR ": "",
        (dwUseable & ADVF_SSE)  ? "SSE ": "",
        (dwUseable & ADVF_SSE2) ? "SSE2 ": "",
        (dwUseable & ADVF_SSE3) ? "SSE3 ": "");

    if (dwUseable != dwAdvFeatures)
    {
        UINT32 dw = dwAdvFeatures & (~dwUseable);
        fprintf(stderr, "cpu_init: %s%s%s%s%s unsupported by OS." ENDLINE,
            (dw & ADVF_MMX)  ? "MMX ": "",
            (dw & ADVF_FSXR) ? "FSXR ": "",
            (dw & ADVF_SSE)  ? "SSE ": "",
            (dw & ADVF_SSE2) ? "SSE2 ": "",
            (dw & ADVF_SSE3) ? "SSE3 ": "");
    }
}

#endif

#define DBCONVERT_NAME1 "dbconvert"
#define DBCONVERT_NAME2 "dbconvert.exe"

int DCL_CDECL main(int argc, char *argv[])
{
#if defined(__INTEL_COMPILER)
    cpu_init();
#endif

    build_version();

    // Look for dbconvert[.exe] in the program name.
    //
    size_t nProg = strlen(argv[0]);
    const char *pProg = argv[0] + nProg - 1;
    while (  nProg
          && (  mux_isalpha(*pProg)
             || *pProg == '.'))
    {
        nProg--;
        pProg--;
    }
    pProg++;
    mudstate.bStandAlone = false;
    if (  mux_stricmp(pProg, DBCONVERT_NAME1) == 0
       || mux_stricmp(pProg, DBCONVERT_NAME2) == 0)
    {
        mudstate.bStandAlone = true;
    }

    mudconf.pid_file = "netmux.pid";

    // Parse the command line
    //
    CLI_Process(argc, argv, OptionTable,
        sizeof(OptionTable)/sizeof(CLI_OptionEntry), CLI_CallBack);

#ifndef MEMORY_BASED
    if (mudstate.bStandAlone)
    {
        int n = 0;
        if (standalone_check)
        {
            n++;
        }
        if (standalone_load)
        {
            n++;
        }
        if (standalone_unload)
        {
            n++;
        }
        if (  !standalone_basename
           || !standalone_infile
           || !standalone_outfile
           || n != 1
           || bServerOption)
        {
            bSyntaxError = true;
        }
        else
        {
            dbconvert();
            return 0;
        }
    }
    else
#endif // MEMORY_BASED

    if (bVersion)
    {
        fprintf(stderr, "Version: %s" ENDLINE, mudstate.version);
        return 1;
    }
    if (  bSyntaxError
       || conffile == NULL
       || !bServerOption)
    {
        fprintf(stderr, "Version: %s" ENDLINE, mudstate.version);
        if (mudstate.bStandAlone)
        {
            fprintf(stderr, "Usage: %s -d <dbname> -i <infile> [-o <outfile>] [-l|-u|-k]" ENDLINE, pProg);
            fprintf(stderr, "  -d  Basename." ENDLINE);
            fprintf(stderr, "  -i  Input file." ENDLINE);
            fprintf(stderr, "  -k  Check." ENDLINE);
            fprintf(stderr, "  -l  Load." ENDLINE);
            fprintf(stderr, "  -o  Output file." ENDLINE);
            fprintf(stderr, "  -u  Unload." ENDLINE);
        }
        else
        {
            fprintf(stderr, "Usage: %s [-c <filename>] [-p <filename>] [-h] [-s] [-v]" ENDLINE, pProg);
            fprintf(stderr, "  -c  Specify configuration file." ENDLINE);
            fprintf(stderr, "  -e  Specify logfile basename (or '-' for stderr)." ENDLINE);
            fprintf(stderr, "  -h  Display this help." ENDLINE);
            fprintf(stderr, "  -p  Specify process ID file." ENDLINE);
            fprintf(stderr, "  -s  Start with a minimal database." ENDLINE);
            fprintf(stderr, "  -v  Display version string." ENDLINE ENDLINE);
        }
        return 1;
    }

    mudstate.bStandAlone = false;

    FLOAT_Initialize();
    TIME_Initialize();
    SeedRandomNumberGenerator();

    Log.SetBasename(pErrorBasename);
    Log.StartLogging();
    game_pid = getpid();
    write_pidfile(mudconf.pid_file);

    BuildSignalNamesTable();

#ifdef MEMORY_ACCOUNTING
    extern CHashFile hfAllocData;
    extern CHashFile hfIdentData;
    extern bool bMemAccountingInitialized;
    hfAllocData.Open("svdptrs.dir", "svdptrs.pag", 40);
    hfIdentData.Open("svdlines.dir", "svdlines.pag", 40);
    bMemAccountingInitialized = true;
#endif

#ifdef WIN32
    // Find which version of Windows we are using - Completion ports do
    // not work with Windows 95/98

    OSVERSIONINFO VersionInformation;

    VersionInformation.dwOSVersionInfoSize = sizeof (VersionInformation);
    GetVersionEx(&VersionInformation);
    platform = VersionInformation.dwPlatformId;
    hGameProcess = GetCurrentProcess();
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        Log.WriteString("Running under Windows NT" ENDLINE);

        // Get a handle to the kernel32 DLL
        //
        HINSTANCE hInstKernel32 = LoadLibrary("kernel32");
        if (!hInstKernel32)
        {
            Log.WriteString("LoadLibrary of kernel32 for a CancelIo entry point failed. Cannot continue." ENDLINE);
            return 1;
        }

        // Find the entry point for CancelIO so we can use it. This is done
        // dynamically because Windows 95/98 doesn't have a CancelIO entry
        // point. If it were done at load time, it would always fail on
        // Windows 95/98...even though we don't use it or depend on it in
        // that case.
        //
        fpCancelIo = (FCANCELIO *)GetProcAddress(hInstKernel32, "CancelIo");
        if (fpCancelIo == NULL)
        {
            Log.WriteString("GetProcAddress of _CancelIo failed. Cannot continue." ENDLINE);
            return 1;
        }
        fpGetProcessTimes = (FGETPROCESSTIMES *)GetProcAddress(hInstKernel32, "GetProcessTimes");
        if (fpGetProcessTimes == NULL)
        {
            Log.WriteString("GetProcAddress of GetProcessTimes failed. Cannot continue." ENDLINE);
            return 1;
        }
    }
    else
    {
        Log.WriteString("Running under Windows 95/98" ENDLINE);
    }

    // Initialize WinSock.
    //
    WORD wVersionRequested = MAKEWORD(2,2);
    WSADATA wsaData;
    if (WSAStartup(wVersionRequested, &wsaData) != 0)
    {
        Log.WriteString("ERROR: Could not initialize WinSock." ENDLINE);
        return 101;
    }

    if (  LOBYTE(wsaData.wVersion) != 2
       || HIBYTE(wsaData.wVersion) != 2)
    {
        // We can't run on this version of WinSock.
        //
        Log.tinyprintf("INFO: WinSock v%d.%d instead of v2.2." ENDLINE,
            LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
        //WSACleanup();
        //return 102;
    }
    if (!bCryptoAPI)
    {
        Log.WriteString("Crypto API unavailable.\r\n");
    }
#endif // WIN32

    mudstate.start_time.GetLocal();
    mudstate.cpu_count_from.GetUTC();
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));

    pool_init(POOL_DESC, sizeof(DESC));
    pool_init(POOL_QENTRY, sizeof(BQUE));
    tcache_init();
    pcache_init();
    cf_init();
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
    init_rlimit();
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE
    init_cmdtab();
    init_logout_cmdtab();
    init_flagtab();
    init_powertab();
    init_functab();
    init_attrtab();
    init_version();

    mudconf.config_file = StringClone(conffile);
    cf_read();

    fcache_init();
    helpindex_init();

#ifdef MEMORY_BASED
    db_free();
#else // MEMORY_BASED
    if (bMinDB)
    {
        RemoveFile(mudconf.game_dir);
        RemoveFile(mudconf.game_pag);
    }
    int ccPageFile = init_dbfile(mudconf.game_dir, mudconf.game_pag, mudconf.cache_pages);
    if (HF_OPEN_STATUS_ERROR == ccPageFile)
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_text("Couldn't load text database: ");
        log_text(mudconf.game_dir);
        log_text(mudconf.game_pag);
        ENDLOG;
        return 2;
    }
#endif // MEMORY_BASED

    mudstate.record_players = 0;

    if (bMinDB)
    {
        db_make_minimal();
    }
    else
    {
#ifdef MEMORY_BASED
        int ccInFile = load_game();
#else // MEMORY_BASED
        int ccInFile = load_game(ccPageFile);
#endif // MEMORY_BASED
        if (LOAD_GAME_NO_INPUT_DB == ccInFile)
        {
            // The input file didn't exist.
            //
#ifndef MEMORY_BASED
            if (HF_OPEN_STATUS_NEW == ccPageFile)
            {
                // Since the .db file didn't exist, and the .pag/.dir files
                // were newly created, just create a minimal DB.
                //
#endif // !MEMORY_BASED
                db_make_minimal();
                ccInFile = LOAD_GAME_SUCCESS;
#ifndef MEMORY_BASED
            }
#endif // !MEMORY_BASED
        }
        if (ccInFile != LOAD_GAME_SUCCESS)
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD")
            log_text("Couldn't load: ");
            log_text(mudconf.indb);
            ENDLOG
            return 2;
        }
    }
    set_signals();
    Guest.StartUp();

    // Do a consistency check and set up the freelist
    //
    do_dbck(NOTHING, NOTHING, NOTHING, 0);

    // Reset all the hash stats
    //
    hashreset(&mudstate.command_htab);
    hashreset(&mudstate.channel_htab);
    hashreset(&mudstate.mail_htab);
    hashreset(&mudstate.logout_cmd_htab);
    hashreset(&mudstate.func_htab);
    hashreset(&mudstate.flags_htab);
    hashreset(&mudstate.attr_name_htab);
    hashreset(&mudstate.player_htab);
    hashreset(&mudstate.fwdlist_htab);
    hashreset(&mudstate.desc_htab);

    int i;
    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i] = alloc_lbuf("main.global_reg");
        mudstate.glob_reg_len[i] = 0;
    }

    ValidateConfigurationDbrefs();
    process_preload();

#ifndef WIN32
    load_restart_db();
    if (!mudstate.restarting)
#endif // !WIN32
    {
        if (fclose(stdout) == 0)
        {
            DebugTotalFiles--;
        }
        if (fclose(stdin) == 0)
        {
            DebugTotalFiles--;
        }
    }
    SetupPorts(&nMainGamePorts, aMainGamePorts, &mudconf.ports);
    boot_slave(GOD, GOD, GOD, 0);
#ifdef QUERY_SLAVE
    boot_sqlslave(GOD, GOD, GOD, 0);
#endif // QUERY_SLAVE

    // All intialization should be complete, allow the local
    // extensions to configure themselves.
    //
    local_startup();

    init_timer();

#ifdef WIN32
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        process_output = process_outputNT;
        shovecharsNT(nMainGamePorts, aMainGamePorts);
    }
    else
    {
        process_output = process_output9x;
        shovechars9x(nMainGamePorts, aMainGamePorts);
    }
#else // WIN32
    shovechars(nMainGamePorts, aMainGamePorts);
#endif // WIN32

    close_sockets(false, "Going down - Bye");
    dump_database();

    // All shutdown, barring logfiles, should be done, shutdown the
    // local extensions.
    //
    local_shutdown();
    CLOSE;

#ifndef WIN32
    CleanUpSlaveSocket();
    CleanUpSlaveProcess();
#endif
#ifdef QUERY_SLAVE
    CleanUpSQLSlaveSocket();
    CleanUpSQLSlaveProcess();
#endif

    // Go ahead and explicitly free the memory for these things so
    // that it's easy to spot unintentional memory leaks.
    //
    for (i = 0; i < mudstate.nHelpDesc; i++)
    {
        helpindex_clean(i);
    }

    db_free();

#ifdef WIN32
    // critical section not needed any more
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        DeleteCriticalSection(&csDescriptorList);
    }
    WSACleanup();
#endif // WIN32

    return 0;
}

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
static void init_rlimit(void)
{
    struct rlimit *rlp;

    rlp = (struct rlimit *)alloc_lbuf("rlimit");

    if (getrlimit(RLIMIT_NOFILE, rlp))
    {
        log_perror("RLM", "FAIL", NULL, "getrlimit()");
        free_lbuf(rlp);
        return;
    }
    rlp->rlim_cur = rlp->rlim_max;
    if (setrlimit(RLIMIT_NOFILE, rlp))
    {
        log_perror("RLM", "FAIL", NULL, "setrlimit()");
    }
    free_lbuf(rlp);

}
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE
