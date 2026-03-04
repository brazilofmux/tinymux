/*! \file funceval2.cpp
 * \brief MUX function handlers.
 *
 * This file began as a place to put function handlers ported from other
 * MU* servers, but has also become home to miscellaneous new functions.
 * These handlers include side-effect functions, comsys / mail functions,
 * ansi functions, zone functions, encrypt / decrypt, random functions,
 * some text-formatting and list-munging functions, deprecated stack
 * functions, regexp functions, etc.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/* ---------------------------------------------------------------------------
 * fun_grab: a combination of extract() and match(), sortof. We grab the
 *           single element that we match.
 *
 *  grab(Test:1 Ack:2 Foof:3,*:2)    => Ack:2
 *  grab(Test-1+Ack-2+Foof-3,*o*,+)  => Foof-3
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_grab)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Walk the wordstring, until we find the word we want.
    //
    UTF8 *s = trim_space_sep(fargs[0], sep);
    do
    {
        UTF8 *r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            safe_str(r, buff, bufc);
            return;
        }
    } while (s);
}

FUNCTION(fun_graball)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    bool bFirst = true;
    UTF8 *s = trim_space_sep(fargs[0], sep);
    do
    {
        UTF8 *r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                bFirst = false;
            }
            safe_str(r, buff, bufc);
        }
    } while (s);
}

/* ---------------------------------------------------------------------------
 * fun_scramble:  randomizes the letters in a string.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_scramble)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    LBUF_OFFSET nPoints = sStr->length_cursor().m_point;

    if (2 <= nPoints)
    {
        mux_string *sOut = nullptr;
        try
        {
            sOut = new mux_string;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == sOut)
        {
            delete sStr;
            return;
        }

        LBUF_OFFSET iPoint;
        mux_cursor iStart, iEnd;
        while (0 < nPoints)
        {
            iPoint = static_cast<LBUF_OFFSET>(RandomINT32(0, static_cast<int32_t>(nPoints-1)));
            sStr->cursor_from_point(iStart, iPoint);
            sStr->cursor_from_point(iEnd, iPoint + 1);
            sOut->append(*sStr, iStart, iEnd);
            sStr->delete_Chars(iStart, iEnd);
            nPoints--;
        }
        *bufc += sOut->export_TextColor(*bufc, CursorMin, CursorMax, buff + (LBUF_SIZE-1) - *bufc);
        delete sOut;
    }
    else
    {
        safe_str(fargs[0], buff, bufc);
    }

    delete sStr;
}

/* ---------------------------------------------------------------------------
 * fun_shuffle: randomize order of words in a list.
 * Borrowed from PennMUSH 1.50
 */
FUNCTION(fun_shuffle)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    mux_string *sIn = nullptr;
    try
    {
        sIn = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sIn)
    {
        return;
    }

    mux_words *words = nullptr;
    try
    {
        words = new mux_words(*sIn);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == words)
    {
        delete sIn;
        return;
    }

    LBUF_OFFSET n = words->find_Words(sep.str);
    mux_string *sOut = nullptr;
    try
    {
        sOut = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sOut)
    {
        delete sIn;
        delete words;
        return;
    }

    bool bFirst = true;
    LBUF_OFFSET i = 0;
    mux_cursor iStart = CursorMin, iEnd = CursorMin;

    while (n > 0)
    {
        if (bFirst)
        {
            bFirst = false;
        }
        else
        {
            sOut->append(osep.str, osep.n);
        }
        i = static_cast<LBUF_OFFSET>(RandomINT32(0, static_cast<int32_t>(n-1)));
        iStart = words->wordBegin(i);
        iEnd = words->wordEnd(i);
        sOut->append(*sIn, iStart, iEnd);
        words->ignore_Word(i);
        n--;
    }
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sOut->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete words;
    delete sIn;
    delete sOut;
}

// pickrand -- choose a random item from a list.
//
FUNCTION(fun_pickrand)
{
    SEP sep;
    if (  nfargs == 0
       || fargs[0][0] == '\0'
       || !OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *s = trim_space_sep(fargs[0], sep);
    if (s[0] == '\0')
    {
        return;
    }

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(s);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr != sStr
       && nullptr != words)
    {
        int32_t n = static_cast<int32_t>(words->find_Words(sep.str));

        if (0 < n)
        {
            LBUF_OFFSET w = static_cast<LBUF_OFFSET>(RandomINT32(0, n-1));
            words->export_WordColor(w, buff, bufc);
        }
    }
    delete sStr;
    delete words;
}

// sortby()
//
typedef struct
{
    UTF8  *buff;;
    dbref executor;
    dbref caller;
    dbref enactor;
    int   aflags;
} ucomp_context;

static int u_comp(ucomp_context *pctx, const void *s1, const void *s2)
{
    if (  mudstate.func_invk_ctr > mudconf.func_invk_lim
       || mudstate.func_nest_lev > mudconf.func_nest_lim
       || alarm_clock.alarmed)
    {
        return 0;
    }

    const UTF8 *elems[2] = { T(s1), T(s2) };

    UTF8 *tbuf = alloc_lbuf("u_comp");
    mux_strncpy(tbuf, pctx->buff, LBUF_SIZE-1);
    UTF8 *result = alloc_lbuf("u_comp");
    UTF8 *bp = result;
    mux_exec(tbuf, LBUF_SIZE-1, result, &bp, pctx->executor, pctx->caller, pctx->enactor,
             AttrTrace(pctx->aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), elems, 2);
    *bp = '\0';
    int n = mux_atol(result);
    free_lbuf(result);
    free_lbuf(tbuf);
    return n;
}

inline int ucomp_bsearch(ucomp_context* pctx, void* arr[], int sz, void* ndl)
{
    int l = 0;
    int r = sz;
    while (l < r)
    {
        int m = (l + r) >> 1;
        if (m == sz)
        {
            return sz;
        }

        if (u_comp(pctx, ndl, arr[m]) < 0)
        {
            r = m;
        }
        else
        {
            l = m + 1;
        }
    }
    return l;
}

static void mincomp_sort(ucomp_context* pctx, void* arr[], int sz)
{
    if (sz <= 1)
    {
        return;
    }

    for (int i = 1; i < sz; i++)
    {
        void* t = arr[i];
        int n = ucomp_bsearch(pctx, arr, i, t);
        for (int j = i; j > n; j--)
        {
            arr[j] = arr[j-1];
        }
        arr[n] = t;
    }
}

FUNCTION(fun_sortby)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    ucomp_context ctx;
    ctx.buff = alloc_lbuf("fun_sortby.ctx");
    mux_strncpy(ctx.buff, atext, LBUF_SIZE-1);
    ctx.executor = thing;
    ctx.caller   = executor;
    ctx.enactor  = enactor;
    ctx.aflags   = aflags;

    UTF8 *list = alloc_lbuf("fun_sortby");
    mux_strncpy(list, fargs[1], LBUF_SIZE-1);
    std::vector<UTF8*> ptrs(LBUF_SIZE / 2);
    const int nptrs = list2arr(ptrs.data(), LBUF_SIZE / 2, list, sep);

    if (nptrs > 1)
    {
        mincomp_sort(&ctx, reinterpret_cast<void**>(ptrs.data()), nptrs);
    }

    arr2list(ptrs.data(), nptrs, buff, bufc, osep);
    free_lbuf(list);
    free_lbuf(ctx.buff);
    free_lbuf(atext);
}

