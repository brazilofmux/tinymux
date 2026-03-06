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

#include "sqlite_backend.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
using namespace std;

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
    {T("Pronoun"),     A_PRONOUN,  AF_VISUAL | AF_NOPROG},
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
            bool done;
            FWDLIST *xfp = fwdlist_get(thing);
            if (xfp)
            {
                delete [] xfp->data;
                delete xfp;
                xfp = nullptr;
            }
            mudstate.forward_lists[thing] = fp;
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
        delete [] xfp->data;
        delete xfp;
        xfp = nullptr;
        const auto it = mudstate.forward_lists.find(thing);
        mudstate.forward_lists.erase(it);
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
        const auto it = mudstate.forward_lists.find(thing);
        if (it != mudstate.forward_lists.end())
            fp = it->second;
        else
            fp = nullptr;
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
    if (!db[thing].name)
    {
        size_t len;
        UTF8 *pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &len);
        db[thing].name = StringCloneLen(pName, len);
        free_lbuf(pName);
    }
    return db[thing].name;
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
            if (!db[thing].name)
            {
                pName = atr_get_LEN(thing, A_NAME, &aowner, &aflags, &nName);
                db[thing].name = StringCloneLen(pName, nName);
                free_lbuf(pName);
            }
            else
            {
                nName = strlen(reinterpret_cast<char *>(db[thing].name));
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
    const UTF8 *pPureName = ConvertToAscii(PureName(thing));
    UTF8 *pPureNameCopy = StringClone(pPureName);

    size_t nMoniker;
    dbref  aowner;
    int    aflags;
    UTF8 *pMoniker = atr_get_LEN(thing, A_MONIKER, &aowner, &aflags,
        &nMoniker);
    const UTF8 *pPureMoniker = ConvertToAscii(strip_color(pMoniker));

    const UTF8 *pReturn = nullptr;
    static UTF8 tbuff[LBUF_SIZE];
    if (strcmp(reinterpret_cast<const char *>(pPureNameCopy), reinterpret_cast<const char *>(pPureMoniker)) == 0)
    {
        // The stripped version of @moniker is the same as the stripped
        // version of @name, so (possibly cache and) use the unstripped
        // version of @moniker.
        //
        if (mudconf.cache_names)
        {
            if (strcmp(reinterpret_cast<const char *>(pMoniker), reinterpret_cast<const char *>(Name(thing))) == 0)
            {
                db[thing].moniker = db[thing].name;
            }
            else
            {
                db[thing].moniker = StringCloneLen(pMoniker, nMoniker);
            }
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
        if (mudconf.cache_names)
        {
            db[thing].moniker = db[thing].name;
            pReturn = db[thing].moniker;
        }
        else
        {
            pReturn = Name(thing);
        }
    }
    free_lbuf(pMoniker);
    MEMFREE(pPureNameCopy);

    return pReturn;
}

void free_Names(OBJ *p)
{
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
    if (!atr_add_raw(thing, A_NAME, s))
    {
        STARTLOG(LOG_PROBLEMS, "DB", "ATTRSYNC");
        log_printf(T("s_Name(#%d): failed to persist A_NAME; leaving in-memory name unchanged."), thing);
        ENDLOG;
        return;
    }

    free_Names(&db[thing]);
    if (nullptr != s)
    {
        db[thing].name = StringClone(s);
    }
}

void free_Moniker(OBJ *p)
{
    if (mudconf.cache_names)
    {
        if (p->name == p->moniker)
        {
            p->moniker = nullptr;
        }
        if (p->moniker)
        {
            MEMFREE(p->moniker);
            p->moniker = nullptr;
        }
    }
}

void s_Moniker(dbref thing, const UTF8 *s)
{
    if (!atr_add_raw(thing, A_MONIKER, s))
    {
        STARTLOG(LOG_PROBLEMS, "DB", "ATTRSYNC");
        log_printf(T("s_Moniker(#%d): failed to persist A_MONIKER."), thing);
        ENDLOG;
        return;
    }
    free_Moniker(&db[thing]);
}

void s_Pass(dbref thing, const UTF8 *s)
{
    if (!atr_add_raw(thing, A_PASS, s))
    {
        STARTLOG(LOG_PROBLEMS, "DB", "ATTRSYNC");
        log_printf(T("s_Pass(#%d): failed to persist A_PASS."), thing);
        ENDLOG;
    }
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
        va = reinterpret_cast<ATTR *>(vattr_find_LEN(pName, nName));
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
    size_t nCased;
    UTF8 *pCased;

    switch (key)
    {
    case ATTRIB_ACCESS:
        {
            // Modify access to user-named attribute
            //
            pCased = mux_strupr(value, nCased);
            string_token st(pCased, T(" "));
            sp = st.parse();
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
                sp = st.parse();
            }

            if (success && !Quiet(executor))
            {
                notify(executor, T("Attribute access changed."));
            }
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

            pName = MakeCanonicalAttributeName(value, &nName, &bValid);
            if (!bValid)
            {
                notify(executor, T("Attribute rename failed."));
                return;
            }

            // Make sure the canonical target name doesn't already exist,
            // unless it resolves to the same attribute.
            //
            va2 = atr_str(pName);
            if (  va2
               && va2 != va)
            {
                notify(executor, T("An attribute with that name already exists."));
                return;
            }

            if (vattr_rename_LEN(OldName, nOldName, pName, nName) == nullptr)
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
          && (n = utf8_FirstByte[static_cast<unsigned char>(*pName)]) < UTF8_CONTINUE
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
        if (  UTF8_CONTINUE <= utf8_FirstByte[static_cast<unsigned char>(*pName)]
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
          && (n = utf8_FirstByte[static_cast<unsigned char>(*pName)]) < UTF8_CONTINUE
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
        const UTF8 *buff = MakeCanonicalAttributeName(a->name, &nLen, &bValid);
        if (!bValid)
        {
            continue;
        }
        anum_extend(a->number);
        anum_set(a->number, a);
        vector<UTF8> v(buff, buff + nLen);
        mudstate.builtin_attribute_names.insert(make_pair(v, a));
    }

    // We specifically allow the '*' character at server
    // initialization because it's part of the A_PASS attribute
    // name.
    //
    for (a = AttrTableSpecial; a->number; a++)
    {
        anum_extend(a->number);
        anum_set(a->number, a);
        const UTF8* buff = a->name;
        const size_t n = strlen(reinterpret_cast<const char*>(buff));
        vector<UTF8> v(buff, buff+n);
        mudstate.builtin_attribute_names.insert(make_pair(v, a));
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
    const vector<UTF8> v(buff, buff + nBuffer);
    const auto it = mudstate.builtin_attribute_names.find(v);
    if (it != mudstate.builtin_attribute_names.end())
    {
        return it->second;
    }

    // Nope, look for a user attribute.
    //
    return vattr_find_LEN(buff, nBuffer);
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
    ATTR **anum_table2 = static_cast<ATTR **>(MEMALLOC((newtop + 1) * sizeof(ATTR *)));
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
    s_ThEmail(executor, mudconf.email_per_hour);
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

bool ThrottleEmail(dbref executor)
{
    if (0 < ThEmail(executor))
    {
        s_ThEmail(executor, ThEmail(executor)-1);
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
        UTF8 *s = find_pattern_delimiter(buff + 1);
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

// Collect attribute numbers for an object from storage.
//
static void collect_attrnums_from_storage(dbref thing, vector<int>& attrnums)
{
    attrnums.clear();

    if (!g_pSQLiteBackend)
    {
        return;
    }

    if (!g_pSQLiteBackend->GetAll(
        static_cast<unsigned int>(thing),
        [&attrnums](unsigned int attrnum, const UTF8 *, size_t, int, int)
        {
            // Skip legacy internal packed attribute list.
            //
            if (  attrnum != static_cast<unsigned int>(A_LIST)
               && attrnum != 0U)
            {
                attrnums.push_back(static_cast<int>(attrnum));
            }
        }))
    {
        Log.tinyprintf(T("collect_attrnums_from_storage: failed to enumerate attrs for #%d" ENDLINE),
            thing);
        attrnums.clear();
        return;
    }

    sort(attrnums.begin(), attrnums.end());
    attrnums.erase(unique(attrnums.begin(), attrnums.end()), attrnums.end());
}

static inline void makekey(dbref thing, int atr, Aname *abuff)
{
    abuff->object = thing;
    abuff->attrnum = atr;
    return;
}

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

bool atr_clr(dbref thing, int atr)
{
    Aname okey;

    makekey(thing, atr, &okey);
    if (!cache_del(&okey))
    {
        Log.tinyprintf(T("atr_clr(#%d/%d): SQLite delete failed, not applying side effects." ENDLINE),
            thing, atr);
        return false;
    }

    switch (atr)
    {
    case A_STARTUP:

        s_Flags(thing, FLAG_WORD1, db[thing].fs.word[FLAG_WORD1] & ~HAS_STARTUP);
        break;

    case A_DAILY:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] & ~HAS_DAILY);
        break;

    case A_FORWARDLIST:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] & ~HAS_FWDLIST);
        if (!mudstate.bStandAlone)
        {
            // We should clear the hashtable, too.
            //
            fwdlist_clr(thing);
        }
        break;

    case A_LISTEN:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] & ~HAS_LISTEN);
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
    return true;
}

/* ---------------------------------------------------------------------------
 * atr_add_raw, atr_add: add attribute of type atr to list
 */

bool atr_add_raw_LEN(dbref thing, int atr, const UTF8 *szValue, size_t nValue)
{
    if (  !szValue
       || '\0' == szValue[0])
    {
        return atr_clr(thing, atr);
    }

    if (nValue > LBUF_SIZE-1)
    {
        nValue = LBUF_SIZE-1;
    }

    Aname okey;
    makekey(thing, atr, &okey);

    // SQLite mode: decode any packed owner/flags prefix from the value
    // before storing in separate columns.  If no prefix is present (the
    // common case), defaults apply.
    //
    dbref raw_owner = Owner(thing);
    int   raw_flags = 0;
    const UTF8 *clean = atr_decode_flags_owner(szValue, &raw_owner, &raw_flags);
    size_t clean_len = nValue - static_cast<size_t>(clean - szValue);

    // Normalize user text to NFC for consistent storage.
    // Legacy internal packed attribute list is deprecated and ignored.
    // NFC never expands the string, so clean_len is a safe bound.
    //
    if (atr == A_LIST)
    {
        return true;
    }

    UTF8 nfc_buf[LBUF_SIZE];
    if (  clean_len > 0
       && !utf8_is_nfc(clean, clean_len))
    {
        size_t nNfc;
        utf8_normalize_nfc(clean, clean_len, nfc_buf, sizeof(nfc_buf) - 1, &nNfc);
        nfc_buf[nNfc] = '\0';
        clean = nfc_buf;
        clean_len = nNfc;
    }

    if (!cache_put(&okey, clean, clean_len + 1, raw_owner, raw_flags))
    {
        Log.tinyprintf(T("atr_add_raw_LEN(#%d/%d): SQLite write failed, not applying side effects." ENDLINE),
            thing, atr);
        return false;
    }

    switch (atr)
    {
    case A_STARTUP:

        s_Flags(thing, FLAG_WORD1, db[thing].fs.word[FLAG_WORD1] | HAS_STARTUP);
        break;

    case A_DAILY:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] | HAS_DAILY);
        break;

    case A_FORWARDLIST:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] | HAS_FWDLIST);
        break;

    case A_LISTEN:

        s_Flags(thing, FLAG_WORD2, db[thing].fs.word[FLAG_WORD2] | HAS_LISTEN);
        break;

    case A_TIMEOUT:

        desc_reload(thing);
        break;

    case A_QUEUEMAX:

        pcache_reload(thing);
        break;
    }
    return true;
}

