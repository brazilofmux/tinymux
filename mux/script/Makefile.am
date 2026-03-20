# Makefile.am for muxscript script-mode driver
#
# Minimal binary that loads engine.so via COM and runs softcode
# without networking.  Links against libmux.so.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) \
    $(SELFCHECK) $(SSL) $(OPENSSL_CFLAGS)

bin_PROGRAMS = muxscript

muxscript_SOURCES = mux_main.cpp
muxscript_CXXFLAGS = $(AM_CXXFLAGS) -fPIC
muxscript_LDFLAGS = -L$(top_builddir)/lib -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_top_builddir)/lib
muxscript_LDADD = -lmux $(LIBS)

# Installation
bindir = $(abs_top_srcdir)/game/bin