// fun_last: Returns last word in a string. Borrowed from TinyMUSH 2.2.
//
FUNCTION(fun_last)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr != sStr
       && nullptr != words)
    {
        LBUF_OFFSET nWords = words->find_Words(sep.str);
        words->export_WordColor(nWords-1, buff, bufc);
    }
    delete sStr;
    delete words;
}

/*
 * ---------------------------------------------------------------------------
 * fun_lrest: Returns all but the last word in a string.
 */

FUNCTION(fun_lrest)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr != sStr
       && nullptr != words)
    {
        LBUF_OFFSET nWords = words->find_Words(sep.str);
        if (nWords > 1)
        {
            words->export_WordColor(0, buff, bufc);
            for (LBUF_OFFSET i = 1; i < nWords - 1; i++)
            {
                print_sep(sep, buff, bufc);
                words->export_WordColor(i, buff, bufc);
            }
        }
    }
    delete sStr;
    delete words;
}

// For an named object, or the executor, find the last created object
// (optionally qualified by type).
//
FUNCTION(fun_lastcreate)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(ncargs);
    UNUSED_PARAMETER(cargs);

    // Determine the target by name, or use the executor if no name is given.
    //
    dbref target = executor;
    if (  0 < nfargs
       && '\0' != fargs[0][0])
    {
        target = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(target))
        {
            safe_nomatch(buff, bufc);
            return;
        }

        // Verify that the executor has access to the named object.  Notice
        // that an executor always has access to itself.
        //
        if (  !WizRoy(executor)
           && !Controls(executor, target))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }

    // If a type is given, qualify the result.
    //
    int iObjectPosition = 4;
    if (  1 < nfargs
       && '\0' != fargs[1][0])
    {
        switch (fargs[1][0])
        {
        case 'R':
        case 'r':
            iObjectPosition = 0;
            break;

        case 'T':
        case 't':
            iObjectPosition = 1;
            break;

        case 'E':
        case 'e':
            iObjectPosition = 2;
            break;

        case 'P':
        case 'p':
            iObjectPosition = 3;
            break;
        }
    }

    int aowner;
    int aflags;

    UTF8* newobject_string = atr_get("fun_lastcreate.2998", target,
            A_NEWOBJS, &aowner, &aflags);

    if (  nullptr == newobject_string
       || '\0' == newobject_string[0])
    {
        safe_str(T("#-1"), buff, bufc);
        return;
    }

    string_token st(newobject_string, T(" "));

    int i;
    UTF8* ptr;
    for ( ptr = st.parse(), i = 0;
          nullptr != ptr && i < 5;
          ptr = st.parse(), i++)
    {
        if (i == iObjectPosition)
        {
            dbref jLastCreated = mux_atol(ptr);
            safe_tprintf_str(buff, bufc, T("#%d"), jLastCreated);
            break;
        }
    }
    free_lbuf(newobject_string);
}

// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_matchall)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    int wcount;
    UTF8 *r, *s, *old, tbuf[I32BUF_SIZE];
    old = *bufc;

    // Check each word individually, returning the word number of all that
    // match. If none match, return 0.
    //
    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do
    {
        r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            mux_ltoa(wcount, tbuf);
            if (old != *bufc)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_str(tbuf, buff, bufc);
        }
        wcount++;
    } while (s);

    if (*bufc == old)
    {
        safe_chr('0', buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_ports: Returns a list of ports for a user.
// Borrowed from TinyMUSH 2.2
//
FUNCTION(fun_ports)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        if (target == executor || Wizard(executor))
        {
            if (Connected(target))
            {
                make_portlist(executor, target, buff, bufc);
            }
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_mix: Like map, but operates on up to ten lists simultaneously, passing
 * the elements as %0 - %10.
 * Borrowed from PennMUSH 1.50, upgraded by RhostMUSH.
 */
FUNCTION(fun_mix)
{
    // Check to see if we have an appropriate number of arguments.
    // If there are more than three arguments, the last argument is
    // ALWAYS assumed to be a delimiter.
    //
    SEP sep;
    int lastn;

    if (nfargs < 4)
    {
        sep.n = 1;
        sep.str[0] = ' ';
        sep.str[1] = '\0';
        lastn = nfargs - 1;
    }
    else if (!OPTIONAL_DELIM(nfargs, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    else
    {
        lastn = nfargs - 2;
    }

    // Get the attribute. Check the permissions.
    //
    dbref thing;
    UTF8 *atext;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Process the lists, one element at a time.
    //
    int i;
    int nwords = 0;
    UTF8 *cp[NUM_ENV_VARS];
    for (i = 0; i < lastn; i++)
    {
        cp[i] = trim_space_sep(fargs[i+1], sep);
        int twords = countwords(cp[i], sep);
        if (nwords < twords)
        {
            nwords = twords;
        }
    }

    const UTF8 *os[NUM_ENV_VARS];
    bool bFirst = true;
    for (  int wc = 0;
           wc < nwords
        && mudstate.func_invk_ctr < mudconf.func_invk_lim
        && !alarm_clock.alarmed;
           wc++)
    {
        if (!bFirst)
        {
            print_sep(sep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }

        for (i = 0; i < lastn; i++)
        {
            os[i] = split_token(&cp[i], sep);
            if (nullptr == os[i])
            {
                os[i] = T("");
            }
        }
        mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            os, lastn);
    }
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_step: A little like a fusion of iter() and mix(), it takes elements
 * of a list X at a time and passes them into a single function as %0, %1,
 * etc.   step(<attribute>,<list>,<step size>,<delim>,<outdelim>)
 */

FUNCTION(fun_step)
{
    int i;

    SEP isep;
    if (!OPTIONAL_DELIM(4, isep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = isep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    int step_size = mux_atol(fargs[2]);
    if (  step_size < 1
       || NUM_ENV_VARS < step_size)
    {
        notify(executor, T("Illegal step size."));
        return;
    }

    // Get attribute. Check permissions.
    //
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    UTF8 *cp = trim_space_sep(fargs[1], isep);

    const UTF8 *os[NUM_ENV_VARS];
    bool bFirst = true;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        if (!bFirst)
        {
            print_sep(osep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }

        for (i = 0; cp && i < step_size; i++)
        {
            os[i] = split_token(&cp, isep);
        }
        mux_exec(atext, LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
             AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), os, i);
    }
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_foreach: like map(), but it operates on a string, rather than on a list,
 * calling a user-defined function for each character in the string.
 * No delimiter is inserted between the results.
 * Borrowed from TinyMUSH 2.2
 */
FUNCTION(fun_foreach)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  nfargs != 2
       && nfargs != 4)
    {
        safe_str(T("#-1 FUNCTION (FOREACH) EXPECTS 2 OR 4 ARGUMENTS"), buff, bufc);
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    UTF8 cbuf[5] = {'\0', '\0', '\0', '\0', '\0'};
    const UTF8 *bp = cbuf;
    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[1]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    sStr->trim();
    size_t nStr = sStr->length_byte();
    LBUF_OFFSET i = 0, nBytes = 0;

    if (  4 == nfargs
       && '\0' != fargs[2][0]
       && '\0' != fargs[3][0])
    {
        bool flag = false;
        UTF8 prev = '\0';

        while (  i < nStr
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !alarm_clock.alarmed)
        {
            nBytes = sStr->export_Char_UTF8(i, cbuf);
            i = i + nBytes;

            if (flag)
            {
                if (  cbuf[0] == *fargs[3]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = false;
                    continue;
                }
            }
            else
            {
                if (  cbuf[0] == *fargs[2]
                   && prev != '\\'
                   && prev != '%')
                {
                    flag = true;
                    continue;
                }
                else
                {
                    safe_copy_buf(cbuf, nBytes, buff, bufc);
                    continue;
                }
            }

            mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), &bp, 1);
            prev = cbuf[0];
        }
    }
    else
    {
        while (  i < nStr
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !alarm_clock.alarmed)
        {
            nBytes = sStr->export_Char_UTF8(i, cbuf);

            mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), &bp, 1);
            i = i + nBytes;
        }
    }
    free_lbuf(atext);
    delete sStr;
}

/* ---------------------------------------------------------------------------
 * fun_munge: combines two lists in an arbitrary manner.
 * Borrowed from TinyMUSH 2.2
 * Hash table rewrite by Ian and Alierak.
 */
#if LBUF_SIZE < UINT16_MAX
typedef uint16_t NHASH;
#define ShiftHash(x) (x) >>= 16
#else
typedef uint32_t NHASH;
#define ShiftHash(x)
#endif

typedef struct munge_htab_rec
{
    NHASH       nHash;         // partial hash value of this record's key
    LBUF_OFFSET iNext;         // index of next record in this hash chain
    LBUF_OFFSET nKeyOffset;    // offset of key string (incremented by 1),
                               //     zero indicates empty record.
    LBUF_OFFSET nValueOffset;  // offset of value string
} munge_htab_rec;

FUNCTION(fun_munge)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Find our object and attribute.
    //
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Copy list1 for later evaluation of the attribute.
    //
    UTF8 *list1 = alloc_lbuf("fun_munge.list1");
    mux_strncpy(list1, fargs[1], LBUF_SIZE-1);

    // Prepare data structures for a hash table that will map
    // elements of list1 to corresponding elements of list2.
    //
    int nWords = countwords(fargs[1], sep);
    if (0 == nWords)
    {
        free_lbuf(atext);
        free_lbuf(list1);
        return;
    }

    std::vector<munge_htab_rec> htab(1 + 2 * nWords);
    std::vector<uint16_t> tails(1 + nWords);

    int iNext = 1 + nWords;  // first unused hash slot past starting area

    // Chop up the lists, converting them into a hash table that
    // maps elements of list1 to corresponding elements of list2.
    //
    UTF8 *p1 = trim_space_sep(fargs[1], sep);
    UTF8 *p2 = trim_space_sep(fargs[2], sep);
    UTF8 *pKey, *pValue;
    for (pKey = split_token(&p1, sep), pValue = split_token(&p2, sep);
         nullptr != pKey && nullptr != pValue;
         pKey = split_token(&p1, sep), pValue = split_token(&p2, sep))
    {
        uint32_t nHash = munge_hash(pKey);
        int nHashSlot = 1 + (nHash % nWords);
        ShiftHash(nHash);

        if (0 != tails[nHashSlot])
        {
            // there is already a hash chain starting in this slot,
            // insert at the tail to preserve order.
            nHashSlot = tails[nHashSlot] =
                htab[tails[nHashSlot]].iNext = static_cast<LBUF_OFFSET>(iNext++);
        }
        else
        {
            tails[nHashSlot] = static_cast<LBUF_OFFSET>(nHashSlot);
        }

        htab[nHashSlot].nHash = static_cast<NHASH>(nHash);
        htab[nHashSlot].nKeyOffset = static_cast<LBUF_OFFSET>(1 + pKey - fargs[1]);
        htab[nHashSlot].nValueOffset = static_cast<LBUF_OFFSET>(pValue - fargs[2]);
    }
    if (  nullptr != pKey
       || nullptr != pValue)
    {
        safe_str(T("#-1 LISTS MUST BE OF EQUAL SIZE"), buff, bufc);
        free_lbuf(atext);
        free_lbuf(list1);
        return;
    }

    // Call the u-function with the first list as %0.
    //
    UTF8 *rlist, *bp;
    const UTF8 *uargs[2];

    bp = rlist = alloc_lbuf("fun_munge");
    uargs[0] = list1;
    uargs[1] = sep.str;
    mux_exec(atext, LBUF_SIZE-1, rlist, &bp, executor, caller, enactor,
             AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL), uargs, 2);
    *bp = '\0';
    free_lbuf(atext);
    free_lbuf(list1);

    // Now that we have our result, put it back into array form.
    // Translate its elements according to the mappings in our hash table.
    //
    bool bFirst = true;
    bp = trim_space_sep(rlist, sep);
    if ('\0' != *bp)
    {
        UTF8 *result;
        for (result = split_token(&bp, sep);
             nullptr != result;
             result = split_token(&bp, sep))
        {
            uint32_t nHash = munge_hash(result);
            int nHashSlot = 1 + (nHash % nWords);
            ShiftHash(nHash);

            while (  0 != htab[nHashSlot].nKeyOffset
                  && (  nHash != htab[nHashSlot].nHash
                     || 0 != strcmp(reinterpret_cast<char *>(result),
                                    reinterpret_cast<char *>(fargs[1] +
                                     htab[nHashSlot].nKeyOffset - 1))))
            {
                nHashSlot = htab[nHashSlot].iNext;
            }
            if (0 != htab[nHashSlot].nKeyOffset)
            {
                if (!bFirst)
                {
                    print_sep(sep, buff, bufc);
                }
                else
                {
                    bFirst = false;
                }
                safe_str(fargs[2] + htab[nHashSlot].nValueOffset, buff, bufc);
                // delete from the hash table
                memcpy(&htab[nHashSlot], &htab[htab[nHashSlot].iNext],
                       sizeof(munge_htab_rec));
            }
        }
    }
    free_lbuf(rlist);
}

FUNCTION(fun_die)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int n   = mux_atol(fargs[0]);
    int die = mux_atol(fargs[1]);

    if (  n == 0
       || die <= 0)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    if (  n < 1
       || LBUF_SIZE <= n)
    {
        safe_range(buff, bufc);
        return;
    }

    if (  3 <= nfargs
       && isTRUE(mux_atol(fargs[2])))
    {
        ITL pContext;
        ItemToList_Init(&pContext, buff, bufc);
        for (int count = 0; count < n; count++)
        {
            if (!ItemToList_AddInteger(&pContext, RandomINT32(1, die)))
            {
                break;
            }
        }
        ItemToList_Final(&pContext);
        return;
    }

    int total = 0;
    for (int count = 0; count < n; count++)
    {
        total += RandomINT32(1, die);
    }

    safe_ltoa(total, buff, bufc);
}

