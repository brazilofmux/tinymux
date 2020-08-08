/*! \file game.cpp
 * \brief Main program and misc functions.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <csignal>

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "file_c.h"
#include "functions.h"
#include "help.h"
#include "mguests.h"
#include "muxcli.h"
#include "pcre.h"
#include "powers.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

#if defined(INLINESQL)
#include <mysql.h>

MYSQL *mush_database = nullptr;
#endif // INLINESQL

void do_dump(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);

#if defined(HAVE_WORKING_FORK)
    if (mudstate.dumping)
    {
        notify(executor, T("Dumping in progress. Try again later."));
        return;
    }
#endif
    notify(executor, T("Dumping..."));
    fork_and_dump(key);
}

// print out stuff into error file
//
void report(void)
{
    STARTLOG(LOG_BUGS, "BUG", "INFO");
    log_text(T("Command: \xE2\x80\x98"));
    log_text(mudstate.debug_cmd);
    log_text(T("\xE2\x80\x99"));
    ENDLOG;
    if (Good_obj(mudstate.curr_executor))
    {
        STARTLOG(LOG_BUGS, "BUG", "INFO");
        log_text(T("Player: "));
        log_name_and_loc(mudstate.curr_executor);
        if (  mudstate.curr_enactor != mudstate.curr_executor
           && Good_obj(mudstate.curr_enactor))
        {
            log_text(T(" Enactor: "));
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
    UTF8 *pattern,
    UTF8 *str,
    int case_opt,
    UTF8 *args[],
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
    if (  alarm_clock.alarmed
       || (re = pcre_compile((char *)pattern, PCRE_UTF8|case_opt, &errptr, &erroffset, nullptr)) == nullptr)
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
    matches = pcre_exec(re, nullptr, (char *)str, static_cast<int>(strlen((char *)str)), 0, 0, ovec, ovecsize);
    if (matches < 0)
    {
        delete [] ovec;
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
        if (pcre_copy_substring((char *)str, ovec, matches, i,
                                (char *)args[i], LBUF_SIZE) < 0)
        {
            free_lbuf(args[i]);
            args[i] = nullptr;
        }
    }

    delete [] ovec;
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
    UTF8  type,
    UTF8  *str,
    UTF8  *raw_str,
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
    if (  (  AMATCH_CMD    == type
          && mudstate.bfNoCommands.IsSet(parent))
       || (  AMATCH_LISTEN == type
          && mudstate.bfNoListens.IsSet(parent)))
    {
        // We may need to process the hash_insert to support exclusion of
        // this parent's $-commands before we return no matches.
        //
        if (hash_insert)
        {
            // Because we know this object contains no commands, there is no
            // need to look at the attribute values.
            //
            atr_push();
            unsigned char *as;
            for (int atr = atr_head(parent, &as); atr; atr = atr_next(&as))
            {
                ATTR *ap = atr_num(atr);

                if (  ap
                   && 0 == (ap->flags & AF_NOPROG)
                   && (  !check_exclude
                      || (  0 == (ap->flags & AF_PRIVATE)
                         && !hashfindLEN(&(ap->number), sizeof(ap->number), &mudstate.parent_htab))))
                {
                    hashaddLEN(&(ap->number), sizeof(ap->number), &atr, &mudstate.parent_htab);
                }
            }
            atr_pop();
        }
        return match;
    }

    bool bFoundCommands = false;
    bool bFoundListens  = false;

    atr_push();
    unsigned char *as;
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
        UTF8 buff[LBUF_SIZE];
        atr_get_str(buff, parent, atr, &aowner, &aflags);

        UTF8 *s = nullptr;
        if (  0 == (aflags & AF_NOPROG)
           &&  (  AMATCH_CMD    == buff[0]
               || AMATCH_LISTEN == buff[0]))
        {
            s = (UTF8 *)strchr((char *)buff+1, ':');
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

        if (aflags & AF_NOPROG)
        {
            continue;
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

        UTF8 *args[NUM_ENV_VARS];
        if (  (  0 != (aflags & AF_REGEXP)
            && regexp_match(buff + 1, (aflags & AF_NOPARSE) ? raw_str : str,
                ((aflags & AF_CASE) ? 0 : PCRE_CASELESS), args, NUM_ENV_VARS))
           || (  0 == (aflags & AF_REGEXP)
              && wild(buff + 1, (aflags & AF_NOPARSE) ? raw_str : str,
                args, NUM_ENV_VARS)))
        {
            match = 1;
            CLinearTimeAbsolute lta;
            wait_que(thing, player, player, AttrTrace(aflags, 0), false, lta,
                NOTHING, 0,
                s,
                NUM_ENV_VARS, (const UTF8 **)args,
                mudstate.global_regs);

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
    UTF8  type,
    UTF8  *str,
    UTF8  *raw_str,
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
        // Remember, strip_color returns a pointer to a static buffer
        // within itself.
        //
        str = strip_color(str);
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

static bool check_filter(dbref object, dbref player, int filter, const UTF8 *msg)
{
    int aflags;
    dbref aowner;
    UTF8 *buf = atr_pget(object, filter, &aowner, &aflags);
    if (!*buf)
    {
        free_lbuf(buf);
        return true;
    }

    reg_ref **preserve = nullptr;
    preserve = PushRegisters(MAX_GLOBAL_REGS);
    save_global_regs(preserve);

    UTF8 *nbuf = alloc_lbuf("check_filter");
    UTF8 *dp = nbuf;
    mux_exec(buf, LBUF_SIZE-1, nbuf, &dp, object, player, player,
        AttrTrace(aflags, EV_FIGNORE|EV_EVAL|EV_TOP),
        nullptr, 0);
    *dp = '\0';
    dp = nbuf;
    free_lbuf(buf);

    restore_global_regs(preserve);
    PopRegisters(preserve, MAX_GLOBAL_REGS);
    preserve = nullptr;

    if (!(aflags & AF_REGEXP))
    {
        do
        {
            UTF8 *cp = parse_to(&dp, ',', EV_STRIP_CURLY);
            mudstate.wild_invk_ctr = 0;
            if (  alarm_clock.alarmed
               || quick_wild(cp, msg))
            {
                free_lbuf(nbuf);
                return false;
            }
        } while (dp != nullptr);
    }
    else
    {
        int case_opt = (aflags & AF_CASE) ? 0 : PCRE_CASELESS;
        do
        {
            int erroffset;
            const char *errptr;
            UTF8 *cp = parse_to(&dp, ',', EV_STRIP_CURLY);
            pcre *re;
            if (  !alarm_clock.alarmed
               && (re = pcre_compile((char *)cp, PCRE_UTF8|case_opt, &errptr, &erroffset, nullptr)) != nullptr)
            {
                const int ovecsize = 33;
                int ovec[ovecsize];
                int matches = pcre_exec(re, nullptr, (char *)msg, static_cast<int>(strlen((char *)msg)), 0, 0,
                    ovec, ovecsize);
                if (0 <= matches)
                {
                    MEMFREE(re);
                    free_lbuf(nbuf);
                    return false;
                }
                MEMFREE(re);
            }
        } while (dp != nullptr);
    }
    free_lbuf(nbuf);
    return true;
}

static UTF8 *make_prefix(dbref object, dbref player, int prefix, const UTF8 *dflt)
{
    int aflags;
    dbref aowner;
    UTF8 *buf, *nbuf, *cp;

    buf = atr_pget(object, prefix, &aowner, &aflags);
    if (!*buf)
    {
        cp = buf;
        if (nullptr == dflt)
        {
            safe_str(T("From "), buf, &cp);
            if (Good_obj(object))
            {
                safe_str(Moniker(object), buf, &cp);
            }
            else
            {
                safe_str(Moniker(player), buf, &cp);
            }
            safe_chr(',', buf, &cp);
        }
        else
        {
            safe_str(dflt, buf, &cp);
        }
    }
    else
    {
        reg_ref **preserve = nullptr;
        preserve = PushRegisters(MAX_GLOBAL_REGS);
        save_global_regs(preserve);

        nbuf = cp = alloc_lbuf("add_prefix");
        mux_exec(buf, LBUF_SIZE-1, nbuf, &cp, object, player, player,
            AttrTrace(aflags, EV_FIGNORE|EV_EVAL|EV_TOP),
            nullptr, 0);
        free_lbuf(buf);

        restore_global_regs(preserve);
        PopRegisters(preserve, MAX_GLOBAL_REGS);

        buf = nbuf;
    }
    if (cp != buf)
    {
        safe_chr(' ', buf, &cp);
    }
    *cp = '\0';
    return buf;
}

/* Do HTML escaping, converting < to &lt;, etc.  'dest' needs to be
 * allocated & freed by the caller.
 *
 * If you're using this to append to a string, you can pass in the
 * safe_{str|chr} (UTF8 **) so we can just do the append directly,
 * saving you an alloc_lbuf()...free_lbuf().  If you want us to append
 * from the start of 'dest', just pass in a 0 for 'destp'.
 *
 * Returns 0 if the copy succeeded, 1 if it failed.
 */
