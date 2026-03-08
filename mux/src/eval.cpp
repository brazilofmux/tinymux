/*! \file eval.cpp
 * \brief Expression and function evaluation.
 *
 * The functions here crack expressions into function calls and arguments,
 * perform %-substitutions, locate matching close-parens and so forth.
 * This is one of the three parsers in the server.  The other two parsers
 * are for commands (see command.cpp) and locks (boolexp.cpp).
 *
 * This file also contains routines to manage the global r-registers.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
using namespace std;

//-----------------------------------------------------------------------------
// parse_to: Split a line at a character, obeying nesting.  The line is
// destructively modified (a null is inserted where the delimiter was found)
// dstr is modified to point to the char after the delimiter, and the function
// return value points to the found string (space compressed if specified). If
// we ran off the end of the string without finding the delimiter, dstr is
// returned as nullptr.
//
static UTF8 *parse_to_cleanup( int eval, int first, UTF8 *cstr, UTF8 *rstr,
                               UTF8 *zstr, UTF8 *strFirewall)
{
    if (  (  mudconf.space_compress
          || (eval & EV_STRIP_TS))
       && !(eval & EV_NO_COMPRESS)
       && !first
       && strFirewall < cstr
       && cstr[-1] == ' ')
    {
        zstr--;
    }

    if (  (eval & EV_STRIP_AROUND)
       && *rstr == '{'
       && strFirewall < zstr
       && zstr[-1] == '}')
    {
        rstr++;
        if (  (  mudconf.space_compress
              && !(eval & EV_NO_COMPRESS))
           || (eval & EV_STRIP_LS))
        {
            while (mux_isspace(*rstr))
            {
                rstr++;
            }
        }
        rstr[-1] = '\0';
        zstr--;
        if (  (  mudconf.space_compress
              && !(eval & EV_NO_COMPRESS))
           || (eval & EV_STRIP_TS))
        {
            while (  strFirewall < zstr
                  && mux_isspace(zstr[-1]))
            {
                zstr--;
            }
        }
        *zstr = '\0';
    }
    *zstr = '\0';
    return rstr;
}

/*! \brief Accesses the isSpecial_ tables with proper typecast.
 *
 * \param table    indicates which table: \c L1, \c L2, \c L3, or \c L4.
 * \param c        character being looked up.
 * \return         lvalue of table entry.
 */
#define isSpecial(table, c) isSpecial_##table[static_cast<unsigned char>(c)]

// During parsing, this table may be modified for a particular terminating delimeter.
// The table is always restored it's original state.
//
// 0 means mundane character.
// 1 is 0x20 ' '  delim overridable (only done by parse_to)
// 2 is 0x5B '['  delim overridable
// 3 is 0x28 '('  delim overridable
// 4 is 0x25 '%' or 0x5C '\\' not overridable.
// 5 is 0x29 ')' or 0x5D ']'  not overridable.
// 6 is 0x7B '{' not overridable.
// 7 is 0x00 '\0' not overridable.
// 8 is the client-specific terminator.
//
// A code 4 or above means that the client-specified delim cannot override it.
// A code 8 is temporary.
//
static int isSpecial_L3[256] =
{
    7, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x10-0x1F
    0, 0, 0, 0, 0, 4, 0, 0,  3, 5, 0, 0, 0, 0, 0, 0, // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x30-0x3F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x40-0x4F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 2, 4, 5, 0, 0, // 0x50-0x5F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x60-0x6F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 6, 0, 0, 0, 0, // 0x70-0x7F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xD0-0xDF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0  // 0xF0-0xFF
};

static const char isSpecial_L4[256] =
{
    4, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x10-0x1F
    0, 0, 0, 0, 0, 1, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x30-0x3F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x40-0x4F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 1, 0, 0, 0, // 0x50-0x5F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x60-0x6F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 2, 0, 3, 0, 0, // 0x70-0x7F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xD0-0xDF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0  // 0xF0-0xFF
};

