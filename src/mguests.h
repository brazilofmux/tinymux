/* mguests.h */
/* $Id: mguests.h,v 1.2 1997/04/16 06:01:22 dpassmor Exp $ */

#ifndef  __MGUESTS_H
#define __MGUESTS_H

#include "copyright.h"
#include "autoconf.h"
#include "interface.h"

extern char *	FDECL(make_guest, (DESC *));
extern dbref	FDECL(create_guest, (char *, char *));
extern void	FDECL(destroy_guest, (dbref));
#endif
