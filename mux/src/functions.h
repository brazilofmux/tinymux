// functions.h -- declarations for functions & function processing.
//
// $Id: functions.h,v 1.2 2003-02-05 06:20:59 jake Exp $
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

#define FN_NO_EVAL  2   // Don't evaluate args to function.
#define FN_PRIV     4   // Perform user-def function as holding obj.
#define FN_PRES     8   // Preseve r-regs before user-def functions.

#define FN_LIST     1   // Corresponds to /list switch. -not- used in
                        // UFUN structure.

/* Special handling of separators. */

#define print_sep(s,b,p) \
if (s) { \
    if (s != '\r') { \
        safe_chr(s,b,p); \
    } else { \
        safe_str("\r\n",b,p); \
    } \
}

extern void init_functab(void);
extern void list_functable(dbref);
extern bool delim_check
(
    char *fargs[], int nfargs, int sep_arg, char *sep, char *buff,
    char **bufc, bool eval, dbref executor, dbref caller, dbref enactor,
    char *cargs[], int ncargs, bool allow_special
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
    if (!delim_check(fargs, nfargs, xnargs, &sep, buff, bufc, false,  \
        executor, caller, enactor, cargs, ncargs, false))             \
        return;

#define evarargs_preamble(xnargs)                                     \
    if (!delim_check(fargs, nfargs, xnargs, &sep, buff, bufc, true,   \
        executor, caller, enactor, cargs, ncargs, false))             \
        return;

#define svarargs_preamble(xnargs)                                     \
    if (!delim_check(fargs, nfargs, xnargs-1, &sep, buff, bufc, false,\
        executor, caller, enactor, cargs, ncargs, false))             \
        return;                                                       \
    if (nfargs < xnargs)                                              \
        osep = sep;                                                   \
    else if (!delim_check(fargs, nfargs, xnargs, &osep, buff, bufc,   \
        false, executor, caller, enactor, cargs, ncargs, true))       \
        return;

#define sevarargs_preamble(xnargs)                                    \
    if (!delim_check(fargs, nfargs, xnargs-1, &sep, buff, bufc, true, \
        executor, caller, enactor, cargs, ncargs, false))             \
        return;                                                       \
    if (nfargs < xnargs)                                              \
        osep = ' ';                                                   \
    else if (!delim_check(fargs, nfargs, xnargs, &osep, buff, bufc,   \
        true, executor, caller, enactor, cargs, ncargs, true))        \
        return;

#endif // !__FUNCTIONS_H
