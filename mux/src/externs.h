/*! \file externs.h
 * \brief Prototypes for externs not defined elsewhere.
 *
 */

#ifndef EXTERNS_H
#define EXTERNS_H

#include "db.h"
#include "match.h"
#include "libmux.h"
#include "modules.h"

class CResultsSet
{
public:
    CResultsSet(QUEUE_INFO *pqi);
    ~CResultsSet(void);
    UINT32 Release(void);
    UINT32 AddRef(void);
    bool   isLoaded(void);
    void   SetError(UINT32 iError);
    UINT32 GetError(void);
    int    GetRowCount(void);
    const UTF8 *FirstField(int iRow);
    const UTF8 *NextField(void);

private:
    UINT32 m_cRef;
    int    m_nFields;
    size_t m_nBlob;
    UTF8  *m_pBlob;
    bool   m_bLoaded;
    UINT32 m_iError;
    int    m_nRows;
    PUTF8 *m_pRows;

    const UTF8 *m_pCurrentField;
    int         m_iCurrentField;
};

#include "mudconf.h"
#include "svdrand.h"

// From conf.cpp
//
void cf_log_notfound(dbref, const UTF8 *, const UTF8 *, const UTF8 *);
int  cf_modify_bits(int *, UTF8 *, void *, UINT32, dbref, UTF8 *);
void DCL_CDECL cf_log_syntax(dbref player, __in_z UTF8 *cmd, __in_z const UTF8 *fmt, ...);
void ValidateConfigurationDbrefs(void);
#if defined(HAVE_IN_ADDR)
bool make_canonical_IPv4(const UTF8 *str, in_addr_t *pnIP);
#endif
int  cf_read(void);
void cf_init(void);
void cf_list(dbref, UTF8 *, UTF8 **);
void cf_display(dbref, UTF8 *, UTF8 *, UTF8 **);
void list_cf_access(dbref);
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
const UTF8 **local_get_info_table(void);

// From mail.cpp.
//
void load_mail(FILE *);
int  dump_mail(FILE *);
struct mail *mail_fetch(dbref, int);
UTF8 *MakeCanonicalMailAlias
(
    UTF8   *pMailAlias,
    size_t *pnValidMailAlias,
    bool   *pbValidMailAlias
);

UTF8 *MakeCanonicalMailAliasDesc
(
    UTF8   *pMailAliasDesc,
    size_t *pnValidMailAliasDesc,
    bool   *pbValidMailAliasDesc,
    size_t *pnVisualWidth
);

#if defined(FIRANMUX)
const UTF8 *MessageFetch(int number);
#endif // FIRANMUX
size_t MessageFetchSize(int number);
void finish_mail();

// From netcommon.cpp.
//
void DCL_CDECL raw_broadcast(int, __in_z const UTF8 *, ...);
void list_siteinfo(dbref);
void logged_out0(dbref executor, dbref caller, dbref enactor, int eval, int key);
void logged_out1(dbref executor, dbref caller, dbref enactor, int eval, int key, UTF8 *arg, const UTF8 *cargs[], int ncargs);
void init_logout_cmdtab(void);
void desc_reload(dbref);
void make_portlist(dbref, dbref, UTF8 *, UTF8 **);
UTF8 *MakeCanonicalDoing(UTF8 *pDoing, size_t *pnValidDoing, bool *pbValidDoing);
LBUF_OFFSET trimmed_name(dbref player, UTF8 cbuff[MBUF_SIZE], LBUF_OFFSET nMin, LBUF_OFFSET nMax, LBUF_OFFSET nPad);

/* From cque.cpp */
int  nfy_que(dbref, int, int, int);
int  halt_que(dbref, dbref);
void wait_que(dbref executor, dbref caller, dbref enactor, int, bool,
    CLinearTimeAbsolute&, dbref, int, UTF8 *, int, const UTF8 *[], reg_ref *[]);
void query_complete(UINT32 hQuery, UINT32 iError, CResultsSet *prs);

#if defined(UNIX_CRYPT)
extern "C" char *crypt(const char *inptr, const char *inkey);
#endif // UNIX_CRYPT
extern bool break_called;

/* From eval.cpp */
void tcache_init(void);
UTF8 *parse_to(UTF8 **, UTF8, int);
void parse_arglist(dbref executor, dbref caller, dbref enactor, UTF8 *,
                    int, UTF8 *[], int, const UTF8 *[], int, int *);
