// functions.h -- declarations for functions & function processing.
//
// $Id: functions.h,v 1.22 2006-01-27 21:22:28 jake Exp $
//

#include "copyright.h"

#ifndef __FUNCTIONS_H
#define __FUNCTIONS_H

typedef struct tagFun
{
    char *name;     // function name
    void (*fun)(char *buff, char **bufc, dbref executor, dbref caller,
        dbref enactor, char *fargs[], int nfargs, char *cargs[],
        int ncargs);  // handler
    int maxArgsParsed;// Maximum number of arguments parsed.
    int minArgs;      // Minimum number of args needed or expected
    int maxArgs;      // Maximum number of arguments permitted
    int flags;        // Function flags
    int perms;        // Access to function
} FUN;

typedef struct ufun {
    char *name;     /* function name */
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

void init_functab(void);
void list_functable(dbref);
extern UFUN *ufun_head;

/* Special handling of separators. */

#define print_sep(ps,b,p) safe_copy_buf((ps)->str,(ps)->n,(b),(p))

#define MAX_SEP_LEN 50
typedef struct
{
    size_t n;
    char   str[MAX_SEP_LEN+1];
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
    char *buff, char **bufc,
    dbref executor, dbref caller, dbref enactor,
    char *fargs[], int nfargs,
    char *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
);

void arr2list(char *arr[], int alen, char *list, char **bufc, SEP *psep);
int list2arr(char *arr[], int maxlen, char *list, SEP *psep);
char *trim_space_sep(char *str, SEP *psep);
char *trim_space_sep_LEN(char *str, size_t nStr, SEP *psep, size_t *nTrim);
char *next_token(char *str, SEP *psep);
char *split_token(char **sp, SEP *psep);
int countwords(char *str, SEP *psep);

// This is the prototype for functions
//
#define FUNCTION(x) \
    void x(char *buff, char **bufc, dbref executor, dbref caller,     \
        dbref enactor, char *fargs[], int nfargs, char *cargs[],      \
        int ncargs)

// This is for functions that take an optional delimiter character.
//
#define OPTIONAL_DELIM(iSep, Sep, dflags)                        \
    delim_check(buff, bufc, executor, caller, enactor,           \
        fargs, nfargs, cargs, ncargs, (iSep), &(Sep), (dflags))

#define XFUNCTION(x) void x(char *buff, char **bufc, dbref executor,    \
 dbref caller, dbref enactor, char *fargs[], int nfargs, char *cargs[],        \
 int ncargs)

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
// In funceval.cpp
XFUNCTION(fun_alphamax);
XFUNCTION(fun_alphamin);
XFUNCTION(fun_andflags);
XFUNCTION(fun_ansi);
XFUNCTION(fun_beep);
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
XFUNCTION(fun_lit);
XFUNCTION(fun_localize);
XFUNCTION(fun_lparent);
XFUNCTION(fun_lrand);
XFUNCTION(fun_lrooms);
XFUNCTION(fun_mail);
XFUNCTION(fun_mailfrom);
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

#ifdef SIDE_EFFECT_FUNCTIONS
XFUNCTION(fun_create);
XFUNCTION(fun_emit);
XFUNCTION(fun_link);
XFUNCTION(fun_oemit);
XFUNCTION(fun_pemit);
XFUNCTION(fun_remit);
XFUNCTION(fun_cemit);
XFUNCTION(fun_set);
XFUNCTION(fun_tel);
XFUNCTION(fun_textfile);
#endif

// In netcommon.cpp
XFUNCTION(fun_doing);
XFUNCTION(fun_host);
XFUNCTION(fun_motd);
XFUNCTION(fun_poll);
XFUNCTION(fun_siteinfo);
// In quota.cpp
XFUNCTION(fun_hasquota);

#endif // !__FUNCTIONS_H
