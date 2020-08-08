/*! \file db_rw.cpp
 * \brief Flatfile implementation.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "mathutil.h"
#include "vattr.h"

static int g_version;
static int g_format;
static int g_flags;

// The following mux_AttrNameInitialSet_latin1 is only used for converting
// A_LOCK.
//
// The first character of an attribute name must be either alphabetic,
// '_', '#', '.', or '~'. It's handled by the following table.
//
// Characters thereafter may be letters, numbers, and characters from
// the set {'?!`/-_.@#$^&~=+<>()}. Lower-case letters are turned into
// uppercase before being used, but lower-case letters are valid input.
//
static bool mux_AttrNameInitialSet_latin1[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 4
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,  // 5
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // 6
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,  // 7

    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,  // B
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // C
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1,  // D
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  // E
    1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0   // F
};

/* ---------------------------------------------------------------------------
 * getboolexp1: Get boolean subexpression from file.
 *
 * This is only used to import v2 flatfiles.
 */

static BOOLEXP *getboolexp1(FILE *f)
{
    BOOLEXP *b;
    UTF8 *buff, *s;
    int d;

    int c = getc(f);
    switch (c)
    {
    case '\n':
        ungetc(c, f);
        return TRUE_BOOLEXP;

    case EOF:

        // Unexpected EOF in boolexp.
        //
        mux_assert(0);
        break;

    case '(':
        b = alloc_bool("getboolexp1.openparen");
        switch (c = getc(f))
        {
        case NOT_TOKEN:
            b->type = BOOLEXP_NOT;
            b->sub1 = getboolexp1(f);
            break;

        case INDIR_TOKEN:
            b->type = BOOLEXP_INDIR;
            b->sub1 = getboolexp1(f);
            break;

        case IS_TOKEN:
            b->type = BOOLEXP_IS;
            b->sub1 = getboolexp1(f);
            break;

        case CARRY_TOKEN:
            b->type = BOOLEXP_CARRY;
            b->sub1 = getboolexp1(f);
            break;

        case OWNER_TOKEN:
            b->type = BOOLEXP_OWNER;
            b->sub1 = getboolexp1(f);
            break;

        default:
            ungetc(c, f);
            b->sub1 = getboolexp1(f);
            if ('\n' == (c = getc(f)))
            {
                c = getc(f);
            }
            switch (c)
            {
            case AND_TOKEN:
                b->type = BOOLEXP_AND;
                break;

            case OR_TOKEN:
                b->type = BOOLEXP_OR;
                break;

            default:
                goto error;
            }
            b->sub2 = getboolexp1(f);
        }

        if ('\n' == (d = getc(f)))
        {
            d = getc(f);
        }
        if (')' != d)
        {
            goto error;
        }
        return b;


    default: // dbref or attribute.

        ungetc(c, f);
        b = alloc_bool("getboolexp1.default");
        b->type = BOOLEXP_CONST;
        b->thing = 0;

        // This is either an attribute, eval, or constant lock. Constant locks
        // are of the form <num>, while attribute and eval locks are of the
        // form <anam-or-anum>:<string> or <aname-or-anum>/<string>
        // respectively. The characters <nl>, |, and & terminate the string.
        //
        if (mux_isdigit(c))
        {
            while (mux_isdigit(c = getc(f)))
            {
                b->thing = b->thing * 10 + c - '0';
            }
        }
        else if (mux_AttrNameInitialSet_latin1[(unsigned char)c])
        {
            buff = alloc_lbuf("getboolexp1.atr_name");

            s = buff;
            while (   EOF != (c = getc(f))
                  && '\n' != c
                  && ':'  != c
                  && '/'  != c
                  && s < buff + LBUF_SIZE - 1)
            {
                *s++ = (UTF8)c;
            }

            if (EOF == c)
            {
                free_lbuf(buff);
                free_bool(b);
                goto error;
            }
            *s = '\0';

            // Look the name up as an attribute. If not found, create a new
            // attribute.
            //
            int anum = mkattr(GOD, buff);
            if (anum <= 0)
            {
                free_bool(b);
                free_lbuf(buff);
                goto error;
            }
            free_lbuf(buff);
            b->thing = anum;
        }
        else
        {
            free_bool(b);
            goto error;
        }

        // If last character is : then this is an attribute lock. A last
        // character of / means an eval lock.
        //
        if (  ':' == c
           || '/' == c)
        {
            if ('/' == c)
            {
                b->type = BOOLEXP_EVAL;
            }
            else
            {
                b->type = BOOLEXP_ATR;
            }

            buff = alloc_lbuf("getboolexp1.attr_lock");
            s = buff;
            while (   EOF != (c = getc(f))
                  && '\n' != c
                  && ')'  != c
                  && OR_TOKEN != c
                  && AND_TOKEN != c
                  && s < buff + LBUF_SIZE - 1)
            {
                *s++ = (UTF8)c;
            }

            if (EOF == c)
            {
                goto error;
            }
            *s = '\0';

            b->sub1 = (BOOLEXP *)StringClone(buff);
            free_lbuf(buff);
        }
        ungetc(c, f);
        return b;
    }

error:

    // Bomb Out.
    //
    mux_assert(0);
    return TRUE_BOOLEXP;
}

