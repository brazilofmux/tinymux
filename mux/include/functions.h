/*! \file functions.h
 * \brief declarations for functions & function processing.
 *
 */

#include "copyright.h"

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <list>
#include <string>

typedef struct tagFun
{
    const UTF8 *name;     // function name
    void (*fun)(struct tagFun *fp, UTF8 *buff, UTF8 **bufc, dbref executor, dbref caller, dbref enactor,
        int eval, const UTF8 * const fargs[], int nfargs, const UTF8 *cargs[], int ncargs);  // handler
    int maxArgsParsed;// Maximum number of arguments parsed.
    int minArgs;      // Minimum number of args needed or expected
    int maxArgs;      // Maximum number of arguments permitted
    int flags;        // Function flags
    int perms;        // Access to function
    void *vp;
} FUN;

typedef struct ufun {
    std::string name;   /* function name */
    dbref obj;          /* Object ID */
    int atr;            /* Attribute ID */
    int flags;          /* Function flags */
    int perms;          /* Access to function */

    ufun() : obj(-1), atr(0), flags(0), perms(0) {}
} UFUN;

#define FN_NOEVAL   2   // Don't evaluate args to function.
#define FN_PRIV     4   // Perform user-def function as holding obj.
#define FN_PRES     8   // Preseve r-regs before user-def functions.
#define FN_RESTRICT 32  // Only callable by wizard code (including inherited).

#define FN_LIST     1   // Corresponds to /list switch. -not- used in
                        // UFUN structure.
#define FN_DELETE   16  // Corresponds to /delete switch. Not used in
                        // UFUN structure.

#define MAX_UFUN_NAME_LEN (SBUF_SIZE-1)

void init_functab(void);
void list_functable(dbref);
extern std::list<UFUN> ufun_list;

/* Special handling of separators. */

#define print_sep(ps,b,p) safe_copy_buf((ps).str,(ps).n,(b),(p))

#define MAX_SEP_LEN 50
typedef struct
{
    size_t n;
    UTF8   str[MAX_SEP_LEN+1];
} SEP;

extern SEP sepSpace;

// dflags in delim_check() accepts the following options.
//
#define DELIM_DFLT   0x0000  // Default processing.
#define DELIM_EVAL   0x0001  // Evaluate delimiter.
#define DELIM_NULL   0x0002  // Allow '@@'.
#define DELIM_CRLF   0x0004  // Allow '%r'.
#define DELIM_STRING 0x0008  // Multi-character.
#define DELIM_INIT   0x0010  // The sep is initialized.

bool delim_check
(
    UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    const UTF8 * const fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
);

void arr2list(UTF8 *arr[], int alen, UTF8 *list, UTF8 **bufc, const SEP &sep);
// Both splitters write arr[i] only for i < their return value, and every
// caller must read no further.  Callers currently hand these tables over
// uninitialized (#2145), so a past-count read is garbage: usually zeros
// from fresh pages (looks fine), and on a recycled allocation a stale but
// valid pointer that dereferences without faulting -- silent wrong output,
// not the nullptr the old zeroed vectors happened to provide.  A crash is
// the exception (poisoned allocators), not the production failure mode.
// list2arr tokenizes `list` in place (destructive: separators become NULs
// in the CALLER's buffer); list2arr_nd tokenizes a private copy in
// `scratch` (>= LBUF_SIZE bytes, must outlive arr[] use) and leaves `list`
// untouched -- note it therefore also TRUNCATES input beyond LBUF_SIZE-1
// bytes, which list2arr does not; unreachable from fargs, but the two are
// not interchangeable at the extreme (#2136).
//
int list2arr(UTF8 *arr[], int maxlen, UTF8 *list, const SEP &sep);
int list2arr_nd(UTF8 *arr[], int maxlen, const UTF8 *list, const SEP &sep,
                UTF8 *scratch);
UTF8 *list_copy_for_split(UTF8 *scratch, const UTF8 *list);
UTF8 *trim_space_sep(UTF8 *str, const SEP &sep);
UTF8 *trim_space_sep_LEN(UTF8 *str, size_t nStr, const SEP &sep, size_t *nTrim);
// Non-destructive counterpart of trim_space_sep for (pointer, length)
// consumers (#2136): same trimming rule, but reports the trimmed extent
// instead of writing a NUL into the caller's buffer.
const UTF8 *trim_space_sep_n(const UTF8 *str, const SEP &sep, size_t *nLen);
UTF8 *next_token(UTF8 *str, const SEP &sep);
UTF8 *split_token(UTF8 **sp, const SEP &sep);
int countwords(const UTF8 *str, const SEP &sep);

bool check_command(dbref player, const UTF8 *name, UTF8 *buff, UTF8 **bufc);

