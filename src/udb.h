/* $Id: udb.h,v 1.4 1997/06/18 02:39:56 dpassmor Exp $ */

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

/* For MUX, an int works great as an object ID
 * And attributes are zero terminated strings, so we heave the size out.
 * We hand around attribute identifiers in the last things.
 */

typedef	char		Attr;

typedef	unsigned int	Objname;
#define ATTR_SIZE(a)	(strlen((a)) + 1)

typedef struct Aname {
	unsigned int	object;
	unsigned int	attrnum;
} Aname;
  
/* In general, we want binary attributes, so we do this. */

typedef struct Attrib {
	int	attrnum;	/* MUX specific identifier */
	int	size;
	char	*data;
} Attrib;

/* An object is a name, an attribute count, and a vector of attributes */
/* which Attr's are stowed in a contiguous array pointed at by atrs.   */

typedef struct Obj {
	Objname	name;
	int	at_count;
	Attrib	*atrs;
} Obj;

#define	ONULL	((Obj *)0)
#define ANULL	((Attr *)0)
#define ATNULL	((Attrib *)0)
#define NNULL	((Aname *)0)
#define CNULL	((Cache *)0)

extern Attr *cache_get();
extern int cache_put();
extern int cache_check();
extern int cache_init();
extern void cache_reset();
extern int cache_sync();
extern void cache_del();

extern Obj *	FDECL(objfromFILE, (char *));
extern int	FDECL(objtoFILE, (Obj *, char *));

#endif
