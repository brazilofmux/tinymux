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
//#define WINDOWS_SSL

#if (_MSC_VER >= 1400)
// 1400 is Visual C++ 2005
#include <SpecStrings.h>
#endif

// Targeting Windows 2000 or later.
//
#define _WIN32_WINNT 0x0500
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <share.h>
#include <io.h>
#include <process.h>

#else  // WIN32

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
#else
#define PRETEND_DYNALIB
#endif // HAVE_DLOPEN
#if defined(SSL_ENABLED)
#define UNIX_SSL
#define UNIX_DIGEST
#endif // SSL_ENABLED

#endif // WIN32

#ifndef __specstrings
#define __deref_in
#define __deref_in_opt
#define __deref_in_ecount(n)
#define __deref_inout
#define __deref_out
#define __in
#define __in_z
#define __in_ecount(n)
#define __in_opt
#define __inout
#define __inout_ecount_full(n)
#define __out
#define __out_ecount(n)
#define __out_opt
#elif defined(WIN64)
//#define __in_z
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
#else // HAVE_FCNTL_H
#ifdef HAVE_SYS_FCNTL_H
#include <sys/fcntl.h>
#endif // HAVE_SYS_FCNTL_H
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif // HAVE_NETINET_IN_H
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif // HAVE_ARPA_INET_H
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif // HAVE_NETDB_H

#if defined(UNIX_NETWORKING_EPOLL) && defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif // UNIX_NETWORKING_EPOLL && HAVE_SYS_EPOLL_H

#if defined(UNIX_NETWORKING_SELECT) && defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif // UNIX_NETWORKING_SELECT && HAVE_SYS_SELECT_H

#ifdef UNIX_SSL
#include <openssl/ssl.h>
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

typedef int   dbref;
typedef int   FLAG;
typedef int   POWER;
#ifdef REALITY_LVLS
typedef unsigned int RLEVEL;
#endif // REALITY_LVLS
typedef char  boolexp_type;

#define UNUSED_PARAMETER(x) ((void)(x))

/* Compile time options */

#define PLAYER_NAME_LIMIT   22  /* Max length for player names */
#define NUM_ENV_VARS        10  /* Number of env vars (%0 et al) */
#define MAX_ARG             100 /* max # args from command processor */
                                /* values beyond 1000 will cause %= substitutions to fail */
#define MAX_GLOBAL_REGS     36  /* r() registers */

#define OUTPUT_BLOCK_SIZE   16384

/* ---------------------------------------------------------------------------
 * Database R/W flags.
 */

#define MANDFLAGS_V2  (V_LINK|V_PARENT|V_XFLAGS|V_ZONE|V_POWERS|V_3FLAGS|V_QUOTED)
#define OFLAGS_V2     (V_DATABASE|V_ATRKEY|V_ATRNAME|V_ATRMONEY)

#define MANDFLAGS_V3  (V_LINK|V_PARENT|V_XFLAGS|V_ZONE|V_POWERS|V_3FLAGS|V_QUOTED|V_ATRKEY)
#define OFLAGS_V3     (V_DATABASE|V_ATRNAME|V_ATRMONEY)

#define MANDFLAGS_V4  (V_LINK|V_PARENT|V_XFLAGS|V_ZONE|V_POWERS|V_3FLAGS|V_QUOTED|V_ATRKEY)
#define OFLAGS_V4     (V_DATABASE|V_ATRNAME|V_ATRMONEY)

#define OUTPUT_VERSION  4
#ifdef MEMORY_BASED
#define OUTPUT_FLAGS    (MANDFLAGS_V4)
#else // MEMORY_BASED
#define OUTPUT_FLAGS    (MANDFLAGS_V4|OFLAGS_V4)
#endif // MEMORY_BASED

#define UNLOAD_VERSION  4
#define UNLOAD_FLAGS    (MANDFLAGS_V4)

#define MIN_SUPPORTED_VERSION 1
#define MAX_SUPPORTED_VERSION 4

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

