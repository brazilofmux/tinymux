// quota.cpp -- Quota Management Commands.
//
// $Id: quota.cpp,v 1.5 2002-06-27 09:06:47 jake Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "functions.h"
#include "powers.h"

// ---------------------------------------------------------------------------
// count_quota, mung_quota, show_quota, do_quota: Manage quotas.
//
static int count_quota(dbref player)
{
    int i, q;

    q = 0 - mudconf.player_quota;
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

static void mung_quotas(dbref player, int key, int value)
{
    dbref aowner;
    int aq, rq, xq, aflags;
    char *buff;

    if (key & QUOTA_FIX)
    {
        // Get value of stuff owned and good value, set other value from that.
        //
        xq = count_quota(player);
        if (key & QUOTA_TOT)
        {
            buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
            aq = Tiny_atol(buff) + xq;
            atr_add_raw(player, A_QUOTA, Tiny_ltoa_t(aq));
            free_lbuf(buff);
        }
        else
        {
            buff = atr_get(player, A_QUOTA, &aowner, &aflags);
            rq = Tiny_atol(buff) - xq;
            atr_add_raw(player, A_RQUOTA, Tiny_ltoa_t(rq));
            free_lbuf(buff);
        }
    }
    else
    {
        // Obtain (or calculate) current relative and absolute quota.
        //
        buff = atr_get(player, A_QUOTA, &aowner, &aflags);
        if (!*buff)
        {
            free_lbuf(buff);
            buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
            rq = Tiny_atol(buff);
            free_lbuf(buff);
            aq = rq + count_quota(player);
        }
        else
        {
            aq = Tiny_atol(buff);
            free_lbuf(buff);
            buff = atr_get(player, A_RQUOTA, &aowner, &aflags);
            rq = Tiny_atol(buff);
            free_lbuf(buff);
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
        atr_add_raw(player, A_QUOTA, Tiny_ltoa_t(aq));
        atr_add_raw(player, A_RQUOTA, Tiny_ltoa_t(rq));
    }
}

static void show_quota(dbref player, dbref victim)
{
    dbref aowner;
    int aq, rq, aflags;
    char *buff;

    buff = atr_get(victim, A_QUOTA, &aowner, &aflags);
    aq = Tiny_atol(buff);
    free_lbuf(buff);
    buff = atr_get(victim, A_RQUOTA, &aowner, &aflags);
    rq = aq - Tiny_atol(buff);
    free_lbuf(buff);
    if (!Free_Quota(victim))
    {
        buff = tprintf("%-16s Quota: %9d  Used: %9d", Name(victim), aq, rq);
    }
    else
    {
        buff = tprintf("%-16s Quota: UNLIMITED  Used: %9d", Name(victim), rq);
    }
    notify_quiet(player, buff);
}

void do_quota
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *arg1,
    char *arg2
)
{
    if (!(mudconf.quotas || Quota(executor)))
    {
        notify_quiet(executor, "Quotas are not enabled.");
        return;
    }
    if ((key & QUOTA_TOT) && (key & QUOTA_REM))
    {
        notify_quiet(executor, "Illegal combination of switches.");
        return;
    }

    dbref who;
    int set = 0, value = 0, i;

    // Show or set all quotas if requested.
    //
    if (key & QUOTA_ALL)
    {
        if (arg1 && *arg1)
        {
            value = Tiny_atol(arg1);
            set = 1;
        }
        else if (key & (QUOTA_SET | QUOTA_FIX))
        {
            value = 0;
            set = 1;
        }
        if (set)
        {
            STARTLOG(LOG_WIZARD, "WIZ", "QUOTA");
            log_name(executor);
            log_text(" changed everyone's quota");
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
        who = lookup_player(executor, arg1, 1);
        if (!Good_obj(who))
        {
            notify_quiet(executor, "Not found.");
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
        set = 1;
        value = Tiny_atol(arg2);
    }
    else if (key & QUOTA_FIX)
    {
        set = 1;
        value = 0;
    }
    if (set)
    {
        STARTLOG(LOG_WIZARD, "WIZ", "QUOTA");
        log_name(executor);
        log_text(" changed the quota of ");
        log_name(who);
        ENDLOG;
        mung_quotas(who, key, value);
    }
    show_quota(executor, who);
}

FUNCTION(fun_hasquota)
{
    if (!mudconf.quotas)
    {
        safe_str("#-1 Quotas are not enabled.", buff, bufc);
        return;
    }

    // Find out whose quota to show.
    //
    dbref who = lookup_player(executor, fargs[0], 1);
    if (!Good_obj(who))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
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

    BOOL bResult = TRUE;
    if (!Free_Quota(who))
    {
        int aflags;
        dbref aowner;
        int rq = Tiny_atol(atr_get(who, A_RQUOTA, &aowner, &aflags));
        bResult = (rq >= Tiny_atol(fargs[1]));
    }
    safe_chr(bResult ? '1' : '0', buff, bufc);
}