bool atr_add_raw(dbref thing, int atr, const UTF8 *szValue)
{
    return atr_add_raw_LEN(thing, atr, szValue, szValue ? strlen(reinterpret_cast<const char *>(szValue)) : 0);
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
        if (!atr_add_raw(thing, atr, tbuff))
        {
            STARTLOG(LOG_PROBLEMS, "DB", "ATTRSYNC");
            log_printf(T("atr_add(#%d, %d): failed to persist attribute."), thing, atr);
            ENDLOG;
        }
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

// In SQLite mode, the cache stores clean text with separate owner/flags.
// These statics carry the owner/flags from cache_get back to callers.
//
static dbref cache_last_owner = NOTHING;
static int   cache_last_flags = 0;

const UTF8 *atr_get_raw_LEN(dbref thing, int atr, size_t *pLen)
{
    Aname okey;

    makekey(thing, atr, &okey);
    size_t nLen;
    const UTF8 *a = cache_get(&okey, &nLen, &cache_last_owner, &cache_last_flags);
    if (a)
    {
        if (0 == nLen)
        {
            Log.tinyprintf(T("atr_get_raw_LEN: invalid zero-length value for #%d/%d" ENDLINE),
                thing, atr);
            nLen = 0;
        }
        else
        {
            nLen--;
        }
    }
    else
    {
        nLen = 0;
    }
    *pLen = nLen;
    return a;
}

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
        // SQLite mode: owner/flags are separate columns, already retrieved
        // by cache_get via atr_get_raw_LEN.  Just copy the clean text.
        //
        *owner = (cache_last_owner == NOTHING) ? Owner(thing) : cache_last_owner;
        *flags = cache_last_flags;
        memcpy(s, buff, (*pLen) + 1);
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
    *owner = (cache_last_owner == NOTHING) ? Owner(thing) : cache_last_owner;
    *flags = cache_last_flags;
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
            *owner = (cache_last_owner == NOTHING) ? Owner(thing) : cache_last_owner;
            *flags = cache_last_flags;
            memcpy(s, buff, (*pLen) + 1);
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
            *owner = (cache_last_owner == NOTHING) ? Owner(thing) : cache_last_owner;
            *flags = cache_last_flags;
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
    atr_push();
    unsigned char *as;
    for (int atr = atr_head(thing, &as); atr; atr = atr_next(&as))
    {
        atr_clr(thing, atr);
    }
    atr_pop();

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
    if (  !attrp
       || !*attrp)
    {
        return 0;
    }

    if (mudstate.attr_iter_ctx.pos >= mudstate.attr_iter_ctx.attrs.size())
    {
        *attrp = nullptr;
        return 0;
    }

    int atr = mudstate.attr_iter_ctx.attrs[mudstate.attr_iter_ctx.pos++];
    if (mudstate.attr_iter_ctx.pos >= mudstate.attr_iter_ctx.attrs.size())
    {
        *attrp = nullptr;
    }
    else
    {
        *attrp = reinterpret_cast<UTF8 *>(
            static_cast<uintptr_t>(mudstate.attr_iter_ctx.pos + 1));
    }
    return atr;
}

