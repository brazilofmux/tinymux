/*! \file svdrand.h
 * \brief Random Numbers.
 *
 * Random Numbers based on algorithms presented in "Numerical Recipes in C",
 * Cambridge Press, 1992.
 */

#ifndef SVDRAND_H
#define SVDRAND_H

void SeedRandomNumberGenerator(void);
double RandomFloat(double flLow, double flHigh);
INT32 RandomINT32(INT32 lLow, INT32 lHigh);

#if defined(WINDOWS_CRYPT)
extern bool bCryptoAPI;
#endif // WINDOWS_CRYPT

#endif // SVDRAND_H
