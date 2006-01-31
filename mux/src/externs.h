// externs.h -- Prototypes for externs not defined elsewhere.
//
// $Id: externs.h,v 1.68 2006-01-31 00:16:23 sdennis Exp $
//

#ifndef EXTERNS_H
#define EXTERNS_H

#include "db.h"
#include "match.h"
#include "mudconf.h"
#include "svdrand.h"

// From bsd.cpp.
//
void boot_slave(dbref executor, dbref caller, dbref enactor, int key);
void close_sockets(bool emergency, char *message);
void CleanUpSlaveSocket(void);
void CleanUpSlaveProcess(void);
#ifdef QUERY_SLAVE
void CleanUpSQLSlaveSocket(void);
void CleanUpSQLSlaveProcess(void);
#endif // QUERY_SLAVE
#ifdef WIN32
extern HANDLE CompletionPort;    // IOs are queued up on this port
extern CRITICAL_SECTION csDescriptorList;
#endif // WIN32

#ifdef QUERY_SLAVE
void boot_sqlslave(dbref executor, dbref caller, dbref enactor, int key);
#endif // QUERY_SLAVE

extern NAMETAB sigactions_nametab[];

// From conf.cpp
//
void cf_log_notfound(dbref, char *, const char *, char *);
int  cf_modify_bits(int *, char *, void *, UINT32, dbref, char *);
void DCL_CDECL cf_log_syntax(dbref player, char *cmd, const char *fmt, ...);
void ValidateConfigurationDbrefs(void);
int  cf_read(void);
void cf_init(void);
void cf_list(dbref, char *, char **);
void cf_display(dbref, char *, char *, char **);
void list_cf_access(dbref);
int cf_set(char *, char *, dbref);
CF_HAND(cf_cf_access);
CF_HAND(cf_access);
CF_HAND(cf_cmd_alias);
CF_HAND(cf_acmd_access);
CF_HAND(cf_attr_access);
CF_HAND(cf_func_access);
CF_HAND(cf_flag_access);
CF_HAND(cf_flag_name);
CF_HAND(cf_art_rule);

// From local.cpp.
//
void local_startup(void);
void local_presync_database(void);
void local_presync_database_sigsegv(void);
void local_dump_database(int);
void local_dump_complete_signal(void);
void local_shutdown(void);
void local_dbck(void);
void local_connect(dbref, int, int);
void local_disconnect(dbref, int);
void local_data_create(dbref);
void local_data_clone(dbref, dbref);
void local_data_free(dbref);

// From mail.cpp.
//
void load_mail(FILE *);
int  dump_mail(FILE *);
struct mail *mail_fetch(dbref, int);

// From netcommon.cpp.
//
void DCL_CDECL raw_broadcast(int, char *, ...);
void list_siteinfo(dbref);
void logged_out0(dbref executor, dbref caller, dbref enactor, int key);
void logged_out1(dbref executor, dbref caller, dbref enactor, int key, char *arg);
void init_logout_cmdtab(void);
void desc_reload(dbref);
void make_portlist(dbref, dbref, char *, char **);

/* From cque.cpp */
int  nfy_que(dbref, int, int, int);
int  halt_que(dbref, dbref);
void wait_que(dbref executor, dbref caller, dbref enactor, bool,
    CLinearTimeAbsolute&, dbref, int, char *, char *[],int, char *[]);

#ifndef WIN32
extern "C" char *crypt(const char *inptr, const char *inkey);
#endif // WIN32
extern bool break_called;

/* From eval.cpp */
void tcache_init(void);
char *parse_to(char **, char, int);
char *parse_arglist(dbref executor, dbref caller, dbref enactor, char *,
                    char, int, char *[], int, char*[], int, int *);
int get_gender(dbref);
void mux_exec(char *buff, char **bufc, dbref executor, dbref caller,
              dbref enactor, int eval, char **dstr, char *cargs[],
              int ncargs);
void save_global_regs(const char *, char *[], size_t []);
void save_and_clear_global_regs(const char *, char *[], size_t[]);
void restore_global_regs(const char *, char *[], size_t []);
char **PushPointers(int nNeeded);
void PopPointers(char **p, int nNeeded);
size_t *PushLengths(int nNeeded);
void PopLengths(size_t *pi, int nNeeded);
extern const signed char mux_RegisterSet[256];
extern const char *ColorTable[256];

/* From game.cpp */
#define notify(p,m)                         notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_saypose(p,m)                 notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_SAYPOSE)
#define notify_html(p,m)                    notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet(p,m)                   notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME)
#define notify_with_cause(p,c,m)            notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_with_cause_ooc(p,c,m)        notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_OOC)
#define notify_with_cause_html(p,c,m)       notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet_with_cause(p,c,m)      notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME)
//#define notify_puppet(p,c,m)                notify_check(p,c,m, MSG_ME_ALL|MSG_F_DOWN)
//#define notify_quiet_puppet(p,c,m)          notify_check(p,c,m, MSG_ME)
#define notify_all(p,c,m)                   notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS|MSG_F_UP|MSG_F_CONTENTS)
#define notify_all_from_inside(p,c,m)       notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE)
#define notify_all_from_inside_saypose(p,c,m) notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_SAYPOSE)
#define notify_all_from_inside_html(p,c,m)  notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_HTML)
//#define notify_all_from_outside(p,c,m)      notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS|MSG_F_UP|MSG_F_CONTENTS|MSG_S_OUTSIDE)

void notify_except(dbref, dbref, dbref, const char *, int key);
void notify_except2(dbref, dbref, dbref, dbref, const char *);

void notify_check(dbref, dbref, const char *, int);

bool Hearer(dbref);
void report(void);

bool atr_match
(
    dbref thing,
    dbref player,
    char  type,
    char  *str,
    char  *raw_str,
    bool  check_parents
);

bool regexp_match
(
    char *pattern,
    char *str,
    int case_opt,
    char *args[],
    int nargs
);

