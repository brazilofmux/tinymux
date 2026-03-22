# Makefile.am for Hydra persistent connection proxy
#
# Standalone binary.  Links against libmux.so + libganl.a.
# Does NOT link against engine.so.

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(SSL) $(OPENSSL_CFLAGS)

bin_PROGRAMS = hydra

hydra_SOURCES = \
    hydra_main.cpp \
    config.cpp \
    hydra_log.cpp \
    front_door.cpp \
    back_door.cpp \
    session_manager.cpp \
    account_manager.cpp \
    telnet_bridge.cpp \
    scrollback.cpp \
    crypto.cpp \
    process_manager.cpp

hydra_CXXFLAGS = $(AM_CXXFLAGS) -fPIC -std=c++17

hydra_LDFLAGS = -L$(top_builddir)/lib \
    -Wl,-rpath,'$$ORIGIN' \
    -Wl,-rpath,$(abs_top_builddir)/lib \
    -pie

hydra_LDADD = -lmux $(top_builddir)/ganl/libganl.a \
    $(top_builddir)/sqlite/libsqlite3.a \
    $(LIBS) $(OPENSSL_LIBS) -lpthread

# Installation — same bin directory as netmux
bindir = $(abs_top_srcdir)/game/bin

install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_top_builddir)/lib/libmux.so .