/* ---------------------------------------------------------------------------
 * atr_push, atr_pop: Push and pop attr lists.
 */

void atr_push(void)
{
    mudstate.attr_iter_stack.push_back(std::move(mudstate.attr_iter_ctx));
    mudstate.attr_iter_ctx = {};
    mudstate.attr_iter_ctx.pos = 0;
}

void atr_pop(void)
{
    if (!mudstate.attr_iter_stack.empty())
    {
        mudstate.attr_iter_ctx = std::move(mudstate.attr_iter_stack.back());
        mudstate.attr_iter_stack.pop_back();
    }
    else
    {
        mudstate.attr_iter_ctx.attrs.clear();
        mudstate.attr_iter_ctx.pos = 0;
    }
}

/* ---------------------------------------------------------------------------
 * atr_head: Returns the head of the attr list for object 'thing'
 */

int atr_head(dbref thing, unsigned char **attrp)
{
    collect_attrnums_from_storage(thing, mudstate.attr_iter_ctx.attrs);
    mudstate.attr_iter_ctx.pos = 0;

    if (mudstate.attr_iter_ctx.attrs.empty())
    {
        *attrp = nullptr;
        return 0;
    }
    *attrp = reinterpret_cast<unsigned char *>(static_cast<uintptr_t>(1));
    return atr_next(reinterpret_cast<UTF8 **>(attrp));
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
        db[thing].cpu_time_used.Set100ns(0);
        db[thing].tThrottleExpired.Set100ns(0);
        s_ThAttrib(thing, 0);
        s_ThMail(thing, 0);
        s_ThRefs(thing, 0);

        db[thing].name = nullptr;
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
    OBJ *newdb = static_cast<OBJ *>(MEMALLOC((newsize + SIZE_HACK) * sizeof(OBJ)));
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
    MARKBUF *newmarkbuf = static_cast<MARKBUF *>(MEMALLOC(marksize));
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
        char *cp = reinterpret_cast<char *>(db);
        MEMFREE(cp);
        cp = nullptr;
        db = nullptr;
    }
    mudstate.db_top = 0;
    mudstate.db_size = 0;
    mudstate.freelist = NOTHING;
}

