// mudconf.h
//
// $Id: mudconf.h,v 1.4 2003-01-29 23:22:30 jake Exp $
//

#ifndef __CONF_H
#define __CONF_H

#include "alloc.h"
#include "htab.h"
#include "mail.h"
#include "stringutil.h"

#ifndef WIN32
#include <netinet/in.h>
#endif // !WIN32

#define WIDTHOF_DOING_STRING 45
#define SIZEOF_DOING_STRING (2*WIDTHOF_DOING_STRING)

/* CONFDATA:    runtime configurable parameters */

typedef struct tag_int_array
{
    int n;
    int *pi;
} IntArray;

typedef struct
{
    const char *CommandName;
    CHashTable *ht;
    char       *pBaseFilename;
    BOOL       bEval;
} HELP_DESC;

typedef struct confdata CONFDATA;
struct confdata
{
    int     active_q_chunk; /* # cmds to run from queue when active */
    BOOL    allow_guest_from_registered_site; // Whether guests from registered sites are allowed.
    ArtRuleset* art_rules;  /* Rulesets for defining exceptions. */
    BOOL    autozone;       // New objects are automatically zoned.
    BOOL    cache_names;    /* Should object names be cached separately */
    int     check_interval; /* interval between db check/cleans in secs */
    int     check_offset;   /* when to perform first check and clean */
    BOOL    clone_copy_cost;/* Does @clone copy value? */
    int     cmd_quota_incr; /* Bump #cmds allowed by this each timeslice */
    int     cmd_quota_max;  /* Max commands at one time */
    char    *compress;       /* program to run to compress */
    BOOL    compress_db;    // should we use compress.
    char    *comsys_db;      /* name of the comsys db */
    char    *config_file;    /* name of config file, used by @restart */
    char    *conn_file;      /* display on connect if no registration */
    int     conn_timeout;   /* Allow this long to connect before booting */
    int     control_flags;  /* Global runtime control flags */
    char    *crashdb;        /* write database here on crash */
    char    *crea_file;      /* display this on login for new users */
    int     createmax;      /* max cost of @create command */
    int     createmin;      /* default (and minimum) cost of @create cmd */
    char    *creg_file;      /* display on connect if registration */
    BOOL    dark_sleepers;  /* Are sleeping players 'dark'? */
    dbref   default_home;   // HOME when home is inaccessable.
    BOOL    destroy_going_now;  // Does GOING act like DESTROY_OK?
    int     digcost;        /* cost of @dig command */
    char    *down_file;      /* display this file if no logins */
    char    downmotd_msg[GBUF_SIZE];  /* Settable 'logins disabled' message */
    int     dump_interval;  /* interval between ckp dumps in seconds */
    char    dump_msg[128];  /* Message displayed when @dump-ing */
    int     dump_offset;    /* when to take first checkpoint dump */
    BOOL    eval_comtitle;  /* Should Comtitles Evaluate? */
    int     events_daily_hour; /* At what hour should @daily be executed? */
    BOOL    ex_flags;       /* TRUE = show flags on examine */
    BOOL    exam_public;    /* Does EXAM show public attrs by default? */
    FLAGSET exit_flags;     /* Flags exits start with */
    int     exit_quota;     /* quota needed to make an exit */
    BOOL    fascist_tport;  /* Src of teleport must be owned/JUMP_OK */
    char    fixed_home_msg[128];  /* Message displayed when going home and FIXED */
    char    fixed_tel_msg[128]; /* Message displayed when teleporting and FIXED */
    int     fork_dump;      // perform dump in a forked process.
    char    *full_file;      /* display when max users exceeded */
    char    fullmotd_msg[GBUF_SIZE];  /* Settable 'Too many players' message */
    int     func_invk_lim;  /* Max funcs invoked by a command */
    int     func_nest_lim;  /* Max nesting of functions */
    char    *game_dir;      /* use this game CHashFile DIR file if we need one */
    char    *game_pag;      /* use this game CHashFile PAG file if we need one */
    dbref   global_error_obj;    // Object that is used to generate error messages.
    dbref   guest_char;     // player num of prototype GUEST character.
    char    *guest_file;     /* display if guest connects */
    dbref   guest_nuker;    // Wiz who nukes the GUEST characters.
    char    guest_prefix[32]; /* Prefix for the guest char's name */
    char    guests_channel[32]; /* Name of guests channel */
    BOOL    have_comsys;    // Should the comsystem be active?
    BOOL    have_mailer;    // Should @mail be active?
    BOOL    have_zones;     // Should zones be active?
    int     hook_cmd;       // @hooks to be initialized.
    dbref   hook_obj;       // Object with @hook data.
    int     idle_interval;  /* when to check for idle users */
    int     idle_timeout;   /* Boot off players idle this long in secs */
    BOOL    idle_wiz_dark;  /* Do idling wizards get set dark? */
    char    *indb;           /* database file name */
    BOOL    indent_desc;    // Newlines before and after descs?
    int     init_size;      // initial db size.
    int     killguarantee;  /* cost of kill cmd that guarantees success */
    int     killmax;        /* max cost of kill command */
    int     killmin;        /* default (and minimum) cost of kill cmd */
    int     linkcost;       /* cost of @link command */
    int     lock_nest_lim;  /* Max nesting of lock evals */
    int     log_info;       /* Info that goes into log entries */
    int     log_options;    /* What gets logged */
    int     machinecost;    /* One in mc+1 cmds costs 1 penny (POW2-1) */
    char    *mail_db;        /* name of the @mail database */
    int     mail_expiration; /* Number of days to wait to delete mail */
    char    many_coins[32]; /* name of many coins (ie. "pennies") */
    unsigned char   markdata[8];    /* Masks for marking/unmarking */
    dbref   master_room;    // Room containing default cmds/exits/etc.
    BOOL    match_mine;     /* Should you check yourself for $-commands? */
    BOOL    match_mine_pl;  /* Should players check selves for $-cmds? */
    unsigned int max_cache_size; /* Max size of attribute cache */
    int     max_cmdsecs;    /* Threshhold for real time taken by command */
    int     max_players;    /* Max # of connected players */
    int     min_guests;     // The # we should start nuking at.
    char    *motd_file;      /* display this file on login */
    char    motd_msg[GBUF_SIZE]; /* Wizard-settable login message */
    char    mud_name[32];   /* Name of the mud */
    BOOL    name_spaces;    // allow player names to have spaces.
    int     nStackLimit;    // Current stack limit.
    int     ntfy_nest_lim;  /* Max nesting of notifys */
    int     number_guests;  // number of guest characters allowed.
    char    one_coin[32];   /* name of one coin (ie. "penny") */
    int     opencost;       /* cost of @open command */
    char    *outdb;          /* checkpoint the database to here */
    int     output_limit;   /* Max # chars queued for output */
    int     pagecost;       /* cost of @page command */
    BOOL    paranoid_alloc; /* Rigorous buffer integrity checks */
    int     parent_nest_lim;/* Max levels of parents */
    int     paycheck;       /* players earn this much each day connected */
    int     payfind;        /* chance to find a penny with wandering */
    int     paylimit;       /* getting money gets hard over this much */
    int     paystart;       /* new players start with this much money */
    BOOL    pemit_any;      /* Can you @pemit to ANY remote object? */
    BOOL    pemit_players;  /* Can you @pemit to faraway players? */
    BOOL    player_listen;  /* Are AxHEAR triggered on players? */
    FLAGSET player_flags;   /* Flags players start with */
    int     player_quota;   /* quota needed to make a robot player */
    IntArray ports;         // user ports.
    char    postdump_msg[128];  /* Message displayed after @dump-ing */
    BOOL    pub_flags;      /* TRUE = flags() works on anything */
    char    public_channel[32]; /* Name of public channel */
    char    pueblo_msg[GBUF_SIZE];   /* Message displayed to Pueblo clients */
    int     queue_chunk;    /* # cmds to run from queue when idle */
    int     queuemax;       /* max commands a player may have in queue */
    BOOL    quiet_look;     /* TRUE = don't see attribs when looking */
    BOOL    quiet_whisper;  /* Can others tell when you whisper? */
    char    *quit_file;      /* display on quit */
    BOOL    quotas;         /* TRUE = have building quotas */
    BOOL    read_rem_desc;  /* Can the DESCs of nonlocal objs be read? */
    BOOL    read_rem_name;  /* Can the NAMEs of nonlocal objs be read? */
    char    *regf_file;      /* display on (failed) create if reg is on */
    int     retry_limit;    /* close conn after this many bad logins */
    int     robotcost;      /* cost of @robot command */
    FLAGSET robot_flags;    /* Flags robots start with */
    BOOL    robot_speak;    /* TRUE = allow robots to speak */
    FLAGSET room_flags;     /* Flags rooms start with */
    int     room_quota;     /* quota needed to make a room */
    BOOL    run_startup;    // If no, startup attributes aren't processed on load.
    int     sacadjust;      /* sacrifice earns (obj_cost/sfactor) + sadj */
    int     sacfactor;      /* sacrifice earns (obj_cost/sfactor) + sadj */
    BOOL    safe_unowned;   /* Are objects not owned by you safe? */
    BOOL    safe_wipe;      // If yes, SAFE flag must be removed to @wipe.
    BOOL    safer_passwords;/* enforce reasonably good password choices? */
    int     searchcost;     /* cost of commands that search the whole DB */
    BOOL    see_own_dark;   /* Do you see your own dark stuff? */
    int     sig_action;     // What to do with fatal signals.
    unsigned int site_chars;// where to truncate site name.
    char    *site_file;      /* display if conn from bad site */
    BOOL    space_compress; /* Convert multiple spaces into one space */
    int     stack_limit;    /* How big can stacks get? */
    dbref   start_home;     // initial HOME for players.
    int     start_quota;    /* Quota for new players */
    dbref   start_room;     // initial location and home for players.
    char    *status_file;    /* Where to write arg to @shutdown */
    BOOL    sweep_dark;     /* Can you sweep dark places? */
    BOOL    switch_df_all;  /* Should @switch match all by default? */
    BOOL    terse_contents; /* Does TERSE look show exits */
    BOOL    terse_exits;    /* Does TERSE look show obvious exits */
    BOOL    terse_look;     /* Does manual look obey TERSE */
    BOOL    terse_movemsg;  /* Show move msgs (SUCC/LEAVE/etc) if TERSE? */
    FLAGSET thing_flags;    /* Flags things start with */
    int     thing_quota;    /* quota needed to make a thing */
    int     timeslice;      /* How often do we bump people's cmd quotas? */
    dbref   toad_recipient; /* Default @toad recipient. */
    int     trace_limit;    /* Max lines of trace output if top-down */
    BOOL    trace_topdown;  /* Is TRACE output top-down or bottom-up? */
    char    *uncompress;     /* program to run to uncompress */
    BOOL    use_hostname;   /* TRUE = use machine NAME rather than quad */
    BOOL    use_http;       /* Should we allow http access? */
    int     vattr_flags;    /* Attr flags for all user-defined attrs */
    int     waitcost;       /* cost of @wait (refunded when finishes) */
    int     wild_invk_lim;  // Max Regular Expression function calls.
    char    *wizmotd_file;   /* display this file on login to wizards */
    char    wizmotd_msg[GBUF_SIZE];  /* Login message for wizards only */
    int     zone_nest_lim;  /* Max nesting of zones */
};

