/*! \file functions.h
 * \brief declarations for functions & function processing.
 *
 */

#include "copyright.h"

#ifndef __FUNCTIONS_H
#define __FUNCTIONS_H

typedef struct tagFun
{
    const UTF8 *name;     // function name
    void (*fun)(__in struct tagFun *fp, __in UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref caller, dbref enactor,
        int eval, __in UTF8 *fargs[], int nfargs, __in const UTF8 *cargs[], int ncargs);  // handler
    int maxArgsParsed;// Maximum number of arguments parsed.
    int minArgs;      // Minimum number of args needed or expected
    int maxArgs;      // Maximum number of arguments permitted
    int flags;        // Function flags
    int perms;        // Access to function
    void *vp;
} FUN;

typedef struct ufun {
    UTF8 *name;     /* function name */
    dbref obj;      /* Object ID */
    int atr;        /* Attribute ID */
    int flags;      /* Function flags */
    int perms;      /* Access to function */
    struct ufun *next;  /* Next ufun in chain */
} UFUN;

#define FN_NOEVAL   2   // Don't evaluate args to function.
#define FN_PRIV     4   // Perform user-def function as holding obj.
#define FN_PRES     8   // Preseve r-regs before user-def functions.

#define FN_LIST     1   // Corresponds to /list switch. -not- used in
                        // UFUN structure.
#define FN_DELETE   16  // Corresponds to /delete switch. Not used in
                        // UFUN structure.

#define MAX_UFUN_NAME_LEN (SBUF_SIZE-1)

void init_functab(void);
void list_functable(dbref);
extern UFUN *ufun_head;

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
    __in UTF8 *buff, __deref_inout UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    __in UTF8 *fargs[], int nfargs,
    __in const UTF8 *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
);

void arr2list(__in_ecount(alen) UTF8 *arr[], int alen, __inout UTF8 *list, __deref_inout UTF8 **bufc, __in const SEP &sep);
int list2arr(__out_ecount(maxlen) UTF8 *arr[], int maxlen, __in UTF8 *list, __in const SEP &sep);
UTF8 *trim_space_sep(__in UTF8 *str, __in const SEP &sep);
UTF8 *trim_space_sep_LEN(__in_ecount(nStr) UTF8 *str, size_t nStr, __in const SEP &sep, __out size_t *nTrim);
UTF8 *next_token(__deref_inout UTF8 *str, const SEP &sep);
UTF8 *split_token(__deref_inout UTF8 **sp, const SEP &sep);
int countwords(__in UTF8 *str, __in const SEP &sep);

bool check_command(dbref player, const UTF8 *name, UTF8 *buff, UTF8 **bufc);

// This is the prototype for functions
//
#define FUNCTION(x) \
    void x(__in FUN *fp, __in UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref caller,  dbref enactor, int eval, \
         __in UTF8 *fargs[], int nfargs,  __in const UTF8 *cargs[], int ncargs)

// This is for functions that take an optional delimiter character.
//
#define OPTIONAL_DELIM(iSep, Sep, dflags)                        \
    delim_check(buff, bufc, executor, caller, enactor, eval,     \
        fargs, nfargs, cargs, ncargs, (iSep), &(Sep), (dflags))

#define XFUNCTION(x) void x(__in FUN *fp, __in UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref caller, dbref enactor, \
    int eval, __in UTF8 *fargs[], int nfargs, __in const UTF8 *cargs[], int ncargs)

// Interface for adding additional hardcode functions.
//
void function_add(FUN *fp);
void functions_add(FUN funlist[]);

// Function definitions from funceval.cpp
//

// In comsys.cpp
XFUNCTION(fun_channels);
XFUNCTION(fun_comalias);
XFUNCTION(fun_comtitle);
XFUNCTION(fun_chanobj);
// In funceval.cpp
XFUNCTION(fun_alphamax);
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
XFUNCTION(fun_hasattr);
XFUNCTION(fun_hasattrp);
XFUNCTION(fun_hastype);
XFUNCTION(fun_ifelse);
XFUNCTION(fun_inzone);
XFUNCTION(fun_isword);
XFUNCTION(fun_last);
XFUNCTION(fun_lastcreate);
XFUNCTION(fun_lit);
XFUNCTION(fun_localize);
XFUNCTION(fun_lparent);
XFUNCTION(fun_lrand);
XFUNCTION(fun_lrooms);
XFUNCTION(fun_mail);
XFUNCTION(fun_mailfrom);
#if defined(FIRANMUX)
XFUNCTION(fun_mailj);
#endif
XFUNCTION(fun_mailsize);
XFUNCTION(fun_mailsubj);
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
XFUNCTION(fun_scramble);
XFUNCTION(fun_shuffle);
XFUNCTION(fun_sortby);
XFUNCTION(fun_squish);
XFUNCTION(fun_step);
XFUNCTION(fun_strcat);
XFUNCTION(fun_stripansi);
XFUNCTION(fun_strtrunc);
XFUNCTION(fun_table);
XFUNCTION(fun_translate);
XFUNCTION(fun_udefault);
XFUNCTION(fun_unpack);
XFUNCTION(fun_valid);
XFUNCTION(fun_visible);
XFUNCTION(fun_zfun);
XFUNCTION(fun_zone);
XFUNCTION(fun_zwho);

#ifdef DEPRECATED
XFUNCTION(fun_empty);
XFUNCTION(fun_items);
XFUNCTION(fun_lstack);
XFUNCTION(fun_peek);
XFUNCTION(fun_pop);
XFUNCTION(fun_push);
#endif // DEPRECATED

XFUNCTION(fun_create);
XFUNCTION(fun_destroy);
XFUNCTION(fun_emit);
XFUNCTION(fun_link);
XFUNCTION(fun_oemit);
XFUNCTION(fun_pemit);
XFUNCTION(fun_remit);
XFUNCTION(fun_cemit);
XFUNCTION(fun_set);
XFUNCTION(fun_tel);
XFUNCTION(fun_textfile);
XFUNCTION(fun_trigger);
XFUNCTION(fun_wipe);
#if defined(FIRANMUX)
XFUNCTION(fun_setparent);
XFUNCTION(fun_setname);
#endif // FIRANMUX

// In netcommon.cpp
XFUNCTION(fun_doing);
XFUNCTION(fun_host);
XFUNCTION(fun_motd);
XFUNCTION(fun_poll);
XFUNCTION(fun_siteinfo);
// In quota.cpp
XFUNCTION(fun_hasquota);

#endif // !__FUNCTIONS_H
