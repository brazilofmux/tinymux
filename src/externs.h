// externs.h - Prototypes for externs not defined elsewhere.
//
// $Id: externs.h,v 1.11 2000-04-15 15:25:10 sdennis Exp $
//
#ifndef EXTERNS_H
#define EXTERNS_H

#include "svdrand.h"
#include "svdhash.h"
#include "db.h"
#include "mudconf.h"
#include "regexp.h"

#ifndef WIN32
#define TRUE    1
#define FALSE   0
#endif

/* From regexp.c (extract from Henry Spencer's package) */

extern regexp *FDECL(regcomp, (char *));
extern int FDECL(regexec, (register regexp *, register char *));
extern void FDECL(regerror, (char *));
extern char regexp_errbuf[];

/* From conf.c */
extern int  cf_modify_bits(int *, char *, unsigned int, dbref, char *);
extern char *StringClone(const char *str);

/* From mail.c*/
extern int  FDECL(load_mail, (FILE *));
extern int  FDECL(dump_mail, (FILE *));
extern void NDECL(mail_init);
extern struct mail *FDECL(mail_fetch, (dbref, int));

/* From netcommon.c */
extern void FDECL(make_ulist, (dbref, char *, char **));
extern int  FDECL(fetch_idle, (dbref));
extern int  FDECL(fetch_connect, (dbref));
extern void DCL_CDECL raw_broadcast(int, char *, ...);

/* From cque.c */
extern int  FDECL(nfy_que, (dbref, int, int, int));
extern int  FDECL(halt_que, (dbref, dbref));
extern void FDECL(wait_que, (dbref, dbref, int, dbref, int, char *, char *[],int, char *[]));
extern void NDECL(recover_queue_deposits);

#ifdef WIN32 // WIN32
#include "crypt/crypt.h"
#else
extern "C" char *crypt(const char *inptr, const char *inkey);
#endif

/* From eval.c */
void tcache_init(void);
char *parse_to(char **, char, int);
char *parse_arglist(dbref, dbref, char *, char, int, char *[], int, char*[], int, int *);
int get_gender(dbref);
void TinyExec(char *buff, char **bufc, int tflags, dbref player, dbref cause, int eval, char **dstr, char *cargs[], int ncargs);
extern void save_global_regs(const char *, char *[], int []);
extern void restore_global_regs(const char *, char *[], int []);

/* From game.c */
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

extern int  FDECL(check_filter, (dbref, dbref, int, const char *));
extern void FDECL(notify_check, (dbref, dbref, const char *, int));
extern int  FDECL(Hearer, (dbref));
extern void NDECL(report);
extern int  FDECL(atr_match, (dbref, dbref, char, char *, int));
extern int  FDECL(list_check, (dbref, dbref, char, char *, int));
extern int  FDECL(html_escape, (const char *src, char *dest, char **destp));
    
#ifndef STANDALONE
#define DUMP_NORMAL    0  // OUTPUT to the outdb through a temporary file.
#define DUMP_PANIC     1  // UNLOAD to a crashdb
#define DUMP_RESTART   2  // OUTPUT to the inputdb
#define DUMP_FLAT      3  // UNLOAD to a .FLAT file
#define DUMP_SIGNAL    4  // UNLOAD to a .KILLED file
#define NUM_DUMP_TYPES 5
extern void dump_database_internal(int);
#endif

/* From help.c */
extern void helpindex_clean(CHashTable *htab);
extern int  helpindex_read(CHashTable *, char *);
extern void helpindex_load(dbref);
extern void helpindex_init(void);

/* From htab.c */
extern int  cf_ntab_access(int *, char *, unsigned int, dbref, char *);

/* From log.c */
extern int start_log(const char *primary, const char *secondary);
extern void NDECL(end_log);
extern void FDECL(log_perror, (const char *, const char *,const char *,
            const char *));
extern void FDECL(log_text, (char *));
extern void FDECL(log_number, (int));
extern void FDECL(log_name, (dbref));
extern void FDECL(log_name_and_loc, (dbref));
extern char *   FDECL(OBJTYP, (dbref));
extern void FDECL(log_type_and_name, (dbref));
extern void FDECL(log_type_and_num, (dbref));

/* From look.c */
extern void FDECL(look_in, (dbref,dbref, int));
extern void FDECL(show_vrml_url, (dbref, dbref));

/* From move.c */
extern void FDECL(move_object, (dbref, dbref));
extern void FDECL(move_via_generic, (dbref, dbref, dbref, int));
extern void FDECL(move_via_exit, (dbref, dbref, dbref, dbref, int));
extern int  FDECL(move_via_teleport, (dbref, dbref, dbref, int));
extern void FDECL(move_exit, (dbref, dbref, int, const char *, int));
extern void FDECL(do_enter_internal, (dbref, dbref, int));