bool db_make_minimal(void)
{
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    auto cleanup_bootstrap_failure = [&sqldb]()
    {
        if (!sqldb.Begin())
        {
            // If a previous transaction is still open, rollback and retry so
            // cleanup remains atomic.
            //
            sqldb.Rollback();
            if (!sqldb.Begin())
            {
                Log.WriteString(T("db_make_minimal: SQLite cleanup transaction begin failed.\n"));
                db_free();
                mudstate.attr_next = A_USER_START;
                mudstate.record_players = 0;
                return;
            }
        }
        if (  !sqldb.ClearAttributes()
           || !sqldb.ClearObjectTable()
           || !sqldb.ClearAttrNames()
           || !sqldb.PutMeta("attr_next", A_USER_START)
           || !sqldb.PutMeta("db_top", 0)
           || !sqldb.PutMeta("record_players", 0)
           || !sqldb.Commit())
        {
            sqldb.Rollback();
        }
        db_free();
        mudstate.attr_next = A_USER_START;
        mudstate.record_players = 0;
    };

    db_free();
    db_grow(1);
    s_Name(0, T("Limbo"));
    if (nullptr == db[0].name)
    {
        Log.WriteString(T("db_make_minimal: failed to persist Limbo name.\n"));
        cleanup_bootstrap_failure();
        return false;
    }
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
    if (!atr_add_raw(0, A_MONEY, T("0")))
    {
        Log.WriteString(T("db_make_minimal: failed to persist Limbo pennies.\n"));
        cleanup_bootstrap_failure();
        return false;
    }
    s_Owner(0, 1);

    // Insert Limbo (#0) into SQLite before creating Wizard.
    // create_player -> create_obj will insert #1 via its own path.
    //
    CSQLiteDB::ObjectRecord rec;
    rec.dbref_val = 0;
    rec.location  = db[0].location;
    rec.contents  = db[0].contents;
    rec.exits     = db[0].exits;
    rec.next      = db[0].next;
    rec.link      = db[0].link;
    rec.owner     = db[0].owner;
    rec.parent    = db[0].parent;
    rec.zone      = db[0].zone;
    rec.pennies   = 0;
    rec.flags1    = db[0].fs.word[FLAG_WORD1];
    rec.flags2    = db[0].fs.word[FLAG_WORD2];
    rec.flags3    = db[0].fs.word[FLAG_WORD3];
    rec.powers1   = db[0].powers;
    rec.powers2   = db[0].powers2;
    bool bPersisted = false;
    if (!sqldb.Begin())
    {
        Log.WriteString(T("db_make_minimal: failed to begin SQLite transaction for Limbo (#0).\n"));
    }
    else if (!sqldb.InsertObject(rec))
    {
        Log.WriteString(T("db_make_minimal: failed to insert Limbo (#0) into SQLite.\n"));
        sqldb.Rollback();
    }
    else if (!sqldb.PutMeta("db_top", mudstate.db_top))
    {
        Log.WriteString(T("db_make_minimal: failed to persist db_top after Limbo insert.\n"));
        sqldb.Rollback();
    }
    else if (!sqldb.Commit())
    {
        Log.WriteString(T("db_make_minimal: failed to commit Limbo insert.\n"));
        sqldb.Rollback();
    }
    else
    {
        bPersisted = true;
    }

    if (!bPersisted)
    {
        Log.WriteString(T("db_make_minimal: attempting full SQLite runtime resync.\n"));
        if (!sqlite_sync_runtime())
        {
            Log.WriteString(T("db_make_minimal: SQLite runtime resync failed.\n"));
            cleanup_bootstrap_failure();
            return false;
        }
    }

    // should be #1
    //
    load_player_names();
    const UTF8 *pmsg;
    dbref obj = create_player(T("Wizard"), T("potrzebie"), NOTHING, false, &pmsg);
    if (obj == NOTHING)
    {
        Log.WriteString(T("db_make_minimal: failed to create Wizard player.\n"));
        cleanup_bootstrap_failure();
        return false;
    }
    const UTF8 *pWizardPass = atr_get_raw(obj, A_PASS);
    if (  nullptr == pWizardPass
       || '\0' == pWizardPass[0])
    {
        Log.WriteString(T("db_make_minimal: failed to persist Wizard password.\n"));
        cleanup_bootstrap_failure();
        return false;
    }
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

    // Authoritative post-bootstrap refresh ensures Wizard setup and db_top
    // are durably reflected in SQLite before declaring minimal boot success.
    //
    if (!sqlite_sync_runtime())
    {
        Log.WriteString(T("db_make_minimal: post-bootstrap sqlite_sync_runtime failed.\n"));
        cleanup_bootstrap_failure();
        return false;
    }
    return true;
}

