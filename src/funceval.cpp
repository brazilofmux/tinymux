// funceval.cpp -- MUX function handlers.
//
// $Id: funceval.cpp,v 1.82 2002-01-15 05:19:59 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <limits.h>
#include <math.h>

#include "attrs.h"
#include "match.h"
#include "command.h"
#include "functions.h"
#include "misc.h"
#include "ansi.h"
#include "comsys.h"
#ifdef RADIX_COMPRESSION
#include "radix.h"
#endif

/* Note: Many functions in this file have been taken, whole or in part, from
 * PennMUSH 1.50, and TinyMUSH 2.2, for softcode compatibility. The
 * maintainers of MUX would like to thank those responsible for PennMUSH 1.50
 * and TinyMUSH 2.2, and hope we have adequately noted in the source where
 * credit is due.
 */

extern NAMETAB indiv_attraccess_nametab[];
extern char *FDECL(next_token, (char *, char));
extern char *FDECL(split_token, (char **, char));
extern int FDECL(countwords, (char *, char));
extern int FDECL(check_read_perms, (dbref, dbref, ATTR *, int, int, char *, char **));
extern void arr2list(char *arr[], int alen, char *list, char **bufc, char sep);
extern void FDECL(make_portlist, (dbref, dbref, char *, char **));

FUNCTION(fun_cwho)
{
    struct channel *ch = select_channel(fargs[0]);
    if (!ch)
    {
        safe_str("#-1 CHANNEL NOT FOUND", buff, bufc);
        return;
    }
    if (!mudconf.have_comsys || (!Comm_All(player) && (player != ch->charge_who)))
    {
        safe_noperm(buff, bufc);
        return;
    }
    DTB pContext;
    struct comuser *user;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    for (user = ch->on_users; user; user = user->on_next)
    {
        if (  Connected(user->who)
           && !DbrefToBuffer_Add(&pContext, user->who))
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
}

FUNCTION(fun_beep)
{
    safe_chr(BEEP_CHAR, buff, bufc);
}

// This function was originally taken from PennMUSH 1.50
//
FUNCTION(fun_ansi)
{
    extern const char *ColorTable[256];

    char *s = fargs[0];
    char *bufc_save = *bufc;

    while (*s)
    {
        const char *pColor = ColorTable[(unsigned char)*s];
        if (pColor)
        {
            safe_str(pColor, buff, bufc);
        }
        s++;
    }
    safe_str(fargs[1], buff, bufc);
    **bufc = '\0';

    // ANSI_NORMAL is guaranteed to be written on the end.
    //
    char Temp[LBUF_SIZE];
    int nVisualWidth;
    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    int nLen = ANSI_TruncateToField(bufc_save, nBufferAvailable, Temp, sizeof(Temp),
        &nVisualWidth, ANSI_ENDGOAL_NORMAL);
    memcpy(bufc_save, Temp, nLen);
    *bufc = bufc_save + nLen;
}

FUNCTION(fun_zone)
{
    dbref it;

    if (!mudconf.have_zones) {
        return;
    }
    it = match_thing(player, fargs[0]);
    if (it == NOTHING || !Examinable(player, it)) {
        safe_nothing(buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, "#%d", Zone(it));
}

#ifdef SIDE_EFFECT_FUNCTIONS

static int check_command(dbref player, char *name, char *buff, char **bufc)
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
            safe_str(NOPERM_MESSAGE, buff, bufc);
            return 1;
        }
    }
    return 0;
}

FUNCTION(fun_link)
{
    if (check_command(player, "@link", buff, bufc))
        return;
    do_link(player, cause, 0, 2, fargs[0], fargs[1]);
}

FUNCTION(fun_tel)
{
    if (check_command(player, "@teleport", buff, bufc))
        return;
    do_teleport(player, cause, 0, 2, fargs[0], fargs[1]);
}