bool html_escape(const UTF8 *src, UTF8 *dest, UTF8 **destp)
{
    const UTF8 *msg_orig;
    bool ret = false;

    if (destp == 0)
    {
        UTF8 *temp = dest;
        destp = &temp;
    }

    for (msg_orig = src; msg_orig && *msg_orig && !ret; msg_orig++)
    {
        UTF8 *p = *destp;
        switch (*msg_orig)
        {
        case '<':
            safe_str(T("&lt;"), dest, destp);
            break;

        case '>':
            safe_str(T("&gt;"), dest, destp);
            break;

        case '&':
            safe_str(T("&amp;"), dest, destp);
            break;

        case '\"':
            safe_str(T("&quot;"), dest, destp);
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

void notify_check(dbref target, dbref sender, const mux_string &msg, int key)
{
    // If speaker is invalid or message is empty, just exit.
    //
    if (  !Good_obj(target)
       || 0 == msg.length_byte())
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

    mux_string *msg_ns = new mux_string;
    mux_string *msgFinal = new mux_string;
    UTF8 *tp;
    UTF8 *prefix;
    dbref aowner,  recip, obj;
    int i, nargs, aflags;
    FWDLIST *fp;

    // If we want NOSPOOF output, generate it.  It is only needed if we are
    // sending the message to the target object.
    //
    if (key & MSG_ME)
    {
        if (  Nospoof(target)
           && target != sender
           && target != mudstate.curr_enactor
           && target != mudstate.curr_executor)
        {
            // I'd really like to use tprintf here but I can't because the
            // caller may have.  notify(target, tprintf(...)) is quite common
            // in the code.
            //
            msg_ns->import(T("["), 1);
            msg_ns->append(Moniker(sender));
            msg_ns->append_TextPlain(T("("), 1);
            msg_ns->append(sender);
            msg_ns->append_TextPlain(T(")"), 1);

            if (sender != Owner(sender))
            {
                msg_ns->append_TextPlain(T("{"), 1);
                msg_ns->append(Moniker(Owner(sender)));
                msg_ns->append_TextPlain(T("}"), 1);
            }

            if (sender != mudstate.curr_enactor)
            {
                msg_ns->append_TextPlain(T("<-("), 3);
                msg_ns->append(mudstate.curr_enactor);
                msg_ns->append_TextPlain(T(")"), 1);
            }

            switch (DecodeMsgSource(key))
            {
            case MSG_SRC_COMSYS:
                msg_ns->append_TextPlain(T(",comsys"));
                break;

            case MSG_SRC_KILL:
                msg_ns->append_TextPlain(T(",kill"));
                break;

            case MSG_SRC_GIVE:
                msg_ns->append_TextPlain(T(",give"));
                break;

            case MSG_SRC_PAGE:
                msg_ns->append_TextPlain(T(",page"));
                break;

            default:
                if (key & MSG_SAYPOSE)
                {
                    msg_ns->append_TextPlain(T(",saypose"));
                }
                break;
            }

            msg_ns->append_TextPlain(T("] "), 2);
        }
    }
    msg_ns->append(msg);

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
                raw_notify_html(target, *msg_ns);
            }
            else
            {
                msgFinal->import(*msg_ns);
                if (Html(target))
                {
                    msgFinal->encode_Html();
                }
                raw_notify(target, *msgFinal);
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
            raw_notify(target, *msg_ns);
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
            msgFinal->import(Moniker(target));
            msgFinal->append_TextPlain(T("> "), 2);
            msgFinal->append(*msg_ns);
            raw_notify(Owner(target), *msgFinal);
        }

        // Check for @Listen match if it will be useful.
        //
        UTF8 *msgPlain = alloc_lbuf("notify_check.plain");
        msg.export_TextPlain(msgPlain);
        bool pass_listen = false;
        UTF8 *args[NUM_ENV_VARS];
        nargs = 0;
        if (  check_listens
           && (key & (MSG_ME | MSG_INV_L))
           && H_Listen(target))
        {
            tp = atr_get("notify_check.790", target, A_LISTEN, &aowner, &aflags);
            if (*tp && wild(tp, msgPlain, args, NUM_ENV_VARS))
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
                did_it( sender, target, 0, nullptr, 0, nullptr, A_AHEAR, 0,
                        (const UTF8 **)args, nargs);
            }
            else
            {
                did_it( sender, target, 0, nullptr, 0, nullptr, A_AMHEAR, 0,
                        (const UTF8 **)args, nargs);
            }
            did_it( sender, target, 0, nullptr, 0, nullptr, A_AAHEAR, 0,
                    (const UTF8 **)args, nargs);
            mudstate.nHearNest--;
        }

        // Get rid of match arguments. We don't need them anymore.
        //
        if (pass_listen)
        {
            for (i = 0; i < nargs; i++)
            {
                if (args[i] != nullptr)
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
            atr_match(target, sender, AMATCH_LISTEN, msgPlain, msgPlain, false);
        }

        // Deliver message to forwardlist members.
        //
        if ( (key & MSG_FWDLIST)
           && is_audible
           && check_filter(target, sender, A_FILTER, msgPlain))
        {
            fp = fwdlist_get(target);
            if (nullptr != fp)
            {
                prefix = make_prefix(target, sender, A_PREFIX, nullptr);
                msgFinal->import(prefix);
                free_lbuf(prefix);
                msgFinal->append(msg);

                for (i = 0; i < fp->count; i++)
                {
                    recip = fp->data[i];
                    if (  !Good_obj(recip)
                       || recip == target)
                    {
                        continue;
                    }
                    notify_check(recip, sender, *msgFinal,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
                }
            }
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
                      && check_filter(obj, sender, A_FILTER, msgPlain)))
                {
                    prefix = make_prefix(obj, target, A_PREFIX, T("From a distance,"));
                    msgFinal->import(prefix);
                    free_lbuf(prefix);

                    msgFinal->append(msg);
                    notify_check(recip, sender, *msgFinal,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
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
                prefix = make_prefix(target, sender, A_PREFIX, nullptr);
                msgFinal->import(prefix);
                free_lbuf(prefix);

                msgFinal->append(msg);
            }
            else
            {
                msgFinal->import(msg);
            }

            mux_string *msgPrefixed2 = new mux_string;

            DOLIST(obj, Exits(Location(target)))
            {
                recip = Location(obj);
                if (  Good_obj(recip)
                   && Audible(obj)
                   && recip != targetloc
                   && recip != target
                   && check_filter(obj, sender, A_FILTER, msgPlain))
                {
                    prefix = make_prefix(obj, target, A_PREFIX, T("From a distance,"));
                    msgPrefixed2->import(prefix);
                    free_lbuf(prefix);

                    msgPrefixed2->append(*msgFinal);
                    notify_check(recip, sender, *msgPrefixed2,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
                }
            }
            delete msgPrefixed2;
        }

        // Deliver message to contents.
        //
        if (  (  (key & MSG_INV)
              || (  (key & MSG_INV_L)
                 && pass_listen))
           && check_filter(target, sender, A_INFILTER, msgPlain))
        {
            // Don't prefix the message if we were given the MSG_NOPREFIX key.
            //
            if (key & MSG_S_OUTSIDE)
            {
                prefix = make_prefix(target, sender, A_INPREFIX, T(""));
                msgFinal->import(prefix);
                free_lbuf(prefix);

                msgFinal->append(msg);
            }
            else
            {
                msgFinal->import(msg);
            }

            DOLIST(obj, Contents(target))
            {
                if (obj != target)
                {
                    notify_check(obj, sender, *msgFinal,
                        MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | (key & (MSG_HTML | MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
                }
            }
        }

        // Deliver message to neighbors.
        //
        if (  has_neighbors
           && (  (key & MSG_NBR)
              || (  (key & MSG_NBR_A)
                 && is_audible
                 && check_filter(target, sender, A_FILTER, msgPlain))))
        {
            if (key & MSG_S_INSIDE)
            {
                prefix = make_prefix(target, sender, A_PREFIX, T(""));
                msgFinal->import(prefix);
                free_lbuf(prefix);

                msgFinal->append(msg);
            }
            else
            {
                msgFinal->import(msg);
            }

            DOLIST(obj, Contents(targetloc))
            {
                if (  obj != target
                   && obj != targetloc)
                {
                    notify_check(obj, sender, *msgFinal,
                        MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
                }
            }
        }

        // Deliver message to container.
        //
        if (  has_neighbors
           && (  (key & MSG_LOC)
              || ( (key & MSG_LOC_A)
                 && is_audible
                 && check_filter(target, sender, A_FILTER, msgPlain))))
        {
            if (key & MSG_S_INSIDE)
            {
                prefix = make_prefix(target, sender, A_PREFIX, nullptr);
                msgFinal->import(prefix);
                free_lbuf(prefix);
                msgFinal->append(msg);
            }
            else
            {
                msgFinal->import(msg);
            }

            notify_check(targetloc, sender, *msgFinal,
                MSG_ME | MSG_F_UP | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
        }
        free_lbuf(msgPlain);
    }
    delete msgFinal;
    delete msg_ns;
    mudstate.ntfy_nest_lev--;
}

void notify_check(dbref target, dbref sender, const UTF8 *msg, int key)
{
    // If speaker is invalid or message is empty, just exit.
    //
    if (  !Good_obj(target)
       || !msg
       || !*msg)
    {
        return;
    }

    mux_string *sMsg = new mux_string(msg);

    notify_check(target, sender, *sMsg, key);

    delete sMsg;
}

void notify_except(dbref loc, dbref player, dbref exception, const UTF8 *msg, int key)
{
    dbref first;

    if (loc != exception)
    {
        notify_check(loc, player, msg, MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A | key);
    }
    DOLIST(first, Contents(loc))
    {
        if (first != exception)
        {
            notify_check(first, player, msg, MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | key);
        }
    }
}

void notify_except2(dbref loc, dbref player, dbref exc1, dbref exc2, const UTF8 *msg)
{
    dbref first;

    if (  loc != exc1
       && loc != exc2)
    {
        notify_check(loc, player, msg, MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A);
    }
    DOLIST(first, Contents(loc))
    {
        if (  first != exc1
           && first != exc2)
        {
            notify_check(first, player, msg, MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE);
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
        start_log(T("OBJ"), T("CPU"));
        log_name(player);
        log_text(T(" checks object time use over "));
        log_number(ltdPeriod.ReturnSeconds());
        log_text(T(" seconds" ENDLINE));
    }
    else
    {
        yes_log = false;
        STARTLOG(LOG_ALWAYS, "WIZ", "TIMECHECK");
        log_name(player);
        log_text(T(" checks object time use over "));
        log_number(ltdPeriod.ReturnSeconds());
        log_text(T(" seconds"));
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
                Log.tinyprintf(T("#%d\t%ld" ENDLINE), thing, used_msecs);
            }
            if (yes_screen)
            {
                raw_notify(player, tprintf(T("#%d\t%ld"), thing, used_msecs));
            }
            if (yes_clear)
            {
                ltd.Set100ns(0);
            }
        }
    }

    long lTotal = ltdTotal.ReturnMilliseconds();
    long lPeriod = ltdPeriod.ReturnSeconds();

    if (yes_screen)
    {
        raw_notify(player,
            tprintf(T("Counted %d objects using %ld msecs over %d seconds."),
            obj_counted, lTotal, lPeriod));
    }

    if (yes_log)
    {
        Log.tinyprintf(T("Counted %d objects using %ld msecs over %d seconds."),
            obj_counted, lTotal, lPeriod);
        end_log();
    }

    if (yes_clear)
    {
        mudstate.cpu_count_from = ltaNow;
    }
}

void do_timecheck(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);

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
    int   eval,
    int   key,
    UTF8 *message,
    const UTF8 *cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Can_SiteAdmin(executor))
    {
        notify(executor, NOPERM_MESSAGE);
        return;
    }

    raw_broadcast(0, T("GAME: Shutdown by %s"), Moniker(Owner(executor)));
    STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN");
    log_text(T("Shutdown by "));
    log_name(executor);
    ENDLOG;

    STARTLOG(LOG_ALWAYS, "WIZ", "SHTDN");
    log_text(T("Shutdown status: "));
    log_text(message);
    ENDLOG;

    int fd;
    if (mux_open(&fd, mudconf.status_file, O_RDWR|O_CREAT|O_TRUNC|O_BINARY))
    {
        mux_write(fd, message, static_cast<unsigned int>(strlen((char *)message)));
        mux_write(fd, ENDLINE, sizeof(ENDLINE)-1);
        DebugTotalFiles++;
        if (mux_close(fd) == 0)
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
        log_text(T("Panic dump: "));
        log_text(mudconf.crashdb);
        ENDLOG;
        dump_database_internal(DUMP_I_PANIC);
        STARTLOG(LOG_ALWAYS, "DMP", "DONE");
        log_text(T("Panic dump complete: "));
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
    UTF8      **ppszOutputBase;
    const UTF8 *szOutputSuffix;
    bool        bUseTemporary;
    int         fType;
    const UTF8 *pszErrorMessage;
} DUMP_PROCEDURE;

static DUMP_PROCEDURE DumpProcedures[NUM_DUMP_TYPES] =
{
    { nullptr,          T(""),     false, 0,                             T("") }, // 0 -- Handled specially.
    { &mudconf.crashdb, T(""),     false, UNLOAD_VERSION | UNLOAD_FLAGS, T("Opening crash file") }, // 1
    { &mudconf.indb,    T(""),     true,  OUTPUT_VERSION | OUTPUT_FLAGS, T("Opening input file") }, // 2
    { &mudconf.indb,   T(".FLAT"), false, UNLOAD_VERSION | UNLOAD_FLAGS, T("Opening flatfile")   }, // 3
    { &mudconf.indb,   T(".SIG"),  false, UNLOAD_VERSION | UNLOAD_FLAGS, T("Opening signalled flatfile")}  // 4
};

#if defined(WINDOWS_FILES)
#define POPEN_READ_OP "rb"
#define POPEN_WRITE_OP "wb"
#elif defined(UNIX_FILES)
#define POPEN_READ_OP "r"
#define POPEN_WRITE_OP "w"
#endif // UNIX_FILES

void dump_database_internal(int dump_type)
{
    UTF8 tmpfile[SIZEOF_PATHNAME+32];
    UTF8 outfn[SIZEOF_PATHNAME+32];
    UTF8 prevfile[SIZEOF_PATHNAME+32];
    FILE *f;

    if (  dump_type < 0
       || NUM_DUMP_TYPES <= dump_type)
    {
        return;
    }

    bool bPotentialConflicts = false;
#if defined(HAVE_WORKING_FORK)
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
#endif // HAVE_WORKING_FORK

    // Call the local dump function only if another dump is not already
    // in progress.
    //
    local_dump_database(dump_type);
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->dump_database(dump_type);
        p = p->pNext;
    }

    if (0 < dump_type)
    {
        DUMP_PROCEDURE *dp = &DumpProcedures[dump_type];
        bool bOpen;

        mux_sprintf(outfn, sizeof(outfn), T("%s%s"), *(dp->ppszOutputBase), dp->szOutputSuffix);
        if (dp->bUseTemporary)
        {
            mux_sprintf(tmpfile, sizeof(tmpfile), T("%s.#%d#"), outfn, mudstate.epoch);
            RemoveFile(tmpfile);
            bOpen = mux_fopen(&f, tmpfile, T("wb"));
        }
        else
        {
            RemoveFile(outfn);
            bOpen = mux_fopen(&f, outfn, T("wb"));
        }

        if (bOpen)
        {
            DebugTotalFiles++;
            setvbuf(f, nullptr, _IOFBF, 16384);
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
            log_perror(T("DMP"), T("FAIL"), dp->pszErrorMessage, outfn);
        }

        if (!bPotentialConflicts)
        {
            if (mudconf.have_mailer)
            {
                if (mux_fopen(&f, mudconf.mail_db, T("wb")))
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
        mux_sprintf(prevfile, sizeof(prevfile), T("%s.prev.gz"), mudconf.outdb);
        mux_sprintf(tmpfile, sizeof(tmpfile), T("%s.#%d#.gz"), mudconf.outdb, mudstate.epoch - 1);
        RemoveFile(tmpfile);
        mux_sprintf(tmpfile, sizeof(tmpfile), T("%s.#%d#.gz"), mudconf.outdb, mudstate.epoch);
        mux_sprintf(outfn, sizeof(outfn), T("%s.gz"), mudconf.outdb);

        f = popen((char *)tprintf(T("%s > %s"), mudconf.compress, tmpfile), POPEN_WRITE_OP);
        if (f)
        {
            DebugTotalFiles++;
            setvbuf(f, nullptr, _IOFBF, 16384);
            db_write(f, F_MUX, OUTPUT_VERSION | OUTPUT_FLAGS);
            if (pclose(f) != -1)
            {
                DebugTotalFiles--;
            }
            ReplaceFile(outfn, prevfile);
            if (ReplaceFile(tmpfile, outfn) < 0)
            {
                log_perror(T("SAV"), T("FAIL"), T("Renaming output file to DB file"), tmpfile);
            }
        }
        else
        {
            log_perror(T("SAV"), T("FAIL"), T("Opening"), tmpfile);
        }
    }
    else
    {
        mux_sprintf(prevfile, sizeof(prevfile), T("%s.prev"), mudconf.outdb);
        mux_sprintf(tmpfile, sizeof(tmpfile), T("%s.#%d#"), mudconf.outdb, mudstate.epoch - 1);
        RemoveFile(tmpfile);
        mux_sprintf(tmpfile, sizeof(tmpfile), T("%s.#%d#"), mudconf.outdb, mudstate.epoch);

        if (mux_fopen(&f, tmpfile, T("wb")))
        {
            DebugTotalFiles++;
            setvbuf(f, nullptr, _IOFBF, 16384);
            db_write(f, F_MUX, OUTPUT_VERSION | OUTPUT_FLAGS);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
            ReplaceFile(mudconf.outdb, prevfile);
            if (ReplaceFile(tmpfile, mudconf.outdb) < 0)
            {
                log_perror(T("SAV"), T("FAIL"), T("Renaming output file to DB file"), tmpfile);
            }
        }
        else
        {
            log_perror(T("SAV"), T("FAIL"), T("Opening"), tmpfile);
        }
    }

    if (mudconf.have_mailer)
    {
        if (mux_fopen(&f, mudconf.mail_db, T("wb")))
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

static void dump_database(void)
{
    UTF8 *buff;

    mudstate.epoch++;

#if defined(HAVE_WORKING_FORK)
    if (mudstate.dumping)
    {
        STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
        log_text(T("Waiting on previously-forked child before dumping... "));
        ENDLOG;

        while (mudstate.dumping)
        {
            // We have a forked dump in progress, so we will wait until the
            // child exits.
            //
            alarm_clock.sleep(time_1s);
        }
    }
    mudstate.dumping = true;
    mudstate.dumped  = 0;
#endif // HAVE_WORKING_FORK
    buff = alloc_mbuf("dump_database");
    mux_sprintf(buff, MBUF_SIZE, T("%s.#%d#"), mudconf.outdb, mudstate.epoch);

    STARTLOG(LOG_DBSAVES, "DMP", "DUMP");
    log_text(T("Dumping: "));
    log_text(buff);
    ENDLOG;

    local_presync_database();
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->presync_database();
        p = p->pNext;
    }

#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    pcache_sync();

    dump_database_internal(DUMP_I_NORMAL);
    SYNC;

    STARTLOG(LOG_DBSAVES, "DMP", "DONE")
    log_text(T("Dump complete: "));
    log_text(buff);
    ENDLOG;
    free_mbuf(buff);

#if defined(HAVE_WORKING_FORK)
    // This doesn't matter. We are about the stop the game. However,
    // leave it in.
    //
    mudstate.dumping = false;
    local_dump_complete_signal();
#endif // HAVE_WORKING_FORK

    p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->dump_complete_signal();
        p = p->pNext;
    }
}

void fork_and_dump(int key)
{
#if defined(HAVE_WORKING_FORK)
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
#endif // HAVE_WORKING_FORK

    // If no options were given, then it means DUMP_TEXT+DUMP_STRUCT.
    //
    if (key == 0)
    {
        key = DUMP_TEXT+DUMP_STRUCT;
    }

    if (*mudconf.dump_msg)
    {
        raw_broadcast(0, T("%s"), mudconf.dump_msg);
    }
    check_mail_expiration();
    UTF8 *buff = alloc_lbuf("fork_and_dump");
    if (key & (DUMP_TEXT|DUMP_STRUCT))
    {
        STARTLOG(LOG_DBSAVES, "DMP", "CHKPT");
        if (key & DUMP_TEXT)
        {
            log_text(T("SYNCing"));
            if (key & DUMP_STRUCT)
            {
                log_text(T(" and "));
            }
        }
        if (key & DUMP_STRUCT)
        {
            mudstate.epoch++;
            mux_sprintf(buff, LBUF_SIZE, T("%s.#%d#"), mudconf.outdb, mudstate.epoch);
            log_text(T("Checkpointing: "));
            log_text(buff);
        }
        ENDLOG;
    }
    if (key & DUMP_FLATFILE)
    {
        STARTLOG(LOG_DBSAVES, "DMP", "FLAT");
        log_text(T("Creating flatfile: "));
        mux_sprintf(buff, LBUF_SIZE, T("%s.FLAT"), mudconf.outdb);
        log_text(buff);
        ENDLOG;
    }
    free_lbuf(buff);

    local_presync_database();
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->presync_database();
        p = p->pNext;
    }

#ifndef MEMORY_BASED
    // Save cached modified attribute list
    //
    al_store();
#endif // MEMORY_BASED

    pcache_sync();
    SYNC;

#if defined(HAVE_WORKING_FORK)
    mudstate.write_protect = true;
    int child = 0;
    bool bChildExists = false;
    mudstate.dumping = true;
    mudstate.dumped  = 0;
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
#endif // HAVE_WORKING_FORK

    if (key & (DUMP_STRUCT|DUMP_FLATFILE))
    {
#if defined(HAVE_WORKING_FORK)
        if (bAttemptFork)
        {
            child = fork();
        }
        if (child == 0)
        {
            // If we don't clear this alarm, the child will eventually receive a
            // SIG_PROF.
            //
            alarm_clock.clear();
#endif // HAVE_WORKING_FORK

            if (key & DUMP_STRUCT)
            {
                dump_database_internal(DUMP_I_NORMAL);
            }
            if (key & DUMP_FLATFILE)
            {
                dump_database_internal(DUMP_I_FLAT);
            }
#if defined(HAVE_WORKING_FORK)
            if (mudconf.fork_dump)
            {
                _exit(0);
            }
        }
        else if (child < 0)
        {
            log_perror(T("DMP"), T("FORK"), nullptr, T("fork()"));
        }
        else
        {
            mudstate.dumper = child;
            if (mudstate.dumper == mudstate.dumped)
            {
                // The child process executed and exited before fork() returned
                // to the parent process.  Without a process id, the parent's
                // SIGCHLD handler could not be certain that the pid of the
                // exiting process would match the pid of this child.
                //
                // At the this point, we can be sure, however, there's
                // nothing much left to do.
                //
                // See SIGCHLD handler in bsd.cpp.
                //
                mudstate.dumper = 0;
                mudstate.dumped = 0;
            }
            else
            {
                bChildExists = true;
            }
        }
#endif // HAVE_WORKING_FORK
    }

#if defined(HAVE_WORKING_FORK)
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
        ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
        while (nullptr != p)
        {
            p->pSink->dump_complete_signal();
            p = p->pNext;
        }
    }
    bRequestAccepted = false;
#endif // HAVE_WORKING_FORK

    if (*mudconf.postdump_msg)
    {
        raw_broadcast(0, T("%s"), mudconf.postdump_msg);
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
    FILE *f = nullptr;
    UTF8 infile[SIZEOF_PATHNAME+8];
    struct stat statbuf;
    int db_format, db_version, db_flags;

    bool compressed = false;

    if (mudconf.compress_db)
    {
        mux_sprintf(infile, sizeof(infile), T("%s.gz"), mudconf.indb);
        if (stat((char *)infile, &statbuf) == 0)
        {
            f = popen((char *)tprintf(T(" %s < %s"), mudconf.uncompress, infile), POPEN_READ_OP);
            if (f != nullptr)
            {
                DebugTotalFiles++;
                compressed = true;
            }
        }
    }

    if (!compressed)
    {
        mux_strncpy(infile, mudconf.indb, sizeof(infile)-1);
        if (stat((char *)infile, &statbuf) != 0)
        {
            // Indicate that we couldn't load because the input db didn't
            // exist.
            //
            return LOAD_GAME_NO_INPUT_DB;
        }

        if (!mux_fopen(&f, infile, T("rb")))
        {
            return LOAD_GAME_CANNOT_OPEN;
        }
        DebugTotalFiles++;
        setvbuf(f, nullptr, _IOFBF, 16384);
    }

    // Ok, read it in.
    //
    STARTLOG(LOG_STARTUP, "INI", "LOAD")
    log_text(T("Loading: "));
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
        log_text(T("Error loading "));
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
            log_text(T("Attributes are not present in either the input file or the attribute database."));
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
            log_text(T("Attributes present in both the input file and the attribute database."));
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
        if (mux_fopen(&f, mudconf.mail_db, T("rb")))
        {
            DebugTotalFiles++;
            setvbuf(f, nullptr, _IOFBF, 16384);
            Log.tinyprintf(T("LOADING: %s" ENDLINE), mudconf.mail_db);
            load_mail(f);
            Log.tinyprintf(T("LOADING: %s (done)" ENDLINE), mudconf.mail_db);
            if (fclose(f) == 0)
            {
                DebugTotalFiles--;
            }
            f = 0;
        }
    }
    STARTLOG(LOG_STARTUP, "INI", "LOAD");
    log_text(T("Load complete."));
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
    UTF8  type,
    UTF8  *str,
    UTF8  *raw_str,
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
#endif // REALITY_LVLS
        {
            bMatch |= atr_match(thing, player, type, str, raw_str, check_parent);
        }

        // Non-authoritative test of circular reference.
        //
        dbref next;
        if (  thing == (next = Next(thing))
           || --limit < 0
           || alarm_clock.alarmed)
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
       || H_Listen(thing))
    {
        return true;
    }

    if (Monitor(thing))
    {
        if (mudstate.bfListens.IsSet(thing))
        {
            return true;
        }
        else if (mudstate.bfNoListens.IsSet(thing))
        {
            return false;
        }
        else
        {
            bool bFoundCommands = false;

            UTF8 *buff = alloc_lbuf("Hearer");
            atr_push();
            unsigned char *as;
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

                UTF8 *s = nullptr;
                if (  AMATCH_CMD    == buff[0]
                   || AMATCH_LISTEN == buff[0])
                {
                    s = (UTF8 *)strchr((char *)buff+1, ':');
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
    }
    return false;
}

void do_readcache(dbref executor, dbref caller, dbref enactor, int eval, int key)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(key);

    helpindex_load(executor);
    fcache_load(executor);
}

static void process_preload(void)
{
    dbref thing, parent, aowner;
    int aflags, lev;
    UTF8 *tstr;

    tstr = alloc_lbuf("process_preload.string");
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
                if (H_Startup(thing))
                {
                    did_it(Owner(thing), thing, 0, nullptr, 0, nullptr, A_STARTUP,
                        0, nullptr, 0);

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
                FWDLIST *fp = fwdlist_load(GOD, tstr);
                if (nullptr != fp)
                {
                    fwdlist_set(thing, fp);

                    if (fp->data)
                    {
                        delete [] fp->data;
                    }
                    delete fp;
                    fp = nullptr;
                }
            }
        }
    }
    free_lbuf(tstr);
}

#ifndef MEMORY_BASED

/*
 * ---------------------------------------------------------------------------
 * * info: display info about the file being read or written.
 */

static void info(int fmt, int flags, int ver)
{
    const UTF8 *cp;

    if (fmt == F_MUX)
    {
        cp = T("MUX");
    }
    else
    {
        cp = T("*unknown*");
    }
    Log.tinyprintf(T("%s version %d:"), cp, ver);
    if (  ver < MIN_SUPPORTED_VERSION
       || MAX_SUPPORTED_VERSION < ver)
    {
        Log.WriteString(T(" Unsupported version"));
        exit(1);
    }
    else if (  (  (  1 == ver
                  || 2 == ver)
               && (flags & MANDFLAGS_V2) != MANDFLAGS_V2)
            || (  3 == ver
               && (flags & MANDFLAGS_V3) != MANDFLAGS_V3)
            || (  4 == ver
               && (flags & MANDFLAGS_V4) != MANDFLAGS_V4))
    {
        Log.WriteString(T(" Unsupported flags"));
        exit(1);
    }
    if (flags & V_DATABASE)
        Log.WriteString(T(" Database"));
    if (flags & V_ATRNAME)
        Log.WriteString(T(" AtrName"));
    if (flags & V_ATRKEY)
        Log.WriteString(T(" AtrKey"));
    if (flags & V_ATRMONEY)
        Log.WriteString(T(" AtrMoney"));
    Log.WriteString(T(ENDLINE));
}

static const UTF8 *standalone_infile = nullptr;
static const UTF8 *standalone_outfile = nullptr;
static const UTF8 *standalone_basename = nullptr;
static bool standalone_check = false;
static bool standalone_load = false;
static bool standalone_unload = false;

static void dbconvert(void)
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags;

    Log.SetBasename(T("-"));
    Log.StartLogging();

    SeedRandomNumberGenerator();

    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));
    pool_init(POOL_STRING, sizeof(mux_string));

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

    UTF8 dirfile[SIZEOF_PATHNAME];
    UTF8 *dirfile_c = dirfile;
    safe_copy_str(standalone_basename, dirfile, &dirfile_c, (SIZEOF_PATHNAME-1));
    safe_copy_str(T(".dir"), dirfile, &dirfile_c, (SIZEOF_PATHNAME-1));
    *dirfile_c = '\0';

    UTF8 pagfile[SIZEOF_PATHNAME];
    UTF8 *pagfile_c = pagfile;
    safe_copy_str(standalone_basename, pagfile, &pagfile_c, (SIZEOF_PATHNAME-1));
    safe_copy_str(T(".pag"), pagfile, &pagfile_c, (SIZEOF_PATHNAME-1));
    *pagfile_c = '\0';

    int cc = init_dbfile(dirfile, pagfile, 650);
    if (cc == HF_OPEN_STATUS_ERROR)
    {
        Log.tinyprintf(T("Can\xE2\x80\x99t open database in (%s, %s) files\n"), dirfile, pagfile);
        exit(1);
    }
    else if (cc == HF_OPEN_STATUS_OLD)
    {
        if (setflags == OUTPUT_FLAGS)
        {
            Log.tinyprintf(T("Would overwrite existing database (%s, %s)\n"), dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }
    else if (cc == HF_OPEN_STATUS_NEW)
    {
        if (setflags == UNLOAD_FLAGS)
        {
            Log.tinyprintf(T("Database (%s, %s) is empty.\n"), dirfile, pagfile);
            CLOSE;
            exit(1);
        }
    }

    FILE *fpIn;
    if (!mux_fopen(&fpIn, standalone_infile, T("rb")))
    {
        exit(1);
    }

    // Go do it.
    //
    if (do_redirect)
    {
        cache_redirect();
    }

    setvbuf(fpIn, nullptr, _IOFBF, 16384);
    if (db_read(fpIn, &db_format, &db_ver, &db_flags) < 0)
    {
        cache_cleanup();
        exit(1);
    }

    if (do_redirect)
    {
        cache_pass2();
    }
    Log.WriteString(T("Input: "));
    info(db_format, db_flags, db_ver);

    if (standalone_check)
    {
        do_dbck(NOTHING, NOTHING, NOTHING, 0, DBCK_FULL);
    }
    fclose(fpIn);

    if (do_write)
    {
        FILE *fpOut;
        if (!mux_fopen(&fpOut, standalone_outfile, T("wb")))
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
        Log.WriteString(T("Output: "));
        info(F_MUX, db_flags, db_ver);
        setvbuf(fpOut, nullptr, _IOFBF, 16384);
#ifndef MEMORY_BASED
        // Save cached modified attribute list
        //
        al_store();
#endif // MEMORY_BASED
        db_write(fpOut, F_MUX, db_ver | db_flags);
        fclose(fpOut);
    }
    CLOSE;
#ifdef SELFCHECK
    db_free();
#endif
    exit(0);
}
#endif // MEMORY_BASED

static void write_pidfile(const UTF8 *pFilename)
{
    FILE *fp;
    if (mux_fopen(&fp, pFilename, T("wb")))
    {
        mux_fprintf(fp, T("%d" ENDLINE), game_pid);
        fclose(fp);
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "PID", "FAIL");
        Log.tinyprintf(T("Failed to write pidfile %s\n"), pFilename);
        ENDLOG;
    }
}

