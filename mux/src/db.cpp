/*! \file db.cpp
 * \brief Attribute interface, some flatfile and object routines.
 *
 * This file mainly has to do with attributes and objects in memory,
 * but it is somewhat bloated.  Contents now include: the list of
 * built-in attributes, fwdlist routines, Name-related routines,
 * the \@attribute and \@fixdb commands, the attribute interface
 * (validation, name / number lookup, attribute list management,
 * encoding / decoding of attribute metadata, get / set, copy, chown),
 * creation throttle routines, in-memory object database routines,
 * new ("minimal") database setup, flatfile i/o, zone control,
 * comsys / mail resource cleanup, restart db i/o, and some basic
 * filesystem delete / rename routines.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "interface.h"
#include "mathutil.h"
#include "powers.h"
#include "vattr.h"

#ifndef O_ACCMODE
#define O_ACCMODE   (O_RDONLY|O_WRONLY|O_RDWR)
#endif // O_ACCMODE

OBJ *db = nullptr;

typedef struct atrcount ATRCOUNT;
struct atrcount
{
    dbref thing;
    int count;
};

// List of Attributes.
//
ATTR AttrTable[] =
{
    {T("Aahear"),      A_AAHEAR,   AF_ODARK | AF_NOPROG},
    {T("Aclone"),      A_ACLONE,   AF_ODARK | AF_NOPROG},
    {T("Aconnect"),    A_ACONNECT, AF_ODARK | AF_NOPROG},
    {T("ACreate"),     A_ACREATE,  AF_MDARK | AF_WIZARD | AF_NOCLONE | AF_NOPROG},
    {T("Adesc"),       A_ADESC,    AF_ODARK | AF_NOPROG},
    {T("ADestroy"),    A_ADESTROY, AF_MDARK | AF_WIZARD | AF_NOCLONE | AF_NOPROG},
    {T("Adfail"),      A_ADFAIL,   AF_ODARK | AF_NOPROG},
    {T("Adisconnect"), A_ADISCONNECT, AF_ODARK | AF_NOPROG},
    {T("Adrop"),       A_ADROP,    AF_ODARK | AF_NOPROG},
    {T("Aefail"),      A_AEFAIL,   AF_ODARK | AF_NOPROG},
    {T("Aenter"),      A_AENTER,   AF_ODARK | AF_NOPROG},
    {T("Afail"),       A_AFAIL,    AF_ODARK | AF_NOPROG},
    {T("Agfail"),      A_AGFAIL,   AF_ODARK | AF_NOPROG},
    {T("Ahear"),       A_AHEAR,    AF_ODARK | AF_NOPROG},
    {T("Akill"),       A_AKILL,    AF_ODARK | AF_NOPROG},
    {T("Aleave"),      A_ALEAVE,   AF_ODARK | AF_NOPROG},
    {T("Alfail"),      A_ALFAIL,   AF_ODARK | AF_NOPROG},
    {T("Alias"),       A_ALIAS,    AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_PRIVATE | AF_CONST | AF_VISUAL},
    {T("Allowance"),   A_ALLOWANCE, AF_MDARK | AF_NOPROG | AF_WIZARD},
    {T("Amail"),       A_AMAIL,    AF_ODARK | AF_NOPROG},
    {T("Amhear"),      A_AMHEAR,   AF_ODARK | AF_NOPROG},
    {T("Amove"),       A_AMOVE,    AF_ODARK | AF_NOPROG},
    {T("Aparent"),     A_APARENT,  AF_ODARK | AF_NOPROG | AF_NOCLONE},
    {T("Apay"),        A_APAY,     AF_ODARK | AF_NOPROG},
    {T("Arfail"),      A_ARFAIL,   AF_ODARK | AF_NOPROG},
    {T("Asucc"),       A_ASUCC,    AF_ODARK | AF_NOPROG},
    {T("Atfail"),      A_ATFAIL,   AF_ODARK | AF_NOPROG},
    {T("Atport"),      A_ATPORT,   AF_ODARK | AF_NOPROG},
    {T("Atofail"),     A_ATOFAIL,  AF_ODARK | AF_NOPROG},
    {T("Aufail"),      A_AUFAIL,   AF_ODARK | AF_NOPROG},
    {T("Ause"),        A_AUSE,     AF_ODARK | AF_NOPROG},
    {T("Away"),        A_AWAY,     AF_ODARK | AF_NOPROG},
    {T("Charges"),     A_CHARGES,  AF_ODARK | AF_NOPROG},
    {T("CmdCheck"),    A_CMDCHECK, AF_DARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_PRIVATE | AF_CONST | AF_NODECOMP},
    {T("Comjoin"),     A_COMJOIN,  AF_ODARK | AF_NOPROG},
    {T("Comleave"),    A_COMLEAVE, AF_ODARK | AF_NOPROG},
    {T("Comment"),     A_COMMENT,  AF_MDARK | AF_WIZARD},
    {T("Comoff"),      A_COMOFF,   AF_ODARK | AF_NOPROG},
    {T("Comon"),       A_COMON,    AF_ODARK | AF_NOPROG},
    {T("ConFormat"),   A_CONFORMAT, AF_ODARK | AF_NOPROG},
    {T("Cost"),        A_COST,     AF_ODARK | AF_NOPROG},
    {T("Created"),     A_CREATED,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_CONST | AF_NODECOMP},
    {T("Daily"),       A_DAILY,    AF_ODARK | AF_NOPROG},
    {T("Desc"),        A_DESC,     AF_VISUAL | AF_NOPROG},
    {T("DefaultLock"), A_LOCK,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK | AF_NODECOMP},
    {T("DescFormat"),  A_DESCFORMAT, AF_ODARK | AF_NOPROG},
    {T("Destroyer"),   A_DESTROYER, AF_MDARK | AF_WIZARD | AF_NOPROG},
    {T("Dfail"),       A_DFAIL,    AF_ODARK | AF_NOPROG},
    {T("Drop"),        A_DROP,     AF_ODARK | AF_NOPROG},
    {T("DropLock"),    A_LDROP,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Ealias"),      A_EALIAS,   AF_ODARK | AF_NOPROG},
    {T("Efail"),       A_EFAIL,    AF_ODARK | AF_NOPROG},
    {T("Enter"),       A_ENTER,    AF_ODARK | AF_NOPROG},
    {T("EnterLock"),   A_LENTER,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("ExitFormat"),  A_EXITFORMAT, AF_ODARK | AF_NOPROG},
    {T("ExitTo"),      A_EXITVARDEST, AF_ODARK | AF_NOPROG | AF_WIZARD},
    {T("Fail"),        A_FAIL,     AF_ODARK | AF_NOPROG},
    {T("Filter"),      A_FILTER,   AF_ODARK | AF_NOPROG},
    {T("Forwardlist"), A_FORWARDLIST, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_CONST},
    {T("GetFromLock"), A_LGET,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Gfail"),       A_GFAIL,    AF_ODARK | AF_NOPROG},
    {T("GiveLock"),    A_LGIVE,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Idesc"),       A_IDESC,    AF_ODARK | AF_NOPROG},
    {T("Idle"),        A_IDLE,     AF_ODARK | AF_NOPROG},
    {T("IdleTimeout"), A_IDLETMOUT, AF_ODARK | AF_NOPROG},
    {T("Infilter"),    A_INFILTER, AF_ODARK | AF_NOPROG},
    {T("Inprefix"),    A_INPREFIX, AF_ODARK | AF_NOPROG},
    {T("Kill"),        A_KILL,     AF_ODARK | AF_NOPROG},
    {T("Lalias"),      A_LALIAS,   AF_ODARK | AF_NOPROG},
    {T("Last"),        A_LAST,     AF_WIZARD | AF_NOCMD | AF_NOPROG | AF_NOCLONE | AF_NODECOMP},
    {T("Lastpage"),    A_LASTPAGE, AF_INTERNAL | AF_NOCMD | AF_NOPROG | AF_GOD | AF_PRIVATE | AF_NODECOMP},
    {T("Lastsite"),    A_LASTSITE, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_GOD | AF_NODECOMP},
    {T("LastIP"),      A_LASTIP,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_GOD | AF_NODECOMP},
    {T("Leave"),       A_LEAVE,    AF_ODARK | AF_NOPROG},
    {T("LeaveLock"),   A_LLEAVE,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Lfail"),       A_LFAIL,    AF_ODARK | AF_NOPROG},
    {T("LinkLock"),    A_LLINK,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Listen"),      A_LISTEN,   AF_ODARK | AF_NOPROG},
    {T("Logindata"),   A_LOGINDATA, AF_MDARK | AF_NOPROG | AF_NOCMD | AF_CONST | AF_NODECOMP},
    {T("Mailcurf"),    A_MAILCURF, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE | AF_NODECOMP},
    {T("Mailflags"),   A_MAILFLAGS, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE | AF_NODECOMP},
    {T("Mailfolders"), A_MAILFOLDERS, AF_MDARK | AF_WIZARD | AF_NOPROG | AF_NOCLONE | AF_NODECOMP},
    {T("MailLock"),    A_LMAIL,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Mailmsg"),     A_MAILMSG,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL | AF_NODECOMP},
    {T("Mailsub"),     A_MAILSUB,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL | AF_NODECOMP},
    {T("Mailsucc"),    A_MAIL,     AF_ODARK | AF_NOPROG},
    {T("Mailto"),      A_MAILTO,   AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL | AF_NODECOMP},
    {T("Mfail"),       A_MFAIL,    AF_ODARK | AF_NOPROG},
    {T("Modified"),    A_MODIFIED, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_NOCLONE | AF_CONST | AF_NODECOMP},
    {T("Moniker"),     A_MONIKER,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_CONST},
    {T("Move"),        A_MOVE,     AF_ODARK | AF_NOPROG},
    {T("Name"),        A_NAME,     AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL | AF_NODECOMP},
    {T("NameFormat"),  A_NAMEFORMAT, AF_ODARK | AF_NOPROG | AF_WIZARD},
    {T("Newobjs"),     A_NEWOBJS,  AF_DARK | AF_NOCMD | AF_NOPROG | AF_NOCLONE | AF_INTERNAL | AF_NODECOMP},
    {T("Odesc"),       A_ODESC,    AF_ODARK | AF_NOPROG},
    {T("Odfail"),      A_ODFAIL,   AF_ODARK | AF_NOPROG},
    {T("Odrop"),       A_ODROP,    AF_ODARK | AF_NOPROG},
    {T("Oefail"),      A_OEFAIL,   AF_ODARK | AF_NOPROG},
    {T("Oenter"),      A_OENTER,   AF_ODARK | AF_NOPROG},
    {T("Ofail"),       A_OFAIL,    AF_ODARK | AF_NOPROG},
    {T("Ogfail"),      A_OGFAIL,   AF_ODARK | AF_NOPROG},
    {T("Okill"),       A_OKILL,    AF_ODARK | AF_NOPROG},
    {T("Oleave"),      A_OLEAVE,   AF_ODARK | AF_NOPROG},
    {T("Olfail"),      A_OLFAIL,   AF_ODARK | AF_NOPROG},
    {T("Omove"),       A_OMOVE,    AF_ODARK | AF_NOPROG},
    {T("Opay"),        A_OPAY,     AF_ODARK | AF_NOPROG},
    {T("OpenLock"),    A_LOPEN,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Orfail"),      A_ORFAIL,   AF_ODARK | AF_NOPROG},
    {T("Osucc"),       A_OSUCC,    AF_ODARK | AF_NOPROG},
    {T("Otfail"),      A_OTFAIL,   AF_ODARK | AF_NOPROG},
    {T("Otport"),      A_OTPORT,   AF_ODARK | AF_NOPROG},
    {T("Otofail"),     A_OTOFAIL,  AF_ODARK | AF_NOPROG},
    {T("Oufail"),      A_OUFAIL,   AF_ODARK | AF_NOPROG},
    {T("Ouse"),        A_OUSE,     AF_ODARK | AF_NOPROG},
    {T("Oxenter"),     A_OXENTER,  AF_ODARK | AF_NOPROG},
    {T("Oxleave"),     A_OXLEAVE,  AF_ODARK | AF_NOPROG},
    {T("Oxtport"),     A_OXTPORT,  AF_ODARK | AF_NOPROG},
    {T("PageLock"),    A_LPAGE,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("ParentLock"),  A_LPARENT,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Pay"),         A_PAY,      AF_ODARK | AF_NOPROG},
    {T("Prefix"),      A_PREFIX,   AF_ODARK | AF_NOPROG},
    {T("ProgCmd"),     A_PROGCMD,  AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL | AF_NODECOMP},
    {T("QueueMax"),    A_QUEUEMAX, AF_MDARK | AF_WIZARD | AF_NOPROG},
    {T("Quota"),       A_QUOTA,    AF_MDARK | AF_NOPROG | AF_GOD | AF_NOCMD | AF_NOCLONE | AF_NODECOMP},
    {T("ReceiveLock"), A_LRECEIVE, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Reject"),      A_REJECT,   AF_ODARK | AF_NOPROG},
    {T("Rfail"),       A_RFAIL,    AF_ODARK | AF_NOPROG},
    {T("Rquota"),      A_RQUOTA,   AF_MDARK | AF_NOPROG | AF_GOD | AF_NOCMD | AF_NOCLONE | AF_NODECOMP},
    {T("Runout"),      A_RUNOUT,   AF_ODARK | AF_NOPROG},
    {T("SayString"),   A_SAYSTRING, AF_ODARK | AF_NOPROG},
    {T("Semaphore"),   A_SEMAPHORE, AF_ODARK | AF_NOPROG | AF_WIZARD | AF_NOCMD | AF_NOCLONE | AF_NODECOMP},
    {T("Sex"),         A_SEX,      AF_VISUAL | AF_NOPROG},
    {T("Signature"),   A_SIGNATURE, AF_ODARK | AF_NOPROG},
    {T("SpeechMod"),   A_SPEECHMOD, AF_ODARK | AF_NOPROG},
    {T("SpeechLock"),  A_LSPEECH,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Startup"),     A_STARTUP,  AF_ODARK | AF_NOPROG},
    {T("Succ"),        A_SUCC,     AF_ODARK | AF_NOPROG},
    {T("TeloutLock"),  A_LTELOUT,  AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Tfail"),       A_TFAIL,    AF_ODARK | AF_NOPROG},
    {T("Timeout"),     A_TIMEOUT,  AF_MDARK | AF_NOPROG | AF_WIZARD},
    {T("Tport"),       A_TPORT,    AF_ODARK | AF_NOPROG},
    {T("TportLock"),   A_LTPORT,   AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("Tofail"),      A_TOFAIL,   AF_ODARK | AF_NOPROG},
    {T("Ufail"),       A_UFAIL,    AF_ODARK | AF_NOPROG},
    {T("Use"),         A_USE,      AF_ODARK | AF_NOPROG},
    {T("UseLock"),     A_LUSE,     AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("UserLock"),    A_LUSER,    AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("VisibleLock"), A_LVISIBLE, AF_ODARK | AF_NOPROG | AF_NOCMD | AF_IS_LOCK},
    {T("VA"),          A_VA,       AF_ODARK},
    {T("VB"),          A_VA + 1,   AF_ODARK},
    {T("VC"),          A_VA + 2,   AF_ODARK},
    {T("VD"),          A_VA + 3,   AF_ODARK},
    {T("VE"),          A_VA + 4,   AF_ODARK},
    {T("VF"),          A_VA + 5,   AF_ODARK},
    {T("VG"),          A_VA + 6,   AF_ODARK},
    {T("VH"),          A_VA + 7,   AF_ODARK},
    {T("VI"),          A_VA + 8,   AF_ODARK},
    {T("VJ"),          A_VA + 9,   AF_ODARK},
    {T("VK"),          A_VA + 10,  AF_ODARK},
    {T("VL"),          A_VA + 11,  AF_ODARK},
    {T("VM"),          A_VA + 12,  AF_ODARK},
    {T("VN"),          A_VA + 13,  AF_ODARK},
    {T("VO"),          A_VA + 14,  AF_ODARK},
    {T("VP"),          A_VA + 15,  AF_ODARK},
    {T("VQ"),          A_VA + 16,  AF_ODARK},
    {T("VR"),          A_VA + 17,  AF_ODARK},
    {T("VS"),          A_VA + 18,  AF_ODARK},
    {T("VT"),          A_VA + 19,  AF_ODARK},
    {T("VU"),          A_VA + 20,  AF_ODARK},
    {T("VV"),          A_VA + 21,  AF_ODARK},
    {T("VW"),          A_VA + 22,  AF_ODARK},
    {T("VX"),          A_VA + 23,  AF_ODARK},
    {T("VY"),          A_VA + 24,  AF_ODARK},
    {T("VZ"),          A_VA + 25,  AF_ODARK},
    {T("VRML_URL"),    A_VRML_URL, AF_ODARK | AF_NOPROG},
    {T("HTDesc"),      A_HTDESC,   AF_NOPROG},
    {T("Reason"),      A_REASON,   AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_GOD | AF_NODECOMP},
#ifdef GAME_DOOFERMUX
    {T("RegInfo"),     A_REGINFO,  AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_WIZARD | AF_NODECOMP},
#endif // GAME_DOOFERMUX
    {T("ConnInfo"),    A_CONNINFO, AF_PRIVATE | AF_MDARK | AF_NOPROG | AF_NOCMD | AF_GOD | AF_NODECOMP},
#ifdef REALITY_LVLS
    {T("Rlevel"),      A_RLEVEL,   AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
#endif // REALITY_LVLS
#if defined(FIRANMUX)
    {T("Color"),       A_COLOR,    AF_ODARK},
    {T("Alead"),       A_ALEAD,    AF_ODARK | AF_WIZARD},
    {T("Lead"),        A_LEAD,     AF_ODARK | AF_NOPROG | AF_WIZARD},
    {T("Olead"),       A_OLEAD,    AF_ODARK | AF_NOPROG | AF_WIZARD},
#endif // FIRANMUX
    {nullptr,               0,          0}
};

// The following 'special' attributes adopt invalid names to make them
// inaccessible to softcode.  A small price of this is that we must
// manually upper-case them in the table.
//
ATTR AttrTableSpecial[] =
{
    {T("*PASSWORD"),   A_PASS,     AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {T("*PRIVILEGES"), A_PRIVS,    AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {T("*MONEY"),      A_MONEY,    AF_DARK | AF_NOPROG | AF_NOCMD | AF_INTERNAL},
    {nullptr,               0,          0}
};

const UTF8 *aszSpecialDBRefNames[1-NOPERM] =
{
    T(""),
    T("*NOTHING*"),
    T("*AMBIGUOUS*"),
    T("*HOME*"),
    T("*NOPERMISSION*")
};

/* ---------------------------------------------------------------------------
 * fwdlist_set, fwdlist_clr: Manage cached forwarding lists
 */

