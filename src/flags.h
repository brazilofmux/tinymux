/* flags.h - object flags */
/* $Id: flags.h,v 1.2 2000-04-13 08:16:43 sdennis Exp $ */

#include "copyright.h"

#ifndef __FLAGS_H
#define __FLAGS_H

#include "htab.h"
#include "db.h"



#define FLAG_WORD2    0x1 /* 2nd word of flags. */
#define FLAG_WORD3    0x2 /* 3rd word of flags. */

/* Object types */
#define TYPE_ROOM     0x0
#define TYPE_THING    0x1
#define TYPE_EXIT     0x2
#define TYPE_PLAYER   0x3
/* Empty */
#define TYPE_GARBAGE  0x5
#define NOTYPE        0x7
#define TYPE_MASK     0x7

/* First word of flags */
#define SEETHRU     0x00000008  /* Can see through to the other side */
#define WIZARD      0x00000010  /* gets automatic control */
#define LINK_OK     0x00000020  /* anybody can link to this room */
#define DARK        0x00000040  /* Don't show contents or presence */
#define JUMP_OK     0x00000080  /* Others may @tel here */
#define STICKY      0x00000100  /* Object goes home when dropped */
#define DESTROY_OK  0x00000200  /* Others may @destroy */
#define HAVEN       0x00000400  /* No killing here, or no pages */
#define QUIET       0x00000800  /* Prevent 'feelgood' messages */
#define HALT        0x00001000  /* object cannot perform actions */
#define TRACE       0x00002000  /* Generate evaluation trace output */
#define GOING       0x00004000  /* object is available for recycling */
#define MONITOR     0x00008000  /* Process ^x:action listens on obj? */
#define MYOPIC      0x00010000  /* See things as nonowner/nonwizard */
#define PUPPET      0x00020000  /* Relays ALL messages to owner */
#define CHOWN_OK    0x00040000  /* Object may be @chowned freely */
#define ENTER_OK    0x00080000  /* Object may be ENTERed */
#define VISUAL      0x00100000  /* Everyone can see properties */
#define IMMORTAL    0x00200000  /* Object can't be killed */
#define HAS_STARTUP 0x00400000  /* Load some attrs at startup */
#define TM_OPAQUE   0x00800000  /* Can't see inside */
#define VERBOSE     0x01000000  /* Tells owner everything it does. */
#define INHERIT     0x02000000  /* Gets owner's privs. (i.e. Wiz) */
#define NOSPOOF     0x04000000  /* Report originator of all actions. */
#define ROBOT       0x08000000  /* Player is a ROBOT */
#define SAFE        0x10000000  /* Need /override to @destroy */
#define ROYALTY     0x20000000  /* Sees like a wiz, but ca't modify */
#define HEARTHRU    0x40000000  /* Can hear out of this obj or exit */
#define TERSE       0x80000000  /* Only show room name on look */

/* Second word of flags */
#define KEY     0x00000001  /* No puppets */
#define ABODE       0x00000002  /* May @set home here */
#define FLOATING    0x00000004  /* Inhibit Floating room.. msgs */
#define UNFINDABLE  0x00000008  /* Cant loc() from afar */
#define PARENT_OK   0x00000010  /* Others may @parent to me */
#define LIGHT       0x00000020  /* Visible in dark places */
#define HAS_LISTEN  0x00000040  /* Internal: LISTEN attr set */
#define HAS_FWDLIST 0x00000080  /* Internal: FORWARDLIST attr set */
#define AUDITORIUM  0x00000100  /* Should we check the SpeechLock? */
#define ANSI            0x00000200 
#define HEAD_FLAG       0x00000400
#define FIXED           0x00000800
#define UNINSPECTED     0x00001000
#define NO_COMMAND      0x00002000
#define DYNAMIC         0x00004000
#define NOBLEED         0x00008000
#define STAFF           0x00010000
#define HAS_DAILY   0x00020000
#define GAGGED      0x00040000