#ifdef INLINESQL
static void init_sql(void)
{
    if ('\0' != mudconf.sql_server[0])
    {
        STARTLOG(LOG_STARTUP,"SQL","CONN");
        log_text(T("Connecting: "));
        log_text(mudconf.sql_database);
        log_text(T("@"));
        log_text(mudconf.sql_server);
        log_text(T(" as "));
        log_text(mudconf.sql_user);
        ENDLOG;

        mush_database = mysql_init(nullptr);

        if (mush_database)
        {
#ifdef MYSQL_OPT_RECONNECT
            // As of MySQL 5.0.3, the default is no longer to reconnect.
            //
            my_bool reconnect = 1;
            mysql_options(mush_database, MYSQL_OPT_RECONNECT, (const char *)&reconnect);
#endif
            mysql_options(mush_database, MYSQL_SET_CHARSET_NAME, "utf8");

            if (mysql_real_connect(mush_database,
                       (char *)mudconf.sql_server, (char *)mudconf.sql_user,
                       (char *)mudconf.sql_password,
                       (char *)mudconf.sql_database, 0, nullptr, 0))
            {
#ifdef MYSQL_OPT_RECONNECT
                // Before MySQL 5.0.19, mysql_real_connect sets the option
                // back to default, so we set it again.
                //
                mysql_options(mush_database, MYSQL_OPT_RECONNECT, (const char *)&reconnect);
#endif
                STARTLOG(LOG_STARTUP,"SQL","CONN");
                log_text(T("Connected to MySQL"));
                ENDLOG;
            }
            else
            {
                STARTLOG(LOG_STARTUP,"SQL","CONN");
                log_text(T("Unable to connect"));
                ENDLOG;
                mysql_close(mush_database);
                mush_database = nullptr;
            }
        }
        else
        {
            STARTLOG(LOG_STARTUP,"SQL","CONN");
            log_text(T("MySQL Library unavailable"));
            ENDLOG;
        }
    }
}

