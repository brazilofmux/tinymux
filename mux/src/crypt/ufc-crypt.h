/*
 * UFC-crypt: ultra fast crypt(3) implementation
 *
 * Copyright (C) 1991, 1992, 1993, 1996, 1997 Free Software Foundation, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * @(#)ufc-crypt.h  1.27 12/20/96
 *
 * Definitions of datatypes
 *
 */

/*
 * Requirements for datatypes:
 *
 * A datatype 'ufc_long' of at least 32 bit
 * *and*
 *   A type 'long32' of exactly 32 bits (_UFC_32_)
 *   *or*
 *   A type 'long64' of exactly 64 bits (_UFC_64_)
 *
 * 'int' is assumed to be at least 8 bit
 */

/*
 * #ifdef's for various architectures
 */

#ifdef cray
/* thanks to <hutton@opus.sdsc.edu> (Tom Hutton)  for testing */
typedef unsigned long ufc_long;
typedef unsigned long long64;
#define _UFC_64_
#endif

#if defined convex || defined __convexc__
/* thanks to pcl@convex.oxford.ac.uk (Paul Leyland) for testing */
typedef unsigned long ufc_long;
typedef long long     long64;
#define _UFC_64_
#endif

#ifdef __sgi
#if _MIPS_SZLONG == 64
typedef unsigned long ufc_long;
typedef long     long64;
#define _UFC_64_
#else
typedef unsigned long ufc_long;
typedef int     long32;
#define _UFC_32_
#endif
#endif

/*
 * Thanks to <iglesias@draco.acs.uci.edu> (Mike Iglesias)
 */

#ifdef __alpha
typedef unsigned long ufc_long;
typedef unsigned long long64;
#define _UFC_64_
#endif

#if defined __sparc__ && defined __arch64__
typedef unsigned long ufc_long;
typedef unsigned long long64;
#define _UFC_64_
#endif

/*
 * For debugging 64 bit code etc with 'gcc'
 */

#ifdef GCC3232
typedef unsigned long ufc_long;
typedef unsigned long long32;
#define _UFC_32_
#endif

#ifdef GCC3264
typedef unsigned long ufc_long;
typedef long long     long64;
#define _UFC_64_
#endif

#ifdef GCC6432
typedef long long ufc_long;
typedef unsigned long long32;
#define _UFC_32_
#endif

#ifdef GCC6464
typedef long long     ufc_long;
typedef long long     long64;
#define _UFC_64_
#endif

/*
 * Catch all for 99.95% of all UNIX machines
 */

#ifndef _UFC_64_
#ifndef _UFC_32_
#define _UFC_32_
typedef unsigned long ufc_long;
typedef unsigned long long32;
#endif
#endif