int64_t creation_seconds(dbref obj)
{
    if (!Good_obj(obj))
    {
        return 0;
    }

    const UTF8 *pCreated = atr_get_raw(obj, A_CREATED);
    if (nullptr == pCreated)
    {
        return 0;
    }

    CLinearTimeAbsolute lta;
    if (!lta.SetString(pCreated))
    {
        return 0;
    }

    lta.Local2UTC();
    return lta.ReturnSeconds();
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
        else if (':' == *p)
        {
            // Parse objid format: dbref:timestamp
            //
            int x = mux_atol(s);
            if (x < 0)
            {
                return NOTHING;
            }

            p++;
            const UTF8 *pTimestamp = p;
            if (!mux_isdigit(*p))
            {
                return NOTHING;
            }
            p++;
            while (mux_isdigit(*p))
            {
                p++;
            }

            // Parse trailing spaces.
            //
            while (mux_isspace(*p))
            {
                p++;
            }

            if ('\0' != *p)
            {
                return NOTHING;
            }

            dbref obj = static_cast<dbref>(x);
            if (!Good_obj(obj))
            {
                return NOTHING;
            }

            int64_t csecs = creation_seconds(obj);
            if (  0 == csecs
               || csecs != mux_atoi64(pTimestamp))
            {
                return NOTHING;
            }
            return obj;
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
            if (fgets(reinterpret_cast<char *>(pInput), static_cast<int>(nBufferLeft), f) == nullptr)
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
                    if (decode_table[static_cast<unsigned char>(ch)] == 0)
                    {
                        // As long as decode_table[*p] is 0, just keep copying the characters.
                        //
                        UTF8 *p = pOutput;
                        do
                        {
                            *pOutput++ = ch;
                            ch = *pInput++;
                        } while (decode_table[static_cast<unsigned char>(ch)] == 0);
                        nOutput = pOutput - p;
                    }
                }
                int iAction = action_table[iState][decode_table[static_cast<unsigned char>(ch)]];
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
            if (fgets(reinterpret_cast<char *>(p), LBUF_SIZE, f) == nullptr)
            {
                // EOF or ERROR.
                //
                p[0] = '\0';
            }
            else
            {
                // How much data did we fetch?
                //
                size_t nLine = strlen(reinterpret_cast<char *>(p));
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
            while ((ch = encode_table[static_cast<unsigned char>(*pRaw)]) == 0)
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
    if (nullptr != fgets(reinterpret_cast<char *>(buf), sizeof(buf), f))
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
        r->sub1 = reinterpret_cast<BOOLEXP *>(StringClone(reinterpret_cast<UTF8 *>(b->sub1)));
        break;
    default:
        Log.WriteString(T("Bad bool type!" ENDLINE));
        return TRUE_BOOLEXP;
    }
    return (r);
}

