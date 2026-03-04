/*! \file flags.h
 * \brief Object flags.
 *
 */

#include "copyright.h"

#ifndef FLAGS_H
#define FLAGS_H

constexpr int FLAG_WORD1    = 0x0; // 1st word of flags.
constexpr int FLAG_WORD2    = 0x1; // 2nd word of flags.
constexpr int FLAG_WORD3    = 0x2; // 3rd word of flags.

/* Object types */
constexpr int TYPE_ROOM     = 0x0;
constexpr int TYPE_THING    = 0x1;
constexpr int TYPE_EXIT     = 0x2;
constexpr int TYPE_PLAYER   = 0x3;
/* Empty */
constexpr int TYPE_GARBAGE  = 0x5;
/* Empty */
constexpr int NOTYPE        = 0x7;
constexpr int TYPE_MASK     = 0x7;

/* First word of flags */
constexpr unsigned int SEETHRU      = 0x00000008;  /* Can see through to the other side */
constexpr unsigned int WIZARD       = 0x00000010;  /* gets automatic control */
constexpr unsigned int LINK_OK      = 0x00000020;  /* anybody can link to this room */
constexpr unsigned int DARK         = 0x00000040;  /* Don't show contents or presence */
constexpr unsigned int JUMP_OK      = 0x00000080;  /* Others may @tel here */
constexpr unsigned int STICKY       = 0x00000100;  /* Object goes home when dropped */
constexpr unsigned int DESTROY_OK   = 0x00000200;  /* Others may @destroy */
constexpr unsigned int HAVEN        = 0x00000400;  /* No killing here, or no pages */
constexpr unsigned int QUIET        = 0x00000800;  /* Prevent 'feelgood' messages */
constexpr unsigned int HALT         = 0x00001000;  /* object cannot perform actions */
constexpr unsigned int TRACE        = 0x00002000;  /* Generate evaluation trace output */
constexpr unsigned int GOING        = 0x00004000;  /* object is available for recycling */
constexpr unsigned int MONITOR      = 0x00008000;  /* Process ^x:action listens on obj? */
constexpr unsigned int MYOPIC       = 0x00010000;  /* See things as nonowner/nonwizard */
constexpr unsigned int PUPPET       = 0x00020000;  /* Relays ALL messages to owner */
constexpr unsigned int CHOWN_OK     = 0x00040000;  /* Object may be @chowned freely */
constexpr unsigned int ENTER_OK     = 0x00080000;  /* Object may be ENTERed */
constexpr unsigned int VISUAL       = 0x00100000;  /* Everyone can see properties */
constexpr unsigned int IMMORTAL     = 0x00200000;  /* Object can't be killed */
constexpr unsigned int HAS_STARTUP  = 0x00400000;  /* Load some attrs at startup */
constexpr unsigned int MUX_OPAQUE   = 0x00800000;  /* Can't see inside */
constexpr unsigned int VERBOSE      = 0x01000000;  /* Tells owner everything it does. */
constexpr unsigned int INHERIT      = 0x02000000;  /* Gets owner's privs. (i.e. Wiz) */
constexpr unsigned int NOSPOOF      = 0x04000000;  /* Report originator of all actions. */
constexpr unsigned int ROBOT        = 0x08000000;  /* Player is a ROBOT */
constexpr unsigned int SAFE         = 0x10000000;  /* Need /override to @destroy */
constexpr unsigned int ROYALTY      = 0x20000000;  /* Sees like a wiz, but ca't modify */
constexpr unsigned int HEARTHRU     = 0x40000000;  /* Can hear out of this obj or exit */
constexpr unsigned int TERSE        = 0x80000000;  /* Only show room name on look */

