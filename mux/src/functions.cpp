// functions.cpp -- MUX function handlers.
//
// $Id: functions.cpp,v 1.5 2002-06-03 21:06:21 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <limits.h>
#include <math.h>
#include <float.h>

#include "attrs.h"
#include "match.h"
#include "command.h"
#include "functions.h"
#include "misc.h"
#include "ansi.h"

UFUN *ufun_head;

extern NAMETAB indiv_attraccess_nametab[];

extern void cf_log_notfound(dbref player, char *cmd, const char *thingname,
                            char *thing);
extern void cf_display(dbref, char *, char *, char **);

// Function definitions from funceval.cpp
//

#define XFUNCTION(x) extern void x(char *buff, char **bufc, dbref player, dbref caller, dbref cause, char *fargs[], int nfargs, char *cargs[], int ncargs)

XFUNCTION(fun_cwho);
XFUNCTION(fun_beep);
XFUNCTION(fun_ansi);
XFUNCTION(fun_zone);
#ifdef SIDE_EFFECT_FUNCTIONS
XFUNCTION(fun_link);
XFUNCTION(fun_tel);
XFUNCTION(fun_pemit);
XFUNCTION(fun_oemit);
XFUNCTION(fun_emit);
XFUNCTION(fun_remit);
XFUNCTION(fun_create);
XFUNCTION(fun_set);
#endif
XFUNCTION(fun_last);
XFUNCTION(fun_matchall);
XFUNCTION(fun_ports);
XFUNCTION(fun_mix);
XFUNCTION(fun_foreach);
XFUNCTION(fun_munge);
XFUNCTION(fun_visible);
XFUNCTION(fun_elements);
XFUNCTION(fun_grab);
XFUNCTION(fun_scramble);
XFUNCTION(fun_shuffle);
XFUNCTION(fun_pickrand);
XFUNCTION(fun_sortby);
XFUNCTION(fun_default);
XFUNCTION(fun_edefault);
XFUNCTION(fun_udefault);
XFUNCTION(fun_findable);
XFUNCTION(fun_isword);
XFUNCTION(fun_hasattr);
XFUNCTION(fun_hasattrp);
XFUNCTION(fun_zwho);
XFUNCTION(fun_inzone);
XFUNCTION(fun_children);
XFUNCTION(fun_encrypt);
XFUNCTION(fun_decrypt);
XFUNCTION(fun_objeval);
XFUNCTION(fun_localize);
XFUNCTION(fun_null);
XFUNCTION(fun_squish);
XFUNCTION(fun_stripansi);
XFUNCTION(fun_zfun);
XFUNCTION(fun_columns);
XFUNCTION(fun_table);
XFUNCTION(fun_playmem);
XFUNCTION(fun_objmem);
XFUNCTION(fun_orflags);
XFUNCTION(fun_andflags);
XFUNCTION(fun_strtrunc);
XFUNCTION(fun_ifelse);
XFUNCTION(fun_inc);
XFUNCTION(fun_dec);
XFUNCTION(fun_mail);
XFUNCTION(fun_mailfrom);
XFUNCTION(fun_die);
XFUNCTION(fun_lrand);
XFUNCTION(fun_lit);
XFUNCTION(fun_shl);
XFUNCTION(fun_shr);
XFUNCTION(fun_band);
XFUNCTION(fun_bor);
XFUNCTION(fun_bnand);
XFUNCTION(fun_crc32);
XFUNCTION(fun_pack);
XFUNCTION(fun_unpack);
XFUNCTION(fun_vadd);
XFUNCTION(fun_vsub);
XFUNCTION(fun_vmul);
XFUNCTION(fun_vmag);
XFUNCTION(fun_vunit);
XFUNCTION(fun_vdim);
XFUNCTION(fun_strcat);
XFUNCTION(fun_grep);
XFUNCTION(fun_grepi);
XFUNCTION(fun_alphamax);
XFUNCTION(fun_alphamin);
XFUNCTION(fun_valid);
XFUNCTION(fun_hastype);
XFUNCTION(fun_lparent);
XFUNCTION(fun_empty);
XFUNCTION(fun_push);
XFUNCTION(fun_peek);
XFUNCTION(fun_pop);
XFUNCTION(fun_items);
XFUNCTION(fun_lstack);
XFUNCTION(fun_regmatch);
XFUNCTION(fun_translate);
XFUNCTION(fun_doing);      // in netcommon.cpp
XFUNCTION(fun_poll);       // in netcommon.cpp
XFUNCTION(fun_motd);       // in netcommon.cpp
XFUNCTION(fun_channels);   // in comsys.cpp
XFUNCTION(fun_comtitle);   // in comsys.cpp
XFUNCTION(fun_comalias);   // in comsys.cpp
XFUNCTION(fun_iadd);
XFUNCTION(fun_isub);
XFUNCTION(fun_imul);
XFUNCTION(fun_iabs);
XFUNCTION(fun_isign);
XFUNCTION(fun_digittime);
XFUNCTION(fun_singletime);
XFUNCTION(fun_exptime);
XFUNCTION(fun_writetime);
XFUNCTION(fun_cmds);
XFUNCTION(fun_startsecs);
XFUNCTION(fun_lflags);
XFUNCTION(fun_lattrcmds);
XFUNCTION(fun_lcmds);
XFUNCTION(fun_conntotal);
XFUNCTION(fun_connmax);
XFUNCTION(fun_connlast);
XFUNCTION(fun_connnum);
XFUNCTION(fun_connleft);
XFUNCTION(fun_art);
XFUNCTION(fun_t);
XFUNCTION(fun_dumping);
XFUNCTION(fun_lrooms);
XFUNCTION(fun_hasquota);

// Trim off leading and trailing spaces if the separator char is a
// space -- known length version.
//
char *trim_space_sep_LEN(char *str, int nStr, char sep, int *nTrim)
{
    if (sep != ' ')
    {
        *nTrim = nStr;
        return str;
    }

    // Advance over leading spaces.
    //
    char *pBegin = str;
    char *pEnd = str + nStr - 1;
    while (*pBegin == ' ')
    {
        pBegin++;
    }

    // Advance over trailing spaces.
    //
    for (; *pEnd == ' ' && pEnd > pBegin; pEnd--)
    {
        // Nothing
    }
    pEnd++;

    *pEnd = '\0';
    *nTrim = pEnd - pBegin;
    return pBegin;
}


/*
 * Trim off leading and trailing spaces if the separator char is a space
 */

char *trim_space_sep(char *str, char sep)
{
    char *p;

    if (sep != ' ')
    {
        return str;
    }
    while (*str == ' ')
    {
        str++;
    }
    for (p = str; *p; p++)
    {
        // Nothing
    }
    for (p--; *p == ' ' && p > str; p--)
    {
        // Nothing
    }
    p++;
    *p = '\0';
    return str;
}

// next_token: Point at start of next token in string -- known length
// version.
//
char *next_token_LEN(char *str, int *nStr, char sep)
{
    char *pBegin = str;
    while (*pBegin && (*pBegin != sep))
    {
        pBegin++;
    }
    if (!*pBegin)
    {
        *nStr = 0;
        return NULL;
    }
    pBegin++;
    if (sep == ' ')
    {
        while (*pBegin == ' ')
        {
            pBegin++;
        }
    }
    *nStr -= pBegin - str;
    return pBegin;
}

/*
 * next_token: Point at start of next token in string
 */

char *next_token(char *str, char sep)
{
    while (*str && (*str != sep))
    {
        str++;
    }
    if (!*str)
    {
        return NULL;
    }
    str++;
    if (sep == ' ')
    {
        while (*str == ' ')
        {
            str++;
        }
    }
    return str;
}

// split_token: Get next token from string as null-term string. String is
// destructively modified -- known length version.
//
char *split_token_LEN(char **sp, int *nStr, char sep, int *nToken)
{
    char *str, *save;

    save = str = *sp;
    if (!str)
    {
        *nStr = 0;
        *sp = NULL;
        *nToken = 0;
        return NULL;
    }

    // Advance over token
    //
    while (*str && (*str != sep))
    {
        str++;
    }
    *nToken = str - save;

    if (*str)
    {
        *str++ = '\0';
        if (sep == ' ')
        {
            while (*str == ' ')
            {
                str++;
            }
        }
        *nStr -= str - save;
    }
    else
    {
        *nStr = 0;
        str = NULL;
    }
    *sp = str;
    return save;
}

/*
 * split_token: Get next token from string as null-term string.  String is
 * * destructively modified.
 */

char *split_token(char **sp, char sep)
{
    char *str = *sp;
    char *save = str;

    if (!str)
    {
        *sp = NULL;
        return NULL;
    }
    while (  *str
          && *str != sep)
    {
        str++;
    }
    if (*str)
    {
        *str++ = '\0';
        if (sep == ' ')
        {
            while (*str == ' ')
            {
                str++;
            }
        }
    }
    else
    {
        str = NULL;
    }
    *sp = str;
    return save;
}

/*
 * ---------------------------------------------------------------------------
 * * List management utilities.
 */

#define ASCII_LIST      1
#define NUMERIC_LIST    2
#define DBREF_LIST      4
#define FLOAT_LIST      8
#define ALL_LIST        (ASCII_LIST|NUMERIC_LIST|DBREF_LIST|FLOAT_LIST)

static int autodetect_list(char *ptrs[], int nitems)
{
    int could_be = ALL_LIST;
    for (int i = 0; i < nitems; i++)
    {
        char *p = ptrs[i];
        if (p[0] != NUMBER_TOKEN)
        {
            could_be &= ~DBREF_LIST;
        }
        if (  (could_be & DBREF_LIST)
           && !is_integer(p+1, NULL))
        {
            could_be &= ~(DBREF_LIST|NUMERIC_LIST|FLOAT_LIST);
        }
        if (  (could_be & FLOAT_LIST)
           && !is_real(p))
        {
            could_be &= ~(NUMERIC_LIST|FLOAT_LIST);
        }
        if (  (could_be & NUMERIC_LIST)
           && !is_integer(p, NULL))
        {
            could_be &= ~NUMERIC_LIST;
        }

        if (could_be == ASCII_LIST)
        {
            return ASCII_LIST;
        }
    }
    if (could_be & NUMERIC_LIST)
    {
        return NUMERIC_LIST;
    }
    else if (could_be & FLOAT_LIST)
    {
        return FLOAT_LIST;
    }
    else if (could_be & DBREF_LIST)
    {
        return DBREF_LIST;
    }
    return ASCII_LIST;
}

static int get_list_type
(
    char *fargs[],
    int nfargs,
    int type_pos,
    char *ptrs[],
    int nitems
)
{
    if (nfargs >= type_pos)
    {
        switch (Tiny_ToLower[(unsigned char)*fargs[type_pos - 1]])
        {
        case 'd':
            return DBREF_LIST;
        case 'n':
            return NUMERIC_LIST;
        case 'f':
            return FLOAT_LIST;
        case '\0':
            return autodetect_list(ptrs, nitems);
        default:
            return ASCII_LIST;
        }
    }
    return autodetect_list(ptrs, nitems);
}

int list2arr(char *arr[], int maxlen, char *list, char sep)
{
    list = trim_space_sep(list, sep);
    if (list[0] == '\0')
    {
        return 0;
    }
    char *p = split_token(&list, sep);
    int i;
    for (i = 0; p && i < maxlen; i++, p = split_token(&list, sep))
    {
        arr[i] = p;
    }
    return i;
}

void arr2list(char *arr[], int alen, char *list, char **bufc, char sep)
{
    int i;
    for (i = 0; i < alen-1; i++)
    {
        safe_str(arr[i], list, bufc);
        print_sep(sep, list, bufc);
    }
    if (alen)
    {
        safe_str(arr[i], list, bufc);
    }
}