/* From object.c */
extern dbref    NDECL(start_home);
extern dbref    NDECL(default_home);
extern int  FDECL(can_set_home, (dbref, dbref, dbref));
extern dbref    FDECL(new_home, (dbref));
extern dbref    FDECL(clone_home, (dbref, dbref));
extern void FDECL(divest_object, (dbref));
extern dbref    FDECL(create_obj, (dbref, int, char *, int));
extern void FDECL(destroy_obj, (dbref, dbref));
extern void FDECL(empty_obj, (dbref));

/* From player.c */
extern void FDECL(record_login, (dbref, int, char *, char *, char *));
extern int  FDECL(check_pass, (dbref, const char *));
extern dbref    FDECL(connect_player, (char *, char *, char *, char *));
extern dbref    FDECL(create_player, (char *, char *, dbref, int, int));
extern int  FDECL(add_player_name, (dbref, char *));
extern int  FDECL(delete_player_name, (dbref, char *));
extern dbref    FDECL(lookup_player, (dbref, char *, int));
extern void NDECL(load_player_names);
extern void FDECL(badname_add, (char *));
extern void FDECL(badname_remove, (char *));
extern int  FDECL(badname_check, (char *));
extern void FDECL(badname_list, (dbref, const char *));

/* From predicates.c */
extern char * DCL_CDECL tprintf(const char *, ...);
extern void DCL_CDECL safe_tprintf_str(char *, char **, char *, ...);
extern dbref    FDECL(insert_first, (dbref, dbref));
extern dbref    FDECL(remove_first, (dbref, dbref));
extern dbref    FDECL(reverse_list, (dbref));
extern int  FDECL(member, (dbref, dbref));
extern int  is_integer(char *, int *);
extern int  FDECL(is_number, (char *));
extern int  FDECL(could_doit, (dbref, dbref, int));
extern int  FDECL(can_see, (dbref, dbref, int));
extern void FDECL(add_quota, (dbref, int));
extern int  FDECL(canpayfees, (dbref, dbref, int, int));
extern void FDECL(giveto, (dbref,int));
extern int  FDECL(payfor, (dbref,int));
extern int  FDECL(ok_name, (const char *));
extern int  FDECL(ok_player_name, (const char *));
extern int  FDECL(ok_attr_name, (const char *));
extern int  ok_password(const char *szPassword, dbref player);
extern void FDECL(handle_ears, (dbref, int, int));
extern dbref    FDECL(match_possessed, (dbref, dbref, char *, dbref, int));
extern void FDECL(parse_range, (char **, dbref *, dbref *));
extern int  FDECL(parse_thing_slash, (dbref, char *, char **, dbref *));
extern int  FDECL(get_obj_and_lock, (dbref, char *, dbref *, ATTR **,
            char *, char **));
extern dbref    FDECL(where_is, (dbref));
extern dbref    FDECL(where_room, (dbref));
extern int  FDECL(locatable, (dbref, dbref, dbref));
extern int  FDECL(nearby, (dbref, dbref));
extern int  FDECL(exit_visible, (dbref, dbref, int));
extern int  FDECL(exit_displayable, (dbref, dbref, int));
extern void FDECL(did_it, (dbref, dbref, int, const char *, int,
            const char *, int, char *[], int));
extern void FDECL(list_bufstats, (dbref));
extern void FDECL(list_buftrace, (dbref));

/* From set.c */
extern int  FDECL(parse_attrib, (dbref, char *, dbref *, int *));
extern int  FDECL(parse_attrib_wild, (dbref, char *, dbref *, int,
            int, int));
extern void FDECL(edit_string, (char *, char **, char *, char *));
extern dbref    FDECL(match_controlled, (dbref, const char *));
extern dbref    FDECL(match_affected, (dbref, const char *));
extern dbref    FDECL(match_examinable, (dbref, const char *));

// From stringutil.c
//
extern char Tiny_IsDigit[256];
extern char Tiny_IsAlpha[256];
extern char Tiny_IsAlphaNumeric[256];
extern char Tiny_IsUpper[256];
extern char Tiny_IsLower[256];
extern char Tiny_IsSpace[256];
extern char Tiny_IsAttributeNameCharacter[256];
extern unsigned char Tiny_ToUpper[256];
extern unsigned char Tiny_ToLower[256];
int ANSI_lex(int nString, const char *pString, int *nLengthToken0, int *nLengthToken1);
#define TOKEN_TEXT_ANSI 0 // Text sequence + optional ANSI sequence.
#define TOKEN_ANSI      1 // ANSI sequence.

