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
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * @(#)crypt.h	1.5 12/20/96
 *
 */

#ifndef _CRYPT_H
#define _CRYPT_H	1

/* Encrypt at most 8 characters from KEY using salt to perturb DES.  */
extern char *crypt(const char *__key, const char *__salt);

/* Setup DES tables according KEY.  */
extern void setkey(const char *__key);

/* Encrypt data in BLOCK in place if EDFLAG is zero; otherwise decrypt
   block in place.  */
extern void encrypt (char *__block, int __edflag);

/* Reentrant versions of the functions above.  The additional argument
   points to a structure where the results are placed in.  */
struct crypt_data
  {
    char keysched[16 * 8];
    char sb0[32768];
    char sb1[32768];
    char sb2[32768];
    char sb3[32768];
    /* end-of-aligment-critical-data */
    char crypt_3_buf[14];
    char current_salt[2];
    long int current_saltbits;
    int  direction, initialized;
  };

extern char *__crypt_r(const char *__key, const char *__salt, struct crypt_data *__data);
extern char *crypt_r(const char *__key, const char *__salt, struct crypt_data *__data);
extern void __setkey_r(const char *__key, struct crypt_data *__data);
extern void setkey_r(const char *__key, struct crypt_data *__data);
extern void __encrypt_r(char *__block, int __edflag, struct crypt_data *__data);
extern void encrypt_r(char *__block, int __edflag, struct crypt_data *__data);

#endif	/* crypt.h */