int get_gender(dbref);
void mux_exec(const UTF8 *pdstr, size_t nStr, UTF8 *buff, UTF8 **bufc, dbref executor,
              dbref caller, dbref enactor, int eval, const UTF8 *cargs[], int ncargs);

inline void BufAddRef(lbuf_ref *lbufref)
{
    if (nullptr != lbufref)
    {
        lbufref->refcount++;
    }
}

inline void BufRelease(lbuf_ref *lbufref)
{
    if (nullptr != lbufref)
    {
        lbufref->refcount--;
        if (0 == lbufref->refcount)
        {
            free_lbuf(lbufref->lbuf_ptr);
            lbufref->lbuf_ptr = nullptr;
            free_lbufref(lbufref);
        }
    }
}

inline void RegAddRef(reg_ref *regref)
{
    if (nullptr != regref)
    {
        regref->refcount++;
    }
}

inline void RegRelease(reg_ref *regref)
{
    if (nullptr != regref)
    {
        regref->refcount--;
        if (0 == regref->refcount)
        {
            BufRelease(regref->lbuf);
            regref->lbuf    = nullptr;
            regref->reg_ptr = nullptr;
            regref->reg_len = 0;
            free_regref(regref);
        }
    }
}
void RegAssign(reg_ref **regref, size_t nLength, const UTF8 *ptr);

void save_global_regs(reg_ref *preserve[]);
void save_and_clear_global_regs(reg_ref *preserve[]);
void restore_global_regs(reg_ref *preserve[]);

reg_ref **PushRegisters(int nNeeded);
void PopRegisters(reg_ref **p, int nNeeded);

extern const signed char mux_RegisterSet[256];
extern const unsigned int ColorTable[256];
bool parse_rgb(size_t n, const UTF8 *p, RGB &rgb);

#define CJC_CENTER 0
#define CJC_LJUST  1
#define CJC_RJUST  2

static const mux_cursor curNewline(2, 2);

LBUF_OFFSET linewrap_general(const UTF8 *pStr, LBUF_OFFSET nWidth,
                             UTF8 *pBuffer, size_t nBuffer,
                             const UTF8 *pLeft = T(""), LBUF_OFFSET nLeft = 0,
                             const UTF8 *pRight = T(""), LBUF_OFFSET nRight = 0,
                             int iJustKey = CJC_LJUST, LBUF_OFFSET nHanging = 0,
                             const UTF8 *pOSep = T("\r\n"),
                             mux_cursor curOSep = curNewline,
                             LBUF_OFFSET nWidth0 = 0);

/* From game.cpp */
#define notify(p,m)                         notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_saypose(p,m)                 notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_SAYPOSE)
#define notify_html(p,m)                    notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet(p,m)                   notify_check(p,p,m, MSG_PUP_ALWAYS|MSG_ME)
#define notify_with_cause(p,c,m)            notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN)
#define notify_with_cause_ooc(p,c,m,s)      notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_OOC|(s))
#define notify_with_cause_html(p,c,m)       notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME_ALL|MSG_F_DOWN|MSG_HTML)
#define notify_quiet_with_cause(p,c,m)      notify_check(p,c,m, MSG_PUP_ALWAYS|MSG_ME)
#define notify_all(p,c,m)                   notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS|MSG_F_UP|MSG_F_CONTENTS)
#define notify_all_from_inside(p,c,m)       notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE)
#define notify_all_from_inside_saypose(p,c,m) notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_SAYPOSE)
#define notify_all_from_inside_html(p,c,m)  notify_check(p,c,m, MSG_ME_ALL|MSG_NBR_EXITS_A|MSG_F_UP|MSG_F_CONTENTS|MSG_S_INSIDE|MSG_HTML)

void notify_except(dbref, dbref, dbref, const UTF8 *, int key);
void notify_except2(dbref, dbref, dbref, dbref, const UTF8 *);

void notify_check(dbref target, dbref sender, const mux_string &msg, int key);
void notify_check(dbref, dbref, const UTF8 *, int);

bool Hearer(dbref);
void report(void);

bool atr_match
(
    dbref thing,
    dbref player,
    UTF8  type,
    UTF8  *str,
    UTF8  *raw_str,
    bool  check_parents
);

bool regexp_match
(
    UTF8 *pattern,
    UTF8 *str,
    int case_opt,
    UTF8 *args[],
    int nargs
);

