// game.cpp
//
// $Id: game.cpp,v 1.20 2000-09-20 19:22:31 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <sys/stat.h>
#include <signal.h>

#include "mudconf.h"
#include "file_c.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "flags.h"
#include "powers.h"
#include "attrs.h"
#include "alloc.h"
#include "slave.h"
#include "comsys.h"
#include "vattr.h"

#ifdef RADIX_COMPRESSION
#include "radix.h"
#endif

extern void NDECL(init_attrtab);
extern void NDECL(init_cmdtab);
extern void NDECL(cf_init);
extern void NDECL(pcache_init);
extern int cf_read(void);
extern void NDECL(init_functab);
extern void FDECL(close_sockets, (int emergency, char *message));
extern void NDECL(init_version);
extern void NDECL(init_logout_cmdtab);
extern void FDECL(raw_notify, (dbref, const char *));
extern void FDECL(do_dbck, (dbref, dbref, int));
extern void boot_slave(dbref, dbref, int);

void FDECL(fork_and_dump, (int));
void NDECL(dump_database);
void NDECL(pcache_sync);
#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
static void NDECL(init_rlimit);
#endif

int reserved;

#ifdef WIN32
extern CRITICAL_SECTION csDescriptorList;      // for thread synchronisation
#else // WIN32
#ifdef CONCENTRATE
int conc_pid = 0;
#endif
#endif // WIN32
#ifdef MEMORY_BASED
int corrupt = 0;
#endif


// used to allocate storage for temporary stuff, cleared before command
// execution
//
void do_dump(dbref player, dbref cause, int key)
{
#if !defined(VMS) && !defined(WIN32)
    if (mudstate.dumping)
    {
        notify(player, "Dumping in progress. Try again later.");
        return;
    }
#endif // !VMS && !WIN32
    notify(player, "Dumping...");
    fork_and_dump(key);
}

/*
 * print out stuff into error file 
 */

void NDECL(report)
{
    STARTLOG(LOG_BUGS, "BUG", "INFO");
    log_text((char *)"Command: '");
    log_text(mudstate.debug_cmd);
    log_text((char *)"'");
    ENDLOG
    if (Good_obj(mudstate.curr_player))
    {
        STARTLOG(LOG_BUGS, "BUG", "INFO")
        log_text((char *)"Player: ");
        log_name_and_loc(mudstate.curr_player);
        if ((mudstate.curr_enactor != mudstate.curr_player) && Good_obj(mudstate.curr_enactor))
        {
            log_text((char *)" Enactor: ");
            log_name_and_loc(mudstate.curr_enactor);
        }
        ENDLOG
    }
}

/* ----------------------------------------------------------------------
 * regexp_match: Load a regular expression match and insert it into
 * registers.
 */

