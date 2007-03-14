/*! \file powers.cpp
 * \brief Power manipulation routines.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "command.h"
#include "powers.h"

/* ---------------------------------------------------------------------------
 * ph_any: set or clear indicated bit, no security checking
 */

static bool ph_any(dbref target, dbref player, POWER power, int fpowers, bool reset)
{
    UNUSED_PARAMETER(player);

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
    return true;
}

/* ---------------------------------------------------------------------------
 * ph_god: only GOD may set or clear the bit
 */

static bool ph_god(dbref target, dbref player, POWER power, int fpowers, bool reset)
{
    if (!God(player))
    {
        return false;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

/* ---------------------------------------------------------------------------
 * ph_wiz: only WIZARDS (or GOD) may set or clear the bit
 */

static bool ph_wiz(dbref target, dbref player, POWER power, int fpowers, bool reset)
{
    if (!Wizard(player))
    {
        return false;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

#if 0

/* ---------------------------------------------------------------------------
 * ph_wizroy: only WIZARDS, ROYALTY, (or GOD) may set or clear the bit
 */

bool ph_wizroy(dbref target, dbref player, POWER power, int fpowers, bool reset)
{
    if (!WizRoy(player))
    {
        return false;
    }
    return (ph_any(target, player, power, fpowers, reset));
}

/* ---------------------------------------------------------------------------
 * ph_inherit: only players may set or clear this bit.
 */

bool ph_inherit(dbref target, dbref player, POWER power, int fpowers, bool reset)
{
    if (!Inherits(player))
    {
        return false;
    }
    return (ph_any(target, player, power, fpowers, reset));
}
#endif

static POWERENT gen_powers[] =
{
    {(UTF8 *)"announce",        POW_ANNOUNCE,   0, 0,   ph_wiz},
    {(UTF8 *)"boot",            POW_BOOT,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"builder",         POW_BUILDER,    POWER_EXT,  0,  ph_any},
#else
    {(UTF8 *)"builder",         POW_BUILDER,    POWER_EXT,  0,  ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"chown_anything",  POW_CHOWN_ANY,  0, 0,   ph_wiz},
    {(UTF8 *)"comm_all",        POW_COMM_ALL,   0, 0,   ph_wiz},
    {(UTF8 *)"control_all",     POW_CONTROL_ALL,0, 0,   ph_god},
    {(UTF8 *)"expanded_who",    POW_WIZARD_WHO, 0, 0,   ph_wiz},
    {(UTF8 *)"find_unfindable", POW_FIND_UNFIND,0, 0,   ph_wiz},
    {(UTF8 *)"free_money",      POW_FREE_MONEY, 0, 0,   ph_wiz},
    {(UTF8 *)"free_quota",      POW_FREE_QUOTA, 0, 0,   ph_wiz},
    {(UTF8 *)"guest",           POW_GUEST,      0, 0,   ph_god},
    {(UTF8 *)"halt",            POW_HALT,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"hide",            POW_HIDE,       0, 0,   ph_any},
#else
    {(UTF8 *)"hide",            POW_HIDE,       0, 0,   ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"idle",            POW_IDLE,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"immutable",       POW_IMMUTABLE,  POWER_EXT, 0, ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"long_fingers",    POW_LONGFINGERS,0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"monitor",         POW_MONITOR,    0, 0,   ph_any},
#else
    {(UTF8 *)"monitor",         POW_MONITOR,    0, 0,   ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"no_destroy",      POW_NO_DESTROY, 0, 0,   ph_wiz},
    {(UTF8 *)"pass_locks",      POW_PASS_LOCKS, 0, 0,   ph_wiz},
    {(UTF8 *)"poll",            POW_POLL,       0, 0,   ph_wiz},
    {(UTF8 *)"prog",            POW_PROG,       0, 0,   ph_wiz},
    {(UTF8 *)"quota",           POW_CHG_QUOTAS, 0, 0,   ph_wiz},
    {(UTF8 *)"search",          POW_SEARCH,     0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"see_all",         POW_EXAM_ALL,   0, 0,   ph_any},
#else
    {(UTF8 *)"see_all",         POW_EXAM_ALL,   0, 0,   ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"see_hidden",      POW_SEE_HIDDEN, 0, 0,   ph_wiz},
    {(UTF8 *)"see_queue",       POW_SEE_QUEUE,  0, 0,   ph_wiz},
    {(UTF8 *)"siteadmin",       POW_SITEADMIN,  0, 0,   ph_wiz},
    {(UTF8 *)"stat_any",        POW_STAT_ANY,   0, 0,   ph_wiz},
    {(UTF8 *)"steal_money",     POW_STEAL,      0, 0,   ph_wiz},
    {(UTF8 *)"tel_anything",    POW_TEL_UNRST,  0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {(UTF8 *)"tel_anywhere",    POW_TEL_ANYWHR, 0, 0,   ph_any},
#else
    {(UTF8 *)"tel_anywhere",    POW_TEL_ANYWHR, 0, 0,   ph_wiz},
#endif // FIRANMUX
    {(UTF8 *)"unkillable",      POW_UNKILLABLE, 0, 0,   ph_wiz},
    {(UTF8 *)NULL,              0,              0, 0,   0}
};

/* ---------------------------------------------------------------------------
 * init_powertab: initialize power hash tables.
 */

void init_powertab(void)
{
    POWERENT *fp;
    UTF8 *nbuf = alloc_sbuf("init_powertab");

    for (fp = gen_powers; fp->powername; fp++)
    {
        mux_strncpy(nbuf, fp->powername, SBUF_SIZE-1);
        mux_strlwr(nbuf);

        if (!hashfindLEN(nbuf, strlen((char *)nbuf), &mudstate.powers_htab))
        {
            hashaddLEN(nbuf, strlen((char *)nbuf), fp, &mudstate.powers_htab);
        }
    }
    free_sbuf(nbuf);
}

/* ---------------------------------------------------------------------------
 * display_powers: display available powers.
 */

void display_powertab(dbref player)
{
    UTF8 *buf, *bp;
    POWERENT *fp;

    bp = buf = alloc_lbuf("display_powertab");
    safe_str((UTF8 *)"Powers:", buf, &bp);
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

static POWERENT *find_power(dbref thing, UTF8 *powername)
{
    UNUSED_PARAMETER(thing);

    // Convert powername to canonical lowercase.
    //
    UTF8 *buff = alloc_sbuf("find_power");
    mux_strncpy(buff, powername, SBUF_SIZE-1);
    mux_strlwr(buff);
    POWERENT *p = (POWERENT *)hashfindLEN(buff, strlen((char *)buff), &mudstate.powers_htab);
    free_sbuf(buff);
    return p;
}

bool decode_power(dbref player, UTF8 *powername, POWERSET *pset)
{
    pset->word1 = 0;
    pset->word2 = 0;

    POWERENT *pent = (POWERENT *)hashfindLEN(powername, strlen((char *)powername), &mudstate.powers_htab);
    if (!pent)
    {
        notify(player, tprintf("%s: Power not found.", powername));
        return false;
    }
    if (pent->powerpower & POWER_EXT)
    {
        pset->word2 = pent->powervalue;
    }
    else
    {
        pset->word1 = pent->powervalue;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * power_set: Set or clear a specified power on an object.
 */

void power_set(dbref target, dbref player, UTF8 *power, int key)
{
    bool bDone = false;

    do
    {
        // Trim spaces, and handle the negation character.
        //
        while (mux_isspace(*power))
        {
            power++;
        }

        bool bNegate = false;
        if (*power == '!')
        {
            bNegate = true;
            do
            {
                power++;
            } while (mux_isspace(*power));
        }

        // Beginning of power name is now 'power'.
        //
        UTF8 *npower = power;
        while (  *npower != '\0'
              && !mux_isspace(*npower))
        {
            npower++;
        }

        if (*npower == '\0')
        {
            bDone = true;
        }
        else
        {
            *npower = '\0';
        }

        // Make sure a power name was specified.
        //
        if (*power == '\0')
        {
            if (bNegate)
            {
                notify(player, (UTF8 *)"You must specify a power to clear.");
            }
            else
            {
                notify(player, (UTF8 *)"You must specify a power to set.");
            }
        }
        else
        {
            POWERENT *fp = find_power(target, power);
            if (fp == NULL)
            {
                notify(player, (UTF8 *)"I don't understand that power.");
            }
            else
            {
                // Invoke the power handler, and print feedback.
                //
                if (!fp->handler(target, player, fp->powervalue, fp->powerpower, bNegate))
                {
                    notify(player, NOPERM_MESSAGE);
                }
                else if (!(key & SET_QUIET) && !Quiet(player))
                {
                    notify(player, (bNegate ? (UTF8 *)"Cleared." : (UTF8 *)"Set."));
                }
            }
        }
        power = npower + 1;

    } while (!bDone);
}

/* ---------------------------------------------------------------------------
 * has_power: does object have power visible to player?
 */

bool has_power(dbref player, dbref it, UTF8 *powername)
{
    POWERENT *fp = find_power(it, powername);
    if (!fp)
    {
        return false;
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
            return false;
        }
        if ((fp->listperm & CA_GOD) && !God(player))
        {
            return false;
        }
        return true;
    }
    return false;
}

/* ---------------------------------------------------------------------------
 * powers_list: Return an LBUG containing the type and powers on thing.
 */

UTF8 *powers_list(dbref player, dbref target)
{
    // Allocate the return buffer.
    //
    UTF8 *buff = alloc_lbuf("powers_list");
    UTF8 *bp = buff;

    bool bFirst = true;
    POWERENT *fp;
    for (fp = gen_powers; fp->powername; fp++)
    {
        POWER fv;
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
            if (  (fp->listperm & CA_WIZARD)
               && !Wizard(player))
            {
                continue;
            }
            if (  (fp->listperm & CA_GOD)
               && !God(player))
            {
                continue;
            }
            if (bFirst)
            {
                bFirst = false;
            }
            else
            {
                safe_chr(' ', buff, &bp);
            }
            safe_str(fp->powername, buff, &bp);
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

void decompile_powers(dbref player, dbref thing, UTF8 *thingname)
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
        notify(player, tprintf("@power %s=%s", thingname, fp->powername));
    }
}