void fwdlist_set(dbref thing, FWDLIST *ifp)
{
    // If fwdlist is null, clear.
    //
    if (  nullptr == ifp
       || ifp->count <= 0)
    {
        fwdlist_clr(thing);
        return;
    }

    // Copy input forwardlist to a correctly-sized buffer.
    //

    FWDLIST *fp = nullptr;
    try
    {
        fp = new FWDLIST;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != fp)
    {
        fp->data = nullptr;
        try
        {
            fp->data = new dbref[ifp->count];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr != fp->data)
        {
            for (int i = 0; i < ifp->count; i++)
            {
                fp->data[i] = ifp->data[i];
            }
            fp->count = ifp->count;

            // Replace an existing forwardlist, or add a new one.
            //
            bool bDone = false;
            FWDLIST *xfp = fwdlist_get(thing);
            if (xfp)
            {
                if (xfp->data)
                {
                    delete [] xfp->data;
                }
                delete xfp;
                xfp = nullptr;

                bDone = hashreplLEN(&thing, sizeof(thing), fp, &mudstate.fwdlist_htab);
            }
            else
            {
                bDone = hashaddLEN(&thing, sizeof(thing), fp, &mudstate.fwdlist_htab);
            }

            // If addition or replacement failed, don't leak new forward list.
            //
            if (!bDone)
            {
                if (fp->data)
                {
                    delete [] fp->data;
                }
                delete fp;
            }
        }
        else
        {
            ISOUTOFMEMORY(fp->data);
        }
    }
    else
    {
        ISOUTOFMEMORY(fp);
    }
}

