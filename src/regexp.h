
/* regexp.h - Definitions etc. for regexp(3) routines. */

#ifndef __REGEXP_H
#define __REGEXP_H

/*
 * This is Henry Spencer's code.
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */

/*
 * -DCHARMASK=0377       compilers without unsigned char (ok on those with it)
 */
#define CHARMASK 0377

#define NSUBEXP  10
typedef struct regexp {
        char *startp[NSUBEXP];
        char *endp[NSUBEXP];
        char regstart;          /* Internal use only. */
        char reganch;           /* Internal use only. */
        char *regmust;          /* Internal use only. */
        int regmlen;            /* Internal use only. */
        char program[1];        /* Unwarranted chumminess with compiler. */
} regexp;

/*
 * The first byte of the regexp internal "program" is actually this magic
 * number; the start node begins in the second byte.
 */
#define MAGIC   0234U

#endif /* __REGEXP_H */
