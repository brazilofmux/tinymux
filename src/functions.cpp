// functions.c - MUX function handlers 
//
// $Id: functions.cpp,v 1.1 2000-04-11 07:14:44 sdennis Exp $
//

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <limits.h>
#include <math.h>
#include <float.h>

#include "mudconf.h"
#include "db.h"
#include "flags.h"
#include "powers.h"
#include "attrs.h"
#include "match.h"
#include "command.h"
#include "functions.h"
#include "misc.h"
#include "alloc.h"
#include "ansi.h"
#include "comsys.h"

UFUN *ufun_head;

extern NAMETAB indiv_attraccess_nametab[];

extern void FDECL(cf_log_notfound, (dbref player, char *cmd, const char *thingname, char *thing));

/*
 * Function definitions from funceval.c 
 */

#define XFUNCTION(x) extern void x(char *buff, char **bufc, dbref player, dbref cause, char *fargs[], int nfargs, char *cargs[], int ncargs)

XFUNCTION(fun_cwho);
XFUNCTION(fun_beep);
XFUNCTION(fun_ansi);
XFUNCTION(fun_zone);
#ifdef SIDE_EFFECT_FUNCTIONS
XFUNCTION(fun_link);
XFUNCTION(fun_tel);
XFUNCTION(fun_pemit);
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
XFUNCTION(fun_squish);
XFUNCTION(fun_stripansi);
XFUNCTION(fun_zfun);
XFUNCTION(fun_columns);
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
XFUNCTION(fun_lit);
XFUNCTION(fun_shl);
XFUNCTION(fun_shr);
XFUNCTION(fun_band);
XFUNCTION(fun_bor);
XFUNCTION(fun_bnand);
XFUNCTION(fun_vadd);
XFUNCTION(fun_vsub);
XFUNCTION(fun_vmul);
XFUNCTION(fun_vmag);
XFUNCTION(fun_vunit);
XFUNCTION(fun_vdim);
XFUNCTION(fun_strcat);
XFUNCTION(fun_grep);
XFUNCTION(fun_grepi);
XFUNCTION(fun_art);
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
 

/*
 * Trim off leading and trailing spaces if the separator char is a space 
 */

char *trim_space_sep(char *str, char sep)
{
    char *p;

    if (sep != ' ')
        return str;
    while (*str && (*str == ' '))
        str++;
    for (p = str; *p; p++) ;
    for (p--; *p == ' ' && p > str; p--) ;
    p++;
    *p = '\0';
    return str;
}

/*
 * next_token: Point at start of next token in string 
 */

char *next_token(char *str, char sep)
{
    while (*str && (*str != sep))
        str++;
    if (!*str)
        return NULL;
    str++;
    if (sep == ' ') {
        while (*str == sep)
            str++;
    }
    return str;
}

/*
 * split_token: Get next token from string as null-term string.  String is
 * * destructively modified.
 */

char *split_token(char **sp, char sep)
{
    char *str, *save;

    save = str = *sp;
    if (!str) {
        *sp = NULL;
        return NULL;
    }
    while (*str && (*str != sep))
        str++;
    if (*str) {
        *str++ = '\0';
        if (sep == ' ') {
            while (*str == sep)
                str++;
        }
    } else {
        str = NULL;
    }
    *sp = str;
    return save;
}

/*
 * ---------------------------------------------------------------------------
 * * List management utilities.
 */

#define ALPHANUM_LIST   1
#define NUMERIC_LIST    2
#define DBREF_LIST  3
#define FLOAT_LIST  4

static int autodetect_list(char *ptrs[], int nitems)
{
    int sort_type, i;
    char *p;

    sort_type = NUMERIC_LIST;
    for (i = 0; i < nitems; i++) {
        switch (sort_type) {
        case NUMERIC_LIST:
            if (!is_number(ptrs[i])) {

                /*
                 * If non-numeric, switch to alphanum sort. * 
                 * 
                 * *  * *  * * Exception: if this is the
                 * first * element * * and * it is a good
                 * dbref, * switch to a * * dbref sort. *
                 * We're a * little looser than *  * the
                 * normal * 'good  * dbref' rules, any * *
                 * number following # * the #-sign is
                 * accepted.  
                 */

                if (i == 0) {
                    p = ptrs[i];
                    if (*p++ != NUMBER_TOKEN) {
                        return ALPHANUM_LIST;
                    } else if (is_integer(p)) {
                        sort_type = DBREF_LIST;
                    } else {
                        return ALPHANUM_LIST;
                    }
                } else {
                    return ALPHANUM_LIST;
                }
            } else if (strchr(ptrs[i], '.')) {
                sort_type = FLOAT_LIST;
            }
            break;
        case FLOAT_LIST:
            if (!is_number(ptrs[i])) {
                sort_type = ALPHANUM_LIST;
                return ALPHANUM_LIST;
            }
            break;
        case DBREF_LIST:
            p = ptrs[i];
            if (*p++ != NUMBER_TOKEN)
                return ALPHANUM_LIST;
            if (!is_integer(p))
                return ALPHANUM_LIST;
            break;
        default:
            return ALPHANUM_LIST;
        }
    }
    return sort_type;
}

static int get_list_type(char *fargs[], int nfargs, int type_pos, char *ptrs[], int nitems)
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
            return ALPHANUM_LIST;
        }
    }
    return autodetect_list(ptrs, nitems);
}

