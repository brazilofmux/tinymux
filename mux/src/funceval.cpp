// funceval.cpp -- MUX function handlers.
//
// $Id: funceval.cpp,v 1.32 2003-02-17 01:59:21 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <limits.h>
#include <math.h>

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "comsys.h"
#include "functions.h"
#include "misc.h"
#include "pcre.h"

/* Note: Many functions in this file have been taken, whole or in part, from
 * PennMUSH 1.50, and TinyMUSH 2.2, for softcode compatibility. The
 * maintainers of MUX would like to thank those responsible for PennMUSH 1.50
 * and TinyMUSH 2.2, and hope we have adequately noted in the source where
 * credit is due.
 */

extern NAMETAB indiv_attraccess_nametab[];
extern int countwords(char *, char);
extern void arr2list(char *arr[], int alen, char *list, char **bufc, char sep);
extern void make_portlist(dbref, dbref, char *, char **);

bool parse_and_get_attrib(dbref executor, char *fargs[], char **atext, dbref *thing, char *buff, char **bufc)
{
    ATTR *ap;

    // Two possibilities for the first arg: <obj>/<attr> and <attr>.
    //
    if (!parse_attrib(executor, fargs[0], thing, &ap)) 
    {
        *thing = executor;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return false;
    }

    // Use it if we can access it, otherwise return an error.
    //
    if (!See_attr(executor, *thing, ap))
    {
        safe_noperm(buff, bufc);
        return false;
    }

    dbref aowner;
    int aflags;
    *atext = atr_pget(*thing, ap->number, &aowner, &aflags);
    if (!*atext) 
    {
        return false;
    } 
    else if (!**atext)
    {
        free_lbuf(*atext);
        return false;
    }
    return true;
}

#define CWHO_ON  0
#define CWHO_OFF 1
#define CWHO_ALL 2