/* Second word of flags */
constexpr unsigned int KEY          = 0x00000001;  /* No puppets */
constexpr unsigned int ABODE        = 0x00000002;  /* May @set home here */
constexpr unsigned int FLOATING     = 0x00000004;  /* Inhibit Floating room.. msgs */
constexpr unsigned int UNFINDABLE   = 0x00000008;  /* Cant loc() from afar */
constexpr unsigned int PARENT_OK    = 0x00000010;  /* Others may @parent to me */
constexpr unsigned int LIGHT        = 0x00000020;  /* Visible in dark places */
constexpr unsigned int HAS_LISTEN   = 0x00000040;  /* Internal: LISTEN attr set */
constexpr unsigned int HAS_FWDLIST  = 0x00000080;  /* Internal: FORWARDLIST attr set */
constexpr unsigned int AUDITORIUM   = 0x00000100;  /* Should we check the SpeechLock? */
constexpr unsigned int ANSI         = 0x00000200;
constexpr unsigned int HEAD_FLAG    = 0x00000400;
constexpr unsigned int FIXED        = 0x00000800;
constexpr unsigned int UNINSPECTED  = 0x00001000;
constexpr unsigned int NO_COMMAND   = 0x00002000;
constexpr unsigned int CKEEPALIVE   = 0x00004000;  /* User receives keepalives from the MUX */
constexpr unsigned int NOBLEED      = 0x00008000;
constexpr unsigned int STAFF        = 0x00010000;
constexpr unsigned int HAS_DAILY    = 0x00020000;
constexpr unsigned int GAGGED       = 0x00040000;
constexpr unsigned int OPEN_OK      = 0x00080000;  // You can open exits from here if you pass the openlock.
constexpr unsigned int COLOR256     = 0x00100000;  // Enable XTERM 256-color codes.
constexpr unsigned int VACATION     = 0x01000000;
constexpr unsigned int PLAYER_MAILS = 0x02000000;
constexpr unsigned int HTML         = 0x04000000;  /* Player supports HTML */
constexpr unsigned int BLIND        = 0x08000000;  // Suppress has arrived / left messages.
constexpr unsigned int SUSPECT      = 0x10000000;  /* Report some activities to wizards */
constexpr unsigned int ASCII        = 0x20000000;  // Strip characters down to 7-bit ASCII.
constexpr unsigned int CONNECTED    = 0x40000000;  /* Player is connected */
constexpr unsigned int SLAVE        = 0x80000000;  /* Disallow most commands */

// Third word of flags
//
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
constexpr unsigned int OBF          = 0x00000001;      // Obfuscate Flag
constexpr unsigned int HSS          = 0x00000002;      // Auspex/Heightened Senses Flag
constexpr unsigned int UMBRA        = 0x00000004;      // Umbra, UMBRADESC
constexpr unsigned int SHROUD       = 0x00000008;      // Shroud, WRAITHDESC
constexpr unsigned int MATRIX       = 0x00000010;      // Matrix, MATRIXDESC
constexpr unsigned int MEDIUM       = 0x00000020;
constexpr unsigned int DEAD         = 0x00000040;
constexpr unsigned int FAE          = 0x00000080;      // Fae, FAEDESC
constexpr unsigned int CHIMERA      = 0x00000100;      // Fae, FAEDESC
constexpr unsigned int PEERING      = 0x00000200;      // Means the a looker is seeing a
                                     // different realm than they are in.
#endif // WOD_REALMS

constexpr unsigned int SITEMON      = 0x00000400;      // Sitemonitor Flag
constexpr unsigned int CMDCHECK     = 0x00000800;      // Has @icmd set
constexpr unsigned int MUX_UNICODE  = 0x00001000;      // UTF-8 override flag
constexpr unsigned int MARK_0       = 0x00400000;      // User-defined flags.
constexpr unsigned int MARK_1       = 0x00800000;
constexpr unsigned int MARK_2       = 0x01000000;
constexpr unsigned int MARK_3       = 0x02000000;
constexpr unsigned int MARK_4       = 0x04000000;
constexpr unsigned int MARK_5       = 0x08000000;
constexpr unsigned int MARK_6       = 0x10000000;
constexpr unsigned int MARK_7       = 0x20000000;
constexpr unsigned int MARK_8       = 0x40000000;
constexpr unsigned int MARK_9       = 0x80000000;

/* ---------------------------------------------------------------------------
 * FLAGENT: Information about object flags.
 */