extern CONFDATA mudconf;

typedef struct site_data SITE;
struct site_data
{
    struct site_data *next;     /* Next site in chain */
    struct in_addr address;     /* Host or network address */
    struct in_addr mask;        /* Mask to apply before comparing */
    int flag;           /* Value to return on match */
};

typedef struct objlist_block OBLOCK;
struct objlist_block
{
    struct objlist_block *next;
    dbref   data[(LBUF_SIZE - sizeof(OBLOCK *)) / sizeof(dbref)];
};

#define OBLOCK_SIZE ((LBUF_SIZE - sizeof(OBLOCK *)) / sizeof(dbref))

typedef struct objlist_stack OLSTK;
struct objlist_stack
{
    struct objlist_stack *next; /* Next object list in stack */
    OBLOCK  *head;              /* Head of object list */
    OBLOCK  *tail;              /* Tail of object list */
    OBLOCK  *cblock;            /* Current block for scan */
    unsigned int count;         /* Number of objs in last obj list block */
    unsigned int citm;          /* Current item for scan */
};

typedef struct markbuf MARKBUF;
struct markbuf
{
    char    chunk[5000];
};

typedef struct alist ALIST;
struct alist
{
    char    *data;
    size_t len;
    struct alist *next;
};

typedef struct badname_struc BADNAME;
struct badname_struc
{
    char    *name;
    struct badname_struc    *next;
};

