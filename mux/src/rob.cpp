// rob.cpp -- Commands dealing with giving/taking/killing things or money.
//
// $Id: rob.cpp,v 1.1 2003/01/22 19:58:26 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "powers.h"

void do_kill
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *what,
    char *costchar
)
{
    char *buf1, *buf2;

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
        notify(executor, "I don't see that player here.");
        break;

    case AMBIGUOUS:
        notify(executor, "I don't know who you mean!");
        break;

    default:
        if (  !isPlayer(victim)
           && !isThing(victim))
        {
            notify(executor, "Sorry, you can only kill players and things.");
            break;
        }
        if (  (  Haven(Location(victim))
              && !Wizard(executor)) 
           || (  Controls(victim, Location(victim)) 
              && !Controls(executor, Location(victim))) 
           || Unkillable(victim))
        {
            notify(executor, "Sorry.");
            break;
        }

        // Go for it.
        //
        int cost = 0;
        if (key == KILL_KILL)
        {
            cost = Tiny_atol(costchar);
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
                notify(executor, tprintf("You don't have enough %s.", mudconf.many_coins));
                return;
            }
        }

        if (  Wizard(victim)
           || (  0 < mudconf.killguarantee
              && !(  RandomINT32(0, mudconf.killguarantee-1) < cost
                  || key == KILL_SLAY)))
        {
            // Failure: notify player and victim only.
            //
            notify(executor, "Your murder attempt failed.");
            buf1 = alloc_lbuf("do_kill.failed");
            sprintf(buf1, "%s tried to kill you!", Name(executor));
            notify_with_cause_ooc(victim, executor, buf1);
            if (Suspect(executor))
            {
                strcpy(buf1, Name(executor));
                if (executor == Owner(executor))
                {
                    raw_broadcast(WIZARD, "[Suspect] %s tried to kill %s(#%d).", buf1, Name(victim), victim);
                }
                else
                {
                    buf2 = alloc_lbuf("do_kill.SUSP.failed");
                    strcpy(buf2, Name(Owner(executor)));
                    raw_broadcast(WIZARD, "[Suspect] %s <via %s(#%d)> tried to kill %s(#%d).",
                        buf2, buf1, executor, Name(victim), victim);
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
            strcpy(buf1, Name(executor));
            if (executor == Owner(executor))
            {
                raw_broadcast(WIZARD, "[Suspect] %s killed %s(#%d).", buf1, Name(victim), victim);
            }
            else
            {
                strcpy(buf2, Name(Owner(executor)));
                raw_broadcast(WIZARD, "[Suspect] %s <via %s(#%d)> killed %s(#%d).",
                    buf2, buf1, executor, Name(victim), victim);
            }
        }
        sprintf(buf1, "You killed %s!", Name(victim));
        sprintf(buf2, "killed %s!", Name(victim));
        if (!isPlayer(victim))
        {
            if (halt_que(NOTHING, victim) > 0)
            {
                if (!Quiet(victim))
                {
                    notify(Owner(victim), "Halted.");
                }
            }
        }
        did_it(executor, victim, A_KILL, buf1, A_OKILL, buf2, A_AKILL, (char **)NULL, 0);

        // notify victim
        //
        sprintf(buf1, "%s killed you!", Name(executor));
        notify_with_cause_ooc(victim, executor, buf1);

        // Pay off the bonus.
        //
        if (key == KILL_KILL)
        {
            cost /= 2;  // Victim gets half.
            if (Pennies(Owner(victim)) < mudconf.paylimit)
            {
                sprintf(buf1, "Your insurance policy pays %d %s.", cost,
                    mudconf.many_coins);
                notify(victim, buf1);
                giveto(Owner(victim), cost);
            }
            else
            {
                notify(victim, "Your insurance policy has been revoked.");
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

static void give_thing(dbref giver, dbref recipient, int key, char *what)
{
    init_match(giver, what, TYPE_THING);
    match_possession();
    match_me();
    dbref thing = match_result();

    switch (thing)
    {
    case NOTHING:
        notify(giver, "You don't have that!");
        return;

    case AMBIGUOUS:
        notify(giver, "I don't know which you mean!");
        return;
    }
    if (thing == giver)
    {
        notify(giver, "You can't give yourself away!");
        return;
    }
    if (thing == recipient)
    {
        notify(giver, "You can't give an object to itself.");
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
    char *str, *sp;
    if (!could_doit(giver, thing, A_LGIVE))
    {
        sp = str = alloc_lbuf("do_give.gfail");
        safe_str("You can't give ", str, &sp);
        safe_str(Name(thing), str, &sp);
        safe_str(" away.", str, &sp);
        *sp = '\0';

        did_it(giver, thing, A_GFAIL, str, A_OGFAIL, NULL, A_AGFAIL, (char **)NULL, 0);
        free_lbuf(str);
        return;
    }
    if (!could_doit(thing, recipient, A_LRECEIVE))
    {
        sp = str = alloc_lbuf("do_give.rfail");
        safe_str(Name(recipient), str, &sp);
        safe_str(" doesn't want ", str, &sp);
        safe_str(Name(thing), str, &sp);
        safe_chr('.', str, &sp);
        *sp = '\0';

        did_it(giver, recipient, A_RFAIL, str, A_ORFAIL, NULL, A_ARFAIL, (char **)NULL, 0);
        free_lbuf(str);
        return;
    }
    move_via_generic(thing, recipient, giver, 0);
    divest_object(thing);
    if (!(key & GIVE_QUIET))
    {
        str = alloc_lbuf("do_give.thing.ok");
        strcpy(str, Name(giver));
        notify_with_cause_ooc(recipient, giver, tprintf("%s gave you %s.", str, Name(thing)));
        notify(giver, "Given.");
        notify_with_cause_ooc(thing, giver, tprintf("%s gave you to %s.", str, Name(recipient)));
        free_lbuf(str);
    }
    did_it(giver, thing, A_DROP, NULL, A_ODROP, NULL, A_ADROP, (char **)NULL, 0);
    did_it(recipient, thing, A_SUCC, NULL, A_OSUCC, NULL, A_ASUCC, (char **)NULL, 0);
}

static void give_money(dbref giver, dbref recipient, int key, int amount)
{
    // Do amount consistency check.
    //
    if (  amount < 0
       && !Steal(giver))
    {
        notify(giver, tprintf("You look through your pockets. Nope, no negative %s.",
            mudconf.many_coins));
        return;
    }
    if (!amount)
    {
        notify(giver, tprintf("You must specify a positive number of %s.",
            mudconf.many_coins));
        return;
    }
    if (!Wizard(giver))
    {
        if (  isPlayer(recipient)
           && (Pennies(recipient) + amount > mudconf.paylimit))
        {
            notify(giver, tprintf("That player doesn't need that many %s!",
                mudconf.many_coins));
            return;
        }
        if (!could_doit(giver, recipient, A_LUSE))
        {
            notify(giver, tprintf("%s won't take your money.", Name(recipient)));
            return;
        }
    }

    // Try to do the give.
    //
    if (!payfor(giver, amount))
    {
        notify(giver, tprintf("You don't have that many %s to give!", 
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
        char *str = atr_pget(recipient, A_COST, &aowner, &aflags);
        cost = Tiny_atol(str);
        free_lbuf(str);

        // Can't afford it?
        //
        if (amount < cost)
        {
            notify(giver, "Feeling poor today?");
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
            notify(giver, tprintf("You give a %s to %s.", mudconf.one_coin, Name(recipient)));
            notify_with_cause_ooc(recipient, giver, tprintf("%s gives you a %s.", Name(giver), mudconf.one_coin));
        }
        else
        {
            notify(giver, tprintf("You give %d %s to %s.", amount, mudconf.many_coins, Name(recipient)));
            notify_with_cause_ooc(recipient, giver, tprintf("%s gives you %d %s.",
                Name(giver), amount, mudconf.many_coins));
        }
    }

    // Report change given
    //
    if ((amount - cost) == 1)
    {
        notify(giver, tprintf("You get 1 %s in change.", mudconf.one_coin));
        giveto(giver, 1);
    }
    else if (amount != cost)
    {
        notify(giver, tprintf("You get %d %s in change.", (amount - cost), mudconf.many_coins));
        giveto(giver, (amount - cost));
    }

    // Transfer the money and run PAY attributes
    //
    giveto(recipient, cost);
    did_it(giver, recipient, A_PAY, NULL, A_OPAY, NULL, A_APAY, (char **)NULL, 0);
    return;
}

void do_give
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *who,
    char *amnt
)
{
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
        notify(executor, "Give to whom?");
        return;

    case AMBIGUOUS:
        notify(executor, "I don't know who you mean!");
        return;
    }

    if (isExit(recipient))
    {
        notify(executor, "You can't give anything to an exit.");
        return;
    }
    if (Guest(recipient))
    {
        notify(executor, "Guests really don't need money or anything.");
        return;
    }
    if (is_rational(amnt))
    {
        give_money(executor, recipient, key, Tiny_atol(amnt));
    }
    else
    {
        give_thing(executor, recipient, key, amnt);
    }
}
