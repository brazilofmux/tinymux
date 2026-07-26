/*! \file mathutil.cpp
 * \brief TinyMUX math-related helper functions.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"

int g_float_precision = -1;

#ifdef HAVE_IEEE_FP_FORMAT

const UTF8 *mux_FPStrings[8] =
{
    T("+Inf"),
    T("-Inf"),
    T("Ind"),
    T("NaN"),
    T("0"),
    T("0"),
    T("0"),
    T("0")
};

#define IEEE_MASK_SIGN     UINT64_C(0x8000000000000000)
#define IEEE_MASK_EXPONENT UINT64_C(0x7FF0000000000000)
#define IEEE_MASK_MANTISSA UINT64_C(0x000FFFFFFFFFFFFF)
#define IEEE_MASK_QNAN     UINT64_C(0x0008000000000000)

#define ARBITRARY_NUMBER 1
#define IEEE_MAKE_TABLESIZE 5

typedef union
{
    uint64_t ui64;
    double d;
} SpecialFloatUnion;

// We return a Quiet NAN when a Signalling NAN is requested because
// any operation on a Signalling NAN will result in a Quiet NAN anyway.
// MUX doesn't catch SIGFPE, but if it did, a Signalling NAN would
// generate a SIGFPE.
//
static SpecialFloatUnion SpecialFloatTable[IEEE_MAKE_TABLESIZE] =
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

int mux_fpclass(double result)
{
    SpecialFloatUnion u;

    u.d = result;

    if ((u.ui64 & IEEE_MASK_EXPONENT) == 0)
    {
        if (u.ui64 & IEEE_MASK_MANTISSA)
        {
            if (u.ui64 & IEEE_MASK_SIGN) return MUX_FPCLASS_ND;
            else                        return MUX_FPCLASS_PD;
        }
        else
        {
            if (u.ui64 & IEEE_MASK_SIGN) return MUX_FPCLASS_NZ;
            else                        return MUX_FPCLASS_PZ;
        }
    }
    else if ((u.ui64 & IEEE_MASK_EXPONENT) == IEEE_MASK_EXPONENT)
    {
        if (u.ui64 & IEEE_MASK_MANTISSA)
        {
            if (u.ui64 & IEEE_MASK_QNAN) return MUX_FPCLASS_QNAN;
            else                        return MUX_FPCLASS_SNAN;
        }
        else
        {
            if (u.ui64 & IEEE_MASK_SIGN) return MUX_FPCLASS_NINF;
            else                        return MUX_FPCLASS_PINF;
        }
    }
    else
    {
        if (u.ui64 & IEEE_MASK_SIGN)     return MUX_FPCLASS_NN;
        else                            return MUX_FPCLASS_PN;
    }
}
#endif // HAVE_IEEE_FP_FORMAT

static double AddWithError(double& err, double a, double b)
{
    double sum = a+b;
    err = b-(sum-a);
    return sum;
}

// Typically, we are within 1ulp of an exact answer, find the shortest answer
// within that 1 ulp (that is, within 0, +ulp, and -ulp).
//
double NearestPretty(double R)
{
    // Fast path for integer-valued results (the common case for
    // mul/add/sub over integer lists).  When |R| < 2^50, every FP
    // neighbor R +/- k*ulp(R) (k = 1..4, the search window below) is
    // non-integral, so dtoa renders each with strictly more decimal
    // digits than the integral R itself -- the ULP search would return
    // R unchanged.  Short-circuiting here skips 9 mux_dtoa calls per
    // formatted number.  NaN/Inf fail the magnitude test and fall
    // through to the exact path; |R| >= 2^50 (where a neighbor can be
    // integral) also falls through unchanged.
    //
    if (fabs(R) < 1125899906842624.0)   // 2^50
    {
        int64_t iR = static_cast<int64_t>(R);
        if (static_cast<double>(iR) == R)
        {
            return R;
        }
    }

    UTF8* rve = nullptr;
    int decpt;
    int bNegative;
    const int mode = 0;

    double ulpR = mux_ulp(R);
    double finalR = R;

    // Seed the search with R itself rather than a sentinel, so R wins any
    // tie and a neighbour is taken only when it is STRICTLY shorter.  The
    // old seed of 10000 let the first candidate examined (i = -4) claim an
    // equal-length "win", so a result already as short as possible was
    // still displaced by a lower neighbour.  That is what broke the
    // identities:
    //
    //   mul(1.224744871391589,1)  ->  1.224744871391588
    //   add(1.224744871391589,0)  ->  1.224744871391588
    //
    // Both render in 16 digits, so neither is prettier; the input was
    // discarded purely because it was examined last.  Genuine shortenings
    // are unaffected — where a neighbour really is shorter than R it is
    // still chosen, which is the point of the function.
    //
    UTF8* p0 = mux_dtoa(R, mode, 50, &decpt, &bNegative, &rve);
    size_t nDigits = rve - p0;

    for (int i = -4; i <= 4; i++)
    {
        if (0 == i)
        {
            // R is the seed above.
            //
            continue;
        }
        double testR = R + ulpR * static_cast<double>(i);
        UTF8* p = mux_dtoa(testR, mode, 50, &decpt, &bNegative, &rve);
        size_t nDigitsR0 = rve - p;
        if (nDigitsR0 < nDigits)
        {
            nDigits = nDigitsR0;
            finalR = testR;
        }
    }
    return finalR;
}

// Compare for decreasing order by absolute value.
//
static int DCL_CDECL f_comp_abs(const void *s1, const void *s2)
{
    double a = fabs(*reinterpret_cast<const double *>(s1));
    double b = fabs(*reinterpret_cast<const double *>(s2));

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

// Double compensation method. Extended by Priest from Knuth and Kahan.
//
// Error of sum is less than 2*epsilon or 1 ulp except for very large n.
// Return the result that yields the shortest number of base-10 digits.
//
double AddDoubles(int n, double pd[])
{
    qsort(pd, n, sizeof(double), f_comp_abs);
    double sum = 0.0;
    if (0 < n)
    {
        sum = pd[0];
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
    }
    return NearestPretty(sum);
}

/* ---------------------------------------------------------------------------
 * fval: copy the floating point value into a buffer and make it presentable
 */