typedef struct forward_list FWDLIST;
struct forward_list
{
    int count;
    int data[1000];
};

#define MAX_ITEXT 100

typedef struct statedata STATEDATA;
struct statedata
{
    CLinearTimeAbsolute now;            /* What time is it now? */
    CLinearTimeAbsolute dump_counter;   /* Countdown to next db dump */
    CLinearTimeAbsolute check_counter;  /* Countdown to next db check */
    CLinearTimeAbsolute idle_counter;   /* Countdown to next idle check */
    CLinearTimeAbsolute events_counter; /* Countdown to next events check */
    CLinearTimeAbsolute start_time;     /* When was MUX started */
    CLinearTimeAbsolute cpu_count_from; /* When did we last reset CPU counters? */

    BOOL bStandAlone;       // Are we running in dbconvert mode.
    int record_players; /* The maximum # of player logged on */

    BOOL bReadingConfiguration;  // are we reading the config file at startup?
    BOOL bCanRestart;            // are we ready to even attempt a restart.
    BOOL panicking;              // are we in the middle of dying horribly?
#ifndef WIN32
    BOOL restarting; /* Are we restarting? */
    BOOL dumping;    /* Are we dumping? */
#endif // !WIN32
    int epoch;      /* Generation number for dumps */
    int generation; /* DB global generation number */
    dbref   curr_enactor;   /* Who initiated the current command */
    dbref   curr_executor;  /* Who is running the current command */
    int events_flag;    /* Flags for check_events */
    BOOL    shutdown_flag;  /* Should interface be shut down? */
    char    *debug_cmd;     // The command we are executing (if any).
    char    *curr_cmd;      /* The current command */
    SITE    *access_list;   /* Access states for sites */
    SITE    *suspect_list;  /* Sites that are suspect */
    BADNAME *badname_head;  /* List of disallowed names */
    int mstat_ixrss[2]; /* Summed shared size */
    int mstat_idrss[2]; /* Summed private data size */
    int mstat_isrss[2]; /* Summed private stack size */
    int mstat_secs[2];  /* Time of samples */
    int mstat_curr; /* Which sample is latest */
    OLSTK   *olist;     /* Stack of object lists for nested searches */
    int     mail_db_top;    /* Like db_top */
    int     mail_db_size;   /* Like db_size */
    MENT    *mail_list;     /* The mail database */
    int     *guest_free;    /* Table to keep track of free guests */
    int     func_nest_lev;  /* Current nesting of functions */
    int     func_invk_ctr;  /* Functions invoked so far by this command */
    int     wild_invk_ctr;  // Regular Expression function calls.
    int     ntfy_nest_lev;  /* Current nesting of notifys */
    int     lock_nest_lev;  /* Current nesting of lock evals */
    char    *global_regs[MAX_GLOBAL_REGS];  /* Global registers */
    int     glob_reg_len[MAX_GLOBAL_REGS];  /* Length of strs */
    int     zone_nest_num;  /* Global current zone nest position */
    BOOL    inpipe;         /* Boolean flag for command piping */
    char    *pout;          /* The output of the pipe used in %| */
    char    *poutnew;       /* The output being build by the current command */
    char    *poutbufc;      /* Buffer position for poutnew */
    dbref   poutobj;        /* Object doing the piping */
    int     in_loop;        // Loop nesting level.
    char    *itext[MAX_ITEXT];       // Text of iter(). Equivalent to ##.
    int     inum[MAX_ITEXT];        // Number of iter(). Equivalent to #@.