int init_dbfile(const UTF8 *indb)
{
    if (mudstate.bStandAlone)
    {
        Log.tinyprintf(T("Opening SQLite (from %s)" ENDLINE), indb);
    }
    int cc = cache_init(indb);
    if (cc != HF_OPEN_STATUS_ERROR)
    {
        if (mudstate.bStandAlone)
        {
            Log.tinyprintf(T("Done opening SQLite." ENDLINE));
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_text(T("Using SQLite storage backend."));
            ENDLOG;
        }
        db_free();
    }
    return cc;
}

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
    do_comdisconnect(obj);
    do_clearcom(obj, obj, obj, 0, 0);
    do_channelnuke(obj);
    del_comsys(obj);
    do_mail_clear(obj, nullptr);
    do_mail_purge(obj);
    malias_cleanup(obj);
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
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it)
    {
        d = *it;
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
    UTF8 *pBuffer = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBuffer));
    if (version < 3)
    {
        // Convert Latin1 and ANSI to UTF-8 code points.
        //
        pBuffer = ConvertToUTF8(reinterpret_cast<char *>(pBuffer), &nBuffer);
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
        DebugTotalSockets++;
        d = alloc_desc("restart");
        init_desc(d);
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
            char *temp = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBuffer));
            if ('\0' != temp[0])
            {
                d->ttype = reinterpret_cast<UTF8 *>(MEMALLOC(nBuffer+1));
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
            UTF8 *pBufferUnicode = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBufferUnicode));
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
            pBufferUnicode = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBufferUnicode));
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
            pBufferUnicode = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBufferUnicode));
            memcpy(d->addr, pBufferUnicode, nBufferUnicode+1);

            // Doing.
            //
            pBufferUnicode = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBufferUnicode));
            memcpy(d->doing, pBufferUnicode, nBufferUnicode+1);

            // User name.
            //
            pBufferUnicode = reinterpret_cast<UTF8 *>(getstring_noalloc(f, true, &nBufferUnicode));
            memcpy(d->username, pBufferUnicode, nBufferUnicode+1);
        }
        else
        {
            // Output Prefix.
            //
            size_t nBufferUnicode;
            UTF8  *pBufferUnicode;
            size_t nBufferLatin1;
            char  *pBufferLatin1 = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBufferLatin1));
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
            pBufferLatin1 = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBufferLatin1));
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
            pBufferLatin1 = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBufferLatin1));
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->addr, pBufferUnicode, nBufferUnicode+1);

            // Doing.
            //
            pBufferLatin1 = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBufferLatin1));
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->doing, pBufferUnicode, nBufferUnicode+1);

            // User name.
            //
            pBufferLatin1 = reinterpret_cast<char *>(getstring_noalloc(f, true, &nBufferLatin1));
            pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
            memcpy(d->username, pBufferUnicode, nBufferUnicode+1);
        }

        d->output_size = 0;
        d->output_tot = 0;
        d->output_lost = 0;
        d->input_size = 0;
        d->input_tot = 0;
        d->input_lost = 0;
        d->raw_input_buf = nullptr;
        d->raw_input_at = nullptr;
        d->nOption = 0;
        d->quota = mudconf.cmd_quota_max;
        d->program_data = nullptr;

        auto it = mudstate.descriptors_list.insert(mudstate.descriptors_list.end(), d);
        mudstate.descriptors_map.insert(make_pair(d, it));

        desc_addhash(d);
        if (isPlayer(d->player))
        {
            s_Connected(d->player);
        }
    }

    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); )
    {
        d = *it;
	++it;
        if (  (d->flags & DS_CONNECTED)
           && !isPlayer(d->player))
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

int ReplaceFile(UTF8 *old_name, UTF8 *new_name)
{
    std::error_code ec;
    std::filesystem::rename(
        std::filesystem::u8path(reinterpret_cast<const char*>(old_name)),
        std::filesystem::u8path(reinterpret_cast<const char*>(new_name)),
        ec);
    if (ec)
    {
        Log.tinyprintf(T("rename %s to %s fails: %s (%d)" ENDLINE),
            old_name, new_name, ec.message().c_str(), ec.value());
        return -1;
    }
    return 0;
}

void RemoveFile(const UTF8 *name)
{
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::u8path(reinterpret_cast<const char*>(name)),
        ec);
}

#define SQLITE_WRITABLE() (!mudstate.bSQLiteLoading)

static void sqlite_log_object_update_failure(const UTF8 *field, dbref obj, int val)
{
    Log.tinyprintf(T("SQLite object update failed: field=%s obj=#%d val=%d" ENDLINE),
        field, obj, val);
}

void s_Location(dbref t, dbref n)
{
    db[t].location = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateLocation(t, n))
        {
            sqlite_log_object_update_failure(T("location"), t, n);
        }
    }
}

void s_Zone(dbref t, dbref n)
{
    db[t].zone = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateZone(t, n))
        {
            sqlite_log_object_update_failure(T("zone"), t, n);
        }
    }
}

void s_Contents(dbref t, dbref n)
{
    db[t].contents = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateContents(t, n))
        {
            sqlite_log_object_update_failure(T("contents"), t, n);
        }
    }
}

void s_Exits(dbref t, dbref n)
{
    db[t].exits = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateExits(t, n))
        {
            sqlite_log_object_update_failure(T("exits"), t, n);
        }
    }
}

void s_Next(dbref t, dbref n)
{
    db[t].next = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateNext(t, n))
        {
            sqlite_log_object_update_failure(T("next"), t, n);
        }
    }
}

void s_Link(dbref t, dbref n)
{
    db[t].link = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateLink(t, n))
        {
            sqlite_log_object_update_failure(T("link"), t, n);
        }
    }
}

void s_Owner(dbref t, dbref n)
{
    db[t].owner = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateOwner(t, n))
        {
            sqlite_log_object_update_failure(T("owner"), t, n);
        }
    }
}

