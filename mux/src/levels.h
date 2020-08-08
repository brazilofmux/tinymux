/*! \file levels.h
 * \brief Reality levels
 *
 */

#include "copyright.h"

#ifndef __LEVELS_H
#define __LEVELS_H

#include "htab.h"
#include "db.h"

#define IsReal(R, T) ((R) == (T) || (RxLevel(R) & TxLevel(T)))

RLEVEL   RxLevel(dbref);
RLEVEL   TxLevel(dbref);
void     notify_except_rlevel(dbref, dbref, dbref, const UTF8 *, int);
void     notify_except2_rlevel(dbref, dbref, dbref, dbref, const UTF8 *);
void     notify_except2_rlevel2(dbref, dbref, dbref, dbref, const UTF8 *);
UTF8 *   rxlevel_description(dbref, dbref);
UTF8 *   txlevel_description(dbref, dbref);
void     decompile_rlevels(dbref, dbref, UTF8 *);
RLEVEL   find_rlevel(UTF8 *);

UTF8 *get_rlevel_desc
(
    dbref player,
    dbref thing,
    int  *piDescUsed
);

void did_it_rlevel
(
    dbref player,
    dbref thing,
    int   what,
    const UTF8 *def,
    int   owhat,
    const UTF8 *odef,
    int   awhat,
    int   ctrl_flags,
    const UTF8 *args[],
    int   nargs
);

#endif // __LEVELS_H
