dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(TinyMUX, 2.7, tinymux@googlegroups.com)
AC_CONFIG_SRCDIR([sample.cpp])
AC_CONFIG_HEADER([autoconf.h])

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

# Checks for header files.
#
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_TIME

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