#define VACATION    0x01000000
#define PLAYER_MAILS    0x02000000
#define HTML        0x04000000      /* Player supports HTML */

#define SUSPECT     0x10000000  /* Report some activities to wizards */
#define COMPRESS    0x20000000  /* Output is compressed */
#define CONNECTED   0x40000000  /* Player is connected */
#define SLAVE       0x80000000  /* Disallow most commands */

// Third word of flags
//
#ifdef WOD_REALMS
#define OBF             0x00000001      // Obfuscate Flag
#define HSS             0x00000002      // Auspex/Heightened Senses Flag
#define UMBRA           0x00000004      // Umbra, UMBRADESC
#define SHROUD          0x00000008      // Shroud, WRAITHDESC
#define MATRIX          0x00000010      // Matrix, MATRIXDESC
#define MEDIUM          0x00000020
#define DEAD            0x00000040
#define FAE             0x00000080      // Fae, FAEDESC
#define CHIMERA         0x00000100      // Fae, FAEDESC
#define PEERING         0x00000200      // Mean the a looker is seeing a different realm than they are in.
#endif

/* Flags from prior versions of MUSH */
/* These are from Penn, and Kalkin's DarkZone version of Penn */
#define PENN_COMBAT          0x8     /* SW custom code */
#define PENN_WIZARD     0x10    /* gets automatic control */
#define PENN_LINK_OK        0x20    /* anybody can link to this room */
#define PENN_DARK       0x40    /* contents of room are not printed */
#define PENN_VERBOSE        0x80    /* print out command before executing it */
#define PENN_STICKY     0x100   /* goes home when dropped */
#define PENN_TRANSPARENT     0x200   /* can look through exit to see next room,
                 * or room "long exit display.
                 */
#define PENN_HAVEN      0x400   /* this room disallows kills in it */
                /* on a player, disallow paging, */
#define PENN_QUIET      0x800   /* On an object, will not emit 'set'
                 * messages.. on a player.. will not see ANY
                                 * set messages
                 */
#define PENN_HALT       0x1000  /* object cannot perform actions */
#define PENN_UNFIND          0x2000  /* object cannot be found (or found in */
#define PENN_GOING      0x4000  /* object is available for recycling */

#define PENN_ACCESSED   0x8000  /* object has been accessed recently */
#define PENN_MARKED     0x10000 /* flag used to trace db checking of room
                 * linkages. */
#define PENN_CHOWN_OK   0x40000 /* object can be 'stolen' and made yours */
#define PENN_ENTER_OK   0x80000 /* object basically acts like a room with
                 * only one exit (leave), on players 
                 * means that items can be given freely, AND
                 * taken from!
                 */
#define PENN_VISUAL     0x100000    /* People other than owners can see 
                     * property list of object.
                     */
#define PENN_ROYALTY        0x400000    /* can ex, and @tel like a wizard */
#define PENN_OPAQUE     0x800000    /* Objects inside object will not be
                     * seen on a look.
                     */
#define PENN_INHERIT    0x1000000       /* Inherit objects can force their
                                     * owners. Inherit players have all
                     * objects inherit.
                     */
#define PENN_DEBUGGING  0x2000000   /* returns parser evals */
#define PENN_SAFE       0x4000000   /* cannot be destroyed */
#define PENN_STARTUP    0x8000000   /* These objects have a @startup
                     * triggered when the MUSH restarts.                        */
#define PENN_AUDIBLE    0x10000000      /* rooms are flagged as having emitter 
                     * exits. exits act like emitters, 
                     * sound propagates to the exit dest. 
                     */
#define PENN_NO_COMMAND      0x20000000 /* don't check for $commands */
/* Penn Toggles */

#define PENN_PLAYER_TERSE    0x8        /* suppress autolook messages */
#define PENN_PLAYER_MYOPIC   0x10       /* look at everything as if player
                     * doesn't control it.
                     */