void s_Parent(dbref t, dbref n)
{
    db[t].parent = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateParent(t, n))
        {
            sqlite_log_object_update_failure(T("parent"), t, n);
        }
    }
}

void s_Flags(dbref t, int f, FLAG n)
{
    db[t].fs.word[f] = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdateFlags(t,
            db[t].fs.word[FLAG_WORD1],
            db[t].fs.word[FLAG_WORD2],
            db[t].fs.word[FLAG_WORD3]))
        {
            sqlite_log_object_update_failure(T("flags"), t,
                static_cast<int>(db[t].fs.word[f]));
        }
    }
}

void s_Powers(dbref t, POWER n)
{
    db[t].powers = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdatePowers(t, db[t].powers, db[t].powers2))
        {
            sqlite_log_object_update_failure(T("powers"), t, db[t].powers);
        }
    }
}

void s_Powers2(dbref t, POWER n)
{
    db[t].powers2 = n;
    if (SQLITE_WRITABLE())
    {
        if (!g_pSQLiteBackend->GetDB().UpdatePowers(t, db[t].powers, db[t].powers2))
        {
            sqlite_log_object_update_failure(T("powers2"), t, db[t].powers2);
        }
    }
}

void s_Home(dbref t, dbref n)
{
    s_Link(t, n);
}

void s_Dropto(dbref t, dbref n)
{
    s_Location(t, n);
}

// Bulk sync attributes, objects, attrnames, and related metadata in one
// transaction. Called after db_read and failover paths to keep SQLite fully
// in lockstep with runtime.
//
bool sqlite_sync_runtime(void)
{
    struct AttrRow
    {
        dbref obj;
        int attrnum;
        std::vector<UTF8> value;
        dbref owner;
        int flags;
    };

    std::vector<AttrRow> attrSnapshot;
    attrSnapshot.reserve(1024);

    dbref iObject;
    DO_WHOLE_DB(iObject)
    {
        if (isGarbage(iObject))
        {
            continue;
        }

        if (!g_pSQLiteBackend->GetAll(
            static_cast<unsigned int>(iObject),
            [&attrSnapshot, iObject](unsigned int attrnum, const UTF8 *value, size_t len, int owner, int flags)
            {
                if (  attrnum == static_cast<unsigned int>(A_LIST)
                   || attrnum == 0U)
                {
                    return;
                }

                AttrRow row;
                row.obj = iObject;
                row.attrnum = static_cast<int>(attrnum);
                row.owner = static_cast<dbref>(owner);
                row.flags = flags;
                if (  nullptr != value
                   && 0 < len)
                {
                    row.value.assign(value, value + len);
                }
                else
                {
                    row.value.push_back('\0');
                }
                attrSnapshot.push_back(std::move(row));
            }))
        {
            Log.tinyprintf(T("sqlite_sync_runtime: failed to enumerate attrs for #%d" ENDLINE),
                iObject);
            return false;
        }
    }

    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
    if (!sqldb.Begin())
    {
        return false;
    }

    if (  !sqldb.ClearAttributes()
       || !sqldb.ClearObjectTable()
       || !sqldb.ClearAttrNames())
    {
        sqldb.Rollback();
        return false;
    }

    for (dbref i = 0; i < mudstate.db_top; i++)
    {
        CSQLiteDB::ObjectRecord rec;
        rec.dbref_val = i;
        rec.location  = db[i].location;
        rec.contents  = db[i].contents;
        rec.exits     = db[i].exits;
        rec.next      = db[i].next;
        rec.link      = db[i].link;
        rec.owner     = db[i].owner;
        rec.parent    = db[i].parent;
        rec.zone      = db[i].zone;
        rec.pennies   = Pennies(i);
        rec.flags1    = db[i].fs.word[FLAG_WORD1];
        rec.flags2    = db[i].fs.word[FLAG_WORD2];
        rec.flags3    = db[i].fs.word[FLAG_WORD3];
        rec.powers1   = db[i].powers;
        rec.powers2   = db[i].powers2;

        if (!sqldb.InsertObject(rec))
        {
            sqldb.Rollback();
            return false;
        }
    }

    for (const auto& row : attrSnapshot)
    {
        if (!sqldb.PutAttribute(row.obj,
                row.attrnum,
                row.value.empty() ? nullptr : row.value.data(),
                row.value.size(),
                row.owner,
                row.flags))
        {
            sqldb.Rollback();
            return false;
        }
    }

    for (int iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        ATTR *vp = static_cast<ATTR *>(anum_get(iAttr));
        if (  vp != nullptr
           && !(vp->flags & AF_DELETED))
        {
            if (!sqldb.PutAttrName(vp->number,
                    reinterpret_cast<const char *>(vp->name), vp->flags))
            {
                sqldb.Rollback();
                return false;
            }
        }
    }

    if (  !sqldb.PutMeta("attr_next", mudstate.attr_next)
       || !sqldb.PutMeta("db_top", mudstate.db_top)
       || !sqldb.PutMeta("record_players", mudstate.record_players))
    {
        sqldb.Rollback();
        return false;
    }

    if (!sqldb.Commit())
    {
        sqldb.Rollback();
        return false;
    }
    return true;
}

