/*! \file unparse.cpp
 * \brief Produces human-readable key info from boolexps.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mathutil.h"

// Boolexp decompile formats
//
#define F_EXAMINE   1  // Normal
#define F_QUIET     2  // Binary for db dumps
#define F_DECOMPILE 3  // @decompile output
#define F_FUNCTION  4  // [lock()] output

/*
 * Take a dbref (loc) and generate a string.  -1, -3, or (#loc) Note, this
 * will give players object numbers of stuff they don't control, but it's
 * only internal currently, so it's not a problem.
 */

static UTF8 *unparse_object_quiet(dbref player, dbref loc)
{
    UNUSED_PARAMETER(player);

    static UTF8 buf[SBUF_SIZE];

    mux_sprintf(buf, SBUF_SIZE, T("(#%d)"), loc);
    return buf;
}

static UTF8 boolexp_buf[LBUF_SIZE];
static UTF8 *buftop;

static void unparse_boolexp1(dbref player, BOOLEXP *b, UTF8 outer_type, int format)
{
    if (TRUE_BOOLEXP == b)
    {
        if (format == F_EXAMINE)
        {
            safe_str(T("*UNLOCKED*"), boolexp_buf, &buftop);
        }
        return;
    }
    switch (b->type)
    {
    case BOOLEXP_AND:
        if (outer_type == BOOLEXP_NOT)
        {
            safe_chr('(', boolexp_buf, &buftop);
        }
        unparse_boolexp1(player, b->sub1, b->type, format);
        safe_chr(AND_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub2, b->type, format);
        if (outer_type == BOOLEXP_NOT)
        {
            safe_chr(')', boolexp_buf, &buftop);
        }
        break;
    case BOOLEXP_OR:
        if (outer_type == BOOLEXP_NOT || outer_type == BOOLEXP_AND)
        {
            safe_chr('(', boolexp_buf, &buftop);
        }
        unparse_boolexp1(player, b->sub1, b->type, format);
        safe_chr(OR_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub2, b->type, format);
        if (outer_type == BOOLEXP_NOT || outer_type == BOOLEXP_AND)
        {
            safe_chr(')', boolexp_buf, &buftop);
        }
        break;
    case BOOLEXP_NOT:
        safe_chr('!', boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub1, b->type, format);
        break;
    case BOOLEXP_INDIR:
        safe_chr(INDIR_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub1, b->type, format);
        break;
    case BOOLEXP_IS:
        safe_chr(IS_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub1, b->type, format);
        break;
    case BOOLEXP_CARRY:
        safe_chr(CARRY_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub1, b->type, format);
        break;
    case BOOLEXP_OWNER:
        safe_chr(OWNER_TOKEN, boolexp_buf, &buftop);
        unparse_boolexp1(player, b->sub1, b->type, format);
        break;
    case BOOLEXP_CONST:

        if (mudstate.bStandAlone)
        {
            safe_str(unparse_object_quiet(player, b->thing),
                boolexp_buf, &buftop);
        }
        else
        {
            switch (format)
            {
            case F_QUIET:

                // Quiet output - for dumps and internal use. Always #Num.
                //
                safe_str(unparse_object_quiet(player, b->thing),
                     boolexp_buf, &buftop);
                break;

            case F_EXAMINE:

                // Examine output - informative. Name(#Num) or Name.
                //
                UTF8 *buff;

                buff = unparse_object(player, b->thing, false);
                safe_str(buff, boolexp_buf, &buftop);
                free_lbuf(buff);
                break;

            case F_DECOMPILE:

                // Decompile output - should be usable on other MUXes. Name if
                // player, Name if thing, else #Num.
                //
                switch (Typeof(b->thing))
                {
                case TYPE_PLAYER:

                    safe_chr('*', boolexp_buf, &buftop);

                case TYPE_THING:

                    safe_str(Name(b->thing), boolexp_buf, &buftop);
                    break;

                default:

                    safe_tprintf_str(boolexp_buf, &buftop, T("#%d"), b->thing);
                    break;
                }
                break;

            case F_FUNCTION:

                // Function output - must be usable by @lock cmd. Name if player,
                // else #Num.
                //
                switch (Typeof(b->thing))
                {
                case TYPE_PLAYER:

                    safe_chr('*', boolexp_buf, &buftop);
                    safe_str(Name(b->thing), boolexp_buf, &buftop);
                    break;

                default:

                    safe_tprintf_str(boolexp_buf, &buftop, T("#%d"), b->thing);
                    break;
                }
            }
        }
        break;

    case BOOLEXP_ATR:
    case BOOLEXP_EVAL:
        ATTR *ap;

        ap = atr_num(b->thing);
        if (ap && ap->number)
        {
            // Use the attribute name if the attribute exists.
            //
            safe_str(ap->name, boolexp_buf, &buftop);
        }
        else
        {
            // Otherwise use the attribute number.
            //
            // Only god or the db loader can create a new boolexp
            // with an invalid attribute, but anyone may keep a lock
            // whose attribute has subsequently disappeared.
            //
            safe_ltoa(b->thing, boolexp_buf, &buftop);
        }
        if (b->type == BOOLEXP_EVAL)
        {
            safe_chr('/', boolexp_buf, &buftop);
        }
        else
        {
            safe_chr(':', boolexp_buf, &buftop);
        }
        safe_str((UTF8 *)b->sub1, boolexp_buf, &buftop);
        break;

    default:

        // Bad type.
        //
        Log.WriteString(T("ABORT! unparse.cpp, fell off the end of switch in unparse_boolexp1()" ENDLINE));
        Log.Flush();
        abort();
        break;
    }
}

UTF8 *unparse_boolexp_quiet(dbref player, BOOLEXP *b)
{
    buftop = boolexp_buf;
    unparse_boolexp1(player, b, BOOLEXP_CONST, F_QUIET);
    *buftop = '\0';
    return boolexp_buf;
}

UTF8 *unparse_boolexp(dbref player, BOOLEXP *b)
{
    buftop = boolexp_buf;
    unparse_boolexp1(player, b, BOOLEXP_CONST, F_EXAMINE);
    *buftop = '\0';
    return boolexp_buf;
}

UTF8 *unparse_boolexp_decompile(dbref player, BOOLEXP *b)
{
    buftop = boolexp_buf;
    unparse_boolexp1(player, b, BOOLEXP_CONST, F_DECOMPILE);
    *buftop = '\0';
    return boolexp_buf;
}

UTF8 *unparse_boolexp_function(dbref player, BOOLEXP *b)
{
    buftop = boolexp_buf;
    unparse_boolexp1(player, b, BOOLEXP_CONST, F_FUNCTION);
    *buftop = '\0';
    return boolexp_buf;
}