// Characters that are valid q-registers, and their offsets in the register
// array. -1 for invalid registers.
//
const signed char mux_RegisterSet[256] =
{
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0x00-0x0F
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0x10-0x1F
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0x20-0x2F
     0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1, // 0x30-0x3F
    -1,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, // 0x40-0x4F
    25,26,27,28,29,30,31,32, 33,34,35,-1,-1,-1,-1,-1, // 0x50-0x5F
    -1,10,11,12,13,14,15,16, 17,18,19,20,21,22,23,24, // 0x60-0x6F
    25,26,27,28,29,30,31,32, 33,34,35,-1,-1,-1,-1,-1, // 0x70-0x7F
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0x80-0x8F
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0x90-0x9F
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0xA0-0xAF
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0xB0-0xBF
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0xC0-0xCF
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0xD0-0xDF
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1, // 0xE0-0xEF
    -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1  // 0xF0-0xFF
};

// Perhaps some compilers don't handle aliased pointers well. For these
// compilers, we can't change this to just '*zstr++ = *cstr++'. However all
// up-to-date compilers that I know about handle this correctly.
//
#if 1
#define NEXTCHAR *zstr++ = *cstr++;
#else
#define NEXTCHAR \
    if (cstr == zstr) \
    { \
        cstr++; \
        zstr++; \
    } \
    else \
    { \
        *zstr++ = *cstr++; \
    }
#endif


