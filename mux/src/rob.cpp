/*! \file rob.cpp
 * \brief Commands dealing with giving/taking/killing things or money.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "mathutil.h"
#include "powers.h"

void do_kill
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *what,
    UTF8 *costchar,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *buf1, *buf2;

    init_match(executor, what, TYPE_PLAYER);
    match_neighbor();
    match_me();
    match_here();
    if (Long_Fingers(executor))
    {
        match_player();
        match_absolute();
    }
    dbref victim = match_result();

    switch (victim)
    {
    case NOTHING:
        notify(executor, T("I don\xE2\x80\x99t see that player here."));
        break;

    case AMBIGUOUS:
        notify(executor, T("I don\xE2\x80\x99t know who you mean!"));
        break;

    default:
        if (  !isPlayer(victim)
           && !isThing(victim))
        {
            notify(executor, T("Sorry, you can only kill players and things."));
            break;
        }
        if (  (  Haven(Location(victim))
              && !Wizard(executor))
           || (  Controls(victim, Location(victim))
              && !Controls(executor, Location(victim)))
           || Unkillable(victim))
        {
            notify(executor, T("Sorry."));
            break;
        }

        // Go for it.
        //
        int cost = 0;
        if (key == KILL_KILL)
        {
            cost = mux_atol(costchar);
            if (cost < mudconf.killmin)
            {
                cost = mudconf.killmin;
            }
            if (cost > mudconf.killmax)
            {
                cost = mudconf.killmax;
            }

            // See if it works.
            //
            if (!payfor(executor, cost))
            {
                notify(executor, tprintf(T("You don\xE2\x80\x99t have enough %s."), mudconf.many_coins));
                return;
            }
        }

        if (  RealWizard(victim)
           || (  0 < mudconf.killguarantee
              && !(  RandomINT32(0, mudconf.killguarantee-1) < cost
                  || key == KILL_SLAY)))
        {
            // Failure: notify player and victim only.
            //
            notify(executor, T("Your murder attempt failed."));
            buf1 = alloc_lbuf("do_kill.failed");
            mux_sprintf(buf1, LBUF_SIZE, T("%s tried to kill you!"), Moniker(executor));
            notify_with_cause_ooc(victim, executor, buf1, MSG_SRC_KILL);
            if (Suspect(executor))
            {
                mux_strncpy(buf1, Moniker(executor), LBUF_SIZE-1);
                if (executor == Owner(executor))
                {
                    raw_broadcast(WIZARD, T("[Suspect] %s tried to kill %s(#%d)."), buf1, Moniker(victim), victim);
                }
                else
                {
                    buf2 = alloc_lbuf("do_kill.SUSP.failed");
                    mux_strncpy(buf2, Moniker(Owner(executor)), LBUF_SIZE-1);
                    raw_broadcast(WIZARD, T("[Suspect] %s <via %s(#%d)> tried to kill %s(#%d)."),
                        buf2, buf1, executor, Moniker(victim), victim);
                    free_lbuf(buf2);
                }
            }
            free_lbuf(buf1);
            break;
        }

        // Success!  You killed him
        //
        buf1 = alloc_lbuf("do_kill.succ.1");
        buf2 = alloc_lbuf("do_kill.succ.2");
        if (Suspect(executor))
        {
            mux_strncpy(buf1, Moniker(executor), LBUF_SIZE-1);
            if (executor == Owner(executor))
            {
                raw_broadcast(WIZARD, T("[Suspect] %s killed %s(#%d)."), buf1, Moniker(victim), victim);
            }
            else
            {
                mux_strncpy(buf2, Moniker(Owner(executor)), LBUF_SIZE-1);
                raw_broadcast(WIZARD, T("[Suspect] %s <via %s(#%d)> killed %s(#%d)."),
                    buf2, buf1, executor, Moniker(victim), victim);
            }
        }
        mux_sprintf(buf1, LBUF_SIZE, T("You killed %s!"), Moniker(victim));
        mux_sprintf(buf2, LBUF_SIZE, T("killed %s!"), Moniker(victim));
        if (!isPlayer(victim))
        {
            if (halt_que(NOTHING, victim) > 0)
            {
                if (!Quiet(victim))
                {
                    notify(Owner(victim), T("Halted."));
                }
            }
        }
        did_it(executor, victim, A_KILL, buf1, A_OKILL, buf2, A_AKILL, 0,
            NULL, 0);

        // notify victim
        //
        mux_sprintf(buf1, LBUF_SIZE, T("%s killed you!"), Moniker(executor));
        notify_with_cause_ooc(victim, executor, buf1, MSG_SRC_KILL);

        // Pay off the bonus.
        //
        if (key == KILL_KILL)
        {
            cost /= 2;  // Victim gets half.
            if (Pennies(Owner(victim)) < mudconf.paylimit)
            {
                mux_sprintf(buf1, LBUF_SIZE, T("Your insurance policy pays %d %s."), cost,
                    mudconf.many_coins);
                notify(victim, buf1);
                giveto(Owner(victim), cost);
            }
            else
            {
                notify(victim, T("Your insurance policy has been revoked."));
            }
        }
        free_lbuf(buf1);
        free_lbuf(buf2);

        // Send him home.
        //
        move_via_generic(victim, HOME, NOTHING, 0);
        divest_object(victim);
        break;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * give_thing, give_money, do_give: Give away money or things.
 */

