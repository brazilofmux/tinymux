/* autoconf.h.  Generated automatically by configure.  */
/* autoconf.h.in -- System-dependent configuration information */

#ifndef AUTOCONF_H
#define AUTOCONF_H

#include "copyright.h"

/* ---------------------------------------------------------------------------
 * Configuration section:
 *
 * These defines are written by the configure script.
 * Change them if need be
 */

/* Define if we have stdlib.h et al */
#define STDC_HEADERS 1
/* Define if we have strchr */
/* #undef HAVE_STRCHR */
/* Define if we have memcpy */
/* #undef HAVE_MEMCPY */
/* Define if we have unistd.h */
#define HAVE_UNISTD_H 1
/* Define if we have memory.h and need it to get memcmp et al */
/* #undef NEED_MEMORY_H */
/* Decl for pid_t */
/* #undef pid_t */
/* signal() return type */
#define RETSIGTYPE void
/* Define if wait3 exists and works */
#define HAVE_WAIT3 1
/* Define if struct tm is not in time.h */
/* #undef TM_IN_SYS_TIME */
/* Define if struct tm has a timezone member */
#define HAVE_TM_ZONE 1
/* Define if tzname[] exists */
/* #undef HAVE_TZNAME */
/* Define if setrlimit exists */
#define HAVE_SETRLIMIT 1
/* Define if getrusage exists */
#define HAVE_GETRUSAGE 1
/* Define if timelocal exists */
#define HAVE_TIMELOCAL 1
/* Define if mktime exists */
#define HAVE_MKTIME 1
/* Define if getdtablesize exists */
#define HAVE_GETDTABLESIZE 1
/* Define if getpagesize exists */
#define HAVE_GETPAGESIZE 1
/* Define if gettimeofday exists */
#define HAVE_GETTIMEOFDAY 1
/* Define if sys_siglist[] exists */
#define SYS_SIGLIST_DECLARED 1
/* Define if sys_signame[] exists */
/* #undef HAVE_SYS_SIGNAME */
/* Define if index/rindex/mem??? are defined in string.h */
/* #undef INDEX_IN_STRING_H */
/* Define if malloc/realloc/free are defined in stdlib.h */
#define MALLOC_IN_STDLIB_H 1
/* Define if calling signal with SIGCHLD when handling SIGCHLD blows chow */
/* #undef SIGNAL_SIGCHLD_BRAINDAMAGE */
/* Define if errno.h exists */
#define HAVE_ERRNO_H 1
/* Define if malloc.h exists */
#define HAVE_MALLOC_H 1
/* Define if sys/wait.h exists */
#define HAVE_SYS_WAIT_H 1
/* Define if sys/select.h exists */
#define HAVE_SYS_SELECT_H 1
/* Define if sys/rusage.h exists */
/* #undef HAVE_SYS_RUSAGE_H */
/* Define if Big Endian */
/* #undef WORDS_BIGENDIAN */
/* Define if const is broken */
/* #undef const */
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
/* Define if inline keyword is broken or nonstandard */
/* #undef inline */
/* Define if we need to redef index/bcopy et al to their SYSV counterparts */
/* #undef NEED_INDEX_DCL */
/* Define if we need to declare malloc et al */
/* #undef NEED_MALLOC_DCL */
/* Define if you need to declare vsprintf yourself */
/* #undef NEED_VSPRINTF_DCL */
/* Define if you need to declare sys_errlist yourself */
/* #undef NEED_SYS_ERRLIST_DCL */
/* Define if you need to declare _sys_errlist yourself */
/* #undef NEED_SYS__ERRLIST_DCL */
/* Define if you need to declare perror yourself */
#define NEED_PERROR_DCL 1
/* Define if you need to declare sprintf yourself */
/* #undef NEED_SPRINTF_DCL */
/* Define if you need to declare getrlimit yourself */
/* #undef NEED_GETRLIMIT_DCL */
/* Define if you need to declare getrusage yourself */
/* #undef NEED_GETRUSAGE_DCL */
/* Define if struct linger is defined */
#define HAVE_LINGER 1
/* Define if signal handlers have a struct sigcontext as their third arg */
#define HAVE_STRUCT_SIGCONTEXT 1
/* Define if stdio.h defines lots of extra functions */
#define EXTENDED_STDIO_DCLS 1
/* Define if sys/socket.h defines lots of extra functions */
#define EXTENDED_SOCKET_DCLS 1
/* Define if socklen_t is defined */
#define SOCKLEN_T_DCL 1
/* Define if sys/wait.h defines union wait. */
#define HAVE_UNION_WAIT 1
/* Define if we have system-supplied ndbm routines */
/* #undef HAVE_NDBM */
/* Define if we have system-supplied (old) dbm routines */
/* #undef HAVE_DBM */
/* Define if we may safely include both time.h and sys/time.h */
#define TIME_WITH_SYS_TIME 1
/* Define if sys/time.h exists */
#define HAVE_SYS_TIME_H 1
/* Define if you need to declare gettimeofday yourself */
/* #undef NEED_GETTIMEOFDAY_DCL */
/* Define if you need to declare wait3 yourself */
/* #undef NEED_WAIT3_DCL */
/* Define if you need to declare getpagesize yourself */
/* #undef NEED_GETPAGESIZE_DCL */
/* Define if you have IEEE floating-point formatted numbers */
#define HAVE_IEEE_FP_FORMAT 1
/* Define if your IEEE floating-point library can generate NaN */
#define HAVE_IEEE_FP_SNAN 1
/* Define if your platform computes the integer quotient as the smallest */
/* integer greater than or or equal to the algebraic quotient. For       */
/* example, -9/5 gives -1                                                */
#define SMALLEST_INT_GTE_NEG_QUOTIENT 1
/* Define if the character special file /dev/urandom is present */
#define HAVE_DEV_URANDOM 1
/* Define if fpu_config.h is available. */
#define HAVE_FPU_CONTROL_H 1
/* Define if your system has the in_addr_t type */
#define HAVE_IN_ADDR_T 1

