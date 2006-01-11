// config.h
//
// $Id: config.h,v 1.19 2006-01-11 06:52:09 sdennis Exp $
//

#ifndef CONFIG_H
#define CONFIG_H

/* Compile time options */

#define SIDE_EFFECT_FUNCTIONS   /* Those neat funcs that should be commands */

#define PLAYER_NAME_LIMIT   22  /* Max length for player names */
#define NUM_ENV_VARS        10  /* Number of env vars (%0 et al) */
#define MAX_ARG             100 /* max # args from command processor */
#define MAX_GLOBAL_REGS     36  /* r() registers */

#define OUTPUT_BLOCK_SIZE   16384

/* ---------------------------------------------------------------------------
 * Database R/W flags.
 */

#define MANDFLAGS  (V_LINK|V_PARENT|V_XFLAGS|V_ZONE|V_POWERS|V_3FLAGS|V_QUOTED)
#define OFLAGS     (V_DATABASE|V_ATRKEY|V_ATRNAME|V_ATRMONEY)

#define OUTPUT_VERSION  1
#ifdef MEMORY_BASED
#define OUTPUT_FLAGS    (MANDFLAGS)
#else // MEMORY_BASED
#define OUTPUT_FLAGS    (MANDFLAGS|OFLAGS)
#endif // MEMORY_BASED

#define UNLOAD_VERSION  1
#define UNLOAD_FLAGS    (MANDFLAGS)

/* magic lock cookies */
#define NOT_TOKEN   '!'
#define AND_TOKEN   '&'
#define OR_TOKEN    '|'
#define LOOKUP_TOKEN    '*'
#define NUMBER_TOKEN    '#'
#define INDIR_TOKEN '@'     /* One of these two should go. */
#define CARRY_TOKEN '+'     /* One of these two should go. */
#define IS_TOKEN    '='
#define OWNER_TOKEN '$'

/* matching attribute tokens */
#define AMATCH_CMD  '$'
#define AMATCH_LISTEN   '^'

/* delimiters for various things */
#define EXIT_DELIMITER  ';'
#define ARG_DELIMITER   '='
#define ARG_LIST_DELIM  ','

/* This token is used to denote a null output delimiter. */

#define NULL_DELIM_VAR  "@@"

/* This is used to indent output from pretty-printing. */

#define INDENT_STR  "  "

/* amount of object endowment, based on cost */
#define OBJECT_ENDOWMENT(cost) (((cost)/mudconf.sacfactor) + mudconf.sacadjust)

/* !!! added for recycling, return value of object */
#define OBJECT_DEPOSIT(pennies) \
    (((pennies) - mudconf.sacadjust)* mudconf.sacfactor)

#ifdef WIN32
#define DCL_CDECL __cdecl
#define DCL_INLINE __inline

typedef __int64          INT64;
typedef unsigned __int64 UINT64;
#define INT64_MAX_VALUE  9223372036854775807i64
#define INT64_MIN_VALUE  (-9223372036854775807i64 - 1)
#define UINT64_MAX_VALUE 0xffffffffffffffffui64

#if (_MSC_VER >= 1400)
// 1400 is Visual C++ 2005
#define TIME_T_MAX_VALUE 32535215999ui64
#define MUX_ULONG_PTR  ULONG_PTR
#define MUX_PULONG_PTR PULONG_PTR
#elif (_MSC_VER >= 1200)
// 1310 is Visual C++ .NET 2003
// 1300 is Visual C++ .NET 2002
// 1200 is Visual C++ 6.0
#define MUX_ULONG_PTR DWORD
#define MUX_PULONG_PTR LPDWORD
#elif
#error TinyMUX Requires at least version 6.0 of Visual C++.
#endif

#define SIZEOF_PATHNAME (_MAX_PATH + 1)
#define SOCKET_WRITE(s,b,n,f) send(s,b,n,f)
#define SOCKET_READ(s,b,n,f) recv(s,b,n,f)
#define SOCKET_CLOSE(s) closesocket(s)
#define IS_SOCKET_ERROR(cc) ((cc) == SOCKET_ERROR)
#define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#define SOCKET_LAST_ERROR (WSAGetLastError())
#define SOCKET_EINTR       (WSAEINTR)
#define SOCKET_EWOULDBLOCK (WSAEWOULDBLOCK)
#define SOCKET_EBADF       (WSAEBADF)

#define popen       _popen
#define pclose      _pclose
#define mux_tzset   _tzset
#define mux_getpid  _getpid
#define mux_open    _open
#define mux_close   _close
#define mux_read    _read
#define mux_write   _write
#define mux_lseek   _lseek

#else // WIN32

#define DCL_CDECL
#define DCL_INLINE inline
#define INVALID_HANDLE_VALUE (-1)
#ifndef O_BINARY
#define O_BINARY 0
#endif // O_BINARY
typedef int HANDLE;

typedef long long          INT64;
typedef unsigned long long UINT64;
#define INT64_MAX_VALUE  9223372036854775807LL
#define INT64_MIN_VALUE  (-9223372036854775807LL - 1)
#define UINT64_MAX_VALUE 0xffffffffffffffffULL