// The smallest char array that will hold the longest representation of each
// bit size of integer.
//
#define LONGEST_I8  5   //  "-128"                  or "256"
#define LONGEST_I16 7   //  "-32768"                or "65536"
#define LONGEST_I32 12  //  "-2147483648"           or "4294967296"
#define LONGEST_I64 21  //  "-9223372036854776320"  or "18446744073709552640"

#define I64BUF_SIZE LONGEST_I64

#ifdef WIN32

#define DCL_CDECL  __cdecl
#define DCL_EXPORT __declspec(dllexport)
#define DCL_API    __stdcall

typedef __int64          INT64;
typedef unsigned __int64 UINT64;
#ifndef INT64_C
#define INT64_C(c)       (c ## i64)
#endif // INT64_C
#ifndef UINT64_C
#define UINT64_C(c)      (c ## ui64)
#endif // UINT64_C

#define LOCALTIME_TIME_T_MIN_VALUE 0
#if (_MSC_VER >= 1400)
// 1600 is Visual C++ 10.0 (2010)
// 1500 is Visual C++ 9.0 (2008)
// 1400 is Visual C++ 8.0 (2005)
#define LOCALTIME_TIME_T_MAX_VALUE UINT64_C(32535215999)
#define MUX_ULONG_PTR ULONG_PTR
#define MUX_PULONG_PTR PULONG_PTR
#elif (_MSC_VER >= 1200)
// 1310 is Visual C++ 7.1 (.NET 2003)
// 1300 is Visual C++ 7.0 (.NET 2002)
// 1200 is Visual C++ 6.0 (1998)
#define MUX_ULONG_PTR DWORD
#define MUX_PULONG_PTR LPDWORD
#else
#error TinyMUX Requires at least version 6.0 of Visual C++.
#endif

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
#ifndef INT64_C
#define INT64_C(c)       (c ## ll)
#endif // INT64_C
#ifndef UINT64_C
#define UINT64_C(c)      (c ## ull)
#endif

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

// Develop an unsigned integer type which is the same size as a pointer.
//
#define SIZEOF_UINT_PTR SIZEOF_VOID_P
#if SIZEOF_UNSIGNED_INT == SIZEOF_VOID_P
typedef unsigned int MUX_UINT_PTR;
#elif SIZEOF_UNSIGNED_LONG_LONG == SIZEOF_VOID_P
typedef UINT64 MUX_UINT_PTR;
#elif SIZEOF_UNSIGNED_LONG == SIZEOF_VOID_P
typedef unsigned long MUX_UINT_PTR;
#elif SIZEOF_UNSIGNED_SHORT == SIZEOF_VOID_P
typedef unsigned short MUX_UINT_PTR;
#else
#error TinyMUX cannot find an integer type with same size as a pointer.
#endif

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

extern bool AssertionFailed(const UTF8 *SourceFile, unsigned int LineNo);
#define mux_assert(exp) (void)( (exp) || (AssertionFailed((UTF8 *)__FILE__, __LINE__), 0) )

extern void OutOfMemory(const UTF8 *SourceFile, unsigned int LineNo);
#define ISOUTOFMEMORY(exp) {if (!(exp)) { OutOfMemory((UTF8 *)__FILE__, __LINE__); }}

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

#if defined(HAVE_SETRLIMIT) && defined(RLIMIT_NOFILE)
void init_rlimit(void);
#endif // HAVE_SETRLIMIT RLIMIT_NOFILE

#if defined(WIN32)

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

// IPPROTO_IPV6 is not defined unless _WIN32_WINNT >= 0x0501, so the following hack is necessary.
//
#if !defined(IPPROTO_IPV6)
#define IPPROTO_IPV6        ((IPPROTO)41)
#endif

#if (_MSC_VER <= 1400)
#define AI_ADDRCONFIG   0x00000400
#define AI_NUMERICSERV  0x00000008
#define AI_V4MAPPED     0x00000800
#define IPV6_V6ONLY           27
#endif

#else

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

#endif

class mux_addr;

#define MUX_SOCKADDR mux_sockaddr
class mux_sockaddr
{
public:
    mux_sockaddr();
    mux_sockaddr(const sockaddr *);
    void set_address(mux_addr *ma);