    CHashTable command_htab;   /* Commands hashtable */
    CHashTable channel_htab;   /* Channels hashtable */
    CHashTable mail_htab;      /* Mail players hashtable */
    CHashTable logout_cmd_htab;/* Logged-out commands hashtable (WHO, etc) */
    CHashTable func_htab;      /* Functions hashtable */
    CHashTable ufunc_htab;     /* Local functions hashtable */
    CHashTable powers_htab;    /* Powers hashtable */
    CHashTable flags_htab;     /* Flags hashtable */
    CHashTable player_htab;    /* Player name->number hashtable */
    CHashTable desc_htab;      /* Socket descriptor hashtable */
    CHashTable fwdlist_htab;   /* Room forwardlists */
    CHashTable parent_htab;    /* Parent $-command exclusion */
#ifdef PARSE_TREES
    CHashTable tree_htab;      /* Parse trees for evaluation */
#endif // PARSE_TREES
    CHashTable acache_htab;    // Attribute Cache

    HELP_DESC *aHelpDesc;      // Table of help files hashes.
    int     mHelpDesc;         // Number of entries allocated.
    int     nHelpDesc;         // Number of entries used.

    char    version[128];      /* MUX version string */
    char    short_ver[64];     /* Short version number (for INFO) */
    char    doing_hdr[SIZEOF_DOING_STRING];  /* Doing column header in the WHO display */
    int     nObjEvalNest;      // The nesting level of objeval()
                               // invocations.
    BOOL    bStackLimitReached;// Was stack slammed?
    int     nStackNest;        // Current stack depth.

