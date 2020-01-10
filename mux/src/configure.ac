dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([TinyMUX],[2.12],[tinymux@googlegroups.com])
AC_CONFIG_SRCDIR([game.cpp])
AC_CONFIG_HEADERS([autoconf.h] [modules/autoconf.h] [tools/autoconf.h])

AC_MSG_CHECKING(whether to enable Reality Levels)
AC_ARG_ENABLE(
  [realitylvls],
  AS_HELP_STRING([--enable-realitylvls],[enable Reality  Levels (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      REALITY_LVLS="-DREALITY_LVLS"
      LIBS="-lstdc++"
      REALITY_SRC="levels.cpp"
      REALITY_OBJ="levels.o"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(REALITY_LVLS)
AC_SUBST(LIBS)
AC_SUBST(REALITY_SRC)
AC_SUBST(REALITY_OBJ)

AC_MSG_CHECKING(whether to enable WOD Realms)
AC_ARG_ENABLE(
  [wodrealms],
  AS_HELP_STRING([--enable-wodrealms],[enable WOD Realms (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      WOD_REALMS="-DWOD_REALMS"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(WOD_REALMS)

AC_MSG_CHECKING(whether to enable Memory-Based Database)
AC_ARG_ENABLE(
  [memorybased],
  AS_HELP_STRING([--enable-memorybased],[enable Memory-Based Database (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      MEMORY_BASED="-DMEMORY_BASED"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(MEMORY_BASED)

AC_MSG_CHECKING(whether to enable Stub Slave)
AC_ARG_ENABLE(
  [stubslave],
  AS_HELP_STRING([--enable-stubslave],[enable Stub Slave (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      STUB_SLAVE="-DSTUB_SLAVE"
      STUBSLAVE_TARGET="stubslave"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(STUB_SLAVE)
AC_SUBST(STUBSLAVE_TARGET)

AC_MSG_CHECKING(whether to enable Firan MUX)
AC_ARG_ENABLE(
  [firanmux],
  AS_HELP_STRING([--enable-firanmux],[enable Firan MUX (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      FIRANMUX="-DFIRANMUX"
      FIRANLIBS=""
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(FIRANMUX)
AC_SUBST(FIRANLIBS)

AC_MSG_CHECKING(whether to enable inline MySQL)
AC_ARG_ENABLE(
  [inlinesql],
  AS_HELP_STRING([--enable-inlinesql],[enable inline MySQL support compatible with PennMUSH and TinyMUSH 3.x (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      INLINESQL="-DINLINESQL"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(INLINESQL)

AC_MSG_CHECKING(whether to perform expensive self-checking)
AC_ARG_ENABLE(
  [selfcheck],
  AS_HELP_STRING([--enable-selfcheck],[enable expensive self-checking (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      SELFCHECK="-DSELFCHECK"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(SELFCHECK)

AC_MSG_CHECKING(for location of SQL header files)
AC_ARG_WITH(
  [mysql-include],
  AS_HELP_STRING([--with-mysql-include=PATH],[set the include path for SQL header files]),
  [
      SQL_INCLUDE="-I$withval"
      AC_MSG_RESULT($withval)
  ],
  [
      if test -d "/usr/include/mysql"; then
          bestguessval="/usr/include/mysql"
          SQL_INCLUDE="-I$bestguessval"
          AC_MSG_RESULT($bestguessval)
      elif test -d "/usr/local/include/mysql"; then
          bestguessval="/usr/local/include/mysql"
          SQL_INCLUDE="-I$bestguessval"
          AC_MSG_RESULT($bestguessval)
      else
          AC_MSG_RESULT(Unknown. Use --with-mysql-include with path)
      fi
  ])
AC_SUBST(SQL_INCLUDE)

AC_MSG_CHECKING(for location of SQL library files)
AC_ARG_WITH(
  [mysql-libs],
  AS_HELP_STRING([--with-mysql-libs=PATH],[set the library path for SQL library files]),
  [
      SQL_LIBPATH="-L$withval"
      AC_MSG_RESULT($withval)
  ],
  [
      if test -d "/usr/lib/mysql"; then
          bestguessval="/usr/lib/mysql"
          SQL_LIBPATH="-L$bestguessval"
          AC_MSG_RESULT($bestguessval)
      elif test -d "/usr/local/lib/mysql"; then
          bestguessval="/usr/local/lib/mysql"
          SQL_LIBPATH="-L$bestguessval"
          AC_MSG_RESULT($bestguessval)
      else
          AC_MSG_RESULT(Unknown. Use --with-mysql-libs with path)
      fi
  ])
AC_SUBST(SQL_LIBPATH)

AC_MSG_CHECKING(whether to enable SSL support)
AC_ARG_ENABLE(
  [ssl],
  AS_HELP_STRING([--enable-ssl],[enable SSL encryption support (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      ENABLE_SSL="yes"
      SSL="-DSSL_ENABLED"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(SSL)

AC_MSG_CHECKING(whether to enable deprecated features)
AC_ARG_ENABLE(
  [deprecated],
  AS_HELP_STRING([--enable-deprecated],[enable deprecated features (default is NO)]),
  [
    if test "x$enableval" = "xno"; then
      AC_MSG_RESULT(no)
    else
      DEPRECATED="-DDEPRECATED"
      AC_MSG_RESULT(yes)
    fi
  ],
  [
    AC_MSG_RESULT(no)
  ])
AC_SUBST(DEPRECATED)

# Checks for programs.
#
AC_PROG_CXX
AC_PROG_CXXCPP
AC_PROG_CC
AC_PROG_CPP
AC_CANONICAL_HOST
AC_USE_SYSTEM_EXTENSIONS
m4_include([m4/ax_cxx_compile_stdcxx.m4])
AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])

# Checks for libraries.
#
AC_SEARCH_LIBS([crypt],[crypt])
AC_SEARCH_LIBS([socket],[socket inet])
AC_SEARCH_LIBS([gethostbyname],[socket nsl bind])
AC_SEARCH_LIBS([inet_addr],[nsl])
AC_SEARCH_LIBS([sqrt],[m])
if test "x$ENABLE_SSL" = "xyes"; then
    AC_CHECK_LIB([ssl], [main])
    AC_CHECK_LIB([crypto], [main])
fi

save_LDFLAGS="$LDFLAGS"
save_LIBS="$LIBS"
LDFLAGS="$save_LDFLAGS $SQL_LIBPATH"
AC_CHECK_LIB(mysqlclient, main)
if test "x$ac_cv_lib_mysqlclient_main" = "xyes"; then
  SQL_LIBS="-lmysqlclient -lz"
  AC_DEFINE([HAVE_MYSQL], [], [Define if mysql exists.])
fi
LDFLAGS="$save_LDFLAGS"
LIBS="$save_LIBS"

AC_MSG_CHECKING(for dlopen)
LIBS_SAVE=$LIBS
AC_SEARCH_LIBS([dlopen],[dl],[
    if test "$ac_cv_search_dlopen" != "none required"; then
      DL_LIB="$ac_cv_search_dlopen"
    fi
])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <dlfcn.h>
    ]],[[
    void *mh = dlopen("foo", RTLD_LAZY);
    void *fp = dlsym(mh, "bar");
    void *pe = dlerror();
    int   cc = dlclose(mh);
    ]])],[
    AC_DEFINE([HAVE_DLOPEN], [], [Define if dlopen exists.])
    ],[])
LIBS=$LIBS_SAVE
case "$host" in
*darwin*) DYNAMICLIB_EXT="so"
          DYNAMICLIB_TARGET="libmux.so"
          DYNAMICLIB_CXXFLAGS="-bundle -fPIC"
          ;;
*cygwin*) DYNAMICLIB_EXT="dll"
          DYNAMICLIB_TARGET="libmux.dll"
          DYNAMICLIB_CXXFLAGS="-shared -fPIC"
          ;;
*)        DYNAMICLIB_EXT="so"
          DYNAMICLIB_TARGET="libmux.so"
          DYNAMICLIB_CXXFLAGS="-shared -fPIC"
          ;;
esac
MUX_LIBS="-lmux"
AC_SUBST(DYNAMICLIB_EXT)
AC_SUBST(DYNAMICLIB_CXXFLAGS)
AC_SUBST(DYNAMICLIB_TARGET)
AC_SUBST(MUX_LIBS)
AC_SUBST(SQL_LIBS)
AC_SUBST(DL_LIB)

# Checks for header files.
#
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_TIME
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS(unistd.h stddef.h memory.h string.h errno.h malloc.h sys/select.h sys/epoll.h sys/event.h)
AC_CHECK_HEADERS(fcntl.h limits.h sys/file.h sys/ioctl.h sys/types.h sys/time.h sys/stat.h sys/param.h sys/fcntl.h)
AC_CHECK_HEADERS(fpu_control.h ieeefp.h fenv.h float.h)
AC_CHECK_HEADERS(netinet/in.h arpa/inet.h netdb.h sys/socket.h)
AS_MESSAGE([checking for sys_errlist decl...])
if test $ac_cv_header_errno_h = no; then
    AC_DEFINE([NEED_SYS_ERRLIST_DCL], [], [Define if you need to declare sys_errlist yourself.])
else
    AC_EGREP_HEADER(sys_errlist,errno.h, ,AC_EGREP_HEADER(sys_errlist,stdio.h,,AC_DEFINE(NEED_SYS_ERRLIST_DCL)))
fi

AC_EGREP_HEADER(getpagesize,unistd.h,,AC_DEFINE([NEED_GETPAGESIZE_DCL], [], [Define if you need to declare getpagesize yourself.]))
if test $ac_cv_header_ieeefp_h = yes; then
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <ieeefp.h>
    ]],[[
    int main(int argc, char *argv[])
    {
        fp_prec_t a = fpgetprec();
        fpsetprec(FP_PD);
        return 0;
    }
    ]])],[AC_DEFINE([IEEEFP_H_USEABLE], [], [Define is ieeefp.h is useable.])],[])
fi

if test $ac_cv_header_fenv_h = yes; then
    AC_EGREP_HEADER(fesetprec,fenv.h,AC_DEFINE([HAVE_FESETPREC], [], [Define if fesetprec is available.]))
    AC_EGREP_HEADER(fegetprec,fenv.h,AC_DEFINE([HAVE_FEGETPREC], [], [Define if fegetprec is available.]))
fi

AS_MESSAGE([checking for extended string dcls...])
AC_EGREP_HEADER(rindex,string.h, ,AC_DEFINE([NEED_INDEX_DCL], [], [Define if we need to redefine index/bcopy et. al. to their SYSV counterparts.]))

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <signal.h>]],[[struct sigcontext scp;]])],[AC_DEFINE([HAVE_STRUCT_SIGCONTEXT], [], [Define if signal handlers have a struct sigcontext as their third arg.])],[])

AS_MESSAGE([checking for malloc dcl...])
querymalloc=no
AC_EGREP_HEADER(realloc,stdlib.h,AC_DEFINE([MALLOC_IN_STDLIB_H], [], [Define if malloc/realloc/free are defined in stdlib.h.]),querymalloc=yes)
if test $ac_cv_header_malloc_h = no && test $querymalloc = yes; then
    AC_DEFINE([NEED_MALLOC_DCL], [], [Define if we need to declare malloc et. al.])
else
    AC_EGREP_HEADER(realloc,malloc.h, ,AC_DEFINE(NEED_MALLOC_DCL))
fi
AS_MESSAGE([checking for extended stdio dcls...])
AC_EGREP_HEADER(fread,stdio.h,AC_DEFINE([EXTENDED_STDIO_DCLS], [], [Define if stdio.h defines lots of extra functions.]))
AS_MESSAGE([checking for extended socket dcls...])
AC_EGREP_HEADER(setsockopt,sys/socket.h,AC_DEFINE([EXTENDED_SOCKET_DCLS], [], [Define if sys/socket.h defines lots of extra functions.]))
AS_MESSAGE([checking for gettimeofday dcl...])
if test $ac_cv_header_time = yes && test $ac_cv_header_sys_time_h = yes ; then
AC_EGREP_HEADER(gettimeofday,[time.h>
#include <sys/time.h], ,AC_DEFINE([NEED_GETTIMEOFDAY_DCL], [], [Define if you need to declare gettimeofday yourself.]))
elif test $ac_cv_header_sys_time_h = yes ; then
AC_EGREP_HEADER(gettimeofday,sys/time.h, ,AC_DEFINE(NEED_GETTIMEOFDAY_DCL))
else
AC_EGREP_HEADER(gettimeofday,time.h, ,AC_DEFINE(NEED_GETTIMEOFDAY_DCL))
fi
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$save_CPPFLAGS $SQL_INCLUDE"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <mysql.h>]], [[int x = 1;]])],[AC_DEFINE([HAVE_MYSQL_H], [], [Define if mysql.h exists.])],[])
CPPFLAGS="$save_CPPFLAGS"

# Checks for typedefs, structures, and compiler characteristics.
#
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_TM
AC_STRUCT_TIMEZONE
AC_C_CONST
AC_C_INLINE
AC_CHECK_SIZEOF([short])
AC_CHECK_SIZEOF([unsigned short])
AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([unsigned int])
AC_CHECK_SIZEOF([long])
AC_CHECK_SIZEOF([unsigned long])
AC_CHECK_SIZEOF([long long])
AC_CHECK_SIZEOF([unsigned long long])
AC_CHECK_SIZEOF([void *])
AS_MESSAGE([checking for socklen_t dcls...])
AC_EGREP_HEADER(socklen_t,sys/socket.h,AC_DEFINE([SOCKLEN_T_DCL], [], [Define if socklen_t is defined.]))
AC_CHECK_DECLS([sys_siglist],[],[],[#include <signal.h>
/* NetBSD declares sys_siglist in unistd.h.  */
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
])
AC_MSG_CHECKING(for sys_signame decl)
AC_EGREP_HEADER(sys_signame,signal.h,AC_DEFINE([HAVE_SYS_SIGNAME], [], [Define if sys_signame[] exists.]) AC_MSG_RESULT(yes),AC_MSG_RESULT(no))
AS_MESSAGE([checking for getrusage dcl...])
AC_EGREP_HEADER(getrusage,sys/resource.h, ,AC_DEFINE([NEED_GETRUSAGE_DCL], [], [Define if you need to declare getrusage yourself.]))
AS_MESSAGE([checking for getrlimit dcl...])
AC_EGREP_HEADER(getrlimit,sys/resource.h, ,AC_DEFINE([NEED_GETRLIMIT_DCL], [], [Define if you need to declare getrlimit yourself.]))
AC_C_BIGENDIAN(AC_DEFINE([WORDS_BIGENDIAN], [], [Define if Big Endian.]),AC_DEFINE([WORDS_LITTLEENDIAN], [], [Define if Little Endian.]),AC_DEFINE([WORDS_UNKNOWN], [], [Define if Unknown Endian.]))
AC_MSG_CHECKING([for in_addr_t])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[in_addr_t foo;]])],[AC_DEFINE([HAVE_IN_ADDR_T], [], [Define if your system has the in_addr_t type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for in_addr])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[struct in_addr ia;]])],[AC_DEFINE([HAVE_IN_ADDR], [], [Define if your system has the in_addr type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for sockaddr_in])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[struct sockaddr_in sa;]])],[AC_DEFINE([HAVE_SOCKADDR_IN], [], [Define if your system has the sockaddr_in type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for in6_addr])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[struct in6_addr i6a;]])],[AC_DEFINE([HAVE_IN6_ADDR], [], [Define if your system has the in6_addr type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for sockaddr_in6])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[struct sockaddr_in6 sa6;]])],[AC_DEFINE([HAVE_SOCKADDR_IN6], [], [Define if your system has the sockaddr_in6 type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])
AC_MSG_CHECKING([for sockaddr_storage])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif]],[[struct sockaddr_storage ss;]])],[AC_DEFINE([HAVE_SOCKADDR_STORAGE], [], [Define if your system has the sockaddr_storage type.])
   AC_MSG_RESULT(yes)],[AC_MSG_RESULT(no)])

