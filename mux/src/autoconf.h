#ifndef AUTOCONF_H
#define AUTOCONF_H

#include "copyright.h"

#define _WIN32_WINNT 0x0400
#define FD_SETSIZE      512
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

#include <malloc.h>
#include <crtdbg.h>

/* ---------------------------------------------------------------------------
 * Configuration section:
 *
 * These defines are written by the configure script.
 * Change them if need be
 */

/* Define if we have stdlib.h et al */
#define STDC_HEADERS
/* Define if we have string.h instead of strings.h */
#define USG
/* Define if we have unistd.h */
#undef HAVE_UNISTD_H
/* Define if we have memory.h and need it to get memcmp et al */
#undef NEED_MEMORY_H
/* signal() return type */
#define RETSIGTYPE void
#undef HAVE_WAIT3
/* Define if struct tm has a timezone member */
#undef HAVE_TM_ZONE
/* Define if tzname[] exists */
#undef HAVE_TZNAME
/* Define if setrlimit exists */
#undef HAVE_SETRLIMIT
/* Define if mktime exists */
#undef HAVE_MKTIME
/* Define if getdtablesize exists */
#undef HAVE_GETDTABLESIZE
/* Define if getpagesize exists */
#undef HAVE_GETPAGESIZE
/* Define if gettimeofday exists */
#undef HAVE_GETTIMEOFDAY
/* Define if sys_siglist[] exists */
#undef SYS_SIGLIST_DECLARED
/* Define if sys_signame[] exists */
#undef HAVE_SYS_SIGNAME
/* Define if index/rindex/mem??? are defined in string.h */
#undef INDEX_IN_STRING_H
/* Define if malloc/realloc/free are defined in stdlib.h */
#define MALLOC_IN_STDLIB_H
/* Define if calling signal with SIGCHLD when handling SIGCHLD blows chow */
#undef SIGNAL_SIGCHLD_BRAINDAMAGE
/* Define if errno.h exists */
#define HAVE_ERRNO_H
/* Define if sys/select.h exists */
#undef HAVE_SYS_SELECT_H
/* Define if sys/rusage.h exists */
#undef HAVE_SYS_RUSAGE_H
/* Define if Big Endian */ 
/* #undef WORDS_BIGENDIAN */
/* Define if Little Endian */
#define WORDS_LITTLEENDIAN 1
/* Define if Unknown Endian */
/* #undef WORDS_UNKNOWN */ 
/* Define if const is broken */
#undef const
/* sizeof(short) */
#define SIZEOF_SHORT 2
/* sizeof(unsigned short) */
#define SIZEOF_UNSIGNED_SHORT 2
/* sizeof(int) */
#define SIZEOF_INT 4
/* sizeof(unsigned int) */
#define SIZEOF_UNSIGNED_INT 4
/* sizeof(long) */
#define SIZEOF_LONG 4
/* sizeof(unsigned long) */
#define SIZEOF_UNSIGNED_LONG 4
/* Define if we need to redef index/bcopy et al to their SYSV counterparts */
#undef NEED_INDEX_DCL
/* Define if we need to declare malloc et al */
#undef NEED_MALLOC_DCL
/* Define if you need to declare sys_errlist yourself */
#undef NEED_SYS_ERRLIST_DCL
/* Define if you need to declare _sys_errlist yourself */
#undef NEED_SYS__ERRLIST_DCL
/* Define if you need to declare perror yourself */
#undef NEED_PERROR_DCL
/* Define if you need to declare sprintf yourself */
#undef NEED_SPRINTF_DCL
/* Define if you need to declare getrlimit yourself */
#undef NEED_GETRLIMIT_DCL
/* Define if you need to declare getrusage yourself */
#undef NEED_GETRUSAGE_DCL
/* Define if struct linger is defined */
#define HAVE_LINGER
/* Define if signal handlers have a struct sigcontext as their third arg */
#undef HAVE_STRUCT_SIGCONTEXT
/* Define if stdio.h defines lots of extra functions */
#define EXTENDED_STDIO_DCLS
/* Define if sys/socket.h defines lots of extra functions */
#undef EXTENDED_SOCKET_DCLS
/* Define if sys/wait.h defines union wait. */
#undef HAVE_UNION_WAIT
/* Define if you have IEEE floating-point formatted numbers */
#define HAVE_IEEE_FP_FORMAT 1
/* Define if your IEEE floating-point library can generate NaN */
#define HAVE_IEEE_FP_SNAN 1
/* Define if your platform computes the integer quotient as the smallest */
/* integer greater than or or equal to the algebraic quotient. For       */
/* example, -9/5 gives -1                                                */
#define SMALLEST_INT_GTE_NEG_QUOTIENT 1
/* Define if the character special file /dev/urandom is present */
#undef HAVE_DEV_URANDOM
/* Define if your system has the in_addr_t type */
#undef HAVE_IN_ADDR_T

/* ---------------------------------------------------------------------------
 * Setup section:
 *
 * Load system-dependent header files.
 */

/* Prototype templates for ANSI C and traditional C */

#ifdef STDC_HEADERS
#include <io.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <process.h>
#else
#include <varargs.h>
extern int  atoi(const char *);
extern double   atof(const char *);
extern long atol(const char *);
#endif

#ifdef NEED_MEMORY_H
#include <memory.h>
#endif

#if defined(USG) || defined(STDC_HEADERS)
#include <string.h>
#ifdef NEED_INDEX_DCL
#define index       strchr
#define rindex      strrchr
#define bcopy(s,d,n)    memmove(d,s,n)
#define bcmp(s1,s2,n)   memcmp(s1,s2,n)
#define bzero(s,n)  memset(s,0,n)
#endif
#else
#include <strings.h>
extern char *strchr(char *, char);
extern void bcopy(char *, char *, int);
extern void bzero(char *, int);
#endif
#define bcopy(d1, d2, n) memmove(d2, d1, n)
#define bcmp(d1, d2, n) memcmp(d1, d2, n)
#define bzero(s,n)  memset(s,0,n)

#ifdef HAVE_ERRNO_H
#include <errno.h>
#ifdef NEED_PERROR_DCL
extern void perror(const char *);
#endif
#else
extern int errno;
extern void perror(const char *);
#endif

#ifdef NEED_SYS_ERRLIST_DCL
extern char *sys_errlist[];
#endif

#include <sys/types.h>
#include <stdio.h>

#include <fcntl.h>

typedef int     dbref;
typedef int     FLAG;
typedef int     POWER;
typedef char    boolexp_type;
typedef char    IBUF[16];

#endif /* AUTOCONF_H */

