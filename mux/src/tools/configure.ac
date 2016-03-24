#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([announce],[2.10],[brazilofmux@gmail.com])
AC_CONFIG_SRCDIR([announce.c])
AC_CONFIG_HEADERS([autoconf.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h netdb.h netinet/in.h stdlib.h sys/param.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
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

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([inet_ntoa socket])

AC_OUTPUT