typedef int SOCKET;
#ifdef PATH_MAX
#define SIZEOF_PATHNAME (PATH_MAX + 1)
#else // PATH_MAX
#define SIZEOF_PATHNAME (4095 + 1)
#endif // PATH_MAX
#define SOCKET_WRITE(s,b,n,f) mux_write(s,b,n)
#define SOCKET_READ(s,b,n,f) mux_read(s,b,n)
#define SOCKET_CLOSE(s) mux_close(s)
#define IS_SOCKET_ERROR(cc) ((cc) < 0)
#define IS_INVALID_SOCKET(s) ((s) < 0)
#define SOCKET_LAST_ERROR (errno)
#define SOCKET_EINTR       (EINTR)
#define SOCKET_EWOULDBLOCK (EWOULDBLOCK)
#ifdef EAGAIN
#define SOCKET_EAGAIN      (EAGAIN)
#endif // EAGAIN
#define SOCKET_EBADF       (EBADF)
#define INVALID_SOCKET (-1)
#define SD_BOTH (2)

#define mux_tzset   tzset
#define mux_getpid  getpid
#define mux_open    open
#define mux_close   close
#define mux_read    read
#define mux_write   write
#define mux_lseek   lseek

#endif // WIN32

#define isTRUE(x) ((x) != 0)

// Find the minimum-sized integer type that will hold 32-bits.
// Promote to 64-bits if necessary.
//
#if SIZEOF_INT == 4
typedef int              INT32;
typedef unsigned int     UINT32;
#ifdef CAN_UNALIGN_INT
#define UNALIGNED32
#endif
#elif SIZEOF_LONG == 4
typedef long             INT32;
typedef unsigned long    UINT32;
#ifdef CAN_UNALIGN_LONG
#define UNALIGNED32
#endif
#elif SIZEOF_SHORT == 4
typedef short            INT32;
typedef unsigned short   UINT32;
#ifdef CAN_UNALIGN_SHORT
#define UNALIGNED32
#endif
#else
typedef INT64            INT32;
typedef UINT64           UINT32;
#ifdef CAN_UNALIGN_LONGLONG
#define UNALIGNED32
#endif
#endif // SIZEOF INT32
#define INT32_MIN_VALUE  (-2147483647 - 1)
#define INT32_MAX_VALUE  2147483647
#define UINT32_MAX_VALUE 0xFFFFFFFFU

// Find the minimum-sized integer type that will hold 16-bits.
// Promote to 32-bits if necessary.
//
#if SIZEOF_INT == 2
typedef int              INT16;
typedef unsigned int     UINT16;
#ifdef CAN_UNALIGN_INT
#define UNALIGNED16
#endif
#elif SIZEOF_LONG == 2
typedef long             INT16;
typedef unsigned long    UINT16;
#ifdef CAN_UNALIGN_LONG
#define UNALIGNED16
#endif
#elif SIZEOF_SHORT == 2
typedef short            INT16;
typedef unsigned short   UINT16;
#ifdef CAN_UNALIGN_SHORT
#define UNALIGNED16
#endif
#else
typedef INT32            INT16;
typedef UINT32           UINT16;
#ifdef UNALIGNED32
#define UNALIGNED16
#endif
#endif // SIZEOF INT16
#define INT16_MIN_VALUE  (-32768)
#define INT16_MAX_VALUE  32767
#define UINT16_MAX_VALUE 0xFFFFU

typedef   signed char INT8;
typedef unsigned char UINT8;

#ifndef HAVE_IN_ADDR_T
typedef UINT32 in_addr_t;
#endif

#ifndef SMALLEST_INT_GTE_NEG_QUOTIENT
#define LARGEST_INT_LTE_NEG_QUOTIENT
#endif // !SMALLEST_INT_GTE_NEG_QUOTIENT

extern bool AssertionFailed(const char *SourceFile, unsigned int LineNo);
#define mux_assert(exp) (void)( (exp) || (AssertionFailed(__FILE__, __LINE__), 0) )

extern void OutOfMemory(const char *SourceFile, unsigned int LineNo);
#define ISOUTOFMEMORY(exp) {if (!(exp)) { OutOfMemory(__FILE__, __LINE__); }}

//#define MEMORY_ACCOUNTING

// Memory Allocation Accounting
//
#ifdef MEMORY_ACCOUNTING
extern void *MemAllocate(size_t n, const char *f, int l);
extern void MemFree(void *p, const char *f, int l);
extern void *MemRealloc(void *p, size_t n, const char *f, int l);
#define MEMALLOC(n)          MemAllocate((n), __FILE__, __LINE__)
#define MEMFREE(p)           MemFree((p), __FILE__, __LINE__)
#define MEMREALLOC(p, n)     MemRealloc((p), (n), __FILE__, __LINE__)
#else // MEMORY_ACCOUNTING
#define MEMALLOC(n)          malloc((n))
#define MEMFREE(p)           free((p))
#define MEMREALLOC(p, n)     realloc((p),(n))
#endif // MEMORY_ACCOUNTING

// If it's Hewlett Packard, then getrusage is provided a different
// way.
//
#ifdef hpux
#define HAVE_GETRUSAGE 1
#include <sys/syscall.h>
#define getrusage(x,p)   syscall(SYS_GETRUSAGE,x,p)
#endif // hpux

#if defined(__INTEL_COMPILER)
extern "C" unsigned int __intel_cpu_indicator;
#endif

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
void init_rlimit(void);
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

#endif // !CONFIG_H
