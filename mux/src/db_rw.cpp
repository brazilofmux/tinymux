// db_rw.cpp
//
// $Id: db_rw.cpp,v 1.9 2002-08-14 00:06:57 jake Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#ifdef STANDALONE
#undef MEMORY_BASED
#endif
#include "externs.h"

#include "attrs.h"
#include "vattr.h"

extern void db_grow(dbref);

extern struct object *db;

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
        Tiny_Assert(0);
        break;

    case '(':
        b = alloc_bool("getboolexp1.openparen");
        switch (c = getc(f))
        {
        case NOT_TOKEN:
            b->type = BOOLEXP_NOT;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case INDIR_TOKEN:
            b->type = BOOLEXP_INDIR;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case IS_TOKEN:
            b->type = BOOLEXP_IS;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case CARRY_TOKEN:
            b->type = BOOLEXP_CARRY;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        case OWNER_TOKEN:
            b->type = BOOLEXP_OWNER;
            b->sub1 = getboolexp1(f);
            if ((d = getc(f)) == '\n')
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        default:
            ungetc(c, f);
            b->sub1 = getboolexp1(f);
            if ((c = getc(f)) == '\n')
                c = getc(f);
            switch (c) {
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
                d = getc(f);
            if (d != ')')
                goto error;
            return b;
        }

    case '-':

        // obsolete NOTHING key, eat it.
        //
        while ((c = getc(f)) != '\n')
        {
            Tiny_Assert(c != EOF);
        }
        ungetc(c, f);
        return TRUE_BOOLEXP;
        break;

    case '"':
        ungetc(c, f);
        buff = alloc_lbuf("getboolexp_quoted");
        StringCopy(buff, getstring_noalloc(f, 1));
        c = fgetc(f);
        if (c == EOF)
        {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }

        b = alloc_bool("getboolexp1_quoted");
        anum = mkattr(buff);
        if (anum <= 0)
        {
            free_bool(b);
            free_lbuf(buff);
            goto error;
        }
        free_lbuf(buff);
        b->thing = anum;

        // if last character is : then this is an attribute lock. A
        // last character of / means an eval lock.
        //
        if ((c == ':') || (c == '/'))
        {
            if (c == '/')
                b->type = BOOLEXP_EVAL;
            else
                b->type = BOOLEXP_ATR;
            buff = alloc_lbuf("getboolexp1.attr_lock");
            StringCopy(buff, getstring_noalloc(f, 1));
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
        if (Tiny_IsDigit[(unsigned int)c])
        {
            while (Tiny_IsDigit[(unsigned int)(c = getc(f))])
            {
                b->thing = b->thing * 10 + c - '0';
            }
        }
        else if (Tiny_IsFirstAttributeNameCharacter[(unsigned char)c])
        {
            buff = alloc_lbuf("getboolexp1.atr_name");

            for (s = buff;
                 ((c = getc(f)) != EOF) && (c != '\n') &&
                 (c != ':') && (c != '/');
                 *s++ = c) ;

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
            anum = mkattr(buff);
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

        // if last character is : then this is an attribute lock. A last
        // character of / means an eval lock.
        //
        if ((c == ':') || (c == '/'))
        {
            if (c == '/')
                b->type = BOOLEXP_EVAL;
            else
                b->type = BOOLEXP_ATR;
            buff = alloc_lbuf("getboolexp1.attr_lock");
            for (  s = buff;

                   ((c = getc(f)) != EOF)
                && (c != '\n')
                && (c != ')')
                && (c != OR_TOKEN)
                && (c != AND_TOKEN);

                   *s++ = c)
            {
                // Nothing
            }
            if (c == EOF)
                goto error;
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
    Tiny_Assert(0);
    return TRUE_BOOLEXP;
}

/* ---------------------------------------------------------------------------
 * getboolexp: Read a boolean expression from the flat file.
 */

static BOOLEXP *getboolexp(FILE *f)
{
    BOOLEXP *b = getboolexp1(f);
    char c = getc(f);
    Tiny_Assert(c == '\n');

    if (g_format == F_MUX)
    {
        if ((c = getc(f)) != '\n')
        {
            ungetc(c, f);
        }
    }
    return b;
}

/* ---------------------------------------------------------------------------
 * get_list: Read attribute list from flat file.
 */

static BOOL get_list(FILE *f, dbref i)
{
    char *buff = alloc_lbuf("get_list");
    while (1)
    {
        dbref atr;
        int c;
        switch (c = getc(f))
        {
        case '>':   // read # then string
            atr = getref(f);
            if (atr > 0)
            {
                // Store the attr
                //
                atr_add_raw(i, atr, getstring_noalloc(f, TRUE));
            }
            else
            {
                // Silently discard
                //
                getstring_noalloc(f, TRUE);
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
                return TRUE;
            }
            return TRUE;

        default:
            Log.tinyprintf("Bad character '%c' when getting attributes on object %d" ENDLINE, c, i);

            // We've found a bad spot.  I hope things aren't too bad.
            //
            getstring_noalloc(f, TRUE);
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
    int len;
    int nVisualWidth;

    g_format = F_UNKNOWN;
    g_version = 0;
    g_flags = 0;

    BOOL header_gotten = FALSE;
    BOOL size_gotten = FALSE;
    BOOL nextattr_gotten = FALSE;

    BOOL read_attribs = TRUE;
    BOOL read_name = TRUE;
    BOOL read_key = TRUE;
    BOOL read_money = TRUE;

    int nName;
    BOOL bValid;
    char *pName;

#ifdef STANDALONE
    Log.WriteString("Reading ");
    Log.Flush();
    int iDotCounter = 0;
#endif
    db_free();
    for (i = 0;; i++)
    {
#ifdef STANDALONE
        if (!iDotCounter)
        {
            iDotCounter = 100;
            fputc('.', stderr);
            fflush(stderr);
        }
        iDotCounter--;
#endif

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
                tstr = getstring_noalloc(f, TRUE);
                if (Tiny_IsDigit[(unsigned char)*tstr])
                {
                    aflags = 0;
                    while (Tiny_IsDigit[(unsigned char)*tstr])
                    {
                        aflags = (aflags * 10) + (*tstr++ - '0');
                    }
                    tstr++; // skip ':'
                }
                else
                {
                    aflags = mudconf.vattr_flags;
                }
                pName = MakeCanonicalAttributeName(tstr, &nName, &bValid);
                if (bValid)
                {
                    vattr_define_LEN(pName, nName, anum, aflags);
                }
            }
            else if (ch == 'X')
            {
                // MUX VERSION
                //
                if (header_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate MUX version header entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                }
                else
                {
                    header_gotten = TRUE;
                    g_format = F_MUX;
                    g_version = getref(f);
                    Tiny_Assert((g_version & MANDFLAGS) == MANDFLAGS);

                    // Otherwise extract feature flags
                    //
                    if (g_version & V_DATABASE)
                    {
                        read_attribs = FALSE;
                        read_name = !(g_version & V_ATRNAME);
                    }
                    read_key = !(g_version & V_ATRKEY);
                    read_money = !(g_version & V_ATRMONEY);
                    g_flags = g_version & ~V_MASK;

                    g_version &= V_MASK;
                }
            }
            else if (ch == 'S')
            {
                // SIZE
                //
                if (size_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate size entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                }
                else
                {
                    mudstate.min_size = getref(f);
                    size_gotten = TRUE;
                }
            }
            else if (ch == 'N')
            {
                // NEXT ATTR TO ALLOC WHEN NO FREELIST
                //
                if (nextattr_gotten)
                {
                    Log.tinyprintf(ENDLINE "Duplicate next free vattr entry at object %d, ignored." ENDLINE, i);
                    tstr = getstring_noalloc(f, 0);
                }
                else
                {
                    mudstate.attr_next = getref(f);
                    nextattr_gotten = TRUE;
                }
            }
            else
            {
                Log.tinyprintf(ENDLINE "Unexpected character '%c' in MUX header near object #%d, ignored." ENDLINE, ch, i);
                tstr = getstring_noalloc(f, 0);
            }
            break;

        case '!':   // MUX entry
            i = getref(f);
            db_grow(i + 1);

            if (read_name)
            {
                tstr = getstring_noalloc(f, TRUE);
                buff = alloc_lbuf("dbread.s_Name");
                len = ANSI_TruncateToField(tstr, MBUF_SIZE, buff, MBUF_SIZE,
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
                s_Pennies(i, getref(f));
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
            tstr = getstring_noalloc(f, 0);
            if (strncmp(tstr, "**END OF DUMP***", 16))
            {
                Log.tinyprintf(ENDLINE "Bad EOF marker at object #%d" ENDLINE, i);
                return -1;
            }
            else
            {
                *db_version = g_version;
                *db_format = g_format;
                *db_flags = g_flags;
#ifdef STANDALONE
                Log.WriteString(ENDLINE);
                Log.Flush();
#else
                load_player_names();
#endif
                return mudstate.db_top;
            }

        case EOF:
            Log.tinyprintf(ENDLINE "Unexpected end of file near object #%d" ENDLINE, i);
            return -1;

        default:
            if (Tiny_IsPrint[(unsigned char)ch])
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

static BOOL db_write_object(FILE *f, dbref i, int db_format, int flags)
{
#ifndef STANDALONE
    ATTR *a;
#endif // !STANDALONE

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
        tempbool = parse_boolexp(GOD, got, TRUE);
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
#ifndef STANDALONE
            a = atr_num(ca);
            if (!a)
            {
                continue;
            }
            j = a->number;
#else // !STANDALONE
            j = ca;
#endif // !STANDALONE
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
            int n = Tiny_ltoa(j, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(char), n, f);
            putstring(f, p);
        }
        fwrite("<\n", sizeof(char), 2, f);
    }
    return FALSE;
}

extern int anum_alc_top;

dbref db_write(FILE *f, int format, int version)
{
    dbref i;
    int flags;
    ATTR *vp;

#ifndef MEMORY_BASED
    al_store();
#endif

    switch (format)
    {
    case F_MUX:
        flags = version;
        break;
    default:
        Log.WriteString("Can only write MUX format." ENDLINE);
        return -1;
    }
#ifdef STANDALONE
    Log.WriteString("Writing ");
    Log.Flush();
#endif // STANDALONE
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
            pBuffer += Tiny_ltoa(vp->number, pBuffer);
            *pBuffer++ = '\n';
            *pBuffer++ = '"';
            pBuffer += Tiny_ltoa(vp->flags, pBuffer);
            *pBuffer++ = ':';
            int nNameLength = strlen(vp->name);
            memcpy(pBuffer, vp->name, nNameLength);
            pBuffer += nNameLength;
            *pBuffer++ = '"';
            *pBuffer++ = '\n';
            fwrite(Buffer, sizeof(char), pBuffer-Buffer, f);
        }
    }

#ifdef STANDALONE
    int iDotCounter = 0;
#endif // STANDALONE
    char buf[SBUF_SIZE];
    buf[0] = '!';
    DO_WHOLE_DB(i)
    {
#ifdef STANDALONE
        if (!iDotCounter)
        {
            iDotCounter = 100;
            fputc('.', stderr);
            fflush(stderr);
        }
        iDotCounter--;
#endif

        if (!isGarbage(i))
        {
            // Format is: "!%d\n", i
            //
            int n = Tiny_ltoa(i, buf+1) + 1;
            buf[n++] = '\n';
            fwrite(buf, sizeof(char), n, f);
            db_write_object(f, i, format, flags);
        }
    }
    fputs("***END OF DUMP***\n", f);
#ifdef STANDALONE
    Log.WriteString(ENDLINE);
    Log.Flush();
#endif // STANDALONE
    return mudstate.db_top;
}
