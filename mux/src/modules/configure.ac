dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([TinyMUX],[2.9],[tinymux@googlegroups.com])
AC_CONFIG_SRCDIR([sample.cpp])
AC_CONFIG_HEADER([autoconf.h])

AC_MSG_CHECKING(for location of SQL header files)
AC_ARG_WITH(
  [mysql-include],
  AS_HELP_STRING([--with-mysql-include=PATH],[set the include path for SQL header files]),
  [
      SQL_INCLUDE="-I$withval"
      AC_MSG_RESULT($withval)
  ],
  [
      SQL_INCLUDE="-I/usr/include/mysql"
      AC_MSG_RESULT(/usr/include/mysql)
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
      SQL_LIBPATH="-L/usr/lib/mysql"
      AC_MSG_RESULT(/usr/lib/mysql)
  ])
AC_SUBST(SQL_LIBPATH)

# Checks for programs.
#
AC_PROG_CXX
AC_PROG_CXXCPP
AC_PROG_CC
AC_PROG_CPP
AC_CANONICAL_HOST
case "$host" in
*irix*) LIBS="-lm" ;;
esac
AC_ISC_POSIX
AC_AIX
AC_MINIX

# Checks for libraries.
#
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
AC_CHECK_LIB(dl,dlopen)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <dlfcn.h>
    ]],[[
    void *mh = dlopen("foo", RTLD_LAZY);
    void *fp = dlsym(mh, "bar");
    void *pe = dlerror();
    int   cc = dlclose(mh);
    ]])],[
    AC_DEFINE([HAVE_DLOPEN], [], [Define if dlopen exists.])
    case "$host" in
    *darwin*) MODULE_EXT="so"
              MODULE_CPPFLAGS="-bundle -O -g"
              ;;
    *)        MODULE_EXT="so"
              MODULE_CPPFLAGS="-shared -fPIC -O -g"
              ;;
    esac
    MUX_LIBS="-lmux"
    ],[])
AC_SUBST(MODULE_EXT)
AC_SUBST(MODULE_CPPFLAGS)
AC_SUBST(MUX_LIBS)
AC_SUBST(SQL_LIBS)

# Checks for header files.
#
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_TIME
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$save_CPPFLAGS $SQL_INCLUDE"
AC_TRY_COMPILE([#include <mysql.h>], [int x = 1;], AC_DEFINE([HAVE_MYSQL_H], [], [Define if mysql.h exists.]))
CPPFLAGS="$save_CPPFLAGS"

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