int list2arr(char *arr[], int maxlen, char *list, char sep)
{
    char *p;
    int i;

    list = trim_space_sep(list, sep);
    p = split_token(&list, sep);
    for (i = 0; p && i < maxlen; i++, p = split_token(&list, sep)) {
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
    if ((strlen(dbr) < 2) && (*dbr != '#'))
        return 0;
    else
        return Tiny_atol(dbr + 1);
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

static char *strip_useless_zeroes(char *pBegin, char *pEnd)
{
    // Remove useless trailing 0's between pBegin and pEnd.
    //
    while (pBegin <= pEnd && pEnd[0] == '0')
    {
        pEnd--;
    }

    // Take care of dangling '.'
    //
    if (pBegin <= pEnd && pEnd[0] == '.')
    {
        pEnd--;
    }

    // Terminate string.
    //
    pEnd++;
    pEnd[0] = '\0';
    return pEnd;
}

#ifdef HAVE_IEEE_FP_FORMAT

char *TinyFPStrings[] = { "+Inf", "-Inf", "Ind", "NaN", "0", "0", "0", "0" };

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
#define IEEE_MASK_EXPONENT 0x7FF0000000000000i64
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFi64
#define IEEE_MASK_QNAN     0x0008000000000000i64
#else
#define IEEE_MASK_SIGN     0x8000000000000000ull
#define IEEE_MASK_EXPONENT 0x7FF0000000000000ull
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFull
#define IEEE_MASK_QNAN     0x0008000000000000ull
#endif

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
    char *pBegin = *bufc;

    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(result);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.6f", result);

        char *pEnd = (*bufc) - 1;

        // Remove useless trailing 0's between pBegin and pEnd.
        //
        pEnd = strip_useless_zeroes(pBegin, pEnd);

        // Update bufc pointer.
        //
        *bufc = pEnd;
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
    char *pBegin = buff;

    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(result);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        sprintf(buff, "%.6f", result);

        char *pEnd = buff + strlen(buff) - 1;

        // Remove useless trailing 0's between pBegin and pEnd.
        //
        strip_useless_zeroes(pBegin, pEnd);
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
 * * fn_range_check: Check # of args to a function with an optional argument
 * * for validity.
 */

int fn_range_check(const char *fname, int nfargs, int minargs, int maxargs, char *result, char **bufc)
{
    if ((nfargs >= minargs) && (nfargs <= maxargs))
        return 1;
    
    if (maxargs == (minargs + 1))
        safe_tprintf_str(result, bufc, "#-1 FUNCTION (%s) EXPECTS %d OR %d ARGUMENTS", fname, minargs, maxargs);
    else
        safe_tprintf_str(result, bufc, "#-1 FUNCTION (%s) EXPECTS BETWEEN %d AND %d ARGUMENTS", fname, minargs, maxargs);
    return 0;
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
            TinyExec(tstr, &bp, 0, player, cause, EV_EVAL | EV_FCHECK, &str, cargs, ncargs);
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
        safe_str("0", buff, bufc);
        return;
    }
    varargs_preamble("WORDS", 2);
    safe_tprintf_str(buff, bufc, "%d", countwords(fargs[0], sep));
}

/*
 * fun_flags: Returns the flags on an object.
 * Because @switch is case-insensitive, not quite as useful as it could be.
 */

FUNCTION(fun_flags)
{
    dbref it;
    char *buff2;

    it = match_thing(player, fargs[0]);
    if ((it != NOTHING) &&
        (mudconf.pub_flags || Examinable(player, it) || (it == cause))) {
        buff2 = unparse_flags(player, it);
        safe_str(buff2, buff, bufc);
        free_sbuf(buff2);
    } else
        safe_str("#-1", buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rand: Return a random number from 0 to arg1-1
 */

FUNCTION(fun_rand)
{
    int num;

    num = Tiny_atol(fargs[0]);
    if (num < 1)
        safe_str("0", buff, bufc);
    else
        safe_tprintf_str(buff, bufc, "%ld", RandomLong(0,num-1));
}

/*
 * ---------------------------------------------------------------------------
 * * fun_abs: Returns the absolute value of its argument.
 */

FUNCTION(fun_abs)
{
    double num;

    num = safe_atof(fargs[0]);
    if (num == 0.0) {
        safe_str("0", buff, bufc);
    } else if (num < 0.0) {
        fval(buff, bufc, -num);
    } else {
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

    num = safe_atof(fargs[0]);
    if (num < 0)
        safe_str("-1", buff, bufc);
    else if (num > 0)
        safe_str("1", buff, bufc);
    else
        safe_str("0", buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_time: Returns nicely-formatted time.
 */

FUNCTION(fun_time)
{
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    char *temp = ltaNow.ReturnDateString();
    safe_str(temp, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_time: Seconds since 0:00 1/1/70
 */

FUNCTION(fun_secs)
{
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetLocal();
    safe_tprintf_str(buff, bufc, "%s", lsaNow.ReturnSecondsString());
}

/*
 * ---------------------------------------------------------------------------
 * * fun_convsecs: converts seconds to time string, based off 0:00 1/1/70
 */

FUNCTION(fun_convsecs)
{
    CLinearTimeAbsolute lta;
    lta.SetSecondsString(fargs[0]);
    char *temp = lta.ReturnDateString();
    safe_str(temp, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_convtime: converts time string to seconds, based off 0:00 1/1/70
//
FUNCTION(fun_convtime)
{
    CLinearTimeAbsolute lta;
    if (lta.SetString(fargs[0]))
    {
        safe_str(Tiny_i64toa_t(lta.ReturnSeconds()), buff, bufc);
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

/*
 * ---------------------------------------------------------------------------
 * * fun_get, fun_get_eval: Get attribute from object.
 */

int check_read_perms(dbref player, dbref thing, ATTR *attr, int aowner, int aflags, char *buff, char **bufc)
{
    int see_it;

    /*
     * If we have explicit read permission to the attr, return it 
     */

    if (See_attr_explicit(player, thing, attr, aowner, aflags))
        return 1;

    /*
     * If we are nearby or have examine privs to the attr and it is * * * 
     * 
     * * visible to us, return it. 
     */

    see_it = See_attr(player, thing, attr, aowner, aflags);
    if ((Examinable(player, thing) || nearby(player, thing) || See_All(player)) && see_it)
        return 1;

    /*
     * For any object, we can read its visible attributes, EXCEPT * for * 
     * 
     * *  * * descs, which are only visible if read_rem_desc is on. 
     */

    if (see_it) {
        if (!mudconf.read_rem_desc && (attr->number == A_DESC)) {
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return 0;
        } else {
            return 1;
        }
    }
    safe_str("#-1 PERMISSION DENIED", buff, bufc);
    return 0;
}

FUNCTION(fun_get)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags;
    ATTR *attr;
    char *atr_gotten;
    struct boolexp *pBoolExp;

    if (!parse_attrib(player, fargs[0], &thing, &attrib)) {
        safe_str("#-1 NO MATCH", buff, bufc);
        return;
    }
    if (attrib == NOTHING) {
        return;
    }
    free_buffer = 1;
    attr = atr_num(attrib); /*
                 * We need the attr's flags for this: 
                 */
    if (!attr) {
        return;
    }
    if (attr->flags & AF_IS_LOCK) {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags)) {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        } else {
            free_lbuf(atr_gotten);
            atr_gotten = (char *)"#-1 PERMISSION DENIED";
        }
        free_buffer = 0;
    } else {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }

    /*
     * Perform access checks.  c_r_p fills buff with an error message * * 
     * 
     * *  * * if needed. 
     */

    if (check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
        safe_str(atr_gotten, buff, bufc);
    if (free_buffer)
        free_lbuf(atr_gotten);
    return;
}

FUNCTION(fun_xget)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags;
    ATTR *attr;
    char *atr_gotten;
    struct boolexp *pBoolExp;

    if (!*fargs[0] || !*fargs[1])
        return;

    if (!parse_attrib(player, tprintf("%s/%s", fargs[0], fargs[1]),
              &thing, &attrib)) {
        safe_str("#-1 NO MATCH", buff, bufc);
        return;
    }
    if (attrib == NOTHING) {
        return;
    }
    free_buffer = 1;
    attr = atr_num(attrib); /*
                 * We need the attr's flags for this: 
                 */
    if (!attr) {
        return;
    }
    if (attr->flags & AF_IS_LOCK) {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags)) {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        } else {
            free_lbuf(atr_gotten);
            atr_gotten = (char *)"#-1 PERMISSION DENIED";
        }
        free_buffer = 0;
    } else {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }

    /*
     * Perform access checks.  c_r_p fills buff with an error message * * 
     * 
     * *  * * if needed. 
     */

    if (check_read_perms(player, thing, attr, aowner, aflags, buff, bufc))
        safe_str(atr_gotten, buff, bufc);
    if (free_buffer)
        free_lbuf(atr_gotten);
    return;
}

FUNCTION(fun_get_eval)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags, eval_it;
    ATTR *attr;
    char *atr_gotten, *str;
    struct boolexp *pBoolExp;

    if (!parse_attrib(player, fargs[0], &thing, &attrib)) {
        safe_str("#-1 NO MATCH", buff, bufc);
        return;
    }
    if (attrib == NOTHING) {
        return;
    }
    free_buffer = 1;
    eval_it = 1;
    attr = atr_num(attrib); /*
                 * We need the attr's flags for this: 
                 */
    if (!attr) {
        return;
    }
    if (attr->flags & AF_IS_LOCK) {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags)) {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        } else {
            free_lbuf(atr_gotten);
            atr_gotten = (char *)"#-1 PERMISSION DENIED";
        }
        free_buffer = 0;
        eval_it = 0;
    } else {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }
    if (!check_read_perms(player, thing, attr, aowner, aflags, buff, bufc)) {
        if (free_buffer)
            free_lbuf(atr_gotten);
        return;
    }
    if (eval_it)
    {
        str = atr_gotten;
        TinyExec(buff, bufc, 0, thing, player, EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
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
    char *str;
    
    if (nfargs != 1) {
        safe_str("#-1 FUNCTION (EVALNOCOMP) EXPECTS 1 OR 2 ARGUMENTS", buff, bufc);
        return;
    }
    
    str = fargs[0];
    TinyExec(buff, bufc, 0, player, cause, EV_EVAL|EV_NO_LOCATION|EV_NOFCHECK|EV_FIGNORE|EV_NO_COMPRESS, &str, (char **)NULL, 0);
}   

FUNCTION(fun_eval)
{
    dbref thing, aowner;
    int attrib, free_buffer, aflags, eval_it;
    ATTR *attr;
    char *atr_gotten, *str;
    struct boolexp *pBoolExp;

    if ((nfargs != 1) && (nfargs != 2)) {
        safe_str("#-1 FUNCTION (EVAL) EXPECTS 1 OR 2 ARGUMENTS", buff, bufc);
        return;
    }
    if (nfargs == 1) {
        str = fargs[0];
        TinyExec(buff, bufc, 0, player, cause, EV_EVAL, &str, (char **)NULL, 0);
        return;
    }
    if (!*fargs[0] || !*fargs[1])
        return;

    if (!parse_attrib(player, tprintf("%s/%s", fargs[0], fargs[1]),
              &thing, &attrib)) {
        safe_str("#-1 NO MATCH", buff, bufc);
        return;
    }
    if (attrib == NOTHING) {
        return;
    }
    free_buffer = 1;
    eval_it = 1;
    attr = atr_num(attrib);
    if (!attr) {
        return;
    }
    if (attr->flags & AF_IS_LOCK) {
        atr_gotten = atr_get(thing, attrib, &aowner, &aflags);
        if (Read_attr(player, thing, attr, aowner, aflags)) {
            pBoolExp = parse_boolexp(player, atr_gotten, 1);
            free_lbuf(atr_gotten);
            atr_gotten = unparse_boolexp(player, pBoolExp);
            free_boolexp(pBoolExp);
        } else {
            free_lbuf(atr_gotten);
            atr_gotten = (char *)"#-1 PERMISSION DENIED";
        }
        free_buffer = 0;
        eval_it = 0;
    } else {
        atr_gotten = atr_pget(thing, attrib, &aowner, &aflags);
    }
    if (!check_read_perms(player, thing, attr, aowner, aflags, buff, bufc)) {
        if (free_buffer)
            free_lbuf(atr_gotten);
        return;
    }
    if (eval_it) {
        str = atr_gotten;
        TinyExec(buff, bufc, 0, thing, player, EV_FIGNORE | EV_EVAL, &str, (char **)NULL, 0);
    } else {
        safe_str(atr_gotten, buff, bufc);
    }
    if (free_buffer)
        free_lbuf(atr_gotten);
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
    int aflags, anum, i;
    ATTR *ap;
    char *atext, *preserve[MAX_GLOBAL_REGS], *str;

    /*
     * We need at least one argument 
     */

    if (nfargs < 1) 
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    /*
     * Two possibilities for the first arg: <obj>/<attr> and <attr>. 
     */

    if (parse_attrib(player, fargs[0], &thing, &anum)) {
        if ((anum == NOTHING) || (!Good_obj(thing)))
            ap = NULL;
        else
            ap = atr_num(anum);
    } else {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    /*
     * Make sure we got a good attribute 
     */

    if (!ap) {
        return;
    }
    /*
     * Use it if we can access it, otherwise return an error. 
     */

    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext) {
        free_lbuf(atext);
        return;
    }
    if (!*atext) {
        free_lbuf(atext);
        return;
    }
    if (!check_read_perms(player, thing, ap, aowner, aflags, buff, bufc)) {
        free_lbuf(atext);
        return;
    }
    /*
     * If we're evaluating locally, preserve the global registers. 
     */

    if (is_local)
    {
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (!mudstate.global_regs[i])
                preserve[i] = NULL;
            else
            {
                preserve[i] = alloc_lbuf("u_regs");
                StringCopy(preserve[i], mudstate.global_regs[i]);
            }
        }
    }
    /*
     * Evaluate it using the rest of the passed function args 
     */

    str = atext;
    TinyExec(buff, bufc, 0, thing, cause, EV_FCHECK | EV_EVAL, &str, &(fargs[1]), nfargs - 1);
    free_lbuf(atext);

    /*
     * If we're evaluating locally, restore the preserved registers. 
     */

    if (is_local)
    {
        for (i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (preserve[i])
            {
                if (!mudstate.global_regs[i])
                    mudstate.global_regs[i] = alloc_lbuf("u_reg");
                StringCopy(mudstate.global_regs[i], preserve[i]);
                free_lbuf(preserve[i]);
            }
            else
            {
                if (mudstate.global_regs[i])
                    *(mudstate.global_regs[i]) = '\0';
            }
        }
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
        safe_str("#-1", buff, bufc);
    }
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parse: Make list from evaluating arg3 with each member of arg2.
 * * arg1 specifies a delimiter character to use in the parsing of arg2.
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_parse)
{
    char *curr, *objstring, *buff2, *buff3, *cp, sep;
    char *dp, *str;
    int first, number = 0;

    evarargs_preamble("PARSE", 3);
    cp = curr = dp = alloc_lbuf("fun_parse");
    str = fargs[0];
    TinyExec(curr, &dp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *dp = '\0';
    cp = trim_space_sep(cp, sep);
    if (!*cp) {
        free_lbuf(curr);
        return;
    }
    first = 1;
    while (cp)
    {
        if (!first)
            safe_chr(' ', buff, bufc);
        first = 0;
        number++;
        objstring = split_token(&cp, sep);
        buff2 = replace_string(BOUND_VAR, objstring, fargs[1]);
        buff3 = replace_string(LISTPLACE_VAR, Tiny_ltoa_t(number), buff2);
        str = buff3;
        TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buff2);
        free_lbuf(buff3);
    }
    free_lbuf(curr);
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

    if (  iPosition0 < 0 || iPosition0 >= LBUF_SIZE
       || nLength    < 0 || nLength    >= LBUF_SIZE
       || iPosition1 >= LBUF_SIZE)
    {
        safe_str("#-1 OUT OF RANGE", buff, bufc);
        return;
    }

    // At this point, iPosition0, nLength, and iPosition1 are reasonable
    // numbers which may -still- not refer to valid data in the string.
    //
    struct ANSI_Context ac;
    ANSI_String_Init(&ac, fargs[0], 0);
    int nDone;
    ANSI_String_Skip(&ac, iPosition0, &nDone);
    if (nDone < iPosition0)
    {
        return;
    }
    int nAvailable = LBUF_SIZE - (*bufc - buff);
    int nSize = ANSI_String_Copy(&ac, nAvailable, *bufc, nLength, &nDone);
    *bufc += nSize;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_first: Returns first word in a string
 */

FUNCTION(fun_first)
{
    char *s, *first, sep;

    /*
     * If we are passed an empty arglist return a null string 
     */

    if (nfargs == 0) {
        return;
    }
    varargs_preamble("FIRST", 2);
    s = trim_space_sep(fargs[0], sep);  /*
                         * leading spaces ... 
                         */
    first = split_token(&s, sep);
    if (first) {
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

    /*
     * If we are passed an empty arglist return a null string 
     */

    if (nfargs == 0) {
        return;
    }
    varargs_preamble("REST", 2);
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
    if (isalpha(tbuf[0]) && tbuf[1]) {

        /*
         * Fetch an attribute from me.  First see if it exists, * * * 
         * 
         * * returning a null string if it does not. 
         */

        ap = atr_str(fargs[0]);
        if (!ap) {
            return;
        }
        /*
         * If we can access it, return it, otherwise return a * null
         * * * * string 
         */

        atr_pget_info(player, ap->number, &aowner, &aflags);
        if (See_attr(player, player, ap, aowner, aflags)) {
            tbuf = atr_pget(player, ap->number, &aowner, &aflags);
            safe_str(tbuf, buff, bufc);
            free_lbuf(tbuf);
        }
        return;
    }
    /*
     * Not an attribute, process as %<arg> 
     */

    sbuf = alloc_sbuf("fun_v");
    sbufc = sbuf;
    safe_sb_chr('%', sbuf, &sbufc);
    safe_sb_str(fargs[0], sbuf, &sbufc);
    *sbufc = '\0';
    str = sbuf;
    TinyExec(buff, bufc, 0, player, cause, EV_EVAL|EV_FIGNORE, &str, cargs, ncargs);
    free_sbuf(sbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_s: Force substitution to occur.
 */

FUNCTION(fun_s)
{
    char *str;

    str = fargs[0];
    TinyExec(buff, bufc, 0, player, cause, EV_FIGNORE | EV_EVAL, &str, cargs, ncargs);
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
    safe_str("#-1", buff, bufc);
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
    safe_str("#-1", buff, bufc);
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
    safe_str("#-1", buff, bufc);
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
        safe_str("#-1", buff, bufc);
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
        safe_str("#-1", buff, bufc);
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
    safe_str("#-1", buff, bufc);
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
        safe_str("#-1", buff, bufc);
    } else if (isRoom(it)) {
        safe_tprintf_str(buff, bufc, "#%d", it);
    } else {
        safe_str("#-1", buff, bufc);
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
    dbref x, y;

    x = match_thing(player, fargs[0]);
    if (x == NOTHING) {
        safe_tprintf_str(buff, bufc, "%s", "#-1 ARG1 NOT FOUND");
        return;
    }
    y = match_thing(player, fargs[1]);
    if (y == NOTHING) {
        safe_tprintf_str(buff, bufc, "%s", "#-1 ARG2 NOT FOUND");
        return;
    }
    safe_tprintf_str(buff, bufc, "%d", Controls(x, y));
}

/*
 * ---------------------------------------------------------------------------
 * * fun_fullname: Return the fullname of an object (good for exits)
 */

FUNCTION(fun_fullname)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (it == NOTHING) {
        return;
    }
    if (!mudconf.read_rem_name) {
        if (!nearby_or_control(player, it) &&
            (!isPlayer(it))) {
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
    int wcount;
    char *r, *s, sep;

    varargs_preamble("MATCH", 3);

    /*
     * Check each word individually, returning the word number of the * * 
     * 
     * *  * * first one that matches.  If none match, return 0. 
     */

    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do {
        r = split_token(&s, sep);
        if (quick_wild(fargs[1], r)) {
            safe_tprintf_str(buff, bufc, "%d", wcount);
            return;
        }
        wcount++;
    } while (s);
    safe_str("0", buff, bufc);
}

FUNCTION(fun_strmatch)
{
    /*
     * Check if we match the whole string.  If so, return 1 
     */

    if (quick_wild(fargs[1], fargs[0]))
        safe_str("1", buff, bufc);
    else
        safe_str("0", buff, bufc);
    return;
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

    varargs_preamble("EXTRACT", 4);

    s = fargs[0];
    start = Tiny_atol(fargs[1]);
    len = Tiny_atol(fargs[2]);

    if ((start < 1) || (len < 1)) {
        return;
    }
    /*
     * Skip to the start of the string to save 
     */

    start--;
    s = trim_space_sep(s, sep);
    while (start && s) {
        s = next_token(s, sep);
        start--;
    }

    /*
     * If we ran of the end of the string, return nothing 
     */

    if (!s || !*s) {
        return;
    }
    /*
     * Count off the words in the string to save 
     */

    r = s;
    len--;
    while (len && s) {
        s = next_token(s, sep);
        len--;
    }

    /*
     * Chop off the rest of the string, if needed 
     */

    if (s && *s)
        t = split_token(&s, sep);
    safe_str(r, buff, bufc);
}

int xlate(char *arg)
{
    int temp;
    char *temp2;

    if (arg[0] == '#') {
        arg++;
        if (is_integer(arg)) {
            temp = Tiny_atol(arg);
            if (temp == -1)
                temp = 0;
            return temp;
        }
        return 0;
    }
    temp2 = trim_space_sep(arg, ' ');
    if (!*temp2)
        return 0;
    if (is_integer(temp2))
        return Tiny_atol(temp2);
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
        return;
    if (c == '\0')
        c = ' ';

    /*
     * move s to point to the start of the item we want 
     */

    start--;
    while (start && s && *s) {
        if ((s = (char *)strchr(s, c)) != NULL)
            s++;
        start--;
    }

    /*
     * skip over just spaces 
     */

    while (s && (*s == ' '))
        s++;
    if (!s || !*s)
        return;

    /*
     * figure out where to end the string 
     */

    p = s;
    while (end && p && *p) {
        if ((p = (char *)strchr(p, c)) != NULL) {
            if (--end == 0) {
                do {
                    p--;
                } while ((*p == ' ') && (p > s));
                *(++p) = '\0';
                safe_str(s, buff, bufc);
                return;
            } else {
                p++;
            }
        }
    }

    /*
     * if we've gotten this far, we've run off the end of the string 
     */

    safe_str(s, buff, bufc);
}


FUNCTION(fun_cat)
{
    int i;

    safe_str(fargs[0], buff, bufc);
    for (i = 1; i < nfargs; i++) {
        safe_chr(' ', buff, bufc);
        safe_str(fargs[i], buff, bufc);
    }
}

FUNCTION(fun_version)
{
    safe_str(mudstate.version, buff, bufc);
}
FUNCTION(fun_strlen)
{
    safe_tprintf_str(buff, bufc, "%d", (int)strlen((char *)strip_ansi(fargs[0])));
}

FUNCTION(fun_num)
{
    safe_tprintf_str(buff, bufc, "#%d", match_thing(player, fargs[0]));
}

FUNCTION(fun_pmatch)
{
    dbref thing;

    if (*fargs[0] == '#') {
        safe_tprintf_str(buff, bufc, "#%d", match_thing(player, fargs[0]));
        return;
    }
    if (!((thing = lookup_player(player, fargs[0], 1)) == NOTHING)) {
        safe_tprintf_str(buff, bufc, "#%d", thing);
        return;
    } else
        safe_str("#-1 NO MATCH", buff, bufc);
}

FUNCTION(fun_gt)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) > safe_atof(fargs[1])));
}
FUNCTION(fun_gte)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) >= safe_atof(fargs[1])));
}
FUNCTION(fun_lt)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) < safe_atof(fargs[1])));
}
FUNCTION(fun_lte)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) <= safe_atof(fargs[1])));
}
FUNCTION(fun_eq)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) == safe_atof(fargs[1])));
}
FUNCTION(fun_neq)
{
    safe_tprintf_str(buff, bufc, "%d", (safe_atof(fargs[0]) != safe_atof(fargs[1])));
}