bool list_check
(
    dbref thing,
    dbref player,
    char  type,
    char  *str,
    char  *raw_str,
    bool  check_parent
);
bool html_escape(const char *src, char *dest, char **destp);

#define DUMP_I_NORMAL    0  // OUTPUT to the outdb through a temporary file.
#define DUMP_I_PANIC     1  // UNLOAD to a crashdb
#define DUMP_I_RESTART   2  // OUTPUT to the inputdb
#define DUMP_I_FLAT      3  // UNLOAD to a .FLAT file
#define DUMP_I_SIGNAL    4  // UNLOAD to a .FLAT file from signal.
#define NUM_DUMP_TYPES   5
void dump_database_internal(int);
void fork_and_dump(int key);

/* From help.cpp */
void helpindex_clean(int);
void helpindex_load(dbref);
void helpindex_init(void);
void help_helper(dbref executor, int iHelpfile, char *topic_arg, char *buff, char **bufc);

/* From htab.cpp */
int  cf_ntab_access(int *, char *, void *, UINT32, dbref, char *);

/* From log.cpp */
#ifdef WIN32
#define ENDLINE "\r\n"
#else // WIN32
#define ENDLINE "\n"
#endif // WIN32
bool start_log(const char *primary, const char *secondary);
void end_log(void);
void log_perror(const char *, const char *,const char *,
            const char *);
void log_text(const char *);
void log_number(int);
void DCL_CDECL log_printf(const char *fmt, ...);
void log_name(dbref);
void log_name_and_loc(dbref);
void log_type_and_name(dbref);

/* From look.cpp */
void look_in(dbref,dbref, int);
void show_vrml_url(dbref, dbref);
#define NUM_ATTRIBUTE_CODES 10
size_t decode_attr_flags(int aflags, char buff[NUM_ATTRIBUTE_CODES+1]);

/* From move.cpp */
void move_object(dbref, dbref);
void move_via_generic(dbref, dbref, dbref, int);
bool move_via_teleport(dbref, dbref, dbref, int);
void move_exit(dbref, dbref, bool, const char *, int);
void do_enter_internal(dbref, dbref, bool);

/* From object.cpp */
dbref start_home(void);
dbref default_home(void);
bool  can_set_home(dbref, dbref, dbref);
dbref new_home(dbref);
dbref clone_home(dbref, dbref);
void  divest_object(dbref);
dbref create_obj(dbref, int, const char *, int);
void  destroy_obj(dbref);
void  empty_obj(dbref);

/* From player.cpp */
dbref create_player(char *name, char *pass, dbref executor, bool isrobot, const char **pmsg);
void AddToPublicChannel(dbref player);
bool add_player_name(dbref, const char *);
bool delete_player_name(dbref, const char *);
dbref lookup_player(dbref, char *, bool);
void load_player_names(void);
void badname_add(char *);
void badname_remove(char *);
bool badname_check(char *);
void badname_list(dbref, const char *);
void ChangePassword(dbref player, const char *szPassword);
const char *mux_crypt(const char *szPassword, const char *szSalt, int *piType);
int  QueueMax(dbref);
int  a_Queue(dbref, int);
void pcache_reload(dbref);
void pcache_init(void);

/* From predicates.cpp */
char * DCL_CDECL tprintf(const char *, ...);
void DCL_CDECL safe_tprintf_str(char *, char **, const char *, ...);
dbref insert_first(dbref, dbref);
dbref remove_first(dbref, dbref);
dbref reverse_list(dbref);
bool member(dbref, dbref);
bool could_doit(dbref, dbref, int);
bool can_see(dbref, dbref, bool);
void add_quota(dbref, int);
bool canpayfees(dbref, dbref, int, int);
void giveto(dbref,int);
bool payfor(dbref,int);
char *MakeCanonicalObjectName(const char *pName, size_t *pnName, bool *pbValid);
char *MakeCanonicalExitName(const char *pName, size_t *pnName, bool *pbValid);
bool ValidatePlayerName(const char *pName);
bool ok_password(const char *szPassword, const char **pmsg);
void handle_ears(dbref, bool, bool);
dbref match_possessed(dbref, dbref, char *, dbref, bool);
void parse_range(char **, dbref *, dbref *);
bool parse_thing_slash(dbref, char *, char **, dbref *);
bool get_obj_and_lock(dbref, char *, dbref *, ATTR **, char *, char **);
dbref where_is(dbref);
dbref where_room(dbref);
bool locatable(dbref, dbref, dbref);
bool nearby(dbref, dbref);
bool exit_visible(dbref, dbref, int);
bool exit_displayable(dbref, dbref, int);
void did_it(dbref, dbref, int, const char *, int, const char *, int, char *[], int);
bool bCanReadAttr(dbref executor, dbref target, ATTR *tattr, bool bParentCheck);
bool bCanSetAttr(dbref executor, dbref target, ATTR *tattr);
bool bCanLockAttr(dbref executor, dbref target, ATTR *tattr);

/* From set.cpp */
bool parse_attrib(dbref, char *, dbref *, ATTR **);
bool parse_attrib_wild(dbref, char *, dbref *, bool, bool, bool);
void edit_string(char *, char **, char *, char *);
dbref match_controlled_handler(dbref player, const char *name, bool bQuiet);
#define match_controlled(player,name)       match_controlled_handler(player, name, false)
#define match_controlled_quiet(player,name) match_controlled_handler(player, name, true)
void set_modified(dbref thing);

/* From boolexp.cpp */
bool eval_boolexp(dbref, dbref, dbref, BOOLEXP *);
BOOLEXP *parse_boolexp(dbref, const char *, bool);
bool eval_boolexp_atr(dbref, dbref, dbref, char *);

/* From functions.cpp */
bool xlate(char *);

#define IEEE_MAKE_NAN  1
#define IEEE_MAKE_IND  2
#define IEEE_MAKE_PINF 3
#define IEEE_MAKE_NINF 4

double MakeSpecialFloat(int iWhich);

