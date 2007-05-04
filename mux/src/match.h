// match.h
//
// $Id: match.h,v 1.1 2002-05-24 06:53:15 sdennis Exp $
//

#include "copyright.h"

#ifndef M_MATCH_H
#define M_MATCH_H

#include "db.h"

typedef struct match_state MSTATE;
struct match_state {
    int confidence;     /* How confident are we?  CON_xx */
    int count;          /* # of matches at this confidence */
    int pref_type;      /* The preferred object type */
    int check_keys;     /* Should we test locks? */
    dbref   absolute_form;      /* If #num, then the number */
    dbref   match;          /* What I've found so far */
    dbref   player;         /* Who is performing match */
    char    *string;        /* The string to search for */
};

/* Match functions
 * Usage:
 *  init_match(player, name, type);
 *  match_this();
 *  match_that();
 *  ...
 *  thing = match_result()
 */

extern void FDECL(init_match, (dbref, const char *, int));
extern void FDECL(init_match_check_keys, (dbref, const char *, int));
extern void NDECL(match_player);
extern void NDECL(match_absolute);
extern void NDECL(match_numeric);
extern void NDECL(match_me);
extern void NDECL(match_here);
extern void NDECL(match_home);
extern void NDECL(match_possession);
extern void NDECL(match_neighbor);
extern void NDECL(match_exit);
extern void NDECL(match_exit_with_parents);
extern void NDECL(match_carried_exit);
extern void NDECL(match_carried_exit_with_parents);
extern void NDECL(match_master_exit);
extern void FDECL(match_everything, (int));
extern dbref    NDECL(match_result);
extern dbref    NDECL(last_match_result);
extern dbref    FDECL(match_status, (dbref, dbref));
extern dbref    NDECL(noisy_match_result);
extern dbref    FDECL(dispatched_match_result, (dbref));
extern int  NDECL(matched_locally);
extern void FDECL(save_match_state, (MSTATE *));
extern void FDECL(restore_match_state, (MSTATE *));
extern void match_zone_exit(void);
extern dbref match_thing(dbref player, char *name);
extern dbref match_thing_quiet(dbref player, char *name);

#define MAT_NO_EXITS        1   /* Don't check for exits */
#define MAT_EXIT_PARENTS    2   /* Check for exits in parents */
#define MAT_NUMERIC     4   /* Check for un-#ified dbrefs */
#define MAT_HOME        8   /* Check for 'home' */

#endif
