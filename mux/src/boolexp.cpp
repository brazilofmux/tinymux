// boolexp.cpp
//
// $Id: boolexp.cpp,v 1.9 2003-02-05 06:20:58 jake Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"

static bool parsing_internal = false;

/* ---------------------------------------------------------------------------
 * check_attr: indicate if attribute ATTR on player passes key when checked by
 * the object lockobj
 */

static bool check_attr(dbref player, dbref lockobj, ATTR *attr, char *key)
{
    dbref aowner;
    int aflags;
    bool bCheck = false;

    char *buff = atr_pget(player, attr->number, &aowner, &aflags);

    if (attr->number == A_LENTER)
    {
        // We can see enterlocks... else we'd break zones.
        //
        bCheck = true;
    }
    else if (See_attr(lockobj, player, attr))
    {
        bCheck = true;
    }
    else if (attr->number == A_NAME)
    {
        bCheck = true;
    }

    if (  bCheck
       && !wild_match(key, buff))
    {
        bCheck = false;
    }
    free_lbuf(buff);
    return bCheck;
}

bool eval_boolexp(dbref player, dbref thing, dbref from, BOOLEXP *b)
{
    if (b == TRUE_BOOLEXP)
    {
        return true;
    }

    dbref aowner, obj, source;
    int aflags;
    char *key, *buff, *buff2, *bp, *str;
    ATTR *a;
    bool bCheck, c;

    switch (b->type)
    {
    case BOOLEXP_AND:
        return   eval_boolexp(player, thing, from, b->sub1)
              && eval_boolexp(player, thing, from, b->sub2);

    case BOOLEXP_OR:
        return   eval_boolexp(player, thing, from, b->sub1)
              || eval_boolexp(player, thing, from, b->sub2);

    case BOOLEXP_NOT:
        return !eval_boolexp(player, thing, from, b->sub1);

    case BOOLEXP_INDIR:

        // BOOLEXP_INDIR (i.e. @) is a unary operation which is replaced at
        // evaluation time by the lock of the object whose number is the
        // argument of the operation.
        //
        mudstate.lock_nest_lev++;
        if (mudstate.lock_nest_lev >= mudconf.lock_nest_lim)
        {
            if (mudstate.bStandAlone)
            {
                Log.WriteString("Lock exceeded recursion limit." ENDLINE);
            }
            else
            {
                STARTLOG(LOG_BUGS, "BUG", "LOCK");
                log_name_and_loc(player);
                log_text(": Lock exceeded recursion limit.");
                ENDLOG;
                notify(player, "Sorry, broken lock!");
            }
            mudstate.lock_nest_lev--;
            return false;
        }
        if (  b->sub1->type != BOOLEXP_CONST
           || b->sub1->thing < 0)
        {
            if (mudstate.bStandAlone)
            {
                Log.WriteString("Broken lock." ENDLINE);
            }
            else
            {
                STARTLOG(LOG_BUGS, "BUG", "LOCK");
                log_name_and_loc(player);
                buff = alloc_mbuf("eval_boolexp.LOG.indir");
                sprintf(buff, ": Lock had bad indirection (%c, type %d)",
                    INDIR_TOKEN, b->sub1->type);
                log_text(buff);
                free_mbuf(buff);
                ENDLOG;
                notify(player, "Sorry, broken lock!");
            }
            mudstate.lock_nest_lev--;
            return false;
        }
        key = atr_get(b->sub1->thing, A_LOCK, &aowner, &aflags);
        c = eval_boolexp_atr(player, b->sub1->thing, from, key);
        free_lbuf(key);
        mudstate.lock_nest_lev--;
        return c;

    case BOOLEXP_CONST:
        return   b->thing == player
              || member(b->thing, Contents(player));

    case BOOLEXP_ATR:
        a = atr_num(b->thing);
        if (!a)
        {
            // No such attribute.
            //
            return false;
        }

        // First check the object itself, then its contents.
        //
        if (check_attr(player, from, a, (char *)b->sub1))
        {
            return true;
        }
        DOLIST(obj, Contents(player))
        {
            if (check_attr(obj, from, a, (char *)b->sub1))
            {
                return true;
            }
        }
        return false;

    case BOOLEXP_EVAL:

        a = atr_num(b->thing);
        if (!a)
        {
            // No such attribute.
            //
            return false;
        }
        source = from;
        buff = atr_pget(from, a->number, &aowner, &aflags);
        if (!buff || !*buff)
        {
            free_lbuf(buff);
            buff = atr_pget(thing, a->number, &aowner, &aflags);
            source = thing;
        }
        bCheck = false;

        if (  a->number == A_NAME
           || a->number == A_LENTER)
        {
            bCheck = true;
        }
        else if (bCanReadAttr(source, source, a, false))
        {
            bCheck = true;
        }
        if (bCheck)
        {
            char **preserve = NULL;
            int *preserve_len = NULL;
            preserve = PushPointers(MAX_GLOBAL_REGS);
            preserve_len = PushIntegers(MAX_GLOBAL_REGS);
            save_global_regs("eval_boolexp_save", preserve, preserve_len);

            buff2 = bp = alloc_lbuf("eval_boolexp");
            str = buff;
            mux_exec(buff2, &bp, source, player, player,
                EV_FIGNORE | EV_EVAL | EV_FCHECK | EV_TOP, &str,
                (char **)NULL, 0);
            *bp = '\0';

            restore_global_regs("eval_boolexp_save", preserve, preserve_len);
            PopIntegers(preserve_len, MAX_GLOBAL_REGS);
            PopPointers(preserve, MAX_GLOBAL_REGS);

            bCheck = !string_compare(buff2, (char *)b->sub1);
            free_lbuf(buff2);
        }
        free_lbuf(buff);
        return bCheck;

    case BOOLEXP_IS:

        // If an object check, do that.
        //
        if (b->sub1->type == BOOLEXP_CONST)
        {
            return (b->sub1->thing == player);
        }

        // Nope, do an attribute check
        //
        a = atr_num(b->sub1->thing);
        if (!a)
        {
            return false;
        }
        return check_attr(player, from, a, (char *)(b->sub1)->sub1);

    case BOOLEXP_CARRY:

        // If an object check, do that
        //
        if (b->sub1->type == BOOLEXP_CONST)
        {
            return member(b->sub1->thing, Contents(player));
        }

        // Nope, do an attribute check
        //
        a = atr_num(b->sub1->thing);
        if (!a)
        {
            return false;
        }
        DOLIST(obj, Contents(player))
        {
            if (check_attr(obj, from, a, (char *)(b->sub1)->sub1))
            {
                return true;
            }
        }
        return false;

    case BOOLEXP_OWNER:

        return (Owner(b->sub1->thing) == Owner(player));

    default:

        // Bad type
        //
        mux_assert(0);
        return false;
    }
}

