/*! \file db.h
 * \brief Attribute interface, some flatfile and object declarations.
 *
 */

#ifndef __DB_H
#define __DB_H

#ifndef MEMORY_BASED
#define SYNC                    cache_sync()
#define CLOSE                   cache_close()
#else // !MEMORY_BASED
#define SYNC
#define CLOSE
#endif // !MEMORY_BASED

#include "attrcache.h"
#include "flags.h"
#include "timeutil.h"

#define ITER_PARENTS(t,p,l) for ((l)=0, (p)=(t); \
                     (Good_obj(p) && \
                      ((l) < mudconf.parent_nest_lim)); \
                     (p)=Parent(p), (l)++)

int get_atr(const UTF8 *name);

typedef struct attr ATTR;
struct attr
{
    const UTF8 *name;   // This has to be first.  braindeath.
    int number;         // attr number
    int flags;
};

#ifdef MEMORY_BASED
typedef struct atrlist ATRLIST;
struct atrlist
{
    UTF8 *data;     /* Attribute text. */
    int size;       /* Length of attribute */
    int number;     /* Attribute number. */
};
#endif // MEMORY_BASED

UTF8 *MakeCanonicalAttributeName(const UTF8 *pName, size_t *pnName, bool *pbValid);
UTF8 *MakeCanonicalAttributeCommand(const UTF8 *pName, size_t *pnName, bool *pbValid);

typedef struct stack MUX_STACK;
struct stack
{
    UTF8      *data;
    MUX_STACK *next;
};

extern ATTR *atr_num(int anum);
extern ATTR *atr_str(const UTF8 *s);

extern ATTR AttrTable[];

extern ATTR **anum_table;
#define anum_get(x) (anum_table[(x)])
#define anum_set(x,v)   anum_table[(x)] = v
extern void anum_extend(int);

#define ATR_INFO_CHAR   '\1'    /* Leadin char for attr control data */

/* Boolean expressions, for locks */
#define BOOLEXP_AND     0
#define BOOLEXP_OR      1
#define BOOLEXP_NOT     2
#define BOOLEXP_CONST   3
#define BOOLEXP_ATR     4
#define BOOLEXP_INDIR   5
#define BOOLEXP_CARRY   6
#define BOOLEXP_IS      7
#define BOOLEXP_OWNER   8
#define BOOLEXP_EVAL    9

typedef struct boolexp BOOLEXP;
struct boolexp
{
  boolexp_type type;
  struct boolexp *sub1;
  struct boolexp *sub2;
  dbref thing;          /* thing refers to an object */
};

#define TRUE_BOOLEXP ((BOOLEXP *) 0)

/* Database format information */

#define F_UNKNOWN   0   /* Unknown database format */
#define F_MUX       5   /* MUX format */

#define V_MASK      0x000000ff  /* Database version */
#define V_ZONE      0x00000100  /* ZONE/DOMAIN field */
#define V_LINK      0x00000200  /* LINK field (exits from objs) */
#define V_DATABASE  0x00000400  /* attrs in a separate database */
#define V_ATRNAME   0x00000800  /* NAME is an attr, not in the hdr */
#define V_ATRKEY    0x00001000  /* KEY is an attr, not in the hdr */
#define V_PARENT    0x00002000  /* db has the PARENT field */
#define V_ATRMONEY  0x00008000  /* Money is kept in an attribute */
#define V_XFLAGS    0x00010000  /* An extra word of flags */
#define V_POWERS    0x00020000  /* Powers? */
#define V_3FLAGS    0x00040000  /* Adding a 3rd flag word */
#define V_QUOTED    0x00080000  /* Quoted strings, ala PennMUSH */

/* Some defines for DarkZone's flavor of PennMUSH */
#define DB_CHANNELS    0x2    /*  Channel system */
#define DB_SLOCK       0x4    /*  Slock */
#define DB_MC          0x8    /*  Master Create Time + modifed */
#define DB_MPAR        0x10   /*  Multiple Parent Code */
#define DB_CLASS       0x20   /*  Class System */
#define DB_RANK        0x40   /*  Rank */
#define DB_DROPLOCK    0x80   /*  Drop/TelOut Lock */
#define DB_GIVELOCK    0x100  /*  Give/TelIn Lock */
#define DB_GETLOCK     0x200  /*  Get Lock */
#define DB_THREEPOW    0x400  /*  Powers have Three Long Words */

/* special dbref's */
#define NOTHING     (-1)    /* null dbref */
#define AMBIGUOUS   (-2)    /* multiple possibilities, for matchers */
#define HOME        (-3)    /* virtual room, represents mover's home */
#define NOPERM      (-4)    /* Error status, no permission */
extern const UTF8 *aszSpecialDBRefNames[1-NOPERM];

typedef struct object OBJ;
struct object
{
    dbref   location;   /* PLAYER, THING: where it is */
                        /* ROOM: dropto: */
                        /* EXIT: where it goes to */
    dbref   contents;   /* PLAYER, THING, ROOM: head of contentslist */
                        /* EXIT: unused */
    dbref   exits;      /* PLAYER, THING, ROOM: head of exitslist */
                        /* EXIT: where it is */
    dbref   next;       /* PLAYER, THING: next in contentslist */
                        /* EXIT: next in exitslist */
                        /* ROOM: unused */
    dbref   link;       /* PLAYER, THING: home location */
                        /* ROOM, EXIT: unused */
    dbref   parent;     /* ALL: defaults for attrs, exits, $cmds, */
    dbref   owner;      /* PLAYER: domain number + class + moreflags */
                        /* THING, ROOM, EXIT: owning player number */

    dbref   zone;       /* Whatever the object is zoned to.*/