/* ---------------------------------------------------------------------------
 * getboolexp: Read a boolean expression from the flat file.
 *
 * This is only used to import v2 flatfile.
 */

static BOOLEXP *getboolexp(FILE *f)
{
    BOOLEXP *b = getboolexp1(f);
    int c = getc(f);
    mux_assert(c == '\n');

    if ((c = getc(f)) != '\n')
    {
        ungetc(c, f);
    }
    return b;
}

int g_max_nam_atr = INT_MIN;
int g_max_obj_atr = INT_MIN;

/* ---------------------------------------------------------------------------
 * get_list: Read attribute list from flat file.
 */

static bool get_list(FILE *f, dbref i)
{
    UTF8 *buff = alloc_lbuf("get_list");
    for (;;)
    {
        dbref atr;
        int c;
        switch (c = getc(f))
        {
        case '>':   // read # then string
            atr = getref(f);
            if (atr > 0)
            {
                // Maximum attribute number across all objects.
                //
                if (g_max_obj_atr < atr)
                {
                    g_max_obj_atr = atr;
                }

                // Store the attr
                //
                size_t nBufferUnicode;
                UTF8 *pBufferUnicode;
                if (3 <= g_version)
                {
                    pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
                }
                else
                {
                    size_t nBufferLatin1;
                    char *pBufferLatin1 = (char *)getstring_noalloc(f, true, &nBufferLatin1);
                    pBufferUnicode = ConvertToUTF8(pBufferLatin1, &nBufferUnicode);
                }
                atr_add_raw_LEN(i, atr, pBufferUnicode, nBufferUnicode);
            }
            else
            {
                // Silently discard
                //
                size_t nBuffer;
                (void)getstring_noalloc(f, true, &nBuffer);
            }
            break;

        case '\n':  // ignore newlines. They're due to v(r).

            break;

        case '<':   // end of list

            free_lbuf(buff);
            c = getc(f);
            if (c != '\n')
            {
                ungetc(c, f);
                Log.tinyprintf(T("No line feed on object %d" ENDLINE), i);
                return true;
            }
            return true;

        default:
            Log.tinyprintf(T("Bad character \xE2\x80\x98%c\xE2\x80\x99 when getting attributes on object %d" ENDLINE), c, i);

            // We've found a bad spot.  I hope things aren't too bad.
            //
            {
                size_t nBuffer;
                (void)getstring_noalloc(f, true, &nBuffer);
            }
        }
    }
}