bool list_check
(
    dbref thing,
    dbref player,
    UTF8  type,
    UTF8  *str,
    UTF8  *raw_str,
    bool  check_parent
);
bool html_escape(const UTF8 *src, UTF8 *dest, UTF8 **destp);

#define DUMP_I_NORMAL    0  // OUTPUT to the outdb through a temporary file.
#define DUMP_I_PANIC     1  // UNLOAD to a crashdb
#define DUMP_I_RESTART   2  // OUTPUT to the inputdb
#define DUMP_I_FLAT      3  // UNLOAD to a .FLAT file
#define DUMP_I_SIGNAL    4  // UNLOAD to a .FLAT file from signal.
#define NUM_DUMP_TYPES   5
void dump_database_internal(int);
void fork_and_dump(int key);

#define MUX_OPEN_INVALID_HANDLE_VALUE (-1)
bool mux_fopen(FILE **pFile, const UTF8 *filename, const UTF8 *mode);
bool mux_open(int *pfh, const UTF8 *filename, int oflag);
const UTF8 *mux_strerror(int errnum);

/* From htab.cpp */
int  cf_ntab_access(int *, UTF8 *, void *, UINT32, dbref, UTF8 *);

/* From log.cpp */
bool start_log(const UTF8 *primary, const UTF8 *secondary);
void end_log(void);
void log_perror(const UTF8 *, const UTF8 *,const UTF8 *,
            const UTF8 *);
void log_text(const UTF8 *);
void log_number(int);
void DCL_CDECL log_printf(__in_z const UTF8 *fmt, ...);
void log_name(dbref);
void log_name_and_loc(dbref);
void log_type_and_name(dbref);

#define SIZEOF_LOG_BUFFER 1024
class CLogFile
{
private:
    CLinearTimeAbsolute m_ltaStarted;
#if defined(WINDOWS_THREADS)
    CRITICAL_SECTION csLog;
#endif // WINDOWS_THREADS
#if defined(WINDOWS_FILES)
    HANDLE m_hFile;
#elif defined(UNIX_FILES)
    int    m_fdFile;
#endif // UNIX_FILES
    size_t m_nSize;
    size_t m_nBuffer;
    UTF8 m_aBuffer[SIZEOF_LOG_BUFFER];
    bool bEnabled;
    bool bUseStderr;
    UTF8 *m_pBasename;
    UTF8 m_szPrefix[32];
    UTF8 m_szFilename[SIZEOF_PATHNAME];

    bool CreateLogFile(void);
    void AppendLogFile(void);
    void CloseLogFile(void);
public:
    CLogFile(void);
    ~CLogFile(void);
    void WriteBuffer(size_t nString, const UTF8 *pString);
    void WriteString(const UTF8 *pString);
    void WriteInteger(int iNumber);
    void DCL_CDECL tinyprintf(const UTF8 *pFormatSpec, ...);
    void Flush(void);
    void SetPrefix(const UTF8 *pPrefix);
    void SetBasename(const UTF8 *pBasename);
    void StartLogging(void);
    void StopLogging(void);
};

extern CLogFile Log;

/* From look.cpp */
void look_in(dbref,dbref, int);
void show_vrml_url(dbref, dbref);
#define NUM_ATTRIBUTE_CODES 12
size_t decode_attr_flags(int aflags, UTF8 buff[NUM_ATTRIBUTE_CODES+1]);
void   decode_attr_flag_names(int aflags, UTF8 *buf, UTF8 **bufc);

/* From move.cpp */
void move_object(dbref, dbref);
void move_via_generic(dbref, dbref, dbref, int);
bool move_via_teleport(dbref, dbref, dbref, int);
void move_exit(dbref, dbref, bool, const UTF8 *, int);
void do_enter_internal(dbref, dbref, bool);

/* From object.cpp */
dbref start_home(void);
dbref default_home(void);
bool  can_set_home(dbref, dbref, dbref);
dbref new_home(dbref);
dbref clone_home(dbref, dbref);
void  divest_object(dbref);
dbref create_obj(dbref, int, const UTF8 *, int);
void  destroy_obj(dbref);
void  empty_obj(dbref);