UTF8 *parse_to(UTF8 **dstr, UTF8 delim, int eval)
{
#define stacklim 32
    UTF8 stack[stacklim];
    UTF8 *rstr, *cstr, *zstr, *strFirewall;
    int sp, tp, bracketlev;

    if (  dstr == nullptr
       || *dstr == nullptr)
    {
        return nullptr;
    }

    if (**dstr == '\0')
    {
        rstr = *dstr;
        *dstr = nullptr;
        return rstr;
    }
    sp = 0;
    bool first = true;
    strFirewall = rstr = *dstr;
    if (  (  mudconf.space_compress
          || (eval & EV_STRIP_LS))
       && !(eval & EV_NO_COMPRESS))
    {
        while (mux_isspace(*rstr))
        {
            rstr++;
        }
        *dstr = rstr;
    }
    zstr = cstr = rstr;
    int iOriginalCode = isSpecial(L3, delim);
    isSpecial(L3, ' ') = 1; // Spaces are special.
    if (iOriginalCode <= 3)
    {
        // We can override this code.
        //
        isSpecial(L3, delim) = 8;
    }

    for (;;)
    {
        int iCode = isSpecial(L3, *cstr);

TryAgain:
        if (iCode == 0)
        {
            // Mundane characters and not the delimiter we are looking for.
            //
            first = false;
            do
            {
                NEXTCHAR
                iCode = isSpecial(L3, *cstr);
            } while (iCode == 0);
        }

        if (iCode <= 4)
        {
            // 1 is 0x20 ' '  delim overridable
            // 2 is 0x5B '['  delim overridable
            // 3 is 0x28 '('  delim overridable
            // 4 is 0x25 '%' or 0x5C '\\' not overridable.
            //
            if (iCode <= 2)
            {
                // 1 is 0x20 ' '  delim overridable
                // 2 is 0x5B '['  delim overridable
                //
                if (iCode == 1)
                {
                    // space
                    //
                    if (  mudconf.space_compress
                       && !(eval & EV_NO_COMPRESS))
                    {
                        if (first)
                        {
                            rstr++;
                        }
                        else if (  strFirewall < cstr
                                && cstr[-1] == ' ')
                        {
                            zstr--;
                        }
                    }
                    NEXTCHAR
                }
                else
                {
                    // '['
                    //
                    first = false;
                    if (sp < stacklim)
                    {
                        stack[sp++] = ']';
                    }
                    NEXTCHAR
                }
            }
            else
            {
                // 3 is 0x28 '('  delim overridable
                // 4 is 0x25 '%' or 0x5C '\\' not overridable.
                //
                if (iCode == 3)
                {
                    first = false;
                    if (sp < stacklim)
                    {
                        stack[sp++] = ')';
                    }
                    NEXTCHAR
                }
                else
                {
                    // % and \ escapes.
                    //
                    first = false;
                    NEXTCHAR
                    if (*cstr)
                    {
                        NEXTCHAR
                    }
                }
            }
        }
        else
        {
            // 5 is 0x29 ')' or 0x5D ']'  not overridable.
            // 6 is 0x7B '{' not overridable.
            // 7 is 0x00 '\0' not overridable.
            // 8 is the client-specific terminator.
            //
            if (iCode <= 6)
            {
                // 5 is 0x29 ')' or 0x5D ']'  not overridable.
                // 6 is 0x7B '{' not overridable.
                //
                if (iCode == 5)
                {
                    // ) and ]
                    //
                    for (tp = sp - 1; tp >= 0 && stack[tp] != *cstr; tp--)
                    {
                        ; // Nothing.
                    }

                    // If we hit something on the stack, unwind to it. Otherwise (it's
                    // not on stack), if it's our delim we are done, and we convert the
                    // delim to a null and return a ptr to the char after the null. If
                    // it's not our delimiter, skip over it normally.
                    //
                    if (tp >= 0)
                    {
                        sp = tp;
                    }
                    else if (*cstr == delim)
                    {
                        rstr = parse_to_cleanup(eval, first, cstr, rstr, zstr, strFirewall);
                        *dstr = ++cstr;
                        isSpecial(L3, delim) = iOriginalCode;
                        isSpecial(L3, ' ') = 0; // Spaces no longer special
                        return rstr;
                    }
                    first = false;
                    NEXTCHAR
                }
                else
                {
                    // {
                    //
                    bracketlev = 1;
                    if (eval & EV_STRIP_CURLY)
                    {
                        cstr++;
                    }
                    else
                    {
                        NEXTCHAR;
                    }
                    for (;;)
                    {
                        int iCodeL4 = isSpecial(L4, *cstr);
                        if (iCodeL4 == 0)
                        {
                            // Mudane Characters
                            //
                            do
                            {
                                NEXTCHAR
                                iCodeL4 = isSpecial(L4, *cstr);
                            } while (iCodeL4 == 0);
                        }


                        if (iCodeL4 == 1)
                        {
                            // % and \ escapes.
                            //
                            if (cstr[1])
                            {
                                NEXTCHAR
                            }
                        }
                        else if (iCodeL4 == 2)
                        {
                            // '{'
                            //
                            bracketlev++;
                        }
                        else if (iCodeL4 == 3)
                        {
                            // '}'
                            //
                            bracketlev--;
                            if (bracketlev <= 0)
                            {
                                break;
                            }
                        }
                        else
                        {
                            // '\0'
                            //
                            break;
                        }
                        NEXTCHAR
                    }

                    if (bracketlev == 0)
                    {
                        if (eval & EV_STRIP_CURLY)
                        {
                            cstr++;
                        }
                        else
                        {
                            NEXTCHAR
                        }
                    }
                    first = false;
                }
            }
            else
            {
                // 7 is 0x00 '\0' not overridable.
                // 8 is the client-specific terminator.
                //
                if (iCode == 7)
                {
                    // '\0' - End of string.
                    //
                    isSpecial(L3, delim) = iOriginalCode;
                    isSpecial(L3, ' ') = 0; // Spaces no longer special
                    break;
                }
                else
                {
                    // Client-Specific terminator
                    //
                    if (sp == 0)
                    {
                        rstr = parse_to_cleanup(eval, first, cstr, rstr, zstr, strFirewall);
                        *dstr = ++cstr;
                        isSpecial(L3, delim) = iOriginalCode;
                        isSpecial(L3, ' ') = 0; // Spaces no longer special
                        return rstr;
                    }

                    // At this point, we need to process the iOriginalCode.
                    //
                    iCode = iOriginalCode;
                    goto TryAgain;
                }
            }
        }
    }
    rstr = parse_to_cleanup(eval, first, cstr, rstr, zstr, strFirewall);
    *dstr = nullptr;
    return rstr;
}