typedef struct
{
    char *pString;
    char aControl[256];
} TINY_STRTOK_STATE;

void Tiny_StrTokString(TINY_STRTOK_STATE *tts, char *pString);
void Tiny_StrTokControl(TINY_STRTOK_STATE *tts, char *pControl);
char *Tiny_StrTokParse(TINY_STRTOK_STATE *tts);

int Tiny_ltoa(long val, char *buf);
char *Tiny_ltoa_t(long val);
void safe_ltoa(long val, char *buff, char **bufc, int size);
int Tiny_i64toa(INT64 val, char *buf);
char *Tiny_i64toa_t(INT64 val);
long Tiny_atol(const char *pString);
INT64 Tiny_atoi64(const char *pString);

typedef struct
{
    char bNormal;
    char bBlink;
    char bHighlite;
    char bInverse;
    char bUnder;

    char iForeground;
    char iBackground;
} ANSI_ColorState;

struct ANSI_Context
{
    ANSI_ColorState acsCurrent;
    ANSI_ColorState acsPrevious;
    ANSI_ColorState acsFinal;
    const char *pString;
    int   nString;
    BOOL  bNoBleed;
    BOOL  bSawNormal;
};
void ANSI_String_Init(struct ANSI_Context *pContext, const char *szString, BOOL bNoBleed);
void ANSI_String_Skip(struct ANSI_Context *pContext, int maxVisualWidth, int *pnVisualWidth);
int ANSI_String_Copy(struct ANSI_Context *pContext, int nField, char *pField0, int maxVisualWidth, int *pnVisualWidth);
int ANSI_TruncateToField(const char *szString, int nField, char *pField, int maxVisual, int *nVisualWidth, BOOL bNoBleed);
extern char *strip_ansi(const char *);
extern char *normal_to_white(const char *);
extern char *   FDECL(munge_space, (char *));
extern char *   FDECL(trim_spaces, (char *));
extern char *   FDECL(grabto, (char **, char));
extern int  FDECL(string_compare, (const char *, const char *));
extern int  FDECL(string_prefix, (const char *, const char *));
extern const char * FDECL(string_match, (const char * ,const char *));
extern char *   FDECL(dollar_to_space, (const char *));
extern char *   FDECL(replace_string, (const char *, const char *,
            const char *));
extern char *   FDECL(replace_string_inplace, (const char *,  const char *,
            char *));
extern char *   FDECL(seek_char, (const char *, char));
extern int  FDECL(prefix_match, (const char *, const char *));
extern int  FDECL(minmatch, (char *, char *, int));
extern char *   FDECL(strsave, (const char *));
void safe_copy_str(const char *src, char *buff, char **bufp, int max);
int safe_copy_buf(const char *src, int nLen, char *buff, char **bufp, int nSizeOfBuffer);
extern int  FDECL(matches_exit_from_list, (char *, char *));
extern char *   FDECL(translate_string, (const char *, int));
#ifndef WIN32
extern int _stricmp(const char *a, const char *b);
extern int _strnicmp(const char *a, const char *b, int n);
extern void _strlwr(char *tp);
extern void _strupr(char *a);
#endif // WIN32

typedef struct tag_dtb
{
    int bFirst;
    char *buff;
    char **bufc;
    int nBufferAvailable;
} DTB;

void DbrefToBuffer_Init(DTB *p, char *arg_buff, char **arg_bufc);
int DbrefToBuffer_Add(DTB *pContext, int i);
void DbrefToBuffer_Final(DTB *pContext);
int DCL_CDECL Tiny_vsnprintf(char *buff, int count, const char *fmt, va_list va);

/* From boolexp.c */
extern int  FDECL(eval_boolexp, (dbref, dbref, dbref, BOOLEXP *));
extern BOOLEXP *FDECL(parse_boolexp, (dbref,const char *, int));
extern int  FDECL(eval_boolexp_atr, (dbref, dbref, dbref, char *));

/* From functions.c */
extern int  FDECL(xlate, (char *));
extern double safe_atof(char *szString);

/* From unparse.c */
extern char *   FDECL(unparse_boolexp, (dbref, BOOLEXP *));
extern char *   FDECL(unparse_boolexp_quiet, (dbref, BOOLEXP *));
extern char *   FDECL(unparse_boolexp_decompile, (dbref, BOOLEXP *));
extern char *   FDECL(unparse_boolexp_function, (dbref, BOOLEXP *));

/* From walkdb.c */
int chown_all(dbref from_player, dbref to_player, dbref acting_player, int key);
extern void olist_push(void);
extern void olist_pop(void);
extern void olist_add(dbref);
extern dbref olist_first(void);
extern dbref olist_next(void);