FUNCTION(fun_lrand)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int n_times = mux_atol(fargs[2]);
    if (n_times < 1)
    {
        return;
    }
    if (n_times > LBUF_SIZE)
    {
        n_times = LBUF_SIZE;
    }
    int32_t iLower = mux_atol(fargs[0]);
    int32_t iUpper = mux_atol(fargs[1]);

    if (iLower <= iUpper)
    {
        for (int i = 0; i < n_times-1; i++)
        {
            int32_t val = RandomINT32(iLower, iUpper);
            safe_ltoa(val, buff, bufc);
            print_sep(sep, buff, bufc);
        }
        int32_t val = RandomINT32(iLower, iUpper);
        safe_ltoa(val, buff, bufc);
    }
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Just returns the argument, literally.
    //
    safe_str(fargs[0], buff, bufc);
}

FUNCTION(fun_dumping)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

#if !defined(HAVE_WORKING_FORK)
    safe_chr('0', buff, bufc);
#else // HAVE_WORKING_FORK
    safe_bool(mudstate.dumping, buff, bufc);
#endif // HAVE_WORKING_FORK
}

static size_t mux_Pack0(int64_t val, int iRadix, UTF8 symbols[], UTF8 *buf)
{
    UTF8 *p = buf;

    // Handle sign.
    //
    if (val < 0)
    {
        *p++ = '-';
        val = -val;
    }

    UTF8 *q = p;
    while (val > iRadix-1)
    {
        int64_t iDiv  = val / iRadix;
        int64_t iTerm = val - iDiv * iRadix;
        val = iDiv;
        *p++ = symbols[iTerm];
    }
    *p++ = symbols[val];

    size_t nLength = p - buf;
    *p-- = '\0';

    // The digits are in reverse order with a possible leading '-'
    // if the value was negative. q points to the first digit,
    // and p points to the last digit.
    //
    while (q < p)
    {
        // Swap characters are *p and *q
        //
        char temp = *p;
        *p = *q;
        *q = temp;

        // Move p and first digit towards the middle.
        //
        --p;
        ++q;

        // Stop when we reach or pass the middle.
        //
    }
    return nLength;
}