void fwdlist_clr(dbref thing)
{
    // If a forwardlist exists, delete it
    //
    FWDLIST *xfp = fwdlist_get(thing);
    if (xfp)
    {
        if (xfp->data)
        {
            delete [] xfp->data;
        }
        delete xfp;
        xfp = nullptr;

        hashdeleteLEN(&thing, sizeof(thing), &mudstate.fwdlist_htab);
    }
}

/* ---------------------------------------------------------------------------
 * fwdlist_load: Load text into a forwardlist.
 */

FWDLIST *fwdlist_load(dbref player, UTF8 *atext)
{
    FWDLIST *fp = nullptr;
    try
    {
        fp = new FWDLIST;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != fp)
    {
        fp->count = 0;
        fp->data = nullptr;
        try
        {
            fp->data = new dbref[LBUF_SIZE/2];
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr != fp->data)
        {
            UTF8 *tp = alloc_lbuf("fwdlist_load.str");
            UTF8 *bp = tp;
            mux_strncpy(tp, atext, LBUF_SIZE-1);

            int count = 0;

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
                UTF8 *dp;
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
                    bool fail;
                    dbref target = mux_atol(dp);
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
                                tprintf(T("Cannot forward to #%d: Permission denied."),
                                target));
                        }
                    }
                    else
                    {
                        fp->data[count++] = target;
                    }
                }
            } while (*bp);

            free_lbuf(tp);

            if (0 < count)
            {
                fp->count = count;
            }
            else
            {
                delete [] fp->data;
                delete fp;
                fp = nullptr;
            }
        }
        else
        {
            delete fp;
            fp = nullptr;
        }
    }
    return fp;
}

/* ---------------------------------------------------------------------------
 * fwdlist_rewrite: Generate a text string from a FWDLIST buffer.
 */