dbref db_read(FILE *f, int *db_format, int *db_version, int *db_flags)
{
    dbref i, anum;
    int ch;
    const UTF8 *tstr;
    int aflags;
    BOOLEXP *tempbool;
    UTF8 *buff;
    size_t nBuffer;

    g_format = F_UNKNOWN;
    g_version = 0;
    g_flags = 0;
    g_max_nam_atr = INT_MIN;
    g_max_obj_atr = INT_MIN;


    bool header_gotten = false;
    bool size_gotten = false;
    bool nextattr_gotten = false;

    bool convert_values = false;
    bool read_attribs = true;
    bool read_name = true;
    bool read_key = true;
    bool read_money = true;

    size_t nName;
    bool bValid;
    UTF8 *pName;

    int iDotCounter = 0;
    if (mudstate.bStandAlone)
    {
        Log.WriteString(T("Reading "));
        Log.Flush();
    }

    db_free();
    for (i = 0;; i++)
    {
        if (mudstate.bStandAlone)
        {
            if (!iDotCounter)
            {
                iDotCounter = 100;
                fputc('.', stderr);
                fflush(stderr);
            }
            iDotCounter--;
        }

        ch = getc(f);
        switch (ch)
        {
        case '-':   // Misc tag
            ch = getc(f);
            if (ch == 'R')
            {
                // Record number of players
                //
                mudstate.record_players = getref(f);
                if (mudconf.reset_players)
                {
                    mudstate.record_players = 0;
                }
            }
            break;

        case '+':

            // MUX header
            //
            ch = getc(f);
            if (ch == 'A')
            {
                // USER-NAMED ATTRIBUTE
                //
                anum = getref(f);
                tstr = (UTF8 *)getstring_noalloc(f, true, &nBuffer);
                if (mux_isdigit(*tstr))
                {
                    aflags = 0;
                    while (mux_isdigit(*tstr))
                    {
                        aflags = (aflags * 10) + (*tstr++ - '0');
                    }
                    tstr++; // skip ':'
                }
                else
                {
                    aflags = mudconf.vattr_flags;
                }

                // If v2 flatfile or earlier, convert tstr to UTF-8.
                //
                if (g_version <= 2)
                {
                    size_t nUnused;
                    tstr = ConvertToUTF8((char *)tstr, &nUnused);
                }

                pName = MakeCanonicalAttributeName(tstr, &nName, &bValid);
                if (bValid)
                {
                    // Maximum attribute number across all names.
                    //
                    if (g_max_nam_atr < anum)
                    {
                        g_max_nam_atr = anum;
                    }

                    vattr_define_LEN(pName, nName, anum, aflags);
                }
            }
            else if (ch == 'X')
            {
                // MUX VERSION
                //
                if (header_gotten)
                {
                    Log.tinyprintf(T(ENDLINE "Duplicate MUX version header entry at object %d, ignored." ENDLINE), i);
                    tstr = (UTF8 *)getstring_noalloc(f, false, &nBuffer);
                }
                else
                {
                    header_gotten = true;
                    g_format = F_MUX;
                    g_version = getref(f);
                    g_flags = g_version & ~V_MASK;
                    g_version &= V_MASK;

                    if (  g_version < MIN_SUPPORTED_VERSION
                       || MAX_SUPPORTED_VERSION < g_version)
                    {
                        Log.tinyprintf(T(ENDLINE "Unsupported flatfile version: %d." ENDLINE), g_version);
                        return -1;
                    }

                    // Due to potential UTF-8 characters in attribute names,
                    // we do not support parsing A_LOCK from the header. After
                    // converting from v2 to v4, this should never be needed
                    // anyway.
                    //
                    mux_assert(  (  (  1 == g_version
                                    || 2 == g_version)
                                 && (g_flags & MANDFLAGS_V2) == MANDFLAGS_V2)
                              || (  3 == g_version
                                 && (g_flags & MANDFLAGS_V3) == MANDFLAGS_V3)
                              || (  4 == g_version
                                 && (g_flags & MANDFLAGS_V4) == MANDFLAGS_V4));

                    // Otherwise extract feature flags
                    //
                    if (g_flags & V_DATABASE)
                    {
                        if (  1 == g_version
                           || 2 == g_version)
                        {
                            // We'll convert the external database from
                            // Latin-1 to UTF-8 at the end.
                            //
                            convert_values = true;
                        }
                        read_attribs = false;
                        read_name = !(g_flags & V_ATRNAME);
                    }
                    read_key = !(g_flags & V_ATRKEY);
                    read_money = !(g_flags & V_ATRMONEY);
                }
            }
            else if (ch == 'S')
            {
                // SIZE
                //
                if (size_gotten)
                {
                    Log.tinyprintf(T(ENDLINE "Duplicate size entry at object %d, ignored." ENDLINE), i);
                    tstr = (UTF8 *)getstring_noalloc(f, false, &nBuffer);
                }
                else
                {
                    mudstate.min_size = getref(f);
                    size_gotten = true;
                }
            }
            else if (ch == 'N')
            {
                // NEXT ATTR TO ALLOC WHEN NO FREELIST
                //
                if (nextattr_gotten)
                {
                    Log.tinyprintf(T(ENDLINE "Duplicate next free vattr entry at object %d, ignored." ENDLINE), i);
                    tstr = (UTF8 *)getstring_noalloc(f, false, &nBuffer);
                }
                else
                {
                    mudstate.attr_next = getref(f);
                    nextattr_gotten = true;
                }
            }
            else
            {
                Log.tinyprintf(T(ENDLINE "Unexpected character \xE2\x80\x98%c\xE2\x80\x99 in MUX header near object #%d, ignored." ENDLINE), ch, i);
                tstr = (UTF8 *)getstring_noalloc(f, false, &nBuffer);
            }
            break;

        case '!':   // MUX entry
            i = getref(f);
            db_grow(i + 1);

            if (read_name)
            {
                tstr = (UTF8 *)getstring_noalloc(f, true, &nBuffer);
                if (g_version <= 2)
                {
                    size_t nUsed;
                    tstr = ConvertToUTF8((char *)tstr, &nUsed);
                }
                buff = alloc_mbuf("dbread.s_Name");
                StripTabsAndTruncate(tstr, buff, MBUF_SIZE-1, MBUF_SIZE-1);
                s_Name(i, buff);
                free_mbuf(buff);

                s_Location(i, getref(f));
            }
            else
            {
                s_Location(i, getref(f));
            }

            // ZONE
            //
            int zone;
            zone = getref(f);
            if (zone < NOTHING)
            {
                zone = NOTHING;
            }
            s_Zone(i, zone);

            // CONTENTS and EXITS
            //
            s_Contents(i, getref(f));
            s_Exits(i, getref(f));

            // LINK
            //
            s_Link(i, getref(f));

            // NEXT
            //
            s_Next(i, getref(f));

            // LOCK
            //
            if (read_key)
            {
                // Parse lock directly from flatfile.
                // Only used when reading v2 format.
                //
                tempbool = getboolexp(f);
                atr_add_raw(i, A_LOCK, unparse_boolexp_quiet(1, tempbool));
                free_boolexp(tempbool);
            }

            // OWNER
            //
            s_Owner(i, getref(f));

            // PARENT
            //
            s_Parent(i, getref(f));

            // PENNIES
            //
            if (read_money)
            {
                s_PenniesDirect(i, getref(f));
            }

            // FLAGS
            //
            s_Flags(i, FLAG_WORD1, getref(f));
            s_Flags(i, FLAG_WORD2, getref(f));
            s_Flags(i, FLAG_WORD3, getref(f));

            // POWERS
            //
            s_Powers(i, getref(f));
            s_Powers2(i, getref(f));

            // ATTRIBUTES
            //
            if (read_attribs)
            {
                if (!get_list(f, i))
                {
                    Log.tinyprintf(T(ENDLINE "Error reading attrs for object #%d" ENDLINE), i);
                    return -1;
                }
            }

            // check to see if it's a player
            //
            if (isPlayer(i))
            {
                c_Connected(i);
            }
            break;

        case '*':   // EOF marker
            tstr = (UTF8 *)getstring_noalloc(f, false, &nBuffer);
            if (strncmp((char *)tstr, "**END OF DUMP***", 16))
            {
                Log.tinyprintf(T(ENDLINE "Bad EOF marker at object #%d" ENDLINE), i);
                return -1;
            }
            else
            {
                // Attribute number warnings.
                //
                if (g_max_nam_atr < g_max_obj_atr)
                {
                    Log.tinyprintf(T(ENDLINE "Warning: One or more attribute values are unnamed. Did you use ./Backup on a running game?"));
                }

                if (!nextattr_gotten)
                {
                    Log.tinyprintf(T(ENDLINE "Warning: Missing +N<next free>. Adjusting."));
                }

                if (mudstate.attr_next <= g_max_nam_atr)
                {
                    if (nextattr_gotten)
                    {
                        Log.tinyprintf(T(ENDLINE "Warning: +N<next free attr> conflicts with existing attribute names. Adjusting."));
                    }
                    mudstate.attr_next = g_max_nam_atr + 1;
                }

                if (mudstate.attr_next <= g_max_obj_atr)
                {
                    if (nextattr_gotten)
                    {
                        Log.tinyprintf(T(ENDLINE "Warning: +N<next free attr> conflicts object attribute numbers. Adjusting."));
                    }
                    mudstate.attr_next = g_max_nam_atr + 1;
                }

                int max_atr = A_USER_START;
                if (max_atr < g_max_nam_atr)
                {
                    max_atr = g_max_nam_atr;
                }

                if (max_atr < g_max_obj_atr)
                {
                    max_atr = g_max_obj_atr;
                }

                if (max_atr + 1 < mudstate.attr_next)
                {
                    if (nextattr_gotten)
                    {
                        Log.tinyprintf(T(ENDLINE "Info: +N<next free attr> can be safely adjusted down."));
                    }
                    mudstate.attr_next = max_atr + 1;
                }

                if (convert_values)
                {
                    Log.WriteString(T("Converting external database to UTF-8 " ENDLINE));
                    Log.Flush();

                    // Convert every attribute on every object in the external database.
                    //
                    dbref iObject;
                    atr_push();
                    DO_WHOLE_DB(iObject)
                    {
                        unsigned char *as;
                        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
                        {
                            if (  0 < iAttr
                               && iAttr <= anum_alc_top)
                            {
                                const char *pLatin1 = (char *)atr_get_raw(iObject, iAttr);
                                if (nullptr != pLatin1)
                                {
                                    size_t nUnicode;
                                    const UTF8 *pUnicode = ConvertToUTF8(pLatin1, &nUnicode);
                                    atr_add_raw_LEN(iObject, iAttr, pUnicode, nUnicode);
                                }
                            }
                        }
                    }
                    atr_pop();
                }

                *db_version = g_version;
                *db_format = g_format;
                *db_flags = g_flags;
                if (mudstate.bStandAlone)
                {
                    Log.WriteString(T(ENDLINE));
                    Log.Flush();
                }
                else
                {
                    load_player_names();
                }
                return mudstate.db_top;
            }

        case EOF:
            Log.tinyprintf(T(ENDLINE "Unexpected end of file near object #%d" ENDLINE), i);
            return -1;

        default:
            if (mux_isprint_ascii(ch))
            {
                Log.tinyprintf(T(ENDLINE "Illegal character \xE2\x80\x98%c\xE2\x80\x99 near object #%d" ENDLINE), ch, i);
            }
            else
            {
                Log.tinyprintf(T(ENDLINE "Illegal character 0x%02x near object #%d" ENDLINE), ch, i);
            }
            return -1;
        }
    }
}