FUNCTION(fun_pemit)
{
    if (check_command(player, "@pemit", buff, bufc))
    {
        return;
    }
    do_pemit_list(player, PEMIT_PEMIT, FALSE, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_oemit)
{
    if (check_command(player, "@oemit", buff, bufc))
    {
        return;
    }
    do_pemit_single(player, PEMIT_OEMIT, FALSE, 0, fargs[0], 0, fargs[1]);
}

FUNCTION(fun_emit)
{
    if (check_command(player, "@emit", buff, bufc))
    {
        return;
    }
    do_say(player, player, SAY_EMIT, fargs[0]);
}

FUNCTION(fun_remit)
{
    if (check_command(player, "@pemit", buff, bufc))
    {
        return;
    }
    do_pemit_single(player, PEMIT_PEMIT, TRUE, 0, fargs[0], 0, fargs[1]);
}

// ------------------------------------------------------------------------
// fun_create: Creates a room, thing or exit.
//
FUNCTION(fun_create)
{
    dbref thing;
    int cost;
    char sep, *name;

    varargs_preamble(3);
    name = fargs[0];

    if (!name || !*name)
    {
        safe_str("#-1 ILLEGAL NAME", buff, bufc);
        return;
    }
    if (nfargs >= 3 && *fargs[2])
        sep = *fargs[2];
    else
        sep = 't';

    switch (sep)
    {
    case 'r':

        if (check_command(player, "@dig", buff, bufc))
        {
            return;
        }
        thing = create_obj(player, TYPE_ROOM, name, 0);
        break;

    case 'e':

        if (check_command(player, "@open", buff, bufc))
        {
            return;
        }
        thing = create_obj(player, TYPE_EXIT, name, 0);
        if (thing != NOTHING)
        {
            s_Exits(thing, player);
            s_Next(thing, Exits(player));
            s_Exits(player, thing);
        }
        break;

    default:

        if (check_command(player, "@create", buff, bufc))
        {
            return;
        }
        if (*fargs[1])
        {
            cost = Tiny_atol(fargs[1]);
            if (cost < mudconf.createmin || cost > mudconf.createmax)
            {
                safe_str("#-1 COST OUT OF RANGE", buff, bufc);
                return;
            }
        }
        else
        {
            cost = mudconf.createmin;
        }
        thing = create_obj(player, TYPE_THING, name, cost);
        if (thing != NOTHING)
        {
            move_via_generic(thing, player, NOTHING, 0);
            s_Home(thing, new_home(player));
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
    dbref aowner;
    int aflags, could_hear;
    ATTR *attr;

    if (isGarbage(thing))
    {
        safe_noperm(buff, bufc);
        notify_quiet(player, "You shouldn't be rummaging through the garbage.");
        return;
    }
    attr = atr_num(attrnum);
    atr_pget_info(thing, attrnum, &aowner, &aflags);
    if (attr && Set_attr(player, thing, attr, aflags))
    {
        if (  (attr->check != NULL)
           && (!(*attr->check) (0, player, thing, attrnum, attrtext)))
        {
            safe_noperm(buff, bufc);
            return;
        }
        could_hear = Hearer(thing);
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
    dbref thing, thing2, aowner;
    char *p, *buff2;
    int atr, atr2, aflags, clear, flagvalue, could_hear;
    ATTR *attr, *attr2;

    if (check_command(player, "@set", buff, bufc))
    {
        return;
    }

    // obj/attr form?
    //
    if (parse_attrib(player, fargs[0], &thing, &atr))
    {
        if (atr != NOTHING)
        {
            // Must specify flag name
            //
            if (!*fargs[1])
            {
                safe_str("#-1 UNSPECIFIED PARAMETER", buff, bufc);
            }

            // Are we clearing?
            //
            clear = 0;
            if (*fargs[0] == NOT_TOKEN)
            {
                fargs[0]++;
                clear = 1;
            }

            // valid attribute flag?
            //
            flagvalue = search_nametab(player, indiv_attraccess_nametab, fargs[1]);
            if (flagvalue < 0)
            {
                safe_str("#-1 CAN NOT SET", buff, bufc);
                return;
            }

            // Make sure attribute is present
            //
            if (!atr_get_info(thing, atr, &aowner, &aflags))
            {
                safe_str("#-1 ATTRIBUTE NOT PRESENT ON OBJECT", buff, bufc);
                return;
            }

            // Can we write to attribute?
            //
            attr = atr_num(atr);
            if (!attr || !Set_attr(player, thing, attr, aflags))
            {
                safe_noperm(buff, bufc);
                return;
            }

            // Just do it!
            //
            if (clear)
                aflags &= ~flagvalue;
            else
                aflags |= flagvalue;
            could_hear = Hearer(thing);
            atr_set_flags(thing, atr, aflags);

            return;
        }
    }

    // Find thing.
    //
    if ((thing = match_controlled(player, fargs[0])) == NOTHING)
    {
        safe_nothing(buff, bufc);
        return;
    }

    // Check for attr set first.
    //
    for (p = fargs[1]; *p && (*p != ':'); p++)
    {
        // Nothing
        ;
    }

    if (*p)
    {
        *p++ = 0;
        atr = mkattr(fargs[1]);
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
        atr_get_info(thing, atr, &aowner, &aflags);
        if (!Set_attr(player, thing, attr, aflags))
        {
            safe_noperm(buff, bufc);
            return;
        }
        buff2 = alloc_lbuf("fun_set");

        // check for _
        //
        if (*p == '_')
        {
            strcpy(buff2, p + 1);
            if (  !parse_attrib(player, p + 1, &thing2, &atr2)
               || (atr2 == NOTHING))
            {
                free_lbuf(buff2);
                safe_nomatch(buff, bufc);
                return;
            }
            attr2 = atr_num(atr);
            p = buff2;
            atr_pget_str(buff2, thing2, atr2, &aowner, &aflags);

            if (  !attr2
               || !See_attr(player, thing2, attr2, aowner, aflags))
            {
                free_lbuf(buff2);
                safe_noperm(buff, bufc);
                return;
            }
        }

        // Set it.
        //
        set_attr_internal(player, thing, atr, p, 0, buff, bufc);
        free_lbuf(buff2);
        return;
    }

    // Set/clear a flag.
    //
    flag_set(thing, player, fargs[1], 0);
}
#endif

// Generate a substitution array.
//
static unsigned int GenCode(char *pCode, const char *pCodeASCII)
{
    // Strip out the ANSI.
    //
    unsigned int nIn;
    char *pIn = strip_ansi(pCodeASCII, &nIn);

    // Process the printable characters.
    //
    char *pOut = pCode;
    while (*pIn)
    {
        if (Tiny_IsPrint[(unsigned char)*pIn])
        {
            *pOut++ = *pIn - ' ';
        }
        pIn++;
    }
    *pOut = '\0';
    return pOut - pCode;
}

static char *crypt_code(char *code, char *text, int type)
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
    unsigned int nText;
    char *p = strip_ansi(text, &nText);
    char *q = codebuff;
    char *r = textbuff;

    int iMod    = '~' - ' ' + 1;

    // Encryption loop:
    //
    while (*p)
    {
        if (Tiny_IsPrint[(unsigned char)*p])
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
    safe_str(crypt_code(fargs[1], fargs[0], 1), buff, bufc);
}

FUNCTION(fun_decrypt)
{
    safe_str(crypt_code(fargs[1], fargs[0], 0), buff, bufc);
}

// Borrowed from DarkZone
//
void scan_zone
(
    dbref player,
    char *szZone,
    int   ObjectType,
    char *buff,
    char **bufc
)
{
    dbref it;

    if (  !mudconf.have_zones
       || (  !Controls(player, it = match_thing_quiet(player, szZone))
          && !WizRoy(player)))
    {
        safe_noperm(buff, bufc);
        return;
    }
    else if (!Good_obj(it))
    {
        safe_nomatch(buff, bufc);
        return;
    }

    dbref i;
    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    for (i = 0; i < mudstate.db_top; i++)
    {
        if (  Typeof(i) == ObjectType
           && Zone(i) == it
           && !DbrefToBuffer_Add(&pContext, i))
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
}

FUNCTION(fun_zwho)
{
    scan_zone(player, fargs[0], TYPE_PLAYER, buff, bufc);
}

FUNCTION(fun_inzone)
{
    scan_zone(player, fargs[0], TYPE_ROOM, buff, bufc);
}

// Borrowed from DarkZone
//
FUNCTION(fun_children)
{
    dbref it = match_thing(player, fargs[0]);
    if (!(WizRoy(player) || Controls(player, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref i;
    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    for (i = 0; i < mudstate.db_top; i++)
    {
        if (  Parent(i) == it
           && !DbrefToBuffer_Add(&pContext, i))
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
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
    TinyExec(name, &bp, 0, player, cause, EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';
    dbref obj = match_thing(player, name);

    if (!Controls(player, obj))
    {
        // The right circumstances were not met, so we are evaluating
        // as the player who gave the command instead of the
        // requested object.
        //
        obj = player;
    }

    mudstate.nObjEvalNest++;
    str = fargs[1];
    TinyExec(buff, bufc, 0, obj, cause, EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);
    free_lbuf(name);
    mudstate.nObjEvalNest--;
}

FUNCTION(fun_localize)
{
    char *preserve[MAX_GLOBAL_REGS];
    int preserve_len[MAX_GLOBAL_REGS];
    save_global_regs("fun_localize", preserve, preserve_len);

    char *str = fargs[0];
    TinyExec(buff, bufc, 0, player, cause,
        EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, &str, cargs, ncargs);

    restore_global_regs("fun_localize", preserve, preserve_len);
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
    safe_str((char *)strip_ansi(fargs[0]), buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_zfun)
{
    dbref aowner;
    int aflags;
    int attrib;
    char *tbuf1, *str;

    dbref zone = Zone(player);

    if (!mudconf.have_zones)
    {
        safe_str("#-1 ZONES DISABLED", buff, bufc);
        return;
    }
    if (!Good_obj(zone))
    {
        safe_str("#-1 INVALID ZONE", buff, bufc);
        return;
    }

    // Find the user function attribute.
    //
    attrib = get_atr(upcasestr(fargs[0]));
    if (!attrib)
    {
        safe_str("#-1 NO SUCH USER FUNCTION", buff, bufc);
        return;
    }
    tbuf1 = atr_pget(zone, attrib, &aowner, &aflags);
    if (!See_attr(player, zone, (ATTR *) atr_num(attrib), aowner, aflags))
    {
        safe_str("#-1 NO PERMISSION TO GET ATTRIBUTE", buff, bufc);
        free_lbuf(tbuf1);
        return;
    }
    str = tbuf1;
    TinyExec(buff, bufc, 0, zone, player, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, &(fargs[1]), nfargs - 1);
    free_lbuf(tbuf1);
}

FUNCTION(fun_columns)
{
    char sep;
    evarargs_preamble(3);

    int nWidth = Tiny_atol(fargs[1]);
    int nIndent = 0;
    if (nfargs == 4)
    {
        nIndent = Tiny_atol(fargs[3]);
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
        safe_str("#-1 OUT OF RANGE", buff, bufc);
        return;
    }
    char *curr = alloc_lbuf("fun_columns");
    char *cp = curr;
    char *bp = curr;
    char *str = fargs[0];
    TinyExec(curr, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL,
        &str, cargs, ncargs);
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
    BOOL bNeedCRLF = FALSE;
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
            bNeedCRLF = FALSE;
        }
        else
        {
            iColumn++;
            nBufferAvailable -= safe_fill(buff, bufc, ' ',
                nWidth - nVisualWidth);
            bNeedCRLF = TRUE;
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
    char *pPaddingStart;
    char *pPaddingEnd;
    if (nfargs == 6 && *fargs[5])
    {
        pPaddingStart = strip_ansi(fargs[5]);
        pPaddingEnd = strchr(pPaddingStart, '\0');
    }
    else
    {
        pPaddingStart = NULL;
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
        nLineLength = Tiny_atol(fargs[2]);
    }

    // Get field width.
    //
    int nFieldWidth = 10;
    if (nfargs >= 2)
    {
        nFieldWidth = Tiny_atol(fargs[1]);
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
        safe_str("#-1 OUT OF RANGE", buff, bufc);
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
    int k;
    int ca;
    char *as, *str;
    ATTR *attr;

    k = sizeof(struct object);

    k += strlen(Name(thing)) + 1;
    for (ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        int nLen;
        str = atr_get_raw_LEN(thing, ca, &nLen);
        k += nLen+1;
        attr = atr_num(ca);
        if (attr)
        {
            str = (char *)attr->name;
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
    dbref thing;

    thing = match_thing(player, fargs[0]);
    if (thing == NOTHING || !Examinable(player, thing))
    {
        safe_noperm(buff, bufc);
        return;
    }
    safe_ltoa(mem_usage(thing), buff, bufc);
}

FUNCTION(fun_playmem)
{
    dbref thing = match_thing(player, fargs[0]);
    if (thing == NOTHING || !Examinable(player, thing))
    {
        safe_noperm(buff, bufc);
        return;
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
// 0 for orflags, 1 for andflags
//
static int handle_flaglists(dbref player, char *name, char *fstr, int type)
{
    char *s;
    char flagletter[2];
    FLAGSET fset;
    FLAG p_type;
    int negate, temp;
    int ret = type;
    dbref it = match_thing(player, name);

    negate = temp = 0;

    if (it == NOTHING)
        return 0;

    for (s = fstr; *s; s++)
    {
        // Check for a negation sign. If we find it, we note it and
        // increment the pointer to the next character.
        //
        if (*s == '!') {
            negate = 1;
            s++;
        } else {
            negate = 0;
        }

        if (!*s) {
            return 0;
        }
        flagletter[0] = *s;
        flagletter[1] = '\0';

        if (!convert_flags(player, flagletter, &fset, &p_type))
        {
            // Either we got a '!' that wasn't followed by a letter, or we
            // couldn't find that flag. For AND, since we've failed a check,
            // we can return false. Otherwise we just go on.
            //
            if (type == 1)
                return 0;
            else
                continue;
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
                   && (Flags(it) & (WIZARD | DARK)) == (WIZARD | DARK)
                   && !Wizard(player))
                {
                    temp = 0;
                }
                else
                {
                    temp = 1;
                }
            }
            else
            {
                temp = 0;
            }

            if (  type == 1
               && (  (negate && temp)
                  || (!negate && !temp)))
            {
                // Too bad there's no NXOR function. At this point we've
                // either got a flag and we don't want it, or we don't have a
                // flag and we want it. Since it's AND, we return false.
                //
                return 0;

            }
            else if (  type == 0
                    && (  (!negate && temp)
                       || (negate && !temp)))
            {
                // We've found something we want, in an OR. We OR a true with
                // the current value.
                //
                ret |= 1;
            }

            // Otherwise, we don't need to do anything.
            //
        }
    }
    return ret;
}

FUNCTION(fun_orflags)
{
    safe_ltoa(handle_flaglists(player, fargs[0], fargs[1], 0), buff, bufc);
}

FUNCTION(fun_andflags)
{
    safe_ltoa(handle_flaglists(player, fargs[0], fargs[1], 1), buff, bufc);
}

FUNCTION(fun_strtrunc)
{
    int maxVisualWidth = Tiny_atol(fargs[1]);
    if (maxVisualWidth < 0)
    {
        safe_str("#-1 OUT OF RANGE", buff, bufc);
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
    TinyExec(lbuff, &bp, 0, player, cause,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    if (  lbuff[0] == '\0'
       || (  is_number(lbuff)
          && Tiny_atol(lbuff) == 0))
    {
        if (nfargs == 3)
        {
            str = fargs[2];
            TinyExec(buff, bufc, 0, player, cause,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        }
    }
    else
    {
        str = fargs[1];
        TinyExec(buff, bufc, 0, player, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    }
    free_lbuf(lbuff);
}

FUNCTION(fun_inc)
{
    if (nfargs == 1)
    {
        safe_ltoa(Tiny_atol(fargs[0]) + 1, buff, bufc);
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
        safe_ltoa(Tiny_atol(fargs[0]) - 1, buff, bufc);
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
// 1. mail(num)         --> returns message <num> for privs.
// 2. mail(player)      --> returns number of messages for <player>.
// 3. mail(player, num) --> returns message <num> for <player>.
// 4. mail()            --> returns number of messages for executor.
//
FUNCTION(fun_mail)
{
    dbref playerask;
    int num, rc, uc, cc;
#ifdef RADIX_COMPRESSION
    char *msgbuff;
#endif

    // Make sure we have the right number of arguments.
    //
    if (nfargs == 0 || !fargs[0] || !fargs[0][0])
    {
        count_mail(player, 0, &rc, &uc, &cc);
        safe_ltoa(rc + uc, buff, bufc);
        return;
    }
    else if (nfargs == 1)
    {
        if (!is_number(fargs[0]))
        {
            // Handle the case of wanting to count the number of
            // messages.
            //
            playerask = lookup_player(player, fargs[0], 1);
            if (playerask == NOTHING)
            {
                safe_str("#-1 NO SUCH PLAYER", buff, bufc);
            }
            else if (playerask == player || Wizard(player))
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
            playerask = player;
            num = Tiny_atol(fargs[0]);
        }
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(player, fargs[0], 1);
        if (playerask == NOTHING)
        {
            safe_str("#-1 NO SUCH PLAYER", buff, bufc);
            return;
        }
        else if (  (playerask == player && !mudstate.nObjEvalNest)
                || God(player))
        {
            num = Tiny_atol(fargs[1]);
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
#ifdef RADIX_COMPRESSION
        msgbuff = alloc_lbuf("fun_mail");
        string_decompress(MessageFetch(mp->number), msgbuff);
        safe_str(msgbuff, buff, bufc);
        free_lbuf(msgbuff);
#else
        safe_str(MessageFetch(mp->number), buff, bufc);
#endif
        return;
    }

    // Ran off the end of the list without finding anything.
    //
    safe_str("#-1 NO SUCH MESSAGE", buff, bufc);
}

// This function can take these formats:
//
//  1) mailfrom(<num>)
//  2) mailfrom(<player>,<num>)
//
// It returns the dbref of the player the mail is from.
//
FUNCTION(fun_mailfrom)
{
    // Make sure we have the right number of arguments.
    //
    int num;
    dbref playerask;
    if (nfargs == 1)
    {
        playerask = player;
        num = Tiny_atol(fargs[0]);
    }
    else // if (nfargs == 2)
    {
        playerask = lookup_player(player, fargs[0], 1);
        if (playerask == NOTHING)
        {
            safe_str("#-1 NO SUCH PLAYER", buff, bufc);
            return;
        }
        if (playerask == player || Wizard(player))
        {
            num = Tiny_atol(fargs[1]);
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

FUNCTION(fun_hasattr)
{
    dbref thing, aowner;
    int aflags;
    ATTR *attr;
    char *tbuf;

    thing = match_thing(player, fargs[0]);
    if (thing == NOTHING) {
        safe_nomatch(buff, bufc);
        return;
    } else if (!Examinable(player, thing)) {
        safe_noperm(buff, bufc);
        return;
    }
    attr = atr_str(fargs[1]);
    int ch = '0';
    if (attr)
    {
        atr_get_info(thing, attr->number, &aowner, &aflags);
        if (See_attr(player, thing, attr, aowner, aflags))
        {
            tbuf = atr_get(thing, attr->number, &aowner, &aflags);
            if (*tbuf)
            {
                ch = '1';
            }
            free_lbuf(tbuf);
        }
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_hasattrp)
{
    dbref thing, aowner;
    int aflags;
    ATTR *attr;
    char *tbuf;

    thing = match_thing(player, fargs[0]);
    if (thing == NOTHING) {
        safe_nomatch(buff, bufc);
        return;
    } else if (!Examinable(player, thing)) {
        safe_noperm(buff, bufc);
        return;
    }
    attr = atr_str(fargs[1]);
    int ch = '0';
    if (attr)
    {
        atr_pget_info(thing, attr->number, &aowner, &aflags);
        if (See_attr(player, thing, attr, aowner, aflags))
        {
            tbuf = atr_pget(thing, attr->number, &aowner, &aflags);
            if (*tbuf)
            {
                ch = '1';
            }
            free_lbuf(tbuf);
        }
    }
    safe_chr(ch, buff, bufc);
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
FUNCTION(fun_default)
{
    dbref thing, aowner;
    int attrib, aflags;
    ATTR *attr;
    char *objname, *atr_gotten, *bp, *str;

    objname = bp = alloc_lbuf("fun_default");
    str = fargs[0];
    TinyExec(objname, &bp, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
    *bp = '\0';

    // First we check to see that the attribute exists on the object.
    // If so, we grab it and use it.
    //
    if (objname != NULL)
    {
        if (  parse_attrib(player, objname, &thing, &attrib)
           && (attrib != NOTHING))
        {
            attr = atr_num(attrib);
            if (attr && !(attr->flags & AF_IS_LOCK))
            {
                atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
                if (  *atr_gotten
                   && check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
                {
                    safe_str(atr_gotten, buff, bufc);
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
    TinyExec(buff, bufc, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
}

FUNCTION(fun_edefault)
{
    dbref thing, aowner;
    int attrib, aflags;
    ATTR *attr;
    char *objname, *atr_gotten, *bp, *str;

    objname = bp = alloc_lbuf("fun_edefault");
    str = fargs[0];
    TinyExec(objname, &bp, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
    *bp = '\0';

    // First we check to see that the attribute exists on the object.
    // If so, we grab it and use it.
    //
    if (objname != NULL)
    {
        if (parse_attrib(player, objname, &thing, &attrib) &&
            (attrib != NOTHING))
        {
            attr = atr_num(attrib);
            if (attr && !(attr->flags & AF_IS_LOCK))
            {
                atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
                if (  *atr_gotten
                   && check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
                {
                    str = atr_gotten;
                    TinyExec(buff, bufc, 0, thing, player, EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
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
    TinyExec(buff, bufc, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
}

FUNCTION(fun_udefault)
{
    dbref thing, aowner;
    int aflags, anum;
    ATTR *ap;
    char *objname, *atext, *bp, *str;

    str = fargs[0];
    objname = bp = alloc_lbuf("fun_udefault");
    TinyExec(objname, &bp, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
    *bp = '\0';

    // First we check to see that the attribute exists on the object.
    // If so, we grab it and use it.
    //
    if (objname != NULL) {
        if (parse_attrib(player, objname, &thing, &anum)) {
            if ((anum == NOTHING) || (!Good_obj(thing)))
                ap = NULL;
            else
                ap = atr_num(anum);
        } else {
            thing = player;
            ap = atr_str(objname);
        }
        if (ap) {
            atext = atr_pget(thing, ap->number, &aowner, &aflags);
            if (atext) {
                if (*atext &&
                    check_read_perms(player, thing, ap, aowner, aflags,
                             buff, bufc)) {
                    str = atext;
                    TinyExec(buff, bufc, 0, thing, cause, EV_FCHECK | EV_EVAL, &str, &(fargs[2]), nfargs - 1);
                    free_lbuf(atext);
                    free_lbuf(objname);
                    return;
                }
                free_lbuf(atext);
            }
        }
        free_lbuf(objname);
    }

    // If we've hit this point, we've not gotten anything useful, so we
    // go and evaluate the default.
    //
    str = fargs[1];
    TinyExec(buff, bufc, 0, player, cause, EV_EVAL | EV_STRIP_CURLY | EV_FCHECK, &str, cargs, ncargs);
}

/* ---------------------------------------------------------------------------
 * fun_findable: can X locate Y
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_findable)
{
    dbref obj = match_thing(player, fargs[0]);
    dbref victim = match_thing(player, fargs[1]);

    if (obj == NOTHING)
        safe_str("#-1 ARG1 NOT FOUND", buff, bufc);
    else if (victim == NOTHING)
        safe_str("#-1 ARG2 NOT FOUND", buff, bufc);
    else
    {
#ifdef WOD_REALMS
        if (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(obj, victim, ACTION_IS_STATIONARY))
        {
            safe_ltoa(locatable(obj, victim, obj), buff, bufc);
        }
        else safe_chr('0', buff, bufc);
#else
        safe_ltoa(locatable(obj, victim, obj), buff, bufc);
#endif
    }
}

/* ---------------------------------------------------------------------------
 * isword: is every character in the argument a letter?
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_isword)
{
    char *p;
    int ch = '1';

    for (p = fargs[0]; *p; p++)
    {
        if (!Tiny_IsAlpha[(unsigned char)*p])
        {
            ch = '0';
            break;
        }
    }
    safe_chr(ch, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_visible:  Can X examine Y. If X does not exist, 0 is returned.
 *               If Y, the object, does not exist, 0 is returned. If
 *               Y the object exists, but the optional attribute does
 *               not, X's ability to return Y the object is returned.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_visible)
{
    dbref it, thing, aowner;
    int aflags, atr;
    ATTR *ap;

    if ((it = match_thing(player, fargs[0])) == NOTHING)
    {
        safe_chr('0', buff, bufc);
        return;
    }
    if (parse_attrib(player, fargs[1], &thing, &atr))
    {
        if (Good_obj(thing) && atr != NOTHING)
        {
            ap = atr_num(atr);
            atr_pget_info(thing, atr, &aowner, &aflags);
            safe_ltoa(See_attr(it, thing, ap, aowner, aflags), buff, bufc);
            return;
        }
    }
    else
    {
        thing = match_thing(player, fargs[1]);
    }
    if (!Good_obj(thing))
    {
        safe_chr('0', buff, bufc);
        return;
    }
    safe_ltoa(Examinable(it, thing), buff, bufc);
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
    char *wordlist, *s, *r, sep, *oldp;

    varargs_preamble(3);
    oldp = *bufc;

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
        cur = Tiny_atol(r) - 1;
        if ((cur >= 0) && (cur < nwords) && ptrs[cur]) {
            if (oldp != *bufc)
                safe_chr(sep, buff, bufc);
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
 *  grab(Test-1+Ack-2+Foof-3,*o*,+)  => Ack:2
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
    int n, i, j;
    char c, *old;

    if (!fargs[0] || !*fargs[0])
    {
        return;
    }
    old = *bufc;

    safe_str(fargs[0], buff, bufc);
    **bufc = '\0';

    n = strlen(old);

    for (i = 0; i < n; i++)
    {
        j = RandomINT32(i, n-1);
        c = old[i];
        old[i] = old[j];
        old[j] = c;
    }
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

    for (i = 0; i < n; i++)
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
static char ucomp_buff[LBUF_SIZE];
static dbref ucomp_cause;
static dbref ucomp_player;

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
    TinyExec(result, &bp, 0, ucomp_player, ucomp_cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &(elems[0]), 2);
    *bp = '\0';
    if (!result)
        n = 0;
    else {
        n = Tiny_atol(result);
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
        if ((*compare) (array[i], array[left]) < 0) {
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
    if ((last - left) < (right - last)) {
        sane_qsort(array, left, last - 1, compare);
        left = last + 1;
        goto loop;
    } else {
        sane_qsort(array, last + 1, right, compare);
        right = last - 1;
        goto loop;
    }
}

FUNCTION(fun_sortby)
{
    char *atext, *list, *ptrs[LBUF_SIZE / 2], sep;
    int nptrs, aflags, anum;
    dbref thing, aowner;
    ATTR *ap;

    varargs_preamble(3);

    if (parse_attrib(player, fargs[0], &thing, &anum)) {
        if ((anum == NOTHING) || !Good_obj(thing))
            ap = NULL;
        else
            ap = atr_num(anum);
    } else {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    if (!ap) {
        return;
    }
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext) {
        return;
    } else if (!*atext || !See_attr(player, thing, ap, aowner, aflags)) {
        free_lbuf(atext);
        return;
    }
    strcpy(ucomp_buff, atext);
    ucomp_player = thing;
    ucomp_cause = cause;

    list = alloc_lbuf("fun_sortby");
    strcpy(list, fargs[1]);
    nptrs = list2arr(ptrs, LBUF_SIZE / 2, list, sep);

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
    char sep;

    // If we are passed an empty arglist return a null string.
    //
    if (nfargs <= 0)
    {
        return;
    }
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
        if (quick_wild(fargs[1], r))
        {
            Tiny_ltoa(wcount, tbuf);
            if (old != *bufc)
                safe_chr(' ', buff, bufc);
            safe_str(tbuf, buff, bufc);
        }
        wcount++;
    } while (s);

    if (*bufc == old)
        safe_chr('0', buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_ports: Returns a list of ports for a user.
// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_ports)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        if (target == player || Wizard(player))
        {
            if (Connected(target))
            {
                make_portlist(player, target, buff, bufc);
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
 * fun_mix: Like map, but operates on two lists simultaneously, passing
 * the elements as %0 as %1.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_mix)
{
    char *atext, *os[2], *str, *cp1, *cp2, *atextbuf, sep;

    varargs_preamble(4);
    char *oldp = *bufc;

    // Get the attribute, check the permissions.
    //
    dbref thing;
    ATTR  *ap;
    int   anum;
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    if (!ap)
    {
        return;
    }

    dbref aowner;
    int aflags;
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (  !*atext
            || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }

    // process the two lists, one element at a time.
    //
    cp1 = trim_space_sep(fargs[1], sep);
    cp2 = trim_space_sep(fargs[2], sep);

    if (countwords(cp1, sep) != countwords(cp2, sep))
    {
        free_lbuf(atext);
        safe_str("#-1 LISTS MUST BE OF EQUAL SIZE", buff, bufc);
        return;
    }
    atextbuf = alloc_lbuf("fun_mix");

    while (  cp1
          && cp2
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        if (*bufc != oldp)
        {
            safe_chr(sep, buff, bufc);
        }
        os[0] = split_token(&cp1, sep);
        os[1] = split_token(&cp2, sep);
        strcpy(atextbuf, atext);
        str = atextbuf;
        TinyExec(buff, bufc, 0, player, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &(os[0]), 2);
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
    dbref aowner, thing;
    int aflags, anum, flag = 0;
    ATTR *ap;
    char *atext, *atextbuf, *str, *cp, *bp;
    char cbuf[2], prev = '\0';

    if (  nfargs != 2
       && nfargs != 4)
    {
        safe_str("#-1 FUNCTION (FOREACH) EXPECTS 2 or 4 ARGUMENTS", buff, bufc);
        return;
    }

    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    if (!ap)
    {
        return;
    }
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (  !*atext
            || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }
    atextbuf = alloc_lbuf("fun_foreach");
    cp = trim_space_sep(fargs[1], ' ');

    bp = cbuf;

    cbuf[1] = '\0';

    if (nfargs == 4)
    {
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
                    flag = 0;
                    continue;
                }
            }
            else
            {
                if (  cbuf[0] == *fargs[2]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = 1;
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
            TinyExec(buff, bufc, 0, thing, player,
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
            TinyExec(buff, bufc, 0, thing, player,
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
    dbref aowner, thing;
    int aflags, anum, nptrs1, nptrs2, nresults, i, j;
    ATTR *ap;
    char *list1, *list2, *rlist;
    char *ptrs1[LBUF_SIZE / 2], *ptrs2[LBUF_SIZE / 2], *results[LBUF_SIZE / 2];
    char *atext, *bp, *str, sep, *oldp;

    oldp = *bufc;
    varargs_preamble(4);

    // Find our object and attribute.
    //
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if ((anum == NOTHING) || !Good_obj(thing))
            ap = NULL;
        else
            ap = atr_num(anum);
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    if (!ap)
    {
        return;
    }
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (!*atext || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }

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
    TinyExec(rlist, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &fargs[1], 1);
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
                    safe_chr(sep, buff, bufc);
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
    int n = Tiny_atol(fargs[0]);
    int die = Tiny_atol(fargs[1]);

    if ((n == 0) || (die <= 0))
    {
        safe_chr('0', buff, bufc);
        return;
    }

    if ((n < 1) || (n > 100))
    {
        safe_str("#-1 NUMBER OUT OF RANGE", buff, bufc);
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
    if (!delim_check(fargs, nfargs, 4, &sep, buff, bufc, 0,
        player, cause, cargs, ncargs, 1))
    {
        return;
    }

    int n_times = Tiny_atol(fargs[2]);
    if (n_times < 1)
    {
        return;
    }
    if (n_times > LBUF_SIZE)
    {
        n_times = LBUF_SIZE;
    }
    INT32 iLower = Tiny_atol(fargs[0]);
    INT32 iUpper = Tiny_atol(fargs[1]);

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
    if (is_number(fargs[0]) && is_number(fargs[1]))
        safe_ltoa(Tiny_atol(fargs[0]) << Tiny_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
}

FUNCTION(fun_shr)
{
    if (is_number(fargs[0]) && is_number(fargs[1]))
        safe_ltoa(Tiny_atol(fargs[0]) >> Tiny_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
}

FUNCTION(fun_band)
{
    if (is_number(fargs[0]) && is_number(fargs[1]))
        safe_ltoa(Tiny_atol(fargs[0]) & Tiny_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
}

FUNCTION(fun_bor)
{
    if (is_number(fargs[0]) && is_number(fargs[1]))
        safe_ltoa(Tiny_atol(fargs[0]) | Tiny_atol(fargs[1]), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
}

FUNCTION(fun_bnand)
{
    if (is_number(fargs[0]) && is_number(fargs[1]))
        safe_ltoa(Tiny_atol(fargs[0]) & ~(Tiny_atol(fargs[1])), buff, bufc);
    else
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
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
    safe_chr(mudstate.dumping ? '1' : '0', buff, bufc);
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
        int nDigits;
        if (  !is_integer(fargs[1], &nDigits)
           || (iRadix = Tiny_atoi64(fargs[1])) < 2
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
    while (Tiny_IsSpace[(unsigned char)*pString])
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
    int nDigits;
    if (  !is_integer(fargs[0], &nDigits)
       || (nfargs == 2 && !is_integer(fargs[1], &nDigits)))
    {
        safe_str("#-1 ARGUMENTS MUST BE NUMBERS", buff, bufc);
        return;
    }
    INT64 val = Tiny_atoi64(fargs[0]);

    // Validate the radix is between 2 and 64.
    //
    INT64 iRadix = 64;
    if (nfargs == 2)
    {
        iRadix = Tiny_atoi64(fargs[1]);
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
    safe_str(fargs[0], buff, bufc);
    for (i = 1; i < nfargs; i++)
    {
        safe_str(fargs[i], buff, bufc);
    }
}

// grep() and grepi() code borrowed from PennMUSH 1.50
//
char *grep_util(dbref player, dbref thing, char *pattern, char *lookfor, int len, int insensitive)
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
    if (parse_attrib_wild(player, buf, &thing, 0, 0, 1))
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
            int nText;
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
                safe_str((char *)(atr_num(ca))->name, tbuf1, &bp);
            }
            free_lbuf(attrib);
        }
    }
    free_lbuf(buf);
    *bp = '\0';
    olist_pop();
    return tbuf1;
}

FUNCTION(fun_grep)
{
    char *tp;

    dbref it = match_thing(player, fargs[0]);

    if (it == NOTHING)
    {
        safe_nomatch(buff, bufc);
        return;
    }
    else if (!(Examinable(player, it)))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Make sure there's an attribute and a pattern.
    //
    if (!*fargs[1])
    {
        safe_str("#-1 NO SUCH ATTRIBUTE", buff, bufc);
        return;
    }
    if (!*fargs[2])
    {
        safe_str("#-1 INVALID GREP PATTERN", buff, bufc);
        return;
    }
    tp = grep_util(player, it, fargs[1], fargs[2], strlen(fargs[2]), 0);
    safe_str(tp, buff, bufc);
    free_lbuf(tp);
}

FUNCTION(fun_grepi)
{
    char *tp;

    dbref it = match_thing(player, fargs[0]);

    if (it == NOTHING) {
        safe_nomatch(buff, bufc);
        return;
    } else if (!(Examinable(player, it))) {
        safe_noperm(buff, bufc);
        return;
    }

    // Make sure there's an attribute and a pattern
    //
    if (!fargs[1] || !*fargs[1]) {
        safe_str("#-1 NO SUCH ATTRIBUTE", buff, bufc);
        return;
    }
    if (!fargs[2] || !*fargs[2]) {
        safe_str("#-1 INVALID GREP PATTERN", buff, bufc);
        return;
    }
    tp = grep_util(player, it, fargs[1], fargs[2], strlen(fargs[2]), 1);
    safe_str(tp, buff, bufc);
    free_lbuf(tp);
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
    safe_tprintf_str(buff, bufc, "%s", amax);
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
    safe_tprintf_str(buff, bufc, "%s", amin);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_valid)
{
    // Checks to see if a given <something> is valid as a parameter of
    // a given type (such as an object name)
    //
    if (!*fargs[0] || !*fargs[1])
    {
        safe_chr('0', buff, bufc);
    }
    else if (!_stricmp(fargs[0], "name"))
    {
        int nValidName;
        BOOL bValid;
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid);
        char ch = (bValid) ? '1' : '0';
        safe_chr(ch, buff, bufc);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_hastype)
{
    dbref it = match_thing(player, fargs[0]);

    if (it == NOTHING)
    {
        safe_nomatch(buff, bufc);
        return;
    }
    int ch = '0';
    switch (Tiny_ToLower[(unsigned char)fargs[1][0]])
    {
    case 'r':

        if (isThing(it))
        {
            ch = '1';
        }
        break;

    case 'e':

        if (isExit(it))
        {
            ch = '1';
        }
        break;

    case 'p':

        if (isPlayer(it))
        {
            ch = '1';
        }
        break;

    case 't':

        if (isThing(it))
        {
            ch = '1';
        }
        break;

    default:

        safe_str("#-1 NO SUCH TYPE", buff, bufc);
        break;
    }
    safe_chr(ch, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lparent)
{
    dbref it = match_thing(player, fargs[0]);
    if (!Good_obj(it))
    {
        safe_nomatch(buff, bufc);
        return;
    }
    else if (!Examinable(player, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);

    if (!DbrefToBuffer_Add(&pContext, it))
    {
        DbrefToBuffer_Final(&pContext);
        return;
    }

    dbref par = Parent(it);

    int iNestLevel = 1;
    while (  Good_obj(par)
          && Examinable(player, it)
          && iNestLevel < mudconf.parent_nest_lim)
    {
        if (!DbrefToBuffer_Add(&pContext, par))
        {
            break;
        }
        it = par;
        par = Parent(par);
        iNestLevel++;
    }
    DbrefToBuffer_Final(&pContext);
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
        doer = player;
    }
    else
    {
        doer = match_thing(player, fargs[0]);
    }

    if (!Controls(player, doer)) {
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
        doer = player;
    }
    else
    {
        doer = match_thing(player, fargs[0]);
    }

    if (!Controls(player, doer))
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
        doer = player;
    }
    else
    {
        doer = match_thing(player, fargs[0]);
    }

    if (!Controls(player, doer))
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
        doer = player;
    }
    else
    {
        doer = match_thing(player, fargs[0]);
    }

    if (!Controls(player, doer))
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
        pos = Tiny_atol(fargs[1]);
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
    STACK *sp, *prev = NULL;
    dbref doer;
    int count = 0, pos;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = player;
    }
    else
    {
        doer = match_thing(player, fargs[0]);
    }

    if (!Controls(player, doer))
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
        pos = Tiny_atol(fargs[1]);
    }

    sp = Stack(doer);

    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str("#-1 POSITION TOO LARGE", buff, bufc);
        return;
    }
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
    STACK *sp;
    dbref doer;
    char *data;

    if (nfargs <= 1 || !*fargs[1])
    {
        doer = player;
        data = fargs[0];
    }
    else
    {
        doer = match_thing(player, fargs[0]);
        data = fargs[1];
    }

    if (!Controls(player, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (stacksize(doer) >= mudconf.stack_limit)
    {
        safe_str("#-1 STACK SIZE EXCEEDED", buff, bufc);
        return;
    }
    sp = (STACK *)MEMALLOC(sizeof(STACK));
    ISOUTOFMEMORY(sp);
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

FUNCTION(fun_regmatch)
{
    regexp *re = regcomp(fargs[1]);
    if (!re)
    {
        // Matching error.
        //
        notify_quiet(player, (const char *) regexp_errbuf);
        safe_chr('0', buff, bufc);
        return;
    }

    int matched = regexec(re, fargs[0]);
    safe_ltoa(regexec(re, fargs[0]), buff, bufc);

    // If we don't have a third argument, we're done.
    //
    if (nfargs != 3)
    {
        MEMFREE(re);
        re = NULL;
        return;
    }

    // We need to parse the list of registers. If a register is
    // mentioned in the list, then either fill the register with the
    // subexpression, or if there wasn't a match, clear it.
    //
    char *qregs[NSUBEXP];
    int nqregs = list2arr(qregs, NSUBEXP, fargs[2], ' ');
    for (int i = 0; i < nqregs; i++)
    {
        int curq;
        if (  qregs[i]
           && *qregs[i]
           && (curq = Tiny_IsRegister[(unsigned char)qregs[i][0]]) != -1
           && qregs[i][1] == '\0'
           && curq < MAX_GLOBAL_REGS)
        {
            if (!mudstate.global_regs[curq])
            {
                mudstate.global_regs[curq] = alloc_lbuf("fun_regmatch");
            }
            int len = 0;
            if (matched && re->startp[i] && re->endp[i])
            {
                // We have a subexpression.
                //
                len = re->endp[i] - re->startp[i];
                if (len > LBUF_SIZE - 1)
                {
                    len = LBUF_SIZE - 1;
                }
                else if (len < 0)
                {
                    len = 0;
                }
                memcpy(mudstate.global_regs[curq], re->startp[i], len);
            }
            mudstate.global_regs[curq][len] = '\0';
            mudstate.glob_reg_len[curq] = len;
        }
    }
    MEMFREE(re);
    re = NULL;
}


/* ---------------------------------------------------------------------------
 * fun_translate: Takes a string and a second argument. If the second argument
 * is 0 or s, control characters are converted to spaces. If it's 1 or p,
 * they're converted to percent substitutions.
 */

FUNCTION(fun_translate)
{
    int type = 0;

    int ch = fargs[1][0];
    if (ch == 'p' || ch == '1')
    {
        type = 1;
    }

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
    BOOL IsSet(unsigned int i);
};

CBitField::CBitField(unsigned int nMaximum_arg)
{
    nInts  = 0;
    pInts  = NULL;
    pMasks = NULL;
    if (0 < nMaximum)
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
        nInts    = (nMaximum+nBitsPer-1) >> nShift;
        pMasks   = (UINT32 *)MEMALLOC((nInts+nBitsPer)*sizeof(UINT32));
        ISOUTOFMEMORY(pMasks);
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
    if (i < nMaximum)
    {
        pInts[i>>nShift] |= pMasks[i&nMask];
    }
}

void CBitField::Clear(unsigned int i)
{
    if (i < nMaximum)
    {
        pInts[i>>nShift] &= ~pMasks[i&nMask];
    }
}

BOOL CBitField::IsSet(unsigned int i)
{
    if (i < nMaximum)
    {
        if (pInts[i>>nShift] & pMasks[i&nMask])
        {
            return TRUE;
        }
    }
    return FALSE;
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
    dbref cause,
    dbref room,
    CBitField &bfTraverse,
    CBitField &bfReport,
    int   level,
    int   maxlevels,
    int   showall
)
{
    // Make sure the player can really see this room from their location.
    //
    if (  (  level == maxlevels
          || showall)
       && (  Examinable(player, room)
          || Location(player) == room
          || room == cause))
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
                room_list(player, cause, loc, bfTraverse, bfReport,
                    (level + 1), maxlevels, showall);
            }
        }
    }
}

FUNCTION(fun_lrooms)
{
    dbref room = match_thing(player, fargs[0]);
    if (!Good_obj(room) || !isRoom(room))
    {
        safe_str("#-1 FIRST ARGUMENT MUST BE A ROOM", buff, bufc);
        return;
    }

    int N = 1;
    if (nfargs >= 2)
    {
        N = Tiny_atol(fargs[1]);
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

    int B = 1;
    if (nfargs == 3)
    {
        B = Tiny_atol(fargs[2]);
    }

    CBitField bfReport(mudstate.db_top-1);
    CBitField bfTraverse(mudstate.db_top-1);
    bfReport.ClearAll();
    bfTraverse.ClearAll();

    bfTraverse.Set(room);
    room_list(player, cause, room, bfTraverse, bfReport, 0, N, B);
    bfReport.Clear(room);

    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (  bfReport.IsSet(i)
           && !DbrefToBuffer_Add(&pContext, i))
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
}