#define PENN_PLAYER_NOSPOOF  0x20       /* sees origin of emits */
#define PENN_PLAYER_SUSPECT  0x40       /* notifies of a player's name changes,
                     * (dis)connects, and possible logs
                     * logs commands.
                     */
#define PENN_PLAYER_GAGGED   0x80       /* can only move */
#define PENN_PLAYER_MONITOR  0x100      /* sees (dis)connects broadcasted */
#define PENN_PLAYER_CONNECT  0x200      /* connected to game */
#define PENN_PLAYER_ANSI     0x400      /* enable sending of ansi control
                     * sequences (for examine).
                     */
#define PENN_PLAYER_ZONE     0x800           /* Zone Master (zone control owner) */

#define PENN_PLAYER_HEAD     0x1000
#define PENN_PLAYER_JUDGE    0x2000          /* Judge flag */
#define PENN_PLAYER_FIXED    0x4000             /* Fixed flag */
#define PENN_PLAYER_ADMIN    0x8000
#define PENN_PLAYER_VACATION 0x10000            /* Vacation Flag */         
#define PENN_PLAYER_MORTAL   0x20000
#define PENN_PLAYER_SLAVE    0x40000
#define PENN_PLAYER_COLOR    0x80000
#define PENN_PLAYER_WEIRDANSI 0x100000   /* resets each line to normal */
#define PENN_COMBAT_FLAG    0x200000
#define PENN_THING_DEST_OK  0x8     /* can be destroyed by anyone */
#define PENN_THING_PUPPET   0x10        /* echoes to its owner */
#define PENN_THING_LISTEN   0x20        /* checks for ^ patterns */
#define PENN_THING_WEAPON    0x40            /* SW custom code for combat */
#define PENN_THING_ARMOR     0x80            /* SW custom code for combat */
#define PENN_THING_ZONED     0x100           /* Zoned Flag, */         
#define PENN_THING_NOLEAVE   0x200           /* Can't leave. */
#define PENN_ROOM_FLOATING  0x8     /* room is not linked to rest of
                     * MUSH. Don't blather about it.
                     */
#define PENN_ROOM_ABODE 0x10        /* players may link themselves here */
#define PENN_ROOM_JUMP_OK   0x20        /* anyone may @teleport to here */
#define PENN_ROOM_NO_TEL    0x40        /* mortals cannot @tel from here */
#define PENN_ROOM_TEMPLE    0x80        /* objects dropped here are sacrificed
                     * (destroyed) and player gets money.
                     */
#define PENN_ROOM_LISTEN    0x100            /* checks for ^ patterns */
#define PENN_ROOM_ZEROG     0x200            /* ZeroG room flag */
#define PENN_ROOM_SLOCK     0x400            /* Auditorium, speech locks */
#define PENN_ROOM_UNINSPECT 0x1000           /* Not inspected */
#define PENN_EXIT_LIGHT 0x10            /* If a room is dark, and an exit
                     * has this flag, then it shows 
                     * up. */

/* From old PernMUSH */
#define PERN_SLAVE  0x00000080
#define PERN_KEY    0x00000008
#define PERN_NOSPOOF    0x00200000
#define PERN_SUSPECT    0x04000000
#define PERN_VERBOSE    0x00000080
#define PERN_IMMORTAL   0x00002000
#define PERN_MONITOR    0x00200000
#define PERN_SAFE   0x04000000
#define PERN_UNFIND 0x02000000
#define V2_ACCESSED 0x00008000
#define V3_MARKED   0x00010000
#define V4_GAGGED   0x00000080
#define V4_SUSPECT  0x20000000
#define V6OBJ_KEY   0x00000008
#define V6_BUILDER  0x00000008
#define V6_FLOATING 0x00000008
#define V6EXIT_KEY  0x00000080
#define V6_SUSPECT  0x00000080
#define V6_CONNECT  0x00000200
#define V6_ABODE    0x00000200
#define V6ROOM_JUMPOK   0x00002000
#define V6PLYR_UNFIND   0x00002000
#define V6ROOM_UNFIND   0x08000000
#define V6_SLAVE    0x10000000