#endif // INLINESQL
long DebugTotalFiles = 3;
long DebugTotalSockets = 0;
#if defined(WINDOWS_NETWORKING)
long DebugTotalThreads = 1;
long DebugTotalSemaphores = 0;
#endif // WINDOWS_NETWORKING
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

static bool bMinDB = false;
static bool bSyntaxError = false;
static const UTF8 *conffile = nullptr;
static bool bVersion = false;
static const UTF8 *pErrorBasename = T("");
static bool bServerOption = false;

#define NUM_CLI_OPTIONS (sizeof(OptionTable)/sizeof(OptionTable[0]))
static CLI_OptionEntry OptionTable[] =
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

static void CLI_CallBack(CLI_OptionEntry *p, const char *pValue)
{
    if (p)
    {
        switch (p->m_Unique)
        {
        case CLI_DO_PID_FILE:
            bServerOption = true;
            mudconf.pid_file = (UTF8 *)pValue;
            break;

        case CLI_DO_CONFIG_FILE:
            bServerOption = true;
            conffile = (UTF8 *)pValue;
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
            pErrorBasename = (UTF8 *)pValue;
            break;

#ifndef MEMORY_BASED
        case CLI_DO_INFILE:
            mudstate.bStandAlone = true;
            standalone_infile = (UTF8 *)pValue;
            break;

        case CLI_DO_OUTFILE:
            mudstate.bStandAlone = true;
            standalone_outfile = (UTF8 *)pValue;
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
            standalone_basename = (UTF8 *)pValue;
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

#if defined(WINDOWS_NETWORKING)
void DetectWindowsCapabilities()
{
    // Get a handle to ws2_32.dll
    //
    HINSTANCE hInstWs2_32 = LoadLibrary(L"ws2_32");
    if (nullptr != hInstWs2_32)
    {
        fpGetNameInfo = (FGETNAMEINFO *)GetProcAddress(hInstWs2_32, "getnameinfo");
        fpGetAddrInfo = (FGETADDRINFO *)GetProcAddress(hInstWs2_32, "getaddrinfo");
        fpFreeAddrInfo = (FFREEADDRINFO *)GetProcAddress(hInstWs2_32, "freeaddrinfo");

        // These interfaces are all-or-nothing.
        //
        if (  nullptr == fpGetNameInfo
           || nullptr == fpGetAddrInfo
           || nullptr == fpFreeAddrInfo)
        {
            fpGetNameInfo = nullptr;
            fpGetAddrInfo = nullptr;
            fpFreeAddrInfo = nullptr;
            FreeLibrary(hInstWs2_32);
            hInstWs2_32 = nullptr;
        }
    }

    if (nullptr == hInstWs2_32)
    {
        // Get a handle to wship6.dll (part of the Windows 2000 IPv6 technology preview).
        //
        HINSTANCE hInstWship6 = LoadLibrary(L"wship6");
        if (nullptr != hInstWship6)
        {
            fpGetNameInfo = (FGETNAMEINFO *)GetProcAddress(hInstWship6, "getnameinfo");
            fpGetAddrInfo = (FGETADDRINFO *)GetProcAddress(hInstWship6, "getaddrinfo");
            fpFreeAddrInfo = (FFREEADDRINFO *)GetProcAddress(hInstWship6, "freeaddrinfo");

            // These interfaces are all-or-nothing.
            //
            if (  nullptr == fpGetNameInfo
               || nullptr == fpGetAddrInfo
               || nullptr == fpFreeAddrInfo)
            {
                fpGetNameInfo = nullptr;
                fpGetAddrInfo = nullptr;
                fpFreeAddrInfo = nullptr;
                FreeLibrary(hInstWship6);
                hInstWship6 = nullptr;
            }
        }
    }
}
#endif // WINDOWS_NETWORKING

#define DBCONVERT_NAME1 T("dbconvert")
#define DBCONVERT_NAME2 T("dbconvert.exe")

int DCL_CDECL main(int argc, char *argv[])
{
    FLOAT_Initialize();

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
    if (  mux_stricmp((UTF8 *)pProg, DBCONVERT_NAME1) == 0
       || mux_stricmp((UTF8 *)pProg, DBCONVERT_NAME2) == 0)
    {
        mudstate.bStandAlone = true;
    }

    mudconf.pid_file = (UTF8 *)"netmux.pid";

    // Parse the command line
    //
    CLI_Process(argc, argv, OptionTable, NUM_CLI_OPTIONS, CLI_CallBack);

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
        mux_fprintf(stderr, T("Version: %s" ENDLINE), mudstate.version);
        return 1;
    }
    if (  bSyntaxError
       || conffile == nullptr
       || !bServerOption)
    {
        mux_fprintf(stderr, T("Version: %s" ENDLINE), mudstate.version);
        if (mudstate.bStandAlone)
        {
            mux_fprintf(stderr, T("Usage: %s -d <dbname> -i <infile> [-o <outfile>] [-l|-u|-k]" ENDLINE), pProg);
            mux_fprintf(stderr, T("  -d  Basename." ENDLINE));
            mux_fprintf(stderr, T("  -i  Input file." ENDLINE));
            mux_fprintf(stderr, T("  -k  Check." ENDLINE));
            mux_fprintf(stderr, T("  -l  Load." ENDLINE));
            mux_fprintf(stderr, T("  -o  Output file." ENDLINE));
            mux_fprintf(stderr, T("  -u  Unload." ENDLINE));
        }
        else
        {
            mux_fprintf(stderr, T("Usage: %s [-c <filename>] [-p <filename>] [-h] [-s] [-v]" ENDLINE), pProg);
            mux_fprintf(stderr, T("  -c  Specify configuration file." ENDLINE));
            mux_fprintf(stderr, T("  -e  Specify logfile basename (or '-' for stderr)." ENDLINE));
            mux_fprintf(stderr, T("  -h  Display this help." ENDLINE));
            mux_fprintf(stderr, T("  -p  Specify process ID file." ENDLINE));
            mux_fprintf(stderr, T("  -s  Start with a minimal database." ENDLINE));
            mux_fprintf(stderr, T("  -v  Display version string." ENDLINE ENDLINE));
        }
        return 1;
    }

    mudstate.bStandAlone = false;

    // Initialize Modules very, very early.
    //
    MUX_RESULT mr = init_modules();

    // TODO: Create platform interface

    TIME_Initialize();
    SeedRandomNumberGenerator();

    Log.SetBasename(pErrorBasename);
    Log.StartLogging();

    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
    if (MUX_SUCCEEDED(mr))
    {
        log_printf(T("Registered netmux modules."));
    }
    else
    {
        log_printf(T("Failed either to initialize module subsystem or register netmux modules (%d)."), mr);
    }
    ENDLOG;

    game_pid = mux_getpid();
    write_pidfile(mudconf.pid_file);

    build_signal_names_table();

#ifdef MEMORY_ACCOUNTING
    extern CHashFile hfAllocData;
    extern CHashFile hfIdentData;
    extern bool bMemAccountingInitialized;
    hfAllocData.Open("svdptrs.dir", "svdptrs.pag", 40);
    hfIdentData.Open("svdlines.dir", "svdlines.pag", 40);
    bMemAccountingInitialized = true;
#endif

#if defined(WINDOWS_NETWORKING)

    game_process_handle = GetCurrentProcess();
    DetectWindowsCapabilities();

    // Initialize WinSock.
    //
    WORD wVersionRequested = MAKEWORD(2,2);
    WSADATA wsaData;
    if (WSAStartup(wVersionRequested, &wsaData) != 0)
    {
        Log.WriteString(T("ERROR: Could not initialize WinSock." ENDLINE));
        return 101;
    }

    if (  LOBYTE(wsaData.wVersion) != 2
       || HIBYTE(wsaData.wVersion) != 2)
    {
        // We can't run on this version of WinSock.
        //
        Log.tinyprintf(T("INFO: WinSock v%d.%d instead of v2.2." ENDLINE),
            LOBYTE(wsaData.wVersion), HIBYTE(wsaData.wVersion));
        //WSACleanup();
        //return 102;
    }

#endif // WINDOWS_NETWORKING

    mudstate.restart_time.GetUTC();
    mudstate.start_time = mudstate.restart_time;
    mudstate.restart_count= 0;

    mudstate.cpu_count_from.GetUTC();
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));

    pool_init(POOL_DESC, sizeof(DESC));
    pool_init(POOL_QENTRY, sizeof(BQUE));
    pool_init(POOL_LBUFREF, sizeof(lbuf_ref));
    pool_init(POOL_REGREF, sizeof(reg_ref));
    pool_init(POOL_STRING, sizeof(mux_string));
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

    // The module subsystem must be ready to go before the configuration files
    // are consumed.  However, this means that the modules can't really do
    // much until they get a notification that the part of loading they depend
    // on is complete.
    //
#if defined(HAVE_WORKING_FORK) && defined(STUB_SLAVE)
    boot_stubslave(GOD, GOD, GOD, 0);
    init_stubslave();
#endif // HAVE_WORKING_FORK && STUB_SLAVE

    mudconf.config_file = StringClone(conffile);
    mudconf.log_dir = StringClone(pErrorBasename);
    cf_read();

    mr = mux_CreateInstance(CID_QueryServer, nullptr, UseSlaveProcess, IID_IQueryControl, (void **)&mudstate.pIQueryControl);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mudstate.pIQueryControl->Connect(mudconf.sql_server, mudconf.sql_database, mudconf.sql_user, mudconf.sql_password);
        if (MUX_SUCCEEDED(mr))
        {
            mux_IQuerySink *pIQuerySink = nullptr;
            mr = mux_CreateInstance(CID_QueryClient, nullptr, UseSameProcess, IID_IQuerySink, (void **)&pIQuerySink);
            if (MUX_SUCCEEDED(mr))
            {
                mr = mudstate.pIQueryControl->Advise(pIQuerySink);
                if (MUX_SUCCEEDED(mr))
                {
                    pIQuerySink->Release();
                    pIQuerySink = nullptr;
                }
                else
                {
                    mudstate.pIQueryControl->Release();
                    mudstate.pIQueryControl = nullptr;

                    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
                    log_printf(T("Couldn\xE2\x80\x99t connect sink to server (%d)."), mr);
                    ENDLOG;
                }
            }
            else
            {
                mudstate.pIQueryControl->Release();
                mudstate.pIQueryControl = nullptr;

                STARTLOG(LOG_ALWAYS, "INI", "LOAD");
                log_printf(T("Couldn\xE2\x80\x99t create Query Sink (%d)."), mr);
                ENDLOG;
            }
        }
        else
        {
            mudstate.pIQueryControl->Release();
            mudstate.pIQueryControl = nullptr;

            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf(T("Couldn\xE2\x80\x99t connect to Query Server (%d)."), mr);
            ENDLOG;
        }
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_text(T("Couldn\xE2\x80\x99t create interface to Query Server."));
        ENDLOG;
    }