FUNCTION(fun_and)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    int val = TRUE;
    for (int i = 0; i < nfargs; i++)
    {
        val = val && Tiny_atol(fargs[i]);
    }
    safe_tprintf_str(buff, bufc, "%d", val);
}

FUNCTION(fun_or)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }

    int val = FALSE;
    for (int i = 0; i < nfargs; i++)
    {
        val = val || Tiny_atol(fargs[i]);
    }
    safe_tprintf_str(buff, bufc, "%d", val);
}

FUNCTION(fun_xor)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }

    int val = FALSE;
    for (int i = 0; i < nfargs; i++)
    {
        int tval = Tiny_atol(fargs[i]);
        val = (val && !tval) || (!val && tval);
    }
    safe_tprintf_str(buff, bufc, "%d", val);
}

FUNCTION(fun_not)
{
    safe_tprintf_str(buff, bufc, "%d", !xlate(fargs[0]));
}

FUNCTION(fun_sqrt)
{
    double val;

    val = safe_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str("0", buff, bufc);
    }
    else
    {
        fval(buff, bufc, sqrt(val));
    }
#else
    fval(buff, bufc, sqrt(val));
#endif
}

FUNCTION(fun_add)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    double sum = 0.0;
    for (int i = 0; i < nfargs; i++)
    {
        sum += safe_atof(fargs[i]);
    }
    fval(buff, bufc, sum);
}

FUNCTION(fun_sub)
{
    fval(buff, bufc, safe_atof(fargs[0]) - safe_atof(fargs[1]));
}

FUNCTION(fun_mul)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    double prod = 1.0;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= safe_atof(fargs[i]);
    }
    fval(buff, bufc, prod);
}

FUNCTION(fun_floor)
{
    double r = floor(safe_atof(fargs[0]));
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
    double r = ceil(safe_atof(fargs[0]));
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
    const char *fstr;
    char *oldp;
    
    oldp = *bufc;
    
    switch (Tiny_atol(fargs[1]))
    {
    case 1:
        fstr = "%.1f";
        break;
    case 2:
        fstr = "%.2f";
        break;
    case 3:
        fstr = "%.3f";
        break;
    case 4:
        fstr = "%.4f";
        break;
    case 5:
        fstr = "%.5f";
        break;
    case 6:
        fstr = "%.6f";
        break;
    default:
        fstr = "%.0f";
        break;
    }
    double r = safe_atof(fargs[0]);
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(r);
    if (TINY_FPGROUP(fpc) == TINY_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, (char *)fstr, r);
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
    double rArg = safe_atof(fargs[0]);
    double rIntegerPart;
    double rFractionalPart;

    rFractionalPart = modf(rArg, &rIntegerPart);
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = Tiny_fpclass(rFractionalPart);
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

FUNCTION(fun_div)
{
    INT64 bot, top;

    top = Tiny_atoi64(fargs[0]);
    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str("#-1 DIVIDE BY ZERO", buff, bufc);
    }
    else
    {
        safe_str(Tiny_i64toa_t(top/bot), buff, bufc);
    }
}

FUNCTION(fun_fdiv)
{
    double bot = safe_atof(fargs[1]);
    double top = safe_atof(fargs[0]);
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

    top = Tiny_atoi64(fargs[0]);
    bot = Tiny_atoi64(fargs[1]);
    if (bot == 0)
    {
        bot = 1;
    }
    safe_str(Tiny_i64toa_t(top%bot), buff, bufc);
}

FUNCTION(fun_pi)
{
    safe_str("3.141592654", buff, bufc);
}
FUNCTION(fun_e)
{
    safe_str("2.718281828", buff, bufc);
}

FUNCTION(fun_sin)
{
    fval(buff, bufc, sin(safe_atof(fargs[0])));
}
FUNCTION(fun_cos)
{
    fval(buff, bufc, cos(safe_atof(fargs[0])));
}
FUNCTION(fun_tan)
{
    fval(buff, bufc, tan(safe_atof(fargs[0])));
}

FUNCTION(fun_exp)
{
    fval(buff, bufc, exp(safe_atof(fargs[0])));
}

FUNCTION(fun_power)
{
    double val1, val2;

    val1 = safe_atof(fargs[0]);
    val2 = safe_atof(fargs[1]);
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

    val = safe_atof(fargs[0]);
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

    val = safe_atof(fargs[0]);
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
    double val;

    val = safe_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
    }
    else
    {
        fval(buff, bufc, asin(val));
    }
#else
    fval(buff, bufc, asin(val));
#endif
}

FUNCTION(fun_acos)
{
    double val;

    val = safe_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
    }
    else
    {
        fval(buff, bufc, acos(val));
    }
#else
    fval(buff, bufc, acos(val));
#endif
}

FUNCTION(fun_atan)
{
    fval(buff, bufc, atan(safe_atof(fargs[0])));
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
    safe_tprintf_str(buff, bufc, "%d", d);
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
    safe_tprintf_str(buff, bufc, "%d", d);
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
            fval_buf(vres[i], safe_atof(v1[i]) + safe_atof(v2[i]));
            v1[i] = (char *) vres[i];
        }
        arr2list(v1, n, buff, bufc, osep);
        break;

    case VSUB_F:

        for (i = 0; i < n; i++)
        {
            fval_buf(vres[i], safe_atof(v1[i]) - safe_atof(v2[i]));
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
            scalar = safe_atof(v1[0]);
            for (i = 0; i < m; i++)
            {
                fval_buf(vres[i], safe_atof(v2[i]) * scalar);
                v1[i] = (char *) vres[i];
            }
            n = m;
        }
        else if (m == 1)
        {
            scalar = safe_atof(v2[0]);
            for (i = 0; i < n; i++)
            {
                fval_buf(vres[i], safe_atof(v1[i]) * scalar);
                v1[i] = (char *) vres[i];
            }
        }
        else
        {
            // Vector elementwise product.
            //
            // Note this is a departure from TinyMUX 1.6, but an
            // imitation of the PennMUSH and MUSH 3.0 behavior:
            // the documentation in Penn claims it's a dot product,
            // but the actual behavior isn't. We implement dot
            // product separately!
            //
            for (i = 0; i < n; i++)
            {
                fval_buf(vres[i], safe_atof(v1[i]) * safe_atof(v2[i]));
                v1[i] = (char *) vres[i];
            }
        }
        arr2list(v1, n, buff, bufc, osep);
        break;

    case VDOT_F:

        scalar = 0.0;
        for (i = 0; i < n; i++)
        {
            scalar += safe_atof(v1[i]) * safe_atof(v2[i]);
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
                a[0][i] = safe_atof(v1[i]);
                a[1][i] = safe_atof(v2[i]);
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
    
    svarargs_preamble("VADD", 4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VADD_F);
}

FUNCTION(fun_vsub)
{
    char sep, osep;
    
    svarargs_preamble("VSUB", 4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VSUB_F);
}

FUNCTION(fun_vmul)
{
    char sep, osep;
    
    svarargs_preamble("VMUL", 4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VMUL_F);
}

FUNCTION(fun_vdot)
{
    // dot product: (a,b,c) . (d,e,f) = ad + be + cf
    //
    char sep, osep;
    
    svarargs_preamble("VDOT", 4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VDOT_F);
}

FUNCTION(fun_vcross)
{
    // cross product: (a,b,c) x (d,e,f) = (bf - ce, cd - af, ae - bd)
    //
    char sep, osep;
    
    svarargs_preamble("VCROSS", 4);
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VCROSS_F);
}

FUNCTION(fun_vmag)
{
    char *v1[LBUF_SIZE];
    int n, i;
    double tmp, res = 0.0;
    char sep;
    
    varargs_preamble("VMAG", 2);
    
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
        tmp = safe_atof(v1[i]);
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
    
    varargs_preamble("VUNIT", 2);
    
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
        tmp = safe_atof(v1[i]);
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
        fval_buf(vres[i], safe_atof(v1[i]) / sqrt(res));
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
        varargs_preamble("VDIM", 2);
        safe_str(Tiny_ltoa_t(countwords(fargs[0],sep)), buff, bufc);
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
    if (x > 0)
        safe_str("1", buff, bufc);
    else if (x < 0)
        safe_str("-1", buff, bufc);
    else
        safe_str("0", buff, bufc);
}

#ifdef WOD_REALMS
FUNCTION(fun_cansee)
{
    if (nfargs < 2 || nfargs > 3)
    {
        safe_str("#-1", buff, bufc);
        return;
    }

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
            if ((Realm_Do & REALM_DO_MASK) == REALM_DO_HIDDEN_FROM_YOU)
            {
                safe_str("0", buff, bufc);
            }
            else
            {
                if (Dark(lookee))
                {
                    safe_str("0", buff, bufc);
                }
                else
                {
                    safe_str("1", buff, bufc);
                }
            }
            return;
        }
    }
    safe_str("#-1", buff, bufc);
}
#endif

/*
 * ---------------------------------------------------------------------------
 * * fun_lcon: Return a list of contents.
 */