bool eval_boolexp_atr(dbref player, dbref thing, dbref from, char *key)
{
    bool ret_value;

    BOOLEXP *b = parse_boolexp(player, key, true);
    if (b == NULL)
    {
        ret_value = true;
    }
    else
    {
        ret_value = eval_boolexp(player, thing, from, b);
        free_boolexp(b);
    }
    return ret_value;
}

// If the parser returns TRUE_BOOLEXP, you lose
// TRUE_BOOLEXP cannot be typed in by the user; use @unlock instead
//
static const char *parsebuf;
static char parsestore[LBUF_SIZE];
static dbref parse_player;

static void skip_whitespace(void)
{
    while (mux_isspace[(unsigned char)*parsebuf])
    {
        parsebuf++;
    }
}

// Defined below.
//
static BOOLEXP *parse_boolexp_E(void);

static BOOLEXP *test_atr(char *s)
{
    char *s1;
    int anum, locktype;

    char *buff = alloc_lbuf("test_atr");
    strcpy(buff, s);
    for (s = buff; *s && (*s != ':') && (*s != '/'); s++)
    {
        ; // Nothing.
    }
    if (!*s)
    {
        free_lbuf(buff);
        return TRUE_BOOLEXP;
    }
    if (*s == '/')
    {
        locktype = BOOLEXP_EVAL;
    }
    else
    {
        locktype = BOOLEXP_ATR;
    }

    *s++ = '\0';

    // See if left side is valid attribute.  Access to attr is checked on eval
    // Also allow numeric references to attributes. It can't hurt us, and lets
    // us import stuff that stores attr locks by number instead of by name.
    //
    ATTR *attrib = atr_str(buff);
    if (!attrib)
    {
        // Only #1 can lock on numbers
        //
        if (!God(parse_player))
        {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }
        for (s1 = buff; mux_isdigit[(unsigned char)*s1]; s1++)
        {
            ; // Nothing.
        }
        if (*s1)
        {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }
        anum = mux_atol(buff);
        if (anum <= 0)
        {
            free_lbuf(buff);
            return TRUE_BOOLEXP;
        }
    }
    else
    {
        anum = attrib->number;
    }

    // made it now make the parse tree node
    //
    BOOLEXP *b = alloc_bool("test_str");
    b->type = locktype;
    b->thing = (dbref) anum;
    b->sub1 = (BOOLEXP *) StringClone(s);
    free_lbuf(buff);
    return b;
}

