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

#include "attrs.h"
#include "functions.h"
#include "mathutil.h"

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
#define isSpecial(table, c) isSpecial_##table[(unsigned char)(c)]

// During parsing, this table may be modified for a particular terminating delimeter.
// The table is always restored it's original state.
//
// 0 means mundane character.
// 1 is 0x20 ' '  delim overridable (only done by parse_to, not parse_to_lite)
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

// This version of parse_to is non-destructive. It doesn't process escapes.
// It's useful with mux_exec which will be copying the characters to another
// buffer anyway and is more than able to perform the escapes and trimming.
//
static const UTF8 *parse_to_lite(const UTF8 *dstr, UTF8 delim1, UTF8 delim2, size_t *nLen, int *iWhichDelim)
{
    if (  nullptr == dstr
       || '\0' == dstr[0])
    {
        *nLen = 0;
        return nullptr;
    }

#define stacklim 32
    UTF8 stack[stacklim];
    int sp, tp, bracketlev;

    sp = 0;
    const UTF8 *rstr = dstr;
    const UTF8 *cstr = dstr;
    int iOriginalCode1 = isSpecial(L3, delim1);
    int iOriginalCode2 = isSpecial(L3, delim2);
    if (iOriginalCode1 <= 3)
    {
        // We can override this code.
        //
        isSpecial(L3, delim1) = 8;
    }
    if (iOriginalCode2 <= 3)
    {
        // We can override this code.
        //
        isSpecial(L3, delim2) = 8;
    }

    for (;;)
    {
        int iCode = isSpecial(L3, *cstr);

TryAgain:
        if (iCode == 0)
        {
            // Mundane characters and not the delimiter we are looking for.
            //
            do
            {
                cstr++;
                iCode = isSpecial(L3, *cstr);
            } while (iCode == 0);
        }

        if (iCode <= 4)
        {
            // 2 is 0x5B '['  delim overridable
            // 3 is 0x28 '('  delim overridable
            // 4 is 0x25 '%' or 0x5C '\\' not overridable.
            //
            if (iCode <= 3)
            {
                // 2 is 0x5B '['  delim overridable
                // 3 is 0x28 '('  delim overridable
                //
                if (sp < stacklim)
                {
                    static UTF8 matcher[2] = { ']', ')'};
                    stack[sp++] = matcher[iCode-2];
                }
                cstr++;
            }
            else
            {
                // 4 is 0x25 '%' or 0x5C '\\' not overridable.
                //
                cstr++;
                if (*cstr)
                {
                    cstr++;
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
                    if (0 <= tp)
                    {
                        sp = tp;
                    }
                    else if (  *cstr == delim1
                            || *cstr == delim2)
                    {
                        if (*cstr == delim1)
                        {
                            *iWhichDelim = 1;
                        }
                        else
                        {
                            *iWhichDelim = 2;
                        }
                        *nLen = (cstr - dstr);
                        rstr = ++cstr;
                        isSpecial(L3, delim1) = iOriginalCode1;
                        isSpecial(L3, delim2) = iOriginalCode2;
                        return rstr;
                    }
                    cstr++;
                }
                else
                {
                    // {
                    //
                    bracketlev = 1;
                    cstr++;
                    for (;;)
                    {
                        int iCodeL4 = isSpecial(L4, *cstr);
                        if (iCodeL4 == 0)
                        {
                            // Mudane Characters
                            //
                            do
                            {
                                cstr++;
                                iCodeL4 = isSpecial(L4, *cstr);
                            } while (iCodeL4 == 0);
                        }


                        if (iCodeL4 == 1)
                        {
                            // '\\' or '%'
                            //
                            if (cstr[1])
                            {
                                cstr++;
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
                        cstr++;
                    }

                    if (bracketlev == 0)
                    {
                        cstr++;
                    }
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
                    isSpecial(L3, delim1) = iOriginalCode1;
                    isSpecial(L3, delim2) = iOriginalCode2;
                    break;
                }
                else
                {
                    // Client-Specific terminator
                    //
                    if (sp == 0)
                    {
                        if (*cstr == delim1)
                        {
                            *iWhichDelim = 1;
                        }
                        else
                        {
                            *iWhichDelim = 2;
                        }
                        *nLen = (cstr - dstr);
                        rstr = ++cstr;
                        isSpecial(L3, delim1) = iOriginalCode1;
                        isSpecial(L3, delim2) = iOriginalCode2;
                        return rstr;
                    }

                    // At this point, we need to process the iOriginalCode.
                    //
                    if (*cstr == delim1)
                    {
                        iCode = iOriginalCode1;
                    }
                    else
                    {
                        iCode = iOriginalCode2;
                    }
                    goto TryAgain;
                }
            }
        }
    }
    *iWhichDelim = 0;
    *nLen = (cstr - dstr);
    rstr = nullptr;
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

static const UTF8 *parse_arglist_lite( dbref executor, dbref caller, dbref enactor,
                          const UTF8 *dstr, int eval, UTF8 *fargs[],
                          int nfargs, const UTF8 *cargs[], int ncargs,
                          int *nArgsParsed)
{
    if (nullptr == dstr)
    {
        *nArgsParsed = 0;
        return nullptr;
    }

    int peval = eval;
    if (eval & EV_EVAL)
    {
        peval = eval | EV_FCHECK;
    }
    else
    {
        peval = ((eval & ~EV_FCHECK)|EV_NOFCHECK);
    }

    const UTF8 *pCurr = dstr;
    const UTF8 *pNext = dstr;
    UTF8 *bp;
    size_t nLen;
    int  arg = 0;
    int  iWhichDelim = 0;

    while (  arg < nfargs
          && pNext
          && iWhichDelim != 2)
    {
        pCurr = pNext;
        if (arg < nfargs - 1)
        {
            pNext = parse_to_lite(pCurr, ',', ')', &nLen, &iWhichDelim);
        }
        else
        {
            pNext = parse_to_lite(pCurr, '\0', ')', &nLen, &iWhichDelim);
        }

        // The following recognizes and returns zero arguments. We avoid
        // allocating an lbuf.
        //
        if (  2 == iWhichDelim
           && 0 == arg
           && 0 == nLen)
        {
            break;
        }

        if (0 < nLen)
        {
            bp = fargs[arg] = alloc_lbuf("parse_arglist");
            mux_exec(pCurr, nLen, fargs[arg], &bp, executor, caller, enactor, peval,
                     cargs, ncargs);
        }
        else
        {
            bp = fargs[arg] = alloc_lbuf("parse_arglist");
        }
        *bp = '\0';
        arg++;
    }
    *nArgsParsed = arg;
    return pNext;
}

//-----------------------------------------------------------------------------
// exec: Process a command line, evaluating function calls and %-substitutions.
//
int get_gender(dbref player)
{
    dbref aowner;
    int aflags;
    UTF8 *atr_gotten = atr_pget(player, A_SEX, &aowner, &aflags);
    UTF8 first = atr_gotten[0];
    free_lbuf(atr_gotten);
    switch (first)
    {
    case 'p':
    case 'P':
        return 4;

    case 'm':
    case 'M':
        return 3;

    case 'f':
    case 'F':
    case 'w':
    case 'W':
        return 2;
    }
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
    if (  strcmp((const char *)orig, (const char *)result)
       && (++tcache_count) <= mudconf.trace_limit)
    {
        TCENT *xp = (TCENT *) alloc_sbuf("tcache_add.sbuf");
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

static bool isSpecial_L1[256] =
{
    1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x10-0x1F
    1, 0, 0, 0, 0, 1, 0, 0,  1, 0, 0, 0, 0, 0, 0, 0, // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x30-0x3F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x40-0x4F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 1, 0, 0, 0, // 0x50-0x5F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x60-0x6F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0, // 0x70-0x7F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xD0-0xDF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0  // 0xF0-0xFF
};

static const unsigned char isSpecial_L2[256] =
{
     18,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0x00-0x0F
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0x10-0x1F
      0,  4,  0,  3,  0, 11,  0,  0,   0,  0,  0, 22,  0,  0,  0,  0, // 0x20-0x2F
      1,  1,  1,  1,  1,  1,  1,  1,   1,  1,  0,  0,  0, 21,  0,  0, // 0x30-0x3F
     20,145,  7, 70,  0,  0,  0,  0,   0, 24,  0, 23,  9,147,140,144, // 0x40-0x4F
    143,130,  5,142,  8,  0,138,  0,  70,  0,  0,  0,  0,  0,  0,  0, // 0x50-0x5F
      0, 17,  7,  6,  0,  0,  0,  0,   0, 24,  0, 23,  9, 19, 12, 16, // 0x60-0x6F
     15,  2,  5, 14,  8,  0, 10,  0,   6,  0,  0,  0, 13,  0,  0,  0, // 0x70-0x7F
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0x80-0x8F
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0x90-0x9F
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0xA0-0xAF
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0xB0-0xBF
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0xC0-0xCF
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0xD0-0xDF
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0xE0-0xEF
      0,  0,  0,  0,  0,  0,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0  // 0xF0-0xFF
};

#define PTRS_PER_FRAME ((LBUF_SIZE - sizeof(UTF8 *) - sizeof(int))/sizeof(UTF8 *))
typedef struct tag_ptrsframe
{
    int   nptrs;
    UTF8 *ptrs[PTRS_PER_FRAME];
    struct tag_ptrsframe *next;
} PtrsFrame;

static PtrsFrame *pPtrsFrame = nullptr;

UTF8 **PushPointers(int nNeeded)
{
    if (  !pPtrsFrame
       || pPtrsFrame->nptrs < nNeeded)
    {
        PtrsFrame *p = (PtrsFrame *)alloc_lbuf("PushPointers");
        p->next = pPtrsFrame;
        p->nptrs = PTRS_PER_FRAME;
        pPtrsFrame = p;
    }
    pPtrsFrame->nptrs -= nNeeded;
    return pPtrsFrame->ptrs + pPtrsFrame->nptrs;
}

void PopPointers(UTF8 **p, int nNeeded)
{
    UNUSED_PARAMETER(p);

    if (pPtrsFrame->nptrs == PTRS_PER_FRAME)
    {
        PtrsFrame *q = pPtrsFrame->next;
        free_lbuf((UTF8 *)pPtrsFrame);
        pPtrsFrame = q;
    }
    //mux_assert(p == pPtrsFrame->ptrs + pPtrsFrame->nptrs);
    pPtrsFrame->nptrs += nNeeded;
}

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
        RefsFrame *p = (RefsFrame *)alloc_lbuf("PushRegisters");
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
        free_lbuf((UTF8 *)pRefsFrame);
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

void mux_exec( const UTF8 *pStr, size_t nStr, UTF8 *buff, UTF8 **bufc, dbref executor,
               dbref caller, dbref enactor, int eval, const UTF8 *cargs[], int ncargs)
{
    if (  nullptr == pStr
       || '\0' == pStr[0]
       || alarm_clock.alarmed)
    {
        return;
    }

    // Stack Limit checking with thanks to RhostMUSH.
    //
    if (mudconf.nStackLimit < mudstate.nStackNest)
    {
        mudstate.bStackLimitReached = true;
        return;
    }

    UTF8 *TempPtr, *tbuf;
    UTF8 *start, *oldp, *savestr;
    const UTF8 *constbuf;
    UTF8 ch;
    UTF8 *realbuff = nullptr, *realbp = nullptr;
    dbref aowner;
    int nfargs, aflags, feval, i;
    size_t n;
    bool ansi = false;
    FUN *fp;
    UFUN *ufp;
    const UTF8 *tstr;

    static const UTF8 *subj[5] =
    {
        T(""),
        T("it"),
        T("she"),
        T("he"),
        T("they")
    };
    static const UTF8 *poss[5] =
    {
        T(""),
        T("its"),
        T("her"),
        T("his"),
        T("their")
    };
    static const UTF8 *obj[5] =
    {
        T(""),
        T("it"),
        T("her"),
        T("him"),
        T("them")
    };
    static const UTF8 *absp[5] =
    {
        T(""),
        T("its"),
        T("hers"),
        T("his"),
        T("theirs")
    };

    // This is scratch buffer is used potentially on every invocation of
    // mux_exec. Do not assume that its contents are valid after you
    // execute any function that could re-enter mux_exec.
    //
    static UTF8 mux_scratch[LBUF_SIZE];

    int at_space = 1;
    int gender = -1;

    bool is_trace = (Trace(executor) || (eval & EV_TRACE)) && !(eval & EV_NOTRACE);
    bool is_top = false;

    // Extend the buffer if we need to.
    //
    if (LBUF_SIZE - SBUF_SIZE < (*bufc) - buff)
    {
        realbuff = buff;
        realbp = *bufc;
        buff = (UTF8 *)MEMALLOC(LBUF_SIZE);
        ISOUTOFMEMORY(buff);
        *bufc = buff;
    }

    oldp = start = *bufc;

    // If we are tracing, save a copy of the starting buffer.
    //
    savestr = nullptr;
    if (is_trace)
    {
        is_top = tcache_empty();
        savestr = alloc_lbuf("exec.save");
        mux_strncpy(savestr, pStr, nStr);
    }

    // Save Parser Mode.
    //
    bool bSpaceIsSpecialSave = isSpecial(L1, ' ');
    bool bParenthesisIsSpecialSave = isSpecial(L1, '(');
    bool bBracketIsSpecialSave = isSpecial(L1, '[');

    // Setup New Parser Mode.
    //
    bool bSpaceIsSpecial = mudconf.space_compress && !(eval & EV_NO_COMPRESS);
    isSpecial(L1, ' ') = bSpaceIsSpecial;
    isSpecial(L1, '(') = (eval & EV_FCHECK) != 0;
    isSpecial(L1, '[') = (eval & EV_NOFCHECK) == 0;

    size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    size_t iStr = 0;

    while (iStr < nStr)
    {
        // Handle mundane characters specially. There are usually a lot of them.
        // Just copy them.
        //
        if (!isSpecial(L1, pStr[iStr]))
        {
            size_t iNormal = iStr + 1;
            while (  iNormal < nStr
                  && !isSpecial(L1, pStr[iNormal]))
            {
                iNormal++;
            }

            n = iNormal - iStr;
            if (nBufferAvailable < n)
            {
                n = nBufferAvailable;
            }

            memcpy(*bufc, pStr + iStr, n);
            nBufferAvailable -= n;
            *bufc += n;
            at_space = 0;
            iStr += n;

            if (nStr <= iStr)
            {
                break;
            }
        }

        // At this point, pStr[iStr] must be one of the following characters:
        //
        // 0x00 0x20 0x25 0x28 0x5B 0x5C 0x7B
        // NUL   SP   %    (     [    \   {
        //
        // Test softcode shows the following distribution:
        //
        // NUL  occurs 116948 times
        //   (  occurs  49567 times
        //   %  occurs  24553 times
        //   [  occurs   7618 times
        //  SP  occurs   1323 times
        //
        if (pStr[iStr] == '\0')
        {
            break;
        }
        else if (pStr[iStr] == '(')
        {
            // pStr[iStr] == '('
            //
            // Arglist start.  See if what precedes is a function. If so,
            // execute it if we should.
            //
            at_space = 0;

            // Load an sbuf with an lowercase version of the func name, and
            // see if the func exists. Trim trailing spaces from the name if
            // configured.
            //
            UTF8 *pEnd = *bufc - 1;
            if (mudconf.space_compress && (eval & EV_FMAND))
            {
                while (  oldp <= pEnd
                      && mux_isspace(*pEnd))
                {
                    pEnd--;
                }
            }

            fp = nullptr;
            ufp = nullptr;

            size_t nFun = 0;
            if (oldp <= pEnd)
            {
                nFun = pEnd - oldp + 1;
                if (LBUF_SIZE <= nFun)
                {
                    nFun = LBUF_SIZE - 1;
                }

                // _strlwr();
                //
                for (size_t iFun = 0; iFun < nFun; iFun++)
                {
                    mux_scratch[iFun] = mux_toupper_ascii(oldp[iFun]);
                }
            }
            mux_scratch[nFun] = '\0';

            if (  0 < nFun
               && nFun <= MAX_UFUN_NAME_LEN)
            {
                fp = (FUN *)hashfindLEN(mux_scratch, nFun, &mudstate.func_htab);

                // If not a builtin func, check for global func.
                //
                if (nullptr == fp)
                {
                    ufp = (UFUN *)hashfindLEN(mux_scratch, nFun, &mudstate.ufunc_htab);
                }
            }

            // Do the right thing if it doesn't exist.
            //
            if (!fp && !ufp)
            {
                if (eval & EV_FMAND)
                {
                    *bufc = oldp;
                    safe_str(T("#-1 FUNCTION ("), buff, bufc);
                    safe_str(mux_scratch, buff, bufc);
                    safe_str(T(") NOT FOUND"), buff, bufc);
                    nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    break;
                }
                else if (nBufferAvailable)
                {
                    *(*bufc)++ = '(';
                    nBufferAvailable--;
                }
            }
            else
            {
                // Get the arglist and count the number of args. Neg # of args
                // means catenate subsequent args.
                //
                if (ufp)
                {
                    nfargs = MAX_ARG;
                }
                else
                {
                    nfargs = fp->maxArgsParsed;
                }

                if (  fp
                   && (fp->flags & FN_NOEVAL))
                {
                    feval = eval & ~(EV_EVAL|EV_TOP|EV_FMAND|EV_STRIP_CURLY);
                }
                else
                {
                    feval = eval & ~(EV_TOP|EV_FMAND);
                }

                UTF8 **fargs = PushPointers(MAX_ARG);
                tstr = parse_arglist_lite(executor, caller, enactor,
                      pStr + iStr + 1, feval, fargs, nfargs, cargs, ncargs,
                      &nfargs);


                // If no closing delim, just insert the '(' and continue normally.
                //
                if (!tstr)
                {
                    if (nBufferAvailable)
                    {
                        *(*bufc)++ = '(';
                        nBufferAvailable--;
                    }
                }
                else
                {
                    iStr = tstr - pStr - 1;

                    // If it's a user-defined function, perform it now.
                    //
                    mudstate.func_nest_lev++;
                    mudstate.func_invk_ctr++;
                    if (mudconf.func_nest_lim <= mudstate.func_nest_lev)
                    {
                         safe_str(T("#-1 FUNCTION RECURSION LIMIT EXCEEDED"), buff, &oldp);
                    }
                    else if (mudconf.func_invk_lim <= mudstate.func_invk_ctr)
                    {
                        safe_str(T("#-1 FUNCTION INVOCATION LIMIT EXCEEDED"), buff, &oldp);
                    }
                    else if (Going(executor))
                    {
                        safe_str(T("#-1 BAD EXECUTOR"), buff, &oldp);
                    }
                    else if (!check_access(executor, ufp ? ufp->perms : fp->perms))
                    {
                        safe_noperm(buff, &oldp);
                    }
                    else if (alarm_clock.alarmed)
                    {
                        safe_str(T("#-1 CPU LIMITED"), buff, &oldp);
                    }
                    else if (ufp)
                    {
                        tbuf = atr_get("mux_exec.1374", ufp->obj, ufp->atr, &aowner, &aflags);
                        if (ufp->flags & FN_PRIV)
                        {
                            i = ufp->obj;
                        }
                        else
                        {
                            i = executor;
                        }

                        reg_ref **preserve = nullptr;

                        if (ufp->flags & FN_PRES)
                        {
                            preserve = PushRegisters(MAX_GLOBAL_REGS);
                            save_global_regs(preserve);
                        }

                        mux_exec(tbuf, LBUF_SIZE-1, buff, &oldp, i, executor, enactor,
                            AttrTrace(aflags, feval), (const UTF8 **)fargs, nfargs);

                        if (ufp->flags & FN_PRES)
                        {
                            restore_global_regs(preserve);
                            PopRegisters(preserve, MAX_GLOBAL_REGS);
                            preserve = nullptr;
                        }
                        free_lbuf(tbuf);
                    }
                    else
                    {
                        // If the number of args is right, perform the func.
                        // Otherwise, return an error message.
                        //
                        if (  fp->minArgs <= nfargs
                           && nfargs <= fp->maxArgs
                           && !alarm_clock.alarmed)
                        {
                            fp->fun(fp, buff, &oldp, executor, caller, enactor,
                                    feval & EV_TRACE, fargs, nfargs, cargs, ncargs);
                        }
                        else
                        {
                            if (fp->minArgs == fp->maxArgs)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    T("#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS"),
                                    fp->name, fp->minArgs);
                            }
                            else if (fp->minArgs + 1 == fp->maxArgs)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    T("#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS"),
                                    fp->name, fp->minArgs, fp->maxArgs);
                            }
                            else if (alarm_clock.alarmed)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch), T("#-1 CPU LIMITED"));
                            }
                            else
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    T("#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS"),
                                    fp->name, fp->minArgs, fp->maxArgs);
                            }
                            safe_str(mux_scratch, buff, &oldp);
                        }
                    }
                    *bufc = oldp;
                    nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    mudstate.func_nest_lev--;
                }

                // Return the space allocated for the arguments.
                //
                for (i = 0; i < nfargs; i++)
                {
                    free_lbuf(fargs[i]);
                }
                PopPointers(fargs, MAX_ARG);
                fargs = nullptr;
            }
            eval &= ~EV_FCHECK;
            isSpecial(L1, '(') = false;
        }
        else if (pStr[iStr] == '%')
        {
            // Percent-replace start.  Evaluate the chars following and
            // perform the appropriate substitution.
            //
            at_space = 0;
            if (!(eval & EV_EVAL))
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = '%';
                    nBufferAvailable--;
                }
                iStr++;
                if (nBufferAvailable)
                {
                    *(*bufc)++ = pStr[iStr];
                    nBufferAvailable--;
                }
            }
            else
            {
                iStr++;
                ch = pStr[iStr];
                unsigned char cType_L2 = isSpecial(L2, ch);
                TempPtr = *bufc;
                int iCode = cType_L2 & 0x3F;
                if (iCode == 1)
                {
                    // 30 31 32 33 34 35 36 37 38 39
                    // 0  1  2  3  4  5  6  7  8  9
                    //
                    // Command argument number N.
                    //
                    i = ch - '0';
                    if (  i < ncargs
                       && cargs[i])
                    {
                        safe_str(cargs[i], buff, bufc);
                        nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    }
                }
                else if (iCode == 2)
                {
                    // 51
                    // Q
                    //
                    iStr++;
                    i = mux_RegisterSet[pStr[iStr]];
                    if (  0 <= i
                       && i < MAX_GLOBAL_REGS)
                    {
                        if (  mudstate.global_regs[i]
                           && mudstate.global_regs[i]->reg_len > 0)
                        {
                            safe_copy_buf(mudstate.global_regs[i]->reg_ptr,
                                mudstate.global_regs[i]->reg_len, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else if (pStr[iStr] == '\0')
                    {
                        iStr--;
                    }
                }
                else if (iCode <= 4)
                {
                    if (iCode == 3)
                    {
                        // 23
                        // #
                        //
                        // Enactor DB number.
                        //
                        mux_scratch[0] = '#';
                        n = mux_ltoa(enactor, mux_scratch+1);
                        safe_copy_buf(mux_scratch, n+1, buff, bufc);
                        nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    }
                    else if (iCode == 4)
                    {
                        // 21
                        // !
                        //
                        // iCode == '!'
                        // Executor DB number.
                        //
                        mux_scratch[0] = '#';
                        n = mux_ltoa(executor, mux_scratch+1);
                        safe_copy_buf(mux_scratch, n+1, buff, bufc);
                        nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    }
                    else
                    {
                        // iCode == 0
                        //
                        // Just copy
                        //
                        if (nBufferAvailable)
                        {
                            *(*bufc)++ = ch;
                            nBufferAvailable--;
                        }
                    }
                }
                else if (iCode <= 6)
                {
                    if (iCode == 6)
                    {
                        // 43 58
                        // C  X
                        //
                        // Color
                        //
                        n = 1;
                        unsigned int iColor;
                        if ('<' == pStr[iStr + n])
                        {
                            n++;

                            while (  '\0' != pStr[iStr + n]
                                  && '>' != pStr[iStr + n])
                            {
                                n++;
                            }
                            if (nStr < iStr + n)
                            {
                                n = nStr - iStr;
                            }
                            if ('>' == pStr[iStr + n])
                            {
                                // Adjust for the x< at the beginning.
                                //
                                iStr += 2;
                                n -= 2;

                                RGB rgb;
                                if (parse_rgb(n, pStr+iStr, rgb))
                                {
                                    iColor = FindNearestPaletteEntry(rgb, true);
                                    if (cType_L2 & 0x40)
                                    {
                                        safe_str(aColors[iColor + COLOR_INDEX_BG].pUTF, buff, bufc);
                                        if (palette[iColor].rgb.r != rgb.r)
                                        {
                                            safe_str(ConvertToUTF8(rgb.r + 0xF0300), buff, bufc);
                                        }
                                        if (palette[iColor].rgb.g != rgb.g)
                                        {
                                            safe_str(ConvertToUTF8(rgb.g + 0xF0400), buff, bufc);
                                        }
                                        if (palette[iColor].rgb.b != rgb.b)
                                        {
                                            safe_str(ConvertToUTF8(rgb.b + 0xF0500), buff, bufc);
                                        }
                                    }
                                    else
                                    {
                                        safe_str(aColors[iColor + COLOR_INDEX_FG].pUTF, buff, bufc);
                                        if (palette[iColor].rgb.r != rgb.r)
                                        {
                                            safe_str(ConvertToUTF8(rgb.r + 0xF0000), buff, bufc);
                                        }
                                        if (palette[iColor].rgb.g != rgb.g)
                                        {
                                            safe_str(ConvertToUTF8(rgb.g + 0xF0100), buff, bufc);
                                        }
                                        if (palette[iColor].rgb.b != rgb.b)
                                        {
                                            safe_str(ConvertToUTF8(rgb.b + 0xF0200), buff, bufc);
                                        }
                                    }
                                    nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                                    ansi = true;
                                }
                                iStr += n;
                            }
                        }
                        else
                        {
                            iColor = ColorTable[pStr[iStr + 1]];
                            if (iColor)
                            {
                                iStr++;
                                ansi = true;
                                safe_str(aColors[iColor].pUTF, buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                            else if (pStr[iStr + 1] && nBufferAvailable)
                            {
                                *(*bufc)++ = pStr[iStr];
                                nBufferAvailable--;
                            }
                        }
                    }
                    else
                    {
                        // 52
                        // R
                        //
                        // Carriage return.
                        //
                        safe_copy_buf(T("\r\n"), 2, buff, bufc);
                        nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    }
                }
                else if (iCode <= 8)
                {
                    if (iCode == 7)
                    {
                        // 42
                        // B
                        //
                        // Blank.
                        //
                        if (nBufferAvailable)
                        {
                            *(*bufc)++ = ' ';
                            nBufferAvailable--;
                        }
                    }
                    else
                    {
                        // 54
                        // T
                        //
                        // Tab.
                        //
                        if (nBufferAvailable)
                        {
                            *(*bufc)++ = '\t';
                            nBufferAvailable--;
                        }
                    }
                }
                else if (iCode <= 10)
                {
                    if (iCode == 9)
                    {
                        // 4C
                        // L
                        //
                        // Enactor Location DB Ref
                        //
                        if (!(eval & EV_NO_LOCATION))
                        {
                            mux_scratch[0] = '#';
                            n = mux_ltoa(where_is(enactor), mux_scratch+1);
                            safe_copy_buf(mux_scratch, n+1, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else
                    {
                        // 56
                        // V
                        //
                        // Variable attribute.
                        //
                        iStr++;
                        if (mux_isazAZ(pStr[iStr]))
                        {
                            i = A_VA + mux_toupper_ascii(pStr[iStr]) - 'A';
                            size_t nAttrGotten;
                            atr_pget_str_LEN(mux_scratch, executor, i,
                                &aowner, &aflags, &nAttrGotten);
                            if (0 < nAttrGotten)
                            {
                                safe_copy_buf(mux_scratch, nAttrGotten, buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                        }
                        else if ('\0' == pStr[iStr])
                        {
                            iStr--;
                        }
                    }
                }
                else if (iCode <= 14)
                {
                    if (iCode <= 12)
                    {
                        if (iCode == 11)
                        {
                            // 25
                            // %
                            //
                            // Percent - a literal %
                            //
                            if (nBufferAvailable)
                            {
                                *(*bufc)++ = '%';
                                nBufferAvailable--;
                            }
                        }
                        else
                        {
                            // 4E
                            // N
                            //
                            // Enactor name
                            //
                            safe_str(Name(enactor), buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else
                    {
                        if (iCode == 13)
                        {
                            // 7C
                            // |
                            //
                            // piped command output.
                            //
                            safe_str(mudstate.pout, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                        else
                        {
                            // 53
                            // S
                            //
                            // Subjective pronoun.
                            //
                            if (gender < 0)
                            {
                                gender = get_gender(enactor);
                            }
                            if (!gender)
                            {
                                constbuf  = Name(enactor);
                            }
                            else
                            {
                                constbuf  = subj[gender];
                            }
                            safe_str(constbuf, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                }
                else if (iCode <= 18)
                {
                    if (iCode <= 16)
                    {
                        if (iCode == 15)
                        {
                            // 50
                            // P
                            //
                            // Personal pronoun.
                            //
                            if (gender < 0)
                            {
                                gender = get_gender(enactor);
                            }

                            if (!gender)
                            {
                                safe_str(Name(enactor), buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                                if (nBufferAvailable)
                                {
                                    *(*bufc)++ = 's';
                                    nBufferAvailable--;
                                }
                            }
                            else
                            {
                                safe_str(poss[gender], buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                        }
                        else // if (iCode == 16)
                        {
                            // 4F
                            // O
                            //
                            // Objective pronoun.
                            //
                            if (gender < 0)
                            {
                                gender = get_gender(enactor);
                            }
                            if (!gender)
                            {
                                constbuf = Name(enactor);
                            }
                            else
                            {
                                constbuf = obj[gender];
                            }
                            safe_str(constbuf, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else
                    {
                        if (iCode == 17)
                        {
                            // 41
                            // A
                            //
                            // Absolute posessive.
                            // Idea from Empedocles.
                            //
                            if (gender < 0)
                            {
                                gender = get_gender(enactor);
                            }

                            if (!gender)
                            {
                                safe_str(Name(enactor), buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                                if (nBufferAvailable)
                                {
                                    *(*bufc)++ = 's';
                                    nBufferAvailable--;
                                }
                            }
                            else
                            {
                                safe_str(absp[gender], buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                        }
                        else // if (iCode == 18)
                        {
                            // 00
                            // \0
                            //
                            // All done.
                            //
                            iStr--;
                        }
                    }
                }
                else if (iCode <= 22)
                {
                    if (iCode <= 20)
                    {
                        if (iCode == 19)
                        {
                            // 4D
                            // M
                            //
                            // Last command
                            //
                            safe_str(mudstate.curr_cmd, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                        else // if (iCode == 20)
                        {
                            // 40
                            // @
                            //
                            // Caller DB number.
                            //
                            mux_scratch[0] = '#';
                            n = mux_ltoa(caller, mux_scratch+1);
                            safe_copy_buf(mux_scratch, n+1, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else
                    {
                        if (iCode == 21)
                        {
                            // 3D
                            // =
                            //
                            // %=<attr> like v(attr).
                            //
                            n = 1;
                            if ('<' == pStr[iStr + n])
                            {
                                n++;

                                while (  '\0' != pStr[iStr + n]
                                      && '>' != pStr[iStr + n])
                                {
                                    n++;
                                }
                                if (nStr < iStr + n)
                                {
                                    n = nStr - iStr;
                                }
                                if ('>' == pStr[iStr + n])
                                {
                                    // Adjust for the =< at the beginning.
                                    //
                                    iStr += 2;
                                    n -= 2;

                                    memcpy(mux_scratch, pStr + iStr, n);
                                    mux_scratch[n] = '\0';

                                    if (mux_isdigit(mux_scratch[0]))
                                    {
                                        // This hideous thing converts 3-digit non-negative integers to
                                        // a longs without loops and with at most one multiply.
                                        //
                                        i = mux_isdigit(mux_scratch[1])
                                          ? (  mux_isdigit(mux_scratch[2])
                                            ?  (  mux_isdigit(mux_scratch[3])
                                               ?  MAX_ARG
                                               :  (  10*TableATOI(mux_scratch[0]-'0', mux_scratch[1]-'0')
                                                  +  mux_scratch[2]-'0'))
                                            :  TableATOI(mux_scratch[0]-'0', mux_scratch[1]-'0'))
                                          : (mux_scratch[0]-'0');

                                        if (  i < ncargs
                                           && nullptr != cargs[i])
                                        {
                                            safe_str(cargs[i], buff, bufc);
                                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                                        }
                                    }
                                    else if (mux_isattrnameinitial(mux_scratch))
                                    {
                                        ATTR *ap = atr_str(mux_scratch);
                                        if (  ap
                                           && See_attr(executor, executor, ap))
                                        {
                                            size_t nLen;
                                            atr_pget_str_LEN(mux_scratch, executor, ap->number, &aowner, &aflags, &nLen);
                                            safe_copy_buf(mux_scratch, nLen, buff, bufc);
                                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                                        }
                                    }
                                    iStr += n;
                                }
                            }
                        }
                        else // if (iCode == 22)
                        {
                            // 2B
                            // +
                            //
                            // Ncargs substitution
                            //
                            safe_i64toa(ncargs, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                }
                else
                {
                    if (iCode == 23)
                    {
                        // 4B or 6B
                        // k or K
                        //
                        // Moniker substitution
                        //
                        safe_str(Moniker(enactor), buff, bufc);
                        nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                    }
                    else // (iCode == 24)
                    {
                        // 49 or 69
                        // i or I
                        // itext() substitutions
                        //
                        ch = pStr[iStr+1];
                        if (mux_isdigit(ch))
                        {
                            iStr++;
                            i = mudstate.in_loop - (ch - '0') - 1;
                            if (  0 <= i
                               && i < MAX_ITEXT)
                            {
                                safe_str(mudstate.itext[i], buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                        }
                        else if (  '\0' != ch
                                && nBufferAvailable)
                        {
                            *(*bufc)++ = pStr[iStr];
                            nBufferAvailable--;
                        }
                    }
                }

                // For some escape letters, if the escape letter
                // was upper-case, then upper-case the first
                // letter of the value.
                //
                if (cType_L2 & 0x80)
                {
                    *TempPtr = mux_toupper_ascii(*TempPtr);
                }
            }
        }
        else if (pStr[iStr] == '[')
        {
            // Function start.  Evaluate the contents of the square brackets
            // as a function. If no closing bracket, insert the '[' and
            // continue.
            //
            mudstate.nStackNest++;
            tstr = parse_to_lite(pStr + iStr + 1, ']', '\0', &n, &at_space);
            at_space = 0;
            if (tstr == nullptr)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = '[';
                    nBufferAvailable--;
                }
            }
            else
            {
                iStr++;
                if (nStr < iStr + n)
                {
                    n = nStr - iStr;
                }
                mudstate.nStackNest--;
                mux_exec(pStr + iStr, n, buff, bufc, executor, caller, enactor,
                    (eval | EV_FCHECK | EV_FMAND) & ~EV_TOP, cargs,
                    ncargs);
                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                iStr = tstr - pStr - 1;
            }
        }

        // At this point, pStr[iStr] must be one of the following characters:
        //
        // 0x20 0x5C 0x7B
        // SP    \    {
        //
        else if (pStr[iStr] == ' ')
        {
            // A space. Add a space if not compressing or if previous char was
            // not a space.
            //
            if (bSpaceIsSpecial && !at_space)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = ' ';
                    nBufferAvailable--;
                }
                at_space = 1;
            }
        }
        else if (pStr[iStr] == '{')
        {
            // pStr[iStr] == '{'
            //
            // Literal start.  Insert everything up to the terminating '}'
            // without parsing. If no closing brace, insert the '{' and
            // continue.
            //
            mudstate.nStackNest++;
            tstr = parse_to_lite(pStr + iStr + 1, '}', '\0', &n, &at_space);
            at_space = 0;
            if (nullptr == tstr)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = '{';
                    nBufferAvailable--;
                }
            }
            else
            {
                iStr++;
                mudstate.nStackNest--;
                if (!(eval & EV_STRIP_CURLY))
                {
                    if (nBufferAvailable)
                    {
                        *(*bufc)++ = '{';
                        nBufferAvailable--;
                    }
                }

                if (eval & EV_EVAL)
                {
                    // Preserve leading spaces (Felan)
                    //
                    i = 0;
                    if (' ' == pStr[iStr])
                    {
                        if (nBufferAvailable)
                        {
                            *(*bufc)++ = ' ';
                            nBufferAvailable--;
                        }
                        i = 1;
                    }
                    if (nStr < iStr + n)
                    {
                        n = nStr - iStr;
                    }

                    mux_exec(pStr + iStr + i, n - i, buff, bufc, executor, caller, enactor,
                        (eval & ~(EV_STRIP_CURLY | EV_FCHECK | EV_FMAND | EV_TOP)),
                        cargs, ncargs);
                }
                else
                {
                    if (nStr < iStr + n)
                    {
                        n = nStr - iStr;
                    }

                    mux_exec(pStr + iStr, n, buff, bufc, executor, caller, enactor,
                        eval & ~(EV_TOP | EV_FMAND), cargs, ncargs);
                }
                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;

                if (!(eval & EV_STRIP_CURLY))
                {
                    if (nBufferAvailable)
                    {
                        *(*bufc)++ = '}';
                        nBufferAvailable--;
                    }
                }
                iStr = tstr - pStr - 1;
            }
        }
        else  // if (pStr[iStr] == '\\')
        {
            // pStr[iStr] must be \.
            //
            // General escape. Add the following char without special
            // processing.
            //
            at_space = 0;
            iStr++;
            if (pStr[iStr])
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = pStr[iStr];
                    nBufferAvailable--;
                }
            }
            else
            {
                iStr--;
            }
        }
        iStr++;
    }

    // If we're eating spaces, and the last thing was a space, eat it up.
    // Complicated by the fact that at_space is initially true. So check to
    // see if we actually put something in the buffer, too.
    //
    if (  bSpaceIsSpecial
       && at_space
       && start != *bufc)
    {
        (*bufc)--;
    }

    **bufc = '\0';

    // Collect and report trace information.
    //
    if (is_trace)
    {
        tcache_add(executor, savestr, start);
        if (  is_top
           || !mudconf.trace_topdown)
        {
            tcache_finish();
        }
        if (  is_top
           && 0 < tcache_count - mudconf.trace_limit)
        {
            tbuf = alloc_mbuf("exec.trace_diag");
            mux_sprintf(tbuf, MBUF_SIZE, T("%d lines of trace output discarded."), tcache_count
                - mudconf.trace_limit);
            notify(executor, tbuf);
            free_mbuf(tbuf);
        }
    }

    if (  realbuff
       || ansi
       || (eval & EV_TOP))
    {
        // We need to transfer and/or ANSI optimize the result.
        //
        size_t nPos = 0;
        if (realbuff)
        {
            *realbp = '\0';
            nPos = TruncateToBuffer(realbuff, mux_scratch, LBUF_SIZE-1);
        }
        nPos += TruncateToBuffer(buff, mux_scratch + nPos, (LBUF_SIZE-1) - nPos);
        if (realbuff)
        {
            MEMFREE(buff);
            buff = realbuff;
        }

        memcpy(buff, mux_scratch, nPos + 1);
        *bufc = buff + nPos;
    }

    // Restore Parser Mode.
    //
    isSpecial(L1, ' ') = bSpaceIsSpecialSave;
    isSpecial(L1, '(') = bParenthesisIsSpecialSave;
    isSpecial(L1, '[') = bBracketIsSpecialSave;
}

/* ---------------------------------------------------------------------------
 * save_global_regs, restore_global_regs:  Save and restore the global
 * registers to protect them from various sorts of munging.
 */

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