/* From wild.c */
extern int  FDECL(wild, (char *, char *, char *[], int));
extern int  FDECL(wild_match, (char *, char *));
extern int  FDECL(quick_wild, (char *, char *));

/* From compress.c */
extern const char * FDECL(uncompress, (const char *, int));
extern const char * FDECL(compress, (const char *, int));
extern char *   FDECL(uncompress_str, (char *, const char *, int));

/* From command.c */
extern int  check_access(dbref player, int mask);
extern void set_prefix_cmds(void);
extern void process_command(dbref, dbref, int, char *, char *[], int);

/* from db.c */
extern int  FDECL(Commer, (dbref));
extern void FDECL(s_Pass, (dbref, const char *));
extern void FDECL(s_Name, (dbref, char *));
extern char *   FDECL(Name, (dbref));
extern char *   FDECL(PureName, (dbref));
extern int  FDECL(fwdlist_load, (FWDLIST *, dbref, char *));
extern void FDECL(fwdlist_set, (dbref, FWDLIST *));
extern void FDECL(fwdlist_clr, (dbref));
extern int  FDECL(fwdlist_rewrite, (FWDLIST *, char *));
extern FWDLIST *FDECL(fwdlist_get, (dbref));
extern void FDECL(clone_object, (dbref, dbref));
extern void NDECL(init_min_db);
extern void NDECL(atr_push);
extern void NDECL(atr_pop);
extern int  FDECL(atr_head, (dbref, char **));
extern int  FDECL(atr_next, (char **));
extern int init_dbfile(char *game_dir_file, char *game_pag_file);
extern void FDECL(atr_cpy, (dbref, dbref, dbref));
extern void FDECL(atr_chown, (dbref));
extern void FDECL(atr_clr, (dbref, int));
extern void atr_add_raw_LEN(dbref, int, char *, int);
extern void atr_add_raw(dbref, int, char *);
extern void FDECL(atr_add, (dbref, int, char *, dbref, int));
extern void FDECL(atr_set_owner, (dbref, int, dbref));
extern void FDECL(atr_set_flags, (dbref, int, int));
extern char *atr_get_raw_LEN(dbref, int, int*);
extern char *atr_get_raw(dbref, int);
extern char *atr_get_LEN(dbref, int, dbref *, int *, int *);
extern char *atr_get(dbref, int, dbref *, int *);
extern char *atr_pget_LEN(dbref, int, dbref *, int *, int *);
extern char *atr_pget(dbref, int, dbref *, int *);
extern char *atr_get_str_LEN(char *s, dbref, int, dbref *, int *, int *);
extern char *atr_get_str(char *, dbref, int, dbref *, int *);
extern char *atr_pget_str_LEN(char *, dbref, int, dbref *, int *, int *);
extern char *atr_pget_str(char *, dbref, int, dbref *, int *);
extern int  FDECL(atr_get_info, (dbref, int, dbref *, int *));
extern int  FDECL(atr_pget_info, (dbref, int, dbref *, int *));
extern void FDECL(atr_free, (dbref));
extern int  FDECL(check_zone, (dbref, dbref));
extern int  FDECL(check_zone_for_player, (dbref, dbref));
extern void FDECL(toast_player, (dbref));

/* Command handler keys */