typedef struct flag_bit_entry
{
    unsigned int flagvalue; // Which bit in the object is the flag.
    UTF8 flaglett;          // Flag letter for listing.
    int  flagflag;          // Ctrl flags for this flag.
    int  listperm;          // Who sees this flag when set.
    bool (*handler)(dbref target, dbref player, FLAG flag, int fflags,
        bool reset);         // Handler for setting/clearing this flag.
} FLAGBITENT;

typedef struct flag_name_entry
{
    const UTF8 *pOrigName;  // Original name of flag.
    bool bPositive;         // Flag sense.
    FLAGBITENT *fbe;        // Which bit is this associated with?
    UTF8 *flagname;         // Name of the flag.
} FLAGNAMEENT;

extern FLAGNAMEENT gen_flag_names[];

/* ---------------------------------------------------------------------------
 * OBJENT: Fundamental object types
 */

typedef struct object_entry
{
    const UTF8 *name;
    char    lett;
    int perm;
    int flags;
} OBJENT;
extern OBJENT object_types[8];

constexpr int OF_CONTENTS = 0x0001;      /* Object has contents: Contents() */
constexpr int OF_LOCATION = 0x0002;      /* Object has a location: Location() */
constexpr int OF_EXITS    = 0x0004;      /* Object has exits: Exits() */
constexpr int OF_HOME     = 0x0008;      /* Object has a home: Home() */
constexpr int OF_DROPTO   = 0x0010;      /* Object has a dropto: Dropto() */
constexpr int OF_OWNER    = 0x0020;      /* Object can own other objects */
constexpr int OF_SIBLINGS = 0x0040;      /* Object has siblings: Next() */

typedef struct flagset
{
    FLAG  word[3];
} FLAGSET;

void init_flagtab(void);
void display_flagtab(dbref);
void flag_set(dbref, dbref, UTF8 *, int);
UTF8 *flag_description(dbref, dbref);
UTF8 *decode_flags(dbref, FLAGSET *);
bool has_flag(dbref, dbref, const UTF8 *);
UTF8 *unparse_object(dbref player, dbref target, bool obey_myopic);
UTF8 *unparse_object_numonly(dbref);
bool convert_flags(dbref, UTF8 *, FLAGSET *, FLAG *);
void decompile_flags(dbref, dbref, UTF8 *);
UTF8 *MakeCanonicalFlagName
(
    const UTF8 *pName,
    int *pnName,
    bool *pbValid
);

#define GOD ((dbref) 1)

/* ---------------------- Object Permission/Attribute Macros */
/* Typeof(X)            - What object type is X */
/* God(X)               - Is X player #1 */
/* Robot(X)             - Is X a robot player */
/* Wizard(X)            - Does X have wizard privs */
/* Immortal(X)          - Is X unkillable */
/* Dark(X)              - Is X dark */
/* Floating(X)          - Prevent 'disconnected room' msgs for room X */
/* Quiet(X)             - Should 'Set.' messages et al from X be disabled */
/* Verbose(X)           - Should owner receive all commands executed? */
/* Trace(X)             - Should owner receive eval trace output? */
/* Player_haven(X)      - Is the owner of X no-page */
/* Haven(X)             - Is X no-kill(rooms) or no-page(players) */
/* Halted(X)            - Is X halted (not allowed to run commands)? */
/* Suspect(X)           - Is X someone the wizzes should keep an eye on */
/* Slave(X)             - Should X be prevented from db-changing commands */
/* Safe(X,P)            - Does P need the /OVERRIDE switch to @destroy X? */
/* Monitor(X)           - Should we check for ^xxx:xxx listens on player? */
/* Terse(X)             - Should we only show the room name on a look? */
/* Myopic(X)            - Should things as if we were nonowner/nonwiz */
/* Audible(X)           - Should X forward messages? */
/* Findroom(X)          - Can players in room X be found via @whereis? */
/* Unfindroom(X)        - Is @whereis blocked for players in room X? */
/* Findable(X)          - Can @whereis find X */
/* Unfindable(X)        - Is @whereis blocked for X */
/* No_robots(X)         - Does X disallow robot players from using */
/* Has_location(X)      - Is X something with a location (ie plyr or obj) */
/* Has_home(X)          - Is X something with a home (ie plyr or obj) */
/* Has_contents(X)      - Is X something with contents (ie plyr/obj/room) */
/* Good_dbref(X)        - Is X inside the DB? */
/* Good_obj(X)          - Is X inside the DB and have a valid type? */
/* Good_owner(X)        - Is X a good owner value? */
/* Going(X)             - Is X marked GOING? */
/* Inherits(X)          - Does X inherit the privs of its owner */
/* Examinable(P,X)      - Can P look at attribs of X */
/* MyopicExam(P,X)      - Can P look at attribs of X (obeys MYOPIC) */
/* Controls(P,X)        - Can P force X to do something */
/* Abode(X)             - Is X an ABODE room */
/* Link_exit(P,X)       - Can P link from exit X */
/* Linkable(P,X)        - Can P link to X */
/* Mark(x)              - Set marked flag on X */
/* Unmark(x)            - Clear marked flag on X */
/* Marked(x)            - Check marked flag on X */
/* See_attr(P,X.A,O,F)  - Can P see text attr A on X if attr has owner O */
/* KeepAlive(x)         - Does the user want keepalives? */

