// htab.h - Structures and declarations needed for table hashing.
//
// $Id: htab.h,v 1.3 2001-11-19 19:37:28 sdennis Exp $
//

#include "copyright.h"

#ifndef __HTAB_H
#define __HTAB_H

#include "db.h"
#include "svdhash.h"

typedef struct name_table NAMETAB;
struct name_table
{
    char    *name;
    int minlen;
    int perm;
    int flag;
};

/* BQUE - Command queue */

typedef struct bque BQUE;
struct bque
{
    CLinearTimeAbsolute waittime;   // time to run command
    dbref   player;                 // player who will do command
    dbref   cause;                  // player causing command (for %N)
    dbref   sem;                    // blocking semaphore
    int     attr;                   // blocking attribute
    int     nargs;                  // How many args I have
    char    *text;                  // buffer for comm, env, and scr text
    char    *comm;                  // command
    char    *env[NUM_ENV_VARS];     // environment vars
    char    *scr[MAX_GLOBAL_REGS];  // temp vars
    BOOL    IsTimed;                // Is there a waittime time on this entry?
};

extern void hashreset(CHashTable *);
extern int  *hashfindLEN(void *, int, CHashTable *);
extern int  hashaddLEN(void *, int, int *, CHashTable *);
extern void hashdeleteLEN(void *, int, CHashTable *);
extern void hashflush(CHashTable *);
extern int  hashreplLEN(void *, int, int *, CHashTable *);
extern void hashreplall(int *, int *, CHashTable *);
extern char *hashinfo(const char *, CHashTable *);
extern int  *hash_nextentry(CHashTable *htab);
extern int  *hash_firstentry(CHashTable *htab);
extern char *hash_firstkey(CHashTable *htab, int *);
extern char *hash_nextkey(CHashTable *htab, int *);

extern NAMETAB powers_nametab[];

extern int  search_nametab(dbref, NAMETAB *, char *);
extern NAMETAB  *find_nametab_ent(dbref, NAMETAB *, char *);
extern void display_nametab(dbref, NAMETAB *, char *, int);
extern void interp_nametab(dbref, NAMETAB *, int, char *, char *, char *);
extern void listset_nametab(dbref, NAMETAB *, int, char *, int);

#endif