/* From unparse.cpp */
char *unparse_boolexp(dbref, BOOLEXP *);
char *unparse_boolexp_quiet(dbref, BOOLEXP *);
char *unparse_boolexp_decompile(dbref, BOOLEXP *);
char *unparse_boolexp_function(dbref, BOOLEXP *);

/* From walkdb.cpp */
int chown_all(dbref from_player, dbref to_player, dbref acting_player, int key);
void olist_push(void);
void olist_pop(void);
void olist_add(dbref);
dbref olist_first(void);
dbref olist_next(void);

/* From wild.cpp */
bool wild(char *, char *, char *[], int);
bool wild_match(char *, const char *);
bool quick_wild(const char *, const char *);

/* From command.cpp */
bool check_access(dbref player, int mask);
void set_prefix_cmds(void);
char *process_command(dbref executor, dbref caller, dbref enactor, bool,
    char *, char *[], int);

#define Protect(f) (cmdp->perms & f)

#define Invalid_Objtype(x) \
((Protect(CA_LOCATION) && !Has_location(x)) || \
 (Protect(CA_CONTENTS) && !Has_contents(x)) || \
 (Protect(CA_PLAYER) && !isPlayer(x)))

/* from db.cpp */
bool Commer(dbref);
void s_Pass(dbref, const char *);
void s_Name(dbref, const char *);
void s_Moniker(dbref thing, const char *s);
const char *Name(dbref thing);
const char *PureName(dbref thing);
const char *Moniker(dbref thing);
int  fwdlist_load(FWDLIST *, dbref, char *);
void fwdlist_set(dbref, FWDLIST *);
void fwdlist_clr(dbref);
int  fwdlist_rewrite(FWDLIST *, char *);
FWDLIST *fwdlist_get(dbref);
void atr_push(void);
void atr_pop(void);
int  atr_head(dbref, char **);
int  atr_next(char **);
int  init_dbfile(char *game_dir_file, char *game_pag_file, int nCachePages);
void atr_cpy(dbref dest, dbref source, bool bInternal);
void atr_chown(dbref);
void atr_clr(dbref, int);
void atr_add_raw_LEN(dbref, int, const char *, size_t n);
void atr_add_raw(dbref, int, const char *);
void atr_add(dbref, int, char *, dbref, int);
void atr_set_flags(dbref, int, int);
const char *atr_get_raw_LEN(dbref, int, size_t *);
const char *atr_get_raw(dbref, int);
char *atr_get_LEN(dbref, int, dbref *, int *, size_t *);
char *atr_get_real(dbref, int, dbref *, int *, const char *, const int);
#define atr_get(t,a,o,f) atr_get_real(t,a,o,f, __FILE__, __LINE__)
char *atr_pget_LEN(dbref, int, dbref *, int *, size_t *);
char *atr_pget_real(dbref, int, dbref *, int *, const char *, const int);
#define atr_pget(t,a,o,f) atr_pget_real(t,a,o,f, __FILE__, __LINE__)
char *atr_get_str_LEN(char *s, dbref, int, dbref *, int *, size_t *);
char *atr_get_str(char *, dbref, int, dbref *, int *);
char *atr_pget_str_LEN(char *, dbref, int, dbref *, int *, size_t *);
char *atr_pget_str(char *, dbref, int, dbref *, int *);
bool atr_get_info(dbref, int, dbref *, int *);
bool atr_pget_info(dbref, int, dbref *, int *);
void atr_free(dbref);
bool check_zone_handler(dbref player, dbref thing, bool bPlayerCheck);
#define check_zone(player, thing) check_zone_handler(player, thing, false)
void ReleaseAllResources(dbref obj);
bool fwdlist_ck(dbref player, dbref thing, int anum, char *atext);

extern int anum_alc_top;

/* Command handler keys */

