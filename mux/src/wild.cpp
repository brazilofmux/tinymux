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

#include "mathutil.h"

#define EQUAL(a,b) (mux_tolower_ascii(a) == mux_tolower_ascii(b))
#define NOTEQUAL(a,b) (mux_tolower_ascii(a) != mux_tolower_ascii(b))

// Argument return space and size.
//
static UTF8 **arglist;
static int numargs;

//
// ---------------------------------------------------------------------------
// quick_wild: do a wildcard match, without remembering the wild data.
//
// This routine will cause crashes if fed nullptrs instead of strings.
//
bool quick_wild(const UTF8 *tstr, const UTF8 *dstr)
{
    if (mudstate.wild_invk_ctr >= mudconf.wild_invk_lim)
    {
        return false;
    }
    mudstate.wild_invk_ctr++;

    while (*tstr != '*')
    {
        switch (*tstr)
        {
        case '?':

            // Single character match.  Return false if at end of data.
            //
            if (!*dstr)
            {
                return false;
            }
            break;

        case '\\':

            // Escape character.  Move up, and force literal match of next
            // character.
            //
            tstr++;

            // FALL THROUGH

        default:

            // Literal character.  Check for a match. If matching end of data,
            // return true.
            //
            if (NOTEQUAL(*dstr, *tstr))
            {
                return false;
            }
            if (!*dstr)
            {
                return true;
            }
        }
        tstr++;
        dstr++;
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
            if (!*dstr)
            {
                return false;
            }
            dstr++;
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

    // Scan for possible matches.
    //
    while (*dstr)
    {
        if (  EQUAL(*dstr, *tstr)
           && quick_wild(tstr + 1, dstr + 1))
        {
            return true;
        }
        dstr++;
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
        switch (*tstr)
        {
        case '?':

            // Single character match.  Return false if at end of data.
            //
            size_t t;
            if (  '\0' == dstr[0]
               || UTF8_CONTINUE <= (t = utf8_FirstByte[*dstr]))
            {
                return false;
            }

            size_t j;
            for (j = 1; j < t; j++)
            {
                if (  '\0' == dstr[j]
                   || UTF8_CONTINUE != utf8_FirstByte[dstr[j]])
                {
                    return false;
                }
            }

            memcpy(arglist[arg], dstr, t);
            arglist[arg][t] = '\0';
            arg++;

            // Jump to the fast routine if we can.
            //
            if (arg >= numargs)
            {
                return quick_wild(tstr + 1, dstr + t);
            }
            dstr += t;
            break;

        case '\\':

            // Escape character.  Move up, and force literal match of next
            // character.
            //
            tstr++;

            // FALL THROUGH

        default:

            // Literal character.  Check for a match. If matching end of data,
            // return true.
            //
            if (NOTEQUAL(*dstr, *tstr))
            {
                return false;
            }
            if (!*dstr)
            {
                return true;
            }
            dstr++;
            break;
        }
        tstr++;
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
                return quick_wild(tstr, dstr);
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
                    return quick_wild(tstr, dstr);
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
    for (;;)
    {
        // Scan forward until first character matches.
        //
        if (*tstr)
        {
            while (NOTEQUAL(*dstr, *tstr))
            {
                if (!*dstr)
                {
                    return false;
                }
                dstr++;
            }
        }
        else
        {
            while (*dstr)
            {
                dstr++;
            }
        }

        // The first character matches, now.  Check if the rest does, using
        // the fastest method, as usual.
        //
        if (  !*dstr
           || ((arg < numargs) ? wild1(tstr + 1, dstr + 1, arg)
                               : quick_wild(tstr + 1, dstr + 1)))
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
            dstr++;
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
    while (  *tstr != '*'
          && *tstr != '?')
    {
        if (*tstr == '\\')
        {
            tstr++;
        }
        if (NOTEQUAL(*dstr, *tstr))
        {
            return false;
        }
        if (!*dstr)
        {
            return true;
        }
        tstr++;
        dstr++;
    }

    // Allocate space for the return args.
    //
    i = 0;
    scan = tstr;
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
    bool value = nargs ? wild1(tstr, dstr, 0) : quick_wild(tstr, dstr);

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
            return (strcmp((char *)dstr, (char *)tstr) < 0);
        }
        else // if ('>' == ch)
        {
            return (strcmp((char *)dstr, (char *)tstr) > 0);
        }
    }
    mudstate.wild_invk_ctr = 0;
    return quick_wild(tstr, dstr);
}