FUNCTION(fun_lcon)
{
    dbref thing, it;
    char *tbuf;
    int first = 1;

    it = match_thing(player, fargs[0]);
    if ((it != NOTHING) &&
        (Has_contents(it)) &&
        (Examinable(player, it) ||
         (Location(player) == it) ||
         (it == cause)))
    {
        tbuf = alloc_sbuf("fun_lcon");
        DOLIST(thing, Contents(it))
        {
#ifdef WOD_REALMS
            if (REALM_DO_HIDDEN_FROM_YOU != DoThingToThingVisibility(player, thing, ACTION_IS_STATIONARY))
            {
#endif
                if (!first)
                {
                    tbuf[0] = ' ';
                    tbuf[1] = '#';
                    Tiny_ltoa(thing, tbuf+2);
                }
                else
                {
                    tbuf[0] = '#';
                    Tiny_ltoa(thing, tbuf+1);
                    first = 0;
                }
                safe_str(tbuf, buff, bufc);
#ifdef WOD_REALMS
            }
#endif
        }
        free_sbuf(tbuf);
    }
    else
    {
        safe_str("#-1", buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lexits: Return a list of exits.
 */

FUNCTION(fun_lexits)
{
    dbref thing, it, parent;
    char *tbuf;
    int exam, lev, key;
    int first = 1;

    it = match_thing(player, fargs[0]);

    if (!Good_obj(it) || !Has_exits(it))
    {
        safe_str("#-1", buff, bufc);
        return;
    }
    exam = Examinable(player, it);
    if (!exam && (where_is(player) != it) && (it != cause)) {
        safe_str("#-1", buff, bufc);
        return;
    }
    tbuf = alloc_sbuf("fun_lexits");

    /*
     * Return info for all parent levels 
     */

    ITER_PARENTS(it, parent, lev) {

        /*
         * Look for exits at each level 
         */

        if (!Has_exits(parent))
            continue;
        key = 0;
        if (Examinable(player, parent))
            key |= VE_LOC_XAM;
        if (Dark(parent))
            key |= VE_LOC_DARK;
        if (Dark(it))
            key |= VE_BASE_DARK;
        DOLIST(thing, Exits(parent))
        {
            if (exit_visible(thing, player, key))
            {
                if (!first)
                    sprintf(tbuf, " #%d", thing);
                else
                {
                    sprintf(tbuf, "#%d", thing);
                    first = 0;
                }
                safe_str(tbuf, buff, bufc);
            }
        }
    }
    free_sbuf(tbuf);
    return;
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
        safe_str("#-1", buff, bufc);
    else if (Has_home(it))
        safe_tprintf_str(buff, bufc, "#%d", Home(it));
    else if (Has_dropto(it))
        safe_tprintf_str(buff, bufc, "#%d", Dropto(it));
    else if (isExit(it))
        safe_tprintf_str(buff, bufc, "#%d", where_is(it));
    else
        safe_str("#-1", buff, bufc);
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
        safe_str("#-1", buff, bufc);
    else
        safe_tprintf_str(buff, bufc, "%d", Pennies(it));
}

/*
 * ---------------------------------------------------------------------------
 * * fun_pos: Find a word in a string 
 */

FUNCTION(fun_pos)
{
    int i = 1;
    char *s, *t, *u;

    i = 1;
    s = fargs[1];
    while (*s) {
        u = s;
        t = fargs[0];
        while (*t && *t == *u)
            ++t, ++u;
        if (*t == '\0') {
            safe_tprintf_str(buff, bufc, "%d", i);
            return;
        }
        ++i, ++s;
    }
    safe_str("#-1", buff, bufc);
    return;
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
    char nullb;

    /*
     * If passed a null string return an empty string, except that we * * 
     * 
     * *  * * are allowed to append to a null string. 
     */

    if ((!str || !*str) && ((flag != IF_INSERT) || (el != 1))) {
        return;
    }
    /*
     * we can't fiddle with anything before the first position 
     */

    if (el < 1) {
        safe_str(str, buff, bufc);
        return;
    }
    /*
     * Split the list up into 'before', 'target', and 'after' chunks * *
     * * * pointed to by sptr, iptr, and eptr respectively. 
     */

    nullb = '\0';
    if (el == 1) {
        /*
         * No 'before' portion, just split off element 1 
         */

        sptr = NULL;
        if (!str || !*str) {
            eptr = NULL;
            iptr = NULL;
        } else {
            eptr = trim_space_sep(str, sep);
            iptr = split_token(&eptr, sep);
        }
    } else {
        /*
         * Break off 'before' portion 
         */

        sptr = eptr = trim_space_sep(str, sep);
        overrun = 1;
        for (ct = el; ct > 2 && eptr; eptr = next_token(eptr, sep), ct--) ;
        if (eptr) {
            overrun = 0;
            iptr = split_token(&eptr, sep);
        }
        /*
         * If we didn't make it to the target element, just return *
         * * * * the string.  Insert is allowed to continue if we are 
         * *  * *  * exactly at the end of the string, but replace
         * and * delete *  *  * * are not. 
         */

        if (!(eptr || ((flag == IF_INSERT) && !overrun))) {
            safe_str(str, buff, bufc);
            return;
        }
        /*
         * Split the 'target' word from the 'after' portion. 
         */

        if (eptr)
            iptr = split_token(&eptr, sep);
        else
            iptr = NULL;
    }

    switch (flag) {
    case IF_DELETE: /*
                 * deletion 
                 */
        if (sptr) {
            safe_str(sptr, buff, bufc);
            if (eptr)
                safe_chr(sep, buff, bufc);
        }
        if (eptr) {
            safe_str(eptr, buff, bufc);
        }
        break;
    case IF_REPLACE:    /*
                 * replacing 
                 */
        if (sptr) {
            safe_str(sptr, buff, bufc);
            safe_chr(sep, buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (eptr) {
            safe_chr(sep, buff, bufc);
            safe_str(eptr, buff, bufc);
        }
        break;
    case IF_INSERT: /*
                 * insertion 
                 */
        if (sptr) {
            safe_str(sptr, buff, bufc);
            safe_chr(sep, buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (iptr) {
            safe_chr(sep, buff, bufc);
            safe_str(iptr, buff, bufc);
        }
        if (eptr) {
            safe_chr(sep, buff, bufc);
            safe_str(eptr, buff, bufc);
        }
        break;
    }
}


FUNCTION(fun_ldelete)
{               /*
                 * delete a word at position X of a list 
                 */
    char sep;

    varargs_preamble("LDELETE", 3);
    do_itemfuns(buff, bufc, fargs[0], Tiny_atol(fargs[1]), NULL, sep, IF_DELETE);
}

FUNCTION(fun_replace)
{               /*
                 * replace a word at position X of a list 
                 */
    char sep;

    varargs_preamble("REPLACE", 4);
    do_itemfuns(buff, bufc, fargs[0], Tiny_atol(fargs[1]), fargs[2], sep, IF_REPLACE);
}

FUNCTION(fun_insert)
{               /*
                 * insert a word at position X of a list 
                 */
    char sep;

    varargs_preamble("INSERT", 4);
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

    varargs_preamble("REMOVE", 3);
    if (strchr(fargs[1], sep)) {
        safe_str("#-1 CAN ONLY DELETE ONE ELEMENT", buff, bufc);
        return;
    }
    s = fargs[0];
    word = fargs[1];

    /*
     * Walk through the string copying words until (if ever) we get to *
     * * * * one that matches the target word. 
     */

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

    varargs_preamble("MEMBER", 3);
    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do {
        r = split_token(&s, sep);
        if (!strcmp(fargs[1], r)) {
            safe_tprintf_str(buff, bufc, "%d", wcount);
            return;
        }
        wcount++;
    } while (s);
    safe_str("0", buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_secure, fun_escape: escape [, ], %, \, and the beginning of the string.
 */

FUNCTION(fun_secure)
{
    char *s;

    s = fargs[0];
    while (*s) {
        switch (*s) {
        case '%':
        case '$':
        case '\\':
        case '[':
        case ']':
        case '(':
        case ')':
        case '{':
        case '}':
        case ',':
        case ';':
            safe_chr(' ', buff, bufc);
            break;
        default:
            safe_chr(*s, buff, bufc);
        }
        s++;
    }
}

FUNCTION(fun_escape)
{
    char *s, *d;

    d = *bufc;
    s = fargs[0];
    while (*s) {
        switch (*s) {
        case '%':
        case '\\':
        case '[':
        case ']':
        case '{':
        case '}':
        case ';':
            safe_chr('\\', buff, bufc);
        default:
            if (*bufc == d)
                safe_chr('\\', buff, bufc);
            safe_chr(*s, buff, bufc);
        }
        s++;
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
    
    varargs_preamble("WORDPOS", 3);
    
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
        safe_tprintf_str(buff, bufc, "%d", i);
        return;
    }
    safe_str("#-1", buff, bufc);
    return;
}

FUNCTION(fun_type)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (!Good_obj(it)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    switch (Typeof(it)) {
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
    return;
}

FUNCTION(fun_hasflag)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (!Good_obj(it)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    if (mudconf.pub_flags || Examinable(player, it) || (it == cause)) {
        if (has_flag(player, it, fargs[1]))
            safe_str("1", buff, bufc);
        else
            safe_str("0", buff, bufc);
    } else {
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
    }
}

FUNCTION(fun_haspower)
{
    dbref it;

    it = match_thing(player, fargs[0]);
    if (!Good_obj(it)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    if (mudconf.pub_flags || Examinable(player, it) || (it == cause)) {
        if (has_power(player, it, fargs[1]))
            safe_str("1", buff, bufc);
        else
            safe_str("0", buff, bufc);
    } else {
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
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
            safe_copy_buf(s, nLen, buff, bufc, LBUF_SIZE-1);
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
        safe_copy_buf(s, iStart, buff, bufc, LBUF_SIZE-1);
    }
    if (iEnd < nLen)
    {
        safe_copy_buf(s + iEnd, nLen - iEnd, buff, bufc, LBUF_SIZE-1);
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

    /*
     * Parse lock supplier into obj + lock 
     */

    if (!get_obj_and_lock(player, fargs[0], &it, &attr, buff, bufc))
        return;

    /*
     * Get the victim and ensure we can do it 
     */

    victim = match_thing(player, fargs[1]);
    if (!Good_obj(victim)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
    } else if (!nearby_or_control(player, victim) &&
           !nearby_or_control(player, it)) {
        safe_str("#-1 TOO FAR AWAY", buff, bufc);
    } else {
        tbuf = atr_get(it, attr->number, &aowner, &aflags);
        if ((attr->number == A_LOCK) ||
            Read_attr(player, it, attr, aowner, aflags)) {
            pBoolExp = parse_boolexp(player, tbuf, 1);
            safe_tprintf_str(buff, bufc, "%d", eval_boolexp(victim, it, it, pBoolExp));
            free_boolexp(pBoolExp);
        } else {
            safe_str("0", buff, bufc);
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

/*
 * ---------------------------------------------------------------------------
 * * fun_nearby: Return whether or not obj1 is near obj2.
 */

FUNCTION(fun_nearby)
{
    dbref obj1, obj2;

    obj1 = match_thing(player, fargs[0]);
    obj2 = match_thing(player, fargs[1]);
    if (!(nearby_or_control(player, obj1) ||
          nearby_or_control(player, obj2)))
        safe_str("0", buff, bufc);
    else if (nearby(obj1, obj2))
        safe_str("1", buff, bufc);
    else
        safe_str("0", buff, bufc);
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
        safe_str("#-1 NO MATCH", buff, bufc);
    }
    else 
    {
        str = (char *)token;
        TinyExec(buff, bufc, 0, it, it, EV_EVAL, &str, (char **)NULL, 0);
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

void ANSI_TransformTextWithTable
(
    char *buff,
    char **bufc,
    char *pString,
    unsigned char xfrmTable[256])
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
    nString = safe_copy_buf(pString, nString, buff, bufc, LBUF_SIZE-1);

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
    int limit = Tiny_atol(fargs[0]);
    if (limit > 0)
    {
        safe_chr('0', buff, bufc);
        for (int ctr = 1; ctr < limit; ctr++)
        {
            char tbuff[10];
            sprintf(tbuff, " %d", ctr);
            char *p = *bufc;
            safe_str(tbuff, buff, bufc);

            // Was there room in the buffer?
            //
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

    /*
     * Check for wildcard matching.  parse_attrib_wild checks for read *
     * * * * permission, so we don't have to.  Have p_a_w assume the * *
     * slash-star * * if it is missing. 
     */

    first = 1;
    if (parse_attrib_wild(player, fargs[0], &thing, 0, 0, 1)) {
        for (ca = olist_first(); ca != NOTHING; ca = olist_next()) {
            attr = atr_num(ca);
            if (attr) {
                if (!first)
                    safe_chr(' ', buff, bufc);
                first = 0;
                safe_str((char *)attr->name, buff, bufc);
            }
        }
    } else {
        safe_str("#-1 NO MATCH", buff, bufc);
    }
    return;
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
    varargs_preamble("REVWORDS", 2);
    ReverseWordsInText_Seperator = sep;
    ANSI_TransformTextReverseWithFunction(buff, bufc, fargs[0], ReverseWordsInText);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_after, fun_before: Return substring after or before a specified string.
 */

FUNCTION(fun_after)
{
    char *cp, *mp;
    int mlen;

    if (nfargs == 0)
    {
        return;
    }
    if (!fn_range_check("AFTER", nfargs, 1, 2, buff, bufc))
    {
        return;
    }

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
    while (*bp)
    {
        // Search for the first character in the target string.
        //
        cp = (char *)strchr(bp, *mp);
        if (cp == NULL)
        {
            // Not found, return empty string.
            //
            return;
        }

        // See if what follows is what we are looking for.
        //
        if (!strncmp(cp, mp, mlen))
        {
            // Yup, return what follows.
            //
            bp = cp + mlen;
            safe_str(bp, buff, bufc);
            return;
        }

        // Continue search after found first character.
        //
        bp = cp + 1;
    }

    // Ran off the end without finding it.
    //
    return;
}

FUNCTION(fun_before)
{
    char *cp, *mp, *ip;
    int mlen;

    if (nfargs == 0)
    {
        return;
    }
    if (!fn_range_check("BEFORE", nfargs, 1, 2, buff, bufc))
        return;

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
    while (*bp)
    {
        // Search for the first character in the target string.
        //
        cp = (char *)strchr(bp, *mp);
        if (cp == NULL)
        {
            // Not found, return entire string.
            //
            safe_str(ip, buff, bufc);
            return;
        }

        // See if what follows is what we are looking for.
        //
        if (!strncmp(cp, mp, mlen))
        {
            // Yup, return what follows.
            //
            *cp = '\0';
            safe_str(ip, buff, bufc);
            return;
        }

        // Continue search after found first character.
        //
        bp = cp + 1;
    }

    // Ran off the end without finding it.
    //
    safe_str(ip, buff, bufc);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_max, fun_min: Return maximum (minimum) value.
 */

FUNCTION(fun_max)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }

    double maximum = DBL_MIN;
    for (int i = 0; i < nfargs; i++)
    {
        double tval = safe_atof(fargs[i]);
        if (tval > maximum)
        {
            maximum = tval;
        }
    }
    fval(buff, bufc, maximum);
}

FUNCTION(fun_min)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }

    double minimum = DBL_MAX;
    for (int i = 0; i < nfargs; i++) 
    {
        double tval = safe_atof(fargs[i]);
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
    dbref thing;
    char *bp, *nbuf;
    SEARCH searchparm;

    /*
     * Set up for the search.  If any errors, abort. 
     */

    if (!search_setup(player, fargs[0], &searchparm)) {
        safe_str("#-1 ERROR DURING SEARCH", buff, bufc);
        return;
    }
    /*
     * Do the search and report the results 
     */

    search_perform(player, cause, &searchparm);
    bp = *bufc;
    nbuf = alloc_sbuf("fun_search");
    for (thing = olist_first(); thing != NOTHING; thing = olist_next())
    {
        if (bp == *bufc)
        {
            nbuf[0] = '#';
            Tiny_ltoa(thing, nbuf+1);
        }
        else
        {
            nbuf[0] = ' ';
            nbuf[1] = '#';
            Tiny_ltoa(thing, nbuf+2);
        }
        safe_str(nbuf, buff, bufc);
    }
    free_sbuf(nbuf);
    olist_init();
}

/*
 * ---------------------------------------------------------------------------
 * * fun_stats: Get database size statistics.
 */

FUNCTION(fun_stats)
{
    dbref who;
    STATS statinfo;

    if ((!fargs[0]) || !*fargs[0] || !string_compare(fargs[0], "all")) {
        who = NOTHING;
    } else {
        who = lookup_player(player, fargs[0], 1);
        if (who == NOTHING) {
            safe_str("#-1 NOT FOUND", buff, bufc);
            return;
        }
    }
    if (!get_stats(player, who, &statinfo)) {
        safe_str("#-1 ERROR GETTING STATS", buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, "%d %d %d %d %d %d", statinfo.s_total, statinfo.s_rooms,
            statinfo.s_exits, statinfo.s_things, statinfo.s_players,
             statinfo.s_garbage);
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

    varargs_preamble("SPLICE", 4);

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
            int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
            if (times > nBufferAvailable)
            {
                times = nBufferAvailable;
            }

            // Fill with repeat character.
            //
            memset(*bufc, *fargs[0], times);
            *bufc += times;
        }
        else
        {
            int nSize = len*times;
            if (  times >= LBUF_SIZE - 1
               || nSize >= LBUF_SIZE - 1)
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
 * * fun_iter: Make list from evaluating arg2 with each member of arg1.
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_iter)
{
    char *curr, *objstring, *buff2, *buff3, *cp, *dp, sep,
    *str;
    int first, number = 0;

    evarargs_preamble("ITER", 3);
    dp = cp = curr = alloc_lbuf("fun_iter");
    str = fargs[0];
    TinyExec(curr, &dp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *dp = '\0';
    cp = trim_space_sep(cp, sep);
    if (!*cp) {
        free_lbuf(curr);
        return;
    }
    first = 1;
    while (cp)
    {
        if (!first)
            safe_chr(' ', buff, bufc);
        first = 0;
        number++;
        objstring = split_token(&cp, sep);
        buff2 = replace_string(BOUND_VAR, objstring, fargs[1]);
        buff3 = replace_string(LISTPLACE_VAR, Tiny_ltoa_t(number), buff2);
        str = buff3;
        TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buff2);
        free_lbuf(buff3);
    }
    free_lbuf(curr);
}

FUNCTION(fun_list)
{
    char *curr, *objstring, *buff2, *buff3, *result, *cp, *dp, *str,
     sep;
    int number = 0;

    evarargs_preamble("LIST", 3);
    cp = curr = dp = alloc_lbuf("fun_list");
    str = fargs[0];
    TinyExec(curr, &dp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    cp = trim_space_sep(cp, sep);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    while (cp)
    {
        number++;
        objstring = split_token(&cp, sep);
        buff2 = replace_string(BOUND_VAR, objstring, fargs[1]);
        buff3 = replace_string(LISTPLACE_VAR, Tiny_ltoa_t(number), buff2);
        dp = result = alloc_lbuf("fun_list.2");
        str = buff3;
        TinyExec(result, &dp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *dp = '\0';
        free_lbuf(buff2);
        free_lbuf(buff3);
        notify(cause, result);
        free_lbuf(result);
    }
    free_lbuf(curr);
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
 * *    You say "->122333444455555"
 * *
 * *      NOTE: To use added list separator, you must use base case!
 */

FUNCTION(fun_fold)
{
    dbref aowner, thing;
    int aflags, anum;
    ATTR *ap;
    char *atext, *result, *curr, *bp, *str, *cp, *atextbuf, *clist[2],
    *rstore, sep;

    /*
     * We need two to four arguements only 
     */

    mvarargs_preamble("FOLD", 2, 4);

    /*
     * Two possibilities for the first arg: <obj>/<attr> and <attr>. 
     */

    if (parse_attrib(player, fargs[0], &thing, &anum)) {
        if ((anum == NOTHING) || (!Good_obj(thing)))
            ap = NULL;
        else
            ap = atr_num(anum);
    } else {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    /*
     * Make sure we got a good attribute 
     */

    if (!ap) {
        return;
    }
    /*
     * Use it if we can access it, otherwise return an error. 
     */

    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext) {
        return;
    } else if (!*atext || !See_attr(player, thing, ap, aowner, aflags)) {
        free_lbuf(atext);
        return;
    }
    /*
     * Evaluate it using the rest of the passed function args 
     */

    cp = curr = fargs[1];
    atextbuf = alloc_lbuf("fun_fold");
    StringCopy(atextbuf, atext);

    /*
     * may as well handle first case now 
     */

    if ((nfargs >= 3) && (fargs[2])) {
        clist[0] = fargs[2];
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    } else {
        clist[0] = split_token(&cp, sep);
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    }

    rstore = result;
    result = NULL;

    while (cp) {
        clist[0] = rstore;
        clist[1] = split_token(&cp, sep);
        StringCopy(atextbuf, atext);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        TinyExec(result, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
        StringCopy(rstore, result);
        free_lbuf(result);
    }
    safe_str(rstore, buff, bufc);
    free_lbuf(rstore);
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_filter: iteratively perform a function with a list of arguments
 * *              and return the arg, if the function evaluates to TRUE using the 
 * *      arg.
 * *
 * *      > &IS_ODD object=mod(%0,2)
 * *      > say filter(object/is_odd,1 2 3 4 5)
 * *      You say "1 3 5"
 * *      > say filter(object/is_odd,1-2-3-4-5,-)
 * *      You say "1-3-5"
 * *
 * *  NOTE:  If you specify a separator it is used to delimit returned list
 */

FUNCTION(fun_filter)
{
    dbref aowner, thing;
    int aflags, anum, first;
    ATTR *ap;
    char *atext, *result, *curr, *objstring, *bp, *str, *cp, *atextbuf,
     sep;

    varargs_preamble("FILTER", 3);

    /*
     * Two possibilities for the first arg: <obj>/<attr> and <attr>. 
     */

    if (parse_attrib(player, fargs[0], &thing, &anum)) {
        if ((anum == NOTHING) || (!Good_obj(thing)))
            ap = NULL;
        else
            ap = atr_num(anum);
    } else {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    /*
     * Make sure we got a good attribute 
     */

    if (!ap) {
        return;
    }
    /*
     * Use it if we can access it, otherwise return an error. 
     */

    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext) {
        return;
    } else if (!*atext || !See_attr(player, thing, ap, aowner, aflags)) {
        free_lbuf(atext);
        return;
    }
    /*
     * Now iteratively eval the attrib with the argument list 
     */

    cp = curr = trim_space_sep(fargs[1], sep);
    atextbuf = alloc_lbuf("fun_filter");
    first = 1;
    while (cp) {
        objstring = split_token(&cp, sep);
        StringCopy(atextbuf, atext);
        result = bp = alloc_lbuf("fun_filter");
        str = atextbuf;
        TinyExec(result, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
        *bp = '\0';
        if (!first && *result == '1')
            safe_chr(sep, buff, bufc);
        if (*result == '1') {
            safe_str(objstring, buff, bufc);
            first = 0;
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
 * *  You say "0.5 1 1.5 2 2.5"
 * *  > say map(object/div_two,1-2-3-4-5,-)
 * *  You say "0.5-1-1.5-2-2.5"
 * *
 */

FUNCTION(fun_map)
{
    dbref aowner, thing;
    int aflags, anum, first;
    ATTR *ap;
    char *atext, *objstring, *str, *cp, *atextbuf, sep;

    varargs_preamble("MAP", 3);

    /*
     * Two possibilities for the second arg: <obj>/<attr> and <attr>. 
     */

    if (parse_attrib(player, fargs[0], &thing, &anum)) {
        if ((anum == NOTHING) || (!Good_obj(thing)))
            ap = NULL;
        else
            ap = atr_num(anum);
    } else {
        thing = player;
        ap = atr_str(fargs[0]);
    }

    /*
     * Make sure we got a good attribute 
     */

    if (!ap) {
        return;
    }
    /*
     * Use it if we can access it, otherwise return an error. 
     */

    atext = atr_pget(thing, ap->number, &aowner, &aflags);
    if (!atext) {
        return;
    } else if (!*atext || !See_attr(player, thing, ap, aowner, aflags)) {
        free_lbuf(atext);
        return;
    }
    /*
     * now process the list one element at a time 
     */

    cp = trim_space_sep(fargs[1], sep);
    atextbuf = alloc_lbuf("fun_map");
    first = 1;
    while (cp) {
        if (!first)
            safe_chr(sep, buff, bufc);
        first = 0;
        objstring = split_token(&cp, sep);
        StringCopy(atextbuf, atext);
        str = atextbuf;
        TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
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

    edit_string((char *)strip_ansi(fargs[0]), &tstr, fargs[1], fargs[2]);
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
    if (!Good_obj(thing)) {
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
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
    char *mbuff, *tbuff, *buf, *bp, *str;

    /*
     * If we don't have at least 2 args, return nothing 
     */

    if (nfargs < 2) {
        return;
    }
    /*
     * Evaluate the target in fargs[0] 
     */

    mbuff = bp = alloc_lbuf("fun_switch");
    str = fargs[0];
    TinyExec(mbuff, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    /*
     * Loop through the patterns looking for a match 
     */

    for (i = 1; (i < nfargs - 1) && fargs[i] && fargs[i + 1]; i += 2) {
        tbuff = bp = alloc_lbuf("fun_switch.2");
        str = fargs[i];
        TinyExec(tbuff, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        if (quick_wild(tbuff, mbuff)) {
            free_lbuf(tbuff);
            buf = alloc_lbuf("fun_switch");
            StringCopy(buf, fargs[i + 1]);
            str = buf;
            TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
            free_lbuf(buf);
            free_lbuf(mbuff);
            return;
        }
        free_lbuf(tbuff);
    }
    free_lbuf(mbuff);

    /*
     * Nope, return the default if there is one 
     */

    if ((i < nfargs) && fargs[i]) {
        buf = alloc_lbuf("fun_switch");
        StringCopy(buf, fargs[i]);
        str = buf;
        TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buf);
    }
    return;
}

FUNCTION(fun_case)
{
    int i;
    char *mbuff, *buf, *bp, *str;

    /*
     * If we don't have at least 2 args, return nothing 
     */

    if (nfargs < 2) {
        return;
    }
    /*
     * Evaluate the target in fargs[0] 
     */

    mbuff = bp = alloc_lbuf("fun_switch");
    str = fargs[0];
    TinyExec(mbuff, &bp, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    /*
     * Loop through the patterns looking for a match 
     */

    for (i = 1; (i < nfargs - 1) && fargs[i] && fargs[i + 1]; i += 2) {
        if (*fargs[i] == *mbuff) {
            buf = alloc_lbuf("fun_switch");
            StringCopy(buf, fargs[i + 1]);
            str = buf;
            TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
            free_lbuf(buf);
            free_lbuf(mbuff);
            return;
        }
    }
    free_lbuf(mbuff);

    /*
     * Nope, return the default if there is one 
     */

    if ((i < nfargs) && fargs[i]) {
        buf = alloc_lbuf("fun_switch");
        StringCopy(buf, fargs[i]);
        str = buf;
        TinyExec(buff, bufc, 0, player, cause, EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buf);
    }
    return;
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
    if (*fargs[0] == '\0')
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
            if (!is_integer(fargs[0]))
            {
                num = 1;
            }
        }
        else if (num < 0)
        {
            num = 0;
        }

    }

    // Check for buffer limits.
    //
    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    if (num > nBufferAvailable)
    {
        num = nBufferAvailable;
    }

    // Fill with spaces.
    //
    memset(*bufc, ' ', num);
    *bufc += num;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_idle, fun_conn: return seconds idle or connected.
 */

FUNCTION(fun_idle)
{
    dbref target;

    target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target) && Dark(target) && !Wizard(player))
        target = NOTHING;
    safe_tprintf_str(buff, bufc, "%d", fetch_idle(target));
}

FUNCTION(fun_conn)
{
    dbref target;

    target = lookup_player(player, fargs[0], 1);
    if (Good_obj(target) && Dark(target) && !Wizard(player))
        target = NOTHING;
    safe_tprintf_str(buff, bufc, "%d", fetch_connect(target));
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
    return 1;
    if (((f_rec *) s1)->data < ((f_rec *) s2)->data)
    return -1;
    return 0;
}

static int DCL_CDECL i_comp(const void *s1, const void *s2)
{
    if (((i_rec *) s1)->data > ((i_rec *) s2)->data)
    return 1;
    if (((i_rec *) s1)->data < ((i_rec *) s2)->data)
    return -1;
    return 0;
}

static void do_asort(char *s[], int n, int sort_type)
{
    int i;
    f_rec *fp;
    i_rec *ip;

    switch (sort_type) {
    case ALPHANUM_LIST:
        qsort((void *)s, n, sizeof(char *), a_comp);

        break;
    case NUMERIC_LIST:
        ip = (i_rec *) MEMALLOC(n * sizeof(i_rec), __FILE__, __LINE__);
        for (i = 0; i < n; i++) {
            ip[i].str = s[i];
            ip[i].data = Tiny_atol(s[i]);
        }
        qsort((void *)ip, n, sizeof(i_rec), i_comp);
        for (i = 0; i < n; i++) {
            s[i] = ip[i].str;
        }
        MEMFREE(ip, __FILE__, __LINE__);
        break;
    case DBREF_LIST:
        ip = (i_rec *) MEMALLOC(n * sizeof(i_rec), __FILE__, __LINE__);
        for (i = 0; i < n; i++) {
            ip[i].str = s[i];
            ip[i].data = dbnum(s[i]);
        }
        qsort((void *)ip, n, sizeof(i_rec), i_comp);
        for (i = 0; i < n; i++) {
            s[i] = ip[i].str;
        }
        MEMFREE(ip, __FILE__, __LINE__);
        break;
    case FLOAT_LIST:
        fp = (f_rec *) MEMALLOC(n * sizeof(f_rec), __FILE__, __LINE__);
        for (i = 0; i < n; i++) {
            fp[i].str = s[i];
            fp[i].data = safe_atof(s[i]);
        }
        qsort((void *)fp, n, sizeof(f_rec), f_comp);
        for (i = 0; i < n; i++) {
            s[i] = fp[i].str;
        }
        MEMFREE(fp, __FILE__, __LINE__);
        break;
    }
}

FUNCTION(fun_sort)
{
    int nitems, sort_type;
    char *list, sep;
    char *ptrs[LBUF_SIZE / 2];

    /*
     * If we are passed an empty arglist return a null string 
     */

    if (nfargs == 0) {
        return;
    }
    mvarargs_preamble("SORT", 1, 3);

    /*
     * Convert the list to an array 
     */

    list = alloc_lbuf("fun_sort");
    StringCopy(list, fargs[0]);
    nitems = list2arr(ptrs, LBUF_SIZE / 2, list, sep);
    sort_type = get_list_type(fargs, nfargs, 2, ptrs, nitems);
    do_asort(ptrs, nitems, sort_type);
    arr2list(ptrs, nitems, buff, bufc, sep);
    free_lbuf(list);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_setunion, fun_setdiff, fun_setinter: Set management.
 */

#define SET_UNION   1
#define SET_INTERSECT   2
#define SET_DIFF    3

static void handle_sets(char *fargs[], char *buff, char **bufc, int oper, char sep)
{
    char *list1, *list2, *oldp;
    char *ptrs1[LBUF_SIZE], *ptrs2[LBUF_SIZE];
    int i1, i2, n1, n2, val, first;

    list1 = alloc_lbuf("fun_setunion.1");
    StringCopy(list1, fargs[0]);
    n1 = list2arr(ptrs1, LBUF_SIZE, list1, sep);
    do_asort(ptrs1, n1, ALPHANUM_LIST);

    list2 = alloc_lbuf("fun_setunion.2");
    StringCopy(list2, fargs[1]);
    n2 = list2arr(ptrs2, LBUF_SIZE, list2, sep);
    do_asort(ptrs2, n2, ALPHANUM_LIST);

    i1 = i2 = 0;
    first = 1;
    oldp = *bufc;
    **bufc = '\0';

    switch (oper) {
    case SET_UNION: /*
                 * Copy elements common to both lists 
                 */

        /*
         * Handle case of two identical single-element lists 
         */

        if ((n1 == 1) && (n2 == 1) &&
            (!strcmp(ptrs1[0], ptrs2[0]))) {
            safe_str(ptrs1[0], buff, bufc);
            break;
        }
        /*
         * Process until one list is empty 
         */

        while ((i1 < n1) && (i2 < n2)) {

            /*
             * Skip over duplicates 
             */

            if ((i1 > 0) || (i2 > 0)) {
                while ((i1 < n1) && !strcmp(ptrs1[i1],
                                oldp))
                    i1++;
                while ((i2 < n2) && !strcmp(ptrs2[i2],
                                oldp))
                    i2++;
            }
            /*
             * Compare and copy 
             */

            if ((i1 < n1) && (i2 < n2)) {
                if (!first)
                    safe_chr(sep, buff, bufc);
                first = 0;
                oldp = *bufc;
                if (strcmp(ptrs1[i1], ptrs2[i2]) < 0) {
                    safe_str(ptrs1[i1], buff, bufc);
                    i1++;
                } else {
                    safe_str(ptrs2[i2], buff, bufc);
                    i2++;
                }
                **bufc = '\0';
            }
        }

        /*
         * Copy rest of remaining list, stripping duplicates 
         */

        for (; i1 < n1; i1++) {
            if (strcmp(oldp, ptrs1[i1])) {
                if (!first)
                    safe_chr(sep, buff, bufc);
                first = 0;
                oldp = *bufc;
                safe_str(ptrs1[i1], buff, bufc);
                **bufc = '\0';
            }
        }
        for (; i2 < n2; i2++) {
            if (strcmp(oldp, ptrs2[i2])) {
                if (!first)
                    safe_chr(sep, buff, bufc);
                first = 0;
                oldp = *bufc;
                safe_str(ptrs2[i2], buff, bufc);
                **bufc = '\0';
            }
        }
        break;
    case SET_INTERSECT: /*
                 * Copy elements not in both lists 
                 */

        while ((i1 < n1) && (i2 < n2)) {
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val) {

                /*
                 * Got a match, copy it 
                 */

                if (!first)
                    safe_chr(sep, buff, bufc);
                first = 0;
                oldp = *bufc;
                safe_str(ptrs1[i1], buff, bufc);
                i1++;
                i2++;
                while ((i1 < n1) && !strcmp(ptrs1[i1], oldp))
                    i1++;
                while ((i2 < n2) && !strcmp(ptrs2[i2], oldp))
                    i2++;
            } else if (val < 0) {
                i1++;
            } else {
                i2++;
            }
        }
        break;
    case SET_DIFF:      /*
                 * Copy elements unique to list1 
                 */

        while ((i1 < n1) && (i2 < n2)) {
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val) {

                /*
                 * Got a match, increment pointers 
                 */

                oldp = ptrs1[i1];
                while ((i1 < n1) && !strcmp(ptrs1[i1], oldp))
                    i1++;
                while ((i2 < n2) && !strcmp(ptrs2[i2], oldp))
                    i2++;
            } else if (val < 0) {

                /*
                 * Item in list1 not in list2, copy 
                 */

                if (!first)
                    safe_chr(sep, buff, bufc);
                first = 0;
                safe_str(ptrs1[i1], buff, bufc);
                oldp = ptrs1[i1];
                i1++;
                while ((i1 < n1) && !strcmp(ptrs1[i1], oldp))
                    i1++;
            } else {

                /*
                 * Item in list2 but not in list1, discard 
                 */

                oldp = ptrs2[i2];
                i2++;
                while ((i2 < n2) && !strcmp(ptrs2[i2], oldp))
                    i2++;
            }
        }

        /*
         * Copy remainder of list1 
         */

        while (i1 < n1) {
            if (!first)
                safe_chr(sep, buff, bufc);
            first = 0;
            safe_str(ptrs1[i1], buff, bufc);
            oldp = ptrs1[i1];
            i1++;
            while ((i1 < n1) && !strcmp(ptrs1[i1], oldp))
                i1++;
        }
    }
    free_lbuf(list1);
    free_lbuf(list2);
    return;
}

FUNCTION(fun_setunion)
{
    char sep;

    varargs_preamble("SETUNION", 3);
    handle_sets(fargs, buff, bufc, SET_UNION, sep);
    return;
}

FUNCTION(fun_setdiff)
{
    char sep;

    varargs_preamble("SETDIFF", 3);
    handle_sets(fargs, buff, bufc, SET_DIFF, sep);
    return;
}

FUNCTION(fun_setinter)
{
    char sep;

    varargs_preamble("SETINTER", 3);
    handle_sets(fargs, buff, bufc, SET_INTERSECT, sep);
    return;
}

/*
 * ---------------------------------------------------------------------------
 * * rjust, ljust, center: Justify or center text, specifying fill character
 */

FUNCTION(fun_ljust)
{
    int spaces, i;
    char sep;

    varargs_preamble("LJUST", 3);
    spaces = Tiny_atol(fargs[1]) - strlen((char *)strip_ansi(fargs[0]));

    /*
     * Sanitize number of spaces 
     */

    if (spaces <= 0) {
        /*
         * no padding needed, just return string 
         */
        safe_str(fargs[0], buff, bufc);
        return;
    } else if (spaces > LBUF_SIZE) {
        spaces = LBUF_SIZE;
    }
    safe_str(fargs[0], buff, bufc);
    for (i = 0; i < spaces; i++)
        safe_chr(sep, buff, bufc);
}

FUNCTION(fun_rjust)
{
    int spaces, i;
    char sep;

    varargs_preamble("RJUST", 3);
    spaces = Tiny_atol(fargs[1]) - strlen((char *)strip_ansi(fargs[0]));

    /*
     * Sanitize number of spaces 
     */

    if (spaces <= 0) {
        /*
         * no padding needed, just return string 
         */
        safe_str(fargs[0], buff, bufc);
        return;
    } else if (spaces > LBUF_SIZE) {
        spaces = LBUF_SIZE;
    }
    for (i = 0; i < spaces; i++)
        safe_chr(sep, buff, bufc);
    safe_str(fargs[0], buff, bufc);
}

FUNCTION(fun_center)
{
    char sep;
    int i, len, lead_chrs, trail_chrs, width;

    varargs_preamble("CENTER", 3);

    width = Tiny_atol(fargs[1]);
    len = strlen((char *)strip_ansi(fargs[0]));

    if (width > LBUF_SIZE)
    {
        safe_str("#-1 OUT OF RANGE", buff, bufc);
        return;
    }
    
    if (len >= width)
    {
        safe_str(fargs[0], buff, bufc);
        return;
    }
    
    // The text is padded with the specified fill character before and
    // after. The length of the suffix padding is always equal to or one
    // greater than the length of the prefix padding.
    //
    lead_chrs = (width - len)/2;
    for (i = 0; i < lead_chrs; i++)
    {
        safe_chr(sep, buff, bufc);
    }

    safe_str(fargs[0], buff, bufc);

    trail_chrs = width - lead_chrs - len;
    for (i = 0; i < trail_chrs; i++)
    {
        safe_chr(sep, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * setq, setr, r: set and read global registers.
 */

FUNCTION(fun_setq)
{
    int regnum;

    regnum = Tiny_atol(fargs[0]);
    if ((regnum < 0) || (regnum >= MAX_GLOBAL_REGS)) {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    } else {
        if (!mudstate.global_regs[regnum])
            mudstate.global_regs[regnum] =
                alloc_lbuf("fun_setq");
        StringCopy(mudstate.global_regs[regnum], fargs[1]);
    }
}

FUNCTION(fun_setr)
{
    int regnum;

    regnum = Tiny_atol(fargs[0]);
    if ((regnum < 0) || (regnum >= MAX_GLOBAL_REGS)) {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
        return;
    } else {
        if (!mudstate.global_regs[regnum])
            mudstate.global_regs[regnum] =
                alloc_lbuf("fun_setq");
        StringCopy(mudstate.global_regs[regnum], fargs[1]);
    }
    safe_str(fargs[1], buff, bufc);
}

FUNCTION(fun_r)
{
    int regnum;

    regnum = Tiny_atol(fargs[0]);
    if ((regnum < 0) || (regnum >= MAX_GLOBAL_REGS)) {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    } else if (mudstate.global_regs[regnum]) {
        safe_str(mudstate.global_regs[regnum], buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * isnum: is the argument a number?
 */

FUNCTION(fun_isnum)
{
    safe_str((is_number(fargs[0]) ? "1" : "0"), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * isdbref: is the argument a valid dbref?
 */

FUNCTION(fun_isdbref)
{
    char *p;
    dbref dbitem;

    p = fargs[0];
    if (*p++ == NUMBER_TOKEN) {
        dbitem = parse_dbref(p);
        if (Good_obj(dbitem)) {
            safe_str("1", buff, bufc);
            return;
        }
    }
    safe_str("0", buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * trim: trim off unwanted white space.
 */

FUNCTION(fun_trim)
{
    char *p, *lastchar, *q, sep;
    int trim;

    if (nfargs == 0) {
        return;
    }
    mvarargs_preamble("TRIM", 1, 3);
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

/* ---------------------------------------------------------------------------
 * flist: List of existing functions in alphabetical order.
 */

FUN flist[] =
{
    {"ABS",      fun_abs,      1,  0,          CA_PUBLIC},
    {"ACOS",     fun_acos,     1,  0,          CA_PUBLIC},
    {"ADD",      fun_add,      0,  FN_VARARGS, CA_PUBLIC},
    {"AFTER",    fun_after,    0,  FN_VARARGS, CA_PUBLIC},
    {"ALPHAMAX", fun_alphamax, 0,  FN_VARARGS, CA_PUBLIC},
    {"ALPHAMIN", fun_alphamin, 0,  FN_VARARGS, CA_PUBLIC},
    {"AND",      fun_and,      0,  FN_VARARGS, CA_PUBLIC},
    {"ANDFLAGS", fun_andflags, 2,  0,          CA_PUBLIC},
    {"ANSI",     fun_ansi,     2,  0,          CA_PUBLIC},
    {"APOSS",    fun_aposs,    1,  0,          CA_PUBLIC},
    {"ART",      fun_art,      1,  0,          CA_PUBLIC},
    {"ASIN",     fun_asin,     1,  0,          CA_PUBLIC},
    {"ATAN",     fun_atan,     1,  0,          CA_PUBLIC},
    {"BAND",     fun_band,     2,  0,          CA_PUBLIC},
    {"BEEP",     fun_beep,     0,  0,          CA_WIZARD},
    {"BEFORE",   fun_before,   0,  FN_VARARGS, CA_PUBLIC},
    {"BNAND",    fun_bnand,    2,  0,          CA_PUBLIC},
    {"BOR",      fun_bor,      2,  0,          CA_PUBLIC},
#ifdef WOD_REALMS
    {"CANSEE",   fun_cansee,   0,  FN_VARARGS, CA_PUBLIC},
#endif
    {"CAPSTR",   fun_capstr,  -1,  0,          CA_PUBLIC},
    {"CAT",      fun_cat,      0,  FN_VARARGS, CA_PUBLIC},
    {"CEIL",     fun_ceil,     1,  0,          CA_PUBLIC},
    {"CENTER",   fun_center,   0,  FN_VARARGS, CA_PUBLIC},
    {"CHANNELS", fun_channels, 0,  0,          CA_PUBLIC},
    {"CHILDREN", fun_children, 1,  0,          CA_PUBLIC},
    {"COLUMNS",  fun_columns,  0,  FN_VARARGS, CA_PUBLIC},
    {"COMALIAS", fun_comalias, 2,  0,          CA_PUBLIC},
    {"COMP",     fun_comp,     2,  0,          CA_PUBLIC},
    {"COMTITLE", fun_comtitle, 2,  0,          CA_PUBLIC},
    {"CON",      fun_con,      1,  0,          CA_PUBLIC},
    {"CONN",     fun_conn,     1,  0,          CA_PUBLIC},
    {"CONTROLS", fun_controls, 2,  0,          CA_PUBLIC},
    {"CONVSECS", fun_convsecs, 1,  0,          CA_PUBLIC},
    {"CONVTIME", fun_convtime, 1,  0,          CA_PUBLIC},
    {"COS",      fun_cos,      1,  0,          CA_PUBLIC},
    {"CREATE",   fun_create,   0,  FN_VARARGS, CA_PUBLIC},
    {"CWHO",     fun_cwho,     1,  0,          CA_PUBLIC},
    {"DEC",      fun_dec,      1,  0,          CA_PUBLIC},
    {"DECRYPT",  fun_decrypt,  2,  0,          CA_PUBLIC},
    {"DEFAULT",  fun_default,  2,  FN_NO_EVAL, CA_PUBLIC},
    {"DELETE",   fun_delete,   3,  0,          CA_PUBLIC},
    {"DIE",      fun_die,      2,  0,          CA_PUBLIC},
    {"DIST2D",   fun_dist2d,   4,  0,          CA_PUBLIC},
    {"DIST3D",   fun_dist3d,   6,  0,          CA_PUBLIC},
    {"DIV",      fun_div,      2,  0,          CA_PUBLIC},
    {"DOING",    fun_doing,    1,  0,          CA_PUBLIC},
    {"E",        fun_e,        0,  0,          CA_PUBLIC},
    {"EDEFAULT", fun_edefault, 2,  FN_NO_EVAL, CA_PUBLIC},
    {"EDIT",     fun_edit,     3,  0,          CA_PUBLIC},
    {"ELEMENTS", fun_elements, 0,  FN_VARARGS, CA_PUBLIC},
    {"ELOCK",    fun_elock,    2,  0,          CA_PUBLIC},
    {"EMPTY",    fun_empty,    0,  FN_VARARGS, CA_PUBLIC},
    {"ENCRYPT",  fun_encrypt,  2,  0,          CA_PUBLIC},
    {"EQ",       fun_eq,       2,  0,          CA_PUBLIC},
    {"ESCAPE",   fun_escape,  -1,  0,          CA_PUBLIC},
    {"EXIT",     fun_exit,     1,  0,          CA_PUBLIC},
    {"EXP",      fun_exp,      1,  0,          CA_PUBLIC},
    {"EXTRACT",  fun_extract,  0,  FN_VARARGS, CA_PUBLIC},
    {"EVAL",     fun_eval,     0,  FN_VARARGS, CA_PUBLIC},
    {"SUBEVAL",  fun_subeval,  1,  0,          CA_PUBLIC},
    {"FDIV",     fun_fdiv,     2,  0,          CA_PUBLIC},
    {"FILTER",   fun_filter,   0,  FN_VARARGS, CA_PUBLIC},
    {"FINDABLE", fun_findable, 2,  0,          CA_PUBLIC},
    {"FIRST",    fun_first,    0,  FN_VARARGS, CA_PUBLIC},
    {"FLAGS",    fun_flags,    1,  0,          CA_PUBLIC},
    {"FLOOR",    fun_floor,    1,  0,          CA_PUBLIC},
    {"FOLD",     fun_fold,     0,  FN_VARARGS, CA_PUBLIC},
    {"FOREACH",  fun_foreach,  0,  FN_VARARGS, CA_PUBLIC},
    {"FULLNAME", fun_fullname, 1,  0,          CA_PUBLIC},
    {"GET",      fun_get,      1,  0,          CA_PUBLIC},
    {"GET_EVAL", fun_get_eval, 1,  0,          CA_PUBLIC},
    {"GRAB",     fun_grab,     0,  FN_VARARGS, CA_PUBLIC},
    {"GREP",     fun_grep,     3,  0,          CA_PUBLIC},
    {"GREPI",    fun_grepi,    3,  0,          CA_PUBLIC},
    {"GT",       fun_gt,       2,  0,          CA_PUBLIC},
    {"GTE",      fun_gte,      2,  0,          CA_PUBLIC},
    {"HASATTR",  fun_hasattr,  2,  0,          CA_PUBLIC},
    {"HASATTRP", fun_hasattrp, 2,  0,          CA_PUBLIC},
    {"HASFLAG",  fun_hasflag,  2,  0,          CA_PUBLIC},
    {"HASPOWER", fun_haspower, 2,  0,          CA_PUBLIC},
    {"HASTYPE",  fun_hastype,  2,  0,          CA_PUBLIC},
    {"HOME",     fun_home,     1,  0,          CA_PUBLIC},
    {"IADD",     fun_iadd,     0,  FN_VARARGS, CA_PUBLIC},
    {"IDIV",     fun_div,      2,  0,          CA_PUBLIC},
    {"IDLE",     fun_idle,     1,  0,          CA_PUBLIC},
    {"IFELSE",   fun_ifelse,   3,  FN_NO_EVAL, CA_PUBLIC},
    {"IMUL",     fun_imul,     0,  FN_VARARGS, CA_PUBLIC},
    {"INC",      fun_inc,      1,  0,          CA_PUBLIC},
    {"INDEX",    fun_index,    4,  0,          CA_PUBLIC},
    {"INSERT",   fun_insert,   0,  FN_VARARGS, CA_PUBLIC},
    {"INZONE",   fun_inzone,   1,  0,          CA_PUBLIC},
    {"ISDBREF",  fun_isdbref,  1,  0,          CA_PUBLIC},
    {"ISNUM",    fun_isnum,    1,  0,          CA_PUBLIC},
    {"ISUB",     fun_isub,     2,  0,          CA_PUBLIC},
    {"ISWORD",   fun_isword,   1,  0,          CA_PUBLIC},
    {"ITER",     fun_iter,     0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC},
    {"LAST",     fun_last,     0,  FN_VARARGS, CA_PUBLIC},
    {"LATTR",    fun_lattr,    1,  0,          CA_PUBLIC},
    {"LCON",     fun_lcon,     1,  0,          CA_PUBLIC},
    {"LCSTR",    fun_lcstr,   -1,  0,          CA_PUBLIC},
    {"LDELETE",  fun_ldelete,  0,  FN_VARARGS, CA_PUBLIC},
    {"LEXITS",   fun_lexits,   1,  0,          CA_PUBLIC},
    {"LPARENT",  fun_lparent,  1,  0,          CA_PUBLIC}, 
    {"LIST",     fun_list,     0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC}, 
    {"LIT",      fun_lit,      1,  FN_NO_EVAL, CA_PUBLIC},
    {"LJUST",    fun_ljust,    0,  FN_VARARGS, CA_PUBLIC},
    {"LINK",     fun_link,     2,  0,          CA_PUBLIC},
    {"LN",       fun_ln,       1,  0,          CA_PUBLIC},
    {"LNUM",     fun_lnum,     1,  0,          CA_PUBLIC},
    {"LOC",      fun_loc,      1,  0,          CA_PUBLIC},
    {"LOCATE",   fun_locate,   3,  0,          CA_PUBLIC},
    {"LOCK",     fun_lock,     1,  0,          CA_PUBLIC},
    {"LOG",      fun_log,      1,  0,          CA_PUBLIC},
    {"LSTACK",   fun_lstack,   0,  FN_VARARGS, CA_PUBLIC},
    {"LT",       fun_lt,       2,  0,          CA_PUBLIC},
    {"LTE",      fun_lte,      2,  0,          CA_PUBLIC},
    {"LWHO",     fun_lwho,     0,  0,          CA_PUBLIC},
    {"MAIL",     fun_mail,     0,  FN_VARARGS, CA_PUBLIC},
    {"MAILFROM", fun_mailfrom, 0,  FN_VARARGS, CA_PUBLIC},
    {"MAP",      fun_map,      0,  FN_VARARGS, CA_PUBLIC},
    {"MATCH",    fun_match,    0,  FN_VARARGS, CA_PUBLIC},
    {"MATCHALL", fun_matchall, 0,  FN_VARARGS, CA_PUBLIC},
    {"MAX",      fun_max,      0,  FN_VARARGS, CA_PUBLIC},
    {"MEMBER",   fun_member,   0,  FN_VARARGS, CA_PUBLIC},
    {"MERGE",    fun_merge,    3,  0,          CA_PUBLIC},
    {"MID",      fun_mid,      3,  0,          CA_PUBLIC},
    {"MIN",      fun_min,      0,  FN_VARARGS, CA_PUBLIC},
    {"MIX",      fun_mix,      0,  FN_VARARGS, CA_PUBLIC},
    {"MOD",      fun_mod,      2,  0,          CA_PUBLIC},
    {"MONEY",    fun_money,    1,  0,          CA_PUBLIC},
    {"MOTD",     fun_motd,     0,  0,          CA_PUBLIC},
    {"MUDNAME",  fun_mudname,  0,  0,          CA_PUBLIC},
    {"MUL",      fun_mul,      0,  FN_VARARGS, CA_PUBLIC},
    {"MUNGE",    fun_munge,    0,  FN_VARARGS, CA_PUBLIC},
    {"NAME",     fun_name,     1,  0,          CA_PUBLIC},
    {"NEARBY",   fun_nearby,   2,  0,          CA_PUBLIC},
    {"NEQ",      fun_neq,      2,  0,          CA_PUBLIC},
    {"NEXT",     fun_next,     1,  0,          CA_PUBLIC},
    {"NOT",      fun_not,      1,  0,          CA_PUBLIC},
    {"NUM",      fun_num,      1,  0,          CA_PUBLIC},
    {"ITEMS",    fun_items,    0,  FN_VARARGS, CA_PUBLIC},
    {"OBJ",      fun_obj,      1,  0,          CA_PUBLIC},
    {"OBJEVAL",  fun_objeval,  2,  FN_NO_EVAL, CA_PUBLIC},
    {"OBJMEM",   fun_objmem,   1,  0,          CA_PUBLIC},
    {"OR",       fun_or,       0,  FN_VARARGS, CA_PUBLIC},
    {"ORFLAGS",  fun_orflags,  2,  0,          CA_PUBLIC},
    {"OWNER",    fun_owner,    1,  0,          CA_PUBLIC},
    {"PARENT",   fun_parent,   1,  0,          CA_PUBLIC},
    {"PARSE",    fun_parse,    0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC},
    {"PEEK",     fun_peek,     0,  FN_VARARGS, CA_PUBLIC},
    {"PEMIT",    fun_pemit,    2,  0,          CA_PUBLIC},
    {"PI",       fun_pi,       0,  0,          CA_PUBLIC},
    {"PLAYMEM",  fun_playmem,  1,  0,          CA_PUBLIC},
    {"PMATCH",   fun_pmatch,   1,  0,          CA_PUBLIC},
    {"POLL",     fun_poll,     0,  0,          CA_PUBLIC},
    {"POP",      fun_pop,      0,  FN_VARARGS, CA_PUBLIC},
    {"PORTS",    fun_ports,    1,  0,          CA_PUBLIC},
    {"POS",      fun_pos,      2,  0,          CA_PUBLIC},
    {"POSS",     fun_poss,     1,  0,          CA_PUBLIC},
    {"POWER",    fun_power,    2,  0,          CA_PUBLIC},
    {"PUSH",     fun_push,     0,  FN_VARARGS, CA_PUBLIC},
    {"CASE",     fun_case,     0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC},
    {"R",        fun_r,        1,  0,          CA_PUBLIC},
    {"RAND",     fun_rand,     1,  0,          CA_PUBLIC},
    {"REGMATCH", fun_regmatch, 0,  FN_VARARGS, CA_PUBLIC},
    {"REMOVE",   fun_remove,   0,  FN_VARARGS, CA_PUBLIC},
    {"REPEAT",   fun_repeat,   2,  0,          CA_PUBLIC},
    {"REPLACE",  fun_replace,  0,  FN_VARARGS, CA_PUBLIC},
    {"REST",     fun_rest,     0,  FN_VARARGS, CA_PUBLIC},
    {"REVERSE",  fun_reverse, -1,  0,          CA_PUBLIC},
    {"REVWORDS", fun_revwords, 0,  FN_VARARGS, CA_PUBLIC},
    {"RJUST",    fun_rjust,    0,  FN_VARARGS, CA_PUBLIC},
    {"RLOC",     fun_rloc,     2,  0,          CA_PUBLIC},
    {"ROOM",     fun_room,     1,  0,          CA_PUBLIC},
    {"ROUND",    fun_round,    2,  0,          CA_PUBLIC},
    {"S",        fun_s,       -1,  0,          CA_PUBLIC},
    {"SCRAMBLE", fun_scramble, 1,  0,          CA_PUBLIC},
    {"SEARCH",   fun_search,  -1,  0,          CA_PUBLIC},
    {"SECS",     fun_secs,     0,  0,          CA_PUBLIC},
    {"SECURE",   fun_secure,  -1,  0,          CA_PUBLIC},
    {"SET",      fun_set,      2,  0,          CA_PUBLIC},
    {"SETDIFF",  fun_setdiff,  0,  FN_VARARGS, CA_PUBLIC},
    {"SETINTER", fun_setinter, 0,  FN_VARARGS, CA_PUBLIC},
    {"SETQ",     fun_setq,     2,  0,          CA_PUBLIC},
    {"SETR",     fun_setr,     2,  0,          CA_PUBLIC},
    {"SETUNION", fun_setunion, 0,  FN_VARARGS, CA_PUBLIC},
    {"SHL",      fun_shl,      2,  0,          CA_PUBLIC},
    {"SHR",      fun_shr,      2,  0,          CA_PUBLIC},
    {"SHUFFLE",  fun_shuffle,  0,  FN_VARARGS, CA_PUBLIC},
    {"SIGN",     fun_sign,     1,  0,          CA_PUBLIC},
    {"SIN",      fun_sin,      1,  0,          CA_PUBLIC},
    {"SORT",     fun_sort,     0,  FN_VARARGS, CA_PUBLIC},
    {"SORTBY",   fun_sortby,   0,  FN_VARARGS, CA_PUBLIC},
    {"SPACE",    fun_space,    1,  0,          CA_PUBLIC},
    {"SPLICE",   fun_splice,   0,  FN_VARARGS, CA_PUBLIC},
    {"SQRT",     fun_sqrt,     1,  0,          CA_PUBLIC},
    {"SQUISH",   fun_squish,   1,  0,          CA_PUBLIC},
    {"STARTTIME",fun_starttime,0,  0,          CA_PUBLIC},
    {"STATS",    fun_stats,    1,  0,          CA_PUBLIC},
    {"STRCAT",   fun_strcat,   0,  FN_VARARGS, CA_PUBLIC},
    {"STRIPANSI",fun_stripansi,1,  0,          CA_PUBLIC},
    {"STRLEN",   fun_strlen,  -1,  0,          CA_PUBLIC},
    {"STRMATCH", fun_strmatch, 2,  0,          CA_PUBLIC},
    {"STRTRUNC", fun_strtrunc, 2,  0,          CA_PUBLIC},
    {"SUB",      fun_sub,      2,  0,          CA_PUBLIC},
    {"SUBJ",     fun_subj,     1,  0,          CA_PUBLIC},
    {"SWITCH",   fun_switch,   0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC},
    {"TAN",      fun_tan,      1,  0,          CA_PUBLIC},
    {"TEL",      fun_tel,      2,  0,          CA_PUBLIC},
    {"TIME",     fun_time,     0,  0,          CA_PUBLIC},
    {"TRANSLATE",fun_translate,2,  0,          CA_PUBLIC},
    {"TRIM",     fun_trim,     0,  FN_VARARGS, CA_PUBLIC},
    {"TRUNC",    fun_trunc,    1,  0,          CA_PUBLIC},
    {"TYPE",     fun_type,     1,  0,          CA_PUBLIC},
    {"U",        fun_u,        0,  FN_VARARGS, CA_PUBLIC},
    {"UCSTR",    fun_ucstr,   -1,  0,          CA_PUBLIC},
    {"UDEFAULT", fun_udefault, 0,  FN_VARARGS|FN_NO_EVAL, CA_PUBLIC},
    {"ULOCAL",   fun_ulocal,   0,  FN_VARARGS, CA_PUBLIC},
    {"V",        fun_v,        1,  0,          CA_PUBLIC},
    {"VADD",     fun_vadd,     0,  FN_VARARGS, CA_PUBLIC},
    {"VALID",    fun_valid,    2,  0,          CA_PUBLIC},
    {"VCROSS",   fun_vcross,   0,  FN_VARARGS, CA_PUBLIC},
    {"VDIM",     fun_vdim,     0,  FN_VARARGS, CA_PUBLIC},
    {"VDOT",     fun_vdot,     0,  FN_VARARGS, CA_PUBLIC},
    {"VERSION",  fun_version,  0,  0,          CA_PUBLIC},
    {"VISIBLE",  fun_visible,  2,  0,          CA_PUBLIC},
    {"VMAG",     fun_vmag,     0,  FN_VARARGS, CA_PUBLIC},
    {"VMUL",     fun_vmul,     0,  FN_VARARGS, CA_PUBLIC},
    {"VSUB",     fun_vsub,     0,  FN_VARARGS, CA_PUBLIC},
    {"VUNIT",    fun_vunit,    0,  FN_VARARGS, CA_PUBLIC},
    {"WHERE",    fun_where,    1,  0,          CA_PUBLIC},
    {"WORDPOS",  fun_wordpos,  0,  FN_VARARGS, CA_PUBLIC},
    {"WORDS",    fun_words,    0,  FN_VARARGS, CA_PUBLIC},
    {"XGET",     fun_xget,     2,  0,          CA_PUBLIC},
    {"XOR",      fun_xor,      0,  FN_VARARGS, CA_PUBLIC},
    {"ZFUN",     fun_zfun,     0,  FN_VARARGS, CA_PUBLIC},
    {"ZONE",     fun_zone,     1,  0,          CA_PUBLIC},
    {"ZWHO",     fun_zwho,     1,  0,          CA_PUBLIC},
    {NULL,       NULL,         0,  0,          0}
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

void do_function(dbref player, dbref cause, int key, char *fname, char *target)
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
            ap = atr_num(ufp2->atr);
            notify(player, tprintf("%-28.28s   #%-7d  %-30.30s  %c%c",
                ufp2->name, ufp2->obj, ap->name, ((ufp2->flags & FN_PRIV) ? 'W' : '-'),
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
        notify_quiet(player, "I don't see that here.");
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
        ufp = (UFUN *) MEMALLOC(sizeof(UFUN), __FILE__, __LINE__);
        ufp->name = strsave(np);
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
            for (ufp2 = ufun_head; ufp2->next; ufp2 = ufp2->next) ;
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
            return (cf_modify_bits(&fp->perms, ap, extra, player, cmd));
        }
    }
    for (ufp = ufun_head; ufp; ufp = ufp->next)
    {
        if (!string_compare(ufp->name, str))
        {
            return (cf_modify_bits(&ufp->perms, ap, extra, player, cmd));
        }
    }
    cf_log_notfound(player, cmd, "Function", str);
    return -1;
}

// Some libraries go nuts...just because you force feed them lots of ASCII.
//
#define ATOF_LIMIT 100
double safe_atof(char *szString)
{
    double ret;

    int n = strlen(szString);
    if (n > ATOF_LIMIT)
    {
        int ch = szString[ATOF_LIMIT-1];
        szString[ATOF_LIMIT-1] = '\0';
        ret = atof(szString);
        szString[ATOF_LIMIT-1] = ch;
    }
    else
    {
        ret = atof(szString);
    }
    return ret;
}

/////////////////////////////////////////////////////////////////
// Function : iadd(Arg[0], Arg[1],..,Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////
 
FUNCTION(fun_iadd)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    INT64 sum = 0;
 
    for (int i = 0; i < nfargs; i++)
    {
        sum += Tiny_atoi64(fargs[i]);
    }
    safe_str(Tiny_i64toa_t(sum), buff, bufc);
}
 
/////////////////////////////////////////////////////////////////
// Function : isub(Arg[0], Arg[1])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////
 
FUNCTION(fun_isub)
{
    INT64 diff = Tiny_atoi64(fargs[0]) - Tiny_atoi64(fargs[1]);
    safe_str(Tiny_i64toa_t(diff), buff, bufc);
}

/////////////////////////////////////////////////////////////////
// Function : imul(Arg[0], Arg[1], ... , Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////
 
FUNCTION(fun_imul)
{
    if (nfargs <= 0)
    {
        safe_str("#-1 TOO FEW ARGUMENTS", buff, bufc);
        return;
    }
    INT64 prod = 1;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= Tiny_atoi64(fargs[i]);
    }
    safe_str(Tiny_i64toa_t(prod), buff, bufc);
}
