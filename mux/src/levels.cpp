#ifdef REALITY_LVLS
/*
 * levels.cpp - Reality levels stuff
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#include "externs.h"
#include "db.h"
#include "attrs.h"
#include "mudconf.h"
#include "command.h"
#include "powers.h"
#include "alloc.h"
#include "match.h"
#include "levels.h"
#include "stringutil.h"

extern void cf_log_notfound(dbref, char *, const char *, char *);

RLEVEL RxLevel(dbref thing)
{
    const char *buff = atr_get_raw(thing, A_RLEVEL);
    if (  NULL != buff
       || strlen(buff) != 17)
    {
        switch(Typeof(thing))
        {
        case TYPE_ROOM:
            return(mudconf.def_room_rx);

        case TYPE_PLAYER:
            return(mudconf.def_player_rx);

        case TYPE_EXIT:
            return(mudconf.def_exit_rx);

        default:
            return(mudconf.def_thing_rx);
        }
    }

    int i;
    RLEVEL rx;
    for (rx=0, i=0; buff[i] && mux_ishex(buff[i]); i++)
    {
        rx = 16 * rx + mux_hex2dec(buff[i]);
    }
    return rx;
}

RLEVEL TxLevel(dbref thing)
{
    char *buff;
    int i;
    RLEVEL tx;

    buff = (char *)atr_get_raw(thing, A_RLEVEL);
    if (!buff || strlen(buff) != 17)
    switch(Typeof(thing))
    {
        case TYPE_ROOM:
            return(mudconf.def_room_tx);
        case TYPE_PLAYER:
            return(mudconf.def_player_tx);
        case TYPE_EXIT:
            return(mudconf.def_exit_tx);
        default:
            return(mudconf.def_thing_tx);
    }
    for(tx=0, i=0; buff[i] && !mux_isspace[buff[i]]; ++i);
    if(buff[i])
        for(++i; buff[i] && mux_ishex(buff[i]); ++i)
            tx = 16 * tx + mux_hex2dec(buff[i]);
    return(tx);
}

void notify_except_rlevel(dbref loc, dbref player, dbref exception, const char *msg, int xflags)
{
    dbref first;

    if (loc != exception && IsReal(loc, player))
        notify_check(loc, player, msg,
            (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A| xflags));
    DOLIST(first, Contents(loc)) {
        if (first != exception && IsReal(first, player)) {
            notify_check(first, player, msg,
                (MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE | xflags));
        }
    }
}

void notify_except2_rlevel(dbref loc, dbref player, dbref exc1, dbref exc2, const char *msg)
{
    dbref first;

    if ((loc != exc1) && (loc != exc2) && IsReal(loc, player))
        notify_check(loc, player, msg,
            (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A));
    DOLIST(first, Contents(loc)) {
        if (first != exc1 && first != exc2 && IsReal(first, player)) {
            notify_check(first, player, msg,
                (MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE));
        }
    }
}

void notify_except2_rlevel2(dbref loc, dbref player, dbref exc1, dbref exc2, const char *msg)
{
    dbref first;

    if ((loc != exc1) && (loc != exc2) && IsReal(loc, player))
        notify_check(loc, player, msg,
            (MSG_ME_ALL | MSG_F_UP | MSG_S_INSIDE | MSG_NBR_EXITS_A));
    DOLIST(first, Contents(loc)) {
        if (first != exc1 && first != exc2
            && IsReal(first, player) && IsReal(first, exc2)) {
            notify_check(first, player, msg,
                (MSG_ME | MSG_F_DOWN | MSG_S_OUTSIDE));
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * rxlevel_description: Return an mbuf containing the RxLevels of the thing.
 */

