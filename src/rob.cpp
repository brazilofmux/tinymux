// rob.cpp -- Commands dealing with giving/taking/killing things or money.
//
// $Id: rob.cpp,v 1.5 2001-02-07 05:28:14 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mudconf.h"
#include "db.h"
#include "interface.h"
#include "match.h"
#include "alloc.h"
#include "attrs.h"
#include "powers.h"

void do_kill(dbref player, dbref cause, int key, char *what, char *costchar)
{
    dbref victim;
    char *buf1, *buf2;
    int cost;

    init_match(player, what, TYPE_PLAYER);
    match_neighbor();
    match_me();
    match_here();
    if (Long_Fingers(player)) {
        match_player();
        match_absolute();
    }
    victim = match_result();

    switch (victim) {
    case NOTHING:
        notify(player, "I don't see that player here.");
        break;
    case AMBIGUOUS:
        notify(player, "I don't know who you mean!");
        break;
    default:
        if ((Typeof(victim) != TYPE_PLAYER) &&
            (Typeof(victim) != TYPE_THING)) {
            notify(player,
                "Sorry, you can only kill players and things.");
            break;
        }
        if ((Haven(Location(victim)) && !Wizard(player)) ||
            (controls(victim, Location(victim)) &&
             !controls(player, Location(victim))) ||
            Unkillable(victim)) {
            notify(player, "Sorry.");
            break;
        }
        /*
         * go for it 
         */

        cost = Tiny_atol(costchar);
        if (key == KILL_KILL) {
            if (cost < mudconf.killmin)
                cost = mudconf.killmin;
            if (cost > mudconf.killmax)
                cost = mudconf.killmax;

            /*
             * see if it works 
             */

            if (!payfor(player, cost))
            {
                notify(player, tprintf("You don't have enough %s.", mudconf.many_coins));
                return;
            }
        }
        else
        {
            cost = 0;
        }

        if (  mudconf.killguarantee
           && !(  (RandomINT32(0, mudconf.killguarantee-1) < cost)
               || (key == KILL_SLAY))
           || Wizard(victim))
        {

            /*
             * Failure: notify player and victim only 
             */

            notify(player, "Your murder attempt failed.");
            buf1 = alloc_lbuf("do_kill.failed");
            sprintf(buf1, "%s tried to kill you!", Name(player));
            notify_with_cause_ooc(victim, player, buf1);
            if (Suspect(player))
            {
                StringCopy(buf1, Name(player));
                if (player == Owner(player))
                {
                    raw_broadcast(WIZARD, "[Suspect] %s tried to kill %s(#%d).", buf1, Name(victim), victim);
                }
                else
                {
                    buf2 = alloc_lbuf("do_kill.SUSP.failed");
                    StringCopy(buf2, Name(Owner(player)));
                    raw_broadcast(WIZARD, "[Suspect] %s <via %s(#%d)> tried to kill %s(#%d).", buf2, buf1, player, Name(victim), victim);
                    free_lbuf(buf2);
                }
            }
            free_lbuf(buf1);
            break;
        }
        /*
         * Success!  You killed him 
         */

        buf1 = alloc_lbuf("do_kill.succ.1");
        buf2 = alloc_lbuf("do_kill.succ.2");
        if (Suspect(player))
        {
            StringCopy(buf1, Name(player));
            if (player == Owner(player))
            {
                raw_broadcast(WIZARD, "[Suspect] %s killed %s(#%d).", buf1, Name(victim), victim);
            }
            else
            {
                StringCopy(buf2, Name(Owner(player)));
                raw_broadcast(WIZARD, "[Suspect] %s <via %s(#%d)> killed %s(#%d).", buf2, buf1, player, Name(victim), victim);
            }
        }
        sprintf(buf1, "You killed %s!", Name(victim));
        sprintf(buf2, "killed %s!", Name(victim));
        if (Typeof(victim) != TYPE_PLAYER)
        {
            if (halt_que(NOTHING, victim) > 0)
                if (!Quiet(victim))
                    notify(Owner(victim), "Halted.");
        }
        did_it(player, victim, A_KILL, buf1, A_OKILL, buf2, A_AKILL, (char **)NULL, 0);

        /*
         * notify victim 
         */

        sprintf(buf1, "%s killed you!", Name(player));
        notify_with_cause_ooc(victim, player, buf1);

        /*
         * Pay off the bonus 
         */

        if (key == KILL_KILL) {
            cost /= 2;  /*
                     * victim gets half 
                     */
            if (Pennies(Owner(victim)) < mudconf.paylimit) {
                sprintf(buf1,
                    "Your insurance policy pays %d %s.",
                    cost, mudconf.many_coins);
                notify(victim, buf1);
                giveto(Owner(victim), cost);
            } else {
                notify(victim,
                 "Your insurance policy has been revoked.");
            }
        }
        free_lbuf(buf1);
        free_lbuf(buf2);

        /*
         * send him home 
         */

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
    dbref thing;
    char *str, *sp;

    init_match(giver, what, TYPE_THING);
    match_possession();
    match_me();
    thing = match_result();

    switch (thing) {
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
    if (((Typeof(thing) != TYPE_THING) && (Typeof(thing) != TYPE_PLAYER)) || !(Enter_ok(recipient) || controls(giver, recipient)))
    {
        notify(giver, NOPERM_MESSAGE);
        return;
    }
    if (!could_doit(giver, thing, A_LGIVE))
    {
        sp = str = alloc_lbuf("do_give.gfail");
        safe_str((char *)"You can't give ", str, &sp);
        safe_str(Name(thing), str, &sp);
        safe_str((char *)" away.", str, &sp);
        *sp = '\0';

        did_it(giver, thing, A_GFAIL, str, A_OGFAIL, NULL, A_AGFAIL, (char **)NULL, 0);
        free_lbuf(str);
        return;
    }
    if (!could_doit(thing, recipient, A_LRECEIVE))
    {
        sp = str = alloc_lbuf("do_give.rfail");
        safe_str(Name(recipient), str, &sp);
        safe_str((char *)" doesn't want ", str, &sp);
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
        StringCopy(str, Name(giver));
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
    dbref aowner;
    int cost, aflags;
    char *str;

    /*
     * do amount consistency check 
     */

    if (amount < 0 && !Steal(giver)) {
        notify(giver,
               tprintf("You look through your pockets. Nope, no negative %s.",
                   mudconf.many_coins));
        return;
    }
    if (!amount) {
        notify(giver,
               tprintf("You must specify a positive number of %s.",
                   mudconf.many_coins));
        return;
    }
    if (!Wizard(giver)) {
        if ((Typeof(recipient) == TYPE_PLAYER) &&
            (Pennies(recipient) + amount > mudconf.paylimit)) {
            notify(giver,
               tprintf("That player doesn't need that many %s!",
                   mudconf.many_coins));
            return;
        }
        if (!could_doit(giver, recipient, A_LUSE)) {
            notify(giver,
                   tprintf("%s won't take your money.",
                       Name(recipient)));
            return;
        }
    }
    /*
     * try to do the give 
     */

    if (!payfor(giver, amount)) {
        notify(giver,
               tprintf("You don't have that many %s to give!",
                   mudconf.many_coins));
        return;
    }
    /*
     * Find out cost if an object 
     */

    if (Typeof(recipient) == TYPE_THING) {
        str = atr_pget(recipient, A_COST, &aowner, &aflags);
        cost = Tiny_atol(str);
        free_lbuf(str);

        /*
         * Can't afford it? 
         */

        if (amount < cost) {
            notify(giver, "Feeling poor today?");
            giveto(giver, amount);
            return;
        }
        /*
         * Negative cost 
         */

        if (cost < 0) {
            return;
        }
    } else {
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
            notify_with_cause_ooc(recipient, giver, tprintf("%s gives you %d %s.", Name(giver), amount, mudconf.many_coins));
        }
    }

    /*
     * Report change given 
     */
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

    /*
     * Transfer the money and run PAY attributes 
     */
    giveto(recipient, cost);
    did_it(giver, recipient, A_PAY, NULL, A_OPAY, NULL, A_APAY, (char **)NULL, 0);
    return;
}

void do_give(dbref player, dbref cause, int key, char *who, char *amnt)
{
    // Check recipient.
    //
    init_match(player, who, TYPE_PLAYER);
    match_neighbor();
    match_possession();
    match_me();
    if (Long_Fingers(player))
    {
        match_player();
        match_absolute();
    }
    dbref recipient = match_result();
    switch (recipient)
    {
    case NOTHING:
        notify(player, "Give to whom?");
        return;
    case AMBIGUOUS:
        notify(player, "I don't know who you mean!");
        return;
    }

    if (isExit(recipient))
    {
        notify(player, "You can't give anything to an exit.");
        return;
    }
    if (Guest(recipient))
    {
        notify(player, "Guest really doesn't need money or anything.");
        return;
    }
    if (is_number(amnt))
    {
        give_money(player, recipient, key, Tiny_atol(amnt));
    }
    else
    {
        give_thing(player, recipient, key, amnt);
    }
}
