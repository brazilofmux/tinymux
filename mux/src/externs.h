// externs.h -- Prototypes for externs not defined elsewhere.
//
// $Id: externs.h,v 1.61 2003-01-05 21:49:33 sdennis Exp $
//

#ifndef EXTERNS_H
#define EXTERNS_H

#include "db.h"
#include "match.h"
#include "mudconf.h"
#include "svdhash.h"
#include "svdrand.h"

/* From conf.cpp */
extern void cf_log_notfound(dbref, char *, const char *, char *);
extern int  cf_modify_bits(int *, char *, void *, UINT32, dbref, char *);

/* From mail.cpp */
extern void load_mail(FILE *);
extern int  dump_mail(FILE *);
extern struct mail *mail_fetch(dbref, int);

/* From netcommon.cpp */
extern void DCL_CDECL raw_broadcast(int, char *, ...);

/* From cque.cpp */
extern int  nfy_que(dbref, int, int, int);
extern int  halt_que(dbref, dbref);
extern void wait_que(dbref executor, dbref caller, dbref enactor, BOOL,
    CLinearTimeAbsolute&, dbref, int, char *, char *[],int, char *[]);

#ifdef WIN32
#include "crypt/crypt.h"
#else // WIN32
extern "C" char *crypt(const char *inptr, const char *inkey);
#endif // WIN32

/* From eval.cpp */
void tcache_init(void);
char *parse_to(char **, char, int);
char *parse_arglist(dbref executor, dbref caller, dbref enactor, char *,
                    char, int, char *[], int, char*[], int, int *);
int get_gender(dbref);
void TinyExec(char *buff, char **bufc, dbref executor, dbref caller,
              dbref enactor, int eval, char **dstr, char *cargs[],
              int ncargs);
extern void save_global_regs(const char *, char *[], int []);
extern void save_and_clear_global_regs(const char *, char *[], int[]);
extern void restore_global_regs(const char *, char *[], int []);
extern char **PushPointers(int nNeeded);
extern void PopPointers(char **p, int nNeeded);
extern int *PushIntegers(int nNeeded);
extern void PopIntegers(int *pi, int nNeeded);
extern const signed char Tiny_IsRegister[256];

/* From game.cpp */
#define notify(p,m)                         notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_saypose(p,m)                 notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_SAYPOSE)
#define notify_html(p,m)                    notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet(p,m)                   notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME)
#define notify_with_cause(p,c,m)            notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_with_cause_ooc(p,c,m)        notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_OOC)
#define notify_with_cause_html(p,c,m)       notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet_with_cause(p,c,m)      notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME)
#define notify_puppet(p,c,m)                notify_check(p,c,m, MSG_ME_ALL|MSG_F_DOWN)
#define notify_quiet_puppet(p,c,m)          notify_check(p,c,m, MSG_ME)
#define notify_all(p,c,m)                   notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS|MSG_F_UP|MSG_F_CONTENTS)
#define notify_all_from_inside(p,c,m)       notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE)
#define notify_all_from_inside_saypose(p,c,m) notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_SAYPOSE)
#define notify_all_from_inside_html(p,c,m)  notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_HTML)
#define notify_all_from_outside(p,c,m)      notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS|MSG_F_UP|MSG_F_CONTENTS|MSG_S_OUTSIDE)

extern void notify_except(dbref, dbref, dbref, const char *, int key);
extern void notify_except2(dbref, dbref, dbref, dbref, const char *);

#ifdef STANDALONE
#define notify_check(p,c,m,k) 
#else
extern void notify_check(dbref, dbref, const char *, int);
#endif

extern BOOL Hearer(dbref);
extern void report(void);
extern int  atr_match(dbref, dbref, char, char *, BOOL);
extern BOOL list_check(dbref, dbref, char, char *, BOOL);
extern BOOL html_escape(const char *src, char *dest, char **destp);

#ifndef STANDALONE
#define DUMP_I_NORMAL    0  // OUTPUT to the outdb through a temporary file.
#define DUMP_I_PANIC     1  // UNLOAD to a crashdb
#define DUMP_I_RESTART   2  // OUTPUT to the inputdb
#define DUMP_I_FLAT      3  // UNLOAD to a .FLAT file
#define DUMP_I_SIGNAL    4  // UNLOAD to a .FLAT file from signal.
#define NUM_DUMP_TYPES   5
extern void dump_database_internal(int);
#endif // !STANDALONE

/* From help.cpp */
extern void helpindex_clean(CHashTable *htab);
extern void helpindex_load(dbref);
extern void helpindex_init(void);

