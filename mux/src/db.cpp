// db.cpp
//
// $Id: db.cpp,v 1.96 2006-01-31 00:16:23 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "interface.h"
#include "powers.h"
#include "vattr.h"

#ifndef O_ACCMODE
#define O_ACCMODE   (O_RDONLY|O_WRONLY|O_RDWR)
#endif // O_ACCMODE

OBJ *db = NULL;

typedef struct atrcount ATRCOUNT;
struct atrcount
{
    dbref thing;
    int count;
};

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
    {"Alias",       A_ALIAS,    AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_PRIVATE | AF_CONST | AF_VISUAL},
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
    {"Desc",        A_DESC,     AF_VISUAL | AF_NOPROG},
    {"DefaultLock", A_LOCK,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"DescFormat",  A_DESCFORMAT, AF_ODARK | AF_NOPROG},
    {"Destroyer",   A_DESTROYER, AF_MDARK | AF_WIZARD | AF_NOPROG},
    {"Dfail",       A_DFAIL,    AF_ODARK | AF_NOPROG},
    {"Drop",        A_DROP,     AF_ODARK | AF_NOPROG},
    {"DropLock",    A_LDROP,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Ealias",      A_EALIAS,   AF_ODARK | AF_NOPROG},
    {"Efail",       A_EFAIL,    AF_ODARK | AF_NOPROG},
    {"Enter",       A_ENTER,    AF_ODARK | AF_NOPROG},
    {"EnterLock",   A_LENTER,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"ExitFormat",  A_EXITFORMAT, AF_ODARK | AF_NOPROG},
    {"ExitTo",      A_EXITVARDEST, AF_ODARK | AF_NOPROG | AF_WIZARD},
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
    {"MailLock",    A_LMAIL,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {"Mailmsg",     A_MAILMSG,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Mailsub",     A_MAILSUB,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Mailsucc",    A_MAIL,     AF_ODARK | AF_NOPROG},
    {"Mailto",      A_MAILTO,   AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {"Mfail",       A_MFAIL,    AF_ODARK | AF_NOPROG},
    {"Modified",    A_MODIFIED, AF_GOD | AF_VISUAL | AF_NOPROG | AF_NOCMD},
    {"Moniker",     A_MONIKER,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_CONST},
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
    {"SayString",   A_SAYSTRING, AF_ODARK | AF_NOPROG},
    {"Semaphore",   A_SEMAPHORE, AF_ODARK | AF_NOPROG | AF_WIZARD | AF_NOCMD | AF_NOCLONE},
    {"Sex",         A_SEX,      AF_VISUAL | AF_NOPROG},
    {"Signature",   A_SIGNATURE, AF_ODARK | AF_NOPROG},
    {"SpeechMod",   A_SPEECHMOD, AF_ODARK | AF_NOPROG},
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
#ifdef REALITY_LVLS
    {"Rlevel",         A_RLEVEL,        AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
#endif /* REALITY_LVLS */
    {NULL,          0,          0}
};

char *aszSpecialDBRefNames[1-NOPERM] =
{
    "", "*NOTHING*", "*AMBIGUOUS*", "*HOME*", "*NOPERMISSION*"
};

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
    ISOUTOFMEMORY(fp);

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
        hashreplLEN(&thing, sizeof(thing), fp, &mudstate.fwdlist_htab);
    }
    else
    {
        hashaddLEN(&thing, sizeof(thing), fp, &mudstate.fwdlist_htab);
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

/* ---------------------------------------------------------------------------
 * fwdlist_load: Load text into a forwardlist.
 */

int fwdlist_load(FWDLIST *fp, dbref player, char *atext)
{
    dbref target;
    char *tp, *bp, *dp;
    bool fail;

    int count = 0;
    int errors = 0;
    bp = tp = alloc_lbuf("fwdlist_load.str");
    mux_strncpy(tp, atext, LBUF_SIZE-1);
    do
    {
        // Skip spaces.
        //
        for (; mux_isspace(*bp); bp++)
        {
            ; // Nothing.
        }

        // Remember string.
        //
        for (dp = bp; *bp && !mux_isspace(*bp); bp++)
        {
            ; // Nothing.
        }

        // Terminate string.
        //
        if (*bp)
        {
            *bp++ = '\0';
        }

        if (  *dp++ == '#'
           && mux_isdigit(*dp))
        {
            target = mux_atol(dp);
            if (mudstate.bStandAlone)
            {
                fail = !Good_obj(target);
            }
            else
            {
                fail = (  !Good_obj(target)
                       || (  !God(player)
                          && !Controls(player, target)
                          && (  !Link_ok(target)
                             || !could_doit(player, target, A_LLINK))));
            }
            if (fail)
            {
                if (!mudstate.bStandAlone)
                {
                    notify(player,
                        tprintf("Cannot forward to #%d: Permission denied.",
                        target));
                }
                errors++;
            }
            else
            {
                fp->data[count++] = target;
            }
        }
    } while (*bp && count < 1000);
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
bool fwdlist_ck(dbref player, dbref thing, int anum, char *atext)
{
    UNUSED_PARAMETER(anum);

    if (mudstate.bStandAlone)
    {
        return true;
    }

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
}

FWDLIST *fwdlist_get(dbref thing)
{
    static FWDLIST *fp = NULL;
    if (mudstate.bStandAlone)
    {
        if (!fp)
        {
            fp = (FWDLIST *) alloc_lbuf("fwdlist_get");
        }
        dbref aowner;
        int   aflags;
        char *tp = atr_get(thing, A_FORWARDLIST, &aowner, &aflags);
        fwdlist_load(fp, GOD, tp);
        free_lbuf(tp);
    }
    else
    {
        fp = (FWDLIST *) hashfindLEN(&thing, sizeof(thing),
            &mudstate.fwdlist_htab);
    }
    return fp;
}

// ---------------------------------------------------------------------------
// Name, PureName, Moniker, s_Moniker, and s_Name: Get or set object's
// various names.
//
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
    if (!db[thing].name)
    {
        size_t len;
        char *pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &len);
        db[thing].name = StringCloneLen(pName, len);
        free_lbuf(pName);
    }
    return db[thing].name;
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
        if (!db[thing].purename)
        {
            size_t nName;
            size_t nPureName;
#ifdef MEMORY_BASED
            pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
            pPureName = strip_ansi(pName, &nPureName);
            free_lbuf(pName);
            db[thing].purename = StringCloneLen(pPureName, nPureName);
#else // MEMORY_BASED
            if (!db[thing].name)
            {
                pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
                db[thing].name = StringCloneLen(pName, nName);
                free_lbuf(pName);
            }
            else
            {
                nName = strlen(db[thing].name);
            }
            pName = db[thing].name;
            pPureName = strip_ansi(pName, &nPureName);
            if (nPureName == nName)
            {
                db[thing].purename = pName;
            }
            else
            {
                db[thing].purename = StringCloneLen(pPureName, nPureName);
            }
#endif // MEMORY_BASED
        }
        return db[thing].purename;
    }
    pName = atr_get(thing, A_NAME, &aowner, &aflags);
    pPureName = strip_ansi(pName);
    free_lbuf(pName);
    return pPureName;
}

const char *Moniker(dbref thing)
{
    if (thing < 0)
    {
        return aszSpecialDBRefNames[-thing];
    }
    if (db[thing].moniker)
    {
        return db[thing].moniker;
    }

    // Compare accent-stripped, ansi-stripped version of @moniker against
    // accent-stripped, ansi-stripped version of @name.
    //
    const char *pPureName = strip_accents(PureName(thing));
    char *pPureNameCopy = StringClone(pPureName);

    size_t nMoniker;
    dbref  aowner;
    int    aflags;
    char *pMoniker = atr_get_LEN(thing, A_MONIKER, &aowner, &aflags,
        &nMoniker);
    char *pPureMoniker = strip_accents(strip_ansi(pMoniker));

    const char *pReturn = NULL;
    static char tbuff[LBUF_SIZE];
    if (strcmp(pPureNameCopy, pPureMoniker) == 0)
    {
        // The stripped version of @moniker is the same as the stripped
        // version of @name, so (possibly cache and) use the unstripped
        // version of @moniker.
        //
        if (mudconf.cache_names)
        {
#ifdef MEMORY_BASED
            db[thing].moniker = StringCloneLen(pMoniker, nMoniker);
#else // MEMORY_BASED
            if (strcmp(pMoniker, Name(thing)) == 0)
            {
                db[thing].moniker = db[thing].name;
            }
            else
            {
                db[thing].moniker = StringCloneLen(pMoniker, nMoniker);
            }
#endif // MEMORY_BASED
            pReturn = db[thing].moniker;
        }
        else
        {
            memcpy(tbuff, pMoniker, nMoniker+1);
            pReturn = tbuff;
        }
    }
    else
    {
        // @moniker can't be used, so instead reflect @name (whether it
        // contains ANSI color and accents or not).
        //
#ifdef MEMORY_BASED
        if (mudconf.cache_names)
        {
            db[thing].moniker = StringClone(Name(thing));
            pReturn = db[thing].moniker;
        }
        else
        {
            pReturn = Name(thing);
        }
#else // MEMORY_BASED
        if (mudconf.cache_names)
        {
            db[thing].moniker = db[thing].name;
            pReturn = db[thing].moniker;
        }
        else
        {
            pReturn = Name(thing);
        }
#endif // MEMORY_BASED
    }
    free_lbuf(pMoniker);
    MEMFREE(pPureNameCopy);

    return pReturn;
}

void s_Name(dbref thing, const char *s)
{
    atr_add_raw(thing, A_NAME, s);
#ifndef MEMORY_BASED
    if (db[thing].name)
    {
        if (mudconf.cache_names)
        {
            if (db[thing].name == db[thing].purename)
            {
                db[thing].purename = NULL;
            }
            if (db[thing].name == db[thing].moniker)
            {
                db[thing].moniker = NULL;
            }
        }
        MEMFREE(db[thing].name);
        db[thing].name = NULL;
    }
    if (s)
    {
        db[thing].name = StringClone(s);
    }
#endif // !MEMORY_BASED
    if (mudconf.cache_names)
    {
        if (db[thing].purename)
        {
            MEMFREE(db[thing].purename);
            db[thing].purename = NULL;
        }
        if (db[thing].moniker)
        {
            MEMFREE(db[thing].moniker);
            db[thing].moniker = NULL;
        }
    }
}

void s_Moniker(dbref thing, const char *s)
{
    atr_add_raw(thing, A_MONIKER, s);
    if (mudconf.cache_names)
    {
#ifndef MEMORY_BASED
        if (db[thing].name == db[thing].moniker)
        {
            db[thing].moniker = NULL;
        }
#endif // !MEMORY_BASED
        if (db[thing].moniker)
        {
            MEMFREE(db[thing].moniker);
            db[thing].moniker = NULL;
        }
    }
}

void s_Pass(dbref thing, const char *s)
{
    atr_add_raw(thing, A_PASS, s);
}

/* ---------------------------------------------------------------------------
 * do_attrib: Manage user-named attributes.
 */

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
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nargs);

    // Look up the user-named attribute we want to play with.
    //
    size_t nName;
    bool bValid = false;
    ATTR *va = NULL;
    char *pName = MakeCanonicalAttributeName(aname, &nName, &bValid);
    if (bValid)
    {
        va = (ATTR *)vattr_find_LEN(pName, nName);
    }

    if (NULL == va)
    {
        notify(executor, "No such user-named attribute.");
        return;
    }

    int f;
    char *sp;
    ATTR *va2;
    bool negate, success;

    switch (key)
    {
    case ATTRIB_ACCESS:

        // Modify access to user-named attribute
        //
        mux_strupr(value);
        MUX_STRTOK_STATE tts;
        mux_strtok_src(&tts, value);
        mux_strtok_ctl(&tts, " ");
        sp = mux_strtok_parse(&tts);
        success = false;
        while (sp != NULL)
        {
            // Check for negation.
            //
            negate = false;
            if (*sp == '!')
            {
                negate = true;
                sp++;
            }

            // Set or clear the appropriate bit.
            //
            if (search_nametab(executor, attraccess_nametab, sp, &f))
            {
                success = true;
                if (negate)
                {
                    va->flags &= ~f;
                }
                else
                {
                    va->flags |= f;
                }
            }
            else
            {
                notify(executor, tprintf("Unknown permission: %s.", sp));
            }

            // Get the next token.
            //
            sp = mux_strtok_parse(&tts);
        }
        if (success && !Quiet(executor))
            notify(executor, "Attribute access changed.");
        break;

    case ATTRIB_RENAME:

        {
            // Save the old name for use later.
            //
            char OldName[SBUF_SIZE];
            char *pOldName = OldName;
            safe_sb_str(pName, OldName, &pOldName);
            *pOldName = '\0';
            size_t nOldName = pOldName - OldName;

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
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nargs);

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
        res = mux_atol(arg2);
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
            if (lookup_player(NOTHING, pValidName, false) != NOTHING)
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
            size_t nTmp;
            bool bValid;
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

// MakeCanonicalAttributeName
//
// See stringutil.cpp for valid characters used here..
//
// We truncate the attribute name to a length of SBUF_SIZE-1, if
// necessary, but we will validate the remaining characters anyway.
//
// NOTE: Refer to init_attrtab() where it directly manipulates
// mux_AttrNameSet to allow the attribute name: "*Password".
//
char *MakeCanonicalAttributeName(const char *pName, size_t *pnName, bool *pbValid)
{
    static char Buffer[SBUF_SIZE];

    if (  !pName
       || !mux_AttrNameInitialSet(*pName))
    {
        *pnName = 0;
        *pbValid = false;
        return NULL;
    }
    int nLeft = SBUF_SIZE-1;
    char *p = Buffer;
    while (*pName && nLeft)
    {
        if (!mux_AttrNameSet(*pName))
        {
            *pnName = 0;
            *pbValid = false;
            return Buffer;
        }
        *p = mux_toupper(*pName);
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
        if (!mux_AttrNameSet(*pName))
        {
            *pnName = 0;
            *pbValid = false;
            return Buffer;
        }
        pName++;
    }

    // Length of truncated result.
    //
    *pnName = p - Buffer;
    *pbValid = true;
    return Buffer;
}

// MakeCanonicalAttributeCommand
//
char *MakeCanonicalAttributeCommand(const char *pName, size_t *pnName, bool *pbValid)
{
    if (!pName)
    {
        *pnName = 0;
        *pbValid = false;
        return NULL;
    }

    static char Buffer[SBUF_SIZE];
    int nLeft = SBUF_SIZE-2;
    char *p = Buffer;

    *p++ = '@';
    while (*pName && nLeft)
    {
        *p = mux_tolower(*pName);
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
    const unsigned char star = '*';
    mux_AttrNameSet[star] = true;
    mux_AttrNameInitialSet[star] = true;
    for (a = attr; a->number; a++)
    {
        size_t nLen;
        bool bValid;
        char *buff = MakeCanonicalAttributeName(a->name, &nLen, &bValid);
        if (!bValid)
        {
            continue;
        }
        anum_extend(a->number);
        anum_set(a->number, a);
        hashaddLEN(buff, nLen, a, &mudstate.attr_name_htab);
    }
    mux_AttrNameInitialSet[star] = false;
    mux_AttrNameSet[star] = false;
}

/* ---------------------------------------------------------------------------
 * atr_str: Look up an attribute by name.
 */

ATTR *atr_str(char *s)
{
    // Make attribute name canonical.
    //
    size_t nBuffer;
    bool bValid;
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
    if (newtop <= anum_alc_top)
    {
        return;
    }

	int delta;
    if (mudstate.bStandAlone)
    {
        delta = 1000;
    }
    else
    {
        delta = mudconf.init_size;
    }
	int h = anum_alc_top + delta;
	if (  anum_alc_top < h
	   && newtop < h)
	{
		newtop = h;
	}
	
    int i;
    ATTR **anum_table2;
	if (anum_table == NULL)
    {
        anum_table = (ATTR **) MEMALLOC((newtop + 1) * sizeof(ATTR *));
        ISOUTOFMEMORY(anum_table);
        for (i = 0; i <= newtop; i++)
        {
            anum_table[i] = NULL;
        }
    }
    else
    {
        anum_table2 = (ATTR **) MEMALLOC((newtop + 1) * sizeof(ATTR *));
        ISOUTOFMEMORY(anum_table2);
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
    if (  anum < 0
       || anum_alc_top < anum)
    {
        return NULL;
    }
    return anum_get(anum);
}

static void SetupThrottle(dbref executor)
{
    CLinearTimeAbsolute tNow;
    CLinearTimeDelta    ltdHour;

    ltdHour.SetSeconds(60*60);
    tNow.GetUTC();

    db[executor].tThrottleExpired = tNow + ltdHour;
    s_ThAttrib(executor, mudconf.vattr_per_hour);
    s_ThMail(executor, mudconf.mail_per_hour);
}

static void SetupGlobalThrottle(void)
{
    CLinearTimeAbsolute tNow;
    CLinearTimeDelta    ltdHour;

    ltdHour.SetSeconds(60*60);
    tNow.GetUTC();

    mudstate.tThrottleExpired = tNow + ltdHour;
    mudstate.pcreates_this_hour = mudconf.pcreate_per_hour;
}

bool ThrottlePlayerCreate(void)
{
    if (0 < mudstate.pcreates_this_hour)
    {
        mudstate.pcreates_this_hour--;
        return false;
    }
    CLinearTimeAbsolute tNow;
    tNow.GetUTC();
    if (mudstate.tThrottleExpired <= tNow)
    {
        SetupGlobalThrottle();
        return false;
    }
    return true;
}

bool ThrottleAttributeNames(dbref executor)
{
    if (0 < ThAttrib(executor))
    {
        s_ThAttrib(executor, ThAttrib(executor)-1);
        return false;
    }
    CLinearTimeAbsolute tNow;
    tNow.GetUTC();
    if (db[executor].tThrottleExpired <= tNow)
    {
        SetupThrottle(executor);
        return false;
    }
    return true;
}

bool ThrottleMail(dbref executor)
{
    if (0 < ThMail(executor))
    {
        s_ThMail(executor, ThMail(executor)-1);
        return false;
    }
    CLinearTimeAbsolute tNow;
    tNow.GetUTC();
    if (db[executor].tThrottleExpired <= tNow)
    {
        SetupThrottle(executor);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * mkattr: Lookup attribute by name, creating if needed.
 */

int mkattr(dbref executor, char *buff)
{
    ATTR *ap = atr_str(buff);
    if (!ap)
    {
        // Unknown attribute name, create a new one.
        //
        size_t nName;
        bool bValid;
        char *pName = MakeCanonicalAttributeName(buff, &nName, &bValid);
        ATTR *va;
        if (bValid)
        {
            if (  !Wizard(executor)
               && ThrottleAttributeNames(executor))
            {
                return -1;
            }
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
// the size of an A_LIST cannot be determined with strlen(). Fortunately, the
// following routine only generates a '\0' if atrnum == 0 (which is
// never used).
//
static char *al_code(char *ap, unsigned int atrnum)
{
    int i;
    unsigned int bits;
    for (i = 0; i < ATR_BUF_INCR - 1; i++)
    {
        bits = atrnum & 0x7F;
        if (atrnum <= 0x7F)
        {
            ap[i] = (char)bits;
            break;
        }
        atrnum >>= 7;
        ap[i] = (char)(bits | 0x80);
    }
    return ap + i + 1;
}

/* ---------------------------------------------------------------------------
 * Commer: check if an object has any $-commands in its attributes.
 */

bool Commer(dbref thing)
{
    if (mudstate.bfNoCommands.IsSet(thing))
    {
        // We already know that there are no commands on this thing.
        //
        return false;
    }
    else if (mudstate.bfCommands.IsSet(thing))
    {
        // We already know that there are definitely commands on this thing.
        //
        return true;
    }

    bool bFoundListens = false;

    char *as;
    atr_push();
    char *buff = alloc_lbuf("Commer");
    for (int atr = atr_head(thing, &as); atr; atr = atr_next(&as))
    {
        ATTR *ap = atr_num(atr);
        if (  !ap
           || (ap->flags & AF_NOPROG))
        {
            continue;
        }

        int   aflags;
        dbref aowner;

        atr_get_str(buff, thing, atr, &aowner, &aflags);

        if (aflags & AF_NOPROG)
        {
            continue;
        }

        if (  AMATCH_CMD != buff[0]
           && AMATCH_LISTEN != buff[0])
        {
            continue;
        }

        // Search for unescaped ':'
        //
        char *s = strchr(buff+1, ':');
        if (!s)
        {
            continue;
        }

        if (AMATCH_CMD == buff[0])
        {
            free_lbuf(buff);
            atr_pop();
            mudstate.bfCommands.Set(thing);
            if (bFoundListens)
            {
                mudstate.bfListens.Set(thing);
                mudstate.bfNoListens.Clear(thing);
            }
            return true;
        }
        else // AMATCH_LISTEN == buff[0]
        {
            bFoundListens = true;
        }
    }
    free_lbuf(buff);
    atr_pop();
    mudstate.bfNoCommands.Set(thing);
    if (bFoundListens)
    {
        mudstate.bfListens.Set(thing);
        mudstate.bfNoListens.Clear(thing);
    }
    else
    {
        mudstate.bfNoListens.Set(thing);
        mudstate.bfListens.Clear(thing);
    }
    return false;
}

// routines to handle object attribute lists
//

#ifndef MEMORY_BASED
/* ---------------------------------------------------------------------------
 * al_fetch, al_store, al_add, al_delete: Manipulate attribute lists
 */

// al_extend: Get more space for attributes, if needed
//
static void al_extend(char **buffer, size_t *bufsiz, size_t len, bool copy)
{
    if (len > *bufsiz)
    {
        size_t newsize = len + ATR_BUF_CHUNK;
        char *tbuff = (char *)MEMALLOC(newsize);
        ISOUTOFMEMORY(tbuff);
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
static char *al_fetch(dbref thing)
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
    size_t len;
    const char *astr = atr_get_raw_LEN(thing, A_LIST, &len);
    if (astr)
    {
        al_extend(&mudstate.mod_alist, &mudstate.mod_size, len+1, false);
        memcpy(mudstate.mod_alist, astr, len+1);
        mudstate.mod_alist_len = len;
    }
    else
    {
        al_extend(&mudstate.mod_alist, &mudstate.mod_size, 1, false);
        *mudstate.mod_alist = '\0';
        mudstate.mod_alist_len = 0;
    }
    mudstate.mod_al_id = thing;
    return mudstate.mod_alist;
}

// al_add: Add an attribute to an attribute list
//
static bool al_add(dbref thing, int attrnum)
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
            return true;
        }
    }

    // The attribute isn't there, so we need to try to add it.
    //
    size_t iPosition = cp - abuf;

    // If we are too large for an attribute
    //
    if (iPosition + ATR_BUF_INCR >= LBUF_SIZE)
    {
        return false;
    }

    // Extend it.
    //
    al_extend(&mudstate.mod_alist, &mudstate.mod_size, (iPosition + ATR_BUF_INCR), true);
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
    return true;
}

// al_delete: Remove an attribute from an attribute list
//
static void al_delete(dbref thing, int attrnum)
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

static DCL_INLINE void makekey(dbref thing, int atr, Aname *abuff)
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
    UNUSED_PARAMETER(atr);

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
    bool neg = false;
    if (*cp == '-')
    {
        neg = true;
        cp++;
    }
    int tmp_owner = 0;
    unsigned int ch = *cp;
    while (mux_isdigit(ch))
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
    while (mux_isdigit(ch))
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
static void atr_decode_LEN(const char *iattr, size_t nLen, char *oattr,
                           dbref thing, dbref *owner, int *flags, size_t *pLen)
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

    mux_assert(0 <= db[thing].at_count);

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
        if (!mudstate.bStandAlone)
        {
            // We should clear the hashtable, too.
            //
            fwdlist_clr(thing);
        }
        break;

    case A_LISTEN:

        db[thing].fs.word[FLAG_WORD2] &= ~HAS_LISTEN;
        break;

    case A_TIMEOUT:

        desc_reload(thing);
        break;

    case A_QUEUEMAX:

        pcache_reload(thing);
        break;

    default:

        // Since this could overwrite an existing ^-Command or $-Command, we
        // longer assert that the object has one.
        //
        mudstate.bfListens.Clear(thing);
        mudstate.bfCommands.Clear(thing);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * atr_add_raw, atr_add: add attribute of type atr to list
 */

void atr_add_raw_LEN(dbref thing, int atr, const char *szValue, size_t nValue)
{
    if (!szValue || szValue[0] == '\0')
    {
        atr_clr(thing, atr);
        return;
    }

#ifdef MEMORY_BASED
    ATRLIST *list;
    bool found = false;
    int hi, lo, mid;
    char *text = StringCloneLen(szValue, nValue);

    if (!db[thing].ahead)
    {
        list = (ATRLIST *)MEMALLOC(sizeof(ATRLIST));
        ISOUTOFMEMORY(list);
        db[thing].ahead = list;
        db[thing].at_count = 1;
        list[0].number = atr;
        list[0].data = text;
        list[0].size = nValue+1;
        found = true;
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
                found = true;
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
            ISOUTOFMEMORY(list);

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

    if (nValue > LBUF_SIZE-1)
    {
        nValue = LBUF_SIZE-1;
    }

    Aname okey;
    makekey(thing, atr, &okey);
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

    case A_TIMEOUT:

        desc_reload(thing);
        break;

    case A_QUEUEMAX:

        pcache_reload(thing);
        break;
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
        switch (buff[0])
        {
        case AMATCH_LISTEN:

            // Since this could be a ^-Command, we no longer assert that the
            // object has none.
            //
            mudstate.bfNoListens.Clear(thing);
            break;

        case AMATCH_CMD:

            // Since this could be a $-Command, we no longer assert that the
            // object has none.
            //
            mudstate.bfNoCommands.Clear(thing);
            break;
        }

        // Since this could overwrite an existing ^-Command or $-Command, we
        // longer assert that the object has one.
        //
        mudstate.bfListens.Clear(thing);
        mudstate.bfCommands.Clear(thing);

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
const char *atr_get_raw_LEN(dbref thing, int atr, size_t *pLen)
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

const char *atr_get_raw_LEN(dbref thing, int atr, size_t *pLen)
{
    Aname okey;

    makekey(thing, atr, &okey);
    size_t nLen;
    const char *a = cache_get(&okey, &nLen);
    nLen = a ? (nLen-1) : 0;
    *pLen = nLen;
    return a;
}
#endif // MEMORY_BASED

const char *atr_get_raw(dbref thing, int atr)
{
    size_t Len;
    return atr_get_raw_LEN(thing, atr, &Len);
}

char *atr_get_str_LEN(char *s, dbref thing, int atr, dbref *owner, int *flags,
    size_t *pLen)
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
    size_t nLen;
    return atr_get_str_LEN(s, thing, atr, owner, flags, &nLen);
}

char *atr_get_LEN(dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
{
    char *buff = alloc_lbuf("atr_get");
    return atr_get_str_LEN(buff, thing, atr, owner, flags, pLen);
}

char *atr_get_real(dbref thing, int atr, dbref *owner, int *flags,
    const char *file, const int line)
{
    size_t nLen;
    char *buff = pool_alloc_lbuf("atr_get", file, line);
    return atr_get_str_LEN(buff, thing, atr, owner, flags, &nLen);
}

bool atr_get_info(dbref thing, int atr, dbref *owner, int *flags)
{
    size_t nLen;
    const char *buff = atr_get_raw_LEN(thing, atr, &nLen);
    if (!buff)
    {
        *owner = Owner(thing);
        *flags = 0;
        return false;
    }
    atr_decode_LEN(buff, nLen, NULL, thing, owner, flags, &nLen);
    return true;
}

char *atr_pget_str_LEN(char *s, dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
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
    size_t nLen;
    return atr_pget_str_LEN(s, thing, atr, owner, flags, &nLen);
}

char *atr_pget_LEN(dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
{
    char *buff = alloc_lbuf("atr_pget");
    return atr_pget_str_LEN(buff, thing, atr, owner, flags, pLen);
}

char *atr_pget_real(dbref thing, int atr, dbref *owner, int *flags,
    const char *file, const int line)
{
    size_t nLen;
    char *buff = pool_alloc_lbuf("atr_pget", file, line);
    return atr_pget_str_LEN(buff, thing, atr, owner, flags, &nLen);
}

bool atr_pget_info(dbref thing, int atr, dbref *owner, int *flags)
{
    dbref parent;
    int lev;
    ATTR *ap;

    ITER_PARENTS(thing, parent, lev)
    {
        size_t nLen;
        const char *buff = atr_get_raw_LEN(parent, atr, &nLen);
        if (buff && *buff)
        {
            atr_decode_LEN(buff, nLen, NULL, thing, owner, flags, &nLen);
            if ((lev == 0) || !(*flags & AF_PRIVATE))
            {
                return true;
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
    return false;
}

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
    char *as;
    atr_push();
    for (int atr = atr_head(thing, &as); atr; atr = atr_next(&as))
    {
        atr_clr(thing, atr);
    }
    atr_pop();
    if (mudstate.mod_al_id == thing)
    {
        al_store(); // remove from cache
    }
    atr_clr(thing, A_LIST);
#endif // MEMORY_BASED

    mudstate.bfCommands.Clear(thing);
    mudstate.bfNoCommands.Set(thing);
    mudstate.bfListens.Clear(thing);
    mudstate.bfNoListens.Set(thing);
}

/* ---------------------------------------------------------------------------
 * atr_cpy: Copy all attributes from one object to another.
 */

void atr_cpy(dbref dest, dbref source, bool bInternal)
{
    dbref owner = Owner(dest);

    char *as;
    atr_push();
    for (int atr = atr_head(source, &as); atr; atr = atr_next(&as))
    {
        int   aflags;
        dbref aowner;
        char *buf = atr_get(source, atr, &aowner, &aflags);

        if (!(aflags & AF_LOCK))
        {
            // Change owner.
            //
            aowner = owner;
        }

        ATTR *at = atr_num(atr);
        if (  atr
           && at)
        {
            if (  !(at->flags & (AF_INTERNAL|AF_NOCLONE))
               && (  bInternal
                  || God(owner)
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
                atr_add(dest, atr, buf, aowner, aflags);
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
    dbref owner = Owner(obj);

    char *as;
    atr_push();
    for (int atr = atr_head(obj, &as); atr; atr = atr_next(&as))
    {
        int   aflags;
        dbref aowner;
        char *buf = atr_get(obj, atr, &aowner, &aflags);
        if (  aowner != owner
           && !(aflags & AF_LOCK))
        {
            atr_add(obj, atr, buf, owner, aflags);
        }
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
        ISOUTOFMEMORY(atr);
        atr->thing = thing;
        atr->count = 1;
        *attrp = (char *)atr;
        return db[thing].ahead[0].number;
    }
    return 0;
#else // MEMORY_BASED
    const char *astr;
    size_t alen;

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
    al_extend(&mudstate.iter_alist.data, &mudstate.iter_alist.len, alen+1, false);
    memcpy(mudstate.iter_alist.data, astr, alen+1);
    *attrp = mudstate.iter_alist.data;
    return atr_next(attrp);
#endif // MEMORY_BASED
}


/* ---------------------------------------------------------------------------
 * db_grow: Extend the struct database.
 */

#define SIZE_HACK   1   // So mistaken refs to #-1 won't die.

static void initialize_objects(dbref first, dbref last)
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
#ifdef DEPRECATED
        s_Stack(thing, NULL);
#endif // DEPRECATED
        db[thing].cpu_time_used.Set100ns(0);
        db[thing].tThrottleExpired.Set100ns(0);
        s_ThAttrib(thing, 0);
        s_ThMail(thing, 0);

#ifdef MEMORY_BASED
        db[thing].ahead = NULL;
        db[thing].at_count = 0;
#else
        db[thing].name = NULL;
#endif // MEMORY_BASED
        db[thing].purename = NULL;
        db[thing].moniker = NULL;
    }
}

void db_grow(dbref newtop)
{
    mudstate.bfCommands.Resize(newtop);
    mudstate.bfNoCommands.Resize(newtop);
    mudstate.bfListens.Resize(newtop);
    mudstate.bfNoListens.Resize(newtop);

    int delta;
    if (mudstate.bStandAlone)
    {
        delta = 1000;
    }
    else
    {
        delta = mudconf.init_size;
    }

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
        initialize_objects(mudstate.db_top, newtop);
        mudstate.db_top = newtop;
        return;
    }

    // Grow by a minimum of delta objects
    //
    int newsize = newtop;
    int nMinimumGrowth = mudstate.db_size + delta;
    if (newtop <= nMinimumGrowth)
    {
        newsize = nMinimumGrowth;
    }

    // Enforce minimum database size
    //
    if (newsize < mudstate.min_size)
    {
        newsize = mudstate.min_size;
    }

    // Grow the db array
    //

    // NOTE: There is always one copy of 'db' around that isn't freed even
    // just before the process terminates. We rely (quite safely) on the OS
    // to reclaim the memory.
    //
    OBJ *newdb = (OBJ *)MEMALLOC((newsize + SIZE_HACK) * sizeof(OBJ));
    ISOUTOFMEMORY(newdb);
    if (db)
    {
        // An old struct database exists. Copy it to the new buffer.
        //
        db -= SIZE_HACK;
        memcpy(newdb, db, (mudstate.db_top + SIZE_HACK) * sizeof(OBJ));
        MEMFREE(db);
    }
    else
    {
        // Creating a brand new struct database. Fill in the 'reserved' area
        // in case it is referenced.
        //
        db = newdb;
        initialize_objects(0, SIZE_HACK);
    }
    db = newdb + SIZE_HACK;
    newdb = NULL;

    initialize_objects(mudstate.db_top, newtop);
    mudstate.db_top = newtop;
    mudstate.db_size = newsize;

    // Grow the db mark buffer.
    //
    int marksize = (newsize + 7) >> 3;
    MARKBUF *newmarkbuf = (MARKBUF *)MEMALLOC(marksize);
    ISOUTOFMEMORY(newmarkbuf);
    memset(newmarkbuf, 0, marksize);
    if (mudstate.markbits)
    {
        marksize = (newtop + 7) >> 3;
        memcpy(newmarkbuf, mudstate.markbits, marksize);
        MEMFREE(mudstate.markbits);
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
    const char *pmsg;
    dbref obj = create_player("Wizard", "potrzebie", NOTHING, false, &pmsg);
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

dbref parse_dbref(const char *s)
{
    // Enforce completely numeric dbrefs
    //
    const char *p = s;
    if (p[0])
    {
        do
        {
            if (!mux_isdigit(*p))
            {
                return NOTHING;
            }
            p++;
        } while (*p);
    }
    else
    {
        return NOTHING;
    }
    int x = mux_atol(s);
    return ((x >= 0) ? x : NOTHING);
}


void putref(FILE *f, dbref ref)
{
    char buf[SBUF_SIZE];
    size_t n = mux_ltoa(ref, buf);
    buf[n] = '\n';
    fwrite(buf, sizeof(char), n+1, f);
}

// Code 0 - Any byte.
// Code 1 - NUL  (0x00)
// Code 2 - '"'  (0x22)
// Code 3 - '\\' (0x5C)
//
static const unsigned char xlat_table[256] =
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
// Action 3 - Terminate parse.
// Action 4 - Move to ESC state.
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
        size_t nBufferLeft = sizeof(buf)-10;
        int iState = STATE_START;
        char *pOutput = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            char *pInput = pOutput + 6;
            if (fgets(pInput, static_cast<int>(nBufferLeft), f) == NULL)
            {
                // EOF or ERROR.
                //
                *pOutput = 0;
                return buf;
            }

            size_t nOutput = 0;

            // De-escape this data. removing the '\\' prefixes.
            // Terminate when you hit a '"'.
            //
            for (;;)
            {
                char ch = *pInput++;
                if (iState == STATE_START)
                {
                    if (xlat_table[(unsigned char)ch] == 0)
                    {
                        // As long as xlat_table[*p] is 0, just keep copying the characters.
                        //
                        char *p = pOutput;
                        do
                        {
                            *pOutput++ = ch;
                            ch = *pInput++;
                        } while (xlat_table[(unsigned char)ch] == 0);
                        nOutput = pOutput - p;
                    }
                }
                int iAction = action_table[iState][xlat_table[(unsigned char)ch]];
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
                size_t nLine = strlen(p);
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
            while ((ch = xlat_table[(unsigned char)*pRaw]) == 0)
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
    if (NULL != fgets(buf, sizeof(buf), f))
    {
        return mux_atol(buf);
    }
    else
    {
        return 0;
    }
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

static BOOLEXP *dup_bool(BOOLEXP *b)
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
int init_dbfile(char *game_dir_file, char *game_pag_file, int nCachePages)
{
    if (mudstate.bStandAlone)
    {
        Log.tinyprintf("Opening (%s,%s)" ENDLINE, game_dir_file, game_pag_file);
    }
    int cc = cache_init(game_dir_file, game_pag_file, nCachePages);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        if (mudstate.bStandAlone)
        {
            Log.tinyprintf("Done opening (%s,%s)." ENDLINE, game_dir_file,
                game_pag_file);
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            Log.tinyprintf("Using game db files: (%s,%s).", game_dir_file,
                game_pag_file);
            ENDLOG;
        }
        db_free();
    }
    return cc;
}
#endif // !MEMORY_BASED

// check_zone - checks back through a zone tree for control.
//
bool check_zone_handler(dbref player, dbref thing, bool bPlayerCheck)
{
    mudstate.zone_nest_num++;

    if (  !mudconf.have_zones
       || !Good_obj(Zone(thing))
       || mudstate.zone_nest_num >= mudconf.zone_nest_lim
       || isPlayer(thing) != bPlayerCheck)
    {
        mudstate.zone_nest_num = 0;
        return false;
    }

    // If the zone doesn't have an enterlock, DON'T allow control.
    //
    if (  atr_get_raw(Zone(thing), A_LENTER)
       && could_doit(player, Zone(thing), A_LENTER))
    {
        mudstate.zone_nest_num = 0;
        return true;
    }
    else if (thing == Zone(thing))
    {
        return false;
    }
    return check_zone_handler(player, Zone(thing), false);
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
    int version = 2;

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
        putref(f, d->raw_input_state);
        putref(f, d->nvt_sga_him_state);
        putref(f, d->nvt_sga_us_state);
        putref(f, d->nvt_eor_him_state);
        putref(f, d->nvt_eor_us_state);
        putref(f, d->nvt_naws_him_state);
        putref(f, d->nvt_naws_us_state);
        putref(f, d->height);
        putref(f, d->width);
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
        mudstate.restarting = false;
        return;
    }
    DebugTotalFiles++;
    mudstate.restarting = true;

    fgets(buf, 3, f);
    mux_assert(strncmp(buf, "+V", 2) == 0);
    int version = getref(f);
    if (  1 == version
       || 2 == version)
    {
        // Version 1 started on 2001-DEC-03
        // Version 2 started on 2005-NOV-08
        //
        nMainGamePorts = getref(f);
        for (int i = 0; i < nMainGamePorts; i++)
        {
            aMainGamePorts[i].port   = getref(f);
            mux_assert(aMainGamePorts[i].port > 0);
            aMainGamePorts[i].socket = getref(f);
            if (maxd <= aMainGamePorts[i].socket)
            {
                maxd = aMainGamePorts[i].socket + 1;
            }
        }
    }
    else
    {
        // The restart file, restart.db, has a version other than 1.  You
        // cannot @restart from the previous version to the new version.  Use
        // @shutdown instead.
        //
        mux_assert(0);
    }
    DebugTotalSockets += nMainGamePorts;

    mudstate.start_time.SetSeconds(getref(f));
    strcpy(mudstate.doing_hdr, getstring_noalloc(f, true));
    mudstate.record_players = getref(f);
    if (mudconf.reset_players)
    {
        mudstate.record_players = 0;
    }

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
        if (2 == version)
        {
            d->raw_input_state    = getref(f);
            d->nvt_sga_him_state  = getref(f);
            d->nvt_sga_us_state   = getref(f);
            d->nvt_eor_him_state  = getref(f);
            d->nvt_eor_us_state   = getref(f);
            d->nvt_naws_him_state = getref(f);
            d->nvt_naws_us_state  = getref(f);
            d->height = getref(f);
            d->width = getref(f);
        }
        else
        {
            d->raw_input_state    = NVT_IS_NORMAL;
            d->nvt_sga_him_state  = OPTION_NO;
            d->nvt_sga_us_state   = OPTION_NO;
            d->nvt_eor_him_state  = OPTION_NO;
            d->nvt_eor_us_state   = OPTION_NO;
            d->nvt_naws_him_state = OPTION_NO;
            d->nvt_naws_us_state  = OPTION_NO;
            d->height = 24;
            d->width = 78;
        }

        temp = getstring_noalloc(f, true);
        if (*temp)
        {
            d->output_prefix = alloc_lbuf("set_userstring");
            strcpy(d->output_prefix, temp);
        }
        else
        {
            d->output_prefix = NULL;
        }
        temp = getstring_noalloc(f, true);
        if (*temp)
        {
            d->output_suffix = alloc_lbuf("set_userstring");
            strcpy(d->output_suffix, temp);
        }
        else
        {
            d->output_suffix = NULL;
        }

        strcpy(d->addr, getstring_noalloc(f, true));
        strcpy(d->doing, getstring_noalloc(f, true));
        strcpy(d->username, getstring_noalloc(f, true));

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
        d->nOption = 0;
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
            s_Connected(d->player);
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
        Log.tinyprintf("MoveFile %s to %s fails with GetLastError() of %d" ENDLINE,
            old_name, new_name, GetLastError());
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