#define ATTRIB_ACCESS   1   /* Change access to attribute */
#define ATTRIB_RENAME   2   /* Rename attribute */
#define ATTRIB_DELETE   4   /* Delete attribute */
#define BOOT_QUIET  1   /* Inhibit boot message to victim */
#define BOOT_PORT   2   /* Boot by port number */
#define CEMIT_NOHEADER  1   /* Channel emit without header */
#define CHOWN_ONE   1   /* item = new_owner */
#define CHOWN_ALL   2   /* old_owner = new_owner */
#define CLIST_FULL  1   /* Full listing of channels */
#define CLONE_LOCATION  0   /* Create cloned object in my location */
#define CLONE_INHERIT   1   /* Keep INHERIT bit if set */
#define CLONE_PRESERVE  2   /* Preserve the owner of the object */
#define CLONE_INVENTORY 4   /* Create cloned object in my inventory */
#define CLONE_SET_COST  8   /* ARG2 is cost of cloned object */
#define CLONE_SET_LOC   16  /* ARG2 is location of cloned object */
#define CLONE_SET_NAME  32  /* ARG2 is alternate name of cloned object */
#define CLONE_PARENT    64  /* Set parent on obj instd of cloning attrs */
#define CRE_INVENTORY   0   /* Create object in my inventory */
#define CRE_LOCATION    1   /* Create object in my location */
#define CRE_SET_LOC 2   /* ARG2 is location of new object */
#define CSET_PUBLIC  0   /* Sets a channel public */
#define CSET_PRIVATE 1   /* Sets a channel private (default) */
#define CSET_LOUD    2   /* Channel shows connects and disconnects */
#define CSET_QUIET   3   /* Channel doesn't show connects/disconnects */
#define CSET_LIST    4   /* Lists channels */
#define CSET_OBJECT  5   /* Sets the channel object for the channel */
#define CSET_SPOOF   6   /* Sets the channel spoofable */
#define CSET_NOSPOOF 7   /* Sets the channel non-spoofable */
#define DBCK_DEFAULT    1   /* Get default tests too */
#define DBCK_REPORT 2   /* Report info to invoker */
#define DBCK_FULL   4   /* Do all tests */
#define DBCK_FLOATING   8   /* Look for floating rooms */
#define DBCK_PURGE  16  /* Purge the db of refs to going objects */
#define DBCK_LINKS  32  /* Validate exit and object chains */
#define DBCK_WEALTH 64  /* Validate object value/wealth */
#define DBCK_OWNER  128 /* Do more extensive owner checking */
#define DBCK_OWN_EXIT   256 /* Check exit owner owns src or dest */
#define DBCK_WIZARD 512 /* Check for wizards/wiz objects */
#define DBCK_TYPES  1024    /* Check for valid & appropriate types */
#define DBCK_SPARE  2048    /* Make sure spare header fields are NOTHING */
#define DBCK_HOMES  4096    /* Make sure homes and droptos are valid */
#define DECOMP_DBREF    1   /* decompile by dbref */
#define DEST_ONE    1   /* object */
#define DEST_ALL    2   /* owner */
#define DEST_OVERRIDE   4   /* override Safe() */
#define DIG_TELEPORT    1   /* teleport to room after @digging */
#define DOLIST_SPACE    0       /* expect spaces as delimiter */
#define DOLIST_DELIMIT  1       /* expect custom delimiter */
#define DOING_MESSAGE   0   /* Set my DOING message */
#define DOING_HEADER    1   /* Set the DOING header */
#define DOING_POLL  2   /* List DOING header */
#define DROP_QUIET  1   /* Don't do odrop/adrop if control */
#define DUMP_STRUCT 1   /* Dump flat structure file */
#define DUMP_TEXT   2   /* Dump text (gdbm) file */
#define EXAM_DEFAULT    0   /* Default */
#define EXAM_BRIEF  1   /* Nonowner sees just owner */
#define EXAM_LONG   2   /* Nonowner sees public attrs too */
#define EXAM_DEBUG  4   /* Display more info for finding db problems */
#define EXAM_PARENT 8   /* Get attr from parent when exam obj/attr */
#define FIXDB_OWNER 1   /* Fix OWNER field */
#define FIXDB_LOC   2   /* Fix LOCATION field */
#define FIXDB_CON   4   /* Fix CONTENTS field */
#define FIXDB_EXITS 8   /* Fix EXITS field */
#define FIXDB_NEXT  16  /* Fix NEXT field */
#define FIXDB_PENNIES   32  /* Fix PENNIES field */
#define FIXDB_ZONE  64  /* Fix ZONE field */
#define FIXDB_LINK  128 /* Fix LINK field */
#define FIXDB_PARENT    256 /* Fix PARENT field */
#define FIXDB_DEL_PN    512 /* Remove player name from player name index */
#define FIXDB_ADD_PN    1024    /* Add player name to player name index */
#define FIXDB_NAME  2048    /* Set NAME attribute */
#define FRC_PREFIX  0   /* #num command */
#define FRC_COMMAND 1   /* what=command */
#define GET_QUIET   1   /* Don't do osucc/asucc if control */
#define GIVE_MONEY  1   /* Give money */
#define GIVE_QUOTA  2   /* Give quota */
#define GIVE_QUIET  64  /* Inhibit give messages */
#define GLOB_ENABLE 1   /* key to enable */
#define GLOB_DISABLE    2   /* key to disable */
#define GLOB_LIST   3   /* key to list */
#define HALT_ALL    1   /* halt everything */
#define HELP_HELP   1   /* get data from help file */
#define HELP_NEWS   2   /* get data from news file */
#define HELP_WIZHELP    3   /* get data from wizard help file */
#define HELP_PLUSHELP   4       /* get data from plushelp file */
#define HELP_WIZNEWS    5       /* wiznews file */
#define HELP_STAFFHELP  6       /* get data from staffhelp file */
#define KILL_KILL   1   /* gives victim insurance */
#define KILL_SLAY   2   /* no insurance */
#define LOOK_LOOK   1   /* list desc (and succ/fail if room) */
#define LOOK_EXAM   2   /* full listing of object */
#define LOOK_DEXAM  3   /* debug listing of object */
#define LOOK_INVENTORY  4   /* list inventory of object */
#define LOOK_SCORE  5   /* list score (# coins) */
#define LOOK_OUTSIDE    8       /* look for object in container of player */
#define MAIL_STATS  1   /* Mail stats */
#define MAIL_DSTATS 2   /* More mail stats */
#define MAIL_FSTATS 3   /* Even more mail stats */
#define MAIL_DEBUG  4   /* Various debugging options */
#define MAIL_NUKE   5   /* Nuke the post office */
#define MAIL_FOLDER 6   /* Do folder stuff */
#define MAIL_LIST   7   /* List @mail by time */
#define MAIL_READ   8   /* Read @mail message */
#define MAIL_CLEAR  9   /* Clear @mail message */
#define MAIL_UNCLEAR    10  /* Unclear @mail message */
#define MAIL_PURGE  11  /* Purge cleared @mail messages */
#define MAIL_FILE   12  /* File @mail in folders */
#define MAIL_TAG    13  /* Tag @mail messages */
#define MAIL_UNTAG  14  /* Untag @mail messages */
#define MAIL_FORWARD    15  /* Forward @mail messages */
#define MAIL_SEND   16  /* Send @mail messages in progress */
#define MAIL_EDIT   17  /* Edit @mail messages in progress */
#define MAIL_URGENT 18  /* Sends a @mail message as URGENT */
#define MAIL_ALIAS  19  /* Creates an @mail alias */
#define MAIL_ALIST  20  /* Lists @mail aliases */
#define MAIL_PROOF  21  /* Proofs @mail messages in progress */
#define MAIL_ABORT  22  /* Aborts @mail messages in progress */
#define MAIL_QUICK  23  /* Sends a quick @mail message */
#define MAIL_REVIEW 24  /* Reviews @mail sent to a player */
#define MAIL_RETRACT    25  /* Retracts @mail sent to a player */
#define MAIL_CC     26  /* Carbon copy */
#define MAIL_SAFE   27  /* Defines a piece of mail as safe. */
#define MAIL_REPLY  28  /* Replies to a message. */
#define MAIL_REPLYALL   29  /* Replies to all recipients of msg */
#define MAIL_QUOTE  0x100   /* Quote back original in the reply? */

