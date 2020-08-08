/*! \file mudconf.h
 * \brief Definitions of global state and configuration structures.
 *
 */

#ifndef __CONF_H
#define __CONF_H

#include "alloc.h"
#include "htab.h"
#include "utf8tables.h"
#include "stringutil.h"

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
    const UTF8 *CommandName;
    CHashTable *ht;
    UTF8       *pBaseFilename;
    bool       bEval;
} HELP_DESC;

typedef struct confdata CONFDATA;
struct confdata
{
    bool    autozone;           // New objects are automatically zoned.
    bool    cache_names;        /* Should object names be cached separately */
    bool    clone_copy_cost;    /* Does @clone copy value? */
    bool    compress_db;        // should we use compress.
    bool    dark_sleepers;      /* Are sleeping players 'dark'? */
    bool    destroy_going_now;  // Does GOING act like DESTROY_OK?
    bool    eval_comtitle;      /* Should Comtitles Evaluate? */
    bool    ex_flags;           /* true = show flags on examine */
    bool    exam_public;        /* Does EXAM show public attrs by default? */
    bool    fascist_tport;      /* Src of teleport must be owned/JUMP_OK */
    bool    fork_dump;          // perform dump in a forked process.
    bool    have_comsys;        // Should the comsystem be active?
    bool    have_mailer;        // Should @mail be active?
    bool    have_zones;         // Should zones be active?
    bool    idle_wiz_dark;      /* Do idling wizards get set dark? */
    bool    indent_desc;        // Newlines before and after descs?
    bool    match_mine;         /* Should you check yourself for $-commands? */
    bool    match_mine_pl;      /* Should players check selves for $-cmds? */
    bool    name_spaces;        // allow player names to have spaces.
    bool    paranoid_alloc;     /* Rigorous buffer integrity checks */
    bool    pemit_any;          /* Can you @pemit to ANY remote object? */
    bool    pemit_players;      /* Can you @pemit to faraway players? */
    bool    player_listen;      /* Are AxHEAR triggered on players? */
    bool    pub_flags;          /* true = flags() works on anything */
    bool    quiet_look;         /* true = don't see attribs when looking */
    bool    quiet_whisper;      /* Can others tell when you whisper? */
    bool    quotas;             /* true = have building quotas */
    bool    read_rem_desc;      /* Can the DESCs of nonlocal objs be read? */
    bool    read_rem_name;      /* Can the NAMEs of nonlocal objs be read? */
    bool    reset_players;      // Reset the maximum player stat.
    bool    robot_speak;        /* true = allow robots to speak */
    bool    run_startup;        // If no, startup attributes aren't processed on load.
    bool    safe_unowned;       /* Are objects not owned by you safe? */
    bool    safe_wipe;          // If yes, SAFE flag must be removed to @wipe.
    bool    safer_passwords;    /* enforce reasonably good password choices? */
    bool    see_own_dark;       /* Do you see your own dark stuff? */
    bool    space_compress;     /* Convert multiple spaces into one space */
    bool    sweep_dark;         /* Can you sweep dark places? */
    bool    switch_df_all;      /* Should @switch match all by default? */
    bool    terse_contents;     /* Does TERSE look show exits */
    bool    terse_exits;        /* Does TERSE look show obvious exits */
    bool    terse_look;         /* Does manual look obey TERSE */
    bool    terse_movemsg;      /* Show move msgs (SUCC/LEAVE/etc) if TERSE? */
    bool    trace_topdown;      /* Is TRACE output top-down or bottom-up? */
    bool    use_hostname;       /* true = use machine NAME rather than quad */
    dbref   default_home;       // HOME when home is inaccessable.
    dbref   exit_parent;        // Default parent for new exit objects
    dbref   global_error_obj;   // Object that is used to generate error messages.
    dbref   guest_char;         // player num of prototype GUEST character.
    dbref   guest_nuker;        // Wiz who nukes the GUEST characters.
    dbref   help_executor;      // Dbref of exec object for eval'd helpfiles
    dbref   hook_obj;           // Object with @hook data.
    dbref   master_room;        // Room containing default cmds/exits/etc.
    dbref   player_parent;      // Default parent for new player objects
    dbref   room_parent;        // Default parent for new room objects
    dbref   start_home;         // initial HOME for players.
    dbref   start_room;         // initial location and home for players.
    dbref   thing_parent;       // Default parent for new thing objects
    dbref   toad_recipient;     /* Default @toad recipient. */

