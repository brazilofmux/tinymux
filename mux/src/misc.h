// misc.h -- miscellaneous structures that are needed in more than one file.
//
// $Id: misc.h,v 1.1 2003/01/22 19:58:25 sdennis Exp $
//

#include "copyright.h"

#ifndef _MISC_H
#define _MISC_H

#include "powers.h"

/* Search structure, used by @search and search(). */

typedef struct search_type SEARCH;
struct search_type {
    int s_wizard;
    dbref   s_owner;
    dbref   s_rst_owner;
    int s_rst_type;
    FLAGSET s_fset;
    POWERSET s_pset;
    dbref   s_parent;
    dbref   s_zone;
    char    *s_rst_name;
    char    *s_rst_eval;
    int low_bound;
    int high_bound;
};

/* Stats structure, used by @stats and stats(). */

typedef struct stats_type STATS;
struct stats_type {
    int s_total;
    int s_rooms;
    int s_exits;
    int s_things;
    int s_players;
    int s_garbage;
};

extern BOOL search_setup(dbref, char *, SEARCH *);
extern void search_perform(dbref executor, dbref caller, dbref enactor, SEARCH *);
extern BOOL get_stats(dbref, dbref, STATS *);

#endif // !_MISC_H