FUNCTION(fun_cwho)
{
    struct channel *ch = select_channel(fargs[0]);
    if (!ch)
    {
        safe_str("#-1 CHANNEL NOT FOUND", buff, bufc);
        return;
    }
    if (  !mudconf.have_comsys
       || (  !Comm_All(executor)
          && executor != ch->charge_who))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int match_type = CWHO_ON;    
    if (nfargs == 2)
    {
        if (mux_stricmp(fargs[1], "all") == 0)
        {
            match_type = CWHO_ALL;
        }
        else if (mux_stricmp(fargs[1], "off") == 0)
        {
            match_type = CWHO_OFF;
        }
        else if (mux_stricmp(fargs[1], "on") == 0)
        {
            match_type = CWHO_ON;
        }
    }

    ITL pContext;
    struct comuser *user;
    ItemToList_Init(&pContext, buff, bufc, '#');
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (  (  match_type == CWHO_ALL
              || (  (Connected(user->who) || isThing(user->who))
                 && (  (match_type == CWHO_ON && user->bUserIsOn)
                    || (match_type == CWHO_OFF && !(user->bUserIsOn)))))
           && !ItemToList_AddInteger(&pContext, user->who))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

FUNCTION(fun_beep)
{
    safe_chr(BEEP_CHAR, buff, bufc);
}

#define ANSI_F  0x00000001
#define ANSI_H  0x00000002
#define ANSI_U  0x00000004
#define ANSI_I  0x00000008
#define ANSI_FC 0x00000010
#define ANSI_BC 0x00000020

static const unsigned char ansi_have_table[256] =
{
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x10-0x1F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x30-0x3F
    0,           0,             ANSI_BC,     ANSI_BC,     // 0x40-0x43
    0,           0,             0,           ANSI_BC,     // 0x44-0x47
    0,           0,             0,           0,           // 0x48-0x4B
    0,           ANSI_BC,       0,           0,           // 0x4B-0x4F
    0,           0,             ANSI_BC,     0,           // 0x50-0x53
    0,           0,             0,           ANSI_BC,     // 0x54-0x57
    ANSI_BC,     ANSI_BC,       0,           0,           // 0x58-0x5B
    0,           0,             0,           0,           // 0x5B-0x5F
    0,           0,             ANSI_FC,     ANSI_FC,     // 0x60-0x63
    0,           0,             ANSI_F,      ANSI_FC,     // 0x64-0x67
    ANSI_H,      ANSI_I,        0,           0,           // 0x68-0x6B
    0,           ANSI_FC,       0,           0,           // 0x6C-0x6F
    0,           0,             ANSI_FC,     0,           // 0x70-0x73
    0,           ANSI_U,        0,           ANSI_FC,     // 0x74-0x77
    ANSI_FC,     ANSI_FC,       0,           0,           // 0x78-0x7B
    0,           0,             0,           0,           // 0x7B-0x7F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xD0-0xDF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0       // 0xF0-0xFF
};

void SimplifyColorLetters(char *pOut, char *pIn)
{
    if (  pIn[0] == 'n'
       && pIn[1] == '\0')
    {
        pOut[0] = 'n';
        pOut[1] = '\0';
        return;
    }
    char *p;
    int have = 0;
    size_t nIn = strlen(pIn);
    for (p = pIn + nIn - 1; p >= pIn && *p != 'n'; p--)
    {
        int mask = ansi_have_table[(unsigned char)*p];
        if (  mask
           && (have & mask) == 0)
        {
            *pOut++ = *p;
            have |= mask;
        }
    }
    *pOut = '\0';
}

// This function was originally taken from PennMUSH 1.50
//
FUNCTION(fun_ansi)
{
    int iArg0;
    for (iArg0 = 0; iArg0 + 1 < nfargs; iArg0 += 2)
    {
        char   pOut[8];
        SimplifyColorLetters(pOut, fargs[iArg0]);
        char tmp[LBUF_SIZE];
        char *bp = tmp;

        char *s = pOut;
        while (*s)
        {
            extern const char *ColorTable[256];
            const char *pColor = ColorTable[(unsigned char)*s];
            if (pColor)
            {
                safe_str(pColor, tmp, &bp);
            }
            s++;
        }
        safe_str(fargs[iArg0+1], tmp, &bp);
        *bp = '\0';
        int nVisualWidth;
        size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
        size_t nLen = ANSI_TruncateToField(tmp, nBufferAvailable, *bufc,
            LBUF_SIZE, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
        *bufc += nLen;
    }
}

FUNCTION(fun_zone)
{
    if (!mudconf.have_zones)
    {
        safe_str("#-1 ZONES DISABLED", buff, bufc);
        return;
    }
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (Examinable(executor, it))
    {
        safe_tprintf_str(buff, bufc, "#%d", Zone(it));
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

#ifdef SIDE_EFFECT_FUNCTIONS

static bool check_command(dbref player, char *name, char *buff, char **bufc)
{
    CMDENT *cmdp = (CMDENT *)hashfindLEN(name, strlen(name), &mudstate.command_htab);
    if (cmdp)
    {
        // Perform checks similiar to (but not exactly like) the
        // ones in process_cmdent(): object type checks, permission
        // checks, ands global flags.
        //
        if (  Invalid_Objtype(player)
           || !check_access(player, cmdp->perms)
           || (  !Builder(player)
              && Protect(CA_GBL_BUILD)
              && !(mudconf.control_flags & CF_BUILD)))
        {
            safe_noperm(buff, bufc);
            return true;
        }
    }
    return false;
}

FUNCTION(fun_link)
{
    if (check_command(executor, "@link", buff, bufc))
    {
        return;
    }
    do_link(executor, caller, enactor, 0, 2, fargs[0], fargs[1]);
}

FUNCTION(fun_tel)
{
    if (check_command(executor, "@teleport", buff, bufc))
    {
        return;
    }
    do_teleport(executor, caller, enactor, 0, 2, fargs[0], fargs[1]);
}

FUNCTION(fun_pemit)
{
    if (check_command(executor, "@pemit", buff, bufc))
    {
        return;
    }
    do_pemit_list(executor, PEMIT_PEMIT, false, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_oemit)
{
    if (check_command(executor, "@oemit", buff, bufc))
    {
        return;
    }
    do_pemit_single(executor, PEMIT_OEMIT, false, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_emit)
{
    if (check_command(executor, "@emit", buff, bufc))
    {
        return;
    }
    do_say(executor, caller, enactor, SAY_EMIT, fargs[0]);
}

FUNCTION(fun_remit)
{
    if (check_command(executor, "@pemit", buff, bufc))
    {
        return;
    }
    do_pemit_single(executor, PEMIT_PEMIT, true, 0, fargs[0], 0, fargs[1]);
}

// ------------------------------------------------------------------------
// fun_create: Creates a room, thing or exit.
//
FUNCTION(fun_create)
{
    char sep;

    varargs_preamble(3);
    char *name = fargs[0];

    if (!name || !*name)
    {
        safe_str("#-1 ILLEGAL NAME", buff, bufc);
        return;
    }
    if (nfargs >= 3 && *fargs[2])
    {
        sep = *fargs[2];
    }
    else
    {
        sep = 't';
    }

    dbref thing;
    int cost;

    switch (sep)
    {
    case 'r':

        if (check_command(executor, "@dig", buff, bufc))
        {
            return;
        }
        thing = create_obj(executor, TYPE_ROOM, name, 0);
        break;

    case 'e':

        if (check_command(executor, "@open", buff, bufc))
        {
            return;
        }
        thing = create_obj(executor, TYPE_EXIT, name, 0);
        if (thing != NOTHING)
        {
            s_Exits(thing, executor);
            s_Next(thing, Exits(executor));
            s_Exits(executor, thing);
        }
        break;

    default:

        if (check_command(executor, "@create", buff, bufc))
        {
            return;
        }
        if (*fargs[1])
        {
            cost = mux_atol(fargs[1]);
            if (cost < mudconf.createmin || cost > mudconf.createmax)
            {
                safe_range(buff, bufc);
                return;
            }
        }
        else
        {
            cost = mudconf.createmin;
        }
        thing = create_obj(executor, TYPE_THING, name, cost);
        if (thing != NOTHING)
        {
            move_via_generic(thing, executor, NOTHING, 0);
            s_Home(thing, new_home(executor));
        }
        break;
    }
    safe_tprintf_str(buff, bufc, "#%d", thing);
}

/* ---------------------------------------------------------------------------
 * fun_set: sets an attribute on an object
 */

static void set_attr_internal(dbref player, dbref thing, int attrnum, char *attrtext, int key, char *buff, char **bufc)
{
    if (!Good_obj(thing))
    {
        safe_noperm(buff, bufc);
        notify_quiet(player, "You shouldn't be rummaging through the garbage.");
        return;
    }

    dbref aowner;
    int aflags;
    ATTR *attr = atr_num(attrnum);
    atr_pget_info(thing, attrnum, &aowner, &aflags);
    if (attr && bCanSetAttr(player, thing, attr))
    {
        bool could_hear = Hearer(thing);
        atr_add(thing, attrnum, attrtext, Owner(player), aflags);
        handle_ears(thing, could_hear, Hearer(thing));
        if (  !(key & SET_QUIET)
           && !Quiet(player)
           && !Quiet(thing))
        {
            notify_quiet(player, "Set.");
        }
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_set)
{
    if (check_command(executor, "@set", buff, bufc))
    {
        return;
    }

    dbref thing, aowner;
    int aflags;
    ATTR *attr;

    // See if we have the <obj>/<attr> form, which is how you set
    // attribute flags.
    //
    if (parse_attrib(executor, fargs[0], &thing, &attr))
    {
        if (  attr
           && See_attr(executor, thing, attr))
        {
            char *flagname = fargs[1];

            // You must specify a flag name.
            //
            if (flagname[0] == '\0')
            {
                safe_str("#-1 UNSPECIFIED PARAMETER", buff, bufc);
                return;
            }

            // Check for clearing.
            //
            bool clear = false;
            if (flagname[0] == NOT_TOKEN)
            {
                flagname++;
                clear = true;
            }

            // Make sure player specified a valid attribute flag.
            //
            int flagvalue = search_nametab(executor, indiv_attraccess_nametab, flagname);
            if (flagvalue < 0)
            {
                safe_str("#-1 CAN NOT SET", buff, bufc);
                return;
            }

            // Make sure the object has the attribute present.
            //
            if (!atr_get_info(thing, attr->number, &aowner, &aflags))
            {
                safe_str("#-1 ATTRIBUTE NOT PRESENT ON OBJECT", buff, bufc);
                return;
            }

            // Make sure we can write to the attribute.
            //
            if (!bCanSetAttr(executor, thing, attr))
            {
                safe_noperm(buff, bufc);
                return;
            }

            // Go do it.
            //
            if (clear)
            {
                aflags &= ~flagvalue;
            }
            else
            {
                aflags |= flagvalue;
            }
            atr_set_flags(thing, attr->number, aflags);
            return;
        }
    }

    // Find thing.
    //
    thing = match_controlled_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_nothing(buff, bufc);
        return;
    }

    // Check for attr set first.
    //
    char *p;
    for (p = fargs[1]; *p && *p != ':'; p++)
    {
        ; // Nothing
    }

    if (*p)
    {
        *p++ = 0;
        int atr = mkattr(fargs[1]);
        if (atr <= 0)
        {
            safe_str("#-1 UNABLE TO CREATE ATTRIBUTE", buff, bufc);
            return;
        }
        attr = atr_num(atr);
        if (!attr)
        {
            safe_noperm(buff, bufc);
            return;
        }
        if (!bCanSetAttr(executor, thing, attr))
        {
            safe_noperm(buff, bufc);
            return;
        }
        char *buff2 = alloc_lbuf("fun_set");

        // Check for _
        //
        if (*p == '_')
        {
            ATTR *attr2;
            dbref thing2;

            strcpy(buff2, p + 1);
            if (!( parse_attrib(executor, p + 1, &thing2, &attr2)
                && attr2))
            {
                free_lbuf(buff2);
                safe_nomatch(buff, bufc);
                return;
            }
            p = buff2;
            atr_pget_str(buff2, thing2, attr2->number, &aowner, &aflags);

            if (!See_attr(executor, thing2, attr2))
            {
                free_lbuf(buff2);
                safe_noperm(buff, bufc);
                return;
            }
        }

        // Go set it.
        //
        set_attr_internal(executor, thing, atr, p, 0, buff, bufc);
        free_lbuf(buff);
        return;
    }

    // Set/clear a flag.
    //
    flag_set(thing, executor, fargs[1], 0);
}
#endif

// Generate a substitution array.
//
static unsigned int GenCode(char *pCode, const char *pCodeASCII)
{
    // Strip out the ANSI.
    //
    size_t nIn;
    char *pIn = strip_ansi(pCodeASCII, &nIn);

    // Process the printable characters.
    //
    char *pOut = pCode;
    while (*pIn)
    {
        if (mux_isprint(*pIn))
        {
            *pOut++ = *pIn - ' ';
        }
        pIn++;
    }
    *pOut = '\0';
    return pOut - pCode;
}

static char *crypt_code(char *code, char *text, bool type)
{
    if (!text && !*text)
    {
        return "";
    }
    if (!code || !*code)
    {
        return text;
    }

    char codebuff[LBUF_SIZE];
    unsigned int nCode = GenCode(codebuff, code);
    if (nCode == 0)
    {
        return text;
    }

    static char textbuff[LBUF_SIZE];
    char *p = strip_ansi(text);
    char *q = codebuff;
    char *r = textbuff;

    int iMod    = '~' - ' ' + 1;

    // Encryption loop:
    //
    while (*p)
    {
        if (mux_isprint(*p))
        {
            int iCode = *p - ' ';
            if (type)
            {
                iCode += *q;
                if (iMod <= iCode)
                {
                    iCode -= iMod;
                }
            }
            else
            {
                iCode -= *q;
                if (iCode < 0)
                {
                    iCode += iMod;
                }
            }
            *r++ = iCode + ' ';
            q++;
            if (*q == '\0')
            {
                q = codebuff;
            }
        }
        p++;
    }
    *r = '\0';
    return textbuff;
}

// Code for encrypt() and decrypt() was taken from the DarkZone
// server.
//
FUNCTION(fun_encrypt)
{
    safe_str(crypt_code(fargs[1], fargs[0], true), buff, bufc);
}

FUNCTION(fun_decrypt)
{
    safe_str(crypt_code(fargs[1], fargs[0], false), buff, bufc);
}

// Borrowed from DarkZone
//
void scan_zone
(
    dbref executor,
    char *szZone,
    int   ObjectType,
    char *buff,
    char **bufc
)
{
    if (!mudconf.have_zones)
    {
        safe_str("#-1 ZONES DISABLED", buff, bufc);
        return;
    }

    dbref it = match_thing_quiet(executor, szZone);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DO_WHOLE_DB(i)
    {
        if (  Typeof(i) == ObjectType
           && Zone(i) == it
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

FUNCTION(fun_zwho)
{
    scan_zone(executor, fargs[0], TYPE_PLAYER, buff, bufc);
}

FUNCTION(fun_inzone)
{
    scan_zone(executor, fargs[0], TYPE_ROOM, buff, bufc);
}

// Borrowed from DarkZone
//
FUNCTION(fun_children)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!(  WizRoy(executor)
              || Controls(executor, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DO_WHOLE_DB(i)
    {
        if (  Parent(i) == it
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

FUNCTION(fun_objeval)
{
    if (!*fargs[0])
    {
        return;
    }
    char *name = alloc_lbuf("fun_objeval");
    char *bp = name;
    char *str = fargs[0];
    mux_exec(name, &bp, executor, caller, enactor,
             EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    dbref obj = match_thing_quiet(executor, name);
    if (!Good_obj(obj))
    {
        safe_match_result(obj, buff, bufc);
        return;
    }

    if (!Controls(executor, obj))
    {
        // The right circumstances were not met, so we are evaluating
        // as the executor who gave the command instead of the
        // requested object.
        //
        obj = executor;
    }

    mudstate.nObjEvalNest++;
    str = fargs[1];
    mux_exec(buff, bufc, obj, executor, enactor,
             EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);
    free_lbuf(name);
    mudstate.nObjEvalNest--;
}

FUNCTION(fun_localize)
{
    char **preserve = NULL;
    int *preserve_len = NULL;
    preserve = PushPointers(MAX_GLOBAL_REGS);
    preserve_len = PushIntegers(MAX_GLOBAL_REGS);
    save_global_regs("fun_localize", preserve, preserve_len);

    char *str = fargs[0];
    mux_exec(buff, bufc, executor, caller, enactor,
        EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);

    restore_global_regs("fun_localize", preserve, preserve_len);
    PopIntegers(preserve_len, MAX_GLOBAL_REGS);
    PopPointers(preserve, MAX_GLOBAL_REGS);
}

FUNCTION(fun_null)
{
    return;
}

FUNCTION(fun_squish)
{
    if (nfargs == 0)
    {
        return;
    }

    char sep;
    varargs_preamble(2);

    char *p;
    char *q = fargs[0];
    while ((p = strchr(q, sep)) != NULL)
    {
        p = p + 1;
        size_t nLen = p - q;
        safe_copy_buf(q, nLen, buff, bufc);
        q = p;
        while (*q == sep)
        {
            q++;
        }
    }
    safe_str(q, buff, bufc);
}

FUNCTION(fun_stripansi)
{
    safe_str(strip_ansi(fargs[0]), buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_zfun)
{
    if (!mudconf.have_zones)
    {
        safe_str("#-1 ZONES DISABLED", buff, bufc);
        return;
    }

    dbref zone = Zone(executor);
    if (!Good_obj(zone))
    {
        safe_str("#-1 INVALID ZONE", buff, bufc);
        return;
    }

    // Find the user function attribute.
    //
    int attrib = get_atr(fargs[0]);
    if (!attrib)
    {
        safe_str("#-1 NO SUCH USER FUNCTION", buff, bufc);
        return;
    }
    dbref aowner;
    int aflags;
    ATTR *attr = atr_num(attrib);
    char *tbuf1 = atr_pget(zone, attrib, &aowner, &aflags);
    if (!attr || !See_attr(executor, zone, attr))
    {
        safe_noperm(buff, bufc);
        free_lbuf(tbuf1);
        return;
    }
    char *str = tbuf1;
    mux_exec(buff, bufc, zone, executor, enactor,
             EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, &(fargs[1]), nfargs - 1);
    free_lbuf(tbuf1);
}

FUNCTION(fun_columns)
{
    char sep;
    evarargs_preamble(3);

    int nWidth = mux_atol(fargs[1]);
    int nIndent = 0;
    if (nfargs == 4)
    {
        nIndent = mux_atol(fargs[3]);
        if (nIndent < 0 || 77 < nIndent)
        {
            nIndent = 1;
        }
    }

    int nRight = nIndent + nWidth;
    if (  nWidth < 1
       || 78 < nWidth
       || nRight < 1
       || 78 < nRight)
    {
        safe_range(buff, bufc);
        return;
    }
    char *curr = alloc_lbuf("fun_columns");
    char *cp = curr;
    char *bp = curr;
    char *str = fargs[0];
    mux_exec(curr, &bp, executor, caller, enactor,
             EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';
    cp = trim_space_sep(cp, sep);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }

    int nColumns = (78-nIndent)/nWidth;
    int iColumn = 0;

    int nBufferAvailable = LBUF_SIZE - (*bufc-buff) - 1;
    bool bNeedCRLF = false;
    while (  cp
          && 0 < nBufferAvailable)
    {
        if (iColumn == 0)
        {
            nBufferAvailable -= safe_fill(buff, bufc, ' ', nIndent);
        }

        char *objstring = split_token(&cp, sep);
        int nVisualWidth;
        int nLen = ANSI_TruncateToField(objstring, nBufferAvailable, *bufc,
            nWidth, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
        *bufc += nLen;
        nBufferAvailable -= nLen;

        if (nColumns-1 <= iColumn)
        {
            iColumn = 0;
            nBufferAvailable -= safe_copy_buf("\r\n", 2, buff, bufc);
            bNeedCRLF = false;
        }
        else
        {
            iColumn++;
            nBufferAvailable -= safe_fill(buff, bufc, ' ',
                nWidth - nVisualWidth);
            bNeedCRLF = true;
        }
    }
    if (bNeedCRLF)
    {
        safe_copy_buf("\r\n", 2, buff, bufc);
    }
    free_lbuf(curr);
}

// table(<list>,<field width>,<line length>,<delimiter>,<output separator>, <padding>)
//
// Ported from PennMUSH 1.7.3 by Morgan.
//
// TODO: Support ANSI in output separator and padding.
//
FUNCTION(fun_table)
{
    // Check argument numbers, assign values and defaults if necessary.
    //
    char *pPaddingStart = NULL;
    char *pPaddingEnd = NULL;
    if (nfargs == 6 && *fargs[5])
    {
        pPaddingStart = strip_ansi(fargs[5]);
        pPaddingEnd = strchr(pPaddingStart, '\0');
    }

    // Get single-character separator.
    //
    char cSeparator = ' ';
    if (nfargs >= 5 && fargs[4][0] != '\0')
    {
        if (fargs[4][1] == '\0')
        {
            cSeparator = *fargs[4];
        }
        else
        {
            safe_str("#-1 SEPARATOR MUST BE ONE CHARACTER", buff, bufc);
            return;
        }
    }

    // Get single-character delimiter.
    //
    char cDelimiter = ' ';
    if (nfargs >= 4 && fargs[3][0] != '\0')
    {
        if (fargs[3][1] == '\0')
        {
            cDelimiter = *fargs[3];
        }
        else
        {
            safe_str("#-1 DELIMITER MUST BE ONE CHARACTER", buff, bufc);
            return;
        }
    }

    // Get line length.
    //
    int nLineLength = 78;
    if (nfargs >= 3)
    {
        nLineLength = mux_atol(fargs[2]);
    }

    // Get field width.
    //
    int nFieldWidth = 10;
    if (nfargs >= 2)
    {
        nFieldWidth = mux_atol(fargs[1]);
    }
    else
    {
        nFieldWidth = 10;
    }

    // Validate nFieldWidth and nLineLength.
    //
    if (  nLineLength < 1
       || LBUF_SIZE   <= nLineLength
       || nFieldWidth < 1
       || nLineLength < nFieldWidth)
    {
        safe_range(buff, bufc);
        return;
    }

    int nNumCols = nLineLength / nFieldWidth;
    char *pNext = trim_space_sep(fargs[0], cDelimiter);
    if (!*pNext)
    {
        return;
    }

    char *pCurrent = split_token(&pNext, cDelimiter);
    if (!pCurrent)
    {
        return;
    }

    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    int nCurrentCol = nNumCols - 1;
    for (;;)
    {
        int nVisibleLength, nPaddingLength;
        int nStringLength =
            ANSI_TruncateToField( pCurrent, nBufferAvailable, *bufc,
                                  nFieldWidth, &nVisibleLength, ANSI_ENDGOAL_NORMAL);

        *bufc += nStringLength;
        nBufferAvailable -= nStringLength;

        nPaddingLength = nFieldWidth - nVisibleLength;
        if (nPaddingLength > nBufferAvailable)
        {
            nPaddingLength = nBufferAvailable;
        }
        if (nPaddingLength)
        {
            nBufferAvailable -= nPaddingLength;
            if (pPaddingStart)
            {
                for (  char *pPaddingCurrent = pPaddingStart;
                       nPaddingLength > 0;
                       nPaddingLength--)
                {
                    **bufc = *pPaddingCurrent;
                    (*bufc)++;
                    pPaddingCurrent++;

                    if (pPaddingCurrent == pPaddingEnd)
                    {
                        pPaddingCurrent = pPaddingStart;
                    }
                }
            }
            else
            {
                memset(*bufc, ' ', nPaddingLength);
                *bufc += nPaddingLength;
            }
        }

        pCurrent = split_token(&pNext, cDelimiter);
        if (!pCurrent)
        {
            break;
        }

        if (!nCurrentCol)
        {
            nCurrentCol = nNumCols - 1;
            if (nBufferAvailable >= 2)
            {
                char *p = *bufc;
                p[0] = '\r';
                p[1] = '\n';

                nBufferAvailable -= 2;
                *bufc += 2;
            }
            else
            {
                // nBufferAvailable has less than 2 characters left, if there's
                // no room left just break out.
                //
                if (!nBufferAvailable)
                {
                    break;
                }
            }
        }
        else
        {
            nCurrentCol--;
            if (!nBufferAvailable)
            {
                break;
            }
            **bufc = cSeparator;
            (*bufc)++;
            nBufferAvailable--;
        }
    }
}

// Code for objmem and playmem borrowed from PennMUSH 1.50
//
static int mem_usage(dbref thing)
{
    int ca;
    char *as;
    int k = sizeof(struct object) + strlen(Name(thing)) + 1;
    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        size_t nLen;
        const char *str = atr_get_raw_LEN(thing, ca, &nLen);
        k += nLen+1;
        ATTR *attr = atr_num(ca);
        if (attr)
        {
            str = attr->name;
            if (str && *str)
            {
                k += strlen(str)+1;
            }
        }
    }
    return k;
}

FUNCTION(fun_objmem)
{
    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
    }
    else if (Examinable(executor, thing))
    {
        safe_ltoa(mem_usage(thing), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_playmem)
{
    dbref thing;
    if (nfargs == 1)
    {
        thing = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(thing))
        {
            safe_match_result(thing, buff, bufc);
            return;
        }
        else if (!Examinable(executor, thing))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }
    else
    {
        thing = executor;
    }
    int tot = 0;
    dbref j;
    DO_WHOLE_DB(j)
    {
        if (Owner(j) == thing)
        {
            tot += mem_usage(j);
        }
    }
    safe_ltoa(tot, buff, bufc);
}

// Code for andflags() and orflags() borrowed from PennMUSH 1.50
// false for orflags, true for andflags
//
static bool handle_flaglists(dbref player, char *name, char *fstr, bool type)
{
    dbref it = match_thing_quiet(player, name);
    if (!Good_obj(it))
    {
        return false;
    }

    char *s;
    char flagletter[2];
    FLAGSET fset;
    FLAG p_type;
    bool negate = false;
    bool temp = false;
    bool ret = type;

    for (s = fstr; *s; s++)
    {
        // Check for a negation sign. If we find it, we note it and
        // increment the pointer to the next character.
        //
        if (*s == '!')
        {
            negate = true;
            s++;
        }
        else
        {
            negate = false;
        }

        if (!*s)
        {
            return false;
        }
        flagletter[0] = *s;
        flagletter[1] = '\0';

        if (!convert_flags(player, flagletter, &fset, &p_type))
        {
            // Either we got a '!' that wasn't followed by a letter, or we
            // couldn't find that flag. For AND, since we've failed a check,
            // we can return false. Otherwise we just go on.
            //
            if (type)
            {
                return false;
            }
            else
            {
                continue;
            }
        }
        else
        {
            // Does the object have this flag?
            //
            if (  (Flags(it) & fset.word[FLAG_WORD1])
               || (Flags2(it) & fset.word[FLAG_WORD2])
               || (Flags3(it) & fset.word[FLAG_WORD3])
               || Typeof(it) == p_type)
            {
                if (  isPlayer(it)
                   && fset.word[FLAG_WORD2] == CONNECTED
                   && Hidden(it)
                   && !See_Hidden(player))
                {
                    temp = false;
                }
                else
                {
                    temp = true;
                }
            }
            else
            {
                temp = false;
            }

            if (  type
               && (  (negate && temp)
                  || (!negate && !temp)))
            {
                // Too bad there's no NXOR function. At this point we've
                // either got a flag and we don't want it, or we don't have a
                // flag and we want it. Since it's AND, we return false.
                //
                return false;

            }
            else if (  !type
                    && (  (!negate && temp)
                       || (negate && !temp)))
            {
                // We've found something we want, in an OR. We OR a true with
                // the current value.
                //
                ret |= true;
            }

            // Otherwise, we don't need to do anything.
            //
        }
    }
    return ret;
}

FUNCTION(fun_orflags)
{
    safe_bool(handle_flaglists(executor, fargs[0], fargs[1], false), buff, bufc);
}

FUNCTION(fun_andflags)
{
    safe_bool(handle_flaglists(executor, fargs[0], fargs[1], true), buff, bufc);
}

FUNCTION(fun_strtrunc)
{
    int maxVisualWidth = mux_atol(fargs[1]);
    if (maxVisualWidth < 0)
    {
        safe_range(buff, bufc);
        return;
    }
    if (maxVisualWidth == 0)
    {
        return;
    }
    int nVisualWidth;
    char buf[LBUF_SIZE+1];
    ANSI_TruncateToField(fargs[0], LBUF_SIZE, buf, maxVisualWidth, &nVisualWidth, ANSI_ENDGOAL_NORMAL);
    safe_str(buf, buff, bufc);
}

FUNCTION(fun_ifelse)
{
    // This function assumes that its arguments have not been evaluated.
    //
    char *lbuff = alloc_lbuf("fun_ifelse");
    char *bp = lbuff;
    char *str = fargs[0];
    mux_exec(lbuff, &bp, executor, caller, enactor,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    if (!xlate(lbuff))
    {
        if (nfargs == 3)
        {
            str = fargs[2];
            mux_exec(buff, bufc, executor, caller, enactor,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        }
    }
    else
    {
        str = fargs[1];
        mux_exec(buff, bufc, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    }
    free_lbuf(lbuff);
}

FUNCTION(fun_inc)
{
    if (nfargs == 1)
    {
        safe_ltoa(mux_atol(fargs[0]) + 1, buff, bufc);
    }
    else
    {
        safe_chr('1', buff, bufc);
    }
}

FUNCTION(fun_dec)
{
    if (nfargs == 1)
    {
        safe_ltoa(mux_atol(fargs[0]) - 1, buff, bufc);
    }
    else
    {
        safe_str("-1", buff, bufc);
    }
}

// Mail functions borrowed from DarkZone.
//
// This function can take one of three formats:
//
// 1. mail(num)           --> returns message <num> for privs.
// 2. mail(executor)      --> returns number of messages for <executor>.
// 3. mail(executor, num) --> returns message <num> for <executor>.
// 4. mail()              --> returns number of messages for executor.
//
FUNCTION(fun_mail)
{
    if (!mudconf.have_mailer)
    {
        safe_str("#-1 MAILER DISABLED.", buff, bufc);
        return;
    }

    dbref playerask;
    int num, rc, uc, cc;

    // Make sure we have the right number of arguments.
    //
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        count_mail(executor, 0, &rc, &uc, &cc);
        safe_ltoa(rc + uc, buff, bufc);
        return;
    }
    else if (nfargs == 1)
    {
        if (!is_integer(fargs[0], NULL))
        {
            // Handle the case of wanting to count the number of
            // messages.
            //
            playerask = lookup_player(executor, fargs[0], true);
            if (playerask == NOTHING)
            {
                playerask = match_thing_quiet(executor, fargs[0]);
                if (!isPlayer(playerask))
                {
                    safe_str("#-1 NO SUCH PLAYER", buff, bufc);
                    return;
                }
            }
            if (playerask == executor || Wizard(executor))
            {
                count_mail(playerask, 0, &rc, &uc, &cc);
                safe_tprintf_str(buff, bufc, "%d %d %d", rc, uc, cc);
            }
            else
            {
                safe_noperm(buff, bufc);
            }
            return;
        }
        else
        {
            playerask = executor;
            num = mux_atol(fargs[0]);
        }
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (playerask == NOTHING)
        {
            safe_str("#-1 NO SUCH PLAYER", buff, bufc);
            return;
        }
        else if (  (playerask == executor && !mudstate.nObjEvalNest)
                || God(executor))
        {
            num = mux_atol(fargs[1]);
        }
        else
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    if (num < 1 || !isPlayer(playerask))
    {
        safe_str("#-1 NO SUCH MESSAGE", buff, bufc);
        return;
    }
    struct mail *mp = mail_fetch(playerask, num);
    if (mp)
    {
        safe_str(MessageFetch(mp->number), buff, bufc);
        return;
    }

    // Ran off the end of the list without finding anything.
    //
    safe_str("#-1 NO SUCH MESSAGE", buff, bufc);
}

// This function can take these formats:
//
//  1) mailfrom(<num>)
//  2) mailfrom(<executor>,<num>)
//
// It returns the dbref of the executor the mail is from.
//
FUNCTION(fun_mailfrom)
{
    if (!mudconf.have_mailer)
    {
        safe_str("#-1 MAILER DISABLED.", buff, bufc);
        return;
    }

    // Make sure we have the right number of arguments.
    //
    int num;
    dbref playerask;
    if (nfargs == 1)
    {
        playerask = executor;
        num = mux_atol(fargs[0]);
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(executor, fargs[0], true);
        if (playerask == NOTHING)
        {
            safe_str("#-1 NO SUCH PLAYER", buff, bufc);
            return;
        }
        if (playerask == executor || Wizard(executor))
        {
            num = mux_atol(fargs[1]);
        }
        else
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    if (num < 1 || !isPlayer(playerask))
    {
        safe_str("#-1 NO SUCH MESSAGE", buff, bufc);
        return;
    }
    struct mail *mp = mail_fetch(playerask, num);
    if (mp != NULL)
    {
        safe_tprintf_str(buff, bufc, "#%d", mp->from);
        return;
    }

    // Ran off the end of the list without finding anything.
    //
    safe_str("#-1 NO SUCH MESSAGE", buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_hasattr: does object X have attribute Y.
// Hasattr (and hasattrp, which is derived from hasattr) borrowed from
// TinyMUSH 2.2.

void hasattr_handler(char *buff, char **bufc, dbref executor, char *fargs[], 
                   bool bCheckParent)
{
    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    ATTR *attr = atr_str(fargs[1]);
    bool result = false;
    if (attr)
    {
        if (!bCanReadAttr(executor, thing, attr, bCheckParent))
        {
            safe_noperm(buff, bufc);
            return;
        }
        else
        {
            if (bCheckParent)
            {
                dbref aowner;
                int aflags;
                char *tbuf = atr_pget(thing, attr->number, &aowner, &aflags);
                result = (tbuf[0] != '\0');
                free_lbuf(tbuf);
            }
            else
            {
                const char *tbuf = atr_get_raw(thing, attr->number);
                result = (tbuf != NULL);
            }
        }
    }
    safe_bool(result, buff, bufc);
}

FUNCTION(fun_hasattr)
{
    hasattr_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_hasattrp)
{
    hasattr_handler(buff, bufc, executor, fargs, true);
}

/* ---------------------------------------------------------------------------
 * fun_default, fun_edefault, and fun_udefault:
 * These check for the presence of an attribute. If it exists, then it
 * is gotten, via the equivalent of get(), get_eval(), or u(), respectively.
 * Otherwise, the default message is used.
 * In the case of udefault(), the remaining arguments to the function
 * are used as arguments to the u().
 */

// default(), edefault(), and udefault() borrowed from TinyMUSH 2.2
//
#define DEFAULT_DEFAULT  1
#define DEFAULT_EDEFAULT 2
#define DEFAULT_UDEFAULT 4

void default_handler(char *buff, char **bufc, dbref executor, dbref caller, dbref enactor,
                     char *fargs[], int nfargs, char *cargs[], int ncargs, int key)
{
    dbref thing, aowner;
    int aflags;
    ATTR *attr;
    char *objname, *bp, *str;

    objname = bp = alloc_lbuf("default_handler");
    str = fargs[0];
    mux_exec(objname, &bp, executor, caller, enactor,
             EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
    *bp = '\0';

    // First we check to see that the attribute exists on the object.
    // If so, we grab it and use it.
    //
    if (objname != NULL)
    {
        if (parse_attrib(executor, objname, &thing, &attr))
        {
            if (  attr
               && See_attr(executor, thing, attr))
            {
                char *atr_gotten = atr_pget(thing, attr->number, &aowner, &aflags);
                if (atr_gotten[0] != '\0')
                {
                    switch (key)
                    {
                    case DEFAULT_DEFAULT:
                        safe_str(atr_gotten, buff, bufc);
                        break;
                    case DEFAULT_EDEFAULT:
                        str = atr_gotten;
                        mux_exec(buff, bufc, thing, executor, executor,
                             EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
                        break;
                    case DEFAULT_UDEFAULT:
                        str = atr_gotten;
                        mux_exec(buff, bufc, thing, caller, enactor,
                             EV_FCHECK | EV_EVAL, &str, &(fargs[2]), nfargs - 2);
                        break;

                    }
                    free_lbuf(atr_gotten);
                    free_lbuf(objname);
                    return;
                }
                free_lbuf(atr_gotten);
            }
        }
        free_lbuf(objname);
    }

    // If we've hit this point, we've not gotten anything useful, so
    // we go and evaluate the default.
    //
    str = fargs[1];
    mux_exec(buff, bufc, executor, caller, enactor,
             EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
}


FUNCTION(fun_default)
{
    default_handler(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs, 
        ncargs, DEFAULT_DEFAULT);
}

FUNCTION(fun_edefault)
{
    default_handler(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs, 
        ncargs, DEFAULT_EDEFAULT);
}

FUNCTION(fun_udefault)
{
    default_handler(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs, 
        ncargs, DEFAULT_UDEFAULT);
}

/* ---------------------------------------------------------------------------
 * fun_findable: can X locate Y
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_findable)
{
    dbref obj = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(obj))
    {
        safe_match_result(obj, buff, bufc);
        safe_str(" (ARG1)", buff, bufc);
        return;
    }
    dbref victim = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(victim))
    {
        safe_match_result(victim, buff, bufc);
        safe_str(" (ARG2)", buff, bufc);
        return;
    }
#ifdef WOD_REALMS
    if (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(obj, victim, ACTION_IS_STATIONARY))
    {
        safe_bool(locatable(obj, victim, obj), buff, bufc);
    }
    else
    {
        safe_chr('0', buff, bufc);
    }
#else
    safe_bool(locatable(obj, victim, obj), buff, bufc);
#endif
}

/* ---------------------------------------------------------------------------
 * isword: is every character in the argument a letter?
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_isword)
{
    char *p;
    bool result = true;

    for (p = fargs[0]; *p; p++)
    {
        if (!mux_isalpha(*p))
        {
            result = false;
            break;
        }
    }
    safe_bool(result, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_visible. Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_visible)
{
    dbref it = match_thing_quiet(executor, fargs[0]); 
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        safe_str(" (ARG1)", buff, bufc);
        return;
    }
    else if (!Controls(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    bool  result = false;
    dbref thing;
    ATTR  *attr;
    if (!parse_attrib(executor, fargs[1], &thing, &attr))
    {
        thing = match_thing_quiet(executor, fargs[1]);
        if (!Good_obj(thing))
        {
            safe_match_result(thing, buff, bufc);
            safe_str(" (ARG2)", buff, bufc);
            return;
        }
    }
    if (Good_obj(thing))
    {
        if (attr)
        {
            result = (See_attr(it, thing, attr));
        }
        else
        {
            result = (Examinable(it, thing));
        }
    }
    safe_bool(result, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_elements: given a list of numbers, get corresponding elements from
 * the list.  elements(ack bar eep foof yay,2 4) ==> bar foof
 * The function takes a separator, but the separator only applies to the
 * first list.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_elements)
{
    int nwords, cur;
    char *ptrs[LBUF_SIZE / 2];
    char *wordlist, *s, *r, sep;

    varargs_preamble(3);
    bool bFirst = true;

    // Turn the first list into an array.
    //
    wordlist = alloc_lbuf("fun_elements.wordlist");
    strcpy(wordlist, fargs[0]);
    nwords = list2arr(ptrs, LBUF_SIZE / 2, wordlist, sep);

    s = trim_space_sep(fargs[1], ' ');

    // Go through the second list, grabbing the numbers and finding the
    // corresponding elements.
    //
    do {
        r = split_token(&s, ' ');
        cur = mux_atol(r) - 1;
        if (  cur >= 0
           && cur < nwords
           && ptrs[cur])
        {
            if (!bFirst)
            {
                safe_chr(sep, buff, bufc);
            }
            bFirst = false;
            safe_str(ptrs[cur], buff, bufc);
        }
    } while (s);
    free_lbuf(wordlist);
}

/* ---------------------------------------------------------------------------
 * fun_grab: a combination of extract() and match(), sortof. We grab the
 *           single element that we match.
 *
 *  grab(Test:1 Ack:2 Foof:3,*:2)    => Ack:2
 *  grab(Test-1+Ack-2+Foof-3,*o*,+)  => Foof-3
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_grab)
{
    char sep;
    varargs_preamble(3);

    // Walk the wordstring, until we find the word we want.
    //
    char *s = trim_space_sep(fargs[0], sep);
    do
    {
        char *r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            safe_str(r, buff, bufc);
            return;
        }
    } while (s);
}

/* ---------------------------------------------------------------------------
 * fun_scramble:  randomizes the letters in a string.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_scramble)
{
    size_t n;
    char *old = strip_ansi(fargs[0], &n);

    if (2 <= n)
    {
        unsigned int i;
        for (i = 0; i < n-1; i++)
        {
            int j = RandomINT32(i, n-1);
            char c = old[i];
            old[i] = old[j];
            old[j] = c;
        }
    }
    safe_str(old, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_shuffle: randomize order of words in a list.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_shuffle)
{
    char *words[LBUF_SIZE];
    int n, i, j;
    char sep;

    varargs_preamble(2);

    n = list2arr(words, LBUF_SIZE, fargs[0], sep);

    for (i = 0; i < n-1; i++)
    {
        j = RandomINT32(i, n-1);

        // Swap words[i] with words[j]
        //
        char *temp = words[i];
        words[i] = words[j];
        words[j] = temp;
    }
    arr2list(words, n, buff, bufc, sep);
}

// pickrand -- choose a random item from a list.
//
FUNCTION(fun_pickrand)
{
    if (nfargs == 0 || fargs[0][0] == '\0')
    {
        return;
    }
    char sep;
    varargs_preamble(2);

    char *s = trim_space_sep(fargs[0], sep);
    char *t = s;
    if (s[0] == '\0')
    {
        return;
    }
    INT32 n;
    for (n = 0; t; t = next_token(t, sep), n++)
    {
        ; // Nothing
    }

    if (n >= 1)
    {
        INT32 w = RandomINT32(0, n-1);
        for (n = 0; n < w; n++)
        {
            s = next_token(s, sep);
        }
        t = split_token(&s, sep);
        safe_str(t, buff, bufc);
    }
}

// sortby() code borrowed from TinyMUSH 2.2
//
static char  ucomp_buff[LBUF_SIZE];
static dbref ucomp_executor;
static dbref ucomp_caller;
static dbref ucomp_enactor;

static int u_comp(const void *s1, const void *s2)
{
    // Note that this function is for use in conjunction with our own
    // sane_qsort routine, NOT with the standard library qsort!
    //
    char *result, *tbuf, *elems[2], *bp, *str;
    int n;

    if ((mudstate.func_invk_ctr > mudconf.func_invk_lim) ||
        (mudstate.func_nest_lev > mudconf.func_nest_lim))
        return 0;

    tbuf = alloc_lbuf("u_comp");
    elems[0] = (char *)s1;
    elems[1] = (char *)s2;
    strcpy(tbuf, ucomp_buff);
    result = bp = alloc_lbuf("u_comp");
    str = tbuf;
    mux_exec(result, &bp, ucomp_executor, ucomp_caller, ucomp_enactor,
             EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &(elems[0]), 2);
    *bp = '\0';
    if (!result)
        n = 0;
    else {
        n = mux_atol(result);
        free_lbuf(result);
    }
    free_lbuf(tbuf);
    return n;
}

typedef int PV(const void *, const void *);

static void sane_qsort(void *array[], int left, int right, PV compare)
{
    // Andrew Molitor's qsort, which doesn't require transitivity between
    // comparisons (essential for preventing crashes due to boneheads
    // who write comparison functions where a > b doesn't mean b < a).
    //
    int i, last;
    void *tmp;

loop:

    if (left >= right)
        return;

    // Pick something at random at swap it into the leftmost slot
    // This is the pivot, we'll put it back in the right spot later.
    //
    i = RandomINT32(0, right - left);
    tmp = array[left + i];
    array[left + i] = array[left];
    array[left] = tmp;

    last = left;
    for (i = left + 1; i <= right; i++) {

        // Walk the array, looking for stuff that's less than our
        // pivot. If it is, swap it with the next thing along
        //
        if ((*compare) (array[i], array[left]) < 0)
        {
            last++;
            if (last == i)
                continue;

            tmp = array[last];
            array[last] = array[i];
            array[i] = tmp;
        }
    }

    // Now we put the pivot back, it's now in the right spot, we never
    // need to look at it again, trust me.
    //
    tmp = array[last];
    array[last] = array[left];
    array[left] = tmp;

    // At this point everything underneath the 'last' index is < the
    // entry at 'last' and everything above it is not < it.
    //
    if ((last - left) < (right - last))
    {
        sane_qsort(array, left, last - 1, compare);
        left = last + 1;
        goto loop;
    }
    else
    {
        sane_qsort(array, last + 1, right, compare);
        right = last - 1;
        goto loop;
    }
}

FUNCTION(fun_sortby)
{
    char sep;
    varargs_preamble(3);

    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    strcpy(ucomp_buff, atext);
    ucomp_executor = thing;
    ucomp_caller   = executor;
    ucomp_enactor  = enactor;

    char *list = alloc_lbuf("fun_sortby");
    strcpy(list, fargs[1]);
    char *ptrs[LBUF_SIZE / 2];
    int nptrs = list2arr(ptrs, LBUF_SIZE / 2, list, sep);

    if (nptrs > 1)
    {
        sane_qsort((void **)ptrs, 0, nptrs - 1, u_comp);
    }

    arr2list(ptrs, nptrs, buff, bufc, sep);
    free_lbuf(list);
    free_lbuf(atext);
}

// fun_last: Returns last word in a string. Borrowed from TinyMUSH 2.2.
//
FUNCTION(fun_last)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs <= 0)
    {
        return;
    }

    char sep;
    varargs_preamble(2);

    // Trim leading spaces.
    //
    int nLen = strlen(fargs[0]);
    char *pStart = trim_space_sep_LEN(fargs[0], nLen, sep, &nLen);
    char *pEnd = pStart + nLen - 1;

    if (sep == ' ')
    {
        // We're dealing with spaces, so trim off the trailing spaces.
        //
        while (pStart <= pEnd && *pEnd == ' ')
        {
            pEnd--;
        }
        pEnd[1] = '\0';
    }

    // Find the separator nearest the end.
    //
    char *p = pEnd;
    while (pStart <= p && *p != sep)
    {
        p--;
    }

    // Return the last token.
    //
    nLen = pEnd - p;
    safe_copy_buf(p+1, nLen, buff, bufc);
}

// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_matchall)
{
    int wcount;
    char *r, *s, *old, sep, tbuf[8];

    varargs_preamble(3);
    old = *bufc;

    // Check each word individually, returning the word number of all that
    // match. If none match, return 0.
    //
    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do
    {
        r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            mux_ltoa(wcount, tbuf);
            if (old != *bufc)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_str(tbuf, buff, bufc);
        }
        wcount++;
    } while (s);

    if (*bufc == old)
    {
        safe_chr('0', buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_ports: Returns a list of ports for a user.
// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_ports)
{
    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        if (target == executor || Wizard(executor))
        {
            if (Connected(target))
            {
                make_portlist(executor, target, buff, bufc);
            }
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_mix: Like map, but operates on up to ten lists simultaneously, passing
 * the elements as %0 - %10.
 * Borrowed from PennMUSH 1.50, upgraded by RhostMUSH.
 */
FUNCTION(fun_mix)
{
    // Check to see if we have an appropriate number of arguments.
    // If there are more than three arguments, the last argument is
    // ALWAYS assumed to be a delimiter.
    //
    char sep;
    int lastn;

    if (nfargs < 4)
    {
        sep = ' ';
        lastn = nfargs - 1;
    }
    else 
    {
        varargs_preamble(nfargs);
        lastn = nfargs - 2;
    }

    // Get the attribute, check the permissions. 
    // 
    dbref thing;
    char *atext;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc)) 
    {
        return;
    }

    char *cp[10];
    int i;
    for (i = 0; i < lastn; i++)
    {
        cp[i] = NULL;
    }

    // Process the lists, one element at a time.
    //
    for (i = 1; i <= lastn; i++) 
    {
        cp[i-1] = trim_space_sep(fargs[i], sep);
    }
    int twords;
    int nwords = countwords(cp[1], sep);
    for (i = 2; i<= lastn; i++) 
    {
        twords = countwords(cp[i-1], sep);
        if (twords > nwords)
           nwords = twords;
    }
    char *oldp = *bufc;
    char *atextbuf = alloc_lbuf("fun_mix");
    char *str, *os[10];
    for (int wc = 0; wc < nwords; wc++) 
    {
        if (*bufc != oldp)
        {
            safe_chr(sep, buff, bufc);
        }
        for (i = 1; i <= lastn; i++) 
        {
            os[i - 1] = split_token(&cp[i - 1], sep);
        }
        strcpy(atextbuf, atext);
        str = atextbuf;
        mux_exec(buff, bufc, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &(os[0]), lastn);
    }
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

/* ---------------------------------------------------------------------------
 * fun_foreach: like map(), but it operates on a string, rather than on a list,
 * calling a user-defined function for each character in the string.
 * No delimiter is inserted between the results.
 * Borrowed from TinyMUSH 2.2
 */
FUNCTION(fun_foreach)
{
    if (  nfargs != 2
       && nfargs != 4)
    {
        safe_str("#-1 FUNCTION (FOREACH) EXPECTS 2 OR 4 ARGUMENTS", buff, bufc);
        return;
    }

    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    char *str;
    char cbuf[2], prev = '\0';
    char *atextbuf = alloc_lbuf("fun_foreach");
    char *cp = trim_space_sep(fargs[1], ' ');

    char *bp = cbuf;

    cbuf[1] = '\0';

    if (nfargs == 4)
    {
        bool flag = false;
        while (  cp
              && *cp
              && mudstate.func_invk_ctr < mudconf.func_invk_lim)
        {
            cbuf[0] = *cp++;

            if (flag)
            {
                if (  cbuf[0] == *fargs[3]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = false;
                    continue;
                }
            }
            else
            {
                if (  cbuf[0] == *fargs[2]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = true;
                    continue;
                }
                else
                {
                    safe_chr(cbuf[0], buff, bufc);
                    continue;
                }
            }

            strcpy(atextbuf, atext);
            str = atextbuf;
            mux_exec(buff, bufc, thing, executor, enactor,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &bp, 1);
            prev = cbuf[0];
        }
    }
    else
    {
        while (  cp
              && *cp
              && mudstate.func_invk_ctr < mudconf.func_invk_lim)
        {
            cbuf[0] = *cp++;

            strcpy(atextbuf, atext);
            str = atextbuf;
            mux_exec(buff, bufc, thing, executor, enactor,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &bp, 1);
        }
    }
    free_lbuf(atextbuf);
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_munge: combines two lists in an arbitrary manner.
 * Borrowed from TinyMUSH 2.2
 */
FUNCTION(fun_munge)
{
    char sep;
    varargs_preamble(4);

    // Find our object and attribute.
    //
    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    int nptrs1, nptrs2, nresults, i, j;
    char *list1, *list2, *rlist, *bp, *str, *oldp;
    char *ptrs1[LBUF_SIZE / 2], *ptrs2[LBUF_SIZE / 2], *results[LBUF_SIZE / 2];
    char *uargs[2], isep[2] = { '\0', '\0' };

    oldp = *bufc;

    // Copy our lists and chop them up.
    //
    list1 = alloc_lbuf("fun_munge.list1");
    list2 = alloc_lbuf("fun_munge.list2");
    strcpy(list1, fargs[1]);
    strcpy(list2, fargs[2]);
    nptrs1 = list2arr(ptrs1, LBUF_SIZE / 2, list1, sep);
    nptrs2 = list2arr(ptrs2, LBUF_SIZE / 2, list2, sep);

    if (nptrs1 != nptrs2)
    {
        safe_str("#-1 LISTS MUST BE OF EQUAL SIZE", buff, bufc);
        free_lbuf(atext);
        free_lbuf(list1);
        free_lbuf(list2);
        return;
    }

    // Call the u-function with the first list as %0.
    //
    bp = rlist = alloc_lbuf("fun_munge");
    str = atext;
    isep[0] = sep;
    uargs[0] = fargs[1];
    uargs[1] = isep;
    mux_exec(rlist, &bp, executor, caller, enactor,
             EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, uargs, 2);
    *bp = '\0';

    // Now that we have our result, put it back into array form.
    // Search through list1 until we find the element position, then
    // copy the corresponding element from list2.
    //
    nresults = list2arr(results, LBUF_SIZE / 2, rlist, sep);

    for (i = 0; i < nresults; i++)
    {
        for (j = 0; j < nptrs1; j++)
        {
            if (!strcmp(results[i], ptrs1[j]))
            {
                if (*bufc != oldp)
                {
                    safe_chr(sep, buff, bufc);
                }
                safe_str(ptrs2[j], buff, bufc);
                ptrs1[j][0] = '\0';
                break;
            }
        }
    }
    free_lbuf(atext);
    free_lbuf(list1);
    free_lbuf(list2);
    free_lbuf(rlist);
}

FUNCTION(fun_die)
{
    int n = mux_atol(fargs[0]);
    int die = mux_atol(fargs[1]);

    if ((n == 0) || (die <= 0))
    {
        safe_chr('0', buff, bufc);
        return;
    }

    if ((n < 1) || (n > 100))
    {
        safe_range(buff, bufc);
        return;
    }
    int total = 0;
    for (int count = 0; count < n; count++)
    {
        total += RandomINT32(1, die);
    }

    safe_ltoa(total, buff, bufc);
}

FUNCTION(fun_lrand)
{
    char sep;
    if (!delim_check(fargs, nfargs, 4, &sep, buff, bufc, false,
        executor, caller, enactor, cargs, ncargs, true))
    {
        return;
    }

    int n_times = mux_atol(fargs[2]);
    if (n_times < 1)
    {
        return;
    }
    if (n_times > LBUF_SIZE)
    {
        n_times = LBUF_SIZE;
    }
    INT32 iLower = mux_atol(fargs[0]);
    INT32 iUpper = mux_atol(fargs[1]);

    if (iLower <= iUpper)
    {
        for (int i = 0; i < n_times-1; i++)
        {
            INT32 val = RandomINT32(iLower, iUpper);
            safe_ltoa(val, buff, bufc);
            print_sep(sep, buff, bufc);
        }
        INT32 val = RandomINT32(iLower, iUpper);
        safe_ltoa(val, buff, bufc);
    }
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lit)
{
    // Just returns the argument, literally.
    //
    safe_str(fargs[0], buff, bufc);
}

// shl() and shr() borrowed from PennMUSH 1.50
//
FUNCTION(fun_shl)
{
    if (is_integer(fargs[0], NULL) && is_integer(fargs[1], NULL))
        safe_ltoa(mux_atol(fargs[0]) << mux_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
}

FUNCTION(fun_shr)
{
    if (is_integer(fargs[0], NULL) && is_integer(fargs[1], NULL))
        safe_ltoa(mux_atol(fargs[0]) >> mux_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
}

FUNCTION(fun_band)
{
    if (is_integer(fargs[0], NULL) && is_integer(fargs[1], NULL))
        safe_ltoa(mux_atol(fargs[0]) & mux_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
}

FUNCTION(fun_bor)
{
    if (is_integer(fargs[0], NULL) && is_integer(fargs[1], NULL))
        safe_ltoa(mux_atol(fargs[0]) | mux_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
}

FUNCTION(fun_bnand)
{
    if (is_integer(fargs[0], NULL) && is_integer(fargs[1], NULL))
        safe_ltoa(mux_atol(fargs[0]) & ~(mux_atol(fargs[1])), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
}

FUNCTION(fun_crc32)
{
    UINT32 ulCRC32 = 0;
    for (int i = 0; i < nfargs; i++)
    {
        int n = strlen(fargs[i]);
        ulCRC32 = CRC32_ProcessBuffer(ulCRC32, fargs[i], n);
    }
    safe_i64toa(ulCRC32, buff, bufc);
}

FUNCTION(fun_dumping)
{
#ifdef WIN32
    safe_chr('0', buff, bufc);
#else // WIN32
    safe_bool(mudstate.dumping, buff, bufc);
#endif // WIN32
}

// The following table contains 64 symbols, so this supports -a-
// radix-64 encoding. It is not however 'unix-to-unix' encoding.
// All of the following characters are valid for an attribute
// name, but not for the first character of an attribute name.
//
static char aRadixTable[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@$";

FUNCTION(fun_unpack)
{
    // Validate radix if present.
    //
    INT64 iRadix = 64;
    if (nfargs == 2)
    {
        if (  !is_integer(fargs[1], NULL)
           || (iRadix = mux_atoi64(fargs[1])) < 2
           || 64 < iRadix)
        {
            safe_str("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64", buff, bufc);
            return;
        }
    }

    // Build Table of valid characters.
    //
    char MatchTable[256];
    memset(MatchTable, 0, sizeof(MatchTable));
    for (int i = 0; i < iRadix; i++)
    {
        MatchTable[aRadixTable[i]] = i+1;
    }

    // Validate that first argument contains only characters from the
    // subset of permitted characters.
    //
    char *pString = fargs[0];
    INT64 sum;
    int c;
    int LeadingCharacter;

    // Leading whitespace
    //
    while (mux_isspace[(unsigned char)*pString])
    {
        pString++;
    }

    // Possible sign
    //
    LeadingCharacter = c = *pString++;
    if (c == '-' || c == '+')
    {
        c = *pString++;
    }

    sum = 0;

    // Convert symbols
    //
    int iValue;
    while ((iValue = MatchTable[(unsigned int)c]))
    {
        sum = iRadix * sum + iValue - 1;
        c = *pString++;
    }

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        sum = -sum;
    }
    safe_i64toa(sum, buff, bufc);
}

FUNCTION(fun_pack)
{
    // Validate the arguments are numeric.
    //
    if (  !is_integer(fargs[0], NULL)
       || (nfargs == 2 && !is_integer(fargs[1], NULL)))
    {
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
        return;
    }
    INT64 val = mux_atoi64(fargs[0]);

    // Validate the radix is between 2 and 64.
    //
    INT64 iRadix = 64;
    if (nfargs == 2)
    {
        iRadix = mux_atoi64(fargs[1]);
        if (iRadix < 2 || 64 < iRadix)
        {
            safe_str("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64", buff, bufc);
            return;
        }
    }

    char TempBuffer[76]; // 1 '-', 63 binary digits, 1 '\0', 11 for safety.
    char *p = TempBuffer;

    // Handle sign.
    //
    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }

    char *q = p;
    while (val > iRadix-1)
    {
        INT64 iDiv  = val / iRadix;
        INT64 iTerm = val - iDiv * iRadix;
        val = iDiv;
        *p++ = aRadixTable[iTerm];
    }
    *p++ = aRadixTable[val];

    int nLength = p - TempBuffer;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    safe_copy_buf(TempBuffer, nLength, buff, bufc);
}

FUNCTION(fun_strcat)
{
    int i;
    for (i = 0; i < nfargs; i++)
    {
        safe_str(fargs[i], buff, bufc);
    }
}

// grep() and grepi() code borrowed from PennMUSH 1.50
//
char *grep_util(dbref player, dbref thing, char *pattern, char *lookfor, int len, bool insensitive)
{
    // Returns a list of attributes which match <pattern> on <thing>
    // whose contents have <lookfor>.
    //
    dbref aowner;
    char *tbuf1, *buf;
    char *bp, *bufc;
    int ca, aflags;

    tbuf1 = alloc_lbuf("grep_util");
    bufc = buf = alloc_lbuf("grep_util.parse_attrib");
    bp = tbuf1;
    safe_tprintf_str(buf, &bufc, "#%d/%s", thing, pattern);
    olist_push();
    if (parse_attrib_wild(player, buf, &thing, false, false, true))
    {
        BMH_State bmhs;
        if (insensitive)
        {
            BMH_PrepareI(&bmhs, len, lookfor);
        }
        else
        {
            BMH_Prepare(&bmhs, len, lookfor);
        }

        for (ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            size_t nText;
            char *attrib = atr_get_LEN(thing, ca, &aowner, &aflags, &nText);
            int i;
            if (insensitive)
            {
                i = BMH_ExecuteI(&bmhs, len, lookfor, nText, attrib);
            }
            else
            {
                i = BMH_Execute(&bmhs, len, lookfor, nText, attrib);
            }
            if (i >= 0)
            {
                if (bp != tbuf1)
                {
                    safe_chr(' ', tbuf1, &bp);
                }
                ATTR *ap = atr_num(ca);
                const char *pName = "(WARNING: Bad Attribute Number)";
                if (ap)
                {
                    pName = ap->name;
                }
                safe_str(pName, tbuf1, &bp);
            }
            free_lbuf(attrib);
        }
    }
    free_lbuf(buf);
    *bp = '\0';
    olist_pop();
    return tbuf1;
}

void grep_handler(char *buff, char **bufc, dbref executor, char *fargs[], 
                   bool bCaseInsens)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }

    if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Make sure there's an attribute and a pattern
    //
    if (!fargs[1] || !*fargs[1])
    {
        safe_str("#-1 NO SUCH ATTRIBUTE", buff, bufc);
        return;
    }
    if (!fargs[2] || !*fargs[2])
    {
        safe_str("#-1 INVALID GREP PATTERN", buff, bufc);
        return;
    }
    char *tp = grep_util(executor, it, fargs[1], fargs[2], strlen(fargs[2]), bCaseInsens);
    safe_str(tp, buff, bufc);
    free_lbuf(tp);
}

FUNCTION(fun_grep)
{
    grep_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_grepi)
{
    grep_handler(buff, bufc, executor, fargs, true);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamax)
{
    char *amax = fargs[0];
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp(amax, fargs[i]) < 0)
        {
            amax = fargs[i];
        }
    }
    safe_str(amax, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamin)
{
    char *amin = fargs[0];
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp(amin, fargs[i]) > 0)
        {
            amin = fargs[i];
        }
    }
    safe_str(amin, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_valid)
{
    // Checks to see if a given <something> is valid as a parameter of
    // a given type (such as an object name)
    //
    int nValidName;
    bool bValid;
    if (!*fargs[0] || !*fargs[1])
    {
        bValid = false;
    }
    else if (!mux_stricmp(fargs[0], "name"))
    {
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], "attrname"))
    {
        MakeCanonicalAttributeName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], "playername"))
    {
        bValid = ValidatePlayerName(fargs[1]);
    }
    else
    {
        safe_nothing(buff, bufc);
        return;
    }
    safe_bool(bValid, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_hastype)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    bool bResult = false;
    switch (mux_tolower[(unsigned char)fargs[1][0]])
    {
    case 'r':

        bResult = isRoom(it);
        break;

    case 'e':

        bResult = isExit(it);
        break;

    case 'p':

        bResult = isPlayer(it);
        break;

    case 't':

        bResult = isThing(it);
        break;

    default:

        safe_str("#-1 NO SUCH TYPE", buff, bufc);
        break;
    }
    safe_bool(bResult, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lparent)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    if (!ItemToList_AddInteger(&pContext, it))
    {
        ItemToList_Final(&pContext);
        return;
    }

    dbref par = Parent(it);

    int iNestLevel = 1;
    while (  Good_obj(par)
          && Examinable(executor, it)
          && iNestLevel < mudconf.parent_nest_lim)
    {
        if (!ItemToList_AddInteger(&pContext, par))
        {
            break;
        }
        it = par;
        par = Parent(par);
        iNestLevel++;
    }
    ItemToList_Final(&pContext);
}

// stacksize - returns how many items are stuffed onto an object stack
//
int stacksize(dbref doer)
{
    int i;
    STACK *sp;
    for (i = 0, sp = Stack(doer); sp != NULL; sp = sp->next, i++)
    {
        // Nothing
        ;
    }
    return i;
}

FUNCTION(fun_lstack)
{
    STACK *sp;
    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    for (sp = Stack(doer); sp != NULL; sp = sp->next)
    {
        safe_str(sp->data, buff, bufc);
        safe_chr(' ', buff, bufc);
    }

    if (sp)
    {
        (*bufc)--;
    }
}

// stack_clr - clear the stack.
//
void stack_clr(dbref obj)
{
    // Clear the stack.
    //
    STACK *sp, *next;
    for (sp = Stack(obj); sp != NULL; sp = next)
    {
        next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
    s_Stack(obj, NULL);
}

FUNCTION(fun_empty)
{
    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    stack_clr(doer);
}

FUNCTION(fun_items)
{
    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    safe_ltoa(stacksize(doer), buff, bufc);
}

FUNCTION(fun_peek)
{
    STACK *sp;
    dbref doer;
    int count, pos;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }

    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str("#-1 POSITION TOO LARGE", buff, bufc);
        return;
    }
    count = 0;
    sp = Stack(doer);
    while (count != pos)
    {
        if (sp == NULL)
        {
            return;
        }
        count++;
        sp = sp->next;
    }

    safe_str(sp->data, buff, bufc);
}

FUNCTION(fun_pop)
{
    dbref doer;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }
    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int pos;
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }
    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str("#-1 POSITION TOO LARGE", buff, bufc);
        return;
    }

    STACK *sp = Stack(doer);
    STACK *prev = NULL;
    int count = 0;
    while (count != pos)
    {
        if (sp == NULL)
        {
            return;
        }
        prev = sp;
        sp = sp->next;
        count++;
    }

    safe_str(sp->data, buff, bufc);
    if (count == 0)
    {
        s_Stack(doer, sp->next);
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
    else
    {
        prev->next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = NULL;
    }
}

FUNCTION(fun_push)
{
    dbref doer;
    char *data;

    if (nfargs <= 1 || !*fargs[1])
    {
        doer = executor;
        data = fargs[0];
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
        data = fargs[1];
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (stacksize(doer) >= mudconf.stack_limit)
    {
        safe_str("#-1 STACK SIZE EXCEEDED", buff, bufc);
        return;
    }
    STACK *sp = (STACK *)MEMALLOC(sizeof(STACK));
    (void)ISOUTOFMEMORY(sp);
    sp->next = Stack(doer);
    sp->data = alloc_lbuf("push");
    strcpy(sp->data, data);
    s_Stack(doer, sp);
}

/* ---------------------------------------------------------------------------
 * fun_regmatch: Return 0 or 1 depending on whether or not a regular
 * expression matches a string. If a third argument is specified, dump
 * the results of a regexp pattern match into a set of arbitrary r()-registers.
 *
 * regmatch(string, pattern, list of registers)
 * If the number of matches exceeds the registers, those bits are tossed
 * out.
 * If -1 is specified as a register number, the matching bit is tossed.
 * Therefore, if the list is "-1 0 3 5", the regexp $0 is tossed, and
 * the regexp $1, $2, and $3 become r(0), r(3), and r(5), respectively.
 */

void real_regmatch(const char *search, const char *pattern, char *registers,
                   int nfargs, char *buff, char **bufc, bool cis)
{
    const char *errptr;
    int erroffset;
    const int ovecsize = 111;
    int ovec[ovecsize];

    pcre *re = pcre_compile(pattern, cis ? PCRE_CASELESS : 0,
        &errptr, &erroffset, NULL);
    if (!re)
    {
        // Matching error.
        //
        safe_str("#-1 REGEXP ERROR ", buff, bufc);
        safe_str(errptr, buff, bufc);
        return;
    }

    int matched = pcre_exec(re, NULL, search, strlen(search), 0, 0,
        ovec, ovecsize);
    safe_bool(matched > 0, buff, bufc);

    // If we don't have a third argument, we're done.
    //
    if (nfargs != 3)
    {
        MEMFREE(re);
        return;
    }

    // We need to parse the list of registers. If a register is
    // mentioned in the list, then either fill the register with the
    // subexpression, or if there wasn't a match, clear it.
    //
    const int NSUBEXP = 36;
    char *qregs[NSUBEXP];
    int nqregs = list2arr(qregs, NSUBEXP, registers, ' ');
    for (int i = 0; i < nqregs; i++)
    {
        int curq;
        if (  qregs[i]
           && *qregs[i]
           && (curq = mux_RegisterSet[(unsigned char)qregs[i][0]]) != -1
           && qregs[i][1] == '\0'
           && curq < MAX_GLOBAL_REGS)
        {
            if (!mudstate.global_regs[curq])
            {
                mudstate.global_regs[curq] = alloc_lbuf("fun_regmatch");
            }
            int len = 0;
            if (matched >= i - 1 && ovec[i*2] >= 0)
            {
                // We have a subexpression.
                //
                len = ovec[(i*2)+1] - ovec[i*2];
                if (len > LBUF_SIZE - 1)
                {
                    len = LBUF_SIZE - 1;
                }
                else if (len < 0)
                {
                    len = 0;
                }
                memcpy(mudstate.global_regs[curq], search + ovec[i*2], len);
            }
            mudstate.global_regs[curq][len] = '\0';
            mudstate.glob_reg_len[curq] = len;
        }
    }
    MEMFREE(re);
}

FUNCTION(fun_regmatch)
{
    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, false);
}

FUNCTION(fun_regmatchi)
{
    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, true);
}


/* ---------------------------------------------------------------------------
 * regrab(), regraball(). Like grab() and graball(), using a regular expression
 * instead of a wildcard pattern. The versions ending in i are case-insensitive.
 */

void real_regrab(char *search, const char *pattern, char sep, char *buff,
                 char **bufc, bool cis, bool all)
{
    pcre *re;
    pcre_extra *study = NULL;
    const char *errptr;
    int erroffset;
    const int ovecsize = 111;
    int ovec[ovecsize];

    re = pcre_compile(pattern, cis ? PCRE_CASELESS : 0,
        &errptr, &erroffset, NULL);
    if (!re)
    {
        // Matching error.
        //
        safe_str("#-1 REGEXP ERROR ", buff, bufc);
        safe_str(errptr, buff, bufc);
        return;
    }

    if (all)
    {
        study = pcre_study(re, 0, &errptr);
    }

    bool first = true;
    char *s = trim_space_sep(search, sep);
    do
    {
        char *r = split_token(&s, sep);
        if (pcre_exec(re, study, r, strlen(r), 0, 0, ovec, ovecsize) >= 1)
        {
            if (first) 
            {
                first = false;
            }
            else
            {
                safe_chr(sep, buff, bufc);
            }
            safe_str(r, buff, bufc);
            if (!all)
            {
                break;
            }
        }
    } while (s);

    MEMFREE(re);
    if (study)
    {
        MEMFREE(study);
    }
}

FUNCTION(fun_regrab) 
{
    char sep;
    varargs_preamble(3);

    real_regrab(fargs[0], fargs[1], sep, buff, bufc, false, false);
}

FUNCTION(fun_regrabi) 
{
    char sep;
    varargs_preamble(3);

    real_regrab(fargs[0], fargs[1], sep, buff, bufc, true, false);
}

FUNCTION(fun_regraball) 
{
    char sep;
    varargs_preamble(3);

    real_regrab(fargs[0], fargs[1], sep, buff, bufc, false, true);
}

FUNCTION(fun_regraballi) 
{
    char sep;
    varargs_preamble(3);

    real_regrab(fargs[0], fargs[1], sep, buff, bufc, true, true);
}


/* ---------------------------------------------------------------------------
 * fun_translate: Takes a string and a second argument. If the second argument
 * is 0 or s, control characters are converted to spaces. If it's 1 or p,
 * they're converted to percent substitutions.
 */

FUNCTION(fun_translate)
{
    int ch = fargs[1][0];
    bool type = (ch == 'p' || ch == '1');
    safe_str(translate_string(fargs[0], type), buff, bufc);
}


class CBitField
{
    unsigned int nBitsPer;
    unsigned int nShift;
    unsigned int nMask;
    unsigned int nMaximum;
    size_t  nInts;
    UINT32 *pInts;
    UINT32 *pMasks;

public:
    CBitField(unsigned int max);
    ~CBitField(void);
    void ClearAll(void);
    void Set(unsigned int i);
    void Clear(unsigned int i);
    bool IsSet(unsigned int i);
};

// Construct a CBitField to hold (nMaximum_arg+1) bits numbered 0 through
// nMaximum_arg.
//
CBitField::CBitField(unsigned int nMaximum_arg)
{
    nInts  = 0;
    pInts  = NULL;
    pMasks = NULL;
    if (0 < nMaximum_arg)
    {
        nMaximum = nMaximum_arg;
        nBitsPer = sizeof(UINT32)*8;

        // Calculate Shift
        //
        nShift = 0;
        unsigned int i = 1;
        while (i < nBitsPer)
        {
            nShift++;
            i <<= 1;
        }

        // Calculate Mask
        //
        nMask = nBitsPer - 1;

        // Allocate array of UINT32s.
        //
        nInts    = (nMaximum+nBitsPer) >> nShift;
        pMasks   = (UINT32 *)MEMALLOC((nInts+nBitsPer)*sizeof(UINT32));
        (void)ISOUTOFMEMORY(pMasks);
        pInts    = pMasks + nBitsPer;

        // Calculate all possible single bits.
        //
        for (i = 0; i < nBitsPer; i++)
        {
            pMasks[i] = ((UINT32)1) << i;
        }
    }
}

CBitField::~CBitField(void)
{
    pInts  = NULL;
    if (pMasks)
    {
        MEMFREE(pMasks);
        pMasks = NULL;
    }
}

void CBitField::ClearAll(void)
{
    memset(pInts, 0, nInts*sizeof(UINT32));
}

void CBitField::Set(unsigned int i)
{
    if (i <= nMaximum)
    {
        pInts[i>>nShift] |= pMasks[i&nMask];
    }
}

void CBitField::Clear(unsigned int i)
{
    if (i <= nMaximum)
    {
        pInts[i>>nShift] &= ~pMasks[i&nMask];
    }
}

bool CBitField::IsSet(unsigned int i)
{
    if (i <= nMaximum)
    {
        if (pInts[i>>nShift] & pMasks[i&nMask])
        {
            return true;
        }
    }
    return false;
}


// -------------------------------------------------------------------------
// fun_lrooms:  Takes a dbref (room), an int (N), and an optional bool (B).
//
// MUX Syntax:  lrooms(<room> [,<N>[,<B>]])
//
// Returns a list of rooms <N>-levels deep from <room>. If <B> == 1, it will
//   return all room dbrefs between 0 and <N> levels, while <B> == 0 will
//   return only the room dbrefs on the Nth level. The default is to show all
//   rooms dbrefs between 0 and <N> levels.
//
// Written by Marlek.  Idea from RhostMUSH.
//
static void room_list
(
    dbref player,
    dbref enactor,
    dbref room,
    CBitField &bfTraverse,
    CBitField &bfReport,
    int   level,
    int   maxlevels,
    bool  showall
)
{
    // Make sure the player can really see this room from their location.
    //
    if (  (  level == maxlevels
          || showall)
       && (  Examinable(player, room)
          || Location(player) == room
          || room == enactor))
    {
        bfReport.Set(room);
    }

    // If the Nth level has been reach, stop this branch in the recursion
    //
    if (level >= maxlevels)
    {
        return;
    }

    // Return info for all parent levels.
    //
    int lev;
    dbref parent;
    ITER_PARENTS(room, parent, lev)
    {
        // Look for exits at each level.
        //
        if (!Has_exits(parent))
        {
            continue;
        }
        int key = 0;
        if (Examinable(player, parent))
        {
            key |= VE_LOC_XAM;
        }
        if (Dark(parent))
        {
            key |= VE_LOC_DARK;
        }
        if (Dark(room))
        {
            key |= VE_BASE_DARK;
        }

        dbref thing;
        DOLIST(thing, Exits(parent))
        {
            dbref loc = Location(thing);
            if (  exit_visible(thing, player, key)
               && !bfTraverse.IsSet(loc))
            {
                bfTraverse.Set(loc);
                room_list(player, enactor, loc, bfTraverse, bfReport,
                    (level + 1), maxlevels, showall);
            }
        }
    }
}

FUNCTION(fun_lrooms)
{
    dbref room = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(room))
    {
        safe_match_result(room, buff, bufc);
        return;
    }
    else if (!isRoom(room))
    {
        safe_str("#-1 FIRST ARGUMENT MUST BE A ROOM", buff, bufc);
        return;
    }

    int N = 1;
    if (nfargs >= 2)
    {
        N = mux_atol(fargs[1]);
        if (N < 0)
        {
            safe_str("#-1 SECOND ARGUMENT MUST BE A POSITIVE NUMBER",
                buff, bufc);
            return;
        }
        else if (N > 50)
        {
            // Maybe this can be turned into a config parameter to prevent
            // misuse by putting in really large values.
            //
            safe_str("#-1 SECOND ARGUMENT IS TOO LARGE", buff, bufc);
            return;
        }
    }

    bool B = true;
    if (nfargs == 3)
    {
        B = xlate(fargs[2]);
    }

    CBitField bfReport(mudstate.db_top-1);
    CBitField bfTraverse(mudstate.db_top-1);
    bfReport.ClearAll();
    bfTraverse.ClearAll();

    bfTraverse.Set(room);
    room_list(executor, enactor, room, bfTraverse, bfReport, 0, N, B);
    bfReport.Clear(room);

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (  bfReport.IsSet(i)
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}