    int     active_q_chunk;     /* # cmds to run from queue when active */
    int     cache_pages;        // Size of hash page cache (in pages).
    int     check_interval;     /* interval between db check/cleans in secs */
    int     check_offset;       /* when to perform first check and clean */
    int     cmd_quota_incr;     /* Bump #cmds allowed by this each timeslice */
    int     cmd_quota_max;      /* Max commands at one time */
    int     conn_timeout;       /* Allow this long to connect before booting */
    int     control_flags;      /* Global runtime control flags */
    int     createmax;          /* max cost of @create command */
    int     createmin;          /* default (and minimum) cost of @create cmd */
    int     digcost;            /* cost of @dig command */
    int     dump_interval;      /* interval between ckp dumps in seconds */
    int     dump_offset;        /* when to take first checkpoint dump */
    int     events_daily_hour;  /* At what hour should @daily be executed? */
    int     exit_quota;         /* quota needed to make an exit */
    int     func_invk_lim;      /* Max funcs invoked by a command */
    int     func_nest_lim;      /* Max nesting of functions */
    int     idle_interval;      /* when to check for idle users */
    int     idle_timeout;       /* Boot off players idle this long in secs */
    int     init_size;          // initial db size.
    int     keepalive_interval; /* when to send keep alive */
    int     killguarantee;      /* cost of kill cmd that guarantees success */
    int     killmax;            /* max cost of kill command */
    int     killmin;            /* default (and minimum) cost of kill cmd */
    int     linkcost;           /* cost of @link command */
    int     lock_nest_lim;      /* Max nesting of lock evals */
    int     log_info;           /* Info that goes into log entries */
    int     log_options;        /* What gets logged */
    int     machinecost;        /* One in mc+1 cmds costs 1 penny (POW2-1) */
    int     mail_expiration;    /* Number of days to wait to delete mail */
    int     mail_per_hour;      // Maximum sent @mail per hour per object.
    int     max_players;        /* Max # of connected players */
    int     min_guests;         // The # we should start nuking at.
    int     nStackLimit;        // Current stack limit.
    int     attr_name_charset;  // Charset restrictions for attribute names.
    int     exit_name_charset;  // Charset restrictions for exit names.
    int     player_name_charset; // Charset restrictions for player names.
    int     room_name_charset;  // Charset restrictions for room names.
    int     thing_name_charset; // Charset restrictions for thing names.
    int     password_methods;   // Password encryption methods.
    int     default_charset;    // Default client charset mapping.
#ifdef REALITY_LVLS
    int     no_levels;          /* Number of reality levels */
    struct  rlevel_def
    {
        UTF8 name[9];           /* Rlevel name */
        RLEVEL value;           /* Rlevel bitmask */
        UTF8 attr[33];          /* desc attribute */
    } reality_level[32];        /* Reality levels */
    RLEVEL  def_room_rx;        /* Default room RX level */
    RLEVEL  def_room_tx;        /* Default room TX level */
    RLEVEL  def_player_rx;      /* Default player RX level */
    RLEVEL  def_player_tx;      /* Default player RX level */
    RLEVEL  def_exit_rx;        /* Default exit RX level */
    RLEVEL  def_exit_tx;        /* Default exit TX level */
    RLEVEL  def_thing_rx;       /* Default thing RX level */
    RLEVEL  def_thing_tx;       /* Default thing TX level */
#endif // REALITY_LVLS
    int     ntfy_nest_lim;      /* Max nesting of notifys */
    int     number_guests;      // number of guest characters allowed.
    int     opencost;           /* cost of @open command */
    int     output_limit;       /* Max # chars queued for output */
    int     pagecost;           /* cost of @page command */
    int     parent_nest_lim;    /* Max levels of parents */
    int     paycheck;           /* players earn this much each day connected */
    int     payfind;            /* chance to find a penny with wandering */
    int     paylimit;           /* getting money gets hard over this much */
    int     paystart;           /* new players start with this much money */
    int     player_quota;       /* quota needed to make a robot player */
    int     pcreate_per_hour;   // Maximum allowed players created per hour */
    int     queue_chunk;        /* # cmds to run from queue when idle */
    int     queuemax;           /* max commands a player may have in queue */
    int     references_per_hour;/* Maximum allowed @reference adds per hour per object */
    int     retry_limit;        /* close conn after this many bad logins */
    int     robotcost;          /* cost of @robot command */
    int     room_quota;         /* quota needed to make a room */
    int     sacadjust;          /* sacrifice earns (obj_cost/sfactor) + sadj */
    int     sacfactor;          /* ... */
    int     searchcost;         /* cost of commands that search the whole DB */
    int     sig_action;         // What to do with fatal signals.
    int     stack_limit;        /* How big can stacks get? */
    int     start_quota;        /* Quota for new players */
    int     thing_quota;        /* quota needed to make a thing */
    int     trace_limit;        /* Max lines of trace output if top-down */
    int     vattr_flags;        /* Attr flags for all user-defined attrs */
    int     vattr_per_hour;     // Maximum allowed vattrs per hour per object.
    int     waitcost;           /* cost of @wait (refunded when finishes) */
    int     wild_invk_lim;      // Max Regular Expression function calls.
    int     zone_nest_lim;      /* Max nesting of zones */
    int     restrict_home;      // Special condition to restrict 'home' command
    int     float_precision;    // Maximum precision of float-to-string conversion.
    int     lbuf_size;          // LBUF_SIZE accessible to softcode.