// Load game state from SQLite (warm start).
// Returns:
//   1  if SQLite had data and we loaded successfully,
//   0  if SQLite has no game data (cold start needed),
//  -1  if SQLite had data but load failed.
//
int sqlite_load_game(void)
{
    CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();

    // Check if SQLite has data by reading db_top.
    //
    int db_top_val = 0;
    if (!sqldb.GetMeta("db_top", &db_top_val) || db_top_val <= 0)
    {
        return 0;
    }

    int attr_next_val = A_USER_START;
    sqldb.GetMeta("attr_next", &attr_next_val);

    int record_players_val = 0;
    sqldb.GetMeta("record_players", &record_players_val);
    mudstate.record_players = record_players_val;

    // Suppress write-through for the entire load.
    //
    mudstate.bSQLiteLoading = true;

    // Load attribute names first (before objects, since objects
    // reference attribute names for Name, etc.)
    //
    int max_attrnum_loaded = A_USER_START - 1;
    if (!sqldb.LoadAllAttrNames(
        [&max_attrnum_loaded](int attrnum, const char *name, int flags)
        {
            anum_extend(attrnum);
            vattr_define_LEN(
                reinterpret_cast<const UTF8 *>(name),
                strlen(name), attrnum, flags);
            if (attrnum > max_attrnum_loaded)
            {
                max_attrnum_loaded = attrnum;
            }
        }))
    {
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    if (attr_next_val <= max_attrnum_loaded)
    {
        attr_next_val = max_attrnum_loaded + 1;
        if (!sqldb.PutMeta("attr_next", attr_next_val))
        {
            Log.tinyprintf(T("sqlite_load_game: failed to persist corrected attr_next=%d" ENDLINE),
                attr_next_val);
        }
    }
    mudstate.attr_next = attr_next_val;

    // Load all objects from the objects table first, then size db[]
    // from the actual max dbref to avoid stale metadata overruns.
    //
    vector<CSQLiteDB::ObjectRecord> objects;
    if (!sqldb.LoadAllObjects(
        [&objects](const CSQLiteDB::ObjectRecord &rec)
        {
            objects.push_back(rec);
        }))
    {
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    dbref max_dbref = -1;
    for (const auto& rec : objects)
    {
        if (rec.dbref_val > max_dbref)
        {
            max_dbref = rec.dbref_val;
        }
    }

    dbref expected_top = db_top_val;
    if (max_dbref >= 0)
    {
        dbref needed = max_dbref + 1;
        if (needed > expected_top)
        {
            expected_top = needed;
        }
    }

    if (expected_top <= 0)
    {
        mudstate.bSQLiteLoading = false;
        return -1;
    }

    vector<bool> seen(static_cast<size_t>(expected_top), false);
    for (const auto& rec : objects)
    {
        dbref i = rec.dbref_val;
        if (  i < 0
           || i >= expected_top)
        {
            mudstate.bSQLiteLoading = false;
            return -1;
        }
        if (seen[static_cast<size_t>(i)])
        {
            mudstate.bSQLiteLoading = false;
            return -1;
        }
        seen[static_cast<size_t>(i)] = true;
    }
    for (dbref i = 0; i < expected_top; i++)
    {
        if (!seen[static_cast<size_t>(i)])
        {
            Log.tinyprintf(T("sqlite_load_game: missing object row #%d (db_top=%d)." ENDLINE),
                i, db_top_val);
            mudstate.bSQLiteLoading = false;
            return -1;
        }
    }

    if (expected_top != db_top_val)
    {
        if (!sqldb.PutMeta("db_top", expected_top))
        {
            Log.tinyprintf(T("sqlite_load_game: failed to persist corrected db_top=%d" ENDLINE),
                expected_top);
        }
        db_top_val = expected_top;
    }

    // Grow db[] to the right size.
    //
    db_grow(expected_top);

    for (const auto& rec : objects)
    {
        dbref i = rec.dbref_val;
        if (  i < 0
           || i >= mudstate.db_top)
        {
            mudstate.bSQLiteLoading = false;
            return -1;
        }

        db[i].location = rec.location;
        db[i].contents = rec.contents;
        db[i].exits    = rec.exits;
        db[i].next     = rec.next;
        db[i].link     = rec.link;
        db[i].owner    = rec.owner;
        db[i].parent   = rec.parent;
        db[i].zone     = rec.zone;
        db[i].fs.word[FLAG_WORD1] = rec.flags1;
        db[i].fs.word[FLAG_WORD2] = rec.flags2;
        db[i].fs.word[FLAG_WORD3] = rec.flags3;
        db[i].powers   = rec.powers1;
        db[i].powers2  = rec.powers2;

        // Clear CONNECTED flag — nobody is connected at startup.
        //
        if (isPlayer(i))
        {
            db[i].fs.word[FLAG_WORD2] &= ~CONNECTED;
        }
    }
    mudstate.bSQLiteLoading = false;

    // Load player names.
    //
    load_player_names();

    STARTLOG(LOG_STARTUP, "INI", "LOAD");
    log_printf(T("Loaded %d objects from SQLite."), db_top_val);
    ENDLOG;

    return 1;
}