static bool db_write_object(FILE *f, dbref i, int db_format, int flags)
{
    UNUSED_PARAMETER(db_format);

    ATTR *a;
    int ca, j;

    if (!(flags & V_ATRNAME))
    {
        putstring(f, Name(i));
    }
    putref(f, Location(i));
    putref(f, Zone(i));
    putref(f, Contents(i));
    putref(f, Exits(i));
    putref(f, Link(i));
    putref(f, Next(i));
    putref(f, Owner(i));
    putref(f, Parent(i));
    if (!(flags & V_ATRMONEY))
    {
        putref(f, Pennies(i));
    }
    putref(f, Flags(i));
    putref(f, Flags2(i));
    putref(f, Flags3(i));
    putref(f, Powers(i));
    putref(f, Powers2(i));

    // Write the attribute list.
    //
    if (!(flags & V_DATABASE))
    {
        UTF8 buf[SBUF_SIZE];
        buf[0] = '>';
        unsigned char *as;
        for (ca = atr_head(i, &as); ca; ca = atr_next(&as))
        {
            if (mudstate.bStandAlone)
            {
                j = ca;
            }
            else
            {
                a = atr_num(ca);
                if (!a)
                {
                    continue;
                }
                j = a->number;
            }

            if (j < A_USER_START)
            {
                switch (j)
                {
                case A_NAME:
                    if (!(flags & V_ATRNAME))
                    {
                        continue;
                    }
                    break;

                case A_LIST:
                case A_MONEY:
                    continue;
                }
            }

            // Format is: ">%d\n", j
            //
            const UTF8 *p = atr_get_raw(i, j);
            size_t n = mux_ltoa(j, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(UTF8), n, f);
            putstring(f, p);
        }
        fwrite("<\n", sizeof(UTF8), 2, f);
    }
    return false;
}