#define Typeof(x)           (Flags(x) & TYPE_MASK)
#define God(x)              ((x) == GOD)
#define Robot(x)            (isPlayer(x) && ((Flags(x) & ROBOT) != 0))
#define OwnsOthers(x)       ((object_types[Typeof(x)].flags & OF_OWNER) != 0)
#define Has_location(x)     ((object_types[Typeof(x)].flags & OF_LOCATION) != 0)
#define Has_contents(x)     ((object_types[Typeof(x)].flags & OF_CONTENTS) != 0)
#define Has_exits(x)        ((object_types[Typeof(x)].flags & OF_EXITS) != 0)
#define Has_siblings(x)     ((object_types[Typeof(x)].flags & OF_SIBLINGS) != 0)
#define Has_home(x)         ((object_types[Typeof(x)].flags & OF_HOME) != 0)
#define Has_dropto(x)       ((object_types[Typeof(x)].flags & OF_DROPTO) != 0)
#define Home_ok(x)          ((object_types[Typeof(x)].flags & OF_HOME) != 0)
#define isPlayer(x)         (Typeof(x) == TYPE_PLAYER)
#define isRoom(x)           (Typeof(x) == TYPE_ROOM)
#define isExit(x)           (Typeof(x) == TYPE_EXIT)
#define isThing(x)          (Typeof(x) == TYPE_THING)
#define isGarbage(x)        (Typeof(x) == TYPE_GARBAGE)

#define Good_dbref(x)       (((x) >= 0) && ((x) < mudstate.db_top))
#define Good_obj(x)         (Good_dbref(x) && (Typeof(x) < TYPE_GARBAGE))
#define Good_owner(x)       (Good_obj(x) && OwnsOthers(x))

#define Staff(x)            (Wizard(x) || Royalty(x) || ((Flags2(x) & STAFF) != 0))
#define Royalty(x)          (  (Flags(x) & ROYALTY) \
                            || (  (Flags(Owner(x)) & ROYALTY) \
                               && Inherits(x) \
                               ) \
                            )
#define RealRoyalty(x)      (Royalty(x))
#define WizRoy(x)           (Royalty(x) || Wizard(x))
#define RealWizRoy(x)       (RealRoyalty(x) || RealWizard(x))
#define Head(x)             ((Flags2(x) & HEAD_FLAG) != 0)
#define Fixed(x)            ((Flags2(x) & FIXED) != 0)
#define Uninspected(x)      ((Flags2(x) & UNINSPECTED) != 0)
#define Ansi(x)             ((Flags2(x) & ANSI) != 0)
#define Color256(x)         ((Flags2(x) & COLOR256) != 0)
#define Ascii(x)            ((Flags2(x) & ASCII) != 0)
#define No_Command(x)       ((Flags2(x) & NO_COMMAND) != 0)
#define NoBleed(x)          ((Flags2(x) & NOBLEED) != 0)
#define KeepAlive(x)        ((Flags2(x) & CKEEPALIVE) != 0)

