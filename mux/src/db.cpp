// db.cpp
//
// $Id: db.cpp,v 1.36 2002-08-03 20:00:18 sdennis Exp $
//
// MUX 2.1
// Portions are derived from MUX 1.6. Portions are original work.
//
// Copyright (C) 1998 through 2002 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#ifdef STANDALONE
#undef MEMORY_BASED
#endif // STANDALONE
#include "externs.h"

#include "attrs.h"
#include "comsys.h"
#include "interface.h"
#include "powers.h"
#include "vattr.h"

#ifndef O_ACCMODE
#define O_ACCMODE   (O_RDONLY|O_WRONLY|O_RDWR)
#endif // O_ACCMODE

OBJ *db = NULL;
NAME *names = NULL;
NAME *purenames = NULL;

extern void desc_addhash(DESC *);

typedef struct atrcount ATRCOUNT;
struct atrcount
{
    dbref thing;
    int count;
};

// Check routine forward declaration.
//
extern void pcache_reload(dbref);
extern void desc_reload(dbref);

// list of attributes
//
ATTR attr[] =
{
    {"Aahear",      A_AAHEAR,   AF_ODARK | AF_NOPROG},
    {"Aclone",      A_ACLONE,   AF_ODARK | AF_NOPROG},
    {"Aconnect",    A_ACONNECT, AF_ODARK | AF_NOPROG},
    {"Adesc",       A_ADESC,    AF_ODARK | AF_NOPROG},
    {"Adfail",      A_ADFAIL,   AF_ODARK | AF_NOPROG},
    {"Adisconnect", A_ADISCONNECT, AF_ODARK | AF_NOPROG},
    {"Adrop",       A_ADROP,    AF_ODARK | AF_NOPROG},
    {"Aefail",      A_AEFAIL,   AF_ODARK | AF_NOPROG},
    {"Aenter",      A_AENTER,   AF_ODARK | AF_NOPROG},
    {"Afail",       A_AFAIL,    AF_ODARK | AF_NOPROG},
    {"Agfail",      A_AGFAIL,   AF_ODARK | AF_NOPROG},
    {"Ahear",       A_AHEAR,    AF_ODARK | AF_NOPROG},
    {"Akill",       A_AKILL,    AF_ODARK | AF_NOPROG},
    {"Aleave",      A_ALEAVE,   AF_ODARK | AF_NOPROG},
    {"Alfail",      A_ALFAIL,   AF_ODARK | AF_NOPROG},
    {"Alias",       A_ALIAS,    AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_PRIVATE | AF_CONST},
    {"Allowance",   A_ALLOWANCE, AF_MDARK | AF_NOPROG | AF_WIZARD},
    {"Amail",       A_AMAIL,    AF_ODARK | AF_NOPROG},
    {"Amhear",      A_AMHEAR,   AF_ODARK | AF_NOPROG},
    {"Amove",       A_AMOVE,    AF_ODARK | AF_NOPROG},
    {"Apay",        A_APAY,     AF_ODARK | AF_NOPROG},
    {"Arfail",      A_ARFAIL,   AF_ODARK | AF_NOPROG},
    {"Asucc",       A_ASUCC,    AF_ODARK | AF_NOPROG},
    {"Atfail",      A_ATFAIL,   AF_ODARK | AF_NOPROG},
    {"Atport",      A_ATPORT,   AF_ODARK | AF_NOPROG},
    {"Atofail",     A_ATOFAIL,  AF_ODARK | AF_NOPROG},
    {"Aufail",      A_AUFAIL,   AF_ODARK | AF_NOPROG},
    {"Ause",        A_AUSE,     AF_ODARK | AF_NOPROG},
    {"Away",        A_AWAY,     AF_ODARK | AF_NOPROG},
    {"Charges",     A_CHARGES,  AF_ODARK | AF_NOPROG},
    {"CmdCheck",    A_CMDCHECK, AF_DARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_PRIVATE | AF_CONST},
    {"Comment",     A_COMMENT,  AF_MDARK | AF_WIZARD},
    {"ConFormat",   A_CONFORMAT, AF_ODARK | AF_NOPROG},
    {"Cost",        A_COST,     AF_ODARK | AF_NOPROG},
    {"Created",     A_CREATED,  AF_GOD | AF_VISUAL | AF_NOPROG | AF_NOCMD},
    {"Daily",       A_DAILY,    AF_ODARK | AF_NOPROG},
    {"Desc",        A_DESC,     AF_NOPROG},
    {"DefaultLock", A_LOCK,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Destroyer",   A_DESTROYER, AF_MDARK | AF_WIZARD | AF_NOPROG},
    {"Dfail",       A_DFAIL,    AF_ODARK | AF_NOPROG},
    {"Drop",        A_DROP,     AF_ODARK | AF_NOPROG},
    {"DropLock",    A_LDROP,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Ealias",      A_EALIAS,   AF_ODARK | AF_NOPROG},
    {"Efail",       A_EFAIL,    AF_ODARK | AF_NOPROG},
    {"Enter",       A_ENTER,    AF_ODARK | AF_NOPROG},
    {"EnterLock",   A_LENTER,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"ExitFormat",  A_EXITFORMAT, AF_ODARK | AF_NOPROG},
    {"Fail",        A_FAIL,     AF_ODARK | AF_NOPROG},
    {"Filter",      A_FILTER,   AF_ODARK | AF_NOPROG},
    {"Forwardlist", A_FORWARDLIST, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_CONST},
    {"GetFromLock", A_LGET,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Gfail",       A_GFAIL,    AF_ODARK | AF_NOPROG},
    {"GiveLock",    A_LGIVE,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Idesc",       A_IDESC,    AF_ODARK | AF_NOPROG},
    {"Idle",        A_IDLE,     AF_ODARK | AF_NOPROG},
    {"IdleTimeout", A_IDLETMOUT, AF_ODARK | AF_NOPROG},
    {"Infilter",    A_INFILTER, AF_ODARK | AF_NOPROG},
    {"Inprefix",    A_INPREFIX, AF_ODARK | AF_NOPROG},
    {"Kill",        A_KILL,     AF_ODARK | AF_NOPROG},
    {"Lalias",      A_LALIAS,   AF_ODARK | AF_NOPROG},
    {"Last",        A_LAST,     AF_WIZARD | AF_NOCMD | AF_NOPROG | AF_NOCLONE},
    {"Lastpage",    A_LASTPAGE, AF_INTERNAL | AF_NOCMD | AF_NOPROG | AF_GOD | AF_PRIVATE},
    {"Lastsite",    A_LASTSITE, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_GOD},
    {"LastIP",      A_LASTIP,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_GOD},
    {"Leave",       A_LEAVE,    AF_ODARK | AF_NOPROG},
    {"LeaveLock",   A_LLEAVE,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Lfail",       A_LFAIL,    AF_ODARK | AF_NOPROG},
    {"LinkLock",    A_LLINK,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Listen",      A_LISTEN,   AF_ODARK | AF_NOPROG},
    {"Logindata",   A_LOGINDATA, AF_MDARK | AF_NOPROG | AF_NOCMD | AF_CONST},
    {"Mailcurf",    A_MAILCURF, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE},
    {"Mailflags",   A_MAILFLAGS, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE},
    {"Mailfolders", A_MAILFOLDERS, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE},
    {"Mailmsg",     A_MAILMSG,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Mailsub",     A_MAILSUB,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Mailsucc",    A_MAIL,     AF_ODARK | AF_NOPROG},
    {"Mailto",      A_MAILTO,   AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Modified",    A_MODIFIED, AF_GOD | AF_VISUAL | AF_NOPROG | AF_NOCMD},
    {"Move",        A_MOVE,     AF_ODARK | AF_NOPROG},
    {"Name",        A_NAME,     AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"NameFormat",  A_NAMEFORMAT, AF_ODARK | AF_NOPROG | AF_WIZARD},
    {"Odesc",       A_ODESC,    AF_ODARK | AF_NOPROG},
    {"Odfail",      A_ODFAIL,   AF_ODARK | AF_NOPROG},
    {"Odrop",       A_ODROP,    AF_ODARK | AF_NOPROG},
    {"Oefail",      A_OEFAIL,   AF_ODARK | AF_NOPROG},
    {"Oenter",      A_OENTER,   AF_ODARK | AF_NOPROG},
    {"Ofail",       A_OFAIL,    AF_ODARK | AF_NOPROG},
    {"Ogfail",      A_OGFAIL,   AF_ODARK | AF_NOPROG},
    {"Okill",       A_OKILL,    AF_ODARK | AF_NOPROG},
    {"Oleave",      A_OLEAVE,   AF_ODARK | AF_NOPROG},
    {"Olfail",      A_OLFAIL,   AF_ODARK | AF_NOPROG},
    {"Omove",       A_OMOVE,    AF_ODARK | AF_NOPROG},
    {"Opay",        A_OPAY,     AF_ODARK | AF_NOPROG},
    {"OpenLock",    A_LOPEN,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Orfail",      A_ORFAIL,   AF_ODARK | AF_NOPROG},
    {"Osucc",       A_OSUCC,    AF_ODARK | AF_NOPROG},
    {"Otfail",      A_OTFAIL,   AF_ODARK | AF_NOPROG},
    {"Otport",      A_OTPORT,   AF_ODARK | AF_NOPROG},
    {"Otofail",     A_OTOFAIL,  AF_ODARK | AF_NOPROG},
    {"Oufail",      A_OUFAIL,   AF_ODARK | AF_NOPROG},
    {"Ouse",        A_OUSE,     AF_ODARK | AF_NOPROG},
    {"Oxenter",     A_OXENTER,  AF_ODARK | AF_NOPROG},
    {"Oxleave",     A_OXLEAVE,  AF_ODARK | AF_NOPROG},
    {"Oxtport",     A_OXTPORT,  AF_ODARK | AF_NOPROG},
    {"PageLock",    A_LPAGE,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"ParentLock",  A_LPARENT,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Pay",         A_PAY,      AF_ODARK | AF_NOPROG},
    {"Prefix",      A_PREFIX,   AF_ODARK | AF_NOPROG},
    {"ProgCmd",     A_PROGCMD,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"QueueMax",    A_QUEUEMAX, AF_MDARK | AF_WIZARD | AF_NOPROG},
    {"Quota",       A_QUOTA,    AF_MDARK | AF_NOPROG | AF_GOD | AF_NOCMD | AF_NOCLONE},
    {"ReceiveLock", A_LRECEIVE, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Reject",      A_REJECT,   AF_ODARK | AF_NOPROG},
    {"Rfail",       A_RFAIL,    AF_ODARK | AF_NOPROG},
    {"Rquota",      A_RQUOTA,   AF_MDARK | AF_NOPROG | AF_GOD | AF_NOCMD | AF_NOCLONE},
    {"Runout",      A_RUNOUT,   AF_ODARK | AF_NOPROG},
    {"Semaphore",   A_SEMAPHORE, AF_ODARK | AF_NOPROG | AF_WIZARD | AF_NOCMD | AF_NOCLONE},
    {"Sex",         A_SEX,      AF_NOPROG},
    {"Signature",   A_SIGNATURE, AF_ODARK | AF_NOPROG},
    {"SpeechLock",  A_LSPEECH,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Startup",     A_STARTUP,  AF_ODARK | AF_NOPROG},
    {"Succ",        A_SUCC,     AF_ODARK | AF_NOPROG},
    {"TeloutLock",  A_LTELOUT,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Tfail",       A_TFAIL,    AF_ODARK | AF_NOPROG},
    {"Timeout",     A_TIMEOUT,  AF_MDARK | AF_NOPROG | AF_WIZARD},
    {"Tport",       A_TPORT,    AF_ODARK | AF_NOPROG},
    {"TportLock",   A_LTPORT,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Tofail",      A_TOFAIL,   AF_ODARK | AF_NOPROG},
    {"Ufail",       A_UFAIL,    AF_ODARK | AF_NOPROG},
    {"Use",         A_USE,      AF_ODARK | AF_NOPROG},
    {"UseLock",     A_LUSE,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"UserLock",    A_LUSER,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"VA",          A_VA,       AF_ODARK},
    {"VB",          A_VA + 1,   AF_ODARK},
    {"VC",          A_VA + 2,   AF_ODARK},
    {"VD",          A_VA + 3,   AF_ODARK},
    {"VE",          A_VA + 4,   AF_ODARK},
    {"VF",          A_VA + 5,   AF_ODARK},
    {"VG",          A_VA + 6,   AF_ODARK},
    {"VH",          A_VA + 7,   AF_ODARK},
    {"VI",          A_VA + 8,   AF_ODARK},
    {"VJ",          A_VA + 9,   AF_ODARK},
    {"VK",          A_VA + 10,  AF_ODARK},
    {"VL",          A_VA + 11,  AF_ODARK},
    {"VM",          A_VA + 12,  AF_ODARK},
    {"VN",          A_VA + 13,  AF_ODARK},
    {"VO",          A_VA + 14,  AF_ODARK},
    {"VP",          A_VA + 15,  AF_ODARK},
    {"VQ",          A_VA + 16,  AF_ODARK},
    {"VR",          A_VA + 17,  AF_ODARK},
    {"VS",          A_VA + 18,  AF_ODARK},
    {"VT",          A_VA + 19,  AF_ODARK},
    {"VU",          A_VA + 20,  AF_ODARK},
    {"VV",          A_VA + 21,  AF_ODARK},
    {"VW",          A_VA + 22,  AF_ODARK},
    {"VX",          A_VA + 23,  AF_ODARK},
    {"VY",          A_VA + 24,  AF_ODARK},
    {"VZ",          A_VA + 25,  AF_ODARK},
    {"VRML_URL",    A_VRML_URL, AF_ODARK | AF_NOPROG},
    {"HTDesc",      A_HTDESC,   AF_NOPROG},
    // Added by D.Piper (del@doofer.org) 2000-APR
    //
    {"Reason",      A_REASON,   AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_GOD},
#ifdef GAME_DOOFERMUX
    {"RegInfo",     A_REGINFO,  AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_WIZARD},
#endif // GAME_DOOFERMUX
    {"ConnInfo",    A_CONNINFO, AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_GOD},
    {"*Password",   A_PASS,     AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"*Privileges", A_PRIVS,    AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"*Money",      A_MONEY,    AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {NULL,          0,          0}
};

char *aszSpecialDBRefNames[1-NOPERM] =
{
    "", "*NOTHING*", "*AMBIGUOUS*", "*HOME*", "*NOPERMISSION*"
};

#ifndef STANDALONE
/* ---------------------------------------------------------------------------
 * fwdlist_set, fwdlist_clr: Manage cached forwarding lists
 */

void fwdlist_set(dbref thing, FWDLIST *ifp)
{
    FWDLIST *fp, *xfp;
    int i;

    // If fwdlist is null, clear.
    //
    if (!ifp || (ifp->count <= 0))
    {
        fwdlist_clr(thing);
        return;
    }

    // Copy input forwardlist to a correctly-sized buffer.
    //
    fp = (FWDLIST *)MEMALLOC(sizeof(int) * ((ifp->count) + 1));
    (void)ISOUTOFMEMORY(fp);

    for (i = 0; i < ifp->count; i++)
    {
        fp->data[i] = ifp->data[i];
    }
    fp->count = ifp->count;

    // Replace an existing forwardlist, or add a new one.
    //
    xfp = fwdlist_get(thing);
    if (xfp)
    {
        MEMFREE(xfp);
        xfp = NULL;
        hashreplLEN(&thing, sizeof(thing), (int *)fp, &mudstate.fwdlist_htab);
    }
    else
    {
        hashaddLEN(&thing, sizeof(thing), (int *)fp, &mudstate.fwdlist_htab);
    }
}

void fwdlist_clr(dbref thing)
{
    // If a forwardlist exists, delete it
    //
    FWDLIST *xfp = fwdlist_get(thing);
    if (xfp)
    {
        MEMFREE(xfp);
        xfp = NULL;
        hashdeleteLEN(&thing, sizeof(thing), &mudstate.fwdlist_htab);
    }
}

#endif // !STANDALONE

/* ---------------------------------------------------------------------------
 * fwdlist_load: Load text into a forwardlist.
 */

int fwdlist_load(FWDLIST *fp, dbref player, char *atext)
{
    dbref target;
    char *tp, *bp, *dp;
    BOOL fail;

    int count = 0;
    int errors = 0;
    bp = tp = alloc_lbuf("fwdlist_load.str");
    strcpy(tp, atext);
    do
    {
        // Skip spaces.
        //
        for (; Tiny_IsSpace[(unsigned char)*bp]; bp++) ;

        // Remember string.
        //
        for (dp = bp; *bp && !Tiny_IsSpace[(unsigned char)*bp]; bp++) ;

        // Terminate string.
        //
        if (*bp)
        {
            *bp++ = '\0';
        }

        if ((*dp++ == '#') && Tiny_IsDigit[(unsigned char)*dp])
        {
            target = Tiny_atol(dp);
#ifdef STANDALONE
            fail = !Good_obj(target);
#else // STANDALONE
            fail = (  !Good_obj(target)
                   || (  !God(player)
                      && !Controls(player, target)
                      && (  !Link_ok(target)
                         || !could_doit(player, target, A_LLINK))));
#endif // STANDALONE
            if (fail)
            {
#ifndef STANDALONE
                notify(player,
                       tprintf("Cannot forward to #%d: Permission denied.",
                           target));
#endif // !STANDALONE
                errors++;
            }
            else
            {
                if (count < 1000)
                {
                    fp->data[count++] = target;
                }
            }
        }
    } while (*bp);
    free_lbuf(tp);
    fp->count = count;
    return errors;
}

/* ---------------------------------------------------------------------------
 * fwdlist_rewrite: Generate a text string from a FWDLIST buffer.
 */

int fwdlist_rewrite(FWDLIST *fp, char *atext)
{
    int count = 0;
    atext[0] = '\0';

    if (fp && fp->count)
    {
        char *bp = atext;
        ITL pContext;
        ItemToList_Init(&pContext, atext, &bp, '#');
        for (int i = 0; i < fp->count; i++)
        {
            if (  Good_obj(fp->data[i])
               && ItemToList_AddInteger(&pContext, fp->data[i]))
            {
                count++;
            }
        }
        ItemToList_Final(&pContext);
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * fwdlist_ck:  Check a list of dbref numbers to forward to for AUDIBLE
 */
BOOL fwdlist_ck(dbref player, dbref thing, int anum, char *atext)
{
#ifdef STANDALONE

    return TRUE;

#else // STANDALONE

    FWDLIST *fp;
    int count = 0;

    if (atext && *atext)
    {
        fp = (FWDLIST *) alloc_lbuf("fwdlist_ck.fp");
        fwdlist_load(fp, player, atext);
    }
    else
    {
        fp = NULL;
    }

    // Set the cached forwardlist.
    //
    fwdlist_set(thing, fp);
    count = fwdlist_rewrite(fp, atext);
    if (fp)
    {
        free_lbuf(fp);
    }
    return ((count > 0) || !atext || !*atext);

#endif // STANDALONE
}

FWDLIST *fwdlist_get(dbref thing)
{
#ifdef STANDALONE
    static FWDLIST *fp = NULL;
    if (!fp)
    {
        fp = (FWDLIST *) alloc_lbuf("fwdlist_get");
    }
    dbref aowner;
    int   aflags;
    char *tp = atr_get(thing, A_FORWARDLIST, &aowner, &aflags);
    fwdlist_load(fp, GOD, tp);
    free_lbuf(tp);
#else // STANDALONE
    FWDLIST *fp = ((FWDLIST *) hashfindLEN(&thing, sizeof(thing), &mudstate.fwdlist_htab));
#endif // STANDALONE
    return fp;
}

/* ---------------------------------------------------------------------------
 * Name, s_Name: Get or set an object's name.
 */

const char *Name(dbref thing)
{
    if (thing < 0)
    {
        return aszSpecialDBRefNames[-thing];
    }

    dbref aowner;
    int aflags;
#ifdef MEMORY_BASED
    static char tbuff[LBUF_SIZE];
    atr_get_str(tbuff, thing, A_NAME, &aowner, &aflags);
    return tbuff;
#else // MEMORY_BASED
    if (!names[thing])
    {
        int len;
        char *pName = atr_get_LEN( thing, A_NAME, &aowner, &aflags, &len);
        names[thing] = StringCloneLen(pName, len);
        free_lbuf(pName);
    }
    return names[thing];
#endif // MEMORY_BASED
}

const char *PureName(dbref thing)
{
    if (thing < 0)
    {
        return aszSpecialDBRefNames[-thing];
    }

    dbref aowner;
    int aflags;

    char *pName, *pPureName;
    if (mudconf.cache_names)
    {
        if (!purenames[thing])
        {
            int nName;
            size_t nPureName;
#ifdef MEMORY_BASED
            pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
            pPureName = strip_ansi(pName, &nPureName);
            free_lbuf(pName);
            purenames[thing] = StringCloneLen(pPureName, nPureName);
#else // MEMORY_BASED
            if (!names[thing])
            {
                pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
                names[thing] = StringCloneLen(pName, nName);
                free_lbuf(pName);
            }
            pName = names[thing];
            pPureName = strip_ansi(pName, &nPureName);
            if (nPureName == nName)
            {
                purenames[thing] = pName;
            }
            else
            {
                purenames[thing] = StringCloneLen(pPureName, nPureName);
            }
#endif // MEMORY_BASED
        }
        return purenames[thing];
    }
    pName = atr_get(thing, A_NAME, &aowner, &aflags);
    pPureName = strip_ansi(pName);
    free_lbuf(pName);
    return pPureName;
}

void s_Name(dbref thing, const char *s)
{
    atr_add_raw(thing, A_NAME, s);
    set_modified(thing);
#ifndef MEMORY_BASED
    if (names[thing])
    {
        if (  mudconf.cache_names
           && names[thing] == purenames[thing])
        {
            purenames[thing] = NULL;
        }
        MEMFREE(names[thing]);
    }
    names[thing] = StringClone(s);
#endif // !MEMORY_BASED
    if (  mudconf.cache_names
       && purenames[thing])
    {
        MEMFREE(purenames[thing]);
        purenames[thing] = NULL;
    }
}

void s_Pass(dbref thing, const char *s)
{
    atr_add_raw(thing, A_PASS, (char *)s);
}

#ifndef STANDALONE

/* ---------------------------------------------------------------------------
 * do_attrib: Manage user-named attributes.
 */

extern NAMETAB attraccess_nametab[];

void do_attribute
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *aname,
    char *value
)
{
    int f;
    char *sp;
    ATTR *va2;
    BOOL negate, success;

    // Look up the user-named attribute we want to play with.
    //
    int nName;
    BOOL bValid;
    ATTR *va;
    char *pName = MakeCanonicalAttributeName(aname, &nName, &bValid);
    if (!bValid || !(va = (ATTR *)vattr_find_LEN(pName, nName)))
    {
        notify(executor, "No such user-named attribute.");
        return;
    }
    switch (key)
    {
    case ATTRIB_ACCESS:

        // Modify access to user-named attribute
        //
        _strupr(value);
        TINY_STRTOK_STATE tts;
        Tiny_StrTokString(&tts, value);
        Tiny_StrTokControl(&tts, " ");
        sp = Tiny_StrTokParse(&tts);
        success = FALSE;
        while (sp != NULL)
        {
            // Check for negation.
            //
            negate = FALSE;
            if (*sp == '!')
            {
                negate = TRUE;
                sp++;
            }

            // Set or clear the appropriate bit.
            //
            f = search_nametab(executor, attraccess_nametab, sp);
            if (f > 0)
            {
                success = TRUE;
                if (negate)
                    va->flags &= ~f;
                else
                    va->flags |= f;
            }
            else
            {
                notify(executor, tprintf("Unknown permission: %s.", sp));
            }

            // Get the next token.
            //
            sp = Tiny_StrTokParse(&tts);
        }
        if (success && !Quiet(executor))
            notify(executor, "Attribute access changed.");
        break;

    case ATTRIB_RENAME:

        {
            // Save the old name for use later.
            //
            char OldName[SBUF_SIZE];
            int nOldName = nName;
            memcpy(OldName, pName, nName+1);

            // Make sure the new name doesn't already exist. This checks
            // the built-in and user-defined data structures.
            //
            va2 = atr_str(value);
            if (va2)
            {
                notify(executor, "An attribute with that name already exists.");
                return;
            }
            pName = MakeCanonicalAttributeName(value, &nName, &bValid);
            if (!bValid || vattr_rename_LEN(OldName, nOldName, pName, nName) == NULL)
                notify(executor, "Attribute rename failed.");
            else
                notify(executor, "Attribute renamed.");
        }
        break;

    case ATTRIB_DELETE:

        // Remove the attribute.
        //
        vattr_delete_LEN(pName, nName);
        notify(executor, "Attribute deleted.");
        break;
    }
}

/* ---------------------------------------------------------------------------
 * do_fixdb: Directly edit database fields
 */

void do_fixdb
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
    init_match(executor, arg1, NOTYPE);
    match_everything(0);
    dbref thing = noisy_match_result();
    if (thing == NOTHING)
    {
        return;
    }

    dbref res = NOTHING;
    switch (key)
    {
    case FIXDB_OWNER:
    case FIXDB_LOC:
    case FIXDB_CON:
    case FIXDB_EXITS:
    case FIXDB_NEXT:
        init_match(executor, arg2, NOTYPE);
        match_everything(0);
        res = noisy_match_result();
        break;
    case FIXDB_PENNIES:
        res = Tiny_atol(arg2);
        break;
    }

    char *pValidName;
    switch (key)
    {
    case FIXDB_OWNER:

        s_Owner(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Owner set to #%d", res));
        break;

    case FIXDB_LOC:

        s_Location(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Location set to #%d", res));
        break;

    case FIXDB_CON:

        s_Contents(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Contents set to #%d", res));
        break;

    case FIXDB_EXITS:

        s_Exits(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Exits set to #%d", res));
        break;

    case FIXDB_NEXT:

        s_Next(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Next set to #%d", res));
        break;

    case FIXDB_PENNIES:

        s_Pennies(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf("Pennies set to %d", res));
        break;

    case FIXDB_NAME:

        if (isPlayer(thing))
        {
            if (!ValidatePlayerName(arg2))
            {
                notify(executor, "That's not a good name for a player.");
                return;
            }
            pValidName = arg2;
            if (lookup_player(NOTHING, pValidName, FALSE) != NOTHING)
            {
                notify(executor, "That name is already in use.");
                return;
            }
            STARTLOG(LOG_SECURITY, "SEC", "CNAME");
            log_name(thing),
            log_text(" renamed to ");
            log_text(pValidName);
            ENDLOG;
            if (Suspect(executor))
            {
                raw_broadcast(WIZARD, "[Suspect] %s renamed to %s",
                    Name(thing), pValidName);
            }
            delete_player_name(thing, Name(thing));
            s_Name(thing, pValidName);
            add_player_name(thing, pValidName);
        }
        else
        {
            int nTmp;
            BOOL bValid;
            pValidName = MakeCanonicalObjectName(arg2, &nTmp, &bValid);
            if (!bValid)
            {
                notify(executor, "That is not a reasonable name.");
                return;
            }
            s_Name(thing, pValidName);
        }
        if (!Quiet(executor))
        {
            notify(executor, tprintf("Name set to %s", pValidName));
        }
        break;
    }
}

#endif // !STANDALONE

// MakeCanonicalAttributeName
//
// See stringutil.cpp for valid characters used here..
//
// We truncate the attribute name to a length of SBUF_SIZE-1, if
// necessary, but we will validate the remaining characters anyway.
//
// NOTE: Refer to init_attrtab() where it directly manipulates
// Tiny_IsAttributeNameCharacter to allow the attribute name: "*Password".
//
char *MakeCanonicalAttributeName(const char *pName, int *pnName, BOOL *pbValid)
{
    static char Buffer[SBUF_SIZE];

    if (  !pName
       || !Tiny_IsFirstAttributeNameCharacter[(unsigned char)*pName])
    {
        *pnName = 0;
        *pbValid = FALSE;
        return NULL;
    }
    int nLeft = SBUF_SIZE-1;
    char *p = Buffer;
    while (*pName && nLeft)
    {
        if (!Tiny_IsAttributeNameCharacter[(unsigned char)*pName])
        {
            *pnName = 0;
            *pbValid = FALSE;
            return Buffer;
        }
        *p = Tiny_ToUpper[(unsigned char)*pName];
        p++;
        pName++;
        nLeft--;
    }
    *p = '\0';

    // Continue to validate remaining characters even though
    // we aren't going to use them. This helps to ensure that
    // softcode will run in the future if we increase the
    // size of SBUF_SIZE.
    //
    while (*pName)
    {
        if (!Tiny_IsAttributeNameCharacter[(unsigned char)*pName])
        {
            *pnName = 0;
            *pbValid = FALSE;
            return Buffer;
        }
        pName++;
    }

    // Length of truncated result.
    //
    *pnName = p - Buffer;
    *pbValid = TRUE;
    return Buffer;
}

// MakeCanonicalAttributeCommand
//
char *MakeCanonicalAttributeCommand(const char *pName, int *pnName, BOOL *pbValid)
{
    if (!pName)
    {
        *pnName = 0;
        *pbValid = FALSE;
        return NULL;
    }

    static char Buffer[SBUF_SIZE];
    int nLeft = SBUF_SIZE-2;
    char *p = Buffer;

    *p++ = '@';
    while (*pName && nLeft)
    {
        *p = Tiny_ToLower[(unsigned char)*pName];
        p++;
        pName++;
        nLeft--;
    }
    *p = '\0';

    // Length of result.
    //
    *pnName = p - Buffer;

    // Is the result valid?
    //
    *pbValid = (*pnName > 1);

    // Pointer to result
    //
    return Buffer;
}

/* ---------------------------------------------------------------------------
 * init_attrtab: Initialize the attribute hash tables.
 */

void init_attrtab(void)
{
    ATTR *a;

    // We specifically allow the '*' character at server
    // initialization because it's part of the A_PASS attribute
    // name.
    //
    Tiny_IsAttributeNameCharacter['*'] = TRUE;
    Tiny_IsFirstAttributeNameCharacter['*'] = TRUE;
    for (a = attr; a->number; a++)
    {
        int nLen;
        BOOL bValid;
        char *buff = MakeCanonicalAttributeName(a->name, &nLen, &bValid);
        if (!bValid)
        {
            continue;
        }
        anum_extend(a->number);
        anum_set(a->number, a);
        hashaddLEN(buff, nLen, (int *)a, &mudstate.attr_name_htab);
    }
    Tiny_IsFirstAttributeNameCharacter['*'] = FALSE;
    Tiny_IsAttributeNameCharacter['*'] = FALSE;
}

/* ---------------------------------------------------------------------------
 * atr_str: Look up an attribute by name.
 */

ATTR *atr_str(char *s)
{
    // Make attribute name canonical.
    //
    int nBuffer;
    BOOL bValid;
    char *buff = MakeCanonicalAttributeName(s, &nBuffer, &bValid);
    if (!bValid)
    {
        return NULL;
    }

    // Look for a predefined attribute.
    //
    ATTR *a = (ATTR *)hashfindLEN(buff, nBuffer, &mudstate.attr_name_htab);
    if (a != NULL)
    {
        return a;
    }

    // Nope, look for a user attribute.
    //
    a = vattr_find_LEN(buff, nBuffer);
    return a;
}

/* ---------------------------------------------------------------------------
 * anum_extend: Grow the attr num lookup table.
 */

ATTR **anum_table = NULL;
int anum_alc_top = 0;

void anum_extend(int newtop)
{
    ATTR **anum_table2;
    int delta, i;

#ifdef STANDALONE
    delta = 1000;
#else // STANDALONE
    delta = mudconf.init_size;
#endif // STANDALONE
    if (newtop <= anum_alc_top)
    {
        return;
    }
    if (newtop < anum_alc_top + delta)
    {
        newtop = anum_alc_top + delta;
    }
    if (anum_table == NULL)
    {
        anum_table = (ATTR **) MEMALLOC((newtop + 1) * sizeof(ATTR *));
        (void)ISOUTOFMEMORY(anum_table);
        for (i = 0; i <= newtop; i++)
        {
            anum_table[i] = NULL;
        }
    }
    else
    {
        anum_table2 = (ATTR **) MEMALLOC((newtop + 1) * sizeof(ATTR *));
        (void)ISOUTOFMEMORY(anum_table2);
        for (i = 0; i <= anum_alc_top; i++)
        {
            anum_table2[i] = anum_table[i];
        }
        for (i = anum_alc_top + 1; i <= newtop; i++)
        {
            anum_table2[i] = NULL;
        }
        MEMFREE((char *)anum_table);
        anum_table = anum_table2;
    }
    anum_alc_top = newtop;
}

// --------------------------------------------------------------------------
// atr_num: Look up an attribute by number.
//
ATTR *atr_num(int anum)
{
    if (anum > anum_alc_top)
    {
        return NULL;
    }
    return  anum_get(anum);
}

/* ---------------------------------------------------------------------------
 * mkattr: Lookup attribute by name, creating if needed.
 */

int mkattr(char *buff)
{
    ATTR *ap = atr_str(buff);
    if (!ap)
    {
        // Unknown attribute name, create a new one.
        //
        int nName;
        BOOL bValid;
        char *pName = MakeCanonicalAttributeName(buff, &nName, &bValid);
        ATTR *va;
        if (bValid)
        {
            int aflags = mudconf.vattr_flags;
            if (pName[0] == '_')
            {
                // An attribute that begins with an underline is
                // hidden from mortals and only changeable by
                // WIZARDs.
                //
                aflags |=  AF_MDARK | AF_WIZARD;
            }
            va = vattr_alloc_LEN(pName, nName, aflags);
            if (va && va->number)
            {
                return va->number;
            }
        }
        return -1;
    }
    else if (ap->number)
    {
        return ap->number;
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * al_decode: Fetch an attribute number from an alist.
 */

static int al_decode(char **app)
{
    char *ap = *app;

    int atrnum = 0, atrshft = 0;
    for (;;)
    {
        int ch = *ap++;
        int bits = ch & 0x7F;

        if (atrshft > 0)
            atrnum |= (bits << atrshft);
        else
            atrnum = bits;

        if ((ch & 0x80) == 0)
        {
            *app = ap;
            return atrnum;
        }
        atrshft += 7;
    }
}

// ---------------------------------------------------------------------------
// al_code: Store an attribute number in an alist
//
// Because A_LIST are attributes, too. We cannot generate a '\0', otherwise
// the size of an A_LIST cannot be determined with strlen. Fortunately, the
// following routine only genreates a '\0' in the atrnum == 0 case (which is
// never used).
//
static char *al_code(char *ap, int atrnum)
{
    int bits;
    for (;;)
    {
        bits = atrnum & 0x7F;
        if (atrnum <= 0x7F) break;
        atrnum >>= 7;
        bits |= 0x80;
        *ap++ = bits;
    }
    *ap++ = bits;
    return ap;
}

/* ---------------------------------------------------------------------------
 * Commer: check if an object has any $-commands in its attributes.
 */

BOOL Commer(dbref thing)
{
    char *s, *as, c;
    int attr, aflags;
    dbref aowner;
    ATTR *ap;

    atr_push();
    for (attr = atr_head(thing, &as); attr; attr = atr_next(&as))
    {
        ap = atr_num(attr);
        if (!ap || (ap->flags & AF_NOPROG))
            continue;

        s = atr_get(thing, attr, &aowner, &aflags);
        c = *s;
        free_lbuf(s);
        if ((c == '$') && !(aflags & AF_NOPROG))
        {
            atr_pop();
            return TRUE;
        }
    }
    atr_pop();
    return FALSE;
}

// routines to handle object attribute lists
//

#ifndef MEMORY_BASED
/* ---------------------------------------------------------------------------
 * al_fetch, al_store, al_add, al_delete: Manipulate attribute lists
 */

// al_extend: Get more space for attributes, if needed
//
void al_extend(char **buffer, int *bufsiz, int len, BOOL copy)
{
    if (len > *bufsiz)
    {
        int newsize = len + ATR_BUF_CHUNK;
        char *tbuff = (char *)MEMALLOC(newsize);
        (void)ISOUTOFMEMORY(tbuff);
        if (*buffer)
        {
            if (copy)
            {
                memcpy(tbuff, *buffer, *bufsiz);
            }
            MEMFREE(*buffer);
            *buffer = NULL;
        }
        *buffer = tbuff;
        *bufsiz = newsize;
    }
}

// al_store: Write modified attribute list
//
void al_store(void)
{
    if (mudstate.mod_al_id != NOTHING)
    {
        if (mudstate.mod_alist_len)
        {
            atr_add_raw_LEN(mudstate.mod_al_id, A_LIST, mudstate.mod_alist, mudstate.mod_alist_len);
        }
        else
        {
            atr_clr(mudstate.mod_al_id, A_LIST);
        }
    }
    mudstate.mod_al_id = NOTHING;
}

// al_fetch: Load attribute list
//
char *al_fetch(dbref thing)
{
    // We only need fetch if we change things.
    //
    if (mudstate.mod_al_id == thing)
    {
        return mudstate.mod_alist;
    }

    // Save old list, then fetch and set up the attribute list.
    //
    al_store();
    int len;
    const char *astr = atr_get_raw_LEN(thing, A_LIST, &len);
    if (astr)
    {
        al_extend(&mudstate.mod_alist, &mudstate.mod_size, len+1, FALSE);
        memcpy(mudstate.mod_alist, astr, len+1);
        mudstate.mod_alist_len = len;
    }
    else
    {
        al_extend(&mudstate.mod_alist, &mudstate.mod_size, 1, FALSE);
        *mudstate.mod_alist = '\0';
        mudstate.mod_alist_len = 0;
    }
    mudstate.mod_al_id = thing;
    return mudstate.mod_alist;
}

// al_add: Add an attribute to an attribute list
//
BOOL al_add(dbref thing, int attrnum)
{
    char *abuf = al_fetch(thing);
    char *cp = abuf;
    int anum;

    // See if attr is in the list.  If so, exit (need not do anything).
    //
    while (*cp)
    {
        anum = al_decode(&cp);
        if (anum == attrnum)
        {
            return TRUE;
        }
    }

    // The attribute isn't there, so we need to try to add it.
    //
    int iPosition = cp - abuf;

    // If we are too large for an attribute
    //
    if (iPosition + ATR_BUF_INCR >= LBUF_SIZE)
    {
        return FALSE;
    }

    // Extend it.
    //
    al_extend(&mudstate.mod_alist, &mudstate.mod_size, (iPosition + ATR_BUF_INCR), TRUE);
    if (mudstate.mod_alist != abuf)
    {
        // extend returned different buffer, re-position the end
        //
        cp = mudstate.mod_alist + iPosition;
    }

    // Add the new attribute on to the end.
    //
    cp = al_code(cp, attrnum);
    *cp = '\0';
    mudstate.mod_alist_len = cp - mudstate.mod_alist;
    return TRUE;
}

// al_delete: Remove an attribute from an attribute list
//
void al_delete(dbref thing, int attrnum)
{
    int anum;
    char *abuf, *cp, *dp;

    // If trying to modify List attrib, return.  Otherwise, get the attribute list.
    //
    if (attrnum == A_LIST)
    {
        return;
    }
    abuf = al_fetch(thing);
    if (!abuf)
    {
        return;
    }

    cp = abuf;
    while (*cp)
    {
        dp = cp;
        anum = al_decode(&cp);
        if (anum == attrnum)
        {
            while (*cp)
            {
                anum = al_decode(&cp);
                dp = al_code(dp, anum);
            }
            *dp = '\0';
            mudstate.mod_alist_len = dp - mudstate.mod_alist;
            return;
        }
    }
    return;
}

DCL_INLINE static void makekey(dbref thing, int atr, Aname *abuff)
{
    abuff->object = thing;
    abuff->attrnum = atr;
    return;
}
#endif // !MEMORY_BASED

/* ---------------------------------------------------------------------------
 * atr_encode: Encode an attribute string.
 */

static char *atr_encode(char *iattr, dbref thing, dbref owner, int flags, int atr)
{

    // If using the default owner and flags (almost all attributes will),
    // just store the string.
    //
    if (((owner == Owner(thing)) || (owner == NOTHING)) && !flags)
        return iattr;

    // Encode owner and flags into the attribute text.
    //
    if (owner == NOTHING)
        owner = Owner(thing);
    return tprintf("%c%d:%d:%s", ATR_INFO_CHAR, owner, flags, iattr);
}

// atr_decode_flags_owner: Decode the owner and flags (if present) and
// return a pointer to the attribute text.
//
static const char *atr_decode_flags_owner(const char *iattr, dbref *owner, int *flags)
{
    // See if the first char of the attribute is the special character
    //
    *flags = 0;
    if (*iattr != ATR_INFO_CHAR)
    {
        return iattr;
    }

    // It has the special character, crack the attr apart.
    //
    const char *cp = iattr + 1;

    // Get the attribute owner
    //
    BOOL neg = FALSE;
    if (*cp == '-')
    {
        neg = TRUE;
        cp++;
    }
    int tmp_owner = 0;
    unsigned int ch = *cp;
    while (Tiny_IsDigit[ch])
    {
        cp++;
        tmp_owner = 10*tmp_owner + (ch-'0');
        ch = *cp;
    }
    if (neg)
    {
        tmp_owner = -tmp_owner;
    }

    // If delimiter is not ':', just return attribute
    //
    if (*cp++ != ':')
    {
        return iattr;
    }

    // Get the attribute flags.
    //
    int tmp_flags = 0;
    ch = *cp;
    while (Tiny_IsDigit[ch])
    {
        cp++;
        tmp_flags = 10*tmp_flags + (ch-'0');
        ch = *cp;
    }

    // If delimiter is not ':', just return attribute.
    //
    if (*cp++ != ':')
    {
        return iattr;
    }

    // Get the attribute text.
    //
    if (tmp_owner != NOTHING)
    {
        *owner = tmp_owner;
    }
    *flags = tmp_flags;
    return cp;
}

// ---------------------------------------------------------------------------
// atr_decode: Decode an attribute string.
//
static void atr_decode_LEN(const char *iattr, int nLen, char *oattr,
                           dbref thing, dbref *owner, int *flags, int *pLen)
{
    // Default the owner
    //
    *owner = Owner(thing);

    // Parse for owner and flags
    //
    const char *cp = atr_decode_flags_owner(iattr, owner, flags);

    // Get the attribute text.
    //
    *pLen = nLen - (cp - iattr);
    if (oattr)
    {
        memcpy(oattr, cp, (*pLen) + 1);
    }
}

/* ---------------------------------------------------------------------------
 * atr_clr: clear an attribute in the list.
 */

void atr_clr(dbref thing, int atr)
{
#ifdef MEMORY_BASED

    if (!db[thing].at_count || !db[thing].ahead)
    {
        return;
    }

    Tiny_Assert(0 <= db[thing].at_count);

    // Binary search for the attribute.
    //
    int lo = 0;
    int mid;
    int hi = db[thing].at_count - 1;
    ATRLIST *list = db[thing].ahead;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (list[mid].number == atr)
        {
            MEMFREE(list[mid].data);
            list[mid].data = NULL;
            db[thing].at_count -= 1;
            if (mid != db[thing].at_count)
            {
                memmove(list+mid, list+mid+1, (db[thing].at_count - mid) * sizeof(ATRLIST));
            }
            break;
        }
        else if (list[mid].number > atr)
        {
            hi = mid - 1;
        }
        else
        {
            lo = mid + 1;
        }
    }
#else // MEMORY_BASED
    Aname okey;

    makekey(thing, atr, &okey);
    cache_del(&okey);
    al_delete(thing, atr);
#endif // MEMORY_BASED
    switch (atr)
    {
    case A_STARTUP:

        db[thing].fs.word[FLAG_WORD1] &= ~HAS_STARTUP;
        break;

    case A_DAILY:

        db[thing].fs.word[FLAG_WORD2] &= ~HAS_DAILY;
        break;

    case A_FORWARDLIST:

        db[thing].fs.word[FLAG_WORD2] &= ~HAS_FWDLIST;
#ifndef STANDALONE
        fwdlist_clr(thing); // We should clear the hashtable too
#endif // !STANDALONE
        break;

    case A_LISTEN:

        db[thing].fs.word[FLAG_WORD2] &= ~HAS_LISTEN;
        break;

#ifndef STANDALONE
    case A_TIMEOUT:

        desc_reload(thing);
        break;

    case A_QUEUEMAX:

        pcache_reload(thing);
        break;

#endif // !STANDALONE
    }
}

/* ---------------------------------------------------------------------------
 * atr_add_raw, atr_add: add attribute of type atr to list
 */

void atr_add_raw_LEN(dbref thing, int atr, const char *szValue, int nValue)
{
#ifdef MEMORY_BASED

    if (!szValue || szValue[0] == '\0')
    {
        atr_clr(thing, atr);
        return;
    }

    if (nValue > LBUF_SIZE-1)
    {
        nValue = LBUF_SIZE-1;
        szValue[nValue] = '\0';
    }
    ATRLIST *list;
    BOOL found = FALSE;
    int hi, lo, mid;
    char *text = StringCloneLen(szValue, nValue);

    if (!db[thing].ahead)
    {
        list = (ATRLIST *)MEMALLOC(sizeof(ATRLIST));
        (void)ISOUTOFMEMORY(list);
        db[thing].ahead = list;
        db[thing].at_count = 1;
        list[0].number = atr;
        list[0].data = text;
        list[0].size = nValue+1;
        found = TRUE;
    }
    else
    {
        // Binary search for the attribute
        //
        lo = 0;
        hi = db[thing].at_count - 1;

        list = db[thing].ahead;
        while (lo <= hi)
        {
            mid = ((hi - lo) >> 1) + lo;
            if (list[mid].number == atr)
            {
                MEMFREE(list[mid].data);
                list[mid].data = text;
                list[mid].size = nValue+1;
                found = TRUE;
                break;
            }
            else if (list[mid].number > atr)
            {
                hi = mid - 1;
            }
            else
            {
                lo = mid + 1;
            }
        }


        if (!found)
        {
            // If we got here, we didn't find it, so lo = hi + 1,
            // and the attribute should be inserted between them.
            //
            list = (ATRLIST *)MEMALLOC((db[thing].at_count + 1) * sizeof(ATRLIST));
            (void)ISOUTOFMEMORY(list);

            // Copy bottom part.
            //
            if (lo > 0)
            {
                memcpy(list, db[thing].ahead, lo * sizeof(ATRLIST));
            }

            // Copy top part.
            //
            if (lo < db[thing].at_count)
            {
                memcpy(list+lo+1, db[thing].ahead+lo, (db[thing].at_count - lo) * sizeof(ATRLIST));
            }
            MEMFREE(db[thing].ahead);
            db[thing].ahead = NULL;

            list[lo].data = text;
            list[lo].number = atr;
            list[lo].size = nValue+1;
            db[thing].at_count++;
            db[thing].ahead = list;
        }
    }
#else // MEMORY_BASED
    Aname okey;

    makekey(thing, atr, &okey);
    if (!szValue || szValue[0] == '\0')
    {
        cache_del(&okey);
        al_delete(thing, atr);
        return;
    }

    if (nValue > LBUF_SIZE-1)
    {
        nValue = LBUF_SIZE-1;
    }

    if (atr == A_LIST)
    {
        // A_LIST is never compressed and it's never listed within itself.
        //
        cache_put(&okey, szValue, nValue+1);
    }
    else
    {
        if (!al_add(thing, atr))
        {
            return;
        }
        cache_put(&okey, szValue, nValue+1);
    }
#endif // MEMORY_BASED

    switch (atr)
    {
    case A_STARTUP:

        db[thing].fs.word[FLAG_WORD1] |= HAS_STARTUP;
        break;

    case A_DAILY:

        db[thing].fs.word[FLAG_WORD2] |= HAS_DAILY;
        break;

    case A_FORWARDLIST:

        db[thing].fs.word[FLAG_WORD2] |= HAS_FWDLIST;
        break;

    case A_LISTEN:

        db[thing].fs.word[FLAG_WORD2] |= HAS_LISTEN;
        break;

#ifndef STANDALONE
    case A_TIMEOUT:

        desc_reload(thing);
        break;

    case A_QUEUEMAX:

        pcache_reload(thing);
        break;

#endif // !STANDALONE
    }
}

void atr_add_raw(dbref thing, int atr, const char *szValue)
{
    atr_add_raw_LEN(thing, atr, szValue, szValue ? strlen(szValue) : 0);
}

void atr_add(dbref thing, int atr, char *buff, dbref owner, int flags)
{
    set_modified(thing);

    if (!buff || !*buff)
    {
        atr_clr(thing, atr);
    }
    else
    {
        char *tbuff = atr_encode(buff, thing, owner, flags, atr);
        atr_add_raw(thing, atr, tbuff);
    }
}

void atr_set_flags(dbref thing, int atr, dbref flags)
{
    dbref aowner;
    int aflags;
    char *buff = atr_get(thing, atr, &aowner, &aflags);
    atr_add(thing, atr, buff, aowner, flags);
    free_lbuf(buff);
}

/* ---------------------------------------------------------------------------
 * get_atr,atr_get_raw, atr_get_str, atr_get: Get an attribute from the database.
 */

int get_atr(char *name)
{
    ATTR *ap = atr_str(name);

    if (!ap)
        return 0;
    if (!(ap->number))
        return -1;
    return ap->number;
}

#ifdef MEMORY_BASED
char *atr_get_raw_LEN(dbref thing, int atr, int *pLen)
{
    if (!Good_obj(thing))
    {
        return NULL;
    }

    // Binary search for the attribute.
    //
    ATRLIST *list = db[thing].ahead;
    if (!list)
    {
        return NULL;
    }

    int lo = 0;
    int hi = db[thing].at_count - 1;
    int mid;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (list[mid].number == atr)
        {
            *pLen = list[mid].size - 1;
            return list[mid].data;
        }
        else if (list[mid].number > atr)
        {
            hi = mid - 1;
        }
        else
        {
            lo = mid + 1;
        }
    }
    *pLen = 0;
    return NULL;
}

#else // MEMORY_BASED

const char *atr_get_raw_LEN(dbref thing, int atr, int *pLen)
{
    Aname okey;

    makekey(thing, atr, &okey);
    int nLen;
    const char *a = cache_get(&okey, &nLen);
    nLen = a ? (nLen-1) : 0;
    *pLen = nLen;
    return a;
}
#endif // MEMORY_BASED

const char *atr_get_raw(dbref thing, int atr)
{
    int Len;
    return atr_get_raw_LEN(thing, atr, &Len);
}

char *atr_get_str_LEN(char *s, dbref thing, int atr, dbref *owner, int *flags, int *pLen)
{
    const char *buff = atr_get_raw_LEN(thing, atr, pLen);
    if (!buff)
    {
        *owner = Owner(thing);
        *flags = 0;
        *pLen = 0;
        *s = '\0';
    }
    else
    {
        atr_decode_LEN(buff, *pLen, s, thing, owner, flags, pLen);
    }
    return s;
}

char *atr_get_str(char *s, dbref thing, int atr, dbref *owner, int *flags)
{
    int nLen;
    return atr_get_str_LEN(s, thing, atr, owner, flags, &nLen);
}

char *atr_get_LEN(dbref thing, int atr, dbref *owner, int *flags, int *pLen)
{
    char *buff = alloc_lbuf("atr_get");
    return atr_get_str_LEN(buff, thing, atr, owner, flags, pLen);
}

char *atr_get(dbref thing, int atr, dbref *owner, int *flags)
{
    int nLen;
    char *buff = alloc_lbuf("atr_get");
    return atr_get_str_LEN(buff, thing, atr, owner, flags, &nLen);
}

BOOL atr_get_info(dbref thing, int atr, dbref *owner, int *flags)
{
    int nLen;
    const char *buff = atr_get_raw_LEN(thing, atr, &nLen);
    if (!buff)
    {
        *owner = Owner(thing);
        *flags = 0;
        return FALSE;
    }
    atr_decode_LEN(buff, nLen, NULL, thing, owner, flags, &nLen);
    return TRUE;
}

#ifndef STANDALONE

char *atr_pget_str_LEN(char *s, dbref thing, int atr, dbref *owner, int *flags, int *pLen)
{
    dbref parent;
    int lev;
    ATTR *ap;
    const char *buff;

    ITER_PARENTS(thing, parent, lev)
    {
        buff = atr_get_raw_LEN(parent, atr, pLen);
        if (buff && *buff)
        {
            atr_decode_LEN(buff, *pLen, s, thing, owner, flags, pLen);
            if (  lev == 0
               || !(*flags & AF_PRIVATE))
            {
                return s;
            }
        }
        if (  lev == 0
           && Good_obj(Parent(parent)))
        {
            ap = atr_num(atr);
            if (!ap || ap->flags & AF_PRIVATE)
            {
                break;
            }
        }
    }
    *owner = Owner(thing);
    *flags = 0;
    *s = '\0';
    *pLen = 0;
    return s;
}

char *atr_pget_str(char *s, dbref thing, int atr, dbref *owner, int *flags)
{
    int nLen;
    return atr_pget_str_LEN(s, thing, atr, owner, flags, &nLen);
}

char *atr_pget_LEN(dbref thing, int atr, dbref *owner, int *flags, int *pLen)
{
    char *buff = alloc_lbuf("atr_pget");
    return atr_pget_str_LEN(buff, thing, atr, owner, flags, pLen);
}

char *atr_pget(dbref thing, int atr, dbref *owner, int *flags)
{
    int nLen;
    char *buff = alloc_lbuf("atr_pget");
    return atr_pget_str_LEN(buff, thing, atr, owner, flags, &nLen);
}

BOOL atr_pget_info(dbref thing, int atr, dbref *owner, int *flags)
{
    dbref parent;
    int lev;
    ATTR *ap;

    ITER_PARENTS(thing, parent, lev)
    {
        int nLen;
        const char *buff = atr_get_raw_LEN(parent, atr, &nLen);
        if (buff && *buff)
        {
            atr_decode_LEN(buff, nLen, NULL, thing, owner, flags, &nLen);
            if ((lev == 0) || !(*flags & AF_PRIVATE))
            {
                return TRUE;
            }
        }
        if ((lev == 0) && Good_obj(Parent(parent)))
        {
            ap = atr_num(atr);
            if (!ap || ap->flags & AF_PRIVATE)
                break;
        }
    }
    *owner = Owner(thing);
    *flags = 0;
    return FALSE;
}

#endif // !STANDALONE

/* ---------------------------------------------------------------------------
 * atr_free: Reset all attributes of an object.
 */

void atr_free(dbref thing)
{
#ifdef MEMORY_BASED
    if (db[thing].ahead)
    {
        MEMFREE(db[thing].ahead);
    }
    db[thing].ahead = NULL;
    db[thing].at_count = 0;
#else // MEMORY_BASED
    int attr;
    char *as;
    atr_push();
    for (attr = atr_head(thing, &as); attr; attr = atr_next(&as))
    {
        atr_clr(thing, attr);
    }
    atr_pop();
    if (mudstate.mod_al_id == thing)
        al_store(); // remove from cache
    atr_clr(thing, A_LIST);
#endif // MEMORY_BASED
}

/* ---------------------------------------------------------------------------
 * atr_cpy: Copy all attributes from one object to another.  
 */

void atr_cpy(dbref dest, dbref source)
{
    int attr, aflags;
    dbref aowner;
    char *as, *buf;
    ATTR *at;

    dbref owner = Owner(dest);
    atr_push();
    for (attr = atr_head(source, &as); attr; attr = atr_next(&as))
    {
        buf = atr_get(source, attr, &aowner, &aflags);
        if (!(aflags & AF_LOCK))
        {
            // Change owner.
            //
            aowner = owner;
        }
        at = atr_num(attr);
        if (attr && at)
        {
            if (  !(at->flags & (AF_INTERNAL|AF_NOCLONE))
               && (  God(owner) 
                  || (  !God(dest) 
                     && !(aflags & AF_LOCK) 
                     && (  (  Controls(owner, dest) 
                           && !(at->flags & (AF_WIZARD|AF_GOD)) 
                           && !(aflags & (AF_WIZARD|AF_GOD)))
                        || (  Wizard(owner)
                           && !(at->flags & AF_GOD))))))
            {
                // Only set attrs that owner has perm to set.
                //
                atr_add(dest, attr, buf, aowner, aflags);
            }
        }
        free_lbuf(buf);
    }
    atr_pop();
}

/* ---------------------------------------------------------------------------
 * atr_chown: Change the ownership of the attributes of an object to the
 * current owner if they are not locked.
 */

void atr_chown(dbref obj)
{
    int attr, aflags;
    dbref aowner;
    char *as, *buf;

    dbref owner = Owner(obj);
    atr_push();
    for (attr = atr_head(obj, &as); attr; attr = atr_next(&as))
    {
        buf = atr_get(obj, attr, &aowner, &aflags);
        if ((aowner != owner) && !(aflags & AF_LOCK))
            atr_add(obj, attr, buf, owner, aflags);
        free_lbuf(buf);
    }
    atr_pop();
}

/* ---------------------------------------------------------------------------
 * atr_next: Return next attribute in attribute list.
 */

int atr_next(char **attrp)
{
#ifdef MEMORY_BASED
    ATRCOUNT *atr;

    if (!attrp || !*attrp)
    {
        return 0;
    }
    else
    {
        atr = (ATRCOUNT *) * attrp;
        if (atr->count >= db[atr->thing].at_count)
        {
            MEMFREE(atr);
            atr = NULL;
            return 0;
        }
        atr->count++;
        return db[atr->thing].ahead[atr->count - 1].number;
    }

#else // MEMORY_BASED
    if (!*attrp || !**attrp)
    {
        return 0;
    }
    else
    {
        return al_decode(attrp);
    }
#endif // MEMORY_BASED
}

/* ---------------------------------------------------------------------------
 * atr_push, atr_pop: Push and pop attr lists.
 */

void atr_push(void)
{
#ifndef MEMORY_BASED
    ALIST *new_alist = (ALIST *) alloc_sbuf("atr_push");
    new_alist->data = mudstate.iter_alist.data;
    new_alist->len = mudstate.iter_alist.len;
    new_alist->next = mudstate.iter_alist.next;

    mudstate.iter_alist.data = NULL;
    mudstate.iter_alist.len = 0;
    mudstate.iter_alist.next = new_alist;
#endif // !MEMORY_BASED
}

void atr_pop(void)
{
#ifndef MEMORY_BASED
    ALIST *old_alist = mudstate.iter_alist.next;

    if (mudstate.iter_alist.data)
    {
        MEMFREE(mudstate.iter_alist.data);
        mudstate.iter_alist.data = NULL;
    }
    if (old_alist)
    {
        mudstate.iter_alist.data = old_alist->data;
        mudstate.iter_alist.len = old_alist->len;
        mudstate.iter_alist.next = old_alist->next;
        char *cp = (char *)old_alist;
        free_sbuf(cp);
    }
    else
    {
        mudstate.iter_alist.data = NULL;
        mudstate.iter_alist.len = 0;
        mudstate.iter_alist.next = NULL;
    }
#endif // !MEMORY_BASED
}

/* ---------------------------------------------------------------------------
 * atr_head: Returns the head of the attr list for object 'thing'
 */

int atr_head(dbref thing, char **attrp)
{
#ifdef MEMORY_BASED
    if (db[thing].at_count)
    {
        ATRCOUNT *atr = (ATRCOUNT *) MEMALLOC(sizeof(ATRCOUNT));
        (void)ISOUTOFMEMORY(atr);
        atr->thing = thing;
        atr->count = 1;
        *attrp = (char *)atr;
        return db[thing].ahead[0].number;
    }
    return 0;
#else // MEMORY_BASED
    const char *astr;
    int alen;

    // Get attribute list.  Save a read if it is in the modify atr list
    //
    if (thing == mudstate.mod_al_id)
    {
        astr = mudstate.mod_alist;
        alen = mudstate.mod_alist_len;
    }
    else
    {
        astr = atr_get_raw_LEN(thing, A_LIST, &alen);
    }

    // If no list, return nothing.
    //
    if (!alen)
    {
        return 0;
    }

    // Set up the list and return the first entry.
    //
    al_extend(&mudstate.iter_alist.data, &mudstate.iter_alist.len, alen+1, FALSE);
    memcpy(mudstate.iter_alist.data, astr, alen+1);
    *attrp = mudstate.iter_alist.data;
    return atr_next(attrp);
#endif // MEMORY_BASED
}


/* ---------------------------------------------------------------------------
 * db_grow: Extend the struct database.
 */

#define SIZE_HACK   1   // So mistaken refs to #-1 won't die.

void initialize_objects(dbref first, dbref last)
{
    dbref thing;

    for (thing = first; thing < last; thing++)
    {
        s_Owner(thing, GOD);
        s_Flags(thing, FLAG_WORD1, (TYPE_GARBAGE | GOING));
        s_Powers(thing, 0);
        s_Powers2(thing, 0);
        s_Location(thing, NOTHING);
        s_Contents(thing, NOTHING);
        s_Exits(thing, NOTHING);
        s_Link(thing, NOTHING);
        s_Next(thing, NOTHING);
        s_Zone(thing, NOTHING);
        s_Parent(thing, NOTHING);
        s_Stack(thing, NULL);
        db[thing].cpu_time_used.Set100ns(0);
#ifdef MEMORY_BASED
        db[thing].ahead = NULL;
        db[thing].at_count = 0;
#endif // MEMORY_BASED
    }
}

void db_grow(dbref newtop)
{
    int newsize, marksize, delta, i;
    MARKBUF *newmarkbuf;
    OBJ *newdb;
    NAME *newpurenames;
    char *cp;

#ifdef STANDALONE
    delta = 1000;
#else // STANDALONE
    delta = mudconf.init_size;
#endif // STANDALONE

    // Determine what to do based on requested size, current top and size.
    // Make sure we grow in reasonable-sized chunks to prevent frequent
    // reallocations of the db array.
    //
    // If requested size is smaller than the current db size, ignore it.
    //
    if (newtop <= mudstate.db_top)
    {
        return;
    }

    // If requested size is greater than the current db size but smaller
    // than the amount of space we have allocated, raise the db size and
    // initialize the new area.
    //
    if (newtop <= mudstate.db_size)
    {
        for (i = mudstate.db_top; i < newtop; i++)
        {
#ifndef MEMORY_BASED
            names[i] = NULL;
#endif // !MEMORY_BASED
            if (mudconf.cache_names)
                purenames[i] = NULL;
        }
        initialize_objects(mudstate.db_top, newtop);
        mudstate.db_top = newtop;
        return;
    }

    // Grow by a minimum of delta objects
    //
    if (newtop <= mudstate.db_size + delta)
    {
        newsize = mudstate.db_size + delta;
    }
    else
    {
        newsize = newtop;
    }

    // Enforce minimum database size
    //
    if (newsize < mudstate.min_size)
        newsize = mudstate.min_size + delta;;

    // Grow the name tables
    //
#ifndef MEMORY_BASED

    // NOTE: There is always one copy of 'names' around that isn't freed even
    // just before the process terminates. We rely (quite safely) on the OS to
    // reclaim the memory.
    //
    NAME *newnames = (NAME *) MEMALLOC((newsize + SIZE_HACK) * sizeof(NAME));
    (void)ISOUTOFMEMORY(newnames);
    memset(newnames, 0, (newsize + SIZE_HACK) * sizeof(NAME));

    if (names)
    {
        // An old name cache exists. Copy it.
        //
        names -= SIZE_HACK;
        memcpy(newnames, names, (newtop + SIZE_HACK) * sizeof(NAME));
        cp = (char *)names;
        MEMFREE(cp);
        cp = NULL;
    }
    else
    {
        // Creating a brand new struct database.  Fill in the 'reserved' area
        // in case it gets referenced.
        //
        names = newnames;
        for (i = 0; i < SIZE_HACK; i++)
        {
            names[i] = NULL;
        }
    }
    names = newnames + SIZE_HACK;
    newnames = NULL;
#endif // !MEMORY_BASED

    if (mudconf.cache_names)
    {
        // NOTE: There is always one copy of 'purenames' around that isn't
        // freed even just before the process terminates. We rely (quite
        // safely) on the OS to reclaim the memory.
        //
        newpurenames = (NAME *)MEMALLOC((newsize + SIZE_HACK) * sizeof(NAME));
        (void)ISOUTOFMEMORY(newpurenames);
        memset(newpurenames, 0, (newsize + SIZE_HACK) * sizeof(NAME));

        if (purenames)
        {
            // An old name cache exists. Copy it.
            //
            purenames -= SIZE_HACK;
            memcpy(newpurenames, purenames, (newtop + SIZE_HACK) * sizeof(NAME));
            cp = (char *)purenames;
            MEMFREE(cp);
            cp = NULL;
        }
        else
        {
            // Creating a brand new struct database. Fill in the 'reserved' area in case it gets referenced.
            //
            purenames = newpurenames;
            for (i = 0; i < SIZE_HACK; i++)
            {
                purenames[i] = NULL;
            }
        }
        purenames = newpurenames + SIZE_HACK;
        newpurenames = NULL;
    }

    // Grow the db array
    //

    // NOTE: There is always one copy of 'db' around that isn't freed even
    // just before the process terminates. We rely (quite safely) on the OS
    // to reclaim the memory.
    //
    newdb = (OBJ *)MEMALLOC((newsize + SIZE_HACK) * sizeof(OBJ));
    (void)ISOUTOFMEMORY(newdb);
    if (db)
    {
        // An old struct database exists. Copy it to the new buffer.
        //
        db -= SIZE_HACK;
        memcpy(newdb, db, (mudstate.db_top + SIZE_HACK) * sizeof(OBJ));
        cp = (char *)db;
        MEMFREE(cp);
        cp = NULL;
    }
    else
    {
        // Creating a brand new struct database. Fill in the 'reserved' area in case it gets referenced.
        //
        db = newdb;
        for (i = 0; i < SIZE_HACK; i++)
        {
#ifdef MEMORY_BASED
            db[i].ahead = NULL;
            db[i].at_count = 0;
#endif // MEMORY_BASED
            s_Owner(i, GOD);
            s_Flags(i, FLAG_WORD1, (TYPE_GARBAGE | GOING));
            s_Powers(i, 0);
            s_Powers2(i, 0);
            s_Location(i, NOTHING);
            s_Contents(i, NOTHING);
            s_Exits(i, NOTHING);
            s_Link(i, NOTHING);
            s_Next(i, NOTHING);
            s_Zone(i, NOTHING);
            s_Parent(i, NOTHING);
            s_Stack(i, NULL);
        }
    }
    db = newdb + SIZE_HACK;
    newdb = NULL;

    for (i = mudstate.db_top; i < newtop; i++)
    {
#ifndef MEMORY_BASED
        names[i] = NULL;
#endif // !MEMORY_BASED
        if (mudconf.cache_names)
        {
            purenames[i] = NULL;
        }
    }
    initialize_objects(mudstate.db_top, newtop);
    mudstate.db_top = newtop;
    mudstate.db_size = newsize;

    // Grow the db mark buffer.
    //
    marksize = (newsize + 7) >> 3;
    newmarkbuf = (MARKBUF *)MEMALLOC(marksize);
    (void)ISOUTOFMEMORY(newmarkbuf);
    memset(newmarkbuf, 0, marksize);
    if (mudstate.markbits)
    {
        marksize = (newtop + 7) >> 3;
        memcpy(newmarkbuf, mudstate.markbits, marksize);
        cp = (char *)mudstate.markbits;
        MEMFREE(cp);
        cp = NULL;
    }
    mudstate.markbits = newmarkbuf;
}

void db_free(void)
{
    char *cp;

    if (db != NULL)
    {
        db -= SIZE_HACK;
        cp = (char *)db;
        MEMFREE(cp);
        cp = NULL;
        db = NULL;
    }
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.freelist = NOTHING;
}

#ifndef STANDALONE
void db_make_minimal(void)
{
    db_free();
    db_grow(1);
    s_Name(0, "Limbo");
    s_Flags(0, FLAG_WORD1, TYPE_ROOM);
    s_Flags(0, FLAG_WORD2, 0);
    s_Flags(0, FLAG_WORD3, 0);
    s_Powers(0, 0);
    s_Powers2(0, 0);
    s_Location(0, NOTHING);
    s_Exits(0, NOTHING);
    s_Link(0, NOTHING);
    s_Parent(0, NOTHING);
    s_Zone(0, NOTHING);
    s_Pennies(0, 0);
    s_Owner(0, 1);

    // should be #1
    //
    load_player_names();
    dbref obj = create_player("Wizard", "potrzebie", NOTHING, FALSE, FALSE);
    s_Flags(obj, FLAG_WORD1, Flags(obj) | WIZARD);
    s_Powers(obj, 0);
    s_Powers2(obj, 0);
    s_Pennies(obj, 1000);

    // Manually link to Limbo, just in case
    //
    s_Location(obj, 0);
    s_Next(obj, NOTHING);
    s_Contents(0, obj);
    s_Link(obj, 0);
}
#endif // !STANDALONE

dbref parse_dbref(const char *s)
{
    const char *p;
    int x;

    // Enforce completely numeric dbrefs
    //
    for (p = s; *p; p++)
    {
        if (!Tiny_IsDigit[(unsigned char)*p])
            return NOTHING;
    }

    x = Tiny_atol(s);
    return ((x >= 0) ? x : NOTHING);
}


void putref(FILE *f, dbref ref)
{
    char buf[SBUF_SIZE];
    int n = Tiny_ltoa(ref, buf);
    buf[n] = '\n';
    fwrite(buf, sizeof(char), n+1, f);
}

// Code 0 - Any byte.
// Code 1 - CR (0x0D)
// Code 2 - '"' (0x22)
// Code 3 - '\\' (0x5C)
//
static const char xlat_table[256] =
{
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define STATE_START     0
#define STATE_HAVE_ESC  1


// Action 0 - Emit X.
// Action 1 - Get a Buffer.
// Action 2 - Emit X. Move to START state.
// Action 3 - Move to ESC state.
// Action 4 - Terminate parse.
//

static const int action_table[2][4] =
{
//   Any '\0' "   backslash
    { 0,  1,  3,  4 }, // STATE_START
    { 2,  1,  2,  2 }  // STATE_ESC
};

char *getstring_noalloc(FILE *f, int new_strings)
{
    static char buf[2*LBUF_SIZE + 20];
    int c = fgetc(f);
    if (new_strings && c == '"')
    {
        int nBufferLeft = sizeof(buf)-10;
        int iState = STATE_START;
        char *pOutput = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            char *pInput = pOutput + 6;
            if (fgets(pInput, nBufferLeft, f) == NULL)
            {
                // EOF or ERROR.
                //
                *pOutput = 0;
                return buf;
            }

            int nOutput = 0;

            // De-escape this data. removing the '\\' prefixes.
            // Terminate when you hit a '"'.
            //
            for (;;)
            {
                int ch = *pInput++;
                if (iState == STATE_START)
                {
                    if (xlat_table[ch] == 0)
                    {
                        // As long as xlat_table[*p] is 0, just keep copying the characters.
                        //
                        char *p = pOutput;
                        do
                        {
                            *pOutput++ = ch;
                            ch = *pInput++;
                        } while (xlat_table[ch] == 0);
                        nOutput = pOutput - p;
                    }
                }
                int iAction = action_table[iState][xlat_table[ch]];
                if (iAction <= 2)
                {
                    if (iAction == 1)
                    {
                        // Get Buffer and remain in the current state.
                        //
                        break;
                    }
                    else
                    {
                        // iAction == 2
                        // Emit X and move to START state.
                        //
                        *pOutput++ = ch;
                        nOutput++;
                        iState = STATE_START;
                    }
                }
                else if (iAction == 3)
                {
                    // Terminate parsing.
                    //
                    *pOutput = 0;
                    return buf;
                }
                else
                {
                    // iAction == 4
                    // Move to ESC state.
                    //
                    iState = STATE_HAVE_ESC;
                }
            }

            nBufferLeft -= nOutput;

            // Do we have any more room?
            //
            if (nBufferLeft <= 0)
            {
                *pOutput = 0;
                return buf;
            }
        }
    }
    else
    {
        ungetc(c, f);

        char *p = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            if (fgets(p, LBUF_SIZE, f) == NULL)
            {
                // EOF or ERROR.
                //
                p[0] = 0;
            }
            else
            {
                // How much data did we fetch?
                //
                int nLine = strlen(p);
                if (nLine >= 2)
                {
                    if (p[nLine-2] == '\r')
                    {
                        // Line is continued on the next line.
                        //
                        p += nLine;
                        continue;
                    }

                    // Eat '\n'
                    //
                    p[nLine-1] = '\0';
                }
            }
            return buf;
        }
    }
}

void putstring(FILE *f, const char *pRaw)
{
    static char aBuffer[2*LBUF_SIZE+4];
    char *pBuffer = aBuffer;

    // Always leave room for four characters. One at the beginning and
    // three on the end. '\\"\n' or '\""\n'
    //
    *pBuffer++ = '"';

    if (pRaw)
    {
        for (;;)
        {
            char ch;
            while ((ch = xlat_table[*pRaw]) == 0)
            {
                *pBuffer++ = *pRaw++;
            }
            if (ch == 1)
            {
                break;
            }
            *pBuffer++ = '\\';
            *pBuffer++ = *pRaw++;
        }
    }

    *pBuffer++ = '"';
    *pBuffer++ = '\n';

    fwrite(aBuffer, sizeof(char), pBuffer - aBuffer, f);
}

int getref(FILE *f)
{
    static char buf[SBUF_SIZE];
    fgets(buf, sizeof(buf), f);
    return Tiny_atol(buf);
}

void free_boolexp(BOOLEXP *b)
{
    if (b == TRUE_BOOLEXP)
        return;

    switch (b->type)
    {
    case BOOLEXP_AND:
    case BOOLEXP_OR:
        free_boolexp(b->sub1);
        free_boolexp(b->sub2);
        free_bool(b);
        break;
    case BOOLEXP_NOT:
    case BOOLEXP_CARRY:
    case BOOLEXP_IS:
    case BOOLEXP_OWNER:
    case BOOLEXP_INDIR:
        free_boolexp(b->sub1);
        free_bool(b);
        break;
    case BOOLEXP_CONST:
        free_bool(b);
        break;
    case BOOLEXP_ATR:
    case BOOLEXP_EVAL:
        MEMFREE(b->sub1);
        b->sub1 = NULL;
        free_bool(b);
        break;
    }
}

BOOLEXP *dup_bool(BOOLEXP *b)
{
    if (b == TRUE_BOOLEXP)
        return (TRUE_BOOLEXP);

    BOOLEXP *r = alloc_bool("dup_bool");
    switch (r->type = b->type)
    {
    case BOOLEXP_AND:
    case BOOLEXP_OR:
        r->sub2 = dup_bool(b->sub2);
    case BOOLEXP_NOT:
    case BOOLEXP_CARRY:
    case BOOLEXP_IS:
    case BOOLEXP_OWNER:
    case BOOLEXP_INDIR:
        r->sub1 = dup_bool(b->sub1);
    case BOOLEXP_CONST:
        r->thing = b->thing;
        break;
    case BOOLEXP_EVAL:
    case BOOLEXP_ATR:
        r->thing = b->thing;
        r->sub1 = (BOOLEXP *)StringClone((char *)b->sub1);
        break;
    default:
        Log.WriteString("Bad bool type!" ENDLINE);
        return TRUE_BOOLEXP;
    }
    return (r);
}

#ifndef MEMORY_BASED
int init_dbfile(char *game_dir_file, char *game_pag_file)
{
#ifdef STANDALONE
    Log.tinyprintf("Opening (%s,%s)" ENDLINE, game_dir_file, game_pag_file);
#endif // STANDALONE
    int cc = cache_init(game_dir_file, game_pag_file);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
#ifdef STANDALONE
        Log.tinyprintf("Done opening (%s,%s)." ENDLINE, game_dir_file, game_pag_file);
#else // STANDALONE
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        Log.tinyprintf("Using game db files: (%s,%s).", game_dir_file, game_pag_file);
        ENDLOG;
#endif // STANDALONE
        db_free();
    }
    return cc;
}
#endif // !MEMORY_BASED


#ifndef STANDALONE

// check_zone - checks back through a zone tree for control.
//
BOOL check_zone_handler (dbref player, dbref thing, BOOL bPlayerCheck)
{
    mudstate.zone_nest_num++;

    if (  !mudconf.have_zones
       || !Good_obj(Zone(thing))
       || mudstate.zone_nest_num >= mudconf.zone_nest_lim
       || (isPlayer(thing) == bPlayerCheck))
    {
        mudstate.zone_nest_num = 0;
        return FALSE;
    }

    // If the zone doesn't have an enterlock, DON'T allow control.
    //
    if (  atr_get_raw(Zone(thing), A_LENTER)
       && could_doit(player, Zone(thing), A_LENTER))
    {
        mudstate.zone_nest_num = 0;
        return TRUE;
    }
    else if (thing == Zone(thing))
    {
        return FALSE;
    }
    return check_zone_handler(player, Zone(thing), FALSE);
}

// This function releases:
//
//  1. comsys resources associated with an object.
//  2. @mail resources associated with an object.
//
void ReleaseAllResources(dbref obj)
{
    if (mudconf.have_comsys)
    {
        do_comdisconnect(obj);
        do_clearcom(obj, obj, obj, 0);
        do_channelnuke(obj);
        del_comsys(obj);
    }
    if (mudconf.have_mailer)
    {
        do_mail_clear(obj, NULL);
        do_mail_purge(obj);
        malias_cleanup(obj);
    }
}

#ifndef WIN32
/* ---------------------------------------------------------------------------
 * dump_restart_db: Writes out socket information.
 */
void dump_restart_db(void)
{
    FILE *f;
    DESC *d;
    int version = 1;

    f = fopen("restart.db", "wb");
    fprintf(f, "+V%d\n", version);
    putref(f, nMainGamePorts);
    for (int i = 0; i < nMainGamePorts; i++)
    {
        putref(f, aMainGamePorts[i].port);
        putref(f, aMainGamePorts[i].socket);
    }
    putref(f, mudstate.start_time.ReturnSeconds());
    putstring(f, mudstate.doing_hdr);
    putref(f, mudstate.record_players);
    DESC_ITER_ALL(d)
    {
        putref(f, d->descriptor);
        putref(f, d->flags);
        putref(f, d->connected_at.ReturnSeconds());
        putref(f, d->command_count);
        putref(f, d->timeout);
        putref(f, d->host_info);
        putref(f, d->player);
        putref(f, d->last_time.ReturnSeconds());
        putstring(f, d->output_prefix);
        putstring(f, d->output_suffix);
        putstring(f, d->addr);
        putstring(f, d->doing);
        putstring(f, d->username);
    }
    putref(f, 0);

    fclose(f);
}

void load_restart_db(void)
{
    DESC *d;
    DESC *p;

    int val;
    char *temp, buf[8];

    FILE *f = fopen("restart.db", "r");
    if (!f)
    {
        mudstate.restarting = FALSE;
        return;
    }
    DebugTotalFiles++;
    mudstate.restarting = TRUE;

    fgets(buf, 3, f);
    Tiny_Assert(strncmp(buf, "+V", 2) == 0);
    int version = getref(f);
    if (version == 1)
    {
        // Started on 2001-DEC-03
        //
        nMainGamePorts = getref(f);
        for (int i = 0; i < nMainGamePorts; i++)
        {
            aMainGamePorts[i].port   = getref(f);
            Tiny_Assert(aMainGamePorts[i].port > 0);
            aMainGamePorts[i].socket = getref(f);
            if (maxd <= aMainGamePorts[i].socket)
            {
                maxd = aMainGamePorts[i].socket + 1;
            }
        }
    }
    else if (version == 0)
    {
        // REMOVE: After 2002-DEC-03, remove support for version 0.
        //
        // This execution path assumes the port config option in the .conf
        // file has changed the aMainGamePorts[0].port part.
        //
        Tiny_Assert(aMainGamePorts[0].port > 0);
        aMainGamePorts[0].socket = getref(f);
        nMainGamePorts = 1;
        maxd = aMainGamePorts[0].socket + 1;
    }
    DebugTotalSockets += nMainGamePorts;

    mudstate.start_time.SetSeconds(getref(f));
    strcpy(mudstate.doing_hdr, getstring_noalloc(f, TRUE));
    mudstate.record_players = getref(f);

    while ((val = getref(f)) != 0)
    {
        ndescriptors++;
        DebugTotalSockets++;
        d = alloc_desc("restart");
        d->descriptor = val;
        d->flags = getref(f);
        d->connected_at.SetSeconds(getref(f));
        d->command_count = getref(f);
        d->timeout = getref(f);
        d->host_info = getref(f);
        d->player = getref(f);
        d->last_time.SetSeconds(getref(f));
        temp = getstring_noalloc(f, TRUE);
        if (*temp)
        {
            d->output_prefix = alloc_lbuf("set_userstring");
            strcpy(d->output_prefix, temp);
        }
        else
        {
            d->output_prefix = NULL;
        }
        temp = getstring_noalloc(f, TRUE);
        if (*temp)
        {
            d->output_suffix = alloc_lbuf("set_userstring");
            strcpy(d->output_suffix, temp);
        }
        else
        {
            d->output_suffix = NULL;
        }

        strcpy(d->addr, getstring_noalloc(f, TRUE));
        strcpy(d->doing, getstring_noalloc(f, TRUE));
        strcpy(d->username, getstring_noalloc(f, TRUE));

        d->output_size = 0;
        d->output_tot = 0;
        d->output_lost = 0;
        d->output_head = NULL;
        d->output_tail = NULL;
        d->input_head = NULL;
        d->input_tail = NULL;
        d->input_size = 0;
        d->input_tot = 0;
        d->input_lost = 0;
        d->raw_input = NULL;
        d->raw_input_at = NULL;
        d->quota = mudconf.cmd_quota_max;
        d->program_data = NULL;
        d->hashnext = NULL;

        if (descriptor_list)
        {
            for (p = descriptor_list; p->next; p = p->next) ;
            d->prev = &p->next;
            p->next = d;
            d->next = NULL;
        }
        else
        {
            d->next = descriptor_list;
            d->prev = &descriptor_list;
            descriptor_list = d;
        }

        if (maxd <= d->descriptor)
        {
            maxd = d->descriptor + 1;
        }
        desc_addhash(d);
        if (isPlayer(d->player))
        {
            s_Flags(d->player, FLAG_WORD2, Flags2(d->player) | CONNECTED);
        }
    }

    DESC_ITER_CONN(d)
    {
        if (!isPlayer(d->player))
        {
            shutdownsock(d, R_QUIT);
        }
    }

    if (fclose(f) == 0)
    {
        DebugTotalFiles--;
    }
    remove("restart.db");
    raw_broadcast(0, "GAME: Restart finished.");
}
#endif // !WIN32
#endif // !STANDALONE

#ifdef WIN32

int ReplaceFile(char *old_name, char *new_name)
{
    DeleteFile(new_name);
    if (MoveFile(old_name, new_name))
    {
        return 0;
    }
    else
    {
        Log.tinyprintf("MoveFile %s to %s fails with GetLastError() of %d" ENDLINE, old_name, new_name, GetLastError());
    }
    return -1;
}

void RemoveFile(char *name)
{
    DeleteFile(name);
}

#else // WIN32

int ReplaceFile(char *old_name, char *new_name)
{
    if (rename(old_name, new_name) == 0)
    {
        return 0;
    }
    else
    {
        Log.tinyprintf("rename %s to %s fails with errno of %s(%d)" ENDLINE, old_name, new_name, strerror(errno), errno);
    }
    return -1;
}

void RemoveFile(char *name)
{
    unlink(name);
}
#endif // WIN32