int regexp_match(char *pattern, char *str, char *args[], int nargs)
{
    regexp *re;
    int got_match;
    int i, len;
    
    /*
     * Load the regexp pattern. This allocates memory which must be
     * later freed. A free() of the regexp does free all structures
     * under it.
     */
    
    if ((re = regcomp(pattern)) == NULL)
    {
        /*
         * This is a matching error. We have an error message in
         * regexp_errbuf that we can ignore, since we're doing
         * command-matching.
         */
        return 0;
    }
    
    /* 
     * Now we try to match the pattern. The relevant fields will
     * automatically be filled in by this.
     */
    got_match = regexec(re, str);
    if (!got_match)
    {
        MEMFREE(re);
        return 0;
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
    
    for (i = 0; (i < NSUBEXP) && (re->startp[i]) && (re->endp[i]); i++)
    {
        len = re->endp[i] - re->startp[i];
        args[i] = alloc_lbuf("regexp_match");
        strncpy(args[i], re->startp[i], len);
        args[i][len] = '\0';        /* strncpy() does not null-terminate */
    }
    
    MEMFREE(re);
    return 1;
}


/*
 * ----------------------------------------------------------------------
 * * atr_match: Check attribute list for wild card matches and queue them.
 */

static int atr_match1(dbref thing, dbref parent, dbref player, char type, char *str, int check_exclude,
              int hash_insert)
{
    dbref aowner;
    int match, attr, aflags, i;
    char buff[LBUF_SIZE], *s, *as;
    char *args[10];
    ATTR *ap;

    /*
     * See if we can do it.  Silently fail if we can't. 
     */

    if (!could_doit(player, parent, A_LUSE))
        return -1;

    match = 0;
    atr_push();
    for (attr = atr_head(parent, &as); attr; attr = atr_next(&as))
    {
        ap = atr_num(attr);

        /*
         * Never check NOPROG attributes. 
         */

        if (!ap || (ap->flags & AF_NOPROG))
            continue;

        /*
         * If we aren't the bottom level check if we saw this attr *
         * * * * before.  Also exclude it if the attribute type is *
         * * PRIVATE. 
         */

        if (check_exclude &&
            ((ap->flags & AF_PRIVATE) ||
             hashfindLEN(&(ap->number), sizeof(ap->number), &mudstate.parent_htab)))
        {
            continue;
        }
        atr_get_str(buff, parent, attr, &aowner, &aflags);

        /*
         * Skip if private and on a parent 
         */

        if (check_exclude && (aflags & AF_PRIVATE)) {
            continue;
        }
        /*
         * If we aren't the top level remember this attr so we * * *
         * exclude * it from now on. 
         */

        if (hash_insert)
        {
            hashaddLEN(&(ap->number), sizeof(ap->number), (int *)&attr, &mudstate.parent_htab);
        }

        /*
         * Check for the leadin character after excluding the attrib
         * * * * * This lets non-command attribs on the child block * 
         * *  * commands * on the parent. 
         */

        if ((buff[0] != type) || (aflags & AF_NOPROG))
            continue;

        /*
         * decode it: search for first un escaped : 
         */

        for (s = buff + 1; *s && (*s != ':'); s++) ;
        if (!*s)
            continue;
        *s++ = 0;
        if (((aflags & AF_REGEXP) &&
             regexp_match(buff + 1, 
                    str, 
                    args, 10)) ||
             wild(buff + 1, 
                    str, 
                    args, 10)) {
            match = 1;
            wait_que(thing, player, 0, NOTHING, 0, s, args, 10,
                 mudstate.global_regs);
            for (i = 0; i < 10; i++) {
                if (args[i])
                    free_lbuf(args[i]);
            }
        }
    }
    atr_pop();
    return (match);
}

int atr_match(dbref thing, dbref player, char type, char *str, int check_parents)
{
    int match, lev, result, exclude, insert;
    dbref parent;

    /*
     * If thing is halted, don't check anything 
     */

    if (Halted(thing))
        return 0;

    /*
     * If not checking parents, just check the thing 
     */

    match = 0;
    if (!check_parents)
        return atr_match1(thing, thing, player, type, str, 0, 0);

    /*
     * Check parents, ignoring halted objects 
     */

    exclude = 0;
    insert = 1;
    hashflush(&mudstate.parent_htab);
    ITER_PARENTS(thing, parent, lev)
    {
        if (!Good_obj(Parent(parent)))
            insert = 0;
        result = atr_match1(thing, parent, player, type, str, exclude, insert);
        if (result > 0)
        {
            match = 1;
        }
        else if (result < 0) 
        {
            return match;
        }
        exclude = 1;
    }

    return match;
}

/*
 * ---------------------------------------------------------------------------
 * * notify_check: notifies the object #target of the message msg, and
 * * optionally notify the contents, neighbors, and location also.
 */

int check_filter(dbref object, dbref player, int filter, const char *msg)
{
    int aflags;
    dbref aowner;
    char *buf, *nbuf, *cp, *dp, *str;
    char *preserve[MAX_GLOBAL_REGS];
    int preserve_len[MAX_GLOBAL_REGS];

    buf = atr_pget(object, filter, &aowner, &aflags);
    if (!*buf) {
        free_lbuf(buf);
        return (1);
    }
    save_global_regs("check_filter_save", preserve, preserve_len);
    nbuf = dp = alloc_lbuf("check_filter");
    str = buf;
    TinyExec(nbuf, &dp, 0, object, player, EV_FIGNORE | EV_EVAL | EV_TOP, &str, (char **)NULL, 0);
    *dp = '\0';
    dp = nbuf;
    free_lbuf(buf);
    restore_global_regs("check_filter_restore", preserve, preserve_len);

    do {
        cp = parse_to(&dp, ',', EV_STRIP_CURLY);
        if (quick_wild(cp, (char *)msg)) {
            free_lbuf(nbuf);
            return (0);
        }
    } while (dp != NULL);
    free_lbuf(nbuf);
    return (1);
}

static char *add_prefix(dbref object, dbref player, int prefix, const char *msg, const char *dflt)
{
    int aflags;
    dbref aowner;
    char *buf, *nbuf, *cp, *bp, *str;
    char *preserve[MAX_GLOBAL_REGS];
    int preserve_len[MAX_GLOBAL_REGS];

    buf = atr_pget(object, prefix, &aowner, &aflags);
    if (!*buf)
    {
        cp = buf;
        safe_str((char *)dflt, buf, &cp);
    }
    else
    {
        save_global_regs("add_prefix_save", preserve, preserve_len);
        nbuf = bp = alloc_lbuf("add_prefix");
        str = buf;
        TinyExec(nbuf, &bp, 0, object, player, EV_FIGNORE | EV_EVAL | EV_TOP, &str, (char **)NULL, 0);
        *bp = '\0';
        free_lbuf(buf);
        restore_global_regs("add_prefix_restore", preserve, preserve_len);
        buf = nbuf;
        cp = &buf[strlen(buf)];
    }
    if (cp != buf)
    {
        safe_str((char *)" ", buf, &cp);
    }
    safe_str((char *)msg, buf, &cp);
    *cp = '\0';
    return (buf);
}

static char *dflt_from_msg(dbref sender, dbref sendloc)
{
    char *tp, *tbuff;

    tp = tbuff = alloc_lbuf("notify_check.fwdlist");
    safe_str((char *)"From ", tbuff, &tp);
    if (Good_obj(sendloc))
        safe_str(Name(sendloc), tbuff, &tp);
    else
        safe_str(Name(sender), tbuff, &tp);
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
int html_escape(const char *src, char *dest, char **destp)
{
    const char *msg_orig;
    char *temp;
    int ret = 0;

    if (destp == 0)
    {
        temp = dest;
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
            ret = 1;
        }
    }
    **destp = 0;
    return ret;
}

void notify_check(dbref target, dbref sender, const char *msg, int key)
{
    char *msg_ns, *mp, *tbuff, *tp, *buff;
    char *args[10];
    dbref aowner, targetloc, recip, obj;
    int i, nargs, aflags, has_neighbors, pass_listen;
    int check_listens, pass_uselock, is_audible;
    FWDLIST *fp;

    /*
     * If speaker is invalid or message is empty, just exit 
     */

    if (!Good_obj(target) || !msg || !*msg)
        return;

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
#endif


    /*
     * Enforce a recursion limit 
     */

    mudstate.ntfy_nest_lev++;
    if (mudstate.ntfy_nest_lev >= mudconf.ntfy_nest_lim) {
        mudstate.ntfy_nest_lev--;
        return;
    }
    
    /*
     * If we want NOSPOOF output, generate it.  It is only needed if 
     * we are sending the message to the target object 
     */

    if (key & MSG_ME) {
        mp = msg_ns = alloc_lbuf("notify_check");
        if (Nospoof(target) &&
            (target != sender) &&
            (target != mudstate.curr_enactor) &&
            (target != mudstate.curr_player)) {

            /*
             * I'd really like to use tprintf here but I can't 
             * because the caller may have.
             * notify(target, tprintf(...)) is quite common 
             * in the code. 
             */

            tbuff = alloc_sbuf("notify_check.nospoof");
            safe_chr('[', msg_ns, &mp);
            safe_str(Name(sender), msg_ns, &mp);
            sprintf(tbuff, "(#%d)", sender);
            safe_str(tbuff, msg_ns, &mp);

            if (sender != Owner(sender)) {
                safe_chr('{', msg_ns, &mp);
                safe_str(Name(Owner(sender)), msg_ns, &mp);
                safe_chr('}', msg_ns, &mp);
            }
            if (sender != mudstate.curr_enactor) {
                sprintf(tbuff, "<-(#%d)",
                    mudstate.curr_enactor);
                safe_str(tbuff, msg_ns, &mp);
            }
            safe_str((char *)"] ", msg_ns, &mp);
            free_sbuf(tbuff);
        }
        safe_str((char *)msg, msg_ns, &mp);
        *mp = '\0';
    } else {
        msg_ns = NULL;
    }

    /*
     * msg contains the raw message, msg_ns contains the NOSPOOFed msg 
     */

    check_listens = Halted(target) ? 0 : 1;
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
            check_listens = 0;
        }

        // FALLTHROUGH

    case TYPE_THING:
    case TYPE_ROOM:

        // If we're in a pipe, objects can receive raw_notify if
        // they're not a player. (players were already notified
        // above.
        //
        if (mudstate.inpipe && !isPlayer(target))
        {
            raw_notify(target, msg_ns);
        }
        
        // Forward puppet message if it is for me.
        //
        has_neighbors = Has_location(target);
        targetloc = where_is(target);
        is_audible = Audible(target);

        if ( (key & MSG_ME)
           && Puppet(target)
           && (target != Owner(target))
           && (  (key & MSG_PUP_ALWAYS)
              || (  (targetloc != Location(Owner(target)))
                 && (targetloc != Owner(target)))))
        {
            tp = tbuff = alloc_lbuf("notify_check.puppet");
            safe_str(Name(target), tbuff, &tp);
            safe_str((char *)"> ", tbuff, &tp);
            safe_str(msg_ns, tbuff, &tp);
            *tp = '\0';
            raw_notify(Owner(target), tbuff);
            free_lbuf(tbuff);
        }

        // Check for @Listen match if it will be useful.
        //
        pass_listen = 0;
        nargs = 0;
        if (check_listens && (key & (MSG_ME | MSG_INV_L)) && H_Listen(target))
        {
            tp = atr_get(target, A_LISTEN, &aowner, &aflags);
            if (*tp && wild(tp, (char *)msg, args, 10))
            {
                for (nargs = 10; nargs && (!args[nargs - 1] || !(*args[nargs - 1])); nargs--)
                {
                    // Nothing
                    //
                    ;
                }
                pass_listen = 1;
            }
            free_lbuf(tp);
        }

        // If we matched the @listen or are monitoring, check the
        // USE lock.
        //
        pass_uselock = 0;
        if (  (key & MSG_ME)
           && check_listens
           && (pass_listen || Monitor(target)))
        {
            pass_uselock = could_doit(sender, target, A_LUSE);
        }

        // Process AxHEAR if we pass LISTEN, USElock and it's for me.
        //
        if ((key & MSG_ME) && pass_listen && pass_uselock)
        {
            if (sender != target)
                did_it(sender, target, 0, NULL, 0, NULL,
                       A_AHEAR, args, nargs);
            else
                did_it(sender, target, 0, NULL, 0, NULL,
                       A_AMHEAR, args, nargs);
            did_it(sender, target, 0, NULL, 0, NULL,
                   A_AAHEAR, args, nargs);
        }

        // Get rid of match arguments. We don't need them anymore.
        //
        if (pass_listen)
        {
            for (i = 0; i < 10; i++)
                if (args[i] != NULL)
                    free_lbuf(args[i]);
        }

        // Process ^-listens if for me, MONITOR, and we pass USElock.
        //
        if (  (key & MSG_ME)
           && pass_uselock
           && (sender != target)
           && Monitor(target))
        {
            (void)atr_match(target, sender, AMATCH_LISTEN, (char *)msg, 0);
        }

        // Deliver message to forwardlist members.
        //
        if ( (key & MSG_FWDLIST)
           && Audible(target)
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
                       || (recip == target))
                    {
                        continue;
                    }
                    notify_check(recip, sender, buff,
                             (MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE));
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
                   && ((recip != target)
                   && check_filter(obj, sender, A_FILTER, msg)))
                {
                    buff = add_prefix(obj, target, A_PREFIX, msg, "From a distance,");
                    notify_check(recip, sender, buff, MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE);
                    free_lbuf(buff);
                }
            }
        }

        // Deliver message through neighboring audible exits.
        //
        if (  has_neighbors
           && (  (key & MSG_NBR_EXITS)
              || ((key & MSG_NBR_EXITS_A) && is_audible)))
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
                if (Good_obj(recip) && Audible(obj) &&
                    (recip != targetloc) &&
                    (recip != target) &&
                 check_filter(obj, sender, A_FILTER, msg)) {
                    tbuff = add_prefix(obj, target,
                               A_PREFIX, buff,
                            "From a distance,");
                    notify_check(recip, sender, tbuff,
                             MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE);
                    free_lbuf(tbuff);
                }
            }
            if (key & MSG_S_INSIDE) {
                free_lbuf(buff);
            }
        }
        /*
         * Deliver message to contents 
         */

        if (((key & MSG_INV) || ((key & MSG_INV_L) && pass_listen)) &&
            (check_filter(target, sender, A_INFILTER, msg))) {

            /*
             * Don't prefix the message if we were given the * *
             * * * MSG_NOPREFIX key. 
             */

            if (key & MSG_S_OUTSIDE) {
                buff = add_prefix(target, sender, A_INPREFIX,
                          msg, "");
            } else {
                buff = (char *)msg;
            }
            DOLIST(obj, Contents(target)) {
                if (obj != target) {
                    notify_check(obj, sender, buff,
                    MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | key & MSG_HTML);
                }
            }
            if (key & MSG_S_OUTSIDE)
                free_lbuf(buff);
        }
        /*
         * Deliver message to neighbors 
         */

        if (has_neighbors &&
            ((key & MSG_NBR) ||
             ((key & MSG_NBR_A) && is_audible &&
              check_filter(target, sender, A_FILTER, msg)))) {
            if (key & MSG_S_INSIDE) {
                tbuff = dflt_from_msg(sender, target);
                buff = add_prefix(target, sender, A_PREFIX,
                          msg, "");
                free_lbuf(tbuff);
            } else {
                buff = (char *)msg;
            }
            DOLIST(obj, Contents(targetloc)) {
                if ((obj != target) && (obj != targetloc)) {
                    notify_check(obj, sender, buff,
                    MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE);
                }
            }
            if (key & MSG_S_INSIDE) {
                free_lbuf(buff);
            }
        }
        /*
         * Deliver message to container 
         */

        if (has_neighbors &&
            ((key & MSG_LOC) ||
             ((key & MSG_LOC_A) && is_audible &&
              check_filter(target, sender, A_FILTER, msg)))) {
            if (key & MSG_S_INSIDE) {
                tbuff = dflt_from_msg(sender, target);
                buff = add_prefix(target, sender, A_PREFIX,
                          msg, tbuff);
                free_lbuf(tbuff);
            } else {
                buff = (char *)msg;
            }
            notify_check(targetloc, sender, buff,
                     MSG_ME | MSG_F_UP | MSG_S_INSIDE);
            if (key & MSG_S_INSIDE) {
                free_lbuf(buff);
            }
        }
    }
    if (msg_ns)
        free_lbuf(msg_ns);
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
    
    if ((loc != exc1) && (loc != exc2))
    {
        notify_check(loc, player, msg, (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A));
    }
    DOLIST(first, Contents(loc))
    {
        if (first != exc1 && first != exc2)
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
    int yes_screen,
    int yes_log,
    int yes_clear
)
{
    int thing, obj_counted;
    CLinearTimeDelta ltdPeriod, ltdTotal;
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetUTC();
    ltdPeriod = ltaNow - mudstate.cpu_count_from;
    
    if (! (yes_log && (LOG_TIMEUSE & mudconf.log_options) != 0))
    {
        yes_log = 0;
        STARTLOG(LOG_ALWAYS, "WIZ", "TIMECHECK");
        log_name(player);
        log_text((char *) " checks object time use over ");
        log_number(ltdPeriod.ReturnSeconds());
        log_text((char *) " seconds\n");
        ENDLOG;
    }
    else
    {
        start_log("OBJ", "CPU");
        log_name(player);
        log_text((char *) " checks object time use over ");
        log_number(ltdPeriod.ReturnSeconds());
        log_text((char *) " seconds\n");
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
                Log.printf("#%d\t%ld\n", thing, used_msecs);
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
        Log.printf("Counted %d objects using %ld msecs over %d seconds.",
            obj_counted, ltdTotal.ReturnMilliseconds(), ltdPeriod.ReturnSeconds());
        end_log();
    }
    
    if (yes_clear)
    {
        mudstate.cpu_count_from = ltaNow;
    }
}