#define Transparent(x)      ((Flags(x) & SEETHRU) != 0)
#define Link_ok(x)          (((Flags(x) & LINK_OK) != 0) && Has_contents(x))
#define Open_ok(x)          (((Flags2(x) & OPEN_OK) != 0) && Has_exits(x))
#define Wizard(x)           (  (Flags(x) & WIZARD) \
                            || (  (Flags(Owner(x)) & WIZARD) \
                               && Inherits(x) \
                               ) \
                            )
#define RealWizard(x)       (Wizard(x))
#define Dark(x)             (((Flags(x) & DARK) != 0) && (Wizard(x) || \
                            !(isPlayer(x) || (Puppet(x) && Has_contents(x)))))
#define Jump_ok(x)          (((Flags(x) & JUMP_OK) != 0) && Has_contents(x))
#define Sticky(x)           ((Flags(x) & STICKY) != 0)
#define Destroy_ok(x)       ((Flags(x) & DESTROY_OK) != 0)
#define Haven(x)            ((Flags(x) & HAVEN) != 0)
#define Player_haven(x)     ((Flags(Owner(x)) & HAVEN) != 0)
#define Quiet(x)            ((Flags(x) & QUIET) != 0)
#define Halted(x)           ((Flags(x) & HALT) != 0)
#define Trace(x)            ((Flags(x) & TRACE) != 0)
#define Going(x)            ((Flags(x) & GOING) != 0)
#define Monitor(x)          ((Flags(x) & MONITOR) != 0)
#define Myopic(x)           ((Flags(x) & MYOPIC) != 0)
#define Puppet(x)           ((Flags(x) & PUPPET) != 0)
#define Chown_ok(x)         ((Flags(x) & CHOWN_OK) != 0)
#define Enter_ok(x)         (((Flags(x) & ENTER_OK) != 0) && \
                            Has_location(x) && Has_contents(x))
#define Immortal(x)         ((Flags(x) & IMMORTAL) || \
                            ((Flags(Owner(x)) & IMMORTAL) && Inherits(x)))
#define Opaque(x)           ((Flags(x) & MUX_OPAQUE) != 0)
#define Verbose(x)          ((Flags(x) & VERBOSE) != 0)
#define Inherits(x)         (((Flags(x) & INHERIT) != 0) || \
                            ((Flags(Owner(x)) & INHERIT) != 0) || \
                            ((x) == Owner(x)))
#define Nospoof(x)          ((Flags(x) & NOSPOOF) != 0)
#define Safe(x,p)           (OwnsOthers(x) || (Flags(x) & SAFE) || \
                            (mudconf.safe_unowned && (Owner(x) != Owner(p))))
#define Audible(x)          ((Flags(x) & HEARTHRU) != 0)
#define Terse(x)            ((Flags(x) & TERSE) != 0)

#define Gagged(x)           ((Flags2(x) & GAGGED) != 0)
#define Vacation(x)         ((Flags2(x) & VACATION) != 0)
#define Key(x)              ((Flags2(x) & KEY) != 0)
#define Abode(x)            (((Flags2(x) & ABODE) != 0) && Home_ok(x))
#define Auditorium(x)       ((Flags2(x) & AUDITORIUM) != 0)
#define Floating(x)         ((Flags2(x) & FLOATING) != 0)
#define Findable(x)         ((Flags2(x) & UNFINDABLE) == 0)
#define Hideout(x)          ((Flags2(x) & UNFINDABLE) != 0)
#define Parent_ok(x)        ((Flags2(x) & PARENT_OK) != 0)
#define Light(x)            ((Flags2(x) & LIGHT) != 0)
#define Suspect(x)          ((Flags2(Owner(x)) & SUSPECT) != 0)
#define Connected(x)        (((Flags2(x) & CONNECTED) != 0) && \
                            (Typeof(x) == TYPE_PLAYER))