#if defined(INLINESQL)
    init_sql();
#endif // INLINESQL

#ifdef UNIX_SSL
    if (!initialize_ssl())
    {
        // Do some extra handling?
        // We do log all errors in initialize_ssl, so it may be unneeded.
    }
#endif

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
        log_text(T("Couldn\xE2\x80\x99t load text database: "));
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
            log_text(T("Couldn\xE2\x80\x99t load: "));
            log_text(mudconf.indb);
            ENDLOG
            return 2;
        }
    }
    set_signals();
    Guest.StartUp();

    // Do a consistency check and set up the freelist
    //
    do_dbck(NOTHING, NOTHING, NOTHING, 0, 0);

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
    hashreset(&mudstate.reference_htab);

    ValidateConfigurationDbrefs();
    process_preload();

#if defined(HAVE_WORKING_FORK)
    load_restart_db();
    if (!mudstate.restarting)
#endif // HAVE_WORKING_FORK
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
#ifdef UNIX_SSL
    SetupPorts(&num_main_game_ports, main_game_ports, &mudconf.ports, &mudconf.sslPorts, mudconf.ip_address);
#else
    SetupPorts(&num_main_game_ports, main_game_ports, &mudconf.ports, nullptr, mudconf.ip_address);
#endif

#if defined(HAVE_WORKING_FORK) || defined(WINDOWS_THREADS)
    boot_slave(GOD, GOD, GOD, 0, 0);