void do_timecheck(dbref player, dbref cause, int key)
{
    int yes_screen, yes_log, yes_clear;
    
    yes_screen = yes_log = yes_clear = 0;
    
    if (key == 0)
    {
        // No switches, default to printing to screen and clearing counters.
        //
        yes_screen = 1;
        yes_clear = 1;
    }
    else
    {
        if (key & TIMECHK_RESET)
            yes_clear = 1;
        if (key & TIMECHK_SCREEN)
            yes_screen = 1;
        if (key & TIMECHK_LOG)
            yes_log = 1;
    }
    
    report_timecheck(player, yes_screen, yes_log, yes_clear);
}

void do_shutdown(dbref player, dbref cause, int key, char *message)
{
    int fd;

    if (player != NOTHING)
    {
        raw_broadcast(0, "Game: Shutdown by %s", Name(Owner(player)));
        STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN")
        log_text((char *)"Shutdown by ");
        log_name(player);
        ENDLOG
    }
    else
    {
        raw_broadcast(0, "Game: Fatal Error: %s", message);
        STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN")
        log_text((char *)"Fatal error: ");
        log_text(message);
        ENDLOG
    }
    STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN")
    log_text((char *)"Shutdown status: ");
    log_text(message);
    ENDLOG
    fd = open(mudconf.status_file, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0600);
    if (fd != -1)
    {
        (void)write(fd, message, strlen(message));
        (void)write(fd, (char *)"\n", 1);
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
        log_text((char *)"Panic dump: ");
        log_text(mudconf.crashdb);
        ENDLOG;
        dump_database_internal(DUMP_I_PANIC);
        STARTLOG(LOG_ALWAYS, "DMP", "DONE");
        log_text((char *)"Panic dump complete: ");
        log_text(mudconf.crashdb);
        ENDLOG;
    }

    // Set up for normal shutdown.
    //
    mudstate.shutdown_flag = 1;
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
    { 0,               ""       , FALSE, 0,                                "" }, // 0 -- Handled specially.
    { &mudconf.crashdb, ""       , FALSE, UNLOAD_VERSION | UNLOAD_OUTFLAGS, "Opening crash file" }, // 1
    { &mudconf.indb,    ""       , TRUE,  OUTPUT_VERSION | OUTPUT_FLAGS,    "Opening outputfile" }, // 2
    { &mudconf.outdb,   ".FLAT"  , FALSE, UNLOAD_VERSION | UNLOAD_OUTFLAGS, "Opening flatfile"   }, // 3
    { &mudconf.indb,    ".KILLED", FALSE, UNLOAD_VERSION | UNLOAD_OUTFLAGS, "Opening killed file"}  // 4
};

