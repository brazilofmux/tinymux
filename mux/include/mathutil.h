/*! \file mathutil.h
 * \brief Declarations for math-related helper functions.
 *
 */

#include "copyright.h"

#ifndef MATHUTIL_H
#define MATHUTIL_H

#define MUX_FPGROUP_PASS  0x00 // Pass-through to printf
#define MUX_FPGROUP_ZERO  0x10 // Force to be zero.
#define MUX_FPGROUP_PINF  0x20 // "+Inf"
#define MUX_FPGROUP_NINF  0x30 // "-Inf"
#define MUX_FPGROUP_IND   0x40 // "Ind"
#define MUX_FPGROUP_NAN   0x50 // "NaN"
#define MUX_FPGROUP(x) ((x) & 0xF0)

// mux_fpclass returns an integer that is one of the following:
//
#define MUX_FPCLASS_PINF  (MUX_FPGROUP_PINF|0) // Positive infinity (+INF)
#define MUX_FPCLASS_NINF  (MUX_FPGROUP_NINF|1) // Negative infinity (-INF)
#define MUX_FPCLASS_QNAN  (MUX_FPGROUP_IND |2) // Quiet NAN (Indefinite)
#define MUX_FPCLASS_SNAN  (MUX_FPGROUP_NAN |3) // Signaling NAN
#define MUX_FPCLASS_ND    (MUX_FPGROUP_ZERO|4) // Negative denormalized
#define MUX_FPCLASS_NZ    (MUX_FPGROUP_ZERO|5) // Negative zero (-0)
#define MUX_FPCLASS_PZ    (MUX_FPGROUP_ZERO|6) // Positive zero (+0)
#define MUX_FPCLASS_PD    (MUX_FPGROUP_ZERO|7) // Positive denormalized
#define MUX_FPCLASS_PN    (MUX_FPGROUP_PASS|8) // Positive normalized non-zero
#define MUX_FPCLASS_NN    (MUX_FPGROUP_PASS|9) // Negative normalized non-zero
#define MUX_FPCLASS(x)    ((x) & 0x0F)

typedef struct
{
    int         iLeadingSign;
    int         iString;
    const UTF8  *pDigitsA;
    size_t      nDigitsA;
    const UTF8  *pDigitsB;
    size_t      nDigitsB;
    int         iExponentSign;
    const UTF8  *pDigitsC;
    size_t      nDigitsC;
    const UTF8  *pMeat;
    size_t      nMeat;

} PARSE_FLOAT_RESULT;

LIBMUX_API int mux_fpclass(double result);
LIBMUX_API bool ParseFloat(PARSE_FLOAT_RESULT *pfr, const UTF8 *str, bool bStrict = true);

LIBMUX_API double AddDoubles(int n, double pd[]);
LIBMUX_API void fval(UTF8 *buff, UTF8 **bufc, double result);
LIBMUX_API double NearestPretty(double R);

LIBMUX_API size_t mux_utox(unsigned long uval, UTF8 *buf, bool bUpperCase);
LIBMUX_API size_t mux_ui64tox(uint64_t uval, UTF8 *buf, bool bUpperCase);
LIBMUX_API size_t mux_utoa(unsigned long uval, UTF8 *buf);
LIBMUX_API size_t mux_ui64toa(uint64_t uval, UTF8 *buf);
LIBMUX_API size_t mux_ltoa(long val, UTF8 *buf);
LIBMUX_API size_t mux_i64toa(int64_t val, UTF8 *buf);
LIBMUX_API double mux_atof(const UTF8 *szString, bool bStrict = true);

LIBMUX_API UTF8 *mux_ltoa_t(long val);
LIBMUX_API UTF8 *mux_i64toa_t(int64_t val);
LIBMUX_API UTF8 *mux_ftoa(double r, bool bRounded, int frac);

LIBMUX_API long mux_atol(const UTF8 *pString);
LIBMUX_API int64_t mux_atoi64(const UTF8 *pString);

LIBMUX_API void safe_ltoa(long val, UTF8 *buff, UTF8 **bufc);
LIBMUX_API void safe_i64toa(int64_t val, UTF8 *buff, UTF8 **bufc);

LIBMUX_API bool is_integer(const UTF8 *str, int *pDigits = nullptr);
LIBMUX_API bool is_rational(const UTF8 *str);
LIBMUX_API bool is_real(const UTF8 *str);

extern LIBMUX_API const UTF8 *mux_FPStrings[8];
extern LIBMUX_API const UTF8 Digits16U[17];
extern LIBMUX_API const UTF8 Digits16L[17];
void safe_hex(uint8_t md[], size_t len, bool bUpper, UTF8 *buff, UTF8 **bufc);

// IEEE special-value construction codes (used by MakeSpecialFloat).
//
#define IEEE_MAKE_NAN  1
#define IEEE_MAKE_IND  2
#define IEEE_MAKE_PINF 3
#define IEEE_MAKE_NINF 4

LIBMUX_API double MakeSpecialFloat(int iWhich);

// From strtod.cpp — low-level float-to-string / string-to-float.
//
LIBMUX_API void   mux_FPInit();
LIBMUX_API void   mux_FPSet();
LIBMUX_API void   mux_FPRestore();
LIBMUX_API double mux_strtod(const UTF8 *s00, UTF8 **se);
LIBMUX_API double mux_ulp(double);
LIBMUX_API UTF8  *mux_dtoa(double d, int mode, int ndigits, int *decpt, int *sign, UTF8 **rve);

// Float-to-string precision limit.  Server layer sets this from
// mudconf.float_precision during startup.  Default -1 means unlimited.
//
extern LIBMUX_API int g_float_precision;

#endif // !MATHUTIL_H