#define MALIAS_DESC 1   /* Describes a mail alias */
#define MALIAS_CHOWN    2   /* Changes a mail alias's owner */
#define MALIAS_ADD  3   /* Adds a player to an alias */
#define MALIAS_REMOVE   4   /* Removes a player from an alias */
#define MALIAS_DELETE   5   /* Deletes a mail alias */
#define MALIAS_RENAME   6   /* Renames a mail alias */
#define MALIAS_LIST 8   /* Lists mail aliases */
#define MALIAS_STATUS   9   /* Status of mail aliases */
#define MARK_SET    0   /* Set mark bits */
#define MARK_CLEAR  1   /* Clear mark bits */
#define MOTD_ALL    0   /* login message for all */
#define MOTD_WIZ    1   /* login message for wizards */
#define MOTD_DOWN   2   /* login message when logins disabled */
#define MOTD_FULL   4   /* login message when too many players on */
#define MOTD_LIST   8   /* Display current login messages */
#define MOTD_BRIEF  16  /* Suppress motd file display for wizards */
#define MOVE_QUIET  1   /* Dont do osucc/ofail/asucc/afail if ctrl */
#define NFY_NFY     0   /* Notify first waiting command */
#define NFY_NFYALL  1   /* Notify all waiting commands */
#define NFY_DRAIN   2   /* Delete waiting commands */
#define OPEN_LOCATION   0   /* Open exit in my location */
#define OPEN_INVENTORY  1   /* Open exit in me */
#define PASS_ANY    1   /* name=newpass */
#define PASS_MINE   2   /* oldpass=newpass */
#define PCRE_PLAYER 1   /* create new player */
#define PCRE_ROBOT  2   /* create robot player */
#define PEMIT_PEMIT 1   /* emit to named player */
#define PEMIT_OEMIT 2   /* emit to all in current room except named */
#define PEMIT_WHISPER   3   /* whisper to player in current room */
#define PEMIT_FSAY  4   /* force controlled obj to say */
#define PEMIT_FEMIT 5   /* force controlled obj to emit */
#define PEMIT_FPOSE 6   /* force controlled obj to pose */
#define PEMIT_FPOSE_NS  7   /* force controlled obj to pose w/o space */
#define PEMIT_CONTENTS  8   /* Send to contents (additive) */
#define PEMIT_HERE  16  /* Send to location (@femit, additive) */
#define PEMIT_ROOM  32  /* Send to containing rm (@femit, additive) */
#define PEMIT_LIST      64      /* Send to a list */
#define PEMIT_HTML  128     /* HTML escape, and no newline */
#define PS_BRIEF    0   /* Short PS report */
#define PS_LONG     1   /* Long PS report */
#define PS_SUMM     2   /* Queue counts only */
#define PS_ALL      4   /* List entire queue */
#define QUEUE_KICK  1   /* Process commands from queue */
#define QUEUE_WARP  2   /* Advance or set back wait queue clock */
#define QUOTA_SET   1   /* Set a quota */
#define QUOTA_FIX   2   /* Repair a quota */
#define QUOTA_TOT   4   /* Operate on total quota */
#define QUOTA_REM   8   /* Operate on remaining quota */
#define QUOTA_ALL   16  /* Operate on all players */
#define SAY_SAY     1   /* say in current room */
#define SAY_NOSPACE 1   /* OR with xx_EMIT to get nospace form */
#define SAY_POSE    2   /* pose in current room */
#define SAY_POSE_NOSPC  3   /* pose w/o space in current room */
#define SAY_PREFIX  4   /* first char indicates foratting */
#define SAY_EMIT    5   /* emit in current room */
#define SAY_SHOUT   8   /* shout to all logged-in players */
#define SAY_WALLPOSE    9   /* Pose to all logged-in players */
#define SAY_WALLEMIT    10  /* Emit to all logged-in players */
#define SAY_WIZSHOUT    12  /* shout to all logged-in wizards */
#define SAY_WIZPOSE 13  /* Pose to all logged-in wizards */
#define SAY_WIZEMIT 14  /* Emit to all logged-in wizards */
#define SAY_ADMINSHOUT  15  /* Emit to all wizards or royalty */ 
#define SAY_GRIPE   16  /* Complain to management */
#define SAY_NOTE    17  /* Comment to log for wizards */
#define SAY_NOTAG   32  /* Don't put Broadcast: in front (additive) */
#define SAY_HERE    64  /* Output to current location */
#define SAY_ROOM    128 /* Output to containing room */
#define SAY_HTML    256     /* Don't output a newline */
#define SET_QUIET   1   /* Don't display 'Set.' message. */
#define SHUTDN_NORMAL   0   /* Normal shutdown */
#define SHUTDN_PANIC    1   /* Write a panic dump file */
#define SHUTDN_EXIT 2   /* Exit from shutdown code */
#define SHUTDN_COREDUMP 4   /* Produce a coredump */
#define SRCH_SEARCH 1   /* Do a normal search */
#define SRCH_MARK   2   /* Set mark bit for matches */
#define SRCH_UNMARK 3   /* Clear mark bit for matches */
#define STAT_PLAYER 0   /* Display stats for one player or tot objs */
#define STAT_ALL    1   /* Display global stats */
#define STAT_ME     2   /* Display stats for me */
#define SWITCH_DEFAULT  0   /* Use the configured default for switch */
#define SWITCH_ANY  1   /* Execute all cases that match */
#define SWITCH_ONE  2   /* Execute only first case that matches */
#define SWEEP_ME    1   /* Check my inventory */
#define SWEEP_HERE  2   /* Check my location */
#define SWEEP_COMMANDS  4   /* Check for $-commands */
#define SWEEP_LISTEN    8   /* Check for @listen-ers */
#define SWEEP_PLAYER    16  /* Check for players and puppets */
#define SWEEP_CONNECT   32  /* Search for connected players/puppets */
#define SWEEP_EXITS 64  /* Search the exits for audible flags */
#define SWEEP_SCAN  128 /* Scan for pattern matching */
#define SWEEP_VERBOSE   256 /* Display what pattern matches */
#define TELEPORT_DEFAULT 1  /* Emit all messages */
#define TELEPORT_QUIET   2  /* Teleport in quietly */
#define TOAD_NO_CHOWN   1   /* Don't change ownership */
#define TRIG_QUIET  1   /* Don't display 'Triggered.' message. */
#define TWARP_QUEUE 1   /* Warp the wait and sem queues */
#define TWARP_DUMP  2   /* Warp the dump interval */
#define TWARP_CLEAN 4   /* Warp the cleaning interval */
#define TWARP_IDLE  8   /* Warp the idle check interval */
/* emprty       16 */
#define TWARP_EVENTS    32  /* Warp the events checking interval */

