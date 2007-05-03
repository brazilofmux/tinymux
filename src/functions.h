/* functions.h - declarations for functions & function processing */
/* $Id: functions.h,v 1.2 1997/04/16 06:01:05 dpassmor Exp $ */

#include "copyright.h"

#ifndef __FUNCTIONS_H
#define __FUNCTIONS_H

typedef struct fun {
	const char *name;	/* function name */
	void	(*fun)();	/* handler */
	int	nargs;		/* Number of args needed or expected */
	int	flags;		/* Function flags */
	int	perms;		/* Access to function */
} FUN;

typedef struct ufun {
	const char *name;	/* function name */
	dbref	obj;		/* Object ID */
	int	atr;		/* Attribute ID */
	int	flags;		/* Function flags */
	int	perms;		/* Access to function */
	struct ufun *next;	/* Next ufun in chain */
} UFUN;

#define	FN_VARARGS	1	/* Function allows a variable # of args */
#define	FN_NO_EVAL	2	/* Don't evaluate args to function */
#define	FN_PRIV		4	/* Perform user-def function as holding obj */
#define FN_PRES		8	/* Preseve r-regs before user-def functions */

extern void	NDECL(init_functab);
extern void	FDECL(list_functable, (dbref));

#endif