/* From player.cpp */
dbref create_player(const UTF8 *name, const UTF8 *pass, dbref executor, bool isrobot, const UTF8 **pmsg);
void AddToPublicChannel(dbref player);
bool add_player_name(dbref player, const UTF8 *name, bool bAlias);
bool delete_player_name(dbref player, const UTF8 *name, bool bAlias);
void delete_all_player_names();
dbref lookup_player_name(UTF8 *name, bool &bAlias);
dbref lookup_player(dbref doer, UTF8 *name, bool check_who);
void load_player_names(void);
void badname_add(UTF8 *);
void badname_remove(UTF8 *);
bool badname_check(UTF8 *);
void badname_list(dbref, const UTF8 *);
void ChangePassword(dbref player, const UTF8 *szPassword);
const UTF8 *mux_crypt(const UTF8 *szPassword, const UTF8 *szSalt, int *piType);
int  QueueMax(dbref);
int  a_Queue(dbref, int);
void pcache_reload(dbref);
void pcache_init(void);

/* From predicates.cpp */
UTF8 * DCL_CDECL tprintf(__in_z const UTF8 *, ...);
void DCL_CDECL safe_tprintf_str(UTF8 *, UTF8 **, __in_z const UTF8 *, ...);
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
bool IsRestricted(const UTF8 *pName, int charset);
UTF8 *MakeCanonicalObjectName(const UTF8 *pName, size_t *pnName, bool *pbValid, int charset);
UTF8 *MakeCanonicalExitName(const UTF8 *pName, size_t *pnName, bool *pbValid);
bool ValidatePlayerName(const UTF8 *pName);
bool ok_password(const UTF8 *szPassword, const UTF8 **pmsg);
void handle_ears(dbref, bool, bool);
dbref match_possessed(dbref, dbref, UTF8 *, dbref, bool);
void parse_range(UTF8 **, dbref *, dbref *);
bool parse_thing_slash(dbref, const UTF8 *, const UTF8 **, dbref *);
bool get_obj_and_lock(dbref, const UTF8 *, dbref *, ATTR **, UTF8 *, UTF8 **);
dbref where_is(dbref);
dbref where_room(dbref);
bool locatable(dbref, dbref, dbref);
bool nearby(dbref, dbref);
bool exit_visible(dbref, dbref, int);
bool exit_displayable(dbref, dbref, int);
void did_it(dbref player, dbref thing, int what, const UTF8 *def, int owhat,
            const UTF8 *odef, int awhat, int ctrl_flags,
            const UTF8 *args[], int nargs);
bool bCanReadAttr(dbref executor, dbref target, ATTR *tattr, bool bParentCheck);
bool bCanSetAttr(dbref executor, dbref target, ATTR *tattr);
bool bCanLockAttr(dbref executor, dbref target, ATTR *tattr);

struct reference_entry
{
    dbref owner;
    dbref target;
    UTF8 *name;
};

/* From set.cpp */
bool parse_attrib(dbref, const UTF8 *, dbref *, ATTR **);
bool parse_attrib_wild(dbref, const UTF8 *, dbref *, bool, bool, bool);
void find_wild_attrs(dbref player, dbref thing, const UTF8 *str,
                     bool check_exclude, bool hash_insert, bool get_locks);
dbref match_controlled_handler(dbref player, const UTF8 *name, bool bQuiet);
#define match_controlled(player,name)       match_controlled_handler(player, name, false)
#define match_controlled_quiet(player,name) match_controlled_handler(player, name, true)
void set_modified(dbref thing);
void TranslateFlags_Clone
(
    FLAG     aClearFlags[3],
    dbref    executor,
    int      key
);
void TranslateFlags_Chown
(
    FLAG     aClearFlags[3],
    FLAG     aSetFlags[3],
    bool    *bClearPowers,
    dbref    executor,
    int      key
);
void SetClearFlags(dbref thing, FLAG aClearFlags[3], FLAG aSetFlags[3]);

/* From boolexp.cpp */
bool eval_boolexp(dbref, dbref, dbref, BOOLEXP *);
BOOLEXP *parse_boolexp(dbref, const UTF8 *, bool);
bool eval_boolexp_atr(dbref, dbref, dbref, UTF8 *);

/* From functions.cpp */
bool xlate(UTF8 *);

#define IEEE_MAKE_NAN  1
#define IEEE_MAKE_IND  2
#define IEEE_MAKE_PINF 3
#define IEEE_MAKE_NINF 4

double MakeSpecialFloat(int iWhich);

/* From unparse.cpp */
UTF8 *unparse_boolexp(dbref, BOOLEXP *);
UTF8 *unparse_boolexp_quiet(dbref, BOOLEXP *);
UTF8 *unparse_boolexp_decompile(dbref, BOOLEXP *);
UTF8 *unparse_boolexp_function(dbref, BOOLEXP *);