/* Hush codes for movement messages */

#define HUSH_ENTER  1   /* xENTER/xEFAIL */
#define HUSH_LEAVE  2   /* xLEAVE/xLFAIL */
#define HUSH_EXIT   4   /* xSUCC/xDROP/xFAIL from exits */

/* Evaluation directives */

//#define EV_FMASK        0x00000300  /* Mask for function type check */
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

// Termination directives
//
#define PT_NOTHING  0x00000000
#define PT_BRACE    0x00000001
#define PT_BRACKET  0x00000002
#define PT_PAREN    0x00000004
#define PT_COMMA    0x00000008
#define PT_SEMI     0x00000010
#define PT_EQUALS   0x00000020
#define PT_SPACE    0x00000040

/* Message forwarding directives */

#define MSG_PUP_ALWAYS  0x00000001UL    /* Always forward msg to puppet own */
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
#define MSG_SAYPOSE     0x00010000UL    /* Indicates that the message is speach. */

#define MSG_ME_ALL  (MSG_ME|MSG_INV_EXITS|MSG_FWDLIST)
#define MSG_F_CONTENTS  (MSG_INV)
#define MSG_F_UP    (MSG_NBR_A|MSG_LOC_A)
#define MSG_F_DOWN  (MSG_INV_L)

/* Look primitive directives */