# Checks for library functions.
#
AC_FUNC_VPRINTF
AC_FUNC_FORK
AC_CHECK_FUNCS(crypt getdtablesize gethostbyaddr gethostbyname getnameinfo getaddrinfo inet_ntop inet_pton getpagesize getrusage gettimeofday)
AC_CHECK_FUNCS(localtime_r nanosleep select setitimer setrlimit socket srandom tzset usleep log2)
AC_CHECK_FUNCS(epoll_create epoll_ctl epoll_wait kqueue kevent)
AC_CHECK_FUNCS(EVP_MD_CTX_create EVP_MD_CTX_new SHA_Init)
AS_MESSAGE([checking for pread and pwrite...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
main() { pwrite(0, "abc", 3, 0); exit(0); }]])],[pwrite_works=yes],[pwrite_works=no],[])

if test $pwrite_works = yes; then
  AC_DEFINE([HAVE_PREAD], [], [Define if pread exists.])
  AC_DEFINE([HAVE_PWRITE], [], [Define if pwrite exists.])
fi

# Platform behaviors
#
AS_MESSAGE([checking for signal SIGCHLD braindamage...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <signal.h>

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

int rlev;

RETSIGTYPE sighand(int sig, int code)
{
  int stat;

  if (rlev++ > 2)
    exit(1);
  signal(SIGCHLD, sighand);
  wait(&stat);
  return;
}

int main(int argc, char *argv[])
{
  rlev = 0;
  signal(SIGCHLD, sighand);
  if (fork()) {
    sleep(10);
  } else {
    sleep(2);
    exit(1);
  }
  exit(0);
}
]])],[],[AC_DEFINE([SIGNAL_SIGCHLD_BRAINDAMAGE], [], [Define if calling signal with SIGCHLD when handling SIGCHLD blows chow.])],[AC_DEFINE(SIGNAL_SIGCHLD_BRAINDAMAGE)])

