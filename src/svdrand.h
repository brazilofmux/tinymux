// svdrand.h -- Random Numbers
//
// $Id: svdrand.h,v 1.5 2001-02-07 05:28:15 sdennis Exp $
//
// Random Numbers based on algorithms presented in "Numerical Recipes in C",
// Cambridge Press, 1992.
// 
// RandomINT32() was derived from existing game server code.
//
// MUX 2.1
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#ifndef SVDRAND_H
#define SVDRAND_H

void SeedRandomNumberGenerator(void);
double RandomFloat(double flLow, double flHigh);
INT32 RandomINT32(INT32 lLow, INT32 lHigh);

#endif // SVDRAND_H