/* From htab.cpp */
extern int  cf_ntab_access(int *, char *, void *, UINT32, dbref, char *);

/* From log.cpp */
#ifdef WIN32
#define ENDLINE "\r\n"
#else // WIN32
#define ENDLINE "\n"
#endif // WIN32
extern BOOL start_log(const char *primary, const char *secondary);
extern void end_log(void);
extern void log_perror(const char *, const char *,const char *,
            const char *);
extern void log_text(const char *);
extern void log_number(int);
extern void log_name(dbref);
extern void log_name_and_loc(dbref);
extern void log_type_and_name(dbref);

/* From look.cpp */
extern void look_in(dbref,dbref, int);
extern void show_vrml_url(dbref, dbref);
size_t decode_attr_flags(int aflags, char *buff);

/* From move.cpp */
extern void move_object(dbref, dbref);
extern void move_via_generic(dbref, dbref, dbref, int);
extern BOOL move_via_teleport(dbref, dbref, dbref, int);
extern void move_exit(dbref, dbref, BOOL, const char *, int);
extern void do_enter_internal(dbref, dbref, BOOL);

/* From object.cpp */
extern dbref start_home(void);
extern dbref default_home(void);
extern BOOL  can_set_home(dbref, dbref, dbref);
extern dbref new_home(dbref);
extern dbref clone_home(dbref, dbref);
extern void  divest_object(dbref);
extern dbref create_obj(dbref, int, const char *, int);
extern void  destroy_obj(dbref);
extern void  empty_obj(dbref);

/* From player.cpp */
extern dbref create_player(char *, char *, dbref, BOOL, BOOL);
extern BOOL add_player_name(dbref, const char *);
extern BOOL delete_player_name(dbref, const char *);
extern dbref lookup_player(dbref, char *, BOOL);
extern void load_player_names(void);
extern void badname_add(char *);
extern void badname_remove(char *);
extern BOOL badname_check(char *);
extern void badname_list(dbref, const char *);

/* From predicates.cpp */
extern char * DCL_CDECL tprintf(const char *, ...);
extern void DCL_CDECL safe_tprintf_str(char *, char **, const char *, ...);
extern dbref insert_first(dbref, dbref);
extern dbref remove_first(dbref, dbref);
extern dbref reverse_list(dbref);
extern BOOL member(dbref, dbref);
extern BOOL could_doit(dbref, dbref, int);
extern BOOL can_see(dbref, dbref, BOOL);
extern void add_quota(dbref, int);
extern BOOL canpayfees(dbref, dbref, int, int);
extern void giveto(dbref,int);
extern BOOL payfor(dbref,int);
extern char *MakeCanonicalObjectName(const char *pName, int *pnName, BOOL *pbValid);
extern BOOL ValidatePlayerName(const char *pName);
extern BOOL ok_password(const char *szPassword, dbref player);
extern void handle_ears(dbref, BOOL, BOOL);
extern dbref match_possessed(dbref, dbref, char *, dbref, BOOL);
extern void parse_range(char **, dbref *, dbref *);
extern BOOL parse_thing_slash(dbref, char *, char **, dbref *);
extern BOOL get_obj_and_lock(dbref, char *, dbref *, ATTR **, char *, char **);
extern dbref where_is(dbref);
extern dbref where_room(dbref);
extern BOOL locatable(dbref, dbref, dbref);
extern BOOL nearby(dbref, dbref);
extern BOOL exit_visible(dbref, dbref, int);
extern BOOL exit_displayable(dbref, dbref, int);
extern void did_it(dbref, dbref, int, const char *, int, const char *, int, char *[], int);
extern BOOL bCanReadAttr(dbref executor, dbref target, ATTR *tattr, BOOL bParentCheck);
extern BOOL bCanSetAttr(dbref executor, dbref target, ATTR *tattr);

/* From set.cpp */
extern BOOL parse_attrib(dbref, char *, dbref *, int *);
extern BOOL parse_attrib_wild(dbref, char *, dbref *, BOOL, BOOL, BOOL);
extern void edit_string(char *, char **, char *, char *);
extern dbref match_controlled_handler(dbref player, const char *name, BOOL bQuiet);
#define match_controlled(player,name)       match_controlled_handler(player, name, FALSE)
#define match_controlled_quiet(player,name) match_controlled_handler(player, name, TRUE)
#ifdef STANDALONE
#define set_modified(thing) 
#else
extern void set_modified(dbref thing);
#endif

/* From boolexp.cpp */
extern BOOL eval_boolexp(dbref, dbref, dbref, BOOLEXP *);
extern BOOLEXP *parse_boolexp(dbref, const char *, BOOL);
extern BOOL eval_boolexp_atr(dbref, dbref, dbref, char *);

