/*! \file wild.cpp
 * \brief Wildcard routines.
 *
 * Written by T. Alexander Popiel, 24 June 1993
 * Last modified by T. Alexander Popiel, 19 August 1993
 *
 * Thanks go to Andrew Molitor for debugging
 * Thanks also go to Rich $alz for code to benchmark against
 *
 * Copyright (c) 1993 by T. Alexander Popiel
 * This code is hereby placed under GNU copyleft,
 * see copyright.h for details.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// The pattern is pre-lowercased by the caller (mux_strlwr, Unicode-aware) at the
// entry points.  Both matchers — quick_wild_impl() (non-capturing) and wild1()
// (capturing) — fold the data side per character too, via wild_lit_eq() →
// mux_tolower(), so wildcard matching is fully Unicode case-insensitive from
// either side.  wild1() captures original-case bytes: only its literal
// comparisons fold; the '*'/'?' capture spans copy from the unmodified data.
// (Historically the data side folded only ASCII via mux_tolower_ascii, so e.g.
// strmatch(CAFÉ,café) and $café matching CAFÉ both failed — see
// docs/survey-wild-matching.md, #835/#836/#837.)
//
// Argument return space and size.
//
static UTF8 **arglist;
static int numargs;

// Length in bytes of the complete UTF-8 character at dstr, or 0 if dstr is at
// end-of-string or points at a malformed / truncated character.  A single '?'
// matches exactly one such character.  This mirrors the validation wild1()
// already performs for its capturing '?' case, so the non-capturing matcher
// (quick_wild_impl, used by strmatch/quick_wild/wild_match) agrees with it and
// with strlen() on multibyte data — historically quick_wild_impl advanced '?'
// by a single byte, so e.g. strmatch(café,????) wrongly failed (5 bytes != 4).
//
static size_t wild_char_len(const UTF8 *dstr)
{
    if ('\0' == dstr[0])
    {
        return 0;
    }
    size_t t = utf8_FirstByte[*dstr];
    if (UTF8_CONTINUE <= t)
    {
        return 0;
    }
    for (size_t j = 1; j < t; j++)
    {
        if (  '\0' == dstr[j]
           || UTF8_CONTINUE != utf8_FirstByte[dstr[j]])
        {
            return 0;
        }
    }
    return t;
}

// Case-insensitively match the data character at dstr against the literal
// character at tstr.  The pattern (tstr) is already lowercased by the caller
// (mux_strlwr at the entry points), so the data character is folded on the fly
// with mux_tolower() — the same Unicode mapping — and the folded bytes are
// compared against the pattern.  This makes non-ASCII letters (É/é, Ñ/ñ, …)
// match case-insensitively from EITHER side, where the old byte-wise EQUAL only
// folded ASCII in the data and so e.g. strmatch(CAFÉ,café) failed.
//
// On a match, returns true and sets *p_dlen / *p_tlen to the bytes to advance
// in the data and pattern respectively; these differ when a fold changes byte
// count.  At end-of-data (*dstr == '\0') it matches only the pattern's NUL (the
// ASCII path folds '\0' to '\0'), which callers treat as "strings both ended".
//
static bool wild_lit_eq(const UTF8 *tstr, const UTF8 *dstr,
                        size_t *p_dlen, size_t *p_tlen)
{
    // ASCII fast path — the overwhelming common case.  tstr is pre-lowercased.
    //
    unsigned char dc = static_cast<unsigned char>(*dstr);
    if (dc < 0x80)
    {
        if (mux_tolower_ascii(dc) != static_cast<unsigned char>(*tstr))
        {
            return false;
        }
        *p_dlen = 1;
        *p_tlen = 1;
        return true;
    }

    // Multibyte data character: validate and fold it.
    //
    size_t dlen = wild_char_len(dstr);
    if (0 == dlen)
    {
        return false;
    }

    bool bXor;
    const string_desc *d = mux_tolower(dstr, bXor);

    if (  nullptr != d
       && bXor
       && d->n_bytes == dlen)
    {
        // XOR transform (e.g. Latin-1 É→é): the folded byte is the original
        // byte XORed with the mask at d->p.  Compare against the pattern.
        //
        for (size_t i = 0; i < dlen; i++)
        {
            if (  static_cast<unsigned char>(tstr[i])
               != (static_cast<unsigned char>(dstr[i]) ^ d->p[i]))
            {
                return false;
            }
        }
        *p_dlen = dlen;
        *p_tlen = dlen;
        return true;
    }

    // Table transform with the folded bytes at d->p (may change byte count), or
    // no fold available (d == nullptr, or a byte-count-changing XOR we don't
    // apply) — compare the folded (or original) bytes against the pattern.
    //
    const UTF8 *fold = (nullptr != d && !bXor) ? d->p       : dstr;
    size_t      flen = (nullptr != d && !bXor) ? d->n_bytes : dlen;

    // Compare folded data bytes to the pattern.  Stops at the first mismatch,
    // including the pattern's NUL terminator, so it never reads past it.
    //
    for (size_t i = 0; i < flen; i++)
    {
        if (static_cast<unsigned char>(tstr[i]) != fold[i])
        {
            return false;
        }
    }
    *p_dlen = dlen;
    *p_tlen = flen;
    return true;
}

//
// ---------------------------------------------------------------------------
// quick_wild_impl: INTERNAL: do a wildcard match, without remembering the
// wild data.  The pattern (tstr) must already be lowercased by the caller.
//
// This routine will cause crashes if fed nullptrs instead of strings.
//
static bool quick_wild_impl(const UTF8 *tstr, const UTF8 *dstr)
{
    if (mudstate.wild_invk_ctr >= mudconf.wild_invk_lim)
    {
        return false;
    }
    mudstate.wild_invk_ctr++;

    while (*tstr != '*')
    {
        if ('?' == *tstr)
        {
            // Single character match: consume one whole UTF-8 character.
            // Return false at end of data or on a malformed character.
            //
            size_t t = wild_char_len(dstr);
            if (0 == t)
            {
                return false;
            }
            dstr += t;
            tstr++;
            continue;
        }

        if ('\\' == *tstr)
        {
            // Escape character.  Move up, and force literal match of next
            // character.
            //
            tstr++;
        }

        // Literal character.  Check for a (case-insensitive, whole-character)
        // match.  If matching end of data, return true.
        //
        size_t dlen, tlen;
        if (!wild_lit_eq(tstr, dstr, &dlen, &tlen))
        {
            return false;
        }
        if (!*dstr)
        {
            return true;
        }
        tstr += tlen;
        dstr += dlen;
    }

    // Skip over '*'.
    //
    tstr++;

    // Return true on trailing '*'.
    //
    if (!*tstr)
    {
        return true;
    }

    // Skip over wildcards.
    //
    while (  *tstr == '?'
          || *tstr == '*')
    {
        if (*tstr == '?')
        {
            // Each '?' consumes one whole UTF-8 character.
            //
            size_t t = wild_char_len(dstr);
            if (0 == t)
            {
                return false;
            }
            dstr += t;
        }
        tstr++;
    }

    // Skip over a backslash in the pattern string if it is there.
    //
    if (*tstr == '\\')
    {
        tstr++;
    }

    // Return true on trailing '*'.
    //
    if (!*tstr)
    {
        return true;
    }

    // Scan for possible matches.  Advance one whole UTF-8 character at a time
    // so a multibyte literal anchor is only tested on character boundaries.
    //
    while (*dstr)
    {
        size_t dlen, tlen;
        if (  wild_lit_eq(tstr, dstr, &dlen, &tlen)
           && quick_wild_impl(tstr + tlen, dstr + dlen))
        {
            return true;
        }
        size_t t = wild_char_len(dstr);
        dstr += (0 != t) ? t : 1;
    }
    return false;
}

// ---------------------------------------------------------------------------
// wild1: INTERNAL: do a wildcard match, remembering the wild data.
//
// DO NOT CALL THIS FUNCTION DIRECTLY - DOING SO MAY RESULT IN SERVER CRASHES
// AND IMPROPER ARGUMENT RETURN.
//
// Side Effect: this routine modifies the 'arglist' static global variable.
//
static bool wild1(UTF8 *tstr, UTF8 *dstr, int arg)
{
    if (mudstate.wild_invk_ctr >= mudconf.wild_invk_lim)
    {
        return false;
    }
    mudstate.wild_invk_ctr++;

    UTF8 *datapos;
    int argpos, numextra;

    while (*tstr != '*')
    {
        if ('?' == *tstr)
        {
            // Single character match: capture one whole UTF-8 character.
            // Return false at end of data or on a malformed character.
            //
            size_t t = wild_char_len(dstr);
            if (0 == t)
            {
                return false;
            }

            memcpy(arglist[arg], dstr, t);
            arglist[arg][t] = '\0';
            arg++;

            // Jump to the fast routine if we can.
            //
            if (arg >= numargs)
            {
                return quick_wild_impl(tstr + 1, dstr + t);
            }
            dstr += t;
            tstr++;
            continue;
        }

        if ('\\' == *tstr)
        {
            // Escape character.  Move up, and force literal match of next
            // character.
            //
            tstr++;
        }

        // Literal character.  Check for a (case-insensitive, whole-character)
        // match.  If matching end of data, return true.
        //
        size_t dlen, tlen;
        if (!wild_lit_eq(tstr, dstr, &dlen, &tlen))
        {
            return false;
        }
        if (!*dstr)
        {
            return true;
        }
        dstr += dlen;
        tstr += tlen;
    }

    // If at end of pattern, slurp the rest, and leave.
    //
    if (!tstr[1])
    {
        mux_strncpy(arglist[arg], dstr, LBUF_SIZE-1);
        return true;
    }

    // Remember current position for filling in the '*' return.
    //
    datapos = dstr;
    argpos = arg;

    // Scan forward until we find a non-wildcard.
    //
    do
    {
        if (argpos < arg)
        {
            // Fill in arguments if someone put another '*' before a fixed
            // string.
            //
            arglist[argpos][0] = '\0';
            argpos++;

            // Jump to the fast routine if we can.
            //
            if (argpos >= numargs)
            {
                return quick_wild_impl(tstr, dstr);
            }

            // Fill in any intervening '?'s
            //
            while (argpos < arg)
            {
                arglist[argpos][0] = *datapos;
                arglist[argpos][1] = '\0';
                datapos++;
                argpos++;

                // Jump to the fast routine if we can.
                //
                if (argpos >= numargs)
                {
                    return quick_wild_impl(tstr, dstr);
                }
            }
        }

        // Skip over the '*' for now...
        //
        tstr++;
        arg++;

        // Skip over '?'s for now...
        //
        numextra = 0;
        while (*tstr == '?')
        {
            if (!*dstr)
            {
                return false;
            }
            tstr++;
            dstr++;
            arg++;
            numextra++;
        }
    } while (*tstr == '*');

    // Skip over a backslash in the pattern string if it is there.
    //
    if (*tstr == '\\')
    {
        tstr++;
    }

    // Check for possible matches.  This loop terminates either at end of data
    // (resulting in failure), or at a successful match.
    //
    size_t anchor_dlen = 0, anchor_tlen = 0;
    for (;;)
    {
        // Scan forward until the literal anchor matches (case-insensitively, on
        // whole-character boundaries).
        //
        if (*tstr)
        {
            while (!wild_lit_eq(tstr, dstr, &anchor_dlen, &anchor_tlen))
            {
                if (!*dstr)
                {
                    return false;
                }
                size_t t = wild_char_len(dstr);
                dstr += (0 != t) ? t : 1;
            }
        }
        else
        {
            while (*dstr)
            {
                dstr++;
            }
        }

        // The anchor matches, now.  Check if the rest does, using the fastest
        // method, as usual.
        //
        if (  !*dstr
           || ((arg < numargs) ? wild1(tstr + anchor_tlen, dstr + anchor_dlen, arg)
                               : quick_wild_impl(tstr + anchor_tlen, dstr + anchor_dlen)))
        {
            // Found a match!  Fill in all remaining arguments. First do the
            // '*'...
            //
            mux_strncpy(arglist[argpos], datapos, (dstr - datapos) - numextra);
            datapos = dstr - numextra;
            argpos++;

            // Fill in any trailing '?'s that are left.
            //
            while (numextra)
            {
                if (argpos >= numargs)
                {
                    return true;
                }
                arglist[argpos][0] = *datapos;
                arglist[argpos][1] = '\0';
                datapos++;
                argpos++;
                numextra--;
            }

            // It's done!
            //
            return true;
        }
        else
        {
            // Anchor matched but the rest didn't — advance past this whole
            // data character and keep scanning.
            //
            size_t t = wild_char_len(dstr);
            dstr += (0 != t) ? t : 1;
        }
    }
}

// ---------------------------------------------------------------------------
// wild: do a wildcard match, remembering the wild data.
//
// This routine will cause crashes if fed nullptrs instead of strings.
//
// Side Effect: this routine modifies the 'arglist' and 'numargs' static
// global variables.
//
bool wild(UTF8 *tstr, UTF8 *dstr, UTF8 *args[], int nargs)
{
    mudstate.wild_invk_ctr = 0;

    // Pre-lowercase the pattern for case-insensitive matching (Unicode-aware).
    //
    size_t nLower;
    UTF8 *pLower = mux_strlwr(tstr, nLower);
    LBuf LoweredPattern = LBuf_Src("wild1");
    if (nLower >= LBUF_SIZE) nLower = LBUF_SIZE - 1;
    memcpy(LoweredPattern, pLower, nLower);
    LoweredPattern[nLower] = '\0';
    UTF8 *lt = LoweredPattern;

    int i;
    UTF8 *scan;

    // Initialize the return array.
    //
    for (i = 0; i < nargs; i++)
    {
        args[i] = nullptr;
    }

    // Do fast match.
    //
    while (  *lt != '*'
          && *lt != '?')
    {
        if (*lt == '\\')
        {
            lt++;
        }
        size_t dlen, tlen;
        if (!wild_lit_eq(lt, dstr, &dlen, &tlen))
        {
            return false;
        }
        if (!*dstr)
        {
            return true;
        }
        lt += tlen;
        dstr += dlen;
    }

    // Allocate space for the return args.
    //
    i = 0;
    scan = lt;
    while (  *scan
          && i < nargs)
    {
        switch (*scan)
        {
        case '?':

            args[i] = alloc_lbuf("wild.?");
            i++;
            break;

        case '*':

            args[i] = alloc_lbuf("wild.*");
            i++;
        }
        scan++;
    }

    // Put stuff in globals for quick recursion.
    //
    arglist = args;
    numargs = nargs;

    // Do the match.
    //
    bool value = nargs ? wild1(lt, dstr, 0) : quick_wild_impl(lt, dstr);

    // Clean out any fake match data left by wild1.
    //
    for (i = 0; i < nargs; i++)
    {
        if (  args[i] != nullptr
           && (  !*args[i]
              || !value))
        {
            free_lbuf(args[i]);
            args[i] = nullptr;
        }
    }
    return value;
}

// ---------------------------------------------------------------------------
// wild_match: do either an order comparison or a wildcard match, remembering
// the wild data, if wildcard match is done.
//
// This routine will cause crashes if fed nullptrs instead of strings.
//
bool wild_match(UTF8 *tstr, const UTF8 *dstr)
{
    PARSE_FLOAT_RESULT pfr;
    UTF8 ch = *tstr;
    if (  '>' == ch
       || '<' == ch)
    {
        tstr++;
        if (  ParseFloat(&pfr, dstr, true)
           && ParseFloat(&pfr, tstr, true))
        {
            double dd = mux_atof(dstr);
            double dt = mux_atof(tstr);
            if ('<' == ch)
            {
                return (dd < dt);
            }
            else
            {
                return (dd > dt);
            }
        }
        else if ('<' == ch)
        {
            return (strcmp(reinterpret_cast<const char *>(dstr), reinterpret_cast<const char *>(tstr)) < 0);
        }
        else // if ('>' == ch)
        {
            return (strcmp(reinterpret_cast<const char *>(dstr), reinterpret_cast<const char *>(tstr)) > 0);
        }
    }

    // Pre-lowercase the pattern for case-insensitive matching (Unicode-aware).
    //
    size_t nLower;
    UTF8 *pLower = mux_strlwr(tstr, nLower);
    LBuf LoweredPattern = LBuf_Src("wild_match");
    if (nLower >= LBUF_SIZE) nLower = LBUF_SIZE - 1;
    memcpy(LoweredPattern, pLower, nLower);
    LoweredPattern[nLower] = '\0';

    mudstate.wild_invk_ctr = 0;
    return quick_wild_impl(LoweredPattern, dstr);
}

// ---------------------------------------------------------------------------
// quick_wild: Public entry point.  Pre-lowercases the pattern for
// Unicode-aware case-insensitive matching, then delegates to quick_wild_impl.
//
bool quick_wild(const UTF8 *tstr, const UTF8 *dstr)
{
    size_t nLower;
    UTF8 *pLower = mux_strlwr(tstr, nLower);
    LBuf LoweredPattern = LBuf_Src("quick_wild");
    if (nLower >= LBUF_SIZE) nLower = LBUF_SIZE - 1;
    memcpy(LoweredPattern, pLower, nLower);
    LoweredPattern[nLower] = '\0';

    return quick_wild_impl(LoweredPattern, dstr);
}
