// game.cpp
//
// $Id: game.cpp,v 1.38 2003-03-04 18:50:35 sdennis Exp $
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

extern void init_attrtab(void);
extern void init_cmdtab(void);
extern void cf_init(void);
extern void pcache_init(void);
extern int  cf_read(void);
extern void ValidateConfigurationDbrefs(void);
extern void init_functab(void);
extern void close_sockets(BOOL emergency, char *message);
extern void build_version(void);
extern void init_version(void);
extern void init_logout_cmdtab(void);
extern void raw_notify(dbref, const char *);
extern void do_dbck(dbref executor, dbref caller, dbref enactor, int);
extern void boot_slave(dbref executor, dbref caller, dbref enactor, int key);

void fork_and_dump(int);
void pcache_sync(void);
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
static void init_rlimit(void);
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

#ifdef WIN32
extern CRITICAL_SECTION csDescriptorList;      // for thread synchronisation
#else
extern pid_t  slave_pid;
extern SOCKET slave_socket;
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

BOOL regexp_match
(
    char *pattern,
    char *str,
    int case_opt,
    char *args[],
    int nargs
)
{
    int matches;
    int i, len;
    const char *errptr;
    int erroffset;
    const int ovecsize = 33;
    int ovec[ovecsize];

    /*
     * Load the regexp pattern. This allocates memory which must be
     * later freed. A free() of the regexp does free all structures
     * under it.
     */

    pcre *re = pcre_compile(pattern, case_opt, &errptr, &erroffset, NULL);
    if (re == NULL)
    {
        /*
         * This is a matching error. We have an error message in
         * regexp_errbuf that we can ignore, since we're doing
         * command-matching.
         */
        return FALSE;
    }

    /*
     * Now we try to match the pattern. The relevant fields will
     * automatically be filled in by this.
     */
    matches = pcre_exec(re, NULL, str, strlen(str), 0, 0, ovec, ovecsize);
    if (matches <= 0)
    {
        MEMFREE(re);
        return FALSE;
    }

    /*
     * Now we fill in our args vector. Note that in regexp matching,
     * 0 is the entire string matched, and the parenthesized strings
     * go from 1 to 9. We DO PRESERVE THIS PARADIGM, for consistency
     * with other languages.
     */

    for (i = 0; i < nargs; i++)
    {
        args[i] = NULL;
    }

    /* Convenient: nargs and NSUBEXP are the same.
     * We are also guaranteed that our buffer is going to be LBUF_SIZE
     * so we can copy without fear.
     */

    for (i = 0; i < matches; ++i)
    {
        if (ovec[i*2] == -1)
        {
            continue;
        }
        len = ovec[(i*2)+1] - ovec[i*2];
        args[i] = alloc_lbuf("regexp_match");
        strncpy(args[i], str + ovec[i*2], len);
        args[i][len] = '\0';        /* strncpy() does not null-terminate */
    }

    MEMFREE(re);
    return TRUE;
}


/* ----------------------------------------------------------------------
 * atr_match: Check attribute list for wild card matches and queue them.
 */