    FLAGSET fs;         // ALL: Flags set on the object.

    POWER   powers;     /* ALL: Powers on object */
    POWER   powers2;    /* ALL: even more powers */

#ifdef DEPRECATED
    MUX_STACK   *stackhead; /* Every object has a stack. */
#endif // DEPRECATED

    CLinearTimeDelta cpu_time_used; /* ALL: CPU time eaten */

    // ALL: When to refurbish throttled counters.
    //
    CLinearTimeAbsolute tThrottleExpired;
    int     throttled_attributes;
    int     throttled_mail;
    int     throttled_references;

    UTF8    *purename;
    UTF8    *moniker;

#ifdef MEMORY_BASED
    ATRLIST *pALHead;   /* The head of the attribute list.       */
    int      nALAlloc;  /* Size of the allocated attribute list. */
    int      nALUsed;   /* Used portion of the attribute list.   */
#else
    UTF8    *name;
#endif // MEMORY_BASED
};

const int INITIAL_ATRLIST_SIZE = 10;

extern OBJ *db;

#define Location(t)     db[t].location

#define Zone(t)         db[t].zone

#define Contents(t)     db[t].contents
#define Exits(t)        db[t].exits
#define Next(t)         db[t].next
#define Link(t)         db[t].link
#define Owner(t)        db[t].owner
#define Parent(t)       db[t].parent
#define Flags(t)        db[t].fs.word[FLAG_WORD1]
#define Flags2(t)       db[t].fs.word[FLAG_WORD2]
#define Flags3(t)       db[t].fs.word[FLAG_WORD3]
#define Powers(t)       db[t].powers
#define Powers2(t)      db[t].powers2
#define Home(t)         Link(t)
#define Dropto(t)       Location(t)
#define ThAttrib(t)     db[t].throttled_attributes
#define ThMail(t)       db[t].throttled_mail
#define ThRefs(t)       db[t].throttled_references

#define s_Location(t,n)     db[t].location = (n)

#define s_Zone(t,n)         db[t].zone = (n)

#define s_Contents(t,n)     db[t].contents = (n)
#define s_Exits(t,n)        db[t].exits = (n)
#define s_Next(t,n)         db[t].next = (n)
#define s_Link(t,n)         db[t].link = (n)
#define s_Owner(t,n)        db[t].owner = (n)
#define s_Parent(t,n)       db[t].parent = (n)
#define s_Flags(t,f,n)      db[t].fs.word[f] = (n)
#define s_Powers(t,n)       db[t].powers = (n)
#define s_Powers2(t,n)      db[t].powers2 = (n)
#define s_Home(t,n)         s_Link(t,n)
#define s_Dropto(t,n)       s_Location(t,n)
#define s_ThAttrib(t,n)     db[t].throttled_attributes = (n);
#define s_ThMail(t,n)       db[t].throttled_mail = (n);
#define s_ThRefs(t,n)       db[t].throttled_references = (n);

#ifdef DEPRECATED
#define Stack(t)            db[t].stackhead
#define s_Stack(t,n)        db[t].stackhead = (n)
#endif // DEPRECATED

int  Pennies(dbref obj);
void s_Pennies(dbref obj, int howfew);
void s_PenniesDirect(dbref obj, int howfew);

#if defined(HAVE_WORKING_FORK)
void load_restart_db(void);
#endif // HAVE_WORKING_FORK

dbref    getref(FILE *);
void putref(FILE *, dbref);
void free_boolexp(BOOLEXP *);
dbref    parse_dbref(const UTF8 *);
bool ThrottleMail(dbref executor);
bool ThrottleAttributeNames(dbref executor);
bool ThrottleReferences(dbref executor);
bool ThrottlePlayerCreate(void);
int  mkattr(dbref executor, const UTF8 *);
void al_store(void);
void db_grow(dbref);
void db_free(void);
void db_make_minimal(void);
dbref    db_read(FILE *, int *, int *, int *);
dbref    db_write(FILE *, int, int);
void destroy_thing(dbref);
void destroy_exit(dbref);
void putstring(FILE *f, const UTF8 *s);
void *getstring_noalloc(FILE *f, bool new_strings, size_t *pnBuffer);
void init_attrtab(void);
int GrowFiftyPercent(int x, int low, int high);

#define DOLIST(thing,list) \
    for ((thing)=(list); \
         ((thing)!=NOTHING) && (Next(thing)!=(thing)); \
         (thing)=Next(thing))
#define SAFE_DOLIST(thing,next,list) \
    for ((thing)=(list),(next)=((thing)==NOTHING ? NOTHING: Next(thing)); \
         (thing)!=NOTHING && (Next(thing)!=(thing)); \
         (thing)=(next), (next)=Next(next))
#define DO_WHOLE_DB(thing) \
    for ((thing)=0; (thing)<mudstate.db_top; (thing)++)
#define DO_WHOLE_DB_BACKWARDS(thing) \
    for ((thing)=mudstate.db_top-1; (thing)>=0; (thing)--)

class attr_info
{
public:
    dbref   m_object;       // Object on which the attribute is stored
    ATTR   *m_attr;         // Attribute this is an instance of
    bool    m_bValid;       // Is this a valid object / attribute pair?

    dbref   m_aowner;       // Attribute owner
    int     m_aflags;       // Attribute flags
    bool    m_bHaveInfo;    // Have we retrieved aowner and aflags yet?

    attr_info(void);
    attr_info(dbref object, ATTR *attr);
    attr_info(dbref executor, const UTF8 *pTarget, bool bCreate = false, bool bDefaultMe = true);
    bool get_info(bool bParent);
};

#endif // !__DB_H