    unsigned int    max_cache_size; /* Max size of attribute cache */
    unsigned int    site_chars; // where to truncate site name.

    IntArray    ports;          // user ports.
#ifdef UNIX_SSL
    IntArray    sslPorts;       // SSL ports

    // Due to OpenSSL requirements, these have to be char, NOT UTF8.  Sorry. :(
    UTF8    ssl_certificate_file[128];      // SSL certificate file (.pem format)
    UTF8    ssl_certificate_key[128];       // SSL certificate private key file (.pem format)
    UTF8    ssl_certificate_password[128];  // SSL certificate private key password
#endif

    UTF8    guest_prefix[32];   /* Prefix for the guest char's name */
    UTF8    guests_channel[32]; /* Name of guests channel */
    UTF8    guests_channel_alias[32]; /* Name of guests channel alias */
    UTF8    many_coins[32];     /* name of many coins (ie. "pennies") */
    UTF8    mud_name[32];       /* Name of the mud */
    UTF8    one_coin[32];       /* name of one coin (ie. "penny") */
    UTF8    public_channel[32]; /* Name of public channel */
    UTF8    public_channel_alias[32]; /* Name of public channel alias */
    UTF8    dump_msg[256];      /* Message displayed when @dump-ing */
    UTF8    fixed_home_msg[128];    /* Message displayed when going home and FIXED */
    UTF8    fixed_tel_msg[128]; /* Message displayed when teleporting and FIXED */
    UTF8    postdump_msg[256];  /* Message displayed after @dump-ing */
#ifdef FIRANMUX
    UTF8    immobile_msg[128];  /* Message displayed to immobile players */
#endif // FIRANMUX

    UTF8    sql_server[128];
    UTF8    sql_user[128];
    UTF8    sql_password[128];
    UTF8    sql_database[128];

    UTF8    mail_server[128];
    UTF8    mail_ehlo[128];
    UTF8    mail_sendaddr[128];
    UTF8    mail_sendname[128];
    UTF8    mail_subject[128];