static void give_thing(dbref giver, dbref recipient, int key, UTF8 *what)
{
    init_match(giver, what, TYPE_THING);
    match_possession();
    match_me();
    dbref thing = match_result();

    switch (thing)
    {
    case NOTHING:
        notify(giver, T("You don\xE2\x80\x99t have that!"));
        return;

    case AMBIGUOUS:
        notify(giver, T("I don\xE2\x80\x99t know which you mean!"));
        return;
    }
    if (thing == giver)
    {
        notify(giver, T("You can\xE2\x80\x99t give yourself away!"));
        return;
    }
    if (thing == recipient)
    {
        notify(giver, T("You can\xE2\x80\x99t give an object to itself."));
        return;
    }
    if (  (!isThing(thing) && !isPlayer(thing))
       || !(  Enter_ok(recipient)
          ||  Controls(giver, recipient))
       || isGarbage(recipient))
    {
        notify(giver, NOPERM_MESSAGE);
        return;
    }
    UTF8 *str, *sp;
    if (!could_doit(giver, thing, A_LGIVE))
    {
        sp = str = alloc_lbuf("do_give.gfail");
        safe_str(T("You can\xE2\x80\x99t give "), str, &sp);
        safe_str(Moniker(thing), str, &sp);
        safe_str(T(" away."), str, &sp);
        *sp = '\0';

        did_it(giver, thing, A_GFAIL, str, A_OGFAIL, NULL, A_AGFAIL, 0,
            NULL, 0);
        free_lbuf(str);
        return;
    }
    if (!could_doit(thing, recipient, A_LRECEIVE))
    {
        sp = str = alloc_lbuf("do_give.rfail");
        safe_str(Moniker(recipient), str, &sp);
        safe_str(T(" doesn\xE2\x80\x99t want "), str, &sp);
        safe_str(Moniker(thing), str, &sp);
        safe_chr('.', str, &sp);
        *sp = '\0';

        did_it(giver, recipient, A_RFAIL, str, A_ORFAIL, NULL, A_ARFAIL, 0,
            NULL, 0);
        free_lbuf(str);
        return;
    }
    move_via_generic(thing, recipient, giver, 0);
    divest_object(thing);
    if (!(key & GIVE_QUIET))
    {
        str = alloc_lbuf("do_give.thing.ok");
        mux_strncpy(str, Moniker(giver), LBUF_SIZE-1);
        notify_with_cause_ooc(recipient, giver, tprintf(T("%s gave you %s."), str, Moniker(thing)), MSG_SRC_GIVE);
        notify(giver, T("Given."));
        notify_with_cause_ooc(thing, giver, tprintf(T("%s gave you to %s."), str, Moniker(recipient)), MSG_SRC_GIVE);
        free_lbuf(str);
    }
    did_it(giver, thing, A_DROP, NULL, A_ODROP, NULL, A_ADROP, 0, NULL, 0);
    did_it(recipient, thing, A_SUCC, NULL, A_OSUCC, NULL, A_ASUCC, 0,
        NULL, 0);
}