/* Flags from MUSE */
#define MUSE_BUILD  0x00000010
#define MUSE_SLAVE  0x00000080
#define MUSE_KEY    0x00000010
#define MUSE_DEST_OK    0x00000200
#define MUSE_ABODE  0x00000200
#define MUSE_SEETHRU    0x00000200
#define MUSE_UNFIND 0x00001000

#define MUSE_CHOWN_OK   0x00000020
#define MUSE_DARK   0x00000040
#define MUSE_STICKY 0x00000100
#define MUSE_HAVEN  0x00000400
#define MUSE_INHERIT    0x00002000
#define MUSE_GOING  0x00004000
#define MUSE_PUPPET 0x00020000
#define MUSE_LINK_OK    0x00040000
#define MUSE_ENTER_OK   0x00080000
#define MUSE_VISUAL 0x00100000
#define MUSE_OPAQUE 0x00800000
#define MUSE_QUIET  0x01000000

/* Flags from TinyMUD */
#define MUD_ABODE   0x00000800
#define MUD_ROBOT   0x00004000
#define MUD_CHOWN_OK    0x00008000

/* ---------------------------------------------------------------------------
 * FLAGENT: Information about object flags.
 */

typedef struct flag_entry {
    const char *flagname;   /* Name of the flag */
    int flagvalue;  /* Which bit in the object is the flag */
    char    flaglett;   /* Flag letter for listing */
    int flagflag;   /* Ctrl flags for this flag (recursive? :-) */
    int listperm;   /* Who sees this flag when set */
    int (*handler)(dbref target, dbref player, FLAG flag, int fflags, int reset);   /* Handler for setting/clearing this flag */
} FLAGENT;

/* ---------------------------------------------------------------------------
 * OBJENT: Fundamental object types
 */

typedef struct object_entry {
    const char *name;
    char    lett;
    int perm;
    int flags;
} OBJENT;
extern OBJENT object_types[8];

#define OF_CONTENTS 0x0001      /* Object has contents: Contents() */
#define OF_LOCATION 0x0002      /* Object has a location: Location() */
#define OF_EXITS    0x0004      /* Object has exits: Exits() */
#define OF_HOME     0x0008      /* Object has a home: Home() */
#define OF_DROPTO   0x0010      /* Object has a dropto: Dropto() */
#define OF_OWNER    0x0020      /* Object can own other objects */
#define OF_SIBLINGS 0x0040      /* Object has siblings: Next() */

typedef struct flagset {
    FLAG    word1;
    FLAG    word2;
    FLAG    word3;
} FLAGSET;

extern void NDECL(init_flagtab);
extern void FDECL(display_flagtab, (dbref));
extern void FDECL(flag_set, (dbref, dbref, char *, int));
extern char *   FDECL(flag_description, (dbref, dbref));
extern FLAGENT *FDECL(find_flag, (dbref, char *));
extern char *   FDECL(decode_flags, (dbref, FLAG, int, int));
extern int  FDECL(has_flag, (dbref, dbref, char *));
extern char *   FDECL(unparse_object, (dbref, dbref, int));
extern char *   FDECL(unparse_object_numonly, (dbref));
extern int  FDECL(convert_flags, (dbref, char *, FLAGSET *, FLAG *));
extern void FDECL(decompile_flags, (dbref, dbref, char *));

#define unparse_flags(p,t) decode_flags(p,Flags(t),Flags2(t),Flags3(t))

#define GOD ((dbref) 1)