#define ATTRIB_ACCESS   1   /* Change access to attribute */
#define ATTRIB_RENAME   2   /* Rename attribute */
#define ATTRIB_DELETE   4   /* Delete attribute */
#define ATTRIB_INFO     8   /* Info (number, flags) about attribute */
#define BOOT_QUIET      1   /* Inhibit boot message to victim */
#define BOOT_PORT       2   /* Boot by port number */
#define CEMIT_NOHEADER  1   /* Channel emit without header */
#define CHOWN_ONE       1   /* item = new_owner */
#define CHOWN_ALL       2   /* old_owner = new_owner */
//#define CHOWN_NOSTRIP   4   /* Don't strip (most) flags from object */
#define CHOWN_NOZONE    8   /* Strip zones from objects */
#define CLIST_FULL      1   /* Full listing of channels */
#define CLIST_HEADERS   2   /* Lists channel headers, like "[Public]" */
#define CLONE_LOCATION  0   /* Create cloned object in my location */
#define CLONE_INHERIT   1   /* Keep INHERIT bit if set */
#define CLONE_PRESERVE  2   /* Preserve the owner of the object */
#define CLONE_INVENTORY 4   /* Create cloned object in my inventory */
#define CLONE_SET_COST  8   /* ARG2 is cost of cloned object */
#define CLONE_SET_LOC   16  /* ARG2 is location of cloned object */
#define CLONE_SET_NAME  32  /* ARG2 is alternate name of cloned object */
#define CLONE_FROM_PARENT 64 /* Set parent on obj instd of cloning attrs */
#define CBOOT_QUIET     1   // Cboot without a message
#define COMTITLE_ON     1   // Turn Comtitles on.
#define COMTITLE_OFF    2   // Turn Comtitles off.
#define CRE_INVENTORY   0   /* Create object in my inventory */
#define CRE_LOCATION    1   /* Create object in my location */
#define CRE_SET_LOC     2   /* ARG2 is location of new object */
#define CSET_PUBLIC     0   /* Sets a channel public */
#define CSET_PRIVATE    1   /* Sets a channel private (default) */
#define CSET_LOUD       2   /* Channel shows connects and disconnects */
#define CSET_QUIET      3   /* Channel doesn't show connects/disconnects */
#define CSET_LIST       4   /* Lists channels */
#define CSET_OBJECT     5   /* Sets the channel object for the channel */
#define CSET_SPOOF      6   /* Sets the channel spoofable */
#define CSET_NOSPOOF    7   /* Sets the channel non-spoofable */
#define CSET_HEADER     8   /* Sets the channel header, like "[Public]" */
#define CSET_LOG        9   // Set maximum number of messages to log.
#define DBCK_DEFAULT    1   /* Get default tests too */
#define DBCK_FULL       2   /* Do all tests */
#define DECOMP_DBREF    1   /* decompile by dbref */
//#define DECOMP_PRETTY   2   /* pretty-format output */
#define DEST_ONE        1   /* object */
#define DEST_OVERRIDE   4   /* override Safe() */
#define DEST_INSTANT    8   /* instantly destroy */
#define DIG_TELEPORT    1   /* teleport to room after @digging */
#define DOLIST_SPACE    0   /* expect spaces as delimiter */
#define DOLIST_DELIMIT  1   /* expect custom delimiter */
#define DOLIST_NOTIFY   2   /* Send an @notify after the @dolist is completed */
#define DOING_MESSAGE   0   /* Set my DOING message */
#define DOING_HEADER    1   /* Set the DOING header */
#define DOING_POLL      2   /* List DOING header */
#define DOING_UNIQUE    3   // Set DOING message for current port only
#define DOING_MASK     15   // Lower four bits form non-SW_MULTIPLE part.
#define DOING_QUIET    16   // Set DOING messages without 'Set.' message.
#define DROP_QUIET      1   /* Don't do odrop/adrop if control */
#define DUMP_STRUCT     1   /* Dump flat structure file */
#define DUMP_TEXT       2   /* Dump to external attribute database. */
#define DUMP_FLATFILE   4   /* Dump .FLAT file */
#define EXAM_DEFAULT    0   /* Default */
#define EXAM_BRIEF      1   /* Nonowner sees just owner */
#define EXAM_LONG       2   /* Nonowner sees public attrs too */
#define EXAM_DEBUG      4   /* Display more info for finding db problems */
#define EXAM_PARENT     8   /* Get attr from parent when exam obj/attr */
//#define EXAM_PRETTY     16  /* Pretty-format output */
//#define EXAM_PAIRS      32  /* Print paren matches in color */
#define FIXDB_OWNER     1   /* Fix OWNER field */
#define FIXDB_LOC       2   /* Fix LOCATION field */
#define FIXDB_CON       4   /* Fix CONTENTS field */
#define FIXDB_EXITS     8   /* Fix EXITS field */
#define FIXDB_NEXT      16  /* Fix NEXT field */
#define FIXDB_PENNIES   32  /* Fix PENNIES field */
#define FIXDB_ZONE      64  /* Fix ZONE field */
#define FIXDB_LINK      128 /* Fix LINK field */
#define FIXDB_PARENT    256 /* Fix PARENT field */
#define FIXDB_NAME      2048 /* Set NAME attribute */
#define FLAG_REMOVE     1   // Remove a flag alias
#define GET_QUIET       1   /* Don't do osucc/asucc if control */
#define GIVE_QUIET      64  /* Inhibit give messages */
#define GLOB_ENABLE     1   /* key to enable */
#define GLOB_DISABLE    2   /* key to disable */
//#define GLOB_LIST       3   /* key to list */
#define HALT_ALL        1   /* halt everything */

#define HOOK_BEFORE     1   /* BEFORE hook */
#define HOOK_AFTER      2   /* AFTER hook */
#define HOOK_PERMIT     4   /* PERMIT hook */
#define HOOK_IGNORE     8   /* IGNORE hook */
#define HOOK_IGSWITCH   16  /* BFAIL hook */
#define HOOK_AFAIL      32  /* AFAIL hook */
#define HOOK_CLEAR      64  /* CLEAR hook */
#define HOOK_LIST       128 /* LIST hooks */
#define ICMD_DISABLE    0
#define ICMD_IGNORE     1
#define ICMD_ON         2
#define ICMD_OFF        4
#define ICMD_CLEAR      8
#define ICMD_CHECK      16
#define ICMD_DROOM      32
#define ICMD_IROOM      64
#define ICMD_CROOM      128
#define ICMD_LROOM      256
#define ICMD_LALLROOM   512
#define KILL_KILL       1   /* gives victim insurance */
#define KILL_SLAY       2   /* no insurance */
#define LOOK_LOOK       1   /* list desc (and succ/fail if room) */
#define LOOK_OUTSIDE    8   /* look for object in container of player */
#define MAIL_STATS      1   /* Mail stats */
#define MAIL_DSTATS     2   /* More mail stats */
#define MAIL_FSTATS     3   /* Even more mail stats */
#define MAIL_DEBUG      4   /* Various debugging options */
#define MAIL_NUKE       5   /* Nuke the post office */
#define MAIL_FOLDER     6   /* Do folder stuff */
#define MAIL_LIST       7   /* List @mail by time */
#define MAIL_READ       8   /* Read @mail message */
#define MAIL_CLEAR      9   /* Clear @mail message */
#define MAIL_UNCLEAR    10  /* Unclear @mail message */
#define MAIL_PURGE      11  /* Purge cleared @mail messages */
#define MAIL_FILE       12  /* File @mail in folders */
#define MAIL_TAG        13  /* Tag @mail messages */
#define MAIL_UNTAG      14  /* Untag @mail messages */
#define MAIL_FORWARD    15  /* Forward @mail messages */
#define MAIL_SEND       16  /* Send @mail messages in progress */
#define MAIL_EDIT       17  /* Edit @mail messages in progress */
#define MAIL_URGENT     18  /* Sends a @mail message as URGENT */
#define MAIL_ALIAS      19  /* Creates an @mail alias */
#define MAIL_ALIST      20  /* Lists @mail aliases */
#define MAIL_PROOF      21  /* Proofs @mail messages in progress */
#define MAIL_ABORT      22  /* Aborts @mail messages in progress */
#define MAIL_QUICK      23  /* Sends a quick @mail message */
#define MAIL_REVIEW     24  /* Reviews @mail sent to a player */
#define MAIL_RETRACT    25  /* Retracts @mail sent to a player */
#define MAIL_CC         26  /* Carbon copy */
#define MAIL_SAFE       27  /* Defines a piece of mail as safe. */
#define MAIL_REPLY      28  /* Replies to a message. */
#define MAIL_REPLYALL   29  /* Replies to all recipients of msg */
#define MAIL_BCC        30  // Blind Carbon Copy. Don't show the recipient list to these.
#define MAIL_QUOTE      0x100   /* Quote back original in the reply? */