    UTF8    crash_msg[GBUF_SIZE];       /* Notification message on signals */
    UTF8    downmotd_msg[GBUF_SIZE];    /* Settable 'logins disabled' message */
    UTF8    fullmotd_msg[GBUF_SIZE];    /* Settable 'Too many players' message */
    UTF8    motd_msg[GBUF_SIZE];    /* Wizard-settable login message */
    UTF8    pueblo_msg[GBUF_SIZE];  /* Message displayed to Pueblo clients */
    UTF8    wizmotd_msg[GBUF_SIZE]; /* Login message for wizards only */
    UTF8    *compress;          /* program to run to compress */
    UTF8    *comsys_db;         /* name of the comsys db */
    UTF8    *config_file;       /* name of config file, used by @restart */
    UTF8    *conn_file;         /* display on connect if no registration */
    UTF8    *crashdb;           /* write database here on crash */
    UTF8    *crea_file;         /* display this on login for new users */
    UTF8    *creg_file;         /* display on connect if registration */
    UTF8    *down_file;         /* display this file if no logins */
    UTF8    *full_file;         /* display when max users exceeded */
    UTF8    *game_dir;          /* use this game CHashFile DIR file if we need one */
    UTF8    *game_pag;          /* use this game CHashFile PAG file if we need one */
    UTF8    *guest_file;        /* display if guest connects */
    UTF8    *indb;              /* database file name */
    UTF8    *log_dir;           /* directory for logging from the cmd line */
    UTF8    *mail_db;           /* name of the @mail database */
    UTF8    *motd_file;         /* display this file on login */
    UTF8    *outdb;             /* checkpoint the database to here */
    UTF8    *quit_file;         /* display on quit */
    UTF8    *regf_file;         /* display on (failed) create if reg is on */
    UTF8    *site_file;         /* display if conn from bad site */
    UTF8    *status_file;       /* Where to write arg to @shutdown */
    UTF8    *uncompress;        /* program to run to uncompress */
    UTF8    *wizmotd_file;      /* display this file on login to wizards */
    UTF8    *ip_address;        /* MUX IP Address */
    const UTF8 *pid_file;       // file for communicating process id back to ./Startmux
    unsigned char    markdata[8];  /* Masks for marking/unmarking */
    CLinearTimeDelta rpt_cmdsecs;  /* Reporting Threshhold for time taken by command */
    CLinearTimeDelta max_cmdsecs;  /* Upper Limit for real time taken by command */
    CLinearTimeDelta cache_tick_period; // Minor cycle for cache maintenance.
    CLinearTimeDelta timeslice;         // How often do we bump people's cmd quotas?

    FLAGSET exit_flags;         /* Flags exits start with */
    FLAGSET player_flags;       /* Flags players start with */
    FLAGSET robot_flags;        /* Flags robots start with */
    FLAGSET room_flags;         /* Flags rooms start with */
    FLAGSET thing_flags;        /* Flags things start with */
    FLAGSET stripped_flags;     // Flags stripped by @chown, @chownall, and @clone.

    ArtRuleset* art_rules;      /* Rulesets for defining exceptions. */
};

extern CONFDATA mudconf;

// Subnets
//
class mux_subnet_node
{
public:
    mux_subnet_node(mux_subnet *msn, unsigned long ulControl);
    ~mux_subnet_node();

private:
    mux_subnet      *msn;
    mux_subnet_node *pnLeft;
    mux_subnet_node *pnInside;
    mux_subnet_node *pnRight;
    unsigned long    ulControl;

    friend class mux_subnets;
};

