/*! \file config.h
 * \brief Compile-time options.
 *
 * Some of these might be okay to change, others aren't really
 * options, and some are portability-related.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ---------------------------------------------------------------------------
 * Setup section:
 *
 * Load system-dependent header files.
 */

#if !defined(STDC_HEADERS)
#error MUX requires standard headers.
#endif

#if defined(WIN32)

#if (_MSC_VER >= 1400)
// 1400 is Visual C++ 2005
#include <SpecStrings.h>
#endif

#define _WIN32_WINNT 0x0400
#define FD_SETSIZE      512
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <richedit.h>
#define MSFTEDIT_CLASS  L"RICHEDIT50W"

#include <share.h>
#include <io.h>
#include <process.h>

#endif // WIN32

#ifndef __specstrings
#define __deref_in
#define __deref_in_opt
#define __deref_in_ecount(n)
#define __deref_inout
#define __deref_out
#define __in
#define __in_ecount(n)
#define __in_opt
#define __inout
#define __inout_ecount_full(n)
#define __out
#define __out_ecount(n)
#define __out_opt
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif // HAVE_UNISTD_H

#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>

#ifdef NEED_MEMORY_H
#include <memory.h>
#endif

#include <string.h>

#ifdef NEED_INDEX_DCL
#define index           strchr
#define rindex          strrchr
#define bcopy(s,d,n)    memmove(d,s,n)
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

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_GETPAGESIZE

#ifdef NEED_GETPAGESIZE_DECL
extern int getpagesize(void);
#endif // NEED_GETPAGESIZE_DECL

#else // HAVE_GETPAGESIZE

#ifdef _SC_PAGESIZE
#define getpagesize() sysconf(_SC_PAGESIZE)
#else // _SC_PAGESIZE

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif // HAVE_SYS_PARAM_H

#ifdef EXEC_PAGESIZE
#define getpagesize() EXEC_PAGESIZE
#else // EXEC_PAGESIZE
#ifdef NBPG
#ifndef CLSIZE
#define CLSIZE 1
#endif // CLSIZE
#define getpagesize() NBPG * CLSIZE
#else // NBPG
#ifdef PAGESIZE
#define getpagesize() PAGESIZE
#else // PAGESIZE
#ifdef NBPC
#define getpagesize() NBPC
#else // NBPC
#define getpagesize() 0
#endif // NBPC
#endif // PAGESIZE
#endif // NBPG
#endif // EXEC_PAGESIZE

#endif // _SC_PAGESIZE
#endif // HAVE_GETPAGESIZE

#ifdef HAVE_ERRNO_H
#include <errno.h>
#else
extern int errno;
#endif

// Assure that malloc, realloc, and free are defined.
//
#if !defined(MALLOC_IN_STDLIB_H)
#if   defined(HAVE_MALLOC_H)
#include <malloc.h>
#ifdef WIN32
#include <crtdbg.h>
#endif // WIN32
#elif defined(NEED_MALLOC_DCL)
extern char *malloc(int);
extern char *realloc(char *, int);
extern int   free(char *);
#endif
#endif

#ifdef NEED_SYS_ERRLIST_DCL
extern char *sys_errlist[];
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif // HAVE_SYS_TYPES_H

#include <stdio.h>

#ifdef HAVE_SYS_FCNTL_H
#include <sys/fcntl.h>
#endif // HAVE_SYS_FCNTL_H

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

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif // HAVE_SYS_SOCKET_H

#ifndef EXTENDED_SOCKET_DCLS
extern int    accept(int, struct sockaddr *, int *);
extern int    bind(int, struct sockaddr *, int);
extern int    listen(int, int);
extern int    sendto(int, void *, int, unsigned int, struct sockaddr *, int);
extern int    setsockopt(int, int, int, void *, int);
extern int    shutdown(int, int);
extern int    socket(int, int, int);
extern int    select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif

#define UNUSED_PARAMETER(x) ((void)(x))

// The smallest char array that will hold the longest representation of each
// bit size of integer.
//
#define LONGEST_I8  5   //  "-128"                  or "256"
#define LONGEST_I16 7   //  "-32768"                or "65536"
#define LONGEST_I32 12  //  "-2147483648"           or "4294967296"
#define LONGEST_I64 21  //  "-9223372036854776320"  or "18446744073709552640"

#define I64BUF_SIZE LONGEST_I64

#ifdef WIN32

// Build Options
//
#define WINDOWS_NETWORKING
#define WINDOWS_SIGNALS
#define WINDOWS_PROCESSES
#define WINDOWS_FILES
#define WINDOWS_DYNALIB
#define WINDOWS_CRYPT
#define WINDOWS_TIME
#define WINDOWS_THREADS
#define WINDOWS_INSTRINSICS
#define TINYMUX_MODULES

#define DCL_CDECL  __cdecl
#define DCL_EXPORT __declspec(dllexport)
#define DCL_API    __stdcall

typedef __int64          INT64;
typedef unsigned __int64 UINT64;
#define INT64_C(c)       (c ## i64)
#define UINT64_C(c)      (c ## ui64)

#define LOCALTIME_TIME_T_MIN_VALUE 0
#if (_MSC_VER >= 1400)
// 1400 is Visual C++ 2005
#define LOCALTIME_TIME_T_MAX_VALUE UINT64_C(32535215999)
#define MUX_ULONG_PTR ULONG_PTR
#define MUX_PULONG_PTR PULONG_PTR
#elif (_MSC_VER >= 1200)
// 1310 is Visual C++ .NET 2003
// 1300 is Visual C++ .NET 2002
// 1200 is Visual C++ 6.0
#define MUX_ULONG_PTR DWORD
#define MUX_PULONG_PTR LPDWORD
#elif
#error Fidget requires at least version 6.0 of Visual C++.
#endif