/* ---------------------------------------------------------------------------
 * Setup section:
 *
 * Load system-dependent header files.
 */

#ifdef STDC_HEADERS
# ifdef __STDC__
#  include <stdarg.h>
# else /* __STDC__ */
#  include <varargs.h>
# endif /* __STDC__ */
#include <stdlib.h>
#include <limits.h>
#else
#include <varargs.h>
extern int    qsort(char *, int, int, int (*)());
#endif

#ifdef NEED_MEMORY_H
#include <memory.h>
#endif

#ifdef STDC_HEADERS
#include <string.h>
#ifdef NEED_INDEX_DCL
#define index           strchr
#define rindex          strrchr
#define bcopy(s,d,n)    memmove(d,s,n)
#endif
#else
#include <strings.h>
extern char *   strchr(char *, char);
extern void     bcopy(char *, char *, int);
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#ifndef HAVE_GETPAGESIZE
#ifndef _SC_PAGE_SIZE
#define NM_BLOODY_PAGE_SYMBOL _SC_PAGESIZE
#else
#define NM_BLOODY_PAGE_SYMBOL _SC_PAGE_SIZE
#endif
#endif
#endif

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if defined(HAVE_SETRLIMIT) || defined(HAVE_GETRUSAGE)
#include <sys/resource.h>
#ifdef NEED_GETRUSAGE_DCL
extern int      getrusage(int, struct rusage *);
#endif
#ifdef NEED_GETRLIMIT_DCL
extern int      getrlimit(int, struct rlimit *);
extern int      setrlimit(int, struct rlimit *);
#endif
#endif

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_GETTIMEOFDAY
#ifdef NEED_GETTIMEOFDAY_DCL
extern int gettimeofday(struct timeval *, struct timezone *);
#endif
#endif

#ifdef HAVE_GETDTABLESIZE
extern int getdtablesize(void);
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_WAIT3
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef NEED_WAIT3_DCL
#ifdef HAVE_UNION_WAIT
extern int wait3(union wait *, int, struct rusage *);
#else
extern int wait3(int *, int, struct rusage *);
#endif
#endif
#endif

#include <sys/param.h>
#ifndef HAVE_GETPAGESIZE
#ifdef EXEC_PAGESIZE
#define getpagesize() EXEC_PAGESIZE
#else
#ifdef NBPG
#ifndef CLSIZE
#define CLSIZE 1
#endif /* no CLSIZE */
#define getpagesize() NBPG * CLSIZE
#else /* no NBPG */
#ifdef NBPC
#define getpagesize() NBPC
#else
#define getpagesize() PAGESIZE
#endif /* no NBPC */
#endif /* no NBPG */
#endif /* no EXEC_PAGESIZE */
#else /* we've got a getpagesize() function, whee */
#ifdef NEED_GETPAGESIZE_DECL
extern int getpagesize(void);
#endif /* NEED_GETPAGESIZE_DECL */
#endif /* HAVE_GETPAGESIZE */

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

// Assure that malloc, realloc, and free are defined.
//
#if !defined(STDC_HEADERS) || !defined(MALLOC_IN_STDLIB_H)
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#else
#ifdef NEED_MALLOC_DCL
extern char *malloc(int);
extern char *realloc(char *, int);
extern int   free(char *);
#endif
#endif
#endif

#ifdef NEED_SYS_ERRLIST_DCL
extern char *sys_errlist[];
#endif

#ifndef HAVE_TIMELOCAL

#ifndef HAVE_MKTIME
#define NEED_TIMELOCAL
extern time_t timelocal(struct tm *);
#else
#define timelocal mktime
#endif /* HAVE_MKTIME */

#endif /* HAVE_TIMELOCAL */

#include <sys/types.h>
#include <stdio.h>

#ifndef VMS
#include <fcntl.h>
#else
#include <sys/fcntl.h>
#endif

#ifdef NEED_SPRINTF_DCL
extern char *sprintf(char *, const char *, ...);
#endif

#ifndef EXTENDED_STDIO_DCLS
extern int    fprintf(FILE *, const char *, ...);
extern int    printf(const char *, ...);
extern int    sscanf(const char *, const char *, ...);
extern int    close(int);
extern int    fclose(FILE *);
extern int    fflush(FILE *);
extern int    fgetc(FILE *);
extern int    fputc(int, FILE *);
extern int    fputs(const char *, FILE *);
extern int    fread(void *, size_t, size_t, FILE *);
extern int    fseek(FILE *, long, int);
extern int    fwrite(void *, size_t, size_t, FILE *);
extern pid_t  getpid(void);
extern int    pclose(FILE *);
extern int    rename(char *, char *);
extern time_t time(time_t *);
extern int    ungetc(int, FILE *);
extern int    unlink(const char *);
#endif

#include <sys/socket.h>
#ifndef EXTENDED_SOCKET_DCLS
extern int    accept(int, struct sockaddr *, int *);
extern int    bind(int, struct sockaddr *, int);
extern int    listen(int, int);
extern int    sendto(int, void *, int, unsigned int,
                    struct sockaddr *, int);
extern int    setsockopt(int, int, int, void *, int);
extern int    shutdown(int, int);
extern int    socket(int, int, int);
extern int    select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif

typedef int   dbref;
typedef int   FLAG;
typedef int   POWER;
typedef char  boolexp_type;
typedef char  IBUF[16];

#endif /* AUTOCONF_H */
