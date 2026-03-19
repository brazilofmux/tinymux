# Makefile.am for netmux driver
#
# Thin networking shell.  These files #include "interface.h" and own
# the DESC struct.  Links against libmux.so + libganl.a.
# engine.so is loaded at runtime via mux_AddModule (COM module).

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/ganl/include -I$(top_srcdir)/sqlite \
    $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) $(TINYMUX_JIT) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

DRIVER_SRC = _build.cpp bsd.cpp driver.cpp driver_bridge.cpp \
    ganl_adapter.cpp modules.cpp muxcli.cpp net.cpp netaddr.cpp \
    signals.cpp sitemon.cpp telnet.cpp websocket.cpp

# Build executables
bin_PROGRAMS = netmux slave

if STUBSLAVE_ENABLED
bin_PROGRAMS += stubslave
endif

# netmux driver — links against libmux.so only.
# RPATH: $$ORIGIN finds .so files next to the binary (installed or symlinked);
# abs_builddir covers running from the build tree before install.
netmux_SOURCES = $(DRIVER_SRC) version.cpp
netmux_CXXFLAGS = $(AM_CXXFLAGS) -fPIC
netmux_LDFLAGS = -L$(top_builddir)/lib -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_top_builddir)/lib -pie
netmux_LDADD = -lmux $(top_builddir)/ganl/libganl.a \
    $(LIBS) $(SQL_LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# slave program
slave_SOURCES = slave.cpp
slave_LDADD = $(LIBS)

# stubslave program
stubslave_SOURCES = stubslave.cpp
stubslave_LDFLAGS = -L$(top_builddir)/lib -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_top_builddir)/lib
stubslave_LDADD = -lmux $(LIBS) $(STUBLIBS)

# Version number routines
# Automake builds version.cpp as netmux-version.o (per-program prefix).
# The DEBIAN_BUILD environment variable can be set to create deterministic builds.
netmux-version.o: version.cpp FORCE
	if [ "x$(DEBIAN_BUILD)" = "x1" ]; then \
		$(CXX) $(netmux_CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"(Debian build)\"" -DMUX_BUILD_NUM="\"0\"" -c -o $@ $<; \
	else \
		$(CXX) $(netmux_CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"`date`\"" -DMUX_BUILD_NUM="\"`sh $(top_srcdir)/buildnum.sh`\"" -c -o $@ $<; \
	fi

.PHONY: FORCE
FORCE:

# Installation
bindir = $(abs_top_srcdir)/game/bin

# Custom install-exec-hook to create symlinks
install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dbconvert && ln -s netmux dbconvert
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_top_builddir)/lib/libmux.so .
	cd $(DESTDIR)$(bindir) && rm -f engine.so && $(LN_S) $(abs_top_builddir)/modules/engine/engine.so .