static int atr_match1(dbref thing, dbref parent, dbref player, char type,
     char *str, int check_exclude, int hash_insert)
{
    // See if we can do it.  Silently fail if we can't.
    //
    if (!could_doit(player, parent, A_LUSE))
    {
        return -1;
    }

    int match = 0;
    int attr;
    char *as;
    atr_push();
    for (attr = atr_head(parent, &as); attr; attr = atr_next(&as))
    {
        ATTR *ap = atr_num(attr);

        // Never check NOPROG attributes.
        //
        if (  !ap
           || (ap->flags & AF_NOPROG))
        {
            continue;
        }

        // If we aren't the bottom level check if we saw this attr
        // before. Also exclude it if the attribute type is PRIVATE.
        //
        if (  check_exclude
           && (  (ap->flags & AF_PRIVATE)
              || hashfindLEN(&(ap->number), sizeof(ap->number), &mudstate.parent_htab)))
        {
            continue;
        }
        dbref aowner;
        int   aflags;
        char buff[LBUF_SIZE];
        atr_get_str(buff, parent, attr, &aowner, &aflags);

        // Skip if private and on a parent.
        //
        if (  check_exclude
           && (aflags & AF_PRIVATE))
        {
            continue;
        }

        // If we aren't the top level remember this attr so we
        // exclude it from now on.
        //
        if (hash_insert)
        {
            hashaddLEN(&(ap->number), sizeof(ap->number), (int *)&attr, &mudstate.parent_htab);
        }

        // Check for the leadin character after excluding the attrib.
        // This lets non-command attribs on the child block commands
        // on the parent.
        //
        if (  buff[0] != type
           || (aflags & AF_NOPROG))
        {
            continue;
        }

        // Decode it: search for first unescaped :
        //
        char *s = strchr(buff+1, ':');
        if (!s)
        {
            continue;
        }
        *s++ = '\0';
        char *args[NUM_ENV_VARS];
        if (  (  (aflags & AF_REGEXP)
              && regexp_match(buff + 1, str,
                     ((aflags & AF_CASE) ? 0 : PCRE_CASELESS), args, NUM_ENV_VARS))
           || (  (aflags & AF_REGEXP) == 0
              && wild(buff + 1, str, args, NUM_ENV_VARS)))
        {
            match = 1;
            CLinearTimeAbsolute lta;
            wait_que(thing, player, player, FALSE, lta, NOTHING, 0, s,
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
    return (match);
}

int atr_match(dbref thing, dbref player, char type, char *str, BOOL check_parents)
{
    int lev, result;
    BOOL exclude, insert;
    dbref parent;

    // If thing is halted, don't check anything
    //
    if (Halted(thing))
    {
        return FALSE;
    }

    // If not checking parents, just check the thing
    //
    BOOL match = FALSE;
    if (!check_parents)
    {
        return atr_match1(thing, thing, player, type, str, FALSE, FALSE);
    }

    // Check parents, ignoring halted objects
    //
    exclude = FALSE;
    insert = TRUE;
    hashflush(&mudstate.parent_htab);
    ITER_PARENTS(thing, parent, lev)
    {
        if (!Good_obj(Parent(parent)))
        {
            insert = FALSE;
        }
        result = atr_match1(thing, parent, player, type, str, exclude, insert);
        if (result > 0)
        {
            match = TRUE;
        }
        else if (result < 0)
        {
            return match;
        }
        exclude = TRUE;
    }
    return match;
}

/* ---------------------------------------------------------------------------
 * notify_check: notifies the object #target of the message msg, and
 * optionally notify the contents, neighbors, and location also.
 */

BOOL check_filter(dbref object, dbref player, int filter, const char *msg)
{
    int aflags;
    dbref aowner;
    char *buf, *nbuf, *cp, *dp, *str;

    buf = atr_pget(object, filter, &aowner, &aflags);
    if (!*buf)
    {
        free_lbuf(buf);
        return TRUE;
    }
    char **preserve = NULL;
    int *preserve_len = NULL;
    preserve = PushPointers(MAX_GLOBAL_REGS);
    preserve_len = PushIntegers(MAX_GLOBAL_REGS);
    save_global_regs("check_filter_save", preserve, preserve_len);
    nbuf = dp = alloc_lbuf("check_filter");
    str = buf;
    TinyExec(nbuf, &dp, object, player, player,
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
            if (quick_wild(cp, msg))
            {
                free_lbuf(nbuf);
                return FALSE;
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
            pcre *re = pcre_compile(cp, case_opt, &errptr, &erroffset, NULL);
            if (re != NULL)
            {
                const int ovecsize = 33;
                int ovec[ovecsize];
                int matches = pcre_exec(re, NULL, msg, strlen(msg), 0, 0,
                    ovec, ovecsize);
                if (0 <= matches)
                {
                    MEMFREE(re);
                    free_lbuf(nbuf);
                    return FALSE;
                }
                MEMFREE(re);
            }
        } while (dp != NULL);
    }
    free_lbuf(nbuf);
    return TRUE;
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
        TinyExec(nbuf, &cp, object, player, player,
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
BOOL html_escape(const char *src, char *dest, char **destp)
{
    const char *msg_orig;
    BOOL ret = FALSE;

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
            ret = TRUE;
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
    BOOL check_listens = !Halted(target);
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
            check_listens = FALSE;
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
        BOOL has_neighbors = Has_location(target);
        dbref targetloc = where_is(target);
        BOOL is_audible = Audible(target);

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
        BOOL pass_listen = FALSE;
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
                pass_listen = TRUE;
            }
            free_lbuf(tp);
        }

        // If we matched the @listen or are monitoring, check the
        // USE lock.
        //
        BOOL pass_uselock = FALSE;
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
           && pass_uselock)
        {
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
            atr_match(target, sender, AMATCH_LISTEN, (char *)msg, FALSE);
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
    BOOL yes_screen,
    BOOL yes_log,
    BOOL yes_clear
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
        yes_log = FALSE;
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
    BOOL yes_screen, yes_log, yes_clear;

    yes_screen = yes_log = yes_clear = FALSE;

    if (key == 0)
    {
        // No switches, default to printing to screen and clearing counters.
        //
        yes_screen = TRUE;
        yes_clear = TRUE;
    }
    else
    {
        if (key & TIMECHK_RESET)
        {
            yes_clear = TRUE;
        }
        if (key & TIMECHK_SCREEN)
        {
            yes_screen = TRUE;
        }
        if (key & TIMECHK_LOG)
        {
            yes_log = TRUE;
        }
    }
    report_timecheck(executor, yes_screen, yes_log, yes_clear);
}

void do_shutdown(dbref executor, dbref caller, dbref enactor, int key, char *message)
{
    if (Good_obj(executor))
    {
        raw_broadcast(0, "GAME: Shutdown by %s", Name(Owner(executor)));
        STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN");
        log_text("Shutdown by ");
        log_name(executor);
        ENDLOG;
    }
    else
    {
        raw_broadcast(0, "GAME: %s", message);
    }
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

        // Close the attribute text db and dump the header db.
        //
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
    mudstate.shutdown_flag = TRUE;
}

#ifndef STANDALONE

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
    BOOL bUseTemporary;
    int  fType;
    char *pszErrorMessage;
} DUMP_PROCEDURE;

DUMP_PROCEDURE DumpProcedures[NUM_DUMP_TYPES] =
{
    { 0,                ""       , FALSE, 0,                             "" }, // 0 -- Handled specially.
    { &mudconf.crashdb, ""       , FALSE, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening crash file" }, // 1
    { &mudconf.indb,    ""       , TRUE,  OUTPUT_VERSION | OUTPUT_FLAGS, "Opening input file" }, // 2
    { &mudconf.indb,   ".FLAT"   , FALSE, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening flatfile"   }, // 3
    { &mudconf.indb,   ".SIG"    , FALSE, UNLOAD_VERSION | UNLOAD_FLAGS, "Opening signalled flatfile"}  // 4
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

    BOOL bPotentialConflicts = FALSE;
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
        bPotentialConflicts = TRUE;
    }
#endif

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
#endif // !STANDALONE

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
            sleep(1);
        }
    }
    mudstate.dumping = TRUE;
#endif
    buff = alloc_mbuf("dump_database");
    sprintf(buff, "%s.#%d#", mudconf.outdb, mudstate.epoch);

    STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
    log_text("Dumping: ");
    log_text(buff);
    ENDLOG;
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
    mudstate.dumping = FALSE;
#endif
}

void fork_and_dump(int key)
{
#ifndef WIN32
    // fork_and_dump is never called with mudstate.dumping TRUE, but we'll
    // ensure assertion now.
    //
    if (mudstate.dumping)
    {
        return;
    }
    mudstate.dumping = TRUE;
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
#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    if (key & DUMP_TEXT)
    {
        pcache_sync();
    }
    SYNC;

    int child = 0;
    BOOL bChildExists = FALSE;
    if (key & (DUMP_STRUCT|DUMP_FLATFILE))
    {
#ifndef WIN32
        if (mudconf.fork_dump)
        {
            child = fork();
        }
#endif
        if (child == 0)
        {
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
#endif
        }
        else if (child < 0)
        {
            log_perror("DMP", "FORK", NULL, "fork()");
        }
        else
        {
            bChildExists = TRUE;
        }
    }

#ifndef WIN32
    if (!bChildExists)
    {
        // We have the ability to fork children, but we are not configured to
        // use it; or, we tried to fork a child and failed; or, we didn't
        // need to dump the structure or a flatfile.
        //
        mudstate.dumping = FALSE;
    }
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

    BOOL compressed = FALSE;

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
                compressed = TRUE;
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
 */

BOOL list_check(dbref thing, dbref player, char type, char *str, BOOL check_parent)
{
    BOOL match = FALSE;

    int limit = mudstate.db_top;
    while (thing != NOTHING)
    {
        if (  thing != player
           && !No_Command(thing))
        {
            if (atr_match(thing, player, type, str, check_parent) > 0)
            {
                match = TRUE;
            }
        }
        thing = Next(thing);
        if (--limit < 0)
        {
            return match;
        }
    }
    return match;
}

BOOL Hearer(dbref thing)
{
    char *as, *buff, *s;
    dbref aowner;
    int attr, aflags;
    ATTR *ap;

    if (  mudstate.inpipe
       && thing == mudstate.poutobj)
    {
        return TRUE;
    }

    if (  Connected(thing)
       || Puppet(thing))
    {
        return TRUE;
    }

    if (Monitor(thing))
    {
        buff = alloc_lbuf("Hearer");
    }
    else
    {
        buff = NULL;
    }
    atr_push();
    for (attr = atr_head(thing, &as); attr; attr = atr_next(&as))
    {
        if (attr == A_LISTEN)
        {
            if (buff)
            {
                free_lbuf(buff);
            }
            atr_pop();
            return TRUE;
        }
        if (Monitor(thing))
        {
            ap = atr_num(attr);
            if (  !ap
               || (ap->flags & AF_NOPROG))
            {
                continue;
            }

            atr_get_str(buff, thing, attr, &aowner, &aflags);

            // Make sure we can execute it.
            //
            if (  buff[0] != AMATCH_LISTEN
               || (aflags & AF_NOPROG))
            {
                continue;
            }

            // Make sure there's a : in it.
            //
            for (s = buff + 1; *s && *s != ':'; s++)
            {
                ; // Nothing
            }
            if (s)
            {
                free_lbuf(buff);
                atr_pop();
                return TRUE;
            }
        }
    }
    if (buff)
    {
        free_lbuf(buff);
    }
    atr_pop();
    return FALSE;
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

BOOL bMinDB = FALSE;
BOOL bSyntaxError = FALSE;
char *conffile = NULL;
BOOL bVersion = FALSE;

CLI_OptionEntry OptionTable[4] =
{
    { "c", CLI_REQUIRED, CLI_DO_CONFIG_FILE },
    { "s", CLI_NONE,     CLI_DO_MINIMAL     },
    { "v", CLI_NONE,     CLI_DO_VERSION     },
    { "h", CLI_NONE,     CLI_DO_USAGE       }
};

void CLI_CallBack(CLI_OptionEntry *p, char *pValue)
{
    if (p)
    {
        switch (p->m_Unique)
        {
        case CLI_DO_CONFIG_FILE:
            conffile = pValue;
            break;

        case CLI_DO_MINIMAL:
            bMinDB = TRUE;
            break;

        case CLI_DO_VERSION:
            bVersion = TRUE;
            break;

        case CLI_DO_USAGE:
        default:
            bSyntaxError = TRUE;
            break;
        }
    }
    else
    {
        bSyntaxError = TRUE;
    }
}

int DCL_CDECL main(int argc, char *argv[])
{
    build_version();

    // Parse the command line
    //
    CLI_Process(argc, argv, OptionTable,
        sizeof(OptionTable)/sizeof(CLI_OptionEntry), CLI_CallBack);
    if (bVersion)
    {
        printf("Version: %s" ENDLINE, mudstate.version);
        return 1;
    }
    if (  bSyntaxError
       || conffile == NULL)
    {
        printf("Version: %s" ENDLINE, mudstate.version);
        printf("Usage: %s [-h] [-v] [-s] [-c filename]" ENDLINE, argv[0]);
        printf("  -v  Display version string." ENDLINE);
        printf("  -s  Start with a minimal database." ENDLINE);
        printf("  -c  Specify configuration file." ENDLINE);
        printf("  -h  Display this help." ENDLINE);
        return 1;
    }

    Log.EnableLogging();

    BuildSignalNamesTable();

    FLOAT_Initialize();
    TIME_Initialize();
    SeedRandomNumberGenerator();
    game_pid = getpid();

#ifdef MEMORY_ACCOUNTING
    extern CHashFile hfAllocData;
    extern CHashFile hfIdentData;
    extern BOOL bMemAccountingInitialized;
    hfAllocData.Open("svdptrs.dir", "svdptrs.pag");
    hfIdentData.Open("svdlines.dir", "svdlines.pag");
    bMemAccountingInitialized = TRUE;
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
    if (QueryPerformanceFrequency((LARGE_INTEGER *)&QP_D))
    {
        bQueryPerformanceAvailable = TRUE;
        QP_A = FACTOR_100NS_PER_SECOND/QP_D;
        QP_B = FACTOR_100NS_PER_SECOND%QP_D;
        QP_C = QP_D/2;
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
    int ccPageFile = init_dbfile(mudconf.game_dir, mudconf.game_pag);
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

    close_sockets(FALSE, "Going down - Bye");
    dump_database();
    CLOSE;

#ifndef WIN32
    shutdown(slave_socket, SD_BOTH);
    close(slave_socket);
    slave_socket = INVALID_SOCKET;
    if (slave_pid > 0)
    {
        kill(slave_pid, SIGKILL);
    }
    slave_pid = 0;
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