dbref db_write(FILE *f, int format, int version)
{
    dbref i;
    int flags;
    ATTR *vp;

    switch (format)
    {
    case F_MUX:
        flags = version;
        break;

    default:
        Log.WriteString(T("Can only write MUX format." ENDLINE));
        return -1;
    }
    if (mudstate.bStandAlone)
    {
        Log.WriteString(T("Writing "));
        Log.Flush();
    }
    i = mudstate.attr_next;
    mux_fprintf(f, T("+X%d\n+S%d\n+N%d\n"), flags, mudstate.db_top, i);
    mux_fprintf(f, T("-R%d\n"), mudstate.record_players);

    // Dump user-named attribute info.
    //
    UTF8 Buffer[LBUF_SIZE];
    Buffer[0] = '+';
    Buffer[1] = 'A';
    int iAttr;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        vp = (ATTR *) anum_get(iAttr);
        if (  vp != nullptr
           && !(vp->flags & AF_DELETED))
        {
            // Format is: "+A%d\n\"%d:%s\"\n", vp->number, vp->flags, vp->name
            //
            UTF8 *pBuffer = Buffer+2;
            pBuffer += mux_ltoa(vp->number, pBuffer);
            *pBuffer++ = '\n';
            *pBuffer++ = '"';
            pBuffer += mux_ltoa(vp->flags, pBuffer);
            *pBuffer++ = ':';
            size_t nNameLength = strlen((char *)vp->name);
            memcpy(pBuffer, vp->name, nNameLength);
            pBuffer += nNameLength;
            *pBuffer++ = '"';
            *pBuffer++ = '\n';
            fwrite(Buffer, sizeof(UTF8), pBuffer-Buffer, f);
        }
    }

    int iDotCounter = 0;
    UTF8 buf[SBUF_SIZE];
    buf[0] = '!';
    DO_WHOLE_DB(i)
    {
        if (mudstate.bStandAlone)
        {
            if (!iDotCounter)
            {
                iDotCounter = 100;
                fputc('.', stderr);
                fflush(stderr);
            }
            iDotCounter--;
        }

        if (!isGarbage(i))
        {
            // Format is: "!%d\n", i
            //
            size_t n = mux_ltoa(i, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(UTF8), n, f);
            db_write_object(f, i, format, flags);
        }
    }
    fputs("***END OF DUMP***\n", f);
    if (mudstate.bStandAlone)
    {
        Log.WriteString(T(ENDLINE));
        Log.Flush();
    }
    return mudstate.db_top;
}