#define Slave(x)            ((Flags2(Owner(x)) & SLAVE) != 0)
#define Hidden(x)           ((Flags(x) & DARK) != 0)
#define Blind(x)            ((Flags2(x) & BLIND) != 0)

#define H_Daily(x)          ((Flags2(x) & HAS_DAILY) != 0)
#define H_Fwdlist(x)        ((Flags2(x) & HAS_FWDLIST) != 0)
#define H_Listen(x)         ((Flags2(x) & HAS_LISTEN) != 0)
#define H_Startup(x)        ((Flags(x) & HAS_STARTUP) != 0)

#define s_Halted(x)         s_Flags((x), FLAG_WORD1, Flags(x) | HALT)
#define s_Going(x)          s_Flags((x), FLAG_WORD1, Flags(x) | GOING)
#define s_Connected(x)      s_Flags((x), FLAG_WORD2, Flags2(x) | CONNECTED)
#define c_Connected(x)      s_Flags((x), FLAG_WORD2, Flags2(x) & ~CONNECTED)
#define SiteMon(x)          ((Flags3(x) & SITEMON) != 0)
#define CmdCheck(x)         ((Flags3(x) & CMDCHECK) != 0)
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
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
#endif // WOD_REALMS


#define Parentable(p,x)     (Controls(p,x) || \
                            (Parent_ok(x) && could_doit(p,x,A_LPARENT)))

#define Examinable(p,x)     (((Flags(x) & VISUAL) != 0) || \
                            (See_All(p)) || \
                            (Owner(p) == Owner(x)) || \
                            (check_zone(p,x)))

#define MyopicExam(p,x)     (((Flags(x) & VISUAL) != 0) || \
                            (!Myopic(p) && (See_All(p) || \
                            (Owner(p) == Owner(x)) || \
                            (check_zone(p,x)))))

#define Controls(p,x)       (Good_obj(x) && \
                            (!(God(x) && !God(p))) && \
                            (Control_All(p) || \
                            ((Owner(p) == Owner(x)) && \
                            (Inherits(p) || !Inherits(x))) || \
                            (check_zone(p,x))))

#define Mark(x)             (mudstate.markbits->chunk[(x)>>3] |= \
                            mudconf.markdata[(x)&7])
#define Unmark(x)           (mudstate.markbits->chunk[(x)>>3] &= \
                            ~mudconf.markdata[(x)&7])
#define Marked(x)           ((mudstate.markbits->chunk[(x)>>3] & \
                            mudconf.markdata[(x)&7]) ? true : false)
#define Mark_all(i)         {for ((i)=0; (i)<((mudstate.db_top+7)>>3); (i)++) \
                            mudstate.markbits->chunk[i]=0xFFU;}
#define Unmark_all(i)       {for ((i)=0; (i)<((mudstate.db_top+7)>>3); (i)++) \
                            mudstate.markbits->chunk[i]=0x0;}
#define Link_exit(p,x)      ((Typeof(x) == TYPE_EXIT) && \
                            ((Location(x) == NOTHING) || Controls(p,x)))
#define Linkable(p,x)       (Good_obj(x) && Has_contents(x) && \
                            (((Flags(x) & LINK_OK) != 0) || Controls(p,x)))
#define See_attr(p,x,a)     (!((a)->flags & AF_IS_LOCK) && bCanReadAttr(p,x,a,false))
#define See_attr_explicit(p,x,a,o,f) (!((a)->flags & (AF_INTERNAL|AF_IS_LOCK)) && \
                            (((f) & AF_VISUAL) || (Owner(p) == (o)) && \
                            !((a)->flags & (AF_DARK|AF_MDARK))))

#define Has_power(p,x)      (check_access((p),powers_nametab[x].flag))
#define Html(x)             ((Flags2(x) & HTML) != 0)
#define s_Html(x)           s_Flags((x), FLAG_WORD2, Flags2(x) | HTML)

#define Unicode(x)          ((Flags3(x) & MUX_UNICODE) != 0)
#define s_Unicode(x)        s_Flags((x), FLAG_WORD3, Flags3(x) | MUX_UNICODE)

#endif // !FLAGS_H