/* ---------------------- Object Permission/Attribute Macros */
/* IS(X,T,F)        - Is X of type T and have flag F set? */
/* Typeof(X)        - What object type is X */
/* God(X)       - Is X player #1 */
/* Robot(X)     - Is X a robot player */
/* Wizard(X)        - Does X have wizard privs */
/* Immortal(X)      - Is X unkillable */
/* Alive(X)     - Is X a player or a puppet */
/* Dark(X)      - Is X dark */
/* WHODark(X)       - Should X be hidden from the WHO report */
/* Floating(X)      - Prevent 'disconnected room' msgs for room X */
/* Quiet(X)     - Should 'Set.' messages et al from X be disabled */
/* Verbose(X)       - Should owner receive all commands executed? */
/* Trace(X)     - Should owner receive eval trace output? */
/* Player_haven(X)  - Is the owner of X no-page */
/* Haven(X)     - Is X no-kill(rooms) or no-page(players) */
/* Halted(X)        - Is X halted (not allowed to run commands)? */
/* Suspect(X)       - Is X someone the wizzes should keep an eye on */
/* Slave(X)     - Should X be prevented from db-changing commands */
/* Safe(X,P)        - Does P need the /OVERRIDE switch to @destroy X? */
/* Monnitor(X)      - Should we check for ^xxx:xxx listens on player? */
/* Terse(X)     - Should we only show the room name on a look? */
/* Myopic(X)        - Should things as if we were nonowner/nonwiz */
/* Audible(X)       - Should X forward messages? */
/* Findroom(X)      - Can players in room X be found via @whereis? */
/* Unfindroom(X)    - Is @whereis blocked for players in room X? */
/* Findable(X)      - Can @whereis find X */
/* Unfindable(X)    - Is @whereis blocked for X */
/* No_robots(X)     - Does X disallow robot players from using */
/* Has_location(X)  - Is X something with a location (ie plyr or obj) */
/* Has_home(X)      - Is X something with a home (ie plyr or obj) */
/* Has_contents(X)  - Is X something with contents (ie plyr/obj/room) */
/* Good_obj(X)      - Is X inside the DB and have a valid type? */
/* Good_owner(X)    - Is X a good owner value? */
/* Going(X)     - Is X marked GOING? */
/* Inherits(X)      - Does X inherit the privs of its owner */
/* Examinable(P,X)  - Can P look at attribs of X */
/* MyopicExam(P,X)  - Can P look at attribs of X (obeys MYOPIC) */
/* Controls(P,X)    - Can P force X to do something */
/* Affects(P,X)     - (Controls in MUSH V1) Is P wiz or same owner as X */
/* Abode(X)     - Is X an ABODE room */
/* Link_exit(P,X)   - Can P link from exit X */
/* Linkable(P,X)    - Can P link to X */
/* Mark(x)      - Set marked flag on X */
/* Unmark(x)        - Clear marked flag on X */
/* Marked(x)        - Check marked flag on X */
/* See_attr(P,X.A,O,F)  - Can P see text attr A on X if attr has owner O */
/* Set_attr(P,X,A,F)    - Can P set/change text attr A (with flags F) on X */
/* Read_attr(P,X,A,O,F) - Can P see attr A on X if attr has owner O */
/* Write_attr(P,X,A,F)  - Can P set/change attr A (with flags F) on X */

#define IS(thing,type,flag) ((Typeof(thing)==(type)) && (Flags(thing) & (flag)))
#define Typeof(x)   (Flags(x) & TYPE_MASK)
#define God(x)      ((x) == GOD)
#define Robot(x)    (isPlayer(x) && ((Flags(x) & ROBOT) != 0))
#define Alive(x)    (isPlayer(x) || (Puppet(x) && Has_contents(x)))
#define OwnsOthers(x)   ((object_types[Typeof(x)].flags & OF_OWNER) != 0)
#define Has_location(x) ((object_types[Typeof(x)].flags & OF_LOCATION) != 0)
#define Has_contents(x) ((object_types[Typeof(x)].flags & OF_CONTENTS) != 0)
#define Has_exits(x)    ((object_types[Typeof(x)].flags & OF_EXITS) != 0)
#define Has_siblings(x) ((object_types[Typeof(x)].flags & OF_SIBLINGS) != 0)
#define Has_home(x) ((object_types[Typeof(x)].flags & OF_HOME) != 0)
#define Has_dropto(x)   ((object_types[Typeof(x)].flags & OF_DROPTO) != 0)
#define Home_ok(x)  ((object_types[Typeof(x)].flags & OF_HOME) != 0)
#define isPlayer(x) (Typeof(x) == TYPE_PLAYER)
#define isRoom(x)   (Typeof(x) == TYPE_ROOM)
#define isExit(x)   (Typeof(x) == TYPE_EXIT)
#define isThing(x)  (Typeof(x) == TYPE_THING)
#define isGarbage(x)    (Typeof(x) == TYPE_GARBAGE)