//-----------------------------------------------------------------------------
// parse_arglist: Parse a line into an argument list contained in lbufs. A
// pointer is returned to whatever follows the final delimiter. If the arglist
// is unterminated, a nullptr is returned.  The original arglist is destructively
// modified.
//
void parse_arglist( dbref executor, dbref caller, dbref enactor, UTF8 *dstr,
                     int eval, UTF8 *fargs[], int nfargs,
                     const UTF8 *cargs[], int ncargs, int *nArgsParsed )
{
    if (dstr == nullptr)
    {
        *nArgsParsed = 0;
        return;
    }

    int iArg = 0;
    UTF8 *tstr, *bp;
    int peval = (eval & ~EV_EVAL);

    while (  iArg < nfargs
          && dstr)
    {
        if (iArg < nfargs - 1)
        {
            tstr = parse_to(&dstr, ',', peval);
        }
        else
        {
            tstr = parse_to(&dstr, '\0', peval);
        }

        bp = fargs[iArg] = alloc_lbuf("parse_arglist");
        if (eval & EV_EVAL)
        {
            mux_exec(tstr, LBUF_SIZE-1, fargs[iArg], &bp, executor, caller, enactor,
                     eval | EV_FCHECK, cargs, ncargs);
            *bp = '\0';
        }
        else
        {
            mux_strncpy(fargs[iArg], tstr, LBUF_SIZE-1);
        }
        iArg++;
    }
    *nArgsParsed = iArg;
}

//-----------------------------------------------------------------------------
// Pronoun support for %-substitutions.
//
static PRONOUN_SET builtin_pronoun_sets[4] =
{
    { "it",   "it",   "its",   "its",    false },  // [0] neuter
    { "she",  "her",  "her",   "hers",   false },  // [1] feminine
    { "he",   "him",  "his",   "his",    false },  // [2] masculine
    { "they", "them", "their", "theirs", true  },   // [3] plural
};

const PRONOUN_SET *get_pronoun_set(dbref player)
{
    // Fast path: check for @pronoun attribute (no parent chain).
    //
    const UTF8 *raw = atr_get_raw(player, A_PRONOUN);
    if (raw)
    {
        size_t nCased;
        const UTF8 *pCased = mux_strupr(raw, nCased);
        std::vector<UTF8> vKey(pCased, pCased + nCased);
        auto it = mudstate.pronoun_groups.find(vKey);
        if (it != mudstate.pronoun_groups.end())
        {
            return it->second;
        }
    }

    // Fall back to @sex first-character matching.
    //
    dbref aowner;
    int aflags;
    UTF8 *atr_gotten = atr_pget(player, A_SEX, &aowner, &aflags);
    UTF8 first = atr_gotten[0];
    free_lbuf(atr_gotten);
    switch (first)
    {
    case 'p':
    case 'P':
        return &builtin_pronoun_sets[3];

    case 'm':
    case 'M':
        return &builtin_pronoun_sets[2];

    case 'f':
    case 'F':
    case 'w':
    case 'W':
        return &builtin_pronoun_sets[1];
    }
    return &builtin_pronoun_sets[0];
}

int get_gender(dbref player)
{
    const PRONOUN_SET *ps = get_pronoun_set(player);
    if (ps == &builtin_pronoun_sets[3]) return 4;
    if (ps == &builtin_pronoun_sets[2]) return 3;
    if (ps == &builtin_pronoun_sets[1]) return 2;
    return 1;
}

//---------------------------------------------------------------------------
// Trace cache routines.
//
typedef struct tcache_ent TCENT;
static struct tcache_ent
{
    dbref player;
    UTF8 *orig;
    UTF8 *result;
    struct tcache_ent *next;
} *tcache_head;

static bool tcache_top;
static int  tcache_count;

void tcache_init(void)
{
    tcache_head = nullptr;
    tcache_top = true;
    tcache_count = 0;
}

static bool tcache_empty(void)
{
    if (tcache_top)
    {
        tcache_top = false;
        tcache_count = 0;
        return true;
    }
    return false;
}

static void tcache_add(dbref player, UTF8 *orig, const UTF8 *result)
{
    if (  strcmp(reinterpret_cast<const char *>(orig), reinterpret_cast<const char *>(result))
       && (++tcache_count) <= mudconf.trace_limit)
    {
        TCENT *xp = reinterpret_cast<TCENT *>(alloc_sbuf("tcache_add.sbuf"));
        UTF8 *tp = alloc_lbuf("tcache_add.lbuf");

        TruncateToBuffer(result, tp, LBUF_SIZE-1);
        xp->result = tp;

        xp->player = player;
        xp->orig = orig;
        xp->next = tcache_head;
        tcache_head = xp;
    }
    else
    {
        free_lbuf(orig);
    }
}