#endif // HAVE_WORKING_FORK

    // All intialization should be complete, allow the local
    // extensions to configure themselves.
    //
    local_startup();

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->startup();
        p = p->pNext;
    }

    init_timer();

    shovechars(num_main_game_ports, main_game_ports);

#ifdef INLINESQL
     if (mush_database)
     {
         mysql_close(mush_database);
         mush_database = nullptr;
         STARTLOG(LOG_STARTUP,"SQL","DISC");
         log_text(T("SQL shut down"));
         ENDLOG;
     }
#endif // INLINESQL

    close_sockets(false, T("Going down - Bye"));
    dump_database();

    // All shutdown, barring logfiles, should be done, shutdown the
    // local extensions.
    //
    local_shutdown();

    p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->shutdown();
        p = p->pNext;
    }
#if defined(STUB_SLAVE)
    final_stubslave();
#endif // STUB_SLAVE
    final_modules();
    CLOSE;

#if defined(HAVE_WORKING_FORK)
    CleanUpSlaveSocket();
    CleanUpSlaveProcess();
#ifdef STUB_SLAVE
    CleanUpStubSlaveSocket();
    WaitOnStubSlaveProcess();
#endif
#endif // HAVE_WORKING_FORK
#if defined(WINDOWS_NETWORKING)
    shutdown_slave();