/* From walkdb.cpp */
int chown_all(dbref from_player, dbref to_player, dbref acting_player, int key);
void olist_push(void);
void olist_pop(void);
void olist_add(dbref);
dbref olist_first(void);
dbref olist_next(void);

/* From wild.cpp */
bool wild(UTF8 *, UTF8 *, UTF8 *[], int);
bool wild_match(UTF8 *, const UTF8 *);
bool quick_wild(const UTF8 *, const UTF8 *);

/* From command.cpp */
bool check_access(dbref player, int mask);
void cache_prefix_cmds(void);
UTF8 *process_command(dbref executor, dbref caller, dbref enactor, int, bool,
    UTF8 *, const UTF8 *[], int);
size_t LeftJustifyString(UTF8 *field, size_t nWidth, const UTF8 *value);
size_t RightJustifyNumber(UTF8 *field, size_t nWidth, INT64 value, UTF8 chFill);

#define Protect(f) (cmdp->perms & f)

#define Invalid_Objtype(x) \
((Protect(CA_LOCATION) && !Has_location(x)) || \
 (Protect(CA_CONTENTS) && !Has_contents(x)) || \
 (Protect(CA_PLAYER) && !isPlayer(x)))

/* from db.cpp */
bool Commer(dbref);
void s_Pass(dbref, const UTF8 *);
void s_Name(dbref, const UTF8 *);
void s_Moniker(dbref thing, const UTF8 *s);
const UTF8 *Name(dbref thing);
const UTF8 *PureName(dbref thing);
const UTF8 *Moniker(dbref thing);
FWDLIST *fwdlist_load(dbref player, UTF8 *atext);
void fwdlist_set(dbref, FWDLIST *);
void fwdlist_clr(dbref);
int  fwdlist_rewrite(FWDLIST *, UTF8 *);
FWDLIST *fwdlist_get(dbref);
void atr_push(void);
void atr_pop(void);
int  atr_head(dbref, unsigned char **);
int  atr_next(UTF8 **);
int  init_dbfile(UTF8 *game_dir_file, UTF8 *game_pag_file, int nCachePages);
void atr_cpy(dbref dest, dbref source, bool bInternal);
void atr_chown(dbref);
void atr_clr(dbref, int);
void atr_add_raw_LEN(dbref thing, int atr, const UTF8 *szValue, size_t nValue);
void atr_add_raw(dbref, int, const UTF8 *);
void atr_add(dbref, int, const UTF8 *, dbref, int);
void atr_set_flags(dbref, int, int);
const UTF8 *atr_decode_flags_owner(const UTF8 *iattr, dbref *owner, int *flags);

// The atr_get family only looks up attributes on the object itself (not parents)
//
const UTF8 *atr_get_raw_LEN(dbref, int, size_t *);
const UTF8 *atr_get_raw(dbref, int);
UTF8 *atr_get_LEN(dbref, int, dbref *, int *, size_t *);
UTF8 *atr_get_real(const UTF8 *tag, dbref, int, dbref *, int *, const UTF8 *, const int);
#define atr_get(g,t,a,o,f) atr_get_real((UTF8 *)g,t,a,o,f, (UTF8 *)__FILE__, __LINE__)

// The atr_pget family looks up attributes on the object or parent (if set)
//
UTF8 *atr_pget_LEN(dbref, int, dbref *, int *, size_t *);
UTF8 *atr_pget_real(dbref, int, dbref *, int *, const UTF8 *, const int);
#define atr_pget(t,a,o,f) atr_pget_real(t,a,o,f, (UTF8 *)__FILE__, __LINE__)
UTF8 *atr_get_str_LEN(UTF8 *s, dbref, int, dbref *, int *, size_t *);
UTF8 *atr_get_str(UTF8 *, dbref, int, dbref *, int *);
UTF8 *atr_pget_str_LEN(UTF8 *, dbref, int, dbref *, int *, size_t *);
UTF8 *atr_pget_str(UTF8 *, dbref, int, dbref *, int *);
bool atr_get_info(dbref, int, dbref *, int *);
bool atr_pget_info(dbref, int, dbref *, int *);
void atr_free(dbref);
bool check_zone_handler(dbref player, dbref thing, bool bPlayerCheck);
#define check_zone(player, thing) check_zone_handler(player, thing, false)
void ReleaseAllResources(dbref obj);
bool fwdlist_ck(dbref player, dbref thing, int anum, UTF8 *atext);

extern int anum_alc_top;