// Host control codes
//
#define HC_PERMIT        0x00000001UL  // Clears HI_REGISTER, HI_FORBID
#define HC_REGISTER      0x00000002UL  // Sets   HI_REGISTER
#define HC_FORBID        0x00000004UL  // Sets   HI_FORBID
#define HC_NOSITEMON     0x00000008UL  // Sets   HI_NOSITEMON
#define HC_SITEMON       0x00000010UL  // Clears HI_NOSITEMON
#define HC_NOGUEST       0x00000020UL  // Sets   HI_NOGUEST
#define HC_GUEST         0x00000040UL  // Clears HI_NOGUEST
#define HC_SUSPECT       0x00000080UL  // Sets   HI_SUSPECT
#define HC_TRUST         0x00000100UL  // Clears HI_SUSPECT
#define HC_RESET         0x00000200UL  // Removes matching subnets.

// Host information codes
//
#define HI_PERMIT        0x0000
#define HI_REGISTER      0x0001  // Registration on
#define HI_FORBID        0x0002  // Reject all connects
#define HI_SUSPECT       0x0004  // Notify wizards of connects/disconnects
#define HI_NOGUEST       0x0008  // Don't permit guests from here
#define HI_NOSITEMON     0x0010  // Disable SiteMon Information

class mux_subnets
{
public:
    // Master subnet removal.  This resets back to the defaults
    //
    bool reset(mux_subnet *msn);

    // Permit Group: permit, registered, and forbid
    //
    bool permit(mux_subnet *msn);
    bool registered(mux_subnet *msn);
    bool forbid(mux_subnet *msn);

    // Sitemon Group: sitemon, nositemon
    //
    bool nositemon(mux_subnet *msn);
    bool sitemon(mux_subnet *msn);

    // Guest Group: guest, noguest
    //
    bool noguest(mux_subnet *msn);
    bool guest(mux_subnet *msn);

    // Suspect Group: suspect, trust
    //
    bool suspect(mux_subnet *msn);
    bool trust(mux_subnet *msn);

    // Queries: registered, forbid, suspect, noguest, nositemon.
    //
    bool isRegistered(MUX_SOCKADDR *pmsa);
    bool isForbid(MUX_SOCKADDR *pmsa);
    bool isSuspect(MUX_SOCKADDR *pmsa);
    bool isNoGuest(MUX_SOCKADDR *pmsa);
    bool isNoSiteMon(MUX_SOCKADDR *pmsa);

    // Returns hosting information codes corresponding to all the above queries at once time.
    //
    int  check(MUX_SOCKADDR *pmsa);

    void listinfo(dbref player);
    void listinfo(dbref player, UTF8 *sLine, UTF8 *sAddress, UTF8 *sControl, mux_subnet_node *p);

    mux_subnets();
    ~mux_subnets();

private:
    mux_subnet_node *msnRoot;
    void insert(mux_subnet_node **msnRoot, mux_subnet_node *msn);
    void search(mux_subnet_node *msnRoot, MUX_SOCKADDR *msa, unsigned long *pulInfo);
    mux_subnet_node *remove(mux_subnet_node *msnRoot, mux_subnet *msn_arg);

    mux_subnet_node *rotr(mux_subnet_node *msnRoot);
    mux_subnet_node *rollallr(mux_subnet_node *msnRoot);
    mux_subnet_node *joinlr(mux_subnet_node *a, mux_subnet_node *b);
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
    unsigned char *data;
    size_t len;
    struct alist *next;
};

typedef struct badname_struc BADNAME;
struct badname_struc
{
    UTF8    *name;
    struct badname_struc    *next;
};

typedef struct forward_list FWDLIST;
struct forward_list
{
    int count;
    dbref *data;
};

typedef struct attr_permission_list ATTRPERM;
struct attr_permission_list
{
    UTF8    *wildcard;
    int      flags;
    struct attr_permission_list *next;
};

#define MAX_ITEXT 100