#define MALIAS_DESC     1   /* Describes a mail alias */
#define MALIAS_CHOWN    2   /* Changes a mail alias's owner */
#define MALIAS_ADD      3   /* Adds a player to an alias */
#define MALIAS_REMOVE   4   /* Removes a player from an alias */
#define MALIAS_DELETE   5   /* Deletes a mail alias */
#define MALIAS_RENAME   6   /* Renames a mail alias */
#define MALIAS_LIST     8   /* Lists mail aliases */
#define MALIAS_STATUS   9   /* Status of mail aliases */
#define MARK_SET        0   /* Set mark bits */
#define MARK_CLEAR      1   /* Clear mark bits */
#define MOTD_ALL        0   /* login message for all */
#define MOTD_WIZ        1   /* login message for wizards */
#define MOTD_DOWN       2   /* login message when logins disabled */
#define MOTD_FULL       4   /* login message when too many players on */
#define MOTD_LIST       8   /* Display current login messages */
#define MOTD_BRIEF      16  /* Suppress motd file display for wizards */
#define MOVE_QUIET      1   /* Dont do osucc/ofail/asucc/afail if ctrl */
#define NFY_NFY         0   /* Notify first waiting command */
#define NFY_NFYALL      1   /* Notify all waiting commands */
#define NFY_DRAIN       2   /* Delete waiting commands */
#define NFY_QUIET       3   /* Suppress "Notified." message */
#define OPEN_LOCATION   0   /* Open exit in my location */
#define OPEN_INVENTORY  1   /* Open exit in me */
#define PCRE_PLAYER     1   /* create new player */
#define PCRE_ROBOT      2   /* create robot player */
#define PEMIT_PEMIT     1   /* emit to named player */
#define PEMIT_OEMIT     2   /* emit to all in current room except named */
#define PEMIT_WHISPER   3   /* whisper to player in current room */
#define PEMIT_FSAY      4   /* force controlled obj to say */
#define PEMIT_FEMIT     5   /* force controlled obj to emit */
#define PEMIT_FPOSE     6   /* force controlled obj to pose */
#define PEMIT_FPOSE_NS  7   /* force controlled obj to pose w/o space */
#define PEMIT_CONTENTS  8   /* Send to contents (additive) */
#define PEMIT_HERE      16  /* Send to location (@femit, additive) */
#define PEMIT_ROOM      32  /* Send to containing rm (@femit, additive) */
#define PEMIT_LIST      64  /* Send to a list */
#define PEMIT_HTML      128 /* HTML escape, and no newline */
#define PS_BRIEF        0   /* Short PS report */
#define PS_LONG         1   /* Long PS report */
#define PS_SUMM         2   /* Queue counts only */
#define PS_ALL          4   /* List entire queue */
#define QUERY_SQL       1   /* SQL query */
#define QUEUE_KICK      1   /* Process commands from queue */
#define QUEUE_WARP      2   /* Advance or set back wait queue clock */
#define QUOTA_SET       1   /* Set a quota */
#define QUOTA_FIX       2   /* Repair a quota */
#define QUOTA_TOT       4   /* Operate on total quota */
#define QUOTA_REM       8   /* Operate on remaining quota */
#define QUOTA_ALL       16  /* Operate on all players */
//#define QUOTA_ROOM      32  /* Room quota set */
//#define QUOTA_EXIT      64  /* Exit quota set */
//#define QUOTA_THING     128 /* Thing quota set */
//#define QUOTA_PLAYER    256 /* Player quota set */
#define SAY_SAY         1   /* say in current room */
#define SAY_NOSPACE     1   /* OR with xx_EMIT to get nospace form */
#define SAY_POSE        2   /* pose in current room */
#define SAY_POSE_NOSPC  3   /* pose w/o space in current room */
#define SAY_PREFIX      4   /* first char indicates formatting */
#define SAY_EMIT        5   /* emit in current room */
#define SAY_NOEVAL      8   // Don't eval message
#define SHOUT_SHOUT     1   /* shout to all logged-in players */
#define SHOUT_WALLPOSE  2   /* Pose to all logged-in players */
#define SHOUT_WALLEMIT  3   /* Emit to all logged-in players */
#define SHOUT_WIZSHOUT  4   /* shout to all logged-in wizards */
#define SHOUT_WIZPOSE   5   /* Pose to all logged-in wizards */
#define SHOUT_WIZEMIT   6   /* Emit to all logged-in wizards */
#define SHOUT_ADMINSHOUT 7  /* Emit to all wizards or royalty */
//#define SAY_GRIPE       16  /* Complain to management */
//#define SAY_NOTE        17  /* Comment to log for wizards */
#define SAY_NOTAG       32  /* Don't put Broadcast: in front (additive) */
#define SAY_HERE        64  /* Output to current location */
#define SAY_ROOM        128 /* Output to containing room */
#define SAY_HTML        256 /* Don't output a newline */
#define SET_QUIET       1   /* Don't display 'Set.' message. */
#define SHOUT_DEFAULT   0   /* Default @wall message */
#define SHOUT_WIZARD    1   /* @wizwall */
#define SHOUT_ADMIN     2   /* @wall/admin */
#define SHUTDN_NORMAL   0   /* Normal shutdown */
#define SHUTDN_PANIC    1   /* Write a panic dump file */
#define SHUTDN_EXIT     2   /* Exit from shutdown code */
#define SHUTDN_COREDUMP 4   /* Produce a coredump */
#define SRCH_SEARCH     1   /* Do a normal search */
#define SRCH_MARK       2   /* Set mark bit for matches */
#define SRCH_UNMARK     3   /* Clear mark bit for matches */
#define STAT_PLAYER     0   /* Display stats for one player or tot objs */
#define STAT_ALL        1   /* Display global stats */
#define STAT_ME         2   /* Display stats for me */
#define SWITCH_DEFAULT  0   /* Use the configured default for switch */
#define SWITCH_ANY      1   /* Execute all cases that match */
#define SWITCH_ONE      2   /* Execute only first case that matches */
#define SWEEP_ME        1   /* Check my inventory */
#define SWEEP_HERE      2   /* Check my location */
#define SWEEP_COMMANDS  4   /* Check for $-commands */
#define SWEEP_LISTEN    8   /* Check for @listen-ers */
#define SWEEP_PLAYER    16  /* Check for players and puppets */
#define SWEEP_CONNECT   32  /* Search for connected players/puppets */
#define SWEEP_EXITS     64  /* Search the exits for audible flags */
#define SWEEP_SCAN      128 /* Scan for pattern matching */
#define SWEEP_VERBOSE   256 /* Display what pattern matches */
#define TELEPORT_DEFAULT 1  /* Emit all messages */
#define TELEPORT_QUIET  2   /* Teleport in quietly */
#define TELEPORT_LIST   4   /* Teleport a list of items */
#define TIMECHK_RESET   1   /* Reset all counters to zero */
#define TIMECHK_SCREEN  2   /* Write info to screen */
#define TIMECHK_LOG     4   /* Write info to log */
#define TOAD_NO_CHOWN   1   /* Don't change ownership */
#define TRIG_QUIET      1   /* Don't display 'Triggered.' message. */
#define TWARP_QUEUE     1   /* Warp the wait and sem queues */
#define TWARP_DUMP      2   /* Warp the dump interval */
#define TWARP_CLEAN     4   /* Warp the cleaning interval */
#define TWARP_IDLE      8   /* Warp the idle check interval */
/* empty       16 */
#define TWARP_EVENTS    32  /* Warp the events checking interval */
#define WAIT_UNTIL      1   // Absolute UTC seconds instead of delta.