char *rxlevel_description(dbref player, dbref target)
{
    RLEVEL rl;
    char *buff, *bp;
    int otype;
    int i;

    /*
     * Allocate the return buffer
     */

    otype = Typeof(target);
    bp = buff = alloc_mbuf("rxlevel_description");

    /*
     * Store the header strings and object type
     */

    safe_mb_str((char *)"RxLevel:", buff, &bp);
    
    rl = RxLevel(target);
    for (i = 0; i < mudconf.no_levels; ++i)
        if((rl & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
        {
            safe_mb_chr(' ', buff, &bp);
            safe_mb_str(mudconf.reality_level[i].name, buff, &bp);
        }

    /*
     * Terminate the string, and return the buffer to the caller
     */

    *bp = '\0';
    return buff;
}

/*
 * ---------------------------------------------------------------------------
 * * txlevel_description: Return an mbuf containing the TxLevels of the thing.
 */

char *txlevel_description(dbref player, dbref target)
{
    RLEVEL tl;
    char *buff, *bp;
    int otype;
    int i;

    /*
     * Allocate the return buffer
     */

    otype = Typeof(target);
    bp = buff = alloc_mbuf("txlevel_description");

    /*
     * Store the header strings and object type
     */

    safe_mb_str((char *)"TxLevel:", buff, &bp);
    
    tl = TxLevel(target);
    for (i = 0; i < mudconf.no_levels; ++i)
        if((tl & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
        {
            safe_mb_chr(' ', buff, &bp);
            safe_mb_str(mudconf.reality_level[i].name, buff, &bp);
        }

    /*
     * Terminate the string, and return the buffer to the caller
     */

    *bp = '\0';
    return buff;
}

RLEVEL find_rlevel(char *name)
{
    int i;
    
    for(i=0; i < mudconf.no_levels; ++i)
        if(!strcasecmp(name, mudconf.reality_level[i].name))
            return mudconf.reality_level[i].value;
    return 0;
}

void do_rxlevel(dbref player, dbref cause, dbref enactor, int nargs, int key, char *object, char *arg)
{
    dbref thing;
    int negate, i;
    RLEVEL result, ormask, andmask;
    char lname[9], *buff;

    if (!arg || !*arg) {
        notify_quiet(player, "I don't know what you want to set!");
        return;
    }
    
    /* find thing */
    if ((thing = match_controlled(player, object)) == NOTHING)
        return;

    ormask = 0;
    andmask = ~ormask;    
    while(*arg)
    {
        negate = 0;
        while(*arg && mux_isspace[*arg])
            arg++;
        if(*arg == '!')
        {
            negate = 1;
            ++arg;
        }
        for(i=0; *arg && !mux_isspace[*arg]; ++arg)
            if(i < 8)
                lname[i++] = *arg;
        lname[i] = '\0';
        if(!lname[0])
        {
            if(negate)
                notify(player, "You must specify a reality level to clear.");
            else
                notify(player, "You must specify a reality level to set.");
            return;
        }
        result = find_rlevel(lname);
        if(!result)
        {
            notify(player, "No such reality level.");
            continue;
        }
        if(negate)
        {
            andmask &= ~result;
            notify(player, "Cleared.");
        }
        else
        {
            ormask |= result;
            notify(player, "Set.");
        }
    }
    
    /* Set the Rx Level */
    buff = alloc_lbuf("do_rxlevel");
    sprintf(buff, "%08X %08X", RxLevel(thing) & andmask | ormask, TxLevel(thing));
    atr_add_raw(thing, A_RLEVEL, buff);
    free_lbuf(buff);
}

void do_txlevel(dbref player, dbref cause, dbref enactor, int nargs, int key, char *object, char *arg)
{
    dbref thing;
    int negate, i;
    RLEVEL result, ormask, andmask;
    char lname[9], *buff;

    if (!arg || !*arg) {
        notify_quiet(player, "I don't know what you want to set!");
        return;
    }
    
    /* find thing */
    if ((thing = match_controlled(player, object)) == NOTHING)
        return;

    ormask = 0;
    andmask = ~ormask;    
    while(*arg)
    {
        negate = 0;
        while(*arg && mux_isspace[*arg])
            arg++;
        if(*arg == '!')
        {
            negate = 1;
            ++arg;
        }
        for(i=0; *arg && !mux_isspace[*arg]; ++arg)
            if(i < 8)
                lname[i++] = *arg;
        lname[i] = '\0';
        if(!lname[0])
        {
            if(negate)
                notify(player, "You must specify a reality level to clear.");
            else
                notify(player, "You must specify a reality level to set.");
            return;
        }
        result = find_rlevel(lname);
        if(!result)
        {
            notify(player, "No such reality level.");
            continue;
        }
        if(negate)
        {
            andmask &= ~result;
            notify(player, "Cleared.");
        }
        else
        {
            ormask |= result;
            notify(player, "Set.");
        }
    }

    /* Set the Tx Level */
    buff = alloc_lbuf("do_rxlevel");
    sprintf(buff, "%08X %08X", RxLevel(thing), TxLevel(thing) & andmask | ormask);
    atr_add_raw(thing, A_RLEVEL, buff);
    free_lbuf(buff);
}

/*
 * ---------------------------------------------------------------------------
 * * decompile_rlevels: Produce commands to set reality levels on target.
 */

void decompile_rlevels(dbref player,dbref thing,char *thingname)
{
    char *buf;
    buf = rxlevel_description(player, thing);
    notify(player, tprintf("@rxlevel %s=%s", strip_ansi(thingname), buf));
    free_mbuf(buf);
    buf = txlevel_description(player, thing);
    notify(player, tprintf("@txlevel %s=%s", strip_ansi(thingname), buf));
    free_mbuf(buf);
}

int *desclist_match(dbref player, dbref thing)
{
    RLEVEL match;
    int i, j;
    ATTR *at;
    static int descbuffer[33];

    descbuffer[0] = 1;
    match = RxLevel(player) & TxLevel(thing);
    for(i = 0; i < mudconf.no_levels; ++i)
        if((match & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
        {
            at = atr_str(mudconf.reality_level[i].attr);
            if(at)
            {
                for(j = 1; j < descbuffer[0]; ++j)
                    if(at->number == descbuffer[j])
                        break;
                if(j == descbuffer[0])
                    descbuffer[descbuffer[0]++] = at->number;
            }
        }
    return descbuffer;
}

/* ---------------------------------------------------------------------------
 * did_it_rlevel: Have player do something to/with thing, watching the attributes
 * 'what' is actually ignored, the desclist match being used instead.
 */
void did_it_rlevel(dbref player, dbref thing, int what, const char *def, int owhat, const char *odef, int awhat, char *args[], int nargs)
{
    char *d, *buff, *act, *charges, *bp, *str, *preserve[MAX_GLOBAL_REGS];
    dbref loc, aowner;
    int num, aflags,  need_pres, preserve_len[MAX_GLOBAL_REGS];
    int i, *desclist, found_a_desc;
        CLinearTimeAbsolute lta;

    need_pres = 0;

    /* message to player */

    if (what > 0) {
      /* Get description list */
      desclist = desclist_match(player, thing);
      found_a_desc = 0;
      for(i = 1; i < desclist[0]; ++i)
      {
                /* Ok, if it's A_DESC, we need to check against A_IDESC */
                if ( (what == A_IDESC) && (desclist[i] == A_DESC) )
                   d = atr_pget(thing, A_IDESC, &aowner, &aflags);
                else
                   d = atr_pget(thing, desclist[i], &aowner, &aflags);
        if (*d) {
            found_a_desc = 1;    /* No need for the 'def' message */
            if(!need_pres)
            {
                need_pres = 1;
                save_global_regs("did_it_save", preserve,
                     preserve_len);
            }
            buff = bp = alloc_lbuf("did_it.1");
            str = d;
            mux_exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if ((desclist[i] == A_HTDESC) && Html(player)) {
                safe_str("\r\n", buff, &bp);
                *bp = '\0';
                notify_html(player, buff);
            } else
                notify(player, buff);
            free_lbuf(buff);
        }
        free_lbuf(d);
      }
      if(!found_a_desc)
      {
          /* No desc found... try the default desc (again) */
          /* A_DESC or A_HTDESC... the worst case we look for it twice */
        d = atr_pget(thing, what, &aowner, &aflags);
        if (*d) {
            found_a_desc = 1;    /* No need for the 'def' message */
            if(!need_pres)
            {
                need_pres = 1;
                save_global_regs("did_it_save", preserve,
                     preserve_len);
            }
            buff = bp = alloc_lbuf("did_it.1");
            str = d;
            mux_exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if ((what == A_HTDESC) && Html(player)) {
                safe_str("\r\n", buff, &bp);
                *bp = '\0';
                notify_html(player, buff);
            } else
                notify(player, buff);
            free_lbuf(buff);
        } else if(def) {
            notify(player, def);
        }
        free_lbuf(d);
      } /* !found_a_desc */
    } else if ((what < 0) && def) {
        notify(player, def);
    }

        if (isPlayer(thing))
        {
           d = atr_pget(mudconf.master_room, get_atr("ASSET_DESC"), &aowner, &aflags);
           if (*d)
           {
              if(!need_pres)
              {
                 need_pres = 1;
                 save_global_regs("did_it_save", preserve, preserve_len);
              }
              buff = bp = alloc_lbuf("did_it.1");
              str = d;
              mux_exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE |EV_TOP, &str, args, nargs);
              *bp = '\0';
              notify(player, buff);
              free_lbuf(buff);
           }
           free_lbuf(d);
        }


    /* message to neighbors */

    if ((owhat > 0) && Has_location(player) &&
        Good_obj(loc = Location(player))) {
        d = atr_pget(thing, owhat, &aowner, &aflags);
        if (*d) {
            if (!need_pres) {
                need_pres = 1;
                save_global_regs("did_it_save", preserve,
                         preserve_len);
            }
            buff = bp = alloc_lbuf("did_it.2");
            str = d;
            mux_exec(buff, &bp, 0, thing, player, EV_EVAL | EV_FIGNORE | EV_TOP, &str, args, nargs);
            *bp = '\0';
            if (*buff)
                notify_except2_rlevel2(loc, player, player, thing,
                       tprintf("%s %s", Name(player), buff));
            free_lbuf(buff);
        } else if (odef) {
            notify_except2_rlevel2(loc, player, player, thing,
                       tprintf("%s %s", Name(player), odef));
        }
        free_lbuf(d);
    } else if ((owhat < 0) && odef && Has_location(player) &&
           Good_obj(loc = Location(player))) {
        notify_except2_rlevel2(loc, player, player, thing,
                   tprintf("%s %s", Name(player), odef));
    }

    /* If we preserved the state of the global registers, restore them. */

    if (need_pres)
        restore_global_regs("did_it_restore", preserve, preserve_len);
        
    /* do the action attribute */

    if (awhat > 0 && IsReal(thing, player)) {

                if (*(act = atr_pget(thing, awhat, &aowner, &aflags))) {
            charges = atr_pget(thing, A_CHARGES, &aowner, &aflags);
            if (*charges) {
                num = mux_atol(charges);
                if (num > 0) {
                    buff = alloc_sbuf("did_it.charges");
                    mux_ltoa(num-1, buff);
                    atr_add_raw(thing, A_CHARGES, buff);
                    free_sbuf(buff);
                } else if (*(buff = atr_pget(thing, A_RUNOUT, &aowner, &aflags))) {
                    free_lbuf(act);
                    act = buff;
                } else {
                    free_lbuf(act);
                    free_lbuf(buff);
                    free_lbuf(charges);
                    return;
                }
            }
            free_lbuf(charges);
            wait_que(thing, player, player, 0, lta, NOTHING, 0, act, args, nargs,
                 mudstate.global_regs);
        }
        free_lbuf(act);
    }
}
#endif /* REALITY_LVLS */
