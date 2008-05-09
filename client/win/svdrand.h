/*! \file svdrand.h
 * \brief Random Numbers.
 *
 * $Id: svdrand.h 3048 2007-12-28 01:08:40Z brazilofmux $
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