#endif

#ifdef SELFCHECK
    // Go ahead and explicitly free the memory for these things so
    // that it's easy to spot unintentional memory leaks.
    //
    int i;
    for (i = 0; i < mudstate.nHelpDesc; i++)
    {
        helpindex_clean(i);
    }

    finish_mail();
    finish_cmdtab();
    db_free();
#endif

#if defined(WINDOWS_NETWORKING)
    // Critical section not needed any more.
    //
    DeleteCriticalSection(&csDescriptorList);
    WSACleanup();
#endif // WINDOWS_NETWORKING

#ifdef UNIX_SSL
    shutdown_ssl();
#endif

    return 0;
}

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
void init_rlimit(void)
{
    struct rlimit *rlp;

    rlp = (struct rlimit *)alloc_lbuf("rlimit");

    if (getrlimit(RLIMIT_NOFILE, rlp))
    {
        log_perror(T("RLM"), T("FAIL"), nullptr, T("getrlimit()"));
        free_lbuf(rlp);
        return;
    }
    rlp->rlim_cur = rlp->rlim_max;
    if (setrlimit(RLIMIT_NOFILE, rlp))
    {
        log_perror(T("RLM"), T("FAIL"), nullptr, T("setrlimit()"));
    }
    free_lbuf(rlp);

}
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