typedef struct statedata STATEDATA;
struct statedata
{
    bool bCanRestart;           // are we ready to even attempt a restart.
    bool bReadingConfiguration; // are we reading the config file at startup?
    bool bStackLimitReached;    // Was stack slammed?
    bool bStandAlone;           // Are we running in dbconvert mode.
    bool panicking;             // are we in the middle of dying horribly?
    bool shutdown_flag;         // Should interface be shut down?
    bool inpipe;                // Are we collecting output for a pipe?
#if defined(HAVE_WORKING_FORK)
    bool          restarting;   // Are we restarting?
    volatile bool dumping;      // Are we dumping?
    volatile pid_t dumper;      // PID of dumping process (as returned by fork()).
    volatile pid_t dumped;      // PID of dumping process (as given by SIGCHLD).
    bool    write_protect;      // Write-protect against modifications to the
                                // database during dumps.
#endif // HAVE_WORKING_FORK

    dbref   curr_enactor;       /* Who initiated the current command */
    dbref   curr_executor;      /* Who is running the current command */
    dbref   freelist;           /* Head of object freelist */
    dbref   mod_al_id;          /* Where did mod_alist come from? */
    dbref   poutobj;            /* Object doing the piping */
    int     asserting;          // Are we in the middle of asserting?
    int     attr_next;          /* Next attr to alloc when freelist is empty */
    int     db_size;            /* Allocated size of db structure */
    int     db_top;             /* Number of items in the db */
    int     epoch;              /* Generation number for dumps */
    int     events_flag;        /* Flags for check_events */
    int     func_invk_ctr;      /* Functions invoked so far by this command */
    int     func_nest_lev;      /* Current nesting of functions */
    int     generation;         /* DB global generation number */
    int     in_loop;            // Loop nesting level.
    int     lock_nest_lev;      /* Current nesting of lock evals */
    int     logging;            /* Are we in the middle of logging? */
    int     mail_db_size;       /* Like db_size */
    int     mail_db_top;        /* Like db_top */
    int     mHelpDesc;          // Number of entries allocated.
    int     min_size;           /* Minimum db size (from file header) */
    int     mstat_curr;         /* Which sample is latest */
    int     nHelpDesc;          // Number of entries used.
    int     nObjEvalNest;       // The nesting level of objeval() invocations.
    int     nStackNest;         // Current stack depth.
    int     nHearNest;          // Current aahear depth.
    int     pipe_nest_lev;      // Number of piped commands.
    int     pcreates_this_hour; // Player creations possible this hour.
    int     ntfy_nest_lev;      // Current nesting of notifys.
    int     train_nest_lev;     // Current nesting of train.
    int     record_players;     // The maximum # of player logged on.
    int     wild_invk_ctr;      // Regular Expression function calls.
    int     zone_nest_num;      /* Global current zone nest position */
    int     mstat_idrss[2];     /* Summed private data size */
    int     mstat_isrss[2];     /* Summed private stack size */
    int     mstat_ixrss[2];     /* Summed shared size */
    int     mstat_secs[2];      /* Time of samples */
    int     inum[MAX_ITEXT];    // Number of iter(). Equivalent to #@.
    int     *guest_free;        /* Table to keep track of free guests */
    size_t  mod_alist_len;      /* Length of mod_alist */
    size_t  mod_size;           /* Length of modified buffer */
    unsigned int restart_count; // Number of @restarts since initial startup

    UTF8    short_ver[64];      /* Short version number (for INFO) */
    UTF8    doing_hdr[SIZEOF_DOING_STRING];  /* Doing column header in the WHO display */
    UTF8    version[128];       /* MUX version string */
    const UTF8    *curr_cmd;    /* The current command */
    const UTF8    *debug_cmd;   // The command we are executing (if any).
    unsigned char *mod_alist;   /* Attribute list for modifying */
    UTF8    *pout;              /* The output of the pipe used in %| */
    UTF8    *poutbufc;          /* Buffer position for poutnew */
    UTF8    *poutnew;           /* The output being build by the current command */
    UTF8    *itext[MAX_ITEXT];  // Text of iter(). Equivalent to ##.