/* Command handler keys */

#define ATTRIB_ACCESS   1   /* Change access to attribute */
#define ATTRIB_RENAME   2   /* Rename attribute */
#define ATTRIB_DELETE   4   /* Delete attribute */
#define ATTRIB_INFO     8   /* Info (number, flags) about attribute */
#define BOOT_QUIET      1   /* Inhibit boot message to victim */
#define BOOT_PORT       2   /* Boot by port number */
#define BREAK_INLINE    1   // Evaluate @break action inline
#define BREAK_QUEUED    2   // Queue @break action.
#define CEMIT_NOHEADER  1   /* Channel emit without header */
#define CHOWN_ONE       1   /* item = new_owner */
#define CHOWN_ALL       2   /* old_owner = new_owner */
#define CHOWN_NOSTRIP   4   /* Don't strip (most) flags from object */
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
#define CLONE_NOSTRIP   128 // Don't strip (most) flags from object.
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
#define CSET_LOG_TIME  10   // Add timestamps to logs
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
#define EDIT_CHANNEL_CCHOWN   0  /* @cchown */
#define EDIT_CHANNEL_CCHARGE  1  /* @ccharge */
#define EDIT_CHANNEL_CPFLAGS  2  /* @cpflags */
#define EDIT_CHANNEL_COFLAGS  3  /* @coflags */
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
#define FOLDER_SET      1   /* Tweak folder settings -- e.g., name */
#define FOLDER_READ     2   /* Read a message in the given folder */
#define FOLDER_FILE     4   /* Move message(s) to a folder */
#define FOLDER_LIST     8   /* List the contents of a folder */
#define FLAG_REMOVE     1   // Remove a flag alias
#define GET_QUIET       1   /* Don't do osucc/asucc if control */
#define GIVE_QUIET      64  /* Inhibit give messages */
#define GLOB_ENABLE     1   /* key to enable */
#define GLOB_DISABLE    2   /* key to disable */
//#define GLOB_LIST       3   /* key to list */
#define HALT_ALL        1   /* halt everything */

#define CEF_HOOK_BEFORE    0x00000001UL  /* BEFORE hook */
#define CEF_HOOK_AFTER     0x00000002UL  /* AFTER hook */
#define CEF_HOOK_PERMIT    0x00000004UL  /* PERMIT hook */
#define CEF_HOOK_IGNORE    0x00000008UL  /* IGNORE hook */
#define CEF_HOOK_IGSWITCH  0x00000010UL  /* BFAIL hook */
#define CEF_HOOK_AFAIL     0x00000020UL  /* AFAIL hook */
#define CEF_HOOK_CLEAR     0x00000040UL  /* CLEAR hook */
#define CEF_HOOK_LIST      0x00000080UL  /* LIST hooks */
#define CEF_HOOK_ARGS      0x00000100UL  /* ARGS hooks */
#define CEF_HOOK_MASK      (  CEF_HOOK_BEFORE|CEF_HOOK_AFTER|CEF_HOOK_PERMIT|CEF_HOOK_IGNORE \
                           |  CEF_HOOK_IGSWITCH|CEF_HOOK_AFAIL|CEF_HOOK_CLEAR|CEF_HOOK_LIST  \
                           |  CEF_HOOK_ARGS)
#define HOOKMASK(x)        ((x) & CEF_HOOK_MASK)

