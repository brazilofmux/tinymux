# Makefile.am for libmux.so
#
# Core utilities with no game-state dependency.  These files include
# core.h (not externs.h) and are shared between netmux, stubslave,
# and modules.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

LIBMUX_SRC = libmux.cpp \
    sha1.cpp svdrand.cpp svdhash.cpp \
    timeutil.cpp timeabsolute.cpp timedelta.cpp timeparser.cpp timezone.cpp \
    utf8_collate.cpp utf8_grapheme.cpp utf8_normalize.cpp utf8tables.cpp \
    strtod.cpp alarm.cpp mathutil.cpp stringutil.cpp alloc.cpp dbutil.cpp

LIBMUX_OBJS = $(LIBMUX_SRC:.cpp=.lo)

all-local: libmux.so

# Compile with -fPIC (default visibility).
%.lo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -c -o $@ $<

libmux.so: $(LIBMUX_OBJS)
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -Wl,-soname,libmux.so -o $@ $(LIBMUX_OBJS) $(LIBS) $(DL_LIB) $(SQL_LIBS)

CLEANFILES = libmux.so *.lo

EXTRA_DIST = $(LIBMUX_SRC)
