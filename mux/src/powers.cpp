// powers.cpp -- Power manipulation routines.
//
// $Id: powers.cpp,v 1.6 2002-12-16 00:21:27 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "command.h"
#include "powers.h"

/* ---------------------------------------------------------------------------
 * ph_any: set or clear indicated bit, no security checking
 */

BOOL ph_any(dbref target, dbref player, POWER power, int fpowers, BOOL reset)
{
    if (fpowers & POWER_EXT)
    {
        if (reset)
        {
            s_Powers2(target, Powers2(target) & ~power);
        }
        else
        {
            s_Powers2(target, Powers2(target) | power);
        }
    }
    else
    {
        if (reset)
        {
            s_Powers(target, Powers(target) & ~power);
        }
        else
        {
            s_Powers(target, Powers(target) | power);
        }
    }
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * ph_god: only GOD may set or clear the bit
 */

BOOL ph_god(dbref target, dbref player, POWER power, int fpowers, BOOL reset)
{
    if (!God(player))
    {
        return FALSE;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

/* ---------------------------------------------------------------------------
 * ph_wiz: only WIZARDS (or GOD) may set or clear the bit
 */

BOOL ph_wiz(dbref target, dbref player, POWER power, int fpowers, BOOL reset)
{
    if (!Wizard(player))
    {
        return FALSE;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

/* ---------------------------------------------------------------------------
 * ph_wizroy: only WIZARDS, ROYALTY, (or GOD) may set or clear the bit
 */

BOOL ph_wizroy(dbref target, dbref player, POWER power, int fpowers, BOOL reset)
{
    if (!WizRoy(player))
    {
        return FALSE;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

/* ---------------------------------------------------------------------------
 * ph_inherit: only players may set or clear this bit.
 */

BOOL ph_inherit(dbref target, dbref player, POWER power, int fpowers, BOOL reset)
{
    if (!Inherits(player))
    {
        return FALSE;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

POWERENT gen_powers[] =
{
    {"quota",           POW_CHG_QUOTAS, 0, 0,   ph_wiz},
    {"chown_anything",  POW_CHOWN_ANY,  0, 0,   ph_wiz},
    {"announce",        POW_ANNOUNCE,   0, 0,   ph_wiz},
    {"boot",            POW_BOOT,       0, 0,   ph_wiz},
    {"halt",            POW_HALT,       0, 0,   ph_wiz},
    {"control_all",     POW_CONTROL_ALL,0, 0,   ph_god},
    {"expanded_who",    POW_WIZARD_WHO, 0, 0,   ph_wiz},
    {"see_all",         POW_EXAM_ALL,   0, 0,   ph_wiz},
    {"prog",            POW_PROG,       0, 0,   ph_wiz},
    {"find_unfindable", POW_FIND_UNFIND,0, 0,   ph_wiz},
    {"free_money",      POW_FREE_MONEY, 0, 0,   ph_wiz},
    {"free_quota",      POW_FREE_QUOTA, 0, 0,   ph_wiz},
    {"hide",            POW_HIDE,       0, 0,   ph_wiz},
    {"idle",            POW_IDLE,       0, 0,   ph_wiz},
    {"search",          POW_SEARCH,     0, 0,   ph_wiz},
    {"long_fingers",    POW_LONGFINGERS,0, 0,   ph_wiz},
    {"comm_all",        POW_COMM_ALL,   0, 0,   ph_wiz},
    {"see_queue",       POW_SEE_QUEUE,  0, 0,   ph_wiz},
    {"see_hidden",      POW_SEE_HIDDEN, 0, 0,   ph_wiz},
    {"monitor",         POW_MONITOR,    0, 0,   ph_wiz},
    {"poll",            POW_POLL,       0, 0,   ph_wiz},
    {"no_destroy",      POW_NO_DESTROY, 0, 0,   ph_wiz},
    {"guest",           POW_GUEST,      0, 0,   ph_god},
    {"stat_any",        POW_STAT_ANY,   0, 0,   ph_wiz},
    {"steal_money",     POW_STEAL,      0, 0,   ph_wiz},
    {"tel_anywhere",    POW_TEL_ANYWHR, 0, 0,   ph_wiz},
    {"tel_anything",    POW_TEL_UNRST,  0, 0,   ph_wiz},
    {"unkillable",      POW_UNKILLABLE, 0, 0,   ph_wiz},
    {"pass_locks",      POW_PASS_LOCKS, 0, 0,   ph_wiz},
    {"builder",         POW_BUILDER,    POWER_EXT,  0,  ph_wiz},
    {NULL,              0,              0, 0,   0}
};

/* ---------------------------------------------------------------------------
 * init_powertab: initialize power hash tables.
 */

void init_powertab(void)
{
    POWERENT *fp;
    char *nbuf = alloc_sbuf("init_powertab");

    for (fp = gen_powers; fp->powername; fp++)
    {
        strncpy(nbuf, fp->powername, SBUF_SIZE);
        nbuf[SBUF_SIZE-1] = '\0';
        mux_strlwr(nbuf);
        hashaddLEN(nbuf, strlen(nbuf), (int *)fp, &mudstate.powers_htab);
    }
    free_sbuf(nbuf);
}

/* ---------------------------------------------------------------------------
 * display_powers: display available powers.
 */

void display_powertab(dbref player)
{
    char *buf, *bp;
    POWERENT *fp;

    bp = buf = alloc_lbuf("display_powertab");
    safe_str("Powers:", buf, &bp);
    for (fp = gen_powers; fp->powername; fp++)
    {
        if ((fp->listperm & CA_WIZARD) && !Wizard(player))
        {
            continue;
        }
        if ((fp->listperm & CA_GOD) && !God(player))
        {
            continue;
        }
        safe_chr(' ', buf, &bp);
        safe_str(fp->powername, buf, &bp);
    }
    *bp = '\0';
    notify(player, buf);
    free_lbuf(buf);
}

POWERENT *find_power(dbref thing, char *powername)
{
    // Convert powername to canonical lowercase.
    //
    char *buff = alloc_sbuf("find_power");
    strncpy(buff, powername, SBUF_SIZE);
    buff[SBUF_SIZE-1] = '\0';
    mux_strlwr(buff);
    POWERENT *p = (POWERENT *)hashfindLEN(buff, strlen(buff), &mudstate.powers_htab);
    free_sbuf(buff);
    return p;
}

BOOL decode_power(dbref player, char *powername, POWERSET *pset)
{
    pset->word1 = 0;
    pset->word2 = 0;

    POWERENT *pent = (POWERENT *)hashfindLEN(powername, strlen(powername), &mudstate.powers_htab);
    if (!pent)
    {
        notify(player, tprintf("%s: Power not found.", powername));
        return FALSE;
    }
    if (pent->powerpower & POWER_EXT)
    {
        pset->word2 = pent->powervalue;
    }
    else
    {
        pset->word1 = pent->powervalue;
    }
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * power_set: Set or clear a specified power on an object.
 */

void power_set(dbref target, dbref player, char *power, int key)
{
    // Trim spaces, and handle the negation character.
    //
    while (Tiny_IsSpace[(unsigned char)*power])
    {
        power++;
    }

    BOOL negate = FALSE;
    if (*power == '!')
    {
        negate = TRUE;
        power++;
    }
    while (Tiny_IsSpace[(unsigned char)*power])
    {
        power++;
    }

    // Make sure a power name was specified.
    //
    if (*power == '\0')
    {
        if (negate)
        {
            notify(player, "You must specify a power to clear.");
        }
        else
        {
            notify(player, "You must specify a power to set.");
        }
        return;
    }
    POWERENT *fp = find_power(target, power);
    if (fp == NULL)
    {
        notify(player, "I don't understand that power.");
        return;
    }

    // Invoke the power handler, and print feedback.
    //
    BOOL result = fp->handler(target, player, fp->powervalue,
                 fp->powerpower, negate);
    if (!result)
    {
        notify(player, NOPERM_MESSAGE);
    }
    else if (!(key & SET_QUIET) && !Quiet(player))
    {
        notify(player, (negate ? "Cleared." : "Set."));
    }
}

/* ---------------------------------------------------------------------------
 * has_power: does object have power visible to player?
 */

BOOL has_power(dbref player, dbref it, char *powername)
{
    POWERENT *fp = find_power(it, powername);
    if (!fp)
    {
        return FALSE;
    }

    POWER fv;
    if (fp->powerpower & POWER_EXT)
    {
        fv = Powers2(it);
    }
    else
    {
        fv = Powers(it);
    }

    if (fv & fp->powervalue)
    {
        if ((fp->listperm & CA_WIZARD) && !Wizard(player))
        {
            return FALSE;
        }
        if ((fp->listperm & CA_GOD) && !God(player))
        {
            return FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------------
 * power_description: Return an mbuf containing the type and powers on thing.
 */

char *power_description(dbref player, dbref target)
{
    char *buff, *bp;
    POWERENT *fp;
    POWER fv;

    // Allocate the return buffer.
    //
    int otype = Typeof(target);
    bp = buff = alloc_mbuf("power_description");

    // Store the header strings and object type.
    //
    safe_mb_str("Powers:", buff, &bp);

    for (fp = gen_powers; fp->powername; fp++)
    {
        if (fp->powerpower & POWER_EXT)
        {
            fv = Powers2(target);
        }
        else
        {
            fv = Powers(target);
        }

        if (fv & fp->powervalue)
        {
            if ((fp->listperm & CA_WIZARD) && !Wizard(player))
            {
                continue;
            }
            if ((fp->listperm & CA_GOD) && !God(player))
            {
                continue;
            }
            safe_mb_chr(' ', buff, &bp);
            safe_mb_str(fp->powername, buff, &bp);
        }
    }

    // Terminate the string, and return the buffer to the caller.
    //
    *bp = '\0';
    return buff;
}


/* ---------------------------------------------------------------------------
 * decompile_powers: Produce commands to set powers on target.
 */

void decompile_powers(dbref player, dbref thing, char *thingname)
{
    POWERENT *fp;

    // Report generic powers.
    //
    POWER f1 = Powers(thing);
    POWER f2 = Powers2(thing);

    for (fp = gen_powers; fp->powername; fp++)
    {
        // Skip if we shouldn't decompile this power
        //
        if (fp->listperm & CA_NO_DECOMP)
        {
            continue;
        }

        // Skip if this power is not set.
        //
        if (fp->powerpower & POWER_EXT)
        {
            if (!(f2 & fp->powervalue))
            {
                continue;
            }
        }
        else
        {
            if (!(f1 & fp->powervalue))
            {
                continue;
            }
        }

        // Skip if we can't see this power.
        //
        if (!check_access(player, fp->listperm))
        {
            continue;
        }

        // We made it this far, report this power.
        //
        notify(player, tprintf("@power %s=%s", strip_ansi(thingname), fp->powername));
    }
}