#define SIZEOF_PATHNAME (_MAX_PATH + 1)
#define SOCKET_WRITE(s,b,n,f) send(s,b,static_cast<int>(n),f)
#define SOCKET_READ(s,b,n,f) recv(s,b,static_cast<int>(n),f)
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
#define mux_close   _close
#define mux_read    _read
#define mux_write   _write
#define mux_lseek   _lseek

#define ENDLINE "\r\n"

#else // WIN32

// Build Options
//
#define UNIX_NETWORKING
#define UNIX_SIGNALS
#define UNIX_PROCESSES
#define UNIX_FILES
#define UNIX_CRYPT
#define UNIX_TIME
#if defined(HAVE_DLOPEN)
#define UNIX_DYNALIB
#define TINYMUX_MODULES
#endif // HAVE_DLOPEN

#if defined(HAVE_SYS_SELECT_H) && defined(HAVE_SELECT)
#define UNIX_NETWORKING_SELECT
#else
#error Platform does not provide select().
#endif

#define DCL_CDECL
#define DCL_EXPORT
#define DCL_API

#ifndef O_BINARY
#define O_BINARY 0
#endif // O_BINARY
typedef int HANDLE;

typedef long long          INT64;
typedef unsigned long long UINT64;
#define INT64_C(c)       (c ## ll)
#define UINT64_C(c)      (c ## ull)

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
#define mux_close   close
#define mux_read    read
#define mux_write   write
#define mux_lseek   lseek

#define ENDLINE "\n"

#endif // WIN32

#define INT64_MAX_VALUE  INT64_C(9223372036854775807)
#define INT64_MIN_VALUE  (INT64_C(-9223372036854775807) - 1)
#define UINT64_MAX_VALUE UINT64_C(0xffffffffffffffff)

#define isTRUE(x) ((x) != 0)

// Find the minimum-sized integer type that will hold 32-bits.
// Promote to 64-bits if necessary.
//
#if SIZEOF_INT == 4
typedef int              INT32;
typedef unsigned int     UINT32;
#define I32BUF_SIZE LONGEST_I32
#ifdef CAN_UNALIGN_INT
#define UNALIGNED32
#endif
#elif SIZEOF_LONG == 4
typedef long             INT32;
typedef unsigned long    UINT32;
#define I32BUF_SIZE LONGEST_I32
#ifdef CAN_UNALIGN_LONG
#define UNALIGNED32
#endif
#elif SIZEOF_SHORT == 4
typedef short            INT32;
typedef unsigned short   UINT32;
#define I32BUF_SIZE LONGEST_I32
#ifdef CAN_UNALIGN_SHORT
#define UNALIGNED32
#endif
#else
typedef INT64            INT32;
typedef UINT64           UINT32;
#define I32BUF_SIZE LONGEST_I64
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
#define I16BUF_SIZE LONGEST_I16
#ifdef CAN_UNALIGN_INT
#define UNALIGNED16
#endif
#elif SIZEOF_LONG == 2
typedef long             INT16;
typedef unsigned long    UINT16;
#define I16BUF_SIZE LONGEST_I16
#ifdef CAN_UNALIGN_LONG
#define UNALIGNED16
#endif
#elif SIZEOF_SHORT == 2
typedef short            INT16;
typedef unsigned short   UINT16;
#define I16BUF_SIZE LONGEST_I16
#ifdef CAN_UNALIGN_SHORT
#define UNALIGNED16
#endif
#else
typedef INT32            INT16;
typedef UINT32           UINT16;
#define I16BUF_SIZE I32BUF_SIZE
#ifdef UNALIGNED32
#define UNALIGNED16
#endif
#endif // SIZEOF INT16
#define INT16_MIN_VALUE  (-32768)
#define INT16_MAX_VALUE  32767
#define UINT16_MAX_VALUE 0xFFFFU

#if LBUF_SIZE < UINT16_MAX_VALUE
typedef UINT16 LBUF_OFFSET;
#elif LBUF_SIZE < UINT32_MAX_VALUE
typedef UINT32 LBUF_OFFSET;
#else
typedef size_t LBUF_OFFSET;
#endif

typedef   signed char INT8;
typedef unsigned char UINT8;
#define I8BUF_SIZE  LONGEST_I8

#ifndef HAVE_IN_ADDR_T
typedef UINT32 in_addr_t;
#endif

typedef UINT8  UTF8;
typedef UINT8 *PUTF8;
#ifdef WIN32
typedef wchar_t UTF16;
#else
typedef UINT16 UTF16;
#endif // WIN32
typedef UINT32 UTF32;

#define T(x)    ((const UTF8 *)x)

#ifndef SMALLEST_INT_GTE_NEG_QUOTIENT
#define LARGEST_INT_LTE_NEG_QUOTIENT
#endif // !SMALLEST_INT_GTE_NEG_QUOTIENT

extern bool AssertionFailed(const WCHAR *SourceFile, unsigned int LineNo);
#define mux_assert(exp) (void)( (exp) || (AssertionFailed((WCHAR *)__FILE__, __LINE__), 0) )

extern void OutOfMemory(const WCHAR *SourceFile, unsigned int LineNo);
#define ISOUTOFMEMORY(exp) {if (!(exp)) { OutOfMemory((WCHAR *)__FILE__, __LINE__); }}

#if defined(__INTEL_COMPILER)
extern "C" unsigned int __intel_cpu_indicator;
#endif

#endif // !CONFIG_H