// A writable LBUF-backed copy of one const farg (#2136), for handing to
// callees that legitimately scribble on their argument text — command
// handlers (do_link, do_pemit_*, ...) own and mutate the command text in
// normal operation, and const-poisoning that whole layer would trade one
// honest copy for hundreds of casts.  RAII; the temporary lives to the
// end of the full expression, which brackets any synchronous handler.
// Contrast list_copy_for_split (caller supplies the scratch, list-shaped)
// — this one is for opaque argument text.
//
class FargCopy
{
public:
    explicit FargCopy(const UTF8 *s)
        : m_p(alloc_lbuf("fargcopy"))
    {
        size_t n = s ? strlen(reinterpret_cast<const char *>(s)) : 0;
        if (LBUF_SIZE - 1 < n)
        {
            n = LBUF_SIZE - 1;
        }
        if (n)
        {
            memcpy(m_p, s, n);
        }
        m_p[n] = '\0';
    }
    ~FargCopy() { free_lbuf(m_p); }
    FargCopy(const FargCopy &) = delete;
    FargCopy &operator=(const FargCopy &) = delete;
    operator UTF8 *() { return m_p; }
private:
    UTF8 *m_p;
};

// The argv counterpart of FargCopy, for CS_ARGV-style handlers
// (do_trigger, do_verb) whose args[] parameter is handler-owned text.
//
class FargVec
{
public:
    FargVec(const UTF8 * const fargs[], int n)
    {
        m_n = (n < 0) ? 0 : ((MAX_ARG < n) ? MAX_ARG : n);
        for (int i = 0; i < m_n; i++)
        {
            m_a[i] = alloc_lbuf("fargvec");
            const UTF8 *s = fargs[i];
            size_t nLen = s ? strlen(reinterpret_cast<const char *>(s)) : 0;
            if (LBUF_SIZE - 1 < nLen)
            {
                nLen = LBUF_SIZE - 1;
            }
            if (nLen)
            {
                memcpy(m_a[i], s, nLen);
            }
            m_a[i][nLen] = '\0';
        }
    }
    ~FargVec()
    {
        for (int i = 0; i < m_n; i++)
        {
            free_lbuf(m_a[i]);
        }
    }
    FargVec(const FargVec &) = delete;
    FargVec &operator=(const FargVec &) = delete;
    operator UTF8 **() { return m_a; }
    UTF8 *operator[](int i) { return m_a[i]; }
    int count() const { return m_n; }
private:
    UTF8 *m_a[MAX_ARG];
    int m_n;
};

// This is the prototype for functions
//
#define FUNCTION(x) \
    void x(FUN *fp, UTF8 *buff, UTF8 **bufc, dbref executor, dbref caller,  dbref enactor, int eval, \
         const UTF8 * const fargs[], int nfargs,  const UTF8 *cargs[], int ncargs)

// This is for functions that take an optional delimiter character.
//
#define OPTIONAL_DELIM(iSep, Sep, dflags)                        \
    delim_check(buff, bufc, executor, caller, enactor, eval,     \
        fargs, nfargs, cargs, ncargs, (iSep), &(Sep), (dflags))

#define XFUNCTION(x) void x(FUN *fp, UTF8 *buff, UTF8 **bufc, dbref executor, dbref caller, dbref enactor, \
    int eval, const UTF8 * const fargs[], int nfargs, const UTF8 *cargs[], int ncargs)

// Interface for adding additional hardcode functions.
//
void function_add(FUN *fp);
void functions_add(FUN funlist[]);

// Function definitions from funceval.cpp
//

