AC_INIT([ganl], [0.1], [your@email.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Check for C++ compiler
AC_PROG_CXX
AC_LANG([C++])

# Add required archive tools
AC_PROG_RANLIB
AM_PROG_AR

# Check for C++14 support
m4_ifdef([AX_CXX_COMPILE_STDCXX_17],
  [AX_CXX_COMPILE_STDCXX_17([noext], [mandatory])],
  [AC_MSG_WARN([AX_CXX_COMPILE_STDCXX_17 macro not found, C++17 support may not be enabled])])

# Output the config header
AC_CONFIG_HEADERS([config.h])

# Platform detection
echo "Host system: $host_os"
AC_CANONICAL_HOST
case $host_os in
  linux*)
    AC_DEFINE([PLATFORM_LINUX], [1], [Define if on Linux])
    PLATFORM_SUBDIR="epoll"
    ;;
  freebsd* | netbsd* | openbsd*)
    AC_DEFINE([PLATFORM_BSD], [1], [Define if on BSD])
    PLATFORM_SUBDIR="kqueue"
    ;;
  darwin*)
    AC_DEFINE([PLATFORM_MACOS], [1], [Define if on macOS])
    PLATFORM_SUBDIR="kqueue"
    ;;
  *)
    AC_MSG_WARN([Unsupported platform: $host_os])
    PLATFORM_SUBDIR="generic"
    ;;
esac

# Define conditional compilation based on platform
AM_CONDITIONAL([LINUX], [test "x$PLATFORM_SUBDIR" = "xepoll"])
AM_CONDITIONAL([BSD], [test "x$PLATFORM_SUBDIR" = "xkqueue" -o "x$PLATFORM_SUBDIR" = "xdarwin"])

AC_SUBST([PLATFORM_SUBDIR])

# Check for OpenSSL
AC_CHECK_HEADERS([openssl/ssl.h], [], [AC_MSG_ERROR([OpenSSL headers not found])])
AC_CHECK_LIB([ssl], [SSL_new], [], [AC_MSG_ERROR([OpenSSL library not found])])
AC_CHECK_LIB([crypto], [CRYPTO_new_ex_data], [], [AC_MSG_ERROR([OpenSSL crypto library not found])])

# Configure output files
AC_CONFIG_FILES([
  Makefile
  src/Makefile
  src/common/Makefile
  src/platform/Makefile
  src/platform/epoll/Makefile
  src/platform/factory/Makefile
  src/platform/kqueue/Makefile
  src/platform/select/Makefile
  src/ssl/Makefile
  src/ssl/openssl/Makefile
  src/ssl/factory/Makefile
  src/protocol/Makefile
  src/protocol/telnet/Makefile
  test/Makefile
  test/unit/Makefile
  test/integration/Makefile
  tools/Makefile
  tools/echo_server/Makefile
  tools/stress_client/Makefile
])

AC_OUTPUT
