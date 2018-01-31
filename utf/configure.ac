dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([Tinymux-UTF 2.12],[tinymux@googlegroups.com])
AC_CONFIG_SRCDIR([ConvertUTF.cpp])
AC_CONFIG_HEADERS([autoconf.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LN_S

# Checks for libraries.
AC_SEARCH_LIBS([iconv],[iconv])

# Checks for header files.
AC_CHECK_HEADERS([memory.h stdlib.h string.h])
AC_CHECK_HEADERS([iconv.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([strchr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
