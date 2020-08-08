/*! \file mathutil.h
 * \brief Declarations for math-related helper functions.
 *
 */

#include "copyright.h"

#ifndef __MATHUTIL_H
#define __MATHUTIL_H

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

int mux_fpclass(double result);
bool ParseFloat(__out PARSE_FLOAT_RESULT *pfr, __in_z const UTF8 *str, bool bStrict = true);

double AddDoubles(__in int n, __in_ecount(n) double pd[]);
void fval(UTF8 *buff, UTF8 **bufc, double result);
double NearestPretty(double R);

size_t mux_utox(unsigned long uval, UTF8 *buf, bool bUpperCase);
size_t mux_ui64tox(UINT64 uval, UTF8 *buf, bool bUpperCase);
size_t mux_utoa(unsigned long uval, UTF8 *buf);
size_t mux_ui64toa(UINT64 uval, UTF8 *buf);
size_t mux_ltoa(long val, __out UTF8 *buf);
size_t mux_i64toa(INT64 val, __out UTF8 *buf);
double mux_atof(__in_z const UTF8 *szString, bool bStrict = true);

UTF8 *mux_ltoa_t(long val);
UTF8 *mux_i64toa_t(INT64 val);
UTF8 *mux_ftoa(double r, bool bRounded, int frac);

long mux_atol(__in const UTF8 *pString);
INT64 mux_atoi64(__in const UTF8 *pString);

void safe_ltoa(long val, __inout UTF8 *buff, __deref_inout UTF8 **bufc);
void safe_i64toa(INT64 val, __inout UTF8 *buff, __deref_inout UTF8 **bufc);

bool is_integer(__in_z const UTF8 *str, __out_opt int *pDigits = nullptr);
bool is_rational(__in_z const UTF8 *str);
bool is_real(__in_z const UTF8 *str);

extern const UTF8 *mux_FPStrings[8];
extern const UTF8 Digits16U[17];
extern const UTF8 Digits16L[17];
extern void safe_hex(UINT8 md[], size_t len, bool bUpper, __in UTF8 *buff, __deref_inout UTF8 **bufc);

#endif