// The following table contains 64 symbols, so this supports -a-
// radix-64 encoding. It is not however 'unix-to-unix' encoding.
// All of the following characters are valid for an attribute
// name, but not for the first character of an attribute name.
//
static UTF8 aRadix64[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz@$";

// These sets are for compatibility with PennMUSH.
//
static UTF8 aRadixPenn36[] =
    "0123456789abcdefghijklmnopqrstuvwxyz";

static UTF8 aRadixPenn64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static bool mux_Unpack(UTF8 *p, int64_t &val, int iRadixFrom, int iRadixTo, bool fPennBehavior)
{
    if (10 == iRadixFrom)
    {
        val = mux_atoi64(p);
        return true;
    }

    bool fPlusSlash = false;
    bool fNegativeNumbers = true;
    UTF8 *symbols = aRadix64;
    if (fPennBehavior)
    {
        fNegativeNumbers = iRadixFrom < 63 && iRadixTo < 63;
        if (iRadixFrom <= 36)
        {
            symbols = aRadixPenn36;
        }
        else if (iRadixFrom < 64)
        {
            symbols = aRadixPenn64;
        }
        else
        {
            fPlusSlash = true;
            symbols = aRadixPenn64;
        }
    }

    // Build Table of valid characters.
    //
    UTF8 MatchTable[256];
    memset(MatchTable, 0, sizeof(MatchTable));
    for (int i = 0; i < iRadixFrom; i++)
    {
        MatchTable[static_cast<unsigned char>(symbols[i])] = static_cast<UTF8>(i + 1);
    }

    if (fPlusSlash)
    {
        MatchTable[static_cast<unsigned char>('+')] = static_cast<UTF8>(62 + 1);
        MatchTable[static_cast<unsigned char>('/')] = static_cast<UTF8>(63 + 1);
    }

    // Leading whitespace
    //
    while (mux_isspace(*p))
    {
        p++;
    }

    // Possible sign
    //
    int LeadingCharacter = '\0';
    if (fNegativeNumbers)
    {
        LeadingCharacter = *p;
        if (  '-' == LeadingCharacter
           || '+' == LeadingCharacter)
        {
            p++;
        }
    }

    // Validate that string contains only characters from the subset of permitted characters.
    //
    int c;
    UTF8 *q = p;
    while (  '\0' != *q
          && !mux_isspace(*q))
    {
        c = *q;
        if (0 == MatchTable[static_cast<unsigned int>(c)])
        {
            return false;
        }
        q++;
    }

    // Verify trailing spaces.
    //
    while ('\0' != *q)
    {
        c = *q;
        if (!mux_isspace(c))
        {
            return false;
        }
        q++;
    }

    // Convert symbols
    //
    val = 0;
    c = *p++;
    for (int iValue = MatchTable[static_cast<unsigned int>(c)];
         iValue;
         iValue = MatchTable[static_cast<unsigned int>(c)])
    {
        val = iRadixFrom * val + iValue - 1;
        c = *p++;
    }

    // Interpret sign
    //
    if ('-' == LeadingCharacter)
    {
        val = -val;
    }
    return true;
}

static void mux_Pack1(int64_t val, int iRadixTo, bool fPennBehavior, UTF8 *buff, UTF8 **bufc)
{
    if (10 == iRadixTo)
    {
        safe_i64toa(val, buff, bufc);
    }
    else if (  val < 0
            && 63 <= iRadixTo)
    {
        safe_str(T("#-1 NEGATIVE NUMBER IS NOT REPRESENTABLE IN OUTPUT RADIX"), buff, bufc);
    }
    else
    {
        UTF8 *symbols = aRadix64;
        if (fPennBehavior)
        {
            if (iRadixTo <= 36)
            {
                symbols = aRadixPenn36;
            }
            else
            {
                symbols = aRadixPenn64;
            }
        }
        UTF8 TempBuffer[76]; // 1 '-', 63 binary digits, 1 '\0', 11 for safety.
        size_t nLength = mux_Pack0(val, iRadixTo, symbols, TempBuffer);
        safe_copy_buf(TempBuffer, nLength, buff, bufc);
    }
}

FUNCTION(fun_unpack)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate radix if present.
    //
    int iRadix = 64;
    if (2 <= nfargs)
    {
        if (  !is_integer(fargs[1], nullptr)
           || (iRadix = mux_atol(fargs[1])) < 2
           || 64 < iRadix)
        {
            safe_str(T("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
            return;
        }
    }

    bool fPennBehavior = false;
    if (3 <= nfargs)
    {
        fPennBehavior = xlate(fargs[2]);
    }

    int64_t val;
    if (!mux_Unpack(fargs[0], val, iRadix, 10, fPennBehavior))
    {
        safe_str(T("#-1 NUMBER IS NOT VALID FOR INPUT RADIX"), buff, bufc);
    }
    else
    {
        safe_i64toa(val, buff, bufc);
    }
}

FUNCTION(fun_pack)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate the arguments are numeric.
    //
    if (  !is_integer(fargs[0], nullptr)
       || (2 <= nfargs && !is_integer(fargs[1], nullptr)))
    {
        safe_str(T("#-1 ARGUMENTS MUST BE NUMBERS"), buff, bufc);
        return;
    }
    int64_t val = mux_atoi64(fargs[0]);

    // Validate that the radix is between 2 and 64.
    //
    int iRadix = 64;
    if (2 <= nfargs)
    {
        iRadix = mux_atol(fargs[1]);
        if (  iRadix < 2
           || 64 < iRadix)
        {
            safe_str(T("#-1 RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
            return;
        }
    }

    bool fPennBehavior = false;
    if (3 <= nfargs)
    {
        fPennBehavior = xlate(fargs[2]);
    }

    mux_Pack1(val, iRadix, fPennBehavior, buff, bufc);
}

FUNCTION(fun_baseconv)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate that input and output radix are integers.
    //
    if (  !is_integer(fargs[1], nullptr)
       || !is_integer(fargs[2], nullptr))
    {
        safe_str(T("#-1 ARGUMENTS MUST BE NUMBERS"), buff, bufc);
        return;
    }

    int iRadixFrom = mux_atol(fargs[1]);
    if (  iRadixFrom < 2
       || 64 < iRadixFrom)
    {
        safe_str(T("#-1 INPUT RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
        return;
    }
    int iRadixTo = mux_atol(fargs[2]);
    if (  iRadixTo < 2
       || 64 < iRadixTo)
    {
        safe_str(T("#-1 OUTPUT RADIX MUST BE A NUMBER BETWEEN 2 and 64"), buff, bufc);
        return;
    }

    int64_t val;
    if (!mux_Unpack(fargs[0], val, iRadixFrom, iRadixTo, true))
    {
        safe_str(T("#-1 NUMBER IS NOT VALID FOR INPUT RADIX"), buff, bufc);
    }
    else
    {
        mux_Pack1(val, iRadixTo, true, buff, bufc);
    }
}

FUNCTION(fun_strcat)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int i;
    for (i = 0; i < nfargs; i++)
    {
        safe_str(fargs[i], buff, bufc);
    }
}

// grep() and grepi() code borrowed from PennMUSH 1.50
//
static UTF8 *grep_util(dbref player, dbref thing, const UTF8 *pattern, const UTF8 *lookfor, size_t len, bool insensitive)
{
    // Returns a list of attributes which match <pattern> on <thing>
    // whose contents have <lookfor>.
    //
    olist_push();
    find_wild_attrs(player, thing, pattern, false, false, false);
    BMH_State bmhs;
    if (insensitive)
    {
        BMH_PrepareI(&bmhs, len, lookfor);
    }
    else
    {
        BMH_Prepare(&bmhs, len, lookfor);
    }

    UTF8 *tbuf1 = alloc_lbuf("grep_util");
    UTF8 *bp = tbuf1;

    dbref aowner;
    int aflags;
    for (int ca = olist_first(); ca != NOTHING && !alarm_clock.alarmed; ca = olist_next())
    {
        size_t nText;
        UTF8 *attrib = atr_get_LEN(thing, ca, &aowner, &aflags, &nText);
        size_t i;
        bool bSucceeded;
        if (insensitive)
        {
            bSucceeded = BMH_ExecuteI(&bmhs, &i, len, lookfor, nText, attrib);
        }
        else
        {
            bSucceeded = BMH_Execute(&bmhs, &i, len, lookfor, nText, attrib);
        }
        if (bSucceeded)
        {
            if (bp != tbuf1)
            {
                safe_chr(' ', tbuf1, &bp);
            }
            ATTR *ap = atr_num(ca);
            const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
            if (ap)
            {
                pName = ap->name;
            }
            safe_str(pName, tbuf1, &bp);
        }
        free_lbuf(attrib);
    }
    *bp = '\0';
    olist_pop();
    return tbuf1;
}

static void grep_handler(UTF8 *buff, UTF8 **bufc, dbref executor, UTF8 *fargs[],
                   bool bCaseInsens)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }

    if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Make sure there's an attribute and a pattern
    //
    if (!fargs[1] || !*fargs[1])
    {
        safe_str(T("#-1 NO SUCH ATTRIBUTE"), buff, bufc);
        return;
    }
    if (!fargs[2] || !*fargs[2])
    {
        safe_str(T("#-1 INVALID GREP PATTERN"), buff, bufc);
        return;
    }
    UTF8 *tp = grep_util(executor, it, fargs[1], fargs[2], strlen(reinterpret_cast<char *>(fargs[2])), bCaseInsens);
    safe_str(tp, buff, bufc);
    free_lbuf(tp);
}

FUNCTION(fun_grep)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    grep_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_grepi)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    grep_handler(buff, bufc, executor, fargs, true);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamax)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *amax = fargs[0];
    UTF8 buf_max[LBUF_SIZE];
    mux_strncpy(buf_max, strip_color(amax), LBUF_SIZE-1);
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp(reinterpret_cast<char *>(buf_max), reinterpret_cast<char *>(strip_color(fargs[i]))) < 0)
        {
            amax = fargs[i];
            mux_strncpy(buf_max, strip_color(amax), LBUF_SIZE-1);
        }
    }
    safe_str(amax, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_alphamin)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *amin = fargs[0];
    UTF8 buf_min[LBUF_SIZE];
    mux_strncpy(buf_min, strip_color(amin), LBUF_SIZE-1);
    for (int i = 1; i < nfargs; i++)
    {
        if (fargs[i] && strcmp(reinterpret_cast<char *>(buf_min), reinterpret_cast<char *>(strip_color(fargs[i]))) > 0)
        {
            amin = fargs[i];
            mux_strncpy(buf_min, strip_color(amin), LBUF_SIZE-1);
        }
    }
    safe_str(amin, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_valid)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Checks to see if a given <something> is valid as a parameter of
    // a given type (such as an object name)
    //
    size_t nValidName;
    bool bValid;
    if (!*fargs[0] || !*fargs[1])
    {
        bValid = false;
    }
    else if (!mux_stricmp(fargs[0], T("attrname")))
    {
        MakeCanonicalAttributeName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("comalias")))
    {
        MakeCanonicalComAlias(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("doing")))
    {
        MakeCanonicalDoing(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("exitname")))
    {
        MakeCanonicalExitName(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("malias")))
    {
        MakeCanonicalMailAlias(fargs[1], &nValidName, &bValid);
    }
    else if (!mux_stricmp(fargs[0], T("maliasdesc")))
    {
        size_t vw;
        MakeCanonicalMailAliasDesc(fargs[1], &nValidName, &bValid, &vw);
    }
    else if (  !mux_stricmp(fargs[0], T("name"))
            || !mux_stricmp(fargs[0], T("thingname")))
    {
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid, mudconf.thing_name_charset);
    }
    else if (!mux_stricmp(fargs[0], T("roomname")))
    {
        MakeCanonicalObjectName(fargs[1], &nValidName, &bValid, mudconf.room_name_charset);
    }
    else if (!mux_stricmp(fargs[0], T("password")))
    {
        const UTF8 *msg;
        bValid = ok_password(fargs[1], &msg);
    }
    else if (!mux_stricmp(fargs[0], T("playername")))
    {
        bValid = ValidatePlayerName(fargs[1]);
    }
    else
    {
        safe_nothing(buff, bufc);
        return;
    }
    safe_bool(bValid, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_hastype)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    bool bResult = false;
    switch (fargs[1][0])
    {
    case 'r':
    case 'R':

        bResult = isRoom(it);
        break;

    case 'e':
    case 'E':

        bResult = isExit(it);
        break;

    case 'p':
    case 'P':

        bResult = isPlayer(it);
        break;

    case 't':
    case 'T':

        bResult = isThing(it);
        break;

    default:

        safe_str(T("#-1 NO SUCH TYPE"), buff, bufc);
        break;
    }
    safe_bool(bResult, buff, bufc);
}

