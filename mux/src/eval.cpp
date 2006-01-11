// eval.cpp -- Command evaluation and cracking.
//
// $Id: eval.cpp,v 1.39 2006-01-11 20:56:29 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "attrs.h"
#include "functions.h"

//-----------------------------------------------------------------------------
// parse_to: Split a line at a character, obeying nesting.  The line is
// destructively modified (a null is inserted where the delimiter was found)
// dstr is modified to point to the char after the delimiter, and the function
// return value points to the found string (space compressed if specified). If
// we ran off the end of the string without finding the delimiter, dstr is
// returned as NULL.
//
static char *parse_to_cleanup( int eval, int first, char *cstr, char *rstr,
                               char *zstr, char *strFirewall)
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
// 4 is 0x25 '%', 0x5C '\\', or 0x1B ESC not overridable.
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
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 4, 0, 0, 0, 0, // 0x10-0x1F
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
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0, // 0x10-0x1F
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

// Stephen: Some silly compilers don't handle aliased pointers well. For these
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


char *parse_to(char **dstr, char delim, int eval)
{
#define stacklim 32
    char stack[stacklim];
    char *rstr, *cstr, *zstr, *strFirewall;
    int sp, tp, bracketlev;

    if (  dstr == NULL
       || *dstr == NULL)
    {
        return NULL;
    }

    if (**dstr == '\0')
    {
        rstr = *dstr;
        *dstr = NULL;
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
            // 4 is 0x25 '%', 0x5C '\\', or 0x1B ESC not overridable.
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
                // 4 is 0x25 '%', 0x5C '\\', or 0x1B ESC not overridable.
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
                    // %, \, and ESC escapes.
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
                            // %, \, and ESC escapes.
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
    *dstr = NULL;
    return rstr;
}

// This version parse_to is less destructive. It only null-terminates the source
// It doesn't process escapes. It's useful with mux_exec which will be copying
// the characters to another buffer anyway and is more than able to perform the
// escapes and trimming.
//
static char *parse_to_lite(char **dstr, char delim1, char delim2, size_t *nLen, int *iWhichDelim)
{
#define stacklim 32
    char stack[stacklim];
    char *rstr, *cstr;
    int sp, tp, bracketlev;

    if (  dstr == NULL
       || *dstr == NULL)
    {
        *nLen = 0;
        return NULL;
    }

    if (**dstr == '\0')
    {
        rstr = *dstr;
        *dstr = NULL;
        *nLen = 0;
        return rstr;
    }
    sp = 0;
    cstr = rstr = *dstr;
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
                    static char matcher[2] = { ']', ')'};
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
                        *cstr = '\0';
                        *nLen = (cstr - rstr);
                        *dstr = ++cstr;
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
                        *cstr = '\0';
                        *nLen = (cstr - rstr);
                        *dstr = ++cstr;
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
    *cstr = '\0';
    *nLen = (cstr - rstr);
    *dstr = NULL;
    return rstr;
}

//-----------------------------------------------------------------------------
// parse_arglist: Parse a line into an argument list contained in lbufs. A
// pointer is returned to whatever follows the final delimiter. If the arglist
// is unterminated, a NULL is returned.  The original arglist is destructively
// modified.
//
char *parse_arglist( dbref executor, dbref caller, dbref enactor, char *dstr,
                     char delim, dbref eval, char *fargs[], dbref nfargs,
                     char *cargs[], dbref ncargs, int *nArgsParsed )
{
    char *rstr, *tstr, *bp, *str;
    int arg, peval;

    if (dstr == NULL)
    {
        *nArgsParsed = 0;
        return NULL;
    }

    size_t nLen;
    int iWhichDelim;
    rstr = parse_to_lite(&dstr, delim, '\0', &nLen, &iWhichDelim);
    arg = 0;

    peval = (eval & ~EV_EVAL);

    while (  arg < nfargs
          && rstr)
    {
        if (arg < nfargs - 1)
        {
            tstr = parse_to(&rstr, ',', peval);
        }
        else
        {
            tstr = parse_to(&rstr, '\0', peval);
        }

        bp = fargs[arg] = alloc_lbuf("parse_arglist");
        if (eval & EV_EVAL)
        {
            str = tstr;
            mux_exec(fargs[arg], &bp, executor, caller, enactor,
                     eval | EV_FCHECK, &str, cargs, ncargs);
            *bp = '\0';
        }
        else
        {
            mux_strncpy(fargs[arg], tstr, LBUF_SIZE-1);
        }
        arg++;
    }
    *nArgsParsed = arg;
    return dstr;
}

static char *parse_arglist_lite( dbref executor, dbref caller, dbref enactor,
                          char *dstr, char delim, int eval, char *fargs[],
                          dbref nfargs, char *cargs[], dbref ncargs,
                          int *nArgsParsed)
{
    UNUSED_PARAMETER(delim);

    char *tstr, *bp, *str;

    if (dstr == NULL)
    {
        *nArgsParsed = 0;
        return NULL;
    }

    size_t nLen;
    int peval = eval;
    if (eval & EV_EVAL)
    {
        peval = eval | EV_FCHECK;
    }
    else
    {
        peval = ((eval & ~EV_FCHECK)|EV_NOFCHECK);
    }
    int arg = 0;
    int iWhichDelim = 0;
    while (  arg < nfargs
          && dstr
          && iWhichDelim != 2)
    {
        if (arg < nfargs - 1)
        {
            tstr = parse_to_lite(&dstr, ',', ')', &nLen, &iWhichDelim);
        }
        else
        {
            tstr = parse_to_lite(&dstr, '\0', ')', &nLen, &iWhichDelim);
        }

        if (  iWhichDelim == 2
           && arg == 0
           && tstr[0] == '\0')
        {
            break;
        }

        bp = fargs[arg] = alloc_lbuf("parse_arglist");
        str = tstr;
        mux_exec(fargs[arg], &bp, executor, caller, enactor, peval, &str,
                 cargs, ncargs);
        *bp = '\0';
        arg++;
    }
    *nArgsParsed = arg;
    return dstr;
}

//-----------------------------------------------------------------------------
// exec: Process a command line, evaluating function calls and %-substitutions.
//
int get_gender(dbref player)
{
    dbref aowner;
    int aflags;
    char *atr_gotten = atr_pget(player, A_SEX, &aowner, &aflags);
    char first = atr_gotten[0];
    free_lbuf(atr_gotten);
    switch (mux_tolower(first))
    {
    case 'p':
        return 4;

    case 'm':
        return 3;

    case 'f':
    case 'w':
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
    char *orig;
    char *result;
    struct tcache_ent *next;
} *tcache_head;

static bool tcache_top;
static int  tcache_count;

void tcache_init(void)
{
    tcache_head = NULL;
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

static void tcache_add(dbref player, char *orig, char *result)
{
    if (strcmp(orig, result))
    {
        tcache_count++;
        if (tcache_count <= mudconf.trace_limit)
        {
            TCENT *xp = (TCENT *) alloc_sbuf("tcache_add.sbuf");
            char *tp = alloc_lbuf("tcache_add.lbuf");

            size_t nvw;
            ANSI_TruncateToField(result, LBUF_SIZE, tp, LBUF_SIZE,
                &nvw, ANSI_ENDGOAL_NORMAL);
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
    else
    {
        free_lbuf(orig);
    }
}

static void tcache_finish(void)
{
    while (tcache_head != NULL)
    {
        TCENT *xp = tcache_head;
        tcache_head = xp->next;
        notify(Owner(xp->player), tprintf("%s(#%d)} '%s' -> '%s'", Name(xp->player),
            xp->player, xp->orig, xp->result));
        free_lbuf(xp->orig);
        free_lbuf(xp->result);
        free_sbuf(xp);
    }
    tcache_top = true;
    tcache_count = 0;
}

const char *ColorTable[256] =
{
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x10-0x1F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,      // 0x30-0x3F
    0,           0,             ANSI_BBLUE,  ANSI_BCYAN,  // 0x40-0x43
    0,           0,             0,           ANSI_BGREEN, // 0x44-0x47
    0,           0,             0,           0,           // 0x48-0x4B
    0,           ANSI_BMAGENTA, 0,           0,           // 0x4B-0x4F
    0,           0,             ANSI_BRED,   0,           // 0x50-0x53
    0,           0,             0,           ANSI_BWHITE, // 0x54-0x57
    ANSI_BBLACK, ANSI_BYELLOW,  0,           0,           // 0x58-0x5B
    0,           0,             0,           0,           // 0x5B-0x5F
    0,           0,             ANSI_BLUE,   ANSI_CYAN,   // 0x60-0x63
    0,           0,             ANSI_BLINK,  ANSI_GREEN,  // 0x64-0x67
    ANSI_HILITE, ANSI_INVERSE,  0,           0,           // 0x68-0x6B
    0,           ANSI_MAGENTA,  ANSI_NORMAL, 0,           // 0x6C-0x6F
    0,           0,             ANSI_RED,    0,           // 0x70-0x73
    0,           ANSI_UNDER,    0,           ANSI_WHITE,  // 0x74-0x77
    ANSI_BLACK,  ANSI_YELLOW,   0,           0,           // 0x78-0x7B
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

static bool isSpecial_L1[256] =
{
    1, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 0, 0, 0, 0, // 0x10-0x1F
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
      0,  4,  0,  3,  0, 11,  0,  0,   0,  0,  0,  0,  0,  0,  0,  0, // 0x20-0x2F
      1,  1,  1,  1,  1,  1,  1,  1,   1,  1,  0,  0,  0,  0,  0,  0, // 0x30-0x3F
     20,145,  7,  6,  0,  0,  0,  0,   0,  0,  0,  0,  9,147,140,144, // 0x40-0x4F
    143,130,  5,142,  8,  0,138,  0,   6,  0,  0,  0,  0,  0,  0,  0, // 0x50-0x5F
     20, 17,  7,  6,  0,  0,  0,  0,   0,  0,  0,  0,  9, 19, 12, 16, // 0x60-0x6F
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

#define PTRS_PER_FRAME ((LBUF_SIZE - sizeof(char *) - sizeof(int))/sizeof(char *))
typedef struct tag_ptrsframe
{
    int   nptrs;
    char *ptrs[PTRS_PER_FRAME];
    struct tag_ptrsframe *next;
} PtrsFrame;

static PtrsFrame *pPtrsFrame = NULL;

char **PushPointers(int nNeeded)
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

void PopPointers(char **p, int nNeeded)
{
    UNUSED_PARAMETER(p);

    if (pPtrsFrame->nptrs == PTRS_PER_FRAME)
    {
        PtrsFrame *q = pPtrsFrame->next;
        free_lbuf((char *)pPtrsFrame);
        pPtrsFrame = q;
    }
    //mux_assert(p == pPtrsFrame->ptrs + pPtrsFrame->nptrs);
    pPtrsFrame->nptrs += nNeeded;
}

#define INTS_PER_FRAME ((LBUF_SIZE - sizeof(char *) - sizeof(int))/sizeof(int))
typedef struct tag_intsframe
{
    int    nints;
    size_t ints[INTS_PER_FRAME];
    struct tag_intsframe *next;
} IntsFrame;

static IntsFrame *pIntsFrame = NULL;

size_t *PushLengths(int nNeeded)
{
    if (  !pIntsFrame
       || pIntsFrame->nints < nNeeded)
    {
        IntsFrame *p = (IntsFrame *)alloc_lbuf("PushLengths");
        p->next = pIntsFrame;
        p->nints = INTS_PER_FRAME;
        pIntsFrame = p;
    }
    pIntsFrame->nints -= nNeeded;
    return pIntsFrame->ints + pIntsFrame->nints;
}

void PopLengths(size_t *pi, int nNeeded)
{
    UNUSED_PARAMETER(pi);

    if (pIntsFrame->nints == INTS_PER_FRAME)
    {
        IntsFrame *p = pIntsFrame->next;
        free_lbuf((char *)pIntsFrame);
        pIntsFrame = p;
    }
    //mux_assert(pi == pIntsFrame->ints + pIntsFrame->nints);
    pIntsFrame->nints += nNeeded;
}

void mux_exec( char *buff, char **bufc, dbref executor, dbref caller,
               dbref enactor, int eval, char **dstr, char *cargs[], int ncargs)
{
    if (  *dstr == NULL
       || **dstr == '\0'
       || MuxAlarm.bAlarmed)
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

    char *TempPtr;
    char *tstr, *tbuf, *start, *oldp, *savestr;
    const char *constbuf;
    char ch;
    char *realbuff = NULL, *realbp = NULL;
    dbref aowner;
    int nfargs, aflags, feval, i;
    size_t n;
    bool ansi = false;
    FUN *fp;
    UFUN *ufp;

    static const char *subj[5] = {"", "it", "she", "he", "they"};
    static const char *poss[5] = {"", "its", "her", "his", "their"};
    static const char *obj[5] =  {"", "it", "her", "him", "them"};
    static const char *absp[5] = {"", "its", "hers", "his", "theirs"};

    // This is scratch buffer is used potentially on every invocation of
    // mux_exec. Do not assume that its contents are valid after you
    // execute any function that could re-enter mux_exec.
    //
    static char mux_scratch[LBUF_SIZE];

    char *pdstr = *dstr;

    int at_space = 1;
    int gender = -1;

    bool is_trace = Trace(executor) && !(eval & EV_NOTRACE);
    bool is_top = false;

    // Extend the buffer if we need to.
    //
    if (LBUF_SIZE - SBUF_SIZE < (*bufc) - buff)
    {
        realbuff = buff;
        realbp = *bufc;
        buff = (char *)MEMALLOC(LBUF_SIZE);
        ISOUTOFMEMORY(buff);
        *bufc = buff;
    }

    oldp = start = *bufc;

    // If we are tracing, save a copy of the starting buffer.
    //
    savestr = NULL;
    if (is_trace)
    {
        is_top = tcache_empty();
        savestr = alloc_lbuf("exec.save");
        mux_strncpy(savestr, pdstr, LBUF_SIZE-1);
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
    for (;;)
    {
        // Handle mundane characters specially. There are usually a lot of them.
        // Just copy them.
        //
        if (!isSpecial(L1, *pdstr))
        {
            char *p = pdstr + 1;
            while (!isSpecial(L1, *p++))
            {
                ; // Nothing.
            }
            n = p - pdstr - 1;
            if (nBufferAvailable < n)
            {
                n = nBufferAvailable;
            }
            memcpy(*bufc, pdstr, n);
            nBufferAvailable -= n;
            *bufc += n;
            at_space = 0;
            pdstr = p - 1;
        }


        // At this point, **dstr must be one of the following characters:
        //
        // 0x00 0x20 0x25 0x28 0x5B 0x5C 0x7B
        // NULL  SP   %    (     [    \   {
        //
        // Test softcode shows the following distribution:
        //
        // NULL occurs 116948 times
        //   (  occurs  49567 times
        //   %  occurs  24553 times
        //   [  occurs   7618 times
        //  SP  occurs   1323 times
        //
        if (*pdstr == '\0')
        {
            break;
        }
        else if (*pdstr == '(')
        {
            // *pdstr == '('
            //
            // Arglist start.  See if what precedes is a function. If so,
            // execute it if we should.
            //
            at_space = 0;

            // Load an sbuf with an lowercase version of the func name, and
            // see if the func exists. Trim trailing spaces from the name if
            // configured.
            //
            char *pEnd = *bufc - 1;
            if (mudconf.space_compress && (eval & EV_FMAND))
            {
                while (  oldp <= pEnd
                      && mux_isspace(*pEnd))
                {
                    pEnd--;
                }
            }

            // _strlwr(tbuf);
            //
            char *p2 = mux_scratch;
            for (char *p = oldp; p <= pEnd; p++)
            {
                *p2++ = mux_tolower(*p);
            }
            *p2 = '\0';

            size_t ntbuf = p2 - mux_scratch;
            fp = (FUN *)hashfindLEN(mux_scratch, ntbuf, &mudstate.func_htab);

            // If not a builtin func, check for global func.
            //
            ufp = NULL;
            if (fp == NULL)
            {
                ufp = (UFUN *)hashfindLEN(mux_scratch, ntbuf, &mudstate.ufunc_htab);
            }

            // Do the right thing if it doesn't exist.
            //
            if (!fp && !ufp)
            {
                if (eval & EV_FMAND)
                {
                    *bufc = oldp;
                    safe_str("#-1 FUNCTION (", buff, bufc);
                    safe_str(mux_scratch, buff, bufc);
                    safe_str(") NOT FOUND", buff, bufc);
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

                tstr = pdstr;
                if (  fp
                   && (fp->flags & FN_NOEVAL))
                {
                    feval = eval & ~(EV_EVAL|EV_TOP|EV_STRIP_CURLY);
                }
                else
                {
                    feval = eval & ~EV_TOP;
                }

                char **fargs = PushPointers(MAX_ARG);
                pdstr = parse_arglist_lite(executor, caller, enactor,
                      pdstr + 1, ')', feval, fargs, nfargs, cargs, ncargs,
                      &nfargs);


                // If no closing delim, just insert the '(' and continue normally.
                //
                if (!pdstr)
                {
                    pdstr = tstr;
                    if (nBufferAvailable)
                    {
                        *(*bufc)++ = *pdstr;
                        nBufferAvailable--;
                    }
                }
                else
                {
                    pdstr--;

                    // If it's a user-defined function, perform it now.
                    //
                    mudstate.func_nest_lev++;
                    mudstate.func_invk_ctr++;
                    if (mudconf.func_nest_lim <= mudstate.func_nest_lev)
                    {
                         safe_str("#-1 FUNCTION RECURSION LIMIT EXCEEDED", buff, &oldp);
                    }
                    else if (mudconf.func_invk_lim <= mudstate.func_invk_ctr)
                    {
                        safe_str("#-1 FUNCTION INVOCATION LIMIT EXCEEDED", buff, &oldp);
                    }
                    else if (Going(executor))
                    {
                        safe_str("#-1 BAD EXECUTOR", buff, &oldp);
                    }
                    else if (!check_access(executor, ufp ? ufp->perms : fp->perms))
                    {
                        safe_noperm(buff, &oldp);
                    }
                    else if (MuxAlarm.bAlarmed)
                    {
                        safe_str("#-1 CPU LIMITED", buff, &oldp);
                    }
                    else if (ufp)
                    {
                        tstr = atr_get(ufp->obj, ufp->atr, &aowner, &aflags);
                        if (ufp->flags & FN_PRIV)
                        {
                            i = ufp->obj;
                        }
                        else
                        {
                            i = executor;
                        }
                        TempPtr = tstr;

                        char **preserve = NULL;
                        size_t *preserve_len = NULL;

                        if (ufp->flags & FN_PRES)
                        {
                            preserve = PushPointers(MAX_GLOBAL_REGS);
                            preserve_len = PushLengths(MAX_GLOBAL_REGS);
                            save_global_regs("eval_save", preserve, preserve_len);
                        }

                        mux_exec(buff, &oldp, i, executor, enactor, feval,
                                 &TempPtr, fargs, nfargs);

                        if (ufp->flags & FN_PRES)
                        {
                            restore_global_regs("eval_restore", preserve, preserve_len);
                            PopLengths(preserve_len, MAX_GLOBAL_REGS);
                            PopPointers(preserve, MAX_GLOBAL_REGS);
                            preserve = NULL;
                            preserve_len = NULL;
                        }
                        free_lbuf(tstr);
                    }
                    else
                    {
                        // If the number of args is right, perform the func.
                        // Otherwise, return an error message.
                        //
                        if (  fp->minArgs <= nfargs
                           && nfargs <= fp->maxArgs
                           && !MuxAlarm.bAlarmed)
                        {
                            fp->fun(buff, &oldp, executor, caller, enactor,
                                    fargs, nfargs, cargs, ncargs);
                        }
                        else
                        {
                            if (fp->minArgs == fp->maxArgs)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    "#-1 FUNCTION (%s) EXPECTS %d ARGUMENTS",
                                    fp->name, fp->minArgs);
                            }
                            else if (fp->minArgs + 1 == fp->maxArgs)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    "#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS",
                                    fp->name, fp->minArgs, fp->maxArgs);
                            }
                            else if (MuxAlarm.bAlarmed)
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch), "#-1 CPU LIMITED");
                            }
                            else
                            {
                                mux_sprintf(mux_scratch, sizeof(mux_scratch),
                                    "#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS",
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
                fargs = NULL;
            }
            eval &= ~EV_FCHECK;
            isSpecial(L1, '(') = false;
        }
        else if (*pdstr == '%')
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
                pdstr++;
                if (nBufferAvailable)
                {
                    *(*bufc)++ = *pdstr;
                    nBufferAvailable--;
                }
            }
            else
            {
                pdstr++;
                ch = *pdstr;
                unsigned char cType_L2 = isSpecial(L2, ch);
                TempPtr = *bufc;
                int iCode = cType_L2 & 0x7F;
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
                    pdstr++;
                    i = mux_RegisterSet[(unsigned char)*pdstr];
                    if (  0 <= i
                       && i < MAX_GLOBAL_REGS)
                    {
                        if (  mudstate.glob_reg_len[i] > 0
                           && mudstate.global_regs[i])
                        {
                            safe_copy_buf(mudstate.global_regs[i],
                                mudstate.glob_reg_len[i], buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                    else if (*pdstr == '\0')
                    {
                        pdstr--;
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
                        const char *pColor = ColorTable[(unsigned char)pdstr[1]];
                        if (pColor)
                        {
                            pdstr++;
                            ansi = true;
                            safe_str(pColor, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                        else if (pdstr[1] && nBufferAvailable)
                        {
                            *(*bufc)++ = *pdstr;
                            nBufferAvailable--;
                        }
                    }
                    else
                    {
                        // 52
                        // R
                        //
                        // Carriage return.
                        //
                        safe_copy_buf("\r\n", 2, buff, bufc);
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
                        pdstr++;
                        if (mux_isalpha(*pdstr))
                        {
                            i = A_VA + mux_toupper(*pdstr) - 'A';
                            size_t nAttrGotten;
                            atr_pget_str_LEN(mux_scratch, executor, i,
                                &aowner, &aflags, &nAttrGotten);
                            if (0 < nAttrGotten)
                            {
                                if (nAttrGotten > nBufferAvailable)
                                {
                                    nAttrGotten = nBufferAvailable;
                                }
                                memcpy(*bufc, mux_scratch, nAttrGotten);
                                *bufc += nAttrGotten;
                                nBufferAvailable -= nAttrGotten;
                            }
                        }
                        else if (ch == '\0')
                        {
                            pdstr--;
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
                else
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
                                safe_str((char *)poss[gender], buff, bufc);
                                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                            }
                        }
                        else
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
                        else if (iCode == 18)
                        {
                            // 00
                            // \0
                            //
                            // All done.
                            //
                            pdstr--;
                        }
                        else if (iCode == 19)
                        {
                            // 4D
                            // M
                            //
                            // Last command
                            //
                            safe_str(mudstate.curr_cmd, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                        else
                        {
                            // 40
                            // @
                            //
                            // iCode == '@'
                            // Caller DB number.
                            //
                            mux_scratch[0] = '#';
                            n = mux_ltoa(caller, mux_scratch+1);
                            safe_copy_buf(mux_scratch, n+1, buff, bufc);
                            nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                        }
                    }
                }

                // For some escape letters, if the escape letter
                // was upper-case, then upper-case the first
                // letter of the value.
                //
                if (cType_L2 & 0x80)
                {
                    *TempPtr = mux_toupper(*TempPtr);
                }
            }
        }
        else if (*pdstr == '[')
        {
            // Function start.  Evaluate the contents of the square brackets
            // as a function. If no closing bracket, insert the '[' and
            // continue.
            //
            tstr = pdstr++;
            mudstate.nStackNest++;
            tbuf = parse_to_lite(&pdstr, ']', '\0', &n, &at_space);
            at_space = 0;
            if (pdstr == NULL)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = '[';
                    nBufferAvailable--;
                }
                pdstr = tstr;
            }
            else
            {
                mudstate.nStackNest--;
                TempPtr = tbuf;
                mux_exec(buff, bufc, executor, caller, enactor,
                    (eval | EV_FCHECK | EV_FMAND) & ~EV_TOP, &TempPtr, cargs,
                    ncargs);
                nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                pdstr--;
            }
        }

        // At this point, *pdstr must be one of the following characters:
        //
        // 0x20 0x5C 0x7B
        // SP    \    {
        //
        else if (*pdstr == ' ')
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
        else if (*pdstr == '{')
        {
            // *pdstr == '{'
            //
            // Literal start.  Insert everything up to the terminating '}'
            // without parsing. If no closing brace, insert the '{' and
            // continue.
            //
            tstr = pdstr++;
            mudstate.nStackNest++;
            tbuf = parse_to_lite(&pdstr, '}', '\0', &n, &at_space);
            at_space = 0;
            if (pdstr == NULL)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = '{';
                    nBufferAvailable--;
                }
                pdstr = tstr;
            }
            else
            {
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
                    if (*tbuf == ' ')
                    {
                        if (nBufferAvailable)
                        {
                            *(*bufc)++ = ' ';
                            nBufferAvailable--;
                        }
                        tbuf++;
                    }

                    TempPtr = tbuf;
                    mux_exec(buff, bufc, executor, caller, enactor,
                        (eval & ~(EV_STRIP_CURLY | EV_FCHECK | EV_TOP)),
                        &TempPtr, cargs, ncargs);
                }
                else
                {
                    TempPtr = tbuf;
                    mux_exec(buff, bufc, executor, caller, enactor,
                        eval & ~EV_TOP, &TempPtr, cargs, ncargs);
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
                pdstr--;
            }
        }
        else if (*pdstr == '\\')
        {
            // *pdstr must be \.
            //
            // General escape. Add the following char without special
            // processing.
            //
            at_space = 0;
            pdstr++;
            if (*pdstr)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = *pdstr;
                    nBufferAvailable--;
                }
            }
            else
            {
                pdstr--;
            }
        }
        else
        {
            // *pdstr must be ESC.
            //
            at_space = 0;
            if (nBufferAvailable)
            {
                *(*bufc)++ = *pdstr;
                nBufferAvailable--;
            }
            pdstr++;
            if (*pdstr)
            {
                if (nBufferAvailable)
                {
                    *(*bufc)++ = *pdstr;
                    nBufferAvailable--;
                }
            }
            else
            {
                pdstr--;
            }
        }
        pdstr++;
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
            mux_sprintf(tbuf, MBUF_SIZE, "%d lines of trace output discarded.", tcache_count
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
        static struct ANSI_In_Context aic;
        static struct ANSI_Out_Context aoc;

        ANSI_String_Out_Init(&aoc, mux_scratch, sizeof(mux_scratch),
            sizeof(mux_scratch), ANSI_ENDGOAL_NORMAL);
        if (realbuff)
        {
            *realbp = '\0';
            ANSI_String_In_Init(&aic, realbuff, ANSI_ENDGOAL_NORMAL);
            ANSI_String_Copy(&aoc, &aic, sizeof(mux_scratch));
        }
        ANSI_String_In_Init(&aic, buff, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, sizeof(mux_scratch));
        if (realbuff)
        {
            MEMFREE(buff);
            buff = realbuff;
        }

        size_t nVisualWidth;
        size_t nLen = ANSI_String_Finalize(&aoc, &nVisualWidth);
        memcpy(buff, mux_scratch, nLen+1);
        *bufc = buff + nLen;
    }

    *dstr = pdstr;

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
    const char *funcname,
    char *preserve[],
    size_t preserve_len[]
)
{
    UNUSED_PARAMETER(funcname);

    int i;

    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (mudstate.global_regs[i])
        {
            preserve[i] = alloc_lbuf(funcname);
            size_t n = mudstate.glob_reg_len[i];
            memcpy(preserve[i], mudstate.global_regs[i], n);
            preserve[i][n] = '\0';
            preserve_len[i] = n;
        }
        else
        {
            preserve[i] = NULL;
            preserve_len[i] = 0;
        }
    }
}

void save_and_clear_global_regs
(
    const char *funcname,
    char *preserve[],
    size_t preserve_len[]
)
{
    UNUSED_PARAMETER(funcname);

    int i;

    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        preserve[i] = mudstate.global_regs[i];
        preserve_len[i] = mudstate.glob_reg_len[i];

        mudstate.global_regs[i] = NULL;
        mudstate.glob_reg_len[i] = 0;
    }
}

void restore_global_regs
(
    const char *funcname,
    char *preserve[],
    size_t preserve_len[]
)
{
    UNUSED_PARAMETER(funcname);

    int i;

    for (i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (preserve[i])
        {
            if (mudstate.global_regs[i])
            {
                free_lbuf(mudstate.global_regs[i]);
            }
            mudstate.global_regs[i] = preserve[i];
            mudstate.glob_reg_len[i] = preserve_len[i];
        }
        else
        {
            if (mudstate.global_regs[i])
            {
                mudstate.global_regs[i][0] = '\0';
            }
            mudstate.glob_reg_len[i] = 0;
        }
    }
}
