/*! \file powers.cpp
 * \brief Power manipulation routines.
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
    {T("announce"),        POW_ANNOUNCE,   0, 0,   ph_wiz},
    {T("boot"),            POW_BOOT,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("builder"),         POW_BUILDER,    POWER_EXT,  0,  ph_any},
#else
    {T("builder"),         POW_BUILDER,    POWER_EXT,  0,  ph_wiz},
#endif // FIRANMUX
    {T("chown_anything"),  POW_CHOWN_ANY,  0, 0,   ph_wiz},
    {T("comm_all"),        POW_COMM_ALL,   0, 0,   ph_wiz},
    {T("control_all"),     POW_CONTROL_ALL,0, 0,   ph_god},
    {T("expanded_who"),    POW_WIZARD_WHO, 0, 0,   ph_wiz},
    {T("find_unfindable"), POW_FIND_UNFIND,0, 0,   ph_wiz},
    {T("free_money"),      POW_FREE_MONEY, 0, 0,   ph_wiz},
    {T("free_quota"),      POW_FREE_QUOTA, 0, 0,   ph_wiz},
    {T("guest"),           POW_GUEST,      0, 0,   ph_god},
    {T("halt"),            POW_HALT,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("hide"),            POW_HIDE,       0, 0,   ph_any},
#else
    {T("hide"),            POW_HIDE,       0, 0,   ph_wiz},
#endif // FIRANMUX
    {T("idle"),            POW_IDLE,       0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("immutable"),       POW_IMMUTABLE,  POWER_EXT, 0, ph_wiz},
#endif // FIRANMUX
    {T("long_fingers"),    POW_LONGFINGERS,0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("monitor"),         POW_MONITOR,    0, 0,   ph_any},
#else
    {T("monitor"),         POW_MONITOR,    0, 0,   ph_wiz},
#endif // FIRANMUX
    {T("no_destroy"),      POW_NO_DESTROY, 0, 0,   ph_wiz},
    {T("pass_locks"),      POW_PASS_LOCKS, 0, 0,   ph_wiz},
    {T("poll"),            POW_POLL,       0, 0,   ph_wiz},
    {T("prog"),            POW_PROG,       0, 0,   ph_wiz},
    {T("quota"),           POW_CHG_QUOTAS, 0, 0,   ph_wiz},
    {T("search"),          POW_SEARCH,     0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("see_all"),         POW_EXAM_ALL,   0, 0,   ph_any},
#else
    {T("see_all"),         POW_EXAM_ALL,   0, 0,   ph_wiz},
#endif // FIRANMUX
    {T("see_hidden"),      POW_SEE_HIDDEN, 0, 0,   ph_wiz},
    {T("see_queue"),       POW_SEE_QUEUE,  0, 0,   ph_wiz},
    {T("siteadmin"),       POW_SITEADMIN,  0, 0,   ph_wiz},
    {T("stat_any"),        POW_STAT_ANY,   0, 0,   ph_wiz},
    {T("steal_money"),     POW_STEAL,      0, 0,   ph_wiz},
    {T("tel_anything"),    POW_TEL_UNRST,  0, 0,   ph_wiz},
#if defined(FIRANMUX)
    {T("tel_anywhere"),    POW_TEL_ANYWHR, 0, 0,   ph_any},
#else
    {T("tel_anywhere"),    POW_TEL_ANYWHR, 0, 0,   ph_wiz},
#endif // FIRANMUX
    {T("unkillable"),      POW_UNKILLABLE, 0, 0,   ph_wiz},
    {(UTF8 *)nullptr,      0,              0, 0,   0}
};

/* ---------------------------------------------------------------------------
 * init_powertab: initialize power hash tables.
 */

void init_powertab(void)
{
    POWERENT *fp;
    for (fp = gen_powers; fp->powername; fp++)
    {
        size_t nCased;
        UTF8 *pCased = mux_strupr(fp->powername, nCased);

        if (!hashfindLEN(pCased, nCased, &mudstate.powers_htab))
        {
            hashaddLEN(pCased, nCased, fp, &mudstate.powers_htab);
        }
    }
}

/* ---------------------------------------------------------------------------
 * display_powers: display available powers.
 */

void display_powertab(dbref player)
{
    UTF8 *buf, *bp;
    POWERENT *fp;

    bp = buf = alloc_lbuf("display_powertab");
    safe_str(T("Powers:"), buf, &bp);
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
    size_t nCased;
    UTF8 *pCased = mux_strupr(powername, nCased);
    POWERENT *p = (POWERENT *)hashfindLEN(pCased, nCased, &mudstate.powers_htab);
    return p;
}

bool decode_power(dbref player, UTF8 *powername, POWERSET *pset)
{
    pset->word1 = 0;
    pset->word2 = 0;

    POWERENT *pent = (POWERENT *)hashfindLEN(powername, strlen((char *)powername), &mudstate.powers_htab);
    if (!pent)
    {
        notify(player, tprintf(T("%s: Power not found."), powername));
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
                notify(player, T("You must specify a power to clear."));
            }
            else
            {
                notify(player, T("You must specify a power to set."));
            }
        }
        else
        {
            POWERENT *fp = find_power(target, power);
            if (fp == nullptr)
            {
                notify(player, T("I don\xE2\x80\x99t understand that power."));
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
                    notify(player, (bNegate ? T("Cleared.") : T("Set.")));
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
        notify(player, tprintf(T("@power %s=%s"), thingname, fp->powername));
    }
}