// Borrowed from PennMUSH 1.50
//
FUNCTION(fun_lparent)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    else if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
        return;
    }

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    if (!ItemToList_AddInteger(&pContext, it))
    {
        ItemToList_Final(&pContext);
        return;
    }

    dbref par = Parent(it);

    int iNestLevel = 1;
    while (  Good_obj(par)
          && Examinable(executor, it)
          && iNestLevel < mudconf.parent_nest_lim)
    {
        if (!ItemToList_AddInteger(&pContext, par))
        {
            break;
        }
        it = par;
        par = Parent(par);
        iNestLevel++;
    }
    ItemToList_Final(&pContext);
}

#ifdef DEPRECATED

// stacksize - returns how many items are stuffed onto an object stack
//
static int stacksize(dbref doer)
{
    int i;
    MUX_STACK *sp;
    for (i = 0, sp = Stack(doer); sp != nullptr; sp = sp->next, i++)
    {
        // Nothing
        ;
    }
    return i;
}

FUNCTION(fun_lstack)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    MUX_STACK *sp;
    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    for (sp = Stack(doer); sp != nullptr; sp = sp->next)
    {
        safe_str(sp->data, buff, bufc);
        if (sp->next != nullptr)
        {
            safe_chr(' ', buff, bufc);
        }
    }
}

// stack_clr - clear the stack.
//
void stack_clr(dbref obj)
{
    // Clear the stack.
    //
    MUX_STACK *sp, *next;
    for (sp = Stack(obj); sp != nullptr; sp = next)
    {
        next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = nullptr;
    }
    s_Stack(obj, nullptr);
}

FUNCTION(fun_empty)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    stack_clr(doer);
}

FUNCTION(fun_items)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs == 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    safe_ltoa(stacksize(doer), buff, bufc);
}

FUNCTION(fun_peek)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    MUX_STACK *sp;
    dbref doer;
    int count, pos;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }

    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str(T("#-1 POSITION TOO LARGE"), buff, bufc);
        return;
    }
    count = 0;
    sp = Stack(doer);
    while (count != pos)
    {
        if (sp == nullptr)
        {
            return;
        }
        count++;
        sp = sp->next;
    }

    safe_str(sp->data, buff, bufc);
}

FUNCTION(fun_pop)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;

    if (nfargs <= 0 || !*fargs[0])
    {
        doer = executor;
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
    }
    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int pos;
    if (nfargs <= 1 || !*fargs[1])
    {
        pos = 0;
    }
    else
    {
        pos = mux_atol(fargs[1]);
    }
    if (stacksize(doer) == 0)
    {
        return;
    }
    if (pos > (stacksize(doer) - 1))
    {
        safe_str(T("#-1 POSITION TOO LARGE"), buff, bufc);
        return;
    }

    MUX_STACK *sp = Stack(doer);
    MUX_STACK *prev = nullptr;
    int count = 0;
    while (count != pos)
    {
        if (sp == nullptr)
        {
            return;
        }
        prev = sp;
        sp = sp->next;
        count++;
    }

    safe_str(sp->data, buff, bufc);
    if (count == 0)
    {
        s_Stack(doer, sp->next);
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = nullptr;
    }
    else
    {
        prev->next = sp->next;
        free_lbuf(sp->data);
        MEMFREE(sp);
        sp = nullptr;
    }
}

FUNCTION(fun_push)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref doer;
    UTF8 *data;

    if (nfargs <= 1 || !*fargs[1])
    {
        doer = executor;
        data = fargs[0];
    }
    else
    {
        doer = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(doer))
        {
            safe_match_result(doer, buff, bufc);
            return;
        }
        data = fargs[1];
    }

    if (!Controls(executor, doer))
    {
        safe_noperm(buff, bufc);
        return;
    }
    if (stacksize(doer) >= mudconf.stack_limit)
    {
        safe_str(T("#-1 STACK SIZE EXCEEDED"), buff, bufc);
        return;
    }
    MUX_STACK *sp = static_cast<MUX_STACK *>(MEMALLOC(sizeof(MUX_STACK)));
    ISOUTOFMEMORY(sp);
    sp->next = Stack(doer);
    sp->data = alloc_lbuf("push");
    mux_strncpy(sp->data, data, LBUF_SIZE-1);
    s_Stack(doer, sp);
}

#endif // DEPRECATED

/* ---------------------------------------------------------------------------
 * fun_regmatch: Return 0 or 1 depending on whether or not a regular
 * expression matches a string. If a third argument is specified, dump
 * the results of a regexp pattern match into a set of arbitrary r()-registers.
 *
 * regmatch(string, pattern, list of registers)
 * If the number of matches exceeds the registers, those bits are tossed
 * out.
 * If -1 is specified as a register number, the matching bit is tossed.
 * Therefore, if the list is "-1 0 3 5", the regexp $0 is tossed, and
 * the regexp $1, $2, and $3 become r(0), r(3), and r(5), respectively.
 */

static void real_regmatch(const UTF8 *search, const UTF8 *pattern, UTF8 *registers,
                   int nfargs, UTF8 *buff, UTF8 **bufc, bool cis)
{
    if (alarm_clock.alarmed)
    {
        return;
    }

    PCRE2_SIZE erroffset;
    int errcode;

    // Compile the pattern
    pcre2_code *re = pcre2_compile_8(
        pattern,                      // pattern string
        PCRE2_ZERO_TERMINATED,        // pattern is zero-terminated
        PCRE2_UTF|(cis ? PCRE2_CASELESS : 0),  // options
        &errcode,                     // for error code
        &erroffset,                   // for error offset
        nullptr                       // use default compile context
    );

    if (!re)
    {
        // Matching error - get the error message
        PCRE2_UCHAR errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));

        safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
        safe_str(reinterpret_cast<UTF8 *>(errbuf), buff, bufc);
        return;
    }

    // Create match data block for storing results
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
    if (!match_data)
    {
        pcre2_code_free(re);
        safe_str(T("#-1 REGEXP MATCH DATA ERROR"), buff, bufc);
        return;
    }

    // Do the match
    int matches = pcre2_match(
        re,                          // compiled pattern
        search,                      // subject string
        PCRE2_ZERO_TERMINATED,       // length of subject string
        0,                          // start offset in subject
        0,                          // options
        match_data,                 // block for storing the result
        nullptr                     // use default match context
    );

    safe_bool(matches > 0, buff, bufc);

    // If we don't have a third argument, we're done.
    if (nfargs != 3 || matches <= 0)
    {
        pcre2_match_data_free(match_data);
        pcre2_code_free(re);
        return;
    }

    // We need to parse the list of registers
    const int NSUBEXP = 2 * MAX_GLOBAL_REGS;
    UTF8 *qregs[NSUBEXP];
    SEP sep;
    sep.n = 1;
    memcpy(sep.str, " ", 2);
    int nqregs = list2arr(qregs, NSUBEXP, registers, sep);

    // Get ovector pointer for accessing capture groups
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);

    // Process each requested register
    for (int i = 0; i < nqregs; i++)
    {
        int curq;
        if (  qregs[i]
           && *qregs[i]
           && (curq = mux_RegisterSet[static_cast<unsigned char>(qregs[i][0])]) != -1
           && qregs[i][1] == '\0'
           && curq < MAX_GLOBAL_REGS)
        {
            // Only try to extract the substring if it exists in the match results
            if (i < matches)
            {
                UTF8 *p = alloc_lbuf("fun_regmatch");
                PCRE2_SIZE outlen = 0;
                int ret = pcre2_substring_copy_bynumber(
                    match_data,    // match data block
                    i,             // capture group number
                    p,             // output buffer
                    &outlen        // output length
                );

                if (ret >= 0)
                {
                    size_t n = static_cast<size_t>(outlen);
                    RegAssign(&mudstate.global_regs[curq], n, p);
                }
                else
                {
                    // No match for this capture, clear register
                    RegAssign(&mudstate.global_regs[curq], 0, nullptr);
                }
                free_lbuf(p);
            }
            else
            {
                // No match for this capture, clear register
                RegAssign(&mudstate.global_regs[curq], 0, nullptr);
            }
        }
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
}