/* Hush codes for movement messages */

#define HUSH_ENTER      1   /* xENTER/xEFAIL */
#define HUSH_LEAVE      2   /* xLEAVE/xLFAIL */
#define HUSH_EXIT       4   /* xSUCC/xDROP/xFAIL from exits */

/* Evaluation directives */

#define EV_FIGNORE      0x00000000  /* Don't look for func if () found */
#define EV_FMAND        0x00000100  /* Text before () must be func name */
#define EV_FCHECK       0x00000200  /* Check text before () for function */
#define EV_STRIP_CURLY  0x00000400  /* Strip one level of brackets */
#define EV_EVAL         0x00000800  /* Evaluate results before returning */
#define EV_STRIP_TS     0x00001000  /* Strip trailing spaces */
#define EV_STRIP_LS     0x00002000  /* Strip leading spaces */
#define EV_STRIP_ESC    0x00004000  /* Strip one level of \ characters */
#define EV_STRIP_AROUND 0x00008000  /* Strip {} only at ends of string */
#define EV_TOP          0x00010000  /* This is a toplevel call to eval() */
#define EV_NOTRACE      0x00020000  /* Don't trace this call to eval */
#define EV_NO_COMPRESS  0x00040000  /* Don't compress spaces. */
#define EV_NO_LOCATION  0x00080000  /* Supresses %l */
#define EV_NOFCHECK     0x00100000  /* Do not evaluate functions! */

/* Message forwarding directives */

#define MSG_PUP_ALWAYS  0x00000001UL    /* Always forward msg to puppet owner */
#define MSG_INV         0x00000002UL    /* Forward msg to contents */
#define MSG_INV_L       0x00000004UL    /* ... only if msg passes my @listen */
#define MSG_INV_EXITS   0x00000008UL    /* Forward through my audible exits */
#define MSG_NBR         0x00000010UL    /* Forward msg to neighbors */
#define MSG_NBR_A       0x00000020UL    /* ... only if I am audible */
#define MSG_NBR_EXITS   0x00000040UL    /* Also forward to neighbor exits */
#define MSG_NBR_EXITS_A 0x00000080UL    /* ... only if I am audible */
#define MSG_LOC         0x00000100UL    /* Send to my location */
#define MSG_LOC_A       0x00000200UL    /* ... only if I am audible */
#define MSG_FWDLIST     0x00000400UL    /* Forward to my fwdlist members if aud */
#define MSG_ME          0x00000800UL    /* Send to me */
#define MSG_S_INSIDE    0x00001000UL    /* Originator is inside target */
#define MSG_S_OUTSIDE   0x00002000UL    /* Originator is outside target */
#define MSG_HTML        0x00004000UL    /* Don't send \r\n */
#define MSG_OOC         0x00008000UL    /* Overide visibility rules because it's OOC */
#define MSG_SAYPOSE     0x00010000UL    /* Indicates that the message is speech. */

#define MSG_ME_ALL      (MSG_ME|MSG_INV_EXITS|MSG_FWDLIST)
#define MSG_F_CONTENTS  (MSG_INV)
#define MSG_F_UP        (MSG_NBR_A|MSG_LOC_A)
#define MSG_F_DOWN      (MSG_INV_L)

/* Look primitive directives */

#define LK_IDESC        0x0001
#define LK_OBEYTERSE    0x0002
#define LK_SHOWATTR     0x0004
#define LK_SHOWEXIT     0x0008
#define LK_SHOWVRML     0x0010