#define CEF_ALLOC          0x00000200UL  // CMDENT was allocated.
#define CEF_VISITED        0x00000400UL  // CMDENT was visted during finish_cmdtab.

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
#define NFY_MASK        3   // Lower two bits form non-SW_MULTIPLE part.
#define NFY_QUIET       4   /* Suppress "Notified." message */
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
#define REFERENCE_LIST  1   /* List @references */
#define SAY_SAY         1   /* say in current room */
#define SAY_NOSPACE     1   /* OR with xx_EMIT to get nospace form */
#define SAY_POSE        2   /* pose in current room */
#define SAY_POSE_NOSPC  3   /* pose w/o space in current room */
#define SAY_PREFIX      4   /* first char indicates formatting */
#define SAY_EMIT        5   /* emit in current room */
#define SAY_NOEVAL      8   // Don't eval message
//#define SAY_GRIPE       16  /* Complain to management */
//#define SAY_NOTE        17  /* Comment to log for wizards */
#define SAY_HERE        64  /* Output to current location */
#define SAY_ROOM        128 /* Output to containing room */
#define SAY_HTML        256 /* Don't output a newline */
#define SET_QUIET       1   /* Don't display 'Set.' message. */
#define SHOUT_DEFAULT   0   /* Default @wall message */
#define SHOUT_WIZARD    1   /* @wizwall */
#define SHOUT_ADMIN     2   /* @wall/admin */
#define SHOUT_NOTAG     4   /* Don't put Broadcast: in front (additive) */
#define SHOUT_EMIT      8   // Don't display name
#define SHOUT_POSE      16  // Pose @wall message
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
#define SWITCH_MASK     3   /* Includes lower 2 bits */
#define SWITCH_NOTIFY   4   /* Send a @notify after the @switch is completed */
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
#define TRIG_NOTIFY     2   /* Send a @notify after the @trigger is completed. */
#define TWARP_QUEUE     1   /* Warp the wait and sem queues */
#define TWARP_DUMP      2   /* Warp the dump interval */
#define TWARP_CLEAN     4   /* Warp the cleaning interval */
#define TWARP_IDLE      8   /* Warp the idle check interval */
/* empty       16 */
#define TWARP_EVENTS    32  /* Warp the events checking interval */
#define VERB_NONAME     1   // Do not preprend name to odefault.
#define WAIT_UNTIL      1   // Absolute UTC seconds instead of delta.

/* Character Set Restrictions */

#define ALLOW_CHARSET_ASCII       0x00000001UL
#define ALLOW_CHARSET_HANGUL      0x00000002UL
#define ALLOW_CHARSET_HIRAGANA    0x00000004UL
#define ALLOW_CHARSET_KANJI       0x00000008UL
#define ALLOW_CHARSET_KATAKANA    0x00000010UL
#define ALLOW_CHARSET_8859_1      0x00000020UL
#define ALLOW_CHARSET_8859_2      0x00000040UL

/* Password Encryption Methods */

#define CRYPT_DEFAULT     0x00000000UL  // The default is SHA1
#define CRYPT_FAIL        0x00000001UL  // Failure -- not a request.
#define CRYPT_SHA1        0x00000002UL
#define CRYPT_DES         0x00000004UL
#define CRYPT_DES_EXT     0x00000008UL  // Not requested.
#define CRYPT_CLEARTEXT   0x00000010UL  // Not requested.
#define CRYPT_MD5         0x00000020UL
#define CRYPT_SHA256      0x00000040UL
#define CRYPT_SHA512      0x00000080UL
#define CRYPT_P6H_XX      0x00000100UL  // From PennMUSH flatfile (XX... style).
#define CRYPT_P6H_VAHT    0x00000200UL  // From PennMUSH flatfile (V:ALGO:HASH:TIMESTAMP)
#define CRYPT_OTHER       0x00000400UL  // Not recognized.

extern NAMETAB method_nametab[];

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

#define MSG_SRC_KILL    0x10000000UL    /* Message originated from kill */
#define MSG_SRC_GIVE    0x20000000UL    /* Message originated from give */
#define MSG_SRC_PAGE    0x40000000UL    /* Message originated from page */
#define MSG_SRC_COMSYS  0x80000000UL    /* Message originated from comsys */
#define MSG_SRC_GENERIC 0x00000000UL    /* Message originated from 'none of the above' */
#define MSG_SRC_MASK    (MSG_SRC_KILL|MSG_SRC_GIVE|MSG_SRC_PAGE|MSG_SRC_COMSYS)

#define MSG_ME_ALL      (MSG_ME|MSG_INV_EXITS|MSG_FWDLIST)
#define MSG_F_CONTENTS  (MSG_INV)
#define MSG_F_UP        (MSG_NBR_A|MSG_LOC_A)
#define MSG_F_DOWN      (MSG_INV_L)

#define DecodeMsgSource(x) ((x)&MSG_SRC_MASK)

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
    if ((((key) & mudconf.log_options) != 0) && start_log(T(p), T(s))) {
#define ENDLOG \
    end_log(); }
#define LOG_SIMPLE(key,p,s,m) \
    STARTLOG(key,p,s) \
        log_text(m); \
    ENDLOG

extern const UTF8 *NOMATCH_MESSAGE;
extern const UTF8 *AMBIGUOUS_MESSAGE;
extern const UTF8 *NOPERM_MESSAGE;
extern const UTF8 *FUNC_FAIL_MESSAGE;
extern const UTF8 *FUNC_NOMATCH_MESSAGE;
extern const UTF8 *OUT_OF_RANGE;
extern const UTF8 *FUNC_NOT_FOUND;
extern const UTF8 *FUNC_AMBIGUOUS;
extern const UTF8 *FUNC_NOPERM_MESSAGE;
extern const UTF8 *OUT_OF_MEMORY;
extern const UTF8 *NOT_CONNECTED;

