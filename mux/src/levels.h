/* levels.h - Reality levels */

#include "copyright.h"

#ifndef __LEVELS_H
#define	__LEVELS_H

#include "htab.h"
#include "db.h"

#define IsReal(R, T)	((R) == (T) || (RxLevel(R) & TxLevel(T)))

extern RLEVEL 	RxLevel(dbref);
extern RLEVEL 	TxLevel(dbref);
extern void 	notify_except_rlevel(dbref, dbref, dbref, const char *, int);
extern void 	notify_except2_rlevel(dbref, dbref, dbref, dbref,const char *);
extern void 	notify_except2_rlevel2(dbref, dbref, dbref, dbref,const char *);
extern char *	rxlevel_description(dbref, dbref);
extern char *	txlevel_description(dbref, dbref);
extern void	decompile_rlevels(dbref, dbref, char *);
extern RLEVEL	find_rlevel(char *);
extern void 	did_it_rlevel(dbref, dbref, int, const char *, int,const char *, int, char *[], int);

#endif /* __LEVELS_H */
