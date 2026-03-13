/*! \file engine.cpp
 * \brief Game engine logic: notification, matching, dumps, and loading.
 *
 * Functions in this file implement game-level semantics including
 * notification dispatch, attribute matching, database dumps and loads,
 * and other engine-side operations.  Driver-level startup, CLI parsing,
 * and process management are in driver.cpp.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "color_ops.h"
#include "sqlite_backend.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

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
    log_text(g_debug_cmd);
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
    PCRE2_SIZE erroffset;
    int errcode;

    /*
     * Load the regexp pattern. This allocates memory which must be
     * later freed. PCRE2 code objects must be explicitly freed with pcre2_code_free().
     */
    pcre2_code *re;
    if (alarm_clock.alarmed)
    {
        return false;
    }

    re = pcre2_compile_8(pattern, PCRE2_ZERO_TERMINATED, PCRE2_UTF|case_opt,
                         &errcode, &erroffset, nullptr);
    if (re == nullptr)
    {
        /*
         * This is a matching error. We have an error message in
         * regexp_errbuf that we can ignore, since we're doing
         * command-matching.
         */
        return false;
    }

    // Create match data block for storing results
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
    if (match_data == nullptr)
    {
        pcre2_code_free(re);
        return false;
    }

    /*
     * Now we try to match the pattern. The relevant fields will
     * automatically be filled in by this.
     */
    matches = pcre2_match(re, str, PCRE2_ZERO_TERMINATED, 0, 0, match_data, nullptr);

    if (matches < 0)
    {
        pcre2_match_data_free(match_data);
        pcre2_code_free(re);
        return false;
    }

    /*
     * Now we fill in our args vector. Note that in regexp matching,
     * 0 is the entire string matched, and the parenthesized strings
     * go from 1 to 9. We DO PRESERVE THIS PARADIGM, for consistency
     * with other languages.
     */
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);

    for (i = 0; i < nargs && i < matches; ++i)
    {
        args[i] = alloc_lbuf("regexp_match");
        PCRE2_SIZE substring_length = 0;

        // Calculate substring length (equivalent to what pcre_copy_substring would do)
        int rc = pcre2_substring_length_bynumber(match_data, i, &substring_length);
        if (rc < 0 || substring_length >= LBUF_SIZE)
        {
            free_lbuf(args[i]);
            args[i] = nullptr;
            continue;
        }

        // Get the substring
        PCRE2_SIZE outlen = 0;
        rc = pcre2_substring_copy_bynumber(match_data, i, args[i], &outlen);
        if (rc < 0)
        {
            free_lbuf(args[i]);
            args[i] = nullptr;
        }
    }

    // Fill any remaining args with nullptr
    for (; i < nargs; ++i)
    {
        args[i] = nullptr;
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
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
                         && mudstate.parent_htab.find(ap->number) == mudstate.parent_htab.end())))
                {
                    mudstate.parent_htab.emplace(ap->number, &atr);
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
            s = find_pattern_delimiter(buff + 1);
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
              || mudstate.parent_htab.find(ap->number) != mudstate.parent_htab.end()))
        {
            continue;
        }

        // If we aren't the top level, remember this attr so we
        // exclude it from now on.
        //
        if (hash_insert)
        {
            mudstate.parent_htab.emplace(ap->number, &atr);
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
        unescape_pattern_colons(buff + 1);
        if (AMATCH_LISTEN == type)
        {
            strip_fancy_quotes(buff + 1);
        }

        UTF8 *args[NUM_ENV_VARS];
        if (  (  0 != (aflags & AF_REGEXP)
            && regexp_match(buff + 1, (aflags & AF_NOPARSE) ? raw_str : str,
                ((aflags & AF_CASE) ? PCRE2_CASELESS : 0), args, NUM_ENV_VARS))
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
    mudstate.parent_htab.clear();
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
    if ((aflags & AF_NOEVAL) || NoEval(object))
    {
        mux_strncpy(nbuf, buf, LBUF_SIZE-1);
        dp = nbuf + strlen((const char *)nbuf);
    }
    else
    {
        mux_exec(buf, LBUF_SIZE-1, nbuf, &dp, object, player, player,
            AttrTrace(aflags, EV_FIGNORE|EV_EVAL|EV_TOP),
            nullptr, 0);
    }
    *dp = '\0';
    strip_fancy_quotes(nbuf);
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
        int case_opt = (aflags & AF_CASE) ? PCRE2_CASELESS : 0;
        do
        {
            PCRE2_SIZE erroffset;
            int errcode;
            UTF8 *cp = parse_to(&dp, ',', EV_STRIP_CURLY);
            pcre2_code *re;
            if (!alarm_clock.alarmed)
            {
                re = pcre2_compile_8(cp, PCRE2_ZERO_TERMINATED, PCRE2_UTF|case_opt,
                                   &errcode, &erroffset, nullptr);
                if (re != nullptr)
                {
                    // Create match data block for storing results
                    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
                    if (match_data != nullptr)
                    {
                        int matches = pcre2_match(re, msg, PCRE2_ZERO_TERMINATED, 0, 0,
                                               match_data, nullptr);

                        if (0 <= matches)
                        {
                            pcre2_match_data_free(match_data);
                            pcre2_code_free(re);
                            free_lbuf(nbuf);
                            return false;
                        }
                        pcre2_match_data_free(match_data);
                    }
                    pcre2_code_free(re);
                }
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
        if ((aflags & AF_NOEVAL) || NoEval(object))
        {
            cp = buf + strlen((const char *)buf);
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

    // msg_ns: NOSPOOF prefix + msg.  All strings are PUA-encoded UTF-8.
    //
    UTF8 *msg_ns = alloc_lbuf("notify_check.msg_ns");
    UTF8 *bp_ns = msg_ns;
    UTF8 *msgFinal = alloc_lbuf("notify_check.final");
    UTF8 *bp_final;
    UTF8 *tp;
    UTF8 *prefix;
    dbref aowner, recip, obj;
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
            if (  mudconf.terse_nospoof
               && (key & MSG_SAYPOSE))
            {
                // Terse: just the dbref for say/pose.
                //
                safe_chr('[', msg_ns, &bp_ns);
                safe_chr('#', msg_ns, &bp_ns);
                safe_ltoa(sender, msg_ns, &bp_ns);
                safe_str(T("] "), msg_ns, &bp_ns);
            }
            else
            {
                safe_chr('[', msg_ns, &bp_ns);
                safe_str(Moniker(sender), msg_ns, &bp_ns);
                safe_chr('(', msg_ns, &bp_ns);
                safe_chr('#', msg_ns, &bp_ns);
                safe_ltoa(sender, msg_ns, &bp_ns);
                safe_chr(')', msg_ns, &bp_ns);

                if (sender != Owner(sender))
                {
                    safe_chr('{', msg_ns, &bp_ns);
                    safe_str(Moniker(Owner(sender)), msg_ns, &bp_ns);
                    safe_chr('}', msg_ns, &bp_ns);
                }

                if (sender != mudstate.curr_enactor)
                {
                    safe_str(T("<-("), msg_ns, &bp_ns);
                    safe_chr('#', msg_ns, &bp_ns);
                    safe_ltoa(mudstate.curr_enactor, msg_ns, &bp_ns);
                    safe_chr(')', msg_ns, &bp_ns);
                }

                switch (DecodeMsgSource(key))
                {
                case MSG_SRC_COMSYS:
                    safe_str(T(",comsys"), msg_ns, &bp_ns);
                    break;

                case MSG_SRC_KILL:
                    safe_str(T(",kill"), msg_ns, &bp_ns);
                    break;

                case MSG_SRC_GIVE:
                    safe_str(T(",give"), msg_ns, &bp_ns);
                    break;

                case MSG_SRC_PAGE:
                    safe_str(T(",page"), msg_ns, &bp_ns);
                    break;

                default:
                    if (key & MSG_SAYPOSE)
                    {
                        safe_str(T(",saypose"), msg_ns, &bp_ns);
                    }
                    break;
                }

                safe_str(T("] "), msg_ns, &bp_ns);
            }
        }
    }
    safe_str(msg, msg_ns, &bp_ns);
    *bp_ns = '\0';

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
            else if (Html(target))
            {
                bp_final = msgFinal;
                html_escape(msg_ns, msgFinal, &bp_final);
                raw_notify(target, msgFinal);
            }
            else
            {
                raw_notify(target, msg_ns);
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
            bp_final = msgFinal;
            safe_str(Moniker(target), msgFinal, &bp_final);
            safe_str(T("> "), msgFinal, &bp_final);
            safe_str(msg_ns, msgFinal, &bp_final);
            *bp_final = '\0';
            raw_notify(Owner(target), msgFinal);
        }

        // Check for @Listen match if it will be useful.
        // Strip PUA color from msg to get plain text for matching.
        //
        UTF8 *msgPlain = alloc_lbuf("notify_check.plain");
        co_strip_color(reinterpret_cast<unsigned char *>(msgPlain),
                       reinterpret_cast<const unsigned char *>(msg),
                       mux_strlen(msg));
        strip_fancy_quotes(msgPlain);
        bool pass_listen = false;
        UTF8 *args[NUM_ENV_VARS];
        nargs = 0;
        if (  check_listens
           && (key & (MSG_ME | MSG_INV_L))
           && H_Listen(target))
        {
            tp = atr_get("notify_check.790", target, A_LISTEN, &aowner, &aflags);
            strip_fancy_quotes(tp);
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
                bp_final = msgFinal;
                safe_str(prefix, msgFinal, &bp_final);
                free_lbuf(prefix);
                safe_str(msg, msgFinal, &bp_final);
                *bp_final = '\0';

                for (i = 0; i < fp->count; i++)
                {
                    recip = fp->data[i];
                    if (  !Good_obj(recip)
                       || recip == target)
                    {
                        continue;
                    }
                    notify_check(recip, sender, msgFinal,
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
                    bp_final = msgFinal;
                    safe_str(prefix, msgFinal, &bp_final);
                    free_lbuf(prefix);
                    safe_str(msg, msgFinal, &bp_final);
                    *bp_final = '\0';

                    notify_check(recip, sender, msgFinal,
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
                bp_final = msgFinal;
                safe_str(prefix, msgFinal, &bp_final);
                free_lbuf(prefix);
                safe_str(msg, msgFinal, &bp_final);
                *bp_final = '\0';
            }
            else
            {
                mux_strncpy(msgFinal, msg, LBUF_SIZE - 1);
            }

            UTF8 *msgPrefixed2 = alloc_lbuf("notify_check.pfx2");
            UTF8 *bp_pfx2;

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
                    bp_pfx2 = msgPrefixed2;
                    safe_str(prefix, msgPrefixed2, &bp_pfx2);
                    free_lbuf(prefix);
                    safe_str(msgFinal, msgPrefixed2, &bp_pfx2);
                    *bp_pfx2 = '\0';

                    notify_check(recip, sender, msgPrefixed2,
                        MSG_ME | MSG_F_UP | MSG_F_CONTENTS | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
                }
            }
            free_lbuf(msgPrefixed2);
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
                bp_final = msgFinal;
                safe_str(prefix, msgFinal, &bp_final);
                free_lbuf(prefix);
                safe_str(msg, msgFinal, &bp_final);
                *bp_final = '\0';
            }
            else
            {
                mux_strncpy(msgFinal, msg, LBUF_SIZE - 1);
            }

            DOLIST(obj, Contents(target))
            {
                if (  obj != target
                   && !(isPlayer(obj) && Alone(target)))
                {
                    notify_check(obj, sender, msgFinal,
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
                bp_final = msgFinal;
                safe_str(prefix, msgFinal, &bp_final);
                free_lbuf(prefix);
                safe_str(msg, msgFinal, &bp_final);
                *bp_final = '\0';
            }
            else
            {
                mux_strncpy(msgFinal, msg, LBUF_SIZE - 1);
            }

            DOLIST(obj, Contents(targetloc))
            {
                if (  obj != target
                   && obj != targetloc
                   && !(isPlayer(obj) && Alone(targetloc)))
                {
                    notify_check(obj, sender, msgFinal,
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
                bp_final = msgFinal;
                safe_str(prefix, msgFinal, &bp_final);
                free_lbuf(prefix);
                safe_str(msg, msgFinal, &bp_final);
                *bp_final = '\0';
            }
            else
            {
                mux_strncpy(msgFinal, msg, LBUF_SIZE - 1);
            }

            notify_check(targetloc, sender, msgFinal,
                MSG_ME | MSG_F_UP | MSG_S_INSIDE | (key & (MSG_SRC_MASK | MSG_SAYPOSE | MSG_OOC)));
        }
        free_lbuf(msgPlain);
    }
    free_lbuf(msgFinal);
    free_lbuf(msg_ns);
    mudstate.ntfy_nest_lev--;
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
        if (  first != exception
           && !(isPlayer(first) && Alone(loc)))
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
           && first != exc2
           && !(isPlayer(first) && Alone(loc)))
        {
            notify_check(first, player, msg, MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE);
        }
    }
}

void notify_except_N(dbref loc, dbref player, dbref aExclude[], int nExclude, const UTF8 *msg, int key)
{
    dbref first;

    // Notify loc itself unless it's in the exclude list.
    //
    bool bExcludeLoc = false;
    int i;
    for (i = 0; i < nExclude; i++)
    {
        if (loc == aExclude[i])
        {
            bExcludeLoc = true;
            break;
        }
    }
    if (!bExcludeLoc)
    {
        notify_check(loc, player, msg, MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A | key);
    }

    DOLIST(first, Contents(loc))
    {
        if (!(isPlayer(first) && Alone(loc)))
        {
            bool bExclude = false;
            for (i = 0; i < nExclude; i++)
            {
                if (first == aExclude[i])
                {
                    bExclude = true;
                    break;
                }
            }
            if (!bExclude)
            {
                notify_check(first, player, msg, MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | key);
            }
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
        mux_write(fd, message, static_cast<unsigned int>(strlen(reinterpret_cast<char *>(message))));
        mux_write(fd, ENDLINE, sizeof(ENDLINE)-1);
        mux_close(fd);
    }

    // Do we perform a normal or an emergency shutdown? Normal
    // shutdown is handled by exiting the GANL main loop,
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

    // Request normal shutdown via COM call to the driver.
    //
    request_shutdown();
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
        // With SQLite write-through, the database is always durable.
        // Only write a flatfile for DUMP_I_FLAT (explicit export).
        //
        if (  dump_type != DUMP_I_FLAT
           && dump_type != DUMP_I_SIGNAL)
        {
            STARTLOG(LOG_DBSAVES, "DMP", "SQLT");
            log_text(T("SQLite write-through is authoritative; skipping flatfile write."));
            ENDLOG;
        }
        else
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
                setvbuf(f, nullptr, _IOFBF, 16384);
                db_write(f, F_MUX, dp->fType);
                fclose(f);

                if (dp->bUseTemporary)
                {
                    ReplaceFile(tmpfile, outfn);
                }
            }
            else
            {
                log_perror(T("DMP"), T("FAIL"), dp->pszErrorMessage, outfn);
            }
        }

        if (!bPotentialConflicts)
        {
        }
        return;
    }

    // With SQLite write-through, periodic and shutdown dumps don't need
    // to write a flatfile.  The WAL checkpoint (via SYNC/cache_sync)
    // is handled by the caller.
    //
    STARTLOG(LOG_DBSAVES, "DMP", "SQLT");
    log_text(T("SQLite checkpoint (no flatfile)."));
    ENDLOG;

}

void dump_database(void)
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

    pcache_sync();
    SYNC;

#if defined(HAVE_WORKING_FORK)
    mudstate.write_protect = true;
    int child = 0;
    bool bChildExists = false;
    mudstate.dumping = true;
    mudstate.dumped  = 0;
    bool bAttemptFork = mudconf.fork_dump;
    if (!(key & DUMP_FLATFILE))
    {
        // With SQLite write-through, periodic dumps are just a WAL
        // checkpoint — fast enough that forking is unnecessary.
        //
        bAttemptFork = false;
    }
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
            if (bAttemptFork)
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

bool clear_sqlite_after_sync_failure(CSQLiteDB &sqldb)
{
    if (!sqldb.Begin())
    {
        sqldb.Rollback();
        if (!sqldb.Begin())
        {
            return false;
        }
    }

    if (  !sqldb.ClearAttributes()
       || !sqldb.ClearObjectTable()
       || !sqldb.ClearAttrNames()
       || !sqldb.PutMeta("attr_next", A_USER_START)
       || !sqldb.PutMeta("db_top", 0)
       || !sqldb.PutMeta("record_players", 0)
       || !sqldb.Commit())
    {
        sqldb.Rollback();
        return false;
    }

    return true;
}

int load_game(int ccPageFile)
{
    FILE *f = nullptr;
    UTF8 infile[SIZEOF_PATHNAME+8];
    struct stat statbuf;
    int db_format, db_version, db_flags;

    mux_strncpy(infile, mudconf.indb, sizeof(infile)-1);
    if (stat(reinterpret_cast<char *>(infile), &statbuf) != 0)
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
    setvbuf(f, nullptr, _IOFBF, 16384);

    // Ok, read it in.
    //
    STARTLOG(LOG_STARTUP, "INI", "LOAD")
    log_text(T("Loading: "));
    log_text(infile);
    ENDLOG

    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();

    // Flatfile import is authoritative for attribute values.
    // Perform clear + import in one transaction so failures rollback.
    //
    if (!sqldb.Begin() || !sqldb.ClearAttributes())
    {
        sqldb.Rollback();
        fclose(f);
        f = 0;
        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("Error clearing SQLite attributes before flatfile load."));
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }

    mudstate.bSQLiteLoading = true;
    if (db_read(f, &db_format, &db_version, &db_flags) < 0)
    {
        mudstate.bSQLiteLoading = false;
        sqldb.Rollback();
        // Everything is not ok.
        //
        fclose(f);
        f = 0;

        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("Error loading "));
        log_text(infile);
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }
    mudstate.bSQLiteLoading = false;
    if (!sqldb.Commit())
    {
        sqldb.Rollback();
        fclose(f);
        f = 0;
        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("Error committing SQLite attributes after flatfile load."));
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }

    // Everything is ok.
    //
    fclose(f);
    f = 0;

    // Bulk-sync object and attribute-name metadata from db[] into SQLite.
    // Attribute values are already authoritative from the import transaction.
    //
    if (!sqlite_sync_runtime())
    {
        if (!clear_sqlite_after_sync_failure(sqldb))
        {
            STARTLOG(LOG_ALWAYS, "INI", "FATAL")
            log_text(T("SQLite cleanup failed after runtime sync failure."));
            ENDLOG
        }
        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("SQLite sync failed after flatfile load."));
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }

    int load_comsys_rc = sqlite_load_comsys();
    if (load_comsys_rc > 0)
    {
        Log.tinyprintf(T("LOADING: comsys (from SQLite)" ENDLINE));
    }
    else if (load_comsys_rc < 0)
    {
        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("SQLite comsys load failed."));
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }
    else
    {
        load_comsys(mudconf.comsys_db);
    }

    int load_mail_rc = sqlite_load_mail();
    if (load_mail_rc > 0)
    {
        Log.tinyprintf(T("LOADING: mail (from SQLite)" ENDLINE));
    }
    else if (load_mail_rc < 0)
    {
        STARTLOG(LOG_ALWAYS, "INI", "FATAL")
        log_text(T("SQLite mail load failed."));
        ENDLOG
        return LOAD_GAME_LOADING_PROBLEM;
    }
    else
    if (mux_fopen(&f, mudconf.mail_db, T("rb")))
    {
        setvbuf(f, nullptr, _IOFBF, 16384);
        Log.tinyprintf(T("LOADING: %s" ENDLINE), mudconf.mail_db);
        load_mail(f);
        Log.tinyprintf(T("LOADING: %s (done)" ENDLINE), mudconf.mail_db);
        if (fclose(f) != 0)
        {
            STARTLOG(LOG_PROBLEMS, "DB", "FCLOSE");
            log_printf(T("fclose failed for %s"), mudconf.mail_db);
            ENDLOG;
        }
        f = 0;
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
                    s = find_pattern_delimiter(buff + 1);
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

void process_preload(void)
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