// L -> (E); L -> object identifier
//
static BOOLEXP *parse_boolexp_L(void)
{
    BOOLEXP *b;
    char *p, *buf;
    MSTATE mstate;

    buf = NULL;
    skip_whitespace();

    switch (*parsebuf)
    {
    case '(':
        parsebuf++;
        b = parse_boolexp_E();
        skip_whitespace();
        if (  b == TRUE_BOOLEXP
           || *parsebuf++ != ')')
        {
            free_boolexp(b);
            return TRUE_BOOLEXP;
        }
        break;
    default:

        // Must have hit an object ref.  Load the name into our buffer.
        //
        buf = alloc_lbuf("parse_boolexp_L");
        p = buf;
        while (  *parsebuf
              && *parsebuf != AND_TOKEN
              && *parsebuf != OR_TOKEN
              && *parsebuf != ')')
        {
            *p++ = *parsebuf++;
        }

        // Strip trailing whitespace.
        //
        *p-- = '\0';
        while (mux_isspace[(unsigned char)*p])
        {
            *p-- = '\0';
        }

        // Check for an attribute.
        //
        if ((b = test_atr(buf)) != NULL)
        {
            free_lbuf(buf);
            return (b);
        }
        b = alloc_bool("parse_boolexp_L");
        b->type = BOOLEXP_CONST;

        // do the match.
        //

        if (!mudstate.bStandAlone)
        {
            // If we are parsing a boolexp that was a stored lock then we
            // know that object refs are all dbrefs, so we skip the
            // expensive match code.
            //
            if (parsing_internal)
            {
                if (buf[0] != '#')
                {
                    free_lbuf(buf);
                    free_bool(b);
                    return TRUE_BOOLEXP;
                }
                b->thing = mux_atol(&buf[1]);
                if (!Good_obj(b->thing))
                {
                    free_lbuf(buf);
                    free_bool(b);
                    return TRUE_BOOLEXP;
                }
            }
            else
            {
                save_match_state(&mstate);
                init_match(parse_player, buf, TYPE_THING);
                match_everything(MAT_EXIT_PARENTS);
                b->thing = match_result();
                restore_match_state(&mstate);
            }
    
            if (b->thing == NOTHING)
            {
                notify(parse_player, tprintf("I don't see %s here.", buf));
                free_lbuf(buf);
                free_bool(b);
                return TRUE_BOOLEXP;
            }
            if (b->thing == AMBIGUOUS)
            {
                notify(parse_player, tprintf("I don't know which %s you mean!",
                    buf));
                free_lbuf(buf);
                free_bool(b);
                return TRUE_BOOLEXP;
            }
        }
        else
        {
            // Had better be #<num> or we're hosed.
            //
            if (buf[0] != '#')
            {
                free_lbuf(buf);
                free_bool(b);
                return TRUE_BOOLEXP;
            }
            b->thing = mux_atol(&buf[1]);
            if (b->thing < 0)
            {
                free_lbuf(buf);
                free_bool(b);
                return TRUE_BOOLEXP;
            }
        }
        free_lbuf(buf);
    }
    return b;
}