static void tcache_finish(void)
{
    while (tcache_head != nullptr)
    {
        TCENT *xp = tcache_head;
        tcache_head = xp->next;
        notify(Owner(xp->player), tprintf(T("%s(#%d)} \xE2\x80\x98%s\xE2\x80\x99 -> \xE2\x80\x98%s\xE2\x80\x99"), Name(xp->player),
            xp->player, xp->orig, xp->result));
        free_lbuf(xp->orig);
        free_lbuf(xp->result);
        free_sbuf(xp);
    }
    tcache_top = true;
    tcache_count = 0;
}

const unsigned int ColorTable[256] =
{
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x10-0x1F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x30-0x3F

    // 0x40-0x47
    0,
    0,
    COLOR_INDEX_BG + COLOR_INDEX_BLUE,
    COLOR_INDEX_BG + COLOR_INDEX_CYAN,
    0,
    0,
    0,
    COLOR_INDEX_BG + COLOR_INDEX_GREEN,

    // 0x48-0x4F
    0,
    0,
    0,
    0,
    0,
    COLOR_INDEX_BG + COLOR_INDEX_MAGENTA,
    0,
    0,

    // 0x50-0x57
    //
    0,
    0,
    COLOR_INDEX_BG + COLOR_INDEX_RED,
    0,
    0,
    0,
    0,
    COLOR_INDEX_BG + COLOR_INDEX_WHITE,

    // 0x58-0x5F
    //
    COLOR_INDEX_BG + COLOR_INDEX_BLACK,
    COLOR_INDEX_BG + COLOR_INDEX_YELLOW,
    0,
    0,
    0,
    0,
    0,
    0,

    // 0x60-0x67
    0,
    0,
    COLOR_INDEX_FG + COLOR_INDEX_BLUE,
    COLOR_INDEX_FG + COLOR_INDEX_CYAN,
    0,
    0,
    COLOR_INDEX_BLINK,
    COLOR_INDEX_FG + COLOR_INDEX_GREEN,

    // 0x68-0x6F
    //
    COLOR_INDEX_INTENSE,
    COLOR_INDEX_INVERSE,
    0,
    0,
    0,
    COLOR_INDEX_FG + COLOR_INDEX_MAGENTA,
    COLOR_INDEX_RESET,
    0,

    // 0x70-0x77
    //
    0,
    0,
    COLOR_INDEX_FG + COLOR_INDEX_RED,
    0,
    0,
    COLOR_INDEX_UNDERLINE,
    0,
    COLOR_INDEX_FG + COLOR_INDEX_WHITE,

    // 0x78-0x7F
    //
    COLOR_INDEX_FG + COLOR_INDEX_BLACK,
    COLOR_INDEX_FG + COLOR_INDEX_YELLOW,
    0,
    0,
    0,
    0,
    0,
    0,

    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xD0-0xDF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0       // 0xF0-0xFF
};

#define REFS_PER_FRAME ((LBUF_SIZE - sizeof(void *) - sizeof(int))/sizeof(void *))
typedef struct tag_refsframe
{
    int      nrefs;
    reg_ref *refs[REFS_PER_FRAME];
    struct tag_refsframe *next;
} RefsFrame;

static RefsFrame *pRefsFrame = nullptr;

reg_ref **PushRegisters(int nNeeded)
{
    if (  !pRefsFrame
       || pRefsFrame->nrefs < nNeeded)
    {
        RefsFrame *p = reinterpret_cast<RefsFrame *>(alloc_lbuf("PushRegisters"));
        p->next = pRefsFrame;
        p->nrefs = REFS_PER_FRAME;
        pRefsFrame = p;
    }
    pRefsFrame->nrefs -= nNeeded;
    return pRefsFrame->refs + pRefsFrame->nrefs;
}

void PopRegisters(reg_ref **p, int nNeeded)
{
    UNUSED_PARAMETER(p);

    if (pRefsFrame->nrefs == REFS_PER_FRAME)
    {
        RefsFrame *q = pRefsFrame->next;
        free_lbuf(reinterpret_cast<UTF8 *>(pRefsFrame));
        pRefsFrame = q;
    }
    //mux_assert(p == pRefsFrame->refs + pRefsFrame->nrefs);
    pRefsFrame->nrefs += nNeeded;
}