// In ast.cpp
XFUNCTION(fun_asteval);
XFUNCTION(fun_astbench);
// In jit_compiler.cpp
#if defined(TINYMUX_JIT)
XFUNCTION(fun_rvbench);
XFUNCTION(fun_jitstats);
XFUNCTION(fun_pocvm2);
#endif
// In comsys.cpp
XFUNCTION(fun_cbuffer);
XFUNCTION(fun_cdesc);
XFUNCTION(fun_cflags);
XFUNCTION(fun_channels);
XFUNCTION(fun_cmsgs);
XFUNCTION(fun_comalias);
XFUNCTION(fun_comtitle);
XFUNCTION(fun_chanobj);
XFUNCTION(fun_cmogrifier);
XFUNCTION(fun_cowner);
XFUNCTION(fun_crecall);
XFUNCTION(fun_cstatus);
XFUNCTION(fun_cusers);
XFUNCTION(fun_chanfind);
XFUNCTION(fun_chaninfo);
XFUNCTION(fun_chanusers);
XFUNCTION(fun_chanuser);
// In funceval.cpp
XFUNCTION(fun_alphamax);
XFUNCTION(fun_between);
XFUNCTION(fun_caplist);
XFUNCTION(fun_delextract);
XFUNCTION(fun_garble);
XFUNCTION(fun_moon);
XFUNCTION(fun_soundex);
XFUNCTION(fun_soundlike);
XFUNCTION(fun_crc32obj);
XFUNCTION(fun_sandbox);
XFUNCTION(fun_subnetmatch);
XFUNCTION(fun_while);
XFUNCTION(fun_wrapcolumns);
XFUNCTION(fun_alphamin);
XFUNCTION(fun_andflags);
XFUNCTION(fun_ansi);
XFUNCTION(fun_beep);
XFUNCTION(fun_baseconv);
XFUNCTION(fun_children);
XFUNCTION(fun_columns);
XFUNCTION(fun_cwho);
XFUNCTION(fun_decrypt);
XFUNCTION(fun_default);
XFUNCTION(fun_die);
XFUNCTION(fun_dumping);
XFUNCTION(fun_edefault);
XFUNCTION(fun_elements);
XFUNCTION(fun_encrypt);
XFUNCTION(fun_entrances);
XFUNCTION(fun_fcount);
XFUNCTION(fun_fdepth);
XFUNCTION(fun_findable);
XFUNCTION(fun_foreach);
XFUNCTION(fun_grab);
XFUNCTION(fun_graball);
XFUNCTION(fun_grep);
XFUNCTION(fun_grepi);
XFUNCTION(fun_regrep);
XFUNCTION(fun_regrepi);
XFUNCTION(fun_hasattr);
XFUNCTION(fun_hasattrp);
XFUNCTION(fun_hastype);
XFUNCTION(fun_ifelse);
XFUNCTION(fun_inzone);
XFUNCTION(fun_isword);
XFUNCTION(fun_last);
XFUNCTION(fun_lastcreate);
XFUNCTION(fun_lrest);
XFUNCTION(fun_letq);
XFUNCTION(fun_lit);
XFUNCTION(fun_localize);
XFUNCTION(fun_lparent);
XFUNCTION(fun_lrand);
XFUNCTION(fun_lrooms);
XFUNCTION(fun_mail);
XFUNCTION(fun_mailcount);
XFUNCTION(fun_mailflags);
XFUNCTION(fun_mailfrom);
XFUNCTION(fun_mailinfo);
XFUNCTION(fun_maillist);
XFUNCTION(fun_mailreview);
XFUNCTION(fun_mailsize);
XFUNCTION(fun_mailstats);
XFUNCTION(fun_mailsubj);
XFUNCTION(fun_malias);
XFUNCTION(fun_matchall);
XFUNCTION(fun_mix);
XFUNCTION(fun_munge);
XFUNCTION(fun_null);
XFUNCTION(fun_objeval);
XFUNCTION(fun_objmem);
XFUNCTION(fun_orflags);
XFUNCTION(fun_pack);
XFUNCTION(fun_pickrand);
XFUNCTION(fun_playmem);
XFUNCTION(fun_ports);
XFUNCTION(fun_regmatch);
XFUNCTION(fun_regmatchi);
XFUNCTION(fun_regrab);
XFUNCTION(fun_regraball);
XFUNCTION(fun_regraballi);
XFUNCTION(fun_regrabi);
XFUNCTION(fun_regedit);
XFUNCTION(fun_regediti);
XFUNCTION(fun_regeditall);
XFUNCTION(fun_regeditalli);
XFUNCTION(fun_route);
XFUNCTION(fun_scramble);
XFUNCTION(fun_shuffle);
XFUNCTION(fun_sortby);
XFUNCTION(fun_squish);
XFUNCTION(fun_step);
XFUNCTION(fun_strcat);
XFUNCTION(fun_stripansi);
XFUNCTION(fun_left);
XFUNCTION(fun_table);
XFUNCTION(fun_translate);
XFUNCTION(fun_udefault);
XFUNCTION(fun_unpack);
XFUNCTION(fun_valid);
XFUNCTION(fun_visible);
XFUNCTION(fun_zchildren);
XFUNCTION(fun_zexits);
XFUNCTION(fun_zfun);
XFUNCTION(fun_zone);
XFUNCTION(fun_zrooms);
XFUNCTION(fun_zthings);
XFUNCTION(fun_zwho);

XFUNCTION(fun_clone);
XFUNCTION(fun_create);
XFUNCTION(fun_destroy);
XFUNCTION(fun_emit);
XFUNCTION(fun_link);
XFUNCTION(fun_pose);
XFUNCTION(fun_oemit);
XFUNCTION(fun_nsemit);
XFUNCTION(fun_nsoemit);
XFUNCTION(fun_nspemit);
XFUNCTION(fun_nsremit);
XFUNCTION(fun_pemit);
XFUNCTION(fun_prompt);
XFUNCTION(fun_remit);
XFUNCTION(fun_cemit);
XFUNCTION(fun_set);
XFUNCTION(fun_attrib_set);
XFUNCTION(fun_tel);
XFUNCTION(fun_textfile);
XFUNCTION(fun_trigger);
XFUNCTION(fun_verb);
XFUNCTION(fun_wipe);

// In funcweb.cpp
XFUNCTION(fun_encode64);
XFUNCTION(fun_decode64);
XFUNCTION(fun_hmac);
XFUNCTION(fun_isjson);
XFUNCTION(fun_json);
XFUNCTION(fun_json_query);
XFUNCTION(fun_json_mod);
XFUNCTION(fun_url_escape);
XFUNCTION(fun_url_unescape);

// In netcommon.cpp
XFUNCTION(fun_doing);
XFUNCTION(fun_host);
XFUNCTION(fun_motd);
XFUNCTION(fun_poll);
XFUNCTION(fun_siteinfo);
// In quota.cpp
XFUNCTION(fun_hasquota);

#endif // !FUNCTIONS_H