#define Good_obj(x) (((x) >= 0) && ((x) < mudstate.db_top) && \
             (Typeof(x) < NOTYPE))
#define Good_owner(x)   (Good_obj(x) && OwnsOthers(x))

#define Staff(x)    (Wizard(x) || Royalty(x) || ((Flags2(x) & STAFF) != 0))
#define Royalty(x)      ((Flags(x) & ROYALTY) != 0)
#define WizRoy(x)       (Royalty(x) || Wizard(x))
#define Dynamic(x)      ((Flags2(x) & DYNAMIC) !=0)
#define Fixed(x)        ((Flags2(x) & FIXED) != 0)
#define Uninspected(x)  ((Flags2(x) & UNINSPECTED) != 0)
#define Ansi(x)         ((Flags2(x) & ANSI) != 0)
#define No_Command(x)   ((Flags2(x) & NO_COMMAND) != 0)
#define NoBleed(x)      ((Flags2(x) & NOBLEED) != 0)
#define Compress(x) ((Flags2(x) & COMPRESS) != 0)

#define Transparent(x)  ((Flags(x) & SEETHRU) != 0)
#define Link_ok(x)  (((Flags(x) & LINK_OK) != 0) && Has_contents(x))
#define Wizard(x)   ((Flags(x) & WIZARD) || \
             ((Flags(Owner(x)) & WIZARD) && Inherits(x)))
#define Dark(x)     (((Flags(x) & DARK) != 0) && (Wizard(x) || !Alive(x)))
#define Jump_ok(x)  (((Flags(x) & JUMP_OK) != 0) && Has_contents(x))
#define Sticky(x)   ((Flags(x) & STICKY) != 0)
#define Destroy_ok(x)   ((Flags(x) & DESTROY_OK) != 0)
#define Haven(x)    ((Flags(x) & HAVEN) != 0)
#define Player_haven(x) ((Flags(Owner(x)) & HAVEN) != 0)
#define Quiet(x)    ((Flags(x) & QUIET) != 0)
#define Halted(x)   ((Flags(x) & HALT) != 0)
#define Trace(x)    ((Flags(x) & TRACE) != 0)
#define Going(x)    ((Flags(x) & GOING) != 0)
#define Monitor(x)  ((Flags(x) & MONITOR) != 0)
#define Myopic(x)   ((Flags(x) & MYOPIC) != 0)
#define Puppet(x)   ((Flags(x) & PUPPET) != 0)
#define Chown_ok(x) ((Flags(x) & CHOWN_OK) != 0)
#define Enter_ok(x) (((Flags(x) & ENTER_OK) != 0) && \
                Has_location(x) && Has_contents(x))
#define Visual(x)   ((Flags(x) & VISUAL) != 0)
#define Immortal(x) ((Flags(x) & IMMORTAL) || \
             ((Flags(Owner(x)) & IMMORTAL) && Inherits(x)))
#define Opaque(x)   ((Flags(x) & TM_OPAQUE) != 0)
#define Verbose(x)  ((Flags(x) & VERBOSE) != 0)
#define Inherits(x) (((Flags(x) & INHERIT) != 0) || \
             ((Flags(Owner(x)) & INHERIT) != 0) || \
             ((x) == Owner(x)))