bool parse_rgb(size_t n, const UTF8 *p, RGB &rgb)
{
    UTF8 ch;
    if (  7 == n
       && '#' == p[0])
    {
        // Look for RRGGBB in hexidecimal.
        //
        for (int i = 1; i < 7; i++)
        {
            ch = p[i];
            if (  !('0' <= ch && ch <= '9')
               && !('a' <= ch && ch <= 'f')
               && !('A' <= ch && ch <= 'F'))
            {
                return false;
            }
        }

        rgb.r = (mux_hex2dec(p[1]) << 4) | mux_hex2dec(p[2]);
        rgb.g = (mux_hex2dec(p[3]) << 4) | mux_hex2dec(p[4]);
        rgb.b = (mux_hex2dec(p[5]) << 4) | mux_hex2dec(p[6]);
        return true;
    }

    int nSpaces = 0;
    int nDigits = 0;
    for (size_t i = 0; i < n; i++)
    {
        ch = p[i];
        if (mux_isspace(ch))
        {
            if (  3 < nDigits
               || 0 == nDigits
               || 1 < nSpaces)
            {
                return false;
            }
            if (0 == nSpaces)
            {
                rgb.r = mux_atol(p+i-nDigits);
                if (rgb.r < 0 || 255 < rgb.r)
                {
                    return false;
                }
            }
            else
            {
                rgb.g = mux_atol(p+i-nDigits);
                if (rgb.g < 0 || 255 < rgb.g)
                {
                    return false;
                }
            }
            nDigits = 0;
            nSpaces++;
        }
        else if (!mux_isdigit(ch))
        {
            return false;
        }
        else
        {
            nDigits++;
        }
    }
    if (  3 < nDigits
       || 0 == nDigits
       || 2 != nSpaces)
    {
        return false;
    }
    rgb.b = mux_atol(p+n-nDigits-1);
    if (rgb.b < 0 || 255 < rgb.b)
    {
        return false;
    }
    return true;
}


/* ---------------------------------------------------------------------------
 * save_global_regs, restore_global_regs:  Save and restore the global
 * registers to protect them from various sorts of munging.
 */

static std::vector<NamedRegsMap*> named_regs_stack;

void save_global_regs
(
    reg_ref *preserve[]
)
{
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (mudstate.global_regs[i])
        {
            RegAddRef(mudstate.global_regs[i]);
        }
        preserve[i] = mudstate.global_regs[i];
    }
    named_regs_stack.push_back(NamedRegsCopy(mudstate.named_regs));
}

void save_and_clear_global_regs
(
    reg_ref *preserve[]
)
{
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        preserve[i] = mudstate.global_regs[i];
        mudstate.global_regs[i] = nullptr;
    }
    named_regs_stack.push_back(mudstate.named_regs);
    mudstate.named_regs = nullptr;
}

void restore_global_regs
(
    reg_ref *preserve[]
)
{
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (mudstate.global_regs[i])
        {
            RegRelease(mudstate.global_regs[i]);
            mudstate.global_regs[i] = nullptr;
        }

        if (preserve[i])
        {
            mudstate.global_regs[i] = preserve[i];
            preserve[i] = nullptr;
        }
    }
    NamedRegsClear(mudstate.named_regs);
    if (!named_regs_stack.empty())
    {
        mudstate.named_regs = named_regs_stack.back();
        named_regs_stack.pop_back();
    }
}

static lbuf_ref *last_lbufref = nullptr;
static size_t    last_left    = 0;
static UTF8     *last_ptr     = nullptr;