AS_MESSAGE([checking for IEEE floating-point format...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   const double d_in = -1e-125;
   const unsigned long long i64_out = 0xA5FB13AC9AAF4C0Full;
   union
   {
       unsigned long long i64;
       double             d;
   } u;
   u.d = d_in;
   if (u.i64 == i64_out)
   {
       return 0;
   }
   return 1;
}
]])],[AC_DEFINE([HAVE_IEEE_FP_FORMAT], [], [Define if you have IEEE floating-point formatted numbers.])],[AC_DEFINE([NO_IEEE_FP_FORMAT], [], [Define if you do not have IEEE floating-point formatted numbers.])],[AC_DEFINE(NO_IEEE_FP_FORMAT)])
AS_MESSAGE([checking for IEEE floating-point exception handling...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <signal.h>
double rZero = 0.0;
int main(int argc, char *argv[])
{
   double d_in;
   const unsigned long long i64_out = 0x7FF0000000000000ull;
   union
   {
       unsigned long long i64;
       double             d;
   } u;
   signal(SIGFPE, SIG_IGN);
   d_in = 1.0/rZero;
   u.d = d_in;
   if (u.i64 == i64_out)
   {
      return 0;
   }
   return 1;
}
]])],[AC_DEFINE([HAVE_IEEE_FP_SNAN], [], [Define if your IEEE floating-point library can generate NaN.])],[AC_DEFINE([NO_IEEE_FP_SNAN], [], [Define if your IEEE floating-point library cannot generate NaN.])],[AC_DEFINE(NO_IEEE_FP_SNAN)])
AS_MESSAGE([checking for how division/moduli of negative quotients are handled...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   int top = -9;
   int bot = 5;
   int quotient = top/bot;
   if (quotient == -1)
   {
       return 0;
   }
   return 1;
}
]])],[AC_DEFINE([SMALLEST_INT_GTE_NEG_QUOTIENT], [], [Define if platform computes integer quotient as the smallest integer greater than or equal to the algebraic quotient.])],[AC_DEFINE([LARGEST_INT_LTE_NEG_QUOTIENT], [], [Define if platform computes integer quotient as the largest integer less than or equal to the algebraic quotient.])],[AC_DEFINE(LARGEST_INT_LTE_NEG_QUOTIENT)])

