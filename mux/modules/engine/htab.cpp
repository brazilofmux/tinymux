/*! \file htab.cpp
 * \brief Table hashing routines.
 *
 * Name table lookup and configuration support.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

/*
 * ---------------------------------------------------------------------------
 * * search_nametab: Search a name table for a match and return the flag value.
 */

bool search_nametab(dbref player, NAMETAB *ntab, const UTF8 *flagname, int *pflag)
{
    for (NAMETAB *nt = ntab; nt->name; nt++)
    {
        if (minmatch(flagname, nt->name, nt->minlen))
        {
            if (check_access(player, nt->perm))
            {
                *pflag = nt->flag;
                return true;
            }
            else
            {
                *pflag = -2;
                return false;
            }
        }
    }
    *pflag = -1;
    return false;
}

/*
 * ---------------------------------------------------------------------------
 * * find_nametab_ent: Search a name table for a match and return a pointer to it.
 */

NAMETAB *find_nametab_ent(dbref player, NAMETAB *ntab, const UTF8 *flagname)
{
    for (NAMETAB *nt = ntab; nt->name; nt++)
    {
        if (minmatch(flagname, nt->name, nt->minlen))
        {
            if (check_access(player, nt->perm))
            {
                return nt;
            }
        }
    }
    return nullptr;
}

/*
 * ---------------------------------------------------------------------------
 * * display_nametab: Print out the names of the entries in a name table.
 */

void display_nametab(dbref player, NAMETAB *ntab, const UTF8 *prefix, bool list_if_none)
{
    bool got_one = false;
    UTF8 *buf = alloc_lbuf("display_nametab");
    UTF8 *bp = buf;

    safe_str(prefix, buf, &bp);
    safe_chr(':', buf, &bp);
    for (NAMETAB *nt = ntab; nt->name; nt++)
    {
        if (  God(player)
           || check_access(player, nt->perm))
        {
            safe_chr(' ', buf, &bp);
            safe_str(nt->name, buf, &bp);
            got_one = true;
        }
    }
    *bp = '\0';
    if (  got_one
       || list_if_none)
    {
        notify(player, buf);
    }
    free_lbuf(buf);
}

/* ---------------------------------------------------------------------------
 * interp_nametab: Print values for flags defined in name table.
 */

void interp_nametab(dbref player, NAMETAB *ntab, int flagword,
    const UTF8 *prefix, const UTF8 *true_text, const UTF8 *false_text)
{
    bool bFirst = true;
    UTF8 *buf = alloc_lbuf("interp_nametab");
    UTF8 *bp = buf;

    safe_str(prefix, buf, &bp);
    for (NAMETAB *nt = ntab; nt->name; nt++)
    {
        if (  God(player)
           || check_access(player, nt->perm))
        {
            if (!bFirst)
            {
                safe_chr(';', buf, &bp);
                bFirst = false;
            }
            safe_chr(' ', buf, &bp);
            safe_str(nt->name, buf, &bp);
            safe_str(T("..."), buf, &bp);
            if ((flagword & nt->flag) != 0)
            {
                safe_str(true_text, buf, &bp);
            }
            else
            {
                safe_str(false_text, buf, &bp);
            }
        }
    }
    *bp = '\0';
    notify(player, buf);
    free_lbuf(buf);
}

/* ---------------------------------------------------------------------------
 * listset_nametab: Print values for flags defined in name table.
 */

void listset_nametab(dbref player, NAMETAB *ntab, int flagword, const UTF8 *prefix, bool list_if_none)
{
    UTF8 *buf = alloc_lbuf("listset_nametab");
    UTF8 *bp = buf;

    safe_str(prefix, buf, &bp);
    safe_chr(':', buf, &bp);

    bool got_one = false;
    for (NAMETAB *nt = ntab; nt->name; nt++)
    {
        if (  ((flagword & nt->flag) != 0)
           && (  God(player)
              || check_access(player, nt->perm)))
        {
            safe_chr(' ', buf, &bp);
            safe_str(nt->name, buf, &bp);
            got_one = true;
        }
    }
    *bp = '\0';
    if (  got_one
       || list_if_none)
    {
        notify(player, buf);
    }
    free_lbuf(buf);
}

/* ---------------------------------------------------------------------------
 * cf_ntab_access: Change the access on a nametab entry.
 */

CF_HAND(cf_ntab_access)
{
    NAMETAB *np;
    UTF8 *ap;

    for (ap = str; *ap && !mux_isspace(*ap); ap++)
    {
        ; // Nothing.
    }
    if (*ap)
    {
        *ap++ = '\0';
    }

    while (mux_isspace(*ap))
    {
        ap++;
    }

    for (np = reinterpret_cast<NAMETAB *>(vp); np->name; np++)
    {
        if (minmatch(str, np->name, np->minlen))
        {
            return cf_modify_bits(&(np->perm), ap, pExtra, nExtra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, T("Entry"), str);
    return -1;
}