void fval(UTF8 *buff, UTF8 **bufc, double result)
{
    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(result);
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        double rIntegerPart;
        double rFractionalPart = modf(result, &rIntegerPart);

        // The upper bound must be compared against 2^63, not INT64_MAX.
        // INT64_MAX (2^63-1) is not representable as a double and
        // converts to exactly 2^63, so `rIntegerPart <= INT64_MAX`
        // accepted 2^63 itself.  The cast is then undefined and yields
        // INT64_MIN on x86-64 and AArch64 alike, so every whole result
        // at or above 2^63 printed as -9223372036854775808 —
        // max(9223372036854775807,0) and abs(-9223372036854775807)
        // both returned a negative number.  2^63 is exactly
        // representable, so the strict `<` is the exact boundary.
        //
        // INT64_MIN is exactly representable, so `<=` is right there.
        //
        // int64_t rather than long: long is 32-bit on Win64, which
        // would silently narrow this fast path to +/-2^31 and format
        // the same value differently across platforms.
        //
        static const double dTwoPow63 = 9223372036854775808.0;
        if (  0.0 == rFractionalPart
           && static_cast<double>(INT64_MIN) <= rIntegerPart
           && rIntegerPart < dTwoPow63)
        {
            int64_t i = static_cast<int64_t>(rIntegerPart);
            safe_i64toa(i, buff, bufc);
        }
        else
        {
            safe_str(mux_ftoa(result, false, 0), buff, bufc);
        }
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

// Floating-point strings match one of the following patterns:
//
// [+-]?[0-9]?(.[0-9]+)([eE][+-]?[0-9]{1,3})?
// [+-]?[0-9]+(.[0-9]?)([eE][+-]?[0-9]{1,3})?
// +Inf
// -Inf
// Ind
// NaN
//
bool ParseFloat(PARSE_FLOAT_RESULT *pfr, const UTF8 *str, bool bStrict)
{
    memset(pfr, 0, sizeof(PARSE_FLOAT_RESULT));

    // Parse Input
    //
    pfr->pMeat = str;
    if (  !mux_isdigit(*str)
       && *str != '.')
    {
        while (mux_isspace(*str))
        {
            str++;
        }

        pfr->pMeat = str;
        if (*str == '-')
        {
            pfr->iLeadingSign = '-';
            str++;
        }
        else if (*str == '+')
        {
            pfr->iLeadingSign = '+';
            str++;
        }

        if (  !mux_isdigit(*str)
           && *str != '.')
        {
            // Look for three magic strings.
            //
            if (  'I' == str[0]
               || 'i' == str[0])
            {
                // Could be 'Inf' or 'Ind'
                //
                if (  'N' == str[1]
                   || 'n' == str[1])
                {
                    if (  'F' == str[2]
                       || 'f' == str[2])
                    {
                        // Inf
                        //
                        if (pfr->iLeadingSign == '-')
                        {
                            pfr->iString = IEEE_MAKE_NINF;
                        }
                        else
                        {
                            pfr->iString = IEEE_MAKE_PINF;
                        }
                        str += 3;
                        goto LastSpaces;
                    }
                    else if (  'D' == str[2]
                            || 'd' == str[2])
                    {
                        // Ind
                        //
                        pfr->iString = IEEE_MAKE_IND;
                        str += 3;
                        goto LastSpaces;
                    }
                }
            }
            else if (  'N' == str[0]
                    || 'n' == str[0])
            {
                // Could be 'Nan'
                //
                if (  'A' == str[1]
                   || 'a' == str[1])
                {
                    if (  'N' == str[2]
                       || 'n' == str[2])
                    {
                        // Nan
                        //
                        pfr->iString = IEEE_MAKE_NAN;
                        str += 3;
                        goto LastSpaces;
                    }
                }
            }
            return false;
        }
    }

    // At this point, we have processed the leading sign, handled all
    // the magic strings, skipped the leading spaces, and best of all
    // we either have a digit or a decimal point.
    //
    pfr->pDigitsA = str;
    while (mux_isdigit(*str))
    {
        pfr->nDigitsA++;
        str++;
    }

    if (*str == '.')
    {
        str++;
    }

    pfr->pDigitsB = str;
    while (mux_isdigit(*str))
    {
        pfr->nDigitsB++;
        str++;
    }

    if (  pfr->nDigitsA == 0
       && pfr->nDigitsB == 0)
    {
        return false;
    }

    if (  'E' == *str
       || 'e' == *str)
    {
        // There is an exponent portion.
        //
        str++;
        if (*str == '-')
        {
            pfr->iExponentSign = '-';
            str++;
        }
        else if (*str == '+')
        {
            pfr->iExponentSign = '+';
            str++;
        }
        pfr->pDigitsC = str;
        while (mux_isdigit(*str))
        {
            pfr->nDigitsC++;
            str++;
        }

        if (  pfr->nDigitsC < 1
           || 4 < pfr->nDigitsC)
        {
            return false;
        }
    }

LastSpaces:

    pfr->nMeat = str - pfr->pMeat;

    // Trailing spaces.
    //
    while (mux_isspace(*str))
    {
        str++;
    }

    if (bStrict)
    {
        return (!*str);
    }
    else
    {
        return true;
    }
}

#define ATOF_LIMIT 100
static const double powerstab[10] =
{
            1.0,
           10.0,
          100.0,
         1000.0,
        10000.0,
       100000.0,
      1000000.0,
     10000000.0,
    100000000.0,
   1000000000.0
};

double mux_atof(const UTF8 *szString, bool bStrict)
{
    PARSE_FLOAT_RESULT pfr;
    if (!ParseFloat(&pfr, szString, bStrict))
    {
        return 0.0;
    }

    if (pfr.iString)
    {
        // Return the double value which corresponds to the
        // string when HAVE_IEEE_FORMAT.
        //
#ifdef HAVE_IEEE_FP_FORMAT
        return MakeSpecialFloat(pfr.iString);
#else // HAVE_IEEE_FP_FORMAT
        return 0.0;
#endif // HAVE_IEEE_FP_FORMAT
    }

    // See if we can shortcut the decoding process.
    //
    double ret;
    if (  pfr.nDigitsA <= 9
       && pfr.nDigitsC == 0)
    {
        if (pfr.nDigitsB <= 9)
        {
            if (pfr.nDigitsB == 0)
            {
                // This 'floating-point' number is just an integer.
                //
                // mux_atoi64: long is 32-bit on LLP64; nDigitsA is at most
                // 9 here so either holds the value, but keep the wider API
                // so a future digit-limit raise cannot reintroduce #1402.
                //
                ret = static_cast<double>(mux_atoi64(pfr.pDigitsA));
            }
            else
            {
                // This 'floating-point' number is fixed-point.
                //
                double rA = static_cast<double>(mux_atoi64(pfr.pDigitsA));
                double rB = static_cast<double>(mux_atoi64(pfr.pDigitsB));
                double rScale = powerstab[pfr.nDigitsB];
                ret = rA + rB/rScale;

                // As it is, ret is within a single bit of what a
                // a call to atof would return. However, we can
                // achieve that last lowest bit of precision by
                // computing a residual.
                //
                double residual = (ret - rA)*rScale;
                ret += (rB - residual)/rScale;
            }
            if (pfr.iLeadingSign == '-')
            {
                ret = -ret;
            }
            return ret;
        }
    }

    const UTF8 *p = pfr.pMeat;
    size_t n = pfr.nMeat;

    // We need to protect certain libraries from going nuts from being
    // force fed lots of ASCII.
    //
    UTF8 *pTmp = nullptr;
    if (n > ATOF_LIMIT)
    {
        pTmp = alloc_lbuf("mux_atof");
        memcpy(pTmp, p, ATOF_LIMIT);
        pTmp[ATOF_LIMIT] = '\0';
        p = pTmp;
    }

    ret = mux_strtod(p, nullptr);

    if (pTmp)
    {
        free_lbuf(pTmp);
    }

    return ret;
}

UTF8 *mux_ftoa(double r, bool bRounded, int frac)
{
    static UTF8 buffer[100];
    UTF8 *q = buffer;
    UTF8 *rve = nullptr;
    int iDecimalPoint = 0;
    int bNegative = 0;
    int mode = 0;
    int nRequestMaximum = 50;
    const int nRequestMinimum = -20;
    int nRequest = nRequestMaximum;

    // If float_precision is enabled, let it override nRequestMaximum.
    //
    if (0 <= g_float_precision)
    {
        mode = 5;
        if (g_float_precision < nRequestMaximum)
        {
            nRequestMaximum = g_float_precision;
            nRequest        = g_float_precision;
        }
    }

    if (bRounded)
    {
        mode = 5;
        nRequest = frac;
        if (nRequestMaximum < nRequest)
        {
            nRequest = nRequestMaximum;
        }
        else if (nRequest < nRequestMinimum)
        {
            nRequest = nRequestMinimum;
        }
    }

    UTF8 *p = mux_dtoa(r, mode, nRequest, &iDecimalPoint, &bNegative, &rve);
    size_t nSize = rve - p;
    if (nSize > 50)
    {
        nSize = 50;
    }

    if (bNegative)
    {
        *q++ = '-';
    }

    if (iDecimalPoint == 9999)
    {
        // Inf or NaN
        //
        memcpy(q, p, nSize);
        q += nSize;
    }
    else if (nSize <= 0)
    {
        // Zero
        //
        if (bNegative)
        {
            // If we laid down a minus sign, we should remove it.
            //
            q--;
        }
        *q++ = '0';
        if (  bRounded
           && 0 < nRequest)
        {
            *q++ = '.';
            memset(q, '0', nRequest);
            q += nRequest;
        }
    }
    else if (  iDecimalPoint <= -6
            || 18 <= iDecimalPoint)
    {
        *q++ = *p++;
        if (1 < nSize)
        {
            *q++ = '.';
            memcpy(q, p, nSize-1);
            q += nSize-1;
        }
        *q++ = 'E';
        q += mux_ltoa(iDecimalPoint-1, q);
    }
    else if (iDecimalPoint <= 0)
    {
        // iDecimalPoint = -5 to 0
        //
        *q++ = '0';
        *q++ = '.';
        memset(q, '0', -iDecimalPoint);
        q += -iDecimalPoint;
        memcpy(q, p, nSize);
        q += nSize;
        if (bRounded)
        {
            size_t nPad = nRequest - (nSize - iDecimalPoint);
            if (0 < nPad)
            {
                memset(q, '0', nPad);
                q += nPad;
            }
        }
    }
    else
    {
        // iDecimalPoint = 1 to 17
        //
        if (nSize <= static_cast<size_t>(iDecimalPoint))
        {
            memcpy(q, p, nSize);
            q += nSize;
            memset(q, '0', iDecimalPoint - nSize);
            q += iDecimalPoint - nSize;
            if (  bRounded
               && 0 < nRequest)
            {
                *q++ = '.';
                memset(q, '0', nRequest);
                q += nRequest;
            }
        }
        else
        {
            memcpy(q, p, iDecimalPoint);
            q += iDecimalPoint;
            p += iDecimalPoint;
            *q++ = '.';
            memcpy(q, p, nSize - iDecimalPoint);
            q += nSize - iDecimalPoint;
            if (bRounded)
            {
                size_t nPad = nRequest - (nSize - iDecimalPoint);
                if (0 < nPad)
                {
                    memset(q, '0', nPad);
                    q += nPad;
                }
            }
        }
    }
    *q = '\0';
    return buffer;
}

// In the conversion routines that follow, digits are decoded into a buffer in
// reverse order with a possible leading '-' if the value was negative.
//
static void ReverseDigits(UTF8 *pFirst, UTF8 *pLast)
{
    // Stop when we reach or pass the middle.
    //
    while (pFirst < pLast)
    {
        // Swap characters at *pFirst and *pLast.
        //
        UTF8 temp = *pLast;
        *pLast = *pFirst;
        *pFirst = temp;

        // Move pFirst and pLast towards the middle.
        //
        --pLast;
        ++pFirst;
    }
}

const UTF8 Digits16U[17] = "0123456789ABCDEF";
const UTF8 Digits16L[17] = "0123456789abcdef";

size_t mux_utox(unsigned long uval, UTF8 *buf, bool bUpperCase)
{
    UTF8 *p = buf;
    UTF8 *q = p;
    const UTF8 *pDigits = bUpperCase ? Digits16U : Digits16L;

    while (uval > 15)
    {
        *p++ = pDigits[uval % 16];
        uval /= 16;
    }
    *p++ = pDigits[uval];
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

size_t mux_ui64tox(uint64_t uval, UTF8 *buf, bool bUpperCase)
{
    UTF8 *p = buf;
    UTF8 *q = p;
    const UTF8 *pDigits = bUpperCase ? Digits16U : Digits16L;

    while (uval > 15)
    {
        *p++ = pDigits[uval % 16];
        uval /= 16;
    }
    *p++ = pDigits[uval];
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

// Octal.  Note the buffer requirement: an unsigned 64-bit value is up to 22
// octal digits, which is one more than LONGEST_I64 allows for decimal, so a
// caller sizing a scratch buffer from I64BUF_SIZE is not large enough.
//
size_t mux_utoo(unsigned long uval, UTF8 *buf)
{
    UTF8 *p = buf;
    UTF8 *q = p;

    while (uval > 7)
    {
        *p++ = static_cast<UTF8>('0' + (uval % 8));
        uval /= 8;
    }
    *p++ = static_cast<UTF8>('0' + uval);
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

size_t mux_ui64too(uint64_t uval, UTF8 *buf)
{
    UTF8 *p = buf;
    UTF8 *q = p;

    while (uval > 7)
    {
        *p++ = static_cast<UTF8>('0' + (uval % 8));
        uval /= 8;
    }
    *p++ = static_cast<UTF8>('0' + uval);
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

const UTF8 Digits100[201] =
"001020304050607080900111213141516171819102122232425262728292\
031323334353637383930414243444546474849405152535455565758595\
061626364656667686960717273747576777879708182838485868788898\
09192939495969798999";

size_t mux_utoa(unsigned long uval, UTF8 *buf)
{
    UTF8 *p = buf;
    UTF8 *q = p;

    const UTF8 *z;
    while (uval > 99)
    {
        z = Digits100 + ((uval % 100) << 1);
        uval /= 100;
        *p++ = *z;
        *p++ = *(z+1);
    }
    z = Digits100 + (uval << 1);
    *p++ = *z;
    if (uval > 9)
    {
        *p++ = *(z+1);
    }
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

size_t mux_ltoa(long val, UTF8 *buf)
{
    UTF8 *p = buf;
    unsigned long uval;
    bool is_negative = val < 0;

    if (is_negative)
    {
        *p++ = '-';
        uval = (val == LONG_MIN) ? (static_cast<unsigned long>(LONG_MAX) + 1) : static_cast<unsigned long>(-val);
    }
    else
    {
        uval = static_cast<unsigned long>(val);
    }
    p += mux_utoa(uval, p);
    return p - buf;
}

UTF8 *mux_ltoa_t(long val)
{
    static UTF8 buff[I32BUF_SIZE];
    mux_ltoa(val, buff);
    return buff;
}

void safe_ltoa(long val, UTF8 *buff, UTF8 **bufc)
{
    static UTF8 temp[I32BUF_SIZE];
    size_t n = mux_ltoa(val, temp);
    safe_copy_buf(temp, n, buff, bufc);
}

size_t mux_ui64toa(uint64_t uval, UTF8 *buf)
{
    UTF8 *p = buf;
    UTF8 *q = p;

    const UTF8 *z;
    while (uval > 99)
    {
        z = Digits100 + ((uval % 100) << 1);
        uval /= 100;
        *p++ = *z;
        *p++ = *(z+1);
    }
    z = Digits100 + (uval << 1);
    *p++ = *z;
    if (uval > 9)
    {
        *p++ = *(z+1);
    }
    *p = '\0';
    ReverseDigits(q, p-1);
    return p - buf;
}

size_t mux_i64toa(int64_t val, UTF8* buf) {
    UTF8* p = buf;
    uint64_t uval;
    bool is_negative = val < 0;

    if (is_negative) {
        *p++ = '-';
        uval = (val == INT64_MIN) ? (static_cast<uint64_t>(INT64_MAX) + 1) : static_cast<uint64_t>(-val);
    }
    else {
        uval = static_cast<uint64_t>(val);
    }
    p += mux_ui64toa(uval, p);
    return p - buf;
}

UTF8 *mux_i64toa_t(int64_t val)
{
    static UTF8 buff[I64BUF_SIZE];
    mux_i64toa(val, buff);
    return buff;
}

void safe_i64toa(int64_t val, UTF8 *buff, UTF8 **bufc)
{
    static UTF8 temp[I64BUF_SIZE];
    size_t n = mux_i64toa(val, temp);
    safe_copy_buf(temp, n, buff, bufc);
}

const UTF8 TableATOI[16][10] =
{
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9},
    { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29},
    { 30, 31, 32, 33, 34, 35, 36, 37, 38, 39},
    { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49},
    { 50, 51, 52, 53, 54, 55, 56, 57, 58, 59},
    { 60, 61, 62, 63, 64, 65, 66, 67, 68, 69},
    { 70, 71, 72, 73, 74, 75, 76, 77, 78, 79},
    { 80, 81, 82, 83, 84, 85, 86, 87, 88, 89},
    { 90, 91, 92, 93, 94, 95, 96, 97, 98, 99}
};

long mux_atol(const UTF8 *pString)
{
    long sum = 0;
    int LeadingCharacter = 0;

    // Convert ASCII digits
    //
    unsigned int c1;
    unsigned int c0 = pString[0];
    if (!mux_isdigit(c0))
    {
        while (mux_isspace(pString[0]))
        {
            pString++;
        }
        LeadingCharacter = pString[0];
        if (  LeadingCharacter == '-'
           || LeadingCharacter == '+')
        {
            pString++;
        }
        c0 = pString[0];
        if (!mux_isdigit(c0))
        {
            return 0;
        }
    }

    do
    {
        c1 = pString[1];
        if (mux_isdigit(c1))
        {
            sum = UINT64_C(100) * sum
                + static_cast<uint64_t>(TableATOI[c0-'0'][c1-'0']);
            pString += 2;
        }
        else
        {
            sum = UINT64_C(10) * sum + static_cast<uint64_t>(c0-'0');
            break;
        }
    } while (mux_isdigit(c0 = pString[0]));

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        // 0u - sum rather than -sum: negating INT64_MIN is undefined, and
        // this is the form the sanitizer message itself recommends.
        //
        sum = UINT64_C(0) - sum;
    }

    // Converting an out-of-range unsigned value is implementation-defined
    // before C++20 rather than undefined, and every compiler this builds
    // with does the two's-complement thing -- which is exactly the value the
    // old signed overflow produced in practice.
    //
    return static_cast<int64_t>(sum);
}

int64_t mux_atoi64(const UTF8 *pString)
{
    // Accumulate unsigned.  The digits come from softcode, so a string past
    // INT64_MAX overflowed a signed accumulator -- undefined behaviour, not
    // merely wrapping, and the compiler is entitled to assume it cannot
    // happen (#1455).  Unsigned arithmetic wraps by definition and produces
    // the same bits, so this removes the UB without changing any answer.
    //
    // Deliberately NOT a range check: what this should return for an
    // out-of-range string is a behaviour question softcode may depend on,
    // and settling it belongs with #1402's conversion work rather than here.
    //
    uint64_t sum = 0;
    int LeadingCharacter = 0;

    // Convert ASCII digits
    //
    unsigned int c1;
    unsigned int c0 = pString[0];
    if (!mux_isdigit(c0))
    {
        while (mux_isspace(pString[0]))
        {
            pString++;
        }
        LeadingCharacter = pString[0];
        if (  LeadingCharacter == '-'
           || LeadingCharacter == '+')
        {
            pString++;
        }
        c0 = pString[0];
        if (!mux_isdigit(c0))
        {
            return 0;
        }
    }

    do
    {
        c1 = pString[1];
        if (mux_isdigit(c1))
        {
            sum = 100 * sum + TableATOI[c0-'0'][c1-'0'];
            pString += 2;
        }
        else
        {
            sum = 10 * sum + (c0-'0');
            break;
        }
    } while (mux_isdigit(c0 = pString[0]));

    // Interpret sign
    //
    if (LeadingCharacter == '-')
    {
        sum = -sum;
    }
    return sum;
}

bool is_integer(const UTF8 *str, int *pDigits)
{
    LBUF_OFFSET i = 0;
    int nDigits = 0;
    if (pDigits)
    {
        *pDigits = 0;
    }

    // Leading spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // Leading minus or plus
    //
    if (str[i] == '-' || str[i] == '+')
    {
        i++;

        // Just a sign by itself isn't an integer.
        //
        if (!str[i])
        {
            return false;
        }
    }

    // Need at least 1 integer
    //
    if (!mux_isdigit(str[i]))
    {
        return false;
    }

    // The number (int)
    //
    do
    {
        i++;
        nDigits++;
    } while (mux_isdigit(str[i]));

    if (pDigits)
    {
        *pDigits = nDigits;
    }

    // Trailing Spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    return (!str[i]);
}

bool is_rational(const UTF8 *str)
{
    LBUF_OFFSET i = 0;

    // Leading spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // Leading minus or plus sign.
    //
    if (str[i] == '-' || str[i] == '+')
    {
        i++;

        // But not if just a sign.
        //
        if (!str[i])
        {
            return false;
        }
    }

    // Need at least one digit.
    //
    bool got_one = false;
    if (mux_isdigit(str[i]))
    {
        got_one = true;
    }

    // The number (int)
    //
    while (mux_isdigit(str[i]))
    {
        i++;
    }

    // Decimal point.
    //
    if (str[i] == '.')
    {
        i++;
    }

    // Need at least one digit
    //
    if (mux_isdigit(str[i]))
    {
        got_one = true;
    }

    if (!got_one)
    {
        return false;
    }

    // The number (fract)
    //
    while (mux_isdigit(str[i]))
    {
        i++;
    }

    // Trailing spaces.
    //
    while (mux_isspace(str[i]))
    {
        i++;
    }

    // There must be nothing else after the trailing spaces.
    //
    return (!str[i]);
}

bool is_real(const UTF8 *str)
{
    PARSE_FLOAT_RESULT pfr;
    return ParseFloat(&pfr, str);
}
