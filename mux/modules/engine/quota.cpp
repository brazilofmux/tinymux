/*! \file quota.cpp
 * \brief Quota Management Commands.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// ---------------------------------------------------------------------------
// count_quota, mung_quota, show_quota, do_quota: Manage quotas.
//
// Quota values are softcode-visible attribute integers.  Keep them as
// int64_t end-to-end (#1402): int sinks after mux_atoi64 wrap on every
// platform (e.g. @quota/all 4294967296 wrote 0).
//
static int64_t count_quota(dbref player)
{
    if (Owner(player) != player)
    {
        return 0;
    }
    int64_t q = 0 - static_cast<int64_t>(mudconf.player_quota);

    dbref i;
    DO_WHOLE_DB(i)
    {
        if (Owner(i) != player)
        {
            continue;
        }
        if (Going(i) && (!isRoom(i)))
        {
            continue;
        }
        switch (Typeof(i))
        {
        case TYPE_EXIT:

            q += mudconf.exit_quota;
            break;

        case TYPE_ROOM:

            q += mudconf.room_quota;
            break;

        case TYPE_THING:

            q += mudconf.thing_quota;
            break;

        case TYPE_PLAYER:

            q += mudconf.player_quota;
            break;
        }
    }
    return q;
}

static void mung_quotas(dbref player, int key, int64_t value)
{
    dbref aowner;
    int64_t aq, rq, xq;
    int aflags;

    if (key & QUOTA_FIX)
    {
        // Get value of stuff owned and good value, set other value from that.
        //
        xq = count_quota(player);
        if (key & QUOTA_TOT)
        {
            LBuf buff = LBuf_Adopt(atr_get("mung_quotas.79", player, A_RQUOTA, &aowner, &aflags));
            aq = mux_atoi64(buff) + xq;
            atr_add_raw(player, A_QUOTA, mux_i64toa_t(aq));
        }
        else
        {
            LBuf buff = LBuf_Adopt(atr_get("mung_quotas.86", player, A_QUOTA, &aowner, &aflags));
            rq = mux_atoi64(buff) - xq;
            atr_add_raw(player, A_RQUOTA, mux_i64toa_t(rq));
        }
    }
    else
    {
        // Obtain (or calculate) current relative and absolute quota.
        //
        {
            LBuf buff = LBuf_Adopt(atr_get("mung_quotas.96", player, A_QUOTA, &aowner, &aflags));
            if (!*buff.get())
            {
                LBuf buff2 = LBuf_Adopt(atr_get("mung_quotas.100", player, A_RQUOTA, &aowner, &aflags));
                rq = mux_atoi64(buff2);
                aq = rq + count_quota(player);
            }
            else
            {
                aq = mux_atoi64(buff);
                LBuf buff2 = LBuf_Adopt(atr_get("mung_quotas.109", player, A_RQUOTA, &aowner, &aflags));
                rq = mux_atoi64(buff2);
            }
        }

        // Adjust values.
        //
        if (key & QUOTA_REM)
        {
            aq += (value - rq);
            rq = value;
        }
        else
        {
            rq += (value - aq);
            aq = value;
        }

        // Set both abs and relative quota.
        //
        atr_add_raw(player, A_QUOTA, mux_i64toa_t(aq));
        atr_add_raw(player, A_RQUOTA, mux_i64toa_t(rq));
    }
}

static void show_quota(dbref player, dbref victim)
{
    dbref aowner;
    int aflags;
    LBuf buff = LBuf_Src("show_quota");

    atr_get_str(buff, victim, A_QUOTA, &aowner, &aflags);
    int64_t aq = mux_atoi64(buff);
    atr_get_str(buff, victim, A_RQUOTA, &aowner, &aflags);
    int64_t rq = aq - mux_atoi64(buff);

    mux_field fldName = StripTabsAndTruncate(Name(victim), buff, LBUF_SIZE-1, 16);

    if (!Free_Quota(victim))
    {
        mux_sprintf(buff + fldName.m_byte, LBUF_SIZE - fldName.m_byte,
                    M_(" Quota: %9lld  Used: %9lld"),
                    static_cast<long long>(aq), static_cast<long long>(rq));
    }
    else
    {
        mux_sprintf(buff + fldName.m_byte, LBUF_SIZE - fldName.m_byte,
                     M_(" Quota: UNLIMITED  Used: %9lld"),
                     static_cast<long long>(rq));
    }
    notify_quiet(player, buff);
}

void do_quota
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
    const UTF8 *cargs[],
    int ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!(mudconf.quotas || Quota(executor)))
    {
        notify_quiet(executor, M_("Quotas are not enabled."));
        return;
    }
    if ((key & QUOTA_TOT) && (key & QUOTA_REM))
    {
        notify_quiet(executor, M_("Illegal combination of switches."));
        return;
    }

    dbref who;
    int64_t value = 0;
    int i;
    bool set = false;

    // Show or set all quotas if requested.
    //
    if (key & QUOTA_ALL)
    {
        if (arg1 && *arg1)
        {
            value = mux_atoi64(arg1);
            set = true;
        }
        else if (key & (QUOTA_SET | QUOTA_FIX))
        {
            value = 0;
            set = true;
        }
        if (set)
        {
            STARTLOG(LOG_WIZARD, "WIZ", "QUOTA");
            log_name(executor);
            log_text(T(" changed everyone’s quota"));
            ENDLOG;
        }
        DO_WHOLE_DB(i)
        {
            if (isPlayer(i))
            {
                if (set)
                {
                    mung_quotas(i, key, value);
                }
                show_quota(executor, i);
            }
        }
        return;
    }

    // Find out whose quota to show or set.
    //
    if (!arg1 || *arg1 == '\0')
    {
        who = Owner(executor);
    }
    else
    {
        who = lookup_player(executor, arg1, true);
        if (!Good_obj(who))
        {
            notify_quiet(executor, M_("Not found."));
            return;
        }
    }

    // Make sure we have permission to do it.
    //
    if (!Quota(executor))
    {
        if (arg2 && *arg2)
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            return;
        }
        if (Owner(executor) != who)
        {
            notify_quiet(executor, NOPERM_MESSAGE);
            return;
        }
    }
    if (arg2 && *arg2)
    {
        set = true;
        value = mux_atoi64(arg2);
    }
    else if (key & QUOTA_FIX)
    {
        set = true;
        value = 0;
    }
    if (set)
    {
        STARTLOG(LOG_WIZARD, "WIZ", "QUOTA");
        log_name(executor);
        log_text(T(" changed the quota of "));
        log_name(who);
        ENDLOG;
        mung_quotas(who, key, value);
    }
    show_quota(executor, who);
}

FUNCTION(fun_hasquota)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudconf.quotas)
    {
        safe_str(S_("#-1 Quotas are not enabled."), buff, bufc);
        return;
    }

    // Find out whose quota to show.
    //
    dbref who = lookup_player(executor, fargs[0], true);
    if (!Good_obj(who))
    {
        safe_str(S_("#-1 NOT FOUND"), buff, bufc);
        return;
    }

    // Make sure we have permission to do it.
    //
    if (  Owner(executor) != who
       && !Quota(executor))
    {
        safe_str(NOPERM_MESSAGE, buff, bufc);
        return;
    }

    bool bResult = true;
    if (!Free_Quota(who))
    {
        int aflags;
        dbref aowner;
        LBuf quota = LBuf_Adopt(atr_get("fun_hasquota.313", who, A_RQUOTA, &aowner, &aflags));
        // int64_t, not int (#1402).  Both sides of this comparison come from
        // mux_atoi64, but only the request kept its width -- narrowing the
        // stored quota made the two operands disagree about their own range.
        // Measured before this change, with RQUOTA=4294967296 the value
        // truncated to 0 and hasquota(player,1) answered 0, denying a player
        // holding four billion quota; RQUOTA=2147483648 truncated to INT_MIN
        // and denied every request outright.
        //
        int64_t rq = mux_atoi64(quota);
        bResult = (rq >= mux_atoi64(fargs[1]));
    }
    safe_bool(bResult, buff, bufc);
}