AS_MESSAGE([checking whether unaligned 'short' access is permitted...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   char foo[sizeof(short)+1];
   short  *ps = (short *)(foo+1);
   *ps = 0;
   return 0;
}
]])],[AC_DEFINE([CAN_UNALIGN_SHORT], [], [Define if unaligned short access is allowed.])],[],[])
AS_MESSAGE([checking whether unaligned 'int' access is permitted...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   char foo[sizeof(int)+1];
   int  *pi = (int *)(foo+1);
   *pi = 0;
   return 0;
}
]])],[AC_DEFINE([CAN_UNALIGN_INT], [], [Define if unaligned int access is allowed.])],[],[])
AS_MESSAGE([checking whether unaligned 'long' access is permitted...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   char foo[sizeof(long)+1];
   long *pl = (long *)(foo+1);
   *pl = 0;
   return 0;
}
]])],[AC_DEFINE([CAN_UNALIGN_LONG], [], [Define if unaligned long access is allowed.])],[],[])
AS_MESSAGE([checking whether unaligned 'long long' access is permitted...])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
int main(int argc, char *argv[])
{
   char foo[sizeof(long long)+1];
   long long *pll = (long long *)(foo+1);
   *pll = 0;
   return 0;
}
]])],[AC_DEFINE([CAN_UNALIGN_LONGLONG], [], [Define if unaligned long long access is allowed.])],[],[])

AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>]],[[struct linger ling; ling.l_onoff = 1;]])],[AC_DEFINE([HAVE_LINGER], [], [Define if struct linger is defined.])],[])
AS_MESSAGE([checking for /dev/urandom...])
if test -c /dev/urandom; then
  AC_DEFINE([HAVE_DEV_URANDOM], [], [Define if the character special file /dev/urandom is present.])
fi

AC_CONFIG_FILES([modules/Makefile tools/Makefile Makefile])
AC_OUTPUT