/* Quota types */
//#define QTYPE_ALL       0
//#define QTYPE_ROOM      1
//#define QTYPE_EXIT      2
//#define QTYPE_THING     3
//#define QTYPE_PLAYER    4

/* Exit visibility precalculation codes */

#define VE_LOC_XAM      0x01    /* Location is examinable */
#define VE_LOC_DARK     0x02    /* Location is dark */
#define VE_LOC_LIGHT    0x04    /* Location is light */
//#define VE_BASE_XAM     0x08    /* Base location (pre-parent) is examinable */
#define VE_BASE_DARK    0x10    /* Base location (pre-parent) is dark */
//#define VE_BASE_LIGHT   0x20    /* Base location (pre-parent) is light */

/* Signal handling directives */

#define SA_EXIT         1   /* Exit, and dump core */
#define SA_DFLT         2   /* Try to restart on a fatal error */

#define STARTLOG(key,p,s) \
    if ((((key) & mudconf.log_options) != 0) && start_log(p, s)) {
#define ENDLOG \
    end_log(); }
#define LOG_SIMPLE(key,p,s,m) \
    STARTLOG(key,p,s) \
        log_text(m); \
    ENDLOG

extern const char *NOMATCH_MESSAGE;
extern const char *AMBIGUOUS_MESSAGE;
extern const char *NOPERM_MESSAGE;
extern const char *FUNC_FAIL_MESSAGE;
extern const char *FUNC_NOMATCH_MESSAGE;
extern const char *OUT_OF_RANGE;
extern const char *FUNC_NOT_FOUND;
extern const char *FUNC_AMBIGUOUS;
extern const char *FUNC_NOPERM_MESSAGE;

#define safe_nothing(b,p)   safe_copy_buf(FUNC_FAIL_MESSAGE,3,(b),(p))
#define safe_noperm(b,p)    safe_copy_buf(FUNC_NOPERM_MESSAGE,21,(b),(p))
#define safe_nomatch(b,p)   safe_copy_buf(FUNC_NOMATCH_MESSAGE,12,(b),(p))
#define safe_range(b,p)     safe_copy_buf(OUT_OF_RANGE,16,(b),(p))
#define safe_ambiguous(b,p) safe_copy_buf(FUNC_AMBIGUOUS,13,(b),(p))
#define safe_notfound(b,p)  safe_copy_buf(FUNC_NOT_FOUND,13,(b),(p))

int  ReplaceFile(char *old_name, char *new_name);
void RemoveFile(char *name);
void destroy_player(dbref agent, dbref victim);
void do_pemit_list
(
    dbref player,
    int key,
    bool bDoContents,
    int pemit_flags,
    char *list,
    int chPoseType,
    char *message
);
void do_pemit_single
(
    dbref player,
    int key,
    bool bDoContents,
    int pemit_flags,
    char *recipient,
    int chPoseType,
    char *message
);
void do_say(dbref executor, dbref caller, dbref enactor, int key,
                   char *message);

int  boot_off(dbref player, const char *message);
void do_mail_clear(dbref player, char *msglist);
void do_mail_purge(dbref player);
void malias_cleanup(dbref player);
void count_mail(dbref player, int folder, int *rcount, int *ucount, int *ccount);
void check_mail_expiration(void);
void check_mail(dbref player, int folder, bool silent);
const char *mail_fetch_message(dbref player, int num);
int  mail_fetch_from(dbref player, int num);
void raw_notify_html(dbref player, const char *msg);
void do_lock(dbref executor, dbref caller, dbref enactor, int key,
                    int nargs, char *name, char *keytext);
void check_events(void);
void list_system_resources(dbref player);

#if defined(WOD_REALMS) || defined(REALITY_LVLS)

#define ACTION_IS_STATIONARY    0
#define ACTION_IS_MOVING        1
#define ACTION_IS_TALKING       2
#define NUMBER_OF_ACTIONS       3  // A count, n, of the number of possible actions 0...n-1

#define REALM_DO_NORMALLY_SEEN        1
#define REALM_DO_HIDDEN_FROM_YOU      2
#define REALM_DO_SHOW_OBFDESC         3
#define REALM_DO_SHOW_WRAITHDESC      4
#define REALM_DO_SHOW_UMBRADESC       5
#define REALM_DO_SHOW_MATRIXDESC      6
#define REALM_DO_SHOW_FAEDESC         7
#define REALM_DO_MASK                 7
#define REALM_DISABLE_ADESC           0x00000008L
int DoThingToThingVisibility(dbref looker, dbref lookee, int action_state);
#endif // WOD_REALMS

typedef struct
{
    int    port;
    SOCKET socket;
} PortInfo;

#define MAX_LISTEN_PORTS 10
extern PortInfo aMainGamePorts[MAX_LISTEN_PORTS];
extern int      nMainGamePorts;

#ifdef WIN32
extern DWORD platform;
#else // WIN32
extern int maxd;
#endif // WIN32

extern unsigned int ndescriptors;

extern long DebugTotalFiles;
extern long DebugTotalSockets;

#ifdef WIN32
extern int game_pid;
extern long DebugTotalThreads;
extern long DebugTotalSemaphores;
extern HANDLE hGameProcess;
typedef BOOL __stdcall FCANCELIO(HANDLE hFile);
typedef BOOL __stdcall FGETPROCESSTIMES(HANDLE hProcess,
    LPFILETIME pftCreate, LPFILETIME pftExit, LPFILETIME pftKernel,
    LPFILETIME pftUser);
extern FCANCELIO *fpCancelIo;
extern FGETPROCESSTIMES *fpGetProcessTimes;
#else // WIN32
extern pid_t game_pid;
#endif // WIN32

// From timer.cpp
//
void init_timer(void);
#ifdef WIN32
void Task_FreeDescriptor(void *arg_voidptr, int arg_Integer);
void Task_DeferredClose(void *arg_voidptr, int arg_Integer);
#endif
void dispatch_DatabaseDump(void *pUnused, int iUnused);
void dispatch_FreeListReconstruction(void *pUnused, int iUnused);
void dispatch_IdleCheck(void *pUnused, int iUnused);
void dispatch_CheckEvents(void *pUnused, int iUnused);
#ifndef MEMORY_BASED
void dispatch_CacheTick(void *pUnused, int iUnused);
#endif