FUNCTION(fun_regmatch)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, false);
}

FUNCTION(fun_regmatchi)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regmatch(fargs[0], fargs[1], fargs[2], nfargs, buff, bufc, true);
}


/* ---------------------------------------------------------------------------
 * regrab(), regraball(). Like grab() and graball(), using a regular expression
 * instead of a wildcard pattern. The versions ending in i are case-insensitive.
 */

static void real_regrab(UTF8 *search, const UTF8 *pattern, const SEP &sep, UTF8 *buff,
                 UTF8 **bufc, bool cis, bool all)
{
    if (alarm_clock.alarmed)
    {
        return;
    }

    PCRE2_SIZE erroffset;
    int errcode;

    // Compile the pattern
    pcre2_code *re = pcre2_compile_8(
        pattern,                      // pattern string
        PCRE2_ZERO_TERMINATED,        // pattern is zero-terminated
        PCRE2_UTF|(cis ? PCRE2_CASELESS : 0),  // options
        &errcode,                     // for error code
        &erroffset,                   // for error offset
        nullptr                       // use default compile context
    );

    if (!re)
    {
        // Matching error - get the error message
        PCRE2_UCHAR errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));

        safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
        safe_str(reinterpret_cast<UTF8 *>(errbuf), buff, bufc);
        return;
    }

    // Create match data block for storing results
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
    if (!match_data)
    {
        pcre2_code_free(re);
        safe_str(T("#-1 REGEXP MATCH DATA ERROR"), buff, bufc);
        return;
    }

    // JIT compile if we're going to use the pattern multiple times
    if (all)
    {
        pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    }

    bool first = true;
    UTF8 *s = trim_space_sep(search, sep);
    do
    {
        UTF8 *r = split_token(&s, sep);
        if (!alarm_clock.alarmed)
        {
            int rc = pcre2_match(
                re,                      // compiled pattern
                r,                       // subject string
                PCRE2_ZERO_TERMINATED,   // length of subject string
                0,                       // start offset in subject
                0,                       // options
                match_data,              // block for storing the result
                nullptr                  // use default match context
            );

            if (rc >= 0)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    print_sep(sep, buff, bufc);
                }
                safe_str(r, buff, bufc);
                if (!all)
                {
                    break;
                }
            }
        }
    } while (s);

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);
}

FUNCTION(fun_regrab)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], sep, buff, bufc, false, false);
}

FUNCTION(fun_regrabi)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], sep, buff, bufc, true, false);
}

FUNCTION(fun_regraball)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], sep, buff, bufc, false, true);
}

FUNCTION(fun_regraballi)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    real_regrab(fargs[0], fargs[1], sep, buff, bufc, true, true);
}


/* ---------------------------------------------------------------------------
 * regedit(), regediti(), regeditall(), regeditalli(). Regex-based
 * find-and-replace on strings, borrowing Penn's concept. Supports $0-$99
 * for numbered capture groups and $<name> for named capture groups in the
 * replacement string.
 */

// regedit_substitute: Expand $N and $<name> references in a replacement
// string using match data from a PCRE2 match.
//
static void regedit_substitute(const UTF8 *replacement,
    pcre2_match_data *match_data, UTF8 *buff, UTF8 **bufc)
{
    const UTF8 *p = replacement;
    while (*p)
    {
        if ('$' == *p)
        {
            p++;
            if ('<' == *p)
            {
                // Named capture group: $<name>
                //
                p++;
                const UTF8 *namestart = p;
                while (  *p
                      && '>' != *p)
                {
                    p++;
                }
                if ('>' == *p)
                {
                    size_t namelen = p - namestart;
                    p++;

                    // Build null-terminated name.
                    //
                    UTF8 namebuf[128];
                    if (namelen < sizeof(namebuf))
                    {
                        memcpy(namebuf, namestart, namelen);
                        namebuf[namelen] = '\0';

                        UTF8 groupbuf[LBUF_SIZE];
                        PCRE2_SIZE outlen = sizeof(groupbuf) - 1;
                        int ret = pcre2_substring_copy_byname(
                            match_data,
                            namebuf,
                            groupbuf,
                            &outlen
                        );
                        if (ret >= 0)
                        {
                            safe_copy_buf(groupbuf, outlen, buff, bufc);
                        }
                    }
                }
                else
                {
                    // Unterminated $<...>, output literally.
                    //
                    safe_chr('$', buff, bufc);
                    safe_chr('<', buff, bufc);
                    safe_str(namestart, buff, bufc);
                }
            }
            else if (mux_isdigit(*p))
            {
                // Numbered capture group: $0 through $99
                //
                int groupnum = *p - '0';
                p++;
                if (mux_isdigit(*p))
                {
                    groupnum = groupnum * 10 + (*p - '0');
                    p++;
                }

                UTF8 groupbuf[LBUF_SIZE];
                PCRE2_SIZE outlen = sizeof(groupbuf) - 1;
                int ret = pcre2_substring_copy_bynumber(
                    match_data,
                    groupnum,
                    groupbuf,
                    &outlen
                );
                if (ret >= 0)
                {
                    safe_copy_buf(groupbuf, outlen, buff, bufc);
                }
            }
            else
            {
                // Lone $ not followed by digit or <, output literally.
                //
                safe_chr('$', buff, bufc);
            }
        }
        else
        {
            safe_chr(*p, buff, bufc);
            p++;
        }
    }
}