    int     logging;    /* Are we in the middle of logging? */
    int     attr_next;  /* Next attr to alloc when freelist is empty */
    int     min_size;   /* Minimum db size (from file header) */
    int     db_top;     /* Number of items in the db */
    int     db_size;    /* Allocated size of db structure */
    dbref   freelist;   /* Head of object freelist */
    MARKBUF *markbits;  /* temp storage for marking/unmarking */

    ALIST   iter_alist;     /* Attribute list for iterations */
    char    *mod_alist; /* Attribute list for modifying */
    size_t  mod_alist_len; /* Length of mod_alist */
    size_t  mod_size;   /* Length of modified buffer */
    dbref   mod_al_id;  /* Where did mod_alist come from? */
    CHashTable attr_name_htab; /* Attribute names hashtable */
    CHashTable vattr_name_htab;/* User attribute names hashtable */
};

extern STATEDATA mudstate;

/* Configuration parameter handler definition */

#define CF_HAND(proc)   int proc(int *vp, char *str, void *pExtra, UINT32 nExtra, dbref player, char *cmd)

/* Global flags */

// Game control flags in mudconf.control_flags.
//
#define CF_LOGIN        0x0001      /* Allow nonwiz logins to the mux */
#define CF_BUILD        0x0002      /* Allow building commands */
#define CF_INTERP       0x0004      /* Allow object triggering */
#define CF_CHECKPOINT   0x0008      /* Perform auto-checkpointing */
#define CF_DBCHECK      0x0010      /* Periodically check/clean the DB */
#define CF_IDLECHECK    0x0020      /* Periodically check for idle users */
#define CF_GUEST        0x0040      /* Allow guest logins to the mux */
/* empty        0x0080 */
#define CF_DEQUEUE      0x0100      /* Remove entries from the queue */
#ifdef MUSH3
#define CF_GODMONITOR   0x0200      // Display commands to the god.
#endif // MUSH3
#define CF_EVENTCHECK   0x0400      // Allow events checking.

// Host information codes
//
#define H_REGISTRATION  0x0001  /* Registration ALWAYS on */
#define H_FORBIDDEN     0x0002  /* Reject all connects */
#define H_SUSPECT       0x0004  /* Notify wizards of connects/disconnects */
#define H_GUEST         0x0008  // Don't permit guests from here
#define H_NOSITEMON     0x0010  // Block SiteMon Information

// Event flags, for noting when an event has taken place.
//
#define ET_DAILY        0x00000001  /* Daily taken place? */

/* Logging options */

#define LOG_ALLCOMMANDS 0x00000001  /* Log all commands */
#define LOG_ACCOUNTING  0x00000002  /* Write accounting info on logout */
#define LOG_BADCOMMANDS 0x00000004  /* Log bad commands */
#define LOG_BUGS        0x00000008  /* Log program bugs found */
#define LOG_DBSAVES     0x00000010  /* Log database dumps */
#define LOG_CONFIGMODS  0x00000020  /* Log changes to configuration */
#define LOG_PCREATES    0x00000040  /* Log character creations */
#define LOG_KILLS       0x00000080  /* Log KILLs */
#define LOG_LOGIN       0x00000100  /* Log logins and logouts */
#define LOG_NET         0x00000200  /* Log net connects and disconnects */
#define LOG_SECURITY    0x00000400  /* Log security-related events */
#define LOG_SHOUTS      0x00000800  /* Log shouts */
#define LOG_STARTUP     0x00001000  /* Log nonfatal errors in startup */
#define LOG_WIZARD      0x00002000  /* Log dangerous things */
#define LOG_ALLOCATE    0x00004000  /* Log alloc/free from buffer pools */
#define LOG_PROBLEMS    0x00008000  /* Log runtime problems */
#define LOG_SUSPECTCMDS 0x00020000  // Log SUSPECT player keyboard commands.
#ifdef MUSH3
#define LOG_KBCOMMANDS  0x00010000  // Log keyboard commands.
#endif // MUSH3
#define LOG_TIMEUSE     0x00040000  // Log CPU time usage.
#define LOG_ALWAYS      0x80000000  /* Always log it */

#define LOGOPT_FLAGS        0x01    /* Report flags on object */
#define LOGOPT_LOC          0x02    /* Report loc of obj when requested */
#define LOGOPT_OWNER        0x04    /* Report owner of obj if not obj */
#define LOGOPT_TIMESTAMP    0x08    /* Timestamp log entries */

#endif // !__CONF_H