// Using a heap as the data structure for representing this priority
// has some attributes which we depend on:
//
// 1. Most importantly, actions scheduled for the same time (i.e.,
//    immediately) keep the order that they were inserted into the
//    heap.
//
// If you ever re-implement this object using another data structure,
// please remember to maintain the properties properties.
//
typedef void FTASK(void *, int);

typedef struct
{
    CLinearTimeAbsolute ltaWhen;

    int        iPriority;
    int        m_Ticket;        // This is the order in which the task was scheduled.
    FTASK      *fpTask;
    void       *arg_voidptr;
    int        arg_Integer;
    int        m_iVisitedMark;
} TASK_RECORD, *PTASK_RECORD;

#define PRIORITY_SYSTEM  100
#define PRIORITY_PLAYER  200
#define PRIORITY_OBJECT  300
#define PRIORITY_SUSPEND 400

// CF_DEQUEUE driven minimum priority levels.
//
#define PRIORITY_CF_DEQUEUE_ENABLED  PRIORITY_OBJECT
#define PRIORITY_CF_DEQUEUE_DISABLED (PRIORITY_PLAYER-1)

typedef int SCHCMP(PTASK_RECORD, PTASK_RECORD);
typedef int SCHLOOK(PTASK_RECORD);

class CTaskHeap
{
private:
    int m_nAllocated;
    int m_nCurrent;
    PTASK_RECORD *m_pHeap;

    int m_iVisitedMark;

    bool Grow(void);
    void SiftDown(int, SCHCMP *);
    void SiftUp(int, SCHCMP *);
    PTASK_RECORD Remove(int, SCHCMP *);
    void Update(int iNode, SCHCMP *pfCompare);
    void Sort(SCHCMP *pfCompare);
    void Remake(SCHCMP *pfCompare);

public:
    CTaskHeap();
    ~CTaskHeap();

    void Insert(PTASK_RECORD, SCHCMP *);
    PTASK_RECORD PeekAtTopmost(void);
    PTASK_RECORD RemoveTopmost(SCHCMP *);
    void CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);

#define IU_DONE        0
#define IU_NEXT_TASK   1
#define IU_REMOVE_TASK 2
#define IU_UPDATE_TASK 3
    int TraverseUnordered(SCHLOOK *pfLook, SCHCMP *pfCompare);
    int TraverseOrdered(SCHLOOK *pfLook, SCHCMP *pfCompare);
};

class CScheduler
{
private:
    CTaskHeap m_WhenHeap;
    CTaskHeap m_PriorityHeap;
    int       m_Ticket;
    int       m_minPriority;

public:
    void TraverseUnordered(SCHLOOK *pfLook);
    void TraverseOrdered(SCHLOOK *pfLook);
    CScheduler(void) { m_Ticket = 0; m_minPriority = PRIORITY_CF_DEQUEUE_ENABLED; }
    void DeferTask(const CLinearTimeAbsolute& ltWhen, int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer);
    void DeferImmediateTask(int iPriority, FTASK *fpTask, void *arg_voidptr, int arg_Integer);
    bool WhenNext(CLinearTimeAbsolute *);
    int  RunTasks(int iCount);
    int  RunAllTasks(void);
    int  RunTasks(const CLinearTimeAbsolute& tNow);
    void ReadyTasks(const CLinearTimeAbsolute& tNow);
    void CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);

    void SetMinPriority(int arg_minPriority);
    int  GetMinPriority(void) { return m_minPriority; }
};

extern CScheduler scheduler;

int fetch_cmds(dbref target);
void fetch_ConnectionInfoFields(dbref target, long anFields[4]);
long fetch_ConnectionInfoField(dbref target, int iField);
void put_ConnectionInfoFields
(
    dbref target,
    long anFields[4],
    CLinearTimeAbsolute &ltaLogout
);

// Added by D.Piper (del@doofer.org) 2000-APR
//
// In order:
//
//     Total online time
//     Longest connection duration
//     Duration of last connection
//     Total number of connections.
//     time (time_t) of last logout.
//
#define CIF_TOTALTIME      0
#define CIF_LONGESTCONNECT 1
#define CIF_LASTCONNECT    2
#define CIF_NUMCONNECTS    3
#define fetch_totaltime(t)      (fetch_ConnectionInfoField((t), CIF_TOTALTIME))
#define fetch_longestconnect(t) (fetch_ConnectionInfoField((t), CIF_LONGESTCONNECT))
#define fetch_lastconnect(t)    (fetch_ConnectionInfoField((t), CIF_LASTCONNECT))
#define fetch_numconnections(t) (fetch_ConnectionInfoField((t), CIF_NUMCONNECTS))
CLinearTimeAbsolute fetch_logouttime(dbref target);

// From strtod.cpp
//
void FLOAT_Initialize(void);
void mux_FPInit();
void mux_FPSet();
void mux_FPRestore();
double ulp(double);
double mux_strtod(const char *s00, char **se);
char *mux_dtoa(double d, int mode, int ndigits, int *decpt, int *sign,
             char **rve);

// From wiz.cpp
//
extern NAMETAB enable_names[];

// From version.cpp
//
void build_version(void);
void init_version(void);

// From player_c.cpp
//
void pcache_sync(void);
void pcache_trim(void);

// From attrcache.cpp
//
void cache_redirect(void);
void cache_pass2(void);
extern CLinearTimeAbsolute cs_ltime;

// From speech.cpp
//
char *modSpeech(dbref player, char *message, bool bWhich, char *command);

// From funceval.cpp
//
#ifdef DEPRECATED
void stack_clr(dbref obj);
#endif // DEPRECATED
bool parse_and_get_attrib(dbref, char *[], char **, dbref *, char *, char **);

#endif // EXTERNS_H