static void real_regedit(UTF8 *fargs[], int nfargs, UTF8 *buff,
    UTF8 **bufc, bool cis, bool all)
{
    if (alarm_clock.alarmed)
    {
        return;
    }

    // Use two lbufs as ping-pong buffers for multi-pair processing.
    //
    UTF8 *inbuf = alloc_lbuf("regedit.in");
    UTF8 *outbuf = alloc_lbuf("regedit.out");

    mux_strncpy(inbuf, fargs[0], LBUF_SIZE - 1);

    for (int i = 1; i + 1 < nfargs; i += 2)
    {
        if (alarm_clock.alarmed)
        {
            break;
        }

        PCRE2_SIZE erroffset;
        int errcode;

        pcre2_code *re = pcre2_compile_8(
            fargs[i],
            PCRE2_ZERO_TERMINATED,
            PCRE2_UTF | (cis ? PCRE2_CASELESS : 0),
            &errcode,
            &erroffset,
            nullptr
        );

        if (!re)
        {
            PCRE2_UCHAR errbuf[256];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));

            free_lbuf(inbuf);
            free_lbuf(outbuf);
            safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
            safe_str(reinterpret_cast<UTF8 *>(errbuf), buff, bufc);
            return;
        }

        pcre2_match_data *match_data =
            pcre2_match_data_create_from_pattern(re, nullptr);
        if (!match_data)
        {
            pcre2_code_free(re);
            free_lbuf(inbuf);
            free_lbuf(outbuf);
            safe_str(T("#-1 REGEXP MATCH DATA ERROR"), buff, bufc);
            return;
        }

        if (all)
        {
            pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
        }

        UTF8 *outp = outbuf;
        PCRE2_SIZE pos = 0;
        PCRE2_SIZE inlen = strlen(reinterpret_cast<char *>(inbuf));
        bool matched = false;

        while (pos <= inlen)
        {
            if (alarm_clock.alarmed)
            {
                break;
            }

            int rc = pcre2_match(
                re,
                inbuf,
                inlen,
                pos,
                0,
                match_data,
                nullptr
            );

            if (rc < 0)
            {
                // No more matches. Copy rest of input.
                //
                safe_copy_buf(inbuf + pos, inlen - pos, outbuf, &outp);
                break;
            }

            matched = true;
            PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);

            // Copy text before this match.
            //
            if (ovector[0] > pos)
            {
                safe_copy_buf(inbuf + pos, ovector[0] - pos, outbuf, &outp);
            }

            // Expand replacement string with capture group substitution.
            //
            regedit_substitute(fargs[i + 1], match_data, outbuf, &outp);

            if (!all)
            {
                // Copy remaining text after the first match.
                //
                safe_copy_buf(inbuf + ovector[1], inlen - ovector[1],
                    outbuf, &outp);
                break;
            }

            // Advance past this match. Handle zero-length matches by
            // advancing one byte to avoid infinite loops.
            //
            if (ovector[1] == pos)
            {
                if (pos < inlen)
                {
                    safe_chr(inbuf[pos], outbuf, &outp);
                }
                pos++;
            }
            else
            {
                pos = ovector[1];
            }
        }

        *outp = '\0';

        pcre2_match_data_free(match_data);
        pcre2_code_free(re);

        // Swap buffers for next pair.
        //
        UTF8 *tmp = inbuf;
        inbuf = outbuf;
        outbuf = tmp;
    }

    safe_str(inbuf, buff, bufc);
    free_lbuf(inbuf);
    free_lbuf(outbuf);
}

FUNCTION(fun_regedit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regedit(fargs, nfargs, buff, bufc, false, false);
}

FUNCTION(fun_regediti)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regedit(fargs, nfargs, buff, bufc, true, false);
}

FUNCTION(fun_regeditall)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regedit(fargs, nfargs, buff, bufc, false, true);
}

FUNCTION(fun_regeditalli)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    real_regedit(fargs, nfargs, buff, bufc, true, true);
}


/* ---------------------------------------------------------------------------
 * fun_translate: Takes a string and a second argument. If the second argument
 * is 0 or s, control characters are converted to spaces. If it's 1 or p,
 * they're converted to percent substitutions.
 */

FUNCTION(fun_translate)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int ch = fargs[1][0];
    bool type = (ch == 'p' || ch == '1');
    safe_str(translate_string(fargs[0], type), buff, bufc);
}


// -------------------------------------------------------------------------
// fun_lrooms:  Takes a dbref (room), an int (N), and an optional bool (B).
//
// MUX Syntax:  lrooms(<room> [,<N>[,<B>]])
//
// Returns a list of rooms <N>-levels deep from <room>. If <B> == 1, it will
//   return all room dbrefs between 0 and <N> levels, while <B> == 0 will
//   return only the room dbrefs on the Nth level. The default is to show all
//   rooms dbrefs between 0 and <N> levels.
//
// Written by Marlek.  Idea from RhostMUSH.
//
static void room_list
(
    dbref player,
    dbref enactor,
    dbref room,
    int   maxlevels,
    bool  showall
)
{
    // BFS level-by-level traversal.  'current' holds rooms at depth d,
    // 'next' collects rooms at depth d+1.
    //
    std::vector<dbref> current;
    std::vector<dbref> next;

    current.push_back(room);

    for (int d = 0; d < maxlevels; d++)
    {
        for (size_t ci = 0; ci < current.size(); ci++)
        {
            dbref r = current[ci];

            int lev;
            dbref parent;
            ITER_PARENTS(r, parent, lev)
            {
                if (!Has_exits(parent))
                {
                    continue;
                }
                int key = 0;
                if (Examinable(player, parent))
                {
                    key |= VE_LOC_XAM;
                }
                if (Dark(parent))
                {
                    key |= VE_LOC_DARK;
                }
                if (Dark(r))
                {
                    key |= VE_BASE_DARK;
                }

                dbref thing;
                DOLIST(thing, Exits(parent))
                {
                    dbref loc = Location(thing);
                    if (  exit_visible(thing, player, key)
                       && !mudstate.bfTraverse.IsSet(loc))
                    {
                        mudstate.bfTraverse.Set(loc);

                        if (  (  showall
                              || d + 1 == maxlevels)
                           && (  Examinable(player, loc)
                              || Location(player) == loc
                              || loc == enactor))
                        {
                            mudstate.bfReport.Set(loc);
                        }

                        next.push_back(loc);
                    }
                }
            }
        }

        current.swap(next);
        next.clear();
    }
}

FUNCTION(fun_lrooms)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref room = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(room))
    {
        safe_match_result(room, buff, bufc);
        return;
    }
    else if (!isRoom(room))
    {
        safe_str(T("#-1 FIRST ARGUMENT MUST BE A ROOM"), buff, bufc);
        return;
    }

    int N = 1;
    if (nfargs >= 2)
    {
        N = mux_atol(fargs[1]);
        if (N < 0)
        {
            safe_str(T("#-1 SECOND ARGUMENT MUST BE A POSITIVE NUMBER"),
                buff, bufc);
            return;
        }
        else if (N > 50)
        {
            // Maybe this can be turned into a config parameter to prevent
            // misuse by putting in really large values.
            //
            safe_str(T("#-1 SECOND ARGUMENT IS TOO LARGE"), buff, bufc);
            return;
        }
    }

    bool B = true;
    if (nfargs == 3)
    {
        B = xlate(fargs[2]);
    }

    mudstate.bfReport.Resize(mudstate.db_top-1);
    mudstate.bfTraverse.Resize(mudstate.db_top-1);
    mudstate.bfReport.ClearAll();
    mudstate.bfTraverse.ClearAll();

    mudstate.bfTraverse.Set(room);
    room_list(executor, enactor, room, N, B);
    mudstate.bfReport.Clear(room);

    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    dbref i;
    DO_WHOLE_DB(i)
    {
        if (  mudstate.bfReport.IsSet(i)
           && !ItemToList_AddInteger(&pContext, i))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}