static int dbnum(char *dbr)
{
    if (dbr[0] != '#' || dbr[1] == '\0')
    {
        return 0;
    }
    else
    {
        return Tiny_atol(dbr + 1);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * nearby_or_control: Check if player is near or controls thing
 */

int nearby_or_control(dbref player, dbref thing)
{
    if (!Good_obj(player) || !Good_obj(thing))
        return 0;
    if (Controls(player, thing))
        return 1;
    if (!nearby(player, thing))
        return 0;
    return 1;
}

#ifdef HAVE_IEEE_FP_FORMAT

const char *TinyFPStrings[] = { "+Inf", "-Inf", "Ind", "NaN", "0", "0", "0", "0" };

#define TINY_FPGROUP_PASS  0x00 // Pass-through to printf
#define TINY_FPGROUP_ZERO  0x10 // Force to be zero.
#define TINY_FPGROUP_PINF  0x20 // "+Inf"
#define TINY_FPGROUP_NINF  0x30 // "-Inf"
#define TINY_FPGROUP_IND   0x40 // "Ind"
#define TINY_FPGROUP_NAN   0x50 // "NaN"
#define TINY_FPGROUP(x) ((x) & 0xF0)

// Tiny_fpclass returns an integer that is one of the following:
//
#define TINY_FPCLASS_PINF  (TINY_FPGROUP_PINF|0) // Positive infinity (+INF)
#define TINY_FPCLASS_NINF  (TINY_FPGROUP_NINF|1) // Negative infinity (-INF)
#define TINY_FPCLASS_QNAN  (TINY_FPGROUP_IND |2) // Quiet NAN (Indefinite)
#define TINY_FPCLASS_SNAN  (TINY_FPGROUP_NAN |3) // Signaling NAN
#define TINY_FPCLASS_ND    (TINY_FPGROUP_ZERO|4) // Negative denormalized
#define TINY_FPCLASS_NZ    (TINY_FPGROUP_ZERO|5) // Negative zero (-0)
#define TINY_FPCLASS_PZ    (TINY_FPGROUP_ZERO|6) // Positive zero (+0)
#define TINY_FPCLASS_PD    (TINY_FPGROUP_ZERO|7) // Positive denormalized
#define TINY_FPCLASS_PN    (TINY_FPGROUP_PASS|8) // Positive normalized non-zero
#define TINY_FPCLASS_NN    (TINY_FPGROUP_PASS|9) // Negative normalized non-zero
#define TINY_FPCLASS(x)    ((x) & 0x0F)

#ifdef WIN32
#define IEEE_MASK_SIGN     0x8000000000000000ui64
#define IEEE_MASK_EXPONENT 0x7FF0000000000000ui64
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFui64
#define IEEE_MASK_QNAN     0x0008000000000000ui64
#else
#define IEEE_MASK_SIGN     0x8000000000000000ull
#define IEEE_MASK_EXPONENT 0x7FF0000000000000ull
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFull
#define IEEE_MASK_QNAN     0x0008000000000000ull
#endif

#define ARBITRARY_NUMBER 1
#define IEEE_MAKE_TABLESIZE 5
typedef union
{
    INT64  i64;
    double d;
} SpecialFloatUnion;

// We return a Quiet NAN when a Signalling NAN is requested because
// any operation on a Signalling NAN will result in a Quiet NAN anyway.
// MUX doesn't catch SIGFPE, but if it did, a Signalling NAN would
// generate a SIGFPE.
//
SpecialFloatUnion SpecialFloatTable[IEEE_MAKE_TABLESIZE] =
{
    { 0 }, // Unused.
    { IEEE_MASK_EXPONENT | IEEE_MASK_QNAN | ARBITRARY_NUMBER },
    { IEEE_MASK_EXPONENT | IEEE_MASK_QNAN | ARBITRARY_NUMBER },
    { IEEE_MASK_EXPONENT },
    { IEEE_MASK_EXPONENT | IEEE_MASK_SIGN }
};

double MakeSpecialFloat(int iWhich)
{
    return SpecialFloatTable[iWhich].d;
}

static int Tiny_fpclass(double result)
{
    UINT64 i64;

    *((double *)&i64) = result;

    if ((i64 & IEEE_MASK_EXPONENT) == 0)
    {
        if (i64 & IEEE_MASK_MANTISSA)
        {
            if (i64 & IEEE_MASK_SIGN) return TINY_FPCLASS_ND;
            else                      return TINY_FPCLASS_PD;
        }
        else
        {
            if (i64 & IEEE_MASK_SIGN) return TINY_FPCLASS_NZ;
            else                      return TINY_FPCLASS_PZ;
        }
    }
    else if ((i64 & IEEE_MASK_EXPONENT) == IEEE_MASK_EXPONENT)
    {
        if (i64 & IEEE_MASK_MANTISSA)
        {
            if (i64 & IEEE_MASK_QNAN) return TINY_FPCLASS_QNAN;
            else                      return TINY_FPCLASS_SNAN;
        }
        else
        {
            if (i64 & IEEE_MASK_SIGN) return TINY_FPCLASS_NINF;
            else                      return TINY_FPCLASS_PINF;
        }
    }
    else
    {
        if (i64 & IEEE_MASK_SIGN)     return TINY_FPCLASS_NN;
        else                          return TINY_FPCLASS_PN;
    }
}
#endif // HAVE_IEEE_FP_FORMAT

/*
 * ---------------------------------------------------------------------------
 * * fval: copy the floating point value into a buffer and make it presentable
 */
static void fval(char *buff, char **bufc, double result)
{
    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(result);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        double rIntegerPart;
        double rFractionalPart;

        rFractionalPart = modf(result, &rIntegerPart);
        if (  0.0 == rFractionalPart
           && LONG_MIN <= rIntegerPart
           && rIntegerPart <= LONG_MAX)
        {
            long i = (long)rIntegerPart;
            safe_ltoa(i, buff, bufc);
        }
        else
        {
            safe_str(Tiny_ftoa(result, FALSE, 0), buff, bufc);
        }
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(TinyFPStrings[TINY_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

static void fval_buf(char *buff, double result)
{
    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(result);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        double rIntegerPart;
        double rFractionalPart;

        rFractionalPart = modf(result, &rIntegerPart);
        if (  -0.0000005 < rFractionalPart
           &&  rFractionalPart < 0.0000005
           &&  LONG_MIN <= rIntegerPart
           &&  rIntegerPart <= LONG_MAX)
        {
            long i = (long)rIntegerPart;
            Tiny_ltoa(i, buff);
        }
        else
        {
            strcpy(buff, Tiny_ftoa(result, FALSE, 0));
        }
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        strcpy(buff, TinyFPStrings[TINY_FPCLASS(fpc)]);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

/*
 * ---------------------------------------------------------------------------
 * * delim_check: obtain delimiter
 */
int delim_check
(
    char *fargs[], int nfargs, int sep_arg, char *sep, char *buff,
    char **bufc, int eval, dbref player, dbref cause, char *cargs[],
    int ncargs, int allow_special
)
{
    int bSuccess = 1;
    if (nfargs >= sep_arg)
    {
        // First, we decide whether to evalute fargs[sep_arg-1] or not.
        //
        char *tstr = fargs[sep_arg-1];
        int tlen = strlen(tstr);
        if (tlen <= 1)
        {
            eval = 0;
        }
        if (eval)
        {
            char *str = tstr;
            char *bp = tstr = alloc_lbuf("delim_check");
            TinyExec(tstr, &bp, player, CALLERQQQ, cause,
                     EV_EVAL | EV_FCHECK, &str, cargs, ncargs);
            *bp = '\0';
            tlen = bp - tstr;
        }

        // Regardless of evaulation or no, tstr contains the we need to
        // look at, and tlen is the length of this string.
        //
        if (tlen == 1)
        {
            *sep = tstr[0];
        }
        else if (tlen == 0)
        {
            *sep = ' ';
        }
        else
        {
            // We might have an issue.
            //
            if (tlen == 2 && allow_special)
            {
                if (memcmp(tstr, (char *)NULL_DELIM_VAR, 2) == 0)
                {
                    *sep = '\0';
                }
                else if (memcmp(tstr, (char *)"\r\n", 2) == 0)
                {
                    *sep = '\r';
                }
                else
                {
                    bSuccess = 0;
                }
            }
            else
            {
                bSuccess = 0;
            }
        }

        // Clean up the evaluation buffer.
        //
        if (eval)
        {
            free_lbuf(tstr);
        }
        if (!bSuccess)
        {
            safe_str("#-1 SEPARATOR MUST BE ONE CHARACTER", buff, bufc);
            return 0;
        }
    }
    else
    {
        *sep = ' ';
    }
    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_words: Returns number of words in a string.
 * * Added 1/28/91 Philip D. Wasson
 */

int countwords(char *str, char sep)
{
    int n;

    str = trim_space_sep(str, sep);
    if (!*str)
        return 0;
    for (n = 0; str; str = next_token(str, sep), n++) ;
    return n;
}

FUNCTION(fun_words)
{
    char sep;

    if (nfargs == 0)
    {
        safe_chr('0', buff, bufc);
        return;
    }
    varargs_preamble(2);
    safe_ltoa(countwords(strip_ansi(fargs[0]), sep), buff, bufc);
}

/*
 * fun_flags: Returns the flags on an object.
 * Because @switch is case-insensitive, not quite as useful as it could be.
 */

FUNCTION(fun_flags)
{
    dbref it = match_thing(player, fargs[0]);
    if (  it != NOTHING
       && (  mudconf.pub_flags
          || Examinable(player, it)
          || it == cause))
    {
        char *buff2 = decode_flags(player, &(db[it].fs));
        safe_str(buff2, buff, bufc);
        free_sbuf(buff2);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rand: Return a random number from 0 to arg1-1
 */

FUNCTION(fun_rand)
{
    int num = Tiny_atol(fargs[0]);
    if (num < 1)
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        safe_ltoa(RandomINT32(0,num-1), buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_abs: Returns the absolute value of its argument.
 */

FUNCTION(fun_abs)
{
    double num;

    num = Tiny_atof(fargs[0]);
    if (num == 0.0)
    {
        safe_chr('0', buff, bufc);
    }
    else if (num < 0.0)
    {
        fval(buff, bufc, -num);
    }
    else
    {
        fval(buff, bufc, num);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_sign: Returns -1, 0, or 1 based on the the sign of its argument.
 */

FUNCTION(fun_sign)
{
    double num;

    num = Tiny_atof(fargs[0]);
    if (num < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_chr((num > 0) ? '1' : '0', buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_time:
//
// With no arguments, it returns local time in the 'Ddd Mmm DD HH:MM:SS YYYY'
// format.
//
// If an argument is provided, "utc" gives a UTC time string, and "local"
// gives the local time string.
//
FUNCTION(fun_time)
{
    CLinearTimeAbsolute ltaNow;
    if (nfargs == 0 || _stricmp("utc", fargs[0]) != 0)
    {
        ltaNow.GetLocal();
    }
    else
    {
        ltaNow.GetUTC();
    }
    char *temp = ltaNow.ReturnDateString();
    safe_str(temp, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_secs.
//
// With no arguments, it returns seconds since Jan 01 00:00:00 1970 UTC not
// counting leap seconds.
//
// If an argument is provided, "utc" gives UTC seconds, and "local" gives
// an integer which corresponds to a local time string. It is not useful
// as a count, but it can be given to convsecs(secs(),raw) to get the
// corresponding time string.
//
FUNCTION(fun_secs)
{
    CLinearTimeAbsolute ltaNow;
    if (nfargs == 0 || _stricmp("local", fargs[0]) != 0)
    {
        ltaNow.GetUTC();
    }
    else
    {
        ltaNow.GetLocal();
    }
    safe_str(ltaNow.ReturnSecondsString(), buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_convsecs.
//
// With one arguments, it converts seconds from Jan 01 00:00:00 1970 UTC to a
// local time string in the 'Ddd Mmm DD HH:MM:SS YYYY' format.
//
// If a second argument is given, it is the <zonename>:
//
//   local - indicates that a conversion for timezone/DST of the server should
//           be applied (default if no second argument is given).
//
//   utc   - indicates that no timezone/DST conversions should be applied.
//           This is useful to give a unique one-to-one mapping between an
//           integer and it's corresponding text-string.
//
FUNCTION(fun_convsecs)
{
    CLinearTimeAbsolute lta;
    lta.SetSecondsString(fargs[0]);
    if (nfargs == 1 || _stricmp("utc", fargs[1]) != 0)
    {
        lta.UTC2Local();
    }
    char *temp = lta.ReturnDateString();
    safe_str(temp, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_convtime.
//
// With one argument, it converts a local time string in the format
//'[Ddd] Mmm DD HH:MM:SS YYYY' to a count of seconds from Jan 01 00:00:00 1970
// UTC.
//
// If a second argument is given, it is the <zonename>:
//
//   local - indicates that the given time string is for the local timezone
//           local DST adjustments (default if no second argument is given).
//
//   utc   - indicates that no timezone/DST conversions should be applied.
//           This is useful to give a unique one-to-one mapping between an
//           integer and it's corresponding text-string.
//
// This function returns -1 if there was a problem parsing the time string.
//
FUNCTION(fun_convtime)
{
    CLinearTimeAbsolute lta;
    BOOL bZoneSpecified = FALSE;
    if (  lta.SetString(fargs[0])
       || ParseDate(lta, fargs[0], &bZoneSpecified))
    {
        if (  !bZoneSpecified
           && (  nfargs == 1
              || _stricmp("utc", fargs[1]) != 0))
        {
            lta.Local2UTC();
        }
        safe_str(lta.ReturnSecondsString(), buff, bufc);
    }
    else
    {
        safe_str("-1", buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_starttime: What time did this system last reboot?
 */

FUNCTION(fun_starttime)
{
    char *temp = mudstate.start_time.ReturnDateString();
    safe_str(temp, buff, bufc);
}


// fun_timefmt
//
// timefmt(<format>[, <secs>])
//
// If <secs> isn't given, the current time is used. Escape sequences
// in <format> are expanded out.
//
// All escape sequences start with a $. Any unrecognized codes or other
// text will be returned unchanged.
//
const char *DayOfWeekStringLong[7] =
{
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
};
extern char *DayOfWeekString[];
extern const char *monthtab[];
const char *MonthTableLong[] =
{
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

const int Map24to12[24] =
{
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
FUNCTION(fun_timefmt)
{
    CLinearTimeAbsolute lta, ltaUTC;
    if (nfargs == 2)
    {
        ltaUTC.SetSecondsString(fargs[1]);
    }
    else
    {
        ltaUTC.GetUTC();
    }
    lta = ltaUTC;
    lta.UTC2Local();

    FIELDEDTIME ft;
    lta.ReturnFields(&ft);

    // Calculate Time Zone Info
    //
    CLinearTimeDelta ltd = lta - ltaUTC;
    int iTZSecond = ltd.ReturnSeconds();
    int iTZSign;
    if (iTZSecond < 0)
    {
        iTZSign = '-';
        iTZSecond = -iTZSecond;
    }
    else
    {
        iTZSign = '+';
    }
    int iTZHour = iTZSecond / 3600;
    iTZSecond %= 3600;
    int iTZMinute = iTZSecond/60;
    int iHour12 = Map24to12[ft.iHour];

    // Calculate Monday and Sunday-oriented week numbers.
    //
    int iWeekOfYearSunday = (ft.iDayOfYear-ft.iDayOfWeek+6)/7;
    int iDayOfWeekMonday  = (ft.iDayOfWeek == 0)?7:ft.iDayOfWeek;
    int iWeekOfYearMonday = (ft.iDayOfYear-iDayOfWeekMonday+7)/7;

    // Calculate ISO Week and Year.  Remember that the ISO Year can be the
    // same, one year ahead, or one year behind of the Gregorian Year.
    //
    int iYearISO = ft.iYear;
    int iWeekISO = 0;
    int iTemp = 0;
    if (  ft.iMonth == 12
       && 35 <= 7 + ft.iDayOfMonth - iDayOfWeekMonday)
    {
        iYearISO++;
        iWeekISO = 1;
    }
    else if (  ft.iMonth == 1
            && ft.iDayOfMonth <= 3
            && (iTemp = 7 - ft.iDayOfMonth + iDayOfWeekMonday) >= 11)
    {
        iYearISO--;
        if (  iTemp == 11
           || (  iTemp == 12
              && isLeapYear(iYearISO)))
        {
            iWeekISO = 53;
        }
        else
        {
            iWeekISO = 52;
        }
    }
    else
    {
        iWeekISO = (7 + ft.iDayOfYear - iDayOfWeekMonday)/7;
        if (4 <= (7 + ft.iDayOfYear - iDayOfWeekMonday)%7)
        {
            iWeekISO++;
        }
    }
        
    char *q;
    char *p = fargs[0];
    while ((q = strchr(p, '$')) != NULL)
    {
        size_t nLen = q - p;
        safe_copy_buf(p, nLen, buff, bufc);
        p = q;

        // Now, p points to a '$'.
        //
        p++;

        // Handle modifiers
        //
        int  iOption = 0;
        int ch = *p++;
        if (ch == '#' || ch == 'E' || ch == 'O')
        {
            iOption = ch;
            ch = *p++;
        }

        // Handle format letter.
        //
        switch (ch)
        {
        case 'a': // $a - Abbreviated weekday name
            safe_str(DayOfWeekString[ft.iDayOfWeek], buff, bufc);
            break;

        case 'A': // $A - Full weekday name
            safe_str(DayOfWeekStringLong[ft.iDayOfWeek], buff, bufc);
            break;

        case 'b': // $b - Abbreviated month name
        case 'h':
            safe_str(monthtab[ft.iMonth-1], buff, bufc);
            break;

        case 'B': // $B - Full month name
            safe_str(MonthTableLong[ft.iMonth-1], buff, bufc);
            break;

        case 'c': // $c - Date and time
            if (iOption == '#')
            {
                // Long version.
                //
                safe_tprintf_str(buff, bufc, "%s, %s %d, %d, %02d:%02d:%02d",
                    DayOfWeekStringLong[ft.iDayOfWeek],
                    MonthTableLong[ft.iMonth-1],
                    ft.iDayOfMonth, ft.iYear, ft.iHour, ft.iMinute,
                    ft.iSecond);
            }
            else
            {
                safe_str(lta.ReturnDateString(), buff, bufc);
            }
            break;

        case 'C': // $C - The century (year/100).
            safe_tprintf_str(buff, bufc, "%d", ft.iYear / 100);
            break;

        case 'd': // $d - Day of Month as decimal number (1-31)
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iDayOfMonth);
            break;

        case 'x': // $x - Date
            if (iOption == '#')
            {
                safe_tprintf_str(buff, bufc, "%s, %s %d, %d",
                    DayOfWeekStringLong[ft.iDayOfWeek],
                    MonthTableLong[ft.iMonth-1],
                    ft.iDayOfMonth, ft.iYear);
                break;
            }

            // FALL THROUGH

        case 'D': // $D - Equivalent to %m/%d/%y
            safe_tprintf_str(buff, bufc, "%02d/%02d/%02d", ft.iMonth,
                ft.iDayOfMonth, ft.iYear % 100);
            break;

        case 'e': // $e - Like $d, the day of the month as a decimal number,
                  // but a leading zero is replaced with a space.
            safe_tprintf_str(buff, bufc, "%2d", ft.iDayOfMonth);
            break;

        case 'F': // $F - The ISO 8601 formated date.
            safe_tprintf_str(buff, bufc, "%d-%02d-%02d", ft.iYear, ft.iMonth,
                ft.iDayOfMonth);
            break;

        case 'g': // $g - Like $G, two-digit ISO 8601 year.
            safe_tprintf_str(buff, bufc, "%02d", iYearISO%100);
            break;

        case 'G': // $G - The ISO 8601 year.
            safe_tprintf_str(buff, bufc, "%04d", iYearISO);
            break;

        case 'H': // $H - Hour of the 24-hour day.
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d", ft.iHour);
            break;

        case 'I': // $I - Hour of the 12-hour day
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d", iHour12);
            break;

        case 'j': // $j - Day of the year.
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%03d",
                ft.iDayOfYear);
            break;

        case 'k': // $k - Hour of the 24-hour day. Pad with a space.
            safe_tprintf_str(buff, bufc, "%2d", ft.iHour);
            break;

        case 'l': // $l - Hour of the 12-hour clock. Pad with a space.
            safe_tprintf_str(buff, bufc, "%2d", iHour12);
            break;

        case 'm': // $m - Month of the year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iMonth);
            break;

        case 'M': // $M - Minutes after the hour
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iMinute);
            break;

        case 'n': // $n - Newline.
            safe_str("\r\n", buff, bufc);
            break;

        case 'p': // $p - AM/PM
            safe_str((ft.iHour < 12)?"AM":"PM", buff, bufc);
            break;

        case 'P': // $p - am/pm
            safe_str((ft.iHour < 12)?"am":"pm", buff, bufc);
            break;

        case 'r': // $r - Equivalent to $I:$M:$S $p
            safe_tprintf_str(buff, bufc,
                (iOption=='#')?"%d:%02d:%02d %s":"%02d:%02d:%02d %s",
                iHour12, ft.iMinute, ft.iSecond,
                (ft.iHour<12)?"AM":"PM");
            break;

        case 'R': // $R - Equivalent to $H:$M
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d:%02d":"%02d:%02d",
                ft.iHour, ft.iMinute);
            break;

        case 's': // $s - Number of seconds since the epoch.
            safe_str(ltaUTC.ReturnSecondsString(), buff, bufc);
            break;

        case 'S': // $S - Seconds after the minute
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iSecond);
            break;

        case 't':
            safe_chr('\t', buff, bufc);
            break;

        case 'X': // $X - Time
        case 'T': // $T - Equivalent to $H:$M:$S
            safe_tprintf_str(buff, bufc,
                (iOption=='#')?"%d:%02d:%02d":"%02d:%02d:%02d",
                ft.iHour, ft.iMinute, ft.iSecond);
            break;

        case 'u': // $u - Day of the Week, range 1 to 7. Monday = 1.
            safe_ltoa(iDayOfWeekMonday, buff, bufc);
            break;

        case 'U': // $U - Week of the year from 1st Sunday
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                iWeekOfYearSunday);
            break;

        case 'V': // $V - ISO 8601:1988 week number.
            safe_tprintf_str(buff, bufc, "%02d", iWeekISO);
            break;

        case 'w': // $w - Day of the week. 0 = Sunday
            safe_ltoa(ft.iDayOfWeek, buff, bufc);
            break;

        case 'W': // $W - Week of the year from 1st Monday
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                iWeekOfYearMonday);
            break;

        case 'y': // $y - Two-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iYear % 100);
            break;

        case 'Y': // $Y - All-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%04d",
                ft.iYear);
            break;

        case 'z': // $z - Time zone
            safe_tprintf_str(buff, bufc, "%c%02d%02d", iTZSign, iTZHour,
                iTZMinute);
            break;

        case 'Z': // $Z - Time zone name
            // TODO
            break;

        case '$': // $$
            safe_chr(ch, buff, bufc);
            break;

        default:
            safe_chr('$', buff, bufc);
            p = q + 1;
            break;
        }
    }
    safe_str(p, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_get, fun_get_eval: Get attribute from object.
 */
int check_read_perms
(
    dbref player,
    dbref thing,
    ATTR *attr,
    int aowner,
    int aflags,
    char *buff,
    char **bufc
)
{
    // If we have explicit read permission to the attr, return it.
    //
    if (See_attr_explicit(player, thing, attr, aowner, aflags))
    {
        return 1;
    }

    // If we are nearby or have examine privs to the attr and it is
    // visible to us, return it.
    //
    int see_it = See_attr(player, thing, attr, aowner, aflags);
    if (  (  Examinable(player, thing)
          || nearby(player, thing)
          || See_All(player))
       && see_it)
    {
        return 1;
    }

    // For any object, we can read its visible attributes, EXCEPT for
    // descs, which are only visible if read_rem_desc is on.
    //
    if (see_it)
    {
        if (  !mudconf.read_rem_desc
           && attr->number == A_DESC)
        {
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return 0;
        }
        else
        {
            return 1;
        }
    }
    safe_noperm(buff, bufc);
    return 0;
}

FUNCTION(fun_get)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags;
    ATTR *attr;
    char *atr_gotten;
    struct boolexp *pBoolExp;

    if (!parse_attrib(player, fargs[0], &thing, &attrib))
    {
        safe_nomatch(buff, bufc);
        return;
    }
    if (attrib == NOTHING)
    {
        return;
    }
    free_buffer = 1;

    // We need the attr's flags for this:
    //
    attr = atr_num(attrib);
    if (!attr)
    {
        return;
    }
    int nLen = 0;
    if (attr->flags & AF_IS_LOCK)
    {
        atr_gotten = atr_get_LEN(thing, attrib, &aowner, &aflags, &nLen);
        if (Read_attr(player, thing, attr, aowner, aflags))
        {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        }
        else
        {
            free_lbuf(atr_gotten);
            // TODO: This is bad practice.
            atr_gotten = (char *)FUNC_NOPERM_MESSAGE;
        }
        free_buffer = 0;
    }
    else
    {
        atr_gotten = atr_pget_LEN(thing, attrib, &aowner, &aflags, &nLen);
    }

    // Perform access checks.  c_r_p fills buff with an error message
    // if needed.
    //
    if (check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
    {
        if (free_buffer)
        {
            if (nLen)
            {
                safe_copy_buf(atr_gotten, nLen, buff, bufc);
            }
        }
        else
        {
            safe_str(atr_gotten, buff, bufc);
        }
    }
    if (free_buffer)
    {
        free_lbuf(atr_gotten);
    }
}

FUNCTION(fun_xget)
{
    dbref thing, aowner;
    int attrib, aflags;
    ATTR *attr;
    char *atr_gotten;
    struct boolexp *pBoolExp;

    if (!*fargs[0] || !*fargs[1])
    {
        return;
    }

    if (!parse_attrib(player, tprintf("%s/%s", fargs[0], fargs[1]), &thing, &attrib))
    {
        safe_nomatch(buff, bufc);
        return;
    }
    if (attrib == NOTHING)
    {
        return;
    }
    // We need the attr's flags for this:
    attr = atr_num(attrib);
    if (!attr)
    {
        return;
    }
    int free_buffer = 1;
    int nLen = 0;
    if (attr->flags & AF_IS_LOCK)
    {
        atr_gotten = atr_get_LEN(thing, attrib, &aowner, &aflags, &nLen);
        if (Read_attr(player, thing, attr, aowner, aflags))
        {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        }
        else
        {
            free_lbuf(atr_gotten);
            // TODO: This is bad practice.
            atr_gotten = (char *)FUNC_NOPERM_MESSAGE;
        }
        free_buffer = 0;
    }
    else
    {
        atr_gotten = atr_pget_LEN(thing, attrib, &aowner, &aflags, &nLen);
    }

    // Perform access checks.  c_r_p fills buff with an error message
    // if needed.
    //
    if (check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
    {
        if (free_buffer)
        {
            safe_copy_buf(atr_gotten, nLen, buff, bufc);
        }
        else
        {
            safe_str(atr_gotten, buff, bufc);
        }
    }
    if (free_buffer)
    {
        free_lbuf(atr_gotten);
    }
}

FUNCTION(fun_get_eval)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags, eval_it;
    ATTR *attr;
    char *atr_gotten, *str;
    struct boolexp *pBoolExp;

    if (!parse_attrib(player, fargs[0], &thing, &attrib))
    {
        safe_nomatch(buff, bufc);
        return;
    }
    if (attrib == NOTHING)
    {
        return;
    }
    free_buffer = 1;
    eval_it = 1;
    attr = atr_num(attrib); // We need the attr's flags for this:
    if (!attr)
    {
        return;
    }
    if (attr->flags & AF_IS_LOCK)
    {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags))
        {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        }
        else
        {
            free_lbuf(atr_gotten);
            // TODO: This is bad practice.
            atr_gotten = (char *)FUNC_NOPERM_MESSAGE;
        }
        free_buffer = 0;
        eval_it = 0;
    }
    else
    {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }
    if (!check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
    {
        if (free_buffer)
        {
            free_lbuf(atr_gotten);
        }
        return;
    }
    if (eval_it)
    {
        str = atr_gotten;
        TinyExec(buff, bufc, thing, CALLERQQQ, player, EV_FIGNORE | EV_EVAL,
                 &str, (char **)NULL, 0);
    }
    else
    {
        safe_str(atr_gotten, buff, bufc);
    }
    if (free_buffer)
        free_lbuf(atr_gotten);
    return;
}

FUNCTION(fun_subeval)
{
    char *str = fargs[0];
    TinyExec(buff, bufc, player, CALLERQQQ, cause,
             EV_EVAL|EV_NO_LOCATION|EV_NOFCHECK|EV_FIGNORE|EV_NO_COMPRESS,
             &str, (char **)NULL, 0);
}

FUNCTION(fun_eval)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags, eval_it;
    ATTR *attr;
    char *atr_gotten, *str;
    struct boolexp *pBoolExp;

    if (nfargs == 1)
    {
        str = fargs[0];
        TinyExec(buff, bufc, player, CALLERQQQ, cause, EV_EVAL, &str,
                 (char **)NULL, 0);
        return;
    }
    if (!*fargs[0] || !*fargs[1])
    {
        return;
    }

    if (!parse_attrib(player, tprintf("%s/%s", fargs[0], fargs[1]),
              &thing, &attrib))
    {
        safe_nomatch(buff, bufc);
        return;
    }
    if (attrib == NOTHING)
    {
        return;
    }
    free_buffer = 1;
    eval_it = 1;
    attr = atr_num(attrib);
    if (!attr)
    {
        return;
    }
    if (attr->flags & AF_IS_LOCK)
    {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags))
        {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        }
        else
        {
            free_lbuf(atr_gotten);
            // TODO: This is bad practice.
            atr_gotten = (char *)FUNC_NOPERM_MESSAGE;
        }
        free_buffer = 0;
        eval_it = 0;
    }
    else
    {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }
    if (!check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
    {
        if (free_buffer)
        {
            free_lbuf(atr_gotten);
        }
        return;
    }
    if (eval_it)
    {
        str = atr_gotten;
        TinyExec(buff, bufc, thing, CALLERQQQ, player, EV_FIGNORE | EV_EVAL,
                 &str, (char **)NULL, 0);
    }
    else
    {
        safe_str(atr_gotten, buff, bufc);
    }
    if (free_buffer)
    {
        free_lbuf(atr_gotten);
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_u and fun_ulocal:  Call a user-defined function.
 */

static void do_ufun(char *buff, char **bufc, dbref player, dbref cause,
            char *fargs[], int nfargs,
            char *cargs[], int ncargs,
            int is_local)
{
    dbref aowner, thing;
    int aflags, anum;
    ATTR *ap;
    char *atext, *str;
    char *preserve[MAX_GLOBAL_REGS];
    int preserve_len[MAX_GLOBAL_REGS];

    // Two possibilities for the first arg: <obj>/<attr> and <attr>.
    //
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return;
    }

    // Use it if we can access it, otherwise return an error.
    //
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (*atext == '\0')
    {
        free_lbuf(atext);
        return;
    }
    if (!check_read_perms(player, thing, ap, aowner, aflags, buff, bufc))
    {
        free_lbuf(atext);
        return;
    }

    // If we're evaluating locally, preserve the global registers.
    //
    if (is_local)
    {
        save_global_regs("fun_ulocal_save", preserve, preserve_len);
    }

    // Evaluate it using the rest of the passed function args.
    //
    str = atext;
    TinyExec(buff, bufc, thing, CALLERQQQ, cause, EV_FCHECK | EV_EVAL,
        &str, &(fargs[1]), nfargs - 1);
    free_lbuf(atext);

    // If we're evaluating locally, restore the preserved registers.
    //
    if (is_local)
    {
        restore_global_regs("fun_ulocal_restore", preserve, preserve_len);
    }
}

FUNCTION(fun_u)
{
    do_ufun(buff, bufc, player, cause, fargs, nfargs, cargs, ncargs, 0);
}

FUNCTION(fun_ulocal)
{
    do_ufun(buff, bufc, player, cause, fargs, nfargs, cargs, ncargs, 1);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parent: Get parent of object.
 */

FUNCTION(fun_parent)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (Good_obj(it) && (Examinable(player, it) || (it == cause))) {
        safe_tprintf_str(buff, bufc, "#%d", Parent(it));
    } else {
        safe_nothing(buff, bufc);
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mid: mid(foobar,2,3) returns oba
 */

FUNCTION(fun_mid)
{
    // Initial checks for iPosition0 [0,LBUF_SIZE), nLength [0,LBUF_SIZE),
    // and iPosition1 [0,LBUF_SIZE).
    //
    int iPosition0 = Tiny_atol(fargs[1]);
    int nLength = Tiny_atol(fargs[2]);
    int iPosition1 = iPosition0 + nLength;

    if (  iPosition0 < 0 || iPosition0 > LBUF_SIZE-1
       || nLength    < 0 || nLength    > LBUF_SIZE-1
       || iPosition1 > LBUF_SIZE-1)
    {
        safe_str("#-1 OUT OF RANGE", buff, bufc);
        return;
    }

    // At this point, iPosition0, nLength, and iPosition1 are reasonable
    // numbers which may -still- not refer to valid data in the string.
    //
    struct ANSI_In_Context aic;
    ANSI_String_In_Init(&aic, fargs[0], ANSI_ENDGOAL_NORMAL);
    int nDone;
    ANSI_String_Skip(&aic, iPosition0, &nDone);
    if (nDone < iPosition0)
    {
        return;
    }

    struct ANSI_Out_Context aoc;
    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    ANSI_String_Out_Init(&aoc, *bufc, nBufferAvailable, nLength, ANSI_ENDGOAL_NORMAL);
    ANSI_String_Copy(&aoc, &aic, nBufferAvailable, nLength);
    int nSize = ANSI_String_Finalize(&aoc, &nDone);
    *bufc += nSize;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_first: Returns first word in a string
 */

FUNCTION(fun_first)
{
    char *s, *first, sep;

    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }
    varargs_preamble(2);
    s = trim_space_sep(fargs[0], sep);
    first = split_token(&s, sep);
    if (first)
    {
        safe_str(first, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rest: Returns all but the first word in a string
 */


FUNCTION(fun_rest)
{
    char *s, *first, sep;

    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }
    varargs_preamble(2);
    s = trim_space_sep(fargs[0], sep);  /*
                         * leading spaces ...
                         */
    first = split_token(&s, sep);
    if (s) {
        safe_str(s, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_v: Function form of %-substitution
 */

FUNCTION(fun_v)
{
    dbref aowner;
    int aflags;
    char *sbuf, *sbufc, *tbuf, *str;
    ATTR *ap;

    tbuf = fargs[0];
    if (Tiny_IsFirstAttributeNameCharacter[(unsigned char)tbuf[0]] && tbuf[1])
    {
        // Fetch an attribute from me. First see if it exists,
        // returning a null string if it does not.
        //
        ap = atr_str(fargs[0]);
        if (!ap)
        {
            return;
        }

        // If we can access it, return it, otherwise return a null
        // string.
        //
        int nLen;
        tbuf = atr_pget_LEN(player, ap->number, &aowner, &aflags, &nLen);
        if (See_attr(player, player, ap, aowner, aflags))
        {
            safe_copy_buf(tbuf, nLen, buff, bufc);
        }
        free_lbuf(tbuf);
        return;
    }

    // Not an attribute, process as %<arg>
    //
    sbuf = alloc_sbuf("fun_v");
    sbufc = sbuf;
    safe_sb_chr('%', sbuf, &sbufc);
    safe_sb_str(fargs[0], sbuf, &sbufc);
    *sbufc = '\0';
    str = sbuf;
    TinyExec(buff, bufc, player, CALLERQQQ, cause, EV_EVAL|EV_FIGNORE, &str,
             cargs, ncargs);
    free_sbuf(sbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_s: Force substitution to occur.
 */

FUNCTION(fun_s)
{
    char *str = fargs[0];
    TinyExec(buff, bufc, player, CALLERQQQ, cause, EV_FIGNORE | EV_EVAL, &str,
             cargs, ncargs);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_con: Returns first item in contents list of object/room
 */

FUNCTION(fun_con)
{
    dbref it;

    it = match_thing(player, fargs[0]);

    if ((it != NOTHING) &&
        (Has_contents(it)) &&
        (Examinable(player, it) ||
         (where_is(player) == it) ||
         (it == cause))) {
        safe_tprintf_str(buff, bufc, "#%d", Contents(it));
        return;
    }
    safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_exit: Returns first exit in exits list of room.
 */

FUNCTION(fun_exit)
{
    dbref it, exit;
    int key;

    it = match_thing(player, fargs[0]);
    if (Good_obj(it) && Has_exits(it) && Good_obj(Exits(it))) {
        key = 0;
        if (Examinable(player, it))
            key |= VE_LOC_XAM;
        if (Dark(it))
            key |= VE_LOC_DARK;
        DOLIST(exit, Exits(it)) {
            if (exit_visible(exit, player, key)) {
                safe_tprintf_str(buff, bufc, "#%d", exit);
                return;
            }
        }
    }
    safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_next: return next thing in contents or exits chain
 */

FUNCTION(fun_next)
{
    dbref it, loc, exit, ex_here;
    int key;

    it = match_thing(player, fargs[0]);
    if (Good_obj(it) && Has_siblings(it)) {
        loc = where_is(it);
        ex_here = Good_obj(loc) ? Examinable(player, loc) : 0;
        if (ex_here || (loc == player) || (loc == where_is(player))) {
            if (!isExit(it)) {
                safe_tprintf_str(buff, bufc, "#%d", Next(it));
                return;
            } else {
                key = 0;
                if (ex_here)
                    key |= VE_LOC_XAM;
                if (Dark(loc))
                    key |= VE_LOC_DARK;
                DOLIST(exit, it) {
                    if ((exit != it) &&
                      exit_visible(exit, player, key)) {
                        safe_tprintf_str(buff, bufc, "#%d", exit);
                        return;
                    }
                }
            }
        }
    }
    safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_loc: Returns the location of something
 */

FUNCTION(fun_loc)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (locatable(player, it, cause))
        safe_tprintf_str(buff, bufc, "#%d", Location(it));
    else
        safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_where: Returns the "true" location of something
 */

FUNCTION(fun_where)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (locatable(player, it, cause))
        safe_tprintf_str(buff, bufc, "#%d", where_is(it));
    else
        safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rloc: Returns the recursed location of something (specifying #levels)
 */

FUNCTION(fun_rloc)
{
    int i, levels;
    dbref it;

    levels = Tiny_atol(fargs[1]);
    if (levels > mudconf.ntfy_nest_lim)
        levels = mudconf.ntfy_nest_lim;

    it = match_thing(player, fargs[0]);
    if (locatable(player, it, cause)) {
        for (i = 0; i < levels; i++) {
            if (!Good_obj(it) || !Has_location(it))
                break;
            it = Location(it);
        }
        safe_tprintf_str(buff, bufc, "#%d", it);
        return;
    }
    safe_nothing(buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_room: Find the room an object is ultimately in.
 */

FUNCTION(fun_room)
{
    dbref it;
    int count;

    it = match_thing(player, fargs[0]);
    if (locatable(player, it, cause)) {
        for (count = mudconf.ntfy_nest_lim; count > 0; count--) {
            it = Location(it);
            if (!Good_obj(it))
                break;
            if (isRoom(it)) {
                safe_tprintf_str(buff, bufc, "#%d", it);
                return;
            }
        }
        safe_nothing(buff, bufc);
    } else if (isRoom(it)) {
        safe_tprintf_str(buff, bufc, "#%d", it);
    } else {
        safe_nothing(buff, bufc);
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_owner: Return the owner of an object.
 */

FUNCTION(fun_owner)
{
    dbref it, aowner;
    int atr, aflags;

    if (parse_attrib(player, fargs[0], &it, &atr)) {
        if (atr == NOTHING) {
            it = NOTHING;
        } else {
            atr_pget_info(it, atr, &aowner, &aflags);
            it = aowner;
        }
    } else {
        it = match_thing(player, fargs[0]);
        if (it != NOTHING)
            it = Owner(it);
    }
    safe_tprintf_str(buff, bufc, "#%d", it);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_controls: Does x control y?
 */

FUNCTION(fun_controls)
{
    dbref x = match_thing(player, fargs[0]);
    if (x == NOTHING)
    {
        safe_tprintf_str(buff, bufc, "%s", "#-1 ARG1 NOT FOUND");
        return;
    }
    dbref y = match_thing(player, fargs[1]);
    if (y == NOTHING)
    {
        safe_tprintf_str(buff, bufc, "%s", "#-1 ARG2 NOT FOUND");
        return;
    }
    safe_ltoa(Controls(x, y), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_fullname: Return the fullname of an object (good for exits)
 */

FUNCTION(fun_fullname)
{
    dbref it = match_thing(player, fargs[0]);
    if (it == NOTHING)
    {
        return;
    }
    if (!mudconf.read_rem_name)
    {
        if (  !nearby_or_control(player, it)
           && !isPlayer(it))
        {
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return;
        }
    }
    safe_str(Name(it), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_name: Return the name of an object
 */

FUNCTION(fun_name)
{
    dbref it;
    char *s, *temp;

    it = match_thing(player, fargs[0]);
    if (it == NOTHING) {
        return;
    }
    if (!mudconf.read_rem_name) {
        if (!nearby_or_control(player, it) && !isPlayer(it) && !Long_Fingers(player)) {
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return;
        }
    }
    temp = *bufc;
    safe_str(Name(it), buff, bufc);
    if (isExit(it))
    {
        for (s = temp; (s != *bufc) && (*s != ';'); s++)
        {
            // Do nothing
            //
            ;
        }
        if (*s == ';')
            *bufc = s;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_match, fun_strmatch: Match arg2 against each word of arg1 returning
 * * index of first match, or against the whole string.
 */

FUNCTION(fun_match)
{
    char sep;
    varargs_preamble(3);

    // Check each word individually, returning the word number of the first
    // one that matches.  If none match, return 0.
    //
    int wcount = 1;
    char *s = trim_space_sep(fargs[0], sep);
    do {
        char *r = split_token(&s, sep);
        if (quick_wild(fargs[1], r))
        {
            safe_ltoa(wcount, buff, bufc);
            return;
        }
        wcount++;
    } while (s);
    safe_chr('0', buff, bufc);
}

FUNCTION(fun_strmatch)
{
    // Check if we match the whole string.  If so, return 1.
    //
    int cc = quick_wild(fargs[1], fargs[0]);
    safe_chr(cc ? '1' : '0', buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_extract: extract words from string:
 * * extract(foo bar baz,1,2) returns 'foo bar'
 * * extract(foo bar baz,2,1) returns 'bar'
 * * extract(foo bar baz,2,2) returns 'bar baz'
 * *
 * * Now takes optional separator extract(foo-bar-baz,1,2,-) returns 'foo-bar'
 */

FUNCTION(fun_extract)
{
    int start, len;
    char *r, *s, *t, sep;

    varargs_preamble(4);

    s = fargs[0];
    start = Tiny_atol(fargs[1]);
    len = Tiny_atol(fargs[2]);

    if ((start < 1) || (len < 1))
    {
        return;
    }

    // Skip to the start of the string to save.
    //
    start--;
    s = trim_space_sep(s, sep);
    while (start && s)
    {
        s = next_token(s, sep);
        start--;
    }

    // If we ran of the end of the string, return nothing.
    //
    if (!s || !*s)
    {
        return;
    }

    // Count off the words in the string to save.
    //
    r = s;
    len--;
    while (len && s)
    {
        s = next_token(s, sep);
        len--;
    }

    // Chop off the rest of the string, if needed.
    //
    if (s && *s)
    {
        t = split_token(&s, sep);
    }
    safe_str(r, buff, bufc);
}

int xlate(char *arg)
{
    int temp;
    char *temp2;

    if (arg[0] == '#')
    {
        arg++;
        if (is_integer(arg, NULL))
        {
            temp = Tiny_atol(arg);
            if (temp == -1)
            {
                temp = 0;
            }
            return temp;
        }
        return 0;
    }
    temp2 = trim_space_sep(arg, ' ');
    if (!*temp2)
    {
        return 0;
    }
    if (is_integer(temp2, NULL))
    {
        return Tiny_atol(temp2);
    }
    return 1;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_index:  like extract(), but it works with an arbitrary separator.
 * * index(a b | c d e | f gh | ij k, |, 2, 1) => c d e
 * * index(a b | c d e | f gh | ij k, |, 2, 2) => c d e | f g h
 */

FUNCTION(fun_index)
{
    int start, end;
    char c, *s, *p;

    s = fargs[0];
    c = *fargs[1];
    start = Tiny_atol(fargs[2]);
    end = Tiny_atol(fargs[3]);

    if ((start < 1) || (end < 1) || (*s == '\0'))
    {
        return;
    }
    if (c == '\0')
    {
        c = ' ';
    }

    // Move s to point to the start of the item we want.
    //
    start--;
    while (start && s && *s)
    {
        if ((s = strchr(s, c)) != NULL)
        {
            s++;
        }
        start--;
    }

    // Skip over just spaces.
    //
    while (s && (*s == ' '))
    {
        s++;
    }
    if (!s || !*s)
    {
        return;
    }

    // Figure out where to end the string.
    //
    p = s;
    while (end && p && *p)
    {
        if ((p = (char *)strchr(p, c)) != NULL)
        {
            if (--end == 0)
            {
                do {
                    p--;
                } while ((*p == ' ') && (p > s));
                *(++p) = '\0';
                safe_str(s, buff, bufc);
                return;
            }
            else
            {
                p++;
            }
        }
    }

    // if we've gotten this far, we've run off the end of the string.
    //
    safe_str(s, buff, bufc);
}


FUNCTION(fun_cat)
{
    if (nfargs)
    {
        safe_str(fargs[0], buff, bufc);
        for (int i = 1; i < nfargs; i++)
        {
            safe_chr(' ', buff, bufc);
            safe_str(fargs[i], buff, bufc);
        }
    }
}

FUNCTION(fun_version)
{
    safe_str(mudstate.version, buff, bufc);
}

FUNCTION(fun_strlen)
{
    unsigned int n = 0;
    if (nfargs >= 1)
    {
        strip_ansi(fargs[0], &n);
    }
    safe_ltoa(n, buff, bufc);
}

FUNCTION(fun_num)
{
    safe_tprintf_str(buff, bufc, "#%d", match_thing(player, fargs[0]));
}

void internalPlayerFind
(
    char* buff,
    char** bufc,
    dbref player,
    char* name,
    int bVerifyPlayer
)
{
    dbref thing;
    if (*name == '#')
    {
        thing = match_thing(player, name);
        if (bVerifyPlayer)
        {
            if (!Good_obj(thing) || !isPlayer(thing))
            {
                safe_nomatch(buff, bufc);
                return;
            }
        }
    }
    else
    {
        char *nptr = name;
        if (*nptr == '*')
        {
            // Start with the second character in the name string.
            //
            nptr++;
        }
        thing = lookup_player(player, nptr, 1);
        if (  (thing == NOTHING)
           || (!isPlayer(thing) && bVerifyPlayer))
        {
            safe_nomatch(buff, bufc);
            return;
        }
    }
    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    DbrefToBuffer_Add(&pContext, thing);
    DbrefToBuffer_Final(&pContext);
}


FUNCTION(fun_pmatch)
{
    internalPlayerFind(buff, bufc, player, fargs[0], TRUE);
}

FUNCTION(fun_pfind)
{
    internalPlayerFind(buff, bufc, player, fargs[0], FALSE);
}

FUNCTION(fun_gt)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) > Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (Tiny_atof(fargs[0]) > Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_gte)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) >= Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (Tiny_atof(fargs[0]) >= Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_lt)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) < Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (Tiny_atof(fargs[0]) < Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_lte)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) <= Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (Tiny_atof(fargs[0]) <= Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_eq)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) == Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (  strcmp(fargs[0], fargs[1]) == 0
            || Tiny_atof(fargs[0]) == Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_neq)
{
    int ch = '0';
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        if (Tiny_atol(fargs[0]) != Tiny_atol(fargs[1]))
        {
            ch = '1';
        }
    }
    else if (  strcmp(fargs[0], fargs[1]) != 0
            && Tiny_atof(fargs[0]) != Tiny_atof(fargs[1]))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

FUNCTION(fun_and)
{
    int val = TRUE;
    for (int i = 0; i < nfargs; i++)
    {
        val = val && Tiny_atol(fargs[i]);
    }
    safe_chr(val ? '1' : '0', buff, bufc);
}

FUNCTION(fun_or)
{
    int val = FALSE;
    for (int i = 0; i < nfargs; i++)
    {
        val = val || Tiny_atol(fargs[i]);
    }
    safe_chr(val ? '1' : '0', buff, bufc);
}

FUNCTION(fun_xor)
{
    int val = FALSE;
    for (int i = 0; i < nfargs; i++)
    {
        int tval = Tiny_atol(fargs[i]);
        val = (val && !tval) || (!val && tval);
    }
    safe_chr(val ? '1' : '0', buff, bufc);
}

FUNCTION(fun_not)
{
    safe_ltoa(!xlate(fargs[0]), buff, bufc);
}

FUNCTION(fun_t)
{
    if (  nfargs <= 0
       || fargs[0][0] == '\0')
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        safe_ltoa(!!xlate(fargs[0]), buff, bufc);
    }
}

// Compare for decreasing order by absolute value.
//
static int DCL_CDECL f_comp_abs(const void *s1, const void *s2)
{
    double a = fabs(*(double *)s1);
    double b = fabs(*(double *)s2);

    if (a > b)
    {
        return -1;
    }
    else if (a < b)
    {
        return 1;
    }
    return 0;
}

static double AddWithError(double& err, double a, double b)
{
    double sum = a+b;
    err = b-(sum-a);
    return sum;
}

// Typically, we are within 1ulp of an exact answer, find the shortest answer
// within that 1 ulp (that is, within 0, +ulp, and -ulp).
//
static double NearestPretty(double R)
{
    char *rve = NULL;
    int decpt;
    int bNegative;
    const int mode = 0;

    double ulpR = ulp(R);
    double R0 = R-ulpR;
    double R1 = R+ulpR;

    // R.
    //
    char *p = Tiny_dtoa(R, mode, 50, &decpt, &bNegative, &rve);
    int nDigits = rve - p;

    // R-ulp(R)
    //
    p = Tiny_dtoa(R0, mode, 50, &decpt, &bNegative, &rve);
    if (rve - p < nDigits)
    {
        nDigits = rve - p;
        R  = R0;
    }

    // R+ulp(R)
    //
    p = Tiny_dtoa(R1, mode, 50, &decpt, &bNegative, &rve);
    if (rve - p < nDigits)
    {
        nDigits = rve - p;
        R = R1;
    }
    return R;
}

// Double compensation method. Extended by Priest from Knuth and Kahan.
//
// Error of sum is less than 2*epsilon or 1 ulp except for very large n.
// Return the result that yields the shortest number of base-10 digits.
//
static double AddDoubles(int n, double pd[])
{
    qsort(pd, n, sizeof(double), f_comp_abs);
    double sum = pd[0];
    double sum_err = 0.0;
    int i;
    for (i = 1; i < n; i++)
    {
        double addend_err;
        double addend = AddWithError(addend_err, sum_err, pd[i]);
        double sum1_err;
        double sum1 = AddWithError(sum1_err, sum, addend);
        sum = AddWithError(sum_err, sum1, addend_err + sum1_err);
    }
    return NearestPretty(sum);
}

static double g_aDoubles[(LBUF_SIZE+1)/2];

/*-------------------------------------------------------------------------
 * List-based numeric functions.
 */

FUNCTION(fun_ladd)
{
    char sep;
    varargs_preamble(2);

    int n = 0;
    char *cp = trim_space_sep(fargs[0], sep);
    while (cp)
    {
        char *curr = split_token(&cp, sep);
        g_aDoubles[n++] = Tiny_atof(curr);
    }
    fval(buff, bufc, AddDoubles(n, g_aDoubles));
}

FUNCTION(fun_sqrt)
{
    double val;

    val = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        fval(buff, bufc, sqrt(val));
    }
#else
    fval(buff, bufc, sqrt(val));
#endif
}

static const long nMaximums[10] =
{
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

FUNCTION(fun_add)
{
    int i;
    for (i = 0; i < nfargs; i++)
    {
        int nDigits;
        long nMaxValue = 0;
        if (  !is_integer(fargs[i], &nDigits)
           || nDigits > 9
           || (nMaxValue += nMaximums[nDigits]) > 999999999L)
        {
            // Do it the slow way.
            //
            for (int j = 0; j < nfargs; j++)
            {
                g_aDoubles[j] = Tiny_atof(fargs[j]);
            }
            fval(buff, bufc, AddDoubles(nfargs, g_aDoubles));
            return;
        }
    }

    // We can do it the fast way.
    //
    long sum = 0;
    for (i = 0; i < nfargs; i++)
    {
        sum += Tiny_atol(fargs[i]);
    }
    safe_ltoa(sum, buff, bufc);
}

FUNCTION(fun_sub)
{
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        int iResult;
        iResult = Tiny_atol(fargs[0]) - Tiny_atol(fargs[1]);
        safe_ltoa(iResult, buff, bufc);
    }
    else
    {
        g_aDoubles[0] = Tiny_atof(fargs[0]);
        g_aDoubles[1] = -Tiny_atof(fargs[1]);
        fval(buff, bufc, AddDoubles(2, g_aDoubles));
    }
}

FUNCTION(fun_mul)
{
    double prod = 1.0;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= Tiny_atof(fargs[i]);
    }
    fval(buff, bufc, NearestPretty(prod));
}

FUNCTION(fun_floor)
{
    double r = floor(Tiny_atof(fargs[0]));
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(r);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", r);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(TinyFPStrings[TINY_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_ceil)
{
    double r = ceil(Tiny_atof(fargs[0]));
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(r);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", r);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(TinyFPStrings[TINY_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_round)
{
    double r = Tiny_atof(fargs[0]);
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(r);
    if (  TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS
       || TINY_FPGROUP(fpc) == TINY_FPGROUP_ZERO)
    {
        if (TINY_FPGROUP(fpc) == TINY_FPGROUP_ZERO)
        {
            r = 0.0;
        }
#endif // HAVE_IEEE_FP_FORMAT
        int frac = Tiny_atol(fargs[1]);
        safe_str(Tiny_ftoa(r, TRUE, frac), buff, bufc);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(TinyFPStrings[TINY_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_trunc)
{
    double rArg = Tiny_atof(fargs[0]);
    double rIntegerPart;
    double rFractionalPart;

    rFractionalPart = modf(rArg, &rIntegerPart);
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(rIntegerPart);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", rIntegerPart);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(TinyFPStrings[TINY_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_idiv)
{
    INT64 bot, top;

    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str("#-1 DIVIDE BY ZERO", buff, bufc);
    }
    else
    {
        top = Tiny_atoi64(fargs[0]);
        top = i64Division(top, bot);
        safe_i64toa(top, buff, bufc);
    }
}

FUNCTION(fun_floordiv)
{
    INT64 bot, top;

    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str("#-1 DIVIDE BY ZERO", buff, bufc);
    }
    else
    {
        top = Tiny_atoi64(fargs[0]);
        top = i64FloorDivision(top, bot);
        safe_i64toa(top, buff, bufc);
    }
}

FUNCTION(fun_fdiv)
{
    double bot = Tiny_atof(fargs[1]);
    double top = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (bot == 0.0)
    {
        if (top > 0.0)
        {
            safe_str("+Inf", buff, bufc);
        }
        else if (top < 0.0)
        {
            safe_str("-Inf", buff, bufc);
        }
        else
        {
            safe_str("Ind", buff, bufc);
        }
    }
    else
    {
        fval(buff, bufc, top/bot);
    }
#else
    fval(buff, bufc, top/bot);
#endif
}

FUNCTION(fun_mod)
{
    INT64 bot, top;

    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        bot = 1;
    }
    top = Tiny_atoi64(fargs[0]);
    top = i64Mod(top, bot);
    safe_i64toa(top, buff, bufc);
}

FUNCTION(fun_remainder)
{
    INT64 bot, top;

    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        bot = 1;
    }
    top = Tiny_atoi64(fargs[0]);
    top = i64Remainder(top, bot);
    safe_i64toa(top, buff, bufc);
  }

FUNCTION(fun_pi)
{
    safe_str("3.141592653589793", buff, bufc);
}
FUNCTION(fun_e)
{
    safe_str("2.718281828459045", buff, bufc);
}

static double ConvertRDG2R(double d, const char *szUnits)
{
    switch (Tiny_ToLower[(unsigned char)szUnits[0]])
    {
    case 'd':
        // Degrees to Radians.
        //
        d *= 0.017453292519943295;
        break;

    case 'g':
        // Gradians to Radians.
        //
        d *= 0.011570796326794896;
        break;
    }
    return d;
}

static double ConvertR2RDG(double d, const char *szUnits)
{
    switch (Tiny_ToLower[(unsigned char)szUnits[0]])
    {
    case 'd':
        // Radians to Degrees.
        //
        d *= 57.29577951308232;
        break;

    case 'g':
        // Radians to Gradians.
        //
        d *= 63.66197723675813;
        break;
    }
    return d;
}

FUNCTION(fun_sin)
{
    double d = Tiny_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }
    fval(buff, bufc, sin(d));
}

FUNCTION(fun_cos)
{
    double d = Tiny_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }
    fval(buff, bufc, cos(d));
}
FUNCTION(fun_tan)
{
    double d = Tiny_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }
    fval(buff, bufc, tan(d));
}

FUNCTION(fun_exp)
{
    fval(buff, bufc, exp(Tiny_atof(fargs[0])));
}

FUNCTION(fun_power)
{
    double val1, val2;

    val1 = Tiny_atof(fargs[0]);
    val2 = Tiny_atof(fargs[1]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val1 < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else
    {
        fval(buff, bufc, pow(val1, val2));
    }
#else
    fval(buff, bufc, pow(val1, val2));
#endif
}

FUNCTION(fun_ln)
{
    double val;

    val = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str("-Inf", buff, bufc);
    }
    else
    {
        fval(buff, bufc, log(val));
    }
#else
    fval(buff, bufc, log(val));
#endif
}

FUNCTION(fun_log)
{
    double val;

    val = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str("-Inf", buff, bufc);
    }
    else
    {
        fval(buff, bufc, log10(val));
    }
#else
    fval(buff, bufc, log10(val));
#endif
}

FUNCTION(fun_asin)
{
    double val = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
        return;
    }
#endif
    val = asin(val);
    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_acos)
{
    double val = Tiny_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
        return;
    }
#endif
    val = acos(val);
    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_atan)
{
    double val = atan(Tiny_atof(fargs[0]));
    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_dist2d)
{
    int d;
    double r;

    d = Tiny_atol(fargs[0]) - Tiny_atol(fargs[2]);
    r = (double)(d * d);
    d = Tiny_atol(fargs[1]) - Tiny_atol(fargs[3]);
    r += (double)(d * d);
    d = (int)(sqrt(r) + 0.5);
    safe_ltoa(d, buff, bufc);
}

FUNCTION(fun_dist3d)
{
    int d;
    double r;

    d = Tiny_atol(fargs[0]) - Tiny_atol(fargs[3]);
    r = (double)(d * d);
    d = Tiny_atol(fargs[1]) - Tiny_atol(fargs[4]);
    r += (double)(d * d);
    d = Tiny_atol(fargs[2]) - Tiny_atol(fargs[5]);
    r += (double)(d * d);
    d = (int)(sqrt(r) + 0.5);
    safe_ltoa(d, buff, bufc);
}



//------------------------------------------------------------------------
// Vector functions: VADD, VSUB, VMUL, VCROSS, VMAG, VUNIT, VDIM
// Vectors are space-separated numbers.
//
#define MAXDIM 20
#define VADD_F 0
#define VSUB_F 1
#define VMUL_F 2
#define VDOT_F 3
#define VCROSS_F 4

static void handle_vectors
(
    char *vecarg1, char *vecarg2, char *buff, char **bufc, char sep,
    char osep, int flag
)
{
    char *v1[(LBUF_SIZE+1)/2], *v2[(LBUF_SIZE+1)/2];
    char vres[MAXDIM][LBUF_SIZE];
    double scalar;
    int n, m, i;

    // Split the list up, or return if the list is empty.
    //
    if (!vecarg1 || !*vecarg1 || !vecarg2 || !*vecarg2)
    {
        return;
    }
    n = list2arr(v1, (LBUF_SIZE+1)/2, vecarg1, sep);
    m = list2arr(v2, (LBUF_SIZE+1)/2, vecarg2, sep);

    // It's okay to have vmul() be passed a scalar first or second arg,
    // but everything else has to be same-dimensional.
    //
    if (  (n != m)
       && (  (flag != VMUL_F)
          || ((n != 1) && (m != 1))
          )
       )
    {
        safe_str("#-1 VECTORS MUST BE SAME DIMENSIONS", buff, bufc);
        return;
    }
    if (n > MAXDIM)
    {
        safe_str("#-1 TOO MANY DIMENSIONS ON VECTORS", buff, bufc);
        return;
    }

    switch (flag)
    {
    case VADD_F:

        for (i = 0; i < n; i++)
        {
            fval_buf(vres[i], Tiny_atof(v1[i]) + Tiny_atof(v2[i]));
            v1[i] = (char *) vres[i];
        }
        arr2list(v1, n, buff, bufc, osep);
        break;

    case VSUB_F:

        for (i = 0; i < n; i++)
        {
            fval_buf(vres[i], Tiny_atof(v1[i]) - Tiny_atof(v2[i]));
            v1[i] = (char *) vres[i];
        }
        arr2list(v1, n, buff, bufc, osep);
        break;

    case VMUL_F:

        // If n or m is 1, this is scalar multiplication.
        // otherwise, multiply elementwise.
        //
        if (n == 1)
        {
            scalar = Tiny_atof(v1[0]);
            for (i = 0; i < m; i++)
            {
                fval_buf(vres[i], Tiny_atof(v2[i]) * scalar);
                v1[i] = (char *) vres[i];
            }
            n = m;
        }
        else if (m == 1)
        {
            scalar = Tiny_atof(v2[0]);
            for (i = 0; i < n; i++)
            {
                fval_buf(vres[i], Tiny_atof(v1[i]) * scalar);
                v1[i] = (char *) vres[i];
            }
        }
        else
        {
            // Vector elementwise product.
            //
            for (i = 0; i < n; i++)
            {
                fval_buf(vres[i], Tiny_atof(v1[i]) * Tiny_atof(v2[i]));
                v1[i] = (char *) vres[i];
            }
        }
        arr2list(v1, n, buff, bufc, osep);
        break;

    case VDOT_F:

        scalar = 0.0;
        for (i = 0; i < n; i++)
        {
            scalar += Tiny_atof(v1[i]) * Tiny_atof(v2[i]);
        }
        fval(buff, bufc, scalar);
        break;

    case VCROSS_F:

        // cross product: (a,b,c) x (d,e,f) = (bf - ce, cd - af, ae - bd)
        //
        // Or in other words:
        //
        //      | a  b  c |
        //  det | d  e  f | = i(bf-ce) + j(cd-af) + k(ae-bd)
        //      | i  j  k |
        //
        // where i, j, and k are unit vectors in the x, y, and z
        // cartisian coordinate space and are understood when expressed
        // in vector form.
        //
        if (n != 3)
        {
            safe_str("#-1 VECTORS MUST BE DIMENSION OF 3", buff, bufc);
        }
        else
        {
            double a[2][3];
            for (i = 0; i < 3; i++)
            {
                a[0][i] = Tiny_atof(v1[i]);
                a[1][i] = Tiny_atof(v2[i]);
                v1[i] = (char *) vres[i];
            }
            fval_buf(vres[0], (a[0][1] * a[1][2]) - (a[0][2] * a[1][1]));
            fval_buf(vres[1], (a[0][2] * a[1][0]) - (a[0][0] * a[1][2]));
            fval_buf(vres[2], (a[0][0] * a[1][1]) - (a[0][1] * a[1][0]));
            arr2list(v1, n, buff, bufc, osep);
        }
        break;

    default:

        // If we reached this, we're in trouble.
        //
        safe_str("#-1 UNIMPLEMENTED", buff, bufc);
    }
}

FUNCTION(fun_vadd)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VADD_F);
}

FUNCTION(fun_vsub)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VSUB_F);
}

FUNCTION(fun_vmul)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VMUL_F);
}

FUNCTION(fun_vdot)
{
    // dot product: (a,b,c) . (d,e,f) = ad + be + cf
    //
    char sep, osep;
    svarargs_preamble(4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VDOT_F);
}

FUNCTION(fun_vcross)
{
    // cross product: (a,b,c) x (d,e,f) = (bf - ce, cd - af, ae - bd)
    //
    char sep, osep;
    svarargs_preamble(4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VCROSS_F);
}

FUNCTION(fun_vmag)
{
    char *v1[LBUF_SIZE];
    int n, i;
    double tmp, res = 0.0;
    char sep;

    varargs_preamble(2);

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }
    n = list2arr(v1, LBUF_SIZE, fargs[0], sep);

    if (n > MAXDIM)
    {
        safe_str("#-1 TOO MANY DIMENSIONS ON VECTORS", buff, bufc);
        return;
    }

    // Calculate the magnitude.
    //
    for (i = 0; i < n; i++)
    {
        tmp = Tiny_atof(v1[i]);
        res += tmp * tmp;
    }

    if (res > 0)
    {
        fval(buff, bufc, sqrt(res));
    }
    else
    {
        safe_chr('0', buff, bufc);
    }
}

FUNCTION(fun_vunit)
{
    char *v1[LBUF_SIZE];
    char vres[MAXDIM][LBUF_SIZE];
    int n, i;
    double tmp, res = 0.0;
    char sep;

    varargs_preamble(2);

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }
    n = list2arr(v1, LBUF_SIZE, fargs[0], sep);

    if (n > MAXDIM)
    {
        safe_str("#-1 TOO MANY DIMENSIONS ON VECTORS", buff, bufc);
        return;
    }

    // Calculate the magnitude.
    //
    for (i = 0; i < n; i++)
    {
        tmp = Tiny_atof(v1[i]);
        res += tmp * tmp;
    }

    if (res <= 0)
    {
        safe_str("#-1 CAN'T MAKE UNIT VECTOR FROM ZERO-LENGTH VECTOR",
            buff, bufc);
        return;
    }
    for (i = 0; i < n; i++)
    {
        fval_buf(vres[i], Tiny_atof(v1[i]) / sqrt(res));
        v1[i] = (char *) vres[i];
    }

    arr2list(v1, n, buff, bufc, sep);
}

FUNCTION(fun_vdim)
{
    char sep;
    if (fargs == 0)
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        varargs_preamble(2);
        safe_ltoa(countwords(fargs[0],sep), buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_comp: string compare.
 */

FUNCTION(fun_comp)
{
    int x;

    x = strcmp(fargs[0], fargs[1]);
    if (x < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_chr((x == 0) ? '0' : '1', buff, bufc);
    }
}

#ifdef WOD_REALMS
FUNCTION(fun_cansee)
{
    dbref looker, lookee;
    int mode;
    looker = match_thing(player, fargs[0]);
    if (looker != NOTHING)
    {
        lookee = match_thing(player, fargs[1]);
        if (lookee != NOTHING)
        {
            if (nfargs == 3)
            {
                mode = Tiny_atol(fargs[2]);
                switch (mode)
                {
                case ACTION_IS_STATIONARY:
                case ACTION_IS_MOVING:
                case ACTION_IS_TALKING:
                    break;

                default:
                    mode = ACTION_IS_STATIONARY;
                    break;
                }
            }
            else
            {
                mode = ACTION_IS_STATIONARY;
            }

            // Do it.
            //
            int Realm_Do = DoThingToThingVisibility(looker, lookee, mode);
            int ch = '0';
            if ((Realm_Do & REALM_DO_MASK) != REALM_DO_HIDDEN_FROM_YOU)
            {
                if (!Dark(lookee))
                {
                    ch = '1';
                }
            }
            safe_chr(ch, buff, bufc);
            return;
        }
    }
    safe_nothing(buff, bufc);
}
#endif

/*
 * ---------------------------------------------------------------------------
 * * fun_lcon: Return a list of contents.
 */

FUNCTION(fun_lcon)
{
    dbref it = match_thing(player, fargs[0]);
    if (  it != NOTHING
       && Has_contents(it)
       && (  Examinable(player, it)
          || Location(player) == it
          || it == cause))
    {
        dbref thing;
        DTB pContext;
        DbrefToBuffer_Init(&pContext, buff, bufc);
        DOLIST(thing, Contents(it))
        {
#ifdef WOD_REALMS
            int iRealmAction = DoThingToThingVisibility(player, thing,
                ACTION_IS_STATIONARY);
            if (iRealmAction != REALM_DO_HIDDEN_FROM_YOU)
            {
#endif
                if (!DbrefToBuffer_Add(&pContext, thing))
                {
                    break;
                }
#ifdef WOD_REALMS
            }
#endif
        }
        DbrefToBuffer_Final(&pContext);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lexits: Return a list of exits.
 */

FUNCTION(fun_lexits)
{
    dbref it = match_thing(player, fargs[0]);

    if (!Good_obj(it) || !Has_exits(it))
    {
        safe_nothing(buff, bufc);
        return;
    }
    BOOL bExam = Examinable(player, it);
    if (  !bExam
       && where_is(player) != it
       && it != cause)
    {
        safe_nothing(buff, bufc);
        return;
    }

    // Return info for all parent levels.
    //
    BOOL bDone = FALSE;
    dbref parent;
    int lev;
    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    ITER_PARENTS(it, parent, lev)
    {
        // Look for exits at each level.
        //
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
        if (Dark(it))
        {
            key |= VE_BASE_DARK;
        }

        dbref thing;
        DOLIST(thing, Exits(parent))
        {
            if (  exit_visible(thing, player, key)
               && !DbrefToBuffer_Add(&pContext, thing))
            {
                bDone = TRUE;
                break;
            }
        }
        if (bDone)
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
}

/*
 * --------------------------------------------------------------------------
 * * fun_home: Return an object's home
 */

FUNCTION(fun_home)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (!Good_obj(it) || !Examinable(player, it))
        safe_nothing(buff, bufc);
    else if (Has_home(it))
        safe_tprintf_str(buff, bufc, "#%d", Home(it));
    else if (Has_dropto(it))
        safe_tprintf_str(buff, bufc, "#%d", Dropto(it));
    else if (isExit(it))
        safe_tprintf_str(buff, bufc, "#%d", where_is(it));
    else
        safe_nothing(buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_money: Return an object's value
 */

FUNCTION(fun_money)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if ((it == NOTHING) || !Examinable(player, it))
        safe_nothing(buff, bufc);
    else
        safe_ltoa(Pennies(it), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_pos: Find a word in a string
 */

FUNCTION(fun_pos)
{
    // Strip ANSI from pattern and save.
    //
    // Note: We need to save it because the next call to strip_ansi()
    // will overwrite the prior result.  Also, we save the pattern
    // instead of the source because the the pattern will tend to be
    // smaller (i.e., on average, fewer bytes to move).
    //
    unsigned int nPat = 0;
    char aPatBuf[LBUF_SIZE];
    memcpy(aPatBuf, strip_ansi(fargs[0], &nPat), nPat);

    // Strip ANSI from source.
    //
    unsigned int nSrc;
    char *pSrc = strip_ansi(fargs[1], &nSrc);

    // Search for pattern string inside source string.
    //
    int i = -1;
    if (nPat == 1)
    {
        // We can optimize the single-character case.
        //
        char *p = strchr(pSrc, aPatBuf[0]);
        if (p)
        {
            i = p - pSrc + 1;
        }
    }
    else if (nPat > 1)
    {
        // We have a multi-byte pattern.
        //
        i = BMH_StringSearch(nPat, aPatBuf, nSrc, pSrc)+1;
    }

    if (i > 0)
    {
        safe_ltoa(i, buff, bufc);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_lpos: Find all occurrences of a character in a string, and return
 * a space-separated list of the positions, starting at 0. i.e.,
 * lpos(a-bc-def-g,-) ==> 1 4 8
 */

FUNCTION(fun_lpos)
{
    if (*fargs[0] == '\0')
    {
        return;
    }

    char c = *fargs[1];
    if (!c)
    {
        c = ' ';
    }

    int i;
    char *bb_p = *bufc;
    char *s = strip_ansi(fargs[0]);
    for (i = 0; *s; i++, s++)
    {
        if (*s == c)
        {
            if (*bufc != bb_p)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_ltoa(i, buff, bufc);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * ldelete: Remove a word from a string by place
 * *  ldelete(<list>,<position>[,<separator>])
 * *
 * * insert: insert a word into a string by place
 * *  insert(<list>,<position>,<new item> [,<separator>])
 * *
 * * replace: replace a word into a string by place
 * *  replace(<list>,<position>,<new item>[,<separator>])
 */

#define IF_DELETE   0
#define IF_REPLACE  1
#define IF_INSERT   2

static void do_itemfuns(char *buff, char **bufc, char *str, int el, char *word, char sep, int flag)
{
    int ct, overrun;
    char *sptr, *iptr, *eptr;
    int slen = 0, ilen = 0, elen = 0;

    char nullb;

    // If passed a null string return an empty string, except that we
    // are allowed to append to a null string.
    //
    if ((!str || !*str) && ((flag != IF_INSERT) || (el != 1)))
    {
        return;
    }
    int nStr = strlen(str);

    // We can't fiddle with anything before the first position.
    //
    if (el < 1)
    {
        safe_copy_buf(str, nStr, buff, bufc);
        return;
    }

    // Split the list up into 'before', 'target', and 'after' chunks
    // pointed to by sptr, iptr, and eptr respectively.
    //
    nullb = '\0';
    if (el == 1)
    {
        // No 'before' portion, just split off element 1
        //
        sptr = NULL;
        slen = 0;
        if (!str || !*str)
        {
            eptr = NULL;
            iptr = NULL;
        }
        else
        {
            eptr = trim_space_sep_LEN(str, nStr, sep, &elen);
            iptr = split_token_LEN(&eptr, &elen, sep, &ilen);
        }
    }
    else
    {
        // Break off 'before' portion.
        //
        sptr = eptr = trim_space_sep_LEN(str, nStr, sep, &elen);
        overrun = 1;
        for (  ct = el;
               ct > 2 && eptr;
               eptr = next_token_LEN(eptr, &elen, sep), ct--)
        {
            // Nothing
        }
        if (eptr)
        {
            // Note: We are using (iptr,ilen) temporarily. It
            // doesn't represent the 'target' word, but the
            // the last token in the 'before' portion.
            //
            overrun = 0;
            iptr = split_token_LEN(&eptr, &elen, sep, &ilen);
            slen = (iptr - sptr) + ilen;
        }

        // If we didn't make it to the target element, just return
        // the string. Insert is allowed to continue if we are exactly
        // at the end of the string, but replace and delete are not.
        //
        if (!(eptr || ((flag == IF_INSERT) && !overrun)))
        {
            safe_copy_buf(str, nStr, buff, bufc);
            return;
        }

        // Split the 'target' word from the 'after' portion.
        //
        if (eptr)
        {
            iptr = split_token_LEN(&eptr, &elen, sep, &ilen);
        }
        else
        {
            iptr = NULL;
            ilen = 0;
        }
    }

    switch (flag)
    {
    case IF_DELETE:

        // deletion
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            if (eptr)
            {
                safe_chr(sep, buff, bufc);
            }
        }
        if (eptr)
        {
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;

    case IF_REPLACE:

        // replacing.
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            safe_chr(sep, buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (eptr)
        {
            safe_chr(sep, buff, bufc);
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;

    case IF_INSERT:

        // Insertion.
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            safe_chr(sep, buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (iptr)
        {
            safe_chr(sep, buff, bufc);
            safe_copy_buf(iptr, ilen, buff, bufc);
        }
        if (eptr)
        {
            safe_chr(sep, buff, bufc);
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;
    }
}


FUNCTION(fun_ldelete)
{
    // Delete a word at position X of a list.
    //
    char sep;
    varargs_preamble(3);
    do_itemfuns(buff, bufc, fargs[0], Tiny_atol(fargs[1]), NULL, sep, IF_DELETE);
}

FUNCTION(fun_replace)
{
    // Replace a word at position X of a list.
    //
    char sep;
    varargs_preamble(4);
    do_itemfuns(buff, bufc, fargs[0], Tiny_atol(fargs[1]), fargs[2], sep, IF_REPLACE);
}

FUNCTION(fun_insert)
{
    // Insert a word at position X of a list.
    //
    char sep;
    varargs_preamble(4);
    do_itemfuns(buff, bufc, fargs[0], Tiny_atol(fargs[1]), fargs[2], sep, IF_INSERT);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_remove: Remove a word from a string
 */

FUNCTION(fun_remove)
{
    char *s, *sp, *word;
    char sep;
    int first, found;

    varargs_preamble(3);
    if (strchr(fargs[1], sep))
    {
        safe_str("#-1 CAN ONLY DELETE ONE ELEMENT", buff, bufc);
        return;
    }
    s = fargs[0];
    word = fargs[1];

    // Walk through the string copying words until (if ever) we get to
    // one that matches the target word.
    //
    sp = s;
    found = 0;
    first = 1;
    while (s) {
        sp = split_token(&s, sep);
        if (found || strcmp(sp, word)) {
            if (!first)
                safe_chr(sep, buff, bufc);
            safe_str(sp, buff, bufc);
            first = 0;
        } else {
            found = 1;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_member: Is a word in a string
 */

FUNCTION(fun_member)
{
    int wcount;
    char *r, *s, sep;

    varargs_preamble(3);
    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do {
        r = split_token(&s, sep);
        if (!strcmp(fargs[1], r))
        {
            safe_ltoa(wcount, buff, bufc);
            return;
        }
        wcount++;
    } while (s);
    safe_chr('0', buff, bufc);
}

// fun_secure: This function replaces any character in the set
// '%$\[](){},;' with a space. It handles ANSI by not replacing
// the '[' character within an ANSI sequence.
//
FUNCTION(fun_secure)
{
    char *pString = fargs[0];
    int nString = strlen(pString);

    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Process TEXT portion (pString, nTokenLength0).
            //
            nString -= nTokenLength0;
            while (nTokenLength0--)
            {
                if (Tiny_IsSecureCharacter[(unsigned char)*pString])
                {
                    safe_chr(' ', buff, bufc);
                }
                else
                {
                    safe_chr(*pString, buff, bufc);
                }
                pString++;
            }
            nTokenLength0 = nTokenLength1;
        }

        if (nTokenLength0)
        {
            // Process ANSI portion (pString, nTokenLength0).
            //
            safe_copy_buf(pString, nTokenLength0, buff, bufc);
            pString += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

// fun_escape: This function prepends a '\' to the beginning of a
// string and before any character which occurs in the set "%\[]{};".
// It handles ANSI by not treating the '[' character within an ANSI
// sequence as a special character.
//
FUNCTION(fun_escape)
{
    char *pString = fargs[0];
    int nString = strlen(pString);

    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Process TEXT portion (pString, nTokenLength0).
            //
            nString -= nTokenLength0;
            while (nTokenLength0--)
            {
                if (  Tiny_IsEscapeCharacter[(unsigned char)*pString]
                   || pString == fargs[0])
                {
                    safe_chr('\\', buff, bufc);
                }
                safe_chr(*pString, buff, bufc);
                pString++;
            }
            nTokenLength0 = nTokenLength1;
        }

        if (nTokenLength0)
        {
            // Process ANSI portion (pString, nTokenLength0).
            //
            safe_copy_buf(pString, nTokenLength0, buff, bufc);
            pString += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

/*
 * Take a character position and return which word that char is in.
 * * wordpos(<string>, <charpos>)
 */
FUNCTION(fun_wordpos)
{
    unsigned charpos;
    int i;
    char *cp, *tp, *xp, sep;

    varargs_preamble(3);

    charpos = Tiny_atol(fargs[1]);
    cp = fargs[0];
    if ((charpos > 0) && (charpos <= strlen(cp)))
    {
        tp = &(cp[charpos - 1]);
        cp = trim_space_sep(cp, sep);
        xp = split_token(&cp, sep);
        for (i = 1; xp; i++)
        {
            if (tp < (xp + strlen(xp)))
                break;
            xp = split_token(&cp, sep);
        }
        safe_ltoa(i, buff, bufc);
        return;
    }
    safe_nothing(buff, bufc);
}

FUNCTION(fun_type)
{
    dbref it = match_thing(player, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    switch (Typeof(it))
    {
    case TYPE_ROOM:
        safe_str("ROOM", buff, bufc);
        break;
    case TYPE_EXIT:
        safe_str("EXIT", buff, bufc);
        break;
    case TYPE_PLAYER:
        safe_str("PLAYER", buff, bufc);
        break;
    case TYPE_THING:
        safe_str("THING", buff, bufc);
        break;
    default:
        safe_str("#-1 ILLEGAL TYPE", buff, bufc);
    }
}

typedef struct
{
    const char *pName;
    int  iMask;
} ATR_HAS_FLAG_ENTRY;

ATR_HAS_FLAG_ENTRY atr_has_flag_table[] =
{
    { "dark",       AF_DARK    },
    { "wizard",     AF_WIZARD  },
    { "hidden",     AF_MDARK   },
    { "html",       AF_HTML    },
    { "locked",     AF_LOCK    },
    { "no_command", AF_NOPROG  },
    { "no_parse",   AF_NOPARSE },
    { "regexp",     AF_REGEXP  },
    { "god",        AF_GOD     },
    { "visual",     AF_VISUAL  },
    { "no_inherit", AF_PRIVATE },
    { "const",      AF_CONST   },
    { NULL,         0          }
};

static int atr_has_flag
(
    dbref player,
    dbref thing,
    ATTR* attr,
    dbref aowner,
    int   aflags,
    const char *flagname
)
{
    if (See_attr(player, thing, attr, aowner, aflags))
    {
        ATR_HAS_FLAG_ENTRY *pEntry = atr_has_flag_table;
        while (pEntry->pName)
        {
            if (string_prefix(pEntry->pName, flagname))
            {
                return aflags & (pEntry->iMask);
            }
            pEntry++;
        }
    }
    return 0;
}

FUNCTION(fun_hasflag)
{
    dbref it;
    int atr;

    if (parse_attrib(player, fargs[0], &it, &atr))
    {
        ATTR *ap;
        if (atr == NOTHING || !(ap = atr_num(atr)))
        {
            safe_str("#-1 NOT FOUND", buff, bufc);
        }
        else
        {
            int aflags;
            dbref aowner;
            atr_pget_info(it, atr, &aowner, &aflags);
            int cc = atr_has_flag(player, it, ap, aowner, aflags, fargs[1]);
            safe_chr(cc ? '1' : '0', buff, bufc);
        }
    }
    else
    {
        it = match_thing(player, fargs[0]);
        if (!Good_obj(it))
        {
            safe_str("#-1 NOT FOUND", buff, bufc);
        }
        else if (  mudconf.pub_flags
                || Examinable(player, it)
                || it == cause)
        {
            int cc = has_flag(player, it, fargs[1]);
            safe_chr(cc ? '1' : '0', buff, bufc);
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
}

FUNCTION(fun_haspower)
{
    dbref it = match_thing(player, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    if (  mudconf.pub_flags
       || Examinable(player, it)
       || it == cause)
    {
        int ch = '0';
        if (has_power(player, it, fargs[1]))
        {
            ch = '1';
        }
        safe_chr(ch, buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_delete)
{
    char *s = fargs[0];
    int iStart = Tiny_atol(fargs[1]);
    int nChars = Tiny_atol(fargs[2]);
    int nLen = strlen(s);

    int iEnd = iStart + nChars;

    // Are we deleting anything at all?
    //
    if (iEnd <= 0 || nLen <= iStart)
    {
        if (nLen)
        {
            safe_copy_buf(s, nLen, buff, bufc);
        }
        return;
    }

    if (iStart < 0) iStart = 0;
    if (nLen < iEnd ) iEnd = nLen;

    // ASSERT: Now [iStart,iEnd) exist somewhere within the the string
    // [s,nLen).
    //
    if (iStart)
    {
        safe_copy_buf(s, iStart, buff, bufc);
    }
    if (iEnd < nLen)
    {
        safe_copy_buf(s + iEnd, nLen - iEnd, buff, bufc);
    }
}

FUNCTION(fun_lock)
{
    dbref it, aowner;
    int aflags;
    char *tbuf;
    ATTR *attr;
    struct boolexp *pBoolExp;

    /*
     * Parse the argument into obj + lock
     */

    if (!get_obj_and_lock(player, fargs[0], &it, &attr, buff, bufc))
        return;

    /*
     * Get the attribute and decode it if we can read it
     */

    tbuf = atr_get(it, attr->number, &aowner, &aflags);
    if (Read_attr(player, it, attr, aowner, aflags)) {
        pBoolExp = parse_boolexp(player, tbuf, 1);
        free_lbuf(tbuf);
        tbuf = (char *)unparse_boolexp_function(player, pBoolExp);
        free_boolexp(pBoolExp);
        safe_str(tbuf, buff, bufc);
    } else
        free_lbuf(tbuf);
}

FUNCTION(fun_elock)
{
    dbref it, victim, aowner;
    int aflags;
    char *tbuf;
    ATTR *attr;
    struct boolexp *pBoolExp;

    // Parse lock supplier into obj + lock.
    //
    if (!get_obj_and_lock(player, fargs[0], &it, &attr, buff, bufc))
    {
        return;
    }

    // Get the victim and ensure we can do it.
    //
    victim = match_thing(player, fargs[1]);
    if (!Good_obj(victim))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
    }
    else if (  !nearby_or_control(player, victim)
            && !nearby_or_control(player, it))
    {
        safe_str("#-1 TOO FAR AWAY", buff, bufc);
    }
    else
    {
        tbuf = atr_get(it, attr->number, &aowner, &aflags);
        if (  attr->number == A_LOCK
           || Read_attr(player, it, attr, aowner, aflags))
        {
            pBoolExp = parse_boolexp(player, tbuf, 1);
            safe_ltoa(eval_boolexp(victim, it, it, pBoolExp), buff, bufc);
            free_boolexp(pBoolExp);
        }
        else
        {
            safe_chr('0', buff, bufc);
        }
        free_lbuf(tbuf);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lwho: Return list of connected users.
 */

FUNCTION(fun_lwho)
{
    make_ulist(player, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_lports: Return list of ports of connected users.
// ---------------------------------------------------------------------------

FUNCTION(fun_lports)
{
    make_port_ulist(player, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_nearby: Return whether or not obj1 is near obj2.
 */

FUNCTION(fun_nearby)
{
    dbref obj1, obj2;
    int ch = '0';

    obj1 = match_thing(player, fargs[0]);
    obj2 = match_thing(player, fargs[1]);
    if (  (  nearby_or_control(player, obj1)
          || nearby_or_control(player, obj2))
       && nearby(obj1, obj2))
    {
        ch = '1';
    }
    safe_chr(ch, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_obj, fun_poss, and fun_subj: perform pronoun sub for object.
 */

static void process_sex(dbref player, char *what, const char *token, char *buff, char **bufc)
{
    dbref it;
    char *str;

    it = match_thing(player, what);
    if (!Good_obj(it) || (!isPlayer(it) && !nearby_or_control(player, it)))
    {
        safe_nomatch(buff, bufc);
    }
    else
    {
        str = (char *)token;
        TinyExec(buff, bufc, it, CALLERQQQ, it, EV_EVAL, &str, (char **)NULL, 0);
    }
}

FUNCTION(fun_obj)
{
    process_sex(player, fargs[0], "%o", buff, bufc);
}

FUNCTION(fun_poss)
{
    process_sex(player, fargs[0], "%p", buff, bufc);
}

FUNCTION(fun_subj)
{
    process_sex(player, fargs[0], "%s", buff, bufc);
}

FUNCTION(fun_aposs)
{
    process_sex(player, fargs[0], "%a", buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mudname: Return the name of the mud.
 */

FUNCTION(fun_mudname)
{
    safe_str(mudconf.mud_name, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_ctime: Return the value of an object's CREATED attribute.
// ---------------------------------------------------------------------------

FUNCTION(fun_ctime)
{
    dbref thing = match_thing(player, fargs[0]);
    if (Examinable(player, thing))
    {
        safe_str(atr_get_raw(thing, A_CREATED), buff, bufc);
    }
    else
    {
        safe_str(NOPERM_MESSAGE, buff, bufc);
    }
}

void ANSI_TransformTextWithTable
(
    char *buff,
    char **bufc,
    char *pString,
    const unsigned char xfrmTable[256])
{
    int   nString = strlen(pString);
    char *pBuffer = *bufc;
    int   nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Determine how much to move.
            //
            int nMove = nTokenLength0;
            if (nMove > nBufferAvailable)
            {
                nMove = nBufferAvailable;
            }
            nBufferAvailable -= nMove;

            // Update pointers and counts.
            //
            char *p = pString;
            nString -= nTokenLength0;
            pString += nTokenLength0;

            // Transform and Move text.
            //
            while (nMove--)
            {
                *pBuffer++ = xfrmTable[(unsigned char)*p++];
            }

            // Determine whether to move the ANSI part.
            //
            if (nTokenLength1)
            {
                if (nTokenLength1 <= nBufferAvailable)
                {
                    memcpy(pBuffer, pString, nTokenLength1);
                    pBuffer += nTokenLength1;
                    nBufferAvailable -= nTokenLength1;
                }
                nString -= nTokenLength1;
                pString += nTokenLength1;
            }
        }
        else
        {
            // TOKEN_ANSI
            //
            // Determine whether to move the ANSI part.
            //
            if (nTokenLength0 <= nBufferAvailable)
            {
                memcpy(pBuffer, pString, nTokenLength0);
                pBuffer += nTokenLength0;
                nBufferAvailable -= nTokenLength0;
            }
            nString -= nTokenLength0;
            pString += nTokenLength0;
        }
    }
    *pBuffer = '\0';
    *bufc = pBuffer;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lcstr, fun_ucstr, fun_capstr: Lowercase, uppercase, or capitalize str.
 */

FUNCTION(fun_lcstr)
{
    ANSI_TransformTextWithTable(buff, bufc, fargs[0], Tiny_ToLower);
}

FUNCTION(fun_ucstr)
{
    ANSI_TransformTextWithTable(buff, bufc, fargs[0], Tiny_ToUpper);
}

FUNCTION(fun_capstr)
{
    char *pString = fargs[0];
    char *pBuffer = *bufc;
    int nString = strlen(pString);
    nString = safe_copy_buf(pString, nString, buff, bufc);

    // Find the first text character in (nString, pBuffer).
    //
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pBuffer, &nTokenLength0, &nTokenLength1);
        if (iType == TOKEN_TEXT_ANSI)
        {
            *pBuffer = Tiny_ToUpper[(unsigned char)*pBuffer];
            return;
        }
        else
        {
            // iType == TOKEN_ANSI
            //
            pBuffer += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lnum: Return a list of numbers.
 */
FUNCTION(fun_lnum)
{
    char sep;
    if (  nfargs == 0
       || !delim_check(fargs, nfargs, 3, &sep, buff, bufc, 0, player, cause, cargs, ncargs, 1))
    {
        return;
    }

    int bot = 0, top;
    if (nfargs == 1)
    {
        top = Tiny_atol(fargs[0]) - 1;
        if (top < 0)
        {
            return;
        }
    }
    else
    {
        bot = Tiny_atol(fargs[0]);
        top = Tiny_atol(fargs[1]);
    }

    int i;
    if (bot == top)
    {
        safe_ltoa(bot, buff, bufc);
    }
    else if (bot < top)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot+1; i <= top; i++)
        {
            print_sep(sep, buff, bufc);
            char *p = *bufc;
            safe_ltoa(i, buff, bufc);
            if (p == *bufc) return;
        }
    }
    else if (top < bot)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot-1; i >= top; i--)
        {
            print_sep(sep, buff, bufc);
            char *p = *bufc;
            safe_ltoa(i, buff, bufc);
            if (p == *bufc) return;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lattr: Return list of attributes I can see on the object.
 */

FUNCTION(fun_lattr)
{
    dbref thing;
    int ca, first;
    ATTR *attr;

    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    first = 1;
    olist_push();
    if (parse_attrib_wild(player, fargs[0], &thing, 0, 0, 1))
    {
        for (ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            attr = atr_num(ca);
            if (attr)
            {
                if (!first)
                {
                    safe_chr(' ', buff, bufc);
                }
                first = 0;
                safe_str((char *)attr->name, buff, bufc);
            }
        }
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

// ---------------------------------------------------------------------------
// fun_attrcnt: Return number of attributes I can see on the object.
// ---------------------------------------------------------------------------

FUNCTION(fun_attrcnt)
{
    dbref thing;
    int ca, count = 0;
    ATTR *attr;

    // Mechanism from lattr.
    //
    olist_push();
    if (parse_attrib_wild(player, fargs[0], &thing, 0, 0, 1))
    {
        for (ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            attr = atr_num(ca);
            if (attr)
            {
                count++;
            }
        }
        safe_ltoa(count, buff, bufc);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

/*
 * ---------------------------------------------------------------------------
 * * fun_reverse, fun_revwords: Reverse things.
 */

static void Tiny_memrevcpy(char *dest, char *src, unsigned int n)
{
    dest += n - 1;
    while (n--)
    {
        *dest-- = *src++;
    }
}

typedef void MEMXFORM(char *dest, char *src, unsigned int n);
void ANSI_TransformTextReverseWithFunction
(
    char *buff,
    char **bufc,
    char *pString,
    MEMXFORM *pfMemXForm
)
{
    // Bounds checking.
    //
    unsigned int nString = strlen(pString);
    char *pBuffer = *bufc;
    unsigned int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    if (nString > nBufferAvailable)
    {
        nString = nBufferAvailable;
        pString[nString] = '\0';
    }

    // How it's done: We have a source string (pString, nString) and a
    // destination buffer (pBuffer, nString) of equal size.
    //
    // We recognize (ANSI,TEXT) phrases and place them at the end of
    // destination buffer working our way to the front. The ANSI part
    // is left inviolate, but the text part is reversed.
    //
    // In this way, (ANSI1,TEXT1)(ANSI2,TEXT2) is traslated into
    // (ANSI2,reverse(TEST2))(ANSI1,reverse(TEXT1)).
    //
    // TODO: Do not reverse the CRLF in the text part either.
    //
    int  nANSI = 0;
    char *pANSI = pString;
    pBuffer += nString;
    *bufc = pBuffer;
    **bufc = '\0';
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // (ANSI,TEXT) is given by (nANSI, nTokenLength0)
            //
            pBuffer -= nANSI + nTokenLength0;
            memcpy(pBuffer, pANSI, nANSI);
            pfMemXForm(pBuffer+nANSI, pString, nTokenLength0);

            // Adjust pointers and counts.
            //
            nString -= nTokenLength0;
            pString += nTokenLength0;
            pANSI = pString;
            nANSI = 0;

            nTokenLength0 = nTokenLength1;
        }
        // TOKEN_ANSI
        //
        nString -= nTokenLength0;
        pString += nTokenLength0;
        nANSI   += nTokenLength0;
    }

    // Copy the last ANSI part (if present). It will be overridden by
    // ANSI further on, but we need to fill up the space. Code
    // elsewhere will compact it before it's sent to the client.
    //
    pBuffer -= nANSI;
    memcpy(pBuffer, pANSI, nANSI);
}

FUNCTION(fun_reverse)
{
    ANSI_TransformTextReverseWithFunction(buff, bufc, fargs[0], Tiny_memrevcpy);
}

char ReverseWordsInText_Seperator;
static void ReverseWordsInText(char *dest, char *src, unsigned int n)
{
    char chSave = src[n];
    src[n] = '\0';
    dest += n;
    while (n)
    {
        char *pWord = strchr(src, ReverseWordsInText_Seperator);
        int nLen;
        if (pWord)
        {
            nLen = (pWord - src);
        }
        else
        {
            nLen = n;
        }
        dest -= nLen;
        memcpy(dest, src, nLen);
        src += nLen;
        n -= nLen;
        if (pWord)
        {
            dest--;
            *dest = ReverseWordsInText_Seperator;
            src++;
            n--;
        }
    }
    src[n] = chSave;
}

FUNCTION(fun_revwords)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }
    char sep;
    varargs_preamble(2);
    ReverseWordsInText_Seperator = sep;
    ANSI_TransformTextReverseWithFunction(buff, bufc, fargs[0], ReverseWordsInText);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_after, fun_before: Return substring after or before a specified string.
 */

FUNCTION(fun_after)
{
    char *mp;
    int mlen;

    // Sanity-check arg1 and arg2.
    //
    char *bp = fargs[0];
    if (nfargs > 1)
    {
        mp = fargs[1];
        mlen = strlen(mp);
    }
    else
    {
        mp = " ";
        mlen = 1;
    }

    if ((mlen == 1) && (*mp == ' '))
    {
        bp = trim_space_sep(bp, ' ');
    }

    // Look for the target string.
    //
    int nText = strlen(bp);
    int i = BMH_StringSearch(mlen, mp, nText, bp);
    if (i >= 0)
    {
        // Yup, return what follows.
        //
        bp += i + mlen;
        safe_copy_buf(bp, nText-i-mlen, buff, bufc);
    }
    //
    // Ran off the end without finding it.
}

FUNCTION(fun_before)
{
    char *mp, *ip;
    int mlen;

    // Sanity-check arg1 and arg2.
    //
    char *bp = fargs[0];
    if (nfargs > 1)
    {
        mp = fargs[1];
        mlen = strlen(mp);
    }
    else
    {
        mp = " ";
        mlen = 1;
    }

    if ((mlen == 1) && (*mp == ' '))
    {
        bp = trim_space_sep(bp, ' ');
    }

    ip = bp;

    // Look for the target string.
    //
    int i = BMH_StringSearch(mlen, mp, strlen(bp), bp);
    if (i >= 0)
    {
        // Yup, return what follows.
        //
        safe_copy_buf(ip, i, buff, bufc);
        return;
    }
    // Ran off the end without finding it.
    //
    safe_str(ip, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_max, fun_min: Return maximum (minimum) value.
 */

FUNCTION(fun_max)
{
    double maximum = -DBL_MAX;
    for (int i = 0; i < nfargs; i++)
    {
        double tval = Tiny_atof(fargs[i]);
        if (tval > maximum)
        {
            maximum = tval;
        }
    }
    fval(buff, bufc, maximum);
}

FUNCTION(fun_min)
{
    double minimum = DBL_MAX;
    for (int i = 0; i < nfargs; i++)
    {
        double tval = Tiny_atof(fargs[i]);
        if (tval < minimum)
        {
            minimum = tval;
        }
    }
    fval(buff, bufc, minimum);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_search: Search the db for things, returning a list of what matches
 */

FUNCTION(fun_search)
{
    char *pArg;
    if (nfargs == 0)
    {
        pArg = "me";
    }
    else
    {
        pArg = fargs[0];
    }

    // Set up for the search.  If any errors, abort.
    //
    SEARCH searchparm;
    if (!search_setup(player, pArg, &searchparm))
    {
        safe_str("#-1 ERROR DURING SEARCH", buff, bufc);
        return;
    }

    // Do the search and report the results.
    //
    olist_push();
    search_perform(player, cause, &searchparm);
    dbref thing;
    DTB pContext;
    DbrefToBuffer_Init(&pContext, buff, bufc);
    for (thing = olist_first(); thing != NOTHING; thing = olist_next())
    {
        if (!DbrefToBuffer_Add(&pContext, thing))
        {
            break;
        }
    }
    DbrefToBuffer_Final(&pContext);
    olist_pop();
}

/*
 * ---------------------------------------------------------------------------
 * * fun_stats: Get database size statistics.
 */

FUNCTION(fun_stats)
{
    dbref who;

    if ((!fargs[0]) || !*fargs[0] || !string_compare(fargs[0], "all"))
    {
        who = NOTHING;
    }
    else
    {
        who = lookup_player(player, fargs[0], 1);
        if (who == NOTHING)
        {
            safe_str("#-1 NOT FOUND", buff, bufc);
            return;
        }
    }
    STATS statinfo;
    if (!get_stats(player, who, &statinfo))
    {
        safe_str("#-1 ERROR GETTING STATS", buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, "%d %d %d %d %d %d", statinfo.s_total, statinfo.s_rooms,
            statinfo.s_exits, statinfo.s_things, statinfo.s_players, statinfo.s_garbage);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_merge:  given two strings and a character, merge the two strings
 * *   by replacing characters in string1 that are the same as the given
 * *   character by the corresponding character in string2 (by position).
 * *   The strings must be of the same length.
 */

FUNCTION(fun_merge)
{
    char *str, *rep;
    char c;

    // Do length checks first.
    //
    if (strlen(fargs[0]) != strlen(fargs[1]))
    {
        safe_str("#-1 STRING LENGTHS MUST BE EQUAL", buff, bufc);
        return;
    }
    if (strlen(fargs[2]) > 1)
    {
        safe_str("#-1 TOO MANY CHARACTERS", buff, bufc);
        return;
    }

    // Find the character to look for. null character is considered a
    // space.
    //
    if (!*fargs[2])
        c = ' ';
    else
        c = *fargs[2];

    // Walk strings, copy from the appropriate string.
    //
    for (str = fargs[0], rep = fargs[1];
         *str && *rep && ((*bufc - buff) < (LBUF_SIZE-1));
         str++, rep++, (*bufc)++)
    {
        if (*str == c)
            **bufc = *rep;
        else
            **bufc = *str;
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_splice: similar to MERGE(), eplaces by word instead of by character.
 */

FUNCTION(fun_splice)
{
    char *p1, *p2, *q1, *q2, sep;
    int words, i, first;

    varargs_preamble(4);

    /*
     * length checks
     */

    if (countwords(fargs[2], sep) > 1) {
        safe_str("#-1 TOO MANY WORDS", buff, bufc);
        return;
    }
    words = countwords(fargs[0], sep);
    if (words != countwords(fargs[1], sep)) {
        safe_str("#-1 NUMBER OF WORDS MUST BE EQUAL", buff, bufc);
        return;
    }
    /*
     * loop through the two lists
     */

    p1 = fargs[0];
    q1 = fargs[1];
    first = 1;
    for (i = 0; i < words; i++) {
        p2 = split_token(&p1, sep);
        q2 = split_token(&q1, sep);
        if (!first)
            safe_chr(sep, buff, bufc);
        if (!strcmp(p2, fargs[2]))
            safe_str(q2, buff, bufc);   /*
                             * replace
                             */
        else
            safe_str(p2, buff, bufc);   /*
                             * copy
                             */
        first = 0;
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_repeat: repeats a string
 */

FUNCTION(fun_repeat)
{
    int times;

    times = Tiny_atol(fargs[1]);
    if (times < 1 || *fargs[0] == '\0')
    {
        // Legal but no work to do.
        //
        return;
    }
    else if (times == 1)
    {
        // It turns into a string copy.
        //
        safe_str(fargs[0], buff, bufc);
    }
    else
    {
        int len = strlen(fargs[0]);
        if (len == 1)
        {
            // It turns into a memset.
            //
            safe_fill(buff, bufc, *fargs[0], times);
        }
        else
        {
            int nSize = len*times;
            if (  times > LBUF_SIZE - 1
               || nSize > LBUF_SIZE - 1)
            {
                safe_str("#-1 STRING TOO LONG", buff, bufc);
            }
            else
            {
                int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                if (nSize > nBufferAvailable)
                {
                    nSize = nBufferAvailable;
                }
                int nFullCopies = nSize / len;
                int nPartial = nSize - nFullCopies * len;
                while (nFullCopies--)
                {
                    memcpy(*bufc, fargs[0], len);
                    *bufc += len;
                }
                if (nPartial)
                {
                    memcpy(*bufc, fargs[0], nPartial);
                    *bufc += nPartial;
                }
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parse: Make list from evaluating arg3 with each member of arg2.
 * * arg1 specifies a delimiter character to use in the parsing of arg2.
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_parse)
{
    char *curr, *objstring, *cp, *dp, sep, osep;
    char *str;
    int number = 0;

    sevarargs_preamble(4);
    cp = curr = dp = alloc_lbuf("fun_parse");
    str = fargs[0];
    TinyExec(curr, &dp, player, CALLERQQQ, cause,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *dp = '\0';
    cp = trim_space_sep(cp, sep);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    BOOL first = TRUE;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = FALSE;
        number++;
        objstring = split_token(&cp, sep);
        char *buff2 = replace_tokens(fargs[1], objstring, Tiny_ltoa_t(number), NULL);
        str = buff2;
        TinyExec(buff, bufc, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buff2);
    }
    free_lbuf(curr);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_iter: Make list from evaluating arg2 with each member of arg1.
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_iter)
{
    char *curr, *objstring, *cp, *dp, sep, osep, *str;
    int number = 0;

    sevarargs_preamble(4);
    dp = cp = curr = alloc_lbuf("fun_iter");
    str = fargs[0];
    TinyExec(curr, &dp, player, CALLERQQQ, cause,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *dp = '\0';
    cp = trim_space_sep(cp, sep);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    mudstate.in_loop++;
    BOOL first = TRUE;
    while (cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = FALSE;
        number++;
        objstring = split_token(&cp, sep);
        char *buff2 = replace_tokens(fargs[1], objstring, Tiny_ltoa_t(number),
            NULL);
        str = buff2;
        TinyExec(buff, bufc, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buff2);
    }
    mudstate.in_loop--;
    free_lbuf(curr);
}

FUNCTION(fun_list)
{
    char *curr, *objstring, *result, *cp, *dp, *str,sep;
    int number = 0;

    evarargs_preamble(3);
    cp = curr = dp = alloc_lbuf("fun_list");
    str = fargs[0];
    TinyExec(curr, &dp, player, CALLERQQQ, cause,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    cp = trim_space_sep(cp, sep);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    mudstate.in_loop++;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        number++;
        objstring = split_token(&cp, sep);
        char *buff2 = replace_tokens(fargs[1], objstring, Tiny_ltoa_t(number),
            NULL);
        dp = result = alloc_lbuf("fun_list.2");
        str = buff2;
        TinyExec(result, &dp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *dp = '\0';
        free_lbuf(buff2);
        notify(cause, result);
        free_lbuf(result);
    }
    mudstate.in_loop--;
    free_lbuf(curr);
}

FUNCTION(fun_ilev)
{
    safe_ltoa(mudstate.in_loop-1, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_fold: iteratively eval an attrib with a list of arguments
 * *        and an optional base case.  With no base case, the first list element
 * *    is passed as %0 and the second is %1.  The attrib is then evaluated
 * *    with these args, the result is then used as %0 and the next arg is
 * *    %1 and so it goes as there are elements left in the list.  The
 * *    optinal base case gives the user a nice starting point.
 * *
 * *    > &REP_NUM object=[%0][repeat(%1,%1)]
 * *    > say fold(OBJECT/REP_NUM,1 2 3 4 5,->)
 * *    You say, "->122333444455555"
 * *
 * *      NOTE: To use added list separator, you must use base case!
 */

FUNCTION(fun_fold)
{
    char *atext, *result, *curr, *bp, *str, *cp, *atextbuf, *clist[2],
    *rstore, sep;

    // We need two to four arguements only.
    //
    varargs_preamble(4);

    // Two possibilities for the first arg: <obj>/<attr> and <attr>.
    //
    dbref thing;
    int   anum;
    ATTR *ap;
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return;
    }

    // Use it if we can access it, otherwise return an error.
    //
    dbref aowner;
    int   aflags;
    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (  !*atext
            || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }

    // Evaluate it using the rest of the passed function args.
    //
    cp = curr = fargs[1];
    atextbuf = alloc_lbuf("fun_fold");
    StringCopy(atextbuf, atext);

    // May as well handle first case now.
    //
    if ( nfargs >= 3
       && fargs[2])
    {
        clist[0] = fargs[2];
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    }
    else
    {
        clist[0] = split_token(&cp, sep);
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    }

    rstore = result;
    result = NULL;

    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        clist[0] = rstore;
        clist[1] = split_token(&cp, sep);
        strcpy(atextbuf, atext);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
        strcpy(rstore, result);
        free_lbuf(result);
    }
    safe_str(rstore, buff, bufc);
    free_lbuf(rstore);
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

// Taken from PennMUSH with permission.
//
// itemize(<list>[,<delim>[,<conjunction>[,<punctuation>]]])
// It takes the elements of list and:
//  If there's just one, returns it.
//  If there's two, returns <e1> <conjunction> <e2>
//  If there's >2, returns <e1><punc> <e2><punc> ... <conjunction> <en>
// Default <conjunction> is "and", default punctuation is ","
//
FUNCTION(fun_itemize)
{ 
    char sep;
    varargs_preamble(2);
    const char *lconj = "and";
    if (nfargs > 2)
    {
        lconj = fargs[2];
    }
    const char *punc = ",";
    if (nfargs > 3)
    {
        punc = fargs[3];
    }

    int pos = 1;
    char *cp = trim_space_sep(fargs[0], sep);
    char *word = split_token(&cp, sep);
    while (cp && *cp)
    {
        pos++;
        safe_str(word, buff, bufc);
        char *nextword = split_token(&cp, sep);

        if (!cp || !*cp)
        {
            // We're at the end.
            //
            if (pos >= 3)
            {
                safe_str(punc, buff, bufc);
            }
            safe_chr(' ', buff, bufc);
            safe_str(lconj, buff, bufc);
        }
        else
        {
            safe_str(punc, buff, bufc);
        }
        safe_chr(' ', buff, bufc);

        word = nextword;
    }
    safe_str(word, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_filter: iteratively perform a function with a list of arguments
 * *              and return the arg, if the function evaluates to TRUE using the
 * *      arg.
 * *
 * *      > &IS_ODD object=mod(%0,2)
 * *      > say filter(object/is_odd,1 2 3 4 5)
 * *      You say, "1 3 5"
 * *      > say filter(object/is_odd,1-2-3-4-5,-)
 * *      You say, "1-3-5"
 * *
 * *  NOTE:  If you specify a separator it is used to delimit returned list
 */

FUNCTION(fun_filter)
{
    char *result, *curr, *objstring, *bp, *str, *cp, *atextbuf, sep;

    varargs_preamble(3);

    // Two possibilities for the first arg: <obj>/<attr> and <attr>.
    //
    dbref thing;
    int   anum;
    ATTR *ap;
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return;
    }

    // Use it if we can access it, otherwise return an error.
    //
    dbref aowner;
    int   aflags;
    char *atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (  !*atext
            || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }

    // Now iteratively eval the attrib with the argument list.
    //
    cp = curr = trim_space_sep(fargs[1], sep);
    atextbuf = alloc_lbuf("fun_filter");
    BOOL first = TRUE;
    while (cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        objstring = split_token(&cp, sep);
        StringCopy(atextbuf, atext);
        result = bp = alloc_lbuf("fun_filter");
        str = atextbuf;
        TinyExec(result, &bp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
        *bp = '\0';
        if (!first && *result == '1')
        {
            safe_chr(sep, buff, bufc);
        }
        if (*result == '1')
        {
            safe_str(objstring, buff, bufc);
            first = FALSE;
        }
        free_lbuf(result);
    }
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_map: iteratively evaluate an attribute with a list of arguments.
 * *
 * *  > &DIV_TWO object=fdiv(%0,2)
 * *  > say map(1 2 3 4 5,object/div_two)
 * *  You say, "0.5 1 1.5 2 2.5"
 * *  > say map(object/div_two,1-2-3-4-5,-)
 * *  You say, "0.5-1-1.5-2-2.5"
 * *
 */

FUNCTION(fun_map)
{
    char *objstring, *str, *cp, *atextbuf, sep, osep;

    svarargs_preamble(4);

    // Two possibilities for the second arg: <obj>/<attr> and <attr>.
    //
    dbref thing;
    int   anum;
    ATTR *ap;
    if (parse_attrib(player, fargs[0], &thing, &anum))
    {
        if (  anum == NOTHING
           || !Good_obj(thing))
        {
            ap = NULL;
        }
        else
        {
            ap = atr_num(anum);
        }
    }
    else
    {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    // Make sure we got a good attribute.
    //
    if (!ap)
    {
        return;
    }

    // Use it if we can access it, otherwise return an error.
    //
    dbref aowner;
    int   aflags;
    char *atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext)
    {
        return;
    }
    else if (  !*atext
            || !See_attr(player, thing, ap, aowner, aflags))
    {
        free_lbuf(atext);
        return;
    }

    // Now process the list one element at a time.
    //
    cp = trim_space_sep(fargs[1], sep);
    atextbuf = alloc_lbuf("fun_map");
    BOOL first = TRUE;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = FALSE;
        objstring = split_token(&cp, sep);
        strcpy(atextbuf, atext);
        str = atextbuf;
        TinyExec(buff, bufc, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
    }
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_edit: Edit text.
 */

FUNCTION(fun_edit)
{
    char *tstr;

    edit_string(strip_ansi(fargs[0]), &tstr, fargs[1], fargs[2]);
    safe_str(tstr, buff, bufc);
    free_lbuf(tstr);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_locate: Search for things with the perspective of another obj.
 */

FUNCTION(fun_locate)
{
    int pref_type, check_locks, verbose, multiple;
    dbref thing, what;
    char *cp;

    pref_type = NOTYPE;
    check_locks = verbose = multiple = 0;

    /*
     * Find the thing to do the looking, make sure we control it.
     */

    if (See_All(player))
        thing = match_thing(player, fargs[0]);
    else
        thing = match_controlled(player, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_noperm(buff, bufc);
        return;
    }
    /*
     * Get pre- and post-conditions and modifiers
     */

    for (cp = fargs[2]; *cp; cp++) {
        switch (*cp) {
        case 'E':
            pref_type = TYPE_EXIT;
            break;
        case 'L':
            check_locks = 1;
            break;
        case 'P':
            pref_type = TYPE_PLAYER;
            break;
        case 'R':
            pref_type = TYPE_ROOM;
            break;
        case 'T':
            pref_type = TYPE_THING;
            break;
        case 'V':
            verbose = 1;
            break;
        case 'X':
            multiple = 1;
            break;
        }
    }

    /*
     * Set up for the search
     */

    if (check_locks)
        init_match_check_keys(thing, fargs[1], pref_type);
    else
        init_match(thing, fargs[1], pref_type);

    /*
     * Search for each requested thing
     */

    for (cp = fargs[2]; *cp; cp++) {
        switch (*cp) {
        case 'a':
            match_absolute();
            break;
        case 'c':
            match_carried_exit_with_parents();
            break;
        case 'e':
            match_exit_with_parents();
            break;
        case 'h':
            match_here();
            break;
        case 'i':
            match_possession();
            break;
        case 'm':
            match_me();
            break;
        case 'n':
            match_neighbor();
            break;
        case 'p':
            match_player();
            break;
        case '*':
            match_everything(MAT_EXIT_PARENTS);
            break;
        }
    }

    /*
     * Get the result and return it to the caller
     */

    if (multiple)
        what = last_match_result();
    else
        what = match_result();

    if (verbose)
        (void)match_status(player, what);

    safe_tprintf_str(buff, bufc, "#%d", what);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_switch: Return value based on pattern matching (ala @switch)
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_switch)
{
    int i;
    char *mbuff, *tbuff, *bp, *str;

    // Evaluate the target in fargs[0].
    //
    mbuff = bp = alloc_lbuf("fun_switch");
    str = fargs[0];
    TinyExec(mbuff, &bp, player, CALLERQQQ, cause,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    // Loop through the patterns looking for a match.
    //
    for (i = 1; (i < nfargs - 1) && fargs[i] && fargs[i + 1]; i += 2)
    {
        tbuff = bp = alloc_lbuf("fun_switch.2");
        str = fargs[i];
        TinyExec(tbuff, &bp, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        if (wild_match(tbuff, mbuff))
        {
            free_lbuf(tbuff);
            tbuff = replace_tokens(fargs[i+1], NULL, NULL, mbuff);
            free_lbuf(mbuff);
            str = tbuff;
            TinyExec(buff, bufc, player, CALLERQQQ, cause,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
            free_lbuf(tbuff);
            return;
        }
        free_lbuf(tbuff);
    }

    // Nope, return the default if there is one.
    //
    if (  i < nfargs
       && fargs[i])
    {
        tbuff = replace_tokens(fargs[i], NULL, NULL, mbuff);
        str = tbuff;
        TinyExec(buff, bufc, player, CALLERQQQ, cause,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(tbuff);
    }
    free_lbuf(mbuff);
}

FUNCTION(fun_case)
{
    int i;
    char *mbuff, *bp, *str;

    // Evaluate the target in fargs[0]
    //
    mbuff = bp = alloc_lbuf("fun_case");
    str = fargs[0];
    TinyExec(mbuff, &bp, player, CALLERQQQ, cause,
             EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    // Loop through the patterns looking for a match.
    //
    for (i = 1; (i < nfargs - 1) && fargs[i] && fargs[i + 1]; i += 2)
    {
        if (!string_compare(fargs[i], mbuff))
        {
            str = fargs[i+1];
            TinyExec(buff, bufc, player, CALLERQQQ, cause,
                     EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
            free_lbuf(mbuff);
            return;
        }
    }
    free_lbuf(mbuff);

    // Nope, return the default if there is one.
    //
    if ((i < nfargs) && fargs[i])
    {
        str = fargs[i];
        TinyExec(buff, bufc, player, CALLERQQQ, cause,
                 EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_space: Make spaces.
 */

FUNCTION(fun_space)
{
    // Validate request.
    //
    int num;
    if (nfargs == 0 || *fargs[0] == '\0')
    {
        num = 1;
    }
    else
    {
        num = Tiny_atol(fargs[0]);
        if (num == 0)
        {
            // If 'space(0)', 'space(00)', ..., then allow num == 0,
            // otherwise, we force to num to be 1.
            //
            if (!is_integer(fargs[0], NULL))
            {
                num = 1;
            }
        }
        else if (num < 0)
        {
            num = 0;
        }

    }
    safe_fill(buff, bufc, ' ', num);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_idle, fun_conn: return seconds idle or connected.
 */

FUNCTION(fun_idle)
{
    char *pTargetName = fargs[0];
    if (*pTargetName == '*')
    {
        pTargetName++;
    }
    dbref target = lookup_player(player, pTargetName, 1);
    if (  Good_obj(target)
       && Hidden(target)
       && !See_Hidden(player))
    {
        target = NOTHING;
    }
    safe_ltoa(fetch_idle(target), buff, bufc);
}

FUNCTION(fun_conn)
{
    char *pTargetName = fargs[0];
    if (*pTargetName == '*')
    {
        pTargetName++;
    }
    dbref target = lookup_player(player, pTargetName, 1);
    if (  Good_obj(target)
       && Hidden(target)
       && !See_Hidden(player))
    {
        target = NOTHING;
    }
    safe_ltoa(fetch_connect(target), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_sort: Sort lists.
 */

typedef struct f_record f_rec;
typedef struct i_record i_rec;

struct f_record {
    double data;
    char *str;
};

struct i_record {
    long data;
    char *str;
};

static int DCL_CDECL a_comp(const void *s1, const void *s2)
{
    return strcmp(*(char **)s1, *(char **)s2);
}

static int DCL_CDECL f_comp(const void *s1, const void *s2)
{
    if (((f_rec *) s1)->data > ((f_rec *) s2)->data)
    {
        return 1;
    }
    else if (((f_rec *) s1)->data < ((f_rec *) s2)->data)
    {
        return -1;
    }
    return 0;
}

static int DCL_CDECL i_comp(const void *s1, const void *s2)
{
    if (((i_rec *) s1)->data > ((i_rec *) s2)->data)
    {
        return 1;
    }
    else if (((i_rec *) s1)->data < ((i_rec *) s2)->data)
    {
        return -1;
    }
    return 0;
}

static void do_asort(char *s[], int n, int sort_type)
{
    int i;
    f_rec *fp;
    i_rec *ip;

    switch (sort_type)
    {
    case ASCII_LIST:

        qsort((void *)s, n, sizeof(char *), a_comp);
        break;

    case NUMERIC_LIST:

        ip = (i_rec *) MEMALLOC(n * sizeof(i_rec));
        (void)ISOUTOFMEMORY(ip);
        for (i = 0; i < n; i++)
        {
            ip[i].str = s[i];
            ip[i].data = Tiny_atol(s[i]);
        }
        qsort((void *)ip, n, sizeof(i_rec), i_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = ip[i].str;
        }
        MEMFREE(ip);
        ip = NULL;
        break;

    case DBREF_LIST:
        ip = (i_rec *) MEMALLOC(n * sizeof(i_rec));
        (void)ISOUTOFMEMORY(ip);
        for (i = 0; i < n; i++)
        {
            ip[i].str = s[i];
            ip[i].data = dbnum(s[i]);
        }
        qsort((void *)ip, n, sizeof(i_rec), i_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = ip[i].str;
        }
        MEMFREE(ip);
        ip = NULL;
        break;

    case FLOAT_LIST:

        fp = (f_rec *) MEMALLOC(n * sizeof(f_rec));
        (void)ISOUTOFMEMORY(fp);
        for (i = 0; i < n; i++)
        {
            fp[i].str = s[i];
            fp[i].data = Tiny_atof(s[i]);
        }
        qsort((void *)fp, n, sizeof(f_rec), f_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = fp[i].str;
        }
        MEMFREE(fp);
        fp = NULL;
        break;
    }
}

FUNCTION(fun_sort)
{
    int nitems, sort_type;
    char *list, sep, osep;
    char *ptrs[LBUF_SIZE / 2];

    // If we are passed an empty arglist return a null string.
    //
    svarargs_preamble(4);

    // Convert the list to an array.
    //
    list = alloc_lbuf("fun_sort");
    StringCopy(list, fargs[0]);
    nitems = list2arr(ptrs, LBUF_SIZE / 2, list, sep);
    sort_type = get_list_type(fargs, nfargs, 2, ptrs, nitems);
    do_asort(ptrs, nitems, sort_type);
    arr2list(ptrs, nitems, buff, bufc, osep);
    free_lbuf(list);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_setunion, fun_setdiff, fun_setinter: Set management.
 */

#define SET_UNION     1
#define SET_INTERSECT 2
#define SET_DIFF      3

static void handle_sets
(
    char *fargs[],
    char *buff,
    char **bufc,
    int  oper,
    char sep,
    char osep
)
{
    char *ptrs1[LBUF_SIZE], *ptrs2[LBUF_SIZE];
    int val;

    char *list1 = alloc_lbuf("fun_setunion.1");
    strcpy(list1, fargs[0]);
    int n1 = list2arr(ptrs1, LBUF_SIZE, list1, sep);
    do_asort(ptrs1, n1, ASCII_LIST);

    char *list2 = alloc_lbuf("fun_setunion.2");
    strcpy(list2, fargs[1]);
    int n2 = list2arr(ptrs2, LBUF_SIZE, list2, sep);
    do_asort(ptrs2, n2, ASCII_LIST);

    int i1 = 0;
    int i2 = 0;
    char *oldp = NULL;
    BOOL bFirst = TRUE;

    switch (oper)
    {
    case SET_UNION:
    
        // Copy elements common to both lists.
        //
        // Handle case of two identical single-element lists.
        //
        if (  n1 == 1
           && n2 == 1
           && strcmp(ptrs1[0], ptrs2[0]) == 0)
        {
            safe_str(ptrs1[0], buff, bufc);
            break;
        }

        // Process until one list is empty.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            // Skip over duplicates.
            //
            if (  i1 > 0
               || i2 > 0)
            {
                while (  i1 < n1
                      && oldp
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && oldp
                      && strcmp(ptrs2[i2], oldp) == 0)
                {
                    i2++;
                }
            }

            // Compare and copy.
            //
            if (  i1 < n1
               && i2 < n2)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = FALSE;
                if (strcmp(ptrs1[i1], ptrs2[i2]) < 0)
                {
                    oldp = ptrs1[i1];
                    safe_str(ptrs1[i1], buff, bufc);
                    i1++;
                }
                else
                {
                    oldp = ptrs2[i2];
                    safe_str(ptrs2[i2], buff, bufc);
                    i2++;
                }
            }
        }

        // Copy rest of remaining list, stripping duplicates.
        //
        for (; i1 < n1; i1++)
        {
            if (  !oldp
               || strcmp(oldp, ptrs1[i1]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = FALSE;
                oldp = ptrs1[i1];
                safe_str(ptrs1[i1], buff, bufc);
            }
        }
        for (; i2 < n2; i2++)
        {
            if (  !oldp
               || strcmp(oldp, ptrs2[i2]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = FALSE;
                oldp = ptrs2[i2];
                safe_str(ptrs2[i2], buff, bufc);
            }
        }
        break;

    case SET_INTERSECT:
    
        // Copy elements not in both lists.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val)
            {
                // Got a match, copy it.
                //
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = FALSE;
                oldp = ptrs1[i1];
                safe_str(ptrs1[i1], buff, bufc);
                i1++;
                i2++;
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
                {
                    i2++;
                }
            }
            else if (val < 0)
            {
                i1++;
            }
            else
            {
                i2++;
            }
        }
        break;

    case SET_DIFF:
    
        // Copy elements unique to list1.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val)
            {
                // Got a match, increment pointers.
                //
                oldp = ptrs1[i1];
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
                {
                    i2++;
                }
            }
            else if (val < 0)
            {
                // Item in list1 not in list2, copy.
                //
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = FALSE;
                safe_str(ptrs1[i1], buff, bufc);
                oldp = ptrs1[i1];
                i1++;
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
            }
            else
            {
                // Item in list2 but not in list1, discard.
                //
                oldp = ptrs2[i2];
                i2++;
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
                {
                    i2++;
                }
            }
        }

        // Copy remainder of list1.
        //
        while (i1 < n1)
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            bFirst = FALSE;
            safe_str(ptrs1[i1], buff, bufc);
            oldp = ptrs1[i1];
            i1++;
            while (  i1 < n1
                  && strcmp(ptrs1[i1], oldp) == 0)
            {
                i1++;
            }
        }
    }
    free_lbuf(list1);
    free_lbuf(list2);
}

FUNCTION(fun_setunion)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_sets(fargs, buff, bufc, SET_UNION, sep, osep);
}

FUNCTION(fun_setdiff)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_sets(fargs, buff, bufc, SET_DIFF, sep, osep);
}

FUNCTION(fun_setinter)
{
    char sep, osep;
    svarargs_preamble(4);
    handle_sets(fargs, buff, bufc, SET_INTERSECT, sep, osep);
}

/*
 * ---------------------------------------------------------------------------
 * * rjust, ljust, center: Justify or center text, specifying fill character
 */
#define CJC_CENTER 0
#define CJC_LJUST  1
#define CJC_RJUST  2

void centerjustcombo
(
    int iType,
    char *buff,
    char **bufc,
    char *fargs[],
    int nfargs
)
{
    // Width must be a number.
    //
    if (!is_integer(fargs[1], NULL))
    {
        return;
    }
    int width = Tiny_atol(fargs[1]);
    if (width <= 0 || LBUF_SIZE <= width)
    {
        safe_str("#-1 OUT OF RANGE", buff, bufc);
        return;
    }

    // Determine string to pad with.
    //
    int  vwPad;
    int  nPad = -1;
    char aPad[SBUF_SIZE];
    struct ANSI_In_Context  aic;
    struct ANSI_Out_Context aoc;
    if (nfargs == 3 && *fargs[2])
    {
        char *p = RemoveSetOfCharacters(fargs[2], "\r\n\t");
        ANSI_String_In_Init(&aic, p, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Out_Init(&aoc, aPad, sizeof(aPad), sizeof(aPad), ANSI_ENDGOAL_LEAK);
        ANSI_String_Copy(&aoc, &aic, sizeof(aPad), sizeof(aPad));
        nPad = ANSI_String_Finalize(&aoc, &vwPad);
    }
    if (nPad <= 0)
    {
        aPad[0] = ' ';
        aPad[1] = '\0';
        nPad    = 1;
        vwPad   = 1;
    }

    int  vwStr;
    char aStr[LBUF_SIZE];
    int nStr = ANSI_TruncateToField(fargs[0], sizeof(aStr), aStr,
        width, &vwStr, ANSI_ENDGOAL_NORMAL);

    // If the visual width of the text fits exactly into the field,
    // then we are done. ANSI_TruncateToField insures that it's
    // never larger.
    //
    if (vwStr == width)
    {
        safe_copy_buf(aStr, nStr, buff, bufc);
        return;
    }

    int vwLeading = 0;
    if (iType == CJC_CENTER)
    {
        vwLeading = (width - vwStr)/2;
    }
    else if (iType == CJC_RJUST)
    {
        vwLeading = width - vwStr;
    }
    int vwTrailing      = width - vwLeading - vwStr;

    // Shortcut this function if nPad == 1 (i.e., the padding is a single
    // character).
    //
    if (nPad == 1 && vwPad == 1)
    {
        safe_fill(buff, bufc, aPad[0], vwLeading);
        safe_copy_buf(aStr, nStr, buff, bufc);
        safe_fill(buff, bufc, aPad[0], vwTrailing);
        return;
    }


    // Calculate the necessary info about the leading padding.
    // The origin on the padding is at byte 0 at beginning of the
    // field (this may cause mis-syncronization on the screen if
    // the same background padding string is used on several lines
    // with each idented from column 0 by a different amount.
    // There is nothing center() can do about this issue. You are
    // on your own.
    //
    // Padding is repeated nLeadFull times and then a partial string
    // of vwLeadPartial visual width is tacked onto the end.
    //
    // vwLeading == nLeadFull * vwPad + vwLeadPartial
    //
    int nLeadFull     = 0;
    int vwLeadPartial = 0;
    if (vwLeading)
    {
        nLeadFull     = vwLeading / vwPad;
        vwLeadPartial = vwLeading - nLeadFull * vwPad;
    }

    // Calculate the necessary info about the trailing padding.
    //
    // vwTrailing == vwTrailPartial0 + nTrailFull * vwPad
    //             + vwTrailPartial1
    //
    int vwTrailSkip0    = 0;
    int vwTrailPartial0 = 0;
    int nTrailFull      = 0;
    int vwTrailPartial1 = 0;
    if (vwTrailing)
    {
        vwTrailSkip0    = (vwLeading + vwStr) % vwPad;
        vwTrailPartial0 = 0;
        if (vwTrailSkip0)
        {
            int n = vwPad - vwTrailSkip0;
            if (vwTrailing >= vwTrailPartial0)
            {
                vwTrailPartial0 = n;
                vwTrailing -= vwTrailPartial0;
            }
        }
        nTrailFull      = vwTrailing / vwPad;
        vwTrailPartial1 = vwTrailing - nTrailFull * vwPad;
    }

    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    ANSI_String_Out_Init(&aoc, *bufc, nBufferAvailable,
        LBUF_SIZE-1, ANSI_ENDGOAL_NORMAL);
    int    vwDone;

    // Output the runs of full leading padding.
    //
    int i, n;
    for (i = 0; i < nLeadFull; i++)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, vwPad);
    }

    // Output the partial leading padding segment.
    //
    if (vwLeadPartial > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, vwLeadPartial);
    }

    // Output the main string to be centered.
    //
    if (nStr > 0)
    {
        ANSI_String_In_Init(&aic, aStr, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, LBUF_SIZE-1);
    }

    // Output the first partial trailing padding segment.
    //
    if (vwTrailPartial0 > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Skip(&aic, vwTrailSkip0, &vwDone);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, LBUF_SIZE-1);
    }

    // Output the runs of full trailing padding.
    //
    for (i = 0; i < nTrailFull; i++)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, vwPad);
    }

    // Output the second partial trailing padding segment.
    //
    if (vwTrailPartial1 > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nBufferAvailable, vwTrailPartial1);
    }

    n = ANSI_String_Finalize(&aoc, &vwDone);
    *bufc += n;
}

FUNCTION(fun_ljust)
{
    centerjustcombo(CJC_LJUST, buff, bufc, fargs, nfargs);
}

FUNCTION(fun_rjust)
{
    centerjustcombo(CJC_RJUST, buff, bufc, fargs, nfargs);
}

FUNCTION(fun_center)
{
    centerjustcombo(CJC_CENTER, buff, bufc, fargs, nfargs);
}

/*
 * ---------------------------------------------------------------------------
 * * setq, setr, r: set and read global registers.
 */

FUNCTION(fun_setq)
{
    int regnum = Tiny_IsRegister[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else
    {
        if (!mudstate.global_regs[regnum])
        {
            mudstate.global_regs[regnum] = alloc_lbuf("fun_setq");
        }
        int n = strlen(fargs[1]);
        memcpy(mudstate.global_regs[regnum], fargs[1], n+1);
        mudstate.glob_reg_len[regnum] = n;
    }
}

FUNCTION(fun_setr)
{
    int regnum = Tiny_IsRegister[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else
    {
        if (!mudstate.global_regs[regnum])
        {
            mudstate.global_regs[regnum] = alloc_lbuf("fun_setq");
        }
        int n = strlen(fargs[1]);
        memcpy(mudstate.global_regs[regnum], fargs[1], n+1);
        mudstate.glob_reg_len[regnum] = n;
        safe_copy_buf(fargs[1], n, buff, bufc);
    }
}

FUNCTION(fun_r)
{
    int regnum = Tiny_IsRegister[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else if (mudstate.global_regs[regnum])
    {
        safe_copy_buf(mudstate.global_regs[regnum],
            mudstate.glob_reg_len[regnum], buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * isnum: is the argument a number?
 */

FUNCTION(fun_isnum)
{
    safe_str((is_real(fargs[0]) ? "1" : "0"), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * israt: is the argument an rational?
 */

FUNCTION(fun_israt)
{
    safe_str((is_rational(fargs[0]) ? "1" : "0"), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * isint: is the argument an integer?
 */

FUNCTION(fun_isint)
{
    safe_str((is_integer(fargs[0], NULL) ? "1" : "0"), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * isdbref: is the argument a valid dbref?
 */

FUNCTION(fun_isdbref)
{
    char *p;
    dbref dbitem;
    int ch = '0';

    p = fargs[0];
    if (*p++ == NUMBER_TOKEN)
    {
        dbitem = parse_dbref(p);
        if (Good_obj(dbitem))
        {
            ch = '1';
        }
    }
    safe_chr(ch, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * trim: trim off unwanted white space.
 */

FUNCTION(fun_trim)
{
    char *p, *lastchar, *q, sep;
    int trim;

    varargs_preamble(3);
    if (nfargs >= 2)
    {
        switch (Tiny_ToLower[(unsigned char)*fargs[1]])
        {
        case 'l':
            trim = 1;
            break;
        case 'r':
            trim = 2;
            break;
        default:
            trim = 3;
            break;
        }
    } else {
        trim = 3;
    }

    if (trim == 2 || trim == 3)
    {
        p = lastchar = fargs[0];
        while (*p != '\0')
        {
            if (*p != sep)
                lastchar = p;
            p++;
        }
        *(lastchar + 1) = '\0';
    }
    q = fargs[0];
    if (trim == 1 || trim == 3)
    {
        while (*q != '\0')
        {
            if (*q == sep)
                q++;
            else
                break;
        }
    }
    safe_str(q, buff, bufc);
}

FUNCTION(fun_config)
{
    cf_display(player, fargs[0], buff, bufc);
}

/* ---------------------------------------------------------------------------
 * flist: List of existing functions in alphabetical order.
 */

FUN flist[] =
{
    {"@@",       fun_null,           1, 1,  1, FN_NO_EVAL, CA_PUBLIC},
    {"ABS",      fun_abs,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ACOS",     fun_acos,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"ADD",      fun_add,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"AFTER",    fun_after,    MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"ALPHAMAX", fun_alphamax, MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"ALPHAMIN", fun_alphamin, MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"AND",      fun_and,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"ANDFLAGS", fun_andflags, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"ANSI",     fun_ansi,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"APOSS",    fun_aposs,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ART",      fun_art,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ASIN",     fun_asin,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"ATAN",     fun_atan,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"ATTRCNT",  fun_attrcnt,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"BAND",     fun_band,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"BEEP",     fun_beep,     MAX_ARG, 0,  0,       0, CA_WIZARD},
    {"BEFORE",   fun_before,   MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"BNAND",    fun_bnand,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"BOR",      fun_bor,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
#ifdef WOD_REALMS
    {"CANSEE",   fun_cansee,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
#endif
    {"CAPSTR",   fun_capstr,   1,       1,  1,       0, CA_PUBLIC},
    {"CASE",     fun_case,     MAX_ARG, 2,  MAX_ARG, FN_NO_EVAL, CA_PUBLIC},
    {"CAT",      fun_cat,      MAX_ARG, 0,  MAX_ARG, 0, CA_PUBLIC},
    {"CEIL",     fun_ceil,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CENTER",   fun_center,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"CHANNELS", fun_channels, MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"CHILDREN", fun_children, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CMDS",     fun_cmds,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"COLUMNS",  fun_columns,  MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"COMALIAS", fun_comalias, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"COMP",     fun_comp,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"COMTITLE", fun_comtitle, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"CON",      fun_con,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONFIG",   fun_config,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONN",     fun_conn,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONNLAST", fun_connlast, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONNLEFT", fun_connleft, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONNMAX",  fun_connmax,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONNNUM",  fun_connnum,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONNTOTAL",fun_conntotal,MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CONTROLS", fun_controls, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"CONVSECS", fun_convsecs, MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"CONVTIME", fun_convtime, MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"COS",      fun_cos,      MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"CRC32",    fun_crc32,    MAX_ARG, 0,  MAX_ARG, 0, CA_PUBLIC},
    {"CREATE",   fun_create,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"CTIME",    fun_ctime,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"CWHO",     fun_cwho,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"DEC",      fun_dec,      MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"DECRYPT",  fun_decrypt,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"DEFAULT",  fun_default,  MAX_ARG, 2,  2, FN_NO_EVAL, CA_PUBLIC},
    {"DELETE",   fun_delete,   MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"DIE",      fun_die,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"DIGITTIME",fun_digittime,MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"DIST2D",   fun_dist2d,   MAX_ARG, 4,  4,       0, CA_PUBLIC},
    {"DIST3D",   fun_dist3d,   MAX_ARG, 6,  6,       0, CA_PUBLIC},
    {"DOING",    fun_doing,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"DUMPING",  fun_dumping,  MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"E",        fun_e,        MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"EDEFAULT", fun_edefault, MAX_ARG, 2,  2, FN_NO_EVAL, CA_PUBLIC},
    {"EDIT",     fun_edit,     MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"ELEMENTS", fun_elements, MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"ELOCK",    fun_elock,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"EMIT",     fun_emit,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"EMPTY",    fun_empty,    MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"ENCRYPT",  fun_encrypt,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"EQ",       fun_eq,       MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"ESCAPE",   fun_escape,   1,       1,  1,       0, CA_PUBLIC},
    {"EVAL",     fun_eval,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"EXIT",     fun_exit,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"EXP",      fun_exp,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"EXPTIME",  fun_exptime,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"EXTRACT",  fun_extract,  MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"FDIV",     fun_fdiv,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"FILTER",   fun_filter,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"FINDABLE", fun_findable, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"FIRST",    fun_first,    MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"FLAGS",    fun_flags,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"FLOOR",    fun_floor,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"FLOORDIV", fun_floordiv, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"FOLD",     fun_fold,     MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"FOREACH",  fun_foreach,  MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"FULLNAME", fun_fullname, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"GET",      fun_get,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"GET_EVAL", fun_get_eval, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"GRAB",     fun_grab,     MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"GREP",     fun_grep,     MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"GREPI",    fun_grepi,    MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"GT",       fun_gt,       MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"GTE",      fun_gte,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HASATTR",  fun_hasattr,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HASATTRP", fun_hasattrp, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HASFLAG",  fun_hasflag,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HASPOWER", fun_haspower, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HASQUOTA", fun_hasquota, MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"HASTYPE",  fun_hastype,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"HOME",     fun_home,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"IABS",     fun_iabs,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"IADD",     fun_iadd,     MAX_ARG, 0,  MAX_ARG, 0, CA_PUBLIC},
    {"IDIV",     fun_idiv,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"IDLE",     fun_idle,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"IF",       fun_ifelse,   MAX_ARG, 2,  3, FN_NO_EVAL, CA_PUBLIC},
    {"IFELSE",   fun_ifelse,   MAX_ARG, 3,  3, FN_NO_EVAL, CA_PUBLIC},
    {"ILEV",     fun_ilev,     MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"IMUL",     fun_imul,     MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"INC",      fun_inc,      MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"INDEX",    fun_index,    MAX_ARG, 4,  4,       0, CA_PUBLIC},
    {"INSERT",   fun_insert,   MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"INZONE",   fun_inzone,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISDBREF",  fun_isdbref,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISIGN",    fun_isign,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISINT",    fun_isint,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISNUM",    fun_isnum,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISRAT",    fun_israt,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ISUB",     fun_isub,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"ISWORD",   fun_isword,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ITEMIZE",  fun_itemize,  MAX_ARG, 1,  4,       0, CA_PUBLIC},
    {"ITEMS",    fun_items,    MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"ITER",     fun_iter,     MAX_ARG, 2,  4, FN_NO_EVAL, CA_PUBLIC},
    {"LADD",     fun_ladd,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"LAST",     fun_last,     MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"LATTR",    fun_lattr,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LATTRCMDS",fun_lattrcmds,MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LCMDS",    fun_lcmds,    MAX_ARG, 1,  3,       0, CA_PUBLIC},
    {"LCON",     fun_lcon,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LCSTR",    fun_lcstr,    1,       1,  1,       0, CA_PUBLIC},
    {"LDELETE",  fun_ldelete,  MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"LEXITS",   fun_lexits,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LFLAGS",   fun_lflags,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LINK",     fun_link,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"LIST",     fun_list,     MAX_ARG, 2,  3, FN_NO_EVAL, CA_PUBLIC},
    {"LIT",      fun_lit,      MAX_ARG, 1,  1, FN_NO_EVAL, CA_PUBLIC},
    {"LJUST",    fun_ljust,    MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"LN",       fun_ln,       MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LNUM",     fun_lnum,     MAX_ARG, 0,  3,       0, CA_PUBLIC},
    {"LOC",      fun_loc,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LOCALIZE", fun_localize, MAX_ARG, 1,  1, FN_NO_EVAL, CA_PUBLIC},
    {"LOCATE",   fun_locate,   MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"LOCK",     fun_lock,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LOG",      fun_log,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LPARENT",  fun_lparent,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"LPORTS",   fun_lports,   MAX_ARG, 0,  0,       0, CA_WIZARD},
    {"LPOS",     fun_lpos,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"LRAND",    fun_lrand,    MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"LROOMS",   fun_lrooms,   MAX_ARG, 1,  3,       0, CA_PUBLIC},
    {"LSTACK",   fun_lstack,   MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"LT",       fun_lt,       MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"LTE",      fun_lte,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"LWHO",     fun_lwho,     MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"MAIL",     fun_mail,     MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"MAILFROM", fun_mailfrom, MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"MAP",      fun_map,      MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"MATCH",    fun_match,    MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"MATCHALL", fun_matchall, MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"MAX",      fun_max,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"MEMBER",   fun_member,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"MERGE",    fun_merge,    MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"MID",      fun_mid,      MAX_ARG, 3,  3,       0, CA_PUBLIC},
    {"MIN",      fun_min,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"MIX",      fun_mix,      MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"MOD",      fun_mod,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"MONEY",    fun_money,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"MOTD",     fun_motd,     MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"MUDNAME",  fun_mudname,  MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"MUL",      fun_mul,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"MUNGE",    fun_munge,    MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"NAME",     fun_name,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"NEARBY",   fun_nearby,   MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"NEQ",      fun_neq,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"NEXT",     fun_next,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"NOT",      fun_not,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"NULL",     fun_null,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"NUM",      fun_num,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"OBJ",      fun_obj,      MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"OBJEVAL",  fun_objeval,  MAX_ARG, 2,  2, FN_NO_EVAL, CA_PUBLIC},
    {"OBJMEM",   fun_objmem,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"OEMIT",    fun_oemit,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"OR",       fun_or,       MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"ORFLAGS",  fun_orflags,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"OWNER",    fun_owner,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"PACK",     fun_pack,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"PARENT",   fun_parent,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"PARSE",    fun_parse,    MAX_ARG, 2,  4, FN_NO_EVAL, CA_PUBLIC},
    {"PEEK",     fun_peek,     MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"PEMIT",    fun_pemit,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"PFIND",    fun_pfind,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"PI",       fun_pi,       MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"PICKRAND", fun_pickrand, MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"PLAYMEM",  fun_playmem,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"PMATCH",   fun_pmatch,   MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"POLL",     fun_poll,     MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"POP",      fun_pop,      MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"PORTS",    fun_ports,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"POS",      fun_pos,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"POSS",     fun_poss,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"POWER",    fun_power,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"PUSH",     fun_push,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"R",        fun_r,        MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"RAND",     fun_rand,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"REGMATCH", fun_regmatch, MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"REMAINDER",fun_remainder,MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"REMIT",    fun_remit,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"REMOVE",   fun_remove,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"REPEAT",   fun_repeat,   MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"REPLACE",  fun_replace,  MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"REST",     fun_rest,     MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"REVERSE",  fun_reverse,  1,       1,  1,       0, CA_PUBLIC},
    {"REVWORDS", fun_revwords, MAX_ARG, 0,  MAX_ARG, 0, CA_PUBLIC},
    {"RJUST",    fun_rjust,    MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"RLOC",     fun_rloc,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"ROOM",     fun_room,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ROUND",    fun_round,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"S",        fun_s,        1,       1,  1,       0, CA_PUBLIC},
    {"SCRAMBLE", fun_scramble, MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"SEARCH",   fun_search,   1,       0,  1,       0, CA_PUBLIC},
    {"SECS",     fun_secs,     MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"SECURE",   fun_secure,   1,       1,  1,       0, CA_PUBLIC},
    {"SET",      fun_set,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SETDIFF",  fun_setdiff,  MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"SETINTER", fun_setinter, MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"SETQ",     fun_setq,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SETR",     fun_setr,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SETUNION", fun_setunion, MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"SHL",      fun_shl,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SHR",      fun_shr,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SHUFFLE",  fun_shuffle,  MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"SIGN",     fun_sign,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"SIN",      fun_sin,      MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"SINGLETIME", fun_singletime, MAX_ARG, 1, 1,    0, CA_PUBLIC},
    {"SORT",     fun_sort,     MAX_ARG, 1,  4,       0, CA_PUBLIC},
    {"SORTBY",   fun_sortby,   MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"SPACE",    fun_space,    MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"SPLICE",   fun_splice,   MAX_ARG, 3,  4,       0, CA_PUBLIC},
    {"SQRT",     fun_sqrt,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"SQUISH",   fun_squish,   MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"STARTSECS",fun_startsecs,MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"STARTTIME",fun_starttime,MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"STATS",    fun_stats,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"STRCAT",   fun_strcat,   MAX_ARG, 0,  MAX_ARG, 0, CA_PUBLIC},
    {"STRIPANSI",fun_stripansi,MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"STRLEN",   fun_strlen,   1,       0,  1,       0, CA_PUBLIC},
    {"STRMATCH", fun_strmatch, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"STRTRUNC", fun_strtrunc, MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SUB",      fun_sub,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"SUBEVAL",  fun_subeval,  MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"SUBJ",     fun_subj,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"SWITCH",   fun_switch,   MAX_ARG, 2,  MAX_ARG, FN_NO_EVAL, CA_PUBLIC},
    {"T",        fun_t,        1,       0,  1,       0, CA_PUBLIC},
    {"TABLE",    fun_table,    MAX_ARG, 1,  6,       0, CA_PUBLIC},
    {"TAN",      fun_tan,      MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"TEL",      fun_tel,      MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"TIME",     fun_time,     MAX_ARG, 0,  1,       0, CA_PUBLIC},
    {"TIMEFMT",  fun_timefmt,  MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"TRANSLATE",fun_translate,MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"TRIM",     fun_trim,     MAX_ARG, 1,  3,       0, CA_PUBLIC},
    {"TRUNC",    fun_trunc,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"TYPE",     fun_type,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"U",        fun_u,        MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"UCSTR",    fun_ucstr,    1,       1,  1,       0, CA_PUBLIC},
    {"UDEFAULT", fun_udefault, MAX_ARG, 2,  MAX_ARG, FN_NO_EVAL, CA_PUBLIC},
    {"ULOCAL",   fun_ulocal,   MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"UNPACK",   fun_unpack,   MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"V",        fun_v,        MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"VADD",     fun_vadd,     MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"VALID",    fun_valid,    MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"VCROSS",   fun_vcross,   MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"VDIM",     fun_vdim,     MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"VDOT",     fun_vdot,     MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"VERSION",  fun_version,  MAX_ARG, 0,  0,       0, CA_PUBLIC},
    {"VISIBLE",  fun_visible,  MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"VMAG",     fun_vmag,     MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"VMUL",     fun_vmul,     MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"VSUB",     fun_vsub,     MAX_ARG, 2,  4,       0, CA_PUBLIC},
    {"VUNIT",    fun_vunit,    MAX_ARG, 1,  2,       0, CA_PUBLIC},
    {"WHERE",    fun_where,    MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"WORDPOS",  fun_wordpos,  MAX_ARG, 2,  3,       0, CA_PUBLIC},
    {"WORDS",    fun_words,    MAX_ARG, 0,  2,       0, CA_PUBLIC},
    {"WRITETIME",fun_writetime,MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"XGET",     fun_xget,     MAX_ARG, 2,  2,       0, CA_PUBLIC},
    {"XOR",      fun_xor,      MAX_ARG, 1,  MAX_ARG, 0, CA_PUBLIC},
    {"ZFUN",     fun_zfun,     MAX_ARG, 2,  11,      0, CA_PUBLIC},
    {"ZONE",     fun_zone,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {"ZWHO",     fun_zwho,     MAX_ARG, 1,  1,       0, CA_PUBLIC},
    {NULL,       NULL,         MAX_ARG, 0,  0,       0, 0}
};


void NDECL(init_functab)
{
    char *buff = alloc_sbuf("init_functab");
    for (FUN *fp = flist; fp->name; fp++)
    {
        memcpy(buff, fp->name, SBUF_SIZE);
        buff[SBUF_SIZE-1] = '\0';
        _strlwr(buff);
        hashaddLEN(buff, strlen(buff), (int *)fp, &mudstate.func_htab);
    }
    free_sbuf(buff);
    ufun_head = NULL;
}

void do_function
(
    dbref player,
    dbref cause,
    int   key,
    int   nargs,
    char *fname,
    char *target
)
{
    UFUN *ufp, *ufp2;
    ATTR *ap;
    char *np, *bp;
    int atr, aflags;
    dbref obj, aowner;

    if ((key & FN_LIST) || !fname || *fname == '\0')
    {
        notify(player, tprintf("%-28s   %-8s  %-30s Flgs",
            "Function Name", "DBref#", "Attribute"));
        notify(player, tprintf("%28s   %8s  %30s %4s",
            "----------------------------", "--------",
            "------------------------------", " -- "));

        int count = 0;
        for (ufp2 = ufun_head; ufp2; ufp2 = ufp2->next)
        {
            const char *pName = "(WARNING: Bad Attribute Number)";
            ap = atr_num(ufp2->atr);
            if (ap)
            {
                pName = ap->name;
            }
            notify(player, tprintf("%-28.28s   #%-7d  %-30.30s  %c%c",
                ufp2->name, ufp2->obj, pName, ((ufp2->flags & FN_PRIV) ? 'W' : '-'),
                ((ufp2->flags & FN_PRES) ? 'p' : '-')));
            count++;
        }

        notify(player, tprintf("%28s   %8s  %30s %4s",
            "----------------------------", "--------",
            "------------------------------", " -- "));

        notify(player, tprintf("Total User-Defined Functions: %d", count));
        return;
    }

    // Make a local uppercase copy of the function name.
    //
    bp = np = alloc_sbuf("add_user_func");
    safe_sb_str(fname, np, &bp);
    *bp = '\0';
    _strlwr(np);

    // Verify that the function doesn't exist in the builtin table.
    //
    if (hashfindLEN(np, strlen(np), &mudstate.func_htab) != NULL)
    {
        notify_quiet(player, "Function already defined in builtin function table.");
        free_sbuf(np);
        return;
    }

    // Make sure the target object exists.
    //
    if (!parse_attrib(player, target, &obj, &atr))
    {
        notify_quiet(player, NOMATCH_MESSAGE);
        free_sbuf(np);
        return;
    }


    // Make sure the attribute exists.
    //
    if (atr == NOTHING)
    {
        notify_quiet(player, "No such attribute.");
        free_sbuf(np);
        return;
    }

    // Make sure attribute is readably by me.
    //
    ap = atr_num(atr);
    if (!ap)
    {
        notify_quiet(player, "No such attribute.");
        free_sbuf(np);
        return;
    }
    atr_get_info(obj, atr, &aowner, &aflags);
    if (!See_attr(player, obj, ap, aowner, aflags))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        free_sbuf(np);
        return;
    }

    // Privileged functions require you control the obj.
    //
    if ((key & FN_PRIV) && !Controls(player, obj))
    {
        notify_quiet(player, NOPERM_MESSAGE);
        free_sbuf(np);
        return;
    }

    // See if function already exists.  If so, redefine it.
    //
    ufp = (UFUN *) hashfindLEN(np, strlen(np), &mudstate.ufunc_htab);

    if (!ufp)
    {
        ufp = (UFUN *) MEMALLOC(sizeof(UFUN));
        (void)ISOUTOFMEMORY(ufp);
        ufp->name = StringClone(np);
        _strupr(ufp->name);
        ufp->obj = obj;
        ufp->atr = atr;
        ufp->perms = CA_PUBLIC;
        ufp->next = NULL;
        if (!ufun_head)
        {
            ufun_head = ufp;
        }
        else
        {
            for (ufp2 = ufun_head; ufp2->next; ufp2 = ufp2->next)
            {
                // Nothing
                ;
            }
            ufp2->next = ufp;
        }
        hashaddLEN(np, strlen(np), (int *)ufp, &mudstate.ufunc_htab);
    }
    ufp->obj = obj;
    ufp->atr = atr;
    ufp->flags = key;
    free_sbuf(np);
    if (!Quiet(player))
    {
        notify_quiet(player, tprintf("Function %s defined.", fname));
    }
}

/*
 * ---------------------------------------------------------------------------
 * * list_functable: List available functions.
 */

void list_functable(dbref player)
{
    FUN *fp;
    UFUN *ufp;
    char *buf, *bp, *cp;

    buf = alloc_lbuf("list_functable");
    bp = buf;
    for (cp = (char *)"Functions:"; *cp; cp++)
        *bp++ = *cp;
    for (fp = flist; fp->name; fp++) {
        if (check_access(player, fp->perms)) {
            *bp++ = ' ';
            for (cp = (char *)(fp->name); *cp; cp++)
                *bp++ = *cp;
        }
    }
    for (ufp = ufun_head; ufp; ufp = ufp->next) {
        if (check_access(player, ufp->perms)) {
            *bp++ = ' ';
            for (cp = (char *)(ufp->name); *cp; cp++)
                *bp++ = *cp;
        }
    }
    *bp = '\0';
    notify(player, buf);
    free_lbuf(buf);
}

/*
 * ---------------------------------------------------------------------------
 * * cf_func_access: set access on functions
 */

CF_HAND(cf_func_access)
{
    FUN *fp;
    UFUN *ufp;
    char *ap;

    for (ap = str; *ap && !Tiny_IsSpace[(unsigned char)*ap]; ap++) ;
    if (*ap)
        *ap++ = '\0';

    for (fp = flist; fp->name; fp++)
    {
        if (!string_compare(fp->name, str))
        {
            return cf_modify_bits(&fp->perms, ap, pExtra, nExtra, player, cmd);
        }
    }
    for (ufp = ufun_head; ufp; ufp = ufp->next)
    {
        if (!string_compare(ufp->name, str))
        {
            return cf_modify_bits(&ufp->perms, ap, pExtra, nExtra, player, cmd);
        }
    }
    cf_log_notfound(player, cmd, "Function", str);
    return -1;
}

/////////////////////////////////////////////////////////////////
// Function : iadd(Arg[0], Arg[1],..,Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_iadd)
{
    INT64 sum = 0;
    for (int i = 0; i < nfargs; i++)
    {
        sum += Tiny_atoi64(fargs[i]);
    }
    safe_i64toa(sum, buff, bufc);
}

/////////////////////////////////////////////////////////////////
// Function : isub(Arg[0], Arg[1])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_isub)
{
    INT64 diff = Tiny_atoi64(fargs[0]) - Tiny_atoi64(fargs[1]);
    safe_i64toa(diff, buff, bufc);
}

/////////////////////////////////////////////////////////////////
// Function : imul(Arg[0], Arg[1], ... , Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_imul)
{
    INT64 prod = 1;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= Tiny_atoi64(fargs[i]);
    }
    safe_i64toa(prod, buff, bufc);
}

// fun_abs: Returns the absolute value of its argument.
//
FUNCTION(fun_iabs)
{
    INT64 num = Tiny_atoi64(fargs[0]);

    if (num == 0)
    {
        safe_chr('0', buff, bufc);
    }
    else if (num < 0)
    {
        safe_i64toa(-num, buff, bufc);
    }
    else
    {
        safe_i64toa(num, buff, bufc);
    }
}

// fun_isign: Returns -1, 0, or 1 based on the the sign of its argument.
//
FUNCTION(fun_isign)
{
    INT64 num = Tiny_atoi64(fargs[0]);

    if (num < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_chr((num > 0) ? '1' : '0', buff, bufc);
    }
}

typedef struct
{
    int  iBase;
    char chLetter;
    int  nName;
    char *pName;

} RADIX_ENTRY;

#define N_RADIX_ENTRIES 4
const RADIX_ENTRY reTable[N_RADIX_ENTRIES] =
{
    { 86400, 'd', 3, "day"    },
    {  3600, 'h', 4, "hour"   },
    {    60, 'm', 6, "minute" },
    {     1, 's', 6, "second" }
};

#define IDAYS    0
#define IHOURS   1
#define IMINUTES 2
#define ISECONDS 3

// This routine supports most of the time formats using the above
// table.
//
void GeneralTimeConversion
(
    char *Buffer,
    long Seconds,
    int iStartBase,
    int iEndBase,
    BOOL bSingleTerm,
    BOOL bNames
)
{
    char *p = Buffer;

    if (Seconds < 0)
    {
        Seconds = 0;
    }

    for (int i = iStartBase; i <= iEndBase; i++)
    {
        if (reTable[i].iBase <= Seconds || i == iEndBase)
        {
            int iValue;

            // Division and remainder.
            //
            iValue = Seconds/reTable[i].iBase;
            Seconds -= iValue * reTable[i].iBase;

            if (iValue != 0 || i == iEndBase)
            {
                if (p != Buffer)
                {
                    *p++ = ' ';
                }
                p += Tiny_ltoa(iValue, p);
                if (bNames)
                {
                    // Use the names with the correct pluralization.
                    //
                    *p++ = ' ';
                    memcpy(p, reTable[i].pName, reTable[i].nName);
                    p += reTable[i].nName;
                    if (iValue != 1)
                    {
                        // More than one or zero.
                        //
                        *p++ = 's';
                    }
                }
                else
                {
                    *p++ = reTable[i].chLetter;
                }
            }
            if (bSingleTerm)
            {
                break;
            }
        }
    }
    *p++ = '\0';
}

// This buffer is used by:
//
//     time_format_1 (23 bytes) uses TimeBuffer64,
//     time_format_2 (17 bytes) uses TimeBuffer32,
//     expand_time   (29 bytes) uses TimeBuffer32,
//     write_time    (52 bytes) uses TimeBuffer64.
//
// time_format_1 and time_format_2 are called from within the same
// printf, so they must use different buffers.
//
// We pick 32 and 64 as a round numbers.
//
static char TimeBuffer32[32];
static char TimeBuffer64[64];

// Show time in days, hours, and minutes
//
// 2^63/86400 is 1.07E14 which is at most 15 digits.
// '(15)d (2):(2)\0' is at most 23 characters.
//
const char *time_format_1(int Seconds)
{
    if (Seconds < 0)
    {
        Seconds = 0;
    }

    // We are showing the time in minutes. 59s --> 0m
    //

    // Divide the time down into days, hours, and minutes.
    //
    int Days = Seconds / 86400;
    Seconds -= Days * 86400;

    int Hours = Seconds / 3600;
    Seconds -= Hours * 3600;

    int Minutes = Seconds / 60;

    if (Days > 0)
    {
        sprintf(TimeBuffer64, "%dd %02d:%02d", Days, Hours, Minutes);
    }
    else
    {
        sprintf(TimeBuffer64, "%02d:%02d", Hours, Minutes);
    }
    return TimeBuffer64;
}

// Show time in days, hours, minutes, or seconds.
//
const char *time_format_2(int Seconds)
{
    // 2^63/86400 is 1.07E14 which is at most 15 digits.
    // '(15)d\0' is at most 17 characters.
    //
    GeneralTimeConversion(TimeBuffer32, Seconds, IDAYS, ISECONDS, TRUE, FALSE);
    return TimeBuffer32;
}

// Del's added functions for dooferMUX ! :)
// D.Piper (del@doofer.org) 1997 & 2000
//

// expand_time - Written (short) time format.
//
const char *expand_time(int Seconds)
{
    // 2^63/86400 is 1.07E14 which is at most 15 digits.
    // '(15)d (2)h (2)m (2)s\0' is at most 29 characters.
    //
    GeneralTimeConversion(TimeBuffer32, Seconds, IDAYS, ISECONDS, FALSE, FALSE);
    return TimeBuffer32;
}

// write_time - Written (long) time format.
//
const char *write_time(int Seconds)
{
    // 2^63/86400 is 1.07E14 which is at most 15 digits.
    // '(15) days (2) hours (2) minutes (2) seconds\0' is at most
    // 52 characters.
    //
    GeneralTimeConversion(TimeBuffer64, Seconds, IDAYS, ISECONDS, FALSE, TRUE);
    return TimeBuffer64;
}

// digittime - Digital format time ([(days)d]HH:MM) from given
// seconds. D.Piper - May 1997 & April 2000
//
FUNCTION(fun_digittime)
{
    int tt = Tiny_atol(fargs[0]);
    safe_str(time_format_1(tt), buff, bufc);
}

// singletime - Single element time from given seconds.
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_singletime)
{
    int tt = Tiny_atol(fargs[0]);
    safe_str(time_format_2(tt), buff, bufc);
}

// exptime - Written (short) time from given seconds
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_exptime)
{
    int tt = Tiny_atol(fargs[0]);
    safe_str(expand_time(tt), buff, bufc);
}

// writetime - Written (long) time from given seconds
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_writetime)
{
    int tt = Tiny_atol(fargs[0]);
    safe_str(write_time(tt), buff, bufc);
}

// cmds - Return player command count (Wizard_Who OR Self ONLY)
// D.Piper - May 1997
//
FUNCTION(fun_cmds)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target) && Connected(target))
    {
        if (!(Wizard_Who(player) || Controls(player, target)))
        {
            target = NOTHING;
        }
        safe_ltoa(fetch_cmds(target), buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// startsecs - Time the MUX was started, in seconds
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_startsecs)
{
    CLinearTimeAbsolute lta;
    lta = mudstate.start_time;
    lta.Local2UTC();
    safe_str(lta.ReturnSecondsString(), buff, bufc);
}

// conntotal - Return player's total online time to the MUX
// (including their current connection). D.Piper - May 1997
//
FUNCTION(fun_conntotal)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        long TotalTime = fetch_totaltime(target);
        if (Connected(target))
        {
            TotalTime += fetch_connect(target);
        }
        safe_ltoa(TotalTime, buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connmax - Return player's longest session to the MUX
// (including the current one). D.Piper - May 1997
//
FUNCTION(fun_connmax)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        long Longest = fetch_longestconnect(target);
        long Current = fetch_connect(target);
        if (Longest < Current)
        {
            Longest = Current;
        }
        safe_ltoa(Longest, buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connlast - Return player's last connection time to the MUX
// D.Piper - May 1997
//
FUNCTION(fun_connlast)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        safe_ltoa(fetch_lastconnect(target), buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connnum - Return the total number of sessions this player has had
// to the MUX (including any current ones). D.Piper - May 1997
//
FUNCTION(fun_connnum)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        long NumConnections = fetch_numconnections(target);
        if (Connected(target))
        {
            NumConnections += fetch_session(target);
        }
        safe_ltoa(NumConnections, buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connleft - Return when a player last logged off the MUX as
// UTC seconds. D.Piper - May 1997
//
FUNCTION(fun_connleft)
{
    dbref target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target))
    {
        CLinearTimeAbsolute cl = fetch_logouttime(target);
        safe_str(cl.ReturnSecondsString(), buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// lattrcmds - Output a list of all attributes containing $ commands.
// Altered from lattr(). D.Piper - May 1997 & April 2000
//
FUNCTION(fun_lattrcmds)
{
    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    dbref thing;
    BOOL isFirst = TRUE;
    olist_push();
    if (parse_attrib_wild(player, fargs[0], &thing, 0, 0, 1))
    {
        char *buf = alloc_lbuf("fun_lattrcmds");
        for (int ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *attr = atr_num(ca);
            if (attr)
            {
                dbref aowner;
                int   aflags;
                atr_get_str(buf, thing, attr->number, &aowner, &aflags);
                if (buf[0] == '$')
                {
                    if (!isFirst)
                    {
                        safe_chr(' ', buff, bufc);
                    }
                    isFirst = FALSE;
                    safe_str(attr->name, buff, bufc);
                }
            }
        }
        free_lbuf(buf);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

// lcmds - Output a list of all $ commands on an object.
// Altered from MUX lattr(). D.Piper - May 1997 & April 2000
// Modified to handle spaced commands and ^-listens - July 2001 (Ash)
// Applied patch and code reviewed - February 2002 (Stephen)
//
FUNCTION(fun_lcmds)
{
    char sep;
    if (!delim_check(fargs, nfargs, 2, &sep, buff, bufc, 0, player,
                     cause, cargs, ncargs, 1))
    {
        return;
    }
    
    // Check to see what type of command matching we will do. '$' commands
    // or '^' listens.  We default with '$' commands.
    //
    char cmd_type = '$';
    if (  nfargs == 3
        && (*fargs[2] == '$' || *fargs[2] == '^'))
    {
        cmd_type = *fargs[2];
    }
    
    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    dbref thing;
    BOOL isFirst = TRUE;
    olist_push();
    if (parse_attrib_wild(player, fargs[0], &thing, 0, 0, 1))
    {
        char *buf = alloc_lbuf("fun_lattrcmds");
        dbref aowner;
        int   aflags;
        for (int ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *attr = atr_num(ca);
            if (attr)
            {
                atr_get_str(buf, thing, attr->number, &aowner, &aflags);
                if (buf[0] == cmd_type)
                {
                    BOOL isFound = FALSE;
                    char *c_ptr = buf+1;
                    
                    // If there is no characters between the '$' or '^' and the
                    // ':' it's not a valid command, so skip it.
                    //
                    if (*c_ptr != ':') 
                    {
                        int isEscaped = FALSE;
                        while (*c_ptr && !isFound) 
                        {
                            // We need to check if the ':' in the command is
                            // escaped.
                            //
                            if (*c_ptr == '\\')
                            {
                                isEscaped = !isEscaped;
                            }
                            else if (*c_ptr == ':' && !isEscaped) 
                            {
                                isFound = TRUE;
                                *c_ptr = '\0';
                            }
                            else if (*c_ptr != '\\' && isEscaped)
                            {
                                isEscaped = FALSE;
                            }
                            c_ptr++;
                        }
                    }
                    
                    // We don't want to bother printing out the $command
                    // if it doesn't have a matching ':'.  It isn't a valid
                    // command then.
                    //
                    if (isFound)
                    {
                        if (!isFirst)
                        {
                            print_sep(sep, buff, bufc);
                        }
                        
                        _strlwr(buf);
                        safe_str(buf+1, buff, bufc);
                        
                        isFirst = FALSE;
                    }
                }
            }
        }
        free_lbuf(buf);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

extern FLAGNAMEENT gen_flag_names[];

// lflags - List flags as names - (modified from 'flag_description()' and
// MUX flags(). D.Piper - May 1997 & May 2000
//
FUNCTION(fun_lflags)
{
    BOOL bFirst = TRUE;
    dbref target = match_thing(player, fargs[0]);
    if (  (target != NOTHING)
       && (mudconf.pub_flags || Examinable(player, target) || (target == cause)))
    {
        FLAGNAMEENT *fp;
        for (fp = gen_flag_names; fp->flagname; fp++)
        {
            if (!fp->bPositive)
            {
                continue;
            }
            FLAGBITENT *fbe = fp->fbe;
            if (db[target].fs.word[fbe->flagflag] & fbe->flagvalue)
            {
                if (  (  (fbe->listperm & CA_STAFF)
                      && !Staff(player))
                   || (  (fbe->listperm & CA_ADMIN)
                      && !WizRoy(player))
                   || (  (fbe->listperm & CA_WIZARD)
                      && !Wizard(player))
                   || (  (fbe->listperm & CA_GOD)
                      && !God(player)))
                {
                    continue;
                }
                if (  isPlayer(target)
                   && (fbe->flagvalue == CONNECTED)
                   && (fbe->flagflag == FLAG_WORD2)
                   && Hidden(target)
                   && !See_Hidden(player))
                {
                    continue;
                }

                if (!bFirst)
                {
                    safe_chr(' ', buff, bufc);
                }
                bFirst = FALSE;

                safe_str(fp->flagname, buff, bufc);
            }
        }
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_art:
//
// Accepts a single argument. Based on the rules specified in the config
// parameters article_regexp and article_exception it determines whether the
// word should use 'an' or 'a' as its article.
//
// By default if a word matches the regexp specified in article_regexp then
// this function will return 'an', otherwise it will return 'a'. If, however
// the word also matches one of the specified article_exception regexp's
// will return the given article.
//

FUNCTION(fun_art)
{
    // Drop the input string into lower case.
    //
    _strlwr(fargs[0]);

    // Search for exceptions.
    //
    ArtRuleset *arRule = mudconf.art_rules;

    while (arRule != NULL)
    {
        regexp* reRuleRegexp = (regexp *) arRule->m_pRegexp;

        if (regexec(reRuleRegexp, fargs[0]))
        {
            safe_str(arRule->m_bUseAn ? "an" : "a", buff, bufc);
            return;
        }

        arRule = arRule->m_pNextRule;
    }

    // Default to 'a'.
    //
    safe_str( "a", buff, bufc);
}

