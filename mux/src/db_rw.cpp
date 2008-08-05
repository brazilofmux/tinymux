// db_rw.cpp
//
// $Id$
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "vattr.h"

static int g_version;
static int g_format;
static int g_flags;

/* ---------------------------------------------------------------------------
 * getboolexp1: Get boolean subexpression from file.
 */

static BOOLEXP *getboolexp1(FILE *f)
{
    BOOLEXP *b;
    char *buff, *s;
    int d, anum;

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
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;

        case INDIR_TOKEN:
            b->type = BOOLEXP_INDIR;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;

        case IS_TOKEN:
            b->type = BOOLEXP_IS;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;

        case CARRY_TOKEN:
            b->type = BOOLEXP_CARRY;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;

        case OWNER_TOKEN:
            b->type = BOOLEXP_OWNER;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;

        default:
            ungetc(c, f);
            b->sub1 = getboolexp1(f);
            if ((c = getc(f)) == '\n')
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
            if ((d = getc(f)) == '\n')
            {
                d = getc(f);
            }
            if (d != ')')
            {
                goto error;
            }
            return b;
        }

    case '-':

        // obsolete NOTHING key, eat it.
        //
        while ((c = getc(f)) != '\n')
        {
            mux_assert(c != EOF);
        }
        ungetc(c, f);
        return TRUE_BOOLEXP;

    case '"':
        ungetc(c, f);
        buff = alloc_lbuf("getboolexp_quoted");
        {
            size_t nBuffer;
            char *pBuffer = getstring_noalloc(f, true, &nBuffer);
            if (LBUF_SIZE - 1 < nBuffer)
            {
                nBuffer = LBUF_SIZE - 1;
                pBuffer[nBuffer] = '\0';
            }
            memcpy(buff, pBuffer, nBuffer+1);
        }

        c = fgetc(f);
        if (c == EOF)
        {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }

        b = alloc_bool("getboolexp1_quoted");
        anum = mkattr(GOD, buff);
        if (anum <= 0)
        {
            free_bool(b);
            free_lbuf(buff);
            goto error;
        }
        free_lbuf(buff);
        b->thing = anum;

        // If last character is : then this is an attribute lock. A
        // last character of / means an eval lock.
        //
        if (  c == ':'
           || c == '/')
        {
            if (c == '/')
            {
                b->type = BOOLEXP_EVAL;
            }
            else
            {
                b->type = BOOLEXP_ATR;
            }

            buff = alloc_lbuf("getboolexp1.attr_lock");
            size_t nBuffer;
            char *pBuffer = getstring_noalloc(f, true, &nBuffer);
            if (LBUF_SIZE - 1 < nBuffer)
            {
                nBuffer = LBUF_SIZE - 1;
                pBuffer[nBuffer] = '\0';
            }
            memcpy(buff, pBuffer, nBuffer+1);

            b->sub1 = (BOOLEXP *)StringClone(buff);
            free_lbuf(buff);
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
        else if (mux_AttrNameInitialSet(c))
        {
            buff = alloc_lbuf("getboolexp1.atr_name");

            for (  s = buff;

                   (c = getc(f)) != EOF
                && c != '\n'
                && c != ':'
                && c != '/'
                && s < buff + LBUF_SIZE;

                   *s++ = (char)c)
            {
                ; // Nothing.
            }

            if (c == EOF)
            {
                free_lbuf(buff);
                free_bool(b);
                goto error;
            }
            *s = '\0';

            // Look the name up as an attribute. If not found, create a new
            // attribute.
            //
            anum = mkattr(GOD, buff);
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
        if (  c == ':'
           || c == '/')
        {
            if (c == '/')
            {
                b->type = BOOLEXP_EVAL;
            }
            else
            {
                b->type = BOOLEXP_ATR;
            }
            buff = alloc_lbuf("getboolexp1.attr_lock");
            for (  s = buff;

                   (c = getc(f)) != EOF
                && c != '\n'
                && c != ')'
                && c != OR_TOKEN
                && c != AND_TOKEN
                && s < buff + LBUF_SIZE;

                   *s++ = (char)c)
            {
                ; // Nothing
            }
            if (c == EOF)
            {
                goto error;
            }
            *s++ = 0;
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
    char *buff = alloc_lbuf("get_list");
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
#if defined(FIRANMUX_CONVERT)
                switch (atr)
                {
                case A_COLOR_OLD:
                    atr = A_COLOR;
                    break;

                case A_ALEAD_OLD:
                    atr = A_ALEAD;
                    break;

                case A_EXITFORMAT_OLD:
                    atr = A_EXITFORMAT;
                    break;
                }
#endif // FIRANMUX_CONVERT

                // Maximum attribute number across all objects.
                //
                if (g_max_obj_atr < atr)
                {
                    g_max_obj_atr = atr;
                }

                // Store the attr
                //
                size_t nBuffer;
                const char *pBuffer;
                if (3 == g_version)
                {
                    size_t nBufferUnicode;
                    UTF8 *pBufferUnicode = (UTF8 *)getstring_noalloc(f, true, &nBufferUnicode);
                    pBufferUnicode = convert_color(pBufferUnicode);
                    if (ATR_INFO_CHAR != pBufferUnicode[0])
                    {
                        pBuffer = ConvertToLatin(pBufferUnicode);
                    }
                    else
                    {
                        pBuffer = tprintf("%c%s", ATR_INFO_CHAR, ConvertToLatin(pBufferUnicode+1));
                    }
                    nBuffer = strlen(pBuffer);
                }
                else
                {
                    pBuffer = getstring_noalloc(f, true, &nBuffer);
                }
                atr_add_raw_LEN(i, atr, pBuffer, nBuffer);
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
                Log.tinyprintf("No line feed on object %d" ENDLINE, i);
                return true;
            }
            return true;

        default:
            Log.tinyprintf("Bad character '%c' when getting attributes on object %d" ENDLINE, c, i);

            // We've found a bad spot.  I hope things aren't too bad.
            //
            {
                size_t nBuffer;
                (void)getstring_noalloc(f, true, &nBuffer);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * putbool_subexp: Write a boolean sub-expression to the flat file.
 */
static void putbool_subexp(FILE *f, BOOLEXP *b)
{
    ATTR *va;

    switch (b->type)
    {
    case BOOLEXP_IS:

        putc('(', f);
        putc(IS_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_CARRY:

        putc('(', f);
        putc(CARRY_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_INDIR:

        putc('(', f);
        putc(INDIR_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_OWNER:

        putc('(', f);
        putc(OWNER_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_AND:

        putc('(', f);
        putbool_subexp(f, b->sub1);
        putc(AND_TOKEN, f);
        putbool_subexp(f, b->sub2);
        putc(')', f);
        break;

    case BOOLEXP_OR:

        putc('(', f);
        putbool_subexp(f, b->sub1);
        putc(OR_TOKEN, f);
        putbool_subexp(f, b->sub2);
        putc(')', f);
        break;

    case BOOLEXP_NOT:

        putc('(', f);
        putc(NOT_TOKEN, f);
        putbool_subexp(f, b->sub1);
        putc(')', f);
        break;

    case BOOLEXP_CONST:

        putref(f, b->thing);
        break;

    case BOOLEXP_ATR:

        va = atr_num(b->thing);
        if (va)
        {
            fprintf(f, "%s:%s", va->name, (char *)b->sub1);
        }
        else
        {
            fprintf(f, "%d:%s\n", b->thing, (char *)b->sub1);
        }
        break;

    case BOOLEXP_EVAL:

        va = atr_num(b->thing);
        if (va)
        {
            fprintf(f, "%s/%s\n", va->name, (char *)b->sub1);
        }
        else
        {
            fprintf(f, "%d/%s\n", b->thing, (char *)b->sub1);
        }
        break;

    default:

        Log.tinyprintf("Unknown boolean type in putbool_subexp: %d" ENDLINE, b->type);
        break;
    }
}

/* ---------------------------------------------------------------------------
 * putboolexp: Write boolean expression to the flat file.
 */

static void putboolexp(FILE *f, BOOLEXP *b)
{
    if (b != TRUE_BOOLEXP)
    {
        putbool_subexp(f, b);
    }
    putc('\n', f);
}

dbref db_read(FILE *f, int *db_format, int *db_version, int *db_flags)
{
    dbref i, anum;
    int ch;
    const char *tstr;
    int aflags;
    BOOLEXP *tempbool;
    char *buff;
    size_t nVisualWidth;
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
    char *pName;

    int iDotCounter = 0;
    if (mudstate.bStandAlone)
    {
        Log.WriteString("Reading ");
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
                tstr = getstring_noalloc(f, true, &nBuffer);
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

                if (3 == g_version)
                {
                    tstr = ConvertToLatin((UTF8 *)tstr);
                }

                pName = MakeCanonicalAttributeName(tstr, &nName, &bValid);
                if (bValid)
                {
#if defined(FIRANMUX_CONVERT)
                    {
                        // Does this user attribute conflict with any built-in attribute names?
                        //
                        ATTR *a = (ATTR *)hashfindLEN(pName, nName, &mudstate.attr_name_htab);
                        if (a)
                        {
                            Log.tinyprintf("Renaming conflicting user attribute, %s, to FIRAN_%s." ENDLINE, pName, pName);
                            char *p = alloc_lbuf("db_read");
                            char *q = p;
                            safe_str("FIRAN_", p, &q);
                            safe_str(pName, p, &q);
                            *q = '\0';

                            pName = MakeCanonicalAttributeName(p, &nName, &bValid);
                            free_lbuf(p);

                            a = vattr_find_LEN(pName, nName);
                            if (a)
                            {
                                Log.tinyprintf("ERROR: Renamed user attribute, %s, already exists." ENDLINE, pName);
                            }
                        }
                    }

                    if (bValid)
#endif // FIRANMUX_CONVERT
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
            }
            else if (ch == 'X')
            {
                // MUX VERSION
                //
                if (header_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate MUX version header entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, false, &nBuffer);
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
                        Log.tinyprintf(ENDLINE "Unsupported flatfile version: %d." ENDLINE, g_version);
                        return -1;
                    }

                    mux_assert(  (  (  1 == g_version
                                    || 2 == g_version)
                                 && (g_flags & MANDFLAGS_V2) == MANDFLAGS_V2)
                              || (  3 == g_version
                                 && (g_flags & MANDFLAGS_V3) == MANDFLAGS_V3));

                    // Otherwise extract feature flags
                    //
                    if (g_flags & V_DATABASE)
                    {
                        if (3 == g_version)
                        {
                            // We'll convert the external database from UTF-8
                            // to Latin-1 at the end.
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
                    Log.tinyprintf(ENDLINE "Duplicate size entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, false, &nBuffer);
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
                    Log.tinyprintf(ENDLINE "Duplicate next free vattr entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, false, &nBuffer);
                }
                else
                {
                    mudstate.attr_next = getref(f);
                    nextattr_gotten = true;
                }
            }
            else
            {
                Log.tinyprintf(ENDLINE "Unexpected character '%c' in MUX header near object #%d, ignored." ENDLINE, ch, i);
                tstr = getstring_noalloc(f, false, &nBuffer);
            }
            break;

        case '!':   // MUX entry
            i = getref(f);
            db_grow(i + 1);

            if (read_name)
            {
                tstr = getstring_noalloc(f, true, &nBuffer);
                if (3 == g_version)
                {
                    tstr = (char *)convert_color((UTF8 *)tstr);
                    tstr = ConvertToLatin((UTF8 *)tstr);
                }
                buff = alloc_lbuf("dbread.s_Name");
                (void)ANSI_TruncateToField(tstr, MBUF_SIZE, buff, MBUF_SIZE,
                    &nVisualWidth, ANSI_ENDGOAL_NORMAL);
                s_Name(i, buff);
                free_lbuf(buff);

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
                tempbool = getboolexp(f);
                atr_add_raw(i, A_LOCK,
                unparse_boolexp_quiet(1, tempbool));
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
                    Log.tinyprintf(ENDLINE "Error reading attrs for object #%d" ENDLINE, i);
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
            tstr = getstring_noalloc(f, false, &nBuffer);
            if (strncmp(tstr, "**END OF DUMP***", 16))
            {
                Log.tinyprintf(ENDLINE "Bad EOF marker at object #%d" ENDLINE, i);
                return -1;
            }
            else
            {
                // Attribute number warnings.
                //
                if (g_max_nam_atr < g_max_obj_atr)
                {
                    Log.tinyprintf(ENDLINE "Warning: One or more attribute values are unnamed. Did you use ./Backup on a running game?");
                }

                if (!nextattr_gotten)
                {
                    Log.tinyprintf(ENDLINE "Warning: Missing +N<next free>. Adjusting.");
                }

                if (mudstate.attr_next <= g_max_nam_atr)
                {
                    if (nextattr_gotten)
                    {
                        Log.tinyprintf(ENDLINE "Warning: +N<next free attr> conflicts with existing attribute names. Adjusting.");
                    }
                    mudstate.attr_next = g_max_nam_atr + 1;
                }

                if (mudstate.attr_next <= g_max_obj_atr)
                {
                    if (nextattr_gotten)
                    {
                        Log.tinyprintf(ENDLINE "Warning: +N<next free attr> conflicts object attribute numbers. Adjusting.");
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
                        Log.tinyprintf(ENDLINE "Info: +N<next free attr> can be safely adjusted down.");
                    }
                    mudstate.attr_next = max_atr + 1;
                }

                if (convert_values)
                {
                    Log.WriteString("Converting external database to Latin-1 " ENDLINE);
                    Log.Flush();

                    // Convert every attribute on every object in the external database.
                    //
                    dbref iObject;
                    atr_push();
                    DO_WHOLE_DB(iObject)
                    {
                        char *as;
                        for (int iAttr = atr_head(iObject, &as); iAttr; iAttr = atr_next(&as))
                        {
                            if (  0 < iAttr
                               && iAttr <= anum_alc_top)
                            {
                                const UTF8 *pUnicode = (const UTF8 *)atr_get_raw(iObject, iAttr);
                                if (NULL != pUnicode)
                                {
                                    const char *pLatin;

                                    pUnicode = convert_color(pUnicode);
                                    if (ATR_INFO_CHAR != pUnicode[0])
                                    {
                                        pLatin = ConvertToLatin(pUnicode);
                                    }
                                    else
                                    {
                                        pLatin = tprintf("%c%s", ATR_INFO_CHAR, ConvertToLatin(pUnicode+1));
                                    }
                                    size_t nLatin = strlen(pLatin);
                                    atr_add_raw_LEN(iObject, iAttr, pLatin, nLatin);
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
                    Log.WriteString(ENDLINE);
                    Log.Flush();
                }
                else
                {
                    load_player_names();
                }
                return mudstate.db_top;
            }

        case EOF:
            Log.tinyprintf(ENDLINE "Unexpected end of file near object #%d" ENDLINE, i);
            return -1;

        default:
            if (mux_isprint(ch))
            {
                Log.tinyprintf(ENDLINE "Illegal character '%c' near object #%d" ENDLINE, ch, i);
            }
            else
            {
                Log.tinyprintf(ENDLINE "Illegal character 0x%02x near object #%d" ENDLINE, ch, i);
            }
            return -1;
        }
    }
}

static bool db_write_object(FILE *f, dbref i, int db_format, int flags)
{
    UNUSED_PARAMETER(db_format);

    ATTR *a;
    char *got, *as;
    dbref aowner;
    int ca, aflags, j;
    BOOLEXP *tempbool;

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
    if (!(flags & V_ATRKEY))
    {
        got = atr_get(i, A_LOCK, &aowner, &aflags);
        tempbool = parse_boolexp(GOD, got, true);
        free_lbuf(got);
        putboolexp(f, tempbool);
        if (tempbool)
        {
            free_boolexp(tempbool);
        }
    }
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
        char buf[SBUF_SIZE];
        buf[0] = '>';
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

                case A_LOCK:
                    if (!(flags & V_ATRKEY))
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
            const char *p = atr_get_raw(i, j);
            size_t n = mux_ltoa(j, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(char), n, f);
            putstring(f, p);
        }
        fwrite("<\n", sizeof(char), 2, f);
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
        Log.WriteString("Can only write MUX format." ENDLINE);
        return -1;
    }
    if (mudstate.bStandAlone)
    {
        Log.WriteString("Writing ");
        Log.Flush();
    }
    i = mudstate.attr_next;
    fprintf(f, "+X%d\n+S%d\n+N%d\n", flags, mudstate.db_top, i);
    fprintf(f, "-R%d\n", mudstate.record_players);

    // Dump user-named attribute info.
    //
    char Buffer[LBUF_SIZE];
    Buffer[0] = '+';
    Buffer[1] = 'A';
    int iAttr;
    for (iAttr = A_USER_START; iAttr <= anum_alc_top; iAttr++)
    {
        vp = (ATTR *) anum_get(iAttr);
        if (  vp != NULL
           && !(vp->flags & AF_DELETED))
        {
            // Format is: "+A%d\n\"%d:%s\"\n", vp->number, vp->flags, vp->name
            //
            char *pBuffer = Buffer+2;
            pBuffer += mux_ltoa(vp->number, pBuffer);
            *pBuffer++ = '\n';
            *pBuffer++ = '"';
            pBuffer += mux_ltoa(vp->flags, pBuffer);
            *pBuffer++ = ':';
            size_t nNameLength = strlen(vp->name);
            memcpy(pBuffer, vp->name, nNameLength);
            pBuffer += nNameLength;
            *pBuffer++ = '"';
            *pBuffer++ = '\n';
            fwrite(Buffer, sizeof(char), pBuffer-Buffer, f);
        }
    }

    int iDotCounter = 0;
    char buf[SBUF_SIZE];
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
            fwrite(buf, sizeof(char), n, f);
            db_write_object(f, i, format, flags);
        }
    }
    fputs("***END OF DUMP***\n", f);
    if (mudstate.bStandAlone)
    {
        Log.WriteString(ENDLINE);
        Log.Flush();
    }
    return mudstate.db_top;
}