// F -> !F; F -> @L; F -> =L; F -> +L; F -> $L
// The argument L must be type BOOLEXP_CONST
//
static BOOLEXP *parse_boolexp_F(void)
{
    BOOLEXP *b2;

    skip_whitespace();
    switch (*parsebuf)
    {
    case NOT_TOKEN:

        parsebuf++;
        b2 = alloc_bool("parse_boolexp_F.not");
        b2->type = BOOLEXP_NOT;
        if ((b2->sub1 = parse_boolexp_F()) == TRUE_BOOLEXP)
        {
            free_boolexp(b2);
            return (TRUE_BOOLEXP);
        }
        else
        {
            return (b2);
        }

        // NOTREACHED
        //
        break;

    case INDIR_TOKEN:

        parsebuf++;
        b2 = alloc_bool("parse_boolexp_F.indir");
        b2->type = BOOLEXP_INDIR;
        b2->sub1 = parse_boolexp_L();
        if ((b2->sub1) == TRUE_BOOLEXP)
        {
            free_boolexp(b2);
            return (TRUE_BOOLEXP);
        }
        else if ((b2->sub1->type) != BOOLEXP_CONST)
        {
            free_boolexp(b2);
            return (TRUE_BOOLEXP);
        }
        else
        {
            return (b2);
        }

        // NOTREACHED
        //
        break;

    case IS_TOKEN:

        parsebuf++;
        b2 = alloc_bool("parse_boolexp_F.is");
        b2->type = BOOLEXP_IS;
        b2->sub1 = parse_boolexp_L();
        if (b2->sub1 == TRUE_BOOLEXP)
        {
            free_boolexp(b2);
            return (TRUE_BOOLEXP);
        }
        else if (  b2->sub1->type != BOOLEXP_CONST
                && b2->sub1->type != BOOLEXP_ATR)
        {
            free_boolexp(b2);
            return TRUE_BOOLEXP;
        }
        else
        {
            return (b2);
        }

        // NOTREACHED
        //
        break;

    case CARRY_TOKEN:

        parsebuf++;
        b2 = alloc_bool("parse_boolexp_F.carry");
        b2->type = BOOLEXP_CARRY;
        b2->sub1 = parse_boolexp_L();
        if (b2->sub1 == TRUE_BOOLEXP)
        {
            free_boolexp(b2);
            return TRUE_BOOLEXP;
        }
        else if (  b2->sub1->type != BOOLEXP_CONST
                && b2->sub1->type != BOOLEXP_ATR)
        {
            free_boolexp(b2);
            return TRUE_BOOLEXP;
        }
        else
        {
            return b2;
        }

        // NOTREACHED
        //
        break;

    case OWNER_TOKEN:

        parsebuf++;
        b2 = alloc_bool("parse_boolexp_F.owner");
        b2->type = BOOLEXP_OWNER;
        b2->sub1 = parse_boolexp_L();
        if (b2->sub1 == TRUE_BOOLEXP)
        {
            free_boolexp(b2);
            return TRUE_BOOLEXP;
        }
        else if (b2->sub1->type != BOOLEXP_CONST)
        {
            free_boolexp(b2);
            return TRUE_BOOLEXP;
        }
        else
        {
            return b2;
        }

        // NOTREACHED
        //
        break;

    default:
        return parse_boolexp_L();
    }
}

// T -> F; T -> F & T
//
static BOOLEXP *parse_boolexp_T(void)
{
    BOOLEXP *b, *b2;

    if ((b = parse_boolexp_F()) != TRUE_BOOLEXP)
    {
        skip_whitespace();
        if (*parsebuf == AND_TOKEN)
        {
            parsebuf++;

            b2 = alloc_bool("parse_boolexp_T");
            b2->type = BOOLEXP_AND;
            b2->sub1 = b;
            if ((b2->sub2 = parse_boolexp_T()) == TRUE_BOOLEXP)
            {
                free_boolexp(b2);
                return TRUE_BOOLEXP;
            }
            b = b2;
        }
    }
    return b;
}

// E -> T; E -> T | E
//
static BOOLEXP *parse_boolexp_E(void)
{
    BOOLEXP *b, *b2;

    if ((b = parse_boolexp_T()) != TRUE_BOOLEXP)
    {
        skip_whitespace();
        if (*parsebuf == OR_TOKEN)
        {
            parsebuf++;

            b2 = alloc_bool("parse_boolexp_E");
            b2->type = BOOLEXP_OR;
            b2->sub1 = b;
            if ((b2->sub2 = parse_boolexp_E()) == TRUE_BOOLEXP)
            {
                free_boolexp(b2);
                return TRUE_BOOLEXP;
            }
            b = b2;
        }
    }
    return b;
}

BOOLEXP *parse_boolexp(dbref player, const char *buf, bool internal)
{
    strcpy(parsestore, buf);
    parsebuf = parsestore;
    parse_player = player;
    if (  buf == NULL
       || *buf == '\0')
    {
        return TRUE_BOOLEXP;
    }
    if (!mudstate.bStandAlone)
    {
        parsing_internal = internal;
    }
    return parse_boolexp_E();
}