void dump_database_internal(int dump_type)
{
    char tmpfile[SIZEOF_PATHNAME+32];
    char outfn[SIZEOF_PATHNAME+32];
    char prevfile[SIZEOF_PATHNAME+32];
    FILE *f;

    if (dump_type < 0 || dump_type >= NUM_DUMP_TYPES)
    {
        return;
    }

    BOOL bPotentialConflicts = FALSE;
#if !defined(VMS) && !defined(WIN32)
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
#endif // !VMS && !WIN32

    if (dump_type > 0)
    {
        DUMP_PROCEDURE *dp = &DumpProcedures[dump_type];

        sprintf(outfn, "%s%s", *(dp->ppszOutputBase), dp->szOutputSuffix);
        if (dp->bUseTemporary)
        {
#ifdef VMS
            sprintf(tmpfile, "%s.-%d-", outfn, mudstate.epoch);
#else
            sprintf(tmpfile, "%s.#%d#", outfn, mudstate.epoch);
#endif
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
            if (mudconf.have_comsys || mudconf.have_macros)
            {
                save_comsys(mudconf.comsys_db);
            }
        }
        return;
    }
    
    // Nuke our predecessor
    //
    sprintf(prevfile, "%s.prev", mudconf.outdb);
    sprintf(tmpfile, "%s.#%d#", mudconf.outdb, mudstate.epoch - 1);
    RemoveFile(tmpfile);

    sprintf(tmpfile, "%s.#%d#", mudconf.outdb, mudstate.epoch);

    if (mudconf.compress_db)
    {
        sprintf(tmpfile, "%s.#%d#.gz", mudconf.outdb, mudstate.epoch - 1);
        RemoveFile(tmpfile);
        sprintf(tmpfile, "%s.#%d#.gz", mudconf.outdb, mudstate.epoch);
        StringCopy(outfn, mudconf.outdb);
        strcat(outfn, ".gz");
        f = popen(tprintf("%s > %s", mudconf.compress, tmpfile), "wb");
        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, NULL, _IOFBF, 16384);
            db_write(f, F_MUX, OUTPUT_VERSION | OUTPUT_FLAGS);
            if (pclose(f) != -1)
            {
                DebugTotalFiles--;
            }
            ReplaceFile(mudconf.outdb, prevfile);
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
    if (mudconf.have_comsys || mudconf.have_macros)
    {
        save_comsys(mudconf.comsys_db);
    }
}
#endif