/* From functions.cpp */
extern BOOL xlate(char *);
extern char *trim_space_sep(char *, char);
extern char *trim_space_sep_LEN(char *str, int nStr, char sep, int *nTrim);
extern char *next_token(char *str, char sep);
extern char *split_token(char **sp, char sep);

#ifdef HAVE_IEEE_FP_FORMAT
#define IEEE_MAKE_NAN  1
#define IEEE_MAKE_IND  2
#define IEEE_MAKE_PINF 3
#define IEEE_MAKE_NINF 4

double MakeSpecialFloat(int iWhich);
#endif // HAVE_IEEE_FP_FORMAT

/* From unparse.cpp */
extern char *unparse_boolexp(dbref, BOOLEXP *);
extern char *unparse_boolexp_quiet(dbref, BOOLEXP *);
extern char *unparse_boolexp_decompile(dbref, BOOLEXP *);
extern char *unparse_boolexp_function(dbref, BOOLEXP *);

/* From walkdb.cpp */
int chown_all(dbref from_player, dbref to_player, dbref acting_player, int key);
extern void olist_push(void);
extern void olist_pop(void);
extern void olist_add(dbref);
extern dbref olist_first(void);
extern dbref olist_next(void);

/* From wild.cpp */
extern BOOL wild(char *, char *, char *[], int);
extern BOOL wild_match(char *, const char *);
extern BOOL quick_wild(const char *, const char *);

/* From command.cpp */
extern BOOL check_access(dbref player, int mask);
extern void set_prefix_cmds(void);
extern char *process_command(dbref executor, dbref caller, dbref enactor, BOOL,
    char *, char *[], int);

#define Protect(f) (cmdp->perms & f)

#define Invalid_Objtype(x) \
((Protect(CA_LOCATION) && !Has_location(x)) || \
 (Protect(CA_CONTENTS) && !Has_contents(x)) || \
 (Protect(CA_PLAYER) && !isPlayer(x)))

/* from db.cpp */
extern BOOL Commer(dbref);
extern void s_Pass(dbref, const char *);
extern void s_Name(dbref, const char *);
extern void s_Moniker(dbref thing, const char *s);
extern const char *Name(dbref thing);
extern const char *PureName(dbref thing);
extern const char *Moniker(dbref thing);
extern int  fwdlist_load(FWDLIST *, dbref, char *);
extern void fwdlist_set(dbref, FWDLIST *);
extern void fwdlist_clr(dbref);
extern int  fwdlist_rewrite(FWDLIST *, char *);
extern FWDLIST *fwdlist_get(dbref);
extern void atr_push(void);
extern void atr_pop(void);
extern int  atr_head(dbref, char **);
extern int  atr_next(char **);
extern int  init_dbfile(char *game_dir_file, char *game_pag_file);
extern void atr_cpy(dbref dest, dbref source);
extern void atr_chown(dbref);
extern void atr_clr(dbref, int);
extern void atr_add_raw_LEN(dbref, int, const char *, int);
extern void atr_add_raw(dbref, int, const char *);
extern void atr_add(dbref, int, char *, dbref, int);
extern void atr_set_flags(dbref, int, int);
extern const char *atr_get_raw_LEN(dbref, int, size_t *);
extern const char *atr_get_raw(dbref, int);
extern char *atr_get_LEN(dbref, int, dbref *, int *, size_t *);
extern char *atr_get_real(dbref, int, dbref *, int *, const char *, const int);
#define atr_get(t,a,o,f) atr_get_real(t,a,o,f, __FILE__, __LINE__)
extern char *atr_pget_LEN(dbref, int, dbref *, int *, size_t *);
extern char *atr_pget_real(dbref, int, dbref *, int *, const char *, const int);
#define atr_pget(t,a,o,f) atr_pget_real(t,a,o,f, __FILE__, __LINE__)
extern char *atr_get_str_LEN(char *s, dbref, int, dbref *, int *, size_t *);
extern char *atr_get_str(char *, dbref, int, dbref *, int *);
extern char *atr_pget_str_LEN(char *, dbref, int, dbref *, int *, size_t *);
extern char *atr_pget_str(char *, dbref, int, dbref *, int *);
extern BOOL atr_get_info(dbref, int, dbref *, int *);
extern BOOL atr_pget_info(dbref, int, dbref *, int *);
extern void atr_free(dbref);
extern BOOL check_zone_handler(dbref player, dbref thing, BOOL bPlayerCheck);
#ifdef STANDALONE
#define check_zone(player, thing) FALSE
#else // STANDALONE
#define check_zone(player, thing) check_zone_handler(player, thing, FALSE)
#endif // STANDALONE
extern void ReleaseAllResources(dbref obj);
extern BOOL fwdlist_ck(dbref player, dbref thing, int anum, char *atext);

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
#define CLONE_PARENT    64  /* Set parent on obj instd of cloning attrs */
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
//#define DBCK_REPORT     2   /* Report info to invoker */
#define DBCK_FULL       4   /* Do all tests */
//#define DBCK_FLOATING   8   /* Look for floating rooms */
//#define DBCK_PURGE      16  /* Purge the db of refs to going objects */
//#define DBCK_LINKS      32  /* Validate exit and object chains */
//#define DBCK_WEALTH     64  /* Validate object value/wealth */
//#define DBCK_OWNER      128 /* Do more extensive owner checking */
//#define DBCK_OWN_EXIT   256 /* Check exit owner owns src or dest */
//#define DBCK_WIZARD     512 /* Check for wizards/wiz objects */
//#define DBCK_TYPES      1024    /* Check for valid & appropriate types */
//#define DBCK_SPARE      2048    /* Make sure spare header fields are NOTHING */
//#define DBCK_HOMES      4096    /* Make sure homes and droptos are valid */
#define DECOMP_DBREF    1   /* decompile by dbref */
//#define DECOMP_PRETTY   2   /* pretty-format output */
#define DEST_ONE        1   /* object */
//#define DEST_ALL        2   /* owner */
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