#define safe_nothing(b,p)   safe_copy_buf(FUNC_FAIL_MESSAGE,3,(b),(p))
#define safe_noperm(b,p)    safe_copy_buf(FUNC_NOPERM_MESSAGE,21,(b),(p))
#define safe_nomatch(b,p)   safe_copy_buf(FUNC_NOMATCH_MESSAGE,12,(b),(p))
#define safe_range(b,p)     safe_copy_buf(OUT_OF_RANGE,16,(b),(p))
#define safe_ambiguous(b,p) safe_copy_buf(FUNC_AMBIGUOUS,13,(b),(p))
#define safe_notfound(b,p)  safe_copy_buf(FUNC_NOT_FOUND,13,(b),(p))
#define safe_notconnected(b,p)  safe_copy_buf(NOT_CONNECTED,17,(b),(p))

int  ReplaceFile(UTF8 *old_name, UTF8 *new_name);
void RemoveFile(UTF8 *name);
void destroy_player(dbref agent, dbref victim);
void do_pemit_list
(
    dbref player,
    int key,
    bool bDoContents,
    int pemit_flags,
    UTF8 *list,
    int chPoseType,
    UTF8 *message
);
void do_pemit_single
(
    dbref player,
    int key,
    bool bDoContents,
    int pemit_flags,
    UTF8 *recipient,
    int chPoseType,
    UTF8 *message
);
void do_say(dbref executor, dbref caller, dbref enactor, int eval, int key,
                   UTF8 *message, const UTF8 *cargs[], int ncargs);

int  boot_off(dbref player, const UTF8 *message);
void do_mail_clear(dbref player, UTF8 *msglist);
void do_mail_purge(dbref player);
void malias_cleanup(dbref player);
void count_mail(dbref player, int folder, int *rcount, int *ucount, int *ccount);
void check_mail_expiration(void);
void check_mail(dbref player, int folder, bool silent);
const UTF8 *mail_fetch_message(dbref player, int num);
int  mail_fetch_from(dbref player, int num);
void raw_notify_html(dbref player, const mux_string &sMsg);
void do_lock(dbref executor, dbref caller, dbref enactor, int eval, int key,
                    int nargs, UTF8 *name, UTF8 *keytext, const UTF8 *cargs[], int ncargs);
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

extern long DebugTotalFiles;
extern pid_t game_pid;

// From timer.cpp
//
void init_timer(void);
void dispatch_DatabaseDump(void *pUnused, int iUnused);
void dispatch_FreeListReconstruction(void *pUnused, int iUnused);
void dispatch_IdleCheck(void *pUnused, int iUnused);
void dispatch_CheckEvents(void *pUnused, int iUnused);
void dispatch_KeepAlive(void *pUnused, int iUnused);
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
// please remember to maintain this property.
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

    void Shrink(void);
    bool Insert(PTASK_RECORD, SCHCMP *);
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
    void Shrink(void);

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
double mux_strtod(const UTF8 *s00, UTF8 **se);
UTF8 *mux_dtoa(double d, int mode, int ndigits, int *decpt, int *sign,
             UTF8 **rve);

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
void cache_cleanup(void);
extern CLinearTimeAbsolute cs_ltime;

// From speech.cpp
//
UTF8 *modSpeech(dbref player, UTF8 *message, bool bWhich, UTF8 *command);

// From funceval.cpp
//
#ifdef DEPRECATED
void stack_clr(dbref obj);
#endif // DEPRECATED
bool parse_and_get_attrib(dbref, UTF8 *[], UTF8 **, dbref *, dbref *, int *, UTF8 *, UTF8 **);

DEFINE_FACTORY(CLogFactory)

typedef struct ServerEventsSinkNode
{
    mux_IServerEventsSink        *pSink;
    struct ServerEventsSinkNode  *pNext;
} ServerEventsSinkNode;
extern ServerEventsSinkNode *g_pServerEventsSinkListHead;

DEFINE_FACTORY(CServerEventsSourceFactory)
DEFINE_FACTORY(CQueryClientFactory)
DEFINE_FACTORY(CFunctionsFactory)

#endif // EXTERNS_H
