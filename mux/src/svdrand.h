// svdrand.h -- Random Numbers.
//
// $Id: svdrand.h,v 1.3 2003-02-05 06:20:59 jake Exp $
//
// MUX 2.3
// Copyright (C) 1998 through 2003 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
// Random Numbers based on algorithms presented in "Numerical Recipes in C",
// Cambridge Press, 1992.
//
#ifndef SVDRAND_H
#define SVDRAND_H

void SeedRandomNumberGenerator(void);
double RandomFloat(double flLow, double flHigh);
INT32 RandomINT32(INT32 lLow, INT32 lHigh);

#ifdef WIN32
extern bool bCryptoAPI;
#endif

#endif // SVDRAND_H