extern int  ReplaceFile(char *old_name, char *new_name);
extern void RemoveFile(char *name);
extern void destroy_player(dbref agent, dbref victim);
extern void do_pemit_list
(
    dbref player,
    int key,
    BOOL bDoContents,
    int pemit_flags,
    char *list,
    int chPoseType,
    char *message
);
extern void do_pemit_single
(
    dbref player,
    int key,
    BOOL bDoContents,
    int pemit_flags,
    char *recipient,
    int chPoseType,
    char *message
);
extern void do_say(dbref executor, dbref caller, dbref enactor, int key,
                   char *message);

extern int  boot_off(dbref player, const char *message);
extern void do_mail_clear(dbref player, char *msglist);
extern void do_mail_purge(dbref player);
extern void raw_notify_html(dbref player, const char *msg);
extern void do_lock(dbref executor, dbref caller, dbref enactor, int key,
                    int nargs, char *name, char *keytext);
extern void check_events(void);
extern void list_system_resources(dbref player);

#ifdef WOD_REALMS

#define ACTION_IS_STATIONARY    0
#define ACTION_IS_MOVING        1
#define ACTION_IS_TALKING       2
#define NUMBER_OF_ACTIONS       3  // A count, n, of the number of possible actions 0...n

#define REALM_DO_NORMALLY_SEEN        1
#define REALM_DO_HIDDEN_FROM_YOU      2
#define REALM_DO_SHOW_OBFDESC         3
#define REALM_DO_SHOW_WRAITHDESC      4
#define REALM_DO_SHOW_UMBRADESC       5
#define REALM_DO_SHOW_MATRIXDESC      6
#define REALM_DO_SHOW_FAEDESC         7
#define REALM_DO_MASK                 7

#define REALM_DISABLE_ADESC           0x00000008L
extern int DoThingToThingVisibility(dbref looker, dbref lookee, int action_state);
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
extern BOOL bQueryPerformanceAvailable;
extern INT64 QP_A;
extern INT64 QP_B;
extern INT64 QP_C;
extern INT64 QP_D;
#else // WIN32
extern pid_t game_pid;
#endif // WIN32

extern void init_timer(void);

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

    BOOL Grow(void);
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
    BOOL WhenNext(CLinearTimeAbsolute *);
    int  RunTasks(int iCount);
    int  RunAllTasks(void);
    int  RunTasks(const CLinearTimeAbsolute& tNow);
    void ReadyTasks(const CLinearTimeAbsolute& tNow);
    void CancelTask(FTASK *fpTask, void *arg_voidptr, int arg_Integer);

    void SetMinPriority(int arg_minPriority);
    int  GetMinPriority(void) { return m_minPriority; }
};

extern CScheduler scheduler;

extern int fetch_cmds(dbref target);
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
extern void FLOAT_Initialize(void);
double ulp(double);
double Tiny_strtod(const char *s00, char **se);
char *Tiny_dtoa(double d, int mode, int ndigits, int *decpt, int *sign,
             char **rve);

#endif // EXTERNS_H