int fwdlist_rewrite(FWDLIST *fp, UTF8 *atext)
{
    int count = 0;
    atext[0] = '\0';

    if (  fp
       && fp->count)
    {
        UTF8 *bp = atext;
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
bool fwdlist_ck(dbref player, dbref thing, int anum, UTF8 *atext)
{
    UNUSED_PARAMETER(anum);

    if (mudstate.bStandAlone)
    {
        return true;
    }

    FWDLIST *fp = nullptr;
    if (  nullptr != atext
       && '\0' != atext[0])
    {
        fp = fwdlist_load(player, atext);
    }

    // Set the cached forwardlist.
    //
    fwdlist_set(thing, fp);
    int count = fwdlist_rewrite(fp, atext);

    if (nullptr != fp)
    {
        if (nullptr != fp->data)
        {
            delete [] fp->data;
        }
        delete fp;
        fp = nullptr;
    }

    return (  count > 0
           || nullptr == atext
           || '\0' == atext[0]);
}

FWDLIST *fwdlist_get(dbref thing)
{
    static FWDLIST *fp = nullptr;
    if (mudstate.bStandAlone)
    {
        dbref aowner;
        int   aflags;
        UTF8 *tp = atr_get("fwdlist_get.543", thing, A_FORWARDLIST, &aowner, &aflags);
        fp = fwdlist_load(GOD, tp);
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
const UTF8 *Name(dbref thing)
{
    if (thing < 0)
    {
        return aszSpecialDBRefNames[-thing];
    }

    dbref aowner;
    int aflags;
#ifdef MEMORY_BASED
    static UTF8 tbuff[LBUF_SIZE];
    atr_get_str(tbuff, thing, A_NAME, &aowner, &aflags);
    return tbuff;
#else // MEMORY_BASED
    if (!db[thing].name)
    {
        size_t len;
        UTF8 *pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &len);
        db[thing].name = StringCloneLen(pName, len);
        free_lbuf(pName);
    }
    return db[thing].name;
#endif // MEMORY_BASED
}

const UTF8 *PureName(dbref thing)
{
    if (thing < 0)
    {
        return aszSpecialDBRefNames[-thing];
    }

    dbref aowner;
    int aflags;

    UTF8 *pName, *pPureName;
    if (mudconf.cache_names)
    {
        if (!db[thing].purename)
        {
            size_t nName;
            size_t nPureName;
#ifdef MEMORY_BASED
            pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
            pPureName = strip_color(pName, &nPureName);
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
                nName = strlen((char *)db[thing].name);
            }
            pName = db[thing].name;
            pPureName = strip_color(pName, &nPureName);
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
    pName = atr_get("PureName.631", thing, A_NAME, &aowner, &aflags);
    pPureName = strip_color(pName);
    free_lbuf(pName);
    return pPureName;
}

const UTF8 *Moniker(dbref thing)
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
    const UTF8 *pPureName = (UTF8 *)ConvertToAscii(PureName(thing));
    UTF8 *pPureNameCopy = StringClone(pPureName);

    size_t nMoniker;
    dbref  aowner;
    int    aflags;
    UTF8 *pMoniker = atr_get_LEN(thing, A_MONIKER, &aowner, &aflags,
        &nMoniker);
    const UTF8 *pPureMoniker = (UTF8 *)ConvertToAscii(strip_color(pMoniker));

    const UTF8 *pReturn = nullptr;
    static UTF8 tbuff[LBUF_SIZE];
    if (strcmp((char *)pPureNameCopy, (char *)pPureMoniker) == 0)
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
            if (strcmp((char *)pMoniker, (char *)Name(thing)) == 0)
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

void free_Names(OBJ *p)
{
#ifndef MEMORY_BASED
    if (p->name)
    {
        if (mudconf.cache_names)
        {
            if (p->name == p->purename)
            {
                p->purename = nullptr;
            }
            if (p->name == p->moniker)
            {
                p->moniker = nullptr;
            }
        }
        MEMFREE(p->name);
        p->name = nullptr;
    }
#endif // !MEMORY_BASED

    if (mudconf.cache_names)
    {
        if (p->purename)
        {
            MEMFREE(p->purename);
            p->purename = nullptr;
        }
        if (p->moniker)
        {
            MEMFREE(p->moniker);
            p->moniker = nullptr;
        }
    }
}


void s_Name(dbref thing, const UTF8 *s)
{
    free_Names(&db[thing]);
    atr_add_raw(thing, A_NAME, s);
#ifndef MEMORY_BASED
    if (nullptr != s)
    {
        db[thing].name = StringClone(s);
    }
#endif // !MEMORY_BASED
}

void free_Moniker(OBJ *p)
{
    if (mudconf.cache_names)
    {
#ifndef MEMORY_BASED
        if (p->name == p->moniker)
        {
            p->moniker = nullptr;
        }
#endif // !MEMORY_BASED
        if (p->moniker)
        {
            MEMFREE(p->moniker);
            p->moniker = nullptr;
        }
    }
}

void s_Moniker(dbref thing, const UTF8 *s)
{
    free_Moniker(&db[thing]);
    atr_add_raw(thing, A_MONIKER, s);
}

void s_Pass(dbref thing, const UTF8 *s)
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
    int   eval,
    int   key,
    int   nargs,
    UTF8 *aname,
    UTF8 *value,
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

    // Look up the user-named attribute we want to play with.
    //
    size_t nName;
    bool bValid = false;
    ATTR *va = nullptr;
    UTF8 *pName = MakeCanonicalAttributeName(aname, &nName, &bValid);
    if (bValid)
    {
        va = (ATTR *)vattr_find_LEN(pName, nName);
    }

    if (nullptr == va)
    {
        notify(executor, T("No such user-named attribute."));
        return;
    }

    int f;
    UTF8 *sp;
    ATTR *va2;
    bool negate, success;
    MUX_STRTOK_STATE tts;
    size_t nCased;
    UTF8 *pCased;

    switch (key)
    {
    case ATTRIB_ACCESS:

        // Modify access to user-named attribute
        //
        pCased = mux_strupr(value, nCased);

        mux_strtok_src(&tts, pCased);
        mux_strtok_ctl(&tts, T(" "));
        sp = mux_strtok_parse(&tts);
        success = false;
        while (sp != nullptr)
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
                notify(executor, tprintf(T("Unknown permission: %s."), sp));
            }

            // Get the next token.
            //
            sp = mux_strtok_parse(&tts);
        }

        if (success && !Quiet(executor))
        {
            notify(executor, T("Attribute access changed."));
        }
        break;

    case ATTRIB_RENAME:

        {
            // Save the old name for use later.
            //
            UTF8 OldName[SBUF_SIZE];
            UTF8 *pOldName = OldName;
            safe_sb_str(pName, OldName, &pOldName);
            *pOldName = '\0';
            size_t nOldName = pOldName - OldName;

            // Make sure the new name doesn't already exist. This checks
            // the built-in and user-defined data structures.
            //
            va2 = atr_str(value);
            if (va2)
            {
                notify(executor, T("An attribute with that name already exists."));
                return;
            }
            pName = MakeCanonicalAttributeName(value, &nName, &bValid);
            if (  !bValid
               || vattr_rename_LEN(OldName, nOldName, pName, nName) == nullptr)
            {
                notify(executor, T("Attribute rename failed."));
            }
            else
            {
                notify(executor, T("Attribute renamed."));
            }
        }
        break;

    case ATTRIB_DELETE:

        // Remove the attribute.
        //
        vattr_delete_LEN(pName, nName);
        notify(executor, T("Attribute deleted."));
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
    int   eval,
    int   key,
    int   nargs,
    UTF8 *arg1,
    UTF8 *arg2,
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

    UTF8 *pValidName;
    switch (key)
    {
    case FIXDB_OWNER:

        s_Owner(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Owner set to #%d"), res));
        break;

    case FIXDB_LOC:

        s_Location(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Location set to #%d"), res));
        break;

    case FIXDB_CON:

        s_Contents(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Contents set to #%d"), res));
        break;

    case FIXDB_EXITS:

        s_Exits(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Exits set to #%d"), res));
        break;

    case FIXDB_NEXT:

        s_Next(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Next set to #%d"), res));
        break;

    case FIXDB_PENNIES:

        s_Pennies(thing, res);
        if (!Quiet(executor))
            notify(executor, tprintf(T("Pennies set to %d"), res));
        break;

    case FIXDB_NAME:

        if (isPlayer(thing))
        {
            if (!ValidatePlayerName(arg2))
            {
                notify(executor, T("That\xE2\x80\x99s not a good name for a player."));
                return;
            }

            bool bAlias = false;
            pValidName = arg2;
            if (  lookup_player_name(pValidName, bAlias) != NOTHING
               || bAlias)
            {
                notify(executor, T("That name is already in use or is an alias."));
                return;
            }

            STARTLOG(LOG_SECURITY, "SEC", "CNAME");
            log_name(thing),
            log_text(T(" renamed to "));
            log_text(pValidName);
            ENDLOG;

            if (Suspect(executor))
            {
                raw_broadcast(WIZARD, T("[Suspect] %s renamed to %s"),
                    Name(thing), pValidName);
            }

            delete_player_name(thing, Name(thing), false);
            s_Name(thing, pValidName);
            add_player_name(thing, pValidName, false);
        }
        else
        {
            size_t nTmp;
            bool bValid;
            pValidName = MakeCanonicalObjectName(arg2, &nTmp, &bValid, 0);
            if (!bValid)
            {
                notify(executor, T("That is not a reasonable name."));
                return;
            }
            s_Name(thing, pValidName);
        }
        if (!Quiet(executor))
        {
            notify(executor, tprintf(T("Name set to %s"), pValidName));
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
UTF8 *MakeCanonicalAttributeName(const UTF8 *pName_arg, size_t *pnName, bool *pbValid)
{
    static UTF8 Buffer[SBUF_SIZE];
    const UTF8 *pName = pName_arg;

    if (  nullptr == pName
       || !mux_isattrnameinitial(pName))
    {
        *pnName = 0;
        *pbValid = false;
        return nullptr;
    }
    size_t nLeft = SBUF_SIZE-1;
    UTF8 *p = Buffer;
    size_t n;
    while (  '\0' != *pName
          && (n = utf8_FirstByte[(unsigned char)*pName]) < UTF8_CONTINUE
          && n <= nLeft)
    {
        if (!mux_isattrname(pName))
        {
            *pnName = 0;
            *pbValid = false;
            return Buffer;
        }

        nLeft -= n;
        bool bXor;
        const string_desc *qDesc = mux_toupper(pName, bXor);
        if (nullptr == qDesc)
        {
            while (n--)
            {
                *p++ = *pName++;
            }
        }
        else
        {
            size_t m = qDesc->n_bytes;
            const UTF8 *q = qDesc->p;
            if (bXor)
            {
                while (m--)
                {
                    *p++ = *pName++ ^ *q++;
                }
            }
            else
            {
                while (m--)
                {
                    *p++ = *q++;
                }
            }
        }
    }
    *p = '\0';

    // Continue to validate remaining characters even though
    // we aren't going to use them. This helps to ensure that
    // softcode will run in the future if we increase the
    // size of SBUF_SIZE.
    //
    while ('\0' != *pName)
    {
        if (  UTF8_CONTINUE <= utf8_FirstByte[(unsigned char)*pName]
           || !mux_isattrname(pName))
        {
            *pnName = 0;
            *pbValid = false;
            return Buffer;
        }
        pName = utf8_NextCodePoint(pName);
    }

    if (IsRestricted(pName, mudconf.attr_name_charset))
    {
        *pnName = 0;
        *pbValid = false;
        return Buffer;
    }

    // Length of truncated result.
    //
    *pnName = p - Buffer;
    *pbValid = true;
    return Buffer;
}

// MakeCanonicalAttributeCommand
//
UTF8 *MakeCanonicalAttributeCommand(const UTF8 *pName, size_t *pnName, bool *pbValid)
{
    if (nullptr == pName)
    {
        *pnName = 0;
        *pbValid = false;
        return nullptr;
    }

    static UTF8 Buffer[SBUF_SIZE];
    size_t nLeft = SBUF_SIZE-2;
    UTF8 *p = Buffer;
    size_t n;

    *p++ = '@';
    while (  '\0' != *pName
          && (n = utf8_FirstByte[(unsigned char)*pName]) < UTF8_CONTINUE
          && n <= nLeft)
    {
        nLeft -= n;
        bool bXor;
        const string_desc *qDesc = mux_tolower(pName, bXor);
        if (nullptr == qDesc)
        {
            while (n--)
            {
                *p++ = *pName++;
            }
        }
        else
        {
            size_t m = qDesc->n_bytes;
            const UTF8 *q = qDesc->p;
            if (bXor)
            {
                while (m--)
                {
                    *p++ = *pName++ ^ *q++;
                }
            }
            else
            {
                while (m--)
                {
                    *p++ = *q++;
                }
            }
        }
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
    for (a = AttrTable; a->number; a++)
    {
        size_t nLen;
        bool bValid;
        UTF8 *buff = MakeCanonicalAttributeName(a->name, &nLen, &bValid);
        if (!bValid)
        {
            continue;
        }
        anum_extend(a->number);
        anum_set(a->number, a);
        hashaddLEN(buff, nLen, a, &mudstate.attr_name_htab);
    }

    // We specifically allow the '*' character at server
    // initialization because it's part of the A_PASS attribute
    // name.
    //
    for (a = AttrTableSpecial; a->number; a++)
    {
        anum_extend(a->number);
        anum_set(a->number, a);
        hashaddLEN((char *)a->name, strlen((char *)a->name), a, &mudstate.attr_name_htab);
    }
}

/* ---------------------------------------------------------------------------
 * atr_str: Look up an attribute by name.
 */

ATTR *atr_str(const UTF8 *s)
{
    // Make attribute name canonical.
    //
    size_t nBuffer;
    bool bValid;
    UTF8 *buff = MakeCanonicalAttributeName(s, &nBuffer, &bValid);
    if (!bValid)
    {
        return nullptr;
    }

    // Look for a predefined attribute.
    //
    ATTR *a = (ATTR *)hashfindLEN(buff, nBuffer, &mudstate.attr_name_htab);
    if (a != nullptr)
    {
        return a;
    }

    // Nope, look for a user attribute.
    //
    a = vattr_find_LEN(buff, nBuffer);
    return a;
}

int GrowFiftyPercent(int x, int low, int high)
{
    if (x < 0)
    {
        x = 0;
    }

    // Calcuate 150% of x clipping goal at INT_MAX.
    //
    int half = x >> 1;
    int goal;
    if (INT_MAX - half <= x)
    {
        goal = INT_MAX;
    }
    else
    {
        goal = x + half;
    }

    // Clip result between requested boundaries.
    //
    if (goal < low)
    {
        goal = low;
    }
    else if (high < goal)
    {
        goal = high;
    }
    return goal;
}

/* ---------------------------------------------------------------------------
 * anum_extend: Grow the attr num lookup table.
 */

ATTR **anum_table = nullptr;
int anum_alc_top = -1;

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

    int h = GrowFiftyPercent(anum_alc_top, delta, INT_MAX);

    if (newtop < h)
    {
        newtop = h;
    }

    int i;
    ATTR **anum_table2 = (ATTR **) MEMALLOC((newtop + 1) * sizeof(ATTR *));
    if (nullptr != anum_table2)
    {
        for (i = anum_alc_top + 1; i <= newtop; i++)
        {
            anum_table2[i] = nullptr;
        }

        if (nullptr != anum_table)
        {
            for (i = 0; i <= anum_alc_top; i++)
            {
                anum_table2[i] = anum_table[i];
            }

            MEMFREE(anum_table);
        }
        anum_table = anum_table2;
    }
    else
    {
        ISOUTOFMEMORY(anum_table2);
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
        return nullptr;
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
    s_ThRefs(executor, mudconf.references_per_hour);
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

bool ThrottleReferences(dbref executor)
{
    if (0 < ThRefs(executor))
    {
        s_ThRefs(executor, ThRefs(executor)-1);
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

int mkattr(dbref executor, const UTF8 *buff)
{
    ATTR *ap = atr_str(buff);
    if (!ap)
    {
        // Unknown attribute name, create a new one.
        //
        size_t nName;
        bool bValid;
        UTF8 *pName = MakeCanonicalAttributeName(buff, &nName, &bValid);
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

#ifndef MEMORY_BASED

/* ---------------------------------------------------------------------------
 * al_decode: Fetch an attribute number from an alist.
 */

static int al_decode(unsigned char **app)
{
    unsigned char *ap = *app;

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
static unsigned char *al_code(unsigned char *ap, unsigned int atrnum)
{
    int i;
    unsigned int bits;
    for (i = 0; i < ATR_BUF_INCR - 1; i++)
    {
        bits = atrnum & 0x7F;
        if (atrnum <= 0x7F)
        {
            ap[i] = (unsigned char)bits;
            break;
        }
        atrnum >>= 7;
        ap[i] = (unsigned char)(bits | 0x80);
    }
    return ap + i + 1;
}

#endif // MEMORY_BASED

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

    atr_push();
    UTF8 *buff = alloc_lbuf("Commer");
    unsigned char *as;
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
        UTF8 *s = (UTF8 *)strchr((char *)buff+1, ':');
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
static void al_extend(unsigned char **buffer, size_t *bufsiz, size_t len, bool copy)
{
    if (len > *bufsiz)
    {
        size_t newsize = len + ATR_BUF_CHUNK;
        unsigned char *tbuff = (unsigned char *)MEMALLOC(newsize);
        ISOUTOFMEMORY(tbuff);
        if (*buffer)
        {
            if (copy)
            {
                memcpy(tbuff, *buffer, *bufsiz);
            }
            MEMFREE(*buffer);
            *buffer = nullptr;
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
static unsigned char *al_fetch(dbref thing)
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
    const unsigned char *astr = atr_get_raw_LEN(thing, A_LIST, &len);
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
    unsigned char *abuf = al_fetch(thing);
    unsigned char *cp = abuf;
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
    unsigned char *abuf, *cp, *dp;

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

static inline void makekey(dbref thing, int atr, Aname *abuff)
{
    abuff->object = thing;
    abuff->attrnum = atr;
    return;
}
#endif // !MEMORY_BASED

/* ---------------------------------------------------------------------------
 * atr_encode: Encode an attribute string.
 */

static const UTF8 *atr_encode(const UTF8 *iattr, dbref thing, dbref owner, int flags, int atr)
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
    return tprintf(T("%c%d:%d:%s"), ATR_INFO_CHAR, owner, flags, iattr);
}

// atr_decode_flags_owner: Decode the owner and flags (if present) and
// return a pointer to the attribute text.
//
const UTF8 *atr_decode_flags_owner(const UTF8 *iattr, dbref *owner, int *flags)
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
    const UTF8 *cp = iattr + 1;

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
static void atr_decode_LEN(const UTF8 *iattr, size_t nLen, UTF8 *oattr,
                           dbref thing, dbref *owner, int *flags, size_t *pLen)
{
    // Default the owner
    //
    *owner = Owner(thing);

    // Parse for owner and flags
    //
    const UTF8 *cp = atr_decode_flags_owner(iattr, owner, flags);

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

    if (  !db[thing].nALUsed
       || !db[thing].pALHead)
    {
        return;
    }

    mux_assert(0 <= db[thing].nALUsed);

    // Binary search for the attribute.
    //
    int lo = 0;
    int mid;
    int hi = db[thing].nALUsed - 1;
    ATRLIST *list = db[thing].pALHead;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (list[mid].number > atr)
        {
            hi = mid - 1;
        }
        else if (list[mid].number < atr)
        {
            lo = mid + 1;
        }
        else // (list[mid].number == atr)
        {
            MEMFREE(list[mid].data);
            list[mid].data = nullptr;
            db[thing].nALUsed--;
            if (mid != db[thing].nALUsed)
            {
                memmove( list + mid,
                         list + mid + 1,
                         (db[thing].nALUsed - mid) * sizeof(ATRLIST));
            }
            break;
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

void atr_add_raw_LEN(dbref thing, int atr, const UTF8 *szValue, size_t nValue)
{
    if (  !szValue
       || '\0' == szValue[0])
    {
        atr_clr(thing, atr);
        return;
    }

#ifdef MEMORY_BASED
    ATRLIST *list = db[thing].pALHead;
    UTF8 *text = StringCloneLen(szValue, nValue);

    if (!list)
    {
        db[thing].nALAlloc = INITIAL_ATRLIST_SIZE;
        list = (ATRLIST *)MEMALLOC(db[thing].nALAlloc*sizeof(ATRLIST));
        ISOUTOFMEMORY(list);
        db[thing].pALHead  = list;
        db[thing].nALUsed  = 1;
        list[0].number = atr;
        list[0].data = text;
        list[0].size = nValue + 1;
    }
    else
    {
        // If this atr is newly allocated, or it comes from the flatfile, it
        // will experience worst-case performance with a binary search, so we
        // perform a quick check to see if it goes on the end.
        //
        int lo;
        int hi = db[thing].nALUsed - 1;
        if (list[hi].number < atr)
        {
            // Attribute should be appended to the end of the list.
            //
            lo = hi + 1;
        }
        else
        {
            // Binary search for the attribute
            //
            lo = 0;
            while (lo <= hi)
            {
                int mid = ((hi - lo) >> 1) + lo;
                if (list[mid].number > atr)
                {
                    hi = mid - 1;
                }
                else if (list[mid].number < atr)
                {
                    lo = mid + 1;
                }
                else // if (list[mid].number == atr)
                {
                    MEMFREE(list[mid].data);
                    list[mid].data = text;
                    list[mid].size = nValue + 1;
                    goto FoundAttribute;
                }
            }
        }

        // We didn't find it, and lo == hi + 1.  The attribute should be
        // inserted between (0,hi) and (lo,nALUsed-1) where hi may be -1
        // and lo may be nALUsed.
        //
        if (db[thing].nALUsed < db[thing].nALAlloc)
        {
            if (lo < db[thing].nALUsed)
            {
                memmove( list + lo + 1,
                         list + lo,
                         (db[thing].nALUsed - lo) * sizeof(ATRLIST));
            }
        }
        else
        {
            // Double the size of the list.
            //
            db[thing].nALAlloc = GrowFiftyPercent(db[thing].nALAlloc,
                INITIAL_ATRLIST_SIZE, INT_MAX);
            list = (ATRLIST *)MEMALLOC(db[thing].nALAlloc
                 * sizeof(ATRLIST));
            ISOUTOFMEMORY(list);

            // Copy bottom part.
            //
            if (lo > 0)
            {
                memcpy(list, db[thing].pALHead, lo * sizeof(ATRLIST));
            }

            // Copy top part.
            //
            if (lo < db[thing].nALUsed)
            {
                memcpy( list + lo + 1,
                        db[thing].pALHead + lo,
                        (db[thing].nALUsed - lo) * sizeof(ATRLIST));
            }
            MEMFREE(db[thing].pALHead);
            db[thing].pALHead = list;
        }
        db[thing].nALUsed++;
        list[lo].data = text;
        list[lo].number = atr;
        list[lo].size = nValue + 1;
    }

FoundAttribute:

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

void atr_add_raw(dbref thing, int atr, const UTF8 *szValue)
{
    atr_add_raw_LEN(thing, atr, szValue, szValue ? strlen((char *)szValue) : 0);
}

void atr_add(dbref thing, int atr, const UTF8 *buff, dbref owner, int flags)
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

        const UTF8 *tbuff = atr_encode(buff, thing, owner, flags, atr);
        atr_add_raw(thing, atr, tbuff);
    }
}

void atr_set_flags(dbref thing, int atr, dbref flags)
{
    dbref aowner;
    int aflags;
    UTF8 *buff = atr_get("atr_set_flags.2212", thing, atr, &aowner, &aflags);
    atr_add(thing, atr, buff, aowner, flags);
    free_lbuf(buff);
}

/* ---------------------------------------------------------------------------
 * get_atr,atr_get_raw, atr_get_str, atr_get: Get an attribute from the database.
 */

int get_atr(const UTF8 *name)
{
    ATTR *ap = atr_str(name);

    if (!ap)
        return 0;
    if (!(ap->number))
        return -1;
    return ap->number;
}

#ifdef MEMORY_BASED
const UTF8 *atr_get_raw_LEN(dbref thing, int atr, size_t *pLen)
{
    if (!Good_obj(thing))
    {
        return nullptr;
    }

    // Binary search for the attribute.
    //
    ATRLIST *list = db[thing].pALHead;
    if (!list)
    {
        return nullptr;
    }

    int lo = 0;
    int hi = db[thing].nALUsed - 1;
    int mid;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (list[mid].number > atr)
        {
            hi = mid - 1;
        }
        else if (list[mid].number < atr)
        {
            lo = mid + 1;
        }
        else // if (list[mid].number == atr)
        {
            *pLen = list[mid].size - 1;
            return list[mid].data;
        }
    }
    *pLen = 0;
    return nullptr;
}

#else // MEMORY_BASED

const UTF8 *atr_get_raw_LEN(dbref thing, int atr, size_t *pLen)
{
    Aname okey;

    makekey(thing, atr, &okey);
    size_t nLen;
    const UTF8 *a = cache_get(&okey, &nLen);
    nLen = a ? (nLen-1) : 0;
    *pLen = nLen;
    return a;
}
#endif // MEMORY_BASED

const UTF8 *atr_get_raw(dbref thing, int atr)
{
    size_t Len;
    return atr_get_raw_LEN(thing, atr, &Len);
}

UTF8 *atr_get_str_LEN(UTF8 *s, dbref thing, int atr, dbref *owner, int *flags,
    size_t *pLen)
{
    const UTF8 *buff = atr_get_raw_LEN(thing, atr, pLen);
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

UTF8 *atr_get_str(UTF8 *s, dbref thing, int atr, dbref *owner, int *flags)
{
    size_t nLen;
    return atr_get_str_LEN(s, thing, atr, owner, flags, &nLen);
}

UTF8 *atr_get_LEN(dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
{
    UTF8 *buff = alloc_lbuf("atr_get_LEN");
    return atr_get_str_LEN(buff, thing, atr, owner, flags, pLen);
}

UTF8 *atr_get_real(const UTF8 *tag, dbref thing, int atr, dbref *owner, int *flags,
    const UTF8 *file, const int line)
{
    size_t nLen;
    UTF8 *buff = pool_alloc_lbuf(tag, file, line);
    return atr_get_str_LEN(buff, thing, atr, owner, flags, &nLen);
}

bool atr_get_info(dbref thing, int atr, dbref *owner, int *flags)
{
    size_t nLen;
    const UTF8 *buff = atr_get_raw_LEN(thing, atr, &nLen);
    if (!buff)
    {
        *owner = Owner(thing);
        *flags = 0;
        return false;
    }
    atr_decode_LEN(buff, nLen, nullptr, thing, owner, flags, &nLen);
    return true;
}

UTF8 *atr_pget_str_LEN(UTF8 *s, dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
{
    dbref parent;
    int lev;
    ATTR *ap;
    const UTF8 *buff;

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

UTF8 *atr_pget_str(UTF8 *s, dbref thing, int atr, dbref *owner, int *flags)
{
    size_t nLen;
    return atr_pget_str_LEN(s, thing, atr, owner, flags, &nLen);
}

UTF8 *atr_pget_LEN(dbref thing, int atr, dbref *owner, int *flags, size_t *pLen)
{
    UTF8 *buff = alloc_lbuf("atr_pget");
    return atr_pget_str_LEN(buff, thing, atr, owner, flags, pLen);
}

UTF8 *atr_pget_real(dbref thing, int atr, dbref *owner, int *flags,
    const UTF8 *file, const int line)
{
    size_t nLen;
    UTF8 *buff = pool_alloc_lbuf(T("atr_pget"), file, line);
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
        const UTF8 *buff = atr_get_raw_LEN(parent, atr, &nLen);
        if (buff && *buff)
        {
            atr_decode_LEN(buff, nLen, nullptr, thing, owner, flags, &nLen);
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
    if (db[thing].pALHead)
    {
        MEMFREE(db[thing].pALHead);
    }
    db[thing].pALHead  = nullptr;
    db[thing].nALAlloc = 0;
    db[thing].nALUsed  = 0;
#else // MEMORY_BASED
    atr_push();
    unsigned char *as;
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

    atr_push();
    unsigned char *as;
    for (int atr = atr_head(source, &as); atr; atr = atr_next(&as))
    {
        int   aflags;
        dbref aowner;
        UTF8 *buf = atr_get("atr_cpy.2480", source, atr, &aowner, &aflags);

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

    atr_push();
    unsigned char *as;
    for (int atr = atr_head(obj, &as); atr; atr = atr_next(&as))
    {
        int   aflags;
        dbref aowner;
        UTF8 *buf = atr_get("atr_chown.2529", obj, atr, &aowner, &aflags);
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

int atr_next(UTF8 **attrp)
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
        if (atr->count >= db[atr->thing].nALUsed)
        {
            MEMFREE(atr);
            atr = nullptr;
            return 0;
        }
        atr->count++;
        return db[atr->thing].pALHead[atr->count - 1].number;
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

    mudstate.iter_alist.data = nullptr;
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
        mudstate.iter_alist.data = nullptr;
    }
    if (old_alist)
    {
        mudstate.iter_alist.data = old_alist->data;
        mudstate.iter_alist.len = old_alist->len;
        mudstate.iter_alist.next = old_alist->next;
        unsigned char *cp = (unsigned char *)old_alist;
        free_sbuf(cp);
    }
    else
    {
        mudstate.iter_alist.data = nullptr;
        mudstate.iter_alist.len = 0;
        mudstate.iter_alist.next = nullptr;
    }
#endif // !MEMORY_BASED
}

/* ---------------------------------------------------------------------------
 * atr_head: Returns the head of the attr list for object 'thing'
 */

int atr_head(dbref thing, unsigned char **attrp)
{
#ifdef MEMORY_BASED
    if (db[thing].nALUsed)
    {
        ATRCOUNT *atr = (ATRCOUNT *) MEMALLOC(sizeof(ATRCOUNT));
        ISOUTOFMEMORY(atr);
        atr->thing = thing;
        atr->count = 1;
        *attrp = (unsigned char *)atr;
        return db[thing].pALHead[0].number;
    }
    return 0;
#else // MEMORY_BASED
    const unsigned char *astr;
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

attr_info::attr_info(void)
{
    m_object    = NOTHING;
    m_attr      = nullptr;
    m_bValid    = false;
    m_aowner    = NOTHING;
    m_aflags    = 0;
    m_bHaveInfo = false;
}

attr_info::attr_info(dbref object, ATTR *attr)
{
    m_aowner    = NOTHING;
    m_aflags    = 0;
    m_bHaveInfo = false;

    m_object    = object;
    m_attr      = attr;
    m_bValid    = (Good_obj(object) && (nullptr != attr));
}

attr_info::attr_info(dbref executor, const UTF8 *pTarget, bool bCreate, bool bDefaultMe)
{
    m_object    = NOTHING;
    m_attr      = nullptr;
    m_bValid    = false;
    m_aowner    = NOTHING;
    m_aflags    = 0;
    m_bHaveInfo = false;

    if (isEmpty(pTarget))
    {
        return;
    }
    const UTF8 *pAttrName = nullptr;
    bool bHaveObject = parse_thing_slash(executor, pTarget, &pAttrName, &m_object);
    if (!bHaveObject)
    {
        if (bDefaultMe)
        {
            m_object = executor;
            pAttrName = pTarget;
        }
        else
        {
            m_object = match_thing(executor, pTarget);
            return;
        }
    }
    if (!isEmpty(pAttrName))
    {
        if (bCreate)
        {
            m_attr = atr_num(mkattr(executor, pAttrName));
        }
        else
        {
            m_attr = atr_str(pAttrName);
        }
        m_bValid = (m_attr != nullptr);
    }
}

bool attr_info::get_info(bool bParent)
{
    if (!m_bValid)
    {
        return false;
    }

    bool bHasAttr = false;
    if (bParent)
    {
        bHasAttr = atr_pget_info(m_object, m_attr->number, &m_aowner, &m_aflags);
    }
    else
    {
        bHasAttr = atr_get_info(m_object, m_attr->number, &m_aowner, &m_aflags);
    }
    m_bHaveInfo = true;
    return bHasAttr;
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
        s_Stack(thing, nullptr);
#endif // DEPRECATED
        db[thing].cpu_time_used.Set100ns(0);
        db[thing].tThrottleExpired.Set100ns(0);
        s_ThAttrib(thing, 0);
        s_ThMail(thing, 0);
        s_ThRefs(thing, 0);

#ifdef MEMORY_BASED
        db[thing].pALHead  = nullptr;
        db[thing].nALAlloc = 0;
        db[thing].nALUsed  = 0;
#else
        db[thing].name = nullptr;
#endif // MEMORY_BASED
        db[thing].purename = nullptr;
        db[thing].moniker = nullptr;
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
    newdb = nullptr;

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
#ifdef SELFCHECK
    delete_all_player_names();
    for (dbref thing = 0; thing < mudstate.db_top; thing++)
    {
        free_Names(&db[thing]);
    }
#endif

    if (db != nullptr)
    {
        db -= SIZE_HACK;
        char *cp = (char *)db;
        MEMFREE(cp);
        cp = nullptr;
        db = nullptr;
    }
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.freelist = NOTHING;
}

void db_make_minimal(void)
{
    db_free();
    db_grow(1);
    s_Name(0, T("Limbo"));
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
    const UTF8 *pmsg;
    dbref obj = create_player(T("Wizard"), T("potrzebie"), NOTHING, false, &pmsg);
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

dbref parse_dbref(const UTF8 *s)
{
    // Skip leading spaces.
    //
    while (mux_isspace(*s))
    {
        s++;
    }

    const UTF8 *p = s;
    if (mux_isdigit(*p))
    {
        // Parse numeric portion.
        //
        p++;
        while (mux_isdigit(*p))
        {
            p++;
        }

        if (  '\0' == *p
           || mux_isspace(*p))
        {
            // Parse trailing spaces.
            //
            while (mux_isspace(*p))
            {
                p++;
            }

            if ('\0' == *p)
            {
                int x = mux_atol(s);
                return ((x >= 0) ? x : NOTHING);
            }
        }
    }
    return NOTHING;
}


void putref(FILE *f, dbref ref)
{
    UTF8 buf[I32BUF_SIZE+1];
    size_t n = mux_ltoa(ref, buf);
    buf[n] = '\n';
    fwrite(buf, sizeof(char), n+1, f);
}

// Code 0 - Any byte.
// Code 1 - NUL  (0x00)
// Code 2 - '"'  (0x22)
// Code 3 - '\\' (0x5C)
// Code 4 - 'e'  (0x65) or 'E' (0x45)
// Code 5 - 'n'  (0x6E) or 'N' (0x4E)
// Code 6 - 'r'  (0x72) or 'R' (0x52)
// Code 7 - 't'  (0x74) or 'T' (0x54)
//
static const unsigned char decode_table[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 1
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, // 4
    0, 0, 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, // 6
    0, 0, 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // F
};

#define STATE_START     0
#define STATE_HAVE_ESC  1

// Action 0 - Emit X.
// Action 1 - Get a Buffer.
// Action 2 - Emit X. Move to START state.
// Action 3 - Terminate parse.
// Action 4 - Move to ESC state.
// Action 5 - Emit ESC (0x1B). Move to START state.
// Action 6 - Emit LF  (0x0A). Move to START state.
// Action 7 - Emit CR  (0x0D). Move to START state.
// Action 8 - Emit TAB (0x09). Move to START state.
//

static const int action_table[2][8] =
{
//   Any  '\0' '"'  '\\' 'e'  'n'  'r'  't'
    { 0,   1,   3,   4,   0,   0,   0,   0}, // STATE_START
    { 2,   1,   2,   2,   5,   6,   7,   8}  // STATE_HAVE_ESC
};

void *getstring_noalloc(FILE *f, bool new_strings, size_t *pnBuffer)
{
    static UTF8 buf[2*LBUF_SIZE + 20];
    int c = fgetc(f);
    if (  new_strings
       && c == '"')
    {
        size_t nBufferLeft = sizeof(buf)-10;
        int iState = STATE_START;
        UTF8 *pOutput = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            UTF8 *pInput = pOutput + 6;
            if (fgets((char *)pInput, static_cast<int>(nBufferLeft), f) == nullptr)
            {
                // EOF or ERROR.
                //
                *pOutput = 0;
                if (pnBuffer)
                {
                    *pnBuffer = pOutput - buf;
                }
                return buf;
            }

            size_t nOutput = 0;

            // De-escape this data. removing the '\\' prefixes.
            // Terminate when you hit a '"'.
            //
            for (;;)
            {
                UTF8 ch = *pInput++;
                if (iState == STATE_START)
                {
                    if (decode_table[(unsigned char)ch] == 0)
                    {
                        // As long as decode_table[*p] is 0, just keep copying the characters.
                        //
                        UTF8 *p = pOutput;
                        do
                        {
                            *pOutput++ = ch;
                            ch = *pInput++;
                        } while (decode_table[(unsigned char)ch] == 0);
                        nOutput = pOutput - p;
                    }
                }
                int iAction = action_table[iState][decode_table[(unsigned char)ch]];
                if (iAction <= 2)
                {
                    if (1 == iAction)
                    {
                        // Get Buffer and remain in the current state.
                        //
                        break;
                    }
                    else
                    {
                        // 2 == iAction
                        // Emit X and move to START state.
                        //
                        *pOutput++ = ch;
                        nOutput++;
                        iState = STATE_START;
                    }
                }
                else if (3 == iAction)
                {
                    // Terminate parsing.
                    //
                    *pOutput = 0;
                    if (pnBuffer)
                    {
                        *pnBuffer = pOutput - buf;
                    }
                    return buf;
                }
                else if (4 == iAction)
                {
                    // Move to ESC state.
                    //
                    iState = STATE_HAVE_ESC;
                }
                else if (5 == iAction)
                {
                    *pOutput++ = ESC_CHAR;
                    nOutput++;
                    iState = STATE_START;
                }
                else if (6 == iAction)
                {
                    *pOutput++ = '\n';
                    nOutput++;
                    iState = STATE_START;
                }
                else if (7 == iAction)
                {
                    *pOutput++ = '\r';
                    nOutput++;
                    iState = STATE_START;
                }
                else
                {
                    // if (8 == iAction)
                    *pOutput++ = '\t';
                    nOutput++;
                    iState = STATE_START;
                }
            }

            nBufferLeft -= nOutput;

            // Do we have any more room?
            //
            if (nBufferLeft <= 0)
            {
                *pOutput = 0;
                if (pnBuffer)
                {
                    *pnBuffer = pOutput - buf;
                }
                return buf;
            }
        }
    }
    else
    {
        ungetc(c, f);

        UTF8 *p = buf;
        for (;;)
        {
            // Fetch up to and including the next LF.
            //
            if (fgets((char *)p, LBUF_SIZE, f) == nullptr)
            {
                // EOF or ERROR.
                //
                p[0] = '\0';
            }
            else
            {
                // How much data did we fetch?
                //
                size_t nLine = strlen((char *)p);
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
            if (pnBuffer)
            {
                *pnBuffer = p - buf;
            }
            return buf;
        }
    }
}

// Code 0 - Any byte.
// Code 1 - NUL  (0x00)
// Code 2 - '"'  (0x22)
// Code 3 - '\\' (0x5C)
// Code 4 - ESC  (0x1B)
// Code 5 - LF   (0x0A)
// Code 6 - CR'  (0x0D)
// Code 7 - TAB  (0x09)
//
static const unsigned char encode_table[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    1, 0, 0, 0, 0, 0, 0, 0, 0, 7, 5, 0, 0, 6, 0, 0, // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, // 1
    0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // F
};

void putstring(FILE *f, const UTF8 *pRaw)
{
    static UTF8 aBuffer[2*LBUF_SIZE+4];
    UTF8 *pBuffer = aBuffer;

    // Always leave room for four characters. One at the beginning and
    // three on the end. '\\"\n' or '\""\n'
    //
    *pBuffer++ = '"';

    if (pRaw)
    {
        for (;;)
        {
            UTF8 ch;
            while ((ch = encode_table[(unsigned char)*pRaw]) == 0)
            {
                *pBuffer++ = *pRaw++;
            }

            if (1 == ch)
            {
                break;
            }

            pRaw++;

            switch (ch)
            {
            case 2: ch = '"'; break;
            case 3: ch = '\\'; break;
            case 4: ch = 'e'; break;
            case 5: ch = 'n'; break;
            case 6: ch = 'r'; break;
            case 7: ch = 't'; break;
            }

            *pBuffer++ = '\\';
            *pBuffer++ = ch;
        }
    }

    *pBuffer++ = '"';
    *pBuffer++ = '\n';

    fwrite(aBuffer, sizeof(UTF8), pBuffer - aBuffer, f);
}

int getref(FILE *f)
{
    static UTF8 buf[SBUF_SIZE];
    if (nullptr != fgets((char *)buf, sizeof(buf), f))
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
        b->sub1 = nullptr;
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
        r->sub1 = (BOOLEXP *)StringClone((UTF8 *)b->sub1);
        break;
    default:
        Log.WriteString(T("Bad bool type!" ENDLINE));
        return TRUE_BOOLEXP;
    }
    return (r);
}

#ifndef MEMORY_BASED
int init_dbfile(UTF8 *game_dir_file, UTF8 *game_pag_file, int nCachePages)
{
    if (mudstate.bStandAlone)
    {
        Log.tinyprintf(T("Opening (%s,%s)" ENDLINE), game_dir_file, game_pag_file);
    }
    int cc = cache_init(game_dir_file, game_pag_file, nCachePages);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        if (mudstate.bStandAlone)
        {
            Log.tinyprintf(T("Done opening (%s,%s)." ENDLINE), game_dir_file,
                game_pag_file);
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            Log.tinyprintf(T("Using game db files: (%s,%s)."), game_dir_file,
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
        do_clearcom(obj, obj, obj, 0, 0);
        do_channelnuke(obj);
        del_comsys(obj);
    }
    if (mudconf.have_mailer)
    {
        do_mail_clear(obj, nullptr);
        do_mail_purge(obj);
        malias_cleanup(obj);
    }
}

#if defined(HAVE_WORKING_FORK)
/* ---------------------------------------------------------------------------
 * dump_restart_db: Writes out socket information.
 */
void dump_restart_db(void)
{
    FILE *f;
    DESC *d;
    int version = 4;

    mux_assert(mux_fopen(&f, T("restart.db"), T("wb")));
    mux_fprintf(f, T("+V%d\n"), version);
    putref(f, num_main_game_ports);
    for (int i = 0; i < num_main_game_ports; i++)
    {
        putref(f, main_game_ports[i].msa.port());
        putref(f, main_game_ports[i].socket);
#ifdef UNIX_SSL
        putref(f, main_game_ports[i].fSSL ? 1 : 0);
#else
        putref(f, 0);
#endif
    }
    putref(f, mudstate.start_time.ReturnSeconds());
    putstring(f, mudstate.doing_hdr);
    putref(f, mudstate.record_players);
    putref(f, mudstate.restart_count);
    DESC_ITER_ALL(d)
    {
        putref(f, d->socket);
        putref(f, d->flags);
        putref(f, d->connected_at.ReturnSeconds());
        putref(f, d->command_count);
        putref(f, d->timeout);
        putref(f, 0);
        putref(f, d->player);
        putref(f, d->last_time.ReturnSeconds());
        putref(f, d->raw_input_state);
        putref(f, d->raw_codepoint_state);

        for (int stateloop = 0; stateloop < 256; stateloop++) {
            putref(f, d->nvt_him_state[stateloop]);
            putref(f, d->nvt_us_state[stateloop]);
        }

        putref(f, d->height);
        putref(f, d->width);
        putstring(f, d->ttype);
        putref(f, d->encoding);
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
    FILE *f;
    if (!mux_fopen(&f, T("restart.db"), T("rb")))
    {
        mudstate.restarting = false;
        return;
    }
    DebugTotalFiles++;
    mudstate.restarting = true;

    char buf[8];
    fgets(buf, 3, f);
    mux_assert(strncmp(buf, "+V", 2) == 0);
    int version = getref(f);
    if (  1 == version
       || 2 == version
       || 3 == version
       || 4 == version)
    {
        // Version 1 started on 2001-DEC-03
        // Version 2 started on 2005-NOV-08
        // Version 3 started on 2007-MAR-09
        // Version 4 started on 2007-AUG-12
        //
        num_main_game_ports = getref(f);
        for (int i = 0; i < num_main_game_ports; i++)
        {
            unsigned short usPort = getref(f);
            main_game_ports[i].socket = getref(f);
            socklen_t n = main_game_ports[i].msa.maxaddrlen();
            if (  0 != getsockname(main_game_ports[i].socket, main_game_ports[i].msa.sa(), &n)
               || usPort != main_game_ports[i].msa.port())
            {
                mux_assert(0);
            }

#if defined(UNIX_NETWORKING_SELECT)
            if (maxd <= main_game_ports[i].socket)
            {
                maxd = main_game_ports[i].socket + 1;
            }
#endif // UNIX_NETWORKING_SELECT

            if (3 <= version)
            {
#ifdef UNIX_SSL
                main_game_ports[i].fSSL = (0 != getref(f));
#else
                // Eat meaningless field.
                (void)getref(f);
#endif
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
    DebugTotalSockets += num_main_game_ports;

    mudstate.start_time.SetSeconds(getref(f));

    size_t nBuffer;
    UTF8 *pBuffer = (UTF8 *)getstring_noalloc(f, true, &nBuffer);
    if (version < 3)
    {
        // Convert Latin1 and ANSI to UTF-8 code points.
        //
        pBuffer = ConvertToUTF8((char *)pBuffer, &nBuffer);
    }
    memcpy(mudstate.doing_hdr, pBuffer, nBuffer+1);

    mudstate.record_players = getref(f);
    if (mudconf.reset_players)
    {
        mudstate.record_players = 0;
    }

    if (4 <= version)
    {
        mudstate.restart_count = getref(f) + 1;
    }

    int val;
    DESC *d;
    while ((val = getref(f)) != 0)
    {
        ndescriptors++;
        DebugTotalSockets++;
        d = alloc_desc("restart");
        d->socket = val;
        d->flags = getref(f);
        d->connected_at.SetSeconds(getref(f));
        d->command_count = getref(f);
        d->timeout = getref(f);
        getref(f); // Eat host_info
        d->player = getref(f);
        d->last_time.SetSeconds(getref(f));
        for (int i = 0; i < 256; i++)
        {
            d->nvt_him_state[i] = OPTION_NO;
        }
        for (int i = 0; i < 256; i++)
        {
            d->nvt_us_state[i] = OPTION_NO;
        }
        d->raw_codepoint_length = 0;
        d->ttype = nullptr;
        d->encoding = mudconf.default_charset;
#ifdef UNIX_SSL
        d->ssl_session = nullptr;
#endif
        if (3 <= version)
        {
            d->raw_input_state              = getref(f);
            d->raw_codepoint_state          = getref(f);
            for (int stateloop = 0; stateloop < 256; stateloop++)
            {
                d->nvt_him_state[stateloop] = getref(f);
                d->nvt_us_state[stateloop] = getref(f);
            }

            d->height = getref(f);
            d->width = getref(f);

            size_t nBuffer;
            char *temp = (char *)getstring_noalloc(f, true, &nBuffer);
            if ('\0' != temp[0])
            {
                d->ttype = (UTF8 *)MEMALLOC(nBuffer+1);
                ISOUTOFMEMORY(d->ttype);
                memcpy(d->ttype, temp, nBuffer + 1);
            }

            d->encoding = getref(f);
        }
        else if (2 == version)
        {
            d->raw_input_state              = getref(f);
            d->raw_codepoint_state          = CL_PRINT_START_STATE;
            d->nvt_him_state[TELNET_SGA]    = getref(f);
            d->nvt_us_state[TELNET_SGA]     = getref(f);
            d->nvt_him_state[TELNET_EOR]    = getref(f);
            d->nvt_us_state[TELNET_EOR]     = getref(f);
            d->nvt_him_state[TELNET_NAWS]   = getref(f);
            d->nvt_us_state[TELNET_NAWS]    = getref(f);
            d->height = getref(f);
            d->width = getref(f);
        }
        else
        {
            d->raw_input_state    = NVT_IS_NORMAL;
            d->raw_codepoint_state= CL_PRINT_START_STATE;
            d->height = 24;
            d->width = 78;
        }

        if (3 <= version)
        {
            // Output Prefix.
            //
            size_t nBufferUnicode;
            UTF8 *pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
            if ('\0' != pBufferUnicode[0])
            {
                d->output_prefix = alloc_lbuf("set_userstring");
                memcpy(d->output_prefix, pBufferUnicode, nBufferUnicode+1);
            }
            else
            {
                d->output_prefix = nullptr;
            }

            // Output Suffix
            //
            pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
            if ('\0' != pBufferUnicode[0])
            {
                d->output_suffix = alloc_lbuf("set_userstring");
                memcpy(d->output_suffix, pBufferUnicode, nBufferUnicode+1);
            }
            else
            {
                d->output_suffix = nullptr;
            }

            // Host address.
            //
            pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
            memcpy(d->addr, pBufferUnicode, nBufferUnicode+1);

            // Doing.
            //
            pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
            memcpy(d->doing, pBufferUnicode, nBufferUnicode+1);

            // User name.
            //
            pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
            memcpy(d->username, pBufferUnicode, nBufferUnicode+1);
        }
        else
        {
            // Output Prefix.
            //
            size_t nBufferUnicode;
            UTF8  *pBufferUnicode;
            size_t nBufferLatin1;
            char  *pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
            if ('\0' != pBufferLatin1[0])
            {
                pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
                d->output_prefix = alloc_lbuf("set_userstring");
                memcpy(d->output_prefix, pBufferUnicode, nBufferUnicode+1);
            }
            else
            {
                d->output_prefix = nullptr;
            }

            // Output Suffix
            //
            pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
            if ('\0' != pBufferLatin1[0])
            {
                pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
                d->output_suffix = alloc_lbuf("set_userstring");
                memcpy(d->output_suffix, pBufferUnicode, nBufferUnicode+1);
            }
            else
            {
                d->output_suffix = nullptr;
            }

            // Host address.
            //
            pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->addr, pBufferUnicode, nBufferUnicode+1);

            // Doing.
            //
            pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->doing, pBufferUnicode, nBufferUnicode+1);

            // User name.
            //
            pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->username, pBufferUnicode, nBufferUnicode+1);
        }

        d->output_size = 0;
        d->output_tot = 0;
        d->output_lost = 0;
        d->output_head = nullptr;
        d->output_tail = nullptr;
        d->input_head = nullptr;
        d->input_tail = nullptr;
        d->input_size = 0;
        d->input_tot = 0;
        d->input_lost = 0;
        d->raw_input = nullptr;
        d->raw_input_at = nullptr;
        d->nOption = 0;
        d->quota = mudconf.cmd_quota_max;
        d->program_data = nullptr;
        d->hashnext = nullptr;

        if (descriptor_list)
        {
            DESC *p;
            for (p = descriptor_list; p->next; p = p->next)
            {
                ; // Nothing.
            }
            d->prev = &p->next;
            p->next = d;
            d->next = nullptr;
        }
        else
        {
            d->next = descriptor_list;
            d->prev = &descriptor_list;
            descriptor_list = d;
        }

#if defined(UNIX_NETWORKING_SELECT)
        if (maxd <= d->socket)
        {
            maxd = d->socket + 1;
        }
#endif // UNIX_NETWORKING_SELECT

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
    raw_broadcast(0, T("GAME: Restart finished."));
}
#endif // HAVE_WORKING_FORK

#if defined(WINDOWS_FILES)

int ReplaceFile(UTF8 *old_name, UTF8 *new_name)
{
    size_t nNewName;
    UTF16 *pNewName = ConvertFromUTF8ToUTF16(new_name, &nNewName);
    if (nullptr == pNewName)
    {
        return -1;
    }

    size_t n = (nNewName+1) * sizeof(UTF16);
    UTF16 *p = (UTF16 *)MEMALLOC(n);
    if (nullptr == p)
    {
        return -1;
    }
    memcpy(p, pNewName, n);
    pNewName = p;

    size_t nOldName;
    UTF16 *pOldName = ConvertFromUTF8ToUTF16(old_name, &nOldName);
    if (nullptr == pOldName)
    {
        MEMFREE(pNewName);
        return -1;
    }

    DeleteFile(pNewName);
    if (MoveFile(pOldName, pNewName))
    {
        MEMFREE(pNewName);
        return 0;
    }
    else
    {
        Log.tinyprintf(T("MoveFile %s to %s fails with GetLastError() of %d" ENDLINE),
            old_name, new_name, GetLastError());
    }
    MEMFREE(pNewName);
    return -1;
}

void RemoveFile(UTF8 *name)
{
    size_t nNewName;
    UTF16 *pFileToDelete = ConvertFromUTF8ToUTF16(name, &nNewName);
    if (nullptr != pFileToDelete)
    {
        DeleteFile(pFileToDelete);
    }
}

#endif // WINDOWS_FILES

#if defined(UNIX_FILES)

int ReplaceFile(UTF8 *old_name, UTF8 *new_name)
{
    if (rename((char *)old_name, (char *)new_name) == 0)
    {
        return 0;
    }
    else
    {
        Log.tinyprintf(T("rename %s to %s fails with errno of %s(%d)" ENDLINE), old_name, new_name, strerror(errno), errno);
    }
    return -1;
}

void RemoveFile(UTF8 *name)
{
    unlink((char *)name);
}
#endif // UNIX_FILES