#define Nospoof(x)  ((Flags(x) & NOSPOOF) != 0)
#define Safe(x,p)   (OwnsOthers(x) || \
             (Flags(x) & SAFE) || \
             (mudconf.safe_unowned && (Owner(x) != Owner(p))))
#define Audible(x)  ((Flags(x) & HEARTHRU) != 0)
#define Terse(x)    ((Flags(x) & TERSE) != 0)

#define Gagged(x)   ((Flags2(x) & GAGGED) != 0)
#define Vacation(x) ((Flags2(x) & VACATION) != 0)
#define Key(x)      ((Flags2(x) & KEY) != 0)
#define Abode(x)    (((Flags2(x) & ABODE) != 0) && Home_ok(x))
#define Auditorium(x)   ((Flags2(x) & AUDITORIUM) != 0)
#define Floating(x) ((Flags2(x) & FLOATING) != 0)
#define Findable(x) ((Flags2(x) & UNFINDABLE) == 0)
#define Hideout(x)  ((Flags2(x) & UNFINDABLE) != 0)
#define Parent_ok(x)    ((Flags2(x) & PARENT_OK) != 0)
#define Light(x)    ((Flags2(x) & LIGHT) != 0)
#define Suspect(x)  ((Flags2(Owner(x)) & SUSPECT) != 0)
#define Connected(x)    (((Flags2(x) & CONNECTED) != 0) && \
             (Typeof(x) == TYPE_PLAYER))
#define Slave(x)    ((Flags2(Owner(x)) & SLAVE) != 0)
#define Hidden(x)   ((Flags(x) & DARK) != 0)

#define H_Startup(x)    ((Flags(x) & HAS_STARTUP) != 0)
#define H_Fwdlist(x)    ((Flags2(x) & HAS_FWDLIST) != 0)
#define H_Listen(x) ((Flags2(x) & HAS_LISTEN) != 0)

#define s_Halted(x) s_Flags((x), Flags(x) | HALT)
#define s_Going(x)  s_Flags((x), Flags(x) | GOING)
#define s_Connected(x)  s_Flags2((x), Flags2(x) | CONNECTED)
#define c_Connected(x)  s_Flags2((x), Flags2(x) & ~CONNECTED)

#ifdef WOD_REALMS
#define isObfuscate(x)        ((Flags3(x) & OBF) != 0)
#define isHeightenedSenses(x) ((Flags3(x) & HSS) != 0)
#define isUmbra(x)            ((Flags3(x) & UMBRA) != 0)
#define isShroud(x)           ((Flags3(x) & SHROUD) != 0)
#define isMatrix(x)           ((Flags3(x) & MATRIX) != 0)
#define isMedium(x)           ((Flags3(x) & MEDIUM) != 0)
#define isDead(x)             ((Flags3(x) & DEAD) != 0)
#define isFae(x)              ((Flags3(x) & FAE) != 0)
#define isChimera(x)          ((Flags3(x) & CHIMERA) != 0)
#define isPeering(x)          ((Flags3(x) & PEERING) != 0)
#endif

#define Parentable(p,x) (Controls(p,x) || \
             (Parent_ok(x) && could_doit(p,x,A_LPARENT)))

#define OnEnterLock(p,x) (check_zone(p,x))

#define Examinable(p,x) (((Flags(x) & VISUAL) != 0) || \
             (See_All(p)) || \
                         (Owner(p) == Owner(x)) || \
             OnEnterLock(p,x))

#define MyopicExam(p,x) (((Flags(x) & VISUAL) != 0) || \
             (!Myopic(p) && (See_All(p) || \
             (Owner(p) == Owner(x)) || \
                     OnEnterLock(p,x))))

#define Controls(p,x)   (Good_obj(x) && \
             (!(God(x) && !God(p))) && \
             (Control_All(p) || \
              ((Owner(p) == Owner(x)) && \
               (Inherits(p) || !Inherits(x))) || \
               OnEnterLock(p,x)))