static void give_money(dbref giver, dbref recipient, int key, int amount)
{
    // Do amount consistency check.
    //
    if (  amount < 0
       && !Steal(giver))
    {
        notify(giver, tprintf(T("You look through your pockets. Nope, no negative %s."),
            mudconf.many_coins));
        return;
    }
    if (!amount)
    {
        notify(giver, tprintf(T("You must specify a positive number of %s."),
            mudconf.many_coins));
        return;
    }
    if (!Wizard(giver))
    {
        if (  isPlayer(recipient)
           && (Pennies(recipient) + amount > mudconf.paylimit))
        {
            notify(giver, tprintf(T("That player doesn\xE2\x80\x99t need that many %s!"),
                mudconf.many_coins));
            return;
        }
        if (!could_doit(giver, recipient, A_LUSE))
        {
            notify(giver, tprintf(T("%s won\xE2\x80\x99t take your money."), Moniker(recipient)));
            return;
        }
    }

    // Try to do the give.
    //
    if (!payfor(giver, amount))
    {
        notify(giver, tprintf(T("You don\xE2\x80\x99t have that many %s to give!"),
            mudconf.many_coins));
        return;
    }

    // Find out cost if an object.
    //
    int cost;
    if (isThing(recipient))
    {
        dbref aowner;
        int aflags;
        UTF8 *str = atr_pget(recipient, A_COST, &aowner, &aflags);
        cost = mux_atol(str);
        free_lbuf(str);

        // Can't afford it?
        //
        if (amount < cost)
        {
            notify(giver, T("Feeling poor today?"));
            giveto(giver, amount);
            return;
        }

        // Negative cost
        //
        if (cost < 0)
        {
            return;
        }
    }
    else
    {
        cost = amount;
    }

    if (!(key & GIVE_QUIET))
    {
        if (amount == 1)
        {
            notify(giver, tprintf(T("You give a %s to %s."), mudconf.one_coin, Moniker(recipient)));
            notify_with_cause_ooc(recipient, giver, tprintf(T("%s gives you a %s."), Moniker(giver), mudconf.one_coin), MSG_SRC_GIVE);
        }
        else
        {
            notify(giver, tprintf(T("You give %d %s to %s."), amount, mudconf.many_coins, Moniker(recipient)));
            notify_with_cause_ooc(recipient, giver, tprintf(T("%s gives you %d %s."),
                Moniker(giver), amount, mudconf.many_coins), MSG_SRC_GIVE);
        }
    }

    // Report change given
    //
    if ((amount - cost) == 1)
    {
        notify(giver, tprintf(T("You get 1 %s in change."), mudconf.one_coin));
        giveto(giver, 1);
    }
    else if (amount != cost)
    {
        notify(giver, tprintf(T("You get %d %s in change."), (amount - cost), mudconf.many_coins));
        giveto(giver, (amount - cost));
    }

    // Transfer the money and run PAY attributes
    //
    giveto(recipient, cost);
    did_it(giver, recipient, A_PAY, NULL, A_OPAY, NULL, A_APAY, 0, NULL, 0);
    return;
}

void do_give
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *who,
    UTF8 *amnt,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Check recipient.
    //
    init_match(executor, who, TYPE_PLAYER);
    match_neighbor();
    match_possession();
    match_me();
    if (Long_Fingers(executor))
    {
        match_player();
        match_absolute();
    }
    dbref recipient = match_result();
    switch (recipient)
    {
    case NOTHING:
        notify(executor, T("Give to whom?"));
        return;

    case AMBIGUOUS:
        notify(executor, T("I don\xE2\x80\x99t know who you mean!"));
        return;
    }

    if (isExit(recipient))
    {
        notify(executor, T("You can\xE2\x80\x99t give anything to an exit."));
        return;
    }
    if (Guest(recipient))
    {
        notify(executor, T("Guests really don\xE2\x80\x99t need money or anything."));
        return;
    }
    if (is_rational(amnt))
    {
        give_money(executor, recipient, key, mux_atol(amnt));
    }
    else
    {
        give_thing(executor, recipient, key, amnt);
    }
}