void NDECL(dump_database)
{
    char *buff;

    mudstate.epoch++;

#if !defined(VMS) && !defined(WIN32)
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
    mudstate.dumping = 1;
#endif
    buff = alloc_mbuf("dump_database");
#ifdef VMS
    sprintf(buff, "%s.-%d-", mudconf.outdb, mudstate.epoch);
#else
    sprintf(buff, "%s.#%d#", mudconf.outdb, mudstate.epoch);
#endif // VMS

    STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
    log_text((char *)"Dumping: ");
    log_text(buff);
    ENDLOG;
    pcache_sync();

    SYNC;

    dump_database_internal(DUMP_I_NORMAL);
    STARTLOG(LOG_DBSAVES, "DMP", "DONE")
    log_text((char *)"Dump complete: ");
    log_text(buff);
    ENDLOG;
    free_mbuf(buff);

#if !defined(VMS) && !defined(WIN32)
    // This doesn't matter. We are about the stop the game. However,
    // leave it in.
    //
    mudstate.dumping = 0;
#endif
}

void fork_and_dump(int key)
{
    char *buff;

#if !defined(VMS) && !defined(WIN32)
    // fork_and_dump is never called with mudstate.dumping == 1, but we'll
    // ensure assertion now.
    //
    if (mudstate.dumping) return;
    mudstate.dumping = 1;
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
    buff = alloc_lbuf("fork_and_dump");
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
#ifndef VMS
            sprintf(buff, "%s.#%d#", mudconf.outdb, mudstate.epoch);
#else
            sprintf(buff, "%s.-%d-", mudconf.outdb, mudstate.epoch);
#endif // VMS
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
#if !defined(VMS) && !defined(WIN32)
        if (mudconf.fork_dump)
        {
            if (mudconf.fork_vfork)
            {
                child = vfork();
            }
            else
            {
                child = fork();
            }
        }
#endif // VMS
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
#if !defined(VMS) && !defined(WIN32)
            if (mudconf.fork_dump)
            {
                _exit(0);
            }
#endif // VMS
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
    
#if !defined(VMS) && !defined(WIN32)
    if (!bChildExists)
    {
        // We have the ability to fork children, but we are not configured to
        // use it; or, we tried to fork a child and failed; or, we didn't
        // need to dump the structure or a flatfile.
        //
        mudstate.dumping = 0;
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
#else
static int load_game(int ccPageFile)
#endif
{
    FILE *f = NULL;
    char infile[SIZEOF_PATHNAME+8];
    struct stat statbuf;
    int db_format, db_version, db_flags;

    int compressed = 0;

#ifndef VMS
    if (mudconf.compress_db)
    {
        StringCopy(infile, mudconf.indb);
        strcat(infile, ".gz");
        if (stat(infile, &statbuf) == 0)
        {
            f = popen(tprintf(" %s < %s", mudconf.uncompress, infile), "rb");
            if (f != NULL)
            {
                DebugTotalFiles++;
                compressed = 1;
            }
        }
    }
#endif // VMS

    if (compressed == 0)
    {
        StringCopy(infile, mudconf.indb);
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
    log_text((char *)"Loading: ");
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
        log_text((char *)"Error loading ");
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
    if (db_flags & V_GDBM)
    {
        // It loaded an output file.
        //
        if (ccPageFile == HF_OPEN_STATUS_NEW)
        {
            STARTLOG(LOG_STARTUP, "INI", "LOAD");
            log_text((char *)"Attributes are not present in either the input file or the attribute database.");
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
            log_text((char *)"Attributes present in both the input file and the attribute database.");
            ENDLOG;
        }
    }
#endif

    if (mudconf.have_comsys || mudconf.have_macros)
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
            Log.printf("LOADING: %s\n", mudconf.mail_db);
            load_mail(f);
            Log.printf("LOADING: %s (done)\n", mudconf.mail_db);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
            f = 0;
        }
    }
    STARTLOG(LOG_STARTUP, "INI", "LOAD");
    log_text((char *)"Load complete.");
    ENDLOG;

    return LOAD_GAME_SUCCESS;
}


/*
 * match a list of things, using the no_command flag 
 */

int list_check(dbref thing, dbref player, char type, char *str, int check_parent)
{
    int match, limit;

    match = 0;
    limit = mudstate.db_top;
    while (thing != NOTHING) {
        if ((thing != player) && (!(No_Command(thing)))) {
            if (atr_match(thing, player, type, str, check_parent) > 0)
                match = 1;
        }
        thing = Next(thing);
        if (--limit < 0)
            return match;
    }
    return match;
}

int Hearer(dbref thing)
{
    char *as, *buff, *s;
    dbref aowner;
    int attr, aflags;
    ATTR *ap;

    if (mudstate.inpipe && (thing == mudstate.poutobj))
        return 1;

    if (Connected(thing) || Puppet(thing))
        return 1;

    if (Monitor(thing))
        buff = alloc_lbuf("Hearer");
    else
        buff = NULL;
    atr_push();
    for (attr = atr_head(thing, &as); attr; attr = atr_next(&as)) {
        if (attr == A_LISTEN) {
            if (buff)
                free_lbuf(buff);
            atr_pop();
            return 1;
        }
        if (Monitor(thing)) {
            ap = atr_num(attr);
            if (!ap || (ap->flags & AF_NOPROG))
                continue;

            atr_get_str(buff, thing, attr, &aowner, &aflags);

            /*
             * Make sure we can execute it 
             */

            if ((buff[0] != AMATCH_LISTEN) || (aflags & AF_NOPROG))
                continue;

            /*
             * Make sure there's a : in it 
             */

            for (s = buff + 1; *s && (*s != ':'); s++) ;
            if (s) {
                free_lbuf(buff);
                atr_pop();
                return 1;
            }
        }
    }
    if (buff)
        free_lbuf(buff);
    atr_pop();
    return 0;
}

void do_readcache(dbref player, dbref cause, int key)
{
    helpindex_load(player);
    fcache_load(player);
}

static void NDECL(process_preload)
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
        /*
         * Ignore GOING objects 
         */

        if (Going(thing))
            continue;

        scheduler.RunTasks(10);

        /*
         * Look for a STARTUP attribute in parents 
         */

        ITER_PARENTS(thing, parent, lev)
        {
            if (Flags(thing) & HAS_STARTUP)
            {
                did_it(Owner(thing), thing, 0, NULL, 0, NULL, A_STARTUP, (char **)NULL, 0);

                // Process queue entries as we add them.
                //
                scheduler.RunTasks(10);
                break;
             }
        }

        /*
         * Look for a FORWARDLIST attribute 
         */

        if (H_Fwdlist(thing))
        {
            (void)atr_get_str(tstr, thing, A_FORWARDLIST, &aowner, &aflags);
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
#endif // WIN32

#ifdef WIN32         // workaround till we have a getopt for windows
#undef USE_GETOPT    // ugly but easy to remove :)  -- carsten
#endif

#ifdef USE_GETOPT
    extern char *optarg;
#endif

int DCL_CDECL main(int argc, char *argv[])
{
    int bMinDB = FALSE;
    int bSyntaxError = FALSE;
    char *conffile = NULL;
#ifndef USE_GETOPT
    int iConfig = 0; // DEFAULT
#else
    int ch;
#endif
    
#ifndef USE_GETOPT
    switch (argc)
    {
    case 1:

        // Use default config file.
        //
        break;

    case 2:

        // Use the specified config file.
        //
        if (strcmp(argv[1], "-s") == 0)
        {
            bMinDB = TRUE;
        }
        else
        {
            iConfig = 1; // argv[1];
        }
        break;

    case 3:

        if (strcmp(argv[1], "-s") == 0)
        {
            // First parameter is "-s", second paramter is the config file.
            //
            bMinDB = TRUE;
            iConfig = 2; // argv[2];
        }
        else if (strcmp(argv[2], "-s") == 0)
        {
            bMinDB = TRUE;
            iConfig = 1; // argv[1];
        }
        else
        {
            bSyntaxError = TRUE;
        }
        break;

    default:

        // Syntax error.
        //
        bSyntaxError = TRUE;
    }
    if (iConfig)
    {
        conffile = argv[iConfig];
    }
#else
    while ((ch = getopt(argc, argv, "?sc:")) != -1)
    {
        switch (ch)
        {
        case 's':

            bMinDB = TRUE;
            break;

        case 'c':

            conffile = optarg;
            break;

        case ':':

        default :

        // Syntax error.
        //
            bSyntaxError = TRUE;
        }
    }
#endif

    if (bSyntaxError)
    {
        printf("Usage: %s [-s] [config-file]\n", argv[0]);
        return 1;
    }

    SeedRandomNumberGenerator();
    TIME_Initialize();
    game_pid = getpid();

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
        Log.WriteString("Running under Windows NT\n");

        // Get a handle to the kernel32 DLL
        //
        HINSTANCE hInstKernel32 = LoadLibrary("kernel32");
        if (!hInstKernel32)
        {
            Log.WriteString("LoadLibrary of kernel32 for a CancelIo entry point failed. Cannot continue.\n");
            return 1;
        }

        // Find the entry point for CancelIO so we can use it. This is done dynamically because Windows 95/98 doesn't
        // have a CancelIO entry point. If it were done at load time, it would always fail on Windows 95/98...even
        // though we don't use it or depend on it in that case.
        //
        fpCancelIo = (FCANCELIO *)GetProcAddress(hInstKernel32, "CancelIo");
        if (fpCancelIo == NULL)
        {
            Log.WriteString("GetProcAddress of _CancelIo failed. Cannot continue.\n");
            return 1;
        }
        fpGetProcessTimes = (FGETPROCESSTIMES *)GetProcAddress(hInstKernel32, "GetProcessTimes");
        if (fpGetProcessTimes == NULL)
        {
            Log.WriteString("GetProcAddress of GetProcessTimes failed. Cannot continue.\n");
            return 1;
        }
    }
    else
    {
        Log.WriteString("Running under Windows 95/98\n");
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
        Log.WriteString("ERROR: Could not initialize WinSock.\n");
        return 101;
    }

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        // We can't run on this version of WinSock.
        //
        Log.printf("INFO: We requested WinSock v2.2, but only WinSock v%d.%d was available.\n", LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
        //WSACleanup();
        //return 102;
    }
#endif // WIN32

#ifdef RADIX_COMPRESSION
    init_string_compress();
#endif // RADIX_COMPRESSION

#ifdef MEMORY_BASED
    // Database isn't corrupted.
    //
    corrupt = 0;
#endif
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
#endif
    init_cmdtab();
    init_logout_cmdtab();
    init_flagtab();
    init_powertab();
    init_functab();
    init_attrtab();
    init_version();

    if (conffile)
    {
        mudconf.config_file = StringClone(conffile);
    }
    else
    {
        mudconf.config_file = StringClone(CONF_FILE);
    }
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
        log_text((char *)"Couldn't load text database: ");
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
#else
        int ccInFile = load_game(ccPageFile);
#endif
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
            log_text((char *)"Couldn't load: ");
            log_text(mudconf.indb);
            ENDLOG
            return 2;
        }
    }
    set_signals();

    // Do a consistency check and set up the freelist 
    //
    do_dbck(NOTHING, NOTHING, 0);

    // Reset all the hash stats 
    //
    hashreset(&mudstate.command_htab);
    hashreset(&mudstate.macro_htab);
    hashreset(&mudstate.channel_htab);
    hashreset(&mudstate.mail_htab);
    hashreset(&mudstate.logout_cmd_htab);
    hashreset(&mudstate.func_htab);
    hashreset(&mudstate.flags_htab);
    hashreset(&mudstate.attr_name_htab);
    hashreset(&mudstate.player_htab);
    hashreset(&mudstate.fwdlist_htab);
    hashreset(&mudstate.news_htab);
    hashreset(&mudstate.help_htab);
    hashreset(&mudstate.wizhelp_htab);
    hashreset(&mudstate.plushelp_htab);
    hashreset(&mudstate.staffhelp_htab);
    hashreset(&mudstate.wiznews_htab);
    hashreset(&mudstate.desc_htab);

    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        mudstate.global_regs[i] = alloc_lbuf("main.global_reg");
        mudstate.glob_reg_len[i] = 0;
    }

    // If master room does not exist in the DB, clear
    // the master room in the configuration.
    //
    if (mudconf.master_room != NOTHING)
    {
        // A master room was specified in the config.
        //
        if (mudconf.master_room < 0 || mudstate.db_top <= mudconf.master_room)
        {
            // The specified master room outside the range of valid DBrefs
            //
            mudconf.master_room = NOTHING;
        }
    }
    process_preload();

