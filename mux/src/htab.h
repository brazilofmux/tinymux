// htab.h - Structures and declarations needed for table hashing.
//
// $Id: htab.h,v 1.2 2003-02-05 06:20:59 jake Exp $
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
extern int  *hashfindLEN(const void *, int, CHashTable *);
extern int  hashaddLEN(const void *, int, int *, CHashTable *);
extern void hashdeleteLEN(const void *, int, CHashTable *);
extern void hashflush(CHashTable *);
extern bool hashreplLEN(const void *, int, int *, CHashTable *);
extern void hashreplall(int *, int *, CHashTable *);
extern int  *hash_nextentry(CHashTable *htab);
extern int  *hash_firstentry(CHashTable *htab);
extern int *hash_firstkey(CHashTable *htab, int *, char **);
extern int *hash_nextkey(CHashTable *htab, int *, char **);

extern NAMETAB powers_nametab[];

extern int  search_nametab(dbref, NAMETAB *, char *);
extern NAMETAB  *find_nametab_ent(dbref, NAMETAB *, char *);
extern void display_nametab(dbref, NAMETAB *, char *, bool);
extern void interp_nametab(dbref, NAMETAB *, int, const char *, const char *, const char *);
extern void listset_nametab(dbref, NAMETAB *, int, char *, bool);

#endif // !__HTAB_H