#define Affects(p,x)    (Good_obj(x) && \
             (!(God(x) && !God(p))) && \
             (Wizard(p) || \
              (Owner(p) == Owner(x))))
#define Mark(x)     (mudstate.markbits->chunk[(x)>>3] |= \
             mudconf.markdata[(x)&7])
#define Unmark(x)   (mudstate.markbits->chunk[(x)>>3] &= \
             ~mudconf.markdata[(x)&7])
#define Marked(x)   (mudstate.markbits->chunk[(x)>>3] & \
             mudconf.markdata[(x)&7])
#define Mark_all(i) {for ((i)=0; (i)<((mudstate.db_top+7)>>3); (i)++) \
                             mudstate.markbits->chunk[i]=0xFFU;}
#define Unmark_all(i)   {for ((i)=0; (i)<((mudstate.db_top+7)>>3); (i)++) \
                             mudstate.markbits->chunk[i]=0x0;}
#define Link_exit(p,x)  ((Typeof(x) == TYPE_EXIT) && \
             ((Location(x) == NOTHING) || Controls(p,x)))
#define Linkable(p,x)   (Good_obj(x) && \
             (Has_contents(x)) && \
             (((Flags(x) & LINK_OK) != 0) || \
              Controls(p,x)))
#define See_attr(p,x,a,o,f) \
            (!((a)->flags & (AF_INTERNAL|AF_IS_LOCK)) && \
             (God(p) || \
              ((f) & AF_VISUAL) || \
                         (((Owner(p) == (o)) || Examinable(p,x)) && \
               !((a)->flags & (AF_DARK|AF_MDARK)) && \
               !((f) & (AF_DARK|AF_MDARK))) || \
              ((Wizard(p) || Royalty(p)) && !((a)->flags & AF_DARK)) || \
              !((a)->flags & (AF_DARK|AF_MDARK|AF_ODARK))))
#define See_attr_explicit(p,x,a,o,f) \
            (!((a)->flags & (AF_INTERNAL|AF_IS_LOCK)) && \
             (((f) & AF_VISUAL) || \
              (Owner(p) == (o)) && \
               !((a)->flags & (AF_DARK|AF_MDARK))))
#define Set_attr(p,x,a,f) \
            (!((a)->flags & (AF_INTERNAL|AF_IS_LOCK)) && \
             (God(p) || \
              (!God(x) && !(f & AF_LOCK) && \
               ((Controls(p,x) && \
                 !((a)->flags & (AF_WIZARD|AF_GOD)) && \
                 !((f) & (AF_WIZARD|AF_GOD))) || \
                (Wizard(p) && \
                 !((a)->flags & AF_GOD))))))
#define Read_attr(p,x,a,o,f) \
            (!((a)->flags & AF_INTERNAL) && \
             (God(p) || \
              ((f) & AF_VISUAL) || \
                         (((Owner(p) == o) || Examinable(p,x)) && \
               !((a)->flags & (AF_DARK|AF_MDARK)) && \
               !((f) & (AF_DARK|AF_MDARK))) || \
              ((Wizard(p) || Royalty(p)) && !((a)->flags & AF_DARK)) || \
              !((a)->flags & (AF_DARK|AF_MDARK|AF_ODARK))))
#define Write_attr(p,x,a,f) \
            (!((a)->flags & AF_INTERNAL) && \
             (God(p) || \
              (!God(x) && !(f & AF_LOCK) && \
               ((Controls(p,x) && \
                 !((a)->flags & (AF_WIZARD|AF_GOD)) && \
                 !((f) & (AF_WIZARD|AF_GOD))) || \
                (Wizard(p) && \
                 !((a)->flags & AF_GOD))))))
#define Has_power(p,x)  (check_access((p),powers_nametab[x].flag))
#define Html(x) ((Flags2(x) & HTML) != 0)
#define s_Html(x) s_Flags2((x), Flags2(x) | HTML)
#endif
