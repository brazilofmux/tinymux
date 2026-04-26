# Makefile.am for libmux.so
#
# Core utilities with no game-state dependency.  These files include
# core.h (not externs.h) and are shared between netmux, stubslave,
# and modules.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

AM_CFLAGS = -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11

LIBMUX_SRC = libmux.cpp \
    sha1.cpp svdrand.cpp svdhash.cpp \
    timeutil.cpp timeabsolute.cpp timedelta.cpp date_scan.cpp timezone.cpp \
    utf8_collate.cpp utf8_grapheme.cpp utf8_normalize.cpp utf8tables.cpp \
    strtod.cpp alarm.cpp mathutil.cpp stringutil.cpp alloc.cpp dbutil.cpp

LIBMUX_C_SRC = color_ops.c unicode_tables.c

LIBMUX_CXX_OBJS = $(LIBMUX_SRC:.cpp=.lo)
LIBMUX_C_OBJS = $(LIBMUX_C_SRC:.c=.lo)
LIBMUX_OBJS = $(LIBMUX_CXX_OBJS) $(LIBMUX_C_OBJS)

all-local: libmux.so

# Compile C++ with -fPIC and hidden visibility (only LIBMUX_API/DCL_EXPORT symbols exported).
# -MMD -MF generates per-object dependency files for header tracking.
%.lo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -MMD -MF $(@:.lo=.d) -c -o $@ $<

# Compile C (Ragel-generated color_ops, unicode tables) with -fPIC and hidden visibility.
# LIBMUX_API fallback in color_ops.h / unicode_tables_c.h provides visibility("default").
%.lo: %.c
	$(CC) $(AM_CFLAGS) -I$(top_srcdir)/include $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -MMD -MF $(@:.lo=.d) -c -o $@ $<

-include $(LIBMUX_CXX_OBJS:.lo=.d)
-include $(LIBMUX_C_OBJS:.lo=.d)

# Ragel .rl -> .c — output is read-only to prevent accidental edits.
color_ops.c: color_ops.rl $(top_srcdir)/include/color_ops.h
	chmod u+w $@ 2>/dev/null || true
	ragel -G2 -C -o $@ $<
	chmod a-w $@

libmux.so: $(LIBMUX_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(DYNAMICLIB_CXXFLAGS) $(LIBMUX_SONAME_FLAG) -o $@ $(LIBMUX_OBJS) $(LIBS) $(DL_LIB) $(SQL_LIBS)

BUILT_SOURCES = color_ops.c

CLEANFILES = libmux.so *.lo *.d color_ops.c

EXTRA_DIST = $(LIBMUX_SRC) $(LIBMUX_C_SRC) color_ops.rl
