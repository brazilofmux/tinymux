// htab.h - Structures and declarations needed for table hashing.
//
// $Id: htab.h,v 1.5 2004-07-07 17:00:47 sdennis Exp $
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
    dbref   executor;               // executor who will do command
    dbref   caller;                 // caller.
    dbref   enactor;                // enactor causing command (for %N)
    dbref   sem;                    // blocking semaphore
    int     attr;                   // blocking attribute
    int     nargs;                  // How many args I have
    char    *text;                  // buffer for comm, env, and scr text
    char    *comm;                  // command
    char    *env[NUM_ENV_VARS];     // environment vars
    char    *scr[MAX_GLOBAL_REGS];  // temp vars
    bool    IsTimed;                // Is there a waittime time on this entry?
};

extern void hashreset(CHashTable *);
extern void *hashfindLEN(const void *Str, size_t nStr, CHashTable *htab);
extern int  hashaddLEN(const void *, int, void *, CHashTable *);
extern void hashdeleteLEN(const void *, int, CHashTable *);
extern void hashflush(CHashTable *);
extern bool hashreplLEN(const void *, int, void *, CHashTable *);
extern void hashreplall(const void *, void *, CHashTable *);
extern void *hash_nextentry(CHashTable *htab);
extern void *hash_firstentry(CHashTable *htab);
extern void *hash_firstkey(CHashTable *htab, int *, char **);
extern void *hash_nextkey(CHashTable *htab, int *, char **);

extern NAMETAB powers_nametab[];

extern bool search_nametab(dbref, NAMETAB *, char *, int *);
extern NAMETAB *find_nametab_ent(dbref, NAMETAB *, char *);
extern void display_nametab(dbref, NAMETAB *, char *, bool);
extern void interp_nametab(dbref, NAMETAB *, int, const char *, const char *, const char *);
extern void listset_nametab(dbref, NAMETAB *, int, char *, bool);

#endif // !__HTAB_H