bool mux_fopen(FILE **pFile, const UTF8 *filename, const UTF8 *mode)
{
    if (pFile)
    {
        *pFile = nullptr;
        if (  nullptr != filename
           && nullptr != mode)
        {
#if defined(WINDOWS_FILES) && !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
            // 1400 is Visual C++ 2005
            //
            return (fopen_s(pFile, (const char *)filename, (const char *)mode) == 0);
#else
            *pFile = fopen((char *)filename, (char *)mode);
            if (nullptr != *pFile)
            {
                return true;
            }
#endif // WINDOWS_FILES
        }
    }
    return false;
}

bool mux_open(int *pfh, const UTF8 *filename, int oflag)
{
    if (nullptr != pfh)
    {
        *pfh = MUX_OPEN_INVALID_HANDLE_VALUE;
        if (nullptr != filename)
        {
#if defined(WINDOWS_FILES) && !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
            // 1400 is Visual C++ 2005
            //
            return (_sopen_s(pfh, (const char *)filename, oflag, _SH_DENYNO, _S_IREAD|_S_IWRITE) == 0);
#elif defined(WINDOWS_FILES)
            *pfh = _open((char *)filename, oflag, _S_IREAD|_S_IWRITE);
            return (0 <= *pfh);
#else
            *pfh = open((char *)filename, oflag, 0600);
            return (0 <= *pfh);
#endif
        }
    }
    return false;
}

const UTF8 *mux_strerror(int errnum)
{
#if defined(WINDOWS_FILES) && !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
    // 1400 is Visual C++ 2005
    //
    static UTF8 buffer[80];
    strerror_s((char *)buffer, sizeof(buffer), errnum);
    return buffer;
#else
    return (UTF8 *)strerror(errnum);
#endif
}