#ifndef WIN32
    load_restart_db();

    if (!mudstate.restarting)
#endif // WIN32
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

    boot_slave(0, 0, 0);

#if defined(CONCENTRATE) && !defined(VMS) && !defined(WIN32)
    if (!mudstate.restarting)
    {
        // Start up the port concentrator. 
        //
        conc_pid = fork();
        if (conc_pid < 0)
        {
            perror("fork");
            exit(-1);
        }
        if (conc_pid == 0)
        {
            char mudp[32], inetp[32];

            // Add port argument to concentrator.
            //
            Tiny_ltoa(mudconf.port, mudp);
            Tiny_ltoa(mudconf.conc_port, inetp);
            execl("./bin/conc", "concentrator", inetp, mudp, "1", 0);
        }
        STARTLOG(LOG_ALWAYS, "CNC", "STRT");
        log_text("Concentrating ports... ");
        log_text(tprintf("Main: %d Conc: %d", mudconf.port, mudconf.conc_port));
        ENDLOG;
    }
#endif // CONCENTRATE && !WIN32

    // go do it.
    //
    init_timer();

#ifdef WIN32
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        process_output = process_outputNT;
        shovecharsNT(mudconf.port);
    }
    else
    {
        process_output = process_output9x;
        shovechars9x(mudconf.port);
    }
#else // !WIN32
    shovechars(mudconf.port);
#endif // WIN32

    close_sockets(0, (char *)"Going down - Bye");
    dump_database();
    CLOSE;

    // Go ahead and explicitly free the memory for these things so
    // that it's easy to spot unintentionally memory leaks.
    //
    helpindex_clean(&mudstate.staffhelp_htab);
    helpindex_clean(&mudstate.plushelp_htab);
    helpindex_clean(&mudstate.wiznews_htab);
    helpindex_clean(&mudstate.news_htab);
    helpindex_clean(&mudstate.help_htab);
    helpindex_clean(&mudstate.wizhelp_htab);

    db_free();

#ifdef WIN32
    // critical section not needed any more
    if (platform == VER_PLATFORM_WIN32_NT)
    {
        DeleteCriticalSection(&csDescriptorList);
    }
    WSACleanup();
#endif

    return 0;
}

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
static void NDECL(init_rlimit)
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
#endif // HAVE_SETRLIMIT