    reg_ref *global_regs[MAX_GLOBAL_REGS];  /* Global registers */
    ALIST   iter_alist;         /* Attribute list for iterations */
    BADNAME *badname_head;      /* List of disallowed names */
    HELP_DESC *aHelpDesc;       // Table of help files hashes.
    MARKBUF *markbits;          /* temp storage for marking/unmarking */
    OLSTK   *olist;             /* Stack of object lists for nested searches */
    mux_subnets access_list;    /* Access/suspect attributes for subnets */

    ATTRPERM *attrperm_list;    /* Wildcarded attribute permissions list */

#if defined(STUB_SLAVE)
    mux_ISlaveControl *pISlaveControl;  // Management interface for StubSlave process.
    CResultsSet *pResultsSet;           // ResultsSet from @query.
    int iRow;                           // Current Row.
#endif // STUB_SLAVE
    mux_IQueryControl *pIQueryControl;

    CLinearTimeAbsolute check_counter;  /* Countdown to next db check */
    CLinearTimeAbsolute cpu_count_from; /* When did we last reset CPU counters? */
    CLinearTimeAbsolute dump_counter;   /* Countdown to next db dump */
    CLinearTimeAbsolute events_counter; /* Countdown to next events check */
    CLinearTimeAbsolute idle_counter;   /* Countdown to next idle check */
    CLinearTimeAbsolute keepalive_counter; /* Countdown to next keep alive cycle */
    CLinearTimeAbsolute start_time;     /* When was MUX started */
    CLinearTimeAbsolute restart_time;   /* When was MUX restarted */
    CLinearTimeAbsolute tThrottleExpired; // How much time is left in this hour of throttling.

#if !defined(MEMORY_BASED)
    CHashTable acache_htab;     // Attribute Cache
#endif // MEMORY_BASED
    CHashTable attr_name_htab;  /* Attribute names hashtable */
    CHashTable channel_htab;    /* Channels hashtable */
    CHashTable command_htab;    /* Commands hashtable */
    CHashTable desc_htab;       /* Socket descriptor hashtable */
    CHashTable flags_htab;      /* Flags hashtable */
    CHashTable func_htab;       /* Functions hashtable */
    CHashTable fwdlist_htab;    /* Room forwardlists */
    CHashTable logout_cmd_htab; /* Logged-out commands hashtable (WHO, etc) */
    CHashTable mail_htab;       /* Mail players hashtable */
    CHashTable parent_htab;     /* Parent $-command exclusion */
    CHashTable player_htab;     /* Player name->number hashtable */
    CHashTable powers_htab;     /* Powers hashtable */
    CHashTable reference_htab;  /* @reference hashtable */
    CHashTable ufunc_htab;      /* Local functions hashtable */
    CHashTable vattr_name_htab; /* User attribute names hashtable */
    CHashTable scratch_htab;    /* Multi-purpose scratch hash table */

    CBitField bfNoListens;      // Cache knowledge that there are no ^-Commands.
    CBitField bfNoCommands;     // Cache knowledge that there are no $-Commands.
    CBitField bfCommands;       // Cache knowledge that there are $-Commands.
    CBitField bfListens;        // Cache knowledge that there are ^-Commands.

    CBitField bfReport;         // Used for LROOMS.
    CBitField bfTraverse;       // Used for LROOMS.
};

extern STATEDATA mudstate;

/* Configuration parameter handler definition */

#define CF_HAND(proc)   int proc(int *vp, UTF8 *str, void *pExtra, UINT32 nExtra, dbref player, UTF8 *cmd)

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
#define CF_EVENTCHECK   0x0400      // Allow events checking.

// Event flags, for noting when an event has taken place.
//
#define ET_DAILY        0x00000001  /* Daily taken place? */

#define LOGOPT_FLAGS        0x01    /* Report flags on object */
#define LOGOPT_LOC          0x02    /* Report loc of obj when requested */
#define LOGOPT_OWNER        0x04    /* Report owner of obj if not obj */
#define LOGOPT_TIMESTAMP    0x08    /* Timestamp log entries */

#endif // !__CONF_H
