// functions.h -- declarations for functions & function processing.
//
// $Id: functions.h,v 1.4 2004-04-17 20:48:49 sdennis Exp $
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

extern void init_functab(void);
extern void list_functable(dbref);

/* Special handling of separators. */

#define print_sep(s,b,p) \
if (s) { \
    if (s != '\r') { \
        safe_chr(s,b,p); \
    } else { \
        safe_str("\r\n",b,p); \
    } \
}

#define MAX_SEP_LEN 15
typedef struct
{
    char str[MAX_SEP_LEN+1];
} SEP;

// dlfags in delim_check() accepts the following options.
//
#define DELIM_DFLT   0x0000  // Default processing.
#define DELIM_EVAL   0x0001  // Evaluate delimiter.
#define DELIM_NULL   0x0002  // Allow '@@'.
#define DELIM_CRLF   0x0004  // Allow '%r'.
#define DELIM_STRING 0x0008  // Multi-character.

extern bool delim_check
(
    char *buff, char **bufc,
    dbref executor, dbref caller, dbref enactor,
    char *fargs[], int nfargs,
    char *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
);

extern int list2arr(char *arr[], int maxlen, char *list, char sep);

// This is the prototype for functions
//
#define FUNCTION(x) \
    void x(char *buff, char **bufc, dbref executor, dbref caller,     \
        dbref enactor, char *fargs[], int nfargs, char *cargs[],      \
        int ncargs)

// This is for functions that take an optional delimiter character.
//
#define varargs_preamble(xnargs)                                      \
    if (!delim_check(buff, bufc, executor, caller, enactor,           \
        fargs, nfargs, cargs, ncargs, xnargs, &sep, DELIM_DFLT))      \
        return;

#define evarargs_preamble(xnargs)                                     \
    if (!delim_check(buff, bufc, executor, caller, enactor,           \
        fargs, nfargs, cargs, ncargs, xnargs, &sep, DELIM_EVAL))      \
        return;

#define svarargs_preamble(xnargs)                                     \
    if (!delim_check(buff, bufc, executor, caller, enactor,           \
        fargs, nfargs, cargs, ncargs, xnargs-1, &sep, DELIM_DFLT))    \
        return;                                                       \
    if (nfargs < xnargs)                                              \
        osep = sep;                                                   \
    else if (!delim_check(buff, bufc, executor, caller, enactor,      \
        fargs, nfargs, cargs, ncargs, xnargs, &osep, DELIM_NULL|DELIM_CRLF)) \
        return;

#define sevarargs_preamble(xnargs)                                    \
    if (!delim_check(buff, bufc, executor, caller, enactor,           \
        fargs, nfargs, cargs, ncargs, xnargs-1, &sep, DELIM_EVAL))    \
        return;                                                       \
    if (nfargs < xnargs)                                              \
        osep.str[0] = ' ';                                            \
    else if (!delim_check(buff, bufc, executor, caller, enactor,      \
        fargs, nfargs, cargs, ncargs, xnargs, &osep, DELIM_EVAL|DELIM_NULL|DELIM_CRLF)) \
        return;

#endif // !__FUNCTIONS_H