void RegAssign(reg_ref **regref, size_t nLength, const UTF8 *ptr)
{
    if (  nullptr == regref
       || nullptr == ptr
       || LBUF_SIZE <= nLength)
    {
        return;
    }

    // Put any previous register value out of the way.
    //
    if (nullptr != *regref)
    {
        RegRelease(*regref);
        *regref = nullptr;
    }

    // Let go of the last lbuf if we can't use it.
    //
    size_t nSize = nLength + 1;
    if (  nullptr != last_lbufref
       && last_left < nSize)
    {
        BufRelease(last_lbufref);
        last_lbufref = nullptr;
        last_left    = 0;
        last_ptr     = nullptr;
    }

    // Grab a new, fresh lbuf if we don't have one.
    //
    if (nullptr == last_lbufref)
    {
        last_ptr = alloc_lbuf("RegAssign");
        last_left = LBUF_SIZE;

        // Fill in new lbufref.
        //
        last_lbufref = alloc_lbufref("RegAssign");
        last_lbufref->refcount = 1;
        last_lbufref->lbuf_ptr = last_ptr;
    }

    // Use last lbuf.
    //
    UTF8 *p = last_ptr;
    memcpy(last_ptr, ptr, nSize);
    last_ptr[nLength] = '\0';
    last_ptr  += nSize;
    last_left -= nSize;

    // Fill in new regref.
    //
    *regref = alloc_regref("RegAssign");
    (*regref)->refcount = 1;
    (*regref)->lbuf     = last_lbufref;
    (*regref)->reg_len  = nLength;
    (*regref)->reg_ptr  = p;

    BufAddRef(last_lbufref);
}

// ---------------------------------------------------------------------------
// Named global register helpers.
//

constexpr size_t MAX_NAMED_REG_LEN = 32;

static std::vector<UTF8> MakeRegKey(const UTF8 *name, size_t len)
{
    std::vector<UTF8> key(len);
    for (size_t i = 0; i < len; i++)
    {
        key[i] = mux_tolower_ascii(name[i]);
    }
    return key;
}

bool IsSingleCharReg(const UTF8 *name, int &regnum)
{
    if (  nullptr != name
       && name[0] != '\0'
       && name[1] == '\0')
    {
        int r = mux_RegisterSet[static_cast<unsigned char>(name[0])];
        if (  0 <= r
           && r < MAX_GLOBAL_REGS)
        {
            regnum = r;
            return true;
        }
    }
    return false;
}

bool IsValidNamedReg(const UTF8 *name, size_t len)
{
    if (  nullptr == name
       || 0 == len
       || MAX_NAMED_REG_LEN < len)
    {
        return false;
    }
    for (size_t i = 0; i < len; i++)
    {
        UTF8 ch = name[i];
        if (  !mux_isalnum(ch)
           && ch != '_')
        {
            return false;
        }
    }
    return true;
}

void NamedRegAssign(NamedRegsMap *&map, const UTF8 *name, size_t nNameLen, size_t nValueLen, const UTF8 *value)
{
    if (  nullptr == name
       || 0 == nNameLen
       || !IsValidNamedReg(name, nNameLen))
    {
        return;
    }

    if (nullptr == map)
    {
        map = new NamedRegsMap;
    }

    std::vector<UTF8> key = MakeRegKey(name, nNameLen);
    auto it = map->find(key);
    if (it != map->end())
    {
        RegRelease(it->second);
        it->second = nullptr;
        map->erase(it);
    }

    if (  nullptr != value
       && nValueLen < LBUF_SIZE)
    {
        reg_ref *rr = nullptr;
        RegAssign(&rr, nValueLen, value);
        (*map)[key] = rr;
    }
}

reg_ref *NamedRegRead(const NamedRegsMap *map, const UTF8 *name, size_t nNameLen)
{
    if (  nullptr == map
       || nullptr == name
       || 0 == nNameLen)
    {
        return nullptr;
    }

    std::vector<UTF8> key = MakeRegKey(name, nNameLen);
    auto it = map->find(key);
    if (it != map->end())
    {
        return it->second;
    }
    return nullptr;
}

void NamedRegsClear(NamedRegsMap *&map)
{
    if (nullptr == map)
    {
        return;
    }
    for (auto &kv : *map)
    {
        RegRelease(kv.second);
    }
    delete map;
    map = nullptr;
}

NamedRegsMap *NamedRegsCopy(const NamedRegsMap *src)
{
    if (nullptr == src || src->empty())
    {
        return nullptr;
    }
    NamedRegsMap *dst = new NamedRegsMap;
    for (const auto &kv : *src)
    {
        RegAddRef(kv.second);
        (*dst)[kv.first] = kv.second;
    }
    return dst;
}