#define LK_IDESC    0x0001
#define LK_OBEYTERSE    0x0002
#define LK_SHOWATTR 0x0004
#define LK_SHOWEXIT 0x0008
#define LK_SHOWVRML 0x0010

/* Exit visibility precalculation codes */

#define VE_LOC_XAM  0x01    /* Location is examinable */
#define VE_LOC_DARK 0x02    /* Location is dark */
#define VE_LOC_LIGHT    0x04    /* Location is light */
#define VE_BASE_XAM 0x08    /* Base location (pre-parent) is examinable */
#define VE_BASE_DARK    0x10    /* Base location (pre-parent) is dark */
#define VE_BASE_LIGHT   0x20    /* Base location (pre-parent) is light */

/* Signal handling directives */

#define SA_EXIT     1   /* Exit, and dump core */
#define SA_DFLT     2   /* Try to restart on a fatal error */

#define STARTLOG(key,p,s) \
    if ((((key) & mudconf.log_options) != 0) && start_log(p, s)) {
#define ENDLOG \
    end_log(); }
#define LOG_SIMPLE(key,p,s,m) \
    STARTLOG(key,p,s) \
        log_text(m); \
    ENDLOG

#define test_top()      ((mudstate.qfirst != NULL) ? 1 : 0)
#define controls(p,x)       Controls(p,x)

extern int ReplaceFile(char *old_name, char *new_name);
extern void RemoveFile(char *name);
extern void destroy_player(dbref victim);
extern dbref match_controlled_quiet(dbref player, const char *name);
extern void do_pemit_list(dbref player, char *list, const char *message);
extern int boot_off(dbref player, char *message);
extern void do_mail_clear(dbref player, char *msglist);
extern void do_mail_purge(dbref player);
extern char *upcasestr(char *);
extern void raw_notify_html(dbref player, const char *msg);
extern void do_lock(dbref player, dbref cause, int key, char *name, char *keytext);
extern void check_events(void);
extern void list_system_resources(dbref player);

#ifdef WOD_REALMS

#define ACTION_IS_STATIONARY 0
#define ACTION_IS_MOVING     1
#define ACTION_IS_TALKING    2
#define NUMBER_OF_ACTIONS    3  // A count, n, of the number of possible actions 0...n

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
#endif

#ifdef WIN32
extern SOCKET MainGameSockPort;
extern DWORD platform;
#else // WIN32
extern int maxd;
#endif // WIN32

extern unsigned int ndescriptors;

#ifdef MEMORY_BASED
extern int corrupt;
#endif

extern long DebugTotalFiles;
extern long DebugTotalSockets;
#ifdef MEMORY_ACCOUNTING
extern long DebugTotalMemory;
#endif // MEMORY_ACCOUNTING

#ifdef WIN32
extern long DebugTotalThreads;
extern long DebugTotalSemaphores;
typedef BOOL __stdcall FCANCELIO(HANDLE hFile);
extern FCANCELIO *fpCancelIo;
#endif // WIN32

extern void init_timer(void);

// Using a heap as the data structure for representing this priority has some attributes which we depend on:
//
// 1. Most importantly, actions scheduled for the same time (i.e., immediately) keep the order that they were
//    inserted into the heap.
//
// If you ever re-implement this object using another data structure, please remember to maintain the properties
// properties.
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
typedef BOOL SCHLOOK(PTASK_RECORD);

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

#endif // EXTERNS_H
