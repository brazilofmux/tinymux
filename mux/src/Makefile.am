# Makefile.am for TinyMUX 2.14

SUBDIRS = tools ganl . modules

# Shared build flags
AM_CPPFLAGS = -I$(srcdir)/ganl/include $(WOD_REALMS) $(REALITY_LVLS) $(STUB_SLAVE) \
    $(SELFCHECK) $(INLINESQL) $(SQL_INCLUDE) $(SSL) $(OPENSSL_CFLAGS)

# Source files for libmux.so — core utilities with no game-state dependency.
# These files include core.h (not externs.h) and are shared between netmux,
# stubslave, and modules.
#
LIBMUX_SRC = libmux.cpp \
    sha1.cpp svdrand.cpp svdhash.cpp \
    timeutil.cpp timeabsolute.cpp timedelta.cpp timeparser.cpp timezone.cpp \
    utf8_collate.cpp utf8_grapheme.cpp utf8_normalize.cpp utf8tables.cpp \
    strtod.cpp alarm.cpp mathutil.cpp stringutil.cpp alloc.cpp

# Source files for engine.so — game logic with full externs.h dependency.
# These files have no #include "interface.h" and no direct DESC access.
# They call driver functions through conn_bridge.cpp (COM delegation).
#
ENGINE_CXX_SRC = ast.cpp ast_scan.cpp attrcache.cpp boolexp.cpp \
    command.cpp comsys.cpp conf.cpp conn_bridge.cpp cque.cpp create.cpp \
    db.cpp db_rw.cpp engine.cpp engine_com.cpp eval.cpp file_c.cpp flags.cpp \
    funceval.cpp funceval2.cpp functions.cpp funmath.cpp help.cpp htab.cpp \
    local.cpp log.cpp look.cpp mail.cpp match.cpp mguests.cpp \
    move.cpp object.cpp predicates.cpp player.cpp player_c.cpp \
    plusemail.cpp powers.cpp quota.cpp rob.cpp set.cpp \
    session.cpp speech.cpp timer.cpp \
    unparse.cpp vattr.cpp walkdb.cpp wild.cpp wiz.cpp

# Add reality levels if enabled
if REALITY_ENABLED
ENGINE_CXX_SRC += levels.cpp
endif

# SQLite storage backend (always enabled)
ENGINE_CXX_SRC += sqlitedb.cpp sqlite_backend.cpp

# Combined source list (for reference only)
ENGINE_SRC = $(ENGINE_CXX_SRC) sqlite/sqlite3.c

# Source files for netmux (driver) — thin networking shell.
# These files #include "interface.h" and own the DESC struct.
#
DRIVER_SRC = _build.cpp bsd.cpp driver.cpp ganl_adapter.cpp \
    modules.cpp muxcli.cpp net.cpp netaddr.cpp signals.cpp \
    sitemon.cpp telnet.cpp

# Build executables
bin_PROGRAMS = netmux slave

if STUBSLAVE_ENABLED
bin_PROGRAMS += stubslave
endif

# Shared libraries (modules resolve symbols from libmux.so at runtime;
# netmux resolves engine symbols from engine.so at link time).
all-local: libmux.so engine.so

LIBMUX_OBJS = $(LIBMUX_SRC:.cpp=.lo)

# Compile libmux sources with -fPIC (default visibility).
%.lo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -c -o $@ $<

libmux.so: $(LIBMUX_OBJS)
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -o $@ $(LIBMUX_OBJS) $(LIBS) $(DL_LIB) $(SQL_LIBS)

# engine.so — game logic shared library.
# -fvisibility=hidden: all symbols hidden unless marked DCL_EXPORT.
# Only COM front-door functions are exported (mux_Register, etc.).
# Engine uses .eo suffix to distinguish from libmux's .lo objects.
ENGINE_CXX_OBJS = $(ENGINE_CXX_SRC:.cpp=.eo)

%.eo: %.cpp
	$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -c -o $@ $<

# Special rule for sqlite3.c (C, not C++)
sqlite/sqlite3.eo: sqlite/sqlite3.c
	$(CC) $(CFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) $(DYNAMICLIB_CXXFLAGS) -fvisibility=hidden -c -o $@ $<

engine.so: $(ENGINE_CXX_OBJS) sqlite/sqlite3.eo libmux.so
	$(CXX) $(CXXFLAGS) $(DYNAMICLIB_CXXFLAGS) -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_builddir) -o $@ $(ENGINE_CXX_OBJS) sqlite/sqlite3.eo libmux.so $(PCRE2_LIBS)

# netmux driver program — links against libmux.so and engine.so.
# RPATH: $ORIGIN finds .so files next to the binary (installed or symlinked);
# abs_builddir covers running from the build tree before install.
netmux_SOURCES = $(DRIVER_SRC) version.cpp
netmux_CXXFLAGS = $(AM_CXXFLAGS) -fPIC
netmux_LDFLAGS = -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,$(abs_builddir) -pie
netmux_LDADD = engine.so libmux.so ganl/libganl.a $(LIBS) $(SQL_LIBS) $(PCRE2_LIBS) $(OPENSSL_LIBS)

# slave program
slave_SOURCES = slave.cpp
slave_LDADD = $(LIBS)

# stubslave program
stubslave_SOURCES = stubslave.cpp
stubslave_LDADD = libmux.so $(LIBS) $(STUBLIBS)

# Version number routines
# The DEBIAN_BUILD environment variable can be set to create deterministic builds
# Example: DEBIAN_BUILD=1 ./configure && make
version.o: version.cpp FORCE
	if [ "x$(DEBIAN_BUILD)" = "x1" ]; then \
		$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"(Debian build)\"" -DMUX_BUILD_NUM="\"0\"" -c -o $@ $<; \
	else \
		$(CXX) $(CXXFLAGS) $(AM_CPPFLAGS) $(CPPFLAGS) -DMUX_BUILD_DATE="\"`date`\"" -DMUX_BUILD_NUM="\"`sh ./buildnum.sh`\"" -c -o $@ $<; \
	fi

.PHONY: FORCE
FORCE:

# Installation
bindir = $(abs_top_srcdir)/../game/bin

# Custom install-exec-hook to create symlinks
install-exec-hook:
	cd $(DESTDIR)$(bindir) && rm -f dbconvert && ln -s netmux dbconvert
	cd $(DESTDIR)$(bindir) && rm -f libmux.so && $(LN_S) $(abs_builddir)/libmux.so .
	cd $(DESTDIR)$(bindir) && rm -f engine.so && $(LN_S) $(abs_builddir)/engine.so .

# No need for manual dependency tracking - automake handles this now

# Ragel scanner generation
ast_scan.cpp: ast_scan.rl ast.h
	ragel -G2 -o $@ $<

BUILT_SOURCES = ast_scan.cpp

# Clean up additional files
CLEANFILES = libmux.so engine.so *.lo *.eo sqlite/sqlite3.eo

# Additional distributions
EXTRA_DIST = buildnum.sh ast_scan.rl
