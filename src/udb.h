/* $Id: udb.h,v 1.3 2000-04-24 23:45:38 sdennis Exp $ */

#ifndef _UDB_H
#define _UDB_H
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

typedef struct Aname
{
    unsigned int    object;
    unsigned int    attrnum;
} Aname;

extern char *cache_get(Aname *nam, int *pLen);
extern int  cache_put(Aname *nam, char *obj, int len);
extern int  cache_check(void);
extern int  cache_init(const char *game_dir_file, const char *game_pag_file);
extern void cache_close(void);
extern void cache_tick(void);
extern int  cache_sync(void);
extern void cache_del(Aname *nam);

#endif
