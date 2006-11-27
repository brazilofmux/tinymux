/* levels.h - Reality levels */

#include "copyright.h"

#ifndef __LEVELS_H
#define __LEVELS_H

#include "htab.h"
#include "db.h"

#define IsReal(R, T) ((R) == (T) || (RxLevel(R) & TxLevel(T)))

RLEVEL   RxLevel(dbref);
RLEVEL   TxLevel(dbref);
void     notify_except_rlevel(dbref, dbref, dbref, const char *, int);
void     notify_except2_rlevel(dbref, dbref, dbref, dbref,const char *);
void     notify_except2_rlevel2(dbref, dbref, dbref, dbref,const char *);
char *   rxlevel_description(dbref, dbref);
char *   txlevel_description(dbref, dbref);
void     decompile_rlevels(dbref, dbref, char *);
RLEVEL   find_rlevel(char *);
void did_it_rlevel
(
    dbref player,
    dbref thing,
    int   what,
    const char *def,
    int   owhat,
    const char *odef,
    int   awhat,
    int   ctrl_flags,
    char *args[],
    int   nargs
);

#endif // __LEVELS_H
