# Makefile.am for muxscript script-mode driver
#
# Minimal binary that loads engine.so via COM and runs softcode
# without networking.  Links against libmux.so.

# Pass the same feature macros as src/ and modules/engine so muxscript never
# disagrees with libmux/engine on STUB_SLAVE- or TINYMUX_JIT-gated layout
# (queue-entry/state members).  Omitting them is a silent ABI footgun the day
# mux_main.cpp transitively includes those headers — #1293.
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) $(TINYMUX_JIT) \
    $(SELFCHECK) $(SSL) $(OPENSSL_CFLAGS)

bin_PROGRAMS = muxscript

muxscript_SOURCES = mux_main.cpp
muxscript_CXXFLAGS = $(AM_CXXFLAGS) -fPIC
muxscript_LDFLAGS = -L$(top_builddir)/lib $(LD_RPATH_ORIGIN) -Wl,-rpath,$(abs_top_builddir)/lib
muxscript_LDADD = -lmux $(LIBS)

# Installation
bindir = $(abs_top_srcdir)/game/bin