    unsigned short Family() const;
    unsigned short port() const;
    void ntop(UTF8 *sAddress, size_t len) const;

    struct sockaddr *sa();
    const struct sockaddr *saro() const;
    struct sockaddr_in *sai();
    const struct sockaddr_in *sairo() const;
    struct sockaddr_in6 *sai6();
    const struct sockaddr_in6 *sai6ro() const;
    size_t salen() const;
    size_t maxaddrlen() const;
    void get_address(struct in_addr *ia) const;
    void get_address(struct in6_addr *ia6) const;

    bool operator==(const mux_sockaddr &it) const;

private:
    void Clear();
    union
    {
        struct sockaddr      sa;
#if defined(HAVE_SOCKADDR_IN)
        struct sockaddr_in   sai;
#endif
#if defined(HAVE_SOCKADDR_IN6)
        struct sockaddr_in6  sai6;
#endif
    } u{};
};

// Abstract
//
class mux_addr
{
public:
    mux_addr() { }
    virtual ~mux_addr();

    virtual int getFamily() const = 0;
    virtual bool isValidMask(int *pnLeadingBits) const = 0;
    virtual void makeMask(int nLeadingBits) = 0;
    virtual bool clearOutsideMask(const mux_addr &itMask) = 0;
    virtual mux_addr *calculateEnd(const mux_addr &itMask) const = 0;
    virtual bool operator<(const mux_addr &it) const = 0;
    virtual bool operator==(const mux_addr &it) const = 0;
};

class mux_subnet
{
public:
    enum Comparison
    {
        kLessThan,
        kEqual,
        kContains,
        kContainedBy,
        kGreaterThan
    };

    mux_subnet() : m_iaBase(nullptr), m_iaMask(nullptr), m_iaEnd(nullptr) { }
    ~mux_subnet();
    int getFamily() const { return m_iaBase->getFamily(); }
    Comparison compare_to(mux_subnet *msn) const;
    Comparison compare_to(MUX_SOCKADDR *msa) const;
    bool listinfo(UTF8 *sAddress, int *pnLeadingBits) const;

protected:
    mux_addr *m_iaBase;
    mux_addr *m_iaMask;
    mux_addr *m_iaEnd;
    int      m_iLeadingBits;

    friend mux_subnet *parse_subnet(UTF8 *str, dbref player, UTF8 *cmd);
};

mux_subnet *parse_subnet(UTF8 *str, dbref player, UTF8 *cmd);

// IPv4
//
#if defined(HAVE_IN_ADDR)
class mux_in_addr : public mux_addr
{
public:
    mux_in_addr() { }
    mux_in_addr(struct in_addr *ia);
    mux_in_addr(in_addr_t bits);
    virtual ~mux_in_addr();

    int getFamily() const { return AF_INET; }
    bool isValidMask(int *pnLeadingBits) const;
    void makeMask(int nLeadingBits);
    bool clearOutsideMask(const mux_addr &itMask);
    mux_addr *calculateEnd(const mux_addr &itMask) const;
    bool operator<(const mux_addr &it) const;
    bool operator==(const mux_addr &it) const;

private:
    struct in_addr m_ia{};

    friend class mux_sockaddr;
};
#endif

// IPv6
//
#if defined(HAVE_IN6_ADDR)
class mux_in6_addr : public mux_addr
{
public:
    mux_in6_addr() { }
    mux_in6_addr(struct in6_addr *ia6);
    virtual ~mux_in6_addr();

    int getFamily() const { return AF_INET6; }
    bool isValidMask(int *pnLeadingBits) const;
    void makeMask(int nLeadingBits);
    bool clearOutsideMask(const mux_addr &itMask);
    mux_addr *calculateEnd(const mux_addr &itMask) const;
    bool operator<(const mux_addr &it) const;
    bool operator==(const mux_addr &it) const;

private:
    struct in6_addr m_ia6{};

    friend class mux_sockaddr;
};
#endif

typedef struct addrinfo MUX_ADDRINFO;

#endif // !CONFIG_H
